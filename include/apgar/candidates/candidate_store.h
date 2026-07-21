#ifndef APGAR_CANDIDATES_CANDIDATE_STORE_H_
#define APGAR_CANDIDATES_CANDIDATE_STORE_H_

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "apgar/board_ir/board.h"
#include "apgar/candidates/route_candidate.h"

namespace apgar::candidates {

inline constexpr std::uint64_t kDefaultMaximumRejectionItemsPerTransaction = 1'024;
inline constexpr std::uint64_t kDefaultMaximumAdmissionItemsPerTransaction = 1'024;
inline constexpr std::uint64_t kDefaultMaximumAdmissionInputBytesPerTransaction =
    64U * 1024U * 1024U;
inline constexpr std::uint64_t kDefaultMaximumAdmissionWorkUnitsPerTransaction = 100'000'000;
inline constexpr std::uint64_t kDefaultMaximumPinLeaseItemsPerTransaction = 1'024;
inline constexpr std::uint64_t kMaximumPinLeaseItemsPerTransaction = 1'000'000;
inline constexpr std::uint64_t kDefaultMaximumExpectedPoolsPerInvocation = 100'000;
inline constexpr std::uint64_t kDefaultMaximumExpectedCandidatesPerInvocation = 1'000'000;
inline constexpr std::uint64_t kMaximumExpectedPoolsPerInvocation = 1'000'000;
inline constexpr std::uint64_t kMaximumExpectedCandidatesPerInvocation = 1'000'000;

struct CandidateStoreConfig {
  std::uint64_t maximum_candidates_per_net = 0;
  std::uint64_t maximum_candidate_bytes_per_net = 0;
  std::uint64_t maximum_rejection_records = 0;
  // Bounds one call to the public batch rejection-ingestion seam independently
  // of the retained-record cap.
  std::uint64_t maximum_rejection_items_per_transaction =
      kDefaultMaximumRejectionItemsPerTransaction;
  // Admission-transaction limits are independent of retained-pool limits. The
  // defaults bound existing call sites while allowing the Phase 3 k <= 128
  // bakeoff; production owners should set workload-specific values.
  std::uint64_t maximum_admission_items_per_transaction =
      kDefaultMaximumAdmissionItemsPerTransaction;
  std::uint64_t maximum_admission_input_bytes_per_transaction =
      kDefaultMaximumAdmissionInputBytesPerTransaction;
  std::uint64_t maximum_admission_work_units_per_transaction =
      kDefaultMaximumAdmissionWorkUnitsPerTransaction;
  // Bounds one atomic selected-candidate pin-lease acquisition independently
  // of retained-pool and candidate-admission limits.
  std::uint64_t maximum_pin_lease_items_per_transaction =
      kDefaultMaximumPinLeaseItemsPerTransaction;
  // Bounds the immutable source-pool compare performed by one conditional
  // deterministic invocation independently of generated-column bounds.
  std::uint64_t maximum_expected_pools_per_invocation = kDefaultMaximumExpectedPoolsPerInvocation;
  std::uint64_t maximum_expected_candidates_per_invocation =
      kDefaultMaximumExpectedCandidatesPerInvocation;

  friend bool operator==(const CandidateStoreConfig&, const CandidateStoreConfig&) = default;
};

enum class CandidateStoreErrorCode : std::uint8_t {
  kInvalidConfiguration = 0,
  kMissingCandidate = 1,
  kInvalidPinOwner = 2,
  kPinLeaseInputBoundExceeded = 3,
  kInvalidPinLeaseRequest = 4,
  kCandidateNetMismatch = 5,
  kCandidatePayloadMismatch = 6,
  kPinLeaseIdentityExhausted = 7,
  kResourceExhausted = 8,
  kCandidateSemanticMismatch = 9,
  kInvalidInvocation = 10,
  kInvocationInputBoundExceeded = 11,
  kStoreDrift = 12,
  kInvocationAdmissionPreflightFailed = 13,
};

struct CandidateStoreError {
  CandidateStoreErrorCode code;
  std::string detail;

  friend bool operator==(const CandidateStoreError&, const CandidateStoreError&) = default;
};

using StoredCandidate = std::shared_ptr<const RouteCandidate>;
using CandidateStoreAdmissionResult = std::variant<StoredCandidate, CandidateRejection>;

struct CandidatePinRequest {
  board_ir::EntityRef net{};
  CandidateId candidate_id;
  std::uint64_t candidate_payload_checksum = 0;
  // Exact immutable value expected by the caller. ID and checksum remain
  // indexed diagnostics; complete typed equality closes their collision gap.
  StoredCandidate expected_candidate;

