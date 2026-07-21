#include "apgar/allocator/targeted_regeneration.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <memory>
#include <set>
#include <span>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

#include "apgar/candidates/route_candidate.h"
#include "src/allocator/one_world_internal.h"
#include "src/allocator/targeted_regeneration_internal.h"
#include "tests/support/board_builder.h"
#include "tests/support/candidate_builder.h"
#include "tests/support/compiler_builder.h"
#include "tests/support/google_test.h"
#include "tests/support/routing_builder.h"

namespace apgar::allocator {
namespace {

[[nodiscard]] MultiNetWorkload BuiltWorkload(MultiNetWorkloadResult result) {
  EXPECT_TRUE(std::holds_alternative<MultiNetWorkload>(result));
  if (!std::holds_alternative<MultiNetWorkload>(result)) {
    std::abort();
  }
  return std::get<MultiNetWorkload>(std::move(result));
}

[[nodiscard]] ResourceCapacityModel BuiltCapacities(ResourceCapacityModelResult result) {
  EXPECT_TRUE(std::holds_alternative<ResourceCapacityModel>(result));
  if (!std::holds_alternative<ResourceCapacityModel>(result)) {
    std::abort();
  }
  return std::get<ResourceCapacityModel>(std::move(result));
}

[[nodiscard]] NegotiatedPriceState BuiltState(NegotiatedPriceStateResult result) {
  EXPECT_TRUE(std::holds_alternative<NegotiatedPriceState>(result));
  if (!std::holds_alternative<NegotiatedPriceState>(result)) {
    std::abort();
  }
  return std::get<NegotiatedPriceState>(std::move(result));
}

[[nodiscard]] PriceSnapshot BuiltSnapshot(NegotiatedPriceSnapshotResult result) {
  EXPECT_TRUE(std::holds_alternative<PriceSnapshot>(result));
  if (!std::holds_alternative<PriceSnapshot>(result)) {
    std::abort();
  }
  return std::get<PriceSnapshot>(std::move(result));
}

[[nodiscard]] OneWorldAllocation BuiltWorld(OneWorldAllocationResult result) {
  EXPECT_TRUE(std::holds_alternative<OneWorldAllocation>(result));
  if (!std::holds_alternative<OneWorldAllocation>(result)) {
    std::abort();
  }
  return std::get<OneWorldAllocation>(std::move(result));
}

[[nodiscard]] TargetedRegenerationPlan BuiltPlan(TargetedRegenerationPlanResult result) {
  EXPECT_TRUE(std::holds_alternative<TargetedRegenerationPlan>(result))
      << (std::holds_alternative<TargetedRegenerationError>(result)
              ? std::string(std::get<TargetedRegenerationError>(result).invariant_id) + ": " +
                    std::string(std::get<TargetedRegenerationError>(result).detail)
              : std::string{});
  if (!std::holds_alternative<TargetedRegenerationPlan>(result)) {
    std::abort();
  }
  return std::get<TargetedRegenerationPlan>(std::move(result));
}

[[nodiscard]] candidates::StoredCandidate Stored(candidates::CandidateStoreAdmissionResult result) {
  EXPECT_TRUE(std::holds_alternative<candidates::StoredCandidate>(result));
  if (!std::holds_alternative<candidates::StoredCandidate>(result)) {
    std::abort();
  }
  return std::get<candidates::StoredCandidate>(std::move(result));
}

[[nodiscard]] internal::TargetedRegenerationResourceScanV1 BuiltResourceScan(
    internal::TargetedRegenerationResourceScanResultV1 result) {
  EXPECT_TRUE(std::holds_alternative<internal::TargetedRegenerationResourceScanV1>(result));
  if (!std::holds_alternative<internal::TargetedRegenerationResourceScanV1>(result)) {
    std::abort();
  }
  return std::get<internal::TargetedRegenerationResourceScanV1>(std::move(result));
}

[[nodiscard]] bool SchemaTargetRanksBefore(const TargetedRegenerationNet& left,
                                           const TargetedRegenerationNet& right) {
  return internal::TargetedRegenerationTargetRanksBeforeV1(left, right);
}

struct PlanningFixture {
  board_ir::BoardSnapshot board;
  MultiNetWorkload workload;
  ResourceCapacityModel capacities;
  std::unique_ptr<candidates::CandidateStore> store;
  std::vector<CandidatePool> pools;
  NegotiatedPriceState initial_price_state;
  OneWorldAllocationRequest request;
  OneWorldAllocation world;

