#include "apgar/allocator/cpu_candidate_allocation_session.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <utility>
#include <variant>
#include <vector>

#include "apgar/board_ir/stable_hash.h"
#include "src/allocator/cpu_candidate_allocation_session_internal.h"
#include "src/allocator/multi_world_internal.h"
#include "src/allocator/negotiated_prices_internal.h"
#include "src/allocator/one_world_internal.h"
#include "src/allocator/targeted_regeneration_execution_internal.h"
#include "src/allocator/targeted_regeneration_internal.h"

namespace apgar::allocator {
namespace {

using UWide = unsigned __int128;

thread_local bool g_final_assembly_failure_for_testing = false;
thread_local std::uint64_t g_source_span_inspections_for_testing = 0;
thread_local std::uint64_t g_epoch_reservation_for_testing = 0;

[[nodiscard]] CpuCandidateAllocationSessionError Error(
    CpuCandidateAllocationSessionErrorCode code, std::string_view invariant_id,
    std::string_view detail, std::optional<std::uint32_t> epoch_index = std::nullopt,
    std::optional<TargetedRegenerationExecutionError::FailedExecutionObservation>
        failed_regeneration = std::nullopt,
    bool candidate_store_publication_committed = false) noexcept {
  return CpuCandidateAllocationSessionError{
      .code = code,
      .invariant_id = invariant_id,
      .detail = detail,
      .epoch_index = epoch_index,
      .failed_regeneration = std::move(failed_regeneration),
      .candidate_store_publication_committed = candidate_store_publication_committed,
  };
}

[[nodiscard]] CpuCandidateAllocationSessionError WithPublicationStatus(
    CpuCandidateAllocationSessionError error, bool candidate_store_publication_committed) noexcept {
  error.candidate_store_publication_committed = candidate_store_publication_committed;
  return error;
}

[[nodiscard]] bool LimitsAreValid(const CpuCandidateAllocationSessionLimits& limits) noexcept {
  return limits.maximum_epoch_records > 0 &&
         limits.maximum_epoch_records <= kMaximumCpuCandidateAllocationEpochsV1 &&
         limits.maximum_total_planning_expanded_resource_visits > 0 &&
         limits.maximum_total_planning_expanded_resource_visits <=
             kMaximumCpuCandidateAllocationWorkItemsV1 &&
         limits.maximum_total_requested_columns > 0 &&
         limits.maximum_total_requested_columns <= kMaximumCpuCandidateAllocationWorkItemsV1 &&
         limits.maximum_total_route_queries > 0 &&
         limits.maximum_total_route_queries <= kMaximumCpuCandidateAllocationWorkItemsV1 &&
         limits.maximum_total_route_work_units > 0 &&
         limits.maximum_total_route_work_units <= kMaximumCpuCandidateAllocationWorkItemsV1 &&
         limits.maximum_total_policy_projection_visits > 0 &&
         limits.maximum_total_policy_projection_visits <=
             kMaximumCpuCandidateAllocationWorkItemsV1 &&
         limits.maximum_total_generated_candidate_bytes > 0 &&
         limits.maximum_total_generated_candidate_bytes <=
             kMaximumCpuCandidateAllocationWorkItemsV1 &&
         limits.maximum_total_rejection_bytes > 0 &&
         limits.maximum_total_rejection_bytes <= kMaximumCpuCandidateAllocationWorkItemsV1 &&
         limits.maximum_total_transient_result_bytes > 0 &&
         limits.maximum_total_transient_result_bytes <= kMaximumCpuCandidateAllocationWorkItemsV1;
}

[[nodiscard]] bool SameAssociations(const AllocationAssociations& left,
                                    const AllocationAssociations& right) noexcept {
  return left == right;
}

[[nodiscard]] AllocationAssociations AssociationsFor(const MultiNetWorkload& workload) noexcept {
  return AllocationAssociations{
      .board_content_hash = workload.board_content_hash(),
      .compiler_profile_fingerprint = workload.compiler_profile_fingerprint(),
      .geometry_compiler_version = workload.geometry_compiler_version(),
  };
}

[[nodiscard]] bool SameCandidateSemantics(const candidates::StoredCandidate& left,
                                          const candidates::StoredCandidate& right) noexcept {
  if (left == nullptr || right == nullptr) {
    return left == right;
  }
  const candidates::GeneratedRouteCandidate& a = left->data();
  const candidates::GeneratedRouteCandidate& b = right->data();
  const candidates::CandidateMetrics& am = a.metrics;
  const candidates::CandidateMetrics& bm = b.metrics;
  return a.net == b.net && a.intended_terminals == b.intended_terminals &&
         a.schema_major == b.schema_major && a.schema_minor == b.schema_minor &&
         a.geometry_schema_version == b.geometry_schema_version &&
         a.resource_schema_version == b.resource_schema_version &&
         a.associations == b.associations && a.geometry == b.geometry &&
         a.resources == b.resources && am.intrinsic_base_cost == bm.intrinsic_base_cost &&
         am.orthogonal_step_count == bm.orthogonal_step_count &&
         am.diagonal_step_count == bm.diagonal_step_count && am.bend_count == bm.bend_count &&
         am.line_primitive_count == bm.line_primitive_count && am.via_count == bm.via_count &&
         am.axis_aligned_length_dbu == bm.axis_aligned_length_dbu &&
         am.diagonal_projection_dbu == bm.diagonal_projection_dbu && a.constraints == b.constraints;
}

[[nodiscard]] bool SameSelectedRouteSemantics(const OneWorldAllocation& left,
                                              const OneWorldAllocation& right) noexcept {
  if (left.selections.size() != right.selections.size()) {
    return false;
  }
  for (std::size_t index = 0; index < left.selections.size(); ++index) {
    const NetSelection& lhs = left.selections[index];
    const NetSelection& rhs = right.selections[index];
    if (lhs.net != rhs.net || lhs.status != rhs.status ||
        static_cast<bool>(lhs.candidate) != static_cast<bool>(rhs.candidate)) {
      return false;
    }
    if (!SameCandidateSemantics(lhs.candidate, rhs.candidate)) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool CompletePriceValuesEqual(const NegotiatedPriceState& left,
                                            const NegotiatedPriceState& right) noexcept {
  return left.prices() == right.prices();
}

[[nodiscard]] bool IsFeasible(const OneWorldAllocation& world) noexcept {
  return world.no_candidate_net_count == 0 && world.total_overuse_units == 0;
}

[[nodiscard]] const RetainedMultiWorld* PreferredWorld(
    const MultiWorldExecution& execution) noexcept {
  if (!execution.preferred_world_identity().has_value()) {
    return nullptr;
  }
  const auto found =
      std::ranges::find(execution.retained_worlds(), *execution.preferred_world_identity(),
                        &RetainedMultiWorld::world_identity);
  return found == execution.retained_worlds().end() ? nullptr : &*found;
}

[[nodiscard]] bool AddFits(std::uint64_t current, std::uint64_t addition,
                           std::uint64_t maximum) noexcept {
  return static_cast<UWide>(current) + addition <= maximum;
}

void AddEntity(board_ir::StableHashBuilder& hash, board_ir::EntityRef entity) noexcept {
  hash.AddU64(entity.id);
  hash.AddU32(entity.generation);
}

void AddCandidateId(board_ir::StableHashBuilder& hash, const candidates::CandidateId& id) noexcept {
  hash.AddU64(id.high);
  hash.AddU64(id.low);
}

void AddAssociations(board_ir::StableHashBuilder& hash,
                     const candidates::CandidateAssociations& associations) noexcept {
  hash.AddU64(associations.board_content_hash);
  hash.AddU64(associations.compiler_profile_fingerprint);
  hash.AddU32(associations.geometry_compiler_version);
  hash.AddU64(associations.routing_profile_fingerprint);
  hash.AddU64(associations.rule_bucket_identity);
}

void AddProvenance(board_ir::StableHashBuilder& hash,
                   const candidates::CandidateProvenance& provenance) noexcept {
  hash.AddByte(static_cast<std::uint8_t>(provenance.generator));
  hash.AddU32(provenance.generator_version);
  hash.AddByte(static_cast<std::uint8_t>(provenance.backend));
  hash.AddString(provenance.supported_device_class);
  hash.AddU64(provenance.deterministic_seed);
  hash.AddU64(provenance.batch_identity);
  hash.AddU64(provenance.query_identity);
  hash.AddU32(provenance.candidate_ordinal);
}

void AddOptionalU64(board_ir::StableHashBuilder& hash,
                    std::optional<std::uint64_t> value) noexcept {
  hash.AddBool(value.has_value());
  if (value.has_value()) {
    hash.AddU64(*value);
  }
}

void AddRejection(board_ir::StableHashBuilder& hash,
                  const candidates::CandidateRejection& rejection) noexcept {
  hash.AddU32(rejection.schema_version);
  hash.AddBool(rejection.candidate_id.has_value());
  if (rejection.candidate_id.has_value()) {
    AddCandidateId(hash, *rejection.candidate_id);
  }
  hash.AddBool(rejection.net.has_value());
  if (rejection.net.has_value()) {
    AddEntity(hash, *rejection.net);
  }
  hash.AddByte(static_cast<std::uint8_t>(rejection.stage));
  hash.AddByte(static_cast<std::uint8_t>(rejection.code));
  hash.AddString(rejection.invariant_id);
  AddAssociations(hash, rejection.associations);
  hash.AddU64(rejection.policy_identity);
  AddProvenance(hash, rejection.provenance);
  AddOptionalU64(hash, rejection.primitive_witness_index);
  AddOptionalU64(hash, rejection.resource_witness_index);
  AddOptionalU64(hash, rejection.expected_value);
  AddOptionalU64(hash, rejection.actual_value);
  hash.AddBool(rejection.conflicting_entity.has_value());
  if (rejection.conflicting_entity.has_value()) {
    AddEntity(hash, *rejection.conflicting_entity);
  }
  AddOptionalU64(hash, rejection.candidate_payload_checksum);
  hash.AddString(rejection.detail);
  hash.AddU64(rejection.logical_bytes);
}

void AddSessionConfig(board_ir::StableHashBuilder& hash,
                      const CpuCandidateAllocationSessionConfig& config) noexcept {
  hash.AddU32(config.schema_version);
  hash.AddU64(config.intrinsic_cost_weight);
  hash.AddU32(config.maximum_regeneration_epochs);
  hash.AddU64(config.price_config.present_step_per_overuse_unit);
  hash.AddU64(config.price_config.history_step_per_overuse_unit);
  hash.AddU64(config.price_config.maximum_price_per_resource);
  hash.AddU32(config.price_config.maximum_iterations);
  hash.AddU64(config.price_config.maximum_price_records);
  hash.AddU64(config.allocator_limits.maximum_nets);
  hash.AddU64(config.allocator_limits.maximum_candidates);
  hash.AddU64(config.allocator_limits.maximum_resource_records);
  hash.AddU64(config.allocator_limits.maximum_expanded_resource_uses);
  hash.AddU64(config.regeneration_plan_config.maximum_target_nets);
  hash.AddU64(config.regeneration_plan_config.maximum_columns_per_net);
  hash.AddU64(config.regeneration_plan_config.maximum_total_columns);
  hash.AddU64(config.regeneration_plan_config.maximum_resource_actions_per_net);
  hash.AddU64(config.regeneration_plan_config.maximum_total_resource_actions);
  hash.AddU64(config.regeneration_plan_config.maximum_expanded_resource_visits);
  const TargetedRegenerationExecutionConfig& execution = config.regeneration_execution_config;
  hash.AddU64(execution.maximum_route_queries);
  hash.AddU64(execution.route_limits.maximum_work_units);
  hash.AddU64(execution.route_limits.maximum_record_count);
  hash.AddU64(execution.route_limits.maximum_queue_size);
  hash.AddU64(execution.route_limits.maximum_reconstruction_states);
  hash.AddU64(execution.maximum_total_route_work_units);
  hash.AddU64(execution.maximum_policy_projection_visits);
  hash.AddU64(execution.maximum_policy_resource_entries);
  hash.AddU64(execution.maximum_candidate_draft_bytes);
  hash.AddU64(execution.maximum_generated_candidate_bytes);
  hash.AddU64(execution.maximum_rejection_bytes);
  hash.AddU64(execution.maximum_transient_result_bytes);
  hash.AddU64(execution.known_unmapped_exact_conflict_count);
  hash.AddU64(config.schedules.size());
  for (const MultiWorldSchedule& schedule : config.schedules) {
    hash.AddU64(schedule.schedule_key);
    hash.AddU64(schedule.search_intrinsic_cost_weight);
    hash.AddU32(schedule.maximum_selection_rounds);
  }
  const MultiWorldExecutionConfig& worlds = config.multi_world_config;
  hash.AddU64(worlds.maximum_worlds);
  hash.AddU64(worlds.maximum_total_selection_rounds);
  hash.AddU64(worlds.maximum_total_candidate_evaluations);
  hash.AddU64(worlds.maximum_total_candidate_span_visits);
  hash.AddU64(worlds.maximum_total_resource_work_units);
  hash.AddU64(worlds.maximum_total_net_outcomes);
  hash.AddU64(worlds.maximum_pareto_comparisons);
  hash.AddU64(worlds.maximum_buffered_terminal_selection_records);
  hash.AddU64(worlds.maximum_buffered_terminal_resource_records);
  hash.AddU64(worlds.maximum_buffered_terminal_price_records);
  hash.AddU64(worlds.maximum_retained_worlds);
  hash.AddU64(worlds.maximum_retained_selection_records);
  hash.AddU64(worlds.maximum_retained_resource_records);
  hash.AddU64(worlds.maximum_retained_price_records);
  hash.AddU64(worlds.maximum_retained_winner_pins);
  hash.AddU64(worlds.maximum_near_feasible_missing_nets);
  hash.AddU64(worlds.maximum_near_feasible_overuse_units);
  hash.AddU64(worlds.known_unmapped_exact_conflict_count);
  const CpuCandidateAllocationSessionLimits& limits = config.limits;
  hash.AddU64(limits.maximum_epoch_records);
  hash.AddU64(limits.maximum_total_planning_expanded_resource_visits);
  hash.AddU64(limits.maximum_total_requested_columns);
  hash.AddU64(limits.maximum_total_route_queries);
  hash.AddU64(limits.maximum_total_route_work_units);
  hash.AddU64(limits.maximum_total_policy_projection_visits);
  hash.AddU64(limits.maximum_total_generated_candidate_bytes);
  hash.AddU64(limits.maximum_total_rejection_bytes);
  hash.AddU64(limits.maximum_total_transient_result_bytes);
  hash.AddU64(config.known_unmapped_exact_conflict_count);
}

}  // namespace

internal::CpuCandidatePoolComparisonV1 internal::CompareCpuCandidatePoolSemanticsV1(
    std::span<const CandidatePool> source, std::span<const CandidatePool> successor,
    std::uint64_t source_manifest_checksum, std::uint64_t successor_manifest_checksum) noexcept {
  bool values_equal = source.size() == successor.size();
  for (std::size_t pool_index = 0; values_equal && pool_index < source.size(); ++pool_index) {
    const CandidatePool& left_pool = source[pool_index];
    const CandidatePool& right_pool = successor[pool_index];
    values_equal = left_pool.net == right_pool.net &&
                   left_pool.candidates.size() == right_pool.candidates.size();
    for (std::size_t candidate_index = 0;
         values_equal && candidate_index < left_pool.candidates.size(); ++candidate_index) {
      const candidates::StoredCandidate& left = left_pool.candidates[candidate_index];
      const candidates::StoredCandidate& right = right_pool.candidates[candidate_index];
      values_equal = left != nullptr && right != nullptr && *left == *right;
    }
  }
  const bool manifests_equal = source_manifest_checksum == successor_manifest_checksum;
  if (values_equal != manifests_equal) {
    return CpuCandidatePoolComparisonV1::kManifestCollisionOrDrift;
  }
  return values_equal ? CpuCandidatePoolComparisonV1::kUnchanged
                      : CpuCandidatePoolComparisonV1::kChanged;
}

bool internal::CpuCandidateAllocationFixedPointV1(CpuCandidatePoolComparisonV1 pool_comparison,
                                                  bool selected_route_semantics_equal,
                                                  bool complete_price_values_equal) noexcept {
  return pool_comparison == CpuCandidatePoolComparisonV1::kUnchanged &&
         selected_route_semantics_equal && complete_price_values_equal;
}

CpuCandidateAllocationTerminalReason internal::ResolveCpuCandidateAllocationTerminalReasonV1(
    CpuCandidateAllocationTerminalReason current, bool resource_refinement_required,
    bool preferred_world_feasible) noexcept {
  if (resource_refinement_required) {
    return CpuCandidateAllocationTerminalReason::kResourceRefinementRequired;
  }
  return preferred_world_feasible ? CpuCandidateAllocationTerminalReason::kFeasible : current;
}

void internal::SetCpuCandidateAllocationFinalAssemblyFailureForTesting(bool enabled) noexcept {
  g_final_assembly_failure_for_testing = enabled;
}

std::uint64_t internal::CpuCandidateAllocationSourceSpanInspectionsForTesting() noexcept {
  return g_source_span_inspections_for_testing;
}

std::uint64_t internal::CpuCandidateAllocationEpochReservationForTesting() noexcept {
  return g_epoch_reservation_for_testing;
}

std::optional<internal::CpuCandidateAllocationSessionEnvelopeV1>
internal::ProjectCpuCandidateAllocationSessionEnvelopeV1(
    std::uint64_t source_pool_count, std::uint64_t source_candidate_count,
    std::uint64_t source_expanded_resource_uses,
    const candidates::CandidateStoreConfig& store_config,
    const CpuCandidateAllocationSessionConfig& config) noexcept {
  const UWide epochs =
      config.known_unmapped_exact_conflict_count == 0 ? config.maximum_regeneration_epochs : 0;
  const UWide columns_per_epoch = config.regeneration_plan_config.maximum_total_columns;
  const UWide maximum_columns = epochs * columns_per_epoch;
  const UWide maximum_route_work =
      maximum_columns * config.regeneration_execution_config.route_limits.maximum_work_units;
  const UWide maximum_planning_visits =
      epochs * config.regeneration_plan_config.maximum_expanded_resource_visits;
  const UWide maximum_policy_visits = epochs * config.regeneration_plan_config.maximum_target_nets *
                                      config.price_config.maximum_price_records *
                                      kTargetedRegenerationPolicyProjectionPassesV2;
  const UWide maximum_generated_bytes =
      maximum_columns * config.regeneration_execution_config.maximum_candidate_draft_bytes;
  const UWide maximum_rejection_bytes =
      maximum_columns * kMaximumTargetedRegenerationRejectionLogicalBytesV2;
  const UWide maximum_transient_bytes =
      maximum_columns * (kTargetedRegenerationColumnBaseLogicalBytesV2 +
                         2U * kMaximumTargetedRegenerationRejectionLogicalBytesV2);
  const UWide retained_candidate_cap =
      static_cast<UWide>(source_pool_count) * store_config.maximum_candidates_per_net;
  const UWide generated_candidate_cap = source_candidate_count + maximum_columns;
  const UWide maximum_final_candidates = std::min(retained_candidate_cap, generated_candidate_cap);
  const UWide maximum_final_expanded_uses =
      static_cast<UWide>(source_expanded_resource_uses) +
      maximum_columns *
          config.regeneration_execution_config.route_limits.maximum_reconstruction_states;
  constexpr UWide kMax = std::numeric_limits<std::uint64_t>::max();
  if (maximum_columns > kMax || maximum_route_work > kMax || maximum_planning_visits > kMax ||
      maximum_policy_visits > kMax || maximum_generated_bytes > kMax ||
      maximum_rejection_bytes > kMax || maximum_transient_bytes > kMax ||
      maximum_final_candidates > kMax || maximum_final_expanded_uses > kMax) {
    return std::nullopt;
  }
  return CpuCandidateAllocationSessionEnvelopeV1{
      .maximum_requested_columns = static_cast<std::uint64_t>(maximum_columns),
      .maximum_route_work_units = static_cast<std::uint64_t>(maximum_route_work),
      .maximum_planning_expanded_resource_visits =
          static_cast<std::uint64_t>(maximum_planning_visits),
      .maximum_policy_projection_visits = static_cast<std::uint64_t>(maximum_policy_visits),
      .maximum_generated_candidate_bytes = static_cast<std::uint64_t>(maximum_generated_bytes),
      .maximum_rejection_bytes = static_cast<std::uint64_t>(maximum_rejection_bytes),
      .maximum_transient_result_bytes = static_cast<std::uint64_t>(maximum_transient_bytes),
      .maximum_final_candidate_count = static_cast<std::uint64_t>(maximum_final_candidates),
      .maximum_final_expanded_resource_uses =
          static_cast<std::uint64_t>(maximum_final_expanded_uses),
  };
}

std::uint64_t internal::ComputeCpuCandidateAllocationRejectionManifestChecksumV1(
    std::span<const candidates::CandidateRejection> rejections) noexcept {
  board_ir::StableHashBuilder hash;
  hash.AddString("APGAR-CPU-CANDIDATE-ALLOCATION-REJECTIONS-V1");
  hash.AddU64(rejections.size());
  for (const candidates::CandidateRejection& rejection : rejections) {
    AddRejection(hash, rejection);
  }
  return hash.Finish();
}

std::uint64_t internal::ComputeCpuCandidateAllocationSessionChecksumV1(
    const CpuCandidateAllocationSessionConfig& config, std::uint64_t board_content_hash,
    std::uint64_t workload_checksum, std::uint64_t capacity_model_checksum,
    std::uint64_t preparation_checksum, CpuCandidateAllocationTerminalReason terminal_reason,
    const CpuCandidateAllocationSessionCounters& counters,
    std::span<const CpuCandidateAllocationEpochRecord> epochs,
    std::uint64_t final_pool_manifest_checksum, std::uint64_t final_rejection_manifest_checksum,
    std::uint64_t final_price_state_checksum, std::uint64_t final_single_world_checksum,
    std::uint64_t final_multi_world_checksum) noexcept {
  board_ir::StableHashBuilder hash;
  hash.AddString("APGAR-CPU-CANDIDATE-ALLOCATION-SESSION-V1");
  AddSessionConfig(hash, config);
  hash.AddU64(board_content_hash);
  hash.AddU64(workload_checksum);
  hash.AddU64(capacity_model_checksum);
  hash.AddU64(preparation_checksum);
  hash.AddByte(static_cast<std::uint8_t>(terminal_reason));
  hash.AddU64(counters.completed_regeneration_epochs);
  hash.AddU64(counters.planning_expanded_resource_visits);
  hash.AddU64(counters.requested_columns);
  hash.AddU64(counters.route_queries);
  hash.AddU64(counters.route_work_units);
  hash.AddU64(counters.policy_projection_visits);
  hash.AddU64(counters.generated_candidate_bytes);
  hash.AddU64(counters.rejection_record_bytes);
  hash.AddU64(counters.transient_result_bytes);
  hash.AddU64(counters.admitted_candidates);
  hash.AddU64(counters.duplicate_candidates);
  hash.AddU64(counters.rejected_columns);
  hash.AddU64(counters.novel_retained_candidates);
  hash.AddU64(counters.changed_selections);
  hash.AddU64(epochs.size());
  for (const CpuCandidateAllocationEpochRecord& epoch : epochs) {
    hash.AddU32(epoch.epoch_index);
    hash.AddU64(epoch.source_pool_manifest_checksum);
    hash.AddU64(epoch.successor_pool_manifest_checksum);
    hash.AddU64(epoch.source_world_checksum);
    hash.AddU64(epoch.successor_world_checksum);
    hash.AddU64(epoch.successor_price_state_checksum);
    hash.AddU64(epoch.plan_checksum);
    hash.AddU64(epoch.execution_checksum);
    hash.AddByte(static_cast<std::uint8_t>(epoch.disposition));
    hash.AddByte(static_cast<std::uint8_t>(epoch.terminal_reason));
    hash.AddU64(epoch.counters.requested_columns);
    hash.AddU64(epoch.counters.route_queries);
    hash.AddU64(epoch.counters.route_work_units);
    hash.AddU64(epoch.counters.policy_projection_visits);
    hash.AddU64(epoch.counters.peak_route_record_count);
    hash.AddU64(epoch.counters.peak_route_queue_size);
    hash.AddU64(epoch.counters.generated_candidate_bytes);
    hash.AddU64(epoch.counters.rejection_record_bytes);
    hash.AddU64(epoch.counters.transient_result_bytes);
    hash.AddU64(epoch.counters.successful_routes);
    hash.AddU64(epoch.counters.built_candidates);
    hash.AddU64(epoch.counters.admitted_candidates);
    hash.AddU64(epoch.counters.duplicate_candidates);
    hash.AddU64(epoch.counters.rejected_columns);
    hash.AddU64(epoch.counters.novel_retained_candidates);
    hash.AddU64(epoch.counters.changed_selections);
    hash.AddU64(epoch.counters.successor_pinned_candidates);
    hash.AddU64(epoch.columns.size());
    for (const TargetedRegenerationColumnRecord& column : epoch.columns) {
      AddEntity(hash, column.net);
      hash.AddU64(column.column_index);
      hash.AddU64(column.policy_identity);
      hash.AddU64(column.batch_identity);
      hash.AddU64(column.query_identity);
      hash.AddBool(column.route_telemetry.has_value());
      if (column.route_telemetry.has_value()) {
        hash.AddU64(column.route_telemetry->queue_pops);
        hash.AddU64(column.route_telemetry->expanded_states);
        hash.AddU64(column.route_telemetry->attempted_relaxations);
        hash.AddU64(column.route_telemetry->accepted_relaxations);
        hash.AddU64(column.route_telemetry->peak_record_count);
        hash.AddU64(column.route_telemetry->peak_queue_size);
        hash.AddU64(column.route_telemetry->work_units);
      }
      hash.AddU64(column.candidate_draft_logical_bytes);
      hash.AddByte(static_cast<std::uint8_t>(column.outcome));
      hash.AddBool(column.candidate_id.has_value());
      if (column.candidate_id.has_value()) {
        AddCandidateId(hash, *column.candidate_id);
      }
      AddOptionalU64(hash, column.candidate_payload_checksum);
      hash.AddBool(column.rejection_code.has_value());
      if (column.rejection_code.has_value()) {
        hash.AddByte(static_cast<std::uint8_t>(*column.rejection_code));
      }
      hash.AddBool(column.rejection.has_value());
      if (column.rejection.has_value()) {
        AddRejection(hash, *column.rejection);
      }
    }
    hash.AddBool(epoch.pool_manifest_changed);
    hash.AddBool(epoch.selected_route_roster_changed);
    hash.AddBool(epoch.complete_price_values_changed);
    hash.AddBool(epoch.fixed_point);
  }
  hash.AddU64(final_pool_manifest_checksum);
  hash.AddU64(final_rejection_manifest_checksum);
  hash.AddU64(final_price_state_checksum);
  hash.AddU64(final_single_world_checksum);
  hash.AddU64(final_multi_world_checksum);
  return hash.Finish();
}

namespace {

struct SessionSourceShape {
  std::uint64_t pool_count = 0;
  std::uint64_t candidate_count = 0;
  std::uint64_t candidate_span_count = 0;
  std::uint64_t expanded_resource_uses = 0;
  std::uint64_t selected_resource_work_upper_bound = 0;
};

struct SessionSourceCounts {
  std::uint64_t pool_count = 0;
  std::uint64_t candidate_count = 0;
};

[[nodiscard]] std::optional<CpuCandidateAllocationSessionError> ValidateSessionScalarConfiguration(
    std::uint32_t schema_version, const CpuCandidateAllocationSessionConfig& config) noexcept {
  if (schema_version != kCpuCandidateAllocationSessionSchemaVersion ||
      config.schema_version != kCpuCandidateAllocationSessionSchemaVersion) {
    return Error(CpuCandidateAllocationSessionErrorCode::kUnsupportedSchema,
                 "allocator.cpu_candidate_session.schema.v1",
                 "CPU candidate-allocation session schema is unsupported");
  }
  if (config.intrinsic_cost_weight == 0 || config.maximum_regeneration_epochs == 0 ||
      config.maximum_regeneration_epochs > kMaximumCpuCandidateAllocationEpochsV1 ||
      config.maximum_regeneration_epochs > config.price_config.maximum_iterations ||
      config.maximum_regeneration_epochs > config.limits.maximum_epoch_records ||
      !LimitsAreValid(config.limits) ||
      !internal::NegotiatedPriceConfigIsValidV1(config.price_config) ||
      !internal::OneWorldAllocatorLimitsAreValidV1(config.allocator_limits) ||
      !internal::TargetedRegenerationConfigIsValidV1(config.regeneration_plan_config) ||
      !internal::TargetedRegenerationExecutionConfigIsValidV2(
          config.regeneration_execution_config) ||
      !internal::MultiWorldExecutionConfigIsValidV1(config.multi_world_config) ||
      config.schedules.empty() ||
      config.schedules.size() > config.multi_world_config.maximum_worlds ||
      config.schedules.size() > kMaximumMultiWorldsV1 ||
      config.regeneration_execution_config.known_unmapped_exact_conflict_count != 0 ||
      config.multi_world_config.known_unmapped_exact_conflict_count != 0) {
    return Error(CpuCandidateAllocationSessionErrorCode::kInvalidConfiguration,
                 "allocator.cpu_candidate_session.configuration.v1",
                 "CPU candidate-allocation session configuration is inconsistent or unbounded");
  }

  bool has_anchor = false;
  UWide total_rounds = 0;
  std::uint32_t maximum_schedule_updates = 0;
  for (const MultiWorldSchedule& schedule : config.schedules) {
    if (schedule.schedule_key == 0 || schedule.search_intrinsic_cost_weight == 0 ||
        schedule.maximum_selection_rounds == 0) {
      return Error(CpuCandidateAllocationSessionErrorCode::kInvalidConfiguration,
                   "allocator.cpu_candidate_session.schedule_value.v1",
                   "Terminal Multi-World schedule values must be positive");
    }
    total_rounds += schedule.maximum_selection_rounds;
    maximum_schedule_updates =
        std::max(maximum_schedule_updates, schedule.maximum_selection_rounds - 1U);
    has_anchor =
        has_anchor || (schedule.search_intrinsic_cost_weight == config.intrinsic_cost_weight &&
                       schedule.maximum_selection_rounds == 1);
  }
  const bool refinement = config.known_unmapped_exact_conflict_count != 0;
  const UWide world_count = refinement ? 0 : config.schedules.size();
  const UWide charged_rounds = refinement ? 0 : total_rounds;
  const UWide pareto_comparisons = world_count * (world_count - 1U) / 2U;
  if (charged_rounds > std::numeric_limits<std::uint64_t>::max() ||
      charged_rounds > config.multi_world_config.maximum_total_selection_rounds ||
      pareto_comparisons > config.multi_world_config.maximum_pareto_comparisons) {
    return Error(CpuCandidateAllocationSessionErrorCode::kWorkBoundExceeded,
                 "allocator.cpu_candidate_session.schedule_work_bound.v1",
                 "Terminal schedule rounds or Pareto comparisons exceed a configured bound");
  }
  if (!has_anchor ||
      static_cast<UWide>(config.maximum_regeneration_epochs) + maximum_schedule_updates >
          config.price_config.maximum_iterations) {
    return Error(CpuCandidateAllocationSessionErrorCode::kInvalidConfiguration,
                 "allocator.cpu_candidate_session.schedule_anchor.v1",
                 "Schedules omit the common-weight one-round anchor or exceed bounded rounds");
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<CpuCandidateAllocationSessionError> ValidateCanonicalSessionSchedules(
    const CpuCandidateAllocationSessionConfig& config) {
  for (std::size_t index = 1; index < config.schedules.size(); ++index) {
    if (config.schedules[index - 1].schedule_key == config.schedules[index].schedule_key) {
      return Error(CpuCandidateAllocationSessionErrorCode::kInvalidConfiguration,
                   "allocator.cpu_candidate_session.schedule.v1",
                   "Terminal Multi-World schedules have a duplicate key");
    }
  }
  std::vector<std::pair<std::uint64_t, std::uint32_t>> search_shapes;
  search_shapes.reserve(config.schedules.size());
  for (const MultiWorldSchedule& schedule : config.schedules) {
    search_shapes.emplace_back(schedule.search_intrinsic_cost_weight,
                               schedule.maximum_selection_rounds);
  }
  std::ranges::sort(search_shapes);
  if (std::ranges::adjacent_find(search_shapes) != search_shapes.end()) {
    return Error(CpuCandidateAllocationSessionErrorCode::kInvalidConfiguration,
                 "allocator.cpu_candidate_session.schedule.v1",
                 "Terminal Multi-World schedules have a duplicate search shape");
  }
  return std::nullopt;
}

[[nodiscard]] std::variant<SessionSourceCounts, CpuCandidateAllocationSessionError>
CountSessionSourceShape(const PreparedCpuCandidatePools& prepared,
                        const CpuCandidateAllocationSessionConfig& config) {
  const candidates::CandidateStoreConfig& store = prepared.candidate_store().config();
  if (prepared.pools().size() > config.allocator_limits.maximum_nets ||
      prepared.pools().size() > store.maximum_expected_pools_per_invocation) {
    return Error(CpuCandidateAllocationSessionErrorCode::kWorkBoundExceeded,
                 "allocator.cpu_candidate_session.source_pool_bound.v1",
                 "Prepared source pool count exceeds a configured bound");
  }
  UWide candidate_count = 0;
  for (const CandidatePool& pool : prepared.pools()) {
    candidate_count += pool.candidates.size();
    if (candidate_count > config.allocator_limits.maximum_candidates ||
        candidate_count > store.maximum_expected_candidates_per_invocation ||
        candidate_count > store.maximum_pin_lease_items_per_transaction) {
      return Error(CpuCandidateAllocationSessionErrorCode::kWorkBoundExceeded,
                   "allocator.cpu_candidate_session.source_candidate_bound.v1",
                   "Prepared source candidate count exceeds a configured bound");
    }
    for (const candidates::StoredCandidate& candidate : pool.candidates) {
      if (candidate == nullptr) {
        return Error(CpuCandidateAllocationSessionErrorCode::kAssociationMismatch,
                     "allocator.cpu_candidate_session.source_candidate.v1",
                     "A prepared source pool contains a null candidate");
      }
    }
  }
  if (candidate_count > std::numeric_limits<std::uint64_t>::max()) {
    return Error(CpuCandidateAllocationSessionErrorCode::kWorkBoundExceeded,
                 "allocator.cpu_candidate_session.source_shape.v1",
                 "Prepared source pool accounting overflows unsigned 64-bit fields");
  }
  return SessionSourceCounts{
      .pool_count = prepared.pools().size(),
      .candidate_count = static_cast<std::uint64_t>(candidate_count),
  };
}

[[nodiscard]] std::variant<SessionSourceShape, CpuCandidateAllocationSessionError>
InspectSessionSourceShape(const PreparedCpuCandidatePools& prepared,
                          const CpuCandidateAllocationSessionConfig& config,
                          const SessionSourceCounts& counts) {
  const auto envelope = internal::ProjectCpuCandidateAllocationSessionEnvelopeV1(
      counts.pool_count, counts.candidate_count, 0, prepared.candidate_store().config(), config);
  if (!envelope.has_value()) {
    return Error(CpuCandidateAllocationSessionErrorCode::kWorkBoundExceeded,
                 "allocator.cpu_candidate_session.aggregate_overflow.v1",
                 "Aggregate candidate-allocation session envelope overflows");
  }
  UWide total_rounds = 0;
  for (const MultiWorldSchedule& schedule : config.schedules) {
    total_rounds += schedule.maximum_selection_rounds;
  }
  const bool refinement = config.known_unmapped_exact_conflict_count != 0;
  const UWide world_count = refinement ? 0 : config.schedules.size();
  const UWide rounds = refinement ? 0 : total_rounds;
  const UWide charged_passes = 1U + rounds + (rounds - world_count);
  const UWide span_quota =
      config.multi_world_config.maximum_total_candidate_span_visits / charged_passes;
  const UWide resource_quota =
      config.multi_world_config.maximum_total_resource_work_units / charged_passes;
  const UWide future_spans = envelope->maximum_final_expanded_resource_uses;
  UWide selected_upper = config.allocator_limits.maximum_resource_records;
  const UWide regeneration_candidate_footprint =
      refinement ? 0
                 : config.regeneration_execution_config.route_limits.maximum_reconstruction_states;
  selected_upper += static_cast<UWide>(counts.pool_count) * regeneration_candidate_footprint;
  if (future_spans > span_quota ||
      future_spans > config.allocator_limits.maximum_expanded_resource_uses ||
      selected_upper > resource_quota) {
    return Error(CpuCandidateAllocationSessionErrorCode::kWorkBoundExceeded,
                 "allocator.cpu_candidate_session.source_reserved_work_bound.v1",
                 "Reserved regeneration or base resource work exhausts a per-pass quota");
  }

  UWide source_span_count = 0;
  UWide source_expanded_uses = 0;
  for (const CandidatePool& pool : prepared.pools()) {
    UWide pool_greatest_candidate_edges = regeneration_candidate_footprint;
    for (const candidates::StoredCandidate& candidate : pool.candidates) {
      UWide candidate_edges = 0;
      for (const candidates::PhysicalEdgeSpan& span : candidate->data().resources) {
        ++g_source_span_inspections_for_testing;
        ++source_span_count;
        if (future_spans + source_span_count > span_quota) {
          return Error(CpuCandidateAllocationSessionErrorCode::kWorkBoundExceeded,
                       "allocator.cpu_candidate_session.source_span_bound.v1",
                       "Prepared source candidate spans exceed a charged-pass quota");
        }
        candidate_edges += span.edge_count;
        source_expanded_uses += span.edge_count;
        if (future_spans + source_expanded_uses >
            config.allocator_limits.maximum_expanded_resource_uses) {
          return Error(CpuCandidateAllocationSessionErrorCode::kWorkBoundExceeded,
                       "allocator.cpu_candidate_session.source_expanded_bound.v1",
                       "Prepared source footprints exceed the One-World expanded-use bound");
        }
        if (candidate_edges > pool_greatest_candidate_edges) {
          selected_upper += candidate_edges - pool_greatest_candidate_edges;
          pool_greatest_candidate_edges = candidate_edges;
          if (selected_upper > resource_quota) {
            return Error(CpuCandidateAllocationSessionErrorCode::kWorkBoundExceeded,
                         "allocator.cpu_candidate_session.source_resource_work_bound.v1",
                         "Prepared source resource work exceeds a charged-pass quota");
          }
        }
      }
    }
  }
  constexpr UWide kMax = std::numeric_limits<std::uint64_t>::max();
  if (source_span_count > kMax || source_expanded_uses > kMax || selected_upper > kMax) {
    return Error(CpuCandidateAllocationSessionErrorCode::kWorkBoundExceeded,
                 "allocator.cpu_candidate_session.source_shape.v1",
                 "Prepared source pool accounting overflows unsigned 64-bit fields");
  }
  return SessionSourceShape{
      .pool_count = counts.pool_count,
      .candidate_count = counts.candidate_count,
      .candidate_span_count = static_cast<std::uint64_t>(source_span_count),
      .expanded_resource_uses = static_cast<std::uint64_t>(source_expanded_uses),
      .selected_resource_work_upper_bound = static_cast<std::uint64_t>(selected_upper),
  };
}

[[nodiscard]] std::optional<CpuCandidateAllocationSessionError> ValidateSessionConfiguration(
    const CpuCandidateAllocationSessionConfig& config, const board_ir::BoardSnapshot& board,
    const ResourceCapacityModel& capacities, const PreparedCpuCandidatePools& prepared,
    const SessionSourceShape& source) {
  UWide total_rounds = 0;
  for (const MultiWorldSchedule& schedule : config.schedules) {
    total_rounds += schedule.maximum_selection_rounds;
  }

  const std::optional<internal::CpuCandidateAllocationSessionEnvelopeV1> envelope =
      internal::ProjectCpuCandidateAllocationSessionEnvelopeV1(
          source.pool_count, source.candidate_count, source.expanded_resource_uses,
          prepared.candidate_store().config(), config);
  if (!envelope.has_value()) {
    return Error(CpuCandidateAllocationSessionErrorCode::kWorkBoundExceeded,
                 "allocator.cpu_candidate_session.aggregate_overflow.v1",
                 "Aggregate candidate-allocation session envelope overflows");
  }
  const CpuCandidateAllocationSessionLimits& limits = config.limits;
  if (envelope->maximum_requested_columns > limits.maximum_total_requested_columns ||
      envelope->maximum_requested_columns > limits.maximum_total_route_queries ||
      envelope->maximum_route_work_units > limits.maximum_total_route_work_units ||
      envelope->maximum_planning_expanded_resource_visits >
          limits.maximum_total_planning_expanded_resource_visits ||
      envelope->maximum_policy_projection_visits > limits.maximum_total_policy_projection_visits ||
      envelope->maximum_generated_candidate_bytes >
          limits.maximum_total_generated_candidate_bytes ||
      envelope->maximum_rejection_bytes > limits.maximum_total_rejection_bytes ||
      envelope->maximum_transient_result_bytes > limits.maximum_total_transient_result_bytes ||
      envelope->maximum_final_candidate_count > config.allocator_limits.maximum_candidates ||
      envelope->maximum_final_expanded_resource_uses >
          config.allocator_limits.maximum_expanded_resource_uses) {
    return Error(CpuCandidateAllocationSessionErrorCode::kWorkBoundExceeded,
                 "allocator.cpu_candidate_session.aggregate_bound.v1",
                 "Aggregate regeneration or final-pool envelope exceeds a session bound");
  }
  const candidates::CandidateStoreConfig& store = prepared.candidate_store().config();
  if (source.pool_count > config.allocator_limits.maximum_nets ||
      source.pool_count > store.maximum_expected_pools_per_invocation ||
      envelope->maximum_final_candidate_count > store.maximum_expected_candidates_per_invocation ||
      envelope->maximum_final_candidate_count > store.maximum_pin_lease_items_per_transaction) {
    return Error(
        CpuCandidateAllocationSessionErrorCode::kWorkBoundExceeded,
        "allocator.cpu_candidate_session.store_snapshot_bound.v1",
        "Worst-case frozen pool does not fit allocator or CandidateStore transaction bounds");
  }
  const UWide regeneration_epochs =
      config.known_unmapped_exact_conflict_count == 0 ? config.maximum_regeneration_epochs : 0;
  const UWide maximum_retained_rejections =
      static_cast<UWide>(prepared.candidate_store().RejectionCount()) +
      regeneration_epochs * (envelope->maximum_final_candidate_count +
                             config.regeneration_plan_config.maximum_total_columns);
  if (maximum_retained_rejections > store.maximum_rejection_records) {
    return Error(CpuCandidateAllocationSessionErrorCode::kWorkBoundExceeded,
                 "allocator.cpu_candidate_session.store_rejection_bound.v1",
                 "Worst-case regeneration diagnostics exceed CandidateStore retention bounds");
  }

  if (regeneration_epochs != 0) {
    const UWide columns_per_epoch = config.regeneration_plan_config.maximum_total_columns;
    const UWide route_work_per_epoch =
        columns_per_epoch * config.regeneration_execution_config.route_limits.maximum_work_units;
    const UWide policy_visits_per_epoch =
        static_cast<UWide>(config.regeneration_plan_config.maximum_target_nets) *
        config.price_config.maximum_price_records * kTargetedRegenerationPolicyProjectionPassesV2;
    const UWide generated_bytes_per_epoch =
        columns_per_epoch * config.regeneration_execution_config.maximum_candidate_draft_bytes;
    const UWide rejection_bytes_per_epoch =
        columns_per_epoch * kMaximumTargetedRegenerationRejectionLogicalBytesV2;
    const UWide transient_bytes_per_epoch =
        columns_per_epoch * (kTargetedRegenerationColumnBaseLogicalBytesV2 +
                             2U * kMaximumTargetedRegenerationRejectionLogicalBytesV2);
    const UWide maximum_projected_policy_entries = std::min<UWide>(
        config.regeneration_execution_config.maximum_policy_resource_entries,
        columns_per_epoch * static_cast<UWide>(config.price_config.maximum_price_records) +
            columns_per_epoch);
    const auto admission_input_per_epoch =
        internal::ComputeTargetedRegenerationAdmissionInputBytesV2(
            static_cast<std::uint64_t>(columns_per_epoch),
            config.regeneration_execution_config.maximum_candidate_draft_bytes,
            static_cast<std::uint64_t>(maximum_projected_policy_entries));
    const auto admission_work_per_epoch = internal::ComputeTargetedRegenerationAdmissionWorkV2(
        static_cast<std::uint64_t>(columns_per_epoch),
        config.regeneration_execution_config.route_limits.maximum_reconstruction_states,
        static_cast<std::uint64_t>(maximum_projected_policy_entries), board.data().obstacles.size(),
        board.data().terminals.size());
    if (columns_per_epoch > config.regeneration_execution_config.maximum_route_queries ||
        columns_per_epoch > store.maximum_admission_items_per_transaction ||
        columns_per_epoch > store.maximum_rejection_items_per_transaction ||
        route_work_per_epoch >
            config.regeneration_execution_config.maximum_total_route_work_units ||
        policy_visits_per_epoch >
            config.regeneration_execution_config.maximum_policy_projection_visits ||
        generated_bytes_per_epoch >
            config.regeneration_execution_config.maximum_generated_candidate_bytes ||
        rejection_bytes_per_epoch > config.regeneration_execution_config.maximum_rejection_bytes ||
        transient_bytes_per_epoch >
            config.regeneration_execution_config.maximum_transient_result_bytes ||
        !admission_input_per_epoch.has_value() ||
        *admission_input_per_epoch > store.maximum_admission_input_bytes_per_transaction ||
        !admission_work_per_epoch.has_value() ||
        *admission_work_per_epoch > store.maximum_admission_work_units_per_transaction) {
      return Error(CpuCandidateAllocationSessionErrorCode::kWorkBoundExceeded,
                   "allocator.cpu_candidate_session.component_epoch_bound.v1",
                   "One configured regeneration epoch exceeds a child execution bound");
    }
  }

  const UWide maximum_resource_input_records =
      static_cast<UWide>(capacities.overrides().size()) + config.price_config.maximum_price_records;
  if (maximum_resource_input_records > config.allocator_limits.maximum_resource_records) {
    return Error(CpuCandidateAllocationSessionErrorCode::kWorkBoundExceeded,
                 "allocator.cpu_candidate_session.resource_input_bound.v1",
                 "Worst-case capacity and price records exceed One-World input bounds");
  }

  const UWide maximum_span_count =
      static_cast<UWide>(source.candidate_span_count) +
      envelope->maximum_requested_columns *
          config.regeneration_execution_config.route_limits.maximum_reconstruction_states;
  if (maximum_span_count > std::numeric_limits<std::uint64_t>::max()) {
    return Error(CpuCandidateAllocationSessionErrorCode::kWorkBoundExceeded,
                 "allocator.cpu_candidate_session.multi_world_span_bound.v1",
                 "Worst-case frozen candidate spans overflow Multi-World accounting");
  }
  const bool refinement = config.known_unmapped_exact_conflict_count != 0;
  const std::uint64_t world_count =
      refinement ? 0 : static_cast<std::uint64_t>(config.schedules.size());
  const std::uint64_t rounds = refinement ? 0 : static_cast<std::uint64_t>(total_rounds);
  internal::MultiWorldKnownWorkProjectionV1 work_projection;
  if (!internal::MultiWorldKnownWorkFitsV1(
          internal::MultiWorldKnownWorkV1{
              .world_count = world_count,
              .total_selection_rounds = rounds,
              .total_price_updates = rounds - world_count,
              .source_net_count = source.pool_count,
              .source_candidate_count = envelope->maximum_final_candidate_count,
              .source_candidate_span_count = static_cast<std::uint64_t>(maximum_span_count),
              .source_resource_work_units_per_candidate_pass =
                  source.selected_resource_work_upper_bound,
          },
          config.multi_world_config, &work_projection)) {
    return Error(CpuCandidateAllocationSessionErrorCode::kWorkBoundExceeded,
                 "allocator.cpu_candidate_session.multi_world_bound.v1",
                 "Worst-case frozen pool does not fit terminal Multi-World work bounds");
  }
  internal::MultiWorldTerminalEnvelopeV1 terminal_projection;
  if (!internal::MultiWorldTerminalEnvelopeFitsV1(
          world_count, source.pool_count, envelope->maximum_final_candidate_count,
          source.selected_resource_work_upper_bound, config.price_config.maximum_price_records,
          config.multi_world_config, &terminal_projection)) {
    return Error(CpuCandidateAllocationSessionErrorCode::kWorkBoundExceeded,
                 "allocator.cpu_candidate_session.multi_world_terminal_bound.v1",
                 "Worst-case terminal worlds exceed a buffer or retention bound");
  }
  return std::nullopt;
}

[[nodiscard]] OneWorldAllocationRequest BuildSessionRequest(
    const MultiNetWorkload& workload, const ResourceCapacityModel& capacities,
    const NegotiatedPriceState& state, std::uint64_t intrinsic_cost_weight,
    const OneWorldAllocatorLimits& limits, const std::vector<CandidatePool>& pools) {
  NegotiatedPriceSnapshotResult snapshot = BuildPriceSnapshotForState(capacities, state);
  if (!std::holds_alternative<PriceSnapshot>(snapshot)) {
    throw std::logic_error("validated candidate-allocation price state cannot build a snapshot");
  }
  return OneWorldAllocationRequest{
      .schema_version = kOneWorldAllocationSchemaVersion,
      .associations = AssociationsFor(workload),
      .capacities = capacities,
      .prices = std::get<PriceSnapshot>(std::move(snapshot)),
      .intrinsic_cost_weight = intrinsic_cost_weight,
      .limits = limits,
      .pools = pools,
      .workload = &workload,
  };
}

[[nodiscard]] bool PlannedEpochFits(const CpuCandidateAllocationSessionCounters& counters,
                                    const TargetedRegenerationPlan& plan,
                                    const CpuCandidateAllocationSessionConfig& config) noexcept {
  const std::uint64_t columns = plan.total_requested_columns();
  const UWide route_work = static_cast<UWide>(columns) *
                           config.regeneration_execution_config.route_limits.maximum_work_units;
  const UWide policy_visits = static_cast<UWide>(plan.targets().size()) *
                              plan.price_state().prices().size() *
                              kTargetedRegenerationPolicyProjectionPassesV2;
  const auto generated = internal::ComputeTargetedRegenerationGeneratedBytesV2(
      columns, config.regeneration_execution_config.maximum_candidate_draft_bytes);
  const auto rejection = internal::ComputeTargetedRegenerationRejectionBytesV2(columns);
  const auto transient = internal::ComputeTargetedRegenerationTransientResultBytesV2(columns);
  return route_work <= std::numeric_limits<std::uint64_t>::max() &&
         policy_visits <= std::numeric_limits<std::uint64_t>::max() && generated.has_value() &&
         rejection.has_value() && transient.has_value() &&
         AddFits(counters.planning_expanded_resource_visits, plan.expanded_resource_visits(),
                 config.limits.maximum_total_planning_expanded_resource_visits) &&
         AddFits(counters.requested_columns, columns,
                 config.limits.maximum_total_requested_columns) &&
         AddFits(counters.route_queries, columns, config.limits.maximum_total_route_queries) &&
         AddFits(counters.route_work_units, static_cast<std::uint64_t>(route_work),
                 config.limits.maximum_total_route_work_units) &&
         AddFits(counters.policy_projection_visits, static_cast<std::uint64_t>(policy_visits),
                 config.limits.maximum_total_policy_projection_visits) &&
         AddFits(counters.generated_candidate_bytes, *generated,
                 config.limits.maximum_total_generated_candidate_bytes) &&
         AddFits(counters.rejection_record_bytes, *rejection,
                 config.limits.maximum_total_rejection_bytes) &&
         AddFits(counters.transient_result_bytes, *transient,
                 config.limits.maximum_total_transient_result_bytes);
}

void AddExecutionCounters(CpuCandidateAllocationSessionCounters& total,
                          const TargetedRegenerationPlan& plan,
                          const TargetedRegenerationExecutionCounters& epoch) noexcept {
  ++total.completed_regeneration_epochs;
  total.planning_expanded_resource_visits += plan.expanded_resource_visits();
  total.requested_columns += epoch.requested_columns;
  total.route_queries += epoch.route_queries;
  total.route_work_units += epoch.route_work_units;
  total.policy_projection_visits += epoch.policy_projection_visits;
  total.generated_candidate_bytes += epoch.generated_candidate_bytes;
  total.rejection_record_bytes += epoch.rejection_record_bytes;
  total.transient_result_bytes += epoch.transient_result_bytes;
  total.admitted_candidates += epoch.admitted_candidates;
  total.duplicate_candidates += epoch.duplicate_candidates;
  total.rejected_columns += epoch.rejected_columns;
  total.novel_retained_candidates += epoch.novel_retained_candidates;
  total.changed_selections += epoch.changed_selections;
}

}  // namespace

CpuCandidateAllocationSessionResult ExecuteCpuCandidateAllocationSession(
    std::uint32_t schema_version, board_ir::BoardSnapshot&& board, MultiNetWorkload&& workload,
    ResourceCapacityModel&& capacities, PreparedCpuCandidatePools&& prepared,
    const CpuCandidateAllocationSessionConfig& input_config) {
  bool candidate_store_publication_committed = false;
  g_source_span_inspections_for_testing = 0;
  g_epoch_reservation_for_testing = 0;
  try {
    if (std::optional<CpuCandidateAllocationSessionError> failure =
            ValidateSessionScalarConfiguration(schema_version, input_config);
        failure.has_value()) {
      return *failure;
    }
    CpuCandidateAllocationSessionConfig config = input_config;
    std::ranges::sort(config.schedules, {}, &MultiWorldSchedule::schedule_key);
    if (std::optional<CpuCandidateAllocationSessionError> failure =
            ValidateCanonicalSessionSchedules(config);
        failure.has_value()) {
      return *failure;
    }
    const AllocationAssociations expected_associations = AssociationsFor(workload);
    if (!prepared.has_candidate_store() || board.content_hash() != workload.board_content_hash() ||
        !SameAssociations(capacities.associations(), expected_associations) ||
        !prepared.candidate_store().valid() || prepared.pools().size() != workload.nets().size()) {
      return Error(
          CpuCandidateAllocationSessionErrorCode::kAssociationMismatch,
          "allocator.cpu_candidate_session.associations.v1",
          "Board, workload, capacity model, or prepared CandidateStore associations differ");
    }
    std::variant<SessionSourceCounts, CpuCandidateAllocationSessionError> count_result =
        CountSessionSourceShape(prepared, config);
    if (const auto* failure = std::get_if<CpuCandidateAllocationSessionError>(&count_result);
        failure != nullptr) {
      return *failure;
    }
    const SessionSourceCounts counts = std::get<SessionSourceCounts>(count_result);
    const SessionSourceShape provisional_source{
        .pool_count = counts.pool_count,
        .candidate_count = counts.candidate_count,
    };
    if (std::optional<CpuCandidateAllocationSessionError> failure =
            ValidateSessionConfiguration(config, board, capacities, prepared, provisional_source);
        failure.has_value()) {
      return *failure;
    }
    std::variant<SessionSourceShape, CpuCandidateAllocationSessionError> source_result =
        InspectSessionSourceShape(prepared, config, counts);
    if (const auto* failure = std::get_if<CpuCandidateAllocationSessionError>(&source_result);
        failure != nullptr) {
      return *failure;
    }
    const SessionSourceShape source = std::get<SessionSourceShape>(source_result);
    if (std::optional<CpuCandidateAllocationSessionError> failure =
            ValidateSessionConfiguration(config, board, capacities, prepared, source);
        failure.has_value()) {
      return *failure;
    }

    NegotiatedPriceStateResult initial_state_result = BuildInitialNegotiatedPriceState(
        kNegotiatedPriceStateSchemaVersion, capacities, workload, config.price_config);
    if (const auto* failure = std::get_if<NegotiatedPriceError>(&initial_state_result);
        failure != nullptr) {
      return Error(failure->code == NegotiatedPriceErrorCode::kResourceExhausted
                       ? CpuCandidateAllocationSessionErrorCode::kResourceExhausted
                       : CpuCandidateAllocationSessionErrorCode::kInitialPriceState,
                   failure->invariant_id, failure->detail);
    }
    NegotiatedPriceState current_state =
        std::get<NegotiatedPriceState>(std::move(initial_state_result));
    std::vector<CandidatePool> current_pools = prepared.pools();
    OneWorldAllocationRequest current_request =
        BuildSessionRequest(workload, capacities, current_state, config.intrinsic_cost_weight,
                            config.allocator_limits, current_pools);
    OneWorldAllocationResult initial_world_result = AllocateOneWorld(current_request);
    if (const auto* failure = std::get_if<AllocationError>(&initial_world_result);
        failure != nullptr) {
      return Error(failure->code == AllocationErrorCode::kResourceExhausted
                       ? CpuCandidateAllocationSessionErrorCode::kResourceExhausted
                       : CpuCandidateAllocationSessionErrorCode::kInitialAllocation,
                   failure->invariant_id, failure->detail);
    }
    OneWorldAllocation current_world =
        std::get<OneWorldAllocation>(std::move(initial_world_result));
    CpuCandidateAllocationSessionCounters counters;
    std::vector<CpuCandidateAllocationEpochRecord> epochs;
    CpuCandidateAllocationTerminalReason terminal_reason =
        CpuCandidateAllocationTerminalReason::kRegenerationEpochLimit;

    if (config.known_unmapped_exact_conflict_count != 0) {
      terminal_reason = CpuCandidateAllocationTerminalReason::kResourceRefinementRequired;
    } else if (IsFeasible(current_world)) {
      terminal_reason = CpuCandidateAllocationTerminalReason::kFeasible;
    } else {
      g_epoch_reservation_for_testing = config.maximum_regeneration_epochs;
      epochs.reserve(config.maximum_regeneration_epochs);
      for (std::uint32_t epoch_index = 0; epoch_index < config.maximum_regeneration_epochs;
           ++epoch_index) {
        TargetedRegenerationPlanResult plan_result = BuildTargetedRegenerationPlan(
            kTargetedRegenerationPlanSchemaVersion, current_state, current_request, current_world,
            prepared.candidate_store(), config.regeneration_plan_config);
        if (const auto* failure = std::get_if<TargetedRegenerationError>(&plan_result);
            failure != nullptr) {
          return WithPublicationStatus(
              Error(failure->code == TargetedRegenerationErrorCode::kResourceExhausted
                        ? CpuCandidateAllocationSessionErrorCode::kResourceExhausted
                        : CpuCandidateAllocationSessionErrorCode::kTargetedRegenerationPlan,
                    failure->invariant_id, failure->detail, epoch_index),
              candidate_store_publication_committed);
        }
        TargetedRegenerationPlan plan = std::get<TargetedRegenerationPlan>(std::move(plan_result));
        if (!PlannedEpochFits(counters, plan, config)) {
          return WithPublicationStatus(
              Error(CpuCandidateAllocationSessionErrorCode::kWorkBoundExceeded,
                    "allocator.cpu_candidate_session.epoch_bound.v1",
                    "The next exact regeneration plan exceeds a cumulative session bound",
                    epoch_index),
              candidate_store_publication_committed);
        }
        TargetedRegenerationExecutionConfig execution_config = config.regeneration_execution_config;
        execution_config.known_unmapped_exact_conflict_count =
            config.known_unmapped_exact_conflict_count;
        TargetedRegenerationExecutionResult execution_result = ExecuteTargetedRegenerationPlanCpu(
            kTargetedRegenerationExecutionSchemaVersion, board, current_request, std::move(plan),
            prepared.candidate_store(), execution_config);
        if (const auto* failure =
                std::get_if<TargetedRegenerationExecutionError>(&execution_result);
            failure != nullptr) {
          TargetedRegenerationExecutionError child_failure =
              std::get<TargetedRegenerationExecutionError>(std::move(execution_result));
          candidate_store_publication_committed =
              candidate_store_publication_committed ||
              (child_failure.failed_execution.has_value() &&
               child_failure.failed_execution->candidate_store_publication_committed);
          return Error(
              child_failure.code == TargetedRegenerationExecutionErrorCode::kResourceExhausted
                  ? CpuCandidateAllocationSessionErrorCode::kResourceExhausted
                  : CpuCandidateAllocationSessionErrorCode::kTargetedRegenerationExecution,
              child_failure.invariant_id, child_failure.detail, epoch_index,
              std::move(child_failure.failed_execution), candidate_store_publication_committed);
        }
        TargetedRegenerationExecution execution =
            std::get<TargetedRegenerationExecution>(std::move(execution_result));
        candidate_store_publication_committed =
            candidate_store_publication_committed || execution.counters().requested_columns != 0;
        const NegotiatedPriceState successor_state = execution.plan().price_state();
        std::vector<CandidatePool> successor_pools = execution.refreshed_pools();
        const OneWorldAllocation successor_world = execution.refreshed_world();
        OneWorldAllocationRequest successor_request =
            BuildSessionRequest(workload, capacities, successor_state, config.intrinsic_cost_weight,
                                config.allocator_limits, successor_pools);
        internal::OneWorldSelectionEvidenceResult evidence_result =
            internal::SelectOneWorldWithoutAccounting(successor_request);
        if (!std::holds_alternative<internal::OneWorldSelectionEvidence>(evidence_result)) {
          return WithPublicationStatus(
              Error(CpuCandidateAllocationSessionErrorCode::kInternalInvariant,
                    "allocator.cpu_candidate_session.successor_manifest.v1",
                    "Targeted execution successor pools cannot reproduce their manifest",
                    epoch_index),
              candidate_store_publication_committed);
        }
        const internal::OneWorldSelectionEvidence& evidence =
            std::get<internal::OneWorldSelectionEvidence>(evidence_result);
        const internal::CpuCandidatePoolComparisonV1 pool_comparison =
            internal::CompareCpuCandidatePoolSemanticsV1(
                current_pools, successor_pools, execution.plan().candidate_pool_manifest_checksum(),
                evidence.candidate_pool_manifest_checksum);
        if (pool_comparison == internal::CpuCandidatePoolComparisonV1::kManifestCollisionOrDrift) {
          return WithPublicationStatus(
              Error(CpuCandidateAllocationSessionErrorCode::kInternalInvariant,
                    "allocator.cpu_candidate_session.pool_manifest_collision.v1",
                    "Typed candidate pools disagree with their manifest equality", epoch_index),
              candidate_store_publication_committed);
        }
        const bool pool_changed =
            pool_comparison == internal::CpuCandidatePoolComparisonV1::kChanged;
        const bool selection_changed = !SameSelectedRouteSemantics(current_world, successor_world);
        const bool price_changed = !CompletePriceValuesEqual(current_state, successor_state);
        const bool fixed_point = internal::CpuCandidateAllocationFixedPointV1(
            pool_comparison, !selection_changed, !price_changed);
        AddExecutionCounters(counters, execution.plan(), execution.counters());
        epochs.push_back(CpuCandidateAllocationEpochRecord{
            .epoch_index = epoch_index,
            .source_pool_manifest_checksum = execution.plan().candidate_pool_manifest_checksum(),
            .successor_pool_manifest_checksum = evidence.candidate_pool_manifest_checksum,
            .source_world_checksum = current_world.world_checksum,
            .successor_world_checksum = successor_world.world_checksum,
            .successor_price_state_checksum = successor_state.state_checksum(),
            .plan_checksum = execution.plan().plan_checksum(),
            .execution_checksum = execution.execution_checksum(),
            .disposition = execution.disposition(),
            .terminal_reason = execution.terminal_reason(),
            .counters = execution.counters(),
            .columns = execution.columns(),
            .pool_manifest_changed = pool_changed,
            .selected_route_roster_changed = selection_changed,
            .complete_price_values_changed = price_changed,
            .fixed_point = fixed_point,
        });
        current_state = successor_state;
        current_pools = std::move(successor_pools);
        current_world = successor_world;
        current_request = std::move(successor_request);
        if (execution.disposition() ==
            TargetedRegenerationExecutionDisposition::kResourceRefinementRequired) {
          terminal_reason = CpuCandidateAllocationTerminalReason::kResourceRefinementRequired;
          break;
        }
        if (IsFeasible(current_world)) {
          terminal_reason = CpuCandidateAllocationTerminalReason::kFeasible;
          break;
        }
        if (fixed_point) {
          terminal_reason = CpuCandidateAllocationTerminalReason::kFixedPoint;
          break;
        }
      }
    }

    MultiWorldExecutionConfig multi_world_config = config.multi_world_config;
    multi_world_config.known_unmapped_exact_conflict_count =
        config.known_unmapped_exact_conflict_count;
    MultiWorldPoolSnapshot frozen{
        .capacities = capacities,
        .allocator_limits = config.allocator_limits,
        .pools = current_pools,
        .workload = &workload,
    };
    MultiWorldExecutionResult worlds_result =
        ExecuteMultiWorldCpu(kMultiWorldExecutionSchemaVersion, frozen, current_state,
                             config.schedules, prepared.candidate_store(), multi_world_config);
    if (const auto* failure = std::get_if<MultiWorldExecutionError>(&worlds_result);
        failure != nullptr) {
      return WithPublicationStatus(
          Error(failure->code == MultiWorldExecutionErrorCode::kResourceExhausted
                    ? CpuCandidateAllocationSessionErrorCode::kResourceExhausted
                    : CpuCandidateAllocationSessionErrorCode::kMultiWorldExecution,
                failure->invariant_id, failure->detail),
          candidate_store_publication_committed);
    }
    MultiWorldExecution final_worlds = std::get<MultiWorldExecution>(std::move(worlds_result));
    const RetainedMultiWorld* preferred = PreferredWorld(final_worlds);
    terminal_reason = internal::ResolveCpuCandidateAllocationTerminalReasonV1(
        terminal_reason, config.known_unmapped_exact_conflict_count != 0,
        preferred != nullptr && IsFeasible(preferred->world));

    internal::OneWorldSelectionEvidenceResult final_evidence_result =
        internal::SelectOneWorldWithoutAccounting(current_request);
    if (!std::holds_alternative<internal::OneWorldSelectionEvidence>(final_evidence_result)) {
      return WithPublicationStatus(
          Error(CpuCandidateAllocationSessionErrorCode::kInternalInvariant,
                "allocator.cpu_candidate_session.final_manifest.v1",
                "Frozen final pools cannot reproduce their canonical manifest"),
          candidate_store_publication_committed);
    }
    const std::uint64_t final_pool_manifest =
        std::get<internal::OneWorldSelectionEvidence>(final_evidence_result)
            .candidate_pool_manifest_checksum;
    const std::vector<candidates::CandidateRejection> final_rejections =
        prepared.candidate_store().Rejections();
    const std::uint64_t rejection_manifest =
        internal::ComputeCpuCandidateAllocationRejectionManifestChecksumV1(final_rejections);
    const std::uint64_t session_checksum = internal::ComputeCpuCandidateAllocationSessionChecksumV1(
        config, board.content_hash(), workload.workload_checksum(),
        current_state.capacity_model_checksum(), prepared.preparation_checksum(), terminal_reason,
        counters, epochs, final_pool_manifest, rejection_manifest, current_state.state_checksum(),
        current_world.world_checksum, final_worlds.execution_checksum());

    if (g_final_assembly_failure_for_testing) {
      g_final_assembly_failure_for_testing = false;
      throw std::bad_alloc();
    }
    return CpuCandidateAllocationSession(
        std::move(board), std::move(workload), std::move(capacities), std::move(prepared),
        std::move(config), terminal_reason, counters, std::move(epochs), std::move(current_pools),
        std::move(current_state), std::move(current_world), final_pool_manifest, rejection_manifest,
        session_checksum, std::move(final_worlds));
  } catch (const std::bad_alloc&) {
    return Error(CpuCandidateAllocationSessionErrorCode::kResourceExhausted,
                 "allocator.cpu_candidate_session.resource_exhausted.v1",
                 "CPU candidate-allocation session exhausted host memory", std::nullopt,
                 std::nullopt, candidate_store_publication_committed);
  } catch (const std::length_error&) {
    return Error(CpuCandidateAllocationSessionErrorCode::kResourceExhausted,
                 "allocator.cpu_candidate_session.resource_exhausted.v1",
                 "CPU candidate-allocation session exceeded a host container limit", std::nullopt,
                 std::nullopt, candidate_store_publication_committed);
  } catch (const std::out_of_range&) {
    return Error(CpuCandidateAllocationSessionErrorCode::kResourceExhausted,
                 "allocator.cpu_candidate_session.resource_exhausted.v1",
                 "CPU candidate-allocation session exceeded a host container range", std::nullopt,
                 std::nullopt, candidate_store_publication_committed);
  } catch (const std::logic_error&) {
    return Error(CpuCandidateAllocationSessionErrorCode::kInternalInvariant,
                 "allocator.cpu_candidate_session.internal.v1",
                 "CPU candidate-allocation session violated an internal invariant", std::nullopt,
                 std::nullopt, candidate_store_publication_committed);
  }
}

}  // namespace apgar::allocator
