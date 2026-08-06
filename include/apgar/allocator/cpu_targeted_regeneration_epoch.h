#ifndef APGAR_ALLOCATOR_CPU_TARGETED_REGENERATION_EPOCH_H_
#define APGAR_ALLOCATOR_CPU_TARGETED_REGENERATION_EPOCH_H_

#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "apgar/allocator/negotiated_regeneration_plan.h"
#include "apgar/allocator/one_world_selection.h"
#include "apgar/board_ir/board.h"
#include "apgar/candidates/candidate_store.h"
#include "apgar/geometry_compiler/compiled_board.h"
#include "apgar/routing/cpu_astar.h"
#include "apgar/routing/planar_route.h"

namespace apgar::allocator {

inline constexpr std::uint64_t kMaximumCpuTargetedEpochNets = 1'000'000;
inline constexpr std::uint64_t kMaximumCpuTargetedEpochSourceCandidates = 1'000'000;
inline constexpr std::uint64_t kMaximumCpuTargetedEpochTargets = 1'000'000;
inline constexpr std::uint64_t kMaximumCpuTargetedEpochPriceProjectionVisits = 100'000'000;

// One retained prepared context for one complete source-pool net. The request
// supplies intrinsic route policy only; resource bans and penalties are
// rejected because the immutable P4R-06 snapshot is the epoch price authority.
struct CpuTargetedRegenerationNetContext {
  const geometry_compiler::CompiledBoard* compiled_board = nullptr;
  routing::PlanarRouteRequest request;
};

// One complete immutable source pool. Owning CandidateStore handles are
// required so the epoch can atomically rebuild self-contained post-publication
// pools without reconstructing or borrowing candidate payloads.
struct CpuTargetedRegenerationSourcePool {
  board_ir::EntityRef net;
  std::span<const candidates::StoredCandidate> candidates;
};

struct CpuTargetedRegenerationEpochLimits {
  std::uint64_t maximum_nets = 1'024;
  std::uint64_t maximum_source_candidates = 131'072;
  std::uint64_t maximum_source_candidate_bytes = 256U * 1024U * 1024U;
  std::uint64_t maximum_targets = 1'024;
  std::uint64_t maximum_price_projection_visits = 100'000'000;
  std::uint64_t maximum_price_entries_per_target = 1'000'000;
  std::uint64_t maximum_aggregate_price_entries = 100'000'000;
  std::uint64_t maximum_cpu_work_units_per_query = 100'000'000;
  std::uint64_t maximum_aggregate_cpu_work_units = 1'000'000'000;
  std::uint64_t maximum_generated_bytes_per_column = 4U * 1024U * 1024U;
  std::uint64_t maximum_aggregate_generated_bytes = 256U * 1024U * 1024U;
  std::uint64_t maximum_retained_candidate_bytes = 256U * 1024U * 1024U;
  candidates::CandidateStoreConfig candidate_store = {
      .maximum_candidates_per_net = 128,
      .maximum_candidate_bytes_per_net = 16U * 1024U * 1024U,
      .maximum_rejection_records = 131'072,
      .maximum_rejection_items_per_transaction = 131'072,
      .maximum_admission_items_per_transaction = 131'072,
      .maximum_admission_input_bytes_per_transaction = 256U * 1024U * 1024U,
      .maximum_admission_work_units_per_transaction = 1'000'000'000,
  };
  OneWorldSelectionLimits refreshed_selection = {
      .maximum_net_pools = 1'024,
      .maximum_total_candidates = 131'072,
      .accounting =
          ResourceAccountingLimits{
              .maximum_candidates = 1'024,
              .maximum_expanded_resource_uses = 100'000'000,
              .maximum_usage_units_per_resource = std::numeric_limits<std::uint64_t>::max(),
          },
  };

  friend bool operator==(const CpuTargetedRegenerationEpochLimits&,
                         const CpuTargetedRegenerationEpochLimits&) = default;
};

struct CpuTargetedRegenerationEpochConfig {
  NegotiatedRegenerationPlanConfig planning;
  CpuTargetedRegenerationEpochLimits limits;

