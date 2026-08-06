#ifndef APGAR_ALLOCATOR_NEGOTIATED_REGENERATION_PLAN_H_
#define APGAR_ALLOCATOR_NEGOTIATED_REGENERATION_PLAN_H_

#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <variant>
#include <vector>

#include "apgar/allocator/one_world_selection.h"
#include "apgar/allocator/resource_accounting.h"
#include "apgar/board_ir/board.h"
#include "apgar/candidates/route_candidate.h"
#include "apgar/routing/candidate_policy.h"

namespace apgar::allocator {

inline constexpr std::uint64_t kMaximumNegotiatedPriceEpoch = 1'000'000;
inline constexpr std::uint64_t kMaximumNegotiatedPriceEntries = 100'000'000;
inline constexpr std::uint64_t kMaximumNegotiatedHotResources = 100'000'000;
inline constexpr std::uint64_t kMaximumNegotiatedRegenerationTargets = 1'000'000;
inline constexpr std::uint64_t kMaximumNegotiatedTargetResourceLinks = 100'000'000;

// Candidate-allocator price schedule for P4R-06. This state is independent of
// the baseline-local congestion costs used by CS-RR-v1.
struct NegotiatedPriceUpdatePolicyV1 {
  std::uint64_t initial_present_factor = 100;
  std::uint64_t present_factor_increment = 100;
  std::uint64_t historical_price_increment = 10;

  friend bool operator==(const NegotiatedPriceUpdatePolicyV1&,
                         const NegotiatedPriceUpdatePolicyV1&) = default;
};

struct NegotiatedResourcePrice {
  routing::EdgeResourceKey resource;
  std::uint64_t present_price = 0;
  std::uint64_t historical_price = 0;
  std::uint64_t total_price = 0;

  friend bool operator==(const NegotiatedResourcePrice&, const NegotiatedResourcePrice&) = default;
};

// Immutable replay input for a later price-update epoch. Entries are strictly
// sorted by canonical EdgeResourceKey. Identity is a deterministic integrity
// binding, not a cryptographic attestation.
struct NegotiatedPriceSnapshot {
  ResourceLatticeAssociations associations;
  std::uint32_t capacity_units = 0;
  std::uint64_t policy_identity = 0;
  std::uint64_t prior_snapshot_identity = 0;
  std::uint64_t epoch_index = 0;
  std::uint64_t present_factor = 0;
  std::vector<NegotiatedResourcePrice> prices;
  std::uint64_t snapshot_identity = 0;

  friend bool operator==(const NegotiatedPriceSnapshot&, const NegotiatedPriceSnapshot&) = default;
};

struct NegotiatedHotResource {
  routing::EdgeResourceKey resource;
  std::uint32_t capacity_units = 0;
  std::uint64_t usage_units = 0;
  std::uint64_t overuse_units = 0;
  std::uint64_t present_price = 0;
  std::uint64_t historical_price = 0;
  std::uint64_t total_price = 0;

  friend bool operator==(const NegotiatedHotResource&, const NegotiatedHotResource&) = default;
};

enum class NegotiatedRegenerationTargetReason : std::uint8_t {
  kEmptyPool = 0,
  kHotResource = 1,
};

struct NegotiatedRegenerationTarget {
  board_ir::EntityRef net;
  NegotiatedRegenerationTargetReason reason = NegotiatedRegenerationTargetReason::kEmptyPool;
  std::optional<candidates::CandidateId> selected_candidate_id;
  // In global hot-resource priority order, not EdgeResourceKey order.
  std::vector<routing::EdgeResourceKey> triggering_hot_resources;
  std::uint64_t selected_resource_uses = 0;
  std::uint64_t total_trigger_price = 0;
  std::uint64_t target_identity = 0;

  friend bool operator==(const NegotiatedRegenerationTarget&,
                         const NegotiatedRegenerationTarget&) = default;
};

enum class NegotiatedRegenerationDisposition : std::uint8_t {
  kNoRegenerationRequired = 0,
  kRegenerationRequired = 1,
};

struct NegotiatedRegenerationPlan {
  ResourceLatticeAssociations associations;
  OneWorldSelection selection;
  NegotiatedPriceSnapshot price_snapshot;
  // Descending total price, descending overuse, then EdgeResourceKey.
  std::vector<NegotiatedHotResource> hot_resources;
  // Empty-pool targets first by net, then hot-resource targets by descending
  // trigger price, descending trigger count, and net.
  std::vector<NegotiatedRegenerationTarget> targets;
  NegotiatedRegenerationDisposition disposition =
      NegotiatedRegenerationDisposition::kNoRegenerationRequired;
  std::uint64_t plan_identity = 0;
  std::uint64_t selected_resource_uses = 0;
  std::uint64_t target_resource_links = 0;
  std::uint64_t aggregate_target_price = 0;