  PlanningFixture(board_ir::BoardSnapshot board_value, MultiNetWorkload workload_value,
                  ResourceCapacityModel capacities_value,
                  std::unique_ptr<candidates::CandidateStore> store_value,
                  std::vector<CandidatePool> pools_value,
                  NegotiatedPriceState initial_price_state_value,
                  OneWorldAllocationRequest request_value, OneWorldAllocation world_value)
      : board(std::move(board_value)),
        workload(std::move(workload_value)),
        capacities(std::move(capacities_value)),
        store(std::move(store_value)),
        pools(std::move(pools_value)),
        initial_price_state(std::move(initial_price_state_value)),
        request(std::move(request_value)),
        world(std::move(world_value)) {
    request.workload = &workload;
  }
};

[[nodiscard]] PlanningFixture BuildPlanningFixture(std::uint32_t default_capacity_units,
                                                   std::uint64_t pin_lease_limit = 16) {
  board_ir::BoardData board_data = test_support::ValidM1TwoNetBoardData();
  board_data.obstacles.clear();
  board_ir::BoardSnapshot board = test_support::Snapshot(std::move(board_data));
  const geometry_compiler::CompilerProfile compiler_profile =
      test_support::DefaultCompilerProfile({0});
  board_ir::RoutingProfile second_profile = board.data().routing_profile;
  second_profile.net = board.data().nets[1].ref;
  const std::array specs = {
      MultiNetRoutingSpec{
          .routing_profile = board.data().routing_profile, .start_layer = 0, .goal_layer = 0},
      MultiNetRoutingSpec{.routing_profile = second_profile, .start_layer = 0, .goal_layer = 0},
  };
  MultiNetWorkload workload = BuiltWorkload(BuildMultiNetWorkload(
      kMultiNetWorkloadSchemaVersion, board, compiler_profile, specs, specs.size()));
  ResourceCapacityModel capacities = BuiltCapacities(BuildResourceCapacityModel(
      kResourceCapacityModelSchemaVersion, board, workload.nets().front().compiled_board,
      default_capacity_units, {}));

  auto store = std::make_unique<candidates::CandidateStore>(candidates::CandidateStoreConfig{
      .maximum_candidates_per_net = 8,
      .maximum_candidate_bytes_per_net = 16U * 1024U * 1024U,
      .maximum_rejection_records = 32,
      .maximum_pin_lease_items_per_transaction = pin_lease_limit,
  });

  std::vector<CandidatePool> pools;
  pools.reserve(workload.nets().size());
  std::uint64_t query = 1;
  for (const PreparedNetRoutingContext& context : workload.nets()) {
    candidates::GeneratedRouteCandidate draft =
        test_support::CandidateDraft(board, context.compiled_board, context.request, 1, query++);
    const candidates::CandidateAdmissionContext admission_context{
        .board = board,
        .compiled_board = context.compiled_board,
        .request = context.request,
    };
    static_cast<void>(Stored(store->Admit(admission_context, std::move(draft))));
    pools.push_back(CandidatePool{
        .net = context.request.net,
        .candidates = store->Enumerate(context.request.net),
    });
  }
  const NegotiatedPriceConfig price_config{
      .present_step_per_overuse_unit = 1,
      .history_step_per_overuse_unit = 1,
      .maximum_price_per_resource = 100,
      .maximum_iterations = 4,
      .maximum_price_records = 1'000,
  };
  NegotiatedPriceState initial_price_state = BuiltState(BuildInitialNegotiatedPriceState(
      kNegotiatedPriceStateSchemaVersion, capacities, workload, price_config));
  OneWorldAllocationRequest request{
      .associations = capacities.associations(),
      .capacities = capacities,
      .prices = BuiltSnapshot(BuildPriceSnapshotForState(capacities, initial_price_state)),
      .intrinsic_cost_weight = 1,
      .limits = OneWorldAllocatorLimits{},
      .pools = pools,
      .workload = &workload,
  };
  OneWorldAllocation world = BuiltWorld(AllocateOneWorld(request));
  return PlanningFixture(std::move(board), std::move(workload), std::move(capacities),
                         std::move(store), std::move(pools), std::move(initial_price_state),
                         std::move(request), std::move(world));
}

[[nodiscard]] TargetedRegenerationConfig PlanConfig() {
  return TargetedRegenerationConfig{
      .maximum_target_nets = 2,
      .maximum_columns_per_net = 4,
      .maximum_total_columns = 8,
      .maximum_resource_actions_per_net = 4,
      .maximum_total_resource_actions = 8,
      .maximum_expanded_resource_visits = 1'000,
  };
}

[[nodiscard]] std::vector<routing::EdgeResourceKey> AtomicResources(
    const candidates::StoredCandidate& candidate) {
  std::vector<routing::EdgeResourceKey> resources;
  for (const candidates::PhysicalEdgeSpan& span : candidate->data().resources) {
    const geometry_compiler::DirectionDelta delta =
        candidates::ResourceSpanStorageDelta(span.direction);
    for (std::uint32_t offset = 0; offset < span.edge_count; ++offset) {
      resources.push_back(routing::EdgeResourceKey{
          .layer = span.layer,
          .lattice_x = span.lattice_x + static_cast<std::int64_t>(delta.x) * offset,
          .lattice_y = span.lattice_y + static_cast<std::int64_t>(delta.y) * offset,
          .direction = span.direction,
      });
    }
  }
  return resources;
}

[[nodiscard]] routing::EdgeResourceKey ResourceUniqueTo(
    const candidates::StoredCandidate& candidate, const candidates::StoredCandidate& alternative) {
  const std::vector<routing::EdgeResourceKey> candidate_resources = AtomicResources(candidate);
  const std::vector<routing::EdgeResourceKey> alternative_vector = AtomicResources(alternative);
  const std::set<routing::EdgeResourceKey> alternative_resources(alternative_vector.begin(),
                                                                 alternative_vector.end());
  const auto unique = std::ranges::find_if(candidate_resources, [&](const auto& resource) {
    return !alternative_resources.contains(resource);
  });
  EXPECT_NE(unique, candidate_resources.end());
  if (unique == candidate_resources.end()) {
    std::abort();
  }
  return *unique;
}

TEST(TargetedRegenerationTest, PlansAuthenticConflictedNetsDeterministically) {
  PlanningFixture fixture = BuildPlanningFixture(0);
  const TargetedRegenerationPlan first = BuiltPlan(BuildTargetedRegenerationPlan(
      kTargetedRegenerationPlanSchemaVersion, fixture.initial_price_state, fixture.request,
      fixture.world, *fixture.store, PlanConfig()));
  const TargetedRegenerationPlan repeated = BuiltPlan(BuildTargetedRegenerationPlan(
      kTargetedRegenerationPlanSchemaVersion, fixture.initial_price_state, fixture.request,
      fixture.world, *fixture.store, PlanConfig()));
  EXPECT_EQ(first, repeated);
  EXPECT_EQ(first.price_state().iteration(), 1U);
  EXPECT_EQ(first.price_state().source_world_checksum(), fixture.world.world_checksum);
  EXPECT_EQ(first.targets().size(), 2U);
  EXPECT_EQ(first.total_requested_columns(), 8U);
  EXPECT_EQ(first.total_resource_actions(), 8U);
  EXPECT_GT(first.total_conflict_resources(), 0U);
  EXPECT_GT(first.total_conflict_impact(), 0U);
  EXPECT_GT(first.expanded_resource_visits(), 0U);
  EXPECT_NE(first.plan_checksum(), 0U);
  EXPECT_NE(first.source_request_manifest_checksum(), 0U);
  EXPECT_NE(first.candidate_pool_manifest_checksum(), 0U);
  EXPECT_EQ(first.source_pool_count(), 2U);
  EXPECT_EQ(first.source_candidate_count(), 2U);
  EXPECT_EQ(first.pinned_candidate_count(), 2U);
  EXPECT_TRUE(first.has_active_pin_lease());
  ASSERT_TRUE(std::ranges::is_sorted(first.targets(), SchemaTargetRanksBefore));
  for (const TargetedRegenerationNet& target : first.targets()) {
    EXPECT_EQ(target.requested_columns, 4U);
    ASSERT_EQ(target.resource_actions.size(), 4U);
    const auto selection =
        std::ranges::find(fixture.world.selections, target.net, &NetSelection::net);
    ASSERT_NE(selection, fixture.world.selections.end());
    EXPECT_EQ(target.source_selected_candidate_id, selection->candidate_id);
    EXPECT_EQ(target.source_selected_candidate_payload_checksum,
              selection->candidate_payload_checksum);
    EXPECT_EQ(target.next_price_candidate_id, selection->candidate_id);
    EXPECT_EQ(target.next_price_candidate_payload_checksum, selection->candidate_payload_checksum);
    for (const RegenerationResourceAction& action : target.resource_actions) {
      EXPECT_GT(action.observed_overuse_units, 0U);
      EXPECT_GT(action.selected_candidate_usage_units, 0U);
      EXPECT_GT(action.negotiated_price, 0U);
      EXPECT_EQ(action.conflict_impact,
                action.observed_overuse_units * action.selected_candidate_usage_units);
    }
  }

  std::ranges::reverse(fixture.request.pools);
  const OneWorldAllocation reordered_world = BuiltWorld(AllocateOneWorld(fixture.request));
  const TargetedRegenerationPlan reordered = BuiltPlan(BuildTargetedRegenerationPlan(
      kTargetedRegenerationPlanSchemaVersion, fixture.initial_price_state, fixture.request,
      reordered_world, *fixture.store, PlanConfig()));
  EXPECT_EQ(reordered, first);
}

TEST(TargetedRegenerationTest, EmptyWhenFeasibleAndDeterministicallyCapsHotsetBudgets) {
  PlanningFixture feasible = BuildPlanningFixture(1);
  ASSERT_EQ(feasible.world.total_overuse_units, 0U);
  const TargetedRegenerationPlan empty = BuiltPlan(BuildTargetedRegenerationPlan(
      kTargetedRegenerationPlanSchemaVersion, feasible.initial_price_state, feasible.request,
      feasible.world, *feasible.store, PlanConfig()));
  EXPECT_TRUE(empty.targets().empty());
  EXPECT_EQ(empty.total_requested_columns(), 0U);
  EXPECT_EQ(empty.total_resource_actions(), 0U);

  PlanningFixture conflicted = BuildPlanningFixture(0);
  const TargetedRegenerationPlan full = BuiltPlan(BuildTargetedRegenerationPlan(
      kTargetedRegenerationPlanSchemaVersion, conflicted.initial_price_state, conflicted.request,
      conflicted.world, *conflicted.store, PlanConfig()));
  ASSERT_EQ(full.targets().size(), 2U);
  EXPECT_EQ(full.expanded_resource_visits(), conflicted.world.selected_logical_resource_uses * 3U);

  TargetedRegenerationConfig exact_visit_bound = PlanConfig();
  exact_visit_bound.maximum_expanded_resource_visits = full.expanded_resource_visits();
  const TargetedRegenerationPlan exact_bound = BuiltPlan(BuildTargetedRegenerationPlan(
      kTargetedRegenerationPlanSchemaVersion, conflicted.initial_price_state, conflicted.request,
      conflicted.world, *conflicted.store, exact_visit_bound));
  EXPECT_EQ(exact_bound.expanded_resource_visits(), full.expanded_resource_visits());

  TargetedRegenerationConfig rescan_one_over = PlanConfig();
  rescan_one_over.maximum_expanded_resource_visits =
      conflicted.world.selected_logical_resource_uses * 2U;
  const TargetedRegenerationPlanResult rescan_bound = BuildTargetedRegenerationPlan(
      kTargetedRegenerationPlanSchemaVersion, conflicted.initial_price_state, conflicted.request,
      conflicted.world, *conflicted.store, rescan_one_over);
  ASSERT_TRUE(std::holds_alternative<TargetedRegenerationError>(rescan_bound));
  EXPECT_EQ(std::get<TargetedRegenerationError>(rescan_bound).code,
            TargetedRegenerationErrorCode::kWorkBoundExceeded);
  TargetedRegenerationConfig bounded = PlanConfig();
  bounded.maximum_target_nets = 1;
  bounded.maximum_columns_per_net = 1;
  bounded.maximum_total_columns = 1;
  bounded.maximum_resource_actions_per_net = 1;
  bounded.maximum_total_resource_actions = 1;
  const TargetedRegenerationPlan one = BuiltPlan(BuildTargetedRegenerationPlan(
      kTargetedRegenerationPlanSchemaVersion, conflicted.initial_price_state, conflicted.request,
      conflicted.world, *conflicted.store, bounded));
  ASSERT_EQ(one.targets().size(), 1U);
  EXPECT_EQ(one.targets().front().requested_columns, 1U);
  EXPECT_EQ(one.targets().front().resource_actions.size(), 1U);
  EXPECT_EQ(one.total_requested_columns(), 1U);
  EXPECT_EQ(one.total_resource_actions(), 1U);
  EXPECT_EQ(one.targets().front().net, full.targets().front().net);
  EXPECT_EQ(one.targets().front().next_price_candidate_id,
            full.targets().front().next_price_candidate_id);
  EXPECT_EQ(one.targets().front().resource_actions.front().resource,
            full.targets().front().resource_actions.front().resource);

  TargetedRegenerationConfig globally_truncated = PlanConfig();
  globally_truncated.maximum_total_resource_actions = 5;
  const TargetedRegenerationPlan truncated = BuiltPlan(BuildTargetedRegenerationPlan(
      kTargetedRegenerationPlanSchemaVersion, conflicted.initial_price_state, conflicted.request,
      conflicted.world, *conflicted.store, globally_truncated));
  ASSERT_EQ(truncated.targets().size(), 2U);
  ASSERT_EQ(truncated.targets()[0].resource_actions.size(), 4U);
  ASSERT_EQ(truncated.targets()[1].resource_actions.size(), 1U);
  EXPECT_EQ(truncated.total_resource_actions(), 5U);
  for (std::size_t target_index = 0; target_index < truncated.targets().size(); ++target_index) {
    EXPECT_EQ(truncated.targets()[target_index].net, full.targets()[target_index].net);
    for (std::size_t action_index = 0;
         action_index < truncated.targets()[target_index].resource_actions.size(); ++action_index) {
      EXPECT_EQ(truncated.targets()[target_index].resource_actions[action_index],
                full.targets()[target_index].resource_actions[action_index]);
    }
  }
}

TEST(TargetedRegenerationTest, CompletePoolAlternativeSuppressesUnnecessaryRegeneration) {
  board_ir::BoardSnapshot board = test_support::Snapshot();
  const geometry_compiler::CompilerProfile compiler_profile =
      test_support::DefaultCompilerProfile({0});
  const std::array specs = {
      MultiNetRoutingSpec{
          .routing_profile = board.data().routing_profile, .start_layer = 0, .goal_layer = 0},
  };
  MultiNetWorkload workload = BuiltWorkload(BuildMultiNetWorkload(
      kMultiNetWorkloadSchemaVersion, board, compiler_profile, specs, specs.size()));
  const PreparedNetRoutingContext& context = workload.nets().front();
  routing::CpuRouteRequest first_request = context.request;
  first_request.candidate_policy.deterministic_seed = 0x1234U;
  candidates::GeneratedRouteCandidate first_draft =
      test_support::CandidateDraft(board, context.compiled_board, first_request, 7, 1);
  const candidates::RouteCandidate first_accepted = test_support::AcceptedCandidate(
      candidates::CandidateAdmissionContext{
          .board = board, .compiled_board = context.compiled_board, .request = first_request},
      first_draft);
  const candidates::StoredCandidate first_probe =
      std::make_shared<const candidates::RouteCandidate>(first_accepted);
  const std::vector<routing::EdgeResourceKey> first_resources = AtomicResources(first_probe);
  ASSERT_FALSE(first_resources.empty());
  routing::CpuRouteRequest alternate_request = first_request;
  alternate_request.candidate_policy.candidate_ordinal = 1;
  alternate_request.candidate_policy.banned_resources = {
      first_resources[first_resources.size() / 2U]};
  candidates::GeneratedRouteCandidate alternate_draft =
      test_support::CandidateDraft(board, context.compiled_board, alternate_request, 7, 2);

  candidates::CandidateStore store(candidates::CandidateStoreConfig{
      .maximum_candidates_per_net = 8,
      .maximum_candidate_bytes_per_net = 16U * 1024U * 1024U,
      .maximum_rejection_records = 32,
      .maximum_pin_lease_items_per_transaction = 2,
  });
  const candidates::CandidateAdmissionContext first_admission{
      .board = board, .compiled_board = context.compiled_board, .request = first_request};
  const candidates::CandidateAdmissionContext alternate_admission{
      .board = board, .compiled_board = context.compiled_board, .request = alternate_request};
  static_cast<void>(Stored(store.Admit(first_admission, std::move(first_draft))));
  static_cast<void>(Stored(store.Admit(alternate_admission, std::move(alternate_draft))));
  const std::vector<candidates::StoredCandidate> candidates = store.Enumerate(context.request.net);
  ASSERT_EQ(candidates.size(), 2U);
  const auto intrinsic_rank = [](const candidates::StoredCandidate& candidate) {
    return std::tuple{candidate->data().metrics.intrinsic_base_cost, candidate->id()};
  };
  const candidates::StoredCandidate intrinsic_best =
      intrinsic_rank(candidates[0]) < intrinsic_rank(candidates[1]) ? candidates[0] : candidates[1];
  const candidates::StoredCandidate alternative =
      intrinsic_best == candidates[0] ? candidates[1] : candidates[0];
  const routing::EdgeResourceKey unavailable = ResourceUniqueTo(intrinsic_best, alternative);
  ResourceCapacityModel capacities = BuiltCapacities(BuildResourceCapacityModel(
      kResourceCapacityModelSchemaVersion, board, context.compiled_board, 1,
      {ResourceCapacityOverride{.resource = unavailable, .capacity_units = 0}}));
  const std::uint64_t intrinsic_difference = alternative->data().metrics.intrinsic_base_cost -
                                             intrinsic_best->data().metrics.intrinsic_base_cost;
  NegotiatedPriceState initial = BuiltState(BuildInitialNegotiatedPriceState(
      kNegotiatedPriceStateSchemaVersion, capacities, workload,
      NegotiatedPriceConfig{.present_step_per_overuse_unit = intrinsic_difference + 1U,
                            .history_step_per_overuse_unit = intrinsic_difference + 1U,
                            .maximum_price_per_resource = 1'000'000'000,
                            .maximum_iterations = 4,
                            .maximum_price_records = 1'000}));
  OneWorldAllocationRequest request{
      .associations = capacities.associations(),
      .capacities = capacities,
      .prices = BuiltSnapshot(BuildPriceSnapshotForState(capacities, initial)),
      .intrinsic_cost_weight = 1,
      .limits = OneWorldAllocatorLimits{},
      .pools = {CandidatePool{.net = context.request.net, .candidates = candidates}},
      .workload = &workload,
  };
  const OneWorldAllocation world = BuiltWorld(AllocateOneWorld(request));
  ASSERT_EQ(world.selections.front().candidate_id, intrinsic_best->id());
  ASSERT_GT(world.total_overuse_units, 0U);

  {
    const TargetedRegenerationPlan plan = BuiltPlan(BuildTargetedRegenerationPlan(
        kTargetedRegenerationPlanSchemaVersion, initial, request, world, store, PlanConfig()));
    EXPECT_TRUE(plan.targets().empty());
    EXPECT_EQ(plan.source_candidate_count(), 2U);
    EXPECT_EQ(plan.pinned_candidate_count(), 2U);
    EXPECT_TRUE(store.IsPinned(intrinsic_best->id()));
    EXPECT_TRUE(store.IsPinned(alternative->id()));
    EXPECT_TRUE(plan.has_active_pin_lease());
    OneWorldAllocationRequest next_request = request;
    next_request.prices = BuiltSnapshot(BuildPriceSnapshotForState(capacities, plan.price_state()));
    const OneWorldAllocation next_world = BuiltWorld(AllocateOneWorld(next_request));
    EXPECT_EQ(next_world.selections.front().candidate_id, alternative->id());
    EXPECT_EQ(next_world.total_overuse_units, 0U);
  }
  EXPECT_FALSE(store.IsPinned(intrinsic_best->id()));
  EXPECT_FALSE(store.IsPinned(alternative->id()));
}

TEST(TargetedRegenerationTest, RejectsDetachedSelectedCandidatesAtAtomicLeaseBoundary) {
  PlanningFixture fixture = BuildPlanningFixture(0);
  candidates::CandidateStore empty_store(fixture.store->config());
  const TargetedRegenerationPlanResult result = BuildTargetedRegenerationPlan(
      kTargetedRegenerationPlanSchemaVersion, fixture.initial_price_state, fixture.request,
      fixture.world, empty_store, PlanConfig());
  ASSERT_TRUE(std::holds_alternative<TargetedRegenerationError>(result));
  EXPECT_EQ(std::get<TargetedRegenerationError>(result).code,
            TargetedRegenerationErrorCode::kCandidateStoreLease);
  for (const NetSelection& selection : fixture.world.selections) {
    ASSERT_TRUE(selection.candidate_id.has_value());
    EXPECT_FALSE(empty_store.IsPinned(*selection.candidate_id));
  }
}

TEST(TargetedRegenerationTest, DeduplicatesSourceAndNextWinnersBeforeExactLeaseCap) {
  PlanningFixture fixture = BuildPlanningFixture(0, 2);
  const TargetedRegenerationPlan plan = BuiltPlan(BuildTargetedRegenerationPlan(
      kTargetedRegenerationPlanSchemaVersion, fixture.initial_price_state, fixture.request,
      fixture.world, *fixture.store, PlanConfig()));
  EXPECT_EQ(plan.targets().size(), 2U);
  EXPECT_EQ(plan.pinned_candidate_count(), 2U);
  EXPECT_TRUE(plan.has_active_pin_lease());
}

TEST(TargetedRegenerationTest, PlanDoesNotReportExecutableLeaseAfterStoreDestruction) {
  PlanningFixture fixture = BuildPlanningFixture(0);
  std::optional<TargetedRegenerationPlan> plan;
  plan.emplace(BuiltPlan(BuildTargetedRegenerationPlan(
      kTargetedRegenerationPlanSchemaVersion, fixture.initial_price_state, fixture.request,
      fixture.world, *fixture.store, PlanConfig())));
  ASSERT_TRUE(plan->has_active_pin_lease());
  fixture.store.reset();
  EXPECT_FALSE(plan->has_active_pin_lease());
  plan.reset();
}

TEST(TargetedRegenerationTest, ResourceScanRanksTopKAndCountsHistoryOnlyExposure) {
  const auto resource = [](std::int64_t lattice_x) {
    return routing::EdgeResourceKey{
        .layer = 0,
        .lattice_x = lattice_x,
        .lattice_y = 0,
        .direction = geometry_compiler::Direction::kEast,
    };
  };
  const std::array spans = {
      candidates::PhysicalEdgeSpan{.layer = 0,
                                   .lattice_x = 0,
                                   .lattice_y = 0,
                                   .direction = geometry_compiler::Direction::kEast,
                                   .edge_count = 1,
                                   .usage_units = 2},
      candidates::PhysicalEdgeSpan{.layer = 0,
                                   .lattice_x = 1,
                                   .lattice_y = 0,
                                   .direction = geometry_compiler::Direction::kEast,
                                   .edge_count = 1,
                                   .usage_units = 3},
      candidates::PhysicalEdgeSpan{.layer = 0,
                                   .lattice_x = 2,
                                   .lattice_y = 0,
                                   .direction = geometry_compiler::Direction::kEast,
                                   .edge_count = 1,
                                   .usage_units = 3},
      candidates::PhysicalEdgeSpan{.layer = 0,
                                   .lattice_x = 3,
                                   .lattice_y = 0,
                                   .direction = geometry_compiler::Direction::kEast,
                                   .edge_count = 1,
                                   .usage_units = 3},
      candidates::PhysicalEdgeSpan{.layer = 0,
                                   .lattice_x = 4,
                                   .lattice_y = 0,
                                   .direction = geometry_compiler::Direction::kEast,
                                   .edge_count = 1,
                                   .usage_units = 1},
  };
  const std::array resources = {
      ResourceUsage{.resource = resource(0), .usage_units = 3, .overuse_units = 3},
      ResourceUsage{.resource = resource(1), .usage_units = 2, .overuse_units = 2},
      ResourceUsage{.resource = resource(2), .usage_units = 2, .overuse_units = 2},
      ResourceUsage{.resource = resource(3), .usage_units = 2, .overuse_units = 2},
      ResourceUsage{.resource = resource(4),
                    .has_capacity_override = true,
                    .capacity_units = 1,
                    .usage_units = 1,
                    .overuse_units = 0},
  };
  const std::array prices = {
      NegotiatedResourcePrice{.resource = resource(0), .history_price = 1, .total_price = 1},
      NegotiatedResourcePrice{.resource = resource(1), .history_price = 8, .total_price = 8},
      NegotiatedResourcePrice{.resource = resource(2), .history_price = 9, .total_price = 9},
      NegotiatedResourcePrice{.resource = resource(3), .history_price = 9, .total_price = 9},
      // This resource has history price but no current overuse. Exposure must
      // still include it even though it cannot become a conflict action.
      NegotiatedResourcePrice{.resource = resource(4), .history_price = 100, .total_price = 100},
  };

  const internal::TargetedRegenerationResourceScanV1 scan =
      BuiltResourceScan(internal::ScanTargetedRegenerationResourcesV1(spans, resources, prices, 4));
  EXPECT_EQ(scan.conflict_resource_count, 4U);
  EXPECT_EQ(scan.conflict_impact, 24U);
  EXPECT_EQ(scan.negotiated_price_exposure, 180U);
  ASSERT_EQ(scan.resource_actions.size(), 4U);
  EXPECT_EQ(scan.resource_actions[0].resource, resource(0));
  EXPECT_EQ(scan.resource_actions[1].resource, resource(2));
  EXPECT_EQ(scan.resource_actions[2].resource, resource(3));
  EXPECT_EQ(scan.resource_actions[3].resource, resource(1));
}

TEST(TargetedRegenerationTest, TargetSeverityOrderDiscriminatesEveryTieBreakKey) {
  const TargetedRegenerationNet base{
      .net = {.id = 20, .generation = 2},
      .source_pool_manifest_checksum = 0,
      .source_pool_candidate_count = 0,
      .source_selected_candidate_id = {},
      .source_selected_candidate_payload_checksum = 0,
      .next_price_candidate_id = {.high = 30, .low = 31},
      .next_price_candidate_payload_checksum = 0,
      .next_price_selection_score = 0,
      .conflict_resource_count = 7,
      .conflict_impact = 11,
      .negotiated_price_exposure = 13,
      .requested_columns = 0,
      .resource_actions = {},
  };
  const auto expect_before = [](TargetedRegenerationNet preferred, TargetedRegenerationNet other) {
    EXPECT_TRUE(internal::TargetedRegenerationTargetRanksBeforeV1(preferred, other));
    EXPECT_FALSE(internal::TargetedRegenerationTargetRanksBeforeV1(other, preferred));
  };

  TargetedRegenerationNet higher_impact = base;
  ++higher_impact.conflict_impact;
  expect_before(higher_impact, base);

  TargetedRegenerationNet higher_exposure = base;
  ++higher_exposure.negotiated_price_exposure;
  expect_before(higher_exposure, base);

  TargetedRegenerationNet more_conflicts = base;
  ++more_conflicts.conflict_resource_count;
  expect_before(more_conflicts, base);

  TargetedRegenerationNet lower_net = base;
  --lower_net.net.id;
  expect_before(lower_net, base);

  TargetedRegenerationNet lower_generation = base;
  --lower_generation.net.generation;
  expect_before(lower_generation, base);

  TargetedRegenerationNet lower_candidate = base;
  --lower_candidate.next_price_candidate_id.low;
  expect_before(lower_candidate, base);
}

TEST(TargetedRegenerationTest, RejectsWorkOverflowAndChecksumConsistentWorldFabrication) {
  const PlanningFixture fixture = BuildPlanningFixture(0);
  TargetedRegenerationConfig tiny = PlanConfig();
  tiny.maximum_expanded_resource_visits = 1;
  const TargetedRegenerationPlanResult work_bound = BuildTargetedRegenerationPlan(
      kTargetedRegenerationPlanSchemaVersion, fixture.initial_price_state, fixture.request,
      fixture.world, *fixture.store, tiny);
  ASSERT_TRUE(std::holds_alternative<TargetedRegenerationError>(work_bound));
  EXPECT_EQ(std::get<TargetedRegenerationError>(work_bound).code,
            TargetedRegenerationErrorCode::kWorkBoundExceeded);

  OneWorldAllocation fabricated = fixture.world;
  auto used = std::ranges::find_if(
      fabricated.resources, [](const ResourceUsage& usage) { return usage.usage_units > 0; });
  ASSERT_NE(used, fabricated.resources.end());
  ++used->usage_units;
  ++used->overuse_units;
  ++fabricated.total_overuse_units;
  fabricated.world_checksum = internal::ComputeOneWorldChecksumV2(fabricated);
  const TargetedRegenerationPlanResult invalid = BuildTargetedRegenerationPlan(
      kTargetedRegenerationPlanSchemaVersion, fixture.initial_price_state, fixture.request,
      fabricated, *fixture.store, PlanConfig());
  ASSERT_TRUE(std::holds_alternative<TargetedRegenerationError>(invalid));
  EXPECT_EQ(std::get<TargetedRegenerationError>(invalid).code,
            TargetedRegenerationErrorCode::kInvalidPricingInput);
}

TEST(TargetedRegenerationTest, PlanChecksumHasGoldenAndFieldSensitivity) {
  internal::TargetedRegenerationChecksumHeaderV1 header{
      .schema_version = 2,
      .associations = {.board_content_hash = 3,
                       .compiler_profile_fingerprint = 5,
                       .geometry_compiler_version = 7},
      .workload_checksum = 11,
      .source_request_manifest_checksum = 13,
      .candidate_pool_manifest_checksum = 17,
      .source_pool_count = 19,
      .source_candidate_count = 23,
      .pinned_candidate_count = 29,
      .config = {.maximum_target_nets = 31,
                 .maximum_columns_per_net = 37,
                 .maximum_total_columns = 41,
                 .maximum_resource_actions_per_net = 43,
                 .maximum_total_resource_actions = 47,
                 .maximum_expanded_resource_visits = 53},
      .price_state_checksum = 59,
      .price_iteration = 61,
      .source_world_checksum = 67,
      .total_requested_columns = 71,
      .total_resource_actions = 73,
      .total_conflict_resources = 79,
      .total_conflict_impact = 83,
      .expanded_resource_visits = 89,
  };
  std::array targets = {
      TargetedRegenerationNet{
          .net = {.id = 97, .generation = 101},
          .source_pool_manifest_checksum = 103,
          .source_pool_candidate_count = 107,
          .source_selected_candidate_id = {.high = 109, .low = 113},
          .source_selected_candidate_payload_checksum = 127,
          .next_price_candidate_id = {.high = 131, .low = 137},
          .next_price_candidate_payload_checksum = 139,
          .next_price_selection_score = 149,
          .conflict_resource_count = 151,
          .conflict_impact = 157,
          .negotiated_price_exposure = 163,
          .requested_columns = 167,
          .resource_actions =
              {
                  RegenerationResourceAction{
                      .resource = {.layer = 173,
                                   .lattice_x = -179,
                                   .lattice_y = 181,
                                   .direction = geometry_compiler::Direction::kNorthWest},
                      .observed_overuse_units = 191,
                      .selected_candidate_usage_units = 193,
                      .negotiated_price = 197,
                      .conflict_impact = 199,
                  },
              },
      },
  };
  const std::uint64_t golden = internal::ComputeTargetedRegenerationPlanChecksumV1(header, targets);
  EXPECT_EQ(golden, 10'960'306'375'439'121'819ULL);
  ++header.price_state_checksum;
  EXPECT_NE(internal::ComputeTargetedRegenerationPlanChecksumV1(header, targets), golden);
  --header.price_state_checksum;
  ++targets.front().resource_actions.front().conflict_impact;
  EXPECT_NE(internal::ComputeTargetedRegenerationPlanChecksumV1(header, targets), golden);
}

}  // namespace
}  // namespace apgar::allocator
