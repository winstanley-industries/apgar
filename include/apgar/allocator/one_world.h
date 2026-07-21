#ifndef APGAR_ALLOCATOR_ONE_WORLD_H_
#define APGAR_ALLOCATOR_ONE_WORLD_H_

#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "apgar/allocator/multi_net_workload.h"
#include "apgar/board_ir/board.h"
#include "apgar/candidates/candidate_store.h"
#include "apgar/routing/candidate_policy.h"

namespace apgar::allocator {

inline constexpr std::uint32_t kResourceCapacityModelSchemaVersion = 1;
inline constexpr std::uint32_t kPriceSnapshotSchemaVersion = 1;
inline constexpr std::uint32_t kOneWorldAllocationSchemaVersionV1 = 1;
inline constexpr std::uint32_t kOneWorldAllocationSchemaVersion = 2;

inline constexpr std::uint64_t kMaximumAllocatorNetsV1 = 1'000'000;
inline constexpr std::uint64_t kMaximumAllocatorCandidatesV1 = 1'000'000;
inline constexpr std::uint64_t kMaximumAllocatorResourceRecordsV1 = 1'000'000;
inline constexpr std::uint64_t kMaximumAllocatorExpandedResourceUsesV1 = 100'000'000;

// One common immutable board/compiler context. Routing-profile and rule-bucket
// associations remain candidate-local because a Phase 4 workload may contain
// multiple compatible rule buckets over the same compiled resource lattice.
struct AllocationAssociations {
  std::uint64_t board_content_hash = 0;
  std::uint64_t compiler_profile_fingerprint = 0;
  std::uint32_t geometry_compiler_version = 0;

  friend bool operator==(const AllocationAssociations&, const AllocationAssociations&) = default;
};

enum class AllocationErrorCode : std::uint8_t {
  kUnsupportedSchema = 0,
  kInvalidConfiguration = 1,
  kInputBoundExceeded = 2,
  kInvalidResource = 3,
  kDuplicateResource = 4,
  kDuplicateNet = 5,
  kNullCandidate = 6,
  kCandidateNetMismatch = 7,
  kCandidateAssociationMismatch = 8,
  kDuplicateCandidate = 9,
  kCandidateInvariant = 10,
  kCostOverflow = 11,
  kUsageOverflow = 12,
  kResourceExhausted = 13,
};

struct AllocationError {
  AllocationErrorCode code = AllocationErrorCode::kInvalidConfiguration;
  // All allocator diagnostics are stable static text. Non-owning views keep the
  // resource-exhausted fallback allocation-free after std::bad_alloc.
  std::string_view invariant_id;
  std::string_view detail;

  friend bool operator==(const AllocationError&, const AllocationError&) = default;
};

struct ResourceCapacityOverride {
  routing::EdgeResourceKey resource;
  std::uint32_t capacity_units = 0;

  friend bool operator==(const ResourceCapacityOverride&,
                         const ResourceCapacityOverride&) = default;
};

// Every exactly admitted physical edge receives default_capacity_units unless
// one canonical override names it. Zero capacity is valid and represents an
// allocator-visible unavailable resource; it does not make candidate geometry
// exact-illegal.
class ResourceCapacityModel {
 public:
  ResourceCapacityModel(const ResourceCapacityModel&) = default;
  ResourceCapacityModel(ResourceCapacityModel&&) noexcept = default;
  ResourceCapacityModel& operator=(const ResourceCapacityModel&) = default;
  ResourceCapacityModel& operator=(ResourceCapacityModel&&) noexcept = default;

  [[nodiscard]] std::uint32_t schema_version() const noexcept { return schema_version_; }
  [[nodiscard]] const AllocationAssociations& associations() const noexcept {
    return associations_;
  }
  [[nodiscard]] std::uint32_t default_capacity_units() const noexcept {
    return default_capacity_units_;
  }
  [[nodiscard]] const std::vector<ResourceCapacityOverride>& overrides() const noexcept {
    return overrides_;
  }

  friend bool operator==(const ResourceCapacityModel&, const ResourceCapacityModel&) = default;

 private:
  ResourceCapacityModel(std::uint32_t schema_version, AllocationAssociations associations,
                        std::uint32_t default_capacity_units,
                        std::vector<ResourceCapacityOverride> overrides)
      : schema_version_(schema_version),
        associations_(associations),
        default_capacity_units_(default_capacity_units),
        overrides_(std::move(overrides)) {}

