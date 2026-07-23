#ifndef APGAR_ALLOCATOR_CPU_CANDIDATE_POOL_PREPARATION_H_
#define APGAR_ALLOCATOR_CPU_CANDIDATE_POOL_PREPARATION_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "apgar/allocator/one_world.h"
#include "apgar/routing/cpu_astar.h"

namespace apgar::allocator {

inline constexpr std::uint32_t kPersistentCpuCandidatePoolPreparerSchemaVersion = 1;
inline constexpr std::uint32_t kCpuCandidatePoolPreparationSchemaVersionV1 = 1;
inline constexpr std::uint32_t kCpuCandidatePoolPreparationSchemaVersion = 2;
inline constexpr std::uint32_t kMaximumPersistentCpuCandidateWorkersV1 = 64;
inline constexpr std::uint64_t kMaximumCpuCandidatePoolQueriesV1 = 1'000'000;
inline constexpr std::uint64_t kMaximumCpuCandidatePoolRouteWorkUnitsV1 = 1'000'000'000'000'000ULL;

struct PersistentCpuCandidatePoolPreparerConfig {
  std::uint32_t schema_version = kPersistentCpuCandidatePoolPreparerSchemaVersion;
  std::uint32_t worker_count = 1;

  friend bool operator==(const PersistentCpuCandidatePoolPreparerConfig&,
                         const PersistentCpuCandidatePoolPreparerConfig&) = default;
};

struct CpuCandidatePoolPreparationLimits {
  std::uint64_t maximum_nets = 100'000;
  std::uint64_t maximum_route_queries = kMaximumCpuCandidatePoolQueriesV1;
  std::uint64_t maximum_total_route_work_units = 100'000'000'000ULL;
  std::uint64_t maximum_concurrent_route_records = 8'000'000;
  std::uint64_t maximum_concurrent_queue_entries = 64'000'000;
  std::uint64_t maximum_concurrent_reconstruction_states = 8'000'000;
  std::uint64_t maximum_policy_resource_entries = 100'000'000;
  std::uint64_t maximum_retained_candidate_bytes = 64ULL * 1024ULL * 1024ULL * 1024ULL;
  std::uint64_t maximum_candidate_draft_bytes = 64ULL * 1024ULL * 1024ULL;
  std::uint64_t maximum_generated_candidate_bytes = 64ULL * 1024ULL * 1024ULL * 1024ULL;

  friend bool operator==(const CpuCandidatePoolPreparationLimits&,
                         const CpuCandidatePoolPreparationLimits&) = default;
};

struct CpuCandidatePoolPreparationConfig {
  std::uint32_t schema_version = kCpuCandidatePoolPreparationSchemaVersion;
  std::uint32_t requested_candidates_per_net = 4;
  std::uint64_t deterministic_seed = 0;
  std::uint64_t step_surcharge_increment = 1;
  std::uint64_t bend_surcharge_increment = 1;
  std::uint64_t resource_penalty_increment = 1'000'000;
  routing::CpuRouteWorkLimits route_limits{
      .maximum_work_units = 20'000'000,
      .maximum_record_count = 1'000'000,
      .maximum_queue_size = 8'000'000,
      .maximum_reconstruction_states = 100'000,
  };
  CpuCandidatePoolPreparationLimits limits;
  candidates::CandidateStoreConfig store_config;

  friend bool operator==(const CpuCandidatePoolPreparationConfig&,
                         const CpuCandidatePoolPreparationConfig&) = default;
};

enum class CpuCandidatePoolColumnOutcome : std::uint8_t {
  kAdmitted = 0,
  kDuplicate = 1,
  kRouteDisconnected = 2,
  kRouteUnsupported = 3,
  kSkippedAfterDisconnectedProof = 4,
  kSkippedAfterUnsupportedProof = 5,
  kBuildRejected = 6,
  kAdmissionRejected = 7,
};

struct CpuCandidatePoolColumnRecord {
  board_ir::EntityRef net{};
  std::uint32_t candidate_ordinal = 0;
  std::uint64_t policy_identity = 0;
  std::uint64_t batch_identity = 0;
  std::uint64_t query_identity = 0;
  std::uint64_t route_work_units = 0;
  CpuCandidatePoolColumnOutcome outcome =
      CpuCandidatePoolColumnOutcome::kSkippedAfterDisconnectedProof;
  std::optional<candidates::CandidateId> candidate_id;
  std::optional<std::uint64_t> candidate_payload_checksum;
  std::optional<candidates::CandidateRejectionCode> rejection_code;

