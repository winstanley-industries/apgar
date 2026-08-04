#ifndef APGAR_ALLOCATOR_RESOURCE_ACCOUNTING_H_
#define APGAR_ALLOCATOR_RESOURCE_ACCOUNTING_H_

#include <cstdint>
#include <limits>
#include <span>
#include <string_view>
#include <variant>
#include <vector>

#include "apgar/board_ir/board.h"
#include "apgar/candidates/route_candidate.h"
#include "apgar/geometry_compiler/compiled_board.h"
#include "apgar/routing/candidate_policy.h"

namespace apgar::allocator {

inline constexpr std::uint64_t kMaximumResourceAccountingCandidates = 1'000'000;
inline constexpr std::uint64_t kMaximumResourceAccountingExpandedUses = 100'000'000;

// One common immutable resource lattice. Routing-profile identity remains
// candidate-local because distinct routed nets intentionally have different
// APGAR-ROUTING-PROFILE-V1 fingerprints over the same physical lattice.
struct ResourceLatticeAssociations {
  std::uint64_t board_content_hash = 0;
  std::uint64_t compiler_profile_fingerprint = 0;
  std::uint32_t geometry_compiler_version = 0;

  friend bool operator==(const ResourceLatticeAssociations&,
                         const ResourceLatticeAssociations&) = default;
};

enum class ResourceAccountingErrorCode : std::uint8_t {
  kInvalidCapacity = 0,
  kAssociationMismatch = 1,
  kInvalidLimits = 2,
  kInputBoundExceeded = 3,
  kNullCandidate = 4,
  kDuplicateCandidate = 5,
  kCandidateAssociationMismatch = 6,
  kCandidateInvariant = 7,
  kUsageOverflow = 8,
  kResourceExhausted = 9,
};

struct ResourceAccountingError {
  ResourceAccountingErrorCode code = ResourceAccountingErrorCode::kInvalidCapacity;
  std::string_view invariant_id;
  std::string_view detail;

  friend bool operator==(const ResourceAccountingError&, const ResourceAccountingError&) = default;
};

// RouteCandidate v1 resources are atomic physical edges with one usage unit,
// so the compatible capacity vocabulary for this slice is deliberately
// binary. Zero makes an exactly legal edge allocator-unavailable; one permits
// one selected candidate use. Exact legality is unchanged.
class ResourceCapacityModel {
 public:
  ResourceCapacityModel(const ResourceCapacityModel&) = default;
  ResourceCapacityModel(ResourceCapacityModel&&) noexcept = default;
  ResourceCapacityModel& operator=(const ResourceCapacityModel&) = default;
  ResourceCapacityModel& operator=(ResourceCapacityModel&&) noexcept = default;

  [[nodiscard]] const ResourceLatticeAssociations& associations() const noexcept {
    return associations_;
  }
  [[nodiscard]] std::uint32_t capacity_units() const noexcept { return capacity_units_; }

  friend bool operator==(const ResourceCapacityModel&, const ResourceCapacityModel&) = default;

 private:
  ResourceCapacityModel(ResourceLatticeAssociations associations, std::uint32_t capacity_units)
      : associations_(associations), capacity_units_(capacity_units) {}

  ResourceLatticeAssociations associations_;
  std::uint32_t capacity_units_ = 1;

  friend std::variant<ResourceCapacityModel, ResourceAccountingError> BuildResourceCapacityModel(
      const board_ir::BoardSnapshot&, const geometry_compiler::CompiledBoard&, std::uint32_t);
};

using ResourceCapacityModelResult = std::variant<ResourceCapacityModel, ResourceAccountingError>;

// Derives immutable lattice association from actual Board IR and compiled
// artifacts. Caller-selected association labels cannot restamp a model.
[[nodiscard]] ResourceCapacityModelResult BuildResourceCapacityModel(
    const board_ir::BoardSnapshot& board, const geometry_compiler::CompiledBoard& compiled_board,
    std::uint32_t capacity_units);

struct ResourceAccountingLimits {
  std::uint64_t maximum_candidates = kMaximumResourceAccountingCandidates;
  std::uint64_t maximum_expanded_resource_uses = kMaximumResourceAccountingExpandedUses;
  // A configurable arithmetic-domain ceiling makes the checked accumulation
  // path discriminating on small tests while production defaults to uint64.
  std::uint64_t maximum_usage_units_per_resource = std::numeric_limits<std::uint64_t>::max();

  friend bool operator==(const ResourceAccountingLimits&,
                         const ResourceAccountingLimits&) = default;
};

struct ResourceUsage {
  routing::EdgeResourceKey resource;
  std::uint32_t capacity_units = 0;
  std::uint64_t usage_units = 0;
  std::uint64_t overuse_units = 0;

  friend bool operator==(const ResourceUsage&, const ResourceUsage&) = default;
};

struct ResourceAccounting {
  ResourceLatticeAssociations associations;
  std::vector<ResourceUsage> resources;
  std::uint64_t candidate_count = 0;
  std::uint64_t expanded_resource_uses = 0;
  std::uint64_t overused_resource_count = 0;
  std::uint64_t total_overuse_units = 0;

  friend bool operator==(const ResourceAccounting&, const ResourceAccounting&) = default;
};

using ResourceAccountingResult = std::variant<ResourceAccounting, ResourceAccountingError>;

// Deterministic CPU reference accumulation over exactly the immutable admitted
// candidates supplied by the caller. The function does not choose, rank,
// score, regenerate, or otherwise mutate candidates. Input order is not
// semantic; output resources are sorted by canonical EdgeResourceKey.
[[nodiscard]] ResourceAccountingResult AccumulateResourceUsage(
    const ResourceCapacityModel& capacities,
    std::span<const candidates::RouteCandidate* const> candidates,
    ResourceAccountingLimits limits = {});

}  // namespace apgar::allocator

#endif  // APGAR_ALLOCATOR_RESOURCE_ACCOUNTING_H_