  std::uint32_t schema_version_ = kResourceCapacityModelSchemaVersion;
  AllocationAssociations associations_;
  std::uint32_t default_capacity_units_ = 1;
  std::vector<ResourceCapacityOverride> overrides_;

  friend std::variant<ResourceCapacityModel, AllocationError> BuildResourceCapacityModel(
      std::uint32_t, const board_ir::BoardSnapshot&, const geometry_compiler::CompiledBoard&,
      std::uint32_t, const std::vector<ResourceCapacityOverride>&);
  friend std::variant<ResourceCapacityModel, AllocationError> BuildResourceCapacityModel(
      std::uint32_t, const board_ir::BoardSnapshot&, const geometry_compiler::CompiledBoard&,
      std::uint32_t, std::vector<ResourceCapacityOverride>&&);
};

using ResourceCapacityModelResult = std::variant<ResourceCapacityModel, AllocationError>;

// Creation factory for immutable resource capacity state. The
// returned value cannot be association-restamped after construction.
[[nodiscard]] ResourceCapacityModelResult BuildResourceCapacityModel(
    std::uint32_t schema_version, const board_ir::BoardSnapshot& board,
    const geometry_compiler::CompiledBoard& compiled_board, std::uint32_t default_capacity_units,
    const std::vector<ResourceCapacityOverride>& overrides);
[[nodiscard]] ResourceCapacityModelResult BuildResourceCapacityModel(
    std::uint32_t schema_version, const board_ir::BoardSnapshot& board,
    const geometry_compiler::CompiledBoard& compiled_board, std::uint32_t default_capacity_units,
    std::vector<ResourceCapacityOverride>&& overrides);

struct ResourcePrice {
  routing::EdgeResourceKey resource;
  std::uint64_t price_per_usage_unit = 0;

  friend bool operator==(const ResourcePrice&, const ResourcePrice&) = default;
};

class PriceSnapshot {
 public:
  PriceSnapshot(const PriceSnapshot&) = default;
  PriceSnapshot(PriceSnapshot&&) noexcept = default;
  PriceSnapshot& operator=(const PriceSnapshot&) = default;
  PriceSnapshot& operator=(PriceSnapshot&&) noexcept = default;

  [[nodiscard]] std::uint32_t schema_version() const noexcept { return schema_version_; }
  [[nodiscard]] const AllocationAssociations& associations() const noexcept {
    return associations_;
  }
  [[nodiscard]] std::uint32_t iteration() const noexcept { return iteration_; }
  [[nodiscard]] const std::vector<ResourcePrice>& prices() const noexcept { return prices_; }

  friend bool operator==(const PriceSnapshot&, const PriceSnapshot&) = default;

 private:
  PriceSnapshot(std::uint32_t schema_version, AllocationAssociations associations,
                std::uint32_t iteration, std::vector<ResourcePrice> prices)
      : schema_version_(schema_version),
        associations_(associations),
        iteration_(iteration),
        prices_(std::move(prices)) {}

  std::uint32_t schema_version_ = kPriceSnapshotSchemaVersion;
  AllocationAssociations associations_;
  std::uint32_t iteration_ = 0;
  std::vector<ResourcePrice> prices_;

  friend std::variant<PriceSnapshot, AllocationError> BuildPriceSnapshot(
      std::uint32_t, const ResourceCapacityModel&, std::uint32_t,
      const std::vector<ResourcePrice>&);
  friend std::variant<PriceSnapshot, AllocationError> BuildPriceSnapshot(
      std::uint32_t, const ResourceCapacityModel&, std::uint32_t, std::vector<ResourcePrice>&&);
};

using PriceSnapshotResult = std::variant<PriceSnapshot, AllocationError>;

// Creation factory for immutable resource prices. Later
// negotiated updates must produce a new snapshot rather than relabel this one.
[[nodiscard]] PriceSnapshotResult BuildPriceSnapshot(std::uint32_t schema_version,
                                                     const ResourceCapacityModel& capacities,
                                                     std::uint32_t iteration,
                                                     const std::vector<ResourcePrice>& prices);
[[nodiscard]] PriceSnapshotResult BuildPriceSnapshot(std::uint32_t schema_version,
                                                     const ResourceCapacityModel& capacities,
                                                     std::uint32_t iteration,
                                                     std::vector<ResourcePrice>&& prices);

struct OneWorldAllocatorLimits {
  std::uint64_t maximum_nets = 100'000;
  std::uint64_t maximum_candidates = 1'000'000;
  std::uint64_t maximum_resource_records = 1'000'000;
  std::uint64_t maximum_expanded_resource_uses = 100'000'000;

