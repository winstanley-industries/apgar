#include "apgar/allocator/multi_world.h"

#include <algorithm>
#include <chrono>
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
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

#include "apgar/board_ir/stable_hash.h"
#include "src/allocator/multi_world_internal.h"
#include "src/allocator/negotiated_prices_internal.h"
#include "src/allocator/one_world_internal.h"
#include "src/operational_timestamp.h"

namespace apgar::allocator {
namespace {

using UWide = __uint128_t;
using OperationalClock = std::chrono::steady_clock;

thread_local std::uint64_t g_preflight_span_inspections = 0;

[[nodiscard]] std::uint64_t OperationalElapsed(OperationalClock::time_point start) noexcept {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(OperationalClock::now() - start)
          .count());
}

void AccumulateOperationalElapsed(std::uint64_t& destination,
                                  OperationalClock::time_point start) noexcept {
  destination += OperationalElapsed(start);
}

[[nodiscard]] MultiWorldExecutionError Error(MultiWorldExecutionErrorCode code,
                                             std::string_view invariant_id,
                                             std::string_view detail) noexcept {
  return MultiWorldExecutionError{.code = code, .invariant_id = invariant_id, .detail = detail};
}

[[nodiscard]] bool ConfigIsValid(const MultiWorldExecutionConfig& config) noexcept {
  return config.maximum_worlds > 0 && config.maximum_worlds <= kMaximumMultiWorldsV1 &&
         config.maximum_total_selection_rounds > 0 &&
         config.maximum_total_selection_rounds <= kMaximumMultiWorldSelectionRoundsV1 &&
         config.maximum_total_candidate_evaluations > 0 &&
         config.maximum_total_candidate_evaluations <= kMaximumMultiWorldWorkItemsV1 &&
         config.maximum_total_candidate_span_visits > 0 &&
         config.maximum_total_candidate_span_visits <= kMaximumMultiWorldWorkItemsV1 &&
         config.maximum_total_resource_work_units > 0 &&
         config.maximum_total_resource_work_units <= kMaximumMultiWorldWorkItemsV1 &&
         config.maximum_total_net_outcomes > 0 &&
         config.maximum_total_net_outcomes <= kMaximumMultiWorldWorkItemsV1 &&
         config.maximum_pareto_comparisons > 0 &&
         config.maximum_pareto_comparisons <= kMaximumMultiWorldParetoComparisonsV1 &&
         config.maximum_buffered_terminal_selection_records > 0 &&
         config.maximum_buffered_terminal_selection_records <= kMaximumMultiWorldWorkItemsV1 &&
         config.maximum_buffered_terminal_resource_records > 0 &&
         config.maximum_buffered_terminal_resource_records <= kMaximumMultiWorldWorkItemsV1 &&
         config.maximum_buffered_terminal_price_records > 0 &&
         config.maximum_buffered_terminal_price_records <= kMaximumMultiWorldWorkItemsV1 &&
         config.maximum_retained_worlds > 0 &&
         config.maximum_retained_worlds <= kMaximumMultiWorldsV1 &&
         config.maximum_retained_selection_records > 0 &&
         config.maximum_retained_selection_records <= kMaximumMultiWorldWorkItemsV1 &&
         config.maximum_retained_resource_records > 0 &&
         config.maximum_retained_resource_records <= kMaximumMultiWorldWorkItemsV1 &&
         config.maximum_retained_price_records > 0 &&
         config.maximum_retained_price_records <= kMaximumMultiWorldWorkItemsV1 &&
         config.maximum_retained_winner_pins > 0 &&
         config.maximum_retained_winner_pins <= candidates::kMaximumPinLeaseItemsPerTransaction &&
         config.maximum_near_feasible_missing_nets <= kMaximumAllocatorNetsV1;
}

[[nodiscard]] AllocationAssociations AssociationsFor(const MultiNetWorkload& workload) noexcept {
  return AllocationAssociations{
      .board_content_hash = workload.board_content_hash(),
      .compiler_profile_fingerprint = workload.compiler_profile_fingerprint(),
      .geometry_compiler_version = workload.geometry_compiler_version(),
  };
}

[[nodiscard]] candidates::CandidateAssociations CandidateAssociationsFor(
    const MultiNetWorkload& workload, const PreparedNetRoutingContext& context) noexcept {
  return candidates::CandidateAssociations{
      .board_content_hash = workload.board_content_hash(),
      .compiler_profile_fingerprint = workload.compiler_profile_fingerprint(),
      .geometry_compiler_version = workload.geometry_compiler_version(),
      .routing_profile_fingerprint = context.routing_profile_fingerprint,
      .rule_bucket_identity = context.compiled_board.rule_bucket().identity,
  };
}

[[nodiscard]] OneWorldAllocationRequest BuildWorldRequest(const MultiWorldPoolSnapshot& source,
                                                          const PriceSnapshot& prices,
                                                          const MultiWorldSchedule& schedule) {
  return OneWorldAllocationRequest{
      .schema_version = kOneWorldAllocationSchemaVersion,
      .associations = AssociationsFor(*source.workload),
      .capacities = source.capacities,
      .prices = prices,
      .intrinsic_cost_weight = schedule.search_intrinsic_cost_weight,
      .limits = source.allocator_limits,
      .pools = source.pools,
      .workload = source.workload,
  };
}

[[nodiscard]] std::variant<std::vector<candidates::CandidateStoreExpectedPool>,
                           MultiWorldExecutionError>
BuildExpectedPools(const MultiWorldPoolSnapshot& source) {
  if (source.pools.size() != source.workload->nets().size()) {
    return Error(MultiWorldExecutionErrorCode::kSourcePoolInvalid,
                 "allocator.multi_world.source_pool_roster.v1",
                 "The source requires exactly one pool for every workload net");
  }
  std::vector<candidates::CandidateStoreExpectedPool> expected;
  expected.reserve(source.pools.size());
  std::set<std::pair<std::uint64_t, std::uint32_t>> unique_pool_nets;
  for (const CandidatePool& pool : source.pools) {
    if (!unique_pool_nets.emplace(pool.net.id, pool.net.generation).second) {
      return Error(MultiWorldExecutionErrorCode::kSourcePoolInvalid,
                   "allocator.multi_world.source_pool_roster.v1",
                   "The source contains more than one pool for a workload net");
    }
    const PreparedNetRoutingContext* context = source.workload->FindNet(pool.net);
    if (context == nullptr) {
      return Error(MultiWorldExecutionErrorCode::kSourcePoolInvalid,
                   "allocator.multi_world.source_pool_roster.v1",
                   "A source candidate pool is absent from the authentic workload");
    }
    const candidates::CandidateAssociations associations =
        CandidateAssociationsFor(*source.workload, *context);
    for (const candidates::StoredCandidate& candidate : pool.candidates) {
      if (candidate == nullptr || candidate->net() != pool.net ||
          candidate->data().associations != associations ||
          !internal::CandidateMatchesWorkloadRequestV1(*candidate, *context)) {
        return Error(MultiWorldExecutionErrorCode::kSourcePoolInvalid,
                     "allocator.multi_world.source_candidate_association.v1",
                     "A source candidate does not belong to its workload pool associations");
      }
    }
    expected.push_back(candidates::CandidateStoreExpectedPool{
        .net = pool.net,
        .associations = associations,
        .candidates = pool.candidates,
    });
  }
  return expected;
}

[[nodiscard]] std::vector<candidates::CandidatePinRequest> SourcePinRequests(
    const MultiWorldPoolSnapshot& source) {
  std::vector<candidates::CandidatePinRequest> requests;
  std::size_t count = 0;
  for (const CandidatePool& pool : source.pools) {
    count += pool.candidates.size();
  }
  requests.reserve(count);
  for (const CandidatePool& pool : source.pools) {
    for (const candidates::StoredCandidate& candidate : pool.candidates) {
      requests.push_back(candidates::CandidatePinRequest{
          .net = pool.net,
          .candidate_id = candidate->id(),
          .candidate_payload_checksum = candidate->data().payload_checksum,
          .expected_candidate = candidate,
      });
    }
  }
  return requests;
}

[[nodiscard]] MultiWorldExecutionError TranslateAllocationError(const AllocationError& error,
                                                                std::string_view invariant_id,
                                                                std::string_view detail) noexcept {
  return Error(
      error.code == AllocationErrorCode::kResourceExhausted
          ? MultiWorldExecutionErrorCode::kResourceExhausted
          : MultiWorldExecutionErrorCode::kAllocation,
      error.code == AllocationErrorCode::kResourceExhausted ? error.invariant_id : invariant_id,
      error.code == AllocationErrorCode::kResourceExhausted ? error.detail : detail);
}

[[nodiscard]] MultiWorldExecutionError TranslatePriceError(const NegotiatedPriceError& error,
                                                           std::string_view invariant_id,
                                                           std::string_view detail) noexcept {
  return Error(error.code == NegotiatedPriceErrorCode::kResourceExhausted
                   ? MultiWorldExecutionErrorCode::kResourceExhausted
                   : MultiWorldExecutionErrorCode::kPriceUpdate,
               error.code == NegotiatedPriceErrorCode::kResourceExhausted ? error.invariant_id
                                                                          : invariant_id,
               error.code == NegotiatedPriceErrorCode::kResourceExhausted ? error.detail : detail);
}

[[nodiscard]] MultiWorldExecutionError TranslateStoreError(
    const candidates::CandidateStoreError& error, std::string_view invariant_id,
    std::string_view detail) noexcept {
  return Error(error.code == candidates::CandidateStoreErrorCode::kResourceExhausted
                   ? MultiWorldExecutionErrorCode::kResourceExhausted
                   : MultiWorldExecutionErrorCode::kCandidateStoreLease,
               invariant_id,
               error.code == candidates::CandidateStoreErrorCode::kResourceExhausted
                   ? "CandidateStore exhausted host resources while acquiring a multi-world lease"
                   : detail);
}

[[nodiscard]] internal::MultiWorldObjectiveV1 ObjectiveFor(
    const MultiWorldSummary& summary) noexcept {
  return internal::MultiWorldObjectiveV1{
      .selected_net_count = summary.selected_net_count,
      .total_overuse_units = summary.total_overuse_units,
      .total_intrinsic_cost = summary.total_intrinsic_cost,
      .schedule_key = summary.schedule.schedule_key,
  };
}

struct CompletedWorld {
  MultiWorldSummary summary;
  NegotiatedPriceState price_state;
  OneWorldAllocation world;
};

[[nodiscard]] MultiWorldExecutionResult WithFailureEnvelope(auto&& operation) {
  try {
    return operation();
  } catch (const std::bad_alloc&) {
    return Error(MultiWorldExecutionErrorCode::kResourceExhausted,
                 "allocator.multi_world.host_memory.v1",
                 "Host allocation failed within configured multi-world bounds");
  } catch (const std::length_error&) {
    return Error(MultiWorldExecutionErrorCode::kResourceExhausted,
                 "allocator.multi_world.host_container.v1",
                 "Host container limits were exhausted within multi-world bounds");
  }
}

void AddSchedule(board_ir::StableHashBuilder& hash, const MultiWorldSchedule& schedule) noexcept {
  hash.AddU64(schedule.schedule_key);
  hash.AddU64(schedule.search_intrinsic_cost_weight);
  hash.AddU32(schedule.maximum_selection_rounds);
}

void AddConfig(board_ir::StableHashBuilder& hash,
               const MultiWorldExecutionConfig& config) noexcept {
  hash.AddU64(config.maximum_worlds);
  hash.AddU64(config.maximum_total_selection_rounds);
  hash.AddU64(config.maximum_total_candidate_evaluations);
  hash.AddU64(config.maximum_total_candidate_span_visits);
  hash.AddU64(config.maximum_total_resource_work_units);
  hash.AddU64(config.maximum_total_net_outcomes);
  hash.AddU64(config.maximum_pareto_comparisons);
  hash.AddU64(config.maximum_buffered_terminal_selection_records);
  hash.AddU64(config.maximum_buffered_terminal_resource_records);
  hash.AddU64(config.maximum_buffered_terminal_price_records);
  hash.AddU64(config.maximum_retained_worlds);
  hash.AddU64(config.maximum_retained_selection_records);
  hash.AddU64(config.maximum_retained_resource_records);
  hash.AddU64(config.maximum_retained_price_records);
  hash.AddU64(config.maximum_retained_winner_pins);
  hash.AddU64(config.maximum_near_feasible_missing_nets);
  hash.AddU64(config.maximum_near_feasible_overuse_units);
  hash.AddU64(config.known_unmapped_exact_conflict_count);
}

void AddCounters(board_ir::StableHashBuilder& hash,
                 const MultiWorldExecutionCounters& counters) noexcept {
  hash.AddU64(counters.scheduled_worlds);
  hash.AddU64(counters.completed_worlds);
  hash.AddU64(counters.selection_rounds);
  hash.AddU64(counters.price_updates);
  hash.AddU64(counters.candidate_evaluations);
  hash.AddU64(counters.candidate_span_visits);
  hash.AddU64(counters.resource_work_units);
  hash.AddU64(counters.net_outcomes);
  hash.AddU64(counters.pareto_comparisons);
  hash.AddU64(counters.pareto_eligible_worlds);
  hash.AddU64(counters.retained_worlds);
  hash.AddU64(counters.retained_winner_pins);
}

}  // namespace

