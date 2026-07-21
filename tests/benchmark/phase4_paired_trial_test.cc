#include "apgar/benchmark/phase4_paired_trial.h"

#include <cstdint>
#include <cstdlib>
#include <limits>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

#include "apgar/allocator/cpu_candidate_pool_preparation.h"
#include "src/allocator/cpu_candidate_allocation_session_internal.h"
#include "src/benchmark/phase4_paired_trial_internal.h"
#include "tests/support/google_test.h"

namespace apgar::benchmark {
namespace {

template <typename Value, typename Error>
[[nodiscard]] Value ValueOf(std::variant<Value, Error> result) {
  std::string failure;
  if (std::holds_alternative<Error>(result)) {
    if constexpr (std::is_same_v<Error, Phase4TrialArmFailure>) {
      failure = std::string(std::get<Error>(result).summary.invariant_id) + ": " +
                std::string(std::get<Error>(result).summary.detail);
    } else {
      failure = std::string(std::get<Error>(result).invariant_id) + ": " +
                std::string(std::get<Error>(result).detail);
    }
  }
  EXPECT_TRUE(std::holds_alternative<Value>(result)) << failure;
  if (!std::holds_alternative<Value>(result)) {
    std::abort();
  }
  return std::get<Value>(std::move(result));
}

template <typename Value, typename Error>
[[nodiscard]] Error ErrorOf(std::variant<Value, Error> result) {
  EXPECT_TRUE(std::holds_alternative<Error>(result));
  if (!std::holds_alternative<Error>(result)) {
    std::abort();
  }
  return std::get<Error>(std::move(result));
}

[[nodiscard]] Phase4PairedTrialError SummaryOf(Phase4TrialArmExecutionResult result) {
  Phase4TrialArmFailure failure = ErrorOf<Phase4TrialArmExecution>(std::move(result));
  return failure.summary;
}

[[nodiscard]] Phase4PairedTrialSpec Spec(Phase4TrialOrder order = Phase4TrialOrder::kBaselineFirst,
                                         std::uint32_t case_id = 100, std::uint64_t net_count = 6) {
  constexpr std::uint64_t pool_size = 4;
  constexpr std::uint64_t epochs = 2;
  const std::uint64_t columns_per_epoch = net_count;
  constexpr std::uint64_t sweeps = 6;
  constexpr std::uint64_t seed = 0xa911'0400'0001ULL;

  Phase4PairedTrialSpec spec;
  spec.case_id = case_id;
  spec.requested_pool_size = pool_size;
  spec.repetition_index = 7;
  spec.root_seed = seed;
  spec.execution_order = order;
  spec.preparation_worker_count = 1;
  spec.external_budget = Phase4ExternalBudget{
      .maximum_prepared_elapsed_nanoseconds = 3'600'000'000'000ULL,
      .maximum_cold_elapsed_nanoseconds = 3'600'000'000'000ULL,
      .maximum_address_space_bytes = 64ULL * 1024ULL * 1024ULL * 1024ULL,
      .maximum_peak_host_bytes = 64ULL * 1024ULL * 1024ULL * 1024ULL,
  };

  spec.baseline_config.deterministic_seed = seed;
  spec.baseline_config.maximum_sweeps = sweeps;
  spec.baseline_config.price_config.maximum_iterations = 100;
  spec.baseline_config.limits.maximum_nets = net_count;
  spec.baseline_config.limits.maximum_route_queries = net_count * sweeps;
  spec.baseline_config.limits.maximum_total_route_work_units =
      net_count * sweeps * spec.baseline_config.route_limits.maximum_work_units;

  spec.preparation_config.requested_candidates_per_net = pool_size;
  spec.preparation_config.deterministic_seed = seed;
  spec.preparation_config.route_limits = spec.baseline_config.route_limits;
  spec.preparation_config.limits.maximum_nets = net_count;
  spec.preparation_config.limits.maximum_route_queries = net_count * pool_size;
  spec.preparation_config.limits.maximum_total_route_work_units =
      net_count * pool_size * spec.baseline_config.route_limits.maximum_work_units;
  spec.preparation_config.limits.maximum_generated_candidate_bytes =
      128ULL * 1024ULL * 1024ULL * 1024ULL;
  const std::uint64_t retained_capacity = net_count * 8;
  spec.preparation_config.store_config = candidates::CandidateStoreConfig{
      .maximum_candidates_per_net = 8,
      .maximum_candidate_bytes_per_net = 64U * 1024U * 1024U,
      .maximum_rejection_records = 4U * retained_capacity + 2U * net_count,
      .maximum_rejection_items_per_transaction = retained_capacity,
      .maximum_admission_items_per_transaction = retained_capacity,
      .maximum_admission_input_bytes_per_transaction = 8ULL * 1024ULL * 1024ULL * 1024ULL,
      .maximum_admission_work_units_per_transaction = 1'000'000'000'000ULL,
      .maximum_pin_lease_items_per_transaction = retained_capacity,
      .maximum_expected_pools_per_invocation = net_count,
      .maximum_expected_candidates_per_invocation = retained_capacity,
  };

  auto& session = spec.candidate_session_config;
  session.intrinsic_cost_weight = spec.baseline_config.intrinsic_cost_weight;
  session.maximum_regeneration_epochs = epochs;
  session.price_config = spec.baseline_config.price_config;
  session.allocator_limits = spec.baseline_config.allocator_limits;
  session.regeneration_plan_config.maximum_target_nets = net_count;
  session.regeneration_plan_config.maximum_columns_per_net = 1;
  session.regeneration_plan_config.maximum_total_columns = columns_per_epoch;
  session.regeneration_plan_config.maximum_resource_actions_per_net = 16;
  session.regeneration_plan_config.maximum_total_resource_actions = net_count * 16;
  session.regeneration_execution_config.deterministic_seed = seed;
  session.regeneration_execution_config.maximum_route_queries = columns_per_epoch;
  session.regeneration_execution_config.route_limits = spec.baseline_config.route_limits;
  session.regeneration_execution_config.maximum_total_route_work_units =
      columns_per_epoch * spec.baseline_config.route_limits.maximum_work_units;
  session.schedules = {
      allocator::MultiWorldSchedule{
          .schedule_key = 1,
          .search_intrinsic_cost_weight = session.intrinsic_cost_weight,
          .maximum_selection_rounds = 1,
      },
      allocator::MultiWorldSchedule{
          .schedule_key = 2,
          .search_intrinsic_cost_weight = session.intrinsic_cost_weight,
          .maximum_selection_rounds = 5,
      },
  };
  session.multi_world_config.maximum_worlds = 2;
  session.multi_world_config.maximum_total_selection_rounds = 6;
  session.multi_world_config.maximum_retained_worlds = 2;
  session.limits.maximum_epoch_records = epochs;
  session.limits.maximum_total_requested_columns = epochs * columns_per_epoch;
  session.limits.maximum_total_route_queries = epochs * columns_per_epoch;
  session.limits.maximum_total_route_work_units =
      epochs * columns_per_epoch * spec.baseline_config.route_limits.maximum_work_units;
  return spec;
}

[[nodiscard]] std::unique_ptr<allocator::PersistentCpuCandidatePoolPreparer> Preparer(
    std::uint32_t workers = 1) {
  return ValueOf<std::unique_ptr<allocator::PersistentCpuCandidatePoolPreparer>>(
      allocator::CreatePersistentCpuCandidatePoolPreparer({.worker_count = workers}));
}

// Synthetic contract input only. Canonical evidence must come from the
// process controller defined by the next slice.
[[nodiscard]] Phase4ExternalResourceObservation SyntheticAuthorityForContractTest(
    const Phase4TrialArmExecution& execution) {
  const bool candidate = execution.semantics.arm == Phase4TrialArm::kReusableCandidateAllocation;
  Phase4ExternalResourceObservation observation{
      .authority_run_identity = 0x4001,
      .controller_identity = 0x4002,
      .process_instance_identity = candidate ? 0x4004U : 0x4003U,
      .associated_semantic_checksum = execution.semantics.semantic_checksum,
      .configured_wall_limit_nanoseconds =
          execution.semantics.external_budget.maximum_cold_elapsed_nanoseconds,
      .configured_address_space_limit_bytes =
          execution.semantics.external_budget.maximum_address_space_bytes,
      .configured_peak_host_limit_bytes =
          execution.semantics.external_budget.maximum_peak_host_bytes,
      .outer_elapsed_nanoseconds = execution.cold_elapsed_nanoseconds,
      .peak_host_bytes = 1,
      .isolated_process = true,
      .wall_authority_enforced = true,
      .memory_authority_enforced = true,
      .persistent_preparer_reused = candidate,
      .preparer_lifecycle = execution.preparer_lifecycle,
      .authority_checksum = 0,
  };
  observation.authority_checksum = internal::ComputePhase4ExternalAuthorityChecksumV1(observation);
  return observation;
}

[[nodiscard]] Phase4TrialArmRecord Finalized(Phase4TrialArmExecution execution) {
  const Phase4ExternalResourceObservation observation =
      SyntheticAuthorityForContractTest(execution);
  return ValueOf<Phase4TrialArmRecord>(FinalizePhase4TrialArmV1(std::move(execution), observation));
}

void Reauthenticate(Phase4TrialArmRecord& record) {
  record.semantics.semantic_checksum =
      internal::ComputePhase4TrialArmSemanticChecksumV1(record.semantics);
  record.external_observation.associated_semantic_checksum = record.semantics.semantic_checksum;
  record.external_observation.authority_checksum =
      internal::ComputePhase4ExternalAuthorityChecksumV1(record.external_observation);
  record.artifact_checksum = internal::ComputePhase4TrialArmArtifactChecksumV1(record);
}

struct ExecutedPair {
  Phase4TrialArmRecord baseline;
  Phase4TrialArmRecord candidate;
};

[[nodiscard]] Phase4TrialArmExecution ExecuteWarmCandidate(const Phase4PairedTrialSpec& spec) {
  std::unique_ptr<allocator::PersistentCpuCandidatePoolPreparer> preparer =
      Preparer(spec.preparation_worker_count);
  // The production preparer persists across repetitions. Exercise one
  // unmeasured invocation so the measured arm can prove actual reuse.
  (void)ValueOf<Phase4TrialArmExecution>(ExecutePhase4TrialArmV1(
      Phase4TrialArm::kReusableCandidateAllocation, spec, {}, preparer.get()));
  return ValueOf<Phase4TrialArmExecution>(ExecutePhase4TrialArmV1(
      Phase4TrialArm::kReusableCandidateAllocation, spec, {}, preparer.get()));
}

[[nodiscard]] ExecutedPair ExecutePair(const Phase4PairedTrialSpec& spec) {
  Phase4TrialArmExecution baseline = ValueOf<Phase4TrialArmExecution>(
      ExecutePhase4TrialArmV1(Phase4TrialArm::kSequentialBaseline, spec, {}));
  Phase4TrialArmExecution candidate = ExecuteWarmCandidate(spec);
  return ExecutedPair{
      .baseline = Finalized(std::move(baseline)),
      .candidate = Finalized(std::move(candidate)),
  };
}

TEST(Phase4PairedTrialTest, ExecutesAndAuthenticatesEqualOpportunityPair) {
  const Phase4PairedTrialSpec spec = Spec();
  ExecutedPair arms = ExecutePair(spec);
  Phase4PairedTrialResult result = ValueOf<Phase4PairedTrialResult>(
      AssemblePhase4PairedTrialV1(std::move(arms.baseline), std::move(arms.candidate)));

  EXPECT_EQ(result.baseline.semantics.opportunity,
            (Phase4RouteOpportunity{.route_queries = 36, .route_work_units = 720'000'000}));
  EXPECT_EQ(result.candidate.semantics.opportunity, result.baseline.semantics.opportunity);
  EXPECT_LE(result.baseline.semantics.actual.route_queries, 36U);
  EXPECT_LE(result.candidate.semantics.actual.route_queries, 36U);
  EXPECT_LE(result.baseline.semantics.actual.route_work_units, 720'000'000U);
  EXPECT_LE(result.candidate.semantics.actual.route_work_units, 720'000'000U);
  EXPECT_EQ(result.baseline.semantics.root_seed, spec.root_seed);
  EXPECT_EQ(result.candidate.semantics.root_seed, spec.root_seed);
  EXPECT_NE(result.semantic_checksum, 0U);
  EXPECT_NE(result.artifact_checksum, 0U);
  EXPECT_EQ(result.semantic_checksum, 6'850'076'695'171'498'078ULL);
}

TEST(Phase4PairedTrialTest, RejectsOneUnitQueryOpportunityMismatchBeforeExecution) {
  Phase4PairedTrialSpec spec = Spec();
  ++spec.candidate_session_config.regeneration_plan_config.maximum_total_columns;
  spec.candidate_session_config.regeneration_plan_config.maximum_columns_per_net = 2;
  const Phase4PairedTrialError error =
      SummaryOf(ExecutePhase4TrialArmV1(Phase4TrialArm::kSequentialBaseline, spec, {}));
  EXPECT_EQ(error.code, Phase4PairedTrialErrorCode::kUnequalBudget);
  EXPECT_EQ(error.invariant_id, "P4PAIR-QUERY-001");
}

TEST(Phase4PairedTrialTest, RejectsStructurallyUnreachableRegenerationOpportunity) {
  Phase4PairedTrialSpec spec = Spec();
  spec.candidate_session_config.regeneration_plan_config.maximum_target_nets = 1;
  const Phase4PairedTrialError error =
      SummaryOf(ExecutePhase4TrialArmV1(Phase4TrialArm::kSequentialBaseline, spec, {}));
  EXPECT_EQ(error.code, Phase4PairedTrialErrorCode::kUnequalBudget);
  EXPECT_EQ(error.invariant_id, "P4PAIR-PLAN-SHAPE-001");
}

TEST(Phase4PairedTrialTest, RejectsUnknownArmAndExecutionOrderBeforeDereference) {
  {
    const Phase4PairedTrialError error =
        SummaryOf(ExecutePhase4TrialArmV1(static_cast<Phase4TrialArm>(255), Spec(), {}));
    EXPECT_EQ(error.invariant_id, "P4PAIR-ENUM-001");
  }
  {
    Phase4PairedTrialSpec spec = Spec();
    spec.execution_order = static_cast<Phase4TrialOrder>(255);
    const Phase4PairedTrialError error =
        SummaryOf(ExecutePhase4TrialArmV1(Phase4TrialArm::kSequentialBaseline, spec, {}));
    EXPECT_EQ(error.invariant_id, "P4PAIR-ENUM-001");
  }
}

TEST(Phase4PairedTrialTest, RejectsSeedAndStoppingLineageMismatch) {
  {
    Phase4PairedTrialSpec spec = Spec();
    ++spec.preparation_config.deterministic_seed;
    const Phase4PairedTrialError error =
        SummaryOf(ExecutePhase4TrialArmV1(Phase4TrialArm::kSequentialBaseline, spec, {}));
    EXPECT_EQ(error.invariant_id, "P4PAIR-SEED-001");
  }
  {
    Phase4PairedTrialSpec spec = Spec();
    ++spec.baseline_config.maximum_sweeps;
    spec.baseline_config.limits.maximum_route_queries += 6;
    spec.baseline_config.limits.maximum_total_route_work_units +=
        6 * spec.baseline_config.route_limits.maximum_work_units;
    spec.candidate_session_config.regeneration_plan_config.maximum_total_columns += 3;
    spec.candidate_session_config.regeneration_plan_config.maximum_columns_per_net = 2;
    spec.candidate_session_config.regeneration_execution_config.maximum_route_queries += 3;
    spec.candidate_session_config.regeneration_execution_config.maximum_total_route_work_units +=
        3 * spec.baseline_config.route_limits.maximum_work_units;
    spec.candidate_session_config.limits.maximum_total_requested_columns += 6;
    spec.candidate_session_config.limits.maximum_total_route_queries += 6;
    spec.candidate_session_config.limits.maximum_total_route_work_units +=
        6 * spec.baseline_config.route_limits.maximum_work_units;
    const Phase4PairedTrialError error =
        SummaryOf(ExecutePhase4TrialArmV1(Phase4TrialArm::kSequentialBaseline, spec, {}));
    EXPECT_EQ(error.invariant_id, "P4PAIR-STOP-003");
  }
}

TEST(Phase4PairedTrialTest, RejectsUnsupportedQueryShapePoolsWithoutClamping) {
  for (const std::uint32_t pool_size : {1U, 1024U}) {
    Phase4PairedTrialSpec spec = Spec();
    spec.case_id = pool_size == 1 ? 2'001 : 2'000;
    spec.requested_pool_size = pool_size;
    spec.preparation_config.requested_candidates_per_net = pool_size;
    const Phase4PairedTrialError error =
        SummaryOf(ExecutePhase4TrialArmV1(Phase4TrialArm::kSequentialBaseline, spec, {}));
    EXPECT_EQ(error.invariant_id, "P4PAIR-POOL-001");
  }
}

TEST(Phase4PairedTrialTest, PreservesTypedCorpusAndPreparationFailures) {
  {
    Phase4PairedTrialSpec spec = Spec();
    spec.corpus_limits.maximum_nets = 5;
    Phase4TrialArmFailure failure = ErrorOf<Phase4TrialArmExecution>(
        ExecutePhase4TrialArmV1(Phase4TrialArm::kSequentialBaseline, spec, {}));
    ASSERT_TRUE(std::holds_alternative<Phase4RepresentativeCorpusError>(failure.payload));
    const auto& child = std::get<Phase4RepresentativeCorpusError>(failure.payload);
    EXPECT_EQ(failure.summary.invariant_id, child.invariant_id);
    EXPECT_EQ(child.code, Phase4RepresentativeCorpusErrorCode::kInputBoundExceeded);
    EXPECT_EQ(child.requested_case_id, spec.case_id);
  }
  {
    Phase4PairedTrialSpec spec = Spec();
    spec.preparation_config.store_config.maximum_candidates_per_net = 0;
    std::unique_ptr<allocator::PersistentCpuCandidatePoolPreparer> preparer = Preparer();
    Phase4TrialArmFailure failure = ErrorOf<Phase4TrialArmExecution>(ExecutePhase4TrialArmV1(
        Phase4TrialArm::kReusableCandidateAllocation, spec, {}, preparer.get()));
    ASSERT_TRUE(std::holds_alternative<Phase4CandidatePreparationFailureState>(failure.payload));
    const auto& child = std::get<Phase4CandidatePreparationFailureState>(failure.payload);
    EXPECT_EQ(failure.summary.invariant_id, child.error.invariant_id);
    EXPECT_EQ(child.case_state.workload.nets().size(), 6U);
  }
}

TEST(Phase4PairedTrialTest, PreservesCallerStateOnSessionFailure) {
  Phase4PairedTrialSpec spec = Spec();
  std::unique_ptr<allocator::PersistentCpuCandidatePoolPreparer> preparer = Preparer();
  allocator::internal::SetCpuCandidateAllocationFinalAssemblyFailureForTesting(true);
  Phase4TrialArmExecutionResult result = ExecutePhase4TrialArmV1(
      Phase4TrialArm::kReusableCandidateAllocation, spec, {}, preparer.get());
  allocator::internal::SetCpuCandidateAllocationFinalAssemblyFailureForTesting(false);
  Phase4TrialArmFailure failure = ErrorOf<Phase4TrialArmExecution>(std::move(result));
  ASSERT_TRUE(std::holds_alternative<Phase4CandidateSessionFailureState>(failure.payload));
  const auto& child = std::get<Phase4CandidateSessionFailureState>(failure.payload);
  EXPECT_FALSE(child.error.candidate_store_publication_committed);
  EXPECT_TRUE(child.prepared.has_candidate_store());
  EXPECT_EQ(child.case_state.board.content_hash(), child.case_state.workload.board_content_hash());
  EXPECT_EQ(child.case_state.workload.nets().size(), 6U);
  EXPECT_EQ(failure.summary.invariant_id, child.error.invariant_id);
}

TEST(Phase4PairedTrialTest, PreservationBoundaryRetainsCommittedStoreEvidence) {
  const Phase4PairedTrialSpec spec = Spec();
  Phase4RepresentativeCase corpus =
      ValueOf<Phase4RepresentativeCase>(BuildPhase4RepresentativeCaseV1(spec.case_id, {}));
  std::unique_ptr<allocator::PersistentCpuCandidatePoolPreparer> preparer = Preparer();
  allocator::PreparedCpuCandidatePools prepared =
      ValueOf<allocator::PreparedCpuCandidatePools>(allocator::PrepareInitialCpuCandidatePools(
          *preparer, corpus.board, corpus.workload, spec.preparation_config));
  allocator::CpuCandidateAllocationSessionError child;
  child.invariant_id = "test.committed.session.failure";
  child.detail = "injected committed session failure";
  child.candidate_store_publication_committed = true;

  Phase4TrialArmFailure failure = internal::PreservePhase4CandidateSessionFailureV1(
      std::move(corpus), std::move(prepared), std::move(child));

  ASSERT_TRUE(std::holds_alternative<Phase4CandidateSessionFailureState>(failure.payload));
  const auto& retained = std::get<Phase4CandidateSessionFailureState>(failure.payload);
  EXPECT_TRUE(retained.error.candidate_store_publication_committed);
  EXPECT_TRUE(retained.prepared.has_candidate_store());
  EXPECT_EQ(retained.case_state.workload.nets().size(), 6U);
  EXPECT_EQ(failure.summary.invariant_id, retained.error.invariant_id);
}

TEST(Phase4PairedTrialTest, RequiresExternalIsolationAndAllResourceAuthorities) {
  const Phase4PairedTrialSpec spec = Spec();
  Phase4TrialArmExecution execution = ValueOf<Phase4TrialArmExecution>(
      ExecutePhase4TrialArmV1(Phase4TrialArm::kSequentialBaseline, spec, {}));
  Phase4ExternalResourceObservation observation = SyntheticAuthorityForContractTest(execution);
  observation.isolated_process = false;
  observation.authority_checksum = internal::ComputePhase4ExternalAuthorityChecksumV1(observation);
  const Phase4PairedTrialError error =
      ErrorOf<Phase4TrialArmRecord>(FinalizePhase4TrialArmV1(std::move(execution), observation));
  EXPECT_EQ(error.code, Phase4PairedTrialErrorCode::kExternalAuthorityUnavailable);
}

TEST(Phase4PairedTrialTest, RequiresAuthorityLifecycleToMatchWorkerCapturedTelemetry) {
  Phase4TrialArmExecution execution = ExecuteWarmCandidate(Spec());
  Phase4ExternalResourceObservation observation = SyntheticAuthorityForContractTest(execution);
  ++observation.preparer_lifecycle.invocations_completed_before;
  observation.authority_checksum = internal::ComputePhase4ExternalAuthorityChecksumV1(observation);
  const Phase4PairedTrialError error =
      ErrorOf<Phase4TrialArmRecord>(FinalizePhase4TrialArmV1(std::move(execution), observation));
  EXPECT_EQ(error.invariant_id, "P4PAIR-FINALIZE-LIFECYCLE-001");
}

TEST(Phase4PairedTrialTest, RequiresNestedTimingIntervalsWithWidenedArithmetic) {
  const Phase4PairedTrialSpec spec = Spec();
  Phase4TrialArmExecution execution = ValueOf<Phase4TrialArmExecution>(
      ExecutePhase4TrialArmV1(Phase4TrialArm::kSequentialBaseline, spec, {}));
  const std::uint64_t interval_sum =
      execution.case_build_elapsed_nanoseconds + execution.prepared_elapsed_nanoseconds;
  ASSERT_GT(interval_sum, 0U);
  execution.cold_elapsed_nanoseconds = interval_sum - 1;
  Phase4ExternalResourceObservation observation = SyntheticAuthorityForContractTest(execution);
  observation.authority_checksum = internal::ComputePhase4ExternalAuthorityChecksumV1(observation);
  const Phase4PairedTrialError error =
      ErrorOf<Phase4TrialArmRecord>(FinalizePhase4TrialArmV1(std::move(execution), observation));
  EXPECT_EQ(error.invariant_id, "P4PAIR-FINALIZE-006");
}

TEST(Phase4PairedTrialTest, CanonicalizesNonsemanticScheduleInputOrder) {
  Phase4PairedTrialSpec ordered = Spec();
  Phase4PairedTrialSpec reversed = ordered;
  std::swap(reversed.candidate_session_config.schedules[0],
            reversed.candidate_session_config.schedules[1]);
  const Phase4TrialArmExecution ordered_execution = ValueOf<Phase4TrialArmExecution>(
      ExecutePhase4TrialArmV1(Phase4TrialArm::kSequentialBaseline, ordered, {}));
  const Phase4TrialArmExecution reversed_execution = ValueOf<Phase4TrialArmExecution>(
      ExecutePhase4TrialArmV1(Phase4TrialArm::kSequentialBaseline, reversed, {}));
  EXPECT_EQ(ordered_execution.semantics.budget_checksum,
            reversed_execution.semantics.budget_checksum);
  EXPECT_EQ(ordered_execution.semantics.semantic_checksum,
            reversed_execution.semantics.semantic_checksum);
}

TEST(Phase4PairedTrialTest, CanonicalAlgorithmBudgetChecksumBindsHiddenConfigs) {
  const Phase4PairedTrialSpec canonical = Spec();
  const std::uint64_t expected =
      internal::ComputePhase4CanonicalAlgorithmBudgetChecksumV1(canonical);
  ASSERT_NE(expected, 0U);

  Phase4PairedTrialSpec changed = canonical;
  ++changed.baseline_config.limits.maximum_trace_bytes;
  EXPECT_NE(internal::ComputePhase4CanonicalAlgorithmBudgetChecksumV1(changed), expected);

  changed = canonical;
  ++changed.preparation_config.store_config.maximum_rejection_records;
  EXPECT_NE(internal::ComputePhase4CanonicalAlgorithmBudgetChecksumV1(changed), expected);

  changed = canonical;
  ++changed.candidate_session_config.multi_world_config.maximum_pareto_comparisons;
  EXPECT_NE(internal::ComputePhase4CanonicalAlgorithmBudgetChecksumV1(changed), expected);

  changed = canonical;
  ++changed.external_budget.maximum_cold_elapsed_nanoseconds;
  --changed.corpus_limits.maximum_active_regions;
  EXPECT_EQ(internal::ComputePhase4CanonicalAlgorithmBudgetChecksumV1(changed), expected);
}

TEST(Phase4PairedTrialTest, SemanticChecksumExcludesOrderAndWorkersButArtifactIncludesThem) {
  ExecutedPair arms = ExecutePair(Spec());
  Phase4TrialArmRecord changed = arms.candidate;
  changed.semantics.execution_order = Phase4TrialOrder::kCandidateFirst;
  ++changed.semantics.preparation_worker_count;
  EXPECT_EQ(internal::ComputePhase4TrialArmSemanticChecksumV1(changed.semantics),
            arms.candidate.semantics.semantic_checksum);
  changed.artifact_checksum = internal::ComputePhase4TrialArmArtifactChecksumV1(changed);
  EXPECT_NE(changed.artifact_checksum, arms.candidate.artifact_checksum);
}

TEST(Phase4PairedTrialTest, RejectsIndividuallyAuthenticatedArmsFromDifferentPairs) {
  ExecutedPair arms = ExecutePair(Spec());
  ++arms.candidate.semantics.repetition_index;
  arms.candidate.semantics.semantic_checksum =
      internal::ComputePhase4TrialArmSemanticChecksumV1(arms.candidate.semantics);
  arms.candidate.external_observation.associated_semantic_checksum =
      arms.candidate.semantics.semantic_checksum;
  arms.candidate.external_observation.authority_checksum =
      internal::ComputePhase4ExternalAuthorityChecksumV1(arms.candidate.external_observation);
  arms.candidate.artifact_checksum =
      internal::ComputePhase4TrialArmArtifactChecksumV1(arms.candidate);
  const Phase4PairedTrialError error = ErrorOf<Phase4PairedTrialResult>(
      AssemblePhase4PairedTrialV1(std::move(arms.baseline), std::move(arms.candidate)));
  EXPECT_EQ(error.invariant_id, "P4PAIR-ASSEMBLE-002");
}

TEST(Phase4PairedTrialTest, AssemblyRevalidatesAuthorityAfterChecksumRecomputation) {
  ExecutedPair arms = ExecutePair(Spec());
  arms.candidate.external_observation.memory_authority_enforced = false;
  arms.candidate.external_observation.authority_checksum =
      internal::ComputePhase4ExternalAuthorityChecksumV1(arms.candidate.external_observation);
  arms.candidate.artifact_checksum =
      internal::ComputePhase4TrialArmArtifactChecksumV1(arms.candidate);
  const Phase4PairedTrialError error = ErrorOf<Phase4PairedTrialResult>(
      AssemblePhase4PairedTrialV1(std::move(arms.baseline), std::move(arms.candidate)));
  EXPECT_EQ(error.code, Phase4PairedTrialErrorCode::kExternalAuthorityUnavailable);
}

TEST(Phase4PairedTrialTest, AssemblyRejectsUnknownTerminalReasonAfterChecksumRecomputation) {
  ExecutedPair arms = ExecutePair(Spec());
  arms.candidate.semantics.terminal_reason = static_cast<Phase4NormalizedTerminalReason>(255);
  Reauthenticate(arms.candidate);
  const Phase4PairedTrialError error = ErrorOf<Phase4PairedTrialResult>(
      AssemblePhase4PairedTrialV1(std::move(arms.baseline), std::move(arms.candidate)));
  EXPECT_EQ(error.invariant_id, "P4PAIR-FINALIZE-001");
}

TEST(Phase4PairedTrialTest, AssemblyRejectsUnknownOutcomeSourceAfterChecksumRecomputation) {
  ExecutedPair arms = ExecutePair(Spec());
  arms.candidate.semantics.candidate_outcome_source =
      static_cast<Phase4CandidateOutcomeSource>(255);
  Reauthenticate(arms.candidate);
  const Phase4PairedTrialError error = ErrorOf<Phase4PairedTrialResult>(
      AssemblePhase4PairedTrialV1(std::move(arms.baseline), std::move(arms.candidate)));
  EXPECT_EQ(error.invariant_id, "P4PAIR-FINALIZE-001");
}

TEST(Phase4PairedTrialTest, AssemblyRejectsImpossibleAuthenticatedSemanticCounters) {
  {
    ExecutedPair arms = ExecutePair(Spec());
    arms.candidate.semantics.final_candidate_count =
        arms.candidate.semantics.outcome.selected_net_count - 1;
    Reauthenticate(arms.candidate);
    const Phase4PairedTrialError error = ErrorOf<Phase4PairedTrialResult>(
        AssemblePhase4PairedTrialV1(std::move(arms.baseline), std::move(arms.candidate)));
    EXPECT_EQ(error.invariant_id, "P4PAIR-FINALIZE-SEMANTICS-001");
  }
  {
    ExecutedPair arms = ExecutePair(Spec());
    ++arms.candidate.semantics.admitted_candidates;
    Reauthenticate(arms.candidate);
    const Phase4PairedTrialError error = ErrorOf<Phase4PairedTrialResult>(
        AssemblePhase4PairedTrialV1(std::move(arms.baseline), std::move(arms.candidate)));
    EXPECT_EQ(error.invariant_id, "P4PAIR-FINALIZE-SEMANTICS-001");
  }
  {
    ExecutedPair arms = ExecutePair(Spec());
    const std::uint64_t per_query_work = arms.candidate.semantics.opportunity.route_work_units /
                                         arms.candidate.semantics.opportunity.route_queries;
    arms.candidate.semantics.preparation_route_queries = 1;
    arms.candidate.semantics.preparation_route_work_units = per_query_work + 1;
    arms.candidate.semantics.regeneration_route_queries = 0;
    arms.candidate.semantics.regeneration_route_work_units = 0;
    arms.candidate.semantics.actual = {
        .route_queries = 1,
        .route_work_units = per_query_work + 1,
    };
    Reauthenticate(arms.candidate);
    const Phase4PairedTrialError error = ErrorOf<Phase4PairedTrialResult>(
        AssemblePhase4PairedTrialV1(std::move(arms.baseline), std::move(arms.candidate)));
    EXPECT_EQ(error.invariant_id, "P4PAIR-FINALIZE-SEMANTICS-001");
  }
}

TEST(Phase4PairedTrialTest, AssemblyRejectsZeroWorkersAfterChecksumRecomputation) {
  ExecutedPair arms = ExecutePair(Spec());
  arms.candidate.semantics.preparation_worker_count = 0;
  arms.candidate.preparer_lifecycle.workers_started_before = 0;
  arms.candidate.preparer_lifecycle.workers_started_after = 0;
  arms.candidate.external_observation.preparer_lifecycle = arms.candidate.preparer_lifecycle;
  Reauthenticate(arms.candidate);
  const Phase4PairedTrialError error = ErrorOf<Phase4PairedTrialResult>(
      AssemblePhase4PairedTrialV1(std::move(arms.baseline), std::move(arms.candidate)));
  EXPECT_EQ(error.invariant_id, "P4PAIR-FINALIZE-001");
}

TEST(Phase4PairedTrialTest, AssemblyRejectsTooManyWorkersAfterChecksumRecomputation) {
  ExecutedPair arms = ExecutePair(Spec());
  arms.candidate.semantics.preparation_worker_count =
      allocator::kMaximumPersistentCpuCandidateWorkersV1 + 1;
  arms.candidate.preparer_lifecycle.workers_started_before =
      arms.candidate.semantics.preparation_worker_count;
  arms.candidate.preparer_lifecycle.workers_started_after =
      arms.candidate.semantics.preparation_worker_count;
  arms.candidate.external_observation.preparer_lifecycle = arms.candidate.preparer_lifecycle;
  Reauthenticate(arms.candidate);
  const Phase4PairedTrialError error = ErrorOf<Phase4PairedTrialResult>(
      AssemblePhase4PairedTrialV1(std::move(arms.baseline), std::move(arms.candidate)));
  EXPECT_EQ(error.invariant_id, "P4PAIR-FINALIZE-001");
}

TEST(Phase4PairedTrialTest, AssemblyRequiresDistinctArmProcessInstances) {
  ExecutedPair arms = ExecutePair(Spec());
  arms.candidate.external_observation.process_instance_identity =
      arms.baseline.external_observation.process_instance_identity;
  arms.candidate.external_observation.authority_checksum =
      internal::ComputePhase4ExternalAuthorityChecksumV1(arms.candidate.external_observation);
  arms.candidate.artifact_checksum =
      internal::ComputePhase4TrialArmArtifactChecksumV1(arms.candidate);
  const Phase4PairedTrialError error = ErrorOf<Phase4PairedTrialResult>(
      AssemblePhase4PairedTrialV1(std::move(arms.baseline), std::move(arms.candidate)));
  EXPECT_EQ(error.invariant_id, "P4PAIR-ASSEMBLE-AUTHORITY-001");
}

}  // namespace
}  // namespace apgar::benchmark
