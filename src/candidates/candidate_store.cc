#if defined(APGAR_CANDIDATE_STORE_PINNED_ROLLBACK_TEST_VARIANT)
#include "tests/support/candidate_store_test_overlay.h"
#else
#include "apgar/candidates/candidate_store.h"
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <new>
#include <optional>
#include <ranges>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

#include "src/candidates/candidate_store_internal.h"
#include "src/candidates/route_candidate_internal.h"

namespace apgar::candidates {

struct CandidateStorePinLease::Control {
  std::mutex mutex;
  CandidateStore* store = nullptr;
};

CandidateStorePinLease::CandidateStorePinLease(CandidateStorePinLease&& other) noexcept
    : control_(std::move(other.control_)), lease_id_(std::exchange(other.lease_id_, 0)) {}

CandidateStorePinLease& CandidateStorePinLease::operator=(CandidateStorePinLease&& other) noexcept {
  if (this != &other) {
    Release();
    control_ = std::move(other.control_);
    lease_id_ = std::exchange(other.lease_id_, 0);
  }
  return *this;
}

CandidateStorePinLease::~CandidateStorePinLease() { Release(); }

bool CandidateStorePinLease::active() const noexcept {
  const std::shared_ptr<Control> control = control_;
  if (control == nullptr || lease_id_ == 0) {
    return false;
  }
  std::scoped_lock lock(control->mutex);
  return control->store != nullptr;
}

bool CandidateStorePinLease::belongs_to(const CandidateStore& store) const noexcept {
  const std::shared_ptr<Control> control = control_;
  if (control == nullptr || lease_id_ == 0) {
    return false;
  }
  std::scoped_lock lock(control->mutex);
  return control->store == &store;
}

void CandidateStorePinLease::Release() noexcept {
  std::shared_ptr<Control> control = std::move(control_);
  const std::uint64_t lease_id = std::exchange(lease_id_, 0);
  if (control == nullptr) {
    return;
  }
  std::scoped_lock lock(control->mutex);
  if (control->store != nullptr) {
    control->store->ReleasePinLease(lease_id);
  }
}

namespace {

using routing::EdgeResourceKey;
using Wide = __int128_t;
using UWide = __uint128_t;

thread_local std::optional<std::uint64_t> g_publication_preparation_failure_countdown;
#if defined(APGAR_CANDIDATE_STORE_PINNED_ROLLBACK_TEST_VARIANT)
thread_local std::optional<board_ir::EntityRef> g_forced_pinned_rollback_net;
std::atomic<internal::SharedRequestPrePublicationHookForTesting>
    g_shared_request_pre_publication_hook{nullptr};
std::atomic<void*> g_shared_request_pre_publication_hook_context{nullptr};
#endif

void MaybeFailPublicationPreparationForTesting() {
  if (!g_publication_preparation_failure_countdown.has_value()) {
    return;
  }
  if (*g_publication_preparation_failure_countdown == 0) {
    g_publication_preparation_failure_countdown.reset();
    throw std::bad_alloc();
  }
  --*g_publication_preparation_failure_countdown;
}

[[nodiscard]] auto NetKey(board_ir::EntityRef net) noexcept {
  return std::tuple{net.id, net.generation};
}

[[nodiscard]] bool SameCandidateContext(const RouteCandidate& left,
                                        const RouteCandidate& right) noexcept {
  return left.net() == right.net() && left.data().associations == right.data().associations;
}

[[nodiscard]] bool SameStoreSession(const CandidateAssociations& left,
                                    const CandidateAssociations& right) noexcept {
  return left.board_content_hash == right.board_content_hash &&
         left.compiler_profile_fingerprint == right.compiler_profile_fingerprint &&
         left.geometry_compiler_version == right.geometry_compiler_version;
}

[[nodiscard]] bool RejectionBefore(const CandidateRejection& left,
                                   const CandidateRejection& right) noexcept {
  const auto key = [](const CandidateRejection& rejection) {
    const board_ir::EntityRef net = rejection.net.value_or(board_ir::EntityRef{});
    const CandidateId id = rejection.candidate_id.value_or(CandidateId{});
    const board_ir::EntityRef conflict =
        rejection.conflicting_entity.value_or(board_ir::EntityRef{});
    return std::tuple{rejection.schema_version,
                      rejection.candidate_id.has_value(),
                      id,
                      rejection.net.has_value(),
                      net.id,
                      net.generation,
                      rejection.stage,
                      rejection.code,
                      rejection.invariant_id.size(),
                      std::string_view(rejection.invariant_id),
                      rejection.associations.board_content_hash,
                      rejection.associations.compiler_profile_fingerprint,
                      rejection.associations.geometry_compiler_version,
                      rejection.associations.routing_profile_fingerprint,
                      rejection.associations.rule_bucket_identity,
                      rejection.policy_identity,
                      rejection.provenance.generator,
                      rejection.provenance.generator_version,
                      rejection.provenance.backend,
                      rejection.provenance.supported_device_class.size(),
                      std::string_view(rejection.provenance.supported_device_class),
                      rejection.provenance.deterministic_seed,
                      rejection.provenance.batch_identity,
                      rejection.provenance.query_identity,
                      rejection.provenance.candidate_ordinal,
                      rejection.primitive_witness_index,
                      rejection.resource_witness_index,
                      rejection.expected_value,
                      rejection.actual_value,
                      rejection.conflicting_entity.has_value(),
                      conflict.id,
                      conflict.generation,
                      rejection.candidate_payload_checksum,
                      rejection.detail.size(),
                      std::string_view(rejection.detail),
                      rejection.logical_bytes};
  };
  return key(left) < key(right);
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
  const auto header_key = [](const GeneratedRouteCandidate& candidate) {
    return std::tuple{candidate.schema_major,
                      candidate.schema_minor,
                      candidate.id,
                      candidate.net.id,
                      candidate.net.generation,
                      candidate.intended_terminals[0].id,
                      candidate.intended_terminals[0].generation,
                      candidate.intended_terminals[1].id,
                      candidate.intended_terminals[1].generation,
                      candidate.associations.board_content_hash,
                      candidate.associations.compiler_profile_fingerprint,
                      candidate.associations.geometry_compiler_version,
                      candidate.associations.routing_profile_fingerprint,
                      candidate.associations.rule_bucket_identity,
                      candidate.geometry_schema_version,
                      candidate.resource_schema_version};
  };
  const auto a_header = header_key(a);
  const auto b_header = header_key(b);
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
  const auto tail_key = [](const GeneratedRouteCandidate& candidate) {
    return std::tuple{candidate.metrics.scalar_policy_cost,
                      candidate.metrics.intrinsic_base_cost,
                      candidate.metrics.orthogonal_step_count,
                      candidate.metrics.diagonal_step_count,
                      candidate.metrics.bend_count,
                      candidate.metrics.line_primitive_count,
                      candidate.metrics.via_count,
                      candidate.metrics.axis_aligned_length_dbu,
                      candidate.metrics.diagonal_projection_dbu,
                      candidate.constraints.supported_hard_constraints_satisfied,
                      candidate.constraints.unsupported_rules_remain,
                      candidate.constraints.connected_intended_terminal_count,
                      candidate.constraints.exact_validation_code,
                      candidate.geometry_signature,
                      candidate.resource_signature,
                      candidate.payload_checksum,
                      candidate.logical_bytes};
  };
  const auto a_tail = tail_key(a);
  const auto b_tail = tail_key(b);
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

[[nodiscard]] std::optional<std::uint64_t> QuantizeOverlapRatioPpm(UWide numerator,
                                                                   UWide denominator) noexcept {
  constexpr UWide kMaximumUWide = ~UWide{0};
  constexpr UWide kPartsPerMillion = 1'000'000;
  if (denominator == 0 || numerator > denominator ||
      numerator > (kMaximumUWide - denominator / 2) / kPartsPerMillion) {
    return std::nullopt;
  }
  const UWide rounded = (numerator * kPartsPerMillion + denominator / 2) / denominator;
  if (rounded > kPartsPerMillion) {
    return std::nullopt;
  }
  return static_cast<std::uint64_t>(rounded);
}

struct RetentionSelectionImpl {
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
  return left.data().resource_signature == right.data().resource_signature &&
         internal::ResourcesEqualWithinSignatureBucket(left, right);
}

[[nodiscard]] RetentionSelectionImpl SelectRetention(
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
  RetentionSelectionImpl selection;
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
         config.maximum_admission_work_units_per_transaction > 0 &&
         config.maximum_pin_lease_items_per_transaction > 0 &&
         config.maximum_pin_lease_items_per_transaction <= kMaximumPinLeaseItemsPerTransaction &&
         config.maximum_expected_pools_per_invocation > 0 &&
         config.maximum_expected_pools_per_invocation <= kMaximumExpectedPoolsPerInvocation &&
         config.maximum_expected_candidates_per_invocation > 0 &&
         config.maximum_expected_candidates_per_invocation <=
             kMaximumExpectedCandidatesPerInvocation;
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

[[nodiscard]] std::optional<CandidateRejection> PreflightSharedRequestPolicyAccounting(
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
        "Shared request-policy logical-byte accounting overflowed before exact admission");
  }
  if (!CheckedMultiply(policy_entries, item_count, repeated_policy_entries)) {
    return TransactionRejection(
        associations, CandidateRejectionCode::kMemoryAccountingOverflow,
        "candidate.store.transaction.work_overflow.v1",
        "Shared request-policy work accounting overflowed before exact admission");
  }
  if (repeated_policy_bytes > std::numeric_limits<std::uint64_t>::max()) {
    return TransactionRejection(
        associations, CandidateRejectionCode::kMemoryAccountingOverflow,
        "candidate.store.transaction.input_bytes_overflow.v1",
        "Repeated shared request-policy logical bytes exceed the version-1 unsigned 64-bit "
        "accounting domain before exact admission");
  }
  const std::uint64_t checked_policy_bytes = static_cast<std::uint64_t>(repeated_policy_bytes);
  if (checked_policy_bytes > config.maximum_admission_input_bytes_per_transaction) {
    return TransactionRejection(
        associations, CandidateRejectionCode::kBudgetExhausted,
        "candidate.store.transaction.input_byte_budget.v1",
        "Repeated shared request-policy logical bytes exceed the configured aggregate "
        "input-byte budget before exact admission",
        config.maximum_admission_input_bytes_per_transaction, checked_policy_bytes);
  }
  if (repeated_policy_entries > std::numeric_limits<std::uint64_t>::max()) {
    return TransactionRejection(
        associations, CandidateRejectionCode::kMemoryAccountingOverflow,
        "candidate.store.transaction.work_overflow.v1",
        "Repeated shared request-policy entries exceed the version-1 unsigned 64-bit accounting "
        "domain before exact admission");
  }
  const std::uint64_t checked_policy_entries = static_cast<std::uint64_t>(repeated_policy_entries);
  if (checked_policy_entries > config.maximum_admission_work_units_per_transaction) {
    return TransactionRejection(
        associations, CandidateRejectionCode::kBudgetExhausted,
        "candidate.store.transaction.work_budget.v1",
        "Repeated shared request-policy entries exceed the configured work-unit budget before "
        "exact admission",
        config.maximum_admission_work_units_per_transaction, checked_policy_entries);
  }
  return std::nullopt;
}

template <typename CompiledAt, typename RequestAt, typename CandidateAt>
[[nodiscard]] std::optional<CandidateRejection> PreflightAdmissionTransaction(
    const CandidateStoreConfig& config, const board_ir::BoardSnapshot& board,
    std::size_t item_count, CompiledAt&& compiled_at, RequestAt&& request_at,
    CandidateAt&& candidate_at) {
  if (std::optional<CandidateRejection> rejection =
          PreflightAdmissionItemCount(config, CandidateAssociations{}, item_count);
      rejection.has_value()) {
    return rejection;
  }
  const CandidateAssociations associations =
      item_count == 0 ? CandidateAssociations{} : AssociationsFor(board, compiled_at(0));

  CandidateTransactionShapeSummary shape_summary;
  for (std::size_t index = 0; index < item_count; ++index) {
    AccumulateCandidateShape(candidate_at(index), shape_summary);
    AccumulatePolicyShape(request_at(index).candidate_policy, shape_summary);
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
  for (std::size_t index = 0; index < item_count; ++index) {
    const GeneratedRouteCandidate& candidate = candidate_at(index);
    const routing::PlanarRouteRequest& request = request_at(index);
    const UWide primitive_count = candidate.geometry.size();
    const UWide resource_count = candidate.resources.size();
    const UWide banned_count = candidate.policy.banned_resources.size();
    const UWide penalty_count = candidate.policy.resource_penalties.size();
    const UWide request_banned_count = request.candidate_policy.banned_resources.size();
    const UWide request_penalty_count = request.candidate_policy.resource_penalties.size();
    const std::optional<UWide> request_policy_bytes = PolicyLogicalBytes(request.candidate_policy);
    const UWide primitive_pairs =
        primitive_count < 2 ? 0 : (primitive_count * (primitive_count - 1)) / 2;
    const UWide obstacle_checks = primitive_count * obstacle_count;
    const UWide terminal_checks = primitive_count * terminal_count;
    if (!CheckedAddWork(primitive_count * kMinimumPrimitiveBytes, structural_input_bytes) ||
        !CheckedAddWork(resource_count * kResourceSpanBytes, structural_input_bytes) ||
        !CheckedAddWork(banned_count * kResourceKeyBytes, structural_input_bytes) ||
        !CheckedAddWork(penalty_count * kResourcePenaltyBytes, structural_input_bytes) ||
        !CheckedAddWork(candidate.provenance.supported_device_class.size(),
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
  for (std::size_t index = 0; index < item_count; ++index) {
    const GeneratedRouteCandidate& candidate = candidate_at(index);
    const routing::PlanarRouteRequest& request = request_at(index);
    const geometry_compiler::CompiledBoard& compiled_board = compiled_at(index);
    const std::optional<std::uint64_t> candidate_bytes = ComputeCandidateLogicalBytes(candidate);
    if (!candidate_bytes.has_value() || !CheckedAddWork(*candidate_bytes, input_bytes)) {
      return TransactionRejection(associations, CandidateRejectionCode::kMemoryAccountingOverflow,
                                  "candidate.store.transaction.input_bytes_overflow.v1",
                                  "Admission transaction input-byte accounting overflowed");
    }
    const std::optional<UWide> request_policy_bytes = PolicyLogicalBytes(request.candidate_policy);
    if (!request_policy_bytes.has_value() || !CheckedAddWork(*request_policy_bytes, input_bytes)) {
      return TransactionRejection(
          associations, CandidateRejectionCode::kMemoryAccountingOverflow,
          "candidate.store.transaction.input_bytes_overflow.v1",
          "Admission transaction request-policy input-byte accounting overflowed");
    }
    for (const CandidatePrimitive& primitive : candidate.geometry) {
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
    for (const PhysicalEdgeSpan& span : candidate.resources) {
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

bool internal::GeometryEqualWithinSignatureBucket(const RouteCandidate& left,
                                                  const RouteCandidate& right) noexcept {
  return SameCandidateContext(left, right) && CanonicalGeometryEqual(left, right);
}

bool internal::ResourcesEqualWithinSignatureBucket(const RouteCandidate& left,
                                                   const RouteCandidate& right) noexcept {
  return SameCandidateContext(left, right) && CanonicalResourcesEqual(left, right);
}

internal::SignatureBucketDuplicate internal::ClassifySignatureBucketDuplicate(
    const RouteCandidate& left, const RouteCandidate& right, bool geometry_bucket_matched,
    bool resource_bucket_matched) noexcept {
  if (geometry_bucket_matched && GeometryEqualWithinSignatureBucket(left, right)) {
    return SignatureBucketDuplicate::kGeometry;
  }
  if (resource_bucket_matched && ResourcesEqualWithinSignatureBucket(left, right)) {
    return SignatureBucketDuplicate::kResources;
  }
  return SignatureBucketDuplicate::kNone;
}

std::strong_ordering internal::CompareTotalStepCount(const CandidateMetrics& left,
                                                     const CandidateMetrics& right) noexcept {
  const UWide left_total =
      static_cast<UWide>(left.orthogonal_step_count) + left.diagonal_step_count;
  const UWide right_total =
      static_cast<UWide>(right.orthogonal_step_count) + right.diagonal_step_count;
  if (left_total < right_total) {
    return std::strong_ordering::less;
  }
  if (left_total > right_total) {
    return std::strong_ordering::greater;
  }
  return std::strong_ordering::equal;
}

std::optional<std::uint64_t> internal::CheckedLogicalByteSum(std::uint64_t accumulated,
                                                             std::uint64_t next) noexcept {
  if (next > std::numeric_limits<std::uint64_t>::max() - accumulated) {
    return std::nullopt;
  }
  return accumulated + next;
}

bool CandidateRanksBefore(const RouteCandidate& left, const RouteCandidate& right) noexcept {
  const CandidateMetrics& left_metrics = left.data().metrics;
  const CandidateMetrics& right_metrics = right.data().metrics;
  const auto left_prefix =
      std::tie(left_metrics.intrinsic_base_cost, left_metrics.via_count, left_metrics.bend_count);
  const auto right_prefix = std::tie(right_metrics.intrinsic_base_cost, right_metrics.via_count,
                                     right_metrics.bend_count);
  if (left_prefix != right_prefix) {
    return left_prefix < right_prefix;
  }
  const std::strong_ordering step_order =
      internal::CompareTotalStepCount(left_metrics, right_metrics);
  if (step_order != std::strong_ordering::equal) {
    return step_order == std::strong_ordering::less;
  }
  const auto left_suffix =
      std::tie(left_metrics.axis_aligned_length_dbu, left_metrics.diagonal_projection_dbu,
               left.data().resource_signature, left.data().geometry_signature,
               left.data().policy_identity, left_metrics.scalar_policy_cost, left.id());
  const auto right_suffix =
      std::tie(right_metrics.axis_aligned_length_dbu, right_metrics.diagonal_projection_dbu,
               right.data().resource_signature, right.data().geometry_signature,
               right.data().policy_identity, right_metrics.scalar_policy_cost, right.id());
  if (left_suffix != right_suffix) {
    return left_suffix < right_suffix;
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

std::optional<std::uint64_t> ResourceJaccardOverlapPpmV1(const RouteCandidate& left,
                                                         const RouteCandidate& right) noexcept {
  if (!SameCandidateContext(left, right)) {
    return 0;
  }
  const ResourceCounts counts = CountResources(left, right);
  const UWide union_count = static_cast<UWide>(counts.left) + counts.right - counts.intersection;
  return QuantizeOverlapRatioPpm(counts.intersection, union_count);
}

std::optional<std::uint64_t> GeometricOverlapRatioPpmV1(const RouteCandidate& left,
                                                        const RouteCandidate& right) {
  if (!SameCandidateContext(left, right)) {
    return 0;
  }
  std::vector<ProjectedInterval> left_intervals;
  std::vector<ProjectedInterval> right_intervals;
  UWide left_projection = 0;
  UWide right_projection = 0;
  if (!BuildProjectedIntervals(left, left_intervals, left_projection) ||
      !BuildProjectedIntervals(right, right_intervals, right_projection)) {
    return 0;
  }
  const UWide denominator = std::min(left_projection, right_projection);
  const UWide shared =
      std::min(SharedProjectedLength(left_intervals, right_intervals), denominator);
  return QuantizeOverlapRatioPpm(shared, denominator);
}

std::optional<std::uint64_t> internal::QuantizeOverlapRatioPpmV1(
    std::uint64_t numerator, std::uint64_t denominator) noexcept {
  return QuantizeOverlapRatioPpm(numerator, denominator);
}

CandidateStore::CandidateStore(CandidateStoreConfig config)
    : config_(config), pin_lease_control_(std::make_shared<CandidateStorePinLease::Control>()) {
  pin_lease_control_->store = this;
}

CandidateStore::~CandidateStore() {
  std::scoped_lock lock(pin_lease_control_->mutex);
  pin_lease_control_->store = nullptr;
}

bool CandidateStore::valid() const noexcept { return StoreConfigurationIsValid(config_); }

CandidateStoreTelemetry CandidateStore::telemetry() const {
  std::scoped_lock lock(mutex_);
  return CandidateStoreTelemetry{
      .last_publication_candidate_inspections = last_publication_candidate_inspections_,
      .last_duplicate_equality_checks = last_duplicate_equality_checks_,
      .rejection_batch_merges = rejection_batch_merges_,
      .shared_request_policy_normalizations = shared_request_policy_normalizations_,
  };
}

internal::RetentionSelection internal::SelectRetentionForPool(
    std::vector<StoredCandidate> pool, const CandidateStoreConfig& config,
    const std::map<CandidateId, std::uint64_t>& pin_counts) {
  RetentionSelectionImpl selected = SelectRetention(std::move(pool), config, pin_counts);
  return internal::RetentionSelection{
      .retained = std::move(selected.retained),
      .pruned = std::move(selected.pruned),
      .pinned_budget_failure = selected.pinned_budget_failure,
  };
}

void internal::SetPublicationPreparationFailureCountdownForTesting(
    std::optional<std::uint64_t> countdown) noexcept {
  g_publication_preparation_failure_countdown = countdown;
}

#if defined(APGAR_CANDIDATE_STORE_PINNED_ROLLBACK_TEST_VARIANT)
void internal::SetSharedRequestPrePublicationHookForTesting(
    SharedRequestPrePublicationHookForTesting hook, void* context) noexcept {
  g_shared_request_pre_publication_hook_context.store(context, std::memory_order_relaxed);
  g_shared_request_pre_publication_hook.store(hook, std::memory_order_release);
}

std::vector<CandidateStoreAdmissionResult> internal::PublishWithPinnedRollbackForTesting(
    CandidateStore& store, std::vector<RouteCandidate> candidates, board_ir::EntityRef pinned_net,
    std::uint64_t shared_request_policy_normalization_delta) {
  g_forced_pinned_rollback_net = pinned_net;
  try {
    std::scoped_lock lock(store.mutex_);
    std::vector<CandidateStoreAdmissionResult> results = store.PublishAcceptedBatchLocked(
        std::move(candidates), {}, shared_request_policy_normalization_delta);
    g_forced_pinned_rollback_net.reset();
    return results;
  } catch (...) {
    g_forced_pinned_rollback_net.reset();
    throw;
  }
}
#endif

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
  if (std::optional<CandidateRejection> rejection = PreflightSharedRequestPolicyAccounting(
          config_, associations, context.request.candidate_policy, generated.size());
      rejection.has_value()) {
    RetainRejection(*rejection);
    return {std::move(*rejection)};
  }
  if (std::optional<CandidateRejection> rejection = PreflightAdmissionTransaction(
          config_, context.board, generated.size(),
          [&context](std::size_t) -> const geometry_compiler::CompiledBoard& {
            return context.compiled_board;
          },
          [&context](std::size_t) -> const routing::PlanarRouteRequest& { return context.request; },
          [&generated](std::size_t index) -> const GeneratedRouteCandidate& {
            return generated[index];
          });
      rejection.has_value()) {
    RetainRejection(*rejection);
    return {std::move(*rejection)};
  }
  if (generated.empty()) {
    return PublishAdmissionResults({});
  }

  // The request is independently untrusted, but it is identical for every
  // item in this overload. Normalize it once, then reuse the immutable result.
  // Each generated policy is still checked independently: exact typed equality
  // uses the verified normalized value, while a differing value is normalized
  // and validated on its own.
  const routing::CandidatePolicyResult verified_request_policy =
      routing::NormalizeCandidateGenerationPolicy(context.compiled_board,
                                                  context.request.candidate_policy);
  std::vector<CandidateAdmissionResult> admitted;
  admitted.reserve(generated.size());
  for (GeneratedRouteCandidate& candidate : generated) {
    admitted.push_back(internal::AdmitRouteCandidateWithVerifiedRequestPolicy(
        context, verified_request_policy, std::move(candidate)));
  }
#if defined(APGAR_CANDIDATE_STORE_PINNED_ROLLBACK_TEST_VARIANT)
  if (const internal::SharedRequestPrePublicationHookForTesting hook =
          g_shared_request_pre_publication_hook.load(std::memory_order_acquire);
      hook != nullptr) {
    hook(g_shared_request_pre_publication_hook_context.load(std::memory_order_relaxed));
  }
#endif
  return PublishAdmissionResults(std::move(admitted), 1);
}

std::vector<CandidateStoreAdmissionResult> CandidateStore::AdmitBatch(
    const board_ir::BoardSnapshot& board, const geometry_compiler::CompiledBoard& compiled_board,
    std::vector<CandidateAdmissionItem>&& items) {
  if (std::optional<CandidateRejection> rejection = PreflightAdmissionTransaction(
          config_, board, items.size(),
          [&compiled_board](std::size_t) -> const geometry_compiler::CompiledBoard& {
            return compiled_board;
          },
          [&items](std::size_t index) -> const routing::PlanarRouteRequest& {
            return items[index].request;
          },
          [&items](std::size_t index) -> const GeneratedRouteCandidate& {
            return items[index].generated;
          });
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

  return PublishAdmissionResults(std::move(admitted));
}

CandidateStoreInvocationAdmissionResult CandidateStore::AdmitInvocationIfSourcePoolsMatch(
    const board_ir::BoardSnapshot& board,
    std::vector<CandidateStoreExpectedPool>&& expected_source_pools,
    std::vector<CandidateInvocationItem>&& items) {
  const auto invocation_error = [](CandidateStoreErrorCode code, std::string detail) {
    return CandidateStoreInvocationAdmissionResult(
        CandidateStoreError{.code = code, .detail = std::move(detail)});
  };
  if (!valid()) {
    return invocation_error(CandidateStoreErrorCode::kInvalidConfiguration,
                            "Cannot admit an invocation into an invalid candidate store");
  }
  if (items.size() > config_.maximum_admission_items_per_transaction) {
    return invocation_error(CandidateStoreErrorCode::kInvocationInputBoundExceeded,
                            "Invocation exceeds the configured input-item bound");
  }
  // Every submitted item can become one rejection. This worst-case check
  // guarantees that exact admission cannot discover a larger rejection batch
  // after mutation has begun.
  if (items.size() > config_.maximum_rejection_items_per_transaction) {
    return invocation_error(CandidateStoreErrorCode::kInvocationInputBoundExceeded,
                            "Invocation exceeds the configured rejection-item bound");
  }
  if (expected_source_pools.empty()) {
    return invocation_error(CandidateStoreErrorCode::kInvalidInvocation,
                            "Invocation must declare its complete expected source-pool roster");
  }
  if (expected_source_pools.size() > config_.maximum_expected_pools_per_invocation) {
    return invocation_error(CandidateStoreErrorCode::kInvocationInputBoundExceeded,
                            "Invocation exceeds the configured expected-pool bound");
  }
  UWide expected_candidate_count = 0;
  for (const CandidateStoreExpectedPool& pool : expected_source_pools) {
    expected_candidate_count += static_cast<UWide>(pool.candidates.size());
  }
  if (expected_candidate_count > config_.maximum_expected_candidates_per_invocation) {
    return invocation_error(CandidateStoreErrorCode::kInvocationInputBoundExceeded,
                            "Invocation exceeds the configured expected-candidate bound");
  }

  try {
    std::ranges::sort(expected_source_pools, [](const CandidateStoreExpectedPool& left,
                                                const CandidateStoreExpectedPool& right) {
      return std::tie(left.net.id, left.net.generation) <
             std::tie(right.net.id, right.net.generation);
    });
    if (std::ranges::adjacent_find(expected_source_pools, {}, &CandidateStoreExpectedPool::net) !=
        expected_source_pools.end()) {
      return invocation_error(CandidateStoreErrorCode::kInvalidInvocation,
                              "Invocation contains a duplicate expected source pool");
    }

    std::set<CandidateId> expected_candidate_ids;
    for (CandidateStoreExpectedPool& pool : expected_source_pools) {
      for (const StoredCandidate& candidate : pool.candidates) {
        if (candidate == nullptr || candidate->net() != pool.net ||
            candidate->data().associations != pool.associations) {
          return invocation_error(
              CandidateStoreErrorCode::kInvalidInvocation,
              "Expected source pool contains a null candidate or a candidate for another net or "
              "association binding");
        }
        if (!expected_candidate_ids.insert(candidate->id()).second) {
          return invocation_error(CandidateStoreErrorCode::kInvalidInvocation,
                                  "Expected source pools contain a duplicate candidate identity");
        }
      }
      std::ranges::sort(pool.candidates, CandidatePointerRanksBefore);
    }

    const auto expected_pool_for =
        [&expected_source_pools](board_ir::EntityRef net) -> const CandidateStoreExpectedPool* {
      const auto found =
          std::ranges::lower_bound(expected_source_pools, std::pair{net.id, net.generation}, {},
                                   [](const CandidateStoreExpectedPool& pool) {
                                     return std::pair{pool.net.id, pool.net.generation};
                                   });
      return found != expected_source_pools.end() && found->net == net ? &*found : nullptr;
    };
    std::vector<CandidateInvocationGeneratedItem*> generated;
    generated.reserve(items.size());
    for (CandidateInvocationItem& item : items) {
      if (auto* draft = std::get_if<CandidateInvocationGeneratedItem>(&item); draft != nullptr) {
        const CandidateStoreExpectedPool* expected = expected_pool_for(draft->request.net);
        if (expected == nullptr) {
          return invocation_error(CandidateStoreErrorCode::kInvalidInvocation,
                                  "Generated invocation item names a net outside the expected "
                                  "source-pool roster");
        }
        if (AssociationsFor(board, draft->compiled_board.get()) != expected->associations) {
          return invocation_error(CandidateStoreErrorCode::kInvalidInvocation,
                                  "Generated invocation item differs from its expected source "
                                  "pool association binding");
        }
        generated.push_back(draft);
      } else {
        const CandidateRejection& rejection = std::get<CandidateRejection>(item);
        const CandidateStoreExpectedPool* expected =
            rejection.net.has_value() ? expected_pool_for(*rejection.net) : nullptr;
        if (expected == nullptr) {
          return invocation_error(CandidateStoreErrorCode::kInvalidInvocation,
                                  "Pre-generation rejection must name a net in the expected "
                                  "source-pool roster");
        }
        if (rejection.associations != expected->associations) {
          return invocation_error(CandidateStoreErrorCode::kInvalidInvocation,
                                  "Pre-generation rejection differs from its expected source "
                                  "pool association binding");
        }
      }
    }

    if (std::optional<CandidateRejection> rejection = PreflightAdmissionTransaction(
            config_, board, generated.size(),
            [&generated](std::size_t index) -> const geometry_compiler::CompiledBoard& {
              return generated[index]->compiled_board.get();
            },
            [&generated](std::size_t index) -> const routing::PlanarRouteRequest& {
              return generated[index]->request;
            },
            [&generated](std::size_t index) -> const GeneratedRouteCandidate& {
              return generated[index]->generated;
            });
        rejection.has_value()) {
      return invocation_error(CandidateStoreErrorCode::kInvocationAdmissionPreflightFailed,
                              rejection->invariant_id + ": " + rejection->detail);
    }

    std::vector<CandidateAdmissionResult> admitted;
    admitted.reserve(items.size());
    for (CandidateInvocationItem& item : items) {
      if (auto* draft = std::get_if<CandidateInvocationGeneratedItem>(&item); draft != nullptr) {
        const CandidateAdmissionContext context{
            .board = board,
            .compiled_board = draft->compiled_board.get(),
            .request = draft->request,
        };
        admitted.push_back(AdmitRouteCandidate(context, std::move(draft->generated)));
      } else {
        admitted.emplace_back(std::get<CandidateRejection>(std::move(item)));
      }
    }
    return PublishConditionalAdmissionResults(std::move(admitted), expected_source_pools);
  } catch (const std::bad_alloc&) {
    return invocation_error(CandidateStoreErrorCode::kResourceExhausted,
                            "Host allocation failed while preparing conditional invocation "
                            "admission");
  } catch (const std::length_error&) {
    return invocation_error(CandidateStoreErrorCode::kResourceExhausted,
                            "Host container limits were exhausted while preparing conditional "
                            "invocation admission");
  }
}

std::vector<CandidateStoreAdmissionResult> CandidateStore::PublishAdmissionResults(
    std::vector<CandidateAdmissionResult> admitted,
    std::uint64_t shared_request_policy_normalization_delta) {
  std::vector<CandidateStoreAdmissionResult> results;
  results.reserve(admitted.size());
  std::vector<RouteCandidate> accepted;
  accepted.reserve(admitted.size());
  std::vector<CandidateRejection> canonical_rejections;
  canonical_rejections.reserve(admitted.size());
  for (CandidateAdmissionResult& result : admitted) {
    if (auto* rejection = std::get_if<CandidateRejection>(&result); rejection != nullptr) {
      CandidateRejection canonical = CanonicalizeCandidateRejectionV1(*rejection);
      canonical_rejections.push_back(canonical);
      results.emplace_back(std::move(canonical));
    } else {
      accepted.push_back(std::get<RouteCandidate>(std::move(result)));
    }
  }
  std::ranges::sort(accepted, CandidateTotalBefore);

  std::scoped_lock lock(mutex_);
  std::vector<CandidateStoreAdmissionResult> publication_results =
      PublishAcceptedBatchLocked(std::move(accepted), std::move(canonical_rejections),
                                 shared_request_policy_normalization_delta);
  results.insert(results.end(), std::make_move_iterator(publication_results.begin()),
                 std::make_move_iterator(publication_results.end()));
  std::ranges::sort(results, StoreResultBefore);
  return results;
}

CandidateStoreInvocationAdmissionResult CandidateStore::PublishConditionalAdmissionResults(
    std::vector<CandidateAdmissionResult> admitted,
    const std::vector<CandidateStoreExpectedPool>& expected_source_pools) {
  std::vector<CandidateStoreAdmissionResult> results;
  results.reserve(admitted.size());
  std::vector<RouteCandidate> accepted;
  accepted.reserve(admitted.size());
  std::vector<CandidateRejection> canonical_rejections;
  canonical_rejections.reserve(admitted.size());
  for (CandidateAdmissionResult& result : admitted) {
    if (auto* rejection = std::get_if<CandidateRejection>(&result); rejection != nullptr) {
      CandidateRejection canonical = CanonicalizeCandidateRejectionV1(*rejection);
      canonical_rejections.push_back(canonical);
      results.emplace_back(std::move(canonical));
    } else {
      accepted.push_back(std::get<RouteCandidate>(std::move(result)));
    }
  }
  std::ranges::sort(accepted, CandidateTotalBefore);

  std::scoped_lock lock(mutex_);
  if (!ExpectedPoolsMatchLocked(expected_source_pools)) {
    return CandidateStoreError{
        .code = CandidateStoreErrorCode::kStoreDrift,
        .detail = "Candidate store source pools changed before invocation publication"};
  }
  if (admitted.empty()) {
    return results;
  }
  std::vector<CandidateStoreAdmissionResult> publication_results =
      PublishAcceptedBatchLocked(std::move(accepted), std::move(canonical_rejections));
  results.insert(results.end(), std::make_move_iterator(publication_results.begin()),
                 std::make_move_iterator(publication_results.end()));
  std::ranges::sort(results, StoreResultBefore);
  return results;
}

bool CandidateStore::ExpectedPoolsMatchLocked(
    const std::vector<CandidateStoreExpectedPool>& expected_source_pools) const noexcept {
  for (const CandidateStoreExpectedPool& expected : expected_source_pools) {
    const auto association = net_associations_.find(expected.net);
    if (association != net_associations_.end()) {
      if (association->second != expected.associations) {
        return false;
      }
    } else {
      if ((bound_associations_.has_value() &&
           !SameStoreSession(*bound_associations_, expected.associations)) ||
          pools_.contains(expected.net) || !expected.candidates.empty()) {
        return false;
      }
    }
    const auto found = pools_.find(expected.net);
    if (found == pools_.end()) {
      if (!expected.candidates.empty()) {
        return false;
      }
      continue;
    }
    const CandidatePool& actual = found->second;
    if (actual.size() != expected.candidates.size()) {
      return false;
    }
    for (std::size_t index = 0; index < actual.size(); ++index) {
      if (actual[index] == nullptr || expected.candidates[index] == nullptr ||
          actual[index]->net() != expected.candidates[index]->net() ||
          actual[index]->data() != expected.candidates[index]->data()) {
        return false;
      }
    }
  }
  return true;
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

std::uint64_t CandidateStore::RejectionCount() const {
  std::scoped_lock lock(mutex_);
  return rejections_.size();
}

std::optional<std::uint64_t> CandidateStore::CandidateBytes(board_ir::EntityRef net) const {
  std::scoped_lock lock(mutex_);
  const auto pool = pools_.find(net);
  if (pool == pools_.end()) {
    return 0;
  }
  std::uint64_t bytes = 0;
  for (const StoredCandidate& candidate : pool->second) {
    const std::optional<std::uint64_t> next =
        internal::CheckedLogicalByteSum(bytes, candidate->logical_bytes());
    if (!next.has_value()) {
      return std::nullopt;
    }
    bytes = *next;
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
  std::vector<CandidateRejection> canonical;
  canonical.reserve(rejections.size());
  for (const CandidateRejection& rejection : rejections) {
    canonical.push_back(CanonicalizeCandidateRejectionV1(rejection));
  }
  std::scoped_lock lock(mutex_);
  RetainCanonicalRejectionsLocked(std::move(canonical));
  return std::nullopt;
}

std::vector<CandidateRejection> CandidateStore::BuildMergedRejectionsLocked(
    std::vector<CandidateRejection> canonical_rejections) {
  std::ranges::sort(canonical_rejections, RejectionBefore);
  const std::size_t maximum_size = std::numeric_limits<std::size_t>::max();
  const std::size_t total_size = canonical_rejections.size() > maximum_size - rejections_.size()
                                     ? maximum_size
                                     : rejections_.size() + canonical_rejections.size();
  const std::size_t configured_size =
      config_.maximum_rejection_records > maximum_size
          ? maximum_size
          : static_cast<std::size_t>(config_.maximum_rejection_records);
  const std::size_t retained_size = std::min(configured_size, total_size);
  std::vector<CandidateRejection> merged;
  merged.reserve(static_cast<std::size_t>(retained_size));
  auto existing = rejections_.begin();
  auto incoming = canonical_rejections.begin();
  while (merged.size() < retained_size &&
         (existing != rejections_.end() || incoming != canonical_rejections.end())) {
    if (existing == rejections_.end()) {
      merged.push_back(std::move(*incoming++));
    } else if (incoming == canonical_rejections.end() || !RejectionBefore(*incoming, *existing)) {
      merged.push_back(*existing++);
    } else {
      merged.push_back(std::move(*incoming++));
    }
  }
  return merged;
}

void CandidateStore::RetainCanonicalRejectionsLocked(
    std::vector<CandidateRejection> canonical_rejections) {
  if (canonical_rejections.empty()) {
    return;
  }
  std::vector<CandidateRejection> merged =
      BuildMergedRejectionsLocked(std::move(canonical_rejections));
  rejections_ = std::move(merged);
  ++rejection_batch_merges_;
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

CandidateStorePinLeaseResult CandidateStore::AcquirePinLease(
    std::span<const CandidatePinRequest> requests) {
  if (!valid()) {
    return CandidateStoreError{.code = CandidateStoreErrorCode::kInvalidConfiguration,
                               .detail = "Cannot acquire a lease from an invalid candidate store"};
  }
  if (requests.empty()) {
    return CandidateStoreError{.code = CandidateStoreErrorCode::kInvalidPinLeaseRequest,
                               .detail = "A candidate pin lease must name at least one candidate"};
  }
  if (requests.size() > config_.maximum_pin_lease_items_per_transaction) {
    return CandidateStoreError{
        .code = CandidateStoreErrorCode::kPinLeaseInputBoundExceeded,
        .detail = "Candidate pin lease exceeds the configured transaction item bound"};
  }

  std::vector<CandidatePinRequest> canonical_requests;
  std::vector<CandidateId> candidate_ids;
  std::vector<CandidateId> inserted_count_records;
  try {
    canonical_requests.assign(requests.begin(), requests.end());
    std::ranges::sort(canonical_requests,
                      [](const CandidatePinRequest& left, const CandidatePinRequest& right) {
                        return std::tie(left.candidate_id, left.net.id, left.net.generation,
                                        left.candidate_payload_checksum) <
                               std::tie(right.candidate_id, right.net.id, right.net.generation,
                                        right.candidate_payload_checksum);
                      });
    const auto duplicate =
        std::ranges::adjacent_find(canonical_requests, {}, &CandidatePinRequest::candidate_id);
    if (duplicate != canonical_requests.end()) {
      return CandidateStoreError{
          .code = CandidateStoreErrorCode::kInvalidPinLeaseRequest,
          .detail = "A candidate pin lease contains a duplicate candidate identity"};
    }
    candidate_ids.reserve(canonical_requests.size());
    inserted_count_records.reserve(canonical_requests.size());
    for (const CandidatePinRequest& request : canonical_requests) {
      candidate_ids.push_back(request.candidate_id);
    }
  } catch (const std::bad_alloc&) {
    return CandidateStoreError{.code = CandidateStoreErrorCode::kResourceExhausted,
                               .detail = "Host allocation failed while preparing a pin lease"};
  } catch (const std::length_error&) {
    return CandidateStoreError{
        .code = CandidateStoreErrorCode::kResourceExhausted,
        .detail = "Host container limits were exhausted while preparing a pin lease"};
  }

  std::scoped_lock lock(mutex_);
  for (const CandidatePinRequest& request : canonical_requests) {
    if (request.expected_candidate == nullptr) {
      return CandidateStoreError{
          .code = CandidateStoreErrorCode::kInvalidPinLeaseRequest,
          .detail = "Candidate pin lease omitted its exact immutable candidate"};
    }
    const auto stored = candidate_id_index_.find(request.candidate_id);
    if (stored == candidate_id_index_.end()) {
      return CandidateStoreError{.code = CandidateStoreErrorCode::kMissingCandidate,
                                 .detail = "Candidate pin lease names an absent candidate"};
    }
    if (stored->second->net() != request.net) {
      return CandidateStoreError{
          .code = CandidateStoreErrorCode::kCandidateNetMismatch,
          .detail = "Candidate pin lease net does not match the stored candidate"};
    }
    if (stored->second->data().payload_checksum != request.candidate_payload_checksum) {
      return CandidateStoreError{
          .code = CandidateStoreErrorCode::kCandidatePayloadMismatch,
          .detail = "Candidate pin lease payload checksum does not match the stored candidate"};
    }
    if (stored->second->net() != request.expected_candidate->net() ||
        stored->second->data() != request.expected_candidate->data()) {
      return CandidateStoreError{
          .code = CandidateStoreErrorCode::kCandidateSemanticMismatch,
          .detail = "Candidate pin lease immutable value differs from the stored candidate"};
    }
    const auto count = pin_counts_.find(request.candidate_id);
    if (count != pin_counts_.end() && count->second == std::numeric_limits<std::uint64_t>::max()) {
      return CandidateStoreError{.code = CandidateStoreErrorCode::kPinLeaseIdentityExhausted,
                                 .detail = "Candidate pin reference count is exhausted"};
    }
  }
  if (next_pin_lease_id_ == 0) {
    return CandidateStoreError{.code = CandidateStoreErrorCode::kPinLeaseIdentityExhausted,
                               .detail = "Candidate pin lease identity space is exhausted"};
  }

  const std::uint64_t lease_id = next_pin_lease_id_;
  try {
    for (const CandidateId candidate_id : candidate_ids) {
      const auto [unused, inserted] = pin_counts_.try_emplace(candidate_id, 0);
      static_cast<void>(unused);
      if (inserted) {
        inserted_count_records.push_back(candidate_id);
      }
    }
    const auto [unused, inserted] = pin_leases_.emplace(lease_id, std::move(candidate_ids));
    static_cast<void>(unused);
    if (!inserted) {
      for (const CandidateId candidate_id : inserted_count_records) {
        pin_counts_.erase(candidate_id);
      }
      return CandidateStoreError{.code = CandidateStoreErrorCode::kPinLeaseIdentityExhausted,
                                 .detail = "Candidate pin lease identity was already active"};
    }
  } catch (const std::bad_alloc&) {
    for (const CandidateId candidate_id : inserted_count_records) {
      pin_counts_.erase(candidate_id);
    }
    return CandidateStoreError{.code = CandidateStoreErrorCode::kResourceExhausted,
                               .detail = "Host allocation failed while acquiring a pin lease"};
  } catch (const std::length_error&) {
    for (const CandidateId candidate_id : inserted_count_records) {
      pin_counts_.erase(candidate_id);
    }
    return CandidateStoreError{
        .code = CandidateStoreErrorCode::kResourceExhausted,
        .detail = "Host container limits were exhausted while acquiring a pin lease"};
  }

  for (const CandidatePinRequest& request : canonical_requests) {
    ++pin_counts_.find(request.candidate_id)->second;
  }
  next_pin_lease_id_ = lease_id == std::numeric_limits<std::uint64_t>::max() ? 0 : lease_id + 1;
  return CandidateStorePinLease(pin_lease_control_, lease_id);
}

CandidateStorePinLeaseResult CandidateStore::AcquirePinLeaseIfSourcePoolsMatch(
    std::vector<CandidateStoreExpectedPool>&& expected_source_pools,
    std::span<const CandidatePinRequest> requests) {
  if (!valid()) {
    return CandidateStoreError{
        .code = CandidateStoreErrorCode::kInvalidConfiguration,
        .detail = "Cannot acquire a conditional lease from an invalid candidate store"};
  }
  if (expected_source_pools.empty()) {
    return CandidateStoreError{
        .code = CandidateStoreErrorCode::kInvalidInvocation,
        .detail = "Conditional pin lease requires a nonempty expected source-pool roster"};
  }
  if (expected_source_pools.size() > config_.maximum_expected_pools_per_invocation) {
    return CandidateStoreError{
        .code = CandidateStoreErrorCode::kInvocationInputBoundExceeded,
        .detail = "Conditional pin lease exceeds the configured expected-pool bound"};
  }
  if (requests.size() > config_.maximum_pin_lease_items_per_transaction) {
    return CandidateStoreError{
        .code = CandidateStoreErrorCode::kPinLeaseInputBoundExceeded,
        .detail = "Conditional pin lease exceeds the configured pin-item bound"};
  }

  UWide expected_candidate_count = 0;
  for (const CandidateStoreExpectedPool& pool : expected_source_pools) {
    expected_candidate_count += static_cast<UWide>(pool.candidates.size());
  }
  if (expected_candidate_count > config_.maximum_expected_candidates_per_invocation) {
    return CandidateStoreError{
        .code = CandidateStoreErrorCode::kInvocationInputBoundExceeded,
        .detail = "Conditional pin lease exceeds the configured expected-candidate bound"};
  }

  struct ExpectedCandidateReference {
    CandidateId id;
    board_ir::EntityRef net{};
    const StoredCandidate* candidate = nullptr;
  };
  std::vector<ExpectedCandidateReference> expected_candidates;
  std::vector<CandidatePinRequest> canonical_requests;
  std::vector<CandidateId> candidate_ids;
  std::vector<CandidateId> inserted_count_records;
  try {
    std::ranges::sort(expected_source_pools, [](const CandidateStoreExpectedPool& left,
                                                const CandidateStoreExpectedPool& right) {
      return std::tie(left.net.id, left.net.generation) <
             std::tie(right.net.id, right.net.generation);
    });
    if (std::ranges::adjacent_find(expected_source_pools, {}, &CandidateStoreExpectedPool::net) !=
        expected_source_pools.end()) {
      return CandidateStoreError{
          .code = CandidateStoreErrorCode::kInvalidInvocation,
          .detail = "Conditional pin lease contains a duplicate expected source pool"};
    }

    expected_candidates.reserve(static_cast<std::size_t>(expected_candidate_count));
    for (CandidateStoreExpectedPool& pool : expected_source_pools) {
      for (const StoredCandidate& candidate : pool.candidates) {
        if (candidate == nullptr || candidate->net() != pool.net ||
            candidate->data().associations != pool.associations) {
          return CandidateStoreError{
              .code = CandidateStoreErrorCode::kInvalidInvocation,
              .detail =
                  "Conditional pin lease expected pool contains a null candidate or a candidate "
                  "for another net or association binding"};
        }
      }
      std::ranges::sort(pool.candidates, CandidatePointerRanksBefore);
      for (const StoredCandidate& candidate : pool.candidates) {
        expected_candidates.push_back(ExpectedCandidateReference{
            .id = candidate->id(), .net = pool.net, .candidate = &candidate});
      }
    }
    std::ranges::sort(expected_candidates,
                      [](const ExpectedCandidateReference& left,
                         const ExpectedCandidateReference& right) { return left.id < right.id; });
    if (std::ranges::adjacent_find(expected_candidates, {}, &ExpectedCandidateReference::id) !=
        expected_candidates.end()) {
      return CandidateStoreError{
          .code = CandidateStoreErrorCode::kInvalidInvocation,
          .detail = "Conditional pin lease expected pools contain a duplicate candidate identity"};
    }

    canonical_requests.assign(requests.begin(), requests.end());
    std::ranges::sort(canonical_requests,
                      [](const CandidatePinRequest& left, const CandidatePinRequest& right) {
                        return std::tie(left.candidate_id, left.net.id, left.net.generation,
                                        left.candidate_payload_checksum) <
                               std::tie(right.candidate_id, right.net.id, right.net.generation,
                                        right.candidate_payload_checksum);
                      });
    if (std::ranges::adjacent_find(canonical_requests, {}, &CandidatePinRequest::candidate_id) !=
        canonical_requests.end()) {
      return CandidateStoreError{
          .code = CandidateStoreErrorCode::kInvalidPinLeaseRequest,
          .detail = "Conditional pin lease contains a duplicate candidate identity"};
    }
    candidate_ids.reserve(canonical_requests.size());
    inserted_count_records.reserve(canonical_requests.size());
    for (const CandidatePinRequest& request : canonical_requests) {
      if (request.expected_candidate == nullptr ||
          request.expected_candidate->id() != request.candidate_id ||
          request.expected_candidate->net() != request.net ||
          request.expected_candidate->data().payload_checksum !=
              request.candidate_payload_checksum) {
        return CandidateStoreError{
            .code = CandidateStoreErrorCode::kInvalidPinLeaseRequest,
            .detail = "Conditional pin request does not match its exact immutable candidate"};
      }
      const auto expected = std::ranges::lower_bound(expected_candidates, request.candidate_id, {},
                                                     &ExpectedCandidateReference::id);
      if (expected == expected_candidates.end() || expected->id != request.candidate_id ||
          expected->net != request.net || expected->candidate == nullptr ||
          *expected->candidate == nullptr ||
          (*expected->candidate)->id() != request.expected_candidate->id() ||
          (*expected->candidate)->net() != request.expected_candidate->net() ||
          (*expected->candidate)->data() != request.expected_candidate->data()) {
        return CandidateStoreError{
            .code = CandidateStoreErrorCode::kInvalidPinLeaseRequest,
            .detail = "Conditional pin request is not an exact member of its expected source pool"};
      }
      candidate_ids.push_back(request.candidate_id);
    }
  } catch (const std::bad_alloc&) {
    return CandidateStoreError{
        .code = CandidateStoreErrorCode::kResourceExhausted,
        .detail = "Host allocation failed while preparing a conditional pin lease"};
  } catch (const std::length_error&) {
    return CandidateStoreError{
        .code = CandidateStoreErrorCode::kResourceExhausted,
        .detail = "Host container limits were exhausted while preparing a conditional pin lease"};
  }

  std::scoped_lock lock(mutex_);
  if (!ExpectedPoolsMatchLocked(expected_source_pools)) {
    return CandidateStoreError{
        .code = CandidateStoreErrorCode::kStoreDrift,
        .detail = "Candidate store source pools changed before conditional pin acquisition"};
  }
  for (const CandidatePinRequest& request : canonical_requests) {
    const auto stored = candidate_id_index_.find(request.candidate_id);
    if (stored == candidate_id_index_.end() || stored->second == nullptr ||
        stored->second->id() != request.expected_candidate->id() ||
        stored->second->net() != request.expected_candidate->net() ||
        stored->second->data() != request.expected_candidate->data()) {
      return CandidateStoreError{
          .code = CandidateStoreErrorCode::kStoreDrift,
          .detail = "Candidate store pin target changed before conditional pin acquisition"};
    }
    const auto count = pin_counts_.find(request.candidate_id);
    if (count != pin_counts_.end() && count->second == std::numeric_limits<std::uint64_t>::max()) {
      return CandidateStoreError{.code = CandidateStoreErrorCode::kPinLeaseIdentityExhausted,
                                 .detail = "Candidate pin reference count is exhausted"};
    }
  }
  if (next_pin_lease_id_ == 0) {
    return CandidateStoreError{.code = CandidateStoreErrorCode::kPinLeaseIdentityExhausted,
                               .detail = "Candidate pin lease identity space is exhausted"};
  }

  const std::uint64_t lease_id = next_pin_lease_id_;
  try {
    for (const CandidateId candidate_id : candidate_ids) {
      const auto [unused, inserted] = pin_counts_.try_emplace(candidate_id, 0);
      static_cast<void>(unused);
      if (inserted) {
        inserted_count_records.push_back(candidate_id);
      }
    }
    const auto [unused, inserted] = pin_leases_.emplace(lease_id, std::move(candidate_ids));
    static_cast<void>(unused);
    if (!inserted) {
      for (const CandidateId candidate_id : inserted_count_records) {
        pin_counts_.erase(candidate_id);
      }
      return CandidateStoreError{.code = CandidateStoreErrorCode::kPinLeaseIdentityExhausted,
                                 .detail = "Candidate pin lease identity was already active"};
    }
  } catch (const std::bad_alloc&) {
    for (const CandidateId candidate_id : inserted_count_records) {
      pin_counts_.erase(candidate_id);
    }
    return CandidateStoreError{
        .code = CandidateStoreErrorCode::kResourceExhausted,
        .detail = "Host allocation failed while acquiring a conditional pin lease"};
  } catch (const std::length_error&) {
    for (const CandidateId candidate_id : inserted_count_records) {
      pin_counts_.erase(candidate_id);
    }
    return CandidateStoreError{
        .code = CandidateStoreErrorCode::kResourceExhausted,
        .detail = "Host container limits were exhausted while acquiring a conditional pin lease"};
  }

  for (const CandidatePinRequest& request : canonical_requests) {
    ++pin_counts_.find(request.candidate_id)->second;
  }
  next_pin_lease_id_ = lease_id == std::numeric_limits<std::uint64_t>::max() ? 0 : lease_id + 1;
  return CandidateStorePinLease(pin_lease_control_, lease_id);
}

CandidateStorePinLeaseResult CandidateStore::AcquireEmptyPinLease() {
  if (!valid()) {
    return CandidateStoreError{.code = CandidateStoreErrorCode::kInvalidConfiguration,
                               .detail = "Cannot acquire a lease from an invalid candidate store"};
  }

  std::scoped_lock lock(mutex_);
  if (next_pin_lease_id_ == 0) {
    return CandidateStoreError{.code = CandidateStoreErrorCode::kPinLeaseIdentityExhausted,
                               .detail = "Candidate pin lease identity space is exhausted"};
  }
  const std::uint64_t lease_id = next_pin_lease_id_;
  try {
    const auto [unused, inserted] = pin_leases_.emplace(lease_id, std::vector<CandidateId>{});
    static_cast<void>(unused);
    if (!inserted) {
      return CandidateStoreError{.code = CandidateStoreErrorCode::kPinLeaseIdentityExhausted,
                                 .detail = "Candidate pin lease identity was already active"};
    }
  } catch (const std::bad_alloc&) {
    return CandidateStoreError{
        .code = CandidateStoreErrorCode::kResourceExhausted,
        .detail = "Host allocation failed while acquiring an empty store-identity lease"};
  } catch (const std::length_error&) {
    return CandidateStoreError{.code = CandidateStoreErrorCode::kResourceExhausted,
                               .detail =
                                   "Host container limits were exhausted while acquiring an empty "
                                   "store-identity lease"};
  }
  next_pin_lease_id_ = lease_id == std::numeric_limits<std::uint64_t>::max() ? 0 : lease_id + 1;
  return CandidateStorePinLease(pin_lease_control_, lease_id);
}

void CandidateStore::ReleasePinLease(std::uint64_t lease_id) noexcept {
  std::scoped_lock lock(mutex_);
  const auto lease = pin_leases_.find(lease_id);
  if (lease == pin_leases_.end()) {
    return;
  }
  for (const CandidateId candidate_id : lease->second) {
    const auto count = pin_counts_.find(candidate_id);
    if (count != pin_counts_.end() && count->second > 0 && --count->second == 0) {
      pin_counts_.erase(count);
    }
  }
  pin_leases_.erase(lease);
}

bool CandidateStore::IsPinned(CandidateId candidate_id) const {
  std::scoped_lock lock(mutex_);
  return IsPinnedLocked(candidate_id);
}

bool CandidateStore::IsPinnedLocked(CandidateId candidate_id) const {
  return pin_counts_.contains(candidate_id);
}

void CandidateStore::RetainRejectionLocked(const CandidateRejection& rejection) {
  CandidateRejection canonical = CanonicalizeCandidateRejectionV1(rejection);
  const auto position = std::ranges::lower_bound(rejections_, canonical, RejectionBefore);
  rejections_.insert(position, std::move(canonical));
  if (rejections_.size() > config_.maximum_rejection_records) {
    rejections_.pop_back();
  }
}

CandidateStoreAdmissionResult CandidateStore::PublishAcceptedLocked(RouteCandidate candidate) {
  std::vector<RouteCandidate> candidates;
  candidates.push_back(std::move(candidate));
  std::vector<CandidateStoreAdmissionResult> results =
      PublishAcceptedBatchLocked(std::move(candidates), {});
  return std::move(results.front());
}

std::vector<CandidateStoreAdmissionResult> CandidateStore::PublishAcceptedBatchLocked(
    std::vector<RouteCandidate> candidates, std::vector<CandidateRejection> canonical_rejections,
    std::uint64_t shared_request_policy_normalization_delta) {
  std::uint64_t publication_candidate_inspections = 0;
  std::uint64_t publication_duplicate_equality_checks = 0;
  std::vector<CandidateStoreAdmissionResult> results;
  results.reserve(candidates.size());
  std::ranges::sort(candidates, CandidateTotalBefore);

  std::vector<StoredCandidate> eligible;
  eligible.reserve(candidates.size());
  std::optional<CandidateAssociations> pending_session = bound_associations_;
  std::map<board_ir::EntityRef, CandidateAssociations, NetLess> pending_net_bindings;
  for (RouteCandidate& candidate : candidates) {
    ++publication_candidate_inspections;
    std::optional<CandidateRejection> rejection;
    if (!valid()) {
      rejection = StoreRejection(
          candidate, CandidateRejectionCode::kInvalidInput, "candidate.store.configuration.v1",
          "Candidate store configuration requires positive bounded pool, rejection, and "
          "admission-transaction capacities");
      rejection->stage = CandidateLifecycleStage::kStored;
    } else if (pending_session.has_value() &&
               !SameStoreSession(*pending_session, candidate.data().associations)) {
      rejection = StoreRejection(
          candidate, CandidateRejectionCode::kAssociationMismatch,
          "candidate.store.association_drift.v1",
          "Candidate board/compiler associations differ from this store's immutable session "
          "binding");
      rejection->stage = CandidateLifecycleStage::kStored;
    } else {
      if (!pending_session.has_value()) {
        pending_session = candidate.data().associations;
      }
      const auto persistent = net_associations_.find(candidate.net());
      const CandidateAssociations* expected_context =
          persistent == net_associations_.end() ? nullptr : &persistent->second;
      if (expected_context == nullptr) {
        const auto [pending, inserted] =
            pending_net_bindings.try_emplace(candidate.net(), candidate.data().associations);
        static_cast<void>(inserted);
        expected_context = &pending->second;
      }
      if (*expected_context != candidate.data().associations) {
        rejection = StoreRejection(
            candidate, CandidateRejectionCode::kAssociationMismatch,
            "candidate.store.net_context_drift.v1",
            "Candidate routing-profile or rule-bucket association differs from this net's "
            "immutable pool binding");
        rejection->stage = CandidateLifecycleStage::kStored;
      }
    }
    if (rejection.has_value()) {
      canonical_rejections.push_back(*rejection);
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
    ++publication_candidate_inspections;
    const auto incumbent = candidate_id_index_.find(candidate->id());
    if (incumbent != candidate_id_index_.end()) {
      CandidateRejection rejection = StoreRejection(
          *candidate, CandidateRejectionCode::kDuplicateIdentity, "candidate.store.duplicate_id.v1",
          incumbent->second->net() == candidate->net()
              ? "Candidate identity is already owned by an immutable candidate in this net"
              : "Candidate identity is already owned by another net in the global stable ID "
                "index");
      canonical_rejections.push_back(rejection);
      results.emplace_back(std::move(rejection));
      continue;
    }
    const auto [winner, inserted] = incoming_id_winners.emplace(candidate->id(), candidate);
    if (!inserted) {
      CandidateRejection rejection = StoreRejection(
          *candidate, CandidateRejectionCode::kDuplicateIdentity, "candidate.store.duplicate_id.v1",
          "Candidate is not the preferred stable duplicate identity representative");
      canonical_rejections.push_back(rejection);
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
    publication_candidate_inspections += entries.size();

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

    std::vector<std::size_t> order_position(entries.size());
    for (std::size_t position = 0; position < order.size(); ++position) {
      order_position[order[position]] = position;
    }
    std::vector<std::optional<std::size_t>> conflict_winner(entries.size());
    std::vector<std::optional<DuplicateRelation>> conflict_relation(entries.size());
    std::vector<std::size_t> duplicate_winners;
    std::map<CandidateId, std::size_t> identity_winners;
    std::map<CandidateSignature, std::vector<std::size_t>> geometry_winners;
    std::map<CandidateSignature, std::vector<std::size_t>> resource_winners;
    for (const std::size_t index : order) {
      std::optional<std::size_t> conflict;
      std::optional<DuplicateRelation> relation;
      const auto consider = [&](std::size_t winner_index, DuplicateRelation duplicate_relation) {
        if (!conflict.has_value() || order_position[winner_index] < order_position[*conflict]) {
          conflict = winner_index;
          relation = duplicate_relation;
        }
      };

      if (const auto identity = identity_winners.find(entries[index].candidate->id());
          identity != identity_winners.end()) {
        consider(identity->second,
                 DuplicateRelation{.code = CandidateRejectionCode::kDuplicateIdentity,
                                   .invariant = "candidate.store.duplicate_id.v1"});
      }
      if (!conflict.has_value() || order_position[*conflict] != 0) {
        if (const auto geometry =
                geometry_winners.find(entries[index].candidate->data().geometry_signature);
            geometry != geometry_winners.end()) {
          for (const std::size_t winner_index : geometry->second) {
            ++publication_candidate_inspections;
            ++publication_duplicate_equality_checks;
            if (internal::ClassifySignatureBucketDuplicate(
                    *entries[index].candidate, *entries[winner_index].candidate, true, false) ==
                internal::SignatureBucketDuplicate::kGeometry) {
              consider(winner_index,
                       DuplicateRelation{.code = CandidateRejectionCode::kDuplicateGeometry,
                                         .invariant = "candidate.store.duplicate_geometry.v1"});
              break;
            }
          }
        }
      }
      if (!conflict.has_value() || order_position[*conflict] != 0) {
        if (const auto resource =
                resource_winners.find(entries[index].candidate->data().resource_signature);
            resource != resource_winners.end()) {
          for (const std::size_t winner_index : resource->second) {
            ++publication_candidate_inspections;
            ++publication_duplicate_equality_checks;
            if (internal::ClassifySignatureBucketDuplicate(
                    *entries[index].candidate, *entries[winner_index].candidate, false, true) ==
                internal::SignatureBucketDuplicate::kResources) {
              consider(winner_index,
                       DuplicateRelation{.code = CandidateRejectionCode::kDuplicateResources,
                                         .invariant = "candidate.store.duplicate_resources.v1"});
              break;
            }
          }
        }
      }
      if (conflict.has_value() && !entry_is_pinned(index)) {
        conflict_winner[index] = *conflict;
        conflict_relation[index] = *relation;
        continue;
      }
      duplicate_winners.push_back(index);
      identity_winners.try_emplace(entries[index].candidate->id(), index);
      geometry_winners[entries[index].candidate->data().geometry_signature].push_back(index);
      resource_winners[entries[index].candidate->data().resource_signature].push_back(index);
    }

    CandidatePool winner_pool;
    winner_pool.reserve(duplicate_winners.size());
    for (const std::size_t index : duplicate_winners) {
      winner_pool.push_back(entries[index].candidate);
    }
    RetentionSelectionImpl selection = SelectRetention(std::move(winner_pool), config_, pin_counts_,
                                                       &publication_candidate_inspections);
#if defined(APGAR_CANDIDATE_STORE_PINNED_ROLLBACK_TEST_VARIANT)
    if (g_forced_pinned_rollback_net == net &&
        std::ranges::any_of(entries, [this](const BatchEntry& entry) {
          return !entry.incoming && IsPinnedLocked(entry.candidate->id());
        })) {
      selection.pinned_budget_failure = true;
      g_forced_pinned_rollback_net.reset();
    }
#endif
    if (selection.pinned_budget_failure) {
      pinned_budget_failure = true;
      break;
    }

    StagedPool& change = staged[net];
    change.retained = std::move(selection.retained);
    change.incoming = incoming;
    for (std::size_t index = 0; index < entries.size(); ++index) {
      const BatchEntry& entry = entries[index];
      const bool retained = !conflict_winner[index].has_value() &&
                            ContainsCandidate(change.retained, entry.candidate);
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
        canonical_rejections.push_back(rejection);
        results.emplace_back(std::move(rejection));
      }
    }
    std::optional<std::vector<CandidateRejection>> merged_rejections;
    if (!canonical_rejections.empty()) {
      merged_rejections.emplace(BuildMergedRejectionsLocked(std::move(canonical_rejections)));
    }
    std::ranges::sort(results, StoreResultBefore);
    // A pinned-budget rollback is an ordinary, authoritative diagnostic
    // publication. Commit its immutable association binding, history, and
    // telemetry only after all fallible staging has completed.
    if (!bound_associations_.has_value() && pending_session.has_value()) {
      bound_associations_ = *pending_session;
    }
    net_associations_.merge(pending_net_bindings);
    if (merged_rejections.has_value()) {
      rejections_ = std::move(*merged_rejections);
      ++rejection_batch_merges_;
    }
    last_publication_candidate_inspections_ = publication_candidate_inspections;
    last_duplicate_equality_checks_ = publication_duplicate_equality_checks;
    shared_request_policy_normalizations_ += shared_request_policy_normalization_delta;
    return results;
  }

  // Finish every potentially allocating rejection/result operation before
  // mutating pools or the global ID index. The final history move below cannot
  // expose a partial per-record publication.
  std::size_t staged_rejection_count = 0;
  std::size_t staged_result_count = 0;
  for (const auto& [net, change] : staged) {
    static_cast<void>(net);
    if (change.rejections.size() >
            std::numeric_limits<std::size_t>::max() - staged_rejection_count ||
        change.results.size() > std::numeric_limits<std::size_t>::max() - staged_result_count) {
      throw std::length_error("Candidate publication result collection exceeds size_t");
    }
    staged_rejection_count += change.rejections.size();
    staged_result_count += change.results.size();
  }
  if (staged_rejection_count >
          std::numeric_limits<std::size_t>::max() - canonical_rejections.size() ||
      staged_result_count > std::numeric_limits<std::size_t>::max() - results.size()) {
    throw std::length_error("Candidate publication result collection exceeds size_t");
  }
  canonical_rejections.reserve(canonical_rejections.size() + staged_rejection_count);
  results.reserve(results.size() + staged_result_count);
  for (auto& [net, change] : staged) {
    static_cast<void>(net);
    canonical_rejections.insert(canonical_rejections.end(),
                                std::make_move_iterator(change.rejections.begin()),
                                std::make_move_iterator(change.rejections.end()));
    results.insert(results.end(), std::make_move_iterator(change.results.begin()),
                   std::make_move_iterator(change.results.end()));
  }
  std::optional<std::vector<CandidateRejection>> merged_rejections;
  if (!canonical_rejections.empty()) {
    merged_rejections.emplace(BuildMergedRejectionsLocked(std::move(canonical_rejections)));
  }
  std::ranges::sort(results, StoreResultBefore);

  // Allocate and populate every replacement map node before the store state
  // changes. Node-handle transfer below uses the same allocators and cannot
  // allocate, so a host allocation failure leaves both authoritative maps at
  // the pre-publication snapshot.
  std::map<board_ir::EntityRef, CandidatePool, NetLess> prepared_pools;
  std::map<CandidateId, StoredCandidate> prepared_candidate_ids;
  for (auto& [net, change] : staged) {
    if (change.retained.empty()) {
      continue;
    }
    std::ranges::sort(change.retained, CandidatePointerRanksBefore);
    MaybeFailPublicationPreparationForTesting();
    auto [prepared, inserted] = prepared_pools.emplace(net, std::move(change.retained));
    if (!inserted) {
      throw std::logic_error("Candidate publication staged a duplicate net replacement");
    }
    for (const StoredCandidate& candidate : prepared->second) {
      MaybeFailPublicationPreparationForTesting();
      if (!prepared_candidate_ids.emplace(candidate->id(), candidate).second) {
        throw std::logic_error("Candidate publication staged a duplicate global identity");
      }
      const auto incumbent = candidate_id_index_.find(candidate->id());
      if (incumbent != candidate_id_index_.end() && !staged.contains(incumbent->second->net())) {
        throw std::logic_error("Candidate publication conflicts with an untouched global ID");
      }
    }
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
  for (const auto& [net, change] : staged) {
    static_cast<void>(change);
    auto prepared = prepared_pools.extract(net);
    if (prepared.empty()) {
      pools_.erase(net);
    } else {
      const auto incumbent = pools_.find(net);
      if (incumbent == pools_.end()) {
        static_cast<void>(pools_.insert(std::move(prepared)));
      } else {
        incumbent->second = std::move(prepared.mapped());
      }
    }
  }
  candidate_id_index_.merge(prepared_candidate_ids);
  if (merged_rejections.has_value()) {
    rejections_ = std::move(*merged_rejections);
    ++rejection_batch_merges_;
  }
  // Exact admission establishes immutable session/net associations even when
  // every eligible candidate is rejected by duplicate or retention policy.
  // These bindings and publication telemetry commit with the authoritative
  // pools, identity index, and rejection history, never with fallible staging.
  if (!bound_associations_.has_value() && pending_session.has_value()) {
    bound_associations_ = *pending_session;
  }
  net_associations_.merge(pending_net_bindings);
  last_publication_candidate_inspections_ = publication_candidate_inspections;
  last_duplicate_equality_checks_ = publication_duplicate_equality_checks;
  shared_request_policy_normalizations_ += shared_request_policy_normalization_delta;
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
  const RetentionSelectionImpl selection =
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
