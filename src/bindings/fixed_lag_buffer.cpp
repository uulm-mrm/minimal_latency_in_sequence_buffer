#include "bindings.hpp"

#include "minimal_latency_buffer/fixed_lag_buffer.hpp"

namespace mlb=minimal_latency_buffer;
namespace nb=nanobind;

using FixedLagBuffer = mlb::FixedLagBuffer<MeasType, SourceId>;
using Params = FixedLagBuffer::Params;

void loadFixedLagBuffer(::nanobind::module_& bound_module)
{
  nb::class_<FixedLagBuffer::Params>(bound_module, "FLParams")
      .def(nb::init<>())
      .def_rw("mode", &FixedLagBuffer::Params::mode)
      .def_rw("reset_threshold", &FixedLagBuffer::Params::reset_threshold)
      .def_rw("delay_mean", &FixedLagBuffer::Params::delay_mean)
      .def_rw("delay_stddev", &FixedLagBuffer::Params::delay_stddev)
      .def_rw("delay_quantile", &FixedLagBuffer::Params::delay_quantile)
      .def_rw("batch", &FixedLagBuffer::Params::batch)
      .def_rw("match", &FixedLagBuffer::Params::match)
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

        stream << ", delay_mean=" << std::to_string(params.delay_mean.count())
               << ", delay_stddev=" << std::to_string(params.delay_stddev.count())
               << ", delay_quantile=" << params.delay_quantile;

        if (params.mode == mlb::BufferMode::BATCH) {
          stream << "batch max_delta: " << params.batch.max_delta;
        }
        else if (params.mode == mlb::BufferMode::MATCH)
        {
          stream << ", match ref stream: " << params.match.reference_stream;
          stream << ", match num streams: " << params.match.num_streams;
        }

        stream << ")";

        return stream.str();
      })
      .def("__getstate__", [](const Params& dat) {
        return std::make_tuple(
            dat.mode,
            dat.reset_threshold,
            dat.delay_mean,
            dat.delay_stddev,
            dat.delay_quantile,
            dat.batch,
            dat.match);
      })
      .def("__setstate__", [](Params& dat, const nb::tuple &state) {
        new (&dat) Params (
            nb::cast<mlb::BufferMode>(state[0]),
            nb::cast<mlb::Duration>(state[1]),
            nb::cast<mlb::Duration>(state[2]),
            nb::cast<mlb::Duration>(state[3]),
            nb::cast<double>(state[4]),
            nb::cast<mlb::BatchParams>(state[5]),
            nb::cast<mlb::MatchParams<SourceId>>(state[6])
        );
      });

  nb::class_<FixedLagBuffer>(bound_module, "FixedLagBuffer")
      .def(nb::init<FixedLagBuffer::Params>())
      .def("push", &FixedLagBuffer::push, "Push new data to the buffer.")
      .def("pop", &FixedLagBuffer::pop, "Remove data from the buffer (if possible).")
      .def("reset", &FixedLagBuffer::reset, "Reset the whole buffer.")
      .def("num_queued_elements", &FixedLagBuffer ::getNumberOfQueuedElements, "Number of queued elements (excluding any placeholders).");
}