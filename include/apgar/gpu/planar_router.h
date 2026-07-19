#ifndef APGAR_GPU_PLANAR_ROUTER_H_
#define APGAR_GPU_PLANAR_ROUTER_H_

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "apgar/board_ir/board.h"
#include "apgar/geometry_compiler/compiled_board.h"
#include "apgar/routing/cpu_astar.h"

namespace apgar::gpu {

inline constexpr std::uint32_t kDeviceCompiledBoardSchemaVersion = 1;
inline constexpr std::uint8_t kIncomingHeadingCount = 9;
inline constexpr std::uint8_t kNoIncomingHeading = 8;
inline constexpr std::uint32_t kInvalidNodeIndex = std::numeric_limits<std::uint32_t>::max();
inline constexpr std::uint32_t kInvalidStateIndex = std::numeric_limits<std::uint32_t>::max();
inline constexpr std::uint64_t kInfiniteRouteCost = std::numeric_limits<std::uint64_t>::max();

enum class PlanarGenerator : std::uint8_t {
  kBucketedFrontier,
  kHeadingAwareSweep,
};

enum class PlanarGpuFailureCode : std::uint8_t {
  kInvalidInput,
  kUnsupported,
  kDisconnected,
  kResourceExhausted,
  kCancelled,
  kBackendFailure,
  kValidationFailed,
  kInternalInvariant,
};

struct KernelTelemetry {
  std::uint64_t persistent_device_bytes = 0;
  std::uint64_t batch_device_bytes = 0;
  std::uint64_t peak_device_bytes = 0;
  std::uint64_t examined_work = 0;
  std::uint64_t heading_turn_relaxations = 0;
  std::uint32_t rounds = 0;
  double kernel_milliseconds = 0.0;

  friend bool operator==(const KernelTelemetry&, const KernelTelemetry&) = default;
};

struct PlanarGpuFailure {
  PlanarGpuFailureCode code;
  std::string detail;
  std::optional<board_ir::EntityRef> obstacle;
  std::optional<KernelTelemetry> telemetry;

  friend bool operator==(const PlanarGpuFailure&, const PlanarGpuFailure&) = default;
};

struct DeviceCompiledHeaderV1 {
  std::uint32_t schema_version = kDeviceCompiledBoardSchemaVersion;
  std::uint32_t compiler_version = 0;
  std::uint64_t source_board_content_hash = 0;
  std::uint64_t compiler_profile_fingerprint = 0;
  std::uint64_t rule_bucket_identity = 0;
  std::uint64_t device_view_fingerprint = 0;
  std::uint64_t represented_nodes = 0;
  std::uint64_t represented_states = 0;
  std::uint64_t estimated_persistent_device_bytes = 0;
  board_ir::Point64 lattice_origin{};
  board_ir::DbCoord lattice_step = 0;
  geometry_compiler::DeterministicCosts costs{};
  board_ir::HeadingMask heading_mask = 0;
  std::array<std::uint8_t, 7> reserved{};

  friend bool operator==(const DeviceCompiledHeaderV1&, const DeviceCompiledHeaderV1&) = default;
};
static_assert(sizeof(DeviceCompiledHeaderV1) == 112);

struct DeviceLayerRangeV1 {
  board_ir::LayerId layer = 0;
  std::uint32_t node_offset = 0;
  std::uint32_t node_count = 0;
  std::int64_t minimum_lattice_x = 0;
  std::int64_t maximum_lattice_x = 0;
  std::int64_t minimum_lattice_y = 0;
  std::int64_t maximum_lattice_y = 0;

  friend bool operator==(const DeviceLayerRangeV1&, const DeviceLayerRangeV1&) = default;
};
static_assert(sizeof(DeviceLayerRangeV1) == 48);

struct DeviceNodeV1 {
  std::int64_t lattice_x = 0;
  std::int64_t lattice_y = 0;
  board_ir::LayerId layer = 0;
  geometry_compiler::DirectionMask legal_edges = 0;
  std::array<std::uint8_t, 3> reserved{};
  std::array<std::uint32_t, 8> neighbors{};

