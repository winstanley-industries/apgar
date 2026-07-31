#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <variant>

#include "apgar/allocator/cpu_candidate_allocation_session.h"
#include "apgar/benchmark/phase3_commit.h"
#include "src/benchmark/phase4_confirmatory_h4096_session_v5_execution_internal.h"
#include "src/benchmark/phase4_h4096_session_v5_canonical_budget_internal.h"

namespace apgar::benchmark::internal {
namespace {

constexpr std::uint64_t kProtocolChecksumV3 = 4'963'299'999'381'388'941ULL;
constexpr std::uint64_t kProtocolChecksumV2 = 11'520'586'171'987'743'043ULL;
constexpr std::uint64_t kBudgetRosterChecksumV4 = 12'316'700'735'749'461'907ULL;
constexpr std::uint64_t kBudgetRosterChecksumV3 = 18'429'170'436'700'418'962ULL;
constexpr std::uint64_t kCorpusChecksumV2 = 4'182'833'841'936'446'798ULL;
constexpr std::uint64_t kRepresentativeManifestChecksumV2 = 9'613'362'670'139'358'355ULL;
constexpr std::uint64_t kWorkloadRosterManifestChecksumV2 = 14'986'327'048'461'036'142ULL;
constexpr std::uint64_t kCanonicalSetupElapsedNanoseconds = 300'000'000'000ULL;
constexpr std::uint64_t kCanonicalPreparedElapsedNanoseconds = 300'000'000'000ULL;
constexpr std::uint64_t kCanonicalColdElapsedNanoseconds = 300'000'000'000ULL;
constexpr std::uint64_t kCanonicalAddressSpaceBytes = 64ULL * 1024ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t kCanonicalPeakHostBytes = 16ULL * 1024ULL * 1024ULL * 1024ULL;

[[nodiscard]] Phase4PairedTrialError Error(Phase4PairedTrialErrorCode code,
                                           std::string_view invariant_id, std::string_view detail,
                                           Phase4TrialArm arm = Phase4TrialArm::kSequentialBaseline,
                                           std::uint64_t required = 0,
                                           std::uint64_t configured = 0) noexcept {
  return Phase4PairedTrialError{
      .code = code,
      .invariant_id = invariant_id,
      .detail = detail,
      .arm = arm,
      .required = required,
      .configured = configured,
  };
}

[[nodiscard]] bool ValidCarrier(Phase4H4096SessionV5Carrier carrier) noexcept {
  return carrier == Phase4H4096SessionV5Carrier::kOrdinary ||
         carrier == Phase4H4096SessionV5Carrier::kSameRun;
}

[[nodiscard]] bool ValidEndpoint(Phase4H4096SessionV5Endpoint endpoint) noexcept {
  return endpoint == Phase4H4096SessionV5Endpoint::kController ||
         endpoint == Phase4H4096SessionV5Endpoint::kWorker;
}

[[nodiscard]] bool ValidRole(Phase4H4096SessionV5CaseRole role) noexcept {
  return role == Phase4H4096SessionV5CaseRole::kExact ||
         role == Phase4H4096SessionV5CaseRole::kCalibration;
}

[[nodiscard]] bool ValidArm(Phase4TrialArm arm) noexcept {
  return arm == Phase4TrialArm::kSequentialBaseline ||
         arm == Phase4TrialArm::kReusableCandidateAllocation;
}

[[nodiscard]] std::optional<Phase4PairedTrialError> ValidateIdentity(
    const Phase4H4096SessionV5PreflightIdentity& identity, Phase4TrialArm arm) {
  if (!ValidCarrier(identity.carrier) || !ValidEndpoint(identity.endpoint) ||
      !ValidRole(identity.case_role) || !ValidArm(arm)) {
    return Error(Phase4PairedTrialErrorCode::kInvalidConfiguration,
                 "P4PAIR-H4096-SESSION-V5-ENUM-001",
                 "the Session-v5 carrier, endpoint, role, and arm must be known values", arm);
  }
  if (identity.execution_authority != Phase4TrialExecutionAuthority::kCorpusV2H4096SessionV5) {
    return Error(Phase4PairedTrialErrorCode::kInvalidConfiguration,
                 "P4PAIR-H4096-SESSION-V5-EXECUTION-AUTHORITY-001",
                 "the separately compiled Session-v5 boundary requires its reserved execution "
                 "identity",
                 arm);
  }
  const Phase4H4096SessionV5PreflightIdentity expected =
      BuildPhase4ConfirmatoryH4096SessionV5PreflightIdentity(identity.carrier, identity.endpoint);
  if (!(identity == expected)) {
    return Error(Phase4PairedTrialErrorCode::kMeasurementAssociation,
                 "P4PAIR-H4096-SESSION-V5-AUTHORITY-001",
                 "the complete Protocol-v3, roster-v4, artifact, carrier, and budget authority "
                 "preimage does not match the compiled Session-v5 identity",
                 arm);
  }
  return std::nullopt;
}

[[nodiscard]] bool EquivalentSpec(const Phase4PairedTrialSpec& left,
                                  const Phase4PairedTrialSpec& right) {
  return left.schema_version == right.schema_version && left.case_id == right.case_id &&
         left.requested_pool_size == right.requested_pool_size &&
         left.repetition_index == right.repetition_index && left.root_seed == right.root_seed &&
         left.execution_order == right.execution_order &&
         left.preparation_worker_count == right.preparation_worker_count &&
         left.corpus_limits == right.corpus_limits &&
         left.baseline_config == right.baseline_config &&
         left.preparation_config == right.preparation_config &&
         left.candidate_session_config == right.candidate_session_config &&
         left.external_budget == right.external_budget;
}

[[nodiscard]] std::uint32_t MaximumTerminalSelectionRounds(
    const allocator::CpuCandidateAllocationSessionConfig& config) noexcept {
  std::uint32_t maximum = 0;
  for (const allocator::MultiWorldSchedule& schedule : config.schedules) {
    maximum = std::max(maximum, schedule.maximum_selection_rounds);
  }
  return maximum;
}

[[nodiscard]] std::optional<Phase4PairedTrialError> ValidateCell(
    const Phase4H4096SessionV5PreflightIdentity& identity, const Phase4CanonicalCellConfig& cell,
    Phase4TrialArm arm) {
  if (std::optional<Phase4PairedTrialError> error = ValidateIdentity(identity, arm);
      error.has_value()) {
    return error;
  }
  const Phase4CanonicalCellConfig expected =
      BuildPhase4ConfirmatoryH4096SessionV5CanonicalCell(identity.carrier);
  if (!(cell == expected)) {
    return Error(
        Phase4PairedTrialErrorCode::kInvalidConfiguration, "P4PAIR-H4096-SESSION-V5-CELL-001",
        "the Session-v5 boundary requires the complete fixed canonical cell preimage", arm);
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<Phase4PairedTrialError> ValidateEmbeddedSource(
    std::string_view embedded_commit, bool source_stamped, bool source_tree_dirty,
    Phase4TrialArm arm) {
  if (!IsPublishableEmbeddedBenchmarkSource(embedded_commit, source_stamped, source_tree_dirty)) {
    return Error(Phase4PairedTrialErrorCode::kMeasurementAssociation,
                 "P4PAIR-H4096-SESSION-V5-SOURCE-001",
                 "Session-v5 preflight requires a clean stamped embedded source commit", arm);
  }
  return std::nullopt;
}

}  // namespace

Phase4H4096SessionV5PreflightIdentity BuildPhase4ConfirmatoryH4096SessionV5PreflightIdentity(
    Phase4H4096SessionV5Carrier carrier, Phase4H4096SessionV5Endpoint endpoint) noexcept {
  const bool same_run = carrier == Phase4H4096SessionV5Carrier::kSameRun;
  return Phase4H4096SessionV5PreflightIdentity{
      .execution_authority = Phase4TrialExecutionAuthority::kCorpusV2H4096SessionV5,
      .carrier = carrier,
      .endpoint = endpoint,
      .case_role = same_run ? Phase4H4096SessionV5CaseRole::kExact
                            : Phase4H4096SessionV5CaseRole::kCalibration,
      .protocol_schema_version = 3,
      .protocol_checksum = kProtocolChecksumV3,
      .superseded_protocol_schema_version = 2,
      .superseded_protocol_checksum = kProtocolChecksumV2,
      .budget_roster_authority = "phase4_confirmatory_canonical_algorithm_budget_roster_v4",
      .budget_roster_schema_version = 4,
      .budget_roster_checksum = kBudgetRosterChecksumV4,
      .superseded_budget_roster_authority =
          "phase4_confirmatory_canonical_algorithm_budget_roster_v3",
      .superseded_budget_roster_schema_version = 3,
      .superseded_budget_roster_checksum = kBudgetRosterChecksumV3,
      .corpus_version = 2,
      .corpus_checksum = kCorpusChecksumV2,
      .representative_manifest_schema_version = 2,
      .representative_manifest_checksum = kRepresentativeManifestChecksumV2,
      .workload_roster_manifest_schema_version = 2,
      .workload_roster_manifest_checksum = kWorkloadRosterManifestChecksumV2,
      .configuration_authority = "phase4_confirmatory_corpus_v2_h4096_session_v5",
      .superseded_configuration_authority = "phase4_confirmatory_corpus_v2_h4096",
      .candidate_session_schema_version = allocator::kCpuCandidateAllocationSessionSchemaVersionV5,
      .targeted_regeneration_plan_schema_version = kPhase4H4096SessionV5PlanSchemaVersion,
      .targeted_regeneration_execution_schema_version = kPhase4H4096SessionV5ExecutionSchemaVersion,
      .present_step_per_overuse_unit = kPhase4CorpusV2ProtocolV1PresentStepPerOveruseUnit,
      .history_step_per_overuse_unit = kPhase4CorpusV2H4096HistoryStepPerOveruseUnit,
      .raw_authority = same_run ? "phase4_confirmatory_same_run_raw_evidence_v3"
                                : "phase4_confirmatory_raw_evidence_v3",
      .superseded_raw_authority = same_run ? "phase4_confirmatory_same_run_raw_evidence_v2"
                                           : "phase4_confirmatory_raw_evidence_v2",
      .raw_schema_version =
          same_run ? kPhase4SameRunRawEvidenceSchemaVersion : kPhase4TrialHarnessSchemaVersion,
      .raw_wire_schema_version =
          same_run ? kPhase4SameRunTrialWireSchemaVersion : kPhase4TrialWireSchemaVersion,
      .telemetry_required = same_run,
      .telemetry_authority = same_run ? "phase4_confirmatory_same_run_decision_telemetry_v3" : "",
      .superseded_telemetry_authority =
          same_run ? "phase4_confirmatory_same_run_decision_telemetry_v2" : "",
      .telemetry_schema_version = same_run ? kPhase4IsolatedSameRunTelemetrySchemaVersion : 0,
      .telemetry_wire_schema_version = same_run ? kPhase4SameRunTrialWireSchemaVersion : 0,
      .canonical_algorithm_budget_checksum =
          same_run ? kPhase4ConfirmatoryH4096SessionV5ExactCanonicalAlgorithmBudgetChecksum
                   : kPhase4ConfirmatoryH4096SessionV5CalibrationCanonicalAlgorithmBudgetChecksum,
      .paired_semantic_budget_checksum =
          same_run ? kPhase4ConfirmatoryH4096SessionV5ExactPairedBudgetChecksum
                   : kPhase4ConfirmatoryH4096SessionV5CalibrationPairedBudgetChecksum,
  };
}

Phase4CanonicalCellConfig BuildPhase4ConfirmatoryH4096SessionV5CanonicalCell(
    Phase4H4096SessionV5Carrier carrier) noexcept {
  const bool same_run = carrier == Phase4H4096SessionV5Carrier::kSameRun;
  Phase4CanonicalCellConfig cell;
  cell.case_id = same_run ? 10'100U : 10'200U;
  cell.requested_pool_size = same_run ? 4U : 8U;
  cell.preparation_worker_count = kPhase4CanonicalPreparationWorkersV1;
  cell.repetitions = kPhase4CanonicalRepetitionsV1;
  cell.maximum_setup_elapsed_nanoseconds = kCanonicalSetupElapsedNanoseconds;
  cell.external_budget = {
      .maximum_prepared_elapsed_nanoseconds = kCanonicalPreparedElapsedNanoseconds,
      .maximum_cold_elapsed_nanoseconds = kCanonicalColdElapsedNanoseconds,
      .maximum_address_space_bytes = kCanonicalAddressSpaceBytes,
      .maximum_peak_host_bytes = kCanonicalPeakHostBytes,
  };
  return cell;
}

std::optional<Phase4PairedTrialError> PreflightPhase4ConfirmatoryH4096SessionV5Spec(
    const Phase4H4096SessionV5PreflightIdentity& identity, const Phase4PairedTrialSpec& spec,
    std::uint32_t expected_repetition, Phase4TrialArm arm) {
  if (std::optional<Phase4PairedTrialError> error = ValidateIdentity(identity, arm);
      error.has_value()) {
    return error;
  }
  if (expected_repetition >= kPhase4CanonicalRepetitionsV1) {
    return Error(Phase4PairedTrialErrorCode::kInvalidConfiguration,
                 "P4PAIR-H4096-SESSION-V5-REPETITION-001",
                 "the Session-v5 repetition must be in the complete twenty-repetition roster", arm,
                 kPhase4CanonicalRepetitionsV1, expected_repetition);
  }
  const Phase4TrialOrder expected_order = expected_repetition % 2U == 0U
                                              ? Phase4TrialOrder::kBaselineFirst
                                              : Phase4TrialOrder::kCandidateFirst;
  const Phase4CanonicalCellConfig cell =
      BuildPhase4ConfirmatoryH4096SessionV5CanonicalCell(identity.carrier);
  Phase4CanonicalSpecResult built = BuildPhase4CanonicalTrialSpecForCorpusV2H4096SessionV5(
      cell, expected_repetition, expected_order);
  const Phase4PairedTrialSpec* expected = std::get_if<Phase4PairedTrialSpec>(&built);
  if (expected == nullptr) {
    return Error(Phase4PairedTrialErrorCode::kInternalInvariant,
                 "P4PAIR-H4096-SESSION-V5-PREIMAGE-001",
                 "the fixed Session-v5 canonical preimage could not be reconstructed", arm);
  }
  if (spec.case_id != expected->case_id ||
      spec.requested_pool_size != expected->requested_pool_size) {
    return Error(Phase4PairedTrialErrorCode::kInvalidConfiguration,
                 "P4PAIR-H4096-SESSION-V5-SCOPE-001",
                 "the carrier is bound to exactly one Session-v5 development cell", arm,
                 expected->case_id, spec.case_id);
  }
  if (spec.repetition_index != expected_repetition || spec.execution_order != expected_order ||
      spec.root_seed != expected->root_seed) {
    return Error(Phase4PairedTrialErrorCode::kInvalidConfiguration,
                 "P4PAIR-H4096-SESSION-V5-TRIAL-IDENTITY-001",
                 "the repetition, alternating order, and deterministic seed must match the "
                 "canonical Session-v5 trial identity",
                 arm);
  }
  if (spec.schema_version != expected->schema_version ||
      spec.baseline_config.schema_version != expected->baseline_config.schema_version ||
      spec.preparation_config.schema_version != expected->preparation_config.schema_version ||
      spec.candidate_session_config.schema_version !=
          allocator::kCpuCandidateAllocationSessionSchemaVersionV5) {
    return Error(Phase4PairedTrialErrorCode::kUnsupportedSchema,
                 "P4PAIR-H4096-SESSION-V5-SCHEMA-001",
                 "the trial, baseline, preparation, and Session-v5 schemas must match the frozen "
                 "authority",
                 arm);
  }
  const auto& baseline_price = spec.baseline_config.price_config;
  const auto& candidate_price = spec.candidate_session_config.price_config;
  if (baseline_price.present_step_per_overuse_unit !=
          kPhase4CorpusV2ProtocolV1PresentStepPerOveruseUnit ||
      baseline_price.history_step_per_overuse_unit !=
          kPhase4CorpusV2H4096HistoryStepPerOveruseUnit ||
      !(baseline_price == candidate_price)) {
    return Error(
        Phase4PairedTrialErrorCode::kInvalidConfiguration, "P4PAIR-H4096-SESSION-V5-PRICE-001",
        "Session-v5 requires the exact equal-arm present=1,history=4096 price authority", arm);
  }
  const std::uint64_t canonical_budget = ComputePhase4CanonicalAlgorithmBudgetChecksumV1(spec);
  if (canonical_budget != identity.canonical_algorithm_budget_checksum) {
    return Error(Phase4PairedTrialErrorCode::kInvalidConfiguration,
                 "P4PAIR-H4096-SESSION-V5-CANONICAL-BUDGET-001",
                 "the complete Session-v5 algorithm configuration differs from its frozen "
                 "canonical budget",
                 arm, identity.canonical_algorithm_budget_checksum, canonical_budget);
  }
  const Phase4CaseDescriptor* descriptor =
      FindPhase4CaseDescriptorForAuthority(Phase4RepresentativeCorpusAuthority::kV2, spec.case_id);
  const Phase4CaseRole expected_role = identity.case_role == Phase4H4096SessionV5CaseRole::kExact
                                           ? Phase4CaseRole::kExactOracle
                                           : Phase4CaseRole::kCalibration;
  if (descriptor == nullptr || descriptor->role != expected_role) {
    return Error(Phase4PairedTrialErrorCode::kMeasurementAssociation,
                 "P4PAIR-H4096-SESSION-V5-CASE-ROLE-001",
                 "the static Corpus-v2 descriptor does not match the fixed Session-v5 case role",
                 arm);
  }
  const Phase4RouteOpportunity opportunity{
      .route_queries = spec.baseline_config.limits.maximum_route_queries,
      .route_work_units = spec.baseline_config.limits.maximum_total_route_work_units,
  };
  const std::uint64_t columns_per_epoch =
      spec.candidate_session_config.regeneration_plan_config.maximum_total_columns;
  const std::uint32_t terminal_rounds =
      MaximumTerminalSelectionRounds(spec.candidate_session_config);
  const std::uint64_t paired_budget = ComputePhase4PairedBudgetChecksumForAuthorityV1(
      Phase4RepresentativeCorpusAuthority::kV2, spec, opportunity, descriptor->requested_net_count,
      columns_per_epoch, terminal_rounds);
  if (paired_budget != identity.paired_semantic_budget_checksum) {
    return Error(Phase4PairedTrialErrorCode::kInvalidConfiguration,
                 "P4PAIR-H4096-SESSION-V5-PAIRED-BUDGET-001",
                 "the independently reconstructed paired semantic budget differs from its frozen "
                 "Session-v5 identity",
                 arm, identity.paired_semantic_budget_checksum, paired_budget);
  }
  if (!EquivalentSpec(spec, *expected)) {
    return Error(Phase4PairedTrialErrorCode::kInvalidConfiguration,
                 "P4PAIR-H4096-SESSION-V5-SPEC-001",
                 "the complete Session-v5 canonical trial specification differs from its fixed "
                 "preimage",
                 arm);
  }
  return std::nullopt;
}

std::optional<Phase4PairedTrialError> PreflightPhase4ConfirmatoryH4096SessionV5Controller(
    const Phase4H4096SessionV5PreflightIdentity& identity, const Phase4CanonicalCellConfig& cell,
    const Phase4H4096SessionV5ControllerSourceAssociation& source) {
  constexpr Phase4TrialArm kInitialArm = Phase4TrialArm::kSequentialBaseline;
  if (identity.endpoint != Phase4H4096SessionV5Endpoint::kController) {
    return Error(Phase4PairedTrialErrorCode::kInvalidConfiguration,
                 "P4PAIR-H4096-SESSION-V5-ENDPOINT-001",
                 "the Session-v5 controller preflight requires the compiled controller endpoint");
  }
  if (std::optional<Phase4PairedTrialError> error = ValidateCell(identity, cell, kInitialArm);
      error.has_value()) {
    return error;
  }
  for (std::uint32_t repetition = 0; repetition < kPhase4CanonicalRepetitionsV1; ++repetition) {
    const Phase4TrialOrder order = repetition % 2U == 0U ? Phase4TrialOrder::kBaselineFirst
                                                         : Phase4TrialOrder::kCandidateFirst;
    Phase4CanonicalSpecResult built =
        BuildPhase4CanonicalTrialSpecForCorpusV2H4096SessionV5(cell, repetition, order);
    const Phase4PairedTrialSpec* spec = std::get_if<Phase4PairedTrialSpec>(&built);
    if (spec == nullptr) {
      return Error(Phase4PairedTrialErrorCode::kInternalInvariant,
                   "P4PAIR-H4096-SESSION-V5-PREIMAGE-001",
                   "the controller could not reconstruct the complete Session-v5 trial roster");
    }
    for (const Phase4TrialArm arm : std::array{Phase4TrialArm::kSequentialBaseline,
                                               Phase4TrialArm::kReusableCandidateAllocation}) {
      if (std::optional<Phase4PairedTrialError> error =
              PreflightPhase4ConfirmatoryH4096SessionV5Spec(identity, *spec, repetition, arm);
          error.has_value()) {
        return error;
      }
    }
  }
  if (std::optional<Phase4PairedTrialError> error = ValidateEmbeddedSource(
          source.embedded_commit, source.source_stamped, source.source_tree_dirty, kInitialArm);
      error.has_value()) {
    return error;
  }
  if (!CommitMatchesBuiltSource(source.runtime_commit, source.embedded_commit)) {
    return Error(Phase4PairedTrialErrorCode::kMeasurementAssociation,
                 "P4PAIR-H4096-SESSION-V5-SOURCE-002",
                 "the explicit runtime commit must exactly equal the clean embedded source commit");
  }
  return PreflightPhase4CorpusV2SessionExecutionAuthority(identity.execution_authority,
                                                          kInitialArm);
}

std::optional<Phase4PairedTrialError> PreflightPhase4ConfirmatoryH4096SessionV5Worker(
    const Phase4H4096SessionV5PreflightIdentity& identity, const Phase4CanonicalCellConfig& cell,
    Phase4TrialArm arm, const Phase4H4096SessionV5WorkerSourceAssociation& source) {
  if (identity.endpoint != Phase4H4096SessionV5Endpoint::kWorker) {
    return Error(Phase4PairedTrialErrorCode::kInvalidConfiguration,
                 "P4PAIR-H4096-SESSION-V5-ENDPOINT-001",
                 "the Session-v5 worker preflight requires the compiled worker endpoint", arm);
  }
  if (std::optional<Phase4PairedTrialError> error = ValidateCell(identity, cell, arm);
      error.has_value()) {
    return error;
  }
  for (std::uint32_t repetition = 0; repetition < kPhase4CanonicalRepetitionsV1; ++repetition) {
    const Phase4TrialOrder order = repetition % 2U == 0U ? Phase4TrialOrder::kBaselineFirst
                                                         : Phase4TrialOrder::kCandidateFirst;
    Phase4CanonicalSpecResult built =
        BuildPhase4CanonicalTrialSpecForCorpusV2H4096SessionV5(cell, repetition, order);
    const Phase4PairedTrialSpec* spec = std::get_if<Phase4PairedTrialSpec>(&built);
    if (spec == nullptr) {
      return Error(Phase4PairedTrialErrorCode::kInternalInvariant,
                   "P4PAIR-H4096-SESSION-V5-PREIMAGE-001",
                   "the worker could not reconstruct the complete Session-v5 trial roster", arm);
    }
    if (std::optional<Phase4PairedTrialError> error =
            PreflightPhase4ConfirmatoryH4096SessionV5Spec(identity, *spec, repetition, arm);
        error.has_value()) {
      return error;
    }
  }
  if (std::optional<Phase4PairedTrialError> error = ValidateEmbeddedSource(
          source.embedded_commit, source.source_stamped, source.source_tree_dirty, arm);
      error.has_value()) {
    return error;
  }
  return PreflightPhase4CorpusV2SessionExecutionAuthority(identity.execution_authority, arm);
}

}  // namespace apgar::benchmark::internal
