#include <iostream>
#include <chrono>
#include "gtest/gtest.h"

#include "../utils.hpp"
#include "minimal_latency_buffer/minimal_latency_buffer.hpp"

using namespace std::chrono_literals;

namespace minimal_latency_buffer::test
{

class MinimalLatencyBufferTwoSources : public ::testing::Test
{
protected:
  void SetUp() override
  {
    // parametrization of the buffer is shared across all tests
    params.max_total_wait_time = std::chrono::milliseconds(100);
    params.batch.max_delta = std::chrono::milliseconds(10);
  }

  MinimalLatencyBuffer::Params params;
};

TEST_F(MinimalLatencyBufferTwoSources, lateJoiningSensorWithHigherLatency)
{
  using namespace minimal_latency_buffer;
  using namespace std::chrono_literals;

  MinimalLatencyBuffer buffer(params);

  // period: 50ms, latency: 10ms
  constexpr auto SENSOR_A = 50U;
  // period: 50ms, latency: 60ms
  constexpr auto SENSOR_B = 100U;

  // two cycles with solely the first sensor
  pop_expect_data(buffer, 10ms, 0);
  push_expect_ok(buffer, SENSOR_A, 60ms, 50ms);
  pop_expect_data(buffer, 60ms, 1);

  // requesting data again with the same current time shouldn't deliver anything new
  pop_expect_data(buffer, 60ms, 0);

  pop_expect_data(buffer, 61ms, 0);

  push_expect_ok(buffer, SENSOR_A, 110ms, 100ms);
  pop_expect_data(buffer, 110ms, 1);

  // second sensor has a higher latency and provides a measurement with a meas timestamp older than our current buffer
  // time --> has to be rejected
  push_expect_ok(buffer, SENSOR_B, 150ms, 90ms);
  auto res = pop_expect_data(buffer, 150ms, 0, 1); // discarding message
  EXPECT_EQ(res.discarded_data.front().id, SENSOR_B);

  pop_expect_data(buffer, 151ms, 0, 0);

  // a single sample from sensor B is not enough to initialize the period estimate
  // --> ignore sensor B for in-sequence constraints until we have at least a second sample
  // --> meas from A is handled similarly to the single sensor use case
  push_expect_ok(buffer, SENSOR_A, 160ms, 150ms);

  pop_expect_data(buffer, 160ms, 1);

  // measurement from sensor B is discarded since it came in too late and we have not received enough inputs to
  // fully initialize the period and latency estimates
  push_expect_ok(buffer, SENSOR_B, 200ms, 140ms);
  pop_expect_data(buffer, 200ms, 0, 1);

  push_expect_ok(buffer, SENSOR_A, 210ms, 200ms);
  pop_expect_data(buffer, 210ms, 1, 0);
  push_expect_ok(buffer, SENSOR_B, 250ms, 190ms);
  pop_expect_data(buffer, 250ms, 0, 1);

  push_expect_ok(buffer, SENSOR_A, 260ms, 250ms);
  pop_expect_data(buffer, 260ms, 0, 0);
  // first time sensor B can be considered since the estimates are now fully initialized
  push_expect_ok(buffer, SENSOR_B, 300ms, 240ms);
  pop_expect_data(buffer, 300ms, 2, 0);
}

TEST_F(MinimalLatencyBufferTwoSources, lateJoiningSensorWithLowerLatency)
{
  using namespace minimal_latency_buffer;
  using namespace std::chrono_literals;

  MinimalLatencyBuffer buffer(params);

  // Note: switched sensor A and B with respect to the test above
  // period: 50ms, latency: 10ms
  constexpr auto SENSOR_A = 50U;
  // period: 50ms, latency: 60ms
  constexpr auto SENSOR_B = 100U;

  // two cycles with solely the first sensor
  pop_expect_data(buffer, 10ms, 0);
  push_expect_ok(buffer, SENSOR_B, 110ms, 50ms);
  pop_expect_data(buffer, 110ms, 1);
  pop_expect_data(buffer, 111ms, 0);

  push_expect_ok(buffer, SENSOR_B, 160ms, 100ms);
  pop_expect_data(buffer, 160ms, 1);
  push_expect_ok(buffer, SENSOR_B, 210ms, 150ms);
  pop_expect_data(buffer, 210ms, 1);

  // estimates for sensor B are now fully initialized

  // second sensor (A) has a lower latency and provides a measurement with a meas timestamp newer than the next expected
  // message for sensor B --> we should wait until we have received the measurement from sensor B
  push_expect_ok(buffer, SENSOR_A, 220ms, 210ms);
  pop_expect_data(buffer, 220ms, 0);
  push_expect_ok(buffer, SENSOR_B, 260ms, 200ms);
  pop_expect_data(buffer, 260ms, 2);

  push_expect_ok(buffer, SENSOR_A, 270ms, 260ms);
  pop_expect_data(buffer, 270ms, 0);
  push_expect_ok(buffer, SENSOR_B, 310ms, 250ms);
  pop_expect_data(buffer, 310ms, 2);
}

TEST_F(MinimalLatencyBufferTwoSources, simulataneousSensorStart)
{
  using namespace minimal_latency_buffer;
  using namespace std::chrono_literals;

  MinimalLatencyBuffer buffer(params);

  // Note: switched sensor A and B with respect to the test above
  // period: 50ms, latency: 10ms
  constexpr auto SENSOR_A = 50U;
  // period: 50ms, latency: 60ms
  constexpr auto SENSOR_B = 100U;

  pop_expect_data(buffer, 10ms, 0);
  push_expect_ok(buffer, SENSOR_A, 60ms, 50ms);
  pop_expect_data(buffer, 60ms, 1);
  push_expect_ok(buffer, SENSOR_B, 70ms, 10ms);
  pop_expect_data(buffer, 70ms, 0, 1);
  push_expect_ok(buffer, SENSOR_A, 110ms, 100ms);
  pop_expect_data(buffer, 110ms, 1);
  push_expect_ok(buffer, SENSOR_B, 120ms, 60ms);
  pop_expect_data(buffer, 120ms, 0, 1);
  push_expect_ok(buffer, SENSOR_A, 160ms, 150ms);
  pop_expect_data(buffer, 160ms, 1);
  push_expect_ok(buffer, SENSOR_B, 170ms, 110ms);
  pop_expect_data(buffer, 170ms, 0, 1);

  // both initialized --> now normal behaviour
  push_expect_ok(buffer, SENSOR_A, 210ms, 200ms);
  pop_expect_data(buffer, 210ms, 0);
  push_expect_ok(buffer, SENSOR_B, 220ms, 160ms);
  pop_expect_data(buffer, 220ms, 2);

  push_expect_ok(buffer, SENSOR_A, 260ms, 250ms);
  pop_expect_data(buffer, 260ms, 0);
  push_expect_ok(buffer, SENSOR_B, 270ms, 210ms);
  pop_expect_data(buffer, 270ms, 2);
}

TEST_F(MinimalLatencyBufferTwoSources, differentSensorFrequencies)
{
  using namespace minimal_latency_buffer;
  using namespace std::chrono_literals;

  MinimalLatencyBuffer buffer(params);

  // Note: switched sensor A and B with respect to the test above
  // period: 50ms, latency: 10ms
  constexpr auto SENSOR_A = 50U;
  // period: 100ms, latency: 60ms
  constexpr auto SENSOR_B = 100U;

  pop_expect_data(buffer, 10ms, 0);
  push_expect_ok(buffer, SENSOR_A, 50ms, 40ms);
  pop_expect_data(buffer, 50ms, 1);
  push_expect_ok(buffer, SENSOR_A, 100ms, 90ms);
  pop_expect_data(buffer, 100ms, 1);

  push_expect_ok(buffer, SENSOR_B, 110ms, 50ms);
  pop_expect_data(buffer, 110ms, 0, 1);

  push_expect_ok(buffer, SENSOR_A, 150ms, 140ms);
  pop_expect_data(buffer, 150ms, 1);
  push_expect_ok(buffer, SENSOR_A, 200ms, 190ms);
  pop_expect_data(buffer, 200ms, 1);

  push_expect_ok(buffer, SENSOR_B, 210ms, 150ms);
  pop_expect_data(buffer, 210ms, 0, 1);

  push_expect_ok(buffer, SENSOR_A, 250ms, 240ms);
  pop_expect_data(buffer, 250ms, 1);
  push_expect_ok(buffer, SENSOR_A, 300ms, 290ms);
  pop_expect_data(buffer, 300ms, 1);

  push_expect_ok(buffer, SENSOR_B, 310ms, 250ms);
  pop_expect_data(buffer, 310ms, 0, 1);

  push_expect_ok(buffer, SENSOR_A, 350ms, 340ms);
  pop_expect_data(buffer, 350ms, 1);
  push_expect_ok(buffer, SENSOR_A, 400ms, 390ms);
  pop_expect_data(buffer, 400ms, 0);

  // first time estimates for sensor B are fully initialized and can thus be used to wait for the input
  push_expect_ok(buffer, SENSOR_B, 410ms, 350ms);
  pop_expect_data(buffer, 410ms, 2);

  push_expect_ok(buffer, SENSOR_A, 450ms, 440ms);
  pop_expect_data(buffer, 450ms, 1);
  push_expect_ok(buffer, SENSOR_A, 500ms, 490ms);
  pop_expect_data(buffer, 500ms, 0);

  push_expect_ok(buffer, SENSOR_B, 510ms, 450ms);
  pop_expect_data(buffer, 510ms, 2);
}

TEST_F(MinimalLatencyBufferTwoSources, missingMeasurements)
{
  using namespace minimal_latency_buffer;
  using Time = Time;
  using namespace std::chrono_literals;

  MinimalLatencyBuffer buffer(params);

  // Note: switched sensor A and B with respect to the test above
  // period: 50ms, latency: 10ms
  constexpr auto SENSOR_A = 50U;
  // period: 100ms, latency: 60ms
  constexpr auto SENSOR_B = 100U;

  pop_expect_data(buffer, 10ms, 0);
  push_expect_ok(buffer, SENSOR_A, 50ms, 40ms);
  pop_expect_data(buffer, 50ms, 1);
  push_expect_ok(buffer, SENSOR_A, 100ms, 90ms);
  pop_expect_data(buffer, 100ms, 1);

  push_expect_ok(buffer, SENSOR_B, 110ms, 50ms);
  pop_expect_data(buffer, 110ms, 0, 1);

  push_expect_ok(buffer, SENSOR_A, 150ms, 140ms);
  pop_expect_data(buffer, 150ms, 1);
  push_expect_ok(buffer, SENSOR_A, 200ms, 190ms);
  pop_expect_data(buffer, 200ms, 1);

  push_expect_ok(buffer, SENSOR_B, 210ms, 150ms);
  pop_expect_data(buffer, 210ms, 0, 1);

  push_expect_ok(buffer, SENSOR_A, 250ms, 240ms);
  pop_expect_data(buffer, 250ms, 1);
  // skipping measurement of SENSOR_A with meas_time 290ms and receipt_time 300ms
  pop_expect_data(buffer, 300ms, 0);
  // internal buffer time stays at 240ms (measurement time stamp of the last output)
  EXPECT_EQ(buffer.getBufferTime(), Time(240ms));

  push_expect_ok(buffer, SENSOR_B, 310ms, 250ms);
  pop_expect_data(buffer, 310ms, 1);

  push_expect_ok(buffer, SENSOR_A, 350ms, 340ms);
  pop_expect_data(buffer, 350ms, 1);
  push_expect_ok(buffer, SENSOR_A, 400ms, 390ms);
  pop_expect_data(buffer, 400ms, 0);

  push_expect_ok(buffer, SENSOR_B, 410ms, 350ms);
  pop_expect_data(buffer, 410ms, 2);
}

TEST_F(MinimalLatencyBufferTwoSources, synchronizedSensorsWithBatching)
{
  using namespace minimal_latency_buffer;
  using namespace std::chrono_literals;

  params.mode = BufferMode::BATCH;
  MinimalLatencyBuffer buffer(params);

  // period: 50ms, latency: 10ms, initial offset: 0ms
  constexpr auto SENSOR_A = 50U;
  // period: 55, latency: 20ms, initial offset: 5ms
  constexpr auto SENSOR_B = 100U;

  pop_expect_data(buffer, 10ms, 0);
  push_expect_ok(buffer, SENSOR_A, 60ms, 50ms);
  pop_expect_data(buffer, 60ms, 1);
  push_expect_ok(buffer, SENSOR_B, 75ms, 55ms);
  pop_expect_data(buffer, 75ms, 1);

  push_expect_ok(buffer, SENSOR_A, 110ms, 100ms);
  pop_expect_data(buffer, 110ms, 1);
  push_expect_ok(buffer, SENSOR_B, 125ms, 105ms);
  pop_expect_data(buffer, 125ms, 1);

  push_expect_ok(buffer, SENSOR_A, 160ms, 150ms);
  pop_expect_data(buffer, 160ms, 1);
  push_expect_ok(buffer, SENSOR_B, 175ms, 155ms);
  pop_expect_data(buffer, 175ms, 1);

  // buffer estimates should now be fully initialized as thus considered for the batching decision
  push_expect_ok(buffer, SENSOR_A, 210ms, 200ms);
  pop_expect_data(buffer, 210ms, 0);
  push_expect_ok(buffer, SENSOR_B, 225ms, 205ms);
  pop_expect_data(buffer, 225ms, 2);

  push_expect_ok(buffer, SENSOR_A, 260ms, 250ms);
  pop_expect_data(buffer, 260ms, 0);
  push_expect_ok(buffer, SENSOR_B, 275ms, 255ms);
  pop_expect_data(buffer, 275ms, 2);

  // missing message of sensor B (receipt time: 325ms, meas time: 305ms)
  push_expect_ok(buffer, SENSOR_A, 310ms, 300ms);
  pop_expect_data(buffer, 310ms, 0);
  pop_expect_data(buffer, 320ms, 0);

  // message of sensor A is forwarded since the latest expected receipt time of the sensor B input is reached
  pop_expect_data(buffer, 325ms, 1);
  pop_expect_data(buffer, 330ms, 0);

  push_expect_ok(buffer, SENSOR_A, 360ms, 350ms);
  pop_expect_data(buffer, 360ms, 0);
  push_expect_ok(buffer, SENSOR_B, 375ms, 355ms);
  pop_expect_data(buffer, 375ms, 2);
}

// intended for simulation / dataset scenarios where only a single timestamp per data sample is available
// and thus the latency as seen by the buffer is zero
TEST_F(MinimalLatencyBufferTwoSources, zeroLatency)
{
  using namespace minimal_latency_buffer;
  using namespace std::chrono_literals;

  MinimalLatencyBuffer buffer(params);

  // period: 50ms, latency: 0ms
  constexpr auto SENSOR_A = 50U;
  // period: 50ms, latency: 0ms
  constexpr auto SENSOR_B = 100U;

  pop_expect_data(buffer, 10ms, 0);
  push_expect_ok(buffer, SENSOR_A, 60ms, 60ms);
  pop_expect_data(buffer, 60ms, 1);
  push_expect_ok(buffer, SENSOR_B, 60ms, 60ms);
  pop_expect_data(buffer, 60ms, 1);
  push_expect_ok(buffer, SENSOR_A, 110ms, 110ms);
  pop_expect_data(buffer, 110ms, 1);
  push_expect_ok(buffer, SENSOR_B, 110ms, 110ms);
  pop_expect_data(buffer, 110ms, 1);
  push_expect_ok(buffer, SENSOR_A, 160ms, 160ms);
  pop_expect_data(buffer, 160ms, 1);
  push_expect_ok(buffer, SENSOR_B, 160ms, 160ms);
  pop_expect_data(buffer, 160ms, 1);

  // both initialized
  push_expect_ok(buffer, SENSOR_A, 210ms, 210ms);
  pop_expect_data(buffer, 210ms, 1);
  push_expect_ok(buffer, SENSOR_B, 210ms, 210ms);
  pop_expect_data(buffer, 210ms, 1);

  push_expect_ok(buffer, SENSOR_A, 260ms, 260ms);
  // skipping intermediate pop
  push_expect_ok(buffer, SENSOR_B, 260ms, 260ms);
  pop_expect_data(buffer, 260ms, 2);
}

}  // namespace minimal_latency_buffer::test
