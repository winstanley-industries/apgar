#include <atomic>
#include <cstdint>
#include <memory>
#include <utility>
#include <variant>

#include "apgar/board_ir/board.h"
#include "apgar/geometry_compiler/compiled_board.h"
#include "apgar/gpu/cuda_backend.h"
#include "apgar/gpu/planar_router.h"
#include "apgar/routing/cpu_astar.h"
#include "tests/support/board_builder.h"
#include "tests/support/compiler_builder.h"
#include "tests/support/google_test.h"

namespace apgar::gpu {
namespace {

using board_ir::AxisAlignedBox64;
using board_ir::BoardCreationResult;
using board_ir::BoardData;
using board_ir::BoardSnapshot;
using board_ir::Point64;
using geometry_compiler::ActiveRegion;
using geometry_compiler::CompiledBoard;
using geometry_compiler::CompileError;
using geometry_compiler::CompilerProfile;
using routing::CpuRoute;
using routing::CpuRouteRequest;
using routing::CpuRouteResult;

[[nodiscard]] BoardSnapshot Snapshot(BoardData data = test_support::ValidM1BoardData()) {
  BoardCreationResult result = board_ir::CreateBoardSnapshot(std::move(data));
  EXPECT_TRUE(std::holds_alternative<BoardSnapshot>(result));
  return std::get<BoardSnapshot>(std::move(result));
}

[[nodiscard]] CompiledBoard Compile(const BoardSnapshot& board, CompilerProfile profile) {
  geometry_compiler::CompileResult result =
      geometry_compiler::CompileBoard(board, std::move(profile));
  EXPECT_TRUE(std::holds_alternative<CompiledBoard>(result))
      << (std::holds_alternative<CompileError>(result) ? std::get<CompileError>(result).detail
                                                       : "");
  return std::get<CompiledBoard>(std::move(result));
}

[[nodiscard]] CpuRouteRequest Request(const BoardSnapshot& board, board_ir::LayerId layer = 0) {
  const board_ir::Net* net = board.FindNet(board.data().routing_profile.net);
  EXPECT_NE(net, nullptr);
  const board_ir::Terminal* first = board.FindTerminal(net->terminals[0]);
  const board_ir::Terminal* second = board.FindTerminal(net->terminals[1]);
  EXPECT_NE(first, nullptr);
  EXPECT_NE(second, nullptr);
  return CpuRouteRequest{
      .net = net->ref,
      .start = first->center,
      .goal = second->center,
      .start_layer = layer,
      .goal_layer = layer,
  };
}

[[nodiscard]] PlanarGpuRouteResult Route(const BoardSnapshot& board, const CompiledBoard& compiled,
                                         const CpuRouteRequest& request, PlanarRoutePolicy policy) {
  std::unique_ptr<IPlanarRouteBackend> backend = CreateCudaPlanarRouteBackend();
  EXPECT_NE(backend, nullptr);
  return RouteWithPlanarGpuBackend(board, compiled, request, policy, *backend);
}

void ExpectDifferentialSuccess(const BoardSnapshot& board, const CompiledBoard& compiled,
                               const CpuRouteRequest& request, PlanarGenerator generator) {
  const CpuRouteResult cpu = routing::RouteWithCpuAStar(board, compiled, request);
  ASSERT_TRUE(std::holds_alternative<CpuRoute>(cpu));
  const PlanarRoutePolicy policy{.generator = generator};
  const PlanarGpuRouteResult first = Route(board, compiled, request, policy);
  const PlanarGpuRouteResult second = Route(board, compiled, request, policy);
  ASSERT_TRUE(std::holds_alternative<PlanarGpuRoute>(first))
      << (std::holds_alternative<PlanarGpuFailure>(first) ? std::get<PlanarGpuFailure>(first).detail
                                                          : "");
  ASSERT_TRUE(std::holds_alternative<PlanarGpuRoute>(second))
      << (std::holds_alternative<PlanarGpuFailure>(second)
              ? std::get<PlanarGpuFailure>(second).detail
              : "");
  const PlanarGpuRoute& first_route = std::get<PlanarGpuRoute>(first);
  const PlanarGpuRoute& second_route = std::get<PlanarGpuRoute>(second);
  EXPECT_EQ(first_route.total_cost, std::get<CpuRoute>(cpu).total_cost);
  EXPECT_EQ(first_route.total_cost, second_route.total_cost);
  EXPECT_EQ(first_route.lattice_path, second_route.lattice_path);
  EXPECT_EQ(first_route.segments, second_route.segments);
  EXPECT_EQ(first_route.backend.backend, "cuda");
  EXPECT_EQ(first_route.backend.compute_capability_major, 12U);
  EXPECT_GT(first_route.telemetry.examined_work, 0U);
  EXPECT_GT(first_route.telemetry.peak_device_bytes, 0U);
}

TEST(CudaPlanarRouteTest, FrontierAndSweepMatchCpuCostAndRepeatExactly) {
  const BoardSnapshot board = Snapshot();
  const CompiledBoard compiled = Compile(board, test_support::DefaultCompilerProfile({0}));
  const CpuRouteRequest request = Request(board);

  ExpectDifferentialSuccess(board, compiled, request, PlanarGenerator::kBucketedFrontier);
  ExpectDifferentialSuccess(board, compiled, request, PlanarGenerator::kHeadingAwareSweep);
}

TEST(CudaPlanarRouteTest, SupportsArbitraryValidDeterministicCosts) {
  BoardData data = test_support::ValidM1BoardData();
  data.obstacles.clear();
  data.terminals[1].center = Point64{.x = 30, .y = -30};
  data.terminals[1].connection_region =
      AxisAlignedBox64{.min = Point64{.x = 20, .y = -40}, .max = Point64{.x = 40, .y = -20}};
  const BoardSnapshot board = Snapshot(std::move(data));
  CompilerProfile profile = test_support::DefaultCompilerProfile({0});
  profile.compilation_roi =
      AxisAlignedBox64{.min = Point64{.x = 0, .y = -30}, .max = Point64{.x = 40, .y = 20}};
  profile.active_regions.clear();
  for (Point64 point : {
           Point64{.x = 0, .y = 0},
           Point64{.x = 10, .y = 0},
           Point64{.x = 10, .y = 10},
           Point64{.x = 20, .y = -20},
           Point64{.x = 20, .y = -10},
           Point64{.x = 20, .y = 20},
           Point64{.x = 30, .y = -30},
           Point64{.x = 30, .y = -10},
           Point64{.x = 30, .y = 10},
           Point64{.x = 40, .y = 0},
       }) {
    profile.active_regions.push_back(
        ActiveRegion{.layer = 0, .bounds = AxisAlignedBox64{.min = point, .max = point}});
  }
  profile.costs = geometry_compiler::DeterministicCosts{
      .orthogonal_step = 10,
      .diagonal_step = 1,
      .bend = 10,
  };
  const CompiledBoard compiled = Compile(board, profile);

  ExpectDifferentialSuccess(board, compiled, Request(board), PlanarGenerator::kBucketedFrontier);
  ExpectDifferentialSuccess(board, compiled, Request(board), PlanarGenerator::kHeadingAwareSweep);
}

TEST(CudaPlanarRouteTest, DistinguishesDisconnectedUnsupportedCancelledAndResourceOutcomes) {
  const BoardSnapshot board = Snapshot();
  CompilerProfile profile = test_support::DefaultCompilerProfile({0});
  profile.heading_mask = static_cast<board_ir::HeadingMask>(board_ir::Heading::kHorizontal);
  profile.compilation_roi =
      AxisAlignedBox64{.min = Point64{.x = 0, .y = 0}, .max = Point64{.x = 100, .y = 0}};
  profile.active_regions = {ActiveRegion{.layer = 0, .bounds = profile.compilation_roi}};
  const CompiledBoard disconnected_board = Compile(board, profile);
  const PlanarGpuRouteResult disconnected =
      Route(board, disconnected_board, Request(board), PlanarRoutePolicy{});
  ASSERT_TRUE(std::holds_alternative<PlanarGpuFailure>(disconnected));
  EXPECT_EQ(std::get<PlanarGpuFailure>(disconnected).code, PlanarGpuFailureCode::kDisconnected);

  const CompiledBoard compiled = Compile(board, test_support::DefaultCompilerProfile());
  CpuRouteRequest through_via = Request(board);
  through_via.goal_layer = 31;
  const PlanarGpuRouteResult unsupported = Route(board, compiled, through_via, PlanarRoutePolicy{});
  ASSERT_TRUE(std::holds_alternative<PlanarGpuFailure>(unsupported));
  EXPECT_EQ(std::get<PlanarGpuFailure>(unsupported).code, PlanarGpuFailureCode::kUnsupported);

  std::atomic_bool cancellation = true;
  PlanarRoutePolicy cancelled_policy;
  cancelled_policy.cancellation = &cancellation;
  const PlanarGpuRouteResult cancelled = Route(board, compiled, Request(board), cancelled_policy);
  ASSERT_TRUE(std::holds_alternative<PlanarGpuFailure>(cancelled));
  EXPECT_EQ(std::get<PlanarGpuFailure>(cancelled).code, PlanarGpuFailureCode::kCancelled);

  PlanarRoutePolicy memory_policy;
  memory_policy.maximum_device_bytes = 1;
  const PlanarGpuRouteResult memory = Route(board, compiled, Request(board), memory_policy);
  ASSERT_TRUE(std::holds_alternative<PlanarGpuFailure>(memory));
  EXPECT_EQ(std::get<PlanarGpuFailure>(memory).code, PlanarGpuFailureCode::kResourceExhausted);

  PlanarRoutePolicy round_policy;
  round_policy.maximum_rounds = 1;
  const PlanarGpuRouteResult rounds = Route(board, compiled, Request(board), round_policy);
  ASSERT_TRUE(std::holds_alternative<PlanarGpuFailure>(rounds));
  EXPECT_EQ(std::get<PlanarGpuFailure>(rounds).code, PlanarGpuFailureCode::kResourceExhausted);
}

TEST(CudaPlanarRouteTest, InjectedPredecessorCycleIsAnInvariantFailure) {
  const BoardSnapshot board = Snapshot();
  const CompiledBoard compiled = Compile(board, test_support::DefaultCompilerProfile({0}));
  PlanarRoutePolicy policy;
  policy.fault_injection = KernelFaultInjection::kGoalPredecessorSelfCycle;

  const PlanarGpuRouteResult result = Route(board, compiled, Request(board), policy);

  ASSERT_TRUE(std::holds_alternative<PlanarGpuFailure>(result));
  EXPECT_EQ(std::get<PlanarGpuFailure>(result).code, PlanarGpuFailureCode::kInternalInvariant);
}

}  // namespace
}  // namespace apgar::gpu
