#ifndef APGAR_SRC_ALLOCATOR_SEQUENTIAL_NEGOTIATED_BASELINE_INTERNAL_H_
#define APGAR_SRC_ALLOCATOR_SEQUENTIAL_NEGOTIATED_BASELINE_INTERNAL_H_

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "apgar/allocator/sequential_negotiated_baseline.h"

namespace apgar::allocator::internal {

struct SequentialNegotiatedPenaltyMergeV1 {
  std::vector<routing::ResourcePenalty> penalties;
  std::uint64_t visits = 0;
};

[[nodiscard]] std::optional<SequentialNegotiatedPenaltyMergeV1>
MergeSequentialNegotiatedPenaltiesV1(std::span<const routing::ResourcePenalty> base,
                                     std::span<const routing::ResourcePenalty> congestion);

[[nodiscard]] std::optional<std::uint64_t> ComputeSequentialNegotiatedMaximumDraftBytesV1(
    std::uint64_t maximum_reconstruction_states,
    std::uint64_t maximum_policy_resource_entries) noexcept;

[[nodiscard]] std::optional<std::uint64_t> ComputeSequentialNegotiatedAdmissionInputBytesV1(
    std::uint64_t maximum_candidate_draft_bytes,
    std::uint64_t maximum_policy_resource_entries) noexcept;

[[nodiscard]] std::optional<std::uint64_t> ComputeSequentialNegotiatedAdmissionWorkV1(
    std::uint64_t maximum_reconstruction_states, std::uint64_t maximum_policy_resource_entries,
    std::uint64_t obstacle_count, std::uint64_t terminal_count) noexcept;

[[nodiscard]] std::uint64_t ComputeSequentialNegotiatedCandidateSemanticChecksumV1(
    const candidates::RouteCandidate& candidate) noexcept;

[[nodiscard]] std::uint64_t ComputeSequentialNegotiatedBatchIdentityV1(
    std::uint64_t board_content_hash, std::uint64_t workload_checksum,
    std::uint64_t capacity_model_checksum,
    const SequentialNegotiatedBaselineConfig& config) noexcept;

[[nodiscard]] std::uint64_t ComputeSequentialNegotiatedSessionChecksumV1(
    const SequentialNegotiatedBaselineConfig& config, std::uint64_t batch_identity,
    std::uint64_t workload_checksum, std::uint64_t capacity_model_checksum,
    SequentialNegotiatedTerminalReason terminal_reason,
    const SequentialNegotiatedBaselineCounters& counters,
    std::span<const SequentialNegotiatedColumnRecord> columns,
    std::span<const SequentialNegotiatedSweepRecord> sweeps,
    std::span<const CandidatePool> final_pools, const OneWorldAllocation& final_world,
    const NegotiatedPriceState& successor_price_state) noexcept;

// Deep replay authority for a retained baseline result. Every retained nested
// checksum is rebuilt from its live preimage before the top-level checksum is
// accepted.
[[nodiscard]] std::optional<std::uint64_t> RecomputeSequentialNegotiatedSessionChecksumFromLiveV1(
    const SequentialNegotiatedBaselineResult& result, const board_ir::BoardSnapshot& board,
    const MultiNetWorkload& workload, const ResourceCapacityModel& capacities) noexcept;

// Defined only by the fault-test variant. It lowers the selected query's
// ephemeral CandidateStore work budget after production preflight.
void SetSequentialNegotiatedAdmissionBudgetFaultForTesting(std::uint64_t query_identity) noexcept;

}  // namespace apgar::allocator::internal

#endif  // APGAR_SRC_ALLOCATOR_SEQUENTIAL_NEGOTIATED_BASELINE_INTERNAL_H_
