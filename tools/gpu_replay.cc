#include <array>
#include <iostream>
#include <memory>
#include <sstream>
#include <string_view>

#include "apgar/gpu/cuda_backend.h"
#include "tools/gpu_replay_runner.h"

namespace {

inline constexpr std::array<std::string_view, 1> kRejectedArtifacts{{
    "replays/gpu/goal_predecessor_self_cycle_wrong_routing_profile_v1.replay",
}};

int RejectArtifact(std::string_view artifact_path) {
  bool backend_constructed = false;
  std::ostringstream output;
  std::ostringstream diagnostics;
  const apgar::tooling::GpuReplayRunOutcome result = apgar::tooling::RunGpuReplayArtifactV1(
      artifact_path,
      [&backend_constructed]() -> std::unique_ptr<apgar::gpu::IPlanarRouteBackend> {
        backend_constructed = true;
        return nullptr;
      },
      output, diagnostics);
  if (result.status != apgar::tooling::GpuReplayRunStatus::kRejected || backend_constructed ||
      result.association_mismatches !=
          apgar::tooling::GpuReplaySemanticAssociationMismatchesV1{.routing_profile = true}) {
    std::cerr << artifact_path
              << ": expected a pre-backend routing-profile rejection: " << diagnostics.str();
    return 1;
  }
  std::cout << "rejected=semantic_association mismatch=routing_profile artifact=" << artifact_path
            << '\n';
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  const apgar::tooling::GpuReplayBackendFactory create_backend = [] {
    return apgar::gpu::CreateCudaPlanarRouteBackend();
  };
  if (argc == 2) {
    return apgar::tooling::RunGpuReplayArtifactV1(std::string_view(argv[1]), create_backend,
                                                  std::cout, std::cerr)
        .exit_code();
  }
  if (argc != 1) {
    std::cerr << "usage: gpu_replay [artifact]\n";
    return 2;
  }
  for (std::string_view rejected : kRejectedArtifacts) {
    if (RejectArtifact(rejected) != 0) {
      return 1;
    }
  }
  return apgar::tooling::RunGpuReplayArtifactV1("replays/gpu/goal_predecessor_self_cycle_v1.replay",
                                                create_backend, std::cout, std::cerr)
      .exit_code();
}
