#include "apgar/candidates/route_candidate.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "apgar/board_ir/board.h"
#include "apgar/geometry_compiler/compiled_board.h"
#include "apgar/gpu/planar_router.h"
#include "apgar/routing/candidate_policy.h"
#include "apgar/routing/cpu_astar.h"
#include "apgar/text/utf8.h"
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
using board_ir::EntityRef;
using board_ir::Point64;
using geometry_compiler::CompiledBoard;
using routing::CpuRouteRequest;
using routing::LayerSegment;
using test_support::CandidateDraft;
using test_support::Compile;
using test_support::NormalizePolicy;
using test_support::Snapshot;
using test_support::TwoTerminalRequest;

template <typename Access>
concept ExposesPrivateMake = requires { &Access::Make; };

template <typename Access>
concept ExposesPrivateSeal = requires { &Access::Seal; };

static_assert(std::is_const_v<
              std::remove_reference_t<decltype(std::declval<const RouteCandidate&>().data())>>);
static_assert(!std::is_default_constructible_v<RouteCandidateAdmissionFactory>);
static_assert(!std::is_default_constructible_v<GeneratedRouteCandidateProducerFactory>);
static_assert(!ExposesPrivateMake<RouteCandidateAdmissionFactory>);
static_assert(!ExposesPrivateSeal<GeneratedRouteCandidateProducerFactory>);

[[nodiscard]] const CandidateRejection& Rejection(const CandidateAdmissionResult& result) {
  EXPECT_TRUE(std::holds_alternative<CandidateRejection>(result));
  if (!std::holds_alternative<CandidateRejection>(result)) {
    std::abort();
  }
  return std::get<CandidateRejection>(result);
}

[[nodiscard]] GeneratedRouteCandidate DraftFromSegments(const BoardSnapshot& board,
                                                        const CompiledBoard& compiled,
                                                        CpuRouteRequest& request,
                                                        std::span<const LayerSegment> segments,
                                                        std::uint64_t reported_cost,
                                                        std::uint64_t query_identity = 1) {
  GeneratedRouteCandidate result = test_support::CandidateDraftAlongSegments(
      board, compiled, request, segments,
      CandidateSchedulingIdentity{.batch_identity = 17, .query_identity = query_identity});
  EXPECT_EQ(result.metrics.scalar_policy_cost, reported_cost);
  return result;
}

TEST(RouteCandidateTest, CpuDraftIsCanonicalStableAndExactlyAdmitted) {
  const BoardSnapshot board = Snapshot();
  const CompiledBoard compiled = Compile(board, test_support::DefaultCompilerProfile({0}));
  CpuRouteRequest request = TwoTerminalRequest(board, 0, 0);
  const GeneratedRouteCandidate first = CandidateDraft(board, compiled, request);
  const GeneratedRouteCandidate second = CandidateDraft(board, compiled, request);

  EXPECT_EQ(first, second);
  EXPECT_FALSE(first.id.empty());
  EXPECT_FALSE(first.geometry_signature.empty());
  EXPECT_FALSE(first.resource_signature.empty());
  EXPECT_FALSE(first.geometry.empty());
  EXPECT_FALSE(first.resources.empty());
  EXPECT_EQ(first.payload_checksum, ComputeCandidatePayloadChecksum(first));
  ASSERT_TRUE(ComputeCandidateLogicalBytes(first).has_value());
  EXPECT_EQ(first.logical_bytes, *ComputeCandidateLogicalBytes(first));

  CandidateAdmissionResult admitted = AdmitRouteCandidate(
      CandidateAdmissionContext{.board = board, .compiled_board = compiled, .request = request},
      first);
  ASSERT_TRUE(std::holds_alternative<RouteCandidate>(admitted));
  EXPECT_EQ(std::get<RouteCandidate>(admitted).data(), first);
}

TEST(RouteCandidateTest, CanonicalV1IdentitySignaturesChecksumAndBytesHaveGoldenValues) {
  GeneratedRouteCandidate candidate{
      .schema_major = kRouteCandidateSchemaMajor,
      .schema_minor = kRouteCandidateSchemaMinor,
      .id = {},
      .net = {.id = 2, .generation = 3},
      .intended_terminals = {{{.id = 5, .generation = 7}, {.id = 11, .generation = 13}}},
      .associations =
          {
              .board_content_hash = 17,
              .compiler_profile_fingerprint = 19,
              .geometry_compiler_version = 23,
              .routing_profile_fingerprint = 29,
              .rule_bucket_identity = 31,
          },
      .geometry_schema_version = kCandidateGeometrySchemaVersion,
      .resource_schema_version = kCandidateResourceSchemaVersion,
      .policy =
          {
              .objective = routing::CandidateObjective::kResourceDiverse,
              .deterministic_seed = 37,
              .candidate_ordinal = 41,
              .orthogonal_step_surcharge = 43,
              .diagonal_step_surcharge = 47,
              .bend_surcharge = 53,
              .banned_resources = {{.layer = 1,
                                    .lattice_x = -2,
                                    .lattice_y = 3,
                                    .direction = geometry_compiler::Direction::kEast}},
              .resource_penalties = {{{.layer = 2,
                                       .lattice_x = 5,
                                       .lattice_y = -7,
                                       .direction = geometry_compiler::Direction::kNorthWest},
                                      59}},
          },
      .policy_identity = 61,
      .provenance =
          {
              .generator = CandidateGeneratorKind::kCudaSweep,
              .generator_version = 67,
              .backend = CandidateBackendKind::kCuda,
              .supported_device_class = "cuda-cc-12.0",
              .deterministic_seed = 37,
              .batch_identity = 71,
              .query_identity = 73,
              .candidate_ordinal = 41,
          },
      .geometry =
          {
              ExactLinePrimitive{
                  .layer = 1,
                  .centerline = {.start = {.x = -79, .y = 83}, .end = {.x = 89, .y = 83}}},
              ThroughViaPrimitive{.template_id = 97,
                                  .position = {.x = 89, .y = 83},
                                  .start_layer = 1,
                                  .end_layer = 2},
          },
      .resources = {{.layer = 1,
                     .lattice_x = -101,
                     .lattice_y = 103,
                     .direction = geometry_compiler::Direction::kNorthEast,
                     .edge_count = 107,
                     .usage_units = 109}},
      .metrics =
          {
              .scalar_policy_cost = 113,
              .intrinsic_base_cost = 127,
              .orthogonal_step_count = 131,
              .diagonal_step_count = 137,
              .bend_count = 139,
              .line_primitive_count = 1,
              .via_count = 1,
              .axis_aligned_length_dbu = 149,
              .diagonal_projection_dbu = 151,
          },
      .constraints =
          {
              .supported_hard_constraints_satisfied = false,
              .unsupported_rules_remain = true,
              .connected_intended_terminal_count = 1,
              .exact_validation_code = CandidateExactValidationCode::kUnsupportedGeometry,
          },
      .geometry_signature = {},
      .resource_signature = {},
      .payload_checksum = 0,
      .logical_bytes = 0,
      .producer_evidence = {},
  };
  candidate.id = DeriveCandidateId(candidate.net, candidate.associations, candidate.policy_identity,
                                   candidate.provenance);
  ASSERT_FALSE(FinalizeGeneratedCandidateDraft(candidate).has_value());

  EXPECT_EQ(candidate.id,
            (CandidateId{.high = 0x31dd0cd88f981122ULL, .low = 0x1a9c591e646f5637ULL}));
  EXPECT_EQ(candidate.geometry_signature,
            (CandidateSignature{.high = 0xb3266ecc6d583ca3ULL, .low = 0xe0d14601d2a1c7eaULL}));
  EXPECT_EQ(candidate.resource_signature,
            (CandidateSignature{.high = 0x6458483b4188f3f0ULL, .low = 0x09d5af2802c5c707ULL}));
  EXPECT_EQ(candidate.payload_checksum, 0x1fc6ba60d39a84b7ULL);
  EXPECT_EQ(candidate.logical_bytes, 511U);
}

