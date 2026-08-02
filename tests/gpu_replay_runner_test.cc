#include "tools/gpu_replay_runner.h"

#include <memory>
#include <sstream>
#include <string_view>

#include "tests/support/google_test.h"

namespace apgar::tooling {
namespace {

TEST(GpuReplayRunnerTest, RejectsWrongProfileBeforeConstructingBackend) {
  bool backend_constructed = false;
  std::ostringstream output;
  std::ostringstream diagnostics;
  const GpuReplayRunOutcome outcome = RunGpuReplayArtifactV1(
      "replays/gpu/goal_predecessor_self_cycle_wrong_routing_profile_v1.replay",
      [&backend_constructed]() -> std::unique_ptr<gpu::IPlanarRouteBackend> {
        backend_constructed = true;
        return nullptr;
      },
      output, diagnostics);

  EXPECT_EQ(outcome.status, GpuReplayRunStatus::kRejected);
  EXPECT_EQ(outcome.association_mismatches,
            (GpuReplaySemanticAssociationMismatchesV1{.routing_profile = true}));
  EXPECT_FALSE(backend_constructed);
  EXPECT_TRUE(output.str().empty());
  EXPECT_EQ(diagnostics.str(), "replay semantic association mismatch: routing_profile\n");
}

TEST(GpuReplayRunnerTest, CanonicalArtifactReachesBackendFactory) {
  bool backend_constructed = false;
  std::ostringstream output;
  std::ostringstream diagnostics;
  const GpuReplayRunOutcome outcome = RunGpuReplayArtifactV1(
      "replays/gpu/goal_predecessor_self_cycle_v1.replay",
      [&backend_constructed]() -> std::unique_ptr<gpu::IPlanarRouteBackend> {
        backend_constructed = true;
        return nullptr;
      },
      output, diagnostics);

  EXPECT_EQ(outcome.status, GpuReplayRunStatus::kFailed);
  EXPECT_FALSE(outcome.association_mismatches.any());
  EXPECT_TRUE(backend_constructed);
  EXPECT_TRUE(output.str().empty());
  EXPECT_EQ(diagnostics.str(), "backend factory returned null\n");
}

}  // namespace
}  // namespace apgar::tooling
