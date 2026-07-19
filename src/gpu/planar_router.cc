#include "apgar/gpu/planar_router.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <ranges>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace apgar::gpu {
namespace {

using geometry_compiler::CompiledBoard;
using geometry_compiler::Direction;
using geometry_compiler::LatticeIndex;
using UWide = __uint128_t;

static_assert(kNoIncomingHeading == routing::kNoIncomingDirection);

[[nodiscard]] PlanarGpuFailure Failure(PlanarGpuFailureCode code, std::string detail,
                                       std::optional<board_ir::EntityRef> obstacle = std::nullopt,
                                       std::optional<KernelTelemetry> telemetry = std::nullopt,
                                       std::string invariant_id = {}) {
  return PlanarGpuFailure{
      .code = code,
      .detail = std::move(detail),
      .invariant_id = std::move(invariant_id),
      .obstacle = obstacle,
      .telemetry = telemetry,
  };
}

[[nodiscard]] PlanarGpuFailure WithTelemetry(PlanarGpuFailure failure,
                                             const KernelTelemetry& telemetry) {
  failure.telemetry = telemetry;
  return failure;
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

[[nodiscard]] std::optional<PlanarGpuFailure> ValidateRequest(
    const board_ir::BoardSnapshot& board, const CompiledBoard& compiled,
    const routing::CpuRouteRequest& request) {
  const std::optional<routing::RouteRequestAdmissionIssue> issue =
      routing::ValidateTwoTerminalRouteRequest(board, compiled, request);
  if (!issue.has_value()) {
    return std::nullopt;
  }
  switch (*issue) {
    case routing::RouteRequestAdmissionIssue::kRoutingProfileNetMismatch:
      return Failure(PlanarGpuFailureCode::kInvalidInput,
                     "GPU route request does not name the Board IR routing-profile net");
    case routing::RouteRequestAdmissionIssue::kInvalidOrCoincidentEndpoints:
      return Failure(PlanarGpuFailureCode::kInvalidInput,
                     "GPU route endpoints must be distinct valid exact coordinates");
    case routing::RouteRequestAdmissionIssue::kRequiresExactlyTwoTerminals:
      return Failure(PlanarGpuFailureCode::kInvalidInput,
                     "M1 GPU routing requires exactly two target terminals");
    case routing::RouteRequestAdmissionIssue::kMissingTerminal:
      return Failure(PlanarGpuFailureCode::kInvalidInput,
                     "GPU route request refers to missing target terminals");
    case routing::RouteRequestAdmissionIssue::kEndpointsNotTerminalCenters:
      return Failure(PlanarGpuFailureCode::kInvalidInput,
                     "M1 GPU route endpoints must be the two exact terminal centers");
    case routing::RouteRequestAdmissionIssue::kUnsupportedTerminalLayer:
      return Failure(PlanarGpuFailureCode::kInvalidInput,
                     "Requested endpoint layer is not a terminal connection layer");
    case routing::RouteRequestAdmissionIssue::kLayerOutsideRuleBucket:
      return Failure(PlanarGpuFailureCode::kInvalidInput,
                     "Requested endpoint layer is not in the compiled M1 rule bucket");
  }
  return Failure(PlanarGpuFailureCode::kInternalInvariant,
                 "Route-request admission validator returned an unknown issue");
}

[[nodiscard]] std::optional<Direction> DirectionBetween(const DeviceNodeV1& start,
                                                        const DeviceNodeV1& end) noexcept {
  if (start.layer != end.layer) {
    return std::nullopt;
  }
  return routing::DirectionBetween(LatticeIndex{.x = start.lattice_x, .y = start.lattice_y},
                                   LatticeIndex{.x = end.lattice_x, .y = end.lattice_y});
}

struct DeviceEndpoints {
  LatticeIndex start;
  LatticeIndex goal;
  std::uint32_t start_node;
  std::uint32_t goal_node;
};

using DeviceEndpointResult = std::variant<DeviceEndpoints, PlanarGpuFailure>;

[[nodiscard]] DeviceEndpointResult ResolveDeviceEndpoints(const CompiledBoard& compiled,
                                                          const DeviceCompiledBoardV1& device,
                                                          const routing::CpuRouteRequest& request) {
  const routing::PlanarEndpointResult resolved = routing::ResolvePlanarEndpoints(compiled, request);
  if (std::holds_alternative<routing::PlanarEndpointIssue>(resolved)) {
    if (std::get<routing::PlanarEndpointIssue>(resolved) ==
        routing::PlanarEndpointIssue::kNotOnCompilerLattice) {
      return Failure(PlanarGpuFailureCode::kInvalidInput,
                     "GPU route endpoints must lie exactly on the compiler lattice");
    }
    return Failure(PlanarGpuFailureCode::kInvalidInput,
                   "GPU route endpoints must both be represented by active sparse tiles");
  }
  const routing::ResolvedPlanarEndpoints& endpoints =
      std::get<routing::ResolvedPlanarEndpoints>(resolved);
  const std::optional<std::uint32_t> start_node =
      FindDeviceNodeIndex(device, request.start_layer, endpoints.start);
  const std::optional<std::uint32_t> goal_node =
      FindDeviceNodeIndex(device, request.goal_layer, endpoints.goal);
  if (!start_node.has_value() || !goal_node.has_value()) {
    return Failure(PlanarGpuFailureCode::kInvalidInput,
                   "GPU route endpoints must both be represented by active sparse tiles");
  }
  return DeviceEndpoints{.start = endpoints.start,
                         .goal = endpoints.goal,
                         .start_node = *start_node,
                         .goal_node = *goal_node};
}

[[nodiscard]] std::optional<PlanarGpuFailure> ValidateRawAssociations(
    const DeviceCompiledBoardV1& device, std::uint32_t start_node, std::uint32_t goal_node,
    PlanarGenerator generator, const UntrustedKernelResult& untrusted) {
  if (untrusted.schema_version != kDeviceCompiledBoardSchemaVersion ||
      untrusted.source_board_content_hash != device.header.source_board_content_hash ||
      untrusted.compiler_profile_fingerprint != device.header.compiler_profile_fingerprint ||
      untrusted.compiler_version != device.header.compiler_version ||
      untrusted.rule_bucket_identity != device.header.rule_bucket_identity ||
      untrusted.device_view_fingerprint != device.header.device_view_fingerprint) {
    return Failure(PlanarGpuFailureCode::kValidationFailed,
                   "GPU result associations do not match the immutable device view");
  }
  if (untrusted.start_node != start_node || untrusted.goal_node != goal_node ||
      untrusted.generator != generator) {
    return Failure(PlanarGpuFailureCode::kValidationFailed,
                   "GPU result endpoint or generator associations do not match the route request");
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<PlanarGpuFailure> ValidateKernelTelemetry(
    const DeviceCompiledBoardV1& device, PlanarGenerator generator,
    const UntrustedKernelResult& untrusted) {
  if (untrusted.telemetry.persistent_device_bytes !=
      device.header.estimated_persistent_device_bytes) {
    return Failure(PlanarGpuFailureCode::kInternalInvariant,
                   "GPU persistent-memory telemetry disagrees with deterministic accounting");
  }
  UWide expected_batch_bytes = sizeof(DeviceResultHeaderV1);
  expected_batch_bytes += static_cast<UWide>(device.header.represented_states) *
                          (sizeof(std::uint64_t) + sizeof(std::uint32_t));
  if (generator == PlanarGenerator::kBucketedFrontier) {
    expected_batch_bytes +=
        static_cast<UWide>(device.header.represented_states) * 2 * sizeof(std::uint32_t) +
        2 * sizeof(std::uint64_t);
  } else if (generator == PlanarGenerator::kHeadingAwareSweep) {
    expected_batch_bytes +=
        static_cast<UWide>(device.header.represented_nodes) * 8 * sizeof(std::uint64_t) +
        2 * sizeof(std::uint64_t);
  } else {
    return Failure(PlanarGpuFailureCode::kValidationFailed,
                   "GPU telemetry validation names an unknown planar generator", std::nullopt,
                   std::nullopt, "gpu.generator.unknown.v1");
  }
  if (expected_batch_bytes > std::numeric_limits<std::uint64_t>::max() ||
      untrusted.telemetry.batch_device_bytes != static_cast<std::uint64_t>(expected_batch_bytes)) {
    return Failure(PlanarGpuFailureCode::kInternalInvariant,
                   "GPU batch-memory telemetry disagrees with deterministic accounting");
  }
  const std::optional<std::uint64_t> expected_peak = routing::CheckedAdd(
      untrusted.telemetry.persistent_device_bytes, untrusted.telemetry.batch_device_bytes);
  if (!expected_peak.has_value() || untrusted.telemetry.peak_device_bytes != *expected_peak) {
    return Failure(PlanarGpuFailureCode::kInternalInvariant,
                   "GPU peak-memory telemetry disagrees with simultaneous owned tiers");
  }
  if (!std::isfinite(untrusted.telemetry.kernel_milliseconds) ||
      untrusted.telemetry.kernel_milliseconds < 0.0) {
    return Failure(PlanarGpuFailureCode::kInternalInvariant,
                   "GPU kernel-time telemetry is not finite and nonnegative");
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<PlanarGpuFailure> ValidateCompletionTelemetry(
    const UntrustedKernelResult& untrusted, std::uint32_t maximum_rounds) {
  switch (untrusted.completion) {
    case KernelCompletion::kReached:
    case KernelCompletion::kDisconnected:
    case KernelCompletion::kBudgetExhausted:
    case KernelCompletion::kCancelled:
      break;
    default:
      return Failure(PlanarGpuFailureCode::kInternalInvariant,
                     "GPU result contains an unknown completion code", std::nullopt, std::nullopt,
                     "gpu.completion.unknown.v1");
  }
  if (untrusted.telemetry.rounds > maximum_rounds) {
    return Failure(PlanarGpuFailureCode::kInternalInvariant,
                   "GPU round telemetry exceeds the configured execution budget");
  }
  if (untrusted.completion == KernelCompletion::kBudgetExhausted &&
      untrusted.telemetry.rounds != maximum_rounds) {
    return Failure(PlanarGpuFailureCode::kInternalInvariant,
                   "GPU reported budget exhaustion before consuming its round budget");
  }
  if ((untrusted.completion == KernelCompletion::kReached ||
       untrusted.completion == KernelCompletion::kDisconnected) &&
      untrusted.telemetry.rounds == 0) {
    return Failure(PlanarGpuFailureCode::kInternalInvariant,
                   "GPU reported a converged completion without an execution round");
  }
  return std::nullopt;
}

enum class PredecessorCostValidation : std::uint8_t {
  kExact,
  kPartial,
};

[[nodiscard]] std::optional<PlanarGpuFailure> ValidateAllPredecessors(
    const CompiledBoard& compiled, const DeviceCompiledBoardV1& device, std::uint32_t start_node,
    const UntrustedKernelResult& untrusted, PredecessorCostValidation cost_validation) {
  const std::uint64_t state_count = device.header.represented_states;
  if (untrusted.labels.size() != state_count || untrusted.predecessors.size() != state_count) {
    return Failure(PlanarGpuFailureCode::kInternalInvariant,
                   "GPU label or predecessor array has an invalid bound");
  }
  if (start_node >= device.nodes.size() ||
      StateIndex(start_node, kNoIncomingHeading) >= state_count) {
    return Failure(PlanarGpuFailureCode::kInternalInvariant,
                   "GPU start node maps outside the flattened state array");
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
    if (predecessor == kInvalidStateIndex &&
        cost_validation == PredecessorCostValidation::kPartial) {
      // A bounded sweep can improve a label during the final round after its
      // stable predecessor-selection pass has observed an older departure
      // snapshot. Such a finite state is structurally valid but is not
      // reconstructible until a later converged round.
      continue;
    }
    if (predecessor >= state_count) {
      return Failure(PlanarGpuFailureCode::kInternalInvariant,
                     "GPU predecessor index is out of bounds");
    }
    if (predecessor == state) {
      return Failure(PlanarGpuFailureCode::kInternalInvariant,
                     "GPU predecessor is self-referential", std::nullopt, std::nullopt,
                     "gpu.predecessor.self_reference.v1");
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
    const std::optional<std::uint64_t> expected = routing::CheckedAdd(
        predecessor_label, routing::StepCost(compiled.profile(), direction, predecessor_heading));
    const bool invalid_cost =
        !expected.has_value() ||
        (cost_validation == PredecessorCostValidation::kExact ? *expected != label
                                                              : *expected > label);
    if (invalid_cost) {
      return Failure(PlanarGpuFailureCode::kInternalInvariant,
                     cost_validation == PredecessorCostValidation::kExact
                         ? "GPU predecessor label does not reconstruct the exact scalar cost"
                         : "GPU partial predecessor label violates the scalar-cost lower bound");
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

[[nodiscard]] std::optional<PlanarGpuFailure> ValidateRouteAdmission(
    const board_ir::BoardSnapshot& board, const CompiledBoard& compiled,
    const routing::CpuRouteRequest& request, const PlanarRoutePolicy& policy) {
  if (policy.maximum_rounds == 0) {
    return Failure(PlanarGpuFailureCode::kInvalidInput, "GPU route round budget must be positive");
  }
  if (policy.generator != PlanarGenerator::kBucketedFrontier &&
      policy.generator != PlanarGenerator::kHeadingAwareSweep) {
    return Failure(PlanarGpuFailureCode::kInvalidInput,
                   "GPU route policy names an unknown planar generator");
  }
  if (std::optional<PlanarGpuFailure> invalid = ValidateRequest(board, compiled, request);
      invalid.has_value()) {
    return invalid;
  }
  if (request.start_layer != request.goal_layer) {
    return Failure(PlanarGpuFailureCode::kUnsupported,
                   "Planar Phase 2 GPU kernels cannot invent via padstack legality");
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<PlanarGpuFailure> ValidatePreparedAssociations(
    const board_ir::BoardSnapshot& board, const CompiledBoard& compiled,
    const DeviceCompiledBoardV1& device) {
  if (routing::ValidateCompiledBoardAssociation(board, compiled).has_value()) {
    return Failure(PlanarGpuFailureCode::kValidationFailed,
                   "Prepared GPU view no longer matches the BoardSnapshot and CompiledBoard");
  }
  if (device.header.schema_version != kDeviceCompiledBoardSchemaVersion ||
      device.header.source_board_content_hash != compiled.source_board_content_hash() ||
      device.header.compiler_version != compiled.compiler_version() ||
      device.header.compiler_profile_fingerprint != compiled.compiler_profile_fingerprint() ||
      device.header.rule_bucket_identity != compiled.rule_bucket().identity) {
    return Failure(PlanarGpuFailureCode::kValidationFailed,
                   "Prepared GPU view associations do not match this compiled route field");
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
  if (generator != PlanarGenerator::kBucketedFrontier &&
      generator != PlanarGenerator::kHeadingAwareSweep) {
    return Failure(PlanarGpuFailureCode::kInvalidInput,
                   "GPU route validation names an unknown planar generator");
  }
  DeviceEndpointResult endpoint_result =
      ResolveDeviceEndpoints(compiled_board, device_board, request);
  if (std::holds_alternative<PlanarGpuFailure>(endpoint_result)) {
    return std::get<PlanarGpuFailure>(std::move(endpoint_result));
  }
  const DeviceEndpoints endpoints = std::get<DeviceEndpoints>(endpoint_result);
  if (std::optional<PlanarGpuFailure> invalid =
          ValidateKernelTelemetry(device_board, generator, untrusted);
      invalid.has_value()) {
    return std::move(*invalid);
  }
  if (std::optional<PlanarGpuFailure> association = ValidateRawAssociations(
          device_board, endpoints.start_node, endpoints.goal_node, generator, untrusted);
      association.has_value()) {
    return WithTelemetry(std::move(*association), untrusted.telemetry);
  }
  if (untrusted.completion != KernelCompletion::kReached) {
    return Failure(PlanarGpuFailureCode::kInternalInvariant,
                   "Route reconstruction requires a reached GPU result", std::nullopt,
                   untrusted.telemetry);
  }
  if (std::optional<PlanarGpuFailure> invalid =
          ValidateAllPredecessors(compiled_board, device_board, endpoints.start_node, untrusted,
                                  PredecessorCostValidation::kExact);
      invalid.has_value()) {
    return WithTelemetry(std::move(*invalid), untrusted.telemetry);
  }

  std::uint64_t best_goal_cost = kInfiniteRouteCost;
  std::uint32_t best_goal_state = kInvalidStateIndex;
  for (std::uint8_t heading = 0; heading < 8; ++heading) {
    const std::uint32_t state = StateIndex(endpoints.goal_node, heading);
    if (untrusted.labels[state] < best_goal_cost ||
        (untrusted.labels[state] == best_goal_cost && state < best_goal_state)) {
      best_goal_cost = untrusted.labels[state];
      best_goal_state = state;
    }
  }
  if (best_goal_cost == kInfiniteRouteCost || untrusted.goal_state != best_goal_state) {
    return Failure(PlanarGpuFailureCode::kInternalInvariant,
                   "GPU goal state is not the stable minimum finite heading state", std::nullopt,
                   untrusted.telemetry);
  }

  const std::uint32_t start_state = StateIndex(endpoints.start_node, kNoIncomingHeading);
  std::vector<std::uint8_t> visited(
      static_cast<std::size_t>(device_board.header.represented_states), 0);
  std::vector<std::uint32_t> reversed_nodes;
  std::uint32_t cursor = best_goal_state;
  while (true) {
    if (cursor >= visited.size()) {
      return Failure(PlanarGpuFailureCode::kInternalInvariant,
                     "GPU reconstruction escaped the allocated state array", std::nullopt,
                     untrusted.telemetry);
    }
    if (visited[cursor] != 0) {
      return Failure(PlanarGpuFailureCode::kInternalInvariant,
                     "GPU predecessor reconstruction contains a cycle", std::nullopt,
                     untrusted.telemetry);
    }
    visited[cursor] = 1;
    reversed_nodes.push_back(NodeIndexForState(cursor));
    if (cursor == start_state) {
      break;
    }
    cursor = untrusted.predecessors[cursor];
    if (reversed_nodes.size() > device_board.header.represented_states) {
      return Failure(PlanarGpuFailureCode::kInternalInvariant,
                     "GPU predecessor reconstruction exceeded its acyclic state bound",
                     std::nullopt, untrusted.telemetry);
    }
  }
  std::ranges::reverse(reversed_nodes);

  std::vector<board_ir::Point64> points;
  points.reserve(reversed_nodes.size());
  for (std::uint32_t node : reversed_nodes) {
    if (node >= device_board.nodes.size()) {
      return Failure(PlanarGpuFailureCode::kInternalInvariant,
                     "GPU reconstruction contains an invalid node index", std::nullopt,
                     untrusted.telemetry);
    }
    const DeviceNodeV1& flattened = device_board.nodes[node];
    const std::optional<board_ir::Point64> point = geometry_compiler::LatticeIndexToExactPoint(
        compiled_board.profile(), LatticeIndex{.x = flattened.lattice_x, .y = flattened.lattice_y});
    if (!point.has_value()) {
      return Failure(PlanarGpuFailureCode::kInternalInvariant,
                     "GPU reconstruction escaped the exact coordinate envelope", std::nullopt,
                     untrusted.telemetry);
    }
    points.push_back(*point);
  }
  if (points.empty() || points.front() != request.start || points.back() != request.goal) {
    return Failure(PlanarGpuFailureCode::kInternalInvariant,
                   "GPU reconstruction does not connect the exact requested endpoints",
                   std::nullopt, untrusted.telemetry);
  }

  std::vector<routing::LayerSegment> segments =
      routing::CoalesceSegments(request.start_layer, points);
  if (std::optional<routing::RouteFailure> invalid =
          routing::ValidateReconstructedRoute(board, compiled_board, request, segments);
      invalid.has_value()) {
    return Failure(PlanarGpuFailureCode::kValidationFailed,
                   "Exact GPU route validation failed: " + invalid->detail, invalid->obstacle,
                   untrusted.telemetry);
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

PreparedPlanarCompiledViewResult PreparePlanarCompiledView(const board_ir::BoardSnapshot& board,
                                                           const CompiledBoard& compiled_board,
                                                           IPlanarRouteBackend& backend) {
  DeviceCompiledBoardResult flattened = BuildDeviceCompiledBoardV1(board, compiled_board);
  if (std::holds_alternative<PlanarGpuFailure>(flattened)) {
    return std::get<PlanarGpuFailure>(std::move(flattened));
  }
  DeviceCompiledBoardV1 device = std::get<DeviceCompiledBoardV1>(std::move(flattened));
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
  return std::unique_ptr<PreparedPlanarCompiledView>(new PreparedPlanarCompiledView(
      backend, std::move(device), std::move(metadata), std::move(uploaded)));
}

PlanarGpuRouteResult RouteWithPreparedPlanarGpuBackend(const board_ir::BoardSnapshot& board,
                                                       const CompiledBoard& compiled_board,
                                                       const routing::CpuRouteRequest& request,
                                                       const PlanarRoutePolicy& policy,
                                                       PreparedPlanarCompiledView& prepared) {
  if (std::optional<PlanarGpuFailure> invalid =
          ValidateRouteAdmission(board, compiled_board, request, policy);
      invalid.has_value()) {
    return std::move(*invalid);
  }
  if (prepared.backend_ == nullptr || prepared.uploaded_ == nullptr) {
    return Failure(PlanarGpuFailureCode::kInternalInvariant,
                   "Prepared GPU view has no live backend upload");
  }
  if (std::optional<PlanarGpuFailure> invalid =
          ValidatePreparedAssociations(board, compiled_board, prepared.device_board_);
      invalid.has_value()) {
    return std::move(*invalid);
  }
  DeviceEndpointResult endpoint_result =
      ResolveDeviceEndpoints(compiled_board, prepared.device_board_, request);
  if (std::holds_alternative<PlanarGpuFailure>(endpoint_result)) {
    return std::get<PlanarGpuFailure>(std::move(endpoint_result));
  }
  const DeviceEndpoints endpoints = std::get<DeviceEndpoints>(endpoint_result);
  if (policy.cancellation != nullptr && policy.cancellation->load()) {
    return Failure(PlanarGpuFailureCode::kCancelled,
                   "GPU route was cancelled before device execution");
  }

  const BackendExecutionRequest execution_request{
      .generator = policy.generator,
      .start_node = endpoints.start_node,
      .goal_node = endpoints.goal_node,
      .maximum_rounds = policy.maximum_rounds,
      .maximum_device_bytes = policy.maximum_device_bytes,
      .cancellation = policy.cancellation,
  };
  ExecutionResult execution_result =
      prepared.backend_->ExecuteRoute(*prepared.uploaded_, execution_request);
  if (std::holds_alternative<BackendError>(execution_result)) {
    return BackendFailure(std::get<BackendError>(execution_result));
  }
  std::unique_ptr<PendingRouteExecution> execution =
      std::get<std::unique_ptr<PendingRouteExecution>>(std::move(execution_result));
  if (execution == nullptr) {
    return Failure(PlanarGpuFailureCode::kInternalInvariant,
                   "Backend returned a null route execution");
  }

  ReadbackResult readback_result = prepared.backend_->ReadbackRoute(*execution);
  if (std::holds_alternative<BackendError>(readback_result)) {
    return BackendFailure(std::get<BackendError>(readback_result));
  }
  UntrustedKernelResult untrusted = std::get<UntrustedKernelResult>(std::move(readback_result));
  if (std::optional<PlanarGpuFailure> invalid =
          ValidateKernelTelemetry(prepared.device_board_, policy.generator, untrusted);
      invalid.has_value()) {
    return std::move(*invalid);
  }
  if (std::optional<PlanarGpuFailure> association =
          ValidateRawAssociations(prepared.device_board_, endpoints.start_node, endpoints.goal_node,
                                  policy.generator, untrusted);
      association.has_value()) {
    return WithTelemetry(std::move(*association), untrusted.telemetry);
  }
  if (std::optional<PlanarGpuFailure> invalid =
          ValidateCompletionTelemetry(untrusted, policy.maximum_rounds);
      invalid.has_value()) {
    return WithTelemetry(std::move(*invalid), untrusted.telemetry);
  }
  if (untrusted.completion != KernelCompletion::kReached) {
    const PredecessorCostValidation predecessor_validation =
        untrusted.completion == KernelCompletion::kDisconnected
            ? PredecessorCostValidation::kExact
            : PredecessorCostValidation::kPartial;
    if (std::optional<PlanarGpuFailure> invalid =
            ValidateAllPredecessors(compiled_board, prepared.device_board_, endpoints.start_node,
                                    untrusted, predecessor_validation);
        invalid.has_value()) {
      return WithTelemetry(std::move(*invalid), untrusted.telemetry);
    }
  }
  switch (untrusted.completion) {
    case KernelCompletion::kReached:
      return ValidateAndReconstructGpuRoute(board, compiled_board, prepared.device_board_, request,
                                            policy.generator, prepared.metadata_, untrusted);
    case KernelCompletion::kDisconnected:
      if (std::optional<PlanarGpuFailure> invalid =
              ValidateDisconnectedResult(prepared.device_board_, untrusted);
          invalid.has_value()) {
        return WithTelemetry(std::move(*invalid), untrusted.telemetry);
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
                 "GPU result contains an unknown completion code", std::nullopt,
                 untrusted.telemetry, "gpu.completion.unknown.v1");
}

PlanarGpuRouteResult RouteWithPlanarGpuBackend(const board_ir::BoardSnapshot& board,
                                               const CompiledBoard& compiled_board,
                                               const routing::CpuRouteRequest& request,
                                               const PlanarRoutePolicy& policy,
                                               IPlanarRouteBackend& backend) {
  if (std::optional<PlanarGpuFailure> invalid =
          ValidateRouteAdmission(board, compiled_board, request, policy);
      invalid.has_value()) {
    return std::move(*invalid);
  }
  if (policy.cancellation != nullptr && policy.cancellation->load()) {
    return Failure(PlanarGpuFailureCode::kCancelled,
                   "GPU route was cancelled before device upload");
  }
  PreparedPlanarCompiledViewResult prepared_result =
      PreparePlanarCompiledView(board, compiled_board, backend);
  if (std::holds_alternative<PlanarGpuFailure>(prepared_result)) {
    return std::get<PlanarGpuFailure>(std::move(prepared_result));
  }
  std::unique_ptr<PreparedPlanarCompiledView> prepared =
      std::get<std::unique_ptr<PreparedPlanarCompiledView>>(std::move(prepared_result));
  if (prepared == nullptr) {
    return Failure(PlanarGpuFailureCode::kInternalInvariant,
                   "GPU preparation returned a null immutable device view");
  }
  return RouteWithPreparedPlanarGpuBackend(board, compiled_board, request, policy, *prepared);
}

}  // namespace apgar::gpu
