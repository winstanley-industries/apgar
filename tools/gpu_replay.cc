#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

#include "apgar/adapters/kicad_fixture.h"
#include "apgar/board_ir/stable_hash.h"
#include "apgar/geometry_compiler/compiled_board.h"
#include "apgar/gpu/cuda_backend.h"
#include "apgar/gpu/planar_router.h"
#include "apgar/routing/cpu_astar.h"

namespace {

using apgar::board_ir::AxisAlignedBox64;
using apgar::board_ir::BoardSnapshot;
using apgar::board_ir::Point64;
using apgar::geometry_compiler::ActiveRegion;
using apgar::geometry_compiler::CompiledBoard;
using apgar::geometry_compiler::CompilerProfile;

struct ReplayArtifact {
  std::string fixture;
  std::uint64_t fixture_checksum = 0;
  apgar::gpu::PlanarGenerator generator = apgar::gpu::PlanarGenerator::kBucketedFrontier;
  apgar::gpu::KernelFaultInjection fault = apgar::gpu::KernelFaultInjection::kNone;
  apgar::board_ir::LayerId layer = 0;
  std::uint32_t maximum_rounds = 0;
  std::uint64_t expected_board_hash = 0;
  std::uint64_t expected_profile_fingerprint = 0;
  std::uint64_t expected_device_fingerprint = 0;
  std::string expected_invariant;
};

[[nodiscard]] std::optional<std::string> ReadFile(const std::string& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return std::nullopt;
  }
  return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

[[nodiscard]] std::string RunfilePath(std::string_view relative) {
  if (const char* source_directory = std::getenv("TEST_SRCDIR"); source_directory != nullptr) {
    const char* workspace = std::getenv("TEST_WORKSPACE");
    if (workspace != nullptr) {
      return std::string(source_directory) + "/" + workspace + "/" + std::string(relative);
    }
  }
  if (const char* runfiles = std::getenv("RUNFILES_DIR"); runfiles != nullptr) {
    return std::string(runfiles) + "/_main/" + std::string(relative);
  }
  if (const char* workspace = std::getenv("BUILD_WORKSPACE_DIRECTORY"); workspace != nullptr) {
    return std::string(workspace) + "/" + std::string(relative);
  }
  return std::string(relative);
}

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
  artifact.fault = apgar::gpu::KernelFaultInjection::kGoalPredecessorSelfCycle;
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

[[nodiscard]] CompilerProfile FixtureProfile() {
  const AxisAlignedBox64 bounds{.min = Point64{.x = 0, .y = 6'000'000},
                                .max = Point64{.x = 20'000'000, .y = 14'000'000}};
  return CompilerProfile{
      .schema_version = apgar::geometry_compiler::kCompilerProfileSchemaVersion,
      .lattice_origin = Point64{.x = 0, .y = 0},
      .lattice_step = 500'000,
      .tile_width_nodes = 8,
      .tile_height_nodes = 8,
      .compilation_roi = bounds,
      .active_regions = {ActiveRegion{.layer = 0, .bounds = bounds}},
      .heading_mask = apgar::board_ir::kM1HeadingMask,
      .costs =
          apgar::geometry_compiler::DeterministicCosts{
              .orthogonal_step = 1000, .diagonal_step = 1414, .bend = 100},
  };
}

[[nodiscard]] std::optional<apgar::routing::CpuRouteRequest> Request(
    const BoardSnapshot& board, apgar::board_ir::LayerId layer) {
  const apgar::board_ir::Net* net = board.FindNet(board.data().routing_profile.net);
  if (net == nullptr || net->terminals.size() != 2) {
    return std::nullopt;
  }
  const apgar::board_ir::Terminal* first = board.FindTerminal(net->terminals[0]);
  const apgar::board_ir::Terminal* second = board.FindTerminal(net->terminals[1]);
  if (first == nullptr || second == nullptr) {
    return std::nullopt;
  }
  return apgar::routing::CpuRouteRequest{
      .net = net->ref,
      .start = first->center,
      .goal = second->center,
      .start_layer = layer,
      .goal_layer = layer,
  };
}

int Replay(std::string_view artifact_path) {
  const std::optional<std::string> artifact_contents = ReadFile(RunfilePath(artifact_path));
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
  const std::optional<std::string> fixture = ReadFile(RunfilePath(artifact->fixture));
  if (!fixture.has_value() ||
      apgar::board_ir::StableHashString(*fixture) != artifact->fixture_checksum) {
    std::cerr << "fixture is missing or its checksum differs\n";
    return 2;
  }
  apgar::adapters::KicadFixtureImportResult imported = apgar::adapters::ImportKicadFixture(
      *fixture, apgar::adapters::KicadFixtureImportConfig{
                    .target_net_name = "TARGET", .nominal_width = 500'000, .clearance = 500'000});
  if (!std::holds_alternative<BoardSnapshot>(imported)) {
    std::cerr << "fixture import failed\n";
    return 2;
  }
  const BoardSnapshot& board = std::get<BoardSnapshot>(imported);
  apgar::geometry_compiler::CompileResult compiled_result =
      apgar::geometry_compiler::CompileBoard(board, FixtureProfile());
  if (!std::holds_alternative<CompiledBoard>(compiled_result)) {
    std::cerr << "fixture compilation failed\n";
    return 2;
  }
  const CompiledBoard& compiled = std::get<CompiledBoard>(compiled_result);
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
  const std::optional<apgar::routing::CpuRouteRequest> request = Request(board, artifact->layer);
  if (!request.has_value()) {
    std::cerr << "replay request construction failed\n";
    return 2;
  }
  std::unique_ptr<apgar::gpu::IPlanarRouteBackend> backend =
      apgar::gpu::CreateCudaPlanarRouteBackend();
  apgar::gpu::PlanarGpuRouteResult result =
      apgar::gpu::RouteWithPlanarGpuBackend(board, compiled, *request,
                                            apgar::gpu::PlanarRoutePolicy{
                                                .generator = artifact->generator,
                                                .maximum_rounds = artifact->maximum_rounds,
                                                .fault_injection = artifact->fault,
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
