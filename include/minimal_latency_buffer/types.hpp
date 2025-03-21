#pragma once
#include <chrono>
#include <vector>
#include <optional>
#include <unordered_map>

namespace minimal_latency_buffer
{

using Clock = std::chrono::high_resolution_clock;
using Duration = std::chrono::duration<int64_t, std::nano>;
using Time = std::chrono::time_point<Clock, Duration>;

enum class BufferMode
{
  SINGLE,  ///< the buffer delivers data with increasing time stamps as soon as possible
  BATCH,   ///< the buffer tries to batch data, this may introduce an additional delay
  MATCH,   ///< the buffer tries to match data, this may introduce an additional delay
};

enum class PushReturn
{
  OK,
  RESET,
};

template <typename Data>
struct PopReturn
{
  Time buffer_time;
  std::vector<Data> data;
  std::vector<Data> discarded_data;
};

template <typename SourceId, typename Data>
struct TimeData
{
  // id to identify the corresponding data/source stream
  SourceId id;
  // for placeholders, the meas_time is set to the earliest_estimated_meas_time to allow easier handling during push/pop
  Time meas_time = Time{ std::chrono::seconds(0) };
  Time receipt_time = Time{ std::chrono::seconds(0) };
  // when measurements are received, the earliest_estimated meas time stays untouched to give insights later on (debug)
  Time earliest_estimated_meas_time = Time{ std::chrono::seconds(0) };
  // estimation of the latest possible reception time based on the confidence settings
  Time latest_receipt_time = Time{ std::chrono::seconds(0) };
  // placeholder to not contain any data
  std::optional<Data> data;
  // flags if this sample was used to create placeholder (suppress multiple placeholder creation)
  bool created_placeholder = false;

  [[nodiscard]] bool is_placeholder() const
  {
    return not data.has_value();
  }
};

template <typename TimeData>
struct MeasTimeComparator
{
  inline bool operator()(const TimeData& first, const TimeData& second)
  {
    return (first.meas_time < second.meas_time);
  }
};


struct BatchParams
{
  Duration max_delta = std::chrono::milliseconds(10);  ///< the max time delta of a batch
};

template <typename SourceId>
struct MatchParams
{
  SourceId reference_stream{};
  // if not estimated by the buffer, the total number of streams must be specified
  std::size_t num_streams{0};
};

struct MatchMapEntry
{
  std::size_t idx;
  // absolute acquisition time difference
  double tau{std::numeric_limits<double>::max()};
};

template <typename SourceId>
using MatchingMap = std::unordered_map<SourceId, MatchMapEntry>;

//////////////////////////////////////
/// Definition of helper functions ///
//////////////////////////////////////

template <typename T, typename Iter>
void remove_indices(std::vector<T> &vec, Iter idx_begin, Iter idx_end)
{
  using iter = typename std::vector<T>::iterator;

  if(idx_begin == idx_end)
  {
    return;
  }

  std::vector<T> output;
  // calculating num of indices would perhaps be more costly than just reserving enough data for our use case
  output.reserve(vec.size());

  // sort indices to allow blockwise copy
  std::sort(idx_begin, idx_end);

  iter block_start = vec.begin();
  for (auto idx_iter=idx_begin; idx_iter != idx_end; ++idx_iter)
  {
    iter block_end = vec.begin() + *idx_iter;
    if (block_start != block_end)
    {
      std::move(
          std::make_move_iterator(block_start),
          std::make_move_iterator(block_end),
          std::back_inserter(output)
      );
    }
    block_start = block_end + 1;
  }

  if (block_start != vec.end())
  {
    std::move(
        std::make_move_iterator(block_start),
        std::make_move_iterator(vec.end()),
        std::back_inserter(output)
    );
  }

  vec = std::move(output);
}


}