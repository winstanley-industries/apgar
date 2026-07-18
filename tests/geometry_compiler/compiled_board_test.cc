#include "apgar/geometry_compiler/compiled_board.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

#include "apgar/board_ir/board.h"
#include "apgar/geometry/exact.h"
#include "tests/support/board_builder.h"
#include "tests/support/compiler_builder.h"
#include "tests/support/google_test.h"

namespace apgar::geometry_compiler {
namespace {

using board_ir::AxisAlignedBox64;
using board_ir::BoardCreationResult;
using board_ir::BoardData;
using board_ir::BoardSnapshot;
using board_ir::Point64;

[[nodiscard]] BoardSnapshot Snapshot(BoardData data) {
  BoardCreationResult result = board_ir::CreateBoardSnapshot(std::move(data));
  EXPECT_TRUE(std::holds_alternative<BoardSnapshot>(result));
  return std::get<BoardSnapshot>(std::move(result));
}

[[nodiscard]] CompiledBoard Compile(const BoardSnapshot& board, CompilerProfile profile) {
  CompileResult result = CompileBoard(board, std::move(profile));
  EXPECT_TRUE(std::holds_alternative<CompiledBoard>(result))
      << (std::holds_alternative<CompileError>(result) ? std::get<CompileError>(result).detail
                                                       : "");
  return std::get<CompiledBoard>(std::move(result));
}

[[nodiscard]] Point64 ExactPoint(const CompilerProfile& profile, LatticeIndex index) {
  const std::optional<Point64> point = LatticeIndexToExactPoint(profile, index);
  EXPECT_TRUE(point.has_value());
  return point.value_or(Point64{});
}

void ExpectEveryCompiledLegalEdgeIsExactLegal(const BoardSnapshot& board,
                                              const CompiledBoard& compiled) {
  std::uint64_t observed_legal_edges = 0;
  for (const SparseTile& tile : compiled.tiles()) {
    for (const CompiledNode& node : tile.nodes) {
      const LatticeIndex index = GlobalLatticeIndex(compiled.profile(), tile.key, node.local_index);
      for (Direction direction : kStableDirectionOrder) {
        if ((node.legal_edges & MaskFor(direction)) == 0) {
          continue;
        }
        ++observed_legal_edges;
        const DirectionDelta delta = DeltaFor(direction);
        const LatticeIndex neighbor{.x = index.x + delta.x, .y = index.y + delta.y};
        const CompiledNode* neighbor_node =
            compiled.FindNode(tile.key.layer, neighbor.x, neighbor.y);
        ASSERT_NE(neighbor_node, nullptr);
        EXPECT_TRUE((neighbor_node->legal_edges & MaskFor(Opposite(direction))) != 0);
        const geometry::MovementValidationResult exact =
            geometry::ValidateMovement(board, tile.key.layer,
                                       board_ir::Segment64{
                                           .start = ExactPoint(compiled.profile(), index),
                                           .end = ExactPoint(compiled.profile(), neighbor),
                                       });
        EXPECT_TRUE(exact.legal()) << exact.detail;
      }
    }
  }
  EXPECT_EQ(observed_legal_edges, compiled.telemetry().legal_directional_edges);
}

TEST(CompiledBoardTest, GeneratedMicrocasesNeverPublishFalseFreeEdges) {
  std::uint64_t generated_legal_edges = 0;
  for (std::int64_t obstacle_x = 20; obstacle_x <= 80; obstacle_x += 15) {
    for (std::int64_t obstacle_y = -20; obstacle_y <= 20; obstacle_y += 10) {
      BoardData data = test_support::ValidM1BoardData();
      data.obstacles.front().bounds = AxisAlignedBox64{
          .min = Point64{.x = obstacle_x - 3, .y = obstacle_y - 4},
          .max = Point64{.x = obstacle_x + 7, .y = obstacle_y + 6},
      };
      const BoardSnapshot board = Snapshot(std::move(data));
      const CompiledBoard compiled = Compile(board, test_support::DefaultCompilerProfile({0}));
      ExpectEveryCompiledLegalEdgeIsExactLegal(board, compiled);
      EXPECT_EQ(compiled.telemetry().false_blocked_directional_edges, 0U);
      generated_legal_edges += compiled.telemetry().legal_directional_edges;
    }
  }
  EXPECT_GT(generated_legal_edges, 1'000U);
}

TEST(CompiledBoardTest, PreservesBoundaryEqualityAndOneUnitPerturbations) {
  const BoardSnapshot board = Snapshot(test_support::ValidM1BoardData());
  CompilerProfile profile = test_support::DefaultCompilerProfile({0});
  profile.lattice_step = 1;
  profile.tile_width_nodes = 8;
  profile.tile_height_nodes = 8;
  profile.compilation_roi =
      AxisAlignedBox64{.min = Point64{.x = 39, .y = 19}, .max = Point64{.x = 61, .y = 20}};
  profile.active_regions = {ActiveRegion{.layer = 0, .bounds = profile.compilation_roi}};

  const CompiledBoard compiled = Compile(board, profile);

  EXPECT_TRUE(compiled.EdgeIsLegal(0, 39, 20, Direction::kEast));
  EXPECT_FALSE(compiled.EdgeIsLegal(0, 39, 19, Direction::kEast));
  ExpectEveryCompiledLegalEdgeIsExactLegal(board, compiled);
}

TEST(CompiledBoardTest, CompilesHorizontalVerticalAndDiagonalMovements) {
  BoardData data = test_support::ValidM1BoardData();
  data.obstacles.clear();
  const BoardSnapshot board = Snapshot(std::move(data));
  CompilerProfile profile = test_support::DefaultCompilerProfile({0});
  profile.compilation_roi =
      AxisAlignedBox64{.min = Point64{.x = 0, .y = 0}, .max = Point64{.x = 20, .y = 20}};
  profile.active_regions = {ActiveRegion{.layer = 0, .bounds = profile.compilation_roi}};
  const CompiledBoard compiled = Compile(board, profile);

  EXPECT_TRUE(compiled.EdgeIsLegal(0, 1, 1, Direction::kEast));
  EXPECT_TRUE(compiled.EdgeIsLegal(0, 1, 1, Direction::kNorth));
  EXPECT_TRUE(compiled.EdgeIsLegal(0, 1, 1, Direction::kNorthEast));
  EXPECT_TRUE(compiled.EdgeIsLegal(0, 1, 1, Direction::kSouthWest));
}

TEST(CompiledBoardTest, ExactDiagonalEnvelopePreventsCornerCutting) {
  BoardData data = test_support::ValidM1BoardData();
  data.obstacles.front().bounds =
      AxisAlignedBox64{.min = Point64{.x = 10, .y = -10}, .max = Point64{.x = 20, .y = 0}};
  const BoardSnapshot board = Snapshot(std::move(data));
  CompilerProfile profile = test_support::DefaultCompilerProfile({0});
  profile.compilation_roi =
      AxisAlignedBox64{.min = Point64{.x = 0, .y = 0}, .max = Point64{.x = 10, .y = 10}};
  profile.active_regions = {ActiveRegion{.layer = 0, .bounds = profile.compilation_roi}};
  const CompiledBoard compiled = Compile(board, profile);

  const geometry::MovementValidationResult exact = geometry::ValidateMovement(
      board, 0,
      board_ir::Segment64{.start = Point64{.x = 0, .y = 0}, .end = Point64{.x = 10, .y = 10}});
  ASSERT_FALSE(exact.legal());
  EXPECT_FALSE(compiled.EdgeIsLegal(0, 0, 0, Direction::kNorthEast));
}

TEST(CompiledBoardTest, StoresCrossTileEdgesAndLeavesSparseGapsUnbridged) {
  BoardData data = test_support::ValidM1BoardData();
  data.obstacles.clear();
  const BoardSnapshot board = Snapshot(std::move(data));
  CompilerProfile profile = test_support::DefaultCompilerProfile({0});
  profile.tile_width_nodes = 2;
  profile.tile_height_nodes = 2;
  profile.compilation_roi =
      AxisAlignedBox64{.min = Point64{.x = 0, .y = 0}, .max = Point64{.x = 40, .y = 0}};
  profile.active_regions = {
      ActiveRegion{.layer = 0,
                   .bounds = AxisAlignedBox64{.min = Point64{.x = 0, .y = 0},
                                              .max = Point64{.x = 10, .y = 0}}},
      ActiveRegion{.layer = 0,
                   .bounds = AxisAlignedBox64{.min = Point64{.x = 30, .y = 0},
                                              .max = Point64{.x = 40, .y = 0}}},
  };
  const CompiledBoard sparse = Compile(board, profile);

  EXPECT_FALSE(sparse.ContainsNode(0, 2, 0));
  EXPECT_FALSE(sparse.EdgeIsLegal(0, 1, 0, Direction::kEast));
  EXPECT_EQ(sparse.telemetry().active_tile_count, 3U);

  profile.active_regions = {ActiveRegion{.layer = 0, .bounds = profile.compilation_roi}};
  const CompiledBoard contiguous = Compile(board, profile);
  EXPECT_TRUE(contiguous.EdgeIsLegal(0, 1, 0, Direction::kEast));
  ASSERT_NE(contiguous.FindTile(TileKey{.layer = 0, .coordinate = TileCoordinate{.x = 0, .y = 0}}),
            nullptr);
  ASSERT_NE(contiguous.FindTile(TileKey{.layer = 0, .coordinate = TileCoordinate{.x = 1, .y = 0}}),
            nullptr);
}

TEST(CompiledBoardTest, KeepsLayerFieldsIndependent) {
  const BoardSnapshot board = Snapshot(test_support::ValidM1BoardData());
  const CompiledBoard compiled = Compile(board, test_support::DefaultCompilerProfile());

  EXPECT_FALSE(compiled.EdgeIsLegal(0, 3, 0, Direction::kEast));
  EXPECT_TRUE(compiled.EdgeIsLegal(31, 3, 0, Direction::kEast));
  ExpectEveryCompiledLegalEdgeIsExactLegal(board, compiled);
}

TEST(CompiledBoardTest, PropagatesStableBoardProfileAndRuleBucketIdentity) {
  const BoardSnapshot board = Snapshot(test_support::ValidM1BoardData());
  const CompilerProfile canonical_profile = test_support::DefaultCompilerProfile();
  CompilerProfile reordered_profile = canonical_profile;
  std::ranges::reverse(reordered_profile.active_regions);
  CompilerProfile duplicated_profile = canonical_profile;
  duplicated_profile.active_regions.push_back(duplicated_profile.active_regions.front());

  const CompiledBoard first = Compile(board, canonical_profile);
  const CompiledBoard second = Compile(board, reordered_profile);

  EXPECT_EQ(first, second);
  EXPECT_EQ(FingerprintCompilerProfile(canonical_profile),
            FingerprintCompilerProfile(reordered_profile));
  EXPECT_EQ(FingerprintCompilerProfile(canonical_profile),
            FingerprintCompilerProfile(duplicated_profile));
  EXPECT_EQ(first.source_board_content_hash(), board.content_hash());
  EXPECT_EQ(first.compiler_profile_fingerprint(), FingerprintCompilerProfile(first.profile()));
  EXPECT_EQ(first.rule_bucket(), DeriveM1RuleBucket(board.data().routing_profile));
  EXPECT_EQ(first.rule_bucket().nominal_width, board.data().routing_profile.nominal_width);
  EXPECT_EQ(first.rule_bucket().clearance, board.data().routing_profile.clearance);
  EXPECT_EQ(first.rule_bucket().allowed_layers, board.data().routing_profile.allowed_layers);
  EXPECT_EQ(first.rule_bucket().allowed_headings, board.data().routing_profile.allowed_headings);
}

TEST(CompiledBoardTest, ReportsDefinedMemoryAndConservatismTelemetry) {
  BoardData data = test_support::ValidM1BoardData();
  data.obstacles.clear();
  const BoardSnapshot board = Snapshot(std::move(data));
  CompilerProfile profile = test_support::DefaultCompilerProfile({0});
  profile.heading_mask = static_cast<board_ir::HeadingMask>(board_ir::Heading::kHorizontal);
  profile.compilation_roi =
      AxisAlignedBox64{.min = Point64{.x = 0, .y = 0}, .max = Point64{.x = 10, .y = 10}};
  profile.active_regions = {ActiveRegion{.layer = 0, .bounds = profile.compilation_roi}};

  const CompiledBoard compiled = Compile(board, profile);
  const CompilerTelemetry& telemetry = compiled.telemetry();

  EXPECT_EQ(telemetry.active_tile_count, 1U);
  EXPECT_EQ(telemetry.represented_nodes, 4U);
  EXPECT_EQ(telemetry.represented_directional_edges, 4U);
  EXPECT_EQ(telemetry.legal_directional_edges, 4U);
  EXPECT_EQ(telemetry.blocked_directional_edges, 0U);
  EXPECT_EQ(telemetry.false_blocked_directional_edges, 0U);
  EXPECT_EQ(telemetry.false_blocked_rate_parts_per_billion, 0U);
  EXPECT_GT(telemetry.estimated_host_bytes, sizeof(CompiledBoard));
}

TEST(CompiledBoardTest, CountsRepresentedNodesAfterOverlappingRegionDeduplication) {
  BoardData data = test_support::ValidM1BoardData();
  data.obstacles.clear();
  const BoardSnapshot board = Snapshot(std::move(data));
  CompilerProfile profile = test_support::DefaultCompilerProfile({0});
  profile.compilation_roi =
      AxisAlignedBox64{.min = Point64{.x = 0, .y = 0}, .max = Point64{.x = 30, .y = 10}};
  profile.active_regions = {
      ActiveRegion{.layer = 0,
                   .bounds = AxisAlignedBox64{.min = Point64{.x = 0, .y = 0},
                                              .max = Point64{.x = 20, .y = 10}}},
      ActiveRegion{.layer = 0,
                   .bounds = AxisAlignedBox64{.min = Point64{.x = 10, .y = 0},
                                              .max = Point64{.x = 30, .y = 10}}},
  };

  const CompiledBoard compiled = Compile(board, profile);

  EXPECT_EQ(compiled.telemetry().represented_nodes, 8U);
}

TEST(CompiledBoardTest, NonSquareTileMappingRoundTripsAcrossNegativeBoundaries) {
  CompilerProfile profile = test_support::DefaultCompilerProfile({0});
  profile.tile_width_nodes = 3;
  profile.tile_height_nodes = 2;
  constexpr std::array<LatticeIndex, 8> kIndices = {
      LatticeIndex{.x = -4, .y = -3}, LatticeIndex{.x = -3, .y = -2},
      LatticeIndex{.x = -1, .y = -1}, LatticeIndex{.x = 0, .y = 0},
      LatticeIndex{.x = 2, .y = 1},   LatticeIndex{.x = 3, .y = 2},
      LatticeIndex{.x = 5, .y = -2},  LatticeIndex{.x = -5, .y = 3},
  };

  for (LatticeIndex index : kIndices) {
    const TileKey key = TileForLatticeIndex(profile, 31, index);
    const std::uint64_t local_index = LocalNodeIndex(profile, index);
    EXPECT_LT(local_index,
              static_cast<std::uint64_t>(profile.tile_width_nodes) * profile.tile_height_nodes);
    EXPECT_EQ(GlobalLatticeIndex(profile, key, local_index), index);
    const std::optional<Point64> exact = LatticeIndexToExactPoint(profile, index);
    ASSERT_TRUE(exact.has_value());
    const std::optional<LatticeIndex> round_trip = ExactPointToLatticeIndex(profile, *exact);
    ASSERT_TRUE(round_trip.has_value());
    EXPECT_EQ(*round_trip, index);
  }
}

TEST(CompiledBoardTest, RejectsMalformedAndUnrepresentableProfilesExplicitly) {
  const BoardSnapshot board = Snapshot(test_support::ValidM1BoardData());
  CompilerProfile malformed = test_support::DefaultCompilerProfile({0});
  malformed.lattice_step = 0;
  CompileResult malformed_result = CompileBoard(board, malformed);
  ASSERT_TRUE(std::holds_alternative<CompileError>(malformed_result));
  EXPECT_EQ(std::get<CompileError>(malformed_result).code, CompileErrorCode::kInvalidProfile);

  CompilerProfile oversized = test_support::DefaultCompilerProfile({0});
  oversized.lattice_step = 1;
  oversized.compilation_roi = AxisAlignedBox64{.min = Point64{.x = -100'000, .y = -100'000},
                                               .max = Point64{.x = 100'000, .y = 100'000}};
  oversized.active_regions = {ActiveRegion{.layer = 0, .bounds = oversized.compilation_roi}};
  CompileResult oversized_result = CompileBoard(board, oversized);
  ASSERT_TRUE(std::holds_alternative<CompileError>(oversized_result));
  EXPECT_EQ(std::get<CompileError>(oversized_result).code,
            CompileErrorCode::kUnrepresentableProfile);
}

TEST(CompiledBoardTest, SupportsNegativeLatticeAndTileCoordinatesWithoutAliasing) {
  BoardData data = test_support::ValidM1BoardData();
  data.obstacles.clear();
  const BoardSnapshot board = Snapshot(std::move(data));
  CompilerProfile profile = test_support::DefaultCompilerProfile({0});
  profile.tile_width_nodes = 2;
  profile.tile_height_nodes = 2;
  profile.compilation_roi =
      AxisAlignedBox64{.min = Point64{.x = -20, .y = -20}, .max = Point64{.x = 10, .y = 10}};
  profile.active_regions = {ActiveRegion{.layer = 0, .bounds = profile.compilation_roi}};
  const CompiledBoard compiled = Compile(board, profile);

  EXPECT_TRUE(compiled.ContainsNode(0, -2, -2));
  EXPECT_TRUE(compiled.ContainsNode(0, -1, -1));
  EXPECT_TRUE(compiled.EdgeIsLegal(0, -1, -1, Direction::kNorthEast));
  EXPECT_NE(compiled.FindTile(TileKey{.layer = 0, .coordinate = TileCoordinate{.x = -1, .y = -1}}),
            nullptr);
}

}  // namespace
}  // namespace apgar::geometry_compiler
