#include "apgar/gpu/planar_router.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <new>
#include <optional>
#include <ranges>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace apgar::gpu {

struct PlanarCandidateBatchItemEvidence {
  std::uint64_t query_id = 0;
  std::uint32_t input_ordinal = 0;
  std::uint64_t policy_identity = 0;
  std::uint32_t batch_schema_version = 0;
  std::uint64_t batch_id = 0;
  bool authenticated_cuda_producer = false;
  PlanarGpuRouteResult result;
};

PlanarCandidateBatchItem PlanarCandidateBatchItem::CreateUnsealed(std::uint64_t query_id,
                                                                  std::uint32_t input_ordinal,
                                                                  PlanarGpuRouteResult result) {
  PlanarCandidateBatchItem item;
  item.query_id_ = query_id;
  item.input_ordinal_ = input_ordinal;
  item.result_ = std::move(result);
  return item;
}

bool PlanarCandidateBatchItem::SetUnsealedPolicyIdentity(std::uint64_t policy_identity) noexcept {
  if (evidence_ != nullptr) {
    return false;
  }
  policy_identity_ = policy_identity;
  return true;
}

bool PlanarCandidateBatchItem::SetUnsealedResult(PlanarGpuRouteResult result) noexcept {
  if (evidence_ != nullptr) {
    return false;
  }
  result_ = std::move(result);
  return true;
}

PlanarGpuRouteResult PlanarCandidateBatchItem::TakeResult() {
  if (evidence_ != nullptr) {
    return evidence_->result;
  }
  return std::move(result_);
}

std::uint64_t PlanarCandidateBatchItem::query_id() const noexcept {
  return evidence_ == nullptr ? query_id_ : evidence_->query_id;
}

std::uint32_t PlanarCandidateBatchItem::input_ordinal() const noexcept {
  return evidence_ == nullptr ? input_ordinal_ : evidence_->input_ordinal;
}

std::uint64_t PlanarCandidateBatchItem::policy_identity() const noexcept {
  return evidence_ == nullptr ? policy_identity_ : evidence_->policy_identity;
}

const PlanarGpuRouteResult& PlanarCandidateBatchItem::result() const noexcept {
  return evidence_ == nullptr ? result_ : evidence_->result;
}

bool PlanarCandidateBatchItem::has_validated_route_evidence() const noexcept {
  return evidence_ != nullptr;
}

bool PlanarCandidateBatchItem::has_authenticated_cuda_producer_evidence() const noexcept {
  return evidence_ != nullptr && evidence_->authenticated_cuda_producer;
}

std::uint32_t PlanarCandidateBatchItem::validated_batch_schema_version() const noexcept {
  return evidence_ == nullptr ? 0 : evidence_->batch_schema_version;
}

std::uint64_t PlanarCandidateBatchItem::validated_batch_id() const noexcept {
  return evidence_ == nullptr ? 0 : evidence_->batch_id;
}

void PlanarCandidateBatchItem::SealValidatedRoute(std::uint32_t schema_version,
                                                  std::uint64_t batch_id,
                                                  bool authenticated_cuda_producer) {
  if (evidence_ != nullptr || !std::holds_alternative<PlanarGpuRoute>(result_) ||
      schema_version == 0 || batch_id == 0) {
    return;
  }
  evidence_ =
      std::make_shared<const PlanarCandidateBatchItemEvidence>(PlanarCandidateBatchItemEvidence{
          .query_id = query_id_,
          .input_ordinal = input_ordinal_,
          .policy_identity = policy_identity_,
          .batch_schema_version = schema_version,
          .batch_id = batch_id,
          .authenticated_cuda_producer = authenticated_cuda_producer,
          .result = std::move(result_),
      });
}

