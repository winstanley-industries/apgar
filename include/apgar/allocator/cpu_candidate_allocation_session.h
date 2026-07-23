#ifndef APGAR_ALLOCATOR_CPU_CANDIDATE_ALLOCATION_SESSION_H_
#define APGAR_ALLOCATOR_CPU_CANDIDATE_ALLOCATION_SESSION_H_

#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "apgar/allocator/cpu_candidate_pool_preparation.h"
#include "apgar/allocator/multi_world.h"
#include "apgar/allocator/targeted_regeneration_execution.h"

namespace apgar::allocator {

inline constexpr std::uint32_t kCpuCandidateAllocationSessionSchemaVersionV1 = 1;
inline constexpr std::uint32_t kCpuCandidateAllocationSessionSchemaVersionV2 = 2;
inline constexpr std::uint32_t kCpuCandidateAllocationSessionSchemaVersion = 3;
inline constexpr std::uint32_t kMaximumCpuCandidateAllocationEpochsV1 = 1'000'000;
inline constexpr std::uint64_t kMaximumCpuCandidateAllocationWorkItemsV1 = 1'000'000'000'000'000ULL;

struct CpuCandidateAllocationSessionLimits {
  std::uint64_t maximum_epoch_records = 8;
  std::uint64_t maximum_total_planning_expanded_resource_visits = 800'000'000;
  std::uint64_t maximum_total_requested_columns = 512;
  std::uint64_t maximum_total_route_queries = 512;
  std::uint64_t maximum_total_route_work_units = 20'000'000'000ULL;
  std::uint64_t maximum_total_policy_projection_visits = 800'000'000;
  std::uint64_t maximum_total_generated_candidate_bytes = 64ULL * 1024ULL * 1024ULL * 1024ULL;
  std::uint64_t maximum_total_rejection_bytes = 4ULL * 1024ULL * 1024ULL * 1024ULL;
  std::uint64_t maximum_total_transient_result_bytes = 16ULL * 1024ULL * 1024ULL * 1024ULL;

  friend bool operator==(const CpuCandidateAllocationSessionLimits&,
                         const CpuCandidateAllocationSessionLimits&) = default;
};

struct CpuCandidateAllocationSessionConfig {
  std::uint32_t schema_version = kCpuCandidateAllocationSessionSchemaVersion;
  std::uint64_t intrinsic_cost_weight = 1;
  std::uint32_t maximum_regeneration_epochs = 4;
  NegotiatedPriceConfig price_config{
      .present_step_per_overuse_unit = 10,
      .history_step_per_overuse_unit = 1,
      .maximum_price_per_resource = 1'000'000,
      .maximum_iterations = 4,
      .maximum_price_records = 1'000'000,
  };
  OneWorldAllocatorLimits allocator_limits;
  TargetedRegenerationConfig regeneration_plan_config{
      .maximum_target_nets = 16,
      .maximum_columns_per_net = 4,
      .maximum_total_columns = 64,
      .maximum_resource_actions_per_net = 16,
      .maximum_total_resource_actions = 256,
      .maximum_expanded_resource_visits = 100'000'000,
  };
  TargetedRegenerationExecutionConfig regeneration_execution_config;
  std::vector<MultiWorldSchedule> schedules{
      MultiWorldSchedule{
          .schedule_key = 1,
          .search_intrinsic_cost_weight = 1,
          .maximum_selection_rounds = 1,
      },
  };
  MultiWorldExecutionConfig multi_world_config;
  CpuCandidateAllocationSessionLimits limits;
  // The session is the sole authority. Nested component fields must remain
  // zero; execution copies this value into each bounded component call.
  std::uint64_t known_unmapped_exact_conflict_count = 0;

