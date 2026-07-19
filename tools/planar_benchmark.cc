#include <benchmark/benchmark.h>
#include <sys/utsname.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "apgar/benchmark/planar_corpus.h"
#include "apgar/board_ir/stable_hash.h"
#include "apgar/gpu/cuda_backend.h"
#include "apgar/gpu/planar_router.h"

namespace {

inline constexpr int kBenchmarkRepetitions = 20;
inline constexpr double kBenchmarkMinimumSeconds = 0.02;
inline constexpr double kBenchmarkWarmupSeconds = 0.01;

struct VisibleOutcome {
  bool reachable = false;
  std::string failure;
  std::uint64_t cost = 0;
  std::uint64_t geometry_fingerprint = 0;

  friend bool operator==(const VisibleOutcome&, const VisibleOutcome&) = default;
};

struct RouteObservation {
  VisibleOutcome visible;
  std::uint64_t examined_work = 0;
  std::uint32_t rounds = 0;
  std::uint64_t peak_device_bytes = 0;
  double kernel_milliseconds = 0;
};

struct BenchmarkContext {
  std::vector<apgar::benchmark::PlanarCorpusCase> corpus;
  std::vector<VisibleOutcome> cpu_oracles;
  std::unique_ptr<apgar::gpu::IPlanarRouteBackend> backend;
};

[[nodiscard]] std::string RunfilePath(std::string_view relative) {
  if (const char* runfiles = std::getenv("RUNFILES_DIR"); runfiles != nullptr) {
    return std::string(runfiles) + "/_main/" + std::string(relative);
  }
  if (const char* workspace = std::getenv("BUILD_WORKSPACE_DIRECTORY"); workspace != nullptr) {
    return std::string(workspace) + "/" + std::string(relative);
  }
  return std::string(relative);
}

[[nodiscard]] std::optional<std::string> ReadFile(std::string_view relative) {
  std::ifstream input(RunfilePath(relative), std::ios::binary);
  if (!input) {
    return std::nullopt;
  }
  return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

[[nodiscard]] std::optional<std::string> ReadHostFile(std::string_view path) {
  std::ifstream input(std::string(path), std::ios::binary);
  if (!input) {
    return std::nullopt;
  }
  return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

[[nodiscard]] std::optional<std::string> LineValue(std::string_view contents,
                                                   std::string_view key) {
  const std::size_t key_offset = contents.find(key);
  if (key_offset == std::string_view::npos) {
    return std::nullopt;
  }
  std::string_view value = contents.substr(key_offset + key.size());
  value = value.substr(0, value.find('\n'));
  while (!value.empty() && (value.front() == ' ' || value.front() == '\t' || value.front() == ':' ||
                            value.front() == '"')) {
    value.remove_prefix(1);
  }
  while (!value.empty() && (value.back() == ' ' || value.back() == '\t' || value.back() == '"')) {
    value.remove_suffix(1);
  }
  return std::string(value);
}

void AddPoint(apgar::board_ir::StableHashBuilder* hash, apgar::board_ir::Point64 point) {
  hash->AddI64(point.x);
  hash->AddI64(point.y);
}

[[nodiscard]] std::uint64_t GeometryFingerprint(
    std::span<const apgar::board_ir::Point64> path,
    std::span<const apgar::routing::LayerSegment> segments) {
  apgar::board_ir::StableHashBuilder hash;
  hash.AddString("APGAR-PHASE2-BENCHMARK-GEOMETRY-V1");
  hash.AddU64(path.size());
  for (apgar::board_ir::Point64 point : path) {
    AddPoint(&hash, point);
  }
  hash.AddU64(segments.size());
  for (const apgar::routing::LayerSegment& segment : segments) {
    hash.AddI32(segment.layer);
    AddPoint(&hash, segment.centerline.start);
    AddPoint(&hash, segment.centerline.end);
  }
  return hash.Finish();
}

[[nodiscard]] std::string CpuFailureName(apgar::routing::RouteFailureCode code) {
  using apgar::routing::RouteFailureCode;
  switch (code) {
    case RouteFailureCode::kInvalidRequest:
      return "invalid";
    case RouteFailureCode::kDisconnected:
      return "disconnected";
    case RouteFailureCode::kUnsupportedLayerTransition:
      return "unsupported";
    case RouteFailureCode::kValidationFailed:
      return "validation";
    case RouteFailureCode::kResourceExhausted:
      return "resource_exhausted";
    case RouteFailureCode::kInternalInvariant:
      return "invariant";
  }
  return "unknown";
}

[[nodiscard]] std::string GpuFailureName(apgar::gpu::PlanarGpuFailureCode code) {
  using apgar::gpu::PlanarGpuFailureCode;
  switch (code) {
    case PlanarGpuFailureCode::kInvalidInput:
      return "invalid";
    case PlanarGpuFailureCode::kUnsupported:
      return "unsupported";
    case PlanarGpuFailureCode::kDisconnected:
      return "disconnected";
    case PlanarGpuFailureCode::kResourceExhausted:
      return "resource_exhausted";
    case PlanarGpuFailureCode::kCancelled:
      return "cancelled";
    case PlanarGpuFailureCode::kBackendFailure:
      return "backend_failure";
    case PlanarGpuFailureCode::kValidationFailed:
      return "validation";
    case PlanarGpuFailureCode::kInternalInvariant:
      return "invariant";
  }
  return "unknown";
}

[[nodiscard]] double FailureCode(std::string_view failure) {
  if (failure.empty()) {
    return 0.0;
  }
  if (failure == "disconnected") {
    return 3.0;
  }
  if (failure == "resource_exhausted") {
    return 4.0;
  }
  if (failure == "unsupported") {
    return 2.0;
  }
  if (failure == "cancelled") {
    return 5.0;
  }
  if (failure == "backend_failure") {
    return 6.0;
  }
  if (failure == "validation") {
    return 7.0;
  }
  if (failure == "invariant") {
    return 8.0;
  }
  return 1.0;
}

[[nodiscard]] RouteObservation RunCpu(const apgar::benchmark::PlanarCorpusCase& test_case) {
  apgar::routing::CpuRouteResult result = apgar::routing::RouteWithCpuAStar(
      test_case.board, test_case.compiled_board, test_case.request);
  RouteObservation observation;
  if (std::holds_alternative<apgar::routing::CpuRoute>(result)) {
    const apgar::routing::CpuRoute& route = std::get<apgar::routing::CpuRoute>(result);
    observation.visible.reachable = true;
    observation.visible.cost = route.total_cost;
    observation.visible.geometry_fingerprint =
        GeometryFingerprint(route.lattice_path, route.segments);
    observation.examined_work = route.telemetry.expanded_states;
  } else {
    const apgar::routing::RouteFailure& failure = std::get<apgar::routing::RouteFailure>(result);
    observation.visible.failure = CpuFailureName(failure.code);
    if (failure.telemetry.has_value()) {
      observation.examined_work = failure.telemetry->expanded_states;
    }
  }
  return observation;
}

[[nodiscard]] RouteObservation RunGpu(const apgar::benchmark::PlanarCorpusCase& test_case,
                                      apgar::gpu::PlanarGenerator generator,
                                      apgar::gpu::IPlanarRouteBackend* backend) {
  apgar::gpu::PlanarGpuRouteResult result = apgar::gpu::RouteWithPlanarGpuBackend(
      test_case.board, test_case.compiled_board, test_case.request,
      apgar::gpu::PlanarRoutePolicy{.generator = generator}, *backend);
  RouteObservation observation;
  if (std::holds_alternative<apgar::gpu::PlanarGpuRoute>(result)) {
    const apgar::gpu::PlanarGpuRoute& route = std::get<apgar::gpu::PlanarGpuRoute>(result);
    observation.visible.reachable = true;
    observation.visible.cost = route.total_cost;
    observation.visible.geometry_fingerprint =
        GeometryFingerprint(route.lattice_path, route.segments);
    observation.examined_work = route.telemetry.examined_work;
    observation.rounds = route.telemetry.rounds;
    observation.peak_device_bytes = route.telemetry.peak_device_bytes;
    observation.kernel_milliseconds = route.telemetry.kernel_milliseconds;
  } else {
    const apgar::gpu::PlanarGpuFailure& failure = std::get<apgar::gpu::PlanarGpuFailure>(result);
    observation.visible.failure = GpuFailureName(failure.code);
    if (failure.telemetry.has_value()) {
      observation.examined_work = failure.telemetry->examined_work;
      observation.rounds = failure.telemetry->rounds;
      observation.peak_device_bytes = failure.telemetry->peak_device_bytes;
      observation.kernel_milliseconds = failure.telemetry->kernel_milliseconds;
    }
  }
  return observation;
}

void PublishCounters(benchmark::State& state, const RouteObservation& observation,
                     bool differential_match, bool deterministic, double kernel_sum,
                     std::uint64_t observed_iterations) {
  state.counters["reachable"] = observation.visible.reachable ? 1.0 : 0.0;
  state.counters["failure_code"] = FailureCode(observation.visible.failure);
  state.counters["path_cost"] = static_cast<double>(observation.visible.cost);
  state.counters["geometry_hash_hi"] =
      static_cast<double>(observation.visible.geometry_fingerprint >> 32U);
  state.counters["geometry_hash_lo"] = static_cast<double>(
      observation.visible.geometry_fingerprint & std::numeric_limits<std::uint32_t>::max());
  state.counters["deterministic"] = deterministic ? 1.0 : 0.0;
  state.counters["differential_match"] = differential_match ? 1.0 : 0.0;
  state.counters["examined_work"] = static_cast<double>(observation.examined_work);
  state.counters["rounds"] = static_cast<double>(observation.rounds);
  state.counters["peak_owned_device_bytes"] = static_cast<double>(observation.peak_device_bytes);
  state.counters["kernel_milliseconds"] =
      observed_iterations == 0 ? 0.0 : kernel_sum / static_cast<double>(observed_iterations);
}

void CpuBenchmark(benchmark::State& state, const apgar::benchmark::PlanarCorpusCase* test_case,
                  const VisibleOutcome* oracle) {
  RouteObservation first;
  bool has_first = false;
  bool deterministic = true;
  for (auto _ : state) {
    static_cast<void>(_);
    RouteObservation observation = RunCpu(*test_case);
    benchmark::DoNotOptimize(observation.visible.geometry_fingerprint);
    if (!has_first) {
      first = observation;
      has_first = true;
    } else {
      deterministic &= observation.visible == first.visible;
    }
  }
  const bool differential = has_first && first.visible.reachable == oracle->reachable &&
                            first.visible.failure == oracle->failure &&
                            (!first.visible.reachable || first.visible.cost == oracle->cost);
  if (!deterministic || !differential) {
    state.SkipWithError("CPU benchmark violated deterministic oracle semantics");
  }
  PublishCounters(state, first, differential, deterministic, 0.0, state.iterations());
}

void GpuBenchmark(benchmark::State& state, const apgar::benchmark::PlanarCorpusCase* test_case,
                  const VisibleOutcome* cpu_oracle, apgar::gpu::PlanarGenerator generator,
                  apgar::gpu::IPlanarRouteBackend* backend) {
  RouteObservation first;
  bool has_first = false;
  bool deterministic = true;
  double kernel_sum = 0.0;
  for (auto _ : state) {
    static_cast<void>(_);
    RouteObservation observation = RunGpu(*test_case, generator, backend);
    benchmark::DoNotOptimize(observation.visible.geometry_fingerprint);
    kernel_sum += observation.kernel_milliseconds;
    if (!has_first) {
      first = observation;
      has_first = true;
    } else {
      deterministic &= observation.visible == first.visible;
    }
  }
  const bool differential = has_first && first.visible.reachable == cpu_oracle->reachable &&
                            first.visible.failure == cpu_oracle->failure &&
                            (!first.visible.reachable || first.visible.cost == cpu_oracle->cost);
  if (!deterministic || !differential) {
    state.SkipWithError("GPU benchmark violated deterministic CPU differential semantics");
  }
  PublishCounters(state, first, differential, deterministic, kernel_sum, state.iterations());
}

void Configure(benchmark::Benchmark* registered) {
  registered->UseRealTime()
      ->Unit(benchmark::kMicrosecond)
      ->MinTime(kBenchmarkMinimumSeconds)
      ->MinWarmUpTime(kBenchmarkWarmupSeconds)
      ->Repetitions(kBenchmarkRepetitions)
      ->DisplayAggregatesOnly(true)
      ->ReportAggregatesOnly(true);
}

[[nodiscard]] std::optional<std::string> ExtractCommit(int* argc, char** argv) {
  std::optional<std::string> commit;
  int destination = 1;
  for (int source = 1; source < *argc; ++source) {
    const std::string_view argument(argv[source]);
    if (argument.starts_with("--apgar_commit=")) {
      commit = argument.substr(15);
    } else {
      argv[destination++] = argv[source];
    }
  }
  *argc = destination;
  return commit;
}

[[nodiscard]] std::optional<BenchmarkContext> BuildContext() {
  const std::optional<std::string> fixture = ReadFile("tests/fixtures/m1_exactness.kicad_pcb");
  if (!fixture.has_value()) {
    return std::nullopt;
  }
  apgar::benchmark::PlanarCorpusResult corpus_result =
      apgar::benchmark::BuildPlanarBakeoffCorpus(*fixture);
  if (!std::holds_alternative<std::vector<apgar::benchmark::PlanarCorpusCase>>(corpus_result)) {
    std::cerr << std::get<std::string>(corpus_result) << '\n';
    return std::nullopt;
  }
  BenchmarkContext context{
      .corpus = std::get<std::vector<apgar::benchmark::PlanarCorpusCase>>(std::move(corpus_result)),
      .cpu_oracles = {},
      .backend = apgar::gpu::CreateCudaPlanarRouteBackend(),
  };
  context.cpu_oracles.reserve(context.corpus.size());
  for (const apgar::benchmark::PlanarCorpusCase& test_case : context.corpus) {
    context.cpu_oracles.push_back(RunCpu(test_case).visible);
  }
  return context;
}

void AddContext(const std::string& commit, const BenchmarkContext& context) {
  const apgar::gpu::BackendMetadataResult metadata_result = context.backend->QueryMetadata();
  if (!std::holds_alternative<apgar::gpu::BackendMetadata>(metadata_result)) {
    return;
  }
  const apgar::gpu::BackendMetadata& metadata =
      std::get<apgar::gpu::BackendMetadata>(metadata_result);
  benchmark::AddCustomContext("apgar_commit", commit);
  benchmark::AddCustomContext("apgar_corpus_version",
                              std::to_string(apgar::benchmark::kPlanarBakeoffCorpusVersion));
  benchmark::AddCustomContext("apgar_baseline", "cpu_astar");
  benchmark::AddCustomContext("apgar_seed", "none_fixed_corpus_v1");
  benchmark::AddCustomContext("apgar_timing_scope",
                              "full_route_including_upload_reconstruction_validation");
  benchmark::AddCustomContext("apgar_repetitions", std::to_string(kBenchmarkRepetitions));
  benchmark::AddCustomContext("apgar_min_time_seconds", std::to_string(kBenchmarkMinimumSeconds));
  benchmark::AddCustomContext("apgar_min_warmup_seconds", std::to_string(kBenchmarkWarmupSeconds));
  benchmark::AddCustomContext("apgar_backend", metadata.backend);
  benchmark::AddCustomContext("apgar_device_name", metadata.device_name);
  benchmark::AddCustomContext("apgar_device_uuid", metadata.device_uuid);
  benchmark::AddCustomContext("apgar_compute_capability",
                              std::to_string(metadata.compute_capability_major) + "." +
                                  std::to_string(metadata.compute_capability_minor));
  benchmark::AddCustomContext("apgar_cuda_runtime", std::to_string(metadata.runtime_version));
  benchmark::AddCustomContext("apgar_cuda_driver", std::to_string(metadata.driver_version));
  benchmark::AddCustomContext("apgar_global_memory_bytes",
                              std::to_string(metadata.global_memory_bytes));
  benchmark::AddCustomContext("apgar_cuda_toolkit", "13.0.2 checksum-pinned redistributables");
  benchmark::AddCustomContext("apgar_nvcc_build", "13.0.88 checksum-pinned redistributable");
  benchmark::AddCustomContext("apgar_cudart_build", "13.0.96 checksum-pinned redistributable");
  benchmark::AddCustomContext("apgar_benchmark_cpp_toolchain",
                              "GCC 15.2.0 checksum-pinned distribution/sysroot");
  benchmark::AddCustomContext("apgar_cuda_host_toolchain",
                              "GCC 15.2.0 checksum-pinned distribution/sysroot");
  struct utsname host{};
  if (uname(&host) == 0) {
    benchmark::AddCustomContext("apgar_host_kernel",
                                std::string(host.sysname) + " " + host.release);
    benchmark::AddCustomContext("apgar_host_architecture", host.machine);
  }
  if (const std::optional<std::string> os_release = ReadHostFile("/etc/os-release");
      os_release.has_value()) {
    if (const std::optional<std::string> pretty_name = LineValue(*os_release, "PRETTY_NAME=");
        pretty_name.has_value()) {
      benchmark::AddCustomContext("apgar_host_os", *pretty_name);
    }
  }
  if (const std::optional<std::string> cpu_info = ReadHostFile("/proc/cpuinfo");
      cpu_info.has_value()) {
    if (const std::optional<std::string> model = LineValue(*cpu_info, "model name");
        model.has_value()) {
      benchmark::AddCustomContext("apgar_cpu_model", *model);
    }
  }
  for (const apgar::benchmark::PlanarCorpusCase& test_case : context.corpus) {
    const apgar::geometry_compiler::CompilerProfile& profile = test_case.compiled_board.profile();
    benchmark::AddCustomContext(
        "apgar_case_" + test_case.name,
        "family=" + test_case.family + ";board_hash=" +
            std::to_string(test_case.board.content_hash()) + ";profile_fingerprint=" +
            std::to_string(test_case.compiled_board.compiler_profile_fingerprint()) + ";nodes=" +
            std::to_string(test_case.compiled_board.telemetry().represented_nodes) + ";edges=" +
            std::to_string(test_case.compiled_board.telemetry().legal_directional_edges) +
            ";step=" + std::to_string(profile.lattice_step) +
            ";tile=" + std::to_string(profile.tile_width_nodes) + "x" +
            std::to_string(profile.tile_height_nodes) +
            ";heading_mask=" + std::to_string(profile.heading_mask) +
            ";costs=" + std::to_string(profile.costs.orthogonal_step) + "," +
            std::to_string(profile.costs.diagonal_step) + "," + std::to_string(profile.costs.bend));
  }
}

void RegisterBenchmarks(BenchmarkContext* context) {
  for (std::size_t index = 0; index < context->corpus.size(); ++index) {
    const apgar::benchmark::PlanarCorpusCase* test_case = &context->corpus[index];
    const VisibleOutcome* oracle = &context->cpu_oracles[index];
    Configure(benchmark::RegisterBenchmark(("cpu_astar/" + test_case->name).c_str(), CpuBenchmark,
                                           test_case, oracle));
    Configure(benchmark::RegisterBenchmark(
        ("cuda_frontier/" + test_case->name).c_str(), GpuBenchmark, test_case, oracle,
        apgar::gpu::PlanarGenerator::kBucketedFrontier, context->backend.get()));
    Configure(benchmark::RegisterBenchmark(
        ("cuda_sweep/" + test_case->name).c_str(), GpuBenchmark, test_case, oracle,
        apgar::gpu::PlanarGenerator::kHeadingAwareSweep, context->backend.get()));
  }
}

}  // namespace

int main(int argc, char** argv) {
  benchmark::MaybeReenterWithoutASLR(argc, argv);
  const std::optional<std::string> commit = ExtractCommit(&argc, argv);
  if (!commit.has_value() || commit->empty()) {
    std::cerr << "planar_benchmark requires --apgar_commit=HEX\n";
    return 2;
  }
  std::optional<BenchmarkContext> context = BuildContext();
  if (!context.has_value()) {
    std::cerr << "failed to build Phase 2 benchmark context\n";
    return 2;
  }
  AddContext(*commit, *context);
  RegisterBenchmarks(&*context);
  benchmark::Initialize(&argc, argv);
  if (benchmark::ReportUnrecognizedArguments(argc, argv)) {
    return 2;
  }
  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();
  return 0;
}
