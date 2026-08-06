#include "apgar/allocator/negotiated_regeneration_plan.h"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <map>
#include <new>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

#include "apgar/board_ir/stable_hash.h"
#include "apgar/candidates/route_candidate.h"

namespace apgar::allocator {
namespace {

using Wide = __int128_t;
using UWide = __uint128_t;

[[nodiscard]] auto NetKey(board_ir::EntityRef net) noexcept {
  return std::pair{net.id, net.generation};
}

[[nodiscard]] NegotiatedRegenerationPlanError Error(NegotiatedRegenerationPlanErrorCode code,
                                                    std::string_view invariant_id,
                                                    std::string_view detail) noexcept {
  return NegotiatedRegenerationPlanError{
      .code = code,
      .invariant_id = invariant_id,
      .detail = detail,
      .net = std::nullopt,
      .resource = std::nullopt,
      .epoch_index = std::nullopt,
      .expected_value = std::nullopt,
      .actual_value = std::nullopt,
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

[[nodiscard]] bool IsCanonicalDirection(geometry_compiler::Direction direction) noexcept {
  switch (direction) {
    case geometry_compiler::Direction::kEast:
    case geometry_compiler::Direction::kNorthEast:
    case geometry_compiler::Direction::kNorth:
    case geometry_compiler::Direction::kNorthWest:
      return true;
    case geometry_compiler::Direction::kWest:
    case geometry_compiler::Direction::kSouthWest:
    case geometry_compiler::Direction::kSouth:
    case geometry_compiler::Direction::kSouthEast:
      return false;
  }
  return false;
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

[[nodiscard]] std::uint64_t PricePolicyIdentity(
    const NegotiatedPriceUpdatePolicyV1& policy) noexcept {
  board_ir::StableHashBuilder hash;
  hash.AddString("APGAR-P4R06-NEGOTIATED-PRICE-POLICY-V1");
  hash.AddU64(policy.initial_present_factor);
  hash.AddU64(policy.present_factor_increment);
  hash.AddU64(policy.historical_price_increment);
  return NonzeroHash(&hash);
}

[[nodiscard]] std::uint64_t SnapshotIdentity(const NegotiatedPriceSnapshot& snapshot) noexcept {
  board_ir::StableHashBuilder hash;
  hash.AddString("APGAR-P4R06-NEGOTIATED-PRICE-SNAPSHOT-V1");
  hash.AddU64(snapshot.associations.board_content_hash);
  hash.AddU64(snapshot.associations.compiler_profile_fingerprint);
  hash.AddU32(snapshot.associations.geometry_compiler_version);
  hash.AddU32(snapshot.capacity_units);
  hash.AddU64(snapshot.policy_identity);
  hash.AddU64(snapshot.prior_snapshot_identity);
  hash.AddU64(snapshot.epoch_index);
  hash.AddU64(snapshot.present_factor);
  hash.AddU64(static_cast<std::uint64_t>(snapshot.prices.size()));
  for (const NegotiatedResourcePrice& price : snapshot.prices) {
    HashResource(&hash, price.resource);
    hash.AddU64(price.present_price);
    hash.AddU64(price.historical_price);
    hash.AddU64(price.total_price);
  }
  return NonzeroHash(&hash);
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

[[nodiscard]] bool ConfigurationIsValid(const NegotiatedRegenerationPlanConfig& config) noexcept {
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

[[nodiscard]] NegotiatedRegenerationPlanError SelectionFailure(
    const OneWorldSelectionError& selection_error) noexcept {
  NegotiatedRegenerationPlanError error =
      Error(NegotiatedRegenerationPlanErrorCode::kSelectionFailure, selection_error.invariant_id,
            selection_error.detail);
  error.selection_error_code = selection_error.code;
  error.accounting_error_code = selection_error.accounting_error_code;
  switch (selection_error.code) {
    case OneWorldSelectionErrorCode::kAssociationMismatch:
    case OneWorldSelectionErrorCode::kCandidateAssociationMismatch:
      error.code = NegotiatedRegenerationPlanErrorCode::kAssociationMismatch;
      break;
    case OneWorldSelectionErrorCode::kInvalidLimits:
      error.code = NegotiatedRegenerationPlanErrorCode::kInvalidConfiguration;
      break;
    case OneWorldSelectionErrorCode::kInputBoundExceeded:
      error.code = NegotiatedRegenerationPlanErrorCode::kBoundExhausted;
      break;
    case OneWorldSelectionErrorCode::kResourceExhausted:
      error.code = NegotiatedRegenerationPlanErrorCode::kResourceExhausted;
      break;
    case OneWorldSelectionErrorCode::kAccountingFailure:
      if (selection_error.accounting_error_code ==
          ResourceAccountingErrorCode::kInputBoundExceeded) {
        error.code = NegotiatedRegenerationPlanErrorCode::kBoundExhausted;
      } else if (selection_error.accounting_error_code ==
                 ResourceAccountingErrorCode::kUsageOverflow) {
        error.code = NegotiatedRegenerationPlanErrorCode::kArithmeticOverflow;
      } else if (selection_error.accounting_error_code ==
                 ResourceAccountingErrorCode::kResourceExhausted) {
        error.code = NegotiatedRegenerationPlanErrorCode::kResourceExhausted;
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

[[nodiscard]] std::optional<NegotiatedRegenerationPlanError> ValidatePriorSnapshot(
    const NegotiatedPriceSnapshot& prior, const ResourceCapacityModel& capacities,
    const NegotiatedRegenerationPlanConfig& config, std::uint64_t policy_identity) {
  const NegotiatedRegenerationPlanLimits& limits = config.limits;
  if (prior.associations != capacities.associations() ||
      prior.capacity_units != capacities.capacity_units()) {
    return Error(NegotiatedRegenerationPlanErrorCode::kAssociationMismatch,
                 "allocator.negotiated_plan.prior_association.v1",
                 "Prior allocator prices do not match the resource-capacity model");
  }
  if (prior.policy_identity != policy_identity) {
    return Error(NegotiatedRegenerationPlanErrorCode::kInvalidInput,
                 "allocator.negotiated_plan.prior_policy.v1",
                 "Prior allocator prices were produced by a different price schedule");
  }
  if ((prior.epoch_index == 0 && prior.prior_snapshot_identity != 0) ||
      (prior.epoch_index != 0 && prior.prior_snapshot_identity == 0)) {
    return Error(NegotiatedRegenerationPlanErrorCode::kInvalidInput,
                 "allocator.negotiated_plan.prior_chain.v1",
                 "Prior allocator prices have an invalid snapshot-chain identity");
  }
  const std::optional<std::uint64_t> prior_steps =
      CheckedMultiply(prior.epoch_index, config.price_policy.present_factor_increment);
  std::uint64_t expected_factor = config.price_policy.initial_present_factor;
  if (!prior_steps.has_value() || !CheckedAdd(*prior_steps, &expected_factor)) {
    return Error(NegotiatedRegenerationPlanErrorCode::kArithmeticOverflow,
                 "allocator.negotiated_plan.prior_schedule_overflow.v1",
                 "Prior epoch cannot be represented by the configured present-price schedule");
  }
  if (prior.present_factor != expected_factor) {
    return Error(NegotiatedRegenerationPlanErrorCode::kInvalidInput,
                 "allocator.negotiated_plan.prior_present_factor.v1",
                 "Prior snapshot present factor does not match its canonical epoch");
  }
  if (prior.prices.size() > limits.maximum_price_entries) {
    NegotiatedRegenerationPlanError error =
        Error(NegotiatedRegenerationPlanErrorCode::kBoundExhausted,
              "allocator.negotiated_plan.prior_price_count.v1",
              "Prior allocator-price entries exceed the configured bound");
    error.expected_value = limits.maximum_price_entries;
    error.actual_value = prior.prices.size();
    return error;
  }

  const std::uint64_t maximum_selected_candidates =
      std::min({limits.selection.maximum_net_pools, limits.selection.maximum_total_candidates,
                limits.selection.accounting.maximum_candidates});
  const std::uint64_t maximum_overuse_per_epoch =
      maximum_selected_candidates > prior.capacity_units
          ? maximum_selected_candidates - prior.capacity_units
          : 0;
  std::uint64_t aggregate_price = 0;
  const routing::EdgeResourceKey* previous_resource = nullptr;
  for (const NegotiatedResourcePrice& price : prior.prices) {
    if (!IsCanonicalDirection(price.resource.direction) ||
        (previous_resource != nullptr && !(*previous_resource < price.resource)) ||
        (price.present_price == 0 && price.historical_price == 0)) {
      return Error(NegotiatedRegenerationPlanErrorCode::kInvalidInput,
                   "allocator.negotiated_plan.prior_price_order.v1",
                   "Prior allocator prices are not a strict canonical nonzero resource map");
    }
    previous_resource = &price.resource;
    std::uint64_t total = price.present_price;
    if (!CheckedAdd(price.historical_price, &total)) {
      return Error(NegotiatedRegenerationPlanErrorCode::kArithmeticOverflow,
                   "allocator.negotiated_plan.prior_price_sum_overflow.v1",
                   "Prior present and historical price sum overflowed uint64");
    }
    if (total != price.total_price) {
      return Error(NegotiatedRegenerationPlanErrorCode::kInvalidInput,
                   "allocator.negotiated_plan.prior_price_total.v1",
                   "Prior stored total price does not match present plus historical price");
    }
    if (price.historical_price % config.price_policy.historical_price_increment != 0) {
      NegotiatedRegenerationPlanError error =
          Error(NegotiatedRegenerationPlanErrorCode::kInvalidInput,
                "allocator.negotiated_plan.prior_history_schedule.v1",
                "Prior historical price is not an exact schedule increment");
      error.resource = price.resource;
      return error;
    }
    const std::uint64_t cumulative_overuse =
        price.historical_price / config.price_policy.historical_price_increment;
    std::uint64_t current_overuse = cumulative_overuse;
    if (prior.present_factor == 0) {
      if (price.present_price != 0) {
        NegotiatedRegenerationPlanError error =
            Error(NegotiatedRegenerationPlanErrorCode::kInvalidInput,
                  "allocator.negotiated_plan.prior_present_schedule.v1",
                  "Prior present price is unreachable under its zero present factor");
        error.resource = price.resource;
        return error;
      }
    } else {
      if (price.present_price % prior.present_factor != 0) {
        NegotiatedRegenerationPlanError error =
            Error(NegotiatedRegenerationPlanErrorCode::kInvalidInput,
                  "allocator.negotiated_plan.prior_present_schedule.v1",
                  "Prior present price is not an exact multiple of its present factor");
        error.resource = price.resource;
        return error;
      }
      current_overuse = price.present_price / prior.present_factor;
    }
    if (current_overuse > maximum_overuse_per_epoch) {
      NegotiatedRegenerationPlanError error =
          Error(NegotiatedRegenerationPlanErrorCode::kBoundExhausted,
                "allocator.negotiated_plan.prior_current_overuse_bound.v1",
                "Prior price encodes more current overuse than one bounded selection can produce");
      error.resource = price.resource;
      error.expected_value = maximum_overuse_per_epoch;
      error.actual_value = current_overuse;
      return error;
    }
    if (cumulative_overuse < current_overuse) {
      NegotiatedRegenerationPlanError error =
          Error(NegotiatedRegenerationPlanErrorCode::kInvalidInput,
                "allocator.negotiated_plan.prior_history_contribution.v1",
                "Prior historical price cannot contain its current-epoch contribution");
      error.resource = price.resource;
      return error;
    }
    const std::optional<std::uint64_t> maximum_prior_overuse =
        CheckedMultiply(prior.epoch_index, maximum_overuse_per_epoch);
    if (!maximum_prior_overuse.has_value()) {
      return Error(NegotiatedRegenerationPlanErrorCode::kArithmeticOverflow,
                   "allocator.negotiated_plan.prior_history_bound_overflow.v1",
                   "Prior cumulative-overuse reachability bound overflowed uint64");
    }
    const std::uint64_t prior_overuse = cumulative_overuse - current_overuse;
    if (prior_overuse > *maximum_prior_overuse) {
      NegotiatedRegenerationPlanError error =
          Error(NegotiatedRegenerationPlanErrorCode::kBoundExhausted,
                "allocator.negotiated_plan.prior_cumulative_overuse_bound.v1",
                "Prior history encodes more overuse than preceding bounded epochs can produce");
      error.resource = price.resource;
      error.expected_value = *maximum_prior_overuse;
      error.actual_value = prior_overuse;
      return error;
    }
    if (price.present_price > limits.maximum_price_value ||
        price.historical_price > limits.maximum_price_value ||
        price.total_price > limits.maximum_price_value) {
      NegotiatedRegenerationPlanError error =
          Error(NegotiatedRegenerationPlanErrorCode::kBoundExhausted,
                "allocator.negotiated_plan.prior_price_bound.v1",
                "A prior allocator price exceeds the configured value bound");
      error.resource = price.resource;
      error.expected_value = limits.maximum_price_value;
      error.actual_value =
          std::max({price.present_price, price.historical_price, price.total_price});
      return error;
    }
    if (!CheckedAdd(price.total_price, &aggregate_price)) {
      return Error(NegotiatedRegenerationPlanErrorCode::kArithmeticOverflow,
                   "allocator.negotiated_plan.prior_aggregate_price_overflow.v1",
                   "Prior aggregate allocator price overflowed uint64");
    }
    if (aggregate_price > limits.maximum_aggregate_price) {
      NegotiatedRegenerationPlanError error =
          Error(NegotiatedRegenerationPlanErrorCode::kBoundExhausted,
                "allocator.negotiated_plan.prior_aggregate_price_bound.v1",
                "Prior aggregate allocator price exceeds the configured bound");
      error.resource = price.resource;
      error.expected_value = limits.maximum_aggregate_price;
      error.actual_value = aggregate_price;
      return error;
    }
  }
  if (prior.snapshot_identity == 0 || prior.snapshot_identity != SnapshotIdentity(prior)) {
    return Error(NegotiatedRegenerationPlanErrorCode::kInvalidInput,
                 "allocator.negotiated_plan.prior_identity.v1",
                 "Prior allocator-price snapshot identity does not match its immutable payload");
  }
  if (prior.epoch_index >= limits.maximum_epoch_index) {
    NegotiatedRegenerationPlanError error =
        Error(NegotiatedRegenerationPlanErrorCode::kBoundExhausted,
              "allocator.negotiated_plan.epoch_bound.v1",
              "The next price epoch would exceed the configured epoch bound");
    error.epoch_index = prior.epoch_index;
    error.expected_value = limits.maximum_epoch_index;
    error.actual_value = prior.epoch_index == std::numeric_limits<std::uint64_t>::max()
                             ? prior.epoch_index
                             : prior.epoch_index + 1U;
    return error;
  }
  return std::nullopt;
}

struct CanonicalPool {
  board_ir::EntityRef net;
  std::vector<const candidates::RouteCandidate*> candidates;
};

[[nodiscard]] std::vector<CanonicalPool> CanonicalPools(
    std::span<const OneWorldCandidatePool> submitted_pools) {
  std::vector<const OneWorldCandidatePool*> ordered;
  ordered.reserve(submitted_pools.size());
  for (const OneWorldCandidatePool& pool : submitted_pools) {
    ordered.push_back(&pool);
  }
  std::ranges::sort(ordered,
                    [](const OneWorldCandidatePool* left, const OneWorldCandidatePool* right) {
                      return NetKey(left->net) < NetKey(right->net);
                    });
  std::vector<CanonicalPool> pools;
  pools.reserve(ordered.size());
  for (const OneWorldCandidatePool* submitted : ordered) {
    CanonicalPool pool{.net = submitted->net, .candidates = {}};
    pool.candidates.assign(submitted->candidates.begin(), submitted->candidates.end());
    std::ranges::sort(pool.candidates, [](const candidates::RouteCandidate* left,
                                          const candidates::RouteCandidate* right) {
      return left->id() < right->id();
    });
    pools.push_back(std::move(pool));
  }
  return pools;
}

[[nodiscard]] std::optional<std::uint64_t> NextPresentFactor(
    std::uint64_t epoch_index, const NegotiatedPriceUpdatePolicyV1& policy) noexcept {
  const std::optional<std::uint64_t> steps =
      CheckedMultiply(epoch_index, policy.present_factor_increment);
  if (!steps.has_value()) {
    return std::nullopt;
  }
  std::uint64_t factor = policy.initial_present_factor;
  if (!CheckedAdd(*steps, &factor)) {
    return std::nullopt;
  }
  return factor;
}

[[nodiscard]] std::variant<std::vector<routing::EdgeResourceKey>, NegotiatedRegenerationPlanError>
ExpandSelectedCandidate(const candidates::RouteCandidate& candidate, board_ir::EntityRef net,
                        const NegotiatedRegenerationPlanLimits& limits,
                        std::uint64_t* aggregate_uses) {
  std::uint64_t per_net_uses = 0;
  for (const candidates::PhysicalEdgeSpan& span : candidate.data().resources) {
    if (!CheckedAdd(span.edge_count, &per_net_uses)) {
      NegotiatedRegenerationPlanError error =
          Error(NegotiatedRegenerationPlanErrorCode::kArithmeticOverflow,
                "allocator.negotiated_plan.net_resource_count_overflow.v1",
                "Selected per-net resource-use count overflowed uint64");
      error.net = net;
      return error;
    }
  }
  if (per_net_uses > limits.maximum_selected_resource_uses_per_net) {
    NegotiatedRegenerationPlanError error =
        Error(NegotiatedRegenerationPlanErrorCode::kBoundExhausted,
              "allocator.negotiated_plan.net_resource_bound.v1",
              "One selected candidate exceeds the per-net resource-use bound");
    error.net = net;
    error.expected_value = limits.maximum_selected_resource_uses_per_net;
    error.actual_value = per_net_uses;
    return error;
  }
  if (per_net_uses > limits.maximum_aggregate_selected_resource_uses - *aggregate_uses) {
    NegotiatedRegenerationPlanError error =
        Error(NegotiatedRegenerationPlanErrorCode::kBoundExhausted,
              "allocator.negotiated_plan.aggregate_resource_bound.v1",
              "Selected candidates exceed the aggregate resource-use bound");
    error.net = net;
    error.expected_value = limits.maximum_aggregate_selected_resource_uses;
    error.actual_value = *aggregate_uses + per_net_uses;
    return error;
  }

  std::vector<routing::EdgeResourceKey> resources;
  resources.reserve(static_cast<std::size_t>(per_net_uses));
  for (const candidates::PhysicalEdgeSpan& span : candidate.data().resources) {
    const geometry_compiler::DirectionDelta delta =
        candidates::ResourceSpanStorageDelta(span.direction);
    for (std::uint32_t offset = 0; offset < span.edge_count; ++offset) {
      const Wide x = static_cast<Wide>(span.lattice_x) + static_cast<Wide>(delta.x) * offset;
      const Wide y = static_cast<Wide>(span.lattice_y) + static_cast<Wide>(delta.y) * offset;
      if (x < std::numeric_limits<std::int64_t>::min() ||
          x > std::numeric_limits<std::int64_t>::max() ||
          y < std::numeric_limits<std::int64_t>::min() ||
          y > std::numeric_limits<std::int64_t>::max() || span.usage_units != 1) {
        NegotiatedRegenerationPlanError error =
            Error(NegotiatedRegenerationPlanErrorCode::kInternalInvariant,
                  "allocator.negotiated_plan.selected_candidate_span.v1",
                  "P4R-03 selected an admitted candidate with a noncanonical resource span");
        error.net = net;
        return error;
      }
      routing::EdgeResourceKey resource{
          .layer = span.layer,
          .lattice_x = static_cast<std::int64_t>(x),
          .lattice_y = static_cast<std::int64_t>(y),
          .direction = span.direction,
      };
      if (!resources.empty() && !(resources.back() < resource)) {
        NegotiatedRegenerationPlanError error =
            Error(NegotiatedRegenerationPlanErrorCode::kInternalInvariant,
                  "allocator.negotiated_plan.selected_candidate_order.v1",
                  "P4R-03 selected candidate resources are not strictly canonical");
        error.net = net;
        return error;
      }
      resources.push_back(resource);
    }
  }
  *aggregate_uses += per_net_uses;
  return resources;
}

[[nodiscard]] std::uint64_t TargetIdentity(const NegotiatedRegenerationTarget& target,
                                           std::uint64_t snapshot_identity) noexcept {
  board_ir::StableHashBuilder hash;
  hash.AddString("APGAR-P4R06-NEGOTIATED-REGENERATION-TARGET-V1");
  hash.AddU64(snapshot_identity);
  hash.AddU64(target.net.id);
  hash.AddU32(target.net.generation);
  hash.AddU32(static_cast<std::uint32_t>(target.reason));
  hash.AddBool(target.selected_candidate_id.has_value());
  if (target.selected_candidate_id.has_value()) {
    HashCandidateId(&hash, *target.selected_candidate_id);
  }
  hash.AddU64(target.selected_resource_uses);
  hash.AddU64(target.total_trigger_price);
  hash.AddU64(static_cast<std::uint64_t>(target.triggering_hot_resources.size()));
  for (const routing::EdgeResourceKey& resource : target.triggering_hot_resources) {
    HashResource(&hash, resource);
  }
  return NonzeroHash(&hash);
}

void HashConfig(board_ir::StableHashBuilder* hash,
                const NegotiatedRegenerationPlanConfig& config) noexcept {
  const auto& selection = config.limits.selection;
  const auto& accounting = selection.accounting;
  hash->AddU64(config.price_policy.initial_present_factor);
  hash->AddU64(config.price_policy.present_factor_increment);
  hash->AddU64(config.price_policy.historical_price_increment);
  hash->AddU64(selection.maximum_net_pools);
  hash->AddU64(selection.maximum_total_candidates);
  hash->AddU64(accounting.maximum_candidates);
  hash->AddU64(accounting.maximum_expanded_resource_uses);
  hash->AddU64(accounting.maximum_usage_units_per_resource);
  hash->AddU64(config.limits.maximum_epoch_index);
  hash->AddU64(config.limits.maximum_price_entries);
  hash->AddU64(config.limits.maximum_hot_resources);
  hash->AddU64(config.limits.maximum_targets);
  hash->AddU64(config.limits.maximum_selected_resource_uses_per_net);
  hash->AddU64(config.limits.maximum_aggregate_selected_resource_uses);
  hash->AddU64(config.limits.maximum_hot_resources_per_target);
  hash->AddU64(config.limits.maximum_aggregate_target_resource_links);
  hash->AddU64(config.limits.maximum_price_value);
  hash->AddU64(config.limits.maximum_aggregate_price);
  hash->AddU64(config.limits.maximum_target_price_per_net);
  hash->AddU64(config.limits.maximum_aggregate_target_price);
}

[[nodiscard]] std::uint64_t PlanIdentity(const NegotiatedRegenerationPlan& plan,
                                         const NegotiatedRegenerationPlanConfig& config,
                                         std::span<const CanonicalPool> pools,
                                         std::uint64_t prior_identity) noexcept {
  board_ir::StableHashBuilder hash;
  hash.AddString("APGAR-P4R06-NEGOTIATED-REGENERATION-PLAN-V1");
  hash.AddU64(prior_identity);
  HashConfig(&hash, config);
  hash.AddU64(static_cast<std::uint64_t>(pools.size()));
  for (const CanonicalPool& pool : pools) {
    hash.AddU64(pool.net.id);
    hash.AddU32(pool.net.generation);
    hash.AddU64(static_cast<std::uint64_t>(pool.candidates.size()));
    for (const candidates::RouteCandidate* candidate : pool.candidates) {
      HashCandidateId(&hash, candidate->id());
    }
  }
  hash.AddU64(static_cast<std::uint64_t>(plan.selection.nets.size()));
  for (const OneWorldNetOutcome& outcome : plan.selection.nets) {
    if (auto* selected = std::get_if<OneWorldSelectedCandidate>(&outcome); selected != nullptr) {
      hash.AddBool(true);
      hash.AddU64(selected->net.id);
      hash.AddU32(selected->net.generation);
      HashCandidateId(&hash, selected->candidate_id);
    } else {
      const OneWorldCandidateAbsence& absence = std::get<OneWorldCandidateAbsence>(outcome);
      hash.AddBool(false);
      hash.AddU64(absence.net.id);
      hash.AddU32(absence.net.generation);
      hash.AddU32(static_cast<std::uint32_t>(absence.reason));
    }
  }
  hash.AddU64(plan.selection.accounting.candidate_count);
  hash.AddU64(plan.selection.accounting.expanded_resource_uses);
  hash.AddU64(plan.selection.accounting.overused_resource_count);
  hash.AddU64(plan.selection.accounting.total_overuse_units);
  hash.AddU64(static_cast<std::uint64_t>(plan.selection.accounting.resources.size()));
  for (const ResourceUsage& usage : plan.selection.accounting.resources) {
    HashResource(&hash, usage.resource);
    hash.AddU32(usage.capacity_units);
    hash.AddU64(usage.usage_units);
    hash.AddU64(usage.overuse_units);
  }
  hash.AddU64(plan.price_snapshot.snapshot_identity);
  hash.AddU32(static_cast<std::uint32_t>(plan.disposition));
  hash.AddU64(plan.selected_resource_uses);
  hash.AddU64(plan.target_resource_links);
  hash.AddU64(plan.aggregate_target_price);
  hash.AddU64(static_cast<std::uint64_t>(plan.hot_resources.size()));
  for (const NegotiatedHotResource& hot : plan.hot_resources) {
    HashResource(&hash, hot.resource);
    hash.AddU64(hot.usage_units);
    hash.AddU64(hot.overuse_units);
    hash.AddU64(hot.total_price);
  }
  hash.AddU64(static_cast<std::uint64_t>(plan.targets.size()));
  for (const NegotiatedRegenerationTarget& target : plan.targets) {
    hash.AddU64(target.target_identity);
  }
  return NonzeroHash(&hash);
}

}  // namespace

NegotiatedRegenerationPlanResult PlanNegotiatedRegeneration(
    const board_ir::BoardSnapshot& board, const ResourceCapacityModel& capacities,
    std::span<const OneWorldCandidatePool> submitted_pools,
    const NegotiatedPriceSnapshot* prior_prices, NegotiatedRegenerationPlanConfig config) {
  if (!ConfigurationIsValid(config)) {
    return Error(NegotiatedRegenerationPlanErrorCode::kInvalidConfiguration,
                 "allocator.negotiated_plan.configuration.v1",
                 "Negotiated-price planner configuration is outside its hard bounds");
  }
  if (board.content_hash() != capacities.associations().board_content_hash) {
    return Error(NegotiatedRegenerationPlanErrorCode::kAssociationMismatch,
                 "allocator.negotiated_plan.capacity_board_association.v1",
                 "Board IR does not match the resource-capacity model");
  }

  try {
    const std::uint64_t policy_identity = PricePolicyIdentity(config.price_policy);
    if (prior_prices != nullptr) {
      if (std::optional<NegotiatedRegenerationPlanError> error =
              ValidatePriorSnapshot(*prior_prices, capacities, config, policy_identity);
          error.has_value()) {
        return *error;
      }
    }

    OneWorldSelectionResult selection_result =
        SelectOneWorldZeroPrice(board, capacities, submitted_pools, config.limits.selection);
    if (auto* selection_error = std::get_if<OneWorldSelectionError>(&selection_result);
        selection_error != nullptr) {
      return SelectionFailure(*selection_error);
    }
    OneWorldSelection selection = std::get<OneWorldSelection>(std::move(selection_result));
    std::vector<CanonicalPool> pools = CanonicalPools(submitted_pools);

    const std::uint64_t epoch_index = prior_prices == nullptr ? 0 : prior_prices->epoch_index + 1U;
    if (epoch_index > config.limits.maximum_epoch_index) {
      NegotiatedRegenerationPlanError error =
          Error(NegotiatedRegenerationPlanErrorCode::kBoundExhausted,
                "allocator.negotiated_plan.epoch_bound.v1",
                "Price epoch exceeds the configured epoch bound");
      error.epoch_index = epoch_index;
      error.expected_value = config.limits.maximum_epoch_index;
      error.actual_value = epoch_index;
      return error;
    }
    const std::optional<std::uint64_t> present_factor =
        NextPresentFactor(epoch_index, config.price_policy);
    if (!present_factor.has_value()) {
      NegotiatedRegenerationPlanError error =
          Error(NegotiatedRegenerationPlanErrorCode::kArithmeticOverflow,
                "allocator.negotiated_plan.present_factor_overflow.v1",
                "Present-price schedule overflowed uint64");
      error.epoch_index = epoch_index;
      return error;
    }
    if (selection.accounting.overused_resource_count > config.limits.maximum_hot_resources) {
      NegotiatedRegenerationPlanError error =
          Error(NegotiatedRegenerationPlanErrorCode::kBoundExhausted,
                "allocator.negotiated_plan.hot_resource_bound.v1",
                "Current selection exceeds the configured hot-resource bound");
      error.epoch_index = epoch_index;
      error.expected_value = config.limits.maximum_hot_resources;
      error.actual_value = selection.accounting.overused_resource_count;
      return error;
    }

    std::map<routing::EdgeResourceKey, NegotiatedResourcePrice> current_prices;
    if (prior_prices != nullptr) {
      for (const NegotiatedResourcePrice& price : prior_prices->prices) {
        if (price.historical_price != 0) {
          current_prices.emplace(price.resource, NegotiatedResourcePrice{
                                                     .resource = price.resource,
                                                     .present_price = 0,
                                                     .historical_price = price.historical_price,
                                                     .total_price = price.historical_price,
                                                 });
        }
      }
    }

    NegotiatedPriceSnapshot snapshot{
        .associations = capacities.associations(),
        .capacity_units = capacities.capacity_units(),
        .policy_identity = policy_identity,
        .prior_snapshot_identity = prior_prices == nullptr ? 0 : prior_prices->snapshot_identity,
        .epoch_index = epoch_index,
        .present_factor = *present_factor,
        .prices = {},
        .snapshot_identity = 0,
    };

    for (const ResourceUsage& usage : selection.accounting.resources) {
      if (usage.overuse_units == 0) {
        continue;
      }
      if (!current_prices.contains(usage.resource) &&
          current_prices.size() >= config.limits.maximum_price_entries) {
        NegotiatedRegenerationPlanError error =
            Error(NegotiatedRegenerationPlanErrorCode::kBoundExhausted,
                  "allocator.negotiated_plan.price_entry_bound.v1",
                  "Updated allocator-price map exceeds the configured entry bound");
        error.resource = usage.resource;
        error.expected_value = config.limits.maximum_price_entries;
        error.actual_value = current_prices.size() + 1U;
        return error;
      }
      const std::optional<std::uint64_t> present_price =
          CheckedMultiply(*present_factor, usage.overuse_units);
      const std::optional<std::uint64_t> history_increment =
          CheckedMultiply(config.price_policy.historical_price_increment, usage.overuse_units);
      if (!present_price.has_value() || !history_increment.has_value()) {
        NegotiatedRegenerationPlanError error =
            Error(NegotiatedRegenerationPlanErrorCode::kArithmeticOverflow,
                  "allocator.negotiated_plan.price_multiply_overflow.v1",
                  "Negotiated present or historical price multiplication overflowed uint64");
        error.resource = usage.resource;
        error.epoch_index = epoch_index;
        return error;
      }
      NegotiatedResourcePrice& price = current_prices[usage.resource];
      price.resource = usage.resource;
      price.present_price = *present_price;
      if (!CheckedAdd(*history_increment, &price.historical_price)) {
        NegotiatedRegenerationPlanError error =
            Error(NegotiatedRegenerationPlanErrorCode::kArithmeticOverflow,
                  "allocator.negotiated_plan.history_overflow.v1",
                  "Negotiated historical price accumulation overflowed uint64");
        error.resource = usage.resource;
        error.epoch_index = epoch_index;
        return error;
      }
      price.total_price = price.present_price;
      if (!CheckedAdd(price.historical_price, &price.total_price)) {
        NegotiatedRegenerationPlanError error =
            Error(NegotiatedRegenerationPlanErrorCode::kArithmeticOverflow,
                  "allocator.negotiated_plan.total_price_overflow.v1",
                  "Negotiated present and historical prices overflowed uint64");
        error.resource = usage.resource;
        error.epoch_index = epoch_index;
        return error;
      }
    }

    std::uint64_t aggregate_price = 0;
    snapshot.prices.reserve(current_prices.size());
    for (const auto& [resource, price] : current_prices) {
      static_cast<void>(resource);
      if (price.present_price > config.limits.maximum_price_value ||
          price.historical_price > config.limits.maximum_price_value ||
          price.total_price > config.limits.maximum_price_value) {
        NegotiatedRegenerationPlanError error =
            Error(NegotiatedRegenerationPlanErrorCode::kBoundExhausted,
                  "allocator.negotiated_plan.price_value_bound.v1",
                  "An updated allocator price exceeds the configured value bound");
        error.resource = price.resource;
        error.epoch_index = epoch_index;
        error.expected_value = config.limits.maximum_price_value;
        error.actual_value =
            std::max({price.present_price, price.historical_price, price.total_price});
        return error;
      }
      if (!CheckedAdd(price.total_price, &aggregate_price)) {
        return Error(NegotiatedRegenerationPlanErrorCode::kArithmeticOverflow,
                     "allocator.negotiated_plan.aggregate_price_overflow.v1",
                     "Updated aggregate allocator price overflowed uint64");
      }
      if (aggregate_price > config.limits.maximum_aggregate_price) {
        NegotiatedRegenerationPlanError error =
            Error(NegotiatedRegenerationPlanErrorCode::kBoundExhausted,
                  "allocator.negotiated_plan.aggregate_price_bound.v1",
                  "Updated aggregate allocator price exceeds the configured bound");
        error.resource = price.resource;
        error.expected_value = config.limits.maximum_aggregate_price;
        error.actual_value = aggregate_price;
        return error;
      }
      snapshot.prices.push_back(price);
    }
    snapshot.snapshot_identity = SnapshotIdentity(snapshot);

    std::vector<NegotiatedHotResource> hot_resources;
    hot_resources.reserve(static_cast<std::size_t>(selection.accounting.overused_resource_count));
    for (const ResourceUsage& usage : selection.accounting.resources) {
      if (usage.overuse_units == 0) {
        continue;
      }
      const auto price = current_prices.find(usage.resource);
      if (price == current_prices.end() || price->second.total_price == 0) {
        return Error(NegotiatedRegenerationPlanErrorCode::kInternalInvariant,
                     "allocator.negotiated_plan.missing_hot_price.v1",
                     "An overused resource has no updated allocator price");
      }
      hot_resources.push_back(NegotiatedHotResource{
          .resource = usage.resource,
          .capacity_units = usage.capacity_units,
          .usage_units = usage.usage_units,
          .overuse_units = usage.overuse_units,
          .present_price = price->second.present_price,
          .historical_price = price->second.historical_price,
          .total_price = price->second.total_price,
      });
    }
    std::ranges::sort(hot_resources,
                      [](const NegotiatedHotResource& left, const NegotiatedHotResource& right) {
                        if (left.total_price != right.total_price) {
                          return left.total_price > right.total_price;
                        }
                        if (left.overuse_units != right.overuse_units) {
                          return left.overuse_units > right.overuse_units;
                        }
                        return left.resource < right.resource;
                      });

    std::map<routing::EdgeResourceKey, std::size_t> hot_rank;
    for (std::size_t index = 0; index < hot_resources.size(); ++index) {
      hot_rank.emplace(hot_resources[index].resource, index);
    }

    std::vector<NegotiatedRegenerationTarget> targets;
    targets.reserve(static_cast<std::size_t>(
        std::min<std::uint64_t>(pools.size(), config.limits.maximum_targets)));
    std::uint64_t aggregate_selected_uses = 0;
    std::uint64_t aggregate_target_links = 0;
    std::uint64_t aggregate_target_price = 0;
    if (selection.nets.size() != pools.size()) {
      return Error(NegotiatedRegenerationPlanErrorCode::kInternalInvariant,
                   "allocator.negotiated_plan.selection_shape.v1",
                   "P4R-03 selection omitted or added a canonical net outcome");
    }
    for (std::size_t pool_index = 0; pool_index < pools.size(); ++pool_index) {
      const CanonicalPool& pool = pools[pool_index];
      const OneWorldNetOutcome& outcome = selection.nets[pool_index];
      if (auto* absence = std::get_if<OneWorldCandidateAbsence>(&outcome); absence != nullptr) {
        if (absence->net != pool.net || !pool.candidates.empty()) {
          return Error(NegotiatedRegenerationPlanErrorCode::kInternalInvariant,
                       "allocator.negotiated_plan.absence_shape.v1",
                       "P4R-03 structured absence does not match its canonical pool");
        }
        NegotiatedRegenerationTarget target{
            .net = pool.net,
            .reason = NegotiatedRegenerationTargetReason::kEmptyPool,
            .selected_candidate_id = std::nullopt,
            .triggering_hot_resources = {},
            .selected_resource_uses = 0,
            .total_trigger_price = 0,
            .target_identity = 0,
        };
        target.target_identity = TargetIdentity(target, snapshot.snapshot_identity);
        if (targets.size() >= config.limits.maximum_targets) {
          NegotiatedRegenerationPlanError error =
              Error(NegotiatedRegenerationPlanErrorCode::kBoundExhausted,
                    "allocator.negotiated_plan.target_count_bound.v1",
                    "Regeneration targets exceed the configured aggregate bound");
          error.net = target.net;
          error.expected_value = config.limits.maximum_targets;
          error.actual_value = targets.size() + 1U;
          return error;
        }
        targets.push_back(std::move(target));
        continue;
      }

      const OneWorldSelectedCandidate& selected = std::get<OneWorldSelectedCandidate>(outcome);
      if (selected.net != pool.net) {
        return Error(NegotiatedRegenerationPlanErrorCode::kInternalInvariant,
                     "allocator.negotiated_plan.selection_net_order.v1",
                     "P4R-03 selected outcome does not match canonical pool order");
      }
      const auto found = std::ranges::lower_bound(
          pool.candidates, selected.candidate_id, {},
          [](const candidates::RouteCandidate* candidate) { return candidate->id(); });
      if (found == pool.candidates.end() || (*found)->id() != selected.candidate_id) {
        return Error(NegotiatedRegenerationPlanErrorCode::kInternalInvariant,
                     "allocator.negotiated_plan.selected_candidate_lookup.v1",
                     "P4R-03 selected candidate is absent from its immutable input pool");
      }
      auto expansion =
          ExpandSelectedCandidate(**found, pool.net, config.limits, &aggregate_selected_uses);
      if (auto* expansion_error = std::get_if<NegotiatedRegenerationPlanError>(&expansion);
          expansion_error != nullptr) {
        return *expansion_error;
      }
      std::vector<routing::EdgeResourceKey> selected_resources =
          std::get<std::vector<routing::EdgeResourceKey>>(std::move(expansion));
      NegotiatedRegenerationTarget target{
          .net = pool.net,
          .reason = NegotiatedRegenerationTargetReason::kHotResource,
          .selected_candidate_id = selected.candidate_id,
          .triggering_hot_resources = {},
          .selected_resource_uses = static_cast<std::uint64_t>(selected_resources.size()),
          .total_trigger_price = 0,
          .target_identity = 0,
      };
      for (const routing::EdgeResourceKey& resource : selected_resources) {
        const auto rank = hot_rank.find(resource);
        if (rank == hot_rank.end()) {
          continue;
        }
        if (target.triggering_hot_resources.empty() &&
            targets.size() >= config.limits.maximum_targets) {
          NegotiatedRegenerationPlanError error =
              Error(NegotiatedRegenerationPlanErrorCode::kBoundExhausted,
                    "allocator.negotiated_plan.target_count_bound.v1",
                    "Regeneration targets exceed the configured aggregate bound");
          error.net = target.net;
          error.expected_value = config.limits.maximum_targets;
          error.actual_value = targets.size() + 1U;
          return error;
        }
        if (target.triggering_hot_resources.size() >=
            config.limits.maximum_hot_resources_per_target) {
          NegotiatedRegenerationPlanError error =
              Error(NegotiatedRegenerationPlanErrorCode::kBoundExhausted,
                    "allocator.negotiated_plan.net_hot_resource_bound.v1",
                    "One target net exceeds its hot-resource link bound");
          error.net = pool.net;
          error.resource = resource;
          error.expected_value = config.limits.maximum_hot_resources_per_target;
          error.actual_value = target.triggering_hot_resources.size() + 1U;
          return error;
        }
        target.triggering_hot_resources.push_back(resource);
        if (!CheckedAdd(hot_resources[rank->second].total_price, &target.total_trigger_price)) {
          NegotiatedRegenerationPlanError error =
              Error(NegotiatedRegenerationPlanErrorCode::kArithmeticOverflow,
                    "allocator.negotiated_plan.net_target_price_overflow.v1",
                    "One target net's aggregate hot-resource price overflowed uint64");
          error.net = pool.net;
          return error;
        }
        if (target.total_trigger_price > config.limits.maximum_target_price_per_net) {
          NegotiatedRegenerationPlanError error =
              Error(NegotiatedRegenerationPlanErrorCode::kBoundExhausted,
                    "allocator.negotiated_plan.net_target_price_bound.v1",
                    "One target net exceeds its aggregate trigger-price bound");
          error.net = pool.net;
          error.resource = resource;
          error.expected_value = config.limits.maximum_target_price_per_net;
          error.actual_value = target.total_trigger_price;
          return error;
        }
        if (!CheckedAdd(1, &aggregate_target_links)) {
          return Error(NegotiatedRegenerationPlanErrorCode::kArithmeticOverflow,
                       "allocator.negotiated_plan.aggregate_target_link_overflow.v1",
                       "Aggregate target hot-resource links overflowed uint64");
        }
        if (aggregate_target_links > config.limits.maximum_aggregate_target_resource_links) {
          NegotiatedRegenerationPlanError error =
              Error(NegotiatedRegenerationPlanErrorCode::kBoundExhausted,
                    "allocator.negotiated_plan.aggregate_target_link_bound.v1",
                    "Target hot-resource links exceed the configured aggregate bound");
          error.net = pool.net;
          error.resource = resource;
          error.expected_value = config.limits.maximum_aggregate_target_resource_links;
          error.actual_value = aggregate_target_links;
          return error;
        }
        if (!CheckedAdd(hot_resources[rank->second].total_price, &aggregate_target_price)) {
          return Error(NegotiatedRegenerationPlanErrorCode::kArithmeticOverflow,
                       "allocator.negotiated_plan.aggregate_target_price_overflow.v1",
                       "Aggregate target trigger price overflowed uint64");
        }
        if (aggregate_target_price > config.limits.maximum_aggregate_target_price) {
          NegotiatedRegenerationPlanError error =
              Error(NegotiatedRegenerationPlanErrorCode::kBoundExhausted,
                    "allocator.negotiated_plan.aggregate_target_price_bound.v1",
                    "Aggregate target trigger price exceeds the configured bound");
          error.net = pool.net;
          error.resource = resource;
          error.expected_value = config.limits.maximum_aggregate_target_price;
          error.actual_value = aggregate_target_price;
          return error;
        }
      }
      if (target.triggering_hot_resources.empty()) {
        continue;
      }
      std::ranges::sort(
          target.triggering_hot_resources,
          [&hot_rank](const routing::EdgeResourceKey& left, const routing::EdgeResourceKey& right) {
            return hot_rank.at(left) < hot_rank.at(right);
          });
      target.target_identity = TargetIdentity(target, snapshot.snapshot_identity);
      targets.push_back(std::move(target));
    }
    if (aggregate_selected_uses != selection.accounting.expanded_resource_uses) {
      return Error(NegotiatedRegenerationPlanErrorCode::kInternalInvariant,
                   "allocator.negotiated_plan.accounting_expansion_agreement.v1",
                   "Independent target expansion disagrees with P4R-03 accounting");
    }

    std::ranges::sort(targets, [](const NegotiatedRegenerationTarget& left,
                                  const NegotiatedRegenerationTarget& right) {
      if (left.reason != right.reason) {
        return left.reason == NegotiatedRegenerationTargetReason::kEmptyPool;
      }
      if (left.reason == NegotiatedRegenerationTargetReason::kHotResource) {
        if (left.total_trigger_price != right.total_trigger_price) {
          return left.total_trigger_price > right.total_trigger_price;
        }
        if (left.triggering_hot_resources.size() != right.triggering_hot_resources.size()) {
          return left.triggering_hot_resources.size() > right.triggering_hot_resources.size();
        }
      }
      return NetKey(left.net) < NetKey(right.net);
    });
    NegotiatedRegenerationPlan plan{
        .associations = capacities.associations(),
        .selection = std::move(selection),
        .price_snapshot = std::move(snapshot),
        .hot_resources = std::move(hot_resources),
        .targets = std::move(targets),
        .disposition = NegotiatedRegenerationDisposition::kNoRegenerationRequired,
        .plan_identity = 0,
        .selected_resource_uses = aggregate_selected_uses,
        .target_resource_links = aggregate_target_links,
        .aggregate_target_price = aggregate_target_price,
    };
    if (!plan.targets.empty()) {
      plan.disposition = NegotiatedRegenerationDisposition::kRegenerationRequired;
    }
    const std::uint64_t prior_identity =
        prior_prices == nullptr ? 0 : prior_prices->snapshot_identity;
    plan.plan_identity = PlanIdentity(plan, config, pools, prior_identity);
    return plan;
  } catch (const std::bad_alloc&) {
    return Error(NegotiatedRegenerationPlanErrorCode::kResourceExhausted,
                 "allocator.negotiated_plan.allocation.v1",
                 "Negotiated-price planning scratch allocation failed");
  } catch (const std::length_error&) {
    return Error(NegotiatedRegenerationPlanErrorCode::kResourceExhausted,
                 "allocator.negotiated_plan.container_capacity.v1",
                 "Negotiated-price planning container capacity was exceeded");
  } catch (...) {
    return Error(NegotiatedRegenerationPlanErrorCode::kInternalInvariant,
                 "allocator.negotiated_plan.exception.v1",
                 "Negotiated-price planning raised an unexpected exception");
  }
}

}  // namespace apgar::allocator
