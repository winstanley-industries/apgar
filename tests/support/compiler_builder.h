#ifndef APGAR_TESTS_SUPPORT_COMPILER_BUILDER_H_
#define APGAR_TESTS_SUPPORT_COMPILER_BUILDER_H_

#include <vector>

#include "apgar/board_ir/board.h"
#include "apgar/geometry_compiler/compiled_board.h"

namespace apgar::test_support {

[[nodiscard]] inline geometry_compiler::CompilerProfile DefaultCompilerProfile(
    std::vector<board_ir::LayerId> layers = {0, 31}) {
  using board_ir::AxisAlignedBox64;
  using board_ir::Point64;
  using geometry_compiler::ActiveRegion;

  std::vector<ActiveRegion> regions;
  for (board_ir::LayerId layer : layers) {
    regions.push_back(ActiveRegion{
        .layer = layer,
        .bounds =
            AxisAlignedBox64{.min = Point64{.x = -20, .y = -30}, .max = Point64{.x = 120, .y = 30}},
    });
  }
  return geometry_compiler::CompilerProfile{
      .schema_version = geometry_compiler::kCompilerProfileSchemaVersion,
      .lattice_origin = Point64{.x = 0, .y = 0},
      .lattice_step = 10,
      .tile_width_nodes = 4,
      .tile_height_nodes = 4,
      .compilation_roi =
          AxisAlignedBox64{.min = Point64{.x = -20, .y = -30}, .max = Point64{.x = 120, .y = 30}},
      .active_regions = std::move(regions),
      .heading_mask = board_ir::kM1HeadingMask,
      .costs =
          geometry_compiler::DeterministicCosts{
              .orthogonal_step = 10,
              .diagonal_step = 14,
              .bend = 3,
          },
  };
}

}  // namespace apgar::test_support

#endif  // APGAR_TESTS_SUPPORT_COMPILER_BUILDER_H_
