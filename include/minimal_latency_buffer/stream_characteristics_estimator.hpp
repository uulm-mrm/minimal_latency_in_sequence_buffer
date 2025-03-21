#pragma once

#include <iostream>
#include <chrono>
#include <boost/math/distributions/normal.hpp>

namespace minimal_latency_buffer
{
template <class ClockT = std::chrono::high_resolution_clock, class DurationT = std::chrono::duration<int64_t, std::nano>>
class StreamCharacteristicsEstimator
{
public:
  using Duration = DurationT;
  using Time = std::chrono::time_point<ClockT, DurationT>;

  StreamCharacteristicsEstimator(Time current_time, Time meas_time, double alpha = 0.05);

  [[nodiscard]] Duration latency() const;
  [[nodiscard]] Duration latency_stddev() const;
  [[nodiscard]] Duration latency_quantile(double quantile) const;

  [[nodiscard]] Duration period() const;
  [[nodiscard]] Duration period_stddev() const;
  [[nodiscard]] Duration period_quantile(double quantile) const;


  [[nodiscard]] std::size_t getNumUpdates() const;

  void update(Time current_time, Time meas_time, std::size_t num_missing_measurements = 0);
  void updateLatencyOnly(Time current_time, Time meas_time);

  [[nodiscard]] bool isInitialized() const;

private:
  // utility struct
  struct State {
    // separately initialized within the first few update steps
    double mean = 0;
    double variance = 0;
  };

  [[nodiscard]] State updateEstimates(const State &state, const double &estimate, bool update_variance = true) const;
  void updatePeriodEstimate(double estimate, std::size_t num_missing_measurements);
  void updateLatencyEstimate(double estimate);

  std::size_t _num_updates = 0;
  Time _last_meas_time = Time{ Duration(0) };
  Time _current_time = Time{ Duration(0) };
  double _alpha;

