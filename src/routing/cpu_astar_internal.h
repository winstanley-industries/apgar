#ifndef APGAR_SRC_ROUTING_CPU_ASTAR_INTERNAL_H_
#define APGAR_SRC_ROUTING_CPU_ASTAR_INTERNAL_H_

#include <span>
#include <vector>

#include "apgar/routing/cpu_astar.h"

namespace apgar::routing {

// Allocation-free validation seam for internal batch callers that hold the
// exact normalization result produced for `request` during admission. This is
// intentionally not part of the public routing API: supplying a normalization
// result from another request is a caller invariant violation.
[[nodiscard]] std::optional<RouteFailure> ValidateReconstructedRouteWithNormalizedPolicy(
    const board_ir::BoardSnapshot& board, const geometry_compiler::CompiledBoard& compiled_board,
    const CpuRouteRequest& request, const NormalizedCandidateGenerationPolicy& normalized_policy,
    std::span<const LayerSegment> segments);

namespace internal {

// Runs the production CPU reference against a caller-owned, read-only copy of
// the compiled tile payload. This source-private seam supports corruption and
// differential tests without exposing a capability to mutate CompiledBoard.
// It deliberately does not mint producer evidence; only the public production
// entrypoint can seal a successful route.
[[nodiscard]] CpuRouteResult RouteWithCpuAStarUsingTileView(
    const board_ir::BoardSnapshot& board, const geometry_compiler::CompiledBoard& compiled_board,
    const CpuRouteRequest& request, std::span<const geometry_compiler::SparseTile> tiles);

// Source-private exact-path fixture seam. It runs the real public CPU A* entry
// point against a read-only restricted subset of the authentic compiled view,
// so successful routes receive normal producer evidence without any resealing
// or caller-authored route payload. Exact route validation still runs first.
[[nodiscard]] CpuRouteResult RouteWithCpuAStarUsingRestrictedTileViewForTesting(
    const board_ir::BoardSnapshot& board, const geometry_compiler::CompiledBoard& compiled_board,
    const CpuRouteRequest& request, std::span<const geometry_compiler::SparseTile> tiles);

}  // namespace internal

}  // namespace apgar::routing

#endif  // APGAR_SRC_ROUTING_CPU_ASTAR_INTERNAL_H_
