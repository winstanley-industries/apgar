#include "apgar/routing/cpu_astar.h"

#include <algorithm>
#include <array>
#include <string>
#include <utility>
#include <variant>

#include "apgar/adapters/kicad_fixture.h"
#include "apgar/board_ir/board.h"
#include "apgar/geometry_compiler/compiled_board.h"
#include "tests/support/board_builder.h"
#include "tests/support/compiler_builder.h"
#include "tests/support/google_test.h"
#include "tests/support/routing_builder.h"

namespace apgar::geometry_compiler {

class CompiledBoardTestPeer {
 public:
  static bool AddLegalEdge(CompiledBoard& board, board_ir::LayerId layer, std::int64_t lattice_x,
                           std::int64_t lattice_y, Direction direction) {
    for (SparseTile& tile : board.tiles_) {
      if (tile.key.layer != layer) {
        continue;
      }
      for (CompiledNode& node : tile.nodes) {
        const LatticeIndex index = GlobalLatticeIndex(board.profile_, tile.key, node.local_index);
        if (index.x == lattice_x && index.y == lattice_y) {
          node.legal_edges |= MaskFor(direction);
          return true;
        }
      }
    }
    return false;
  }

  static void CorruptProfileFingerprint(CompiledBoard& board) {
    ++board.compiler_profile_fingerprint_;
  }

  static void CorruptRuleBucketIdentity(CompiledBoard& board) { ++board.rule_bucket_.identity; }
};

}  // namespace apgar::geometry_compiler

