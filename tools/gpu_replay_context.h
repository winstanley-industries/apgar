#ifndef APGAR_TOOLS_GPU_REPLAY_CONTEXT_H_
#define APGAR_TOOLS_GPU_REPLAY_CONTEXT_H_

#include <string>
#include <variant>

#include "apgar/board_ir/board.h"
#include "apgar/geometry_compiler/compiled_board.h"
#include "apgar/gpu/planar_router.h"

namespace apgar::tooling {

using GpuReplayDeviceContextV1 = std::variant<gpu::DeviceCompiledBoardV1, std::string>;

[[nodiscard]] GpuReplayDeviceContextV1 PrepareDefaultGpuReplayDeviceContextV1(
    const board_ir::BoardSnapshot& board, const geometry_compiler::CompiledBoard& compiled_board);

}  // namespace apgar::tooling

#endif  // APGAR_TOOLS_GPU_REPLAY_CONTEXT_H_