  friend bool operator==(const CpuCandidatePoolColumnRecord&,
                         const CpuCandidatePoolColumnRecord&) = default;
};

struct CpuCandidatePoolPreparationCounters {
  std::uint64_t requested_columns = 0;
  std::uint64_t executed_route_queries = 0;
  std::uint64_t route_work_units = 0;
  std::uint64_t successful_routes = 0;
  std::uint64_t disconnected_proofs = 0;
  std::uint64_t unsupported_proofs = 0;
  std::uint64_t skipped_columns = 0;
  std::uint64_t built_candidates = 0;
  std::uint64_t admitted_candidates = 0;
  std::uint64_t duplicate_candidates = 0;
  std::uint64_t rejected_columns = 0;
  std::uint64_t retained_candidates = 0;

  friend bool operator==(const CpuCandidatePoolPreparationCounters&,
                         const CpuCandidatePoolPreparationCounters&) = default;
};

struct PersistentCpuCandidatePoolTelemetry {
  std::uint64_t workers_started = 0;
  std::uint64_t invocations_started = 0;
  std::uint64_t invocations_completed = 0;
  std::uint64_t jobs_dispatched = 0;

  friend bool operator==(const PersistentCpuCandidatePoolTelemetry&,
                         const PersistentCpuCandidatePoolTelemetry&) = default;
};

// Diagnostic-only wall intervals for one separately instrumented preparation.
// Ordinary callers pass no profile pointer and execute no profile clocks.
// Worker-sum intervals can exceed their enclosing wave wall interval because
// persistent CPU workers execute concurrently.
struct CpuCandidatePoolPreparationOperationalProfileV1 {
  std::uint64_t component_wall_nanoseconds = 0;
  std::uint64_t validation_and_scheduling_wall_nanoseconds = 0;
  std::uint64_t base_worker_wave_wall_nanoseconds = 0;
  std::uint64_t alternative_policy_wall_nanoseconds = 0;
  std::uint64_t alternative_worker_wave_wall_nanoseconds = 0;
  std::uint64_t route_and_candidate_build_worker_sum_nanoseconds = 0;
  std::uint64_t exact_admission_and_store_publication_wall_nanoseconds = 0;
  std::uint64_t publication_correlation_and_pool_materialization_wall_nanoseconds = 0;
  std::uint64_t unclassified_serial_wall_nanoseconds = 0;
  std::uint64_t base_jobs_dispatched = 0;
  std::uint64_t alternative_jobs_dispatched = 0;

  friend bool operator==(const CpuCandidatePoolPreparationOperationalProfileV1&,
                         const CpuCandidatePoolPreparationOperationalProfileV1&) = default;
};

enum class CpuCandidatePoolPreparationErrorCode : std::uint8_t {
  kUnsupportedSchema = 0,
  kInvalidConfiguration = 1,
  kBusy = 2,
  kAssociationMismatch = 3,
  kInputBoundExceeded = 4,
  kWorkBoundExceeded = 5,
  kPolicySynthesis = 6,
  kCandidateGeneration = 7,
  kCandidateStore = 8,
  kResourceExhausted = 9,
  kInternalInvariant = 10,
};

enum class CpuCandidatePoolAttemptState : std::uint8_t {
  kQueryInFlight = 0,
  kRouteSucceeded = 1,
  kRouteFailed = 2,
};

struct CpuCandidatePoolAttemptedColumnRecord {
  board_ir::EntityRef net{};
  std::uint32_t candidate_ordinal = 0;
  std::uint64_t policy_identity = 0;
  std::uint64_t batch_identity = 0;
  std::uint64_t query_identity = 0;
  std::uint64_t route_work_units = 0;
  CpuCandidatePoolAttemptState state = CpuCandidatePoolAttemptState::kQueryInFlight;
  std::optional<routing::RouteFailureCode> route_failure_code;

  friend bool operator==(const CpuCandidatePoolAttemptedColumnRecord&,
                         const CpuCandidatePoolAttemptedColumnRecord&) = default;
};

struct CpuCandidatePoolFailedPreparationCounters {
  std::uint64_t requested_columns = 0;
  std::uint64_t route_queries = 0;
  std::uint64_t route_work_units = 0;

