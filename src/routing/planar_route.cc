#include "apgar/routing/planar_route.h"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <ranges>

namespace apgar::routing {
namespace {

using Wide = __int128_t;

[[nodiscard]] bool TerminalSupportsLayer(const board_ir::Terminal& terminal,
                                         board_ir::LayerId layer) noexcept {
  return std::ranges::binary_search(terminal.layers, layer);
}

}  // namespace

std::optional<CompiledBoardAssociationIssue> ValidateCompiledBoardAssociation(
    const board_ir::BoardSnapshot& board,
    const geometry_compiler::CompiledBoard& compiled_board) noexcept {
  if (compiled_board.source_board_content_hash() != board.content_hash()) {
    return CompiledBoardAssociationIssue::kSourceBoardMismatch;
  }
  if (compiled_board.compiler_version() != geometry_compiler::kGeometryCompilerVersion) {
    return CompiledBoardAssociationIssue::kCompilerVersionMismatch;
  }
  if (compiled_board.compiler_profile_fingerprint() !=
      geometry_compiler::FingerprintCompilerProfile(compiled_board.profile())) {
    return CompiledBoardAssociationIssue::kProfileFingerprintMismatch;
  }
  if (compiled_board.rule_bucket() !=
      geometry_compiler::DeriveM1RuleBucket(board.data().routing_profile)) {
    return CompiledBoardAssociationIssue::kRuleBucketMismatch;
  }
  return std::nullopt;
}

std::optional<RouteRequestAdmissionIssue> ValidateTwoTerminalRouteRequest(
    const board_ir::BoardSnapshot& board, const geometry_compiler::CompiledBoard& compiled_board,
    const CpuRouteRequest& request) noexcept {
  const board_ir::RoutingProfile& routing = board.data().routing_profile;
  if (request.net != routing.net || board.FindNet(request.net) == nullptr) {
    return RouteRequestAdmissionIssue::kRoutingProfileNetMismatch;
  }
  if (!board_ir::PointIsValid(request.start) || !board_ir::PointIsValid(request.goal) ||
      request.start == request.goal) {
    return RouteRequestAdmissionIssue::kInvalidOrCoincidentEndpoints;
  }
  const board_ir::Net* net = board.FindNet(request.net);
  if (net == nullptr || net->terminals.size() != 2) {
    return RouteRequestAdmissionIssue::kRequiresExactlyTwoTerminals;
  }
  const board_ir::Terminal* first = board.FindTerminal(net->terminals[0]);
  const board_ir::Terminal* second = board.FindTerminal(net->terminals[1]);
  if (first == nullptr || second == nullptr) {
    return RouteRequestAdmissionIssue::kMissingTerminal;
  }
  const bool forward = request.start == first->center && request.goal == second->center;
  const bool reverse = request.start == second->center && request.goal == first->center;
  if (!forward && !reverse) {
    return RouteRequestAdmissionIssue::kEndpointsNotTerminalCenters;
  }
  const board_ir::Terminal& start_terminal = forward ? *first : *second;
  const board_ir::Terminal& goal_terminal = forward ? *second : *first;
  if (!TerminalSupportsLayer(start_terminal, request.start_layer) ||
      !TerminalSupportsLayer(goal_terminal, request.goal_layer)) {
    return RouteRequestAdmissionIssue::kUnsupportedTerminalLayer;
  }
  if (!std::ranges::binary_search(compiled_board.rule_bucket().allowed_layers,
                                  request.start_layer) ||
      !std::ranges::binary_search(compiled_board.rule_bucket().allowed_layers,
                                  request.goal_layer)) {
    return RouteRequestAdmissionIssue::kLayerOutsideRuleBucket;
  }
  return std::nullopt;
}

TwoTerminalRequestResult BuildTwoTerminalRouteRequest(const board_ir::BoardSnapshot& board,
                                                      board_ir::LayerId start_layer,
                                                      board_ir::LayerId goal_layer) noexcept {
  const board_ir::Net* net = board.FindNet(board.data().routing_profile.net);
  if (net == nullptr) {
    return TwoTerminalRequestIssue::kMissingRoutingProfileNet;
  }
  if (net->terminals.size() != 2) {
    return TwoTerminalRequestIssue::kRequiresExactlyTwoTerminals;
  }
  const board_ir::Terminal* first = board.FindTerminal(net->terminals[0]);
  const board_ir::Terminal* second = board.FindTerminal(net->terminals[1]);
  if (first == nullptr || second == nullptr) {
    return TwoTerminalRequestIssue::kMissingTerminal;
  }
  return CpuRouteRequest{
      .net = net->ref,
      .start = first->center,
      .goal = second->center,
      .start_layer = start_layer,
      .goal_layer = goal_layer,
  };
}

PlanarEndpointResult ResolvePlanarEndpoints(const geometry_compiler::CompiledBoard& compiled_board,
                                            const CpuRouteRequest& request) noexcept {
  const std::optional<geometry_compiler::LatticeIndex> start =
      geometry_compiler::ExactPointToLatticeIndex(compiled_board.profile(), request.start);
  const std::optional<geometry_compiler::LatticeIndex> goal =
      geometry_compiler::ExactPointToLatticeIndex(compiled_board.profile(), request.goal);
  if (!start.has_value() || !goal.has_value()) {
    return PlanarEndpointIssue::kNotOnCompilerLattice;
  }
  if (!compiled_board.ContainsNode(request.start_layer, start->x, start->y) ||
      !compiled_board.ContainsNode(request.goal_layer, goal->x, goal->y)) {
    return PlanarEndpointIssue::kNotRepresented;
  }
  return ResolvedPlanarEndpoints{.start = *start, .goal = *goal};
}

std::optional<std::uint64_t> CheckedAdd(std::uint64_t left, std::uint64_t right) noexcept {
  if (right > std::numeric_limits<std::uint64_t>::max() - left) {
    return std::nullopt;
  }
  return left + right;
}

std::uint64_t StepCost(const geometry_compiler::CompilerProfile& profile,
                       geometry_compiler::Direction direction,
                       std::uint8_t incoming_direction) noexcept {
  std::uint64_t cost = geometry_compiler::IsDiagonal(direction) ? profile.costs.diagonal_step
                                                                : profile.costs.orthogonal_step;
  if (incoming_direction != kNoIncomingDirection &&
      incoming_direction != static_cast<std::uint8_t>(direction)) {
    cost += profile.costs.bend;
  }
  return cost;
}

std::optional<geometry_compiler::Direction> DirectionBetween(
    geometry_compiler::LatticeIndex start, geometry_compiler::LatticeIndex end) noexcept {
  const Wide delta_x = static_cast<Wide>(end.x) - start.x;
  const Wide delta_y = static_cast<Wide>(end.y) - start.y;
  for (geometry_compiler::Direction direction : geometry_compiler::kStableDirectionOrder) {
    const geometry_compiler::DirectionDelta delta = geometry_compiler::DeltaFor(direction);
    if (delta_x == delta.x && delta_y == delta.y) {
      return direction;
    }
  }
  return std::nullopt;
}

std::vector<LayerSegment> CoalesceSegments(board_ir::LayerId layer,
                                           std::span<const board_ir::Point64> points) {
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

}  // namespace apgar::routing
