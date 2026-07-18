#include "apgar/routing/cpu_astar.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
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

inline constexpr std::uint8_t kNoIncomingDirection = 8;

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
                                   std::optional<board_ir::EntityRef> obstacle = std::nullopt) {
  return RouteFailure{.code = code, .detail = std::move(detail), .obstacle = obstacle};
}

[[nodiscard]] std::optional<RouteFailure> ValidateAssociation(const board_ir::BoardSnapshot& board,
                                                              const CompiledBoard& compiled) {
  if (compiled.source_board_content_hash() != board.content_hash()) {
    return Failure(RouteFailureCode::kValidationFailed,
                   "Compiled board source hash does not match the exact BoardSnapshot");
  }
  if (compiled.compiler_version() != geometry_compiler::kGeometryCompilerVersion) {
    return Failure(RouteFailureCode::kValidationFailed,
                   "Compiled board compiler version is not supported");
  }
  if (compiled.compiler_profile_fingerprint() !=
      geometry_compiler::FingerprintCompilerProfile(compiled.profile())) {
    return Failure(RouteFailureCode::kValidationFailed,
                   "Compiled board profile fingerprint does not match its profile payload");
  }
  if (compiled.rule_bucket() !=
      geometry_compiler::DeriveM1RuleBucket(board.data().routing_profile)) {
    return Failure(RouteFailureCode::kValidationFailed,
                   "Compiled board rule bucket is stale or does not match the BoardSnapshot");
  }
  return std::nullopt;
}

[[nodiscard]] bool TerminalSupportsLayer(const board_ir::Terminal& terminal,
                                         board_ir::LayerId layer) {
  return std::ranges::binary_search(terminal.layers, layer);
}

[[nodiscard]] std::optional<RouteFailure> ValidateRequest(const board_ir::BoardSnapshot& board,
                                                          const CompiledBoard& compiled,
                                                          const CpuRouteRequest& request) {
  const board_ir::RoutingProfile& routing = board.data().routing_profile;
  if (request.net != routing.net || board.FindNet(request.net) == nullptr) {
    return Failure(RouteFailureCode::kInvalidRequest,
                   "CPU route request does not name the Board IR routing-profile net");
  }
  if (!board_ir::PointIsValid(request.start) || !board_ir::PointIsValid(request.goal) ||
      request.start == request.goal) {
    return Failure(RouteFailureCode::kInvalidRequest,
                   "CPU route endpoints must be distinct valid exact coordinates");
  }
  const board_ir::Net* net = board.FindNet(request.net);
  if (net == nullptr || net->terminals.size() != 2) {
    return Failure(RouteFailureCode::kInvalidRequest,
                   "M1 CPU routing requires exactly two target terminals");
  }
  const board_ir::Terminal* first = board.FindTerminal(net->terminals[0]);
  const board_ir::Terminal* second = board.FindTerminal(net->terminals[1]);
  if (first == nullptr || second == nullptr) {
    return Failure(RouteFailureCode::kInvalidRequest,
                   "CPU route request refers to missing target terminals");
  }
  const bool forward = request.start == first->center && request.goal == second->center;
  const bool reverse = request.start == second->center && request.goal == first->center;
  if (!forward && !reverse) {
    return Failure(RouteFailureCode::kInvalidRequest,
                   "M1 CPU route endpoints must be the two exact terminal centers");
  }
  const board_ir::Terminal& start_terminal = forward ? *first : *second;
  const board_ir::Terminal& goal_terminal = forward ? *second : *first;
  if (!TerminalSupportsLayer(start_terminal, request.start_layer) ||
      !TerminalSupportsLayer(goal_terminal, request.goal_layer)) {
    return Failure(RouteFailureCode::kInvalidRequest,
                   "Requested endpoint layer is not a terminal connection layer");
  }
  if (!std::ranges::binary_search(compiled.rule_bucket().allowed_layers, request.start_layer) ||
      !std::ranges::binary_search(compiled.rule_bucket().allowed_layers, request.goal_layer)) {
    return Failure(RouteFailureCode::kInvalidRequest,
                   "Requested endpoint layer is not in the compiled M1 rule bucket");
  }
  return std::nullopt;
}

[[nodiscard]] std::uint64_t AbsoluteDifference(std::int64_t left, std::int64_t right) noexcept {
  const Wide difference = static_cast<Wide>(left) - right;
  return static_cast<std::uint64_t>(difference < 0 ? -difference : difference);
}

