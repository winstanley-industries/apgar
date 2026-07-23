#include "apgar/allocator/targeted_regeneration.h"

#include <algorithm>
#include <chrono>
#include <limits>
#include <new>
#include <queue>
#include <ranges>
#include <stdexcept>
#include <tuple>
#include <utility>

#include "apgar/board_ir/stable_hash.h"
#include "src/allocator/one_world_internal.h"
#include "src/allocator/targeted_regeneration_internal.h"
#include "src/operational_timestamp.h"

namespace apgar::allocator {
namespace {

using UWide = __uint128_t;
using Wide = __int128_t;
using OperationalClock = std::chrono::steady_clock;

[[nodiscard]] std::uint64_t OperationalElapsed(OperationalClock::time_point start) noexcept {
  const auto elapsed =
      std::chrono::duration_cast<std::chrono::nanoseconds>(OperationalClock::now() - start).count();
  return elapsed <= 0 ? 0 : static_cast<std::uint64_t>(elapsed);
}

[[nodiscard]] TargetedRegenerationError Error(TargetedRegenerationErrorCode code,
                                              std::string_view invariant_id,
                                              std::string_view detail) noexcept {
  return TargetedRegenerationError{.code = code, .invariant_id = invariant_id, .detail = detail};
}

[[nodiscard]] bool ConfigIsValid(const TargetedRegenerationConfig& config) noexcept {
  return config.maximum_target_nets > 0 && config.maximum_target_nets <= kMaximumAllocatorNetsV1 &&
         config.maximum_columns_per_net > 0 &&
         config.maximum_columns_per_net <= kMaximumAllocatorCandidatesV1 &&
         config.maximum_total_columns > 0 &&
         config.maximum_total_columns <= kMaximumAllocatorCandidatesV1 &&
         config.maximum_resource_actions_per_net > 0 &&
         config.maximum_resource_actions_per_net <= kMaximumAllocatorResourceRecordsV1 &&
         config.maximum_total_resource_actions > 0 &&
         config.maximum_total_resource_actions <= kMaximumAllocatorResourceRecordsV1 &&
         config.maximum_expanded_resource_visits > 0 &&
         config.maximum_expanded_resource_visits <= kMaximumAllocatorExpandedResourceUsesV1;
}

void AddResourceKey(board_ir::StableHashBuilder& hash,
                    const routing::EdgeResourceKey& resource) noexcept {
  hash.AddU32(resource.layer);
  hash.AddI64(resource.lattice_x);
  hash.AddI64(resource.lattice_y);
  hash.AddByte(static_cast<std::uint8_t>(resource.direction));
}

[[nodiscard]] routing::EdgeResourceKey ResourceAt(const candidates::PhysicalEdgeSpan& span,
                                                  std::uint32_t offset) noexcept {
  const geometry_compiler::DirectionDelta delta =
      candidates::ResourceSpanStorageDelta(span.direction);
  const Wide x = static_cast<Wide>(span.lattice_x) + static_cast<Wide>(delta.x) * offset;
  const Wide y = static_cast<Wide>(span.lattice_y) + static_cast<Wide>(delta.y) * offset;
  return routing::EdgeResourceKey{
      .layer = span.layer,
      .lattice_x = static_cast<std::int64_t>(x),
      .lattice_y = static_cast<std::int64_t>(y),
      .direction = span.direction,
  };
}

[[nodiscard]] bool ActionRanksBefore(const RegenerationResourceAction& left,
                                     const RegenerationResourceAction& right) noexcept {
  if (left.conflict_impact != right.conflict_impact) {
    return left.conflict_impact > right.conflict_impact;
  }
  if (left.observed_overuse_units != right.observed_overuse_units) {
    return left.observed_overuse_units > right.observed_overuse_units;
  }
  if (left.negotiated_price != right.negotiated_price) {
    return left.negotiated_price > right.negotiated_price;
  }
  return left.resource < right.resource;
}

struct ActionBetter {
  [[nodiscard]] bool operator()(const RegenerationResourceAction& left,
                                const RegenerationResourceAction& right) const noexcept {
    return ActionRanksBefore(left, right);
  }
};

[[nodiscard]] bool TargetRanksBefore(const TargetedRegenerationNet& left,
                                     const TargetedRegenerationNet& right) noexcept {
  if (left.conflict_impact != right.conflict_impact) {
    return left.conflict_impact > right.conflict_impact;
  }
  if (left.negotiated_price_exposure != right.negotiated_price_exposure) {
    return left.negotiated_price_exposure > right.negotiated_price_exposure;
  }
  if (left.conflict_resource_count != right.conflict_resource_count) {
    return left.conflict_resource_count > right.conflict_resource_count;
  }
  return std::tie(left.net.id, left.net.generation, left.next_price_candidate_id) <
         std::tie(right.net.id, right.net.generation, right.next_price_candidate_id);
}

[[nodiscard]] bool NetRanksBefore(const board_ir::EntityRef& left,
                                  const board_ir::EntityRef& right) noexcept {
  return std::tie(left.id, left.generation) < std::tie(right.id, right.generation);
}

struct TargetBetter {
  [[nodiscard]] bool operator()(const TargetedRegenerationNet& left,
                                const TargetedRegenerationNet& right) const noexcept {
    return TargetRanksBefore(left, right);
  }
};

[[nodiscard]] bool SelectionProjectionMatches(
    const OneWorldAllocation& world, const internal::OneWorldSelectionEvidence& evidence) noexcept {
  const OneWorldAllocation& projection = evidence.selection_projection;
  if (world.schema_version != projection.schema_version ||
      world.associations != projection.associations ||
      world.workload_checksum != projection.workload_checksum ||
      world.price_iteration != projection.price_iteration ||
      world.default_capacity_units != projection.default_capacity_units ||
      world.intrinsic_cost_weight != projection.intrinsic_cost_weight ||
      world.selections.size() != projection.selections.size() ||
      world.selected_net_count != projection.selected_net_count ||
      world.no_candidate_net_count != projection.no_candidate_net_count ||
      world.total_intrinsic_cost != projection.total_intrinsic_cost ||
      world.total_selection_score != projection.total_selection_score ||
      world.scoring_span_queries != projection.scoring_span_queries ||
      world.scoring_price_matches != projection.scoring_price_matches) {
    return false;
  }
  for (std::size_t index = 0; index < world.selections.size(); ++index) {
    const NetSelection& actual = world.selections[index];
    const NetSelection& expected = projection.selections[index];
    if (actual.net != expected.net || actual.status != expected.status ||
        actual.candidate_id != expected.candidate_id ||
        actual.candidate_payload_checksum != expected.candidate_payload_checksum ||
        (actual.candidate != nullptr) != (expected.candidate != nullptr) ||
        (actual.candidate != nullptr && actual.candidate->data() != expected.candidate->data()) ||
        actual.intrinsic_cost != expected.intrinsic_cost ||
        actual.price_cost != expected.price_cost ||
        actual.selection_score != expected.selection_score) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] TargetedRegenerationError TranslateSelectionFailure(
    const AllocationError& failure, std::string_view invariant_id,
    std::string_view detail) noexcept {
  return Error(
      failure.code == AllocationErrorCode::kResourceExhausted
          ? TargetedRegenerationErrorCode::kResourceExhausted
          : TargetedRegenerationErrorCode::kInvalidPricingInput,
      failure.code == AllocationErrorCode::kResourceExhausted ? failure.invariant_id : invariant_id,
      failure.code == AllocationErrorCode::kResourceExhausted ? failure.detail : detail);
}

template <typename Operation>
[[nodiscard]] auto WithFailureEnvelope(Operation&& operation)
    -> decltype(std::forward<Operation>(operation)()) {
  try {
    return std::forward<Operation>(operation)();
  } catch (const std::bad_alloc&) {
    return Error(TargetedRegenerationErrorCode::kResourceExhausted,
                 "allocator.targeted_regeneration.host_memory_exhausted.v1",
                 "Host allocation failed within targeted-regeneration bounds");
  } catch (const std::length_error&) {
    return Error(TargetedRegenerationErrorCode::kResourceExhausted,
                 "allocator.targeted_regeneration.host_container_exhausted.v1",
                 "Host container limits were exhausted within regeneration bounds");
  }
}

[[nodiscard]] TargetedRegenerationError TranslatePolicyFailure(
    const routing::CandidatePolicyError& failure) noexcept {
  switch (failure.code) {
    case routing::CandidatePolicyErrorCode::kCostOverflow:
      return Error(TargetedRegenerationErrorCode::kArithmeticOverflow,
                   "allocator.targeted_regeneration.policy_cost.v1",
                   "A targeted-regeneration policy cost exceeds finite uint64 routing cost");
    case routing::CandidatePolicyErrorCode::kTooManyResources:
      return Error(TargetedRegenerationErrorCode::kWorkBoundExceeded,
                   "allocator.targeted_regeneration.policy_resources.v1",
                   "Targeted-regeneration policy resources exceed the schema-v1 work bound");
    case routing::CandidatePolicyErrorCode::kUnsupportedSchema:
    case routing::CandidatePolicyErrorCode::kUnsupportedObjective:
    case routing::CandidatePolicyErrorCode::kInvalidResource:
    case routing::CandidatePolicyErrorCode::kConflictingResourceAction:
    case routing::CandidatePolicyErrorCode::kInvalidAlternativeSchedule:
      return Error(TargetedRegenerationErrorCode::kInvalidPricingInput,
                   "allocator.targeted_regeneration.policy_input.v1",
                   "Targeted-regeneration policy input is invalid for the prepared net");
  }
  return Error(TargetedRegenerationErrorCode::kInternalInvariant,
               "allocator.targeted_regeneration.policy_error.v1",
               "Candidate-policy validation returned an unknown error");
}

}  // namespace

bool internal::TargetedRegenerationConfigIsValidV1(
    const TargetedRegenerationConfig& config) noexcept {
  return ConfigIsValid(config);
}

std::uint64_t internal::ComputeTargetedRegenerationPlanChecksumV1(
    const TargetedRegenerationChecksumHeaderV1& header,
    std::span<const TargetedRegenerationNet> targets) noexcept {
  board_ir::StableHashBuilder hash;
  hash.AddString("APGAR-TARGETED-REGENERATION-PLAN-V1");
  hash.AddU32(header.schema_version);
  hash.AddU64(header.associations.board_content_hash);
  hash.AddU64(header.associations.compiler_profile_fingerprint);
  hash.AddU32(header.associations.geometry_compiler_version);
  hash.AddU64(header.workload_checksum);
  hash.AddU64(header.source_request_manifest_checksum);
  hash.AddU64(header.candidate_pool_manifest_checksum);
  hash.AddU64(header.source_pool_count);
  hash.AddU64(header.source_candidate_count);
  hash.AddU64(header.pinned_candidate_count);
  hash.AddU64(header.config.maximum_target_nets);
  hash.AddU64(header.config.maximum_columns_per_net);
  hash.AddU64(header.config.maximum_total_columns);
  hash.AddU64(header.config.maximum_resource_actions_per_net);
  hash.AddU64(header.config.maximum_total_resource_actions);
  hash.AddU64(header.config.maximum_expanded_resource_visits);
  hash.AddU64(header.price_state_checksum);
  hash.AddU32(header.price_iteration);
  hash.AddU64(header.source_world_checksum);
  hash.AddU64(static_cast<std::uint64_t>(targets.size()));
  hash.AddU64(header.total_requested_columns);
  hash.AddU64(header.total_resource_actions);
  hash.AddU64(header.total_conflict_resources);
  hash.AddU64(header.total_conflict_impact);
  hash.AddU64(header.expanded_resource_visits);
  for (const TargetedRegenerationNet& target : targets) {
    hash.AddU64(target.net.id);
    hash.AddU32(target.net.generation);
    hash.AddU64(target.source_pool_manifest_checksum);
    hash.AddU64(target.source_pool_candidate_count);
    hash.AddU64(target.source_selected_candidate_id.high);
    hash.AddU64(target.source_selected_candidate_id.low);
    hash.AddU64(target.source_selected_candidate_payload_checksum);
    hash.AddU64(target.next_price_candidate_id.high);
    hash.AddU64(target.next_price_candidate_id.low);
    hash.AddU64(target.next_price_candidate_payload_checksum);
    hash.AddU64(target.next_price_selection_score);
    hash.AddU64(target.conflict_resource_count);
    hash.AddU64(target.conflict_impact);
    hash.AddU64(target.negotiated_price_exposure);
    hash.AddU64(target.requested_columns);
    hash.AddU64(static_cast<std::uint64_t>(target.resource_actions.size()));
    for (const RegenerationResourceAction& action : target.resource_actions) {
      AddResourceKey(hash, action.resource);
      hash.AddU64(action.observed_overuse_units);
      hash.AddU64(action.selected_candidate_usage_units);
      hash.AddU64(action.negotiated_price);
      hash.AddU64(action.conflict_impact);
    }
  }
  return hash.Finish();
}

bool internal::TargetedRegenerationTargetRanksBeforeV1(
    const TargetedRegenerationNet& left, const TargetedRegenerationNet& right) noexcept {
  return TargetRanksBefore(left, right);
}

internal::TargetedRegenerationPolicyEntryProjectionV1
internal::ProjectTargetedRegenerationPolicyEntriesV1(
    const geometry_compiler::CompiledBoard& compiled_board,
    std::span<const NegotiatedResourcePrice> prices,
    const TargetedRegenerationNet& target) noexcept {
  TargetedRegenerationPolicyEntryProjectionV1 projection;
  for (const NegotiatedResourcePrice& price : prices) {
    if (routing::ResourceExists(compiled_board, price.resource)) {
      ++projection.target_legal_price_count;
    }
  }
  projection.aggregate_entry_count = projection.target_legal_price_count * target.requested_columns;
  for (std::uint64_t column = 1; column < target.requested_columns; ++column) {
    const routing::EdgeResourceKey& action =
        target.resource_actions[static_cast<std::size_t>(column - 1U)].resource;
    const auto price =
        std::ranges::lower_bound(prices, action, {}, &NegotiatedResourcePrice::resource);
    if (price == prices.end() || price->resource != action) {
      ++projection.aggregate_entry_count;
    }
  }
  return projection;
}

bool internal::TargetedRegenerationPolicyEntriesFitV1(std::uint64_t projected_entries,
                                                      std::uint64_t maximum_entries) noexcept {
  return projected_entries <= maximum_entries;
}

TargetedRegenerationPolicyResult BuildTargetedRegenerationPoliciesV1(
    const PreparedNetRoutingContext& context, const NegotiatedPriceState& next_price_state,
    std::uint64_t intrinsic_cost_weight, const TargetedRegenerationNet& target,
    std::uint64_t deterministic_seed, std::uint32_t first_candidate_ordinal) {
  if (next_price_state.schema_version() != kNegotiatedPriceStateSchemaVersion ||
      next_price_state.associations().board_content_hash !=
          context.compiled_board.source_board_content_hash() ||
      next_price_state.associations().compiler_profile_fingerprint !=
          context.compiled_board.compiler_profile_fingerprint() ||
      next_price_state.associations().geometry_compiler_version !=
          context.compiled_board.compiler_version() ||
      target.net != context.request.net || target.net != context.routing_profile.net ||
      context.routing_profile_fingerprint !=
          routing::FingerprintRoutingProfile(context.routing_profile) ||
      context.routing_profile_fingerprint !=
          routing::FingerprintRoutingProfile(context.compiled_board.routing_profile())) {
    return Error(TargetedRegenerationErrorCode::kInvalidPricingInput,
                 "allocator.targeted_regeneration.policy_association.v1",
                 "Policy synthesis input does not match the prepared net and next price state");
  }
  if (intrinsic_cost_weight == 0 || target.requested_columns == 0 ||
      target.requested_columns > kMaximumAllocatorCandidatesV1 ||
      target.resource_actions.size() > kMaximumAllocatorResourceRecordsV1 ||
      target.requested_columns > static_cast<UWide>(target.resource_actions.size()) + 1U) {
    return Error(
        TargetedRegenerationErrorCode::kInvalidConfiguration,
        "allocator.targeted_regeneration.policy_configuration.v1",
        "Policy synthesis requires positive bounded columns and one action per forced column");
  }
  if (static_cast<UWide>(first_candidate_ordinal) + target.requested_columns - 1U >
      std::numeric_limits<std::uint32_t>::max()) {
    return Error(TargetedRegenerationErrorCode::kArithmeticOverflow,
                 "allocator.targeted_regeneration.policy_ordinal.v1",
                 "Targeted-regeneration candidate ordinals overflow uint32");
  }

  return WithFailureEnvelope([&]() -> TargetedRegenerationPolicyResult {
    for (std::size_t index = 0; index < target.resource_actions.size(); ++index) {
      const RegenerationResourceAction& action = target.resource_actions[index];
      if (!routing::ResourceExists(context.compiled_board, action.resource)) {
        return Error(TargetedRegenerationErrorCode::kInvalidPricingInput,
                     "allocator.targeted_regeneration.policy_action_resource.v1",
                     "A targeted-regeneration action resource is absent from the prepared net");
      }
      if (index != 0) {
        const RegenerationResourceAction& previous = target.resource_actions[index - 1U];
        if (previous.resource == action.resource || ActionRanksBefore(action, previous)) {
          return Error(TargetedRegenerationErrorCode::kInvalidPricingInput,
                       "allocator.targeted_regeneration.policy_action_order.v1",
                       "Targeted-regeneration actions are duplicate or outside stable order");
        }
      }
    }

    const UWide weight_increment = intrinsic_cost_weight - 1U;
    const geometry_compiler::DeterministicCosts costs = context.compiled_board.profile().costs;
    const UWide orthogonal_surcharge = weight_increment * costs.orthogonal_step;
    const UWide diagonal_surcharge = weight_increment * costs.diagonal_step;
    const UWide bend_surcharge = weight_increment * costs.bend;
    if (orthogonal_surcharge > std::numeric_limits<std::uint64_t>::max() ||
        diagonal_surcharge > std::numeric_limits<std::uint64_t>::max() ||
        bend_surcharge > std::numeric_limits<std::uint64_t>::max()) {
      return Error(TargetedRegenerationErrorCode::kArithmeticOverflow,
                   "allocator.targeted_regeneration.policy_weight.v1",
                   "Intrinsic-cost weighting overflows candidate-policy surcharges");
    }

    std::optional<routing::EdgeResourceKey> previous_price_resource;
    for (const NegotiatedResourcePrice& price : next_price_state.prices()) {
      const UWide raw_total = static_cast<UWide>(price.present_price) + price.history_price;
      const std::uint64_t expected_total = static_cast<std::uint64_t>(
          std::min<UWide>(raw_total, next_price_state.config().maximum_price_per_resource));
      if (price.total_price == 0 ||
          price.present_price > next_price_state.config().maximum_price_per_resource ||
          price.history_price > next_price_state.config().maximum_price_per_resource ||
          price.total_price != expected_total ||
          price.total_clamped !=
              (raw_total > next_price_state.config().maximum_price_per_resource) ||
          (previous_price_resource.has_value() &&
           !(previous_price_resource.value() < price.resource))) {
        return Error(
            TargetedRegenerationErrorCode::kInvalidPricingInput,
            "allocator.targeted_regeneration.policy_price_resource.v1",
            "Next negotiated prices are noncanonical, duplicate, or internally inconsistent");
      }
      previous_price_resource = price.resource;
    }

    const internal::TargetedRegenerationPolicyEntryProjectionV1 entry_projection =
        internal::ProjectTargetedRegenerationPolicyEntriesV1(context.compiled_board,
                                                             next_price_state.prices(), target);
    if (!internal::TargetedRegenerationPolicyEntriesFitV1(entry_projection.aggregate_entry_count,
                                                          routing::kMaximumPolicyResourceEntries)) {
      return Error(TargetedRegenerationErrorCode::kWorkBoundExceeded,
                   "allocator.targeted_regeneration.policy_resources.v1",
                   "Targeted-regeneration policy batch exceeds its aggregate resource-entry bound");
    }

    routing::CandidateGenerationPolicy base_policy;
    base_policy.objective = routing::CandidateObjective::kResourceDiverse;
    base_policy.deterministic_seed = deterministic_seed;
    base_policy.candidate_ordinal = first_candidate_ordinal;
    base_policy.orthogonal_step_surcharge = static_cast<std::uint64_t>(orthogonal_surcharge);
    base_policy.diagonal_step_surcharge = static_cast<std::uint64_t>(diagonal_surcharge);
    base_policy.bend_surcharge = static_cast<std::uint64_t>(bend_surcharge);
    base_policy.resource_penalties.reserve(
        static_cast<std::size_t>(entry_projection.target_legal_price_count));
    for (const NegotiatedResourcePrice& price : next_price_state.prices()) {
      if (!routing::ResourceExists(context.compiled_board, price.resource)) {
        // Prices are global across compatible net/rule views. An edge absent
        // from this target's compiled view contributes zero to every route for
        // that target and therefore is not part of its complete legal
        // projection.
        continue;
      }
      base_policy.resource_penalties.push_back(routing::ResourcePenalty{
          .resource = price.resource,
          .additional_cost = price.total_price,
      });
    }

    std::vector<routing::NormalizedCandidateGenerationPolicy> policies;
    policies.reserve(static_cast<std::size_t>(target.requested_columns));
    for (std::uint64_t column = 0; column < target.requested_columns; ++column) {
      routing::CandidateGenerationPolicy policy = base_policy;
      policy.candidate_ordinal =
          static_cast<std::uint32_t>(static_cast<std::uint64_t>(first_candidate_ordinal) + column);
      if (column != 0) {
        const routing::EdgeResourceKey& banned =
            target.resource_actions[static_cast<std::size_t>(column - 1U)].resource;
        const auto penalty = std::ranges::lower_bound(policy.resource_penalties, banned, {},
                                                      &routing::ResourcePenalty::resource);
        if (penalty != policy.resource_penalties.end() && penalty->resource == banned) {
          // A ban makes the priced edge absent. Removing its otherwise-complete
          // price record satisfies the policy contract forbidding one resource
          // from being both banned and penalized.
          policy.resource_penalties.erase(penalty);
        }
        policy.banned_resources.push_back(banned);
      }
      routing::CandidatePolicyResult normalized =
          routing::NormalizeCandidateGenerationPolicy(context.compiled_board, policy);
      if (const auto* failure = std::get_if<routing::CandidatePolicyError>(&normalized);
          failure != nullptr) {
        return TranslatePolicyFailure(*failure);
      }
      policies.push_back(
          std::get<routing::NormalizedCandidateGenerationPolicy>(std::move(normalized)));
    }
    return policies;
  });
}

internal::TargetedRegenerationResourceScanResultV1 internal::ScanTargetedRegenerationResourcesV1(
    std::span<const candidates::PhysicalEdgeSpan> spans,
    std::span<const ResourceUsage> world_resources, std::span<const NegotiatedResourcePrice> prices,
    std::uint64_t maximum_retained_actions) {
  std::priority_queue<RegenerationResourceAction, std::vector<RegenerationResourceAction>,
                      ActionBetter>
      best_actions;
  UWide conflict_count = 0;
  UWide conflict_impact = 0;
  UWide price_exposure = 0;
  for (const candidates::PhysicalEdgeSpan& span : spans) {
    for (std::uint32_t offset = 0; offset < span.edge_count; ++offset) {
      const routing::EdgeResourceKey resource = ResourceAt(span, offset);
      const auto usage =
          std::ranges::lower_bound(world_resources, resource, {}, &ResourceUsage::resource);
      const bool has_world_usage = usage != world_resources.end() && usage->resource == resource;
      const std::uint64_t observed_overuse = has_world_usage ? usage->overuse_units : 0;

      const auto price =
          std::ranges::lower_bound(prices, resource, {}, &NegotiatedResourcePrice::resource);
      const bool has_price = price != prices.end() && price->resource == resource;
      const std::uint64_t negotiated_price = has_price ? price->total_price : 0;
      price_exposure += static_cast<UWide>(negotiated_price) * span.usage_units;
      if (price_exposure > std::numeric_limits<std::uint64_t>::max()) {
        return Error(TargetedRegenerationErrorCode::kArithmeticOverflow,
                     "allocator.targeted_regeneration.metric_overflow.v1",
                     "Targeted-regeneration conflict metrics exceed unsigned 64-bit fields");
      }
      if (observed_overuse == 0) {
        continue;
      }
      if (!has_price) {
        return Error(TargetedRegenerationErrorCode::kInternalInvariant,
                     "allocator.targeted_regeneration.price_resource_roster.v1",
                     "Updated price state omitted an overused selected resource");
      }

      const UWide action_impact = static_cast<UWide>(observed_overuse) * span.usage_units;
      ++conflict_count;
      conflict_impact += action_impact;
      if (action_impact > std::numeric_limits<std::uint64_t>::max() ||
          conflict_count > std::numeric_limits<std::uint64_t>::max() ||
          conflict_impact > std::numeric_limits<std::uint64_t>::max()) {
        return Error(TargetedRegenerationErrorCode::kArithmeticOverflow,
                     "allocator.targeted_regeneration.metric_overflow.v1",
                     "Targeted-regeneration conflict metrics exceed unsigned 64-bit fields");
      }
      if (maximum_retained_actions == 0) {
        continue;
      }
      RegenerationResourceAction action{
          .resource = resource,
          .observed_overuse_units = observed_overuse,
          .selected_candidate_usage_units = span.usage_units,
          .negotiated_price = negotiated_price,
          .conflict_impact = static_cast<std::uint64_t>(action_impact),
      };
      if (best_actions.size() < maximum_retained_actions) {
        best_actions.push(action);
      } else if (ActionRanksBefore(action, best_actions.top())) {
        best_actions.pop();
        best_actions.push(action);
      }
    }
  }

  std::vector<RegenerationResourceAction> actions;
  actions.reserve(best_actions.size());
  while (!best_actions.empty()) {
    actions.push_back(best_actions.top());
    best_actions.pop();
  }
  std::ranges::sort(actions, ActionRanksBefore);
  return TargetedRegenerationResourceScanV1{
      .conflict_resource_count = static_cast<std::uint64_t>(conflict_count),
      .conflict_impact = static_cast<std::uint64_t>(conflict_impact),
      .negotiated_price_exposure = static_cast<std::uint64_t>(price_exposure),
      .resource_actions = std::move(actions),
  };
}

template <bool CaptureOperationalProfile>
TargetedRegenerationPlanResult BuildTargetedRegenerationPlanImpl(
    std::uint32_t schema_version, const NegotiatedPriceState& previous_price_state,
    const OneWorldAllocationRequest& source_request, const OneWorldAllocation& world,
    candidates::CandidateStore& candidate_store, const TargetedRegenerationConfig& config,
    TargetedRegenerationPlanningOperationalProfileV1* operational_profile) {
  ::apgar::internal::OperationalTimestamp<CaptureOperationalProfile, OperationalClock>
      component_start;
  if constexpr (CaptureOperationalProfile) {
    *operational_profile = {};
    component_start =
        ::apgar::internal::OperationalNow<CaptureOperationalProfile, OperationalClock>();
  }
  if (schema_version != kTargetedRegenerationPlanSchemaVersion) {
    return Error(TargetedRegenerationErrorCode::kUnsupportedSchema,
                 "allocator.targeted_regeneration.schema.v1",
                 "Targeted-regeneration plan schema is unsupported");
  }
  if (!ConfigIsValid(config)) {
    return Error(TargetedRegenerationErrorCode::kInvalidConfiguration,
                 "allocator.targeted_regeneration.configuration.v1",
                 "Targeted-regeneration configuration is outside schema-v1 bounds");
  }

  return WithFailureEnvelope([&]() -> TargetedRegenerationPlanResult {
    ::apgar::internal::OperationalTimestamp<CaptureOperationalProfile, OperationalClock>
        source_selection_start;
    if constexpr (CaptureOperationalProfile) {
      source_selection_start =
          ::apgar::internal::OperationalNow<CaptureOperationalProfile, OperationalClock>();
    }
    internal::OneWorldSelectionEvidenceResult source_selection_result =
        internal::SelectOneWorldWithoutAccounting(source_request);
    if (const auto* failure = std::get_if<AllocationError>(&source_selection_result);
        failure != nullptr) {
      return TranslateSelectionFailure(
          *failure, "allocator.targeted_regeneration.source_selection.v1",
          "The source request cannot reproduce its canonical pool selections");
    }
    internal::OneWorldSelectionEvidence source_selection =
        std::get<internal::OneWorldSelectionEvidence>(std::move(source_selection_result));
    if (source_selection.selected_expanded_resource_uses >
        config.maximum_expanded_resource_visits) {
      return Error(TargetedRegenerationErrorCode::kWorkBoundExceeded,
                   "allocator.targeted_regeneration.expanded_resource_budget.v1",
                   "Source selected footprints exceed the configured planning budget");
    }
    if (!SelectionProjectionMatches(world, source_selection)) {
      return Error(TargetedRegenerationErrorCode::kInvalidPricingInput,
                   "allocator.targeted_regeneration.source_selection_projection.v1",
                   "The source world selection projection differs from the complete request");
    }
    if (source_selection.source_candidate_count > source_request.limits.maximum_candidates) {
      return Error(TargetedRegenerationErrorCode::kInternalInvariant,
                   "allocator.targeted_regeneration.candidate_headroom.v1",
                   "Validated source candidates exceed the source request candidate bound");
    }
    const std::uint64_t candidate_headroom =
        source_request.limits.maximum_candidates - source_selection.source_candidate_count;

    ::apgar::internal::OperationalTimestamp<CaptureOperationalProfile, OperationalClock>
        price_update_start;
    if constexpr (CaptureOperationalProfile) {
      operational_profile->source_selection_and_resource_accumulation_wall_nanoseconds =
          OperationalElapsed(source_selection_start);
      price_update_start =
          ::apgar::internal::OperationalNow<CaptureOperationalProfile, OperationalClock>();
    }
    NegotiatedPriceStateResult update =
        UpdateNegotiatedPrices(previous_price_state, source_request, world);
    if (const auto* failure = std::get_if<NegotiatedPriceError>(&update); failure != nullptr) {
      return Error(failure->code == NegotiatedPriceErrorCode::kResourceExhausted
                       ? TargetedRegenerationErrorCode::kResourceExhausted
                       : TargetedRegenerationErrorCode::kInvalidPricingInput,
                   failure->invariant_id, failure->detail);
    }
    NegotiatedPriceState price_state = std::get<NegotiatedPriceState>(std::move(update));

    ::apgar::internal::OperationalTimestamp<CaptureOperationalProfile, OperationalClock>
        next_selection_start;
    if constexpr (CaptureOperationalProfile) {
      operational_profile->price_update_wall_nanoseconds = OperationalElapsed(price_update_start);
      operational_profile->price_update_operations = 1;
      next_selection_start =
          ::apgar::internal::OperationalNow<CaptureOperationalProfile, OperationalClock>();
    }
    NegotiatedPriceSnapshotResult next_snapshot_result =
        BuildPriceSnapshotForState(source_request.capacities, price_state);
    if (const auto* failure = std::get_if<NegotiatedPriceError>(&next_snapshot_result);
        failure != nullptr) {
      return Error(failure->code == NegotiatedPriceErrorCode::kResourceExhausted
                       ? TargetedRegenerationErrorCode::kResourceExhausted
                       : TargetedRegenerationErrorCode::kInvalidPricingInput,
                   failure->invariant_id, failure->detail);
    }
    OneWorldAllocationRequest next_request = source_request;
    next_request.prices = std::get<PriceSnapshot>(std::move(next_snapshot_result));
    internal::OneWorldSelectionEvidenceResult next_selection_result =
        internal::SelectOneWorldWithoutAccounting(next_request);
    if (const auto* failure = std::get_if<AllocationError>(&next_selection_result);
        failure != nullptr) {
      return TranslateSelectionFailure(
          *failure, "allocator.targeted_regeneration.next_price_selection.v1",
          "The complete candidate pools cannot be selected under the next price snapshot");
    }
    internal::OneWorldSelectionEvidence next_selection =
        std::get<internal::OneWorldSelectionEvidence>(std::move(next_selection_result));
    if (next_selection.candidate_pool_manifest_checksum !=
            source_selection.candidate_pool_manifest_checksum ||
        next_selection.source_pool_count != source_selection.source_pool_count ||
        next_selection.source_candidate_count != source_selection.source_candidate_count) {
      return Error(TargetedRegenerationErrorCode::kInternalInvariant,
                   "allocator.targeted_regeneration.pool_manifest_replay.v1",
                   "Candidate-pool identity changed while applying the next price snapshot");
    }
    UWide expanded_visits = static_cast<UWide>(source_selection.selected_expanded_resource_uses) +
                            next_selection.selected_expanded_resource_uses;
    if (expanded_visits > config.maximum_expanded_resource_visits) {
      return Error(TargetedRegenerationErrorCode::kWorkBoundExceeded,
                   "allocator.targeted_regeneration.expanded_resource_budget.v1",
                   "Source and next-price selected footprints exceed the planning budget");
    }

    ::apgar::internal::OperationalTimestamp<CaptureOperationalProfile, OperationalClock>
        assembly_start;
    if constexpr (CaptureOperationalProfile) {
      operational_profile->next_price_selection_and_resource_accumulation_wall_nanoseconds =
          OperationalElapsed(next_selection_start);
      assembly_start =
          ::apgar::internal::OperationalNow<CaptureOperationalProfile, OperationalClock>();
    }
    const std::uint64_t maximum_retained_targets =
        std::min({config.maximum_target_nets, config.maximum_total_columns,
                  config.maximum_total_resource_actions, candidate_headroom});
    std::priority_queue<TargetedRegenerationNet, std::vector<TargetedRegenerationNet>, TargetBetter>
        best_targets;
    for (std::size_t index = 0; index < next_selection.pools.size(); ++index) {
      const internal::OneWorldPoolSelectionEvidence& next_pool = next_selection.pools[index];
      const internal::OneWorldPoolSelectionEvidence& source_pool = source_selection.pools[index];
      const NetSelection& selection = next_pool.selection;
      if (selection.candidate == nullptr || source_pool.net != next_pool.net) {
        continue;
      }
      internal::TargetedRegenerationResourceScanResultV1 scan_result =
          internal::ScanTargetedRegenerationResourcesV1(selection.candidate->data().resources,
                                                        world.resources, price_state.prices(), 0);
      if (const auto* failure = std::get_if<TargetedRegenerationError>(&scan_result);
          failure != nullptr) {
        return *failure;
      }
      const internal::TargetedRegenerationResourceScanV1& scan =
          std::get<internal::TargetedRegenerationResourceScanV1>(scan_result);
      if (scan.conflict_resource_count == 0) {
        continue;
      }
      TargetedRegenerationNet target{
          .net = selection.net,
          .source_pool_manifest_checksum = source_pool.pool_manifest_checksum,
          .source_pool_candidate_count = source_pool.candidate_count,
          .source_selected_candidate_id = *source_pool.selection.candidate_id,
          .source_selected_candidate_payload_checksum =
              *source_pool.selection.candidate_payload_checksum,
          .next_price_candidate_id = *selection.candidate_id,
          .next_price_candidate_payload_checksum = *selection.candidate_payload_checksum,
          .next_price_selection_score = selection.selection_score,
          .conflict_resource_count = scan.conflict_resource_count,
          .conflict_impact = scan.conflict_impact,
          .negotiated_price_exposure = scan.negotiated_price_exposure,
          .requested_columns = 0,
          .resource_actions = {},
      };
      if (best_targets.size() < maximum_retained_targets) {
        best_targets.push(std::move(target));
      } else if (maximum_retained_targets != 0 && TargetRanksBefore(target, best_targets.top())) {
        best_targets.pop();
        best_targets.push(std::move(target));
      }
    }

    std::vector<TargetedRegenerationNet> targets;
    targets.reserve(best_targets.size());
    while (!best_targets.empty()) {
      targets.push_back(best_targets.top());
      best_targets.pop();
    }
    std::ranges::sort(targets, TargetRanksBefore);
    UWide total_columns = 0;
    UWide total_actions = 0;
    UWide total_conflicts = 0;
    UWide total_impact = 0;
    std::size_t retained_target_count = 0;
    for (TargetedRegenerationNet& target : targets) {
      if (total_columns >= config.maximum_total_columns ||
          total_actions >= config.maximum_total_resource_actions ||
          total_columns >= candidate_headroom) {
        break;
      }
      const std::uint64_t available_actions = static_cast<std::uint64_t>(
          static_cast<UWide>(config.maximum_total_resource_actions) - total_actions);
      const std::uint64_t maximum_actions =
          std::min(config.maximum_resource_actions_per_net, available_actions);
      const auto selected_pool =
          std::ranges::lower_bound(next_selection.pools, target.net, NetRanksBefore,
                                   &internal::OneWorldPoolSelectionEvidence::net);
      if (selected_pool == next_selection.pools.end() || selected_pool->net != target.net ||
          selected_pool->selection.candidate == nullptr ||
          selected_pool->selection.candidate->id() != target.next_price_candidate_id) {
        return Error(TargetedRegenerationErrorCode::kInternalInvariant,
                     "allocator.targeted_regeneration.target_selection.v1",
                     "Ranked regeneration target no longer matches its validated selection");
      }
      UWide action_scan_visits = 0;
      for (const candidates::PhysicalEdgeSpan& span :
           selected_pool->selection.candidate->data().resources) {
        action_scan_visits += span.edge_count;
      }
      if (expanded_visits + action_scan_visits > config.maximum_expanded_resource_visits) {
        return Error(TargetedRegenerationErrorCode::kWorkBoundExceeded,
                     "allocator.targeted_regeneration.expanded_resource_budget.v1",
                     "Retained-target action scans exceed the planning visit budget");
      }
      expanded_visits += action_scan_visits;
      internal::TargetedRegenerationResourceScanResultV1 action_scan_result =
          internal::ScanTargetedRegenerationResourcesV1(
              selected_pool->selection.candidate->data().resources, world.resources,
              price_state.prices(), maximum_actions);
      if (const auto* failure = std::get_if<TargetedRegenerationError>(&action_scan_result);
          failure != nullptr) {
        return *failure;
      }
      internal::TargetedRegenerationResourceScanV1 action_scan =
          std::get<internal::TargetedRegenerationResourceScanV1>(std::move(action_scan_result));
      if (action_scan.conflict_resource_count != target.conflict_resource_count ||
          action_scan.conflict_impact != target.conflict_impact ||
          action_scan.negotiated_price_exposure != target.negotiated_price_exposure) {
        return Error(TargetedRegenerationErrorCode::kInternalInvariant,
                     "allocator.targeted_regeneration.target_metric_replay.v1",
                     "Ranked regeneration target metrics changed during action materialization");
      }
      target.resource_actions = std::move(action_scan.resource_actions);
      if (target.resource_actions.empty()) {
        break;
      }
      const std::uint64_t desired_columns =
          std::min({config.maximum_columns_per_net, target.conflict_resource_count,
                    static_cast<std::uint64_t>(target.resource_actions.size()) + 1U});
      const std::uint64_t available_columns = static_cast<std::uint64_t>(
          static_cast<UWide>(config.maximum_total_columns) - total_columns);
      const std::uint64_t available_candidate_headroom =
          static_cast<std::uint64_t>(static_cast<UWide>(candidate_headroom) - total_columns);
      target.requested_columns =
          std::min({desired_columns, available_columns, available_candidate_headroom});
      if (target.requested_columns == 0) {
        break;
      }
      total_columns += target.requested_columns;
      total_actions += target.resource_actions.size();
      total_conflicts += target.conflict_resource_count;
      total_impact += target.conflict_impact;
      if (total_conflicts > std::numeric_limits<std::uint64_t>::max() ||
          total_impact > std::numeric_limits<std::uint64_t>::max()) {
        return Error(TargetedRegenerationErrorCode::kArithmeticOverflow,
                     "allocator.targeted_regeneration.aggregate_overflow.v1",
                     "Targeted-regeneration aggregate metrics exceed replay fields");
      }
      ++retained_target_count;
    }
    targets.resize(retained_target_count);

    std::vector<candidates::CandidatePinRequest> pin_requests;
    pin_requests.reserve(source_selection.selection_projection.selected_net_count +
                         next_selection.selection_projection.selected_net_count);
    for (const NetSelection& selection : source_selection.selection_projection.selections) {
      if (selection.candidate != nullptr) {
        pin_requests.push_back(candidates::CandidatePinRequest{
            .net = selection.net,
            .candidate_id = *selection.candidate_id,
            .candidate_payload_checksum = *selection.candidate_payload_checksum,
            .expected_candidate = selection.candidate,
        });
      }
    }
    for (const NetSelection& selection : next_selection.selection_projection.selections) {
      if (selection.candidate != nullptr) {
        pin_requests.push_back(candidates::CandidatePinRequest{
            .net = selection.net,
            .candidate_id = *selection.candidate_id,
            .candidate_payload_checksum = *selection.candidate_payload_checksum,
            .expected_candidate = selection.candidate,
        });
      }
    }
    std::ranges::sort(pin_requests, [](const candidates::CandidatePinRequest& left,
                                       const candidates::CandidatePinRequest& right) {
      return left.candidate_id < right.candidate_id;
    });
    const auto unique_end =
        std::ranges::unique(pin_requests, {}, &candidates::CandidatePinRequest::candidate_id);
    pin_requests.erase(unique_end.begin(), unique_end.end());
    std::optional<candidates::CandidateStorePinLease> pin_lease;
    candidates::CandidateStorePinLeaseResult lease_result =
        pin_requests.empty() ? candidate_store.AcquireEmptyPinLease()
                             : candidate_store.AcquirePinLease(pin_requests);
    if (const auto* failure = std::get_if<candidates::CandidateStoreError>(&lease_result);
        failure != nullptr) {
      return Error(failure->code == candidates::CandidateStoreErrorCode::kResourceExhausted
                       ? TargetedRegenerationErrorCode::kResourceExhausted
                       : TargetedRegenerationErrorCode::kCandidateStoreLease,
                   "allocator.targeted_regeneration.candidate_store_lease.v1",
                   "CandidateStore could not issue the plan's exact identity/retention lease");
    }
    pin_lease.emplace(std::get<candidates::CandidateStorePinLease>(std::move(lease_result)));

    const internal::TargetedRegenerationChecksumHeaderV1 checksum_header{
        .schema_version = schema_version,
        .associations = price_state.associations(),
        .workload_checksum = price_state.workload_checksum(),
        .source_request_manifest_checksum = source_selection.request_manifest_checksum,
        .candidate_pool_manifest_checksum = source_selection.candidate_pool_manifest_checksum,
        .source_pool_count = source_selection.source_pool_count,
        .source_candidate_count = source_selection.source_candidate_count,
        .pinned_candidate_count = static_cast<std::uint64_t>(pin_requests.size()),
        .config = config,
        .price_state_checksum = price_state.state_checksum(),
        .price_iteration = price_state.iteration(),
        .source_world_checksum = world.world_checksum,
        .total_requested_columns = static_cast<std::uint64_t>(total_columns),
        .total_resource_actions = static_cast<std::uint64_t>(total_actions),
        .total_conflict_resources = static_cast<std::uint64_t>(total_conflicts),
        .total_conflict_impact = static_cast<std::uint64_t>(total_impact),
        .expanded_resource_visits = static_cast<std::uint64_t>(expanded_visits),
    };
    const std::uint64_t checksum =
        internal::ComputeTargetedRegenerationPlanChecksumV1(checksum_header, targets);
    TargetedRegenerationPlan result(
        schema_version, price_state.associations(), price_state.workload_checksum(),
        source_selection.request_manifest_checksum,
        source_selection.candidate_pool_manifest_checksum, source_selection.source_pool_count,
        source_selection.source_candidate_count, static_cast<std::uint64_t>(pin_requests.size()),
        config, std::move(price_state), std::move(targets),
        static_cast<std::uint64_t>(total_columns), static_cast<std::uint64_t>(total_actions),
        static_cast<std::uint64_t>(total_conflicts), static_cast<std::uint64_t>(total_impact),
        static_cast<std::uint64_t>(expanded_visits), checksum, std::move(pin_lease));
    if constexpr (CaptureOperationalProfile) {
      operational_profile->target_ranking_retention_and_assembly_wall_nanoseconds =
          OperationalElapsed(assembly_start);
      operational_profile->component_wall_nanoseconds = OperationalElapsed(component_start);
      const UWide classified =
          static_cast<UWide>(
              operational_profile->source_selection_and_resource_accumulation_wall_nanoseconds) +
          operational_profile->price_update_wall_nanoseconds +
          operational_profile->next_price_selection_and_resource_accumulation_wall_nanoseconds +
          operational_profile->target_ranking_retention_and_assembly_wall_nanoseconds;
      operational_profile->unclassified_serial_wall_nanoseconds =
          classified <= operational_profile->component_wall_nanoseconds
              ? operational_profile->component_wall_nanoseconds -
                    static_cast<std::uint64_t>(classified)
              : 0;
    }
    return result;
  });
}

TargetedRegenerationPlanResult BuildTargetedRegenerationPlan(
    std::uint32_t schema_version, const NegotiatedPriceState& previous_price_state,
    const OneWorldAllocationRequest& source_request, const OneWorldAllocation& world,
    candidates::CandidateStore& candidate_store, const TargetedRegenerationConfig& config) {
  return BuildTargetedRegenerationPlanImpl<false>(schema_version, previous_price_state,
                                                  source_request, world, candidate_store, config,
                                                  nullptr);
}

TargetedRegenerationPlanResult BuildTargetedRegenerationPlanWithOperationalProfileV1(
    std::uint32_t schema_version, const NegotiatedPriceState& previous_price_state,
    const OneWorldAllocationRequest& source_request, const OneWorldAllocation& world,
    candidates::CandidateStore& candidate_store, const TargetedRegenerationConfig& config,
    TargetedRegenerationPlanningOperationalProfileV1& operational_profile) {
  return BuildTargetedRegenerationPlanImpl<true>(schema_version, previous_price_state,
                                                 source_request, world, candidate_store, config,
                                                 &operational_profile);
}

}  // namespace apgar::allocator