TEST(RouteCandidateTest, IncompatibleCandidateAndSignatureVersionsFailClosed) {
  const BoardSnapshot board = Snapshot();
  const CompiledBoard compiled = Compile(board, test_support::DefaultCompilerProfile({0}));
  CpuRouteRequest request = TwoTerminalRequest(board, 0, 0);
  const CandidateAdmissionContext context{
      .board = board, .compiled_board = compiled, .request = request};

  for (int corruption = 0; corruption < 4; ++corruption) {
    GeneratedRouteCandidate candidate = CandidateDraft(board, compiled, request);
    if (corruption == 0) {
      ++candidate.schema_major;
    } else if (corruption == 1) {
      ++candidate.schema_minor;
    } else if (corruption == 2) {
      ++candidate.geometry_schema_version;
    } else {
      ++candidate.resource_schema_version;
    }
    const CandidateAdmissionResult result = AdmitRouteCandidate(context, std::move(candidate));
    const CandidateRejection& rejection = Rejection(result);
    EXPECT_EQ(rejection.stage, CandidateLifecycleStage::kGenerated);
    EXPECT_EQ(rejection.code, CandidateRejectionCode::kUnsupported);
    EXPECT_EQ(rejection.invariant_id, "candidate.schema.compatibility.v1");
  }
}

TEST(RouteCandidateTest, ReverseRequestNormalizesToCanonicalTerminalOrder) {
  const BoardSnapshot board = Snapshot();
  const CompiledBoard compiled = Compile(board, test_support::DefaultCompilerProfile({0}));
  CpuRouteRequest request = TwoTerminalRequest(board, 0, 0);
  std::swap(request.start, request.goal);
  std::swap(request.start_layer, request.goal_layer);
  GeneratedRouteCandidate draft = CandidateDraft(board, compiled, request);

  ASSERT_TRUE(std::holds_alternative<ExactLinePrimitive>(draft.geometry.front()));
  const board_ir::Net* net = board.FindNet(request.net);
  ASSERT_NE(net, nullptr);
  const board_ir::Terminal* first = board.FindTerminal(net->terminals[0]);
  const board_ir::Terminal* second = board.FindTerminal(net->terminals[1]);
  ASSERT_NE(first, nullptr);
  ASSERT_NE(second, nullptr);
  EXPECT_EQ(std::get<ExactLinePrimitive>(draft.geometry.front()).centerline.start, first->center);
  EXPECT_EQ(std::get<ExactLinePrimitive>(draft.geometry.back()).centerline.end, second->center);

  EXPECT_TRUE(std::holds_alternative<RouteCandidate>(AdmitRouteCandidate(
      CandidateAdmissionContext{.board = board, .compiled_board = compiled, .request = request},
      std::move(draft))));
}

TEST(RouteCandidateTest, AdmissionMergesCollinearSegmentsWithoutTrustingGeneratorLayout) {
  BoardData data = test_support::ValidM1BoardData();
  data.obstacles.clear();
  const BoardSnapshot board = Snapshot(std::move(data));
  const CompiledBoard compiled = Compile(board, test_support::DefaultCompilerProfile({0}));
  CpuRouteRequest request = TwoTerminalRequest(board, 0, 0);
  GeneratedRouteCandidate draft = CandidateDraft(board, compiled, request);
  ASSERT_EQ(draft.geometry.size(), 1U);
  const ExactLinePrimitive original = std::get<ExactLinePrimitive>(draft.geometry.front());
  draft.geometry = {
      ExactLinePrimitive{
          .layer = original.layer,
          .centerline = {.start = original.centerline.start, .end = Point64{.x = 50, .y = 0}}},
      ExactLinePrimitive{
          .layer = original.layer,
          .centerline = {.start = Point64{.x = 50, .y = 0}, .end = original.centerline.end}},
  };

  CandidateAdmissionResult result = AdmitRouteCandidate(
      CandidateAdmissionContext{.board = board, .compiled_board = compiled, .request = request},
      std::move(draft));
  ASSERT_TRUE(std::holds_alternative<RouteCandidate>(result));
  EXPECT_EQ(std::get<RouteCandidate>(result).data().geometry.size(), 1U);
}

TEST(RouteCandidateTest, EntityRefZeroIsAcceptedAndRetainedInRejections) {
  BoardData data = test_support::ValidM1BoardData();
  constexpr EntityRef kZeroNet{.id = 0, .generation = 0};
  data.nets[0].ref = kZeroNet;
  data.terminals[0].net = kZeroNet;
  data.terminals[1].net = kZeroNet;
  data.routing_profile.net = kZeroNet;
  const BoardSnapshot board = Snapshot(std::move(data));
  const CompiledBoard compiled = Compile(board, test_support::DefaultCompilerProfile({0}));
  CpuRouteRequest request = TwoTerminalRequest(board, 0, 0);
  GeneratedRouteCandidate draft = CandidateDraft(board, compiled, request);

  EXPECT_TRUE(std::holds_alternative<RouteCandidate>(AdmitRouteCandidate(
      CandidateAdmissionContext{.board = board, .compiled_board = compiled, .request = request},
      draft)));

  draft.payload_checksum = 0;
  std::get<ExactLinePrimitive>(draft.geometry.front()).centerline.start.x =
      board_ir::kMaxAbsDbCoord + 1;
  const std::uint64_t expected_rejection_checksum = ComputeCandidatePayloadChecksum(draft);
  const CandidateAdmissionResult rejected = AdmitRouteCandidate(
      CandidateAdmissionContext{.board = board, .compiled_board = compiled, .request = request},
      std::move(draft));
  const CandidateRejection& rejection = Rejection(rejected);
  ASSERT_TRUE(rejection.net.has_value());
  EXPECT_EQ(*rejection.net, kZeroNet);
  ASSERT_TRUE(rejection.candidate_payload_checksum.has_value());
  EXPECT_EQ(*rejection.candidate_payload_checksum, expected_rejection_checksum);
}

TEST(RouteCandidateTest, NorthWestResourcesCoalesceInAscendingCanonicalKeyOrder) {
  BoardData data = test_support::ValidM1BoardData();
  data.obstacles.clear();
  data.terminals[1].center = Point64{.x = -20, .y = 20};
  data.terminals[1].connection_region =
      AxisAlignedBox64{.min = data.terminals[1].center, .max = data.terminals[1].center};
  const BoardSnapshot board = Snapshot(std::move(data));
  const CompiledBoard compiled = Compile(board, test_support::DefaultCompilerProfile({0}));
  CpuRouteRequest request = TwoTerminalRequest(board, 0, 0);
  const std::array segments = {
      LayerSegment{
          .layer = 0,
          .centerline = {.start = Point64{.x = 0, .y = 0}, .end = Point64{.x = -20, .y = 20}}},
  };

  const GeneratedRouteCandidate draft = DraftFromSegments(board, compiled, request, segments, 28);
  ASSERT_EQ(draft.resources.size(), 1U);
  EXPECT_EQ(draft.resources.front().direction, geometry_compiler::Direction::kNorthWest);
  EXPECT_EQ(draft.resources.front().lattice_x, -1);
  EXPECT_EQ(draft.resources.front().lattice_y, 1);
  EXPECT_EQ(draft.resources.front().edge_count, 2U);
  EXPECT_TRUE(std::holds_alternative<RouteCandidate>(AdmitRouteCandidate(
      CandidateAdmissionContext{.board = board, .compiled_board = compiled, .request = request},
      draft)));
}

