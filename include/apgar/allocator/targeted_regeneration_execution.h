#ifndef APGAR_ALLOCATOR_TARGETED_REGENERATION_EXECUTION_H_
#define APGAR_ALLOCATOR_TARGETED_REGENERATION_EXECUTION_H_

#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "apgar/allocator/targeted_regeneration.h"
#include "apgar/routing/cpu_astar.h"

namespace apgar::allocator {

inline constexpr std::uint32_t kTargetedRegenerationExecutionSchemaVersionV1 = 1;
inline constexpr std::uint32_t kTargetedRegenerationExecutionSchemaVersionV2 = 2;
inline constexpr std::uint32_t kTargetedRegenerationExecutionSchemaVersion = 3;
inline constexpr std::uint64_t kMaximumTargetedRegenerationRouteQueriesV2 = 1'000'000;
inline constexpr std::uint64_t kMaximumTargetedRegenerationRouteWorkUnitsV2 =
    1'000'000'000'000'000ULL;
inline constexpr std::uint64_t kTargetedRegenerationPolicyProjectionPassesV2 = 4;
inline constexpr std::uint64_t kMaximumTargetedRegenerationPolicyProjectionVisitsV2 =
    1'000'000'000'000'000ULL;
inline constexpr std::uint64_t kMaximumTargetedRegenerationRejectionLogicalBytesV2 = 4'096;
inline constexpr std::uint64_t kTargetedRegenerationColumnBaseLogicalBytesV2 = 256;

struct TargetedRegenerationExecutionConfig {
  // Caller-rooted seed mixed into every derived batch and policy identity.
  std::uint64_t deterministic_seed = 0;
  std::uint64_t maximum_route_queries = 1'000'000;
  routing::CpuRouteWorkLimits route_limits{
      .maximum_work_units = 20'000'000,
      .maximum_record_count = 1'000'000,
      .maximum_queue_size = 8'000'000,
      .maximum_reconstruction_states = 100'000,
  };
  std::uint64_t maximum_total_route_work_units = 100'000'000'000ULL;
  std::uint64_t maximum_policy_projection_visits = 100'000'000;
  std::uint64_t maximum_policy_resource_entries = 100'000'000;
  std::uint64_t maximum_candidate_draft_bytes = 64ULL * 1024ULL * 1024ULL;
  std::uint64_t maximum_generated_candidate_bytes = 64ULL * 1024ULL * 1024ULL * 1024ULL;
  std::uint64_t maximum_rejection_bytes = 4ULL * 1024ULL * 1024ULL * 1024ULL;
  std::uint64_t maximum_transient_result_bytes = 16ULL * 1024ULL * 1024ULL * 1024ULL;
  // A nonzero value declares that the current allocator resource vocabulary
  // cannot represent known exact combined-route conflicts. Execution returns
  // an explicit refinement-required terminal result without generating or
  // publishing columns.
  std::uint64_t known_unmapped_exact_conflict_count = 0;

  friend bool operator==(const TargetedRegenerationExecutionConfig&,
                         const TargetedRegenerationExecutionConfig&) = default;
};

enum class TargetedRegenerationColumnOutcome : std::uint8_t {
  kAdmitted = 0,
  kDuplicate = 1,
  kRouteDisconnected = 2,
  kRouteUnsupported = 3,
  kBuildRejected = 4,
  kAdmissionRejected = 5,
  // Failure-observation-only stages. Successful executions finalize every
  // column to one of the six outcomes above before returning.
  kQueryInFlight = 6,
  kBuildInFlight = 7,
  kGeneratedPendingPublication = 8,
  kRejectionEvidenceInFlight = 9,
  kPublicationCommittedOutcomeCorrelationPending = 10,
};
static_assert(
    static_cast<std::uint8_t>(TargetedRegenerationColumnOutcome::kRejectionEvidenceInFlight) == 9);
static_assert(
    static_cast<std::uint8_t>(
        TargetedRegenerationColumnOutcome::kPublicationCommittedOutcomeCorrelationPending) == 10);

struct TargetedRegenerationColumnRecord {
  board_ir::EntityRef net{};
  std::uint64_t column_index = 0;
  std::uint64_t policy_identity = 0;
  std::uint64_t batch_identity = 0;
  std::uint64_t query_identity = 0;
  std::optional<routing::CpuRouteTelemetry> route_telemetry;
  std::uint64_t candidate_draft_logical_bytes = 0;
  TargetedRegenerationColumnOutcome outcome = TargetedRegenerationColumnOutcome::kQueryInFlight;
  std::optional<candidates::CandidateId> candidate_id;
  std::optional<std::uint64_t> candidate_payload_checksum;
  std::optional<candidates::CandidateRejectionCode> rejection_code;
  std::optional<candidates::CandidateRejection> rejection;

