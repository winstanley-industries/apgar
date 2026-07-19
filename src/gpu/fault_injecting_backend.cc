#include "apgar/gpu/fault_injecting_backend.h"

#include <memory>
#include <utility>
#include <variant>

namespace apgar::gpu {
namespace {

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

}  // namespace

std::unique_ptr<IPlanarRouteBackend> CreateFaultInjectingPlanarRouteBackend(
    IPlanarRouteBackend& inner, UntrustedResultFault fault) {
  return std::make_unique<FaultInjectingPlanarRouteBackend>(inner, fault);
}

}  // namespace apgar::gpu