bool internal::MultiWorldExecutionConfigIsValidV1(
    const MultiWorldExecutionConfig& config) noexcept {
  return ConfigIsValid(config);
}

bool internal::MultiWorldTerminalEnvelopeFitsV1(std::uint64_t world_count,
                                                std::uint64_t source_net_count,
                                                std::uint64_t maximum_source_candidate_count,
                                                std::uint64_t maximum_resource_records_per_world,
                                                std::uint64_t maximum_price_records_per_world,
                                                const MultiWorldExecutionConfig& config,
                                                MultiWorldTerminalEnvelopeV1* projection) noexcept {
  const UWide selection_records = static_cast<UWide>(world_count) * source_net_count;
  const UWide resource_records =
      static_cast<UWide>(world_count) * maximum_resource_records_per_world;
  const UWide price_records = static_cast<UWide>(world_count) * maximum_price_records_per_world;
  const UWide winner_pins = std::min<UWide>(maximum_source_candidate_count, selection_records);
  constexpr UWide kMax = std::numeric_limits<std::uint64_t>::max();
  if (world_count > config.maximum_worlds ||
      selection_records > config.maximum_buffered_terminal_selection_records ||
      resource_records > config.maximum_buffered_terminal_resource_records ||
      price_records > config.maximum_buffered_terminal_price_records ||
      world_count > config.maximum_retained_worlds ||
      selection_records > config.maximum_retained_selection_records ||
      resource_records > config.maximum_retained_resource_records ||
      price_records > config.maximum_retained_price_records ||
      winner_pins > config.maximum_retained_winner_pins || selection_records > kMax ||
      resource_records > kMax || price_records > kMax || winner_pins > kMax) {
    return false;
  }
  if (projection != nullptr) {
    *projection = MultiWorldTerminalEnvelopeV1{
        .buffered_selection_records = static_cast<std::uint64_t>(selection_records),
        .buffered_resource_records = static_cast<std::uint64_t>(resource_records),
        .buffered_price_records = static_cast<std::uint64_t>(price_records),
        .retained_worlds = world_count,
        .retained_selection_records = static_cast<std::uint64_t>(selection_records),
        .retained_resource_records = static_cast<std::uint64_t>(resource_records),
        .retained_price_records = static_cast<std::uint64_t>(price_records),
        .retained_winner_pins = static_cast<std::uint64_t>(winner_pins),
    };
  }
  return true;
}