  friend bool operator==(const CandidatePinRequest&, const CandidatePinRequest&) = default;
};

class CandidateStore;

// One independent, store-issued retention lease over an atomic candidate
// group. Moving transfers the lease; destruction and repeated Release calls
// are safe and release the group at most once. The control block also makes a
// lease that accidentally outlives its store harmless.
class CandidateStorePinLease {
 public:
  CandidateStorePinLease(const CandidateStorePinLease&) = delete;
  CandidateStorePinLease& operator=(const CandidateStorePinLease&) = delete;
  CandidateStorePinLease(CandidateStorePinLease&& other) noexcept;
  CandidateStorePinLease& operator=(CandidateStorePinLease&& other) noexcept;
  ~CandidateStorePinLease();

  void Release() noexcept;
  [[nodiscard]] bool active() const noexcept;
  [[nodiscard]] bool belongs_to(const CandidateStore& store) const noexcept;

 private:
  struct Control;

  CandidateStorePinLease(std::shared_ptr<Control> control, std::uint64_t lease_id) noexcept
      : control_(std::move(control)), lease_id_(lease_id) {}

  std::shared_ptr<Control> control_;
  std::uint64_t lease_id_ = 0;

  friend class CandidateStore;
};

using CandidateStorePinLeaseResult = std::variant<CandidateStorePinLease, CandidateStoreError>;

struct CandidateAdmissionItem {
  routing::PlanarRouteRequest request;
  GeneratedRouteCandidate generated;
};

// One generated item in a deterministic invocation. Different items may use
// distinct authentic per-net CompiledBoards while sharing one BoardSnapshot.
struct CandidateInvocationGeneratedItem {
  std::reference_wrapper<const geometry_compiler::CompiledBoard> compiled_board;
  routing::PlanarRouteRequest request;
  GeneratedRouteCandidate generated;
};

using CandidateInvocationItem = std::variant<CandidateInvocationGeneratedItem, CandidateRejection>;

// Exact source pool expected by a conditional invocation. The per-net
// association binding remains authoritative when candidates is empty.
struct CandidateStoreExpectedPool {
  board_ir::EntityRef net{};
  CandidateAssociations associations;
  std::vector<StoredCandidate> candidates;
};

using CandidateStoreInvocationAdmissionResult =
    std::variant<std::vector<CandidateStoreAdmissionResult>, CandidateStoreError>;

struct CandidateStoreTelemetry {
  std::uint64_t last_publication_candidate_inspections = 0;
  std::uint64_t last_duplicate_equality_checks = 0;
  std::uint64_t rejection_batch_merges = 0;
  std::uint64_t shared_request_policy_normalizations = 0;

  friend bool operator==(const CandidateStoreTelemetry&, const CandidateStoreTelemetry&) = default;
};

// A total, versioned rank. Lower values are preferred. No comparison depends
// on insertion order, pointer identity, or hash-table iteration.
[[nodiscard]] bool CandidateRanksBefore(const RouteCandidate& left,
                                        const RouteCandidate& right) noexcept;
[[nodiscard]] bool CandidateMetricsDominate(const RouteCandidate& left,
                                            const RouteCandidate& right) noexcept;
[[nodiscard]] double ResourceJaccardOverlap(const RouteCandidate& left,
                                            const RouteCandidate& right) noexcept;
[[nodiscard]] double GeometricOverlapRatio(const RouteCandidate& left, const RouteCandidate& right);
// Exact integer-ratio v1 quantization used by durable telemetry. Each ratio is
// rounded to nearest integer ppm with exact halves upward. Invalid/nonmatching
// candidate contexts retain the established zero-overlap value; nullopt is
// reserved for arithmetic failure and must fail durable telemetry closed.
[[nodiscard]] std::optional<std::uint64_t> ResourceJaccardOverlapPpmV1(
    const RouteCandidate& left, const RouteCandidate& right) noexcept;
[[nodiscard]] std::optional<std::uint64_t> GeometricOverlapRatioPpmV1(const RouteCandidate& left,
                                                                      const RouteCandidate& right);

class CandidateStore {
 public:
  explicit CandidateStore(CandidateStoreConfig config);
  ~CandidateStore();
  CandidateStore(const CandidateStore&) = delete;
  CandidateStore& operator=(const CandidateStore&) = delete;

