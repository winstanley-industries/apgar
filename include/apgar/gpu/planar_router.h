#ifndef APGAR_GPU_PLANAR_ROUTER_H_
#define APGAR_GPU_PLANAR_ROUTER_H_

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "apgar/board_ir/board.h"
#include "apgar/geometry_compiler/compiled_board.h"
#include "apgar/routing/candidate_policy.h"
#include "apgar/routing/cpu_astar.h"

namespace apgar::gpu {

inline constexpr std::uint32_t kDeviceCompiledBoardSchemaVersion = 1;
inline constexpr std::uint32_t kDeviceCandidateBatchSchemaVersion = 1;
inline constexpr std::uint8_t kIncomingHeadingCount = 9;
inline constexpr std::uint8_t kNoIncomingHeading = 8;
inline constexpr std::uint32_t kInvalidNodeIndex = std::numeric_limits<std::uint32_t>::max();
inline constexpr std::uint32_t kInvalidStateIndex = std::numeric_limits<std::uint32_t>::max();
inline constexpr std::uint64_t kInfiniteRouteCost = std::numeric_limits<std::uint64_t>::max();
inline constexpr std::uint64_t kBannedResourceAdjustment =
    std::numeric_limits<std::uint64_t>::max();
inline constexpr std::uint32_t kCandidateFrontierChunkRounds = 32;
inline constexpr std::uint32_t kCandidateSweepChunkRounds = 8;

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

// Shared execution telemetry is owned once by a candidate batch. It is not
// duplicated into each query result.
struct CandidateBatchTelemetry {
  std::uint64_t persistent_device_bytes = 0;
  std::uint64_t batch_device_bytes = 0;
  std::uint64_t peak_device_bytes = 0;
  std::uint64_t batch_host_bytes = 0;
  std::uint64_t kernel_launch_count = 0;
  std::uint64_t blocking_status_readback_count = 0;
  std::uint32_t dispatched_rounds = 0;
  std::uint32_t finalization_launch_count = 0;
  std::uint32_t chunk_rounds = 0;
  double kernel_milliseconds = 0.0;

  friend bool operator==(const CandidateBatchTelemetry&, const CandidateBatchTelemetry&) = default;
};

// Query telemetry contains only work attributable to that query's private
// workspace. Shared memory, launch, synchronization, and timing telemetry
// lives on CandidateBatchTelemetry.
struct CandidateQueryTelemetry {
  std::uint64_t examined_work = 0;
  std::uint64_t heading_turn_relaxations = 0;
  std::uint32_t rounds = 0;

  friend bool operator==(const CandidateQueryTelemetry&, const CandidateQueryTelemetry&) = default;
};

struct PlanarGpuFailure {
  PlanarGpuFailureCode code;
  std::string detail;
  std::string invariant_id;
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

struct DeviceResultHeaderV1 {
  std::uint32_t schema_version = kDeviceCompiledBoardSchemaVersion;
  std::uint32_t compiler_version = 0;
  std::uint32_t start_node = kInvalidNodeIndex;
  std::uint32_t goal_node = kInvalidNodeIndex;
  std::uint64_t source_board_content_hash = 0;
  std::uint64_t compiler_profile_fingerprint = 0;
  std::uint64_t rule_bucket_identity = 0;
  std::uint64_t device_view_fingerprint = 0;
  PlanarGenerator generator = PlanarGenerator::kBucketedFrontier;
  std::array<std::uint8_t, 7> reserved{};

  friend bool operator==(const DeviceResultHeaderV1&, const DeviceResultHeaderV1&) = default;
};
static_assert(sizeof(DeviceResultHeaderV1) == 56);

struct DeviceCompiledBoardV1 {
  DeviceCompiledHeaderV1 header;
  std::vector<DeviceLayerRangeV1> layers;
  std::vector<DeviceNodeV1> nodes;
  std::vector<DeviceRunV1> runs;
  std::vector<std::uint32_t> run_nodes;

