#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <variant>
#include <vector>

#include "apgar/benchmark/planar_corpus.h"
#include "apgar/board_ir/board.h"
#include "apgar/candidates/gpu_candidate_adapter.h"
#include "apgar/geometry_compiler/compiled_board.h"
#include "apgar/gpu/cuda_backend.h"
#include "apgar/gpu/fault_injecting_backend.h"
#include "apgar/gpu/planar_router.h"
#include "apgar/routing/candidate_policy.h"
#include "apgar/routing/cpu_astar.h"
#include "tests/support/board_builder.h"
#include "tests/support/compiler_builder.h"
#include "tests/support/google_test.h"
#include "tests/support/routing_builder.h"

namespace apgar::gpu {
namespace {

using board_ir::BoardSnapshot;
using geometry_compiler::CompiledBoard;
using routing::CpuRoute;
using routing::CpuRouteRequest;
using routing::CpuRouteResult;
using routing::EdgeResourceKey;

using test_support::Compile;
using test_support::CompilePreparedNet;
using test_support::ReadFixture;
using test_support::RequestForNet;
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

[[nodiscard]] EdgeResourceKey FirstCanonicalResource(const DeviceCompiledBoardV1& device) {
  for (const DeviceNodeV1& node : device.nodes) {
    for (std::uint8_t direction = 0; direction < 4; ++direction) {
      if ((node.legal_edges & static_cast<std::uint8_t>(1U << direction)) != 0) {
        return EdgeResourceKey{
            .layer = node.layer,
            .lattice_x = node.lattice_x,
            .lattice_y = node.lattice_y,
            .direction = static_cast<geometry_compiler::Direction>(direction),
        };
      }
    }
  }
  ADD_FAILURE() << "Compiled device board contains no canonical legal resource";
  return {};
}

[[nodiscard]] std::vector<PlanarCandidateBatchQuery> PolicyQueries(
    const BoardSnapshot& board, const DeviceCompiledBoardV1& device) {
  const EdgeResourceKey resource = FirstCanonicalResource(device);
  CpuRouteRequest base = TwoTerminalRequest(board);

  CpuRouteRequest surcharged = base;
  surcharged.candidate_policy.deterministic_seed = 0x1234;
  surcharged.candidate_policy.candidate_ordinal = 1;
  surcharged.candidate_policy.orthogonal_step_surcharge = 7;
  surcharged.candidate_policy.diagonal_step_surcharge = 5;
  surcharged.candidate_policy.bend_surcharge = 11;

  CpuRouteRequest penalized = base;
  penalized.candidate_policy.deterministic_seed = 0x1234;
  penalized.candidate_policy.candidate_ordinal = 2;
  penalized.candidate_policy.resource_penalties.push_back(
      routing::ResourcePenalty{.resource = resource, .additional_cost = 97});

  CpuRouteRequest banned = base;
  banned.candidate_policy.deterministic_seed = 0x1234;
  banned.candidate_policy.candidate_ordinal = 3;
  banned.candidate_policy.banned_resources.push_back(resource);

  // Deliberately unsorted. The public batch contract returns query-id order.
  return {
      PlanarCandidateBatchQuery{.query_id = 40, .input_ordinal = 0, .request = base},
      PlanarCandidateBatchQuery{.query_id = 7, .input_ordinal = 1, .request = surcharged},
      PlanarCandidateBatchQuery{.query_id = 31, .input_ordinal = 2, .request = penalized},
      PlanarCandidateBatchQuery{.query_id = 19, .input_ordinal = 3, .request = banned},
  };
}

[[nodiscard]] const CpuRouteRequest& RequestForQuery(
    const std::vector<PlanarCandidateBatchQuery>& queries, std::uint64_t query_id) {
  const auto found = std::ranges::find(queries, query_id, &PlanarCandidateBatchQuery::query_id);
  EXPECT_NE(found, queries.end());
  if (found == queries.end()) {
    return queries.front().request;
  }
  return found->request;
}

void ExpectCpuDifferential(const BoardSnapshot& board, const CompiledBoard& compiled,
                           const std::vector<PlanarCandidateBatchQuery>& queries,
                           const PlanarCandidateBatch& batch) {
  ASSERT_EQ(batch.items.size(), queries.size());
  EXPECT_TRUE(std::ranges::is_sorted(batch.items, {}, &PlanarCandidateBatchItem::query_id));
  EXPECT_GT(batch.prepared_node_lookup_host_bytes, 0U);
  EXPECT_GT(batch.telemetry.persistent_device_bytes, 0U);
  EXPECT_GT(batch.telemetry.batch_device_bytes, 0U);
  EXPECT_GE(batch.telemetry.workspace_capacity_device_bytes, batch.telemetry.batch_device_bytes);
  EXPECT_GT(batch.telemetry.batch_host_bytes, 0U);
  EXPECT_GT(batch.telemetry.device_to_host_readback_bytes, 0U);
  EXPECT_EQ(batch.telemetry.peak_device_bytes, batch.telemetry.persistent_device_bytes +
                                                   batch.telemetry.workspace_capacity_device_bytes);
  EXPECT_GE(batch.telemetry.kernel_milliseconds, 0.0);
  EXPECT_GE(batch.telemetry.kernel_launch_count, 2U);
  EXPECT_GT(batch.telemetry.blocking_status_readback_count, 0U);
  EXPECT_GT(batch.telemetry.dispatched_rounds, 0U);
  EXPECT_LE(batch.telemetry.finalization_launch_count, 1U);
  EXPECT_EQ(batch.telemetry.chunk_rounds, batch.generator == PlanarGenerator::kBucketedFrontier
                                              ? kCandidateFrontierChunkRounds
                                              : kCandidateSweepChunkRounds);
  for (const PlanarCandidateBatchItem& item : batch.items) {
    const CpuRouteResult cpu =
        routing::RouteWithCpuAStar(board, compiled, RequestForQuery(queries, item.query_id()));
    if (std::holds_alternative<CpuRoute>(cpu)) {
      ASSERT_TRUE(std::holds_alternative<PlanarGpuRoute>(item.result()))
          << std::get<PlanarGpuFailure>(item.result()).detail;
      const CpuRoute& cpu_route = std::get<CpuRoute>(cpu);
      const PlanarGpuRoute& gpu_route = std::get<PlanarGpuRoute>(item.result());
      EXPECT_EQ(gpu_route.total_cost, cpu_route.total_cost);
      EXPECT_EQ(gpu_route.policy_identity, cpu_route.candidate_policy_identity);
      EXPECT_EQ(item.policy_identity(), cpu_route.candidate_policy_identity);
      EXPECT_EQ(gpu_route.backend.backend, "cuda");
      EXPECT_EQ(gpu_route.telemetry.persistent_device_bytes, 0U);
      EXPECT_EQ(gpu_route.telemetry.batch_device_bytes, 0U);
      EXPECT_EQ(gpu_route.telemetry.peak_device_bytes, 0U);
      EXPECT_EQ(gpu_route.telemetry.kernel_milliseconds, 0.0);
    } else {
      ASSERT_EQ(std::get<routing::RouteFailure>(cpu).code,
                routing::RouteFailureCode::kDisconnected);
      ASSERT_TRUE(std::holds_alternative<PlanarGpuFailure>(item.result()));
      const PlanarGpuFailure& failure = std::get<PlanarGpuFailure>(item.result());
      EXPECT_EQ(failure.code, PlanarGpuFailureCode::kDisconnected);
      ASSERT_TRUE(failure.telemetry.has_value());
      EXPECT_EQ(failure.telemetry->persistent_device_bytes, 0U);
      EXPECT_EQ(failure.telemetry->batch_device_bytes, 0U);
      EXPECT_EQ(failure.telemetry->peak_device_bytes, 0U);
      EXPECT_EQ(failure.telemetry->kernel_milliseconds, 0.0);
    }
  }
}

void ExpectRepeatableBatch(const PlanarCandidateBatch& first, const PlanarCandidateBatch& second) {
  EXPECT_EQ(first.prepared_node_lookup_host_bytes, second.prepared_node_lookup_host_bytes);
  EXPECT_EQ(first.telemetry.persistent_device_bytes, second.telemetry.persistent_device_bytes);
  EXPECT_EQ(first.telemetry.batch_device_bytes, second.telemetry.batch_device_bytes);
  EXPECT_EQ(first.telemetry.workspace_capacity_device_bytes,
            second.telemetry.workspace_capacity_device_bytes);
  EXPECT_EQ(first.telemetry.peak_device_bytes, second.telemetry.peak_device_bytes);
  EXPECT_EQ(first.telemetry.batch_host_bytes, second.telemetry.batch_host_bytes);
  EXPECT_EQ(first.telemetry.device_to_host_readback_bytes,
            second.telemetry.device_to_host_readback_bytes);
  EXPECT_EQ(first.telemetry.kernel_launch_count, second.telemetry.kernel_launch_count);
  EXPECT_EQ(first.telemetry.blocking_status_readback_count,
            second.telemetry.blocking_status_readback_count);
  EXPECT_EQ(first.telemetry.chunk_rounds, second.telemetry.chunk_rounds);
  EXPECT_EQ(first.telemetry.dispatched_rounds, second.telemetry.dispatched_rounds);
  EXPECT_EQ(first.telemetry.finalization_launch_count, second.telemetry.finalization_launch_count);
  ASSERT_EQ(first.items.size(), second.items.size());
  for (std::size_t index = 0; index < first.items.size(); ++index) {
    const PlanarCandidateBatchItem& left = first.items[index];
    const PlanarCandidateBatchItem& right = second.items[index];
    EXPECT_EQ(left.query_id(), right.query_id());
    EXPECT_EQ(left.input_ordinal(), right.input_ordinal());
    EXPECT_EQ(left.policy_identity(), right.policy_identity());
    ASSERT_EQ(left.result().index(), right.result().index());
    if (std::holds_alternative<PlanarGpuRoute>(left.result())) {
      const PlanarGpuRoute& left_route = std::get<PlanarGpuRoute>(left.result());
      const PlanarGpuRoute& right_route = std::get<PlanarGpuRoute>(right.result());
      EXPECT_EQ(left_route.total_cost, right_route.total_cost);
      EXPECT_EQ(left_route.lattice_path, right_route.lattice_path);
      EXPECT_EQ(left_route.segments, right_route.segments);
      EXPECT_EQ(left_route.policy_identity, right_route.policy_identity);
      EXPECT_EQ(left_route.telemetry.examined_work, right_route.telemetry.examined_work);
      EXPECT_EQ(left_route.telemetry.heading_turn_relaxations,
                right_route.telemetry.heading_turn_relaxations);
      EXPECT_EQ(left_route.telemetry.rounds, right_route.telemetry.rounds);
    } else {
      EXPECT_EQ(std::get<PlanarGpuFailure>(left.result()).code,
                std::get<PlanarGpuFailure>(right.result()).code);
    }
  }
}

enum class BatchReadbackFault : std::uint8_t {
  kStateOwner,
  kPredecessorOwner,
  kPredecessorIndex,
  kCrossQueryLabels,
  kExtraQuery,
  kBatchTelemetryMemory,
  kBatchTelemetryLaunches,
  kQueryTelemetryRounds,
  kQueryHeaderRounds,
  kQueryHeaderCompletion,
  kCompactPathOwner,
  kCompactPathState,
};

class CorruptingBatchBackend final : public IPlanarRouteBackend {
 public:
  CorruptingBatchBackend(IPlanarRouteBackend& inner, BatchReadbackFault fault)
      : inner_(inner), fault_(fault) {}

