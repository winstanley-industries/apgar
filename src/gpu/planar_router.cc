#include "apgar/gpu/planar_router.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace apgar::gpu {
namespace {

using geometry_compiler::CompiledBoard;
using geometry_compiler::CompilerProfile;
using geometry_compiler::Direction;
using geometry_compiler::DirectionDelta;
using geometry_compiler::LatticeIndex;

[[nodiscard]] PlanarGpuFailure Failure(PlanarGpuFailureCode code, std::string detail,
                                       std::optional<board_ir::EntityRef> obstacle = std::nullopt,
                                       std::optional<KernelTelemetry> telemetry = std::nullopt) {
  return PlanarGpuFailure{
      .code = code, .detail = std::move(detail), .obstacle = obstacle, .telemetry = telemetry};
}

[[nodiscard]] PlanarGpuFailure BackendFailure(const BackendError& error) {
  switch (error.code) {
    case BackendErrorCode::kResourceExhausted:
      return Failure(PlanarGpuFailureCode::kResourceExhausted, error.detail);
    case BackendErrorCode::kBackendFailure:
      return Failure(PlanarGpuFailureCode::kBackendFailure, error.detail);
    case BackendErrorCode::kInternalInvariant:
      return Failure(PlanarGpuFailureCode::kInternalInvariant, error.detail);
  }
  return Failure(PlanarGpuFailureCode::kInternalInvariant,
                 "Backend returned an unknown error code");
}

[[nodiscard]] bool TerminalSupportsLayer(const board_ir::Terminal& terminal,
                                         board_ir::LayerId layer) {
  return std::ranges::binary_search(terminal.layers, layer);
}

[[nodiscard]] std::optional<PlanarGpuFailure> ValidateRequest(
    const board_ir::BoardSnapshot& board, const CompiledBoard& compiled,
    const routing::CpuRouteRequest& request) {
  const board_ir::RoutingProfile& routing = board.data().routing_profile;
  if (request.net != routing.net || board.FindNet(request.net) == nullptr) {
    return Failure(PlanarGpuFailureCode::kInvalidInput,
                   "GPU route request does not name the Board IR routing-profile net");
  }
  if (!board_ir::PointIsValid(request.start) || !board_ir::PointIsValid(request.goal) ||
      request.start == request.goal) {
    return Failure(PlanarGpuFailureCode::kInvalidInput,
                   "GPU route endpoints must be distinct valid exact coordinates");
  }
  const board_ir::Net* net = board.FindNet(request.net);
  if (net == nullptr || net->terminals.size() != 2) {
    return Failure(PlanarGpuFailureCode::kInvalidInput,
                   "M1 GPU routing requires exactly two target terminals");
  }
  const board_ir::Terminal* first = board.FindTerminal(net->terminals[0]);
  const board_ir::Terminal* second = board.FindTerminal(net->terminals[1]);
  if (first == nullptr || second == nullptr) {
    return Failure(PlanarGpuFailureCode::kInvalidInput,
                   "GPU route request refers to missing target terminals");
  }
  const bool forward = request.start == first->center && request.goal == second->center;
  const bool reverse = request.start == second->center && request.goal == first->center;
  if (!forward && !reverse) {
    return Failure(PlanarGpuFailureCode::kInvalidInput,
                   "M1 GPU route endpoints must be the two exact terminal centers");
  }
  const board_ir::Terminal& start_terminal = forward ? *first : *second;
  const board_ir::Terminal& goal_terminal = forward ? *second : *first;
  if (!TerminalSupportsLayer(start_terminal, request.start_layer) ||
      !TerminalSupportsLayer(goal_terminal, request.goal_layer)) {
    return Failure(PlanarGpuFailureCode::kInvalidInput,
                   "Requested endpoint layer is not a terminal connection layer");
  }
  if (!std::ranges::binary_search(compiled.rule_bucket().allowed_layers, request.start_layer) ||
      !std::ranges::binary_search(compiled.rule_bucket().allowed_layers, request.goal_layer)) {
    return Failure(PlanarGpuFailureCode::kInvalidInput,
                   "Requested endpoint layer is not in the compiled M1 rule bucket");
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<std::uint64_t> CheckedAdd(std::uint64_t left,
                                                      std::uint64_t right) noexcept {
  if (right > std::numeric_limits<std::uint64_t>::max() - left) {
    return std::nullopt;
  }
  return left + right;
}

[[nodiscard]] std::uint64_t StepCost(const CompilerProfile& profile, Direction direction,
                                     std::uint8_t incoming_heading) noexcept {
  std::uint64_t cost = geometry_compiler::IsDiagonal(direction) ? profile.costs.diagonal_step
                                                                : profile.costs.orthogonal_step;
  if (incoming_heading != kNoIncomingHeading &&
      incoming_heading != static_cast<std::uint8_t>(direction)) {
    cost += profile.costs.bend;
  }
  return cost;
}

[[nodiscard]] std::optional<Direction> DirectionBetween(const DeviceNodeV1& start,
                                                        const DeviceNodeV1& end) noexcept {
  if (start.layer != end.layer) {
    return std::nullopt;
  }
  const std::int64_t delta_x = end.lattice_x - start.lattice_x;
  const std::int64_t delta_y = end.lattice_y - start.lattice_y;
  for (Direction direction : geometry_compiler::kStableDirectionOrder) {
    const DirectionDelta delta = geometry_compiler::DeltaFor(direction);
    if (delta_x == delta.x && delta_y == delta.y) {
      return direction;
    }
  }
  return std::nullopt;
}

[[nodiscard]] std::vector<routing::LayerSegment> CoalesceSegments(
    board_ir::LayerId layer, std::span<const board_ir::Point64> points) {
  std::vector<routing::LayerSegment> segments;
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
      segments.push_back(routing::LayerSegment{
          .layer = layer,
          .centerline = board_ir::Segment64{.start = segment_start, .end = points[index - 1]},
      });
      segment_start = points[index - 1];
      previous_delta_x = delta_x;
      previous_delta_y = delta_y;
    }
  }
  segments.push_back(routing::LayerSegment{
      .layer = layer,
      .centerline = board_ir::Segment64{.start = segment_start, .end = points.back()},
  });
  return segments;
}

[[nodiscard]] std::optional<PlanarGpuFailure> ValidateRawAssociations(
    const DeviceCompiledBoardV1& device, std::uint32_t start_node, std::uint32_t goal_node,
    const UntrustedKernelResult& untrusted) {
  if (untrusted.schema_version != kDeviceCompiledBoardSchemaVersion ||
      untrusted.source_board_content_hash != device.header.source_board_content_hash ||
      untrusted.compiler_profile_fingerprint != device.header.compiler_profile_fingerprint ||
      untrusted.compiler_version != device.header.compiler_version ||
      untrusted.rule_bucket_identity != device.header.rule_bucket_identity ||
      untrusted.device_view_fingerprint != device.header.device_view_fingerprint) {
    return Failure(PlanarGpuFailureCode::kValidationFailed,
                   "GPU result associations do not match the immutable device view");
  }
  if (untrusted.start_node != start_node || untrusted.goal_node != goal_node) {
    return Failure(PlanarGpuFailureCode::kValidationFailed,
                   "GPU result endpoint state associations do not match the route request");
  }
  if (untrusted.telemetry.persistent_device_bytes !=
      device.header.estimated_persistent_device_bytes) {
    return Failure(PlanarGpuFailureCode::kInternalInvariant,
                   "GPU persistent-memory telemetry disagrees with deterministic accounting");
  }
  if (untrusted.telemetry.peak_device_bytes < untrusted.telemetry.persistent_device_bytes ||
      untrusted.telemetry.peak_device_bytes < untrusted.telemetry.batch_device_bytes) {
    return Failure(PlanarGpuFailureCode::kInternalInvariant,
                   "GPU peak-memory telemetry is smaller than an owned memory tier");
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<PlanarGpuFailure> ValidateAllPredecessors(
    const CompiledBoard& compiled, const DeviceCompiledBoardV1& device, std::uint32_t start_node,
    const UntrustedKernelResult& untrusted) {
  const std::uint64_t state_count = device.header.represented_states;
  if (untrusted.labels.size() != state_count || untrusted.predecessors.size() != state_count) {
    return Failure(PlanarGpuFailureCode::kInternalInvariant,
                   "GPU label or predecessor array has an invalid bound");
  }
  const std::uint32_t start_state = StateIndex(start_node, kNoIncomingHeading);
  if (untrusted.labels[start_state] != 0 ||
      untrusted.predecessors[start_state] != kInvalidStateIndex) {
    return Failure(PlanarGpuFailureCode::kInternalInvariant,
                   "GPU start state does not have the canonical zero label and no predecessor");
  }

  for (std::uint32_t state = 0; state < state_count; ++state) {
    const std::uint64_t label = untrusted.labels[state];
    const std::uint32_t predecessor = untrusted.predecessors[state];
    const std::uint8_t incoming = IncomingHeadingForState(state);
    if (state == start_state) {
      continue;
    }
    if (incoming == kNoIncomingHeading) {
      if (label != kInfiniteRouteCost || predecessor != kInvalidStateIndex) {
        return Failure(PlanarGpuFailureCode::kInternalInvariant,
                       "GPU created a no-incoming-heading state away from the start");
      }
      continue;
    }
    if (label == kInfiniteRouteCost) {
      if (predecessor != kInvalidStateIndex) {
        return Failure(PlanarGpuFailureCode::kInternalInvariant,
                       "GPU unreachable state carries a predecessor");
      }
      continue;
    }
    if (predecessor >= state_count || predecessor == state) {
      return Failure(PlanarGpuFailureCode::kInternalInvariant,
                     "GPU predecessor index is out of bounds or self-referential");
    }
    const std::uint32_t node = NodeIndexForState(state);
    const std::uint32_t predecessor_node = NodeIndexForState(predecessor);
    if (node >= device.nodes.size() || predecessor_node >= device.nodes.size()) {
      return Failure(PlanarGpuFailureCode::kInternalInvariant,
                     "GPU state maps outside the flattened node array");
    }
    const Direction direction = static_cast<Direction>(incoming);
    const std::optional<Direction> geometric_direction =
        DirectionBetween(device.nodes[predecessor_node], device.nodes[node]);
    if (!geometric_direction.has_value() || *geometric_direction != direction ||
        device.nodes[predecessor_node].neighbors[static_cast<std::size_t>(direction)] != node ||
        (device.nodes[predecessor_node].legal_edges & geometry_compiler::MaskFor(direction)) == 0) {
      return Failure(PlanarGpuFailureCode::kInternalInvariant,
                     "GPU predecessor does not follow one adjacent compiled legal edge");
    }
    const std::uint8_t predecessor_heading = IncomingHeadingForState(predecessor);
    if (predecessor_heading == kNoIncomingHeading && predecessor != start_state) {
      return Failure(PlanarGpuFailureCode::kInternalInvariant,
                     "GPU predecessor uses the start-only heading sentinel elsewhere");
    }
    const std::uint64_t predecessor_label = untrusted.labels[predecessor];
    if (predecessor_label == kInfiniteRouteCost) {
      return Failure(PlanarGpuFailureCode::kInternalInvariant,
                     "GPU finite state follows an unreachable predecessor");
    }
    const std::optional<std::uint64_t> expected =
        CheckedAdd(predecessor_label, StepCost(compiled.profile(), direction, predecessor_heading));
    if (!expected.has_value() || *expected != label) {
      return Failure(PlanarGpuFailureCode::kInternalInvariant,
                     "GPU predecessor label does not reconstruct the exact scalar cost");
    }
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<PlanarGpuFailure> ValidateDisconnectedResult(
    const DeviceCompiledBoardV1& device, const UntrustedKernelResult& untrusted) {
  if (untrusted.labels.size() != device.header.represented_states ||
      untrusted.predecessors.size() != device.header.represented_states) {
    return Failure(PlanarGpuFailureCode::kInternalInvariant,
                   "Disconnected GPU result has invalid array bounds");
  }
  for (std::uint8_t heading = 0; heading < 8; ++heading) {
    if (untrusted.labels[StateIndex(untrusted.goal_node, heading)] != kInfiniteRouteCost) {
      return Failure(PlanarGpuFailureCode::kInternalInvariant,
                     "GPU reported disconnected with a finite goal label");
    }
  }
  return std::nullopt;
}

}  // namespace

PlanarGpuRouteResult ValidateAndReconstructGpuRoute(const board_ir::BoardSnapshot& board,
                                                    const CompiledBoard& compiled_board,
                                                    const DeviceCompiledBoardV1& device_board,
                                                    const routing::CpuRouteRequest& request,
                                                    PlanarGenerator generator,
                                                    const BackendMetadata& backend,
                                                    const UntrustedKernelResult& untrusted) {
  const std::optional<LatticeIndex> start =
      geometry_compiler::ExactPointToLatticeIndex(compiled_board.profile(), request.start);
  const std::optional<LatticeIndex> goal =
      geometry_compiler::ExactPointToLatticeIndex(compiled_board.profile(), request.goal);
  if (!start.has_value() || !goal.has_value()) {
    return Failure(PlanarGpuFailureCode::kInvalidInput,
                   "GPU route endpoints must lie exactly on the compiler lattice");
  }
  const std::optional<std::uint32_t> start_node =
      FindDeviceNodeIndex(device_board, request.start_layer, *start);
  const std::optional<std::uint32_t> goal_node =
      FindDeviceNodeIndex(device_board, request.goal_layer, *goal);
  if (!start_node.has_value() || !goal_node.has_value()) {
    return Failure(PlanarGpuFailureCode::kInvalidInput,
                   "GPU route endpoints must both be represented by active sparse tiles");
  }
  if (std::optional<PlanarGpuFailure> association =
          ValidateRawAssociations(device_board, *start_node, *goal_node, untrusted);
      association.has_value()) {
    return std::move(*association);
  }
  if (untrusted.completion != KernelCompletion::kReached) {
    return Failure(PlanarGpuFailureCode::kInternalInvariant,
                   "Route reconstruction requires a reached GPU result");
  }
  if (std::optional<PlanarGpuFailure> invalid =
          ValidateAllPredecessors(compiled_board, device_board, *start_node, untrusted);
      invalid.has_value()) {
    return std::move(*invalid);
  }

  std::uint64_t best_goal_cost = kInfiniteRouteCost;
  std::uint32_t best_goal_state = kInvalidStateIndex;
  for (std::uint8_t heading = 0; heading < 8; ++heading) {
    const std::uint32_t state = StateIndex(*goal_node, heading);
    if (untrusted.labels[state] < best_goal_cost ||
        (untrusted.labels[state] == best_goal_cost && state < best_goal_state)) {
      best_goal_cost = untrusted.labels[state];
      best_goal_state = state;
    }
  }
  if (best_goal_cost == kInfiniteRouteCost || untrusted.goal_state != best_goal_state) {
    return Failure(PlanarGpuFailureCode::kInternalInvariant,
                   "GPU goal state is not the stable minimum finite heading state");
  }

  const std::uint32_t start_state = StateIndex(*start_node, kNoIncomingHeading);
  std::vector<std::uint8_t> visited(
      static_cast<std::size_t>(device_board.header.represented_states), 0);
  std::vector<std::uint32_t> reversed_nodes;
  std::uint32_t cursor = best_goal_state;
  while (true) {
    if (cursor >= visited.size()) {
      return Failure(PlanarGpuFailureCode::kInternalInvariant,
                     "GPU reconstruction escaped the allocated state array");
    }
    if (visited[cursor] != 0) {
      return Failure(PlanarGpuFailureCode::kInternalInvariant,
                     "GPU predecessor reconstruction contains a cycle");
    }
    visited[cursor] = 1;
    reversed_nodes.push_back(NodeIndexForState(cursor));
    if (cursor == start_state) {
      break;
    }
    cursor = untrusted.predecessors[cursor];
    if (reversed_nodes.size() > device_board.header.represented_states) {
      return Failure(PlanarGpuFailureCode::kInternalInvariant,
                     "GPU predecessor reconstruction exceeded its acyclic state bound");
    }
  }
  std::ranges::reverse(reversed_nodes);

  std::vector<board_ir::Point64> points;
  points.reserve(reversed_nodes.size());
  for (std::uint32_t node : reversed_nodes) {
    if (node >= device_board.nodes.size()) {
      return Failure(PlanarGpuFailureCode::kInternalInvariant,
                     "GPU reconstruction contains an invalid node index");
    }
    const DeviceNodeV1& flattened = device_board.nodes[node];
    const std::optional<board_ir::Point64> point = geometry_compiler::LatticeIndexToExactPoint(
        compiled_board.profile(), LatticeIndex{.x = flattened.lattice_x, .y = flattened.lattice_y});
    if (!point.has_value()) {
      return Failure(PlanarGpuFailureCode::kInternalInvariant,
                     "GPU reconstruction escaped the exact coordinate envelope");
    }
    points.push_back(*point);
  }
  if (points.empty() || points.front() != request.start || points.back() != request.goal) {
    return Failure(PlanarGpuFailureCode::kInternalInvariant,
                   "GPU reconstruction does not connect the exact requested endpoints");
  }

  std::vector<routing::LayerSegment> segments = CoalesceSegments(request.start_layer, points);
  if (std::optional<routing::RouteFailure> invalid =
          routing::ValidateReconstructedRoute(board, compiled_board, request, segments);
      invalid.has_value()) {
    return Failure(PlanarGpuFailureCode::kValidationFailed,
                   "Exact GPU route validation failed: " + invalid->detail, invalid->obstacle);
  }

  return PlanarGpuRoute{
      .source_board_content_hash = untrusted.source_board_content_hash,
      .compiler_profile_fingerprint = untrusted.compiler_profile_fingerprint,
      .compiler_version = untrusted.compiler_version,
      .rule_bucket_identity = untrusted.rule_bucket_identity,
      .device_view_fingerprint = untrusted.device_view_fingerprint,
      .generator = generator,
      .backend = backend,
      .total_cost = best_goal_cost,
      .lattice_path = std::move(points),
      .segments = std::move(segments),
      .telemetry = untrusted.telemetry,
  };
}

PlanarGpuRouteResult RouteWithPlanarGpuBackend(const board_ir::BoardSnapshot& board,
                                               const CompiledBoard& compiled_board,
                                               const routing::CpuRouteRequest& request,
                                               const PlanarRoutePolicy& policy,
                                               IPlanarRouteBackend& backend) {
  if (policy.maximum_rounds == 0) {
    return Failure(PlanarGpuFailureCode::kInvalidInput, "GPU route round budget must be positive");
  }
  if (std::optional<PlanarGpuFailure> invalid = ValidateRequest(board, compiled_board, request);
      invalid.has_value()) {
    return std::move(*invalid);
  }
  if (request.start_layer != request.goal_layer) {
    return Failure(PlanarGpuFailureCode::kUnsupported,
                   "Planar Phase 2 GPU kernels cannot invent via padstack legality");
  }

  DeviceCompiledBoardResult flattened = BuildDeviceCompiledBoardV1(board, compiled_board);
  if (std::holds_alternative<PlanarGpuFailure>(flattened)) {
    return std::get<PlanarGpuFailure>(std::move(flattened));
  }
  DeviceCompiledBoardV1 device = std::get<DeviceCompiledBoardV1>(std::move(flattened));

  const std::optional<LatticeIndex> start =
      geometry_compiler::ExactPointToLatticeIndex(compiled_board.profile(), request.start);
  const std::optional<LatticeIndex> goal =
      geometry_compiler::ExactPointToLatticeIndex(compiled_board.profile(), request.goal);
  if (!start.has_value() || !goal.has_value()) {
    return Failure(PlanarGpuFailureCode::kInvalidInput,
                   "GPU route endpoints must lie exactly on the compiler lattice");
  }
  const std::optional<std::uint32_t> start_node =
      FindDeviceNodeIndex(device, request.start_layer, *start);
  const std::optional<std::uint32_t> goal_node =
      FindDeviceNodeIndex(device, request.goal_layer, *goal);
  if (!start_node.has_value() || !goal_node.has_value()) {
    return Failure(PlanarGpuFailureCode::kInvalidInput,
                   "GPU route endpoints must both be represented by active sparse tiles");
  }
  if (policy.cancellation != nullptr && policy.cancellation->load()) {
    return Failure(PlanarGpuFailureCode::kCancelled,
                   "GPU route was cancelled before device upload");
  }

  BackendMetadataResult metadata_result = backend.QueryMetadata();
  if (std::holds_alternative<BackendError>(metadata_result)) {
    return BackendFailure(std::get<BackendError>(metadata_result));
  }
  BackendMetadata metadata = std::get<BackendMetadata>(std::move(metadata_result));

  UploadResult upload_result = backend.UploadCompiledView(device);
  if (std::holds_alternative<BackendError>(upload_result)) {
    return BackendFailure(std::get<BackendError>(upload_result));
  }
  std::unique_ptr<UploadedCompiledView> uploaded =
      std::get<std::unique_ptr<UploadedCompiledView>>(std::move(upload_result));
  if (uploaded == nullptr) {
    return Failure(PlanarGpuFailureCode::kInternalInvariant,
                   "Backend returned a null uploaded compiled view");
  }

  const BackendExecutionRequest execution_request{
      .generator = policy.generator,
      .start_node = *start_node,
      .goal_node = *goal_node,
      .maximum_rounds = policy.maximum_rounds,
      .maximum_device_bytes = policy.maximum_device_bytes,
      .cancellation = policy.cancellation,
      .fault_injection = policy.fault_injection,
  };
  ExecutionResult execution_result = backend.ExecuteRoute(*uploaded, execution_request);
  if (std::holds_alternative<BackendError>(execution_result)) {
    return BackendFailure(std::get<BackendError>(execution_result));
  }
  std::unique_ptr<PendingRouteExecution> execution =
      std::get<std::unique_ptr<PendingRouteExecution>>(std::move(execution_result));
  if (execution == nullptr) {
    return Failure(PlanarGpuFailureCode::kInternalInvariant,
                   "Backend returned a null route execution");
  }

  ReadbackResult readback_result = backend.ReadbackRoute(*execution);
  if (std::holds_alternative<BackendError>(readback_result)) {
    return BackendFailure(std::get<BackendError>(readback_result));
  }
  UntrustedKernelResult untrusted = std::get<UntrustedKernelResult>(std::move(readback_result));
  if (std::optional<PlanarGpuFailure> association =
          ValidateRawAssociations(device, *start_node, *goal_node, untrusted);
      association.has_value()) {
    return std::move(*association);
  }
  switch (untrusted.completion) {
    case KernelCompletion::kReached:
      return ValidateAndReconstructGpuRoute(board, compiled_board, device, request,
                                            policy.generator, metadata, untrusted);
    case KernelCompletion::kDisconnected:
      if (std::optional<PlanarGpuFailure> invalid = ValidateDisconnectedResult(device, untrusted);
          invalid.has_value()) {
        return std::move(*invalid);
      }
      return Failure(PlanarGpuFailureCode::kDisconnected,
                     "No planar path connects the represented start and goal fields", std::nullopt,
                     untrusted.telemetry);
    case KernelCompletion::kBudgetExhausted:
      return Failure(PlanarGpuFailureCode::kResourceExhausted,
                     "GPU route did not converge within the configured round budget", std::nullopt,
                     untrusted.telemetry);
    case KernelCompletion::kCancelled:
      return Failure(PlanarGpuFailureCode::kCancelled,
                     "GPU route was cancelled at a bounded launch boundary", std::nullopt,
                     untrusted.telemetry);
  }
  return Failure(PlanarGpuFailureCode::kInternalInvariant,
                 "GPU result contains an unknown completion code");
}

}  // namespace apgar::gpu