TEST(RouteCandidateTest, BoundaryEqualityPassesAndOneDbuClearanceViolationFails) {
  const BoardSnapshot board = Snapshot();
  const CompiledBoard compiled = Compile(board, test_support::DefaultCompilerProfile({0}));
  CpuRouteRequest request = TwoTerminalRequest(board, 0, 0);
  const std::array equality_segments = {
      LayerSegment{.layer = 0, .centerline = {.start = {.x = 0, .y = 0}, .end = {.x = 20, .y = 0}}},
      LayerSegment{.layer = 0,
                   .centerline = {.start = {.x = 20, .y = 0}, .end = {.x = 20, .y = 20}}},
      LayerSegment{.layer = 0,
                   .centerline = {.start = {.x = 20, .y = 20}, .end = {.x = 80, .y = 20}}},
      LayerSegment{.layer = 0,
                   .centerline = {.start = {.x = 80, .y = 20}, .end = {.x = 80, .y = 0}}},
      LayerSegment{.layer = 0,
                   .centerline = {.start = {.x = 80, .y = 0}, .end = {.x = 100, .y = 0}}},
  };
  GeneratedRouteCandidate equality =
      DraftFromSegments(board, compiled, request, equality_segments, 152);
  EXPECT_TRUE(std::holds_alternative<RouteCandidate>(AdmitRouteCandidate(
      CandidateAdmissionContext{.board = board, .compiled_board = compiled, .request = request},
      equality)));

  equality.geometry = {
      ExactLinePrimitive{.layer = 0,
                         .centerline = {.start = {.x = 0, .y = 0}, .end = {.x = 20, .y = 0}}},
      ExactLinePrimitive{.layer = 0,
                         .centerline = {.start = {.x = 20, .y = 0}, .end = {.x = 20, .y = 19}}},
      ExactLinePrimitive{.layer = 0,
                         .centerline = {.start = {.x = 20, .y = 19}, .end = {.x = 80, .y = 19}}},
      ExactLinePrimitive{.layer = 0,
                         .centerline = {.start = {.x = 80, .y = 19}, .end = {.x = 80, .y = 0}}},
      ExactLinePrimitive{.layer = 0,
                         .centerline = {.start = {.x = 80, .y = 0}, .end = {.x = 100, .y = 0}}},
  };
  const std::uint64_t expected_policy_identity = equality.policy_identity;
  const CandidateAdmissionResult result = AdmitRouteCandidate(
      CandidateAdmissionContext{.board = board, .compiled_board = compiled, .request = request},
      std::move(equality));
  const CandidateRejection& rejection = Rejection(result);
  EXPECT_EQ(rejection.code, CandidateRejectionCode::kExactValidation);
  EXPECT_EQ(rejection.invariant_id, "candidate.geometry.swept_clearance.v1");
  EXPECT_EQ(rejection.policy_identity, expected_policy_identity);
  EXPECT_TRUE(rejection.conflicting_entity.has_value());
}

TEST(RouteCandidateTest, UnintendedTerminalSweptEqualityAndOneDbuPerturbationAreExact) {
  BoardData data = test_support::ValidM1BoardData();
  data.obstacles.clear();
  constexpr EntityRef kOtherTerminal{.id = 40, .generation = 0};
  data.nets[1].terminals = {kOtherTerminal};
  data.terminals.push_back(board_ir::Terminal{
      .ref = kOtherTerminal,
      .net = data.nets[1].ref,
      .component = "U3",
      .pin = "1",
      .center = {.x = 50, .y = 35},
      .connection_region = {.min = {.x = 45, .y = 30}, .max = {.x = 55, .y = 40}},
      .layers = {0},
  });
  const BoardSnapshot board = Snapshot(std::move(data));
  const CompiledBoard compiled = Compile(board, test_support::DefaultCompilerProfile({0}));
  CpuRouteRequest request = TwoTerminalRequest(board, 0, 0);
  const std::array equality_segments = {
      LayerSegment{.layer = 0, .centerline = {.start = {.x = 0, .y = 0}, .end = {.x = 0, .y = 20}}},
      LayerSegment{.layer = 0,
                   .centerline = {.start = {.x = 0, .y = 20}, .end = {.x = 100, .y = 20}}},
      LayerSegment{.layer = 0,
                   .centerline = {.start = {.x = 100, .y = 20}, .end = {.x = 100, .y = 0}}},
  };
  GeneratedRouteCandidate equality =
      DraftFromSegments(board, compiled, request, equality_segments, 146);
  EXPECT_TRUE(std::holds_alternative<RouteCandidate>(AdmitRouteCandidate(
      CandidateAdmissionContext{.board = board, .compiled_board = compiled, .request = request},
      equality)));

  equality.geometry = {
      ExactLinePrimitive{.layer = 0,
                         .centerline = {.start = {.x = 0, .y = 0}, .end = {.x = 0, .y = 21}}},
      ExactLinePrimitive{.layer = 0,
                         .centerline = {.start = {.x = 0, .y = 21}, .end = {.x = 100, .y = 21}}},
      ExactLinePrimitive{.layer = 0,
                         .centerline = {.start = {.x = 100, .y = 21}, .end = {.x = 100, .y = 0}}},
  };
  const CandidateAdmissionResult result = AdmitRouteCandidate(
      CandidateAdmissionContext{.board = board, .compiled_board = compiled, .request = request},
      std::move(equality));
  const CandidateRejection& rejection = Rejection(result);
  EXPECT_EQ(rejection.invariant_id, "candidate.terminals.unintended.v1");
  EXPECT_EQ(rejection.conflicting_entity, kOtherTerminal);
}

TEST(RouteCandidateTest, RejectionDiagnosticTruncationPreservesUtf8Boundaries) {
  BoardData data = test_support::ValidM1BoardData();
  constexpr std::string_view kDetailPrefix =
      "Exact swept trace validation failed: Movement conflicts with ";
  ASSERT_LT(kDetailPrefix.size(), kMaximumCandidateDiagnosticBytes);
  const std::size_t available = kMaximumCandidateDiagnosticBytes - kDetailPrefix.size();
  const std::size_t ascii_pad = available % 3U == 0U ? 1U : 0U;
  data.obstacles.front().provenance.assign(ascii_pad, 'x');
  for (std::size_t index = 0; index < 400U; ++index) {
    data.obstacles.front().provenance.append("\xE2\x82\xAC");
  }
  const BoardSnapshot board = Snapshot(std::move(data));
  const CompiledBoard compiled = Compile(board, test_support::DefaultCompilerProfile({0}));
  CpuRouteRequest request = TwoTerminalRequest(board, 0, 0);
  GeneratedRouteCandidate draft = CandidateDraft(board, compiled, request);
  draft.geometry = {
      ExactLinePrimitive{.layer = 0,
                         .centerline = {.start = {.x = 0, .y = 0}, .end = {.x = 100, .y = 0}}},
  };

  const CandidateAdmissionResult rejected = AdmitRouteCandidate(
      CandidateAdmissionContext{.board = board, .compiled_board = compiled, .request = request},
      std::move(draft));
  const CandidateRejection& rejection = Rejection(rejected);
  EXPECT_EQ(rejection.invariant_id, "candidate.geometry.swept_clearance.v1");
  EXPECT_LE(rejection.detail.size(), kMaximumCandidateDiagnosticBytes);
  EXPECT_TRUE(text::IsValidUtf8(rejection.detail));
}

