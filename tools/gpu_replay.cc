#include <charconv>
#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

#include "apgar/benchmark/planar_corpus.h"
#include "apgar/board_ir/stable_hash.h"
#include "apgar/gpu/cuda_backend.h"
#include "apgar/gpu/fault_injecting_backend.h"
#include "apgar/gpu/planar_router.h"
#include "apgar/tooling/runfiles.h"

namespace {

struct ReplayArtifact {
  std::string fixture;
  std::uint64_t fixture_checksum = 0;
  apgar::gpu::PlanarGenerator generator = apgar::gpu::PlanarGenerator::kBucketedFrontier;
  apgar::gpu::UntrustedResultFault fault =
      apgar::gpu::UntrustedResultFault::kGoalPredecessorSelfCycle;
  apgar::board_ir::LayerId layer = 0;
  std::uint32_t maximum_rounds = 0;
  std::uint64_t expected_board_hash = 0;
  std::uint64_t expected_profile_fingerprint = 0;
  std::uint64_t expected_device_fingerprint = 0;
  std::string expected_invariant;
};

template <typename Integer>
[[nodiscard]] bool ParseUnsigned(std::string_view text, Integer* value) {
  const char* begin = text.data();
  const char* end = text.data() + text.size();
  const auto [next, error] = std::from_chars(begin, end, *value);
  return error == std::errc{} && next == end;
}

[[nodiscard]] std::optional<ReplayArtifact> ParseArtifact(std::string_view contents,
                                                          std::string* error) {
  if (contents.empty() || contents.back() != '\n') {
    *error = "canonical replay must end every line with LF";
    return std::nullopt;
  }
  const std::size_t checksum_start = contents.rfind("checksum_fnv1a64=");
  if (checksum_start == std::string_view::npos || checksum_start == 0 ||
      contents[checksum_start - 1] != '\n') {
    *error = "missing final replay checksum";
    return std::nullopt;
  }
  const std::string_view payload = contents.substr(0, checksum_start);
  std::string_view checksum_text = contents.substr(checksum_start + 17);
  checksum_text.remove_suffix(1);
  std::uint64_t recorded_checksum = 0;
  if (!ParseUnsigned(checksum_text, &recorded_checksum) ||
      apgar::board_ir::StableHashString(payload) != recorded_checksum) {
    *error = "replay payload checksum mismatch";
    return std::nullopt;
  }

  std::vector<std::string_view> values;
  constexpr std::string_view kKeys[] = {
      "format=",
      "schema_version=",
      "fixture=",
      "fixture_checksum_fnv1a64=",
      "generator=",
      "fault=",
      "layer=",
      "maximum_rounds=",
      "expected_board_content_hash=",
      "expected_profile_fingerprint=",
      "expected_device_view_fingerprint=",
      "expected_failure=",
      "expected_invariant=",
  };
  std::size_t offset = 0;
  for (std::string_view key : kKeys) {
    const std::size_t end = payload.find('\n', offset);
    if (end == std::string_view::npos) {
      *error = "replay payload is truncated";
      return std::nullopt;
    }
    const std::string_view line = payload.substr(offset, end - offset);
    if (!line.starts_with(key)) {
      *error = "replay fields are missing or out of canonical order";
      return std::nullopt;
    }
    values.push_back(line.substr(key.size()));
    offset = end + 1;
  }
  if (offset != payload.size()) {
    *error = "replay payload has unknown fields";
    return std::nullopt;
  }
  if (values[0] != "apgar_gpu_invariant_replay" || values[1] != "1" ||
      values[4] != "bucketed_frontier" || values[5] != "goal_predecessor_self_cycle" ||
      values[11] != "internal_invariant" || values[12] != "gpu.predecessor.self_reference.v1") {
    *error = "replay format, schema, generator, fault, or expected outcome is unsupported";
    return std::nullopt;
  }

  ReplayArtifact artifact;
  artifact.fixture = values[2];
  artifact.generator = apgar::gpu::PlanarGenerator::kBucketedFrontier;
  artifact.expected_invariant = std::string(values[12]);
  if (!ParseUnsigned(values[3], &artifact.fixture_checksum) ||
      !ParseUnsigned(values[6], &artifact.layer) ||
      !ParseUnsigned(values[7], &artifact.maximum_rounds) || artifact.maximum_rounds == 0 ||
      !ParseUnsigned(values[8], &artifact.expected_board_hash) ||
      !ParseUnsigned(values[9], &artifact.expected_profile_fingerprint) ||
      !ParseUnsigned(values[10], &artifact.expected_device_fingerprint)) {
    *error = "replay contains an invalid integer field";
    return std::nullopt;
  }
  return artifact;
}

int Replay(std::string_view artifact_path) {
  const std::optional<std::string> artifact_contents = apgar::tooling::ReadRunfile(artifact_path);
  if (!artifact_contents.has_value()) {
    std::cerr << "unable to read replay artifact\n";
    return 2;
  }
  std::string parse_error;
  const std::optional<ReplayArtifact> artifact = ParseArtifact(*artifact_contents, &parse_error);
  if (!artifact.has_value()) {
    std::cerr << parse_error << '\n';
    return 2;
  }
  const std::optional<std::string> fixture = apgar::tooling::ReadRunfile(artifact->fixture);
  if (!fixture.has_value() ||
      apgar::board_ir::StableHashString(*fixture) != artifact->fixture_checksum) {
    std::cerr << "fixture is missing or its checksum differs\n";
    return 2;
  }
  apgar::benchmark::PlanarCorpusCaseResult case_result =
      apgar::benchmark::BuildPlanarBakeoffKicadCaseV1(*fixture);
  if (!std::holds_alternative<apgar::benchmark::PlanarCorpusCase>(case_result)) {
    std::cerr << std::get<std::string>(case_result) << '\n';
    return 2;
  }
  const apgar::benchmark::PlanarCorpusCase& replay_case =
      std::get<apgar::benchmark::PlanarCorpusCase>(case_result);
  const apgar::board_ir::BoardSnapshot& board = replay_case.board;
  const apgar::geometry_compiler::CompiledBoard& compiled = replay_case.compiled_board;
  if (replay_case.request.start_layer != artifact->layer ||
      replay_case.request.goal_layer != artifact->layer) {
    std::cerr << "replay layer differs from the versioned corpus case\n";
    return 2;
  }
  apgar::gpu::DeviceCompiledBoardResult device_result =
      apgar::gpu::BuildDeviceCompiledBoardV1(board, compiled);
  if (!std::holds_alternative<apgar::gpu::DeviceCompiledBoardV1>(device_result)) {
    std::cerr << "device-view compilation failed\n";
    return 2;
  }
  const apgar::gpu::DeviceCompiledBoardV1& device =
      std::get<apgar::gpu::DeviceCompiledBoardV1>(device_result);
  if (board.content_hash() != artifact->expected_board_hash ||
      compiled.compiler_profile_fingerprint() != artifact->expected_profile_fingerprint ||
      device.header.device_view_fingerprint != artifact->expected_device_fingerprint) {
    std::cerr << "replay semantic associations differ: board=" << board.content_hash()
              << " profile=" << compiled.compiler_profile_fingerprint()
              << " device=" << device.header.device_view_fingerprint << '\n';
    return 2;
  }
  std::unique_ptr<apgar::gpu::IPlanarRouteBackend> cuda_backend =
      apgar::gpu::CreateCudaPlanarRouteBackend();
  std::unique_ptr<apgar::gpu::IPlanarRouteBackend> backend =
      apgar::gpu::CreateFaultInjectingPlanarRouteBackend(*cuda_backend, artifact->fault);
  apgar::gpu::PlanarGpuRouteResult result =
      apgar::gpu::RouteWithPlanarGpuBackend(board, compiled, replay_case.request,
                                            apgar::gpu::PlanarRoutePolicy{
                                                .generator = artifact->generator,
                                                .maximum_rounds = artifact->maximum_rounds,
                                            },
                                            *backend);
  if (!std::holds_alternative<apgar::gpu::PlanarGpuFailure>(result) ||
      std::get<apgar::gpu::PlanarGpuFailure>(result).code !=
          apgar::gpu::PlanarGpuFailureCode::kInternalInvariant ||
      std::get<apgar::gpu::PlanarGpuFailure>(result).invariant_id != artifact->expected_invariant) {
    std::cerr << "expected GPU invariant failure was not reproduced\n";
    return 1;
  }
  const apgar::gpu::BackendMetadataResult metadata = backend->QueryMetadata();
  std::cout << "reproduced=internal_invariant invariant=" << artifact->expected_invariant
            << " artifact_schema=1";
  if (std::holds_alternative<apgar::gpu::BackendMetadata>(metadata)) {
    const apgar::gpu::BackendMetadata& value = std::get<apgar::gpu::BackendMetadata>(metadata);
    std::cout << " backend=" << value.backend << " device=\"" << value.device_name << "\""
              << " compute_capability=" << value.compute_capability_major << '.'
              << value.compute_capability_minor;
  }
  std::cout << '\n';
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  const std::string_view artifact =
      argc == 2 ? std::string_view(argv[1])
                : std::string_view("replays/gpu/goal_predecessor_self_cycle_v1.replay");
  return Replay(artifact);
}
