#pragma once

#include <chrono>
#include <iostream>
#include <boost/math/distributions/normal.hpp>

#include "minimal_latency_buffer/types.hpp"

namespace minimal_latency_buffer
{

template <class Data, class SourceId = std::size_t>
class FixedLagBuffer
{
public:

  using Data_t = Data;
  using SourceId_t = SourceId;

  using IndexList = std::vector<std::size_t>;

  using TimeData_t = TimeData<SourceId, Data>;
  using PopReturn_t = PopReturn<TimeData_t>;
  using MeasTimeComparator_t = MeasTimeComparator<TimeData_t>;

  using MatchingMap_t = MatchingMap<SourceId>;

  struct Params
  {
    BufferMode mode = BufferMode::SINGLE;
    // If the receipt time jumps further into the past than this threshold, the whole buffer is reset
    Duration reset_threshold{std::chrono::seconds {0}};

    Duration delay_mean{0};
    Duration delay_stddev{0};
    double delay_quantile{0.5};


    BatchParams batch;
    MatchParams<SourceId> match;
  };

  explicit FixedLagBuffer(Params params);

  PushReturn push(SourceId id, Time receipt_time, Time meas_time, Data&& data);
  PopReturn_t pop(Time time);

  std::pair<IndexList, IndexList> runMatching(IndexList ready_for_output_inds);

  void reset();

  Time getBufferTime() const;
  Time getCurrentTime() const;
  std::size_t getNumberOfQueuedElements() const;


protected:

  Params _params;
  std::vector<TimeData_t> _data;
  Duration _fixed_lag_delay{std::chrono::seconds {0}};
  Time _buffer_time{std::chrono::seconds {0}};
  Time _current_time{std::chrono::seconds {0}};

};

template <class Data, class SourceId>
FixedLagBuffer<Data, SourceId>::FixedLagBuffer(Params params)
: _params{params}
{
  _fixed_lag_delay = _params.delay_mean;
  if (_params.mode == BufferMode::BATCH)
  {
    _fixed_lag_delay += _params.batch.max_delta;
  }
  const double delay_stddev = std::chrono::duration<double>(_params.delay_stddev).count();
  if (delay_stddev > std::numeric_limits<double>::epsilon())
  {
    const double latency_quantile = boost::math::quantile(boost::math::normal_distribution(0.0,delay_stddev),
                                                          1. - (1 - _params.delay_quantile)/2.0);
    const std::chrono::duration<double> latency_quantile_duration(latency_quantile);
    _fixed_lag_delay += std::chrono::duration_cast<Duration>(latency_quantile_duration);
  }
}

template <class Data, class SourceId>
PushReturn FixedLagBuffer<Data, SourceId>::push(SourceId id, minimal_latency_buffer::Time receipt_time, minimal_latency_buffer::Time meas_time, Data&& data)
{
  if (_current_time - receipt_time > _params.reset_threshold)
  {
    reset();
    return PushReturn::RESET;
  }

  TimeData_t new_element{id, meas_time, receipt_time, meas_time, receipt_time, std::move(data)};
  _data.push_back(std::move(new_element));

  std::sort(_data.begin(), _data.end(), MeasTimeComparator_t());

  return PushReturn::OK;
}

template <class Data, class SourceId>
PopReturn<TimeData<SourceId, Data>> FixedLagBuffer<Data, SourceId>::pop(minimal_latency_buffer::Time time)
{
  IndexList output_inds;
  IndexList discard_inds;

  // all messages acquired prior to the ref time are can potentially be outputted
  Time ref_meas_time = time - _fixed_lag_delay;

  for (std::size_t idx{0}; idx < _data.size(); ++idx)
  {
    auto const &element = _data.at(idx);
    if (element.meas_time <= _buffer_time)
    {
      discard_inds.push_back(idx);
    }
    else if (element.meas_time <= ref_meas_time)
    {
      output_inds.push_back(idx);
    }
    else
    {
      // _data is sorted, there must not be another element which is older than ref_meas_time
      break;
    }
  }

  if (_params.mode == BufferMode::BATCH and not output_inds.empty())
  {
    IndexList batch;
    Time oldest_output_time = _data.at(output_inds.front()).meas_time;
    Time batch_reference_time = oldest_output_time + _params.batch.max_delta;

    batch.push_back(output_inds.front());

    // also output data within the batch width, even if they are not as much delayed
    for (std::size_t idx{output_inds.front() + 1}; idx < _data.size(); ++idx)
    {
      auto const &element = _data.at(idx);
      if (element.meas_time < batch_reference_time)
      {
        // sorting is preserved for output here
        batch.push_back(idx);
      }
    }
    output_inds = std::move(batch);
  }
  else if(_params.mode == BufferMode::MATCH and not output_inds.empty())
  {
    auto out_and_delete = runMatching(output_inds);
    output_inds = std::move(out_and_delete.first);
    std::copy(out_and_delete.second.begin(), out_and_delete.second.end(), std::back_inserter(discard_inds));
  }

  PopReturn_t result{};
  for (std::size_t idx : output_inds)
  {
    result.data.push_back(std::move(_data.at(idx)));
  }
  for (std::size_t idx : discard_inds)
  {
    result.discarded_data.push_back(std::move(_data.at(idx)));
  }

  if (not result.data.empty())
  {
    _buffer_time = result.data.back().meas_time;
  }
  result.buffer_time = _buffer_time;

  std::copy(output_inds.begin(), output_inds.end(), std::back_inserter(discard_inds));
  remove_indices(_data, discard_inds.begin(), discard_inds.end());

  std::sort(_data.begin(), _data.end(), MeasTimeComparator_t());

  return result;
}
template <class Data, class SourceId>
std::pair<typename FixedLagBuffer<Data,SourceId>::IndexList, typename FixedLagBuffer<Data,SourceId>::IndexList> FixedLagBuffer<Data, SourceId>::runMatching(IndexList ready_for_output_inds)
{
  IndexList tuple_inds;
  IndexList delete_inds;

  //////////////////////////////////////////////////
  // find reference frame (oldest in buffer which may be outputted)
  //////////////////////////////////////////////////
  bool found_ref{false};
  bool found_next_ref{false};
  std::size_t ref_idx{0};
  Time oldest_ref_meas_time{ std::chrono::seconds(0) };
  Time next_ref_meas_time{ std::chrono::seconds(0) };
  for (auto const& idx : ready_for_output_inds)
  {
    const TimeData_t &element = _data.at(idx);
    if (element.id == _params.match.reference_stream)
    {
      if (not found_ref)
      {
        found_ref = true;
        ref_idx = idx;
        oldest_ref_meas_time = element.meas_time;
      }
      else
      {
        found_next_ref = true;
        next_ref_meas_time = element.meas_time;
        break;
      }
    }
  }
  if (not found_ref)
  {
    return {tuple_inds, delete_inds};
  }

  if (not found_next_ref)
  {
    // search received but not yet ready samples for next ref as well
    for (std::size_t idx{ref_idx+1}; idx < _data.size();++idx)
    {
      auto const &element = _data.at(idx);
      if (element.id == _params.match.reference_stream)
      {
        found_next_ref = true;
        next_ref_meas_time = element.meas_time;
        break;
      }

    }
    if (not found_next_ref)
    {
      // without stream characteristics there is no way of estimating the next ref sample if not already received
      next_ref_meas_time = Time{std::chrono::seconds {0}};
    }
  }

  //////////////////////////////////////////////////
  // check for fitting matches
  //////////////////////////////////////////////////
  MatchingMap_t matching_map;
  MatchMapEntry &ref_el = matching_map[_params.match.reference_stream];
  ref_el.idx = ref_idx;
  ref_el.tau = 0;
  // flags if data was found for a stream fitting better to the next sample AND no other sample for the current ref
  bool found_better_for_next{false};
  for (std::size_t idx{0}; idx < _data.size(); ++idx)
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
      auto map_it=matching_map.find(element.id);
      // no other sample with this id was found before
      if (map_it == matching_map.end())
      {
        found_better_for_next = true;
      }
      // further sample won't fitter better
      break;
    }

