#ifndef APGAR_ALLOCATOR_TARGETED_REGENERATION_EXECUTION_H_
#define APGAR_ALLOCATOR_TARGETED_REGENERATION_EXECUTION_H_

#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "apgar/allocator/targeted_regeneration.h"

namespace apgar::allocator {

inline constexpr std::uint32_t kTargetedRegenerationExecutionSchemaVersion = 1;

struct TargetedRegenerationExecutionConfig {
  std::uint64_t maximum_route_queries = 1'000'000;
  std::uint64_t maximum_route_work_units = 1'000'000'000;
  std::uint64_t maximum_policy_resource_entries = 100'000'000;
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
};

struct TargetedRegenerationColumnRecord {
  board_ir::EntityRef net{};
  std::uint64_t column_index = 0;
  std::uint64_t policy_identity = 0;
  std::uint64_t batch_identity = 0;
  std::uint64_t query_identity = 0;
  TargetedRegenerationColumnOutcome outcome = TargetedRegenerationColumnOutcome::kRouteDisconnected;
  std::optional<candidates::CandidateId> candidate_id;
  std::optional<std::uint64_t> candidate_payload_checksum;
  std::optional<candidates::CandidateRejectionCode> rejection_code;

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

  friend bool operator==(const TargetedRegenerationExecutionError&,
                         const TargetedRegenerationExecutionError&) = default;
};

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
};

using TargetedRegenerationExecutionResult =
    std::variant<TargetedRegenerationExecution, TargetedRegenerationExecutionError>;

// Executes one immutable plan against its exact source request. The workload,
// board, and CandidateStore must outlive the call and the returned execution;
// callers must serialize other mutations of the same store with this seam.
[[nodiscard]] TargetedRegenerationExecutionResult ExecuteTargetedRegenerationPlanCpu(
    std::uint32_t schema_version, const board_ir::BoardSnapshot& board,
    const OneWorldAllocationRequest& source_request, TargetedRegenerationPlan&& plan,
    candidates::CandidateStore& candidate_store, const TargetedRegenerationExecutionConfig& config);

}  // namespace apgar::allocator

#endif  // APGAR_ALLOCATOR_TARGETED_REGENERATION_EXECUTION_H_
