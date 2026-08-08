#ifndef APGAR_ALLOCATOR_CPU_CANDIDATE_ALLOCATION_SESSION_H_
#define APGAR_ALLOCATOR_CPU_CANDIDATE_ALLOCATION_SESSION_H_

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <variant>
#include <vector>

#include "apgar/allocator/cpu_targeted_regeneration_epoch.h"
#include "apgar/allocator/negotiated_regeneration_plan.h"
#include "apgar/allocator/one_world_selection.h"
#include "apgar/allocator/resource_accounting.h"
#include "apgar/board_ir/board.h"
#include "apgar/candidates/candidate_store.h"

namespace apgar::allocator {

inline constexpr std::uint64_t kMaximumCpuCandidateAllocationSessionEpochs = 1'000'000;
inline constexpr std::uint64_t kMaximumCpuCandidateAllocationSessionCandidateVisits = 100'000'000;
inline constexpr std::uint64_t kMaximumCpuCandidateAllocationSessionTargets = 100'000'000;
inline constexpr std::uint64_t kMaximumCpuCandidateAllocationSessionPriceProjectionVisits =
    1'000'000'000'000ULL;
inline constexpr std::uint64_t kMaximumCpuCandidateAllocationSessionTransactionItems = 200'000'000;
inline constexpr std::uint64_t kMaximumCpuCandidateAllocationSessionBytes = 1ULL << 50U;
inline constexpr std::uint64_t kMaximumCpuCandidateAllocationSessionWorkUnits =
    1'000'000'000'000'000'000ULL;

struct CpuCandidateAllocationSessionLimits {
  std::uint64_t maximum_executed_epochs = 64;
  std::uint64_t maximum_cumulative_source_candidate_visits = 8'388'608;
  std::uint64_t maximum_cumulative_source_candidate_bytes = 16ULL * 1024ULL * 1024ULL * 1024ULL;
  std::uint64_t maximum_cumulative_targets = 65'536;
  std::uint64_t maximum_cumulative_price_projection_visits = 6'400'000'000ULL;
  std::uint64_t maximum_reserved_cpu_work_units = 64'000'000'000ULL;
  std::uint64_t maximum_reserved_generated_bytes = 16ULL * 1024ULL * 1024ULL * 1024ULL;
  std::uint64_t maximum_cumulative_transaction_items = 8'454'144;

  friend bool operator==(const CpuCandidateAllocationSessionLimits&,
                         const CpuCandidateAllocationSessionLimits&) = default;
};

struct CpuCandidateAllocationSessionConfig {
  CpuTargetedRegenerationEpochConfig epoch;
  CpuCandidateAllocationSessionLimits limits;

  friend bool operator==(const CpuCandidateAllocationSessionConfig&,
                         const CpuCandidateAllocationSessionConfig&) = default;
};

enum class CpuCandidateAllocationStopReason : std::uint8_t {
  kFixedPoint = 0,
  kEpochBoundExhausted = 1,
  kSessionBoundExhausted = 2,
};

struct CpuCandidateAllocationStop {
  CpuCandidateAllocationStopReason reason = CpuCandidateAllocationStopReason::kFixedPoint;
  std::string_view invariant_id;
  std::string_view detail;
  std::optional<std::uint64_t> expected_value;
  std::optional<std::uint64_t> actual_value;
  std::optional<NegotiatedRegenerationPlanErrorCode> plan_error_code;
  std::optional<CpuTargetedRegenerationEpochErrorCode> epoch_error_code;

  friend bool operator==(const CpuCandidateAllocationStop&,
                         const CpuCandidateAllocationStop&) = default;
};

struct CpuCandidateAllocationSessionCounters {
  std::uint64_t planning_steps = 0;
  std::uint64_t executed_epochs = 0;
  std::uint64_t source_candidate_visits = 0;
  std::uint64_t source_candidate_bytes = 0;
  std::uint64_t target_count = 0;
  std::uint64_t route_query_count = 0;
  std::uint64_t price_projection_visits = 0;
  std::uint64_t reserved_cpu_work_units = 0;
  std::uint64_t actual_cpu_work_units = 0;
  std::uint64_t reserved_generated_bytes = 0;
  std::uint64_t actual_generated_bytes = 0;
  std::uint64_t transaction_items = 0;
  std::uint64_t admitted_columns = 0;
  std::uint64_t duplicate_columns = 0;
  std::uint64_t rejected_columns = 0;

  friend bool operator==(const CpuCandidateAllocationSessionCounters&,
                         const CpuCandidateAllocationSessionCounters&) = default;
};

struct CpuCandidateAllocationStep {
  std::uint64_t input_pool_identity = 0;
  std::uint64_t plan_identity = 0;
  std::uint64_t prior_snapshot_identity = 0;
  std::uint64_t price_snapshot_identity = 0;
  std::uint64_t price_epoch_index = 0;
  NegotiatedRegenerationDisposition disposition =
      NegotiatedRegenerationDisposition::kNoRegenerationRequired;
  std::uint64_t target_count = 0;
  std::optional<std::uint64_t> batch_identity;
  std::optional<std::uint64_t> epoch_identity;
  std::optional<std::uint64_t> output_pool_identity;
  CpuTargetedRegenerationEpochCounters epoch_counters;

