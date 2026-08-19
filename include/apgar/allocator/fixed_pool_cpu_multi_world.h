#ifndef APGAR_ALLOCATOR_FIXED_POOL_CPU_MULTI_WORLD_H_
#define APGAR_ALLOCATOR_FIXED_POOL_CPU_MULTI_WORLD_H_

#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <variant>
#include <vector>

#include "apgar/allocator/cpu_candidate_allocation_session.h"
#include "apgar/allocator/negotiated_regeneration_plan.h"
#include "apgar/allocator/one_world_selection.h"
#include "apgar/allocator/resource_accounting.h"
#include "apgar/board_ir/board.h"

namespace apgar::allocator {

inline constexpr std::uint64_t kMaximumFixedPoolCpuMultiWorlds = 1'000'000;
inline constexpr std::uint64_t kMaximumFixedPoolCpuMultiWorldRounds = 1'000'000'000;
inline constexpr std::uint64_t kMaximumFixedPoolCpuMultiWorldWorkItems =
    1'000'000'000'000'000'000ULL;
inline constexpr std::uint64_t kMaximumFixedPoolCpuMultiWorldRecords = 1'000'000'000;
inline constexpr std::uint64_t kMaximumFixedPoolCpuMultiWorldParetoComparisons =
    1'000'000'000'000ULL;

struct FixedPoolCpuWorldSchedule {
  std::uint64_t schedule_key = 0;
  NegotiatedPriceUpdatePolicyV1 price_policy;
  std::uint32_t maximum_selection_rounds = 1;

  friend bool operator==(const FixedPoolCpuWorldSchedule&,
                         const FixedPoolCpuWorldSchedule&) = default;
};

struct FixedPoolCpuWorldPriceState {
  std::uint64_t policy_identity = 0;
  std::uint64_t prior_state_identity = 0;
  std::uint64_t completed_updates = 0;
  std::uint64_t present_factor = 0;
  std::vector<NegotiatedResourcePrice> prices;
  std::uint64_t state_identity = 0;

  friend bool operator==(const FixedPoolCpuWorldPriceState&,
                         const FixedPoolCpuWorldPriceState&) = default;
};

struct FixedPoolCpuWorldRoundTrace {
  std::uint32_t round_index = 0;
  std::uint64_t input_price_state_identity = 0;
  std::uint64_t selection_identity = 0;
  std::optional<std::uint64_t> output_price_state_identity;

  friend bool operator==(const FixedPoolCpuWorldRoundTrace&,
                         const FixedPoolCpuWorldRoundTrace&) = default;
};

enum class FixedPoolCpuWorldTerminalReason : std::uint8_t {
  kFeasible = 0,
  kNoCandidateWithoutRegeneration = 1,
  kSelectionRoundBound = 2,
};

struct FixedPoolCpuWorldObjective {
  std::uint64_t selected_net_count = 0;
  std::uint64_t missing_net_count = 0;
  std::uint64_t total_overuse_units = 0;
  std::uint64_t total_intrinsic_cost = 0;

  friend bool operator==(const FixedPoolCpuWorldObjective&,
                         const FixedPoolCpuWorldObjective&) = default;
};

struct FixedPoolCpuWorldSummary {
  std::uint64_t world_identity = 0;
  FixedPoolCpuWorldSchedule schedule;
  FixedPoolCpuWorldTerminalReason terminal_reason =
      FixedPoolCpuWorldTerminalReason::kSelectionRoundBound;
  std::vector<FixedPoolCpuWorldRoundTrace> trace;
  FixedPoolCpuWorldObjective objective;
  std::uint64_t final_price_state_identity = 0;
  std::uint64_t final_selection_identity = 0;
  std::uint64_t outcome_identity = 0;
  bool pareto_eligible = false;
  bool pareto_retained = false;

  friend bool operator==(const FixedPoolCpuWorldSummary&,
                         const FixedPoolCpuWorldSummary&) = default;
};

struct RetainedFixedPoolCpuWorld {
  std::uint64_t world_identity = 0;
  FixedPoolCpuWorldSchedule schedule;
  FixedPoolCpuWorldTerminalReason terminal_reason =
      FixedPoolCpuWorldTerminalReason::kSelectionRoundBound;
  FixedPoolCpuWorldObjective objective;
  FixedPoolCpuWorldPriceState price_state;
  OneWorldSelection selection;
  std::vector<FixedPoolCpuWorldRoundTrace> trace;
  std::uint64_t outcome_identity = 0;