  friend bool operator==(const CpuCandidateAllocationSessionConfig&,
                         const CpuCandidateAllocationSessionConfig&) = default;
};

enum class CpuCandidateAllocationTerminalReason : std::uint8_t {
  kFeasible = 0,
  kFixedPoint = 1,
  kRegenerationEpochLimit = 2,
  kResourceRefinementRequired = 3,
};
static_assert(static_cast<std::uint8_t>(
                  CpuCandidateAllocationTerminalReason::kResourceRefinementRequired) == 3);

struct CpuCandidateAllocationEpochRecord {
  std::uint32_t epoch_index = 0;
  std::uint64_t source_pool_manifest_checksum = 0;
  std::uint64_t successor_pool_manifest_checksum = 0;
  std::uint64_t source_world_checksum = 0;
  std::uint64_t successor_world_checksum = 0;
  std::uint64_t successor_price_state_checksum = 0;
  std::uint64_t plan_checksum = 0;
  std::uint64_t execution_checksum = 0;
  TargetedRegenerationExecutionDisposition disposition =
      TargetedRegenerationExecutionDisposition::kNoWork;
  TargetedRegenerationTerminalReason terminal_reason =
      TargetedRegenerationTerminalReason::kNoTargets;
  TargetedRegenerationExecutionCounters counters;
  std::vector<TargetedRegenerationColumnRecord> columns;
  bool pool_manifest_changed = false;
  bool selected_route_roster_changed = false;
  bool complete_price_values_changed = false;
  bool fixed_point = false;

  friend bool operator==(const CpuCandidateAllocationEpochRecord&,
                         const CpuCandidateAllocationEpochRecord&) = default;
};

struct CpuCandidateAllocationSessionCounters {
  std::uint64_t completed_regeneration_epochs = 0;
  std::uint64_t planning_expanded_resource_visits = 0;
  std::uint64_t requested_columns = 0;
  std::uint64_t route_queries = 0;
  std::uint64_t route_work_units = 0;
  std::uint64_t policy_projection_visits = 0;
  std::uint64_t generated_candidate_bytes = 0;
  std::uint64_t rejection_record_bytes = 0;
  std::uint64_t transient_result_bytes = 0;
  std::uint64_t admitted_candidates = 0;
  std::uint64_t duplicate_candidates = 0;
  std::uint64_t rejected_columns = 0;
  std::uint64_t novel_retained_candidates = 0;
  std::uint64_t changed_selections = 0;

  friend bool operator==(const CpuCandidateAllocationSessionCounters&,
                         const CpuCandidateAllocationSessionCounters&) = default;
};

// Compact same-run association for one separately instrumented reusable
// candidate-allocation session. It deliberately retains no config, column, or
// contender storage so authentic destruction remains inside measured scope.
// Publication must join it to an independent unmeasured full-preimage replay;
// this capture is not standalone algorithm authority.
struct CpuCandidateAllocationSessionReplayWitnessV1 {
  std::uint64_t session_checksum = 0;
  std::uint64_t board_content_hash = 0;
  std::uint64_t workload_checksum = 0;
  std::uint64_t capacity_model_checksum = 0;
  std::uint64_t preparation_checksum = 0;
  std::uint32_t maximum_regeneration_epochs = 0;
  CpuCandidateAllocationTerminalReason terminal_reason =
      CpuCandidateAllocationTerminalReason::kRegenerationEpochLimit;
  CpuCandidateAllocationSessionCounters counters;
  std::uint64_t epoch_record_count = 0;
  std::uint64_t epoch_association_checksum = 0;
  std::uint64_t final_pool_manifest_checksum = 0;
  std::uint64_t final_rejection_manifest_checksum = 0;

  friend bool operator==(const CpuCandidateAllocationSessionReplayWitnessV1&,
                         const CpuCandidateAllocationSessionReplayWitnessV1&) = default;
};

struct CpuCandidateAllocationSessionOperationalProfileV1 {
  std::uint64_t component_wall_nanoseconds = 0;
  std::uint64_t validation_and_source_inspection_wall_nanoseconds = 0;
  std::uint64_t initial_price_state_wall_nanoseconds = 0;
  std::uint64_t initial_selection_and_resource_accumulation_wall_nanoseconds = 0;
  std::uint64_t targeted_regeneration_planning_wall_nanoseconds = 0;
  std::uint64_t targeted_regeneration_price_update_wall_nanoseconds = 0;
  std::uint64_t targeted_regeneration_selection_and_target_planning_wall_nanoseconds = 0;
  std::uint64_t targeted_regeneration_execution_wall_nanoseconds = 0;
  std::uint64_t successor_correlation_wall_nanoseconds = 0;
  std::uint64_t terminal_multi_world_component_wall_nanoseconds = 0;
  std::uint64_t terminal_multi_world_price_update_wall_nanoseconds = 0;
  std::uint64_t final_manifest_and_assembly_wall_nanoseconds = 0;
  std::uint64_t unclassified_serial_wall_nanoseconds = 0;
  std::uint64_t planning_expanded_resource_visits = 0;
  std::vector<TargetedRegenerationPlanningOperationalProfileV1> regeneration_plans;
  std::vector<TargetedRegenerationOperationalProfileV1> regeneration_epochs;
  MultiWorldOperationalProfileV1 terminal_multi_world;
  CpuCandidateAllocationSessionReplayWitnessV1 replay_witness;