TEST(RouteCandidateTest, ThroughViaDisconnectedAndInvalidHeadingAreRejectedBeforeStorage) {
  BoardData data = test_support::ValidM1BoardData();
  data.obstacles.clear();
  const BoardSnapshot board = Snapshot(std::move(data));
  const CompiledBoard compiled = Compile(board, test_support::DefaultCompilerProfile({0}));
  CpuRouteRequest request = TwoTerminalRequest(board, 0, 0);
  const CandidateAdmissionContext context{
      .board = board, .compiled_board = compiled, .request = request};

  GeneratedRouteCandidate via = CandidateDraft(board, compiled, request);
  via.geometry = {ThroughViaPrimitive{
      .template_id = 1, .position = {.x = 0, .y = 0}, .start_layer = 0, .end_layer = 31}};
  EXPECT_EQ(Rejection(AdmitRouteCandidate(context, std::move(via))).code,
            CandidateRejectionCode::kUnsupported);

  GeneratedRouteCandidate disconnected = CandidateDraft(board, compiled, request);
  disconnected.geometry = {
      ExactLinePrimitive{.layer = 0,
                         .centerline = {.start = {.x = 0, .y = 0}, .end = {.x = 20, .y = 20}}},
      ExactLinePrimitive{.layer = 0,
                         .centerline = {.start = {.x = 30, .y = 20}, .end = {.x = 80, .y = 20}}},
      ExactLinePrimitive{.layer = 0,
                         .centerline = {.start = {.x = 80, .y = 20}, .end = {.x = 100, .y = 0}}},
  };
  EXPECT_EQ(Rejection(AdmitRouteCandidate(context, std::move(disconnected))).invariant_id,
            "candidate.geometry.connectivity.v1");

  GeneratedRouteCandidate invalid_heading = CandidateDraft(board, compiled, request);
  invalid_heading.geometry = {
      ExactLinePrimitive{.layer = 0,
                         .centerline = {.start = {.x = 0, .y = 0}, .end = {.x = 30, .y = 20}}},
      ExactLinePrimitive{.layer = 0,
                         .centerline = {.start = {.x = 30, .y = 20}, .end = {.x = 100, .y = 0}}},
  };
  EXPECT_EQ(Rejection(AdmitRouteCandidate(context, std::move(invalid_heading))).invariant_id,
            "candidate.geometry.heading.v1");
}

TEST(RouteCandidateTest, CorruptAssociationsResourcesMetricsSignaturesAndBytesFailClosed) {
  const BoardSnapshot board = Snapshot();
  const CompiledBoard compiled = Compile(board, test_support::DefaultCompilerProfile({0}));
  const CpuRouteRequest request = TwoTerminalRequest(board, 0, 0);
  const CandidateAdmissionContext context{
      .board = board, .compiled_board = compiled, .request = request};

  GeneratedRouteCandidate association = CandidateDraft(board, compiled, request);
  ++association.associations.board_content_hash;
  association.id = DeriveCandidateId(association.net, association.associations,
                                     association.policy_identity, association.provenance);
  ASSERT_FALSE(FinalizeGeneratedCandidateDraft(association).has_value());
  EXPECT_EQ(Rejection(AdmitRouteCandidate(context, std::move(association))).code,
            CandidateRejectionCode::kAssociationMismatch);

  GeneratedRouteCandidate resources = CandidateDraft(board, compiled, request);
  ASSERT_FALSE(resources.resources.empty());
  ++resources.resources.front().edge_count;
  EXPECT_EQ(Rejection(AdmitRouteCandidate(context, std::move(resources))).invariant_id,
            "candidate.resources.equivalence.v1");

  GeneratedRouteCandidate metrics = CandidateDraft(board, compiled, request);
  ++metrics.metrics.scalar_policy_cost;
  EXPECT_EQ(Rejection(AdmitRouteCandidate(context, std::move(metrics))).invariant_id,
            "candidate.metrics.scalar_cost.v1");

  GeneratedRouteCandidate intrinsic = CandidateDraft(board, compiled, request);
  ++intrinsic.metrics.intrinsic_base_cost;
  EXPECT_EQ(Rejection(AdmitRouteCandidate(context, std::move(intrinsic))).invariant_id,
            "candidate.metrics.equivalence.v1");

  GeneratedRouteCandidate signature = CandidateDraft(board, compiled, request);
  ++signature.geometry_signature.low;
  EXPECT_EQ(Rejection(AdmitRouteCandidate(context, std::move(signature))).code,
            CandidateRejectionCode::kSignatureMismatch);

  GeneratedRouteCandidate bytes = CandidateDraft(board, compiled, request);
  ++bytes.logical_bytes;
  EXPECT_EQ(Rejection(AdmitRouteCandidate(context, std::move(bytes))).code,
            CandidateRejectionCode::kMemoryAccountingMismatch);
}

TEST(RouteCandidateTest, HostileIdentityProvenanceCoordinatesEndpointsResourcesAndConstraintsFail) {
  BoardData data = test_support::ValidM1BoardData();
  data.obstacles.clear();
  const BoardSnapshot board = Snapshot(std::move(data));
  const CompiledBoard compiled = Compile(board, test_support::DefaultCompilerProfile({0}));
  const CpuRouteRequest request = TwoTerminalRequest(board, 0, 0);
  const CandidateAdmissionContext context{
      .board = board, .compiled_board = compiled, .request = request};

  GeneratedRouteCandidate terminals = CandidateDraft(board, compiled, request);
  ++terminals.intended_terminals[0].id;
  EXPECT_EQ(Rejection(AdmitRouteCandidate(context, std::move(terminals))).invariant_id,
            "candidate.terminals.association.v1");

  GeneratedRouteCandidate provenance = CandidateDraft(board, compiled, request);
  provenance.provenance.supported_device_class.clear();
  EXPECT_EQ(Rejection(AdmitRouteCandidate(context, std::move(provenance))).invariant_id,
            "candidate.provenance.complete.v1");

  GeneratedRouteCandidate coordinate = CandidateDraft(board, compiled, request);
  std::get<ExactLinePrimitive>(coordinate.geometry.front()).centerline.start.x =
      board_ir::kMaxAbsDbCoord + 1;
  EXPECT_EQ(Rejection(AdmitRouteCandidate(context, std::move(coordinate))).invariant_id,
            "candidate.geometry.coordinate_bounds.v1");

  GeneratedRouteCandidate endpoint = CandidateDraft(board, compiled, request);
  std::get<ExactLinePrimitive>(endpoint.geometry.front()).centerline.start.x = 10;
  EXPECT_EQ(Rejection(AdmitRouteCandidate(context, std::move(endpoint))).invariant_id,
            "candidate.terminals.endpoints.v1");

  for (int corruption = 0; corruption < 3; ++corruption) {
    GeneratedRouteCandidate resource = CandidateDraft(board, compiled, request);
    ASSERT_FALSE(resource.resources.empty());
    if (corruption == 0) {
      resource.resources.front().direction = geometry_compiler::Direction::kWest;
    } else if (corruption == 1) {
      resource.resources.front().edge_count = 0;
    } else {
      resource.resources.front().usage_units = 2;
    }
    EXPECT_EQ(Rejection(AdmitRouteCandidate(context, std::move(resource))).invariant_id,
              "candidate.resources.equivalence.v1");
  }

  GeneratedRouteCandidate constraints = CandidateDraft(board, compiled, request);
  constraints.constraints.connected_intended_terminal_count = 1;
  EXPECT_EQ(Rejection(AdmitRouteCandidate(context, std::move(constraints))).invariant_id,
            "candidate.constraints.equivalence.v1");
}

TEST(RouteCandidateTest, ReusedPhysicalEdgeIsExplicitlyInvalidInLoopFreeV1) {
  BoardData data = test_support::ValidM1BoardData();
  data.obstacles.clear();
  const BoardSnapshot board = Snapshot(std::move(data));
  const CompiledBoard compiled = Compile(board, test_support::DefaultCompilerProfile({0}));
  const CpuRouteRequest request = TwoTerminalRequest(board, 0, 0);
  GeneratedRouteCandidate loop = CandidateDraft(board, compiled, request);
  loop.geometry = {
      ExactLinePrimitive{.layer = 0,
                         .centerline = {.start = {.x = 0, .y = 0}, .end = {.x = 20, .y = 0}}},
      ExactLinePrimitive{.layer = 0,
                         .centerline = {.start = {.x = 20, .y = 0}, .end = {.x = 10, .y = 0}}},
      ExactLinePrimitive{.layer = 0,
                         .centerline = {.start = {.x = 10, .y = 0}, .end = {.x = 100, .y = 0}}},
  };
  const CandidateAdmissionResult result = AdmitRouteCandidate(
      CandidateAdmissionContext{.board = board, .compiled_board = compiled, .request = request},
      std::move(loop));
  const CandidateRejection& rejection = Rejection(result);
  EXPECT_EQ(rejection.code, CandidateRejectionCode::kResourceMismatch);
  EXPECT_EQ(rejection.invariant_id, "candidate.resources.reused_edge.v1");
}

