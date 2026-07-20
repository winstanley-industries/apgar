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

// Replay/test-only corruption of one batched candidate readback. These faults
// model distinct host trust-boundary invariant classes; they are deliberately
// absent from production generator policy and concrete CUDA backends.
enum class UntrustedCandidateBatchResultFault : std::uint8_t {
  kNone,
  kWorkspaceBounds,
  kWorkspaceOwner,
  kQueryTelemetry,
  kMemoryAccounting,
  kBatchTelemetry,
  kQueryIdentity,
  kFalseDisconnected,
};

[[nodiscard]] std::unique_ptr<IPlanarRouteBackend> CreateFaultInjectingPlanarRouteBackend(
    IPlanarRouteBackend& inner, UntrustedResultFault fault);

[[nodiscard]] std::unique_ptr<IPlanarRouteBackend> CreateFaultInjectingCandidateBatchBackend(
    IPlanarRouteBackend& inner, UntrustedCandidateBatchResultFault fault);

}  // namespace apgar::gpu

#endif  // APGAR_GPU_FAULT_INJECTING_BACKEND_H_
