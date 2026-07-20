#ifndef APGAR_GPU_CUDA_BACKEND_H_
#define APGAR_GPU_CUDA_BACKEND_H_

#include <memory>

#include "apgar/gpu/planar_router.h"

namespace apgar::gpu {

[[nodiscard]] std::unique_ptr<IPlanarRouteBackend> CreateCudaPlanarRouteBackend();

// The always-linked core authenticates the exact final wrapper constructed by
// this checksum-pinned CUDA factory and marks the resulting prepared view as
// eligible to mint CUDA candidate provenance. Generic/test backends, including
// wrappers that merely report CUDA-looking metadata, are rejected.
[[nodiscard]] PreparedPlanarCompiledViewResult PrepareCudaPlanarCompiledView(
    const board_ir::BoardSnapshot& board, const geometry_compiler::CompiledBoard& compiled_board,
    IPlanarRouteBackend& backend);

}  // namespace apgar::gpu

#endif  // APGAR_GPU_CUDA_BACKEND_H_