  [[nodiscard]] BackendMetadataResult QueryMetadata() const override {
    return inner_.QueryMetadata();
  }
  [[nodiscard]] UploadResult UploadCompiledView(const DeviceCompiledBoardV1& board) override {
    return inner_.UploadCompiledView(board);
  }
  [[nodiscard]] ExecutionResult ExecuteRoute(const UploadedCompiledView& board,
                                             const BackendExecutionRequest& request) override {
    return inner_.ExecuteRoute(board, request);
  }
  [[nodiscard]] ReadbackResult ReadbackRoute(const PendingRouteExecution& execution) override {
    return inner_.ReadbackRoute(execution);
  }
  [[nodiscard]] CandidateBatchExecutionResult ExecuteCandidateBatch(
      const UploadedCompiledView& board,
      const BackendCandidateBatchExecutionRequest& request) override {
    return inner_.ExecuteCandidateBatch(board, request);
  }
  [[nodiscard]] CandidateBatchReadbackResult ReadbackCandidateBatch(
      const PendingCandidateBatchExecution& execution) override {
    CandidateBatchReadbackResult result = inner_.ReadbackCandidateBatch(execution);
    if (!std::holds_alternative<UntrustedCandidateBatchResult>(result)) {
      return result;
    }
    UntrustedCandidateBatchResult& batch = std::get<UntrustedCandidateBatchResult>(result);
    if (batch.queries.size() < 2) {
      return result;
    }
    switch (fault_) {
      case BatchReadbackFault::kStateOwner: {
        const std::uint64_t offset = batch.queries[1].workspace_offset;
        if (offset < batch.state_owners.size()) {
          batch.state_owners[static_cast<std::size_t>(offset)] =
              batch.queries.front().header.workspace_owner;
        }
        break;
      }
      case BatchReadbackFault::kPredecessorOwner: {
        const std::uint64_t offset = batch.queries[1].workspace_offset;
        if (offset < batch.predecessor_owners.size()) {
          batch.predecessor_owners[static_cast<std::size_t>(offset)] =
              batch.queries.front().header.workspace_owner;
        }
        break;
      }
      case BatchReadbackFault::kPredecessorIndex: {
        const std::uint32_t goal = batch.queries[1].header.goal_state;
        const std::uint64_t count = batch.queries[1].workspace_state_count;
        const std::uint64_t offset = batch.queries[1].workspace_offset;
        if (offset <= batch.predecessors.size() && goal < count &&
            goal < batch.predecessors.size() - offset) {
          batch.predecessors[static_cast<std::size_t>(offset + goal)] =
              static_cast<std::uint32_t>(count);
        }
        break;
      }
      case BatchReadbackFault::kCrossQueryLabels: {
        const std::uint64_t count = batch.queries.front().workspace_state_count;
        const std::uint64_t source = batch.queries.front().workspace_offset;
        const std::uint64_t destination = batch.queries[1].workspace_offset;
        if (batch.queries[1].workspace_state_count == count && source <= batch.labels.size() &&
            count <= batch.labels.size() - source && destination <= batch.labels.size() &&
            count <= batch.labels.size() - destination) {
          std::ranges::copy_n(batch.labels.begin() + static_cast<std::ptrdiff_t>(source),
                              static_cast<std::ptrdiff_t>(count),
                              batch.labels.begin() + static_cast<std::ptrdiff_t>(destination));
        }
        break;
      }
      case BatchReadbackFault::kExtraQuery: {
        UntrustedCandidateBatchQueryResult extra = batch.queries.back();
        extra.header.query_id = std::numeric_limits<std::uint64_t>::max();
        extra.header.workspace_owner = std::numeric_limits<std::uint64_t>::max();
        batch.queries.push_back(std::move(extra));
        break;
      }
      case BatchReadbackFault::kBatchTelemetryMemory:
        ++batch.telemetry.batch_device_bytes;
        break;
      case BatchReadbackFault::kBatchTelemetryLaunches:
        ++batch.telemetry.kernel_launch_count;
        break;
      case BatchReadbackFault::kQueryTelemetryRounds:
        ++batch.queries[1].telemetry.rounds;
        break;
      case BatchReadbackFault::kQueryHeaderRounds:
        batch.queries[1].header.rounds = batch.telemetry.dispatched_rounds + 1;
        batch.queries[1].telemetry.rounds = batch.queries[1].header.rounds;
        break;
      case BatchReadbackFault::kQueryHeaderCompletion:
        batch.queries[1].header.completion = KernelCompletion::kBudgetExhausted;
        break;
      case BatchReadbackFault::kCompactPathOwner:
        if (batch.queries[1].compact_path.has_value()) {
          batch.queries[1].compact_path->workspace_owner =
              batch.queries.front().header.workspace_owner;
        }
        break;
      case BatchReadbackFault::kCompactPathState:
        if (batch.queries[1].compact_path.has_value() &&
            batch.queries[1].compact_path->state_count != 0 &&
            batch.queries[1].compact_path->state_offset < batch.compact_path_states.size()) {
          batch.compact_path_states[static_cast<std::size_t>(
              batch.queries[1].compact_path->state_offset)] = kInvalidStateIndex;
        }
        break;
    }
    return result;
  }

