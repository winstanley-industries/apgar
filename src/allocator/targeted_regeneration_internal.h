#ifndef APGAR_SRC_ALLOCATOR_TARGETED_REGENERATION_INTERNAL_H_
#define APGAR_SRC_ALLOCATOR_TARGETED_REGENERATION_INTERNAL_H_

#include <cstdint>
#include <span>
#include <variant>
#include <vector>

#include "apgar/allocator/targeted_regeneration.h"

namespace apgar::allocator::internal {

void SetTargetedRegenerationPlanSchemaVersionForTesting(TargetedRegenerationPlan& plan,
                                                        std::uint32_t schema_version) noexcept;

[[nodiscard]] bool TargetedRegenerationConfigIsValidV1(
    const TargetedRegenerationConfig& config) noexcept;

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

using TargetedRegenerationChecksumHeaderV2 = TargetedRegenerationChecksumHeaderV1;

struct TargetedRegenerationChecksumHeaderV3 : TargetedRegenerationChecksumHeaderV2 {
  std::uint64_t coverage_seed_target_count = 0;
};

struct TargetedRegenerationResourceScanV1 {
  std::uint64_t conflict_resource_count = 0;
  std::uint64_t conflict_impact = 0;
  std::uint64_t negotiated_price_exposure = 0;
  std::vector<RegenerationResourceAction> resource_actions;
};

struct TargetedRegenerationRetentionV3ForTesting {
  std::vector<TargetedRegenerationNet> coverage_seeds;
  std::vector<TargetedRegenerationNet> fallback_targets;

  friend bool operator==(const TargetedRegenerationRetentionV3ForTesting&,
                         const TargetedRegenerationRetentionV3ForTesting&) = default;
};

struct TargetedRegenerationPolicyEntryProjectionV1 {
  std::uint64_t target_legal_price_count = 0;
  std::uint64_t aggregate_entry_count = 0;

  friend bool operator==(const TargetedRegenerationPolicyEntryProjectionV1&,
                         const TargetedRegenerationPolicyEntryProjectionV1&) = default;
};

using TargetedRegenerationResourceScanResultV1 =
    std::variant<TargetedRegenerationResourceScanV1, TargetedRegenerationError>;
using TargetedRegenerationRetentionResultV3ForTesting =
    std::variant<TargetedRegenerationRetentionV3ForTesting, TargetedRegenerationError>;

[[nodiscard]] bool TargetedRegenerationTargetRanksBeforeV1(
    const TargetedRegenerationNet& left, const TargetedRegenerationNet& right) noexcept;

// Counts the exact aggregate policy entries without allocating policy storage.
// Inputs are expected to have passed canonical price/action validation.
[[nodiscard]] TargetedRegenerationPolicyEntryProjectionV1
ProjectTargetedRegenerationPolicyEntriesV1(const geometry_compiler::CompiledBoard& compiled_board,
                                           std::span<const NegotiatedResourcePrice> prices,
                                           const TargetedRegenerationNet& target) noexcept;

[[nodiscard]] bool TargetedRegenerationPolicyEntriesFitV1(std::uint64_t projected_entries,
                                                          std::uint64_t maximum_entries) noexcept;

// Production resource scan shared with arithmetic/ranking regressions. Inputs
// are expected to have passed the complete negotiated-price/world validation.
[[nodiscard]] TargetedRegenerationResourceScanResultV1 ScanTargetedRegenerationResourcesV1(
    std::span<const candidates::PhysicalEdgeSpan> spans,
    std::span<const ResourceUsage> world_resources, std::span<const NegotiatedResourcePrice> prices,
    std::uint64_t maximum_retained_actions);

// Source-private coverage/fallback retention seam over already authenticated
// provisional targets. Production and tests share the same bounded collector.
[[nodiscard]] TargetedRegenerationRetentionResultV3ForTesting
RetainTargetedRegenerationTargetsV3ForTesting(
    std::span<const TargetedRegenerationNet> provisional_targets,
    std::uint64_t maximum_fallback_targets, std::uint64_t coverage_capacity);

[[nodiscard]] std::uint64_t ComputeTargetedRegenerationCoverageCapacityV3ForTesting(
    const TargetedRegenerationConfig& config, std::uint64_t candidate_headroom) noexcept;

[[nodiscard]] std::uint64_t ComputeTargetedRegenerationPlanChecksumV1(
    const TargetedRegenerationChecksumHeaderV1& header,
    std::span<const TargetedRegenerationNet> targets) noexcept;

[[nodiscard]] std::uint64_t ComputeTargetedRegenerationPlanChecksumV2(
    const TargetedRegenerationChecksumHeaderV2& header,
    std::span<const TargetedRegenerationNet> targets) noexcept;

[[nodiscard]] std::uint64_t ComputeTargetedRegenerationPlanChecksumV3(
    const TargetedRegenerationChecksumHeaderV3& header,
    std::span<const TargetedRegenerationNet> targets) noexcept;

// Source-private one-shot drift injection immediately before the provisional
// primary/full-rescan replay check.
void SetTargetedRegenerationPrimaryReplayMismatchForTesting(bool enabled) noexcept;

// Source-private one-shot inconsistent-summary injection into the exact-net
// coverage/fallback union.
void SetTargetedRegenerationUnionMismatchForTesting(bool enabled) noexcept;

}  // namespace apgar::allocator::internal

#endif  // APGAR_SRC_ALLOCATOR_TARGETED_REGENERATION_INTERNAL_H_
