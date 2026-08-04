#include "apgar/allocator/cpu_sequential_negotiated_routing.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <new>
#include <optional>
#include <ranges>
#include <set>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

#include "apgar/board_ir/stable_hash.h"
#include "src/allocator/cpu_sequential_negotiated_routing_internal.h"

namespace apgar::allocator {
namespace {

using Wide = __int128_t;
using UWide = __uint128_t;

[[nodiscard]] auto NetKey(board_ir::EntityRef net) noexcept {
  return std::pair{net.id, net.generation};
}

[[nodiscard]] CpuSequentialNegotiatedRoutingError Error(
    CpuSequentialNegotiatedRoutingErrorCode code, std::string_view invariant_id,
    std::string_view detail, std::optional<board_ir::EntityRef> net = std::nullopt) noexcept {
  return CpuSequentialNegotiatedRoutingError{
      .code = code,
      .invariant_id = invariant_id,
      .detail = detail,
      .net = net,
      .pass_index = std::nullopt,
      .query_identity = std::nullopt,
      .expected_value = std::nullopt,
      .actual_value = std::nullopt,
      .policy_error_code = std::nullopt,
      .route_failure_code = std::nullopt,
      .candidate_rejection_code = std::nullopt,
      .accounting_error_code = std::nullopt,
  };
}

[[nodiscard]] bool CheckedAdd(std::uint64_t term, std::uint64_t* total) noexcept {
  if (term > std::numeric_limits<std::uint64_t>::max() - *total) {
    return false;
  }
  *total += term;
  return true;
}

[[nodiscard]] std::optional<std::uint64_t> CheckedMultiply(std::uint64_t left,
                                                           std::uint64_t right) noexcept {
  const UWide product = static_cast<UWide>(left) * right;
  if (product > std::numeric_limits<std::uint64_t>::max()) {
    return std::nullopt;
  }
  return static_cast<std::uint64_t>(product);
}

[[nodiscard]] bool ConfigurationIsValid(
    const CpuSequentialNegotiatedRoutingConfig& config) noexcept {
  const auto& policy = config.policy;
  const auto& limits = config.limits;
  return policy.maximum_passes > 0 && policy.maximum_passes <= kMaximumCpuSequentialPasses &&
         policy.present_cost_increment > 0 && policy.historical_cost_increment > 0 &&
         limits.maximum_nets > 0 && limits.maximum_nets <= kMaximumCpuSequentialNets &&
         limits.maximum_total_attempts > 0 &&
         limits.maximum_total_attempts <= kMaximumCpuSequentialAttempts &&
         limits.maximum_cpu_work_units_per_query > 0 &&
         limits.maximum_aggregate_cpu_work_units > 0 &&
         limits.maximum_candidate_bytes_per_attempt > 0 &&
         limits.maximum_candidate_bytes_per_attempt <= kMaximumCpuSequentialCandidateBytes &&
         limits.maximum_aggregate_generated_candidate_bytes > 0 &&
         limits.maximum_aggregate_generated_candidate_bytes <=
             kMaximumCpuSequentialGeneratedBytes &&
         limits.maximum_retained_candidate_bytes > 0 &&
         limits.maximum_retained_candidate_bytes <= kMaximumCpuSequentialRetainedBytes &&
         limits.maximum_expanded_resource_uses_per_candidate > 0 &&
         limits.maximum_expanded_resource_uses_per_candidate <=
             candidates::kMaximumCandidateExpandedResourceEdges &&
         limits.maximum_aggregate_expanded_resource_uses > 0 &&
         limits.maximum_aggregate_expanded_resource_uses <= kMaximumCpuSequentialExpandedUses &&
         limits.maximum_retained_diagnostics <= kMaximumCpuSequentialDiagnostics;
}

[[nodiscard]] bool SameResourceLattice(const geometry_compiler::CompiledBoard& compiled_board,
                                       const ResourceCapacityModel& capacities) noexcept {
  const ResourceLatticeAssociations& associations = capacities.associations();
  return compiled_board.source_board_content_hash() == associations.board_content_hash &&
         compiled_board.compiler_profile_fingerprint() ==
             associations.compiler_profile_fingerprint &&
         compiled_board.compiler_version() == associations.geometry_compiler_version;
}

[[nodiscard]] std::uint64_t NonzeroHash(board_ir::StableHashBuilder& hash) noexcept {
  const std::uint64_t value = hash.Finish();
  return value == 0 ? 1 : value;
}

struct NetPlan {
  const CpuSequentialNetRequest* submitted = nullptr;
  routing::NormalizedCandidateGenerationPolicy base_policy;
};

[[nodiscard]] std::uint64_t RunIdentity(const board_ir::BoardSnapshot& board,
                                        const ResourceCapacityModel& capacities,
                                        const CpuSequentialNegotiatedRoutingConfig& config,
                                        std::span<const NetPlan* const> plans) noexcept {
  board_ir::StableHashBuilder hash;
  hash.AddString("APGAR-P4R05-CANONICAL-SEQUENTIAL-RIP-UP-REROUTE-V1");
  hash.AddU64(board.content_hash());
  hash.AddU64(capacities.associations().compiler_profile_fingerprint);
  hash.AddU32(capacities.associations().geometry_compiler_version);
  hash.AddU32(capacities.capacity_units());
  hash.AddU32(config.policy.maximum_passes);
  hash.AddU64(config.policy.initial_present_cost);
  hash.AddU64(config.policy.present_cost_increment);
  hash.AddU64(config.policy.historical_cost_increment);
  hash.AddU64(config.limits.maximum_nets);
  hash.AddU64(config.limits.maximum_total_attempts);
  hash.AddU64(config.limits.maximum_cpu_work_units_per_query);
  hash.AddU64(config.limits.maximum_aggregate_cpu_work_units);
  hash.AddU64(config.limits.maximum_candidate_bytes_per_attempt);
  hash.AddU64(config.limits.maximum_aggregate_generated_candidate_bytes);
  hash.AddU64(config.limits.maximum_retained_candidate_bytes);
  hash.AddU64(config.limits.maximum_expanded_resource_uses_per_candidate);
  hash.AddU64(config.limits.maximum_aggregate_expanded_resource_uses);
  hash.AddU64(config.limits.maximum_retained_diagnostics);
  hash.AddU64(config.limits.maximum_congestion_cost_value);
  hash.AddU64(static_cast<std::uint64_t>(plans.size()));
  for (const NetPlan* plan : plans) {
    const routing::PlanarRouteRequest& request = plan->submitted->request;
    hash.AddU64(request.net.id);
    hash.AddU32(request.net.generation);
    hash.AddI64(request.start.x);
    hash.AddI64(request.start.y);
    hash.AddI64(request.goal.x);
    hash.AddI64(request.goal.y);
    hash.AddU32(request.start_layer);
    hash.AddU32(request.goal_layer);
    hash.AddU64(plan->submitted->compiled_board->compiler_profile_fingerprint());
    hash.AddU32(plan->submitted->compiled_board->compiler_version());
    hash.AddU64(routing::FingerprintRoutingProfile(
        plan->submitted->compiled_board->prepared_routing_profile().profile()));
    hash.AddU64(plan->base_policy.identity);
  }
  return NonzeroHash(hash);
}

[[nodiscard]] std::uint64_t QueryIdentity(std::uint64_t run_identity, board_ir::EntityRef net,
                                          std::uint32_t pass_index) noexcept {
  board_ir::StableHashBuilder hash;
  hash.AddString("APGAR-P4R05-CANONICAL-NET-PASS-QUERY-V1");
  hash.AddU64(run_identity);
  hash.AddU64(net.id);
  hash.AddU32(net.generation);
  hash.AddU32(pass_index);
  return NonzeroHash(hash);
}

struct AtomicResourceUse {
  routing::EdgeResourceKey resource;
  std::uint64_t usage_units = 0;
};

struct RetainedCandidateState {
  candidates::RouteCandidate candidate;
  std::vector<AtomicResourceUse> uses;
  std::uint32_t installed_pass_index = 0;
  std::uint64_t installed_query_identity = 0;
};

struct AbsenceState {
  CpuSequentialAbsenceReason reason = CpuSequentialAbsenceReason::kDisconnected;
  std::uint32_t pass_index = 0;
  std::uint64_t query_identity = 0;
};

struct NetState {
  NetPlan plan;
  std::optional<RetainedCandidateState> incumbent;
  std::optional<AbsenceState> absence;
};

[[nodiscard]] bool AddUses(const std::vector<AtomicResourceUse>& uses,
                           std::map<routing::EdgeResourceKey, std::uint64_t>* occupancy) {
  for (const AtomicResourceUse& use : uses) {
    std::uint64_t& total = (*occupancy)[use.resource];
    if (!CheckedAdd(use.usage_units, &total)) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool RemoveUses(const std::vector<AtomicResourceUse>& uses,
                              std::map<routing::EdgeResourceKey, std::uint64_t>* occupancy) {
  for (const AtomicResourceUse& use : uses) {
    const auto found = occupancy->find(use.resource);
    if (found == occupancy->end() || found->second < use.usage_units) {
      return false;
    }
    found->second -= use.usage_units;
    if (found->second == 0) {
      occupancy->erase(found);
    }
  }
  return true;
}

using ExpansionResult =
    std::variant<std::vector<AtomicResourceUse>, CpuSequentialNegotiatedRoutingError>;

[[nodiscard]] ExpansionResult ExpandCandidate(const candidates::RouteCandidate& candidate,
                                              const CpuSequentialNegotiatedRoutingLimits& limits,
                                              std::uint64_t* aggregate_expanded_uses,
                                              board_ir::EntityRef net) {
  std::uint64_t candidate_expanded_uses = 0;
  for (const candidates::PhysicalEdgeSpan& span : candidate.data().resources) {
    if (!CheckedAdd(span.edge_count, &candidate_expanded_uses)) {
      return Error(CpuSequentialNegotiatedRoutingErrorCode::kArithmeticOverflow,
                   "allocator.cpu_sequential.candidate_expansion_overflow.v1",
                   "Candidate expanded resource-use count overflowed uint64", net);
    }
  }
  if (candidate_expanded_uses > limits.maximum_expanded_resource_uses_per_candidate) {
    CpuSequentialNegotiatedRoutingError error =
        Error(CpuSequentialNegotiatedRoutingErrorCode::kBoundExhausted,
              "allocator.cpu_sequential.candidate_expansion_bound.v1",
              "Admitted candidate exceeds the configured expanded-resource bound", net);
    error.expected_value = limits.maximum_expanded_resource_uses_per_candidate;
    error.actual_value = candidate_expanded_uses;
    return error;
  }
  if (candidate_expanded_uses >
      limits.maximum_aggregate_expanded_resource_uses - *aggregate_expanded_uses) {
    CpuSequentialNegotiatedRoutingError error =
        Error(CpuSequentialNegotiatedRoutingErrorCode::kBoundExhausted,
              "allocator.cpu_sequential.aggregate_expansion_bound.v1",
              "Admitted candidates exceed the aggregate expanded-resource bound", net);
    error.expected_value = limits.maximum_aggregate_expanded_resource_uses;
    error.actual_value = *aggregate_expanded_uses + candidate_expanded_uses;
    return error;
  }

  std::vector<AtomicResourceUse> uses;
  uses.reserve(static_cast<std::size_t>(candidate_expanded_uses));
  for (const candidates::PhysicalEdgeSpan& span : candidate.data().resources) {
    const geometry_compiler::DirectionDelta delta =
        candidates::ResourceSpanStorageDelta(span.direction);
    for (std::uint32_t offset = 0; offset < span.edge_count; ++offset) {
      const Wide x = static_cast<Wide>(span.lattice_x) + static_cast<Wide>(delta.x) * offset;
      const Wide y = static_cast<Wide>(span.lattice_y) + static_cast<Wide>(delta.y) * offset;
      if (x < std::numeric_limits<std::int64_t>::min() ||
          x > std::numeric_limits<std::int64_t>::max() ||
          y < std::numeric_limits<std::int64_t>::min() ||
          y > std::numeric_limits<std::int64_t>::max() || span.usage_units == 0) {
        return Error(CpuSequentialNegotiatedRoutingErrorCode::kInternalInvariant,
                     "allocator.cpu_sequential.candidate_span.v1",
                     "An admitted candidate contains a noncanonical resource span", net);
      }
      AtomicResourceUse use{
          .resource =
              routing::EdgeResourceKey{
                  .layer = span.layer,
                  .lattice_x = static_cast<std::int64_t>(x),
                  .lattice_y = static_cast<std::int64_t>(y),
                  .direction = span.direction,
              },
          .usage_units = span.usage_units,
      };
      if (!uses.empty() && !(uses.back().resource < use.resource)) {
        return Error(CpuSequentialNegotiatedRoutingErrorCode::kInternalInvariant,
                     "allocator.cpu_sequential.candidate_resource_order.v1",
                     "An admitted candidate footprint is not strictly canonical", net);
      }
      uses.push_back(use);
    }
  }
  *aggregate_expanded_uses += candidate_expanded_uses;
  return uses;
}

struct QueryCostSnapshot {
  std::uint64_t present_factor = 0;
  std::vector<routing::ResourcePenalty> congestion_costs;
  std::uint64_t work_units = 0;
};

using SnapshotResult = std::variant<QueryCostSnapshot, CpuSequentialNegotiatedRoutingError>;

[[nodiscard]] SnapshotResult BuildQueryCostSnapshot(
    const NetState& state, std::uint32_t pass_index, std::uint64_t query_identity,
    const ResourceCapacityModel& capacities, const CpuSequentialNegotiatedRoutingConfig& config,
    const std::map<routing::EdgeResourceKey, std::uint64_t>& occupancy,
    const std::map<routing::EdgeResourceKey, std::uint64_t>& historical_costs,
    std::uint64_t maximum_work_units) {
  const std::optional<std::uint64_t> present_increment =
      CheckedMultiply(pass_index, config.policy.present_cost_increment);
  if (!present_increment.has_value()) {
    CpuSequentialNegotiatedRoutingError error =
        Error(CpuSequentialNegotiatedRoutingErrorCode::kArithmeticOverflow,
              "allocator.cpu_sequential.present_factor_overflow.v1",
              "Present congestion factor multiplication overflowed uint64",
              state.plan.submitted->request.net);
    error.pass_index = pass_index;
    error.query_identity = query_identity;
    return error;
  }
  std::uint64_t present_factor = config.policy.initial_present_cost;
  if (!CheckedAdd(*present_increment, &present_factor)) {
    CpuSequentialNegotiatedRoutingError error = Error(
        CpuSequentialNegotiatedRoutingErrorCode::kArithmeticOverflow,
        "allocator.cpu_sequential.present_factor_overflow.v1",
        "Present congestion factor addition overflowed uint64", state.plan.submitted->request.net);
    error.pass_index = pass_index;
    error.query_identity = query_identity;
    return error;
  }

  const std::size_t base_resource_entries = state.plan.base_policy.policy.banned_resources.size() +
                                            state.plan.base_policy.policy.resource_penalties.size();
  if (base_resource_entries > routing::kMaximumPolicyResourceEntries) {
    return Error(CpuSequentialNegotiatedRoutingErrorCode::kInternalInvariant,
                 "allocator.cpu_sequential.base_policy_shape.v1",
                 "A preflight-normalized base policy exceeds its resource-entry bound",
                 state.plan.submitted->request.net);
  }
  const std::size_t maximum_snapshot_entries =
      routing::kMaximumPolicyResourceEntries - base_resource_entries;
  std::set<routing::EdgeResourceKey> resources;
  std::size_t added_snapshot_entries = 0;
  std::uint64_t snapshot_work_units = 0;
  const auto consume_snapshot_work = [&]() {
    if (snapshot_work_units == maximum_work_units) {
      return false;
    }
    ++snapshot_work_units;
    return true;
  };
  const auto snapshot_work_error = [&]() {
    CpuSequentialNegotiatedRoutingError error =
        Error(CpuSequentialNegotiatedRoutingErrorCode::kBoundExhausted,
              "allocator.cpu_sequential.snapshot_work_bound.v1",
              "Congestion-snapshot construction exhausted the per-query CPU work bound",
              state.plan.submitted->request.net);
    error.pass_index = pass_index;
    error.query_identity = query_identity;
    error.expected_value = maximum_work_units;
    if (maximum_work_units != std::numeric_limits<std::uint64_t>::max()) {
      error.actual_value = maximum_work_units + 1U;
    }
    return error;
  };
  const auto add_snapshot_resource = [&](const routing::EdgeResourceKey& resource) {
    if (std::ranges::binary_search(state.plan.base_policy.policy.banned_resources, resource) ||
        resources.contains(resource)) {
      return true;
    }
    const bool merges_with_base_penalty =
        std::ranges::binary_search(state.plan.base_policy.policy.resource_penalties, resource, {},
                                   &routing::ResourcePenalty::resource);
    if (!merges_with_base_penalty) {
      if (added_snapshot_entries >= maximum_snapshot_entries) {
        return false;
      }
      ++added_snapshot_entries;
    }
    resources.insert(resource);
    return true;
  };
  for (const auto& [resource, usage] : occupancy) {
    if (!consume_snapshot_work()) {
      return snapshot_work_error();
    }
    static_cast<void>(usage);
    if (!add_snapshot_resource(resource)) {
      CpuSequentialNegotiatedRoutingError error =
          Error(CpuSequentialNegotiatedRoutingErrorCode::kBoundExhausted,
                "allocator.cpu_sequential.snapshot_entry_bound.v1",
                "Per-query congestion snapshot exceeds the candidate-policy resource bound",
                state.plan.submitted->request.net);
      error.pass_index = pass_index;
      error.query_identity = query_identity;
      return error;
    }
  }
  for (const auto& [resource, cost] : historical_costs) {
    if (!consume_snapshot_work()) {
      return snapshot_work_error();
    }
    static_cast<void>(cost);
    if (!add_snapshot_resource(resource)) {
      CpuSequentialNegotiatedRoutingError error =
          Error(CpuSequentialNegotiatedRoutingErrorCode::kBoundExhausted,
                "allocator.cpu_sequential.snapshot_entry_bound.v1",
                "Per-query congestion snapshot exceeds the candidate-policy resource bound",
                state.plan.submitted->request.net);
      error.pass_index = pass_index;
      error.query_identity = query_identity;
      return error;
    }
  }
  if (capacities.capacity_units() == 0 && present_factor != 0) {
    constexpr std::array<geometry_compiler::Direction, 4> kCanonicalDirections = {
        geometry_compiler::Direction::kEast,
        geometry_compiler::Direction::kNorthEast,
        geometry_compiler::Direction::kNorth,
        geometry_compiler::Direction::kNorthWest,
    };
    const geometry_compiler::CompiledBoard& compiled = *state.plan.submitted->compiled_board;
    for (const geometry_compiler::SparseTile& tile : compiled.tiles()) {
      for (const geometry_compiler::CompiledNode& node : tile.nodes) {
        const geometry_compiler::LatticeIndex source =
            geometry_compiler::GlobalLatticeIndex(compiled.profile(), tile.key, node.local_index);
        for (geometry_compiler::Direction direction : kCanonicalDirections) {
          if (!consume_snapshot_work()) {
            return snapshot_work_error();
          }
          const std::optional<routing::EdgeResourceKey> resource =
              routing::CanonicalPhysicalEdgeResource(tile.key.layer, source, direction);
          if (!resource.has_value() || !routing::ResourceExists(compiled, *resource)) {
            continue;
          }
          if (!add_snapshot_resource(*resource)) {
            CpuSequentialNegotiatedRoutingError error =
                Error(CpuSequentialNegotiatedRoutingErrorCode::kBoundExhausted,
                      "allocator.cpu_sequential.snapshot_entry_bound.v1",
                      "Zero-capacity query snapshot exceeds the candidate-policy resource bound",
                      state.plan.submitted->request.net);
            error.pass_index = pass_index;
            error.query_identity = query_identity;
            return error;
          }
        }
      }
    }
  }

  QueryCostSnapshot snapshot{
      .present_factor = present_factor,
      .congestion_costs = {},
      .work_units = 0,
  };
  snapshot.congestion_costs.reserve(resources.size());
  for (const routing::EdgeResourceKey& resource : resources) {
    if (!consume_snapshot_work()) {
      return snapshot_work_error();
    }
    const auto usage_found = occupancy.find(resource);
    const std::uint64_t usage = usage_found == occupancy.end() ? 0 : usage_found->second;
    if (usage == std::numeric_limits<std::uint64_t>::max()) {
      return Error(CpuSequentialNegotiatedRoutingErrorCode::kArithmeticOverflow,
                   "allocator.cpu_sequential.predicted_usage_overflow.v1",
                   "Per-query predicted resource usage overflowed uint64",
                   state.plan.submitted->request.net);
    }
    const std::uint64_t predicted_usage = usage + 1;
    const std::uint64_t predicted_overuse = predicted_usage > capacities.capacity_units()
                                                ? predicted_usage - capacities.capacity_units()
                                                : 0;
    const std::optional<std::uint64_t> present_cost =
        CheckedMultiply(present_factor, predicted_overuse);
    if (!present_cost.has_value()) {
      return Error(CpuSequentialNegotiatedRoutingErrorCode::kArithmeticOverflow,
                   "allocator.cpu_sequential.present_cost_overflow.v1",
                   "Per-query present congestion cost overflowed uint64",
                   state.plan.submitted->request.net);
    }
    const auto history_found = historical_costs.find(resource);
    std::uint64_t cost = history_found == historical_costs.end() ? 0 : history_found->second;
    if (!CheckedAdd(*present_cost, &cost)) {
      return Error(CpuSequentialNegotiatedRoutingErrorCode::kArithmeticOverflow,
                   "allocator.cpu_sequential.snapshot_cost_overflow.v1",
                   "Per-query present and historical congestion costs overflowed uint64",
                   state.plan.submitted->request.net);
    }
    if (cost > config.limits.maximum_congestion_cost_value) {
      CpuSequentialNegotiatedRoutingError error =
          Error(CpuSequentialNegotiatedRoutingErrorCode::kBoundExhausted,
                "allocator.cpu_sequential.snapshot_cost_bound.v1",
                "Per-query congestion cost exceeds the configured value bound",
                state.plan.submitted->request.net);
      error.pass_index = pass_index;
      error.query_identity = query_identity;
      error.expected_value = config.limits.maximum_congestion_cost_value;
      error.actual_value = cost;
      return error;
    }
    if (cost != 0) {
      snapshot.congestion_costs.push_back(
          routing::ResourcePenalty{.resource = resource, .additional_cost = cost});
    }
  }
  snapshot.work_units = snapshot_work_units;
  return snapshot;
}

using QueryPolicyResult =
    std::variant<routing::NormalizedCandidateGenerationPolicy, CpuSequentialNegotiatedRoutingError>;

[[nodiscard]] QueryPolicyResult BuildQueryPolicy(const NetState& state, std::uint32_t pass_index,
                                                 std::uint64_t query_identity,
                                                 const QueryCostSnapshot& snapshot,
                                                 std::uint64_t maximum_congestion_cost_value) {
  routing::CandidateGenerationPolicy policy = state.plan.base_policy.policy;
  policy.candidate_ordinal = pass_index;
  const std::size_t base_penalty_count = policy.resource_penalties.size();
  std::size_t base_index = 0;
  for (const routing::ResourcePenalty& congestion : snapshot.congestion_costs) {
    while (base_index < base_penalty_count &&
           policy.resource_penalties[base_index].resource < congestion.resource) {
      ++base_index;
    }
    if (base_index < base_penalty_count &&
        policy.resource_penalties[base_index].resource == congestion.resource) {
      std::uint64_t& combined_cost = policy.resource_penalties[base_index].additional_cost;
      if (!CheckedAdd(congestion.additional_cost, &combined_cost)) {
        CpuSequentialNegotiatedRoutingError error =
            Error(CpuSequentialNegotiatedRoutingErrorCode::kArithmeticOverflow,
                  "allocator.cpu_sequential.combined_cost_overflow.v1",
                  "Base and negotiated per-resource cost overflowed uint64",
                  state.plan.submitted->request.net);
        error.pass_index = pass_index;
        error.query_identity = query_identity;
        return error;
      }
      if (combined_cost > maximum_congestion_cost_value) {
        CpuSequentialNegotiatedRoutingError error =
            Error(CpuSequentialNegotiatedRoutingErrorCode::kBoundExhausted,
                  "allocator.cpu_sequential.combined_cost_bound.v1",
                  "Base and negotiated per-resource cost exceeds the configured value bound",
                  state.plan.submitted->request.net);
        error.pass_index = pass_index;
        error.query_identity = query_identity;
        error.expected_value = maximum_congestion_cost_value;
        error.actual_value = combined_cost;
        return error;
      }
      continue;
    }
    policy.resource_penalties.push_back(congestion);
  }
  routing::CandidatePolicyResult normalized =
      routing::NormalizeCandidateGenerationPolicy(*state.plan.submitted->compiled_board, policy);
  if (auto* policy_error = std::get_if<routing::CandidatePolicyError>(&normalized);
      policy_error != nullptr) {
    CpuSequentialNegotiatedRoutingErrorCode code =
        CpuSequentialNegotiatedRoutingErrorCode::kInternalInvariant;
    if (policy_error->code == routing::CandidatePolicyErrorCode::kCostOverflow) {
      code = CpuSequentialNegotiatedRoutingErrorCode::kArithmeticOverflow;
    } else if (policy_error->code == routing::CandidatePolicyErrorCode::kTooManyResources) {
      code = CpuSequentialNegotiatedRoutingErrorCode::kBoundExhausted;
    }
    CpuSequentialNegotiatedRoutingError error =
        Error(code, "allocator.cpu_sequential.query_policy.v1",
              "Canonical congestion snapshot could not form a valid CPU A* query policy",
              state.plan.submitted->request.net);
    error.pass_index = pass_index;
    error.query_identity = query_identity;
    error.policy_error_code = policy_error->code;
    return error;
  }
  return std::get<routing::NormalizedCandidateGenerationPolicy>(std::move(normalized));
}

[[nodiscard]] CpuSequentialAbsenceReason AbsenceFor(CpuSequentialAttemptOutcome outcome) noexcept {
  switch (outcome) {
    case CpuSequentialAttemptOutcome::kDisconnected:
      return CpuSequentialAbsenceReason::kDisconnected;
    case CpuSequentialAttemptOutcome::kUnsupported:
      return CpuSequentialAbsenceReason::kUnsupported;
    case CpuSequentialAttemptOutcome::kCandidateBuildRejected:
      return CpuSequentialAbsenceReason::kCandidateBuildRejected;
    case CpuSequentialAttemptOutcome::kAdmissionRejected:
      return CpuSequentialAbsenceReason::kAdmissionRejected;
    case CpuSequentialAttemptOutcome::kAdmitted:
      break;
  }
  return CpuSequentialAbsenceReason::kAdmissionRejected;
}

[[nodiscard]] std::optional<CpuSequentialNegotiatedRoutingError> RetainDiagnostic(
    NetState* state, std::uint32_t pass_index, std::uint64_t query_identity,
    std::uint64_t policy_identity, CpuSequentialAttemptOutcome outcome,
    std::optional<routing::RouteFailureCode> route_failure_code,
    std::optional<candidates::CandidateRejectionCode> rejection_code,
    const CpuSequentialNegotiatedRoutingLimits& limits,
    std::vector<CpuSequentialAttemptDiagnostic>* diagnostics) {
  const bool incumbent_retained = state->incumbent.has_value();
  if (diagnostics->size() >= limits.maximum_retained_diagnostics) {
    CpuSequentialNegotiatedRoutingError error =
        Error(CpuSequentialNegotiatedRoutingErrorCode::kBoundExhausted,
              "allocator.cpu_sequential.diagnostic_bound.v1",
              "Ordinary route outcomes exceed the retained-diagnostic bound",
              state->plan.submitted->request.net);
    error.pass_index = pass_index;
    error.query_identity = query_identity;
    error.expected_value = limits.maximum_retained_diagnostics;
    error.actual_value = diagnostics->size() + 1;
    return error;
  }
  diagnostics->push_back(CpuSequentialAttemptDiagnostic{
      .net = state->plan.submitted->request.net,
      .pass_index = pass_index,
      .query_identity = query_identity,
      .policy_identity = policy_identity,
      .outcome = outcome,
      .incumbent_retained = incumbent_retained,
      .route_failure_code = route_failure_code,
      .candidate_rejection_code = rejection_code,
  });
  if (!incumbent_retained) {
    state->absence = AbsenceState{
        .reason = AbsenceFor(outcome),
        .pass_index = pass_index,
        .query_identity = query_identity,
    };
  }
  return std::nullopt;
}

struct CanonicalStateKey {
  board_ir::EntityRef net;
  bool has_route = false;
  candidates::CandidateSignature geometry_signature;
  candidates::CandidateSignature resource_signature;
  CpuSequentialAbsenceReason absence_reason = CpuSequentialAbsenceReason::kDisconnected;

  friend bool operator==(const CanonicalStateKey&, const CanonicalStateKey&) = default;
};

[[nodiscard]] std::vector<CanonicalStateKey> StateKey(std::span<const NetState> states) {
  std::vector<CanonicalStateKey> key;
  key.reserve(states.size());
  for (const NetState& state : states) {
    if (state.incumbent.has_value()) {
      key.push_back(CanonicalStateKey{
          .net = state.plan.submitted->request.net,
          .has_route = true,
          .geometry_signature = state.incumbent->candidate.data().geometry_signature,
          .resource_signature = state.incumbent->candidate.data().resource_signature,
          .absence_reason = CpuSequentialAbsenceReason::kDisconnected,
      });
    } else {
      key.push_back(CanonicalStateKey{
          .net = state.plan.submitted->request.net,
          .has_route = false,
          .geometry_signature = {},
          .resource_signature = {},
          .absence_reason = state.absence->reason,
      });
    }
  }
  return key;
}

[[nodiscard]] CpuSequentialNegotiatedRoutingError AccountingError(
    const ResourceAccountingError& accounting_error) noexcept {
  CpuSequentialNegotiatedRoutingErrorCode code =
      CpuSequentialNegotiatedRoutingErrorCode::kInternalInvariant;
  switch (accounting_error.code) {
    case ResourceAccountingErrorCode::kInvalidLimits:
    case ResourceAccountingErrorCode::kInputBoundExceeded:
      code = CpuSequentialNegotiatedRoutingErrorCode::kBoundExhausted;
      break;
    case ResourceAccountingErrorCode::kUsageOverflow:
      code = CpuSequentialNegotiatedRoutingErrorCode::kArithmeticOverflow;
      break;
    case ResourceAccountingErrorCode::kResourceExhausted:
      code = CpuSequentialNegotiatedRoutingErrorCode::kResourceExhausted;
      break;
    case ResourceAccountingErrorCode::kInvalidCapacity:
    case ResourceAccountingErrorCode::kAssociationMismatch:
    case ResourceAccountingErrorCode::kNullCandidate:
    case ResourceAccountingErrorCode::kDuplicateCandidate:
    case ResourceAccountingErrorCode::kCandidateAssociationMismatch:
    case ResourceAccountingErrorCode::kCandidateInvariant:
      break;
  }
  CpuSequentialNegotiatedRoutingError error =
      Error(code, accounting_error.invariant_id, accounting_error.detail);
  error.accounting_error_code = accounting_error.code;
  return error;
}

[[nodiscard]] std::variant<std::vector<NetState>, CpuSequentialNegotiatedRoutingError> Preflight(
    const board_ir::BoardSnapshot& board, const ResourceCapacityModel& capacities,
    std::span<const CpuSequentialNetRequest> submitted_nets,
    const CpuSequentialNegotiatedRoutingConfig& config) {
  if (!ConfigurationIsValid(config)) {
    return Error(CpuSequentialNegotiatedRoutingErrorCode::kInvalidConfiguration,
                 "allocator.cpu_sequential.configuration.v1",
                 "CS-RR-v1 configuration is outside its explicit hard bounds");
  }
  if (board.content_hash() != capacities.associations().board_content_hash) {
    return Error(CpuSequentialNegotiatedRoutingErrorCode::kAssociationMismatch,
                 "allocator.cpu_sequential.capacity_board_association.v1",
                 "Board IR does not match the resource-capacity model");
  }
  if (submitted_nets.empty()) {
    return Error(CpuSequentialNegotiatedRoutingErrorCode::kInvalidInput,
                 "allocator.cpu_sequential.empty_request.v1",
                 "Sequential negotiated routing requires at least one requested net");
  }
  if (submitted_nets.size() > config.limits.maximum_nets) {
    CpuSequentialNegotiatedRoutingError error =
        Error(CpuSequentialNegotiatedRoutingErrorCode::kBoundExhausted,
              "allocator.cpu_sequential.net_count_bound.v1",
              "Requested net count exceeds the configured bound");
    error.expected_value = config.limits.maximum_nets;
    error.actual_value = submitted_nets.size();
    return error;
  }

  std::vector<const CpuSequentialNetRequest*> ordered;
  ordered.reserve(submitted_nets.size());
  for (const CpuSequentialNetRequest& net : submitted_nets) {
    ordered.push_back(&net);
  }
  std::ranges::sort(ordered,
                    [](const CpuSequentialNetRequest* left, const CpuSequentialNetRequest* right) {
                      return NetKey(left->request.net) < NetKey(right->request.net);
                    });

  std::vector<NetState> states;
  states.reserve(ordered.size());
  const board_ir::EntityRef* previous_net = nullptr;
  for (const CpuSequentialNetRequest* submitted : ordered) {
    const board_ir::EntityRef net = submitted->request.net;
    if (previous_net != nullptr && *previous_net == net) {
      return Error(CpuSequentialNegotiatedRoutingErrorCode::kInvalidInput,
                   "allocator.cpu_sequential.duplicate_net.v1",
                   "Each requested net must appear exactly once", net);
    }
    previous_net = &submitted->request.net;
    if (board.FindNet(net) == nullptr) {
      return Error(CpuSequentialNegotiatedRoutingErrorCode::kInvalidInput,
                   "allocator.cpu_sequential.unknown_net.v1",
                   "A requested net is absent from the Board IR snapshot", net);
    }
    if (submitted->compiled_board == nullptr) {
      return Error(CpuSequentialNegotiatedRoutingErrorCode::kInvalidInput,
                   "allocator.cpu_sequential.null_compiled_board.v1",
                   "A requested net has no retained prepared CompiledBoard", net);
    }
    if (routing::ValidatePreparedCompiledBoardAssociation(board, *submitted->compiled_board)
            .has_value() ||
        !SameResourceLattice(*submitted->compiled_board, capacities)) {
      return Error(CpuSequentialNegotiatedRoutingErrorCode::kAssociationMismatch,
                   "allocator.cpu_sequential.compiled_association.v1",
                   "A retained prepared CompiledBoard does not match Board IR or capacity lattice",
                   net);
    }
    if (routing::ValidateTwoTerminalRouteRequest(board, *submitted->compiled_board,
                                                 submitted->request)
            .has_value()) {
      return Error(CpuSequentialNegotiatedRoutingErrorCode::kInvalidInput,
                   "allocator.cpu_sequential.route_request.v1",
                   "A requested net has an invalid prepared two-terminal route request", net);
    }
    if (!routing::CandidateGenerationPolicyShapeIsWithinV1Bounds(
            submitted->request.candidate_policy)) {
      const std::uint64_t banned_count =
          static_cast<std::uint64_t>(submitted->request.candidate_policy.banned_resources.size());
      const std::uint64_t penalty_count =
          static_cast<std::uint64_t>(submitted->request.candidate_policy.resource_penalties.size());
      CpuSequentialNegotiatedRoutingError error = Error(
          CpuSequentialNegotiatedRoutingErrorCode::kBoundExhausted,
          "allocator.cpu_sequential.base_policy_shape_bound.v1",
          "A requested net's baseline policy exceeds the schema-v1 resource-entry bound", net);
      error.expected_value = routing::kMaximumPolicyResourceEntries;
      error.actual_value = banned_count > routing::kMaximumPolicyResourceEntries ? banned_count
                           : penalty_count > routing::kMaximumPolicyResourceEntries
                               ? penalty_count
                               : banned_count + penalty_count;
      error.policy_error_code = routing::CandidatePolicyErrorCode::kTooManyResources;
      return error;
    }
    routing::CandidateGenerationPolicy base = submitted->request.candidate_policy;
    base.candidate_ordinal = 0;
    routing::CandidatePolicyResult normalized =
        routing::NormalizeCandidateGenerationPolicy(*submitted->compiled_board, base);
    if (auto* policy_error = std::get_if<routing::CandidatePolicyError>(&normalized);
        policy_error != nullptr) {
      CpuSequentialNegotiatedRoutingError error =
          Error(CpuSequentialNegotiatedRoutingErrorCode::kInvalidConfiguration,
                "allocator.cpu_sequential.base_policy.v1",
                "A requested net has an invalid baseline CPU A* policy", net);
      error.policy_error_code = policy_error->code;
      return error;
    }
    states.push_back(NetState{
        .plan =
            NetPlan{
                .submitted = submitted,
                .base_policy =
                    std::get<routing::NormalizedCandidateGenerationPolicy>(std::move(normalized)),
            },
        .incumbent = std::nullopt,
        .absence = std::nullopt,
    });
  }

  const std::uint64_t net_count = static_cast<std::uint64_t>(states.size());
  const std::optional<std::uint64_t> maximum_attempts =
      CheckedMultiply(net_count, config.policy.maximum_passes);
  if (!maximum_attempts.has_value()) {
    return Error(CpuSequentialNegotiatedRoutingErrorCode::kArithmeticOverflow,
                 "allocator.cpu_sequential.attempt_count_overflow.v1",
                 "Net-count and pass-count multiplication overflowed uint64");
  }
  if (*maximum_attempts > config.limits.maximum_total_attempts) {
    CpuSequentialNegotiatedRoutingError error =
        Error(CpuSequentialNegotiatedRoutingErrorCode::kBoundExhausted,
              "allocator.cpu_sequential.attempt_count_bound.v1",
              "Configured net/pass envelope exceeds the total route-attempt bound");
    error.expected_value = config.limits.maximum_total_attempts;
    error.actual_value = *maximum_attempts;
    return error;
  }

  struct ProductBound {
    std::uint64_t per_item;
    std::uint64_t aggregate;
    std::string_view overflow_id;
    std::string_view bound_id;
    std::string_view detail;
  };
  const std::array<ProductBound, 3> product_bounds = {
      ProductBound{
          .per_item = config.limits.maximum_cpu_work_units_per_query,
          .aggregate = config.limits.maximum_aggregate_cpu_work_units,
          .overflow_id = "allocator.cpu_sequential.aggregate_work_overflow.v1",
          .bound_id = "allocator.cpu_sequential.aggregate_work_bound.v1",
          .detail = "Configured attempts exceed the aggregate CPU work bound",
      },
      ProductBound{
          .per_item = config.limits.maximum_candidate_bytes_per_attempt,
          .aggregate = config.limits.maximum_aggregate_generated_candidate_bytes,
          .overflow_id = "allocator.cpu_sequential.generated_bytes_overflow.v1",
          .bound_id = "allocator.cpu_sequential.generated_bytes_bound.v1",
          .detail = "Configured attempts exceed the aggregate generated-candidate byte bound",
      },
      ProductBound{
          .per_item = config.limits.maximum_expanded_resource_uses_per_candidate,
          .aggregate = config.limits.maximum_aggregate_expanded_resource_uses,
          .overflow_id = "allocator.cpu_sequential.expanded_uses_overflow.v1",
          .bound_id = "allocator.cpu_sequential.expanded_uses_bound.v1",
          .detail = "Configured attempts exceed the aggregate expanded-resource bound",
      },
  };
  for (const ProductBound& bound : product_bounds) {
    const std::optional<std::uint64_t> product = CheckedMultiply(*maximum_attempts, bound.per_item);
    if (!product.has_value()) {
      return Error(CpuSequentialNegotiatedRoutingErrorCode::kArithmeticOverflow, bound.overflow_id,
                   "Configured aggregate multiplication overflowed uint64");
    }
    if (*product > bound.aggregate) {
      CpuSequentialNegotiatedRoutingError error = Error(
          CpuSequentialNegotiatedRoutingErrorCode::kBoundExhausted, bound.bound_id, bound.detail);
      error.expected_value = bound.aggregate;
      error.actual_value = *product;
      return error;
    }
  }

  const std::optional<std::uint64_t> maximum_retained_bytes =
      CheckedMultiply(net_count, config.limits.maximum_candidate_bytes_per_attempt);
  if (!maximum_retained_bytes.has_value()) {
    return Error(CpuSequentialNegotiatedRoutingErrorCode::kArithmeticOverflow,
                 "allocator.cpu_sequential.retained_bytes_overflow.v1",
                 "Net-count and retained-candidate byte multiplication overflowed uint64");
  }
  if (*maximum_retained_bytes > config.limits.maximum_retained_candidate_bytes) {
    CpuSequentialNegotiatedRoutingError error =
        Error(CpuSequentialNegotiatedRoutingErrorCode::kBoundExhausted,
              "allocator.cpu_sequential.retained_bytes_bound.v1",
              "Configured final-route envelope exceeds the retained-candidate byte bound");
    error.expected_value = config.limits.maximum_retained_candidate_bytes;
    error.actual_value = *maximum_retained_bytes;
    return error;
  }

  const std::uint64_t preceding_passes = config.policy.maximum_passes - 1U;
  const std::optional<std::uint64_t> present_steps =
      CheckedMultiply(preceding_passes, config.policy.present_cost_increment);
  if (!present_steps.has_value()) {
    return Error(CpuSequentialNegotiatedRoutingErrorCode::kArithmeticOverflow,
                 "allocator.cpu_sequential.cost_schedule_overflow.v1",
                 "Present congestion schedule multiplication overflowed uint64");
  }
  std::uint64_t maximum_present_factor = config.policy.initial_present_cost;
  if (!CheckedAdd(*present_steps, &maximum_present_factor)) {
    return Error(CpuSequentialNegotiatedRoutingErrorCode::kArithmeticOverflow,
                 "allocator.cpu_sequential.cost_schedule_overflow.v1",
                 "Present congestion schedule addition overflowed uint64");
  }
  const std::optional<std::uint64_t> maximum_present_cost =
      CheckedMultiply(maximum_present_factor, net_count);
  const std::optional<std::uint64_t> history_pass_units =
      CheckedMultiply(preceding_passes, net_count);
  const std::optional<std::uint64_t> maximum_historical_cost =
      history_pass_units.has_value()
          ? CheckedMultiply(*history_pass_units, config.policy.historical_cost_increment)
          : std::nullopt;
  if (!maximum_present_cost.has_value() || !history_pass_units.has_value() ||
      !maximum_historical_cost.has_value()) {
    return Error(CpuSequentialNegotiatedRoutingErrorCode::kArithmeticOverflow,
                 "allocator.cpu_sequential.cost_schedule_overflow.v1",
                 "Worst-case congestion-cost schedule overflowed uint64");
  }
  std::uint64_t maximum_base_resource_penalty = 0;
  for (const NetState& state : states) {
    for (const routing::ResourcePenalty& penalty :
         state.plan.base_policy.policy.resource_penalties) {
      maximum_base_resource_penalty =
          std::max(maximum_base_resource_penalty, penalty.additional_cost);
    }
  }
  std::uint64_t maximum_snapshot_cost = *maximum_present_cost;
  if (!CheckedAdd(*maximum_historical_cost, &maximum_snapshot_cost) ||
      !CheckedAdd(maximum_base_resource_penalty, &maximum_snapshot_cost)) {
    return Error(CpuSequentialNegotiatedRoutingErrorCode::kArithmeticOverflow,
                 "allocator.cpu_sequential.cost_schedule_overflow.v1",
                 "Worst-case base, present, and historical resource cost overflowed uint64");
  }
  if (maximum_snapshot_cost > config.limits.maximum_congestion_cost_value) {
    CpuSequentialNegotiatedRoutingError error =
        Error(CpuSequentialNegotiatedRoutingErrorCode::kBoundExhausted,
              "allocator.cpu_sequential.cost_schedule_bound.v1",
              "Worst-case congestion-cost schedule exceeds the configured value bound");
    error.expected_value = config.limits.maximum_congestion_cost_value;
    error.actual_value = maximum_snapshot_cost;
    return error;
  }
  return states;
}

[[nodiscard]] CpuSequentialNegotiatedRoutingResult RouteImpl(
    const board_ir::BoardSnapshot& board, const ResourceCapacityModel& capacities,
    std::span<const CpuSequentialNetRequest> submitted_nets,
    CpuSequentialNegotiatedRoutingConfig config,
    internal::CpuSequentialNegotiatedRoutingTestHooks hooks) {
  try {
    auto preflight = Preflight(board, capacities, submitted_nets, config);
    if (std::holds_alternative<CpuSequentialNegotiatedRoutingError>(preflight)) {
      return std::get<CpuSequentialNegotiatedRoutingError>(std::move(preflight));
    }
    std::vector<NetState> states = std::get<std::vector<NetState>>(std::move(preflight));
    std::vector<const NetPlan*> plans;
    plans.reserve(states.size());
    for (const NetState& state : states) {
      plans.push_back(&state.plan);
    }
    const std::uint64_t run_identity = RunIdentity(board, capacities, config, plans);

    std::map<routing::EdgeResourceKey, std::uint64_t> occupancy;
    std::map<routing::EdgeResourceKey, std::uint64_t> historical_costs;
    std::vector<CpuSequentialAttemptDiagnostic> diagnostics;
    diagnostics.reserve(static_cast<std::size_t>(std::min<std::uint64_t>(
        config.limits.maximum_retained_diagnostics, config.limits.maximum_total_attempts)));
    std::set<std::uint64_t> query_identities;
    std::optional<std::vector<CanonicalStateKey>> previous_state_key;
    ResourceAccounting final_accounting;
    CpuSequentialTerminationReason termination = CpuSequentialTerminationReason::kPassLimit;
    std::uint64_t route_attempts = 0;
    std::uint64_t cpu_work_units = 0;
    std::uint64_t generated_candidate_bytes = 0;
    std::uint64_t retained_candidate_bytes = 0;
    std::uint64_t expanded_resource_uses = 0;
    std::uint32_t completed_passes = 0;

    for (std::uint32_t pass_index = 0; pass_index < config.policy.maximum_passes; ++pass_index) {
      for (NetState& state : states) {
        const board_ir::EntityRef net = state.plan.submitted->request.net;
        const std::uint64_t query_identity = QueryIdentity(run_identity, net, pass_index);
        if (!query_identities.insert(query_identity).second) {
          CpuSequentialNegotiatedRoutingError error =
              Error(CpuSequentialNegotiatedRoutingErrorCode::kInternalInvariant,
                    "allocator.cpu_sequential.query_identity_collision.v1",
                    "Canonical net/pass identities produced a duplicate query identity", net);
          error.pass_index = pass_index;
          error.query_identity = query_identity;
          return error;
        }
        ++route_attempts;
        if (hooks.injected_failure != nullptr) {
          switch (hooks.injected_failure(static_cast<std::size_t>(route_attempts - 1), pass_index,
                                         net, hooks.context)) {
            case internal::CpuSequentialInjectedFailure::kNone:
              break;
            case internal::CpuSequentialInjectedFailure::kResourceExhausted:
              return Error(CpuSequentialNegotiatedRoutingErrorCode::kResourceExhausted,
                           "allocator.cpu_sequential.injected_resource_exhaustion.v1",
                           "A required host resource was exhausted", net);
            case internal::CpuSequentialInjectedFailure::kInternalInvariant:
              return Error(CpuSequentialNegotiatedRoutingErrorCode::kInternalInvariant,
                           "allocator.cpu_sequential.injected_internal_invariant.v1",
                           "An internal sequential-routing invariant failed", net);
          }
        }

        if (state.incumbent.has_value() && !RemoveUses(state.incumbent->uses, &occupancy)) {
          return Error(CpuSequentialNegotiatedRoutingErrorCode::kInternalInvariant,
                       "allocator.cpu_sequential.incumbent_removal.v1",
                       "Incumbent resource usage was absent during deterministic rip-up", net);
        }
        const auto restore_incumbent = [&state, &occupancy]() {
          return !state.incumbent.has_value() || AddUses(state.incumbent->uses, &occupancy);
        };

        SnapshotResult snapshot_result = BuildQueryCostSnapshot(
            state, pass_index, query_identity, capacities, config, occupancy, historical_costs,
            config.limits.maximum_cpu_work_units_per_query);
        if (std::holds_alternative<CpuSequentialNegotiatedRoutingError>(snapshot_result)) {
          return std::get<CpuSequentialNegotiatedRoutingError>(std::move(snapshot_result));
        }
        const QueryCostSnapshot snapshot = std::get<QueryCostSnapshot>(std::move(snapshot_result));
        if (!CheckedAdd(snapshot.work_units, &cpu_work_units)) {
          return Error(CpuSequentialNegotiatedRoutingErrorCode::kArithmeticOverflow,
                       "allocator.cpu_sequential.actual_work_overflow.v1",
                       "Actual aggregate CPU work overflowed uint64", net);
        }
        if (cpu_work_units > config.limits.maximum_aggregate_cpu_work_units) {
          return Error(CpuSequentialNegotiatedRoutingErrorCode::kBoundExhausted,
                       "allocator.cpu_sequential.actual_work_bound.v1",
                       "Actual aggregate CPU work exceeded its configured bound", net);
        }
        if (snapshot.work_units == config.limits.maximum_cpu_work_units_per_query) {
          CpuSequentialNegotiatedRoutingError error =
              Error(CpuSequentialNegotiatedRoutingErrorCode::kBoundExhausted,
                    "allocator.cpu_sequential.query_work_bound.v1",
                    "Congestion-snapshot work left no CPU A* work budget", net);
          error.pass_index = pass_index;
          error.query_identity = query_identity;
          error.expected_value = config.limits.maximum_cpu_work_units_per_query;
          return error;
        }
        QueryPolicyResult policy_result =
            BuildQueryPolicy(state, pass_index, query_identity, snapshot,
                             config.limits.maximum_congestion_cost_value);
        if (std::holds_alternative<CpuSequentialNegotiatedRoutingError>(policy_result)) {
          return std::get<CpuSequentialNegotiatedRoutingError>(std::move(policy_result));
        }
        routing::NormalizedCandidateGenerationPolicy query_policy =
            std::get<routing::NormalizedCandidateGenerationPolicy>(std::move(policy_result));
        routing::PlanarRouteRequest query_request = state.plan.submitted->request;
        query_request.candidate_policy = query_policy.policy;

        routing::CpuRouteResult route_result = routing::RouteWithCpuAStar(
            board, *state.plan.submitted->compiled_board, query_request,
            routing::CpuRouteWorkLimits{
                .maximum_work_units =
                    config.limits.maximum_cpu_work_units_per_query - snapshot.work_units,
            });
        if (auto* route = std::get_if<routing::CpuRoute>(&route_result); route != nullptr) {
          if (hooks.after_cpu_route != nullptr) {
            hooks.after_cpu_route(static_cast<std::size_t>(route_attempts - 1), pass_index, net,
                                  *route, hooks.context);
          }
          if (!CheckedAdd(route->telemetry.work_units, &cpu_work_units)) {
            return Error(CpuSequentialNegotiatedRoutingErrorCode::kArithmeticOverflow,
                         "allocator.cpu_sequential.actual_work_overflow.v1",
                         "Actual aggregate CPU work overflowed uint64", net);
          }
          if (cpu_work_units > config.limits.maximum_aggregate_cpu_work_units) {
            return Error(CpuSequentialNegotiatedRoutingErrorCode::kBoundExhausted,
                         "allocator.cpu_sequential.actual_work_bound.v1",
                         "Actual aggregate CPU work exceeded its configured bound", net);
          }

          candidates::CandidateDraftBuildResult draft =
              candidates::BuildGeneratedCandidateFromCpuRoute(
                  board, *state.plan.submitted->compiled_board, query_request, query_policy, *route,
                  candidates::CandidateSchedulingIdentity{
                      .batch_identity = run_identity,
                      .query_identity = query_identity,
                  });
          if (hooks.after_candidate_build != nullptr) {
            hooks.after_candidate_build(static_cast<std::size_t>(route_attempts - 1), pass_index,
                                        net, draft, hooks.context);
          }
          const std::uint64_t draft_bytes =
              std::holds_alternative<candidates::GeneratedRouteCandidate>(draft)
                  ? std::get<candidates::GeneratedRouteCandidate>(draft).logical_bytes
                  : std::get<candidates::CandidateRejection>(draft).logical_bytes;
          if (draft_bytes > config.limits.maximum_candidate_bytes_per_attempt) {
            return Error(CpuSequentialNegotiatedRoutingErrorCode::kBoundExhausted,
                         "allocator.cpu_sequential.candidate_bytes_bound.v1",
                         "One candidate draft exceeds the configured per-attempt byte bound", net);
          }
          if (!CheckedAdd(draft_bytes, &generated_candidate_bytes)) {
            return Error(CpuSequentialNegotiatedRoutingErrorCode::kArithmeticOverflow,
                         "allocator.cpu_sequential.generated_bytes_overflow.v1",
                         "Actual generated-candidate bytes overflowed uint64", net);
          }
          if (generated_candidate_bytes >
              config.limits.maximum_aggregate_generated_candidate_bytes) {
            return Error(CpuSequentialNegotiatedRoutingErrorCode::kBoundExhausted,
                         "allocator.cpu_sequential.actual_generated_bytes_bound.v1",
                         "Actual generated-candidate bytes exceed the aggregate bound", net);
          }

          if (auto* rejection = std::get_if<candidates::CandidateRejection>(&draft);
              rejection != nullptr) {
            if (!restore_incumbent()) {
              return Error(CpuSequentialNegotiatedRoutingErrorCode::kInternalInvariant,
                           "allocator.cpu_sequential.incumbent_restore.v1",
                           "Rejected reroute could not restore incumbent resource usage", net);
            }
            if (auto diagnostic_error =
                    RetainDiagnostic(&state, pass_index, query_identity, query_policy.identity,
                                     CpuSequentialAttemptOutcome::kCandidateBuildRejected,
                                     std::nullopt, rejection->code, config.limits, &diagnostics);
                diagnostic_error.has_value()) {
              return *diagnostic_error;
            }
            continue;
          }

          candidates::CandidateAdmissionResult admission = candidates::AdmitRouteCandidate(
              candidates::CandidateAdmissionContext{
                  .board = board,
                  .compiled_board = *state.plan.submitted->compiled_board,
                  .request = query_request,
              },
              std::get<candidates::GeneratedRouteCandidate>(std::move(draft)));
          if (auto* rejection = std::get_if<candidates::CandidateRejection>(&admission);
              rejection != nullptr) {
            if (!restore_incumbent()) {
              return Error(CpuSequentialNegotiatedRoutingErrorCode::kInternalInvariant,
                           "allocator.cpu_sequential.incumbent_restore.v1",
                           "Rejected reroute could not restore incumbent resource usage", net);
            }
            if (auto diagnostic_error =
                    RetainDiagnostic(&state, pass_index, query_identity, query_policy.identity,
                                     CpuSequentialAttemptOutcome::kAdmissionRejected, std::nullopt,
                                     rejection->code, config.limits, &diagnostics);
                diagnostic_error.has_value()) {
              return *diagnostic_error;
            }
            continue;
          }

          candidates::RouteCandidate candidate =
              std::get<candidates::RouteCandidate>(std::move(admission));
          ExpansionResult expansion =
              ExpandCandidate(candidate, config.limits, &expanded_resource_uses, net);
          if (std::holds_alternative<CpuSequentialNegotiatedRoutingError>(expansion)) {
            return std::get<CpuSequentialNegotiatedRoutingError>(std::move(expansion));
          }
          std::vector<AtomicResourceUse> uses =
              std::get<std::vector<AtomicResourceUse>>(std::move(expansion));
          std::uint64_t replacement_retained_bytes = retained_candidate_bytes;
          if (state.incumbent.has_value()) {
            if (replacement_retained_bytes < state.incumbent->candidate.logical_bytes()) {
              return Error(CpuSequentialNegotiatedRoutingErrorCode::kInternalInvariant,
                           "allocator.cpu_sequential.retained_bytes_underflow.v1",
                           "Retained candidate-byte accounting underflowed during replacement",
                           net);
            }
            replacement_retained_bytes -= state.incumbent->candidate.logical_bytes();
          }
          if (!CheckedAdd(candidate.logical_bytes(), &replacement_retained_bytes)) {
            return Error(CpuSequentialNegotiatedRoutingErrorCode::kArithmeticOverflow,
                         "allocator.cpu_sequential.retained_bytes_overflow.v1",
                         "Retained candidate-byte accounting overflowed uint64", net);
          }
          if (replacement_retained_bytes > config.limits.maximum_retained_candidate_bytes) {
            return Error(CpuSequentialNegotiatedRoutingErrorCode::kBoundExhausted,
                         "allocator.cpu_sequential.actual_retained_bytes_bound.v1",
                         "Retained final routes exceed the configured candidate-byte bound", net);
          }
          if (!AddUses(uses, &occupancy)) {
            return Error(CpuSequentialNegotiatedRoutingErrorCode::kArithmeticOverflow,
                         "allocator.cpu_sequential.occupancy_overflow.v1",
                         "Installing an admitted replacement overflowed resource usage", net);
          }
          retained_candidate_bytes = replacement_retained_bytes;
          state.incumbent.emplace(RetainedCandidateState{
              .candidate = std::move(candidate),
              .uses = std::move(uses),
              .installed_pass_index = pass_index,
              .installed_query_identity = query_identity,
          });
          state.absence.reset();
          continue;
        }

        const routing::RouteFailure& failure = std::get<routing::RouteFailure>(route_result);
        if (failure.telemetry.has_value()) {
          if (!CheckedAdd(failure.telemetry->work_units, &cpu_work_units)) {
            return Error(CpuSequentialNegotiatedRoutingErrorCode::kArithmeticOverflow,
                         "allocator.cpu_sequential.actual_work_overflow.v1",
                         "Actual aggregate CPU work overflowed uint64", net);
          }
        }
        if (cpu_work_units > config.limits.maximum_aggregate_cpu_work_units) {
          return Error(CpuSequentialNegotiatedRoutingErrorCode::kBoundExhausted,
                       "allocator.cpu_sequential.actual_work_bound.v1",
                       "Actual aggregate CPU work exceeded its configured bound", net);
        }
        if (failure.code == routing::RouteFailureCode::kResourceExhausted) {
          CpuSequentialNegotiatedRoutingError error =
              Error(failure.telemetry.has_value() && failure.telemetry->work_limit_exhausted
                        ? CpuSequentialNegotiatedRoutingErrorCode::kBoundExhausted
                        : CpuSequentialNegotiatedRoutingErrorCode::kResourceExhausted,
                    failure.telemetry.has_value() && failure.telemetry->work_limit_exhausted
                        ? "allocator.cpu_sequential.query_work_bound.v1"
                        : "allocator.cpu_sequential.cpu_resource_exhaustion.v1",
                    "Production CPU A* exhausted a required bound or host resource", net);
          error.pass_index = pass_index;
          error.query_identity = query_identity;
          error.route_failure_code = failure.code;
          return error;
        }
        CpuSequentialAttemptOutcome ordinary_outcome;
        if (failure.code == routing::RouteFailureCode::kDisconnected) {
          ordinary_outcome = CpuSequentialAttemptOutcome::kDisconnected;
        } else if (failure.code == routing::RouteFailureCode::kUnsupportedLayerTransition ||
                   failure.code == routing::RouteFailureCode::kUnsupportedPolicy) {
          ordinary_outcome = CpuSequentialAttemptOutcome::kUnsupported;
        } else {
          CpuSequentialNegotiatedRoutingError error =
              Error(CpuSequentialNegotiatedRoutingErrorCode::kInternalInvariant,
                    "allocator.cpu_sequential.cpu_route_failure.v1",
                    "Production CPU A* returned a non-policy routing failure after preflight", net);
          error.pass_index = pass_index;
          error.query_identity = query_identity;
          error.route_failure_code = failure.code;
          return error;
        }
        if (!restore_incumbent()) {
          return Error(CpuSequentialNegotiatedRoutingErrorCode::kInternalInvariant,
                       "allocator.cpu_sequential.incumbent_restore.v1",
                       "Ordinary reroute failure could not restore incumbent resource usage", net);
        }
        if (auto diagnostic_error = RetainDiagnostic(
                &state, pass_index, query_identity, query_policy.identity, ordinary_outcome,
                failure.code, std::nullopt, config.limits, &diagnostics);
            diagnostic_error.has_value()) {
          return *diagnostic_error;
        }
      }

      std::vector<const candidates::RouteCandidate*> final_candidates;
      final_candidates.reserve(states.size());
      bool every_net_routed = true;
      for (const NetState& state : states) {
        if (state.incumbent.has_value()) {
          final_candidates.push_back(&state.incumbent->candidate);
        } else {
          every_net_routed = false;
        }
      }
      ResourceAccountingResult accounting = AccumulateResourceUsage(
          capacities, final_candidates,
          ResourceAccountingLimits{
              .maximum_candidates = config.limits.maximum_nets,
              .maximum_expanded_resource_uses =
                  config.limits.maximum_aggregate_expanded_resource_uses,
              .maximum_usage_units_per_resource = std::numeric_limits<std::uint64_t>::max(),
          });
      if (auto* accounting_error = std::get_if<ResourceAccountingError>(&accounting);
          accounting_error != nullptr) {
        return AccountingError(*accounting_error);
      }
      final_accounting = std::get<ResourceAccounting>(std::move(accounting));
      completed_passes = pass_index + 1U;

      // Deterministic termination precedence at a completed pass boundary:
      // feasible, then stalled, then pass limit. Fatal errors return before a
      // board-level result and therefore dominate all three.
      if (every_net_routed && final_accounting.total_overuse_units == 0) {
        termination = CpuSequentialTerminationReason::kFeasible;
        break;
      }
      std::vector<CanonicalStateKey> current_state_key = StateKey(states);
      if (previous_state_key.has_value() && *previous_state_key == current_state_key) {
        termination = CpuSequentialTerminationReason::kStalled;
        break;
      }
      if (completed_passes == config.policy.maximum_passes) {
        termination = CpuSequentialTerminationReason::kPassLimit;
        break;
      }
      previous_state_key = std::move(current_state_key);

      // Historical costs change only after a nonterminal completed pass and
      // are derived exclusively from that pass's canonical accounting.
      for (const ResourceUsage& usage : final_accounting.resources) {
        if (usage.overuse_units == 0) {
          continue;
        }
        const std::optional<std::uint64_t> increment =
            CheckedMultiply(usage.overuse_units, config.policy.historical_cost_increment);
        if (!increment.has_value()) {
          return Error(CpuSequentialNegotiatedRoutingErrorCode::kArithmeticOverflow,
                       "allocator.cpu_sequential.history_update_overflow.v1",
                       "Historical congestion-cost update overflowed uint64");
        }
        std::uint64_t& historical = historical_costs[usage.resource];
        if (!CheckedAdd(*increment, &historical)) {
          return Error(CpuSequentialNegotiatedRoutingErrorCode::kArithmeticOverflow,
                       "allocator.cpu_sequential.history_update_overflow.v1",
                       "Historical congestion-cost accumulation overflowed uint64");
        }
        if (historical > config.limits.maximum_congestion_cost_value) {
          return Error(CpuSequentialNegotiatedRoutingErrorCode::kBoundExhausted,
                       "allocator.cpu_sequential.history_update_bound.v1",
                       "Historical congestion cost exceeds the configured value bound");
        }
      }
    }

    CpuSequentialNegotiatedRouting result{
        .associations = capacities.associations(),
        .nets = {},
        .accounting = std::move(final_accounting),
        .diagnostics = std::move(diagnostics),
        .termination_reason = termination,
        .run_identity = run_identity,
        .completed_passes = completed_passes,
        .route_attempts = route_attempts,
        .cpu_work_units = cpu_work_units,
        .generated_candidate_bytes = generated_candidate_bytes,
        .retained_candidate_bytes = retained_candidate_bytes,
        .expanded_resource_uses = expanded_resource_uses,
    };
    result.nets.reserve(states.size());
    for (NetState& state : states) {
      const board_ir::EntityRef net = state.plan.submitted->request.net;
      if (state.incumbent.has_value()) {
        result.nets.emplace_back(CpuSequentialRetainedRoute{
            .net = net,
            .candidate = std::move(state.incumbent->candidate),
            .installed_pass_index = state.incumbent->installed_pass_index,
            .installed_query_identity = state.incumbent->installed_query_identity,
        });
      } else if (state.absence.has_value()) {
        result.nets.emplace_back(CpuSequentialRouteAbsence{
            .net = net,
            .reason = state.absence->reason,
            .final_attempt_pass_index = state.absence->pass_index,
            .final_query_identity = state.absence->query_identity,
        });
      } else {
        return Error(CpuSequentialNegotiatedRoutingErrorCode::kInternalInvariant,
                     "allocator.cpu_sequential.missing_final_outcome.v1",
                     "A requested net has neither a retained route nor structured absence", net);
      }
    }
    return result;
  } catch (const std::bad_alloc&) {
    return Error(CpuSequentialNegotiatedRoutingErrorCode::kResourceExhausted,
                 "allocator.cpu_sequential.allocation.v1",
                 "Sequential negotiated-routing scratch allocation failed");
  } catch (const std::length_error&) {
    return Error(CpuSequentialNegotiatedRoutingErrorCode::kResourceExhausted,
                 "allocator.cpu_sequential.container_capacity.v1",
                 "Sequential negotiated-routing container capacity was exceeded");
  } catch (...) {
    return Error(CpuSequentialNegotiatedRoutingErrorCode::kInternalInvariant,
                 "allocator.cpu_sequential.exception.v1",
                 "Sequential negotiated routing raised an unexpected exception");
  }
}

}  // namespace

CpuSequentialNegotiatedRoutingResult RouteCpuSequentialNegotiated(
    const board_ir::BoardSnapshot& board, const ResourceCapacityModel& capacities,
    std::span<const CpuSequentialNetRequest> nets, CpuSequentialNegotiatedRoutingConfig config) {
  return RouteImpl(board, capacities, nets, std::move(config), {});
}

CpuSequentialNegotiatedRoutingResult internal::RouteCpuSequentialNegotiatedWithTestHooks(
    const board_ir::BoardSnapshot& board, const ResourceCapacityModel& capacities,
    std::span<const CpuSequentialNetRequest> nets, CpuSequentialNegotiatedRoutingConfig config,
    CpuSequentialNegotiatedRoutingTestHooks hooks) {
  return RouteImpl(board, capacities, nets, std::move(config), hooks);
}

}  // namespace apgar::allocator
