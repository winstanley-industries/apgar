#include <atomic>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "apgar/benchmark/planar_corpus.h"
#include "apgar/board_ir/board.h"
#include "apgar/geometry_compiler/compiled_board.h"
#include "apgar/gpu/cuda_backend.h"
#include "apgar/gpu/fault_injecting_backend.h"
#include "apgar/gpu/planar_router.h"
#include "apgar/routing/cpu_astar.h"
#include "tests/support/board_builder.h"
#include "tests/support/compiler_builder.h"
#include "tests/support/google_test.h"
#include "tests/support/routing_builder.h"

namespace apgar::gpu {
namespace {

using board_ir::AxisAlignedBox64;
using board_ir::BoardData;
using board_ir::BoardSnapshot;
using board_ir::Point64;
using geometry_compiler::ActiveRegion;
using geometry_compiler::CompiledBoard;
using geometry_compiler::CompilerProfile;
using routing::CpuRoute;
using routing::CpuRouteRequest;
using routing::CpuRouteResult;

using test_support::Compile;
using test_support::ReadFixture;
using test_support::Snapshot;
using test_support::TwoTerminalRequest;

[[nodiscard]] DeviceCompiledBoardV1 Flatten(const BoardSnapshot& board,
                                            const CompiledBoard& compiled) {
  DeviceCompiledBoardResult result = BuildDeviceCompiledBoardV1(board, compiled);
  EXPECT_TRUE(std::holds_alternative<DeviceCompiledBoardV1>(result));
  if (!std::holds_alternative<DeviceCompiledBoardV1>(result)) {
    return {};
  }
  return std::get<DeviceCompiledBoardV1>(std::move(result));
}

void ExpectUploadInvariant(IPlanarRouteBackend& backend, const DeviceCompiledBoardV1& device) {
  UploadResult upload = backend.UploadCompiledView(device);
  ASSERT_TRUE(std::holds_alternative<BackendError>(upload));
  EXPECT_EQ(std::get<BackendError>(upload).code, BackendErrorCode::kInternalInvariant);
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
  EXPECT_GE(first_route.backend.compute_capability_major, 12U);
  EXPECT_GT(first_route.telemetry.examined_work, 0U);
  EXPECT_GT(first_route.telemetry.peak_device_bytes, 0U);
}

TEST(CudaPlanarRouteTest, FrontierAndSweepMatchCpuCostAndRepeatExactly) {
  const BoardSnapshot board = Snapshot();
  const CompiledBoard compiled = Compile(board, test_support::DefaultCompilerProfile({0}));
  const CpuRouteRequest request = TwoTerminalRequest(board);

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

  ExpectDifferentialSuccess(board, compiled, TwoTerminalRequest(board),
                            PlanarGenerator::kBucketedFrontier);
  ExpectDifferentialSuccess(board, compiled, TwoTerminalRequest(board),
                            PlanarGenerator::kHeadingAwareSweep);
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
      Route(board, disconnected_board, TwoTerminalRequest(board), PlanarRoutePolicy{});
  ASSERT_TRUE(std::holds_alternative<PlanarGpuFailure>(disconnected));
  EXPECT_EQ(std::get<PlanarGpuFailure>(disconnected).code, PlanarGpuFailureCode::kDisconnected);

  const CompiledBoard compiled = Compile(board, test_support::DefaultCompilerProfile());
  CpuRouteRequest through_via = TwoTerminalRequest(board);
  through_via.goal_layer = 31;
  const PlanarGpuRouteResult unsupported = Route(board, compiled, through_via, PlanarRoutePolicy{});
  ASSERT_TRUE(std::holds_alternative<PlanarGpuFailure>(unsupported));
  EXPECT_EQ(std::get<PlanarGpuFailure>(unsupported).code, PlanarGpuFailureCode::kUnsupported);

  std::atomic_bool cancellation = true;
  PlanarRoutePolicy cancelled_policy;
  cancelled_policy.cancellation = &cancellation;
  const PlanarGpuRouteResult cancelled =
      Route(board, compiled, TwoTerminalRequest(board), cancelled_policy);
  ASSERT_TRUE(std::holds_alternative<PlanarGpuFailure>(cancelled));
  EXPECT_EQ(std::get<PlanarGpuFailure>(cancelled).code, PlanarGpuFailureCode::kCancelled);

  PlanarRoutePolicy memory_policy;
  memory_policy.maximum_device_bytes = 1;
  const PlanarGpuRouteResult memory =
      Route(board, compiled, TwoTerminalRequest(board), memory_policy);
  ASSERT_TRUE(std::holds_alternative<PlanarGpuFailure>(memory));
  EXPECT_EQ(std::get<PlanarGpuFailure>(memory).code, PlanarGpuFailureCode::kResourceExhausted);

  PlanarRoutePolicy round_policy;
  round_policy.maximum_rounds = 1;
  const PlanarGpuRouteResult rounds =
      Route(board, compiled, TwoTerminalRequest(board), round_policy);
  ASSERT_TRUE(std::holds_alternative<PlanarGpuFailure>(rounds));
  EXPECT_EQ(std::get<PlanarGpuFailure>(rounds).code, PlanarGpuFailureCode::kResourceExhausted);

  PlanarRoutePolicy sweep_round_policy;
  sweep_round_policy.generator = PlanarGenerator::kHeadingAwareSweep;
  sweep_round_policy.maximum_rounds = 1;
  const PlanarGpuRouteResult sweep_rounds =
      Route(board, compiled, TwoTerminalRequest(board), sweep_round_policy);
  ASSERT_TRUE(std::holds_alternative<PlanarGpuFailure>(sweep_rounds));
  EXPECT_EQ(std::get<PlanarGpuFailure>(sweep_rounds).code, PlanarGpuFailureCode::kResourceExhausted)
      << std::get<PlanarGpuFailure>(sweep_rounds).detail;
}

TEST(CudaPlanarRouteTest, InjectedPredecessorCycleIsAnInvariantFailure) {
  const BoardSnapshot board = Snapshot();
  const CompiledBoard compiled = Compile(board, test_support::DefaultCompilerProfile({0}));
  std::unique_ptr<IPlanarRouteBackend> cuda_backend = CreateCudaPlanarRouteBackend();
  ASSERT_NE(cuda_backend, nullptr);
  std::unique_ptr<IPlanarRouteBackend> corrupting_backend = CreateFaultInjectingPlanarRouteBackend(
      *cuda_backend, UntrustedResultFault::kGoalPredecessorSelfCycle);
  ASSERT_NE(corrupting_backend, nullptr);
  const PlanarGpuRouteResult result = RouteWithPlanarGpuBackend(
      board, compiled, TwoTerminalRequest(board), PlanarRoutePolicy{}, *corrupting_backend);

  ASSERT_TRUE(std::holds_alternative<PlanarGpuFailure>(result));
  const PlanarGpuFailure& failure = std::get<PlanarGpuFailure>(result);
  EXPECT_EQ(failure.code, PlanarGpuFailureCode::kInternalInvariant);
  EXPECT_EQ(failure.invariant_id, "gpu.predecessor.self_reference.v1");
  EXPECT_TRUE(failure.telemetry.has_value());
}

TEST(CudaPlanarBackendTest, RejectsMalformedUploadsAndCoincidentBackendEndpoints) {
  const BoardSnapshot board = Snapshot();
  const CompiledBoard compiled = Compile(board, test_support::DefaultCompilerProfile({0}));
  const DeviceCompiledBoardV1 valid = Flatten(board, compiled);
  std::unique_ptr<IPlanarRouteBackend> backend = CreateCudaPlanarRouteBackend();
  ASSERT_NE(backend, nullptr);

  DeviceCompiledBoardV1 bad_neighbor = valid;
  bool changed_neighbor = false;
  for (DeviceNodeV1& node : bad_neighbor.nodes) {
    for (std::uint32_t& neighbor : node.neighbors) {
      if (neighbor != kInvalidNodeIndex) {
        neighbor = static_cast<std::uint32_t>(bad_neighbor.nodes.size());
        changed_neighbor = true;
        break;
      }
    }
    if (changed_neighbor) {
      break;
    }
  }
  ASSERT_TRUE(changed_neighbor);
  bad_neighbor.header.device_view_fingerprint =
      ComputeDeviceCompiledBoardFingerprintV1(bad_neighbor);
  ExpectUploadInvariant(*backend, bad_neighbor);

  DeviceCompiledBoardV1 bad_run = valid;
  ASSERT_FALSE(bad_run.runs.empty());
  bad_run.runs.front().node_offset = static_cast<std::uint32_t>(bad_run.run_nodes.size());
  bad_run.header.device_view_fingerprint = ComputeDeviceCompiledBoardFingerprintV1(bad_run);
  ExpectUploadInvariant(*backend, bad_run);

  DeviceCompiledBoardV1 stale_fingerprint = valid;
  ++stale_fingerprint.header.device_view_fingerprint;
  ExpectUploadInvariant(*backend, stale_fingerprint);

  UploadResult uploaded_result = backend->UploadCompiledView(valid);
  ASSERT_TRUE(std::holds_alternative<std::unique_ptr<UploadedCompiledView>>(uploaded_result));
  std::unique_ptr<UploadedCompiledView> uploaded =
      std::get<std::unique_ptr<UploadedCompiledView>>(std::move(uploaded_result));
  ASSERT_NE(uploaded, nullptr);
  const BackendExecutionRequest coincident{
      .generator = PlanarGenerator::kBucketedFrontier,
      .start_node = 0,
      .goal_node = 0,
      .maximum_rounds = 1,
      .maximum_device_bytes = std::numeric_limits<std::uint64_t>::max(),
      .cancellation = nullptr,
  };
  ExecutionResult execution = backend->ExecuteRoute(*uploaded, coincident);
  ASSERT_TRUE(std::holds_alternative<BackendError>(execution));
  EXPECT_EQ(std::get<BackendError>(execution).code, BackendErrorCode::kInternalInvariant);
}

TEST(CudaPlanarBackendTest, PreparedFrontierIsExactlyRepeatableUnderDenseContention) {
  benchmark::PlanarCorpusResult corpus_result = benchmark::BuildPlanarBakeoffCorpus(ReadFixture());
  ASSERT_TRUE(std::holds_alternative<std::vector<benchmark::PlanarCorpusCase>>(corpus_result));
  const benchmark::PlanarCorpusCase& dense =
      std::get<std::vector<benchmark::PlanarCorpusCase>>(corpus_result).front();
  ASSERT_EQ(dense.family, "dense-corridors");
  std::unique_ptr<IPlanarRouteBackend> backend = CreateCudaPlanarRouteBackend();
  ASSERT_NE(backend, nullptr);
  PreparedPlanarCompiledViewResult prepared_result =
      PreparePlanarCompiledView(dense.board, dense.compiled_board, *backend);
  ASSERT_TRUE(std::holds_alternative<std::unique_ptr<PreparedPlanarCompiledView>>(prepared_result));
  std::unique_ptr<PreparedPlanarCompiledView> prepared =
      std::get<std::unique_ptr<PreparedPlanarCompiledView>>(std::move(prepared_result));
  ASSERT_NE(prepared, nullptr);

  std::optional<std::uint64_t> expected_cost;
  std::vector<Point64> expected_path;
  std::vector<routing::LayerSegment> expected_segments;
  for (int repetition = 0; repetition < 16; ++repetition) {
    const PlanarGpuRouteResult result = RouteWithPreparedPlanarGpuBackend(
        dense.board, dense.compiled_board, dense.request,
        PlanarRoutePolicy{.generator = PlanarGenerator::kBucketedFrontier}, *prepared);
    ASSERT_TRUE(std::holds_alternative<PlanarGpuRoute>(result))
        << (std::holds_alternative<PlanarGpuFailure>(result)
                ? std::get<PlanarGpuFailure>(result).detail
                : "");
    const PlanarGpuRoute& route = std::get<PlanarGpuRoute>(result);
    if (!expected_cost.has_value()) {
      expected_cost = route.total_cost;
      expected_path = route.lattice_path;
      expected_segments = route.segments;
    } else {
      EXPECT_EQ(route.total_cost, *expected_cost);
      EXPECT_EQ(route.lattice_path, expected_path);
      EXPECT_EQ(route.segments, expected_segments);
    }
  }
}

TEST(CudaPlanarDifferentialTest, ForcedGeneratorsMatchCpuAcrossVersionedBakeoffCorpus) {
  benchmark::PlanarCorpusResult corpus_result = benchmark::BuildPlanarBakeoffCorpus(ReadFixture());
  ASSERT_TRUE(std::holds_alternative<std::vector<benchmark::PlanarCorpusCase>>(corpus_result))
      << (std::holds_alternative<std::string>(corpus_result) ? std::get<std::string>(corpus_result)
                                                             : "");
  const std::vector<benchmark::PlanarCorpusCase>& corpus =
      std::get<std::vector<benchmark::PlanarCorpusCase>>(corpus_result);
  ASSERT_EQ(corpus.size(), 8U);

  for (const benchmark::PlanarCorpusCase& test_case : corpus) {
    SCOPED_TRACE(test_case.name);
    const CpuRouteResult cpu =
        routing::RouteWithCpuAStar(test_case.board, test_case.compiled_board, test_case.request);
    for (PlanarGenerator generator :
         {PlanarGenerator::kBucketedFrontier, PlanarGenerator::kHeadingAwareSweep}) {
      const PlanarRoutePolicy policy{.generator = generator};
      const PlanarGpuRouteResult first =
          Route(test_case.board, test_case.compiled_board, test_case.request, policy);
      const PlanarGpuRouteResult second =
          Route(test_case.board, test_case.compiled_board, test_case.request, policy);
      if (std::holds_alternative<CpuRoute>(cpu)) {
        ASSERT_TRUE(std::holds_alternative<PlanarGpuRoute>(first))
            << std::get<PlanarGpuFailure>(first).detail;
        ASSERT_TRUE(std::holds_alternative<PlanarGpuRoute>(second))
            << std::get<PlanarGpuFailure>(second).detail;
        const PlanarGpuRoute& first_route = std::get<PlanarGpuRoute>(first);
        const PlanarGpuRoute& second_route = std::get<PlanarGpuRoute>(second);
        EXPECT_EQ(first_route.total_cost, std::get<CpuRoute>(cpu).total_cost);
        EXPECT_EQ(first_route.total_cost, second_route.total_cost);
        EXPECT_EQ(first_route.lattice_path, second_route.lattice_path);
        EXPECT_EQ(first_route.segments, second_route.segments);
      } else {
        ASSERT_EQ(std::get<routing::RouteFailure>(cpu).code,
                  routing::RouteFailureCode::kDisconnected);
        ASSERT_TRUE(std::holds_alternative<PlanarGpuFailure>(first));
        ASSERT_TRUE(std::holds_alternative<PlanarGpuFailure>(second));
        EXPECT_EQ(std::get<PlanarGpuFailure>(first).code, PlanarGpuFailureCode::kDisconnected);
        EXPECT_EQ(std::get<PlanarGpuFailure>(second).code, PlanarGpuFailureCode::kDisconnected);
      }
    }
  }
}

}  // namespace
}  // namespace apgar::gpu