  friend bool operator==(const CpuTargetedRegenerationEpochConfig&,
                         const CpuTargetedRegenerationEpochConfig&) = default;
};

enum class CpuTargetedRegenerationColumnOutcome : std::uint8_t {
  kAdmitted = 0,
  kDuplicate = 1,
  kRouteDisconnected = 2,
  kRouteUnsupported = 3,
  kCandidateBuildRejected = 4,
  kAdmissionRejected = 5,
};

struct CpuTargetedRegenerationColumn {
  board_ir::EntityRef net;
  std::uint64_t target_identity = 0;
  std::uint64_t policy_identity = 0;
  std::uint64_t query_identity = 0;
  std::uint32_t candidate_ordinal = 0;
  CpuTargetedRegenerationColumnOutcome outcome =
      CpuTargetedRegenerationColumnOutcome::kAdmissionRejected;
  std::optional<candidates::CandidateId> candidate_id;
  std::optional<routing::RouteFailureCode> route_failure_code;
  std::optional<candidates::CandidateRejection> rejection;
  std::uint64_t generated_bytes = 0;

  friend bool operator==(const CpuTargetedRegenerationColumn&,
                         const CpuTargetedRegenerationColumn&) = default;
};

struct CpuTargetedRegenerationEpochCounters {
  std::uint64_t source_candidate_count = 0;
  std::uint64_t source_candidate_bytes = 0;
  std::uint64_t target_count = 0;
  std::uint64_t route_query_count = 0;
  std::uint64_t price_projection_visits = 0;
  std::uint64_t projected_price_entries = 0;
  std::uint64_t cpu_work_units = 0;
  std::uint64_t generated_bytes = 0;
  std::uint64_t admitted_columns = 0;
  std::uint64_t duplicate_columns = 0;
  std::uint64_t rejected_columns = 0;
  std::uint64_t retained_candidate_count = 0;
  std::uint64_t retained_candidate_bytes = 0;

  friend bool operator==(const CpuTargetedRegenerationEpochCounters&,
                         const CpuTargetedRegenerationEpochCounters&) = default;
};

class CpuTargetedRegenerationPool {
 public:
  CpuTargetedRegenerationPool(CpuTargetedRegenerationPool&&) noexcept = default;
  CpuTargetedRegenerationPool& operator=(CpuTargetedRegenerationPool&&) noexcept = default;
  CpuTargetedRegenerationPool(const CpuTargetedRegenerationPool&) = delete;
  CpuTargetedRegenerationPool& operator=(const CpuTargetedRegenerationPool&) = delete;

  [[nodiscard]] board_ir::EntityRef net() const noexcept { return net_; }
  [[nodiscard]] std::span<const candidates::StoredCandidate> candidates() const noexcept {
    return candidates_;
  }
  [[nodiscard]] OneWorldCandidatePool one_world_pool() const noexcept {
    return OneWorldCandidatePool{.net = net_, .candidates = one_world_candidates_};
  }

 private:
  explicit CpuTargetedRegenerationPool(board_ir::EntityRef net) : net_(net) {}

  board_ir::EntityRef net_;
  std::vector<candidates::StoredCandidate> candidates_;
  std::vector<const candidates::RouteCandidate*> one_world_candidates_;

  friend struct CpuTargetedRegenerationEpochFactory;
};

class CpuTargetedRegenerationEpoch {
 public:
  CpuTargetedRegenerationEpoch(CpuTargetedRegenerationEpoch&&) noexcept = default;
  CpuTargetedRegenerationEpoch& operator=(CpuTargetedRegenerationEpoch&&) noexcept = default;
  CpuTargetedRegenerationEpoch(const CpuTargetedRegenerationEpoch&) = delete;
  CpuTargetedRegenerationEpoch& operator=(const CpuTargetedRegenerationEpoch&) = delete;

