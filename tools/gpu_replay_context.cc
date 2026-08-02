#include "tools/gpu_replay_context.h"

#include <optional>
#include <string>
#include <utility>
#include <variant>

#include "apgar/routing/planar_route.h"

namespace apgar::tooling {

GpuReplayDeviceContextV1 PrepareDefaultGpuReplayDeviceContextV1(
    const board_ir::BoardSnapshot& board, const geometry_compiler::CompiledBoard& compiled_board) {
  if (routing::ValidateCompiledBoardAssociation(board, compiled_board).has_value()) {
    return std::string("replay requires the Board IR default routing context");
  }
  gpu::DeviceCompiledBoardResult device_result =
      gpu::BuildDeviceCompiledBoardV1(board, compiled_board);
  if (!std::holds_alternative<gpu::DeviceCompiledBoardV1>(device_result)) {
    return std::get<gpu::PlanarGpuFailure>(std::move(device_result)).detail;
  }
  return std::get<gpu::DeviceCompiledBoardV1>(std::move(device_result));
}

}  // namespace apgar::tooling
