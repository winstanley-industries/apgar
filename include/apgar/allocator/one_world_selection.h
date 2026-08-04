#ifndef APGAR_ALLOCATOR_ONE_WORLD_SELECTION_H_
#define APGAR_ALLOCATOR_ONE_WORLD_SELECTION_H_

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <variant>
#include <vector>

#include "apgar/allocator/resource_accounting.h"
#include "apgar/board_ir/board.h"
#include "apgar/candidates/route_candidate.h"

namespace apgar::allocator {

inline constexpr std::uint64_t kMaximumOneWorldNetPools = 1'000'000;
inline constexpr std::uint64_t kMaximumOneWorldPoolCandidates = 1'000'000;

// One caller-authored immutable candidate pool for one explicitly selected
// Board IR net. Candidate and pool order are not semantic.
struct OneWorldCandidatePool {
  board_ir::EntityRef net;
  std::span<const candidates::RouteCandidate* const> candidates;
};

struct OneWorldSelectedCandidate {
  board_ir::EntityRef net;
  candidates::CandidateId candidate_id;

  friend bool operator==(const OneWorldSelectedCandidate&,
                         const OneWorldSelectedCandidate&) = default;
};

enum class OneWorldCandidateAbsenceReason : std::uint8_t {
  kEmptyPool = 0,
};

struct OneWorldCandidateAbsence {
  board_ir::EntityRef net;
  OneWorldCandidateAbsenceReason reason = OneWorldCandidateAbsenceReason::kEmptyPool;

  friend bool operator==(const OneWorldCandidateAbsence&,
                         const OneWorldCandidateAbsence&) = default;
};

using OneWorldNetOutcome = std::variant<OneWorldSelectedCandidate, OneWorldCandidateAbsence>;

struct OneWorldSelection {
  ResourceLatticeAssociations associations;
  // Canonically sorted by net entity ID and generation.
  std::vector<OneWorldNetOutcome> nets;
  ResourceAccounting accounting;
  std::uint64_t input_candidate_count = 0;

  friend bool operator==(const OneWorldSelection&, const OneWorldSelection&) = default;
};

enum class OneWorldSelectionErrorCode : std::uint8_t {
  kAssociationMismatch = 0,
  kInvalidLimits = 1,
  kInputBoundExceeded = 2,
  kDuplicateNetPool = 3,
  kUnknownNet = 4,
  kNullCandidate = 5,
  kCandidateNetMismatch = 6,
  kInvalidCandidateIdentity = 7,
  kDuplicateCandidateIdentity = 8,
  kCandidateAssociationMismatch = 9,
  kAccountingFailure = 10,
  kResourceExhausted = 11,
};

struct OneWorldSelectionError {
  OneWorldSelectionErrorCode code = OneWorldSelectionErrorCode::kAssociationMismatch;
  std::string_view invariant_id;
  std::string_view detail;
  std::optional<ResourceAccountingErrorCode> accounting_error_code;

  friend bool operator==(const OneWorldSelectionError&, const OneWorldSelectionError&) = default;
};

struct OneWorldSelectionLimits {
  std::uint64_t maximum_net_pools = kMaximumOneWorldNetPools;
  std::uint64_t maximum_total_candidates = kMaximumOneWorldPoolCandidates;
  ResourceAccountingLimits accounting;

  friend bool operator==(const OneWorldSelectionLimits&, const OneWorldSelectionLimits&) = default;
};

using OneWorldSelectionResult = std::variant<OneWorldSelection, OneWorldSelectionError>;

// Deterministic CPU One-World selection at an explicit zero resource price.
// Each nonempty pool selects the lexicographic minimum of:
//
//   intrinsic base cost, via count, bend count, mathematical total step count,
//   axis-aligned DBU length, diagonal DBU projection, candidate ID.
//
// Candidate ID is the final canonical tie-break. The function neither prepares
// pools nor mutates, regenerates, prices, pins, or retains candidates.
[[nodiscard]] OneWorldSelectionResult SelectOneWorldZeroPrice(
    const board_ir::BoardSnapshot& board, const ResourceCapacityModel& capacities,
    std::span<const OneWorldCandidatePool> pools, OneWorldSelectionLimits limits = {});

}  // namespace apgar::allocator

#endif  // APGAR_ALLOCATOR_ONE_WORLD_SELECTION_H_
