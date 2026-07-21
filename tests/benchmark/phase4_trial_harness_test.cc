#include "apgar/benchmark/phase4_trial_harness.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include "apgar/benchmark/phase4_representative_corpus.h"
#include "src/benchmark/phase4_paired_trial_internal.h"
#include "tests/support/google_test.h"

namespace apgar::benchmark {
namespace {

[[nodiscard]] Phase4CanonicalCellConfig Cell(std::uint32_t case_id = 100,
                                             std::uint32_t pool_size = 4) {
  Phase4CanonicalCellConfig cell;
  cell.case_id = case_id;
  cell.requested_pool_size = pool_size;
  cell.preparation_worker_count = 4;
  cell.repetitions = 20;
  cell.maximum_setup_elapsed_nanoseconds = 60'000'000'000ULL;
  cell.external_budget = {
      .maximum_prepared_elapsed_nanoseconds = 120'000'000'000ULL,
      .maximum_cold_elapsed_nanoseconds = 180'000'000'000ULL,
      .maximum_address_space_bytes = 64ULL * 1024ULL * 1024ULL * 1024ULL,
      .maximum_peak_host_bytes = 32ULL * 1024ULL * 1024ULL * 1024ULL,
  };
  return cell;
}

[[nodiscard]] Phase4PairedTrialSpec Built(Phase4CanonicalSpecResult result) {
  EXPECT_TRUE(std::holds_alternative<Phase4PairedTrialSpec>(result))
      << (std::holds_alternative<Phase4TrialHarnessError>(result)
              ? std::get<Phase4TrialHarnessError>(result).detail
              : "");
  if (!std::holds_alternative<Phase4PairedTrialSpec>(result)) {
    std::abort();
  }
  return std::get<Phase4PairedTrialSpec>(std::move(result));
}

[[nodiscard]] const Phase4TrialHarnessError& Rejected(const Phase4CanonicalSpecResult& result) {
  EXPECT_TRUE(std::holds_alternative<Phase4TrialHarnessError>(result));
  if (!std::holds_alternative<Phase4TrialHarnessError>(result)) {
    std::abort();
  }
  return std::get<Phase4TrialHarnessError>(result);
}

[[nodiscard]] Phase4RepresentativeCase BuiltCase(std::uint32_t case_id) {
  Phase4RepresentativeCaseResult result = BuildPhase4RepresentativeCaseV1(case_id, {});
  EXPECT_TRUE(std::holds_alternative<Phase4RepresentativeCase>(result));
  if (!std::holds_alternative<Phase4RepresentativeCase>(result)) {
    std::abort();
  }
  return std::get<Phase4RepresentativeCase>(std::move(result));
}

TEST(Phase4TrialHarnessTest, BuildsExactEqualOpportunityMathForEveryCanonicalPool) {
  for (const std::uint32_t pool_size : {4U, 8U, 16U}) {
    const Phase4CanonicalCellConfig cell = Cell(200, pool_size);
    const Phase4CaseDescriptor* descriptor = FindPhase4CaseDescriptorV1(cell.case_id);
    ASSERT_NE(descriptor, nullptr);
    const std::uint64_t net_count = descriptor->requested_net_count;
    const std::uint64_t sweeps = pool_size + 2;
    const std::uint64_t route_work_limit = kPhase4CanonicalRouteWorkUnitsPerQueryV1;

    const Phase4PairedTrialSpec spec =
        Built(BuildPhase4CanonicalTrialSpecV1(cell, 7, Phase4TrialOrder::kCandidateFirst));
    EXPECT_EQ(spec.case_id, cell.case_id);
    EXPECT_EQ(spec.requested_pool_size, pool_size);
    EXPECT_EQ(spec.repetition_index, 7U);
    EXPECT_EQ(spec.execution_order, Phase4TrialOrder::kCandidateFirst);
    EXPECT_EQ(spec.preparation_worker_count, 4U);
    EXPECT_EQ(spec.external_budget, cell.external_budget);
    EXPECT_EQ(spec.corpus_limits, cell.corpus_limits);
    EXPECT_NE(spec.root_seed, 0U);

    EXPECT_EQ(spec.baseline_config.maximum_sweeps, sweeps);
    EXPECT_EQ(spec.baseline_config.limits.maximum_route_queries, net_count * sweeps);
    EXPECT_EQ(spec.baseline_config.limits.maximum_total_route_work_units,
              net_count * sweeps * route_work_limit);
    EXPECT_EQ(spec.preparation_config.requested_candidates_per_net, pool_size);
    EXPECT_EQ(spec.preparation_config.limits.maximum_route_queries, net_count * pool_size);
    EXPECT_EQ(spec.candidate_session_config.maximum_regeneration_epochs, 2U);
    EXPECT_EQ(spec.candidate_session_config.regeneration_plan_config.maximum_total_columns,
              net_count);
    EXPECT_EQ(spec.candidate_session_config.limits.maximum_total_route_queries, net_count * 2);
    EXPECT_EQ(spec.preparation_config.limits.maximum_route_queries +
                  spec.candidate_session_config.limits.maximum_total_route_queries,
              spec.baseline_config.limits.maximum_route_queries);
    EXPECT_EQ(spec.preparation_config.limits.maximum_total_route_work_units +
                  spec.candidate_session_config.limits.maximum_total_route_work_units,
              spec.baseline_config.limits.maximum_total_route_work_units);
    ASSERT_EQ(spec.candidate_session_config.schedules.size(), 2U);
    EXPECT_EQ(spec.candidate_session_config.schedules[1].maximum_selection_rounds, pool_size + 1);
    EXPECT_EQ(spec.candidate_session_config.multi_world_config.maximum_total_selection_rounds,
              pool_size + 2);
  }
}

TEST(Phase4TrialHarnessTest, RootSeedIsCellScopedAndIndependentOfOrderAndRepetition) {
  const Phase4CanonicalCellConfig cell = Cell(200, 4);
  const Phase4PairedTrialSpec first =
      Built(BuildPhase4CanonicalTrialSpecV1(cell, 0, Phase4TrialOrder::kBaselineFirst));
  const Phase4PairedTrialSpec last =
      Built(BuildPhase4CanonicalTrialSpecV1(cell, 19, Phase4TrialOrder::kCandidateFirst));
  EXPECT_EQ(first.root_seed, last.root_seed);
  EXPECT_EQ(first.baseline_config.deterministic_seed, first.root_seed);
  EXPECT_EQ(first.preparation_config.deterministic_seed, first.root_seed);
  EXPECT_EQ(first.candidate_session_config.regeneration_execution_config.deterministic_seed,
            first.root_seed);

  const Phase4PairedTrialSpec other_pool =
      Built(BuildPhase4CanonicalTrialSpecV1(Cell(200, 8), 0, Phase4TrialOrder::kBaselineFirst));
  EXPECT_NE(first.root_seed, other_pool.root_seed);
}

TEST(Phase4TrialHarnessTest, FrozenManifestPinsCanonicalAlgorithmBudgetChecksums) {
  const Phase4PairedTrialSpec exact =
      Built(BuildPhase4CanonicalTrialSpecV1(Cell(100, 4), 0, Phase4TrialOrder::kBaselineFirst));
  EXPECT_EQ(internal::ComputePhase4CanonicalAlgorithmBudgetChecksumV1(exact),
            5'568'679'732'527'683'289ULL);

  const Phase4PairedTrialSpec held_out = Built(
      BuildPhase4CanonicalTrialSpecV1(Cell(1'200, 16), 19, Phase4TrialOrder::kCandidateFirst));
  EXPECT_EQ(internal::ComputePhase4CanonicalAlgorithmBudgetChecksumV1(held_out),
            17'882'871'627'995'708'506ULL);
}

TEST(Phase4TrialHarnessTest, RejectsEveryInvalidCellAxisBeforeBuildingWork) {
  {
    Phase4CanonicalCellConfig cell = Cell();
    cell.schema_version += 1;
    EXPECT_EQ(Rejected(BuildPhase4CanonicalTrialSpecV1(cell, 0, Phase4TrialOrder::kBaselineFirst))
                  .invariant_id,
              "P4HARNESS-SPEC-001");
  }
  for (const auto mutation : {
           +[](Phase4CanonicalCellConfig* cell) { cell->preparation_worker_count = 0; },
           +[](Phase4CanonicalCellConfig* cell) {
             cell->preparation_worker_count =
                 allocator::kMaximumPersistentCpuCandidateWorkersV1 + 1U;
           },
           +[](Phase4CanonicalCellConfig* cell) { cell->repetitions = 0; },
           +[](Phase4CanonicalCellConfig* cell) { cell->repetitions = 21; },
           +[](Phase4CanonicalCellConfig* cell) { cell->maximum_setup_elapsed_nanoseconds = 0; },
           +[](Phase4CanonicalCellConfig* cell) {
             cell->maximum_setup_elapsed_nanoseconds = kPhase4MaximumWatchdogNanosecondsV1 + 1;
           },
           +[](Phase4CanonicalCellConfig* cell) {
             cell->external_budget.maximum_prepared_elapsed_nanoseconds = 0;
           },
           +[](Phase4CanonicalCellConfig* cell) {
             cell->external_budget.maximum_cold_elapsed_nanoseconds = 0;
           },
           +[](Phase4CanonicalCellConfig* cell) {
             cell->external_budget.maximum_cold_elapsed_nanoseconds =
                 kPhase4MaximumWatchdogNanosecondsV1 + 1;
           },
           +[](Phase4CanonicalCellConfig* cell) {
             cell->external_budget.maximum_prepared_elapsed_nanoseconds =
                 cell->external_budget.maximum_cold_elapsed_nanoseconds + 1;
           },
           +[](Phase4CanonicalCellConfig* cell) {
             cell->external_budget.maximum_address_space_bytes = 0;
           },
           +[](Phase4CanonicalCellConfig* cell) {
             cell->external_budget.maximum_address_space_bytes =
                 std::numeric_limits<std::uint64_t>::max();
           },
           +[](Phase4CanonicalCellConfig* cell) {
             cell->external_budget.maximum_peak_host_bytes = 0;
           },
           +[](Phase4CanonicalCellConfig* cell) { cell->corpus_limits.maximum_nets = 0; },
           +[](Phase4CanonicalCellConfig* cell) {
             cell->corpus_limits.maximum_nets = kMaximumPhase4RepresentativeNetsV1 + 1ULL;
           },
           +[](Phase4CanonicalCellConfig* cell) {
             cell->corpus_limits.maximum_compiled_nodes += 1;
           },
           +[](Phase4CanonicalCellConfig* cell) {
             cell->corpus_limits.maximum_compiled_host_bytes += 1;
           },
           +[](Phase4CanonicalCellConfig* cell) {
             cell->corpus_limits.maximum_active_regions = kMaximumPhase4ActiveRegionsV1 + 1;
           },
           +[](Phase4CanonicalCellConfig* cell) {
             cell->corpus_limits.maximum_board_entities = kMaximumPhase4BoardEntitiesV1 + 1;
           },
       }) {
    Phase4CanonicalCellConfig cell = Cell();
    mutation(&cell);
    EXPECT_EQ(Rejected(BuildPhase4CanonicalTrialSpecV1(cell, 0, Phase4TrialOrder::kBaselineFirst))
                  .invariant_id,
              "P4HARNESS-SPEC-001");
  }

  EXPECT_EQ(
      Rejected(BuildPhase4CanonicalTrialSpecV1(Cell(100, 5), 0, Phase4TrialOrder::kBaselineFirst))
          .invariant_id,
      "P4HARNESS-SPEC-001");
  EXPECT_EQ(
      Rejected(BuildPhase4CanonicalTrialSpecV1(Cell(100, 8), 0, Phase4TrialOrder::kBaselineFirst))
          .invariant_id,
      "P4HARNESS-SPEC-002");
  EXPECT_EQ(Rejected(BuildPhase4CanonicalTrialSpecV1(Cell(), 20, Phase4TrialOrder::kBaselineFirst))
                .invariant_id,
            "P4HARNESS-SPEC-001");
  EXPECT_EQ(Rejected(BuildPhase4CanonicalTrialSpecV1(Cell(), 0, static_cast<Phase4TrialOrder>(255)))
                .invariant_id,
            "P4HARNESS-SPEC-001");
}

TEST(Phase4TrialHarnessTest, ReconcilesCorpusFailureWithoutFlatteningBoundWitnesses) {
  Phase4RepresentativeCorpusError child{
      .code = Phase4RepresentativeCorpusErrorCode::kWorkBoundExceeded,
      .invariant_id = "corpus.work.bound",
      .detail = "both bounds exceeded",
      .requested_case_id = 3'002,
      .limiting_work_bound = Phase4RepresentativeWorkBound::kCompiledNodesAndHostBytes,
      .maximum_preparable_net_count = 4'000,
      .first_unpreparable_net = board_ir::EntityRef{.id = 800, .generation = 9},
      .required_compiled_nodes = 101,
      .configured_compiled_node_limit = 100,
      .required_compiled_host_bytes = 201,
      .configured_compiled_host_byte_limit = 200,
  };
  Phase4TrialArmFailure failure{
      .summary =
          Phase4PairedTrialError{
              .code = Phase4PairedTrialErrorCode::kCaseBuild,
              .invariant_id = "summary.case",
              .detail = "case failed",
              .arm = Phase4TrialArm::kSequentialBaseline,
              .required = 101,
              .configured = 100,
          },
      .payload = child,
  };

  const Phase4DurableArmFailure durable = ReconcilePhase4TrialArmFailureV1(std::move(failure));
  EXPECT_EQ(durable.summary_code, Phase4PairedTrialErrorCode::kCaseBuild);
  EXPECT_EQ(durable.payload_kind, Phase4DurableFailurePayloadKind::kCorpus);
  EXPECT_EQ(durable.child_error_code, static_cast<std::uint8_t>(child.code));
  EXPECT_EQ(durable.child_invariant_id, child.invariant_id);
  EXPECT_TRUE(durable.has_child_net);
  EXPECT_EQ(durable.child_net_id, 800U);
  EXPECT_EQ(durable.child_net_generation, 9U);
  EXPECT_EQ(durable.child_required, 101U);
  EXPECT_EQ(durable.child_configured, 100U);
  EXPECT_EQ(durable.child_bound_kind,
            static_cast<std::uint8_t>(Phase4RepresentativeWorkBound::kCompiledNodesAndHostBytes));
  EXPECT_EQ(durable.child_secondary_required, 201U);
  EXPECT_EQ(durable.child_secondary_configured, 200U);
  EXPECT_EQ(durable.case_id, 3'002U);
  EXPECT_EQ(durable.attempted_column_count, 4'000U);
  EXPECT_NE(durable.payload_checksum, 0U);
}

TEST(Phase4TrialHarnessTest, ReconcilesCaseIdentityAndTypedSequentialDiagnostic) {
  Phase4RepresentativeCase case_state = BuiltCase(100);
  const std::uint64_t expected_case_checksum = case_state.case_checksum;
  const std::uint64_t expected_board_hash = case_state.board.content_hash();
  const std::uint64_t expected_workload_checksum = case_state.workload.workload_checksum();
  Phase4TrialArmFailure failure{
      .summary =
          Phase4PairedTrialError{
              .code = Phase4PairedTrialErrorCode::kSequentialExecution,
              .invariant_id = "summary.sequential",
              .detail = "sequential failed",
              .arm = Phase4TrialArm::kSequentialBaseline,
          },
      .payload =
          Phase4SequentialFailureState{
              .case_state = std::move(case_state),
              .error =
                  allocator::SequentialNegotiatedBaselineError{
                      .code = allocator::SequentialNegotiatedBaselineErrorCode::kWorkBoundExceeded,
                      .invariant_id = "sequential.work",
                      .detail = "work exhausted",
                      .net = board_ir::EntityRef{.id = 44, .generation = 2},
                      .required = 12,
                      .configured = 11,
                  },
          },
  };

  const Phase4DurableArmFailure durable = ReconcilePhase4TrialArmFailureV1(std::move(failure));
  EXPECT_EQ(durable.payload_kind, Phase4DurableFailurePayloadKind::kSequential);
  EXPECT_TRUE(durable.has_case_identity);
  EXPECT_EQ(durable.case_id, 100U);
  EXPECT_EQ(durable.case_checksum, expected_case_checksum);
  EXPECT_EQ(durable.board_content_hash, expected_board_hash);
  EXPECT_EQ(durable.workload_checksum, expected_workload_checksum);
  EXPECT_NE(durable.descriptor_fingerprint, 0U);
  EXPECT_NE(durable.capacity_model_checksum, 0U);
  EXPECT_TRUE(durable.has_child_net);
  EXPECT_EQ(durable.child_net_id, 44U);
  EXPECT_EQ(durable.child_required, 12U);
  EXPECT_EQ(durable.child_configured, 11U);
  EXPECT_NE(durable.payload_checksum, 0U);
}

TEST(Phase4TrialHarnessTest, PreservesFailedPreparationObservationWithoutClaimingStore) {
  Phase4RepresentativeCase case_state = BuiltCase(100);
  allocator::CpuCandidatePoolFailedPreparationObservation observation;
  observation.counters.route_queries = 17;
  observation.counters.route_work_units = 18;
  observation.attempted_columns.resize(17);
  observation.candidate_store_publication_committed = false;
  observation.observation_checksum = 19;
  Phase4TrialArmFailure failure{
      .summary =
          Phase4PairedTrialError{
              .code = Phase4PairedTrialErrorCode::kCandidatePreparation,
              .invariant_id = "summary.preparation",
              .detail = "preparation failed",
              .arm = Phase4TrialArm::kReusableCandidateAllocation,
          },
      .payload =
          Phase4CandidatePreparationFailureState{
              .case_state = std::move(case_state),
              .error =
                  allocator::CpuCandidatePoolPreparationError{
                      .code = allocator::CpuCandidatePoolPreparationErrorCode::kWorkBoundExceeded,
                      .invariant_id = "preparation.work",
                      .detail = "preparation work exhausted",
                      .net = std::nullopt,
                      .required = 20,
                      .configured = 16,
                      .failed_preparation = std::move(observation),
                  },
          },
  };

  const Phase4DurableArmFailure durable = ReconcilePhase4TrialArmFailureV1(std::move(failure));
  EXPECT_EQ(durable.payload_kind, Phase4DurableFailurePayloadKind::kCandidatePreparation);
  EXPECT_TRUE(durable.has_case_identity);
  EXPECT_TRUE(durable.has_failed_observation);
  EXPECT_EQ(durable.failed_observation_checksum, 19U);
  EXPECT_EQ(durable.attempted_column_count, 17U);
  EXPECT_EQ(durable.attempted_route_queries, 17U);
  EXPECT_EQ(durable.attempted_route_work_units, 18U);
  EXPECT_FALSE(durable.candidate_store_publication_committed);
  EXPECT_FALSE(durable.authoritative_candidate_store_present);
  EXPECT_EQ(durable.reconciled_candidate_count, 0U);
  EXPECT_EQ(durable.reconciled_rejection_count, 0U);
  EXPECT_EQ(durable.reconciled_candidate_store_checksum, 0U);
  EXPECT_NE(durable.payload_checksum, 0U);
}

TEST(Phase4TrialHarnessTest, SerializesCanonicalDeterministicJsonWithFullEscaping) {
  Phase4IsolatedCellResult result;
  result.config = Cell();
  result.environment.host_os = "linux\n\t\"\\";
  result.environment.host_kernel = std::string("kernel\x01", 7);
  result.environment.host_architecture = "x86_64";
  result.environment.cpu_model = "model";
  result.environment.compiler_identity = "clang";
  result.environment.online_cpu_count = 8;
  result.environment.affinity_cpu_count = 4;
  result.environment.total_host_memory_bytes = 123;
  result.environment.environment_checksum = 456;
  result.corpus_checksum = 1;
  result.cell_plan_checksum = 2;
  result.authority_run_identity = 3;
  result.controller_identity = 4;
  Phase4IsolatedPairAttempt attempt;
  attempt.case_id = 100;
  attempt.requested_pool_size = 4;
  attempt.repetition_index = 0;
  attempt.root_seed = 5;
  attempt.baseline.controller_detail = "line1\nline2";
  attempt.candidate.controller_invariant_id = "quoted\"value";
  attempt.attempt_checksum = 6;
  result.attempts.push_back(std::move(attempt));
  result.artifact_checksum = 7;

  const std::string first = SerializePhase4IsolatedCellJsonV1(result, "commit\"\\\n", true, false);
  const std::string second = SerializePhase4IsolatedCellJsonV1(result, "commit\"\\\n", true, false);
  EXPECT_EQ(first, second);
  ASSERT_FALSE(first.empty());
  EXPECT_EQ(first.front(), '{');
  EXPECT_EQ(first.back(), '\n');
  EXPECT_EQ(std::count(first.begin(), first.end(), '\n'), 1);
  EXPECT_EQ(first.find('\t'), std::string::npos);
  EXPECT_NE(first.find("\"source_commit\":\"commit\\\"\\\\\\n\""), std::string::npos);
  EXPECT_NE(first.find("\"host_os\":\"linux\\n\\t\\\"\\\\\""), std::string::npos);
  EXPECT_NE(first.find("\"host_kernel\":\"kernel\\u0001\""), std::string::npos);
  EXPECT_NE(first.find("\"record\":null,\"child_failure\":null"), std::string::npos);
  const std::uint64_t source_envelope_checksum = ComputePhase4SourceEnvelopeChecksumV1(
      kPhase4TrialWireSchemaVersion, "commit\"\\\n", true, false, result.artifact_checksum);
  EXPECT_NE(source_envelope_checksum, 0U);
  EXPECT_NE(first.find("\"source_envelope_checksum\":" + std::to_string(source_envelope_checksum)),
            std::string::npos);
  EXPECT_LT(first.find("\"config\":"), first.find("\"environment\":"));
  EXPECT_LT(first.find("\"environment\":"), first.find("\"attempts\":"));
  EXPECT_LT(first.find("\"source_tree_dirty\":false"), first.find("\"source_envelope_checksum\":"));
  EXPECT_LT(first.find("\"source_envelope_checksum\":"), first.find("\"schema_version\":"));
}

TEST(Phase4TrialHarnessTest, SourceEnvelopeChecksumBindsEveryProvenanceFieldAndArtifact) {
  constexpr std::string_view kCommit = "0123456789abcdef0123456789abcdef01234567";
  constexpr std::uint64_t kArtifactChecksum = 0x0123456789abcdefULL;
  const std::uint64_t checksum = ComputePhase4SourceEnvelopeChecksumV1(
      kPhase4TrialWireSchemaVersion, kCommit, true, false, kArtifactChecksum);

  EXPECT_NE(checksum, 0U);
  EXPECT_NE(ComputePhase4SourceEnvelopeChecksumV1(kPhase4TrialWireSchemaVersion + 1U, kCommit, true,
                                                  false, kArtifactChecksum),
            checksum);
  EXPECT_NE(ComputePhase4SourceEnvelopeChecksumV1(kPhase4TrialWireSchemaVersion, "different", true,
                                                  false, kArtifactChecksum),
            checksum);
  EXPECT_NE(ComputePhase4SourceEnvelopeChecksumV1(kPhase4TrialWireSchemaVersion, kCommit, false,
                                                  false, kArtifactChecksum),
            checksum);
  EXPECT_NE(ComputePhase4SourceEnvelopeChecksumV1(kPhase4TrialWireSchemaVersion, kCommit, true,
                                                  true, kArtifactChecksum),
            checksum);
  EXPECT_NE(ComputePhase4SourceEnvelopeChecksumV1(kPhase4TrialWireSchemaVersion, kCommit, true,
                                                  false, kArtifactChecksum + 1U),
            checksum);
}

}  // namespace
}  // namespace apgar::benchmark
