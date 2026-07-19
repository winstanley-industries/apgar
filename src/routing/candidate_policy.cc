#include "apgar/routing/candidate_policy.h"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <ranges>
#include <set>
#include <utility>

#include "apgar/board_ir/stable_hash.h"

namespace apgar::routing {
namespace {

using UWide = __uint128_t;

[[nodiscard]] bool IsCanonicalDirection(geometry_compiler::Direction direction) noexcept {
  return direction == geometry_compiler::Direction::kEast ||
         direction == geometry_compiler::Direction::kNorthEast ||
         direction == geometry_compiler::Direction::kNorth ||
         direction == geometry_compiler::Direction::kNorthWest;
}

[[nodiscard]] bool ObjectiveIsSupported(CandidateObjective objective) noexcept {
  switch (objective) {
    case CandidateObjective::kBaseScalarCost:
    case CandidateObjective::kLengthBiased:
    case CandidateObjective::kBendBiased:
    case CandidateObjective::kResourceDiverse:
      return true;
  }
  return false;
}

[[nodiscard]] CandidatePolicyError Error(CandidatePolicyErrorCode code, std::string detail) {
  return CandidatePolicyError{.code = code, .detail = std::move(detail)};
}

[[nodiscard]] std::optional<std::uint64_t> CheckedScale(std::uint64_t value,
                                                        std::uint64_t multiplier) noexcept {
  const UWide product = static_cast<UWide>(value) * multiplier;
  if (product > std::numeric_limits<std::uint64_t>::max()) {
    return std::nullopt;
  }
  return static_cast<std::uint64_t>(product);
}

[[nodiscard]] bool CheckedAccumulate(std::uint64_t increment, std::uint64_t* value) noexcept {
  if (increment > std::numeric_limits<std::uint64_t>::max() - *value) {
    return false;
  }
  *value += increment;
  return true;
}

void HashResource(board_ir::StableHashBuilder& hash, const EdgeResourceKey& resource) noexcept {
  hash.AddU32(resource.layer);
  hash.AddI64(resource.lattice_x);
  hash.AddI64(resource.lattice_y);
  hash.AddByte(static_cast<std::uint8_t>(resource.direction));
}

}  // namespace

std::optional<EdgeResourceKey> CanonicalPhysicalEdgeResource(
    board_ir::LayerId layer, geometry_compiler::LatticeIndex source,
    geometry_compiler::Direction direction) noexcept {
  if (static_cast<std::uint8_t>(direction) >= geometry_compiler::kStableDirectionOrder.size()) {
    return std::nullopt;
  }
  geometry_compiler::Direction canonical = direction;
  __int128_t x = source.x;
  __int128_t y = source.y;
  switch (direction) {
    case geometry_compiler::Direction::kEast:
    case geometry_compiler::Direction::kNorthEast:
    case geometry_compiler::Direction::kNorth:
    case geometry_compiler::Direction::kNorthWest:
      break;
    case geometry_compiler::Direction::kWest:
      --x;
      canonical = geometry_compiler::Direction::kEast;
      break;
    case geometry_compiler::Direction::kSouthWest:
      --x;
      --y;
      canonical = geometry_compiler::Direction::kNorthEast;
      break;
    case geometry_compiler::Direction::kSouth:
      --y;
      canonical = geometry_compiler::Direction::kNorth;
      break;
    case geometry_compiler::Direction::kSouthEast:
      ++x;
      --y;
      canonical = geometry_compiler::Direction::kNorthWest;
      break;
  }
  if (x < std::numeric_limits<std::int64_t>::min() ||
      x > std::numeric_limits<std::int64_t>::max() ||
      y < std::numeric_limits<std::int64_t>::min() ||
      y > std::numeric_limits<std::int64_t>::max()) {
    return std::nullopt;
  }
  return EdgeResourceKey{.layer = layer,
                         .lattice_x = static_cast<std::int64_t>(x),
                         .lattice_y = static_cast<std::int64_t>(y),
                         .direction = canonical};
}

bool ResourceExists(const geometry_compiler::CompiledBoard& board,
                    const EdgeResourceKey& resource) noexcept {
  if (!IsCanonicalDirection(resource.direction) ||
      !board.EdgeIsLegal(resource.layer, resource.lattice_x, resource.lattice_y,
                         resource.direction)) {
    return false;
  }
  const geometry_compiler::DirectionDelta delta = geometry_compiler::DeltaFor(resource.direction);
  const __int128_t destination_x = static_cast<__int128_t>(resource.lattice_x) + delta.x;
  const __int128_t destination_y = static_cast<__int128_t>(resource.lattice_y) + delta.y;
  if (destination_x < std::numeric_limits<std::int64_t>::min() ||
      destination_x > std::numeric_limits<std::int64_t>::max() ||
      destination_y < std::numeric_limits<std::int64_t>::min() ||
      destination_y > std::numeric_limits<std::int64_t>::max()) {
    return false;
  }
  return board.EdgeIsLegal(resource.layer, static_cast<std::int64_t>(destination_x),
                           static_cast<std::int64_t>(destination_y),
                           geometry_compiler::Opposite(resource.direction));
}

std::uint64_t FingerprintCandidateGenerationPolicy(
    const CandidateGenerationPolicy& normalized_policy) noexcept {
  board_ir::StableHashBuilder hash;
  hash.AddString("APGAR-CANDIDATE-POLICY-V1");
  hash.AddU32(normalized_policy.schema_version);
  hash.AddByte(static_cast<std::uint8_t>(normalized_policy.objective));
  hash.AddU64(normalized_policy.deterministic_seed);
  hash.AddU32(normalized_policy.candidate_ordinal);
  hash.AddU64(normalized_policy.orthogonal_step_surcharge);
  hash.AddU64(normalized_policy.diagonal_step_surcharge);
  hash.AddU64(normalized_policy.bend_surcharge);
  hash.AddU64(static_cast<std::uint64_t>(normalized_policy.banned_resources.size()));
  for (const EdgeResourceKey& resource : normalized_policy.banned_resources) {
    HashResource(hash, resource);
  }
  hash.AddU64(static_cast<std::uint64_t>(normalized_policy.resource_penalties.size()));
  for (const ResourcePenalty& penalty : normalized_policy.resource_penalties) {
    HashResource(hash, penalty.resource);
    hash.AddU64(penalty.additional_cost);
  }
  return hash.Finish();
}

std::uint64_t FingerprintRoutingProfile(const board_ir::RoutingProfile& profile) noexcept {
  board_ir::StableHashBuilder hash;
  hash.AddString("APGAR-ROUTING-PROFILE-V1");
  hash.AddU64(profile.net.id);
  hash.AddU32(profile.net.generation);
  hash.AddI64(profile.nominal_width);
  hash.AddI64(profile.clearance);
  hash.AddU64(static_cast<std::uint64_t>(profile.allowed_layers.size()));
  for (board_ir::LayerId layer : profile.allowed_layers) {
    hash.AddU32(layer);
  }
  hash.AddByte(profile.allowed_headings);
  return hash.Finish();
}

CandidatePolicyResult NormalizeCandidateGenerationPolicy(
    const geometry_compiler::CompiledBoard& board, CandidateGenerationPolicy policy) {
  if (policy.schema_version != kCandidateGenerationPolicySchemaVersion) {
    return Error(CandidatePolicyErrorCode::kUnsupportedSchema,
                 "Candidate policy schema version is unsupported");
  }
  if (!ObjectiveIsSupported(policy.objective)) {
    return Error(CandidatePolicyErrorCode::kUnsupportedObjective,
                 "Candidate policy objective is unsupported");
  }
  if (policy.banned_resources.size() > kMaximumPolicyResourceEntries ||
      policy.resource_penalties.size() > kMaximumPolicyResourceEntries ||
      policy.banned_resources.size() >
          kMaximumPolicyResourceEntries - policy.resource_penalties.size()) {
    return Error(CandidatePolicyErrorCode::kTooManyResources,
                 "Candidate policy exceeds the bounded resource-entry count");
  }

  std::ranges::sort(policy.banned_resources);
  policy.banned_resources.erase(std::ranges::unique(policy.banned_resources).begin(),
                                policy.banned_resources.end());
  for (const EdgeResourceKey& resource : policy.banned_resources) {
    if (!ResourceExists(board, resource)) {
      return Error(CandidatePolicyErrorCode::kInvalidResource,
                   "Candidate policy bans a resource absent from the CompiledBoard");
    }
  }

  std::ranges::sort(policy.resource_penalties);
  std::vector<ResourcePenalty> combined;
  combined.reserve(policy.resource_penalties.size());
  for (const ResourcePenalty& penalty : policy.resource_penalties) {
    if (!ResourceExists(board, penalty.resource)) {
      return Error(CandidatePolicyErrorCode::kInvalidResource,
                   "Candidate policy penalizes a resource absent from the CompiledBoard");
    }
    if (penalty.additional_cost == 0) {
      continue;
    }
    if (!combined.empty() && combined.back().resource == penalty.resource) {
      if (penalty.additional_cost >
          std::numeric_limits<std::uint64_t>::max() - combined.back().additional_cost) {
        return Error(CandidatePolicyErrorCode::kCostOverflow,
                     "Duplicate candidate resource penalties overflow uint64");
      }
      combined.back().additional_cost += penalty.additional_cost;
    } else {
      combined.push_back(penalty);
    }
  }
  policy.resource_penalties = std::move(combined);
  for (const ResourcePenalty& penalty : policy.resource_penalties) {
    if (std::ranges::binary_search(policy.banned_resources, penalty.resource)) {
      return Error(CandidatePolicyErrorCode::kConflictingResourceAction,
                   "A candidate policy resource cannot be both banned and penalized");
    }
  }

  const UWide orthogonal =
      static_cast<UWide>(board.profile().costs.orthogonal_step) + policy.orthogonal_step_surcharge;
  const UWide diagonal =
      static_cast<UWide>(board.profile().costs.diagonal_step) + policy.diagonal_step_surcharge;
  const UWide bend = static_cast<UWide>(board.profile().costs.bend) + policy.bend_surcharge;
  std::uint64_t maximum_penalty = 0;
  for (const ResourcePenalty& penalty : policy.resource_penalties) {
    maximum_penalty = std::max(maximum_penalty, penalty.additional_cost);
  }
  const UWide maximum_transition = std::max(orthogonal, diagonal) + bend + maximum_penalty;
  const UWide maximum_path = maximum_transition * board.telemetry().represented_nodes *
                             geometry_compiler::kStableDirectionOrder.size();
  if (orthogonal >= std::numeric_limits<std::uint64_t>::max() ||
      diagonal >= std::numeric_limits<std::uint64_t>::max() ||
      bend >= std::numeric_limits<std::uint64_t>::max() ||
      maximum_path >= std::numeric_limits<std::uint64_t>::max()) {
    return Error(CandidatePolicyErrorCode::kCostOverflow,
                 "Candidate policy cost bound collides with the unreachable sentinel");
  }

  const std::uint64_t identity = FingerprintCandidateGenerationPolicy(policy);
  return NormalizedCandidateGenerationPolicy{.policy = std::move(policy), .identity = identity};
}

CandidatePolicyBatchResult BuildDeterministicAlternativePolicies(
    const geometry_compiler::CompiledBoard& board, CandidateGenerationPolicy base_policy,
    DeterministicAlternativePolicySchedule schedule) {
  if (schedule.candidate_count == 0 || schedule.candidate_count > kMaximumAlternativePolicyCount) {
    return Error(CandidatePolicyErrorCode::kInvalidAlternativeSchedule,
                 "Alternative policy count must be positive and within the safety bound");
  }
  CandidatePolicyResult normalized_base_result =
      NormalizeCandidateGenerationPolicy(board, std::move(base_policy));
  if (std::holds_alternative<CandidatePolicyError>(normalized_base_result)) {
    return std::get<CandidatePolicyError>(std::move(normalized_base_result));
  }
  NormalizedCandidateGenerationPolicy normalized_base =
      std::get<NormalizedCandidateGenerationPolicy>(std::move(normalized_base_result));

  if (schedule.candidate_count > 1) {
    if (schedule.alternative_resources.empty()) {
      return Error(CandidatePolicyErrorCode::kInvalidAlternativeSchedule,
                   "More than one alternative policy requires a resource set");
    }
    if (schedule.step_surcharge_increment == 0 || schedule.bend_surcharge_increment == 0 ||
        schedule.resource_penalty_increment == 0) {
      return Error(CandidatePolicyErrorCode::kInvalidAlternativeSchedule,
                   "Alternative policy surcharge and penalty increments must be positive");
    }
  }
  if (schedule.alternative_resources.size() > kMaximumPolicyResourceEntries) {
    return Error(CandidatePolicyErrorCode::kTooManyResources,
                 "Alternative schedule exceeds the bounded resource-entry count");
  }
  std::ranges::sort(schedule.alternative_resources);
  schedule.alternative_resources.erase(std::ranges::unique(schedule.alternative_resources).begin(),
                                       schedule.alternative_resources.end());
  for (const EdgeResourceKey& resource : schedule.alternative_resources) {
    if (!ResourceExists(board, resource)) {
      return Error(CandidatePolicyErrorCode::kInvalidResource,
                   "Alternative schedule names a resource absent from the CompiledBoard");
    }
    if (PolicyBansResource(normalized_base.policy, resource) ||
        PolicyPenaltyForResource(normalized_base.policy, resource) != 0) {
      return Error(CandidatePolicyErrorCode::kConflictingResourceAction,
                   "Alternative schedule resources must be absent from the base policy");
    }
  }
  if (schedule.candidate_count > 1 && schedule.alternative_resources.empty()) {
    return Error(CandidatePolicyErrorCode::kInvalidAlternativeSchedule,
                 "Alternative schedule resources must not normalize to an empty set");
  }
  if (static_cast<std::uint64_t>(schedule.candidate_count - 1) >
      std::numeric_limits<std::uint32_t>::max() - normalized_base.policy.candidate_ordinal) {
    return Error(CandidatePolicyErrorCode::kInvalidAlternativeSchedule,
                 "Alternative candidate ordinals overflow uint32");
  }
  const UWide base_resource_entries = normalized_base.policy.banned_resources.size() +
                                      normalized_base.policy.resource_penalties.size();
  const UWide maximum_generated_resource_entries =
      static_cast<UWide>(schedule.candidate_count) * base_resource_entries +
      (schedule.candidate_count - 1U);
  if (maximum_generated_resource_entries > kMaximumPolicyResourceEntries) {
    return Error(CandidatePolicyErrorCode::kTooManyResources,
                 "Alternative policy batch exceeds its deterministic resource-entry bound");
  }

  std::vector<NormalizedCandidateGenerationPolicy> result;
  result.reserve(schedule.candidate_count);
  std::set<std::uint64_t> identities;
  for (std::uint32_t index = 0; index < schedule.candidate_count; ++index) {
    CandidateGenerationPolicy policy = normalized_base.policy;
    policy.candidate_ordinal += index;
    if (index != 0) {
      const std::uint64_t variation = static_cast<std::uint64_t>(index - 1);
      const std::uint8_t mode = static_cast<std::uint8_t>(variation % 4U);
      const std::uint64_t strength = variation / 4U + 1U;
      const EdgeResourceKey& resource =
          schedule.alternative_resources[(variation / 4U) % schedule.alternative_resources.size()];
      const std::optional<std::uint64_t> step_increment =
          CheckedScale(schedule.step_surcharge_increment, strength);
      const std::optional<std::uint64_t> bend_increment =
          CheckedScale(schedule.bend_surcharge_increment, strength);
      const std::optional<std::uint64_t> penalty_increment =
          CheckedScale(schedule.resource_penalty_increment, strength);
      if (!step_increment.has_value() || !bend_increment.has_value() ||
          !penalty_increment.has_value()) {
        return Error(CandidatePolicyErrorCode::kCostOverflow,
                     "Alternative policy strength overflows uint64");
      }
      switch (mode) {
        case 0:
          policy.objective = CandidateObjective::kLengthBiased;
          if (!CheckedAccumulate(*step_increment, &policy.orthogonal_step_surcharge) ||
              !CheckedAccumulate(*step_increment, &policy.diagonal_step_surcharge)) {
            return Error(CandidatePolicyErrorCode::kCostOverflow,
                         "Alternative objective surcharge overflows uint64");
          }
          break;
        case 1:
          policy.objective = CandidateObjective::kBendBiased;
          if (!CheckedAccumulate(*step_increment, &policy.orthogonal_step_surcharge) ||
              !CheckedAccumulate(*step_increment, &policy.diagonal_step_surcharge) ||
              !CheckedAccumulate(*bend_increment, &policy.bend_surcharge)) {
            return Error(CandidatePolicyErrorCode::kCostOverflow,
                         "Alternative scalar surcharge overflows uint64");
          }
          break;
        case 2:
          policy.objective = CandidateObjective::kResourceDiverse;
          policy.resource_penalties.push_back(ResourcePenalty{
              .resource = resource,
              .additional_cost = *penalty_increment,
          });
          break;
        case 3:
          policy.objective = CandidateObjective::kResourceDiverse;
          policy.banned_resources.push_back(resource);
          break;
        default:
          return Error(CandidatePolicyErrorCode::kInvalidAlternativeSchedule,
                       "Alternative policy mode is outside the version-1 cycle");
      }
    }

    CandidatePolicyResult normalized_result =
        NormalizeCandidateGenerationPolicy(board, std::move(policy));
    if (std::holds_alternative<CandidatePolicyError>(normalized_result)) {
      return std::get<CandidatePolicyError>(std::move(normalized_result));
    }
    NormalizedCandidateGenerationPolicy normalized =
        std::get<NormalizedCandidateGenerationPolicy>(std::move(normalized_result));
    if (!identities.insert(normalized.identity).second) {
      return Error(CandidatePolicyErrorCode::kInvalidAlternativeSchedule,
                   "Alternative policies produced a policy-identity collision");
    }
    result.push_back(std::move(normalized));
  }
  return result;
}

bool PolicyBansResource(const CandidateGenerationPolicy& normalized_policy,
                        const EdgeResourceKey& resource) noexcept {
  return std::ranges::binary_search(normalized_policy.banned_resources, resource);
}

std::uint64_t PolicyPenaltyForResource(const CandidateGenerationPolicy& normalized_policy,
                                       const EdgeResourceKey& resource) noexcept {
  const auto found = std::ranges::lower_bound(normalized_policy.resource_penalties, resource, {},
                                              &ResourcePenalty::resource);
  return found != normalized_policy.resource_penalties.end() && found->resource == resource
             ? found->additional_cost
             : 0;
}

std::optional<std::uint64_t> StepCostUnderPolicy(const geometry_compiler::CompilerProfile& profile,
                                                 geometry_compiler::Direction direction,
                                                 std::uint8_t incoming_direction,
                                                 const CandidateGenerationPolicy& normalized_policy,
                                                 const EdgeResourceKey& resource) noexcept {
  UWide cost = geometry_compiler::IsDiagonal(direction)
                   ? static_cast<UWide>(profile.costs.diagonal_step) +
                         normalized_policy.diagonal_step_surcharge
                   : static_cast<UWide>(profile.costs.orthogonal_step) +
                         normalized_policy.orthogonal_step_surcharge;
  if (incoming_direction != 8 && incoming_direction != static_cast<std::uint8_t>(direction)) {
    cost += static_cast<UWide>(profile.costs.bend) + normalized_policy.bend_surcharge;
  }
  cost += PolicyPenaltyForResource(normalized_policy, resource);
  if (cost >= std::numeric_limits<std::uint64_t>::max()) {
    return std::nullopt;
  }
  return static_cast<std::uint64_t>(cost);
}

}  // namespace apgar::routing
