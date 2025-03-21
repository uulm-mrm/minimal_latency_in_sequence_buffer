#pragma once

#include <chrono>
#include <list>
#include <vector>
#include <algorithm>
#include <ranges>
#include <numeric>
#include <optional>
#include <unordered_map>

#include "minimal_latency_buffer/stream_characteristics_estimator.hpp"
#include "minimal_latency_buffer/types.hpp"

namespace minimal_latency_buffer
{

/**
 * Measurement buffer for ensuring in sequence processing of incoming measurements.
 *
 * Assumptions:
 *  - update period and latency are changing "slow" relative to the measurement frequency
 *  - a source delivers data with increasing time stamps
 *
 * Note: jumps within update period and / or latency are possible, but may lead to suboptimal buffer
 *       performance until the parameter estimation has converged again
 *
 * @tparam Data     Type of the measurement.
 * @tparam SourceId Type used for identifying different source IDs.
 */
template <class Data, class SourceId = std::size_t>
class MinimalLatencyBuffer
{
public:
  // allows to access the underlying types for a buffer instance
  using Data_t = Data;
  using SourceId_t = SourceId;
  using Estimator = StreamCharacteristicsEstimator<Clock,Duration>;

  using IndexList = std::vector<std::size_t>;

  using TimeData_t = TimeData<SourceId, Data>;
  using PopReturn_t = PopReturn<TimeData_t>;
  using MeasTimeComparator_t = MeasTimeComparator<TimeData_t>;

  using MatchingMap_t = MatchingMap<SourceId>;

  struct Params
  {
    BufferMode mode = BufferMode::SINGLE;
    // If the receipt time jumps further into the past than this threshold, the whole buffer is reset
    Duration reset_threshold = std::chrono::seconds(1);

    // confidence to evaluate the measurement update period estimated gauss distribution
    double measurement_confidence_quantile = 0.99;
    // limit the absolute measurement jitter
    Duration max_abs_measurement_jitter = std::chrono::seconds(100);
    // confidence to evaluate the estimated gauss distribution for the wait time (var = meas_var + latency_var)
    double wait_confidence_quantile = 0.99;
    // limit the absolute waiting jitter (meas_var + latency_var)_jitter
    Duration max_abs_wait_jitter = std::chrono::seconds(100);

    // limit the maximal time the buffer waits for a sample (measurement_jitter + latency + latency_jitter)
    Duration max_total_wait_time = std::chrono::seconds(1000);

    BatchParams batch {};
    MatchParams<SourceId> match {};
  };


  explicit MinimalLatencyBuffer(Params params);

  [[nodiscard]] PushReturn push(SourceId id, Time receipt_time, Time meas_time, Data&& data);

  PopReturn_t pop(Time time);

  /**
   * @return Currently stored number of elements.
   */
  [[nodiscard]] std::size_t getNumberOfQueuedElements() const;
  [[nodiscard]] std::size_t total_size() const;  // TODO: remove, only for debugging the correct set-up of placeholders

  /**
   * @return Measurement time of the latest popped message.
   */
  [[nodiscard]] Time getBufferTime() const;

  /**
   * Allows to query the next expected measurement time stamp.
   *
   * In other words: with the confidence score configured within the parameters, no measurement with an older timestamp
   * than the returned time will be received in the future (excluding new sources).
   * @return Earliest quantile of unpopped messages within the measurement time.
   */
  [[nodiscard]] Time getEstimatedBufferTime() const;

  /**
   * @return Oldest reception time across all data that is currently hold back within the buffer.
   */
  [[nodiscard]] Time getEarliestHoldBackReceptionTime() const;

  [[nodiscard]] Duration getEstimatedLatency(SourceId id) const;
  [[nodiscard]] Duration getEstimatedLatencyStddev(SourceId id) const;
  [[nodiscard]] Duration getEstimatedLatencyQuantile(SourceId id, double quantile) const;
  [[nodiscard]] Duration getEstimatedPeriod(SourceId id) const;
  [[nodiscard]] Duration getEstimatedPeriodStddev(SourceId id) const;
  [[nodiscard]] Duration getEstimatedPeriodQuantile(SourceId id, double quantile) const;

