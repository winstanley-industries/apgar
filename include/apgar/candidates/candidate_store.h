#ifndef APGAR_CANDIDATES_CANDIDATE_STORE_H_
#define APGAR_CANDIDATES_CANDIDATE_STORE_H_

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "apgar/board_ir/board.h"
#include "apgar/candidates/route_candidate.h"

namespace apgar::candidates {

struct CandidateStoreConfig {
  std::uint64_t maximum_candidates_per_net = 0;
  std::uint64_t maximum_candidate_bytes_per_net = 0;
  std::uint64_t maximum_rejection_records = 0;
  // Bounds one call to the public batch rejection-ingestion seam independently
  // of the retained-record cap.
  std::uint64_t maximum_rejection_items_per_transaction = 1'024;
  // Admission-transaction limits are independent of retained-pool limits. The
  // defaults bound existing call sites while allowing the Phase 3 k <= 128
  // bakeoff; production owners should set workload-specific values.
  std::uint64_t maximum_admission_items_per_transaction = 1'024;
  std::uint64_t maximum_admission_input_bytes_per_transaction = 64U * 1024U * 1024U;
  std::uint64_t maximum_admission_work_units_per_transaction = 100'000'000;

  friend bool operator==(const CandidateStoreConfig&, const CandidateStoreConfig&) = default;
};

enum class CandidateStoreErrorCode : std::uint8_t {
  kInvalidConfiguration = 0,
  kMissingCandidate = 1,
  kInvalidPinOwner = 2,
};

struct CandidateStoreError {
  CandidateStoreErrorCode code;
  std::string detail;

  friend bool operator==(const CandidateStoreError&, const CandidateStoreError&) = default;
};

using StoredCandidate = std::shared_ptr<const RouteCandidate>;
using CandidateStoreAdmissionResult = std::variant<StoredCandidate, CandidateRejection>;

struct CandidateAdmissionItem {
  routing::PlanarRouteRequest request;
  GeneratedRouteCandidate generated;
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

class CandidateStore {
 public:
  explicit CandidateStore(CandidateStoreConfig config);
  CandidateStore(const CandidateStore&) = delete;
  CandidateStore& operator=(const CandidateStore&) = delete;

  [[nodiscard]] const CandidateStoreConfig& config() const noexcept { return config_; }
  [[nodiscard]] bool valid() const noexcept;

  // Admission consumes explicit caller-owned rvalues. Binding these seams does
  // not copy any policy/geometry/resource bulk; transaction preflight runs on
  // the transferred object before elements are moved into internal scratch.
  [[nodiscard]] CandidateStoreAdmissionResult Admit(const CandidateAdmissionContext& context,
                                                    GeneratedRouteCandidate&& generated);
  [[nodiscard]] std::vector<CandidateStoreAdmissionResult> AdmitBatch(
      const CandidateAdmissionContext& context, std::vector<GeneratedRouteCandidate>&& generated);
  [[nodiscard]] std::vector<CandidateStoreAdmissionResult> AdmitBatch(
      const board_ir::BoardSnapshot& board, const geometry_compiler::CompiledBoard& compiled_board,
      std::vector<CandidateAdmissionItem>&& items);

  [[nodiscard]] std::vector<StoredCandidate> Enumerate(board_ir::EntityRef net) const;
  [[nodiscard]] std::vector<CandidateRejection> Rejections() const;
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
  [[nodiscard]] bool IsPinned(CandidateId candidate_id) const;

  // Reapplies the deterministic retention order. Pins are always preserved;
  // a failure leaves the current pool unchanged.
  [[nodiscard]] std::optional<CandidateRejection> Prune(board_ir::EntityRef net);

 private:
  friend class CandidateStoreTestPeer;

  using PinKey = std::pair<std::uint64_t, CandidateId>;
  struct NetLess {
    [[nodiscard]] bool operator()(board_ir::EntityRef left,
                                  board_ir::EntityRef right) const noexcept {
      return std::pair{left.id, left.generation} < std::pair{right.id, right.generation};
    }
  };
  using CandidatePool = std::vector<StoredCandidate>;

  [[nodiscard]] bool IsPinnedLocked(CandidateId candidate_id) const;
  void RetainRejectionLocked(const CandidateRejection& rejection);
  [[nodiscard]] CandidateStoreAdmissionResult PublishAcceptedLocked(RouteCandidate candidate);
  [[nodiscard]] std::vector<CandidateStoreAdmissionResult> PublishAcceptedBatchLocked(
      std::vector<RouteCandidate> candidates);
  [[nodiscard]] std::optional<CandidateRejection> PruneLocked(board_ir::EntityRef net,
                                                              CandidateId newest_id);

  CandidateStoreConfig config_;
  mutable std::mutex mutex_;
  std::map<board_ir::EntityRef, CandidatePool, NetLess> pools_;
  std::map<CandidateId, StoredCandidate> candidate_id_index_;
  std::vector<CandidateRejection> rejections_;
  std::set<PinKey> pins_;
  std::map<CandidateId, std::uint64_t> pin_counts_;
  std::optional<CandidateAssociations> bound_associations_;
  // Deterministic test instrumentation: candidate-level publication work for
  // the most recent transaction. Unrelated pools must not affect this value.
  std::uint64_t last_publication_candidate_inspections_ = 0;
};

}  // namespace apgar::candidates

#endif  // APGAR_CANDIDATES_CANDIDATE_STORE_H_
