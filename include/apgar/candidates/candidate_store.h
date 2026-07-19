#ifndef APGAR_CANDIDATES_CANDIDATE_STORE_H_
#define APGAR_CANDIDATES_CANDIDATE_STORE_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "apgar/board_ir/board.h"
#include "apgar/candidates/route_candidate.h"

namespace apgar::candidates {

struct CandidateStoreConfig {
  std::uint64_t maximum_candidates_per_net = 0;
  std::uint64_t maximum_candidate_bytes_per_net = 0;
  std::uint64_t maximum_rejection_records = 0;

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

  [[nodiscard]] CandidateStoreAdmissionResult Admit(const CandidateAdmissionContext& context,
                                                    GeneratedRouteCandidate generated);
  [[nodiscard]] std::vector<CandidateStoreAdmissionResult> AdmitBatch(
      const CandidateAdmissionContext& context, std::vector<GeneratedRouteCandidate> generated);
  [[nodiscard]] std::vector<CandidateStoreAdmissionResult> AdmitBatch(
      const board_ir::BoardSnapshot& board, const geometry_compiler::CompiledBoard& compiled_board,
      std::vector<CandidateAdmissionItem> items);

  [[nodiscard]] std::vector<StoredCandidate> Enumerate(board_ir::EntityRef net) const;
  [[nodiscard]] std::vector<CandidateRejection> Rejections() const;
  [[nodiscard]] std::optional<std::uint64_t> CandidateBytes(board_ir::EntityRef net) const;

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

  [[nodiscard]] bool IsPinnedLocked(CandidateId candidate_id) const;
  void RetainRejectionLocked(CandidateRejection rejection);
  [[nodiscard]] CandidateStoreAdmissionResult PublishAcceptedLocked(RouteCandidate candidate);
  [[nodiscard]] std::optional<CandidateRejection> PruneLocked(board_ir::EntityRef net,
                                                              CandidateId newest_id);

  CandidateStoreConfig config_;
  mutable std::mutex mutex_;
  std::vector<StoredCandidate> candidates_;
  std::vector<CandidateRejection> rejections_;
  std::vector<PinKey> pins_;
  std::optional<CandidateAssociations> bound_associations_;
};

}  // namespace apgar::candidates

#endif  // APGAR_CANDIDATES_CANDIDATE_STORE_H_