std::uint64_t internal::MultiWorldPreflightSpanInspectionsForTesting() noexcept {
  return g_preflight_span_inspections;
}

bool internal::MultiWorldKnownWorkFitsV1(const MultiWorldKnownWorkV1& work,
                                         const MultiWorldExecutionConfig& config,
                                         MultiWorldKnownWorkProjectionV1* projection) noexcept {
  if ((work.world_count == 0) !=
          (work.total_selection_rounds == 0 && work.total_price_updates == 0) ||
      work.total_selection_rounds < work.world_count ||
      work.total_price_updates != work.total_selection_rounds - work.world_count) {
    return false;
  }
  const UWide charged_candidate_passes =
      1U + static_cast<UWide>(work.total_selection_rounds) + work.total_price_updates;
  const UWide candidate_evaluations = charged_candidate_passes * work.source_candidate_count;
  const UWide span_visits = charged_candidate_passes * work.source_candidate_span_count;
  const UWide resource_work_units =
      charged_candidate_passes * work.source_resource_work_units_per_candidate_pass;
  const UWide net_outcomes = charged_candidate_passes * work.source_net_count;
  const UWide pareto_comparisons =
      work.world_count == 0 ? 0
                            : static_cast<UWide>(work.world_count) * (work.world_count - 1U) / 2U;
  if (work.world_count > config.maximum_worlds ||
      work.total_selection_rounds > config.maximum_total_selection_rounds ||
      candidate_evaluations > config.maximum_total_candidate_evaluations ||
      span_visits > config.maximum_total_candidate_span_visits ||
      resource_work_units > config.maximum_total_resource_work_units ||
      net_outcomes > config.maximum_total_net_outcomes ||
      pareto_comparisons > config.maximum_pareto_comparisons ||
      charged_candidate_passes > std::numeric_limits<std::uint64_t>::max() ||
      candidate_evaluations > std::numeric_limits<std::uint64_t>::max() ||
      span_visits > std::numeric_limits<std::uint64_t>::max() ||
      resource_work_units > std::numeric_limits<std::uint64_t>::max() ||
      net_outcomes > std::numeric_limits<std::uint64_t>::max() ||
      pareto_comparisons > std::numeric_limits<std::uint64_t>::max()) {
    return false;
  }
  if (projection != nullptr) {
    *projection = MultiWorldKnownWorkProjectionV1{
        .charged_candidate_passes = static_cast<std::uint64_t>(charged_candidate_passes),
        .candidate_evaluations = static_cast<std::uint64_t>(candidate_evaluations),
        .candidate_span_visits = static_cast<std::uint64_t>(span_visits),
        .resource_work_units = static_cast<std::uint64_t>(resource_work_units),
        .net_outcomes = static_cast<std::uint64_t>(net_outcomes),
        .pareto_comparisons = static_cast<std::uint64_t>(pareto_comparisons),
    };
  }
  return true;
}

bool internal::MultiWorldDominatesV1(const MultiWorldObjectiveV1& left,
                                     const MultiWorldObjectiveV1& right) noexcept {
  const bool no_worse = left.selected_net_count >= right.selected_net_count &&
                        left.total_overuse_units <= right.total_overuse_units &&
                        left.total_intrinsic_cost <= right.total_intrinsic_cost;
  const bool strict = left.selected_net_count > right.selected_net_count ||
                      left.total_overuse_units < right.total_overuse_units ||
                      left.total_intrinsic_cost < right.total_intrinsic_cost;
  return no_worse && strict;
}

bool internal::MultiWorldPreferredBeforeV1(const MultiWorldObjectiveV1& left,
                                           const MultiWorldObjectiveV1& right) noexcept {
  return std::tuple{std::numeric_limits<std::uint64_t>::max() - left.selected_net_count,
                    left.total_overuse_units, left.total_intrinsic_cost, left.schedule_key} <
         std::tuple{std::numeric_limits<std::uint64_t>::max() - right.selected_net_count,
                    right.total_overuse_units, right.total_intrinsic_cost, right.schedule_key};
}

std::uint64_t internal::ComputeMultiWorldPoolSnapshotChecksumV1(
    const MultiWorldPoolSnapshotChecksumHeaderV1& header) noexcept {
  board_ir::StableHashBuilder hash;
  hash.AddString("APGAR-MULTI-WORLD-POOL-SNAPSHOT-V1");
  hash.AddU32(header.schema_version);
  hash.AddU64(header.associations.board_content_hash);
  hash.AddU64(header.associations.compiler_profile_fingerprint);
  hash.AddU32(header.associations.geometry_compiler_version);
  hash.AddU64(header.workload_checksum);
  hash.AddU64(header.capacity_model_checksum);
  hash.AddU64(header.allocator_limits.maximum_nets);
  hash.AddU64(header.allocator_limits.maximum_candidates);
  hash.AddU64(header.allocator_limits.maximum_resource_records);
  hash.AddU64(header.allocator_limits.maximum_expanded_resource_uses);
  hash.AddU64(header.candidate_pool_manifest_checksum);
  hash.AddU64(header.source_pool_count);
  hash.AddU64(header.source_candidate_count);
  return hash.Finish();
}

std::uint64_t internal::ComputeMultiWorldIdentityV1(std::uint64_t source_snapshot_checksum,
                                                    std::uint64_t branch_state_checksum,
                                                    const MultiWorldSchedule& schedule) noexcept {
  board_ir::StableHashBuilder hash;
  hash.AddString("APGAR-MULTI-WORLD-IDENTITY-V1");
  hash.AddU64(source_snapshot_checksum);
  hash.AddU64(branch_state_checksum);
  AddSchedule(hash, schedule);
  return hash.Finish();
}

