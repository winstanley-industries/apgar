#include "apgar/allocator/targeted_regeneration_execution.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <optional>
#include <ranges>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

#include "apgar/board_ir/stable_hash.h"
#include "apgar/candidates/route_candidate.h"
#include "apgar/routing/cpu_astar.h"
#include "src/allocator/one_world_internal.h"
#include "src/allocator/targeted_regeneration_execution_internal.h"
#include "src/allocator/targeted_regeneration_internal.h"

namespace apgar::allocator {
namespace {

using UWide = unsigned __int128;

[[nodiscard]] TargetedRegenerationExecutionError Error(TargetedRegenerationExecutionErrorCode code,
                                                       std::string_view invariant_id,
                                                       std::string_view detail) noexcept {
  return TargetedRegenerationExecutionError{
      .code = code, .invariant_id = invariant_id, .detail = detail};
}

[[nodiscard]] bool ConfigIsValid(const TargetedRegenerationExecutionConfig& config) noexcept {
  return config.maximum_route_queries > 0 && config.maximum_route_queries <= 1'000'000 &&
         config.maximum_route_work_units > 0 && config.maximum_policy_resource_entries > 0 &&
         config.maximum_policy_resource_entries <= 100'000'000;
}

[[nodiscard]] bool NetBefore(board_ir::EntityRef left, board_ir::EntityRef right) noexcept {
  return std::tie(left.id, left.generation) < std::tie(right.id, right.generation);
}

[[nodiscard]] std::uint64_t NonZeroBatchIdentity(std::uint64_t plan_checksum,
                                                 board_ir::EntityRef net,
                                                 std::uint64_t target_index) noexcept {
  board_ir::StableHashBuilder hash;
  hash.AddString("APGAR-TARGETED-REGENERATION-CPU-BATCH-V1");
  hash.AddU64(plan_checksum);
  hash.AddU64(net.id);
  hash.AddU32(net.generation);
  hash.AddU64(target_index);
  const std::uint64_t identity = hash.Finish();
  return identity == 0 ? 1 : identity;
}

[[nodiscard]] candidates::CandidateRejection RouteFailureRejection(
    const candidates::CandidateAssociations& associations, const routing::RouteFailure& failure,
    board_ir::EntityRef net, const routing::NormalizedCandidateGenerationPolicy& policy,
    candidates::CandidateSchedulingIdentity scheduling) {
  candidates::CandidateRejection rejection;
  rejection.net = net;
  rejection.stage = candidates::CandidateLifecycleStage::kGenerated;
  rejection.code = failure.code == routing::RouteFailureCode::kDisconnected
                       ? candidates::CandidateRejectionCode::kBackendFailure
                       : candidates::CandidateRejectionCode::kUnsupported;
  rejection.invariant_id = failure.code == routing::RouteFailureCode::kDisconnected
                               ? "allocator.targeted_regeneration.route_disconnected.v1"
                               : "allocator.targeted_regeneration.route_unsupported.v1";
  rejection.associations = associations;
  rejection.policy_identity = policy.identity;
  rejection.provenance = candidates::CandidateProvenance{
      .generator = candidates::CandidateGeneratorKind::kCpuAStar,
      .generator_version = 1,
      .backend = candidates::CandidateBackendKind::kCpu,
      .supported_device_class = std::string(candidates::kCpuReferenceDeviceClassV1),
      .deterministic_seed = policy.policy.deterministic_seed,
      .batch_identity = scheduling.batch_identity,
      .query_identity = scheduling.query_identity,
      .candidate_ordinal = policy.policy.candidate_ordinal,
  };
  rejection.detail = failure.code == routing::RouteFailureCode::kDisconnected
                         ? "CPU targeted regeneration found no connected route"
                         : "CPU targeted regeneration encountered an unsupported route request";
  return candidates::CanonicalizeCandidateRejectionV1(rejection);
}

[[nodiscard]] bool ObjectiveImproved(const OneWorldAllocation& baseline,
                                     const OneWorldAllocation& refreshed) noexcept {
  if (baseline.selected_net_count != refreshed.selected_net_count) {
    return refreshed.selected_net_count > baseline.selected_net_count;
  }
  if (baseline.total_overuse_units != refreshed.total_overuse_units) {
    return refreshed.total_overuse_units < baseline.total_overuse_units;
  }
  return refreshed.total_intrinsic_cost < baseline.total_intrinsic_cost;
}

[[nodiscard]] std::uint64_t ChangedSelectionCount(const OneWorldAllocation& baseline,
                                                  const OneWorldAllocation& refreshed) noexcept {
  std::uint64_t changed = 0;
  std::size_t left = 0;
  std::size_t right = 0;
  while (left < baseline.selections.size() && right < refreshed.selections.size()) {
    const NetSelection& baseline_selection = baseline.selections[left];
    const NetSelection& refreshed_selection = refreshed.selections[right];
    if (NetBefore(baseline_selection.net, refreshed_selection.net)) {
      ++changed;
      ++left;
    } else if (NetBefore(refreshed_selection.net, baseline_selection.net)) {
      ++changed;
      ++right;
    } else {
      if (baseline_selection.status != refreshed_selection.status ||
          baseline_selection.candidate_id != refreshed_selection.candidate_id ||
          baseline_selection.candidate_payload_checksum !=
              refreshed_selection.candidate_payload_checksum) {
        ++changed;
      }
      ++left;
      ++right;
    }
  }
  changed += static_cast<std::uint64_t>(baseline.selections.size() - left);
  changed += static_cast<std::uint64_t>(refreshed.selections.size() - right);
  return changed;
}

[[nodiscard]] std::vector<candidates::CandidatePinRequest> WinnerPinRequests(
    const OneWorldAllocation& world) {
  std::vector<candidates::CandidatePinRequest> requests;
  requests.reserve(world.selections.size());
  for (const NetSelection& selection : world.selections) {
    if (selection.candidate == nullptr) {
      continue;
    }
    requests.push_back(candidates::CandidatePinRequest{
        .net = selection.net,
        .candidate_id = *selection.candidate_id,
        .candidate_payload_checksum = *selection.candidate_payload_checksum,
        .expected_candidate = selection.candidate,
    });
  }
  return requests;
}

[[nodiscard]] bool IsDuplicateCode(candidates::CandidateRejectionCode code) noexcept {
  return code == candidates::CandidateRejectionCode::kDuplicateIdentity ||
         code == candidates::CandidateRejectionCode::kDuplicateGeometry ||
         code == candidates::CandidateRejectionCode::kDuplicateResources;
}

[[nodiscard]] std::optional<std::size_t> FindColumnByQuery(
    std::span<const TargetedRegenerationColumnRecord> columns,
    std::uint64_t query_identity) noexcept {
  const auto found =
      std::ranges::find(columns, query_identity, &TargetedRegenerationColumnRecord::query_identity);
  if (found == columns.end()) {
    return std::nullopt;
  }
  return static_cast<std::size_t>(found - columns.begin());
}

struct PreparedTargetExecution {
  const TargetedRegenerationNet* target = nullptr;
  const PreparedNetRoutingContext* context = nullptr;
  std::uint64_t batch_identity = 0;
  std::uint64_t first_global_column = 0;
  std::vector<routing::NormalizedCandidateGenerationPolicy> policies;
};

[[nodiscard]] TargetedRegenerationExecutionResult WithFailureEnvelope(auto&& operation) {
  try {
    return operation();
  } catch (const std::bad_alloc&) {
    return Error(TargetedRegenerationExecutionErrorCode::kResourceExhausted,
                 "allocator.targeted_regeneration_execution.host_memory.v1",
                 "Host allocation failed within targeted-regeneration execution bounds");
  } catch (const std::length_error&) {
    return Error(TargetedRegenerationExecutionErrorCode::kResourceExhausted,
                 "allocator.targeted_regeneration_execution.host_container.v1",
                 "Host container limits were exhausted within execution bounds");
  }
}

void AddCandidateId(board_ir::StableHashBuilder& hash, const candidates::CandidateId& id) noexcept {
  hash.AddU64(id.high);
  hash.AddU64(id.low);
}

}  // namespace

std::uint64_t internal::ComputeTargetedRegenerationExecutionChecksumV1(
    const TargetedRegenerationExecutionChecksumHeaderV1& header,
    std::span<const TargetedRegenerationColumnRecord> columns) noexcept {
  board_ir::StableHashBuilder hash;
  hash.AddString("APGAR-TARGETED-REGENERATION-EXECUTION-V1");
  hash.AddU32(header.schema_version);
  hash.AddU64(header.plan_checksum);
  hash.AddU64(header.config.maximum_route_queries);
  hash.AddU64(header.config.maximum_route_work_units);
  hash.AddU64(header.config.maximum_policy_resource_entries);
  hash.AddU64(header.config.known_unmapped_exact_conflict_count);
  hash.AddU64(header.refreshed_request_manifest_checksum);
  hash.AddU64(header.refreshed_candidate_pool_manifest_checksum);
  hash.AddU64(header.baseline_world_checksum);
  hash.AddU64(header.refreshed_world_checksum);
  hash.AddByte(static_cast<std::uint8_t>(header.disposition));
  hash.AddByte(static_cast<std::uint8_t>(header.terminal_reason));
  hash.AddU64(header.counters.requested_columns);
  hash.AddU64(header.counters.successful_routes);
  hash.AddU64(header.counters.built_candidates);
  hash.AddU64(header.counters.admitted_candidates);
  hash.AddU64(header.counters.duplicate_candidates);
  hash.AddU64(header.counters.rejected_columns);
  hash.AddU64(header.counters.novel_retained_candidates);
  hash.AddU64(header.counters.changed_selections);
  hash.AddU64(header.counters.successor_pinned_candidates);
  hash.AddU64(columns.size());
  for (const TargetedRegenerationColumnRecord& column : columns) {
    hash.AddU64(column.net.id);
    hash.AddU32(column.net.generation);
    hash.AddU64(column.column_index);
    hash.AddU64(column.policy_identity);
    hash.AddU64(column.batch_identity);
    hash.AddU64(column.query_identity);
    hash.AddByte(static_cast<std::uint8_t>(column.outcome));
    hash.AddBool(column.candidate_id.has_value());
    if (column.candidate_id.has_value()) {
      AddCandidateId(hash, *column.candidate_id);
    }
    hash.AddBool(column.candidate_payload_checksum.has_value());
    if (column.candidate_payload_checksum.has_value()) {
      hash.AddU64(*column.candidate_payload_checksum);
    }
    hash.AddBool(column.rejection_code.has_value());
    if (column.rejection_code.has_value()) {
      hash.AddByte(static_cast<std::uint8_t>(*column.rejection_code));
    }
  }
  return hash.Finish();
}

routing::PlanarRouteRequest internal::BuildTargetedRegenerationRouteRequestV1(
    const routing::PlanarRouteRequest& source,
    const routing::CandidateGenerationPolicy& candidate_policy) {
  return routing::PlanarRouteRequest{
      .net = source.net,
      .start = source.start,
      .goal = source.goal,
      .start_layer = source.start_layer,
      .goal_layer = source.goal_layer,
      .candidate_policy = candidate_policy,
  };
}

TargetedRegenerationExecutionResult ExecuteTargetedRegenerationPlanCpu(
    std::uint32_t schema_version, const board_ir::BoardSnapshot& board,
    const OneWorldAllocationRequest& source_request, TargetedRegenerationPlan&& plan,
    candidates::CandidateStore& candidate_store,
    const TargetedRegenerationExecutionConfig& config) {
  if (schema_version != kTargetedRegenerationExecutionSchemaVersion) {
    return Error(TargetedRegenerationExecutionErrorCode::kUnsupportedSchema,
                 "allocator.targeted_regeneration_execution.schema.v1",
                 "Targeted-regeneration execution schema is unsupported");
  }
  if (!ConfigIsValid(config)) {
    return Error(TargetedRegenerationExecutionErrorCode::kInvalidConfiguration,
                 "allocator.targeted_regeneration_execution.configuration.v1",
                 "Targeted-regeneration execution configuration is outside schema-v1 bounds");
  }
  if (!plan.has_active_pin_lease()) {
    return Error(TargetedRegenerationExecutionErrorCode::kInactivePlanLease,
                 "allocator.targeted_regeneration_execution.plan_lease.v1",
                 "Execution requires the plan's active CandidateStore retention lease");
  }
  if (!plan.pin_lease_belongs_to(candidate_store)) {
    return Error(TargetedRegenerationExecutionErrorCode::kWrongCandidateStore,
                 "allocator.targeted_regeneration_execution.plan_store.v1",
                 "The plan retention lease belongs to a different CandidateStore");
  }

  return WithFailureEnvelope([&]() -> TargetedRegenerationExecutionResult {
    if (source_request.workload == nullptr ||
        source_request.workload->workload_checksum() != plan.workload_checksum() ||
        source_request.workload->board_content_hash() != board.content_hash() ||
        source_request.associations != plan.associations() ||
        source_request.capacities.associations() != plan.associations()) {
      return Error(TargetedRegenerationExecutionErrorCode::kAssociationMismatch,
                   "allocator.targeted_regeneration_execution.associations.v1",
                   "Board, workload, request, capacity, and plan associations must match");
    }

    const bool resource_refinement_required = config.known_unmapped_exact_conflict_count != 0;
    if (source_request.pools.size() >
        candidate_store.config().maximum_expected_pools_per_invocation) {
      return Error(TargetedRegenerationExecutionErrorCode::kWorkBoundExceeded,
                   "allocator.targeted_regeneration_execution.source_pool_budget.v1",
                   "The source request exceeds the CandidateStore expected-pool bound");
    }
    UWide source_candidate_count = 0;
    for (const CandidatePool& pool : source_request.pools) {
      source_candidate_count += pool.candidates.size();
      if (source_candidate_count >
          candidate_store.config().maximum_expected_candidates_per_invocation) {
        return Error(TargetedRegenerationExecutionErrorCode::kWorkBoundExceeded,
                     "allocator.targeted_regeneration_execution.source_candidate_budget.v1",
                     "The source request exceeds the CandidateStore expected-candidate bound");
      }
    }
    if (!resource_refinement_required &&
        (plan.total_requested_columns() > config.maximum_route_queries ||
         plan.total_requested_columns() >
             candidate_store.config().maximum_admission_items_per_transaction ||
         plan.total_requested_columns() >
             candidate_store.config().maximum_rejection_items_per_transaction)) {
      return Error(TargetedRegenerationExecutionErrorCode::kWorkBoundExceeded,
                   "allocator.targeted_regeneration_execution.query_budget.v1",
                   "The plan exceeds a CPU query or CandidateStore invocation item bound");
    }

    UWide route_work = 0;
    UWide projected_policy_entries = 0;
    std::uint64_t preflight_column_count = 0;
    for (std::size_t target_index = 0;
         !resource_refinement_required && target_index < plan.targets().size(); ++target_index) {
      const TargetedRegenerationNet& target = plan.targets()[target_index];
      const PreparedNetRoutingContext* context = source_request.workload->FindNet(target.net);
      if (context == nullptr) {
        return Error(TargetedRegenerationExecutionErrorCode::kAssociationMismatch,
                     "allocator.targeted_regeneration_execution.target_workload.v1",
                     "A regeneration target is absent from the authentic workload");
      }
      if (target.requested_columns == 0 ||
          target.requested_columns > target.resource_actions.size() + 1) {
        return Error(TargetedRegenerationExecutionErrorCode::kPlanInvariant,
                     "allocator.targeted_regeneration_execution.target_columns.v1",
                     "A target's column budget cannot be represented by its action schedule");
      }
      route_work += static_cast<UWide>(target.requested_columns) *
                    context->compiled_board.telemetry().represented_nodes *
                    (routing::kNoIncomingDirection + 1U);
      if (route_work > config.maximum_route_work_units) {
        return Error(TargetedRegenerationExecutionErrorCode::kWorkBoundExceeded,
                     "allocator.targeted_regeneration_execution.route_work_budget.v1",
                     "The deterministic CPU route-work bound is exceeded before generation");
      }
      if (preflight_column_count > std::numeric_limits<std::uint32_t>::max() ||
          target.requested_columns >
              static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()) -
                  preflight_column_count + 1U) {
        return Error(TargetedRegenerationExecutionErrorCode::kWorkBoundExceeded,
                     "allocator.targeted_regeneration_execution.ordinal_budget.v1",
                     "Candidate ordinals exceed the schema-v1 unsigned 32-bit domain");
      }
      const internal::TargetedRegenerationPolicyEntryProjectionV1 entry_projection =
          internal::ProjectTargetedRegenerationPolicyEntriesV1(context->compiled_board,
                                                               plan.price_state().prices(), target);
      if (!internal::TargetedRegenerationPolicyEntriesFitV1(
              entry_projection.aggregate_entry_count, routing::kMaximumPolicyResourceEntries) ||
          entry_projection.aggregate_entry_count >
              config.maximum_policy_resource_entries - projected_policy_entries) {
        return Error(TargetedRegenerationExecutionErrorCode::kWorkBoundExceeded,
                     "allocator.targeted_regeneration_execution.policy_entry_budget.v1",
                     "Synthesized policies exceed the aggregate resource-entry budget");
      }
      projected_policy_entries += entry_projection.aggregate_entry_count;
      preflight_column_count += target.requested_columns;
    }
    if (!resource_refinement_required && preflight_column_count != plan.total_requested_columns()) {
      return Error(TargetedRegenerationExecutionErrorCode::kPlanInvariant,
                   "allocator.targeted_regeneration_execution.preflight_column_count.v1",
                   "Target columns differ from the plan's aggregate column count");
    }

    internal::OneWorldSelectionEvidenceResult source_evidence_result =
        internal::SelectOneWorldWithoutAccounting(source_request);
    if (const auto* failure = std::get_if<AllocationError>(&source_evidence_result);
        failure != nullptr) {
      static_cast<void>(failure);
      return Error(TargetedRegenerationExecutionErrorCode::kSourceRequestDrift,
                   "allocator.targeted_regeneration_execution.source_request.v1",
                   "The source request no longer passes canonical One-World validation");
    }
    const internal::OneWorldSelectionEvidence& source_evidence =
        std::get<internal::OneWorldSelectionEvidence>(source_evidence_result);
    if (source_evidence.request_manifest_checksum != plan.source_request_manifest_checksum() ||
        source_evidence.candidate_pool_manifest_checksum !=
            plan.candidate_pool_manifest_checksum() ||
        source_evidence.source_pool_count != plan.source_pool_count() ||
        source_evidence.source_candidate_count != plan.source_candidate_count()) {
      return Error(TargetedRegenerationExecutionErrorCode::kSourceRequestDrift,
                   "allocator.targeted_regeneration_execution.source_manifest.v1",
                   "The complete source request or candidate pools differ from the planned input");
    }

    NegotiatedPriceSnapshotResult price_result =
        BuildPriceSnapshotForState(source_request.capacities, plan.price_state());
    if (!std::holds_alternative<PriceSnapshot>(price_result)) {
      return Error(TargetedRegenerationExecutionErrorCode::kPlanInvariant,
                   "allocator.targeted_regeneration_execution.price_snapshot.v1",
                   "The plan's next negotiated-price state cannot recreate its snapshot");
    }
    OneWorldAllocationRequest baseline_request = source_request;
    baseline_request.prices = std::get<PriceSnapshot>(std::move(price_result));

    internal::OneWorldSelectionEvidenceResult baseline_evidence_result =
        internal::SelectOneWorldWithoutAccounting(baseline_request);
    if (!std::holds_alternative<internal::OneWorldSelectionEvidence>(baseline_evidence_result)) {
      return Error(TargetedRegenerationExecutionErrorCode::kPlanInvariant,
                   "allocator.targeted_regeneration_execution.baseline_selection.v1",
                   "The next-price baseline selection cannot be reproduced");
    }
    const internal::OneWorldSelectionEvidence& baseline_evidence =
        std::get<internal::OneWorldSelectionEvidence>(baseline_evidence_result);
    if (baseline_evidence.candidate_pool_manifest_checksum !=
        plan.candidate_pool_manifest_checksum()) {
      return Error(TargetedRegenerationExecutionErrorCode::kPlanInvariant,
                   "allocator.targeted_regeneration_execution.baseline_pool_manifest.v1",
                   "Applying the next price snapshot changed candidate-pool identity");
    }
    OneWorldAllocationResult baseline_world_result = AllocateOneWorld(baseline_request);
    if (!std::holds_alternative<OneWorldAllocation>(baseline_world_result)) {
      return Error(TargetedRegenerationExecutionErrorCode::kAllocation,
                   "allocator.targeted_regeneration_execution.baseline_world.v1",
                   "The complete next-price baseline world cannot be allocated");
    }
    OneWorldAllocation baseline_world =
        std::get<OneWorldAllocation>(std::move(baseline_world_result));

    std::vector<candidates::CandidateStoreExpectedPool> expected_pools;
    expected_pools.reserve(source_request.pools.size());
    for (const CandidatePool& pool : source_request.pools) {
      const PreparedNetRoutingContext* context = source_request.workload->FindNet(pool.net);
      if (context == nullptr) {
        return Error(TargetedRegenerationExecutionErrorCode::kAssociationMismatch,
                     "allocator.targeted_regeneration_execution.pool_workload.v1",
                     "A source candidate pool is absent from the authentic workload");
      }
      expected_pools.push_back(candidates::CandidateStoreExpectedPool{
          .net = pool.net,
          .associations = candidates::AssociationsFor(board, context->compiled_board),
          .candidates = pool.candidates,
      });
    }
    candidates::CandidateStoreInvocationAdmissionResult source_store_result =
        candidate_store.AdmitInvocationIfSourcePoolsMatch(
            board, std::vector<candidates::CandidateStoreExpectedPool>(expected_pools), {});
    if (const auto* failure = std::get_if<candidates::CandidateStoreError>(&source_store_result);
        failure != nullptr) {
      return Error(failure->code == candidates::CandidateStoreErrorCode::kResourceExhausted
                       ? TargetedRegenerationExecutionErrorCode::kResourceExhausted
                       : TargetedRegenerationExecutionErrorCode::kCandidateStore,
                   failure->code == candidates::CandidateStoreErrorCode::kStoreDrift
                       ? "allocator.targeted_regeneration_execution.store_drift.v1"
                       : "allocator.targeted_regeneration_execution.store_preflight.v1",
                   failure->code == candidates::CandidateStoreErrorCode::kStoreDrift
                       ? "CandidateStore pools or associations changed before CPU generation"
                       : "CandidateStore rejected the source-pool execution preflight");
    }
    if (!std::get<std::vector<candidates::CandidateStoreAdmissionResult>>(source_store_result)
             .empty()) {
      return Error(TargetedRegenerationExecutionErrorCode::kInternalInvariant,
                   "allocator.targeted_regeneration_execution.store_preflight_result.v1",
                   "A source-only CandidateStore preflight returned publication outcomes");
    }

    UWide policy_entries = 0;
    std::uint64_t prepared_column_count = 0;
    std::vector<PreparedTargetExecution> prepared_targets;
    std::vector<TargetedRegenerationColumnRecord> columns;
    std::vector<candidates::CandidateInvocationItem> invocation_items;
    if (!resource_refinement_required) {
      prepared_targets.reserve(plan.targets().size());
      columns.reserve(static_cast<std::size_t>(plan.total_requested_columns()));
      invocation_items.reserve(static_cast<std::size_t>(plan.total_requested_columns()));
    }

    for (std::size_t target_index = 0;
         !resource_refinement_required && target_index < plan.targets().size(); ++target_index) {
      const TargetedRegenerationNet& target = plan.targets()[target_index];
      const PreparedNetRoutingContext* context = source_request.workload->FindNet(target.net);
      if (context == nullptr) {
        return Error(TargetedRegenerationExecutionErrorCode::kAssociationMismatch,
                     "allocator.targeted_regeneration_execution.target_workload.v1",
                     "A regeneration target is absent from the authentic workload");
      }
      const auto baseline_pool =
          std::ranges::lower_bound(baseline_evidence.pools, target.net, NetBefore,
                                   &internal::OneWorldPoolSelectionEvidence::net);
      if (baseline_pool == baseline_evidence.pools.end() || baseline_pool->net != target.net ||
          baseline_pool->pool_manifest_checksum != target.source_pool_manifest_checksum ||
          baseline_pool->candidate_count != target.source_pool_candidate_count ||
          baseline_pool->selection.candidate_id != target.next_price_candidate_id ||
          baseline_pool->selection.candidate_payload_checksum !=
              target.next_price_candidate_payload_checksum ||
          baseline_pool->selection.selection_score != target.next_price_selection_score) {
        return Error(TargetedRegenerationExecutionErrorCode::kPlanInvariant,
                     "allocator.targeted_regeneration_execution.target_replay.v1",
                     "A target no longer matches its complete-pool next-price selection");
      }
      const std::uint64_t batch_identity =
          NonZeroBatchIdentity(plan.plan_checksum(), target.net, target_index);
      TargetedRegenerationPolicyResult policies_result = BuildTargetedRegenerationPoliciesV1(
          *context, plan.price_state(), source_request.intrinsic_cost_weight, target,
          batch_identity, static_cast<std::uint32_t>(prepared_column_count));
      if (!std::holds_alternative<std::vector<routing::NormalizedCandidateGenerationPolicy>>(
              policies_result)) {
        return Error(TargetedRegenerationExecutionErrorCode::kPolicySynthesis,
                     "allocator.targeted_regeneration_execution.policy_synthesis.v1",
                     "The plan cannot synthesize its exact bounded CPU policy schedule");
      }
      std::vector<routing::NormalizedCandidateGenerationPolicy> policies =
          std::get<std::vector<routing::NormalizedCandidateGenerationPolicy>>(
              std::move(policies_result));
      if (policies.size() != target.requested_columns) {
        return Error(TargetedRegenerationExecutionErrorCode::kInternalInvariant,
                     "allocator.targeted_regeneration_execution.policy_count.v1",
                     "Policy synthesis returned a different number of columns than planned");
      }
      UWide synthesized_policy_entries = 0;
      for (const routing::NormalizedCandidateGenerationPolicy& policy : policies) {
        synthesized_policy_entries += policy.policy.banned_resources.size();
        synthesized_policy_entries += policy.policy.resource_penalties.size();
      }
      policy_entries += synthesized_policy_entries;
      prepared_targets.push_back(PreparedTargetExecution{
          .target = &target,
          .context = context,
          .batch_identity = batch_identity,
          .first_global_column = prepared_column_count,
          .policies = std::move(policies),
      });
      prepared_column_count += target.requested_columns;
    }
    if (!resource_refinement_required && prepared_column_count != plan.total_requested_columns()) {
      return Error(TargetedRegenerationExecutionErrorCode::kInternalInvariant,
                   "allocator.targeted_regeneration_execution.preflight_column_count.v1",
                   "Prepared CPU policies differ from the plan's aggregate column count");
    }
    if (!resource_refinement_required && policy_entries != projected_policy_entries) {
      return Error(TargetedRegenerationExecutionErrorCode::kInternalInvariant,
                   "allocator.targeted_regeneration_execution.policy_entry_replay.v1",
                   "Policy synthesis differs from its allocation-free resource-entry preflight");
    }

    std::uint64_t global_column = 0;
    for (const PreparedTargetExecution& prepared : prepared_targets) {
      const TargetedRegenerationNet& target = *prepared.target;
      const PreparedNetRoutingContext& context = *prepared.context;
      if (global_column != prepared.first_global_column) {
        return Error(TargetedRegenerationExecutionErrorCode::kInternalInvariant,
                     "allocator.targeted_regeneration_execution.prepared_order.v1",
                     "Prepared target schedules are outside canonical plan order");
      }
      for (std::size_t column_index = 0; column_index < prepared.policies.size(); ++column_index) {
        const routing::NormalizedCandidateGenerationPolicy& policy =
            prepared.policies[column_index];
        const std::uint64_t query_identity = global_column + 1U;
        const candidates::CandidateSchedulingIdentity scheduling{
            .batch_identity = prepared.batch_identity, .query_identity = query_identity};
        columns.push_back(TargetedRegenerationColumnRecord{
            .net = target.net,
            .column_index = column_index,
            .policy_identity = policy.identity,
            .batch_identity = prepared.batch_identity,
            .query_identity = query_identity,
            .candidate_id = std::nullopt,
            .candidate_payload_checksum = std::nullopt,
            .rejection_code = std::nullopt,
        });
        routing::PlanarRouteRequest request =
            internal::BuildTargetedRegenerationRouteRequestV1(context.request, policy.policy);
        routing::CpuRouteResult route_result =
            routing::RouteWithCpuAStar(board, context.compiled_board, request);
        if (const auto* failure = std::get_if<routing::RouteFailure>(&route_result);
            failure != nullptr) {
          if (failure->code == routing::RouteFailureCode::kDisconnected ||
              failure->code == routing::RouteFailureCode::kUnsupportedLayerTransition ||
              failure->code == routing::RouteFailureCode::kUnsupportedPolicy) {
            columns.back().outcome = failure->code == routing::RouteFailureCode::kDisconnected
                                         ? TargetedRegenerationColumnOutcome::kRouteDisconnected
                                         : TargetedRegenerationColumnOutcome::kRouteUnsupported;
            const candidates::CandidateRejection rejection =
                RouteFailureRejection(candidates::AssociationsFor(board, context.compiled_board),
                                      *failure, target.net, policy, scheduling);
            columns.back().rejection_code = rejection.code;
            invocation_items.emplace_back(rejection);
            ++global_column;
            continue;
          }
          return Error(failure->code == routing::RouteFailureCode::kResourceExhausted
                           ? TargetedRegenerationExecutionErrorCode::kResourceExhausted
                           : TargetedRegenerationExecutionErrorCode::kCandidateGeneration,
                       "allocator.targeted_regeneration_execution.cpu_route.v1",
                       "CPU A* failed after successful execution preflight");
        }
        const routing::CpuRoute& route = std::get<routing::CpuRoute>(route_result);
        candidates::CandidateDraftBuildResult draft_result =
            candidates::BuildGeneratedCandidateFromCpuRoute(board, context.compiled_board, request,
                                                            policy, route, scheduling);
        if (auto* rejection = std::get_if<candidates::CandidateRejection>(&draft_result);
            rejection != nullptr) {
          columns.back().outcome = TargetedRegenerationColumnOutcome::kBuildRejected;
          columns.back().candidate_id = rejection->candidate_id;
          columns.back().candidate_payload_checksum = rejection->candidate_payload_checksum;
          columns.back().rejection_code = rejection->code;
          invocation_items.emplace_back(std::move(*rejection));
          ++global_column;
          continue;
        }
        candidates::GeneratedRouteCandidate draft =
            std::get<candidates::GeneratedRouteCandidate>(std::move(draft_result));
        columns.back().candidate_id = draft.id;
        columns.back().candidate_payload_checksum = draft.payload_checksum;
        invocation_items.emplace_back(candidates::CandidateInvocationGeneratedItem{
            .compiled_board = std::cref(context.compiled_board),
            .request = std::move(request),
            .generated = std::move(draft),
        });
        ++global_column;
      }
    }
    if (!resource_refinement_required &&
        (global_column != plan.total_requested_columns() || columns.size() != global_column)) {
      return Error(TargetedRegenerationExecutionErrorCode::kInternalInvariant,
                   "allocator.targeted_regeneration_execution.column_count.v1",
                   "Executed CPU columns differ from the plan's aggregate count");
    }

    candidates::CandidateStoreInvocationAdmissionResult publication_result =
        candidate_store.AdmitInvocationIfSourcePoolsMatch(board, std::move(expected_pools),
                                                          std::move(invocation_items));
    if (const auto* failure = std::get_if<candidates::CandidateStoreError>(&publication_result);
        failure != nullptr) {
      return Error(failure->code == candidates::CandidateStoreErrorCode::kResourceExhausted
                       ? TargetedRegenerationExecutionErrorCode::kResourceExhausted
                       : TargetedRegenerationExecutionErrorCode::kCandidateStore,
                   failure->code == candidates::CandidateStoreErrorCode::kStoreDrift
                       ? "allocator.targeted_regeneration_execution.store_drift.v1"
                       : "allocator.targeted_regeneration_execution.store_publication.v1",
                   failure->code == candidates::CandidateStoreErrorCode::kStoreDrift
                       ? "CandidateStore pools changed since the plan's source request"
                       : "CandidateStore rejected the atomic invocation publication");
    }

    TargetedRegenerationExecutionCounters counters;
    counters.requested_columns = global_column;
    std::vector<candidates::CandidateStoreAdmissionResult> publication =
        std::get<std::vector<candidates::CandidateStoreAdmissionResult>>(
            std::move(publication_result));
    for (const candidates::CandidateStoreAdmissionResult& result : publication) {
      std::uint64_t query_identity = 0;
      if (const auto* stored = std::get_if<candidates::StoredCandidate>(&result);
          stored != nullptr) {
        query_identity = (*stored)->data().provenance.query_identity;
      } else {
        query_identity = std::get<candidates::CandidateRejection>(result).provenance.query_identity;
      }
      const std::optional<std::size_t> column_index = FindColumnByQuery(columns, query_identity);
      if (!column_index.has_value()) {
        return Error(TargetedRegenerationExecutionErrorCode::kInternalInvariant,
                     "allocator.targeted_regeneration_execution.publication_correlation.v1",
                     "CandidateStore returned an outcome without its scheduled CPU column");
      }
      TargetedRegenerationColumnRecord& column = columns[*column_index];
      if (const auto* stored = std::get_if<candidates::StoredCandidate>(&result);
          stored != nullptr) {
        column.outcome = TargetedRegenerationColumnOutcome::kAdmitted;
        column.candidate_id = (*stored)->id();
        column.candidate_payload_checksum = (*stored)->data().payload_checksum;
        column.rejection_code.reset();
        ++counters.admitted_candidates;
      } else {
        const candidates::CandidateRejection& rejection =
            std::get<candidates::CandidateRejection>(result);
        column.candidate_id = rejection.candidate_id;
        column.candidate_payload_checksum = rejection.candidate_payload_checksum;
        column.rejection_code = rejection.code;
        if (IsDuplicateCode(rejection.code)) {
          column.outcome = TargetedRegenerationColumnOutcome::kDuplicate;
          ++counters.duplicate_candidates;
        } else if (column.outcome != TargetedRegenerationColumnOutcome::kRouteDisconnected &&
                   column.outcome != TargetedRegenerationColumnOutcome::kRouteUnsupported &&
                   column.outcome != TargetedRegenerationColumnOutcome::kBuildRejected) {
          column.outcome = TargetedRegenerationColumnOutcome::kAdmissionRejected;
        }
      }
    }
    for (const TargetedRegenerationColumnRecord& column : columns) {
      if (column.outcome != TargetedRegenerationColumnOutcome::kRouteDisconnected &&
          column.outcome != TargetedRegenerationColumnOutcome::kRouteUnsupported) {
        ++counters.successful_routes;
      }
      if (column.outcome == TargetedRegenerationColumnOutcome::kAdmitted ||
          column.outcome == TargetedRegenerationColumnOutcome::kDuplicate ||
          column.outcome == TargetedRegenerationColumnOutcome::kAdmissionRejected) {
        ++counters.built_candidates;
      }
      if (column.outcome != TargetedRegenerationColumnOutcome::kAdmitted) {
        ++counters.rejected_columns;
      }
    }

    std::set<candidates::CandidateId> source_ids;
    for (const CandidatePool& pool : source_request.pools) {
      for (const candidates::StoredCandidate& candidate : pool.candidates) {
        source_ids.insert(candidate->id());
      }
    }
    std::vector<CandidatePool> refreshed_pools;
    refreshed_pools.reserve(source_request.pools.size());
    for (const CandidatePool& pool : source_request.pools) {
      std::vector<candidates::StoredCandidate> current = candidate_store.Enumerate(pool.net);
      for (const candidates::StoredCandidate& candidate : current) {
        if (!source_ids.contains(candidate->id())) {
          ++counters.novel_retained_candidates;
        }
      }
      refreshed_pools.push_back(CandidatePool{.net = pool.net, .candidates = std::move(current)});
    }
    std::ranges::sort(refreshed_pools, [](const CandidatePool& left, const CandidatePool& right) {
      return NetBefore(left.net, right.net);
    });

    OneWorldAllocationRequest refreshed_request = baseline_request;
    refreshed_request.pools = refreshed_pools;
    internal::OneWorldSelectionEvidenceResult refreshed_evidence_result =
        internal::SelectOneWorldWithoutAccounting(refreshed_request);
    if (!std::holds_alternative<internal::OneWorldSelectionEvidence>(refreshed_evidence_result)) {
      return Error(TargetedRegenerationExecutionErrorCode::kAllocation,
                   "allocator.targeted_regeneration_execution.refreshed_request.v1",
                   "The refreshed complete store pools cannot be selected");
    }
    const internal::OneWorldSelectionEvidence& refreshed_evidence =
        std::get<internal::OneWorldSelectionEvidence>(refreshed_evidence_result);
    OneWorldAllocationResult refreshed_world_result = AllocateOneWorld(refreshed_request);
    if (!std::holds_alternative<OneWorldAllocation>(refreshed_world_result)) {
      return Error(TargetedRegenerationExecutionErrorCode::kAllocation,
                   "allocator.targeted_regeneration_execution.refreshed_world.v1",
                   "The refreshed complete store pools cannot be allocated");
    }
    OneWorldAllocation refreshed_world =
        std::get<OneWorldAllocation>(std::move(refreshed_world_result));
    counters.changed_selections = ChangedSelectionCount(baseline_world, refreshed_world);

    std::optional<candidates::CandidateStorePinLease> successor_lease;
    std::vector<candidates::CandidatePinRequest> pin_requests = WinnerPinRequests(refreshed_world);
    counters.successor_pinned_candidates = pin_requests.size();
    if (!pin_requests.empty()) {
      candidates::CandidateStorePinLeaseResult lease_result =
          candidate_store.AcquirePinLease(pin_requests);
      if (const auto* failure = std::get_if<candidates::CandidateStoreError>(&lease_result);
          failure != nullptr) {
        return Error(failure->code == candidates::CandidateStoreErrorCode::kResourceExhausted
                         ? TargetedRegenerationExecutionErrorCode::kResourceExhausted
                         : TargetedRegenerationExecutionErrorCode::kSuccessorLease,
                     "allocator.targeted_regeneration_execution.successor_lease.v1",
                     "CandidateStore could not atomically retain refreshed world selections");
      }
      successor_lease.emplace(
          std::get<candidates::CandidateStorePinLease>(std::move(lease_result)));
    }

    TargetedRegenerationExecutionDisposition disposition;
    TargetedRegenerationTerminalReason terminal_reason;
    if (resource_refinement_required) {
      disposition = TargetedRegenerationExecutionDisposition::kResourceRefinementRequired;
      terminal_reason = TargetedRegenerationTerminalReason::kResourceRefinementRequired;
    } else if (plan.targets().empty()) {
      disposition = TargetedRegenerationExecutionDisposition::kNoWork;
      terminal_reason =
          refreshed_world.total_overuse_units == 0 && refreshed_world.no_candidate_net_count == 0
              ? TargetedRegenerationTerminalReason::kFeasible
              : TargetedRegenerationTerminalReason::kNoTargets;
    } else if (ObjectiveImproved(baseline_world, refreshed_world)) {
      disposition = TargetedRegenerationExecutionDisposition::kProgress;
      terminal_reason = TargetedRegenerationTerminalReason::kLexicographicallyImproved;
    } else {
      disposition = TargetedRegenerationExecutionDisposition::kStalled;
      if (counters.successful_routes == 0) {
        terminal_reason = TargetedRegenerationTerminalReason::kNoSuccessfulGeneration;
      } else if (counters.novel_retained_candidates == 0) {
        terminal_reason = TargetedRegenerationTerminalReason::kNoNovelRetainedColumns;
      } else if (counters.changed_selections == 0) {
        terminal_reason = TargetedRegenerationTerminalReason::kNoSelectionChange;
      } else {
        terminal_reason =
            TargetedRegenerationTerminalReason::kSelectionChangedWithoutObjectiveImprovement;
      }
    }

    const internal::TargetedRegenerationExecutionChecksumHeaderV1 checksum_header{
        .schema_version = schema_version,
        .plan_checksum = plan.plan_checksum(),
        .config = config,
        .refreshed_request_manifest_checksum = refreshed_evidence.request_manifest_checksum,
        .refreshed_candidate_pool_manifest_checksum =
            refreshed_evidence.candidate_pool_manifest_checksum,
        .baseline_world_checksum = baseline_world.world_checksum,
        .refreshed_world_checksum = refreshed_world.world_checksum,
        .disposition = disposition,
        .terminal_reason = terminal_reason,
        .counters = counters,
    };
    const std::uint64_t checksum =
        internal::ComputeTargetedRegenerationExecutionChecksumV1(checksum_header, columns);
    return TargetedRegenerationExecution(
        schema_version, std::move(plan), config, disposition, terminal_reason, counters,
        std::move(columns), std::move(refreshed_pools), std::move(baseline_world),
        std::move(refreshed_world), checksum, std::move(successor_lease));
  });
}

}  // namespace apgar::allocator
