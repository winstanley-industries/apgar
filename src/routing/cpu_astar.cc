#include "apgar/routing/cpu_astar.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <queue>
#include <ranges>
#include <span>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "apgar/board_ir/stable_hash.h"
#include "apgar/geometry/exact.h"
#include "src/routing/cpu_astar_internal.h"

namespace apgar::routing {
namespace {

using geometry_compiler::CompiledBoard;
using geometry_compiler::CompilerProfile;
using geometry_compiler::Direction;
using geometry_compiler::DirectionDelta;
using geometry_compiler::kStableDirectionOrder;
using geometry_compiler::LatticeIndex;
using Wide = __int128_t;
using UWide = __uint128_t;

struct SearchState {
  std::int64_t x;
  std::int64_t y;
  std::uint8_t incoming_direction;

  friend bool operator==(const SearchState&, const SearchState&) = default;
};

struct SearchStateHash {
  [[nodiscard]] std::size_t operator()(const SearchState& state) const noexcept {
    board_ir::StableHashBuilder hash;
    hash.AddI64(state.x);
    hash.AddI64(state.y);
    hash.AddByte(state.incoming_direction);
    return static_cast<std::size_t>(hash.Finish());
  }
};

struct SearchRecord {
  std::uint64_t cost;
  std::optional<SearchState> predecessor;
};

struct QueueItem {
  std::uint64_t estimated_total;
  std::uint64_t heuristic;
  std::uint64_t cost;
  SearchState state;
  std::uint64_t sequence;
};

struct QueueGreater {
  [[nodiscard]] bool operator()(const QueueItem& left, const QueueItem& right) const noexcept {
    return std::tie(left.estimated_total, left.heuristic, left.cost, left.state.y, left.state.x,
                    left.state.incoming_direction, left.sequence) >
           std::tie(right.estimated_total, right.heuristic, right.cost, right.state.y,
                    right.state.x, right.state.incoming_direction, right.sequence);
  }
};

[[nodiscard]] RouteFailure Failure(RouteFailureCode code, std::string detail,
                                   std::optional<board_ir::EntityRef> obstacle = std::nullopt,
                                   std::optional<CpuRouteTelemetry> telemetry = std::nullopt) {
  return RouteFailure{
      .code = code, .detail = std::move(detail), .obstacle = obstacle, .telemetry = telemetry};
}

[[nodiscard]] RouteFailure AssociationFailure(CompiledBoardAssociationIssue issue) {
  switch (issue) {
    case CompiledBoardAssociationIssue::kSourceBoardMismatch:
      return Failure(RouteFailureCode::kValidationFailed,
                     "Compiled board source hash does not match the exact BoardSnapshot");
    case CompiledBoardAssociationIssue::kCompilerVersionMismatch:
      return Failure(RouteFailureCode::kValidationFailed,
                     "Compiled board compiler version is not supported");
    case CompiledBoardAssociationIssue::kProfileFingerprintMismatch:
      return Failure(RouteFailureCode::kValidationFailed,
                     "Compiled board profile fingerprint does not match its profile payload");
    case CompiledBoardAssociationIssue::kRuleBucketMismatch:
      return Failure(RouteFailureCode::kValidationFailed,
                     "Compiled board rule bucket is stale or does not match the BoardSnapshot");
  }
  return Failure(RouteFailureCode::kInternalInvariant,
                 "Compiled-board association validator returned an unknown issue");
}

[[nodiscard]] RouteFailure AdmissionFailure(RouteRequestAdmissionIssue issue) {
  switch (issue) {
    case RouteRequestAdmissionIssue::kRoutingProfileNetMismatch:
      return Failure(RouteFailureCode::kInvalidRequest,
                     "CPU route request does not name the Board IR routing-profile net");
    case RouteRequestAdmissionIssue::kInvalidOrCoincidentEndpoints:
      return Failure(RouteFailureCode::kInvalidRequest,
                     "CPU route endpoints must be distinct valid exact coordinates");
    case RouteRequestAdmissionIssue::kRequiresExactlyTwoTerminals:
      return Failure(RouteFailureCode::kInvalidRequest,
                     "M1 CPU routing requires exactly two target terminals");
    case RouteRequestAdmissionIssue::kMissingTerminal:
      return Failure(RouteFailureCode::kInvalidRequest,
                     "CPU route request refers to missing target terminals");
    case RouteRequestAdmissionIssue::kEndpointsNotTerminalCenters:
      return Failure(RouteFailureCode::kInvalidRequest,
                     "M1 CPU route endpoints must be the two exact terminal centers");
    case RouteRequestAdmissionIssue::kUnsupportedTerminalLayer:
      return Failure(RouteFailureCode::kInvalidRequest,
                     "Requested endpoint layer is not a terminal connection layer");
    case RouteRequestAdmissionIssue::kLayerOutsideRuleBucket:
      return Failure(RouteFailureCode::kInvalidRequest,
                     "Requested endpoint layer is not in the compiled M1 rule bucket");
  }
  return Failure(RouteFailureCode::kInternalInvariant,
                 "Route-request admission validator returned an unknown issue");
}

[[nodiscard]] RouteFailure PolicyFailure(const CandidatePolicyError& error) {
  switch (error.code) {
    case CandidatePolicyErrorCode::kUnsupportedSchema:
    case CandidatePolicyErrorCode::kUnsupportedObjective:
      return Failure(RouteFailureCode::kUnsupportedPolicy,
                     "CPU candidate policy is unsupported: " + error.detail);
    case CandidatePolicyErrorCode::kInvalidResource:
    case CandidatePolicyErrorCode::kConflictingResourceAction:
    case CandidatePolicyErrorCode::kTooManyResources:
    case CandidatePolicyErrorCode::kCostOverflow:
    case CandidatePolicyErrorCode::kInvalidAlternativeSchedule:
      return Failure(RouteFailureCode::kInvalidRequest,
                     "CPU candidate policy is invalid: " + error.detail);
  }
  return Failure(RouteFailureCode::kInternalInvariant,
                 "Candidate-policy normalization returned an unknown error");
}

[[nodiscard]] std::uint64_t AbsoluteDifference(std::int64_t left, std::int64_t right) noexcept {
  const Wide difference = static_cast<Wide>(left) - right;
  return static_cast<std::uint64_t>(difference < 0 ? -difference : difference);
}

[[nodiscard]] std::uint64_t Heuristic(const CompilerProfile& profile,
                                      const CandidateGenerationPolicy& policy, LatticeIndex point,
                                      LatticeIndex goal) noexcept {
  const std::uint64_t delta_x = AbsoluteDifference(point.x, goal.x);
  const std::uint64_t delta_y = AbsoluteDifference(point.y, goal.y);
  const bool horizontal = (profile.heading_mask &
                           static_cast<board_ir::HeadingMask>(board_ir::Heading::kHorizontal)) != 0;
  const bool vertical = (profile.heading_mask &
                         static_cast<board_ir::HeadingMask>(board_ir::Heading::kVertical)) != 0;
  const bool diagonal = (profile.heading_mask &
                         static_cast<board_ir::HeadingMask>(board_ir::Heading::kDiagonal45)) != 0;
  const std::uint64_t orthogonal_step =
      static_cast<std::uint64_t>(profile.costs.orthogonal_step) + policy.orthogonal_step_surcharge;
  const std::uint64_t diagonal_step =
      static_cast<std::uint64_t>(profile.costs.diagonal_step) + policy.diagonal_step_surcharge;

  UWide estimate = 0;
  if (horizontal && vertical) {
    if (diagonal) {
      if (diagonal_step < orthogonal_step) {
        // Cheap alternating diagonals can make axial progress for less than an
        // orthogonal step. max(dx, dy) * diagonal_cost is a deliberately
        // relaxed lower bound that remains admissible across parity and bends.
        estimate = static_cast<UWide>(std::max(delta_x, delta_y)) * diagonal_step;
      } else {
        const std::uint64_t diagonal_steps = std::min(delta_x, delta_y);
        const std::uint64_t orthogonal_steps = std::max(delta_x, delta_y) - diagonal_steps;
        const std::uint64_t effective_diagonal = static_cast<std::uint64_t>(
            std::min<UWide>(diagonal_step, static_cast<UWide>(orthogonal_step) * 2U));
        estimate = static_cast<UWide>(diagonal_steps) * effective_diagonal +
                   static_cast<UWide>(orthogonal_steps) * orthogonal_step;
      }
    } else {
      estimate = (static_cast<UWide>(delta_x) + delta_y) * orthogonal_step;
    }
  } else if (horizontal && !vertical && !diagonal) {
    estimate = static_cast<UWide>(delta_x) * orthogonal_step;
  } else if (vertical && !horizontal && !diagonal) {
    estimate = static_cast<UWide>(delta_y) * orthogonal_step;
  } else if (diagonal) {
    std::uint64_t minimum_step_cost = diagonal_step;
    if (horizontal || vertical) {
      minimum_step_cost = std::min(minimum_step_cost, orthogonal_step);
    }
    estimate = static_cast<UWide>(std::max(delta_x, delta_y)) * minimum_step_cost;
  } else {
    // Profile validation rejects this empty-heading case.
    return 0;
  }
  if (estimate > std::numeric_limits<std::uint64_t>::max()) {
    return 0;
  }
  return static_cast<std::uint64_t>(estimate);
}

[[nodiscard]] std::uint64_t EstimatedTotal(std::uint64_t cost, std::uint64_t heuristic) noexcept {
  return CheckedAddFiniteRouteCost(cost, heuristic)
      .value_or(std::numeric_limits<std::uint64_t>::max());
}

[[nodiscard]] std::optional<RouteFailure> ValidateExactSegments(
    const board_ir::BoardSnapshot& board, const CpuRouteRequest& request,
    std::span<const LayerSegment> segments) {
  if (request.start_layer != request.goal_layer) {
    return Failure(RouteFailureCode::kUnsupportedLayerTransition,
                   "M1 compiled fields are planar; exact through-via transitions are not defined");
  }
  if (segments.empty()) {
    return Failure(RouteFailureCode::kValidationFailed,
                   "Reconstructed route contains no line segments");
  }
  board_ir::Point64 expected_start = request.start;
  for (const LayerSegment& segment : segments) {
    if (segment.layer != request.start_layer) {
      return Failure(RouteFailureCode::kValidationFailed,
                     "Reconstructed route changes layers without an exact via primitive");
    }
    if (segment.centerline.start != expected_start) {
      return Failure(RouteFailureCode::kValidationFailed,
                     "Reconstructed route segments are not exactly contiguous");
    }
    const geometry::MovementValidationResult exact =
        geometry::ValidateMovement(board, segment.layer, segment.centerline);
    if (!exact.legal()) {
      return Failure(RouteFailureCode::kValidationFailed,
                     "Exact reconstructed-route validation failed: " + exact.detail,
                     exact.obstacle);
    }
    expected_start = segment.centerline.end;
  }
  if (expected_start != request.goal) {
    return Failure(RouteFailureCode::kValidationFailed,
                   "Reconstructed route does not terminate at the exact requested goal");
  }
  return std::nullopt;
}

}  // namespace

bool CpuRouteHasAuthenticatedAStarEvidence(const CpuRoute& route) noexcept {
  const std::shared_ptr<const CpuRouteProducerEvidence>& evidence =
      route.producer_evidence.evidence;
  return evidence != nullptr &&
         evidence->source_board_content_hash == route.source_board_content_hash &&
         evidence->compiler_profile_fingerprint == route.compiler_profile_fingerprint &&
         evidence->compiler_version == route.compiler_version &&
         evidence->rule_bucket_identity == route.rule_bucket_identity &&
         evidence->candidate_policy_identity == route.candidate_policy_identity &&
         evidence->total_cost == route.total_cost && evidence->segments == route.segments;
}

std::optional<RouteFailure> ValidateReconstructedRoute(const board_ir::BoardSnapshot& board,
                                                       const CompiledBoard& compiled_board,
                                                       const CpuRouteRequest& request,
                                                       std::span<const LayerSegment> segments) {
  if (std::optional<CompiledBoardAssociationIssue> association =
          ValidateCompiledBoardAssociation(board, compiled_board);
      association.has_value()) {
    return AssociationFailure(*association);
  }
  if (std::optional<RouteRequestAdmissionIssue> invalid =
          ValidateTwoTerminalRouteRequest(board, compiled_board, request);
      invalid.has_value()) {
    return AdmissionFailure(*invalid);
  }
  CandidatePolicyResult normalized_policy =
      NormalizeCandidateGenerationPolicy(compiled_board, request.candidate_policy);
  if (std::holds_alternative<CandidatePolicyError>(normalized_policy)) {
    return PolicyFailure(std::get<CandidatePolicyError>(normalized_policy));
  }
  return ValidateExactSegments(board, request, segments);
}

std::optional<RouteFailure> ValidateReconstructedRouteWithNormalizedPolicy(
    const board_ir::BoardSnapshot& board, const CompiledBoard& compiled_board,
    const CpuRouteRequest& request, const NormalizedCandidateGenerationPolicy& normalized_policy,
    std::span<const LayerSegment> segments) {
  if (std::optional<CompiledBoardAssociationIssue> association =
          ValidateCompiledBoardAssociation(board, compiled_board);
      association.has_value()) {
    return AssociationFailure(*association);
  }
  if (std::optional<RouteRequestAdmissionIssue> invalid =
          ValidateTwoTerminalRouteRequest(board, compiled_board, request);
      invalid.has_value()) {
    return AdmissionFailure(*invalid);
  }
  if (!CandidateGenerationPolicyShapeIsWithinV1Bounds(normalized_policy.policy) ||
      FingerprintCandidateGenerationPolicy(normalized_policy.policy) !=
          normalized_policy.identity) {
    return Failure(RouteFailureCode::kInternalInvariant,
                   "Preflight-normalized candidate policy has a stale identity");
  }
  return ValidateExactSegments(board, request, segments);
}

CpuRouteResult RouteWithCpuAStar(const board_ir::BoardSnapshot& board,
                                 const CompiledBoard& compiled_board,
                                 const CpuRouteRequest& request) {
  if (std::optional<CompiledBoardAssociationIssue> association =
          ValidateCompiledBoardAssociation(board, compiled_board);
      association.has_value()) {
    return AssociationFailure(*association);
  }
  if (std::optional<RouteRequestAdmissionIssue> invalid =
          ValidateTwoTerminalRouteRequest(board, compiled_board, request);
      invalid.has_value()) {
    return AdmissionFailure(*invalid);
  }
  if (request.start_layer != request.goal_layer) {
    return Failure(RouteFailureCode::kUnsupportedLayerTransition,
                   "Planar M1 CPU A* cannot invent via padstack legality");
  }

  const CompilerProfile& profile = compiled_board.profile();
  const PlanarEndpointResult endpoint_result = ResolvePlanarEndpoints(compiled_board, request);
  if (std::holds_alternative<PlanarEndpointIssue>(endpoint_result)) {
    if (std::get<PlanarEndpointIssue>(endpoint_result) ==
        PlanarEndpointIssue::kNotOnCompilerLattice) {
      return Failure(RouteFailureCode::kInvalidRequest,
                     "CPU route endpoints must lie exactly on the compiler lattice");
    }
    return Failure(RouteFailureCode::kInvalidRequest,
                   "CPU route endpoints must both be represented by active sparse tiles");
  }
  const ResolvedPlanarEndpoints& endpoints = std::get<ResolvedPlanarEndpoints>(endpoint_result);
  const LatticeIndex& start = endpoints.start;
  const LatticeIndex& goal = endpoints.goal;
  CandidatePolicyResult normalized_policy_result =
      NormalizeCandidateGenerationPolicy(compiled_board, request.candidate_policy);
  if (std::holds_alternative<CandidatePolicyError>(normalized_policy_result)) {
    return PolicyFailure(std::get<CandidatePolicyError>(normalized_policy_result));
  }
  const NormalizedCandidateGenerationPolicy& normalized_policy =
      std::get<NormalizedCandidateGenerationPolicy>(normalized_policy_result);
  const CandidateGenerationPolicy& policy = normalized_policy.policy;

  const SearchState start_state{
      .x = start.x,
      .y = start.y,
      .incoming_direction = kNoIncomingDirection,
  };
  std::unordered_map<SearchState, SearchRecord, SearchStateHash> records;
  records.reserve(static_cast<std::size_t>(
      std::min<std::uint64_t>(compiled_board.telemetry().represented_nodes * 2, 1'000'000)));
  records.emplace(start_state, SearchRecord{.cost = 0, .predecessor = std::nullopt});
  std::priority_queue<QueueItem, std::vector<QueueItem>, QueueGreater> queue;
  const std::uint64_t start_heuristic = Heuristic(profile, policy, start, goal);
  std::uint64_t sequence = 0;
  queue.push(QueueItem{.estimated_total = start_heuristic,
                       .heuristic = start_heuristic,
                       .cost = 0,
                       .state = start_state,
                       .sequence = sequence++});
  CpuRouteTelemetry telemetry{
      .peak_record_count = records.size(),
      .peak_queue_size = queue.size(),
  };

  std::optional<SearchState> goal_state;
  while (!queue.empty()) {
    const QueueItem current = queue.top();
    queue.pop();
    ++telemetry.queue_pops;
    const auto current_record = records.find(current.state);
    if (current_record == records.end() || current_record->second.cost != current.cost) {
      continue;
    }
    if (current.state.x == goal.x && current.state.y == goal.y) {
      goal_state = current.state;
      break;
    }
    ++telemetry.expanded_states;

    const geometry_compiler::CompiledNode* current_node =
        compiled_board.FindNode(request.start_layer, current.state.x, current.state.y);
    if (current_node == nullptr) {
      return Failure(RouteFailureCode::kInternalInvariant,
                     "A* expanded a state outside the represented sparse field", std::nullopt,
                     telemetry);
    }

    for (Direction direction : kStableDirectionOrder) {
      const bool mask_is_legal =
          (current_node->legal_edges & geometry_compiler::MaskFor(direction)) != 0;
      if ((profile.heading_mask & geometry_compiler::HeadingFor(direction)) == 0) {
        if (mask_is_legal) {
          return Failure(RouteFailureCode::kValidationFailed,
                         "Compiled mask enables a direction excluded by its profile", std::nullopt,
                         telemetry);
        }
        continue;
      }
      if (!mask_is_legal) {
        continue;
      }
      ++telemetry.attempted_relaxations;
      const std::optional<EdgeResourceKey> resource = CanonicalPhysicalEdgeResource(
          request.start_layer, LatticeIndex{.x = current.state.x, .y = current.state.y}, direction);
      if (!resource.has_value()) {
        return Failure(RouteFailureCode::kInternalInvariant,
                       "A* could not canonicalize a compiled physical edge", std::nullopt,
                       telemetry);
      }
      const DirectionDelta delta = geometry_compiler::DeltaFor(direction);
      const __int128_t neighbor_x = static_cast<__int128_t>(current.state.x) + delta.x;
      const __int128_t neighbor_y = static_cast<__int128_t>(current.state.y) + delta.y;
      if (neighbor_x < std::numeric_limits<std::int64_t>::min() ||
          neighbor_x > std::numeric_limits<std::int64_t>::max() ||
          neighbor_y < std::numeric_limits<std::int64_t>::min() ||
          neighbor_y > std::numeric_limits<std::int64_t>::max() ||
          !compiled_board.ContainsNode(request.start_layer, static_cast<std::int64_t>(neighbor_x),
                                       static_cast<std::int64_t>(neighbor_y))) {
        return Failure(RouteFailureCode::kInternalInvariant,
                       "Compiled legal edge points outside the represented sparse field",
                       std::nullopt, telemetry);
      }
      const SearchState neighbor{
          .x = static_cast<std::int64_t>(neighbor_x),
          .y = static_cast<std::int64_t>(neighbor_y),
          .incoming_direction = static_cast<std::uint8_t>(direction),
      };
      // Policy is request-local guidance, never a way to suppress validation of
      // the immutable CompiledBoard representation. Check the compiled-edge
      // destination before a ban is allowed to skip relaxation.
      if (PolicyBansResource(policy, *resource)) {
        continue;
      }
      const std::optional<std::uint64_t> transition_cost = StepCostUnderPolicy(
          profile, direction, current.state.incoming_direction, policy, *resource);
      if (!transition_cost.has_value()) {
        return Failure(RouteFailureCode::kInternalInvariant,
                       "A normalized candidate policy produced an overflowing transition cost",
                       std::nullopt, telemetry);
      }
      const std::optional<std::uint64_t> next_cost =
          CheckedAddFiniteRouteCost(current.cost, *transition_cost);
      if (!next_cost.has_value()) {
        return Failure(RouteFailureCode::kResourceExhausted,
                       "Integer route cost overflowed the validated search envelope", std::nullopt,
                       telemetry);
      }
      const auto existing = records.find(neighbor);
      if (existing != records.end() && existing->second.cost <= *next_cost) {
        continue;
      }
      records.insert_or_assign(neighbor,
                               SearchRecord{.cost = *next_cost, .predecessor = current.state});
      ++telemetry.accepted_relaxations;
      const std::uint64_t heuristic =
          Heuristic(profile, policy, LatticeIndex{.x = neighbor.x, .y = neighbor.y}, goal);
      queue.push(QueueItem{
          .estimated_total = EstimatedTotal(*next_cost, heuristic),
          .heuristic = heuristic,
          .cost = *next_cost,
          .state = neighbor,
          .sequence = sequence++,
      });
      telemetry.peak_record_count =
          std::max<std::uint64_t>(telemetry.peak_record_count, records.size());
      telemetry.peak_queue_size = std::max<std::uint64_t>(telemetry.peak_queue_size, queue.size());
    }
  }

  if (!goal_state.has_value()) {
    return Failure(RouteFailureCode::kDisconnected,
                   "No planar path connects the represented start and goal fields", std::nullopt,
                   telemetry);
  }

  std::vector<SearchState> reversed_states;
  SearchState cursor = *goal_state;
  const std::uint64_t maximum_reconstruction_states =
      compiled_board.telemetry().represented_nodes * kStableDirectionOrder.size() + 1;
  while (true) {
    reversed_states.push_back(cursor);
    if (cursor == start_state) {
      break;
    }
    if (reversed_states.size() > maximum_reconstruction_states) {
      return Failure(RouteFailureCode::kInternalInvariant,
                     "A* predecessor reconstruction exceeded the acyclic state bound", std::nullopt,
                     telemetry);
    }
    const auto record = records.find(cursor);
    if (record == records.end() || !record->second.predecessor.has_value()) {
      return Failure(RouteFailureCode::kInternalInvariant,
                     "A* predecessor reconstruction encountered a missing state", std::nullopt,
                     telemetry);
    }
    cursor = *record->second.predecessor;
  }
  std::ranges::reverse(reversed_states);

  std::vector<board_ir::Point64> points;
  points.reserve(reversed_states.size());
  for (const SearchState& state : reversed_states) {
    const std::optional<board_ir::Point64> point = geometry_compiler::LatticeIndexToExactPoint(
        profile, LatticeIndex{.x = state.x, .y = state.y});
    if (!point.has_value()) {
      return Failure(RouteFailureCode::kInternalInvariant,
                     "A* reconstruction escaped the exact coordinate envelope", std::nullopt,
                     telemetry);
    }
    points.push_back(*point);
  }

  std::uint64_t reconstructed_cost = 0;
  std::uint8_t incoming_direction = kNoIncomingDirection;
  for (std::size_t index = 1; index < reversed_states.size(); ++index) {
    const std::optional<Direction> direction = DirectionBetween(
        LatticeIndex{.x = reversed_states[index - 1].x, .y = reversed_states[index - 1].y},
        LatticeIndex{.x = reversed_states[index].x, .y = reversed_states[index].y});
    if (!direction.has_value() ||
        (profile.heading_mask & geometry_compiler::HeadingFor(*direction)) == 0 ||
        !compiled_board.EdgeIsLegal(request.start_layer, reversed_states[index - 1].x,
                                    reversed_states[index - 1].y, *direction)) {
      return Failure(RouteFailureCode::kInternalInvariant,
                     "A* reconstruction does not follow adjacent compiled legal edges",
                     std::nullopt, telemetry);
    }
    const std::optional<EdgeResourceKey> resource = CanonicalPhysicalEdgeResource(
        request.start_layer,
        LatticeIndex{.x = reversed_states[index - 1].x, .y = reversed_states[index - 1].y},
        *direction);
    if (!resource.has_value() || PolicyBansResource(policy, *resource)) {
      return Failure(RouteFailureCode::kInternalInvariant,
                     "A* reconstruction traverses an invalid or banned physical resource",
                     std::nullopt, telemetry);
    }
    const std::optional<std::uint64_t> transition_cost =
        StepCostUnderPolicy(profile, *direction, incoming_direction, policy, *resource);
    if (!transition_cost.has_value()) {
      return Failure(RouteFailureCode::kInternalInvariant,
                     "A normalized candidate policy overflowed during reconstruction", std::nullopt,
                     telemetry);
    }
    const std::optional<std::uint64_t> next_cost =
        CheckedAddFiniteRouteCost(reconstructed_cost, *transition_cost);
    if (!next_cost.has_value()) {
      return Failure(RouteFailureCode::kResourceExhausted,
                     "Reconstructed integer route cost overflowed", std::nullopt, telemetry);
    }
    reconstructed_cost = *next_cost;
    incoming_direction = static_cast<std::uint8_t>(*direction);
  }
  if (reconstructed_cost != records.at(*goal_state).cost) {
    return Failure(RouteFailureCode::kInternalInvariant,
                   "Reconstructed path cost disagrees with the deterministic A* label",
                   std::nullopt, telemetry);
  }

  std::vector<LayerSegment> segments = CoalesceSegments(request.start_layer, points);
  if (std::optional<RouteFailure> invalid = ValidateExactSegments(board, request, segments);
      invalid.has_value()) {
    invalid->telemetry = telemetry;
    return std::move(*invalid);
  }

  CpuRoute route{
      .source_board_content_hash = compiled_board.source_board_content_hash(),
      .compiler_profile_fingerprint = compiled_board.compiler_profile_fingerprint(),
      .compiler_version = compiled_board.compiler_version(),
      .rule_bucket_identity = compiled_board.rule_bucket().identity,
      .candidate_policy_identity = normalized_policy.identity,
      .total_cost = reconstructed_cost,
      .lattice_path = std::move(points),
      .segments = std::move(segments),
      .telemetry = telemetry,
      .producer_evidence = {},
  };
  route.producer_evidence.evidence =
      std::make_shared<const CpuRouteProducerEvidence>(CpuRouteProducerEvidence{
          .source_board_content_hash = route.source_board_content_hash,
          .compiler_profile_fingerprint = route.compiler_profile_fingerprint,
          .compiler_version = route.compiler_version,
          .rule_bucket_identity = route.rule_bucket_identity,
          .candidate_policy_identity = route.candidate_policy_identity,
          .total_cost = route.total_cost,
          .segments = route.segments,
      });
  return route;
}

}  // namespace apgar::routing