  friend bool operator==(const CpuCandidatePoolFailedPreparationCounters&,
                         const CpuCandidatePoolFailedPreparationCounters&) = default;
};

struct CpuCandidatePoolFailedPreparationObservation {
  std::uint32_t schema_version = kCpuCandidatePoolPreparationSchemaVersion;
  std::uint64_t board_content_hash = 0;
  std::uint64_t workload_checksum = 0;
  CpuCandidatePoolPreparationConfig config;
  std::uint64_t batch_identity = 0;
  CpuCandidatePoolFailedPreparationCounters counters;
  std::vector<CpuCandidatePoolAttemptedColumnRecord> attempted_columns;
  bool candidate_store_publication_committed = false;
  std::unique_ptr<candidates::CandidateStore> authoritative_candidate_store;
  std::uint64_t observation_checksum = 0;

  friend bool operator==(const CpuCandidatePoolFailedPreparationObservation& left,
                         const CpuCandidatePoolFailedPreparationObservation& right) {
    return left.schema_version == right.schema_version &&
           left.board_content_hash == right.board_content_hash &&
           left.workload_checksum == right.workload_checksum && left.config == right.config &&
           left.batch_identity == right.batch_identity && left.counters == right.counters &&
           left.attempted_columns == right.attempted_columns &&
           left.candidate_store_publication_committed ==
               right.candidate_store_publication_committed &&
           static_cast<bool>(left.authoritative_candidate_store) ==
               static_cast<bool>(right.authoritative_candidate_store) &&
           left.observation_checksum == right.observation_checksum;
  }
};

struct CpuCandidatePoolPreparationError {
  CpuCandidatePoolPreparationErrorCode code =
      CpuCandidatePoolPreparationErrorCode::kInvalidConfiguration;
  std::string_view invariant_id;
  std::string_view detail;
  std::optional<board_ir::EntityRef> net;
  std::uint64_t required = 0;
  std::uint64_t configured = 0;
  std::optional<CpuCandidatePoolFailedPreparationObservation> failed_preparation;

  friend bool operator==(const CpuCandidatePoolPreparationError&,
                         const CpuCandidatePoolPreparationError&) = default;
};

class PersistentCpuCandidatePoolPreparer;

class PreparedCpuCandidatePools {
 public:
  PreparedCpuCandidatePools(const PreparedCpuCandidatePools&) = delete;
  PreparedCpuCandidatePools(PreparedCpuCandidatePools&&) noexcept = default;
  PreparedCpuCandidatePools& operator=(const PreparedCpuCandidatePools&) = delete;
  PreparedCpuCandidatePools& operator=(PreparedCpuCandidatePools&&) noexcept = default;

  [[nodiscard]] const CpuCandidatePoolPreparationConfig& config() const noexcept { return config_; }
  [[nodiscard]] std::uint64_t batch_identity() const noexcept { return batch_identity_; }
  [[nodiscard]] const CpuCandidatePoolPreparationCounters& counters() const noexcept {
    return counters_;
  }
  [[nodiscard]] const std::vector<CpuCandidatePoolColumnRecord>& columns() const noexcept {
    return columns_;
  }
  [[nodiscard]] const std::vector<CandidatePool>& pools() const noexcept { return pools_; }
  [[nodiscard]] bool has_candidate_store() const noexcept { return candidate_store_ != nullptr; }
  [[nodiscard]] candidates::CandidateStore& candidate_store() noexcept { return *candidate_store_; }
  [[nodiscard]] const candidates::CandidateStore& candidate_store() const noexcept {
    return *candidate_store_;
  }
  [[nodiscard]] std::uint64_t preparation_checksum() const noexcept {
    return preparation_checksum_;
  }

 private:
  PreparedCpuCandidatePools(CpuCandidatePoolPreparationConfig config, std::uint64_t batch_identity,
                            CpuCandidatePoolPreparationCounters counters,
                            std::vector<CpuCandidatePoolColumnRecord> columns,
                            std::vector<CandidatePool> pools,
                            std::unique_ptr<candidates::CandidateStore> candidate_store,
                            std::uint64_t preparation_checksum) noexcept
      : config_(std::move(config)),
        batch_identity_(batch_identity),
        counters_(counters),
        columns_(std::move(columns)),
        pools_(std::move(pools)),
        candidate_store_(std::move(candidate_store)),
        preparation_checksum_(preparation_checksum) {}

  CpuCandidatePoolPreparationConfig config_;
  std::uint64_t batch_identity_ = 0;
  CpuCandidatePoolPreparationCounters counters_;
  std::vector<CpuCandidatePoolColumnRecord> columns_;
  std::vector<CandidatePool> pools_;
  std::unique_ptr<candidates::CandidateStore> candidate_store_;
  std::uint64_t preparation_checksum_ = 0;

