#ifndef APGAR_ALLOCATOR_SEQUENTIAL_NEGOTIATED_BASELINE_H_
#define APGAR_ALLOCATOR_SEQUENTIAL_NEGOTIATED_BASELINE_H_

#include <cstdint>
#include <optional>
#include <string_view>
#include <variant>
#include <vector>

#include "apgar/allocator/negotiated_prices.h"
#include "apgar/routing/cpu_astar.h"

namespace apgar::allocator {

inline constexpr std::uint32_t kSequentialNegotiatedBaselineSchemaVersion = 1;
inline constexpr std::uint32_t kMaximumSequentialNegotiatedSweepsV1 = 1'000'000;
inline constexpr std::uint64_t kMaximumSequentialNegotiatedQueriesV1 = 1'000'000'000ULL;
inline constexpr std::uint64_t kMaximumSequentialNegotiatedAggregateWorkV1 =
    1'000'000'000'000'000ULL;
inline constexpr std::uint64_t kSequentialNegotiatedColumnTraceBytesV1 = 512;
inline constexpr std::uint64_t kSequentialNegotiatedSweepTraceBytesV1 = 256;
inline constexpr std::uint64_t kMaximumSequentialNegotiatedRejectionBytesV1 = 16'384;

struct SequentialNegotiatedBaselineLimits {
  std::uint64_t maximum_nets = 100'000;
  std::uint64_t maximum_route_queries = 10'000'000;
  std::uint64_t maximum_total_route_work_units = 100'000'000'000'000ULL;
  std::uint64_t maximum_policy_projection_visits = 100'000'000'000ULL;
  std::uint64_t maximum_policy_resource_entries = 100'000'000'000ULL;
  std::uint64_t maximum_expanded_resource_visits = 100'000'000'000ULL;
  std::uint64_t maximum_occupancy_resource_records = 700'000;
  std::uint64_t maximum_candidate_draft_bytes = 256ULL * 1024ULL * 1024ULL;
  std::uint64_t maximum_retained_candidate_bytes = 64ULL * 1024ULL * 1024ULL * 1024ULL;
  std::uint64_t maximum_retained_rejection_bytes = 1024ULL * 1024ULL * 1024ULL;
  std::uint64_t maximum_trace_bytes = 1024ULL * 1024ULL * 1024ULL;

  friend bool operator==(const SequentialNegotiatedBaselineLimits&,
                         const SequentialNegotiatedBaselineLimits&) = default;
};

struct SequentialNegotiatedBaselineConfig {
  std::uint32_t schema_version = kSequentialNegotiatedBaselineSchemaVersion;
  std::uint64_t deterministic_seed = 0;
  std::uint64_t intrinsic_cost_weight = 1;
  std::uint32_t maximum_sweeps = 16;
  routing::CpuRouteWorkLimits route_limits{
      .maximum_work_units = 20'000'000,
      .maximum_record_count = 1'000'000,
      .maximum_queue_size = 8'000'000,
      .maximum_reconstruction_states = 100'000,
  };
  NegotiatedPriceConfig price_config{
      .present_step_per_overuse_unit = 1,
      .history_step_per_overuse_unit = 1,
      .maximum_price_per_resource = 1'000'000'000,
      .maximum_iterations = 10'000,
      .maximum_price_records = 250'000,
  };
  candidates::CandidateStoreConfig admission_store_config{
      .maximum_candidates_per_net = 1,
      .maximum_candidate_bytes_per_net = 256ULL * 1024ULL * 1024ULL,
      .maximum_rejection_records = 1,
      .maximum_rejection_items_per_transaction = 1,
      .maximum_admission_items_per_transaction = 1,
      .maximum_admission_input_bytes_per_transaction = 512ULL * 1024ULL * 1024ULL,
      .maximum_admission_work_units_per_transaction = 1'000'000'000'000ULL,
      .maximum_pin_lease_items_per_transaction = 1,
      .maximum_expected_pools_per_invocation = 1,
      .maximum_expected_candidates_per_invocation = 1,
  };
  OneWorldAllocatorLimits allocator_limits;
  SequentialNegotiatedBaselineLimits limits;
  std::uint64_t known_unmapped_exact_conflict_count = 0;

  friend bool operator==(const SequentialNegotiatedBaselineConfig&,
                         const SequentialNegotiatedBaselineConfig&) = default;
};

enum class SequentialNegotiatedColumnOutcome : std::uint8_t {
  kAdmitted = 0,
  kRouteDisconnected = 1,
  kRouteUnsupported = 2,
  kBuildRejected = 3,
  kAdmissionRejected = 4,
};

struct SequentialNegotiatedColumnRecord {
  std::uint32_t sweep_index = 0;
  board_ir::EntityRef net{};
  std::uint64_t policy_identity = 0;
  std::uint64_t batch_identity = 0;
  std::uint64_t query_identity = 0;
  std::uint64_t route_work_units = 0;
  std::uint64_t prospective_congestion_resource_count = 0;
  std::uint64_t total_prospective_congestion_penalty = 0;
  SequentialNegotiatedColumnOutcome outcome = SequentialNegotiatedColumnOutcome::kRouteDisconnected;
  bool retained_prior_candidate = false;
  std::optional<candidates::CandidateId> candidate_id;
  std::optional<std::uint64_t> candidate_payload_checksum;
  std::optional<std::uint64_t> candidate_semantic_checksum;
  std::optional<candidates::CandidateRejection> rejection;