  friend bool operator==(const TargetedRegenerationColumnRecord&,
                         const TargetedRegenerationColumnRecord&) = default;
};

enum class TargetedRegenerationExecutionDisposition : std::uint8_t {
  kNoWork = 0,
  kProgress = 1,
  kStalled = 2,
  kResourceRefinementRequired = 3,
};

enum class TargetedRegenerationTerminalReason : std::uint8_t {
  kFeasible = 0,
  kLexicographicallyImproved = 1,
  kNoTargets = 2,
  kNoSuccessfulGeneration = 3,
  kNoNovelRetainedColumns = 4,
  kNoSelectionChange = 5,
  kSelectionChangedWithoutObjectiveImprovement = 6,
  kResourceRefinementRequired = 7,
};

struct TargetedRegenerationExecutionCounters {
  std::uint64_t requested_columns = 0;
  std::uint64_t route_queries = 0;
  std::uint64_t route_work_units = 0;
  std::uint64_t policy_projection_visits = 0;
  std::uint64_t peak_route_record_count = 0;
  std::uint64_t peak_route_queue_size = 0;
  std::uint64_t generated_candidate_bytes = 0;
  std::uint64_t rejection_record_bytes = 0;
  std::uint64_t transient_result_bytes = 0;
  std::uint64_t successful_routes = 0;
  std::uint64_t built_candidates = 0;
  std::uint64_t admitted_candidates = 0;
  std::uint64_t duplicate_candidates = 0;
  std::uint64_t rejected_columns = 0;
  std::uint64_t novel_retained_candidates = 0;
  std::uint64_t changed_selections = 0;
  std::uint64_t successor_pinned_candidates = 0;

  friend bool operator==(const TargetedRegenerationExecutionCounters&,
                         const TargetedRegenerationExecutionCounters&) = default;
};

enum class TargetedRegenerationExecutionErrorCode : std::uint8_t {
  kUnsupportedSchema = 0,
  kInvalidConfiguration = 1,
  kInactivePlanLease = 2,
  kWrongCandidateStore = 3,
  kAssociationMismatch = 4,
  kSourceRequestDrift = 5,
  kPlanInvariant = 6,
  kWorkBoundExceeded = 7,
  kPolicySynthesis = 8,
  kCandidateGeneration = 9,
  kCandidateStore = 10,
  kAllocation = 11,
  kSuccessorLease = 12,
  kResourceExhausted = 13,
  kInternalInvariant = 14,
};

struct TargetedRegenerationExecutionError {
  TargetedRegenerationExecutionErrorCode code =
      TargetedRegenerationExecutionErrorCode::kInvalidConfiguration;
  std::string_view invariant_id;
  std::string_view detail;
  struct FailedExecutionObservation {
    std::uint32_t schema_version = kTargetedRegenerationExecutionSchemaVersion;
    std::uint64_t plan_checksum = 0;
    TargetedRegenerationExecutionConfig config;
    candidates::CandidateStoreConfig store_config;
    // False means the CandidateStore remains at the pre-invocation snapshot.
    // True means atomic candidate publication completed before this later
    // execution failure; callers must retain or explicitly reconcile it.
    bool candidate_store_publication_committed = false;
    TargetedRegenerationExecutionCounters counters;
    std::vector<TargetedRegenerationColumnRecord> columns;
    std::uint64_t observation_checksum = 0;

