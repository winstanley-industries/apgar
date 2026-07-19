#ifndef APGAR_TESTS_SUPPORT_ROUTING_BUILDER_H_
#define APGAR_TESTS_SUPPORT_ROUTING_BUILDER_H_

#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <variant>

#include "apgar/board_ir/board.h"
#include "apgar/geometry_compiler/compiled_board.h"
#include "apgar/routing/planar_route.h"
#include "tests/support/board_builder.h"
#include "tests/support/google_test.h"

namespace apgar::test_support {

[[nodiscard]] inline board_ir::BoardSnapshot Snapshot(
    board_ir::BoardData data = ValidM1BoardData()) {
  board_ir::BoardCreationResult result = board_ir::CreateBoardSnapshot(std::move(data));
  EXPECT_TRUE(std::holds_alternative<board_ir::BoardSnapshot>(result));
  if (!std::holds_alternative<board_ir::BoardSnapshot>(result)) {
    std::abort();
  }
  return std::get<board_ir::BoardSnapshot>(std::move(result));
}

[[nodiscard]] inline geometry_compiler::CompiledBoard Compile(
    const board_ir::BoardSnapshot& board, geometry_compiler::CompilerProfile profile) {
  geometry_compiler::CompileResult result =
      geometry_compiler::CompileBoard(board, std::move(profile));
  EXPECT_TRUE(std::holds_alternative<geometry_compiler::CompiledBoard>(result))
      << (std::holds_alternative<geometry_compiler::CompileError>(result)
              ? std::get<geometry_compiler::CompileError>(result).detail
              : "");
  if (!std::holds_alternative<geometry_compiler::CompiledBoard>(result)) {
    std::abort();
  }
  return std::get<geometry_compiler::CompiledBoard>(std::move(result));
}

[[nodiscard]] inline routing::CpuRouteRequest TwoTerminalRequest(
    const board_ir::BoardSnapshot& board, board_ir::LayerId start_layer = 0,
    board_ir::LayerId goal_layer = 0) {
  routing::TwoTerminalRequestResult result =
      routing::BuildTwoTerminalRouteRequest(board, start_layer, goal_layer);
  EXPECT_TRUE(std::holds_alternative<routing::CpuRouteRequest>(result));
  if (!std::holds_alternative<routing::CpuRouteRequest>(result)) {
    std::abort();
  }
  return std::get<routing::CpuRouteRequest>(std::move(result));
}

[[nodiscard]] inline std::string ReadFixture(
    const std::string& workspace_path = "tests/fixtures/m1_exactness.kicad_pcb") {
  const char* test_srcdir = std::getenv("TEST_SRCDIR");
  const char* test_workspace = std::getenv("TEST_WORKSPACE");
  if (test_srcdir == nullptr || test_workspace == nullptr) {
    return {};
  }
  const std::string path = std::string(test_srcdir) + "/" + test_workspace + "/" + workspace_path;
  std::ifstream input(path);
  if (!input) {
    return {};
  }
  return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

}  // namespace apgar::test_support

#endif  // APGAR_TESTS_SUPPORT_ROUTING_BUILDER_H_
