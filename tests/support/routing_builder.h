#ifndef APGAR_TESTS_SUPPORT_ROUTING_BUILDER_H_
#define APGAR_TESTS_SUPPORT_ROUTING_BUILDER_H_

#include <cstdlib>
#include <string>
#include <utility>
#include <variant>

#include "apgar/board_ir/board.h"
#include "apgar/geometry_compiler/compiled_board.h"
#include "apgar/routing/planar_route.h"
#include "apgar/tooling/runfiles.h"
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

[[nodiscard]] inline routing::CpuRouteRequest RequestForNet(const board_ir::BoardSnapshot& board,
                                                            board_ir::EntityRef net,
                                                            board_ir::LayerId start_layer = 0,
                                                            board_ir::LayerId goal_layer = 0) {
  const board_ir::Net* routed_net = board.FindNet(net);
  EXPECT_NE(routed_net, nullptr);
  if (routed_net != nullptr) {
    EXPECT_EQ(routed_net->terminals.size(), 2U);
  }
  if (routed_net == nullptr || routed_net->terminals.size() != 2) {
    std::abort();
  }
  const board_ir::Terminal* start = board.FindTerminal(routed_net->terminals[0]);
  const board_ir::Terminal* goal = board.FindTerminal(routed_net->terminals[1]);
  EXPECT_NE(start, nullptr);
  EXPECT_NE(goal, nullptr);
  if (start == nullptr || goal == nullptr) {
    std::abort();
  }
  return routing::CpuRouteRequest{
      .net = net,
      .start = start->center,
      .goal = goal->center,
      .start_layer = start_layer,
      .goal_layer = goal_layer,
      .candidate_policy = {},
  };
}

[[nodiscard]] inline geometry_compiler::CompiledBoard CompilePreparedNet(
    const board_ir::BoardSnapshot& board, board_ir::EntityRef net,
    geometry_compiler::CompilerProfile compiler_profile) {
  board_ir::RoutingProfile routing_profile = board.data().routing_profile;
  routing_profile.net = net;
  board_ir::RoutingProfilePreparationResult preparation =
      board_ir::PrepareRoutingProfile(board, std::move(routing_profile));
  EXPECT_TRUE(std::holds_alternative<board_ir::PreparedRoutingProfile>(preparation));
  if (!std::holds_alternative<board_ir::PreparedRoutingProfile>(preparation)) {
    std::abort();
  }
  geometry_compiler::CompileResult result = geometry_compiler::CompileBoard(
      board, std::move(compiler_profile),
      std::get<board_ir::PreparedRoutingProfile>(std::move(preparation)));
  EXPECT_TRUE(std::holds_alternative<geometry_compiler::CompiledBoard>(result));
  if (!std::holds_alternative<geometry_compiler::CompiledBoard>(result)) {
    std::abort();
  }
  return std::get<geometry_compiler::CompiledBoard>(std::move(result));
}

[[nodiscard]] inline std::string ReadFixture(
    const std::string& workspace_path = "tests/fixtures/m1_exactness.kicad_pcb") {
  return tooling::ReadRunfile(workspace_path).value_or(std::string{});
}

}  // namespace apgar::test_support

#endif  // APGAR_TESTS_SUPPORT_ROUTING_BUILDER_H_