std::uint64_t internal::ComputeMultiWorldExecutionChecksumV1(
    const MultiWorldExecutionChecksumHeaderV1& header,
    std::span<const MultiWorldSchedule> schedules, std::span<const MultiWorldSummary> summaries,
    std::span<const MultiWorldRetainedReplayV1> retained_worlds) noexcept {
  board_ir::StableHashBuilder hash;
  hash.AddString("APGAR-MULTI-WORLD-EXECUTION-V1");
  hash.AddU32(header.schema_version);
  hash.AddU64(header.source_snapshot_checksum);
  hash.AddU64(header.branch_state_checksum);
  AddConfig(hash, header.config);
  hash.AddByte(static_cast<std::uint8_t>(header.disposition));
  AddCounters(hash, header.counters);
  hash.AddBool(header.preferred_world_identity.has_value());
  if (header.preferred_world_identity.has_value()) {
    hash.AddU64(*header.preferred_world_identity);
  }
  hash.AddU64(schedules.size());
  for (const MultiWorldSchedule& schedule : schedules) {
    AddSchedule(hash, schedule);
  }
  hash.AddU64(summaries.size());
  for (const MultiWorldSummary& summary : summaries) {
    hash.AddU64(summary.world_identity);
    AddSchedule(hash, summary.schedule);
    hash.AddByte(static_cast<std::uint8_t>(summary.terminal_reason));
    hash.AddU64(summary.trace.size());
    for (const MultiWorldRoundTrace& trace : summary.trace) {
      hash.AddU32(trace.round_index);
      hash.AddU64(trace.price_state_checksum);
      hash.AddU64(trace.world_checksum);
    }
    hash.AddU64(summary.selected_net_count);
    hash.AddU64(summary.no_candidate_net_count);
    hash.AddU64(summary.overused_resource_count);
    hash.AddU64(summary.total_overuse_units);
    hash.AddU64(summary.total_intrinsic_cost);
    hash.AddBool(summary.pareto_eligible);
    hash.AddBool(summary.pareto_retained);
  }
  hash.AddU64(retained_worlds.size());
  for (const MultiWorldRetainedReplayV1& retained : retained_worlds) {
    hash.AddU64(retained.world_identity);
    hash.AddU64(retained.price_state_checksum);
    hash.AddU64(retained.world_checksum);
  }
  return hash.Finish();
}

