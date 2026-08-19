#include "apgar/allocator/cpu_candidate_allocation_session.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <optional>
#include <ranges>
#include <set>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

#include "apgar/board_ir/stable_hash.h"
#include "apgar/candidates/route_candidate.h"
#include "apgar/routing/cpu_astar.h"
#include "apgar/routing/planar_route.h"
#include "src/allocator/cpu_candidate_allocation_session_internal.h"

namespace apgar::allocator {
namespace {

using UWide = __uint128_t;

[[nodiscard]] auto NetKey(board_ir::EntityRef net) noexcept {
  return std::pair{net.id, net.generation};
}

[[nodiscard]] CpuCandidateAllocationSessionError Error(
    CpuCandidateAllocationSessionErrorCode code, std::string_view invariant_id,
    std::string_view detail, std::optional<board_ir::EntityRef> net = std::nullopt) noexcept {
  return CpuCandidateAllocationSessionError{
      .code = code,
      .invariant_id = invariant_id,
      .detail = detail,
      .net = net,
      .expected_value = std::nullopt,
      .actual_value = std::nullopt,
      .plan_error = std::nullopt,
      .epoch_error = std::nullopt,
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

[[nodiscard]] bool SelectionLimitsAreValid(const OneWorldSelectionLimits& limits) noexcept {
  return limits.maximum_net_pools > 0 && limits.maximum_net_pools <= kMaximumOneWorldNetPools &&
         limits.maximum_total_candidates > 0 &&
         limits.maximum_total_candidates <= kMaximumOneWorldPoolCandidates &&
         limits.accounting.maximum_candidates > 0 &&
         limits.accounting.maximum_candidates <= kMaximumResourceAccountingCandidates &&
         limits.accounting.maximum_expanded_resource_uses > 0 &&
         limits.accounting.maximum_expanded_resource_uses <=
             kMaximumResourceAccountingExpandedUses &&
         limits.accounting.maximum_usage_units_per_resource > 0;
}

[[nodiscard]] bool PlanningConfigIsValid(const NegotiatedRegenerationPlanConfig& config) noexcept {
  const NegotiatedRegenerationPlanLimits& limits = config.limits;
  return config.price_policy.present_factor_increment > 0 &&
         config.price_policy.historical_price_increment > 0 &&
         SelectionLimitsAreValid(limits.selection) &&
         limits.maximum_epoch_index <= kMaximumNegotiatedPriceEpoch &&
         limits.maximum_price_entries > 0 &&
         limits.maximum_price_entries <= kMaximumNegotiatedPriceEntries &&
         limits.maximum_hot_resources > 0 &&
         limits.maximum_hot_resources <= kMaximumNegotiatedHotResources &&
         limits.maximum_targets > 0 &&
         limits.maximum_targets <= kMaximumNegotiatedRegenerationTargets &&
         limits.maximum_selected_resource_uses_per_net > 0 &&
         limits.maximum_selected_resource_uses_per_net <=
             candidates::kMaximumCandidateExpandedResourceEdges &&
         limits.maximum_aggregate_selected_resource_uses > 0 &&
         limits.maximum_aggregate_selected_resource_uses <=
             kMaximumResourceAccountingExpandedUses &&
         limits.maximum_hot_resources_per_target > 0 &&
         limits.maximum_hot_resources_per_target <= kMaximumNegotiatedHotResources &&
         limits.maximum_aggregate_target_resource_links > 0 &&
         limits.maximum_aggregate_target_resource_links <= kMaximumNegotiatedTargetResourceLinks &&
         limits.maximum_price_value > 0 && limits.maximum_aggregate_price > 0 &&
         limits.maximum_target_price_per_net > 0 && limits.maximum_aggregate_target_price > 0;
}

[[nodiscard]] bool StoreConfigIsValid(const candidates::CandidateStoreConfig& config) noexcept {
  return config.maximum_candidates_per_net > 0 && config.maximum_candidate_bytes_per_net > 0 &&
         config.maximum_rejection_records > 0 &&
         config.maximum_rejection_items_per_transaction > 0 &&
         config.maximum_admission_items_per_transaction > 0 &&
         config.maximum_admission_input_bytes_per_transaction > 0 &&
         config.maximum_admission_work_units_per_transaction > 0;
}

[[nodiscard]] bool EpochConfigIsValid(const CpuTargetedRegenerationEpochConfig& config) noexcept {
  const CpuTargetedRegenerationEpochLimits& limits = config.limits;
  return PlanningConfigIsValid(config.planning) && limits.maximum_nets > 0 &&
         limits.maximum_nets <= kMaximumCpuTargetedEpochNets &&
         limits.maximum_source_candidates > 0 &&
         limits.maximum_source_candidates <= kMaximumCpuTargetedEpochSourceCandidates &&
         limits.maximum_source_candidate_bytes > 0 && limits.maximum_targets > 0 &&
         limits.maximum_targets <= kMaximumCpuTargetedEpochTargets &&
         limits.maximum_price_projection_visits > 0 &&
         limits.maximum_price_projection_visits <= kMaximumCpuTargetedEpochPriceProjectionVisits &&
         limits.maximum_price_entries_per_target > 0 &&
         limits.maximum_price_entries_per_target <= routing::kMaximumPolicyResourceEntries &&
         limits.maximum_aggregate_price_entries > 0 &&
         limits.maximum_aggregate_price_entries <= kMaximumCpuTargetedEpochPriceProjectionVisits &&
         limits.maximum_cpu_work_units_per_query > 0 &&
         limits.maximum_aggregate_cpu_work_units > 0 &&
         limits.maximum_generated_bytes_per_column > 0 &&
         limits.maximum_aggregate_generated_bytes > 0 &&
         limits.maximum_retained_candidate_bytes > 0 &&
         StoreConfigIsValid(limits.candidate_store) &&
         SelectionLimitsAreValid(limits.refreshed_selection);
}

[[nodiscard]] bool SessionConfigIsValid(
    const CpuCandidateAllocationSessionConfig& config) noexcept {
  const CpuCandidateAllocationSessionLimits& limits = config.limits;
  return EpochConfigIsValid(config.epoch) && limits.maximum_executed_epochs > 0 &&
         limits.maximum_executed_epochs <= kMaximumCpuCandidateAllocationSessionEpochs &&
         limits.maximum_cumulative_source_candidate_visits > 0 &&
         limits.maximum_cumulative_source_candidate_visits <=
             kMaximumCpuCandidateAllocationSessionCandidateVisits &&
         limits.maximum_cumulative_source_candidate_bytes > 0 &&
         limits.maximum_cumulative_source_candidate_bytes <=
             kMaximumCpuCandidateAllocationSessionBytes &&
         limits.maximum_cumulative_targets > 0 &&
         limits.maximum_cumulative_targets <= kMaximumCpuCandidateAllocationSessionTargets &&
         limits.maximum_cumulative_price_projection_visits > 0 &&
         limits.maximum_cumulative_price_projection_visits <=
             kMaximumCpuCandidateAllocationSessionPriceProjectionVisits &&
         limits.maximum_reserved_cpu_work_units > 0 &&
         limits.maximum_reserved_cpu_work_units <= kMaximumCpuCandidateAllocationSessionWorkUnits &&
         limits.maximum_reserved_generated_bytes > 0 &&
         limits.maximum_reserved_generated_bytes <= kMaximumCpuCandidateAllocationSessionBytes &&
         limits.maximum_cumulative_transaction_items > 0 &&
         limits.maximum_cumulative_transaction_items <=
             kMaximumCpuCandidateAllocationSessionTransactionItems;
}

[[nodiscard]] bool MatchesLattice(const geometry_compiler::CompiledBoard& compiled,
                                  const ResourceCapacityModel& capacities) noexcept {
  return compiled.source_board_content_hash() == capacities.associations().board_content_hash &&
         compiled.compiler_profile_fingerprint() ==
             capacities.associations().compiler_profile_fingerprint &&
         compiled.compiler_version() == capacities.associations().geometry_compiler_version;
}

void HashCandidateId(board_ir::StableHashBuilder* hash,
                     candidates::CandidateId candidate_id) noexcept {
  hash->AddU64(candidate_id.high);
  hash->AddU64(candidate_id.low);
}

void HashResource(board_ir::StableHashBuilder* hash,
                  const routing::EdgeResourceKey& resource) noexcept {
  hash->AddU32(resource.layer);
  hash->AddI64(resource.lattice_x);
  hash->AddI64(resource.lattice_y);
  hash->AddU32(static_cast<std::uint32_t>(resource.direction));
}

[[nodiscard]] std::uint64_t NonzeroHash(board_ir::StableHashBuilder* hash) noexcept {
  const std::uint64_t value = hash->Finish();
  return value == 0 ? 1 : value;
}

void HashSelection(board_ir::StableHashBuilder* hash, const OneWorldSelection& selection) noexcept {
  hash->AddU64(selection.associations.board_content_hash);
  hash->AddU64(selection.associations.compiler_profile_fingerprint);
  hash->AddU32(selection.associations.geometry_compiler_version);
  hash->AddU64(selection.input_candidate_count);
  hash->AddU64(static_cast<std::uint64_t>(selection.nets.size()));
  for (const OneWorldNetOutcome& outcome : selection.nets) {
    if (const auto* selected = std::get_if<OneWorldSelectedCandidate>(&outcome);
        selected != nullptr) {
      hash->AddBool(true);
      hash->AddU64(selected->net.id);
      hash->AddU32(selected->net.generation);
      HashCandidateId(hash, selected->candidate_id);
    } else {
      const OneWorldCandidateAbsence& absence = std::get<OneWorldCandidateAbsence>(outcome);
      hash->AddBool(false);
      hash->AddU64(absence.net.id);
      hash->AddU32(absence.net.generation);
      hash->AddU32(static_cast<std::uint32_t>(absence.reason));
    }
  }
  const ResourceAccounting& accounting = selection.accounting;
  hash->AddU64(accounting.associations.board_content_hash);
  hash->AddU64(accounting.associations.compiler_profile_fingerprint);
  hash->AddU32(accounting.associations.geometry_compiler_version);
  hash->AddU64(accounting.candidate_count);
  hash->AddU64(accounting.expanded_resource_uses);
  hash->AddU64(accounting.overused_resource_count);
  hash->AddU64(accounting.total_overuse_units);
  hash->AddU64(static_cast<std::uint64_t>(accounting.resources.size()));
  for (const ResourceUsage& usage : accounting.resources) {
    HashResource(hash, usage.resource);
    hash->AddU32(usage.capacity_units);
    hash->AddU64(usage.usage_units);
    hash->AddU64(usage.overuse_units);
  }
}

void HashPlanningConfig(board_ir::StableHashBuilder* hash,
                        const NegotiatedRegenerationPlanConfig& config) noexcept {
  hash->AddU64(config.price_policy.initial_present_factor);
  hash->AddU64(config.price_policy.present_factor_increment);
  hash->AddU64(config.price_policy.historical_price_increment);
  const NegotiatedRegenerationPlanLimits& limits = config.limits;
  hash->AddU64(limits.selection.maximum_net_pools);
  hash->AddU64(limits.selection.maximum_total_candidates);
  hash->AddU64(limits.selection.accounting.maximum_candidates);
  hash->AddU64(limits.selection.accounting.maximum_expanded_resource_uses);
  hash->AddU64(limits.selection.accounting.maximum_usage_units_per_resource);
  hash->AddU64(limits.maximum_epoch_index);
  hash->AddU64(limits.maximum_price_entries);
  hash->AddU64(limits.maximum_hot_resources);
  hash->AddU64(limits.maximum_targets);
  hash->AddU64(limits.maximum_selected_resource_uses_per_net);
  hash->AddU64(limits.maximum_aggregate_selected_resource_uses);
  hash->AddU64(limits.maximum_hot_resources_per_target);
  hash->AddU64(limits.maximum_aggregate_target_resource_links);
  hash->AddU64(limits.maximum_price_value);
  hash->AddU64(limits.maximum_aggregate_price);
  hash->AddU64(limits.maximum_target_price_per_net);
  hash->AddU64(limits.maximum_aggregate_target_price);
}

void HashStoreConfig(board_ir::StableHashBuilder* hash,
                     const candidates::CandidateStoreConfig& config) noexcept {
  hash->AddU64(config.maximum_candidates_per_net);
  hash->AddU64(config.maximum_candidate_bytes_per_net);
  hash->AddU64(config.maximum_rejection_records);
  hash->AddU64(config.maximum_rejection_items_per_transaction);
  hash->AddU64(config.maximum_admission_items_per_transaction);
  hash->AddU64(config.maximum_admission_input_bytes_per_transaction);
  hash->AddU64(config.maximum_admission_work_units_per_transaction);
}

void HashSelectionLimits(board_ir::StableHashBuilder* hash,
                         const OneWorldSelectionLimits& limits) noexcept {
  hash->AddU64(limits.maximum_net_pools);
  hash->AddU64(limits.maximum_total_candidates);
  hash->AddU64(limits.accounting.maximum_candidates);
  hash->AddU64(limits.accounting.maximum_expanded_resource_uses);
  hash->AddU64(limits.accounting.maximum_usage_units_per_resource);
}

void HashConfig(board_ir::StableHashBuilder* hash,
                const CpuCandidateAllocationSessionConfig& config) noexcept {
  HashPlanningConfig(hash, config.epoch.planning);
  const CpuTargetedRegenerationEpochLimits& epoch = config.epoch.limits;
  hash->AddU64(epoch.maximum_nets);
  hash->AddU64(epoch.maximum_source_candidates);
  hash->AddU64(epoch.maximum_source_candidate_bytes);
  hash->AddU64(epoch.maximum_targets);
  hash->AddU64(epoch.maximum_price_projection_visits);
  hash->AddU64(epoch.maximum_price_entries_per_target);
  hash->AddU64(epoch.maximum_aggregate_price_entries);
  hash->AddU64(epoch.maximum_cpu_work_units_per_query);
  hash->AddU64(epoch.maximum_aggregate_cpu_work_units);
  hash->AddU64(epoch.maximum_generated_bytes_per_column);
  hash->AddU64(epoch.maximum_aggregate_generated_bytes);
  hash->AddU64(epoch.maximum_retained_candidate_bytes);
  HashStoreConfig(hash, epoch.candidate_store);
  HashSelectionLimits(hash, epoch.refreshed_selection);
  const CpuCandidateAllocationSessionLimits& limits = config.limits;
  hash->AddU64(limits.maximum_executed_epochs);
  hash->AddU64(limits.maximum_cumulative_source_candidate_visits);
  hash->AddU64(limits.maximum_cumulative_source_candidate_bytes);
  hash->AddU64(limits.maximum_cumulative_targets);
  hash->AddU64(limits.maximum_cumulative_price_projection_visits);
  hash->AddU64(limits.maximum_reserved_cpu_work_units);
  hash->AddU64(limits.maximum_reserved_generated_bytes);
  hash->AddU64(limits.maximum_cumulative_transaction_items);
}

[[nodiscard]] std::uint64_t PoolIdentity(std::span<const CpuCandidateAllocationPool> pools) {
  board_ir::StableHashBuilder hash;
  hash.AddString("APGAR-P4R08-CPU-CANDIDATE-ALLOCATION-POOLS-V1");
  hash.AddU64(static_cast<std::uint64_t>(pools.size()));
  for (const CpuCandidateAllocationPool& pool : pools) {
    hash.AddU64(pool.net().id);
    hash.AddU32(pool.net().generation);
    hash.AddU64(static_cast<std::uint64_t>(pool.candidates().size()));
    for (const candidates::StoredCandidate& candidate : pool.candidates()) {
      HashCandidateId(&hash, candidate->id());
      hash.AddU64(candidate->data().payload_checksum);
      hash.AddU64(candidate->logical_bytes());
    }
  }
  return NonzeroHash(&hash);
}

[[nodiscard]] std::uint64_t SessionIdentity(
    const CpuCandidateAllocationSession& session,
    const CpuCandidateAllocationSessionConfig& config) noexcept {
  board_ir::StableHashBuilder hash;
  hash.AddString("APGAR-P4R08-CPU-CANDIDATE-ALLOCATION-SESSION-V1");
  HashConfig(&hash, config);
  hash.AddU64(session.final_selection().associations.board_content_hash);
  hash.AddU64(session.final_selection().associations.compiler_profile_fingerprint);
  hash.AddU32(session.final_selection().associations.geometry_compiler_version);
  hash.AddU64(session.initial_pool_identity());
  hash.AddU64(session.final_pool_identity());
  hash.AddU64(static_cast<std::uint64_t>(session.steps().size()));
  for (const CpuCandidateAllocationStep& step : session.steps()) {
    hash.AddU64(step.input_pool_identity);
    hash.AddU64(step.plan_identity);
    hash.AddU64(step.prior_snapshot_identity);
    hash.AddU64(step.price_snapshot_identity);
    hash.AddU64(step.price_epoch_index);
    hash.AddU32(static_cast<std::uint32_t>(step.disposition));
    hash.AddU64(step.target_count);
    hash.AddBool(step.batch_identity.has_value());
    if (step.batch_identity.has_value()) {
      hash.AddU64(*step.batch_identity);
    }
    hash.AddBool(step.epoch_identity.has_value());
    if (step.epoch_identity.has_value()) {
      hash.AddU64(*step.epoch_identity);
    }
    hash.AddBool(step.output_pool_identity.has_value());
    if (step.output_pool_identity.has_value()) {
      hash.AddU64(*step.output_pool_identity);
    }
    const CpuTargetedRegenerationEpochCounters& epoch = step.epoch_counters;
    hash.AddU64(epoch.source_candidate_count);
    hash.AddU64(epoch.source_candidate_bytes);
    hash.AddU64(epoch.target_count);
    hash.AddU64(epoch.route_query_count);
    hash.AddU64(epoch.price_projection_visits);
    hash.AddU64(epoch.projected_price_entries);
    hash.AddU64(epoch.cpu_work_units);
    hash.AddU64(epoch.generated_bytes);
    hash.AddU64(epoch.admitted_columns);
    hash.AddU64(epoch.duplicate_columns);
    hash.AddU64(epoch.rejected_columns);
    hash.AddU64(epoch.retained_candidate_count);
    hash.AddU64(epoch.retained_candidate_bytes);
  }
  const CpuCandidateAllocationSessionCounters& total = session.counters();
  hash.AddU64(total.planning_steps);
  hash.AddU64(total.executed_epochs);
  hash.AddU64(total.source_candidate_visits);
  hash.AddU64(total.source_candidate_bytes);
  hash.AddU64(total.target_count);
  hash.AddU64(total.route_query_count);
  hash.AddU64(total.price_projection_visits);
  hash.AddU64(total.reserved_cpu_work_units);
  hash.AddU64(total.actual_cpu_work_units);
  hash.AddU64(total.reserved_generated_bytes);
  hash.AddU64(total.actual_generated_bytes);
  hash.AddU64(total.transaction_items);
  hash.AddU64(total.admitted_columns);
  hash.AddU64(total.duplicate_columns);
  hash.AddU64(total.rejected_columns);
  hash.AddU32(static_cast<std::uint32_t>(session.stop().reason));
  hash.AddString(session.stop().invariant_id);
  hash.AddString(session.stop().detail);
  hash.AddBool(session.stop().expected_value.has_value());
  if (session.stop().expected_value.has_value()) {
    hash.AddU64(*session.stop().expected_value);
  }
  hash.AddBool(session.stop().actual_value.has_value());
  if (session.stop().actual_value.has_value()) {
    hash.AddU64(*session.stop().actual_value);
  }
  hash.AddBool(session.stop().plan_error_code.has_value());
  if (session.stop().plan_error_code.has_value()) {
    hash.AddU32(static_cast<std::uint32_t>(*session.stop().plan_error_code));
  }
  hash.AddBool(session.stop().epoch_error_code.has_value());
  if (session.stop().epoch_error_code.has_value()) {
    hash.AddU32(static_cast<std::uint32_t>(*session.stop().epoch_error_code));
  }
  hash.AddBool(session.terminal_plan().has_value());
  if (session.terminal_plan().has_value()) {
    hash.AddU64(session.terminal_plan()->plan_identity);
  }
  HashSelection(&hash, session.final_selection());
  return NonzeroHash(&hash);
}

[[nodiscard]] CpuCandidateAllocationSessionError PlanningError(
    const NegotiatedRegenerationPlanError& plan_error) {
  CpuCandidateAllocationSessionError error =
      Error(CpuCandidateAllocationSessionErrorCode::kPlanningFailure, plan_error.invariant_id,
            plan_error.detail, plan_error.net);
  error.expected_value = plan_error.expected_value;
  error.actual_value = plan_error.actual_value;
  error.plan_error = plan_error;
  switch (plan_error.code) {
    case NegotiatedRegenerationPlanErrorCode::kInvalidConfiguration:
      error.code = CpuCandidateAllocationSessionErrorCode::kInvalidConfiguration;
      break;
    case NegotiatedRegenerationPlanErrorCode::kInvalidInput:
      error.code = CpuCandidateAllocationSessionErrorCode::kInvalidInput;
      break;
    case NegotiatedRegenerationPlanErrorCode::kAssociationMismatch:
      error.code = CpuCandidateAllocationSessionErrorCode::kAssociationMismatch;
      break;
    case NegotiatedRegenerationPlanErrorCode::kArithmeticOverflow:
      error.code = CpuCandidateAllocationSessionErrorCode::kArithmeticOverflow;
      break;
    case NegotiatedRegenerationPlanErrorCode::kResourceExhausted:
      error.code = CpuCandidateAllocationSessionErrorCode::kResourceExhausted;
      break;
    case NegotiatedRegenerationPlanErrorCode::kInternalInvariant:
      error.code = CpuCandidateAllocationSessionErrorCode::kInternalInvariant;
      break;
    case NegotiatedRegenerationPlanErrorCode::kBoundExhausted:
    case NegotiatedRegenerationPlanErrorCode::kSelectionFailure:
      break;
  }
  return error;
}

[[nodiscard]] CpuCandidateAllocationSessionError EpochError(
    const CpuTargetedRegenerationEpochError& epoch_error) {
  CpuCandidateAllocationSessionError error =
      Error(CpuCandidateAllocationSessionErrorCode::kEpochFailure, epoch_error.invariant_id,
            epoch_error.detail, epoch_error.net);
  error.expected_value = epoch_error.expected_value;
  error.actual_value = epoch_error.actual_value;
  error.epoch_error = epoch_error;
  switch (epoch_error.code) {
    case CpuTargetedRegenerationEpochErrorCode::kInvalidConfiguration:
      error.code = CpuCandidateAllocationSessionErrorCode::kInvalidConfiguration;
      break;
    case CpuTargetedRegenerationEpochErrorCode::kInvalidInput:
      error.code = CpuCandidateAllocationSessionErrorCode::kInvalidInput;
      break;
    case CpuTargetedRegenerationEpochErrorCode::kAssociationMismatch:
      error.code = CpuCandidateAllocationSessionErrorCode::kAssociationMismatch;
      break;
    case CpuTargetedRegenerationEpochErrorCode::kPlanMismatch:
      error.code = CpuCandidateAllocationSessionErrorCode::kInternalInvariant;
      break;
    case CpuTargetedRegenerationEpochErrorCode::kArithmeticOverflow:
      error.code = CpuCandidateAllocationSessionErrorCode::kArithmeticOverflow;
      break;
    case CpuTargetedRegenerationEpochErrorCode::kResourceExhausted:
      error.code = CpuCandidateAllocationSessionErrorCode::kResourceExhausted;
      break;
    case CpuTargetedRegenerationEpochErrorCode::kInternalInvariant:
      error.code = CpuCandidateAllocationSessionErrorCode::kInternalInvariant;
      break;
    case CpuTargetedRegenerationEpochErrorCode::kBoundExhausted:
    case CpuTargetedRegenerationEpochErrorCode::kCandidateGeneration:
    case CpuTargetedRegenerationEpochErrorCode::kPublicationFailure:
    case CpuTargetedRegenerationEpochErrorCode::kSelectionFailure:
      break;
  }
  return error;
}

[[nodiscard]] CpuCandidateAllocationStop FixedPointStop() noexcept {
  return CpuCandidateAllocationStop{
      .reason = CpuCandidateAllocationStopReason::kFixedPoint,
      .invariant_id = "allocator.cpu_allocation_session.fixed_point.v1",
      .detail = "The authenticated P4R-06 plan has no regeneration targets",
      .expected_value = std::nullopt,
      .actual_value = std::nullopt,
      .plan_error_code = std::nullopt,
      .epoch_error_code = std::nullopt,
  };
}

[[nodiscard]] CpuCandidateAllocationStop EpochBoundStop(
    const NegotiatedRegenerationPlanError& error) noexcept {
  return CpuCandidateAllocationStop{
      .reason = CpuCandidateAllocationStopReason::kEpochBoundExhausted,
      .invariant_id = error.invariant_id,
      .detail = error.detail,
      .expected_value = error.expected_value,
      .actual_value = error.actual_value,
      .plan_error_code = error.code,
      .epoch_error_code = std::nullopt,
  };
}

[[nodiscard]] CpuCandidateAllocationStop EpochBoundStop(
    const CpuTargetedRegenerationEpochError& error) noexcept {
  return CpuCandidateAllocationStop{
      .reason = CpuCandidateAllocationStopReason::kEpochBoundExhausted,
      .invariant_id = error.invariant_id,
      .detail = error.detail,
      .expected_value = error.expected_value,
      .actual_value = error.actual_value,
      .plan_error_code = std::nullopt,
      .epoch_error_code = error.code,
  };
}

[[nodiscard]] CpuCandidateAllocationStop SessionBoundStop(std::string_view invariant_id,
                                                          std::string_view detail,
                                                          std::uint64_t expected,
                                                          std::uint64_t actual) noexcept {
  return CpuCandidateAllocationStop{
      .reason = CpuCandidateAllocationStopReason::kSessionBoundExhausted,
      .invariant_id = invariant_id,
      .detail = detail,
      .expected_value = expected,
      .actual_value = actual,
      .plan_error_code = std::nullopt,
      .epoch_error_code = std::nullopt,
  };
}

struct EpochReservation {
  std::uint64_t source_candidates = 0;
  std::uint64_t source_bytes = 0;
  std::uint64_t targets = 0;
  std::uint64_t price_projection_visits = 0;
  std::uint64_t cpu_work_units = 0;
  std::uint64_t generated_bytes = 0;
  std::uint64_t transaction_items = 0;
};

struct ReservationFailure {
  bool arithmetic = false;
  std::string_view invariant_id;
  std::string_view detail;
  std::uint64_t expected = 0;
  std::uint64_t actual = 0;
};

using ReservationResult = std::variant<EpochReservation, ReservationFailure>;

[[nodiscard]] ReservationResult ReserveEpoch(std::span<const CpuCandidateAllocationPool> pools,
                                             const NegotiatedRegenerationPlan& plan,
                                             const CpuCandidateAllocationSessionCounters& counters,
                                             const CpuCandidateAllocationSessionConfig& config) {
  EpochReservation reservation{.targets = static_cast<std::uint64_t>(plan.targets.size())};
  for (const CpuCandidateAllocationPool& pool : pools) {
    if (!CheckedAdd(pool.candidates().size(), &reservation.source_candidates)) {
      return ReservationFailure{
          .arithmetic = true,
          .invariant_id = "allocator.cpu_allocation_session.source_count_overflow.v1",
          .detail = "Next-epoch source candidate visits overflowed uint64",
      };
    }
    for (const candidates::StoredCandidate& candidate : pool.candidates()) {
      if (!CheckedAdd(candidate->logical_bytes(), &reservation.source_bytes)) {
        return ReservationFailure{
            .arithmetic = true,
            .invariant_id = "allocator.cpu_allocation_session.source_bytes_overflow.v1",
            .detail = "Next-epoch source candidate bytes overflowed uint64",
        };
      }
    }
  }
  const std::optional<std::uint64_t> visits = CheckedMultiply(
      reservation.targets, static_cast<std::uint64_t>(plan.price_snapshot.prices.size()));
  const std::optional<std::uint64_t> work =
      CheckedMultiply(reservation.targets, config.epoch.limits.maximum_cpu_work_units_per_query);
  const std::optional<std::uint64_t> generated =
      CheckedMultiply(reservation.targets, config.epoch.limits.maximum_generated_bytes_per_column);
  if (!visits.has_value() || !work.has_value() || !generated.has_value() ||
      !CheckedAdd(reservation.targets, &reservation.transaction_items) ||
      !CheckedAdd(reservation.source_candidates, &reservation.transaction_items)) {
    return ReservationFailure{
        .arithmetic = true,
        .invariant_id = "allocator.cpu_allocation_session.reservation_overflow.v1",
        .detail = "Next-epoch whole-session reservation overflowed uint64",
    };
  }
  reservation.price_projection_visits = *visits;
  reservation.cpu_work_units = *work;
  reservation.generated_bytes = *generated;

  const CpuCandidateAllocationSessionLimits& limits = config.limits;
  struct Check {
    std::uint64_t current;
    std::uint64_t contribution;
    std::uint64_t limit;
    std::string_view invariant_id;
    std::string_view detail;
  };
  const Check checks[] = {
      {counters.source_candidate_visits, reservation.source_candidates,
       limits.maximum_cumulative_source_candidate_visits,
       "allocator.cpu_allocation_session.source_candidate_visit_bound.v1",
       "Next epoch exceeds the cumulative source-candidate visit bound"},
      {counters.source_candidate_bytes, reservation.source_bytes,
       limits.maximum_cumulative_source_candidate_bytes,
       "allocator.cpu_allocation_session.source_candidate_byte_bound.v1",
       "Next epoch exceeds the cumulative source-candidate byte bound"},
      {counters.target_count, reservation.targets, limits.maximum_cumulative_targets,
       "allocator.cpu_allocation_session.target_bound.v1",
       "Next epoch exceeds the cumulative target and route-query bound"},
      {counters.price_projection_visits, reservation.price_projection_visits,
       limits.maximum_cumulative_price_projection_visits,
       "allocator.cpu_allocation_session.price_projection_visit_bound.v1",
       "Next epoch exceeds the cumulative price-projection visit bound"},
      {counters.reserved_cpu_work_units, reservation.cpu_work_units,
       limits.maximum_reserved_cpu_work_units,
       "allocator.cpu_allocation_session.cpu_work_reservation_bound.v1",
       "Next epoch exceeds the whole-session CPU-work reservation bound"},
      {counters.reserved_generated_bytes, reservation.generated_bytes,
       limits.maximum_reserved_generated_bytes,
       "allocator.cpu_allocation_session.generated_byte_reservation_bound.v1",
       "Next epoch exceeds the whole-session generated-byte reservation bound"},
      {counters.transaction_items, reservation.transaction_items,
       limits.maximum_cumulative_transaction_items,
       "allocator.cpu_allocation_session.transaction_item_bound.v1",
       "Next epoch exceeds the cumulative atomic-publication item bound"},
  };
  for (const Check& check : checks) {
    std::uint64_t actual = check.current;
    if (!CheckedAdd(check.contribution, &actual)) {
      return ReservationFailure{
          .arithmetic = true,
          .invariant_id = "allocator.cpu_allocation_session.cumulative_overflow.v1",
          .detail = "Whole-session cumulative reservation overflowed uint64",
      };
    }
    if (actual > check.limit) {
      return ReservationFailure{
          .arithmetic = false,
          .invariant_id = check.invariant_id,
          .detail = check.detail,
          .expected = check.limit,
          .actual = actual,
      };
    }
  }
  return reservation;
}

}  // namespace

struct CpuCandidateAllocationSessionFactory {
  [[nodiscard]] static CpuCandidateAllocationPool MakePool(
      board_ir::EntityRef net, std::span<const candidates::StoredCandidate> candidates) {
    CpuCandidateAllocationPool pool(net);
    pool.candidates_.assign(candidates.begin(), candidates.end());
    std::ranges::sort(pool.candidates_, [](const candidates::StoredCandidate& left,
                                           const candidates::StoredCandidate& right) {
      return left->id() < right->id();
    });
    pool.one_world_candidates_.reserve(pool.candidates_.size());
    for (const candidates::StoredCandidate& candidate : pool.candidates_) {
      pool.one_world_candidates_.push_back(candidate.get());
    }
    return pool;
  }

  [[nodiscard]] static CpuCandidateAllocationSession Make(
      std::vector<CpuCandidateAllocationPool> pools, OneWorldSelection final_selection,
      std::optional<NegotiatedRegenerationPlan> terminal_plan,
      std::vector<CpuCandidateAllocationStep> steps, CpuCandidateAllocationSessionCounters counters,
      CpuCandidateAllocationStop stop, std::uint64_t initial_pool_identity,
      const CpuCandidateAllocationSessionConfig& config) {
    CpuCandidateAllocationSession session;
    session.pools_ = std::move(pools);
    session.final_selection_ = std::move(final_selection);
    session.terminal_plan_ = std::move(terminal_plan);
    session.steps_ = std::move(steps);
    session.counters_ = counters;
    session.stop_ = stop;
    session.initial_pool_identity_ = initial_pool_identity;
    session.final_pool_identity_ = PoolIdentity(session.pools_);

    session.session_identity_ = SessionIdentity(session, config);
    return session;
  }
};

namespace {

[[nodiscard]] std::optional<CpuCandidateAllocationSessionError> ValidateSessionReplayImpl(
    const board_ir::BoardSnapshot& board, const ResourceCapacityModel& capacities,
    const CpuCandidateAllocationSession& session,
    const CpuCandidateAllocationSessionConfig& config) {
  if (!SessionConfigIsValid(config)) {
    return Error(CpuCandidateAllocationSessionErrorCode::kInvalidConfiguration,
                 "allocator.cpu_allocation_session.replay_configuration.v1",
                 "CPU allocation session replay configuration is outside its hard bounds");
  }
  if (board.content_hash() != capacities.associations().board_content_hash) {
    return Error(CpuCandidateAllocationSessionErrorCode::kAssociationMismatch,
                 "allocator.cpu_allocation_session.replay_capacity_board.v1",
                 "CPU allocation session replay Board IR and capacities do not match");
  }

  try {
    if (session.pools().empty() || session.steps().empty() ||
        session.initial_pool_identity() == 0 || session.final_pool_identity() == 0 ||
        session.session_identity() == 0) {
      return Error(CpuCandidateAllocationSessionErrorCode::kInvalidInput,
                   "allocator.cpu_allocation_session.replay_shape.v1",
                   "CPU allocation session replay requires nonempty canonical result state");
    }

    std::vector<OneWorldCandidatePool> pools;
    pools.reserve(session.pools().size());
    std::optional<std::pair<std::uint64_t, std::uint32_t>> previous_net;
    for (const CpuCandidateAllocationPool& pool : session.pools()) {
      const auto net_key = NetKey(pool.net());
      if (previous_net.has_value() && !(*previous_net < net_key)) {
        return Error(CpuCandidateAllocationSessionErrorCode::kInvalidInput,
                     "allocator.cpu_allocation_session.replay_pool_order.v1",
                     "CPU allocation session final pools are not in strict canonical net order");
      }
      previous_net = net_key;
      candidates::CandidateId previous_id;
      bool has_previous_id = false;
      for (const candidates::StoredCandidate& candidate : pool.candidates()) {
        if (candidate == nullptr || candidate->net() != pool.net() ||
            (has_previous_id && !(previous_id < candidate->id()))) {
          return Error(CpuCandidateAllocationSessionErrorCode::kInvalidInput,
                       "allocator.cpu_allocation_session.replay_candidate_order.v1",
                       "CPU allocation session final candidates are null, wrong-net, or unordered",
                       pool.net());
        }
        previous_id = candidate->id();
        has_previous_id = true;
      }
      pools.push_back(pool.one_world_pool());
    }

    const std::uint64_t computed_final_pool_identity = PoolIdentity(session.pools());
    if (computed_final_pool_identity != session.final_pool_identity()) {
      return Error(CpuCandidateAllocationSessionErrorCode::kInvalidInput,
                   "allocator.cpu_allocation_session.replay_final_pool_identity.v1",
                   "CPU allocation session final-pool identity does not match its owning roster");
    }
    OneWorldSelectionResult planning_selection =
        SelectOneWorldZeroPrice(board, capacities, pools, config.epoch.planning.limits.selection);
    bool selection_matches =
        std::holds_alternative<OneWorldSelection>(planning_selection) &&
        std::get<OneWorldSelection>(planning_selection) == session.final_selection();
    OneWorldSelectionResult refreshed_selection =
        SelectOneWorldZeroPrice(board, capacities, pools, config.epoch.limits.refreshed_selection);
    selection_matches =
        selection_matches ||
        (std::holds_alternative<OneWorldSelection>(refreshed_selection) &&
         std::get<OneWorldSelection>(refreshed_selection) == session.final_selection());
    if (!selection_matches) {
      const OneWorldSelectionError* association_failure = nullptr;
      for (const OneWorldSelectionResult* result : {&planning_selection, &refreshed_selection}) {
        const auto* failure = std::get_if<OneWorldSelectionError>(result);
        if (failure != nullptr &&
            (failure->code == OneWorldSelectionErrorCode::kAssociationMismatch ||
             failure->code == OneWorldSelectionErrorCode::kCandidateAssociationMismatch)) {
          association_failure = failure;
          break;
        }
      }
      if (association_failure != nullptr) {
        return Error(CpuCandidateAllocationSessionErrorCode::kAssociationMismatch,
                     association_failure->invariant_id, association_failure->detail);
      }
      return Error(CpuCandidateAllocationSessionErrorCode::kInvalidInput,
                   "allocator.cpu_allocation_session.replay_final_selection.v1",
                   "CPU allocation session final selection is not an authorized P4R-03 result over "
                   "its final pools");
    }

    CpuCandidateAllocationSessionCounters expected_counters;
    expected_counters.planning_steps = session.steps().size();
    std::uint64_t expected_input_pool_identity = session.initial_pool_identity();
    std::uint64_t expected_prior_snapshot_identity = 0;
    std::uint64_t expected_price_epoch = 0;
    bool last_step_executed = false;
    for (std::size_t index = 0; index < session.steps().size(); ++index) {
      const CpuCandidateAllocationStep& step = session.steps()[index];
      const bool is_last = index + 1U == session.steps().size();
      const bool has_batch = step.batch_identity.has_value();
      const bool has_epoch = step.epoch_identity.has_value();
      const bool has_output = step.output_pool_identity.has_value();
      if (step.input_pool_identity != expected_input_pool_identity || step.plan_identity == 0 ||
          step.prior_snapshot_identity != expected_prior_snapshot_identity ||
          step.price_snapshot_identity == 0 || step.price_epoch_index != expected_price_epoch ||
          has_batch != has_epoch || has_batch != has_output) {
        return Error(
            CpuCandidateAllocationSessionErrorCode::kInvalidInput,
            "allocator.cpu_allocation_session.replay_step_lineage.v1",
            "CPU allocation session step does not advance the canonical pool/snapshot lineage");
      }
      ++expected_price_epoch;
      last_step_executed = has_output;
      if (has_output) {
        if (step.disposition != NegotiatedRegenerationDisposition::kRegenerationRequired ||
            step.target_count == 0 || !CheckedAdd(1, &expected_counters.executed_epochs) ||
            !CheckedAdd(step.epoch_counters.source_candidate_count,
                        &expected_counters.source_candidate_visits) ||
            !CheckedAdd(step.epoch_counters.source_candidate_bytes,
                        &expected_counters.source_candidate_bytes) ||
            !CheckedAdd(step.target_count, &expected_counters.target_count) ||
            !CheckedAdd(step.epoch_counters.route_query_count,
                        &expected_counters.route_query_count) ||
            !CheckedAdd(step.epoch_counters.price_projection_visits,
                        &expected_counters.price_projection_visits) ||
            !CheckedAdd(step.epoch_counters.cpu_work_units,
                        &expected_counters.actual_cpu_work_units) ||
            !CheckedAdd(step.epoch_counters.generated_bytes,
                        &expected_counters.actual_generated_bytes) ||
            !CheckedAdd(step.epoch_counters.admitted_columns,
                        &expected_counters.admitted_columns) ||
            !CheckedAdd(step.epoch_counters.duplicate_columns,
                        &expected_counters.duplicate_columns) ||
            !CheckedAdd(step.epoch_counters.rejected_columns,
                        &expected_counters.rejected_columns)) {
          return Error(CpuCandidateAllocationSessionErrorCode::kInvalidInput,
                       "allocator.cpu_allocation_session.replay_counter_shape.v1",
                       "CPU allocation session executed-step shape or counters are invalid");
        }
        const std::optional<std::uint64_t> reserved_work = CheckedMultiply(
            step.target_count, config.epoch.limits.maximum_cpu_work_units_per_query);
        const std::optional<std::uint64_t> reserved_bytes = CheckedMultiply(
            step.target_count, config.epoch.limits.maximum_generated_bytes_per_column);
        std::uint64_t transaction_items = step.epoch_counters.source_candidate_count;
        std::uint64_t column_outcomes = step.epoch_counters.admitted_columns;
        if (!reserved_work.has_value() || !reserved_bytes.has_value() ||
            !CheckedAdd(step.target_count, &transaction_items) ||
            !CheckedAdd(step.epoch_counters.duplicate_columns, &column_outcomes) ||
            !CheckedAdd(step.epoch_counters.rejected_columns, &column_outcomes) ||
            !CheckedAdd(*reserved_work, &expected_counters.reserved_cpu_work_units) ||
            !CheckedAdd(*reserved_bytes, &expected_counters.reserved_generated_bytes) ||
            !CheckedAdd(transaction_items, &expected_counters.transaction_items) ||
            step.epoch_counters.target_count != step.target_count ||
            step.epoch_counters.route_query_count != step.target_count ||
            step.epoch_counters.cpu_work_units > *reserved_work ||
            step.epoch_counters.generated_bytes > *reserved_bytes ||
            column_outcomes != step.target_count) {
          return Error(CpuCandidateAllocationSessionErrorCode::kInvalidInput,
                       "allocator.cpu_allocation_session.replay_counter_agreement.v1",
                       "CPU allocation session counters do not match executed-step reservations");
        }
        expected_input_pool_identity = *step.output_pool_identity;
        expected_prior_snapshot_identity = step.price_snapshot_identity;
      } else {
        if (!is_last ||
            (step.disposition == NegotiatedRegenerationDisposition::kNoRegenerationRequired
                 ? step.target_count != 0
                 : step.target_count == 0)) {
          return Error(CpuCandidateAllocationSessionErrorCode::kInvalidInput,
                       "allocator.cpu_allocation_session.replay_terminal_step.v1",
                       "CPU allocation session has a nonterminal or inconsistent unexecuted step");
        }
      }
    }
    if (expected_input_pool_identity != session.final_pool_identity() ||
        expected_counters != session.counters()) {
      return Error(
          CpuCandidateAllocationSessionErrorCode::kInvalidInput,
          "allocator.cpu_allocation_session.replay_counters.v1",
          "CPU allocation session final pool or aggregate counters do not match its trace");
    }

    const CpuCandidateAllocationStep& last_step = session.steps().back();
    const bool has_terminal_plan = session.terminal_plan().has_value();
    switch (session.stop().reason) {
      case CpuCandidateAllocationStopReason::kFixedPoint:
        if (!has_terminal_plan || last_step_executed ||
            last_step.disposition != NegotiatedRegenerationDisposition::kNoRegenerationRequired) {
          return Error(CpuCandidateAllocationSessionErrorCode::kInvalidInput,
                       "allocator.cpu_allocation_session.replay_fixed_point.v1",
                       "P4R-08 fixed-point terminal shape is inconsistent");
        }
        break;
      case CpuCandidateAllocationStopReason::kSessionBoundExhausted:
        if (!has_terminal_plan || last_step_executed ||
            last_step.disposition != NegotiatedRegenerationDisposition::kRegenerationRequired) {
          return Error(CpuCandidateAllocationSessionErrorCode::kInvalidInput,
                       "allocator.cpu_allocation_session.replay_session_bound.v1",
                       "P4R-08 session-bound terminal shape is inconsistent");
        }
        break;
      case CpuCandidateAllocationStopReason::kEpochBoundExhausted:
        if (has_terminal_plan == last_step_executed ||
            (has_terminal_plan &&
             last_step.disposition != NegotiatedRegenerationDisposition::kRegenerationRequired)) {
          return Error(CpuCandidateAllocationSessionErrorCode::kInvalidInput,
                       "allocator.cpu_allocation_session.replay_epoch_bound.v1",
                       "P4R-08 epoch-bound terminal shape is inconsistent");
        }
        break;
    }
    if (has_terminal_plan) {
      const NegotiatedRegenerationPlan& plan = *session.terminal_plan();
      if (plan.plan_identity != last_step.plan_identity ||
          plan.price_snapshot.prior_snapshot_identity != last_step.prior_snapshot_identity ||
          plan.price_snapshot.snapshot_identity != last_step.price_snapshot_identity ||
          plan.price_snapshot.epoch_index != last_step.price_epoch_index ||
          plan.disposition != last_step.disposition ||
          plan.targets.size() != last_step.target_count ||
          plan.selection != session.final_selection()) {
        return Error(CpuCandidateAllocationSessionErrorCode::kInvalidInput,
                     "allocator.cpu_allocation_session.replay_terminal_plan.v1",
                     "P4R-08 terminal plan does not match its final trace and selection");
      }
    }
    if (SessionIdentity(session, config) != session.session_identity()) {
      return Error(CpuCandidateAllocationSessionErrorCode::kInvalidInput,
                   "allocator.cpu_allocation_session.replay_identity.v1",
                   "CPU allocation session identity does not match its configuration and payload");
    }
    return std::nullopt;
  } catch (const std::bad_alloc&) {
    return Error(CpuCandidateAllocationSessionErrorCode::kResourceExhausted,
                 "allocator.cpu_allocation_session.replay_allocation.v1",
                 "CPU allocation session replay validation exhausted host memory");
  } catch (const std::length_error&) {
    return Error(CpuCandidateAllocationSessionErrorCode::kResourceExhausted,
                 "allocator.cpu_allocation_session.replay_container_capacity.v1",
                 "CPU allocation session replay validation exceeded container capacity");
  } catch (...) {
    return Error(CpuCandidateAllocationSessionErrorCode::kInternalInvariant,
                 "allocator.cpu_allocation_session.replay_exception.v1",
                 "CPU allocation session replay validation raised an unexpected exception");
  }
}

[[nodiscard]] std::variant<std::vector<CpuCandidateAllocationPool>,
                           CpuCandidateAllocationSessionError>
ValidateAndCopyInitialPools(const board_ir::BoardSnapshot& board,
                            const ResourceCapacityModel& capacities,
                            std::span<const CpuTargetedRegenerationSourcePool> submitted_pools,
                            std::span<const CpuTargetedRegenerationNetContext> submitted_contexts,
                            const CpuCandidateAllocationSessionConfig& config) {
  if (submitted_pools.empty() || submitted_contexts.empty() ||
      submitted_pools.size() != submitted_contexts.size()) {
    return Error(CpuCandidateAllocationSessionErrorCode::kInvalidInput,
                 "allocator.cpu_allocation_session.complete_roster.v1",
                 "CPU allocation session requires one context for every source pool");
  }
  if (submitted_pools.size() > config.epoch.limits.maximum_nets) {
    CpuCandidateAllocationSessionError error =
        Error(CpuCandidateAllocationSessionErrorCode::kInvalidInput,
              "allocator.cpu_allocation_session.net_count.v1",
              "CPU allocation session net roster exceeds the epoch configuration");
    error.expected_value = config.epoch.limits.maximum_nets;
    error.actual_value = submitted_pools.size();
    return error;
  }

  std::vector<const CpuTargetedRegenerationSourcePool*> pools;
  pools.reserve(submitted_pools.size());
  for (const CpuTargetedRegenerationSourcePool& pool : submitted_pools) {
    pools.push_back(&pool);
  }
  std::ranges::sort(pools, [](const CpuTargetedRegenerationSourcePool* left,
                              const CpuTargetedRegenerationSourcePool* right) {
    return NetKey(left->net) < NetKey(right->net);
  });
  std::vector<const CpuTargetedRegenerationNetContext*> contexts;
  contexts.reserve(submitted_contexts.size());
  for (const CpuTargetedRegenerationNetContext& context : submitted_contexts) {
    contexts.push_back(&context);
  }
  std::ranges::sort(contexts, [](const CpuTargetedRegenerationNetContext* left,
                                 const CpuTargetedRegenerationNetContext* right) {
    return NetKey(left->request.net) < NetKey(right->request.net);
  });

  std::set<candidates::CandidateId> candidate_ids;
  std::set<std::pair<std::uint64_t, std::uint64_t>> provenance_ids;
  std::uint64_t total_candidates = 0;
  std::uint64_t total_bytes = 0;
  std::vector<CpuCandidateAllocationPool> copied;
  copied.reserve(pools.size());
  for (std::size_t index = 0; index < pools.size(); ++index) {
    const CpuTargetedRegenerationSourcePool& pool = *pools[index];
    const CpuTargetedRegenerationNetContext& context = *contexts[index];
    if ((index != 0 && pools[index - 1]->net == pool.net) ||
        (index != 0 && contexts[index - 1]->request.net == context.request.net) ||
        context.request.net != pool.net) {
      return Error(CpuCandidateAllocationSessionErrorCode::kInvalidInput,
                   "allocator.cpu_allocation_session.duplicate_or_missing_net.v1",
                   "Source pools and contexts are not one complete unique net roster", pool.net);
    }
    if (context.compiled_board == nullptr || board.FindNet(pool.net) == nullptr) {
      return Error(CpuCandidateAllocationSessionErrorCode::kInvalidInput,
                   "allocator.cpu_allocation_session.net_context.v1",
                   "A source-pool net or retained compiled context is missing", pool.net);
    }
    if (routing::ValidatePreparedCompiledBoardAssociation(board, *context.compiled_board)
            .has_value() ||
        !MatchesLattice(*context.compiled_board, capacities)) {
      return Error(CpuCandidateAllocationSessionErrorCode::kAssociationMismatch,
                   "allocator.cpu_allocation_session.compiled_association.v1",
                   "A retained prepared context does not match the Board IR or resource lattice",
                   pool.net);
    }
    if (const std::optional<routing::RouteRequestAdmissionIssue> issue =
            routing::ValidateTwoTerminalRouteRequest(board, *context.compiled_board,
                                                     context.request);
        issue.has_value()) {
      const bool association =
          *issue == routing::RouteRequestAdmissionIssue::kRoutingProfileNetMismatch;
      return Error(association ? CpuCandidateAllocationSessionErrorCode::kAssociationMismatch
                               : CpuCandidateAllocationSessionErrorCode::kInvalidInput,
                   association ? "allocator.cpu_allocation_session.request_association.v1"
                               : "allocator.cpu_allocation_session.route_request.v1",
                   association ? "A route request does not match its prepared net context"
                               : "A route request is invalid for bounded M1 CPU execution",
                   pool.net);
    }
    if (!context.request.candidate_policy.banned_resources.empty() ||
        !context.request.candidate_policy.resource_penalties.empty()) {
      return Error(CpuCandidateAllocationSessionErrorCode::kInvalidInput,
                   "allocator.cpu_allocation_session.caller_resource_policy.v1",
                   "P4R-06 prices must remain the only resource-action authority", pool.net);
    }
    if (pool.candidates.size() > config.epoch.limits.candidate_store.maximum_candidates_per_net) {
      CpuCandidateAllocationSessionError error =
          Error(CpuCandidateAllocationSessionErrorCode::kInvalidInput,
                "allocator.cpu_allocation_session.source_candidates_per_net.v1",
                "A source pool cannot fit one configured P4R-07 publication", pool.net);
      error.expected_value = config.epoch.limits.candidate_store.maximum_candidates_per_net;
      error.actual_value = pool.candidates.size();
      return error;
    }
    std::uint64_t per_net_bytes = 0;
    for (const candidates::StoredCandidate& candidate : pool.candidates) {
      if (candidate == nullptr || candidate->net() != pool.net ||
          !candidate_ids.insert(candidate == nullptr ? candidates::CandidateId{} : candidate->id())
               .second) {
        return Error(CpuCandidateAllocationSessionErrorCode::kInvalidInput,
                     "allocator.cpu_allocation_session.source_candidate_roster.v1",
                     "Source pools contain a null, wrong-net, or duplicate candidate identity",
                     pool.net);
      }
      if (candidate->data().associations !=
          candidates::AssociationsFor(board, *context.compiled_board)) {
        return Error(CpuCandidateAllocationSessionErrorCode::kAssociationMismatch,
                     "allocator.cpu_allocation_session.source_candidate_association.v1",
                     "A source candidate does not match its exact prepared context", pool.net);
      }
      if (!provenance_ids
               .insert({candidate->data().provenance.batch_identity,
                        candidate->data().provenance.query_identity})
               .second) {
        return Error(CpuCandidateAllocationSessionErrorCode::kInvalidInput,
                     "allocator.cpu_allocation_session.source_provenance.v1",
                     "Source candidates contain duplicate batch/query provenance", pool.net);
      }
      if (!CheckedAdd(candidate->logical_bytes(), &per_net_bytes) ||
          !CheckedAdd(candidate->logical_bytes(), &total_bytes) ||
          !CheckedAdd(1, &total_candidates)) {
        return Error(CpuCandidateAllocationSessionErrorCode::kArithmeticOverflow,
                     "allocator.cpu_allocation_session.source_accounting_overflow.v1",
                     "Initial source candidate accounting overflowed uint64", pool.net);
      }
    }
    if (per_net_bytes > config.epoch.limits.candidate_store.maximum_candidate_bytes_per_net) {
      CpuCandidateAllocationSessionError error =
          Error(CpuCandidateAllocationSessionErrorCode::kInvalidInput,
                "allocator.cpu_allocation_session.source_bytes_per_net.v1",
                "A source pool cannot fit one configured P4R-07 byte budget", pool.net);
      error.expected_value = config.epoch.limits.candidate_store.maximum_candidate_bytes_per_net;
      error.actual_value = per_net_bytes;
      return error;
    }
    copied.push_back(CpuCandidateAllocationSessionFactory::MakePool(pool.net, pool.candidates));
  }
  if (total_candidates > config.epoch.limits.maximum_source_candidates) {
    CpuCandidateAllocationSessionError error =
        Error(CpuCandidateAllocationSessionErrorCode::kInvalidInput,
              "allocator.cpu_allocation_session.source_aggregate_count_bound.v1",
              "Initial source candidate count cannot fit one configured P4R-07 epoch");
    error.expected_value = config.epoch.limits.maximum_source_candidates;
    error.actual_value = total_candidates;
    return error;
  }
  if (total_bytes > config.epoch.limits.maximum_source_candidate_bytes) {
    CpuCandidateAllocationSessionError error =
        Error(CpuCandidateAllocationSessionErrorCode::kInvalidInput,
              "allocator.cpu_allocation_session.source_aggregate_byte_bound.v1",
              "Initial source candidate bytes cannot fit one configured P4R-07 epoch");
    error.expected_value = config.epoch.limits.maximum_source_candidate_bytes;
    error.actual_value = total_bytes;
    return error;
  }
  return copied;
}

[[nodiscard]] CpuCandidateAllocationSessionResult RunImpl(
    const board_ir::BoardSnapshot& board, const ResourceCapacityModel& capacities,
    std::span<const CpuTargetedRegenerationSourcePool> source_pools,
    std::span<const CpuTargetedRegenerationNetContext> contexts,
    CpuCandidateAllocationSessionConfig config,
    internal::CpuCandidateAllocationSessionTestHooks hooks) {
  if (!SessionConfigIsValid(config)) {
    return Error(CpuCandidateAllocationSessionErrorCode::kInvalidConfiguration,
                 "allocator.cpu_allocation_session.configuration.v1",
                 "CPU allocation session configuration is outside its hard bounds");
  }
  if (board.content_hash() != capacities.associations().board_content_hash) {
    return Error(CpuCandidateAllocationSessionErrorCode::kAssociationMismatch,
                 "allocator.cpu_allocation_session.capacity_board_association.v1",
                 "Board IR does not match the resource-capacity model");
  }

  try {
    auto copied = ValidateAndCopyInitialPools(board, capacities, source_pools, contexts, config);
    if (auto* error = std::get_if<CpuCandidateAllocationSessionError>(&copied); error != nullptr) {
      return *error;
    }
    std::vector<CpuCandidateAllocationPool> current =
        std::get<std::vector<CpuCandidateAllocationPool>>(std::move(copied));
    const std::uint64_t initial_pool_identity = PoolIdentity(current);
    std::optional<NegotiatedPriceSnapshot> prior_snapshot;
    std::optional<OneWorldSelection> last_selection;
    std::vector<CpuCandidateAllocationStep> steps;
    steps.reserve(static_cast<std::size_t>(
        std::min<std::uint64_t>(config.limits.maximum_executed_epochs + 1U, 65'536)));
    CpuCandidateAllocationSessionCounters counters;

    while (true) {
      if (hooks.before_planning != nullptr) {
        hooks.before_planning(static_cast<std::size_t>(counters.planning_steps),
                              prior_snapshot.has_value() ? &*prior_snapshot : nullptr,
                              hooks.context);
      }
      std::vector<OneWorldCandidatePool> planning_pools;
      std::vector<CpuTargetedRegenerationSourcePool> execution_pools;
      planning_pools.reserve(current.size());
      execution_pools.reserve(current.size());
      for (const CpuCandidateAllocationPool& pool : current) {
        planning_pools.push_back(pool.one_world_pool());
        execution_pools.push_back(pool.source_pool());
      }
      NegotiatedRegenerationPlanResult plan_result = PlanNegotiatedRegeneration(
          board, capacities, planning_pools,
          prior_snapshot.has_value() ? &*prior_snapshot : nullptr, config.epoch.planning);
      if (auto* plan_error = std::get_if<NegotiatedRegenerationPlanError>(&plan_result);
          plan_error != nullptr) {
        if (plan_error->code == NegotiatedRegenerationPlanErrorCode::kBoundExhausted &&
            last_selection.has_value()) {
          return CpuCandidateAllocationSessionFactory::Make(
              std::move(current), std::move(*last_selection), std::nullopt, std::move(steps),
              counters, EpochBoundStop(*plan_error), initial_pool_identity, config);
        }
        return PlanningError(*plan_error);
      }
      NegotiatedRegenerationPlan plan =
          std::get<NegotiatedRegenerationPlan>(std::move(plan_result));
      if (!CheckedAdd(1, &counters.planning_steps)) {
        return Error(CpuCandidateAllocationSessionErrorCode::kArithmeticOverflow,
                     "allocator.cpu_allocation_session.planning_step_overflow.v1",
                     "Accepted planning-step count overflowed uint64");
      }
      CpuCandidateAllocationStep step{
          .input_pool_identity = PoolIdentity(current),
          .plan_identity = plan.plan_identity,
          .prior_snapshot_identity = plan.price_snapshot.prior_snapshot_identity,
          .price_snapshot_identity = plan.price_snapshot.snapshot_identity,
          .price_epoch_index = plan.price_snapshot.epoch_index,
          .disposition = plan.disposition,
          .target_count = static_cast<std::uint64_t>(plan.targets.size()),
          .batch_identity = std::nullopt,
          .epoch_identity = std::nullopt,
          .output_pool_identity = std::nullopt,
          .epoch_counters = {},
      };

      if (plan.disposition == NegotiatedRegenerationDisposition::kNoRegenerationRequired) {
        steps.push_back(step);
        OneWorldSelection final_selection = plan.selection;
        return CpuCandidateAllocationSessionFactory::Make(
            std::move(current), std::move(final_selection), std::move(plan), std::move(steps),
            counters, FixedPointStop(), initial_pool_identity, config);
      }
      if (counters.executed_epochs >= config.limits.maximum_executed_epochs) {
        steps.push_back(step);
        const std::uint64_t attempted = counters.executed_epochs + 1U;
        OneWorldSelection final_selection = plan.selection;
        return CpuCandidateAllocationSessionFactory::Make(
            std::move(current), std::move(final_selection), std::move(plan), std::move(steps),
            counters,
            SessionBoundStop("allocator.cpu_allocation_session.epoch_count_bound.v1",
                             "Another regeneration epoch exceeds the session epoch bound",
                             config.limits.maximum_executed_epochs, attempted),
            initial_pool_identity, config);
      }

      ReservationResult reservation_result = ReserveEpoch(current, plan, counters, config);
      if (auto* failure = std::get_if<ReservationFailure>(&reservation_result);
          failure != nullptr) {
        if (failure->arithmetic) {
          return Error(CpuCandidateAllocationSessionErrorCode::kArithmeticOverflow,
                       failure->invariant_id, failure->detail);
        }
        steps.push_back(step);
        OneWorldSelection final_selection = plan.selection;
        return CpuCandidateAllocationSessionFactory::Make(
            std::move(current), std::move(final_selection), std::move(plan), std::move(steps),
            counters,
            SessionBoundStop(failure->invariant_id, failure->detail, failure->expected,
                             failure->actual),
            initial_pool_identity, config);
      }
      const EpochReservation reservation = std::get<EpochReservation>(reservation_result);

      CpuTargetedRegenerationEpochResult epoch_result = ExecuteCpuTargetedRegenerationEpoch(
          board, capacities, execution_pools, plan,
          prior_snapshot.has_value() ? &*prior_snapshot : nullptr, contexts, config.epoch);
      if (auto* epoch_error = std::get_if<CpuTargetedRegenerationEpochError>(&epoch_result);
          epoch_error != nullptr) {
        if (epoch_error->code == CpuTargetedRegenerationEpochErrorCode::kBoundExhausted) {
          steps.push_back(step);
          OneWorldSelection final_selection = plan.selection;
          return CpuCandidateAllocationSessionFactory::Make(
              std::move(current), std::move(final_selection), std::move(plan), std::move(steps),
              counters, EpochBoundStop(*epoch_error), initial_pool_identity, config);
        }
        return EpochError(*epoch_error);
      }
      CpuTargetedRegenerationEpoch epoch =
          std::get<CpuTargetedRegenerationEpoch>(std::move(epoch_result));
      const CpuTargetedRegenerationEpochCounters& actual = epoch.counters();
      if (actual.source_candidate_count != reservation.source_candidates ||
          actual.source_candidate_bytes != reservation.source_bytes ||
          actual.target_count != reservation.targets ||
          actual.route_query_count != reservation.targets ||
          actual.price_projection_visits != reservation.price_projection_visits ||
          actual.cpu_work_units > reservation.cpu_work_units ||
          actual.generated_bytes > reservation.generated_bytes ||
          actual.admitted_columns + actual.duplicate_columns + actual.rejected_columns !=
              reservation.targets) {
        return Error(CpuCandidateAllocationSessionErrorCode::kInternalInvariant,
                     "allocator.cpu_allocation_session.epoch_counter_agreement.v1",
                     "Successful P4R-07 counters disagree with the reserved epoch contribution");
      }

      std::vector<CpuCandidateAllocationPool> next;
      next.reserve(epoch.pools().size());
      for (const CpuTargetedRegenerationPool& pool : epoch.pools()) {
        next.push_back(
            CpuCandidateAllocationSessionFactory::MakePool(pool.net(), pool.candidates()));
      }
      if (next.size() != current.size()) {
        return Error(CpuCandidateAllocationSessionErrorCode::kInternalInvariant,
                     "allocator.cpu_allocation_session.output_pool_shape.v1",
                     "Successful P4R-07 epoch changed the complete net-pool roster");
      }
      step.batch_identity = epoch.batch_identity();
      step.epoch_identity = epoch.epoch_identity();
      step.output_pool_identity = PoolIdentity(next);
      step.epoch_counters = actual;
      steps.push_back(step);

      struct CounterCharge {
        std::uint64_t contribution;
        std::uint64_t* destination;
      };
      const CounterCharge charges[] = {
          {1, &counters.executed_epochs},
          {reservation.source_candidates, &counters.source_candidate_visits},
          {reservation.source_bytes, &counters.source_candidate_bytes},
          {reservation.targets, &counters.target_count},
          {actual.route_query_count, &counters.route_query_count},
          {reservation.price_projection_visits, &counters.price_projection_visits},
          {reservation.cpu_work_units, &counters.reserved_cpu_work_units},
          {actual.cpu_work_units, &counters.actual_cpu_work_units},
          {reservation.generated_bytes, &counters.reserved_generated_bytes},
          {actual.generated_bytes, &counters.actual_generated_bytes},
          {reservation.transaction_items, &counters.transaction_items},
          {actual.admitted_columns, &counters.admitted_columns},
          {actual.duplicate_columns, &counters.duplicate_columns},
          {actual.rejected_columns, &counters.rejected_columns},
      };
      for (const CounterCharge& charge : charges) {
        if (!CheckedAdd(charge.contribution, charge.destination)) {
          return Error(CpuCandidateAllocationSessionErrorCode::kArithmeticOverflow,
                       "allocator.cpu_allocation_session.counter_overflow.v1",
                       "Accepted whole-session counters overflowed uint64");
        }
      }
      last_selection = epoch.refreshed_selection();
      prior_snapshot = plan.price_snapshot;
      current = std::move(next);
    }
  } catch (const std::bad_alloc&) {
    return Error(CpuCandidateAllocationSessionErrorCode::kResourceExhausted,
                 "allocator.cpu_allocation_session.allocation.v1",
                 "CPU allocation session scratch allocation failed");
  } catch (const std::length_error&) {
    return Error(CpuCandidateAllocationSessionErrorCode::kResourceExhausted,
                 "allocator.cpu_allocation_session.container_capacity.v1",
                 "CPU allocation session container capacity was exceeded");
  } catch (...) {
    return Error(CpuCandidateAllocationSessionErrorCode::kInternalInvariant,
                 "allocator.cpu_allocation_session.exception.v1",
                 "CPU allocation session raised an unexpected exception");
  }
}

}  // namespace

std::optional<CpuCandidateAllocationSessionError> ValidateCpuCandidateAllocationSessionReplay(
    const board_ir::BoardSnapshot& board, const ResourceCapacityModel& capacities,
    const CpuCandidateAllocationSession& session, CpuCandidateAllocationSessionConfig config) {
  return ValidateSessionReplayImpl(board, capacities, session, config);
}

CpuCandidateAllocationSessionResult RunCpuCandidateAllocationSession(
    const board_ir::BoardSnapshot& board, const ResourceCapacityModel& capacities,
    std::span<const CpuTargetedRegenerationSourcePool> source_pools,
    std::span<const CpuTargetedRegenerationNetContext> contexts,
    CpuCandidateAllocationSessionConfig config) {
  return RunImpl(board, capacities, source_pools, contexts, std::move(config), {});
}

CpuCandidateAllocationSessionResult internal::RunCpuCandidateAllocationSessionWithTestHooks(
    const board_ir::BoardSnapshot& board, const ResourceCapacityModel& capacities,
    std::span<const CpuTargetedRegenerationSourcePool> source_pools,
    std::span<const CpuTargetedRegenerationNetContext> contexts,
    CpuCandidateAllocationSessionConfig config, CpuCandidateAllocationSessionTestHooks hooks) {
  return RunImpl(board, capacities, source_pools, contexts, std::move(config), hooks);
}

}  // namespace apgar::allocator