  friend bool operator==(const CpuCandidateAllocationSessionOperationalProfileV1&,
                         const CpuCandidateAllocationSessionOperationalProfileV1&) = default;
};

enum class CpuCandidateAllocationSessionErrorCode : std::uint8_t {
  kUnsupportedSchema = 0,
  kInvalidConfiguration = 1,
  kAssociationMismatch = 2,
  kWorkBoundExceeded = 3,
  kInitialPriceState = 4,
  kInitialAllocation = 5,
  kTargetedRegenerationPlan = 6,
  kTargetedRegenerationExecution = 7,
  kMultiWorldExecution = 8,
  kResourceExhausted = 9,
  kInternalInvariant = 10,
};

struct CpuCandidateAllocationSessionError {
  CpuCandidateAllocationSessionErrorCode code =
      CpuCandidateAllocationSessionErrorCode::kInvalidConfiguration;
  std::string_view invariant_id;
  std::string_view detail;
  std::optional<std::uint32_t> epoch_index;
  std::optional<TargetedRegenerationExecutionError::FailedExecutionObservation> failed_regeneration;
  // True after a nonempty atomic targeted publication, including when a
  // failed child observation proves that publication committed.
  bool candidate_store_publication_committed = false;

  friend bool operator==(const CpuCandidateAllocationSessionError&,
                         const CpuCandidateAllocationSessionError&) = default;
};

class CpuCandidateAllocationSession {
 public:
  CpuCandidateAllocationSession(const CpuCandidateAllocationSession&) = delete;
  CpuCandidateAllocationSession(CpuCandidateAllocationSession&&) noexcept = default;
  CpuCandidateAllocationSession& operator=(const CpuCandidateAllocationSession&) = delete;
  CpuCandidateAllocationSession& operator=(CpuCandidateAllocationSession&&) noexcept = delete;

  [[nodiscard]] const board_ir::BoardSnapshot& board() const noexcept { return board_; }
  [[nodiscard]] const MultiNetWorkload& workload() const noexcept { return workload_; }
  [[nodiscard]] const ResourceCapacityModel& capacities() const noexcept { return capacities_; }
  [[nodiscard]] const PreparedCpuCandidatePools& preparation() const noexcept { return prepared_; }
  [[nodiscard]] const CpuCandidateAllocationSessionConfig& config() const noexcept {
    return config_;
  }
  [[nodiscard]] CpuCandidateAllocationTerminalReason terminal_reason() const noexcept {
    return terminal_reason_;
  }
  [[nodiscard]] const CpuCandidateAllocationSessionCounters& counters() const noexcept {
    return counters_;
  }
  [[nodiscard]] const std::vector<CpuCandidateAllocationEpochRecord>& epochs() const noexcept {
    return epochs_;
  }
  [[nodiscard]] const std::vector<CandidatePool>& final_pools() const noexcept {
    return final_pools_;
  }
  [[nodiscard]] const NegotiatedPriceState& final_price_state() const noexcept {
    return final_price_state_;
  }
  [[nodiscard]] const OneWorldAllocation& final_single_world() const noexcept {
    return final_single_world_;
  }
  [[nodiscard]] const MultiWorldExecution& final_multi_world() const noexcept {
    return final_multi_world_;
  }
  [[nodiscard]] std::uint64_t final_pool_manifest_checksum() const noexcept {
    return final_pool_manifest_checksum_;
  }
  [[nodiscard]] std::uint64_t final_rejection_manifest_checksum() const noexcept {
    return final_rejection_manifest_checksum_;
  }
  [[nodiscard]] std::uint64_t session_checksum() const noexcept { return session_checksum_; }

