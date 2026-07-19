#include "apgar/candidates/route_candidate.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

#include "apgar/board_ir/stable_hash.h"
#include "apgar/geometry/exact.h"
#include "apgar/text/utf8.h"

namespace apgar::candidates {
namespace {

using geometry_compiler::Direction;
using geometry_compiler::DirectionDelta;
using geometry_compiler::LatticeIndex;
using Wide = __int128_t;
using UWide = __uint128_t;

struct VerificationFailure {
  CandidateLifecycleStage stage = CandidateLifecycleStage::kGenerated;
  CandidateRejectionCode code = CandidateRejectionCode::kInvalidInput;
  std::string invariant_id;
  std::string detail;
  std::optional<std::uint64_t> primitive_witness_index;
  std::optional<std::uint64_t> resource_witness_index;
  std::optional<std::uint64_t> expected_value;
  std::optional<std::uint64_t> actual_value;
  std::optional<board_ir::EntityRef> obstacle;
};

struct DerivedCandidateFields {
  std::vector<PhysicalEdgeSpan> resources;
  CandidateMetrics metrics;
};

[[nodiscard]] VerificationFailure Failure(CandidateLifecycleStage stage,
                                          CandidateRejectionCode code, std::string invariant_id,
                                          std::string detail) {
  VerificationFailure failure;
  failure.stage = stage;
  failure.code = code;
  failure.invariant_id = std::move(invariant_id);
  failure.detail = std::move(detail);
  return failure;
}

[[nodiscard]] bool IsKnownGenerator(CandidateGeneratorKind generator) noexcept {
  switch (generator) {
    case CandidateGeneratorKind::kCpuAStar:
    case CandidateGeneratorKind::kCudaFrontier:
    case CandidateGeneratorKind::kCudaSweep:
      return true;
  }
  return false;
}

[[nodiscard]] bool IsKnownBackend(CandidateBackendKind backend) noexcept {
  switch (backend) {
    case CandidateBackendKind::kCpu:
    case CandidateBackendKind::kCuda:
      return true;
  }
  return false;
}

[[nodiscard]] bool ProvenanceCombinationIsSupported(
    const CandidateProvenance& provenance) noexcept {
  if (provenance.generator == CandidateGeneratorKind::kCpuAStar) {
    return provenance.backend == CandidateBackendKind::kCpu;
  }
  return provenance.backend == CandidateBackendKind::kCuda;
}

[[nodiscard]] std::optional<std::uint64_t> AddBytes(std::uint64_t current,
                                                    std::uint64_t additional) noexcept {
  if (additional > std::numeric_limits<std::uint64_t>::max() - current) {
    return std::nullopt;
  }
  return current + additional;
}

class CanonicalFieldEncoder {
 public:
  explicit CanonicalFieldEncoder(std::string_view domain) { hash_.AddString(domain); }

  void AddByte(std::uint8_t value) noexcept {
    hash_.AddByte(value);
    Count(sizeof(value));
  }
  void AddBool(bool value) noexcept {
    hash_.AddBool(value);
    Count(sizeof(std::uint8_t));
  }
  void AddU16(std::uint16_t value) noexcept {
    hash_.AddByte(static_cast<std::uint8_t>(value & 0xffU));
    hash_.AddByte(static_cast<std::uint8_t>((value >> 8U) & 0xffU));
    Count(sizeof(value));
  }
  void AddU32(std::uint32_t value) noexcept {
    hash_.AddU32(value);
    Count(sizeof(value));
  }
  void AddU64(std::uint64_t value) noexcept {
    hash_.AddU64(value);
    Count(sizeof(value));
  }
  void AddI64(std::int64_t value) noexcept {
    hash_.AddI64(value);
    Count(sizeof(value));
  }
  void AddString(std::string_view value) noexcept {
    hash_.AddString(value);
    Count(sizeof(std::uint64_t));
    Count(static_cast<std::uint64_t>(value.size()));
  }

  [[nodiscard]] std::uint64_t hash() const noexcept { return hash_.Finish(); }
  [[nodiscard]] std::optional<std::uint64_t> bytes() const noexcept {
    return overflowed_ ? std::nullopt : std::optional<std::uint64_t>(bytes_);
  }

 private:
  void Count(std::uint64_t additional) noexcept {
    if (overflowed_) {
      return;
    }
    const std::optional<std::uint64_t> next = AddBytes(bytes_, additional);
    if (!next.has_value()) {
      overflowed_ = true;
      return;
    }
    bytes_ = *next;
  }

