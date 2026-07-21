#include "apgar/allocator/targeted_regeneration_execution.h"

#include <array>
#include <cstdlib>
#include <memory>
#include <span>
#include <utility>
#include <variant>
#include <vector>

#include "src/allocator/targeted_regeneration_execution_internal.h"
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
  EXPECT_TRUE(std::holds_alternative<TargetedRegenerationPlan>(result));
  if (!std::holds_alternative<TargetedRegenerationPlan>(result)) {
    std::abort();
  }
  return std::get<TargetedRegenerationPlan>(std::move(result));
}

[[nodiscard]] TargetedRegenerationExecution BuiltExecution(
    TargetedRegenerationExecutionResult result) {
  EXPECT_TRUE(std::holds_alternative<TargetedRegenerationExecution>(result))
      << (std::holds_alternative<TargetedRegenerationExecutionError>(result)
              ? std::string(std::get<TargetedRegenerationExecutionError>(result).invariant_id)
              : std::string{});
  if (!std::holds_alternative<TargetedRegenerationExecution>(result)) {
    std::abort();
  }
  return std::get<TargetedRegenerationExecution>(std::move(result));
}

[[nodiscard]] candidates::StoredCandidate Stored(candidates::CandidateStoreAdmissionResult result) {
  EXPECT_TRUE(std::holds_alternative<candidates::StoredCandidate>(result));
  if (!std::holds_alternative<candidates::StoredCandidate>(result)) {
    std::abort();
  }
  return std::get<candidates::StoredCandidate>(std::move(result));
}

struct ExecutionFixture {
  board_ir::BoardSnapshot board;
  MultiNetWorkload workload;
  ResourceCapacityModel capacities;
  std::unique_ptr<candidates::CandidateStore> store;
  NegotiatedPriceState initial_state;
  OneWorldAllocationRequest request;
  OneWorldAllocation world;

  ExecutionFixture(board_ir::BoardSnapshot board_value, MultiNetWorkload workload_value,
                   ResourceCapacityModel capacities_value,
                   std::unique_ptr<candidates::CandidateStore> store_value,
                   NegotiatedPriceState initial_state_value,
                   OneWorldAllocationRequest request_value, OneWorldAllocation world_value)
      : board(std::move(board_value)),
        workload(std::move(workload_value)),
        capacities(std::move(capacities_value)),
        store(std::move(store_value)),
        initial_state(std::move(initial_state_value)),
        request(std::move(request_value)),
        world(std::move(world_value)) {
    request.workload = &workload;
  }
};

[[nodiscard]] candidates::CandidateStoreConfig StoreConfig() {
  return candidates::CandidateStoreConfig{
      .maximum_candidates_per_net = 16,
      .maximum_candidate_bytes_per_net = 16U * 1024U * 1024U,
      .maximum_rejection_records = 64,
      .maximum_pin_lease_items_per_transaction = 16,
      .maximum_expected_pools_per_invocation = 16,
      .maximum_expected_candidates_per_invocation = 64,
  };
}

