#include "bindings.hpp"

#include "minimal_latency_buffer/types.hpp"

namespace mlb=minimal_latency_buffer;
namespace nb=nanobind;

using PopReturn = mlb::PopReturn<TimeData>;

void loadTypes(::nanobind::module_& bound_module)
{
  nb::enum_<mlb::BufferMode>(bound_module, "Mode")
      .value("Single", mlb::BufferMode::SINGLE)
      .value("Batch", mlb::BufferMode::BATCH)
      .value("Match", mlb::BufferMode::MATCH)
      .export_values();

  nb::class_<mlb::BatchParams>(bound_module, "BatchParams")
      .def(nb::init<>())
      .def_rw("max_delta", &mlb::BatchParams::max_delta)
      .def("__getstate__",[](const mlb::BatchParams &dat) {
        return std::make_tuple(dat.max_delta);
      })
      .def("__setstate__",[](mlb::BatchParams &pop, const nb::tuple &state){
        new (&pop) mlb::BatchParams(
            nb::cast<mlb::Duration>(state[0])
        );});

  nb::class_<mlb::MatchParams<SourceId>>(bound_module, "MatchParams")
      .def(nb::init<>())
      .def_rw("reference_stream", &mlb::MatchParams<SourceId>::reference_stream)
      .def_rw("num_streams", &mlb::MatchParams<SourceId>::num_streams)
      .def("__getstate__",[](const mlb::MatchParams<SourceId> &dat) {
        return std::make_tuple(
            dat.reference_stream,
            dat.num_streams);
      })
      .def("__setstate__",[](mlb::MatchParams<SourceId> &pop, const nb::tuple &state){
        new (&pop) mlb::MatchParams(
            nb::cast<SourceId>(state[0]),
                nb::cast<std::size_t>(state[0])
        );});

  nb::enum_<mlb::PushReturn>(bound_module, "PushReturn")
      .value("Ok", mlb::PushReturn::OK)
      .value("Reset", mlb::PushReturn::RESET)
      .export_values();

  nb::class_<TimeData>(bound_module, "TimeData")
      .def(nb::init<>())
          // adding this, lets you call the init by naming the arguments
      .def(nb::init< SourceId , Time, Time, Time, Time, MeasType, bool>(),
           nb::arg("id") = SourceId {},
           nb::arg("meas_time") = Time {},
           nb::arg("receipt_time") = Time {},
           nb::arg("earliest_meas_time") = Time {},
           nb::arg("latest_receipt_time") = Time {},
           nb::arg("data") = nb::none(),
           nb::arg("create_placeholder") = false
      )
      .def_rw("id", &TimeData::id)
      .def_rw("meas_time", &TimeData::meas_time)
      .def_rw("receipt_time", &TimeData::receipt_time)
      .def_rw("earliest_meas_time", &TimeData::earliest_estimated_meas_time)
      .def_rw("latest_receipt_time", &TimeData::latest_receipt_time)
      .def_rw("data", &TimeData::data)
      .def("is_placeholder", &TimeData::is_placeholder)
          // required for pickling data
      .def("__getstate__",[](const TimeData &dat) {
        return std::make_tuple(dat.id, dat.meas_time, dat.receipt_time, dat.earliest_estimated_meas_time, dat.latest_receipt_time, dat.data, dat.created_placeholder);
      })
      .def("__setstate__",[](TimeData &pop, const nb::tuple &state){
        new (&pop) TimeData(
            nb::cast<SourceId>(state[0]),
            nb::cast<Time>(state[1]),
            nb::cast<Time>(state[2]),
            nb::cast<Time>(state[3]),
            nb::cast<Time>(state[4]),
            nb::cast<MeasType>(state[5]),
            nb::cast<bool>(state[6])
        );});

  // bind vector to prevent python from copying every access...
  nb::bind_vector<std::vector<TimeData>>(bound_module, "TimeDataList")
      .def("__getstate__", [](const std::vector<TimeData> &dat) {
        nb::list list{};
        for (auto const &element : dat)
        {
          list.append(element);
        }
        return list;
      })
      .def("__setstate__", [](std::vector<TimeData> &dat, const nb::list &list) {
        new (&dat) std::vector<TimeData>;
        dat.reserve(list.size());
        for (auto const &element : list)
        {
          dat.push_back(nb::cast<TimeData>(element));
        }
      });

  nb::class_<PopReturn>(bound_module, "PopReturn")
      .def(nb::init<>())
      .def(nb::init< Time, std::vector<TimeData>, std::vector<TimeData>>(),
           nb::arg("buffer_time"),
           nb::arg("data") = std::vector<TimeData>(),
           nb::arg("discarded_data") = std::vector<TimeData>()
      )
      .def_rw("buffer_time", &PopReturn::buffer_time)
      .def_rw("data", &PopReturn::data)
      .def_rw("discarded_data", &PopReturn::discarded_data)
      .def("__getstate__",[](const PopReturn &pop) -> nb::tuple{
        return nb::make_tuple(pop.buffer_time, pop.data, pop.discarded_data);
      })
      .def("__setstate__",[](PopReturn &pop, const std::tuple<Time, std::vector<TimeData>, std::vector<TimeData>> &state){
        new (&pop) PopReturn(std::get<0>(state), std::get<1>(state), std::get<2>(state));
      });

}