 private:
  CpuCandidateAllocationSession(
      board_ir::BoardSnapshot board, MultiNetWorkload workload, ResourceCapacityModel capacities,
      PreparedCpuCandidatePools prepared, CpuCandidateAllocationSessionConfig config,
      CpuCandidateAllocationTerminalReason terminal_reason,
      CpuCandidateAllocationSessionCounters counters,
      std::vector<CpuCandidateAllocationEpochRecord> epochs, std::vector<CandidatePool> final_pools,
      NegotiatedPriceState final_price_state, OneWorldAllocation final_single_world,
      std::uint64_t final_pool_manifest_checksum, std::uint64_t final_rejection_manifest_checksum,
      std::uint64_t session_checksum, MultiWorldExecution final_multi_world) noexcept
      : board_(std::move(board)),
        workload_(std::move(workload)),
        capacities_(std::move(capacities)),
        prepared_(std::move(prepared)),
        config_(std::move(config)),
        terminal_reason_(terminal_reason),
        counters_(counters),
        epochs_(std::move(epochs)),
        final_pools_(std::move(final_pools)),
        final_price_state_(std::move(final_price_state)),
        final_single_world_(std::move(final_single_world)),
        final_pool_manifest_checksum_(final_pool_manifest_checksum),
        final_rejection_manifest_checksum_(final_rejection_manifest_checksum),
        session_checksum_(session_checksum),
        final_multi_world_(std::move(final_multi_world)) {}

  board_ir::BoardSnapshot board_;
  MultiNetWorkload workload_;
  ResourceCapacityModel capacities_;
  // Owns the CandidateStore. This member is declared before every lease-
  // bearing result so reverse destruction releases leases first.
  PreparedCpuCandidatePools prepared_;
  CpuCandidateAllocationSessionConfig config_;
  CpuCandidateAllocationTerminalReason terminal_reason_ =
      CpuCandidateAllocationTerminalReason::kRegenerationEpochLimit;
  CpuCandidateAllocationSessionCounters counters_;
  std::vector<CpuCandidateAllocationEpochRecord> epochs_;
  std::vector<CandidatePool> final_pools_;
  NegotiatedPriceState final_price_state_;
  OneWorldAllocation final_single_world_;
  std::uint64_t final_pool_manifest_checksum_ = 0;
  std::uint64_t final_rejection_manifest_checksum_ = 0;
  std::uint64_t session_checksum_ = 0;
  MultiWorldExecution final_multi_world_;

  friend std::variant<CpuCandidateAllocationSession, CpuCandidateAllocationSessionError>
  ExecuteCpuCandidateAllocationSession(std::uint32_t, board_ir::BoardSnapshot&&, MultiNetWorkload&&,
                                       ResourceCapacityModel&&, PreparedCpuCandidatePools&&,
                                       const CpuCandidateAllocationSessionConfig&);
  template <bool>
  friend std::variant<CpuCandidateAllocationSession, CpuCandidateAllocationSessionError>
  ExecuteCpuCandidateAllocationSessionImpl(std::uint32_t, board_ir::BoardSnapshot&&,
                                           MultiNetWorkload&&, ResourceCapacityModel&&,
                                           PreparedCpuCandidatePools&&,
                                           const CpuCandidateAllocationSessionConfig&,
                                           CpuCandidateAllocationSessionOperationalProfileV1*);
};

using CpuCandidateAllocationSessionResult =
    std::variant<CpuCandidateAllocationSession, CpuCandidateAllocationSessionError>;

// Consumes all inputs only on success so the result is lifetime-self-contained.
// On failure they remain caller-owned; when failed_regeneration says
// publication committed, the prepared store remains authoritative and must be
// retained or reconciled by that caller. Regeneration uses one common
// One-World lineage; the final pools are then frozen for one fixed-pool
// Multi-World execution.
[[nodiscard]] CpuCandidateAllocationSessionResult ExecuteCpuCandidateAllocationSession(
    std::uint32_t schema_version, board_ir::BoardSnapshot&& board, MultiNetWorkload&& workload,
    ResourceCapacityModel&& capacities, PreparedCpuCandidatePools&& prepared,
    const CpuCandidateAllocationSessionConfig& config);

}  // namespace apgar::allocator

#endif  // APGAR_ALLOCATOR_CPU_CANDIDATE_ALLOCATION_SESSION_H_
