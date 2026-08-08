#include "apgar/allocator/cpu_targeted_regeneration_epoch.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
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
#include "apgar/routing/candidate_policy.h"
#include "src/allocator/cpu_targeted_regeneration_epoch_internal.h"
#include "src/candidates/route_candidate_internal.h"

namespace apgar::allocator {
namespace {

using UWide = __uint128_t;

[[nodiscard]] auto NetKey(board_ir::EntityRef net) noexcept {
  return std::pair{net.id, net.generation};
}

[[nodiscard]] CpuTargetedRegenerationEpochError Error(
    CpuTargetedRegenerationEpochErrorCode code, std::string_view invariant_id,
    std::string_view detail, std::optional<board_ir::EntityRef> net = std::nullopt) noexcept {
  return CpuTargetedRegenerationEpochError{
      .code = code,
      .invariant_id = invariant_id,
      .detail = detail,
      .net = net,
      .target_identity = std::nullopt,
      .query_identity = std::nullopt,
      .expected_value = std::nullopt,
      .actual_value = std::nullopt,
      .plan_error_code = std::nullopt,
      .policy_error_code = std::nullopt,
      .route_failure_code = std::nullopt,
      .candidate_rejection_code = std::nullopt,
      .candidate_rejection_invariant = {},
      .selection_error_code = std::nullopt,
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

[[nodiscard]] bool StoreConfigIsValid(const candidates::CandidateStoreConfig& config) noexcept {
  return config.maximum_candidates_per_net > 0 && config.maximum_candidate_bytes_per_net > 0 &&
         config.maximum_rejection_records > 0 &&
         config.maximum_rejection_items_per_transaction > 0 &&
         config.maximum_admission_items_per_transaction > 0 &&
         config.maximum_admission_input_bytes_per_transaction > 0 &&
         config.maximum_admission_work_units_per_transaction > 0;
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

[[nodiscard]] bool ConfigurationIsValid(const CpuTargetedRegenerationEpochConfig& config) noexcept {
  const CpuTargetedRegenerationEpochLimits& limits = config.limits;
  return limits.maximum_nets > 0 && limits.maximum_nets <= kMaximumCpuTargetedEpochNets &&
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

void HashOptionalU64(board_ir::StableHashBuilder* hash,
                     std::optional<std::uint64_t> value) noexcept {
  hash->AddBool(value.has_value());
  if (value.has_value()) {
    hash->AddU64(*value);
  }
}

void HashOptionalNet(board_ir::StableHashBuilder* hash,
                     std::optional<board_ir::EntityRef> value) noexcept {
  hash->AddBool(value.has_value());
  if (value.has_value()) {
    hash->AddU64(value->id);
    hash->AddU32(value->generation);
  }
}

void HashProvenance(board_ir::StableHashBuilder* hash,
                    const candidates::CandidateProvenance& provenance) noexcept {
  hash->AddU32(static_cast<std::uint32_t>(provenance.generator));
  hash->AddU32(provenance.generator_version);
  hash->AddU32(static_cast<std::uint32_t>(provenance.backend));
  hash->AddString(provenance.supported_device_class);
  hash->AddU64(provenance.deterministic_seed);
  hash->AddU64(provenance.batch_identity);
  hash->AddU64(provenance.query_identity);
  hash->AddU32(provenance.candidate_ordinal);
}

void HashRejection(board_ir::StableHashBuilder* hash,
                   const candidates::CandidateRejection& rejection) noexcept {
  hash->AddU32(rejection.schema_version);
  hash->AddBool(rejection.candidate_id.has_value());
  if (rejection.candidate_id.has_value()) {
    HashCandidateId(hash, *rejection.candidate_id);
  }
  HashOptionalNet(hash, rejection.net);
  hash->AddU32(static_cast<std::uint32_t>(rejection.stage));
  hash->AddU32(static_cast<std::uint32_t>(rejection.code));
  hash->AddString(rejection.invariant_id);
  hash->AddU64(rejection.associations.board_content_hash);
  hash->AddU64(rejection.associations.compiler_profile_fingerprint);
  hash->AddU32(rejection.associations.geometry_compiler_version);
  hash->AddU64(rejection.associations.routing_profile_fingerprint);
  hash->AddU64(rejection.associations.rule_bucket_identity);
  hash->AddU64(rejection.policy_identity);
  HashProvenance(hash, rejection.provenance);
  HashOptionalU64(hash, rejection.primitive_witness_index);
  HashOptionalU64(hash, rejection.resource_witness_index);
  HashOptionalU64(hash, rejection.expected_value);
  HashOptionalU64(hash, rejection.actual_value);
  HashOptionalNet(hash, rejection.conflicting_entity);
  HashOptionalU64(hash, rejection.candidate_payload_checksum);
  hash->AddString(rejection.detail);
  hash->AddU64(rejection.logical_bytes);
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
                const CpuTargetedRegenerationEpochConfig& config) noexcept {
  const CpuTargetedRegenerationEpochLimits& limits = config.limits;
  hash->AddU64(limits.maximum_nets);
  hash->AddU64(limits.maximum_source_candidates);
  hash->AddU64(limits.maximum_source_candidate_bytes);
  hash->AddU64(limits.maximum_targets);
  hash->AddU64(limits.maximum_price_projection_visits);
  hash->AddU64(limits.maximum_price_entries_per_target);
  hash->AddU64(limits.maximum_aggregate_price_entries);
  hash->AddU64(limits.maximum_cpu_work_units_per_query);
  hash->AddU64(limits.maximum_aggregate_cpu_work_units);
  hash->AddU64(limits.maximum_generated_bytes_per_column);
  hash->AddU64(limits.maximum_aggregate_generated_bytes);
  hash->AddU64(limits.maximum_retained_candidate_bytes);
  HashStoreConfig(hash, limits.candidate_store);
  HashSelectionLimits(hash, limits.refreshed_selection);
}

[[nodiscard]] std::uint64_t NonzeroHash(board_ir::StableHashBuilder* hash) noexcept {
  const std::uint64_t value = hash->Finish();
  return value == 0 ? 1 : value;
}

struct CanonicalPoolInput {
  const CpuTargetedRegenerationSourcePool* pool = nullptr;
  const CpuTargetedRegenerationNetContext* context = nullptr;
};

struct TargetSlot {
  const NegotiatedRegenerationTarget* target = nullptr;
  const CpuTargetedRegenerationNetContext* context = nullptr;
  routing::PlanarRouteRequest request;
  routing::NormalizedCandidateGenerationPolicy policy;
  std::uint64_t query_identity = 0;
  std::optional<candidates::CandidateDraftBuildResult> draft;
  std::optional<routing::RouteFailureCode> route_failure_code;
  std::optional<routing::CpuRouteTelemetry> route_telemetry;
  CpuTargetedRegenerationColumnOutcome prepublication_outcome =
      CpuTargetedRegenerationColumnOutcome::kAdmissionRejected;
  std::uint64_t generated_bytes = 0;
};

[[nodiscard]] candidates::CandidateProvenance CpuProvenance(const TargetSlot& slot,
                                                            std::uint64_t batch_identity) {
  return candidates::CandidateProvenance{
      .generator = candidates::CandidateGeneratorKind::kCpuAStar,
      .generator_version = 1,
      .backend = candidates::CandidateBackendKind::kCpu,
      .supported_device_class = std::string(candidates::kCpuReferenceDeviceClassV1),
      .deterministic_seed = slot.policy.policy.deterministic_seed,
      .batch_identity = batch_identity,
      .query_identity = slot.query_identity,
      .candidate_ordinal = slot.policy.policy.candidate_ordinal,
  };
}

[[nodiscard]] candidates::CandidateDraftBuildResult RouteDiagnostic(
    const board_ir::BoardSnapshot& board, const TargetSlot& slot, std::uint64_t batch_identity,
    const routing::RouteFailure& failure, bool unsupported) {
  return candidates::internal::RejectGeneratedCandidateDraft(
      board, *slot.context->compiled_board, slot.request, slot.policy,
      CpuProvenance(slot, batch_identity),
      candidates::AssociationsFor(board, *slot.context->compiled_board),
      candidates::CandidateLifecycleStage::kGenerated,
      unsupported ? candidates::CandidateRejectionCode::kUnsupported
                  : candidates::CandidateRejectionCode::kExactValidation,
      unsupported ? "allocator.targeted_epoch.route_unsupported.v1"
                  : "allocator.targeted_epoch.route_disconnected.v1",
      failure.detail);
}

[[nodiscard]] CpuTargetedRegenerationEpochError PlanError(
    const NegotiatedRegenerationPlanError& plan_error) noexcept {
  CpuTargetedRegenerationEpochError error =
      Error(CpuTargetedRegenerationEpochErrorCode::kPlanMismatch, plan_error.invariant_id,
            plan_error.detail, plan_error.net);
  error.plan_error_code = plan_error.code;
  error.expected_value = plan_error.expected_value;
  error.actual_value = plan_error.actual_value;
  switch (plan_error.code) {
    case NegotiatedRegenerationPlanErrorCode::kInvalidConfiguration:
      error.code = CpuTargetedRegenerationEpochErrorCode::kInvalidConfiguration;
      break;
    case NegotiatedRegenerationPlanErrorCode::kAssociationMismatch:
      error.code = CpuTargetedRegenerationEpochErrorCode::kAssociationMismatch;
      break;
    case NegotiatedRegenerationPlanErrorCode::kBoundExhausted:
      error.code = CpuTargetedRegenerationEpochErrorCode::kBoundExhausted;
      break;
    case NegotiatedRegenerationPlanErrorCode::kArithmeticOverflow:
      error.code = CpuTargetedRegenerationEpochErrorCode::kArithmeticOverflow;
      break;
    case NegotiatedRegenerationPlanErrorCode::kResourceExhausted:
      error.code = CpuTargetedRegenerationEpochErrorCode::kResourceExhausted;
      break;
    case NegotiatedRegenerationPlanErrorCode::kInternalInvariant:
      error.code = CpuTargetedRegenerationEpochErrorCode::kInternalInvariant;
      break;
    case NegotiatedRegenerationPlanErrorCode::kInvalidInput:
    case NegotiatedRegenerationPlanErrorCode::kSelectionFailure:
      break;
  }
  return error;
}

[[nodiscard]] CpuTargetedRegenerationEpochError BuildRejectionError(
    const candidates::CandidateRejection& rejection, const TargetSlot& slot) noexcept {
  CpuTargetedRegenerationEpochError error =
      Error(CpuTargetedRegenerationEpochErrorCode::kCandidateGeneration,
            "allocator.targeted_epoch.candidate_build_fatal.v1",
            "Authenticated CPU candidate construction reported a fatal outcome", slot.target->net);
  error.target_identity = slot.target->target_identity;
  error.query_identity = slot.query_identity;
  error.candidate_rejection_code = rejection.code;
  error.candidate_rejection_invariant = rejection.invariant_id;
  switch (rejection.code) {
    case candidates::CandidateRejectionCode::kAssociationMismatch:
      error.code = CpuTargetedRegenerationEpochErrorCode::kAssociationMismatch;
      break;
    case candidates::CandidateRejectionCode::kMemoryAccountingOverflow:
      error.code = CpuTargetedRegenerationEpochErrorCode::kArithmeticOverflow;
      break;
    case candidates::CandidateRejectionCode::kBudgetExhausted:
      error.code = CpuTargetedRegenerationEpochErrorCode::kBoundExhausted;
      break;
    case candidates::CandidateRejectionCode::kBackendFailure:
      error.code = CpuTargetedRegenerationEpochErrorCode::kResourceExhausted;
      break;
    case candidates::CandidateRejectionCode::kInternalInvariant:
      error.code = CpuTargetedRegenerationEpochErrorCode::kInternalInvariant;
      break;
    case candidates::CandidateRejectionCode::kInvalidInput:
    case candidates::CandidateRejectionCode::kUnsupported:
    case candidates::CandidateRejectionCode::kExactValidation:
    case candidates::CandidateRejectionCode::kResourceMismatch:
    case candidates::CandidateRejectionCode::kMetricMismatch:
    case candidates::CandidateRejectionCode::kSignatureMismatch:
    case candidates::CandidateRejectionCode::kDuplicateIdentity:
    case candidates::CandidateRejectionCode::kDuplicateGeometry:
    case candidates::CandidateRejectionCode::kDuplicateResources:
    case candidates::CandidateRejectionCode::kMemoryAccountingMismatch:
    case candidates::CandidateRejectionCode::kCancelled:
      break;
  }
  return error;
}

[[nodiscard]] bool IsFatalBuildRejection(const candidates::CandidateRejection& rejection) noexcept {
  switch (rejection.code) {
    case candidates::CandidateRejectionCode::kAssociationMismatch:
    case candidates::CandidateRejectionCode::kMemoryAccountingOverflow:
    case candidates::CandidateRejectionCode::kBudgetExhausted:
    case candidates::CandidateRejectionCode::kBackendFailure:
    case candidates::CandidateRejectionCode::kInternalInvariant:
      return true;
    case candidates::CandidateRejectionCode::kInvalidInput:
    case candidates::CandidateRejectionCode::kUnsupported:
    case candidates::CandidateRejectionCode::kExactValidation:
    case candidates::CandidateRejectionCode::kResourceMismatch:
    case candidates::CandidateRejectionCode::kMetricMismatch:
    case candidates::CandidateRejectionCode::kSignatureMismatch:
    case candidates::CandidateRejectionCode::kDuplicateIdentity:
    case candidates::CandidateRejectionCode::kDuplicateGeometry:
    case candidates::CandidateRejectionCode::kDuplicateResources:
    case candidates::CandidateRejectionCode::kMemoryAccountingMismatch:
    case candidates::CandidateRejectionCode::kCancelled:
      return false;
  }
  return true;
}

[[nodiscard]] bool IsDuplicate(candidates::CandidateRejectionCode code) noexcept {
  return code == candidates::CandidateRejectionCode::kDuplicateIdentity ||
         code == candidates::CandidateRejectionCode::kDuplicateGeometry ||
         code == candidates::CandidateRejectionCode::kDuplicateResources;
}

[[nodiscard]] bool IsPermittedSourcePublicationRejection(
    candidates::CandidateRejectionCode code) noexcept {
  return IsDuplicate(code) || code == candidates::CandidateRejectionCode::kBudgetExhausted;
}

[[nodiscard]] std::pair<std::uint64_t, std::uint64_t> ResultProvenance(
    const candidates::CandidateStoreAdmissionResult& result) noexcept {
  if (auto* stored = std::get_if<candidates::StoredCandidate>(&result); stored != nullptr) {
    return {(*stored)->data().provenance.batch_identity,
            (*stored)->data().provenance.query_identity};
  }
  const candidates::CandidateRejection& rejection =
      std::get<candidates::CandidateRejection>(result);
  return {rejection.provenance.batch_identity, rejection.provenance.query_identity};
}

[[nodiscard]] CpuTargetedRegenerationEpochError StoreTransactionError(
    const candidates::CandidateRejection& rejection) noexcept {
  CpuTargetedRegenerationEpochError error =
      Error(CpuTargetedRegenerationEpochErrorCode::kPublicationFailure,
            "allocator.targeted_epoch.store_transaction.v1",
            "CandidateStore rejected the complete source-and-column transaction");
  error.candidate_rejection_code = rejection.code;
  error.candidate_rejection_invariant = rejection.invariant_id;
  error.expected_value = rejection.expected_value;
  error.actual_value = rejection.actual_value;
  switch (rejection.code) {
    case candidates::CandidateRejectionCode::kBudgetExhausted:
      error.code = CpuTargetedRegenerationEpochErrorCode::kBoundExhausted;
      break;
    case candidates::CandidateRejectionCode::kMemoryAccountingOverflow:
      error.code = CpuTargetedRegenerationEpochErrorCode::kArithmeticOverflow;
      break;
    case candidates::CandidateRejectionCode::kAssociationMismatch:
      error.code = CpuTargetedRegenerationEpochErrorCode::kAssociationMismatch;
      break;
    case candidates::CandidateRejectionCode::kBackendFailure:
      error.code = CpuTargetedRegenerationEpochErrorCode::kResourceExhausted;
      break;
    case candidates::CandidateRejectionCode::kInternalInvariant:
      error.code = CpuTargetedRegenerationEpochErrorCode::kInternalInvariant;
      break;
    case candidates::CandidateRejectionCode::kInvalidInput:
    case candidates::CandidateRejectionCode::kUnsupported:
    case candidates::CandidateRejectionCode::kExactValidation:
    case candidates::CandidateRejectionCode::kResourceMismatch:
    case candidates::CandidateRejectionCode::kMetricMismatch:
    case candidates::CandidateRejectionCode::kSignatureMismatch:
    case candidates::CandidateRejectionCode::kDuplicateIdentity:
    case candidates::CandidateRejectionCode::kDuplicateGeometry:
    case candidates::CandidateRejectionCode::kDuplicateResources:
    case candidates::CandidateRejectionCode::kMemoryAccountingMismatch:
    case candidates::CandidateRejectionCode::kCancelled:
      break;
  }
  return error;
}

void HashSelection(board_ir::StableHashBuilder* hash, const OneWorldSelection& selection) noexcept {
  hash->AddU64(selection.associations.board_content_hash);
  hash->AddU64(selection.associations.compiler_profile_fingerprint);
  hash->AddU32(selection.associations.geometry_compiler_version);
  hash->AddU64(selection.input_candidate_count);
  hash->AddU64(static_cast<std::uint64_t>(selection.nets.size()));
  for (const OneWorldNetOutcome& outcome : selection.nets) {
    if (auto* selected = std::get_if<OneWorldSelectedCandidate>(&outcome); selected != nullptr) {
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

}  // namespace

struct CpuTargetedRegenerationEpochFactory {
  [[nodiscard]] static CpuTargetedRegenerationEpoch Make(
      const NegotiatedRegenerationPlan& plan, std::vector<CpuTargetedRegenerationColumn> columns,
      std::vector<CpuTargetedRegenerationPool> pools, OneWorldSelection refreshed_selection,
      CpuTargetedRegenerationEpochCounters counters, std::uint64_t batch_identity) {
    CpuTargetedRegenerationEpoch epoch;
    epoch.plan_ = plan;
    epoch.columns_ = std::move(columns);
    epoch.pools_ = std::move(pools);
    epoch.refreshed_selection_ = std::move(refreshed_selection);
    epoch.counters_ = counters;
    epoch.batch_identity_ = batch_identity;

    board_ir::StableHashBuilder hash;
    hash.AddString("APGAR-P4R07-CPU-TARGETED-REGENERATION-EPOCH-V1");
    hash.AddU64(epoch.plan_.plan_identity);
    hash.AddU64(epoch.plan_.price_snapshot.snapshot_identity);
    hash.AddU64(epoch.batch_identity_);
    hash.AddU64(epoch.counters_.source_candidate_count);
    hash.AddU64(epoch.counters_.source_candidate_bytes);
    hash.AddU64(epoch.counters_.target_count);
    hash.AddU64(epoch.counters_.route_query_count);
    hash.AddU64(epoch.counters_.price_projection_visits);
    hash.AddU64(epoch.counters_.projected_price_entries);
    hash.AddU64(epoch.counters_.cpu_work_units);
    hash.AddU64(epoch.counters_.generated_bytes);
    hash.AddU64(epoch.counters_.admitted_columns);
    hash.AddU64(epoch.counters_.duplicate_columns);
    hash.AddU64(epoch.counters_.rejected_columns);
    hash.AddU64(epoch.counters_.retained_candidate_count);
    hash.AddU64(epoch.counters_.retained_candidate_bytes);
    hash.AddU64(static_cast<std::uint64_t>(epoch.columns_.size()));
    for (const CpuTargetedRegenerationColumn& column : epoch.columns_) {
      hash.AddU64(column.net.id);
      hash.AddU32(column.net.generation);
      hash.AddU64(column.target_identity);
      hash.AddU64(column.policy_identity);
      hash.AddU64(column.query_identity);
      hash.AddU32(column.candidate_ordinal);
      hash.AddU32(static_cast<std::uint32_t>(column.outcome));
      hash.AddBool(column.candidate_id.has_value());
      if (column.candidate_id.has_value()) {
        HashCandidateId(&hash, *column.candidate_id);
      }
      hash.AddBool(column.route_failure_code.has_value());
      if (column.route_failure_code.has_value()) {
        hash.AddU32(static_cast<std::uint32_t>(*column.route_failure_code));
      }
      hash.AddBool(column.rejection.has_value());
      if (column.rejection.has_value()) {
        HashRejection(&hash, *column.rejection);
      }
      hash.AddU64(column.generated_bytes);
    }
    hash.AddU64(static_cast<std::uint64_t>(epoch.pools_.size()));
    for (const CpuTargetedRegenerationPool& pool : epoch.pools_) {
      hash.AddU64(pool.net_.id);
      hash.AddU32(pool.net_.generation);
      hash.AddU64(static_cast<std::uint64_t>(pool.candidates_.size()));
      for (const candidates::StoredCandidate& candidate : pool.candidates_) {
        HashCandidateId(&hash, candidate->id());
        hash.AddU64(candidate->data().payload_checksum);
        hash.AddU64(candidate->logical_bytes());
      }
    }
    HashSelection(&hash, epoch.refreshed_selection_);
    epoch.epoch_identity_ = NonzeroHash(&hash);
    return epoch;
  }

  [[nodiscard]] static CpuTargetedRegenerationPool MakePool(
      board_ir::EntityRef net, std::vector<candidates::StoredCandidate> candidates) {
    CpuTargetedRegenerationPool pool(net);
    pool.candidates_ = std::move(candidates);
    pool.one_world_candidates_.reserve(pool.candidates_.size());
    for (const candidates::StoredCandidate& candidate : pool.candidates_) {
      pool.one_world_candidates_.push_back(candidate.get());
    }
    return pool;
  }
};

namespace {

CpuTargetedRegenerationEpochResult ExecuteImpl(
    const board_ir::BoardSnapshot& board, const ResourceCapacityModel& capacities,
    std::span<const CpuTargetedRegenerationSourcePool> submitted_source_pools,
    const NegotiatedRegenerationPlan& plan, const NegotiatedPriceSnapshot* prior_prices,
    std::span<const CpuTargetedRegenerationNetContext> submitted_contexts,
    CpuTargetedRegenerationEpochConfig config,
    internal::CpuTargetedRegenerationEpochTestHooks hooks) {
  if (!ConfigurationIsValid(config)) {
    return Error(CpuTargetedRegenerationEpochErrorCode::kInvalidConfiguration,
                 "allocator.targeted_epoch.configuration.v1",
                 "CPU targeted-regeneration epoch configuration is outside its hard bounds");
  }
  if (board.content_hash() != capacities.associations().board_content_hash) {
    return Error(CpuTargetedRegenerationEpochErrorCode::kAssociationMismatch,
                 "allocator.targeted_epoch.capacity_board_association.v1",
                 "Board IR does not match the resource-capacity model");
  }
  if (submitted_source_pools.empty() || submitted_contexts.empty() ||
      submitted_source_pools.size() != submitted_contexts.size()) {
    return Error(CpuTargetedRegenerationEpochErrorCode::kInvalidInput,
                 "allocator.targeted_epoch.complete_roster.v1",
                 "CPU targeted epoch requires one context for every source-pool roster entry");
  }
  if (submitted_source_pools.size() > config.limits.maximum_nets ||
      submitted_contexts.size() > config.limits.maximum_nets) {
    CpuTargetedRegenerationEpochError error =
        Error(CpuTargetedRegenerationEpochErrorCode::kBoundExhausted,
              "allocator.targeted_epoch.net_count.v1",
              "CPU targeted epoch net roster exceeds its configured bound");
    error.expected_value = config.limits.maximum_nets;
    error.actual_value = std::max(submitted_source_pools.size(), submitted_contexts.size());
    return error;
  }
  if (plan.targets.size() > config.limits.maximum_targets) {
    CpuTargetedRegenerationEpochError error =
        Error(CpuTargetedRegenerationEpochErrorCode::kBoundExhausted,
              "allocator.targeted_epoch.target_count.v1",
              "P4R-06 targets exceed the configured epoch bound");
    error.expected_value = config.limits.maximum_targets;
    error.actual_value = plan.targets.size();
    return error;
  }

  try {
    std::vector<std::vector<const candidates::RouteCandidate*>> replay_candidate_views;
    replay_candidate_views.reserve(submitted_source_pools.size());
    std::vector<OneWorldCandidatePool> replay_pools;
    replay_pools.reserve(submitted_source_pools.size());
    for (const CpuTargetedRegenerationSourcePool& pool : submitted_source_pools) {
      std::vector<const candidates::RouteCandidate*>& candidates =
          replay_candidate_views.emplace_back();
      candidates.reserve(pool.candidates.size());
      for (const candidates::StoredCandidate& candidate : pool.candidates) {
        candidates.push_back(candidate.get());
      }
      replay_pools.push_back(OneWorldCandidatePool{.net = pool.net, .candidates = candidates});
    }
    if (std::optional<NegotiatedRegenerationPlanError> plan_error =
            ValidateNegotiatedRegenerationPlanReplay(capacities, replay_pools, plan, prior_prices,
                                                     config.planning);
        plan_error.has_value()) {
      return PlanError(*plan_error);
    }

    std::vector<const CpuTargetedRegenerationSourcePool*> ordered_pools;
    ordered_pools.reserve(submitted_source_pools.size());
    for (const CpuTargetedRegenerationSourcePool& pool : submitted_source_pools) {
      ordered_pools.push_back(&pool);
    }
    std::ranges::sort(ordered_pools, [](const CpuTargetedRegenerationSourcePool* left,
                                        const CpuTargetedRegenerationSourcePool* right) {
      return NetKey(left->net) < NetKey(right->net);
    });
    std::vector<const CpuTargetedRegenerationNetContext*> ordered_contexts;
    ordered_contexts.reserve(submitted_contexts.size());
    for (const CpuTargetedRegenerationNetContext& context : submitted_contexts) {
      ordered_contexts.push_back(&context);
    }
    std::ranges::sort(ordered_contexts, [](const CpuTargetedRegenerationNetContext* left,
                                           const CpuTargetedRegenerationNetContext* right) {
      return NetKey(left->request.net) < NetKey(right->request.net);
    });

    CpuTargetedRegenerationEpochCounters counters{
        .source_candidate_count = 0,
        .source_candidate_bytes = 0,
        .target_count = static_cast<std::uint64_t>(plan.targets.size()),
    };
    std::set<candidates::CandidateId> source_ids;
    std::set<std::pair<std::uint64_t, std::uint64_t>> source_provenance;
    std::vector<CanonicalPoolInput> canonical_inputs;
    canonical_inputs.reserve(ordered_pools.size());
    const board_ir::EntityRef* previous_net = nullptr;
    for (std::size_t index = 0; index < ordered_pools.size(); ++index) {
      const CpuTargetedRegenerationSourcePool& pool = *ordered_pools[index];
      const CpuTargetedRegenerationNetContext& context = *ordered_contexts[index];
      if ((previous_net != nullptr && *previous_net == pool.net) ||
          context.request.net != pool.net ||
          (index != 0 && ordered_contexts[index - 1]->request.net == context.request.net)) {
        return Error(CpuTargetedRegenerationEpochErrorCode::kInvalidInput,
                     "allocator.targeted_epoch.duplicate_or_missing_net.v1",
                     "Source pools and prepared contexts are not one complete unique net roster",
                     pool.net);
      }
      previous_net = &pool.net;
      if (context.compiled_board == nullptr) {
        return Error(CpuTargetedRegenerationEpochErrorCode::kInvalidInput,
                     "allocator.targeted_epoch.null_compiled_context.v1",
                     "A source-pool net has no retained prepared compiler context", pool.net);
      }
      if (board.FindNet(pool.net) == nullptr) {
        return Error(CpuTargetedRegenerationEpochErrorCode::kInvalidInput,
                     "allocator.targeted_epoch.unknown_net.v1",
                     "A source pool names a net absent from the Board IR snapshot", pool.net);
      }
      if (routing::ValidatePreparedCompiledBoardAssociation(board, *context.compiled_board)
              .has_value() ||
          !MatchesLattice(*context.compiled_board, capacities)) {
        return Error(CpuTargetedRegenerationEpochErrorCode::kAssociationMismatch,
                     "allocator.targeted_epoch.compiled_association.v1",
                     "A retained prepared context does not match the Board IR or resource lattice",
                     pool.net);
      }
      if (const std::optional<routing::RouteRequestAdmissionIssue> request_issue =
              routing::ValidateTwoTerminalRouteRequest(board, *context.compiled_board,
                                                       context.request);
          request_issue.has_value()) {
        const bool association =
            *request_issue == routing::RouteRequestAdmissionIssue::kRoutingProfileNetMismatch;
        return Error(association ? CpuTargetedRegenerationEpochErrorCode::kAssociationMismatch
                                 : CpuTargetedRegenerationEpochErrorCode::kInvalidInput,
                     association ? "allocator.targeted_epoch.request_association.v1"
                                 : "allocator.targeted_epoch.route_request.v1",
                     association ? "A route request does not match its prepared net context"
                                 : "A route request is invalid for bounded M1 CPU execution",
                     pool.net);
      }
      if (!context.request.candidate_policy.banned_resources.empty() ||
          !context.request.candidate_policy.resource_penalties.empty()) {
        return Error(CpuTargetedRegenerationEpochErrorCode::kInvalidInput,
                     "allocator.targeted_epoch.caller_resource_policy.v1",
                     "P4R-06 prices must be the epoch's sole resource-action authority", pool.net);
      }

      std::uint64_t per_net_bytes = 0;
      if (pool.candidates.size() > config.limits.candidate_store.maximum_candidates_per_net) {
        CpuTargetedRegenerationEpochError error =
            Error(CpuTargetedRegenerationEpochErrorCode::kBoundExhausted,
                  "allocator.targeted_epoch.source_candidates_per_net.v1",
                  "A source pool cannot fit the configured atomic publication pool", pool.net);
        error.expected_value = config.limits.candidate_store.maximum_candidates_per_net;
        error.actual_value = pool.candidates.size();
        return error;
      }
      for (const candidates::StoredCandidate& candidate : pool.candidates) {
        if (candidate == nullptr || candidate->net() != pool.net ||
            !source_ids.insert(candidate == nullptr ? candidates::CandidateId{} : candidate->id())
                 .second) {
          return Error(CpuTargetedRegenerationEpochErrorCode::kInvalidInput,
                       "allocator.targeted_epoch.source_candidate_roster.v1",
                       "Source pools contain a null, wrong-net, or duplicate candidate identity",
                       pool.net);
        }
        if (candidate->data().associations !=
            candidates::AssociationsFor(board, *context.compiled_board)) {
          return Error(CpuTargetedRegenerationEpochErrorCode::kAssociationMismatch,
                       "allocator.targeted_epoch.source_candidate_association.v1",
                       "A source candidate does not match its exact prepared context", pool.net);
        }
        if (!CheckedAdd(candidate->logical_bytes(), &per_net_bytes) ||
            !CheckedAdd(candidate->logical_bytes(), &counters.source_candidate_bytes)) {
          return Error(CpuTargetedRegenerationEpochErrorCode::kArithmeticOverflow,
                       "allocator.targeted_epoch.source_candidate_bytes_overflow.v1",
                       "Source candidate logical-byte accounting overflowed uint64", pool.net);
        }
        if (!source_provenance
                 .insert({candidate->data().provenance.batch_identity,
                          candidate->data().provenance.query_identity})
                 .second) {
          return Error(CpuTargetedRegenerationEpochErrorCode::kInvalidInput,
                       "allocator.targeted_epoch.source_provenance.v1",
                       "Source candidates contain a duplicate batch/query provenance identity",
                       pool.net);
        }
      }
      if (per_net_bytes > config.limits.candidate_store.maximum_candidate_bytes_per_net) {
        CpuTargetedRegenerationEpochError error = Error(
            CpuTargetedRegenerationEpochErrorCode::kBoundExhausted,
            "allocator.targeted_epoch.source_bytes_per_net.v1",
            "A source pool cannot fit the configured atomic publication byte budget", pool.net);
        error.expected_value = config.limits.candidate_store.maximum_candidate_bytes_per_net;
        error.actual_value = per_net_bytes;
        return error;
      }
      if (!CheckedAdd(pool.candidates.size(), &counters.source_candidate_count)) {
        return Error(CpuTargetedRegenerationEpochErrorCode::kArithmeticOverflow,
                     "allocator.targeted_epoch.source_candidate_count_overflow.v1",
                     "Source candidate count overflowed uint64", pool.net);
      }
      canonical_inputs.push_back(CanonicalPoolInput{.pool = &pool, .context = &context});
    }
    if (counters.source_candidate_count > config.limits.maximum_source_candidates ||
        counters.source_candidate_bytes > config.limits.maximum_source_candidate_bytes) {
      CpuTargetedRegenerationEpochError error =
          Error(CpuTargetedRegenerationEpochErrorCode::kBoundExhausted,
                "allocator.targeted_epoch.source_aggregate_bound.v1",
                "Source candidate count or bytes exceed the configured aggregate bound");
      error.expected_value = config.limits.maximum_source_candidates;
      error.actual_value = counters.source_candidate_count;
      return error;
    }

    const std::optional<std::uint64_t> maximum_work =
        CheckedMultiply(counters.target_count, config.limits.maximum_cpu_work_units_per_query);
    if (!maximum_work.has_value()) {
      return Error(CpuTargetedRegenerationEpochErrorCode::kArithmeticOverflow,
                   "allocator.targeted_epoch.aggregate_work_overflow.v1",
                   "Preflight target CPU work overflowed uint64");
    }
    if (*maximum_work > config.limits.maximum_aggregate_cpu_work_units) {
      CpuTargetedRegenerationEpochError error =
          Error(CpuTargetedRegenerationEpochErrorCode::kBoundExhausted,
                "allocator.targeted_epoch.aggregate_work_bound.v1",
                "Preflight target CPU work exceeds the configured aggregate bound");
      error.expected_value = config.limits.maximum_aggregate_cpu_work_units;
      error.actual_value = *maximum_work;
      return error;
    }
    const std::optional<std::uint64_t> maximum_generated =
        CheckedMultiply(counters.target_count, config.limits.maximum_generated_bytes_per_column);
    if (!maximum_generated.has_value()) {
      return Error(CpuTargetedRegenerationEpochErrorCode::kArithmeticOverflow,
                   "allocator.targeted_epoch.aggregate_generated_bytes_overflow.v1",
                   "Preflight generated-column bytes overflowed uint64");
    }
    if (*maximum_generated > config.limits.maximum_aggregate_generated_bytes) {
      CpuTargetedRegenerationEpochError error =
          Error(CpuTargetedRegenerationEpochErrorCode::kBoundExhausted,
                "allocator.targeted_epoch.aggregate_generated_bytes_bound.v1",
                "Preflight generated-column bytes exceed the configured aggregate bound");
      error.expected_value = config.limits.maximum_aggregate_generated_bytes;
      error.actual_value = *maximum_generated;
      return error;
    }
    std::uint64_t transaction_items = counters.source_candidate_count;
    if (!CheckedAdd(counters.target_count, &transaction_items)) {
      return Error(CpuTargetedRegenerationEpochErrorCode::kArithmeticOverflow,
                   "allocator.targeted_epoch.transaction_item_overflow.v1",
                   "Source-plus-target publication item count overflowed uint64");
    }
    if (transaction_items > config.limits.candidate_store.maximum_admission_items_per_transaction ||
        transaction_items > config.limits.candidate_store.maximum_rejection_items_per_transaction ||
        transaction_items >
            config.limits.candidate_store.maximum_admission_work_units_per_transaction) {
      CpuTargetedRegenerationEpochError error =
          Error(CpuTargetedRegenerationEpochErrorCode::kBoundExhausted,
                "allocator.targeted_epoch.store_transaction_items.v1",
                "CandidateStore transaction limits cannot contain source plus target items");
      error.expected_value = config.limits.candidate_store.maximum_admission_items_per_transaction;
      error.actual_value = transaction_items;
      return error;
    }

    std::map<board_ir::EntityRef, const CpuTargetedRegenerationNetContext*,
             decltype([](board_ir::EntityRef left, board_ir::EntityRef right) {
               return NetKey(left) < NetKey(right);
             })>
        context_by_net;
    for (const CanonicalPoolInput& input : canonical_inputs) {
      context_by_net.emplace(input.pool->net, input.context);
    }
    std::vector<TargetSlot> slots;
    slots.reserve(plan.targets.size());
    for (const NegotiatedRegenerationTarget& target : plan.targets) {
      const auto context = context_by_net.find(target.net);
      if (context == context_by_net.end()) {
        return Error(CpuTargetedRegenerationEpochErrorCode::kInternalInvariant,
                     "allocator.targeted_epoch.target_context.v1",
                     "Validated P4R-06 target has no canonical prepared context", target.net);
      }
      routing::PlanarRouteRequest request = context->second->request;
      if (plan.price_snapshot.prices.size() >
          config.limits.maximum_price_projection_visits - counters.price_projection_visits) {
        CpuTargetedRegenerationEpochError error =
            Error(CpuTargetedRegenerationEpochErrorCode::kBoundExhausted,
                  "allocator.targeted_epoch.price_projection_visit_bound.v1",
                  "Target price projection exceeds the aggregate visit bound", target.net);
        error.target_identity = target.target_identity;
        error.expected_value = config.limits.maximum_price_projection_visits;
        error.actual_value = counters.price_projection_visits + plan.price_snapshot.prices.size();
        return error;
      }
      counters.price_projection_visits += plan.price_snapshot.prices.size();
      request.candidate_policy.resource_penalties.reserve(std::min<std::size_t>(
          plan.price_snapshot.prices.size(), config.limits.maximum_price_entries_per_target));
      for (const NegotiatedResourcePrice& price : plan.price_snapshot.prices) {
        if (!routing::ResourceExists(*context->second->compiled_board, price.resource)) {
          continue;
        }
        if (request.candidate_policy.resource_penalties.size() >=
            config.limits.maximum_price_entries_per_target) {
          CpuTargetedRegenerationEpochError error =
              Error(CpuTargetedRegenerationEpochErrorCode::kBoundExhausted,
                    "allocator.targeted_epoch.price_entries_per_target.v1",
                    "One target price projection exceeds its configured entry bound", target.net);
          error.target_identity = target.target_identity;
          error.expected_value = config.limits.maximum_price_entries_per_target;
          error.actual_value = request.candidate_policy.resource_penalties.size() + 1U;
          return error;
        }
        if (counters.projected_price_entries >= config.limits.maximum_aggregate_price_entries) {
          CpuTargetedRegenerationEpochError error =
              Error(CpuTargetedRegenerationEpochErrorCode::kBoundExhausted,
                    "allocator.targeted_epoch.aggregate_price_entries.v1",
                    "Target price projections exceed their aggregate entry bound", target.net);
          error.target_identity = target.target_identity;
          error.expected_value = config.limits.maximum_aggregate_price_entries;
          error.actual_value = counters.projected_price_entries + 1U;
          return error;
        }
        request.candidate_policy.resource_penalties.push_back(routing::ResourcePenalty{
            .resource = price.resource, .additional_cost = price.total_price});
        ++counters.projected_price_entries;
      }
      routing::CandidatePolicyResult policy_result = routing::NormalizeCandidateGenerationPolicy(
          *context->second->compiled_board, request.candidate_policy);
      if (auto* policy_error = std::get_if<routing::CandidatePolicyError>(&policy_result);
          policy_error != nullptr) {
        CpuTargetedRegenerationEpochError error =
            Error(CpuTargetedRegenerationEpochErrorCode::kInvalidInput,
                  "allocator.targeted_epoch.price_policy.v1",
                  "P4R-06 total-price projection produced an invalid CPU route policy", target.net);
        error.target_identity = target.target_identity;
        error.policy_error_code = policy_error->code;
        if (policy_error->code == routing::CandidatePolicyErrorCode::kCostOverflow) {
          error.code = CpuTargetedRegenerationEpochErrorCode::kArithmeticOverflow;
        } else if (policy_error->code == routing::CandidatePolicyErrorCode::kTooManyResources) {
          error.code = CpuTargetedRegenerationEpochErrorCode::kBoundExhausted;
        }
        return error;
      }
      routing::NormalizedCandidateGenerationPolicy normalized =
          std::get<routing::NormalizedCandidateGenerationPolicy>(std::move(policy_result));
      request.candidate_policy = normalized.policy;
      slots.push_back(TargetSlot{
          .target = &target,
          .context = context->second,
          .request = std::move(request),
          .policy = std::move(normalized),
          .query_identity = 0,
          .draft = std::nullopt,
          .route_failure_code = std::nullopt,
          .route_telemetry = std::nullopt,
          .prepublication_outcome = CpuTargetedRegenerationColumnOutcome::kAdmissionRejected,
          .generated_bytes = 0,
      });
    }

    board_ir::StableHashBuilder batch_hash;
    batch_hash.AddString("APGAR-P4R07-CPU-TARGETED-REGENERATION-BATCH-V1");
    batch_hash.AddU64(board.content_hash());
    batch_hash.AddU64(plan.plan_identity);
    batch_hash.AddU64(plan.price_snapshot.snapshot_identity);
    HashConfig(&batch_hash, config);
    batch_hash.AddU64(static_cast<std::uint64_t>(canonical_inputs.size()));
    for (const CanonicalPoolInput& input : canonical_inputs) {
      const routing::PlanarRouteRequest& request = input.context->request;
      batch_hash.AddU64(input.pool->net.id);
      batch_hash.AddU32(input.pool->net.generation);
      batch_hash.AddI64(request.start.x);
      batch_hash.AddI64(request.start.y);
      batch_hash.AddI64(request.goal.x);
      batch_hash.AddI64(request.goal.y);
      batch_hash.AddU32(request.start_layer);
      batch_hash.AddU32(request.goal_layer);
      batch_hash.AddU64(static_cast<std::uint64_t>(input.pool->candidates.size()));
      std::vector<candidates::CandidateId> ids;
      ids.reserve(input.pool->candidates.size());
      for (const candidates::StoredCandidate& candidate : input.pool->candidates) {
        ids.push_back(candidate->id());
      }
      std::ranges::sort(ids);
      for (candidates::CandidateId id : ids) {
        HashCandidateId(&batch_hash, id);
      }
    }
    batch_hash.AddU64(static_cast<std::uint64_t>(slots.size()));
    for (const TargetSlot& slot : slots) {
      batch_hash.AddU64(slot.target->target_identity);
      batch_hash.AddU64(slot.target->net.id);
      batch_hash.AddU32(slot.target->net.generation);
      batch_hash.AddU64(slot.policy.identity);
      batch_hash.AddU32(slot.policy.policy.candidate_ordinal);
    }
    const std::uint64_t batch_identity = NonzeroHash(&batch_hash);
    std::set<std::uint64_t> query_identities;
    for (TargetSlot& slot : slots) {
      board_ir::StableHashBuilder query_hash;
      query_hash.AddString("APGAR-P4R07-CPU-TARGETED-REGENERATION-QUERY-V1");
      query_hash.AddU64(batch_identity);
      query_hash.AddU64(slot.target->target_identity);
      query_hash.AddU64(slot.target->net.id);
      query_hash.AddU32(slot.target->net.generation);
      query_hash.AddU64(slot.policy.identity);
      query_hash.AddU32(slot.policy.policy.candidate_ordinal);
      slot.query_identity = NonzeroHash(&query_hash);
      if (!query_identities.insert(slot.query_identity).second ||
          source_provenance.contains({batch_identity, slot.query_identity})) {
        return Error(CpuTargetedRegenerationEpochErrorCode::kInternalInvariant,
                     "allocator.targeted_epoch.query_identity_collision.v1",
                     "Canonical target queries produced a duplicate provenance identity",
                     slot.target->net);
      }
    }

    for (std::size_t slot_index = 0; slot_index < slots.size(); ++slot_index) {
      TargetSlot& slot = slots[slot_index];
      if (hooks.before_target_route != nullptr) {
        const internal::CpuTargetedRegenerationInjectedFailure injected =
            hooks.before_target_route(slot_index, hooks.context);
        if (injected != internal::CpuTargetedRegenerationInjectedFailure::kNone) {
          return Error(
              injected == internal::CpuTargetedRegenerationInjectedFailure::kResourceExhausted
                  ? CpuTargetedRegenerationEpochErrorCode::kResourceExhausted
                  : CpuTargetedRegenerationEpochErrorCode::kInternalInvariant,
              injected == internal::CpuTargetedRegenerationInjectedFailure::kResourceExhausted
                  ? "allocator.targeted_epoch.injected_target_resource.v1"
                  : "allocator.targeted_epoch.injected_target_invariant.v1",
              "Source-private test hook injected a canonical target failure", slot.target->net);
        }
      }
      routing::CpuRouteResult route_result = routing::RouteWithCpuAStar(
          board, *slot.context->compiled_board, slot.request,
          routing::CpuRouteWorkLimits{
              .maximum_work_units = config.limits.maximum_cpu_work_units_per_query,
          });
      ++counters.route_query_count;
      if (auto* route = std::get_if<routing::CpuRoute>(&route_result); route != nullptr) {
        if (hooks.after_cpu_route != nullptr) {
          hooks.after_cpu_route(slot_index, *route, hooks.context);
        }
        slot.route_telemetry = route->telemetry;
        slot.draft.emplace(candidates::BuildGeneratedCandidateFromCpuRoute(
            board, *slot.context->compiled_board, slot.request, slot.policy, *route,
            candidates::CandidateSchedulingIdentity{
                .batch_identity = batch_identity,
                .query_identity = slot.query_identity,
            }));
        if (hooks.after_candidate_draft != nullptr) {
          hooks.after_candidate_draft(slot_index, *slot.draft, hooks.context);
        }
        if (auto* rejection = std::get_if<candidates::CandidateRejection>(&*slot.draft);
            rejection != nullptr) {
          slot.prepublication_outcome =
              CpuTargetedRegenerationColumnOutcome::kCandidateBuildRejected;
          if (IsFatalBuildRejection(*rejection)) {
            return BuildRejectionError(*rejection, slot);
          }
        }
      } else {
        const routing::RouteFailure& failure = std::get<routing::RouteFailure>(route_result);
        slot.route_failure_code = failure.code;
        slot.route_telemetry = failure.telemetry;
        switch (failure.code) {
          case routing::RouteFailureCode::kDisconnected:
            slot.prepublication_outcome = CpuTargetedRegenerationColumnOutcome::kRouteDisconnected;
            slot.draft.emplace(RouteDiagnostic(board, slot, batch_identity, failure, false));
            break;
          case routing::RouteFailureCode::kUnsupportedLayerTransition:
          case routing::RouteFailureCode::kUnsupportedPolicy:
            slot.prepublication_outcome = CpuTargetedRegenerationColumnOutcome::kRouteUnsupported;
            slot.draft.emplace(RouteDiagnostic(board, slot, batch_identity, failure, true));
            break;
          case routing::RouteFailureCode::kResourceExhausted: {
            CpuTargetedRegenerationEpochError error =
                Error(failure.telemetry.has_value() && failure.telemetry->work_limit_exhausted
                          ? CpuTargetedRegenerationEpochErrorCode::kBoundExhausted
                          : CpuTargetedRegenerationEpochErrorCode::kResourceExhausted,
                      failure.telemetry.has_value() && failure.telemetry->work_limit_exhausted
                          ? "allocator.targeted_epoch.query_work_bound.v1"
                          : "allocator.targeted_epoch.query_resource.v1",
                      "Production CPU target generation exhausted a required bound or resource",
                      slot.target->net);
            error.target_identity = slot.target->target_identity;
            error.query_identity = slot.query_identity;
            error.route_failure_code = failure.code;
            return error;
          }
          case routing::RouteFailureCode::kInvalidRequest:
          case routing::RouteFailureCode::kValidationFailed:
          case routing::RouteFailureCode::kInternalInvariant: {
            CpuTargetedRegenerationEpochError error =
                Error(CpuTargetedRegenerationEpochErrorCode::kCandidateGeneration,
                      "allocator.targeted_epoch.route_fatal.v1",
                      "Production CPU target generation reported a fatal route failure",
                      slot.target->net);
            error.target_identity = slot.target->target_identity;
            error.query_identity = slot.query_identity;
            error.route_failure_code = failure.code;
            return error;
          }
        }
      }
      if (!slot.draft.has_value()) {
        return Error(CpuTargetedRegenerationEpochErrorCode::kInternalInvariant,
                     "allocator.targeted_epoch.missing_draft.v1",
                     "Canonical target generation completed without a draft or diagnostic",
                     slot.target->net);
      }
      if (slot.route_telemetry.has_value()) {
        if (!CheckedAdd(slot.route_telemetry->work_units, &counters.cpu_work_units)) {
          return Error(CpuTargetedRegenerationEpochErrorCode::kArithmeticOverflow,
                       "allocator.targeted_epoch.actual_work_overflow.v1",
                       "Actual target CPU work overflowed uint64", slot.target->net);
        }
        if (counters.cpu_work_units > config.limits.maximum_aggregate_cpu_work_units) {
          CpuTargetedRegenerationEpochError error =
              Error(CpuTargetedRegenerationEpochErrorCode::kBoundExhausted,
                    "allocator.targeted_epoch.actual_work_bound.v1",
                    "Actual target CPU work exceeds the aggregate bound", slot.target->net);
          error.expected_value = config.limits.maximum_aggregate_cpu_work_units;
          error.actual_value = counters.cpu_work_units;
          return error;
        }
      }
      slot.generated_bytes =
          std::holds_alternative<candidates::GeneratedRouteCandidate>(*slot.draft)
              ? std::get<candidates::GeneratedRouteCandidate>(*slot.draft).logical_bytes
              : std::get<candidates::CandidateRejection>(*slot.draft).logical_bytes;
      if (slot.generated_bytes > config.limits.maximum_generated_bytes_per_column) {
        CpuTargetedRegenerationEpochError error =
            Error(CpuTargetedRegenerationEpochErrorCode::kBoundExhausted,
                  "allocator.targeted_epoch.column_generated_bytes.v1",
                  "One target column exceeds its generated-byte bound", slot.target->net);
        error.target_identity = slot.target->target_identity;
        error.query_identity = slot.query_identity;
        error.expected_value = config.limits.maximum_generated_bytes_per_column;
        error.actual_value = slot.generated_bytes;
        return error;
      }
      if (!CheckedAdd(slot.generated_bytes, &counters.generated_bytes)) {
        return Error(CpuTargetedRegenerationEpochErrorCode::kArithmeticOverflow,
                     "allocator.targeted_epoch.actual_generated_bytes_overflow.v1",
                     "Actual generated-column bytes overflowed uint64", slot.target->net);
      }
      if (counters.generated_bytes > config.limits.maximum_aggregate_generated_bytes) {
        CpuTargetedRegenerationEpochError error =
            Error(CpuTargetedRegenerationEpochErrorCode::kBoundExhausted,
                  "allocator.targeted_epoch.actual_generated_bytes_bound.v1",
                  "Actual generated-column bytes exceed the aggregate bound", slot.target->net);
        error.expected_value = config.limits.maximum_aggregate_generated_bytes;
        error.actual_value = counters.generated_bytes;
        return error;
      }
    }

    std::vector<candidates::CandidateStoreIncumbentItem> incumbents;
    incumbents.reserve(static_cast<std::size_t>(counters.source_candidate_count));
    for (const CanonicalPoolInput& input : canonical_inputs) {
      std::vector<candidates::StoredCandidate> candidates(input.pool->candidates.begin(),
                                                          input.pool->candidates.end());
      std::ranges::sort(candidates, [](const candidates::StoredCandidate& left,
                                       const candidates::StoredCandidate& right) {
        return left->id() < right->id();
      });
      for (const candidates::StoredCandidate& candidate : candidates) {
        routing::PlanarRouteRequest request = input.context->request;
        request.candidate_policy = candidate->data().policy;
        incumbents.push_back(candidates::CandidateStoreIncumbentItem{
            .compiled_board = input.context->compiled_board,
            .request = std::move(request),
            .candidate = candidate,
        });
      }
    }
    std::vector<candidates::CandidateStoreDraftItem> store_items;
    store_items.reserve(slots.size());
    for (TargetSlot& slot : slots) {
      store_items.push_back(candidates::CandidateStoreDraftItem{
          .compiled_board = slot.context->compiled_board,
          .request = std::move(slot.request),
          .draft = std::move(*slot.draft),
      });
    }

    candidates::CandidateStore store(config.limits.candidate_store);
    std::vector<candidates::CandidateStoreAdmissionResult> store_results =
        store.AdmitDraftBatchWithIncumbents(board, std::move(incumbents), std::move(store_items));
    if (hooks.after_local_publication != nullptr) {
      const internal::CpuTargetedRegenerationInjectedFailure injected =
          hooks.after_local_publication(hooks.context);
      if (injected != internal::CpuTargetedRegenerationInjectedFailure::kNone) {
        return Error(
            injected == internal::CpuTargetedRegenerationInjectedFailure::kResourceExhausted
                ? CpuTargetedRegenerationEpochErrorCode::kResourceExhausted
                : CpuTargetedRegenerationEpochErrorCode::kInternalInvariant,
            injected == internal::CpuTargetedRegenerationInjectedFailure::kResourceExhausted
                ? "allocator.targeted_epoch.injected_post_publication_resource.v1"
                : "allocator.targeted_epoch.injected_post_publication_invariant.v1",
            "Source-private test hook injected a post-publication local failure");
      }
    }
    std::map<std::pair<std::uint64_t, std::uint64_t>, std::size_t> target_by_provenance;
    for (std::size_t index = 0; index < slots.size(); ++index) {
      target_by_provenance.emplace(std::pair{batch_identity, slots[index].query_identity}, index);
    }
    std::vector<candidates::CandidateStoreAdmissionResult*> target_results(slots.size(), nullptr);
    for (candidates::CandidateStoreAdmissionResult& result : store_results) {
      const auto provenance = ResultProvenance(result);
      const auto target = target_by_provenance.find(provenance);
      if (target != target_by_provenance.end()) {
        if (target_results[target->second] != nullptr) {
          return Error(CpuTargetedRegenerationEpochErrorCode::kInternalInvariant,
                       "allocator.targeted_epoch.duplicate_store_result.v1",
                       "CandidateStore returned two outcomes for one target query");
        }
        target_results[target->second] = &result;
        continue;
      }
      if (source_provenance.contains(provenance)) {
        if (auto* rejection = std::get_if<candidates::CandidateRejection>(&result);
            rejection != nullptr && !IsPermittedSourcePublicationRejection(rejection->code)) {
          return StoreTransactionError(*rejection);
        }
        continue;
      }
      if (auto* rejection = std::get_if<candidates::CandidateRejection>(&result);
          rejection != nullptr && provenance == std::pair<std::uint64_t, std::uint64_t>{0, 0}) {
        return StoreTransactionError(*rejection);
      }
      return Error(CpuTargetedRegenerationEpochErrorCode::kInternalInvariant,
                   "allocator.targeted_epoch.foreign_store_result.v1",
                   "CandidateStore returned a foreign publication result");
    }
    if (store_results.size() != transaction_items ||
        std::ranges::any_of(target_results, [](const auto* result) { return result == nullptr; })) {
      return Error(CpuTargetedRegenerationEpochErrorCode::kInternalInvariant,
                   "allocator.targeted_epoch.store_result_count.v1",
                   "CandidateStore omitted or added a source-and-column transaction result");
    }

    std::vector<CpuTargetedRegenerationColumn> columns;
    columns.reserve(slots.size());
    for (std::size_t index = 0; index < slots.size(); ++index) {
      TargetSlot& slot = slots[index];
      candidates::CandidateStoreAdmissionResult& result = *target_results[index];
      CpuTargetedRegenerationColumn column{
          .net = slot.target->net,
          .target_identity = slot.target->target_identity,
          .policy_identity = slot.policy.identity,
          .query_identity = slot.query_identity,
          .candidate_ordinal = slot.policy.policy.candidate_ordinal,
          .outcome = slot.prepublication_outcome,
          .candidate_id = std::nullopt,
          .route_failure_code = slot.route_failure_code,
          .rejection = std::nullopt,
          .generated_bytes = slot.generated_bytes,
      };
      if (auto* stored = std::get_if<candidates::StoredCandidate>(&result); stored != nullptr) {
        column.outcome = CpuTargetedRegenerationColumnOutcome::kAdmitted;
        column.candidate_id = (*stored)->id();
        ++counters.admitted_columns;
      } else {
        candidates::CandidateRejection rejection =
            std::get<candidates::CandidateRejection>(std::move(result));
        if (rejection.code == candidates::CandidateRejectionCode::kAssociationMismatch ||
            rejection.code == candidates::CandidateRejectionCode::kMemoryAccountingOverflow ||
            rejection.code == candidates::CandidateRejectionCode::kBackendFailure ||
            rejection.code == candidates::CandidateRejectionCode::kInternalInvariant) {
          return StoreTransactionError(rejection);
        }
        column.candidate_id = rejection.candidate_id;
        if (slot.prepublication_outcome ==
            CpuTargetedRegenerationColumnOutcome::kAdmissionRejected) {
          column.outcome = IsDuplicate(rejection.code)
                               ? CpuTargetedRegenerationColumnOutcome::kDuplicate
                               : CpuTargetedRegenerationColumnOutcome::kAdmissionRejected;
        }
        if (column.outcome == CpuTargetedRegenerationColumnOutcome::kDuplicate) {
          ++counters.duplicate_columns;
        } else {
          ++counters.rejected_columns;
        }
        column.rejection = std::move(rejection);
      }
      columns.push_back(std::move(column));
    }

    std::vector<CpuTargetedRegenerationPool> refreshed_pools;
    refreshed_pools.reserve(canonical_inputs.size());
    for (const CanonicalPoolInput& input : canonical_inputs) {
      std::vector<candidates::StoredCandidate> retained = store.Enumerate(input.pool->net);
      for (const candidates::StoredCandidate& candidate : retained) {
        if (!CheckedAdd(1, &counters.retained_candidate_count) ||
            !CheckedAdd(candidate->logical_bytes(), &counters.retained_candidate_bytes)) {
          return Error(CpuTargetedRegenerationEpochErrorCode::kArithmeticOverflow,
                       "allocator.targeted_epoch.retained_accounting_overflow.v1",
                       "Refreshed pool candidate or byte accounting overflowed uint64",
                       input.pool->net);
        }
      }
      if (counters.retained_candidate_bytes > config.limits.maximum_retained_candidate_bytes) {
        CpuTargetedRegenerationEpochError error = Error(
            CpuTargetedRegenerationEpochErrorCode::kBoundExhausted,
            "allocator.targeted_epoch.retained_bytes_bound.v1",
            "Refreshed immutable pools exceed the aggregate retained-byte bound", input.pool->net);
        error.expected_value = config.limits.maximum_retained_candidate_bytes;
        error.actual_value = counters.retained_candidate_bytes;
        return error;
      }
      refreshed_pools.push_back(
          CpuTargetedRegenerationEpochFactory::MakePool(input.pool->net, std::move(retained)));
    }

    std::vector<OneWorldCandidatePool> refreshed_one_world;
    refreshed_one_world.reserve(refreshed_pools.size());
    for (const CpuTargetedRegenerationPool& pool : refreshed_pools) {
      refreshed_one_world.push_back(pool.one_world_pool());
    }
    OneWorldSelectionResult selection_result = SelectOneWorldZeroPrice(
        board, capacities, refreshed_one_world, config.limits.refreshed_selection);
    if (auto* selection_error = std::get_if<OneWorldSelectionError>(&selection_result);
        selection_error != nullptr) {
      CpuTargetedRegenerationEpochError error =
          Error(CpuTargetedRegenerationEpochErrorCode::kSelectionFailure,
                selection_error->invariant_id, selection_error->detail);
      error.selection_error_code = selection_error->code;
      error.accounting_error_code = selection_error->accounting_error_code;
      switch (selection_error->code) {
        case OneWorldSelectionErrorCode::kAssociationMismatch:
        case OneWorldSelectionErrorCode::kCandidateAssociationMismatch:
          error.code = CpuTargetedRegenerationEpochErrorCode::kAssociationMismatch;
          break;
        case OneWorldSelectionErrorCode::kInputBoundExceeded:
          error.code = CpuTargetedRegenerationEpochErrorCode::kBoundExhausted;
          break;
        case OneWorldSelectionErrorCode::kResourceExhausted:
          error.code = CpuTargetedRegenerationEpochErrorCode::kResourceExhausted;
          break;
        case OneWorldSelectionErrorCode::kInvalidLimits:
          error.code = CpuTargetedRegenerationEpochErrorCode::kInvalidConfiguration;
          break;
        case OneWorldSelectionErrorCode::kAccountingFailure:
          if (selection_error->accounting_error_code ==
              ResourceAccountingErrorCode::kUsageOverflow) {
            error.code = CpuTargetedRegenerationEpochErrorCode::kArithmeticOverflow;
          } else if (selection_error->accounting_error_code ==
                     ResourceAccountingErrorCode::kInputBoundExceeded) {
            error.code = CpuTargetedRegenerationEpochErrorCode::kBoundExhausted;
          } else if (selection_error->accounting_error_code ==
                     ResourceAccountingErrorCode::kResourceExhausted) {
            error.code = CpuTargetedRegenerationEpochErrorCode::kResourceExhausted;
          }
          break;
        case OneWorldSelectionErrorCode::kDuplicateNetPool:
        case OneWorldSelectionErrorCode::kUnknownNet:
        case OneWorldSelectionErrorCode::kNullCandidate:
        case OneWorldSelectionErrorCode::kCandidateNetMismatch:
        case OneWorldSelectionErrorCode::kInvalidCandidateIdentity:
        case OneWorldSelectionErrorCode::kDuplicateCandidateIdentity:
          break;
      }
      return error;
    }
    OneWorldSelection refreshed_selection =
        std::get<OneWorldSelection>(std::move(selection_result));
    return CpuTargetedRegenerationEpochFactory::Make(
        plan, std::move(columns), std::move(refreshed_pools), std::move(refreshed_selection),
        counters, batch_identity);
  } catch (const std::bad_alloc&) {
    return Error(CpuTargetedRegenerationEpochErrorCode::kResourceExhausted,
                 "allocator.targeted_epoch.allocation.v1",
                 "CPU targeted-regeneration epoch scratch allocation failed");
  } catch (const std::length_error&) {
    return Error(CpuTargetedRegenerationEpochErrorCode::kResourceExhausted,
                 "allocator.targeted_epoch.container_capacity.v1",
                 "CPU targeted-regeneration epoch container capacity was exceeded");
  } catch (...) {
    return Error(CpuTargetedRegenerationEpochErrorCode::kInternalInvariant,
                 "allocator.targeted_epoch.exception.v1",
                 "CPU targeted-regeneration epoch raised an unexpected exception");
  }
}

}  // namespace

CpuTargetedRegenerationEpochResult ExecuteCpuTargetedRegenerationEpoch(
    const board_ir::BoardSnapshot& board, const ResourceCapacityModel& capacities,
    std::span<const CpuTargetedRegenerationSourcePool> source_pools,
    const NegotiatedRegenerationPlan& plan, const NegotiatedPriceSnapshot* prior_prices,
    std::span<const CpuTargetedRegenerationNetContext> contexts,
    CpuTargetedRegenerationEpochConfig config) {
  return ExecuteImpl(board, capacities, source_pools, plan, prior_prices, contexts,
                     std::move(config), {});
}

CpuTargetedRegenerationEpochResult internal::ExecuteCpuTargetedRegenerationEpochWithTestHooks(
    const board_ir::BoardSnapshot& board, const ResourceCapacityModel& capacities,
    std::span<const CpuTargetedRegenerationSourcePool> source_pools,
    const NegotiatedRegenerationPlan& plan, const NegotiatedPriceSnapshot* prior_prices,
    std::span<const CpuTargetedRegenerationNetContext> contexts,
    CpuTargetedRegenerationEpochConfig config, CpuTargetedRegenerationEpochTestHooks hooks) {
  return ExecuteImpl(board, capacities, source_pools, plan, prior_prices, contexts,
                     std::move(config), hooks);
}

}  // namespace apgar::allocator