[[nodiscard]] ExecutionFixture BuildExecutionFixture(
    bool conflicting, std::size_t empty_pool_count = 0,
    candidates::CandidateStoreConfig store_config = StoreConfig()) {
  board_ir::BoardData data = test_support::ValidM1TwoNetBoardData();
  data.obstacles.clear();
  board_ir::BoardSnapshot board = test_support::Snapshot(std::move(data));
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
      kResourceCapacityModelSchemaVersion, board, workload.nets().front().compiled_board, 1, {}));
  auto store = std::make_unique<candidates::CandidateStore>(store_config);

  std::vector<CandidatePool> pools;
  pools.reserve(2);
  for (std::size_t index = 0; index < workload.nets().size(); ++index) {
    const PreparedNetRoutingContext& context = workload.nets()[index];
    if (index >= workload.nets().size() - empty_pool_count) {
      pools.push_back(CandidatePool{.net = context.request.net, .candidates = {}});
      continue;
    }
    routing::PlanarRouteRequest request = context.request;
    candidates::GeneratedRouteCandidate draft;
    if (conflicting && index == 1) {
      const std::array forced = {
          routing::LayerSegment{
              .layer = 0, .centerline = {.start = {.x = 0, .y = 20}, .end = {.x = 20, .y = 20}}},
          routing::LayerSegment{
              .layer = 0, .centerline = {.start = {.x = 20, .y = 20}, .end = {.x = 20, .y = 0}}},
          routing::LayerSegment{
              .layer = 0, .centerline = {.start = {.x = 20, .y = 0}, .end = {.x = 80, .y = 0}}},
          routing::LayerSegment{
              .layer = 0, .centerline = {.start = {.x = 80, .y = 0}, .end = {.x = 80, .y = 20}}},
          routing::LayerSegment{
              .layer = 0, .centerline = {.start = {.x = 80, .y = 20}, .end = {.x = 100, .y = 20}}},
      };
      draft = test_support::CandidateDraftAlongSegments(
          board, context.compiled_board, request, forced,
          candidates::CandidateSchedulingIdentity{.batch_identity = 1, .query_identity = 2});
    } else {
      draft = test_support::CandidateDraft(board, context.compiled_board, request, 1, index + 1);
    }
    const candidates::CandidateAdmissionContext admission_context{
        .board = board, .compiled_board = context.compiled_board, .request = request};
    static_cast<void>(Stored(store->Admit(admission_context, std::move(draft))));
    pools.push_back(CandidatePool{.net = context.request.net,
                                  .candidates = store->Enumerate(context.request.net)});
  }

  const NegotiatedPriceConfig price_config{
      .present_step_per_overuse_unit = 10,
      .history_step_per_overuse_unit = 1,
      .maximum_price_per_resource = 1'000,
      .maximum_iterations = 8,
      .maximum_price_records = 10'000,
  };
  NegotiatedPriceState initial_state = BuiltState(BuildInitialNegotiatedPriceState(
      kNegotiatedPriceStateSchemaVersion, capacities, workload, price_config));
  OneWorldAllocationRequest request{
      .associations = capacities.associations(),
      .capacities = capacities,
      .prices = BuiltSnapshot(BuildPriceSnapshotForState(capacities, initial_state)),
      .intrinsic_cost_weight = 1,
      .limits = OneWorldAllocatorLimits{},
      .pools = std::move(pools),
      .workload = &workload,
  };
  OneWorldAllocation world = BuiltWorld(AllocateOneWorld(request));
  return ExecutionFixture(std::move(board), std::move(workload), std::move(capacities),
                          std::move(store), std::move(initial_state), std::move(request),
                          std::move(world));
}

[[nodiscard]] TargetedRegenerationConfig PlanConfig() {
  return TargetedRegenerationConfig{
      .maximum_target_nets = 1,
      .maximum_columns_per_net = 4,
      .maximum_total_columns = 4,
      .maximum_resource_actions_per_net = 4,
      .maximum_total_resource_actions = 4,
      .maximum_expanded_resource_visits = 10'000,
  };
}

[[nodiscard]] TargetedRegenerationPlan PlanFor(ExecutionFixture& fixture) {
  return BuiltPlan(BuildTargetedRegenerationPlan(kTargetedRegenerationPlanSchemaVersion,
                                                 fixture.initial_state, fixture.request,
                                                 fixture.world, *fixture.store, PlanConfig()));
}

