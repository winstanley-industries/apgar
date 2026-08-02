#include "apgar/routing/cpu_astar.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <limits>
#include <string>
#include <utility>
#include <variant>

#include "apgar/adapters/kicad_fixture.h"
#include "apgar/board_ir/board.h"
#include "apgar/geometry_compiler/compiled_board.h"
#include "apgar/routing/candidate_policy.h"
#include "apgar/routing/planar_route.h"
#include "src/routing/cpu_astar_internal.h"
#include "tests/support/board_builder.h"
#include "tests/support/compiled_board_test_access.h"
#include "tests/support/compiler_builder.h"
#include "tests/support/google_test.h"
#include "tests/support/routing_builder.h"

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
using test_support::CompilePreparedNet;
using test_support::ReadFixture;
using test_support::RequestForNet;
using test_support::Snapshot;
using test_support::TwoTerminalRequest;

TEST(RouteCostArithmeticTest, ReservesUint64MaxAsUnreachableSentinel) {
  constexpr std::uint64_t kMaximum = std::numeric_limits<std::uint64_t>::max();
  ASSERT_TRUE(CheckedAddFiniteRouteCost(kMaximum - 1U, 0).has_value());
  EXPECT_EQ(*CheckedAddFiniteRouteCost(kMaximum - 1U, 0), kMaximum - 1U);
  EXPECT_FALSE(CheckedAddFiniteRouteCost(kMaximum - 1U, 1).has_value());
  EXPECT_FALSE(CheckedAddFiniteRouteCost(0, kMaximum).has_value());

  // Generic byte/count accounting still permits the full uint64 domain.
  ASSERT_TRUE(CheckedAdd(kMaximum - 1U, 1).has_value());
  EXPECT_EQ(*CheckedAdd(kMaximum - 1U, 1), kMaximum);
}

struct HorizontalPolicyCase {
  BoardSnapshot board;
  CompiledBoard compiled;
  CpuRouteRequest request;
  EdgeResourceKey middle_resource;
};

[[nodiscard]] HorizontalPolicyCase MakeHorizontalPolicyCase(bool allow_detour) {
  BoardData data = test_support::ValidM1BoardData();
  data.obstacles.clear();
  BoardSnapshot board = Snapshot(std::move(data));
  CompilerProfile profile = test_support::DefaultCompilerProfile({0});
  profile.heading_mask = static_cast<board_ir::HeadingMask>(board_ir::Heading::kHorizontal);
  if (allow_detour) {
    profile.heading_mask |= static_cast<board_ir::HeadingMask>(board_ir::Heading::kVertical);
  } else {
    profile.compilation_roi =
        AxisAlignedBox64{.min = Point64{.x = 0, .y = 0}, .max = Point64{.x = 100, .y = 0}};
    profile.active_regions = {ActiveRegion{.layer = 0, .bounds = profile.compilation_roi}};
  }
  CompiledBoard compiled = Compile(board, profile);
  CpuRouteRequest request = TwoTerminalRequest(board, 0, 0);
  EdgeResourceKey middle{
      .layer = 0,
      .lattice_x = 4,
      .lattice_y = 0,
      .direction = geometry_compiler::Direction::kEast,
  };
  EXPECT_TRUE(ResourceExists(compiled, middle));
  return HorizontalPolicyCase{
      .board = std::move(board),
      .compiled = std::move(compiled),
      .request = std::move(request),
      .middle_resource = middle,
  };
}

[[nodiscard]] bool RouteUsesResource(const CpuRoute& route, const CompiledBoard& compiled,
                                     const EdgeResourceKey& resource) {
  for (std::size_t index = 1; index < route.lattice_path.size(); ++index) {
    const std::optional<geometry_compiler::LatticeIndex> start =
        geometry_compiler::ExactPointToLatticeIndex(compiled.profile(),
                                                    route.lattice_path[index - 1]);
    const std::optional<geometry_compiler::LatticeIndex> end =
        geometry_compiler::ExactPointToLatticeIndex(compiled.profile(), route.lattice_path[index]);
    if (!start.has_value() || !end.has_value()) {
      return false;
    }
    const std::optional<geometry_compiler::Direction> direction = DirectionBetween(*start, *end);
    if (!direction.has_value()) {
      return false;
    }
    const std::optional<EdgeResourceKey> traversed =
        CanonicalPhysicalEdgeResource(resource.layer, *start, *direction);
    if (traversed == resource) {
      return true;
    }
  }
  return false;
}

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
  EXPECT_EQ(std::get<CpuRoute>(first).rule_bucket_identity,
            first_compiled.rule_bucket().numeric_rule_identity());
  const CandidatePolicyResult normalized_default =
      NormalizeCandidateGenerationPolicy(first_compiled, CandidateGenerationPolicy{});
  ASSERT_TRUE(std::holds_alternative<NormalizedCandidateGenerationPolicy>(normalized_default));
  EXPECT_EQ(std::get<CpuRoute>(first).candidate_policy_identity,
            std::get<NormalizedCandidateGenerationPolicy>(normalized_default).identity);
}