  friend bool operator==(const RetainedFixedPoolCpuWorld&,
                         const RetainedFixedPoolCpuWorld&) = default;
};

struct FixedPoolCpuMultiWorldCounters {
  std::uint64_t scheduled_worlds = 0;
  std::uint64_t completed_worlds = 0;
  std::uint64_t selection_rounds = 0;
  std::uint64_t price_updates = 0;
  std::uint64_t candidate_evaluations = 0;
  std::uint64_t candidate_resource_visits = 0;
  std::uint64_t selected_resource_uses = 0;
  std::uint64_t net_outcomes = 0;
  std::uint64_t emitted_price_entries = 0;
  std::uint64_t trace_records = 0;
  std::uint64_t pareto_comparisons = 0;
  std::uint64_t pareto_eligible_worlds = 0;
  std::uint64_t retained_worlds = 0;

  friend bool operator==(const FixedPoolCpuMultiWorldCounters&,
                         const FixedPoolCpuMultiWorldCounters&) = default;
};

struct FixedPoolCpuMultiWorldLimits {
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
  std::uint64_t maximum_worlds = 256;
  std::uint64_t maximum_total_selection_rounds = 65'536;
  std::uint64_t maximum_total_price_updates = 65'536;
  std::uint64_t maximum_candidate_evaluations = 8'589'934'592ULL;
  std::uint64_t maximum_candidate_resource_visits = 1'000'000'000'000ULL;
  std::uint64_t maximum_selected_resource_uses = 100'000'000'000ULL;
  std::uint64_t maximum_net_outcomes = 67'108'864;
  std::uint64_t maximum_emitted_price_entries = 100'000'000'000ULL;
  std::uint64_t maximum_trace_records = 65'536;
  std::uint64_t maximum_buffered_terminal_selection_records = 262'144;
  std::uint64_t maximum_buffered_terminal_resource_records = 100'000'000;
  std::uint64_t maximum_buffered_terminal_price_records = 100'000'000;
  std::uint64_t maximum_pareto_comparisons = 32'640;
  std::uint64_t maximum_retained_worlds = 256;
  std::uint64_t maximum_retained_selection_records = 262'144;
  std::uint64_t maximum_retained_resource_records = 100'000'000;
  std::uint64_t maximum_retained_price_records = 100'000'000;
  std::uint64_t maximum_price_entries_per_world = 1'000'000;
  std::uint64_t maximum_price_value = 10'000'000;
  std::uint64_t maximum_aggregate_price_per_world = 1'000'000'000'000ULL;
  std::uint64_t maximum_candidate_score = 1'000'000'000'000'000ULL;

  friend bool operator==(const FixedPoolCpuMultiWorldLimits&,
                         const FixedPoolCpuMultiWorldLimits&) = default;
};

struct FixedPoolCpuMultiWorldConfig {
  FixedPoolCpuMultiWorldLimits limits;
  std::uint64_t maximum_near_feasible_missing_nets = 0;
  std::uint64_t maximum_near_feasible_overuse_units = 0;

  friend bool operator==(const FixedPoolCpuMultiWorldConfig&,
                         const FixedPoolCpuMultiWorldConfig&) = default;
};

enum class FixedPoolCpuMultiWorldDisposition : std::uint8_t {
  kCompleted = 0,
  kNoEligibleWorlds = 1,
};

enum class FixedPoolCpuMultiWorldErrorCode : std::uint8_t {
  kInvalidConfiguration = 0,
  kInvalidInput = 1,
  kAssociationMismatch = 2,
  kSourceReplayFailure = 3,
  kInvalidSchedule = 4,
  kDuplicateSchedule = 5,
  kBoundExhausted = 6,
  kRetentionBoundExhausted = 7,
  kArithmeticOverflow = 8,
  kAccountingFailure = 9,
  kResourceExhausted = 10,
  kInternalInvariant = 11,
};

struct FixedPoolCpuMultiWorldError {
  FixedPoolCpuMultiWorldErrorCode code = FixedPoolCpuMultiWorldErrorCode::kInvalidConfiguration;
  std::string_view invariant_id;
  std::string_view detail;
  std::optional<std::uint64_t> schedule_key;
  std::optional<std::uint64_t> expected_value;
  std::optional<std::uint64_t> actual_value;
  std::optional<CpuCandidateAllocationSessionError> source_error;
  std::optional<ResourceAccountingError> accounting_error;