  friend bool operator==(const DeviceCompiledBoardV1&, const DeviceCompiledBoardV1&) = default;
};

[[nodiscard]] std::uint64_t ComputeDeviceCompiledBoardFingerprintV1(
    const DeviceCompiledBoardV1& board);

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
  PlanarGenerator generator = PlanarGenerator::kBucketedFrontier;
  KernelCompletion completion = KernelCompletion::kDisconnected;
  std::uint32_t start_node = kInvalidNodeIndex;
  std::uint32_t goal_node = kInvalidNodeIndex;
  std::uint32_t goal_state = kInvalidStateIndex;
  std::vector<std::uint64_t> labels;
  std::vector<std::uint32_t> predecessors;
  KernelTelemetry telemetry;
};

struct PlanarRoutePolicy {
  PlanarGenerator generator = PlanarGenerator::kBucketedFrontier;
  std::uint32_t maximum_rounds = 100'000;
  std::uint64_t maximum_device_bytes = std::numeric_limits<std::uint64_t>::max();
  const std::atomic_bool* cancellation = nullptr;
};

struct BackendExecutionRequest {
  PlanarGenerator generator;
  std::uint32_t start_node;
  std::uint32_t goal_node;
  std::uint32_t maximum_rounds;
  std::uint64_t maximum_device_bytes;
  const std::atomic_bool* cancellation;
};

// One canonical physical device edge adjustment. physical_edge_index is the
// source-node-major directed-edge index for one of the four canonical positive
// directions. Reverse traversal resolves to the same index. UINT64_MAX means
// banned; every other value is a finite additive transition penalty.
struct DeviceCandidatePolicyEdgeV1 {
  std::uint32_t physical_edge_index = kInvalidStateIndex;
  std::uint32_t reserved = 0;
  std::uint64_t adjustment = 0;

  friend bool operator==(const DeviceCandidatePolicyEdgeV1&,
                         const DeviceCandidatePolicyEdgeV1&) = default;
};
static_assert(sizeof(DeviceCandidatePolicyEdgeV1) == 16);

// Versioned query record copied once for a query-major batch. All offsets and
// counts are checked by the host and backend before allocation or launch.
struct DeviceCandidateBatchQueryV1 {
  std::uint32_t schema_version = kDeviceCandidateBatchSchemaVersion;
  std::uint32_t input_ordinal = 0;
  std::uint64_t batch_id = 0;
  std::uint64_t query_id = 0;
  std::uint64_t workspace_owner = 0;
  std::uint64_t policy_identity = 0;
  std::uint64_t routing_profile_fingerprint = 0;
  std::uint32_t start_node = kInvalidNodeIndex;
  std::uint32_t goal_node = kInvalidNodeIndex;
  std::uint32_t policy_offset = 0;
  std::uint32_t policy_count = 0;
  std::uint64_t workspace_offset = 0;
  std::uint64_t workspace_state_count = 0;
  std::uint64_t frontier_state_capacity = 0;
  std::uint64_t orthogonal_step_surcharge = 0;
  std::uint64_t diagonal_step_surcharge = 0;
  std::uint64_t bend_surcharge = 0;
  std::uint32_t maximum_rounds = 0;
  PlanarGenerator generator = PlanarGenerator::kBucketedFrontier;
  std::array<std::uint8_t, 3> reserved{};

  friend bool operator==(const DeviceCandidateBatchQueryV1&,
                         const DeviceCandidateBatchQueryV1&) = default;
};
static_assert(sizeof(DeviceCandidateBatchQueryV1) == 120);

// Device-produced result header. It repeats immutable-view, query, policy, and
// workspace ownership associations before the host interprets any query state.
struct DeviceCandidateBatchResultV1 {
  std::uint32_t schema_version = kDeviceCandidateBatchSchemaVersion;
  std::uint32_t compiler_version = 0;
  std::uint32_t input_ordinal = 0;
  std::uint32_t start_node = kInvalidNodeIndex;
  std::uint32_t goal_node = kInvalidNodeIndex;
  std::uint32_t goal_state = kInvalidStateIndex;
  std::uint32_t rounds = 0;
  PlanarGenerator generator = PlanarGenerator::kBucketedFrontier;
  KernelCompletion completion = KernelCompletion::kDisconnected;
  std::array<std::uint8_t, 2> reserved{};
  std::uint64_t batch_id = 0;
  std::uint64_t query_id = 0;
  std::uint64_t workspace_owner = 0;
  std::uint64_t policy_identity = 0;
  std::uint64_t routing_profile_fingerprint = 0;
  std::uint64_t source_board_content_hash = 0;
  std::uint64_t compiler_profile_fingerprint = 0;
  std::uint64_t rule_bucket_identity = 0;
  std::uint64_t device_view_fingerprint = 0;
  std::uint64_t examined_work = 0;
  std::uint64_t heading_turn_relaxations = 0;

