#include "apgar/geometry_compiler/compiled_board.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "apgar/board_ir/board.h"
#include "apgar/candidates/route_candidate.h"
#include "apgar/geometry/exact.h"
#include "apgar/routing/candidate_policy.h"
#include "apgar/routing/cpu_astar.h"
#include "apgar/routing/planar_route.h"
#include "tests/support/board_builder.h"
#include "tests/support/candidate_builder.h"
#include "tests/support/compiler_builder.h"
#include "tests/support/google_test.h"

namespace apgar::geometry_compiler {
namespace {

using board_ir::AxisAlignedBox64;
using board_ir::BoardCreationResult;
using board_ir::BoardData;
using board_ir::BoardSnapshot;
using board_ir::Point64;
using board_ir::PreparedRoutingProfile;

constexpr board_ir::EntityRef kSecondNet{.id = 11, .generation = 0};
constexpr board_ir::EntityRef kEmptyNet{.id = 12, .generation = 0};
constexpr board_ir::EntityRef kThirdTerminal{.id = 22, .generation = 0};
constexpr board_ir::EntityRef kFourthTerminal{.id = 23, .generation = 0};

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

[[nodiscard]] PreparedRoutingProfile Prepare(const BoardSnapshot& board,
                                             board_ir::RoutingProfile profile) {
  board_ir::RoutingProfilePreparationResult result =
      board_ir::PrepareRoutingProfile(board, std::move(profile));
  EXPECT_TRUE(std::holds_alternative<PreparedRoutingProfile>(result))
      << (std::holds_alternative<board_ir::BoardValidationError>(result)
              ? std::get<board_ir::BoardValidationError>(result).message
              : "");
  return std::get<PreparedRoutingProfile>(std::move(result));
}

[[nodiscard]] BoardData MultiNetBoardData() {
  BoardData data = test_support::ValidM1BoardData();
  data.layers.push_back(board_ir::Layer{
      .ref = board_ir::EntityRef{.id = 3, .generation = 0},
      .routing_id = 99,
      .name = "internal-signal",
      .physical_order = 2,
      .type = board_ir::LayerType::kSignal,
      .routable = false,
  });
  data.nets[1].terminals = {kThirdTerminal, kFourthTerminal};
  data.nets.push_back(board_ir::Net{.ref = kEmptyNet, .name = "EMPTY", .terminals = {}});
  data.terminals.push_back(board_ir::Terminal{
      .ref = kThirdTerminal,
      .net = kSecondNet,
      .component = "U4",
      .pin = "1",
      .center = Point64{.x = 0, .y = 20},
      .connection_region =
          AxisAlignedBox64{.min = Point64{.x = -10, .y = 10}, .max = Point64{.x = 10, .y = 30}},
      .layers = {0},
  });
  data.terminals.push_back(board_ir::Terminal{
      .ref = kFourthTerminal,
      .net = kSecondNet,
      .component = "U5",
      .pin = "1",
      .center = Point64{.x = 100, .y = 20},
      .connection_region =
          AxisAlignedBox64{.min = Point64{.x = 90, .y = 10}, .max = Point64{.x = 110, .y = 30}},
      .layers = {31},
  });
  data.obstacles.push_back(board_ir::Obstacle{
      .ref = board_ir::EntityRef{.id = 31, .generation = 0},
      .layer = 0,
      .bounds =
          AxisAlignedBox64{.min = Point64{.x = 70, .y = -10}, .max = Point64{.x = 80, .y = 10}},
      .owner_net = data.routing_profile.net,
      .provenance = "U6/pad-1",
  });
  data.obstacles.push_back(board_ir::Obstacle{
      .ref = board_ir::EntityRef{.id = 32, .generation = 0},
      .layer = 0,
      .bounds =
          AxisAlignedBox64{.min = Point64{.x = -15, .y = 15}, .max = Point64{.x = -5, .y = 25}},
      .owner_net = std::nullopt,
      .provenance = "board-keepout",
  });
  return data;
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

[[nodiscard]] std::optional<bool> ExactEdgeIsLegalForProfile(
    const BoardSnapshot& board, const board_ir::RoutingProfile& routing_profile,
    board_ir::LayerId layer, board_ir::Segment64 centerline) {
  for (const board_ir::Obstacle& obstacle : board.ObstaclesOnLayer(layer)) {
    if (obstacle.owner_net.has_value() && *obstacle.owner_net == routing_profile.net) {
      continue;
    }
    const geometry::SegmentClearanceResult clearance = geometry::SweptTraceClearanceAtLeast(
        centerline, obstacle.bounds, routing_profile.nominal_width, routing_profile.clearance);
    if (!clearance.ok()) {
      ADD_FAILURE() << clearance.detail;
      return std::nullopt;
    }
    if (!clearance.clearance_satisfied) {
      return false;
    }
  }
  return true;
}

void ExpectCompiledEdgesMatchPreparedProfile(const BoardSnapshot& board,
                                             const board_ir::RoutingProfile& requested_profile,
                                             const CompiledBoard& compiled) {
  EXPECT_EQ(compiled.prepared_routing_profile().profile(), requested_profile);
  std::uint64_t observed_edges = 0;
  std::uint64_t observed_legal_edges = 0;
  for (const SparseTile& tile : compiled.tiles()) {
    for (const CompiledNode& node : tile.nodes) {
      const LatticeIndex index = GlobalLatticeIndex(compiled.profile(), tile.key, node.local_index);
      for (Direction direction : kStableDirectionOrder) {
        if ((compiled.profile().heading_mask & HeadingFor(direction)) == 0) {
          continue;
        }
        const DirectionDelta delta = DeltaFor(direction);
        const LatticeIndex neighbor{.x = index.x + delta.x, .y = index.y + delta.y};
        if (compiled.FindNode(tile.key.layer, neighbor.x, neighbor.y) == nullptr) {
          continue;
        }
        ++observed_edges;
        const std::optional<bool> exact =
            ExactEdgeIsLegalForProfile(board, requested_profile, tile.key.layer,
                                       board_ir::Segment64{
                                           .start = ExactPoint(compiled.profile(), index),
                                           .end = ExactPoint(compiled.profile(), neighbor),
                                       });
        ASSERT_TRUE(exact.has_value());
        const bool compiled_legal = (node.legal_edges & MaskFor(direction)) != 0;
        EXPECT_EQ(compiled_legal, *exact)
            << "layer=" << tile.key.layer << " x=" << index.x << " y=" << index.y
            << " direction=" << static_cast<unsigned int>(direction);
        observed_legal_edges += compiled_legal ? 1U : 0U;
      }
    }
  }
  EXPECT_EQ(observed_edges, compiled.telemetry().represented_directional_edges);
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
  EXPECT_EQ(first.prepared_routing_profile().profile(), board.data().routing_profile);
}

TEST(CompiledBoardTest, CanonicalizesPreparedProfileLayers) {
  const BoardSnapshot board = Snapshot(MultiNetBoardData());
  board_ir::RoutingProfile unsorted_profile = board.data().routing_profile;
  unsorted_profile.net = kSecondNet;
  unsorted_profile.allowed_layers = {31, 0};
  board_ir::RoutingProfilePreparationResult prepared =
      board_ir::PrepareRoutingProfile(board, unsorted_profile);
  ASSERT_TRUE(std::holds_alternative<PreparedRoutingProfile>(prepared));
  const board_ir::RoutingProfile second_profile =
      std::get<PreparedRoutingProfile>(std::move(prepared)).profile();
  EXPECT_EQ(second_profile.allowed_layers, (std::vector<board_ir::LayerId>{0, 31}));

  board_ir::RoutingProfile sorted_profile = unsorted_profile;
  std::ranges::sort(sorted_profile.allowed_layers);
  CompileResult unsorted_result = CompileBoard(board, test_support::DefaultCompilerProfile({0}),
                                               Prepare(board, unsorted_profile));
  CompileResult sorted_result = CompileBoard(board, test_support::DefaultCompilerProfile({0}),
                                             Prepare(board, sorted_profile));
  CompileResult repeat_result = CompileBoard(board, test_support::DefaultCompilerProfile({0}),
                                             Prepare(board, unsorted_profile));
  ASSERT_TRUE(std::holds_alternative<CompiledBoard>(unsorted_result));
  ASSERT_TRUE(std::holds_alternative<CompiledBoard>(sorted_result));
  ASSERT_TRUE(std::holds_alternative<CompiledBoard>(repeat_result));
  const CompiledBoard unsorted = std::get<CompiledBoard>(std::move(unsorted_result));
  const CompiledBoard sorted = std::get<CompiledBoard>(std::move(sorted_result));
  const CompiledBoard repeat = std::get<CompiledBoard>(std::move(repeat_result));
  EXPECT_EQ(unsorted, sorted);
  EXPECT_EQ(unsorted, repeat);
}

TEST(CompiledBoardTest, CompilesNetSpecificObstacleOwnership) {
  const BoardSnapshot board = Snapshot(MultiNetBoardData());
  board_ir::RoutingProfile second_profile = board.data().routing_profile;
  second_profile.net = kSecondNet;

  const CompiledBoard first = Compile(board, test_support::DefaultCompilerProfile({0}));
  CompileResult second_result = CompileBoard(board, test_support::DefaultCompilerProfile({0}),
                                             Prepare(board, second_profile));
  ASSERT_TRUE(std::holds_alternative<CompiledBoard>(second_result));
  const CompiledBoard second = std::get<CompiledBoard>(std::move(second_result));

  EXPECT_EQ(second.prepared_routing_profile().profile(), second_profile);
  EXPECT_EQ(second.rule_bucket(), DeriveM1RuleBucket(second_profile));
  EXPECT_EQ(first.rule_bucket().routed_net, board.data().routing_profile.net);
  EXPECT_EQ(second.rule_bucket().routed_net, kSecondNet);
  EXPECT_EQ(second.rule_bucket().identity, first.rule_bucket().identity);
  EXPECT_NE(routing::FingerprintRoutingProfile(second.prepared_routing_profile().profile()),
            routing::FingerprintRoutingProfile(board.data().routing_profile));
  EXPECT_FALSE(first.EdgeIsLegal(0, 3, 0, Direction::kEast));
  EXPECT_TRUE(second.EdgeIsLegal(0, 3, 0, Direction::kEast));
  EXPECT_TRUE(first.EdgeIsLegal(0, 7, 0, Direction::kEast));
  EXPECT_FALSE(second.EdgeIsLegal(0, 7, 0, Direction::kEast));
  EXPECT_FALSE(first.EdgeIsLegal(0, -2, 2, Direction::kEast));
  EXPECT_FALSE(second.EdgeIsLegal(0, -2, 2, Direction::kEast));
  ExpectCompiledEdgesMatchPreparedProfile(board, second_profile, second);
}

TEST(CompiledBoardTest, NonDefaultContextFailsClosedAtRouteAndAdmission) {
  const BoardSnapshot board = Snapshot(MultiNetBoardData());
  const CompiledBoard first = Compile(board, test_support::DefaultCompilerProfile({0}));
  board_ir::RoutingProfile net_only_profile = board.data().routing_profile;
  net_only_profile.net = kSecondNet;
  CompileResult net_only_result = CompileBoard(board, test_support::DefaultCompilerProfile({0}),
                                               Prepare(board, net_only_profile));
  ASSERT_TRUE(std::holds_alternative<CompiledBoard>(net_only_result));
  const CompiledBoard net_only_board = std::get<CompiledBoard>(std::move(net_only_result));

  EXPECT_FALSE(routing::ValidateCompiledBoardAssociation(board, first).has_value());
  EXPECT_EQ(routing::ValidateCompiledBoardAssociation(board, net_only_board),
            routing::CompiledBoardAssociationIssue::kRuleBucketMismatch);

  const routing::TwoTerminalRequestResult request_result =
      routing::BuildTwoTerminalRouteRequest(board, 0, 0);
  ASSERT_TRUE(std::holds_alternative<routing::CpuRouteRequest>(request_result));
  const routing::CpuRouteRequest request = std::get<routing::CpuRouteRequest>(request_result);
  const routing::CpuRouteResult route_result =
      routing::RouteWithCpuAStar(board, net_only_board, request);
  ASSERT_TRUE(std::holds_alternative<routing::RouteFailure>(route_result));
  const routing::RouteFailure& route_failure = std::get<routing::RouteFailure>(route_result);
  EXPECT_EQ(route_failure.code, routing::RouteFailureCode::kValidationFailed);
  EXPECT_EQ(route_failure.detail,
            "Compiled board rule bucket is stale or does not match the BoardSnapshot");

  candidates::GeneratedRouteCandidate generated =
      test_support::CandidateDraft(board, first, request);
  const candidates::CandidateAdmissionResult admission = candidates::AdmitRouteCandidate(
      candidates::CandidateAdmissionContext{
          .board = board, .compiled_board = net_only_board, .request = request},
      std::move(generated));
  ASSERT_TRUE(std::holds_alternative<candidates::CandidateRejection>(admission));
  const candidates::CandidateRejection& rejection =
      std::get<candidates::CandidateRejection>(admission);
  EXPECT_EQ(rejection.code, candidates::CandidateRejectionCode::kAssociationMismatch);
  EXPECT_EQ(rejection.invariant_id, "candidate.associations.compiled_board.v1");
}

TEST(CompiledBoardTest, SeparatesRoutingProfileAndCompilerProfileErrors) {
  const BoardSnapshot board = Snapshot(MultiNetBoardData());
  board_ir::RoutingProfile second_profile = board.data().routing_profile;
  second_profile.net = kSecondNet;

  const CompileResult invalid_compiler_profile = CompileBoard(
      board, test_support::DefaultCompilerProfile({99}), Prepare(board, second_profile));
  ASSERT_TRUE(std::holds_alternative<CompileError>(invalid_compiler_profile));
  EXPECT_EQ(std::get<CompileError>(invalid_compiler_profile).code,
            CompileErrorCode::kInvalidProfile);
  EXPECT_EQ(std::get<CompileError>(invalid_compiler_profile).detail,
            "Active-region layers must belong to the routing rule bucket");

  board_ir::RoutingProfile stale = second_profile;
  ++stale.net.generation;
  const board_ir::RoutingProfilePreparationResult stale_preparation =
      board_ir::PrepareRoutingProfile(board, stale);
  ASSERT_TRUE(std::holds_alternative<board_ir::BoardValidationError>(stale_preparation));
  EXPECT_EQ(std::get<board_ir::BoardValidationError>(stale_preparation).code,
            board_ir::BoardValidationCode::kInvalidRoutingProfile);
  const PreparedRoutingProfile second_prepared = Prepare(board, second_profile);
  BoardData revised_data = MultiNetBoardData();
  ++revised_data.revision;
  const BoardSnapshot revised_board = Snapshot(std::move(revised_data));
  const CompileResult mismatched_snapshot =
      CompileBoard(revised_board, test_support::DefaultCompilerProfile({0}), second_prepared);
  ASSERT_TRUE(std::holds_alternative<CompileError>(mismatched_snapshot));
  EXPECT_EQ(std::get<CompileError>(mismatched_snapshot).code,
            CompileErrorCode::kInvalidRoutingProfile);
  EXPECT_EQ(std::get<CompileError>(mismatched_snapshot).detail,
            "Prepared routing profile belongs to a different Board IR snapshot");

  const CompileResult binding_precedes_profile_validation =
      CompileBoard(revised_board, test_support::DefaultCompilerProfile({99}), second_prepared);
  ASSERT_TRUE(std::holds_alternative<CompileError>(binding_precedes_profile_validation));
  EXPECT_EQ(std::get<CompileError>(binding_precedes_profile_validation).code,
            CompileErrorCode::kInvalidRoutingProfile);
  EXPECT_EQ(std::get<CompileError>(binding_precedes_profile_validation).detail,
            "Prepared routing profile belongs to a different Board IR snapshot");
}

TEST(CompiledBoardTest, PreparedExactOracleChecksSnapshotBindingDirectly) {
  const BoardSnapshot board = Snapshot(MultiNetBoardData());
  board_ir::RoutingProfile second_profile = board.data().routing_profile;
  second_profile.net = kSecondNet;
  const PreparedRoutingProfile prepared = Prepare(board, second_profile);
  constexpr board_ir::Segment64 kSecondNetOwnedMovement{
      .start = Point64{.x = 40, .y = 0},
      .end = Point64{.x = 50, .y = 0},
  };

  const geometry::MovementValidationResult default_context =
      geometry::ValidateMovement(board, 0, kSecondNetOwnedMovement);
  EXPECT_EQ(default_context.code, geometry::MovementViolationCode::kStaticObstacleConflict);
  EXPECT_EQ(default_context.obstacle, (board_ir::EntityRef{.id = 30, .generation = 0}));

  const geometry::MovementValidationResult accepted =
      geometry::ValidateMovement(board, prepared, 0, kSecondNetOwnedMovement);
  EXPECT_TRUE(accepted.legal()) << accepted.detail;

  BoardData revised_data = MultiNetBoardData();
  ++revised_data.revision;
  const BoardSnapshot revised_board = Snapshot(std::move(revised_data));
  const geometry::MovementValidationResult rejected =
      geometry::ValidateMovement(revised_board, prepared, 0, kSecondNetOwnedMovement);
  EXPECT_FALSE(rejected.legal());
  EXPECT_EQ(rejected.code, geometry::MovementViolationCode::kPreparedProfileSnapshotMismatch);
  EXPECT_EQ(rejected.detail, "Prepared routing profile belongs to a different Board IR snapshot");

  const geometry::MovementValidationResult binding_precedes_geometry =
      geometry::ValidateMovement(revised_board, prepared, 99, kSecondNetOwnedMovement);
  EXPECT_EQ(binding_precedes_geometry.code,
            geometry::MovementViolationCode::kPreparedProfileSnapshotMismatch);
  EXPECT_FALSE(binding_precedes_geometry.obstacle.has_value());
}

TEST(CompiledBoardTest, RejectsInvalidPreparedPerNetProfiles) {
  const BoardSnapshot board = Snapshot(MultiNetBoardData());
  board_ir::RoutingProfile valid = board.data().routing_profile;
  valid.net = kSecondNet;

  const auto expect_rejected = [&](board_ir::RoutingProfile profile,
                                   board_ir::BoardValidationCode expected_code,
                                   std::string_view expected_message) {
    const board_ir::RoutingProfilePreparationResult result =
        board_ir::PrepareRoutingProfile(board, std::move(profile));
    ASSERT_TRUE(std::holds_alternative<board_ir::BoardValidationError>(result));
    const board_ir::BoardValidationError& error = std::get<board_ir::BoardValidationError>(result);
    EXPECT_EQ(error.code, expected_code);
    EXPECT_EQ(error.message, expected_message);
  };

  board_ir::RoutingProfile invalid = valid;
  invalid.net = kEmptyNet;
  expect_rejected(invalid, board_ir::BoardValidationCode::kInvalidRoutingProfile,
                  "M1 routing profiles require exactly two terminals");

  invalid = valid;
  invalid.nominal_width = 0;
  expect_rejected(invalid, board_ir::BoardValidationCode::kInvalidRoutingProfile,
                  "Routing profile dimensions, layers, or headings are invalid");
  invalid.nominal_width = board_ir::kMaxAbsDbCoord + 1;
  expect_rejected(invalid, board_ir::BoardValidationCode::kInvalidRoutingProfile,
                  "Routing profile dimensions, layers, or headings are invalid");

  invalid = valid;
  invalid.clearance = -1;
  expect_rejected(invalid, board_ir::BoardValidationCode::kInvalidRoutingProfile,
                  "Routing profile dimensions, layers, or headings are invalid");
  invalid.clearance = board_ir::kMaxAbsDbCoord + 1;
  expect_rejected(invalid, board_ir::BoardValidationCode::kInvalidRoutingProfile,
                  "Routing profile dimensions, layers, or headings are invalid");

  invalid = valid;
  invalid.allowed_layers.clear();
  expect_rejected(invalid, board_ir::BoardValidationCode::kInvalidRoutingProfile,
                  "Routing profile dimensions, layers, or headings are invalid");
  invalid.allowed_layers = {0, 0, 31};
  expect_rejected(invalid, board_ir::BoardValidationCode::kInvalidRoutingProfile,
                  "Routing profile layers contain duplicates");
  invalid.allowed_layers = {0, 31, 1'000};
  expect_rejected(invalid, board_ir::BoardValidationCode::kInvalidRoutingProfile,
                  "Routing profile contains an unavailable signal layer");
  invalid.allowed_layers = {0, 31, 99};
  expect_rejected(invalid, board_ir::BoardValidationCode::kInvalidRoutingProfile,
                  "Routing profile contains an unavailable signal layer");
  invalid.allowed_layers = {0};
  expect_rejected(invalid, board_ir::BoardValidationCode::kInvalidRoutingProfile,
                  "Every routed-net terminal must intersect an allowed routing layer");

  board_ir::RoutingProfile narrowed_default = board.data().routing_profile;
  narrowed_default.allowed_layers = {0};
  expect_rejected(
      narrowed_default, board_ir::BoardValidationCode::kInvalidRoutingProfile,
      "Prepared routing profiles may differ from the Board IR default only by routed net");

  invalid = valid;
  invalid.allowed_headings = 0;
  expect_rejected(invalid, board_ir::BoardValidationCode::kInvalidRoutingProfile,
                  "Routing profile dimensions, layers, or headings are invalid");
  invalid.allowed_headings = static_cast<board_ir::HeadingMask>(1U << 7U);
  expect_rejected(invalid, board_ir::BoardValidationCode::kInvalidRoutingProfile,
                  "Routing profile dimensions, layers, or headings are invalid");

  invalid = valid;
  ++invalid.clearance;
  expect_rejected(
      invalid, board_ir::BoardValidationCode::kInvalidRoutingProfile,
      "Prepared routing profiles may differ from the Board IR default only by routed net");

  invalid = valid;
  ++invalid.nominal_width;
  expect_rejected(
      invalid, board_ir::BoardValidationCode::kInvalidRoutingProfile,
      "Prepared routing profiles may differ from the Board IR default only by routed net");

  invalid = valid;
  invalid.allowed_headings = static_cast<board_ir::HeadingMask>(board_ir::Heading::kHorizontal) |
                             static_cast<board_ir::HeadingMask>(board_ir::Heading::kVertical);
  expect_rejected(
      invalid, board_ir::BoardValidationCode::kInvalidRoutingProfile,
      "Prepared routing profiles may differ from the Board IR default only by routed net");

  // PrepareRoutingProfile accepts an immutable, already validated snapshot, so
  // stale terminal references cannot reach it. Snapshot admission rejects that
  // malformed ownership before any profile can be prepared.
  BoardData stale_terminal = MultiNetBoardData();
  ++stale_terminal.nets[1].terminals.back().generation;
  const BoardCreationResult stale_result = board_ir::CreateBoardSnapshot(std::move(stale_terminal));
  ASSERT_TRUE(std::holds_alternative<board_ir::BoardValidationError>(stale_result));
  EXPECT_EQ(std::get<board_ir::BoardValidationError>(stale_result).code,
            board_ir::BoardValidationCode::kInvalidReference);
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
  std::uint64_t expected_host_bytes = sizeof(CompiledBoard);
  expected_host_bytes += compiled.profile().active_regions.size() * sizeof(ActiveRegion);
  expected_host_bytes += compiled.rule_bucket().allowed_layers.size() * sizeof(board_ir::LayerId);
  expected_host_bytes += compiled.prepared_routing_profile().profile().allowed_layers.size() *
                         sizeof(board_ir::LayerId);
  expected_host_bytes += compiled.tiles().size() * sizeof(SparseTile);
  for (const SparseTile& tile : compiled.tiles()) {
    expected_host_bytes += tile.nodes.size() * sizeof(CompiledNode);
  }
  EXPECT_EQ(telemetry.estimated_host_bytes, expected_host_bytes);
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

TEST(CompiledBoardTest, RejectsEmptyHeadingMaskAsInvalidProfile) {
  const BoardSnapshot board = Snapshot(test_support::ValidM1BoardData());
  CompilerProfile profile = test_support::DefaultCompilerProfile({0});
  profile.heading_mask = 0;

  CompileResult result = CompileBoard(board, profile);

  ASSERT_TRUE(std::holds_alternative<CompileError>(result));
  const CompileError& error = std::get<CompileError>(result);
  EXPECT_EQ(error.code, CompileErrorCode::kInvalidProfile);
  EXPECT_EQ(error.detail, "Compiler profile heading mask must be non-empty");
}

TEST(CompiledBoardTest, RejectsNonM1HeadingBitAsUnsupported) {
  const BoardSnapshot board = Snapshot(test_support::ValidM1BoardData());
  CompilerProfile profile = test_support::DefaultCompilerProfile({0});
  profile.heading_mask = static_cast<board_ir::HeadingMask>(1U << 3U);

  CompileResult result = CompileBoard(board, profile);

  ASSERT_TRUE(std::holds_alternative<CompileError>(result));
  const CompileError& error = std::get<CompileError>(result);
  EXPECT_EQ(error.code, CompileErrorCode::kUnsupported);
  EXPECT_EQ(error.detail,
            "Compiler profile heading mask contains headings outside the M1 H/V/45 set");
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
