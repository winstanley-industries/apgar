#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

#include "apgar/board_ir/board.h"
#include "apgar/geometry_compiler/compiled_board.h"
#include "apgar/gpu/planar_router.h"
#include "tests/support/board_builder.h"
#include "tests/support/compiler_builder.h"
#include "tests/support/google_test.h"
#include "tests/support/routing_builder.h"

namespace apgar::gpu {
namespace {

using board_ir::BoardData;
using board_ir::BoardSnapshot;
using geometry_compiler::CompiledBoard;
using geometry_compiler::CompilerProfile;
using geometry_compiler::Direction;

using routing::CpuRouteRequest;
using test_support::Compile;
using test_support::Snapshot;
using test_support::TwoTerminalRequest;

[[nodiscard]] DeviceCompiledBoardV1 Flatten(const BoardSnapshot& board,
                                            const CompiledBoard& compiled) {
  DeviceCompiledBoardResult result = BuildDeviceCompiledBoardV1(board, compiled);
  EXPECT_TRUE(std::holds_alternative<DeviceCompiledBoardV1>(result))
      << (std::holds_alternative<PlanarGpuFailure>(result)
              ? std::get<PlanarGpuFailure>(result).detail
              : "");
  return std::get<DeviceCompiledBoardV1>(std::move(result));
}

void SetBatchTelemetry(const DeviceCompiledBoardV1& device, PlanarGenerator generator,
                       UntrustedKernelResult* result) {
  result->generator = generator;
  std::uint64_t batch_bytes = sizeof(DeviceResultHeaderV1) +
                              result->labels.size() * sizeof(std::uint64_t) +
                              result->predecessors.size() * sizeof(std::uint32_t);
  if (generator == PlanarGenerator::kBucketedFrontier) {
    batch_bytes += result->labels.size() * 2 * sizeof(std::uint32_t) + 2 * sizeof(std::uint64_t);
  } else {
    batch_bytes +=
        device.header.represented_nodes * 8 * sizeof(std::uint64_t) + 2 * sizeof(std::uint64_t);
  }
  result->telemetry.persistent_device_bytes = device.header.estimated_persistent_device_bytes;
  result->telemetry.batch_device_bytes = batch_bytes;
  result->telemetry.peak_device_bytes =
      result->telemetry.persistent_device_bytes + result->telemetry.batch_device_bytes;
}

[[nodiscard]] UntrustedKernelResult StraightEastResult(const CompiledBoard& compiled,
                                                       DeviceCompiledBoardV1* device,
                                                       const CpuRouteRequest& request,
                                                       bool force_edges) {
  const std::optional<geometry_compiler::LatticeIndex> start =
      geometry_compiler::ExactPointToLatticeIndex(compiled.profile(), request.start);
  const std::optional<geometry_compiler::LatticeIndex> goal =
      geometry_compiler::ExactPointToLatticeIndex(compiled.profile(), request.goal);
  EXPECT_TRUE(start.has_value());
  EXPECT_TRUE(goal.has_value());
  EXPECT_EQ(start->y, goal->y);
  EXPECT_LT(start->x, goal->x);
  const std::optional<std::uint32_t> start_node =
      FindDeviceNodeIndex(*device, request.start_layer, *start);
  const std::optional<std::uint32_t> goal_node =
      FindDeviceNodeIndex(*device, request.goal_layer, *goal);
  EXPECT_TRUE(start_node.has_value());
  EXPECT_TRUE(goal_node.has_value());

  UntrustedKernelResult result{
      .source_board_content_hash = device->header.source_board_content_hash,
      .compiler_profile_fingerprint = device->header.compiler_profile_fingerprint,
      .compiler_version = device->header.compiler_version,
      .rule_bucket_identity = device->header.rule_bucket_identity,
      .device_view_fingerprint = device->header.device_view_fingerprint,
      .completion = KernelCompletion::kReached,
      .start_node = *start_node,
      .goal_node = *goal_node,
      .labels = std::vector<std::uint64_t>(device->header.represented_states, kInfiniteRouteCost),
      .predecessors =
          std::vector<std::uint32_t>(device->header.represented_states, kInvalidStateIndex),
      .telemetry = {},
  };
  SetBatchTelemetry(*device, PlanarGenerator::kBucketedFrontier, &result);

  std::uint32_t predecessor = StateIndex(*start_node, kNoIncomingHeading);
  result.labels[predecessor] = 0;
  std::uint64_t cost = 0;
  for (std::int64_t lattice_x = start->x + 1; lattice_x <= goal->x; ++lattice_x) {
    const std::optional<std::uint32_t> previous_node =
        FindDeviceNodeIndex(*device, request.start_layer,
                            geometry_compiler::LatticeIndex{.x = lattice_x - 1, .y = start->y});
    const std::optional<std::uint32_t> node =
        FindDeviceNodeIndex(*device, request.start_layer,
                            geometry_compiler::LatticeIndex{.x = lattice_x, .y = start->y});
    EXPECT_TRUE(previous_node.has_value());
    EXPECT_TRUE(node.has_value());
    if (force_edges) {
      device->nodes[*previous_node].neighbors[static_cast<std::size_t>(Direction::kEast)] = *node;
      device->nodes[*previous_node].legal_edges |= geometry_compiler::MaskFor(Direction::kEast);
      device->nodes[*node].neighbors[static_cast<std::size_t>(Direction::kWest)] = *previous_node;
      device->nodes[*node].legal_edges |= geometry_compiler::MaskFor(Direction::kWest);
    }
    const std::uint32_t state = StateIndex(*node, static_cast<std::uint8_t>(Direction::kEast));
    cost += compiled.profile().costs.orthogonal_step;
    result.labels[state] = cost;
    result.predecessors[state] = predecessor;
    predecessor = state;
  }
  result.goal_state = predecessor;
  return result;
}

[[nodiscard]] PlanarGpuRouteResult Validate(const BoardSnapshot& board,
                                            const CompiledBoard& compiled,
                                            const DeviceCompiledBoardV1& device,
                                            const CpuRouteRequest& request,
                                            const UntrustedKernelResult& result) {
  return ValidateAndReconstructGpuRoute(board, compiled, device, request,
                                        PlanarGenerator::kBucketedFrontier,
                                        BackendMetadata{
                                            .backend = "adversarial-test",
                                            .device_name = "untrusted",
                                            .device_uuid = "none",
                                            .compute_capability_major = 0,
                                            .compute_capability_minor = 0,
                                            .runtime_version = 0,
                                            .driver_version = 0,
                                            .global_memory_bytes = 0,
                                        },
                                        result);
}

void ExpectFailureCode(const PlanarGpuRouteResult& result, PlanarGpuFailureCode code) {
  ASSERT_TRUE(std::holds_alternative<PlanarGpuFailure>(result));
  EXPECT_EQ(std::get<PlanarGpuFailure>(result).code, code);
}

class StubUploadedView final : public UploadedCompiledView {};
class StubPendingExecution final : public PendingRouteExecution {};

class ScriptedBackend final : public IPlanarRouteBackend {
 public:
  explicit ScriptedBackend(UntrustedKernelResult result, bool fail_metadata = false)
      : result_(std::move(result)), fail_metadata_(fail_metadata) {}

