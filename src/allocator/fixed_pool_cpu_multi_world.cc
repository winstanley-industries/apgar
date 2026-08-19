#include "apgar/allocator/fixed_pool_cpu_multi_world.h"

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
#include "apgar/routing/planar_route.h"
#include "src/allocator/fixed_pool_cpu_multi_world_internal.h"

namespace apgar::allocator {
namespace {

using Wide = __int128_t;
using UWide = __uint128_t;

[[nodiscard]] FixedPoolCpuMultiWorldError Error(FixedPoolCpuMultiWorldErrorCode code,
                                                std::string_view invariant_id,
                                                std::string_view detail) noexcept {
  return FixedPoolCpuMultiWorldError{
      .code = code,
      .invariant_id = invariant_id,
      .detail = detail,
      .schedule_key = std::nullopt,
      .expected_value = std::nullopt,
      .actual_value = std::nullopt,
      .source_error = std::nullopt,
      .accounting_error = std::nullopt,
  };
}

[[nodiscard]] FixedPoolCpuMultiWorldError BoundError(std::string_view invariant_id,
                                                     std::string_view detail,
                                                     std::uint64_t expected,
                                                     std::uint64_t actual) noexcept {
  FixedPoolCpuMultiWorldError error =
      Error(FixedPoolCpuMultiWorldErrorCode::kBoundExhausted, invariant_id, detail);
  error.expected_value = expected;
  error.actual_value = actual;
  return error;
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

[[nodiscard]] bool ConfigurationIsValid(const FixedPoolCpuMultiWorldConfig& config) noexcept {
  const FixedPoolCpuMultiWorldLimits& limits = config.limits;
  return SelectionLimitsAreValid(limits.selection) && limits.maximum_worlds > 0 &&
         limits.maximum_worlds <= kMaximumFixedPoolCpuMultiWorlds &&
         limits.maximum_total_selection_rounds > 0 &&
         limits.maximum_total_selection_rounds <= kMaximumFixedPoolCpuMultiWorldRounds &&
         limits.maximum_total_price_updates > 0 &&
         limits.maximum_total_price_updates <= kMaximumFixedPoolCpuMultiWorldRounds &&
         limits.maximum_candidate_evaluations > 0 &&
         limits.maximum_candidate_evaluations <= kMaximumFixedPoolCpuMultiWorldWorkItems &&
         limits.maximum_candidate_resource_visits > 0 &&
         limits.maximum_candidate_resource_visits <= kMaximumFixedPoolCpuMultiWorldWorkItems &&
         limits.maximum_selected_resource_uses > 0 &&
         limits.maximum_selected_resource_uses <= kMaximumFixedPoolCpuMultiWorldWorkItems &&
         limits.maximum_net_outcomes > 0 &&
         limits.maximum_net_outcomes <= kMaximumFixedPoolCpuMultiWorldWorkItems &&
         limits.maximum_emitted_price_entries > 0 &&
         limits.maximum_emitted_price_entries <= kMaximumFixedPoolCpuMultiWorldWorkItems &&
         limits.maximum_trace_records > 0 &&
         limits.maximum_trace_records <= kMaximumFixedPoolCpuMultiWorldRecords &&
         limits.maximum_buffered_terminal_selection_records > 0 &&
         limits.maximum_buffered_terminal_selection_records <=
             kMaximumFixedPoolCpuMultiWorldRecords &&
         limits.maximum_buffered_terminal_resource_records > 0 &&
         limits.maximum_buffered_terminal_resource_records <=
             kMaximumFixedPoolCpuMultiWorldRecords &&
         limits.maximum_buffered_terminal_price_records > 0 &&
         limits.maximum_buffered_terminal_price_records <= kMaximumFixedPoolCpuMultiWorldRecords &&
         limits.maximum_pareto_comparisons > 0 &&
         limits.maximum_pareto_comparisons <= kMaximumFixedPoolCpuMultiWorldParetoComparisons &&
         limits.maximum_retained_worlds > 0 &&
         limits.maximum_retained_worlds <= kMaximumFixedPoolCpuMultiWorlds &&
         limits.maximum_retained_selection_records > 0 &&
         limits.maximum_retained_selection_records <= kMaximumFixedPoolCpuMultiWorldRecords &&
         limits.maximum_retained_resource_records > 0 &&
         limits.maximum_retained_resource_records <= kMaximumFixedPoolCpuMultiWorldRecords &&
         limits.maximum_retained_price_records > 0 &&
         limits.maximum_retained_price_records <= kMaximumFixedPoolCpuMultiWorldRecords &&
         limits.maximum_price_entries_per_world > 0 &&
         limits.maximum_price_entries_per_world <= kMaximumNegotiatedPriceEntries &&
         limits.maximum_price_value > 0 && limits.maximum_aggregate_price_per_world > 0 &&
         limits.maximum_candidate_score > 0;
}

void HashResource(board_ir::StableHashBuilder* hash,
                  const routing::EdgeResourceKey& resource) noexcept {
  hash->AddU32(resource.layer);
  hash->AddI64(resource.lattice_x);
  hash->AddI64(resource.lattice_y);
  hash->AddU32(static_cast<std::uint32_t>(resource.direction));
}

void HashCandidateId(board_ir::StableHashBuilder* hash,
                     candidates::CandidateId candidate_id) noexcept {
  hash->AddU64(candidate_id.high);
  hash->AddU64(candidate_id.low);
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

void HashSchedule(board_ir::StableHashBuilder* hash,
                  const FixedPoolCpuWorldSchedule& schedule) noexcept {
  hash->AddU64(schedule.schedule_key);
  hash->AddU64(schedule.price_policy.initial_present_factor);
  hash->AddU64(schedule.price_policy.present_factor_increment);
  hash->AddU64(schedule.price_policy.historical_price_increment);
  hash->AddU32(schedule.maximum_selection_rounds);
}

void HashConfig(board_ir::StableHashBuilder* hash,
                const FixedPoolCpuMultiWorldConfig& config) noexcept {
  const FixedPoolCpuMultiWorldLimits& limits = config.limits;
  hash->AddU64(limits.selection.maximum_net_pools);
  hash->AddU64(limits.selection.maximum_total_candidates);
  hash->AddU64(limits.selection.accounting.maximum_candidates);
  hash->AddU64(limits.selection.accounting.maximum_expanded_resource_uses);
  hash->AddU64(limits.selection.accounting.maximum_usage_units_per_resource);
  hash->AddU64(limits.maximum_worlds);
  hash->AddU64(limits.maximum_total_selection_rounds);
  hash->AddU64(limits.maximum_total_price_updates);
  hash->AddU64(limits.maximum_candidate_evaluations);
  hash->AddU64(limits.maximum_candidate_resource_visits);
  hash->AddU64(limits.maximum_selected_resource_uses);
  hash->AddU64(limits.maximum_net_outcomes);
  hash->AddU64(limits.maximum_emitted_price_entries);
  hash->AddU64(limits.maximum_trace_records);
  hash->AddU64(limits.maximum_buffered_terminal_selection_records);
  hash->AddU64(limits.maximum_buffered_terminal_resource_records);
  hash->AddU64(limits.maximum_buffered_terminal_price_records);
  hash->AddU64(limits.maximum_pareto_comparisons);
  hash->AddU64(limits.maximum_retained_worlds);
  hash->AddU64(limits.maximum_retained_selection_records);
  hash->AddU64(limits.maximum_retained_resource_records);
  hash->AddU64(limits.maximum_retained_price_records);
  hash->AddU64(limits.maximum_price_entries_per_world);
  hash->AddU64(limits.maximum_price_value);
  hash->AddU64(limits.maximum_aggregate_price_per_world);
  hash->AddU64(limits.maximum_candidate_score);
  hash->AddU64(config.maximum_near_feasible_missing_nets);
  hash->AddU64(config.maximum_near_feasible_overuse_units);
}

void HashCounters(board_ir::StableHashBuilder* hash,
                  const FixedPoolCpuMultiWorldCounters& counters) noexcept {
  hash->AddU64(counters.scheduled_worlds);
  hash->AddU64(counters.completed_worlds);
  hash->AddU64(counters.selection_rounds);
  hash->AddU64(counters.price_updates);
  hash->AddU64(counters.candidate_evaluations);
  hash->AddU64(counters.candidate_resource_visits);
  hash->AddU64(counters.selected_resource_uses);
  hash->AddU64(counters.net_outcomes);
  hash->AddU64(counters.emitted_price_entries);
  hash->AddU64(counters.trace_records);
  hash->AddU64(counters.pareto_comparisons);
  hash->AddU64(counters.pareto_eligible_worlds);
  hash->AddU64(counters.retained_worlds);
}

[[nodiscard]] std::uint64_t PricePolicyIdentity(
    const NegotiatedPriceUpdatePolicyV1& policy) noexcept {
  board_ir::StableHashBuilder hash;
  hash.AddString("APGAR-P4R06-NEGOTIATED-PRICE-POLICY-V1");
  hash.AddU64(policy.initial_present_factor);
  hash.AddU64(policy.present_factor_increment);
  hash.AddU64(policy.historical_price_increment);
  return NonzeroHash(&hash);
}

[[nodiscard]] std::uint64_t CommonStateIdentity(
    const ResourceCapacityModel& capacities, const CpuCandidateAllocationSession& source) noexcept {
  board_ir::StableHashBuilder hash;
  hash.AddString("APGAR-P4R09-FIXED-POOL-COMMON-STATE-V1");
  hash.AddU64(source.session_identity());
  hash.AddU64(source.final_pool_identity());
  hash.AddU64(capacities.associations().board_content_hash);
  hash.AddU64(capacities.associations().compiler_profile_fingerprint);
  hash.AddU32(capacities.associations().geometry_compiler_version);
  hash.AddU32(capacities.capacity_units());
  HashSelection(&hash, source.final_selection());
  hash.AddU64(0);
  return NonzeroHash(&hash);
}

[[nodiscard]] std::uint64_t WorldIdentity(std::uint64_t common_state_identity,
                                          const FixedPoolCpuWorldSchedule& schedule) noexcept {
  board_ir::StableHashBuilder hash;
  hash.AddString("APGAR-P4R09-FIXED-POOL-WORLD-V1");
  hash.AddU64(common_state_identity);
  HashSchedule(&hash, schedule);
  return NonzeroHash(&hash);
}

[[nodiscard]] std::uint64_t SelectionIdentity(std::uint64_t world_identity,
                                              std::uint32_t round_index,
                                              std::uint64_t input_price_state_identity,
                                              const OneWorldSelection& selection) noexcept {
  board_ir::StableHashBuilder hash;
  hash.AddString("APGAR-P4R09-FIXED-POOL-SELECTION-V1");
  hash.AddU64(world_identity);
  hash.AddU32(round_index);
  hash.AddU64(input_price_state_identity);
  HashSelection(&hash, selection);
  return NonzeroHash(&hash);
}

[[nodiscard]] std::uint64_t PriceStateIdentity(std::uint64_t common_state_identity,
                                               std::uint64_t world_identity,
                                               const FixedPoolCpuWorldSchedule& schedule,
                                               const FixedPoolCpuWorldPriceState& state) noexcept {
  board_ir::StableHashBuilder hash;
  hash.AddString("APGAR-P4R09-FIXED-POOL-PRICE-STATE-V1");
  hash.AddU64(common_state_identity);
  hash.AddU64(world_identity);
  HashSchedule(&hash, schedule);
  hash.AddU64(state.policy_identity);
  hash.AddU64(state.prior_state_identity);
  hash.AddU64(state.completed_updates);
  hash.AddU64(state.present_factor);
  hash.AddU64(static_cast<std::uint64_t>(state.prices.size()));
  for (const NegotiatedResourcePrice& price : state.prices) {
    HashResource(&hash, price.resource);
    hash.AddU64(price.present_price);
    hash.AddU64(price.historical_price);
    hash.AddU64(price.total_price);
  }
  return NonzeroHash(&hash);
}

[[nodiscard]] std::uint64_t OutcomeIdentity(const FixedPoolCpuWorldSummary& summary,
                                            const OneWorldSelection& selection) noexcept {
  board_ir::StableHashBuilder hash;
  hash.AddString("APGAR-P4R09-FIXED-POOL-WORLD-OUTCOME-V1");
  hash.AddU64(summary.world_identity);
  HashSchedule(&hash, summary.schedule);
  hash.AddU32(static_cast<std::uint32_t>(summary.terminal_reason));
  hash.AddU64(summary.objective.selected_net_count);
  hash.AddU64(summary.objective.missing_net_count);
  hash.AddU64(summary.objective.total_overuse_units);
  hash.AddU64(summary.objective.total_intrinsic_cost);
  hash.AddU64(summary.final_price_state_identity);
  hash.AddU64(summary.final_selection_identity);
  hash.AddBool(summary.pareto_eligible);
  hash.AddU64(static_cast<std::uint64_t>(summary.trace.size()));
  for (const FixedPoolCpuWorldRoundTrace& trace : summary.trace) {
    hash.AddU32(trace.round_index);
    hash.AddU64(trace.input_price_state_identity);
    hash.AddU64(trace.selection_identity);
    hash.AddBool(trace.output_price_state_identity.has_value());
    if (trace.output_price_state_identity.has_value()) {
      hash.AddU64(*trace.output_price_state_identity);
    }
  }
  HashSelection(&hash, selection);
  return NonzeroHash(&hash);
}

[[nodiscard]] bool Dominates(const FixedPoolCpuWorldObjective& left,
                             const FixedPoolCpuWorldObjective& right) noexcept {
  const bool no_worse = left.selected_net_count >= right.selected_net_count &&
                        left.total_overuse_units <= right.total_overuse_units &&
                        left.total_intrinsic_cost <= right.total_intrinsic_cost;
  const bool strict = left.selected_net_count > right.selected_net_count ||
                      left.total_overuse_units < right.total_overuse_units ||
                      left.total_intrinsic_cost < right.total_intrinsic_cost;
  return no_worse && strict;
}

[[nodiscard]] bool PreferredBefore(const FixedPoolCpuWorldSummary& left,
                                   const FixedPoolCpuWorldSummary& right) noexcept {
  return std::tuple{
             left.objective.missing_net_count,    left.objective.total_overuse_units,
             left.objective.total_intrinsic_cost, static_cast<std::uint8_t>(left.terminal_reason),
             left.schedule.schedule_key,          left.world_identity} <
         std::tuple{
             right.objective.missing_net_count,    right.objective.total_overuse_units,
             right.objective.total_intrinsic_cost, static_cast<std::uint8_t>(right.terminal_reason),
             right.schedule.schedule_key,          right.world_identity};
}

[[nodiscard]] const NegotiatedResourcePrice* FindPrice(
    std::span<const NegotiatedResourcePrice> prices,
    const routing::EdgeResourceKey& resource) noexcept {
  const auto found =
      std::ranges::lower_bound(prices, resource, {}, &NegotiatedResourcePrice::resource);
  return found != prices.end() && found->resource == resource ? &*found : nullptr;
}

[[nodiscard]] std::optional<routing::EdgeResourceKey> ResourceAt(
    const candidates::PhysicalEdgeSpan& span, std::uint32_t offset) noexcept {
  const geometry_compiler::DirectionDelta delta =
      candidates::ResourceSpanStorageDelta(span.direction);
  const Wide x = static_cast<Wide>(span.lattice_x) + static_cast<Wide>(delta.x) * offset;
  const Wide y = static_cast<Wide>(span.lattice_y) + static_cast<Wide>(delta.y) * offset;
  if (x < std::numeric_limits<std::int64_t>::min() ||
      x > std::numeric_limits<std::int64_t>::max() ||
      y < std::numeric_limits<std::int64_t>::min() ||
      y > std::numeric_limits<std::int64_t>::max()) {
    return std::nullopt;
  }
  return routing::EdgeResourceKey{
      .layer = span.layer,
      .lattice_x = static_cast<std::int64_t>(x),
      .lattice_y = static_cast<std::int64_t>(y),
      .direction = span.direction,
  };
}

struct CandidateScore {
  const candidates::RouteCandidate* candidate = nullptr;
  std::uint64_t negotiated_score = 0;
};

[[nodiscard]] bool CandidateRanksBefore(const CandidateScore& left,
                                        const CandidateScore& right) noexcept {
  const candidates::CandidateMetrics& left_metrics = left.candidate->data().metrics;
  const candidates::CandidateMetrics& right_metrics = right.candidate->data().metrics;
  const UWide left_steps =
      static_cast<UWide>(left_metrics.orthogonal_step_count) + left_metrics.diagonal_step_count;
  const UWide right_steps =
      static_cast<UWide>(right_metrics.orthogonal_step_count) + right_metrics.diagonal_step_count;
  return std::tuple{left.negotiated_score,
                    left_metrics.intrinsic_base_cost,
                    left_metrics.via_count,
                    left_metrics.bend_count,
                    left_steps,
                    left_metrics.axis_aligned_length_dbu,
                    left_metrics.diagonal_projection_dbu,
                    left.candidate->id()} < std::tuple{right.negotiated_score,
                                                       right_metrics.intrinsic_base_cost,
                                                       right_metrics.via_count,
                                                       right_metrics.bend_count,
                                                       right_steps,
                                                       right_metrics.axis_aligned_length_dbu,
                                                       right_metrics.diagonal_projection_dbu,
                                                       right.candidate->id()};
}

using CandidateScoreResult = std::variant<CandidateScore, FixedPoolCpuMultiWorldError>;

[[nodiscard]] CandidateScoreResult ScoreCandidate(const candidates::RouteCandidate& candidate,
                                                  std::span<const NegotiatedResourcePrice> prices,
                                                  const FixedPoolCpuWorldSchedule& schedule,
                                                  const FixedPoolCpuMultiWorldConfig& config) {
  std::uint64_t score = candidate.data().metrics.intrinsic_base_cost;
  if (score > config.limits.maximum_candidate_score) {
    FixedPoolCpuMultiWorldError error =
        BoundError("allocator.fixed_pool_multi_world.candidate_score_bound.v1",
                   "A fixed-pool negotiated candidate score exceeds its configured bound",
                   config.limits.maximum_candidate_score, score);
    error.schedule_key = schedule.schedule_key;
    return error;
  }
  for (const candidates::PhysicalEdgeSpan& span : candidate.data().resources) {
    for (std::uint32_t offset = 0; offset < span.edge_count; ++offset) {
      const std::optional<routing::EdgeResourceKey> resource = ResourceAt(span, offset);
      if (!resource.has_value()) {
        FixedPoolCpuMultiWorldError error =
            Error(FixedPoolCpuMultiWorldErrorCode::kInvalidInput,
                  "allocator.fixed_pool_multi_world.candidate_resource.v1",
                  "A fixed-pool candidate resource coordinate is outside int64");
        error.schedule_key = schedule.schedule_key;
        return error;
      }
      const NegotiatedResourcePrice* price = FindPrice(prices, *resource);
      if (price == nullptr) {
        continue;
      }
      const std::optional<std::uint64_t> contribution =
          CheckedMultiply(price->total_price, span.usage_units);
      if (!contribution.has_value() || !CheckedAdd(*contribution, &score)) {
        FixedPoolCpuMultiWorldError error =
            Error(FixedPoolCpuMultiWorldErrorCode::kArithmeticOverflow,
                  "allocator.fixed_pool_multi_world.candidate_score_overflow.v1",
                  "A fixed-pool negotiated candidate score overflowed uint64");
        error.schedule_key = schedule.schedule_key;
        return error;
      }
      if (score > config.limits.maximum_candidate_score) {
        FixedPoolCpuMultiWorldError error =
            BoundError("allocator.fixed_pool_multi_world.candidate_score_bound.v1",
                       "A fixed-pool negotiated candidate score exceeds its configured bound",
                       config.limits.maximum_candidate_score, score);
        error.schedule_key = schedule.schedule_key;
        return error;
      }
    }
  }
  return CandidateScore{.candidate = &candidate, .negotiated_score = score};
}

struct RoundSelection {
  OneWorldSelection selection;
  std::uint64_t total_intrinsic_cost = 0;
  std::uint64_t selection_identity = 0;
};

using RoundSelectionResult = std::variant<RoundSelection, FixedPoolCpuMultiWorldError>;

[[nodiscard]] RoundSelectionResult SelectRound(const ResourceCapacityModel& capacities,
                                               std::span<const CpuCandidateAllocationPool> pools,
                                               const FixedPoolCpuWorldPriceState& price_state,
                                               const FixedPoolCpuWorldSchedule& schedule,
                                               const FixedPoolCpuMultiWorldConfig& config,
                                               std::uint64_t world_identity,
                                               std::uint32_t round_index) {
  OneWorldSelection selection{
      .associations = capacities.associations(),
      .nets = {},
      .accounting = {},
      .input_candidate_count = 0,
  };
  selection.nets.reserve(pools.size());
  std::vector<const candidates::RouteCandidate*> selected_candidates;
  selected_candidates.reserve(pools.size());
  std::uint64_t total_intrinsic_cost = 0;

  for (const CpuCandidateAllocationPool& pool : pools) {
    if (!CheckedAdd(pool.candidates().size(), &selection.input_candidate_count)) {
      FixedPoolCpuMultiWorldError error =
          Error(FixedPoolCpuMultiWorldErrorCode::kArithmeticOverflow,
                "allocator.fixed_pool_multi_world.candidate_count_overflow.v1",
                "Fixed-pool selection candidate count overflowed uint64");
      error.schedule_key = schedule.schedule_key;
      return error;
    }
    if (pool.candidates().empty()) {
      selection.nets.emplace_back(OneWorldCandidateAbsence{
          .net = pool.net(),
          .reason = OneWorldCandidateAbsenceReason::kEmptyPool,
      });
      continue;
    }
    std::optional<CandidateScore> best;
    for (const candidates::StoredCandidate& stored : pool.candidates()) {
      CandidateScoreResult score_result =
          ScoreCandidate(*stored, price_state.prices, schedule, config);
      if (auto* failure = std::get_if<FixedPoolCpuMultiWorldError>(&score_result);
          failure != nullptr) {
        return *failure;
      }
      CandidateScore score = std::get<CandidateScore>(score_result);
      if (!best.has_value() || CandidateRanksBefore(score, *best)) {
        best = score;
      }
    }
    if (!best.has_value() ||
        !CheckedAdd(best->candidate->data().metrics.intrinsic_base_cost, &total_intrinsic_cost)) {
      FixedPoolCpuMultiWorldError error =
          Error(FixedPoolCpuMultiWorldErrorCode::kArithmeticOverflow,
                "allocator.fixed_pool_multi_world.intrinsic_cost_overflow.v1",
                "Fixed-pool terminal intrinsic objective overflowed uint64");
      error.schedule_key = schedule.schedule_key;
      return error;
    }
    selected_candidates.push_back(best->candidate);
    selection.nets.emplace_back(OneWorldSelectedCandidate{
        .net = pool.net(),
        .candidate_id = best->candidate->id(),
    });
  }

  ResourceAccountingResult accounting =
      AccumulateResourceUsage(capacities, selected_candidates, config.limits.selection.accounting);
  if (const auto* failure = std::get_if<ResourceAccountingError>(&accounting); failure != nullptr) {
    FixedPoolCpuMultiWorldError error =
        Error(failure->code == ResourceAccountingErrorCode::kResourceExhausted
                  ? FixedPoolCpuMultiWorldErrorCode::kResourceExhausted
                  : FixedPoolCpuMultiWorldErrorCode::kAccountingFailure,
              failure->invariant_id, failure->detail);
    error.schedule_key = schedule.schedule_key;
    error.accounting_error = *failure;
    return error;
  }
  selection.accounting = std::get<ResourceAccounting>(std::move(accounting));
  const std::uint64_t identity =
      SelectionIdentity(world_identity, round_index, price_state.state_identity, selection);
  return RoundSelection{
      .selection = std::move(selection),
      .total_intrinsic_cost = total_intrinsic_cost,
      .selection_identity = identity,
  };
}

using PriceUpdateResult = std::variant<FixedPoolCpuWorldPriceState, FixedPoolCpuMultiWorldError>;

[[nodiscard]] PriceUpdateResult UpdatePrices(const FixedPoolCpuWorldPriceState& prior,
                                             const ResourceAccounting& accounting,
                                             const FixedPoolCpuWorldSchedule& schedule,
                                             const FixedPoolCpuMultiWorldConfig& config,
                                             std::uint64_t common_state_identity,
                                             std::uint64_t world_identity) {
  const std::uint64_t update_index = prior.completed_updates;
  const std::optional<std::uint64_t> increment =
      CheckedMultiply(update_index, schedule.price_policy.present_factor_increment);
  std::uint64_t present_factor = schedule.price_policy.initial_present_factor;
  if (!increment.has_value() || !CheckedAdd(*increment, &present_factor)) {
    FixedPoolCpuMultiWorldError error =
        Error(FixedPoolCpuMultiWorldErrorCode::kArithmeticOverflow,
              "allocator.fixed_pool_multi_world.present_factor_overflow.v1",
              "A fixed-pool world present-price factor overflowed uint64");
    error.schedule_key = schedule.schedule_key;
    return error;
  }

  std::map<routing::EdgeResourceKey, NegotiatedResourcePrice> prices;
  for (const NegotiatedResourcePrice& price : prior.prices) {
    if (price.historical_price != 0) {
      prices.emplace(price.resource, NegotiatedResourcePrice{
                                         .resource = price.resource,
                                         .present_price = 0,
                                         .historical_price = price.historical_price,
                                         .total_price = price.historical_price,
                                     });
    }
  }
  for (const ResourceUsage& usage : accounting.resources) {
    if (usage.overuse_units == 0) {
      continue;
    }
    const std::optional<std::uint64_t> present =
        CheckedMultiply(present_factor, usage.overuse_units);
    const std::optional<std::uint64_t> history =
        CheckedMultiply(schedule.price_policy.historical_price_increment, usage.overuse_units);
    if (!present.has_value() || !history.has_value()) {
      FixedPoolCpuMultiWorldError error =
          Error(FixedPoolCpuMultiWorldErrorCode::kArithmeticOverflow,
                "allocator.fixed_pool_multi_world.price_multiply_overflow.v1",
                "A fixed-pool world price multiplication overflowed uint64");
      error.schedule_key = schedule.schedule_key;
      return error;
    }
    NegotiatedResourcePrice& price = prices[usage.resource];
    price.resource = usage.resource;
    price.present_price = *present;
    if (!CheckedAdd(*history, &price.historical_price)) {
      FixedPoolCpuMultiWorldError error =
          Error(FixedPoolCpuMultiWorldErrorCode::kArithmeticOverflow,
                "allocator.fixed_pool_multi_world.history_overflow.v1",
                "A fixed-pool world historical price overflowed uint64");
      error.schedule_key = schedule.schedule_key;
      return error;
    }
    price.total_price = price.present_price;
    if (!CheckedAdd(price.historical_price, &price.total_price)) {
      FixedPoolCpuMultiWorldError error =
          Error(FixedPoolCpuMultiWorldErrorCode::kArithmeticOverflow,
                "allocator.fixed_pool_multi_world.total_price_overflow.v1",
                "A fixed-pool world total price overflowed uint64");
      error.schedule_key = schedule.schedule_key;
      return error;
    }
  }
  if (prices.size() > config.limits.maximum_price_entries_per_world) {
    FixedPoolCpuMultiWorldError error =
        BoundError("allocator.fixed_pool_multi_world.price_entry_bound.v1",
                   "A fixed-pool world price map exceeds its configured entry bound",
                   config.limits.maximum_price_entries_per_world, prices.size());
    error.schedule_key = schedule.schedule_key;
    return error;
  }

  FixedPoolCpuWorldPriceState state{
      .policy_identity = PricePolicyIdentity(schedule.price_policy),
      .prior_state_identity = prior.state_identity,
      .completed_updates = update_index + 1U,
      .present_factor = present_factor,
      .prices = {},
      .state_identity = 0,
  };
  std::uint64_t aggregate_price = 0;
  state.prices.reserve(prices.size());
  for (const auto& [resource, price] : prices) {
    static_cast<void>(resource);
    const std::uint64_t largest =
        std::max({price.present_price, price.historical_price, price.total_price});
    if (largest > config.limits.maximum_price_value) {
      FixedPoolCpuMultiWorldError error =
          BoundError("allocator.fixed_pool_multi_world.price_value_bound.v1",
                     "A fixed-pool world price exceeds its configured value bound",
                     config.limits.maximum_price_value, largest);
      error.schedule_key = schedule.schedule_key;
      return error;
    }
    if (!CheckedAdd(price.total_price, &aggregate_price)) {
      FixedPoolCpuMultiWorldError error =
          Error(FixedPoolCpuMultiWorldErrorCode::kArithmeticOverflow,
                "allocator.fixed_pool_multi_world.aggregate_price_overflow.v1",
                "A fixed-pool world aggregate price overflowed uint64");
      error.schedule_key = schedule.schedule_key;
      return error;
    }
    if (aggregate_price > config.limits.maximum_aggregate_price_per_world) {
      FixedPoolCpuMultiWorldError error =
          BoundError("allocator.fixed_pool_multi_world.aggregate_price_bound.v1",
                     "A fixed-pool world aggregate price exceeds its configured bound",
                     config.limits.maximum_aggregate_price_per_world, aggregate_price);
      error.schedule_key = schedule.schedule_key;
      return error;
    }
    state.prices.push_back(price);
  }
  state.state_identity = PriceStateIdentity(common_state_identity, world_identity, schedule, state);
  return state;
}

struct SourceShape {
  internal::FixedPoolCpuMultiWorldKnownWork work;
};

using SourceShapeResult = std::variant<SourceShape, FixedPoolCpuMultiWorldError>;

[[nodiscard]] SourceShapeResult InspectSource(const CpuCandidateAllocationSession& source,
                                              const FixedPoolCpuMultiWorldConfig& config) {
  if (source.pools().size() > config.limits.selection.maximum_net_pools) {
    return BoundError("allocator.fixed_pool_multi_world.source_net_bound.v1",
                      "The P4R-08 final pool roster exceeds the configured net bound",
                      config.limits.selection.maximum_net_pools, source.pools().size());
  }
  std::uint64_t candidate_count = 0;
  std::uint64_t candidate_resource_uses = 0;
  std::uint64_t maximum_selected_uses = 0;
  std::set<routing::EdgeResourceKey> unique_resources;
  for (const CpuCandidateAllocationPool& pool : source.pools()) {
    if (!CheckedAdd(pool.candidates().size(), &candidate_count)) {
      return Error(FixedPoolCpuMultiWorldErrorCode::kArithmeticOverflow,
                   "allocator.fixed_pool_multi_world.source_candidate_overflow.v1",
                   "The P4R-08 final candidate count overflowed uint64");
    }
    std::uint64_t pool_maximum = 0;
    for (const candidates::StoredCandidate& candidate : pool.candidates()) {
      std::uint64_t candidate_uses = 0;
      for (const candidates::PhysicalEdgeSpan& span : candidate->data().resources) {
        if (!CheckedAdd(span.edge_count, &candidate_uses) ||
            !CheckedAdd(span.edge_count, &candidate_resource_uses)) {
          return Error(FixedPoolCpuMultiWorldErrorCode::kArithmeticOverflow,
                       "allocator.fixed_pool_multi_world.source_resource_overflow.v1",
                       "The P4R-08 final candidate resource count overflowed uint64");
        }
        for (std::uint32_t offset = 0; offset < span.edge_count; ++offset) {
          const std::optional<routing::EdgeResourceKey> resource = ResourceAt(span, offset);
          if (!resource.has_value()) {
            return Error(FixedPoolCpuMultiWorldErrorCode::kInvalidInput,
                         "allocator.fixed_pool_multi_world.source_resource.v1",
                         "The P4R-08 final pool contains an invalid resource coordinate");
          }
          unique_resources.insert(*resource);
        }
      }
      pool_maximum = std::max(pool_maximum, candidate_uses);
    }
    if (!CheckedAdd(pool_maximum, &maximum_selected_uses)) {
      return Error(FixedPoolCpuMultiWorldErrorCode::kArithmeticOverflow,
                   "allocator.fixed_pool_multi_world.selected_resource_overflow.v1",
                   "The maximum selected resource count overflowed uint64");
    }
  }
  if (candidate_count > config.limits.selection.maximum_total_candidates) {
    return BoundError("allocator.fixed_pool_multi_world.source_candidate_bound.v1",
                      "The P4R-08 final pools exceed the configured candidate bound",
                      config.limits.selection.maximum_total_candidates, candidate_count);
  }
  if (source.pools().size() > config.limits.selection.accounting.maximum_candidates) {
    return BoundError("allocator.fixed_pool_multi_world.accounting_candidate_bound.v1",
                      "One selected candidate per source pool exceeds the accounting bound",
                      config.limits.selection.accounting.maximum_candidates, source.pools().size());
  }
  if (maximum_selected_uses > config.limits.selection.accounting.maximum_expanded_resource_uses) {
    return BoundError("allocator.fixed_pool_multi_world.accounting_resource_bound.v1",
                      "A selection can exceed the configured accounting resource bound",
                      config.limits.selection.accounting.maximum_expanded_resource_uses,
                      maximum_selected_uses);
  }
  return SourceShape{.work = internal::FixedPoolCpuMultiWorldKnownWork{
                         .world_count = 0,
                         .total_selection_rounds = 0,
                         .total_price_updates = 0,
                         .source_net_count = source.pools().size(),
                         .source_candidate_count = candidate_count,
                         .source_candidate_resource_uses = candidate_resource_uses,
                         .maximum_selected_resource_uses_per_round = maximum_selected_uses,
                         .maximum_price_entries_per_world = unique_resources.size(),
                     }};
}

struct CompletedWorld {
  FixedPoolCpuWorldSummary summary;
  FixedPoolCpuWorldPriceState price_state;
  OneWorldSelection selection;
};

[[nodiscard]] std::uint64_t ExecutionIdentity(
    const FixedPoolCpuMultiWorldExecution& execution) noexcept {
  board_ir::StableHashBuilder hash;
  hash.AddString("APGAR-P4R09-FIXED-POOL-MULTI-WORLD-EXECUTION-V1");
  HashConfig(&hash, execution.config());
  hash.AddU32(static_cast<std::uint32_t>(execution.disposition()));
  hash.AddU64(execution.source_session_identity());
  hash.AddU64(execution.source_pool_identity());
  hash.AddU64(execution.common_state_identity());
  hash.AddU64(static_cast<std::uint64_t>(execution.schedules().size()));
  for (const FixedPoolCpuWorldSchedule& schedule : execution.schedules()) {
    HashSchedule(&hash, schedule);
  }
  HashCounters(&hash, execution.counters());
  hash.AddBool(execution.preferred_world_identity().has_value());
  if (execution.preferred_world_identity().has_value()) {
    hash.AddU64(*execution.preferred_world_identity());
  }
  hash.AddU64(static_cast<std::uint64_t>(execution.summaries().size()));
  for (const FixedPoolCpuWorldSummary& summary : execution.summaries()) {
    hash.AddU64(summary.world_identity);
    HashSchedule(&hash, summary.schedule);
    hash.AddU32(static_cast<std::uint32_t>(summary.terminal_reason));
    hash.AddU64(summary.objective.selected_net_count);
    hash.AddU64(summary.objective.missing_net_count);
    hash.AddU64(summary.objective.total_overuse_units);
    hash.AddU64(summary.objective.total_intrinsic_cost);
    hash.AddU64(summary.final_price_state_identity);
    hash.AddU64(summary.final_selection_identity);
    hash.AddU64(summary.outcome_identity);
    hash.AddBool(summary.pareto_eligible);
    hash.AddBool(summary.pareto_retained);
    hash.AddU64(static_cast<std::uint64_t>(summary.trace.size()));
    for (const FixedPoolCpuWorldRoundTrace& trace : summary.trace) {
      hash.AddU32(trace.round_index);
      hash.AddU64(trace.input_price_state_identity);
      hash.AddU64(trace.selection_identity);
      hash.AddBool(trace.output_price_state_identity.has_value());
      if (trace.output_price_state_identity.has_value()) {
        hash.AddU64(*trace.output_price_state_identity);
      }
    }
  }
  hash.AddU64(static_cast<std::uint64_t>(execution.retained_worlds().size()));
  for (const RetainedFixedPoolCpuWorld& retained : execution.retained_worlds()) {
    hash.AddU64(retained.world_identity);
    hash.AddU64(retained.outcome_identity);
    hash.AddU64(retained.price_state.state_identity);
    HashSelection(&hash, retained.selection);
  }
  return NonzeroHash(&hash);
}

}  // namespace

struct FixedPoolCpuMultiWorldExecutionFactory {
  [[nodiscard]] static FixedPoolCpuMultiWorldExecution Make(
      FixedPoolCpuMultiWorldConfig config, FixedPoolCpuMultiWorldDisposition disposition,
      std::uint64_t source_session_identity, std::uint64_t source_pool_identity,
      std::uint64_t common_state_identity, std::vector<FixedPoolCpuWorldSchedule> schedules,
      std::vector<FixedPoolCpuWorldSummary> summaries,
      std::vector<RetainedFixedPoolCpuWorld> retained_worlds,
      std::optional<std::uint64_t> preferred_world_identity,
      FixedPoolCpuMultiWorldCounters counters) {
    FixedPoolCpuMultiWorldExecution execution;
    execution.config_ = std::move(config);
    execution.disposition_ = disposition;
    execution.source_session_identity_ = source_session_identity;
    execution.source_pool_identity_ = source_pool_identity;
    execution.common_state_identity_ = common_state_identity;
    execution.schedules_ = std::move(schedules);
    execution.summaries_ = std::move(summaries);
    execution.retained_worlds_ = std::move(retained_worlds);
    execution.preferred_world_identity_ = preferred_world_identity;
    execution.counters_ = counters;
    execution.execution_identity_ = ExecutionIdentity(execution);
    return execution;
  }
};

internal::FixedPoolCpuMultiWorldProjectionResult internal::ProjectFixedPoolCpuMultiWorldKnownWork(
    const FixedPoolCpuMultiWorldKnownWork& work, const FixedPoolCpuMultiWorldConfig& config,
    FixedPoolCpuMultiWorldWorkProjection* projection) noexcept {
  const UWide candidate_evaluations =
      static_cast<UWide>(work.total_selection_rounds) * work.source_candidate_count;
  const UWide candidate_resource_visits =
      static_cast<UWide>(work.total_selection_rounds) * work.source_candidate_resource_uses;
  const UWide selected_resource_uses = static_cast<UWide>(work.total_selection_rounds) *
                                       work.maximum_selected_resource_uses_per_round;
  const UWide net_outcomes =
      static_cast<UWide>(work.total_selection_rounds) * work.source_net_count;
  const UWide emitted_price_entries =
      static_cast<UWide>(work.total_price_updates) * work.maximum_price_entries_per_world;
  const UWide buffered_selection_records =
      static_cast<UWide>(work.world_count) * work.source_net_count;
  const UWide buffered_resource_records =
      static_cast<UWide>(work.world_count) * work.maximum_selected_resource_uses_per_round;
  const UWide buffered_price_records =
      static_cast<UWide>(work.world_count) * work.maximum_price_entries_per_world;
  const UWide pareto_comparisons =
      work.world_count == 0 ? 0
                            : static_cast<UWide>(work.world_count) * (work.world_count - 1U) / 2U;
  constexpr UWide kMax = std::numeric_limits<std::uint64_t>::max();
  if (candidate_evaluations > kMax || candidate_resource_visits > kMax ||
      selected_resource_uses > kMax || net_outcomes > kMax || emitted_price_entries > kMax ||
      buffered_selection_records > kMax || buffered_resource_records > kMax ||
      buffered_price_records > kMax || pareto_comparisons > kMax) {
    return FixedPoolCpuMultiWorldProjectionResult::kArithmeticOverflow;
  }
  const FixedPoolCpuMultiWorldLimits& limits = config.limits;
  if (work.world_count == 0 || work.world_count > limits.maximum_worlds ||
      work.total_selection_rounds < work.world_count ||
      work.total_selection_rounds > limits.maximum_total_selection_rounds ||
      work.total_price_updates != work.total_selection_rounds - work.world_count ||
      work.total_price_updates > limits.maximum_total_price_updates ||
      work.maximum_price_entries_per_world > limits.maximum_price_entries_per_world ||
      candidate_evaluations > limits.maximum_candidate_evaluations ||
      candidate_resource_visits > limits.maximum_candidate_resource_visits ||
      selected_resource_uses > limits.maximum_selected_resource_uses ||
      net_outcomes > limits.maximum_net_outcomes ||
      emitted_price_entries > limits.maximum_emitted_price_entries ||
      work.total_selection_rounds > limits.maximum_trace_records ||
      buffered_selection_records > limits.maximum_buffered_terminal_selection_records ||
      buffered_resource_records > limits.maximum_buffered_terminal_resource_records ||
      buffered_price_records > limits.maximum_buffered_terminal_price_records ||
      pareto_comparisons > limits.maximum_pareto_comparisons) {
    return FixedPoolCpuMultiWorldProjectionResult::kBoundExhausted;
  }
  if (projection != nullptr) {
    *projection = FixedPoolCpuMultiWorldWorkProjection{
        .candidate_evaluations = static_cast<std::uint64_t>(candidate_evaluations),
        .candidate_resource_visits = static_cast<std::uint64_t>(candidate_resource_visits),
        .selected_resource_uses = static_cast<std::uint64_t>(selected_resource_uses),
        .net_outcomes = static_cast<std::uint64_t>(net_outcomes),
        .emitted_price_entries = static_cast<std::uint64_t>(emitted_price_entries),
        .trace_records = work.total_selection_rounds,
        .buffered_terminal_selection_records =
            static_cast<std::uint64_t>(buffered_selection_records),
        .buffered_terminal_resource_records = static_cast<std::uint64_t>(buffered_resource_records),
        .buffered_terminal_price_records = static_cast<std::uint64_t>(buffered_price_records),
        .pareto_comparisons = static_cast<std::uint64_t>(pareto_comparisons),
    };
  }
  return FixedPoolCpuMultiWorldProjectionResult::kFits;
}

bool internal::FixedPoolCpuWorldDominatesForTesting(
    const FixedPoolCpuWorldObjective& left, const FixedPoolCpuWorldObjective& right) noexcept {
  return Dominates(left, right);
}

bool internal::FixedPoolCpuWorldPreferredBeforeForTesting(
    const FixedPoolCpuWorldSummary& left, const FixedPoolCpuWorldSummary& right) noexcept {
  return PreferredBefore(left, right);
}

namespace {

[[nodiscard]] FixedPoolCpuMultiWorldResult RunImpl(
    const board_ir::BoardSnapshot& board, const ResourceCapacityModel& capacities,
    const CpuCandidateAllocationSession& source, CpuCandidateAllocationSessionConfig source_config,
    std::span<const FixedPoolCpuWorldSchedule> submitted_schedules,
    FixedPoolCpuMultiWorldConfig config, internal::FixedPoolCpuMultiWorldTestHooks hooks) {
  if (!ConfigurationIsValid(config)) {
    return Error(FixedPoolCpuMultiWorldErrorCode::kInvalidConfiguration,
                 "allocator.fixed_pool_multi_world.configuration.v1",
                 "Fixed-pool Multi-World configuration is outside its hard bounds");
  }
  if (board.content_hash() != capacities.associations().board_content_hash) {
    return Error(FixedPoolCpuMultiWorldErrorCode::kAssociationMismatch,
                 "allocator.fixed_pool_multi_world.capacity_board.v1",
                 "Fixed-pool Multi-World Board IR and capacities do not match");
  }
  if (std::optional<CpuCandidateAllocationSessionError> source_error =
          ValidateCpuCandidateAllocationSessionReplay(board, capacities, source, source_config);
      source_error.has_value()) {
    FixedPoolCpuMultiWorldError error =
        Error(FixedPoolCpuMultiWorldErrorCode::kSourceReplayFailure,
              "allocator.fixed_pool_multi_world.source_replay.v1",
              "The supplied P4R-08 terminal result failed replay validation");
    error.source_error = *source_error;
    return error;
  }
  try {
    SourceShapeResult shape_result = InspectSource(source, config);
    if (auto* failure = std::get_if<FixedPoolCpuMultiWorldError>(&shape_result);
        failure != nullptr) {
      return *failure;
    }
    SourceShape shape = std::get<SourceShape>(shape_result);

    if (submitted_schedules.empty() || submitted_schedules.size() > config.limits.maximum_worlds) {
      return Error(FixedPoolCpuMultiWorldErrorCode::kInvalidSchedule,
                   "allocator.fixed_pool_multi_world.schedule_count.v1",
                   "Fixed-pool Multi-World requires a bounded nonempty schedule roster");
    }
    std::uint64_t total_rounds = 0;
    std::uint64_t total_updates = 0;
    for (const FixedPoolCpuWorldSchedule& schedule : submitted_schedules) {
      if (schedule.schedule_key == 0 || schedule.maximum_selection_rounds == 0 ||
          schedule.price_policy.present_factor_increment == 0 ||
          schedule.price_policy.historical_price_increment == 0) {
        FixedPoolCpuMultiWorldError error =
            Error(FixedPoolCpuMultiWorldErrorCode::kInvalidSchedule,
                  "allocator.fixed_pool_multi_world.schedule_value.v1",
                  "Every fixed-pool schedule requires a nonzero key, rounds, and price increments");
        error.schedule_key = schedule.schedule_key;
        return error;
      }
      if (!CheckedAdd(schedule.maximum_selection_rounds, &total_rounds) ||
          !CheckedAdd(schedule.maximum_selection_rounds - 1U, &total_updates)) {
        return Error(FixedPoolCpuMultiWorldErrorCode::kArithmeticOverflow,
                     "allocator.fixed_pool_multi_world.schedule_total_overflow.v1",
                     "Aggregate fixed-pool schedule rounds overflowed uint64");
      }
    }
    std::vector<FixedPoolCpuWorldSchedule> schedules(submitted_schedules.begin(),
                                                     submitted_schedules.end());
    std::ranges::sort(schedules, {}, &FixedPoolCpuWorldSchedule::schedule_key);
    if (std::ranges::adjacent_find(schedules, {}, &FixedPoolCpuWorldSchedule::schedule_key) !=
        schedules.end()) {
      return Error(FixedPoolCpuMultiWorldErrorCode::kDuplicateSchedule,
                   "allocator.fixed_pool_multi_world.duplicate_schedule_key.v1",
                   "Fixed-pool Multi-World schedules contain a duplicate key");
    }
    std::set<std::tuple<std::uint64_t, std::uint64_t, std::uint64_t, std::uint32_t>> exact;
    for (const FixedPoolCpuWorldSchedule& schedule : schedules) {
      if (!exact
               .emplace(schedule.price_policy.initial_present_factor,
                        schedule.price_policy.present_factor_increment,
                        schedule.price_policy.historical_price_increment,
                        schedule.maximum_selection_rounds)
               .second) {
        return Error(FixedPoolCpuMultiWorldErrorCode::kDuplicateSchedule,
                     "allocator.fixed_pool_multi_world.duplicate_schedule.v1",
                     "Different fixed-pool keys describe the same exact schedule");
      }
    }

    shape.work.world_count = schedules.size();
    shape.work.total_selection_rounds = total_rounds;
    shape.work.total_price_updates = total_updates;
    internal::FixedPoolCpuMultiWorldWorkProjection reservation;
    const internal::FixedPoolCpuMultiWorldProjectionResult projection =
        internal::ProjectFixedPoolCpuMultiWorldKnownWork(shape.work, config, &reservation);
    if (projection == internal::FixedPoolCpuMultiWorldProjectionResult::kArithmeticOverflow) {
      return Error(FixedPoolCpuMultiWorldErrorCode::kArithmeticOverflow,
                   "allocator.fixed_pool_multi_world.known_work_overflow.v1",
                   "Whole-execution fixed-pool work projection overflowed uint64");
    }
    if (projection == internal::FixedPoolCpuMultiWorldProjectionResult::kBoundExhausted) {
      return Error(FixedPoolCpuMultiWorldErrorCode::kBoundExhausted,
                   "allocator.fixed_pool_multi_world.known_work_bound.v1",
                   "Whole-execution fixed-pool work exceeds a configured bound");
    }

    const std::uint64_t common_state_identity = CommonStateIdentity(capacities, source);
    FixedPoolCpuMultiWorldCounters counters{.scheduled_worlds = schedules.size()};
    std::vector<CompletedWorld> completed;
    completed.reserve(schedules.size());
    std::uint64_t buffered_selection_records = 0;
    std::uint64_t buffered_resource_records = 0;
    std::uint64_t buffered_price_records = 0;

    for (std::size_t schedule_index = 0; schedule_index < schedules.size(); ++schedule_index) {
      if (hooks.before_world != nullptr && !hooks.before_world(schedule_index, hooks.context)) {
        return Error(FixedPoolCpuMultiWorldErrorCode::kInternalInvariant,
                     "allocator.fixed_pool_multi_world.injected_world_failure.v1",
                     "A test-only fixed-pool world hook injected a fatal failure");
      }
      const FixedPoolCpuWorldSchedule& schedule = schedules[schedule_index];
      const std::uint64_t world_identity = WorldIdentity(common_state_identity, schedule);
      FixedPoolCpuWorldPriceState price_state{
          .policy_identity = 0,
          .prior_state_identity = 0,
          .completed_updates = 0,
          .present_factor = 0,
          .prices = {},
          .state_identity = common_state_identity,
      };
      FixedPoolCpuWorldSummary summary{
          .world_identity = world_identity,
          .schedule = schedule,
          .terminal_reason = FixedPoolCpuWorldTerminalReason::kSelectionRoundBound,
          .trace = {},
          .objective = {},
          .final_price_state_identity = 0,
          .final_selection_identity = 0,
          .outcome_identity = 0,
          .pareto_eligible = false,
          .pareto_retained = false,
      };
      summary.trace.reserve(schedule.maximum_selection_rounds);
      std::optional<OneWorldSelection> final_selection;

      for (std::uint32_t round_index = 0; round_index < schedule.maximum_selection_rounds;
           ++round_index) {
        RoundSelectionResult round_result = SelectRound(
            capacities, source.pools(), price_state, schedule, config, world_identity, round_index);
        if (auto* failure = std::get_if<FixedPoolCpuMultiWorldError>(&round_result);
            failure != nullptr) {
          return *failure;
        }
        RoundSelection selected = std::get<RoundSelection>(std::move(round_result));
        summary.trace.push_back(FixedPoolCpuWorldRoundTrace{
            .round_index = round_index,
            .input_price_state_identity = price_state.state_identity,
            .selection_identity = selected.selection_identity,
            .output_price_state_identity = std::nullopt,
        });
        if (!CheckedAdd(1, &counters.selection_rounds) ||
            !CheckedAdd(shape.work.source_candidate_count, &counters.candidate_evaluations) ||
            !CheckedAdd(shape.work.source_candidate_resource_uses,
                        &counters.candidate_resource_visits) ||
            !CheckedAdd(selected.selection.accounting.expanded_resource_uses,
                        &counters.selected_resource_uses) ||
            !CheckedAdd(shape.work.source_net_count, &counters.net_outcomes) ||
            !CheckedAdd(1, &counters.trace_records)) {
          return Error(FixedPoolCpuMultiWorldErrorCode::kArithmeticOverflow,
                       "allocator.fixed_pool_multi_world.runtime_counter_overflow.v1",
                       "Fixed-pool runtime work counters overflowed uint64");
        }

        const std::uint64_t selected_count = selected.selection.accounting.candidate_count;
        const std::uint64_t missing_count = shape.work.source_net_count - selected_count;
        const bool feasible =
            missing_count == 0 && selected.selection.accounting.total_overuse_units == 0;
        const bool no_candidate = missing_count != 0;
        const bool final_round = round_index + 1U == schedule.maximum_selection_rounds;
        if (feasible || no_candidate || final_round) {
          summary.terminal_reason =
              feasible       ? FixedPoolCpuWorldTerminalReason::kFeasible
              : no_candidate ? FixedPoolCpuWorldTerminalReason::kNoCandidateWithoutRegeneration
                             : FixedPoolCpuWorldTerminalReason::kSelectionRoundBound;
          summary.objective = FixedPoolCpuWorldObjective{
              .selected_net_count = selected_count,
              .missing_net_count = missing_count,
              .total_overuse_units = selected.selection.accounting.total_overuse_units,
              .total_intrinsic_cost = selected.total_intrinsic_cost,
          };
          summary.final_price_state_identity = price_state.state_identity;
          summary.final_selection_identity = selected.selection_identity;
          summary.pareto_eligible = missing_count <= config.maximum_near_feasible_missing_nets &&
                                    selected.selection.accounting.total_overuse_units <=
                                        config.maximum_near_feasible_overuse_units;
          final_selection = std::move(selected.selection);
          summary.outcome_identity = OutcomeIdentity(summary, *final_selection);
          break;
        }

        PriceUpdateResult update =
            UpdatePrices(price_state, selected.selection.accounting, schedule, config,
                         common_state_identity, world_identity);
        if (auto* failure = std::get_if<FixedPoolCpuMultiWorldError>(&update); failure != nullptr) {
          return *failure;
        }
        FixedPoolCpuWorldPriceState next = std::get<FixedPoolCpuWorldPriceState>(std::move(update));
        summary.trace.back().output_price_state_identity = next.state_identity;
        if (!CheckedAdd(1, &counters.price_updates) ||
            !CheckedAdd(next.prices.size(), &counters.emitted_price_entries)) {
          return Error(FixedPoolCpuMultiWorldErrorCode::kArithmeticOverflow,
                       "allocator.fixed_pool_multi_world.price_counter_overflow.v1",
                       "Fixed-pool price-update counters overflowed uint64");
        }
        price_state = std::move(next);
      }
      if (!final_selection.has_value() || summary.outcome_identity == 0) {
        return Error(FixedPoolCpuMultiWorldErrorCode::kInternalInvariant,
                     "allocator.fixed_pool_multi_world.missing_terminal.v1",
                     "A bounded fixed-pool world produced no terminal outcome");
      }
      if (!CheckedAdd(1, &counters.completed_worlds) ||
          !CheckedAdd(final_selection->nets.size(), &buffered_selection_records) ||
          !CheckedAdd(final_selection->accounting.resources.size(), &buffered_resource_records) ||
          !CheckedAdd(price_state.prices.size(), &buffered_price_records)) {
        return Error(FixedPoolCpuMultiWorldErrorCode::kArithmeticOverflow,
                     "allocator.fixed_pool_multi_world.buffered_counter_overflow.v1",
                     "Buffered fixed-pool terminal records overflowed uint64");
      }
      if (buffered_selection_records > config.limits.maximum_buffered_terminal_selection_records ||
          buffered_resource_records > config.limits.maximum_buffered_terminal_resource_records ||
          buffered_price_records > config.limits.maximum_buffered_terminal_price_records) {
        return Error(FixedPoolCpuMultiWorldErrorCode::kBoundExhausted,
                     "allocator.fixed_pool_multi_world.buffered_terminal_bound.v1",
                     "Buffered fixed-pool terminal records exceed a configured bound");
      }
      completed.push_back(CompletedWorld{
          .summary = std::move(summary),
          .price_state = std::move(price_state),
          .selection = std::move(*final_selection),
      });
    }

    if (counters.selection_rounds > total_rounds || counters.price_updates > total_updates ||
        counters.candidate_evaluations > reservation.candidate_evaluations ||
        counters.candidate_resource_visits > reservation.candidate_resource_visits ||
        counters.selected_resource_uses > reservation.selected_resource_uses ||
        counters.net_outcomes > reservation.net_outcomes ||
        counters.emitted_price_entries > reservation.emitted_price_entries ||
        counters.trace_records > reservation.trace_records) {
      return Error(FixedPoolCpuMultiWorldErrorCode::kInternalInvariant,
                   "allocator.fixed_pool_multi_world.runtime_reservation.v1",
                   "Fixed-pool runtime counters exceeded the whole-execution reservation");
    }

    std::vector<bool> dominated(completed.size(), false);
    for (std::size_t left = 0; left < completed.size(); ++left) {
      if (!completed[left].summary.pareto_eligible) {
        continue;
      }
      if (!CheckedAdd(1, &counters.pareto_eligible_worlds)) {
        return Error(FixedPoolCpuMultiWorldErrorCode::kArithmeticOverflow,
                     "allocator.fixed_pool_multi_world.eligible_counter_overflow.v1",
                     "Fixed-pool eligible-world count overflowed uint64");
      }
      for (std::size_t right = left + 1U; right < completed.size(); ++right) {
        if (!completed[right].summary.pareto_eligible) {
          continue;
        }
        if (!CheckedAdd(1, &counters.pareto_comparisons)) {
          return Error(FixedPoolCpuMultiWorldErrorCode::kArithmeticOverflow,
                       "allocator.fixed_pool_multi_world.pareto_counter_overflow.v1",
                       "Fixed-pool Pareto-comparison count overflowed uint64");
        }
        if (Dominates(completed[left].summary.objective, completed[right].summary.objective)) {
          dominated[right] = true;
        } else if (Dominates(completed[right].summary.objective,
                             completed[left].summary.objective)) {
          dominated[left] = true;
        }
      }
    }

    std::uint64_t retained_selection_records = 0;
    std::uint64_t retained_resource_records = 0;
    std::uint64_t retained_price_records = 0;
    for (std::size_t index = 0; index < completed.size(); ++index) {
      if (!completed[index].summary.pareto_eligible || dominated[index]) {
        continue;
      }
      completed[index].summary.pareto_retained = true;
      if (!CheckedAdd(1, &counters.retained_worlds) ||
          !CheckedAdd(completed[index].selection.nets.size(), &retained_selection_records) ||
          !CheckedAdd(completed[index].selection.accounting.resources.size(),
                      &retained_resource_records) ||
          !CheckedAdd(completed[index].price_state.prices.size(), &retained_price_records)) {
        return Error(FixedPoolCpuMultiWorldErrorCode::kArithmeticOverflow,
                     "allocator.fixed_pool_multi_world.retention_counter_overflow.v1",
                     "Fixed-pool retained-record counts overflowed uint64");
      }
    }
    if (counters.retained_worlds > config.limits.maximum_retained_worlds ||
        retained_selection_records > config.limits.maximum_retained_selection_records ||
        retained_resource_records > config.limits.maximum_retained_resource_records ||
        retained_price_records > config.limits.maximum_retained_price_records) {
      return Error(FixedPoolCpuMultiWorldErrorCode::kRetentionBoundExhausted,
                   "allocator.fixed_pool_multi_world.pareto_retention_bound.v1",
                   "The exact fixed-pool Pareto frontier exceeds a retention bound");
    }

    std::optional<std::size_t> preferred_index;
    for (std::size_t index = 0; index < completed.size(); ++index) {
      if (!completed[index].summary.pareto_retained) {
        continue;
      }
      if (!preferred_index.has_value() ||
          PreferredBefore(completed[index].summary, completed[*preferred_index].summary)) {
        preferred_index = index;
      }
    }
    std::optional<std::uint64_t> preferred_world_identity;
    if (preferred_index.has_value()) {
      preferred_world_identity = completed[*preferred_index].summary.world_identity;
    }

    std::vector<FixedPoolCpuWorldSummary> summaries;
    std::vector<RetainedFixedPoolCpuWorld> retained;
    summaries.reserve(completed.size());
    retained.reserve(counters.retained_worlds);
    for (CompletedWorld& world : completed) {
      summaries.push_back(world.summary);
      if (!world.summary.pareto_retained) {
        continue;
      }
      retained.push_back(RetainedFixedPoolCpuWorld{
          .world_identity = world.summary.world_identity,
          .schedule = world.summary.schedule,
          .terminal_reason = world.summary.terminal_reason,
          .objective = world.summary.objective,
          .price_state = std::move(world.price_state),
          .selection = std::move(world.selection),
          .trace = world.summary.trace,
          .outcome_identity = world.summary.outcome_identity,
      });
    }
    const FixedPoolCpuMultiWorldDisposition disposition =
        retained.empty() ? FixedPoolCpuMultiWorldDisposition::kNoEligibleWorlds
                         : FixedPoolCpuMultiWorldDisposition::kCompleted;
    return FixedPoolCpuMultiWorldExecutionFactory::Make(
        std::move(config), disposition, source.session_identity(), source.final_pool_identity(),
        common_state_identity, std::move(schedules), std::move(summaries), std::move(retained),
        preferred_world_identity, counters);
  } catch (const std::bad_alloc&) {
    return Error(FixedPoolCpuMultiWorldErrorCode::kResourceExhausted,
                 "allocator.fixed_pool_multi_world.allocation.v1",
                 "Fixed-pool Multi-World execution exhausted host memory");
  } catch (const std::length_error&) {
    return Error(FixedPoolCpuMultiWorldErrorCode::kResourceExhausted,
                 "allocator.fixed_pool_multi_world.container_capacity.v1",
                 "Fixed-pool Multi-World execution exceeded container capacity");
  } catch (...) {
    return Error(FixedPoolCpuMultiWorldErrorCode::kInternalInvariant,
                 "allocator.fixed_pool_multi_world.exception.v1",
                 "Fixed-pool Multi-World execution raised an unexpected exception");
  }
}

}  // namespace

FixedPoolCpuMultiWorldResult ExecuteFixedPoolCpuMultiWorld(
    const board_ir::BoardSnapshot& board, const ResourceCapacityModel& capacities,
    const CpuCandidateAllocationSession& source, CpuCandidateAllocationSessionConfig source_config,
    std::span<const FixedPoolCpuWorldSchedule> schedules, FixedPoolCpuMultiWorldConfig config) {
  return RunImpl(board, capacities, source, std::move(source_config), schedules, std::move(config),
                 {});
}

FixedPoolCpuMultiWorldResult internal::ExecuteFixedPoolCpuMultiWorldWithTestHooks(
    const board_ir::BoardSnapshot& board, const ResourceCapacityModel& capacities,
    const CpuCandidateAllocationSession& source, CpuCandidateAllocationSessionConfig source_config,
    std::span<const FixedPoolCpuWorldSchedule> schedules, FixedPoolCpuMultiWorldConfig config,
    FixedPoolCpuMultiWorldTestHooks hooks) {
  return RunImpl(board, capacities, source, std::move(source_config), schedules, std::move(config),
                 hooks);
}

}  // namespace apgar::allocator
