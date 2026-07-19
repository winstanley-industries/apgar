#ifndef APGAR_GPU_FAULT_INJECTING_BACKEND_H_
#define APGAR_GPU_FAULT_INJECTING_BACKEND_H_

#include <cstdint>
#include <memory>

#include "apgar/gpu/planar_router.h"

namespace apgar::gpu {

// Replay/test-only corruption applied after backend readback. Keeping this in a
// decorator exercises the normal untrusted-result validator without adding
// test policy to production route requests or any concrete GPU backend.
enum class UntrustedResultFault : std::uint8_t {
  kGoalPredecessorSelfCycle,
};

[[nodiscard]] std::unique_ptr<IPlanarRouteBackend> CreateFaultInjectingPlanarRouteBackend(
    IPlanarRouteBackend& inner, UntrustedResultFault fault);

}  // namespace apgar::gpu

#endif  // APGAR_GPU_FAULT_INJECTING_BACKEND_H_
