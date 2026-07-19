#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <ranges>
#include <string>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

#include "apgar/board_ir/stable_hash.h"
#include "apgar/gpu/planar_router.h"

namespace apgar::gpu {
namespace {

using geometry_compiler::CompiledBoard;
using geometry_compiler::Direction;
using geometry_compiler::DirectionDelta;
using geometry_compiler::LatticeIndex;
using UWide = __uint128_t;

struct NodeKey {
  board_ir::LayerId layer;
  std::int64_t x;
  std::int64_t y;

  friend auto operator<=>(const NodeKey&, const NodeKey&) = default;
};

[[nodiscard]] PlanarGpuFailure Failure(PlanarGpuFailureCode code, std::string detail) {
  return PlanarGpuFailure{.code = code, .detail = std::move(detail), .obstacle = std::nullopt};
}

[[nodiscard]] std::optional<PlanarGpuFailure> ValidateAssociation(
    const board_ir::BoardSnapshot& board, const CompiledBoard& compiled) {
  if (compiled.source_board_content_hash() != board.content_hash()) {
    return Failure(PlanarGpuFailureCode::kValidationFailed,
                   "Compiled board source hash does not match the exact BoardSnapshot");
  }
  if (compiled.compiler_version() != geometry_compiler::kGeometryCompilerVersion) {
    return Failure(PlanarGpuFailureCode::kValidationFailed,
                   "Compiled board compiler version is not supported by device schema v1");
  }
  if (compiled.compiler_profile_fingerprint() !=
      geometry_compiler::FingerprintCompilerProfile(compiled.profile())) {
    return Failure(PlanarGpuFailureCode::kValidationFailed,
                   "Compiled board profile fingerprint does not match its payload");
  }
  if (compiled.rule_bucket() !=
      geometry_compiler::DeriveM1RuleBucket(board.data().routing_profile)) {
    return Failure(PlanarGpuFailureCode::kValidationFailed,
                   "Compiled board rule bucket is stale or does not match the BoardSnapshot");
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<std::uint64_t> PersistentBytes(
    const DeviceCompiledBoardV1& board) noexcept {
  UWide bytes = sizeof(DeviceCompiledHeaderV1);
  bytes += static_cast<UWide>(board.layers.size()) * sizeof(DeviceLayerRangeV1);
  bytes += static_cast<UWide>(board.nodes.size()) * sizeof(DeviceNodeV1);
  bytes += static_cast<UWide>(board.runs.size()) * sizeof(DeviceRunV1);
  bytes += static_cast<UWide>(board.run_nodes.size()) * sizeof(std::uint32_t);
  if (bytes > std::numeric_limits<std::uint64_t>::max()) {
    return std::nullopt;
  }
  return static_cast<std::uint64_t>(bytes);
}

[[nodiscard]] std::uint64_t Fingerprint(const DeviceCompiledBoardV1& board) {
  board_ir::StableHashBuilder hash;
  hash.AddString("APGAR-DEVICE-COMPILED-BOARD-V1");
  hash.AddU32(board.header.schema_version);
  hash.AddU32(board.header.compiler_version);
  hash.AddU64(board.header.source_board_content_hash);
  hash.AddU64(board.header.compiler_profile_fingerprint);
  hash.AddU64(board.header.rule_bucket_identity);
  hash.AddU64(board.header.represented_nodes);
  hash.AddU64(board.header.represented_states);
  hash.AddU64(board.header.estimated_persistent_device_bytes);
  hash.AddI64(board.header.lattice_origin.x);
  hash.AddI64(board.header.lattice_origin.y);
  hash.AddI64(board.header.lattice_step);
  hash.AddU32(board.header.costs.orthogonal_step);
  hash.AddU32(board.header.costs.diagonal_step);
  hash.AddU32(board.header.costs.bend);
  hash.AddByte(board.header.heading_mask);

  hash.AddU64(static_cast<std::uint64_t>(board.layers.size()));
  for (const DeviceLayerRangeV1& layer : board.layers) {
    hash.AddU32(layer.layer);
    hash.AddU32(layer.node_offset);
    hash.AddU32(layer.node_count);
    hash.AddI64(layer.minimum_lattice_x);
    hash.AddI64(layer.maximum_lattice_x);
    hash.AddI64(layer.minimum_lattice_y);
    hash.AddI64(layer.maximum_lattice_y);
  }

  hash.AddU64(static_cast<std::uint64_t>(board.nodes.size()));
  for (const DeviceNodeV1& node : board.nodes) {
    hash.AddI64(node.lattice_x);
    hash.AddI64(node.lattice_y);
    hash.AddU32(node.layer);
    hash.AddByte(node.legal_edges);
    for (std::uint32_t neighbor : node.neighbors) {
      hash.AddU32(neighbor);
    }
  }

  hash.AddU64(static_cast<std::uint64_t>(board.runs.size()));
  for (const DeviceRunV1& run : board.runs) {
    hash.AddU32(run.node_offset);
    hash.AddU32(run.node_count);
    hash.AddByte(static_cast<std::uint8_t>(run.direction));
  }
  hash.AddU64(static_cast<std::uint64_t>(board.run_nodes.size()));
  for (std::uint32_t node : board.run_nodes) {
    hash.AddU32(node);
  }
  return hash.Finish();
}

}  // namespace

DeviceCompiledBoardResult BuildDeviceCompiledBoardV1(const board_ir::BoardSnapshot& board,
                                                     const CompiledBoard& compiled_board) {
  if (std::optional<PlanarGpuFailure> association = ValidateAssociation(board, compiled_board);
      association.has_value()) {
    return std::move(*association);
  }
  if (compiled_board.telemetry().represented_nodes == 0 ||
      compiled_board.telemetry().represented_nodes > geometry_compiler::kMaximumRepresentedNodes) {
    return Failure(PlanarGpuFailureCode::kInternalInvariant,
                   "Compiled board represented-node telemetry is outside schema v1 bounds");
  }
  const UWide state_count =
      static_cast<UWide>(compiled_board.telemetry().represented_nodes) * kIncomingHeadingCount;
  if (state_count >= kInvalidStateIndex) {
    return Failure(PlanarGpuFailureCode::kResourceExhausted,
                   "Flattened heading-state count exceeds the stable uint32 index space");
  }

  DeviceCompiledBoardV1 device;
  device.header.compiler_version = compiled_board.compiler_version();
  device.header.source_board_content_hash = compiled_board.source_board_content_hash();
  device.header.compiler_profile_fingerprint = compiled_board.compiler_profile_fingerprint();
  device.header.rule_bucket_identity = compiled_board.rule_bucket().identity;
  device.header.represented_nodes = compiled_board.telemetry().represented_nodes;
  device.header.represented_states = static_cast<std::uint64_t>(state_count);
  device.header.lattice_origin = compiled_board.profile().lattice_origin;
  device.header.lattice_step = compiled_board.profile().lattice_step;
  device.header.costs = compiled_board.profile().costs;
  device.header.heading_mask = compiled_board.profile().heading_mask;

  device.nodes.reserve(static_cast<std::size_t>(compiled_board.telemetry().represented_nodes));
  std::map<NodeKey, std::uint32_t> indices;
  for (const geometry_compiler::SparseTile& tile : compiled_board.tiles()) {
    for (const geometry_compiler::CompiledNode& node : tile.nodes) {
      const LatticeIndex global = geometry_compiler::GlobalLatticeIndex(compiled_board.profile(),
                                                                        tile.key, node.local_index);
      if (device.nodes.size() >= kInvalidNodeIndex) {
        return Failure(PlanarGpuFailureCode::kResourceExhausted,
                       "Flattened node count exceeds the stable uint32 index space");
      }
      const std::uint32_t index = static_cast<std::uint32_t>(device.nodes.size());
      const bool inserted =
          indices.emplace(NodeKey{.layer = tile.key.layer, .x = global.x, .y = global.y}, index)
              .second;
      if (!inserted) {
        return Failure(PlanarGpuFailureCode::kInternalInvariant,
                       "Compiled sparse tiles contain a duplicate global node identity");
      }
      DeviceNodeV1 flattened{
          .lattice_x = global.x,
          .lattice_y = global.y,
          .layer = tile.key.layer,
          .legal_edges = node.legal_edges,
      };
      flattened.neighbors.fill(kInvalidNodeIndex);
      device.nodes.push_back(flattened);
    }
  }
  if (device.nodes.size() != compiled_board.telemetry().represented_nodes) {
    return Failure(PlanarGpuFailureCode::kInternalInvariant,
                   "Flattened node count disagrees with compiler telemetry");
  }

  for (std::uint32_t index = 0; index < device.nodes.size(); ++index) {
    DeviceNodeV1& node = device.nodes[index];
    for (Direction direction : geometry_compiler::kStableDirectionOrder) {
      const bool legal = (node.legal_edges & geometry_compiler::MaskFor(direction)) != 0;
      if ((compiled_board.profile().heading_mask & geometry_compiler::HeadingFor(direction)) == 0) {
        if (legal) {
          return Failure(PlanarGpuFailureCode::kValidationFailed,
                         "Compiled mask enables a direction excluded by its profile");
        }
        continue;
      }
      if (!legal) {
        continue;
      }
      const DirectionDelta delta = geometry_compiler::DeltaFor(direction);
      const auto found = indices.find(NodeKey{
          .layer = node.layer,
          .x = node.lattice_x + delta.x,
          .y = node.lattice_y + delta.y,
      });
      if (found == indices.end()) {
        return Failure(PlanarGpuFailureCode::kInternalInvariant,
                       "Compiled legal edge exits the represented sparse field");
      }
      node.neighbors[static_cast<std::size_t>(direction)] = found->second;
    }
  }

  for (std::uint32_t index = 0; index < device.nodes.size(); ++index) {
    const DeviceNodeV1& node = device.nodes[index];
    for (Direction direction : geometry_compiler::kStableDirectionOrder) {
      const bool legal = (node.legal_edges & geometry_compiler::MaskFor(direction)) != 0;
      const std::uint32_t neighbor = node.neighbors[static_cast<std::size_t>(direction)];
      if (legal != (neighbor != kInvalidNodeIndex)) {
        return Failure(PlanarGpuFailureCode::kInternalInvariant,
                       "Flattened legal mask and neighbor index disagree");
      }
      if (!legal) {
        continue;
      }
      const DeviceNodeV1& target = device.nodes[neighbor];
      if ((target.legal_edges &
           geometry_compiler::MaskFor(geometry_compiler::Opposite(direction))) == 0 ||
          target.neighbors[static_cast<std::size_t>(geometry_compiler::Opposite(direction))] !=
              index) {
        return Failure(PlanarGpuFailureCode::kInternalInvariant,
                       "Flattened directional edge lacks its compiled reverse edge");
      }
    }
  }

  std::size_t layer_begin = 0;
  while (layer_begin < device.nodes.size()) {
    const board_ir::LayerId layer = device.nodes[layer_begin].layer;
    std::size_t layer_end = layer_begin;
    DeviceLayerRangeV1 range{
        .layer = layer,
        .node_offset = static_cast<std::uint32_t>(layer_begin),
        .minimum_lattice_x = device.nodes[layer_begin].lattice_x,
        .maximum_lattice_x = device.nodes[layer_begin].lattice_x,
        .minimum_lattice_y = device.nodes[layer_begin].lattice_y,
        .maximum_lattice_y = device.nodes[layer_begin].lattice_y,
    };
    while (layer_end < device.nodes.size() && device.nodes[layer_end].layer == layer) {
      range.minimum_lattice_x =
          std::min(range.minimum_lattice_x, device.nodes[layer_end].lattice_x);
      range.maximum_lattice_x =
          std::max(range.maximum_lattice_x, device.nodes[layer_end].lattice_x);
      range.minimum_lattice_y =
          std::min(range.minimum_lattice_y, device.nodes[layer_end].lattice_y);
      range.maximum_lattice_y =
          std::max(range.maximum_lattice_y, device.nodes[layer_end].lattice_y);
      ++layer_end;
    }
    range.node_count = static_cast<std::uint32_t>(layer_end - layer_begin);
    device.layers.push_back(range);
    layer_begin = layer_end;
  }

  for (Direction direction : geometry_compiler::kStableDirectionOrder) {
    const std::size_t direction_index = static_cast<std::size_t>(direction);
    const Direction opposite = geometry_compiler::Opposite(direction);
    for (std::uint32_t start = 0; start < device.nodes.size(); ++start) {
      const DeviceNodeV1& start_node = device.nodes[start];
      if ((start_node.legal_edges & geometry_compiler::MaskFor(direction)) == 0) {
        continue;
      }
      const std::uint32_t previous = start_node.neighbors[static_cast<std::size_t>(opposite)];
      if (previous != kInvalidNodeIndex &&
          (device.nodes[previous].legal_edges & geometry_compiler::MaskFor(direction)) != 0) {
        continue;
      }

      const std::size_t offset = device.run_nodes.size();
      std::uint32_t cursor = start;
      device.run_nodes.push_back(cursor);
      while ((device.nodes[cursor].legal_edges & geometry_compiler::MaskFor(direction)) != 0) {
        const std::uint32_t next = device.nodes[cursor].neighbors[direction_index];
        if (next == kInvalidNodeIndex || device.run_nodes.size() - offset > device.nodes.size()) {
          return Failure(PlanarGpuFailureCode::kInternalInvariant,
                         "Directional run reconstruction encountered an invalid edge or cycle");
        }
        device.run_nodes.push_back(next);
        cursor = next;
      }
      const std::size_t count = device.run_nodes.size() - offset;
      if (offset >= kInvalidNodeIndex || count > std::numeric_limits<std::uint32_t>::max()) {
        return Failure(PlanarGpuFailureCode::kResourceExhausted,
                       "Directional run storage exceeds schema v1 index bounds");
      }
      device.runs.push_back(DeviceRunV1{
          .node_offset = static_cast<std::uint32_t>(offset),
          .node_count = static_cast<std::uint32_t>(count),
          .direction = direction,
      });
    }
  }

  const std::optional<std::uint64_t> bytes = PersistentBytes(device);
  if (!bytes.has_value()) {
    return Failure(PlanarGpuFailureCode::kResourceExhausted,
                   "Flattened device memory accounting overflowed uint64");
  }
  device.header.estimated_persistent_device_bytes = *bytes;
  device.header.device_view_fingerprint = Fingerprint(device);
  return device;
}

std::optional<std::uint32_t> FindDeviceNodeIndex(const DeviceCompiledBoardV1& board,
                                                 board_ir::LayerId layer,
                                                 LatticeIndex index) noexcept {
  const auto range = std::ranges::lower_bound(board.layers, layer, {}, &DeviceLayerRangeV1::layer);
  if (range == board.layers.end() || range->layer != layer) {
    return std::nullopt;
  }
  const std::uint32_t end = range->node_offset + range->node_count;
  for (std::uint32_t node = range->node_offset; node < end; ++node) {
    if (board.nodes[node].lattice_x == index.x && board.nodes[node].lattice_y == index.y) {
      return node;
    }
  }
  return std::nullopt;
}

const DeviceNodeV1* FindDeviceNode(const DeviceCompiledBoardV1& board, board_ir::LayerId layer,
                                   LatticeIndex index) noexcept {
  const std::optional<std::uint32_t> found = FindDeviceNodeIndex(board, layer, index);
  return found.has_value() ? &board.nodes[*found] : nullptr;
}

}  // namespace apgar::gpu