  friend bool operator==(const DeviceCandidateBatchResultV1&,
                         const DeviceCandidateBatchResultV1&) = default;
};
static_assert(sizeof(DeviceCandidateBatchResultV1) == 120);

struct BackendCandidateBatchExecutionRequest {
  std::uint32_t schema_version = kDeviceCandidateBatchSchemaVersion;
  std::uint64_t batch_id = 0;
  PlanarGenerator generator = PlanarGenerator::kBucketedFrontier;
  std::uint32_t maximum_rounds = 0;
  std::uint64_t maximum_workspace_states_per_query = std::numeric_limits<std::uint64_t>::max();
  std::uint64_t maximum_frontier_states_per_query = std::numeric_limits<std::uint64_t>::max();
  std::uint64_t maximum_device_bytes = std::numeric_limits<std::uint64_t>::max();
  std::uint64_t maximum_host_bytes = std::numeric_limits<std::uint64_t>::max();
  std::vector<DeviceCandidateBatchQueryV1> queries;
  std::vector<DeviceCandidatePolicyEdgeV1> policy_edges;
  const std::atomic_bool* cancellation = nullptr;
};

// Deterministic logical upper bound for transient host payload owned while a
// candidate batch is encoded, executed, read back, and partitioned into
// query-local results. input_query_count includes retained preflight failures;
// admitted_query_count includes only queries allocated execution/readback
// workspaces. Container allocator overhead is deliberately excluded.
[[nodiscard]] std::optional<std::uint64_t> EstimateCandidateBatchHostBytesV1(
    std::uint64_t input_query_count, std::uint64_t admitted_query_count,
    std::uint64_t policy_edge_count, std::uint64_t represented_states) noexcept;

// All-input-admitted convenience form used by a backend after preflight.
[[nodiscard]] std::optional<std::uint64_t> EstimateCandidateBatchHostBytesV1(
    std::uint64_t query_count, std::uint64_t policy_edge_count,
    std::uint64_t represented_states) noexcept;

// Peak of the earlier host-preflight phase: retained per-input classification
// envelopes plus normalized-policy scratch for every individually shape-valid
// submitted policy. This phase is checked before policy normalization.
[[nodiscard]] std::optional<std::uint64_t> EstimateCandidateBatchPolicyPreflightHostBytesV1(
    std::uint64_t input_query_count, std::uint64_t submitted_policy_entry_count) noexcept;

enum class BackendErrorCode : std::uint8_t {
  kUnsupported,
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

class PendingCandidateBatchExecution {
 public:
  virtual ~PendingCandidateBatchExecution() = default;
};

struct UntrustedCandidateBatchQueryResult {
  DeviceCandidateBatchResultV1 header;
  std::vector<std::uint64_t> labels;
  std::vector<std::uint32_t> predecessors;
  std::vector<std::uint64_t> state_owners;
  std::vector<std::uint64_t> predecessor_owners;
  CandidateQueryTelemetry telemetry;
};

struct UntrustedCandidateBatchResult {
  std::uint32_t schema_version = kDeviceCandidateBatchSchemaVersion;
  std::uint64_t batch_id = 0;
  PlanarGenerator generator = PlanarGenerator::kBucketedFrontier;
  CandidateBatchTelemetry telemetry;
  std::vector<UntrustedCandidateBatchQueryResult> queries;
};

using BackendMetadataResult = std::variant<BackendMetadata, BackendError>;
using UploadResult = std::variant<std::unique_ptr<UploadedCompiledView>, BackendError>;
using ExecutionResult = std::variant<std::unique_ptr<PendingRouteExecution>, BackendError>;
using ReadbackResult = std::variant<UntrustedKernelResult, BackendError>;
using CandidateBatchExecutionResult =
    std::variant<std::unique_ptr<PendingCandidateBatchExecution>, BackendError>;
using CandidateBatchReadbackResult = std::variant<UntrustedCandidateBatchResult, BackendError>;

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

