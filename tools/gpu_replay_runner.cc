#include "tools/gpu_replay_runner.h"

#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <utility>
#include <variant>

#include "apgar/benchmark/planar_corpus.h"
#include "apgar/board_ir/stable_hash.h"
#include "apgar/geometry_compiler/compiled_board.h"
#include "apgar/gpu/fault_injecting_backend.h"
#include "apgar/tooling/runfiles.h"
#include "tools/gpu_replay_parser.h"

namespace apgar::tooling {
namespace {

void WriteMismatches(const GpuReplaySemanticAssociationMismatchesV1& mismatches,
                     std::ostream& diagnostics) {
  diagnostics << "replay semantic association mismatch:";
  if (mismatches.board) {
    diagnostics << " board";
  }
  if (mismatches.compiler_profile) {
    diagnostics << " compiler_profile";
  }
  if (mismatches.routing_profile) {
    diagnostics << " routing_profile";
  }
  if (mismatches.device_view) {
    diagnostics << " device_view";
  }
  diagnostics << '\n';
}

}  // namespace

GpuReplayRunOutcome RunGpuReplayArtifactV1(std::string_view artifact_path,
                                           const GpuReplayBackendFactory& create_backend,
                                           std::ostream& output, std::ostream& diagnostics) {
  const std::optional<std::string> artifact_contents = ReadRunfile(artifact_path);
  if (!artifact_contents.has_value()) {
    diagnostics << "unable to read replay artifact\n";
    return {.status = GpuReplayRunStatus::kRejected, .association_mismatches = {}};
  }
  std::string parse_error;
  const std::optional<GpuReplayArtifactV1> artifact =
      ParseGpuReplayArtifactV1(*artifact_contents, &parse_error);
  if (!artifact.has_value()) {
    diagnostics << parse_error << '\n';
    return {.status = GpuReplayRunStatus::kRejected, .association_mismatches = {}};
  }
  const std::optional<std::string> fixture = ReadRunfile(artifact->fixture);
  if (!fixture.has_value() || board_ir::StableHashString(*fixture) != artifact->fixture_checksum) {
    diagnostics << "fixture is missing or its checksum differs\n";
    return {.status = GpuReplayRunStatus::kRejected, .association_mismatches = {}};
  }
  benchmark::PlanarCorpusCaseResult case_result =
      benchmark::BuildPlanarBakeoffKicadCaseV1(*fixture);
  if (!std::holds_alternative<benchmark::PlanarCorpusCase>(case_result)) {
    diagnostics << std::get<std::string>(case_result) << '\n';
    return {.status = GpuReplayRunStatus::kRejected, .association_mismatches = {}};
  }
  const benchmark::PlanarCorpusCase& replay_case =
      std::get<benchmark::PlanarCorpusCase>(case_result);
  const board_ir::BoardSnapshot& board = replay_case.board;
  const geometry_compiler::CompiledBoard& compiled = replay_case.compiled_board;
  if (replay_case.request.start_layer != artifact->layer ||
      replay_case.request.goal_layer != artifact->layer) {
    diagnostics << "replay layer differs from the versioned corpus case\n";
    return {.status = GpuReplayRunStatus::kRejected, .association_mismatches = {}};
  }
  gpu::DeviceCompiledBoardResult device_result = gpu::BuildDeviceCompiledBoardV1(board, compiled);
  if (!std::holds_alternative<gpu::DeviceCompiledBoardV1>(device_result)) {
    diagnostics << "device-view compilation failed\n";
    return {.status = GpuReplayRunStatus::kRejected, .association_mismatches = {}};
  }
  const gpu::DeviceCompiledBoardV1& device = std::get<gpu::DeviceCompiledBoardV1>(device_result);
  const GpuReplaySemanticAssociationsV1 actual =
      BuildGpuReplaySemanticAssociationsV1(board, compiled, device);
  const GpuReplaySemanticAssociationMismatchesV1 mismatches =
      FindGpuReplaySemanticAssociationMismatchesV1(*artifact, actual);
  if (mismatches.any()) {
    WriteMismatches(mismatches, diagnostics);
    return {
        .status = GpuReplayRunStatus::kRejected,
        .association_mismatches = mismatches,
    };
  }
  std::unique_ptr<gpu::IPlanarRouteBackend> underlying_backend = create_backend();
  if (underlying_backend == nullptr) {
    diagnostics << "backend factory returned null\n";
    return {.status = GpuReplayRunStatus::kFailed, .association_mismatches = {}};
  }
  std::unique_ptr<gpu::IPlanarRouteBackend> backend =
      gpu::CreateFaultInjectingPlanarRouteBackend(*underlying_backend, artifact->fault);
  gpu::PlanarGpuRouteResult result =
      gpu::RouteWithPlanarGpuBackend(board, compiled, replay_case.request,
                                     gpu::PlanarRoutePolicy{
                                         .generator = artifact->generator,
                                         .maximum_rounds = artifact->maximum_rounds,
                                     },
                                     *backend);
  if (!std::holds_alternative<gpu::PlanarGpuFailure>(result) ||
      std::get<gpu::PlanarGpuFailure>(result).code !=
          gpu::PlanarGpuFailureCode::kInternalInvariant ||
      std::get<gpu::PlanarGpuFailure>(result).invariant_id != artifact->expected_invariant) {
    diagnostics << "expected GPU invariant failure was not reproduced\n";
    return {.status = GpuReplayRunStatus::kFailed, .association_mismatches = {}};
  }
  const gpu::BackendMetadataResult metadata = backend->QueryMetadata();
  output << "reproduced=internal_invariant invariant=" << artifact->expected_invariant
         << " artifact_schema=1";
  if (std::holds_alternative<gpu::BackendMetadata>(metadata)) {
    const gpu::BackendMetadata& value = std::get<gpu::BackendMetadata>(metadata);
    output << " backend=" << value.backend << " device=\"" << value.device_name << "\""
           << " compute_capability=" << value.compute_capability_major << '.'
           << value.compute_capability_minor;
  }
  output << '\n';
  return {.status = GpuReplayRunStatus::kReproduced, .association_mismatches = {}};
}

}  // namespace apgar::tooling
