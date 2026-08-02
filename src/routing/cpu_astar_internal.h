#ifndef APGAR_SRC_ROUTING_CPU_ASTAR_INTERNAL_H_
#define APGAR_SRC_ROUTING_CPU_ASTAR_INTERNAL_H_

#include <vector>

#include "apgar/routing/cpu_astar.h"

namespace apgar::routing {

struct CpuRouteProducerEvidence {
  std::uint64_t source_board_content_hash = 0;
  std::uint64_t compiler_profile_fingerprint = 0;
  std::uint32_t compiler_version = 0;
  std::uint64_t routing_profile_fingerprint = 0;
  std::uint64_t rule_bucket_identity = 0;
  std::uint64_t candidate_policy_identity = 0;
  std::uint64_t total_cost = 0;
  std::vector<LayerSegment> segments;
};

// Allocation-free validation seam for internal batch callers that hold the
// exact normalization result produced for `request` during admission. This is
// intentionally not part of the public routing API: supplying a normalization
// result from another request is a caller invariant violation.
[[nodiscard]] std::optional<RouteFailure> ValidateReconstructedRouteWithNormalizedPolicy(
    const board_ir::BoardSnapshot& board, const geometry_compiler::CompiledBoard& compiled_board,
    const CpuRouteRequest& request, const NormalizedCandidateGenerationPolicy& normalized_policy,
    std::span<const LayerSegment> segments);

}  // namespace apgar::routing

#endif  // APGAR_SRC_ROUTING_CPU_ASTAR_INTERNAL_H_
