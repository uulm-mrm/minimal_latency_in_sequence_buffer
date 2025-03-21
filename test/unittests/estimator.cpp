#include "gtest/gtest.h"

#include "minimal_latency_buffer/stream_characteristics_estimator.hpp"

namespace minimal_latency_buffer::test
{

template<class Estimator>
void push_update(Estimator &estimator,
                 typename Estimator::Duration const receipt_stamp,
                 typename Estimator::Duration const meas_stamp,
                 const std::size_t num_missing_measurements = 0) {
  auto const receipt_time = typename Estimator::Time(receipt_stamp);
  auto const meas_time = typename Estimator::Time(meas_stamp);
  estimator.update(receipt_time, meas_time, num_missing_measurements);
}

TEST(Estimator, MissingMeasurements)
{
  using namespace minimal_latency_buffer;
  using namespace std::chrono_literals;
  using Estimator = StreamCharacteristicsEstimator<std::chrono::high_resolution_clock, std::chrono::nanoseconds>;

  // we provide the estimator with perfectly aligned measurements in 50ms steps with 10ms latency
  // but omit an input in between
  Estimator estimator(Estimator::Time(60ms), Estimator::Time(50ms));
  push_update(estimator, 110ms, 100ms);
  push_update(estimator, 160ms, 150ms);
  push_update(estimator, 210ms, 200ms);
  push_update(estimator, 260ms, 250ms);
  EXPECT_EQ(estimator.period(), 50ms);
  EXPECT_EQ(estimator.period_stddev(), 0ms);
  EXPECT_EQ(estimator.latency(), 10ms);
  EXPECT_EQ(estimator.latency_stddev(), 0ms);

  // omitting a measurement received at 310ms (with meas stamp 300ms)
  push_update(estimator, 360ms, 350ms, 1);

  EXPECT_EQ(estimator.period(), 50ms);
  EXPECT_EQ(estimator.period_stddev(), 0ms);
  EXPECT_EQ(estimator.latency(), 10ms);
  EXPECT_EQ(estimator.latency_stddev(), 0ms);

  EXPECT_NO_THROW(push_update(estimator, 410ms, 400ms, 2));

  auto diff_time = 50ms;
  auto receive_time = 310ms;
  auto meas_time = 300ms;
  for (std::size_t idx{0}; idx<10;++idx)
  {
    push_update(estimator, 310ms + idx * diff_time, 300ms + idx * diff_time);
  }

  // throws only after a certain amount of updates
  EXPECT_THROW(push_update(estimator, 810ms, 800ms, 10), std::runtime_error);

}

TEST(Estimator, ErrorReportedUsingTracking)
{
  using namespace minimal_latency_buffer;
  using namespace std::chrono_literals;
  using Estimator = StreamCharacteristicsEstimator<std::chrono::high_resolution_clock, std::chrono::nanoseconds>;

  // we provide the estimator with perfectly aligned measurements in 50ms steps with 10ms latency
  // but omit an input in between
  Estimator estimator(Estimator::Time(0ms), Estimator::Time(0ms));

  constexpr std::size_t n_pre_samples{100};
  constexpr std::size_t n_latent_samples{10};

  constexpr auto latency{10ms};
  constexpr auto update_period = 100ms;

  for (std::size_t idx{0}; idx < n_pre_samples; ++idx)
  {
    const auto current_time = idx * update_period;
    push_update(estimator, current_time, current_time);
    EXPECT_LE(estimator.latency(), latency);
    EXPECT_GE(estimator.latency(), 0ms);
  }

  auto offset = n_pre_samples * update_period;

  for (std::size_t idx{0}; idx < n_latent_samples; ++idx)
  {
    const auto current_time = idx * update_period + offset;
    push_update(estimator, current_time + latency, current_time);
    EXPECT_LE(estimator.latency(), latency);
    EXPECT_GE(estimator.latency(), 0ms);
  }

  offset += n_latent_samples * update_period;

  for (std::size_t idx{0}; idx < n_latent_samples; ++idx)
  {
    const auto current_time = idx * update_period + offset;
    push_update(estimator, current_time, current_time);
    EXPECT_LE(estimator.latency(), latency);
    EXPECT_GE(estimator.latency(), 0ms);
  }


}

}  // namespace min_latency_buffer::test