  friend bool operator==(const NegotiatedRegenerationPlan&,
                         const NegotiatedRegenerationPlan&) = default;
};

struct NegotiatedRegenerationPlanLimits {
  OneWorldSelectionLimits selection = {
      .maximum_net_pools = 1'024,
      .maximum_total_candidates = 131'072,
      .accounting =
          ResourceAccountingLimits{
              .maximum_candidates = 1'024,
              .maximum_expanded_resource_uses = 100'000'000,
              .maximum_usage_units_per_resource = std::numeric_limits<std::uint64_t>::max(),
          },
  };
  std::uint64_t maximum_epoch_index = 1'024;
  std::uint64_t maximum_price_entries = 1'000'000;
  std::uint64_t maximum_hot_resources = 1'000'000;
  std::uint64_t maximum_targets = 1'024;
  std::uint64_t maximum_selected_resource_uses_per_net = 1'000'000;
  std::uint64_t maximum_aggregate_selected_resource_uses = 100'000'000;
  std::uint64_t maximum_hot_resources_per_target = 1'000'000;
  std::uint64_t maximum_aggregate_target_resource_links = 100'000'000;
  std::uint64_t maximum_price_value = 10'000'000;
  std::uint64_t maximum_aggregate_price = 1'000'000'000'000ULL;
  std::uint64_t maximum_target_price_per_net = 1'000'000'000'000ULL;
  std::uint64_t maximum_aggregate_target_price = 1'000'000'000'000'000ULL;

  friend bool operator==(const NegotiatedRegenerationPlanLimits&,
                         const NegotiatedRegenerationPlanLimits&) = default;
};

struct NegotiatedRegenerationPlanConfig {
  NegotiatedPriceUpdatePolicyV1 price_policy;
  NegotiatedRegenerationPlanLimits limits;

  friend bool operator==(const NegotiatedRegenerationPlanConfig&,
                         const NegotiatedRegenerationPlanConfig&) = default;
};

enum class NegotiatedRegenerationPlanErrorCode : std::uint8_t {
  kInvalidConfiguration = 0,
  kInvalidInput = 1,
  kAssociationMismatch = 2,
  kBoundExhausted = 3,
  kArithmeticOverflow = 4,
  kSelectionFailure = 5,
  kResourceExhausted = 6,
  kInternalInvariant = 7,
};

struct NegotiatedRegenerationPlanError {
  NegotiatedRegenerationPlanErrorCode code =
      NegotiatedRegenerationPlanErrorCode::kInvalidConfiguration;
  std::string_view invariant_id;
  std::string_view detail;
  std::optional<board_ir::EntityRef> net;
  std::optional<routing::EdgeResourceKey> resource;
  std::optional<std::uint64_t> epoch_index;
  std::optional<std::uint64_t> expected_value;
  std::optional<std::uint64_t> actual_value;
  std::optional<OneWorldSelectionErrorCode> selection_error_code;
  std::optional<ResourceAccountingErrorCode> accounting_error_code;

  friend bool operator==(const NegotiatedRegenerationPlanError&,
                         const NegotiatedRegenerationPlanError&) = default;
};

using NegotiatedRegenerationPlanResult =
    std::variant<NegotiatedRegenerationPlan, NegotiatedRegenerationPlanError>;

// Pure P4R-06 planning boundary. It calls P4R-03 selection/accounting, updates
// allocator-owned prices, and returns a replayable plan. It never routes,
// builds, admits, stores, publishes, or mutates a candidate or pool.
[[nodiscard]] NegotiatedRegenerationPlanResult PlanNegotiatedRegeneration(
    const board_ir::BoardSnapshot& board, const ResourceCapacityModel& capacities,
    std::span<const OneWorldCandidatePool> pools,
    const NegotiatedPriceSnapshot* prior_prices = nullptr,
    NegotiatedRegenerationPlanConfig config = {});

// Verifies the stable replay bindings and canonical shape of an immutable
// P4R-06 plan against its exact source pools and optional prior snapshot. This
// does not rerun P4R-03 selection or perform any P4R-07 execution work.
[[nodiscard]] std::optional<NegotiatedRegenerationPlanError>
ValidateNegotiatedRegenerationPlanReplay(const ResourceCapacityModel& capacities,
                                         std::span<const OneWorldCandidatePool> pools,
                                         const NegotiatedRegenerationPlan& plan,
                                         const NegotiatedPriceSnapshot* prior_prices = nullptr,
                                         NegotiatedRegenerationPlanConfig config = {});

}  // namespace apgar::allocator

#endif  // APGAR_ALLOCATOR_NEGOTIATED_REGENERATION_PLAN_H_
