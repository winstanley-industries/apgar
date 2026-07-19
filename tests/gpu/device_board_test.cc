#include <cstddef>
#include <cstdint>
#include <set>
#include <tuple>
#include <utility>
#include <variant>

#include "apgar/board_ir/board.h"
#include "apgar/geometry_compiler/compiled_board.h"
#include "apgar/gpu/planar_router.h"
#include "tests/support/board_builder.h"
#include "tests/support/compiler_builder.h"
#include "tests/support/google_test.h"

namespace apgar::gpu {
namespace {

using board_ir::BoardCreationResult;
using board_ir::BoardSnapshot;
using geometry_compiler::CompiledBoard;
using geometry_compiler::CompileError;
using geometry_compiler::CompilerProfile;
using geometry_compiler::Direction;

[[nodiscard]] BoardSnapshot Snapshot() {
  BoardCreationResult result = board_ir::CreateBoardSnapshot(test_support::ValidM1BoardData());
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

[[nodiscard]] DeviceCompiledBoardV1 Flatten(const BoardSnapshot& board,
                                            const CompiledBoard& compiled) {
  DeviceCompiledBoardResult result = BuildDeviceCompiledBoardV1(board, compiled);
  EXPECT_TRUE(std::holds_alternative<DeviceCompiledBoardV1>(result))
      << (std::holds_alternative<PlanarGpuFailure>(result)
              ? std::get<PlanarGpuFailure>(result).detail
              : "");
  return std::get<DeviceCompiledBoardV1>(std::move(result));
}

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

}  // namespace
}  // namespace apgar::gpu