    friend bool operator==(const FailedExecutionObservation&,
                           const FailedExecutionObservation&) = default;
  };
  std::optional<FailedExecutionObservation> failed_execution;

  friend bool operator==(const TargetedRegenerationExecutionError&,
                         const TargetedRegenerationExecutionError&) = default;
};

struct TargetedRegenerationOperationalProfileV1;

class TargetedRegenerationExecution {
 public:
  TargetedRegenerationExecution(const TargetedRegenerationExecution&) = delete;
  TargetedRegenerationExecution(TargetedRegenerationExecution&&) noexcept = default;
  TargetedRegenerationExecution& operator=(const TargetedRegenerationExecution&) = delete;
  TargetedRegenerationExecution& operator=(TargetedRegenerationExecution&&) noexcept = default;

  [[nodiscard]] std::uint32_t schema_version() const noexcept { return schema_version_; }
  [[nodiscard]] const TargetedRegenerationPlan& plan() const noexcept { return plan_; }
  [[nodiscard]] const TargetedRegenerationExecutionConfig& config() const noexcept {
    return config_;
  }
  [[nodiscard]] const candidates::CandidateStoreConfig& store_config() const noexcept {
    return store_config_;
  }
  [[nodiscard]] TargetedRegenerationExecutionDisposition disposition() const noexcept {
    return disposition_;
  }
  [[nodiscard]] TargetedRegenerationTerminalReason terminal_reason() const noexcept {
    return terminal_reason_;
  }
  [[nodiscard]] const TargetedRegenerationExecutionCounters& counters() const noexcept {
    return counters_;
  }
  [[nodiscard]] const std::vector<TargetedRegenerationColumnRecord>& columns() const noexcept {
    return columns_;
  }
  [[nodiscard]] const std::vector<CandidatePool>& refreshed_pools() const noexcept {
    return refreshed_pools_;
  }
  [[nodiscard]] const OneWorldAllocation& baseline_world() const noexcept {
    return baseline_world_;
  }
  [[nodiscard]] const OneWorldAllocation& refreshed_world() const noexcept {
    return refreshed_world_;
  }
  [[nodiscard]] std::uint64_t execution_checksum() const noexcept { return execution_checksum_; }
  [[nodiscard]] bool has_active_successor_lease() const noexcept {
    return successor_lease_.has_value() && successor_lease_->active();
  }

 private:
  TargetedRegenerationExecution(std::uint32_t schema_version, TargetedRegenerationPlan plan,
                                TargetedRegenerationExecutionConfig config,
                                candidates::CandidateStoreConfig store_config,
                                TargetedRegenerationExecutionDisposition disposition,
                                TargetedRegenerationTerminalReason terminal_reason,
                                TargetedRegenerationExecutionCounters counters,
                                std::vector<TargetedRegenerationColumnRecord> columns,
                                std::vector<CandidatePool> refreshed_pools,
                                OneWorldAllocation baseline_world,
                                OneWorldAllocation refreshed_world,
                                std::uint64_t execution_checksum,
                                std::optional<candidates::CandidateStorePinLease> successor_lease)
      : schema_version_(schema_version),
        plan_(std::move(plan)),
        config_(config),
        store_config_(store_config),
        disposition_(disposition),
        terminal_reason_(terminal_reason),
        counters_(counters),
        columns_(std::move(columns)),
        refreshed_pools_(std::move(refreshed_pools)),
        baseline_world_(std::move(baseline_world)),
        refreshed_world_(std::move(refreshed_world)),
        execution_checksum_(execution_checksum),
        successor_lease_(std::move(successor_lease)) {}

