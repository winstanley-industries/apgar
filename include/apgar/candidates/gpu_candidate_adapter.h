#ifndef APGAR_CANDIDATES_GPU_CANDIDATE_ADAPTER_H_
#define APGAR_CANDIDATES_GPU_CANDIDATE_ADAPTER_H_

#include <functional>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

#include "apgar/candidates/route_candidate.h"
#include "apgar/gpu/planar_router.h"

namespace apgar::candidates {

// Canonical mappings shared by provenance construction and diagnostics. An
// unknown generator or incomplete/non-CUDA descriptor has no v1 mapping.
[[nodiscard]] std::optional<CandidateGeneratorKind> CandidateGeneratorForPlanarGenerator(
    gpu::PlanarGenerator generator) noexcept;
[[nodiscard]] std::optional<std::string> CudaCandidateDeviceClass(
    const gpu::BackendMetadata& metadata);

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

// Bounded batch form. It proves item membership once through a sorted query-ID
// index, then retains every per-item seal, association, and producer check used
// by the single-item adapter. Results preserve request order.
[[nodiscard]] GpuCandidateBatchBuildResult BuildGeneratedCandidatesFromGpuBatchItems(
    const board_ir::BoardSnapshot& board, const geometry_compiler::CompiledBoard& compiled_board,
    const gpu::PlanarCandidateBatch& batch,
    std::span<const GpuCandidateBatchBuildRequest> requests);

}  // namespace apgar::candidates

#endif  // APGAR_CANDIDATES_GPU_CANDIDATE_ADAPTER_H_