  friend bool operator==(const CpuCandidateAllocationStep&,
                         const CpuCandidateAllocationStep&) = default;
};

class CpuCandidateAllocationPool {
 public:
  CpuCandidateAllocationPool(CpuCandidateAllocationPool&&) noexcept = default;
  CpuCandidateAllocationPool& operator=(CpuCandidateAllocationPool&&) noexcept = default;
  CpuCandidateAllocationPool(const CpuCandidateAllocationPool&) = delete;
  CpuCandidateAllocationPool& operator=(const CpuCandidateAllocationPool&) = delete;

  [[nodiscard]] board_ir::EntityRef net() const noexcept { return net_; }
  [[nodiscard]] std::span<const candidates::StoredCandidate> candidates() const noexcept {
    return candidates_;
  }
  [[nodiscard]] OneWorldCandidatePool one_world_pool() const noexcept {
    return OneWorldCandidatePool{.net = net_, .candidates = one_world_candidates_};
  }
  [[nodiscard]] CpuTargetedRegenerationSourcePool source_pool() const noexcept {
    return CpuTargetedRegenerationSourcePool{.net = net_, .candidates = candidates_};
  }

 private:
  explicit CpuCandidateAllocationPool(board_ir::EntityRef net) : net_(net) {}

  board_ir::EntityRef net_;
  std::vector<candidates::StoredCandidate> candidates_;
  std::vector<const candidates::RouteCandidate*> one_world_candidates_;

  friend struct CpuCandidateAllocationSessionFactory;
};

class CpuCandidateAllocationSession {
 public:
  CpuCandidateAllocationSession(CpuCandidateAllocationSession&&) noexcept = default;
  CpuCandidateAllocationSession& operator=(CpuCandidateAllocationSession&&) noexcept = default;
  CpuCandidateAllocationSession(const CpuCandidateAllocationSession&) = delete;
  CpuCandidateAllocationSession& operator=(const CpuCandidateAllocationSession&) = delete;

  [[nodiscard]] std::span<const CpuCandidateAllocationPool> pools() const noexcept {
    return pools_;
  }
  [[nodiscard]] const OneWorldSelection& final_selection() const noexcept {
    return final_selection_;
  }
  [[nodiscard]] const std::optional<NegotiatedRegenerationPlan>& terminal_plan() const noexcept {
    return terminal_plan_;
  }
  [[nodiscard]] std::span<const CpuCandidateAllocationStep> steps() const noexcept {
    return steps_;
  }
  [[nodiscard]] const CpuCandidateAllocationSessionCounters& counters() const noexcept {
    return counters_;
  }
  [[nodiscard]] const CpuCandidateAllocationStop& stop() const noexcept { return stop_; }
  [[nodiscard]] std::uint64_t initial_pool_identity() const noexcept {
    return initial_pool_identity_;
  }
  [[nodiscard]] std::uint64_t final_pool_identity() const noexcept { return final_pool_identity_; }
  [[nodiscard]] std::uint64_t session_identity() const noexcept { return session_identity_; }

 private:
  CpuCandidateAllocationSession() = default;

  std::vector<CpuCandidateAllocationPool> pools_;
  OneWorldSelection final_selection_;
  std::optional<NegotiatedRegenerationPlan> terminal_plan_;
  std::vector<CpuCandidateAllocationStep> steps_;
  CpuCandidateAllocationSessionCounters counters_;
  CpuCandidateAllocationStop stop_;
  std::uint64_t initial_pool_identity_ = 0;
  std::uint64_t final_pool_identity_ = 0;
  std::uint64_t session_identity_ = 0;

  friend struct CpuCandidateAllocationSessionFactory;
};

enum class CpuCandidateAllocationSessionErrorCode : std::uint8_t {
  kInvalidConfiguration = 0,
  kInvalidInput = 1,
  kAssociationMismatch = 2,
  kPlanningFailure = 3,
  kEpochFailure = 4,
  kArithmeticOverflow = 5,
  kResourceExhausted = 6,
  kInternalInvariant = 7,
};

struct CpuCandidateAllocationSessionError {
  CpuCandidateAllocationSessionErrorCode code =
      CpuCandidateAllocationSessionErrorCode::kInvalidConfiguration;
  std::string_view invariant_id;
  std::string_view detail;
  std::optional<board_ir::EntityRef> net;
  std::optional<std::uint64_t> expected_value;
  std::optional<std::uint64_t> actual_value;
  std::optional<NegotiatedRegenerationPlanError> plan_error;
  std::optional<CpuTargetedRegenerationEpochError> epoch_error;

  friend bool operator==(const CpuCandidateAllocationSessionError&,
                         const CpuCandidateAllocationSessionError&) = default;
};

using CpuCandidateAllocationSessionResult =
    std::variant<CpuCandidateAllocationSession, CpuCandidateAllocationSessionError>;

// Composes replay-linked P4R-06/P4R-07 steps until a deterministic no-target
// fixed point or a typed bounded stop. A non-bound failure exposes no session
// or completed prefix.
[[nodiscard]] CpuCandidateAllocationSessionResult RunCpuCandidateAllocationSession(
    const board_ir::BoardSnapshot& board, const ResourceCapacityModel& capacities,
    std::span<const CpuTargetedRegenerationSourcePool> source_pools,
    std::span<const CpuTargetedRegenerationNetContext> contexts,
    CpuCandidateAllocationSessionConfig config = {});

}  // namespace apgar::allocator

#endif  // APGAR_ALLOCATOR_CPU_CANDIDATE_ALLOCATION_SESSION_H_
