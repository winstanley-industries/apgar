#ifndef APGAR_SRC_ALLOCATOR_TARGETED_REGENERATION_EXECUTION_INTERNAL_H_
#define APGAR_SRC_ALLOCATOR_TARGETED_REGENERATION_EXECUTION_INTERNAL_H_

#include <cstdint>
#include <optional>
#include <span>

#include "apgar/allocator/targeted_regeneration_execution.h"

namespace apgar::allocator::internal {

[[nodiscard]] bool TargetedRegenerationExecutionConfigIsValidV2(
    const TargetedRegenerationExecutionConfig& config) noexcept;

[[nodiscard]] routing::PlanarRouteRequest BuildTargetedRegenerationRouteRequestV1(
    const routing::PlanarRouteRequest& source,
    const routing::CandidateGenerationPolicy& candidate_policy);

struct TargetedRegenerationExecutionChecksumHeaderV2 {
  std::uint32_t schema_version = 0;
  std::uint64_t plan_checksum = 0;
  TargetedRegenerationExecutionConfig config;
  candidates::CandidateStoreConfig store_config;
  std::uint64_t refreshed_request_manifest_checksum = 0;
  std::uint64_t refreshed_candidate_pool_manifest_checksum = 0;
  std::uint64_t baseline_world_checksum = 0;
  std::uint64_t refreshed_world_checksum = 0;
  TargetedRegenerationExecutionDisposition disposition =
      TargetedRegenerationExecutionDisposition::kNoWork;
  TargetedRegenerationTerminalReason terminal_reason =
      TargetedRegenerationTerminalReason::kNoTargets;
  TargetedRegenerationExecutionCounters counters;
};

[[nodiscard]] std::uint64_t ComputeTargetedRegenerationExecutionChecksumV2(
    const TargetedRegenerationExecutionChecksumHeaderV2& header,
    std::span<const TargetedRegenerationColumnRecord> columns) noexcept;

[[nodiscard]] std::optional<std::uint64_t> ComputeTargetedRegenerationMaximumDraftBytesV2(
    std::uint64_t maximum_reconstruction_states,
    std::uint64_t maximum_policy_resource_entries) noexcept;

[[nodiscard]] std::optional<std::uint64_t> ComputeTargetedRegenerationGeneratedBytesV2(
    std::uint64_t route_queries, std::uint64_t maximum_candidate_draft_bytes) noexcept;

[[nodiscard]] std::optional<std::uint64_t> ComputeTargetedRegenerationAdmissionInputBytesV2(
    std::uint64_t route_queries, std::uint64_t maximum_candidate_draft_bytes,
    std::uint64_t aggregate_policy_resource_entries) noexcept;

[[nodiscard]] std::optional<std::uint64_t> ComputeTargetedRegenerationAdmissionWorkV2(
    std::uint64_t route_queries, std::uint64_t maximum_reconstruction_states,
    std::uint64_t aggregate_policy_resource_entries, std::uint64_t obstacle_count,
    std::uint64_t terminal_count) noexcept;

[[nodiscard]] std::optional<std::uint64_t> ComputeTargetedRegenerationPolicyProjectionVisitsV2(
    std::uint64_t target_count, std::uint64_t price_count) noexcept;

[[nodiscard]] std::optional<std::uint64_t> ComputeTargetedRegenerationRejectionBytesV2(
    std::uint64_t route_queries) noexcept;

[[nodiscard]] std::optional<std::uint64_t> ComputeTargetedRegenerationTransientResultBytesV2(
    std::uint64_t route_queries) noexcept;

[[nodiscard]] std::optional<std::uint64_t> ComputeTargetedRegenerationColumnLogicalBytesV2(
    const TargetedRegenerationColumnRecord& column) noexcept;

[[nodiscard]] std::uint64_t ComputeTargetedRegenerationFailedObservationChecksumV2(
    std::uint64_t plan_checksum, const TargetedRegenerationExecutionConfig& config,
    const candidates::CandidateStoreConfig& store_config,
    bool candidate_store_publication_committed, TargetedRegenerationExecutionErrorCode error_code,
    const TargetedRegenerationExecutionCounters& counters,
    std::span<const TargetedRegenerationColumnRecord> columns) noexcept;

enum class TargetedRegenerationHostFailureForTesting : std::uint8_t {
  kBadAlloc = 0,
  kLengthError = 1,
};

enum class TargetedRegenerationRejectionEvidenceBoundaryForTesting : std::uint8_t {
  kRoute = 0,
  kBuild = 1,
};

// Source-private deterministic failure after one complete request copy but
// before its query column and counters become observable. Zero fails the next
// preparation; positive values allow that many complete preparations first.
void SetTargetedRegenerationRequestPreparationHostFailureForTesting(
    std::optional<std::uint64_t> countdown,
    TargetedRegenerationHostFailureForTesting failure =
        TargetedRegenerationHostFailureForTesting::kBadAlloc) noexcept;

// Source-private deterministic failure immediately after a query becomes
// observable. Zero fails the next query; positive values allow that many
// query starts first.
void SetTargetedRegenerationPostQueryHostFailureForTesting(
    std::optional<std::uint64_t> countdown,
    TargetedRegenerationHostFailureForTesting failure =
        TargetedRegenerationHostFailureForTesting::kBadAlloc) noexcept;

// Source-private rejection-evidence fault. The build boundary forces the next
// authenticated route through the builder's invalid-scheduling rejection so
// the normally unreachable authenticated-build rejection path is observable.
void SetTargetedRegenerationRejectionEvidenceHostFailureForTesting(
    std::optional<TargetedRegenerationRejectionEvidenceBoundaryForTesting> boundary,
    TargetedRegenerationHostFailureForTesting failure =
        TargetedRegenerationHostFailureForTesting::kBadAlloc) noexcept;

// Source-private one-shot replacement of an authentic completed CPU result
// with a typed disconnection immediately before normal route-failure handling.
void SetTargetedRegenerationRouteFailureForTesting(bool enabled) noexcept;

// Source-private one-shot corruption of the next authentic CPU route's opaque
// producer evidence immediately before candidate construction. This forces a
// typed build rejection without also forcing a host exception.
void SetTargetedRegenerationBuildRejectionForTesting(bool enabled) noexcept;

// Source-private deterministic failure immediately after CandidateStore
// publication commits and the failed-observation commit bit becomes true.
void SetTargetedRegenerationPostPublicationHostFailureForTesting(
    std::optional<TargetedRegenerationHostFailureForTesting> failure) noexcept;

// Source-private one-shot typed failure immediately before successor lease
// acquisition. The production cardinality is already bounded by the plan
// lease; this seam covers the post-publication failure handoff.
void SetTargetedRegenerationSuccessorLeaseFailureForTesting(bool enabled) noexcept;

// Source-private observation proving preflight failures occur before CPU A*.
[[nodiscard]] std::uint64_t TargetedRegenerationRouteQueriesForTesting() noexcept;

// Source-private observation proving source-footprint preflight stops before
// traversing a rejected suffix.
[[nodiscard]] std::uint64_t TargetedRegenerationSourceResourceSpanVisitsForTesting() noexcept;

}  // namespace apgar::allocator::internal

#endif  // APGAR_SRC_ALLOCATOR_TARGETED_REGENERATION_EXECUTION_INTERNAL_H_
