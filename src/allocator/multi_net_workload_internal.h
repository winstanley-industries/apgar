#ifndef APGAR_SRC_ALLOCATOR_MULTI_NET_WORKLOAD_INTERNAL_H_
#define APGAR_SRC_ALLOCATOR_MULTI_NET_WORKLOAD_INTERNAL_H_

#include <cstdint>
#include <span>

#include "apgar/allocator/multi_net_workload.h"
#include "apgar/board_ir/board.h"

namespace apgar::allocator::internal {

enum class MultiNetWorkloadFaultForTesting : std::uint8_t {
  kNone = 0,
  kBadAlloc = 1,
  kLengthError = 2,
};

void SetMultiNetWorkloadFaultForTesting(MultiNetWorkloadFaultForTesting fault) noexcept;

struct MultiNetWorkloadChecksumRecordV1 {
  board_ir::EntityRef net;
  std::uint64_t routing_profile_fingerprint = 0;
  std::uint64_t rule_bucket_identity = 0;
  board_ir::Point64 start;
  board_ir::Point64 goal;
  board_ir::LayerId start_layer = 0;
  board_ir::LayerId goal_layer = 0;
};

// Source-private representation encoder used by the production workload and
// a representation-level golden compatibility fixture.
[[nodiscard]] std::uint64_t ComputeMultiNetWorkloadChecksumV1(
    std::uint32_t schema_version, std::uint64_t board_content_hash,
    std::uint64_t compiler_profile_fingerprint, std::uint32_t compiler_version,
    std::span<const MultiNetWorkloadChecksumRecordV1> nets) noexcept;

// Rebuilds the production workload checksum from the retained live routing
// contexts instead of trusting MultiNetWorkload::workload_checksum().
[[nodiscard]] std::uint64_t RecomputeMultiNetWorkloadChecksumV1(
    const MultiNetWorkload& workload) noexcept;

}  // namespace apgar::allocator::internal

#endif  // APGAR_SRC_ALLOCATOR_MULTI_NET_WORKLOAD_INTERNAL_H_