TEST(TargetedRegenerationExecutionTest,
     AuthenticallyGeneratesAdmitsAndSelectsLexicographicProgressDeterministically) {
  ExecutionFixture first_fixture = BuildExecutionFixture(true);
  ASSERT_GT(first_fixture.world.total_overuse_units, 0U);
  TargetedRegenerationPlan first_plan = PlanFor(first_fixture);
  ASSERT_FALSE(first_plan.targets().empty());
  TargetedRegenerationExecution first = BuiltExecution(ExecuteTargetedRegenerationPlanCpu(
      kTargetedRegenerationExecutionSchemaVersion, first_fixture.board, first_fixture.request,
      std::move(first_plan), *first_fixture.store, TargetedRegenerationExecutionConfig{}));

  EXPECT_EQ(first.disposition(), TargetedRegenerationExecutionDisposition::kProgress);
  EXPECT_EQ(first.terminal_reason(),
            TargetedRegenerationTerminalReason::kLexicographicallyImproved);
  EXPECT_LT(first.refreshed_world().total_overuse_units,
            first.baseline_world().total_overuse_units);
  EXPECT_GT(first.counters().admitted_candidates, 0U);
  EXPECT_GT(first.counters().novel_retained_candidates, 0U);
  EXPECT_GT(first.counters().changed_selections, 0U);
  EXPECT_TRUE(first.has_active_successor_lease());
  EXPECT_NE(first.execution_checksum(), 0U);

  ExecutionFixture repeated_fixture = BuildExecutionFixture(true);
  TargetedRegenerationPlan repeated_plan = PlanFor(repeated_fixture);
  TargetedRegenerationExecution repeated = BuiltExecution(ExecuteTargetedRegenerationPlanCpu(
      kTargetedRegenerationExecutionSchemaVersion, repeated_fixture.board, repeated_fixture.request,
      std::move(repeated_plan), *repeated_fixture.store, TargetedRegenerationExecutionConfig{}));
  EXPECT_EQ(repeated.columns(), first.columns());
  EXPECT_EQ(repeated.counters(), first.counters());
  EXPECT_EQ(repeated.refreshed_world().world_checksum, first.refreshed_world().world_checksum);
  EXPECT_EQ(repeated.execution_checksum(), first.execution_checksum());
}

TEST(TargetedRegenerationExecutionTest, ReportsFeasibleNoWorkAndResourceRefinementExplicitly) {
  ExecutionFixture feasible = BuildExecutionFixture(false);
  ASSERT_EQ(feasible.world.total_overuse_units, 0U);
  TargetedRegenerationExecution no_work = BuiltExecution(ExecuteTargetedRegenerationPlanCpu(
      kTargetedRegenerationExecutionSchemaVersion, feasible.board, feasible.request,
      PlanFor(feasible), *feasible.store, TargetedRegenerationExecutionConfig{}));
  EXPECT_EQ(no_work.disposition(), TargetedRegenerationExecutionDisposition::kNoWork);
  EXPECT_EQ(no_work.terminal_reason(), TargetedRegenerationTerminalReason::kFeasible);
  EXPECT_TRUE(no_work.columns().empty());

  ExecutionFixture conflicted = BuildExecutionFixture(true);
  const std::vector<candidates::StoredCandidate> first_pool_before =
      conflicted.store->Enumerate(conflicted.request.pools.front().net);
  TargetedRegenerationExecutionConfig refinement_config;
  refinement_config.maximum_route_queries = 1;
  refinement_config.maximum_route_work_units = 1;
  refinement_config.maximum_policy_resource_entries = 1;
  refinement_config.known_unmapped_exact_conflict_count = 1;
  TargetedRegenerationExecution refinement = BuiltExecution(ExecuteTargetedRegenerationPlanCpu(
      kTargetedRegenerationExecutionSchemaVersion, conflicted.board, conflicted.request,
      PlanFor(conflicted), *conflicted.store, refinement_config));
  EXPECT_EQ(refinement.disposition(),
            TargetedRegenerationExecutionDisposition::kResourceRefinementRequired);
  EXPECT_EQ(refinement.terminal_reason(),
            TargetedRegenerationTerminalReason::kResourceRefinementRequired);
  EXPECT_TRUE(refinement.columns().empty());
  EXPECT_EQ(conflicted.store->Enumerate(conflicted.request.pools.front().net), first_pool_before);
}