 private:
  IPlanarRouteBackend& inner_;
  BatchReadbackFault fault_;
};

TEST(CudaCandidateBatchTest, FrontierAndSweepMatchCpuPoliciesAndRepeatExactly) {
  const BoardSnapshot board = Snapshot();
  geometry_compiler::CompilerProfile profile = test_support::DefaultCompilerProfile({0});
  profile.costs = geometry_compiler::DeterministicCosts{
      .orthogonal_step = 17,
      .diagonal_step = 29,
      .bend = 23,
  };
  const CompiledBoard compiled = Compile(board, profile);
  const DeviceCompiledBoardV1 device = Flatten(board, compiled);
  const std::vector<PlanarCandidateBatchQuery> queries = PolicyQueries(board, device);

  for (PlanarGenerator generator :
       {PlanarGenerator::kBucketedFrontier, PlanarGenerator::kHeadingAwareSweep}) {
    SCOPED_TRACE(generator == PlanarGenerator::kBucketedFrontier ? "frontier" : "sweep");
    std::unique_ptr<IPlanarRouteBackend> backend = CreateCudaPlanarRouteBackend();
    ASSERT_NE(backend, nullptr);
    PreparedPlanarCompiledViewResult prepared_result =
        PreparePlanarCompiledView(board, compiled, *backend);
    ASSERT_TRUE(
        std::holds_alternative<std::unique_ptr<PreparedPlanarCompiledView>>(prepared_result));
    std::unique_ptr<PreparedPlanarCompiledView> prepared =
        std::get<std::unique_ptr<PreparedPlanarCompiledView>>(std::move(prepared_result));
    ASSERT_NE(prepared, nullptr);
    const PlanarCandidateBatchPolicy policy{
        .batch_id = 0xabc,
        .generator = generator,
    };
    const PlanarCandidateBatchResult first = RouteCandidateBatchWithPreparedPlanarGpuBackend(
        board, compiled, queries, policy, *prepared);
    const PlanarCandidateBatchResult second = RouteCandidateBatchWithPreparedPlanarGpuBackend(
        board, compiled, queries, policy, *prepared);
    ASSERT_TRUE(std::holds_alternative<PlanarCandidateBatch>(first))
        << std::get<PlanarGpuFailure>(first).detail;
    ASSERT_TRUE(std::holds_alternative<PlanarCandidateBatch>(second))
        << std::get<PlanarGpuFailure>(second).detail;
    const PlanarCandidateBatch& first_batch = std::get<PlanarCandidateBatch>(first);
    const PlanarCandidateBatch& second_batch = std::get<PlanarCandidateBatch>(second);
    ExpectCpuDifferential(board, compiled, queries, first_batch);
    ExpectRepeatableBatch(first_batch, second_batch);
  }
}

TEST(CudaCandidateBatchTest, SweepWorkspacePersistsCapacityAndShrinksForATighterBound) {
  const BoardSnapshot board = Snapshot();
  const CompiledBoard compiled = Compile(board, test_support::DefaultCompilerProfile({0}));
  const DeviceCompiledBoardV1 device = Flatten(board, compiled);
  const std::vector<PlanarCandidateBatchQuery> queries = PolicyQueries(board, device);
  std::unique_ptr<IPlanarRouteBackend> backend = CreateCudaPlanarRouteBackend();
  ASSERT_NE(backend, nullptr);
  PreparedPlanarCompiledViewResult prepared_result =
      PreparePlanarCompiledView(board, compiled, *backend);
  ASSERT_TRUE(std::holds_alternative<std::unique_ptr<PreparedPlanarCompiledView>>(prepared_result));
  std::unique_ptr<PreparedPlanarCompiledView> prepared =
      std::get<std::unique_ptr<PreparedPlanarCompiledView>>(std::move(prepared_result));

  const PlanarCandidateBatchResult large = RouteCandidateBatchWithPreparedPlanarGpuBackend(
      board, compiled, queries,
      PlanarCandidateBatchPolicy{.batch_id = 0x601,
                                 .generator = PlanarGenerator::kHeadingAwareSweep},
      *prepared);
  ASSERT_TRUE(std::holds_alternative<PlanarCandidateBatch>(large));
  const CandidateBatchTelemetry large_telemetry = std::get<PlanarCandidateBatch>(large).telemetry;
  EXPECT_EQ(large_telemetry.workspace_capacity_device_bytes, large_telemetry.batch_device_bytes);

  const std::span<const PlanarCandidateBatchQuery> small_queries = std::span(queries).first(2);
  const PlanarCandidateBatchResult reused = RouteCandidateBatchWithPreparedPlanarGpuBackend(
      board, compiled, small_queries,
      PlanarCandidateBatchPolicy{.batch_id = 0x602,
                                 .generator = PlanarGenerator::kHeadingAwareSweep},
      *prepared);
  ASSERT_TRUE(std::holds_alternative<PlanarCandidateBatch>(reused));
  const CandidateBatchTelemetry reused_telemetry = std::get<PlanarCandidateBatch>(reused).telemetry;
  EXPECT_EQ(reused_telemetry.workspace_capacity_device_bytes,
            large_telemetry.workspace_capacity_device_bytes);
  EXPECT_GT(reused_telemetry.workspace_capacity_device_bytes, reused_telemetry.batch_device_bytes);

  const PlanarCandidateBatchResult bounded = RouteCandidateBatchWithPreparedPlanarGpuBackend(
      board, compiled, small_queries,
      PlanarCandidateBatchPolicy{
          .batch_id = 0x603,
          .generator = PlanarGenerator::kHeadingAwareSweep,
          .maximum_device_bytes =
              device.header.estimated_persistent_device_bytes + reused_telemetry.batch_device_bytes,
      },
      *prepared);
  ASSERT_TRUE(std::holds_alternative<PlanarCandidateBatch>(bounded));
  const CandidateBatchTelemetry bounded_telemetry =
      std::get<PlanarCandidateBatch>(bounded).telemetry;
  EXPECT_EQ(bounded_telemetry.workspace_capacity_device_bytes,
            bounded_telemetry.batch_device_bytes);
  EXPECT_EQ(bounded_telemetry.peak_device_bytes,
            device.header.estimated_persistent_device_bytes + bounded_telemetry.batch_device_bytes);
}

TEST(CudaCandidateBatchTest, CompactHostBudgetAcceptsExactEstimateAndRejectsOneByteBelow) {
  const BoardSnapshot board = Snapshot();
  const CompiledBoard compiled = Compile(board, test_support::DefaultCompilerProfile({0}));
  const DeviceCompiledBoardV1 device = Flatten(board, compiled);
  const std::vector<PlanarCandidateBatchQuery> all_queries = PolicyQueries(board, device);
  const std::span<const PlanarCandidateBatchQuery> queries = std::span(all_queries).first(2);
  std::unique_ptr<IPlanarRouteBackend> backend = CreateCudaPlanarRouteBackend();
  ASSERT_NE(backend, nullptr);
  PreparedPlanarCompiledViewResult prepared_result =
      PreparePlanarCompiledView(board, compiled, *backend);
  ASSERT_TRUE(std::holds_alternative<std::unique_ptr<PreparedPlanarCompiledView>>(prepared_result));
  std::unique_ptr<PreparedPlanarCompiledView> prepared =
      std::get<std::unique_ptr<PreparedPlanarCompiledView>>(std::move(prepared_result));

  const PlanarCandidateBatchResult reference = RouteCandidateBatchWithPreparedPlanarGpuBackend(
      board, compiled, queries,
      PlanarCandidateBatchPolicy{.batch_id = 0x606,
                                 .generator = PlanarGenerator::kHeadingAwareSweep},
      *prepared);
  ASSERT_TRUE(std::holds_alternative<PlanarCandidateBatch>(reference));
  const std::uint64_t exact_bytes =
      std::get<PlanarCandidateBatch>(reference).telemetry.batch_host_bytes;
  ASSERT_GT(exact_bytes, 0U);

  const PlanarCandidateBatchResult exact = RouteCandidateBatchWithPreparedPlanarGpuBackend(
      board, compiled, queries,
      PlanarCandidateBatchPolicy{.batch_id = 0x607,
                                 .generator = PlanarGenerator::kHeadingAwareSweep,
                                 .maximum_host_bytes = exact_bytes},
      *prepared);
  ASSERT_TRUE(std::holds_alternative<PlanarCandidateBatch>(exact));
  for (const PlanarCandidateBatchItem& item : std::get<PlanarCandidateBatch>(exact).items) {
    EXPECT_TRUE(std::holds_alternative<PlanarGpuRoute>(item.result()));
  }

  const PlanarCandidateBatchResult below = RouteCandidateBatchWithPreparedPlanarGpuBackend(
      board, compiled, queries,
      PlanarCandidateBatchPolicy{.batch_id = 0x608,
                                 .generator = PlanarGenerator::kHeadingAwareSweep,
                                 .maximum_host_bytes = exact_bytes - 1U},
      *prepared);
  ASSERT_TRUE(std::holds_alternative<PlanarCandidateBatch>(below));
  for (const PlanarCandidateBatchItem& item : std::get<PlanarCandidateBatch>(below).items) {
    ASSERT_TRUE(std::holds_alternative<PlanarGpuFailure>(item.result()));
    EXPECT_EQ(std::get<PlanarGpuFailure>(item.result()).code,
              PlanarGpuFailureCode::kResourceExhausted);
  }
}

TEST(CudaCandidateBatchTest, BoundedFrontierEvictsAnIdleSweepWorkspace) {
  const BoardSnapshot board = Snapshot();
  const CompiledBoard compiled = Compile(board, test_support::DefaultCompilerProfile({0}));
  const DeviceCompiledBoardV1 device = Flatten(board, compiled);
  const std::vector<PlanarCandidateBatchQuery> queries = PolicyQueries(board, device);
  const std::span<const PlanarCandidateBatchQuery> small_queries = std::span(queries).first(2);
  std::unique_ptr<IPlanarRouteBackend> backend = CreateCudaPlanarRouteBackend();
  ASSERT_NE(backend, nullptr);

  PreparedPlanarCompiledViewResult reference_result =
      PreparePlanarCompiledView(board, compiled, *backend);
  ASSERT_TRUE(
      std::holds_alternative<std::unique_ptr<PreparedPlanarCompiledView>>(reference_result));
  std::unique_ptr<PreparedPlanarCompiledView> reference =
      std::get<std::unique_ptr<PreparedPlanarCompiledView>>(std::move(reference_result));
  const PlanarCandidateBatchResult reference_frontier =
      RouteCandidateBatchWithPreparedPlanarGpuBackend(
          board, compiled, small_queries,
          PlanarCandidateBatchPolicy{.batch_id = 0x611,
                                     .generator = PlanarGenerator::kBucketedFrontier},
          *reference);
  ASSERT_TRUE(std::holds_alternative<PlanarCandidateBatch>(reference_frontier));
  const CandidateBatchTelemetry reference_telemetry =
      std::get<PlanarCandidateBatch>(reference_frontier).telemetry;
  const CpuRouteRequest legacy_request = TwoTerminalRequest(board);
  const PlanarGpuRouteResult reference_legacy = RouteWithPreparedPlanarGpuBackend(
      board, compiled, legacy_request, PlanarRoutePolicy{}, *reference);
  ASSERT_TRUE(std::holds_alternative<PlanarGpuRoute>(reference_legacy));
  const KernelTelemetry reference_legacy_telemetry =
      std::get<PlanarGpuRoute>(reference_legacy).telemetry;

  PreparedPlanarCompiledViewResult prepared_result =
      PreparePlanarCompiledView(board, compiled, *backend);
  ASSERT_TRUE(std::holds_alternative<std::unique_ptr<PreparedPlanarCompiledView>>(prepared_result));
  std::unique_ptr<PreparedPlanarCompiledView> prepared =
      std::get<std::unique_ptr<PreparedPlanarCompiledView>>(std::move(prepared_result));
  const PlanarCandidateBatchResult large_sweep = RouteCandidateBatchWithPreparedPlanarGpuBackend(
      board, compiled, queries,
      PlanarCandidateBatchPolicy{.batch_id = 0x612,
                                 .generator = PlanarGenerator::kHeadingAwareSweep},
      *prepared);
  ASSERT_TRUE(std::holds_alternative<PlanarCandidateBatch>(large_sweep));
  const CandidateBatchTelemetry large_sweep_telemetry =
      std::get<PlanarCandidateBatch>(large_sweep).telemetry;

  const PlanarCandidateBatchResult bounded_frontier =
      RouteCandidateBatchWithPreparedPlanarGpuBackend(
          board, compiled, small_queries,
          PlanarCandidateBatchPolicy{
              .batch_id = 0x613,
              .generator = PlanarGenerator::kBucketedFrontier,
              .maximum_device_bytes = device.header.estimated_persistent_device_bytes +
                                      reference_telemetry.batch_device_bytes,
          },
          *prepared);
  ASSERT_TRUE(std::holds_alternative<PlanarCandidateBatch>(bounded_frontier));
  const CandidateBatchTelemetry bounded_frontier_telemetry =
      std::get<PlanarCandidateBatch>(bounded_frontier).telemetry;
  EXPECT_EQ(bounded_frontier_telemetry.batch_device_bytes, reference_telemetry.batch_device_bytes);
  EXPECT_EQ(bounded_frontier_telemetry.peak_device_bytes,
            device.header.estimated_persistent_device_bytes +
                bounded_frontier_telemetry.batch_device_bytes);

  const PlanarCandidateBatchResult small_sweep = RouteCandidateBatchWithPreparedPlanarGpuBackend(
      board, compiled, small_queries,
      PlanarCandidateBatchPolicy{.batch_id = 0x614,
                                 .generator = PlanarGenerator::kHeadingAwareSweep},
      *prepared);
  ASSERT_TRUE(std::holds_alternative<PlanarCandidateBatch>(small_sweep));
  const CandidateBatchTelemetry small_sweep_telemetry =
      std::get<PlanarCandidateBatch>(small_sweep).telemetry;
  EXPECT_LT(small_sweep_telemetry.workspace_capacity_device_bytes,
            large_sweep_telemetry.workspace_capacity_device_bytes);
  EXPECT_EQ(small_sweep_telemetry.workspace_capacity_device_bytes,
            small_sweep_telemetry.batch_device_bytes);

  const PlanarGpuRouteResult bounded_legacy = RouteWithPreparedPlanarGpuBackend(
      board, compiled, legacy_request,
      PlanarRoutePolicy{.maximum_device_bytes = reference_legacy_telemetry.peak_device_bytes},
      *prepared);
  ASSERT_TRUE(std::holds_alternative<PlanarGpuRoute>(bounded_legacy));
  const KernelTelemetry& bounded_legacy_telemetry =
      std::get<PlanarGpuRoute>(bounded_legacy).telemetry;
  EXPECT_EQ(bounded_legacy_telemetry.persistent_device_bytes,
            reference_legacy_telemetry.persistent_device_bytes);
  EXPECT_EQ(bounded_legacy_telemetry.batch_device_bytes,
            reference_legacy_telemetry.batch_device_bytes);
  EXPECT_EQ(bounded_legacy_telemetry.peak_device_bytes,
            reference_legacy_telemetry.peak_device_bytes);
}

TEST(CudaCandidateBatchTest, SingleQueryApiDelegatesNonDefaultPolicyToBatchOfOne) {
  const BoardSnapshot board = Snapshot();
  const CompiledBoard compiled = Compile(board, test_support::DefaultCompilerProfile({0}));
  CpuRouteRequest request = TwoTerminalRequest(board);
  request.candidate_policy.deterministic_seed = 77;
  request.candidate_policy.candidate_ordinal = 4;
  request.candidate_policy.orthogonal_step_surcharge = 9;
  request.candidate_policy.diagonal_step_surcharge = 12;
  request.candidate_policy.bend_surcharge = 15;
  const CpuRouteResult cpu = routing::RouteWithCpuAStar(board, compiled, request);
  ASSERT_TRUE(std::holds_alternative<CpuRoute>(cpu));
  for (PlanarGenerator generator :
       {PlanarGenerator::kBucketedFrontier, PlanarGenerator::kHeadingAwareSweep}) {
    std::unique_ptr<IPlanarRouteBackend> backend = CreateCudaPlanarRouteBackend();
    ASSERT_NE(backend, nullptr);
    const PlanarGpuRouteResult gpu = RouteWithPlanarGpuBackend(
        board, compiled, request, PlanarRoutePolicy{.generator = generator}, *backend);
    ASSERT_TRUE(std::holds_alternative<PlanarGpuRoute>(gpu))
        << std::get<PlanarGpuFailure>(gpu).detail;
    EXPECT_EQ(std::get<PlanarGpuRoute>(gpu).total_cost, std::get<CpuRoute>(cpu).total_cost);
    EXPECT_EQ(std::get<PlanarGpuRoute>(gpu).policy_identity,
              std::get<CpuRoute>(cpu).candidate_policy_identity);
    const KernelTelemetry& telemetry = std::get<PlanarGpuRoute>(gpu).telemetry;
    EXPECT_GT(telemetry.persistent_device_bytes, 0U);
    EXPECT_GT(telemetry.batch_device_bytes, 0U);
    EXPECT_EQ(telemetry.peak_device_bytes,
              telemetry.persistent_device_bytes + telemetry.batch_device_bytes);
    EXPECT_GE(telemetry.kernel_milliseconds, 0.0);
  }
}

TEST(CudaCandidateBatchTest, SealedBatchItemAuthenticatesCudaCandidateProvenance) {
  const BoardSnapshot board = Snapshot(test_support::MultiNetM1BoardData());
  const CompiledBoard compiled = CompilePreparedNet(board, board.data().nets[1].ref,
                                                    test_support::DefaultCompilerProfile({0}));
  CpuRouteRequest request = RequestForNet(board, board.data().nets[1].ref);
  request.candidate_policy.deterministic_seed = 0x7345;
  request.candidate_policy.candidate_ordinal = 7;
  const routing::CandidatePolicyResult normalized_result =
      routing::NormalizeCandidateGenerationPolicy(compiled, request.candidate_policy);
  ASSERT_TRUE(
      std::holds_alternative<routing::NormalizedCandidateGenerationPolicy>(normalized_result));
  const routing::NormalizedCandidateGenerationPolicy& normalized =
      std::get<routing::NormalizedCandidateGenerationPolicy>(normalized_result);

  std::unique_ptr<IPlanarRouteBackend> backend = CreateCudaPlanarRouteBackend();
  ASSERT_NE(backend, nullptr);
  PreparedPlanarCompiledViewResult prepared_result =
      PrepareCudaPlanarCompiledView(board, compiled, *backend);
  ASSERT_TRUE(std::holds_alternative<std::unique_ptr<PreparedPlanarCompiledView>>(prepared_result));
  std::unique_ptr<PreparedPlanarCompiledView> prepared =
      std::get<std::unique_ptr<PreparedPlanarCompiledView>>(std::move(prepared_result));
  ASSERT_NE(prepared, nullptr);

  const PlanarCandidateBatchQuery query{
      .query_id = 0x73450001,
      .input_ordinal = 19,
      .request = request,
  };
  const PlanarCandidateBatchResult result = RouteCandidateBatchWithPreparedPlanarGpuBackend(
      board, compiled, std::span(&query, 1),
      PlanarCandidateBatchPolicy{
          .batch_id = 0x73450002,
          .generator = PlanarGenerator::kHeadingAwareSweep,
      },
      *prepared);
  ASSERT_TRUE(std::holds_alternative<PlanarCandidateBatch>(result))
      << std::get<PlanarGpuFailure>(result).detail;
  const PlanarCandidateBatch& batch = std::get<PlanarCandidateBatch>(result);
  ASSERT_EQ(batch.items.size(), 1U);
  const PlanarCandidateBatchItem& item = batch.items.front();
  ASSERT_TRUE(std::holds_alternative<PlanarGpuRoute>(item.result()))
      << std::get<PlanarGpuFailure>(item.result()).detail;
  EXPECT_TRUE(item.has_validated_route_evidence());
  EXPECT_TRUE(item.has_authenticated_cuda_producer_evidence());
  EXPECT_EQ(item.validated_batch_schema_version(), kDeviceCandidateBatchSchemaVersion);
  EXPECT_EQ(item.validated_batch_id(), batch.batch_id);

  PlanarCandidateBatchItem copied_item = item;
  const std::uint64_t sealed_policy_identity = copied_item.policy_identity();
  EXPECT_FALSE(copied_item.SetUnsealedPolicyIdentity(sealed_policy_identity + 1));
  EXPECT_FALSE(copied_item.SetUnsealedResult(PlanarGpuFailure{
      .code = PlanarGpuFailureCode::kBackendFailure,
      .detail = "hostile post-validation replacement",
      .invariant_id = "test.hostile.sealed_mutation",
      .obstacle = std::nullopt,
      .telemetry = std::nullopt,
  }));
  ASSERT_TRUE(std::holds_alternative<PlanarGpuRoute>(copied_item.result()));
  EXPECT_EQ(copied_item.policy_identity(), sealed_policy_identity);
  EXPECT_EQ(std::get<PlanarGpuRoute>(copied_item.result()).total_cost,
            std::get<PlanarGpuRoute>(item.result()).total_cost);

  const candidates::CandidateDraftBuildResult built =
      candidates::BuildGeneratedCandidateFromGpuBatchItem(board, compiled, query, normalized, batch,
                                                          copied_item);
  ASSERT_TRUE(std::holds_alternative<candidates::GeneratedRouteCandidate>(built));
  const candidates::GeneratedRouteCandidate& candidate =
      std::get<candidates::GeneratedRouteCandidate>(built);
  EXPECT_EQ(candidate.associations.routing_profile_fingerprint,
            routing::FingerprintRoutingProfile(compiled.prepared_routing_profile().profile()));
  EXPECT_NE(candidate.associations.routing_profile_fingerprint,
            routing::FingerprintRoutingProfile(board.data().routing_profile));
  const candidates::CandidateProvenance& provenance = candidate.provenance;
  EXPECT_EQ(provenance.generator, candidates::CandidateGeneratorKind::kCudaSweep);
  EXPECT_EQ(provenance.backend, candidates::CandidateBackendKind::kCuda);
  EXPECT_EQ(provenance.batch_identity, batch.batch_id);
  EXPECT_EQ(provenance.query_identity, query.query_id);
  EXPECT_EQ(provenance.candidate_ordinal, request.candidate_policy.candidate_ordinal);

  const std::array bulk_requests = {candidates::GpuCandidateBatchBuildRequest{
      .query = std::cref(query),
      .normalized_policy = std::cref(normalized),
      .item = std::cref(copied_item),
  }};
  const candidates::GpuCandidateBatchBuildResult bulk =
      candidates::BuildGeneratedCandidatesFromGpuBatchItems(board, compiled, batch, bulk_requests);
  ASSERT_TRUE(std::holds_alternative<std::vector<candidates::CandidateDraftBuildResult>>(bulk));
  const std::vector<candidates::CandidateDraftBuildResult>& bulk_results =
      std::get<std::vector<candidates::CandidateDraftBuildResult>>(bulk);
  ASSERT_EQ(bulk_results.size(), 1U);
  ASSERT_TRUE(std::holds_alternative<candidates::GeneratedRouteCandidate>(bulk_results.front()));
  EXPECT_EQ(std::get<candidates::GeneratedRouteCandidate>(bulk_results.front()).payload_checksum,
            std::get<candidates::GeneratedRouteCandidate>(built).payload_checksum);

  PlanarCandidateBatchQuery swapped_query = query;
  ++swapped_query.query_id;
  const std::array ordered_requests = {
      candidates::GpuCandidateBatchBuildRequest{
          .query = std::cref(swapped_query),
          .normalized_policy = std::cref(normalized),
          .item = std::cref(copied_item),
      },
      candidates::GpuCandidateBatchBuildRequest{
          .query = std::cref(query),
          .normalized_policy = std::cref(normalized),
          .item = std::cref(copied_item),
      },
  };
  const candidates::GpuCandidateBatchBuildResult ordered =
      candidates::BuildGeneratedCandidatesFromGpuBatchItems(board, compiled, batch,
                                                            ordered_requests);
  ASSERT_TRUE(std::holds_alternative<std::vector<candidates::CandidateDraftBuildResult>>(ordered));
  const std::vector<candidates::CandidateDraftBuildResult>& ordered_results =
      std::get<std::vector<candidates::CandidateDraftBuildResult>>(ordered);
  ASSERT_EQ(ordered_results.size(), 2U);
  ASSERT_TRUE(std::holds_alternative<candidates::CandidateRejection>(ordered_results[0]));
  EXPECT_EQ(std::get<candidates::CandidateRejection>(ordered_results[0]).invariant_id,
            "candidate.builder.gpu_query_attribution.v1");
  ASSERT_TRUE(std::holds_alternative<candidates::GeneratedRouteCandidate>(ordered_results[1]));
  EXPECT_EQ(std::get<candidates::GeneratedRouteCandidate>(ordered_results[1]).payload_checksum,
            std::get<candidates::GeneratedRouteCandidate>(built).payload_checksum);

  PlanarCandidateBatch duplicate_batch = batch;
  duplicate_batch.items.push_back(copied_item);
  const candidates::GpuCandidateBatchBuildResult duplicate =
      candidates::BuildGeneratedCandidatesFromGpuBatchItems(board, compiled, duplicate_batch,
                                                            bulk_requests);
  ASSERT_TRUE(
      std::holds_alternative<std::vector<candidates::CandidateDraftBuildResult>>(duplicate));
  const std::vector<candidates::CandidateDraftBuildResult>& duplicate_results =
      std::get<std::vector<candidates::CandidateDraftBuildResult>>(duplicate);
  ASSERT_EQ(duplicate_results.size(), 1U);
  ASSERT_TRUE(std::holds_alternative<candidates::CandidateRejection>(duplicate_results.front()));
  EXPECT_EQ(std::get<candidates::CandidateRejection>(duplicate_results.front()).invariant_id,
            "candidate.builder.gpu_batch_item_membership.v1");

  PlanarCandidateBatch absent_batch = batch;
  absent_batch.items.clear();
  const candidates::GpuCandidateBatchBuildResult absent =
      candidates::BuildGeneratedCandidatesFromGpuBatchItems(board, compiled, absent_batch,
                                                            bulk_requests);
  ASSERT_TRUE(std::holds_alternative<std::vector<candidates::CandidateDraftBuildResult>>(absent));
  const std::vector<candidates::CandidateDraftBuildResult>& absent_results =
      std::get<std::vector<candidates::CandidateDraftBuildResult>>(absent);
  ASSERT_EQ(absent_results.size(), 1U);
  ASSERT_TRUE(std::holds_alternative<candidates::CandidateRejection>(absent_results.front()));
  EXPECT_EQ(std::get<candidates::CandidateRejection>(absent_results.front()).invariant_id,
            "candidate.builder.gpu_batch_item_membership.v1");

  const candidates::CandidateDraftBuildResult swapped =
      candidates::BuildGeneratedCandidateFromGpuBatchItem(board, compiled, swapped_query,
                                                          normalized, batch, item);
  ASSERT_TRUE(std::holds_alternative<candidates::CandidateRejection>(swapped));
  EXPECT_EQ(std::get<candidates::CandidateRejection>(swapped).invariant_id,
            "candidate.builder.gpu_query_attribution.v1");

  routing::NormalizedCandidateGenerationPolicy oversized_policy = normalized;
  oversized_policy.policy.banned_resources.resize(routing::kMaximumPolicyResourceEntries + 1U);
  const candidates::CandidateDraftBuildResult oversized =
      candidates::BuildGeneratedCandidateFromGpuBatchItem(board, compiled, query, oversized_policy,
                                                          batch, item);
  ASSERT_TRUE(std::holds_alternative<candidates::CandidateRejection>(oversized));
  const candidates::CandidateRejection& oversized_rejection =
      std::get<candidates::CandidateRejection>(oversized);
  EXPECT_EQ(oversized_rejection.invariant_id, "candidate.policy.resource_entry_count.v1");
  EXPECT_EQ(oversized_rejection.expected_value, routing::kMaximumPolicyResourceEntries);
  EXPECT_EQ(oversized_rejection.actual_value, routing::kMaximumPolicyResourceEntries + 1U);
  EXPECT_FALSE(oversized_rejection.candidate_payload_checksum.has_value());
}

TEST(CudaCandidateBatchTest, GenericCudaLookingWrapperCannotMintCudaCandidateProvenance) {
  const BoardSnapshot board = Snapshot();
  const CompiledBoard compiled = Compile(board, test_support::DefaultCompilerProfile({0}));
  CpuRouteRequest request = TwoTerminalRequest(board);
  request.candidate_policy.deterministic_seed = 0x5a17;
  request.candidate_policy.candidate_ordinal = 3;
  const routing::CandidatePolicyResult normalized_result =
      routing::NormalizeCandidateGenerationPolicy(compiled, request.candidate_policy);
  ASSERT_TRUE(
      std::holds_alternative<routing::NormalizedCandidateGenerationPolicy>(normalized_result));
  const routing::NormalizedCandidateGenerationPolicy& normalized =
      std::get<routing::NormalizedCandidateGenerationPolicy>(normalized_result);

  std::unique_ptr<IPlanarRouteBackend> cuda_backend = CreateCudaPlanarRouteBackend();
  ASSERT_NE(cuda_backend, nullptr);
  std::unique_ptr<IPlanarRouteBackend> wrapper = CreateFaultInjectingCandidateBatchBackend(
      *cuda_backend, UntrustedCandidateBatchResultFault::kNone);
  ASSERT_NE(wrapper, nullptr);

  const PreparedPlanarCompiledViewResult authenticated_attempt =
      PrepareCudaPlanarCompiledView(board, compiled, *wrapper);
  ASSERT_TRUE(std::holds_alternative<PlanarGpuFailure>(authenticated_attempt));
  const PlanarGpuFailure& authentication_failure =
      std::get<PlanarGpuFailure>(authenticated_attempt);
  EXPECT_EQ(authentication_failure.code, PlanarGpuFailureCode::kUnsupported);
  EXPECT_EQ(authentication_failure.invariant_id, "gpu.producer.authentication.v1");

  PreparedPlanarCompiledViewResult prepared_result =
      PreparePlanarCompiledView(board, compiled, *wrapper);
  ASSERT_TRUE(std::holds_alternative<std::unique_ptr<PreparedPlanarCompiledView>>(prepared_result));
  std::unique_ptr<PreparedPlanarCompiledView> prepared =
      std::get<std::unique_ptr<PreparedPlanarCompiledView>>(std::move(prepared_result));
  ASSERT_NE(prepared, nullptr);
  EXPECT_FALSE(prepared->has_authenticated_cuda_producer());

  const PlanarCandidateBatchQuery query{
      .query_id = 0x5a170001,
      .input_ordinal = 4,
      .request = request,
  };
  const PlanarCandidateBatchResult result = RouteCandidateBatchWithPreparedPlanarGpuBackend(
      board, compiled, std::span(&query, 1),
      PlanarCandidateBatchPolicy{
          .batch_id = 0x5a170002,
          .generator = PlanarGenerator::kHeadingAwareSweep,
      },
      *prepared);
  ASSERT_TRUE(std::holds_alternative<PlanarCandidateBatch>(result))
      << std::get<PlanarGpuFailure>(result).detail;
  const PlanarCandidateBatch& batch = std::get<PlanarCandidateBatch>(result);
  ASSERT_EQ(batch.items.size(), 1U);
  const PlanarCandidateBatchItem& item = batch.items.front();
  ASSERT_TRUE(std::holds_alternative<PlanarGpuRoute>(item.result()))
      << std::get<PlanarGpuFailure>(item.result()).detail;
  EXPECT_TRUE(item.has_validated_route_evidence());
  EXPECT_FALSE(item.has_authenticated_cuda_producer_evidence());

  const candidates::CandidateDraftBuildResult built =
      candidates::BuildGeneratedCandidateFromGpuBatchItem(board, compiled, query, normalized, batch,
                                                          item);
  ASSERT_TRUE(std::holds_alternative<candidates::CandidateRejection>(built));
  const candidates::CandidateRejection& rejection = std::get<candidates::CandidateRejection>(built);
  EXPECT_EQ(rejection.code, candidates::CandidateRejectionCode::kUnsupported);
  EXPECT_EQ(rejection.invariant_id, "candidate.builder.gpu_producer_authentication.v1");
}

TEST(CudaCandidateBatchTest, IsolatesIncompatibleCancellationAndResourceOutcomes) {
  const BoardSnapshot board = Snapshot();
  const CompiledBoard compiled = Compile(board, test_support::DefaultCompilerProfile());
  std::unique_ptr<IPlanarRouteBackend> backend = CreateCudaPlanarRouteBackend();
  ASSERT_NE(backend, nullptr);
  PreparedPlanarCompiledViewResult prepared_result =
      PreparePlanarCompiledView(board, compiled, *backend);
  ASSERT_TRUE(std::holds_alternative<std::unique_ptr<PreparedPlanarCompiledView>>(prepared_result));
  std::unique_ptr<PreparedPlanarCompiledView> prepared =
      std::get<std::unique_ptr<PreparedPlanarCompiledView>>(std::move(prepared_result));
  ASSERT_NE(prepared, nullptr);

  CpuRouteRequest incompatible = TwoTerminalRequest(board);
  incompatible.goal_layer = 31;
  incompatible.candidate_policy.deterministic_seed = 0x55;
  incompatible.candidate_policy.candidate_ordinal = 1;
  const std::vector<PlanarCandidateBatchQuery> mixed{
      PlanarCandidateBatchQuery{
          .query_id = 9, .input_ordinal = 0, .request = TwoTerminalRequest(board)},
      PlanarCandidateBatchQuery{.query_id = 10, .input_ordinal = 1, .request = incompatible},
  };
  PlanarCandidateBatchResult mixed_result = RouteCandidateBatchWithPreparedPlanarGpuBackend(
      board, compiled, mixed,
      PlanarCandidateBatchPolicy{.batch_id = 1, .generator = PlanarGenerator::kBucketedFrontier},
      *prepared);
  ASSERT_TRUE(std::holds_alternative<PlanarCandidateBatch>(mixed_result));
  const PlanarCandidateBatch& mixed_batch = std::get<PlanarCandidateBatch>(mixed_result);
  ASSERT_EQ(mixed_batch.items.size(), 2U);
  EXPECT_TRUE(std::holds_alternative<PlanarGpuRoute>(mixed_batch.items[0].result()));
  ASSERT_TRUE(std::holds_alternative<PlanarGpuFailure>(mixed_batch.items[1].result()));
  EXPECT_EQ(std::get<PlanarGpuFailure>(mixed_batch.items[1].result()).code,
            PlanarGpuFailureCode::kUnsupported);
  const routing::CandidatePolicyResult normalized_incompatible =
      routing::NormalizeCandidateGenerationPolicy(compiled, incompatible.candidate_policy);
  ASSERT_TRUE(std::holds_alternative<routing::NormalizedCandidateGenerationPolicy>(
      normalized_incompatible));
  EXPECT_EQ(
      mixed_batch.items[1].policy_identity(),
      std::get<routing::NormalizedCandidateGenerationPolicy>(normalized_incompatible).identity);

  std::atomic_bool cancellation = true;
  PlanarCandidateBatchResult cancelled = RouteCandidateBatchWithPreparedPlanarGpuBackend(
      board, compiled, std::span(mixed).first(1),
      PlanarCandidateBatchPolicy{.batch_id = 2,
                                 .generator = PlanarGenerator::kBucketedFrontier,
                                 .cancellation = &cancellation},
      *prepared);
  ASSERT_TRUE(std::holds_alternative<PlanarCandidateBatch>(cancelled));
  ASSERT_TRUE(std::holds_alternative<PlanarGpuFailure>(
      std::get<PlanarCandidateBatch>(cancelled).items.front().result()));
  EXPECT_EQ(
      std::get<PlanarGpuFailure>(std::get<PlanarCandidateBatch>(cancelled).items.front().result())
          .code,
      PlanarGpuFailureCode::kCancelled);

  PlanarCandidateBatchResult workspace = RouteCandidateBatchWithPreparedPlanarGpuBackend(
      board, compiled, std::span(mixed).first(1),
      PlanarCandidateBatchPolicy{.batch_id = 3,
                                 .generator = PlanarGenerator::kHeadingAwareSweep,
                                 .maximum_workspace_states_per_query = 1},
      *prepared);
  ASSERT_TRUE(std::holds_alternative<PlanarCandidateBatch>(workspace));
  ASSERT_TRUE(std::holds_alternative<PlanarGpuFailure>(
      std::get<PlanarCandidateBatch>(workspace).items.front().result()));
  EXPECT_EQ(
      std::get<PlanarGpuFailure>(std::get<PlanarCandidateBatch>(workspace).items.front().result())
          .code,
      PlanarGpuFailureCode::kResourceExhausted);

  PlanarCandidateBatchResult rounds = RouteCandidateBatchWithPreparedPlanarGpuBackend(
      board, compiled, std::span(mixed).first(1),
      PlanarCandidateBatchPolicy{
          .batch_id = 4, .generator = PlanarGenerator::kBucketedFrontier, .maximum_rounds = 1},
      *prepared);
  ASSERT_TRUE(std::holds_alternative<PlanarCandidateBatch>(rounds));
  const PlanarCandidateBatch& rounds_batch = std::get<PlanarCandidateBatch>(rounds);
  ASSERT_TRUE(std::holds_alternative<PlanarGpuFailure>(rounds_batch.items.front().result()));
  EXPECT_EQ(std::get<PlanarGpuFailure>(rounds_batch.items.front().result()).code,
            PlanarGpuFailureCode::kResourceExhausted);
  EXPECT_EQ(rounds_batch.telemetry.chunk_rounds, kCandidateFrontierChunkRounds);
  EXPECT_EQ(rounds_batch.telemetry.blocking_status_readback_count, 1U);
  EXPECT_EQ(rounds_batch.telemetry.kernel_launch_count, 4U);
  EXPECT_EQ(rounds_batch.telemetry.dispatched_rounds, 1U);
  EXPECT_EQ(rounds_batch.telemetry.finalization_launch_count, 1U);
}

TEST(CudaCandidateBatchTest, RejectsOneForeignWorkspaceOwnerWithoutContaminatingPeers) {
  const BoardSnapshot board = Snapshot();
  const CompiledBoard compiled = Compile(board, test_support::DefaultCompilerProfile({0}));
  const DeviceCompiledBoardV1 device = Flatten(board, compiled);
  std::vector<PlanarCandidateBatchQuery> queries = PolicyQueries(board, device);
  queries.resize(2);
  for (BatchReadbackFault fault :
       {BatchReadbackFault::kStateOwner, BatchReadbackFault::kPredecessorOwner}) {
    std::unique_ptr<IPlanarRouteBackend> cuda = CreateCudaPlanarRouteBackend();
    ASSERT_NE(cuda, nullptr);
    CorruptingBatchBackend corrupting(*cuda, fault);
    PlanarCandidateBatchResult result = RouteCandidateBatchWithPlanarGpuBackend(
        board, compiled, queries,
        PlanarCandidateBatchPolicy{.batch_id = 0x55,
                                   .generator = PlanarGenerator::kBucketedFrontier},
        corrupting);
    ASSERT_TRUE(std::holds_alternative<PlanarCandidateBatch>(result));
    const PlanarCandidateBatch& batch = std::get<PlanarCandidateBatch>(result);
    ASSERT_EQ(batch.items.size(), 2U);
    EXPECT_TRUE(std::holds_alternative<PlanarGpuRoute>(batch.items[0].result()));
    ASSERT_TRUE(std::holds_alternative<PlanarGpuFailure>(batch.items[1].result()));
    const PlanarGpuFailure& failure = std::get<PlanarGpuFailure>(batch.items[1].result());
    EXPECT_EQ(failure.code, PlanarGpuFailureCode::kInternalInvariant);
    EXPECT_EQ(failure.invariant_id, "gpu.batch.workspace.owner.v1");
  }
}

TEST(CudaCandidateBatchTest, RejectsForgedPredecessorAndForeignExtraResult) {
  const BoardSnapshot board = Snapshot();
  const CompiledBoard compiled = Compile(board, test_support::DefaultCompilerProfile({0}));
  const DeviceCompiledBoardV1 device = Flatten(board, compiled);
  std::vector<PlanarCandidateBatchQuery> queries = PolicyQueries(board, device);
  queries.resize(2);

  {
    std::unique_ptr<IPlanarRouteBackend> cuda = CreateCudaPlanarRouteBackend();
    ASSERT_NE(cuda, nullptr);
    CorruptingBatchBackend corrupting(*cuda, BatchReadbackFault::kPredecessorIndex);
    PlanarCandidateBatchResult result = RouteCandidateBatchWithPlanarGpuBackend(
        board, compiled, queries,
        PlanarCandidateBatchPolicy{.batch_id = 0x56,
                                   .generator = PlanarGenerator::kBucketedFrontier},
        corrupting);
    ASSERT_TRUE(std::holds_alternative<PlanarCandidateBatch>(result));
    const PlanarCandidateBatch& batch = std::get<PlanarCandidateBatch>(result);
    EXPECT_TRUE(std::holds_alternative<PlanarGpuRoute>(batch.items[0].result()));
    ASSERT_TRUE(std::holds_alternative<PlanarGpuFailure>(batch.items[1].result()));
    EXPECT_EQ(std::get<PlanarGpuFailure>(batch.items[1].result()).code,
              PlanarGpuFailureCode::kInternalInvariant);
  }

  {
    std::unique_ptr<IPlanarRouteBackend> cuda = CreateCudaPlanarRouteBackend();
    ASSERT_NE(cuda, nullptr);
    CorruptingBatchBackend corrupting(*cuda, BatchReadbackFault::kExtraQuery);
    PlanarCandidateBatchResult result = RouteCandidateBatchWithPlanarGpuBackend(
        board, compiled, queries,
        PlanarCandidateBatchPolicy{.batch_id = 0x57,
                                   .generator = PlanarGenerator::kHeadingAwareSweep},
        corrupting);
    ASSERT_TRUE(std::holds_alternative<PlanarCandidateBatch>(result));
    const PlanarCandidateBatch& batch = std::get<PlanarCandidateBatch>(result);
    for (const PlanarCandidateBatchItem& item : batch.items) {
      ASSERT_TRUE(std::holds_alternative<PlanarGpuFailure>(item.result()));
      const PlanarGpuFailure& failure = std::get<PlanarGpuFailure>(item.result());
      EXPECT_EQ(failure.code, PlanarGpuFailureCode::kInternalInvariant);
      EXPECT_EQ(failure.invariant_id, "gpu.batch.query.identity.v1");
    }
  }
}

TEST(CudaCandidateBatchTest, RejectsCrossQueryLabelContaminationWithoutRejectingPeer) {
  const BoardSnapshot board = Snapshot();
  const CompiledBoard compiled = Compile(board, test_support::DefaultCompilerProfile({0}));
  const DeviceCompiledBoardV1 device = Flatten(board, compiled);
  std::vector<PlanarCandidateBatchQuery> queries = PolicyQueries(board, device);
  queries.resize(2);

  std::unique_ptr<IPlanarRouteBackend> cuda = CreateCudaPlanarRouteBackend();
  ASSERT_NE(cuda, nullptr);
  CorruptingBatchBackend corrupting(*cuda, BatchReadbackFault::kCrossQueryLabels);
  PlanarCandidateBatchResult result = RouteCandidateBatchWithPlanarGpuBackend(
      board, compiled, queries,
      PlanarCandidateBatchPolicy{.batch_id = 0x58, .generator = PlanarGenerator::kBucketedFrontier},
      corrupting);
  ASSERT_TRUE(std::holds_alternative<PlanarCandidateBatch>(result));
  const PlanarCandidateBatch& batch = std::get<PlanarCandidateBatch>(result);
  ASSERT_EQ(batch.items.size(), 2U);
  EXPECT_TRUE(std::holds_alternative<PlanarGpuRoute>(batch.items[0].result()));
  ASSERT_TRUE(std::holds_alternative<PlanarGpuFailure>(batch.items[1].result()));
  EXPECT_EQ(std::get<PlanarGpuFailure>(batch.items[1].result()).code,
            PlanarGpuFailureCode::kInternalInvariant);
}

TEST(CudaCandidateBatchTest, RejectsCorruptedSharedTelemetryForTheWholeBatch) {
  const BoardSnapshot board = Snapshot();
  const CompiledBoard compiled = Compile(board, test_support::DefaultCompilerProfile({0}));
  const DeviceCompiledBoardV1 device = Flatten(board, compiled);
  std::vector<PlanarCandidateBatchQuery> queries = PolicyQueries(board, device);
  queries.resize(2);

  for (const auto [fault, expected_invariant] :
       {std::pair{BatchReadbackFault::kBatchTelemetryMemory, "gpu.batch.memory.accounting.v1"},
        std::pair{BatchReadbackFault::kBatchTelemetryLaunches, "gpu.batch.telemetry.v1"}}) {
    std::unique_ptr<IPlanarRouteBackend> cuda = CreateCudaPlanarRouteBackend();
    ASSERT_NE(cuda, nullptr);
    CorruptingBatchBackend corrupting(*cuda, fault);
    PlanarCandidateBatchResult result = RouteCandidateBatchWithPlanarGpuBackend(
        board, compiled, queries,
        PlanarCandidateBatchPolicy{.batch_id = 0x59,
                                   .generator = PlanarGenerator::kBucketedFrontier},
        corrupting);
    ASSERT_TRUE(std::holds_alternative<PlanarCandidateBatch>(result));
    const PlanarCandidateBatch& batch = std::get<PlanarCandidateBatch>(result);
    EXPECT_EQ(batch.telemetry, CandidateBatchTelemetry{});
    ASSERT_EQ(batch.items.size(), queries.size());
    for (const PlanarCandidateBatchItem& item : batch.items) {
      ASSERT_TRUE(std::holds_alternative<PlanarGpuFailure>(item.result()));
      const PlanarGpuFailure& failure = std::get<PlanarGpuFailure>(item.result());
      EXPECT_EQ(failure.code, PlanarGpuFailureCode::kInternalInvariant);
      EXPECT_EQ(failure.invariant_id, expected_invariant);
    }
  }
}

TEST(CudaCandidateBatchTest, RejectsCorruptedQueryTelemetryWithoutRejectingPeer) {
  const BoardSnapshot board = Snapshot();
  const CompiledBoard compiled = Compile(board, test_support::DefaultCompilerProfile({0}));
  const DeviceCompiledBoardV1 device = Flatten(board, compiled);
  std::vector<PlanarCandidateBatchQuery> queries = PolicyQueries(board, device);
  queries.resize(2);

  for (BatchReadbackFault fault :
       {BatchReadbackFault::kQueryTelemetryRounds, BatchReadbackFault::kQueryHeaderRounds,
        BatchReadbackFault::kQueryHeaderCompletion}) {
    std::unique_ptr<IPlanarRouteBackend> cuda = CreateCudaPlanarRouteBackend();
    ASSERT_NE(cuda, nullptr);
    CorruptingBatchBackend corrupting(*cuda, fault);
    PlanarCandidateBatchResult result = RouteCandidateBatchWithPlanarGpuBackend(
        board, compiled, queries,
        PlanarCandidateBatchPolicy{.batch_id = 0x5a,
                                   .generator = PlanarGenerator::kBucketedFrontier},
        corrupting);
    ASSERT_TRUE(std::holds_alternative<PlanarCandidateBatch>(result));
    const PlanarCandidateBatch& batch = std::get<PlanarCandidateBatch>(result);
    ASSERT_EQ(batch.items.size(), 2U);
    EXPECT_TRUE(std::holds_alternative<PlanarGpuRoute>(batch.items[0].result()));
    ASSERT_TRUE(std::holds_alternative<PlanarGpuFailure>(batch.items[1].result()));
    const PlanarGpuFailure& failure = std::get<PlanarGpuFailure>(batch.items[1].result());
    EXPECT_EQ(failure.code, PlanarGpuFailureCode::kInternalInvariant);
    EXPECT_EQ(failure.invariant_id, "gpu.batch.query.telemetry.v1");
  }
}

TEST(CudaCandidateBatchTest, RejectsCorruptedCompactSweepPathWithoutRejectingPeer) {
  const BoardSnapshot board = Snapshot();
  const CompiledBoard compiled = Compile(board, test_support::DefaultCompilerProfile({0}));
  const DeviceCompiledBoardV1 device = Flatten(board, compiled);
  std::vector<PlanarCandidateBatchQuery> queries = PolicyQueries(board, device);
  queries.resize(2);

  for (const auto [fault, expected_invariant] :
       {std::pair{BatchReadbackFault::kCompactPathOwner, "gpu.batch.compact_path.bounds.v1"},
        std::pair{BatchReadbackFault::kCompactPathState, "gpu.batch.compact_path.endpoint.v1"}}) {
    std::unique_ptr<IPlanarRouteBackend> cuda = CreateCudaPlanarRouteBackend();
    ASSERT_NE(cuda, nullptr);
    CorruptingBatchBackend corrupting(*cuda, fault);
    PlanarCandidateBatchResult result = RouteCandidateBatchWithPlanarGpuBackend(
        board, compiled, queries,
        PlanarCandidateBatchPolicy{.batch_id = 0x5b,
                                   .generator = PlanarGenerator::kHeadingAwareSweep},
        corrupting);
    ASSERT_TRUE(std::holds_alternative<PlanarCandidateBatch>(result));
    const PlanarCandidateBatch& batch = std::get<PlanarCandidateBatch>(result);
    ASSERT_EQ(batch.items.size(), 2U);
    EXPECT_TRUE(std::holds_alternative<PlanarGpuRoute>(batch.items[0].result()));
    ASSERT_TRUE(std::holds_alternative<PlanarGpuFailure>(batch.items[1].result()));
    const PlanarGpuFailure& failure = std::get<PlanarGpuFailure>(batch.items[1].result());
    EXPECT_EQ(failure.code, PlanarGpuFailureCode::kInternalInvariant);
    EXPECT_EQ(failure.invariant_id, expected_invariant);
  }
}

TEST(CudaCandidateBatchTest, RejectsDuplicateOrZeroQueryIdentityBeforeExecution) {
  const BoardSnapshot board = Snapshot();
  const CompiledBoard compiled = Compile(board, test_support::DefaultCompilerProfile({0}));
  std::unique_ptr<IPlanarRouteBackend> backend = CreateCudaPlanarRouteBackend();
  ASSERT_NE(backend, nullptr);
  std::vector<PlanarCandidateBatchQuery> duplicate{
      PlanarCandidateBatchQuery{
          .query_id = 3, .input_ordinal = 0, .request = TwoTerminalRequest(board)},
      PlanarCandidateBatchQuery{
          .query_id = 3, .input_ordinal = 1, .request = TwoTerminalRequest(board)},
  };
  PlanarCandidateBatchResult result = RouteCandidateBatchWithPlanarGpuBackend(
      board, compiled, duplicate,
      PlanarCandidateBatchPolicy{.batch_id = 9, .generator = PlanarGenerator::kHeadingAwareSweep},
      *backend);
  ASSERT_TRUE(std::holds_alternative<PlanarGpuFailure>(result));
  EXPECT_EQ(std::get<PlanarGpuFailure>(result).code, PlanarGpuFailureCode::kInvalidInput);
}

TEST(CudaCandidateBatchTest, InputOrdinalsAreIndependentOfNonzeroBasePolicySchedule) {
  const BoardSnapshot board = Snapshot();
  const CompiledBoard compiled = Compile(board, test_support::DefaultCompilerProfile({0}));
  const DeviceCompiledBoardV1 device = Flatten(board, compiled);
  routing::CandidateGenerationPolicy base_policy;
  base_policy.deterministic_seed = 0xabcdef;
  base_policy.candidate_ordinal = 11;
  const routing::CandidatePolicyBatchResult schedule =
      routing::BuildDeterministicAlternativePolicies(
          compiled, base_policy,
          routing::DeterministicAlternativePolicySchedule{
              .candidate_count = 5,
              .step_surcharge_increment = 2,
              .bend_surcharge_increment = 3,
              .resource_penalty_increment = 5,
              .alternative_resources = {FirstCanonicalResource(device)},
          });
  ASSERT_TRUE(
      std::holds_alternative<std::vector<routing::NormalizedCandidateGenerationPolicy>>(schedule));
  const std::vector<routing::NormalizedCandidateGenerationPolicy>& policies =
      std::get<std::vector<routing::NormalizedCandidateGenerationPolicy>>(schedule);
  std::vector<PlanarCandidateBatchQuery> queries;
  queries.reserve(policies.size());
  for (std::size_t index = 0; index < policies.size(); ++index) {
    CpuRouteRequest request = TwoTerminalRequest(board);
    request.candidate_policy = policies[index].policy;
    queries.push_back(PlanarCandidateBatchQuery{
        .query_id = 100U - index,
        .input_ordinal = static_cast<std::uint32_t>(index),
        .request = std::move(request),
    });
  }

  std::unique_ptr<IPlanarRouteBackend> backend = CreateCudaPlanarRouteBackend();
  ASSERT_NE(backend, nullptr);
  PreparedPlanarCompiledViewResult prepared_result =
      PreparePlanarCompiledView(board, compiled, *backend);
  ASSERT_TRUE(std::holds_alternative<std::unique_ptr<PreparedPlanarCompiledView>>(prepared_result));
  std::unique_ptr<PreparedPlanarCompiledView> prepared =
      std::get<std::unique_ptr<PreparedPlanarCompiledView>>(std::move(prepared_result));
  ASSERT_NE(prepared, nullptr);

  std::uint64_t batch_id = 0x9100;
  for (PlanarGenerator generator :
       {PlanarGenerator::kBucketedFrontier, PlanarGenerator::kHeadingAwareSweep}) {
    const PlanarCandidateBatchResult result = RouteCandidateBatchWithPreparedPlanarGpuBackend(
        board, compiled, queries,
        PlanarCandidateBatchPolicy{.batch_id = batch_id++, .generator = generator}, *prepared);
    ASSERT_TRUE(std::holds_alternative<PlanarCandidateBatch>(result))
        << std::get<PlanarGpuFailure>(result).detail;
    const PlanarCandidateBatch& batch = std::get<PlanarCandidateBatch>(result);
    ExpectCpuDifferential(board, compiled, queries, batch);
    for (const PlanarCandidateBatchItem& item : batch.items) {
      const CpuRouteRequest& request = RequestForQuery(queries, item.query_id());
      EXPECT_NE(item.input_ordinal(), request.candidate_policy.candidate_ordinal);
      const routing::CandidatePolicyResult normalized =
          routing::NormalizeCandidateGenerationPolicy(compiled, request.candidate_policy);
      ASSERT_TRUE(std::holds_alternative<routing::NormalizedCandidateGenerationPolicy>(normalized));
      EXPECT_EQ(item.policy_identity(),
                std::get<routing::NormalizedCandidateGenerationPolicy>(normalized).identity);
    }
  }
}

TEST(CudaCandidateBatchDifferentialTest, ForcedGeneratorsMatchEveryPhase3CorpusCase) {
  benchmark::PlanarCorpusResult corpus_result =
      benchmark::BuildPhase3CandidateCorpus(ReadFixture());
  ASSERT_TRUE(std::holds_alternative<std::vector<benchmark::PlanarCorpusCase>>(corpus_result))
      << (std::holds_alternative<std::string>(corpus_result) ? std::get<std::string>(corpus_result)
                                                             : "");
  const std::vector<benchmark::PlanarCorpusCase>& corpus =
      std::get<std::vector<benchmark::PlanarCorpusCase>>(corpus_result);
  ASSERT_GE(corpus.size(), 11U);

  std::uint64_t batch_id = 100;
  for (const benchmark::PlanarCorpusCase& test_case : corpus) {
    SCOPED_TRACE(test_case.name);
    CpuRouteRequest varied = test_case.request;
    varied.candidate_policy.deterministic_seed = 0x9999;
    varied.candidate_policy.candidate_ordinal = 1;
    varied.candidate_policy.orthogonal_step_surcharge = 2;
    varied.candidate_policy.diagonal_step_surcharge = 3;
    varied.candidate_policy.bend_surcharge = 5;
    const std::vector<PlanarCandidateBatchQuery> queries{
        PlanarCandidateBatchQuery{.query_id = 2, .input_ordinal = 0, .request = test_case.request},
        PlanarCandidateBatchQuery{.query_id = 1, .input_ordinal = 1, .request = varied},
    };
    std::unique_ptr<IPlanarRouteBackend> backend = CreateCudaPlanarRouteBackend();
    ASSERT_NE(backend, nullptr);
    PreparedPlanarCompiledViewResult prepared_result =
        PreparePlanarCompiledView(test_case.board, test_case.compiled_board, *backend);
    ASSERT_TRUE(
        std::holds_alternative<std::unique_ptr<PreparedPlanarCompiledView>>(prepared_result));
    std::unique_ptr<PreparedPlanarCompiledView> prepared =
        std::get<std::unique_ptr<PreparedPlanarCompiledView>>(std::move(prepared_result));
    ASSERT_NE(prepared, nullptr);
    for (PlanarGenerator generator :
         {PlanarGenerator::kBucketedFrontier, PlanarGenerator::kHeadingAwareSweep}) {
      const PlanarCandidateBatchResult result = RouteCandidateBatchWithPreparedPlanarGpuBackend(
          test_case.board, test_case.compiled_board, queries,
          PlanarCandidateBatchPolicy{.batch_id = batch_id++, .generator = generator}, *prepared);
      ASSERT_TRUE(std::holds_alternative<PlanarCandidateBatch>(result))
          << std::get<PlanarGpuFailure>(result).detail;
      ExpectCpuDifferential(test_case.board, test_case.compiled_board, queries,
                            std::get<PlanarCandidateBatch>(result));
    }
  }
}

TEST(CudaCandidateBatchDifferentialTest,
     BanAndPenaltyRerouteBottleneckAcrossCandidatePoolSizesRepeatExactly) {
  benchmark::PlanarCorpusResult corpus_result =
      benchmark::BuildPhase3CandidateCorpus(ReadFixture());
  ASSERT_TRUE(std::holds_alternative<std::vector<benchmark::PlanarCorpusCase>>(corpus_result));
  const std::vector<benchmark::PlanarCorpusCase>& corpus =
      std::get<std::vector<benchmark::PlanarCorpusCase>>(corpus_result);
  const auto found =
      std::ranges::find(corpus, "ban-penalty-alternatives", &benchmark::PlanarCorpusCase::family);
  ASSERT_NE(found, corpus.end());
  const benchmark::PlanarCorpusCase& test_case = *found;
  const CpuRouteResult base_result =
      routing::RouteWithCpuAStar(test_case.board, test_case.compiled_board, test_case.request);
  ASSERT_TRUE(std::holds_alternative<CpuRoute>(base_result));
  const CpuRoute& base = std::get<CpuRoute>(base_result);
  ASSERT_GE(base.lattice_path.size(), 4U);
  const std::size_t edge_position = base.lattice_path.size() / 2U - 1U;
  const std::optional<geometry_compiler::LatticeIndex> source =
      geometry_compiler::ExactPointToLatticeIndex(test_case.compiled_board.profile(),
                                                  base.lattice_path[edge_position]);
  const std::optional<geometry_compiler::LatticeIndex> target =
      geometry_compiler::ExactPointToLatticeIndex(test_case.compiled_board.profile(),
                                                  base.lattice_path[edge_position + 1U]);
  ASSERT_TRUE(source.has_value());
  ASSERT_TRUE(target.has_value());
  const std::optional<geometry_compiler::Direction> direction =
      routing::DirectionBetween(*source, *target);
  ASSERT_TRUE(direction.has_value());
  const std::optional<EdgeResourceKey> resource =
      routing::CanonicalPhysicalEdgeResource(0, *source, *direction);
  ASSERT_TRUE(resource.has_value());

  CpuRouteRequest banned = test_case.request;
  banned.candidate_policy.deterministic_seed = 0xabc;
  banned.candidate_policy.candidate_ordinal = 1;
  banned.candidate_policy.banned_resources.push_back(*resource);
  CpuRouteRequest penalized = test_case.request;
  penalized.candidate_policy.deterministic_seed = 0xabc;
  penalized.candidate_policy.candidate_ordinal = 2;
  penalized.candidate_policy.resource_penalties.push_back(
      routing::ResourcePenalty{.resource = *resource, .additional_cost = 1'000'000});
  for (const CpuRouteRequest* request : {&banned, &penalized}) {
    const CpuRouteResult rerouted =
        routing::RouteWithCpuAStar(test_case.board, test_case.compiled_board, *request);
    ASSERT_TRUE(std::holds_alternative<CpuRoute>(rerouted));
    EXPECT_NE(std::get<CpuRoute>(rerouted).lattice_path, base.lattice_path);
  }

  std::unique_ptr<IPlanarRouteBackend> backend = CreateCudaPlanarRouteBackend();
  ASSERT_NE(backend, nullptr);
  PreparedPlanarCompiledViewResult prepared_result =
      PreparePlanarCompiledView(test_case.board, test_case.compiled_board, *backend);
  ASSERT_TRUE(std::holds_alternative<std::unique_ptr<PreparedPlanarCompiledView>>(prepared_result));
  std::unique_ptr<PreparedPlanarCompiledView> prepared =
      std::get<std::unique_ptr<PreparedPlanarCompiledView>>(std::move(prepared_result));
  ASSERT_NE(prepared, nullptr);

  std::uint64_t batch_id = 0xbeef;
  for (std::uint32_t candidate_count : {4U, 8U, 16U, 32U, 64U, 128U, 256U, 257U, 512U}) {
    SCOPED_TRACE(candidate_count);
    std::vector<PlanarCandidateBatchQuery> queries;
    queries.reserve(candidate_count);
    for (std::uint32_t ordinal = 0; ordinal < candidate_count; ++ordinal) {
      CpuRouteRequest request = ordinal % 2U == 0 ? banned : penalized;
      request.candidate_policy.deterministic_seed = 0x1000 + ordinal;
      request.candidate_policy.candidate_ordinal = ordinal;
      request.candidate_policy.orthogonal_step_surcharge += ordinal % 3U;
      request.candidate_policy.diagonal_step_surcharge += ordinal % 5U;
      request.candidate_policy.bend_surcharge += ordinal % 7U;
      queries.push_back(PlanarCandidateBatchQuery{
          .query_id = 1'000U + candidate_count - ordinal,
          .input_ordinal = ordinal,
          .request = std::move(request),
      });
    }
    for (PlanarGenerator generator :
         {PlanarGenerator::kBucketedFrontier, PlanarGenerator::kHeadingAwareSweep}) {
      SCOPED_TRACE(generator == PlanarGenerator::kBucketedFrontier ? "frontier" : "sweep");
      const PlanarCandidateBatchPolicy policy{
          .batch_id = batch_id++,
          .generator = generator,
      };
      const PlanarCandidateBatchResult first = RouteCandidateBatchWithPreparedPlanarGpuBackend(
          test_case.board, test_case.compiled_board, queries, policy, *prepared);
      const PlanarCandidateBatchResult second = RouteCandidateBatchWithPreparedPlanarGpuBackend(
          test_case.board, test_case.compiled_board, queries, policy, *prepared);
      ASSERT_TRUE(std::holds_alternative<PlanarCandidateBatch>(first))
          << std::get<PlanarGpuFailure>(first).detail;
      ASSERT_TRUE(std::holds_alternative<PlanarCandidateBatch>(second))
          << std::get<PlanarGpuFailure>(second).detail;
      const PlanarCandidateBatch& first_batch = std::get<PlanarCandidateBatch>(first);
      ExpectCpuDifferential(test_case.board, test_case.compiled_board, queries, first_batch);
      ExpectRepeatableBatch(first_batch, std::get<PlanarCandidateBatch>(second));
      for (const PlanarCandidateBatchItem& item : first_batch.items) {
        ASSERT_TRUE(std::holds_alternative<PlanarGpuRoute>(item.result()));
        EXPECT_NE(std::get<PlanarGpuRoute>(item.result()).lattice_path, base.lattice_path);
      }
    }
  }
}

}  // namespace
}  // namespace apgar::gpu
