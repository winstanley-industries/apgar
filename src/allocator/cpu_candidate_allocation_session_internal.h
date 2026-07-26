#ifndef APGAR_SRC_ALLOCATOR_CPU_CANDIDATE_ALLOCATION_SESSION_INTERNAL_H_
#define APGAR_SRC_ALLOCATOR_CPU_CANDIDATE_ALLOCATION_SESSION_INTERNAL_H_

#include <cstdint>
#include <optional>
#include <span>

#include "apgar/allocator/cpu_candidate_allocation_session.h"

namespace apgar::allocator::internal {

struct CpuCandidateAllocationSessionEnvelopeV1 {
  std::uint64_t maximum_requested_columns = 0;
  std::uint64_t maximum_route_work_units = 0;
  std::uint64_t maximum_planning_expanded_resource_visits = 0;
  std::uint64_t maximum_policy_projection_visits = 0;
  std::uint64_t maximum_generated_candidate_bytes = 0;
  std::uint64_t maximum_rejection_bytes = 0;
  std::uint64_t maximum_transient_result_bytes = 0;
  std::uint64_t maximum_final_candidate_count = 0;
  std::uint64_t maximum_final_expanded_resource_uses = 0;

  friend bool operator==(const CpuCandidateAllocationSessionEnvelopeV1&,
                         const CpuCandidateAllocationSessionEnvelopeV1&) = default;
};

enum class CpuCandidatePoolComparisonV1 : std::uint8_t {
  kUnchanged = 0,
  kChanged = 1,
  kManifestCollisionOrDrift = 2,
};

[[nodiscard]] CpuCandidatePoolComparisonV1 CompareCpuCandidatePoolSemanticsV1(
    std::span<const CandidatePool> source, std::span<const CandidatePool> successor,
    std::uint64_t source_manifest_checksum, std::uint64_t successor_manifest_checksum) noexcept;

[[nodiscard]] bool CpuCandidateAllocationFixedPointV1(CpuCandidatePoolComparisonV1 pool_comparison,
                                                      bool selected_route_semantics_equal,
                                                      bool complete_price_values_equal) noexcept;

[[nodiscard]] CpuCandidateAllocationTerminalReason ResolveCpuCandidateAllocationTerminalReasonV1(
    CpuCandidateAllocationTerminalReason current, bool resource_refinement_required,
    bool preferred_world_feasible) noexcept;

void SetCpuCandidateAllocationFinalAssemblyFailureForTesting(bool enabled) noexcept;

[[nodiscard]] std::uint64_t CpuCandidateAllocationSourceSpanInspectionsForTesting() noexcept;

[[nodiscard]] std::uint64_t CpuCandidateAllocationEpochReservationForTesting() noexcept;

[[nodiscard]] std::optional<CpuCandidateAllocationSessionEnvelopeV1>
ProjectCpuCandidateAllocationSessionEnvelopeV1(
    std::uint64_t source_pool_count, std::uint64_t source_candidate_count,
    std::uint64_t source_expanded_resource_uses,
    const candidates::CandidateStoreConfig& store_config,
    const CpuCandidateAllocationSessionConfig& config) noexcept;

[[nodiscard]] std::uint64_t ComputeCpuCandidateAllocationRejectionManifestChecksumV1(
    std::span<const candidates::CandidateRejection> rejections) noexcept;

[[nodiscard]] std::uint64_t ComputeCpuCandidateAllocationSessionChecksumV3(
    const CpuCandidateAllocationSessionConfig& config, std::uint64_t board_content_hash,
    std::uint64_t workload_checksum, std::uint64_t capacity_model_checksum,
    std::uint64_t preparation_checksum, CpuCandidateAllocationTerminalReason terminal_reason,
    const CpuCandidateAllocationSessionCounters& counters,
    std::span<const CpuCandidateAllocationEpochRecord> epochs,
    std::uint64_t final_pool_manifest_checksum, std::uint64_t final_rejection_manifest_checksum,
    std::uint64_t final_price_state_checksum, std::uint64_t final_single_world_checksum,
    std::uint64_t final_multi_world_checksum) noexcept;

[[nodiscard]] std::uint64_t ComputeCpuCandidateAllocationSessionChecksumV4(
    const CpuCandidateAllocationSessionConfig& config, std::uint64_t board_content_hash,
    std::uint64_t workload_checksum, std::uint64_t capacity_model_checksum,
    std::uint64_t preparation_checksum, CpuCandidateAllocationTerminalReason terminal_reason,
    const CpuCandidateAllocationSessionCounters& counters,
    std::span<const CpuCandidateAllocationEpochRecord> epochs,
    std::uint64_t final_pool_manifest_checksum, std::uint64_t final_rejection_manifest_checksum,
    std::uint64_t final_price_state_checksum, std::uint64_t final_single_world_checksum,
    std::uint64_t final_multi_world_checksum) noexcept;

[[nodiscard]] std::uint64_t ComputeCpuCandidateAllocationSessionChecksumV5(
    const CpuCandidateAllocationSessionConfig& config, std::uint64_t board_content_hash,
    std::uint64_t workload_checksum, std::uint64_t capacity_model_checksum,
    std::uint64_t preparation_checksum, CpuCandidateAllocationTerminalReason terminal_reason,
    const CpuCandidateAllocationSessionCounters& counters,
    std::span<const CpuCandidateAllocationEpochRecord> epochs,
    std::uint64_t final_pool_manifest_checksum, std::uint64_t final_rejection_manifest_checksum,
    std::uint64_t final_price_state_checksum, std::uint64_t final_single_world_checksum,
    std::uint64_t final_multi_world_checksum) noexcept;

[[nodiscard]] std::uint64_t ComputeCpuCandidateAllocationEpochAssociationChecksumV1(
    std::span<const CpuCandidateAllocationEpochRecord> epochs,
    std::uint64_t planning_expanded_resource_visits) noexcept;

// Distills the compact publication witness from a complete live session. The
// session checksum itself authenticates the full config, epoch preimages,
// price/world state, pools, and rejection roster; the additional fields make
// the measured-profile join explicit and independently checkable.
[[nodiscard]] std::optional<CpuCandidateAllocationSessionReplayWitnessV1>
BuildCpuCandidateAllocationSessionReplayWitnessV1(const CpuCandidateAllocationSession& session);

// Produces the timed component profile and compact same-run association without
// retaining contender storage. Publication supplies the independent
// full-preimage authority join.
[[nodiscard]] CpuCandidateAllocationSessionResult
ExecuteCpuCandidateAllocationSessionWithOperationalProfileV1(
    std::uint32_t schema_version, board_ir::BoardSnapshot&& board, MultiNetWorkload&& workload,
    ResourceCapacityModel&& capacities, PreparedCpuCandidatePools&& prepared,
    const CpuCandidateAllocationSessionConfig& config,
    CpuCandidateAllocationSessionOperationalProfileV1& operational_profile);

}  // namespace apgar::allocator::internal

#endif  // APGAR_SRC_ALLOCATOR_CPU_CANDIDATE_ALLOCATION_SESSION_INTERNAL_H_
