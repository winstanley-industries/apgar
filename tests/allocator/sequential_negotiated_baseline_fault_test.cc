#include <array>
#include <cstdint>
#include <cstdlib>
#include <utility>
#include <variant>
#include <vector>

#include "apgar/allocator/sequential_negotiated_baseline.h"
#include "src/allocator/sequential_negotiated_baseline_internal.h"
#include "tests/support/board_builder.h"
#include "tests/support/compiler_builder.h"
#include "tests/support/google_test.h"

namespace apgar::allocator {
namespace {

[[nodiscard]] board_ir::BoardSnapshot Snapshot(board_ir::BoardData data) {
  board_ir::BoardCreationResult result = board_ir::CreateBoardSnapshot(std::move(data));
  EXPECT_TRUE(std::holds_alternative<board_ir::BoardSnapshot>(result));
  if (!std::holds_alternative<board_ir::BoardSnapshot>(result)) {
    std::abort();
  }
  return std::get<board_ir::BoardSnapshot>(std::move(result));
}

[[nodiscard]] MultiNetWorkload Workload(MultiNetWorkloadResult result) {
  EXPECT_TRUE(std::holds_alternative<MultiNetWorkload>(result));
  if (!std::holds_alternative<MultiNetWorkload>(result)) {
    std::abort();
  }
  return std::get<MultiNetWorkload>(std::move(result));
}

[[nodiscard]] ResourceCapacityModel Capacities(ResourceCapacityModelResult result) {
  EXPECT_TRUE(std::holds_alternative<ResourceCapacityModel>(result));
  if (!std::holds_alternative<ResourceCapacityModel>(result)) {
    std::abort();
  }
  return std::get<ResourceCapacityModel>(std::move(result));
}

TEST(SequentialNegotiatedBaselineFaultTest,
     SecondSweepAdmissionRejectionRetainsEvidenceAndRestoresPriorRoute) {
  board_ir::BoardData data = test_support::ValidM1BoardData();
  data.obstacles.clear();
  board_ir::BoardSnapshot board = Snapshot(std::move(data));
  geometry_compiler::CompilerProfile profile = test_support::DefaultCompilerProfile({0});
  profile.heading_mask = static_cast<board_ir::HeadingMask>(board_ir::Heading::kHorizontal);
  profile.compilation_roi = {.min = {.x = 0, .y = 0}, .max = {.x = 100, .y = 0}};
  profile.active_regions = {{.layer = 0, .bounds = profile.compilation_roi}};
  const std::array specs = {MultiNetRoutingSpec{
      .routing_profile = board.data().routing_profile, .start_layer = 0, .goal_layer = 0}};
  MultiNetWorkload workload = Workload(
      BuildMultiNetWorkload(kMultiNetWorkloadSchemaVersion, board, profile, specs, specs.size()));
  std::vector<ResourceCapacityOverride> overrides;
  for (std::int64_t x = 0; x < 10; ++x) {
    overrides.push_back(ResourceCapacityOverride{
        .resource = routing::EdgeResourceKey{.layer = 0,
                                             .lattice_x = x,
                                             .lattice_y = 0,
                                             .direction = geometry_compiler::Direction::kEast},
        .capacity_units = 0,
    });
  }
  ResourceCapacityModel capacities = Capacities(
      BuildResourceCapacityModel(kResourceCapacityModelSchemaVersion, board,
                                 workload.nets().front().compiled_board, 1, std::move(overrides)));
  SequentialNegotiatedBaselineConfig config;
  config.maximum_sweeps = 2;
  config.price_config = NegotiatedPriceConfig{
      .present_step_per_overuse_unit = 1,
      .history_step_per_overuse_unit = 1,
      .maximum_price_per_resource = 1'000,
      .maximum_iterations = 2,
      .maximum_price_records = 10'000,
  };

  internal::SetSequentialNegotiatedAdmissionBudgetFaultForTesting(2);
  SequentialNegotiatedBaselineExecution execution =
      ExecuteSequentialNegotiatedBaseline(board, workload, capacities, config);
  ASSERT_TRUE(std::holds_alternative<SequentialNegotiatedBaselineResult>(execution))
      << (std::holds_alternative<SequentialNegotiatedBaselineError>(execution)
              ? std::string(std::get<SequentialNegotiatedBaselineError>(execution).invariant_id)
              : std::string{});
  const SequentialNegotiatedBaselineResult& result =
      std::get<SequentialNegotiatedBaselineResult>(execution);

  EXPECT_EQ(result.terminal_reason(), SequentialNegotiatedTerminalReason::kSweepBudgetExhausted);
  ASSERT_EQ(result.columns().size(), 2U);
  EXPECT_EQ(result.columns()[0].outcome, SequentialNegotiatedColumnOutcome::kAdmitted);
  EXPECT_EQ(result.columns()[1].outcome, SequentialNegotiatedColumnOutcome::kAdmissionRejected);
  EXPECT_TRUE(result.columns()[1].retained_prior_candidate);
  ASSERT_TRUE(result.columns()[1].rejection.has_value());
  const candidates::CandidateRejection& rejection = *result.columns()[1].rejection;
  EXPECT_EQ(rejection.schema_version, candidates::kCandidateRejectionSchemaVersion);
  EXPECT_EQ(rejection.stage, candidates::CandidateLifecycleStage::kGenerated);
  EXPECT_EQ(rejection.code, candidates::CandidateRejectionCode::kBudgetExhausted);
  EXPECT_EQ(rejection.invariant_id, "candidate.store.transaction.work_budget.v1");
  EXPECT_FALSE(rejection.net.has_value());
  EXPECT_EQ(rejection.associations,
            candidates::AssociationsFor(board, workload.nets().front().compiled_board));
  EXPECT_TRUE(rejection.expected_value.has_value());
  EXPECT_TRUE(rejection.actual_value.has_value());
  EXPECT_FALSE(rejection.detail.empty());
  EXPECT_EQ(candidates::ComputeRejectionLogicalBytes(rejection), rejection.logical_bytes);
  EXPECT_EQ(result.counters().restored_prior_routes, 1U);
  EXPECT_EQ(result.counters().retained_rejection_bytes, rejection.logical_bytes);
  ASSERT_EQ(result.final_pools().front().candidates.size(), 1U);
  ASSERT_TRUE(result.columns()[0].candidate_id.has_value());
  EXPECT_EQ(result.final_pools().front().candidates.front()->id(),
            *result.columns()[0].candidate_id);
  EXPECT_EQ(result.final_world().total_overuse_units, 10U);
}

}  // namespace
}  // namespace apgar::allocator
