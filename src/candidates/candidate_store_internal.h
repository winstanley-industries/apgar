#ifndef APGAR_SRC_CANDIDATES_CANDIDATE_STORE_INTERNAL_H_
#define APGAR_SRC_CANDIDATES_CANDIDATE_STORE_INTERNAL_H_

#include <compare>
#include <cstdint>
#include <map>
#include <optional>
#include <vector>

#include "apgar/candidates/candidate_store.h"

namespace apgar::candidates::internal {

enum class SignatureBucketDuplicate : std::uint8_t {
  kNone = 0,
  kGeometry = 1,
  kResources = 2,
};

// Callers have already matched the corresponding signature bucket. These
// helpers deliberately ignore the 128-bit bucket key and perform the required
// collision-safe context plus canonical-payload equality check.
[[nodiscard]] bool GeometryEqualWithinSignatureBucket(const RouteCandidate& left,
                                                      const RouteCandidate& right) noexcept;
[[nodiscard]] bool ResourcesEqualWithinSignatureBucket(const RouteCandidate& left,
                                                       const RouteCandidate& right) noexcept;

// Complete pair classification after lookup-key matching. Geometry has stable
// precedence over resource equality, matching publication behavior.
[[nodiscard]] SignatureBucketDuplicate ClassifySignatureBucketDuplicate(
    const RouteCandidate& left, const RouteCandidate& right, bool geometry_bucket_matched,
    bool resource_bucket_matched) noexcept;

// Pure checked-arithmetic decisions shared with production and source-private
// adversarial tests. They do not require corrupting an immutable candidate.
[[nodiscard]] std::strong_ordering CompareTotalStepCount(const CandidateMetrics& left,
                                                         const CandidateMetrics& right) noexcept;
[[nodiscard]] std::optional<std::uint64_t> CheckedLogicalByteSum(std::uint64_t accumulated,
                                                                 std::uint64_t next) noexcept;
[[nodiscard]] std::optional<std::uint64_t> QuantizeOverlapRatioPpmV1(
    std::uint64_t numerator, std::uint64_t denominator) noexcept;

struct RetentionSelection {
  std::vector<StoredCandidate> retained;
  std::vector<StoredCandidate> pruned;
  bool pinned_budget_failure = false;
};

// Source-private invocation of the production retention reducer. It accepts
// immutable candidate handles and explicit pin counts, so adversarial tables
// never need to corrupt a CandidateStore's private session state.
[[nodiscard]] RetentionSelection SelectRetentionForPool(
    std::vector<StoredCandidate> pool, const CandidateStoreConfig& config,
    const std::map<CandidateId, std::uint64_t>& pin_counts);

// Source-private deterministic fault injection for the allocation-only
// preparation phase immediately before publication commit. Zero fails at the
// next preparation point; positive values count successful points first.
void SetPublicationPreparationFailureCountdownForTesting(
    std::optional<std::uint64_t> countdown) noexcept;

}  // namespace apgar::candidates::internal

#endif  // APGAR_SRC_CANDIDATES_CANDIDATE_STORE_INTERNAL_H_