  std::uint32_t schema_version_ = kTargetedRegenerationExecutionSchemaVersion;
  TargetedRegenerationPlan plan_;
  TargetedRegenerationExecutionConfig config_;
  candidates::CandidateStoreConfig store_config_;
  TargetedRegenerationExecutionDisposition disposition_ =
      TargetedRegenerationExecutionDisposition::kNoWork;
  TargetedRegenerationTerminalReason terminal_reason_ =
      TargetedRegenerationTerminalReason::kNoTargets;
  TargetedRegenerationExecutionCounters counters_;
  std::vector<TargetedRegenerationColumnRecord> columns_;
  std::vector<CandidatePool> refreshed_pools_;
  OneWorldAllocation baseline_world_;
  OneWorldAllocation refreshed_world_;
  std::uint64_t execution_checksum_ = 0;
  std::optional<candidates::CandidateStorePinLease> successor_lease_;

  friend std::variant<TargetedRegenerationExecution, TargetedRegenerationExecutionError>
  ExecuteTargetedRegenerationPlanCpu(std::uint32_t, const board_ir::BoardSnapshot&,
                                     const OneWorldAllocationRequest&, TargetedRegenerationPlan&&,
                                     candidates::CandidateStore&,
                                     const TargetedRegenerationExecutionConfig&);
  template <bool>
  friend std::variant<TargetedRegenerationExecution, TargetedRegenerationExecutionError>
  ExecuteTargetedRegenerationPlanCpuImpl(std::uint32_t, const board_ir::BoardSnapshot&,
                                         const OneWorldAllocationRequest&,
                                         TargetedRegenerationPlan&&, candidates::CandidateStore&,
                                         const TargetedRegenerationExecutionConfig&,
                                         TargetedRegenerationOperationalProfileV1*);
};

using TargetedRegenerationExecutionResult =
    std::variant<TargetedRegenerationExecution, TargetedRegenerationExecutionError>;

// Diagnostic-only wall intervals for one separately instrumented targeted
// regeneration. These values never participate in semantic or replay
// checksums, and the ordinary execution path supplies no profile pointer.
struct TargetedRegenerationOperationalProfileV1 {
  std::uint32_t epoch_index = 0;
  std::uint64_t plan_checksum = 0;
  std::uint64_t execution_checksum = 0;
  TargetedRegenerationExecutionCounters counters;
  std::uint64_t component_wall_nanoseconds = 0;
  std::uint64_t validation_and_preflight_wall_nanoseconds = 0;
  std::uint64_t baseline_selection_and_source_store_preflight_wall_nanoseconds = 0;
  std::uint64_t policy_projection_and_candidate_generation_wall_nanoseconds = 0;
  std::uint64_t exact_admission_and_store_publication_wall_nanoseconds = 0;
  std::uint64_t publication_correlation_wall_nanoseconds = 0;
  std::uint64_t refreshed_selection_and_resource_accumulation_wall_nanoseconds = 0;
  std::uint64_t successor_retention_wall_nanoseconds = 0;
  std::uint64_t final_assembly_wall_nanoseconds = 0;
  std::uint64_t unclassified_serial_wall_nanoseconds = 0;

  friend bool operator==(const TargetedRegenerationOperationalProfileV1&,
                         const TargetedRegenerationOperationalProfileV1&) = default;
};

// Executes one immutable plan against its exact source request. The workload,
// board, and CandidateStore must outlive the call and the returned execution;
// callers must serialize other mutations of the same store with this seam.
[[nodiscard]] TargetedRegenerationExecutionResult ExecuteTargetedRegenerationPlanCpu(
    std::uint32_t schema_version, const board_ir::BoardSnapshot& board,
    const OneWorldAllocationRequest& source_request, TargetedRegenerationPlan&& plan,
    candidates::CandidateStore& candidate_store, const TargetedRegenerationExecutionConfig& config);

[[nodiscard]] TargetedRegenerationExecutionResult
ExecuteTargetedRegenerationPlanCpuWithOperationalProfileV1(
    std::uint32_t schema_version, const board_ir::BoardSnapshot& board,
    const OneWorldAllocationRequest& source_request, TargetedRegenerationPlan&& plan,
    candidates::CandidateStore& candidate_store, const TargetedRegenerationExecutionConfig& config,
    TargetedRegenerationOperationalProfileV1& operational_profile);

}  // namespace apgar::allocator

#endif  // APGAR_ALLOCATOR_TARGETED_REGENERATION_EXECUTION_H_
