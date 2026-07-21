#include "apgar/allocator/targeted_regeneration_execution.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <memory>
#include <span>
#include <utility>
#include <variant>
#include <vector>

#include "src/allocator/targeted_regeneration_execution_internal.h"
#include "src/allocator/targeted_regeneration_internal.h"
#include "src/candidates/candidate_store_internal.h"
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
      .maximum_admission_input_bytes_per_transaction = 1024ULL * 1024ULL * 1024ULL,
      .maximum_admission_work_units_per_transaction = 100'000'000'000ULL,
      .maximum_pin_lease_items_per_transaction = 16,
      .maximum_expected_pools_per_invocation = 16,
      .maximum_expected_candidates_per_invocation = 64,
  };
}

[[nodiscard]] ExecutionFixture BuildExecutionFixture(
    bool conflicting, std::size_t empty_pool_count = 0,
    candidates::CandidateStoreConfig store_config = StoreConfig(),
    bool asymmetric_second_net = false) {
  board_ir::BoardData data = test_support::ValidM1TwoNetBoardData();
  data.obstacles.clear();
  if (asymmetric_second_net) {
    data.obstacles.push_back(board_ir::Obstacle{
        .ref = {.id = 31, .generation = 0},
        .layer = 0,
        .bounds = {.min = {.x = 40, .y = 15}, .max = {.x = 60, .y = 25}},
        .owner_net = data.nets[0].ref,
        .provenance = "asymmetric-second-net-blocker",
    });
  }
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

[[nodiscard]] TargetedRegenerationPlan TwoTargetPlanFor(ExecutionFixture& fixture) {
  TargetedRegenerationConfig config = PlanConfig();
  config.maximum_target_nets = 2;
  config.maximum_total_columns = 8;
  config.maximum_total_resource_actions = 8;
  return BuiltPlan(BuildTargetedRegenerationPlan(kTargetedRegenerationPlanSchemaVersion,
                                                 fixture.initial_state, fixture.request,
                                                 fixture.world, *fixture.store, config));
}

struct ExecutionPreflightWitness {
  std::uint64_t source_candidates = 0;
  std::uint64_t route_queries = 0;
  std::uint64_t policy_projection_visits = 0;
  std::uint64_t aggregate_policy_entries = 0;
  std::uint64_t maximum_policy_entries_per_candidate = 0;
  std::uint64_t draft_bytes = 0;
  std::uint64_t generated_bytes = 0;
  std::uint64_t admission_input_bytes = 0;
  std::uint64_t admission_work_units = 0;
  std::uint64_t rejection_bytes = 0;
  std::uint64_t transient_result_bytes = 0;
};

[[nodiscard]] ExecutionPreflightWitness PreflightWitness(
    const ExecutionFixture& fixture, const TargetedRegenerationPlan& plan,
    const TargetedRegenerationExecutionConfig& config) {
  ExecutionPreflightWitness witness{.route_queries = plan.total_requested_columns()};
  for (const CandidatePool& pool : fixture.request.pools) {
    witness.source_candidates += pool.candidates.size();
  }
  witness.policy_projection_visits = internal::ComputeTargetedRegenerationPolicyProjectionVisitsV2(
                                         plan.targets().size(), plan.price_state().prices().size())
                                         .value_or(0);
  for (const TargetedRegenerationNet& target : plan.targets()) {
    const PreparedNetRoutingContext* context = fixture.workload.FindNet(target.net);
    EXPECT_NE(context, nullptr);
    if (context == nullptr) {
      std::abort();
    }
    const internal::TargetedRegenerationPolicyEntryProjectionV1 projection =
        internal::ProjectTargetedRegenerationPolicyEntriesV1(context->compiled_board,
                                                             plan.price_state().prices(), target);
    witness.aggregate_policy_entries += projection.aggregate_entry_count;
    witness.maximum_policy_entries_per_candidate =
        std::max(witness.maximum_policy_entries_per_candidate,
                 projection.target_legal_price_count + (target.requested_columns > 1U ? 1U : 0U));
  }
  const auto draft = internal::ComputeTargetedRegenerationMaximumDraftBytesV2(
      config.route_limits.maximum_reconstruction_states,
      witness.maximum_policy_entries_per_candidate);
  EXPECT_TRUE(draft.has_value());
  witness.draft_bytes = draft.value_or(0);
  const auto generated = internal::ComputeTargetedRegenerationGeneratedBytesV2(
      witness.route_queries, witness.draft_bytes);
  EXPECT_TRUE(generated.has_value());
  witness.generated_bytes = generated.value_or(0);
  const auto input = internal::ComputeTargetedRegenerationAdmissionInputBytesV2(
      witness.route_queries, witness.draft_bytes, witness.aggregate_policy_entries);
  EXPECT_TRUE(input.has_value());
  witness.admission_input_bytes = input.value_or(0);
  const auto work = internal::ComputeTargetedRegenerationAdmissionWorkV2(
      witness.route_queries, config.route_limits.maximum_reconstruction_states,
      witness.aggregate_policy_entries, fixture.board.data().obstacles.size(),
      fixture.board.data().terminals.size());
  EXPECT_TRUE(work.has_value());
  witness.admission_work_units = work.value_or(0);
  witness.rejection_bytes =
      internal::ComputeTargetedRegenerationRejectionBytesV2(witness.route_queries).value_or(0);
  witness.transient_result_bytes =
      internal::ComputeTargetedRegenerationTransientResultBytesV2(witness.route_queries)
          .value_or(0);
  return witness;
}

[[nodiscard]] TargetedRegenerationExecutionConfig ExactConfigFor(
    const ExecutionPreflightWitness& witness) {
  TargetedRegenerationExecutionConfig config;
  config.maximum_route_queries = witness.route_queries;
  config.maximum_total_route_work_units =
      witness.route_queries * config.route_limits.maximum_work_units;
  config.maximum_policy_projection_visits = witness.policy_projection_visits;
  config.maximum_candidate_draft_bytes = witness.draft_bytes;
  config.maximum_generated_candidate_bytes = witness.generated_bytes;
  config.maximum_rejection_bytes = witness.rejection_bytes;
  config.maximum_transient_result_bytes = witness.transient_result_bytes;
  return config;
}

void ExpectReachedColumnStageCounters(
    const TargetedRegenerationExecutionError::FailedExecutionObservation& observation) {
  std::uint64_t successful_routes = 0;
  std::uint64_t built_candidates = 0;
  std::uint64_t rejected_columns = 0;
  std::uint64_t ambiguous_rejection_evidence = 0;
  for (const TargetedRegenerationColumnRecord& column : observation.columns) {
    switch (column.outcome) {
      case TargetedRegenerationColumnOutcome::kQueryInFlight:
        break;
      case TargetedRegenerationColumnOutcome::kBuildInFlight:
        ++successful_routes;
        break;
      case TargetedRegenerationColumnOutcome::kGeneratedPendingPublication:
      case TargetedRegenerationColumnOutcome::kPublicationCommittedOutcomeCorrelationPending:
      case TargetedRegenerationColumnOutcome::kAdmitted:
        ++successful_routes;
        ++built_candidates;
        break;
      case TargetedRegenerationColumnOutcome::kDuplicate:
      case TargetedRegenerationColumnOutcome::kAdmissionRejected:
        ++successful_routes;
        ++built_candidates;
        ++rejected_columns;
        break;
      case TargetedRegenerationColumnOutcome::kBuildRejected:
        ++successful_routes;
        ++rejected_columns;
        break;
      case TargetedRegenerationColumnOutcome::kRouteDisconnected:
      case TargetedRegenerationColumnOutcome::kRouteUnsupported:
        ++rejected_columns;
        break;
      case TargetedRegenerationColumnOutcome::kRejectionEvidenceInFlight:
        ++ambiguous_rejection_evidence;
        break;
    }
  }
  EXPECT_GE(observation.counters.successful_routes, successful_routes);
  EXPECT_LE(observation.counters.successful_routes,
            successful_routes + ambiguous_rejection_evidence);
  EXPECT_GE(observation.counters.built_candidates, built_candidates);
  EXPECT_LE(observation.counters.built_candidates, built_candidates + ambiguous_rejection_evidence);
  EXPECT_EQ(observation.counters.rejected_columns, rejected_columns);
}

void ExpectEveryFinalRejectionHasCompleteEvidence(
    const TargetedRegenerationExecutionError::FailedExecutionObservation& observation) {
  for (const TargetedRegenerationColumnRecord& column : observation.columns) {
    const bool final_rejection =
        column.outcome == TargetedRegenerationColumnOutcome::kDuplicate ||
        column.outcome == TargetedRegenerationColumnOutcome::kRouteDisconnected ||
        column.outcome == TargetedRegenerationColumnOutcome::kRouteUnsupported ||
        column.outcome == TargetedRegenerationColumnOutcome::kBuildRejected ||
        column.outcome == TargetedRegenerationColumnOutcome::kAdmissionRejected;
    if (final_rejection) {
      ASSERT_TRUE(column.rejection.has_value());
      EXPECT_EQ(column.rejection_code, column.rejection->code);
    }
  }
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
  EXPECT_EQ(first.counters().route_queries, first.columns().size());
  std::uint64_t route_work = 0;
  std::uint64_t generated_bytes = 0;
  std::uint64_t peak_records = 0;
  std::uint64_t peak_queue = 0;
  std::uint64_t rejection_bytes = 0;
  std::uint64_t transient_result_bytes = 0;
  for (const TargetedRegenerationColumnRecord& column : first.columns()) {
    ASSERT_TRUE(column.route_telemetry.has_value());
    route_work += column.route_telemetry->work_units;
    generated_bytes += column.candidate_draft_logical_bytes;
    peak_records = std::max(peak_records, column.route_telemetry->peak_record_count);
    peak_queue = std::max(peak_queue, column.route_telemetry->peak_queue_size);
    const auto column_bytes = internal::ComputeTargetedRegenerationColumnLogicalBytesV2(column);
    ASSERT_TRUE(column_bytes.has_value());
    transient_result_bytes += *column_bytes;
    if (column.rejection.has_value()) {
      EXPECT_EQ(column.rejection_code, column.rejection->code);
      rejection_bytes += column.rejection->logical_bytes;
      transient_result_bytes += column.rejection->logical_bytes;
    } else {
      EXPECT_FALSE(column.rejection_code.has_value());
    }
  }
  EXPECT_EQ(first.counters().route_work_units, route_work);
  EXPECT_EQ(first.counters().generated_candidate_bytes, generated_bytes);
  EXPECT_EQ(first.counters().peak_route_record_count, peak_records);
  EXPECT_EQ(first.counters().peak_route_queue_size, peak_queue);
  EXPECT_EQ(first.counters().policy_projection_visits,
            first.plan().targets().size() * first.plan().price_state().prices().size() *
                kTargetedRegenerationPolicyProjectionPassesV2);
  EXPECT_EQ(first.counters().rejection_record_bytes, rejection_bytes);
  EXPECT_EQ(first.counters().transient_result_bytes, transient_result_bytes);
  EXPECT_EQ(first.store_config(), first_fixture.store->config());
  EXPECT_EQ(first_fixture.store->RejectionCount(), first_fixture.store->Rejections().size());
  EXPECT_TRUE(first.has_active_successor_lease());
  std::uint64_t expected_successor_pins = 0;
  for (const NetSelection& selection : first.refreshed_world().selections) {
    if (selection.status != NetSelectionStatus::kSelected) {
      continue;
    }
    ASSERT_TRUE(selection.candidate_id.has_value());
    ++expected_successor_pins;
    EXPECT_TRUE(first_fixture.store->IsPinned(*selection.candidate_id));
  }
  EXPECT_EQ(first.counters().successor_pinned_candidates, expected_successor_pins);
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
  refinement_config.maximum_total_route_work_units = 1;
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

TEST(TargetedRegenerationExecutionTest, NoTargetPlanIgnoresHypotheticalQueryAndDraftEnvelopes) {
  candidates::CandidateStoreConfig tiny_store = StoreConfig();
  tiny_store.maximum_rejection_records = 1;
  tiny_store.maximum_rejection_items_per_transaction = 1;
  tiny_store.maximum_admission_items_per_transaction = 1;
  tiny_store.maximum_admission_input_bytes_per_transaction = 1;
  tiny_store.maximum_admission_work_units_per_transaction = 1;
  ExecutionFixture fixture = BuildExecutionFixture(false, 2, tiny_store);
  TargetedRegenerationPlan plan = PlanFor(fixture);
  ASSERT_TRUE(plan.targets().empty());
  const std::vector<candidates::CandidateRejection> rejections_before = fixture.store->Rejections();
  const candidates::CandidateStoreTelemetry telemetry_before = fixture.store->telemetry();
  TargetedRegenerationExecutionConfig config;
  config.maximum_route_queries = 1;
  config.route_limits = routing::CpuRouteWorkLimits{
      .maximum_work_units = 1,
      .maximum_record_count = 1,
      .maximum_queue_size = 1,
      .maximum_reconstruction_states = 1,
  };
  config.maximum_total_route_work_units = 1;
  config.maximum_policy_projection_visits = 1;
  config.maximum_policy_resource_entries = 1;
  config.maximum_candidate_draft_bytes = 1;
  config.maximum_generated_candidate_bytes = 1;
  config.maximum_rejection_bytes = 1;
  config.maximum_transient_result_bytes = 1;
  TargetedRegenerationExecution execution = BuiltExecution(
      ExecuteTargetedRegenerationPlanCpu(kTargetedRegenerationExecutionSchemaVersion, fixture.board,
                                         fixture.request, std::move(plan), *fixture.store, config));
  EXPECT_EQ(execution.terminal_reason(), TargetedRegenerationTerminalReason::kNoTargets);
  EXPECT_TRUE(execution.columns().empty());
  EXPECT_EQ(execution.counters(), TargetedRegenerationExecutionCounters{});
  EXPECT_EQ(fixture.store->Rejections(), rejections_before);
  EXPECT_EQ(fixture.store->telemetry(), telemetry_before);

  candidates::CandidateStoreConfig populated_store = StoreConfig();
  populated_store.maximum_rejection_records = 1;
  ExecutionFixture populated = BuildExecutionFixture(false, 0, populated_store);
  TargetedRegenerationPlan populated_plan = PlanFor(populated);
  ASSERT_TRUE(populated_plan.targets().empty());
  ASSERT_GT(populated_plan.source_candidate_count(), populated_store.maximum_rejection_records);
  const std::vector<candidates::CandidateRejection> populated_rejections_before =
      populated.store->Rejections();
  const candidates::CandidateStoreTelemetry populated_telemetry_before =
      populated.store->telemetry();
  TargetedRegenerationExecution populated_execution =
      BuiltExecution(ExecuteTargetedRegenerationPlanCpu(
          kTargetedRegenerationExecutionSchemaVersion, populated.board, populated.request,
          std::move(populated_plan), *populated.store, config));
  EXPECT_EQ(populated_execution.terminal_reason(), TargetedRegenerationTerminalReason::kFeasible);
  EXPECT_TRUE(populated_execution.columns().empty());
  EXPECT_EQ(populated_execution.counters().route_queries, 0U);
  EXPECT_EQ(populated_execution.counters().rejected_columns, 0U);
  EXPECT_EQ(populated_execution.counters().successor_pinned_candidates,
            populated_execution.refreshed_world().selected_net_count);
  EXPECT_EQ(populated.store->Rejections(), populated_rejections_before);
  EXPECT_EQ(populated.store->telemetry(), populated_telemetry_before);
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

  const std::uint64_t first_target_work =
      plan.targets().front().requested_columns *
      TargetedRegenerationExecutionConfig{}.route_limits.maximum_work_units;
  TargetedRegenerationExecutionConfig config;
  config.maximum_total_route_work_units = first_target_work;
  const std::vector<candidates::CandidateRejection> rejections_before = fixture.store->Rejections();
  const TargetedRegenerationExecutionResult result =
      ExecuteTargetedRegenerationPlanCpu(kTargetedRegenerationExecutionSchemaVersion, fixture.board,
                                         fixture.request, std::move(plan), *fixture.store, config);
  ASSERT_TRUE(std::holds_alternative<TargetedRegenerationExecutionError>(result));
  EXPECT_EQ(std::get<TargetedRegenerationExecutionError>(result).code,
            TargetedRegenerationExecutionErrorCode::kWorkBoundExceeded);
  EXPECT_EQ(fixture.store->Rejections(), rejections_before);
}

TEST(TargetedRegenerationExecutionTest,
     AcceptsExactAggregateEnvelopesAndRejectsOneUnderBeforeCpuRouting) {
  ExecutionFixture witness_fixture = BuildExecutionFixture(true);
  TargetedRegenerationPlan witness_plan = PlanFor(witness_fixture);
  const ExecutionPreflightWitness witness =
      PreflightWitness(witness_fixture, witness_plan, TargetedRegenerationExecutionConfig{});
  ASSERT_GT(witness.source_candidates, 1U);
  const TargetedRegenerationExecutionConfig exact_config = ExactConfigFor(witness);

  ExecutionFixture exact_fixture = BuildExecutionFixture(true);
  TargetedRegenerationExecution exact = BuiltExecution(ExecuteTargetedRegenerationPlanCpu(
      kTargetedRegenerationExecutionSchemaVersion, exact_fixture.board, exact_fixture.request,
      PlanFor(exact_fixture), *exact_fixture.store, exact_config));
  EXPECT_EQ(internal::TargetedRegenerationRouteQueriesForTesting(), witness.route_queries);
  EXPECT_LE(exact.counters().route_work_units, exact_config.maximum_total_route_work_units);
  EXPECT_LE(exact.counters().generated_candidate_bytes,
            exact_config.maximum_generated_candidate_bytes);

  const auto expect_config_preflight_failure = [&](auto mutate_config) {
    ExecutionFixture fixture = BuildExecutionFixture(true);
    TargetedRegenerationExecutionConfig config = exact_config;
    mutate_config(config);
    const std::vector<candidates::StoredCandidate> pool_before =
        fixture.store->Enumerate(fixture.request.pools.front().net);
    const std::vector<candidates::CandidateRejection> rejections_before =
        fixture.store->Rejections();
    const candidates::CandidateStoreTelemetry telemetry_before = fixture.store->telemetry();
    TargetedRegenerationExecutionResult result = ExecuteTargetedRegenerationPlanCpu(
        kTargetedRegenerationExecutionSchemaVersion, fixture.board, fixture.request,
        PlanFor(fixture), *fixture.store, config);
    ASSERT_TRUE(std::holds_alternative<TargetedRegenerationExecutionError>(result));
    EXPECT_EQ(std::get<TargetedRegenerationExecutionError>(result).code,
              TargetedRegenerationExecutionErrorCode::kWorkBoundExceeded);
    EXPECT_FALSE(std::get<TargetedRegenerationExecutionError>(result).failed_execution.has_value());
    EXPECT_EQ(internal::TargetedRegenerationRouteQueriesForTesting(), 0U);
    EXPECT_EQ(fixture.store->Enumerate(fixture.request.pools.front().net), pool_before);
    EXPECT_EQ(fixture.store->Rejections(), rejections_before);
    EXPECT_EQ(fixture.store->telemetry(), telemetry_before);
  };
  expect_config_preflight_failure(
      [](TargetedRegenerationExecutionConfig& config) { --config.maximum_total_route_work_units; });
  expect_config_preflight_failure([](TargetedRegenerationExecutionConfig& config) {
    --config.maximum_policy_projection_visits;
  });
  expect_config_preflight_failure(
      [](TargetedRegenerationExecutionConfig& config) { --config.maximum_candidate_draft_bytes; });
  expect_config_preflight_failure([](TargetedRegenerationExecutionConfig& config) {
    --config.maximum_generated_candidate_bytes;
  });
  expect_config_preflight_failure(
      [](TargetedRegenerationExecutionConfig& config) { --config.maximum_rejection_bytes; });
  expect_config_preflight_failure(
      [](TargetedRegenerationExecutionConfig& config) { --config.maximum_transient_result_bytes; });

  const auto expect_store_preflight = [&](bool one_under_input, bool one_under_work,
                                          bool one_under_rejections) {
    candidates::CandidateStoreConfig store_config = StoreConfig();
    store_config.maximum_admission_input_bytes_per_transaction = witness.admission_input_bytes;
    store_config.maximum_admission_work_units_per_transaction = witness.admission_work_units;
    // Any source incumbent can acquire an eviction diagnostic when the generated
    // batch is published, so equality includes both the complete source pool and Q.
    store_config.maximum_rejection_records = witness.source_candidates + witness.route_queries;
    if (one_under_input) {
      --store_config.maximum_admission_input_bytes_per_transaction;
    }
    if (one_under_work) {
      --store_config.maximum_admission_work_units_per_transaction;
    }
    if (one_under_rejections) {
      --store_config.maximum_rejection_records;
    }
    ExecutionFixture fixture = BuildExecutionFixture(true, 0, store_config);
    const std::vector<candidates::StoredCandidate> pool_before =
        fixture.store->Enumerate(fixture.request.pools.front().net);
    const std::vector<candidates::CandidateRejection> rejections_before =
        fixture.store->Rejections();
    const candidates::CandidateStoreTelemetry telemetry_before = fixture.store->telemetry();
    TargetedRegenerationExecutionResult result = ExecuteTargetedRegenerationPlanCpu(
        kTargetedRegenerationExecutionSchemaVersion, fixture.board, fixture.request,
        PlanFor(fixture), *fixture.store, exact_config);
    if (!one_under_input && !one_under_work && !one_under_rejections) {
      EXPECT_TRUE(std::holds_alternative<TargetedRegenerationExecution>(result));
      return;
    }
    ASSERT_TRUE(std::holds_alternative<TargetedRegenerationExecutionError>(result));
    EXPECT_EQ(std::get<TargetedRegenerationExecutionError>(result).code,
              TargetedRegenerationExecutionErrorCode::kWorkBoundExceeded);
    EXPECT_FALSE(std::get<TargetedRegenerationExecutionError>(result).failed_execution.has_value());
    EXPECT_EQ(internal::TargetedRegenerationRouteQueriesForTesting(), 0U);
    EXPECT_EQ(fixture.store->Enumerate(fixture.request.pools.front().net), pool_before);
    EXPECT_EQ(fixture.store->Rejections(), rejections_before);
    EXPECT_EQ(fixture.store->telemetry(), telemetry_before);
  };
  expect_store_preflight(false, false, false);
  expect_store_preflight(true, false, false);
  expect_store_preflight(false, true, false);
  expect_store_preflight(false, false, true);
}

TEST(TargetedRegenerationExecutionTest,
     PreflightsWorstCaseRefreshedOneWorldBoundsBeforeCpuRouting) {
  ExecutionFixture witness_fixture = BuildExecutionFixture(true);
  TargetedRegenerationPlan witness_plan = PlanFor(witness_fixture);
  const std::uint64_t route_queries = witness_plan.total_requested_columns();
  ASSERT_GT(route_queries, 0U);
  std::uint64_t source_candidates = 0;
  std::uint64_t source_expanded_resource_uses = 0;
  for (const CandidatePool& pool : witness_fixture.request.pools) {
    source_candidates += pool.candidates.size();
    for (const candidates::StoredCandidate& candidate : pool.candidates) {
      ASSERT_NE(candidate, nullptr);
      for (const candidates::PhysicalEdgeSpan& span : candidate->data().resources) {
        source_expanded_resource_uses += span.edge_count;
      }
    }
  }
  const TargetedRegenerationExecutionConfig config;
  const std::uint64_t exact_candidates = source_candidates + route_queries;
  const std::uint64_t exact_expanded_resource_uses =
      source_expanded_resource_uses +
      route_queries * config.route_limits.maximum_reconstruction_states;
  const std::uint64_t exact_resource_records =
      witness_fixture.request.capacities.overrides().size() +
      witness_plan.price_state().prices().size();

  const auto configure_limits = [&](ExecutionFixture& fixture) {
    fixture.request.limits.maximum_nets = fixture.request.pools.size();
    fixture.request.limits.maximum_candidates = exact_candidates;
    fixture.request.limits.maximum_resource_records = exact_resource_records;
    fixture.request.limits.maximum_expanded_resource_uses = exact_expanded_resource_uses;
  };

  ExecutionFixture equality = BuildExecutionFixture(true);
  configure_limits(equality);
  TargetedRegenerationExecution accepted = BuiltExecution(ExecuteTargetedRegenerationPlanCpu(
      kTargetedRegenerationExecutionSchemaVersion, equality.board, equality.request,
      PlanFor(equality), *equality.store, config));
  EXPECT_EQ(accepted.counters().route_queries, route_queries);

  // Planning already clamps Q to candidate headroom, so a valid plan cannot
  // exceed the refreshed candidate-count formula. Expanded resource uses are
  // execution-specific and supply the authentic one-under boundary.
  ExecutionFixture one_under = BuildExecutionFixture(true);
  configure_limits(one_under);
  --one_under.request.limits.maximum_expanded_resource_uses;
  TargetedRegenerationPlan one_under_plan = PlanFor(one_under);
  ASSERT_EQ(one_under_plan.total_requested_columns(), route_queries);
  std::vector<std::vector<candidates::StoredCandidate>> pools_before;
  for (const CandidatePool& pool : one_under.request.pools) {
    pools_before.push_back(one_under.store->Enumerate(pool.net));
  }
  const auto rejections_before = one_under.store->Rejections();
  const auto telemetry_before = one_under.store->telemetry();
  TargetedRegenerationExecutionResult result = ExecuteTargetedRegenerationPlanCpu(
      kTargetedRegenerationExecutionSchemaVersion, one_under.board, one_under.request,
      std::move(one_under_plan), *one_under.store, config);
  ASSERT_TRUE(std::holds_alternative<TargetedRegenerationExecutionError>(result));
  const TargetedRegenerationExecutionError& error =
      std::get<TargetedRegenerationExecutionError>(result);
  EXPECT_EQ(error.code, TargetedRegenerationExecutionErrorCode::kWorkBoundExceeded);
  EXPECT_EQ(error.invariant_id,
            "allocator.targeted_regeneration_execution.refreshed_expanded_resource_budget.v2");
  EXPECT_FALSE(error.failed_execution.has_value());
  EXPECT_EQ(internal::TargetedRegenerationRouteQueriesForTesting(), 0U);
  for (std::size_t index = 0; index < one_under.request.pools.size(); ++index) {
    EXPECT_EQ(one_under.store->Enumerate(one_under.request.pools[index].net), pools_before[index]);
  }
  EXPECT_EQ(one_under.store->Rejections(), rejections_before);
  EXPECT_EQ(one_under.store->telemetry(), telemetry_before);
}

TEST(TargetedRegenerationExecutionTest,
     SourceFootprintPreflightRejectsCountDriftAndStopsAtExpandedUseEnvelope) {
  ExecutionFixture count_drift = BuildExecutionFixture(true);
  TargetedRegenerationPlan count_drift_plan = PlanFor(count_drift);
  ASSERT_FALSE(count_drift.request.pools.front().candidates.empty());
  count_drift.request.pools.front().candidates.push_back(
      count_drift.request.pools.front().candidates.front());
  TargetedRegenerationExecutionResult count_drift_result = ExecuteTargetedRegenerationPlanCpu(
      kTargetedRegenerationExecutionSchemaVersion, count_drift.board, count_drift.request,
      std::move(count_drift_plan), *count_drift.store, TargetedRegenerationExecutionConfig{});
  ASSERT_TRUE(std::holds_alternative<TargetedRegenerationExecutionError>(count_drift_result));
  const TargetedRegenerationExecutionError& count_error =
      std::get<TargetedRegenerationExecutionError>(count_drift_result);
  EXPECT_EQ(count_error.code, TargetedRegenerationExecutionErrorCode::kSourceRequestDrift);
  EXPECT_EQ(count_error.invariant_id,
            "allocator.targeted_regeneration_execution.source_candidate_count.v2");
  EXPECT_EQ(internal::TargetedRegenerationSourceResourceSpanVisitsForTesting(), 0U);
  EXPECT_EQ(internal::TargetedRegenerationRouteQueriesForTesting(), 0U);

  ExecutionFixture cutoff = BuildExecutionFixture(true);
  TargetedRegenerationPlan cutoff_plan = PlanFor(cutoff);
  ASSERT_EQ(cutoff.request.pools.size(), 2U);
  ASSERT_EQ(cutoff.request.pools.front().candidates.size(), 1U);
  ASSERT_EQ(cutoff.request.pools.back().candidates.size(), 1U);
  const candidates::StoredCandidate repeated = cutoff.request.pools.front().candidates.front();
  ASSERT_NE(repeated, nullptr);
  ASSERT_FALSE(repeated->data().resources.empty());
  cutoff.request.pools.back().candidates.front() = repeated;
  const TargetedRegenerationExecutionConfig config;
  const std::uint64_t generated_envelope =
      cutoff_plan.total_requested_columns() * config.route_limits.maximum_reconstruction_states;
  ASSERT_GT(repeated->data().resources.front().edge_count, 0U);
  cutoff.request.limits.maximum_expanded_resource_uses =
      generated_envelope + repeated->data().resources.front().edge_count - 1U;
  TargetedRegenerationExecutionResult cutoff_result = ExecuteTargetedRegenerationPlanCpu(
      kTargetedRegenerationExecutionSchemaVersion, cutoff.board, cutoff.request,
      std::move(cutoff_plan), *cutoff.store, config);
  ASSERT_TRUE(std::holds_alternative<TargetedRegenerationExecutionError>(cutoff_result));
  const TargetedRegenerationExecutionError& cutoff_error =
      std::get<TargetedRegenerationExecutionError>(cutoff_result);
  EXPECT_EQ(cutoff_error.code, TargetedRegenerationExecutionErrorCode::kWorkBoundExceeded);
  EXPECT_EQ(cutoff_error.invariant_id,
            "allocator.targeted_regeneration_execution.refreshed_expanded_resource_budget.v2");
  EXPECT_FALSE(cutoff_error.failed_execution.has_value());
  EXPECT_EQ(internal::TargetedRegenerationSourceResourceSpanVisitsForTesting(), 1U);
  EXPECT_EQ(internal::TargetedRegenerationRouteQueriesForTesting(), 0U);
}

TEST(TargetedRegenerationExecutionTest,
     PerQueryCpuBoundsFailAsWorkBoundsWithoutCandidateStoreMutation) {
  const auto expect_route_failure = [&](auto mutate_limits) {
    ExecutionFixture fixture = BuildExecutionFixture(true);
    TargetedRegenerationExecutionConfig config;
    mutate_limits(config.route_limits);
    config.maximum_total_route_work_units =
        config.maximum_route_queries * config.route_limits.maximum_work_units;
    const std::vector<candidates::StoredCandidate> pool_before =
        fixture.store->Enumerate(fixture.request.pools.front().net);
    const std::vector<candidates::CandidateRejection> rejections_before =
        fixture.store->Rejections();
    const candidates::CandidateStoreTelemetry telemetry_before = fixture.store->telemetry();
    TargetedRegenerationExecutionResult result = ExecuteTargetedRegenerationPlanCpu(
        kTargetedRegenerationExecutionSchemaVersion, fixture.board, fixture.request,
        PlanFor(fixture), *fixture.store, config);
    ASSERT_TRUE(std::holds_alternative<TargetedRegenerationExecutionError>(result));
    const TargetedRegenerationExecutionError& error =
        std::get<TargetedRegenerationExecutionError>(result);
    EXPECT_EQ(error.code, TargetedRegenerationExecutionErrorCode::kWorkBoundExceeded);
    ASSERT_TRUE(error.failed_execution.has_value());
    const auto& observation = *error.failed_execution;
    ASSERT_EQ(observation.columns.size(), 1U);
    ASSERT_TRUE(observation.columns.front().route_telemetry.has_value());
    const routing::CpuRouteTelemetry& route_telemetry =
        *observation.columns.front().route_telemetry;
    EXPECT_EQ(observation.counters.route_queries, 1U);
    EXPECT_EQ(observation.counters.route_work_units, route_telemetry.work_units);
    EXPECT_EQ(observation.counters.peak_route_record_count, route_telemetry.peak_record_count);
    EXPECT_EQ(observation.counters.peak_route_queue_size, route_telemetry.peak_queue_size);
    EXPECT_EQ(observation.observation_checksum,
              internal::ComputeTargetedRegenerationFailedObservationChecksumV2(
                  observation.plan_checksum, observation.config, observation.store_config,
                  observation.candidate_store_publication_committed, error.code,
                  observation.counters, observation.columns));
    EXPECT_GT(internal::TargetedRegenerationRouteQueriesForTesting(), 0U);
    EXPECT_EQ(fixture.store->Enumerate(fixture.request.pools.front().net), pool_before);
    EXPECT_EQ(fixture.store->Rejections(), rejections_before);
    EXPECT_EQ(fixture.store->telemetry(), telemetry_before);
  };
  expect_route_failure([](routing::CpuRouteWorkLimits& limits) { limits.maximum_work_units = 1; });
  expect_route_failure(
      [](routing::CpuRouteWorkLimits& limits) { limits.maximum_record_count = 1; });
  expect_route_failure([](routing::CpuRouteWorkLimits& limits) { limits.maximum_queue_size = 1; });
  expect_route_failure(
      [](routing::CpuRouteWorkLimits& limits) { limits.maximum_reconstruction_states = 1; });
}

TEST(TargetedRegenerationExecutionTest,
     LaterBoundedFailureRetainsEveryPriorAndAttemptedColumnInOrder) {
  ExecutionFixture witness_fixture = BuildExecutionFixture(true, 0, StoreConfig(), true);
  TargetedRegenerationExecution witness = BuiltExecution(ExecuteTargetedRegenerationPlanCpu(
      kTargetedRegenerationExecutionSchemaVersion, witness_fixture.board, witness_fixture.request,
      TwoTargetPlanFor(witness_fixture), *witness_fixture.store,
      TargetedRegenerationExecutionConfig{}));
  ASSERT_GT(witness.columns().size(), 1U);
  enum class BoundKind { kWork, kRecords, kQueue };
  BoundKind bound_kind = BoundKind::kWork;
  std::uint64_t prefix_work = witness.columns().front().route_telemetry->work_units;
  std::uint64_t prefix_records = witness.columns().front().route_telemetry->peak_record_count;
  std::uint64_t prefix_queue = witness.columns().front().route_telemetry->peak_queue_size;
  std::uint64_t selected_limit = 0;
  std::size_t failure_index = witness.columns().size();
  for (std::size_t index = 1; index < witness.columns().size(); ++index) {
    ASSERT_TRUE(witness.columns()[index].route_telemetry.has_value());
    const routing::CpuRouteTelemetry& telemetry = *witness.columns()[index].route_telemetry;
    if (telemetry.work_units > prefix_work) {
      bound_kind = BoundKind::kWork;
      selected_limit = prefix_work;
      failure_index = index;
      break;
    }
    if (telemetry.peak_record_count > prefix_records) {
      bound_kind = BoundKind::kRecords;
      selected_limit = prefix_records;
      failure_index = index;
      break;
    }
    if (telemetry.peak_queue_size > prefix_queue) {
      bound_kind = BoundKind::kQueue;
      selected_limit = prefix_queue;
      failure_index = index;
      break;
    }
    prefix_work = std::max(prefix_work, telemetry.work_units);
    prefix_records = std::max(prefix_records, telemetry.peak_record_count);
    prefix_queue = std::max(prefix_queue, telemetry.peak_queue_size);
  }
  ASSERT_LT(failure_index, witness.columns().size());

  ExecutionFixture fixture = BuildExecutionFixture(true, 0, StoreConfig(), true);
  TargetedRegenerationPlan plan = TwoTargetPlanFor(fixture);
  TargetedRegenerationExecutionConfig config;
  if (bound_kind == BoundKind::kWork) {
    config.route_limits.maximum_work_units = selected_limit;
  } else if (bound_kind == BoundKind::kRecords) {
    config.route_limits.maximum_record_count = selected_limit;
  } else {
    config.route_limits.maximum_queue_size = selected_limit;
  }
  config.maximum_total_route_work_units =
      plan.total_requested_columns() * config.route_limits.maximum_work_units;
  const auto pool_before = fixture.store->Enumerate(fixture.request.pools.front().net);
  const auto rejections_before = fixture.store->Rejections();
  const auto telemetry_before = fixture.store->telemetry();
  TargetedRegenerationExecutionResult result =
      ExecuteTargetedRegenerationPlanCpu(kTargetedRegenerationExecutionSchemaVersion, fixture.board,
                                         fixture.request, std::move(plan), *fixture.store, config);
  ASSERT_TRUE(std::holds_alternative<TargetedRegenerationExecutionError>(result));
  const TargetedRegenerationExecutionError& error =
      std::get<TargetedRegenerationExecutionError>(result);
  ASSERT_EQ(error.code, TargetedRegenerationExecutionErrorCode::kWorkBoundExceeded);
  ASSERT_TRUE(error.failed_execution.has_value());
  const auto& observation = *error.failed_execution;
  ASSERT_EQ(observation.columns.size(), failure_index + 1U);
  EXPECT_EQ(observation.counters.route_queries, observation.columns.size());
  std::uint64_t observed_work = 0;
  for (std::size_t index = 0; index < observation.columns.size(); ++index) {
    EXPECT_EQ(observation.columns[index].query_identity, index + 1U);
    ASSERT_TRUE(observation.columns[index].route_telemetry.has_value());
    observed_work += observation.columns[index].route_telemetry->work_units;
  }
  EXPECT_EQ(observation.columns.back().outcome, TargetedRegenerationColumnOutcome::kQueryInFlight);
  ASSERT_GT(observation.columns.size(), 1U);
  EXPECT_EQ(observation.columns.front().outcome,
            TargetedRegenerationColumnOutcome::kGeneratedPendingPublication);
  EXPECT_GT(observation.columns.front().candidate_draft_logical_bytes, 0U);
  ExpectReachedColumnStageCounters(observation);
  EXPECT_EQ(observation.counters.route_work_units, observed_work);
  EXPECT_EQ(observation.observation_checksum,
            internal::ComputeTargetedRegenerationFailedObservationChecksumV2(
                observation.plan_checksum, observation.config, observation.store_config,
                observation.candidate_store_publication_committed, error.code, observation.counters,
                observation.columns));
  EXPECT_EQ(fixture.store->Enumerate(fixture.request.pools.front().net), pool_before);
  EXPECT_EQ(fixture.store->Rejections(), rejections_before);
  EXPECT_EQ(fixture.store->telemetry(), telemetry_before);
}

TEST(TargetedRegenerationExecutionTest, PostQueryHostFailuresRetainFirstAndLaterAttemptedPrefixes) {
  const auto expect_failure = [](std::uint64_t successful_query_starts,
                                 internal::TargetedRegenerationHostFailureForTesting failure_kind) {
    ExecutionFixture fixture = BuildExecutionFixture(true);
    TargetedRegenerationPlan plan = PlanFor(fixture);
    ASSERT_GT(plan.total_requested_columns(), successful_query_starts);
    ASSERT_FALSE(fixture.request.pools.front().candidates.empty());
    const candidates::StoredCandidate retained = fixture.request.pools.front().candidates.front();
    const std::array pin_requests = {candidates::CandidatePinRequest{
        .net = retained->net(),
        .candidate_id = retained->id(),
        .candidate_payload_checksum = retained->data().payload_checksum,
        .expected_candidate = retained,
    }};
    candidates::CandidateStorePinLeaseResult lease_result =
        fixture.store->AcquirePinLease(pin_requests);
    ASSERT_TRUE(std::holds_alternative<candidates::CandidateStorePinLease>(lease_result));
    candidates::CandidateStorePinLease independent_lease =
        std::get<candidates::CandidateStorePinLease>(std::move(lease_result));
    ASSERT_TRUE(independent_lease.active());

    std::vector<std::vector<candidates::StoredCandidate>> pools_before;
    for (const CandidatePool& pool : fixture.request.pools) {
      pools_before.push_back(fixture.store->Enumerate(pool.net));
    }
    const auto rejections_before = fixture.store->Rejections();
    const auto telemetry_before = fixture.store->telemetry();
    internal::SetTargetedRegenerationPostQueryHostFailureForTesting(successful_query_starts,
                                                                    failure_kind);
    TargetedRegenerationExecutionResult result = ExecuteTargetedRegenerationPlanCpu(
        kTargetedRegenerationExecutionSchemaVersion, fixture.board, fixture.request,
        std::move(plan), *fixture.store, TargetedRegenerationExecutionConfig{});
    internal::SetTargetedRegenerationPostQueryHostFailureForTesting(std::nullopt);

    ASSERT_TRUE(std::holds_alternative<TargetedRegenerationExecutionError>(result));
    const TargetedRegenerationExecutionError& error =
        std::get<TargetedRegenerationExecutionError>(result);
    EXPECT_EQ(error.code, TargetedRegenerationExecutionErrorCode::kResourceExhausted);
    ASSERT_TRUE(error.failed_execution.has_value());
    const auto& observation = *error.failed_execution;
    EXPECT_FALSE(observation.candidate_store_publication_committed);
    ASSERT_EQ(observation.columns.size(), successful_query_starts + 1U);
    EXPECT_EQ(observation.counters.route_queries, observation.columns.size());
    for (std::size_t index = 0; index < observation.columns.size(); ++index) {
      EXPECT_EQ(observation.columns[index].query_identity, index + 1U);
    }
    EXPECT_FALSE(observation.columns.back().route_telemetry.has_value());
    EXPECT_EQ(observation.columns.back().outcome,
              TargetedRegenerationColumnOutcome::kQueryInFlight);
    if (successful_query_starts != 0) {
      EXPECT_EQ(observation.columns.front().outcome,
                TargetedRegenerationColumnOutcome::kGeneratedPendingPublication);
      EXPECT_GT(observation.columns.front().candidate_draft_logical_bytes, 0U);
    }
    ExpectReachedColumnStageCounters(observation);
    EXPECT_EQ(observation.observation_checksum,
              internal::ComputeTargetedRegenerationFailedObservationChecksumV2(
                  observation.plan_checksum, observation.config, observation.store_config,
                  observation.candidate_store_publication_committed, error.code,
                  observation.counters, observation.columns));
    for (std::size_t index = 0; index < fixture.request.pools.size(); ++index) {
      EXPECT_EQ(fixture.store->Enumerate(fixture.request.pools[index].net), pools_before[index]);
    }
    EXPECT_EQ(fixture.store->Rejections(), rejections_before);
    EXPECT_EQ(fixture.store->telemetry(), telemetry_before);
    EXPECT_TRUE(fixture.store->IsPinned(retained->id()));
    EXPECT_TRUE(independent_lease.active());
  };

  expect_failure(0, internal::TargetedRegenerationHostFailureForTesting::kBadAlloc);
  expect_failure(1, internal::TargetedRegenerationHostFailureForTesting::kLengthError);
}

TEST(TargetedRegenerationExecutionTest,
     LaterRequestPreparationFailureRetainsOnlyPreviouslyStartedQueries) {
  const auto expect_failure = [](internal::TargetedRegenerationHostFailureForTesting failure_kind) {
    ExecutionFixture fixture = BuildExecutionFixture(true);
    TargetedRegenerationPlan plan = PlanFor(fixture);
    ASSERT_GT(plan.total_requested_columns(), 1U);
    internal::SetTargetedRegenerationRequestPreparationHostFailureForTesting(1, failure_kind);
    TargetedRegenerationExecutionResult result = ExecuteTargetedRegenerationPlanCpu(
        kTargetedRegenerationExecutionSchemaVersion, fixture.board, fixture.request,
        std::move(plan), *fixture.store, TargetedRegenerationExecutionConfig{});
    internal::SetTargetedRegenerationRequestPreparationHostFailureForTesting(std::nullopt);

    ASSERT_TRUE(std::holds_alternative<TargetedRegenerationExecutionError>(result));
    const TargetedRegenerationExecutionError& error =
        std::get<TargetedRegenerationExecutionError>(result);
    EXPECT_EQ(error.code, TargetedRegenerationExecutionErrorCode::kResourceExhausted);
    ASSERT_TRUE(error.failed_execution.has_value());
    const auto& observation = *error.failed_execution;
    ASSERT_EQ(observation.columns.size(), 1U);
    EXPECT_EQ(observation.counters.route_queries, observation.columns.size());
    EXPECT_EQ(internal::TargetedRegenerationRouteQueriesForTesting(), observation.columns.size());
    EXPECT_EQ(observation.columns.front().query_identity, 1U);
    EXPECT_NE(observation.columns.front().outcome,
              TargetedRegenerationColumnOutcome::kQueryInFlight);
    ExpectReachedColumnStageCounters(observation);
    ExpectEveryFinalRejectionHasCompleteEvidence(observation);
  };

  expect_failure(internal::TargetedRegenerationHostFailureForTesting::kBadAlloc);
  expect_failure(internal::TargetedRegenerationHostFailureForTesting::kLengthError);
}

TEST(TargetedRegenerationExecutionTest,
     RejectionEvidenceHostFailuresRetainTruthfulIncompleteStage) {
  const auto expect_route_failure =
      [](internal::TargetedRegenerationHostFailureForTesting failure_kind) {
        ExecutionFixture fixture = BuildExecutionFixture(true);
        TargetedRegenerationPlan plan = PlanFor(fixture);
        internal::SetTargetedRegenerationRouteFailureForTesting(true);
        internal::SetTargetedRegenerationRejectionEvidenceHostFailureForTesting(
            internal::TargetedRegenerationRejectionEvidenceBoundaryForTesting::kRoute,
            failure_kind);
        TargetedRegenerationExecutionResult result = ExecuteTargetedRegenerationPlanCpu(
            kTargetedRegenerationExecutionSchemaVersion, fixture.board, fixture.request,
            std::move(plan), *fixture.store, TargetedRegenerationExecutionConfig{});
        internal::SetTargetedRegenerationRejectionEvidenceHostFailureForTesting(std::nullopt);
        internal::SetTargetedRegenerationRouteFailureForTesting(false);

        ASSERT_TRUE(std::holds_alternative<TargetedRegenerationExecutionError>(result));
        const TargetedRegenerationExecutionError& error =
            std::get<TargetedRegenerationExecutionError>(result);
        ASSERT_TRUE(error.failed_execution.has_value());
        const auto& observation = *error.failed_execution;
        ASSERT_EQ(observation.columns.size(), 1U);
        EXPECT_EQ(observation.columns.front().outcome,
                  TargetedRegenerationColumnOutcome::kRejectionEvidenceInFlight);
        EXPECT_FALSE(observation.columns.front().rejection.has_value());
        EXPECT_FALSE(observation.columns.front().rejection_code.has_value());
        EXPECT_EQ(observation.counters.rejected_columns, 0U);
        ExpectReachedColumnStageCounters(observation);
        ExpectEveryFinalRejectionHasCompleteEvidence(observation);
      };
  const auto expect_build_failure =
      [](internal::TargetedRegenerationHostFailureForTesting failure_kind) {
        ExecutionFixture fixture = BuildExecutionFixture(true);
        TargetedRegenerationPlan plan = PlanFor(fixture);
        internal::SetTargetedRegenerationRejectionEvidenceHostFailureForTesting(
            internal::TargetedRegenerationRejectionEvidenceBoundaryForTesting::kBuild,
            failure_kind);
        TargetedRegenerationExecutionResult result = ExecuteTargetedRegenerationPlanCpu(
            kTargetedRegenerationExecutionSchemaVersion, fixture.board, fixture.request,
            std::move(plan), *fixture.store, TargetedRegenerationExecutionConfig{});
        internal::SetTargetedRegenerationRejectionEvidenceHostFailureForTesting(std::nullopt);

        ASSERT_TRUE(std::holds_alternative<TargetedRegenerationExecutionError>(result));
        const TargetedRegenerationExecutionError& error =
            std::get<TargetedRegenerationExecutionError>(result);
        ASSERT_TRUE(error.failed_execution.has_value());
        const auto& observation = *error.failed_execution;
        ASSERT_EQ(observation.columns.size(), 1U);
        EXPECT_EQ(observation.columns.front().outcome,
                  TargetedRegenerationColumnOutcome::kRejectionEvidenceInFlight);
        EXPECT_FALSE(observation.columns.front().rejection.has_value());
        EXPECT_FALSE(observation.columns.front().rejection_code.has_value());
        EXPECT_EQ(observation.counters.successful_routes, 1U);
        EXPECT_EQ(observation.counters.built_candidates, 0U);
        EXPECT_EQ(observation.counters.rejected_columns, 0U);
        ExpectReachedColumnStageCounters(observation);
        ExpectEveryFinalRejectionHasCompleteEvidence(observation);
      };

  for (const internal::TargetedRegenerationHostFailureForTesting failure_kind :
       {internal::TargetedRegenerationHostFailureForTesting::kBadAlloc,
        internal::TargetedRegenerationHostFailureForTesting::kLengthError}) {
    expect_route_failure(failure_kind);
    expect_build_failure(failure_kind);
  }

  ExecutionFixture completed_fixture = BuildExecutionFixture(true);
  internal::SetTargetedRegenerationRouteFailureForTesting(true);
  TargetedRegenerationExecution completed = BuiltExecution(ExecuteTargetedRegenerationPlanCpu(
      kTargetedRegenerationExecutionSchemaVersion, completed_fixture.board,
      completed_fixture.request, PlanFor(completed_fixture), *completed_fixture.store,
      TargetedRegenerationExecutionConfig{}));
  internal::SetTargetedRegenerationRouteFailureForTesting(false);
  EXPECT_TRUE(std::ranges::any_of(completed.columns(), [](const auto& column) {
    return column.outcome == TargetedRegenerationColumnOutcome::kRouteDisconnected &&
           column.rejection.has_value() && column.rejection_code == column.rejection->code;
  }));
}

TEST(TargetedRegenerationExecutionTest,
     CompletedBuildRejectionRetainsCanonicalEvidenceAndFinalCorrelation) {
  ExecutionFixture fixture = BuildExecutionFixture(true);
  internal::SetTargetedRegenerationBuildRejectionForTesting(true);
  TargetedRegenerationExecution execution = BuiltExecution(ExecuteTargetedRegenerationPlanCpu(
      kTargetedRegenerationExecutionSchemaVersion, fixture.board, fixture.request, PlanFor(fixture),
      *fixture.store, TargetedRegenerationExecutionConfig{}));
  internal::SetTargetedRegenerationBuildRejectionForTesting(false);

  const auto build_rejection =
      std::ranges::find(execution.columns(), TargetedRegenerationColumnOutcome::kBuildRejected,
                        &TargetedRegenerationColumnRecord::outcome);
  ASSERT_NE(build_rejection, execution.columns().end());
  ASSERT_TRUE(build_rejection->rejection.has_value());
  const candidates::CandidateRejection& rejection = *build_rejection->rejection;
  EXPECT_EQ(rejection, candidates::CanonicalizeCandidateRejectionV1(rejection));
  EXPECT_EQ(rejection.invariant_id, "candidate.builder.cpu_producer_authentication.v1");
  EXPECT_EQ(build_rejection->rejection_code, rejection.code);
  EXPECT_EQ(build_rejection->candidate_id, rejection.candidate_id);
  EXPECT_EQ(build_rejection->candidate_payload_checksum, rejection.candidate_payload_checksum);
  EXPECT_EQ(rejection.provenance.batch_identity, build_rejection->batch_identity);
  EXPECT_EQ(rejection.provenance.query_identity, build_rejection->query_identity);

  const std::uint64_t build_rejection_count =
      std::ranges::count(execution.columns(), TargetedRegenerationColumnOutcome::kBuildRejected,
                         &TargetedRegenerationColumnRecord::outcome);
  const std::uint64_t built_candidate_count = std::ranges::count_if(
      execution.columns(), [](const TargetedRegenerationColumnRecord& column) {
        return column.outcome == TargetedRegenerationColumnOutcome::kAdmitted ||
               column.outcome == TargetedRegenerationColumnOutcome::kDuplicate ||
               column.outcome == TargetedRegenerationColumnOutcome::kAdmissionRejected;
      });
  const std::uint64_t rejected_column_count = std::ranges::count_if(
      execution.columns(), [](const TargetedRegenerationColumnRecord& column) {
        return column.outcome == TargetedRegenerationColumnOutcome::kDuplicate ||
               column.outcome == TargetedRegenerationColumnOutcome::kRouteDisconnected ||
               column.outcome == TargetedRegenerationColumnOutcome::kRouteUnsupported ||
               column.outcome == TargetedRegenerationColumnOutcome::kBuildRejected ||
               column.outcome == TargetedRegenerationColumnOutcome::kAdmissionRejected;
      });
  EXPECT_EQ(build_rejection_count, 1U);
  EXPECT_EQ(execution.counters().successful_routes, execution.columns().size());
  EXPECT_EQ(execution.counters().built_candidates, built_candidate_count);
  EXPECT_EQ(execution.counters().rejected_columns, rejected_column_count);
  EXPECT_TRUE(std::ranges::none_of(execution.columns(), [](const auto& column) {
    return column.outcome == TargetedRegenerationColumnOutcome::kQueryInFlight ||
           column.outcome == TargetedRegenerationColumnOutcome::kBuildInFlight ||
           column.outcome == TargetedRegenerationColumnOutcome::kGeneratedPendingPublication ||
           column.outcome == TargetedRegenerationColumnOutcome::kRejectionEvidenceInFlight ||
           column.outcome ==
               TargetedRegenerationColumnOutcome::kPublicationCommittedOutcomeCorrelationPending;
  }));
  const std::vector<candidates::CandidateRejection> retained_rejections =
      fixture.store->Rejections();
  EXPECT_NE(std::ranges::find(retained_rejections, rejection), retained_rejections.end());
}

TEST(TargetedRegenerationExecutionTest,
     CandidateStorePreparationFailureIsFullyAtomicThroughExecution) {
  ExecutionFixture fixture = BuildExecutionFixture(true);
  TargetedRegenerationPlan plan = PlanFor(fixture);
  ASSERT_FALSE(fixture.request.pools.front().candidates.empty());
  const candidates::StoredCandidate retained = fixture.request.pools.front().candidates.front();
  const std::array pin_requests = {candidates::CandidatePinRequest{
      .net = retained->net(),
      .candidate_id = retained->id(),
      .candidate_payload_checksum = retained->data().payload_checksum,
      .expected_candidate = retained,
  }};
  candidates::CandidateStorePinLeaseResult lease_result =
      fixture.store->AcquirePinLease(pin_requests);
  ASSERT_TRUE(std::holds_alternative<candidates::CandidateStorePinLease>(lease_result));
  candidates::CandidateStorePinLease independent_lease =
      std::get<candidates::CandidateStorePinLease>(std::move(lease_result));

  std::vector<std::vector<candidates::StoredCandidate>> pools_before;
  for (const CandidatePool& pool : fixture.request.pools) {
    pools_before.push_back(fixture.store->Enumerate(pool.net));
  }
  const auto rejections_before = fixture.store->Rejections();
  const auto telemetry_before = fixture.store->telemetry();
  candidates::internal::SetPublicationPreparationFailureCountdownForTesting(0);
  TargetedRegenerationExecutionResult result = ExecuteTargetedRegenerationPlanCpu(
      kTargetedRegenerationExecutionSchemaVersion, fixture.board, fixture.request, std::move(plan),
      *fixture.store, TargetedRegenerationExecutionConfig{});
  candidates::internal::SetPublicationPreparationFailureCountdownForTesting(std::nullopt);

  ASSERT_TRUE(std::holds_alternative<TargetedRegenerationExecutionError>(result));
  const TargetedRegenerationExecutionError& error =
      std::get<TargetedRegenerationExecutionError>(result);
  EXPECT_EQ(error.code, TargetedRegenerationExecutionErrorCode::kResourceExhausted);
  ASSERT_TRUE(error.failed_execution.has_value());
  const auto& observation = *error.failed_execution;
  EXPECT_FALSE(observation.candidate_store_publication_committed);
  EXPECT_EQ(observation.counters.route_queries, observation.columns.size());
  EXPECT_TRUE(std::ranges::none_of(observation.columns, [](const auto& column) {
    return column.outcome == TargetedRegenerationColumnOutcome::kQueryInFlight ||
           column.outcome == TargetedRegenerationColumnOutcome::kBuildInFlight;
  }));
  EXPECT_TRUE(std::ranges::any_of(observation.columns, [](const auto& column) {
    return column.outcome == TargetedRegenerationColumnOutcome::kGeneratedPendingPublication;
  }));
  ExpectReachedColumnStageCounters(observation);
  EXPECT_EQ(observation.observation_checksum,
            internal::ComputeTargetedRegenerationFailedObservationChecksumV2(
                observation.plan_checksum, observation.config, observation.store_config,
                observation.candidate_store_publication_committed, error.code, observation.counters,
                observation.columns));
  for (std::size_t index = 0; index < fixture.request.pools.size(); ++index) {
    EXPECT_EQ(fixture.store->Enumerate(fixture.request.pools[index].net), pools_before[index]);
  }
  EXPECT_EQ(fixture.store->Rejections(), rejections_before);
  EXPECT_EQ(fixture.store->telemetry(), telemetry_before);
  EXPECT_TRUE(fixture.store->IsPinned(retained->id()));
  EXPECT_TRUE(independent_lease.active());
}

TEST(TargetedRegenerationExecutionTest,
     PostPublicationHostFailuresReportCommitAndRetainAuthoritativeStoreState) {
  const auto expect_failure = [](internal::TargetedRegenerationHostFailureForTesting failure_kind) {
    ExecutionFixture expected_fixture = BuildExecutionFixture(true);
    TargetedRegenerationExecution expected = BuiltExecution(ExecuteTargetedRegenerationPlanCpu(
        kTargetedRegenerationExecutionSchemaVersion, expected_fixture.board,
        expected_fixture.request, PlanFor(expected_fixture), *expected_fixture.store,
        TargetedRegenerationExecutionConfig{}));
    const std::vector<candidates::CandidateRejection> expected_rejections =
        expected_fixture.store->Rejections();
    ASSERT_FALSE(expected_rejections.empty());
    const candidates::CandidateStoreTelemetry expected_telemetry =
        expected_fixture.store->telemetry();

    ExecutionFixture fixture = BuildExecutionFixture(true);
    internal::SetTargetedRegenerationPostPublicationHostFailureForTesting(failure_kind);
    TargetedRegenerationExecutionResult result = ExecuteTargetedRegenerationPlanCpu(
        kTargetedRegenerationExecutionSchemaVersion, fixture.board, fixture.request,
        PlanFor(fixture), *fixture.store, TargetedRegenerationExecutionConfig{});
    internal::SetTargetedRegenerationPostPublicationHostFailureForTesting(std::nullopt);

    ASSERT_TRUE(std::holds_alternative<TargetedRegenerationExecutionError>(result));
    const TargetedRegenerationExecutionError& error =
        std::get<TargetedRegenerationExecutionError>(result);
    EXPECT_EQ(error.code, TargetedRegenerationExecutionErrorCode::kResourceExhausted);
    ASSERT_TRUE(error.failed_execution.has_value());
    const auto& observation = *error.failed_execution;
    EXPECT_TRUE(observation.candidate_store_publication_committed);
    EXPECT_EQ(observation.columns.size(), expected.columns().size());
    EXPECT_EQ(observation.counters.route_queries, observation.columns.size());
    EXPECT_TRUE(std::ranges::none_of(observation.columns, [](const auto& column) {
      return column.outcome == TargetedRegenerationColumnOutcome::kQueryInFlight ||
             column.outcome == TargetedRegenerationColumnOutcome::kBuildInFlight ||
             column.outcome == TargetedRegenerationColumnOutcome::kGeneratedPendingPublication ||
             column.outcome == TargetedRegenerationColumnOutcome::kRejectionEvidenceInFlight;
    }));
    EXPECT_TRUE(std::ranges::any_of(observation.columns, [](const auto& column) {
      return column.outcome ==
             TargetedRegenerationColumnOutcome::kPublicationCommittedOutcomeCorrelationPending;
    }));
    EXPECT_EQ(observation.counters.admitted_candidates, 0U);
    EXPECT_EQ(observation.counters.duplicate_candidates, 0U);
    ExpectReachedColumnStageCounters(observation);
    ExpectEveryFinalRejectionHasCompleteEvidence(observation);
    EXPECT_EQ(observation.observation_checksum,
              internal::ComputeTargetedRegenerationFailedObservationChecksumV2(
                  observation.plan_checksum, observation.config, observation.store_config,
                  observation.candidate_store_publication_committed, error.code,
                  observation.counters, observation.columns));

    for (const CandidatePool& expected_pool : expected.refreshed_pools()) {
      const std::vector<candidates::StoredCandidate> actual_pool =
          fixture.store->Enumerate(expected_pool.net);
      ASSERT_EQ(actual_pool.size(), expected_pool.candidates.size());
      for (std::size_t index = 0; index < actual_pool.size(); ++index) {
        ASSERT_NE(actual_pool[index], nullptr);
        ASSERT_NE(expected_pool.candidates[index], nullptr);
        EXPECT_EQ(actual_pool[index]->net(), expected_pool.candidates[index]->net());
        EXPECT_EQ(actual_pool[index]->data(), expected_pool.candidates[index]->data());
      }
    }
    EXPECT_EQ(fixture.store->Rejections(), expected_rejections);
    EXPECT_EQ(fixture.store->telemetry(), expected_telemetry);

    std::vector<candidates::CandidateStoreExpectedPool> expected_pools;
    expected_pools.reserve(fixture.request.pools.size());
    for (const CandidatePool& source_pool : fixture.request.pools) {
      const PreparedNetRoutingContext* context = fixture.workload.FindNet(source_pool.net);
      ASSERT_NE(context, nullptr);
      expected_pools.push_back(candidates::CandidateStoreExpectedPool{
          .net = source_pool.net,
          .associations = candidates::AssociationsFor(fixture.board, context->compiled_board),
          .candidates = fixture.store->Enumerate(source_pool.net),
      });
    }
    candidates::CandidateStoreInvocationAdmissionResult binding_result =
        fixture.store->AdmitInvocationIfSourcePoolsMatch(
            fixture.board, std::vector<candidates::CandidateStoreExpectedPool>(expected_pools), {});
    ASSERT_TRUE(std::holds_alternative<std::vector<candidates::CandidateStoreAdmissionResult>>(
        binding_result));
    EXPECT_TRUE(
        std::get<std::vector<candidates::CandidateStoreAdmissionResult>>(binding_result).empty());
    // The source-only conditional invocation compares the persistent per-net
    // association bindings as well as complete candidate values.
    EXPECT_EQ(fixture.store->telemetry(), expected_telemetry);
  };

  expect_failure(internal::TargetedRegenerationHostFailureForTesting::kBadAlloc);
  expect_failure(internal::TargetedRegenerationHostFailureForTesting::kLengthError);
}

TEST(TargetedRegenerationExecutionTest,
     SuccessorLeaseFailureDoesNotClaimPinsAndRetainsCommittedPublication) {
  ExecutionFixture expected_fixture = BuildExecutionFixture(true);
  std::vector<candidates::CandidateId> expected_source_ids;
  for (const CandidatePool& pool : expected_fixture.request.pools) {
    for (const candidates::StoredCandidate& candidate : pool.candidates) {
      expected_source_ids.push_back(candidate->id());
    }
  }
  TargetedRegenerationExecution expected = BuiltExecution(ExecuteTargetedRegenerationPlanCpu(
      kTargetedRegenerationExecutionSchemaVersion, expected_fixture.board, expected_fixture.request,
      PlanFor(expected_fixture), *expected_fixture.store, TargetedRegenerationExecutionConfig{}));
  std::vector<candidates::CandidateId> expected_novel_winner_ids;
  for (const NetSelection& selection : expected.refreshed_world().selections) {
    if (selection.status == NetSelectionStatus::kSelected && selection.candidate_id.has_value() &&
        std::ranges::find(expected_source_ids, *selection.candidate_id) ==
            expected_source_ids.end()) {
      expected_novel_winner_ids.push_back(*selection.candidate_id);
    }
  }
  ASSERT_FALSE(expected_novel_winner_ids.empty());

  ExecutionFixture fixture = BuildExecutionFixture(true);
  TargetedRegenerationPlan plan = PlanFor(fixture);
  std::vector<candidates::CandidateId> source_ids;
  for (const CandidatePool& pool : fixture.request.pools) {
    for (const candidates::StoredCandidate& candidate : pool.candidates) {
      source_ids.push_back(candidate->id());
    }
  }
  internal::SetTargetedRegenerationSuccessorLeaseFailureForTesting(true);
  TargetedRegenerationExecutionResult result = ExecuteTargetedRegenerationPlanCpu(
      kTargetedRegenerationExecutionSchemaVersion, fixture.board, fixture.request, std::move(plan),
      *fixture.store, TargetedRegenerationExecutionConfig{});
  internal::SetTargetedRegenerationSuccessorLeaseFailureForTesting(false);

  ASSERT_TRUE(std::holds_alternative<TargetedRegenerationExecutionError>(result));
  const TargetedRegenerationExecutionError& error =
      std::get<TargetedRegenerationExecutionError>(result);
  EXPECT_EQ(error.code, TargetedRegenerationExecutionErrorCode::kSuccessorLease);
  ASSERT_TRUE(error.failed_execution.has_value());
  const auto& observation = *error.failed_execution;
  EXPECT_TRUE(observation.candidate_store_publication_committed);
  EXPECT_EQ(observation.counters.successor_pinned_candidates, 0U);
  EXPECT_TRUE(plan.has_active_pin_lease());
  for (const candidates::CandidateId& expected_winner_id : expected_novel_winner_ids) {
    bool found_expected_winner = false;
    for (const CandidatePool& pool : fixture.request.pools) {
      for (const candidates::StoredCandidate& candidate : fixture.store->Enumerate(pool.net)) {
        if (candidate->id() == expected_winner_id) {
          found_expected_winner = true;
          EXPECT_TRUE(std::ranges::find(source_ids, candidate->id()) == source_ids.end());
          EXPECT_FALSE(fixture.store->IsPinned(candidate->id()));
        }
      }
    }
    EXPECT_TRUE(found_expected_winner);
  }
  ExpectReachedColumnStageCounters(observation);
  ExpectEveryFinalRejectionHasCompleteEvidence(observation);
}

TEST(TargetedRegenerationExecutionTest, RejectsSchemaV1AndBindsStoreConfigIntoBatchIdentity) {
  ExecutionFixture old_schema = BuildExecutionFixture(true);
  TargetedRegenerationExecutionResult old_result = ExecuteTargetedRegenerationPlanCpu(
      kTargetedRegenerationExecutionSchemaVersionV1, old_schema.board, old_schema.request,
      PlanFor(old_schema), *old_schema.store, TargetedRegenerationExecutionConfig{});
  ASSERT_TRUE(std::holds_alternative<TargetedRegenerationExecutionError>(old_result));
  EXPECT_EQ(std::get<TargetedRegenerationExecutionError>(old_result).code,
            TargetedRegenerationExecutionErrorCode::kUnsupportedSchema);
  EXPECT_EQ(internal::TargetedRegenerationRouteQueriesForTesting(), 0U);

  ExecutionFixture first_fixture = BuildExecutionFixture(true);
  TargetedRegenerationExecution first = BuiltExecution(ExecuteTargetedRegenerationPlanCpu(
      kTargetedRegenerationExecutionSchemaVersion, first_fixture.board, first_fixture.request,
      PlanFor(first_fixture), *first_fixture.store, TargetedRegenerationExecutionConfig{}));
  candidates::CandidateStoreConfig changed_store_config = StoreConfig();
  ++changed_store_config.maximum_rejection_records;
  ExecutionFixture changed_fixture = BuildExecutionFixture(true, 0, changed_store_config);
  TargetedRegenerationExecution changed = BuiltExecution(ExecuteTargetedRegenerationPlanCpu(
      kTargetedRegenerationExecutionSchemaVersion, changed_fixture.board, changed_fixture.request,
      PlanFor(changed_fixture), *changed_fixture.store, TargetedRegenerationExecutionConfig{}));
  ASSERT_FALSE(first.columns().empty());
  ASSERT_FALSE(changed.columns().empty());
  EXPECT_NE(first.columns().front().batch_identity, changed.columns().front().batch_identity);
  EXPECT_NE(first.execution_checksum(), changed.execution_checksum());
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
  candidates::CandidateRejection fixture_rejection;
  fixture_rejection.candidate_id = candidates::CandidateId{.high = 257, .low = 263};
  fixture_rejection.net = board_ir::EntityRef{.id = 179, .generation = 181};
  fixture_rejection.code = candidates::CandidateRejectionCode::kBudgetExhausted;
  fixture_rejection.invariant_id = "candidate.fixture.rejection.v1";
  fixture_rejection.policy_identity = 193;
  fixture_rejection.provenance.batch_identity = 197;
  fixture_rejection.provenance.query_identity = 199;
  fixture_rejection.candidate_payload_checksum = 269;
  fixture_rejection.detail = "checksum fixture rejection";
  fixture_rejection = candidates::CanonicalizeCandidateRejectionV1(fixture_rejection);
  internal::TargetedRegenerationExecutionChecksumHeaderV2 header{
      .schema_version = 2,
      .plan_checksum = 3,
      .config =
          TargetedRegenerationExecutionConfig{
              .maximum_route_queries = 5,
              .route_limits =
                  routing::CpuRouteWorkLimits{
                      .maximum_work_units = 7,
                      .maximum_record_count = 11,
                      .maximum_queue_size = 13,
                      .maximum_reconstruction_states = 17,
                  },
              .maximum_total_route_work_units = 19,
              .maximum_policy_projection_visits = 23,
              .maximum_policy_resource_entries = 29,
              .maximum_candidate_draft_bytes = 31,
              .maximum_generated_candidate_bytes = 37,
              .maximum_rejection_bytes = 41,
              .maximum_transient_result_bytes = 43,
              .known_unmapped_exact_conflict_count = 47,
          },
      .store_config =
          candidates::CandidateStoreConfig{
              .maximum_candidates_per_net = 41,
              .maximum_candidate_bytes_per_net = 43,
              .maximum_rejection_records = 47,
              .maximum_rejection_items_per_transaction = 53,
              .maximum_admission_items_per_transaction = 59,
              .maximum_admission_input_bytes_per_transaction = 61,
              .maximum_admission_work_units_per_transaction = 67,
              .maximum_pin_lease_items_per_transaction = 71,
              .maximum_expected_pools_per_invocation = 73,
              .maximum_expected_candidates_per_invocation = 79,
          },
      .refreshed_request_manifest_checksum = 83,
      .refreshed_candidate_pool_manifest_checksum = 89,
      .baseline_world_checksum = 97,
      .refreshed_world_checksum = 101,
      .disposition = TargetedRegenerationExecutionDisposition::kStalled,
      .terminal_reason = TargetedRegenerationTerminalReason::kNoSelectionChange,
      .counters =
          TargetedRegenerationExecutionCounters{
              .requested_columns = 103,
              .route_queries = 107,
              .route_work_units = 109,
              .policy_projection_visits = 113,
              .peak_route_record_count = 127,
              .peak_route_queue_size = 131,
              .generated_candidate_bytes = 137,
              .rejection_record_bytes = 139,
              .transient_result_bytes = 149,
              .successful_routes = 151,
              .built_candidates = 157,
              .admitted_candidates = 163,
              .duplicate_candidates = 167,
              .rejected_columns = 173,
              .novel_retained_candidates = 179,
              .changed_selections = 181,
              .successor_pinned_candidates = 191,
          },
  };
  std::vector<TargetedRegenerationColumnRecord> columns = {
      TargetedRegenerationColumnRecord{
          .net = {.id = 179, .generation = 181},
          .column_index = 191,
          .policy_identity = 193,
          .batch_identity = 197,
          .query_identity = 199,
          .route_telemetry =
              routing::CpuRouteTelemetry{
                  .queue_pops = 211,
                  .expanded_states = 223,
                  .attempted_relaxations = 227,
                  .accepted_relaxations = 229,
                  .peak_record_count = 233,
                  .peak_queue_size = 239,
                  .work_units = 241,
              },
          .candidate_draft_logical_bytes = 251,
          .outcome = TargetedRegenerationColumnOutcome::kAdmissionRejected,
          .candidate_id = candidates::CandidateId{.high = 257, .low = 263},
          .candidate_payload_checksum = 269,
          .rejection_code = candidates::CandidateRejectionCode::kBudgetExhausted,
          .rejection = fixture_rejection,
      },
  };
  const std::uint64_t golden =
      internal::ComputeTargetedRegenerationExecutionChecksumV2(header, columns);
  EXPECT_EQ(golden, 13666812750934195884ULL);
  const std::uint64_t failure_golden =
      internal::ComputeTargetedRegenerationFailedObservationChecksumV2(
          header.plan_checksum, header.config, header.store_config, false,
          TargetedRegenerationExecutionErrorCode::kResourceExhausted, header.counters, columns);
  EXPECT_EQ(failure_golden, 14174562675389583303ULL);
  EXPECT_NE(
      internal::ComputeTargetedRegenerationFailedObservationChecksumV2(
          header.plan_checksum, header.config, header.store_config, true,
          TargetedRegenerationExecutionErrorCode::kResourceExhausted, header.counters, columns),
      failure_golden);
  ++header.counters.admitted_candidates;
  EXPECT_NE(internal::ComputeTargetedRegenerationExecutionChecksumV2(header, columns), golden);
  header.counters.admitted_candidates--;
  ++header.store_config.maximum_rejection_records;
  EXPECT_NE(internal::ComputeTargetedRegenerationExecutionChecksumV2(header, columns), golden);
  --header.store_config.maximum_rejection_records;
  ++header.config.route_limits.maximum_queue_size;
  EXPECT_NE(internal::ComputeTargetedRegenerationExecutionChecksumV2(header, columns), golden);
  --header.config.route_limits.maximum_queue_size;
  ++columns.front().route_telemetry->work_units;
  EXPECT_NE(internal::ComputeTargetedRegenerationExecutionChecksumV2(header, columns), golden);
  --columns.front().route_telemetry->work_units;
  columns.front().rejection->detail.push_back('x');
  EXPECT_NE(internal::ComputeTargetedRegenerationExecutionChecksumV2(header, columns), golden);
  columns.front().rejection->detail.pop_back();
  columns.front().candidate_id->low++;
  EXPECT_NE(internal::ComputeTargetedRegenerationExecutionChecksumV2(header, columns), golden);
}

}  // namespace
}  // namespace apgar::allocator