  // CUDA overrides these with one query-major allocation and batched launches.
  // The default implementation is explicitly unsupported so existing narrow
  // test doubles do not need to pretend to implement batching.
  [[nodiscard]] virtual CandidateBatchExecutionResult ExecuteCandidateBatch(
      const UploadedCompiledView& board, const BackendCandidateBatchExecutionRequest& request);
  [[nodiscard]] virtual CandidateBatchReadbackResult ReadbackCandidateBatch(
      const PendingCandidateBatchExecution& execution);
};

// Exact final producer type used by the always-linked core authentication
// boundary. CUDA implementation details remain behind the backend interface;
// construction and delegate binding are available only to the hermetic CUDA
// factory. Generic callers cannot subclass or inject a delegate into this
// wrapper.
class CudaPlanarRouteBackend final : public IPlanarRouteBackend {
 public:
  ~CudaPlanarRouteBackend() override = default;

  [[nodiscard]] BackendMetadataResult QueryMetadata() const override {
    return implementation_->QueryMetadata();
  }
  [[nodiscard]] UploadResult UploadCompiledView(const DeviceCompiledBoardV1& board) override {
    return implementation_->UploadCompiledView(board);
  }
  [[nodiscard]] ExecutionResult ExecuteRoute(const UploadedCompiledView& board,
                                             const BackendExecutionRequest& request) override {
    return implementation_->ExecuteRoute(board, request);
  }
  [[nodiscard]] ReadbackResult ReadbackRoute(const PendingRouteExecution& execution) override {
    return implementation_->ReadbackRoute(execution);
  }
  [[nodiscard]] CandidateBatchExecutionResult ExecuteCandidateBatch(
      const UploadedCompiledView& board,
      const BackendCandidateBatchExecutionRequest& request) override {
    return implementation_->ExecuteCandidateBatch(board, request);
  }
  [[nodiscard]] CandidateBatchReadbackResult ReadbackCandidateBatch(
      const PendingCandidateBatchExecution& execution) override {
    return implementation_->ReadbackCandidateBatch(execution);
  }

 private:
  CudaPlanarRouteBackend();

  std::unique_ptr<IPlanarRouteBackend> implementation_;

  friend std::unique_ptr<IPlanarRouteBackend> CreateCudaPlanarRouteBackend();
};

struct PlanarGpuRoute {
  std::uint64_t source_board_content_hash = 0;
  std::uint64_t compiler_profile_fingerprint = 0;
  std::uint32_t compiler_version = 0;
  std::uint64_t rule_bucket_identity = 0;
  std::uint64_t device_view_fingerprint = 0;
  PlanarGenerator generator = PlanarGenerator::kBucketedFrontier;
  std::uint64_t policy_identity = 0;
  BackendMetadata backend;
  std::uint64_t total_cost = 0;
  std::vector<board_ir::Point64> lattice_path;
  std::vector<routing::LayerSegment> segments;
  KernelTelemetry telemetry;
};

using PlanarGpuRouteResult = std::variant<PlanarGpuRoute, PlanarGpuFailure>;

class PreparedPlanarCompiledView;
using PreparedPlanarCompiledViewResult =
    std::variant<std::unique_ptr<PreparedPlanarCompiledView>, PlanarGpuFailure>;

// Reusable immutable device view. Preparation owns flattening, backend metadata
// discovery, and upload; each route still executes, reads back, reconstructs,
// and validates a fresh untrusted kernel result. The backend passed to
// PreparePlanarCompiledView must outlive this object and every route call that
// uses it.
class PreparedPlanarCompiledView {
 public:
  PreparedPlanarCompiledView(const PreparedPlanarCompiledView&) = delete;
  PreparedPlanarCompiledView& operator=(const PreparedPlanarCompiledView&) = delete;
  PreparedPlanarCompiledView(PreparedPlanarCompiledView&&) = delete;
  PreparedPlanarCompiledView& operator=(PreparedPlanarCompiledView&&) = delete;

  [[nodiscard]] IPlanarRouteBackend& backend() const noexcept { return *backend_; }
  [[nodiscard]] const DeviceCompiledBoardV1& device_board() const noexcept { return device_board_; }
  [[nodiscard]] const BackendMetadata& metadata() const noexcept { return metadata_; }
  [[nodiscard]] const UploadedCompiledView& uploaded() const noexcept { return *uploaded_; }
  [[nodiscard]] bool has_authenticated_cuda_producer() const noexcept {
    return authenticated_cuda_producer_;
  }

