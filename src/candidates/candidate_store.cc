#include "apgar/candidates/candidate_store.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <ranges>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

namespace apgar::candidates {
namespace {

using routing::EdgeResourceKey;
using Wide = __int128_t;
using UWide = __uint128_t;

[[nodiscard]] auto NetKey(board_ir::EntityRef net) noexcept {
  return std::tuple{net.id, net.generation};
}

[[nodiscard]] bool SameCandidateContext(const RouteCandidate& left,
                                        const RouteCandidate& right) noexcept {
  return left.net() == right.net() && left.data().associations == right.data().associations;
}

[[nodiscard]] bool RejectionBefore(const CandidateRejection& left,
                                   const CandidateRejection& right) noexcept {
  const board_ir::EntityRef left_net = left.net.value_or(board_ir::EntityRef{});
  const board_ir::EntityRef right_net = right.net.value_or(board_ir::EntityRef{});
  const CandidateId left_id = left.candidate_id.value_or(CandidateId{});
  const CandidateId right_id = right.candidate_id.value_or(CandidateId{});
  const board_ir::EntityRef left_conflict = left.conflicting_entity.value_or(board_ir::EntityRef{});
  const board_ir::EntityRef right_conflict =
      right.conflicting_entity.value_or(board_ir::EntityRef{});
  const bool left_has_candidate_id = left.candidate_id.has_value();
  const bool right_has_candidate_id = right.candidate_id.has_value();
  const bool left_has_net = left.net.has_value();
  const bool right_has_net = right.net.has_value();
  const bool left_has_conflict = left.conflicting_entity.has_value();
  const bool right_has_conflict = right.conflicting_entity.has_value();
  return std::tuple{left.schema_version,
                    left_has_candidate_id,
                    left_id,
                    left_has_net,
                    left_net.id,
                    left_net.generation,
                    left.stage,
                    left.code,
                    left.invariant_id.size(),
                    std::string_view(left.invariant_id),
                    left.associations.board_content_hash,
                    left.associations.compiler_profile_fingerprint,
                    left.associations.geometry_compiler_version,
                    left.associations.routing_profile_fingerprint,
                    left.associations.rule_bucket_identity,
                    left.policy_identity,
                    left.provenance.generator,
                    left.provenance.generator_version,
                    left.provenance.backend,
                    left.provenance.supported_device_class.size(),
                    std::string_view(left.provenance.supported_device_class),
                    left.provenance.deterministic_seed,
                    left.provenance.batch_identity,
                    left.provenance.query_identity,
                    left.provenance.candidate_ordinal,
                    left.primitive_witness_index,
                    left.resource_witness_index,
                    left.expected_value,
                    left.actual_value,
                    left_has_conflict,
                    left_conflict.id,
                    left_conflict.generation,
                    left.candidate_payload_checksum,
                    left.detail.size(),
                    std::string_view(left.detail),
                    left.logical_bytes} <
         std::tuple{right.schema_version,
                    right_has_candidate_id,
                    right_id,
                    right_has_net,
                    right_net.id,
                    right_net.generation,
                    right.stage,
                    right.code,
                    right.invariant_id.size(),
                    std::string_view(right.invariant_id),
                    right.associations.board_content_hash,
                    right.associations.compiler_profile_fingerprint,
                    right.associations.geometry_compiler_version,
                    right.associations.routing_profile_fingerprint,
                    right.associations.rule_bucket_identity,
                    right.policy_identity,
                    right.provenance.generator,
                    right.provenance.generator_version,
                    right.provenance.backend,
                    right.provenance.supported_device_class.size(),
                    std::string_view(right.provenance.supported_device_class),
                    right.provenance.deterministic_seed,
                    right.provenance.batch_identity,
                    right.provenance.query_identity,
                    right.provenance.candidate_ordinal,
                    right.primitive_witness_index,
                    right.resource_witness_index,
                    right.expected_value,
                    right.actual_value,
                    right_has_conflict,
                    right_conflict.id,
                    right_conflict.generation,
                    right.candidate_payload_checksum,
                    right.detail.size(),
                    std::string_view(right.detail),
                    right.logical_bytes};
}

[[nodiscard]] bool EntityRefBefore(board_ir::EntityRef left, board_ir::EntityRef right) noexcept {
  return NetKey(left) < NetKey(right);
}

[[nodiscard]] bool PolicyBefore(const routing::CandidateGenerationPolicy& left,
                                const routing::CandidateGenerationPolicy& right) noexcept {
  const auto left_prefix =
      std::tie(left.schema_version, left.objective, left.deterministic_seed, left.candidate_ordinal,
               left.orthogonal_step_surcharge, left.diagonal_step_surcharge, left.bend_surcharge);
  const auto right_prefix = std::tie(
      right.schema_version, right.objective, right.deterministic_seed, right.candidate_ordinal,
      right.orthogonal_step_surcharge, right.diagonal_step_surcharge, right.bend_surcharge);
  if (left_prefix != right_prefix) {
    return left_prefix < right_prefix;
  }
  if (left.banned_resources != right.banned_resources) {
    return std::ranges::lexicographical_compare(left.banned_resources, right.banned_resources);
  }
  return std::ranges::lexicographical_compare(left.resource_penalties, right.resource_penalties);
}

[[nodiscard]] bool ProvenanceBefore(const CandidateProvenance& left,
                                    const CandidateProvenance& right) noexcept {
  return std::tie(left.generator, left.generator_version, left.backend, left.supported_device_class,
                  left.deterministic_seed, left.batch_identity, left.query_identity,
                  left.candidate_ordinal) < std::tie(right.generator, right.generator_version,
                                                     right.backend, right.supported_device_class,
                                                     right.deterministic_seed, right.batch_identity,
                                                     right.query_identity, right.candidate_ordinal);
}

[[nodiscard]] bool PrimitiveBefore(const CandidatePrimitive& left,
                                   const CandidatePrimitive& right) noexcept {
  if (left.index() != right.index()) {
    return left.index() < right.index();
  }
  if (const auto* left_line = std::get_if<ExactLinePrimitive>(&left); left_line != nullptr) {
    const ExactLinePrimitive& right_line = std::get<ExactLinePrimitive>(right);
    return std::tie(left_line->layer, left_line->centerline.start.x, left_line->centerline.start.y,
                    left_line->centerline.end.x, left_line->centerline.end.y) <
           std::tie(right_line.layer, right_line.centerline.start.x, right_line.centerline.start.y,
                    right_line.centerline.end.x, right_line.centerline.end.y);
  }
  const ThroughViaPrimitive& left_via = std::get<ThroughViaPrimitive>(left);
  const ThroughViaPrimitive& right_via = std::get<ThroughViaPrimitive>(right);
  return std::tie(left_via.template_id, left_via.position.x, left_via.position.y,
                  left_via.start_layer, left_via.end_layer) <
         std::tie(right_via.template_id, right_via.position.x, right_via.position.y,
                  right_via.start_layer, right_via.end_layer);
}

[[nodiscard]] bool GeometryBefore(std::span<const CandidatePrimitive> left,
                                  std::span<const CandidatePrimitive> right) noexcept {
  return std::lexicographical_compare(left.begin(), left.end(), right.begin(), right.end(),
                                      PrimitiveBefore);
}

[[nodiscard]] bool CandidateCanonicalBefore(const RouteCandidate& left,
                                            const RouteCandidate& right) noexcept {
  const GeneratedRouteCandidate& a = left.data();
  const GeneratedRouteCandidate& b = right.data();
  const auto a_header = std::tuple{a.schema_major,
                                   a.schema_minor,
                                   a.id,
                                   a.net.id,
                                   a.net.generation,
                                   a.intended_terminals[0].id,
                                   a.intended_terminals[0].generation,
                                   a.intended_terminals[1].id,
                                   a.intended_terminals[1].generation,
                                   a.associations.board_content_hash,
                                   a.associations.compiler_profile_fingerprint,
                                   a.associations.geometry_compiler_version,
                                   a.associations.routing_profile_fingerprint,
                                   a.associations.rule_bucket_identity,
                                   a.geometry_schema_version,
                                   a.resource_schema_version};
  const auto b_header = std::tuple{b.schema_major,
                                   b.schema_minor,
                                   b.id,
                                   b.net.id,
                                   b.net.generation,
                                   b.intended_terminals[0].id,
                                   b.intended_terminals[0].generation,
                                   b.intended_terminals[1].id,
                                   b.intended_terminals[1].generation,
                                   b.associations.board_content_hash,
                                   b.associations.compiler_profile_fingerprint,
                                   b.associations.geometry_compiler_version,
                                   b.associations.routing_profile_fingerprint,
                                   b.associations.rule_bucket_identity,
                                   b.geometry_schema_version,
                                   b.resource_schema_version};
  if (a_header != b_header) {
    return a_header < b_header;
  }
  if (a.policy != b.policy) {
    return PolicyBefore(a.policy, b.policy);
  }
  if (a.policy_identity != b.policy_identity) {
    return a.policy_identity < b.policy_identity;
  }
  if (a.provenance != b.provenance) {
    return ProvenanceBefore(a.provenance, b.provenance);
  }
  if (a.geometry != b.geometry) {
    return GeometryBefore(a.geometry, b.geometry);
  }
  if (a.resources != b.resources) {
    return std::ranges::lexicographical_compare(a.resources, b.resources);
  }
  const auto a_tail = std::tuple{a.metrics.scalar_policy_cost,
                                 a.metrics.intrinsic_base_cost,
                                 a.metrics.orthogonal_step_count,
                                 a.metrics.diagonal_step_count,
                                 a.metrics.bend_count,
                                 a.metrics.line_primitive_count,
                                 a.metrics.via_count,
                                 a.metrics.axis_aligned_length_dbu,
                                 a.metrics.diagonal_projection_dbu,
                                 a.constraints.supported_hard_constraints_satisfied,
                                 a.constraints.unsupported_rules_remain,
                                 a.constraints.connected_intended_terminal_count,
                                 a.constraints.exact_validation_code,
                                 a.geometry_signature,
                                 a.resource_signature,
                                 a.payload_checksum,
                                 a.logical_bytes};
  const auto b_tail = std::tuple{b.metrics.scalar_policy_cost,
                                 b.metrics.intrinsic_base_cost,
                                 b.metrics.orthogonal_step_count,
                                 b.metrics.diagonal_step_count,
                                 b.metrics.bend_count,
                                 b.metrics.line_primitive_count,
                                 b.metrics.via_count,
                                 b.metrics.axis_aligned_length_dbu,
                                 b.metrics.diagonal_projection_dbu,
                                 b.constraints.supported_hard_constraints_satisfied,
                                 b.constraints.unsupported_rules_remain,
                                 b.constraints.connected_intended_terminal_count,
                                 b.constraints.exact_validation_code,
                                 b.geometry_signature,
                                 b.resource_signature,
                                 b.payload_checksum,
                                 b.logical_bytes};
  return a_tail < b_tail;
}

[[nodiscard]] bool CandidateTotalBefore(const RouteCandidate& left,
                                        const RouteCandidate& right) noexcept {
  return CandidateRanksBefore(left, right);
}

struct DuplicateRelation {
  CandidateRejectionCode code;
  const char* invariant;
};

[[nodiscard]] std::optional<DuplicateRelation> DuplicateRelationBetween(
    const RouteCandidate& candidate, const RouteCandidate& incumbent) noexcept {
  if (candidate.id() == incumbent.id()) {
    return DuplicateRelation{
        .code = CandidateRejectionCode::kDuplicateIdentity,
        .invariant = "candidate.store.duplicate_id.v1",
    };
  }
  if (SameCandidateContext(candidate, incumbent) &&
      candidate.data().geometry_signature == incumbent.data().geometry_signature &&
      CanonicalGeometryEqual(candidate, incumbent)) {
    return DuplicateRelation{
        .code = CandidateRejectionCode::kDuplicateGeometry,
        .invariant = "candidate.store.duplicate_geometry.v1",
    };
  }
  if (SameCandidateContext(candidate, incumbent) &&
      candidate.data().resource_signature == incumbent.data().resource_signature &&
      CanonicalResourcesEqual(candidate, incumbent)) {
    return DuplicateRelation{
        .code = CandidateRejectionCode::kDuplicateResources,
        .invariant = "candidate.store.duplicate_resources.v1",
    };
  }
  return std::nullopt;
}

[[nodiscard]] bool StoreResultBefore(const CandidateStoreAdmissionResult& left,
                                     const CandidateStoreAdmissionResult& right) noexcept {
  const auto result_net =
      [](const CandidateStoreAdmissionResult& result) -> std::optional<board_ir::EntityRef> {
    if (const auto* stored = std::get_if<StoredCandidate>(&result); stored != nullptr) {
      return (*stored)->net();
    }
    return std::get<CandidateRejection>(result).net;
  };
  const auto result_id =
      [](const CandidateStoreAdmissionResult& result) -> std::optional<CandidateId> {
    if (const auto* stored = std::get_if<StoredCandidate>(&result); stored != nullptr) {
      return (*stored)->id();
    }
    return std::get<CandidateRejection>(result).candidate_id;
  };
  const std::optional<board_ir::EntityRef> left_net = result_net(left);
  const std::optional<board_ir::EntityRef> right_net = result_net(right);
  if (left_net.has_value() != right_net.has_value()) {
    return !left_net.has_value();
  }
  if (left_net.has_value() && *left_net != *right_net) {
    return EntityRefBefore(*left_net, *right_net);
  }
  const std::optional<CandidateId> left_id = result_id(left);
  const std::optional<CandidateId> right_id = result_id(right);
  if (left_id != right_id) {
    return left_id < right_id;
  }
  if (left.index() != right.index()) {
    return left.index() < right.index();
  }
  if (const auto* left_stored = std::get_if<StoredCandidate>(&left); left_stored != nullptr) {
    return CandidateTotalBefore(**left_stored, **std::get_if<StoredCandidate>(&right));
  }
  return RejectionBefore(std::get<CandidateRejection>(left), std::get<CandidateRejection>(right));
}

[[nodiscard]] CandidateRejection StoreRejection(const RouteCandidate& candidate,
                                                CandidateRejectionCode code,
                                                std::string invariant_id, std::string detail) {
  CandidateRejection rejection;
  rejection.candidate_id = candidate.id();
  rejection.net = candidate.net();
  rejection.stage = CandidateLifecycleStage::kSignedAndDeduplicated;
  rejection.code = code;
  rejection.invariant_id = std::move(invariant_id);
  rejection.associations = candidate.data().associations;
  rejection.policy_identity = candidate.data().policy_identity;
  rejection.provenance = candidate.data().provenance;
  rejection.candidate_payload_checksum = candidate.data().payload_checksum;
  rejection.detail = std::move(detail);
  return CanonicalizeCandidateRejectionV1(std::move(rejection));
}

[[nodiscard]] CandidateRejection BudgetRejection(const RouteCandidate& candidate,
                                                 std::string detail) {
  CandidateRejection rejection =
      StoreRejection(candidate, CandidateRejectionCode::kBudgetExhausted,
                     "candidate.store.per_net_budget.v1", std::move(detail));
  rejection.stage = CandidateLifecycleStage::kStored;
  return CanonicalizeCandidateRejectionV1(std::move(rejection));
}

struct ExpandedResourceCursor {
  std::span<const PhysicalEdgeSpan> spans;
  std::size_t span_index = 0;
  std::uint32_t edge_offset = 0;

