#include <cuda_runtime.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <ranges>
#include <sstream>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "apgar/gpu/cuda_backend.h"

namespace apgar::gpu {
namespace {

constexpr std::uint32_t kThreadsPerBlock = 256;

using geometry_compiler::Direction;

static_assert(static_cast<std::uint8_t>(Direction::kEast) == 0);
static_assert(static_cast<std::uint8_t>(Direction::kNorthEast) == 1);
static_assert(static_cast<std::uint8_t>(Direction::kNorth) == 2);
static_assert(static_cast<std::uint8_t>(Direction::kNorthWest) == 3);
static_assert(static_cast<std::uint8_t>(Direction::kWest) == 4);
static_assert(static_cast<std::uint8_t>(Direction::kSouthWest) == 5);
static_assert(static_cast<std::uint8_t>(Direction::kSouth) == 6);
static_assert(static_cast<std::uint8_t>(Direction::kSouthEast) == 7);
static_assert(geometry_compiler::Opposite(Direction::kEast) == Direction::kWest);
static_assert(geometry_compiler::Opposite(Direction::kNorthEast) == Direction::kSouthWest);
static_assert(geometry_compiler::Opposite(Direction::kNorth) == Direction::kSouth);
static_assert(geometry_compiler::Opposite(Direction::kNorthWest) == Direction::kSouthEast);
static_assert(!geometry_compiler::IsDiagonal(Direction::kEast));
static_assert(geometry_compiler::IsDiagonal(Direction::kNorthEast));
static_assert(!geometry_compiler::IsDiagonal(Direction::kNorth));
static_assert(geometry_compiler::IsDiagonal(Direction::kNorthWest));
static_assert(!geometry_compiler::IsDiagonal(Direction::kWest));
static_assert(geometry_compiler::IsDiagonal(Direction::kSouthWest));
static_assert(!geometry_compiler::IsDiagonal(Direction::kSouth));
static_assert(geometry_compiler::IsDiagonal(Direction::kSouthEast));

struct FrontierRoundStats {
  std::uint64_t minimum_next_label;
  std::uint64_t examined_work;
};
static_assert(sizeof(FrontierRoundStats) == 16);

inline constexpr std::uint64_t kSweepChangedBit = std::uint64_t{1} << 63U;
inline constexpr std::uint64_t kSweepWorkMask = ~kSweepChangedBit;

[[nodiscard]] std::uint32_t BlockCount(std::uint64_t work_items) {
  return static_cast<std::uint32_t>((work_items + kThreadsPerBlock - 1) / kThreadsPerBlock);
}

[[nodiscard]] BackendError CudaError(BackendErrorCode code, const char* operation,
                                     cudaError_t status) {
  return BackendError{.code = code,
                      .detail = std::string(operation) + " failed: " + cudaGetErrorString(status)};
}

[[nodiscard]] BackendError CudaError(const char* operation, cudaError_t status) {
  const BackendErrorCode code = status == cudaErrorMemoryAllocation
                                    ? BackendErrorCode::kResourceExhausted
                                    : BackendErrorCode::kBackendFailure;
  return CudaError(code, operation, status);
}

template <typename T>
class DeviceBuffer {
 public:
  DeviceBuffer() = default;
  DeviceBuffer(const DeviceBuffer&) = delete;
  DeviceBuffer& operator=(const DeviceBuffer&) = delete;

  DeviceBuffer(DeviceBuffer&& other) noexcept
      : data_(std::exchange(other.data_, nullptr)),
        count_(std::exchange(other.count_, 0)),
        device_(std::exchange(other.device_, -1)) {}

  DeviceBuffer& operator=(DeviceBuffer&& other) noexcept {
    if (this != &other) {
      Reset();
      data_ = std::exchange(other.data_, nullptr);
      count_ = std::exchange(other.count_, 0);
      device_ = std::exchange(other.device_, -1);
    }
    return *this;
  }

  ~DeviceBuffer() { Reset(); }

  [[nodiscard]] std::optional<BackendError> Allocate(std::size_t count, const char* operation) {
    Reset();
    if (count == 0) {
      return std::nullopt;
    }
    int device = -1;
    if (const cudaError_t status = cudaGetDevice(&device); status != cudaSuccess) {
      return CudaError("cudaGetDevice(allocation)", status);
    }
    const cudaError_t status = cudaMalloc(&data_, count * sizeof(T));
    if (status != cudaSuccess) {
      return CudaError(operation, status);
    }
    count_ = count;
    device_ = device;
    return std::nullopt;
  }

  void Reset() noexcept {
    if (data_ != nullptr) {
      int previous_device = -1;
      const bool restore_device = cudaGetDevice(&previous_device) == cudaSuccess && device_ >= 0 &&
                                  previous_device != device_ &&
                                  cudaSetDevice(device_) == cudaSuccess;
      cudaFree(data_);
      if (restore_device) {
        cudaSetDevice(previous_device);
      }
    }
    data_ = nullptr;
    count_ = 0;
    device_ = -1;
  }

  [[nodiscard]] T* get() noexcept { return data_; }
  [[nodiscard]] const T* get() const noexcept { return data_; }
  [[nodiscard]] std::size_t count() const noexcept { return count_; }
  [[nodiscard]] std::uint64_t bytes() const noexcept {
    return static_cast<std::uint64_t>(count_) * sizeof(T);
  }

