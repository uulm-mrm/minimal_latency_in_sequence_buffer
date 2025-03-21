#include "bindings.hpp"

// NOLINTNEXTLINE
NB_MODULE(_minimal_latency_buffer, module)
{
  loadTypes(module);
  loadMinimalLatencyBuffer(module);
  loadFixedLagBuffer(module);
}