  [[nodiscard]] const NegotiatedRegenerationPlan& plan() const noexcept { return plan_; }
  [[nodiscard]] std::span<const CpuTargetedRegenerationColumn> columns() const noexcept {
    return columns_;
  }
  [[nodiscard]] std::span<const CpuTargetedRegenerationPool> pools() const noexcept {
    return pools_;
  }
  [[nodiscard]] const OneWorldSelection& refreshed_selection() const noexcept {
    return refreshed_selection_;
  }
  [[nodiscard]] const CpuTargetedRegenerationEpochCounters& counters() const noexcept {
    return counters_;
  }
  [[nodiscard]] std::uint64_t batch_identity() const noexcept { return batch_identity_; }
  [[nodiscard]] std::uint64_t epoch_identity() const noexcept { return epoch_identity_; }

 private:
  CpuTargetedRegenerationEpoch() = default;

  NegotiatedRegenerationPlan plan_;
  std::vector<CpuTargetedRegenerationColumn> columns_;
  std::vector<CpuTargetedRegenerationPool> pools_;
  OneWorldSelection refreshed_selection_;
  CpuTargetedRegenerationEpochCounters counters_;
  std::uint64_t batch_identity_ = 0;
  std::uint64_t epoch_identity_ = 0;

  friend struct CpuTargetedRegenerationEpochFactory;
};

enum class CpuTargetedRegenerationEpochErrorCode : std::uint8_t {
  kInvalidConfiguration = 0,
  kInvalidInput = 1,
  kAssociationMismatch = 2,
  kPlanMismatch = 3,
  kBoundExhausted = 4,
  kArithmeticOverflow = 5,
  kCandidateGeneration = 6,
  kPublicationFailure = 7,
  kSelectionFailure = 8,
  kResourceExhausted = 9,
  kInternalInvariant = 10,
};

struct CpuTargetedRegenerationEpochError {
  CpuTargetedRegenerationEpochErrorCode code =
      CpuTargetedRegenerationEpochErrorCode::kInvalidConfiguration;
  std::string_view invariant_id;
  std::string_view detail;
  std::optional<board_ir::EntityRef> net;
  std::optional<std::uint64_t> target_identity;
  std::optional<std::uint64_t> query_identity;
  std::optional<std::uint64_t> expected_value;
  std::optional<std::uint64_t> actual_value;
  std::optional<NegotiatedRegenerationPlanErrorCode> plan_error_code;
  std::optional<routing::CandidatePolicyErrorCode> policy_error_code;
  std::optional<routing::RouteFailureCode> route_failure_code;
  std::optional<candidates::CandidateRejectionCode> candidate_rejection_code;
  std::string candidate_rejection_invariant;
  std::optional<OneWorldSelectionErrorCode> selection_error_code;
  std::optional<ResourceAccountingErrorCode> accounting_error_code;

  friend bool operator==(const CpuTargetedRegenerationEpochError&,
                         const CpuTargetedRegenerationEpochError&) = default;
};

using CpuTargetedRegenerationEpochResult =
    std::variant<CpuTargetedRegenerationEpoch, CpuTargetedRegenerationEpochError>;

// Executes exactly one immutable P4R-06 target roster, publishes source plus
// generated drafts through one fresh local CandidateStore transaction, and
// performs exactly one refreshed P4R-03 selection/accounting call.
[[nodiscard]] CpuTargetedRegenerationEpochResult ExecuteCpuTargetedRegenerationEpoch(
    const board_ir::BoardSnapshot& board, const ResourceCapacityModel& capacities,
    std::span<const CpuTargetedRegenerationSourcePool> source_pools,
    const NegotiatedRegenerationPlan& plan, const NegotiatedPriceSnapshot* prior_prices,
    std::span<const CpuTargetedRegenerationNetContext> contexts,
    CpuTargetedRegenerationEpochConfig config = {});

}  // namespace apgar::allocator

#endif  // APGAR_ALLOCATOR_CPU_TARGETED_REGENERATION_EPOCH_H_
