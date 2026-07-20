#include "apgar/gpu/fault_injecting_backend.h"

#include <algorithm>
#include <limits>
#include <memory>
#include <span>
#include <utility>
#include <variant>

namespace apgar::gpu {
namespace {

template <typename T>
[[nodiscard]] std::span<T> MutableWorkspaceSlice(
    std::vector<T>& storage, const UntrustedCandidateBatchQueryResult& query) noexcept {
  const std::uint64_t offset = query.workspace_offset;
  const std::uint64_t count = query.workspace_state_count;
  if (offset > storage.size() || count > static_cast<std::uint64_t>(storage.size()) - offset) {
    return {};
  }
  return std::span<T>(storage).subspan(static_cast<std::size_t>(offset),
                                       static_cast<std::size_t>(count));
}

class FaultInjectingPlanarRouteBackend final : public IPlanarRouteBackend {
 public:
  FaultInjectingPlanarRouteBackend(IPlanarRouteBackend& inner, UntrustedResultFault fault)
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
    ReadbackResult readback = inner_.ReadbackRoute(execution);
    if (!std::holds_alternative<UntrustedKernelResult>(readback)) {
      return readback;
    }
    UntrustedKernelResult& result = std::get<UntrustedKernelResult>(readback);
    if (fault_ == UntrustedResultFault::kGoalPredecessorSelfCycle &&
        result.completion == KernelCompletion::kReached &&
        result.goal_state < result.predecessors.size()) {
      result.predecessors[result.goal_state] = result.goal_state;
    }
    return readback;
  }

 private:
  IPlanarRouteBackend& inner_;
  UntrustedResultFault fault_;
};

class FaultInjectingCandidateBatchBackend final : public IPlanarRouteBackend {
 public:
  FaultInjectingCandidateBatchBackend(IPlanarRouteBackend& inner,
                                      UntrustedCandidateBatchResultFault fault)
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
    CandidateBatchReadbackResult readback = inner_.ReadbackCandidateBatch(execution);
    if (!std::holds_alternative<UntrustedCandidateBatchResult>(readback)) {
      return readback;
    }
    UntrustedCandidateBatchResult& result = std::get<UntrustedCandidateBatchResult>(readback);
    if (result.queries.empty()) {
      return readback;
    }
    UntrustedCandidateBatchQueryResult& query = result.queries.front();
    switch (fault_) {
      case UntrustedCandidateBatchResultFault::kNone:
        break;
      case UntrustedCandidateBatchResultFault::kWorkspaceBounds:
        query.workspace_state_count =
            query.workspace_state_count == 0 ? 1 : query.workspace_state_count - 1;
        break;
      case UntrustedCandidateBatchResultFault::kWorkspaceOwner: {
        std::span<std::uint64_t> owners = MutableWorkspaceSlice(result.state_owners, query);
        if (!owners.empty()) {
          owners.front() = query.header.workspace_owner == std::numeric_limits<std::uint64_t>::max()
                               ? 0
                               : query.header.workspace_owner + 1;
        }
        break;
      }
      case UntrustedCandidateBatchResultFault::kQueryTelemetry:
        query.header.rounds = query.telemetry.rounds == std::numeric_limits<std::uint32_t>::max()
                                  ? query.telemetry.rounds - 1
                                  : query.telemetry.rounds + 1;
        break;
      case UntrustedCandidateBatchResultFault::kMemoryAccounting:
        result.telemetry.batch_host_bytes =
            result.telemetry.batch_host_bytes == std::numeric_limits<std::uint64_t>::max()
                ? result.telemetry.batch_host_bytes - 1
                : result.telemetry.batch_host_bytes + 1;
        break;
      case UntrustedCandidateBatchResultFault::kBatchTelemetry:
        result.telemetry.kernel_launch_count =
            result.telemetry.kernel_launch_count == std::numeric_limits<std::uint64_t>::max()
                ? result.telemetry.kernel_launch_count - 1
                : result.telemetry.kernel_launch_count + 1;
        break;
      case UntrustedCandidateBatchResultFault::kQueryIdentity:
        query.header.query_id = query.header.query_id == std::numeric_limits<std::uint64_t>::max()
                                    ? 0
                                    : query.header.query_id + 1;
        break;
      case UntrustedCandidateBatchResultFault::kFalseDisconnected: {
        query.header.completion = KernelCompletion::kDisconnected;
        query.header.goal_state = kInvalidStateIndex;
        std::span<std::uint64_t> labels = MutableWorkspaceSlice(result.labels, query);
        std::span<std::uint32_t> predecessors = MutableWorkspaceSlice(result.predecessors, query);
        std::ranges::fill(labels, kInfiniteRouteCost);
        std::ranges::fill(predecessors, kInvalidStateIndex);
        const std::uint64_t start_state =
            static_cast<std::uint64_t>(query.header.start_node) * kIncomingHeadingCount +
            kNoIncomingHeading;
        if (start_state < labels.size()) {
          labels[static_cast<std::size_t>(start_state)] = 0;
        }
        break;
      }
    }
    return readback;
  }

 private:
  IPlanarRouteBackend& inner_;
  UntrustedCandidateBatchResultFault fault_;
};

}  // namespace

std::unique_ptr<IPlanarRouteBackend> CreateFaultInjectingPlanarRouteBackend(
    IPlanarRouteBackend& inner, UntrustedResultFault fault) {
  return std::make_unique<FaultInjectingPlanarRouteBackend>(inner, fault);
}

std::unique_ptr<IPlanarRouteBackend> CreateFaultInjectingCandidateBatchBackend(
    IPlanarRouteBackend& inner, UntrustedCandidateBatchResultFault fault) {
  return std::make_unique<FaultInjectingCandidateBatchBackend>(inner, fault);
}

}  // namespace apgar::gpu