  friend bool operator==(const SequentialNegotiatedColumnRecord&,
                         const SequentialNegotiatedColumnRecord&) = default;
};

struct SequentialNegotiatedSweepRecord {
  std::uint32_t sweep_index = 0;
  std::uint32_t source_price_iteration = 0;
  std::uint64_t source_price_state_checksum = 0;
  std::uint64_t successor_price_state_checksum = 0;
  std::uint64_t world_checksum = 0;
  std::uint64_t first_query_identity = 0;
  std::uint64_t route_query_count = 0;
  std::uint64_t selected_net_count = 0;
  std::uint64_t no_candidate_net_count = 0;
  std::uint64_t overused_resource_count = 0;
  std::uint64_t total_overuse_units = 0;
  std::uint64_t total_intrinsic_cost = 0;
  bool winners_unchanged = false;
  bool price_values_unchanged = false;

  friend bool operator==(const SequentialNegotiatedSweepRecord&,
                         const SequentialNegotiatedSweepRecord&) = default;
};

enum class SequentialNegotiatedTerminalReason : std::uint8_t {
  kFeasible = 0,
  kNoAdmissibleCandidate = 1,
  kFixedPointStalled = 2,
  kSweepBudgetExhausted = 3,
};

struct SequentialNegotiatedBaselineCounters {
  std::uint64_t completed_sweeps = 0;
  std::uint64_t route_queries = 0;
  std::uint64_t route_work_units = 0;
  std::uint64_t successful_routes = 0;
  std::uint64_t admitted_candidates = 0;
  std::uint64_t rejected_routes = 0;
  std::uint64_t restored_prior_routes = 0;
  std::uint64_t policy_projection_visits = 0;
  std::uint64_t policy_resource_entries = 0;
  std::uint64_t expanded_resource_visits = 0;
  std::uint64_t price_updates = 0;
  std::uint64_t peak_retained_candidate_bytes = 0;
  std::uint64_t retained_rejection_bytes = 0;

  friend bool operator==(const SequentialNegotiatedBaselineCounters&,
                         const SequentialNegotiatedBaselineCounters&) = default;
};

// Diagnostic-only wall intervals for one separately instrumented traditional
// baseline execution. The ordinary Raw path supplies no profile pointer and
// therefore executes no profiling clocks.
struct SequentialNegotiatedBaselineOperationalProfileV1 {
  std::uint64_t component_wall_nanoseconds = 0;
  std::uint64_t validation_and_initialization_wall_nanoseconds = 0;
  std::uint64_t scheduling_and_policy_projection_wall_nanoseconds = 0;
  std::uint64_t candidate_generation_wall_nanoseconds = 0;
  std::uint64_t exact_admission_and_store_publication_wall_nanoseconds = 0;
  std::uint64_t incremental_resource_accumulation_wall_nanoseconds = 0;
  std::uint64_t sweep_selection_and_resource_replay_wall_nanoseconds = 0;
  std::uint64_t price_update_wall_nanoseconds = 0;
  std::uint64_t final_assembly_wall_nanoseconds = 0;
  std::uint64_t unclassified_serial_wall_nanoseconds = 0;

  friend bool operator==(const SequentialNegotiatedBaselineOperationalProfileV1&,
                         const SequentialNegotiatedBaselineOperationalProfileV1&) = default;
};

enum class SequentialNegotiatedBaselineErrorCode : std::uint8_t {
  kUnsupportedSchema = 0,
  kInvalidConfiguration = 1,
  kAssociationMismatch = 2,
  kInputBoundExceeded = 3,
  kWorkBoundExceeded = 4,
  kPolicySynthesis = 5,
  kCandidateGeneration = 6,
  kCandidateAdmission = 7,
  kAllocation = 8,
  kPriceUpdate = 9,
  kResourceRefinementRequired = 10,
  kResourceExhausted = 11,
  kInternalInvariant = 12,
};

struct SequentialNegotiatedBaselineError {
  SequentialNegotiatedBaselineErrorCode code =
      SequentialNegotiatedBaselineErrorCode::kInvalidConfiguration;
  std::string_view invariant_id;
  std::string_view detail;
  std::optional<board_ir::EntityRef> net;
  std::uint64_t required = 0;
  std::uint64_t configured = 0;

  friend bool operator==(const SequentialNegotiatedBaselineError&,
                         const SequentialNegotiatedBaselineError&) = default;
};

class SequentialNegotiatedBaselineResult {
 public:
  SequentialNegotiatedBaselineResult(const SequentialNegotiatedBaselineResult&) = delete;
  SequentialNegotiatedBaselineResult(SequentialNegotiatedBaselineResult&&) noexcept = default;
  SequentialNegotiatedBaselineResult& operator=(const SequentialNegotiatedBaselineResult&) = delete;
  SequentialNegotiatedBaselineResult& operator=(SequentialNegotiatedBaselineResult&&) noexcept =
      default;

