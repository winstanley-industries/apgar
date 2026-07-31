#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string_view>
#include <utility>
#include <variant>

#include "apgar/benchmark/phase4_paired_trial.h"
#include "apgar/benchmark/phase4_representative_corpus.h"
#include "apgar/benchmark/phase4_trial_harness.h"
#include "src/allocator/negotiated_prices_internal.h"
#include "src/benchmark/phase4_confirmatory_h4096_execution_internal.h"
#include "src/benchmark/phase4_h4096_canonical_budget_internal.h"
#include "src/benchmark/phase4_paired_trial_internal.h"
#include "tests/support/google_test.h"

namespace apgar::benchmark {
namespace {

constexpr std::string_view kCleanCommit = "0123456789abcdef0123456789abcdef01234567";

enum class RawCarrier : std::uint8_t {
  kOrdinary,
  kSameRun,
};

[[nodiscard]] Phase4CanonicalCellConfig CanonicalCell(RawCarrier carrier) {
  Phase4CanonicalCellConfig cell;
  cell.case_id = carrier == RawCarrier::kOrdinary ? 10'200U : 10'100U;
  cell.requested_pool_size = carrier == RawCarrier::kOrdinary ? 8U : 4U;
  cell.maximum_setup_elapsed_nanoseconds = 300'000'000'000ULL;
  cell.external_budget = {
      .maximum_prepared_elapsed_nanoseconds = 300'000'000'000ULL,
      .maximum_cold_elapsed_nanoseconds = 300'000'000'000ULL,
      .maximum_address_space_bytes = 64ULL * 1024ULL * 1024ULL * 1024ULL,
      .maximum_peak_host_bytes = 16ULL * 1024ULL * 1024ULL * 1024ULL,
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

[[nodiscard]] Phase4PairedTrialSpec H4096Spec(const Phase4CanonicalCellConfig& cell,
                                              std::uint32_t repetition, Phase4TrialOrder order) {
  return Built(internal::BuildPhase4CanonicalTrialSpecForCorpusV2H4096(cell, repetition, order));
}

[[nodiscard]] Phase4TrialArmSemantics AssociatedSemantics(const Phase4PairedTrialSpec& spec,
                                                          Phase4TrialArm arm) {
  Phase4RepresentativeCaseResult built =
      BuildPhase4RepresentativeCaseV2(spec.case_id, {}, spec.corpus_limits);
  EXPECT_TRUE(std::holds_alternative<Phase4RepresentativeCase>(built));
  if (!std::holds_alternative<Phase4RepresentativeCase>(built)) {
    std::abort();
  }
  const Phase4RepresentativeCase representative =
      std::get<Phase4RepresentativeCase>(std::move(built));
  const Phase4RouteOpportunity opportunity{
      .route_queries = spec.baseline_config.limits.maximum_route_queries,
      .route_work_units = spec.baseline_config.limits.maximum_total_route_work_units,
  };
  const std::uint32_t terminal_rounds =
      spec.candidate_session_config.schedules.back().maximum_selection_rounds;
  const std::uint64_t columns_per_epoch =
      spec.candidate_session_config.regeneration_plan_config.maximum_total_columns;

  Phase4TrialArmSemantics semantics;
  semantics.arm = arm;
  semantics.execution_order = spec.execution_order;
  semantics.corpus_version =
      Phase4RepresentativeCorpusVersionForAuthority(Phase4RepresentativeCorpusAuthority::kV2);
  semantics.corpus_checksum =
      Phase4RepresentativeCorpusChecksumForAuthority(Phase4RepresentativeCorpusAuthority::kV2);
  semantics.case_id = spec.case_id;
  semantics.descriptor_fingerprint = FingerprintPhase4CaseDescriptorV2(representative.descriptor);
  semantics.case_checksum = representative.case_checksum;
  semantics.board_content_hash = representative.board.content_hash();
  semantics.workload_checksum = representative.workload.workload_checksum();
  semantics.capacity_model_checksum =
      allocator::internal::RecomputeResourceCapacityModelChecksumV1(representative.capacities);
  semantics.budget_checksum = internal::ComputePhase4PairedBudgetChecksumForAuthorityV1(
      Phase4RepresentativeCorpusAuthority::kV2, spec, opportunity,
      representative.descriptor.requested_net_count, columns_per_epoch, terminal_rounds);
  semantics.workload_net_count = representative.descriptor.requested_net_count;
  semantics.requested_pool_size = spec.requested_pool_size;
  semantics.repetition_index = spec.repetition_index;
  semantics.root_seed = spec.root_seed;
  semantics.preparation_worker_count = spec.preparation_worker_count;
  semantics.baseline_sweeps = spec.baseline_config.maximum_sweeps;
  semantics.candidate_regeneration_epochs =
      spec.candidate_session_config.maximum_regeneration_epochs;
  semantics.candidate_columns_per_epoch = columns_per_epoch;
  semantics.candidate_terminal_selection_rounds = terminal_rounds;
  semantics.external_budget = spec.external_budget;
  semantics.opportunity = opportunity;
  semantics.algorithm_session_checksum = 101;
  semantics.terminal_reason = Phase4NormalizedTerminalReason::kNoAdmissibleCandidate;
  semantics.outcome.no_candidate_net_count = representative.workload.nets().size();
  semantics.outcome.world_checksum = 103;
  semantics.semantic_checksum = internal::ComputePhase4TrialArmSemanticChecksumV1(semantics);
  EXPECT_FALSE(internal::ValidatePhase4TrialArmSemanticsForAuthorityV1(
                   Phase4RepresentativeCorpusAuthority::kV2, semantics)
                   .has_value());
  return semantics;
}

[[nodiscard]] Phase4IsolatedCellResult MinimalHistoricalRaw(RawCarrier carrier) {
  Phase4IsolatedCellResult raw;
  raw.carrier = carrier == RawCarrier::kOrdinary ? Phase4IsolatedCellCarrier::kRawWireV1
                                                 : Phase4IsolatedCellCarrier::kSameRunWireV2;
  raw.config = CanonicalCell(carrier);
  raw.environment.host_os = "test-linux";
  raw.environment.host_kernel = "test-kernel";
  raw.environment.host_architecture = "test-architecture";
  raw.environment.cpu_model = "test-cpu";
  raw.environment.compiler_identity = "test-compiler";
  raw.environment.online_cpu_count = 1;
  raw.environment.affinity_cpu_count = 1;
  raw.environment.total_host_memory_bytes = 1;
  raw.corpus_checksum =
      Phase4RepresentativeCorpusChecksumForAuthority(Phase4RepresentativeCorpusAuthority::kV2);
  raw.controller_identity = 0xC011EC70ULL;

  raw.attempts.reserve(kPhase4CanonicalRepetitionsV1);
  for (std::uint32_t repetition = 0; repetition < kPhase4CanonicalRepetitionsV1; ++repetition) {
    const Phase4TrialOrder order = repetition % 2U == 0U ? Phase4TrialOrder::kBaselineFirst
                                                         : Phase4TrialOrder::kCandidateFirst;
    const Phase4PairedTrialSpec spec = H4096Spec(raw.config, repetition, order);
    Phase4IsolatedPairAttempt pair;
    pair.case_id = spec.case_id;
    pair.requested_pool_size = spec.requested_pool_size;
    pair.repetition_index = repetition;
    pair.root_seed = spec.root_seed;
    pair.execution_order = order;
    pair.baseline.arm = Phase4TrialArm::kSequentialBaseline;
    pair.baseline.repetition_index = repetition;
    pair.baseline.execution_order = order;
    pair.baseline.disposition = Phase4IsolatedAttemptDisposition::kNotRunAfterFatal;
    pair.candidate.arm = Phase4TrialArm::kReusableCandidateAllocation;
    pair.candidate.repetition_index = repetition;
    pair.candidate.execution_order = order;
    pair.candidate.disposition = Phase4IsolatedAttemptDisposition::kNotRunAfterFatal;
    raw.attempts.push_back(std::move(pair));
  }

  internal::ReauthenticatePhase4ConfirmatoryH4096RawCellForTesting(&raw);

  Phase4IsolatedArmAttempt& attempt = raw.attempts.front().baseline;
  const Phase4PairedTrialSpec spec = H4096Spec(raw.config, 0, Phase4TrialOrder::kBaselineFirst);
  Phase4TrialArmExecution execution;
  execution.semantics = AssociatedSemantics(spec, Phase4TrialArm::kSequentialBaseline);
  execution.case_build_elapsed_nanoseconds = 1;
  execution.prepared_elapsed_nanoseconds = 1;
  execution.cold_elapsed_nanoseconds = 3;
  Phase4ExternalResourceObservation observation;
  observation.authority_run_identity = raw.authority_run_identity;
  observation.controller_identity = raw.controller_identity;
  observation.process_instance_identity = 0xBA5E11EULL;
  observation.associated_semantic_checksum = execution.semantics.semantic_checksum;
  observation.configured_wall_limit_nanoseconds =
      spec.external_budget.maximum_cold_elapsed_nanoseconds;
  observation.configured_address_space_limit_bytes =
      spec.external_budget.maximum_address_space_bytes;
  observation.configured_peak_host_limit_bytes = spec.external_budget.maximum_peak_host_bytes;
  observation.outer_elapsed_nanoseconds = 4;
  observation.peak_host_bytes = 4'096;
  observation.isolated_process = true;
  observation.wall_authority_enforced = true;
  observation.memory_authority_enforced = true;
  observation.authority_checksum = internal::ComputePhase4ExternalAuthorityChecksumV1(observation);
  Phase4TrialArmRecordResult finalized =
      FinalizePhase4TrialArmForCorpusV2(std::move(execution), observation);
  EXPECT_TRUE(std::holds_alternative<Phase4TrialArmRecord>(finalized))
      << (std::holds_alternative<Phase4PairedTrialError>(finalized)
              ? std::string(std::get<Phase4PairedTrialError>(finalized).invariant_id)
              : "");
  if (!std::holds_alternative<Phase4TrialArmRecord>(finalized)) {
    std::abort();
  }

  attempt.disposition = Phase4IsolatedAttemptDisposition::kSuccess;
  attempt.dispatch_ordinal = 1;
  attempt.process_instance_identity = observation.process_instance_identity;
  attempt.outer_elapsed_nanoseconds = observation.outer_elapsed_nanoseconds;
  attempt.process_lifetime_peak_host_bytes = observation.peak_host_bytes;
  attempt.raw_wait_status = 0;
  attempt.process_exit_code = 0;
  attempt.terminating_signal = 0;
  attempt.record = std::get<Phase4TrialArmRecord>(std::move(finalized));
  internal::ReauthenticatePhase4ConfirmatoryH4096RawCellForTesting(&raw);
  return raw;
}

[[nodiscard]] Phase4TrialArmRecord& SuccessfulRecord(Phase4IsolatedCellResult* raw) {
  for (Phase4IsolatedPairAttempt& pair : raw->attempts) {
    if (pair.baseline.record.has_value()) {
      return *pair.baseline.record;
    }
    if (pair.candidate.record.has_value()) {
      return *pair.candidate.record;
    }
  }
  std::abort();
}

void ReplacePairedBudgetAndReauthenticate(Phase4IsolatedCellResult* raw,
                                          std::uint64_t paired_budget) {
  Phase4TrialArmRecord& record = SuccessfulRecord(raw);
  record.semantics.budget_checksum = paired_budget;
  record.semantics.semantic_checksum =
      internal::ComputePhase4TrialArmSemanticChecksumV1(record.semantics);
  record.external_observation.associated_semantic_checksum = record.semantics.semantic_checksum;
  record.external_observation.authority_checksum =
      internal::ComputePhase4ExternalAuthorityChecksumV1(record.external_observation);
  record.artifact_checksum = internal::ComputePhase4TrialArmArtifactChecksumV1(record);
  internal::ReauthenticatePhase4ConfirmatoryH4096RawCellForTesting(raw);

  EXPECT_EQ(record.semantics.semantic_checksum,
            internal::ComputePhase4TrialArmSemanticChecksumV1(record.semantics));
  EXPECT_EQ(record.external_observation.authority_checksum,
            internal::ComputePhase4ExternalAuthorityChecksumV1(record.external_observation));
  EXPECT_EQ(record.artifact_checksum, internal::ComputePhase4TrialArmArtifactChecksumV1(record));
  for (const Phase4IsolatedPairAttempt& pair : raw->attempts) {
    EXPECT_NE(pair.baseline.attempt_checksum, 0ULL);
    EXPECT_NE(pair.candidate.attempt_checksum, 0ULL);
    EXPECT_NE(pair.attempt_checksum, 0ULL);
  }
  const std::uint64_t expected_cell_checksum =
      raw->carrier == Phase4IsolatedCellCarrier::kRawWireV1
          ? ComputePhase4IsolatedCellArtifactChecksumV1(*raw)
          : ComputePhase4SameRunIsolatedCellArtifactChecksumV2(*raw);
  EXPECT_EQ(raw->artifact_checksum, expected_cell_checksum);
}

[[nodiscard]] std::optional<std::string> HistoricalRawJson(RawCarrier carrier,
                                                           const Phase4IsolatedCellResult& raw) {
  return carrier == RawCarrier::kOrdinary
             ? internal::SerializePhase4ConfirmatoryH4096OrdinaryCellJsonV1(raw, kCleanCommit, true,
                                                                            false)
             : internal::SerializePhase4ConfirmatoryH4096SameRunCellJsonV2(raw, kCleanCommit, true,
                                                                           false);
}

[[nodiscard]] std::optional<std::string> StructuralRawJson(RawCarrier carrier,
                                                           const Phase4IsolatedCellResult& raw) {
  return carrier == RawCarrier::kOrdinary
             ? SerializePhase4IsolatedCellJsonV1(raw, kCleanCommit, true, false)
             : SerializePhase4SameRunIsolatedCellJsonV2(raw, kCleanCommit, true, false);
}

[[nodiscard]] std::uint64_t SessionV5PairedBudget(RawCarrier carrier) {
  return carrier == RawCarrier::kOrdinary
             ? internal::kPhase4ConfirmatoryH4096SessionV5CalibrationPairedBudgetChecksum
             : internal::kPhase4ConfirmatoryH4096SessionV5ExactPairedBudgetChecksum;
}

TEST(Phase4H4096SessionV5SerializerFirewallTest,
     HistoricalRawSerializersAcceptV4AndRejectReauthenticatedV5BudgetSemantics) {
  for (const RawCarrier carrier : {RawCarrier::kOrdinary, RawCarrier::kSameRun}) {
    SCOPED_TRACE(static_cast<int>(carrier));
    const Phase4IsolatedCellResult v4 = MinimalHistoricalRaw(carrier);
    ASSERT_TRUE(HistoricalRawJson(carrier, v4).has_value());
    ASSERT_TRUE(StructuralRawJson(carrier, v4).has_value());

    Phase4IsolatedCellResult v5_budget = v4;
    const std::uint64_t predecessor_budget = SuccessfulRecord(&v5_budget).semantics.budget_checksum;
    ASSERT_NE(predecessor_budget, SessionV5PairedBudget(carrier));
    ReplacePairedBudgetAndReauthenticate(&v5_budget, SessionV5PairedBudget(carrier));
    EXPECT_TRUE(StructuralRawJson(carrier, v5_budget).has_value());
    EXPECT_FALSE(HistoricalRawJson(carrier, v5_budget).has_value());
  }
}

TEST(Phase4H4096SessionV5SerializerFirewallTest,
     SameRunTelemetryAuthenticatesRawBeforeRepresentativeCaseConstruction) {
  Phase4IsolatedCellWithSameRunDecisionTelemetryV1 v4_capture;
  v4_capture.raw_cell = MinimalHistoricalRaw(RawCarrier::kSameRun);
  v4_capture.same_run_attempts.resize(kPhase4CanonicalRepetitionsV1);

  internal::ResetPhase4RepresentativeCaseBuildProbeForTesting();
  internal::ArmPhase4RepresentativeCaseBuildProbeForTesting();
  EXPECT_FALSE(internal::SerializePhase4ConfirmatoryH4096SameRunDecisionTelemetryJsonV1(
                   v4_capture, {}, kCleanCommit, true, false)
                   .has_value());
  EXPECT_EQ(internal::Phase4RepresentativeCaseBuildProbeCountForTesting(), 1ULL);

  Phase4IsolatedCellWithSameRunDecisionTelemetryV1 v5_budget_capture = v4_capture;
  ReplacePairedBudgetAndReauthenticate(
      &v5_budget_capture.raw_cell,
      internal::kPhase4ConfirmatoryH4096SessionV5ExactPairedBudgetChecksum);
  internal::ResetPhase4RepresentativeCaseBuildProbeForTesting();
  internal::ArmPhase4RepresentativeCaseBuildProbeForTesting();
  EXPECT_FALSE(internal::SerializePhase4ConfirmatoryH4096SameRunDecisionTelemetryJsonV1(
                   v5_budget_capture, {}, kCleanCommit, true, false)
                   .has_value());
  EXPECT_EQ(internal::Phase4RepresentativeCaseBuildProbeCountForTesting(), 0ULL);
  internal::ResetPhase4RepresentativeCaseBuildProbeForTesting();
}

TEST(Phase4H4096SessionV5SerializerFirewallTest,
     SameRunTelemetryRejectsInvalidSourceBeforeRawOrCaseConstruction) {
  Phase4IsolatedCellWithSameRunDecisionTelemetryV1 capture;
  capture.raw_cell = MinimalHistoricalRaw(RawCarrier::kSameRun);
  capture.same_run_attempts.resize(kPhase4CanonicalRepetitionsV1);

  struct SourceProbe {
    std::string_view commit;
    bool stamped;
    bool dirty;
  };
  constexpr SourceProbe probes[] = {
      {.commit = kCleanCommit, .stamped = false, .dirty = false},
      {.commit = kCleanCommit, .stamped = true, .dirty = true},
      {.commit = "0123456789abcdef0123456789abcdef0123456", .stamped = true, .dirty = false},
      {.commit = "0123456789ABCDEF0123456789ABCDEF01234567", .stamped = true, .dirty = false},
      {.commit = "g123456789abcdef0123456789abcdef01234567", .stamped = true, .dirty = false},
  };
  for (const SourceProbe& probe : probes) {
    SCOPED_TRACE(probe.commit);
    internal::ResetPhase4RepresentativeCaseBuildProbeForTesting();
    internal::ArmPhase4RepresentativeCaseBuildProbeForTesting();
    EXPECT_FALSE(internal::SerializePhase4ConfirmatoryH4096SameRunDecisionTelemetryJsonV1(
                     capture, {}, probe.commit, probe.stamped, probe.dirty)
                     .has_value());
    EXPECT_EQ(internal::Phase4RepresentativeCaseBuildProbeCountForTesting(), 0ULL);
  }
  internal::ResetPhase4RepresentativeCaseBuildProbeForTesting();
}

}  // namespace
}  // namespace apgar::benchmark
