#ifndef APGAR_SRC_ALLOCATOR_ONE_WORLD_INTERNAL_H_
#define APGAR_SRC_ALLOCATOR_ONE_WORLD_INTERNAL_H_

#include <cstdint>
#include <new>
#include <span>
#include <stdexcept>
#include <utility>
#include <variant>
#include <vector>

#include "apgar/allocator/one_world.h"

namespace apgar::allocator::internal {

struct AccumulatedResourceUse {
  routing::EdgeResourceKey resource;
  std::uint64_t usage_units = 0;

  friend bool operator==(const AccumulatedResourceUse&, const AccumulatedResourceUse&) = default;
};

struct SelectedResourceAccumulation {
  std::vector<AccumulatedResourceUse> resources;
  std::uint64_t logical_resource_uses = 0;
  std::uint64_t materialized_resource_edges = 0;
  std::uint64_t span_boundary_events = 0;
};

struct OneWorldPoolSelectionEvidence {
  board_ir::EntityRef net{};
  std::uint64_t candidate_count = 0;
  std::uint64_t pool_manifest_checksum = 0;
  NetSelection selection;
};

// Source-private result of the production request validation, canonical pool
// construction, and score-minimizing selection pass. It deliberately stops
// before selected footprints are atomically materialized into world occupancy.
// Consumers may therefore enforce a compressed selected-footprint bound before
// invoking the complete allocator/pricing reconstruction.
struct OneWorldSelectionEvidence {
  std::uint64_t request_manifest_checksum = 0;
  std::uint64_t candidate_pool_manifest_checksum = 0;
  std::uint64_t source_pool_count = 0;
  std::uint64_t source_candidate_count = 0;
  std::uint64_t selected_expanded_resource_uses = 0;
  OneWorldAllocation selection_projection;
  std::vector<OneWorldPoolSelectionEvidence> pools;
};

using OneWorldSelectionEvidenceResult = std::variant<OneWorldSelectionEvidence, AllocationError>;

[[nodiscard]] OneWorldSelectionEvidenceResult SelectOneWorldWithoutAccounting(
    const OneWorldAllocationRequest& request);

using SelectedResourceAccumulationResult =
    std::variant<SelectedResourceAccumulation, AllocationError>;

// CPU reference primitive for selected-world resource accumulation. It sweeps
// canonical compressed-span boundaries, then expands each unique output edge
// once. Work is therefore proportional to span boundaries plus unique output,
// rather than selected candidates multiplied by shared-corridor length.
[[nodiscard]] SelectedResourceAccumulationResult AccumulateSelectedResources(
    std::span<const candidates::StoredCandidate> selected_candidates,
    std::uint64_t maximum_expanded_resource_uses);

using ResourceSpanStream = std::span<const candidates::PhysicalEdgeSpan>;

// Factory preflight shared by capacity and price state. Keep this check pure so
// the exact schema boundary can be exercised without allocating an oversized
// input vector merely to reach the validator.
[[nodiscard]] constexpr bool ResourceRecordCountFitsV1(std::uint64_t count) noexcept {
  return count <= kMaximumAllocatorResourceRecordsV1;
}

[[nodiscard]] constexpr bool CombinedResourceRecordCountFitsV1(std::uint64_t capacity_count,
                                                               std::uint64_t price_count) noexcept {
  return capacity_count <= kMaximumAllocatorResourceRecordsV1 &&
         price_count <= kMaximumAllocatorResourceRecordsV1 - capacity_count;
}

// Same production reducer over validated immutable-footprint representation,
// exposed source-privately for four-direction and overlap boundary tables.
[[nodiscard]] SelectedResourceAccumulationResult AccumulateResourceSpanStreams(
    std::span<const ResourceSpanStream> streams, std::uint64_t maximum_expanded_resource_uses);

// Stable replay encoder entry point. It intentionally accepts a representation
// value rather than rerunning allocation so a golden test can distinguish every
// adjacent encoded field.
[[nodiscard]] std::uint64_t ComputeOneWorldChecksumV1(const OneWorldAllocation& world) noexcept;
[[nodiscard]] std::uint64_t ComputeOneWorldChecksumV2(const OneWorldAllocation& world) noexcept;

template <typename Operation>
[[nodiscard]] auto RunWithAllocationFailureEnvelope(Operation&& operation)
    -> decltype(std::forward<Operation>(operation)()) {
  using Result = decltype(std::forward<Operation>(operation)());
  try {
    return std::forward<Operation>(operation)();
  } catch (const std::bad_alloc&) {
    return Result{
        AllocationError{.code = AllocationErrorCode::kResourceExhausted,
                        .invariant_id = "allocator.host_memory_exhausted.v1",
                        .detail = "Host allocation failed within the configured one-world bounds"}};
  } catch (const std::length_error&) {
    return Result{
        AllocationError{.code = AllocationErrorCode::kResourceExhausted,
                        .invariant_id = "allocator.host_container_exhausted.v1",
                        .detail = "Host container limits were exhausted within one-world bounds"}};
  }
}

}  // namespace apgar::allocator::internal

#endif  // APGAR_SRC_ALLOCATOR_ONE_WORLD_INTERNAL_H_
