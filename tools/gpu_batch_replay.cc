#include <array>
#include <charconv>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

#include "apgar/benchmark/planar_corpus.h"
#include "apgar/board_ir/stable_hash.h"
#include "apgar/candidates/gpu_candidate_adapter.h"
#include "apgar/gpu/cuda_backend.h"
#include "apgar/gpu/fault_injecting_backend.h"
#include "apgar/gpu/planar_router.h"
#include "apgar/routing/candidate_policy.h"
#include "apgar/tooling/runfiles.h"

namespace {

using apgar::gpu::UntrustedCandidateBatchResultFault;

struct FaultContract {
  std::string_view name;
  UntrustedCandidateBatchResultFault fault;
  std::string_view expected_failure;
  std::string_view invariant;
};

inline constexpr std::array<FaultContract, 8> kFaultContracts{{
    {"workspace_bounds", UntrustedCandidateBatchResultFault::kWorkspaceBounds, "internal_invariant",
     "gpu.batch.workspace.bounds.v1"},
    {"workspace_owner", UntrustedCandidateBatchResultFault::kWorkspaceOwner, "internal_invariant",
     "gpu.batch.workspace.owner.v1"},
    {"query_telemetry", UntrustedCandidateBatchResultFault::kQueryTelemetry, "internal_invariant",
     "gpu.batch.query.telemetry.v1"},
    {"memory_accounting", UntrustedCandidateBatchResultFault::kMemoryAccounting,
     "internal_invariant", "gpu.batch.memory.accounting.v1"},
    {"batch_telemetry", UntrustedCandidateBatchResultFault::kBatchTelemetry, "internal_invariant",
     "gpu.batch.telemetry.v1"},
    {"query_identity", UntrustedCandidateBatchResultFault::kQueryIdentity, "internal_invariant",
     "gpu.batch.query.identity.v1"},
    {"false_disconnected", UntrustedCandidateBatchResultFault::kFalseDisconnected,
     "internal_invariant", "gpu.disconnected.cpu_reachable.v1"},
    {"unauthenticated_cuda_producer", UntrustedCandidateBatchResultFault::kNone, "unsupported",
     "candidate.builder.gpu_producer_authentication.v1"},
}};

inline constexpr std::array<std::string_view, 8> kDefaultArtifacts{{
    "replays/gpu/batch_workspace_bounds_v1.replay",
    "replays/gpu/batch_workspace_owner_v1.replay",
    "replays/gpu/batch_query_telemetry_v1.replay",
    "replays/gpu/batch_memory_accounting_v1.replay",
    "replays/gpu/batch_telemetry_v1.replay",
    "replays/gpu/batch_query_identity_v1.replay",
    "replays/gpu/batch_false_disconnected_v1.replay",
    "replays/gpu/batch_unauthenticated_cuda_producer_v1.replay",
}};

struct ReplayArtifact {
  std::string fixture;
  std::uint64_t fixture_checksum = 0;
  apgar::gpu::PlanarGenerator generator = apgar::gpu::PlanarGenerator::kBucketedFrontier;
  UntrustedCandidateBatchResultFault fault = UntrustedCandidateBatchResultFault::kWorkspaceBounds;
  apgar::board_ir::LayerId layer = 0;
  std::uint32_t maximum_rounds = 0;
  std::uint64_t maximum_workspace_states_per_query = 0;
  std::uint64_t maximum_frontier_states_per_query = 0;
  std::uint64_t maximum_device_bytes = 0;
  std::uint64_t maximum_host_bytes = 0;
  std::uint64_t batch_id = 0;
  std::uint64_t query_id = 0;
  std::uint32_t input_ordinal = 0;
  apgar::routing::CandidateGenerationPolicy candidate_policy;
  std::uint64_t expected_policy_identity = 0;
  std::uint64_t expected_board_hash = 0;
  std::uint64_t expected_profile_fingerprint = 0;
  std::uint32_t expected_compiler_version = 0;
  std::uint64_t expected_rule_bucket_identity = 0;
  std::uint64_t expected_routing_profile_fingerprint = 0;
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

[[nodiscard]] const FaultContract* FindFault(std::string_view name) {
  for (const FaultContract& contract : kFaultContracts) {
    if (contract.name == name) {
      return &contract;
    }
  }
  return nullptr;
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

  constexpr std::array<std::string_view, 34> kKeys{{
      "format=",
      "schema_version=",
      "device_candidate_batch_schema_version=",
      "fixture=",
      "fixture_checksum_fnv1a64=",
      "generator=",
      "fault=",
      "layer=",
      "maximum_rounds=",
      "maximum_workspace_states_per_query=",
      "maximum_frontier_states_per_query=",
      "maximum_device_bytes=",
      "maximum_host_bytes=",
      "batch_id=",
      "query_id=",
      "input_ordinal=",
      "policy_schema_version=",
      "objective=",
      "deterministic_seed=",
      "candidate_ordinal=",
      "orthogonal_step_surcharge=",
      "diagonal_step_surcharge=",
      "bend_surcharge=",
      "banned_resources=",
      "resource_penalties=",
      "expected_policy_identity=",
      "expected_board_content_hash=",
      "expected_compiler_profile_fingerprint=",
      "expected_compiler_version=",
      "expected_rule_bucket_identity=",
      "expected_routing_profile_fingerprint=",
      "expected_device_view_fingerprint=",
      "expected_failure=",
      "expected_invariant=",
  }};
  std::vector<std::string_view> values;
  values.reserve(kKeys.size());
  std::size_t offset = 0;
  const auto consume = [&](std::span<const std::string_view> keys) -> bool {
    for (std::string_view key : keys) {
      const std::size_t end = payload.find('\n', offset);
      if (end == std::string_view::npos) {
        *error = "replay payload is truncated";
        return false;
      }
      const std::string_view line = payload.substr(offset, end - offset);
      if (!line.starts_with(key)) {
        *error = "replay fields are missing or out of canonical order";
        return false;
      }
      values.push_back(line.substr(key.size()));
      offset = end + 1;
    }
    return true;
  };
  if (!consume(kKeys) || offset != payload.size()) {
    if (error->empty()) {
      *error = "replay payload has unknown fields";
    }
    return std::nullopt;
  }

  const FaultContract* fault = FindFault(values[6]);
  if (values[0] != "apgar_gpu_batch_invariant_replay" || values[1] != "1" || values[2] != "1" ||
      values[5] != "bucketed_frontier" || fault == nullptr || values[16] != "1" ||
      values[17] != "base_scalar_cost" || values[23] != "none" || values[24] != "none" ||
      values[32] != fault->expected_failure || values[33] != fault->invariant) {
    *error = "replay format, schema, generator, policy, fault, or expected outcome is unsupported";
    return std::nullopt;
  }

  ReplayArtifact artifact;
  artifact.fixture = values[3];
  artifact.fault = fault->fault;
  artifact.expected_invariant = std::string(values[33]);
  artifact.candidate_policy.objective = apgar::routing::CandidateObjective::kBaseScalarCost;
  if (!ParseUnsigned(values[4], &artifact.fixture_checksum) ||
      !ParseUnsigned(values[7], &artifact.layer) ||
      !ParseUnsigned(values[8], &artifact.maximum_rounds) || artifact.maximum_rounds == 0 ||
      !ParseUnsigned(values[9], &artifact.maximum_workspace_states_per_query) ||
      !ParseUnsigned(values[10], &artifact.maximum_frontier_states_per_query) ||
      !ParseUnsigned(values[11], &artifact.maximum_device_bytes) ||
      !ParseUnsigned(values[12], &artifact.maximum_host_bytes) ||
      !ParseUnsigned(values[13], &artifact.batch_id) || artifact.batch_id == 0 ||
      !ParseUnsigned(values[14], &artifact.query_id) || artifact.query_id == 0 ||
      !ParseUnsigned(values[15], &artifact.input_ordinal) ||
      !ParseUnsigned(values[16], &artifact.candidate_policy.schema_version) ||
      !ParseUnsigned(values[18], &artifact.candidate_policy.deterministic_seed) ||
      !ParseUnsigned(values[19], &artifact.candidate_policy.candidate_ordinal) ||
      !ParseUnsigned(values[20], &artifact.candidate_policy.orthogonal_step_surcharge) ||
      !ParseUnsigned(values[21], &artifact.candidate_policy.diagonal_step_surcharge) ||
      !ParseUnsigned(values[22], &artifact.candidate_policy.bend_surcharge) ||
      !ParseUnsigned(values[25], &artifact.expected_policy_identity) ||
      !ParseUnsigned(values[26], &artifact.expected_board_hash) ||
      !ParseUnsigned(values[27], &artifact.expected_profile_fingerprint) ||
      !ParseUnsigned(values[28], &artifact.expected_compiler_version) ||
      !ParseUnsigned(values[29], &artifact.expected_rule_bucket_identity) ||
      !ParseUnsigned(values[30], &artifact.expected_routing_profile_fingerprint) ||
      !ParseUnsigned(values[31], &artifact.expected_device_fingerprint)) {
    *error = "replay contains an invalid integer field";
    return std::nullopt;
  }
  return artifact;
}

int Replay(std::string_view artifact_path) {
  const std::optional<std::string> artifact_contents = apgar::tooling::ReadRunfile(artifact_path);
  if (!artifact_contents.has_value()) {
    std::cerr << artifact_path << ": unable to read replay artifact\n";
    return 2;
  }
  std::string parse_error;
  const std::optional<ReplayArtifact> artifact = ParseArtifact(*artifact_contents, &parse_error);
  if (!artifact.has_value()) {
    std::cerr << artifact_path << ": " << parse_error << '\n';
    return 2;
  }
  const std::optional<std::string> fixture = apgar::tooling::ReadRunfile(artifact->fixture);
  if (!fixture.has_value() ||
      apgar::board_ir::StableHashString(*fixture) != artifact->fixture_checksum) {
    std::cerr << artifact_path << ": fixture is missing or its checksum differs\n";
    return 2;
  }

  apgar::benchmark::PlanarCorpusCaseResult case_result =
      apgar::benchmark::BuildPlanarBakeoffKicadCaseV1(*fixture);
  if (!std::holds_alternative<apgar::benchmark::PlanarCorpusCase>(case_result)) {
    std::cerr << artifact_path << ": " << std::get<std::string>(case_result) << '\n';
    return 2;
  }
  const apgar::benchmark::PlanarCorpusCase& replay_case =
      std::get<apgar::benchmark::PlanarCorpusCase>(case_result);
  const apgar::board_ir::BoardSnapshot& board = replay_case.board;
  const apgar::geometry_compiler::CompiledBoard& compiled = replay_case.compiled_board;
  if (replay_case.request.start_layer != artifact->layer ||
      replay_case.request.goal_layer != artifact->layer) {
    std::cerr << artifact_path << ": replay layer differs from the versioned corpus case\n";
    return 2;
  }

  apgar::gpu::DeviceCompiledBoardResult device_result =
      apgar::gpu::BuildDeviceCompiledBoardV1(board, compiled);
  if (!std::holds_alternative<apgar::gpu::DeviceCompiledBoardV1>(device_result)) {
    std::cerr << artifact_path << ": device-view compilation failed\n";
    return 2;
  }
  const apgar::gpu::DeviceCompiledBoardV1& device =
      std::get<apgar::gpu::DeviceCompiledBoardV1>(device_result);
  const std::uint64_t routing_profile_fingerprint =
      apgar::routing::FingerprintRoutingProfile(board.data().routing_profile);
  const apgar::routing::CandidatePolicyResult normalized_result =
      apgar::routing::NormalizeCandidateGenerationPolicy(compiled, artifact->candidate_policy);
  if (!std::holds_alternative<apgar::routing::NormalizedCandidateGenerationPolicy>(
          normalized_result)) {
    std::cerr << artifact_path << ": replay candidate policy no longer normalizes\n";
    return 2;
  }
  const apgar::routing::NormalizedCandidateGenerationPolicy& normalized =
      std::get<apgar::routing::NormalizedCandidateGenerationPolicy>(normalized_result);
  if (board.content_hash() != artifact->expected_board_hash ||
      compiled.compiler_profile_fingerprint() != artifact->expected_profile_fingerprint ||
      compiled.compiler_version() != artifact->expected_compiler_version ||
      compiled.rule_bucket().identity != artifact->expected_rule_bucket_identity ||
      routing_profile_fingerprint != artifact->expected_routing_profile_fingerprint ||
      device.header.device_view_fingerprint != artifact->expected_device_fingerprint ||
      normalized.identity != artifact->expected_policy_identity) {
    std::cerr << artifact_path
              << ": replay semantic associations differ: board=" << board.content_hash()
              << " compiler_profile=" << compiled.compiler_profile_fingerprint()
              << " compiler_version=" << compiled.compiler_version()
              << " rule_bucket=" << compiled.rule_bucket().identity
              << " routing_profile=" << routing_profile_fingerprint
              << " device=" << device.header.device_view_fingerprint
              << " policy=" << normalized.identity << '\n';
    return 2;
  }

  apgar::routing::PlanarRouteRequest request = replay_case.request;
  request.candidate_policy = normalized.policy;
  const std::array<apgar::gpu::PlanarCandidateBatchQuery, 1> queries{{
      apgar::gpu::PlanarCandidateBatchQuery{
          .query_id = artifact->query_id,
          .input_ordinal = artifact->input_ordinal,
          .request = std::move(request),
      },
  }};
  std::unique_ptr<apgar::gpu::IPlanarRouteBackend> cuda_backend =
      apgar::gpu::CreateCudaPlanarRouteBackend();
  if (cuda_backend == nullptr) {
    std::cerr << artifact_path << ": CUDA backend factory returned null\n";
    return 2;
  }
  std::unique_ptr<apgar::gpu::IPlanarRouteBackend> backend =
      apgar::gpu::CreateFaultInjectingCandidateBatchBackend(*cuda_backend, artifact->fault);
  if (backend == nullptr) {
    std::cerr << artifact_path << ": fault-decorator factory returned null\n";
    return 2;
  }
  apgar::gpu::PlanarCandidateBatchResult result =
      apgar::gpu::RouteCandidateBatchWithPlanarGpuBackend(
          board, compiled, queries,
          apgar::gpu::PlanarCandidateBatchPolicy{
              .batch_id = artifact->batch_id,
              .generator = artifact->generator,
              .maximum_rounds = artifact->maximum_rounds,
              .maximum_workspace_states_per_query = artifact->maximum_workspace_states_per_query,
              .maximum_frontier_states_per_query = artifact->maximum_frontier_states_per_query,
              .maximum_device_bytes = artifact->maximum_device_bytes,
              .maximum_host_bytes = artifact->maximum_host_bytes,
          },
          *backend);
  if (!std::holds_alternative<apgar::gpu::PlanarCandidateBatch>(result)) {
    const apgar::gpu::PlanarGpuFailure& failure = std::get<apgar::gpu::PlanarGpuFailure>(result);
    std::cerr << artifact_path << ": batch failed before query classification: " << failure.detail
              << '\n';
    return 1;
  }
  const apgar::gpu::PlanarCandidateBatch& batch =
      std::get<apgar::gpu::PlanarCandidateBatch>(result);
  if (artifact->fault == UntrustedCandidateBatchResultFault::kNone) {
    if (batch.batch_id != artifact->batch_id || batch.items.size() != 1 ||
        batch.items.front().query_id() != artifact->query_id ||
        batch.items.front().input_ordinal() != artifact->input_ordinal ||
        batch.items.front().policy_identity() != artifact->expected_policy_identity ||
        !batch.items.front().has_validated_route_evidence() ||
        batch.items.front().has_authenticated_cuda_producer_evidence() ||
        !std::holds_alternative<apgar::gpu::PlanarGpuRoute>(batch.items.front().result())) {
      std::cerr << artifact_path
                << ": expected one host-validated but unauthenticated route was not produced\n";
      return 1;
    }
    const apgar::candidates::CandidateDraftBuildResult draft =
        apgar::candidates::BuildGeneratedCandidateFromGpuBatchItem(
            board, compiled, queries.front(), normalized, batch, batch.items.front());
    if (!std::holds_alternative<apgar::candidates::CandidateRejection>(draft)) {
      std::cerr << artifact_path
                << ": unauthenticated CUDA-looking producer unexpectedly minted a candidate\n";
      return 1;
    }
    const apgar::candidates::CandidateRejection& rejection =
        std::get<apgar::candidates::CandidateRejection>(draft);
    if (rejection.code != apgar::candidates::CandidateRejectionCode::kUnsupported ||
        rejection.invariant_id != artifact->expected_invariant) {
      std::cerr << artifact_path << ": expected candidate provenance rejection was not reproduced: "
                << "code=" << static_cast<unsigned>(rejection.code)
                << " invariant=" << rejection.invariant_id << " detail=" << rejection.detail
                << '\n';
      return 1;
    }
    std::cout << "reproduced=unsupported invariant=" << artifact->expected_invariant
              << " artifact_schema=1 backend=" << batch.backend.backend << " device=\""
              << batch.backend.device_name
              << "\" producer_authentication=generic_wrapper_rejected\n";
    return 0;
  }
  if (batch.batch_id != artifact->batch_id || batch.items.size() != 1 ||
      batch.items.front().query_id() != artifact->query_id ||
      batch.items.front().input_ordinal() != artifact->input_ordinal ||
      batch.items.front().policy_identity() != artifact->expected_policy_identity ||
      !std::holds_alternative<apgar::gpu::PlanarGpuFailure>(batch.items.front().result())) {
    std::cerr << artifact_path << ": expected one associated GPU query failure was not produced\n";
    return 1;
  }
  const apgar::gpu::PlanarGpuFailure& failure =
      std::get<apgar::gpu::PlanarGpuFailure>(batch.items.front().result());
  if (failure.code != apgar::gpu::PlanarGpuFailureCode::kInternalInvariant ||
      failure.invariant_id != artifact->expected_invariant) {
    std::cerr << artifact_path << ": expected GPU batch invariant was not reproduced: code="
              << static_cast<unsigned>(failure.code) << " invariant=" << failure.invariant_id
              << " detail=" << failure.detail << '\n';
    return 1;
  }
  std::cout << "reproduced=internal_invariant invariant=" << artifact->expected_invariant
            << " artifact_schema=1 backend=" << batch.backend.backend << " device=\""
            << batch.backend.device_name
            << "\" compute_capability=" << batch.backend.compute_capability_major << '.'
            << batch.backend.compute_capability_minor << '\n';
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  int exit_code = 0;
  const auto replay = [&](std::string_view artifact) {
    const int result = Replay(artifact);
    if (result == 2) {
      exit_code = 2;
    } else if (result != 0 && exit_code == 0) {
      exit_code = 1;
    }
  };
  if (argc == 1) {
    for (std::string_view artifact : kDefaultArtifacts) {
      replay(artifact);
    }
  } else {
    for (int index = 1; index < argc; ++index) {
      replay(argv[index]);
    }
  }
  return exit_code;
}
