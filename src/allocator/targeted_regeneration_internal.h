#ifndef APGAR_SRC_ALLOCATOR_TARGETED_REGENERATION_INTERNAL_H_
#define APGAR_SRC_ALLOCATOR_TARGETED_REGENERATION_INTERNAL_H_

#include <cstdint>
#include <span>
#include <variant>
#include <vector>

#include "apgar/allocator/targeted_regeneration.h"

namespace apgar::allocator::internal {

struct TargetedRegenerationChecksumHeaderV1 {
  std::uint32_t schema_version = 0;
  AllocationAssociations associations;
  std::uint64_t workload_checksum = 0;
  std::uint64_t source_request_manifest_checksum = 0;
  std::uint64_t candidate_pool_manifest_checksum = 0;
  std::uint64_t source_pool_count = 0;
  std::uint64_t source_candidate_count = 0;
  std::uint64_t pinned_candidate_count = 0;
  TargetedRegenerationConfig config;
  std::uint64_t price_state_checksum = 0;
  std::uint32_t price_iteration = 0;
  std::uint64_t source_world_checksum = 0;
  std::uint64_t total_requested_columns = 0;
  std::uint64_t total_resource_actions = 0;
  std::uint64_t total_conflict_resources = 0;
  std::uint64_t total_conflict_impact = 0;
  std::uint64_t expanded_resource_visits = 0;
};

struct TargetedRegenerationResourceScanV1 {
  std::uint64_t conflict_resource_count = 0;
  std::uint64_t conflict_impact = 0;
  std::uint64_t negotiated_price_exposure = 0;
  std::vector<RegenerationResourceAction> resource_actions;
};

using TargetedRegenerationResourceScanResultV1 =
    std::variant<TargetedRegenerationResourceScanV1, TargetedRegenerationError>;

[[nodiscard]] bool TargetedRegenerationTargetRanksBeforeV1(
    const TargetedRegenerationNet& left, const TargetedRegenerationNet& right) noexcept;

// Production resource scan shared with arithmetic/ranking regressions. Inputs
// are expected to have passed the complete negotiated-price/world validation.
[[nodiscard]] TargetedRegenerationResourceScanResultV1 ScanTargetedRegenerationResourcesV1(
    std::span<const candidates::PhysicalEdgeSpan> spans,
    std::span<const ResourceUsage> world_resources, std::span<const NegotiatedResourcePrice> prices,
    std::uint64_t maximum_retained_actions);

[[nodiscard]] std::uint64_t ComputeTargetedRegenerationPlanChecksumV1(
    const TargetedRegenerationChecksumHeaderV1& header,
    std::span<const TargetedRegenerationNet> targets) noexcept;

}  // namespace apgar::allocator::internal

#endif  // APGAR_SRC_ALLOCATOR_TARGETED_REGENERATION_INTERNAL_H_
