#ifndef APGAR_SRC_BENCHMARK_PHASE4_PAIRED_TRIAL_INTERNAL_H_
#define APGAR_SRC_BENCHMARK_PHASE4_PAIRED_TRIAL_INTERNAL_H_

#include <cstdint>
#include <optional>

#include "apgar/benchmark/phase4_paired_trial.h"

namespace apgar::benchmark::internal {

[[nodiscard]] Phase4TrialArmFailure PreservePhase4CandidateSessionFailureV1(
    Phase4RepresentativeCase case_state, allocator::PreparedCpuCandidatePools prepared,
    allocator::CpuCandidateAllocationSessionError error);

[[nodiscard]] std::uint64_t ComputePhase4PairedBudgetChecksumV1(
    const Phase4PairedTrialSpec& spec, const Phase4RouteOpportunity& opportunity,
    std::uint32_t workload_net_count, std::uint64_t candidate_columns_per_epoch,
    std::uint32_t candidate_terminal_selection_rounds);

[[nodiscard]] std::uint64_t ComputePhase4CanonicalAlgorithmBudgetChecksumV1(
    const Phase4PairedTrialSpec& spec);

[[nodiscard]] std::uint64_t ComputePhase4TrialArmSemanticChecksumV1(
    const Phase4TrialArmSemantics& semantics) noexcept;

// Shared validation for a semantic-only arm. Finalization, diagnostic report
// artifacts, and telemetry must reject the same enum, counter, component, and
// baseline/candidate source-shape drift before trusting its checksum.
[[nodiscard]] std::optional<Phase4PairedTrialError> ValidatePhase4TrialArmSemanticsV1(
    const Phase4TrialArmSemantics& semantics) noexcept;

[[nodiscard]] std::uint64_t ComputePhase4ArmReportTelemetryChecksumV1(
    const Phase4ArmReportTelemetryV1& telemetry) noexcept;

[[nodiscard]] std::optional<Phase4PairedTrialError> ValidatePhase4ArmReportTelemetryV1(
    const Phase4TrialArmSemantics& semantics, const allocator::MultiNetWorkload& workload,
    const Phase4ArmReportTelemetryV1& telemetry) noexcept;

[[nodiscard]] std::uint64_t ComputePhase4SameRunArmDecisionTelemetryChecksumV1(
    const Phase4SameRunArmDecisionTelemetryV1& telemetry) noexcept;

[[nodiscard]] std::uint64_t ComputePhase4TrialArmOperationalProfileChecksumV1(
    const Phase4TrialArmOperationalProfileV1& profile) noexcept;

[[nodiscard]] std::optional<Phase4PairedTrialError> ValidatePhase4TrialArmOperationalProfileV1(
    const Phase4TrialArmOperationalProfileV1& profile) noexcept;

[[nodiscard]] std::optional<Phase4PairedTrialError> ValidatePhase4SameRunArmDecisionTelemetryV1(
    const Phase4TrialArmSemantics& semantics, const allocator::MultiNetWorkload& workload,
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
