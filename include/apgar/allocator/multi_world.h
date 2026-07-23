#ifndef APGAR_ALLOCATOR_MULTI_WORLD_H_
#define APGAR_ALLOCATOR_MULTI_WORLD_H_

#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "apgar/allocator/negotiated_prices.h"
#include "apgar/candidates/candidate_store.h"

namespace apgar::allocator {

inline constexpr std::uint32_t kMultiWorldExecutionSchemaVersion = 1;
inline constexpr std::uint64_t kMaximumMultiWorldsV1 = 1'000'000;
inline constexpr std::uint64_t kMaximumMultiWorldSelectionRoundsV1 = 1'000'000;
inline constexpr std::uint64_t kMaximumMultiWorldWorkItemsV1 = 100'000'000;
inline constexpr std::uint64_t kMaximumMultiWorldParetoComparisonsV1 = 100'000'000;

// One immutable complete candidate-pool snapshot shared by every branch. The
// workload is borrowed during execution and must be non-null and authentic.
struct MultiWorldPoolSnapshot {
  ResourceCapacityModel capacities;
  OneWorldAllocatorLimits allocator_limits;
  std::vector<CandidatePool> pools;
  const MultiNetWorkload* workload = nullptr;
};

// Search schedules are canonicalized by schedule_key. The weight influences
// selection, while retained-world quality always uses the common unweighted
// intrinsic base cost so outcomes remain comparable across schedules.
struct MultiWorldSchedule {
  std::uint64_t schedule_key = 0;
  std::uint64_t search_intrinsic_cost_weight = 1;
  std::uint32_t maximum_selection_rounds = 1;

  friend bool operator==(const MultiWorldSchedule&, const MultiWorldSchedule&) = default;
};

struct MultiWorldExecutionConfig {
  std::uint64_t maximum_worlds = 256;
  std::uint64_t maximum_total_selection_rounds = 10'000;
  std::uint64_t maximum_total_candidate_evaluations = 100'000'000;
  std::uint64_t maximum_total_candidate_span_visits = 100'000'000;
  // Conservative resource work charged once for source authentication and for
  // each complete One-World allocation pass, including the hidden
  // reconstruction inside each price update.
  std::uint64_t maximum_total_resource_work_units = 100'000'000;
  std::uint64_t maximum_total_net_outcomes = 100'000'000;
  std::uint64_t maximum_pareto_comparisons = 1'000'000;
  std::uint64_t maximum_buffered_terminal_selection_records = 10'000'000;
  std::uint64_t maximum_buffered_terminal_resource_records = 10'000'000;
  std::uint64_t maximum_buffered_terminal_price_records = 10'000'000;
  std::uint64_t maximum_retained_worlds = 256;
  std::uint64_t maximum_retained_selection_records = 10'000'000;
  std::uint64_t maximum_retained_resource_records = 10'000'000;
  std::uint64_t maximum_retained_price_records = 10'000'000;
  std::uint64_t maximum_retained_winner_pins = 1'000'000;
  std::uint64_t maximum_near_feasible_missing_nets = 1'000'000;
  std::uint64_t maximum_near_feasible_overuse_units = std::numeric_limits<std::uint64_t>::max();
  // A nonzero value declares that an exact combined-route conflict cannot be
  // represented by the current resource vocabulary. No world may be called
  // feasible in that state.
  std::uint64_t known_unmapped_exact_conflict_count = 0;

  friend bool operator==(const MultiWorldExecutionConfig&,
                         const MultiWorldExecutionConfig&) = default;
};

enum class MultiWorldExecutionDisposition : std::uint8_t {
  kCompleted = 0,
  kNoEligibleWorlds = 1,
  kResourceRefinementRequired = 2,
};

enum class MultiWorldTerminalReason : std::uint8_t {
  kFeasible = 0,
  kNoCandidateWithoutRegeneration = 1,
  kSelectionRoundLimit = 2,
};

struct MultiWorldRoundTrace {
  std::uint32_t round_index = 0;
  std::uint64_t price_state_checksum = 0;
  std::uint64_t world_checksum = 0;