 private:
  friend PreparedPlanarCompiledViewResult PreparePlanarCompiledView(
      const board_ir::BoardSnapshot& board, const geometry_compiler::CompiledBoard& compiled_board,
      IPlanarRouteBackend& backend);
  friend PreparedPlanarCompiledViewResult PrepareCudaPlanarCompiledView(
      const board_ir::BoardSnapshot& board, const geometry_compiler::CompiledBoard& compiled_board,
      IPlanarRouteBackend& backend);
  friend PlanarGpuRouteResult RouteWithPreparedPlanarGpuBackend(
      const board_ir::BoardSnapshot& board, const geometry_compiler::CompiledBoard& compiled_board,
      const routing::CpuRouteRequest& request, const PlanarRoutePolicy& policy,
      PreparedPlanarCompiledView& prepared);
  PreparedPlanarCompiledView(IPlanarRouteBackend& backend, DeviceCompiledBoardV1 device_board,
                             BackendMetadata metadata,
                             std::unique_ptr<UploadedCompiledView> uploaded)
      : backend_(&backend),
        device_board_(std::move(device_board)),
        metadata_(std::move(metadata)),
        uploaded_(std::move(uploaded)) {}

  IPlanarRouteBackend* const backend_;
  const DeviceCompiledBoardV1 device_board_;
  const BackendMetadata metadata_;
  std::unique_ptr<UploadedCompiledView> uploaded_;
  bool authenticated_cuda_producer_ = false;
};

struct PlanarCandidateBatchQuery {
  std::uint64_t query_id = 0;
  // Opaque caller/list correlation; independent of policy candidate_ordinal.
  std::uint32_t input_ordinal = 0;
  routing::PlanarRouteRequest request;

  friend bool operator==(const PlanarCandidateBatchQuery&,
                         const PlanarCandidateBatchQuery&) = default;
};

struct PlanarCandidateBatchPolicy {
  std::uint32_t schema_version = kDeviceCandidateBatchSchemaVersion;
  std::uint64_t batch_id = 0;
  PlanarGenerator generator = PlanarGenerator::kBucketedFrontier;
  std::uint32_t maximum_rounds = 100'000;
  std::uint64_t maximum_workspace_states_per_query = std::numeric_limits<std::uint64_t>::max();
  std::uint64_t maximum_frontier_states_per_query = std::numeric_limits<std::uint64_t>::max();
  std::uint64_t maximum_device_bytes = std::numeric_limits<std::uint64_t>::max();
  std::uint64_t maximum_host_bytes = std::numeric_limits<std::uint64_t>::max();
  const std::atomic_bool* cancellation = nullptr;
};

class PlanarCandidateBatchItem;

struct PlanarCandidateBatch {
  std::uint32_t schema_version = kDeviceCandidateBatchSchemaVersion;
  std::uint64_t batch_id = 0;
  std::uint64_t device_view_fingerprint = 0;
  PlanarGenerator generator = PlanarGenerator::kBucketedFrontier;
  BackendMetadata backend;
  CandidateBatchTelemetry telemetry;
  std::vector<PlanarCandidateBatchItem> items;
};

using PlanarCandidateBatchResult = std::variant<PlanarCandidateBatch, PlanarGpuFailure>;

struct PlanarCandidateBatchItemEvidence;

// A public item can be inspected and copied. Construction/mutation helpers can
// create only unsealed diagnostic state. Once a reached item is sealed, every
// accessor reads a separately allocated truly-const evidence snapshot; public
// mutation helpers cannot alter it. The snapshot records whether execution came
// through the concrete checksum-pinned CUDA producer boundary. Candidate
// provenance adapters require both host validation and producer authentication.
class PlanarCandidateBatchItem {
 public:
  PlanarCandidateBatchItem(const PlanarCandidateBatchItem&) = default;
  PlanarCandidateBatchItem(PlanarCandidateBatchItem&&) noexcept = default;
  PlanarCandidateBatchItem& operator=(const PlanarCandidateBatchItem&) = default;
  PlanarCandidateBatchItem& operator=(PlanarCandidateBatchItem&&) noexcept = default;

  [[nodiscard]] static PlanarCandidateBatchItem CreateUnsealed(std::uint64_t query_id,
                                                               std::uint32_t input_ordinal,
                                                               PlanarGpuRouteResult result);
  [[nodiscard]] bool SetUnsealedPolicyIdentity(std::uint64_t policy_identity) noexcept;
  [[nodiscard]] bool SetUnsealedResult(PlanarGpuRouteResult result) noexcept;
  [[nodiscard]] PlanarGpuRouteResult TakeResult();