  void reset();

protected:
  /**
   * Creates the n-th placeholder starting with the provided measurement time.
   * @param id                Input source id.
   * @param meas_time         Measurement time stamp of the latest sample from the requested source.
   * @param placeholder_index Number of periods the placeholder is located in the future with respect to the provided
   *                          measurement time stamp.
   * @return
   */
  [[nodiscard]] TimeData_t createPlaceholder(SourceId id, Time meas_time, std::size_t placeholder_index = 1) const;
  [[nodiscard]] std::vector<TimeData_t> create_placeholders(TimeData_t& element,
                                                            std::size_t max_number = MAX_INSERTED_PLACEHOLDERS) const;

  IndexList runBatching(IndexList ready_for_output_ids, Time time);
  std::pair<IndexList, IndexList> runMatching(IndexList ready_for_output_ids);

  Params _params;
  std::vector<TimeData_t> _data;
  std::unordered_map<SourceId, Estimator> _source_infos;
  Time _buffer_time = Time{ std::chrono::seconds(0) };   ///< the time of the buffer, i.e., the time of the last msg pop
  Time _current_time = Time{ std::chrono::seconds(0) };  ///< external time


  // maximum number of newly inserted placeholders for a single push (should only be reached in case of bad estimates,
  // e.g. directly after the initialization)
  static constexpr std::size_t MAX_INSERTED_PLACEHOLDERS = 10;
};


//////////////////////////////////////
/// Definition of member functions ///
//////////////////////////////////////

template <class SourceId, class DataT>
MinimalLatencyBuffer<SourceId, DataT>::MinimalLatencyBuffer(Params params) : _params{ params }
{
}

template <class Data, class SourceId>
auto MinimalLatencyBuffer<Data, SourceId>::push(SourceId id, Time receipt_time, Time meas_time, Data&& data)
    -> PushReturn

{
  assert(std::is_sorted(_data.begin(), _data.end(), MeasTimeComparator_t()) && "Data queue is not sorted according to "
                                                                               "meas timestamps!");

  //   data should always be provided in consecutive order with respect to the reception timestamp / requested time
  //   via pop()
  //   --> allow looping of recordings by resetting in case the assumption above is violated
  if (_current_time - receipt_time > _params.reset_threshold)
  {
    reset();
    return PushReturn::RESET;
  }
  _current_time = std::max(_current_time, receipt_time);

  // index within _data to the best matching placeholder
  std::optional<std::size_t> best_ind;
  // number of missed placeholder during best_fit search
  std::size_t num_missed_placeholder{0};

  auto source_estimator_it = _source_infos.find(id);
  if (source_estimator_it == _source_infos.end())
  {
    _source_infos.emplace(id, Estimator{ receipt_time, meas_time });
    _data.push_back(TimeData_t(id, meas_time, receipt_time, meas_time, receipt_time, std::move(data)));
  }
  else
  {
    // minimal matching distance to existing placeholders, best_ind is only set, if a match with less than period/2 is found
    Duration min = source_estimator_it->second.period() / 2;
    // there can only be fitting samples within _data, if a source_info was available
    for (std::size_t i = 0; i < _data.size(); ++i)
    {
      auto& sample = _data.at(i);
      if (sample.id == id and sample.is_placeholder())
      {
        // all placeholder older than the current sample are considered to be missed (best fitting is later subtracted)
        if (sample.meas_time < meas_time)
        {
          ++num_missed_placeholder;
        }
        if (std::chrono::abs(sample.meas_time - meas_time) < min)
        {
          min = std::chrono::abs(sample.meas_time - meas_time);
          best_ind = i;
        }
      }
    }

    if (best_ind)
    {
      // best fit may have been counted incorrectly before
      if (num_missed_placeholder > 0 and meas_time > _data.at(best_ind.value()).meas_time)
      {
        // reduce by the best fitting placeholder which was counted before but is not missed
        --num_missed_placeholder;
      }

      // replace placeholder with provided measurement
      // handling of already created placeholders is done by create_placeholders()
      auto& element = _data.at(best_ind.value());
      element.data = std::move(data);
      element.meas_time = meas_time;
      element.receipt_time = receipt_time;  // mainly for debugging / evaluation
      std::vector<TimeData_t> new_placeholders = create_placeholders(element);
      _data.insert(_data.end(),
                   std::make_move_iterator(new_placeholders.begin()),
                   std::make_move_iterator(new_placeholders.end()));
    }
    else
    {
      // initialize a new element within the queue
      TimeData_t new_element = TimeData_t(id, meas_time, receipt_time, meas_time, receipt_time, std::move(data));
      std::vector<TimeData_t> new_placeholders = create_placeholders(new_element);
      _data.insert(_data.end(),
                   std::make_move_iterator(new_placeholders.begin()),
                   std::make_move_iterator(new_placeholders.end()));
      _data.push_back(std::move(new_element));
    }

    try
    {
      if (not source_estimator_it->second.isInitialized())
      {
        // do not consider num_missed_placeholder if not initialized before
        source_estimator_it->second.update(receipt_time, meas_time);
      }
      else if (best_ind)
      {
        source_estimator_it->second.update(receipt_time, meas_time, num_missed_placeholder);
      }
      else
      {
        // in this case, num_missed_placeholder may be incorrect -> only update latency
        source_estimator_it->second.updateLatencyOnly(receipt_time, meas_time);
      }
    }
    catch (const std::runtime_error &e)
    {
//      std::cout << "Skipping sample for estimator update, due to failure during attempted update: \n" << e.what() << std::endl;
    }

    // delete older no longer needed placeholders
    std::vector<TimeData_t> cleaned_data;
    std::size_t counter{0};
//    std::cout << "deleting old placeholder for " << id << std::endl;
    for (std::size_t i = 0; i < _data.size(); ++i)
    {
      auto& sample = _data.at(i);
      if (sample.id == id and sample.is_placeholder() and sample.meas_time < meas_time)
      {
        ++counter;
        continue;
      }
      cleaned_data.push_back(std::move(sample));
    }
//    std::cout << "skipped " << counter << " data" << std::endl;
    _data = std::move(cleaned_data);
  }

  // Improvement note: could potentially be skipped if insertion of new elements happens at the right place
  std::sort(_data.begin(), _data.end(), MeasTimeComparator_t());

  return PushReturn::OK;
}

template <class Data, class SourceId>
MinimalLatencyBuffer<Data, SourceId>::PopReturn_t MinimalLatencyBuffer<Data, SourceId>::pop(Time time)
{
  assert(std::is_sorted(_data.begin(), _data.end(), MeasTimeComparator_t()) && "Data queue is not sorted according to "
                                                                               "meas timestamps!");

  // assumption: pop and push are only called with increasing time stamps as they should follow some real clock
  if (time < _current_time)
  {
    // error: either pop() or push() have already been called with a later time
    return { _buffer_time, {}, {} };
  }

  // iterate through the queue and pop all elements until we reach the first placeholder
  std::vector<std::size_t> output_inds;
  std::vector<std::size_t> discard_inds;
  std::vector<std::size_t> delete_inds;
  std::vector<TimeData_t> cleaned_data;

  for (std::size_t i = 0; i < _data.size(); ++i)
  {
    TimeData_t& element = _data.at(i);
    // _data may start with samples that are older than our last output --> discard these elements
    // possible if, e.g., we stopped waiting for data, but it was received a little later
    if (element.meas_time < _buffer_time)
    {
      // only delete non placeholder here, placeholders are handeled during push
      if (not element.is_placeholder())
      {
        discard_inds.push_back(i);
        delete_inds.push_back(i);
      }
    }
    else
    {
      if (element.is_placeholder()){
        if (element.receipt_time >= time)
        {
          break;
        }
      }
      else
      {
        if (element.meas_time > time)
        {
//          std::cout << "next element with meas_time: " << element.meas_time << std::endl;
          break;
        }
        else
        {
          output_inds.push_back(i);
        }
      }
    }

    std::vector<TimeData_t> new_placeholders = create_placeholders(element);
    if (not new_placeholders.empty())
    {
      time = std::min(time, new_placeholders.back().meas_time);
    }
    std::move(new_placeholders.begin(), new_placeholders.end(), std::back_inserter(cleaned_data));
  }

  // batch mode handling
  if (_params.mode == BufferMode::BATCH and not output_inds.empty())
  {
    output_inds = runBatching(output_inds, time);
  }
  else if (_params.mode == BufferMode::MATCH and not output_inds.empty())
  {
    // elements which would require deletion are automatically deleted during push/pop since buffer_time advances
    auto out_and_delete = runMatching(output_inds);
    output_inds = std::move(out_and_delete.first);
    std::copy(out_and_delete.second.begin(), out_and_delete.second.end(), std::back_inserter(delete_inds));
    std::move(out_and_delete.second.begin(), out_and_delete.second.end(), std::back_inserter(discard_inds));
  }

  // consider all samples in _data and either output, keep or discard them.
  std::vector<TimeData_t> output;
  output.reserve(output_inds.size());
  // discarded data is only used for debug purposes and allows the user to gain insights
  std::vector<TimeData_t> discarded_data;
  discarded_data.reserve(discard_inds.size());

  for (const std::size_t idx : output_inds)
  {
    output.push_back(std::move(_data.at(idx)));
  }
  for (const std::size_t idx : discard_inds)
  {
    discarded_data.push_back(std::move(_data.at(idx)));
  }

  // all output indices must be deleted as well
  delete_inds.insert(delete_inds.end(), output_inds.begin(), output_inds.end());
  remove_indices(_data, delete_inds.begin(), delete_inds.end());

  // append all generated samples to the data buffer prior to sorting it
  std::move(cleaned_data.begin(), cleaned_data.end(), std::back_inserter(_data));

  std::sort(_data.begin(), _data.end(), MeasTimeComparator_t());
  // advance our internal buffer time to the last output element (if we later receive anything with an earlier
  // measurement time stamp (e.g. new sensor)) we have to discard it because we otherwise would forward an
  // out-of-sequence measurement with respect to the data we already returned
  if (not output.empty())
  {
    _buffer_time = output.back().meas_time;
  }

  return { _buffer_time, std::move(output), std::move(discarded_data) };
}

template <class Data, class SourceId>
MinimalLatencyBuffer<Data,SourceId>::IndexList MinimalLatencyBuffer<Data, SourceId>::runBatching(IndexList ready_for_output_ids, Time time)
{
  const auto batch_start_time = _data.at(ready_for_output_ids.front()).meas_time;

  // check whether it is worth waiting for the next input to form a batch
  bool found_placeholder = false;
  const auto offset_iter = _data.begin() + ready_for_output_ids.back();
  for (auto iter = offset_iter; iter != _data.end(); ++iter)
  {
    if (not iter->is_placeholder())
    {
      continue;
    }

    if (iter->earliest_estimated_meas_time - batch_start_time < _params.batch.max_delta and
        iter->latest_receipt_time > time)
    {
      found_placeholder = true;
      break;
    }
  }

  if (found_placeholder)
  {
    // prevent output of ready data elements
    return {};
  }
  return ready_for_output_ids;
}

template <class Data, class SourceId>
std::pair<typename MinimalLatencyBuffer<Data,SourceId>::IndexList, typename MinimalLatencyBuffer<Data,SourceId>::IndexList> MinimalLatencyBuffer<Data, SourceId>::runMatching(IndexList ready_for_output_ids)
{
//  std::cout << "running matching" << std::endl;

  IndexList tuple_inds;
  IndexList delete_inds;

  // sort output inds to omit full for loops
  std::sort(ready_for_output_ids.begin(), ready_for_output_ids.end());


  //////////////////////////////////////////////////
  // find reference frame (oldest in buffer which may be outputted)
  //////////////////////////////////////////////////
  bool found_ref{false};
  bool found_next_ref{false};
  std::size_t ref_idx{0};
  Time oldest_ref_meas_time{ std::chrono::seconds(0) };
  Time next_ref_meas_time{ std::chrono::seconds(0) };
  for (auto const& idx : ready_for_output_ids)
  {
    const TimeData_t &element = _data.at(idx);
    if (element.id == _params.match.reference_stream)
    {
      if (not found_ref)
      {
//        std::cout << "found valid ref" << std::endl;
        found_ref = true;
        ref_idx = idx;
        oldest_ref_meas_time = element.meas_time;
      }
      else
      {
//        std::cout << "found valid next" << std::endl;
        found_next_ref = true;
        next_ref_meas_time = element.meas_time;
        break;
      }
    }
  }
  if (not found_ref)
  {
//    std::cout << "no valid ref found" << std::endl;
    return {tuple_inds, delete_inds};
  }

  if (not found_next_ref)
  {
    auto est_it = _source_infos.find(_params.match.reference_stream);
    if (est_it != _source_infos.end())
    {
      next_ref_meas_time = oldest_ref_meas_time + est_it->second.period();
    }
  }

//  std::cout << "times: " << oldest_ref_meas_time << " | " << next_ref_meas_time << std::endl;
//  std::cout << "avail outputs: " << ready_for_output_ids.size() << std::endl;
//  std::cout << "avail data: " << _data.size() << std::endl;

  // assumption: no overlapping data within single stream (interval/meas_time)
  // then using the earliest_meas_time is sufficient when considering placeholders

  //////////////////////////////////////////////////
  // check for fitting matches
  //////////////////////////////////////////////////
  MatchingMap_t matching_map;
  MatchMapEntry &ref_el = matching_map[_params.match.reference_stream];
  ref_el.idx = ref_idx;
  ref_el.tau = 0;
  // remember the highest index used in _data
  // later on it is sufficient to start there, since _data is sorted
  std::size_t latest_data_idx{0};
  for (std::size_t out_idx{0}; out_idx < ready_for_output_ids.size(); ++out_idx)
  {
    const std::size_t idx = ready_for_output_ids[out_idx];
    const TimeData_t &element = _data.at(idx);
    latest_data_idx = idx;

    if (element.id == _params.match.reference_stream)
    {
      // omit taking a newer reference, as only the oldest may be considered
      continue;
    }

    auto current_diff = std::chrono::abs(element.meas_time - oldest_ref_meas_time);
    auto next_diff = std::chrono::abs(element.meas_time - next_ref_meas_time);

    // check if it fits better to the next reference sample
    if (next_diff < current_diff)
    {
//      std::cout << "found output sample better fitting next for " << element.id << " at: " << element.meas_time << std::endl;
      // there won't be any other sample fitting to the current ref, since indices have been sorted
      break;
    }

    // compare entry is created at first access
    MatchMapEntry &compare = matching_map[element.id];
    double current_diff_double = std::chrono::duration<double>(current_diff).count();
    if (current_diff_double < compare.tau)
    {
//      std::cout << "taking newer sample for id: " << element.id << " at: " << element.meas_time << std::endl;
      compare.idx = idx;
      compare.tau = current_diff_double;
    }
    else
    {
//      std::cout << "considered sample is not better: " << element.id << " at: " << element.meas_time << std::endl;
//      std::cout << "cur_diff: " << current_diff << std::endl;
//      std::cout << "cur_tau: " << current_diff_double << std::endl;
//      std::cout << "tau: " << compare.tau << std::endl;

    }
  }
  // IMPORTANT: do not return here if not every source has a valid sample!!
  // if not enough sample are received, but no better sample is anticipated data must be deleted
  // by deleting the current reference, everything else is automatically deleted with the next iteration


  // all elements coming after latest_data_idx are not currently available for output -> no diff between placeholder and actual data samples
  // waiting would be required anyway
  bool found_better_sample {false};
  for (std::size_t idx{latest_data_idx+1}; idx < _data.size(); ++idx)
  {
    const TimeData_t &element = _data.at(idx);
    if (element.id == _params.match.reference_stream)
    {
      // omit taking a newer reference, as only the oldest may be considered
      continue;
    }

    auto current_diff = std::chrono::abs(element.meas_time - oldest_ref_meas_time);
    auto next_diff = std::chrono::abs(element.meas_time - next_ref_meas_time);

    if (next_diff < current_diff)
    {
//      std::cout << "found waiting sample better fitting next" << std::endl;
      // there won't be any other sample fitting to the current ref, since indices have been sorted
      break;
//      continue;
    }

    // creating new entries is explicitly indented here
    MatchMapEntry &compare = matching_map[element.id];
    double current_diff_double = std::chrono::duration<double>(current_diff).count();
    if (current_diff_double < compare.tau)
    {
//      std::cout << "found better fitting waiting sample for this for " << element.id << " at: " << element.meas_time << std::endl;
      found_better_sample = true;
      break;
    }
  }

  // IMPORTANT: check if tuple possible before waiting if 'found_better_sample'
  if (matching_map.size() != _source_infos.size())
  {
    // current reference sample must be deleted, as there is no tuple possible (not even anticipated)
    // other entries will be deleted automatically, as soon as another tuple is successfully created
    delete_inds.push_back(ref_idx);
//    std::cout << "tuple impossible; ref sample must be deleted at: " << oldest_ref_meas_time << std::endl;
    return {tuple_inds, delete_inds};
  }

  if (found_better_sample)
  {
//    std::cout << "better to wait" << std::endl;
    return {tuple_inds, delete_inds};
  }

  tuple_inds.reserve(matching_map.size());
  std::transform(matching_map.begin(), matching_map.end(), std::back_inserter(tuple_inds), [] (const auto map_entry) -> std::size_t {return map_entry.second.idx;});

//  std::cout << "output tuple: " << tuple_inds.size() << " | " << matching_map.size() << std::endl;
  return {tuple_inds, delete_inds};
}

template <class Data, class SourceId>
[[nodiscard]] std::size_t MinimalLatencyBuffer<Data, SourceId>::getNumberOfQueuedElements() const
{
  return std::count_if(
      _data.begin(), _data.end(), [](TimeData_t const& time_data) { return not time_data.is_placeholder(); });
}

template <class Data, class SourceId>
[[nodiscard]] std::size_t MinimalLatencyBuffer<Data, SourceId>::total_size() const
{
  return _data.size();
}

template <class Data, class SourceId>
[[nodiscard]] Time MinimalLatencyBuffer<Data, SourceId>::getBufferTime() const
{
  return _buffer_time;
}

template <class Data, class SourceId>
[[nodiscard]] Time MinimalLatencyBuffer<Data, SourceId>::getEstimatedBufferTime() const
{
  if (_data.empty())
  {
    return _buffer_time;
  }

  return _data.front().meas_time;
}

template <class Data, class SourceId>
[[nodiscard]] Time MinimalLatencyBuffer<Data, SourceId>::getEarliestHoldBackReceptionTime() const
{
  Time min_receipt_time = Time::max();

  for (const auto& element : _data)
  {
    if (element.is_placeholder())
    {
      continue;
    }

    min_receipt_time = std::min(min_receipt_time, element.receipt_time);
  }

  return min_receipt_time;
}

template <class Data, class SourceId>
[[nodiscard]] Duration MinimalLatencyBuffer<Data, SourceId>::getEstimatedLatency(SourceId id) const
{
  auto it = _source_infos.find(id);
  if (it != _source_infos.end())
  {
    return it->second.latency();
  }
  return Duration(0);
}

template <class Data, class SourceId>
[[nodiscard]] Duration MinimalLatencyBuffer<Data, SourceId>::getEstimatedLatencyStddev(SourceId id) const
{
  auto it = _source_infos.find(id);
  if (it != _source_infos.end())
  {
    return it->second.latency_stddev();
  }
  return Duration(0);
}

template <class Data, class SourceId>
[[nodiscard]] Duration MinimalLatencyBuffer<Data, SourceId>::getEstimatedLatencyQuantile(SourceId id, double quantile) const
{
  auto it = _source_infos.find(id);
  if (it != _source_infos.end())
  {
    return it->second.latency_quantile(quantile);
  }
  return Duration(0);
}

template <class Data, class SourceId>
[[nodiscard]] Duration MinimalLatencyBuffer<Data, SourceId>::getEstimatedPeriod(SourceId id) const
{
  auto it = _source_infos.find(id);
  if (it != _source_infos.end())
  {
    return it->second.period();
  }
  return Duration(0);
}

template <class Data, class SourceId>
[[nodiscard]] Duration MinimalLatencyBuffer<Data, SourceId>::getEstimatedPeriodStddev(SourceId id) const
{
  auto it = _source_infos.find(id);
  if (it != _source_infos.end())
  {
    return it->second.period_stddev();
  }
  return Duration(0);
}

template <class Data, class SourceId>
[[nodiscard]] Duration MinimalLatencyBuffer<Data, SourceId>::getEstimatedPeriodQuantile(SourceId id,
                                                                                                  double quantile) const
{
  auto it = _source_infos.find(id);
  if (it != _source_infos.end())
  {
    return it->second.period_quantile(quantile);
  }
  return Duration(0);
}

template <class Data, class SourceId>
void MinimalLatencyBuffer<Data, SourceId>::reset()
{
  _data.clear();
  _buffer_time = Time{ std::chrono::seconds(0) };
  _current_time = Time{ std::chrono::seconds(0) };
  _source_infos.clear();
}

template <class Data, class SourceId>
[[nodiscard]] std::vector<typename MinimalLatencyBuffer<Data, SourceId>::TimeData_t>
MinimalLatencyBuffer<Data, SourceId>::create_placeholders(TimeData_t& element, std::size_t max_number) const
{
  // new placeholder elements are only inserted into the queue if the estimator is already properly initialized
  // --> first few measurements of a new sensor might be discarded
  std::vector<TimeData_t> out;
  if (not _source_infos.contains(element.id) or not _source_infos.at(element.id).isInitialized() or
      element.created_placeholder)
  {
    return out;
  }
  element.created_placeholder = true;
  for (auto i = 1U; i <= max_number; i++)
  {
    TimeData_t placeholder = createPlaceholder(element.id, element.meas_time, i);
    placeholder.created_placeholder = true;
    Time const earliest_expected_meas_time = placeholder.earliest_estimated_meas_time;
    out.emplace_back(std::move(placeholder));

    if (earliest_expected_meas_time > _buffer_time)
    {
      out.back().created_placeholder = false;
      break;
    }
  }
  return out;
}

template <class Data, class SourceId>
[[nodiscard]] MinimalLatencyBuffer<Data, SourceId>::TimeData_t
MinimalLatencyBuffer<Data, SourceId>::createPlaceholder(const SourceId id,
                                          const Time meas_time,
                                          const std::size_t placeholder_index) const
{
  // new placeholder elements are only inserted into the queue if the estimator is already properly initialized
  // --> first few measurements of a new sensor might be discarded
  const Estimator &estimator = _source_infos.at(id);
  if (not estimator.isInitialized())
  {
    throw std::runtime_error("creating placeholder failed, base sample is not initialized");
  }

  Duration period_offset = placeholder_index * estimator.period();
  double const period_variance = std::pow(static_cast<double>(estimator.period_stddev().count()), 2);
  const double period_stddev_sum = std::sqrt(placeholder_index * period_variance);

  Duration meas_quantile_limited{0};
  // check necessary because of unit-testing where perfect input timing causes zero standard deviation
  if (period_stddev_sum > 0)
  {

    // Note: the new placeholder is inserted with respect to its worst case expected time ( = left jitter boundary)
    // since evaluated without a mean, the result can be used in "both directions"
    const double meas_quantile = boost::math::quantile(boost::math::normal_distribution(0.0, period_stddev_sum),
                                                              (1 - _params.measurement_confidence_quantile) / 2);
    meas_quantile_limited = std::clamp(
        Duration(static_cast<Duration::rep>(meas_quantile)),
        - _params.max_abs_measurement_jitter,
        _params.max_abs_measurement_jitter
    );
  }

  Duration wait_quantile_limited{0};
  // check necessary because of unit-testing where perfect input timing causes zero standard deviation
  if (estimator.latency_stddev().count() > 0 )
  {
    const double wait_stddev = std::hypot(period_stddev_sum, static_cast<double>(estimator.latency_stddev().count()));
    const double wait_quantile = boost::math::quantile(
        boost::math::normal_distribution(0.0, wait_stddev),
        1 - (1 - _params.wait_confidence_quantile) / 2
    );

    wait_quantile_limited = std::clamp(
        Duration(static_cast<Duration::rep>(wait_quantile)),
        -_params.max_abs_wait_jitter,
        _params.max_abs_wait_jitter
    );
  }

  Time earliest_expected_meas_time = meas_time + period_offset + meas_quantile_limited;

  Time latest_expected_reception_time = meas_time + period_offset + std::min(estimator.latency() + wait_quantile_limited, _params.max_total_wait_time);

  return { .id = id,
           .meas_time = earliest_expected_meas_time,
           .receipt_time = latest_expected_reception_time,
           .earliest_estimated_meas_time = earliest_expected_meas_time,
           .latest_receipt_time = latest_expected_reception_time,
           .data = std::nullopt,
           .created_placeholder = false};
}



}  // namespace min_latency_buffer
