#ifndef APGAR_ROUTING_CANDIDATE_POLICY_H_
#define APGAR_ROUTING_CANDIDATE_POLICY_H_

#include <compare>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

#include "apgar/board_ir/board.h"
#include "apgar/geometry_compiler/compiled_board.h"

namespace apgar::routing {

inline constexpr std::uint32_t kCandidateGenerationPolicySchemaVersion = 1;
inline constexpr std::uint64_t kMaximumPolicyResourceEntries = 1'000'000;
inline constexpr std::uint32_t kMaximumAlternativePolicyCount = 1'000'000;
// Stable heading-state sentinel shared by policy cost reconstruction and every
// planar search implementation. Valid movement directions occupy [0, 8).
inline constexpr std::uint8_t kNoIncomingDirection =
    static_cast<std::uint8_t>(geometry_compiler::kStableDirectionOrder.size());

enum class CandidateObjective : std::uint8_t {
  kBaseScalarCost = 0,
  kLengthBiased = 1,
  kBendBiased = 2,
  kResourceDiverse = 3,
};

// One collision-free physical planar edge. Reverse traversal maps to the same
// canonical key and one of the four positive canonical directions.
struct EdgeResourceKey {
  board_ir::LayerId layer = 0;
  std::int64_t lattice_x = 0;
  std::int64_t lattice_y = 0;
  geometry_compiler::Direction direction = geometry_compiler::Direction::kEast;

  friend bool operator==(const EdgeResourceKey&, const EdgeResourceKey&) = default;
  friend auto operator<=>(const EdgeResourceKey&, const EdgeResourceKey&) = default;
};

struct ResourcePenalty {
  EdgeResourceKey resource;
  std::uint64_t additional_cost = 0;

  friend bool operator==(const ResourcePenalty&, const ResourcePenalty&) = default;
  friend auto operator<=>(const ResourcePenalty&, const ResourcePenalty&) = default;
};

// DeviceCandidateBatch host-memory schema v1 reserves 40 bytes of
// normalization scratch per submitted resource entry. Keep layout growth from
// silently invalidating that deterministic upper bound.
static_assert(sizeof(EdgeResourceKey) <= 40);
static_assert(sizeof(ResourcePenalty) <= 40);

struct CandidateGenerationPolicy {
  std::uint32_t schema_version = kCandidateGenerationPolicySchemaVersion;
  CandidateObjective objective = CandidateObjective::kBaseScalarCost;
  std::uint64_t deterministic_seed = 0;
  std::uint32_t candidate_ordinal = 0;
  std::uint64_t orthogonal_step_surcharge = 0;
  std::uint64_t diagonal_step_surcharge = 0;
  std::uint64_t bend_surcharge = 0;
  std::vector<EdgeResourceKey> banned_resources;
  std::vector<ResourcePenalty> resource_penalties;

  friend bool operator==(const CandidateGenerationPolicy&,
                         const CandidateGenerationPolicy&) = default;
};

enum class CandidatePolicyErrorCode : std::uint8_t {
  kUnsupportedSchema,
  kUnsupportedObjective,
  kInvalidResource,
  kConflictingResourceAction,
  kTooManyResources,
  kCostOverflow,
  kInvalidAlternativeSchedule,
};

struct CandidatePolicyError {
  CandidatePolicyErrorCode code;
  std::string detail;

  friend bool operator==(const CandidatePolicyError&, const CandidatePolicyError&) = default;
};

struct NormalizedCandidateGenerationPolicy {
  CandidateGenerationPolicy policy;
  std::uint64_t identity = 0;

  friend bool operator==(const NormalizedCandidateGenerationPolicy&,
                         const NormalizedCandidateGenerationPolicy&) = default;
};

using CandidatePolicyResult =
    std::variant<NormalizedCandidateGenerationPolicy, CandidatePolicyError>;

// A backend-neutral, geometry-independent schedule for precomputing the same
// ordered policy batch for CPU and GPU execution. Candidate zero is the
// normalized base policy. Later candidates cycle objective variation, scalar
// surcharge, resource penalty, and resource ban modes. Increment strengths
// increase deterministically after each complete cycle.
struct DeterministicAlternativePolicySchedule {
  std::uint32_t candidate_count = 1;
  std::uint64_t step_surcharge_increment = 1;
  std::uint64_t bend_surcharge_increment = 1;
  std::uint64_t resource_penalty_increment = 1;
  std::vector<EdgeResourceKey> alternative_resources;

  friend bool operator==(const DeterministicAlternativePolicySchedule&,
                         const DeterministicAlternativePolicySchedule&) = default;
};

using CandidatePolicyBatchResult =
    std::variant<std::vector<NormalizedCandidateGenerationPolicy>, CandidatePolicyError>;

[[nodiscard]] std::optional<EdgeResourceKey> CanonicalPhysicalEdgeResource(
    board_ir::LayerId layer, geometry_compiler::LatticeIndex source,
    geometry_compiler::Direction direction) noexcept;

[[nodiscard]] bool ResourceExists(const geometry_compiler::CompiledBoard& board,
                                  const EdgeResourceKey& resource) noexcept;

[[nodiscard]] std::uint64_t FingerprintCandidateGenerationPolicy(
    const CandidateGenerationPolicy& normalized_policy) noexcept;

[[nodiscard]] std::uint64_t FingerprintRoutingProfile(
    const board_ir::RoutingProfile& profile) noexcept;

// Public policy containers are untrusted until this O(1) shape check passes.
// Callers that own a copy, hash, sort, or walk policy resources must perform
// this check first. Normalization performs the check internally before making
// its canonical owned copy.
[[nodiscard]] bool CandidateGenerationPolicyShapeIsWithinV1Bounds(
    const CandidateGenerationPolicy& policy) noexcept;

[[nodiscard]] CandidatePolicyResult NormalizeCandidateGenerationPolicy(
    const geometry_compiler::CompiledBoard& board,
    const CandidateGenerationPolicy& submitted_policy);

[[nodiscard]] CandidatePolicyBatchResult BuildDeterministicAlternativePolicies(
    const geometry_compiler::CompiledBoard& board, CandidateGenerationPolicy base_policy,
    DeterministicAlternativePolicySchedule schedule);

[[nodiscard]] bool PolicyBansResource(const CandidateGenerationPolicy& normalized_policy,
                                      const EdgeResourceKey& resource) noexcept;

[[nodiscard]] std::uint64_t PolicyPenaltyForResource(
    const CandidateGenerationPolicy& normalized_policy, const EdgeResourceKey& resource) noexcept;

// Returns nullopt only on arithmetic overflow. Callers check PolicyBansResource
// first; a ban is absence of an edge rather than a finite transition cost.
[[nodiscard]] std::optional<std::uint64_t> StepCostUnderPolicy(
    const geometry_compiler::CompilerProfile& profile, geometry_compiler::Direction direction,
    std::uint8_t incoming_direction, const CandidateGenerationPolicy& normalized_policy,
    const EdgeResourceKey& resource) noexcept;

}  // namespace apgar::routing

#endif  // APGAR_ROUTING_CANDIDATE_POLICY_H_
