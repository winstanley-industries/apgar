#include "apgar/allocator/sequential_negotiated_baseline.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

#include "src/allocator/sequential_negotiated_baseline_internal.h"
#include "tests/support/board_builder.h"
#include "tests/support/compiler_builder.h"
#include "tests/support/google_test.h"

namespace apgar::allocator {
namespace {

struct Fixture {
  board_ir::BoardSnapshot board;
  MultiNetWorkload workload;
  ResourceCapacityModel capacities;
};

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

[[nodiscard]] SequentialNegotiatedBaselineResult Executed(
    SequentialNegotiatedBaselineExecution result) {
  EXPECT_TRUE(std::holds_alternative<SequentialNegotiatedBaselineResult>(result))
      << (std::holds_alternative<SequentialNegotiatedBaselineError>(result)
              ? std::string(std::get<SequentialNegotiatedBaselineError>(result).invariant_id)
              : std::string{});
  if (!std::holds_alternative<SequentialNegotiatedBaselineResult>(result)) {
    std::abort();
  }
  return std::get<SequentialNegotiatedBaselineResult>(std::move(result));
}

[[nodiscard]] SequentialNegotiatedBaselineConfig Config(std::uint32_t maximum_sweeps = 4) {
  SequentialNegotiatedBaselineConfig config;
  config.deterministic_seed = 0x1234'5678'9abc'def0ULL;
  config.maximum_sweeps = maximum_sweeps;
  config.price_config = NegotiatedPriceConfig{
      .present_step_per_overuse_unit = 50,
      .history_step_per_overuse_unit = 10,
      .maximum_price_per_resource = 1'000,
      .maximum_iterations = maximum_sweeps,
      .maximum_price_records = 10'000,
  };
  return config;
}

[[nodiscard]] Fixture TwoNetFixture() {
  board_ir::BoardData data = test_support::ValidM1TwoNetBoardData();
  data.obstacles.clear();
  board_ir::BoardSnapshot board = Snapshot(std::move(data));
  board_ir::RoutingProfile second = board.data().routing_profile;
  second.net = board.data().nets[1].ref;
  const std::array specs = {
      MultiNetRoutingSpec{
          .routing_profile = board.data().routing_profile, .start_layer = 0, .goal_layer = 0},
      MultiNetRoutingSpec{.routing_profile = second, .start_layer = 0, .goal_layer = 0},
  };
  MultiNetWorkload workload = Workload(
      BuildMultiNetWorkload(kMultiNetWorkloadSchemaVersion, board,
                            test_support::DefaultCompilerProfile({0}), specs, specs.size()));
  ResourceCapacityModel capacities = Capacities(BuildResourceCapacityModel(
      kResourceCapacityModelSchemaVersion, board, workload.nets().front().compiled_board, 1, {}));
  return Fixture{.board = std::move(board),
                 .workload = std::move(workload),
                 .capacities = std::move(capacities)};
}

[[nodiscard]] Fixture OneNetHorizontalFixture(bool obstacle, bool zero_capacity_direct_edges) {
  board_ir::BoardData data = test_support::ValidM1BoardData();
  if (!obstacle) {
    data.obstacles.clear();
  }
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
  if (zero_capacity_direct_edges) {
    for (std::int64_t x = 0; x < 10; ++x) {
      overrides.push_back(ResourceCapacityOverride{
          .resource = routing::EdgeResourceKey{.layer = 0,
                                               .lattice_x = x,
                                               .lattice_y = 0,
                                               .direction = geometry_compiler::Direction::kEast},
          .capacity_units = 0,
      });
    }
  }
  ResourceCapacityModel capacities = Capacities(
      BuildResourceCapacityModel(kResourceCapacityModelSchemaVersion, board,
                                 workload.nets().front().compiled_board, 1, std::move(overrides)));
  return Fixture{.board = std::move(board),
                 .workload = std::move(workload),
                 .capacities = std::move(capacities)};
}

[[nodiscard]] Fixture ContestedChannelFixture() {
  board_ir::BoardData data = test_support::ValidM1TwoNetBoardData();
  data.obstacles.clear();
  data.terminals[2].center = {.x = 0, .y = 40};
  data.terminals[2].connection_region = {.min = {.x = 0, .y = 40}, .max = {.x = 0, .y = 40}};
  data.terminals[3].center = {.x = 100, .y = 40};
  data.terminals[3].connection_region = {.min = {.x = 100, .y = 40}, .max = {.x = 100, .y = 40}};
  board_ir::BoardSnapshot board = Snapshot(std::move(data));
  geometry_compiler::CompilerProfile profile = test_support::DefaultCompilerProfile({0});
  profile.heading_mask = static_cast<board_ir::HeadingMask>(board_ir::Heading::kHorizontal) |
                         static_cast<board_ir::HeadingMask>(board_ir::Heading::kVertical);
  profile.compilation_roi = {.min = {.x = 0, .y = 0}, .max = {.x = 100, .y = 40}};
  profile.active_regions = {
      {.layer = 0, .bounds = {.min = {.x = 0, .y = 0}, .max = {.x = 0, .y = 40}}},
      {.layer = 0, .bounds = {.min = {.x = 100, .y = 0}, .max = {.x = 100, .y = 40}}},
      {.layer = 0, .bounds = {.min = {.x = 0, .y = 20}, .max = {.x = 100, .y = 20}}},
  };
  board_ir::RoutingProfile second = board.data().routing_profile;
  second.net = board.data().nets[1].ref;
  const std::array specs = {
      MultiNetRoutingSpec{
          .routing_profile = board.data().routing_profile, .start_layer = 0, .goal_layer = 0},
      MultiNetRoutingSpec{.routing_profile = second, .start_layer = 0, .goal_layer = 0},
  };
  MultiNetWorkload workload = Workload(
      BuildMultiNetWorkload(kMultiNetWorkloadSchemaVersion, board, profile, specs, specs.size()));
  ResourceCapacityModel capacities = Capacities(BuildResourceCapacityModel(
      kResourceCapacityModelSchemaVersion, board, workload.nets().front().compiled_board, 1, {}));
  return Fixture{.board = std::move(board),
                 .workload = std::move(workload),
                 .capacities = std::move(capacities)};
}

[[nodiscard]] Fixture LongSpatiallyPrunedStaircaseFixture() {
  constexpr std::size_t kPrimitiveCount = 1'420;
  static_assert(kPrimitiveCount > 1'414 && kPrimitiveCount % 2U == 0);
  board_ir::BoardData data = test_support::ValidM1BoardData();
  data.obstacles.clear();
  constexpr std::int64_t kGoalCoordinate = static_cast<std::int64_t>(kPrimitiveCount / 2U) * 20;
  data.terminals[1].center = {.x = kGoalCoordinate, .y = kGoalCoordinate};
  data.terminals[1].connection_region = {
      .min = {.x = kGoalCoordinate, .y = kGoalCoordinate},
      .max = {.x = kGoalCoordinate, .y = kGoalCoordinate},
  };
  data.routing_profile.allowed_headings =
      static_cast<board_ir::HeadingMask>(board_ir::Heading::kHorizontal) |
      static_cast<board_ir::HeadingMask>(board_ir::Heading::kVertical);
  board_ir::BoardSnapshot board = Snapshot(std::move(data));
  geometry_compiler::CompilerProfile profile = test_support::DefaultCompilerProfile({0});
  profile.heading_mask = board.data().routing_profile.allowed_headings;
  profile.compilation_roi = {
      .min = {.x = 0, .y = 0},
      .max = {.x = kGoalCoordinate, .y = kGoalCoordinate},
  };
  profile.active_regions.clear();
  profile.active_regions.reserve(kPrimitiveCount);
  std::int64_t x = 0;
  std::int64_t y = 0;
  for (std::size_t index = 0; index < kPrimitiveCount; ++index) {
    const bool horizontal = index % 2U == 0;
    const std::int64_t next_x = horizontal ? x + 20 : x;
    const std::int64_t next_y = horizontal ? y : y + 20;
    profile.active_regions.push_back(geometry_compiler::ActiveRegion{
        .layer = 0,
        .bounds = {.min = {.x = x, .y = y}, .max = {.x = next_x, .y = next_y}},
    });
    x = next_x;
    y = next_y;
  }
  const std::array specs = {MultiNetRoutingSpec{
      .routing_profile = board.data().routing_profile, .start_layer = 0, .goal_layer = 0}};
  MultiNetWorkload workload = Workload(
      BuildMultiNetWorkload(kMultiNetWorkloadSchemaVersion, board, profile, specs, specs.size()));
  ResourceCapacityModel capacities = Capacities(BuildResourceCapacityModel(
      kResourceCapacityModelSchemaVersion, board, workload.nets().front().compiled_board, 1, {}));
  return Fixture{.board = std::move(board),
                 .workload = std::move(workload),
                 .capacities = std::move(capacities)};
}

void ExpectSameReplay(const SequentialNegotiatedBaselineResult& left,
                      const SequentialNegotiatedBaselineResult& right) {
  EXPECT_EQ(left.config(), right.config());
  EXPECT_EQ(left.batch_identity(), right.batch_identity());
  EXPECT_EQ(left.workload_checksum(), right.workload_checksum());
  EXPECT_EQ(left.capacity_model_checksum(), right.capacity_model_checksum());
  EXPECT_EQ(left.terminal_reason(), right.terminal_reason());
  EXPECT_EQ(left.counters(), right.counters());
  EXPECT_EQ(left.columns(), right.columns());
  EXPECT_EQ(left.sweeps(), right.sweeps());
  EXPECT_EQ(left.final_world().world_checksum, right.final_world().world_checksum);
  EXPECT_EQ(left.successor_price_state(), right.successor_price_state());
  EXPECT_EQ(left.session_checksum(), right.session_checksum());
  ASSERT_EQ(left.final_pools().size(), right.final_pools().size());
  for (std::size_t index = 0; index < left.final_pools().size(); ++index) {
    ASSERT_EQ(left.final_pools()[index].net, right.final_pools()[index].net);
    ASSERT_EQ(left.final_pools()[index].candidates.size(),
              right.final_pools()[index].candidates.size());
    if (!left.final_pools()[index].candidates.empty()) {
      EXPECT_EQ(*left.final_pools()[index].candidates.front(),
                *right.final_pools()[index].candidates.front());
    }
  }
}

TEST(SequentialNegotiatedBaselineTest,
     RoutesInCanonicalSequenceAndReplaysTheIndependentFeasibleWorld) {
  const Fixture fixture = TwoNetFixture();
  const SequentialNegotiatedBaselineConfig config = Config();
  const SequentialNegotiatedBaselineResult first = Executed(ExecuteSequentialNegotiatedBaseline(
      fixture.board, fixture.workload, fixture.capacities, config));
  const SequentialNegotiatedBaselineResult repeated = Executed(ExecuteSequentialNegotiatedBaseline(
      fixture.board, fixture.workload, fixture.capacities, config));

  EXPECT_EQ(first.terminal_reason(), SequentialNegotiatedTerminalReason::kFeasible);
  EXPECT_EQ(first.counters().completed_sweeps, 1U);
  EXPECT_EQ(first.counters().route_queries, 2U);
  EXPECT_EQ(first.counters().admitted_candidates, 2U);
  EXPECT_EQ(first.final_world().selected_net_count, 2U);
  EXPECT_EQ(first.final_world().no_candidate_net_count, 0U);
  EXPECT_EQ(first.final_world().total_overuse_units, 0U);
  ASSERT_EQ(first.columns().size(), 2U);
  EXPECT_LT(first.columns()[0].query_identity, first.columns()[1].query_identity);
  EXPECT_EQ(first.columns()[0].net, fixture.workload.nets()[0].request.net);
  EXPECT_EQ(first.columns()[1].net, fixture.workload.nets()[1].request.net);
  EXPECT_NE(first.batch_identity(), 0U);
  EXPECT_TRUE(first.columns()[0].candidate_semantic_checksum.has_value());
  EXPECT_TRUE(first.columns()[1].candidate_semantic_checksum.has_value());
  EXPECT_NE(first.session_checksum(), 0U);
  ExpectSameReplay(first, repeated);
}

TEST(SequentialNegotiatedBaselineTest, UntouchedDefaultConfigurationExecutesSupportedBoard) {
  const Fixture fixture = OneNetHorizontalFixture(false, false);
  const SequentialNegotiatedBaselineExecution execution = ExecuteSequentialNegotiatedBaseline(
      fixture.board, fixture.workload, fixture.capacities, SequentialNegotiatedBaselineConfig{});
  ASSERT_TRUE(std::holds_alternative<SequentialNegotiatedBaselineResult>(execution))
      << (std::holds_alternative<SequentialNegotiatedBaselineError>(execution)
              ? std::string(std::get<SequentialNegotiatedBaselineError>(execution).invariant_id)
              : std::string{});
  const SequentialNegotiatedBaselineResult& result =
      std::get<SequentialNegotiatedBaselineResult>(execution);
  EXPECT_EQ(result.terminal_reason(), SequentialNegotiatedTerminalReason::kFeasible);
  EXPECT_EQ(result.counters().completed_sweeps, 1U);
}

TEST(SequentialNegotiatedBaselineTest, DisconnectedNetProducesOneExplicitEmptyPool) {
  const Fixture fixture = OneNetHorizontalFixture(true, false);
  const SequentialNegotiatedBaselineResult result = Executed(ExecuteSequentialNegotiatedBaseline(
      fixture.board, fixture.workload, fixture.capacities, Config()));

  EXPECT_EQ(result.terminal_reason(), SequentialNegotiatedTerminalReason::kNoAdmissibleCandidate);
  ASSERT_EQ(result.columns().size(), 1U);
  EXPECT_EQ(result.columns().front().outcome,
            SequentialNegotiatedColumnOutcome::kRouteDisconnected);
  EXPECT_FALSE(result.columns().front().retained_prior_candidate);
  ASSERT_EQ(result.final_pools().size(), 1U);
  EXPECT_TRUE(result.final_pools().front().candidates.empty());
  EXPECT_EQ(result.final_world().no_candidate_net_count, 1U);
}

TEST(SequentialNegotiatedBaselineTest, SaturatedUnavoidableResourceStopsOnlyAtACompleteFixedPoint) {
  const Fixture fixture = OneNetHorizontalFixture(false, true);
  SequentialNegotiatedBaselineConfig config = Config(4);
  config.price_config.present_step_per_overuse_unit = 1;
  config.price_config.history_step_per_overuse_unit = 1;
  config.price_config.maximum_price_per_resource = 1;
  const SequentialNegotiatedBaselineResult result = Executed(ExecuteSequentialNegotiatedBaseline(
      fixture.board, fixture.workload, fixture.capacities, config));

  EXPECT_EQ(result.terminal_reason(), SequentialNegotiatedTerminalReason::kFixedPointStalled);
  ASSERT_EQ(result.sweeps().size(), 3U);
  EXPECT_FALSE(result.sweeps()[0].winners_unchanged);
  EXPECT_TRUE(result.sweeps()[1].winners_unchanged);
  EXPECT_FALSE(result.sweeps()[1].price_values_unchanged);
  EXPECT_TRUE(result.sweeps()[2].winners_unchanged);
  EXPECT_TRUE(result.sweeps()[2].price_values_unchanged);
  EXPECT_GT(result.final_world().total_overuse_units, 0U);
  ASSERT_EQ(result.final_pools().front().candidates.size(), 1U);
  EXPECT_TRUE(result.columns()[1].candidate_semantic_checksum.has_value());
  EXPECT_EQ(result.columns()[0].candidate_semantic_checksum,
            result.columns()[1].candidate_semantic_checksum);
  EXPECT_EQ(result.columns()[1].candidate_semantic_checksum,
            result.columns()[2].candidate_semantic_checksum);
}

TEST(SequentialNegotiatedBaselineTest, CommitsEarlierNetAndRipsUpSelfBeforeEachProspectivePolicy) {
  const Fixture fixture = ContestedChannelFixture();
  const SequentialNegotiatedBaselineResult result = Executed(ExecuteSequentialNegotiatedBaseline(
      fixture.board, fixture.workload, fixture.capacities, Config(2)));

  EXPECT_EQ(result.terminal_reason(), SequentialNegotiatedTerminalReason::kSweepBudgetExhausted);
  ASSERT_EQ(result.columns().size(), 4U);
  EXPECT_EQ(result.columns()[0].prospective_congestion_resource_count, 0U);
  EXPECT_EQ(result.columns()[0].total_prospective_congestion_penalty, 0U);
  EXPECT_EQ(result.columns()[1].prospective_congestion_resource_count, 14U);
  EXPECT_EQ(result.columns()[1].total_prospective_congestion_penalty, 700U);
  // Four directed vertical resources are visible in the prospective policy but
  // are not shared by the two final routes. The ten shared horizontal resources
  // therefore acquire history 10 on sweep two. Removing the current route leaves
  // exactly the other net's usage: 14 * present 50 + 10 * history 10 = 800.
  // Forgetting self rip-up would charge a present value of 100 on all 14.
  EXPECT_EQ(result.columns()[2].prospective_congestion_resource_count, 14U);
  EXPECT_EQ(result.columns()[2].total_prospective_congestion_penalty, 800U);
  EXPECT_EQ(result.columns()[3].prospective_congestion_resource_count, 14U);
  EXPECT_EQ(result.columns()[3].total_prospective_congestion_penalty, 800U);
  EXPECT_EQ(result.final_world().overused_resource_count, 10U);
  EXPECT_EQ(result.final_world().total_overuse_units, 10U);
}

TEST(SequentialNegotiatedBaselineTest, AggregateRouteWorkPreflightHonorsEqualityBoundary) {
  const Fixture fixture = OneNetHorizontalFixture(false, false);
  SequentialNegotiatedBaselineConfig exact = Config(1);
  exact.limits.maximum_route_queries = 1;
  exact.limits.maximum_total_route_work_units = exact.route_limits.maximum_work_units;
  EXPECT_TRUE(std::holds_alternative<SequentialNegotiatedBaselineResult>(
      ExecuteSequentialNegotiatedBaseline(fixture.board, fixture.workload, fixture.capacities,
                                          exact)));

  SequentialNegotiatedBaselineConfig one_under = exact;
  --one_under.limits.maximum_total_route_work_units;
  const SequentialNegotiatedBaselineExecution rejected = ExecuteSequentialNegotiatedBaseline(
      fixture.board, fixture.workload, fixture.capacities, one_under);
  ASSERT_TRUE(std::holds_alternative<SequentialNegotiatedBaselineError>(rejected));
  const SequentialNegotiatedBaselineError& failure =
      std::get<SequentialNegotiatedBaselineError>(rejected);
  EXPECT_EQ(failure.code, SequentialNegotiatedBaselineErrorCode::kWorkBoundExceeded);
  EXPECT_EQ(failure.invariant_id, "allocator.sequential_negotiated.route_work_preflight.v1");
}

TEST(SequentialNegotiatedBaselineTest, OccupancyRecordPreflightHonorsEqualityAndOneUnder) {
  const Fixture fixture = OneNetHorizontalFixture(false, false);
  SequentialNegotiatedBaselineConfig exact = Config(1);
  exact.route_limits.maximum_reconstruction_states = 11;
  exact.limits.maximum_occupancy_resource_records = 11;
  EXPECT_TRUE(std::holds_alternative<SequentialNegotiatedBaselineResult>(
      ExecuteSequentialNegotiatedBaseline(fixture.board, fixture.workload, fixture.capacities,
                                          exact)));

  SequentialNegotiatedBaselineConfig one_under = exact;
  --one_under.limits.maximum_occupancy_resource_records;
  const SequentialNegotiatedBaselineExecution rejected = ExecuteSequentialNegotiatedBaseline(
      fixture.board, fixture.workload, fixture.capacities, one_under);
  ASSERT_TRUE(std::holds_alternative<SequentialNegotiatedBaselineError>(rejected));
  const SequentialNegotiatedBaselineError& failure =
      std::get<SequentialNegotiatedBaselineError>(rejected);
  EXPECT_EQ(failure.code, SequentialNegotiatedBaselineErrorCode::kInputBoundExceeded);
  EXPECT_EQ(failure.invariant_id, "allocator.sequential_negotiated.occupancy_record_preflight.v1");
}

TEST(SequentialNegotiatedBaselineTest, CongestionPenaltyMergeIsLinearForHighSuffixInput) {
  constexpr std::size_t kEntryCount = 50'000;
  std::vector<routing::ResourcePenalty> base;
  std::vector<routing::ResourcePenalty> congestion;
  base.reserve(kEntryCount);
  congestion.reserve(kEntryCount);
  for (std::size_t index = 0; index < kEntryCount; ++index) {
    congestion.push_back(routing::ResourcePenalty{
        .resource =
            routing::EdgeResourceKey{
                .layer = 0,
                .lattice_x = static_cast<std::int64_t>(index),
                .lattice_y = 0,
                .direction = geometry_compiler::Direction::kEast,
            },
        .additional_cost = 1,
    });
    base.push_back(routing::ResourcePenalty{
        .resource =
            routing::EdgeResourceKey{
                .layer = 0,
                .lattice_x = static_cast<std::int64_t>(kEntryCount + index),
                .lattice_y = 0,
                .direction = geometry_compiler::Direction::kEast,
            },
        .additional_cost = 2,
    });
  }

  const std::optional<internal::SequentialNegotiatedPenaltyMergeV1> merged =
      internal::MergeSequentialNegotiatedPenaltiesV1(base, congestion);
  ASSERT_TRUE(merged.has_value());
  EXPECT_EQ(merged->visits, 2U * kEntryCount);
  EXPECT_EQ(merged->penalties.size(), 2U * kEntryCount);
  EXPECT_TRUE(std::ranges::is_sorted(merged->penalties, {}, &routing::ResourcePenalty::resource));
  EXPECT_EQ(merged->penalties.front().additional_cost, 1U);
  EXPECT_EQ(merged->penalties.back().additional_cost, 2U);
}

TEST(SequentialNegotiatedBaselineTest, PerQueryCpuWorkHonorsEqualityAndRejectsOneUnder) {
  const Fixture fixture = OneNetHorizontalFixture(false, false);
  SequentialNegotiatedBaselineConfig probe_config = Config(1);
  const SequentialNegotiatedBaselineResult probe = Executed(ExecuteSequentialNegotiatedBaseline(
      fixture.board, fixture.workload, fixture.capacities, probe_config));
  ASSERT_EQ(probe.columns().size(), 1U);
  const std::uint64_t exact_work = probe.columns().front().route_work_units;
  ASSERT_GT(exact_work, 1U);

  SequentialNegotiatedBaselineConfig exact = probe_config;
  exact.route_limits.maximum_work_units = exact_work;
  exact.limits.maximum_total_route_work_units = exact_work;
  const SequentialNegotiatedBaselineResult equality = Executed(ExecuteSequentialNegotiatedBaseline(
      fixture.board, fixture.workload, fixture.capacities, exact));
  EXPECT_EQ(equality.columns().front().route_work_units, exact_work);
  EXPECT_EQ(equality.columns().front().candidate_semantic_checksum,
            probe.columns().front().candidate_semantic_checksum);
  EXPECT_EQ(equality.final_world().total_intrinsic_cost, probe.final_world().total_intrinsic_cost);

  SequentialNegotiatedBaselineConfig one_under = exact;
  --one_under.route_limits.maximum_work_units;
  --one_under.limits.maximum_total_route_work_units;
  const SequentialNegotiatedBaselineExecution rejected = ExecuteSequentialNegotiatedBaseline(
      fixture.board, fixture.workload, fixture.capacities, one_under);
  ASSERT_TRUE(std::holds_alternative<SequentialNegotiatedBaselineError>(rejected));
  const SequentialNegotiatedBaselineError& failure =
      std::get<SequentialNegotiatedBaselineError>(rejected);
  EXPECT_EQ(failure.code, SequentialNegotiatedBaselineErrorCode::kWorkBoundExceeded);
  EXPECT_EQ(failure.invariant_id, "allocator.sequential_negotiated.cpu_astar_bound.v1");
}

TEST(SequentialNegotiatedBaselineTest, DraftAdmissionAndTracePreflightHonorEqualityAndOneUnder) {
  const Fixture fixture = OneNetHorizontalFixture(false, false);
  SequentialNegotiatedBaselineConfig exact = Config(1);
  const std::uint64_t maximum_policy_entries = exact.price_config.maximum_price_records +
                                               exact.limits.maximum_occupancy_resource_records +
                                               fixture.capacities.overrides().size();
  const std::optional<std::uint64_t> draft_bytes =
      internal::ComputeSequentialNegotiatedMaximumDraftBytesV1(
          exact.route_limits.maximum_reconstruction_states, maximum_policy_entries);
  ASSERT_TRUE(draft_bytes.has_value());
  const std::optional<std::uint64_t> admission_bytes =
      internal::ComputeSequentialNegotiatedAdmissionInputBytesV1(*draft_bytes,
                                                                 maximum_policy_entries);
  const std::optional<std::uint64_t> admission_work =
      internal::ComputeSequentialNegotiatedAdmissionWorkV1(
          exact.route_limits.maximum_reconstruction_states, maximum_policy_entries,
          fixture.board.data().obstacles.size(), fixture.board.data().terminals.size());
  ASSERT_TRUE(admission_bytes.has_value());
  ASSERT_TRUE(admission_work.has_value());
  exact.limits.maximum_candidate_draft_bytes = *draft_bytes;
  exact.admission_store_config.maximum_candidate_bytes_per_net = *draft_bytes;
  exact.admission_store_config.maximum_admission_input_bytes_per_transaction = *admission_bytes;
  exact.admission_store_config.maximum_admission_work_units_per_transaction = *admission_work;
  exact.limits.maximum_trace_bytes =
      kSequentialNegotiatedColumnTraceBytesV1 + kSequentialNegotiatedSweepTraceBytesV1;
  EXPECT_TRUE(std::holds_alternative<SequentialNegotiatedBaselineResult>(
      ExecuteSequentialNegotiatedBaseline(fixture.board, fixture.workload, fixture.capacities,
                                          exact)));

  SequentialNegotiatedBaselineConfig draft_under = exact;
  --draft_under.limits.maximum_candidate_draft_bytes;
  --draft_under.admission_store_config.maximum_candidate_bytes_per_net;
  const SequentialNegotiatedBaselineExecution draft_rejected = ExecuteSequentialNegotiatedBaseline(
      fixture.board, fixture.workload, fixture.capacities, draft_under);
  ASSERT_TRUE(std::holds_alternative<SequentialNegotiatedBaselineError>(draft_rejected));
  EXPECT_EQ(std::get<SequentialNegotiatedBaselineError>(draft_rejected).invariant_id,
            "allocator.sequential_negotiated.draft_shape_preflight.v1");

  SequentialNegotiatedBaselineConfig input_under = exact;
  --input_under.admission_store_config.maximum_admission_input_bytes_per_transaction;
  const SequentialNegotiatedBaselineExecution input_rejected = ExecuteSequentialNegotiatedBaseline(
      fixture.board, fixture.workload, fixture.capacities, input_under);
  ASSERT_TRUE(std::holds_alternative<SequentialNegotiatedBaselineError>(input_rejected));
  EXPECT_EQ(std::get<SequentialNegotiatedBaselineError>(input_rejected).invariant_id,
            "allocator.sequential_negotiated.admission_byte_preflight.v1");

  SequentialNegotiatedBaselineConfig work_under = exact;
  --work_under.admission_store_config.maximum_admission_work_units_per_transaction;
  const SequentialNegotiatedBaselineExecution work_rejected = ExecuteSequentialNegotiatedBaseline(
      fixture.board, fixture.workload, fixture.capacities, work_under);
  ASSERT_TRUE(std::holds_alternative<SequentialNegotiatedBaselineError>(work_rejected));
  EXPECT_EQ(std::get<SequentialNegotiatedBaselineError>(work_rejected).invariant_id,
            "allocator.sequential_negotiated.admission_work_preflight.v1");

  SequentialNegotiatedBaselineConfig trace_under = exact;
  --trace_under.limits.maximum_trace_bytes;
  const SequentialNegotiatedBaselineExecution trace_rejected = ExecuteSequentialNegotiatedBaseline(
      fixture.board, fixture.workload, fixture.capacities, trace_under);
  ASSERT_TRUE(std::holds_alternative<SequentialNegotiatedBaselineError>(trace_rejected));
  EXPECT_EQ(std::get<SequentialNegotiatedBaselineError>(trace_rejected).invariant_id,
            "allocator.sequential_negotiated.trace_byte_preflight.v1");
}

TEST(SequentialNegotiatedBaselineTest,
     AdmissionWorkPreflightCoversSpatiallyPrunedRoutesAbovePairBudgetRoot) {
  const Fixture fixture = LongSpatiallyPrunedStaircaseFixture();
  SequentialNegotiatedBaselineConfig exact = Config(1);
  exact.route_limits.maximum_reconstruction_states = 5'000;
  exact.limits.maximum_occupancy_resource_records = 10'000;
  const std::uint64_t maximum_policy_entries = exact.price_config.maximum_price_records +
                                               exact.limits.maximum_occupancy_resource_records +
                                               fixture.capacities.overrides().size();
  const std::optional<std::uint64_t> exact_work =
      internal::ComputeSequentialNegotiatedAdmissionWorkV1(
          exact.route_limits.maximum_reconstruction_states, maximum_policy_entries,
          fixture.board.data().obstacles.size(), fixture.board.data().terminals.size());
  ASSERT_TRUE(exact_work.has_value());
  exact.admission_store_config.maximum_admission_work_units_per_transaction = *exact_work;

  const SequentialNegotiatedBaselineResult result = Executed(ExecuteSequentialNegotiatedBaseline(
      fixture.board, fixture.workload, fixture.capacities, exact));
  ASSERT_EQ(result.final_pools().size(), 1U);
  ASSERT_EQ(result.final_pools().front().candidates.size(), 1U);
  EXPECT_GT(result.final_pools().front().candidates.front()->data().geometry.size(), 1'414U);

  SequentialNegotiatedBaselineConfig one_under = exact;
  --one_under.admission_store_config.maximum_admission_work_units_per_transaction;
  const SequentialNegotiatedBaselineExecution rejected = ExecuteSequentialNegotiatedBaseline(
      fixture.board, fixture.workload, fixture.capacities, one_under);
  ASSERT_TRUE(std::holds_alternative<SequentialNegotiatedBaselineError>(rejected));
  const SequentialNegotiatedBaselineError& failure =
      std::get<SequentialNegotiatedBaselineError>(rejected);
  EXPECT_EQ(failure.code, SequentialNegotiatedBaselineErrorCode::kInvalidConfiguration);
  EXPECT_EQ(failure.invariant_id, "allocator.sequential_negotiated.admission_work_preflight.v1");
}

TEST(SequentialNegotiatedBaselineTest, KnownUnmappedConflictRequiresResourceRefinement) {
  const Fixture fixture = OneNetHorizontalFixture(false, false);
  SequentialNegotiatedBaselineConfig config = Config();
  config.known_unmapped_exact_conflict_count = 1;
  const SequentialNegotiatedBaselineExecution rejected = ExecuteSequentialNegotiatedBaseline(
      fixture.board, fixture.workload, fixture.capacities, config);
  ASSERT_TRUE(std::holds_alternative<SequentialNegotiatedBaselineError>(rejected));
  const SequentialNegotiatedBaselineError& failure =
      std::get<SequentialNegotiatedBaselineError>(rejected);
  EXPECT_EQ(failure.code, SequentialNegotiatedBaselineErrorCode::kResourceRefinementRequired);
  EXPECT_EQ(failure.required, 1U);
}

TEST(SequentialNegotiatedBaselineTest, RefinementShortCircuitStillAuthenticatesInputs) {
  const Fixture fixture = OneNetHorizontalFixture(false, false);
  board_ir::BoardData different_data = test_support::ValidM1BoardData();
  different_data.obstacles.clear();
  ++different_data.revision;
  const board_ir::BoardSnapshot different_board = Snapshot(std::move(different_data));
  SequentialNegotiatedBaselineConfig config = Config();
  config.known_unmapped_exact_conflict_count = 1;
  const SequentialNegotiatedBaselineExecution rejected = ExecuteSequentialNegotiatedBaseline(
      different_board, fixture.workload, fixture.capacities, config);
  ASSERT_TRUE(std::holds_alternative<SequentialNegotiatedBaselineError>(rejected));
  const SequentialNegotiatedBaselineError& failure =
      std::get<SequentialNegotiatedBaselineError>(rejected);
  EXPECT_EQ(failure.code, SequentialNegotiatedBaselineErrorCode::kAssociationMismatch);
}

TEST(SequentialNegotiatedBaselineTest, SemanticAndSessionEncodersHaveStableGoldens) {
  const Fixture fixture = OneNetHorizontalFixture(false, false);
  const SequentialNegotiatedBaselineResult result = Executed(ExecuteSequentialNegotiatedBaseline(
      fixture.board, fixture.workload, fixture.capacities, Config()));
  ASSERT_EQ(result.final_pools().front().candidates.size(), 1U);
  const std::uint64_t semantic = internal::ComputeSequentialNegotiatedCandidateSemanticChecksumV1(
      *result.final_pools().front().candidates.front());
  EXPECT_EQ(semantic, 17'976'206'991'628'397'487ULL);
  EXPECT_EQ(result.session_checksum(), 13'612'100'766'122'023'035ULL);
}

}  // namespace
}  // namespace apgar::allocator