  board_ir::StableHashBuilder hash_;
  std::uint64_t bytes_ = 0;
  bool overflowed_ = false;
};

void EncodeEntityRef(CanonicalFieldEncoder& encoder, board_ir::EntityRef ref) noexcept {
  encoder.AddU64(ref.id);
  encoder.AddU32(ref.generation);
}

void EncodeHash128(CanonicalFieldEncoder& encoder, Hash128 hash) noexcept {
  encoder.AddU64(hash.high);
  encoder.AddU64(hash.low);
}

void EncodeAssociations(CanonicalFieldEncoder& encoder,
                        const CandidateAssociations& associations) noexcept {
  encoder.AddU64(associations.board_content_hash);
  encoder.AddU64(associations.compiler_profile_fingerprint);
  encoder.AddU32(associations.geometry_compiler_version);
  encoder.AddU64(associations.routing_profile_fingerprint);
  encoder.AddU64(associations.rule_bucket_identity);
}

void EncodeResourceKey(CanonicalFieldEncoder& encoder,
                       const routing::EdgeResourceKey& resource) noexcept {
  encoder.AddU32(resource.layer);
  encoder.AddI64(resource.lattice_x);
  encoder.AddI64(resource.lattice_y);
  encoder.AddByte(static_cast<std::uint8_t>(resource.direction));
}

void EncodePolicy(CanonicalFieldEncoder& encoder,
                  const routing::CandidateGenerationPolicy& policy) noexcept {
  encoder.AddU32(policy.schema_version);
  encoder.AddByte(static_cast<std::uint8_t>(policy.objective));
  encoder.AddU64(policy.deterministic_seed);
  encoder.AddU32(policy.candidate_ordinal);
  encoder.AddU64(policy.orthogonal_step_surcharge);
  encoder.AddU64(policy.diagonal_step_surcharge);
  encoder.AddU64(policy.bend_surcharge);
  encoder.AddU64(static_cast<std::uint64_t>(policy.banned_resources.size()));
  for (const routing::EdgeResourceKey& resource : policy.banned_resources) {
    EncodeResourceKey(encoder, resource);
  }
  encoder.AddU64(static_cast<std::uint64_t>(policy.resource_penalties.size()));
  for (const routing::ResourcePenalty& penalty : policy.resource_penalties) {
    EncodeResourceKey(encoder, penalty.resource);
    encoder.AddU64(penalty.additional_cost);
  }
}

void EncodeProvenance(CanonicalFieldEncoder& encoder,
                      const CandidateProvenance& provenance) noexcept {
  encoder.AddByte(static_cast<std::uint8_t>(provenance.generator));
  encoder.AddU32(provenance.generator_version);
  encoder.AddByte(static_cast<std::uint8_t>(provenance.backend));
  encoder.AddString(provenance.supported_device_class);
  encoder.AddU64(provenance.deterministic_seed);
  encoder.AddU64(provenance.batch_identity);
  encoder.AddU64(provenance.query_identity);
  encoder.AddU32(provenance.candidate_ordinal);
}

void EncodePrimitive(CanonicalFieldEncoder& encoder, const CandidatePrimitive& primitive) noexcept {
  if (const auto* line = std::get_if<ExactLinePrimitive>(&primitive); line != nullptr) {
    encoder.AddByte(1);
    encoder.AddU32(line->layer);
    encoder.AddI64(line->centerline.start.x);
    encoder.AddI64(line->centerline.start.y);
    encoder.AddI64(line->centerline.end.x);
    encoder.AddI64(line->centerline.end.y);
    return;
  }
  const ThroughViaPrimitive& via = std::get<ThroughViaPrimitive>(primitive);
  encoder.AddByte(2);
  encoder.AddU64(via.template_id);
  encoder.AddI64(via.position.x);
  encoder.AddI64(via.position.y);
  encoder.AddU32(via.start_layer);
  encoder.AddU32(via.end_layer);
}

void EncodeResourceSpan(CanonicalFieldEncoder& encoder, const PhysicalEdgeSpan& resource) noexcept {
  encoder.AddU32(resource.layer);
  encoder.AddI64(resource.lattice_x);
  encoder.AddI64(resource.lattice_y);
  encoder.AddByte(static_cast<std::uint8_t>(resource.direction));
  encoder.AddU32(resource.edge_count);
  encoder.AddU32(resource.usage_units);
}

void EncodeMetrics(CanonicalFieldEncoder& encoder, const CandidateMetrics& metrics) noexcept {
  encoder.AddU64(metrics.scalar_policy_cost);
  encoder.AddU64(metrics.intrinsic_base_cost);
  encoder.AddU64(metrics.orthogonal_step_count);
  encoder.AddU64(metrics.diagonal_step_count);
  encoder.AddU64(metrics.bend_count);
  encoder.AddU64(metrics.line_primitive_count);
  encoder.AddU64(metrics.via_count);
  encoder.AddU64(metrics.axis_aligned_length_dbu);
  encoder.AddU64(metrics.diagonal_projection_dbu);
}

void EncodeConstraints(CanonicalFieldEncoder& encoder,
                       const ConstraintAssessment& constraints) noexcept {
  encoder.AddBool(constraints.supported_hard_constraints_satisfied);
  encoder.AddBool(constraints.unsupported_rules_remain);
  encoder.AddU32(constraints.connected_intended_terminal_count);
  encoder.AddByte(static_cast<std::uint8_t>(constraints.exact_validation_code));
}

void EncodeCandidatePayload(CanonicalFieldEncoder& encoder,
                            const GeneratedRouteCandidate& candidate) noexcept {
  encoder.AddU16(candidate.schema_major);
  encoder.AddU16(candidate.schema_minor);
  EncodeHash128(encoder, candidate.id);
  EncodeEntityRef(encoder, candidate.net);
  for (const board_ir::EntityRef terminal : candidate.intended_terminals) {
    EncodeEntityRef(encoder, terminal);
  }
  EncodeAssociations(encoder, candidate.associations);
  encoder.AddU32(candidate.geometry_schema_version);
  encoder.AddU32(candidate.resource_schema_version);
  EncodePolicy(encoder, candidate.policy);
  encoder.AddU64(candidate.policy_identity);
  EncodeProvenance(encoder, candidate.provenance);
  encoder.AddU64(static_cast<std::uint64_t>(candidate.geometry.size()));
  for (const CandidatePrimitive& primitive : candidate.geometry) {
    EncodePrimitive(encoder, primitive);
  }
  encoder.AddU64(static_cast<std::uint64_t>(candidate.resources.size()));
  for (const PhysicalEdgeSpan& resource : candidate.resources) {
    EncodeResourceSpan(encoder, resource);
  }
  EncodeMetrics(encoder, candidate.metrics);
  EncodeConstraints(encoder, candidate.constraints);
  EncodeHash128(encoder, candidate.geometry_signature);
  EncodeHash128(encoder, candidate.resource_signature);
}

[[nodiscard]] std::optional<Direction> ExactSegmentDirection(board_ir::Segment64 segment) noexcept {
  const Wide delta_x = static_cast<Wide>(segment.end.x) - segment.start.x;
  const Wide delta_y = static_cast<Wide>(segment.end.y) - segment.start.y;
  if (delta_x == 0 && delta_y == 0) {
    return std::nullopt;
  }
  const int x_sign = (delta_x > 0) - (delta_x < 0);
  const int y_sign = (delta_y > 0) - (delta_y < 0);
  if (delta_x != 0 && delta_y != 0 &&
      (delta_x < 0 ? -delta_x : delta_x) != (delta_y < 0 ? -delta_y : delta_y)) {
    return std::nullopt;
  }
  if (x_sign == 1 && y_sign == 0) {
    return Direction::kEast;
  }
  if (x_sign == 1 && y_sign == 1) {
    return Direction::kNorthEast;
  }
  if (x_sign == 0 && y_sign == 1) {
    return Direction::kNorth;
  }
  if (x_sign == -1 && y_sign == 1) {
    return Direction::kNorthWest;
  }
  if (x_sign == -1 && y_sign == 0) {
    return Direction::kWest;
  }
  if (x_sign == -1 && y_sign == -1) {
    return Direction::kSouthWest;
  }
  if (x_sign == 0 && y_sign == -1) {
    return Direction::kSouth;
  }
  return Direction::kSouthEast;
}

[[nodiscard]] std::variant<std::vector<CandidatePrimitive>, VerificationFailure> NormalizeGeometry(
    const board_ir::BoardSnapshot& board, std::span<const CandidatePrimitive> input) {
  if (input.empty() || input.size() > kMaximumCandidatePrimitives) {
    return Failure(CandidateLifecycleStage::kNormalized, CandidateRejectionCode::kInvalidInput,
                   "candidate.geometry.primitive_count.v1",
                   "Candidate geometry primitive count is outside the v1 bound");
  }
  std::vector<ExactLinePrimitive> lines;
  lines.reserve(input.size());
  for (std::size_t index = 0; index < input.size(); ++index) {
    if (std::holds_alternative<ThroughViaPrimitive>(input[index])) {
      VerificationFailure failure = Failure(
          CandidateLifecycleStage::kNormalized, CandidateRejectionCode::kUnsupported,
          "candidate.geometry.through_via_unsupported.v1",
          "Through-via encoding is reserved but exact via-template validation is unavailable");
      failure.primitive_witness_index = index;
      return failure;
    }
    const ExactLinePrimitive& line = std::get<ExactLinePrimitive>(input[index]);
    if (!board_ir::PointIsValid(line.centerline.start) ||
        !board_ir::PointIsValid(line.centerline.end)) {
      VerificationFailure failure =
          Failure(CandidateLifecycleStage::kNormalized, CandidateRejectionCode::kInvalidInput,
                  "candidate.geometry.coordinate_bounds.v1",
                  "Candidate line endpoint exceeds the signed Board IR coordinate envelope");
      failure.primitive_witness_index = index;
      return failure;
    }
    if (line.centerline.start == line.centerline.end) {
      VerificationFailure failure =
          Failure(CandidateLifecycleStage::kNormalized, CandidateRejectionCode::kInvalidInput,
                  "candidate.geometry.degenerate_line.v1", "Candidate line is degenerate");
      failure.primitive_witness_index = index;
      return failure;
    }
    if (!ExactSegmentDirection(line.centerline).has_value()) {
      VerificationFailure failure =
          Failure(CandidateLifecycleStage::kNormalized, CandidateRejectionCode::kExactValidation,
                  "candidate.geometry.heading.v1", "Candidate line is not H/V/45-degree");
      failure.primitive_witness_index = index;
      return failure;
    }
    if (!lines.empty() && lines.back().centerline.end != line.centerline.start) {
      VerificationFailure failure =
          Failure(CandidateLifecycleStage::kNormalized, CandidateRejectionCode::kExactValidation,
                  "candidate.geometry.connectivity.v1",
                  "Candidate primitives are not exactly contiguous and ordered");
      failure.primitive_witness_index = index;
      return failure;
    }
    lines.push_back(line);
  }

  const board_ir::Net* net = board.FindNet(board.data().routing_profile.net);
  if (net == nullptr || net->terminals.size() != 2) {
    return Failure(CandidateLifecycleStage::kExactValidated,
                   CandidateRejectionCode::kAssociationMismatch,
                   "candidate.terminals.board_contract.v1",
                   "Board routing-profile net does not have exactly two terminals");
  }
  const board_ir::Terminal* first = board.FindTerminal(net->terminals[0]);
  const board_ir::Terminal* second = board.FindTerminal(net->terminals[1]);
  if (first == nullptr || second == nullptr) {
    return Failure(CandidateLifecycleStage::kExactValidated,
                   CandidateRejectionCode::kAssociationMismatch, "candidate.terminals.missing.v1",
                   "Board routing-profile terminal reference is stale");
  }

  const bool forward = lines.front().centerline.start == first->center &&
                       lines.back().centerline.end == second->center;
  const bool reverse = lines.front().centerline.start == second->center &&
                       lines.back().centerline.end == first->center;
  if (!forward && !reverse) {
    return Failure(CandidateLifecycleStage::kExactValidated,
                   CandidateRejectionCode::kExactValidation, "candidate.terminals.endpoints.v1",
                   "Candidate does not run between exactly the intended terminal centers");
  }
  if (reverse) {
    std::ranges::reverse(lines);
    for (ExactLinePrimitive& line : lines) {
      std::swap(line.centerline.start, line.centerline.end);
    }
  }

  std::vector<CandidatePrimitive> normalized;
  normalized.reserve(lines.size());
  for (const ExactLinePrimitive& line : lines) {
    const Direction direction = *ExactSegmentDirection(line.centerline);
    if (!normalized.empty()) {
      ExactLinePrimitive& previous = std::get<ExactLinePrimitive>(normalized.back());
      const Direction previous_direction = *ExactSegmentDirection(previous.centerline);
      if (previous.layer == line.layer && previous.centerline.end == line.centerline.start &&
          previous_direction == direction) {
        previous.centerline.end = line.centerline.end;
        continue;
      }
    }
    normalized.emplace_back(line);
  }
  return normalized;
}

struct PathVertex {
  board_ir::LayerId layer;
  board_ir::Point64 point;
  std::size_t path_position;
};

struct IndexedLineBounds {
  std::size_t primitive_index;
  board_ir::LayerId layer;
  board_ir::Segment64 centerline;
  board_ir::DbCoord min_x;
  board_ir::DbCoord max_x;
  board_ir::DbCoord min_y;
  board_ir::DbCoord max_y;
};

[[nodiscard]] bool Collinear(board_ir::Segment64 first, board_ir::Segment64 second) noexcept {
  const Wide first_x = static_cast<Wide>(first.end.x) - first.start.x;
  const Wide first_y = static_cast<Wide>(first.end.y) - first.start.y;
  const Wide second_x = static_cast<Wide>(second.end.x) - second.start.x;
  const Wide second_y = static_cast<Wide>(second.end.y) - second.start.y;
  const Wide offset_x = static_cast<Wide>(second.start.x) - first.start.x;
  const Wide offset_y = static_cast<Wide>(second.start.y) - first.start.y;
  return first_x * second_y == first_y * second_x && first_x * offset_y == first_y * offset_x;
}

[[nodiscard]] bool PositiveLengthCollinearOverlap(board_ir::Segment64 first,
                                                  board_ir::Segment64 second) noexcept {
  if (!Collinear(first, second)) {
    return false;
  }
  if (first.start.x != first.end.x) {
    return std::max(std::min(first.start.x, first.end.x), std::min(second.start.x, second.end.x)) <
           std::min(std::max(first.start.x, first.end.x), std::max(second.start.x, second.end.x));
  }
  return std::max(std::min(first.start.y, first.end.y), std::min(second.start.y, second.end.y)) <
         std::min(std::max(first.start.y, first.end.y), std::max(second.start.y, second.end.y));
}

[[nodiscard]] Wide AxisGap(board_ir::DbCoord first_min, board_ir::DbCoord first_max,
                           board_ir::DbCoord second_min, board_ir::DbCoord second_max) noexcept {
  if (first_max < second_min) {
    return static_cast<Wide>(second_min) - first_max;
  }
  if (second_max < first_min) {
    return static_cast<Wide>(first_min) - second_max;
  }
  return 0;
}

[[nodiscard]] std::optional<VerificationFailure> ValidateCanonicalSelfTopology(
    const board_ir::RoutingProfile& profile, std::span<const CandidatePrimitive> geometry,
    std::uint64_t maximum_pair_checks) {
  std::vector<PathVertex> vertices;
  vertices.reserve(geometry.size() + 1U);
  const ExactLinePrimitive& first = std::get<ExactLinePrimitive>(geometry.front());
  vertices.push_back(PathVertex{
      .layer = first.layer,
      .point = first.centerline.start,
      .path_position = 0,
  });

  std::vector<IndexedLineBounds> lines;
  lines.reserve(geometry.size());
  for (std::size_t index = 0; index < geometry.size(); ++index) {
    const ExactLinePrimitive& line = std::get<ExactLinePrimitive>(geometry[index]);
    vertices.push_back(PathVertex{
        .layer = line.layer,
        .point = line.centerline.end,
        .path_position = index + 1U,
    });
    lines.push_back(IndexedLineBounds{
        .primitive_index = index,
        .layer = line.layer,
        .centerline = line.centerline,
        .min_x = std::min(line.centerline.start.x, line.centerline.end.x),
        .max_x = std::max(line.centerline.start.x, line.centerline.end.x),
        .min_y = std::min(line.centerline.start.y, line.centerline.end.y),
        .max_y = std::max(line.centerline.start.y, line.centerline.end.y),
    });
  }

  std::ranges::sort(vertices, [](const PathVertex& left, const PathVertex& right) {
    return std::tie(left.layer, left.point.x, left.point.y, left.path_position) <
           std::tie(right.layer, right.point.x, right.point.y, right.path_position);
  });
  for (std::size_t index = 1; index < vertices.size(); ++index) {
    const PathVertex& previous = vertices[index - 1U];
    const PathVertex& current = vertices[index];
    if (previous.layer == current.layer && previous.point == current.point) {
      VerificationFailure failure =
          Failure(CandidateLifecycleStage::kExactValidated,
                  CandidateRejectionCode::kExactValidation, "candidate.geometry.reused_vertex.v1",
                  "Candidate revisits a path vertex first used at path position " +
                      std::to_string(previous.path_position));
      failure.primitive_witness_index = current.path_position - 1U;
      return failure;
    }
  }

  std::ranges::sort(lines, [](const IndexedLineBounds& left, const IndexedLineBounds& right) {
    return std::tie(left.min_x, left.min_y, left.max_x, left.max_y, left.layer,
                    left.primitive_index) < std::tie(right.min_x, right.min_y, right.max_x,
                                                     right.max_y, right.layer,
                                                     right.primitive_index);
  });
  const Wide required_clearance = profile.nominal_width;
  std::uint64_t pair_checks = 0;
  for (std::size_t left_index = 0; left_index < lines.size(); ++left_index) {
    const IndexedLineBounds& left = lines[left_index];
    for (std::size_t right_index = left_index + 1U; right_index < lines.size(); ++right_index) {
      const IndexedLineBounds& right = lines[right_index];
      if (static_cast<Wide>(right.min_x) - left.max_x >= required_clearance) {
        break;
      }
      if (pair_checks == maximum_pair_checks) {
        VerificationFailure failure = Failure(
            CandidateLifecycleStage::kExactValidated, CandidateRejectionCode::kBudgetExhausted,
            "candidate.geometry.self_clearance_pair_budget.v1",
            "Candidate exceeded the deterministic v1 self-clearance pair-check budget");
        failure.primitive_witness_index = right.primitive_index;
        failure.expected_value = maximum_pair_checks;
        failure.actual_value = pair_checks + 1U;
        return failure;
      }
      ++pair_checks;
      if (left.layer != right.layer || (left.primitive_index + 1U == right.primitive_index) ||
          (right.primitive_index + 1U == left.primitive_index) ||
          AxisGap(left.min_y, left.max_y, right.min_y, right.max_y) >= required_clearance) {
        continue;
      }
      const geometry::SegmentClearanceResult clearance = geometry::SegmentToSegmentClearanceAtLeast(
          left.centerline, right.centerline, profile.nominal_width);
      if (!clearance.ok()) {
        VerificationFailure failure = Failure(
            CandidateLifecycleStage::kExactValidated, CandidateRejectionCode::kExactValidation,
            "candidate.geometry.self_clearance_inputs.v1",
            "Candidate self-clearance inputs are outside exact geometry bounds");
        failure.primitive_witness_index = right.primitive_index;
        return failure;
      }
      // Positive-length centerline reuse is represented exactly by duplicate
      // physical edge resources. Preserve that more specific collision-safe
      // diagnostic in DeriveFields; this exact check owns distinct-edge
      // crossings, point self-touches, and swept near-overlap.
      if (PositiveLengthCollinearOverlap(left.centerline, right.centerline)) {
        continue;
      }
      if (!clearance.clearance_satisfied) {
        VerificationFailure failure = Failure(
            CandidateLifecycleStage::kExactValidated, CandidateRejectionCode::kExactValidation,
            "candidate.geometry.swept_self_overlap.v1",
            "Nonconsecutive candidate swept traces overlap primitive " +
                std::to_string(left.primitive_index));
        failure.primitive_witness_index = right.primitive_index;
        return failure;
      }
    }
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<VerificationFailure> ValidateExactGeometry(
    const board_ir::BoardSnapshot& board, const routing::PlanarRouteRequest& request,
    std::span<const CandidatePrimitive> geometry,
    std::uint64_t maximum_self_clearance_pair_checks) {
  const board_ir::Net* intended_net = board.FindNet(request.net);
  if (intended_net == nullptr || intended_net->terminals.size() != 2) {
    return Failure(CandidateLifecycleStage::kExactValidated,
                   CandidateRejectionCode::kAssociationMismatch, "candidate.net.missing.v1",
                   "Candidate net reference is stale");
  }
  const board_ir::Terminal* first = board.FindTerminal(intended_net->terminals[0]);
  const board_ir::Terminal* second = board.FindTerminal(intended_net->terminals[1]);
  if (first == nullptr || second == nullptr) {
    return Failure(CandidateLifecycleStage::kExactValidated,
                   CandidateRejectionCode::kAssociationMismatch, "candidate.terminals.missing.v1",
                   "Candidate intended terminal reference is stale");
  }
  const bool request_is_forward = request.start == first->center && request.goal == second->center;
  const bool request_is_reverse = request.start == second->center && request.goal == first->center;
  if (!request_is_forward && !request_is_reverse) {
    return Failure(CandidateLifecycleStage::kExactValidated,
                   CandidateRejectionCode::kAssociationMismatch,
                   "candidate.terminals.request_orientation.v1",
                   "Route request does not name the canonical intended terminal pair");
  }
  if (const std::optional<VerificationFailure> failure = ValidateCanonicalSelfTopology(
          board.data().routing_profile, geometry, maximum_self_clearance_pair_checks);
      failure.has_value()) {
    return failure;
  }
  board_ir::Point64 expected = first->center;
  board_ir::LayerId expected_layer = request_is_forward ? request.start_layer : request.goal_layer;
  const board_ir::LayerId canonical_goal_layer =
      request_is_forward ? request.goal_layer : request.start_layer;
  for (std::size_t index = 0; index < geometry.size(); ++index) {
    const ExactLinePrimitive& line = std::get<ExactLinePrimitive>(geometry[index]);
    if (line.layer != expected_layer || line.centerline.start != expected) {
      VerificationFailure failure = Failure(
          CandidateLifecycleStage::kExactValidated, CandidateRejectionCode::kExactValidation,
          "candidate.geometry.layer_connectivity.v1",
          "Candidate changes layer or disconnects without an exact via primitive");
      failure.primitive_witness_index = index;
      return failure;
    }
    const geometry::MovementValidationResult exact =
        geometry::ValidateMovement(board, line.layer, line.centerline);
    if (!exact.legal()) {
      VerificationFailure failure =
          Failure(CandidateLifecycleStage::kExactValidated,
                  CandidateRejectionCode::kExactValidation, "candidate.geometry.swept_clearance.v1",
                  "Exact swept trace validation failed: " + exact.detail);
      failure.primitive_witness_index = index;
      failure.obstacle = exact.obstacle;
      return failure;
    }
    expected = line.centerline.end;
    expected_layer = line.layer;
  }
  if (expected != second->center || expected_layer != canonical_goal_layer) {
    return Failure(CandidateLifecycleStage::kExactValidated,
                   CandidateRejectionCode::kExactValidation, "candidate.geometry.goal.v1",
                   "Candidate does not terminate at the requested exact goal and layer");
  }

  for (const board_ir::Terminal& terminal : board.data().terminals) {
    if (terminal.ref == intended_net->terminals[0] || terminal.ref == intended_net->terminals[1]) {
      continue;
    }
    for (std::size_t index = 0; index < geometry.size(); ++index) {
      const ExactLinePrimitive& line = std::get<ExactLinePrimitive>(geometry[index]);
      const geometry::SegmentClearanceResult clearance = geometry::SweptTraceClearanceAtLeast(
          line.centerline, terminal.connection_region, board.data().routing_profile.nominal_width,
          board.data().routing_profile.clearance);
      if (std::ranges::binary_search(terminal.layers, line.layer) &&
          (!clearance.ok() || !clearance.clearance_satisfied)) {
        VerificationFailure failure =
            Failure(CandidateLifecycleStage::kExactValidated,
                    CandidateRejectionCode::kExactValidation, "candidate.terminals.unintended.v1",
                    "Candidate intersects a terminal outside the intended two-terminal net");
        failure.primitive_witness_index = index;
        return failure;
      }
    }
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<std::uint64_t> Magnitude(Wide value) noexcept {
  const UWide magnitude = value < 0 ? static_cast<UWide>(-value) : static_cast<UWide>(value);
  if (magnitude > std::numeric_limits<std::uint64_t>::max()) {
    return std::nullopt;
  }
  return static_cast<std::uint64_t>(magnitude);
}

[[nodiscard]] bool SpanCanAppend(const PhysicalEdgeSpan& span,
                                 const routing::EdgeResourceKey& next) noexcept {
  if (span.layer != next.layer || span.direction != next.direction || span.usage_units != 1 ||
      span.edge_count == std::numeric_limits<std::uint32_t>::max()) {
    return false;
  }
  const DirectionDelta delta = ResourceSpanStorageDelta(span.direction);
  const Wide expected_x =
      static_cast<Wide>(span.lattice_x) + static_cast<Wide>(delta.x) * span.edge_count;
  const Wide expected_y =
      static_cast<Wide>(span.lattice_y) + static_cast<Wide>(delta.y) * span.edge_count;
  return expected_x == next.lattice_x && expected_y == next.lattice_y;
}

[[nodiscard]] std::variant<DerivedCandidateFields, VerificationFailure> DeriveFields(
    const geometry_compiler::CompiledBoard& compiled_board,
    const routing::CandidateGenerationPolicy& policy,
    std::span<const CandidatePrimitive> geometry) {
  std::vector<routing::EdgeResourceKey> atomic_resources;
  const std::uint64_t maximum_atomic_edges =
      compiled_board.telemetry().legal_directional_edges / 2U;
  CandidateMetrics metrics;
  metrics.line_primitive_count = geometry.size();
  std::uint8_t incoming_direction = routing::kNoIncomingDirection;
  UWide scalar_cost = 0;
  UWide intrinsic_base_cost = 0;
  UWide orthogonal_steps = 0;
  UWide diagonal_steps = 0;
  UWide bends = 0;

  for (std::size_t primitive_index = 0; primitive_index < geometry.size(); ++primitive_index) {
    const ExactLinePrimitive& line = std::get<ExactLinePrimitive>(geometry[primitive_index]);
    const std::optional<LatticeIndex> start = geometry_compiler::ExactPointToLatticeIndex(
        compiled_board.profile(), line.centerline.start);
    const std::optional<LatticeIndex> end =
        geometry_compiler::ExactPointToLatticeIndex(compiled_board.profile(), line.centerline.end);
    if (!start.has_value() || !end.has_value()) {
      VerificationFailure failure = Failure(
          CandidateLifecycleStage::kResourceAccounted, CandidateRejectionCode::kResourceMismatch,
          "candidate.resources.lattice_alignment.v1",
          "Candidate exact geometry is not aligned to the associated compiler lattice");
      failure.primitive_witness_index = primitive_index;
      return failure;
    }
    const Wide delta_x = static_cast<Wide>(end->x) - start->x;
    const Wide delta_y = static_cast<Wide>(end->y) - start->y;
    const std::optional<std::uint64_t> x_magnitude = Magnitude(delta_x);
    const std::optional<std::uint64_t> y_magnitude = Magnitude(delta_y);
    if (!x_magnitude.has_value() || !y_magnitude.has_value()) {
      return Failure(CandidateLifecycleStage::kResourceAccounted,
                     CandidateRejectionCode::kMemoryAccountingOverflow,
                     "candidate.resources.edge_count_overflow.v1",
                     "Candidate lattice edge count cannot be represented");
    }
    const std::uint64_t step_count = std::max(*x_magnitude, *y_magnitude);
    const std::optional<Direction> direction = routing::DirectionBetween(
        *start, LatticeIndex{.x = start->x + (delta_x > 0) - (delta_x < 0),
                             .y = start->y + (delta_y > 0) - (delta_y < 0)});
    if (!direction.has_value() || step_count == 0 ||
        (delta_x != 0 && delta_y != 0 && *x_magnitude != *y_magnitude)) {
      VerificationFailure failure =
          Failure(CandidateLifecycleStage::kResourceAccounted,
                  CandidateRejectionCode::kResourceMismatch, "candidate.resources.heading.v1",
                  "Candidate geometry cannot be expanded into adjacent physical edges");
      failure.primitive_witness_index = primitive_index;
      return failure;
    }
    if (step_count > maximum_atomic_edges ||
        atomic_resources.size() > maximum_atomic_edges - step_count) {
      return Failure(CandidateLifecycleStage::kResourceAccounted,
                     CandidateRejectionCode::kMemoryAccountingOverflow,
                     "candidate.resources.bound.v1",
                     "Candidate physical-edge expansion exceeds the associated board bound");
    }
    const DirectionDelta step = geometry_compiler::DeltaFor(*direction);
    LatticeIndex source = *start;
    for (std::uint64_t step_index = 0; step_index < step_count; ++step_index) {
      if (!compiled_board.EdgeIsLegal(line.layer, source.x, source.y, *direction)) {
        VerificationFailure failure = Failure(
            CandidateLifecycleStage::kResourceAccounted, CandidateRejectionCode::kResourceMismatch,
            "candidate.resources.compiled_edge.v1",
            "Candidate geometry names an edge absent from the associated CompiledBoard");
        failure.primitive_witness_index = primitive_index;
        failure.resource_witness_index = atomic_resources.size();
        return failure;
      }
      const std::optional<routing::EdgeResourceKey> resource =
          routing::CanonicalPhysicalEdgeResource(line.layer, source, *direction);
      if (!resource.has_value()) {
        return Failure(CandidateLifecycleStage::kResourceAccounted,
                       CandidateRejectionCode::kMemoryAccountingOverflow,
                       "candidate.resources.coordinate_overflow.v1",
                       "Canonical physical-edge coordinate overflowed");
      }
      if (routing::PolicyBansResource(policy, *resource)) {
        VerificationFailure failure =
            Failure(CandidateLifecycleStage::kExactValidated,
                    CandidateRejectionCode::kExactValidation, "candidate.policy.banned_resource.v1",
                    "Candidate traverses a request-local banned resource");
        failure.primitive_witness_index = primitive_index;
        failure.resource_witness_index = atomic_resources.size();
        return failure;
      }
      const std::optional<std::uint64_t> transition_cost = routing::StepCostUnderPolicy(
          compiled_board.profile(), *direction, incoming_direction, policy, *resource);
      if (!transition_cost.has_value()) {
        return Failure(CandidateLifecycleStage::kResourceAccounted,
                       CandidateRejectionCode::kMemoryAccountingOverflow,
                       "candidate.metrics.scalar_overflow.v1",
                       "Candidate scalar cost overflowed uint64");
      }
      scalar_cost += *transition_cost;
      intrinsic_base_cost +=
          routing::StepCost(compiled_board.profile(), *direction, incoming_direction);
      if (scalar_cost > std::numeric_limits<std::uint64_t>::max()) {
        return Failure(CandidateLifecycleStage::kResourceAccounted,
                       CandidateRejectionCode::kMemoryAccountingOverflow,
                       "candidate.metrics.scalar_overflow.v1",
                       "Candidate scalar cost overflowed uint64");
      }
      if (intrinsic_base_cost > std::numeric_limits<std::uint64_t>::max()) {
        return Failure(CandidateLifecycleStage::kResourceAccounted,
                       CandidateRejectionCode::kMemoryAccountingOverflow,
                       "candidate.metrics.intrinsic_cost_overflow.v1",
                       "Candidate intrinsic base cost overflowed uint64");
      }
      if (incoming_direction != routing::kNoIncomingDirection &&
          incoming_direction != static_cast<std::uint8_t>(*direction)) {
        ++bends;
      }
      incoming_direction = static_cast<std::uint8_t>(*direction);
      if (geometry_compiler::IsDiagonal(*direction)) {
        ++diagonal_steps;
      } else {
        ++orthogonal_steps;
      }
      atomic_resources.push_back(*resource);
      source.x += step.x;
      source.y += step.y;
    }
  }

  std::ranges::sort(atomic_resources);
  const auto duplicate = std::ranges::adjacent_find(atomic_resources);
  if (duplicate != atomic_resources.end()) {
    VerificationFailure failure =
        Failure(CandidateLifecycleStage::kResourceAccounted,
                CandidateRejectionCode::kResourceMismatch, "candidate.resources.reused_edge.v1",
                "Candidate traverses the same physical edge more than once");
    failure.resource_witness_index =
        static_cast<std::uint64_t>(duplicate - atomic_resources.begin());
    return failure;
  }

  std::vector<PhysicalEdgeSpan> spans;
  spans.reserve(atomic_resources.size());
  for (const routing::EdgeResourceKey& resource : atomic_resources) {
    if (!spans.empty() && SpanCanAppend(spans.back(), resource)) {
      ++spans.back().edge_count;
      continue;
    }
    spans.push_back(PhysicalEdgeSpan{.layer = resource.layer,
                                     .lattice_x = resource.lattice_x,
                                     .lattice_y = resource.lattice_y,
                                     .direction = resource.direction,
                                     .edge_count = 1,
                                     .usage_units = 1});
  }

  const UWide lattice_step = static_cast<UWide>(compiled_board.profile().lattice_step);
  const UWide axis_length = orthogonal_steps * lattice_step;
  const UWide diagonal_projection = diagonal_steps * lattice_step;
  if (orthogonal_steps > std::numeric_limits<std::uint64_t>::max() ||
      diagonal_steps > std::numeric_limits<std::uint64_t>::max() ||
      bends > std::numeric_limits<std::uint64_t>::max() ||
      axis_length > std::numeric_limits<std::uint64_t>::max() ||
      diagonal_projection > std::numeric_limits<std::uint64_t>::max()) {
    return Failure(CandidateLifecycleStage::kResourceAccounted,
                   CandidateRejectionCode::kMemoryAccountingOverflow,
                   "candidate.metrics.length_overflow.v1",
                   "Candidate metric accounting overflowed uint64");
  }
  metrics.scalar_policy_cost = static_cast<std::uint64_t>(scalar_cost);
  metrics.intrinsic_base_cost = static_cast<std::uint64_t>(intrinsic_base_cost);
  metrics.orthogonal_step_count = static_cast<std::uint64_t>(orthogonal_steps);
  metrics.diagonal_step_count = static_cast<std::uint64_t>(diagonal_steps);
  metrics.bend_count = static_cast<std::uint64_t>(bends);
  metrics.axis_aligned_length_dbu = static_cast<std::uint64_t>(axis_length);
  metrics.diagonal_projection_dbu = static_cast<std::uint64_t>(diagonal_projection);
  return DerivedCandidateFields{.resources = std::move(spans), .metrics = metrics};
}

[[nodiscard]] CandidateRejection RejectionFrom(const GeneratedRouteCandidate& candidate,
                                               VerificationFailure failure) {
  CandidateRejection rejection{
      .candidate_id =
          candidate.id.empty() ? std::nullopt : std::optional<CandidateId>(candidate.id),
      .net = candidate.net,
      .stage = failure.stage,
      .code = failure.code,
      .invariant_id = std::move(failure.invariant_id),
      .associations = candidate.associations,
      .policy_identity = candidate.policy_identity,
      .provenance = candidate.provenance,
      .primitive_witness_index = failure.primitive_witness_index,
      .resource_witness_index = failure.resource_witness_index,
      .expected_value = failure.expected_value,
      .actual_value = failure.actual_value,
      .obstacle = failure.obstacle,
      .candidate_payload_checksum = candidate.payload_checksum,
      .detail = std::move(failure.detail),
  };
  if (!text::IsValidUtf8(rejection.invariant_id)) {
    rejection.invariant_id = "candidate.diagnostic.invalid_utf8.v1";
  } else if (rejection.invariant_id.size() > kMaximumCandidateDiagnosticBytes) {
    rejection.invariant_id.resize(
        text::Utf8PrefixBytes(rejection.invariant_id, kMaximumCandidateDiagnosticBytes));
  }
  if (rejection.provenance.supported_device_class.size() > kMaximumCandidateDiagnosticBytes ||
      !text::IsValidUtf8(rejection.provenance.supported_device_class)) {
    rejection.provenance.supported_device_class = "invalid-device-class";
  }
  if (!text::IsValidUtf8(rejection.detail)) {
    rejection.detail = "Candidate diagnostic contained invalid UTF-8";
  } else if (rejection.detail.size() > kMaximumCandidateDiagnosticBytes) {
    rejection.detail.resize(
        text::Utf8PrefixBytes(rejection.detail, kMaximumCandidateDiagnosticBytes));
  }
  rejection.logical_bytes = ComputeRejectionLogicalBytes(rejection).value_or(0);
  return rejection;
}

[[nodiscard]] std::optional<VerificationFailure> ValidateProvenance(
    const GeneratedRouteCandidate& candidate) {
  const CandidateProvenance& provenance = candidate.provenance;
  if (!IsKnownGenerator(provenance.generator) || !IsKnownBackend(provenance.backend) ||
      !ProvenanceCombinationIsSupported(provenance) || provenance.generator_version == 0 ||
      provenance.batch_identity == 0 || provenance.query_identity == 0 ||
      provenance.supported_device_class.empty() ||
      provenance.supported_device_class.size() > kMaximumCandidateDiagnosticBytes ||
      !text::IsValidUtf8(provenance.supported_device_class)) {
    return Failure(CandidateLifecycleStage::kGenerated, CandidateRejectionCode::kInvalidInput,
                   "candidate.provenance.complete.v1",
                   "Candidate provenance is incomplete, invalid, or semantically inconsistent");
  }
  if (provenance.deterministic_seed != candidate.policy.deterministic_seed ||
      provenance.candidate_ordinal != candidate.policy.candidate_ordinal) {
    return Failure(CandidateLifecycleStage::kGenerated,
                   CandidateRejectionCode::kAssociationMismatch, "candidate.provenance.policy.v1",
                   "Candidate provenance seed or ordinal does not match its complete policy");
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<VerificationFailure> ValidateAssociations(
    const CandidateAdmissionContext& context, const GeneratedRouteCandidate& candidate) {
  if (const std::optional<routing::CompiledBoardAssociationIssue> issue =
          routing::ValidateCompiledBoardAssociation(context.board, context.compiled_board);
      issue.has_value()) {
    return Failure(CandidateLifecycleStage::kExactValidated,
                   CandidateRejectionCode::kAssociationMismatch,
                   "candidate.associations.compiled_board.v1",
                   "Supplied CompiledBoard is not associated with the exact BoardSnapshot");
  }
  if (const std::optional<routing::RouteRequestAdmissionIssue> issue =
          routing::ValidateTwoTerminalRouteRequest(context.board, context.compiled_board,
                                                   context.request);
      issue.has_value()) {
    return Failure(CandidateLifecycleStage::kExactValidated,
                   CandidateRejectionCode::kAssociationMismatch,
                   "candidate.associations.route_request.v1",
                   "Supplied route request is not admitted by the exact board contract");
  }
  const CandidateAssociations expected = AssociationsFor(context.board, context.compiled_board);
  if (candidate.associations != expected) {
    return Failure(CandidateLifecycleStage::kExactValidated,
                   CandidateRejectionCode::kAssociationMismatch,
                   "candidate.associations.equivalence.v1",
                   "Candidate Board/compiler/routing/rule associations do not match context");
  }
  if (candidate.net != context.request.net) {
    return Failure(CandidateLifecycleStage::kExactValidated,
                   CandidateRejectionCode::kAssociationMismatch, "candidate.net.association.v1",
                   "Candidate net identity does not match the intended route request");
  }
  const routing::CandidatePolicyResult request_policy_result =
      routing::NormalizeCandidateGenerationPolicy(context.compiled_board,
                                                  context.request.candidate_policy);
  if (!std::holds_alternative<routing::NormalizedCandidateGenerationPolicy>(
          request_policy_result) ||
      std::get<routing::NormalizedCandidateGenerationPolicy>(request_policy_result).identity !=
          candidate.policy_identity ||
      std::get<routing::NormalizedCandidateGenerationPolicy>(request_policy_result).policy !=
          candidate.policy) {
    return Failure(CandidateLifecycleStage::kExactValidated,
                   CandidateRejectionCode::kAssociationMismatch,
                   "candidate.policy.request_association.v1",
                   "Candidate policy does not match the route request policy");
  }
  const board_ir::Net* net = context.board.FindNet(context.request.net);
  if (net == nullptr || net->terminals.size() != 2 ||
      candidate.intended_terminals !=
          std::array<board_ir::EntityRef, 2>{net->terminals[0], net->terminals[1]}) {
    return Failure(CandidateLifecycleStage::kExactValidated,
                   CandidateRejectionCode::kAssociationMismatch,
                   "candidate.terminals.association.v1",
                   "Candidate intended-terminal identities are incomplete or incorrect");
  }
  return std::nullopt;
}

}  // namespace

struct RouteCandidateAdmissionFactory {
  [[nodiscard]] static RouteCandidate Make(GeneratedRouteCandidate data) {
    return RouteCandidate(std::move(data));
  }
};

CandidateAssociations AssociationsFor(
    const board_ir::BoardSnapshot& board,
    const geometry_compiler::CompiledBoard& compiled_board) noexcept {
  return CandidateAssociations{
      .board_content_hash = board.content_hash(),
      .compiler_profile_fingerprint = compiled_board.compiler_profile_fingerprint(),
      .geometry_compiler_version = compiled_board.compiler_version(),
      .routing_profile_fingerprint =
          routing::FingerprintRoutingProfile(board.data().routing_profile),
      .rule_bucket_identity = compiled_board.rule_bucket().identity,
  };
}

CandidateId DeriveCandidateId(board_ir::EntityRef net, const CandidateAssociations& associations,
                              std::uint64_t policy_identity,
                              const CandidateProvenance& provenance) noexcept {
  const auto derive_half = [&](std::string_view domain) {
    CanonicalFieldEncoder encoder(domain);
    EncodeEntityRef(encoder, net);
    EncodeAssociations(encoder, associations);
    encoder.AddU64(policy_identity);
    EncodeProvenance(encoder, provenance);
    return encoder.hash();
  };
  return CandidateId{
      .high = derive_half("APGAR-CANDIDATE-ID-V1-A"),
      .low = derive_half("APGAR-CANDIDATE-ID-V1-B"),
  };
}

CandidateSignature ComputeGeometrySignature(std::span<const CandidatePrimitive> geometry) noexcept {
  const auto derive_half = [&](std::string_view domain) {
    CanonicalFieldEncoder encoder(domain);
    encoder.AddU64(static_cast<std::uint64_t>(geometry.size()));
    for (const CandidatePrimitive& primitive : geometry) {
      EncodePrimitive(encoder, primitive);
    }
    return encoder.hash();
  };
  return CandidateSignature{
      .high = derive_half("APGAR-CANDIDATE-GEOMETRY-V1-A"),
      .low = derive_half("APGAR-CANDIDATE-GEOMETRY-V1-B"),
  };
}

CandidateSignature ComputeResourceSignature(std::span<const PhysicalEdgeSpan> resources) noexcept {
  const auto derive_half = [&](std::string_view domain) {
    CanonicalFieldEncoder encoder(domain);
    encoder.AddU64(static_cast<std::uint64_t>(resources.size()));
    for (const PhysicalEdgeSpan& resource : resources) {
      EncodeResourceSpan(encoder, resource);
    }
    return encoder.hash();
  };
  return CandidateSignature{
      .high = derive_half("APGAR-CANDIDATE-RESOURCES-V1-A"),
      .low = derive_half("APGAR-CANDIDATE-RESOURCES-V1-B"),
  };
}

std::uint64_t ComputeCandidatePayloadChecksum(const GeneratedRouteCandidate& candidate) noexcept {
  CanonicalFieldEncoder encoder("APGAR-ROUTE-CANDIDATE-V1");
  EncodeCandidatePayload(encoder, candidate);
  return encoder.hash();
}

std::optional<std::uint64_t> ComputeCandidateLogicalBytes(
    const GeneratedRouteCandidate& candidate) noexcept {
  CanonicalFieldEncoder encoder("APGAR-ROUTE-CANDIDATE-V1");
  EncodeCandidatePayload(encoder, candidate);
  std::optional<std::uint64_t> bytes = encoder.bytes();
  if (!bytes.has_value()) {
    return std::nullopt;
  }
  bytes = AddBytes(*bytes, sizeof(candidate.payload_checksum));
  if (!bytes.has_value()) {
    return std::nullopt;
  }
  return AddBytes(*bytes, sizeof(candidate.logical_bytes));
}

std::optional<std::uint64_t> ComputeRejectionLogicalBytes(
    const CandidateRejection& rejection) noexcept {
  CanonicalFieldEncoder encoder("APGAR-CANDIDATE-REJECTION-V1");
  encoder.AddU32(rejection.schema_version);
  encoder.AddBool(rejection.candidate_id.has_value());
  if (rejection.candidate_id.has_value()) {
    EncodeHash128(encoder, *rejection.candidate_id);
  }
  encoder.AddBool(rejection.net.has_value());
  if (rejection.net.has_value()) {
    EncodeEntityRef(encoder, *rejection.net);
  }
  encoder.AddByte(static_cast<std::uint8_t>(rejection.stage));
  encoder.AddByte(static_cast<std::uint8_t>(rejection.code));
  encoder.AddString(rejection.invariant_id);
  EncodeAssociations(encoder, rejection.associations);
  encoder.AddU64(rejection.policy_identity);
  EncodeProvenance(encoder, rejection.provenance);
  const auto encode_optional_u64 = [&encoder](std::optional<std::uint64_t> value) {
    encoder.AddBool(value.has_value());
    if (value.has_value()) {
      encoder.AddU64(*value);
    }
  };
  encode_optional_u64(rejection.primitive_witness_index);
  encode_optional_u64(rejection.resource_witness_index);
  encode_optional_u64(rejection.expected_value);
  encode_optional_u64(rejection.actual_value);
  encoder.AddBool(rejection.obstacle.has_value());
  if (rejection.obstacle.has_value()) {
    EncodeEntityRef(encoder, *rejection.obstacle);
  }
  encode_optional_u64(rejection.candidate_payload_checksum);
  encoder.AddString(rejection.detail);
  std::optional<std::uint64_t> bytes = encoder.bytes();
  if (!bytes.has_value()) {
    return std::nullopt;
  }
  return AddBytes(*bytes, sizeof(rejection.logical_bytes));
}

std::optional<CandidateBuildError> FinalizeGeneratedCandidateDraft(
    GeneratedRouteCandidate& candidate) noexcept {
  candidate.geometry_signature = ComputeGeometrySignature(candidate.geometry);
  candidate.resource_signature = ComputeResourceSignature(candidate.resources);
  candidate.payload_checksum = ComputeCandidatePayloadChecksum(candidate);
  const std::optional<std::uint64_t> logical_bytes = ComputeCandidateLogicalBytes(candidate);
  if (!logical_bytes.has_value()) {
    return CandidateBuildError{
        .code = CandidateRejectionCode::kMemoryAccountingOverflow,
        .invariant_id = "candidate.memory.logical_bytes_overflow.v1",
        .detail = "Canonical candidate byte accounting overflowed uint64",
    };
  }
  candidate.logical_bytes = *logical_bytes;
  return std::nullopt;
}

CandidateDraftBuildResult BuildGeneratedCandidateFromPlanarRoute(
    const board_ir::BoardSnapshot& board, const geometry_compiler::CompiledBoard& compiled_board,
    const routing::PlanarRouteRequest& request,
    const routing::NormalizedCandidateGenerationPolicy& normalized_policy,
    const CandidateAssociations& route_associations, std::uint64_t route_policy_identity,
    std::uint64_t reported_scalar_cost, std::span<const routing::LayerSegment> segments,
    CandidateProvenance provenance) {
  if (const std::optional<routing::CompiledBoardAssociationIssue> issue =
          routing::ValidateCompiledBoardAssociation(board, compiled_board);
      issue.has_value()) {
    return CandidateBuildError{
        .code = CandidateRejectionCode::kAssociationMismatch,
        .invariant_id = "candidate.builder.compiled_board_association.v1",
        .detail = "Cannot build a candidate from a stale CompiledBoard",
    };
  }
  if (const std::optional<routing::RouteRequestAdmissionIssue> issue =
          routing::ValidateTwoTerminalRouteRequest(board, compiled_board, request);
      issue.has_value()) {
    return CandidateBuildError{
        .code = CandidateRejectionCode::kInvalidInput,
        .invariant_id = "candidate.builder.route_request.v1",
        .detail = "Cannot build a candidate from an invalid two-terminal request",
    };
  }
  const routing::CandidatePolicyResult renormalized =
      routing::NormalizeCandidateGenerationPolicy(compiled_board, normalized_policy.policy);
  if (!std::holds_alternative<routing::NormalizedCandidateGenerationPolicy>(renormalized) ||
      std::get<routing::NormalizedCandidateGenerationPolicy>(renormalized) != normalized_policy) {
    return CandidateBuildError{
        .code = CandidateRejectionCode::kInvalidInput,
        .invariant_id = "candidate.builder.policy_normalized.v1",
        .detail = "Candidate builder requires a verified normalized policy",
    };
  }
  const routing::CandidatePolicyResult normalized_request_policy =
      routing::NormalizeCandidateGenerationPolicy(compiled_board, request.candidate_policy);
  if (!std::holds_alternative<routing::NormalizedCandidateGenerationPolicy>(
          normalized_request_policy) ||
      std::get<routing::NormalizedCandidateGenerationPolicy>(normalized_request_policy) !=
          normalized_policy) {
    return CandidateBuildError{
        .code = CandidateRejectionCode::kAssociationMismatch,
        .invariant_id = "candidate.builder.request_policy_association.v1",
        .detail = "Normalized candidate policy does not match the route request policy",
    };
  }
  if (route_associations != AssociationsFor(board, compiled_board)) {
    return CandidateBuildError{
        .code = CandidateRejectionCode::kAssociationMismatch,
        .invariant_id = "candidate.builder.planar_route_association.v1",
        .detail = "Planar route associations do not match the candidate context",
    };
  }
  if (route_policy_identity != normalized_policy.identity) {
    return CandidateBuildError{
        .code = CandidateRejectionCode::kAssociationMismatch,
        .invariant_id = "candidate.builder.planar_route_policy_association.v1",
        .detail = "Planar route policy identity does not match candidate provenance",
    };
  }
  if (segments.empty()) {
    return CandidateBuildError{
        .code = CandidateRejectionCode::kInvalidInput,
        .invariant_id = "candidate.builder.planar_route_geometry.v1",
        .detail = "Planar route contains no exact segments",
    };
  }

  const board_ir::Net* net = board.FindNet(request.net);
  if (net == nullptr || net->terminals.size() != 2) {
    return CandidateBuildError{
        .code = CandidateRejectionCode::kAssociationMismatch,
        .invariant_id = "candidate.builder.terminals.v1",
        .detail = "Candidate builder cannot resolve exactly two intended terminals",
    };
  }
  provenance.deterministic_seed = normalized_policy.policy.deterministic_seed;
  provenance.candidate_ordinal = normalized_policy.policy.candidate_ordinal;
  GeneratedRouteCandidate generated;
  generated.net = request.net;
  generated.intended_terminals = {net->terminals[0], net->terminals[1]};
  generated.associations = AssociationsFor(board, compiled_board);
  generated.policy = normalized_policy.policy;
  generated.policy_identity = normalized_policy.identity;
  generated.provenance = std::move(provenance);
  generated.id = DeriveCandidateId(generated.net, generated.associations, generated.policy_identity,
                                   generated.provenance);
  generated.geometry.reserve(segments.size());
  for (const routing::LayerSegment& segment : segments) {
    generated.geometry.emplace_back(ExactLinePrimitive{
        .layer = segment.layer,
        .centerline = segment.centerline,
    });
  }

  std::variant<std::vector<CandidatePrimitive>, VerificationFailure> normalized_geometry =
      NormalizeGeometry(board, generated.geometry);
  if (std::holds_alternative<VerificationFailure>(normalized_geometry)) {
    const VerificationFailure& failure = std::get<VerificationFailure>(normalized_geometry);
    return CandidateBuildError{
        .code = failure.code, .invariant_id = failure.invariant_id, .detail = failure.detail};
  }
  generated.geometry = std::get<std::vector<CandidatePrimitive>>(std::move(normalized_geometry));
  if (const std::optional<VerificationFailure> failure = ValidateExactGeometry(
          board, request, generated.geometry, kMaximumCandidateSelfClearancePairChecks);
      failure.has_value()) {
    return CandidateBuildError{
        .code = failure->code, .invariant_id = failure->invariant_id, .detail = failure->detail};
  }
  std::variant<DerivedCandidateFields, VerificationFailure> derived =
      DeriveFields(compiled_board, generated.policy, generated.geometry);
  if (std::holds_alternative<VerificationFailure>(derived)) {
    const VerificationFailure& failure = std::get<VerificationFailure>(derived);
    return CandidateBuildError{
        .code = failure.code, .invariant_id = failure.invariant_id, .detail = failure.detail};
  }
  DerivedCandidateFields fields = std::get<DerivedCandidateFields>(std::move(derived));
  if (reported_scalar_cost != fields.metrics.scalar_policy_cost) {
    return CandidateBuildError{
        .code = CandidateRejectionCode::kMetricMismatch,
        .invariant_id = "candidate.builder.planar_route_cost.v1",
        .detail = "Planar route scalar cost does not match independent policy reconstruction",
    };
  }
  generated.resources = std::move(fields.resources);
  generated.metrics = fields.metrics;
  generated.constraints = ConstraintAssessment{
      .supported_hard_constraints_satisfied = true,
      .unsupported_rules_remain = false,
      .connected_intended_terminal_count = 2,
      .exact_validation_code = CandidateExactValidationCode::kPassed,
  };
  if (std::optional<CandidateBuildError> error = FinalizeGeneratedCandidateDraft(generated);
      error.has_value()) {
    return std::move(*error);
  }
  return generated;
}

CandidateDraftBuildResult BuildGeneratedCandidateFromCpuRoute(
    const board_ir::BoardSnapshot& board, const geometry_compiler::CompiledBoard& compiled_board,
    const routing::CpuRouteRequest& request,
    const routing::NormalizedCandidateGenerationPolicy& normalized_policy,
    const routing::CpuRoute& route, CandidateProvenance provenance) {
  CandidateAssociations route_associations = AssociationsFor(board, compiled_board);
  route_associations.board_content_hash = route.source_board_content_hash;
  route_associations.compiler_profile_fingerprint = route.compiler_profile_fingerprint;
  route_associations.geometry_compiler_version = route.compiler_version;
  route_associations.rule_bucket_identity = route.rule_bucket_identity;
  return BuildGeneratedCandidateFromPlanarRoute(
      board, compiled_board, request, normalized_policy, route_associations,
      route.candidate_policy_identity, route.total_cost, route.segments, std::move(provenance));
}

CandidateAdmissionResult AdmitRouteCandidateWithReducedSelfClearanceBudgetForTesting(
    const CandidateAdmissionContext& context, GeneratedRouteCandidate generated,
    std::uint64_t maximum_pair_checks) {
  if (generated.schema_major != kRouteCandidateSchemaMajor ||
      generated.schema_minor != kRouteCandidateSchemaMinor ||
      generated.geometry_schema_version != kCandidateGeometrySchemaVersion ||
      generated.resource_schema_version != kCandidateResourceSchemaVersion) {
    return RejectionFrom(
        generated,
        Failure(CandidateLifecycleStage::kGenerated, CandidateRejectionCode::kUnsupported,
                "candidate.schema.compatibility.v1",
                "Candidate schema or signature version is incompatible with v1.0"));
  }
  if (generated.id.empty()) {
    return RejectionFrom(
        generated,
        Failure(CandidateLifecycleStage::kGenerated, CandidateRejectionCode::kInvalidInput,
                "candidate.identity.nonzero.v1", "Candidate identity must be nonzero"));
  }

  const routing::CandidatePolicyResult policy_result =
      routing::NormalizeCandidateGenerationPolicy(context.compiled_board, generated.policy);
  if (!std::holds_alternative<routing::NormalizedCandidateGenerationPolicy>(policy_result)) {
    return RejectionFrom(
        generated,
        Failure(CandidateLifecycleStage::kGenerated, CandidateRejectionCode::kInvalidInput,
                "candidate.policy.normalization.v1",
                "Candidate policy is invalid for the associated CompiledBoard: " +
                    std::get<routing::CandidatePolicyError>(policy_result).detail));
  }
  const routing::NormalizedCandidateGenerationPolicy& normalized_policy =
      std::get<routing::NormalizedCandidateGenerationPolicy>(policy_result);
  if (generated.policy_identity != normalized_policy.identity) {
    VerificationFailure failure =
        Failure(CandidateLifecycleStage::kGenerated, CandidateRejectionCode::kAssociationMismatch,
                "candidate.policy.identity.v1",
                "Candidate policy identity does not match its normalized complete policy");
    failure.expected_value = normalized_policy.identity;
    failure.actual_value = generated.policy_identity;
    return RejectionFrom(generated, std::move(failure));
  }
  generated.policy = normalized_policy.policy;
  if (const std::optional<VerificationFailure> failure = ValidateProvenance(generated);
      failure.has_value()) {
    return RejectionFrom(generated, *failure);
  }
  const CandidateId expected_id = DeriveCandidateId(
      generated.net, generated.associations, generated.policy_identity, generated.provenance);
  if (generated.id != expected_id) {
    return RejectionFrom(
        generated,
        Failure(CandidateLifecycleStage::kGenerated, CandidateRejectionCode::kAssociationMismatch,
                "candidate.identity.derivation.v1",
                "Candidate ID does not match deterministic provenance derivation"));
  }

  std::variant<std::vector<CandidatePrimitive>, VerificationFailure> geometry_result =
      NormalizeGeometry(context.board, generated.geometry);
  if (std::holds_alternative<VerificationFailure>(geometry_result)) {
    return RejectionFrom(generated, std::get<VerificationFailure>(std::move(geometry_result)));
  }
  generated.geometry = std::get<std::vector<CandidatePrimitive>>(std::move(geometry_result));

  if (const std::optional<VerificationFailure> failure = ValidateAssociations(context, generated);
      failure.has_value()) {
    return RejectionFrom(generated, *failure);
  }
  if (const std::optional<VerificationFailure> failure = ValidateExactGeometry(
          context.board, context.request, generated.geometry,
          std::min(maximum_pair_checks, kMaximumCandidateSelfClearancePairChecks));
      failure.has_value()) {
    return RejectionFrom(generated, *failure);
  }

  std::variant<DerivedCandidateFields, VerificationFailure> derived_result =
      DeriveFields(context.compiled_board, generated.policy, generated.geometry);
  if (std::holds_alternative<VerificationFailure>(derived_result)) {
    return RejectionFrom(generated, std::get<VerificationFailure>(std::move(derived_result)));
  }
  DerivedCandidateFields derived = std::get<DerivedCandidateFields>(std::move(derived_result));
  if (generated.resources != derived.resources) {
    return RejectionFrom(
        generated,
        Failure(CandidateLifecycleStage::kResourceAccounted,
                CandidateRejectionCode::kResourceMismatch, "candidate.resources.equivalence.v1",
                "Supplied compressed footprint differs from exact geometry recomputation"));
  }
  generated.resources = std::move(derived.resources);
  if (generated.metrics != derived.metrics) {
    const bool scalar_mismatch =
        generated.metrics.scalar_policy_cost != derived.metrics.scalar_policy_cost;
    VerificationFailure failure = Failure(
        CandidateLifecycleStage::kResourceAccounted, CandidateRejectionCode::kMetricMismatch,
        scalar_mismatch ? "candidate.metrics.scalar_cost.v1" : "candidate.metrics.equivalence.v1",
        "Supplied metrics differ from independent exact/resource reconstruction");
    if (scalar_mismatch) {
      failure.expected_value = derived.metrics.scalar_policy_cost;
      failure.actual_value = generated.metrics.scalar_policy_cost;
    }
    return RejectionFrom(generated, std::move(failure));
  }
  generated.metrics = derived.metrics;

  const ConstraintAssessment expected_constraints{
      .supported_hard_constraints_satisfied = true,
      .unsupported_rules_remain = false,
      .connected_intended_terminal_count = 2,
      .exact_validation_code = CandidateExactValidationCode::kPassed,
  };
  if (generated.constraints != expected_constraints) {
    return RejectionFrom(
        generated,
        Failure(CandidateLifecycleStage::kExactValidated, CandidateRejectionCode::kExactValidation,
                "candidate.constraints.equivalence.v1",
                "Constraint assessment does not match exact admission evidence"));
  }
  generated.constraints = expected_constraints;

  const CandidateSignature expected_geometry_signature =
      ComputeGeometrySignature(generated.geometry);
  if (generated.geometry_signature != expected_geometry_signature) {
    return RejectionFrom(
        generated,
        Failure(CandidateLifecycleStage::kSignedAndDeduplicated,
                CandidateRejectionCode::kSignatureMismatch, "candidate.signature.geometry.v1",
                "Geometry signature does not match canonical geometry"));
  }
  const CandidateSignature expected_resource_signature =
      ComputeResourceSignature(generated.resources);
  if (generated.resource_signature != expected_resource_signature) {
    return RejectionFrom(
        generated,
        Failure(CandidateLifecycleStage::kSignedAndDeduplicated,
                CandidateRejectionCode::kSignatureMismatch, "candidate.signature.resources.v1",
                "Resource signature does not match the canonical footprint"));
  }
  generated.geometry_signature = expected_geometry_signature;
  generated.resource_signature = expected_resource_signature;

  const std::uint64_t expected_checksum = ComputeCandidatePayloadChecksum(generated);
  if (generated.payload_checksum != expected_checksum) {
    VerificationFailure failure =
        Failure(CandidateLifecycleStage::kSignedAndDeduplicated,
                CandidateRejectionCode::kSignatureMismatch, "candidate.checksum.payload.v1",
                "Candidate payload checksum does not match its canonical encoding");
    failure.expected_value = expected_checksum;
    failure.actual_value = generated.payload_checksum;
    return RejectionFrom(generated, std::move(failure));
  }
  const std::optional<std::uint64_t> expected_logical_bytes =
      ComputeCandidateLogicalBytes(generated);
  if (!expected_logical_bytes.has_value()) {
    return RejectionFrom(generated,
                         Failure(CandidateLifecycleStage::kSignedAndDeduplicated,
                                 CandidateRejectionCode::kMemoryAccountingOverflow,
                                 "candidate.memory.logical_bytes_overflow.v1",
                                 "Canonical candidate byte accounting overflowed uint64"));
  }
  if (generated.logical_bytes != *expected_logical_bytes) {
    VerificationFailure failure =
        Failure(CandidateLifecycleStage::kSignedAndDeduplicated,
                CandidateRejectionCode::kMemoryAccountingMismatch,
                "candidate.memory.logical_bytes_mismatch.v1",
                "Declared candidate logical bytes differ from canonical accounting");
    failure.expected_value = *expected_logical_bytes;
    failure.actual_value = generated.logical_bytes;
    return RejectionFrom(generated, std::move(failure));
  }
  return RouteCandidateAdmissionFactory::Make(std::move(generated));
}

CandidateAdmissionResult AdmitRouteCandidate(const CandidateAdmissionContext& context,
                                             GeneratedRouteCandidate generated) {
  return AdmitRouteCandidateWithReducedSelfClearanceBudgetForTesting(
      context, std::move(generated), kMaximumCandidateSelfClearancePairChecks);
}

bool CanonicalGeometryEqual(const RouteCandidate& left, const RouteCandidate& right) noexcept {
  return left.data().geometry == right.data().geometry;
}

bool CanonicalResourcesEqual(const RouteCandidate& left, const RouteCandidate& right) noexcept {
  return left.data().resources == right.data().resources;
}

}  // namespace apgar::candidates