    // compare entry is created at first access
    MatchMapEntry &compare = matching_map[element.id];
    double current_diff_double = std::chrono::duration<double>(current_diff).count();
    if (current_diff_double < compare.tau)
    {
      compare.idx = idx;
      compare.tau = current_diff_double;
    }
  }

  // IMPORTANT: check if tuple possible before waiting if 'found_better_sample'
  if (matching_map.size() != _params.match.num_streams)
  {
    if (found_better_for_next)
    {
      // delete current ref since tuple is impossible
      // other entries will be deleted automatically, as soon as another tuple is successfully created
      delete_inds.push_back(ref_idx);
    }

    return {tuple_inds, delete_inds};
  }

  tuple_inds.reserve(matching_map.size());
  std::transform(matching_map.begin(), matching_map.end(), std::back_inserter(tuple_inds), [] (const auto map_entry) -> std::size_t {return map_entry.second.idx;});

  return {tuple_inds, delete_inds};
}

template <class Data, class SourceId>
void FixedLagBuffer<Data, SourceId>::reset()
{
  _data.clear();
  _buffer_time = Time{std::chrono::seconds{0}};
  _current_time = Time{std::chrono::seconds{0}};
}

template <class Data, class SourceId>
Time FixedLagBuffer<Data, SourceId>::getBufferTime() const
{
  return _buffer_time;
}

template <class Data, class SourceId>
Time FixedLagBuffer<Data, SourceId>::getCurrentTime() const
{
  return _current_time;
}

template <class Data, class SourceId>
std::size_t FixedLagBuffer<Data, SourceId>::getNumberOfQueuedElements() const
{
  return _data.size();
}


}


