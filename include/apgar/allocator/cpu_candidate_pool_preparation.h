#ifndef APGAR_ALLOCATOR_CPU_CANDIDATE_POOL_PREPARATION_H_
#define APGAR_ALLOCATOR_CPU_CANDIDATE_POOL_PREPARATION_H_

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <variant>
#include <vector>

#include "apgar/allocator/one_world_selection.h"
#include "apgar/board_ir/board.h"
#include "apgar/candidates/candidate_store.h"
#include "apgar/geometry_compiler/compiled_board.h"
#include "apgar/routing/candidate_policy.h"
#include "apgar/routing/cpu_astar.h"
#include "apgar/routing/planar_route.h"

namespace apgar::allocator {

inline constexpr std::uint64_t kMaximumCpuCandidatePoolNets = 1'000'000;
inline constexpr std::uint64_t kMaximumCpuCandidatesPerNet = 1'000'000;
inline constexpr std::uint64_t kMaximumCpuCandidatePoolQueries = 1'000'000;
inline constexpr std::uint32_t kMaximumCpuCandidatePoolWorkers = 1'024;

// One explicit net-local production CPU schedule. The CompiledBoard must be a
// retained prepared context for request.net. Its address is borrowed only for
// the call and never participates in identity, ordering, or output.
struct CpuCandidatePoolNetSchedule {
  const geometry_compiler::CompiledBoard* compiled_board = nullptr;
  routing::PlanarRouteRequest request;
  routing::DeterministicAlternativePolicySchedule candidate_schedule;
};

struct CpuCandidatePoolPreparationLimits {
  std::uint64_t maximum_nets = 1'024;
  std::uint64_t maximum_candidates_per_net = 128;
  std::uint64_t maximum_total_queries = 131'072;
  std::uint64_t maximum_cpu_work_units_per_query = 100'000'000;
  std::uint64_t maximum_aggregate_cpu_work_units = 1'000'000'000;
  std::uint64_t maximum_generated_bytes_per_query = 4U * 1024U * 1024U;
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

  friend bool operator==(const CpuCandidatePoolPreparationLimits&,
                         const CpuCandidatePoolPreparationLimits&) = default;
};

struct CpuCandidatePoolPreparationConfig {
  std::uint32_t worker_count = 1;
  CpuCandidatePoolPreparationLimits limits;

  friend bool operator==(const CpuCandidatePoolPreparationConfig&,
                         const CpuCandidatePoolPreparationConfig&) = default;
};

enum class CpuCandidateColumnOutcomeCode : std::uint8_t {
  kAdmitted = 0,
  kDisconnectedRoute = 1,
  kUnsupportedRoute = 2,
  kCandidateBuildRejected = 3,
  kAdmissionRejected = 4,
};

struct CpuCandidatePoolColumn {
  board_ir::EntityRef net;
  std::uint64_t policy_identity = 0;
  std::uint64_t query_identity = 0;
  std::uint32_t candidate_ordinal = 0;
  CpuCandidateColumnOutcomeCode outcome = CpuCandidateColumnOutcomeCode::kAdmissionRejected;
  std::optional<candidates::CandidateId> candidate_id;
  std::optional<routing::RouteFailureCode> route_failure_code;
  std::optional<candidates::CandidateRejectionCode> candidate_rejection_code;

  friend bool operator==(const CpuCandidatePoolColumn&, const CpuCandidatePoolColumn&) = default;
};

struct CpuCandidatePoolDiagnostic {
  board_ir::EntityRef net;
  std::uint64_t policy_identity = 0;
  std::uint64_t query_identity = 0;
  std::uint32_t candidate_ordinal = 0;
  CpuCandidateColumnOutcomeCode outcome = CpuCandidateColumnOutcomeCode::kAdmissionRejected;
  std::optional<routing::RouteFailureCode> route_failure_code;
  candidates::CandidateRejection rejection;

  friend bool operator==(const CpuCandidatePoolDiagnostic&,
                         const CpuCandidatePoolDiagnostic&) = default;
};

class PreparedCpuCandidatePool {
 public:
  PreparedCpuCandidatePool(PreparedCpuCandidatePool&&) noexcept = default;
  PreparedCpuCandidatePool& operator=(PreparedCpuCandidatePool&&) noexcept = default;
  PreparedCpuCandidatePool(const PreparedCpuCandidatePool&) = delete;
  PreparedCpuCandidatePool& operator=(const PreparedCpuCandidatePool&) = delete;