TEST(TargetedRegenerationExecutionTest, IncompleteAndAllEmptyWorldsAreNotReportedFeasible) {
  ExecutionFixture incomplete = BuildExecutionFixture(false, 1);
  ASSERT_EQ(incomplete.world.total_overuse_units, 0U);
  ASSERT_EQ(incomplete.world.no_candidate_net_count, 1U);
  TargetedRegenerationExecution incomplete_result =
      BuiltExecution(ExecuteTargetedRegenerationPlanCpu(
          kTargetedRegenerationExecutionSchemaVersion, incomplete.board, incomplete.request,
          PlanFor(incomplete), *incomplete.store, TargetedRegenerationExecutionConfig{}));
  EXPECT_EQ(incomplete_result.disposition(), TargetedRegenerationExecutionDisposition::kNoWork);
  EXPECT_EQ(incomplete_result.terminal_reason(), TargetedRegenerationTerminalReason::kNoTargets);

  ExecutionFixture all_empty = BuildExecutionFixture(false, 2);
  ASSERT_EQ(all_empty.world.selected_net_count, 0U);
  ASSERT_EQ(all_empty.world.no_candidate_net_count, 2U);
  TargetedRegenerationPlan empty_plan = PlanFor(all_empty);
  EXPECT_TRUE(empty_plan.has_active_pin_lease());
  EXPECT_TRUE(empty_plan.pin_lease_belongs_to(*all_empty.store));
  TargetedRegenerationExecution all_empty_result =
      BuiltExecution(ExecuteTargetedRegenerationPlanCpu(
          kTargetedRegenerationExecutionSchemaVersion, all_empty.board, all_empty.request,
          std::move(empty_plan), *all_empty.store, TargetedRegenerationExecutionConfig{}));
  EXPECT_EQ(all_empty_result.terminal_reason(), TargetedRegenerationTerminalReason::kNoTargets);
  EXPECT_EQ(all_empty_result.counters().successor_pinned_candidates, 0U);
  EXPECT_FALSE(all_empty_result.has_active_successor_lease());
}

TEST(TargetedRegenerationExecutionTest, RejectsWrongStoreBoundsAndExactSourcePoolDrift) {
  ExecutionFixture wrong_store_fixture = BuildExecutionFixture(true);
  TargetedRegenerationPlan wrong_store_plan = PlanFor(wrong_store_fixture);
  candidates::CandidateStore wrong_store(StoreConfig());
  const TargetedRegenerationExecutionResult wrong_store_result = ExecuteTargetedRegenerationPlanCpu(
      kTargetedRegenerationExecutionSchemaVersion, wrong_store_fixture.board,
      wrong_store_fixture.request, std::move(wrong_store_plan), wrong_store,
      TargetedRegenerationExecutionConfig{});
  ASSERT_TRUE(std::holds_alternative<TargetedRegenerationExecutionError>(wrong_store_result));
  EXPECT_EQ(std::get<TargetedRegenerationExecutionError>(wrong_store_result).code,
            TargetedRegenerationExecutionErrorCode::kWrongCandidateStore);

  ExecutionFixture bounded_fixture = BuildExecutionFixture(true);
  TargetedRegenerationPlan bounded_plan = PlanFor(bounded_fixture);
  ASSERT_GT(bounded_plan.total_requested_columns(), 1U);
  TargetedRegenerationExecutionConfig bounded_config;
  bounded_config.maximum_route_queries = bounded_plan.total_requested_columns() - 1U;
  const std::vector<candidates::StoredCandidate> bounded_pool_before =
      bounded_fixture.store->Enumerate(bounded_fixture.request.pools.front().net);
  const TargetedRegenerationExecutionResult bounded_result = ExecuteTargetedRegenerationPlanCpu(
      kTargetedRegenerationExecutionSchemaVersion, bounded_fixture.board, bounded_fixture.request,
      std::move(bounded_plan), *bounded_fixture.store, bounded_config);
  ASSERT_TRUE(std::holds_alternative<TargetedRegenerationExecutionError>(bounded_result));
  EXPECT_EQ(std::get<TargetedRegenerationExecutionError>(bounded_result).code,
            TargetedRegenerationExecutionErrorCode::kWorkBoundExceeded);
  EXPECT_EQ(bounded_fixture.store->Enumerate(bounded_fixture.request.pools.front().net),
            bounded_pool_before);

  ExecutionFixture drift_fixture = BuildExecutionFixture(true);
  TargetedRegenerationPlan drift_plan = PlanFor(drift_fixture);
  const PreparedNetRoutingContext& context = drift_fixture.workload.nets().front();
  routing::PlanarRouteRequest alternative_request = context.request;
  alternative_request.candidate_policy.deterministic_seed = 99;
  alternative_request.candidate_policy.candidate_ordinal = 99;
  const std::array detour = {
      routing::LayerSegment{.layer = 0,
                            .centerline = {.start = {.x = 0, .y = 0}, .end = {.x = 10, .y = -10}}},
      routing::LayerSegment{
          .layer = 0, .centerline = {.start = {.x = 10, .y = -10}, .end = {.x = 90, .y = -10}}},
      routing::LayerSegment{
          .layer = 0, .centerline = {.start = {.x = 90, .y = -10}, .end = {.x = 100, .y = 0}}},
  };
  candidates::GeneratedRouteCandidate alternative = test_support::CandidateDraftAlongSegments(
      drift_fixture.board, context.compiled_board, alternative_request, detour,
      candidates::CandidateSchedulingIdentity{.batch_identity = 99, .query_identity = 99});
  const candidates::CandidateAdmissionContext admission_context{
      .board = drift_fixture.board,
      .compiled_board = context.compiled_board,
      .request = alternative_request,
  };
  static_cast<void>(Stored(drift_fixture.store->Admit(admission_context, std::move(alternative))));
  const std::vector<candidates::StoredCandidate> drifted_pool =
      drift_fixture.store->Enumerate(context.request.net);
  const TargetedRegenerationExecutionResult drift_result = ExecuteTargetedRegenerationPlanCpu(
      kTargetedRegenerationExecutionSchemaVersion, drift_fixture.board, drift_fixture.request,
      std::move(drift_plan), *drift_fixture.store, TargetedRegenerationExecutionConfig{});
  ASSERT_TRUE(std::holds_alternative<TargetedRegenerationExecutionError>(drift_result));
  EXPECT_EQ(std::get<TargetedRegenerationExecutionError>(drift_result).code,
            TargetedRegenerationExecutionErrorCode::kCandidateStore);
  EXPECT_EQ(std::get<TargetedRegenerationExecutionError>(drift_result).invariant_id,
            "allocator.targeted_regeneration_execution.store_drift.v1");
  EXPECT_EQ(drift_fixture.store->Enumerate(context.request.net), drifted_pool);
}