TEST(RouteCandidateTest, RepeatedVerticesAndNonconsecutiveCrossingsFailExactAdmission) {
  BoardData data = test_support::ValidM1BoardData();
  data.obstacles.clear();
  const BoardSnapshot board = Snapshot(std::move(data));
  const CompiledBoard compiled = Compile(board, test_support::DefaultCompilerProfile({0}));
  const CpuRouteRequest request = TwoTerminalRequest(board, 0, 0);
  const CandidateAdmissionContext context{
      .board = board, .compiled_board = compiled, .request = request};

  GeneratedRouteCandidate repeated_vertex = CandidateDraft(board, compiled, request);
  repeated_vertex.geometry = {
      ExactLinePrimitive{.layer = 0,
                         .centerline = {.start = {.x = 0, .y = 0}, .end = {.x = 0, .y = 10}}},
      ExactLinePrimitive{.layer = 0,
                         .centerline = {.start = {.x = 0, .y = 10}, .end = {.x = 10, .y = 10}}},
      ExactLinePrimitive{.layer = 0,
                         .centerline = {.start = {.x = 10, .y = 10}, .end = {.x = 10, .y = 0}}},
      ExactLinePrimitive{.layer = 0,
                         .centerline = {.start = {.x = 10, .y = 0}, .end = {.x = 0, .y = 0}}},
      ExactLinePrimitive{.layer = 0,
                         .centerline = {.start = {.x = 0, .y = 0}, .end = {.x = -10, .y = -10}}},
      ExactLinePrimitive{
          .layer = 0, .centerline = {.start = {.x = -10, .y = -10}, .end = {.x = 100, .y = -10}}},
      ExactLinePrimitive{.layer = 0,
                         .centerline = {.start = {.x = 100, .y = -10}, .end = {.x = 100, .y = 0}}},
  };
  EXPECT_EQ(Rejection(AdmitRouteCandidate(context, std::move(repeated_vertex))).invariant_id,
            "candidate.geometry.reused_vertex.v1");

  GeneratedRouteCandidate crossing = CandidateDraft(board, compiled, request);
  crossing.geometry = {
      ExactLinePrimitive{.layer = 0,
                         .centerline = {.start = {.x = 0, .y = 0}, .end = {.x = 10, .y = 10}}},
      ExactLinePrimitive{.layer = 0,
                         .centerline = {.start = {.x = 10, .y = 10}, .end = {.x = 20, .y = 10}}},
      ExactLinePrimitive{.layer = 0,
                         .centerline = {.start = {.x = 20, .y = 10}, .end = {.x = 20, .y = 0}}},
      ExactLinePrimitive{.layer = 0,
                         .centerline = {.start = {.x = 20, .y = 0}, .end = {.x = 10, .y = 0}}},
      ExactLinePrimitive{.layer = 0,
                         .centerline = {.start = {.x = 10, .y = 0}, .end = {.x = -10, .y = 20}}},
      ExactLinePrimitive{.layer = 0,
                         .centerline = {.start = {.x = -10, .y = 20}, .end = {.x = 100, .y = 20}}},
      ExactLinePrimitive{.layer = 0,
                         .centerline = {.start = {.x = 100, .y = 20}, .end = {.x = 100, .y = 0}}},
  };
  GeneratedRouteCandidate crossing_for_budget = crossing;
  const CandidateAdmissionResult exhausted =
      AdmitRouteCandidateWithReducedSelfClearanceBudgetForTesting(
          context, std::move(crossing_for_budget), 0);
  const CandidateRejection& budget_rejection = Rejection(exhausted);
  EXPECT_EQ(budget_rejection.code, CandidateRejectionCode::kBudgetExhausted);
  EXPECT_EQ(budget_rejection.invariant_id, "candidate.geometry.self_clearance_pair_budget.v1");
  EXPECT_EQ(budget_rejection.expected_value, 0U);
  EXPECT_EQ(budget_rejection.actual_value, 1U);
  EXPECT_TRUE(budget_rejection.primitive_witness_index.has_value());

  EXPECT_EQ(Rejection(AdmitRouteCandidate(context, std::move(crossing))).invariant_id,
            "candidate.geometry.swept_self_overlap.v1");
}

TEST(RouteCandidateTest, SelfClearanceEqualityIsLegalAndOneDbuInsideIsRejected) {
  BoardData data = test_support::ValidM1BoardData();
  data.obstacles.clear();
  const BoardSnapshot board = Snapshot(std::move(data));
  const CompiledBoard compiled = Compile(board, test_support::DefaultCompilerProfile({0}));
  CpuRouteRequest request = TwoTerminalRequest(board, 0, 0);
  const CandidateAdmissionContext context{
      .board = board, .compiled_board = compiled, .request = request};
  const std::array equality_segments = {
      LayerSegment{.layer = 0, .centerline = {.start = {.x = 0, .y = 0}, .end = {.x = 20, .y = 0}}},
      LayerSegment{.layer = 0,
                   .centerline = {.start = {.x = 20, .y = 0}, .end = {.x = 20, .y = 10}}},
      LayerSegment{.layer = 0,
                   .centerline = {.start = {.x = 20, .y = 10}, .end = {.x = 0, .y = 10}}},
      LayerSegment{.layer = 0,
                   .centerline = {.start = {.x = 0, .y = 10}, .end = {.x = 0, .y = 20}}},
      LayerSegment{.layer = 0,
                   .centerline = {.start = {.x = 0, .y = 20}, .end = {.x = 100, .y = 20}}},
      LayerSegment{.layer = 0,
                   .centerline = {.start = {.x = 100, .y = 20}, .end = {.x = 100, .y = 0}}},
  };
  GeneratedRouteCandidate equality =
      DraftFromSegments(board, compiled, request, equality_segments, 195);
  EXPECT_TRUE(std::holds_alternative<RouteCandidate>(AdmitRouteCandidate(context, equality)));

  std::get<ExactLinePrimitive>(equality.geometry[1]).centerline.end.y = 9;
  std::get<ExactLinePrimitive>(equality.geometry[2]).centerline.start.y = 9;
  std::get<ExactLinePrimitive>(equality.geometry[2]).centerline.end.y = 9;
  std::get<ExactLinePrimitive>(equality.geometry[3]).centerline.start.y = 9;
  const CandidateAdmissionResult rejected = AdmitRouteCandidate(context, std::move(equality));
  const CandidateRejection& rejection = Rejection(rejected);
  EXPECT_EQ(rejection.code, CandidateRejectionCode::kExactValidation);
  EXPECT_EQ(rejection.invariant_id, "candidate.geometry.swept_self_overlap.v1");
}

TEST(RouteCandidateTest, IntrinsicBaseCostIgnoresRequestLocalPolicySurcharges) {
  BoardData data = test_support::ValidM1BoardData();
  data.obstacles.clear();
  const BoardSnapshot board = Snapshot(std::move(data));
  const CompiledBoard compiled = Compile(board, test_support::DefaultCompilerProfile({0}));
  CpuRouteRequest request = TwoTerminalRequest(board, 0, 0);
  // Keep the direct all-orthogonal route optimal so this isolates metric
  // reconstruction instead of selecting a diagonal alternative.
  request.candidate_policy.orthogonal_step_surcharge = 1;
  const GeneratedRouteCandidate candidate = CandidateDraft(board, compiled, request);
  EXPECT_EQ(candidate.policy.orthogonal_step_surcharge, 1U);
  EXPECT_EQ(candidate.metrics.intrinsic_base_cost, 100U);
  EXPECT_EQ(candidate.metrics.scalar_policy_cost, 110U);
  EXPECT_TRUE(std::holds_alternative<RouteCandidate>(AdmitRouteCandidate(
      CandidateAdmissionContext{.board = board, .compiled_board = compiled, .request = request},
      candidate)));
}

