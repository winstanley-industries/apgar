#ifndef APGAR_SRC_ALLOCATOR_NEGOTIATED_PRICES_INTERNAL_H_
#define APGAR_SRC_ALLOCATOR_NEGOTIATED_PRICES_INTERNAL_H_

#include <cstdint>
#include <span>
#include <string_view>

#include "apgar/allocator/negotiated_prices.h"

namespace apgar::allocator::internal {

[[nodiscard]] bool NegotiatedPriceConfigIsValidV1(const NegotiatedPriceConfig& config) noexcept;

struct ResourceCapacityChecksumHeaderV1 {
  std::uint32_t schema_version = 0;
  AllocationAssociations associations;
  std::uint32_t default_capacity_units = 0;
};

struct NegotiatedPriceChecksumHeaderV1 {
  std::uint32_t schema_version = 0;
  AllocationAssociations associations;
  std::uint64_t workload_checksum = 0;
  std::uint64_t capacity_model_checksum = 0;
  NegotiatedPriceConfig config;
  std::uint32_t iteration = 0;
  std::uint64_t predecessor_state_checksum = 0;
  std::uint64_t source_world_checksum = 0;
  std::uint64_t clamped_resource_count = 0;
  std::uint64_t total_present_price = 0;
  std::uint64_t total_history_price = 0;
  std::uint64_t total_price = 0;
};

// Representation-level stable encoder used by production and golden tests.
[[nodiscard]] std::uint64_t ComputeNegotiatedPriceStateChecksumV1(
    const NegotiatedPriceChecksumHeaderV1& header,
    std::span<const NegotiatedResourcePrice> prices) noexcept;

[[nodiscard]] std::uint64_t RecomputeNegotiatedPriceStateChecksumV1(
    const NegotiatedPriceState& state) noexcept;

// Pure bound helper shared by the update path and boundary regression tests.
[[nodiscard]] bool NegotiatedPriceRosterFitsV1(std::uint64_t capacity_records,
                                               std::uint64_t price_records,
                                               std::uint64_t configured_price_limit) noexcept;

// Representation-level stable encoder used by production and golden tests.
[[nodiscard]] std::uint64_t ComputeResourceCapacityModelChecksumV1(
    const ResourceCapacityChecksumHeaderV1& header,
    std::span<const ResourceCapacityOverride> overrides) noexcept;

[[nodiscard]] std::uint64_t RecomputeResourceCapacityModelChecksumV1(
    const ResourceCapacityModel& capacities) noexcept;

// Nested allocator factories already consume allocation exceptions. Preserve
// that classification instead of relabeling exhaustion as corrupt input.
[[nodiscard]] NegotiatedPriceError TranslateNestedAllocationError(
    const AllocationError& failure, NegotiatedPriceErrorCode fallback_code,
    std::string_view fallback_invariant, std::string_view fallback_detail) noexcept;

}  // namespace apgar::allocator::internal

#endif  // APGAR_SRC_ALLOCATOR_NEGOTIATED_PRICES_INTERNAL_H_
