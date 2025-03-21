/**
 * Collection of utility functions and definitions for unittests.
 */

#pragma once

#include <memory>

#include <minimal_latency_buffer/minimal_latency_buffer.hpp>
#include <minimal_latency_buffer/fixed_lag_buffer.hpp>

namespace minimal_latency_buffer::test {
// forward declaration
struct Measurement;

// using a unique_ptr ensures that the internal functionality will not copy the data (this could be performance
// critical for large inputs)
using MeasurementPtr = std::unique_ptr<Measurement>;
using MinimalLatencyBuffer = MinimalLatencyBuffer<MeasurementPtr>;
using FixedLagBuffer = FixedLagBuffer<MeasurementPtr>;

struct Measurement
{
  explicit Measurement(Time meas_time_stamp, Time receipt_time_stamp)
    : _meas_stamp(meas_time_stamp), _receipt_stamp(receipt_time_stamp)
  {
  }

  // Note: members are kept public since this is only a utility struct for testing
  Time _meas_stamp;     // Time at which the measurement has been recorded
  Time _receipt_stamp;  // Time at which the measurement has been received
};


template <typename T>
inline void push_expect_ok(std::size_t line_number_called_from,
                           T &buffer, MinimalLatencyBuffer::SourceId_t const id,
                           Duration const receipt_stamp,
                           Duration const meas_stamp) {
  auto const receipt_time = Time(receipt_stamp);
  auto const meas_time = Time(meas_stamp);
  const auto status = buffer.push(id, receipt_time, meas_time, std::make_unique<Measurement>(meas_time, receipt_time));
  EXPECT_EQ(status, PushReturn::OK) << "called from line_number: " << line_number_called_from;
}

template <typename T>
inline typename T::PopReturn_t pop_expect_data(std::size_t line_number_called_from,
                                               T &buffer, Duration const cur_time,
                                               unsigned int num_data = 1, unsigned int num_discarded = 0) {
  auto res = buffer.pop(Time(cur_time));

  EXPECT_EQ(res.data.size(), num_data) << "called from line_number: " << line_number_called_from;
  EXPECT_EQ(res.discarded_data.size(), num_discarded) << "called from line_number: " << line_number_called_from;

  auto acc_placeholder_counter =
      [](std::size_t count, MinimalLatencyBuffer::TimeData_t &element)
      {
        return count + (element.is_placeholder() ? 1 : 0);
      };

  std::size_t num_placeholder = std::accumulate(res.data.begin(), res.data.end(), std::size_t{0}, acc_placeholder_counter);
  EXPECT_EQ(num_placeholder, 0);
  if (num_placeholder != 0)
  {
    std::cout << "output data is placeholder: ";
    for (auto & el : res.data)
    {
      std::cout << el.is_placeholder() << " | ";
    }
    std::cout << std::endl;
  }

  return res;
}
}

#define push_expect_ok(...) push_expect_ok(__LINE__, ##__VA_ARGS__)
#define pop_expect_data(...) pop_expect_data(__LINE__, ##__VA_ARGS__)