TEST(RouteCandidateTest, CpuBuilderDerivesCpuOnlyProvenanceFromTypedEvidence) {
  const BoardSnapshot board = Snapshot();
  const CompiledBoard compiled = Compile(board, test_support::DefaultCompilerProfile({0}));
  const CpuRouteRequest request = TwoTerminalRequest(board, 0, 0);
  const routing::NormalizedCandidateGenerationPolicy policy = NormalizePolicy(compiled, request);
  const routing::CpuRoute route = test_support::CpuRouteForCandidate(board, compiled, request);

  const CandidateDraftBuildResult result = BuildGeneratedCandidateFromCpuRoute(
      board, compiled, request, policy, route,
      CandidateSchedulingIdentity{.batch_identity = 91, .query_identity = 93});
  ASSERT_TRUE(std::holds_alternative<GeneratedRouteCandidate>(result));
  const CandidateProvenance& provenance = std::get<GeneratedRouteCandidate>(result).provenance;
  EXPECT_EQ(provenance.generator, CandidateGeneratorKind::kCpuAStar);
  EXPECT_EQ(provenance.backend, CandidateBackendKind::kCpu);
  EXPECT_EQ(provenance.supported_device_class, "cpu-reference-v1");
  EXPECT_EQ(provenance.batch_identity, 91U);
  EXPECT_EQ(provenance.query_identity, 93U);

  const CandidateDraftBuildResult invalid_scheduling = BuildGeneratedCandidateFromCpuRoute(
      board, compiled, request, policy, route,
      CandidateSchedulingIdentity{.batch_identity = 0, .query_identity = 93});
  ASSERT_TRUE(std::holds_alternative<CandidateRejection>(invalid_scheduling));
  const CandidateRejection& rejection = std::get<CandidateRejection>(invalid_scheduling);
  EXPECT_EQ(rejection.invariant_id, "candidate.builder.scheduling_identity.v1");
  EXPECT_EQ(rejection.provenance.generator, CandidateGeneratorKind::kCpuAStar);
  EXPECT_EQ(rejection.provenance.backend, CandidateBackendKind::kCpu);

  routing::CpuRoute caller_fabricated = route;
  caller_fabricated.producer_evidence = {};
  const CandidateDraftBuildResult unauthenticated = BuildGeneratedCandidateFromCpuRoute(
      board, compiled, request, policy, caller_fabricated,
      CandidateSchedulingIdentity{.batch_identity = 91, .query_identity = 93});
  ASSERT_TRUE(std::holds_alternative<CandidateRejection>(unauthenticated));
  EXPECT_EQ(std::get<CandidateRejection>(unauthenticated).invariant_id,
            "candidate.builder.cpu_producer_authentication.v1");

  CandidateAssociations stale_associations = AssociationsFor(board, compiled);
  ++stale_associations.board_content_hash;
  const CandidateDraftBuildResult stale = test_support::BuildUnsealedCandidateFromSegments(
      board, compiled, request, policy, route.segments, route.total_cost,
      CandidateSchedulingIdentity{.batch_identity = 91, .query_identity = 93}, stale_associations);
  ASSERT_TRUE(std::holds_alternative<CandidateRejection>(stale));
  const CandidateRejection& stale_rejection = std::get<CandidateRejection>(stale);
  EXPECT_EQ(stale_rejection.invariant_id, "candidate.builder.planar_route_association.v1");
  EXPECT_EQ(stale_rejection.associations.board_content_hash, stale_associations.board_content_hash);
  EXPECT_TRUE(stale_rejection.candidate_id.has_value());
  EXPECT_TRUE(stale_rejection.candidate_payload_checksum.has_value());
}

TEST(RouteCandidateTest, CpuEvidenceAuthenticatesSegmentsNotRedundantDiagnostics) {
  const BoardSnapshot board = Snapshot();
  const CompiledBoard compiled = Compile(board, test_support::DefaultCompilerProfile({0}));
  const CpuRouteRequest request = TwoTerminalRequest(board, 0, 0);
  const routing::NormalizedCandidateGenerationPolicy policy = NormalizePolicy(compiled, request);
  const routing::CpuRoute route = test_support::CpuRouteForCandidate(board, compiled, request);

  routing::CpuRoute diagnostic_rewrite = route;
  diagnostic_rewrite.lattice_path.clear();
  ++diagnostic_rewrite.telemetry.queue_pops;
  EXPECT_TRUE(routing::CpuRouteHasAuthenticatedAStarEvidence(diagnostic_rewrite));
  EXPECT_TRUE(std::holds_alternative<GeneratedRouteCandidate>(BuildGeneratedCandidateFromCpuRoute(
      board, compiled, request, policy, diagnostic_rewrite,
      CandidateSchedulingIdentity{.batch_identity = 101, .query_identity = 103})));

  routing::CpuRoute semantic_rewrite = route;
  ASSERT_FALSE(semantic_rewrite.segments.empty());
  ++semantic_rewrite.segments.front().centerline.end.x;
  EXPECT_FALSE(routing::CpuRouteHasAuthenticatedAStarEvidence(semantic_rewrite));
  const CandidateDraftBuildResult rejected = BuildGeneratedCandidateFromCpuRoute(
      board, compiled, request, policy, semantic_rewrite,
      CandidateSchedulingIdentity{.batch_identity = 101, .query_identity = 103});
  ASSERT_TRUE(std::holds_alternative<CandidateRejection>(rejected));
  EXPECT_EQ(std::get<CandidateRejection>(rejected).invariant_id,
            "candidate.builder.cpu_producer_authentication.v1");
}

TEST(RouteCandidateTest, AdmissionRejectsCpuPayloadRelabeledAsCudaProvenance) {
  const BoardSnapshot board = Snapshot();
  const CompiledBoard compiled = Compile(board, test_support::DefaultCompilerProfile({0}));
  const CpuRouteRequest request = TwoTerminalRequest(board, 0, 0);
  GeneratedRouteCandidate forged = CandidateDraft(board, compiled, request);
  forged.provenance.generator = CandidateGeneratorKind::kCudaSweep;
  forged.provenance.backend = CandidateBackendKind::kCuda;
  forged.provenance.supported_device_class = "cuda-cc-12.0";
  forged.id =
      DeriveCandidateId(forged.net, forged.associations, forged.policy_identity, forged.provenance);
  ASSERT_FALSE(FinalizeGeneratedCandidateDraft(forged).has_value());

  const CandidateAdmissionResult result = AdmitRouteCandidate(
      CandidateAdmissionContext{.board = board, .compiled_board = compiled, .request = request},
      std::move(forged));

  const CandidateRejection& rejection = Rejection(result);
  EXPECT_EQ(rejection.code, CandidateRejectionCode::kAssociationMismatch);
  EXPECT_EQ(rejection.invariant_id, "candidate.provenance.producer_authentication.v1");
  EXPECT_EQ(rejection.provenance.generator, CandidateGeneratorKind::kCudaSweep);
  EXPECT_EQ(rejection.provenance.backend, CandidateBackendKind::kCuda);
}

TEST(RouteCandidateTest, RejectionChecksumIsIndependentlyRecomputedForBoundedPayload) {
  const BoardSnapshot board = Snapshot();
  const CompiledBoard compiled = Compile(board, test_support::DefaultCompilerProfile({0}));
  const CpuRouteRequest request = TwoTerminalRequest(board, 0, 0);
  GeneratedRouteCandidate corrupt = CandidateDraft(board, compiled, request);
  ++corrupt.metrics.scalar_policy_cost;
  corrupt.payload_checksum = 0xfeedU;
  const std::uint64_t expected_checksum = ComputeCandidatePayloadChecksum(corrupt);
  ASSERT_NE(expected_checksum, corrupt.payload_checksum);

  const CandidateAdmissionResult result = AdmitRouteCandidate(
      CandidateAdmissionContext{.board = board, .compiled_board = compiled, .request = request},
      std::move(corrupt));

  const CandidateRejection& rejection = Rejection(result);
  EXPECT_EQ(rejection.invariant_id, "candidate.metrics.scalar_cost.v1");
  EXPECT_EQ(rejection.candidate_payload_checksum, expected_checksum);
  EXPECT_NE(rejection.candidate_payload_checksum, 0xfeedU);
}