template <bool CaptureOperationalProfile>
MultiWorldExecutionResult ExecuteMultiWorldCpuImpl(
    std::uint32_t schema_version, const MultiWorldPoolSnapshot& source,
    const NegotiatedPriceState& branch_state, std::span<const MultiWorldSchedule> schedules,
    candidates::CandidateStore& candidate_store, const MultiWorldExecutionConfig& config,
    MultiWorldOperationalProfileV1* operational_profile) {
  ::apgar::internal::OperationalTimestamp<CaptureOperationalProfile, OperationalClock>
      component_start;
  ::apgar::internal::OperationalTimestamp<CaptureOperationalProfile, OperationalClock>
      preflight_start;
  if constexpr (CaptureOperationalProfile) {
    *operational_profile = {};
    component_start =
        ::apgar::internal::OperationalNow<CaptureOperationalProfile, OperationalClock>();
    preflight_start = component_start;
  }
  g_preflight_span_inspections = 0;
  if (schema_version != kMultiWorldExecutionSchemaVersion) {
    return Error(MultiWorldExecutionErrorCode::kUnsupportedSchema,
                 "allocator.multi_world.schema.v1", "Multi-world execution schema is unsupported");
  }
  if (!ConfigIsValid(config)) {
    return Error(MultiWorldExecutionErrorCode::kInvalidConfiguration,
                 "allocator.multi_world.configuration.v1",
                 "Multi-world execution configuration is outside schema-v1 bounds");
  }
  if (source.workload == nullptr || source.workload->nets().empty()) {
    return Error(MultiWorldExecutionErrorCode::kAssociationMismatch,
                 "allocator.multi_world.workload.v1",
                 "Multi-world execution requires a nonempty authentic workload");
  }
  if (source.pools.empty()) {
    return Error(MultiWorldExecutionErrorCode::kSourcePoolInvalid,
                 "allocator.multi_world.source_pool_roster.v1",
                 "The source requires exactly one pool for every workload net");
  }
  if (!candidate_store.valid()) {
    return Error(MultiWorldExecutionErrorCode::kCandidateStoreLease,
                 "allocator.multi_world.candidate_store_configuration.v1",
                 "Multi-world execution requires a valid CandidateStore");
  }
  const candidates::CandidateStoreConfig& store_config = candidate_store.config();
  if (source.pools.size() > store_config.maximum_expected_pools_per_invocation) {
    return Error(MultiWorldExecutionErrorCode::kWorkBoundExceeded,
                 "allocator.multi_world.candidate_store_preflight.v1",
                 "The fixed source snapshot exceeds a known CandidateStore transaction bound");
  }
  if (!internal::OneWorldAllocatorLimitsAreValidV1(source.allocator_limits)) {
    return Error(MultiWorldExecutionErrorCode::kAllocation, "allocator.configuration.v1",
                 "The fixed source snapshot has invalid One-World allocator limits");
  }
  const UWide source_resource_input_records =
      static_cast<UWide>(source.capacities.overrides().size()) + branch_state.prices().size();
  if (source_resource_input_records > source.allocator_limits.maximum_resource_records) {
    return Error(MultiWorldExecutionErrorCode::kWorkBoundExceeded,
                 "allocator.input.resource_record_budget.v1",
                 "The fixed source capacity and price records exceed the One-World input bound");
  }
  if (schedules.empty() || schedules.size() > config.maximum_worlds) {
    return Error(MultiWorldExecutionErrorCode::kInvalidSchedule,
                 "allocator.multi_world.schedule_count.v1",
                 "Multi-world execution requires a bounded nonempty schedule roster");
  }

  if (branch_state.iteration() > branch_state.config().maximum_iterations) {
    return Error(MultiWorldExecutionErrorCode::kInvalidBranchState,
                 "allocator.multi_world.branch_iteration.v1",
                 "The branch state iteration exceeds its configured maximum");
  }

  UWide total_rounds_wide = 0;
  UWide total_updates_wide = 0;
  for (const MultiWorldSchedule& schedule : schedules) {
    if (schedule.schedule_key == 0 || schedule.search_intrinsic_cost_weight == 0 ||
        schedule.maximum_selection_rounds == 0) {
      return Error(MultiWorldExecutionErrorCode::kInvalidSchedule,
                   "allocator.multi_world.schedule_value.v1",
                   "Every schedule requires a nonzero key, weight, and selection-round count");
    }
    const std::uint64_t updates = schedule.maximum_selection_rounds - 1U;
    if (updates > branch_state.config().maximum_iterations - branch_state.iteration()) {
      return Error(MultiWorldExecutionErrorCode::kInvalidSchedule,
                   "allocator.multi_world.schedule_iteration.v1",
                   "A schedule exceeds the branch state's remaining negotiated-price iterations");
    }
    total_rounds_wide += schedule.maximum_selection_rounds;
    total_updates_wide += updates;
  }
  if (total_rounds_wide > std::numeric_limits<std::uint64_t>::max() ||
      total_updates_wide > std::numeric_limits<std::uint64_t>::max()) {
    return Error(MultiWorldExecutionErrorCode::kWorkBoundExceeded,
                 "allocator.multi_world.total_round_overflow.v1",
                 "The aggregate selection-round count exceeds unsigned 64-bit bounds");
  }

  const bool resource_refinement_required = config.known_unmapped_exact_conflict_count != 0;
  const UWide charged_candidate_passes_wide =
      resource_refinement_required ? 1U : 1U + total_rounds_wide + total_updates_wide;
  const UWide pareto_comparisons_wide =
      resource_refinement_required
          ? 0U
          : static_cast<UWide>(schedules.size()) * (schedules.size() - 1U) / 2U;
  if ((!resource_refinement_required &&
       total_rounds_wide > config.maximum_total_selection_rounds) ||
      charged_candidate_passes_wide > std::numeric_limits<std::uint64_t>::max() ||
      pareto_comparisons_wide > config.maximum_pareto_comparisons) {
    return Error(MultiWorldExecutionErrorCode::kWorkBoundExceeded,
                 "allocator.multi_world.known_work_budget.v1",
                 "Known multi-world work exceeds a configured aggregate bound");
  }
  const std::uint64_t charged_candidate_passes =
      static_cast<std::uint64_t>(charged_candidate_passes_wide);
  const std::uint64_t maximum_source_candidates_by_work =
      config.maximum_total_candidate_evaluations / charged_candidate_passes;
  const std::uint64_t maximum_source_spans_by_work =
      config.maximum_total_candidate_span_visits / charged_candidate_passes;
  const std::uint64_t maximum_source_nets_by_work =
      config.maximum_total_net_outcomes / charged_candidate_passes;
  const std::uint64_t maximum_resource_work_per_pass =
      config.maximum_total_resource_work_units / charged_candidate_passes;
  if (source.pools.size() > source.allocator_limits.maximum_nets ||
      source.pools.size() > maximum_source_nets_by_work ||
      source.allocator_limits.maximum_resource_records > maximum_resource_work_per_pass) {
    return Error(MultiWorldExecutionErrorCode::kWorkBoundExceeded,
                 "allocator.multi_world.known_work_budget.v1",
                 "Known multi-world work exceeds a configured aggregate bound");
  }

  UWide source_candidate_count_wide = 0;
  for (const CandidatePool& pool : source.pools) {
    source_candidate_count_wide += pool.candidates.size();
    if (source_candidate_count_wide > store_config.maximum_expected_candidates_per_invocation ||
        source_candidate_count_wide > store_config.maximum_pin_lease_items_per_transaction) {
      return Error(MultiWorldExecutionErrorCode::kWorkBoundExceeded,
                   "allocator.multi_world.candidate_store_preflight.v1",
                   "The fixed source snapshot exceeds a known CandidateStore transaction bound");
    }
    if (source_candidate_count_wide > source.allocator_limits.maximum_candidates ||
        source_candidate_count_wide > maximum_source_candidates_by_work) {
      return Error(MultiWorldExecutionErrorCode::kWorkBoundExceeded,
                   "allocator.multi_world.known_work_budget.v1",
                   "Known multi-world work exceeds a configured aggregate bound");
    }
  }

  UWide source_span_count_wide = 0;
  UWide source_expanded_resource_uses_wide = 0;
  UWide maximum_selected_resource_records_wide = 0;
  const std::uint64_t maximum_selected_resource_records_by_work =
      maximum_resource_work_per_pass - source.allocator_limits.maximum_resource_records;
  for (const CandidatePool& pool : source.pools) {
    UWide pool_maximum_resource_records = 0;
    for (const candidates::StoredCandidate& candidate : pool.candidates) {
      if (candidate == nullptr) {
        return Error(MultiWorldExecutionErrorCode::kSourcePoolInvalid,
                     "allocator.multi_world.null_candidate.v1",
                     "A fixed source pool contains a null candidate handle");
      }
      source_span_count_wide += candidate->data().resources.size();
      if (source_span_count_wide > maximum_source_spans_by_work) {
        return Error(MultiWorldExecutionErrorCode::kWorkBoundExceeded,
                     "allocator.multi_world.known_work_budget.v1",
                     "Known multi-world work exceeds a configured aggregate bound");
      }
      UWide candidate_resource_records = 0;
      for (const candidates::PhysicalEdgeSpan& span : candidate->data().resources) {
        ++g_preflight_span_inspections;
        candidate_resource_records += span.edge_count;
        source_expanded_resource_uses_wide += span.edge_count;
        if (source_expanded_resource_uses_wide >
            source.allocator_limits.maximum_expanded_resource_uses) {
          return Error(
              MultiWorldExecutionErrorCode::kWorkBoundExceeded,
              "allocator.input.expanded_resource_budget.v1",
              "The fixed source candidate footprints exceed the One-World expanded-use bound");
        }
        if (maximum_selected_resource_records_wide +
                std::max(pool_maximum_resource_records, candidate_resource_records) >
            maximum_selected_resource_records_by_work) {
          return Error(MultiWorldExecutionErrorCode::kWorkBoundExceeded,
                       "allocator.multi_world.known_work_budget.v1",
                       "Known multi-world work exceeds a configured aggregate bound");
        }
      }
      pool_maximum_resource_records =
          std::max(pool_maximum_resource_records, candidate_resource_records);
    }
    maximum_selected_resource_records_wide += pool_maximum_resource_records;
  }
  if (source_span_count_wide > std::numeric_limits<std::uint64_t>::max() ||
      maximum_selected_resource_records_wide > std::numeric_limits<std::uint64_t>::max() ||
      maximum_selected_resource_records_wide + source.allocator_limits.maximum_resource_records >
          std::numeric_limits<std::uint64_t>::max()) {
    return Error(MultiWorldExecutionErrorCode::kWorkBoundExceeded,
                 "allocator.multi_world.source_count_overflow.v1",
                 "Source candidate or resource-span counts exceed unsigned 64-bit bounds");
  }
  const internal::MultiWorldKnownWorkV1 known_work{
      .world_count = schedules.size(),
      .total_selection_rounds = static_cast<std::uint64_t>(total_rounds_wide),
      .total_price_updates = static_cast<std::uint64_t>(total_updates_wide),
      .source_net_count = source.pools.size(),
      .source_candidate_count = static_cast<std::uint64_t>(source_candidate_count_wide),
      .source_candidate_span_count = static_cast<std::uint64_t>(source_span_count_wide),
      .source_resource_work_units_per_candidate_pass =
          static_cast<std::uint64_t>(maximum_selected_resource_records_wide +
                                     source.allocator_limits.maximum_resource_records),
  };
  internal::MultiWorldKnownWorkV1 charged_work = known_work;
  if (resource_refinement_required) {
    charged_work.world_count = 0;
    charged_work.total_selection_rounds = 0;
    charged_work.total_price_updates = 0;
  }
  internal::MultiWorldKnownWorkProjectionV1 work_projection;
  if (!internal::MultiWorldKnownWorkFitsV1(charged_work, config, &work_projection)) {
    return Error(MultiWorldExecutionErrorCode::kWorkBoundExceeded,
                 "allocator.multi_world.known_work_budget.v1",
                 "Known multi-world work exceeds a configured aggregate bound");
  }

  if constexpr (CaptureOperationalProfile) {
    operational_profile->validation_and_source_preflight_wall_nanoseconds =
        OperationalElapsed(preflight_start);
  }
  return WithFailureEnvelope([&]() -> MultiWorldExecutionResult {
    std::vector<MultiWorldSchedule> canonical_schedules(schedules.begin(), schedules.end());
    std::ranges::sort(canonical_schedules, {}, &MultiWorldSchedule::schedule_key);
    if (std::ranges::adjacent_find(canonical_schedules, {}, &MultiWorldSchedule::schedule_key) !=
        canonical_schedules.end()) {
      return Error(MultiWorldExecutionErrorCode::kDuplicateSchedule,
                   "allocator.multi_world.duplicate_schedule_key.v1",
                   "Multi-world schedules contain a duplicate schedule key");
    }
    std::set<std::pair<std::uint64_t, std::uint32_t>> exact_schedules;
    for (const MultiWorldSchedule& schedule : canonical_schedules) {
      if (!exact_schedules
               .insert({schedule.search_intrinsic_cost_weight, schedule.maximum_selection_rounds})
               .second) {
        return Error(MultiWorldExecutionErrorCode::kDuplicateSchedule,
                     "allocator.multi_world.duplicate_schedule.v1",
                     "Different schedule keys describe the same exact search schedule");
      }
    }

    NegotiatedPriceSnapshotResult branch_snapshot_result =
        BuildPriceSnapshotForState(source.capacities, branch_state);
    if (const auto* failure = std::get_if<NegotiatedPriceError>(&branch_snapshot_result);
        failure != nullptr) {
      return Error(failure->code == NegotiatedPriceErrorCode::kResourceExhausted
                       ? MultiWorldExecutionErrorCode::kResourceExhausted
                       : MultiWorldExecutionErrorCode::kInvalidBranchState,
                   failure->invariant_id, failure->detail);
    }
    const PriceSnapshot branch_snapshot =
        std::get<PriceSnapshot>(std::move(branch_snapshot_result));
    if (branch_state.workload_checksum() != source.workload->workload_checksum()) {
      return Error(MultiWorldExecutionErrorCode::kInvalidBranchState,
                   "allocator.multi_world.branch_workload.v1",
                   "The branch state belongs to a different authentic workload");
    }

    OneWorldAllocationRequest first_request =
        BuildWorldRequest(source, branch_snapshot, canonical_schedules.front());

    auto expected_result = BuildExpectedPools(source);
    if (const auto* failure = std::get_if<MultiWorldExecutionError>(&expected_result);
        failure != nullptr) {
      return *failure;
    }
    std::vector<candidates::CandidatePinRequest> source_pin_requests = SourcePinRequests(source);
    candidates::CandidateStorePinLeaseResult source_lease_result =
        candidate_store.AcquirePinLeaseIfSourcePoolsMatch(
            std::get<std::vector<candidates::CandidateStoreExpectedPool>>(
                std::move(expected_result)),
            source_pin_requests);
    if (const auto* failure = std::get_if<candidates::CandidateStoreError>(&source_lease_result);
        failure != nullptr) {
      return TranslateStoreError(*failure, "allocator.multi_world.source_lease.v1",
                                 "CandidateStore source pools differ from the fixed snapshot");
    }
    candidates::CandidateStorePinLease source_lease =
        std::get<candidates::CandidateStorePinLease>(std::move(source_lease_result));

    internal::OneWorldSelectionEvidenceResult source_evidence_result =
        internal::SelectOneWorldWithoutAccounting(first_request);
    if (const auto* failure = std::get_if<AllocationError>(&source_evidence_result);
        failure != nullptr) {
      return Error(failure->code == AllocationErrorCode::kResourceExhausted
                       ? MultiWorldExecutionErrorCode::kResourceExhausted
                       : MultiWorldExecutionErrorCode::kSourcePoolInvalid,
                   failure->invariant_id, failure->detail);
    }
    const internal::OneWorldSelectionEvidence& source_evidence =
        std::get<internal::OneWorldSelectionEvidence>(source_evidence_result);
    if (source_evidence.source_pool_count != source.pools.size() ||
        source_evidence.source_candidate_count != known_work.source_candidate_count) {
      return Error(MultiWorldExecutionErrorCode::kInternalInvariant,
                   "allocator.multi_world.source_manifest_count.v1",
                   "Canonical source evidence changed preflighted pool or candidate counts");
    }

    const internal::ResourceCapacityChecksumHeaderV1 capacity_header{
        .schema_version = source.capacities.schema_version(),
        .associations = source.capacities.associations(),
        .default_capacity_units = source.capacities.default_capacity_units(),
    };
    const std::uint64_t capacity_checksum = internal::ComputeResourceCapacityModelChecksumV1(
        capacity_header, source.capacities.overrides());
    const std::uint64_t source_snapshot_checksum =
        internal::ComputeMultiWorldPoolSnapshotChecksumV1(
            internal::MultiWorldPoolSnapshotChecksumHeaderV1{
                .schema_version = kMultiWorldExecutionSchemaVersion,
                .associations = AssociationsFor(*source.workload),
                .workload_checksum = source.workload->workload_checksum(),
                .capacity_model_checksum = capacity_checksum,
                .allocator_limits = source.allocator_limits,
                .candidate_pool_manifest_checksum =
                    source_evidence.candidate_pool_manifest_checksum,
                .source_pool_count = source_evidence.source_pool_count,
                .source_candidate_count = source_evidence.source_candidate_count,
            });

    std::vector<std::uint64_t> world_identities;
    world_identities.reserve(canonical_schedules.size());
    std::set<std::uint64_t> unique_world_identities;
    for (const MultiWorldSchedule& schedule : canonical_schedules) {
      const std::uint64_t identity = internal::ComputeMultiWorldIdentityV1(
          source_snapshot_checksum, branch_state.state_checksum(), schedule);
      if (!unique_world_identities.insert(identity).second) {
        return Error(MultiWorldExecutionErrorCode::kInternalInvariant,
                     "allocator.multi_world.identity_collision.v1",
                     "Distinct canonical schedules produced the same world identity");
      }
      world_identities.push_back(identity);
    }

    MultiWorldExecutionCounters counters{
        .scheduled_worlds = canonical_schedules.size(),
        .candidate_evaluations = known_work.source_candidate_count,
        .candidate_span_visits = known_work.source_candidate_span_count,
        .resource_work_units = known_work.source_resource_work_units_per_candidate_pass,
        .net_outcomes = known_work.source_net_count,
    };

    const auto finalize =
        [&](MultiWorldExecutionDisposition disposition, std::vector<MultiWorldSummary> summaries,
            std::vector<RetainedMultiWorld> retained_worlds,
            std::optional<std::uint64_t> preferred_world_identity,
            std::vector<candidates::CandidatePinRequest> winner_requests,
            MultiWorldExecutionCounters final_counters) -> MultiWorldExecutionResult {
      ::apgar::internal::OperationalTimestamp<CaptureOperationalProfile, OperationalClock>
          retention_start;
      if constexpr (CaptureOperationalProfile) {
        retention_start =
            ::apgar::internal::OperationalNow<CaptureOperationalProfile, OperationalClock>();
      }
      auto successor_expected_result = BuildExpectedPools(source);
      if (const auto* failure = std::get_if<MultiWorldExecutionError>(&successor_expected_result);
          failure != nullptr) {
        return *failure;
      }
      candidates::CandidateStorePinLeaseResult successor_result =
          candidate_store.AcquirePinLeaseIfSourcePoolsMatch(
              std::get<std::vector<candidates::CandidateStoreExpectedPool>>(
                  std::move(successor_expected_result)),
              winner_requests);
      if (const auto* failure = std::get_if<candidates::CandidateStoreError>(&successor_result);
          failure != nullptr) {
        return TranslateStoreError(*failure, "allocator.multi_world.successor_lease.v1",
                                   "CandidateStore source pools drifted before Pareto retention");
      }
      candidates::CandidateStorePinLease successor_lease =
          std::get<candidates::CandidateStorePinLease>(std::move(successor_result));
      source_lease.Release();

      std::vector<internal::MultiWorldRetainedReplayV1> retained_replay;
      retained_replay.reserve(retained_worlds.size());
      for (const RetainedMultiWorld& retained : retained_worlds) {
        retained_replay.push_back(internal::MultiWorldRetainedReplayV1{
            .world_identity = retained.world_identity,
            .price_state_checksum = retained.price_state.state_checksum(),
            .world_checksum = retained.world.world_checksum,
        });
      }
      const std::uint64_t execution_checksum = internal::ComputeMultiWorldExecutionChecksumV1(
          internal::MultiWorldExecutionChecksumHeaderV1{
              .schema_version = kMultiWorldExecutionSchemaVersion,
              .source_snapshot_checksum = source_snapshot_checksum,
              .branch_state_checksum = branch_state.state_checksum(),
              .config = config,
              .disposition = disposition,
              .counters = final_counters,
              .preferred_world_identity = preferred_world_identity,
          },
          canonical_schedules, summaries, retained_replay);
      MultiWorldExecution result(kMultiWorldExecutionSchemaVersion, config,
                                 source_snapshot_checksum, branch_state.state_checksum(),
                                 disposition, final_counters, std::move(summaries),
                                 std::move(retained_worlds), preferred_world_identity,
                                 execution_checksum, std::move(successor_lease));
      if constexpr (CaptureOperationalProfile) {
        AccumulateOperationalElapsed(
            operational_profile->terminal_retention_and_assembly_wall_nanoseconds, retention_start);
        operational_profile->component_wall_nanoseconds = OperationalElapsed(component_start);
        const UWide classified =
            static_cast<UWide>(
                operational_profile->validation_and_source_preflight_wall_nanoseconds) +
            operational_profile->selection_and_resource_accumulation_wall_nanoseconds +
            operational_profile->price_update_and_snapshot_wall_nanoseconds +
            operational_profile->terminal_retention_and_assembly_wall_nanoseconds;
        operational_profile->unclassified_serial_wall_nanoseconds =
            classified <= operational_profile->component_wall_nanoseconds
                ? operational_profile->component_wall_nanoseconds -
                      static_cast<std::uint64_t>(classified)
                : 0;
      }
      return result;
    };

    if (config.known_unmapped_exact_conflict_count != 0) {
      return finalize(MultiWorldExecutionDisposition::kResourceRefinementRequired, {}, {},
                      std::nullopt, {}, counters);
    }

    std::vector<CompletedWorld> completed;
    completed.reserve(canonical_schedules.size());
    UWide buffered_terminal_selection_records = 0;
    UWide buffered_terminal_resource_records = 0;
    UWide buffered_terminal_price_records = 0;
    for (std::size_t schedule_index = 0; schedule_index < canonical_schedules.size();
         ++schedule_index) {
      const MultiWorldSchedule& schedule = canonical_schedules[schedule_index];
      OneWorldAllocationRequest request =
          schedule_index == 0 ? std::move(first_request)
                              : BuildWorldRequest(source, branch_snapshot, schedule);
      NegotiatedPriceState price_state = branch_state;
      MultiWorldSummary summary;
      summary.world_identity = world_identities[schedule_index];
      summary.schedule = schedule;
      summary.trace.reserve(schedule.maximum_selection_rounds);
      std::optional<OneWorldAllocation> final_world;
      for (std::uint32_t round = 0; round < schedule.maximum_selection_rounds; ++round) {
        ::apgar::internal::OperationalTimestamp<CaptureOperationalProfile, OperationalClock>
            selection_start;
        if constexpr (CaptureOperationalProfile) {
          selection_start =
              ::apgar::internal::OperationalNow<CaptureOperationalProfile, OperationalClock>();
        }
        OneWorldAllocationResult world_result = AllocateOneWorld(request);
        if (const auto* failure = std::get_if<AllocationError>(&world_result); failure != nullptr) {
          return TranslateAllocationError(
              *failure, "allocator.multi_world.allocation.v1",
              "A canonical multi-world branch failed One-World allocation");
        }
        OneWorldAllocation world = std::get<OneWorldAllocation>(std::move(world_result));
        if constexpr (CaptureOperationalProfile) {
          AccumulateOperationalElapsed(
              operational_profile->selection_and_resource_accumulation_wall_nanoseconds,
              selection_start);
        }
        summary.trace.push_back(MultiWorldRoundTrace{
            .round_index = round,
            .price_state_checksum = price_state.state_checksum(),
            .world_checksum = world.world_checksum,
        });
        ++counters.selection_rounds;
        counters.candidate_evaluations += known_work.source_candidate_count;
        counters.candidate_span_visits += known_work.source_candidate_span_count;
        counters.resource_work_units += known_work.source_resource_work_units_per_candidate_pass;
        counters.net_outcomes += known_work.source_net_count;

        const bool feasible = world.no_candidate_net_count == 0 && world.total_overuse_units == 0;
        const bool no_candidate = world.no_candidate_net_count != 0;
        const bool final_round = round + 1U == schedule.maximum_selection_rounds;
        if (feasible || no_candidate || final_round) {
          summary.terminal_reason = feasible ? MultiWorldTerminalReason::kFeasible
                                    : no_candidate
                                        ? MultiWorldTerminalReason::kNoCandidateWithoutRegeneration
                                        : MultiWorldTerminalReason::kSelectionRoundLimit;
          summary.selected_net_count = world.selected_net_count;
          summary.no_candidate_net_count = world.no_candidate_net_count;
          summary.overused_resource_count = world.overused_resource_count;
          summary.total_overuse_units = world.total_overuse_units;
          summary.total_intrinsic_cost = world.total_intrinsic_cost;
          summary.pareto_eligible =
              world.no_candidate_net_count <= config.maximum_near_feasible_missing_nets &&
              world.total_overuse_units <= config.maximum_near_feasible_overuse_units;
          final_world.emplace(std::move(world));
          break;
        }

        ::apgar::internal::OperationalTimestamp<CaptureOperationalProfile, OperationalClock>
            price_update_start;
        if constexpr (CaptureOperationalProfile) {
          price_update_start =
              ::apgar::internal::OperationalNow<CaptureOperationalProfile, OperationalClock>();
        }
        NegotiatedPriceStateResult update = UpdateNegotiatedPrices(price_state, request, world);
        if (const auto* failure = std::get_if<NegotiatedPriceError>(&update); failure != nullptr) {
          return TranslatePriceError(*failure, "allocator.multi_world.price_update.v1",
                                     "A canonical multi-world price update failed");
        }
        counters.candidate_evaluations += known_work.source_candidate_count;
        counters.candidate_span_visits += known_work.source_candidate_span_count;
        counters.resource_work_units += known_work.source_resource_work_units_per_candidate_pass;
        counters.net_outcomes += known_work.source_net_count;
        price_state = std::get<NegotiatedPriceState>(std::move(update));
        NegotiatedPriceSnapshotResult next_snapshot =
            BuildPriceSnapshotForState(source.capacities, price_state);
        if (const auto* failure = std::get_if<NegotiatedPriceError>(&next_snapshot);
            failure != nullptr) {
          return TranslatePriceError(*failure, "allocator.multi_world.price_snapshot.v1",
                                     "A branch state failed to produce its next price snapshot");
        }
        request.prices = std::get<PriceSnapshot>(std::move(next_snapshot));
        if constexpr (CaptureOperationalProfile) {
          AccumulateOperationalElapsed(
              operational_profile->price_update_and_snapshot_wall_nanoseconds, price_update_start);
        }
        ++counters.price_updates;
      }
      if (!final_world.has_value()) {
        return Error(MultiWorldExecutionErrorCode::kInternalInvariant,
                     "allocator.multi_world.terminal_world.v1",
                     "A bounded multi-world branch produced no terminal world");
      }
      ++counters.completed_worlds;
      if (summary.pareto_eligible) {
        ++counters.pareto_eligible_worlds;
      }
      buffered_terminal_selection_records += final_world->selections.size();
      buffered_terminal_resource_records += final_world->resources.size();
      buffered_terminal_price_records += price_state.prices().size();
      if (buffered_terminal_selection_records >
              config.maximum_buffered_terminal_selection_records ||
          buffered_terminal_resource_records > config.maximum_buffered_terminal_resource_records ||
          buffered_terminal_price_records > config.maximum_buffered_terminal_price_records) {
        return Error(MultiWorldExecutionErrorCode::kWorkBoundExceeded,
                     "allocator.multi_world.buffered_terminal_budget.v1",
                     "Buffered terminal worlds exceed a configured aggregate record bound");
      }
      completed.push_back(CompletedWorld{.summary = std::move(summary),
                                         .price_state = std::move(price_state),
                                         .world = std::move(*final_world)});
    }

    const UWide actual_candidate_passes =
        1U + static_cast<UWide>(counters.selection_rounds) + counters.price_updates;
    if (actual_candidate_passes > work_projection.charged_candidate_passes ||
        counters.candidate_evaluations !=
            actual_candidate_passes * known_work.source_candidate_count ||
        counters.candidate_span_visits !=
            actual_candidate_passes * known_work.source_candidate_span_count ||
        counters.resource_work_units !=
            actual_candidate_passes * known_work.source_resource_work_units_per_candidate_pass ||
        counters.net_outcomes != actual_candidate_passes * known_work.source_net_count) {
      return Error(MultiWorldExecutionErrorCode::kInternalInvariant,
                   "allocator.multi_world.runtime_work_accounting.v1",
                   "Runtime work counters differ from the declared per-pass charge");
    }

    std::vector<bool> dominated(completed.size(), false);
    for (std::size_t left = 0; left < completed.size(); ++left) {
      if (!completed[left].summary.pareto_eligible) {
        continue;
      }
      for (std::size_t right = left + 1U; right < completed.size(); ++right) {
        if (!completed[right].summary.pareto_eligible) {
          continue;
        }
        ++counters.pareto_comparisons;
        const internal::MultiWorldObjectiveV1 left_objective =
            ObjectiveFor(completed[left].summary);
        const internal::MultiWorldObjectiveV1 right_objective =
            ObjectiveFor(completed[right].summary);
        if (internal::MultiWorldDominatesV1(left_objective, right_objective)) {
          dominated[right] = true;
        } else if (internal::MultiWorldDominatesV1(right_objective, left_objective)) {
          dominated[left] = true;
        }
      }
    }

    UWide retained_selection_records = 0;
    UWide retained_resource_records = 0;
    UWide retained_price_records = 0;
    std::uint64_t retained_count = 0;
    for (std::size_t index = 0; index < completed.size(); ++index) {
      if (!completed[index].summary.pareto_eligible || dominated[index]) {
        continue;
      }
      completed[index].summary.pareto_retained = true;
      ++retained_count;
      retained_selection_records += completed[index].world.selections.size();
      retained_resource_records += completed[index].world.resources.size();
      retained_price_records += completed[index].price_state.prices().size();
    }
    if (retained_count > config.maximum_retained_worlds ||
        retained_selection_records > config.maximum_retained_selection_records ||
        retained_resource_records > config.maximum_retained_resource_records ||
        retained_price_records > config.maximum_retained_price_records) {
      return Error(MultiWorldExecutionErrorCode::kWorkBoundExceeded,
                   "allocator.multi_world.pareto_retention_budget.v1",
                   "The exact Pareto frontier exceeds a configured retention bound");
    }

    std::map<candidates::CandidateId, candidates::CandidatePinRequest> unique_winners;
    std::optional<internal::MultiWorldObjectiveV1> preferred_objective;
    std::optional<std::uint64_t> preferred_world_identity;
    for (const CompletedWorld& item : completed) {
      if (!item.summary.pareto_retained) {
        continue;
      }
      const internal::MultiWorldObjectiveV1 objective = ObjectiveFor(item.summary);
      if (!preferred_objective.has_value() ||
          internal::MultiWorldPreferredBeforeV1(objective, *preferred_objective)) {
        preferred_objective = objective;
        preferred_world_identity = item.summary.world_identity;
      }
      for (const NetSelection& selection : item.world.selections) {
        if (selection.candidate == nullptr) {
          continue;
        }
        const candidates::CandidatePinRequest request{
            .net = selection.net,
            .candidate_id = *selection.candidate_id,
            .candidate_payload_checksum = *selection.candidate_payload_checksum,
            .expected_candidate = selection.candidate,
        };
        const auto [found, inserted] = unique_winners.emplace(request.candidate_id, request);
        if (!inserted &&
            (found->second.net != request.net ||
             found->second.candidate_payload_checksum != request.candidate_payload_checksum ||
             found->second.expected_candidate == nullptr || request.expected_candidate == nullptr ||
             found->second.expected_candidate->data() != request.expected_candidate->data())) {
          return Error(MultiWorldExecutionErrorCode::kInternalInvariant,
                       "allocator.multi_world.winner_identity_collision.v1",
                       "Pareto worlds disagree on the immutable value of one candidate identity");
        }
      }
    }
    if (unique_winners.size() > config.maximum_retained_winner_pins) {
      return Error(MultiWorldExecutionErrorCode::kWorkBoundExceeded,
                   "allocator.multi_world.winner_pin_budget.v1",
                   "Unique Pareto winners exceed the configured successor-pin bound");
    }

    std::vector<candidates::CandidatePinRequest> winner_requests;
    winner_requests.reserve(unique_winners.size());
    for (auto& [candidate_id, request] : unique_winners) {
      static_cast<void>(candidate_id);
      winner_requests.push_back(std::move(request));
    }

    std::vector<MultiWorldSummary> summaries;
    summaries.reserve(completed.size());
    std::vector<RetainedMultiWorld> retained_worlds;
    retained_worlds.reserve(retained_count);
    for (CompletedWorld& item : completed) {
      summaries.push_back(item.summary);
      if (!item.summary.pareto_retained) {
        continue;
      }
      retained_worlds.push_back(RetainedMultiWorld{
          .world_identity = item.summary.world_identity,
          .schedule = item.summary.schedule,
          .price_state = std::move(item.price_state),
          .world = std::move(item.world),
      });
    }
    counters.retained_worlds = retained_worlds.size();
    counters.retained_winner_pins = winner_requests.size();
    const MultiWorldExecutionDisposition disposition =
        retained_worlds.empty() ? MultiWorldExecutionDisposition::kNoEligibleWorlds
                                : MultiWorldExecutionDisposition::kCompleted;
    return finalize(disposition, std::move(summaries), std::move(retained_worlds),
                    preferred_world_identity, std::move(winner_requests), counters);
  });
}

MultiWorldExecutionResult ExecuteMultiWorldCpu(std::uint32_t schema_version,
                                               const MultiWorldPoolSnapshot& source,
                                               const NegotiatedPriceState& branch_state,
                                               std::span<const MultiWorldSchedule> schedules,
                                               candidates::CandidateStore& candidate_store,
                                               const MultiWorldExecutionConfig& config) {
  return ExecuteMultiWorldCpuImpl<false>(schema_version, source, branch_state, schedules,
                                         candidate_store, config, nullptr);
}

MultiWorldExecutionResult ExecuteMultiWorldCpuWithOperationalProfileV1(
    std::uint32_t schema_version, const MultiWorldPoolSnapshot& source,
    const NegotiatedPriceState& branch_state, std::span<const MultiWorldSchedule> schedules,
    candidates::CandidateStore& candidate_store, const MultiWorldExecutionConfig& config,
    MultiWorldOperationalProfileV1& operational_profile) {
  return ExecuteMultiWorldCpuImpl<true>(schema_version, source, branch_state, schedules,
                                        candidate_store, config, &operational_profile);
}

}  // namespace apgar::allocator