TEST(CpuAStarTest, RoutesAndAuthenticatesTheRetainedPreparedNetContext) {
  const BoardSnapshot board = Snapshot(test_support::MultiNetM1BoardData());
  const CompiledBoard compiled = CompilePreparedNet(board, board.data().nets[1].ref,
                                                    test_support::DefaultCompilerProfile({0}));
  const CpuRouteRequest request = RequestForNet(board, compiled.rule_bucket().routed_net);

  EXPECT_EQ(ValidateCompiledBoardAssociation(board, compiled),
            CompiledBoardAssociationIssue::kRuleBucketMismatch);
  EXPECT_FALSE(ValidatePreparedCompiledBoardAssociation(board, compiled).has_value());
  const CpuRouteResult result = RouteWithCpuAStar(board, compiled, request);
  ASSERT_TRUE(std::holds_alternative<CpuRoute>(result));
  const CpuRoute& route = std::get<CpuRoute>(result);
  EXPECT_TRUE(CpuRouteHasAuthenticatedAStarEvidence(route));
  EXPECT_EQ(AuthenticatedCpuRouteRoutingProfileFingerprint(route),
            FingerprintRoutingProfile(compiled.prepared_routing_profile().profile()));
  EXPECT_NE(AuthenticatedCpuRouteRoutingProfileFingerprint(route),
            FingerprintRoutingProfile(board.data().routing_profile));

  CpuRoute cleared = route;
  cleared.producer_evidence.evidence.reset();
  EXPECT_FALSE(CpuRouteHasAuthenticatedAStarEvidence(cleared));
  EXPECT_FALSE(AuthenticatedCpuRouteRoutingProfileFingerprint(cleared).has_value());

  CpuRoute relabeled = route;
  ++relabeled.rule_bucket_identity;
  EXPECT_FALSE(CpuRouteHasAuthenticatedAStarEvidence(relabeled));
  EXPECT_FALSE(AuthenticatedCpuRouteRoutingProfileFingerprint(relabeled).has_value());

  CpuRoute resegmented = route;
  ++resegmented.segments.front().centerline.end.x;
  EXPECT_FALSE(CpuRouteHasAuthenticatedAStarEvidence(resegmented));
  EXPECT_FALSE(AuthenticatedCpuRouteRoutingProfileFingerprint(resegmented).has_value());

  CpuRoute diagnostic_only = route;
  ++diagnostic_only.telemetry.queue_pops;
  diagnostic_only.lattice_path.clear();
  EXPECT_TRUE(CpuRouteHasAuthenticatedAStarEvidence(diagnostic_only));
  EXPECT_EQ(AuthenticatedCpuRouteRoutingProfileFingerprint(diagnostic_only),
            FingerprintRoutingProfile(compiled.prepared_routing_profile().profile()));

  const CpuRouteResult relabeled_request =
      RouteWithCpuAStar(board, compiled, TwoTerminalRequest(board, 0, 0));
  ASSERT_TRUE(std::holds_alternative<RouteFailure>(relabeled_request));
  EXPECT_EQ(std::get<RouteFailure>(relabeled_request).code, RouteFailureCode::kInvalidRequest);
  EXPECT_EQ(std::get<RouteFailure>(relabeled_request).detail,
            "CPU route request does not match the prepared routing context");
  EXPECT_FALSE(std::get<RouteFailure>(relabeled_request).telemetry.has_value());
}