[[nodiscard]] std::uint64_t Heuristic(const CompilerProfile& profile, LatticeIndex point,
                                      LatticeIndex goal) noexcept {
  const std::uint64_t delta_x = AbsoluteDifference(point.x, goal.x);
  const std::uint64_t delta_y = AbsoluteDifference(point.y, goal.y);
  const bool horizontal = (profile.heading_mask &
                           static_cast<board_ir::HeadingMask>(board_ir::Heading::kHorizontal)) != 0;
  const bool vertical = (profile.heading_mask &
                         static_cast<board_ir::HeadingMask>(board_ir::Heading::kVertical)) != 0;
  const bool diagonal = (profile.heading_mask &
                         static_cast<board_ir::HeadingMask>(board_ir::Heading::kDiagonal45)) != 0;

  UWide estimate = 0;
  if (horizontal && vertical) {
    if (diagonal) {
      if (profile.costs.diagonal_step < profile.costs.orthogonal_step) {
        // Cheap alternating diagonals can make axial progress for less than an
        // orthogonal step. max(dx, dy) * diagonal_cost is a deliberately
        // relaxed lower bound that remains admissible across parity and bends.
        estimate = static_cast<UWide>(std::max(delta_x, delta_y)) * profile.costs.diagonal_step;
      } else {
        const std::uint64_t diagonal_steps = std::min(delta_x, delta_y);
        const std::uint64_t orthogonal_steps = std::max(delta_x, delta_y) - diagonal_steps;
        const std::uint64_t effective_diagonal =
            std::min<std::uint64_t>(profile.costs.diagonal_step,
                                    static_cast<std::uint64_t>(profile.costs.orthogonal_step) * 2);
        estimate = static_cast<UWide>(diagonal_steps) * effective_diagonal +
                   static_cast<UWide>(orthogonal_steps) * profile.costs.orthogonal_step;
      }
    } else {
      estimate = static_cast<UWide>(delta_x + delta_y) * profile.costs.orthogonal_step;
    }
  } else if (horizontal && !vertical && !diagonal) {
    estimate = static_cast<UWide>(delta_x) * profile.costs.orthogonal_step;
  } else if (vertical && !horizontal && !diagonal) {
    estimate = static_cast<UWide>(delta_y) * profile.costs.orthogonal_step;
  } else if (diagonal) {
    std::uint64_t minimum_step_cost = profile.costs.diagonal_step;
    if (horizontal || vertical) {
      minimum_step_cost = std::min<std::uint64_t>(minimum_step_cost, profile.costs.orthogonal_step);
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

[[nodiscard]] std::optional<std::uint64_t> CheckedAdd(std::uint64_t left,
                                                      std::uint64_t right) noexcept {
  if (right > std::numeric_limits<std::uint64_t>::max() - left) {
    return std::nullopt;
  }
  return left + right;
}

[[nodiscard]] std::uint64_t EstimatedTotal(std::uint64_t cost, std::uint64_t heuristic) noexcept {
  return CheckedAdd(cost, heuristic).value_or(std::numeric_limits<std::uint64_t>::max());
}

[[nodiscard]] std::uint64_t StepCost(const CompilerProfile& profile, Direction direction,
                                     std::uint8_t incoming_direction) noexcept {
  std::uint64_t cost = geometry_compiler::IsDiagonal(direction) ? profile.costs.diagonal_step
                                                                : profile.costs.orthogonal_step;
  if (incoming_direction != kNoIncomingDirection &&
      incoming_direction != static_cast<std::uint8_t>(direction)) {
    cost += profile.costs.bend;
  }
  return cost;
}

[[nodiscard]] std::optional<Direction> DirectionBetween(LatticeIndex start,
                                                        LatticeIndex end) noexcept {
  const std::int64_t delta_x = end.x - start.x;
  const std::int64_t delta_y = end.y - start.y;
  for (Direction direction : kStableDirectionOrder) {
    const DirectionDelta delta = geometry_compiler::DeltaFor(direction);
    if (delta_x == delta.x && delta_y == delta.y) {
      return direction;
    }
  }
  return std::nullopt;
}

[[nodiscard]] std::vector<LayerSegment> CoalesceSegments(
    board_ir::LayerId layer, std::span<const board_ir::Point64> points) {
  std::vector<LayerSegment> segments;
  if (points.size() < 2) {
    return segments;
  }
  board_ir::Point64 segment_start = points.front();
  board_ir::DbCoord previous_delta_x = points[1].x - points[0].x;
  board_ir::DbCoord previous_delta_y = points[1].y - points[0].y;
  for (std::size_t index = 2; index < points.size(); ++index) {
    const board_ir::DbCoord delta_x = points[index].x - points[index - 1].x;
    const board_ir::DbCoord delta_y = points[index].y - points[index - 1].y;
    if (delta_x != previous_delta_x || delta_y != previous_delta_y) {
      segments.push_back(LayerSegment{
          .layer = layer,
          .centerline = board_ir::Segment64{.start = segment_start, .end = points[index - 1]},
      });
      segment_start = points[index - 1];
      previous_delta_x = delta_x;
      previous_delta_y = delta_y;
    }
  }
  segments.push_back(LayerSegment{
      .layer = layer,
      .centerline = board_ir::Segment64{.start = segment_start, .end = points.back()},
  });
  return segments;
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

std::optional<RouteFailure> ValidateReconstructedRoute(const board_ir::BoardSnapshot& board,
                                                       const CompiledBoard& compiled_board,
                                                       const CpuRouteRequest& request,
                                                       std::span<const LayerSegment> segments) {
  if (std::optional<RouteFailure> association = ValidateAssociation(board, compiled_board);
      association.has_value()) {
    return association;
  }
  if (std::optional<RouteFailure> invalid = ValidateRequest(board, compiled_board, request);
      invalid.has_value()) {
    return invalid;
  }
  return ValidateExactSegments(board, request, segments);
}

CpuRouteResult RouteWithCpuAStar(const board_ir::BoardSnapshot& board,
                                 const CompiledBoard& compiled_board,
                                 const CpuRouteRequest& request) {
  if (std::optional<RouteFailure> association = ValidateAssociation(board, compiled_board);
      association.has_value()) {
    return std::move(*association);
  }
  if (std::optional<RouteFailure> invalid = ValidateRequest(board, compiled_board, request);
      invalid.has_value()) {
    return std::move(*invalid);
  }
  if (request.start_layer != request.goal_layer) {
    return Failure(RouteFailureCode::kUnsupportedLayerTransition,
                   "Planar M1 CPU A* cannot invent via padstack legality");
  }

  const CompilerProfile& profile = compiled_board.profile();
  const std::optional<LatticeIndex> start =
      geometry_compiler::ExactPointToLatticeIndex(profile, request.start);
  const std::optional<LatticeIndex> goal =
      geometry_compiler::ExactPointToLatticeIndex(profile, request.goal);
  if (!start.has_value() || !goal.has_value()) {
    return Failure(RouteFailureCode::kInvalidRequest,
                   "CPU route endpoints must lie exactly on the compiler lattice");
  }
  if (!compiled_board.ContainsNode(request.start_layer, start->x, start->y) ||
      !compiled_board.ContainsNode(request.goal_layer, goal->x, goal->y)) {
    return Failure(RouteFailureCode::kInvalidRequest,
                   "CPU route endpoints must both be represented by active sparse tiles");
  }

  const SearchState start_state{
      .x = start->x,
      .y = start->y,
      .incoming_direction = kNoIncomingDirection,
  };
  std::unordered_map<SearchState, SearchRecord, SearchStateHash> records;
  records.reserve(static_cast<std::size_t>(
      std::min<std::uint64_t>(compiled_board.telemetry().represented_nodes * 2, 1'000'000)));
  records.emplace(start_state, SearchRecord{.cost = 0, .predecessor = std::nullopt});
  std::priority_queue<QueueItem, std::vector<QueueItem>, QueueGreater> queue;
  const std::uint64_t start_heuristic = Heuristic(profile, *start, *goal);
  std::uint64_t sequence = 0;
  queue.push(QueueItem{.estimated_total = start_heuristic,
                       .heuristic = start_heuristic,
                       .cost = 0,
                       .state = start_state,
                       .sequence = sequence++});

  std::optional<SearchState> goal_state;
  while (!queue.empty()) {
    const QueueItem current = queue.top();
    queue.pop();
    const auto current_record = records.find(current.state);
    if (current_record == records.end() || current_record->second.cost != current.cost) {
      continue;
    }
    if (current.state.x == goal->x && current.state.y == goal->y) {
      goal_state = current.state;
      break;
    }

    const geometry_compiler::CompiledNode* current_node =
        compiled_board.FindNode(request.start_layer, current.state.x, current.state.y);
    if (current_node == nullptr) {
      return Failure(RouteFailureCode::kInternalInvariant,
                     "A* expanded a state outside the represented sparse field");
    }

    for (Direction direction : kStableDirectionOrder) {
      const bool mask_is_legal =
          (current_node->legal_edges & geometry_compiler::MaskFor(direction)) != 0;
      if ((profile.heading_mask & geometry_compiler::HeadingFor(direction)) == 0) {
        if (mask_is_legal) {
          return Failure(RouteFailureCode::kValidationFailed,
                         "Compiled mask enables a direction excluded by its profile");
        }
        continue;
      }
      if (!mask_is_legal) {
        continue;
      }
      const DirectionDelta delta = geometry_compiler::DeltaFor(direction);
      const SearchState neighbor{
          .x = current.state.x + delta.x,
          .y = current.state.y + delta.y,
          .incoming_direction = static_cast<std::uint8_t>(direction),
      };
      if (!compiled_board.ContainsNode(request.start_layer, neighbor.x, neighbor.y)) {
        return Failure(RouteFailureCode::kInternalInvariant,
                       "Compiled legal edge points outside the represented sparse field");
      }
      const std::optional<std::uint64_t> next_cost =
          CheckedAdd(current.cost, StepCost(profile, direction, current.state.incoming_direction));
      if (!next_cost.has_value()) {
        return Failure(RouteFailureCode::kResourceExhausted,
                       "Integer route cost overflowed the validated search envelope");
      }
      const auto existing = records.find(neighbor);
      if (existing != records.end() && existing->second.cost <= *next_cost) {
        continue;
      }
      records.insert_or_assign(neighbor,
                               SearchRecord{.cost = *next_cost, .predecessor = current.state});
      const std::uint64_t heuristic =
          Heuristic(profile, LatticeIndex{.x = neighbor.x, .y = neighbor.y}, *goal);
      queue.push(QueueItem{
          .estimated_total = EstimatedTotal(*next_cost, heuristic),
          .heuristic = heuristic,
          .cost = *next_cost,
          .state = neighbor,
          .sequence = sequence++,
      });
    }
  }

  if (!goal_state.has_value()) {
    return Failure(RouteFailureCode::kDisconnected,
                   "No planar path connects the represented start and goal fields");
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
                     "A* predecessor reconstruction exceeded the acyclic state bound");
    }
    const auto record = records.find(cursor);
    if (record == records.end() || !record->second.predecessor.has_value()) {
      return Failure(RouteFailureCode::kInternalInvariant,
                     "A* predecessor reconstruction encountered a missing state");
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
                     "A* reconstruction escaped the exact coordinate envelope");
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
                     "A* reconstruction does not follow adjacent compiled legal edges");
    }
    const std::optional<std::uint64_t> next_cost =
        CheckedAdd(reconstructed_cost, StepCost(profile, *direction, incoming_direction));
    if (!next_cost.has_value()) {
      return Failure(RouteFailureCode::kResourceExhausted,
                     "Reconstructed integer route cost overflowed");
    }
    reconstructed_cost = *next_cost;
    incoming_direction = static_cast<std::uint8_t>(*direction);
  }
  if (reconstructed_cost != records.at(*goal_state).cost) {
    return Failure(RouteFailureCode::kInternalInvariant,
                   "Reconstructed path cost disagrees with the deterministic A* label");
  }

  std::vector<LayerSegment> segments = CoalesceSegments(request.start_layer, points);
  if (std::optional<RouteFailure> invalid = ValidateExactSegments(board, request, segments);
      invalid.has_value()) {
    return std::move(*invalid);
  }

  return CpuRoute{
      .source_board_content_hash = compiled_board.source_board_content_hash(),
      .compiler_profile_fingerprint = compiled_board.compiler_profile_fingerprint(),
      .compiler_version = compiled_board.compiler_version(),
      .rule_bucket_identity = compiled_board.rule_bucket().identity,
      .total_cost = reconstructed_cost,
      .lattice_path = std::move(points),
      .segments = std::move(segments),
  };
}

}  // namespace apgar::routing
