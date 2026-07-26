#ifndef APGAR_ALLOCATOR_TARGETED_REGENERATION_H_
#define APGAR_ALLOCATOR_TARGETED_REGENERATION_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "apgar/allocator/negotiated_prices.h"
#include "apgar/candidates/candidate_store.h"

namespace apgar::allocator {

class TargetedRegenerationPlan;
struct TargetedRegenerationNet;

namespace internal {
void SetTargetedRegenerationPlanSchemaVersionForTesting(TargetedRegenerationPlan& plan,
                                                        std::uint32_t schema_version) noexcept;
void SetTargetedRegenerationCoverageSeedTargetCountForTesting(
    TargetedRegenerationPlan& plan, std::uint64_t coverage_seed_target_count) noexcept;
void ReplaceTargetedRegenerationTargetForTesting(TargetedRegenerationPlan& plan,
                                                 std::size_t target_index,
                                                 TargetedRegenerationNet target);
void SetTargetedRegenerationPlanAggregatesForTesting(TargetedRegenerationPlan& plan,
                                                     std::uint64_t total_requested_columns,
                                                     std::uint64_t total_resource_actions,
                                                     std::uint64_t total_conflict_resources,
                                                     std::uint64_t total_conflict_impact) noexcept;
}  // namespace internal

inline constexpr std::uint32_t kTargetedRegenerationPlanSchemaVersionV1 = 1;
inline constexpr std::uint32_t kTargetedRegenerationPlanSchemaVersionV2 = 2;
inline constexpr std::uint32_t kTargetedRegenerationPlanSchemaVersion = 3;

struct TargetedRegenerationConfig {
  std::uint64_t maximum_target_nets = 100'000;
  std::uint64_t maximum_columns_per_net = 16;
  std::uint64_t maximum_total_columns = 1'000'000;
  std::uint64_t maximum_resource_actions_per_net = 64;
  std::uint64_t maximum_total_resource_actions = 1'000'000;
  std::uint64_t maximum_expanded_resource_visits = 100'000'000;

  friend bool operator==(const TargetedRegenerationConfig&,
                         const TargetedRegenerationConfig&) = default;
};

struct RegenerationResourceAction {
  routing::EdgeResourceKey resource;
  std::uint64_t observed_overuse_units = 0;
  std::uint64_t selected_candidate_usage_units = 0;
  std::uint64_t negotiated_price = 0;
  std::uint64_t conflict_impact = 0;

  friend bool operator==(const RegenerationResourceAction&,
                         const RegenerationResourceAction&) = default;
};

struct TargetedRegenerationNet {
  board_ir::EntityRef net{};
  std::uint64_t source_pool_manifest_checksum = 0;
  std::uint64_t source_pool_candidate_count = 0;
  candidates::CandidateId source_selected_candidate_id;
  std::uint64_t source_selected_candidate_payload_checksum = 0;
  candidates::CandidateId next_price_candidate_id;
  std::uint64_t next_price_candidate_payload_checksum = 0;
  std::uint64_t next_price_selection_score = 0;
  std::uint64_t conflict_resource_count = 0;
  std::uint64_t conflict_impact = 0;
  std::uint64_t negotiated_price_exposure = 0;
  std::uint64_t requested_columns = 0;
  // Stable severity order: impact, overuse, price, then canonical resource.
  std::vector<RegenerationResourceAction> resource_actions;

  friend bool operator==(const TargetedRegenerationNet&, const TargetedRegenerationNet&) = default;
};

enum class TargetedRegenerationErrorCode : std::uint8_t {
  kUnsupportedSchema = 0,
  kInvalidConfiguration = 1,
  kInvalidPricingInput = 2,
  kWorkBoundExceeded = 3,
  kArithmeticOverflow = 4,
  kResourceExhausted = 5,
  kInternalInvariant = 6,
  kCandidateStoreLease = 7,
};

struct TargetedRegenerationError {
  TargetedRegenerationErrorCode code = TargetedRegenerationErrorCode::kInvalidConfiguration;
  std::string_view invariant_id;
  std::string_view detail;