  [[nodiscard]] const SequentialNegotiatedBaselineConfig& config() const noexcept {
    return config_;
  }
  [[nodiscard]] std::uint64_t batch_identity() const noexcept { return batch_identity_; }
  [[nodiscard]] std::uint64_t workload_checksum() const noexcept { return workload_checksum_; }
  [[nodiscard]] std::uint64_t capacity_model_checksum() const noexcept {
    return capacity_model_checksum_;
  }
  [[nodiscard]] SequentialNegotiatedTerminalReason terminal_reason() const noexcept {
    return terminal_reason_;
  }
  [[nodiscard]] const SequentialNegotiatedBaselineCounters& counters() const noexcept {
    return counters_;
  }
  [[nodiscard]] const std::vector<SequentialNegotiatedColumnRecord>& columns() const noexcept {
    return columns_;
  }
  [[nodiscard]] const std::vector<SequentialNegotiatedSweepRecord>& sweeps() const noexcept {
    return sweeps_;
  }
  [[nodiscard]] const std::vector<CandidatePool>& final_pools() const noexcept {
    return final_pools_;
  }
  [[nodiscard]] const OneWorldAllocation& final_world() const noexcept { return final_world_; }
  [[nodiscard]] const NegotiatedPriceState& successor_price_state() const noexcept {
    return successor_price_state_;
  }
  [[nodiscard]] std::uint64_t session_checksum() const noexcept { return session_checksum_; }

 private:
  SequentialNegotiatedBaselineResult(SequentialNegotiatedBaselineConfig config,
                                     std::uint64_t batch_identity, std::uint64_t workload_checksum,
                                     std::uint64_t capacity_model_checksum,
                                     SequentialNegotiatedTerminalReason terminal_reason,
                                     SequentialNegotiatedBaselineCounters counters,
                                     std::vector<SequentialNegotiatedColumnRecord> columns,
                                     std::vector<SequentialNegotiatedSweepRecord> sweeps,
                                     std::vector<CandidatePool> final_pools,
                                     OneWorldAllocation final_world,
                                     NegotiatedPriceState successor_price_state,
                                     std::uint64_t session_checksum) noexcept;

  SequentialNegotiatedBaselineConfig config_;
  std::uint64_t batch_identity_ = 0;
  std::uint64_t workload_checksum_ = 0;
  std::uint64_t capacity_model_checksum_ = 0;
  SequentialNegotiatedTerminalReason terminal_reason_ =
      SequentialNegotiatedTerminalReason::kSweepBudgetExhausted;
  SequentialNegotiatedBaselineCounters counters_;
  std::vector<SequentialNegotiatedColumnRecord> columns_;
  std::vector<SequentialNegotiatedSweepRecord> sweeps_;
  std::vector<CandidatePool> final_pools_;
  OneWorldAllocation final_world_;
  NegotiatedPriceState successor_price_state_;
  std::uint64_t session_checksum_ = 0;

  friend std::variant<SequentialNegotiatedBaselineResult, SequentialNegotiatedBaselineError>
  ExecuteSequentialNegotiatedBaseline(const board_ir::BoardSnapshot&, const MultiNetWorkload&,
                                      const ResourceCapacityModel&,
                                      const SequentialNegotiatedBaselineConfig&);
  template <bool>
  friend std::variant<SequentialNegotiatedBaselineResult, SequentialNegotiatedBaselineError>
  ExecuteSequentialNegotiatedBaselineImpl(const board_ir::BoardSnapshot&, const MultiNetWorkload&,
                                          const ResourceCapacityModel&,
                                          const SequentialNegotiatedBaselineConfig&,
                                          SequentialNegotiatedBaselineOperationalProfileV1*);
};

using SequentialNegotiatedBaselineExecution =
    std::variant<SequentialNegotiatedBaselineResult, SequentialNegotiatedBaselineError>;

// Deterministic traditional net->route->commit baseline. Each sweep processes
// canonical workload order, rips up a net's prior winner, freezes one immutable
// prospective congestion policy, runs bounded production CPU A*, exact-admits
// at most one replacement, then commits that replacement before the next net.
[[nodiscard]] SequentialNegotiatedBaselineExecution ExecuteSequentialNegotiatedBaseline(
    const board_ir::BoardSnapshot& board, const MultiNetWorkload& workload,
    const ResourceCapacityModel& capacities, const SequentialNegotiatedBaselineConfig& config);

[[nodiscard]] SequentialNegotiatedBaselineExecution
ExecuteSequentialNegotiatedBaselineWithOperationalProfileV1(
    const board_ir::BoardSnapshot& board, const MultiNetWorkload& workload,
    const ResourceCapacityModel& capacities, const SequentialNegotiatedBaselineConfig& config,
    SequentialNegotiatedBaselineOperationalProfileV1& operational_profile);

}  // namespace apgar::allocator

#endif  // APGAR_ALLOCATOR_SEQUENTIAL_NEGOTIATED_BASELINE_H_