TEST(CpuAStarTest, PreparedExactValidationPinsClearanceEqualityAndOneUnitViolation) {
  BoardData equality_data = test_support::MultiNetM1BoardData();
  equality_data.obstacles.push_back(board_ir::Obstacle{
      .ref = board_ir::EntityRef{.id = 31, .generation = 0},
      .layer = 0,
      .bounds =
          AxisAlignedBox64{.min = Point64{.x = 40, .y = 30}, .max = Point64{.x = 60, .y = 40}},
      .owner_net = equality_data.nets[0].ref,
      .provenance = "foreign/equality",
  });
  const BoardSnapshot equality_board = Snapshot(std::move(equality_data));
  const CompiledBoard equality_compiled = CompilePreparedNet(
      equality_board, equality_board.data().nets[1].ref, test_support::DefaultCompilerProfile({0}));
  const CpuRouteRequest equality_request =
      RequestForNet(equality_board, equality_board.data().nets[1].ref);
  const std::array equality_segment{LayerSegment{
      .layer = 0,
      .centerline =
          board_ir::Segment64{.start = equality_request.start, .end = equality_request.goal},
  }};
  EXPECT_FALSE(ValidateReconstructedRoute(equality_board, equality_compiled, equality_request,
                                          equality_segment)
                   .has_value());

  BoardData one_unit_data = test_support::MultiNetM1BoardData();
  one_unit_data.obstacles.push_back(board_ir::Obstacle{
      .ref = board_ir::EntityRef{.id = 31, .generation = 0},
      .layer = 0,
      .bounds =
          AxisAlignedBox64{.min = Point64{.x = 40, .y = 29}, .max = Point64{.x = 60, .y = 39}},
      .owner_net = one_unit_data.nets[0].ref,
      .provenance = "foreign/one-unit-inside",
  });
  const BoardSnapshot one_unit_board = Snapshot(std::move(one_unit_data));
  const CompiledBoard one_unit_compiled = CompilePreparedNet(
      one_unit_board, one_unit_board.data().nets[1].ref, test_support::DefaultCompilerProfile({0}));
  const CpuRouteRequest one_unit_request =
      RequestForNet(one_unit_board, one_unit_board.data().nets[1].ref);
  const std::array one_unit_segment{LayerSegment{
      .layer = 0,
      .centerline =
          board_ir::Segment64{.start = one_unit_request.start, .end = one_unit_request.goal},
  }};
  const std::optional<RouteFailure> rejected = ValidateReconstructedRoute(
      one_unit_board, one_unit_compiled, one_unit_request, one_unit_segment);
  ASSERT_TRUE(rejected.has_value());
  EXPECT_EQ(rejected->code, RouteFailureCode::kValidationFailed);
  EXPECT_EQ(rejected->obstacle, (board_ir::EntityRef{.id = 31, .generation = 0}));
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

TEST(CpuAStarTest, PreflightNormalizedExactValidatorRejectsAStalePolicyIdentity) {
  BoardData data = test_support::ValidM1BoardData();
  data.obstacles.clear();
  const BoardSnapshot board = Snapshot(std::move(data));
  const CompiledBoard compiled = Compile(board, test_support::DefaultCompilerProfile());
  const CpuRouteRequest request = TwoTerminalRequest(board, 0, 0);
  const CpuRouteResult route_result = RouteWithCpuAStar(board, compiled, request);
  ASSERT_TRUE(std::holds_alternative<CpuRoute>(route_result));
  const CandidatePolicyResult normalized_result =
      NormalizeCandidateGenerationPolicy(compiled, request.candidate_policy);
  ASSERT_TRUE(std::holds_alternative<NormalizedCandidateGenerationPolicy>(normalized_result));
  NormalizedCandidateGenerationPolicy normalized =
      std::get<NormalizedCandidateGenerationPolicy>(normalized_result);
  ++normalized.identity;

  const std::optional<RouteFailure> result = ValidateReconstructedRouteWithNormalizedPolicy(
      board, compiled, request, normalized, std::get<CpuRoute>(route_result).segments);

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->code, RouteFailureCode::kInternalInvariant);
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
  EXPECT_EQ(std::get<RouteFailure>(invalid_bucket).detail,
            "Compiled board prepared routing context is invalid or mismatched");
  EXPECT_FALSE(std::get<RouteFailure>(invalid_bucket).telemetry.has_value());
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

TEST(CpuAStarPolicyTest, BansAreAbsentAndLargePenaltiesSelectTheSameDetour) {
  HorizontalPolicyCase test_case = MakeHorizontalPolicyCase(true);
  const CpuRouteResult base =
      RouteWithCpuAStar(test_case.board, test_case.compiled, test_case.request);
  ASSERT_TRUE(std::holds_alternative<CpuRoute>(base));
  ASSERT_EQ(std::get<CpuRoute>(base).total_cost, 100U);
  ASSERT_TRUE(
      RouteUsesResource(std::get<CpuRoute>(base), test_case.compiled, test_case.middle_resource));

  CpuRouteRequest banned_request = test_case.request;
  banned_request.candidate_policy.objective = CandidateObjective::kResourceDiverse;
  banned_request.candidate_policy.banned_resources = {test_case.middle_resource};
  const CpuRouteResult banned =
      RouteWithCpuAStar(test_case.board, test_case.compiled, banned_request);
  ASSERT_TRUE(std::holds_alternative<CpuRoute>(banned));
  EXPECT_EQ(std::get<CpuRoute>(banned).total_cost, 126U);
  EXPECT_FALSE(
      RouteUsesResource(std::get<CpuRoute>(banned), test_case.compiled, test_case.middle_resource));

  CpuRouteRequest penalized_request = test_case.request;
  penalized_request.candidate_policy.objective = CandidateObjective::kResourceDiverse;
  penalized_request.candidate_policy.resource_penalties = {
      ResourcePenalty{.resource = test_case.middle_resource, .additional_cost = 200}};
  const CpuRouteResult penalized =
      RouteWithCpuAStar(test_case.board, test_case.compiled, penalized_request);
  ASSERT_TRUE(std::holds_alternative<CpuRoute>(penalized));
  EXPECT_EQ(std::get<CpuRoute>(penalized).total_cost, 126U);
  EXPECT_FALSE(RouteUsesResource(std::get<CpuRoute>(penalized), test_case.compiled,
                                 test_case.middle_resource));
}

TEST(CpuAStarPolicyTest, ObjectiveSurchargesAndPenaltyApplyInBothTraversalDirections) {
  HorizontalPolicyCase test_case = MakeHorizontalPolicyCase(false);
  CandidateGenerationPolicy policy{
      .objective = CandidateObjective::kLengthBiased,
      .deterministic_seed = 0x12345678U,
      .candidate_ordinal = 7,
      .orthogonal_step_surcharge = 2,
      .diagonal_step_surcharge = 3,
      .bend_surcharge = 5,
      .banned_resources = {},
      .resource_penalties =
          {
              ResourcePenalty{.resource = test_case.middle_resource, .additional_cost = 10},
              ResourcePenalty{.resource = test_case.middle_resource, .additional_cost = 7},
          },
  };
  test_case.request.candidate_policy = policy;
  const CandidatePolicyResult normalized =
      NormalizeCandidateGenerationPolicy(test_case.compiled, policy);
  ASSERT_TRUE(std::holds_alternative<NormalizedCandidateGenerationPolicy>(normalized));

  const CpuRouteResult first =
      RouteWithCpuAStar(test_case.board, test_case.compiled, test_case.request);
  const CpuRouteResult second =
      RouteWithCpuAStar(test_case.board, test_case.compiled, test_case.request);
  ASSERT_TRUE(std::holds_alternative<CpuRoute>(first));
  ASSERT_TRUE(std::holds_alternative<CpuRoute>(second));
  EXPECT_EQ(first, second);
  EXPECT_EQ(std::get<CpuRoute>(first).total_cost, 137U);
  EXPECT_EQ(std::get<CpuRoute>(first).candidate_policy_identity,
            std::get<NormalizedCandidateGenerationPolicy>(normalized).identity);

  CpuRouteRequest reverse = test_case.request;
  std::swap(reverse.start, reverse.goal);
  const CpuRouteResult reversed = RouteWithCpuAStar(test_case.board, test_case.compiled, reverse);
  ASSERT_TRUE(std::holds_alternative<CpuRoute>(reversed));
  EXPECT_EQ(std::get<CpuRoute>(reversed).total_cost, 137U);
  EXPECT_EQ(std::get<CpuRoute>(reversed).candidate_policy_identity,
            std::get<CpuRoute>(first).candidate_policy_identity);
}

TEST(CpuAStarPolicyTest, AOnlyCorridorBanIsDisconnected) {
  HorizontalPolicyCase test_case = MakeHorizontalPolicyCase(false);
  test_case.request.candidate_policy.banned_resources = {test_case.middle_resource};

  const CpuRouteResult result =
      RouteWithCpuAStar(test_case.board, test_case.compiled, test_case.request);

  ASSERT_TRUE(std::holds_alternative<RouteFailure>(result));
  EXPECT_EQ(std::get<RouteFailure>(result).code, RouteFailureCode::kDisconnected);
}

TEST(CpuAStarPolicyTest, PolicyBanCannotMaskADanglingCompiledEdge) {
  HorizontalPolicyCase test_case = MakeHorizontalPolicyCase(false);
  const PlanarEndpointResult endpoints =
      ResolvePlanarEndpoints(test_case.compiled, test_case.request);
  ASSERT_TRUE(std::holds_alternative<ResolvedPlanarEndpoints>(endpoints));
  const geometry_compiler::LatticeIndex start = std::get<ResolvedPlanarEndpoints>(endpoints).start;
  const geometry_compiler::Direction outward =
      start.x == 0 ? geometry_compiler::Direction::kWest : geometry_compiler::Direction::kEast;
  ASSERT_TRUE(geometry_compiler::CompiledBoardTestPeer::AddLegalEdge(
      test_case.compiled, test_case.request.start_layer, start.x, start.y, outward));
  const std::optional<EdgeResourceKey> dangling =
      CanonicalPhysicalEdgeResource(test_case.request.start_layer, start, outward);
  ASSERT_TRUE(dangling.has_value());
  ASSERT_FALSE(ResourceExists(test_case.compiled, *dangling));

  // A request cannot name the dangling edge as a ban: normalization requires
  // both directed halves of every physical resource to exist.
  CpuRouteRequest direct_ban = test_case.request;
  direct_ban.candidate_policy.banned_resources = {*dangling};
  const CpuRouteResult invalid_policy =
      RouteWithCpuAStar(test_case.board, test_case.compiled, direct_ban);
  ASSERT_TRUE(std::holds_alternative<RouteFailure>(invalid_policy));
  EXPECT_EQ(std::get<RouteFailure>(invalid_policy).code, RouteFailureCode::kInvalidRequest);

  // Even a valid, unrelated policy ban cannot make search skip the independent
  // destination-containment invariant on the corrupted legal edge.
  CpuRouteRequest unrelated_ban = test_case.request;
  unrelated_ban.candidate_policy.banned_resources = {test_case.middle_resource};
  const CpuRouteResult corrupted =
      RouteWithCpuAStar(test_case.board, test_case.compiled, unrelated_ban);
  ASSERT_TRUE(std::holds_alternative<RouteFailure>(corrupted));
  EXPECT_EQ(std::get<RouteFailure>(corrupted).code, RouteFailureCode::kInternalInvariant);
}

TEST(CpuAStarPolicyTest, RejectsInvalidOverflowingAndUnsupportedPoliciesBeforeSearch) {
  HorizontalPolicyCase test_case = MakeHorizontalPolicyCase(false);

  CpuRouteRequest invalid = test_case.request;
  invalid.candidate_policy.banned_resources = {EdgeResourceKey{
      .layer = 0,
      .lattice_x = 400,
      .lattice_y = 0,
      .direction = geometry_compiler::Direction::kEast,
  }};
  const CpuRouteResult invalid_result =
      RouteWithCpuAStar(test_case.board, test_case.compiled, invalid);
  ASSERT_TRUE(std::holds_alternative<RouteFailure>(invalid_result));
  EXPECT_EQ(std::get<RouteFailure>(invalid_result).code, RouteFailureCode::kInvalidRequest);

  CpuRouteRequest overflow = test_case.request;
  overflow.candidate_policy.orthogonal_step_surcharge = std::numeric_limits<std::uint64_t>::max();
  const CpuRouteResult overflow_result =
      RouteWithCpuAStar(test_case.board, test_case.compiled, overflow);
  ASSERT_TRUE(std::holds_alternative<RouteFailure>(overflow_result));
  EXPECT_EQ(std::get<RouteFailure>(overflow_result).code, RouteFailureCode::kInvalidRequest);

  CpuRouteRequest unsupported = test_case.request;
  ++unsupported.candidate_policy.schema_version;
  const CpuRouteResult unsupported_result =
      RouteWithCpuAStar(test_case.board, test_case.compiled, unsupported);
  ASSERT_TRUE(std::holds_alternative<RouteFailure>(unsupported_result));
  EXPECT_EQ(std::get<RouteFailure>(unsupported_result).code, RouteFailureCode::kUnsupportedPolicy);
}

}  // namespace
}  // namespace apgar::routing