TEST(RouteCandidateTest, CandidateShapePreflightRejectsBulkBeforeChecksumming) {
  const BoardSnapshot board = Snapshot();
  const CompiledBoard compiled = Compile(board, test_support::DefaultCompilerProfile({0}));
  const CpuRouteRequest request = TwoTerminalRequest(board, 0, 0);
  const CandidateAdmissionContext context{
      .board = board, .compiled_board = compiled, .request = request};

  GeneratedRouteCandidate geometry = CandidateDraft(board, compiled, request);
  geometry.geometry.resize(kMaximumCandidatePrimitives + 1U);
  geometry.payload_checksum = 0x1111U;
  const CandidateAdmissionResult geometry_result =
      AdmitRouteCandidate(context, std::move(geometry));
  const CandidateRejection& geometry_rejection = Rejection(geometry_result);
  EXPECT_EQ(geometry_rejection.invariant_id, "candidate.geometry.primitive_count.v1");
  EXPECT_FALSE(geometry_rejection.candidate_payload_checksum.has_value());

  GeneratedRouteCandidate resources = CandidateDraft(board, compiled, request);
  resources.resources.resize(kMaximumCandidateResourceSpans + 1U);
  resources.payload_checksum = 0x2222U;
  const CandidateAdmissionResult resource_result =
      AdmitRouteCandidate(context, std::move(resources));
  const CandidateRejection& resource_rejection = Rejection(resource_result);
  EXPECT_EQ(resource_rejection.invariant_id, "candidate.resources.span_count.v1");
  EXPECT_FALSE(resource_rejection.candidate_payload_checksum.has_value());

  GeneratedRouteCandidate device_class = CandidateDraft(board, compiled, request);
  device_class.provenance.supported_device_class.assign(kMaximumCandidateDiagnosticBytes + 1U, 'd');
  device_class.payload_checksum = 0x3333U;
  const CandidateAdmissionResult device_result =
      AdmitRouteCandidate(context, std::move(device_class));
  const CandidateRejection& device_rejection = Rejection(device_result);
  EXPECT_EQ(device_rejection.invariant_id, "candidate.provenance.device_class_bytes.v1");
  EXPECT_FALSE(device_rejection.candidate_payload_checksum.has_value());
  EXPECT_EQ(device_rejection.provenance.supported_device_class, "invalid-device-class");
}

TEST(RouteCandidateTest, ExpandedResourceMaterializationHasDeterministicSchemaBound) {
  const BoardSnapshot board = Snapshot();
  const CompiledBoard compiled = Compile(board, test_support::DefaultCompilerProfile({0}));
  const CpuRouteRequest request = TwoTerminalRequest(board, 0, 0);
  GeneratedRouteCandidate generated = CandidateDraft(board, compiled, request);
  const std::uint64_t expanded_edges =
      generated.metrics.orthogonal_step_count + generated.metrics.diagonal_step_count;
  ASSERT_GT(expanded_edges, 0U);
  const std::uint64_t reduced_budget = expanded_edges - 1U;

  const CandidateAdmissionResult result =
      AdmitRouteCandidateWithReducedResourceEdgeBudgetForTesting(
          CandidateAdmissionContext{.board = board, .compiled_board = compiled, .request = request},
          std::move(generated), reduced_budget);

  const CandidateRejection& rejection = Rejection(result);
  EXPECT_EQ(rejection.stage, CandidateLifecycleStage::kResourceAccounted);
  EXPECT_EQ(rejection.code, CandidateRejectionCode::kBudgetExhausted);
  EXPECT_EQ(rejection.invariant_id, "candidate.resources.expanded_edge_budget.v1");
  EXPECT_EQ(rejection.expected_value, reduced_budget);
  EXPECT_EQ(rejection.actual_value, expanded_edges);
}

TEST(RouteCandidateTest, OversizedPolicyBulkIsRejectedBeforeBuilderOrAdmissionChecksumming) {
  BoardData data = test_support::ValidM1BoardData();
  data.obstacles.clear();
  const BoardSnapshot board = Snapshot(std::move(data));
  const CompiledBoard compiled = Compile(board, test_support::DefaultCompilerProfile({0}));
  const CpuRouteRequest request = TwoTerminalRequest(board, 0, 0);
  routing::NormalizedCandidateGenerationPolicy forged_policy = NormalizePolicy(compiled, request);
  const routing::CpuRoute route = test_support::CpuRouteForCandidate(board, compiled, request);
  forged_policy.policy.banned_resources.resize(routing::kMaximumPolicyResourceEntries + 1U);
  ASSERT_FALSE(routing::CandidateGenerationPolicyShapeIsWithinV1Bounds(forged_policy.policy));

  const CandidateDraftBuildResult built = BuildGeneratedCandidateFromCpuRoute(
      board, compiled, request, forged_policy, route,
      CandidateSchedulingIdentity{.batch_identity = 131, .query_identity = 137});
  ASSERT_TRUE(std::holds_alternative<CandidateRejection>(built));
  const CandidateRejection& builder_rejection = std::get<CandidateRejection>(built);
  EXPECT_EQ(builder_rejection.invariant_id, "candidate.policy.resource_entry_count.v1");
  EXPECT_EQ(builder_rejection.expected_value, routing::kMaximumPolicyResourceEntries);
  EXPECT_EQ(builder_rejection.actual_value, routing::kMaximumPolicyResourceEntries + 1U);
  EXPECT_FALSE(builder_rejection.candidate_payload_checksum.has_value());

  forged_policy.policy.banned_resources.clear();
  forged_policy.policy.banned_resources.shrink_to_fit();
  GeneratedRouteCandidate generated = CandidateDraft(board, compiled, request);
  generated.policy.resource_penalties.resize(routing::kMaximumPolicyResourceEntries + 1U);
  generated.payload_checksum = 0xfeedU;
  const CandidateAdmissionResult admitted = AdmitRouteCandidate(
      CandidateAdmissionContext{.board = board, .compiled_board = compiled, .request = request},
      std::move(generated));
  const CandidateRejection& admission_rejection = Rejection(admitted);
  EXPECT_EQ(admission_rejection.invariant_id, "candidate.policy.resource_entry_count.v1");
  EXPECT_EQ(admission_rejection.expected_value, routing::kMaximumPolicyResourceEntries);
  EXPECT_EQ(admission_rejection.actual_value, routing::kMaximumPolicyResourceEntries + 1U);
  EXPECT_FALSE(admission_rejection.candidate_payload_checksum.has_value());
}

TEST(RouteCandidateTest, GpuBatchItemsCannotBeFabricatedAsValidatedEvidence) {
  static_assert(std::is_final_v<gpu::CudaPlanarRouteBackend>);
  static_assert(!std::is_default_constructible_v<gpu::CudaPlanarRouteBackend>);
  static_assert(!std::is_aggregate_v<gpu::PlanarCandidateBatchItem>);
  static_assert(!std::is_default_constructible_v<gpu::PlanarCandidateBatchItem>);
  static_assert(!std::is_constructible_v<gpu::PlanarCandidateBatchItem, std::uint64_t,
                                         std::uint32_t, std::uint64_t, gpu::PlanarGpuRouteResult>);
  SUCCEED();
}