  friend bool operator==(const OneWorldAllocatorLimits&, const OneWorldAllocatorLimits&) = default;
};

struct CandidatePool {
  board_ir::EntityRef net{};
  std::vector<candidates::StoredCandidate> candidates;
};

struct OneWorldAllocationRequest {
  std::uint32_t schema_version = kOneWorldAllocationSchemaVersion;
  AllocationAssociations associations;
  ResourceCapacityModel capacities;
  PriceSnapshot prices;
  // The first reference objective is deliberately narrow and explainable.
  // Cross-policy candidates compare their independently reconstructed
  // intrinsic base cost, multiplied by this positive fixed-point weight.
  std::uint64_t intrinsic_cost_weight = 1;
  OneWorldAllocatorLimits limits;
  std::vector<CandidatePool> pools;
  // When present, the canonical workload is the authoritative routable-net
  // roster and per-net profile/rule association. Exactly one pool, including
  // an explicit empty pool, is required for every workload net.
  const MultiNetWorkload* workload = nullptr;
};

enum class NetSelectionStatus : std::uint8_t {
  kSelected = 0,
  kNoAdmissibleCandidate = 1,
};

struct NetSelection {
  board_ir::EntityRef net{};
  NetSelectionStatus status = NetSelectionStatus::kNoAdmissibleCandidate;
  std::optional<candidates::CandidateId> candidate_id;
  std::optional<std::uint64_t> candidate_payload_checksum;
  // Retains the exact immutable candidate for downstream use. Stable identity
  // and serialization use candidate_id rather than pointer identity.
  candidates::StoredCandidate candidate;
  std::uint64_t intrinsic_cost = 0;
  std::uint64_t price_cost = 0;
  std::uint64_t selection_score = 0;
};

struct ResourceUsage {
  routing::EdgeResourceKey resource;
  bool has_capacity_override = false;
  std::uint32_t capacity_units = 0;
  std::uint64_t usage_units = 0;
  std::uint64_t overuse_units = 0;
  bool has_explicit_price = false;
  std::uint64_t price_per_usage_unit = 0;

  friend bool operator==(const ResourceUsage&, const ResourceUsage&) = default;
};

struct OneWorldAllocation {
  std::uint32_t schema_version = kOneWorldAllocationSchemaVersion;
  AllocationAssociations associations;
  std::uint64_t workload_checksum = 0;
  std::uint32_t price_iteration = 0;
  std::uint32_t default_capacity_units = 0;
  std::uint64_t intrinsic_cost_weight = 0;
  std::vector<NetSelection> selections;
  std::vector<ResourceUsage> resources;
  std::uint64_t selected_net_count = 0;
  std::uint64_t no_candidate_net_count = 0;
  std::uint64_t overused_resource_count = 0;
  std::uint64_t total_overuse_units = 0;
  std::uint64_t total_intrinsic_cost = 0;
  std::uint64_t total_selection_score = 0;
  // Deterministic work counters distinguish the zero-price fast path and make
  // compressed reference-path scaling externally visible.
  std::uint64_t scoring_span_queries = 0;
  std::uint64_t scoring_price_matches = 0;
  // Logical repeated selected-footprint volume is an input quantity, not work.
  std::uint64_t selected_logical_resource_uses = 0;
  // Unique atomic resources actually materialized by the accounting sweep.
  std::uint64_t accounting_materialized_resource_edges = 0;
  // Two processed events per selected compressed span.
  std::uint64_t accounting_span_boundary_events = 0;
  // FNV-1a over the canonical externally visible world. This is a stable
  // replay checksum, not a cryptographic integrity claim.
  std::uint64_t world_checksum = 0;
};

using OneWorldAllocationResult = std::variant<OneWorldAllocation, AllocationError>;

// Deterministic single-world CPU reference. Input order is not semantic:
// pools, candidates, capacities, and prices are canonicalized before work.
// Each nonempty pool selects exactly one candidate by
// (intrinsic + resource-price score, candidate ID). Empty pools produce one
// structured kNoAdmissibleCandidate outcome.
[[nodiscard]] OneWorldAllocationResult AllocateOneWorld(const OneWorldAllocationRequest& request);

}  // namespace apgar::allocator

#endif  // APGAR_ALLOCATOR_ONE_WORLD_H_
