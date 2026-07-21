#include "apgar/allocator/multi_world.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <memory>
#include <ranges>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

#include "src/allocator/multi_world_internal.h"
#include "tests/support/board_builder.h"
#include "tests/support/candidate_builder.h"
#include "tests/support/compiler_builder.h"
#include "tests/support/google_test.h"
#include "tests/support/routing_builder.h"

namespace apgar::allocator {
namespace {

using candidates::CandidateAdmissionContext;
using candidates::CandidateId;
using candidates::CandidateStore;
using candidates::CandidateStoreAdmissionResult;
using candidates::StoredCandidate;
using routing::LayerSegment;

template <typename State, typename Error>
[[nodiscard]] State Built(std::variant<State, Error> result) {
  EXPECT_TRUE(std::holds_alternative<State>(result));
  if (!std::holds_alternative<State>(result)) {
    std::abort();
  }
  return std::get<State>(std::move(result));
}

[[nodiscard]] MultiWorldExecution Executed(MultiWorldExecutionResult result) {
  EXPECT_TRUE(std::holds_alternative<MultiWorldExecution>(result))
      << (std::holds_alternative<MultiWorldExecutionError>(result)
              ? std::string(std::get<MultiWorldExecutionError>(result).invariant_id)
              : std::string{});
  if (!std::holds_alternative<MultiWorldExecution>(result)) {
    std::abort();
  }
  return std::get<MultiWorldExecution>(std::move(result));
}

[[nodiscard]] StoredCandidate Stored(CandidateStoreAdmissionResult result) {
  EXPECT_TRUE(std::holds_alternative<StoredCandidate>(result));
  if (!std::holds_alternative<StoredCandidate>(result)) {
    std::abort();
  }
  return std::get<StoredCandidate>(std::move(result));
}

[[nodiscard]] candidates::CandidateStoreConfig StoreConfig() {
  return candidates::CandidateStoreConfig{
      .maximum_candidates_per_net = 8,
      .maximum_candidate_bytes_per_net = 8U * 1024U * 1024U,
      .maximum_rejection_records = 32,
      .maximum_pin_lease_items_per_transaction = 16,
      .maximum_expected_pools_per_invocation = 8,
      .maximum_expected_candidates_per_invocation = 16,
  };
}

[[nodiscard]] std::vector<LayerSegment> RoutedAt(std::int64_t start_y, std::int64_t route_y) {
  if (start_y == route_y) {
    return {LayerSegment{
        .layer = 0,
        .centerline = {.start = {.x = 0, .y = start_y}, .end = {.x = 100, .y = start_y}},
    }};
  }
  return {
      LayerSegment{.layer = 0,
                   .centerline = {.start = {.x = 0, .y = start_y}, .end = {.x = 0, .y = route_y}}},
      LayerSegment{
          .layer = 0,
          .centerline = {.start = {.x = 0, .y = route_y}, .end = {.x = 100, .y = route_y}}},
      LayerSegment{
          .layer = 0,
          .centerline = {.start = {.x = 100, .y = route_y}, .end = {.x = 100, .y = start_y}}},
  };
}

enum class FixturePools {
  kAlternatives,
  kDetoursOnly,
  kSecondNetEmpty,
  kAllEmpty,
};

struct MultiWorldFixture {
  board_ir::BoardSnapshot board;
  std::unique_ptr<MultiNetWorkload> workload;
  ResourceCapacityModel capacities;
  std::unique_ptr<CandidateStore> store;
  MultiWorldPoolSnapshot source;
  NegotiatedPriceState branch_state;
  std::vector<StoredCandidate> all_candidates;