namespace apgar::routing {
namespace {

using board_ir::AxisAlignedBox64;
using board_ir::BoardData;
using board_ir::BoardSnapshot;
using board_ir::Point64;
using geometry_compiler::ActiveRegion;
using geometry_compiler::CompiledBoard;
using geometry_compiler::CompilerProfile;
using test_support::Compile;
using test_support::ReadFixture;
using test_support::Snapshot;
using test_support::TwoTerminalRequest;

TEST(CpuAStarTest, RepeatedCompilationAndRoutingAreExternallyIdentical) {
  const BoardSnapshot board = Snapshot(test_support::ValidM1BoardData());
  const CompilerProfile profile = test_support::DefaultCompilerProfile();
  const CompiledBoard first_compiled = Compile(board, profile);
  const CompiledBoard second_compiled = Compile(board, profile);

  const CpuRouteResult first =
      RouteWithCpuAStar(board, first_compiled, TwoTerminalRequest(board, 0, 0));
  const CpuRouteResult second =
      RouteWithCpuAStar(board, second_compiled, TwoTerminalRequest(board, 0, 0));

  ASSERT_TRUE(std::holds_alternative<CpuRoute>(first));
  ASSERT_TRUE(std::holds_alternative<CpuRoute>(second));
  EXPECT_EQ(first_compiled, second_compiled);
  EXPECT_EQ(std::get<CpuRoute>(first), std::get<CpuRoute>(second));
  EXPECT_EQ(std::get<CpuRoute>(first).source_board_content_hash, board.content_hash());
  EXPECT_EQ(std::get<CpuRoute>(first).compiler_profile_fingerprint,
            first_compiled.compiler_profile_fingerprint());
  EXPECT_EQ(std::get<CpuRoute>(first).rule_bucket_identity, first_compiled.rule_bucket().identity);
}

TEST(CpuAStarTest, CheapDiagonalHeuristicRemainsAdmissible) {
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
  for (Point64 point : std::array{
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

  const CpuRouteResult result = RouteWithCpuAStar(board, compiled, TwoTerminalRequest(board, 0, 0));

  ASSERT_TRUE(std::holds_alternative<CpuRoute>(result));
  const CpuRoute& route = std::get<CpuRoute>(result);
  EXPECT_EQ(route.total_cost, 37U);
  EXPECT_EQ(route.lattice_path, (std::vector<Point64>{
                                    Point64{.x = 0, .y = 0},
                                    Point64{.x = 10, .y = 10},
                                    Point64{.x = 20, .y = 20},
                                    Point64{.x = 30, .y = 10},
                                    Point64{.x = 40, .y = 0},
                                    Point64{.x = 30, .y = -10},
                                    Point64{.x = 20, .y = -20},
                                    Point64{.x = 30, .y = -30},
                                }));
}

TEST(CpuAStarTest, ReturnsStructuredDisconnectedAndMalformedRequestFailures) {
  const BoardSnapshot board = Snapshot(test_support::ValidM1BoardData());
  CompilerProfile profile = test_support::DefaultCompilerProfile({0});
  profile.heading_mask = static_cast<board_ir::HeadingMask>(board_ir::Heading::kHorizontal);
  profile.compilation_roi =
      AxisAlignedBox64{.min = Point64{.x = 0, .y = 0}, .max = Point64{.x = 100, .y = 0}};
  profile.active_regions = {ActiveRegion{.layer = 0, .bounds = profile.compilation_roi}};
  const CompiledBoard compiled = Compile(board, profile);

  const CpuRouteResult disconnected =
      RouteWithCpuAStar(board, compiled, TwoTerminalRequest(board, 0, 0));
  ASSERT_TRUE(std::holds_alternative<RouteFailure>(disconnected));
  EXPECT_EQ(std::get<RouteFailure>(disconnected).code, RouteFailureCode::kDisconnected);

  CpuRouteRequest malformed = TwoTerminalRequest(board, 0, 0);
  malformed.net.generation += 1;
  const CpuRouteResult invalid = RouteWithCpuAStar(board, compiled, malformed);
  ASSERT_TRUE(std::holds_alternative<RouteFailure>(invalid));
  EXPECT_EQ(std::get<RouteFailure>(invalid).code, RouteFailureCode::kInvalidRequest);
}

TEST(CpuAStarTest, DeclaresLayerTransitionsUnsupportedInsteadOfInventingVias) {
  const BoardSnapshot board = Snapshot(test_support::ValidM1BoardData());
  const CompiledBoard compiled = Compile(board, test_support::DefaultCompilerProfile());
  CpuRouteRequest request = TwoTerminalRequest(board, 0, 0);
  request.goal_layer = 31;

  const CpuRouteResult result = RouteWithCpuAStar(board, compiled, request);

  ASSERT_TRUE(std::holds_alternative<RouteFailure>(result));
  EXPECT_EQ(std::get<RouteFailure>(result).code, RouteFailureCode::kUnsupportedLayerTransition);
}

TEST(CpuAStarTest, RejectsEndpointsOutsideTheRepresentedLattice) {
  const BoardSnapshot board = Snapshot(test_support::ValidM1BoardData());
  CompilerProfile profile = test_support::DefaultCompilerProfile({0});
  profile.lattice_origin = Point64{.x = 1, .y = 0};
  const CompiledBoard compiled = Compile(board, profile);

  const CpuRouteResult result = RouteWithCpuAStar(board, compiled, TwoTerminalRequest(board, 0, 0));

  ASSERT_TRUE(std::holds_alternative<RouteFailure>(result));
  EXPECT_EQ(std::get<RouteFailure>(result).code, RouteFailureCode::kInvalidRequest);
}

TEST(CpuAStarTest, ExactValidatorRejectsDeliberatelyCorruptedReconstructedPath) {
  const BoardSnapshot board = Snapshot(test_support::ValidM1BoardData());
  const CompiledBoard compiled = Compile(board, test_support::DefaultCompilerProfile());
  const CpuRouteRequest request = TwoTerminalRequest(board, 0, 0);
  const LayerSegment illegal{
      .layer = 0,
      .centerline = board_ir::Segment64{.start = request.start, .end = request.goal},
  };

  const std::optional<RouteFailure> result =
      ValidateReconstructedRoute(board, compiled, request, std::span(&illegal, 1));

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->code, RouteFailureCode::kValidationFailed);
  EXPECT_TRUE(result->obstacle.has_value());
}

TEST(CpuAStarTest, ExactValidatorRejectsAPathEnabledByACorruptedMask) {
  const BoardSnapshot board = Snapshot(test_support::ValidM1BoardData());
  CompilerProfile profile = test_support::DefaultCompilerProfile({0});
  profile.lattice_step = 100;
  profile.tile_width_nodes = 2;
  profile.tile_height_nodes = 1;
  profile.heading_mask = static_cast<board_ir::HeadingMask>(board_ir::Heading::kHorizontal);
  profile.compilation_roi =
      AxisAlignedBox64{.min = Point64{.x = 0, .y = 0}, .max = Point64{.x = 100, .y = 0}};
  profile.active_regions = {ActiveRegion{.layer = 0, .bounds = profile.compilation_roi}};
  CompiledBoard compiled = Compile(board, profile);
  ASSERT_FALSE(compiled.EdgeIsLegal(0, 0, 0, geometry_compiler::Direction::kEast));
  ASSERT_TRUE(geometry_compiler::CompiledBoardTestPeer::AddLegalEdge(
      compiled, 0, 0, 0, geometry_compiler::Direction::kEast));

  const CpuRouteResult result = RouteWithCpuAStar(board, compiled, TwoTerminalRequest(board, 0, 0));

  ASSERT_TRUE(std::holds_alternative<RouteFailure>(result));
  const RouteFailure& failure = std::get<RouteFailure>(result);
  EXPECT_EQ(failure.code, RouteFailureCode::kValidationFailed);
  EXPECT_TRUE(failure.obstacle.has_value());
  ASSERT_TRUE(failure.telemetry.has_value());
  EXPECT_GT(failure.telemetry->expanded_states, 0U);
}

TEST(CpuAStarTest, RejectsCorruptedMaskDirectionsExcludedByCompilerProfile) {
  BoardData data = test_support::ValidM1BoardData();
  data.obstacles.clear();
  const BoardSnapshot board = Snapshot(std::move(data));
  CompilerProfile profile = test_support::DefaultCompilerProfile({0});
  profile.heading_mask = static_cast<board_ir::HeadingMask>(board_ir::Heading::kHorizontal);
  CompiledBoard compiled = Compile(board, profile);
  ASSERT_TRUE(geometry_compiler::CompiledBoardTestPeer::AddLegalEdge(
      compiled, 0, 0, 0, geometry_compiler::Direction::kNorthEast));

  const CpuRouteResult result = RouteWithCpuAStar(board, compiled, TwoTerminalRequest(board, 0, 0));

  ASSERT_TRUE(std::holds_alternative<RouteFailure>(result));
  const RouteFailure& failure = std::get<RouteFailure>(result);
  EXPECT_EQ(failure.code, RouteFailureCode::kValidationFailed);
  ASSERT_TRUE(failure.telemetry.has_value());
  EXPECT_GT(failure.telemetry->expanded_states, 0U);
}

TEST(CpuAStarTest, RejectsStaleBoardAndProfileAssociationsBeforeSearch) {
  const BoardSnapshot board = Snapshot(test_support::ValidM1BoardData());
  CompiledBoard compiled = Compile(board, test_support::DefaultCompilerProfile());

  BoardData changed_data = test_support::ValidM1BoardData();
  ++changed_data.revision;
  const BoardSnapshot changed = Snapshot(std::move(changed_data));
  const CpuRouteResult stale_board =
      RouteWithCpuAStar(changed, compiled, TwoTerminalRequest(changed, 0, 0));
  ASSERT_TRUE(std::holds_alternative<RouteFailure>(stale_board));
  EXPECT_EQ(std::get<RouteFailure>(stale_board).code, RouteFailureCode::kValidationFailed);

  geometry_compiler::CompiledBoardTestPeer::CorruptProfileFingerprint(compiled);
  const CpuRouteResult stale_profile =
      RouteWithCpuAStar(board, compiled, TwoTerminalRequest(board, 0, 0));
  ASSERT_TRUE(std::holds_alternative<RouteFailure>(stale_profile));
  EXPECT_EQ(std::get<RouteFailure>(stale_profile).code, RouteFailureCode::kValidationFailed);

  CompiledBoard stale_bucket = Compile(board, test_support::DefaultCompilerProfile());
  geometry_compiler::CompiledBoardTestPeer::CorruptRuleBucketIdentity(stale_bucket);
  const CpuRouteResult invalid_bucket =
      RouteWithCpuAStar(board, stale_bucket, TwoTerminalRequest(board, 0, 0));
  ASSERT_TRUE(std::holds_alternative<RouteFailure>(invalid_bucket));
  EXPECT_EQ(std::get<RouteFailure>(invalid_bucket).code, RouteFailureCode::kValidationFailed);
}

TEST(CpuAStarIntegrationTest, RoutesKiCadFixtureAroundFrontBlockerAndDirectlyOnBack) {
  const std::string contents = ReadFixture();
  ASSERT_FALSE(contents.empty());
  adapters::KicadFixtureImportResult imported = adapters::ImportKicadFixture(
      contents, adapters::KicadFixtureImportConfig{
                    .target_net_name = "TARGET", .nominal_width = 500'000, .clearance = 500'000});
  ASSERT_TRUE(std::holds_alternative<BoardSnapshot>(imported));
  const BoardSnapshot& board = std::get<BoardSnapshot>(imported);
  CompilerProfile profile{
      .schema_version = geometry_compiler::kCompilerProfileSchemaVersion,
      .lattice_origin = Point64{.x = 0, .y = 0},
      .lattice_step = 500'000,
      .tile_width_nodes = 8,
      .tile_height_nodes = 8,
      .compilation_roi = AxisAlignedBox64{.min = Point64{.x = 0, .y = 6'000'000},
                                          .max = Point64{.x = 20'000'000, .y = 14'000'000}},
      .active_regions =
          {
              ActiveRegion{
                  .layer = 0,
                  .bounds = AxisAlignedBox64{.min = Point64{.x = 0, .y = 6'000'000},
                                             .max = Point64{.x = 20'000'000, .y = 14'000'000}}},
              ActiveRegion{
                  .layer = 31,
                  .bounds = AxisAlignedBox64{.min = Point64{.x = 0, .y = 6'000'000},
                                             .max = Point64{.x = 20'000'000, .y = 14'000'000}}},
          },
      .heading_mask = board_ir::kM1HeadingMask,
      .costs =
          geometry_compiler::DeterministicCosts{
              .orthogonal_step = 1000, .diagonal_step = 1414, .bend = 100},
  };
  const CompiledBoard compiled = Compile(board, profile);
  CpuRouteRequest front_request = TwoTerminalRequest(board, 0, 0);
  CpuRouteRequest back_request = TwoTerminalRequest(board, 31, 31);
  if (front_request.start.x > front_request.goal.x) {
    std::swap(front_request.start, front_request.goal);
    std::swap(back_request.start, back_request.goal);
  }

  const CpuRouteResult first_front = RouteWithCpuAStar(board, compiled, front_request);
  const CpuRouteResult second_front = RouteWithCpuAStar(board, compiled, front_request);
  const CpuRouteResult back = RouteWithCpuAStar(board, compiled, back_request);

  ASSERT_TRUE(std::holds_alternative<CpuRoute>(first_front));
  ASSERT_TRUE(std::holds_alternative<CpuRoute>(second_front));
  ASSERT_TRUE(std::holds_alternative<CpuRoute>(back));
  const CpuRoute& front_route = std::get<CpuRoute>(first_front);
  const CpuRoute& back_route = std::get<CpuRoute>(back);
  EXPECT_EQ(front_route, std::get<CpuRoute>(second_front));
  EXPECT_GT(front_route.segments.size(), 1U);
  ASSERT_EQ(back_route.segments.size(), 1U);
  EXPECT_EQ(back_route.segments.front().centerline.start, back_request.start);
  EXPECT_EQ(back_route.segments.front().centerline.end, back_request.goal);
  EXPECT_GT(front_route.total_cost, back_route.total_cost);
}

}  // namespace
}  // namespace apgar::routing