  friend bool operator==(const FixedPoolCpuMultiWorldError&,
                         const FixedPoolCpuMultiWorldError&) = default;
};

class FixedPoolCpuMultiWorldExecution {
 public:
  FixedPoolCpuMultiWorldExecution(FixedPoolCpuMultiWorldExecution&&) noexcept = default;
  FixedPoolCpuMultiWorldExecution& operator=(FixedPoolCpuMultiWorldExecution&&) noexcept = default;
  FixedPoolCpuMultiWorldExecution(const FixedPoolCpuMultiWorldExecution&) = delete;
  FixedPoolCpuMultiWorldExecution& operator=(const FixedPoolCpuMultiWorldExecution&) = delete;

  [[nodiscard]] const FixedPoolCpuMultiWorldConfig& config() const noexcept { return config_; }
  [[nodiscard]] FixedPoolCpuMultiWorldDisposition disposition() const noexcept {
    return disposition_;
  }
  [[nodiscard]] std::uint64_t source_session_identity() const noexcept {
    return source_session_identity_;
  }
  [[nodiscard]] std::uint64_t source_pool_identity() const noexcept {
    return source_pool_identity_;
  }
  [[nodiscard]] std::uint64_t common_state_identity() const noexcept {
    return common_state_identity_;
  }
  [[nodiscard]] std::span<const FixedPoolCpuWorldSchedule> schedules() const noexcept {
    return schedules_;
  }
  [[nodiscard]] std::span<const FixedPoolCpuWorldSummary> summaries() const noexcept {
    return summaries_;
  }
  [[nodiscard]] std::span<const RetainedFixedPoolCpuWorld> retained_worlds() const noexcept {
    return retained_worlds_;
  }
  [[nodiscard]] std::optional<std::uint64_t> preferred_world_identity() const noexcept {
    return preferred_world_identity_;
  }
  [[nodiscard]] const FixedPoolCpuMultiWorldCounters& counters() const noexcept {
    return counters_;
  }
  [[nodiscard]] std::uint64_t execution_identity() const noexcept { return execution_identity_; }

 private:
  FixedPoolCpuMultiWorldExecution() = default;

  FixedPoolCpuMultiWorldConfig config_;
  FixedPoolCpuMultiWorldDisposition disposition_ = FixedPoolCpuMultiWorldDisposition::kCompleted;
  std::uint64_t source_session_identity_ = 0;
  std::uint64_t source_pool_identity_ = 0;
  std::uint64_t common_state_identity_ = 0;
  std::vector<FixedPoolCpuWorldSchedule> schedules_;
  std::vector<FixedPoolCpuWorldSummary> summaries_;
  std::vector<RetainedFixedPoolCpuWorld> retained_worlds_;
  std::optional<std::uint64_t> preferred_world_identity_;
  FixedPoolCpuMultiWorldCounters counters_;
  std::uint64_t execution_identity_ = 0;

  friend struct FixedPoolCpuMultiWorldExecutionFactory;
};

using FixedPoolCpuMultiWorldResult =
    std::variant<FixedPoolCpuMultiWorldExecution, FixedPoolCpuMultiWorldError>;

// Deterministic fixed-pool CPU reference. Every world branches from the same
// replay-validated P4R-08 final pool and common zero-price state. The function
// never routes, regenerates, admits, prunes, publishes, or mutates candidates.
[[nodiscard]] FixedPoolCpuMultiWorldResult ExecuteFixedPoolCpuMultiWorld(
    const board_ir::BoardSnapshot& board, const ResourceCapacityModel& capacities,
    const CpuCandidateAllocationSession& source, CpuCandidateAllocationSessionConfig source_config,
    std::span<const FixedPoolCpuWorldSchedule> schedules, FixedPoolCpuMultiWorldConfig config = {});

}  // namespace apgar::allocator

#endif  // APGAR_ALLOCATOR_FIXED_POOL_CPU_MULTI_WORLD_H_