  friend bool operator==(const MultiWorldRoundTrace&, const MultiWorldRoundTrace&) = default;
};

// Compact terminal evidence for every scheduled branch. It intentionally does
// not retain candidate handles, resource vectors, or price vectors.
struct MultiWorldSummary {
  std::uint64_t world_identity = 0;
  MultiWorldSchedule schedule;
  MultiWorldTerminalReason terminal_reason = MultiWorldTerminalReason::kSelectionRoundLimit;
  std::vector<MultiWorldRoundTrace> trace;
  std::uint64_t selected_net_count = 0;
  std::uint64_t no_candidate_net_count = 0;
  std::uint64_t overused_resource_count = 0;
  std::uint64_t total_overuse_units = 0;
  std::uint64_t total_intrinsic_cost = 0;
  bool pareto_eligible = false;
  bool pareto_retained = false;

  friend bool operator==(const MultiWorldSummary&, const MultiWorldSummary&) = default;
};

struct RetainedMultiWorld {
  std::uint64_t world_identity = 0;
  MultiWorldSchedule schedule;
  NegotiatedPriceState price_state;
  OneWorldAllocation world;
};

struct MultiWorldExecutionCounters {
  std::uint64_t scheduled_worlds = 0;
  std::uint64_t completed_worlds = 0;
  // Branch-only rounds and updates exclude the fixed source-authentication
  // pass charged by the four candidate-processing counters below.
  std::uint64_t selection_rounds = 0;
  std::uint64_t price_updates = 0;
  std::uint64_t candidate_evaluations = 0;
  std::uint64_t candidate_span_visits = 0;
  std::uint64_t resource_work_units = 0;
  std::uint64_t net_outcomes = 0;
  std::uint64_t pareto_comparisons = 0;
  std::uint64_t pareto_eligible_worlds = 0;
  std::uint64_t retained_worlds = 0;
  std::uint64_t retained_winner_pins = 0;

  friend bool operator==(const MultiWorldExecutionCounters&,
                         const MultiWorldExecutionCounters&) = default;
};

// Diagnostic-only wall intervals for one separately instrumented fixed-pool
// multi-world execution. The ordinary execution entry point never observes
// or mutates this profile.
struct MultiWorldOperationalProfileV1 {
  std::uint64_t component_wall_nanoseconds = 0;
  std::uint64_t validation_and_source_preflight_wall_nanoseconds = 0;
  std::uint64_t selection_and_resource_accumulation_wall_nanoseconds = 0;
  std::uint64_t price_update_and_snapshot_wall_nanoseconds = 0;
  std::uint64_t terminal_retention_and_assembly_wall_nanoseconds = 0;
  std::uint64_t unclassified_serial_wall_nanoseconds = 0;

  friend bool operator==(const MultiWorldOperationalProfileV1&,
                         const MultiWorldOperationalProfileV1&) = default;
};

enum class MultiWorldExecutionErrorCode : std::uint8_t {
  kUnsupportedSchema = 0,
  kInvalidConfiguration = 1,
  kInvalidSchedule = 2,
  kDuplicateSchedule = 3,
  kAssociationMismatch = 4,
  kInvalidBranchState = 5,
  kSourcePoolInvalid = 6,
  kWorkBoundExceeded = 7,
  kAllocation = 8,
  kPriceUpdate = 9,
  kCandidateStoreLease = 10,
  kResourceExhausted = 11,
  kInternalInvariant = 12,
};

struct MultiWorldExecutionError {
  MultiWorldExecutionErrorCode code = MultiWorldExecutionErrorCode::kInvalidConfiguration;
  std::string_view invariant_id;
  std::string_view detail;

  friend bool operator==(const MultiWorldExecutionError&,
                         const MultiWorldExecutionError&) = default;
};

class MultiWorldExecution {
 public:
  MultiWorldExecution(const MultiWorldExecution&) = delete;
  MultiWorldExecution(MultiWorldExecution&&) noexcept = default;
  MultiWorldExecution& operator=(const MultiWorldExecution&) = delete;
  MultiWorldExecution& operator=(MultiWorldExecution&&) noexcept = default;

  [[nodiscard]] std::uint32_t schema_version() const noexcept { return schema_version_; }
  [[nodiscard]] const MultiWorldExecutionConfig& config() const noexcept { return config_; }
  [[nodiscard]] std::uint64_t source_snapshot_checksum() const noexcept {
    return source_snapshot_checksum_;
  }
  [[nodiscard]] std::uint64_t branch_state_checksum() const noexcept {
    return branch_state_checksum_;
  }
  [[nodiscard]] MultiWorldExecutionDisposition disposition() const noexcept { return disposition_; }
  [[nodiscard]] const MultiWorldExecutionCounters& counters() const noexcept { return counters_; }
  [[nodiscard]] const std::vector<MultiWorldSummary>& summaries() const noexcept {
    return summaries_;
  }
  [[nodiscard]] const std::vector<RetainedMultiWorld>& retained_worlds() const noexcept {
    return retained_worlds_;
  }
  [[nodiscard]] std::optional<std::uint64_t> preferred_world_identity() const noexcept {
    return preferred_world_identity_;
  }
  [[nodiscard]] std::uint64_t execution_checksum() const noexcept { return execution_checksum_; }
  [[nodiscard]] bool has_active_retention_lease() const noexcept {
    return retention_lease_.has_value() && retention_lease_->active();
  }
  [[nodiscard]] bool retention_lease_belongs_to(
      const candidates::CandidateStore& store) const noexcept {
    return retention_lease_.has_value() && retention_lease_->belongs_to(store);
  }