TEST(TargetedRegenerationExecutionTest, RejectsAggregateLaterTargetWorkDuringFullPreflight) {
  ExecutionFixture fixture = BuildExecutionFixture(true);
  TargetedRegenerationConfig plan_config = PlanConfig();
  plan_config.maximum_target_nets = 2;
  plan_config.maximum_total_columns = 8;
  plan_config.maximum_total_resource_actions = 8;
  TargetedRegenerationPlan plan = BuiltPlan(
      BuildTargetedRegenerationPlan(kTargetedRegenerationPlanSchemaVersion, fixture.initial_state,
                                    fixture.request, fixture.world, *fixture.store, plan_config));
  ASSERT_EQ(plan.targets().size(), 2U);

  const PreparedNetRoutingContext* first_context =
      fixture.workload.FindNet(plan.targets().front().net);
  ASSERT_NE(first_context, nullptr);
  const std::uint64_t first_target_work =
      plan.targets().front().requested_columns *
      first_context->compiled_board.telemetry().represented_nodes *
      (routing::kNoIncomingDirection + 1U);
  TargetedRegenerationExecutionConfig config;
  config.maximum_route_work_units = first_target_work;
  const std::vector<candidates::CandidateRejection> rejections_before = fixture.store->Rejections();
  const TargetedRegenerationExecutionResult result =
      ExecuteTargetedRegenerationPlanCpu(kTargetedRegenerationExecutionSchemaVersion, fixture.board,
                                         fixture.request, std::move(plan), *fixture.store, config);
  ASSERT_TRUE(std::holds_alternative<TargetedRegenerationExecutionError>(result));
  EXPECT_EQ(std::get<TargetedRegenerationExecutionError>(result).code,
            TargetedRegenerationExecutionErrorCode::kWorkBoundExceeded);
  EXPECT_EQ(fixture.store->Rejections(), rejections_before);
}