  [[nodiscard]] const CandidateStoreConfig& config() const noexcept { return config_; }
  [[nodiscard]] bool valid() const noexcept;
  [[nodiscard]] CandidateStoreTelemetry telemetry() const;

  // Admission consumes explicit caller-owned rvalues. Binding these seams does
  // not copy any policy/geometry/resource bulk; transaction preflight runs on
  // the transferred object before elements are moved into internal scratch.
  [[nodiscard]] CandidateStoreAdmissionResult Admit(const CandidateAdmissionContext& context,
                                                    GeneratedRouteCandidate&& generated);
  // Applies one caller-owned request to the complete batch. V1 accounting
  // conservatively counts its policy once per candidate, while implementation
  // admission keeps the request by reference and normalizes its policy once.
  [[nodiscard]] std::vector<CandidateStoreAdmissionResult> AdmitBatch(
      const CandidateAdmissionContext& context, std::vector<GeneratedRouteCandidate>&& generated);
  [[nodiscard]] std::vector<CandidateStoreAdmissionResult> AdmitBatch(
      const board_ir::BoardSnapshot& board, const geometry_compiler::CompiledBoard& compiled_board,
      std::vector<CandidateAdmissionItem>&& items);
  // Publishes one complete deterministic invocation through one CAN-004
  // boundary. Every item and expected pool is preflighted before exact work.
  // Immediately before publication, all expected pools are compared under the
  // publication mutex by association and canonical complete immutable value.
  // Drift and aggregate admission preflight failures are typed and change no
  // store state.
  [[nodiscard]] CandidateStoreInvocationAdmissionResult AdmitInvocationIfSourcePoolsMatch(
      const board_ir::BoardSnapshot& board,
      std::vector<CandidateStoreExpectedPool>&& expected_source_pools,
      std::vector<CandidateInvocationItem>&& items);

  [[nodiscard]] std::vector<StoredCandidate> Enumerate(board_ir::EntityRef net) const;
  [[nodiscard]] std::vector<CandidateRejection> Rejections() const;
  [[nodiscard]] std::uint64_t RejectionCount() const;
  [[nodiscard]] std::optional<std::uint64_t> CandidateBytes(board_ir::EntityRef net) const;

  // Retains structured diagnostics produced before exact store admission (for
  // example, by a planar-route candidate builder). Retention is canonically
  // ordered and bounded by maximum_rejection_records. Malformed public records
  // become bounded candidate.rejection.ingestion.v1 diagnostics. An over-cap
  // batch is not inspected and becomes one deterministic candidate-less
  // candidate.store.rejection_transaction.item_budget.v1 diagnostic returned
  // to the caller and submitted to bounded history. A within-cap batch returns
  // nullopt.
  void RetainRejection(const CandidateRejection& rejection);
  std::optional<CandidateRejection> RetainRejections(
      std::span<const CandidateRejection> rejections);

  [[nodiscard]] std::optional<CandidateStoreError> Pin(std::uint64_t owner_id,
                                                       CandidateId candidate_id);
  [[nodiscard]] std::optional<CandidateStoreError> Unpin(std::uint64_t owner_id,
                                                         CandidateId candidate_id);
  // Validates the complete identity group and acquires all pins under one store
  // lock. Failure leaves every candidate's pin state unchanged.
  [[nodiscard]] CandidateStorePinLeaseResult AcquirePinLease(
      std::span<const CandidatePinRequest> requests);
  // Atomically validates one complete canonical source-pool roster and
  // acquires a lease over a deduplicated subset of those exact candidates.
  // Unlike AcquirePinLease, an empty requested group is valid and returns a
  // store-identity lease. The expected roster itself must be nonempty.
  [[nodiscard]] CandidateStorePinLeaseResult AcquirePinLeaseIfSourcePoolsMatch(
      std::vector<CandidateStoreExpectedPool>&& expected_source_pools,
      std::span<const CandidatePinRequest> requests);
  // Acquires a store-identity lease without pinning candidates. This is an
  // explicit capability for zero-selection allocator plans; AcquirePinLease
  // continues to reject an empty candidate group.
  [[nodiscard]] CandidateStorePinLeaseResult AcquireEmptyPinLease();
  [[nodiscard]] bool IsPinned(CandidateId candidate_id) const;