 private:
  MultiWorldExecution(std::uint32_t schema_version, MultiWorldExecutionConfig config,
                      std::uint64_t source_snapshot_checksum, std::uint64_t branch_state_checksum,
                      MultiWorldExecutionDisposition disposition,
                      MultiWorldExecutionCounters counters,
                      std::vector<MultiWorldSummary> summaries,
                      std::vector<RetainedMultiWorld> retained_worlds,
                      std::optional<std::uint64_t> preferred_world_identity,
                      std::uint64_t execution_checksum,
                      candidates::CandidateStorePinLease retention_lease)
      : schema_version_(schema_version),
        config_(config),
        source_snapshot_checksum_(source_snapshot_checksum),
        branch_state_checksum_(branch_state_checksum),
        disposition_(disposition),
        counters_(counters),
        summaries_(std::move(summaries)),
        retained_worlds_(std::move(retained_worlds)),
        preferred_world_identity_(preferred_world_identity),
        execution_checksum_(execution_checksum),
        retention_lease_(std::move(retention_lease)) {}

  std::uint32_t schema_version_ = kMultiWorldExecutionSchemaVersion;
  MultiWorldExecutionConfig config_;
  std::uint64_t source_snapshot_checksum_ = 0;
  std::uint64_t branch_state_checksum_ = 0;
  MultiWorldExecutionDisposition disposition_ = MultiWorldExecutionDisposition::kCompleted;
  MultiWorldExecutionCounters counters_;
  std::vector<MultiWorldSummary> summaries_;
  std::vector<RetainedMultiWorld> retained_worlds_;
  std::optional<std::uint64_t> preferred_world_identity_;
  std::uint64_t execution_checksum_ = 0;
  std::optional<candidates::CandidateStorePinLease> retention_lease_;

  friend std::variant<MultiWorldExecution, MultiWorldExecutionError> ExecuteMultiWorldCpu(
      std::uint32_t, const MultiWorldPoolSnapshot&, const NegotiatedPriceState&,
      std::span<const MultiWorldSchedule>, candidates::CandidateStore&,
      const MultiWorldExecutionConfig&);
  template <bool>
  friend std::variant<MultiWorldExecution, MultiWorldExecutionError> ExecuteMultiWorldCpuImpl(
      std::uint32_t, const MultiWorldPoolSnapshot&, const NegotiatedPriceState&,
      std::span<const MultiWorldSchedule>, candidates::CandidateStore&,
      const MultiWorldExecutionConfig&, MultiWorldOperationalProfileV1*);
};

using MultiWorldExecutionResult = std::variant<MultiWorldExecution, MultiWorldExecutionError>;

// Deterministic fixed-pool CPU reference. CandidateStore supplies one complete
// source lease before execution and one retained-winner lease at publication;
// no candidate generation or store mutation occurs between them.
[[nodiscard]] MultiWorldExecutionResult ExecuteMultiWorldCpu(
    std::uint32_t schema_version, const MultiWorldPoolSnapshot& source,
    const NegotiatedPriceState& branch_state, std::span<const MultiWorldSchedule> schedules,
    candidates::CandidateStore& candidate_store, const MultiWorldExecutionConfig& config);

[[nodiscard]] MultiWorldExecutionResult ExecuteMultiWorldCpuWithOperationalProfileV1(
    std::uint32_t schema_version, const MultiWorldPoolSnapshot& source,
    const NegotiatedPriceState& branch_state, std::span<const MultiWorldSchedule> schedules,
    candidates::CandidateStore& candidate_store, const MultiWorldExecutionConfig& config,
    MultiWorldOperationalProfileV1& operational_profile);

}  // namespace apgar::allocator

#endif  // APGAR_ALLOCATOR_MULTI_WORLD_H_
