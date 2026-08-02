#include "tools/gpu_replay_parser.h"

#include <iterator>
#include <optional>
#include <string>
#include <string_view>

#include "apgar/routing/candidate_policy.h"
#include "apgar/tooling/replay.h"

namespace apgar::tooling {

std::optional<GpuReplayArtifactV1> ParseGpuReplayArtifactV1(std::string_view contents,
                                                            std::string* error) {
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
      "expected_routing_profile_fingerprint=",
      "expected_device_view_fingerprint=",
      "expected_failure=",
      "expected_invariant=",
  };
  static_assert(std::size(kKeys) == 14);
  const std::optional<CanonicalReplayEnvelope> envelope =
      ParseCanonicalReplayEnvelope(contents, kKeys, error);
  if (!envelope.has_value()) {
    return std::nullopt;
  }
  const auto& values = envelope->values;
  if (values[0] != "apgar_gpu_invariant_replay" || values[1] != "1" ||
      values[4] != "bucketed_frontier" || values[5] != "goal_predecessor_self_cycle" ||
      values[12] != "internal_invariant" || values[13] != "gpu.predecessor.self_reference.v1") {
    *error = "replay format, schema, generator, fault, or expected outcome is unsupported";
    return std::nullopt;
  }

  GpuReplayArtifactV1 artifact;
  artifact.fixture = values[2];
  artifact.expected_invariant = std::string(values[13]);
  if (!ParseCanonicalUnsignedDecimal(values[3], &artifact.fixture_checksum) ||
      !ParseCanonicalUnsignedDecimal(values[6], &artifact.layer) ||
      !ParseCanonicalUnsignedDecimal(values[7], &artifact.maximum_rounds) ||
      artifact.maximum_rounds == 0 ||
      !ParseCanonicalUnsignedDecimal(values[8], &artifact.expected_board_hash) ||
      !ParseCanonicalUnsignedDecimal(values[9], &artifact.expected_profile_fingerprint) ||
      !ParseCanonicalUnsignedDecimal(values[10], &artifact.expected_routing_profile_fingerprint) ||
      !ParseCanonicalUnsignedDecimal(values[11], &artifact.expected_device_fingerprint)) {
    *error = "replay contains an invalid integer field";
    return std::nullopt;
  }
  return artifact;
}

GpuReplaySemanticAssociationsV1 BuildGpuReplaySemanticAssociationsV1(
    const board_ir::BoardSnapshot& board, const geometry_compiler::CompiledBoard& compiled_board,
    const gpu::DeviceCompiledBoardV1& device_board) noexcept {
  return GpuReplaySemanticAssociationsV1{
      .board_hash = board.content_hash(),
      .compiler_profile_fingerprint = compiled_board.compiler_profile_fingerprint(),
      .routing_profile_fingerprint =
          routing::FingerprintRoutingProfile(compiled_board.prepared_routing_profile().profile()),
      .device_view_fingerprint = device_board.header.device_view_fingerprint,
  };
}

GpuReplaySemanticAssociationMismatchesV1 FindGpuReplaySemanticAssociationMismatchesV1(
    const GpuReplayArtifactV1& artifact, const GpuReplaySemanticAssociationsV1& actual) noexcept {
  return GpuReplaySemanticAssociationMismatchesV1{
      .board = actual.board_hash != artifact.expected_board_hash,
      .compiler_profile =
          actual.compiler_profile_fingerprint != artifact.expected_profile_fingerprint,
      .routing_profile =
          actual.routing_profile_fingerprint != artifact.expected_routing_profile_fingerprint,
      .device_view = actual.device_view_fingerprint != artifact.expected_device_fingerprint,
  };
}

bool GpuReplaySemanticAssociationsMatchV1(const GpuReplayArtifactV1& artifact,
                                          const GpuReplaySemanticAssociationsV1& actual) noexcept {
  return !FindGpuReplaySemanticAssociationMismatchesV1(artifact, actual).any();
}

}  // namespace apgar::tooling