  // Reapplies the deterministic retention order. Pins are always preserved;
  // a failure leaves the current pool unchanged.
  [[nodiscard]] std::optional<CandidateRejection> Prune(board_ir::EntityRef net);

 private:
  using PinKey = std::pair<std::uint64_t, CandidateId>;
  struct NetLess {
    [[nodiscard]] bool operator()(board_ir::EntityRef left,
                                  board_ir::EntityRef right) const noexcept {
      return std::pair{left.id, left.generation} < std::pair{right.id, right.generation};
    }
  };
  using CandidatePool = std::vector<StoredCandidate>;

  [[nodiscard]] bool IsPinnedLocked(CandidateId candidate_id) const;
  void ReleasePinLease(std::uint64_t lease_id) noexcept;
  void RetainRejectionLocked(const CandidateRejection& rejection);
  void RetainCanonicalRejectionsLocked(std::vector<CandidateRejection> canonical_rejections);
  [[nodiscard]] std::vector<CandidateRejection> BuildMergedRejectionsLocked(
      std::vector<CandidateRejection> canonical_rejections);
  [[nodiscard]] std::vector<CandidateStoreAdmissionResult> PublishAdmissionResults(
      std::vector<CandidateAdmissionResult> admitted,
      std::uint64_t shared_request_policy_normalization_delta = 0);
  [[nodiscard]] CandidateStoreInvocationAdmissionResult PublishConditionalAdmissionResults(
      std::vector<CandidateAdmissionResult> admitted,
      const std::vector<CandidateStoreExpectedPool>& expected_source_pools);
  [[nodiscard]] bool ExpectedPoolsMatchLocked(
      const std::vector<CandidateStoreExpectedPool>& expected_source_pools) const noexcept;
  [[nodiscard]] CandidateStoreAdmissionResult PublishAcceptedLocked(RouteCandidate candidate);
  [[nodiscard]] std::vector<CandidateStoreAdmissionResult> PublishAcceptedBatchLocked(
      std::vector<RouteCandidate> candidates,
      std::vector<CandidateRejection> canonical_rejections = {},
      std::uint64_t shared_request_policy_normalization_delta = 0);
  [[nodiscard]] std::optional<CandidateRejection> PruneLocked(board_ir::EntityRef net,
                                                              CandidateId newest_id);

  CandidateStoreConfig config_;
  mutable std::mutex mutex_;
  std::map<board_ir::EntityRef, CandidatePool, NetLess> pools_;
  std::map<CandidateId, StoredCandidate> candidate_id_index_;
  std::vector<CandidateRejection> rejections_;
  std::set<PinKey> pins_;
  std::map<CandidateId, std::uint64_t> pin_counts_;
  std::map<std::uint64_t, std::vector<CandidateId>> pin_leases_;
  std::uint64_t next_pin_lease_id_ = 1;
  std::shared_ptr<CandidateStorePinLease::Control> pin_lease_control_;
  // The first three fields bind the common Board IR/compiler session. Routing
  // profile and rule-bucket fields are bound independently by each net pool.
  std::optional<CandidateAssociations> bound_associations_;
  // Exact admission binds a net even when retention later rejects every
  // candidate or a multi-pool transaction rolls back. This authority is
  // independent of retained pool contents.
  std::map<board_ir::EntityRef, CandidateAssociations, NetLess> net_associations_;
  // Deterministic test instrumentation: candidate-level publication work for
  // the most recent transaction. Unrelated pools must not affect this value.
  std::uint64_t last_publication_candidate_inspections_ = 0;
  // Collision-safe canonical equality is performed only inside matching
  // signature buckets. This counts those comparisons for the most recent
  // publication transaction, independently of generic retention work.
  std::uint64_t last_duplicate_equality_checks_ = 0;
  // Cumulative, mutex-protected instrumentation. Each nonempty batch
  // rejection transaction performs exactly one sort/merge/truncate operation;
  // direct single-record lower-bound insertion is intentionally excluded.
  std::uint64_t rejection_batch_merges_ = 0;
  // Cumulative, mutex-protected source-private instrumentation. One nonempty
  // shared-request admission transaction commits exactly one increment with
  // its authoritative publication, regardless of candidate count or outcome.
  std::uint64_t shared_request_policy_normalizations_ = 0;

  friend class CandidateStorePinLease;
};

}  // namespace apgar::candidates

#endif  // APGAR_CANDIDATES_CANDIDATE_STORE_H_
