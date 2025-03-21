#include "bindings.hpp"


#include "minimal_latency_buffer/minimal_latency_buffer.hpp"

namespace mlb=minimal_latency_buffer;
namespace nb=nanobind;

using MinimalLatencyBuffer = mlb::MinimalLatencyBuffer<MeasType, SourceId>;
using Params = MinimalLatencyBuffer::Params;

void loadMinimalLatencyBuffer(::nanobind::module_& bound_module)
{

  nb::class_<Params>(bound_module, "MLParams")
      .def(nb::init<>())
      .def_rw("mode", &Params::mode)
      .def_rw("jitter_quantile", &Params::measurement_confidence_quantile)
      .def_rw("max_jitter", &Params::max_abs_measurement_jitter)
      .def_rw("max_wait_duration_quantile", &Params::wait_confidence_quantile)
      .def_rw("max_abs_wait_jitter", &Params::max_abs_wait_jitter)
      .def_rw("max_wait_duration", &Params::max_total_wait_time)
      .def_rw("batch", &Params::batch)
      .def_rw("match", &Params::match)
      .def("__repr__", [](const Params& params) {
        std::stringstream stream;

        if (params.mode == mlb::BufferMode::SINGLE)
        {
          stream << "Params(mode=SINGLE";
        }
        else if (params.mode == mlb::BufferMode::BATCH)
        {
          stream << "Params(mode=BATCH";
        }
        else
        {
          stream << "Params(mode=MATCH";
        }

        stream << ", jitter_quantile=" << std::to_string(params.measurement_confidence_quantile)
               << ", max_jitter=" << std::to_string(params.max_abs_measurement_jitter.count())
               << ", max_wait_duration=" << std::to_string(params.max_total_wait_time.count())
               << ", max_wait_quantile=" << params.wait_confidence_quantile;

        if (params.mode == mlb::BufferMode::BATCH) {
          stream << params.batch.max_delta;
        }
        stream << ")";

        return stream.str();
      })
      .def("__getstate__", [](const Params& dat) {
        return std::make_tuple(
            dat.mode,
            dat.reset_threshold,
            dat.measurement_confidence_quantile,
            dat.max_abs_measurement_jitter,
            dat.wait_confidence_quantile,
            dat.max_abs_wait_jitter,
            dat.max_total_wait_time,
            dat.batch,
            dat.match);
      })
      .def("__setstate__", [](Params& dat, const nb::tuple &state) {
        new (&dat) Params (
            nb::cast<mlb::BufferMode>(state[0]),
            nb::cast<mlb::Duration>(state[1]),
            nb::cast<double>(state[2]),
            nb::cast<mlb::Duration>(state[3]),
            nb::cast<double>(state[4]),
            nb::cast<mlb::Duration>(state[5]),
            nb::cast<mlb::Duration>(state[6]),
            nb::cast<mlb::BatchParams>(state[7]),
            nb::cast<mlb::MatchParams<SourceId>>(state[8])
        );
      });

  nb::class_<MinimalLatencyBuffer>(bound_module, "MinimalLatencyBuffer")
      .def(nb::init<Params>())
      .def("estimated_latency", &MinimalLatencyBuffer::getEstimatedLatency,
           "Getter for the estimated latency of the given data source.")
      .def("estimated_latency_stddev", &MinimalLatencyBuffer::getEstimatedLatencyStddev,
           "Getter for the estimated standard deviation of the latency of the given data source.")
      .def("estimated_latency_jitter", &MinimalLatencyBuffer::getEstimatedLatencyQuantile)
      .def("estimated_period", &MinimalLatencyBuffer::getEstimatedPeriod,
           "Getter for the estimated period of the given data source.")
      .def("estimated_period_stddev", &MinimalLatencyBuffer::getEstimatedPeriodStddev,
           "Getter for the estimated standard deviation of the period of the given data source.")
      .def("estimated_period_jitter", &MinimalLatencyBuffer::getEstimatedPeriodQuantile)
      .def("push", &MinimalLatencyBuffer::push, "Push new data to the buffer.")
      .def("pop", &MinimalLatencyBuffer::pop, "Remove data from the buffer (if possible).")
      .def("reset", &MinimalLatencyBuffer::reset, "Reset the whole buffer.")
      .def("total_size", &MinimalLatencyBuffer::total_size, "total size, i.e., size with placeholders, of the buffer")
      .def("num_queued_elements", &MinimalLatencyBuffer::getNumberOfQueuedElements, "Number of queued elements (excluding any placeholders).");
}

