#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "apgar/board_ir/board.h"
#include "apgar/candidates/route_candidate.h"
#include "apgar/geometry_compiler/compiled_board.h"
#include "apgar/routing/candidate_policy.h"
#include "apgar/routing/cpu_astar.h"
#include "tests/support/board_builder.h"
#include "tests/support/candidate_builder.h"
#include "tests/support/compiler_builder.h"
#include "tests/support/google_test.h"
#include "tests/support/routing_builder.h"

namespace apgar::candidates {
namespace {

using board_ir::AxisAlignedBox64;
using board_ir::BoardData;
using board_ir::BoardSnapshot;
using board_ir::Point64;
using geometry_compiler::CompiledBoard;
using geometry_compiler::Direction;
using geometry_compiler::LatticeIndex;
using routing::CpuRoute;
using routing::CpuRouteRequest;
using routing::EdgeResourceKey;
using routing::LayerSegment;
using test_support::Compile;
using test_support::NormalizePolicy;
using test_support::Snapshot;
using test_support::TwoTerminalRequest;

struct DirectedCase {
  std::string_view name;
  Direction direction;
  Point64 start;
  Point64 goal;
};

struct OracleDelta {
  std::int64_t x;
  std::int64_t y;
};

constexpr std::array<DirectedCase, 8> kDirectedCases = {{
    {.name = "east", .direction = Direction::kEast, .start = {-50, -20}, .goal = {10, -20}},
    {.name = "north-east",
     .direction = Direction::kNorthEast,
     .start = {-50, -50},
     .goal = {10, 10}},
    {.name = "north", .direction = Direction::kNorth, .start = {-20, -50}, .goal = {-20, 10}},
    {.name = "north-west",
     .direction = Direction::kNorthWest,
     .start = {50, -50},
     .goal = {-10, 10}},
    {.name = "west", .direction = Direction::kWest, .start = {50, 20}, .goal = {-10, 20}},
    {.name = "south-west",
     .direction = Direction::kSouthWest,
     .start = {50, 50},
     .goal = {-10, -10}},
    {.name = "south", .direction = Direction::kSouth, .start = {20, 50}, .goal = {20, -10}},
    {.name = "south-east",
     .direction = Direction::kSouthEast,
     .start = {-50, 50},
     .goal = {10, -10}},
}};

[[nodiscard]] AxisAlignedBox64 TerminalRegion(Point64 center) {
  return AxisAlignedBox64{.min = {.x = center.x - 2, .y = center.y - 2},
                          .max = {.x = center.x + 2, .y = center.y + 2}};
}

[[nodiscard]] BoardSnapshot OpenBoard(Point64 start, Point64 goal) {
  BoardData data = test_support::ValidM1BoardData();
  data.obstacles.clear();
  data.terminals[0].center = start;
  data.terminals[0].connection_region = TerminalRegion(start);
  data.terminals[1].center = goal;
  data.terminals[1].connection_region = TerminalRegion(goal);
  data.routing_profile.nominal_width = 2;
  data.routing_profile.clearance = 1;
  data.routing_profile.allowed_layers = {0};
  return Snapshot(std::move(data));
}

[[nodiscard]] geometry_compiler::CompilerProfile PropertyProfile(Point64 start, Point64 goal) {
  geometry_compiler::CompilerProfile profile = test_support::DefaultCompilerProfile({0});
  const Point64 minimum{.x = std::min(start.x, goal.x) - 40, .y = std::min(start.y, goal.y) - 40};
  const Point64 maximum{.x = std::max(start.x, goal.x) + 40, .y = std::max(start.y, goal.y) + 40};
  profile.compilation_roi = {.min = minimum, .max = maximum};
  profile.active_regions = {{.layer = 0, .bounds = {.min = minimum, .max = maximum}}};
  return profile;
}

// Independent test oracle for the v1 undirected physical-edge key. Deliberately
// does not call CanonicalPhysicalEdgeResource or ResourceSpanStorageDelta.
[[nodiscard]] EdgeResourceKey OracleCanonicalResource(std::uint32_t layer, LatticeIndex source,
                                                      Direction direction) {
  switch (direction) {
    case Direction::kEast:
    case Direction::kNorthEast:
    case Direction::kNorth:
    case Direction::kNorthWest:
      return {.layer = layer, .lattice_x = source.x, .lattice_y = source.y, .direction = direction};
    case Direction::kWest:
      return {.layer = layer,
              .lattice_x = source.x - 1,
              .lattice_y = source.y,
              .direction = Direction::kEast};
    case Direction::kSouthWest:
      return {.layer = layer,
              .lattice_x = source.x - 1,
              .lattice_y = source.y - 1,
              .direction = Direction::kNorthEast};
    case Direction::kSouth:
      return {.layer = layer,
              .lattice_x = source.x,
              .lattice_y = source.y - 1,
              .direction = Direction::kNorth};
    case Direction::kSouthEast:
      return {.layer = layer,
              .lattice_x = source.x + 1,
              .lattice_y = source.y - 1,
              .direction = Direction::kNorthWest};
  }
  std::abort();
}

[[nodiscard]] OracleDelta OracleDirectionDelta(Direction direction) {
  switch (direction) {
    case Direction::kEast:
      return {1, 0};
    case Direction::kNorthEast:
      return {1, 1};
    case Direction::kNorth:
      return {0, 1};
    case Direction::kNorthWest:
      return {-1, 1};
    case Direction::kWest:
      return {-1, 0};
    case Direction::kSouthWest:
      return {-1, -1};
    case Direction::kSouth:
      return {0, -1};
    case Direction::kSouthEast:
      return {1, -1};
  }
  std::abort();
}

[[nodiscard]] std::vector<EdgeResourceKey> OracleResources(const DirectedCase& test_case) {
  constexpr std::int64_t kLatticeStep = 10;
  constexpr std::uint64_t kStepCount = 6;
  const OracleDelta delta = OracleDirectionDelta(test_case.direction);
  LatticeIndex source{.x = test_case.start.x / kLatticeStep, .y = test_case.start.y / kLatticeStep};
  std::vector<EdgeResourceKey> resources;
  resources.reserve(kStepCount);
  for (std::uint64_t index = 0; index < kStepCount; ++index) {
    resources.push_back(OracleCanonicalResource(0, source, test_case.direction));
    source.x += delta.x;
    source.y += delta.y;
  }
  std::ranges::sort(resources);
  return resources;
}

[[nodiscard]] std::int64_t OracleTileCoordinate(std::int64_t lattice_coordinate) {
  constexpr std::int64_t kTileNodes = 4;
  std::int64_t quotient = lattice_coordinate / kTileNodes;
  if (lattice_coordinate % kTileNodes < 0) {
    --quotient;
  }
  return quotient;
}

[[nodiscard]] std::vector<EdgeResourceKey> OracleExpandSpans(
    std::span<const PhysicalEdgeSpan> spans) {
  std::vector<EdgeResourceKey> expanded;
  for (const PhysicalEdgeSpan& span : spans) {
    std::int64_t x = span.lattice_x;
    std::int64_t y = span.lattice_y;
    for (std::uint32_t index = 0; index < span.edge_count; ++index) {
      expanded.push_back(
          {.layer = span.layer, .lattice_x = x, .lattice_y = y, .direction = span.direction});
      switch (span.direction) {
        case Direction::kEast:
          ++x;
          break;
        case Direction::kNorthEast:
          ++x;
          ++y;
          break;
        case Direction::kNorth:
          ++y;
          break;
        case Direction::kNorthWest:
          ++x;
          --y;
          break;
        case Direction::kWest:
        case Direction::kSouthWest:
        case Direction::kSouth:
        case Direction::kSouthEast:
          ADD_FAILURE() << "candidate span contained a noncanonical direction";
          break;
      }
    }
  }
  return expanded;
}

[[nodiscard]] CandidateMetrics OracleMetrics(Direction direction, std::uint64_t penalty) {
  constexpr std::uint64_t kStepCount = 6;
  constexpr std::uint64_t kLatticeStep = 10;
  constexpr std::uint64_t kOrthogonalBaseCost = 10;
  constexpr std::uint64_t kDiagonalBaseCost = 14;
  constexpr std::uint64_t kOrthogonalSurcharge = 1;
  constexpr std::uint64_t kDiagonalSurcharge = 2;
  const bool diagonal = direction == Direction::kNorthEast || direction == Direction::kNorthWest ||
                        direction == Direction::kSouthWest || direction == Direction::kSouthEast;
  const std::uint64_t base_cost = diagonal ? kDiagonalBaseCost : kOrthogonalBaseCost;
  const std::uint64_t surcharge = diagonal ? kDiagonalSurcharge : kOrthogonalSurcharge;
  return CandidateMetrics{
      .scalar_policy_cost = kStepCount * (base_cost + surcharge) + penalty,
      .intrinsic_base_cost = kStepCount * base_cost,
      .orthogonal_step_count = diagonal ? 0U : kStepCount,
      .diagonal_step_count = diagonal ? kStepCount : 0U,
      .bend_count = 0,
      .line_primitive_count = 1,
      .via_count = 0,
      .axis_aligned_length_dbu = diagonal ? 0U : kStepCount * kLatticeStep,
      .diagonal_projection_dbu = diagonal ? kStepCount * kLatticeStep : 0U,
  };
}

TEST(RouteCandidatePropertyTest,
     EightDirectedHeadingsCanonicalizeAcrossReflectionsTranslationsAndTileBoundaries) {
  constexpr std::uint64_t kPenalty = 2;
  for (const DirectedCase& test_case : kDirectedCases) {
    SCOPED_TRACE(test_case.name);
    const BoardSnapshot board = OpenBoard(test_case.start, test_case.goal);
    const CompiledBoard compiled = Compile(board, PropertyProfile(test_case.start, test_case.goal));
    CpuRouteRequest forward = TwoTerminalRequest(board, 0, 0);
    ASSERT_EQ(forward.start, test_case.start);
    ASSERT_EQ(forward.goal, test_case.goal);

    const std::vector<EdgeResourceKey> expected_resources = OracleResources(test_case);
    ASSERT_EQ(expected_resources.size(), 6U);
    EXPECT_TRUE(std::ranges::any_of(expected_resources, [](const EdgeResourceKey& resource) {
      return resource.lattice_x < 0 || resource.lattice_y < 0;
    }));
    const std::int64_t first_tile_x = OracleTileCoordinate(expected_resources.front().lattice_x);
    const std::int64_t first_tile_y = OracleTileCoordinate(expected_resources.front().lattice_y);
    EXPECT_TRUE(std::ranges::any_of(expected_resources, [&](const EdgeResourceKey& resource) {
      return OracleTileCoordinate(resource.lattice_x) != first_tile_x ||
             OracleTileCoordinate(resource.lattice_y) != first_tile_y;
    })) << "property fixture must actually cross a sparse compiler tile boundary";

    forward.candidate_policy.deterministic_seed = 0x5eedU;
    forward.candidate_policy.candidate_ordinal = 3;
    forward.candidate_policy.orthogonal_step_surcharge = 1;
    forward.candidate_policy.diagonal_step_surcharge = 2;
    forward.candidate_policy.bend_surcharge = 3;
    forward.candidate_policy.resource_penalties = {
        {.resource = expected_resources[expected_resources.size() / 2],
         .additional_cost = kPenalty}};

    const GeneratedRouteCandidate forward_candidate =
        test_support::CandidateDraft(board, compiled, forward, 17, 19);
    CpuRouteRequest reverse = forward;
    std::swap(reverse.start, reverse.goal);
    std::swap(reverse.start_layer, reverse.goal_layer);
    const GeneratedRouteCandidate reverse_candidate =
        test_support::CandidateDraft(board, compiled, reverse, 17, 19);

    EXPECT_EQ(forward_candidate, reverse_candidate);
    ASSERT_EQ(forward_candidate.geometry.size(), 1U);
    ASSERT_TRUE(std::holds_alternative<ExactLinePrimitive>(forward_candidate.geometry.front()));
    EXPECT_EQ(std::get<ExactLinePrimitive>(forward_candidate.geometry.front()),
              (ExactLinePrimitive{
                  .layer = 0, .centerline = {.start = test_case.start, .end = test_case.goal}}));
    EXPECT_EQ(OracleExpandSpans(forward_candidate.resources), expected_resources);
    EXPECT_EQ(forward_candidate.metrics, OracleMetrics(test_case.direction, kPenalty));
    EXPECT_EQ(forward_candidate.geometry_signature, reverse_candidate.geometry_signature);
    EXPECT_EQ(forward_candidate.resource_signature, reverse_candidate.resource_signature);

    const CandidateAdmissionContext forward_context{
        .board = board, .compiled_board = compiled, .request = forward};
    const CandidateAdmissionContext reverse_context{
        .board = board, .compiled_board = compiled, .request = reverse};
    EXPECT_TRUE(std::holds_alternative<RouteCandidate>(
        AdmitRouteCandidate(forward_context, forward_candidate)));
    EXPECT_TRUE(std::holds_alternative<RouteCandidate>(
        AdmitRouteCandidate(reverse_context, reverse_candidate)));
  }
}

[[nodiscard]] CandidateDraftBuildResult BuildBoundaryCandidate(std::int64_t obstacle_max_y) {
  BoardData data = test_support::ValidM1BoardData();
  data.obstacles.front().bounds.max.y = obstacle_max_y;
  const BoardSnapshot board = Snapshot(std::move(data));
  const CompiledBoard compiled = Compile(board, test_support::DefaultCompilerProfile({0}));
  const CpuRouteRequest request = TwoTerminalRequest(board, 0, 0);
  const routing::NormalizedCandidateGenerationPolicy policy = NormalizePolicy(compiled, request);
  const CandidateAssociations associations = AssociationsFor(board, compiled);
  const std::array segments = {
      LayerSegment{.layer = 0, .centerline = {.start = {0, 0}, .end = {20, 0}}},
      LayerSegment{.layer = 0, .centerline = {.start = {20, 0}, .end = {20, 20}}},
      LayerSegment{.layer = 0, .centerline = {.start = {20, 20}, .end = {80, 20}}},
      LayerSegment{.layer = 0, .centerline = {.start = {80, 20}, .end = {80, 0}}},
      LayerSegment{.layer = 0, .centerline = {.start = {80, 0}, .end = {100, 0}}},
  };
  return test_support::BuildUnsealedCandidateFromSegments(
      board, compiled, request, policy, segments, 152,
      CandidateSchedulingIdentity{.batch_identity = 23, .query_identity = 29}, associations);
}

TEST(RouteCandidatePropertyTest, ExactClearanceBoundaryAndBothOneDbuPerturbationsAreDistinct) {
  // The trace centerline is y=20, and half-width plus clearance is 10 DBU.
  // Obstacle max-y 10 is exact equality, 9 is one DBU safer, and 11 is one
  // DBU inside the forbidden swept region.
  const CandidateDraftBuildResult one_dbu_outside = BuildBoundaryCandidate(9);
  ASSERT_TRUE(std::holds_alternative<GeneratedRouteCandidate>(one_dbu_outside));

  const CandidateDraftBuildResult equality = BuildBoundaryCandidate(10);
  ASSERT_TRUE(std::holds_alternative<GeneratedRouteCandidate>(equality));

  const CandidateDraftBuildResult one_dbu_inside = BuildBoundaryCandidate(11);
  ASSERT_TRUE(std::holds_alternative<CandidateRejection>(one_dbu_inside));
  const CandidateRejection& rejection = std::get<CandidateRejection>(one_dbu_inside);
  EXPECT_EQ(rejection.stage, CandidateLifecycleStage::kExactValidated);
  EXPECT_EQ(rejection.code, CandidateRejectionCode::kExactValidation);
  EXPECT_EQ(rejection.invariant_id, "candidate.geometry.swept_clearance.v1");
  EXPECT_TRUE(rejection.conflicting_entity.has_value());
}

}  // namespace
}  // namespace apgar::candidates