 private:
  T* data_ = nullptr;
  std::size_t count_ = 0;
  int device_ = -1;
};

class CudaUploadedCompiledView final : public UploadedCompiledView {
 public:
  int device = -1;
  DeviceCompiledHeaderV1 header;
  DeviceBuffer<DeviceCompiledHeaderV1> device_header;
  DeviceBuffer<DeviceLayerRangeV1> layers;
  DeviceBuffer<DeviceNodeV1> nodes;
  DeviceBuffer<DeviceRunV1> runs;
  DeviceBuffer<std::uint32_t> run_nodes;
};

class CudaPendingRouteExecution final : public PendingRouteExecution {
 public:
  int device = -1;
  BackendExecutionRequest request{};
  KernelCompletion completion = KernelCompletion::kDisconnected;
  KernelTelemetry telemetry;
  DeviceBuffer<DeviceResultHeaderV1> result_header;
  DeviceBuffer<std::uint64_t> labels;
  DeviceBuffer<std::uint32_t> predecessors;
};

__global__ void InitializeResultHeader(const DeviceCompiledHeaderV1* compiled,
                                       DeviceResultHeaderV1* result, std::uint32_t start_node,
                                       std::uint32_t goal_node, PlanarGenerator generator) {
  if (blockIdx.x != 0 || threadIdx.x != 0) {
    return;
  }
  result->schema_version = compiled->schema_version;
  result->compiler_version = compiled->compiler_version;
  result->start_node = start_node;
  result->goal_node = goal_node;
  result->source_board_content_hash = compiled->source_board_content_hash;
  result->compiler_profile_fingerprint = compiled->compiler_profile_fingerprint;
  result->rule_bucket_identity = compiled->rule_bucket_identity;
  result->device_view_fingerprint = compiled->device_view_fingerprint;
  result->generator = generator;
  for (std::uint8_t& byte : result->reserved) {
    byte = 0;
  }
}

__device__ std::uint64_t DeviceStepCost(const DeviceCompiledHeaderV1& header,
                                        std::uint8_t direction, std::uint8_t incoming_heading) {
  const bool diagonal = (direction % 2U) != 0;
  std::uint64_t cost = diagonal ? header.costs.diagonal_step : header.costs.orthogonal_step;
  if (incoming_heading != kNoIncomingHeading && incoming_heading != direction) {
    cost += header.costs.bend;
  }
  return cost;
}

__device__ std::uint32_t DeviceStateIndex(std::uint32_t node_index, std::uint8_t incoming_heading) {
  return node_index * kIncomingHeadingCount + incoming_heading;
}

__device__ bool DeviceCheckedAdd(std::uint64_t left, std::uint64_t right, std::uint64_t* result) {
  if (left == kInfiniteRouteCost || right > kInfiniteRouteCost - left) {
    return false;
  }
  *result = left + right;
  return true;
}

// Frontier workers can read a state while another worker relaxes that same
// label. Pair every such read with the device-scoped atomicMin writers.
__device__ std::uint64_t DeviceAtomicLoad(const std::uint64_t* value) {
  return static_cast<std::uint64_t>(
      atomicAdd(reinterpret_cast<unsigned long long*>(const_cast<std::uint64_t*>(value)), 0ULL));
}

__global__ void InitializeSearch(std::uint64_t* labels, std::uint32_t* predecessors,
                                 std::uint32_t* active, std::uint64_t state_count,
                                 std::uint32_t start_state) {
  const std::uint64_t state = static_cast<std::uint64_t>(blockIdx.x) * blockDim.x + threadIdx.x;
  if (state >= state_count) {
    return;
  }
  labels[state] = state == start_state ? 0 : kInfiniteRouteCost;
  predecessors[state] = kInvalidStateIndex;
  if (active != nullptr) {
    active[state] = state == start_state ? 1 : 0;
  }
}

__global__ void FrontierRelax(const DeviceNodeV1* nodes, DeviceCompiledHeaderV1 header,
                              const std::uint32_t* current_active, std::uint32_t* next_active,
                              std::uint32_t current_generation, std::uint32_t next_generation,
                              std::uint64_t* labels, std::uint64_t bucket_upper,
                              FrontierRoundStats* stats) {
  const std::uint64_t state = static_cast<std::uint64_t>(blockIdx.x) * blockDim.x + threadIdx.x;
  if (state >= header.represented_states || current_active[state] != current_generation) {
    return;
  }
  const std::uint64_t label = DeviceAtomicLoad(&labels[state]);
  if (label == kInfiniteRouteCost) {
    return;
  }
  if (label > bucket_upper) {
    atomicExch(&next_active[state], next_generation);
    atomicMin(reinterpret_cast<unsigned long long*>(&stats->minimum_next_label),
              static_cast<unsigned long long>(label));
    return;
  }

  const std::uint32_t node_index = static_cast<std::uint32_t>(state / kIncomingHeadingCount);
  const std::uint8_t incoming = static_cast<std::uint8_t>(state % kIncomingHeadingCount);
  const DeviceNodeV1 node = nodes[node_index];
  std::uint64_t local_examined_work = 0;
  for (std::uint8_t direction = 0; direction < 8; ++direction) {
    if ((node.legal_edges & static_cast<std::uint8_t>(1U << direction)) == 0) {
      continue;
    }
    ++local_examined_work;
    const std::uint32_t neighbor = node.neighbors[direction];
    const std::uint32_t target_state = DeviceStateIndex(neighbor, direction);
    std::uint64_t candidate = 0;
    if (!DeviceCheckedAdd(label, DeviceStepCost(header, direction, incoming), &candidate)) {
      continue;
    }
    const unsigned long long previous =
        atomicMin(reinterpret_cast<unsigned long long*>(&labels[target_state]),
                  static_cast<unsigned long long>(candidate));
    if (candidate < previous) {
      atomicExch(&next_active[target_state], next_generation);
      atomicMin(reinterpret_cast<unsigned long long*>(&stats->minimum_next_label),
                static_cast<unsigned long long>(candidate));
    }
  }
  if (local_examined_work != 0) {
    atomicAdd(reinterpret_cast<unsigned long long*>(&stats->examined_work),
              static_cast<unsigned long long>(local_examined_work));
  }
}

__global__ void ComputeDepartureLabels(DeviceCompiledHeaderV1 header, std::uint32_t start_node,
                                       const std::uint64_t* labels, std::uint64_t* departure_labels,
                                       std::uint64_t* turn_relaxations) {
  const std::uint64_t item = static_cast<std::uint64_t>(blockIdx.x) * blockDim.x + threadIdx.x;
  const std::uint64_t departure_count = header.represented_nodes * 8;
  if (item >= departure_count) {
    return;
  }
  const std::uint32_t node = static_cast<std::uint32_t>(item / 8);
  const std::uint8_t direction = static_cast<std::uint8_t>(item % 8);
  const std::uint64_t same_heading = labels[DeviceStateIndex(node, direction)];
  std::uint64_t best = node == start_node ? 0 : same_heading;
  bool used_turn = false;
  for (std::uint8_t incoming = 0; incoming < 8; ++incoming) {
    const std::uint64_t label = labels[DeviceStateIndex(node, incoming)];
    if (label == kInfiniteRouteCost) {
      continue;
    }
    std::uint64_t candidate = 0;
    const std::uint64_t turn_cost = incoming == direction ? 0 : header.costs.bend;
    if (DeviceCheckedAdd(label, turn_cost, &candidate) && candidate < best) {
      best = candidate;
      used_turn = incoming != direction;
    }
  }
  departure_labels[item] = best;
  if (used_turn) {
    atomicAdd(reinterpret_cast<unsigned long long*>(turn_relaxations), 1ULL);
  }
}

__global__ void SweepRuns(const DeviceRunV1* runs, std::uint64_t run_count,
                          const std::uint32_t* run_nodes, DeviceCompiledHeaderV1 header,
                          const std::uint64_t* departure_labels, std::uint64_t* labels,
                          std::uint64_t* round_stats) {
  const std::uint64_t run_index = static_cast<std::uint64_t>(blockIdx.x) * blockDim.x + threadIdx.x;
  if (run_index >= run_count) {
    return;
  }
  const DeviceRunV1 run = runs[run_index];
  const std::uint8_t direction = static_cast<std::uint8_t>(run.direction);
  const std::uint64_t step_cost =
      (direction % 2U) != 0 ? header.costs.diagonal_step : header.costs.orthogonal_step;
  const std::uint32_t first = run_nodes[run.node_offset];
  std::uint64_t carry = departure_labels[static_cast<std::uint64_t>(first) * 8 + direction];
  bool changed = false;
  for (std::uint32_t position = 0; position + 1 < run.node_count; ++position) {
    const std::uint32_t source = run_nodes[run.node_offset + position];
    const std::uint32_t target = run_nodes[run.node_offset + position + 1];
    carry = min(carry, departure_labels[static_cast<std::uint64_t>(source) * 8 + direction]);
    std::uint64_t candidate = 0;
    if (DeviceCheckedAdd(carry, step_cost, &candidate)) {
      const std::uint32_t target_state = DeviceStateIndex(target, direction);
      const unsigned long long previous =
          atomicMin(reinterpret_cast<unsigned long long*>(&labels[target_state]),
                    static_cast<unsigned long long>(candidate));
      if (candidate < previous) {
        changed = true;
      }
      carry = min(candidate, departure_labels[static_cast<std::uint64_t>(target) * 8 + direction]);
    } else {
      carry = departure_labels[static_cast<std::uint64_t>(target) * 8 + direction];
    }
  }
  atomicAdd(reinterpret_cast<unsigned long long*>(round_stats),
            static_cast<unsigned long long>(run.node_count - 1));
  if (changed) {
    atomicOr(reinterpret_cast<unsigned long long*>(round_stats),
             static_cast<unsigned long long>(kSweepChangedBit));
  }
}

__global__ void SelectStablePredecessors(const DeviceNodeV1* nodes, DeviceCompiledHeaderV1 header,
                                         std::uint32_t start_node, const std::uint64_t* labels,
                                         std::uint32_t* predecessors) {
  const std::uint64_t state = static_cast<std::uint64_t>(blockIdx.x) * blockDim.x + threadIdx.x;
  if (state >= header.represented_states) {
    return;
  }
  const std::uint32_t node = static_cast<std::uint32_t>(state / kIncomingHeadingCount);
  const std::uint8_t incoming = static_cast<std::uint8_t>(state % kIncomingHeadingCount);
  const std::uint32_t start_state = DeviceStateIndex(start_node, kNoIncomingHeading);
  if (state == start_state || incoming == kNoIncomingHeading ||
      labels[state] == kInfiniteRouteCost) {
    predecessors[state] = kInvalidStateIndex;
    return;
  }

  const std::uint8_t opposite = static_cast<std::uint8_t>((incoming + 4U) % 8U);
  const std::uint32_t predecessor_node = nodes[node].neighbors[opposite];
  if (predecessor_node == kInvalidNodeIndex) {
    predecessors[state] = kInvalidStateIndex;
    return;
  }
  std::uint32_t best = kInvalidStateIndex;
  for (std::uint8_t predecessor_heading = 0; predecessor_heading < kIncomingHeadingCount;
       ++predecessor_heading) {
    if (predecessor_heading == kNoIncomingHeading && predecessor_node != start_node) {
      continue;
    }
    const std::uint32_t predecessor_state = DeviceStateIndex(predecessor_node, predecessor_heading);
    const std::uint64_t predecessor_label = labels[predecessor_state];
    std::uint64_t candidate = 0;
    if (DeviceCheckedAdd(predecessor_label, DeviceStepCost(header, incoming, predecessor_heading),
                         &candidate) &&
        candidate == labels[state] && predecessor_state < best) {
      best = predecessor_state;
    }
  }
  predecessors[state] = best;
}

[[nodiscard]] std::optional<BackendError> CopyToDevice(void* destination, const void* source,
                                                       std::size_t bytes, const char* operation) {
  if (bytes == 0) {
    return std::nullopt;
  }
  const cudaError_t status = cudaMemcpy(destination, source, bytes, cudaMemcpyHostToDevice);
  if (status != cudaSuccess) {
    return CudaError(operation, status);
  }
  return std::nullopt;
}

template <typename T>
[[nodiscard]] std::optional<BackendError> AllocateAndUpload(DeviceBuffer<T>& destination,
                                                            const T* source, std::size_t count,
                                                            const char* allocation_operation,
                                                            const char* copy_operation) {
  if (std::optional<BackendError> error = destination.Allocate(count, allocation_operation);
      error.has_value()) {
    return error;
  }
  return CopyToDevice(destination.get(), source, count * sizeof(T), copy_operation);
}

[[nodiscard]] BackendError InvalidDeviceView(std::string detail) {
  return BackendError{.code = BackendErrorCode::kInternalInvariant, .detail = std::move(detail)};
}

[[nodiscard]] bool DirectionIsEnabled(const DeviceCompiledHeaderV1& header, Direction direction) {
  return (header.heading_mask & geometry_compiler::HeadingFor(direction)) != 0;
}

[[nodiscard]] std::optional<BackendError> ValidateDeviceViewForUpload(
    const DeviceCompiledBoardV1& board) {
  const DeviceCompiledHeaderV1& header = board.header;
  const __uint128_t expected_states =
      static_cast<__uint128_t>(board.nodes.size()) * kIncomingHeadingCount;
  if (header.schema_version != kDeviceCompiledBoardSchemaVersion ||
      header.compiler_version != geometry_compiler::kGeometryCompilerVersion ||
      board.nodes.empty() || board.nodes.size() > geometry_compiler::kMaximumRepresentedNodes ||
      board.nodes.size() >= kInvalidNodeIndex || header.represented_nodes != board.nodes.size() ||
      expected_states >= kInvalidStateIndex ||
      header.represented_states != static_cast<std::uint64_t>(expected_states)) {
    return InvalidDeviceView(
        "CUDA upload rejected inconsistent schema, compiler, or node/state "
        "count bounds");
  }
  if (!board_ir::PointIsValid(header.lattice_origin) || header.lattice_step <= 0 ||
      header.lattice_step > board_ir::kMaxAbsDbCoord || header.costs.orthogonal_step == 0 ||
      header.costs.diagonal_step == 0 || header.heading_mask == 0 ||
      (header.heading_mask & static_cast<board_ir::HeadingMask>(~board_ir::kM1HeadingMask)) != 0 ||
      std::ranges::any_of(header.reserved, [](std::uint8_t byte) { return byte != 0; })) {
    return InvalidDeviceView(
        "CUDA upload rejected invalid lattice, cost, heading-mask, or reserved header fields");
  }
  const __uint128_t maximum_transition_cost =
      static_cast<__uint128_t>(std::max(header.costs.orthogonal_step, header.costs.diagonal_step)) +
      header.costs.bend;
  if (maximum_transition_cost * header.represented_nodes *
          geometry_compiler::kStableDirectionOrder.size() >
      std::numeric_limits<std::uint64_t>::max()) {
    return InvalidDeviceView("CUDA upload rejected costs that exceed the simple-state path bound");
  }

  if (board.layers.empty()) {
    return InvalidDeviceView("CUDA upload rejected an empty layer partition");
  }
  std::uint64_t expected_node_offset = 0;
  for (std::size_t layer_index = 0; layer_index < board.layers.size(); ++layer_index) {
    const DeviceLayerRangeV1& layer = board.layers[layer_index];
    const std::uint64_t range_end =
        static_cast<std::uint64_t>(layer.node_offset) + layer.node_count;
    if (layer.node_count == 0 || layer.node_offset != expected_node_offset ||
        range_end > board.nodes.size() || layer.minimum_lattice_x > layer.maximum_lattice_x ||
        layer.minimum_lattice_y > layer.maximum_lattice_y ||
        (layer_index != 0 && board.layers[layer_index - 1].layer >= layer.layer)) {
      return InvalidDeviceView("CUDA upload rejected an invalid or non-canonical layer range");
    }
    std::int64_t minimum_x = board.nodes[layer.node_offset].lattice_x;
    std::int64_t maximum_x = minimum_x;
    std::int64_t minimum_y = board.nodes[layer.node_offset].lattice_y;
    std::int64_t maximum_y = minimum_y;
    for (std::uint64_t node_index = layer.node_offset; node_index < range_end; ++node_index) {
      const DeviceNodeV1& node = board.nodes[node_index];
      if (node.layer != layer.layer) {
        return InvalidDeviceView("CUDA upload rejected a node outside its declared layer range");
      }
      minimum_x = std::min(minimum_x, node.lattice_x);
      maximum_x = std::max(maximum_x, node.lattice_x);
      minimum_y = std::min(minimum_y, node.lattice_y);
      maximum_y = std::max(maximum_y, node.lattice_y);
    }
    if (minimum_x != layer.minimum_lattice_x || maximum_x != layer.maximum_lattice_x ||
        minimum_y != layer.minimum_lattice_y || maximum_y != layer.maximum_lattice_y) {
      return InvalidDeviceView("CUDA upload rejected inexact layer bounds");
    }
    expected_node_offset = range_end;
  }
  if (expected_node_offset != board.nodes.size()) {
    return InvalidDeviceView(
        "CUDA upload rejected a layer partition that does not cover all nodes");
  }

  for (std::uint32_t node_index = 0; node_index < board.nodes.size(); ++node_index) {
    const DeviceNodeV1& node = board.nodes[node_index];
    if (std::ranges::any_of(node.reserved, [](std::uint8_t byte) { return byte != 0; })) {
      return InvalidDeviceView("CUDA upload rejected nonzero reserved node fields");
    }
    for (Direction direction : geometry_compiler::kStableDirectionOrder) {
      const std::size_t direction_index = static_cast<std::size_t>(direction);
      const bool legal = (node.legal_edges & geometry_compiler::MaskFor(direction)) != 0;
      const std::uint32_t neighbor_index = node.neighbors[direction_index];
      if (legal != (neighbor_index != kInvalidNodeIndex) ||
          (legal &&
           (!DirectionIsEnabled(header, direction) || neighbor_index >= board.nodes.size()))) {
        return InvalidDeviceView(
            "CUDA upload rejected a legal mask and neighbor-index disagreement");
      }
      if (!legal) {
        continue;
      }
      const DeviceNodeV1& neighbor = board.nodes[neighbor_index];
      const geometry_compiler::DirectionDelta delta = geometry_compiler::DeltaFor(direction);
      if (neighbor.layer != node.layer ||
          static_cast<__int128_t>(neighbor.lattice_x) !=
              static_cast<__int128_t>(node.lattice_x) + delta.x ||
          static_cast<__int128_t>(neighbor.lattice_y) !=
              static_cast<__int128_t>(node.lattice_y) + delta.y) {
        return InvalidDeviceView("CUDA upload rejected a non-adjacent neighbor index");
      }
      const Direction opposite = geometry_compiler::Opposite(direction);
      if ((neighbor.legal_edges & geometry_compiler::MaskFor(opposite)) == 0 ||
          neighbor.neighbors[static_cast<std::size_t>(opposite)] != node_index) {
        return InvalidDeviceView("CUDA upload rejected an edge without its exact reverse edge");
      }
    }
  }

  if (board.run_nodes.size() >= kInvalidNodeIndex) {
    return InvalidDeviceView("CUDA upload rejected directional-run storage outside uint32 bounds");
  }
  std::vector<std::uint8_t> run_edge_masks;
  try {
    run_edge_masks.resize(board.nodes.size(), 0);
  } catch (const std::bad_alloc&) {
    return BackendError{.code = BackendErrorCode::kResourceExhausted,
                        .detail = "CUDA upload could not allocate run-validation scratch"};
  }
  std::uint64_t expected_run_node_offset = 0;
  std::uint8_t previous_direction = 0;
  std::uint32_t previous_start = 0;
  bool have_previous_run = false;
  for (const DeviceRunV1& run : board.runs) {
    const std::uint8_t direction_index = static_cast<std::uint8_t>(run.direction);
    const std::uint64_t run_end = static_cast<std::uint64_t>(run.node_offset) + run.node_count;
    if (direction_index >= geometry_compiler::kStableDirectionOrder.size() || run.node_count < 2 ||
        run.node_offset != expected_run_node_offset || run_end > board.run_nodes.size() ||
        std::ranges::any_of(run.reserved, [](std::uint8_t byte) { return byte != 0; })) {
      return InvalidDeviceView("CUDA upload rejected invalid directional-run bounds or direction");
    }
    const Direction direction = static_cast<Direction>(direction_index);
    if (!DirectionIsEnabled(header, direction)) {
      return InvalidDeviceView("CUDA upload rejected a run excluded by the heading mask");
    }
    const std::uint32_t first_node = board.run_nodes[run.node_offset];
    if (first_node >= board.nodes.size()) {
      return InvalidDeviceView("CUDA upload rejected an out-of-range run-node index");
    }
    if (have_previous_run &&
        (direction_index < previous_direction ||
         (direction_index == previous_direction && first_node <= previous_start))) {
      return InvalidDeviceView("CUDA upload rejected non-canonical directional-run ordering");
    }
    have_previous_run = true;
    previous_direction = direction_index;
    previous_start = first_node;
    const Direction opposite = geometry_compiler::Opposite(direction);
    const std::uint32_t before =
        board.nodes[first_node].neighbors[static_cast<std::size_t>(opposite)];
    if (before != kInvalidNodeIndex &&
        (board.nodes[before].legal_edges & geometry_compiler::MaskFor(direction)) != 0) {
      return InvalidDeviceView("CUDA upload rejected a non-maximal directional-run start");
    }
    for (std::uint32_t position = 0; position + 1 < run.node_count; ++position) {
      const std::uint32_t source = board.run_nodes[run.node_offset + position];
      const std::uint32_t target = board.run_nodes[run.node_offset + position + 1];
      if (source >= board.nodes.size() || target >= board.nodes.size() ||
          board.nodes[source].neighbors[direction_index] != target) {
        return InvalidDeviceView("CUDA upload rejected an invalid directed run edge");
      }
      const std::uint8_t mask = geometry_compiler::MaskFor(direction);
      if ((run_edge_masks[source] & mask) != 0) {
        return InvalidDeviceView("CUDA upload rejected a duplicated directed run edge");
      }
      run_edge_masks[source] |= mask;
    }
    const std::uint32_t final_node = board.run_nodes[run.node_offset + run.node_count - 1];
    if (final_node >= board.nodes.size() ||
        (board.nodes[final_node].legal_edges & geometry_compiler::MaskFor(direction)) != 0) {
      return InvalidDeviceView("CUDA upload rejected a non-maximal directional-run end");
    }
    expected_run_node_offset = run_end;
  }
  if (expected_run_node_offset != board.run_nodes.size()) {
    return InvalidDeviceView("CUDA upload rejected unowned directional run-node storage");
  }
  for (std::size_t node = 0; node < board.nodes.size(); ++node) {
    if (run_edge_masks[node] != board.nodes[node].legal_edges) {
      return InvalidDeviceView("CUDA upload rejected incomplete directional-run edge coverage");
    }
  }

  const __uint128_t expected_bytes =
      sizeof(DeviceCompiledHeaderV1) +
      static_cast<__uint128_t>(board.layers.size()) * sizeof(DeviceLayerRangeV1) +
      static_cast<__uint128_t>(board.nodes.size()) * sizeof(DeviceNodeV1) +
      static_cast<__uint128_t>(board.runs.size()) * sizeof(DeviceRunV1) +
      static_cast<__uint128_t>(board.run_nodes.size()) * sizeof(std::uint32_t);
  if (expected_bytes > std::numeric_limits<std::uint64_t>::max() ||
      header.estimated_persistent_device_bytes != static_cast<std::uint64_t>(expected_bytes)) {
    return InvalidDeviceView("CUDA upload rejected inconsistent persistent-memory accounting");
  }
  if (header.device_view_fingerprint != ComputeDeviceCompiledBoardFingerprintV1(board)) {
    return InvalidDeviceView("CUDA upload rejected a stale device-view fingerprint");
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<BackendError> CheckLaunch(const char* operation) {
  const cudaError_t status = cudaGetLastError();
  if (status != cudaSuccess) {
    return CudaError(operation, status);
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<BackendError> CopyScalarToHost(void* destination, const void* source,
                                                           std::size_t bytes,
                                                           const char* operation) {
  const cudaError_t status = cudaMemcpy(destination, source, bytes, cudaMemcpyDeviceToHost);
  if (status != cudaSuccess) {
    return CudaError(operation, status);
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<BackendError> InitializeExecution(
    const CudaUploadedCompiledView& uploaded, CudaPendingRouteExecution& execution,
    std::uint32_t* active) {
  InitializeResultHeader<<<1, 1>>>(uploaded.device_header.get(), execution.result_header.get(),
                                   execution.request.start_node, execution.request.goal_node,
                                   execution.request.generator);
  if (std::optional<BackendError> error = CheckLaunch("InitializeResultHeader launch");
      error.has_value()) {
    return error;
  }
  const std::uint64_t state_count = uploaded.header.represented_states;
  InitializeSearch<<<BlockCount(state_count), kThreadsPerBlock>>>(
      execution.labels.get(), execution.predecessors.get(), active, state_count,
      StateIndex(execution.request.start_node, kNoIncomingHeading));
  return CheckLaunch("InitializeSearch launch");
}

[[nodiscard]] std::optional<BackendError> SelectPredecessors(
    const CudaUploadedCompiledView& uploaded, CudaPendingRouteExecution& execution) {
  SelectStablePredecessors<<<BlockCount(uploaded.header.represented_states), kThreadsPerBlock>>>(
      uploaded.nodes.get(), uploaded.header, execution.request.start_node, execution.labels.get(),
      execution.predecessors.get());
  return CheckLaunch("SelectStablePredecessors launch");
}

[[nodiscard]] std::optional<BackendError> FinishExecution(const CudaUploadedCompiledView& uploaded,
                                                          CudaPendingRouteExecution& execution) {
  if (std::optional<BackendError> error = SelectPredecessors(uploaded, execution);
      error.has_value()) {
    return error;
  }
  if (execution.request.cancellation != nullptr && execution.request.cancellation->load()) {
    execution.completion = KernelCompletion::kCancelled;
  }
  return std::nullopt;
}

[[nodiscard]] bool CancellationRequested(CudaPendingRouteExecution& execution) {
  if (execution.request.cancellation == nullptr || !execution.request.cancellation->load()) {
    return false;
  }
  execution.completion = KernelCompletion::kCancelled;
  return true;
}

[[nodiscard]] std::optional<BackendError> DetermineCompletion(CudaPendingRouteExecution& execution,
                                                              bool converged) {
  if (!converged) {
    return std::nullopt;
  }
  std::array<std::uint64_t, 8> goal_labels{};
  const std::uint32_t first_goal_state = StateIndex(execution.request.goal_node, 0);
  if (std::optional<BackendError> error =
          CopyScalarToHost(goal_labels.data(), execution.labels.get() + first_goal_state,
                           goal_labels.size() * sizeof(std::uint64_t), "cudaMemcpy(goal labels)");
      error.has_value()) {
    return error;
  }
  execution.completion =
      std::ranges::all_of(goal_labels,
                          [](std::uint64_t label) { return label == kInfiniteRouteCost; })
          ? KernelCompletion::kDisconnected
          : KernelCompletion::kReached;
  return std::nullopt;
}

[[nodiscard]] std::optional<BackendError> CompleteBoundedSearch(
    const CudaUploadedCompiledView& uploaded, CudaPendingRouteExecution& execution,
    bool converged) {
  if (execution.completion != KernelCompletion::kCancelled) {
    execution.completion =
        converged ? KernelCompletion::kDisconnected : KernelCompletion::kBudgetExhausted;
  }
  if (std::optional<BackendError> error = DetermineCompletion(execution, converged);
      error.has_value()) {
    return error;
  }
  return FinishExecution(uploaded, execution);
}

[[nodiscard]] std::optional<BackendError> RunFrontier(const CudaUploadedCompiledView& uploaded,
                                                      CudaPendingRouteExecution& execution,
                                                      std::uint64_t* batch_bytes) {
  const std::uint64_t state_count = uploaded.header.represented_states;
  DeviceBuffer<std::uint32_t> first_active;
  DeviceBuffer<std::uint32_t> second_active;
  DeviceBuffer<FrontierRoundStats> stats;
  if (std::optional<BackendError> error =
          first_active.Allocate(state_count, "cudaMalloc(frontier current)");
      error.has_value()) {
    return error;
  }
  if (std::optional<BackendError> error =
          second_active.Allocate(state_count, "cudaMalloc(frontier next)");
      error.has_value()) {
    return error;
  }
  if (std::optional<BackendError> error = stats.Allocate(1, "cudaMalloc(frontier round stats)");
      error.has_value()) {
    return error;
  }
  *batch_bytes += first_active.bytes() + second_active.bytes() + stats.bytes();
  if (std::optional<BackendError> error =
          InitializeExecution(uploaded, execution, first_active.get());
      error.has_value()) {
    return error;
  }
  if (const cudaError_t status =
          cudaMemset(second_active.get(), 0, state_count * sizeof(std::uint32_t));
      status != cudaSuccess) {
    return CudaError("cudaMemset(frontier next initial)", status);
  }

  std::uint32_t* current = first_active.get();
  std::uint32_t* next = second_active.get();
  std::uint32_t current_generation = 1;
  std::uint32_t next_generation = 1;
  std::uint64_t bucket_upper = 0;
  const std::uint64_t delta = std::min<std::uint64_t>(uploaded.header.costs.orthogonal_step,
                                                      uploaded.header.costs.diagonal_step);
  bool converged = false;
  for (std::uint32_t round = 0; round < execution.request.maximum_rounds; ++round) {
    if (CancellationRequested(execution)) {
      break;
    }
    if (const cudaError_t status =
            cudaMemset(&stats.get()->minimum_next_label, 0xff, sizeof(std::uint64_t));
        status != cudaSuccess) {
      return CudaError("cudaMemset(frontier minimum)", status);
    }
    if (const cudaError_t status =
            cudaMemset(&stats.get()->examined_work, 0, sizeof(std::uint64_t));
        status != cudaSuccess) {
      return CudaError("cudaMemset(frontier work)", status);
    }
    FrontierRelax<<<BlockCount(state_count), kThreadsPerBlock>>>(
        uploaded.nodes.get(), uploaded.header, current, next, current_generation, next_generation,
        execution.labels.get(), bucket_upper, stats.get());
    if (std::optional<BackendError> error = CheckLaunch("FrontierRelax launch");
        error.has_value()) {
      return error;
    }
    FrontierRoundStats round_stats{};
    if (std::optional<BackendError> error = CopyScalarToHost(
            &round_stats, stats.get(), sizeof(round_stats), "cudaMemcpy(frontier round stats)");
        error.has_value()) {
      return error;
    }
    execution.telemetry.examined_work += round_stats.examined_work;
    execution.telemetry.rounds = round + 1;
    // The blocking stats readback is the kernel-batch boundary. Resample
    // cancellation before classifying convergence or budget exhaustion.
    if (CancellationRequested(execution)) {
      break;
    }
    if (round_stats.minimum_next_label == kInfiniteRouteCost) {
      converged = true;
      break;
    }
    const std::uint64_t bucket_base = (round_stats.minimum_next_label / delta) * delta;
    bucket_upper = bucket_base > kInfiniteRouteCost - (delta - 1) ? kInfiniteRouteCost
                                                                  : bucket_base + delta - 1;
    std::swap(current, next);
    const std::uint32_t old_current_generation = current_generation;
    current_generation = next_generation;
    next_generation = old_current_generation + 1;
  }
  return CompleteBoundedSearch(uploaded, execution, converged);
}

[[nodiscard]] std::optional<BackendError> RunSweep(const CudaUploadedCompiledView& uploaded,
                                                   CudaPendingRouteExecution& execution,
                                                   std::uint64_t* batch_bytes) {
  DeviceBuffer<std::uint64_t> departures;
  DeviceBuffer<std::uint64_t> turns;
  DeviceBuffer<std::uint64_t> round_stats;
  const std::uint64_t departure_count = uploaded.header.represented_nodes * 8;
  if (std::optional<BackendError> error =
          departures.Allocate(departure_count, "cudaMalloc(sweep departures)");
      error.has_value()) {
    return error;
  }
  if (std::optional<BackendError> error = turns.Allocate(1, "cudaMalloc(sweep turns)");
      error.has_value()) {
    return error;
  }
  if (std::optional<BackendError> error = round_stats.Allocate(1, "cudaMalloc(sweep round stats)");
      error.has_value()) {
    return error;
  }
  *batch_bytes += departures.bytes() + turns.bytes() + round_stats.bytes();
  if (std::optional<BackendError> error = InitializeExecution(uploaded, execution, nullptr);
      error.has_value()) {
    return error;
  }
  if (const cudaError_t status = cudaMemset(turns.get(), 0, sizeof(std::uint64_t));
      status != cudaSuccess) {
    return CudaError("cudaMemset(sweep turns)", status);
  }

  bool converged = false;
  for (std::uint32_t round = 0; round < execution.request.maximum_rounds; ++round) {
    if (CancellationRequested(execution)) {
      break;
    }
    if (const cudaError_t status = cudaMemset(round_stats.get(), 0, sizeof(std::uint64_t));
        status != cudaSuccess) {
      return CudaError("cudaMemset(sweep round stats)", status);
    }
    ComputeDepartureLabels<<<BlockCount(departure_count), kThreadsPerBlock>>>(
        uploaded.header, execution.request.start_node, execution.labels.get(), departures.get(),
        turns.get());
    if (std::optional<BackendError> error = CheckLaunch("ComputeDepartureLabels launch");
        error.has_value()) {
      return error;
    }
    if (uploaded.runs.count() != 0) {
      SweepRuns<<<BlockCount(uploaded.runs.count()), kThreadsPerBlock>>>(
          uploaded.runs.get(), uploaded.runs.count(), uploaded.run_nodes.get(), uploaded.header,
          departures.get(), execution.labels.get(), round_stats.get());
      if (std::optional<BackendError> error = CheckLaunch("SweepRuns launch"); error.has_value()) {
        return error;
      }
    }
    std::uint64_t packed_round_stats = 0;
    if (std::optional<BackendError> error =
            CopyScalarToHost(&packed_round_stats, round_stats.get(), sizeof(packed_round_stats),
                             "cudaMemcpy(sweep round stats)");
        error.has_value()) {
      return error;
    }
    execution.telemetry.examined_work += packed_round_stats & kSweepWorkMask;
    execution.telemetry.rounds = round + 1;
    // The blocking stats readback is the kernel-batch boundary. Resample
    // cancellation before classifying convergence or budget exhaustion.
    if (CancellationRequested(execution)) {
      break;
    }
    if ((packed_round_stats & kSweepChangedBit) == 0) {
      converged = true;
      break;
    }
  }
  if (std::optional<BackendError> error =
          CopyScalarToHost(&execution.telemetry.heading_turn_relaxations, turns.get(),
                           sizeof(std::uint64_t), "cudaMemcpy(sweep turns)");
      error.has_value()) {
    return error;
  }
  return CompleteBoundedSearch(uploaded, execution, converged);
}

class CudaPlanarRouteBackend final : public IPlanarRouteBackend {
 public:
  [[nodiscard]] BackendMetadataResult QueryMetadata() const override {
    int device = 0;
    cudaError_t status = cudaGetDevice(&device);
    if (status != cudaSuccess) {
      return CudaError("cudaGetDevice", status);
    }
    {
      static std::mutex cache_mutex;
      static std::map<int, BackendMetadata> cache;
      std::scoped_lock lock(cache_mutex);
      const auto cached = cache.find(device);
      if (cached != cache.end()) {
        return cached->second;
      }

      BackendMetadataResult metadata = QueryMetadataForDevice(device);
      if (const auto* value = std::get_if<BackendMetadata>(&metadata); value != nullptr) {
        cache.emplace(device, *value);
      }
      return metadata;
    }
  }

  [[nodiscard]] UploadResult UploadCompiledView(const DeviceCompiledBoardV1& board) override {
    if (std::optional<BackendError> error = ValidateDeviceViewForUpload(board); error.has_value()) {
      return std::move(*error);
    }
    int device = -1;
    if (const cudaError_t status = cudaGetDevice(&device); status != cudaSuccess) {
      return CudaError("cudaGetDevice(upload)", status);
    }
    auto uploaded = std::make_unique<CudaUploadedCompiledView>();
    uploaded->device = device;
    uploaded->header = board.header;
    if (std::optional<BackendError> error =
            AllocateAndUpload(uploaded->device_header, &board.header, 1,
                              "cudaMalloc(device header)", "cudaMemcpy(device header)");
        error.has_value()) {
      return std::move(*error);
    }
    if (std::optional<BackendError> error =
            AllocateAndUpload(uploaded->layers, board.layers.data(), board.layers.size(),
                              "cudaMalloc(device layers)", "cudaMemcpy(device layers)");
        error.has_value()) {
      return std::move(*error);
    }
    if (std::optional<BackendError> error =
            AllocateAndUpload(uploaded->nodes, board.nodes.data(), board.nodes.size(),
                              "cudaMalloc(device nodes)", "cudaMemcpy(device nodes)");
        error.has_value()) {
      return std::move(*error);
    }
    if (std::optional<BackendError> error =
            AllocateAndUpload(uploaded->runs, board.runs.data(), board.runs.size(),
                              "cudaMalloc(device runs)", "cudaMemcpy(device runs)");
        error.has_value()) {
      return std::move(*error);
    }
    if (std::optional<BackendError> error =
            AllocateAndUpload(uploaded->run_nodes, board.run_nodes.data(), board.run_nodes.size(),
                              "cudaMalloc(device run nodes)", "cudaMemcpy(device run nodes)");
        error.has_value()) {
      return std::move(*error);
    }
    const std::uint64_t actual_bytes = uploaded->device_header.bytes() + uploaded->layers.bytes() +
                                       uploaded->nodes.bytes() + uploaded->runs.bytes() +
                                       uploaded->run_nodes.bytes();
    if (actual_bytes != board.header.estimated_persistent_device_bytes) {
      return BackendError{
          .code = BackendErrorCode::kInternalInvariant,
          .detail = "CUDA upload allocation bytes disagree with deterministic memory accounting",
      };
    }
    return std::unique_ptr<UploadedCompiledView>(std::move(uploaded));
  }

 private:
  [[nodiscard]] static BackendMetadataResult QueryMetadataForDevice(int device) {
    int count = 0;
    cudaError_t status = cudaGetDeviceCount(&count);
    if (status != cudaSuccess) {
      return CudaError("cudaGetDeviceCount", status);
    }
    if (count < 1 || device < 0 || device >= count) {
      return BackendError{.code = BackendErrorCode::kBackendFailure,
                          .detail = "No current CUDA device is available"};
    }
    cudaDeviceProp properties{};
    status = cudaGetDeviceProperties(&properties, device);
    if (status != cudaSuccess) {
      return CudaError("cudaGetDeviceProperties", status);
    }
    int runtime_version = 0;
    int driver_version = 0;
    status = cudaRuntimeGetVersion(&runtime_version);
    if (status != cudaSuccess) {
      return CudaError("cudaRuntimeGetVersion", status);
    }
    status = cudaDriverGetVersion(&driver_version);
    if (status != cudaSuccess) {
      return CudaError("cudaDriverGetVersion", status);
    }
    std::ostringstream uuid;
    uuid << std::hex << std::setfill('0');
    for (char byte : properties.uuid.bytes) {
      uuid << std::setw(2) << static_cast<unsigned int>(static_cast<unsigned char>(byte));
    }
    return BackendMetadata{
        .backend = "cuda",
        .device_name = properties.name,
        .device_uuid = uuid.str(),
        .compute_capability_major = static_cast<std::uint32_t>(properties.major),
        .compute_capability_minor = static_cast<std::uint32_t>(properties.minor),
        .runtime_version = static_cast<std::uint32_t>(runtime_version),
        .driver_version = static_cast<std::uint32_t>(driver_version),
        .global_memory_bytes = properties.totalGlobalMem,
    };
  }

 public:
  [[nodiscard]] ExecutionResult ExecuteRoute(const UploadedCompiledView& board,
                                             const BackendExecutionRequest& request) override {
    const auto* uploaded = dynamic_cast<const CudaUploadedCompiledView*>(&board);
    if (uploaded == nullptr) {
      return BackendError{.code = BackendErrorCode::kInternalInvariant,
                          .detail = "CUDA execute received a view from another backend"};
    }
    int current_device = -1;
    if (const cudaError_t status = cudaGetDevice(&current_device); status != cudaSuccess) {
      return CudaError("cudaGetDevice(execute)", status);
    }
    if (current_device != uploaded->device) {
      return BackendError{.code = BackendErrorCode::kBackendFailure,
                          .detail = "CUDA execute current device differs from upload device"};
    }
    if (request.start_node >= uploaded->header.represented_nodes ||
        request.goal_node >= uploaded->header.represented_nodes || request.maximum_rounds == 0) {
      return BackendError{.code = BackendErrorCode::kInternalInvariant,
                          .detail = "CUDA execute received invalid endpoint or budget bounds"};
    }
    if (request.start_node == request.goal_node) {
      return BackendError{
          .code = BackendErrorCode::kInternalInvariant,
          .detail = "CUDA execute requires distinct start and goal nodes",
      };
    }
    if (request.generator != PlanarGenerator::kBucketedFrontier &&
        request.generator != PlanarGenerator::kHeadingAwareSweep) {
      return BackendError{.code = BackendErrorCode::kInternalInvariant,
                          .detail = "CUDA execute received an unknown generator"};
    }

    auto execution = std::make_unique<CudaPendingRouteExecution>();
    execution->device = current_device;
    execution->request = request;
    const std::uint64_t state_count = uploaded->header.represented_states;
    const std::uint64_t base_batch_bytes = sizeof(DeviceResultHeaderV1) +
                                           state_count * sizeof(std::uint64_t) +
                                           state_count * sizeof(std::uint32_t);
    const std::uint64_t algorithm_bytes =
        request.generator == PlanarGenerator::kBucketedFrontier
            ? state_count * 2 * sizeof(std::uint32_t) + 2 * sizeof(std::uint64_t)
            : uploaded->header.represented_nodes * 8 * sizeof(std::uint64_t) +
                  2 * sizeof(std::uint64_t);
    const std::uint64_t batch_budget = base_batch_bytes + algorithm_bytes;
    if (uploaded->header.estimated_persistent_device_bytes > request.maximum_device_bytes ||
        batch_budget >
            request.maximum_device_bytes - uploaded->header.estimated_persistent_device_bytes) {
      return BackendError{.code = BackendErrorCode::kResourceExhausted,
                          .detail = "CUDA route exceeds the deterministic device-memory budget"};
    }
    if (std::optional<BackendError> error =
            execution->result_header.Allocate(1, "cudaMalloc(route result header)");
        error.has_value()) {
      return std::move(*error);
    }
    if (std::optional<BackendError> error =
            execution->labels.Allocate(state_count, "cudaMalloc(route labels)");
        error.has_value()) {
      return std::move(*error);
    }
    if (std::optional<BackendError> error =
            execution->predecessors.Allocate(state_count, "cudaMalloc(route predecessors)");
        error.has_value()) {
      return std::move(*error);
    }

    cudaEvent_t start_event = nullptr;
    cudaEvent_t stop_event = nullptr;
    cudaError_t status = cudaEventCreate(&start_event);
    if (status != cudaSuccess) {
      return CudaError("cudaEventCreate(start)", status);
    }
    status = cudaEventCreate(&stop_event);
    if (status != cudaSuccess) {
      cudaEventDestroy(start_event);
      return CudaError("cudaEventCreate(stop)", status);
    }
    status = cudaEventRecord(start_event);
    if (status != cudaSuccess) {
      cudaEventDestroy(stop_event);
      cudaEventDestroy(start_event);
      return CudaError("cudaEventRecord(start)", status);
    }

    std::uint64_t batch_bytes = execution->result_header.bytes() + execution->labels.bytes() +
                                execution->predecessors.bytes();
    std::optional<BackendError> run_error;
    switch (request.generator) {
      case PlanarGenerator::kBucketedFrontier:
        run_error = RunFrontier(*uploaded, *execution, &batch_bytes);
        break;
      case PlanarGenerator::kHeadingAwareSweep:
        run_error = RunSweep(*uploaded, *execution, &batch_bytes);
        break;
    }
    if (!run_error.has_value()) {
      status = cudaEventRecord(stop_event);
      if (status == cudaSuccess) {
        status = cudaEventSynchronize(stop_event);
      }
      float elapsed = 0.0F;
      if (status == cudaSuccess) {
        status = cudaEventElapsedTime(&elapsed, start_event, stop_event);
      }
      if (status != cudaSuccess) {
        run_error = CudaError("CUDA kernel timing", status);
      } else {
        execution->telemetry.kernel_milliseconds = elapsed;
      }
    }
    cudaEventDestroy(stop_event);
    cudaEventDestroy(start_event);
    if (run_error.has_value()) {
      return std::move(*run_error);
    }
    if (request.cancellation != nullptr && request.cancellation->load()) {
      execution->completion = KernelCompletion::kCancelled;
    }

    execution->telemetry.persistent_device_bytes =
        uploaded->header.estimated_persistent_device_bytes;
    execution->telemetry.batch_device_bytes = batch_bytes;
    execution->telemetry.peak_device_bytes =
        execution->telemetry.persistent_device_bytes + batch_bytes;
    return std::unique_ptr<PendingRouteExecution>(std::move(execution));
  }

  [[nodiscard]] ReadbackResult ReadbackRoute(const PendingRouteExecution& pending) override {
    const auto* execution = dynamic_cast<const CudaPendingRouteExecution*>(&pending);
    if (execution == nullptr) {
      return BackendError{.code = BackendErrorCode::kInternalInvariant,
                          .detail = "CUDA readback received a job from another backend"};
    }
    int current_device = -1;
    if (const cudaError_t device_status = cudaGetDevice(&current_device);
        device_status != cudaSuccess) {
      return CudaError("cudaGetDevice(readback)", device_status);
    }
    if (current_device != execution->device) {
      return BackendError{.code = BackendErrorCode::kBackendFailure,
                          .detail = "CUDA readback current device differs from execution device"};
    }
    DeviceResultHeaderV1 result_header;
    cudaError_t status = cudaMemcpy(&result_header, execution->result_header.get(),
                                    sizeof(result_header), cudaMemcpyDeviceToHost);
    if (status != cudaSuccess) {
      return CudaError("cudaMemcpy(route result header)", status);
    }
    UntrustedKernelResult result{
        .schema_version = result_header.schema_version,
        .source_board_content_hash = result_header.source_board_content_hash,
        .compiler_profile_fingerprint = result_header.compiler_profile_fingerprint,
        .compiler_version = result_header.compiler_version,
        .rule_bucket_identity = result_header.rule_bucket_identity,
        .device_view_fingerprint = result_header.device_view_fingerprint,
        .generator = result_header.generator,
        .completion = execution->completion,
        .start_node = result_header.start_node,
        .goal_node = result_header.goal_node,
        .telemetry = execution->telemetry,
    };
    result.labels.resize(execution->labels.count());
    result.predecessors.resize(execution->predecessors.count());
    status = cudaMemcpy(result.labels.data(), execution->labels.get(), execution->labels.bytes(),
                        cudaMemcpyDeviceToHost);
    if (status != cudaSuccess) {
      return CudaError("cudaMemcpy(route labels)", status);
    }
    status = cudaMemcpy(result.predecessors.data(), execution->predecessors.get(),
                        execution->predecessors.bytes(), cudaMemcpyDeviceToHost);
    if (status != cudaSuccess) {
      return CudaError("cudaMemcpy(route predecessors)", status);
    }
    if (result.completion == KernelCompletion::kReached) {
      std::uint64_t best_cost = kInfiniteRouteCost;
      for (std::uint8_t heading = 0; heading < 8; ++heading) {
        const std::uint32_t state = StateIndex(result.goal_node, heading);
        if (result.labels[state] < best_cost ||
            (result.labels[state] == best_cost && state < result.goal_state)) {
          best_cost = result.labels[state];
          result.goal_state = state;
        }
      }
    }
    return result;
  }
};

}  // namespace

std::unique_ptr<IPlanarRouteBackend> CreateCudaPlanarRouteBackend() {
  return std::make_unique<CudaPlanarRouteBackend>();
}

}  // namespace apgar::gpu