  friend bool operator==(const DeviceNodeV1&, const DeviceNodeV1&) = default;
};
static_assert(sizeof(DeviceNodeV1) == 56);

struct DeviceRunV1 {
  std::uint32_t node_offset = 0;
  std::uint32_t node_count = 0;
  geometry_compiler::Direction direction = geometry_compiler::Direction::kEast;
  std::array<std::uint8_t, 3> reserved{};

  friend bool operator==(const DeviceRunV1&, const DeviceRunV1&) = default;
};
static_assert(sizeof(DeviceRunV1) == 12);

struct DeviceCompiledBoardV1 {
  DeviceCompiledHeaderV1 header;
  std::vector<DeviceLayerRangeV1> layers;
  std::vector<DeviceNodeV1> nodes;
  std::vector<DeviceRunV1> runs;
  std::vector<std::uint32_t> run_nodes;

  friend bool operator==(const DeviceCompiledBoardV1&, const DeviceCompiledBoardV1&) = default;
};

using DeviceCompiledBoardResult = std::variant<DeviceCompiledBoardV1, PlanarGpuFailure>;

[[nodiscard]] DeviceCompiledBoardResult BuildDeviceCompiledBoardV1(
    const board_ir::BoardSnapshot& board, const geometry_compiler::CompiledBoard& compiled_board);

[[nodiscard]] constexpr std::uint32_t StateIndex(std::uint32_t node_index,
                                                 std::uint8_t incoming_heading) noexcept {
  return node_index * kIncomingHeadingCount + incoming_heading;
}

[[nodiscard]] constexpr std::uint32_t NodeIndexForState(std::uint32_t state_index) noexcept {
  return state_index / kIncomingHeadingCount;
}

[[nodiscard]] constexpr std::uint8_t IncomingHeadingForState(std::uint32_t state_index) noexcept {
  return static_cast<std::uint8_t>(state_index % kIncomingHeadingCount);
}

[[nodiscard]] const DeviceNodeV1* FindDeviceNode(const DeviceCompiledBoardV1& board,
                                                 board_ir::LayerId layer,
                                                 geometry_compiler::LatticeIndex index) noexcept;
[[nodiscard]] std::optional<std::uint32_t> FindDeviceNodeIndex(
    const DeviceCompiledBoardV1& board, board_ir::LayerId layer,
    geometry_compiler::LatticeIndex index) noexcept;

struct BackendMetadata {
  std::string backend;
  std::string device_name;
  std::string device_uuid;
  std::uint32_t compute_capability_major = 0;
  std::uint32_t compute_capability_minor = 0;
  std::uint32_t runtime_version = 0;
  std::uint32_t driver_version = 0;
  std::uint64_t global_memory_bytes = 0;