  [[nodiscard]] bool done() const noexcept { return span_index >= spans.size(); }

  [[nodiscard]] EdgeResourceKey current() const noexcept {
    const PhysicalEdgeSpan& span = spans[span_index];
    const geometry_compiler::DirectionDelta delta = ResourceSpanStorageDelta(span.direction);
    return EdgeResourceKey{
        .layer = span.layer,
        .lattice_x = static_cast<std::int64_t>(static_cast<Wide>(span.lattice_x) +
                                               static_cast<Wide>(delta.x) * edge_offset),
        .lattice_y = static_cast<std::int64_t>(static_cast<Wide>(span.lattice_y) +
                                               static_cast<Wide>(delta.y) * edge_offset),
        .direction = span.direction,
    };
  }

  void Advance() noexcept {
    ++edge_offset;
    if (edge_offset == spans[span_index].edge_count) {
      ++span_index;
      edge_offset = 0;
    }
  }
};

struct ResourceCounts {
  std::uint64_t left = 0;
  std::uint64_t right = 0;
  std::uint64_t intersection = 0;
};

enum class ProjectionFamily : std::uint8_t {
  kHorizontal = 0,
  kVertical = 1,
  kRisingDiagonal = 2,
  kFallingDiagonal = 3,
};

struct ProjectedInterval {
  board_ir::LayerId layer = 0;
  ProjectionFamily family = ProjectionFamily::kHorizontal;
  Wide supporting_line = 0;
  std::int64_t begin = 0;
  std::int64_t end = 0;
};

[[nodiscard]] bool ProjectionLineBefore(const ProjectedInterval& left,
                                        const ProjectedInterval& right) noexcept {
  if (left.layer != right.layer) {
    return left.layer < right.layer;
  }
  if (left.family != right.family) {
    return left.family < right.family;
  }
  return left.supporting_line < right.supporting_line;
}

[[nodiscard]] bool ProjectedIntervalBefore(const ProjectedInterval& left,
                                           const ProjectedInterval& right) noexcept {
  if (ProjectionLineBefore(left, right)) {
    return true;
  }
  if (ProjectionLineBefore(right, left)) {
    return false;
  }
  return std::tie(left.begin, left.end) < std::tie(right.begin, right.end);
}

[[nodiscard]] bool BuildProjectedIntervals(const RouteCandidate& candidate,
                                           std::vector<ProjectedInterval>& intervals,
                                           UWide& total_projection) {
  intervals.clear();
  intervals.reserve(candidate.data().geometry.size());
  total_projection = 0;
  constexpr UWide kMaximumUWide = ~UWide{0};
  for (const CandidatePrimitive& primitive : candidate.data().geometry) {
    const auto* line = std::get_if<ExactLinePrimitive>(&primitive);
    if (line == nullptr) {
      return false;
    }
    const Wide start_x = line->centerline.start.x;
    const Wide start_y = line->centerline.start.y;
    const Wide end_x = line->centerline.end.x;
    const Wide end_y = line->centerline.end.y;
    const Wide delta_x = end_x - start_x;
    const Wide delta_y = end_y - start_y;
    const Wide absolute_x = delta_x < 0 ? -delta_x : delta_x;
    const Wide absolute_y = delta_y < 0 ? -delta_y : delta_y;

    ProjectedInterval interval;
    interval.layer = line->layer;
    std::int64_t first_projection = 0;
    std::int64_t second_projection = 0;
    if (delta_y == 0 && delta_x != 0) {
      interval.family = ProjectionFamily::kHorizontal;
      interval.supporting_line = start_y;
      first_projection = line->centerline.start.x;
      second_projection = line->centerline.end.x;
    } else if (delta_x == 0 && delta_y != 0) {
      interval.family = ProjectionFamily::kVertical;
      interval.supporting_line = start_x;
      first_projection = line->centerline.start.y;
      second_projection = line->centerline.end.y;
    } else if (absolute_x == absolute_y && absolute_x != 0) {
      first_projection = line->centerline.start.x;
      second_projection = line->centerline.end.x;
      if ((delta_x < 0) == (delta_y < 0)) {
        interval.family = ProjectionFamily::kRisingDiagonal;
        interval.supporting_line = start_y - start_x;
      } else {
        interval.family = ProjectionFamily::kFallingDiagonal;
        interval.supporting_line = start_y + start_x;
      }
    } else {
      return false;
    }
    interval.begin = std::min(first_projection, second_projection);
    interval.end = std::max(first_projection, second_projection);
    const UWide length =
        static_cast<UWide>(static_cast<Wide>(interval.end) - static_cast<Wide>(interval.begin));
    if (length > kMaximumUWide - total_projection) {
      return false;
    }
    total_projection += length;
    intervals.push_back(interval);
  }
  std::ranges::sort(intervals, ProjectedIntervalBefore);
  return total_projection != 0;
}

[[nodiscard]] UWide SharedProjectedLength(std::span<const ProjectedInterval> left,
                                          std::span<const ProjectedInterval> right) noexcept {
  constexpr UWide kMaximumUWide = ~UWide{0};
  UWide shared = 0;
  std::size_t left_index = 0;
  std::size_t right_index = 0;
  while (left_index < left.size() && right_index < right.size()) {
    if (ProjectionLineBefore(left[left_index], right[right_index])) {
      ++left_index;
      continue;
    }
    if (ProjectionLineBefore(right[right_index], left[left_index])) {
      ++right_index;
      continue;
    }
    const std::int64_t overlap_begin = std::max(left[left_index].begin, right[right_index].begin);
    const std::int64_t overlap_end = std::min(left[left_index].end, right[right_index].end);
    if (overlap_begin < overlap_end) {
      const UWide overlap =
          static_cast<UWide>(static_cast<Wide>(overlap_end) - static_cast<Wide>(overlap_begin));
      if (overlap > kMaximumUWide - shared) {
        return 0;
      }
      shared += overlap;
    }
    if (left[left_index].end < right[right_index].end) {
      ++left_index;
    } else if (right[right_index].end < left[left_index].end) {
      ++right_index;
    } else {
      ++left_index;
      ++right_index;
    }
  }
  return shared;
}

struct RetentionSelection {
  std::vector<StoredCandidate> retained;
  std::vector<StoredCandidate> pruned;
  bool pinned_budget_failure = false;
};

[[nodiscard]] bool CandidatePointerRanksBefore(const StoredCandidate& left,
                                               const StoredCandidate& right) noexcept;

[[nodiscard]] bool ContainsCandidate(std::span<const StoredCandidate> candidates,
                                     const StoredCandidate& candidate) noexcept {
  return std::ranges::any_of(candidates, [&candidate](const StoredCandidate& current) {
    return current.get() == candidate.get();
  });
}

[[nodiscard]] bool ResourceEquivalent(const RouteCandidate& left,
                                      const RouteCandidate& right) noexcept {
  return SameCandidateContext(left, right) &&
         left.data().resource_signature == right.data().resource_signature &&
         CanonicalResourcesEqual(left, right);
}

[[nodiscard]] RetentionSelection SelectRetention(
    std::vector<StoredCandidate> pool, const CandidateStoreConfig& config,
    const std::map<CandidateId, std::uint64_t>& pin_counts,
    std::uint64_t* candidate_inspections = nullptr) {
  const auto inspect = [candidate_inspections]() {
    if (candidate_inspections != nullptr &&
        *candidate_inspections != std::numeric_limits<std::uint64_t>::max()) {
      ++*candidate_inspections;
    }
  };
  std::ranges::sort(pool, CandidatePointerRanksBefore);
  RetentionSelection selection;
  std::uint64_t retained_bytes = 0;
  const auto is_pinned = [&pin_counts](CandidateId id) { return pin_counts.contains(id); };
  const auto try_add = [&](const StoredCandidate& candidate) {
    if (ContainsCandidate(selection.retained, candidate)) {
      return true;
    }
    if (selection.retained.size() >= config.maximum_candidates_per_net ||
        retained_bytes > config.maximum_candidate_bytes_per_net ||
        candidate->logical_bytes() > config.maximum_candidate_bytes_per_net - retained_bytes) {
      return false;
    }
    retained_bytes += candidate->logical_bytes();
    selection.retained.push_back(candidate);
    return true;
  };

  // CAN-002: pins are the only hard retention class. If they cannot fit, fail
  // closed and leave the current store untouched.
  for (const StoredCandidate& candidate : pool) {
    inspect();
    if (is_pinned(candidate->id()) && !try_add(candidate)) {
      selection.pinned_budget_failure = true;
      return selection;
    }
  }

  // Preserve the best currently nondominated alternatives while capacity
  // remains. Stable rank resolves an overfull Pareto frontier.
  for (const StoredCandidate& candidate : pool) {
    const bool dominated =
        std::ranges::any_of(pool, [&candidate, &inspect](const StoredCandidate& other) {
          inspect();
          return candidate.get() != other.get() && CandidateMetricsDominate(*other, *candidate);
        });
    if (!dominated) {
      (void)try_add(candidate);
    }
  }

  // Then preserve the stable best representative of each collision-safe exact
  // resource footprint. A signature match alone never forms a group.
  std::vector<StoredCandidate> resource_representatives;
  for (const StoredCandidate& candidate : pool) {
    const bool represented = std::ranges::any_of(
        resource_representatives, [&candidate, &inspect](const StoredCandidate& representative) {
          inspect();
          return ResourceEquivalent(*candidate, *representative);
        });
    if (!represented) {
      resource_representatives.push_back(candidate);
      (void)try_add(candidate);
    }
  }

  // Fill any remaining count/byte headroom by the total stable rank.
  for (const StoredCandidate& candidate : pool) {
    inspect();
    (void)try_add(candidate);
  }
  std::ranges::sort(selection.retained, CandidatePointerRanksBefore);
  for (const StoredCandidate& candidate : pool) {
    inspect();
    if (!ContainsCandidate(selection.retained, candidate)) {
      selection.pruned.push_back(candidate);
    }
  }
  return selection;
}

[[nodiscard]] ResourceCounts CountResources(const RouteCandidate& left,
                                            const RouteCandidate& right) noexcept {
  ResourceCounts counts;
  for (const PhysicalEdgeSpan& span : left.data().resources) {
    counts.left += span.edge_count;
  }
  for (const PhysicalEdgeSpan& span : right.data().resources) {
    counts.right += span.edge_count;
  }
  ExpandedResourceCursor left_cursor{.spans = left.data().resources};
  ExpandedResourceCursor right_cursor{.spans = right.data().resources};
  while (!left_cursor.done() && !right_cursor.done()) {
    const EdgeResourceKey left_key = left_cursor.current();
    const EdgeResourceKey right_key = right_cursor.current();
    if (left_key < right_key) {
      left_cursor.Advance();
    } else if (right_key < left_key) {
      right_cursor.Advance();
    } else {
      ++counts.intersection;
      left_cursor.Advance();
      right_cursor.Advance();
    }
  }
  return counts;
}

[[nodiscard]] bool CandidatePointerRanksBefore(const StoredCandidate& left,
                                               const StoredCandidate& right) noexcept {
  return CandidateRanksBefore(*left, *right);
}

[[nodiscard]] bool StoreConfigurationIsValid(const CandidateStoreConfig& config) noexcept {
  return config.maximum_candidates_per_net > 0 && config.maximum_candidate_bytes_per_net > 0 &&
         config.maximum_rejection_records > 0 &&
         config.maximum_rejection_items_per_transaction > 0 &&
         config.maximum_admission_items_per_transaction > 0 &&
         config.maximum_admission_input_bytes_per_transaction > 0 &&
         config.maximum_admission_work_units_per_transaction > 0;
}

[[nodiscard]] CandidateRejection TransactionRejection(
    const CandidateAssociations& associations, CandidateRejectionCode code,
    std::string invariant_id, std::string detail,
    std::optional<std::uint64_t> expected = std::nullopt,
    std::optional<std::uint64_t> actual = std::nullopt) {
  CandidateRejection rejection;
  rejection.stage = CandidateLifecycleStage::kGenerated;
  rejection.code = code;
  rejection.invariant_id = std::move(invariant_id);
  rejection.associations = associations;
  rejection.expected_value = expected;
  rejection.actual_value = actual;
  rejection.detail = std::move(detail);
  return CanonicalizeCandidateRejectionV1(std::move(rejection));
}

[[nodiscard]] UWide PolicyResourceEntryCount(
    const routing::CandidateGenerationPolicy& policy) noexcept {
  return static_cast<UWide>(policy.banned_resources.size()) +
         static_cast<UWide>(policy.resource_penalties.size());
}

[[nodiscard]] CandidateRejection PolicyShapeTransactionRejection(
    const CandidateAssociations& associations, UWide actual_count) {
  const std::optional<std::uint64_t> actual =
      actual_count <= std::numeric_limits<std::uint64_t>::max()
          ? std::optional<std::uint64_t>(static_cast<std::uint64_t>(actual_count))
          : std::nullopt;
  return TransactionRejection(
      associations, CandidateRejectionCode::kInvalidInput,
      "candidate.store.transaction.policy_resource_entry_count.v1",
      "Admission transaction contains a policy outside the schema-v1 resource-entry bound",
      routing::kMaximumPolicyResourceEntries, actual);
}

struct CandidateTransactionShapeSummary {
  bool invalid_policy_shape = false;
  UWide maximum_invalid_policy_entries = 0;
  std::size_t maximum_geometry_count = 0;
  std::size_t maximum_resource_count = 0;
  std::size_t maximum_device_class_bytes = 0;
};

void AccumulatePolicyShape(const routing::CandidateGenerationPolicy& policy,
                           CandidateTransactionShapeSummary& summary) noexcept {
  if (!routing::CandidateGenerationPolicyShapeIsWithinV1Bounds(policy)) {
    summary.invalid_policy_shape = true;
    summary.maximum_invalid_policy_entries =
        std::max(summary.maximum_invalid_policy_entries, PolicyResourceEntryCount(policy));
  }
}

void AccumulateCandidateShape(const GeneratedRouteCandidate& candidate,
                              CandidateTransactionShapeSummary& summary) noexcept {
  AccumulatePolicyShape(candidate.policy, summary);
  summary.maximum_geometry_count =
      std::max(summary.maximum_geometry_count, candidate.geometry.size());
  summary.maximum_resource_count =
      std::max(summary.maximum_resource_count, candidate.resources.size());
  summary.maximum_device_class_bytes = std::max(summary.maximum_device_class_bytes,
                                                candidate.provenance.supported_device_class.size());
}

[[nodiscard]] std::optional<CandidateRejection> CandidateShapeTransactionRejection(
    const CandidateAssociations& associations, const CandidateTransactionShapeSummary& summary) {
  // Fixed shape-failure precedence is policy, geometry, resource footprint,
  // then device-class bytes. Within one field the maximum invalid count is
  // reported, so input permutation cannot select a different diagnostic.
  if (summary.invalid_policy_shape) {
    return PolicyShapeTransactionRejection(associations, summary.maximum_invalid_policy_entries);
  }
  if (summary.maximum_geometry_count > kMaximumCandidatePrimitives) {
    return TransactionRejection(
        associations, CandidateRejectionCode::kInvalidInput,
        "candidate.store.transaction.geometry_primitive_count.v1",
        "Admission transaction contains geometry outside the schema-v1 primitive-count bound",
        kMaximumCandidatePrimitives, static_cast<std::uint64_t>(summary.maximum_geometry_count));
  }
  if (summary.maximum_resource_count > kMaximumCandidateResourceSpans) {
    return TransactionRejection(
        associations, CandidateRejectionCode::kInvalidInput,
        "candidate.store.transaction.resource_span_count.v1",
        "Admission transaction contains a footprint outside the schema-v1 span-count bound",
        kMaximumCandidateResourceSpans, static_cast<std::uint64_t>(summary.maximum_resource_count));
  }
  if (summary.maximum_device_class_bytes > kMaximumCandidateDiagnosticBytes) {
    return TransactionRejection(
        associations, CandidateRejectionCode::kInvalidInput,
        "candidate.store.transaction.device_class_bytes.v1",
        "Admission transaction contains a device class outside the schema-v1 byte bound",
        kMaximumCandidateDiagnosticBytes,
        static_cast<std::uint64_t>(summary.maximum_device_class_bytes));
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<CandidateRejection> PreflightSharedRequestCandidateShapes(
    const CandidateAssociations& associations,
    const routing::CandidateGenerationPolicy& request_policy,
    std::span<const GeneratedRouteCandidate> generated) {
  CandidateTransactionShapeSummary summary;
  AccumulatePolicyShape(request_policy, summary);
  for (const GeneratedRouteCandidate& candidate : generated) {
    AccumulateCandidateShape(candidate, summary);
  }
  return CandidateShapeTransactionRejection(associations, summary);
}

[[nodiscard]] std::optional<CandidateRejection> PreflightAdmissionItemCount(
    const CandidateStoreConfig& config, const CandidateAssociations& associations,
    std::size_t item_count) {
  if (!StoreConfigurationIsValid(config)) {
    return TransactionRejection(
        associations, CandidateRejectionCode::kInvalidInput, "candidate.store.configuration.v1",
        "Candidate store configuration requires positive bounded pool, rejection, and "
        "admission-transaction capacities");
  }
  if (static_cast<UWide>(item_count) > config.maximum_admission_items_per_transaction) {
    return TransactionRejection(associations, CandidateRejectionCode::kBudgetExhausted,
                                "candidate.store.transaction.item_budget.v1",
                                "Admission transaction exceeds the configured input-item budget",
                                config.maximum_admission_items_per_transaction,
                                static_cast<std::uint64_t>(item_count));
  }
  return std::nullopt;
}

[[nodiscard]] bool CheckedAddWork(UWide term, UWide& total) noexcept {
  constexpr UWide kMaximum = ~UWide{0};
  if (term > kMaximum - total) {
    return false;
  }
  total += term;
  return true;
}

[[nodiscard]] bool CheckedMultiply(UWide left, UWide right, UWide& product) noexcept {
  constexpr UWide kMaximum = ~UWide{0};
  if (left != 0 && right > kMaximum / left) {
    return false;
  }
  product = left * right;
  return true;
}

[[nodiscard]] std::optional<UWide> PolicyLogicalBytes(
    const routing::CandidateGenerationPolicy& policy) noexcept {
  constexpr UWide kFixedPolicyBytes = 57;
  constexpr UWide kResourceKeyBytes = 21;
  constexpr UWide kResourcePenaltyBytes = 29;
  UWide bytes = kFixedPolicyBytes;
  if (!CheckedAddWork(static_cast<UWide>(policy.banned_resources.size()) * kResourceKeyBytes,
                      bytes) ||
      !CheckedAddWork(static_cast<UWide>(policy.resource_penalties.size()) * kResourcePenaltyBytes,
                      bytes)) {
    return std::nullopt;
  }
  return bytes;
}

[[nodiscard]] std::optional<CandidateRejection> PreflightSharedRequestPolicyCopies(
    const CandidateStoreConfig& config, const CandidateAssociations& associations,
    const routing::CandidateGenerationPolicy& policy, std::size_t item_count) {
  if (!routing::CandidateGenerationPolicyShapeIsWithinV1Bounds(policy)) {
    return PolicyShapeTransactionRejection(associations, PolicyResourceEntryCount(policy));
  }
  const std::optional<UWide> policy_bytes = PolicyLogicalBytes(policy);
  UWide repeated_policy_bytes = 0;
  const UWide policy_entries =
      static_cast<UWide>(policy.banned_resources.size()) + policy.resource_penalties.size();
  UWide repeated_policy_entries = 0;
  if (!policy_bytes.has_value() ||
      !CheckedMultiply(*policy_bytes, item_count, repeated_policy_bytes)) {
    return TransactionRejection(
        associations, CandidateRejectionCode::kMemoryAccountingOverflow,
        "candidate.store.transaction.input_bytes_overflow.v1",
        "Shared request-policy copy accounting overflowed before admission wrapper construction");
  }
  if (!CheckedMultiply(policy_entries, item_count, repeated_policy_entries)) {
    return TransactionRejection(
        associations, CandidateRejectionCode::kMemoryAccountingOverflow,
        "candidate.store.transaction.work_overflow.v1",
        "Shared request-policy work accounting overflowed before admission wrapper construction");
  }
  if (repeated_policy_bytes > std::numeric_limits<std::uint64_t>::max()) {
    return TransactionRejection(
        associations, CandidateRejectionCode::kMemoryAccountingOverflow,
        "candidate.store.transaction.input_bytes_overflow.v1",
        "Repeated shared request policies exceed the version-1 unsigned 64-bit accounting "
        "domain before wrapper construction");
  }
  const std::uint64_t checked_policy_bytes = static_cast<std::uint64_t>(repeated_policy_bytes);
  if (checked_policy_bytes > config.maximum_admission_input_bytes_per_transaction) {
    return TransactionRejection(
        associations, CandidateRejectionCode::kBudgetExhausted,
        "candidate.store.transaction.input_byte_budget.v1",
        "Repeated shared request policies exceed the configured aggregate input-byte budget "
        "before wrapper construction",
        config.maximum_admission_input_bytes_per_transaction, checked_policy_bytes);
  }
  if (repeated_policy_entries > std::numeric_limits<std::uint64_t>::max()) {
    return TransactionRejection(
        associations, CandidateRejectionCode::kMemoryAccountingOverflow,
        "candidate.store.transaction.work_overflow.v1",
        "Repeated shared request-policy entries exceed the version-1 unsigned 64-bit accounting "
        "domain before wrapper construction");
  }
  const std::uint64_t checked_policy_entries = static_cast<std::uint64_t>(repeated_policy_entries);
  if (checked_policy_entries > config.maximum_admission_work_units_per_transaction) {
    return TransactionRejection(
        associations, CandidateRejectionCode::kBudgetExhausted,
        "candidate.store.transaction.work_budget.v1",
        "Repeated shared request-policy entries exceed the configured work-unit budget before "
        "wrapper construction",
        config.maximum_admission_work_units_per_transaction, checked_policy_entries);
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<CandidateRejection> PreflightAdmissionTransaction(
    const CandidateStoreConfig& config, const board_ir::BoardSnapshot& board,
    const geometry_compiler::CompiledBoard& compiled_board,
    std::span<const CandidateAdmissionItem> items) {
  const CandidateAssociations associations = AssociationsFor(board, compiled_board);
  if (std::optional<CandidateRejection> rejection =
          PreflightAdmissionItemCount(config, associations, items.size());
      rejection.has_value()) {
    return rejection;
  }

  CandidateTransactionShapeSummary shape_summary;
  for (const CandidateAdmissionItem& item : items) {
    AccumulateCandidateShape(item.generated, shape_summary);
    AccumulatePolicyShape(item.request.candidate_policy, shape_summary);
  }
  if (std::optional<CandidateRejection> rejection =
          CandidateShapeTransactionRejection(associations, shape_summary);
      rejection.has_value()) {
    return rejection;
  }

  // O(1)-per-container lower bounds reject oversized vectors before walking
  // their elements. These widths are the minimum canonical v1 encodings of
  // the tagged primitive variants and the fixed resource records.
  constexpr UWide kMinimumPrimitiveBytes = 33;
  constexpr UWide kResourceSpanBytes = 29;
  constexpr UWide kResourceKeyBytes = 21;
  constexpr UWide kResourcePenaltyBytes = 29;
  UWide structural_input_bytes = 0;
  UWide structural_work_units = 0;
  const UWide obstacle_count = board.data().obstacles.size();
  const UWide terminal_count = board.data().terminals.size();
  for (const CandidateAdmissionItem& item : items) {
    const UWide primitive_count = item.generated.geometry.size();
    const UWide resource_count = item.generated.resources.size();
    const UWide banned_count = item.generated.policy.banned_resources.size();
    const UWide penalty_count = item.generated.policy.resource_penalties.size();
    const UWide request_banned_count = item.request.candidate_policy.banned_resources.size();
    const UWide request_penalty_count = item.request.candidate_policy.resource_penalties.size();
    const std::optional<UWide> request_policy_bytes =
        PolicyLogicalBytes(item.request.candidate_policy);
    const UWide primitive_pairs =
        primitive_count < 2 ? 0 : (primitive_count * (primitive_count - 1)) / 2;
    const UWide obstacle_checks = primitive_count * obstacle_count;
    const UWide terminal_checks = primitive_count * terminal_count;
    if (!CheckedAddWork(primitive_count * kMinimumPrimitiveBytes, structural_input_bytes) ||
        !CheckedAddWork(resource_count * kResourceSpanBytes, structural_input_bytes) ||
        !CheckedAddWork(banned_count * kResourceKeyBytes, structural_input_bytes) ||
        !CheckedAddWork(penalty_count * kResourcePenaltyBytes, structural_input_bytes) ||
        !CheckedAddWork(item.generated.provenance.supported_device_class.size(),
                        structural_input_bytes) ||
        !request_policy_bytes.has_value() ||
        !CheckedAddWork(*request_policy_bytes, structural_input_bytes)) {
      return TransactionRejection(associations, CandidateRejectionCode::kMemoryAccountingOverflow,
                                  "candidate.store.transaction.input_bytes_overflow.v1",
                                  "Admission transaction structural input-byte accounting "
                                  "overflowed");
    }
    if (!CheckedAddWork(1, structural_work_units) ||
        !CheckedAddWork(primitive_count, structural_work_units) ||
        !CheckedAddWork(primitive_pairs, structural_work_units) ||
        !CheckedAddWork(obstacle_checks, structural_work_units) ||
        !CheckedAddWork(terminal_checks, structural_work_units) ||
        !CheckedAddWork(resource_count, structural_work_units) ||
        !CheckedAddWork(banned_count, structural_work_units) ||
        !CheckedAddWork(penalty_count, structural_work_units) ||
        !CheckedAddWork(request_banned_count, structural_work_units) ||
        !CheckedAddWork(request_penalty_count, structural_work_units)) {
      return TransactionRejection(associations, CandidateRejectionCode::kMemoryAccountingOverflow,
                                  "candidate.store.transaction.work_overflow.v1",
                                  "Admission transaction structural work-unit accounting "
                                  "overflowed");
    }
  }
  if (structural_input_bytes > std::numeric_limits<std::uint64_t>::max()) {
    return TransactionRejection(
        associations, CandidateRejectionCode::kMemoryAccountingOverflow,
        "candidate.store.transaction.input_bytes_overflow.v1",
        "Admission transaction structural input bytes exceed the version-1 unsigned 64-bit "
        "accounting domain");
  }
  if (structural_input_bytes > config.maximum_admission_input_bytes_per_transaction) {
    return TransactionRejection(
        associations, CandidateRejectionCode::kBudgetExhausted,
        "candidate.store.transaction.input_byte_budget.v1",
        "Admission transaction's structural input lower bound exceeds the configured aggregate "
        "input-byte budget",
        config.maximum_admission_input_bytes_per_transaction);
  }
  if (structural_work_units > std::numeric_limits<std::uint64_t>::max()) {
    return TransactionRejection(
        associations, CandidateRejectionCode::kMemoryAccountingOverflow,
        "candidate.store.transaction.work_overflow.v1",
        "Admission transaction structural work exceeds the version-1 unsigned 64-bit accounting "
        "domain");
  }
  if (structural_work_units > config.maximum_admission_work_units_per_transaction) {
    return TransactionRejection(
        associations, CandidateRejectionCode::kBudgetExhausted,
        "candidate.store.transaction.work_budget.v1",
        "Admission transaction's structural work lower bound exceeds the configured "
        "deterministic work-unit budget",
        config.maximum_admission_work_units_per_transaction);
  }

  UWide input_bytes = 0;
  UWide work_units = structural_work_units;
  for (const CandidateAdmissionItem& item : items) {
    const std::optional<std::uint64_t> candidate_bytes =
        ComputeCandidateLogicalBytes(item.generated);
    if (!candidate_bytes.has_value() || !CheckedAddWork(*candidate_bytes, input_bytes)) {
      return TransactionRejection(associations, CandidateRejectionCode::kMemoryAccountingOverflow,
                                  "candidate.store.transaction.input_bytes_overflow.v1",
                                  "Admission transaction input-byte accounting overflowed");
    }
    const std::optional<UWide> request_policy_bytes =
        PolicyLogicalBytes(item.request.candidate_policy);
    if (!request_policy_bytes.has_value() || !CheckedAddWork(*request_policy_bytes, input_bytes)) {
      return TransactionRejection(
          associations, CandidateRejectionCode::kMemoryAccountingOverflow,
          "candidate.store.transaction.input_bytes_overflow.v1",
          "Admission transaction request-policy input-byte accounting overflowed");
    }
    for (const CandidatePrimitive& primitive : item.generated.geometry) {
      const auto* line = std::get_if<ExactLinePrimitive>(&primitive);
      if (line == nullptr) {
        continue;
      }
      const std::optional<geometry_compiler::LatticeIndex> start =
          geometry_compiler::ExactPointToLatticeIndex(compiled_board.profile(),
                                                      line->centerline.start);
      const std::optional<geometry_compiler::LatticeIndex> end =
          geometry_compiler::ExactPointToLatticeIndex(compiled_board.profile(),
                                                      line->centerline.end);
      if (!start.has_value() || !end.has_value()) {
        continue;
      }
      const Wide delta_x = static_cast<Wide>(end->x) - start->x;
      const Wide delta_y = static_cast<Wide>(end->y) - start->y;
      const UWide x_steps = static_cast<UWide>(delta_x < 0 ? -delta_x : delta_x);
      const UWide y_steps = static_cast<UWide>(delta_y < 0 ? -delta_y : delta_y);
      if (!CheckedAddWork(std::max(x_steps, y_steps), work_units)) {
        return TransactionRejection(
            associations, CandidateRejectionCode::kMemoryAccountingOverflow,
            "candidate.store.transaction.work_overflow.v1",
            "Admission transaction derived lattice-edge work accounting overflowed");
      }
    }
    for (const PhysicalEdgeSpan& span : item.generated.resources) {
      if (!CheckedAddWork(span.edge_count, work_units)) {
        return TransactionRejection(associations, CandidateRejectionCode::kMemoryAccountingOverflow,
                                    "candidate.store.transaction.work_overflow.v1",
                                    "Admission transaction work-unit accounting overflowed");
      }
    }
  }

  if (input_bytes > std::numeric_limits<std::uint64_t>::max()) {
    return TransactionRejection(
        associations, CandidateRejectionCode::kMemoryAccountingOverflow,
        "candidate.store.transaction.input_bytes_overflow.v1",
        "Admission transaction input bytes exceed the version-1 unsigned 64-bit accounting "
        "domain");
  }
  const std::uint64_t checked_input_bytes = static_cast<std::uint64_t>(input_bytes);
  if (checked_input_bytes > config.maximum_admission_input_bytes_per_transaction) {
    return TransactionRejection(
        associations, CandidateRejectionCode::kBudgetExhausted,
        "candidate.store.transaction.input_byte_budget.v1",
        "Admission transaction exceeds the configured aggregate input-byte budget",
        config.maximum_admission_input_bytes_per_transaction, checked_input_bytes);
  }
  if (work_units > std::numeric_limits<std::uint64_t>::max()) {
    return TransactionRejection(
        associations, CandidateRejectionCode::kMemoryAccountingOverflow,
        "candidate.store.transaction.work_overflow.v1",
        "Admission transaction work exceeds the version-1 unsigned 64-bit accounting domain");
  }
  const std::uint64_t checked_work_units = static_cast<std::uint64_t>(work_units);
  if (checked_work_units > config.maximum_admission_work_units_per_transaction) {
    return TransactionRejection(
        associations, CandidateRejectionCode::kBudgetExhausted,
        "candidate.store.transaction.work_budget.v1",
        "Admission transaction exceeds the configured deterministic work-unit budget",
        config.maximum_admission_work_units_per_transaction, checked_work_units);
  }
  return std::nullopt;
}

}  // namespace

bool CandidateRanksBefore(const RouteCandidate& left, const RouteCandidate& right) noexcept {
  const CandidateMetrics& left_metrics = left.data().metrics;
  const CandidateMetrics& right_metrics = right.data().metrics;
  const UWide left_steps =
      static_cast<UWide>(left_metrics.orthogonal_step_count) + left_metrics.diagonal_step_count;
  const UWide right_steps =
      static_cast<UWide>(right_metrics.orthogonal_step_count) + right_metrics.diagonal_step_count;
  const auto left_rank = std::tie(
      left_metrics.intrinsic_base_cost, left_metrics.via_count, left_metrics.bend_count, left_steps,
      left_metrics.axis_aligned_length_dbu, left_metrics.diagonal_projection_dbu,
      left.data().resource_signature, left.data().geometry_signature, left.data().policy_identity,
      left_metrics.scalar_policy_cost, left.id());
  const auto right_rank = std::tie(
      right_metrics.intrinsic_base_cost, right_metrics.via_count, right_metrics.bend_count,
      right_steps, right_metrics.axis_aligned_length_dbu, right_metrics.diagonal_projection_dbu,
      right.data().resource_signature, right.data().geometry_signature,
      right.data().policy_identity, right_metrics.scalar_policy_cost, right.id());
  if (left_rank != right_rank) {
    return left_rank < right_rank;
  }
  if (left.data().payload_checksum != right.data().payload_checksum) {
    return left.data().payload_checksum < right.data().payload_checksum;
  }
  return CandidateCanonicalBefore(left, right);
}

bool CandidateMetricsDominate(const RouteCandidate& left, const RouteCandidate& right) noexcept {
  const CandidateMetrics& a = left.data().metrics;
  const CandidateMetrics& b = right.data().metrics;
  const bool no_worse = a.intrinsic_base_cost <= b.intrinsic_base_cost &&
                        a.orthogonal_step_count <= b.orthogonal_step_count &&
                        a.diagonal_step_count <= b.diagonal_step_count &&
                        a.bend_count <= b.bend_count && a.via_count <= b.via_count;
  const bool strictly_better = a.intrinsic_base_cost < b.intrinsic_base_cost ||
                               a.orthogonal_step_count < b.orthogonal_step_count ||
                               a.diagonal_step_count < b.diagonal_step_count ||
                               a.bend_count < b.bend_count || a.via_count < b.via_count;
  return no_worse && strictly_better;
}

double ResourceJaccardOverlap(const RouteCandidate& left, const RouteCandidate& right) noexcept {
  if (!SameCandidateContext(left, right)) {
    return 0.0;
  }
  const ResourceCounts counts = CountResources(left, right);
  const std::uint64_t union_count = counts.left + counts.right - counts.intersection;
  if (union_count == 0) {
    return 0.0;
  }
  return static_cast<double>(counts.intersection) / static_cast<double>(union_count);
}

double GeometricOverlapRatio(const RouteCandidate& left, const RouteCandidate& right) {
  if (!SameCandidateContext(left, right)) {
    return 0.0;
  }
  std::vector<ProjectedInterval> left_intervals;
  std::vector<ProjectedInterval> right_intervals;
  UWide left_projection = 0;
  UWide right_projection = 0;
  if (!BuildProjectedIntervals(left, left_intervals, left_projection) ||
      !BuildProjectedIntervals(right, right_intervals, right_projection)) {
    return 0.0;
  }
  const UWide denominator = std::min(left_projection, right_projection);
  const UWide shared =
      std::min(SharedProjectedLength(left_intervals, right_intervals), denominator);
  return static_cast<double>(shared) / static_cast<double>(denominator);
}

CandidateStore::CandidateStore(CandidateStoreConfig config) : config_(config) {}

bool CandidateStore::valid() const noexcept { return StoreConfigurationIsValid(config_); }

CandidateStoreAdmissionResult CandidateStore::Admit(const CandidateAdmissionContext& context,
                                                    GeneratedRouteCandidate&& generated) {
  std::vector<GeneratedRouteCandidate> batch;
  batch.push_back(std::move(generated));
  std::vector<CandidateStoreAdmissionResult> results = AdmitBatch(context, std::move(batch));
  return std::move(results.front());
}

std::vector<CandidateStoreAdmissionResult> CandidateStore::AdmitBatch(
    const CandidateAdmissionContext& context, std::vector<GeneratedRouteCandidate>&& generated) {
  const CandidateAssociations associations = AssociationsFor(context.board, context.compiled_board);
  if (std::optional<CandidateRejection> rejection =
          PreflightAdmissionItemCount(config_, associations, generated.size());
      rejection.has_value()) {
    RetainRejection(*rejection);
    return {std::move(*rejection)};
  }
  if (std::optional<CandidateRejection> rejection = PreflightSharedRequestCandidateShapes(
          associations, context.request.candidate_policy, generated);
      rejection.has_value()) {
    RetainRejection(*rejection);
    return {std::move(*rejection)};
  }
  if (std::optional<CandidateRejection> rejection = PreflightSharedRequestPolicyCopies(
          config_, associations, context.request.candidate_policy, generated.size());
      rejection.has_value()) {
    RetainRejection(*rejection);
    return {std::move(*rejection)};
  }
  std::vector<CandidateAdmissionItem> items;
  items.reserve(generated.size());
  for (GeneratedRouteCandidate& candidate : generated) {
    items.push_back(
        CandidateAdmissionItem{.request = context.request, .generated = std::move(candidate)});
  }
  return AdmitBatch(context.board, context.compiled_board, std::move(items));
}

std::vector<CandidateStoreAdmissionResult> CandidateStore::AdmitBatch(
    const board_ir::BoardSnapshot& board, const geometry_compiler::CompiledBoard& compiled_board,
    std::vector<CandidateAdmissionItem>&& items) {
  if (std::optional<CandidateRejection> rejection =
          PreflightAdmissionTransaction(config_, board, compiled_board, items);
      rejection.has_value()) {
    RetainRejection(*rejection);
    return {std::move(*rejection)};
  }

  std::vector<CandidateAdmissionResult> admitted;
  admitted.reserve(items.size());
  for (CandidateAdmissionItem& item : items) {
    const CandidateAdmissionContext context{
        .board = board, .compiled_board = compiled_board, .request = item.request};
    admitted.push_back(AdmitRouteCandidate(context, std::move(item.generated)));
  }

  std::vector<CandidateStoreAdmissionResult> results;
  results.reserve(admitted.size());
  std::vector<RouteCandidate> accepted;
  accepted.reserve(admitted.size());
  for (CandidateAdmissionResult& result : admitted) {
    if (auto* rejection = std::get_if<CandidateRejection>(&result); rejection != nullptr) {
      results.emplace_back(std::move(*rejection));
    } else {
      accepted.push_back(std::get<RouteCandidate>(std::move(result)));
    }
  }
  std::ranges::sort(accepted, CandidateTotalBefore);

  std::scoped_lock lock(mutex_);
  for (CandidateStoreAdmissionResult& result : results) {
    RetainRejectionLocked(std::get<CandidateRejection>(result));
  }
  std::vector<CandidateStoreAdmissionResult> publication_results =
      PublishAcceptedBatchLocked(std::move(accepted));
  results.insert(results.end(), std::make_move_iterator(publication_results.begin()),
                 std::make_move_iterator(publication_results.end()));
  std::ranges::sort(results, StoreResultBefore);
  return results;
}

std::vector<StoredCandidate> CandidateStore::Enumerate(board_ir::EntityRef net) const {
  std::scoped_lock lock(mutex_);
  const auto pool = pools_.find(net);
  if (pool == pools_.end()) {
    return {};
  }
  return pool->second;
}

std::vector<CandidateRejection> CandidateStore::Rejections() const {
  std::scoped_lock lock(mutex_);
  return rejections_;
}

std::optional<std::uint64_t> CandidateStore::CandidateBytes(board_ir::EntityRef net) const {
  std::scoped_lock lock(mutex_);
  const auto pool = pools_.find(net);
  if (pool == pools_.end()) {
    return 0;
  }
  std::uint64_t bytes = 0;
  for (const StoredCandidate& candidate : pool->second) {
    if (candidate->logical_bytes() > std::numeric_limits<std::uint64_t>::max() - bytes) {
      return std::nullopt;
    }
    bytes += candidate->logical_bytes();
  }
  return bytes;
}

void CandidateStore::RetainRejection(const CandidateRejection& rejection) {
  std::scoped_lock lock(mutex_);
  RetainRejectionLocked(rejection);
}

std::optional<CandidateRejection> CandidateStore::RetainRejections(
    std::span<const CandidateRejection> rejections) {
  if (static_cast<UWide>(rejections.size()) > config_.maximum_rejection_items_per_transaction) {
    const std::optional<std::uint64_t> actual_count =
        static_cast<UWide>(rejections.size()) <= std::numeric_limits<std::uint64_t>::max()
            ? std::optional<std::uint64_t>(static_cast<std::uint64_t>(rejections.size()))
            : std::nullopt;
    const CandidateRejection over_cap = TransactionRejection(
        CandidateAssociations{}, CandidateRejectionCode::kBudgetExhausted,
        "candidate.store.rejection_transaction.item_budget.v1",
        "Rejection-ingestion transaction exceeds the configured input-item budget",
        config_.maximum_rejection_items_per_transaction, actual_count);
    std::scoped_lock lock(mutex_);
    RetainRejectionLocked(over_cap);
    return over_cap;
  }
  std::scoped_lock lock(mutex_);
  for (const CandidateRejection& rejection : rejections) {
    rejections_.push_back(CanonicalizeCandidateRejectionV1(rejection));
  }
  std::ranges::sort(rejections_, RejectionBefore);
  if (rejections_.size() > config_.maximum_rejection_records) {
    rejections_.resize(static_cast<std::size_t>(config_.maximum_rejection_records));
  }
  return std::nullopt;
}

std::optional<CandidateStoreError> CandidateStore::Pin(std::uint64_t owner_id,
                                                       CandidateId candidate_id) {
  if (owner_id == 0) {
    return CandidateStoreError{.code = CandidateStoreErrorCode::kInvalidPinOwner,
                               .detail = "Retention-pin owner identity must be nonzero"};
  }
  std::scoped_lock lock(mutex_);
  if (!candidate_id_index_.contains(candidate_id)) {
    return CandidateStoreError{.code = CandidateStoreErrorCode::kMissingCandidate,
                               .detail = "Retention pin names an absent candidate"};
  }
  const PinKey key{owner_id, candidate_id};
  if (pins_.insert(key).second) {
    ++pin_counts_[candidate_id];
  }
  return std::nullopt;
}

std::optional<CandidateStoreError> CandidateStore::Unpin(std::uint64_t owner_id,
                                                         CandidateId candidate_id) {
  if (owner_id == 0) {
    return CandidateStoreError{.code = CandidateStoreErrorCode::kInvalidPinOwner,
                               .detail = "Retention-pin owner identity must be nonzero"};
  }
  std::scoped_lock lock(mutex_);
  const PinKey key{owner_id, candidate_id};
  if (pins_.erase(key) == 0) {
    return CandidateStoreError{.code = CandidateStoreErrorCode::kMissingCandidate,
                               .detail = "Retention pin owner/candidate pair is absent"};
  }
  auto count = pin_counts_.find(candidate_id);
  if (count != pin_counts_.end() && --count->second == 0) {
    pin_counts_.erase(count);
  }
  return std::nullopt;
}

bool CandidateStore::IsPinned(CandidateId candidate_id) const {
  std::scoped_lock lock(mutex_);
  return IsPinnedLocked(candidate_id);
}

bool CandidateStore::IsPinnedLocked(CandidateId candidate_id) const {
  return pin_counts_.contains(candidate_id);
}

void CandidateStore::RetainRejectionLocked(const CandidateRejection& rejection) {
  rejections_.push_back(CanonicalizeCandidateRejectionV1(rejection));
  std::ranges::sort(rejections_, RejectionBefore);
  if (rejections_.size() > config_.maximum_rejection_records) {
    rejections_.resize(static_cast<std::size_t>(config_.maximum_rejection_records));
  }
}

CandidateStoreAdmissionResult CandidateStore::PublishAcceptedLocked(RouteCandidate candidate) {
  std::vector<RouteCandidate> candidates;
  candidates.push_back(std::move(candidate));
  std::vector<CandidateStoreAdmissionResult> results =
      PublishAcceptedBatchLocked(std::move(candidates));
  return std::move(results.front());
}

std::vector<CandidateStoreAdmissionResult> CandidateStore::PublishAcceptedBatchLocked(
    std::vector<RouteCandidate> candidates) {
  last_publication_candidate_inspections_ = 0;
  std::vector<CandidateStoreAdmissionResult> results;
  results.reserve(candidates.size());
  std::ranges::sort(candidates, CandidateTotalBefore);

  std::vector<StoredCandidate> eligible;
  eligible.reserve(candidates.size());
  for (RouteCandidate& candidate : candidates) {
    ++last_publication_candidate_inspections_;
    std::optional<CandidateRejection> rejection;
    if (!valid()) {
      rejection = StoreRejection(
          candidate, CandidateRejectionCode::kInvalidInput, "candidate.store.configuration.v1",
          "Candidate store configuration requires positive bounded pool, rejection, and "
          "admission-transaction capacities");
      rejection->stage = CandidateLifecycleStage::kStored;
    } else if (bound_associations_.has_value() &&
               *bound_associations_ != candidate.data().associations) {
      rejection = StoreRejection(
          candidate, CandidateRejectionCode::kAssociationMismatch,
          "candidate.store.association_drift.v1",
          "Candidate associations differ from this store's immutable session binding");
      rejection->stage = CandidateLifecycleStage::kStored;
    }
    if (rejection.has_value()) {
      RetainRejectionLocked(*rejection);
      results.emplace_back(std::move(*rejection));
      continue;
    }
    eligible.push_back(std::make_shared<const RouteCandidate>(std::move(candidate)));
  }

  // Candidate identity is global even though geometry/resource deduplication
  // and retention are per-net. Stable sort selects one incoming identity
  // representative without consulting unrelated pool contents.
  std::ranges::sort(eligible, CandidatePointerRanksBefore);
  std::map<CandidateId, StoredCandidate> incoming_id_winners;
  std::map<board_ir::EntityRef, CandidatePool, NetLess> incoming_by_net;
  for (const StoredCandidate& candidate : eligible) {
    ++last_publication_candidate_inspections_;
    const auto incumbent = candidate_id_index_.find(candidate->id());
    if (incumbent != candidate_id_index_.end()) {
      CandidateRejection rejection = StoreRejection(
          *candidate, CandidateRejectionCode::kDuplicateIdentity, "candidate.store.duplicate_id.v1",
          incumbent->second->net() == candidate->net()
              ? "Candidate identity is already owned by an immutable candidate in this net"
              : "Candidate identity is already owned by another net in the global stable ID "
                "index");
      RetainRejectionLocked(rejection);
      results.emplace_back(std::move(rejection));
      continue;
    }
    const auto [winner, inserted] = incoming_id_winners.emplace(candidate->id(), candidate);
    if (!inserted) {
      CandidateRejection rejection = StoreRejection(
          *candidate, CandidateRejectionCode::kDuplicateIdentity, "candidate.store.duplicate_id.v1",
          "Candidate is not the preferred stable duplicate identity representative");
      RetainRejectionLocked(rejection);
      results.emplace_back(std::move(rejection));
      continue;
    }
    incoming_by_net[candidate->net()].push_back(candidate);
  }

  struct BatchEntry {
    StoredCandidate candidate;
    bool incoming = false;
  };
  struct StagedPool {
    CandidatePool retained;
    std::vector<CandidateRejection> rejections;
    std::vector<CandidateStoreAdmissionResult> results;
    std::vector<StoredCandidate> incoming;
  };
  std::map<board_ir::EntityRef, StagedPool, NetLess> staged;
  bool pinned_budget_failure = false;
  for (auto& [net, incoming] : incoming_by_net) {
    std::vector<BatchEntry> entries;
    const auto incumbent_pool = pools_.find(net);
    const std::size_t incumbent_count =
        incumbent_pool == pools_.end() ? 0 : incumbent_pool->second.size();
    entries.reserve(incumbent_count + incoming.size());
    if (incumbent_pool != pools_.end()) {
      for (const StoredCandidate& candidate : incumbent_pool->second) {
        entries.push_back(BatchEntry{.candidate = candidate});
      }
    }
    for (const StoredCandidate& candidate : incoming) {
      entries.push_back(BatchEntry{.candidate = candidate, .incoming = true});
    }
    last_publication_candidate_inspections_ += entries.size();

    const auto entry_is_pinned = [this, &entries](std::size_t index) {
      return !entries[index].incoming && IsPinnedLocked(entries[index].candidate->id());
    };
    std::vector<std::size_t> order(entries.size());
    for (std::size_t index = 0; index < order.size(); ++index) {
      order[index] = index;
    }
    std::ranges::sort(order, [&entries, &entry_is_pinned](std::size_t left, std::size_t right) {
      const bool left_pinned = entry_is_pinned(left);
      const bool right_pinned = entry_is_pinned(right);
      if (left_pinned != right_pinned) {
        return left_pinned;
      }
      if (CandidateTotalBefore(*entries[left].candidate, *entries[right].candidate)) {
        return true;
      }
      if (CandidateTotalBefore(*entries[right].candidate, *entries[left].candidate)) {
        return false;
      }
      if (entries[left].incoming != entries[right].incoming) {
        return !entries[left].incoming;
      }
      return false;
    });

    std::vector<bool> winner(entries.size(), false);
    std::vector<std::optional<std::size_t>> conflict_winner(entries.size());
    std::vector<std::optional<DuplicateRelation>> conflict_relation(entries.size());
    std::vector<std::size_t> duplicate_winners;
    for (const std::size_t index : order) {
      std::optional<std::size_t> conflict;
      std::optional<DuplicateRelation> relation;
      for (const std::size_t winner_index : duplicate_winners) {
        ++last_publication_candidate_inspections_;
        relation =
            DuplicateRelationBetween(*entries[index].candidate, *entries[winner_index].candidate);
        if (relation.has_value()) {
          conflict = winner_index;
          break;
        }
      }
      if (conflict.has_value() && !entry_is_pinned(index)) {
        conflict_winner[index] = *conflict;
        conflict_relation[index] = *relation;
        continue;
      }
      winner[index] = true;
      duplicate_winners.push_back(index);
    }

    CandidatePool winner_pool;
    winner_pool.reserve(duplicate_winners.size());
    for (const std::size_t index : duplicate_winners) {
      winner_pool.push_back(entries[index].candidate);
    }
    RetentionSelection selection = SelectRetention(std::move(winner_pool), config_, pin_counts_,
                                                   &last_publication_candidate_inspections_);
    if (selection.pinned_budget_failure) {
      pinned_budget_failure = true;
      break;
    }

    StagedPool& change = staged[net];
    change.retained = std::move(selection.retained);
    change.incoming = incoming;
    for (std::size_t index = 0; index < entries.size(); ++index) {
      const BatchEntry& entry = entries[index];
      const bool retained = winner[index] && ContainsCandidate(change.retained, entry.candidate);
      if (retained) {
        if (entry.incoming) {
          change.results.emplace_back(entry.candidate);
        }
        continue;
      }

      CandidateRejection rejection;
      if (conflict_winner[index].has_value()) {
        const std::size_t representative = *conflict_winner[index];
        rejection = StoreRejection(
            *entry.candidate, conflict_relation[index]->code, conflict_relation[index]->invariant,
            entry_is_pinned(representative)
                ? "Candidate duplicates a retained pinned representative"
                : "Candidate is not the preferred stable duplicate representative");
      } else {
        rejection = BudgetRejection(
            *entry.candidate,
            "Candidate was outside deterministic pinned/Pareto/resource/rank retention capacity; "
            "a lower-ranked duplicate is never promoted to fit bytes");
      }
      if (entry.incoming) {
        change.results.emplace_back(rejection);
      }
      change.rejections.emplace_back(std::move(rejection));
    }
  }

  if (pinned_budget_failure) {
    for (const auto& [net, incoming] : incoming_by_net) {
      static_cast<void>(net);
      for (const StoredCandidate& candidate : incoming) {
        CandidateRejection rejection = BudgetRejection(
            *candidate,
            "Admission transaction rolled back because a touched pool's pinned candidates "
            "cannot fit its configured retention budget");
        RetainRejectionLocked(rejection);
        results.emplace_back(std::move(rejection));
      }
    }
    std::ranges::sort(results, StoreResultBefore);
    return results;
  }

  // Commit every touched pool and the global ID index as one mutex-protected
  // publication. Untouched pools are neither copied nor recomputed.
  for (const auto& [net, change] : staged) {
    static_cast<void>(change);
    const auto incumbent = pools_.find(net);
    if (incumbent != pools_.end()) {
      for (const StoredCandidate& candidate : incumbent->second) {
        candidate_id_index_.erase(candidate->id());
      }
    }
  }
  for (auto& [net, change] : staged) {
    if (change.retained.empty()) {
      pools_.erase(net);
    } else {
      std::ranges::sort(change.retained, CandidatePointerRanksBefore);
      pools_[net] = std::move(change.retained);
      for (const StoredCandidate& candidate : pools_[net]) {
        candidate_id_index_.emplace(candidate->id(), candidate);
      }
    }
    for (CandidateRejection& rejection : change.rejections) {
      RetainRejectionLocked(std::move(rejection));
    }
    results.insert(results.end(), std::make_move_iterator(change.results.begin()),
                   std::make_move_iterator(change.results.end()));
  }
  if (!bound_associations_.has_value() && !candidate_id_index_.empty()) {
    bound_associations_ = candidate_id_index_.begin()->second->data().associations;
  }
  std::ranges::sort(results, StoreResultBefore);
  return results;
}

std::optional<CandidateRejection> CandidateStore::Prune(board_ir::EntityRef net) {
  std::scoped_lock lock(mutex_);
  return PruneLocked(net, CandidateId{});
}

std::optional<CandidateRejection> CandidateStore::PruneLocked(board_ir::EntityRef net,
                                                              CandidateId newest_id) {
  last_publication_candidate_inspections_ = 0;
  const auto current = pools_.find(net);
  if (current == pools_.end()) {
    return std::nullopt;
  }
  CandidatePool pool = current->second;
  last_publication_candidate_inspections_ += pool.size();
  if (!valid()) {
    CandidateRejection rejection = StoreRejection(
        *pool.front(), CandidateRejectionCode::kInvalidInput, "candidate.store.configuration.v1",
        "Cannot prune with an invalid candidate-store configuration");
    rejection.stage = CandidateLifecycleStage::kStored;
    RetainRejectionLocked(rejection);
    return rejection;
  }
  const RetentionSelection selection =
      SelectRetention(pool, config_, pin_counts_, &last_publication_candidate_inspections_);
  if (selection.pinned_budget_failure) {
    CandidateRejection rejection =
        BudgetRejection(*pool.front(), "Pinned candidates cannot fit the configured prune budget");
    if (!newest_id.empty()) {
      rejection.candidate_id = newest_id;
    }
    RetainRejectionLocked(rejection);
    return rejection;
  }
  if (selection.pruned.empty()) {
    return std::nullopt;
  }

  for (const StoredCandidate& candidate : current->second) {
    candidate_id_index_.erase(candidate->id());
  }
  if (selection.retained.empty()) {
    pools_.erase(current);
  } else {
    pools_[net] = selection.retained;
    for (const StoredCandidate& candidate : selection.retained) {
      candidate_id_index_.emplace(candidate->id(), candidate);
    }
  }
  std::optional<CandidateRejection> first;
  for (const StoredCandidate& candidate : selection.pruned) {
    CandidateRejection rejection =
        BudgetRejection(*candidate, "Candidate was removed by explicit deterministic pruning");
    if (!first.has_value()) {
      first = rejection;
    }
    RetainRejectionLocked(std::move(rejection));
  }
  return first;
}

}  // namespace apgar::candidates