  [[nodiscard]] BackendMetadataResult QueryMetadata() const override {
    ++metadata_queries;
    if (fail_metadata_) {
      return BackendError{.code = BackendErrorCode::kBackendFailure,
                          .detail = "deliberate backend metadata failure"};
    }
    return BackendMetadata{
        .backend = "scripted-test",
        .device_name = "none",
        .device_uuid = "none",
    };
  }

  [[nodiscard]] UploadResult UploadCompiledView(const DeviceCompiledBoardV1&) override {
    ++uploads;
    return std::unique_ptr<UploadedCompiledView>(std::make_unique<StubUploadedView>());
  }

  [[nodiscard]] ExecutionResult ExecuteRoute(const UploadedCompiledView&,
                                             const BackendExecutionRequest&) override {
    ++executions;
    return std::unique_ptr<PendingRouteExecution>(std::make_unique<StubPendingExecution>());
  }

  [[nodiscard]] ReadbackResult ReadbackRoute(const PendingRouteExecution&) override {
    ++readbacks;
    return result_;
  }

  mutable std::uint32_t metadata_queries = 0;
  std::uint32_t uploads = 0;
  std::uint32_t executions = 0;
  std::uint32_t readbacks = 0;

 private:
  UntrustedKernelResult result_;
  bool fail_metadata_;
};

TEST(DeviceCompiledBoardTest, FlatteningIsStableAndAccountsEveryOwnedByte) {
  const BoardSnapshot board = Snapshot();
  const CompiledBoard compiled = Compile(board, test_support::DefaultCompilerProfile());

  const DeviceCompiledBoardV1 first = Flatten(board, compiled);
  const DeviceCompiledBoardV1 second = Flatten(board, compiled);

  EXPECT_EQ(first, second);
  EXPECT_EQ(first.header.schema_version, kDeviceCompiledBoardSchemaVersion);
  EXPECT_EQ(first.header.source_board_content_hash, board.content_hash());
  EXPECT_EQ(first.header.compiler_profile_fingerprint, compiled.compiler_profile_fingerprint());
  EXPECT_EQ(first.header.rule_bucket_identity, compiled.rule_bucket().identity);
  EXPECT_EQ(first.header.represented_nodes, compiled.telemetry().represented_nodes);
  EXPECT_EQ(first.header.represented_states,
            compiled.telemetry().represented_nodes * kIncomingHeadingCount);
  EXPECT_NE(first.header.device_view_fingerprint, 0U);

  const std::uint64_t expected_bytes =
      sizeof(DeviceCompiledHeaderV1) + first.layers.size() * sizeof(DeviceLayerRangeV1) +
      first.nodes.size() * sizeof(DeviceNodeV1) + first.runs.size() * sizeof(DeviceRunV1) +
      first.run_nodes.size() * sizeof(std::uint32_t);
  EXPECT_EQ(first.header.estimated_persistent_device_bytes, expected_bytes);
}

TEST(DeviceCompiledBoardTest, StableIndicesPreserveNegativeAndCrossTileAdjacency) {
  const BoardSnapshot board = Snapshot();
  CompilerProfile profile = test_support::DefaultCompilerProfile({0});
  profile.tile_width_nodes = 3;
  profile.tile_height_nodes = 2;
  const CompiledBoard compiled = Compile(board, profile);
  const DeviceCompiledBoardV1 device = Flatten(board, compiled);

  const std::optional<std::uint32_t> negative =
      FindDeviceNodeIndex(device, 0, geometry_compiler::LatticeIndex{.x = -1, .y = -1});
  const std::optional<std::uint32_t> boundary =
      FindDeviceNodeIndex(device, 0, geometry_compiler::LatticeIndex{.x = 0, .y = -1});
  ASSERT_TRUE(negative.has_value());
  ASSERT_TRUE(boundary.has_value());
  EXPECT_EQ(device.nodes[*negative].neighbors[static_cast<std::size_t>(Direction::kEast)],
            *boundary);
  EXPECT_EQ(StateIndex(*negative, static_cast<std::uint8_t>(Direction::kEast)),
            *negative * kIncomingHeadingCount);
  EXPECT_EQ(NodeIndexForState(StateIndex(*boundary, kNoIncomingHeading)), *boundary);
  EXPECT_EQ(IncomingHeadingForState(StateIndex(*boundary, kNoIncomingHeading)), kNoIncomingHeading);
}

TEST(DeviceCompiledBoardTest, SegmentedRunsPartitionEveryLegalDirectedEdge) {
  const BoardSnapshot board = Snapshot();
  const CompiledBoard compiled = Compile(board, test_support::DefaultCompilerProfile({0}));
  const DeviceCompiledBoardV1 device = Flatten(board, compiled);
  std::set<std::tuple<std::uint32_t, std::uint32_t, std::uint8_t>> run_edges;

  for (const DeviceRunV1& run : device.runs) {
    ASSERT_GE(run.node_count, 2U);
    ASSERT_LE(static_cast<std::uint64_t>(run.node_offset) + run.node_count,
              device.run_nodes.size());
    const std::uint8_t direction = static_cast<std::uint8_t>(run.direction);
    for (std::uint32_t offset = 0; offset + 1 < run.node_count; ++offset) {
      const std::uint32_t source = device.run_nodes[run.node_offset + offset];
      const std::uint32_t target = device.run_nodes[run.node_offset + offset + 1];
      ASSERT_LT(source, device.nodes.size());
      ASSERT_LT(target, device.nodes.size());
      EXPECT_EQ(device.nodes[source].neighbors[direction], target);
      EXPECT_TRUE(run_edges.emplace(source, target, direction).second);
    }
  }
  EXPECT_EQ(run_edges.size(), compiled.telemetry().legal_directional_edges);
}

TEST(GpuUntrustedResultTest, RejectsAssociationsBoundsHeadingsCostsAndCycles) {
  BoardData data = test_support::ValidM1BoardData();
  data.obstacles.clear();
  const BoardSnapshot board = Snapshot(std::move(data));
  const CompiledBoard compiled = Compile(board, test_support::DefaultCompilerProfile({0}));
  DeviceCompiledBoardV1 device = Flatten(board, compiled);
  const CpuRouteRequest request = TwoTerminalRequest(board);
  const UntrustedKernelResult valid = StraightEastResult(compiled, &device, request, false);
  ASSERT_TRUE(
      std::holds_alternative<PlanarGpuRoute>(Validate(board, compiled, device, request, valid)));

  UntrustedKernelResult association = valid;
  ++association.compiler_profile_fingerprint;
  ExpectFailureCode(Validate(board, compiled, device, request, association),
                    PlanarGpuFailureCode::kValidationFailed);

  UntrustedKernelResult generator = valid;
  generator.generator = PlanarGenerator::kHeadingAwareSweep;
  ExpectFailureCode(Validate(board, compiled, device, request, generator),
                    PlanarGpuFailureCode::kValidationFailed);

  UntrustedKernelResult bounds = valid;
  bounds.labels.pop_back();
  ExpectFailureCode(Validate(board, compiled, device, request, bounds),
                    PlanarGpuFailureCode::kInternalInvariant);

  UntrustedKernelResult predecessor = valid;
  predecessor.predecessors[predecessor.goal_state] =
      static_cast<std::uint32_t>(predecessor.labels.size());
  ExpectFailureCode(Validate(board, compiled, device, request, predecessor),
                    PlanarGpuFailureCode::kInternalInvariant);

  UntrustedKernelResult heading = valid;
  const std::uint32_t away_from_start =
      StateIndex(NodeIndexForState(valid.goal_state), kNoIncomingHeading);
  heading.labels[away_from_start] = 1;
  ExpectFailureCode(Validate(board, compiled, device, request, heading),
                    PlanarGpuFailureCode::kInternalInvariant);

  UntrustedKernelResult cost = valid;
  ++cost.labels[cost.goal_state];
  ExpectFailureCode(Validate(board, compiled, device, request, cost),
                    PlanarGpuFailureCode::kInternalInvariant);

  UntrustedKernelResult cycle = valid;
  cycle.predecessors[cycle.goal_state] = cycle.goal_state;
  const PlanarGpuRouteResult cycle_result = Validate(board, compiled, device, request, cycle);
  ExpectFailureCode(cycle_result, PlanarGpuFailureCode::kInternalInvariant);
  EXPECT_EQ(std::get<PlanarGpuFailure>(cycle_result).invariant_id,
            "gpu.predecessor.self_reference.v1");

  UntrustedKernelResult memory = valid;
  memory.telemetry.batch_device_bytes = 0;
  memory.telemetry.peak_device_bytes = memory.telemetry.persistent_device_bytes;
  ExpectFailureCode(Validate(board, compiled, device, request, memory),
                    PlanarGpuFailureCode::kInternalInvariant);

  UntrustedKernelResult timing = valid;
  timing.telemetry.kernel_milliseconds = -1.0;
  ExpectFailureCode(Validate(board, compiled, device, request, timing),
                    PlanarGpuFailureCode::kInternalInvariant);

  UntrustedKernelResult inflated_peak = valid;
  ++inflated_peak.telemetry.peak_device_bytes;
  ExpectFailureCode(Validate(board, compiled, device, request, inflated_peak),
                    PlanarGpuFailureCode::kInternalInvariant);
}

TEST(GpuUntrustedResultTest, ValidatesFailureArraysAndPolicyBeforeClassifyingOutcomes) {
  BoardData data = test_support::ValidM1BoardData();
  data.obstacles.clear();
  const BoardSnapshot board = Snapshot(std::move(data));
  const CompiledBoard compiled = Compile(board, test_support::DefaultCompilerProfile({0}));
  DeviceCompiledBoardV1 device = Flatten(board, compiled);
  const CpuRouteRequest request = TwoTerminalRequest(board);
  UntrustedKernelResult valid = StraightEastResult(compiled, &device, request, false);
  valid.telemetry.rounds = 1;

  UntrustedKernelResult malformed_budget = valid;
  malformed_budget.completion = KernelCompletion::kBudgetExhausted;
  malformed_budget.labels.clear();
  ScriptedBackend malformed_backend(std::move(malformed_budget));
  const PlanarGpuRouteResult malformed = RouteWithPlanarGpuBackend(
      board, compiled, request,
      PlanarRoutePolicy{.generator = PlanarGenerator::kBucketedFrontier, .maximum_rounds = 1},
      malformed_backend);
  ExpectFailureCode(malformed, PlanarGpuFailureCode::kInternalInvariant);
  EXPECT_TRUE(std::get<PlanarGpuFailure>(malformed).telemetry.has_value());

  UntrustedKernelResult partial_sweep = valid;
  SetBatchTelemetry(device, PlanarGenerator::kHeadingAwareSweep, &partial_sweep);
  partial_sweep.completion = KernelCompletion::kBudgetExhausted;
  const std::uint32_t partial_state = partial_sweep.goal_state;
  partial_sweep.predecessors[partial_state] = kInvalidStateIndex;
  ScriptedBackend partial_backend(partial_sweep);
  const PlanarRoutePolicy partial_policy{.generator = PlanarGenerator::kHeadingAwareSweep,
                                         .maximum_rounds = 1};
  const PlanarGpuRouteResult partial =
      RouteWithPlanarGpuBackend(board, compiled, request, partial_policy, partial_backend);
  ExpectFailureCode(partial, PlanarGpuFailureCode::kResourceExhausted);
  EXPECT_TRUE(std::get<PlanarGpuFailure>(partial).telemetry.has_value());

  partial_sweep.predecessors[partial_state] = partial_state;
  ScriptedBackend corrupt_partial_backend(std::move(partial_sweep));
  const PlanarGpuRouteResult corrupt_partial =
      RouteWithPlanarGpuBackend(board, compiled, request, partial_policy, corrupt_partial_backend);
  ExpectFailureCode(corrupt_partial, PlanarGpuFailureCode::kInternalInvariant);
  EXPECT_EQ(std::get<PlanarGpuFailure>(corrupt_partial).invariant_id,
            "gpu.predecessor.self_reference.v1");

  UntrustedKernelResult corrupted_disconnected = valid;
  corrupted_disconnected.completion = KernelCompletion::kDisconnected;
  for (std::uint8_t heading = 0; heading < 8; ++heading) {
    const std::uint32_t goal_state = StateIndex(corrupted_disconnected.goal_node, heading);
    corrupted_disconnected.labels[goal_state] = kInfiniteRouteCost;
    corrupted_disconnected.predecessors[goal_state] = kInvalidStateIndex;
  }
  for (std::uint32_t state = 0;
       state < static_cast<std::uint32_t>(corrupted_disconnected.labels.size()); ++state) {
    if (corrupted_disconnected.labels[state] != kInfiniteRouteCost &&
        corrupted_disconnected.predecessors[state] != kInvalidStateIndex) {
      corrupted_disconnected.predecessors[state] = state;
      break;
    }
  }
  ScriptedBackend disconnected_backend(std::move(corrupted_disconnected));
  const PlanarGpuRouteResult disconnected = RouteWithPlanarGpuBackend(
      board, compiled, request, PlanarRoutePolicy{}, disconnected_backend);
  ExpectFailureCode(disconnected, PlanarGpuFailureCode::kInternalInvariant);

  ScriptedBackend failing_backend(valid, true);
  const PlanarGpuRouteResult backend_failure =
      RouteWithPlanarGpuBackend(board, compiled, request, PlanarRoutePolicy{}, failing_backend);
  ExpectFailureCode(backend_failure, PlanarGpuFailureCode::kBackendFailure);

  ScriptedBackend unused_backend(valid);
  PlanarRoutePolicy invalid_policy;
  invalid_policy.generator = static_cast<PlanarGenerator>(255);
  const PlanarGpuRouteResult invalid =
      RouteWithPlanarGpuBackend(board, compiled, request, invalid_policy, unused_backend);
  ExpectFailureCode(invalid, PlanarGpuFailureCode::kInvalidInput);
  EXPECT_EQ(unused_backend.metadata_queries, 0U);
}

TEST(GpuPreparedViewTest, ReusesOneUploadAndRejectsStaleBoardAssociation) {
  BoardData data = test_support::ValidM1BoardData();
  data.obstacles.clear();
  const BoardSnapshot board = Snapshot(data);
  const CompiledBoard compiled = Compile(board, test_support::DefaultCompilerProfile({0}));
  DeviceCompiledBoardV1 device = Flatten(board, compiled);
  const CpuRouteRequest request = TwoTerminalRequest(board);
  UntrustedKernelResult kernel_result = StraightEastResult(compiled, &device, request, false);
  kernel_result.telemetry.rounds = 1;
  ScriptedBackend backend(std::move(kernel_result));

  PreparedPlanarCompiledViewResult prepared_result =
      PreparePlanarCompiledView(board, compiled, backend);
  ASSERT_TRUE(std::holds_alternative<std::unique_ptr<PreparedPlanarCompiledView>>(prepared_result));
  std::unique_ptr<PreparedPlanarCompiledView> prepared =
      std::get<std::unique_ptr<PreparedPlanarCompiledView>>(std::move(prepared_result));
  ASSERT_NE(prepared, nullptr);
  EXPECT_EQ(backend.metadata_queries, 1U);
  EXPECT_EQ(backend.uploads, 1U);

  for (int repetition = 0; repetition < 2; ++repetition) {
    const PlanarGpuRouteResult result =
        RouteWithPreparedPlanarGpuBackend(board, compiled, request, PlanarRoutePolicy{}, *prepared);
    ASSERT_TRUE(std::holds_alternative<PlanarGpuRoute>(result));
  }
  EXPECT_EQ(backend.metadata_queries, 1U);
  EXPECT_EQ(backend.uploads, 1U);
  EXPECT_EQ(backend.executions, 2U);
  EXPECT_EQ(backend.readbacks, 2U);

  ++data.revision;
  const BoardSnapshot stale_board = Snapshot(std::move(data));
  const PlanarGpuRouteResult stale = RouteWithPreparedPlanarGpuBackend(
      stale_board, compiled, request, PlanarRoutePolicy{}, *prepared);
  ExpectFailureCode(stale, PlanarGpuFailureCode::kValidationFailed);
  EXPECT_EQ(backend.executions, 2U);
}

TEST(GpuCandidateBatchHostTest, EmptyDefaultPolicyReachesBackendWithoutUndefinedPointerMath) {
  const BoardSnapshot board = Snapshot();
  const CompiledBoard compiled = Compile(board, test_support::DefaultCompilerProfile({0}));
  DeviceCompiledBoardV1 device = Flatten(board, compiled);
  const CpuRouteRequest request = TwoTerminalRequest(board);
  ScriptedBackend backend(StraightEastResult(compiled, &device, request, false));

  PreparedPlanarCompiledViewResult prepared_result =
      PreparePlanarCompiledView(board, compiled, backend);
  ASSERT_TRUE(std::holds_alternative<std::unique_ptr<PreparedPlanarCompiledView>>(prepared_result));
  std::unique_ptr<PreparedPlanarCompiledView> prepared =
      std::get<std::unique_ptr<PreparedPlanarCompiledView>>(std::move(prepared_result));
  ASSERT_NE(prepared, nullptr);

  const PlanarCandidateBatchQuery query{
      .query_id = 1,
      .input_ordinal = 0,
      .request = request,
  };
  const PlanarCandidateBatchResult result = RouteCandidateBatchWithPreparedPlanarGpuBackend(
      board, compiled, std::span(&query, 1), PlanarCandidateBatchPolicy{.batch_id = 1}, *prepared);

  ASSERT_TRUE(std::holds_alternative<PlanarCandidateBatch>(result));
  const PlanarCandidateBatch& batch = std::get<PlanarCandidateBatch>(result);
  ASSERT_EQ(batch.items.size(), 1U);
  ASSERT_TRUE(std::holds_alternative<PlanarGpuFailure>(batch.items.front().result));
  EXPECT_EQ(std::get<PlanarGpuFailure>(batch.items.front().result).code,
            PlanarGpuFailureCode::kUnsupported);
}

TEST(GpuCandidateBatchHostTest, ValidatesEnvelopeAndCancelsItemsBeforeBackendDiscovery) {
  const BoardSnapshot board = Snapshot();
  const CompiledBoard compiled = Compile(board, test_support::DefaultCompilerProfile({0}));
  DeviceCompiledBoardV1 device = Flatten(board, compiled);
  const CpuRouteRequest request = TwoTerminalRequest(board);
  const PlanarCandidateBatchQuery query{
      .query_id = 7,
      .input_ordinal = 0,
      .request = request,
  };

  ScriptedBackend invalid_backend(StraightEastResult(compiled, &device, request, false), true);
  PlanarCandidateBatchPolicy unsupported_policy{.batch_id = 1};
  ++unsupported_policy.schema_version;
  const PlanarCandidateBatchResult unsupported = RouteCandidateBatchWithPlanarGpuBackend(
      board, compiled, std::span(&query, 1), unsupported_policy, invalid_backend);
  ASSERT_TRUE(std::holds_alternative<PlanarGpuFailure>(unsupported));
  EXPECT_EQ(std::get<PlanarGpuFailure>(unsupported).code, PlanarGpuFailureCode::kUnsupported);
  EXPECT_EQ(invalid_backend.metadata_queries, 0U);

  std::atomic_bool cancellation = true;
  ScriptedBackend cancelled_backend(StraightEastResult(compiled, &device, request, false), true);
  const PlanarCandidateBatchResult cancelled =
      RouteCandidateBatchWithPlanarGpuBackend(board, compiled, std::span(&query, 1),
                                              PlanarCandidateBatchPolicy{
                                                  .batch_id = 2,
                                                  .cancellation = &cancellation,
                                              },
                                              cancelled_backend);
  ASSERT_TRUE(std::holds_alternative<PlanarCandidateBatch>(cancelled));
  const PlanarCandidateBatch& batch = std::get<PlanarCandidateBatch>(cancelled);
  ASSERT_EQ(batch.items.size(), 1U);
  ASSERT_TRUE(std::holds_alternative<PlanarGpuFailure>(batch.items.front().result));
  EXPECT_EQ(std::get<PlanarGpuFailure>(batch.items.front().result).code,
            PlanarGpuFailureCode::kCancelled);
  EXPECT_EQ(cancelled_backend.metadata_queries, 0U);
}

TEST(GpuCandidateBatchHostTest, PreservesInvalidPeersWhenPreparedUploadDiscoveryFails) {
  const BoardSnapshot board = Snapshot();
  const CompiledBoard compiled = Compile(board, test_support::DefaultCompilerProfile({0}));
  DeviceCompiledBoardV1 device = Flatten(board, compiled);
  const CpuRouteRequest request = TwoTerminalRequest(board);
  const std::vector<PlanarCandidateBatchQuery> queries{
      PlanarCandidateBatchQuery{
          .query_id = 9,
          .input_ordinal = 0,
          .request = request,
      },
      PlanarCandidateBatchQuery{
          .query_id = 3,
          .input_ordinal = 1,
          .request = request,
      },
  };
  ScriptedBackend backend(StraightEastResult(compiled, &device, request, false), true);

  const PlanarCandidateBatchResult result = RouteCandidateBatchWithPlanarGpuBackend(
      board, compiled, queries, PlanarCandidateBatchPolicy{.batch_id = 31}, backend);

  ASSERT_TRUE(std::holds_alternative<PlanarCandidateBatch>(result));
  const PlanarCandidateBatch& batch = std::get<PlanarCandidateBatch>(result);
  ASSERT_EQ(batch.items.size(), 2U);
  EXPECT_EQ(batch.items[0].query_id, 3U);
  ASSERT_TRUE(std::holds_alternative<PlanarGpuFailure>(batch.items[0].result));
  EXPECT_EQ(std::get<PlanarGpuFailure>(batch.items[0].result).code,
            PlanarGpuFailureCode::kInvalidInput);
  EXPECT_EQ(batch.items[1].query_id, 9U);
  ASSERT_TRUE(std::holds_alternative<PlanarGpuFailure>(batch.items[1].result));
  EXPECT_EQ(std::get<PlanarGpuFailure>(batch.items[1].result).code,
            PlanarGpuFailureCode::kBackendFailure);
  EXPECT_EQ(backend.metadata_queries, 1U);
  EXPECT_EQ(backend.uploads, 0U);
}

TEST(GpuCandidateBatchHostTest, EnforcesLogicalHostAndAggregatePolicyBoundsBeforeDiscovery) {
  const BoardSnapshot board = Snapshot();
  const CompiledBoard compiled = Compile(board, test_support::DefaultCompilerProfile({0}));
  DeviceCompiledBoardV1 device = Flatten(board, compiled);
  const CpuRouteRequest request = TwoTerminalRequest(board);
  const PlanarCandidateBatchQuery query{
      .query_id = 7,
      .input_ordinal = 0,
      .request = request,
  };
  ScriptedBackend backend(StraightEastResult(compiled, &device, request, false), true);

  const PlanarCandidateBatchResult result =
      RouteCandidateBatchWithPlanarGpuBackend(board, compiled, std::span(&query, 1),
                                              PlanarCandidateBatchPolicy{
                                                  .batch_id = 32,
                                                  .maximum_host_bytes = 1,
                                              },
                                              backend);

  ASSERT_TRUE(std::holds_alternative<PlanarGpuFailure>(result));
  EXPECT_EQ(std::get<PlanarGpuFailure>(result).code, PlanarGpuFailureCode::kResourceExhausted);
  EXPECT_EQ(backend.metadata_queries, 0U);
  EXPECT_EQ(backend.uploads, 0U);
  EXPECT_FALSE(EstimateCandidateBatchHostBytesV1(1, routing::kMaximumPolicyResourceEntries + 1, 1)
                   .has_value());
  EXPECT_FALSE(EstimateCandidateBatchHostBytesV1(std::numeric_limits<std::uint64_t>::max(),
                                                 routing::kMaximumPolicyResourceEntries,
                                                 std::numeric_limits<std::uint64_t>::max())
                   .has_value());
}

TEST(GpuUntrustedResultTest, ExactSweptGeometryRejectsStructurallyValidFalseFreePath) {
  const BoardSnapshot board = Snapshot();
  const CompiledBoard compiled = Compile(board, test_support::DefaultCompilerProfile({0}));
  DeviceCompiledBoardV1 device = Flatten(board, compiled);
  const CpuRouteRequest request = TwoTerminalRequest(board);
  const UntrustedKernelResult false_free = StraightEastResult(compiled, &device, request, true);

  ExpectFailureCode(Validate(board, compiled, device, request, false_free),
                    PlanarGpuFailureCode::kValidationFailed);
}

}  // namespace
}  // namespace apgar::gpu