  friend bool operator==(const BackendMetadata&, const BackendMetadata&) = default;
};

enum class KernelCompletion : std::uint8_t {
  kReached,
  kDisconnected,
  kBudgetExhausted,
  kCancelled,
};

struct UntrustedKernelResult {
  std::uint32_t schema_version = kDeviceCompiledBoardSchemaVersion;
  std::uint64_t source_board_content_hash = 0;
  std::uint64_t compiler_profile_fingerprint = 0;
  std::uint32_t compiler_version = 0;
  std::uint64_t rule_bucket_identity = 0;
  std::uint64_t device_view_fingerprint = 0;
  KernelCompletion completion = KernelCompletion::kDisconnected;
  std::uint32_t start_node = kInvalidNodeIndex;
  std::uint32_t goal_node = kInvalidNodeIndex;
  std::uint32_t goal_state = kInvalidStateIndex;
  std::vector<std::uint64_t> labels;
  std::vector<std::uint32_t> predecessors;
  KernelTelemetry telemetry;
};

enum class KernelFaultInjection : std::uint8_t {
  kNone,
  kGoalPredecessorSelfCycle,
};

struct PlanarRoutePolicy {
  PlanarGenerator generator = PlanarGenerator::kBucketedFrontier;
  std::uint32_t maximum_rounds = 100'000;
  std::uint64_t maximum_device_bytes = std::numeric_limits<std::uint64_t>::max();
  const std::atomic_bool* cancellation = nullptr;
  // Test/replay-only deterministic corruption. Production callers leave this
  // at kNone.
  KernelFaultInjection fault_injection = KernelFaultInjection::kNone;
};

struct BackendExecutionRequest {
  PlanarGenerator generator;
  std::uint32_t start_node;
  std::uint32_t goal_node;
  std::uint32_t maximum_rounds;
  std::uint64_t maximum_device_bytes;
  const std::atomic_bool* cancellation;
  KernelFaultInjection fault_injection;
};

enum class BackendErrorCode : std::uint8_t {
  kResourceExhausted,
  kBackendFailure,
  kInternalInvariant,
};

struct BackendError {
  BackendErrorCode code;
  std::string detail;
};

class UploadedCompiledView {
 public:
  virtual ~UploadedCompiledView() = default;
};

class PendingRouteExecution {
 public:
  virtual ~PendingRouteExecution() = default;
};

using BackendMetadataResult = std::variant<BackendMetadata, BackendError>;
using UploadResult = std::variant<std::unique_ptr<UploadedCompiledView>, BackendError>;
using ExecutionResult = std::variant<std::unique_ptr<PendingRouteExecution>, BackendError>;
using ReadbackResult = std::variant<UntrustedKernelResult, BackendError>;

// Semantic backend seam: implementations upload an immutable compiled view,
// execute one bounded route job, and read back an explicitly untrusted result.
// CUDA allocation, streams, events, and API handles do not cross this boundary.
class IPlanarRouteBackend {
 public:
  virtual ~IPlanarRouteBackend() = default;

  [[nodiscard]] virtual BackendMetadataResult QueryMetadata() const = 0;
  [[nodiscard]] virtual UploadResult UploadCompiledView(const DeviceCompiledBoardV1& board) = 0;
  [[nodiscard]] virtual ExecutionResult ExecuteRoute(const UploadedCompiledView& board,
                                                     const BackendExecutionRequest& request) = 0;
  [[nodiscard]] virtual ReadbackResult ReadbackRoute(const PendingRouteExecution& execution) = 0;
};

struct PlanarGpuRoute {
  std::uint64_t source_board_content_hash = 0;
  std::uint64_t compiler_profile_fingerprint = 0;
  std::uint32_t compiler_version = 0;
  std::uint64_t rule_bucket_identity = 0;
  std::uint64_t device_view_fingerprint = 0;
  PlanarGenerator generator = PlanarGenerator::kBucketedFrontier;
  BackendMetadata backend;
  std::uint64_t total_cost = 0;
  std::vector<board_ir::Point64> lattice_path;
  std::vector<routing::LayerSegment> segments;
  KernelTelemetry telemetry;
};

using PlanarGpuRouteResult = std::variant<PlanarGpuRoute, PlanarGpuFailure>;

[[nodiscard]] PlanarGpuRouteResult ValidateAndReconstructGpuRoute(
    const board_ir::BoardSnapshot& board, const geometry_compiler::CompiledBoard& compiled_board,
    const DeviceCompiledBoardV1& device_board, const routing::CpuRouteRequest& request,
    PlanarGenerator generator, const BackendMetadata& backend,
    const UntrustedKernelResult& untrusted);

[[nodiscard]] PlanarGpuRouteResult RouteWithPlanarGpuBackend(
    const board_ir::BoardSnapshot& board, const geometry_compiler::CompiledBoard& compiled_board,
    const routing::CpuRouteRequest& request, const PlanarRoutePolicy& policy,
    IPlanarRouteBackend& backend);

}  // namespace apgar::gpu

#endif  // APGAR_GPU_PLANAR_ROUTER_H_
