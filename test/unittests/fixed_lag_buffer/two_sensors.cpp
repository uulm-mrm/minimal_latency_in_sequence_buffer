#include <iostream>
#include <chrono>
#include "gtest/gtest.h"

#include "../utils.hpp"
#include "minimal_latency_buffer/fixed_lag_buffer.hpp"

using namespace std::chrono_literals;

namespace minimal_latency_buffer::test
{

class FixedLagBufferTwoSources : public ::testing::Test
{
protected:
  void SetUp() override
  {
    params.mode = BufferMode::SINGLE;
    // parametrization of the buffer is shared across all tests
    params.delay_mean = 50ms;
    params.delay_stddev = 10ms;
    params.delay_quantile= 0.99;
  }

  FixedLagBuffer::Params params;
};

TEST_F(FixedLagBufferTwoSources, Single)
{
  FixedLagBuffer buffer(params);

  double delay_quant = boost::math::quantile(boost::math::normal_distribution(params.delay_mean.count()/1e9, params.delay_stddev.count()/1e9), 1 - (1 - params.delay_quantile) /2.0);

  Duration delay{std::chrono::duration_cast<Duration>(std::chrono::duration<double>(delay_quant))};

  // period: 50ms, latency: 10ms
  constexpr auto SENSOR_A = 50U;
  // period: 50ms, latency: 60ms
  constexpr auto SENSOR_B = 100U;

  // two cycles with solely the first sensor
  pop_expect_data(buffer, 10ms, 0);
  push_expect_ok(buffer, SENSOR_A, 60ms, 50ms);
  pop_expect_data(buffer, 60ms, 0);

  // requesting data again with the same current time shouldn't deliver anything new
  pop_expect_data(buffer, 60ms, 0);

  pop_expect_data(buffer, 61ms, 0);

  push_expect_ok(buffer, SENSOR_A, 110ms, 100ms);
  push_expect_ok(buffer, SENSOR_B, 110ms, 60ms);
  pop_expect_data(buffer, 110ms, 0);

  pop_expect_data(buffer, 50ms + delay, 1);

  pop_expect_data(buffer, 100ms + delay, 2);
}

TEST_F(FixedLagBufferTwoSources, BatchingLateIncomming)
{
  params.mode = BufferMode::BATCH;
  params.batch.max_delta = std::chrono::milliseconds(10);
  FixedLagBuffer buffer(params);

  double delay_quant = boost::math::quantile(boost::math::normal_distribution(params.delay_mean.count()/1e9, params.delay_stddev.count()/1e9), 1 - (1 - params.delay_quantile) / 2.0);

  Duration delay{std::chrono::duration_cast<Duration>(std::chrono::duration<double>(delay_quant)) + params.batch.max_delta};


  constexpr auto SENSOR_A = 50U;
  constexpr auto SENSOR_B = 100U;

  // two cycles with solely the first sensor
  pop_expect_data(buffer, 10ms, 0);
  push_expect_ok(buffer, SENSOR_A, 60ms, 50ms);
  pop_expect_data(buffer, 60ms, 0);

  // requesting data again with the same current time shouldn't deliver anything new
  pop_expect_data(buffer, 60ms, 0);

  pop_expect_data(buffer, 61ms, 0);

  push_expect_ok(buffer, SENSOR_A, 110ms, 100ms);
  // sensor b measurement is late for batch with a
  push_expect_ok(buffer, SENSOR_B, 55ms + delay, 60ms);
  pop_expect_data(buffer, 110ms, 0);

  pop_expect_data(buffer, 50ms + delay, 1);

  pop_expect_data(buffer, 100ms + delay, 1);
  pop_expect_data(buffer, 100ms + delay, 1);

  push_expect_ok(buffer, SENSOR_A, 210ms, 200ms);
  // sensor b measurement is late for batch with a
  push_expect_ok(buffer, SENSOR_B, 230ms, 195ms);

  pop_expect_data(buffer, 200ms + delay, 2);
}

TEST_F(FixedLagBufferTwoSources, BatchingCloseMeasurements)
{
  params.mode = BufferMode::BATCH;
  params.batch.max_delta = std::chrono::milliseconds(10);
  FixedLagBuffer buffer(params);

  double delay_quant = boost::math::quantile(boost::math::normal_distribution(params.delay_mean.count()/1e9, params.delay_stddev.count()/1e9), 1 - (1 - params.delay_quantile) / 2.0);

  Duration delay{std::chrono::duration_cast<Duration>(std::chrono::duration<double>(delay_quant)) + params.batch.max_delta};


  constexpr auto SENSOR_A = 50U;

  // two cycles with solely the first sensor
  push_expect_ok(buffer, SENSOR_A, 60ms, 50ms);
  push_expect_ok(buffer, SENSOR_A, 61ms, 59ms);

  pop_expect_data(buffer, 60ms + delay, 2);
}

TEST_F(FixedLagBufferTwoSources, Matching)
{
  params.mode = BufferMode::MATCH;
  params.match.reference_stream = 50U;
  params.match.num_streams = 2;

  FixedLagBuffer buffer(params);

  double delay_quant = boost::math::quantile(boost::math::normal_distribution(params.delay_mean.count()/1e9, params.delay_stddev.count()/1e9), 1 - (1 - params.delay_quantile) / 2.0);

  Duration delay{std::chrono::duration_cast<Duration>(std::chrono::duration<double>(delay_quant))};


  constexpr auto SENSOR_A = 50U;
  constexpr auto SENSOR_B = 100U;

  // two cycles with solely the first sensor
  pop_expect_data(buffer, 10ms, 0);
  push_expect_ok(buffer, SENSOR_A, 60ms, 50ms);
  pop_expect_data(buffer, 60ms, 0);

  // requesting data again with the same current time shouldn't deliver anything new
  pop_expect_data(buffer, 60ms, 0);

  pop_expect_data(buffer, 61ms, 0);

  push_expect_ok(buffer, SENSOR_B, 120ms, 60ms);
//  push_expect_ok(buffer, SENSOR_A, 110ms, 100ms);
  pop_expect_data(buffer, 50ms + delay, 2);

  pop_expect_data(buffer, 110ms + delay, 0);

  push_expect_ok(buffer, SENSOR_A, 250ms, 200ms);
  pop_expect_data(buffer, 260ms, 0);
  push_expect_ok(buffer, SENSOR_A, 300ms, 250ms);
  pop_expect_data(buffer, 300ms, 0);
  push_expect_ok(buffer, SENSOR_B, 305ms, 230ms);


  pop_expect_data(buffer, 305ms, 0, 1);
  pop_expect_data(buffer, 250ms + delay, 2, 0);
}

}  // namespace minimal_latency_buffer::test
