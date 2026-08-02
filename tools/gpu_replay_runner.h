#ifndef APGAR_TOOLS_GPU_REPLAY_RUNNER_H_
#define APGAR_TOOLS_GPU_REPLAY_RUNNER_H_

#include <functional>
#include <iosfwd>
#include <memory>
#include <string_view>

#include "apgar/gpu/planar_router.h"
#include "tools/gpu_replay_parser.h"

namespace apgar::tooling {

using GpuReplayBackendFactory = std::function<std::unique_ptr<gpu::IPlanarRouteBackend>()>;

enum class GpuReplayRunStatus {
  kReproduced,
  kRejected,
  kFailed,
};

struct GpuReplayRunOutcome {
  GpuReplayRunStatus status = GpuReplayRunStatus::kRejected;
  GpuReplaySemanticAssociationMismatchesV1 association_mismatches;

  [[nodiscard]] int exit_code() const noexcept {
    switch (status) {
      case GpuReplayRunStatus::kReproduced:
        return 0;
      case GpuReplayRunStatus::kRejected:
        return 2;
      case GpuReplayRunStatus::kFailed:
        return 1;
    }
    return 1;
  }
};

[[nodiscard]] GpuReplayRunOutcome RunGpuReplayArtifactV1(
    std::string_view artifact_path, const GpuReplayBackendFactory& create_backend,
    std::ostream& output, std::ostream& diagnostics);

}  // namespace apgar::tooling

#endif  // APGAR_TOOLS_GPU_REPLAY_RUNNER_H_