  MultiWorldFixture(board_ir::BoardSnapshot board_value,
                    std::unique_ptr<MultiNetWorkload> workload_value,
                    ResourceCapacityModel capacities_value,
                    std::unique_ptr<CandidateStore> store_value, std::vector<CandidatePool> pools,
                    NegotiatedPriceState branch_state_value,
                    std::vector<StoredCandidate> all_candidates_value)
      : board(std::move(board_value)),
        workload(std::move(workload_value)),
        capacities(std::move(capacities_value)),
        store(std::move(store_value)),
        source{.capacities = capacities,
               .allocator_limits = OneWorldAllocatorLimits{},
               .pools = std::move(pools),
               .workload = workload.get()},
        branch_state(std::move(branch_state_value)),
        all_candidates(std::move(all_candidates_value)) {}
};

[[nodiscard]] MultiWorldFixture BuildFixture(
    FixturePools pool_mode = FixturePools::kAlternatives,
    std::uint32_t maximum_price_iterations = 4,
    candidates::CandidateStoreConfig store_config = StoreConfig()) {
  board_ir::BoardData data = test_support::ValidM1TwoNetBoardData();
  data.obstacles.clear();
  board_ir::BoardSnapshot board = test_support::Snapshot(std::move(data));
  const geometry_compiler::CompilerProfile compiler_profile =
      test_support::DefaultCompilerProfile({0, 31});
  geometry_compiler::CompilerProfile expanded_compiler_profile = compiler_profile;
  expanded_compiler_profile.compilation_roi.max.y = 60;
  for (geometry_compiler::ActiveRegion& region : expanded_compiler_profile.active_regions) {
    region.bounds.max.y = 60;
  }
  board_ir::RoutingProfile second_profile = board.data().routing_profile;
  second_profile.net = board.data().nets[1].ref;
  const std::array specs = {
      MultiNetRoutingSpec{
          .routing_profile = board.data().routing_profile, .start_layer = 0, .goal_layer = 0},
      MultiNetRoutingSpec{.routing_profile = second_profile, .start_layer = 0, .goal_layer = 0},
  };
  auto workload = std::make_unique<MultiNetWorkload>(Built<MultiNetWorkload>(BuildMultiNetWorkload(
      kMultiNetWorkloadSchemaVersion, board, expanded_compiler_profile, specs, specs.size())));
  ResourceCapacityModel capacities = Built<ResourceCapacityModel>(BuildResourceCapacityModel(
      kResourceCapacityModelSchemaVersion, board, workload->nets().front().compiled_board, 1, {}));
  auto store = std::make_unique<CandidateStore>(store_config);

  std::vector<CandidatePool> pools;
  std::vector<StoredCandidate> all_candidates;
  pools.reserve(workload->nets().size());
  for (std::size_t net_index = 0; net_index < workload->nets().size(); ++net_index) {
    const PreparedNetRoutingContext& context = workload->nets()[net_index];
    const bool empty = pool_mode == FixturePools::kAllEmpty ||
                       (pool_mode == FixturePools::kSecondNetEmpty && net_index == 1);
    if (empty) {
      pools.push_back(CandidatePool{.net = context.request.net, .candidates = {}});
      continue;
    }

    const std::size_t first_route = pool_mode == FixturePools::kDetoursOnly ? 1 : 0;
    for (std::size_t route_index = first_route; route_index < 2; ++route_index) {
      routing::PlanarRouteRequest request = context.request;
      request.candidate_policy.deterministic_seed = 0xA000U + net_index;
      request.candidate_policy.candidate_ordinal = static_cast<std::uint32_t>(route_index);
      std::vector<LayerSegment> segments;
      if (net_index == 0) {
        segments = RoutedAt(0, route_index == 0 ? 0 : -20);
      } else if (route_index == 0) {
        segments = {
            LayerSegment{.layer = 0,
                         .centerline = {.start = {.x = 0, .y = 20}, .end = {.x = 20, .y = 20}}},
            LayerSegment{.layer = 0,
                         .centerline = {.start = {.x = 20, .y = 20}, .end = {.x = 20, .y = 0}}},
            LayerSegment{.layer = 0,
                         .centerline = {.start = {.x = 20, .y = 0}, .end = {.x = 80, .y = 0}}},
            LayerSegment{.layer = 0,
                         .centerline = {.start = {.x = 80, .y = 0}, .end = {.x = 80, .y = 20}}},
            LayerSegment{.layer = 0,
                         .centerline = {.start = {.x = 80, .y = 20}, .end = {.x = 100, .y = 20}}},
        };
      } else {
        segments = RoutedAt(20, 50);
      }
      candidates::GeneratedRouteCandidate draft = test_support::CandidateDraftAlongSegments(
          board, context.compiled_board, request, segments,
          candidates::CandidateSchedulingIdentity{
              .batch_identity = 100U + net_index,
              .query_identity = 1U + net_index * 2U + route_index,
          });
      const CandidateAdmissionContext admission{
          .board = board, .compiled_board = context.compiled_board, .request = request};
      all_candidates.push_back(Stored(store->Admit(admission, std::move(draft))));
    }
    pools.push_back(CandidatePool{.net = context.request.net,
                                  .candidates = store->Enumerate(context.request.net)});
  }

  const NegotiatedPriceConfig price_config{
      .present_step_per_overuse_unit = 4,
      .history_step_per_overuse_unit = 4,
      .maximum_price_per_resource = 1'000,
      .maximum_iterations = maximum_price_iterations,
      .maximum_price_records = 10'000,
  };
  NegotiatedPriceState branch_state = Built<NegotiatedPriceState>(BuildInitialNegotiatedPriceState(
      kNegotiatedPriceStateSchemaVersion, capacities, *workload, price_config));
  return MultiWorldFixture(std::move(board), std::move(workload), std::move(capacities),
                           std::move(store), std::move(pools), std::move(branch_state),
                           std::move(all_candidates));
}

[[nodiscard]] MultiWorldExecutionConfig ExecutionConfig() {
  return MultiWorldExecutionConfig{
      .maximum_worlds = 16,
      .maximum_total_selection_rounds = 64,
      .maximum_total_candidate_evaluations = 1'000,
      .maximum_total_candidate_span_visits = 10'000,
      .maximum_total_resource_work_units = 10'000'000,
      .maximum_total_net_outcomes = 1'000,
      .maximum_pareto_comparisons = 1'000,
      .maximum_buffered_terminal_selection_records = 1'000,
      .maximum_buffered_terminal_resource_records = 10'000,
      .maximum_buffered_terminal_price_records = 10'000,
      .maximum_retained_worlds = 16,
      .maximum_retained_selection_records = 1'000,
      .maximum_retained_resource_records = 10'000,
      .maximum_retained_price_records = 10'000,
      .maximum_retained_winner_pins = 16,
      .maximum_near_feasible_missing_nets = 16,
      .maximum_near_feasible_overuse_units = std::numeric_limits<std::uint64_t>::max(),
  };
}

struct ManualWorldRun {
  NegotiatedPriceState price_state;
  OneWorldAllocation world;
  MultiWorldTerminalReason terminal_reason;
  std::vector<MultiWorldRoundTrace> trace;
};

[[nodiscard]] std::uint64_t ResourceWorkUnitsPerCandidatePass(const MultiWorldFixture& fixture) {
  std::uint64_t per_pass = fixture.source.allocator_limits.maximum_resource_records;
  for (const CandidatePool& pool : fixture.source.pools) {
    std::uint64_t pool_maximum = 0;
    for (const StoredCandidate& candidate : pool.candidates) {
      std::uint64_t candidate_total = 0;
      for (const candidates::PhysicalEdgeSpan& span : candidate->data().resources) {
        candidate_total += span.edge_count;
      }
      pool_maximum = std::max(pool_maximum, candidate_total);
    }
    per_pass += pool_maximum;
  }
  return per_pass;
}

[[nodiscard]] std::vector<routing::EdgeResourceKey> FirstDistinctResources(
    const MultiWorldFixture& fixture, std::size_t requested_count) {
  std::set<routing::EdgeResourceKey> unique;
  for (const CandidatePool& pool : fixture.source.pools) {
    for (const StoredCandidate& candidate : pool.candidates) {
      for (const candidates::PhysicalEdgeSpan& span : candidate->data().resources) {
        const geometry_compiler::DirectionDelta delta =
            candidates::ResourceSpanStorageDelta(span.direction);
        for (std::uint32_t offset = 0; offset < span.edge_count; ++offset) {
          unique.emplace(routing::EdgeResourceKey{
              .layer = span.layer,
              .lattice_x = span.lattice_x + static_cast<std::int64_t>(delta.x) * offset,
              .lattice_y = span.lattice_y + static_cast<std::int64_t>(delta.y) * offset,
              .direction = span.direction,
          });
          if (unique.size() == requested_count) {
            return std::vector<routing::EdgeResourceKey>(unique.begin(), unique.end());
          }
        }
      }
    }
  }
  return std::vector<routing::EdgeResourceKey>(unique.begin(), unique.end());
}

[[nodiscard]] NegotiatedPriceState DifferentWorkloadBranchState(const MultiWorldFixture& fixture) {
  geometry_compiler::CompilerProfile compiler_profile =
      test_support::DefaultCompilerProfile({0, 31});
  compiler_profile.compilation_roi.max.y = 60;
  for (geometry_compiler::ActiveRegion& region : compiler_profile.active_regions) {
    region.bounds.max.y = 60;
  }
  const std::array specs = {MultiNetRoutingSpec{
      .routing_profile = fixture.board.data().routing_profile, .start_layer = 0, .goal_layer = 0}};
  MultiNetWorkload other = Built<MultiNetWorkload>(BuildMultiNetWorkload(
      kMultiNetWorkloadSchemaVersion, fixture.board, compiler_profile, specs, specs.size()));
  EXPECT_NE(other.workload_checksum(), fixture.workload->workload_checksum());
  return Built<NegotiatedPriceState>(
      BuildInitialNegotiatedPriceState(kNegotiatedPriceStateSchemaVersion, fixture.capacities,
                                       other, fixture.branch_state.config()));
}

[[nodiscard]] ManualWorldRun RunManualWorld(const MultiWorldFixture& fixture,
                                            const MultiWorldSchedule& schedule) {
  NegotiatedPriceState state = fixture.branch_state;
  std::optional<OneWorldAllocation> terminal_world;
  MultiWorldTerminalReason terminal_reason = MultiWorldTerminalReason::kSelectionRoundLimit;
  std::vector<MultiWorldRoundTrace> trace;
  for (std::uint32_t round = 0; round < schedule.maximum_selection_rounds; ++round) {
    PriceSnapshot prices =
        Built<PriceSnapshot>(BuildPriceSnapshotForState(fixture.capacities, state));
    OneWorldAllocationRequest request{
        .associations = fixture.capacities.associations(),
        .capacities = fixture.capacities,
        .prices = std::move(prices),
        .intrinsic_cost_weight = schedule.search_intrinsic_cost_weight,
        .limits = fixture.source.allocator_limits,
        .pools = fixture.source.pools,
        .workload = fixture.workload.get(),
    };
    OneWorldAllocation world = Built<OneWorldAllocation>(AllocateOneWorld(request));
    trace.push_back(MultiWorldRoundTrace{
        .round_index = round,
        .price_state_checksum = state.state_checksum(),
        .world_checksum = world.world_checksum,
    });
    if (world.no_candidate_net_count != 0) {
      terminal_reason = MultiWorldTerminalReason::kNoCandidateWithoutRegeneration;
      terminal_world.emplace(std::move(world));
      break;
    }
    if (world.total_overuse_units == 0) {
      terminal_reason = MultiWorldTerminalReason::kFeasible;
      terminal_world.emplace(std::move(world));
      break;
    }
    if (round + 1U == schedule.maximum_selection_rounds) {
      terminal_reason = MultiWorldTerminalReason::kSelectionRoundLimit;
      terminal_world.emplace(std::move(world));
      break;
    }
    state = Built<NegotiatedPriceState>(UpdateNegotiatedPrices(state, request, world));
  }
  EXPECT_TRUE(terminal_world.has_value());
  if (!terminal_world.has_value()) {
    std::abort();
  }
  return ManualWorldRun{.price_state = std::move(state),
                        .world = std::move(*terminal_world),
                        .terminal_reason = terminal_reason,
                        .trace = std::move(trace)};
}

[[nodiscard]] const MultiWorldSummary& SummaryFor(const MultiWorldExecution& execution,
                                                  std::uint64_t schedule_key) {
  const auto found = std::ranges::find(
      execution.summaries(), schedule_key,
      [](const MultiWorldSummary& summary) { return summary.schedule.schedule_key; });
  EXPECT_NE(found, execution.summaries().end());
  if (found == execution.summaries().end()) {
    std::abort();
  }
  return *found;
}

[[nodiscard]] const RetainedMultiWorld* RetainedFor(const MultiWorldExecution& execution,
                                                    std::uint64_t schedule_key) {
  const auto found = std::ranges::find(
      execution.retained_worlds(), schedule_key,
      [](const RetainedMultiWorld& world) { return world.schedule.schedule_key; });
  return found == execution.retained_worlds().end() ? nullptr : &*found;
}

void ExpectRetainedEqual(const MultiWorldExecution& left, const MultiWorldExecution& right) {
  ASSERT_EQ(left.retained_worlds().size(), right.retained_worlds().size());
  for (std::size_t index = 0; index < left.retained_worlds().size(); ++index) {
    const RetainedMultiWorld& lhs = left.retained_worlds()[index];
    const RetainedMultiWorld& rhs = right.retained_worlds()[index];
    EXPECT_EQ(lhs.world_identity, rhs.world_identity);
    EXPECT_EQ(lhs.schedule, rhs.schedule);
    EXPECT_EQ(lhs.price_state, rhs.price_state);
    EXPECT_EQ(lhs.world.world_checksum, rhs.world.world_checksum);
  }
}

TEST(MultiWorldTest, MatchesIndependentBranchesAndIsPermutationInvariant) {
  MultiWorldFixture fixture = BuildFixture();
  const std::array schedules = {
      MultiWorldSchedule{
          .schedule_key = 11, .search_intrinsic_cost_weight = 1, .maximum_selection_rounds = 1},
      MultiWorldSchedule{
          .schedule_key = 13, .search_intrinsic_cost_weight = 1, .maximum_selection_rounds = 2},
      MultiWorldSchedule{
          .schedule_key = 17, .search_intrinsic_cost_weight = 10, .maximum_selection_rounds = 2},
  };
  MultiWorldExecution first = Executed(
      ExecuteMultiWorldCpu(kMultiWorldExecutionSchemaVersion, fixture.source, fixture.branch_state,
                           schedules, *fixture.store, ExecutionConfig()));

  for (const MultiWorldSchedule& schedule : schedules) {
    const ManualWorldRun manual = RunManualWorld(fixture, schedule);
    const MultiWorldSummary& summary = SummaryFor(first, schedule.schedule_key);
    EXPECT_EQ(summary.terminal_reason, manual.terminal_reason);
    EXPECT_EQ(summary.trace, manual.trace);
    EXPECT_EQ(summary.selected_net_count, manual.world.selected_net_count);
    EXPECT_EQ(summary.no_candidate_net_count, manual.world.no_candidate_net_count);
    EXPECT_EQ(summary.overused_resource_count, manual.world.overused_resource_count);
    EXPECT_EQ(summary.total_overuse_units, manual.world.total_overuse_units);
    EXPECT_EQ(summary.total_intrinsic_cost, manual.world.total_intrinsic_cost);
    if (const RetainedMultiWorld* retained = RetainedFor(first, schedule.schedule_key);
        retained != nullptr) {
      EXPECT_EQ(retained->price_state, manual.price_state);
      EXPECT_EQ(retained->world.world_checksum, manual.world.world_checksum);
    }
  }
  EXPECT_EQ(SummaryFor(first, 11).terminal_reason, MultiWorldTerminalReason::kSelectionRoundLimit);
  EXPECT_EQ(SummaryFor(first, 13).terminal_reason, MultiWorldTerminalReason::kFeasible);
  EXPECT_GT(SummaryFor(first, 11).total_overuse_units, SummaryFor(first, 13).total_overuse_units);
  EXPECT_LT(SummaryFor(first, 11).total_intrinsic_cost, SummaryFor(first, 13).total_intrinsic_cost);
  EXPECT_EQ(first.counters().selection_rounds, 5U);
  EXPECT_EQ(first.counters().price_updates, 2U);
  EXPECT_EQ(first.counters().candidate_evaluations,
            (1U + first.counters().selection_rounds + first.counters().price_updates) * 4U);
  std::uint64_t source_span_count = 0;
  for (const CandidatePool& pool : fixture.source.pools) {
    for (const StoredCandidate& candidate : pool.candidates) {
      source_span_count += candidate->data().resources.size();
    }
  }
  EXPECT_EQ(first.counters().candidate_span_visits,
            (1U + first.counters().selection_rounds + first.counters().price_updates) *
                source_span_count);
  EXPECT_EQ(first.counters().resource_work_units,
            (1U + first.counters().selection_rounds + first.counters().price_updates) *
                ResourceWorkUnitsPerCandidatePass(fixture));
  EXPECT_EQ(first.counters().net_outcomes,
            (1U + first.counters().selection_rounds + first.counters().price_updates) * 2U);

  MultiWorldPoolSnapshot reordered = fixture.source;
  std::ranges::reverse(reordered.pools);
  for (CandidatePool& pool : reordered.pools) {
    std::ranges::reverse(pool.candidates);
  }
  std::array reversed_schedules = schedules;
  std::ranges::reverse(reversed_schedules);
  MultiWorldExecution repeated = Executed(
      ExecuteMultiWorldCpu(kMultiWorldExecutionSchemaVersion, reordered, fixture.branch_state,
                           reversed_schedules, *fixture.store, ExecutionConfig()));
  EXPECT_EQ(repeated.source_snapshot_checksum(), first.source_snapshot_checksum());
  EXPECT_EQ(repeated.branch_state_checksum(), first.branch_state_checksum());
  EXPECT_EQ(repeated.summaries(), first.summaries());
  EXPECT_EQ(repeated.counters(), first.counters());
  EXPECT_EQ(repeated.preferred_world_identity(), first.preferred_world_identity());
  EXPECT_EQ(repeated.execution_checksum(), first.execution_checksum());
  ExpectRetainedEqual(repeated, first);
}

TEST(MultiWorldTest, RetainsRawParetoEqualObjectivesAndPreferredLexicographicWorld) {
  MultiWorldFixture fixture = BuildFixture();
  const std::array schedules = {
      MultiWorldSchedule{
          .schedule_key = 5, .search_intrinsic_cost_weight = 1, .maximum_selection_rounds = 1},
      MultiWorldSchedule{
          .schedule_key = 7, .search_intrinsic_cost_weight = 9, .maximum_selection_rounds = 1},
      MultiWorldSchedule{
          .schedule_key = 11, .search_intrinsic_cost_weight = 1, .maximum_selection_rounds = 2},
  };
  MultiWorldExecution execution = Executed(
      ExecuteMultiWorldCpu(kMultiWorldExecutionSchemaVersion, fixture.source, fixture.branch_state,
                           schedules, *fixture.store, ExecutionConfig()));
  const MultiWorldSummary& equal_low_weight = SummaryFor(execution, 5);
  const MultiWorldSummary& equal_high_weight = SummaryFor(execution, 7);
  const MultiWorldSummary& feasible = SummaryFor(execution, 11);
  EXPECT_EQ(equal_low_weight.selected_net_count, equal_high_weight.selected_net_count);
  EXPECT_EQ(equal_low_weight.total_overuse_units, equal_high_weight.total_overuse_units);
  EXPECT_EQ(equal_low_weight.total_intrinsic_cost, equal_high_weight.total_intrinsic_cost);
  EXPECT_TRUE(equal_low_weight.pareto_retained);
  EXPECT_TRUE(equal_high_weight.pareto_retained);
  EXPECT_TRUE(feasible.pareto_retained);
  EXPECT_TRUE(internal::MultiWorldPreferredBeforeV1(
      internal::MultiWorldObjectiveV1{.selected_net_count = feasible.selected_net_count,
                                      .total_overuse_units = feasible.total_overuse_units,
                                      .total_intrinsic_cost = feasible.total_intrinsic_cost,
                                      .schedule_key = feasible.schedule.schedule_key},
      internal::MultiWorldObjectiveV1{.selected_net_count = equal_low_weight.selected_net_count,
                                      .total_overuse_units = equal_low_weight.total_overuse_units,
                                      .total_intrinsic_cost = equal_low_weight.total_intrinsic_cost,
                                      .schedule_key = equal_low_weight.schedule.schedule_key}));
  ASSERT_TRUE(execution.preferred_world_identity().has_value());
  EXPECT_EQ(*execution.preferred_world_identity(), feasible.world_identity);

  const internal::MultiWorldObjectiveV1 conflict{
      .selected_net_count = equal_low_weight.selected_net_count,
      .total_overuse_units = equal_low_weight.total_overuse_units,
      .total_intrinsic_cost = equal_low_weight.total_intrinsic_cost,
      .schedule_key = 5,
  };
  const internal::MultiWorldObjectiveV1 detour{
      .selected_net_count = feasible.selected_net_count,
      .total_overuse_units = feasible.total_overuse_units,
      .total_intrinsic_cost = feasible.total_intrinsic_cost,
      .schedule_key = 11,
  };
  EXPECT_FALSE(internal::MultiWorldDominatesV1(conflict, detour));
  EXPECT_FALSE(internal::MultiWorldDominatesV1(detour, conflict));
}

TEST(MultiWorldTest, StrictDominanceTruthTableRemovesAnAuthenticDominatedWorld) {
  const internal::MultiWorldObjectiveV1 base{
      .selected_net_count = 2,
      .total_overuse_units = 3,
      .total_intrinsic_cost = 5,
      .schedule_key = 7,
  };
  EXPECT_TRUE(
      internal::MultiWorldDominatesV1(internal::MultiWorldObjectiveV1{.selected_net_count = 3,
                                                                      .total_overuse_units = 3,
                                                                      .total_intrinsic_cost = 5,
                                                                      .schedule_key = 11},
                                      base));
  EXPECT_TRUE(
      internal::MultiWorldDominatesV1(internal::MultiWorldObjectiveV1{.selected_net_count = 2,
                                                                      .total_overuse_units = 2,
                                                                      .total_intrinsic_cost = 5,
                                                                      .schedule_key = 11},
                                      base));
  EXPECT_TRUE(
      internal::MultiWorldDominatesV1(internal::MultiWorldObjectiveV1{.selected_net_count = 2,
                                                                      .total_overuse_units = 3,
                                                                      .total_intrinsic_cost = 4,
                                                                      .schedule_key = 11},
                                      base));
  const internal::MultiWorldObjectiveV1 equal_different_key{
      .selected_net_count = 2,
      .total_overuse_units = 3,
      .total_intrinsic_cost = 5,
      .schedule_key = 13,
  };
  EXPECT_FALSE(internal::MultiWorldDominatesV1(base, equal_different_key));
  EXPECT_FALSE(internal::MultiWorldDominatesV1(equal_different_key, base));
  const internal::MultiWorldObjectiveV1 tradeoff{
      .selected_net_count = 3,
      .total_overuse_units = 4,
      .total_intrinsic_cost = 5,
      .schedule_key = 17,
  };
  EXPECT_FALSE(internal::MultiWorldDominatesV1(base, tradeoff));
  EXPECT_FALSE(internal::MultiWorldDominatesV1(tradeoff, base));
  EXPECT_TRUE(internal::MultiWorldDominatesV1(
      base, internal::MultiWorldObjectiveV1{.selected_net_count = 1,
                                            .total_overuse_units = 4,
                                            .total_intrinsic_cost = 6,
                                            .schedule_key = 19}));

  MultiWorldFixture fixture = BuildFixture();
  const std::array schedules = {
      MultiWorldSchedule{
          .schedule_key = 1, .search_intrinsic_cost_weight = 1, .maximum_selection_rounds = 2},
      MultiWorldSchedule{
          .schedule_key = 2, .search_intrinsic_cost_weight = 2, .maximum_selection_rounds = 2},
  };
  MultiWorldExecution execution = Executed(
      ExecuteMultiWorldCpu(kMultiWorldExecutionSchemaVersion, fixture.source, fixture.branch_state,
                           schedules, *fixture.store, ExecutionConfig()));
  const MultiWorldSummary& dominated = SummaryFor(execution, 1);
  const MultiWorldSummary& winner = SummaryFor(execution, 2);
  ASSERT_EQ(dominated.selected_net_count, 2U);
  ASSERT_EQ(winner.selected_net_count, 2U);
  ASSERT_EQ(dominated.total_overuse_units, 0U);
  ASSERT_EQ(winner.total_overuse_units, 0U);
  EXPECT_EQ(dominated.total_intrinsic_cost, 312U);
  EXPECT_EQ(winner.total_intrinsic_cost, 266U);
  EXPECT_TRUE(dominated.pareto_eligible);
  EXPECT_FALSE(dominated.pareto_retained);
  EXPECT_EQ(RetainedFor(execution, 1), nullptr);
  EXPECT_TRUE(winner.pareto_eligible);
  EXPECT_TRUE(winner.pareto_retained);
  EXPECT_NE(RetainedFor(execution, 2), nullptr);
  EXPECT_EQ(execution.retained_worlds().size(), 1U);
  EXPECT_EQ(execution.counters().pareto_eligible_worlds, 2U);
  EXPECT_EQ(execution.counters().pareto_comparisons, 1U);
  EXPECT_EQ(execution.counters().retained_worlds, 1U);
  EXPECT_EQ(execution.counters().retained_winner_pins, 2U);
  ASSERT_TRUE(execution.preferred_world_identity().has_value());
  EXPECT_EQ(*execution.preferred_world_identity(), winner.world_identity);
}

TEST(MultiWorldTest, ReportsTerminalReasonsEligibilityThresholdsAndEmptyLease) {
  const MultiWorldSchedule one_round{
      .schedule_key = 1, .search_intrinsic_cost_weight = 1, .maximum_selection_rounds = 1};
  const MultiWorldSchedule two_round{
      .schedule_key = 2, .search_intrinsic_cost_weight = 1, .maximum_selection_rounds = 2};

  MultiWorldFixture regular = BuildFixture();
  MultiWorldExecution regular_result = Executed(
      ExecuteMultiWorldCpu(kMultiWorldExecutionSchemaVersion, regular.source, regular.branch_state,
                           std::array{one_round, two_round}, *regular.store, ExecutionConfig()));
  const std::uint64_t actual_overuse = SummaryFor(regular_result, 1).total_overuse_units;
  ASSERT_GT(actual_overuse, 0U);
  MultiWorldExecutionConfig overuse_equality = ExecutionConfig();
  overuse_equality.maximum_near_feasible_overuse_units = actual_overuse;
  MultiWorldExecution at_overuse = Executed(ExecuteMultiWorldCpu(
      kMultiWorldExecutionSchemaVersion, regular.source, regular.branch_state,
      std::span<const MultiWorldSchedule>(&one_round, 1), *regular.store, overuse_equality));
  EXPECT_TRUE(SummaryFor(at_overuse, 1).pareto_eligible);
  --overuse_equality.maximum_near_feasible_overuse_units;
  MultiWorldExecution over_overuse = Executed(ExecuteMultiWorldCpu(
      kMultiWorldExecutionSchemaVersion, regular.source, regular.branch_state,
      std::span<const MultiWorldSchedule>(&one_round, 1), *regular.store, overuse_equality));
  EXPECT_FALSE(SummaryFor(over_overuse, 1).pareto_eligible);
  EXPECT_EQ(over_overuse.disposition(), MultiWorldExecutionDisposition::kNoEligibleWorlds);

  MultiWorldFixture incomplete = BuildFixture(FixturePools::kSecondNetEmpty);
  MultiWorldExecutionConfig missing_equality = ExecutionConfig();
  missing_equality.maximum_near_feasible_missing_nets = 1;
  MultiWorldExecution at_missing = Executed(ExecuteMultiWorldCpu(
      kMultiWorldExecutionSchemaVersion, incomplete.source, incomplete.branch_state,
      std::span<const MultiWorldSchedule>(&one_round, 1), *incomplete.store, missing_equality));
  EXPECT_EQ(SummaryFor(at_missing, 1).terminal_reason,
            MultiWorldTerminalReason::kNoCandidateWithoutRegeneration);
  EXPECT_TRUE(SummaryFor(at_missing, 1).pareto_eligible);
  missing_equality.maximum_near_feasible_missing_nets = 0;
  MultiWorldExecution over_missing = Executed(ExecuteMultiWorldCpu(
      kMultiWorldExecutionSchemaVersion, incomplete.source, incomplete.branch_state,
      std::span<const MultiWorldSchedule>(&one_round, 1), *incomplete.store, missing_equality));
  EXPECT_FALSE(SummaryFor(over_missing, 1).pareto_eligible);

  MultiWorldFixture all_empty = BuildFixture(FixturePools::kAllEmpty);
  MultiWorldExecution empty = Executed(ExecuteMultiWorldCpu(
      kMultiWorldExecutionSchemaVersion, all_empty.source, all_empty.branch_state,
      std::span<const MultiWorldSchedule>(&one_round, 1), *all_empty.store, ExecutionConfig()));
  EXPECT_EQ(SummaryFor(empty, 1).terminal_reason,
            MultiWorldTerminalReason::kNoCandidateWithoutRegeneration);
  EXPECT_EQ(empty.counters().retained_winner_pins, 0U);
  EXPECT_TRUE(empty.has_active_retention_lease());
  EXPECT_TRUE(empty.retention_lease_belongs_to(*all_empty.store));
}

TEST(MultiWorldTest, RejectsDifferentAuthenticWorkloadBeforeEveryTerminalShortcut) {
  MultiWorldFixture fixture = BuildFixture();
  const NegotiatedPriceState different_workload_state = DifferentWorkloadBranchState(fixture);
  ASSERT_EQ(different_workload_state.associations(), fixture.branch_state.associations());
  ASSERT_NE(different_workload_state.workload_checksum(), fixture.branch_state.workload_checksum());
  const MultiWorldSchedule one_round{
      .schedule_key = 1, .search_intrinsic_cost_weight = 1, .maximum_selection_rounds = 1};

  const auto expect_workload_rejection = [&](const MultiWorldPoolSnapshot& source,
                                             CandidateStore& store,
                                             const MultiWorldExecutionConfig& config) {
    MultiWorldExecutionResult result =
        ExecuteMultiWorldCpu(kMultiWorldExecutionSchemaVersion, source, different_workload_state,
                             std::span<const MultiWorldSchedule>(&one_round, 1), store, config);
    ASSERT_TRUE(std::holds_alternative<MultiWorldExecutionError>(result));
    const MultiWorldExecutionError& error = std::get<MultiWorldExecutionError>(result);
    EXPECT_EQ(error.code, MultiWorldExecutionErrorCode::kInvalidBranchState);
    EXPECT_EQ(error.invariant_id, "allocator.multi_world.branch_workload.v1");
  };

  expect_workload_rejection(fixture.source, *fixture.store, ExecutionConfig());
  MultiWorldFixture missing_candidate = BuildFixture(FixturePools::kSecondNetEmpty);
  expect_workload_rejection(missing_candidate.source, *missing_candidate.store, ExecutionConfig());
  MultiWorldExecutionConfig refinement = ExecutionConfig();
  refinement.known_unmapped_exact_conflict_count = 1;
  expect_workload_rejection(fixture.source, *fixture.store, refinement);
}

TEST(MultiWorldTest, RejectsDuplicateSchedulesSemanticDriftAndIterationOverflowMutationFree) {
  MultiWorldFixture fixture = BuildFixture();
  const std::vector<candidates::CandidateRejection> rejections_before = fixture.store->Rejections();
  const candidates::CandidateStoreTelemetry telemetry_before = fixture.store->telemetry();
  const MultiWorldSchedule schedule{
      .schedule_key = 3, .search_intrinsic_cost_weight = 1, .maximum_selection_rounds = 1};
  const std::array duplicate_key = {
      schedule,
      MultiWorldSchedule{
          .schedule_key = 3, .search_intrinsic_cost_weight = 2, .maximum_selection_rounds = 1},
  };
  MultiWorldExecutionResult duplicate =
      ExecuteMultiWorldCpu(kMultiWorldExecutionSchemaVersion, fixture.source, fixture.branch_state,
                           duplicate_key, *fixture.store, ExecutionConfig());
  ASSERT_TRUE(std::holds_alternative<MultiWorldExecutionError>(duplicate));
  EXPECT_EQ(std::get<MultiWorldExecutionError>(duplicate).code,
            MultiWorldExecutionErrorCode::kDuplicateSchedule);
  const std::array exact_duplicate = {schedule, schedule};
  MultiWorldExecutionResult exact =
      ExecuteMultiWorldCpu(kMultiWorldExecutionSchemaVersion, fixture.source, fixture.branch_state,
                           exact_duplicate, *fixture.store, ExecutionConfig());
  ASSERT_TRUE(std::holds_alternative<MultiWorldExecutionError>(exact));
  EXPECT_EQ(std::get<MultiWorldExecutionError>(exact).code,
            MultiWorldExecutionErrorCode::kDuplicateSchedule);
  const std::array duplicate_exact_search = {
      schedule,
      MultiWorldSchedule{.schedule_key = 4,
                         .search_intrinsic_cost_weight = schedule.search_intrinsic_cost_weight,
                         .maximum_selection_rounds = schedule.maximum_selection_rounds},
  };
  MultiWorldExecutionResult exact_search =
      ExecuteMultiWorldCpu(kMultiWorldExecutionSchemaVersion, fixture.source, fixture.branch_state,
                           duplicate_exact_search, *fixture.store, ExecutionConfig());
  ASSERT_TRUE(std::holds_alternative<MultiWorldExecutionError>(exact_search));
  EXPECT_EQ(std::get<MultiWorldExecutionError>(exact_search).code,
            MultiWorldExecutionErrorCode::kDuplicateSchedule);

  MultiWorldPoolSnapshot wrong_candidate = fixture.source;
  wrong_candidate.pools.front().candidates.front() =
      wrong_candidate.pools.back().candidates.front();
  MultiWorldExecutionResult semantic = ExecuteMultiWorldCpu(
      kMultiWorldExecutionSchemaVersion, wrong_candidate, fixture.branch_state,
      std::span<const MultiWorldSchedule>(&schedule, 1), *fixture.store, ExecutionConfig());
  ASSERT_TRUE(std::holds_alternative<MultiWorldExecutionError>(semantic));
  EXPECT_EQ(std::get<MultiWorldExecutionError>(semantic).code,
            MultiWorldExecutionErrorCode::kSourcePoolInvalid);
  EXPECT_EQ(std::get<MultiWorldExecutionError>(semantic).invariant_id,
            "allocator.multi_world.source_candidate_association.v1");

  MultiWorldPoolSnapshot no_pools = fixture.source;
  no_pools.pools.clear();
  MultiWorldExecutionResult no_pool_result = ExecuteMultiWorldCpu(
      kMultiWorldExecutionSchemaVersion, no_pools, fixture.branch_state,
      std::span<const MultiWorldSchedule>(&schedule, 1), *fixture.store, ExecutionConfig());
  ASSERT_TRUE(std::holds_alternative<MultiWorldExecutionError>(no_pool_result));
  EXPECT_EQ(std::get<MultiWorldExecutionError>(no_pool_result).code,
            MultiWorldExecutionErrorCode::kSourcePoolInvalid);
  EXPECT_EQ(std::get<MultiWorldExecutionError>(no_pool_result).invariant_id,
            "allocator.multi_world.source_pool_roster.v1");

  MultiWorldPoolSnapshot omitted = fixture.source;
  omitted.pools.front().candidates.pop_back();
  MultiWorldExecutionResult drift = ExecuteMultiWorldCpu(
      kMultiWorldExecutionSchemaVersion, omitted, fixture.branch_state,
      std::span<const MultiWorldSchedule>(&schedule, 1), *fixture.store, ExecutionConfig());
  ASSERT_TRUE(std::holds_alternative<MultiWorldExecutionError>(drift));
  EXPECT_EQ(std::get<MultiWorldExecutionError>(drift).code,
            MultiWorldExecutionErrorCode::kCandidateStoreLease);

  CandidateStore wrong_store(StoreConfig());
  MultiWorldExecutionResult wrong_store_result = ExecuteMultiWorldCpu(
      kMultiWorldExecutionSchemaVersion, fixture.source, fixture.branch_state,
      std::span<const MultiWorldSchedule>(&schedule, 1), wrong_store, ExecutionConfig());
  ASSERT_TRUE(std::holds_alternative<MultiWorldExecutionError>(wrong_store_result));
  EXPECT_EQ(std::get<MultiWorldExecutionError>(wrong_store_result).code,
            MultiWorldExecutionErrorCode::kCandidateStoreLease);

  candidates::CandidateStoreConfig pin_bounded_config = StoreConfig();
  pin_bounded_config.maximum_pin_lease_items_per_transaction = 3;
  MultiWorldFixture pin_bounded = BuildFixture(FixturePools::kAlternatives, 4, pin_bounded_config);
  const std::vector<candidates::CandidateRejection> pin_rejections =
      pin_bounded.store->Rejections();
  const candidates::CandidateStoreTelemetry pin_telemetry = pin_bounded.store->telemetry();
  MultiWorldExecutionResult pin_bound = ExecuteMultiWorldCpu(
      kMultiWorldExecutionSchemaVersion, pin_bounded.source, pin_bounded.branch_state,
      std::span<const MultiWorldSchedule>(&schedule, 1), *pin_bounded.store, ExecutionConfig());
  ASSERT_TRUE(std::holds_alternative<MultiWorldExecutionError>(pin_bound));
  EXPECT_EQ(std::get<MultiWorldExecutionError>(pin_bound).code,
            MultiWorldExecutionErrorCode::kWorkBoundExceeded);
  EXPECT_EQ(internal::MultiWorldPreflightSpanInspectionsForTesting(), 0U);

  MultiWorldPoolSnapshot over_cap_with_sentinel = pin_bounded.source;
  over_cap_with_sentinel.pools.back().candidates.push_back(nullptr);
  MultiWorldExecutionResult sentinel = ExecuteMultiWorldCpu(
      kMultiWorldExecutionSchemaVersion, over_cap_with_sentinel, pin_bounded.branch_state,
      std::span<const MultiWorldSchedule>(&schedule, 1), *pin_bounded.store, ExecutionConfig());
  ASSERT_TRUE(std::holds_alternative<MultiWorldExecutionError>(sentinel));
  EXPECT_EQ(std::get<MultiWorldExecutionError>(sentinel).code,
            MultiWorldExecutionErrorCode::kWorkBoundExceeded);
  EXPECT_EQ(std::get<MultiWorldExecutionError>(sentinel).invariant_id,
            "allocator.multi_world.candidate_store_preflight.v1");
  EXPECT_EQ(internal::MultiWorldPreflightSpanInspectionsForTesting(), 0U);
  EXPECT_EQ(pin_bounded.store->Rejections(), pin_rejections);
  EXPECT_EQ(pin_bounded.store->telemetry(), pin_telemetry);

  MultiWorldFixture remaining = BuildFixture(FixturePools::kAlternatives, 1);
  const MultiWorldSchedule equality{
      .schedule_key = 5, .search_intrinsic_cost_weight = 1, .maximum_selection_rounds = 2};
  EXPECT_TRUE(std::holds_alternative<MultiWorldExecution>(ExecuteMultiWorldCpu(
      kMultiWorldExecutionSchemaVersion, remaining.source, remaining.branch_state,
      std::span<const MultiWorldSchedule>(&equality, 1), *remaining.store, ExecutionConfig())));
  MultiWorldSchedule one_over = equality;
  one_over.schedule_key = 7;
  ++one_over.maximum_selection_rounds;
  MultiWorldExecutionResult iteration_over = ExecuteMultiWorldCpu(
      kMultiWorldExecutionSchemaVersion, remaining.source, remaining.branch_state,
      std::span<const MultiWorldSchedule>(&one_over, 1), *remaining.store, ExecutionConfig());
  ASSERT_TRUE(std::holds_alternative<MultiWorldExecutionError>(iteration_over));
  EXPECT_EQ(std::get<MultiWorldExecutionError>(iteration_over).code,
            MultiWorldExecutionErrorCode::kInvalidSchedule);

  EXPECT_EQ(fixture.store->Rejections(), rejections_before);
  EXPECT_EQ(fixture.store->telemetry(), telemetry_before);
}

TEST(MultiWorldTest, RejectsSourceCandidateFromDifferentAuthenticEndpointLayerMutationFree) {
  MultiWorldFixture fixture = BuildFixture();
  const std::array specs = {
      MultiNetRoutingSpec{.routing_profile = fixture.board.data().routing_profile,
                          .start_layer = 31,
                          .goal_layer = 31},
      MultiNetRoutingSpec{.routing_profile = fixture.workload->nets()[1].routing_profile,
                          .start_layer = 31,
                          .goal_layer = 31},
  };
  fixture.workload = std::make_unique<MultiNetWorkload>(Built<MultiNetWorkload>(
      BuildMultiNetWorkload(kMultiNetWorkloadSchemaVersion, fixture.board,
                            fixture.workload->compiler_profile(), specs, specs.size())));
  fixture.source.workload = fixture.workload.get();
  fixture.branch_state = Built<NegotiatedPriceState>(
      BuildInitialNegotiatedPriceState(kNegotiatedPriceStateSchemaVersion, fixture.capacities,
                                       *fixture.workload, fixture.branch_state.config()));
  const std::vector<candidates::CandidateRejection> rejections_before = fixture.store->Rejections();
  const candidates::CandidateStoreTelemetry telemetry_before = fixture.store->telemetry();
  for (const StoredCandidate& candidate : fixture.all_candidates) {
    ASSERT_FALSE(fixture.store->IsPinned(candidate->id()));
  }
  const MultiWorldSchedule schedule{
      .schedule_key = 1, .search_intrinsic_cost_weight = 1, .maximum_selection_rounds = 1};

  MultiWorldExecutionResult result = ExecuteMultiWorldCpu(
      kMultiWorldExecutionSchemaVersion, fixture.source, fixture.branch_state,
      std::span<const MultiWorldSchedule>(&schedule, 1), *fixture.store, ExecutionConfig());
  ASSERT_TRUE(std::holds_alternative<MultiWorldExecutionError>(result));
  const MultiWorldExecutionError& error = std::get<MultiWorldExecutionError>(result);
  EXPECT_EQ(error.code, MultiWorldExecutionErrorCode::kSourcePoolInvalid);
  EXPECT_EQ(error.invariant_id, "allocator.multi_world.source_candidate_association.v1");
  EXPECT_EQ(fixture.store->Rejections(), rejections_before);
  EXPECT_EQ(fixture.store->telemetry(), telemetry_before);
  for (const StoredCandidate& candidate : fixture.all_candidates) {
    EXPECT_FALSE(fixture.store->IsPinned(candidate->id()));
  }
}

TEST(MultiWorldTest, EnforcesKnownAndRetainedCapsAtEqualityAndOneOver) {
  const internal::MultiWorldKnownWorkV1 work{
      .world_count = 2,
      .total_selection_rounds = 3,
      .total_price_updates = 1,
      .source_net_count = 5,
      .source_candidate_count = 7,
      .source_candidate_span_count = 11,
      .source_resource_work_units_per_candidate_pass = 13,
  };
  const std::uint64_t charged_candidate_passes =
      1U + work.total_selection_rounds + work.total_price_updates;
  MultiWorldExecutionConfig exact = ExecutionConfig();
  exact.maximum_worlds = work.world_count;
  exact.maximum_total_selection_rounds = work.total_selection_rounds;
  exact.maximum_total_candidate_evaluations =
      charged_candidate_passes * work.source_candidate_count;
  exact.maximum_total_candidate_span_visits =
      charged_candidate_passes * work.source_candidate_span_count;
  exact.maximum_total_resource_work_units =
      charged_candidate_passes * work.source_resource_work_units_per_candidate_pass;
  exact.maximum_total_net_outcomes = charged_candidate_passes * work.source_net_count;
  exact.maximum_pareto_comparisons = 1;
  internal::MultiWorldKnownWorkProjectionV1 projection;
  EXPECT_TRUE(internal::MultiWorldKnownWorkFitsV1(work, exact, &projection));
  EXPECT_EQ(projection.charged_candidate_passes, charged_candidate_passes);
  EXPECT_EQ(projection.candidate_evaluations, exact.maximum_total_candidate_evaluations);
  EXPECT_EQ(projection.candidate_span_visits, exact.maximum_total_candidate_span_visits);
  EXPECT_EQ(projection.resource_work_units, exact.maximum_total_resource_work_units);
  EXPECT_EQ(projection.net_outcomes, exact.maximum_total_net_outcomes);
  EXPECT_EQ(projection.pareto_comparisons, exact.maximum_pareto_comparisons);

  internal::MultiWorldKnownWorkV1 impossible_zero_world = work;
  impossible_zero_world.world_count = 0;
  impossible_zero_world.total_selection_rounds = 5;
  impossible_zero_world.total_price_updates = 5;
  EXPECT_FALSE(
      internal::MultiWorldKnownWorkFitsV1(impossible_zero_world, ExecutionConfig(), nullptr));
  impossible_zero_world.total_selection_rounds = 0;
  impossible_zero_world.total_price_updates = 1;
  EXPECT_FALSE(
      internal::MultiWorldKnownWorkFitsV1(impossible_zero_world, ExecutionConfig(), nullptr));

  const auto expect_one_over = [&](auto member) {
    MultiWorldExecutionConfig bounded = exact;
    --(bounded.*member);
    internal::MultiWorldKnownWorkProjectionV1 ignored;
    EXPECT_FALSE(internal::MultiWorldKnownWorkFitsV1(work, bounded, &ignored));
  };
  expect_one_over(&MultiWorldExecutionConfig::maximum_worlds);
  expect_one_over(&MultiWorldExecutionConfig::maximum_total_selection_rounds);
  expect_one_over(&MultiWorldExecutionConfig::maximum_total_candidate_evaluations);
  expect_one_over(&MultiWorldExecutionConfig::maximum_total_candidate_span_visits);
  expect_one_over(&MultiWorldExecutionConfig::maximum_total_resource_work_units);
  expect_one_over(&MultiWorldExecutionConfig::maximum_total_net_outcomes);
  expect_one_over(&MultiWorldExecutionConfig::maximum_pareto_comparisons);

  internal::MultiWorldKnownWorkV1 malformed_updates = work;
  --malformed_updates.total_price_updates;
  EXPECT_FALSE(
      internal::MultiWorldKnownWorkFitsV1(malformed_updates, ExecutionConfig(), &projection));
  malformed_updates.total_price_updates = work.total_price_updates + 1U;
  EXPECT_FALSE(
      internal::MultiWorldKnownWorkFitsV1(malformed_updates, ExecutionConfig(), &projection));

  const internal::MultiWorldKnownWorkV1 overflow{
      .world_count = 1,
      .total_selection_rounds = std::numeric_limits<std::uint64_t>::max(),
      .total_price_updates = std::numeric_limits<std::uint64_t>::max() - 1U,
      .source_net_count = 2,
      .source_candidate_count = 2,
      .source_candidate_span_count = 2,
      .source_resource_work_units_per_candidate_pass = 2,
  };
  EXPECT_FALSE(internal::MultiWorldKnownWorkFitsV1(overflow, ExecutionConfig(), &projection));

  MultiWorldFixture fixture = BuildFixture();
  const MultiWorldSchedule resource_probe_schedule{
      .schedule_key = 97, .search_intrinsic_cost_weight = 1, .maximum_selection_rounds = 1};
  std::uint64_t total_source_spans = 0;
  std::uint64_t total_source_expanded_uses = 0;
  for (const CandidatePool& pool : fixture.source.pools) {
    for (const StoredCandidate& candidate : pool.candidates) {
      total_source_spans += candidate->data().resources.size();
      for (const candidates::PhysicalEdgeSpan& span : candidate->data().resources) {
        total_source_expanded_uses += span.edge_count;
      }
    }
  }
  ASSERT_GT(total_source_spans, 1U);
  ASSERT_GT(total_source_expanded_uses, 1U);

  MultiWorldExecutionConfig span_count_probe = ExecutionConfig();
  span_count_probe.maximum_total_candidate_span_visits = 1;
  MultiWorldExecutionResult span_count_result =
      ExecuteMultiWorldCpu(kMultiWorldExecutionSchemaVersion, fixture.source, fixture.branch_state,
                           std::span<const MultiWorldSchedule>(&resource_probe_schedule, 1),
                           *fixture.store, span_count_probe);
  ASSERT_TRUE(std::holds_alternative<MultiWorldExecutionError>(span_count_result));
  EXPECT_EQ(std::get<MultiWorldExecutionError>(span_count_result).invariant_id,
            "allocator.multi_world.known_work_budget.v1");
  EXPECT_EQ(internal::MultiWorldPreflightSpanInspectionsForTesting(), 0U);
  span_count_probe.maximum_total_candidate_span_visits = 2U * total_source_spans;
  {
    MultiWorldExecutionResult exact_span_count = ExecuteMultiWorldCpu(
        kMultiWorldExecutionSchemaVersion, fixture.source, fixture.branch_state,
        std::span<const MultiWorldSchedule>(&resource_probe_schedule, 1), *fixture.store,
        span_count_probe);
    ASSERT_TRUE(std::holds_alternative<MultiWorldExecution>(exact_span_count));
  }

  MultiWorldPoolSnapshot exact_expanded_source = fixture.source;
  exact_expanded_source.allocator_limits.maximum_expanded_resource_uses =
      total_source_expanded_uses;
  {
    MultiWorldExecutionResult exact_expanded = ExecuteMultiWorldCpu(
        kMultiWorldExecutionSchemaVersion, exact_expanded_source, fixture.branch_state,
        std::span<const MultiWorldSchedule>(&resource_probe_schedule, 1), *fixture.store,
        ExecutionConfig());
    ASSERT_TRUE(std::holds_alternative<MultiWorldExecution>(exact_expanded));
  }
  ASSERT_FALSE(fixture.source.pools.front().candidates.front()->data().resources.empty());
  const std::uint32_t first_span_edge_count =
      fixture.source.pools.front().candidates.front()->data().resources.front().edge_count;
  ASSERT_GT(first_span_edge_count, 1U);
  MultiWorldPoolSnapshot early_stop_expanded_source = exact_expanded_source;
  early_stop_expanded_source.allocator_limits.maximum_expanded_resource_uses =
      first_span_edge_count - 1U;
  MultiWorldExecutionResult early_stop_expanded = ExecuteMultiWorldCpu(
      kMultiWorldExecutionSchemaVersion, early_stop_expanded_source, fixture.branch_state,
      std::span<const MultiWorldSchedule>(&resource_probe_schedule, 1), *fixture.store,
      ExecutionConfig());
  ASSERT_TRUE(std::holds_alternative<MultiWorldExecutionError>(early_stop_expanded));
  EXPECT_EQ(std::get<MultiWorldExecutionError>(early_stop_expanded).invariant_id,
            "allocator.input.expanded_resource_budget.v1");
  EXPECT_EQ(internal::MultiWorldPreflightSpanInspectionsForTesting(), 1U);

  MultiWorldFixture resource_record_fixture = BuildFixture();
  const std::vector<routing::EdgeResourceKey> override_resources =
      FirstDistinctResources(resource_record_fixture, 2);
  ASSERT_EQ(override_resources.size(), 2U);
  const std::vector<ResourceCapacityOverride> overrides = {
      ResourceCapacityOverride{.resource = override_resources[0], .capacity_units = 1},
      ResourceCapacityOverride{.resource = override_resources[1], .capacity_units = 1},
  };
  resource_record_fixture.capacities = Built<ResourceCapacityModel>(BuildResourceCapacityModel(
      kResourceCapacityModelSchemaVersion, resource_record_fixture.board,
      resource_record_fixture.workload->nets().front().compiled_board, 1, overrides));
  resource_record_fixture.source.capacities = resource_record_fixture.capacities;
  resource_record_fixture.branch_state =
      Built<NegotiatedPriceState>(BuildInitialNegotiatedPriceState(
          kNegotiatedPriceStateSchemaVersion, resource_record_fixture.capacities,
          *resource_record_fixture.workload, resource_record_fixture.branch_state.config()));
  const MultiWorldSchedule price_record_schedule{
      .schedule_key = 101, .search_intrinsic_cost_weight = 1, .maximum_selection_rounds = 2};
  const ManualWorldRun priced_branch =
      RunManualWorld(resource_record_fixture, price_record_schedule);
  ASSERT_FALSE(priced_branch.price_state.prices().empty());
  resource_record_fixture.branch_state = priced_branch.price_state;
  resource_record_fixture.source.allocator_limits.maximum_resource_records =
      overrides.size() + resource_record_fixture.branch_state.prices().size();
  {
    MultiWorldExecutionResult exact_resource_records =
        ExecuteMultiWorldCpu(kMultiWorldExecutionSchemaVersion, resource_record_fixture.source,
                             resource_record_fixture.branch_state,
                             std::span<const MultiWorldSchedule>(&resource_probe_schedule, 1),
                             *resource_record_fixture.store, ExecutionConfig());
    ASSERT_TRUE(std::holds_alternative<MultiWorldExecution>(exact_resource_records));
  }
  --resource_record_fixture.source.allocator_limits.maximum_resource_records;
  const std::vector<candidates::CandidateRejection> record_rejections =
      resource_record_fixture.store->Rejections();
  const candidates::CandidateStoreTelemetry record_telemetry =
      resource_record_fixture.store->telemetry();
  MultiWorldExecutionResult one_under_resource_records =
      ExecuteMultiWorldCpu(kMultiWorldExecutionSchemaVersion, resource_record_fixture.source,
                           resource_record_fixture.branch_state,
                           std::span<const MultiWorldSchedule>(&resource_probe_schedule, 1),
                           *resource_record_fixture.store, ExecutionConfig());
  ASSERT_TRUE(std::holds_alternative<MultiWorldExecutionError>(one_under_resource_records));
  EXPECT_EQ(std::get<MultiWorldExecutionError>(one_under_resource_records).invariant_id,
            "allocator.input.resource_record_budget.v1");
  EXPECT_EQ(internal::MultiWorldPreflightSpanInspectionsForTesting(), 0U);
  EXPECT_EQ(resource_record_fixture.store->Rejections(), record_rejections);
  EXPECT_EQ(resource_record_fixture.store->telemetry(), record_telemetry);
  for (const StoredCandidate& candidate : resource_record_fixture.all_candidates) {
    EXPECT_FALSE(resource_record_fixture.store->IsPinned(candidate->id()));
  }

  MultiWorldExecutionConfig resource_probe = ExecutionConfig();
  resource_probe.maximum_total_resource_work_units =
      2U * fixture.source.allocator_limits.maximum_resource_records;
  MultiWorldExecutionResult resource_probe_result =
      ExecuteMultiWorldCpu(kMultiWorldExecutionSchemaVersion, fixture.source, fixture.branch_state,
                           std::span<const MultiWorldSchedule>(&resource_probe_schedule, 1),
                           *fixture.store, resource_probe);
  ASSERT_TRUE(std::holds_alternative<MultiWorldExecutionError>(resource_probe_result));
  EXPECT_EQ(std::get<MultiWorldExecutionError>(resource_probe_result).code,
            MultiWorldExecutionErrorCode::kWorkBoundExceeded);
  EXPECT_EQ(std::get<MultiWorldExecutionError>(resource_probe_result).invariant_id,
            "allocator.multi_world.known_work_budget.v1");
  EXPECT_EQ(internal::MultiWorldPreflightSpanInspectionsForTesting(), 1U);

  const std::array schedules = {
      MultiWorldSchedule{
          .schedule_key = 1, .search_intrinsic_cost_weight = 1, .maximum_selection_rounds = 1},
      MultiWorldSchedule{
          .schedule_key = 2, .search_intrinsic_cost_weight = 1, .maximum_selection_rounds = 2},
  };
  MultiWorldExecution baseline = Executed(
      ExecuteMultiWorldCpu(kMultiWorldExecutionSchemaVersion, fixture.source, fixture.branch_state,
                           schedules, *fixture.store, ExecutionConfig()));
  ASSERT_GT(baseline.retained_worlds().size(), 1U);
  std::uint64_t buffered_terminal_selections = 0;
  std::uint64_t buffered_terminal_resources = 0;
  std::uint64_t buffered_terminal_prices = 0;
  for (const MultiWorldSchedule& schedule : schedules) {
    const ManualWorldRun manual = RunManualWorld(fixture, schedule);
    buffered_terminal_selections += manual.world.selections.size();
    buffered_terminal_resources += manual.world.resources.size();
    buffered_terminal_prices += manual.price_state.prices().size();
  }
  MultiWorldExecutionConfig exact_buffering = ExecutionConfig();
  exact_buffering.maximum_buffered_terminal_selection_records = buffered_terminal_selections;
  exact_buffering.maximum_buffered_terminal_resource_records = buffered_terminal_resources;
  exact_buffering.maximum_buffered_terminal_price_records = buffered_terminal_prices;
  EXPECT_TRUE(std::holds_alternative<MultiWorldExecution>(
      ExecuteMultiWorldCpu(kMultiWorldExecutionSchemaVersion, fixture.source, fixture.branch_state,
                           schedules, *fixture.store, exact_buffering)));

  const auto expect_buffering_one_over = [&](auto member) {
    MultiWorldExecutionConfig bounded = exact_buffering;
    ASSERT_GT(bounded.*member, 1U);
    --(bounded.*member);
    MultiWorldExecutionResult result =
        ExecuteMultiWorldCpu(kMultiWorldExecutionSchemaVersion, fixture.source,
                             fixture.branch_state, schedules, *fixture.store, bounded);
    ASSERT_TRUE(std::holds_alternative<MultiWorldExecutionError>(result));
    EXPECT_EQ(std::get<MultiWorldExecutionError>(result).code,
              MultiWorldExecutionErrorCode::kWorkBoundExceeded);
  };
  expect_buffering_one_over(
      &MultiWorldExecutionConfig::maximum_buffered_terminal_selection_records);
  expect_buffering_one_over(&MultiWorldExecutionConfig::maximum_buffered_terminal_resource_records);
  expect_buffering_one_over(&MultiWorldExecutionConfig::maximum_buffered_terminal_price_records);

  std::uint64_t retained_selections = 0;
  std::uint64_t retained_resources = 0;
  std::uint64_t retained_prices = 0;
  for (const RetainedMultiWorld& world : baseline.retained_worlds()) {
    retained_selections += world.world.selections.size();
    retained_resources += world.world.resources.size();
    retained_prices += world.price_state.prices().size();
  }
  MultiWorldExecutionConfig exact_retention = ExecutionConfig();
  exact_retention.maximum_retained_worlds = baseline.retained_worlds().size();
  exact_retention.maximum_retained_selection_records = retained_selections;
  exact_retention.maximum_retained_resource_records = retained_resources;
  exact_retention.maximum_retained_price_records = retained_prices;
  exact_retention.maximum_retained_winner_pins = baseline.counters().retained_winner_pins;
  EXPECT_TRUE(std::holds_alternative<MultiWorldExecution>(
      ExecuteMultiWorldCpu(kMultiWorldExecutionSchemaVersion, fixture.source, fixture.branch_state,
                           schedules, *fixture.store, exact_retention)));

  const auto expect_retention_one_over = [&](auto member) {
    MultiWorldExecutionConfig bounded = exact_retention;
    ASSERT_GT(bounded.*member, 1U);
    --(bounded.*member);
    MultiWorldExecutionResult result =
        ExecuteMultiWorldCpu(kMultiWorldExecutionSchemaVersion, fixture.source,
                             fixture.branch_state, schedules, *fixture.store, bounded);
    ASSERT_TRUE(std::holds_alternative<MultiWorldExecutionError>(result));
    EXPECT_EQ(std::get<MultiWorldExecutionError>(result).code,
              MultiWorldExecutionErrorCode::kWorkBoundExceeded);
  };
  expect_retention_one_over(&MultiWorldExecutionConfig::maximum_retained_worlds);
  expect_retention_one_over(&MultiWorldExecutionConfig::maximum_retained_selection_records);
  expect_retention_one_over(&MultiWorldExecutionConfig::maximum_retained_resource_records);
  expect_retention_one_over(&MultiWorldExecutionConfig::maximum_retained_price_records);
  expect_retention_one_over(&MultiWorldExecutionConfig::maximum_retained_winner_pins);
}

TEST(MultiWorldTest, RetentionPinsOnlyFinalWinnersAndRefinementShortCircuits) {
  MultiWorldFixture fixture = BuildFixture();
  const MultiWorldSchedule schedule{
      .schedule_key = 1, .search_intrinsic_cost_weight = 1, .maximum_selection_rounds = 2};
  for (const StoredCandidate& candidate : fixture.all_candidates) {
    EXPECT_FALSE(fixture.store->IsPinned(candidate->id()));
  }
  {
    MultiWorldExecutionConfig config = ExecutionConfig();
    config.maximum_retained_worlds = 1;
    MultiWorldExecution execution = Executed(ExecuteMultiWorldCpu(
        kMultiWorldExecutionSchemaVersion, fixture.source, fixture.branch_state,
        std::span<const MultiWorldSchedule>(&schedule, 1), *fixture.store, config));
    ASSERT_EQ(execution.retained_worlds().size(), 1U);
    std::set<CandidateId> winners;
    for (const NetSelection& selection : execution.retained_worlds().front().world.selections) {
      if (selection.candidate_id.has_value()) {
        winners.insert(*selection.candidate_id);
      }
    }
    EXPECT_EQ(execution.counters().retained_winner_pins, winners.size());
    for (const StoredCandidate& candidate : fixture.all_candidates) {
      EXPECT_EQ(fixture.store->IsPinned(candidate->id()), winners.contains(candidate->id()));
    }
  }
  for (const StoredCandidate& candidate : fixture.all_candidates) {
    EXPECT_FALSE(fixture.store->IsPinned(candidate->id()));
  }

  const std::vector<candidates::CandidateRejection> rejections_before = fixture.store->Rejections();
  const candidates::CandidateStoreTelemetry telemetry_before = fixture.store->telemetry();
  std::uint64_t source_candidate_count = 0;
  std::uint64_t source_span_count = 0;
  for (const CandidatePool& pool : fixture.source.pools) {
    source_candidate_count += pool.candidates.size();
    for (const StoredCandidate& candidate : pool.candidates) {
      source_span_count += candidate->data().resources.size();
    }
  }
  MultiWorldExecutionConfig refinement = ExecutionConfig();
  refinement.maximum_total_selection_rounds = 1;
  refinement.maximum_total_candidate_evaluations = source_candidate_count;
  refinement.maximum_total_candidate_span_visits = source_span_count;
  refinement.maximum_total_resource_work_units = ResourceWorkUnitsPerCandidatePass(fixture);
  refinement.maximum_total_net_outcomes = fixture.source.pools.size();
  refinement.maximum_pareto_comparisons = 1;
  refinement.known_unmapped_exact_conflict_count = 1;
  MultiWorldExecution short_circuit = Executed(ExecuteMultiWorldCpu(
      kMultiWorldExecutionSchemaVersion, fixture.source, fixture.branch_state,
      std::span<const MultiWorldSchedule>(&schedule, 1), *fixture.store, refinement));
  EXPECT_EQ(short_circuit.disposition(),
            MultiWorldExecutionDisposition::kResourceRefinementRequired);
  EXPECT_TRUE(short_circuit.summaries().empty());
  EXPECT_TRUE(short_circuit.retained_worlds().empty());
  const MultiWorldExecutionCounters expected_refinement_counters{
      .scheduled_worlds = 1,
      .candidate_evaluations = source_candidate_count,
      .candidate_span_visits = source_span_count,
      .resource_work_units = ResourceWorkUnitsPerCandidatePass(fixture),
      .net_outcomes = fixture.source.pools.size(),
  };
  EXPECT_EQ(short_circuit.counters(), expected_refinement_counters);

  const auto expect_authentication_one_over = [&](auto member) {
    MultiWorldExecutionConfig bounded = refinement;
    ASSERT_GT(bounded.*member, 1U);
    --(bounded.*member);
    MultiWorldExecutionResult result = ExecuteMultiWorldCpu(
        kMultiWorldExecutionSchemaVersion, fixture.source, fixture.branch_state,
        std::span<const MultiWorldSchedule>(&schedule, 1), *fixture.store, bounded);
    ASSERT_TRUE(std::holds_alternative<MultiWorldExecutionError>(result));
    EXPECT_EQ(std::get<MultiWorldExecutionError>(result).code,
              MultiWorldExecutionErrorCode::kWorkBoundExceeded);
  };
  expect_authentication_one_over(&MultiWorldExecutionConfig::maximum_total_candidate_evaluations);
  expect_authentication_one_over(&MultiWorldExecutionConfig::maximum_total_candidate_span_visits);
  expect_authentication_one_over(&MultiWorldExecutionConfig::maximum_total_resource_work_units);
  expect_authentication_one_over(&MultiWorldExecutionConfig::maximum_total_net_outcomes);
  EXPECT_EQ(fixture.store->Rejections(), rejections_before);
  EXPECT_EQ(fixture.store->telemetry(), telemetry_before);
}

TEST(MultiWorldTest, IdentityAndPoolSnapshotChecksumsAreGoldenAndFieldSensitive) {
  MultiWorldSchedule schedule{
      .schedule_key = 5, .search_intrinsic_cost_weight = 7, .maximum_selection_rounds = 11};
  const std::uint64_t identity = internal::ComputeMultiWorldIdentityV1(2, 3, schedule);
  EXPECT_EQ(identity, 4575393395812064757ULL);
  EXPECT_NE(internal::ComputeMultiWorldIdentityV1(3, 3, schedule), identity);
  EXPECT_NE(internal::ComputeMultiWorldIdentityV1(2, 4, schedule), identity);
  ++schedule.schedule_key;
  EXPECT_NE(internal::ComputeMultiWorldIdentityV1(2, 3, schedule), identity);
  --schedule.schedule_key;
  ++schedule.search_intrinsic_cost_weight;
  EXPECT_NE(internal::ComputeMultiWorldIdentityV1(2, 3, schedule), identity);
  --schedule.search_intrinsic_cost_weight;
  ++schedule.maximum_selection_rounds;
  EXPECT_NE(internal::ComputeMultiWorldIdentityV1(2, 3, schedule), identity);

  const internal::MultiWorldPoolSnapshotChecksumHeaderV1 header{
      .schema_version = 2,
      .associations =
          AllocationAssociations{
              .board_content_hash = 3,
              .compiler_profile_fingerprint = 5,
              .geometry_compiler_version = 7,
          },
      .workload_checksum = 11,
      .capacity_model_checksum = 13,
      .allocator_limits =
          OneWorldAllocatorLimits{
              .maximum_nets = 17,
              .maximum_candidates = 19,
              .maximum_resource_records = 23,
              .maximum_expanded_resource_uses = 29,
          },
      .candidate_pool_manifest_checksum = 31,
      .source_pool_count = 37,
      .source_candidate_count = 41,
  };
  const std::uint64_t pool_snapshot = internal::ComputeMultiWorldPoolSnapshotChecksumV1(header);
  EXPECT_EQ(pool_snapshot, 8008195133944007559ULL);
  const auto expect_pool_field_sensitive = [&](auto mutate) {
    internal::MultiWorldPoolSnapshotChecksumHeaderV1 changed = header;
    mutate(changed);
    EXPECT_NE(internal::ComputeMultiWorldPoolSnapshotChecksumV1(changed), pool_snapshot);
  };
  expect_pool_field_sensitive([](auto& changed) { ++changed.schema_version; });
  expect_pool_field_sensitive([](auto& changed) { ++changed.associations.board_content_hash; });
  expect_pool_field_sensitive(
      [](auto& changed) { ++changed.associations.compiler_profile_fingerprint; });
  expect_pool_field_sensitive(
      [](auto& changed) { ++changed.associations.geometry_compiler_version; });
  expect_pool_field_sensitive([](auto& changed) { ++changed.workload_checksum; });
  expect_pool_field_sensitive([](auto& changed) { ++changed.capacity_model_checksum; });
  expect_pool_field_sensitive([](auto& changed) { ++changed.allocator_limits.maximum_nets; });
  expect_pool_field_sensitive([](auto& changed) { ++changed.allocator_limits.maximum_candidates; });
  expect_pool_field_sensitive(
      [](auto& changed) { ++changed.allocator_limits.maximum_resource_records; });
  expect_pool_field_sensitive(
      [](auto& changed) { ++changed.allocator_limits.maximum_expanded_resource_uses; });
  expect_pool_field_sensitive([](auto& changed) { ++changed.candidate_pool_manifest_checksum; });
  expect_pool_field_sensitive([](auto& changed) { ++changed.source_pool_count; });
  expect_pool_field_sensitive([](auto& changed) { ++changed.source_candidate_count; });
}

TEST(MultiWorldTest, ChecksumRepresentationIsGoldenAndFieldSensitive) {
  internal::MultiWorldExecutionChecksumHeaderV1 header{
      .schema_version = 2,
      .source_snapshot_checksum = 3,
      .branch_state_checksum = 5,
      .config =
          MultiWorldExecutionConfig{
              .maximum_worlds = 7,
              .maximum_total_selection_rounds = 11,
              .maximum_total_candidate_evaluations = 13,
              .maximum_total_candidate_span_visits = 17,
              .maximum_total_resource_work_units = 19,
              .maximum_total_net_outcomes = 23,
              .maximum_pareto_comparisons = 29,
              .maximum_buffered_terminal_selection_records = 31,
              .maximum_buffered_terminal_resource_records = 37,
              .maximum_buffered_terminal_price_records = 41,
              .maximum_retained_worlds = 43,
              .maximum_retained_selection_records = 47,
              .maximum_retained_resource_records = 53,
              .maximum_retained_price_records = 59,
              .maximum_retained_winner_pins = 61,
              .maximum_near_feasible_missing_nets = 67,
              .maximum_near_feasible_overuse_units = 71,
              .known_unmapped_exact_conflict_count = 73,
          },
      .disposition = MultiWorldExecutionDisposition::kNoEligibleWorlds,
      .counters =
          MultiWorldExecutionCounters{
              .scheduled_worlds = 79,
              .completed_worlds = 83,
              .selection_rounds = 89,
              .price_updates = 97,
              .candidate_evaluations = 101,
              .candidate_span_visits = 103,
              .resource_work_units = 107,
              .net_outcomes = 109,
              .pareto_comparisons = 113,
              .pareto_eligible_worlds = 127,
              .retained_worlds = 131,
              .retained_winner_pins = 137,
          },
      .preferred_world_identity = 139,
  };
  std::vector<MultiWorldSchedule> schedules = {
      MultiWorldSchedule{.schedule_key = 113,
                         .search_intrinsic_cost_weight = 127,
                         .maximum_selection_rounds = 131},
  };
  std::vector<MultiWorldSummary> summaries = {
      MultiWorldSummary{
          .world_identity = 137,
          .schedule = schedules.front(),
          .terminal_reason = MultiWorldTerminalReason::kSelectionRoundLimit,
          .trace = {MultiWorldRoundTrace{
              .round_index = 139, .price_state_checksum = 149, .world_checksum = 151}},
          .selected_net_count = 157,
          .no_candidate_net_count = 163,
          .overused_resource_count = 167,
          .total_overuse_units = 173,
          .total_intrinsic_cost = 179,
          .pareto_eligible = true,
          .pareto_retained = false,
      },
  };
  std::vector<internal::MultiWorldRetainedReplayV1> retained = {
      internal::MultiWorldRetainedReplayV1{
          .world_identity = 181, .price_state_checksum = 191, .world_checksum = 193},
  };
  const std::uint64_t golden =
      internal::ComputeMultiWorldExecutionChecksumV1(header, schedules, summaries, retained);
  // Filled from the stable encoder once the implementation target is linked.
  EXPECT_EQ(golden, 2021987165982029810ULL);
  const auto checksum =
      [&](const internal::MultiWorldExecutionChecksumHeaderV1& changed_header,
          const std::vector<MultiWorldSchedule>& changed_schedules,
          const std::vector<MultiWorldSummary>& changed_summaries,
          const std::vector<internal::MultiWorldRetainedReplayV1>& changed_retained) {
        return internal::ComputeMultiWorldExecutionChecksumV1(changed_header, changed_schedules,
                                                              changed_summaries, changed_retained);
      };
  const auto expect_header_sensitive = [&](auto mutate) {
    internal::MultiWorldExecutionChecksumHeaderV1 changed = header;
    mutate(changed);
    EXPECT_NE(checksum(changed, schedules, summaries, retained), golden);
  };
  expect_header_sensitive([](auto& changed) { ++changed.schema_version; });
  expect_header_sensitive([](auto& changed) { ++changed.source_snapshot_checksum; });
  expect_header_sensitive([](auto& changed) { ++changed.branch_state_checksum; });
  expect_header_sensitive(
      [](auto& changed) { changed.disposition = MultiWorldExecutionDisposition::kCompleted; });
  expect_header_sensitive([](auto& changed) { changed.preferred_world_identity.reset(); });
  expect_header_sensitive([](auto& changed) { ++*changed.preferred_world_identity; });

  const auto expect_config_sensitive = [&](auto member) {
    expect_header_sensitive([&](auto& changed) { ++(changed.config.*member); });
  };
  expect_config_sensitive(&MultiWorldExecutionConfig::maximum_worlds);
  expect_config_sensitive(&MultiWorldExecutionConfig::maximum_total_selection_rounds);
  expect_config_sensitive(&MultiWorldExecutionConfig::maximum_total_candidate_evaluations);
  expect_config_sensitive(&MultiWorldExecutionConfig::maximum_total_candidate_span_visits);
  expect_config_sensitive(&MultiWorldExecutionConfig::maximum_total_resource_work_units);
  expect_config_sensitive(&MultiWorldExecutionConfig::maximum_total_net_outcomes);
  expect_config_sensitive(&MultiWorldExecutionConfig::maximum_pareto_comparisons);
  expect_config_sensitive(&MultiWorldExecutionConfig::maximum_buffered_terminal_selection_records);
  expect_config_sensitive(&MultiWorldExecutionConfig::maximum_buffered_terminal_resource_records);
  expect_config_sensitive(&MultiWorldExecutionConfig::maximum_buffered_terminal_price_records);
  expect_config_sensitive(&MultiWorldExecutionConfig::maximum_retained_worlds);
  expect_config_sensitive(&MultiWorldExecutionConfig::maximum_retained_selection_records);
  expect_config_sensitive(&MultiWorldExecutionConfig::maximum_retained_resource_records);
  expect_config_sensitive(&MultiWorldExecutionConfig::maximum_retained_price_records);
  expect_config_sensitive(&MultiWorldExecutionConfig::maximum_retained_winner_pins);
  expect_config_sensitive(&MultiWorldExecutionConfig::maximum_near_feasible_missing_nets);
  expect_config_sensitive(&MultiWorldExecutionConfig::maximum_near_feasible_overuse_units);
  expect_config_sensitive(&MultiWorldExecutionConfig::known_unmapped_exact_conflict_count);

  const auto expect_counter_sensitive = [&](auto member) {
    expect_header_sensitive([&](auto& changed) { ++(changed.counters.*member); });
  };
  expect_counter_sensitive(&MultiWorldExecutionCounters::scheduled_worlds);
  expect_counter_sensitive(&MultiWorldExecutionCounters::completed_worlds);
  expect_counter_sensitive(&MultiWorldExecutionCounters::selection_rounds);
  expect_counter_sensitive(&MultiWorldExecutionCounters::price_updates);
  expect_counter_sensitive(&MultiWorldExecutionCounters::candidate_evaluations);
  expect_counter_sensitive(&MultiWorldExecutionCounters::candidate_span_visits);
  expect_counter_sensitive(&MultiWorldExecutionCounters::resource_work_units);
  expect_counter_sensitive(&MultiWorldExecutionCounters::net_outcomes);
  expect_counter_sensitive(&MultiWorldExecutionCounters::pareto_comparisons);
  expect_counter_sensitive(&MultiWorldExecutionCounters::pareto_eligible_worlds);
  expect_counter_sensitive(&MultiWorldExecutionCounters::retained_worlds);
  expect_counter_sensitive(&MultiWorldExecutionCounters::retained_winner_pins);

  const auto expect_schedule_sensitive = [&](auto mutate) {
    std::vector<MultiWorldSchedule> changed = schedules;
    mutate(changed.front());
    EXPECT_NE(checksum(header, changed, summaries, retained), golden);
  };
  expect_schedule_sensitive([](auto& changed) { ++changed.schedule_key; });
  expect_schedule_sensitive([](auto& changed) { ++changed.search_intrinsic_cost_weight; });
  expect_schedule_sensitive([](auto& changed) { ++changed.maximum_selection_rounds; });
  {
    std::vector<MultiWorldSchedule> changed = schedules;
    changed.push_back(MultiWorldSchedule{
        .schedule_key = 197, .search_intrinsic_cost_weight = 199, .maximum_selection_rounds = 211});
    EXPECT_NE(checksum(header, changed, summaries, retained), golden);
    const std::uint64_t ordered = checksum(header, changed, summaries, retained);
    std::ranges::reverse(changed);
    EXPECT_NE(checksum(header, changed, summaries, retained), ordered);
  }

  const auto expect_summary_sensitive = [&](auto mutate) {
    std::vector<MultiWorldSummary> changed = summaries;
    mutate(changed.front());
    EXPECT_NE(checksum(header, schedules, changed, retained), golden);
  };
  expect_summary_sensitive([](auto& changed) { ++changed.world_identity; });
  expect_summary_sensitive([](auto& changed) { ++changed.schedule.schedule_key; });
  expect_summary_sensitive([](auto& changed) { ++changed.schedule.search_intrinsic_cost_weight; });
  expect_summary_sensitive([](auto& changed) { ++changed.schedule.maximum_selection_rounds; });
  expect_summary_sensitive(
      [](auto& changed) { changed.terminal_reason = MultiWorldTerminalReason::kFeasible; });
  expect_summary_sensitive([](auto& changed) { ++changed.trace.front().round_index; });
  expect_summary_sensitive([](auto& changed) { ++changed.trace.front().price_state_checksum; });
  expect_summary_sensitive([](auto& changed) { ++changed.trace.front().world_checksum; });
  expect_summary_sensitive([](auto& changed) {
    changed.trace.push_back(MultiWorldRoundTrace{
        .round_index = 197, .price_state_checksum = 199, .world_checksum = 211});
  });
  {
    std::vector<MultiWorldSummary> changed = summaries;
    changed.front().trace.push_back(MultiWorldRoundTrace{
        .round_index = 197, .price_state_checksum = 199, .world_checksum = 211});
    const std::uint64_t ordered = checksum(header, schedules, changed, retained);
    std::ranges::reverse(changed.front().trace);
    EXPECT_NE(checksum(header, schedules, changed, retained), ordered);
  }
  expect_summary_sensitive([](auto& changed) { ++changed.selected_net_count; });
  expect_summary_sensitive([](auto& changed) { ++changed.no_candidate_net_count; });
  expect_summary_sensitive([](auto& changed) { ++changed.overused_resource_count; });
  expect_summary_sensitive([](auto& changed) { ++changed.total_overuse_units; });
  expect_summary_sensitive([](auto& changed) { ++changed.total_intrinsic_cost; });
  expect_summary_sensitive(
      [](auto& changed) { changed.pareto_eligible = !changed.pareto_eligible; });
  expect_summary_sensitive(
      [](auto& changed) { changed.pareto_retained = !changed.pareto_retained; });
  {
    std::vector<MultiWorldSummary> changed = summaries;
    MultiWorldSummary second = changed.front();
    ++second.world_identity;
    changed.push_back(std::move(second));
    EXPECT_NE(checksum(header, schedules, changed, retained), golden);
    const std::uint64_t ordered = checksum(header, schedules, changed, retained);
    std::ranges::reverse(changed);
    EXPECT_NE(checksum(header, schedules, changed, retained), ordered);
  }

  const auto expect_retained_sensitive = [&](auto mutate) {
    std::vector<internal::MultiWorldRetainedReplayV1> changed = retained;
    mutate(changed.front());
    EXPECT_NE(checksum(header, schedules, summaries, changed), golden);
  };
  expect_retained_sensitive([](auto& changed) { ++changed.world_identity; });
  expect_retained_sensitive([](auto& changed) { ++changed.price_state_checksum; });
  expect_retained_sensitive([](auto& changed) { ++changed.world_checksum; });
  {
    std::vector<internal::MultiWorldRetainedReplayV1> changed = retained;
    internal::MultiWorldRetainedReplayV1 second = changed.front();
    ++second.world_identity;
    changed.push_back(second);
    EXPECT_NE(checksum(header, schedules, summaries, changed), golden);
    const std::uint64_t ordered = checksum(header, schedules, summaries, changed);
    std::ranges::reverse(changed);
    EXPECT_NE(checksum(header, schedules, summaries, changed), ordered);
  }
}

}  // namespace
}  // namespace apgar::allocator