namespace {

void ReplaceUnsealedResult(PlanarCandidateBatchItem& item, PlanarGpuRouteResult result) {
  static_cast<void>(item.SetUnsealedResult(std::move(result)));
}

void AssignUnsealedPolicyIdentity(PlanarCandidateBatchItem& item, std::uint64_t policy_identity) {
  static_cast<void>(item.SetUnsealedPolicyIdentity(policy_identity));
}

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

[[nodiscard]] KernelTelemetry QueryKernelTelemetry(
    const CandidateQueryTelemetry& telemetry) noexcept {
  return KernelTelemetry{
      .examined_work = telemetry.examined_work,
      .heading_turn_relaxations = telemetry.heading_turn_relaxations,
      .rounds = telemetry.rounds,
  };
}

[[nodiscard]] KernelTelemetry CombinedKernelTelemetry(const CandidateBatchTelemetry& batch,
                                                      const KernelTelemetry& query) noexcept {
  return KernelTelemetry{
      .persistent_device_bytes = batch.persistent_device_bytes,
      .batch_device_bytes = batch.batch_device_bytes,
      .peak_device_bytes = batch.peak_device_bytes,
      .examined_work = query.examined_work,
      .heading_turn_relaxations = query.heading_turn_relaxations,
      .rounds = query.rounds,
      .kernel_milliseconds = batch.kernel_milliseconds,
  };
}

[[nodiscard]] PlanarGpuFailure BackendFailure(const BackendError& error) {
  switch (error.code) {
    case BackendErrorCode::kUnsupported:
      return Failure(PlanarGpuFailureCode::kUnsupported, error.detail);
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

using CompiledEndpointResult = std::variant<routing::ResolvedPlanarEndpoints, PlanarGpuFailure>;
using DeviceEndpointResult = std::variant<DeviceEndpoints, PlanarGpuFailure>;

[[nodiscard]] CompiledEndpointResult ResolveCompiledEndpoints(
    const CompiledBoard& compiled, const routing::CpuRouteRequest& request) {
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
  return std::get<routing::ResolvedPlanarEndpoints>(resolved);
}

[[nodiscard]] DeviceEndpointResult MapResolvedDeviceEndpoints(
    const DeviceCompiledBoardV1& device, const routing::CpuRouteRequest& request,
    const routing::ResolvedPlanarEndpoints& endpoints) {
  const std::optional<std::uint32_t> start_node =
      FindDeviceNodeIndex(device, request.start_layer, endpoints.start);
  const std::optional<std::uint32_t> goal_node =
      FindDeviceNodeIndex(device, request.goal_layer, endpoints.goal);
  if (!start_node.has_value() || !goal_node.has_value()) {
    return Failure(PlanarGpuFailureCode::kValidationFailed,
                   "Immutable device view omits a compiled endpoint accepted during host "
                   "preflight");
  }
  return DeviceEndpoints{.start = endpoints.start,
                         .goal = endpoints.goal,
                         .start_node = *start_node,
                         .goal_node = *goal_node};
}

[[nodiscard]] DeviceEndpointResult ResolveDeviceEndpoints(const CompiledBoard& compiled,
                                                          const DeviceCompiledBoardV1& device,
                                                          const routing::CpuRouteRequest& request) {
  CompiledEndpointResult resolved = ResolveCompiledEndpoints(compiled, request);
  if (std::holds_alternative<PlanarGpuFailure>(resolved)) {
    return std::get<PlanarGpuFailure>(std::move(resolved));
  }
  return MapResolvedDeviceEndpoints(device, request,
                                    std::get<routing::ResolvedPlanarEndpoints>(resolved));
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
    const UntrustedKernelResult& untrusted, PredecessorCostValidation cost_validation,
    const routing::CandidateGenerationPolicy* candidate_policy = nullptr) {
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
    std::optional<std::uint64_t> transition_cost;
    if (candidate_policy == nullptr) {
      transition_cost = routing::StepCost(compiled.profile(), direction, predecessor_heading);
    } else {
      const DeviceNodeV1& predecessor_device_node = device.nodes[predecessor_node];
      const std::optional<routing::EdgeResourceKey> resource =
          routing::CanonicalPhysicalEdgeResource(
              predecessor_device_node.layer,
              LatticeIndex{.x = predecessor_device_node.lattice_x,
                           .y = predecessor_device_node.lattice_y},
              direction);
      if (!resource.has_value() || routing::PolicyBansResource(*candidate_policy, *resource)) {
        return Failure(PlanarGpuFailureCode::kInternalInvariant,
                       "GPU predecessor traverses a banned or unaddressable resource", std::nullopt,
                       std::nullopt, "gpu.policy.banned_predecessor.v1");
      }
      transition_cost = routing::StepCostUnderPolicy(
          compiled.profile(), direction, predecessor_heading, *candidate_policy, *resource);
    }
    const std::optional<std::uint64_t> expected =
        transition_cost.has_value() ? routing::CheckedAdd(predecessor_label, *transition_cost)
                                    : std::nullopt;
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

[[nodiscard]] std::optional<PlanarGpuFailure> ConfirmDisconnectedWithCpuOracle(
    const board_ir::BoardSnapshot& board, const CompiledBoard& compiled,
    const routing::CpuRouteRequest& request,
    const routing::NormalizedCandidateGenerationPolicy& normalized_policy) {
  routing::CpuRouteRequest oracle_request = request;
  oracle_request.candidate_policy = normalized_policy.policy;
  try {
    routing::CpuRouteResult oracle = routing::RouteWithCpuAStar(board, compiled, oracle_request);
    if (std::holds_alternative<routing::CpuRoute>(oracle)) {
      return Failure(PlanarGpuFailureCode::kInternalInvariant,
                     "GPU reported disconnected for a query the CPU oracle reached under the "
                     "identical normalized policy",
                     std::nullopt, std::nullopt, "gpu.disconnected.cpu_reachable.v1");
    }
    const routing::RouteFailure& failure = std::get<routing::RouteFailure>(oracle);
    if (failure.code == routing::RouteFailureCode::kDisconnected) {
      return std::nullopt;
    }
    if (failure.code == routing::RouteFailureCode::kResourceExhausted) {
      return Failure(PlanarGpuFailureCode::kResourceExhausted,
                     "CPU oracle could not confirm GPU disconnection: " + failure.detail,
                     failure.obstacle);
    }
    if (failure.code == routing::RouteFailureCode::kValidationFailed) {
      return Failure(PlanarGpuFailureCode::kValidationFailed,
                     "CPU oracle could not confirm GPU disconnection: " + failure.detail,
                     failure.obstacle);
    }
    return Failure(PlanarGpuFailureCode::kInternalInvariant,
                   "CPU oracle could not classify a GPU disconnected result: " + failure.detail,
                   failure.obstacle);
  } catch (const std::bad_alloc&) {
    return Failure(PlanarGpuFailureCode::kResourceExhausted,
                   "CPU oracle allocation failed while confirming GPU disconnection");
  } catch (const std::length_error&) {
    return Failure(PlanarGpuFailureCode::kResourceExhausted,
                   "CPU oracle container bound was exceeded while confirming GPU disconnection");
  }
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

[[nodiscard]] PlanarGpuFailure CandidatePolicyFailure(const routing::CandidatePolicyError& error) {
  if (error.code == routing::CandidatePolicyErrorCode::kUnsupportedSchema ||
      error.code == routing::CandidatePolicyErrorCode::kUnsupportedObjective) {
    return Failure(PlanarGpuFailureCode::kUnsupported,
                   "GPU candidate policy is unsupported: " + error.detail);
  }
  return Failure(PlanarGpuFailureCode::kInvalidInput,
                 "GPU candidate policy is invalid: " + error.detail);
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

std::optional<std::uint64_t> EstimateCandidateBatchHostBytesV1(
    std::uint64_t input_query_count, std::uint64_t admitted_query_count,
    std::uint64_t policy_edge_count, std::uint64_t represented_states) noexcept {
  if (admitted_query_count > input_query_count ||
      (admitted_query_count == 0 && policy_edge_count != 0) ||
      policy_edge_count > routing::kMaximumPolicyResourceEntries) {
    return std::nullopt;
  }
  // Every input retains one bounded classification/result envelope. Only
  // admitted queries retain the two additional encoded/execution query
  // records, status words, normalized policies, and readback workspaces.
  constexpr UWide kPerInputEnvelopeBytes =
      sizeof(DeviceCandidateBatchQueryV1) + sizeof(DeviceCandidateBatchResultV1) + 128U;
  constexpr UWide kPerAdmittedEnvelopeBytes =
      2U * sizeof(DeviceCandidateBatchQueryV1) + 2U * sizeof(std::uint32_t);
  constexpr UWide kPerPolicyEdgeBytes = sizeof(DeviceCandidatePolicyEdgeV1) + 40U;
  constexpr UWide kPerStateWorkspaceBytes =
      sizeof(std::uint64_t) + sizeof(std::uint32_t) + 2U * sizeof(std::uint64_t);
  const UWide total_states = static_cast<UWide>(admitted_query_count) * represented_states;
  const UWide bytes = static_cast<UWide>(input_query_count) * kPerInputEnvelopeBytes +
                      static_cast<UWide>(admitted_query_count) * kPerAdmittedEnvelopeBytes +
                      static_cast<UWide>(policy_edge_count) * kPerPolicyEdgeBytes +
                      2U * total_states * kPerStateWorkspaceBytes;
  if (bytes > std::numeric_limits<std::uint64_t>::max()) {
    return std::nullopt;
  }
  return static_cast<std::uint64_t>(bytes);
}

std::optional<std::uint64_t> EstimateCandidateBatchPolicyPreflightHostBytesV1(
    std::uint64_t input_query_count, std::uint64_t submitted_policy_entry_count) noexcept {
  if (submitted_policy_entry_count > routing::kMaximumPolicyResourceEntries) {
    return std::nullopt;
  }
  constexpr UWide kPerInputEnvelopeBytes =
      sizeof(DeviceCandidateBatchQueryV1) + sizeof(DeviceCandidateBatchResultV1) + 128U;
  constexpr UWide kPerNormalizedPolicyEntryBytes = 40U;
  const UWide bytes =
      static_cast<UWide>(input_query_count) * kPerInputEnvelopeBytes +
      static_cast<UWide>(submitted_policy_entry_count) * kPerNormalizedPolicyEntryBytes;
  if (bytes > std::numeric_limits<std::uint64_t>::max()) {
    return std::nullopt;
  }
  return static_cast<std::uint64_t>(bytes);
}

std::optional<std::uint64_t> EstimateCandidateBatchHostBytesV1(
    std::uint64_t query_count, std::uint64_t policy_edge_count,
    std::uint64_t represented_states) noexcept {
  return EstimateCandidateBatchHostBytesV1(query_count, query_count, policy_edge_count,
                                           represented_states);
}

CandidateBatchExecutionResult IPlanarRouteBackend::ExecuteCandidateBatch(
    const UploadedCompiledView&, const BackendCandidateBatchExecutionRequest&) {
  return BackendError{.code = BackendErrorCode::kUnsupported,
                      .detail = "Backend does not implement candidate-batch execution"};
}

CandidateBatchReadbackResult IPlanarRouteBackend::ReadbackCandidateBatch(
    const PendingCandidateBatchExecution&) {
  return BackendError{.code = BackendErrorCode::kUnsupported,
                      .detail = "Backend does not implement candidate-batch readback"};
}

PlanarGpuRouteResult ValidateAndReconstructGpuRouteWithPolicy(
    const board_ir::BoardSnapshot& board, const CompiledBoard& compiled_board,
    const DeviceCompiledBoardV1& device_board, const routing::CpuRouteRequest& request,
    PlanarGenerator generator, const BackendMetadata& backend,
    const UntrustedKernelResult& untrusted,
    const routing::NormalizedCandidateGenerationPolicy* candidate_policy,
    bool batch_telemetry_validated = false) {
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
  if (!batch_telemetry_validated) {
    if (std::optional<PlanarGpuFailure> invalid =
            ValidateKernelTelemetry(device_board, generator, untrusted);
        invalid.has_value()) {
      return std::move(*invalid);
    }
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
  if (std::optional<PlanarGpuFailure> invalid = ValidateAllPredecessors(
          compiled_board, device_board, endpoints.start_node, untrusted,
          PredecessorCostValidation::kExact,
          candidate_policy == nullptr ? nullptr : &candidate_policy->policy);
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
      .policy_identity = candidate_policy == nullptr ? 0 : candidate_policy->identity,
      .backend = backend,
      .total_cost = best_goal_cost,
      .lattice_path = std::move(points),
      .segments = std::move(segments),
      .telemetry = untrusted.telemetry,
  };
}

PlanarGpuRouteResult ValidateAndReconstructGpuRoute(const board_ir::BoardSnapshot& board,
                                                    const CompiledBoard& compiled_board,
                                                    const DeviceCompiledBoardV1& device_board,
                                                    const routing::CpuRouteRequest& request,
                                                    PlanarGenerator generator,
                                                    const BackendMetadata& backend,
                                                    const UntrustedKernelResult& untrusted) {
  routing::CandidatePolicyResult normalized_result =
      routing::NormalizeCandidateGenerationPolicy(compiled_board, request.candidate_policy);
  if (std::holds_alternative<routing::CandidatePolicyError>(normalized_result)) {
    return CandidatePolicyFailure(std::get<routing::CandidatePolicyError>(normalized_result));
  }
  const routing::NormalizedCandidateGenerationPolicy normalized_policy =
      std::get<routing::NormalizedCandidateGenerationPolicy>(std::move(normalized_result));
  return ValidateAndReconstructGpuRouteWithPolicy(board, compiled_board, device_board, request,
                                                  generator, backend, untrusted, &normalized_policy,
                                                  false);
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

PreparedPlanarCompiledViewResult PrepareCudaPlanarCompiledView(const board_ir::BoardSnapshot& board,
                                                               const CompiledBoard& compiled_board,
                                                               IPlanarRouteBackend& backend) {
  if (dynamic_cast<CudaPlanarRouteBackend*>(&backend) == nullptr) {
    return Failure(PlanarGpuFailureCode::kUnsupported,
                   "CUDA provenance preparation requires the exact final backend type built by "
                   "the checksum-pinned CUDA target",
                   std::nullopt, std::nullopt, "gpu.producer.authentication.v1");
  }
  PreparedPlanarCompiledViewResult prepared_result =
      PreparePlanarCompiledView(board, compiled_board, backend);
  if (auto* prepared = std::get_if<std::unique_ptr<PreparedPlanarCompiledView>>(&prepared_result);
      prepared != nullptr && *prepared != nullptr) {
    (*prepared)->authenticated_cuda_producer_ = true;
  }
  return prepared_result;
}

PlanarGpuRouteResult RouteWithPreparedPlanarGpuBackend(const board_ir::BoardSnapshot& board,
                                                       const CompiledBoard& compiled_board,
                                                       const routing::CpuRouteRequest& request,
                                                       const PlanarRoutePolicy& policy,
                                                       PreparedPlanarCompiledView& prepared) try {
  if (std::optional<PlanarGpuFailure> invalid =
          ValidateRouteAdmission(board, compiled_board, request, policy);
      invalid.has_value()) {
    return std::move(*invalid);
  }
  routing::CandidatePolicyResult normalized_result =
      routing::NormalizeCandidateGenerationPolicy(compiled_board, request.candidate_policy);
  if (std::holds_alternative<routing::CandidatePolicyError>(normalized_result)) {
    return CandidatePolicyFailure(std::get<routing::CandidatePolicyError>(normalized_result));
  }
  const routing::NormalizedCandidateGenerationPolicy normalized_policy =
      std::get<routing::NormalizedCandidateGenerationPolicy>(std::move(normalized_result));
  if (request.candidate_policy != routing::CandidateGenerationPolicy{}) {
    const std::uint64_t identity = normalized_policy.identity;
    const PlanarCandidateBatchQuery batch_query{
        .query_id = 1,
        .input_ordinal = request.candidate_policy.candidate_ordinal,
        .request = request,
    };
    const PlanarCandidateBatchPolicy batch_policy{
        .batch_id = identity == 0 ? 1 : identity,
        .generator = policy.generator,
        .maximum_rounds = policy.maximum_rounds,
        .maximum_device_bytes = policy.maximum_device_bytes,
        .cancellation = policy.cancellation,
    };
    PlanarCandidateBatchResult batch = RouteCandidateBatchWithPreparedPlanarGpuBackend(
        board, compiled_board, std::span(&batch_query, 1), batch_policy, prepared);
    if (std::holds_alternative<PlanarGpuFailure>(batch)) {
      return std::get<PlanarGpuFailure>(std::move(batch));
    }
    PlanarCandidateBatch& completed = std::get<PlanarCandidateBatch>(batch);
    if (completed.items.size() != 1) {
      return Failure(PlanarGpuFailureCode::kInternalInvariant,
                     "GPU batch-of-one returned a non-unit result count");
    }
    PlanarGpuRouteResult result = completed.items.front().TakeResult();
    if (std::holds_alternative<PlanarGpuRoute>(result)) {
      PlanarGpuRoute& route = std::get<PlanarGpuRoute>(result);
      route.telemetry = CombinedKernelTelemetry(completed.telemetry, route.telemetry);
    } else {
      PlanarGpuFailure& failure = std::get<PlanarGpuFailure>(result);
      if (failure.telemetry.has_value()) {
        failure.telemetry = CombinedKernelTelemetry(completed.telemetry, *failure.telemetry);
      }
    }
    return result;
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
      return ValidateAndReconstructGpuRouteWithPolicy(board, compiled_board, prepared.device_board_,
                                                      request, policy.generator, prepared.metadata_,
                                                      untrusted, &normalized_policy, true);
    case KernelCompletion::kDisconnected:
      if (std::optional<PlanarGpuFailure> invalid =
              ValidateDisconnectedResult(prepared.device_board_, untrusted);
          invalid.has_value()) {
        return WithTelemetry(std::move(*invalid), untrusted.telemetry);
      }
      if (std::optional<PlanarGpuFailure> unconfirmed =
              ConfirmDisconnectedWithCpuOracle(board, compiled_board, request, normalized_policy);
          unconfirmed.has_value()) {
        return WithTelemetry(std::move(*unconfirmed), untrusted.telemetry);
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
} catch (const std::bad_alloc&) {
  return Failure(PlanarGpuFailureCode::kResourceExhausted,
                 "GPU route host allocation failed during prepared execution");
} catch (const std::length_error&) {
  return Failure(PlanarGpuFailureCode::kResourceExhausted,
                 "GPU route host container capacity was exceeded during prepared execution");
}

PlanarGpuRouteResult RouteWithPlanarGpuBackend(const board_ir::BoardSnapshot& board,
                                               const CompiledBoard& compiled_board,
                                               const routing::CpuRouteRequest& request,
                                               const PlanarRoutePolicy& policy,
                                               IPlanarRouteBackend& backend) try {
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
} catch (const std::bad_alloc&) {
  return Failure(PlanarGpuFailureCode::kResourceExhausted,
                 "GPU route host allocation failed before prepared execution");
} catch (const std::length_error&) {
  return Failure(PlanarGpuFailureCode::kResourceExhausted,
                 "GPU route host container capacity was exceeded before prepared execution");
}

namespace {

struct AdmittedCandidateBatchQuery {
  std::size_t item_index = 0;
  routing::NormalizedCandidateGenerationPolicy normalized_policy;
  DeviceEndpoints endpoints;
  DeviceCandidateBatchQueryV1 device_query;
};

struct HostAdmittedCandidateBatchQuery {
  std::size_t item_index = 0;
  routing::NormalizedCandidateGenerationPolicy normalized_policy;
  routing::ResolvedPlanarEndpoints endpoints;
};

struct CandidateBatchHostPreflight {
  std::vector<const PlanarCandidateBatchQuery*> ordered;
  std::vector<PlanarCandidateBatchItem> items;
  std::vector<HostAdmittedCandidateBatchQuery> admitted;
  std::uint64_t aggregate_policy_edges = 0;
};

using CandidateBatchHostPreflightResult =
    std::variant<CandidateBatchHostPreflight, PlanarGpuFailure>;

[[nodiscard]] std::optional<PlanarGpuFailure> ValidateCandidateBatchShape(
    std::span<const PlanarCandidateBatchQuery> queries, const PlanarCandidateBatchPolicy& policy) {
  if (policy.schema_version != kDeviceCandidateBatchSchemaVersion) {
    return Failure(PlanarGpuFailureCode::kUnsupported,
                   "GPU candidate batch schema version is unsupported");
  }
  if (policy.batch_id == 0 || queries.empty() ||
      queries.size() > routing::kMaximumAlternativePolicyCount || policy.maximum_rounds == 0 ||
      (policy.generator != PlanarGenerator::kBucketedFrontier &&
       policy.generator != PlanarGenerator::kHeadingAwareSweep)) {
    return Failure(PlanarGpuFailureCode::kInvalidInput,
                   "GPU candidate batch requires bounded queries, nonzero identity and round "
                   "budget, and a known forced generator");
  }
  for (const PlanarCandidateBatchQuery& query : queries) {
    if (query.query_id == 0) {
      return Failure(PlanarGpuFailureCode::kInvalidInput,
                     "GPU candidate batch query identities must be nonzero");
    }
  }
  return std::nullopt;
}

[[nodiscard]] CandidateBatchHostPreflightResult PreflightCandidateBatch(
    const board_ir::BoardSnapshot& board, const CompiledBoard& compiled_board,
    std::span<const PlanarCandidateBatchQuery> queries, const PlanarCandidateBatchPolicy& policy) {
  if (std::optional<PlanarGpuFailure> invalid = ValidateCandidateBatchShape(queries, policy);
      invalid.has_value()) {
    return std::move(*invalid);
  }
  const UWide represented_states_wide =
      static_cast<UWide>(compiled_board.telemetry().represented_nodes) * kIncomingHeadingCount;
  if (represented_states_wide > std::numeric_limits<std::uint64_t>::max()) {
    return Failure(PlanarGpuFailureCode::kResourceExhausted,
                   "GPU candidate batch host-state accounting overflowed uint64");
  }
  const std::uint64_t represented_states = static_cast<std::uint64_t>(represented_states_wide);
  const std::optional<std::uint64_t> minimum_host_bytes =
      EstimateCandidateBatchHostBytesV1(queries.size(), 0, 0, represented_states);
  if (!minimum_host_bytes.has_value() || *minimum_host_bytes > policy.maximum_host_bytes) {
    return Failure(
        PlanarGpuFailureCode::kResourceExhausted,
        "GPU candidate batch input classification envelopes exceed the deterministic host-memory "
        "budget");
  }

  // Bound total policy work before any valid policy is copied, sorted, or
  // hashed. Individually invalid shapes remain cheap query-local failures and
  // therefore do not contribute to the valid-input aggregate.
  UWide submitted_policy_entries = 0;
  for (const PlanarCandidateBatchQuery& query : queries) {
    const routing::CandidateGenerationPolicy& candidate_policy = query.request.candidate_policy;
    if (!routing::CandidateGenerationPolicyShapeIsWithinV1Bounds(candidate_policy)) {
      continue;
    }
    submitted_policy_entries += candidate_policy.banned_resources.size();
    submitted_policy_entries += candidate_policy.resource_penalties.size();
    if (submitted_policy_entries > routing::kMaximumPolicyResourceEntries) {
      return Failure(
          PlanarGpuFailureCode::kResourceExhausted,
          "GPU candidate batch aggregate submitted policy entries exceed the v1 work bound "
          "before normalization");
    }
  }
  const std::optional<std::uint64_t> policy_preflight_host_bytes =
      EstimateCandidateBatchPolicyPreflightHostBytesV1(
          queries.size(), static_cast<std::uint64_t>(submitted_policy_entries));
  if (!policy_preflight_host_bytes.has_value() ||
      *policy_preflight_host_bytes > policy.maximum_host_bytes) {
    return Failure(
        PlanarGpuFailureCode::kResourceExhausted,
        "GPU candidate batch policy-normalization scratch exceeds the deterministic host-memory "
        "budget before normalization");
  }

  // The minimum all-input envelope and aggregate valid policy work are now
  // known to fit. Only after those allocation-free checks do we allocate
  // uniqueness and output bookkeeping used to preserve query-local outcomes.
  std::vector<std::uint64_t> query_ids;
  query_ids.reserve(queries.size());
  for (const PlanarCandidateBatchQuery& query : queries) {
    query_ids.push_back(query.query_id);
  }
  std::ranges::sort(query_ids);
  if (std::ranges::adjacent_find(query_ids) != query_ids.end()) {
    return Failure(PlanarGpuFailureCode::kInvalidInput,
                   "GPU candidate batch query identities must be unique");
  }

  CandidateBatchHostPreflight preflight;
  preflight.ordered.reserve(queries.size());
  for (const PlanarCandidateBatchQuery& query : queries) {
    preflight.ordered.push_back(&query);
  }
  std::ranges::sort(preflight.ordered, [](const PlanarCandidateBatchQuery* left,
                                          const PlanarCandidateBatchQuery* right) {
    return left->query_id < right->query_id;
  });
  preflight.items.reserve(preflight.ordered.size());
  preflight.admitted.reserve(preflight.ordered.size());
  const PlanarRoutePolicy route_policy{
      .generator = policy.generator,
      .maximum_rounds = policy.maximum_rounds,
      .maximum_device_bytes = policy.maximum_device_bytes,
      .cancellation = policy.cancellation,
  };
  UWide aggregate_policy_edges = 0;
  for (const PlanarCandidateBatchQuery* query_pointer : preflight.ordered) {
    const PlanarCandidateBatchQuery& query = *query_pointer;
    const std::size_t item_index = preflight.items.size();
    PlanarCandidateBatchItem item = PlanarCandidateBatchItem::CreateUnsealed(
        query.query_id, query.input_ordinal,
        Failure(PlanarGpuFailureCode::kInternalInvariant,
                "GPU candidate batch query was not classified"));
    routing::CandidatePolicyResult normalized =
        routing::NormalizeCandidateGenerationPolicy(compiled_board, query.request.candidate_policy);
    if (std::holds_alternative<routing::CandidatePolicyError>(normalized)) {
      ReplaceUnsealedResult(
          item, CandidatePolicyFailure(std::get<routing::CandidatePolicyError>(normalized)));
    } else {
      routing::NormalizedCandidateGenerationPolicy normalized_policy =
          std::get<routing::NormalizedCandidateGenerationPolicy>(std::move(normalized));
      AssignUnsealedPolicyIdentity(item, normalized_policy.identity);
      if (std::optional<PlanarGpuFailure> invalid =
              ValidateRouteAdmission(board, compiled_board, query.request, route_policy);
          invalid.has_value()) {
        ReplaceUnsealedResult(item, std::move(*invalid));
      } else {
        CompiledEndpointResult endpoint_result =
            ResolveCompiledEndpoints(compiled_board, query.request);
        if (std::holds_alternative<PlanarGpuFailure>(endpoint_result)) {
          ReplaceUnsealedResult(item, std::get<PlanarGpuFailure>(std::move(endpoint_result)));
        } else {
          aggregate_policy_edges += normalized_policy.policy.banned_resources.size();
          aggregate_policy_edges += normalized_policy.policy.resource_penalties.size();
          preflight.admitted.push_back(HostAdmittedCandidateBatchQuery{
              .item_index = item_index,
              .normalized_policy = std::move(normalized_policy),
              .endpoints = std::get<routing::ResolvedPlanarEndpoints>(std::move(endpoint_result)),
          });
        }
      }
    }
    preflight.items.push_back(std::move(item));
  }
  std::optional<PlanarGpuFailure> admitted_failure;
  if (aggregate_policy_edges > routing::kMaximumPolicyResourceEntries) {
    admitted_failure =
        Failure(PlanarGpuFailureCode::kResourceExhausted,
                "GPU candidate batch aggregate normalized policy entries exceed the v1 bound");
  } else {
    preflight.aggregate_policy_edges = static_cast<std::uint64_t>(aggregate_policy_edges);
    const std::optional<std::uint64_t> host_bytes =
        EstimateCandidateBatchHostBytesV1(queries.size(), preflight.admitted.size(),
                                          preflight.aggregate_policy_edges, represented_states);
    if (!host_bytes.has_value() || *host_bytes > policy.maximum_host_bytes) {
      admitted_failure =
          Failure(PlanarGpuFailureCode::kResourceExhausted,
                  "GPU candidate batch exceeds the deterministic host-memory budget");
    }
  }
  if (admitted_failure.has_value()) {
    for (const HostAdmittedCandidateBatchQuery& admitted : preflight.admitted) {
      ReplaceUnsealedResult(preflight.items[admitted.item_index], *admitted_failure);
    }
    preflight.admitted.clear();
    preflight.aggregate_policy_edges = 0;
  }
  return preflight;
}

[[nodiscard]] PlanarCandidateBatch BuildUnexecutedBatch(CandidateBatchHostPreflight preflight,
                                                        const PlanarCandidateBatchPolicy& policy,
                                                        const PlanarGpuFailure& admitted_failure,
                                                        std::string reason) {
  PlanarCandidateBatch output{
      .batch_id = policy.batch_id,
      .generator = policy.generator,
      .backend =
          BackendMetadata{
              .backend = "not-executed",
              .device_name = std::move(reason),
              .device_uuid = "none",
          },
      .telemetry = {},
      .items = std::move(preflight.items),
  };
  for (const HostAdmittedCandidateBatchQuery& admitted : preflight.admitted) {
    ReplaceUnsealedResult(output.items[admitted.item_index], admitted_failure);
  }
  return output;
}

[[nodiscard]] std::optional<std::uint32_t> PhysicalDeviceEdgeIndex(
    const DeviceCompiledBoardV1& device, const routing::EdgeResourceKey& resource) {
  const std::uint8_t direction = static_cast<std::uint8_t>(resource.direction);
  if (direction >= 4) {
    return std::nullopt;
  }
  const std::optional<std::uint32_t> node = FindDeviceNodeIndex(
      device, resource.layer, LatticeIndex{.x = resource.lattice_x, .y = resource.lattice_y});
  if (!node.has_value() ||
      (device.nodes[*node].legal_edges & geometry_compiler::MaskFor(resource.direction)) == 0) {
    return std::nullopt;
  }
  const UWide physical = static_cast<UWide>(*node) * 8 + direction;
  if (physical >= kInvalidStateIndex) {
    return std::nullopt;
  }
  return static_cast<std::uint32_t>(physical);
}

[[nodiscard]] std::optional<PlanarGpuFailure> AppendDevicePolicyEdges(
    const DeviceCompiledBoardV1& device,
    const routing::NormalizedCandidateGenerationPolicy& normalized,
    std::vector<DeviceCandidatePolicyEdgeV1>* edges, std::uint32_t* offset, std::uint32_t* count) {
  if (edges->size() > std::numeric_limits<std::uint32_t>::max()) {
    return Failure(PlanarGpuFailureCode::kResourceExhausted,
                   "GPU candidate batch policy-edge offset exceeds uint32 bounds");
  }
  *offset = static_cast<std::uint32_t>(edges->size());
  for (const routing::EdgeResourceKey& resource : normalized.policy.banned_resources) {
    const std::optional<std::uint32_t> physical = PhysicalDeviceEdgeIndex(device, resource);
    if (!physical.has_value()) {
      edges->resize(*offset);
      return Failure(PlanarGpuFailureCode::kValidationFailed,
                     "Normalized GPU candidate ban does not resolve to one compiled device edge");
    }
    edges->push_back(DeviceCandidatePolicyEdgeV1{
        .physical_edge_index = *physical,
        .adjustment = kBannedResourceAdjustment,
    });
  }
  for (const routing::ResourcePenalty& penalty : normalized.policy.resource_penalties) {
    const std::optional<std::uint32_t> physical = PhysicalDeviceEdgeIndex(device, penalty.resource);
    if (!physical.has_value()) {
      edges->resize(*offset);
      return Failure(
          PlanarGpuFailureCode::kValidationFailed,
          "Normalized GPU candidate penalty does not resolve to one compiled device edge");
    }
    edges->push_back(DeviceCandidatePolicyEdgeV1{
        .physical_edge_index = *physical,
        .adjustment = penalty.additional_cost,
    });
  }
  const std::size_t slice_count = edges->size() - *offset;
  if (slice_count != 0) {
    std::ranges::sort(edges->begin() + *offset, edges->end(), {},
                      &DeviceCandidatePolicyEdgeV1::physical_edge_index);
  }
  if (slice_count > std::numeric_limits<std::uint32_t>::max()) {
    edges->resize(*offset);
    return Failure(PlanarGpuFailureCode::kResourceExhausted,
                   "GPU candidate batch policy-edge count exceeds uint32 bounds");
  }
  for (std::size_t index = *offset + 1; index < edges->size(); ++index) {
    if ((*edges)[index - 1].physical_edge_index == (*edges)[index].physical_edge_index) {
      edges->resize(*offset);
      return Failure(PlanarGpuFailureCode::kInternalInvariant,
                     "Normalized GPU candidate policy maps multiple actions to one physical edge");
    }
  }
  *count = static_cast<std::uint32_t>(slice_count);
  return std::nullopt;
}

[[nodiscard]] std::optional<std::uint64_t> ExpectedCandidateBatchBytes(
    const DeviceCompiledBoardV1& device, const BackendCandidateBatchExecutionRequest& request) {
  UWide bytes = static_cast<UWide>(request.queries.size()) *
                (sizeof(DeviceCandidateBatchQueryV1) + sizeof(DeviceCandidateBatchResultV1) +
                 sizeof(std::uint32_t));
  bytes += static_cast<UWide>(request.policy_edges.size()) * sizeof(DeviceCandidatePolicyEdgeV1);
  const UWide total_states =
      static_cast<UWide>(device.header.represented_states) * request.queries.size();
  bytes +=
      total_states * (sizeof(std::uint64_t) + sizeof(std::uint32_t) + 2 * sizeof(std::uint64_t));
  if (request.generator == PlanarGenerator::kBucketedFrontier) {
    bytes += total_states * sizeof(std::uint8_t);
  } else {
    const UWide departure_count =
        static_cast<UWide>(device.header.represented_nodes) * 8 * request.queries.size();
    bytes += departure_count * sizeof(std::uint64_t);
    bytes += static_cast<UWide>(request.queries.size()) * sizeof(std::uint32_t);
  }
  if (bytes > std::numeric_limits<std::uint64_t>::max()) {
    return std::nullopt;
  }
  return static_cast<std::uint64_t>(bytes);
}

[[nodiscard]] std::optional<PlanarGpuFailure> ValidateCandidateBatchEnvelope(
    const DeviceCompiledBoardV1& device, const DeviceCandidateBatchQueryV1& expected,
    const UntrustedCandidateBatchQueryResult& untrusted, std::uint32_t maximum_rounds,
    std::uint32_t dispatched_rounds, std::uint32_t finalization_launch_count) {
  const DeviceCandidateBatchResultV1& header = untrusted.header;
  const KernelTelemetry query_telemetry = QueryKernelTelemetry(untrusted.telemetry);
  if (header.schema_version != kDeviceCandidateBatchSchemaVersion ||
      header.compiler_version != device.header.compiler_version ||
      header.input_ordinal != expected.input_ordinal || header.start_node != expected.start_node ||
      header.goal_node != expected.goal_node || header.generator != expected.generator ||
      header.batch_id != expected.batch_id || header.query_id != expected.query_id ||
      header.workspace_owner != expected.workspace_owner ||
      header.policy_identity != expected.policy_identity ||
      header.routing_profile_fingerprint != expected.routing_profile_fingerprint ||
      header.source_board_content_hash != device.header.source_board_content_hash ||
      header.compiler_profile_fingerprint != device.header.compiler_profile_fingerprint ||
      header.rule_bucket_identity != device.header.rule_bucket_identity ||
      header.device_view_fingerprint != device.header.device_view_fingerprint ||
      std::ranges::any_of(header.reserved, [](std::uint8_t byte) { return byte != 0; })) {
    return Failure(PlanarGpuFailureCode::kValidationFailed,
                   "GPU candidate batch result associations do not match its immutable query "
                   "workspace");
  }
  const std::uint64_t state_count = device.header.represented_states;
  if (untrusted.labels.size() != state_count || untrusted.predecessors.size() != state_count ||
      untrusted.state_owners.size() != state_count ||
      untrusted.predecessor_owners.size() != state_count) {
    return Failure(PlanarGpuFailureCode::kInternalInvariant,
                   "GPU candidate batch query-major workspace has invalid array bounds",
                   std::nullopt, query_telemetry, "gpu.batch.workspace.bounds.v1");
  }
  if (std::ranges::any_of(untrusted.state_owners,
                          [&](std::uint64_t owner) { return owner != expected.workspace_owner; }) ||
      std::ranges::any_of(untrusted.predecessor_owners,
                          [&](std::uint64_t owner) { return owner != expected.workspace_owner; })) {
    return Failure(PlanarGpuFailureCode::kInternalInvariant,
                   "GPU candidate batch workspace contains foreign query ownership", std::nullopt,
                   query_telemetry, "gpu.batch.workspace.owner.v1");
  }
  if (untrusted.telemetry.rounds != header.rounds || header.rounds > dispatched_rounds) {
    return Failure(PlanarGpuFailureCode::kInternalInvariant,
                   "GPU candidate query round telemetry disagrees with its result header or "
                   "shared dispatch envelope",
                   std::nullopt, query_telemetry, "gpu.batch.query.telemetry.v1");
  }
  if ((header.completion == KernelCompletion::kBudgetExhausted ||
       header.completion == KernelCompletion::kCancelled) &&
      finalization_launch_count != 1) {
    return Failure(PlanarGpuFailureCode::kInternalInvariant,
                   "GPU unfinished query completion lacks its shared finalization launch",
                   std::nullopt, query_telemetry, "gpu.batch.query.telemetry.v1");
  }
  UWide maximum_examined = 0;
  UWide maximum_turns = 0;
  if (expected.generator == PlanarGenerator::kBucketedFrontier) {
    maximum_examined = static_cast<UWide>(untrusted.telemetry.rounds) * 8U;
    maximum_turns = 0;
  } else if (expected.generator == PlanarGenerator::kHeadingAwareSweep) {
    maximum_examined =
        static_cast<UWide>(untrusted.telemetry.rounds) * device.header.represented_nodes * 8U;
    maximum_turns = maximum_examined * 8U;
  }
  if (untrusted.telemetry.examined_work > maximum_examined ||
      untrusted.telemetry.heading_turn_relaxations > maximum_turns) {
    return Failure(PlanarGpuFailureCode::kInternalInvariant,
                   "GPU candidate query work telemetry exceeds deterministic bounds", std::nullopt,
                   query_telemetry, "gpu.batch.query.telemetry.v1");
  }
  UntrustedKernelResult completion_view;
  completion_view.completion = header.completion;
  completion_view.telemetry = query_telemetry;
  return ValidateCompletionTelemetry(completion_view, maximum_rounds);
}

[[nodiscard]] std::optional<PlanarGpuFailure> ValidateCandidateBatchTelemetry(
    const DeviceCompiledBoardV1& device, const BackendCandidateBatchExecutionRequest& request,
    std::uint64_t expected_batch_bytes, std::uint64_t expected_host_bytes,
    const UntrustedCandidateBatchResult& untrusted) {
  const CandidateBatchTelemetry& telemetry = untrusted.telemetry;
  if (telemetry.persistent_device_bytes != device.header.estimated_persistent_device_bytes ||
      telemetry.batch_device_bytes != expected_batch_bytes ||
      telemetry.batch_host_bytes != expected_host_bytes) {
    return Failure(PlanarGpuFailureCode::kInternalInvariant,
                   "GPU candidate batch device or host memory telemetry disagrees with "
                   "deterministic accounting",
                   std::nullopt, std::nullopt, "gpu.batch.memory.accounting.v1");
  }
  const std::optional<std::uint64_t> peak =
      routing::CheckedAdd(telemetry.persistent_device_bytes, telemetry.batch_device_bytes);
  const std::uint32_t expected_chunk_rounds =
      request.generator == PlanarGenerator::kBucketedFrontier ? kCandidateFrontierChunkRounds
                                                              : kCandidateSweepChunkRounds;
  if (!peak.has_value() || telemetry.peak_device_bytes != *peak ||
      !std::isfinite(telemetry.kernel_milliseconds) || telemetry.kernel_milliseconds < 0.0 ||
      telemetry.chunk_rounds != expected_chunk_rounds ||
      telemetry.dispatched_rounds > request.maximum_rounds ||
      telemetry.finalization_launch_count > 1 ||
      (telemetry.dispatched_rounds == 0 && telemetry.finalization_launch_count != 1)) {
    return Failure(PlanarGpuFailureCode::kInternalInvariant,
                   "GPU candidate batch peak, timing, or chunk telemetry is invalid", std::nullopt,
                   std::nullopt, "gpu.batch.telemetry.v1");
  }

  const std::uint64_t expected_readbacks =
      telemetry.dispatched_rounds == 0
          ? 0
          : (static_cast<std::uint64_t>(telemetry.dispatched_rounds) + expected_chunk_rounds - 1U) /
                expected_chunk_rounds;
  UWide expected_launches = 2U;  // initialization and stable predecessor selection
  if (request.generator == PlanarGenerator::kBucketedFrontier) {
    expected_launches += expected_readbacks;
  } else {
    const UWide kernels_per_round = device.runs.empty() ? 3U : 4U;
    expected_launches += static_cast<UWide>(telemetry.dispatched_rounds) * kernels_per_round;
  }
  expected_launches += telemetry.finalization_launch_count;
  if (expected_launches > std::numeric_limits<std::uint64_t>::max() ||
      telemetry.kernel_launch_count != static_cast<std::uint64_t>(expected_launches) ||
      telemetry.blocking_status_readback_count != expected_readbacks) {
    return Failure(PlanarGpuFailureCode::kInternalInvariant,
                   "GPU candidate batch launch or blocking-readback telemetry is invalid",
                   std::nullopt, std::nullopt, "gpu.batch.telemetry.v1");
  }
  return std::nullopt;
}

[[nodiscard]] UntrustedKernelResult QueryKernelView(
    const UntrustedCandidateBatchQueryResult& query) {
  const DeviceCandidateBatchResultV1& header = query.header;
  return UntrustedKernelResult{
      .schema_version = kDeviceCompiledBoardSchemaVersion,
      .source_board_content_hash = header.source_board_content_hash,
      .compiler_profile_fingerprint = header.compiler_profile_fingerprint,
      .compiler_version = header.compiler_version,
      .rule_bucket_identity = header.rule_bucket_identity,
      .device_view_fingerprint = header.device_view_fingerprint,
      .generator = header.generator,
      .completion = header.completion,
      .start_node = header.start_node,
      .goal_node = header.goal_node,
      .goal_state = header.goal_state,
      .labels = query.labels,
      .predecessors = query.predecessors,
      .telemetry = QueryKernelTelemetry(query.telemetry),
  };
}

void SetAdmittedBatchFailure(std::span<const AdmittedCandidateBatchQuery> admitted,
                             const PlanarGpuFailure& failure, PlanarCandidateBatch* output) {
  for (const AdmittedCandidateBatchQuery& query : admitted) {
    ReplaceUnsealedResult(output->items[query.item_index], failure);
  }
}

}  // namespace

[[nodiscard]] static PlanarCandidateBatchResult RouteCandidateBatchWithPreparedPreflight(
    const board_ir::BoardSnapshot& board, const CompiledBoard& compiled_board,
    const PlanarCandidateBatchPolicy& policy, PreparedPlanarCompiledView& prepared,
    CandidateBatchHostPreflight preflight) {
  if (preflight.admitted.empty()) {
    return BuildUnexecutedBatch(std::move(preflight), policy,
                                Failure(PlanarGpuFailureCode::kInternalInvariant,
                                        "GPU candidate query unexpectedly remained admitted"),
                                "no-admitted-queries");
  }
  IPlanarRouteBackend& backend = prepared.backend();
  const UploadedCompiledView& upload = prepared.uploaded();
  const DeviceCompiledBoardV1& device = prepared.device_board();
  if (std::optional<PlanarGpuFailure> invalid =
          ValidatePreparedAssociations(board, compiled_board, device);
      invalid.has_value()) {
    return BuildUnexecutedBatch(std::move(preflight), policy, *invalid,
                                "prepared-view-association-failed");
  }

  PlanarCandidateBatch output{
      .batch_id = policy.batch_id,
      .device_view_fingerprint = device.header.device_view_fingerprint,
      .generator = policy.generator,
      .backend = prepared.metadata(),
      .telemetry = {},
      .items = std::move(preflight.items),
  };
  std::vector<AdmittedCandidateBatchQuery> admitted;
  admitted.reserve(preflight.admitted.size());
  BackendCandidateBatchExecutionRequest backend_request{
      .batch_id = policy.batch_id,
      .generator = policy.generator,
      .maximum_rounds = policy.maximum_rounds,
      .maximum_workspace_states_per_query = policy.maximum_workspace_states_per_query,
      .maximum_frontier_states_per_query = policy.maximum_frontier_states_per_query,
      .maximum_device_bytes = policy.maximum_device_bytes,
      .maximum_host_bytes = policy.maximum_host_bytes,
      .queries = {},
      .policy_edges = {},
      .cancellation = policy.cancellation,
  };

  for (HostAdmittedCandidateBatchQuery& host_admitted : preflight.admitted) {
    const std::size_t item_index = host_admitted.item_index;
    const PlanarCandidateBatchQuery& query = *preflight.ordered[item_index];
    routing::NormalizedCandidateGenerationPolicy normalized =
        std::move(host_admitted.normalized_policy);
    DeviceEndpointResult endpoint_result =
        MapResolvedDeviceEndpoints(device, query.request, host_admitted.endpoints);
    if (std::holds_alternative<PlanarGpuFailure>(endpoint_result)) {
      ReplaceUnsealedResult(output.items[item_index],
                            std::get<PlanarGpuFailure>(std::move(endpoint_result)));
      continue;
    }
    const DeviceEndpoints endpoints = std::get<DeviceEndpoints>(endpoint_result);
    std::uint32_t policy_offset = 0;
    std::uint32_t policy_count = 0;
    if (std::optional<PlanarGpuFailure> invalid = AppendDevicePolicyEdges(
            device, normalized, &backend_request.policy_edges, &policy_offset, &policy_count);
        invalid.has_value()) {
      ReplaceUnsealedResult(output.items[item_index], std::move(*invalid));
      continue;
    }
    const UWide workspace_offset =
        static_cast<UWide>(admitted.size()) * device.header.represented_states;
    if (workspace_offset > std::numeric_limits<std::uint64_t>::max()) {
      ReplaceUnsealedResult(output.items[item_index],
                            Failure(PlanarGpuFailureCode::kResourceExhausted,
                                    "GPU candidate batch workspace offset overflows uint64"));
      backend_request.policy_edges.resize(policy_offset);
      continue;
    }
    // query_id is already required to be unique and nonzero, making it a
    // collision-free deterministic workspace owner within the batch.
    const std::uint64_t owner = query.query_id;
    DeviceCandidateBatchQueryV1 device_query{
        .input_ordinal = query.input_ordinal,
        .batch_id = policy.batch_id,
        .query_id = query.query_id,
        .workspace_owner = owner,
        .policy_identity = normalized.identity,
        .routing_profile_fingerprint =
            routing::FingerprintRoutingProfile(board.data().routing_profile),
        .start_node = endpoints.start_node,
        .goal_node = endpoints.goal_node,
        .policy_offset = policy_offset,
        .policy_count = policy_count,
        .workspace_offset = static_cast<std::uint64_t>(workspace_offset),
        .workspace_state_count = device.header.represented_states,
        .frontier_state_capacity = policy.generator == PlanarGenerator::kBucketedFrontier
                                       ? device.header.represented_states
                                       : 0,
        .orthogonal_step_surcharge = normalized.policy.orthogonal_step_surcharge,
        .diagonal_step_surcharge = normalized.policy.diagonal_step_surcharge,
        .bend_surcharge = normalized.policy.bend_surcharge,
        .maximum_rounds = policy.maximum_rounds,
        .generator = policy.generator,
    };
    backend_request.queries.push_back(device_query);
    admitted.push_back(AdmittedCandidateBatchQuery{
        .item_index = item_index,
        .normalized_policy = std::move(normalized),
        .endpoints = endpoints,
        .device_query = device_query,
    });
  }
  if (admitted.empty()) {
    return output;
  }
  if (policy.maximum_workspace_states_per_query < device.header.represented_states) {
    SetAdmittedBatchFailure(
        admitted,
        Failure(PlanarGpuFailureCode::kResourceExhausted,
                "GPU candidate batch exceeds the per-query workspace-state budget"),
        &output);
    return output;
  }
  if (policy.generator == PlanarGenerator::kBucketedFrontier &&
      policy.maximum_frontier_states_per_query < device.header.represented_states) {
    SetAdmittedBatchFailure(
        admitted,
        Failure(PlanarGpuFailureCode::kResourceExhausted,
                "GPU candidate frontier exceeds the per-query frontier-state capacity"),
        &output);
    return output;
  }
  if (policy.cancellation != nullptr && policy.cancellation->load()) {
    SetAdmittedBatchFailure(admitted,
                            Failure(PlanarGpuFailureCode::kCancelled,
                                    "GPU candidate batch was cancelled before device execution"),
                            &output);
    return output;
  }
  const std::optional<std::uint64_t> expected_batch_bytes =
      ExpectedCandidateBatchBytes(device, backend_request);
  if (!expected_batch_bytes.has_value()) {
    SetAdmittedBatchFailure(
        admitted,
        Failure(PlanarGpuFailureCode::kResourceExhausted,
                "GPU candidate batch deterministic memory accounting overflowed uint64"),
        &output);
    return output;
  }
  const std::optional<std::uint64_t> expected_host_bytes = EstimateCandidateBatchHostBytesV1(
      backend_request.queries.size(), backend_request.policy_edges.size(),
      device.header.represented_states);
  if (!expected_host_bytes.has_value() || *expected_host_bytes > policy.maximum_host_bytes) {
    SetAdmittedBatchFailure(
        admitted,
        Failure(PlanarGpuFailureCode::kResourceExhausted,
                "GPU candidate batch exceeds the deterministic host-memory budget"),
        &output);
    return output;
  }

  CandidateBatchExecutionResult execution_result =
      backend.ExecuteCandidateBatch(upload, backend_request);
  if (std::holds_alternative<BackendError>(execution_result)) {
    SetAdmittedBatchFailure(admitted, BackendFailure(std::get<BackendError>(execution_result)),
                            &output);
    return output;
  }
  std::unique_ptr<PendingCandidateBatchExecution> execution =
      std::get<std::unique_ptr<PendingCandidateBatchExecution>>(std::move(execution_result));
  if (execution == nullptr) {
    SetAdmittedBatchFailure(admitted,
                            Failure(PlanarGpuFailureCode::kInternalInvariant,
                                    "Backend returned a null candidate-batch execution"),
                            &output);
    return output;
  }
  CandidateBatchReadbackResult readback_result = backend.ReadbackCandidateBatch(*execution);
  if (std::holds_alternative<BackendError>(readback_result)) {
    SetAdmittedBatchFailure(admitted, BackendFailure(std::get<BackendError>(readback_result)),
                            &output);
    return output;
  }
  UntrustedCandidateBatchResult readback =
      std::get<UntrustedCandidateBatchResult>(std::move(readback_result));
  if (readback.schema_version != kDeviceCandidateBatchSchemaVersion ||
      readback.batch_id != policy.batch_id || readback.generator != policy.generator) {
    SetAdmittedBatchFailure(
        admitted,
        Failure(PlanarGpuFailureCode::kValidationFailed,
                "GPU candidate batch readback associations do not match the execution request"),
        &output);
    return output;
  }

  std::set<std::uint64_t> admitted_query_ids;
  for (const AdmittedCandidateBatchQuery& query : admitted) {
    admitted_query_ids.insert(query.device_query.query_id);
  }
  const bool foreign_query =
      std::ranges::any_of(readback.queries, [&](const UntrustedCandidateBatchQueryResult& query) {
        return !admitted_query_ids.contains(query.header.query_id);
      });
  if (readback.queries.size() != admitted.size() || foreign_query) {
    SetAdmittedBatchFailure(
        admitted,
        Failure(PlanarGpuFailureCode::kInternalInvariant,
                "GPU candidate batch returned an extra, foreign, or missing query result",
                std::nullopt, std::nullopt, "gpu.batch.query.identity.v1"),
        &output);
    return output;
  }
  if (std::optional<PlanarGpuFailure> invalid = ValidateCandidateBatchTelemetry(
          device, backend_request, *expected_batch_bytes, *expected_host_bytes, readback);
      invalid.has_value()) {
    SetAdmittedBatchFailure(admitted, *invalid, &output);
    return output;
  }
  output.telemetry = readback.telemetry;

  std::map<std::uint64_t, const UntrustedCandidateBatchQueryResult*> by_query;
  std::set<std::uint64_t> duplicate_query_ids;
  for (const UntrustedCandidateBatchQueryResult& query : readback.queries) {
    if (!by_query.emplace(query.header.query_id, &query).second) {
      duplicate_query_ids.insert(query.header.query_id);
    }
  }
  for (const AdmittedCandidateBatchQuery& admitted_query : admitted) {
    PlanarCandidateBatchItem& item = output.items[admitted_query.item_index];
    const auto found = by_query.find(admitted_query.device_query.query_id);
    if (found == by_query.end() ||
        duplicate_query_ids.contains(admitted_query.device_query.query_id)) {
      ReplaceUnsealedResult(
          item, Failure(PlanarGpuFailureCode::kInternalInvariant,
                        "GPU candidate batch omitted or duplicated a query result", std::nullopt,
                        std::nullopt, "gpu.batch.query.identity.v1"));
      continue;
    }
    const UntrustedCandidateBatchQueryResult& untrusted = *found->second;
    if (std::optional<PlanarGpuFailure> invalid = ValidateCandidateBatchEnvelope(
            device, admitted_query.device_query, untrusted, policy.maximum_rounds,
            readback.telemetry.dispatched_rounds, readback.telemetry.finalization_launch_count);
        invalid.has_value()) {
      ReplaceUnsealedResult(item, std::move(*invalid));
      continue;
    }
    UntrustedKernelResult kernel = QueryKernelView(untrusted);
    if (kernel.completion != KernelCompletion::kReached) {
      const PredecessorCostValidation predecessor_validation =
          kernel.completion == KernelCompletion::kDisconnected
              ? PredecessorCostValidation::kExact
              : PredecessorCostValidation::kPartial;
      if (std::optional<PlanarGpuFailure> invalid = ValidateAllPredecessors(
              compiled_board, device, admitted_query.endpoints.start_node, kernel,
              predecessor_validation, &admitted_query.normalized_policy.policy);
          invalid.has_value()) {
        ReplaceUnsealedResult(item, WithTelemetry(std::move(*invalid), kernel.telemetry));
        continue;
      }
    }
    switch (kernel.completion) {
      case KernelCompletion::kReached:
        ReplaceUnsealedResult(
            item, ValidateAndReconstructGpuRouteWithPolicy(
                      board, compiled_board, device,
                      preflight.ordered[admitted_query.item_index]->request, policy.generator,
                      prepared.metadata(), kernel, &admitted_query.normalized_policy, true));
        break;
      case KernelCompletion::kDisconnected:
        if (std::optional<PlanarGpuFailure> invalid = ValidateDisconnectedResult(device, kernel);
            invalid.has_value()) {
          ReplaceUnsealedResult(item, WithTelemetry(std::move(*invalid), kernel.telemetry));
        } else if (std::optional<PlanarGpuFailure> unconfirmed = ConfirmDisconnectedWithCpuOracle(
                       board, compiled_board, preflight.ordered[admitted_query.item_index]->request,
                       admitted_query.normalized_policy);
                   unconfirmed.has_value()) {
          ReplaceUnsealedResult(item, WithTelemetry(std::move(*unconfirmed), kernel.telemetry));
        } else {
          ReplaceUnsealedResult(
              item, Failure(PlanarGpuFailureCode::kDisconnected,
                            "No planar path connects this candidate query under its policy",
                            std::nullopt, kernel.telemetry));
        }
        break;
      case KernelCompletion::kBudgetExhausted:
        ReplaceUnsealedResult(
            item, Failure(PlanarGpuFailureCode::kResourceExhausted,
                          "GPU candidate query did not converge within its round budget",
                          std::nullopt, kernel.telemetry));
        break;
      case KernelCompletion::kCancelled:
        ReplaceUnsealedResult(
            item, Failure(PlanarGpuFailureCode::kCancelled,
                          "GPU candidate query was cancelled at a bounded batch boundary",
                          std::nullopt, kernel.telemetry));
        break;
    }
  }
  return output;
}

PlanarCandidateBatchResult RouteCandidateBatchWithPreparedPlanarGpuBackend(
    const board_ir::BoardSnapshot& board, const CompiledBoard& compiled_board,
    std::span<const PlanarCandidateBatchQuery> queries, const PlanarCandidateBatchPolicy& policy,
    PreparedPlanarCompiledView& prepared) {
  try {
    CandidateBatchHostPreflightResult preflight_result =
        PreflightCandidateBatch(board, compiled_board, queries, policy);
    if (std::holds_alternative<PlanarGpuFailure>(preflight_result)) {
      return std::get<PlanarGpuFailure>(std::move(preflight_result));
    }
    PlanarCandidateBatchResult result = RouteCandidateBatchWithPreparedPreflight(
        board, compiled_board, policy, prepared,
        std::get<CandidateBatchHostPreflight>(std::move(preflight_result)));
    if (PlanarCandidateBatch* batch = std::get_if<PlanarCandidateBatch>(&result);
        batch != nullptr) {
      for (PlanarCandidateBatchItem& item : batch->items) {
        item.SealValidatedRoute(batch->schema_version, batch->batch_id,
                                prepared.has_authenticated_cuda_producer());
      }
    }
    return result;
  } catch (const std::bad_alloc&) {
    return Failure(PlanarGpuFailureCode::kResourceExhausted,
                   "GPU candidate batch host allocation failed during prepared execution");
  } catch (const std::length_error&) {
    return Failure(PlanarGpuFailureCode::kResourceExhausted,
                   "GPU candidate batch host container capacity was exceeded during prepared "
                   "execution");
  }
}

PlanarCandidateBatchResult RouteCandidateBatchWithPlanarGpuBackend(
    const board_ir::BoardSnapshot& board, const CompiledBoard& compiled_board,
    std::span<const PlanarCandidateBatchQuery> queries, const PlanarCandidateBatchPolicy& policy,
    IPlanarRouteBackend& backend) {
  try {
    CandidateBatchHostPreflightResult preflight_result =
        PreflightCandidateBatch(board, compiled_board, queries, policy);
    if (std::holds_alternative<PlanarGpuFailure>(preflight_result)) {
      return std::get<PlanarGpuFailure>(std::move(preflight_result));
    }
    CandidateBatchHostPreflight preflight =
        std::get<CandidateBatchHostPreflight>(std::move(preflight_result));
    if (policy.cancellation != nullptr && policy.cancellation->load()) {
      return BuildUnexecutedBatch(std::move(preflight), policy,
                                  Failure(PlanarGpuFailureCode::kCancelled,
                                          "GPU candidate query was cancelled before device upload"),
                                  "cancelled-before-upload");
    }
    if (preflight.admitted.empty()) {
      return BuildUnexecutedBatch(std::move(preflight), policy,
                                  Failure(PlanarGpuFailureCode::kInternalInvariant,
                                          "GPU candidate query unexpectedly remained admitted"),
                                  "no-admitted-queries");
    }
    PreparedPlanarCompiledViewResult prepared_result =
        PreparePlanarCompiledView(board, compiled_board, backend);
    if (std::holds_alternative<PlanarGpuFailure>(prepared_result)) {
      const PlanarGpuFailure preparation_failure =
          std::get<PlanarGpuFailure>(std::move(prepared_result));
      return BuildUnexecutedBatch(std::move(preflight), policy, preparation_failure,
                                  "preparation-failed");
    }
    std::unique_ptr<PreparedPlanarCompiledView> prepared =
        std::get<std::unique_ptr<PreparedPlanarCompiledView>>(std::move(prepared_result));
    if (prepared == nullptr) {
      return BuildUnexecutedBatch(
          std::move(preflight), policy,
          Failure(PlanarGpuFailureCode::kInternalInvariant,
                  "GPU candidate batch preparation returned a null device view"),
          "preparation-returned-null");
    }
    PlanarCandidateBatchResult result = RouteCandidateBatchWithPreparedPreflight(
        board, compiled_board, policy, *prepared, std::move(preflight));
    if (PlanarCandidateBatch* batch = std::get_if<PlanarCandidateBatch>(&result);
        batch != nullptr) {
      for (PlanarCandidateBatchItem& item : batch->items) {
        item.SealValidatedRoute(batch->schema_version, batch->batch_id,
                                prepared->has_authenticated_cuda_producer());
      }
    }
    return result;
  } catch (const std::bad_alloc&) {
    return Failure(PlanarGpuFailureCode::kResourceExhausted,
                   "GPU candidate batch host allocation failed before completion");
  } catch (const std::length_error&) {
    return Failure(PlanarGpuFailureCode::kResourceExhausted,
                   "GPU candidate batch host container capacity was exceeded before completion");
  }
}

}  // namespace apgar::gpu