TEST(TargetedRegenerationExecutionTest, EnforcesKnownStoreAndPolicyCapsBeforeBulkPreparation) {
  candidates::CandidateStoreConfig pool_bounded_store = StoreConfig();
  pool_bounded_store.maximum_expected_pools_per_invocation = 1;
  ExecutionFixture pool_bounded = BuildExecutionFixture(true, 0, pool_bounded_store);
  TargetedRegenerationExecutionResult pool_result = ExecuteTargetedRegenerationPlanCpu(
      kTargetedRegenerationExecutionSchemaVersion, pool_bounded.board, pool_bounded.request,
      PlanFor(pool_bounded), *pool_bounded.store, TargetedRegenerationExecutionConfig{});
  ASSERT_TRUE(std::holds_alternative<TargetedRegenerationExecutionError>(pool_result));
  EXPECT_EQ(std::get<TargetedRegenerationExecutionError>(pool_result).code,
            TargetedRegenerationExecutionErrorCode::kWorkBoundExceeded);

  candidates::CandidateStoreConfig candidate_bounded_store = StoreConfig();
  candidate_bounded_store.maximum_expected_candidates_per_invocation = 1;
  ExecutionFixture candidate_bounded = BuildExecutionFixture(true, 0, candidate_bounded_store);
  TargetedRegenerationExecutionResult candidate_result = ExecuteTargetedRegenerationPlanCpu(
      kTargetedRegenerationExecutionSchemaVersion, candidate_bounded.board,
      candidate_bounded.request, PlanFor(candidate_bounded), *candidate_bounded.store,
      TargetedRegenerationExecutionConfig{});
  ASSERT_TRUE(std::holds_alternative<TargetedRegenerationExecutionError>(candidate_result));
  EXPECT_EQ(std::get<TargetedRegenerationExecutionError>(candidate_result).code,
            TargetedRegenerationExecutionErrorCode::kWorkBoundExceeded);

  ExecutionFixture exact_fixture = BuildExecutionFixture(true);
  TargetedRegenerationPlan exact_plan = PlanFor(exact_fixture);
  const TargetedRegenerationNet& target = exact_plan.targets().front();
  const PreparedNetRoutingContext* context = exact_fixture.workload.FindNet(target.net);
  ASSERT_NE(context, nullptr);
  TargetedRegenerationPolicyResult policies_result = BuildTargetedRegenerationPoliciesV1(
      *context, exact_plan.price_state(), exact_fixture.request.intrinsic_cost_weight, target, 1,
      0);
  ASSERT_TRUE(std::holds_alternative<std::vector<routing::NormalizedCandidateGenerationPolicy>>(
      policies_result));
  std::uint64_t exact_policy_entries = 0;
  for (const routing::NormalizedCandidateGenerationPolicy& policy :
       std::get<std::vector<routing::NormalizedCandidateGenerationPolicy>>(policies_result)) {
    exact_policy_entries += policy.policy.banned_resources.size();
    exact_policy_entries += policy.policy.resource_penalties.size();
  }
  ASSERT_GT(exact_policy_entries, 1U);
  TargetedRegenerationExecutionConfig exact_config;
  exact_config.maximum_policy_resource_entries = exact_policy_entries;
  EXPECT_TRUE(
      std::holds_alternative<TargetedRegenerationExecution>(ExecuteTargetedRegenerationPlanCpu(
          kTargetedRegenerationExecutionSchemaVersion, exact_fixture.board, exact_fixture.request,
          std::move(exact_plan), *exact_fixture.store, exact_config)));

  ExecutionFixture one_over_fixture = BuildExecutionFixture(true);
  TargetedRegenerationPlan one_over_plan = PlanFor(one_over_fixture);
  TargetedRegenerationExecutionConfig one_over_config;
  one_over_config.maximum_policy_resource_entries = exact_policy_entries - 1U;
  const std::vector<candidates::CandidateRejection> one_over_rejections =
      one_over_fixture.store->Rejections();
  const TargetedRegenerationExecutionResult one_over_result = ExecuteTargetedRegenerationPlanCpu(
      kTargetedRegenerationExecutionSchemaVersion, one_over_fixture.board, one_over_fixture.request,
      std::move(one_over_plan), *one_over_fixture.store, one_over_config);
  ASSERT_TRUE(std::holds_alternative<TargetedRegenerationExecutionError>(one_over_result));
  EXPECT_EQ(std::get<TargetedRegenerationExecutionError>(one_over_result).code,
            TargetedRegenerationExecutionErrorCode::kWorkBoundExceeded);
  EXPECT_EQ(one_over_fixture.store->Rejections(), one_over_rejections);
}