  friend bool operator==(const TargetedRegenerationError&,
                         const TargetedRegenerationError&) = default;
};

struct TargetedRegenerationPlanningOperationalProfileV1 {
  std::uint32_t epoch_index = 0;
  std::uint64_t plan_checksum = 0;
  std::uint64_t component_wall_nanoseconds = 0;
  std::uint64_t source_selection_and_resource_accumulation_wall_nanoseconds = 0;
  std::uint64_t price_update_wall_nanoseconds = 0;
  std::uint64_t next_price_selection_and_resource_accumulation_wall_nanoseconds = 0;
  std::uint64_t target_ranking_retention_and_assembly_wall_nanoseconds = 0;
  std::uint64_t unclassified_serial_wall_nanoseconds = 0;
  std::uint64_t price_update_operations = 0;

  friend bool operator==(const TargetedRegenerationPlanningOperationalProfileV1&,
                         const TargetedRegenerationPlanningOperationalProfileV1&) = default;
};

class TargetedRegenerationPlan {
 public:
  TargetedRegenerationPlan(const TargetedRegenerationPlan&) = delete;
  TargetedRegenerationPlan(TargetedRegenerationPlan&&) noexcept = default;
  TargetedRegenerationPlan& operator=(const TargetedRegenerationPlan&) = delete;
  TargetedRegenerationPlan& operator=(TargetedRegenerationPlan&&) noexcept = default;

  [[nodiscard]] std::uint32_t schema_version() const noexcept { return schema_version_; }
  [[nodiscard]] const AllocationAssociations& associations() const noexcept {
    return associations_;
  }
  [[nodiscard]] std::uint64_t workload_checksum() const noexcept { return workload_checksum_; }
  [[nodiscard]] std::uint64_t source_request_manifest_checksum() const noexcept {
    return source_request_manifest_checksum_;
  }
  [[nodiscard]] std::uint64_t candidate_pool_manifest_checksum() const noexcept {
    return candidate_pool_manifest_checksum_;
  }
  [[nodiscard]] std::uint64_t source_pool_count() const noexcept { return source_pool_count_; }
  [[nodiscard]] std::uint64_t source_candidate_count() const noexcept {
    return source_candidate_count_;
  }
  [[nodiscard]] std::uint64_t pinned_candidate_count() const noexcept {
    return pinned_candidate_count_;
  }
  [[nodiscard]] bool has_active_pin_lease() const noexcept {
    return pin_lease_.has_value() && pin_lease_->active();
  }
  [[nodiscard]] bool pin_lease_belongs_to(
      const candidates::CandidateStore& candidate_store) const noexcept {
    return pin_lease_.has_value() && pin_lease_->belongs_to(candidate_store);
  }
  [[nodiscard]] const TargetedRegenerationConfig& config() const noexcept { return config_; }
  [[nodiscard]] const NegotiatedPriceState& price_state() const noexcept { return price_state_; }
  [[nodiscard]] const std::vector<TargetedRegenerationNet>& targets() const noexcept {
    return targets_;
  }
  [[nodiscard]] std::uint64_t coverage_seed_target_count() const noexcept {
    return coverage_seed_target_count_;
  }
  [[nodiscard]] std::uint64_t total_requested_columns() const noexcept {
    return total_requested_columns_;
  }
  [[nodiscard]] std::uint64_t total_resource_actions() const noexcept {
    return total_resource_actions_;
  }
  [[nodiscard]] std::uint64_t total_conflict_resources() const noexcept {
    return total_conflict_resources_;
  }
  [[nodiscard]] std::uint64_t total_conflict_impact() const noexcept {
    return total_conflict_impact_;
  }
  [[nodiscard]] std::uint64_t expanded_resource_visits() const noexcept {
    return expanded_resource_visits_;
  }
  [[nodiscard]] std::uint64_t plan_checksum() const noexcept { return plan_checksum_; }

