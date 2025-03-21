#include <nanobind/nanobind.h>
#include <nanobind/operators.h>

#include <string>

// do not add the vector file, as long as a specific vector type needs to be bound
// nanobind does not allow to include both 'vector.h' and 'bind_vector.h'
//#include <nanobind/stl/vector.h>
#include <nanobind/stl/bind_vector.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/chrono.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/tuple.h>

#include "minimal_latency_buffer/types.hpp"

using MeasType = nanobind::object;
using SourceId = std::size_t;

using Time = minimal_latency_buffer::Time;
using TimeData = minimal_latency_buffer::TimeData<SourceId, MeasType>;

void loadTypes(::nanobind::module_& bound_module);
void loadMinimalLatencyBuffer(::nanobind::module_& bound_module);
void loadFixedLagBuffer(::nanobind::module_& bound_module);
