#include "apgar/allocator/resource_accounting.h"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <map>
#include <new>
#include <vector>

#include "apgar/routing/planar_route.h"

namespace apgar::allocator {
namespace {

using Wide = __int128_t;
using UWide = __uint128_t;

[[nodiscard]] ResourceAccountingError Error(ResourceAccountingErrorCode code,
                                            std::string_view invariant_id,
                                            std::string_view detail) noexcept {
  return ResourceAccountingError{.code = code, .invariant_id = invariant_id, .detail = detail};
}

[[nodiscard]] bool IsCanonicalDirection(geometry_compiler::Direction direction) noexcept {
  switch (direction) {
    case geometry_compiler::Direction::kEast:
    case geometry_compiler::Direction::kNorthEast:
    case geometry_compiler::Direction::kNorth:
    case geometry_compiler::Direction::kNorthWest:
      return true;
    case geometry_compiler::Direction::kWest:
    case geometry_compiler::Direction::kSouthWest:
    case geometry_compiler::Direction::kSouth:
    case geometry_compiler::Direction::kSouthEast:
      return false;
  }
  return false;
}

[[nodiscard]] bool FitsCoordinate(Wide coordinate) noexcept {
  return coordinate >= std::numeric_limits<std::int64_t>::min() &&
         coordinate <= std::numeric_limits<std::int64_t>::max();
}

[[nodiscard]] bool SpanCoordinatesAreValid(const candidates::PhysicalEdgeSpan& span) noexcept {
  if (!IsCanonicalDirection(span.direction) || span.edge_count == 0 || span.usage_units == 0) {
    return false;
  }
  const geometry_compiler::DirectionDelta storage_delta =
      candidates::ResourceSpanStorageDelta(span.direction);
  const geometry_compiler::DirectionDelta edge_delta = geometry_compiler::DeltaFor(span.direction);
  const Wide last_offset = static_cast<Wide>(span.edge_count) - 1;
  const Wide last_x =
      static_cast<Wide>(span.lattice_x) + static_cast<Wide>(storage_delta.x) * last_offset;
  const Wide last_y =
      static_cast<Wide>(span.lattice_y) + static_cast<Wide>(storage_delta.y) * last_offset;
  const Wide first_endpoint_x = static_cast<Wide>(span.lattice_x) + edge_delta.x;
  const Wide first_endpoint_y = static_cast<Wide>(span.lattice_y) + edge_delta.y;
  const Wide last_endpoint_x = last_x + edge_delta.x;
  const Wide last_endpoint_y = last_y + edge_delta.y;
  return FitsCoordinate(last_x) && FitsCoordinate(last_y) && FitsCoordinate(first_endpoint_x) &&
         FitsCoordinate(first_endpoint_y) && FitsCoordinate(last_endpoint_x) &&
         FitsCoordinate(last_endpoint_y);
}

[[nodiscard]] bool CandidateMatchesModel(const candidates::RouteCandidate& candidate,
                                         const ResourceCapacityModel& capacities) noexcept {
  const candidates::CandidateAssociations& candidate_associations = candidate.data().associations;
  const ResourceLatticeAssociations& model_associations = capacities.associations();
  return candidate_associations.board_content_hash == model_associations.board_content_hash &&
         candidate_associations.compiler_profile_fingerprint ==
             model_associations.compiler_profile_fingerprint &&
         candidate_associations.geometry_compiler_version ==
             model_associations.geometry_compiler_version;
}

[[nodiscard]] bool LimitsAreValid(const ResourceAccountingLimits& limits) noexcept {
  return limits.maximum_candidates > 0 &&
         limits.maximum_candidates <= kMaximumResourceAccountingCandidates &&
         limits.maximum_expanded_resource_uses > 0 &&
         limits.maximum_expanded_resource_uses <= kMaximumResourceAccountingExpandedUses &&
         limits.maximum_usage_units_per_resource > 0;
}

}  // namespace

ResourceCapacityModelResult BuildResourceCapacityModel(
    const board_ir::BoardSnapshot& board, const geometry_compiler::CompiledBoard& compiled_board,
    std::uint32_t capacity_units) {
  if (capacity_units > 1) {
    return Error(ResourceAccountingErrorCode::kInvalidCapacity,
                 "allocator.capacity.binary_units.v1",
                 "Atomic physical-edge capacity must be zero or one usage unit");
  }
  if (routing::ValidatePreparedCompiledBoardAssociation(board, compiled_board).has_value()) {
    return Error(ResourceAccountingErrorCode::kAssociationMismatch,
                 "allocator.capacity.compiled_association.v1",
                 "Capacity model source Board IR and CompiledBoard do not match");
  }
  return ResourceCapacityModel(
      ResourceLatticeAssociations{
          .board_content_hash = board.content_hash(),
          .compiler_profile_fingerprint = compiled_board.compiler_profile_fingerprint(),
          .geometry_compiler_version = compiled_board.compiler_version(),
      },
      capacity_units);
}

ResourceAccountingResult AccumulateResourceUsage(
    const ResourceCapacityModel& capacities,
    std::span<const candidates::RouteCandidate* const> submitted_candidates,
    ResourceAccountingLimits limits) {
  if (!LimitsAreValid(limits)) {
    return Error(ResourceAccountingErrorCode::kInvalidLimits, "allocator.accounting.limits.v1",
                 "Resource-accounting limits must be positive and within hard bounds");
  }
  if (submitted_candidates.size() > limits.maximum_candidates) {
    return Error(ResourceAccountingErrorCode::kInputBoundExceeded,
                 "allocator.accounting.candidate_count.v1",
                 "Explicit candidate set exceeds the configured candidate bound");
  }

  try {
    std::vector<const candidates::RouteCandidate*> ordered_candidates;
    ordered_candidates.reserve(submitted_candidates.size());
    for (const candidates::RouteCandidate* candidate : submitted_candidates) {
      if (candidate == nullptr) {
        return Error(ResourceAccountingErrorCode::kNullCandidate,
                     "allocator.accounting.null_candidate.v1",
                     "Explicit candidate set contains a null candidate");
      }
      ordered_candidates.push_back(candidate);
    }
    std::ranges::sort(ordered_candidates, [](const candidates::RouteCandidate* left,
                                             const candidates::RouteCandidate* right) {
      return left->id() < right->id();
    });

    UWide expanded_resource_uses = 0;
    const candidates::CandidateId* previous_id = nullptr;
    for (const candidates::RouteCandidate* candidate : ordered_candidates) {
      if (previous_id != nullptr && *previous_id == candidate->id()) {
        return Error(ResourceAccountingErrorCode::kDuplicateCandidate,
                     "allocator.accounting.duplicate_candidate.v1",
                     "Explicit candidate set contains the same immutable candidate twice");
      }
      previous_id = &candidate->id();
      if (!CandidateMatchesModel(*candidate, capacities)) {
        return Error(ResourceAccountingErrorCode::kCandidateAssociationMismatch,
                     "allocator.accounting.candidate_association.v1",
                     "Candidate resource lattice does not match the capacity model");
      }
      if (candidate->data().resource_schema_version !=
              candidates::kCandidateResourceSchemaVersion ||
          candidate->data().resources.empty()) {
        return Error(ResourceAccountingErrorCode::kCandidateInvariant,
                     "allocator.accounting.candidate_resource_schema.v1",
                     "Candidate does not carry a supported nonempty admitted resource footprint");
      }
      for (const candidates::PhysicalEdgeSpan& span : candidate->data().resources) {
        if (!SpanCoordinatesAreValid(span) || span.usage_units != 1) {
          return Error(ResourceAccountingErrorCode::kCandidateInvariant,
                       "allocator.accounting.candidate_span.v1",
                       "Candidate span is not a canonical RouteCandidate v1 physical-edge span");
        }
        expanded_resource_uses += span.edge_count;
        if (expanded_resource_uses > limits.maximum_expanded_resource_uses) {
          return Error(ResourceAccountingErrorCode::kInputBoundExceeded,
                       "allocator.accounting.expanded_resource_uses.v1",
                       "Candidate footprints exceed the configured atomic-resource bound");
        }
      }
    }

    std::map<routing::EdgeResourceKey, std::uint64_t> usage_by_resource;
    for (const candidates::RouteCandidate* candidate : ordered_candidates) {
      for (const candidates::PhysicalEdgeSpan& span : candidate->data().resources) {
        const geometry_compiler::DirectionDelta delta =
            candidates::ResourceSpanStorageDelta(span.direction);
        for (std::uint32_t edge_offset = 0; edge_offset < span.edge_count; ++edge_offset) {
          const Wide resource_x =
              static_cast<Wide>(span.lattice_x) + static_cast<Wide>(delta.x) * edge_offset;
          const Wide resource_y =
              static_cast<Wide>(span.lattice_y) + static_cast<Wide>(delta.y) * edge_offset;
          const routing::EdgeResourceKey resource{
              .layer = span.layer,
              .lattice_x = static_cast<std::int64_t>(resource_x),
              .lattice_y = static_cast<std::int64_t>(resource_y),
              .direction = span.direction,
          };
          std::uint64_t& usage = usage_by_resource[resource];
          if (usage > limits.maximum_usage_units_per_resource - span.usage_units) {
            return Error(ResourceAccountingErrorCode::kUsageOverflow,
                         "allocator.accounting.resource_usage_overflow.v1",
                         "Atomic resource usage exceeds the configured arithmetic domain");
          }
          usage += span.usage_units;
        }
      }
    }

    ResourceAccounting accounting{
        .associations = capacities.associations(),
        .resources = {},
        .candidate_count = static_cast<std::uint64_t>(ordered_candidates.size()),
        .expanded_resource_uses = static_cast<std::uint64_t>(expanded_resource_uses),
        .overused_resource_count = 0,
        .total_overuse_units = 0,
    };
    accounting.resources.reserve(usage_by_resource.size());
    for (const auto& [resource, usage_units] : usage_by_resource) {
      const std::uint64_t overuse_units =
          usage_units > capacities.capacity_units() ? usage_units - capacities.capacity_units() : 0;
      if (overuse_units > 0) {
        if (accounting.total_overuse_units >
            std::numeric_limits<std::uint64_t>::max() - overuse_units) {
          return Error(ResourceAccountingErrorCode::kUsageOverflow,
                       "allocator.accounting.total_overuse_overflow.v1",
                       "Total resource overuse exceeds uint64");
        }
        accounting.total_overuse_units += overuse_units;
        ++accounting.overused_resource_count;
      }
      accounting.resources.push_back(ResourceUsage{
          .resource = resource,
          .capacity_units = capacities.capacity_units(),
          .usage_units = usage_units,
          .overuse_units = overuse_units,
      });
    }
    return accounting;
  } catch (const std::bad_alloc&) {
    return Error(ResourceAccountingErrorCode::kResourceExhausted,
                 "allocator.accounting.allocation.v1",
                 "Resource-accounting scratch allocation failed");
  }
}

}  // namespace apgar::allocator
