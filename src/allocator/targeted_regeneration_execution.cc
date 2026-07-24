#include "apgar/allocator/targeted_regeneration_execution.h"

#include <algorithm>
#include <chrono>
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
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "apgar/board_ir/stable_hash.h"
#include "apgar/candidates/route_candidate.h"
#include "apgar/routing/cpu_astar.h"
#include "src/allocator/one_world_internal.h"
#include "src/allocator/targeted_regeneration_execution_internal.h"
#include "src/allocator/targeted_regeneration_internal.h"
#include "src/operational_timestamp.h"

namespace apgar::allocator {
namespace {

using UWide = unsigned __int128;
using OperationalClock = std::chrono::steady_clock;

[[nodiscard]] std::uint64_t OperationalElapsed(OperationalClock::time_point start) noexcept {
  const auto elapsed =
      std::chrono::duration_cast<std::chrono::nanoseconds>(OperationalClock::now() - start).count();
  return elapsed <= 0 ? 0 : static_cast<std::uint64_t>(elapsed);
}

thread_local std::uint64_t g_route_queries_started = 0;
thread_local std::uint64_t g_source_resource_span_visits = 0;
thread_local std::optional<std::uint64_t> g_request_preparation_host_failure_countdown;
thread_local internal::TargetedRegenerationHostFailureForTesting
    g_request_preparation_host_failure =
        internal::TargetedRegenerationHostFailureForTesting::kBadAlloc;
thread_local std::optional<std::uint64_t> g_post_query_host_failure_countdown;
thread_local internal::TargetedRegenerationHostFailureForTesting g_post_query_host_failure =
    internal::TargetedRegenerationHostFailureForTesting::kBadAlloc;
thread_local std::optional<internal::TargetedRegenerationHostFailureForTesting>
    g_post_publication_host_failure;
thread_local std::optional<internal::TargetedRegenerationRejectionEvidenceBoundaryForTesting>
    g_rejection_evidence_host_failure_boundary;
thread_local internal::TargetedRegenerationHostFailureForTesting g_rejection_evidence_host_failure =
    internal::TargetedRegenerationHostFailureForTesting::kBadAlloc;
thread_local bool g_route_failure_for_testing = false;
thread_local bool g_build_rejection_for_testing = false;
thread_local bool g_successor_lease_failure_for_testing = false;

[[noreturn]] void ThrowHostFailureForTesting(
    internal::TargetedRegenerationHostFailureForTesting failure, std::string_view detail) {
  if (failure == internal::TargetedRegenerationHostFailureForTesting::kLengthError) {
    throw std::length_error(std::string(detail));
  }
  if (failure == internal::TargetedRegenerationHostFailureForTesting::kUnexpectedException) {
    throw std::runtime_error(std::string(detail));
  }
  throw std::bad_alloc();
}

void MaybeFailAfterRequestPreparationForTesting() {
  if (!g_request_preparation_host_failure_countdown.has_value()) {
    return;
  }
  if (*g_request_preparation_host_failure_countdown != 0) {
    --*g_request_preparation_host_failure_countdown;
    return;
  }
  g_request_preparation_host_failure_countdown.reset();
  ThrowHostFailureForTesting(g_request_preparation_host_failure,
                             "Injected targeted-regeneration request-preparation failure");
}

void MaybeFailAfterQueryStartForTesting() {
  if (!g_post_query_host_failure_countdown.has_value()) {
    return;
  }
  if (*g_post_query_host_failure_countdown != 0) {
    --*g_post_query_host_failure_countdown;
    return;
  }
  g_post_query_host_failure_countdown.reset();
  ThrowHostFailureForTesting(g_post_query_host_failure,
                             "Injected targeted-regeneration post-query failure");
}

void MaybeFailRejectionEvidenceForTesting(
    internal::TargetedRegenerationRejectionEvidenceBoundaryForTesting boundary) {
  if (g_rejection_evidence_host_failure_boundary != boundary) {
    return;
  }
  g_rejection_evidence_host_failure_boundary.reset();
  ThrowHostFailureForTesting(g_rejection_evidence_host_failure,
                             "Injected targeted-regeneration rejection-evidence failure");
}

void MaybeFailAfterPublicationForTesting() {
  if (!g_post_publication_host_failure.has_value()) {
    return;
  }
  const internal::TargetedRegenerationHostFailureForTesting failure =
      *std::exchange(g_post_publication_host_failure, std::nullopt);
  ThrowHostFailureForTesting(failure, "Injected targeted-regeneration post-publication failure");
}

[[nodiscard]] TargetedRegenerationExecutionError Error(TargetedRegenerationExecutionErrorCode code,
                                                       std::string_view invariant_id,
                                                       std::string_view detail) noexcept {
  return TargetedRegenerationExecutionError{
      .code = code,
      .invariant_id = invariant_id,
      .detail = detail,
      .failed_execution = std::nullopt,
  };
}

[[nodiscard]] bool ConfigIsValid(const TargetedRegenerationExecutionConfig& config) noexcept {
  return config.maximum_route_queries > 0 &&
         config.maximum_route_queries <= kMaximumTargetedRegenerationRouteQueriesV2 &&
         config.route_limits.maximum_work_units > 0 &&
         config.route_limits.maximum_work_units <= kMaximumTargetedRegenerationRouteWorkUnitsV2 &&
         config.route_limits.maximum_record_count > 0 &&
         config.route_limits.maximum_queue_size > 0 &&
         config.route_limits.maximum_reconstruction_states > 0 &&
         config.maximum_total_route_work_units > 0 &&
         config.maximum_total_route_work_units <= kMaximumTargetedRegenerationRouteWorkUnitsV2 &&
         config.maximum_policy_projection_visits > 0 &&
         config.maximum_policy_projection_visits <=
             kMaximumTargetedRegenerationPolicyProjectionVisitsV2 &&
         config.maximum_policy_resource_entries > 0 &&
         config.maximum_policy_resource_entries <=
             kMaximumTargetedRegenerationPolicyResourceEntriesV4 &&
         config.maximum_candidate_draft_bytes > 0 && config.maximum_generated_candidate_bytes > 0 &&
         config.maximum_rejection_bytes > 0 && config.maximum_transient_result_bytes > 0;
}

[[nodiscard]] bool NetBefore(board_ir::EntityRef left, board_ir::EntityRef right) noexcept {
  return std::tie(left.id, left.generation) < std::tie(right.id, right.generation);
}

void AddExecutionConfig(board_ir::StableHashBuilder& hash,
                        const TargetedRegenerationExecutionConfig& config) noexcept {
  hash.AddU64(config.deterministic_seed);
  hash.AddU64(config.maximum_route_queries);
  hash.AddU64(config.route_limits.maximum_work_units);
  hash.AddU64(config.route_limits.maximum_record_count);
  hash.AddU64(config.route_limits.maximum_queue_size);
  hash.AddU64(config.route_limits.maximum_reconstruction_states);
  hash.AddU64(config.maximum_total_route_work_units);
  hash.AddU64(config.maximum_policy_projection_visits);
  hash.AddU64(config.maximum_policy_resource_entries);
  hash.AddU64(config.maximum_candidate_draft_bytes);
  hash.AddU64(config.maximum_generated_candidate_bytes);
  hash.AddU64(config.maximum_rejection_bytes);
  hash.AddU64(config.maximum_transient_result_bytes);
  hash.AddU64(config.known_unmapped_exact_conflict_count);
}

void AddStoreConfig(board_ir::StableHashBuilder& hash,
                    const candidates::CandidateStoreConfig& config) noexcept {
  hash.AddU64(config.maximum_candidates_per_net);
  hash.AddU64(config.maximum_candidate_bytes_per_net);
  hash.AddU64(config.maximum_rejection_records);
  hash.AddU64(config.maximum_rejection_items_per_transaction);
  hash.AddU64(config.maximum_admission_items_per_transaction);
  hash.AddU64(config.maximum_admission_input_bytes_per_transaction);
  hash.AddU64(config.maximum_admission_work_units_per_transaction);
  hash.AddU64(config.maximum_pin_lease_items_per_transaction);
  hash.AddU64(config.maximum_expected_pools_per_invocation);
  hash.AddU64(config.maximum_expected_candidates_per_invocation);
}

[[nodiscard]] std::uint64_t NonZeroBatchIdentity(
    std::uint64_t plan_checksum, board_ir::EntityRef net, std::uint64_t target_index,
    const TargetedRegenerationExecutionConfig& config,
    const candidates::CandidateStoreConfig& store_config) noexcept {
  board_ir::StableHashBuilder hash;
  hash.AddString("APGAR-TARGETED-REGENERATION-CPU-BATCH-V5");
  hash.AddU64(plan_checksum);
  hash.AddU64(net.id);
  hash.AddU32(net.generation);
  hash.AddU64(target_index);
  AddExecutionConfig(hash, config);
  AddStoreConfig(hash, store_config);
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

void AddCounters(board_ir::StableHashBuilder& hash,
                 const TargetedRegenerationExecutionCounters& counters) noexcept {
  hash.AddU64(counters.requested_columns);
  hash.AddU64(counters.route_queries);
  hash.AddU64(counters.route_work_units);
  hash.AddU64(counters.policy_projection_visits);
  hash.AddU64(counters.peak_route_record_count);
  hash.AddU64(counters.peak_route_queue_size);
  hash.AddU64(counters.generated_candidate_bytes);
  hash.AddU64(counters.rejection_record_bytes);
  hash.AddU64(counters.transient_result_bytes);
  hash.AddU64(counters.successful_routes);
  hash.AddU64(counters.built_candidates);
  hash.AddU64(counters.admitted_candidates);
  hash.AddU64(counters.duplicate_candidates);
  hash.AddU64(counters.rejected_columns);
  hash.AddU64(counters.novel_retained_candidates);
  hash.AddU64(counters.changed_selections);
  hash.AddU64(counters.successor_pinned_candidates);
}

void AddColumns(board_ir::StableHashBuilder& hash,
                std::span<const TargetedRegenerationColumnRecord> columns) noexcept {
  hash.AddU64(columns.size());
  for (const TargetedRegenerationColumnRecord& column : columns) {
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
}

[[nodiscard]] TargetedRegenerationExecutionError ErrorWithFailedExecution(
    TargetedRegenerationExecutionErrorCode code, std::string_view invariant_id,
    std::string_view detail, std::uint64_t plan_checksum,
    const TargetedRegenerationExecutionConfig& config,
    const candidates::CandidateStoreConfig& store_config,
    bool candidate_store_publication_committed,
    const TargetedRegenerationExecutionCounters& counters,
    std::vector<TargetedRegenerationColumnRecord>&& columns) noexcept {
  TargetedRegenerationExecutionError::FailedExecutionObservation observation{
      .schema_version = kTargetedRegenerationExecutionSchemaVersion,
      .plan_checksum = plan_checksum,
      .config = config,
      .store_config = store_config,
      .candidate_store_publication_committed = candidate_store_publication_committed,
      .counters = counters,
      .columns = std::move(columns),
  };
  observation.observation_checksum =
      internal::ComputeTargetedRegenerationFailedObservationChecksumV5(
          plan_checksum, config, store_config, candidate_store_publication_committed, code,
          counters, observation.columns);
  return TargetedRegenerationExecutionError{
      .code = code,
      .invariant_id = invariant_id,
      .detail = detail,
      .failed_execution = std::move(observation),
  };
}

struct PreparedTargetExecution {
  const TargetedRegenerationNet* target = nullptr;
  const PreparedNetRoutingContext* context = nullptr;
  std::uint64_t batch_identity = 0;
  std::uint64_t first_global_column = 0;
  std::vector<routing::NormalizedCandidateGenerationPolicy> policies;
};

struct FailedExecutionState {
  bool query_started = false;
  bool candidate_store_publication_committed = false;
  TargetedRegenerationExecutionCounters counters;
  std::vector<TargetedRegenerationColumnRecord> columns;
};

[[nodiscard]] TargetedRegenerationExecutionResult WithFailureEnvelope(
    auto&& operation, FailedExecutionState& failure_state, std::uint64_t plan_checksum,
    const TargetedRegenerationExecutionConfig& config,
    const candidates::CandidateStoreConfig& store_config) {
  try {
    return operation();
  } catch (const std::bad_alloc&) {
    if (failure_state.query_started) {
      return ErrorWithFailedExecution(
          TargetedRegenerationExecutionErrorCode::kResourceExhausted,
          "allocator.targeted_regeneration_execution.host_memory.v2",
          "Host allocation failed after targeted-regeneration query execution began", plan_checksum,
          config, store_config, failure_state.candidate_store_publication_committed,
          failure_state.counters, std::move(failure_state.columns));
    }
    return Error(TargetedRegenerationExecutionErrorCode::kResourceExhausted,
                 "allocator.targeted_regeneration_execution.host_memory.v1",
                 "Host allocation failed within targeted-regeneration execution bounds");
  } catch (const std::length_error&) {
    if (failure_state.query_started) {
      return ErrorWithFailedExecution(
          TargetedRegenerationExecutionErrorCode::kResourceExhausted,
          "allocator.targeted_regeneration_execution.host_container.v2",
          "Host container limits were exhausted after targeted-regeneration query execution began",
          plan_checksum, config, store_config, failure_state.candidate_store_publication_committed,
          failure_state.counters, std::move(failure_state.columns));
    }
    return Error(TargetedRegenerationExecutionErrorCode::kResourceExhausted,
                 "allocator.targeted_regeneration_execution.host_container.v1",
                 "Host container limits were exhausted within execution bounds");
  } catch (...) {
    if (failure_state.query_started) {
      return ErrorWithFailedExecution(
          TargetedRegenerationExecutionErrorCode::kInternalInvariant,
          "allocator.targeted_regeneration_execution.unexpected_exception.v4",
          "An unexpected exception escaped after targeted-regeneration query execution began",
          plan_checksum, config, store_config, failure_state.candidate_store_publication_committed,
          failure_state.counters, std::move(failure_state.columns));
    }
    return Error(TargetedRegenerationExecutionErrorCode::kInternalInvariant,
                 "allocator.targeted_regeneration_execution.unexpected_exception.v4",
                 "An unexpected exception escaped before targeted-regeneration query execution");
  }
}

}  // namespace

bool internal::TargetedRegenerationExecutionConfigIsValidV4(
    const TargetedRegenerationExecutionConfig& config) noexcept {
  return ConfigIsValid(config);
}

bool internal::TargetedRegenerationExecutionConfigIsValidV5(
    const TargetedRegenerationExecutionConfig& config) noexcept {
  return ConfigIsValid(config);
}

std::optional<std::uint64_t> internal::ComputeTargetedRegenerationMaximumDraftBytesV2(
    std::uint64_t maximum_reconstruction_states,
    std::uint64_t maximum_policy_resource_entries) noexcept {
  const UWide bytes = 4'096U + static_cast<UWide>(512U) * maximum_reconstruction_states +
                      static_cast<UWide>(128U) * maximum_policy_resource_entries;
  return bytes <= std::numeric_limits<std::uint64_t>::max()
             ? std::optional<std::uint64_t>(static_cast<std::uint64_t>(bytes))
             : std::nullopt;
}

std::optional<std::uint64_t> internal::ComputeTargetedRegenerationGeneratedBytesV2(
    std::uint64_t route_queries, std::uint64_t maximum_candidate_draft_bytes) noexcept {
  const UWide bytes = static_cast<UWide>(route_queries) * maximum_candidate_draft_bytes;
  return bytes <= std::numeric_limits<std::uint64_t>::max()
             ? std::optional<std::uint64_t>(static_cast<std::uint64_t>(bytes))
             : std::nullopt;
}

std::optional<std::uint64_t> internal::ComputeTargetedRegenerationAdmissionInputBytesV2(
    std::uint64_t route_queries, std::uint64_t maximum_candidate_draft_bytes,
    std::uint64_t aggregate_policy_resource_entries) noexcept {
  const UWide bytes = static_cast<UWide>(route_queries) *
                          (static_cast<UWide>(maximum_candidate_draft_bytes) + 57U) +
                      static_cast<UWide>(29U) * aggregate_policy_resource_entries;
  return bytes <= std::numeric_limits<std::uint64_t>::max()
             ? std::optional<std::uint64_t>(static_cast<std::uint64_t>(bytes))
             : std::nullopt;
}

std::optional<std::uint64_t> internal::ComputeTargetedRegenerationAdmissionWorkV2(
    std::uint64_t route_queries, std::uint64_t maximum_reconstruction_states,
    std::uint64_t aggregate_policy_resource_entries, std::uint64_t obstacle_count,
    std::uint64_t terminal_count) noexcept {
  const UWide primitives = std::min<std::uint64_t>(maximum_reconstruction_states,
                                                   candidates::kMaximumCandidatePrimitives);
  const UWide primitive_pairs = primitives * (primitives - (primitives == 0 ? 0U : 1U)) / 2U;
  const UWide resource_spans = std::min<std::uint64_t>(maximum_reconstruction_states,
                                                       candidates::kMaximumCandidateResourceSpans);
  const UWide per_query = 1U + primitives + primitive_pairs +
                          primitives * (static_cast<UWide>(obstacle_count) + terminal_count) +
                          resource_spans + static_cast<UWide>(2U) * maximum_reconstruction_states;
  const UWide work = static_cast<UWide>(route_queries) * per_query +
                     static_cast<UWide>(2U) * aggregate_policy_resource_entries;
  return work <= std::numeric_limits<std::uint64_t>::max()
             ? std::optional<std::uint64_t>(static_cast<std::uint64_t>(work))
             : std::nullopt;
}

std::optional<std::uint64_t> internal::ComputeTargetedRegenerationPolicyProjectionVisitsV2(
    std::uint64_t target_count, std::uint64_t price_count) noexcept {
  const UWide visits = static_cast<UWide>(target_count) * price_count *
                       kTargetedRegenerationPolicyProjectionPassesV2;
  return visits <= std::numeric_limits<std::uint64_t>::max()
             ? std::optional<std::uint64_t>(static_cast<std::uint64_t>(visits))
             : std::nullopt;
}

std::optional<std::uint64_t> internal::ComputeTargetedRegenerationRejectionBytesV2(
    std::uint64_t route_queries) noexcept {
  const UWide bytes =
      static_cast<UWide>(route_queries) * kMaximumTargetedRegenerationRejectionLogicalBytesV2;
  return bytes <= std::numeric_limits<std::uint64_t>::max()
             ? std::optional<std::uint64_t>(static_cast<std::uint64_t>(bytes))
             : std::nullopt;
}

std::optional<std::uint64_t> internal::ComputeTargetedRegenerationTransientResultBytesV2(
    std::uint64_t route_queries) noexcept {
  const UWide per_column =
      kTargetedRegenerationColumnBaseLogicalBytesV2 +
      static_cast<UWide>(2U) * kMaximumTargetedRegenerationRejectionLogicalBytesV2;
  const UWide bytes = static_cast<UWide>(route_queries) * per_column;
  return bytes <= std::numeric_limits<std::uint64_t>::max()
             ? std::optional<std::uint64_t>(static_cast<std::uint64_t>(bytes))
             : std::nullopt;
}

std::optional<std::uint64_t> internal::ComputeTargetedRegenerationColumnLogicalBytesV2(
    const TargetedRegenerationColumnRecord& column) noexcept {
  UWide bytes = kTargetedRegenerationColumnBaseLogicalBytesV2;
  if (column.rejection.has_value()) {
    bytes += column.rejection->logical_bytes;
  }
  return bytes <= std::numeric_limits<std::uint64_t>::max()
             ? std::optional<std::uint64_t>(static_cast<std::uint64_t>(bytes))
             : std::nullopt;
}

std::uint64_t internal::TargetedRegenerationRouteQueriesForTesting() noexcept {
  return g_route_queries_started;
}

std::uint64_t internal::TargetedRegenerationSourceResourceSpanVisitsForTesting() noexcept {
  return g_source_resource_span_visits;
}

void internal::SetTargetedRegenerationPostQueryHostFailureForTesting(
    std::optional<std::uint64_t> countdown,
    TargetedRegenerationHostFailureForTesting failure) noexcept {
  g_post_query_host_failure_countdown = countdown;
  g_post_query_host_failure = failure;
}

void internal::SetTargetedRegenerationRequestPreparationHostFailureForTesting(
    std::optional<std::uint64_t> countdown,
    TargetedRegenerationHostFailureForTesting failure) noexcept {
  g_request_preparation_host_failure_countdown = countdown;
  g_request_preparation_host_failure = failure;
}

void internal::SetTargetedRegenerationRejectionEvidenceHostFailureForTesting(
    std::optional<TargetedRegenerationRejectionEvidenceBoundaryForTesting> boundary,
    TargetedRegenerationHostFailureForTesting failure) noexcept {
  g_rejection_evidence_host_failure_boundary = boundary;
  g_rejection_evidence_host_failure = failure;
}

void internal::SetTargetedRegenerationRouteFailureForTesting(bool enabled) noexcept {
  g_route_failure_for_testing = enabled;
}

void internal::SetTargetedRegenerationBuildRejectionForTesting(bool enabled) noexcept {
  g_build_rejection_for_testing = enabled;
}

void internal::SetTargetedRegenerationPostPublicationHostFailureForTesting(
    std::optional<TargetedRegenerationHostFailureForTesting> failure) noexcept {
  g_post_publication_host_failure = failure;
}

void internal::SetTargetedRegenerationSuccessorLeaseFailureForTesting(bool enabled) noexcept {
  g_successor_lease_failure_for_testing = enabled;
}

std::uint64_t internal::ComputeTargetedRegenerationExecutionChecksumV4(
    const TargetedRegenerationExecutionChecksumHeaderV4& header,
    std::span<const TargetedRegenerationColumnRecord> columns) noexcept {
  board_ir::StableHashBuilder hash;
  hash.AddString("APGAR-TARGETED-REGENERATION-EXECUTION-V4");
  hash.AddU32(header.schema_version);
  hash.AddU64(header.plan_checksum);
  AddExecutionConfig(hash, header.config);
  AddStoreConfig(hash, header.store_config);
  hash.AddU64(header.refreshed_request_manifest_checksum);
  hash.AddU64(header.refreshed_candidate_pool_manifest_checksum);
  hash.AddU64(header.baseline_world_checksum);
  hash.AddU64(header.refreshed_world_checksum);
  hash.AddByte(static_cast<std::uint8_t>(header.disposition));
  hash.AddByte(static_cast<std::uint8_t>(header.terminal_reason));
  AddCounters(hash, header.counters);
  AddColumns(hash, columns);
  return hash.Finish();
}

std::uint64_t internal::ComputeTargetedRegenerationFailedObservationChecksumV4(
    std::uint64_t plan_checksum, const TargetedRegenerationExecutionConfig& config,
    const candidates::CandidateStoreConfig& store_config,
    bool candidate_store_publication_committed, TargetedRegenerationExecutionErrorCode error_code,
    const TargetedRegenerationExecutionCounters& counters,
    std::span<const TargetedRegenerationColumnRecord> columns) noexcept {
  board_ir::StableHashBuilder hash;
  hash.AddString("APGAR-TARGETED-REGENERATION-FAILED-EXECUTION-V4");
  hash.AddU32(kTargetedRegenerationExecutionSchemaVersionV4);
  hash.AddU64(plan_checksum);
  AddExecutionConfig(hash, config);
  AddStoreConfig(hash, store_config);
  hash.AddBool(candidate_store_publication_committed);
  hash.AddByte(static_cast<std::uint8_t>(error_code));
  AddCounters(hash, counters);
  AddColumns(hash, columns);
  return hash.Finish();
}

std::uint64_t internal::ComputeTargetedRegenerationExecutionChecksumV5(
    const TargetedRegenerationExecutionChecksumHeaderV5& header,
    std::span<const TargetedRegenerationColumnRecord> columns) noexcept {
  board_ir::StableHashBuilder hash;
  hash.AddString("APGAR-TARGETED-REGENERATION-EXECUTION-V5");
  hash.AddU32(header.schema_version);
  hash.AddU64(header.plan_checksum);
  AddExecutionConfig(hash, header.config);
  AddStoreConfig(hash, header.store_config);
  hash.AddU64(header.refreshed_request_manifest_checksum);
  hash.AddU64(header.refreshed_candidate_pool_manifest_checksum);
  hash.AddU64(header.baseline_world_checksum);
  hash.AddU64(header.refreshed_world_checksum);
  hash.AddByte(static_cast<std::uint8_t>(header.disposition));
  hash.AddByte(static_cast<std::uint8_t>(header.terminal_reason));
  AddCounters(hash, header.counters);
  AddColumns(hash, columns);
  return hash.Finish();
}

std::uint64_t internal::ComputeTargetedRegenerationFailedObservationChecksumV5(
    std::uint64_t plan_checksum, const TargetedRegenerationExecutionConfig& config,
    const candidates::CandidateStoreConfig& store_config,
    bool candidate_store_publication_committed, TargetedRegenerationExecutionErrorCode error_code,
    const TargetedRegenerationExecutionCounters& counters,
    std::span<const TargetedRegenerationColumnRecord> columns) noexcept {
  board_ir::StableHashBuilder hash;
  hash.AddString("APGAR-TARGETED-REGENERATION-FAILED-EXECUTION-V5");
  hash.AddU32(kTargetedRegenerationExecutionSchemaVersion);
  hash.AddU64(plan_checksum);
  AddExecutionConfig(hash, config);
  AddStoreConfig(hash, store_config);
  hash.AddBool(candidate_store_publication_committed);
  hash.AddByte(static_cast<std::uint8_t>(error_code));
  AddCounters(hash, counters);
  AddColumns(hash, columns);
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

template <bool CaptureOperationalProfile>
TargetedRegenerationExecutionResult ExecuteTargetedRegenerationPlanCpuImpl(
    std::uint32_t schema_version, const board_ir::BoardSnapshot& board,
    const OneWorldAllocationRequest& source_request, TargetedRegenerationPlan&& plan,
    candidates::CandidateStore& candidate_store, const TargetedRegenerationExecutionConfig& config,
    TargetedRegenerationOperationalProfileV1* operational_profile) {
  ::apgar::internal::OperationalTimestamp<CaptureOperationalProfile, OperationalClock>
      validation_start;
  if constexpr (CaptureOperationalProfile) {
    *operational_profile = {};
    validation_start =
        ::apgar::internal::OperationalNow<CaptureOperationalProfile, OperationalClock>();
  }
  g_route_queries_started = 0;
  g_source_resource_span_visits = 0;
  if (schema_version != kTargetedRegenerationExecutionSchemaVersion) {
    return Error(TargetedRegenerationExecutionErrorCode::kUnsupportedSchema,
                 "allocator.targeted_regeneration_execution.schema.v5",
                 "Targeted-regeneration execution schema is unsupported");
  }
  if (!ConfigIsValid(config)) {
    return Error(TargetedRegenerationExecutionErrorCode::kInvalidConfiguration,
                 "allocator.targeted_regeneration_execution.configuration.v5",
                 "Targeted-regeneration execution configuration is outside schema-v5 bounds");
  }
  if (plan.schema_version() != kTargetedRegenerationPlanSchemaVersion) {
    return Error(TargetedRegenerationExecutionErrorCode::kUnsupportedSchema,
                 "allocator.targeted_regeneration_execution.plan_schema.v5",
                 "Targeted-regeneration execution requires a schema-v2 plan");
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

  FailedExecutionState failure_state;
  const std::uint64_t plan_checksum = plan.plan_checksum();
  const candidates::CandidateStoreConfig store_config = candidate_store.config();
  return WithFailureEnvelope(
      [&]() -> TargetedRegenerationExecutionResult {
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
        if (source_request.pools.size() != plan.source_pool_count()) {
          return Error(TargetedRegenerationExecutionErrorCode::kSourceRequestDrift,
                       "allocator.targeted_regeneration_execution.source_pool_count.v2",
                       "The source pool count differs from the planned input");
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
        if (source_candidate_count != plan.source_candidate_count()) {
          return Error(TargetedRegenerationExecutionErrorCode::kSourceRequestDrift,
                       "allocator.targeted_regeneration_execution.source_candidate_count.v2",
                       "The source candidate count differs from the planned input");
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

        UWide source_expanded_resource_uses = 0;
        if (!resource_refinement_required) {
          const OneWorldAllocatorLimits& allocator_limits = source_request.limits;
          const UWide refreshed_candidate_count =
              source_candidate_count + plan.total_requested_columns();
          const UWide refreshed_resource_records =
              static_cast<UWide>(source_request.capacities.overrides().size()) +
              plan.price_state().prices().size();
          if (source_request.pools.size() > allocator_limits.maximum_nets ||
              refreshed_resource_records > allocator_limits.maximum_resource_records) {
            return Error(
                TargetedRegenerationExecutionErrorCode::kWorkBoundExceeded,
                "allocator.targeted_regeneration_execution.refreshed_static_budget.v2",
                "Refreshed One-World pool or resource-record inputs exceed allocator bounds");
          }
          if (refreshed_candidate_count > allocator_limits.maximum_candidates) {
            return Error(TargetedRegenerationExecutionErrorCode::kWorkBoundExceeded,
                         "allocator.targeted_regeneration_execution.refreshed_candidate_budget.v2",
                         "Source candidates plus every possible novel column exceed the refreshed "
                         "One-World candidate bound");
          }
          const UWide generated_expanded_resource_uses =
              static_cast<UWide>(plan.total_requested_columns()) *
              config.route_limits.maximum_reconstruction_states;
          if (generated_expanded_resource_uses > allocator_limits.maximum_expanded_resource_uses) {
            return Error(
                TargetedRegenerationExecutionErrorCode::kWorkBoundExceeded,
                "allocator.targeted_regeneration_execution.refreshed_expanded_resource_budget.v2",
                "Generated route envelopes exceed the refreshed One-World expanded-resource bound");
          }
          const UWide maximum_source_expanded_resource_uses =
              static_cast<UWide>(allocator_limits.maximum_expanded_resource_uses) -
              generated_expanded_resource_uses;
          for (const CandidatePool& pool : source_request.pools) {
            for (const candidates::StoredCandidate& candidate : pool.candidates) {
              if (candidate == nullptr) {
                return Error(TargetedRegenerationExecutionErrorCode::kSourceRequestDrift,
                             "allocator.targeted_regeneration_execution.source_candidate.v2",
                             "The source request contains a null immutable candidate handle");
              }
              for (const candidates::PhysicalEdgeSpan& span : candidate->data().resources) {
                ++g_source_resource_span_visits;
                source_expanded_resource_uses += span.edge_count;
                if (source_expanded_resource_uses > maximum_source_expanded_resource_uses) {
                  return Error(
                      TargetedRegenerationExecutionErrorCode::kWorkBoundExceeded,
                      "allocator.targeted_regeneration_execution.refreshed_expanded_resource_"
                      "budget.v2",
                      "Source footprints plus every bounded generated route exceed the refreshed "
                      "One-World expanded-resource bound");
                }
              }
            }
          }
        }

        std::uint64_t projected_policy_visits = 0;
        if (!resource_refinement_required) {
          const auto visits = internal::ComputeTargetedRegenerationPolicyProjectionVisitsV2(
              plan.targets().size(), plan.price_state().prices().size());
          if (!visits.has_value() || *visits > config.maximum_policy_projection_visits) {
            return Error(
                TargetedRegenerationExecutionErrorCode::kWorkBoundExceeded,
                "allocator.targeted_regeneration_execution.policy_projection_visit_budget.v2",
                "Declared full price-roster projection passes exceed the semantic visit bound");
          }
          projected_policy_visits = *visits;
        }

        UWide projected_policy_entries = 0;
        UWide maximum_policy_entries_per_candidate = 0;
        std::uint64_t preflight_column_count = 0;
        for (std::size_t target_index = 0;
             !resource_refinement_required && target_index < plan.targets().size();
             ++target_index) {
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
          if (preflight_column_count > std::numeric_limits<std::uint32_t>::max() ||
              target.requested_columns >
                  static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()) -
                      preflight_column_count + 1U) {
            return Error(TargetedRegenerationExecutionErrorCode::kWorkBoundExceeded,
                         "allocator.targeted_regeneration_execution.ordinal_budget.v1",
                         "Candidate ordinals exceed the schema-v1 unsigned 32-bit domain");
          }
          const internal::TargetedRegenerationPolicyEntryProjectionV1 entry_projection =
              internal::ProjectTargetedRegenerationPolicyEntriesV1(
                  context->compiled_board, plan.price_state().prices(), target);
          if (!internal::TargetedRegenerationPolicyEntriesFitV1(
                  entry_projection.aggregate_entry_count, routing::kMaximumPolicyResourceEntries) ||
              entry_projection.aggregate_entry_count >
                  config.maximum_policy_resource_entries - projected_policy_entries) {
            return Error(TargetedRegenerationExecutionErrorCode::kWorkBoundExceeded,
                         "allocator.targeted_regeneration_execution.policy_entry_budget.v1",
                         "Synthesized policies exceed the aggregate resource-entry budget");
          }
          projected_policy_entries += entry_projection.aggregate_entry_count;
          maximum_policy_entries_per_candidate =
              std::max(maximum_policy_entries_per_candidate,
                       static_cast<UWide>(entry_projection.target_legal_price_count) +
                           (target.requested_columns > 1U ? 1U : 0U));
          preflight_column_count += target.requested_columns;
        }
        if (!resource_refinement_required &&
            preflight_column_count != plan.total_requested_columns()) {
          return Error(TargetedRegenerationExecutionErrorCode::kPlanInvariant,
                       "allocator.targeted_regeneration_execution.preflight_column_count.v1",
                       "Target columns differ from the plan's aggregate column count");
        }
        if (!resource_refinement_required) {
          const UWide route_work =
              static_cast<UWide>(preflight_column_count) * config.route_limits.maximum_work_units;
          if (route_work > config.maximum_total_route_work_units) {
            return Error(TargetedRegenerationExecutionErrorCode::kWorkBoundExceeded,
                         "allocator.targeted_regeneration_execution.route_work_budget.v2",
                         "Worst-case bounded CPU A* work exceeds the aggregate execution bound");
          }
          if (preflight_column_count != 0) {
            const auto draft_bytes = internal::ComputeTargetedRegenerationMaximumDraftBytesV2(
                config.route_limits.maximum_reconstruction_states,
                static_cast<std::uint64_t>(maximum_policy_entries_per_candidate));
            if (!draft_bytes.has_value() || *draft_bytes > config.maximum_candidate_draft_bytes) {
              return Error(
                  TargetedRegenerationExecutionErrorCode::kWorkBoundExceeded,
                  "allocator.targeted_regeneration_execution.draft_byte_budget.v2",
                  "Worst-case bounded route and policy exceed the candidate draft-byte cap");
            }
          }
          const auto generated_bytes = internal::ComputeTargetedRegenerationGeneratedBytesV2(
              preflight_column_count, config.maximum_candidate_draft_bytes);
          if (!generated_bytes.has_value() ||
              *generated_bytes > config.maximum_generated_candidate_bytes) {
            return Error(TargetedRegenerationExecutionErrorCode::kWorkBoundExceeded,
                         "allocator.targeted_regeneration_execution.generated_byte_budget.v2",
                         "Worst-case unpublished candidate drafts exceed the generated-byte cap");
          }
          const auto rejection_bytes =
              internal::ComputeTargetedRegenerationRejectionBytesV2(preflight_column_count);
          if (!rejection_bytes.has_value() || *rejection_bytes > config.maximum_rejection_bytes) {
            return Error(TargetedRegenerationExecutionErrorCode::kWorkBoundExceeded,
                         "allocator.targeted_regeneration_execution.rejection_byte_budget.v2",
                         "Worst-case canonical rejection records exceed the rejection-byte cap");
          }
          const auto transient_result_bytes =
              internal::ComputeTargetedRegenerationTransientResultBytesV2(preflight_column_count);
          if (!transient_result_bytes.has_value() ||
              *transient_result_bytes > config.maximum_transient_result_bytes) {
            return Error(
                TargetedRegenerationExecutionErrorCode::kWorkBoundExceeded,
                "allocator.targeted_regeneration_execution.transient_result_byte_budget.v2",
                "Worst-case columns and rejection copies exceed the transient-result cap");
          }
          const candidates::CandidateStoreConfig& store_config = candidate_store.config();
          const auto admission_input = internal::ComputeTargetedRegenerationAdmissionInputBytesV2(
              preflight_column_count, config.maximum_candidate_draft_bytes,
              static_cast<std::uint64_t>(projected_policy_entries));
          if (!admission_input.has_value() ||
              *admission_input > store_config.maximum_admission_input_bytes_per_transaction) {
            return Error(TargetedRegenerationExecutionErrorCode::kWorkBoundExceeded,
                         "allocator.targeted_regeneration_execution.store_input_byte_budget.v2",
                         "CandidateStore cannot retain and re-account the worst-case invocation");
          }
          const auto admission_work = internal::ComputeTargetedRegenerationAdmissionWorkV2(
              preflight_column_count, config.route_limits.maximum_reconstruction_states,
              static_cast<std::uint64_t>(projected_policy_entries), board.data().obstacles.size(),
              board.data().terminals.size());
          if (!admission_work.has_value() ||
              *admission_work > store_config.maximum_admission_work_units_per_transaction) {
            return Error(TargetedRegenerationExecutionErrorCode::kWorkBoundExceeded,
                         "allocator.targeted_regeneration_execution.store_work_budget.v2",
                         "CandidateStore cannot exactly validate the worst-case invocation");
          }
          const UWide invocation_rejections =
              preflight_column_count == 0 ? 0 : source_candidate_count + preflight_column_count;
          const UWide retained_rejections =
              static_cast<UWide>(candidate_store.RejectionCount()) + invocation_rejections;
          if (retained_rejections > store_config.maximum_rejection_records) {
            return Error(
                TargetedRegenerationExecutionErrorCode::kWorkBoundExceeded,
                "allocator.targeted_regeneration_execution.store_rejection_budget.v2",
                "CandidateStore cannot retain complete existing and invocation diagnostics");
          }
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
          return Error(
              TargetedRegenerationExecutionErrorCode::kSourceRequestDrift,
              "allocator.targeted_regeneration_execution.source_manifest.v1",
              "The complete source request or candidate pools differ from the planned input");
        }

        ::apgar::internal::OperationalTimestamp<CaptureOperationalProfile, OperationalClock>
            baseline_preflight_start;
        if constexpr (CaptureOperationalProfile) {
          operational_profile->validation_and_preflight_wall_nanoseconds =
              OperationalElapsed(validation_start);
          baseline_preflight_start =
              ::apgar::internal::OperationalNow<CaptureOperationalProfile, OperationalClock>();
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
        if (!std::holds_alternative<internal::OneWorldSelectionEvidence>(
                baseline_evidence_result)) {
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
        if (const auto* failure =
                std::get_if<candidates::CandidateStoreError>(&source_store_result);
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

        ::apgar::internal::OperationalTimestamp<CaptureOperationalProfile, OperationalClock>
            generation_start;
        if constexpr (CaptureOperationalProfile) {
          operational_profile->baseline_selection_and_source_store_preflight_wall_nanoseconds =
              OperationalElapsed(baseline_preflight_start);
          generation_start =
              ::apgar::internal::OperationalNow<CaptureOperationalProfile, OperationalClock>();
        }
        UWide policy_entries = 0;
        std::uint64_t prepared_column_count = 0;
        std::vector<PreparedTargetExecution> prepared_targets;
        std::vector<TargetedRegenerationColumnRecord>& columns = failure_state.columns;
        std::vector<candidates::CandidateInvocationItem> invocation_items;
        if (!resource_refinement_required) {
          prepared_targets.reserve(plan.targets().size());
          columns.reserve(static_cast<std::size_t>(plan.total_requested_columns()));
          invocation_items.reserve(static_cast<std::size_t>(plan.total_requested_columns()));
        }

        for (std::size_t target_index = 0;
             !resource_refinement_required && target_index < plan.targets().size();
             ++target_index) {
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
          const std::uint64_t batch_identity = NonZeroBatchIdentity(
              plan.plan_checksum(), target.net, target_index, config, candidate_store.config());
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
        if (!resource_refinement_required &&
            prepared_column_count != plan.total_requested_columns()) {
          return Error(TargetedRegenerationExecutionErrorCode::kInternalInvariant,
                       "allocator.targeted_regeneration_execution.preflight_column_count.v1",
                       "Prepared CPU policies differ from the plan's aggregate column count");
        }
        if (!resource_refinement_required && policy_entries != projected_policy_entries) {
          return Error(
              TargetedRegenerationExecutionErrorCode::kInternalInvariant,
              "allocator.targeted_regeneration_execution.policy_entry_replay.v1",
              "Policy synthesis differs from its allocation-free resource-entry preflight");
        }

        TargetedRegenerationExecutionCounters& counters = failure_state.counters;
        counters = TargetedRegenerationExecutionCounters{
            .requested_columns = plan.total_requested_columns(),
            .policy_projection_visits = projected_policy_visits,
        };
        const auto record_rejection = [&](TargetedRegenerationColumnRecord& column,
                                          const candidates::CandidateRejection& submitted) {
          candidates::CandidateRejection canonical =
              candidates::CanonicalizeCandidateRejectionV1(submitted);
          if (canonical.logical_bytes > kMaximumTargetedRegenerationRejectionLogicalBytesV2 ||
              canonical.code != column.rejection_code.value_or(canonical.code)) {
            return false;
          }
          if (column.rejection.has_value()) {
            return *column.rejection == canonical;
          }
          const UWide rejection_bytes =
              static_cast<UWide>(counters.rejection_record_bytes) + canonical.logical_bytes;
          const UWide transient_bytes = static_cast<UWide>(counters.transient_result_bytes) +
                                        static_cast<UWide>(2U) * canonical.logical_bytes;
          if (rejection_bytes > config.maximum_rejection_bytes ||
              transient_bytes > config.maximum_transient_result_bytes) {
            return false;
          }
          static_assert(
              std::is_nothrow_move_constructible_v<candidates::CandidateRejection>,
              "A fully staged rejection must move into its observation without another failure");
          column.rejection.emplace(std::move(canonical));
          counters.rejection_record_bytes = static_cast<std::uint64_t>(rejection_bytes);
          counters.transient_result_bytes = static_cast<std::uint64_t>(transient_bytes);
          column.rejection_code = column.rejection->code;
          return true;
        };
        std::uint64_t global_column = 0;
        for (const PreparedTargetExecution& prepared : prepared_targets) {
          const TargetedRegenerationNet& target = *prepared.target;
          const PreparedNetRoutingContext& context = *prepared.context;
          if (global_column != prepared.first_global_column) {
            if (failure_state.query_started) {
              return ErrorWithFailedExecution(
                  TargetedRegenerationExecutionErrorCode::kInternalInvariant,
                  "allocator.targeted_regeneration_execution.prepared_order.v2",
                  "Prepared target schedules are outside canonical plan order",
                  plan.plan_checksum(), config, candidate_store.config(), false, counters,
                  std::move(columns));
            }
            return Error(TargetedRegenerationExecutionErrorCode::kInternalInvariant,
                         "allocator.targeted_regeneration_execution.prepared_order.v1",
                         "Prepared target schedules are outside canonical plan order");
          }
          for (std::size_t column_index = 0; column_index < prepared.policies.size();
               ++column_index) {
            const routing::NormalizedCandidateGenerationPolicy& policy =
                prepared.policies[column_index];
            const std::uint64_t query_identity = global_column + 1U;
            const candidates::CandidateSchedulingIdentity scheduling{
                .batch_identity = prepared.batch_identity, .query_identity = query_identity};
            routing::PlanarRouteRequest request =
                internal::BuildTargetedRegenerationRouteRequestV1(context.request, policy.policy);
            MaybeFailAfterRequestPreparationForTesting();
            columns.push_back(TargetedRegenerationColumnRecord{
                .net = target.net,
                .column_index = column_index,
                .policy_identity = policy.identity,
                .batch_identity = prepared.batch_identity,
                .query_identity = query_identity,
                .route_telemetry = std::nullopt,
                .candidate_draft_logical_bytes = 0,
                .outcome = TargetedRegenerationColumnOutcome::kQueryInFlight,
                .candidate_id = std::nullopt,
                .candidate_payload_checksum = std::nullopt,
                .rejection_code = std::nullopt,
                .rejection = std::nullopt,
            });
            counters.transient_result_bytes += kTargetedRegenerationColumnBaseLogicalBytesV2;
            failure_state.query_started = true;
            ++g_route_queries_started;
            ++counters.route_queries;
            MaybeFailAfterQueryStartForTesting();
            routing::CpuRouteResult route_result = routing::RouteWithCpuAStar(
                board, context.compiled_board, request, config.route_limits);
            if (std::exchange(g_route_failure_for_testing, false)) {
              std::optional<routing::CpuRouteTelemetry> telemetry;
              if (const auto* route = std::get_if<routing::CpuRoute>(&route_result);
                  route != nullptr) {
                telemetry = route->telemetry;
              } else {
                telemetry = std::get<routing::RouteFailure>(route_result).telemetry;
              }
              route_result = routing::RouteFailure{
                  .code = routing::RouteFailureCode::kDisconnected,
                  .detail = {},
                  .obstacle = std::nullopt,
                  .telemetry = telemetry,
              };
            }
            if (std::holds_alternative<routing::CpuRoute>(route_result)) {
              columns.back().outcome = TargetedRegenerationColumnOutcome::kBuildInFlight;
              ++counters.successful_routes;
            }
            const routing::CpuRouteTelemetry* route_telemetry = nullptr;
            if (const auto* route = std::get_if<routing::CpuRoute>(&route_result);
                route != nullptr) {
              route_telemetry = &route->telemetry;
            } else {
              const routing::RouteFailure& failure = std::get<routing::RouteFailure>(route_result);
              if (failure.telemetry.has_value()) {
                route_telemetry = &*failure.telemetry;
              }
            }
            if (route_telemetry != nullptr) {
              columns.back().route_telemetry = *route_telemetry;
              const UWide runtime_work =
                  static_cast<UWide>(counters.route_work_units) + route_telemetry->work_units;
              if (runtime_work > config.maximum_total_route_work_units) {
                return ErrorWithFailedExecution(
                    TargetedRegenerationExecutionErrorCode::kInternalInvariant,
                    "allocator.targeted_regeneration_execution.runtime_route_work.v2",
                    "Bounded CPU A* telemetry exceeded its aggregate preflight envelope",
                    plan.plan_checksum(), config, candidate_store.config(), false, counters,
                    std::move(columns));
              }
              counters.route_work_units = static_cast<std::uint64_t>(runtime_work);
              counters.peak_route_record_count =
                  std::max(counters.peak_route_record_count, route_telemetry->peak_record_count);
              counters.peak_route_queue_size =
                  std::max(counters.peak_route_queue_size, route_telemetry->peak_queue_size);
            }
            if (const auto* failure = std::get_if<routing::RouteFailure>(&route_result);
                failure != nullptr) {
              if (failure->code == routing::RouteFailureCode::kDisconnected ||
                  failure->code == routing::RouteFailureCode::kUnsupportedLayerTransition ||
                  failure->code == routing::RouteFailureCode::kUnsupportedPolicy) {
                columns.back().outcome =
                    TargetedRegenerationColumnOutcome::kRejectionEvidenceInFlight;
                MaybeFailRejectionEvidenceForTesting(
                    internal::TargetedRegenerationRejectionEvidenceBoundaryForTesting::kRoute);
                const candidates::CandidateRejection rejection = RouteFailureRejection(
                    candidates::AssociationsFor(board, context.compiled_board), *failure,
                    target.net, policy, scheduling);
                if (!record_rejection(columns.back(), rejection)) {
                  return ErrorWithFailedExecution(
                      TargetedRegenerationExecutionErrorCode::kInternalInvariant,
                      "allocator.targeted_regeneration_execution.runtime_rejection_bytes.v2",
                      "Canonical route rejection exceeded its preflighted result-byte envelope",
                      plan.plan_checksum(), config, candidate_store.config(), false, counters,
                      std::move(columns));
                }
                columns.back().outcome = failure->code == routing::RouteFailureCode::kDisconnected
                                             ? TargetedRegenerationColumnOutcome::kRouteDisconnected
                                             : TargetedRegenerationColumnOutcome::kRouteUnsupported;
                ++counters.rejected_columns;
                invocation_items.emplace_back(rejection);
                ++global_column;
                continue;
              }
              const bool work_bound =
                  failure->code == routing::RouteFailureCode::kWorkBoundExceeded;
              const bool resource_exhausted =
                  failure->code == routing::RouteFailureCode::kResourceExhausted;
              return ErrorWithFailedExecution(
                  work_bound ? TargetedRegenerationExecutionErrorCode::kWorkBoundExceeded
                  : resource_exhausted
                      ? TargetedRegenerationExecutionErrorCode::kResourceExhausted
                      : TargetedRegenerationExecutionErrorCode::kCandidateGeneration,
                  work_bound ? "allocator.targeted_regeneration_execution.cpu_route_work_bound.v2"
                  : resource_exhausted
                      ? "allocator.targeted_regeneration_execution.cpu_route_resource.v2"
                      : "allocator.targeted_regeneration_execution.cpu_route.v2",
                  work_bound ? "Bounded CPU A* exhausted a declared per-query work or memory proxy"
                  : resource_exhausted
                      ? "CPU A* exhausted a non-work routing resource after preflight"
                      : "CPU A* failed after successful execution preflight",
                  plan.plan_checksum(), config, candidate_store.config(), false, counters,
                  std::move(columns));
            }
            routing::CpuRoute& route = std::get<routing::CpuRoute>(route_result);
            if (std::exchange(g_build_rejection_for_testing, false) ||
                g_rejection_evidence_host_failure_boundary ==
                    internal::TargetedRegenerationRejectionEvidenceBoundaryForTesting::kBuild) {
              route.producer_evidence = {};
            }
            candidates::CandidateDraftBuildResult draft_result =
                candidates::BuildGeneratedCandidateFromCpuRoute(board, context.compiled_board,
                                                                request, policy, route, scheduling);
            if (auto* rejection = std::get_if<candidates::CandidateRejection>(&draft_result);
                rejection != nullptr) {
              columns.back().outcome =
                  TargetedRegenerationColumnOutcome::kRejectionEvidenceInFlight;
              MaybeFailRejectionEvidenceForTesting(
                  internal::TargetedRegenerationRejectionEvidenceBoundaryForTesting::kBuild);
              if (!record_rejection(columns.back(), *rejection)) {
                return ErrorWithFailedExecution(
                    TargetedRegenerationExecutionErrorCode::kInternalInvariant,
                    "allocator.targeted_regeneration_execution.runtime_rejection_bytes.v2",
                    "Canonical build rejection exceeded its preflighted result-byte envelope",
                    plan.plan_checksum(), config, candidate_store.config(), false, counters,
                    std::move(columns));
              }
              columns.back().candidate_id = columns.back().rejection->candidate_id;
              columns.back().candidate_payload_checksum =
                  columns.back().rejection->candidate_payload_checksum;
              columns.back().outcome = TargetedRegenerationColumnOutcome::kBuildRejected;
              ++counters.rejected_columns;
              invocation_items.emplace_back(std::move(*rejection));
              ++global_column;
              continue;
            }
            candidates::GeneratedRouteCandidate draft =
                std::get<candidates::GeneratedRouteCandidate>(std::move(draft_result));
            columns.back().candidate_draft_logical_bytes = draft.logical_bytes;
            columns.back().outcome =
                TargetedRegenerationColumnOutcome::kGeneratedPendingPublication;
            ++counters.built_candidates;
            columns.back().candidate_id = draft.id;
            columns.back().candidate_payload_checksum = draft.payload_checksum;
            const UWide generated_bytes =
                static_cast<UWide>(counters.generated_candidate_bytes) + draft.logical_bytes;
            if (draft.logical_bytes > config.maximum_candidate_draft_bytes ||
                generated_bytes > config.maximum_generated_candidate_bytes) {
              return ErrorWithFailedExecution(
                  TargetedRegenerationExecutionErrorCode::kWorkBoundExceeded,
                  "allocator.targeted_regeneration_execution.runtime_generated_bytes.v2",
                  "A generated draft exceeded its preflighted logical-byte envelope",
                  plan.plan_checksum(), config, candidate_store.config(), false, counters,
                  std::move(columns));
            }
            counters.generated_candidate_bytes = static_cast<std::uint64_t>(generated_bytes);
            invocation_items.emplace_back(candidates::CandidateInvocationGeneratedItem{
                .compiled_board = std::cref(context.compiled_board),
                .request = std::move(request),
                .generated = std::move(draft),
            });
            ++global_column;
          }
        }
        if constexpr (CaptureOperationalProfile) {
          operational_profile->policy_projection_and_candidate_generation_wall_nanoseconds =
              OperationalElapsed(generation_start);
        }
        if (!resource_refinement_required &&
            (global_column != plan.total_requested_columns() || columns.size() != global_column)) {
          return ErrorWithFailedExecution(
              TargetedRegenerationExecutionErrorCode::kInternalInvariant,
              "allocator.targeted_regeneration_execution.column_count.v2",
              "Executed CPU columns differ from the plan's aggregate count", plan.plan_checksum(),
              config, candidate_store.config(), false, counters, std::move(columns));
        }

        ::apgar::internal::OperationalTimestamp<CaptureOperationalProfile, OperationalClock>
            publication_start;
        if constexpr (CaptureOperationalProfile) {
          publication_start =
              ::apgar::internal::OperationalNow<CaptureOperationalProfile, OperationalClock>();
        }
        candidates::CandidateStoreInvocationAdmissionResult publication_result =
            candidate_store.AdmitInvocationIfSourcePoolsMatch(board, std::move(expected_pools),
                                                              std::move(invocation_items));
        if constexpr (CaptureOperationalProfile) {
          operational_profile->exact_admission_and_store_publication_wall_nanoseconds =
              OperationalElapsed(publication_start);
        }
        if (const auto* failure = std::get_if<candidates::CandidateStoreError>(&publication_result);
            failure != nullptr) {
          return ErrorWithFailedExecution(
              failure->code == candidates::CandidateStoreErrorCode::kResourceExhausted
                  ? TargetedRegenerationExecutionErrorCode::kResourceExhausted
                  : TargetedRegenerationExecutionErrorCode::kCandidateStore,
              failure->code == candidates::CandidateStoreErrorCode::kStoreDrift
                  ? "allocator.targeted_regeneration_execution.store_drift.v2"
                  : "allocator.targeted_regeneration_execution.store_publication.v2",
              failure->code == candidates::CandidateStoreErrorCode::kStoreDrift
                  ? "CandidateStore pools changed since the plan's source request"
                  : "CandidateStore rejected the atomic invocation publication",
              plan.plan_checksum(), config, candidate_store.config(), false, counters,
              std::move(columns));
        }
        for (TargetedRegenerationColumnRecord& column : columns) {
          if (column.outcome == TargetedRegenerationColumnOutcome::kGeneratedPendingPublication) {
            column.outcome =
                TargetedRegenerationColumnOutcome::kPublicationCommittedOutcomeCorrelationPending;
          }
        }
        failure_state.candidate_store_publication_committed = true;
        MaybeFailAfterPublicationForTesting();

        ::apgar::internal::OperationalTimestamp<CaptureOperationalProfile, OperationalClock>
            correlation_start;
        if constexpr (CaptureOperationalProfile) {
          correlation_start =
              ::apgar::internal::OperationalNow<CaptureOperationalProfile, OperationalClock>();
        }
        std::vector<candidates::CandidateStoreAdmissionResult> publication =
            std::get<std::vector<candidates::CandidateStoreAdmissionResult>>(
                std::move(publication_result));
        if (publication.size() != columns.size()) {
          return ErrorWithFailedExecution(
              TargetedRegenerationExecutionErrorCode::kInternalInvariant,
              "allocator.targeted_regeneration_execution.publication_count.v2",
              "CandidateStore returned a different number of outcomes than submitted columns",
              plan.plan_checksum(), config, candidate_store.config(), true, counters,
              std::move(columns));
        }
        std::vector<bool> publication_seen(columns.size(), false);
        for (const candidates::CandidateStoreAdmissionResult& result : publication) {
          std::uint64_t query_identity = 0;
          if (const auto* stored = std::get_if<candidates::StoredCandidate>(&result);
              stored != nullptr) {
            query_identity = (*stored)->data().provenance.query_identity;
          } else {
            query_identity =
                std::get<candidates::CandidateRejection>(result).provenance.query_identity;
          }
          if (query_identity == 0 || query_identity > columns.size()) {
            return ErrorWithFailedExecution(
                TargetedRegenerationExecutionErrorCode::kInternalInvariant,
                "allocator.targeted_regeneration_execution.publication_query_range.v2",
                "CandidateStore returned an outcome outside the direct query-index domain",
                plan.plan_checksum(), config, candidate_store.config(), true, counters,
                std::move(columns));
          }
          const std::size_t column_index = static_cast<std::size_t>(query_identity - 1U);
          TargetedRegenerationColumnRecord& column = columns[column_index];
          if (column.query_identity != query_identity || publication_seen[column_index]) {
            return ErrorWithFailedExecution(
                TargetedRegenerationExecutionErrorCode::kInternalInvariant,
                "allocator.targeted_regeneration_execution.publication_correlation.v2",
                "CandidateStore returned a duplicate or misassociated direct query outcome",
                plan.plan_checksum(), config, candidate_store.config(), true, counters,
                std::move(columns));
          }
          publication_seen[column_index] = true;
          if (const auto* stored = std::get_if<candidates::StoredCandidate>(&result);
              stored != nullptr) {
            if (column.outcome !=
                TargetedRegenerationColumnOutcome::kPublicationCommittedOutcomeCorrelationPending) {
              return ErrorWithFailedExecution(
                  TargetedRegenerationExecutionErrorCode::kInternalInvariant,
                  "allocator.targeted_regeneration_execution.publication_stored_stage.v2",
                  "CandidateStore admitted a column that had not produced a candidate draft",
                  plan.plan_checksum(), config, candidate_store.config(), true, counters,
                  std::move(columns));
            }
            column.outcome = TargetedRegenerationColumnOutcome::kAdmitted;
            column.candidate_id = (*stored)->id();
            column.candidate_payload_checksum = (*stored)->data().payload_checksum;
            column.rejection_code.reset();
            ++counters.admitted_candidates;
          } else {
            const candidates::CandidateRejection& rejection =
                std::get<candidates::CandidateRejection>(result);
            const bool generated_outcome_pending =
                column.outcome ==
                TargetedRegenerationColumnOutcome::kPublicationCommittedOutcomeCorrelationPending;
            if (generated_outcome_pending) {
              column.outcome = TargetedRegenerationColumnOutcome::kRejectionEvidenceInFlight;
            }
            if (!record_rejection(column, rejection)) {
              return ErrorWithFailedExecution(
                  TargetedRegenerationExecutionErrorCode::kInternalInvariant,
                  "allocator.targeted_regeneration_execution.publication_rejection.v2",
                  "CandidateStore returned a rejection outside the canonical column envelope",
                  plan.plan_checksum(), config, candidate_store.config(), true, counters,
                  std::move(columns));
            }
            column.candidate_id = column.rejection->candidate_id;
            column.candidate_payload_checksum = column.rejection->candidate_payload_checksum;
            if (!generated_outcome_pending) {
              if (IsDuplicateCode(rejection.code) ||
                  (column.outcome != TargetedRegenerationColumnOutcome::kRouteDisconnected &&
                   column.outcome != TargetedRegenerationColumnOutcome::kRouteUnsupported &&
                   column.outcome != TargetedRegenerationColumnOutcome::kBuildRejected)) {
                return ErrorWithFailedExecution(
                    TargetedRegenerationExecutionErrorCode::kInternalInvariant,
                    "allocator.targeted_regeneration_execution.publication_rejection_stage.v2",
                    "CandidateStore changed a finalized prepublication rejection outcome",
                    plan.plan_checksum(), config, candidate_store.config(), true, counters,
                    std::move(columns));
              }
            } else if (IsDuplicateCode(rejection.code)) {
              column.outcome = TargetedRegenerationColumnOutcome::kDuplicate;
              ++counters.duplicate_candidates;
              ++counters.rejected_columns;
            } else {
              column.outcome = TargetedRegenerationColumnOutcome::kAdmissionRejected;
              ++counters.rejected_columns;
            }
          }
        }
        if (!std::ranges::all_of(publication_seen, [](bool seen) { return seen; })) {
          return ErrorWithFailedExecution(
              TargetedRegenerationExecutionErrorCode::kInternalInvariant,
              "allocator.targeted_regeneration_execution.publication_completeness.v2",
              "CandidateStore omitted a submitted direct query outcome", plan.plan_checksum(),
              config, candidate_store.config(), true, counters, std::move(columns));
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
          refreshed_pools.push_back(
              CandidatePool{.net = pool.net, .candidates = std::move(current)});
        }
        std::ranges::sort(refreshed_pools,
                          [](const CandidatePool& left, const CandidatePool& right) {
                            return NetBefore(left.net, right.net);
                          });

        if constexpr (CaptureOperationalProfile) {
          operational_profile->publication_correlation_wall_nanoseconds =
              OperationalElapsed(correlation_start);
        }
        ::apgar::internal::OperationalTimestamp<CaptureOperationalProfile, OperationalClock>
            refreshed_selection_start;
        if constexpr (CaptureOperationalProfile) {
          refreshed_selection_start =
              ::apgar::internal::OperationalNow<CaptureOperationalProfile, OperationalClock>();
        }
        OneWorldAllocationRequest refreshed_request = baseline_request;
        refreshed_request.pools = refreshed_pools;
        internal::OneWorldSelectionEvidenceResult refreshed_evidence_result =
            internal::SelectOneWorldWithoutAccounting(refreshed_request);
        if (!std::holds_alternative<internal::OneWorldSelectionEvidence>(
                refreshed_evidence_result)) {
          return ErrorWithFailedExecution(
              TargetedRegenerationExecutionErrorCode::kAllocation,
              "allocator.targeted_regeneration_execution.refreshed_request.v2",
              "The preflighted refreshed complete store pools cannot be selected",
              plan.plan_checksum(), config, candidate_store.config(), true, counters,
              std::move(columns));
        }
        const internal::OneWorldSelectionEvidence& refreshed_evidence =
            std::get<internal::OneWorldSelectionEvidence>(refreshed_evidence_result);
        OneWorldAllocationResult refreshed_world_result = AllocateOneWorld(refreshed_request);
        if (!std::holds_alternative<OneWorldAllocation>(refreshed_world_result)) {
          return ErrorWithFailedExecution(
              TargetedRegenerationExecutionErrorCode::kAllocation,
              "allocator.targeted_regeneration_execution.refreshed_world.v2",
              "The preflighted refreshed complete store pools cannot be allocated",
              plan.plan_checksum(), config, candidate_store.config(), true, counters,
              std::move(columns));
        }
        OneWorldAllocation refreshed_world =
            std::get<OneWorldAllocation>(std::move(refreshed_world_result));
        counters.changed_selections = ChangedSelectionCount(baseline_world, refreshed_world);

        if constexpr (CaptureOperationalProfile) {
          operational_profile->refreshed_selection_and_resource_accumulation_wall_nanoseconds =
              OperationalElapsed(refreshed_selection_start);
        }
        ::apgar::internal::OperationalTimestamp<CaptureOperationalProfile, OperationalClock>
            retention_start;
        if constexpr (CaptureOperationalProfile) {
          retention_start =
              ::apgar::internal::OperationalNow<CaptureOperationalProfile, OperationalClock>();
        }
        std::optional<candidates::CandidateStorePinLease> successor_lease;
        std::vector<candidates::CandidatePinRequest> pin_requests =
            WinnerPinRequests(refreshed_world);
        if (!pin_requests.empty()) {
          if (std::exchange(g_successor_lease_failure_for_testing, false)) {
            return ErrorWithFailedExecution(
                TargetedRegenerationExecutionErrorCode::kSuccessorLease,
                "allocator.targeted_regeneration_execution.successor_lease.v2",
                "Injected CandidateStore successor-lease handoff failure", plan.plan_checksum(),
                config, candidate_store.config(), true, counters, std::move(columns));
          }
          candidates::CandidateStorePinLeaseResult lease_result =
              candidate_store.AcquirePinLease(pin_requests);
          if (const auto* failure = std::get_if<candidates::CandidateStoreError>(&lease_result);
              failure != nullptr) {
            return ErrorWithFailedExecution(
                failure->code == candidates::CandidateStoreErrorCode::kResourceExhausted
                    ? TargetedRegenerationExecutionErrorCode::kResourceExhausted
                    : TargetedRegenerationExecutionErrorCode::kSuccessorLease,
                "allocator.targeted_regeneration_execution.successor_lease.v2",
                "CandidateStore could not atomically retain refreshed world selections",
                plan.plan_checksum(), config, candidate_store.config(), true, counters,
                std::move(columns));
          }
          successor_lease.emplace(
              std::get<candidates::CandidateStorePinLease>(std::move(lease_result)));
          counters.successor_pinned_candidates = pin_requests.size();
        }

        if constexpr (CaptureOperationalProfile) {
          operational_profile->successor_retention_wall_nanoseconds =
              OperationalElapsed(retention_start);
        }
        TargetedRegenerationExecutionDisposition disposition;
        TargetedRegenerationTerminalReason terminal_reason;
        if (resource_refinement_required) {
          disposition = TargetedRegenerationExecutionDisposition::kResourceRefinementRequired;
          terminal_reason = TargetedRegenerationTerminalReason::kResourceRefinementRequired;
        } else if (plan.targets().empty()) {
          disposition = TargetedRegenerationExecutionDisposition::kNoWork;
          terminal_reason = refreshed_world.total_overuse_units == 0 &&
                                    refreshed_world.no_candidate_net_count == 0
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

        ::apgar::internal::OperationalTimestamp<CaptureOperationalProfile, OperationalClock>
            final_start;
        if constexpr (CaptureOperationalProfile) {
          final_start =
              ::apgar::internal::OperationalNow<CaptureOperationalProfile, OperationalClock>();
        }
        const internal::TargetedRegenerationExecutionChecksumHeaderV5 checksum_header{
            .schema_version = schema_version,
            .plan_checksum = plan.plan_checksum(),
            .config = config,
            .store_config = candidate_store.config(),
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
            internal::ComputeTargetedRegenerationExecutionChecksumV5(checksum_header, columns);
        TargetedRegenerationExecution result(
            schema_version, std::move(plan), config, candidate_store.config(), disposition,
            terminal_reason, counters, std::move(columns), std::move(refreshed_pools),
            std::move(baseline_world), std::move(refreshed_world), checksum,
            std::move(successor_lease));
        if constexpr (CaptureOperationalProfile) {
          operational_profile->final_assembly_wall_nanoseconds = OperationalElapsed(final_start);
          operational_profile->plan_checksum = plan_checksum;
          operational_profile->execution_checksum = checksum;
          operational_profile->counters = result.counters();
          operational_profile->component_wall_nanoseconds = OperationalElapsed(validation_start);
          const UWide classified =
              static_cast<UWide>(operational_profile->validation_and_preflight_wall_nanoseconds) +
              operational_profile->baseline_selection_and_source_store_preflight_wall_nanoseconds +
              operational_profile->policy_projection_and_candidate_generation_wall_nanoseconds +
              operational_profile->exact_admission_and_store_publication_wall_nanoseconds +
              operational_profile->publication_correlation_wall_nanoseconds +
              operational_profile->refreshed_selection_and_resource_accumulation_wall_nanoseconds +
              operational_profile->successor_retention_wall_nanoseconds +
              operational_profile->final_assembly_wall_nanoseconds;
          operational_profile->unclassified_serial_wall_nanoseconds =
              classified <= operational_profile->component_wall_nanoseconds
                  ? operational_profile->component_wall_nanoseconds -
                        static_cast<std::uint64_t>(classified)
                  : 0;
        }
        return result;
      },
      failure_state, plan_checksum, config, store_config);
}

TargetedRegenerationExecutionResult ExecuteTargetedRegenerationPlanCpu(
    std::uint32_t schema_version, const board_ir::BoardSnapshot& board,
    const OneWorldAllocationRequest& source_request, TargetedRegenerationPlan&& plan,
    candidates::CandidateStore& candidate_store,
    const TargetedRegenerationExecutionConfig& config) {
  return ExecuteTargetedRegenerationPlanCpuImpl<false>(
      schema_version, board, source_request, std::move(plan), candidate_store, config, nullptr);
}

TargetedRegenerationExecutionResult ExecuteTargetedRegenerationPlanCpuWithOperationalProfileV1(
    std::uint32_t schema_version, const board_ir::BoardSnapshot& board,
    const OneWorldAllocationRequest& source_request, TargetedRegenerationPlan&& plan,
    candidates::CandidateStore& candidate_store, const TargetedRegenerationExecutionConfig& config,
    TargetedRegenerationOperationalProfileV1& operational_profile) {
  return ExecuteTargetedRegenerationPlanCpuImpl<true>(schema_version, board, source_request,
                                                      std::move(plan), candidate_store, config,
                                                      &operational_profile);
}

}  // namespace apgar::allocator
