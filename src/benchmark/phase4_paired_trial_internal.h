#ifndef APGAR_SRC_BENCHMARK_PHASE4_PAIRED_TRIAL_INTERNAL_H_
#define APGAR_SRC_BENCHMARK_PHASE4_PAIRED_TRIAL_INTERNAL_H_

#include <cstdint>
#include <optional>

#include "apgar/benchmark/phase4_paired_trial.h"

namespace apgar::benchmark::internal {

inline constexpr std::uint64_t kPhase4CorpusV2ProtocolV1PresentStepPerOveruseUnit = 1;
inline constexpr std::uint64_t kPhase4CorpusV2ProtocolV1HistoryStepPerOveruseUnit = 2'250;
inline constexpr std::uint64_t kPhase4CorpusV2H4096HistoryStepPerOveruseUnit = 4'096;
inline constexpr std::uint64_t kPhase4ConfirmatoryH4096ExactCanonicalAlgorithmBudgetChecksum =
    8'829'615'204'625'848'656ULL;
inline constexpr std::uint64_t kPhase4ConfirmatoryH4096CalibrationCanonicalAlgorithmBudgetChecksum =
    8'230'401'457'668'518'004ULL;

// Trusted, out-of-band execution authority. Public Corpus-v2 entry points
// always select kCorpusV2H2250; only separately named internal development
// entry points may select kCorpusV2H4096.
enum class Phase4TrialExecutionAuthority : std::uint8_t {
  kCorpusV1 = 0,
  kCorpusV2H2250 = 1,
  kCorpusV2H4096 = 2,
};

// Pure configuration preflights. These do not construct a representative case
// or touch a fixture, preparer, or worker.
[[nodiscard]] std::optional<Phase4PairedTrialError> PreflightPhase4ConfirmatoryH4096OrdinarySpec(
    const Phase4PairedTrialSpec& spec, Phase4TrialArm arm);

[[nodiscard]] std::optional<Phase4PairedTrialError> PreflightPhase4ConfirmatoryH4096SameRunSpec(
    const Phase4PairedTrialSpec& spec, Phase4TrialArm arm);

// The only H=4096 arm-execution surfaces opened by the initial development
// slice: calibration (10200,8) over ordinary Raw/Wire1 and exact (10100,4)
// over same-run Raw/Wire2.
[[nodiscard]] Phase4TrialArmExecutionResult ExecutePhase4ConfirmatoryH4096OrdinaryTrialArm(
    Phase4TrialArm arm, const Phase4PairedTrialSpec& spec, std::string_view imported_fixture,
    allocator::PersistentCpuCandidatePoolPreparer* candidate_preparer = nullptr);

[[nodiscard]] Phase4TrialArmWithSameRunTelemetryExecutionResultV1
ExecutePhase4ConfirmatoryH4096SameRunTrialArm(
    Phase4TrialArm arm, const Phase4PairedTrialSpec& spec, std::string_view imported_fixture,
    allocator::PersistentCpuCandidatePoolPreparer* candidate_preparer = nullptr);

// Finalization and assembly require the independently reconstructed canonical
// spec so a structurally valid Corpus-v2/H=2250 wire result cannot cross into
// either H=4096 artifact authority.
[[nodiscard]] Phase4TrialArmRecordResult FinalizePhase4ConfirmatoryH4096OrdinaryTrialArm(
    const Phase4PairedTrialSpec& expected_spec, Phase4TrialArmExecution execution,
    const Phase4ExternalResourceObservation& observation);

[[nodiscard]] Phase4TrialArmRecordResult FinalizePhase4ConfirmatoryH4096SameRunTrialArm(
    const Phase4PairedTrialSpec& expected_spec, Phase4TrialArmExecution execution,
    const Phase4ExternalResourceObservation& observation);

[[nodiscard]] Phase4PairedTrialAssemblyResult AssemblePhase4ConfirmatoryH4096OrdinaryPairedTrial(
    const Phase4PairedTrialSpec& expected_spec, Phase4TrialArmRecord baseline,
    Phase4TrialArmRecord candidate);

[[nodiscard]] Phase4PairedTrialAssemblyResult AssemblePhase4ConfirmatoryH4096SameRunPairedTrial(
    const Phase4PairedTrialSpec& expected_spec, Phase4TrialArmRecord baseline,
    Phase4TrialArmRecord candidate);

[[nodiscard]] Phase4TrialArmFailure PreservePhase4CandidateSessionFailureV1(
    Phase4RepresentativeCase case_state, allocator::PreparedCpuCandidatePools prepared,
    allocator::CpuCandidateAllocationSessionError error);

[[nodiscard]] std::uint64_t ComputePhase4PairedBudgetChecksumV1(
    const Phase4PairedTrialSpec& spec, const Phase4RouteOpportunity& opportunity,
    std::uint32_t workload_net_count, std::uint64_t candidate_columns_per_epoch,
    std::uint32_t candidate_terminal_selection_rounds);

[[nodiscard]] std::uint64_t ComputePhase4PairedBudgetChecksumForAuthorityV1(
    Phase4RepresentativeCorpusAuthority authority, const Phase4PairedTrialSpec& spec,
    const Phase4RouteOpportunity& opportunity, std::uint32_t workload_net_count,
    std::uint64_t candidate_columns_per_epoch, std::uint32_t candidate_terminal_selection_rounds);

[[nodiscard]] std::uint64_t ComputePhase4CanonicalAlgorithmBudgetChecksumV1(
    const Phase4PairedTrialSpec& spec);

[[nodiscard]] std::uint64_t ComputePhase4TrialArmSemanticChecksumV1(
    const Phase4TrialArmSemantics& semantics) noexcept;

// Shared validation for a semantic-only arm. Finalization, diagnostic report
// artifacts, and telemetry must reject the same enum, counter, component, and
// baseline/candidate source-shape drift before trusting its checksum.
[[nodiscard]] std::optional<Phase4PairedTrialError> ValidatePhase4TrialArmSemanticsV1(
    const Phase4TrialArmSemantics& semantics) noexcept;

[[nodiscard]] std::optional<Phase4PairedTrialError> ValidatePhase4TrialArmSemanticsForAuthorityV1(
    Phase4RepresentativeCorpusAuthority authority,
    const Phase4TrialArmSemantics& semantics) noexcept;

[[nodiscard]] std::uint64_t ComputePhase4ArmReportTelemetryChecksumV1(
    const Phase4ArmReportTelemetryV1& telemetry) noexcept;

[[nodiscard]] std::optional<Phase4PairedTrialError> ValidatePhase4ArmReportTelemetryV1(
    const Phase4TrialArmSemantics& semantics, const allocator::MultiNetWorkload& workload,
    const Phase4ArmReportTelemetryV1& telemetry) noexcept;

[[nodiscard]] std::optional<Phase4PairedTrialError> ValidatePhase4ArmReportTelemetryForAuthorityV1(
    Phase4RepresentativeCorpusAuthority authority, const Phase4TrialArmSemantics& semantics,
    const allocator::MultiNetWorkload& workload,
    const Phase4ArmReportTelemetryV1& telemetry) noexcept;

[[nodiscard]] std::uint64_t ComputePhase4SameRunArmDecisionTelemetryChecksumV1(
    const Phase4SameRunArmDecisionTelemetryV1& telemetry) noexcept;

[[nodiscard]] std::uint64_t ComputePhase4TrialArmOperationalProfileChecksumV1(
    const Phase4TrialArmOperationalProfileV1& profile) noexcept;

[[nodiscard]] std::optional<Phase4PairedTrialError> ValidatePhase4TrialArmOperationalProfileV1(
    const Phase4TrialArmOperationalProfileV1& profile) noexcept;

[[nodiscard]] std::optional<Phase4PairedTrialError>
ValidatePhase4TrialArmOperationalProfileForAuthorityV1(
    Phase4RepresentativeCorpusAuthority authority,
    const Phase4TrialArmOperationalProfileV1& profile) noexcept;

[[nodiscard]] std::uint64_t ComputePhase4TrialArmReplayAuthorityChecksumV1(
    const Phase4TrialArmReplayAuthorityV1& authority) noexcept;

[[nodiscard]] std::optional<Phase4PairedTrialError> ValidatePhase4TrialArmReplayAuthorityV1(
    const Phase4TrialArmReplayAuthorityV1& authority) noexcept;

[[nodiscard]] std::optional<Phase4PairedTrialError>
ValidatePhase4TrialArmReplayAuthorityForAuthorityV1(
    Phase4RepresentativeCorpusAuthority corpus_authority,
    const Phase4TrialArmReplayAuthorityV1& authority) noexcept;

[[nodiscard]] std::optional<Phase4PairedTrialError> ValidatePhase4SameRunArmDecisionTelemetryV1(
    const Phase4TrialArmSemantics& semantics, const allocator::MultiNetWorkload& workload,
    const Phase4SameRunArmDecisionTelemetryV1& telemetry) noexcept;

[[nodiscard]] std::optional<Phase4PairedTrialError>
ValidatePhase4SameRunArmDecisionTelemetryForAuthorityV1(
    Phase4RepresentativeCorpusAuthority authority, const Phase4TrialArmSemantics& semantics,
    const allocator::MultiNetWorkload& workload,
    const Phase4SameRunArmDecisionTelemetryV1& telemetry) noexcept;

[[nodiscard]] bool AccumulatePhase4BaselineColumnV1(
    const allocator::SequentialNegotiatedColumnRecord& column,
    Phase4PerNetColumnOutcomesV1* outcomes) noexcept;

[[nodiscard]] bool AccumulatePhase4PreparationColumnV1(
    const allocator::CpuCandidatePoolColumnRecord& column,
    Phase4PerNetColumnOutcomesV1* outcomes) noexcept;

[[nodiscard]] bool AccumulatePhase4RegenerationColumnV1(
    const allocator::TargetedRegenerationColumnRecord& column,
    Phase4PerNetColumnOutcomesV1* outcomes) noexcept;

[[nodiscard]] std::uint64_t ComputePhase4TrialArmArtifactChecksumV1(
    const Phase4TrialArmRecord& record) noexcept;

[[nodiscard]] std::uint64_t ComputePhase4ExternalAuthorityChecksumV1(
    const Phase4ExternalResourceObservation& observation) noexcept;

[[nodiscard]] std::uint64_t ComputePhase4PairedTrialSemanticChecksumV1(
    const Phase4PairedTrialResult& result) noexcept;

[[nodiscard]] std::uint64_t ComputePhase4PairedTrialArtifactChecksumV1(
    const Phase4PairedTrialResult& result) noexcept;

}  // namespace apgar::benchmark::internal

#endif  // APGAR_SRC_BENCHMARK_PHASE4_PAIRED_TRIAL_INTERNAL_H_