  friend bool operator==(const TargetedRegenerationPlan& left,
                         const TargetedRegenerationPlan& right) noexcept {
    return left.schema_version_ == right.schema_version_ &&
           left.associations_ == right.associations_ &&
           left.workload_checksum_ == right.workload_checksum_ &&
           left.source_request_manifest_checksum_ == right.source_request_manifest_checksum_ &&
           left.candidate_pool_manifest_checksum_ == right.candidate_pool_manifest_checksum_ &&
           left.source_pool_count_ == right.source_pool_count_ &&
           left.source_candidate_count_ == right.source_candidate_count_ &&
           left.pinned_candidate_count_ == right.pinned_candidate_count_ &&
           left.config_ == right.config_ && left.price_state_ == right.price_state_ &&
           left.targets_ == right.targets_ &&
           left.coverage_seed_target_count_ == right.coverage_seed_target_count_ &&
           left.total_requested_columns_ == right.total_requested_columns_ &&
           left.total_resource_actions_ == right.total_resource_actions_ &&
           left.total_conflict_resources_ == right.total_conflict_resources_ &&
           left.total_conflict_impact_ == right.total_conflict_impact_ &&
           left.expanded_resource_visits_ == right.expanded_resource_visits_ &&
           left.plan_checksum_ == right.plan_checksum_;
  }

 private:
  TargetedRegenerationPlan(
      std::uint32_t schema_version, AllocationAssociations associations,
      std::uint64_t workload_checksum, std::uint64_t source_request_manifest_checksum,
      std::uint64_t candidate_pool_manifest_checksum, std::uint64_t source_pool_count,
      std::uint64_t source_candidate_count, std::uint64_t pinned_candidate_count,
      TargetedRegenerationConfig config, NegotiatedPriceState price_state,
      std::vector<TargetedRegenerationNet> targets, std::uint64_t coverage_seed_target_count,
      std::uint64_t total_requested_columns, std::uint64_t total_resource_actions,
      std::uint64_t total_conflict_resources, std::uint64_t total_conflict_impact,
      std::uint64_t expanded_resource_visits, std::uint64_t plan_checksum,
      std::optional<candidates::CandidateStorePinLease> pin_lease)
      : schema_version_(schema_version),
        associations_(associations),
        workload_checksum_(workload_checksum),
        source_request_manifest_checksum_(source_request_manifest_checksum),
        candidate_pool_manifest_checksum_(candidate_pool_manifest_checksum),
        source_pool_count_(source_pool_count),
        source_candidate_count_(source_candidate_count),
        pinned_candidate_count_(pinned_candidate_count),
        config_(config),
        price_state_(std::move(price_state)),
        targets_(std::move(targets)),
        coverage_seed_target_count_(coverage_seed_target_count),
        total_requested_columns_(total_requested_columns),
        total_resource_actions_(total_resource_actions),
        total_conflict_resources_(total_conflict_resources),
        total_conflict_impact_(total_conflict_impact),
        expanded_resource_visits_(expanded_resource_visits),
        plan_checksum_(plan_checksum),
        pin_lease_(std::move(pin_lease)) {}

  std::uint32_t schema_version_ = kTargetedRegenerationPlanSchemaVersion;
  AllocationAssociations associations_;
  std::uint64_t workload_checksum_ = 0;
  std::uint64_t source_request_manifest_checksum_ = 0;
  std::uint64_t candidate_pool_manifest_checksum_ = 0;
  std::uint64_t source_pool_count_ = 0;
  std::uint64_t source_candidate_count_ = 0;
  std::uint64_t pinned_candidate_count_ = 0;
  TargetedRegenerationConfig config_;
  NegotiatedPriceState price_state_;
  std::vector<TargetedRegenerationNet> targets_;
  std::uint64_t coverage_seed_target_count_ = 0;
  std::uint64_t total_requested_columns_ = 0;
  std::uint64_t total_resource_actions_ = 0;
  std::uint64_t total_conflict_resources_ = 0;
  std::uint64_t total_conflict_impact_ = 0;
  std::uint64_t expanded_resource_visits_ = 0;
  std::uint64_t plan_checksum_ = 0;
  std::optional<candidates::CandidateStorePinLease> pin_lease_;