  State _period_state;
  State _latency_state;
};

template <class Clock, class Duration>
StreamCharacteristicsEstimator<Clock, Duration>::StreamCharacteristicsEstimator(Time current_time, Time meas_time, const double alpha) : _last_meas_time{ meas_time }, _current_time{ current_time }, _alpha(alpha)
{
    // latency can be directly initialized with the first sample while the remaining parameters required a second one
    _latency_state.mean = static_cast<double>(std::chrono::duration_cast<Duration>(current_time - meas_time).count());
}

template <class Clock, class Duration>
[[nodiscard]] Duration StreamCharacteristicsEstimator<Clock, Duration>::latency() const
{
    return Duration(static_cast<Duration::rep>(_latency_state.mean));
}

template <class Clock, class Duration>
[[nodiscard]] Duration StreamCharacteristicsEstimator<Clock, Duration>::latency_stddev() const
{
  return Duration(static_cast<Duration::rep>(std::sqrt(_latency_state.variance)));
}

template <class Clock, class Duration>
[[nodiscard]] Duration StreamCharacteristicsEstimator<Clock, Duration>::latency_quantile(double quantile) const
{
    // should only occur within unit-testing
    if (_latency_state.variance == 0) {
      // if no variance, all quantiles are technically on the mean value
      return latency();
    }

    boost::math::normal_distribution dist(_latency_state.mean, std::sqrt(_latency_state.variance));
    return Duration(static_cast<Duration::rep>(boost::math::quantile(dist, quantile)));
}

template <class Clock, class Duration>
[[nodiscard]] Duration StreamCharacteristicsEstimator<Clock, Duration>::period() const
{
  return Duration(static_cast<Duration::rep>(_period_state.mean));
}

template <class Clock, class Duration>
[[nodiscard]] Duration StreamCharacteristicsEstimator<Clock, Duration>::period_stddev() const
{
  const auto stddev = Duration(static_cast<Duration::rep>(std::sqrt(_period_state.variance)));
  return stddev;
}

template <class Clock, class Duration>
[[nodiscard]] Duration StreamCharacteristicsEstimator<Clock, Duration>::period_quantile(double quantile) const
{
    // should only occur within unit-testing
    if (_period_state.variance == 0) {
      // if no variance, all quantiles are technically on the mean value
      return period();
    }
    boost::math::normal_distribution dist(_period_state.mean, std::sqrt(_period_state.variance));
    return Duration(static_cast<Duration::rep>(boost::math::quantile(dist, quantile)));
}

template <class Clock, class Duration>
std::size_t StreamCharacteristicsEstimator<Clock, Duration>::getNumUpdates() const
{
  return _num_updates;
}

template <class Clock, class Duration>
void StreamCharacteristicsEstimator<Clock, Duration>::update(const Time current_time, const Time meas_time, const std::size_t num_missing_measurement)
{
    // determine new estimates
    auto estimated_latency = static_cast<double>(std::chrono::duration_cast<Duration>(current_time - meas_time).count());
    auto estimated_period = static_cast<double>(std::chrono::duration_cast<Duration>(meas_time - _last_meas_time).count());

    // perform update step (including potential initialization)
    updatePeriodEstimate(estimated_period, num_missing_measurement);
    updateLatencyEstimate(estimated_latency);

    _last_meas_time = meas_time;
    _current_time = current_time;
    _num_updates++;
}

template <class Clock, class Duration>
void StreamCharacteristicsEstimator<Clock, Duration>::updateLatencyOnly(const Time current_time, const Time meas_time)
{
  // determine new estimates
  auto estimated_latency = static_cast<double>(std::chrono::duration_cast<Duration>(current_time - meas_time).count());

  updateLatencyEstimate(estimated_latency);

  _last_meas_time = meas_time;
  _current_time = current_time;

  // do not count updates, as there is no full update possible...
  // only counting latency updates separately seems unnecessary (wodtko 12/2024)
}

template <class Clock, class Duration>
[[nodiscard]] bool StreamCharacteristicsEstimator<Clock, Duration>::isInitialized() const {
    return _num_updates >= 2;
}

template <class Clock, class Duration>
StreamCharacteristicsEstimator<Clock, Duration>::State StreamCharacteristicsEstimator<Clock, Duration>::updateEstimates(const StreamCharacteristicsEstimator::State &state, const double &estimate, bool update_variance) const {
    const auto diff = estimate - state.mean;
    const auto increment = _alpha * diff;
    const auto mean = state.mean + increment;

    const auto variance = (update_variance) ? (1 - _alpha) * (state.variance + diff * increment) : state.variance;

    return { mean, variance };
}

template <class Clock, class Duration>
void StreamCharacteristicsEstimator<Clock, Duration>::updatePeriodEstimate(const double estimate, const std::size_t num_missing_measurements) {
    // Note: in contrast to the latency estimation the period requires three data points (since we need two differences
    //       to initialize the variance)
    if (_num_updates == 0) {
        _period_state.mean = estimate;
        return;
    } else if (_num_updates == 1) {
        const double first_estimate = _period_state.mean;

        // update only mean since variance is not yet initialized
        _period_state = updateEstimates(_period_state, estimate, false);

        _period_state.variance = std::pow(first_estimate - _period_state.mean, 2)
                                 + std::pow(estimate - _period_state.mean, 2);
        return;
    }

    auto corrected_estimate = estimate - num_missing_measurements * _period_state.mean;

    // if signs differ, something seems off
    if (corrected_estimate < 0) {
      if (_num_updates > 10)
      {
        std::string message {"number of missing estimates is off...: "};
        message += "num_missing_meas: " + std::to_string(num_missing_measurements);
        message += "\n | estimate:           " + std::to_string(estimate);
        message += "\n | mean:               " + std::to_string(_period_state.mean);
        message += "\n | corrected_estimate: " + std::to_string(corrected_estimate);
        message += "\n | num_updates:        " + std::to_string(_num_updates);
        throw std::runtime_error(message);
      }
      return;
    }

    _period_state = updateEstimates(_period_state, corrected_estimate);
}

template <class Clock, class Duration>
void StreamCharacteristicsEstimator<Clock, Duration>::updateLatencyEstimate(const double estimate) {
    // initialization
    if (_num_updates == 0) {
        // Note: first latency estimate is already received within the constructor, hence, the variance can already
        //       be initialized within the first update step

        // update only mean since variance is not yet initialized
        _latency_state = updateEstimates(_latency_state, estimate, false);

        // initialize variance based on first two latency estimates
        const auto first_estimate = (_current_time - _last_meas_time).count();
        _latency_state.variance = std::pow(static_cast<double>(first_estimate) - _latency_state.mean, 2)
                                  + std::pow(estimate - _latency_state.mean, 2);
        return;
    }

    _latency_state = updateEstimates(_latency_state, estimate);
}
}  // namespace min_latency_buffer
