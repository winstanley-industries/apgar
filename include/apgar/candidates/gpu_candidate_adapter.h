#ifndef APGAR_CANDIDATES_GPU_CANDIDATE_ADAPTER_H_
#define APGAR_CANDIDATES_GPU_CANDIDATE_ADAPTER_H_

#include "apgar/candidates/route_candidate.h"
#include "apgar/gpu/planar_router.h"

namespace apgar::candidates {

// GPU evidence must remain inside its validated batch/result envelope. A
// successful item is an opaque capability sealed only by the host validator;
// the public batch fields provide consistency context but cannot authenticate
// a route by themselves. This adapter binds the seal, batch, query, policy,
// generator, backend, device, and route associations before deriving CUDA
// provenance. A copied sealed item remains valid, while caller-fabricated or
// mutated envelope metadata cannot create successful GPU evidence.
[[nodiscard]] CandidateDraftBuildResult BuildGeneratedCandidateFromGpuBatchItem(
    const board_ir::BoardSnapshot& board, const geometry_compiler::CompiledBoard& compiled_board,
    const gpu::PlanarCandidateBatchQuery& query,
    const routing::NormalizedCandidateGenerationPolicy& normalized_policy,
    const gpu::PlanarCandidateBatch& batch, const gpu::PlanarCandidateBatchItem& item);

}  // namespace apgar::candidates

#endif  // APGAR_CANDIDATES_GPU_CANDIDATE_ADAPTER_H_
