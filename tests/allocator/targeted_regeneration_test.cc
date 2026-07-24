#include "apgar/allocator/targeted_regeneration.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <limits>
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
  EXPECT_TRUE(std::holds_alternative<MultiNetWorkload>(result))
      << (std::holds_alternative<MultiNetWorkloadError>(result)
              ? std::string(std::get<MultiNetWorkloadError>(result).invariant_id) + ": " +
                    std::string(std::get<MultiNetWorkloadError>(result).detail)
              : std::string{});
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

[[nodiscard]] std::vector<routing::NormalizedCandidateGenerationPolicy> BuiltPolicies(
    TargetedRegenerationPolicyResult result) {
  EXPECT_TRUE(
      std::holds_alternative<std::vector<routing::NormalizedCandidateGenerationPolicy>>(result))
      << (std::holds_alternative<TargetedRegenerationError>(result)
              ? std::string(std::get<TargetedRegenerationError>(result).invariant_id) + ": " +
                    std::string(std::get<TargetedRegenerationError>(result).detail)
              : std::string{});
  if (!std::holds_alternative<std::vector<routing::NormalizedCandidateGenerationPolicy>>(result)) {
    std::abort();
  }
  return std::get<std::vector<routing::NormalizedCandidateGenerationPolicy>>(std::move(result));
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

[[nodiscard]] PlanningFixture BuildSingletonConflictFixture() {
  PlanningFixture fixture = BuildPlanningFixture(1);
  EXPECT_EQ(fixture.pools.size(), 2U);
  if (fixture.pools.size() != 2U) {
    std::abort();
  }
  EXPECT_EQ(fixture.pools[0].candidates.size(), 1U);
  EXPECT_EQ(fixture.pools[1].candidates.size(), 1U);
  if (fixture.pools[0].candidates.size() != 1U || fixture.pools[1].candidates.size() != 1U) {
    std::abort();
  }
  const std::vector<routing::EdgeResourceKey> first =
      AtomicResources(fixture.pools[0].candidates.front());
  EXPECT_FALSE(first.empty());
  if (first.empty()) {
    std::abort();
  }
  fixture.capacities = BuiltCapacities(BuildResourceCapacityModel(
      kResourceCapacityModelSchemaVersion, fixture.board,
      fixture.workload.nets().front().compiled_board, 1,
      {ResourceCapacityOverride{.resource = first.front(), .capacity_units = 0}}));
  fixture.initial_price_state = BuiltState(BuildInitialNegotiatedPriceState(
      kNegotiatedPriceStateSchemaVersion, fixture.capacities, fixture.workload,
      NegotiatedPriceConfig{
          .present_step_per_overuse_unit = 1,
          .history_step_per_overuse_unit = 1,
          .maximum_price_per_resource = 100,
          .maximum_iterations = 4,
          .maximum_price_records = 1'000,
      }));
  fixture.request.capacities = fixture.capacities;
  fixture.request.prices =
      BuiltSnapshot(BuildPriceSnapshotForState(fixture.capacities, fixture.initial_price_state));
  fixture.world = BuiltWorld(AllocateOneWorld(fixture.request));
  return fixture;
}

TEST(TargetedRegenerationTest, SingletonConflictPlansPriceOnlyAndSoleBanColumns) {
  PlanningFixture fixture = BuildSingletonConflictFixture();
  ASSERT_EQ(fixture.world.total_overuse_units, 1U);
  TargetedRegenerationConfig config = PlanConfig();
  config.maximum_target_nets = 2;
  config.maximum_columns_per_net = 2;
  config.maximum_total_columns = 2;
  config.maximum_resource_actions_per_net = 1;
  config.maximum_total_resource_actions = 2;

  const TargetedRegenerationPlan plan = BuiltPlan(BuildTargetedRegenerationPlan(
      kTargetedRegenerationPlanSchemaVersion, fixture.initial_price_state, fixture.request,
      fixture.world, *fixture.store, config));
  ASSERT_EQ(plan.targets().size(), 1U);
  const TargetedRegenerationNet& target = plan.targets().front();
  EXPECT_EQ(target.conflict_resource_count, 1U);
  ASSERT_EQ(target.resource_actions.size(), 1U);
  EXPECT_EQ(target.requested_columns, 2U);
  EXPECT_EQ(plan.total_requested_columns(), 2U);

  const PreparedNetRoutingContext* context = fixture.workload.FindNet(target.net);
  ASSERT_NE(context, nullptr);
  const std::vector<routing::NormalizedCandidateGenerationPolicy> policies = BuiltPolicies(
      BuildTargetedRegenerationPoliciesV1(*context, plan.price_state(), 1, target, 0x4321U, 9));
  ASSERT_EQ(policies.size(), 2U);
  EXPECT_TRUE(policies[0].policy.banned_resources.empty());
  ASSERT_EQ(policies[1].policy.banned_resources.size(), 1U);
  EXPECT_EQ(policies[1].policy.banned_resources.front(), target.resource_actions[0].resource);
}

TEST(TargetedRegenerationTest, RejectsLegacyPlanSchemaBeforeStoreMutation) {
  PlanningFixture fixture = BuildSingletonConflictFixture();
  const std::vector<candidates::StoredCandidate> before_first =
      fixture.store->Enumerate(fixture.request.pools[0].net);
  const std::vector<candidates::StoredCandidate> before_second =
      fixture.store->Enumerate(fixture.request.pools[1].net);
  const std::vector<candidates::CandidateRejection> before_rejections = fixture.store->Rejections();

  const TargetedRegenerationPlanResult result = BuildTargetedRegenerationPlan(
      kTargetedRegenerationPlanSchemaVersionV1, fixture.initial_price_state, fixture.request,
      fixture.world, *fixture.store, PlanConfig());
  ASSERT_TRUE(std::holds_alternative<TargetedRegenerationError>(result));
  EXPECT_EQ(std::get<TargetedRegenerationError>(result).code,
            TargetedRegenerationErrorCode::kUnsupportedSchema);
  EXPECT_EQ(fixture.store->Enumerate(fixture.request.pools[0].net), before_first);
  EXPECT_EQ(fixture.store->Enumerate(fixture.request.pools[1].net), before_second);
  EXPECT_EQ(fixture.store->Rejections(), before_rejections);
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

TEST(TargetedRegenerationTest, BuildsCompletePricedCpuPoliciesAndOrderedActionBans) {
  PlanningFixture fixture = BuildPlanningFixture(0);
  const TargetedRegenerationPlan plan = BuiltPlan(BuildTargetedRegenerationPlan(
      kTargetedRegenerationPlanSchemaVersion, fixture.initial_price_state, fixture.request,
      fixture.world, *fixture.store, PlanConfig()));
  ASSERT_FALSE(plan.targets().empty());
  const TargetedRegenerationNet& target = plan.targets().front();
  const PreparedNetRoutingContext* context = fixture.workload.FindNet(target.net);
  ASSERT_NE(context, nullptr);

  const std::vector<routing::NormalizedCandidateGenerationPolicy> policies = BuiltPolicies(
      BuildTargetedRegenerationPoliciesV1(*context, plan.price_state(), 3, target, 0x1234U, 17));
  ASSERT_EQ(policies.size(), target.requested_columns);
  const internal::TargetedRegenerationPolicyEntryProjectionV1 entry_projection =
      internal::ProjectTargetedRegenerationPolicyEntriesV1(context->compiled_board,
                                                           plan.price_state().prices(), target);
  std::uint64_t actual_policy_entries = 0;
  const geometry_compiler::DeterministicCosts costs = context->compiled_board.profile().costs;
  for (std::size_t column = 0; column < policies.size(); ++column) {
    const routing::CandidateGenerationPolicy& policy = policies[column].policy;
    actual_policy_entries += policy.banned_resources.size() + policy.resource_penalties.size();
    EXPECT_EQ(policy.deterministic_seed, 0x1234U);
    EXPECT_EQ(policy.candidate_ordinal, 17U + column);
    EXPECT_EQ(policy.orthogonal_step_surcharge, 2U * costs.orthogonal_step);
    EXPECT_EQ(policy.diagonal_step_surcharge, 2U * costs.diagonal_step);
    EXPECT_EQ(policy.bend_surcharge, 2U * costs.bend);
    if (column == 0) {
      EXPECT_TRUE(policy.banned_resources.empty());
      ASSERT_EQ(policy.resource_penalties.size(), plan.price_state().prices().size());
    } else {
      ASSERT_EQ(policy.banned_resources.size(), 1U);
      EXPECT_EQ(policy.banned_resources.front(), target.resource_actions[column - 1U].resource);
    }
    for (const NegotiatedResourcePrice& price : plan.price_state().prices()) {
      const bool banned = std::ranges::binary_search(policy.banned_resources, price.resource);
      EXPECT_EQ(routing::PolicyPenaltyForResource(policy, price.resource),
                banned ? 0U : price.total_price);
    }
  }
  EXPECT_EQ(entry_projection.aggregate_entry_count, actual_policy_entries);
  EXPECT_TRUE(internal::TargetedRegenerationPolicyEntriesFitV1(
      routing::kMaximumPolicyResourceEntries, routing::kMaximumPolicyResourceEntries));
  EXPECT_FALSE(internal::TargetedRegenerationPolicyEntriesFitV1(
      routing::kMaximumPolicyResourceEntries + 1U, routing::kMaximumPolicyResourceEntries));

  const std::vector<routing::NormalizedCandidateGenerationPolicy> repeated = BuiltPolicies(
      BuildTargetedRegenerationPoliciesV1(*context, plan.price_state(), 3, target, 0x1234U, 17));
  EXPECT_EQ(repeated, policies);
}

TEST(TargetedRegenerationTest, ProjectsAuthenticGlobalPricesToEachTargetLegalView) {
  board_ir::BoardData board_data = test_support::ValidM1TwoNetBoardData();
  ASSERT_FALSE(board_data.obstacles.empty());
  board_data.obstacles.front().bounds = {
      .min = {.x = 40, .y = 15},
      .max = {.x = 60, .y = 25},
  };
  const board_ir::BoardSnapshot board = test_support::Snapshot(std::move(board_data));
  const geometry_compiler::CompilerProfile compiler_profile =
      test_support::DefaultCompilerProfile({0});
  board_ir::RoutingProfile first_profile = board.data().routing_profile;
  board_ir::RoutingProfile second_profile = board.data().routing_profile;
  second_profile.net = board.data().nets[1].ref;
  const std::array specs = {
      MultiNetRoutingSpec{.routing_profile = first_profile, .start_layer = 0, .goal_layer = 0},
      MultiNetRoutingSpec{.routing_profile = second_profile, .start_layer = 0, .goal_layer = 0},
  };
  const MultiNetWorkload workload = BuiltWorkload(BuildMultiNetWorkload(
      kMultiNetWorkloadSchemaVersion, board, compiler_profile, specs, specs.size()));
  const ResourceCapacityModel capacities = BuiltCapacities(BuildResourceCapacityModel(
      kResourceCapacityModelSchemaVersion, board, workload.nets().front().compiled_board, 0, {}));

  std::vector<CandidatePool> pools;
  pools.reserve(workload.nets().size());
  std::uint64_t query = 1;
  for (const PreparedNetRoutingContext& context : workload.nets()) {
    candidates::GeneratedRouteCandidate draft =
        test_support::CandidateDraft(board, context.compiled_board, context.request, 1, query++);
    candidates::RouteCandidate accepted = test_support::AcceptedCandidate(
        candidates::CandidateAdmissionContext{
            .board = board,
            .compiled_board = context.compiled_board,
            .request = context.request,
        },
        std::move(draft));
    pools.push_back(CandidatePool{
        .net = context.request.net,
        .candidates = {std::make_shared<const candidates::RouteCandidate>(std::move(accepted))},
    });
  }

  const NegotiatedPriceState initial = BuiltState(
      BuildInitialNegotiatedPriceState(kNegotiatedPriceStateSchemaVersion, capacities, workload,
                                       NegotiatedPriceConfig{
                                           .present_step_per_overuse_unit = 3,
                                           .history_step_per_overuse_unit = 5,
                                           .maximum_price_per_resource = 100,
                                           .maximum_iterations = 4,
                                           .maximum_price_records = 1'000,
                                       }));
  OneWorldAllocationRequest request{
      .associations = capacities.associations(),
      .capacities = capacities,
      .prices = BuiltSnapshot(BuildPriceSnapshotForState(capacities, initial)),
      .intrinsic_cost_weight = 1,
      .limits = OneWorldAllocatorLimits{},
      .pools = std::move(pools),
      .workload = &workload,
  };
  const OneWorldAllocation first_world = BuiltWorld(AllocateOneWorld(request));
  ASSERT_EQ(first_world.selected_net_count, 2U);
  const NegotiatedPriceState first_prices =
      BuiltState(UpdateNegotiatedPrices(initial, request, first_world));

  const PreparedNetRoutingContext& target_context = workload.nets().front();
  const PreparedNetRoutingContext& other_context = workload.nets()[1];
  ASSERT_EQ(target_context.request.start_layer, 0U);
  const auto current_outside_target_view =
      std::ranges::find_if(first_prices.prices(), [&](const NegotiatedResourcePrice& price) {
        return price.present_price > 0 &&
               !routing::ResourceExists(target_context.compiled_board, price.resource) &&
               routing::ResourceExists(other_context.compiled_board, price.resource);
      });
  ASSERT_NE(current_outside_target_view, first_prices.prices().end());
  TargetedRegenerationNet target;
  target.net = target_context.request.net;
  target.requested_columns = 1;
  const auto expect_complete_projection = [&](const NegotiatedPriceState& global_prices) {
    const std::vector<routing::NormalizedCandidateGenerationPolicy> policies = BuiltPolicies(
        BuildTargetedRegenerationPoliciesV1(target_context, global_prices, 3, target, 0x1234U, 7));
    ASSERT_EQ(policies.size(), 1U);
    const routing::CandidateGenerationPolicy& policy = policies.front().policy;
    const geometry_compiler::DeterministicCosts costs =
        target_context.compiled_board.profile().costs;
    EXPECT_EQ(policy.orthogonal_step_surcharge, 2U * costs.orthogonal_step);
    EXPECT_EQ(policy.diagonal_step_surcharge, 2U * costs.diagonal_step);
    EXPECT_EQ(policy.bend_surcharge, 2U * costs.bend);
    std::size_t legal_price_count = 0;
    for (const NegotiatedResourcePrice& price : global_prices.prices()) {
      const bool target_legal =
          routing::ResourceExists(target_context.compiled_board, price.resource);
      legal_price_count += target_legal ? 1U : 0U;
      EXPECT_EQ(routing::PolicyPenaltyForResource(policy, price.resource),
                target_legal ? price.total_price : 0U);
    }
    EXPECT_EQ(policy.resource_penalties.size(), legal_price_count);
  };
  expect_complete_projection(first_prices);

  request.prices = BuiltSnapshot(BuildPriceSnapshotForState(capacities, first_prices));
  ASSERT_EQ(request.pools.size(), 2U);
  request.pools[1].candidates.clear();
  const OneWorldAllocation second_world = BuiltWorld(AllocateOneWorld(request));
  ASSERT_EQ(second_world.selected_net_count, 1U);
  const NegotiatedPriceState second_prices =
      BuiltState(UpdateNegotiatedPrices(first_prices, request, second_world));
  const auto history_only_outside_target_view =
      std::ranges::find_if(second_prices.prices(), [&](const NegotiatedResourcePrice& price) {
        return price.present_price == 0 && price.history_price > 0 &&
               !routing::ResourceExists(target_context.compiled_board, price.resource) &&
               routing::ResourceExists(other_context.compiled_board, price.resource);
      });
  ASSERT_NE(history_only_outside_target_view, second_prices.prices().end());
  expect_complete_projection(second_prices);
}

TEST(TargetedRegenerationTest, RejectsPolicyOverflowInvalidResourceAndOrdinalOverflow) {
  PlanningFixture fixture = BuildPlanningFixture(0);
  const TargetedRegenerationPlan plan = BuiltPlan(BuildTargetedRegenerationPlan(
      kTargetedRegenerationPlanSchemaVersion, fixture.initial_price_state, fixture.request,
      fixture.world, *fixture.store, PlanConfig()));
  ASSERT_FALSE(plan.targets().empty());
  TargetedRegenerationNet target = plan.targets().front();
  const PreparedNetRoutingContext* context = fixture.workload.FindNet(target.net);
  ASSERT_NE(context, nullptr);

  const TargetedRegenerationPolicyResult weighted_overflow = BuildTargetedRegenerationPoliciesV1(
      *context, plan.price_state(), std::numeric_limits<std::uint64_t>::max(), target, 1, 0);
  ASSERT_TRUE(std::holds_alternative<TargetedRegenerationError>(weighted_overflow));
  EXPECT_EQ(std::get<TargetedRegenerationError>(weighted_overflow).code,
            TargetedRegenerationErrorCode::kArithmeticOverflow);

  target.requested_columns = 2;
  target.resource_actions.resize(1);
  target.resource_actions.front().resource.lattice_x = std::numeric_limits<std::int64_t>::max();
  const TargetedRegenerationPolicyResult invalid_resource =
      BuildTargetedRegenerationPoliciesV1(*context, plan.price_state(), 1, target, 1, 0);
  ASSERT_TRUE(std::holds_alternative<TargetedRegenerationError>(invalid_resource));
  EXPECT_EQ(std::get<TargetedRegenerationError>(invalid_resource).code,
            TargetedRegenerationErrorCode::kInvalidPricingInput);

  target = plan.targets().front();
  target.requested_columns = 2;
  const TargetedRegenerationPolicyResult ordinal_overflow = BuildTargetedRegenerationPoliciesV1(
      *context, plan.price_state(), 1, target, 1, std::numeric_limits<std::uint32_t>::max());
  ASSERT_TRUE(std::holds_alternative<TargetedRegenerationError>(ordinal_overflow));
  EXPECT_EQ(std::get<TargetedRegenerationError>(ordinal_overflow).code,
            TargetedRegenerationErrorCode::kArithmeticOverflow);
}

TEST(TargetedRegenerationTest, CandidateHeadroomCapsRequestedColumnsExactly) {
  PlanningFixture no_headroom = BuildPlanningFixture(0);
  no_headroom.request.limits.maximum_candidates = no_headroom.request.pools.size();
  const TargetedRegenerationPlan none = BuiltPlan(BuildTargetedRegenerationPlan(
      kTargetedRegenerationPlanSchemaVersion, no_headroom.initial_price_state, no_headroom.request,
      no_headroom.world, *no_headroom.store, PlanConfig()));
  EXPECT_TRUE(none.targets().empty());
  EXPECT_EQ(none.total_requested_columns(), 0U);

  PlanningFixture one_slot = BuildPlanningFixture(0);
  one_slot.request.limits.maximum_candidates = one_slot.request.pools.size() + 1U;
  const TargetedRegenerationPlan one = BuiltPlan(BuildTargetedRegenerationPlan(
      kTargetedRegenerationPlanSchemaVersion, one_slot.initial_price_state, one_slot.request,
      one_slot.world, *one_slot.store, PlanConfig()));
  ASSERT_EQ(one.targets().size(), 1U);
  EXPECT_EQ(one.targets().front().requested_columns, 1U);
  EXPECT_EQ(one.total_requested_columns(), 1U);
}

TEST(TargetedRegenerationTest, ZeroSelectionPlanRetainsExactStoreIdentityLease) {
  PlanningFixture fixture = BuildPlanningFixture(1);
  for (CandidatePool& pool : fixture.request.pools) {
    pool.candidates.clear();
  }
  fixture.world = BuiltWorld(AllocateOneWorld(fixture.request));
  ASSERT_EQ(fixture.world.selected_net_count, 0U);

  const TargetedRegenerationPlan plan = BuiltPlan(BuildTargetedRegenerationPlan(
      kTargetedRegenerationPlanSchemaVersion, fixture.initial_price_state, fixture.request,
      fixture.world, *fixture.store, PlanConfig()));
  EXPECT_TRUE(plan.targets().empty());
  EXPECT_EQ(plan.pinned_candidate_count(), 0U);
  EXPECT_TRUE(plan.has_active_pin_lease());
  EXPECT_TRUE(plan.pin_lease_belongs_to(*fixture.store));

  candidates::CandidateStore other_store(fixture.store->config());
  EXPECT_FALSE(plan.pin_lease_belongs_to(other_store));
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

    const NegotiatedPriceState history_only =
        BuiltState(UpdateNegotiatedPrices(plan.price_state(), next_request, next_world));
    const auto historical =
        std::ranges::find_if(history_only.prices(), [](const NegotiatedResourcePrice& price) {
          return price.present_price == 0 && price.history_price > 0;
        });
    ASSERT_NE(historical, history_only.prices().end());
    TargetedRegenerationNet synthetic_target;
    synthetic_target.net = context.request.net;
    synthetic_target.requested_columns = 1;
    const std::vector<routing::NormalizedCandidateGenerationPolicy> policies = BuiltPolicies(
        BuildTargetedRegenerationPoliciesV1(context, history_only, 1, synthetic_target, 99, 7));
    ASSERT_EQ(policies.size(), 1U);
    EXPECT_EQ(routing::PolicyPenaltyForResource(policies.front().policy, historical->resource),
              historical->history_price);
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
  header.schema_version = kTargetedRegenerationPlanSchemaVersion;
  const std::uint64_t v2_golden =
      internal::ComputeTargetedRegenerationPlanChecksumV2(header, targets);
  EXPECT_EQ(v2_golden, 8958480360901484544ULL);
  ++header.price_state_checksum;
  EXPECT_NE(internal::ComputeTargetedRegenerationPlanChecksumV1(header, targets), golden);
  EXPECT_NE(internal::ComputeTargetedRegenerationPlanChecksumV2(header, targets), v2_golden);
  --header.price_state_checksum;
  ++targets.front().resource_actions.front().conflict_impact;
  EXPECT_NE(internal::ComputeTargetedRegenerationPlanChecksumV1(header, targets), golden);
}

}  // namespace
}  // namespace apgar::allocator
