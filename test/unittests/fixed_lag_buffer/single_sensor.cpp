/**
 * Sanity checks to ensure the buffer isn't harmful even though only a single sensor is involved.
 */

#include "gtest/gtest.h"

#include "../utils.hpp"
#include "minimal_latency_buffer/fixed_lag_buffer.hpp"

namespace minimal_latency_buffer::test
{

class FixedLagBufferSingleSource : public ::testing::TestWithParam<std::string>
{
public:
  void SetUp() override
  {
    // parametrization of the buffer is shared across all tests
    params.batch.max_delta = std::chrono::milliseconds(10);

    // create a few measurements
    for (auto i = 1U; i <= 10; i++) {
      // we assume a constant latency of 10ms
      const auto meas_time = Time(std::chrono::milliseconds(50 * i));
      const auto receipt_time = meas_time + std::chrono::milliseconds(10);
      measurements.emplace_back(std::make_unique<Measurement>(meas_time, receipt_time));
    }
  }

  FixedLagBuffer::Params params;
  std::list<MeasurementPtr> measurements;

  // decides how many timestamps (each 1ms) are necessary within the test (depends on the generated measurements)
  constexpr static std::size_t max_test_time = 550;
};


TEST_P(FixedLagBufferSingleSource, InSequenceMeasurements)
{
  using namespace minimal_latency_buffer;

  if (GetParam() == "single") {
    params.mode = BufferMode::SINGLE;
  } else {
    params.mode = BufferMode::BATCH;
  }
  FixedLagBuffer buffer(params);

  // check the behaviour of the buffer for normal 'in-sequence' measurements
  // --> we expect each measurement to be directly available within the next pop() following the push() call
  for (auto i = 0U; i < max_test_time; i++) {
    const auto cur_time = Time(std::chrono::milliseconds(i));

    bool pushed = false;
    if (!measurements.empty() && measurements.front()->_receipt_stamp == cur_time) {
      auto meas = std::move(measurements.front());
      measurements.pop_front();

      const auto receipt_stamp = meas->_receipt_stamp;
      const auto meas_stamp = meas->_meas_stamp;
      const auto status = buffer.push(0, receipt_stamp, meas_stamp, std::move(meas));
      EXPECT_EQ(status, PushReturn::OK);
      pushed = true;
    }

    const auto res = buffer.pop(cur_time);

    EXPECT_EQ(res.discarded_data.size(), 0);
    if (pushed) {
      EXPECT_EQ(res.data.size(), 1);
    } else {
      EXPECT_EQ(res.data.size(), 0);
    }
  }
}

INSTANTIATE_TEST_SUITE_P(FixedLagBufferSingleSource,
                         FixedLagBufferSingleSource,
                         testing::Values("single", "batch"));


}  // namespace min_latency_buffer::test