TEST(RouteCandidateTest, BuilderFailuresRetainVersionedExactAndResourceDiagnostics) {
  const auto build = [](const BoardSnapshot& board, const CompiledBoard& compiled,
                        const CpuRouteRequest& request, std::vector<LayerSegment> segments,
                        std::uint64_t total_cost) -> CandidateDraftBuildResult {
    const routing::NormalizedCandidateGenerationPolicy policy = NormalizePolicy(compiled, request);
    const CandidateAssociations associations = AssociationsFor(board, compiled);
    return test_support::BuildUnsealedCandidateFromSegments(
        board, compiled, request, policy, segments, total_cost,
        CandidateSchedulingIdentity{.batch_identity = 107, .query_identity = 109}, associations);
  };

  const BoardSnapshot blocked_board = Snapshot();
  const CompiledBoard blocked_compiled =
      Compile(blocked_board, test_support::DefaultCompilerProfile({0}));
  const CpuRouteRequest blocked_request = TwoTerminalRequest(blocked_board, 0, 0);
  CandidateDraftBuildResult exact = build(
      blocked_board, blocked_compiled, blocked_request,
      {LayerSegment{.layer = 0,
                    .centerline = {.start = blocked_request.start, .end = blocked_request.goal}}},
      100);
  ASSERT_TRUE(std::holds_alternative<CandidateRejection>(exact));
  const CandidateRejection& exact_rejection = std::get<CandidateRejection>(exact);
  EXPECT_EQ(exact_rejection.stage, CandidateLifecycleStage::kExactValidated);
  EXPECT_EQ(exact_rejection.invariant_id, "candidate.geometry.swept_clearance.v1");
  EXPECT_TRUE(exact_rejection.candidate_id.has_value());
  EXPECT_TRUE(exact_rejection.net.has_value());
  EXPECT_TRUE(exact_rejection.primitive_witness_index.has_value());
  EXPECT_TRUE(exact_rejection.conflicting_entity.has_value());
  EXPECT_TRUE(exact_rejection.candidate_payload_checksum.has_value());
  EXPECT_GT(exact_rejection.logical_bytes, 0U);

  BoardData clear_data = test_support::ValidM1BoardData();
  clear_data.obstacles.clear();
  const BoardSnapshot clear_board = Snapshot(std::move(clear_data));
  const CompiledBoard clear_compiled =
      Compile(clear_board, test_support::DefaultCompilerProfile({0}));
  const CpuRouteRequest clear_request = TwoTerminalRequest(clear_board, 0, 0);
  CandidateDraftBuildResult resource = build(
      clear_board, clear_compiled, clear_request,
      {
          LayerSegment{.layer = 0,
                       .centerline = {.start = {.x = 0, .y = 0}, .end = {.x = 15, .y = 15}}},
          LayerSegment{.layer = 0,
                       .centerline = {.start = {.x = 15, .y = 15}, .end = {.x = 85, .y = 15}}},
          LayerSegment{.layer = 0,
                       .centerline = {.start = {.x = 85, .y = 15}, .end = {.x = 100, .y = 0}}},
      },
      0);
  ASSERT_TRUE(std::holds_alternative<CandidateRejection>(resource));
  const CandidateRejection& resource_rejection = std::get<CandidateRejection>(resource);
  EXPECT_EQ(resource_rejection.stage, CandidateLifecycleStage::kResourceAccounted);
  EXPECT_EQ(resource_rejection.invariant_id, "candidate.resources.lattice_alignment.v1");
  EXPECT_TRUE(resource_rejection.primitive_witness_index.has_value());
  EXPECT_TRUE(resource_rejection.candidate_payload_checksum.has_value());
  EXPECT_GT(resource_rejection.logical_bytes, 0U);

  geometry_compiler::CompilerProfile sparse_profile = test_support::DefaultCompilerProfile({0});
  sparse_profile.active_regions = {
      geometry_compiler::ActiveRegion{
          .layer = 0, .bounds = {.min = {.x = 0, .y = 0}, .max = {.x = 10, .y = 0}}},
      geometry_compiler::ActiveRegion{
          .layer = 0, .bounds = {.min = {.x = 90, .y = 0}, .max = {.x = 100, .y = 0}}},
  };
  const CompiledBoard sparse_compiled = Compile(clear_board, std::move(sparse_profile));
  CandidateDraftBuildResult sparse =
      build(clear_board, sparse_compiled, clear_request,
            {LayerSegment{.layer = 0,
                          .centerline = {.start = {.x = 0, .y = 0}, .end = {.x = 100, .y = 0}}}},
            100);
  ASSERT_TRUE(std::holds_alternative<CandidateRejection>(sparse));
  const CandidateRejection& sparse_rejection = std::get<CandidateRejection>(sparse);
  EXPECT_EQ(sparse_rejection.code, CandidateRejectionCode::kResourceMismatch);
  EXPECT_EQ(sparse_rejection.invariant_id, "candidate.resources.compiled_edge.v1");
}

TEST(RouteCandidateTest, BuilderChecksPrimitiveBoundBeforeDraftAllocation) {
  BoardData data = test_support::ValidM1BoardData();
  data.obstacles.clear();
  const BoardSnapshot board = Snapshot(std::move(data));
  const CompiledBoard compiled = Compile(board, test_support::DefaultCompilerProfile({0}));
  const CpuRouteRequest request = TwoTerminalRequest(board, 0, 0);
  const routing::NormalizedCandidateGenerationPolicy policy = NormalizePolicy(compiled, request);
  const CandidateAssociations associations = AssociationsFor(board, compiled);
  const auto build_count = [&](std::size_t count) {
    const std::vector<LayerSegment> segments(
        count, LayerSegment{.layer = 0,
                            .centerline = {.start = {.x = 0, .y = 0}, .end = {.x = 10, .y = 0}}});
    return test_support::BuildUnsealedCandidateFromSegments(
        board, compiled, request, policy, segments, 0,
        CandidateSchedulingIdentity{.batch_identity = 113, .query_identity = 127}, associations);
  };

  const CandidateDraftBuildResult at_max = build_count(kMaximumCandidatePrimitives);
  ASSERT_TRUE(std::holds_alternative<CandidateRejection>(at_max));
  EXPECT_NE(std::get<CandidateRejection>(at_max).invariant_id,
            "candidate.geometry.primitive_count.v1");

  const CandidateDraftBuildResult over_max = build_count(kMaximumCandidatePrimitives + 1U);
  ASSERT_TRUE(std::holds_alternative<CandidateRejection>(over_max));
  const CandidateRejection& rejection = std::get<CandidateRejection>(over_max);
  EXPECT_EQ(rejection.stage, CandidateLifecycleStage::kNormalized);
  EXPECT_EQ(rejection.invariant_id, "candidate.geometry.primitive_count.v1");
  EXPECT_EQ(rejection.expected_value, kMaximumCandidatePrimitives);
  EXPECT_EQ(rejection.actual_value, kMaximumCandidatePrimitives + 1U);
  EXPECT_FALSE(rejection.candidate_payload_checksum.has_value());
  EXPECT_GT(rejection.logical_bytes, 0U);
}

TEST(RouteCandidateTest, BuilderRejectsPolicyMisattributionEvenWhenGeometryAndCostMatch) {
  BoardData data = test_support::ValidM1BoardData();
  data.obstacles.clear();
  const BoardSnapshot board = Snapshot(std::move(data));
  const CompiledBoard compiled = Compile(board, test_support::DefaultCompilerProfile({0}));
  CpuRouteRequest request = TwoTerminalRequest(board, 0, 0);
  const routing::NormalizedCandidateGenerationPolicy first_policy =
      NormalizePolicy(compiled, request);
  const routing::CpuRoute route = test_support::CpuRouteForCandidate(board, compiled, request);

  ++request.candidate_policy.candidate_ordinal;
  const routing::NormalizedCandidateGenerationPolicy second_policy =
      NormalizePolicy(compiled, request);
  ASSERT_NE(first_policy.identity, second_policy.identity);
  const CandidateDraftBuildResult result = BuildGeneratedCandidateFromCpuRoute(
      board, compiled, request, second_policy, route,
      CandidateSchedulingIdentity{.batch_identity = 17, .query_identity = 1});
  ASSERT_TRUE(std::holds_alternative<CandidateRejection>(result));
  EXPECT_EQ(std::get<CandidateRejection>(result).invariant_id,
            "candidate.builder.planar_route_policy_association.v1");
}

}  // namespace
}  // namespace apgar::candidates
