#include <cuda_runtime.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <sstream>
#include <string>
#include <utility>
#include <variant>

#include "apgar/gpu/cuda_backend.h"

namespace apgar::gpu {
namespace {

constexpr std::uint32_t kThreadsPerBlock = 256;

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
      : data_(std::exchange(other.data_, nullptr)), count_(std::exchange(other.count_, 0)) {}

  DeviceBuffer& operator=(DeviceBuffer&& other) noexcept {
    if (this != &other) {
      Reset();
      data_ = std::exchange(other.data_, nullptr);
      count_ = std::exchange(other.count_, 0);
    }
    return *this;
  }

  ~DeviceBuffer() { Reset(); }

  [[nodiscard]] std::optional<BackendError> Allocate(std::size_t count, const char* operation) {
    Reset();
    if (count == 0) {
      return std::nullopt;
    }
    const cudaError_t status = cudaMalloc(&data_, count * sizeof(T));
    if (status != cudaSuccess) {
      return CudaError(operation, status);
    }
    count_ = count;
    return std::nullopt;
  }

  void Reset() noexcept {
    if (data_ != nullptr) {
      cudaFree(data_);
    }
    data_ = nullptr;
    count_ = 0;
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
};

class CudaUploadedCompiledView final : public UploadedCompiledView {
 public:
  DeviceCompiledHeaderV1 header;
  DeviceBuffer<DeviceCompiledHeaderV1> device_header;
  DeviceBuffer<DeviceLayerRangeV1> layers;
  DeviceBuffer<DeviceNodeV1> nodes;
  DeviceBuffer<DeviceRunV1> runs;
  DeviceBuffer<std::uint32_t> run_nodes;
};

class CudaPendingRouteExecution final : public PendingRouteExecution {
 public:
  DeviceCompiledHeaderV1 header;
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
                              std::uint64_t* labels, std::uint64_t bucket_upper,
                              std::uint64_t* minimum_next_label, std::uint64_t* examined_work) {
  const std::uint64_t state = static_cast<std::uint64_t>(blockIdx.x) * blockDim.x + threadIdx.x;
  if (state >= header.represented_states || current_active[state] == 0) {
    return;
  }
  const std::uint64_t label = static_cast<std::uint64_t>(
      atomicAdd(reinterpret_cast<unsigned long long*>(&labels[state]), 0ULL));
  if (label == kInfiniteRouteCost) {
    return;
  }
  if (label > bucket_upper) {
    atomicExch(&next_active[state], 1U);
    atomicMin(reinterpret_cast<unsigned long long*>(minimum_next_label),
              static_cast<unsigned long long>(label));
    return;
  }

  const std::uint32_t node_index = static_cast<std::uint32_t>(state / kIncomingHeadingCount);
  const std::uint8_t incoming = static_cast<std::uint8_t>(state % kIncomingHeadingCount);
  const DeviceNodeV1 node = nodes[node_index];
  for (std::uint8_t direction = 0; direction < 8; ++direction) {
    if ((node.legal_edges & static_cast<std::uint8_t>(1U << direction)) == 0) {
      continue;
    }
    atomicAdd(reinterpret_cast<unsigned long long*>(examined_work), 1ULL);
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
      atomicExch(&next_active[target_state], 1U);
      atomicMin(reinterpret_cast<unsigned long long*>(minimum_next_label),
                static_cast<unsigned long long>(candidate));
    }
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
                          std::uint32_t* changed, std::uint64_t* examined_work) {
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
  for (std::uint32_t position = 0; position + 1 < run.node_count; ++position) {
    atomicAdd(reinterpret_cast<unsigned long long*>(examined_work), 1ULL);
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
        atomicExch(changed, 1U);
      }
      carry = min(candidate, departure_labels[static_cast<std::uint64_t>(target) * 8 + direction]);
    } else {
      carry = departure_labels[static_cast<std::uint64_t>(target) * 8 + direction];
    }
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

[[nodiscard]] std::optional<BackendError> RunFrontier(const CudaUploadedCompiledView& uploaded,
                                                      CudaPendingRouteExecution& execution,
                                                      std::uint64_t* batch_bytes) {
  const std::uint64_t state_count = uploaded.header.represented_states;
  DeviceBuffer<std::uint32_t> first_active;
  DeviceBuffer<std::uint32_t> second_active;
  DeviceBuffer<std::uint64_t> minimum_next;
  DeviceBuffer<std::uint64_t> examined;
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
  if (std::optional<BackendError> error = minimum_next.Allocate(1, "cudaMalloc(frontier minimum)");
      error.has_value()) {
    return error;
  }
  if (std::optional<BackendError> error = examined.Allocate(1, "cudaMalloc(frontier work)");
      error.has_value()) {
    return error;
  }
  *batch_bytes +=
      first_active.bytes() + second_active.bytes() + minimum_next.bytes() + examined.bytes();
  if (std::optional<BackendError> error =
          InitializeExecution(uploaded, execution, first_active.get());
      error.has_value()) {
    return error;
  }

  std::uint32_t* current = first_active.get();
  std::uint32_t* next = second_active.get();
  std::uint64_t bucket_upper = 0;
  const std::uint64_t delta = std::min<std::uint64_t>(uploaded.header.costs.orthogonal_step,
                                                      uploaded.header.costs.diagonal_step);
  bool converged = false;
  for (std::uint32_t round = 0; round < execution.request.maximum_rounds; ++round) {
    if (execution.request.cancellation != nullptr && execution.request.cancellation->load()) {
      execution.completion = KernelCompletion::kCancelled;
      break;
    }
    if (const cudaError_t status = cudaMemset(next, 0, state_count * sizeof(std::uint32_t));
        status != cudaSuccess) {
      return CudaError("cudaMemset(frontier next)", status);
    }
    if (const cudaError_t status = cudaMemset(minimum_next.get(), 0xff, sizeof(std::uint64_t));
        status != cudaSuccess) {
      return CudaError("cudaMemset(frontier minimum)", status);
    }
    if (const cudaError_t status = cudaMemset(examined.get(), 0, sizeof(std::uint64_t));
        status != cudaSuccess) {
      return CudaError("cudaMemset(frontier work)", status);
    }
    FrontierRelax<<<BlockCount(state_count), kThreadsPerBlock>>>(
        uploaded.nodes.get(), uploaded.header, current, next, execution.labels.get(), bucket_upper,
        minimum_next.get(), examined.get());
    if (std::optional<BackendError> error = CheckLaunch("FrontierRelax launch");
        error.has_value()) {
      return error;
    }
    std::uint64_t next_label = kInfiniteRouteCost;
    std::uint64_t round_work = 0;
    if (std::optional<BackendError> error = CopyScalarToHost(
            &next_label, minimum_next.get(), sizeof(next_label), "cudaMemcpy(frontier minimum)");
        error.has_value()) {
      return error;
    }
    if (std::optional<BackendError> error = CopyScalarToHost(
            &round_work, examined.get(), sizeof(round_work), "cudaMemcpy(frontier work)");
        error.has_value()) {
      return error;
    }
    execution.telemetry.examined_work += round_work;
    execution.telemetry.rounds = round + 1;
    if (execution.request.cancellation != nullptr && execution.request.cancellation->load()) {
      execution.completion = KernelCompletion::kCancelled;
      break;
    }
    if (next_label == kInfiniteRouteCost) {
      converged = true;
      break;
    }
    const std::uint64_t bucket_base = (next_label / delta) * delta;
    bucket_upper = bucket_base > kInfiniteRouteCost - (delta - 1) ? kInfiniteRouteCost
                                                                  : bucket_base + delta - 1;
    std::swap(current, next);
  }
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

[[nodiscard]] std::optional<BackendError> RunSweep(const CudaUploadedCompiledView& uploaded,
                                                   CudaPendingRouteExecution& execution,
                                                   std::uint64_t* batch_bytes) {
  DeviceBuffer<std::uint64_t> departures;
  DeviceBuffer<std::uint64_t> examined;
  DeviceBuffer<std::uint64_t> turns;
  DeviceBuffer<std::uint32_t> changed;
  const std::uint64_t departure_count = uploaded.header.represented_nodes * 8;
  if (std::optional<BackendError> error =
          departures.Allocate(departure_count, "cudaMalloc(sweep departures)");
      error.has_value()) {
    return error;
  }
  if (std::optional<BackendError> error = examined.Allocate(1, "cudaMalloc(sweep work)");
      error.has_value()) {
    return error;
  }
  if (std::optional<BackendError> error = turns.Allocate(1, "cudaMalloc(sweep turns)");
      error.has_value()) {
    return error;
  }
  if (std::optional<BackendError> error = changed.Allocate(1, "cudaMalloc(sweep changed)");
      error.has_value()) {
    return error;
  }
  *batch_bytes += departures.bytes() + examined.bytes() + turns.bytes() + changed.bytes();
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
    if (execution.request.cancellation != nullptr && execution.request.cancellation->load()) {
      execution.completion = KernelCompletion::kCancelled;
      break;
    }
    if (const cudaError_t status = cudaMemset(changed.get(), 0, sizeof(std::uint32_t));
        status != cudaSuccess) {
      return CudaError("cudaMemset(sweep changed)", status);
    }
    if (const cudaError_t status = cudaMemset(examined.get(), 0, sizeof(std::uint64_t));
        status != cudaSuccess) {
      return CudaError("cudaMemset(sweep work)", status);
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
          departures.get(), execution.labels.get(), changed.get(), examined.get());
      if (std::optional<BackendError> error = CheckLaunch("SweepRuns launch"); error.has_value()) {
        return error;
      }
    }
    std::uint32_t round_changed = 0;
    std::uint64_t round_work = 0;
    if (std::optional<BackendError> error = CopyScalarToHost(
            &round_changed, changed.get(), sizeof(round_changed), "cudaMemcpy(sweep changed)");
        error.has_value()) {
      return error;
    }
    if (std::optional<BackendError> error = CopyScalarToHost(
            &round_work, examined.get(), sizeof(round_work), "cudaMemcpy(sweep work)");
        error.has_value()) {
      return error;
    }
    execution.telemetry.examined_work += round_work;
    execution.telemetry.rounds = round + 1;
    if (execution.request.cancellation != nullptr && execution.request.cancellation->load()) {
      execution.completion = KernelCompletion::kCancelled;
      break;
    }
    if (round_changed == 0) {
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

class CudaPlanarRouteBackend final : public IPlanarRouteBackend {
 public:
  [[nodiscard]] BackendMetadataResult QueryMetadata() const override {
    int device = 0;
    cudaError_t status = cudaGetDevice(&device);
    if (status != cudaSuccess) {
      return CudaError("cudaGetDevice", status);
    }
    int count = 0;
    status = cudaGetDeviceCount(&count);
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

  [[nodiscard]] UploadResult UploadCompiledView(const DeviceCompiledBoardV1& board) override {
    if (board.header.schema_version != kDeviceCompiledBoardSchemaVersion ||
        board.header.represented_nodes != board.nodes.size() ||
        board.header.represented_states != board.nodes.size() * kIncomingHeadingCount) {
      return BackendError{.code = BackendErrorCode::kInternalInvariant,
                          .detail = "CUDA upload rejected an inconsistent device schema"};
    }
    auto uploaded = std::make_unique<CudaUploadedCompiledView>();
    uploaded->header = board.header;
    if (std::optional<BackendError> error =
            uploaded->device_header.Allocate(1, "cudaMalloc(device header)");
        error.has_value()) {
      return std::move(*error);
    }
    if (std::optional<BackendError> error =
            uploaded->layers.Allocate(board.layers.size(), "cudaMalloc(device layers)");
        error.has_value()) {
      return std::move(*error);
    }
    if (std::optional<BackendError> error =
            uploaded->nodes.Allocate(board.nodes.size(), "cudaMalloc(device nodes)");
        error.has_value()) {
      return std::move(*error);
    }
    if (std::optional<BackendError> error =
            uploaded->runs.Allocate(board.runs.size(), "cudaMalloc(device runs)");
        error.has_value()) {
      return std::move(*error);
    }
    if (std::optional<BackendError> error =
            uploaded->run_nodes.Allocate(board.run_nodes.size(), "cudaMalloc(device run nodes)");
        error.has_value()) {
      return std::move(*error);
    }
    if (std::optional<BackendError> error =
            CopyToDevice(uploaded->device_header.get(), &board.header, sizeof(board.header),
                         "cudaMemcpy(device header)");
        error.has_value()) {
      return std::move(*error);
    }
    if (std::optional<BackendError> error = CopyToDevice(
            uploaded->layers.get(), board.layers.data(),
            board.layers.size() * sizeof(DeviceLayerRangeV1), "cudaMemcpy(device layers)");
        error.has_value()) {
      return std::move(*error);
    }
    if (std::optional<BackendError> error =
            CopyToDevice(uploaded->nodes.get(), board.nodes.data(),
                         board.nodes.size() * sizeof(DeviceNodeV1), "cudaMemcpy(device nodes)");
        error.has_value()) {
      return std::move(*error);
    }
    if (std::optional<BackendError> error =
            CopyToDevice(uploaded->runs.get(), board.runs.data(),
                         board.runs.size() * sizeof(DeviceRunV1), "cudaMemcpy(device runs)");
        error.has_value()) {
      return std::move(*error);
    }
    if (std::optional<BackendError> error = CopyToDevice(
            uploaded->run_nodes.get(), board.run_nodes.data(),
            board.run_nodes.size() * sizeof(std::uint32_t), "cudaMemcpy(device run nodes)");
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

  [[nodiscard]] ExecutionResult ExecuteRoute(const UploadedCompiledView& board,
                                             const BackendExecutionRequest& request) override {
    const auto* uploaded = dynamic_cast<const CudaUploadedCompiledView*>(&board);
    if (uploaded == nullptr) {
      return BackendError{.code = BackendErrorCode::kInternalInvariant,
                          .detail = "CUDA execute received a view from another backend"};
    }
    if (request.start_node >= uploaded->header.represented_nodes ||
        request.goal_node >= uploaded->header.represented_nodes || request.maximum_rounds == 0) {
      return BackendError{.code = BackendErrorCode::kInternalInvariant,
                          .detail = "CUDA execute received invalid endpoint or budget bounds"};
    }
    if ((request.generator != PlanarGenerator::kBucketedFrontier &&
         request.generator != PlanarGenerator::kHeadingAwareSweep) ||
        (request.fault_injection != KernelFaultInjection::kNone &&
         request.fault_injection != KernelFaultInjection::kGoalPredecessorSelfCycle)) {
      return BackendError{.code = BackendErrorCode::kInternalInvariant,
                          .detail = "CUDA execute received an unknown generator or fault"};
    }

    auto execution = std::make_unique<CudaPendingRouteExecution>();
    execution->header = uploaded->header;
    execution->request = request;
    const std::uint64_t state_count = uploaded->header.represented_states;
    const std::uint64_t base_batch_bytes = sizeof(DeviceResultHeaderV1) +
                                           state_count * sizeof(std::uint64_t) +
                                           state_count * sizeof(std::uint32_t);
    const std::uint64_t algorithm_bytes =
        request.generator == PlanarGenerator::kBucketedFrontier
            ? state_count * 2 * sizeof(std::uint32_t) + 2 * sizeof(std::uint64_t)
            : uploaded->header.represented_nodes * 8 * sizeof(std::uint64_t) +
                  2 * sizeof(std::uint64_t) + sizeof(std::uint32_t);
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
      if (execution->request.fault_injection == KernelFaultInjection::kGoalPredecessorSelfCycle) {
        result.predecessors[result.goal_state] = result.goal_state;
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