  [[nodiscard]] board_ir::EntityRef net() const noexcept { return net_; }
  [[nodiscard]] std::span<const candidates::StoredCandidate> candidates() const noexcept {
    return candidates_;
  }
  [[nodiscard]] OneWorldCandidatePool one_world_pool() const noexcept {
    return OneWorldCandidatePool{.net = net_, .candidates = one_world_candidates_};
  }

 private:
  explicit PreparedCpuCandidatePool(board_ir::EntityRef net) : net_(net) {}

  board_ir::EntityRef net_;
  std::vector<candidates::StoredCandidate> candidates_;
  std::vector<const candidates::RouteCandidate*> one_world_candidates_;

  friend struct PreparedCpuCandidatePoolsFactory;
};

class PreparedCpuCandidatePools {
 public:
  PreparedCpuCandidatePools(PreparedCpuCandidatePools&&) noexcept = default;
  PreparedCpuCandidatePools& operator=(PreparedCpuCandidatePools&&) noexcept = default;
  PreparedCpuCandidatePools(const PreparedCpuCandidatePools&) = delete;
  PreparedCpuCandidatePools& operator=(const PreparedCpuCandidatePools&) = delete;

  [[nodiscard]] std::span<const PreparedCpuCandidatePool> pools() const noexcept { return pools_; }
  [[nodiscard]] std::span<const CpuCandidatePoolColumn> columns() const noexcept {
    return columns_;
  }
  [[nodiscard]] std::span<const CpuCandidatePoolDiagnostic> diagnostics() const noexcept {
    return diagnostics_;
  }
  [[nodiscard]] std::uint64_t batch_identity() const noexcept { return batch_identity_; }
  [[nodiscard]] std::uint64_t query_count() const noexcept { return query_count_; }
  [[nodiscard]] std::uint64_t cpu_work_units() const noexcept { return cpu_work_units_; }
  [[nodiscard]] std::uint64_t generated_bytes() const noexcept { return generated_bytes_; }
  [[nodiscard]] std::uint64_t retained_candidate_bytes() const noexcept {
    return retained_candidate_bytes_;
  }

 private:
  PreparedCpuCandidatePools() = default;

  std::vector<PreparedCpuCandidatePool> pools_;
  std::vector<CpuCandidatePoolColumn> columns_;
  std::vector<CpuCandidatePoolDiagnostic> diagnostics_;
  std::uint64_t batch_identity_ = 0;
  std::uint64_t query_count_ = 0;
  std::uint64_t cpu_work_units_ = 0;
  std::uint64_t generated_bytes_ = 0;
  std::uint64_t retained_candidate_bytes_ = 0;

  friend struct PreparedCpuCandidatePoolsFactory;
};

enum class CpuCandidatePoolPreparationErrorCode : std::uint8_t {
  kInvalidConfiguration = 0,
  kInvalidInput = 1,
  kAssociationMismatch = 2,
  kBoundExhausted = 3,
  kArithmeticOverflow = 4,
  kResourceExhausted = 5,
  kInternalInvariant = 6,
};

struct CpuCandidatePoolPreparationError {
  CpuCandidatePoolPreparationErrorCode code =
      CpuCandidatePoolPreparationErrorCode::kInvalidConfiguration;
  std::string_view invariant_id;
  std::string_view detail;
  std::optional<board_ir::EntityRef> net;
  std::optional<std::uint64_t> policy_identity;
  std::optional<std::uint64_t> query_identity;
  std::optional<std::uint64_t> expected_value;
  std::optional<std::uint64_t> actual_value;
  std::optional<routing::CandidatePolicyErrorCode> policy_error_code;
  std::optional<routing::RouteFailureCode> route_failure_code;
  std::optional<candidates::CandidateRejectionCode> candidate_rejection_code;

  friend bool operator==(const CpuCandidatePoolPreparationError&,
                         const CpuCandidatePoolPreparationError&) = default;
};

using CpuCandidatePoolPreparationResult =
    std::variant<PreparedCpuCandidatePools, CpuCandidatePoolPreparationError>;

// Prepares one immutable canonical pool per requested net. Workers execute
// only production CPU A* and typed candidate construction into canonical
// result slots. One local CandidateStore mixed-draft transaction performs all
// exact admission, diagnostics, deduplication, retention, and publication.
[[nodiscard]] CpuCandidatePoolPreparationResult PrepareCpuCandidatePools(
    const board_ir::BoardSnapshot& board, std::span<const CpuCandidatePoolNetSchedule> schedules,
    CpuCandidatePoolPreparationConfig config = {});

}  // namespace apgar::allocator

#endif  // APGAR_ALLOCATOR_CPU_CANDIDATE_POOL_PREPARATION_H_
