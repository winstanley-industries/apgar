#include "apgar/allocator/one_world.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <new>
#include <optional>
#include <ranges>
#include <set>
#include <stdexcept>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "apgar/board_ir/stable_hash.h"
#include "apgar/candidates/route_candidate.h"
#include "apgar/geometry_compiler/compiled_board.h"
#include "src/allocator/one_world_internal.h"

namespace apgar::allocator {
namespace {

using UWide = __uint128_t;
using Wide = __int128_t;

[[nodiscard]] constexpr AllocationError Error(AllocationErrorCode code,
                                              std::string_view invariant_id,
                                              std::string_view detail) noexcept {
  return AllocationError{.code = code, .invariant_id = invariant_id, .detail = detail};
}

[[nodiscard]] auto NetKey(board_ir::EntityRef net) noexcept {
  return std::tuple{net.id, net.generation};
}

[[nodiscard]] bool IsCanonicalResource(const routing::EdgeResourceKey& resource) noexcept {
  if (static_cast<std::uint8_t>(resource.direction) >
      static_cast<std::uint8_t>(geometry_compiler::Direction::kNorthWest)) {
    return false;
  }
  const geometry_compiler::DirectionDelta delta = geometry_compiler::DeltaFor(resource.direction);
  const Wide endpoint_x = static_cast<Wide>(resource.lattice_x) + static_cast<Wide>(delta.x);
  const Wide endpoint_y = static_cast<Wide>(resource.lattice_y) + static_cast<Wide>(delta.y);
  return endpoint_x >= std::numeric_limits<std::int64_t>::min() &&
         endpoint_x <= std::numeric_limits<std::int64_t>::max() &&
         endpoint_y >= std::numeric_limits<std::int64_t>::min() &&
         endpoint_y <= std::numeric_limits<std::int64_t>::max();
}

[[nodiscard]] bool SharedAssociationsMatch(
    const AllocationAssociations& expected,
    const candidates::CandidateAssociations& actual) noexcept {
  return expected.board_content_hash == actual.board_content_hash &&
         expected.compiler_profile_fingerprint == actual.compiler_profile_fingerprint &&
         expected.geometry_compiler_version == actual.geometry_compiler_version;
}

[[nodiscard]] std::optional<AllocationError> ValidateResourceSpanEnvelope(
    const candidates::PhysicalEdgeSpan& span) noexcept {
  const routing::EdgeResourceKey first{.layer = span.layer,
                                       .lattice_x = span.lattice_x,
                                       .lattice_y = span.lattice_y,
                                       .direction = span.direction};
  if (!IsCanonicalResource(first) || span.edge_count == 0 || span.usage_units == 0) {
    return Error(AllocationErrorCode::kCandidateInvariant, "allocator.candidate.resource_span.v1",
                 "An immutable candidate contains an invalid physical-edge resource span");
  }
  const geometry_compiler::DirectionDelta storage_delta =
      candidates::ResourceSpanStorageDelta(span.direction);
  const Wide final_x = static_cast<Wide>(span.lattice_x) +
                       static_cast<Wide>(storage_delta.x) * (span.edge_count - 1U);
  const Wide final_y = static_cast<Wide>(span.lattice_y) +
                       static_cast<Wide>(storage_delta.y) * (span.edge_count - 1U);
  if (final_x < std::numeric_limits<std::int64_t>::min() ||
      final_x > std::numeric_limits<std::int64_t>::max() ||
      final_y < std::numeric_limits<std::int64_t>::min() ||
      final_y > std::numeric_limits<std::int64_t>::max()) {
    return Error(AllocationErrorCode::kCandidateInvariant,
                 "allocator.candidate.resource_coordinate_overflow.v1",
                 "An immutable candidate resource span exceeds signed lattice coordinates");
  }
  const routing::EdgeResourceKey last{.layer = span.layer,
                                      .lattice_x = static_cast<std::int64_t>(final_x),
                                      .lattice_y = static_cast<std::int64_t>(final_y),
                                      .direction = span.direction};
  if (!IsCanonicalResource(last)) {
    return Error(AllocationErrorCode::kCandidateInvariant,
                 "allocator.candidate.resource_coordinate_overflow.v1",
                 "An immutable candidate resource span endpoint exceeds signed coordinates");
  }
  return std::nullopt;
}

struct CanonicalResourceInputs {
  std::map<routing::EdgeResourceKey, std::uint32_t> capacities;
  std::map<routing::EdgeResourceKey, std::uint64_t> prices;
};

using CanonicalResourceInputsResult = std::variant<CanonicalResourceInputs, AllocationError>;

[[nodiscard]] CanonicalResourceInputsResult CanonicalizeResourceInputs(
    const OneWorldAllocationRequest& request) {
  const UWide record_count = static_cast<UWide>(request.capacities.overrides().size()) +
                             static_cast<UWide>(request.prices.prices().size());
  if (record_count > request.limits.maximum_resource_records) {
    return Error(AllocationErrorCode::kInputBoundExceeded,
                 "allocator.input.resource_record_budget.v1",
                 "Capacity and price records exceed the configured one-world bound");
  }

  std::vector<ResourceCapacityOverride> capacities = request.capacities.overrides();
  std::ranges::sort(
      capacities, [](const ResourceCapacityOverride& left, const ResourceCapacityOverride& right) {
        return std::tie(left.resource, left.capacity_units) <
               std::tie(right.resource, right.capacity_units);
      });
  for (std::size_t index = 0; index < capacities.size(); ++index) {
    if (!IsCanonicalResource(capacities[index].resource)) {
      return Error(AllocationErrorCode::kInvalidResource, "allocator.capacity.resource_key.v1",
                   "A capacity override does not name a canonical physical edge");
    }
    if (index != 0 && capacities[index - 1].resource == capacities[index].resource) {
      return Error(AllocationErrorCode::kDuplicateResource,
                   "allocator.capacity.duplicate_resource.v1",
                   "A physical resource has more than one capacity override");
    }
    if (capacities[index].capacity_units > 1U) {
      return Error(AllocationErrorCode::kInvalidConfiguration,
                   "allocator.capacity.binary_physical_edge.v1",
                   "Physical-edge capacity v1 must be zero or one routing unit");
    }
  }

  std::vector<ResourcePrice> prices = request.prices.prices();
  std::ranges::sort(prices, [](const ResourcePrice& left, const ResourcePrice& right) {
    return std::tie(left.resource, left.price_per_usage_unit) <
           std::tie(right.resource, right.price_per_usage_unit);
  });
  for (std::size_t index = 0; index < prices.size(); ++index) {
    if (!IsCanonicalResource(prices[index].resource)) {
      return Error(AllocationErrorCode::kInvalidResource, "allocator.price.resource_key.v1",
                   "A price record does not name a canonical physical edge");
    }
    if (index != 0 && prices[index - 1].resource == prices[index].resource) {
      return Error(AllocationErrorCode::kDuplicateResource, "allocator.price.duplicate_resource.v1",
                   "A physical resource has more than one price in a snapshot");
    }
  }

  CanonicalResourceInputs result;
  for (const ResourceCapacityOverride& capacity : capacities) {
    result.capacities.emplace(capacity.resource, capacity.capacity_units);
  }
  for (const ResourcePrice& price : prices) {
    result.prices.emplace(price.resource, price.price_per_usage_unit);
  }
  return result;
}

struct CanonicalPool {
  board_ir::EntityRef net{};
  std::vector<const candidates::StoredCandidate*> candidates;
};

struct CanonicalPools {
  std::vector<CanonicalPool> pools;
};

using CanonicalPoolsResult = std::variant<CanonicalPools, AllocationError>;

[[nodiscard]] CanonicalPoolsResult CanonicalizeAndValidatePools(
    const OneWorldAllocationRequest& request) {
  if (request.pools.size() > request.limits.maximum_nets) {
    return Error(AllocationErrorCode::kInputBoundExceeded, "allocator.input.net_budget.v1",
                 "Candidate-pool count exceeds the configured one-world bound");
  }

  std::vector<const CandidatePool*> ordered_pools;
  ordered_pools.reserve(request.pools.size());
  for (const CandidatePool& pool : request.pools) {
    ordered_pools.push_back(&pool);
  }
  std::ranges::sort(ordered_pools, [](const CandidatePool* left, const CandidatePool* right) {
    return NetKey(left->net) < NetKey(right->net);
  });
  for (std::size_t index = 1; index < ordered_pools.size(); ++index) {
    if (ordered_pools[index - 1]->net == ordered_pools[index]->net) {
      return Error(AllocationErrorCode::kDuplicateNet, "allocator.pool.duplicate_net.v1",
                   "A net has more than one candidate pool in one world");
    }
  }
  if (request.workload != nullptr) {
    if (ordered_pools.size() != request.workload->nets().size()) {
      return Error(AllocationErrorCode::kCandidateNetMismatch, "allocator.pool.workload_roster.v1",
                   "One pool, including explicit empty pools, is required for every workload net");
    }
    for (std::size_t index = 0; index < ordered_pools.size(); ++index) {
      if (ordered_pools[index]->net != request.workload->nets()[index].request.net) {
        return Error(AllocationErrorCode::kCandidateNetMismatch,
                     "allocator.pool.workload_roster.v1",
                     "Candidate-pool net roster does not match the canonical workload");
      }
    }
  }

  CanonicalPools result;
  result.pools.reserve(ordered_pools.size());
  UWide candidate_count = 0;
  UWide expanded_resource_uses = 0;
  std::set<candidates::CandidateId> candidate_ids;
  for (const CandidatePool* pool : ordered_pools) {
    candidate_count += pool->candidates.size();
    if (candidate_count > request.limits.maximum_candidates) {
      return Error(AllocationErrorCode::kInputBoundExceeded, "allocator.input.candidate_budget.v1",
                   "Candidate count exceeds the configured one-world bound");
    }

    CanonicalPool canonical_pool{.net = pool->net, .candidates = {}};
    canonical_pool.candidates.reserve(pool->candidates.size());
    for (const candidates::StoredCandidate& stored : pool->candidates) {
      if (stored == nullptr) {
        return Error(AllocationErrorCode::kNullCandidate, "allocator.pool.null_candidate.v1",
                     "A candidate pool contains a null immutable-candidate handle");
      }
      canonical_pool.candidates.push_back(&stored);
    }
    std::ranges::sort(canonical_pool.candidates, [](const candidates::StoredCandidate* left,
                                                    const candidates::StoredCandidate* right) {
      return (*left)->id() < (*right)->id();
    });

    std::optional<std::pair<std::uint64_t, std::uint64_t>> pool_profile;
    const PreparedNetRoutingContext* workload_context =
        request.workload == nullptr ? nullptr : request.workload->FindNet(canonical_pool.net);
    for (const candidates::StoredCandidate* stored_handle : canonical_pool.candidates) {
      const candidates::StoredCandidate& stored = *stored_handle;
      const candidates::RouteCandidate& candidate = *stored;
      const candidates::GeneratedRouteCandidate& data = candidate.data();
      if (candidate.net() != canonical_pool.net) {
        return Error(AllocationErrorCode::kCandidateNetMismatch, "allocator.pool.candidate_net.v1",
                     "An immutable candidate is stored under a different net");
      }
      if (!SharedAssociationsMatch(request.associations, data.associations)) {
        return Error(AllocationErrorCode::kCandidateAssociationMismatch,
                     "allocator.pool.candidate_association.v1",
                     "An immutable candidate belongs to a different board/compiler context");
      }
      const std::pair profile{data.associations.routing_profile_fingerprint,
                              data.associations.rule_bucket_identity};
      if (workload_context != nullptr &&
          (profile.first != workload_context->routing_profile_fingerprint ||
           profile.second != workload_context->compiled_board.rule_bucket().identity)) {
        return Error(AllocationErrorCode::kCandidateAssociationMismatch,
                     "allocator.pool.workload_context.v1",
                     "Candidate profile/rule association does not match its workload net");
      }
      if (workload_context != nullptr &&
          !internal::CandidateMatchesWorkloadRequestV1(candidate, *workload_context)) {
        return Error(AllocationErrorCode::kCandidateAssociationMismatch,
                     "allocator.pool.workload_request.v1",
                     "Candidate endpoint coordinates or layers do not match its workload net");
      }
      if (!pool_profile.has_value()) {
        pool_profile = profile;
      } else if (*pool_profile != profile) {
        return Error(AllocationErrorCode::kCandidateAssociationMismatch,
                     "allocator.pool.mixed_profile.v1",
                     "One net pool mixes routing-profile or rule-bucket associations");
      }
      if (candidate.id().empty() || data.schema_major != candidates::kRouteCandidateSchemaMajor ||
          data.schema_minor != candidates::kRouteCandidateSchemaMinor ||
          data.geometry_schema_version != candidates::kCandidateGeometrySchemaVersion ||
          data.resource_schema_version != candidates::kCandidateResourceSchemaVersion ||
          data.associations.routing_profile_fingerprint == 0 ||
          data.associations.rule_bucket_identity == 0 ||
          !data.constraints.supported_hard_constraints_satisfied ||
          data.constraints.connected_intended_terminal_count != 2 ||
          data.constraints.exact_validation_code !=
              candidates::CandidateExactValidationCode::kPassed) {
        return Error(AllocationErrorCode::kCandidateInvariant,
                     "allocator.pool.candidate_contract.v1",
                     "A selected-pool item is not an exact-admitted RouteCandidate v1");
      }
      if (!candidate_ids.emplace(candidate.id()).second) {
        return Error(AllocationErrorCode::kDuplicateCandidate,
                     "allocator.pool.duplicate_candidate.v1",
                     "A candidate identity occurs more than once in one allocation request");
      }
      for (const candidates::PhysicalEdgeSpan& span : data.resources) {
        if (std::optional<AllocationError> failure = ValidateResourceSpanEnvelope(span);
            failure.has_value()) {
          return *failure;
        }
        expanded_resource_uses += span.edge_count;
        if (expanded_resource_uses > request.limits.maximum_expanded_resource_uses) {
          return Error(AllocationErrorCode::kInputBoundExceeded,
                       "allocator.input.expanded_resource_budget.v1",
                       "Candidate footprints exceed the configured expanded-resource bound");
        }
      }
    }
    result.pools.push_back(std::move(canonical_pool));
  }
  return result;
}

struct CandidateScore {
  std::uint64_t intrinsic_cost = 0;
  std::uint64_t price_cost = 0;
  std::uint64_t score = 0;
};

struct SpanLineKey {
  std::uint32_t layer = 0;
  geometry_compiler::Direction direction = geometry_compiler::Direction::kEast;
  Wide invariant = 0;
};

[[nodiscard]] bool operator==(const SpanLineKey& left, const SpanLineKey& right) noexcept {
  return std::tie(left.layer, left.direction, left.invariant) ==
         std::tie(right.layer, right.direction, right.invariant);
}

[[nodiscard]] bool operator<(const SpanLineKey& left, const SpanLineKey& right) noexcept {
  return std::tie(left.layer, left.direction, left.invariant) <
         std::tie(right.layer, right.direction, right.invariant);
}

struct SpanBoundaryEvent {
  SpanLineKey line;
  Wide coordinate = 0;
  Wide usage_delta = 0;
};

[[nodiscard]] SpanLineKey LineFor(const candidates::PhysicalEdgeSpan& span) noexcept {
  switch (span.direction) {
    case geometry_compiler::Direction::kEast:
      return SpanLineKey{
          .layer = span.layer, .direction = span.direction, .invariant = span.lattice_y};
    case geometry_compiler::Direction::kNorthEast:
      return SpanLineKey{.layer = span.layer,
                         .direction = span.direction,
                         .invariant = static_cast<Wide>(span.lattice_y) - span.lattice_x};
    case geometry_compiler::Direction::kNorth:
      return SpanLineKey{
          .layer = span.layer, .direction = span.direction, .invariant = span.lattice_x};
    case geometry_compiler::Direction::kNorthWest:
      return SpanLineKey{.layer = span.layer,
                         .direction = span.direction,
                         .invariant = static_cast<Wide>(span.lattice_x) + span.lattice_y};
    default:
      return {};
  }
  return {};
}

[[nodiscard]] SpanLineKey LineFor(const routing::EdgeResourceKey& resource) noexcept {
  return LineFor(candidates::PhysicalEdgeSpan{.layer = resource.layer,
                                              .lattice_x = resource.lattice_x,
                                              .lattice_y = resource.lattice_y,
                                              .direction = resource.direction,
                                              .edge_count = 1,
                                              .usage_units = 1});
}

[[nodiscard]] Wide SpanCoordinate(const candidates::PhysicalEdgeSpan& span) noexcept {
  return span.direction == geometry_compiler::Direction::kNorth ? span.lattice_y : span.lattice_x;
}

[[nodiscard]] Wide ResourceCoordinate(const routing::EdgeResourceKey& resource) noexcept {
  return resource.direction == geometry_compiler::Direction::kNorth ? resource.lattice_y
                                                                    : resource.lattice_x;
}

struct LinePrice {
  SpanLineKey line;
  Wide coordinate = 0;
  std::uint64_t price_per_usage_unit = 0;
};

[[nodiscard]] bool LinePriceBefore(const LinePrice& left, const LinePrice& right) noexcept {
  return std::tie(left.line, left.coordinate, left.price_per_usage_unit) <
         std::tie(right.line, right.coordinate, right.price_per_usage_unit);
}

[[nodiscard]] std::vector<LinePrice> BuildNonzeroLinePrices(
    const std::map<routing::EdgeResourceKey, std::uint64_t>& prices) {
  std::vector<LinePrice> result;
  result.reserve(prices.size());
  for (const auto& [resource, price] : prices) {
    if (price != 0) {
      result.push_back(LinePrice{.line = LineFor(resource),
                                 .coordinate = ResourceCoordinate(resource),
                                 .price_per_usage_unit = price});
    }
  }
  std::ranges::sort(result, LinePriceBefore);
  return result;
}

[[nodiscard]] routing::EdgeResourceKey ResourceAt(const SpanLineKey& line,
                                                  Wide coordinate) noexcept {
  Wide x = 0;
  Wide y = 0;
  switch (line.direction) {
    case geometry_compiler::Direction::kEast:
      x = coordinate;
      y = line.invariant;
      break;
    case geometry_compiler::Direction::kNorthEast:
      x = coordinate;
      y = coordinate + line.invariant;
      break;
    case geometry_compiler::Direction::kNorth:
      x = line.invariant;
      y = coordinate;
      break;
    case geometry_compiler::Direction::kNorthWest:
      x = coordinate;
      y = line.invariant - coordinate;
      break;
    default:
      break;
  }
  return routing::EdgeResourceKey{.layer = line.layer,
                                  .lattice_x = static_cast<std::int64_t>(x),
                                  .lattice_y = static_cast<std::int64_t>(y),
                                  .direction = line.direction};
}

[[nodiscard]] internal::SelectedResourceAccumulationResult AccumulateResourceSpanStreamsImpl(
    std::span<const internal::ResourceSpanStream> streams,
    std::uint64_t maximum_expanded_resource_uses) {
  std::vector<SpanBoundaryEvent> events;
  UWide expanded_resource_uses = 0;
  UWide boundary_event_count = 0;
  for (const internal::ResourceSpanStream stream : streams) {
    for (const candidates::PhysicalEdgeSpan& span : stream) {
      if (std::optional<AllocationError> failure = ValidateResourceSpanEnvelope(span);
          failure.has_value()) {
        return *failure;
      }
      expanded_resource_uses += span.edge_count;
      boundary_event_count += 2U;
      if (expanded_resource_uses > maximum_expanded_resource_uses ||
          boundary_event_count > std::numeric_limits<std::uint64_t>::max()) {
        return Error(AllocationErrorCode::kInputBoundExceeded,
                     "allocator.accounting.expanded_resource_budget.v1",
                     "Selected resources exceed the configured accounting bound");
      }
      const Wide begin = SpanCoordinate(span);
      const Wide end = begin + span.edge_count;
      const SpanLineKey line = LineFor(span);
      events.push_back(
          SpanBoundaryEvent{.line = line, .coordinate = begin, .usage_delta = span.usage_units});
      events.push_back(SpanBoundaryEvent{
          .line = line, .coordinate = end, .usage_delta = -static_cast<Wide>(span.usage_units)});
    }
  }

  std::ranges::sort(events, [](const SpanBoundaryEvent& left, const SpanBoundaryEvent& right) {
    return std::tie(left.line, left.coordinate, left.usage_delta) <
           std::tie(right.line, right.coordinate, right.usage_delta);
  });
  internal::SelectedResourceAccumulation result{
      .resources = {},
      .logical_resource_uses = static_cast<std::uint64_t>(expanded_resource_uses),
      .materialized_resource_edges = 0,
      .span_boundary_events = static_cast<std::uint64_t>(boundary_event_count),
  };
  std::size_t event_index = 0;
  while (event_index < events.size()) {
    const SpanLineKey line = events[event_index].line;
    Wide active_usage = 0;
    while (event_index < events.size() && events[event_index].line == line) {
      const Wide coordinate = events[event_index].coordinate;
      do {
        active_usage += events[event_index].usage_delta;
        ++event_index;
      } while (event_index < events.size() && events[event_index].line == line &&
               events[event_index].coordinate == coordinate);
      const Wide next_coordinate = event_index < events.size() && events[event_index].line == line
                                       ? events[event_index].coordinate
                                       : coordinate;
      if (active_usage < 0 || active_usage > std::numeric_limits<std::uint64_t>::max()) {
        return Error(AllocationErrorCode::kUsageOverflow, "allocator.usage.resource_overflow.v1",
                     "Selected resource usage exceeds uint64");
      }
      if (active_usage == 0) {
        continue;
      }
      for (Wide edge = coordinate; edge < next_coordinate; ++edge) {
        result.resources.push_back(internal::AccumulatedResourceUse{
            .resource = ResourceAt(line, edge),
            .usage_units = static_cast<std::uint64_t>(active_usage),
        });
      }
    }
    if (active_usage != 0) {
      return Error(AllocationErrorCode::kCandidateInvariant,
                   "allocator.accounting.unbalanced_span_events.v1",
                   "Selected candidate spans produced unbalanced accounting events");
    }
  }
  std::ranges::sort(result.resources, {}, &internal::AccumulatedResourceUse::resource);
  result.materialized_resource_edges = result.resources.size();
  return result;
}

[[nodiscard]] internal::SelectedResourceAccumulationResult AccumulateSelectedResourcesImpl(
    std::span<const candidates::StoredCandidate> selected_candidates,
    std::uint64_t maximum_expanded_resource_uses) {
  std::vector<internal::ResourceSpanStream> streams;
  streams.reserve(selected_candidates.size());
  for (const candidates::StoredCandidate& candidate : selected_candidates) {
    if (candidate == nullptr) {
      return Error(AllocationErrorCode::kNullCandidate, "allocator.accounting.null_candidate.v1",
                   "Selected-resource accounting received a null candidate handle");
    }
    streams.emplace_back(candidate->data().resources);
  }
  return AccumulateResourceSpanStreamsImpl(streams, maximum_expanded_resource_uses);
}

using CandidateScoreResult = std::variant<CandidateScore, AllocationError>;

[[nodiscard]] CandidateScoreResult ScoreCandidate(const candidates::RouteCandidate& candidate,
                                                  std::uint64_t intrinsic_cost_weight,
                                                  std::span<const LinePrice> nonzero_prices,
                                                  std::uint64_t* scoring_span_queries,
                                                  std::uint64_t* scoring_price_matches) {
  const std::uint64_t intrinsic_cost = candidate.data().metrics.intrinsic_base_cost;
  UWide price_cost = 0;
  if (!nonzero_prices.empty()) {
    for (const candidates::PhysicalEdgeSpan& span : candidate.data().resources) {
      ++*scoring_span_queries;
      const SpanLineKey line = LineFor(span);
      const Wide begin = SpanCoordinate(span);
      const Wide end = begin + span.edge_count;
      const LinePrice lower{.line = line, .coordinate = begin, .price_per_usage_unit = 0};
      auto price = std::ranges::lower_bound(nonzero_prices, lower, LinePriceBefore);
      while (price != nonzero_prices.end() && price->line == line && price->coordinate < end) {
        ++*scoring_price_matches;
        price_cost += static_cast<UWide>(price->price_per_usage_unit) * span.usage_units;
        if (price_cost > std::numeric_limits<std::uint64_t>::max()) {
          return Error(AllocationErrorCode::kCostOverflow,
                       "allocator.score.resource_price_overflow.v1",
                       "Candidate resource-price cost exceeds uint64");
        }
        ++price;
      }
    }
  }
  const UWide score = static_cast<UWide>(intrinsic_cost) * intrinsic_cost_weight + price_cost;
  if (score > std::numeric_limits<std::uint64_t>::max()) {
    return Error(AllocationErrorCode::kCostOverflow, "allocator.score.total_overflow.v1",
                 "Candidate intrinsic and resource-price score exceeds uint64");
  }
  return CandidateScore{
      .intrinsic_cost = intrinsic_cost,
      .price_cost = static_cast<std::uint64_t>(price_cost),
      .score = static_cast<std::uint64_t>(score),
  };
}

void HashResource(board_ir::StableHashBuilder& hash,
                  const routing::EdgeResourceKey& resource) noexcept {
  hash.AddU32(resource.layer);
  hash.AddI64(resource.lattice_x);
  hash.AddI64(resource.lattice_y);
  hash.AddByte(static_cast<std::uint8_t>(resource.direction));
}

[[nodiscard]] std::uint64_t ComputeWorldChecksumImpl(const OneWorldAllocation& world,
                                                     bool include_workload) noexcept {
  board_ir::StableHashBuilder hash;
  hash.AddString(include_workload ? "APGAR-ONE-WORLD-V2" : "APGAR-ONE-WORLD-V1");
  hash.AddU32(world.schema_version);
  hash.AddU64(world.associations.board_content_hash);
  hash.AddU64(world.associations.compiler_profile_fingerprint);
  hash.AddU32(world.associations.geometry_compiler_version);
  if (include_workload) {
    hash.AddU64(world.workload_checksum);
  }
  hash.AddU32(world.price_iteration);
  hash.AddU32(world.default_capacity_units);
  hash.AddU64(world.intrinsic_cost_weight);
  hash.AddU64(static_cast<std::uint64_t>(world.selections.size()));
  for (const NetSelection& selection : world.selections) {
    hash.AddU64(selection.net.id);
    hash.AddU32(selection.net.generation);
    hash.AddByte(static_cast<std::uint8_t>(selection.status));
    hash.AddBool(selection.candidate_id.has_value());
    if (selection.candidate_id.has_value()) {
      hash.AddU64(selection.candidate_id->high);
      hash.AddU64(selection.candidate_id->low);
    }
    hash.AddBool(selection.candidate_payload_checksum.has_value());
    if (selection.candidate_payload_checksum.has_value()) {
      hash.AddU64(*selection.candidate_payload_checksum);
    }
    hash.AddU64(selection.intrinsic_cost);
    hash.AddU64(selection.price_cost);
    hash.AddU64(selection.selection_score);
  }
  hash.AddU64(static_cast<std::uint64_t>(world.resources.size()));
  for (const ResourceUsage& usage : world.resources) {
    HashResource(hash, usage.resource);
    hash.AddBool(usage.has_capacity_override);
    hash.AddU32(usage.capacity_units);
    hash.AddU64(usage.usage_units);
    hash.AddU64(usage.overuse_units);
    hash.AddBool(usage.has_explicit_price);
    hash.AddU64(usage.price_per_usage_unit);
  }
  hash.AddU64(world.selected_net_count);
  hash.AddU64(world.no_candidate_net_count);
  hash.AddU64(world.overused_resource_count);
  hash.AddU64(world.total_overuse_units);
  hash.AddU64(world.total_intrinsic_cost);
  hash.AddU64(world.total_selection_score);
  hash.AddU64(world.scoring_span_queries);
  hash.AddU64(world.scoring_price_matches);
  hash.AddU64(world.selected_logical_resource_uses);
  hash.AddU64(world.accounting_materialized_resource_edges);
  hash.AddU64(world.accounting_span_boundary_events);
  return hash.Finish();
}

[[nodiscard]] std::uint64_t ComputePoolManifestChecksumV1(const CanonicalPool& pool) noexcept {
  board_ir::StableHashBuilder hash;
  hash.AddString("APGAR-ONE-WORLD-POOL-MANIFEST-V1");
  hash.AddU64(pool.net.id);
  hash.AddU32(pool.net.generation);
  hash.AddU64(static_cast<std::uint64_t>(pool.candidates.size()));
  for (const candidates::StoredCandidate* candidate : pool.candidates) {
    hash.AddU64((*candidate)->id().high);
    hash.AddU64((*candidate)->id().low);
    hash.AddU64((*candidate)->data().payload_checksum);
  }
  return hash.Finish();
}

[[nodiscard]] std::uint64_t ComputePoolsManifestChecksumV1(
    std::span<const internal::OneWorldPoolSelectionEvidence> pools) noexcept {
  board_ir::StableHashBuilder hash;
  hash.AddString("APGAR-ONE-WORLD-POOLS-MANIFEST-V1");
  hash.AddU64(static_cast<std::uint64_t>(pools.size()));
  for (const internal::OneWorldPoolSelectionEvidence& pool : pools) {
    hash.AddU64(pool.net.id);
    hash.AddU32(pool.net.generation);
    hash.AddU64(pool.pool_manifest_checksum);
  }
  return hash.Finish();
}

[[nodiscard]] std::uint64_t ComputeRequestManifestChecksumV1(
    const OneWorldAllocationRequest& request, const CanonicalResourceInputs& resources,
    const internal::OneWorldSelectionEvidence& evidence) noexcept {
  board_ir::StableHashBuilder hash;
  hash.AddString("APGAR-ONE-WORLD-REQUEST-MANIFEST-V1");
  hash.AddU32(request.schema_version);
  hash.AddU64(request.associations.board_content_hash);
  hash.AddU64(request.associations.compiler_profile_fingerprint);
  hash.AddU32(request.associations.geometry_compiler_version);
  hash.AddU64(request.workload == nullptr ? 0 : request.workload->workload_checksum());
  hash.AddU64(request.intrinsic_cost_weight);
  hash.AddU64(request.limits.maximum_nets);
  hash.AddU64(request.limits.maximum_candidates);
  hash.AddU64(request.limits.maximum_resource_records);
  hash.AddU64(request.limits.maximum_expanded_resource_uses);
  hash.AddU32(request.capacities.schema_version());
  hash.AddU32(request.capacities.default_capacity_units());
  hash.AddU64(static_cast<std::uint64_t>(resources.capacities.size()));
  for (const auto& [resource, capacity] : resources.capacities) {
    HashResource(hash, resource);
    hash.AddU32(capacity);
  }
  hash.AddU32(request.prices.schema_version());
  hash.AddU32(request.prices.iteration());
  hash.AddU64(static_cast<std::uint64_t>(resources.prices.size()));
  for (const auto& [resource, price] : resources.prices) {
    HashResource(hash, resource);
    hash.AddU64(price);
  }
  hash.AddU64(evidence.candidate_pool_manifest_checksum);
  hash.AddU64(evidence.source_pool_count);
  hash.AddU64(evidence.source_candidate_count);
  return hash.Finish();
}

struct PreparedOneWorldSelection {
  CanonicalResourceInputs canonical_resources;
  internal::OneWorldSelectionEvidence evidence;
};

using PreparedOneWorldSelectionResult = std::variant<PreparedOneWorldSelection, AllocationError>;

[[nodiscard]] PreparedOneWorldSelectionResult PrepareOneWorldSelection(
    const OneWorldAllocationRequest& request) {
  if ((request.schema_version != kOneWorldAllocationSchemaVersionV1 &&
       request.schema_version != kOneWorldAllocationSchemaVersion) ||
      request.capacities.schema_version() != kResourceCapacityModelSchemaVersion ||
      request.prices.schema_version() != kPriceSnapshotSchemaVersion) {
    return Error(AllocationErrorCode::kUnsupportedSchema, "allocator.schema.v1",
                 "One-world request, capacity model, or price snapshot schema is unsupported");
  }
  if (request.schema_version == kOneWorldAllocationSchemaVersionV1 && request.workload != nullptr) {
    return Error(AllocationErrorCode::kUnsupportedSchema, "allocator.workload.schema.v2",
                 "Canonical workload binding requires One-World schema v2");
  }
  if (!internal::OneWorldAllocatorLimitsAreValidV1(request.limits) ||
      request.intrinsic_cost_weight == 0 || request.associations.board_content_hash == 0 ||
      request.associations.compiler_profile_fingerprint == 0 ||
      request.associations.geometry_compiler_version == 0 ||
      request.capacities.default_capacity_units() > 1U) {
    return Error(
        AllocationErrorCode::kInvalidConfiguration, "allocator.configuration.v1",
        "Limits, binary capacity, positive intrinsic weight, and associations are required");
  }
  if (request.capacities.associations() != request.associations ||
      request.prices.associations() != request.associations) {
    return Error(AllocationErrorCode::kCandidateAssociationMismatch,
                 "allocator.resource_state.association.v1",
                 "Capacity and price state must match the request board/compiler context");
  }
  if (request.workload != nullptr &&
      (request.workload->schema_version() != kMultiNetWorkloadSchemaVersion ||
       request.workload->board_content_hash() != request.associations.board_content_hash ||
       request.workload->compiler_profile_fingerprint() !=
           request.associations.compiler_profile_fingerprint ||
       request.workload->geometry_compiler_version() !=
           request.associations.geometry_compiler_version ||
       request.workload->nets().size() > request.limits.maximum_nets)) {
    return Error(AllocationErrorCode::kCandidateAssociationMismatch,
                 "allocator.workload.association.v1",
                 "Canonical workload does not match the one-world board/compiler context");
  }

  CanonicalResourceInputsResult canonical_resources_result = CanonicalizeResourceInputs(request);
  if (const auto* failure = std::get_if<AllocationError>(&canonical_resources_result);
      failure != nullptr) {
    return *failure;
  }
  CanonicalResourceInputs canonical_resources =
      std::get<CanonicalResourceInputs>(std::move(canonical_resources_result));
  const std::vector<LinePrice> nonzero_prices = BuildNonzeroLinePrices(canonical_resources.prices);

  CanonicalPoolsResult canonical_pools_result = CanonicalizeAndValidatePools(request);
  if (const auto* failure = std::get_if<AllocationError>(&canonical_pools_result);
      failure != nullptr) {
    return *failure;
  }
  CanonicalPools canonical_pools = std::get<CanonicalPools>(std::move(canonical_pools_result));

  OneWorldAllocation world{
      .schema_version = request.schema_version,
      .associations = request.associations,
      .workload_checksum = request.workload == nullptr ? 0 : request.workload->workload_checksum(),
      .price_iteration = request.prices.iteration(),
      .default_capacity_units = request.capacities.default_capacity_units(),
      .intrinsic_cost_weight = request.intrinsic_cost_weight,
      .selections = {},
      .resources = {},
      .selected_net_count = 0,
      .no_candidate_net_count = 0,
      .overused_resource_count = 0,
      .total_overuse_units = 0,
      .total_intrinsic_cost = 0,
      .total_selection_score = 0,
      .scoring_span_queries = 0,
      .scoring_price_matches = 0,
      .selected_logical_resource_uses = 0,
      .accounting_materialized_resource_edges = 0,
      .accounting_span_boundary_events = 0,
      .world_checksum = 0,
  };
  world.selections.reserve(canonical_pools.pools.size());
  internal::OneWorldSelectionEvidence evidence{
      .request_manifest_checksum = 0,
      .candidate_pool_manifest_checksum = 0,
      .source_pool_count = static_cast<std::uint64_t>(canonical_pools.pools.size()),
      .source_candidate_count = 0,
      .selected_expanded_resource_uses = 0,
      .selection_projection = {},
      .pools = {},
  };
  evidence.pools.reserve(canonical_pools.pools.size());
  UWide selected_expanded_resource_uses = 0;

  for (const CanonicalPool& pool : canonical_pools.pools) {
    const std::uint64_t pool_manifest_checksum = ComputePoolManifestChecksumV1(pool);
    evidence.source_candidate_count += static_cast<std::uint64_t>(pool.candidates.size());
    if (pool.candidates.empty()) {
      NetSelection selection{
          .net = pool.net,
          .status = NetSelectionStatus::kNoAdmissibleCandidate,
          .candidate_id = std::nullopt,
          .candidate_payload_checksum = std::nullopt,
          .candidate = nullptr,
          .intrinsic_cost = 0,
          .price_cost = 0,
          .selection_score = 0,
      };
      world.selections.push_back(selection);
      evidence.pools.push_back(internal::OneWorldPoolSelectionEvidence{
          .net = pool.net,
          .candidate_count = 0,
          .pool_manifest_checksum = pool_manifest_checksum,
          .selection = std::move(selection),
      });
      ++world.no_candidate_net_count;
      continue;
    }

    const candidates::StoredCandidate* best = nullptr;
    CandidateScore best_score;
    for (const candidates::StoredCandidate* candidate_handle : pool.candidates) {
      const candidates::StoredCandidate& candidate = *candidate_handle;
      CandidateScoreResult score_result =
          ScoreCandidate(*candidate, request.intrinsic_cost_weight, nonzero_prices,
                         &world.scoring_span_queries, &world.scoring_price_matches);
      if (const auto* failure = std::get_if<AllocationError>(&score_result); failure != nullptr) {
        return *failure;
      }
      const CandidateScore score = std::get<CandidateScore>(score_result);
      if (best == nullptr ||
          std::tie(score.score, candidate->id()) < std::tie(best_score.score, (*best)->id())) {
        best = &candidate;
        best_score = score;
      }
    }

    NetSelection selection{
        .net = pool.net,
        .status = NetSelectionStatus::kSelected,
        .candidate_id = (*best)->id(),
        .candidate_payload_checksum = (*best)->data().payload_checksum,
        .candidate = *best,
        .intrinsic_cost = best_score.intrinsic_cost,
        .price_cost = best_score.price_cost,
        .selection_score = best_score.score,
    };
    const UWide total_intrinsic =
        static_cast<UWide>(world.total_intrinsic_cost) + selection.intrinsic_cost;
    const UWide total_score =
        static_cast<UWide>(world.total_selection_score) + selection.selection_score;
    if (total_intrinsic > std::numeric_limits<std::uint64_t>::max() ||
        total_score > std::numeric_limits<std::uint64_t>::max()) {
      return Error(AllocationErrorCode::kCostOverflow, "allocator.world.cost_overflow.v1",
                   "Selected world cost totals exceed uint64");
    }
    world.total_intrinsic_cost = static_cast<std::uint64_t>(total_intrinsic);
    world.total_selection_score = static_cast<std::uint64_t>(total_score);
    for (const candidates::PhysicalEdgeSpan& span : selection.candidate->data().resources) {
      selected_expanded_resource_uses += span.edge_count;
    }
    world.selections.push_back(selection);
    evidence.pools.push_back(internal::OneWorldPoolSelectionEvidence{
        .net = pool.net,
        .candidate_count = static_cast<std::uint64_t>(pool.candidates.size()),
        .pool_manifest_checksum = pool_manifest_checksum,
        .selection = std::move(selection),
    });
    ++world.selected_net_count;
  }

  if (selected_expanded_resource_uses > std::numeric_limits<std::uint64_t>::max()) {
    return Error(AllocationErrorCode::kInputBoundExceeded,
                 "allocator.selection.selected_expanded_resource_overflow.v1",
                 "Selected compressed footprints exceed unsigned 64-bit accounting");
  }
  evidence.selected_expanded_resource_uses =
      static_cast<std::uint64_t>(selected_expanded_resource_uses);
  evidence.candidate_pool_manifest_checksum = ComputePoolsManifestChecksumV1(evidence.pools);
  evidence.request_manifest_checksum =
      ComputeRequestManifestChecksumV1(request, canonical_resources, evidence);
  evidence.selection_projection = std::move(world);
  return PreparedOneWorldSelection{
      .canonical_resources = std::move(canonical_resources),
      .evidence = std::move(evidence),
  };
}

[[nodiscard]] OneWorldAllocationResult AllocateOneWorldImpl(
    const OneWorldAllocationRequest& request) {
  PreparedOneWorldSelectionResult prepared_result = PrepareOneWorldSelection(request);
  if (const auto* failure = std::get_if<AllocationError>(&prepared_result); failure != nullptr) {
    return *failure;
  }
  PreparedOneWorldSelection prepared =
      std::get<PreparedOneWorldSelection>(std::move(prepared_result));
  CanonicalResourceInputs canonical_resources = std::move(prepared.canonical_resources);
  OneWorldAllocation world = std::move(prepared.evidence.selection_projection);

  // Selected canonical spans are reduced by boundary sweep before atomic
  // output materialization. Shared long corridors are expanded once, not once
  // per selected candidate.
  std::vector<ResourceUsage> contributions;
  contributions.reserve(canonical_resources.capacities.size() + canonical_resources.prices.size() +
                        world.selected_net_count);
  for (const auto& [resource, capacity] : canonical_resources.capacities) {
    contributions.push_back(ResourceUsage{
        .resource = resource,
        .has_capacity_override = true,
        .capacity_units = capacity,
    });
  }
  for (const auto& [resource, price] : canonical_resources.prices) {
    contributions.push_back(ResourceUsage{
        .resource = resource,
        .has_explicit_price = true,
        .price_per_usage_unit = price,
    });
  }
  std::vector<candidates::StoredCandidate> selected_candidates;
  selected_candidates.reserve(world.selected_net_count);
  for (const NetSelection& selection : world.selections) {
    if (selection.candidate != nullptr) {
      selected_candidates.push_back(selection.candidate);
    }
  }
  internal::SelectedResourceAccumulationResult accumulation_result =
      AccumulateSelectedResourcesImpl(selected_candidates,
                                      request.limits.maximum_expanded_resource_uses);
  if (const auto* failure = std::get_if<AllocationError>(&accumulation_result);
      failure != nullptr) {
    return *failure;
  }
  internal::SelectedResourceAccumulation accumulation =
      std::get<internal::SelectedResourceAccumulation>(std::move(accumulation_result));
  world.selected_logical_resource_uses = accumulation.logical_resource_uses;
  world.accounting_materialized_resource_edges = accumulation.materialized_resource_edges;
  world.accounting_span_boundary_events = accumulation.span_boundary_events;
  for (const internal::AccumulatedResourceUse& accumulated : accumulation.resources) {
    contributions.push_back(ResourceUsage{
        .resource = accumulated.resource,
        .usage_units = accumulated.usage_units,
    });
  }

  std::ranges::sort(contributions, {}, &ResourceUsage::resource);
  std::size_t reduced_size = 0;
  for (std::size_t read = 0; read < contributions.size(); ++read) {
    const ResourceUsage contribution = contributions[read];
    if (reduced_size == 0 || contributions[reduced_size - 1].resource != contribution.resource) {
      contributions[reduced_size] = ResourceUsage{
          .resource = contribution.resource,
          .capacity_units = request.capacities.default_capacity_units(),
      };
      ++reduced_size;
    }
    ResourceUsage& usage = contributions[reduced_size - 1];
    if (contribution.has_capacity_override) {
      usage.has_capacity_override = true;
      usage.capacity_units = contribution.capacity_units;
    }
    if (contribution.has_explicit_price) {
      usage.has_explicit_price = true;
      usage.price_per_usage_unit = contribution.price_per_usage_unit;
    }
    if (usage.usage_units > std::numeric_limits<std::uint64_t>::max() - contribution.usage_units) {
      return Error(AllocationErrorCode::kUsageOverflow, "allocator.usage.resource_overflow.v1",
                   "Selected resource usage exceeds uint64");
    }
    usage.usage_units += contribution.usage_units;
  }
  contributions.resize(reduced_size);
  world.resources = std::move(contributions);
  for (ResourceUsage& usage : world.resources) {
    if (usage.usage_units > usage.capacity_units) {
      usage.overuse_units = usage.usage_units - usage.capacity_units;
      if (world.total_overuse_units >
          std::numeric_limits<std::uint64_t>::max() - usage.overuse_units) {
        return Error(AllocationErrorCode::kUsageOverflow, "allocator.usage.total_overflow.v1",
                     "World over-capacity total exceeds uint64");
      }
      world.total_overuse_units += usage.overuse_units;
      ++world.overused_resource_count;
    }
  }
  world.world_checksum = world.schema_version == kOneWorldAllocationSchemaVersionV1
                             ? internal::ComputeOneWorldChecksumV1(world)
                             : internal::ComputeOneWorldChecksumV2(world);
  return world;
}

}  // namespace

bool internal::OneWorldAllocatorLimitsAreValidV1(const OneWorldAllocatorLimits& limits) noexcept {
  return limits.maximum_nets > 0 && limits.maximum_nets <= kMaximumAllocatorNetsV1 &&
         limits.maximum_candidates > 0 &&
         limits.maximum_candidates <= kMaximumAllocatorCandidatesV1 &&
         limits.maximum_resource_records > 0 &&
         limits.maximum_resource_records <= kMaximumAllocatorResourceRecordsV1 &&
         limits.maximum_expanded_resource_uses > 0 &&
         limits.maximum_expanded_resource_uses <= kMaximumAllocatorExpandedResourceUsesV1;
}

bool internal::CandidateMatchesWorkloadRequestV1(
    const candidates::RouteCandidate& candidate,
    const PreparedNetRoutingContext& workload_context) noexcept {
  const std::vector<candidates::CandidatePrimitive>& geometry = candidate.data().geometry;
  if (geometry.empty()) {
    return false;
  }
  const auto* first = std::get_if<candidates::ExactLinePrimitive>(&geometry.front());
  const auto* last = std::get_if<candidates::ExactLinePrimitive>(&geometry.back());
  return first != nullptr && last != nullptr &&
         first->layer == workload_context.request.start_layer &&
         first->centerline.start == workload_context.request.start &&
         last->layer == workload_context.request.goal_layer &&
         last->centerline.end == workload_context.request.goal;
}

bool internal::CandidateHasAuthenticLivePayloadV1(
    const candidates::StoredCandidate& candidate,
    const PreparedNetRoutingContext& workload_context) noexcept {
  if (candidate == nullptr) {
    return false;
  }
  const candidates::GeneratedRouteCandidate& data = candidate->data();
  const std::optional<std::uint64_t> logical_bytes = candidates::ComputeCandidateLogicalBytes(data);
  return data.id == candidates::DeriveCandidateId(data.net, data.associations, data.policy_identity,
                                                  data.provenance) &&
         data.geometry_signature == candidates::ComputeGeometrySignature(data.geometry) &&
         data.resource_signature == candidates::ComputeResourceSignature(data.resources) &&
         data.payload_checksum == candidates::ComputeCandidatePayloadChecksum(data) &&
         logical_bytes.has_value() && data.logical_bytes == *logical_bytes &&
         CandidateMatchesWorkloadRequestV1(*candidate, workload_context);
}

ResourceCapacityModelResult BuildResourceCapacityModel(
    std::uint32_t schema_version, const board_ir::BoardSnapshot& board,
    const geometry_compiler::CompiledBoard& compiled_board, std::uint32_t default_capacity_units,
    const std::vector<ResourceCapacityOverride>& overrides) {
  if (!internal::ResourceRecordCountFitsV1(overrides.size())) {
    return Error(AllocationErrorCode::kInputBoundExceeded,
                 "allocator.capacity.resource_record_hard_bound.v1",
                 "Capacity overrides exceed the schema-v1 hard bound");
  }
  try {
    std::vector<ResourceCapacityOverride> owned(overrides);
    return BuildResourceCapacityModel(schema_version, board, compiled_board, default_capacity_units,
                                      std::move(owned));
  } catch (const std::bad_alloc&) {
    return Error(AllocationErrorCode::kResourceExhausted, "allocator.host_memory_exhausted.v1",
                 "Host allocation failed while building immutable capacity state");
  } catch (const std::length_error&) {
    return Error(AllocationErrorCode::kResourceExhausted, "allocator.host_container_exhausted.v1",
                 "Host container limits were exhausted while building capacity state");
  }
}

ResourceCapacityModelResult BuildResourceCapacityModel(
    std::uint32_t schema_version, const board_ir::BoardSnapshot& board,
    const geometry_compiler::CompiledBoard& compiled_board, std::uint32_t default_capacity_units,
    std::vector<ResourceCapacityOverride>&& overrides) {
  if (!internal::ResourceRecordCountFitsV1(overrides.size())) {
    return Error(AllocationErrorCode::kInputBoundExceeded,
                 "allocator.capacity.resource_record_hard_bound.v1",
                 "Capacity overrides exceed the schema-v1 hard bound");
  }
  try {
    if (schema_version != kResourceCapacityModelSchemaVersion) {
      return Error(AllocationErrorCode::kUnsupportedSchema, "allocator.capacity.schema.v1",
                   "Resource-capacity schema is unsupported");
    }
    if (compiled_board.source_board_content_hash() != board.content_hash() ||
        compiled_board.compiler_version() != geometry_compiler::kGeometryCompilerVersion ||
        compiled_board.compiler_profile_fingerprint() !=
            geometry_compiler::FingerprintCompilerProfile(compiled_board.profile())) {
      return Error(AllocationErrorCode::kCandidateAssociationMismatch,
                   "allocator.capacity.compiled_board_association.v1",
                   "Capacity state requires an authentic compiled view of the supplied board");
    }
    if (default_capacity_units > 1U) {
      return Error(AllocationErrorCode::kInvalidConfiguration,
                   "allocator.capacity.configuration.v1",
                   "Capacity state requires binary physical-edge capacity");
    }
    std::ranges::sort(
        overrides, [](const ResourceCapacityOverride& left, const ResourceCapacityOverride& right) {
          return std::tie(left.resource, left.capacity_units) <
                 std::tie(right.resource, right.capacity_units);
        });
    for (std::size_t index = 0; index < overrides.size(); ++index) {
      if (!IsCanonicalResource(overrides[index].resource)) {
        return Error(AllocationErrorCode::kInvalidResource, "allocator.capacity.resource_key.v1",
                     "A capacity override does not name a canonical physical edge");
      }
      if (overrides[index].capacity_units > 1U) {
        return Error(AllocationErrorCode::kInvalidConfiguration,
                     "allocator.capacity.binary_physical_edge.v1",
                     "Physical-edge capacity v1 must be zero or one routing unit");
      }
      if (index != 0 && overrides[index - 1].resource == overrides[index].resource) {
        return Error(AllocationErrorCode::kDuplicateResource,
                     "allocator.capacity.duplicate_resource.v1",
                     "A physical resource has more than one capacity override");
      }
    }
    const AllocationAssociations associations{
        .board_content_hash = board.content_hash(),
        .compiler_profile_fingerprint = compiled_board.compiler_profile_fingerprint(),
        .geometry_compiler_version = compiled_board.compiler_version(),
    };
    return ResourceCapacityModel(schema_version, associations, default_capacity_units,
                                 std::move(overrides));
  } catch (const std::bad_alloc&) {
    return Error(AllocationErrorCode::kResourceExhausted, "allocator.host_memory_exhausted.v1",
                 "Host allocation failed while building immutable capacity state");
  } catch (const std::length_error&) {
    return Error(AllocationErrorCode::kResourceExhausted, "allocator.host_container_exhausted.v1",
                 "Host container limits were exhausted while building capacity state");
  }
}

PriceSnapshotResult BuildPriceSnapshot(std::uint32_t schema_version,
                                       const ResourceCapacityModel& capacities,
                                       std::uint32_t iteration,
                                       const std::vector<ResourcePrice>& prices) {
  if (!internal::CombinedResourceRecordCountFitsV1(capacities.overrides().size(), prices.size())) {
    return Error(AllocationErrorCode::kInputBoundExceeded,
                 "allocator.price.resource_record_hard_bound.v1",
                 "Combined capacity and price records exceed the schema-v1 hard bound");
  }
  try {
    std::vector<ResourcePrice> owned(prices);
    return BuildPriceSnapshot(schema_version, capacities, iteration, std::move(owned));
  } catch (const std::bad_alloc&) {
    return Error(AllocationErrorCode::kResourceExhausted, "allocator.host_memory_exhausted.v1",
                 "Host allocation failed while building immutable price state");
  } catch (const std::length_error&) {
    return Error(AllocationErrorCode::kResourceExhausted, "allocator.host_container_exhausted.v1",
                 "Host container limits were exhausted while building price state");
  }
}

PriceSnapshotResult BuildPriceSnapshot(std::uint32_t schema_version,
                                       const ResourceCapacityModel& capacities,
                                       std::uint32_t iteration,
                                       std::vector<ResourcePrice>&& prices) {
  if (!internal::CombinedResourceRecordCountFitsV1(capacities.overrides().size(), prices.size())) {
    return Error(AllocationErrorCode::kInputBoundExceeded,
                 "allocator.price.resource_record_hard_bound.v1",
                 "Combined capacity and price records exceed the schema-v1 hard bound");
  }
  try {
    if (schema_version != kPriceSnapshotSchemaVersion) {
      return Error(AllocationErrorCode::kUnsupportedSchema, "allocator.price.schema.v1",
                   "Resource-price schema is unsupported");
    }
    std::ranges::sort(prices, [](const ResourcePrice& left, const ResourcePrice& right) {
      return std::tie(left.resource, left.price_per_usage_unit) <
             std::tie(right.resource, right.price_per_usage_unit);
    });
    for (std::size_t index = 0; index < prices.size(); ++index) {
      if (!IsCanonicalResource(prices[index].resource)) {
        return Error(AllocationErrorCode::kInvalidResource, "allocator.price.resource_key.v1",
                     "A price record does not name a canonical physical edge");
      }
      if (index != 0 && prices[index - 1].resource == prices[index].resource) {
        return Error(AllocationErrorCode::kDuplicateResource,
                     "allocator.price.duplicate_resource.v1",
                     "A physical resource has more than one price in a snapshot");
      }
    }
    return PriceSnapshot(schema_version, capacities.associations(), iteration, std::move(prices));
  } catch (const std::bad_alloc&) {
    return Error(AllocationErrorCode::kResourceExhausted, "allocator.host_memory_exhausted.v1",
                 "Host allocation failed while building immutable price state");
  } catch (const std::length_error&) {
    return Error(AllocationErrorCode::kResourceExhausted, "allocator.host_container_exhausted.v1",
                 "Host container limits were exhausted while building price state");
  }
}

internal::SelectedResourceAccumulationResult internal::AccumulateSelectedResources(
    std::span<const candidates::StoredCandidate> selected_candidates,
    std::uint64_t maximum_expanded_resource_uses) {
  return AccumulateSelectedResourcesImpl(selected_candidates, maximum_expanded_resource_uses);
}

internal::OneWorldSelectionEvidenceResult internal::SelectOneWorldWithoutAccounting(
    const OneWorldAllocationRequest& request) {
  return internal::RunWithAllocationFailureEnvelope(
      [&request]() -> internal::OneWorldSelectionEvidenceResult {
        PreparedOneWorldSelectionResult prepared_result = PrepareOneWorldSelection(request);
        if (const auto* failure = std::get_if<AllocationError>(&prepared_result);
            failure != nullptr) {
          return *failure;
        }
        return std::move(std::get<PreparedOneWorldSelection>(prepared_result).evidence);
      });
}

internal::SelectedResourceAccumulationResult internal::AccumulateResourceSpanStreams(
    std::span<const ResourceSpanStream> streams, std::uint64_t maximum_expanded_resource_uses) {
  return AccumulateResourceSpanStreamsImpl(streams, maximum_expanded_resource_uses);
}

std::uint64_t internal::ComputeOneWorldChecksumV1(const OneWorldAllocation& world) noexcept {
  return ComputeWorldChecksumImpl(world, false);
}

std::uint64_t internal::ComputeOneWorldChecksumV2(const OneWorldAllocation& world) noexcept {
  return ComputeWorldChecksumImpl(world, true);
}

std::uint64_t internal::RecomputeOneWorldPoolManifestChecksumV1(
    std::span<const CandidatePool> pools) noexcept {
  board_ir::StableHashBuilder manifest;
  manifest.AddString("APGAR-ONE-WORLD-POOLS-MANIFEST-V1");
  manifest.AddU64(static_cast<std::uint64_t>(pools.size()));
  for (const CandidatePool& pool : pools) {
    board_ir::StableHashBuilder pool_hash;
    pool_hash.AddString("APGAR-ONE-WORLD-POOL-MANIFEST-V1");
    pool_hash.AddU64(pool.net.id);
    pool_hash.AddU32(pool.net.generation);
    pool_hash.AddU64(static_cast<std::uint64_t>(pool.candidates.size()));
    for (const candidates::StoredCandidate& candidate : pool.candidates) {
      if (candidate == nullptr) {
        return 0;
      }
      pool_hash.AddU64(candidate->id().high);
      pool_hash.AddU64(candidate->id().low);
      pool_hash.AddU64(candidate->data().payload_checksum);
    }
    manifest.AddU64(pool.net.id);
    manifest.AddU32(pool.net.generation);
    manifest.AddU64(pool_hash.Finish());
  }
  return manifest.Finish();
}

OneWorldAllocationResult AllocateOneWorld(const OneWorldAllocationRequest& request) {
  return internal::RunWithAllocationFailureEnvelope(
      [&request]() { return AllocateOneWorldImpl(request); });
}

}  // namespace apgar::allocator