  friend std::variant<PreparedCpuCandidatePools, CpuCandidatePoolPreparationError>
  PrepareInitialCpuCandidatePools(PersistentCpuCandidatePoolPreparer&,
                                  const board_ir::BoardSnapshot&, const MultiNetWorkload&,
                                  const CpuCandidatePoolPreparationConfig&);
  template <bool>
  friend std::variant<PreparedCpuCandidatePools, CpuCandidatePoolPreparationError>
  PrepareInitialCpuCandidatePoolsImpl(PersistentCpuCandidatePoolPreparer&,
                                      const board_ir::BoardSnapshot&, const MultiNetWorkload&,
                                      const CpuCandidatePoolPreparationConfig&,
                                      CpuCandidatePoolPreparationOperationalProfileV1*);
};

using PreparedCpuCandidatePoolsResult =
    std::variant<PreparedCpuCandidatePools, CpuCandidatePoolPreparationError>;

class PersistentCpuCandidatePoolPreparer {
 public:
  ~PersistentCpuCandidatePoolPreparer();
  PersistentCpuCandidatePoolPreparer(const PersistentCpuCandidatePoolPreparer&) = delete;
  PersistentCpuCandidatePoolPreparer& operator=(const PersistentCpuCandidatePoolPreparer&) = delete;
  PersistentCpuCandidatePoolPreparer(PersistentCpuCandidatePoolPreparer&&) = delete;
  PersistentCpuCandidatePoolPreparer& operator=(PersistentCpuCandidatePoolPreparer&&) = delete;

  [[nodiscard]] const PersistentCpuCandidatePoolPreparerConfig& config() const noexcept {
    return config_;
  }
  [[nodiscard]] PersistentCpuCandidatePoolTelemetry telemetry() const noexcept;

 private:
  struct Impl;

  PersistentCpuCandidatePoolPreparer(PersistentCpuCandidatePoolPreparerConfig config,
                                     std::unique_ptr<Impl> impl) noexcept;

  PersistentCpuCandidatePoolPreparerConfig config_;
  std::unique_ptr<Impl> impl_;

  friend std::variant<std::unique_ptr<PersistentCpuCandidatePoolPreparer>,
                      CpuCandidatePoolPreparationError>
  CreatePersistentCpuCandidatePoolPreparer(const PersistentCpuCandidatePoolPreparerConfig&);
  friend PreparedCpuCandidatePoolsResult PrepareInitialCpuCandidatePools(
      PersistentCpuCandidatePoolPreparer&, const board_ir::BoardSnapshot&, const MultiNetWorkload&,
      const CpuCandidatePoolPreparationConfig&);
  template <bool>
  friend PreparedCpuCandidatePoolsResult PrepareInitialCpuCandidatePoolsImpl(
      PersistentCpuCandidatePoolPreparer&, const board_ir::BoardSnapshot&, const MultiNetWorkload&,
      const CpuCandidatePoolPreparationConfig&, CpuCandidatePoolPreparationOperationalProfileV1*);
};

using PersistentCpuCandidatePoolPreparerResult =
    std::variant<std::unique_ptr<PersistentCpuCandidatePoolPreparer>,
                 CpuCandidatePoolPreparationError>;

[[nodiscard]] PersistentCpuCandidatePoolPreparerResult CreatePersistentCpuCandidatePoolPreparer(
    const PersistentCpuCandidatePoolPreparerConfig& config);

// Synchronous and fail-closed. Concurrent calls on the same preparer receive
// kBusy; a call never retains Board/workload references after returning.
// Workers fill canonical query slots only. One fresh CandidateStore receives
// all generated drafts and ordinary rejections through one conditional CAN-004
// publication after both worker waves complete.
[[nodiscard]] PreparedCpuCandidatePoolsResult PrepareInitialCpuCandidatePools(
    PersistentCpuCandidatePoolPreparer& preparer, const board_ir::BoardSnapshot& board,
    const MultiNetWorkload& workload, const CpuCandidatePoolPreparationConfig& config);

[[nodiscard]] PreparedCpuCandidatePoolsResult
PrepareInitialCpuCandidatePoolsWithOperationalProfileV1(
    PersistentCpuCandidatePoolPreparer& preparer, const board_ir::BoardSnapshot& board,
    const MultiNetWorkload& workload, const CpuCandidatePoolPreparationConfig& config,
    CpuCandidatePoolPreparationOperationalProfileV1& operational_profile);

}  // namespace apgar::allocator

#endif  // APGAR_ALLOCATOR_CPU_CANDIDATE_POOL_PREPARATION_H_