  [[nodiscard]] std::uint64_t query_id() const noexcept;
  [[nodiscard]] std::uint32_t input_ordinal() const noexcept;
  [[nodiscard]] std::uint64_t policy_identity() const noexcept;
  [[nodiscard]] const PlanarGpuRouteResult& result() const noexcept;
  [[nodiscard]] bool has_validated_route_evidence() const noexcept;
  [[nodiscard]] bool has_authenticated_cuda_producer_evidence() const noexcept;
  [[nodiscard]] std::uint32_t validated_batch_schema_version() const noexcept;
  [[nodiscard]] std::uint64_t validated_batch_id() const noexcept;

 private:
  PlanarCandidateBatchItem() = default;
  void SealValidatedRoute(std::uint32_t schema_version, std::uint64_t batch_id,
                          bool authenticated_cuda_producer);

  std::uint64_t query_id_ = 0;
  std::uint32_t input_ordinal_ = 0;
  std::uint64_t policy_identity_ = 0;
  PlanarGpuRouteResult result_;
  std::shared_ptr<const PlanarCandidateBatchItemEvidence> evidence_;

  friend PlanarCandidateBatchResult RouteCandidateBatchWithPreparedPlanarGpuBackend(
      const board_ir::BoardSnapshot& board, const geometry_compiler::CompiledBoard& compiled_board,
      std::span<const PlanarCandidateBatchQuery> queries, const PlanarCandidateBatchPolicy& policy,
      PreparedPlanarCompiledView& prepared);
  friend PlanarCandidateBatchResult RouteCandidateBatchWithPlanarGpuBackend(
      const board_ir::BoardSnapshot& board, const geometry_compiler::CompiledBoard& compiled_board,
      std::span<const PlanarCandidateBatchQuery> queries, const PlanarCandidateBatchPolicy& policy,
      IPlanarRouteBackend& backend);
};

[[nodiscard]] PreparedPlanarCompiledViewResult PreparePlanarCompiledView(
    const board_ir::BoardSnapshot& board, const geometry_compiler::CompiledBoard& compiled_board,
    IPlanarRouteBackend& backend);

[[nodiscard]] PreparedPlanarCompiledViewResult PrepareCudaPlanarCompiledView(
    const board_ir::BoardSnapshot& board, const geometry_compiler::CompiledBoard& compiled_board,
    IPlanarRouteBackend& backend);

[[nodiscard]] PlanarGpuRouteResult ValidateAndReconstructGpuRoute(
    const board_ir::BoardSnapshot& board, const geometry_compiler::CompiledBoard& compiled_board,
    const DeviceCompiledBoardV1& device_board, const routing::CpuRouteRequest& request,
    PlanarGenerator generator, const BackendMetadata& backend,
    const UntrustedKernelResult& untrusted);

[[nodiscard]] PlanarGpuRouteResult RouteWithPlanarGpuBackend(
    const board_ir::BoardSnapshot& board, const geometry_compiler::CompiledBoard& compiled_board,
    const routing::CpuRouteRequest& request, const PlanarRoutePolicy& policy,
    IPlanarRouteBackend& backend);

[[nodiscard]] PlanarGpuRouteResult RouteWithPreparedPlanarGpuBackend(
    const board_ir::BoardSnapshot& board, const geometry_compiler::CompiledBoard& compiled_board,
    const routing::CpuRouteRequest& request, const PlanarRoutePolicy& policy,
    PreparedPlanarCompiledView& prepared);

// Executes all admitted queries against one already uploaded immutable device
// view. Output is always sorted by unique query_id; invalid or unsupported
// queries retain query-local structured failures while compatible peers run.
[[nodiscard]] PlanarCandidateBatchResult RouteCandidateBatchWithPreparedPlanarGpuBackend(
    const board_ir::BoardSnapshot& board, const geometry_compiler::CompiledBoard& compiled_board,
    std::span<const PlanarCandidateBatchQuery> queries, const PlanarCandidateBatchPolicy& policy,
    PreparedPlanarCompiledView& prepared);

[[nodiscard]] PlanarCandidateBatchResult RouteCandidateBatchWithPlanarGpuBackend(
    const board_ir::BoardSnapshot& board, const geometry_compiler::CompiledBoard& compiled_board,
    std::span<const PlanarCandidateBatchQuery> queries, const PlanarCandidateBatchPolicy& policy,
    IPlanarRouteBackend& backend);

}  // namespace apgar::gpu

#endif  // APGAR_GPU_PLANAR_ROUTER_H_
