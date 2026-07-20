#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include "apgar/benchmark/planar_corpus.h"
#include "apgar/board_ir/stable_hash.h"
#include "apgar/candidates/route_candidate.h"
#include "apgar/routing/candidate_policy.h"
#include "apgar/routing/cpu_astar.h"
#include "apgar/tooling/replay.h"
#include "apgar/tooling/runfiles.h"

namespace {

struct ReplayArtifact {
  std::string fixture;
  std::uint64_t fixture_checksum = 0;
  apgar::board_ir::LayerId layer = 0;
  std::uint64_t seed = 0;
  std::uint32_t candidate_ordinal = 0;
  std::uint64_t batch_identity = 0;
  std::uint64_t query_identity = 0;
  std::uint64_t board_hash = 0;
  std::uint64_t profile_fingerprint = 0;
  std::uint64_t routing_profile_fingerprint = 0;
  std::uint64_t rule_bucket_identity = 0;
  std::uint64_t policy_identity = 0;
  apgar::candidates::CandidateId candidate_id;
  std::uint64_t faulty_payload_checksum = 0;
  std::string expected_stage;
  std::string expected_invariant;
};

struct FaultyCandidate {
  apgar::benchmark::PlanarCorpusCase replay_case;
  apgar::routing::NormalizedCandidateGenerationPolicy policy;
  apgar::candidates::GeneratedRouteCandidate generated;
};

[[nodiscard]] std::optional<ReplayArtifact> ParseArtifact(std::string_view contents,
                                                          std::string* error) {
  constexpr std::string_view kKeys[] = {
      "format=",
      "schema_version=",
      "fixture=",
      "fixture_checksum_fnv1a64=",
      "board_schema_version=",
      "compiler_profile_schema_version=",
      "geometry_compiler_version=",
      "candidate_schema_major=",
      "candidate_schema_minor=",
      "geometry_schema_version=",
      "resource_schema_version=",
      "policy_schema_version=",
      "generator=",
      "generator_version=",
      "backend=",
      "supported_device_class=",
      "fault=",
      "layer=",
      "deterministic_seed=",
      "candidate_ordinal=",
      "batch_identity=",
      "query_identity=",
      "expected_board_content_hash=",
      "expected_profile_fingerprint=",
      "expected_routing_profile_fingerprint=",
      "expected_rule_bucket_identity=",
      "expected_policy_identity=",
      "expected_candidate_id_high=",
      "expected_candidate_id_low=",
      "expected_faulty_payload_checksum=",
      "expected_stage=",
      "expected_failure=",
      "expected_invariant=",
  };
  const std::optional<apgar::tooling::CanonicalReplayEnvelope> envelope =
      apgar::tooling::ParseCanonicalReplayEnvelope(contents, kKeys, error);
  if (!envelope.has_value()) {
    return std::nullopt;
  }
  const auto& values = envelope->values;
  if (values[0] != "apgar_candidate_failure_replay" || values[1] != "1" || values[4] != "1" ||
      values[5] != "1" || values[6] != "1" || values[7] != "1" || values[8] != "0" ||
      values[9] != "1" || values[10] != "1" || values[11] != "1" || values[12] != "cpu_astar" ||
      values[13] != "1" || values[14] != "cpu" ||
      values[15] != apgar::candidates::kCpuReferenceDeviceClassV1 ||
      values[16] != "resource_edge_count_increment" || values[30] != "resource_accounted" ||
      values[31] != "resource_mismatch" || values[32] != "candidate.resources.equivalence.v1") {
    *error =
        "replay format, schema associations, provenance, fault, or expected outcome is "
        "unsupported";
    return std::nullopt;
  }

  ReplayArtifact artifact;
  artifact.fixture = std::string(values[2]);
  artifact.expected_stage = std::string(values[30]);
  artifact.expected_invariant = std::string(values[32]);
  if (!apgar::tooling::ParseCanonicalUnsignedDecimal(values[3], &artifact.fixture_checksum) ||
      !apgar::tooling::ParseCanonicalUnsignedDecimal(values[17], &artifact.layer) ||
      !apgar::tooling::ParseCanonicalUnsignedDecimal(values[18], &artifact.seed) ||
      !apgar::tooling::ParseCanonicalUnsignedDecimal(values[19], &artifact.candidate_ordinal) ||
      !apgar::tooling::ParseCanonicalUnsignedDecimal(values[20], &artifact.batch_identity) ||
      artifact.batch_identity == 0 ||
      !apgar::tooling::ParseCanonicalUnsignedDecimal(values[21], &artifact.query_identity) ||
      artifact.query_identity == 0 ||
      !apgar::tooling::ParseCanonicalUnsignedDecimal(values[22], &artifact.board_hash) ||
      !apgar::tooling::ParseCanonicalUnsignedDecimal(values[23], &artifact.profile_fingerprint) ||
      !apgar::tooling::ParseCanonicalUnsignedDecimal(values[24],
                                                     &artifact.routing_profile_fingerprint) ||
      !apgar::tooling::ParseCanonicalUnsignedDecimal(values[25], &artifact.rule_bucket_identity) ||
      !apgar::tooling::ParseCanonicalUnsignedDecimal(values[26], &artifact.policy_identity) ||
      !apgar::tooling::ParseCanonicalUnsignedDecimal(values[27], &artifact.candidate_id.high) ||
      !apgar::tooling::ParseCanonicalUnsignedDecimal(values[28], &artifact.candidate_id.low) ||
      !apgar::tooling::ParseCanonicalUnsignedDecimal(values[29],
                                                     &artifact.faulty_payload_checksum)) {
    *error = "replay contains an invalid integer field";
    return std::nullopt;
  }
  return artifact;
}

[[nodiscard]] std::optional<FaultyCandidate> BuildFaultyCandidate(
    std::string_view fixture, apgar::board_ir::LayerId layer, std::uint64_t seed,
    std::uint32_t candidate_ordinal, std::uint64_t batch_identity, std::uint64_t query_identity,
    std::string* error) {
  apgar::benchmark::PlanarCorpusCaseResult case_result =
      apgar::benchmark::BuildPlanarBakeoffKicadCaseV1(fixture);
  if (!std::holds_alternative<apgar::benchmark::PlanarCorpusCase>(case_result)) {
    *error = std::get<std::string>(case_result);
    return std::nullopt;
  }
  apgar::benchmark::PlanarCorpusCase replay_case =
      std::get<apgar::benchmark::PlanarCorpusCase>(std::move(case_result));
  replay_case.request.start_layer = layer;
  replay_case.request.goal_layer = layer;
  replay_case.request.candidate_policy.deterministic_seed = seed;
  replay_case.request.candidate_policy.candidate_ordinal = candidate_ordinal;

  apgar::routing::CandidatePolicyResult normalized_result =
      apgar::routing::NormalizeCandidateGenerationPolicy(replay_case.compiled_board,
                                                         replay_case.request.candidate_policy);
  if (!std::holds_alternative<apgar::routing::NormalizedCandidateGenerationPolicy>(
          normalized_result)) {
    *error = std::get<apgar::routing::CandidatePolicyError>(normalized_result).detail;
    return std::nullopt;
  }
  apgar::routing::NormalizedCandidateGenerationPolicy policy =
      std::get<apgar::routing::NormalizedCandidateGenerationPolicy>(std::move(normalized_result));
  replay_case.request.candidate_policy = policy.policy;
  apgar::routing::CpuRouteResult route_result = apgar::routing::RouteWithCpuAStar(
      replay_case.board, replay_case.compiled_board, replay_case.request);
  if (!std::holds_alternative<apgar::routing::CpuRoute>(route_result)) {
    *error = std::get<apgar::routing::RouteFailure>(route_result).detail;
    return std::nullopt;
  }
  apgar::candidates::CandidateDraftBuildResult draft_result =
      apgar::candidates::BuildGeneratedCandidateFromCpuRoute(
          replay_case.board, replay_case.compiled_board, replay_case.request, policy,
          std::get<apgar::routing::CpuRoute>(route_result),
          apgar::candidates::CandidateSchedulingIdentity{
              .batch_identity = batch_identity,
              .query_identity = query_identity,
          });
  if (!std::holds_alternative<apgar::candidates::GeneratedRouteCandidate>(draft_result)) {
    *error = std::get<apgar::candidates::CandidateRejection>(draft_result).detail;
    return std::nullopt;
  }
  apgar::candidates::GeneratedRouteCandidate generated =
      std::get<apgar::candidates::GeneratedRouteCandidate>(std::move(draft_result));
  if (generated.resources.empty() ||
      generated.resources.front().edge_count == std::numeric_limits<std::uint32_t>::max()) {
    *error = "generated replay candidate has no incrementable resource span";
    return std::nullopt;
  }
  ++generated.resources.front().edge_count;
  if (const std::optional<apgar::candidates::CandidateRejection> finalize_error =
          apgar::candidates::FinalizeGeneratedCandidateDraft(generated);
      finalize_error.has_value()) {
    *error = finalize_error->detail;
    return std::nullopt;
  }
  return FaultyCandidate{.replay_case = std::move(replay_case),
                         .policy = std::move(policy),
                         .generated = std::move(generated)};
}

[[nodiscard]] std::string CanonicalArtifactPayload(std::string_view fixture_path,
                                                   std::uint64_t fixture_checksum,
                                                   const FaultyCandidate& faulty) {
  const apgar::candidates::GeneratedRouteCandidate& candidate = faulty.generated;
  std::ostringstream output;
  output << "format=apgar_candidate_failure_replay\n"
         << "schema_version=1\n"
         << "fixture=" << fixture_path << '\n'
         << "fixture_checksum_fnv1a64=" << fixture_checksum << '\n'
         << "board_schema_version=" << apgar::board_ir::kBoardSchemaVersion << '\n'
         << "compiler_profile_schema_version="
         << apgar::geometry_compiler::kCompilerProfileSchemaVersion << '\n'
         << "geometry_compiler_version=" << apgar::geometry_compiler::kGeometryCompilerVersion
         << '\n'
         << "candidate_schema_major=" << apgar::candidates::kRouteCandidateSchemaMajor << '\n'
         << "candidate_schema_minor=" << apgar::candidates::kRouteCandidateSchemaMinor << '\n'
         << "geometry_schema_version=" << apgar::candidates::kCandidateGeometrySchemaVersion << '\n'
         << "resource_schema_version=" << apgar::candidates::kCandidateResourceSchemaVersion << '\n'
         << "policy_schema_version=" << apgar::routing::kCandidateGenerationPolicySchemaVersion
         << '\n'
         << "generator=cpu_astar\n"
         << "generator_version=1\n"
         << "backend=cpu\n"
         << "supported_device_class=" << apgar::candidates::kCpuReferenceDeviceClassV1 << '\n'
         << "fault=resource_edge_count_increment\n"
         << "layer=" << faulty.replay_case.request.start_layer << '\n'
         << "deterministic_seed=" << candidate.provenance.deterministic_seed << '\n'
         << "candidate_ordinal=" << candidate.provenance.candidate_ordinal << '\n'
         << "batch_identity=" << candidate.provenance.batch_identity << '\n'
         << "query_identity=" << candidate.provenance.query_identity << '\n'
         << "expected_board_content_hash=" << candidate.associations.board_content_hash << '\n'
         << "expected_profile_fingerprint=" << candidate.associations.compiler_profile_fingerprint
         << '\n'
         << "expected_routing_profile_fingerprint="
         << candidate.associations.routing_profile_fingerprint << '\n'
         << "expected_rule_bucket_identity=" << candidate.associations.rule_bucket_identity << '\n'
         << "expected_policy_identity=" << candidate.policy_identity << '\n'
         << "expected_candidate_id_high=" << candidate.id.high << '\n'
         << "expected_candidate_id_low=" << candidate.id.low << '\n'
         << "expected_faulty_payload_checksum=" << candidate.payload_checksum << '\n'
         << "expected_stage=resource_accounted\n"
         << "expected_failure=resource_mismatch\n"
         << "expected_invariant=candidate.resources.equivalence.v1\n";
  return output.str();
}

int EmitArtifact() {
  constexpr std::string_view kFixturePath = "tests/fixtures/m1_exactness.kicad_pcb";
  const std::optional<std::string> fixture = apgar::tooling::ReadRunfile(kFixturePath);
  if (!fixture.has_value()) {
    std::cerr << "unable to read replay fixture\n";
    return 2;
  }
  std::string error;
  const std::optional<FaultyCandidate> faulty = BuildFaultyCandidate(
      *fixture, 0, 0x504841534533ULL, 7, 0xBADC0FFEEULL, 0xC0FFEE123ULL, &error);
  if (!faulty.has_value()) {
    std::cerr << error << '\n';
    return 2;
  }
  const std::string payload =
      CanonicalArtifactPayload(kFixturePath, apgar::board_ir::StableHashString(*fixture), *faulty);
  std::cout << payload << "checksum_fnv1a64=" << apgar::board_ir::StableHashString(payload) << '\n';
  return 0;
}

int Replay(std::string_view artifact_path) {
  const std::optional<std::string> artifact_contents = apgar::tooling::ReadRunfile(artifact_path);
  if (!artifact_contents.has_value()) {
    std::cerr << "unable to read replay artifact\n";
    return 2;
  }
  std::string error;
  const std::optional<ReplayArtifact> artifact = ParseArtifact(*artifact_contents, &error);
  if (!artifact.has_value()) {
    std::cerr << error << '\n';
    return 2;
  }
  const std::optional<std::string> fixture = apgar::tooling::ReadRunfile(artifact->fixture);
  if (!fixture.has_value() ||
      apgar::board_ir::StableHashString(*fixture) != artifact->fixture_checksum) {
    std::cerr << "fixture is missing or its checksum differs\n";
    return 2;
  }
  const std::optional<FaultyCandidate> faulty =
      BuildFaultyCandidate(*fixture, artifact->layer, artifact->seed, artifact->candidate_ordinal,
                           artifact->batch_identity, artifact->query_identity, &error);
  if (!faulty.has_value()) {
    std::cerr << error << '\n';
    return 2;
  }
  const apgar::candidates::GeneratedRouteCandidate& candidate = faulty->generated;
  if (faulty->replay_case.board.data().schema_version != apgar::board_ir::kBoardSchemaVersion ||
      faulty->replay_case.compiled_board.profile().schema_version !=
          apgar::geometry_compiler::kCompilerProfileSchemaVersion ||
      candidate.schema_major != apgar::candidates::kRouteCandidateSchemaMajor ||
      candidate.schema_minor != apgar::candidates::kRouteCandidateSchemaMinor ||
      candidate.geometry_schema_version != apgar::candidates::kCandidateGeometrySchemaVersion ||
      candidate.resource_schema_version != apgar::candidates::kCandidateResourceSchemaVersion ||
      candidate.policy.schema_version != apgar::routing::kCandidateGenerationPolicySchemaVersion ||
      candidate.provenance.generator != apgar::candidates::CandidateGeneratorKind::kCpuAStar ||
      candidate.provenance.generator_version != 1 ||
      candidate.provenance.backend != apgar::candidates::CandidateBackendKind::kCpu ||
      candidate.provenance.supported_device_class !=
          apgar::candidates::kCpuReferenceDeviceClassV1 ||
      candidate.associations.board_content_hash != artifact->board_hash ||
      candidate.associations.compiler_profile_fingerprint != artifact->profile_fingerprint ||
      candidate.associations.routing_profile_fingerprint != artifact->routing_profile_fingerprint ||
      candidate.associations.rule_bucket_identity != artifact->rule_bucket_identity ||
      candidate.policy_identity != artifact->policy_identity ||
      candidate.id != artifact->candidate_id ||
      candidate.payload_checksum != artifact->faulty_payload_checksum) {
    std::cerr << "replay candidate associations, identity, or payload checksum differ\n";
    return 2;
  }
  apgar::candidates::CandidateAdmissionResult admitted = apgar::candidates::AdmitRouteCandidate(
      apgar::candidates::CandidateAdmissionContext{
          .board = faulty->replay_case.board,
          .compiled_board = faulty->replay_case.compiled_board,
          .request = faulty->replay_case.request,
      },
      candidate);
  if (!std::holds_alternative<apgar::candidates::CandidateRejection>(admitted)) {
    std::cerr << "corrupted candidate was unexpectedly admitted\n";
    return 1;
  }
  const apgar::candidates::CandidateRejection& rejection =
      std::get<apgar::candidates::CandidateRejection>(admitted);
  if (artifact->expected_stage != "resource_accounted" ||
      rejection.stage != apgar::candidates::CandidateLifecycleStage::kResourceAccounted ||
      rejection.code != apgar::candidates::CandidateRejectionCode::kResourceMismatch ||
      rejection.invariant_id != artifact->expected_invariant) {
    std::cerr << "expected candidate resource invariant was not reproduced: code="
              << static_cast<unsigned>(rejection.code) << " invariant=" << rejection.invariant_id
              << '\n';
    return 1;
  }
  std::cout << "reproduced=resource_mismatch invariant=" << rejection.invariant_id
            << " artifact_schema=1 candidate_id=" << candidate.id.high << ':' << candidate.id.low
            << '\n';
  return 0;
}

int SelfTestParser() {
  constexpr std::string_view kArtifactPath =
      "replays/candidates/resource_footprint_mismatch_v1.replay";
  const std::optional<std::string> canonical = apgar::tooling::ReadRunfile(kArtifactPath);
  if (!canonical.has_value()) {
    std::cerr << "unable to read replay artifact for parser self-test\n";
    return 2;
  }
  constexpr std::string_view kSeedKey = "deterministic_seed=";
  const std::size_t seed = canonical->find(kSeedKey);
  const std::size_t checksum = canonical->rfind("checksum_fnv1a64=");
  if (seed == std::string::npos || checksum == std::string::npos || seed >= checksum) {
    std::cerr << "canonical replay is missing parser self-test fields\n";
    return 2;
  }
  std::string noncanonical = canonical->substr(0, checksum);
  noncanonical.insert(seed + kSeedKey.size(), 1, '0');
  const std::uint64_t payload_checksum = apgar::board_ir::StableHashString(noncanonical);
  noncanonical += "checksum_fnv1a64=";
  noncanonical += std::to_string(payload_checksum);
  noncanonical += '\n';

  std::string error;
  if (ParseArtifact(noncanonical, &error).has_value() ||
      error != "replay contains an invalid integer field") {
    std::cerr << "replay parser accepted a noncanonical leading-zero decimal\n";
    return 1;
  }
  std::cout << "rejected=noncanonical_decimal artifact_schema=1\n";
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc == 2 && std::string_view(argv[1]) == "--emit") {
    return EmitArtifact();
  }
  if (argc == 2 && std::string_view(argv[1]) == "--self-test-parser") {
    return SelfTestParser();
  }
  const std::string_view artifact =
      argc == 2 ? std::string_view(argv[1])
                : std::string_view("replays/candidates/resource_footprint_mismatch_v1.replay");
  return Replay(artifact);
}