TEST(TargetedRegenerationExecutionTest, ReplacesSourcePolicyWithoutCopyingDiscardedPolicyBulk) {
  routing::PlanarRouteRequest source;
  source.net = {.id = 11, .generation = 13};
  source.start = {.x = 17, .y = 19};
  source.goal = {.x = 23, .y = 29};
  source.start_layer = 31;
  source.goal_layer = 37;
  source.candidate_policy.deterministic_seed = 41;
  source.candidate_policy.banned_resources = {
      {.layer = 0, .lattice_x = 43, .lattice_y = 47},
      {.layer = 0, .lattice_x = 53, .lattice_y = 59},
  };
  routing::CandidateGenerationPolicy replacement;
  replacement.deterministic_seed = 61;
  replacement.resource_penalties = {
      {.resource = {.layer = 0, .lattice_x = 67, .lattice_y = 71}, .additional_cost = 73}};

  const routing::PlanarRouteRequest request =
      internal::BuildTargetedRegenerationRouteRequestV1(source, replacement);
  EXPECT_EQ(request.net, source.net);
  EXPECT_EQ(request.start, source.start);
  EXPECT_EQ(request.goal, source.goal);
  EXPECT_EQ(request.start_layer, source.start_layer);
  EXPECT_EQ(request.goal_layer, source.goal_layer);
  EXPECT_EQ(request.candidate_policy, replacement);
  EXPECT_EQ(source.candidate_policy.banned_resources.size(), 2U);
  EXPECT_NE(request.candidate_policy, source.candidate_policy);
}

TEST(TargetedRegenerationExecutionTest, ChecksumRepresentationIsGoldenAndFieldSensitive) {
  internal::TargetedRegenerationExecutionChecksumHeaderV1 header{
      .schema_version = 2,
      .plan_checksum = 3,
      .config =
          TargetedRegenerationExecutionConfig{
              .maximum_route_queries = 5,
              .maximum_route_work_units = 7,
              .maximum_policy_resource_entries = 11,
              .known_unmapped_exact_conflict_count = 13,
          },
      .refreshed_request_manifest_checksum = 17,
      .refreshed_candidate_pool_manifest_checksum = 19,
      .baseline_world_checksum = 23,
      .refreshed_world_checksum = 29,
      .disposition = TargetedRegenerationExecutionDisposition::kStalled,
      .terminal_reason = TargetedRegenerationTerminalReason::kNoSelectionChange,
      .counters =
          TargetedRegenerationExecutionCounters{
              .requested_columns = 31,
              .successful_routes = 37,
              .built_candidates = 41,
              .admitted_candidates = 43,
              .duplicate_candidates = 47,
              .rejected_columns = 53,
              .novel_retained_candidates = 59,
              .changed_selections = 61,
              .successor_pinned_candidates = 67,
          },
  };
  std::vector<TargetedRegenerationColumnRecord> columns = {
      TargetedRegenerationColumnRecord{
          .net = {.id = 71, .generation = 73},
          .column_index = 79,
          .policy_identity = 83,
          .batch_identity = 89,
          .query_identity = 97,
          .outcome = TargetedRegenerationColumnOutcome::kAdmissionRejected,
          .candidate_id = candidates::CandidateId{.high = 101, .low = 103},
          .candidate_payload_checksum = 107,
          .rejection_code = candidates::CandidateRejectionCode::kBudgetExhausted,
      },
  };
  const std::uint64_t golden =
      internal::ComputeTargetedRegenerationExecutionChecksumV1(header, columns);
  EXPECT_EQ(golden, 16407228703171655649ULL);
  ++header.counters.admitted_candidates;
  EXPECT_NE(internal::ComputeTargetedRegenerationExecutionChecksumV1(header, columns), golden);
  header.counters.admitted_candidates--;
  columns.front().candidate_id->low++;
  EXPECT_NE(internal::ComputeTargetedRegenerationExecutionChecksumV1(header, columns), golden);
}

}  // namespace
}  // namespace apgar::allocator