  friend std::variant<TargetedRegenerationPlan, TargetedRegenerationError>
  BuildTargetedRegenerationPlan(std::uint32_t, const NegotiatedPriceState&,
                                const OneWorldAllocationRequest&, const OneWorldAllocation&,
                                candidates::CandidateStore&, const TargetedRegenerationConfig&);
  friend void internal::SetTargetedRegenerationPlanSchemaVersionForTesting(
      TargetedRegenerationPlan&, std::uint32_t) noexcept;
  friend void internal::SetTargetedRegenerationCoverageSeedTargetCountForTesting(
      TargetedRegenerationPlan&, std::uint64_t) noexcept;
  friend void internal::ReplaceTargetedRegenerationTargetForTesting(TargetedRegenerationPlan&,
                                                                    std::size_t,
                                                                    TargetedRegenerationNet);
  friend void internal::SetTargetedRegenerationPlanAggregatesForTesting(TargetedRegenerationPlan&,
                                                                        std::uint64_t,
                                                                        std::uint64_t,
                                                                        std::uint64_t,
                                                                        std::uint64_t) noexcept;
  template <bool>
  friend std::variant<TargetedRegenerationPlan, TargetedRegenerationError>
  BuildTargetedRegenerationPlanImpl(std::uint32_t, const NegotiatedPriceState&,
                                    const OneWorldAllocationRequest&, const OneWorldAllocation&,
                                    candidates::CandidateStore&, const TargetedRegenerationConfig&,
                                    TargetedRegenerationPlanningOperationalProfileV1*);
};

using TargetedRegenerationPlanResult =
    std::variant<TargetedRegenerationPlan, TargetedRegenerationError>;

using TargetedRegenerationPolicyResult =
    std::variant<std::vector<routing::NormalizedCandidateGenerationPolicy>,
                 TargetedRegenerationError>;

// Deterministic CPU-reference pricing policies for one retained regeneration
// target. Column zero applies the complete next negotiated-price field. Each
// later column additionally bans one distinct resource in the target's stable
// action order. The scheduling seed and first ordinal are caller-visible
// provenance inputs and are checked before any policy bulk is materialized.
[[nodiscard]] TargetedRegenerationPolicyResult BuildTargetedRegenerationPoliciesV1(
    const PreparedNetRoutingContext& context, const NegotiatedPriceState& next_price_state,
    std::uint64_t intrinsic_cost_weight, const TargetedRegenerationNet& target,
    std::uint64_t deterministic_seed, std::uint32_t first_candidate_ordinal);

[[nodiscard]] TargetedRegenerationPlanResult BuildTargetedRegenerationPlan(
    std::uint32_t schema_version, const NegotiatedPriceState& previous_price_state,
    const OneWorldAllocationRequest& source_request, const OneWorldAllocation& world,
    candidates::CandidateStore& candidate_store, const TargetedRegenerationConfig& config);

[[nodiscard]] TargetedRegenerationPlanResult BuildTargetedRegenerationPlanWithOperationalProfileV1(
    std::uint32_t schema_version, const NegotiatedPriceState& previous_price_state,
    const OneWorldAllocationRequest& source_request, const OneWorldAllocation& world,
    candidates::CandidateStore& candidate_store, const TargetedRegenerationConfig& config,
    TargetedRegenerationPlanningOperationalProfileV1& operational_profile);

}  // namespace apgar::allocator

#endif  // APGAR_ALLOCATOR_TARGETED_REGENERATION_H_
