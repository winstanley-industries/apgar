#ifndef APGAR_SRC_BENCHMARK_PHASE4_PAIRED_TRIAL_INTERNAL_H_
#define APGAR_SRC_BENCHMARK_PHASE4_PAIRED_TRIAL_INTERNAL_H_

#include <cstdint>

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
