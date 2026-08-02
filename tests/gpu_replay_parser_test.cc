#include "tools/gpu_replay_parser.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include "apgar/benchmark/planar_corpus.h"
#include "apgar/board_ir/stable_hash.h"
#include "apgar/gpu/planar_router.h"
#include "apgar/routing/candidate_policy.h"
#include "apgar/tooling/runfiles.h"
#include "tests/support/board_builder.h"
#include "tests/support/compiler_builder.h"
#include "tests/support/google_test.h"
#include "tests/support/routing_builder.h"

namespace apgar::tooling {
namespace {

[[nodiscard]] std::string Rechecksum(std::string payload) {
  const std::uint64_t checksum = board_ir::StableHashString(payload);
  payload += "checksum_fnv1a64=";
  payload += std::to_string(checksum);
  payload += '\n';
  return payload;
}

TEST(GpuReplayParserTest, ParsesCanonicalV1AndRejectsMissingOrSwappedContextField) {
  constexpr std::string_view kArtifactPath = "replays/gpu/goal_predecessor_self_cycle_v1.replay";
  const std::optional<std::string> canonical = ReadRunfile(kArtifactPath);
  ASSERT_TRUE(canonical.has_value());
  std::string error;
  const std::optional<GpuReplayArtifactV1> artifact = ParseGpuReplayArtifactV1(*canonical, &error);
  ASSERT_TRUE(artifact.has_value()) << error;
  EXPECT_EQ(artifact->expected_routing_profile_fingerprint, 1820138087830637135U);
  EXPECT_EQ(artifact->expected_device_fingerprint, 2764614999745307367U);

  constexpr std::string_view kRoutingLine =
      "expected_routing_profile_fingerprint=1820138087830637135\n";
  constexpr std::string_view kDeviceLine = "expected_device_view_fingerprint=2764614999745307367\n";
  const std::size_t checksum = canonical->rfind("checksum_fnv1a64=");
  ASSERT_NE(checksum, std::string::npos);
  std::string payload = canonical->substr(0, checksum);
  const std::size_t routing = payload.find(kRoutingLine);
  ASSERT_NE(routing, std::string::npos);
  std::string missing = payload;
  missing.erase(routing, kRoutingLine.size());
  error.clear();
  EXPECT_FALSE(ParseGpuReplayArtifactV1(Rechecksum(std::move(missing)), &error).has_value());
  EXPECT_EQ(error, "replay fields are missing or out of canonical order");

  const std::size_t device = payload.find(kDeviceLine);
  ASSERT_EQ(device, routing + kRoutingLine.size());
  payload.replace(routing, kRoutingLine.size() + kDeviceLine.size(),
                  std::string(kDeviceLine) + std::string(kRoutingLine));
  error.clear();
  EXPECT_FALSE(ParseGpuReplayArtifactV1(Rechecksum(std::move(payload)), &error).has_value());
  EXPECT_EQ(error, "replay fields are missing or out of canonical order");

  std::string checksum_mismatch = *canonical;
  checksum_mismatch[routing + kRoutingLine.size() - 2] = '6';
  error.clear();
  EXPECT_FALSE(ParseGpuReplayArtifactV1(checksum_mismatch, &error).has_value());
  EXPECT_EQ(error, "replay payload checksum mismatch");
}

TEST(GpuReplayParserTest, SemanticAssociationRejectsRelabelAndUsesRetainedPreparedProfile) {
  constexpr std::string_view kCanonicalPath = "replays/gpu/goal_predecessor_self_cycle_v1.replay";
  constexpr std::string_view kNegativePath =
      "replays/gpu/goal_predecessor_self_cycle_wrong_routing_profile_v1.replay";
  const std::optional<std::string> canonical_contents = ReadRunfile(kCanonicalPath);
  const std::optional<std::string> negative_contents = ReadRunfile(kNegativePath);
  ASSERT_TRUE(canonical_contents.has_value());
  ASSERT_TRUE(negative_contents.has_value());
  std::string error;
  const std::optional<GpuReplayArtifactV1> canonical =
      ParseGpuReplayArtifactV1(*canonical_contents, &error);
  ASSERT_TRUE(canonical.has_value()) << error;
  error.clear();
  const std::optional<GpuReplayArtifactV1> negative =
      ParseGpuReplayArtifactV1(*negative_contents, &error);
  ASSERT_TRUE(negative.has_value()) << error;

  const std::optional<std::string> fixture = ReadRunfile(negative->fixture);
  ASSERT_TRUE(fixture.has_value());
  benchmark::PlanarCorpusCaseResult case_result =
      benchmark::BuildPlanarBakeoffKicadCaseV1(*fixture);
  ASSERT_TRUE(std::holds_alternative<benchmark::PlanarCorpusCase>(case_result));
  const benchmark::PlanarCorpusCase& replay_case =
      std::get<benchmark::PlanarCorpusCase>(case_result);
  gpu::DeviceCompiledBoardResult device_result =
      gpu::BuildDeviceCompiledBoardV1(replay_case.board, replay_case.compiled_board);
  ASSERT_TRUE(std::holds_alternative<gpu::DeviceCompiledBoardV1>(device_result));
  const GpuReplaySemanticAssociationsV1 checked_actual =
      BuildGpuReplaySemanticAssociationsV1(replay_case.board, replay_case.compiled_board,
                                           std::get<gpu::DeviceCompiledBoardV1>(device_result));
  EXPECT_TRUE(GpuReplaySemanticAssociationsMatchV1(*canonical, checked_actual));
  EXPECT_EQ(checked_actual.routing_profile_fingerprint,
            routing::FingerprintRoutingProfile(replay_case.board.data().routing_profile));

  GpuReplayArtifactV1 mismatched = *canonical;
  ++mismatched.expected_board_hash;
  EXPECT_EQ(FindGpuReplaySemanticAssociationMismatchesV1(mismatched, checked_actual),
            (GpuReplaySemanticAssociationMismatchesV1{.board = true}));
  mismatched = *canonical;
  ++mismatched.expected_profile_fingerprint;
  EXPECT_EQ(FindGpuReplaySemanticAssociationMismatchesV1(mismatched, checked_actual),
            (GpuReplaySemanticAssociationMismatchesV1{.compiler_profile = true}));
  mismatched = *canonical;
  ++mismatched.expected_routing_profile_fingerprint;
  EXPECT_EQ(FindGpuReplaySemanticAssociationMismatchesV1(mismatched, checked_actual),
            (GpuReplaySemanticAssociationMismatchesV1{.routing_profile = true}));
  mismatched = *canonical;
  ++mismatched.expected_device_fingerprint;
  EXPECT_EQ(FindGpuReplaySemanticAssociationMismatchesV1(mismatched, checked_actual),
            (GpuReplaySemanticAssociationMismatchesV1{.device_view = true}));

  EXPECT_EQ(negative->expected_board_hash, checked_actual.board_hash);
  EXPECT_EQ(negative->expected_profile_fingerprint, checked_actual.compiler_profile_fingerprint);
  EXPECT_EQ(negative->expected_device_fingerprint, checked_actual.device_view_fingerprint);
  EXPECT_NE(negative->expected_routing_profile_fingerprint,
            checked_actual.routing_profile_fingerprint);
  EXPECT_EQ(FindGpuReplaySemanticAssociationMismatchesV1(*negative, checked_actual),
            (GpuReplaySemanticAssociationMismatchesV1{.routing_profile = true}));
  EXPECT_FALSE(GpuReplaySemanticAssociationsMatchV1(*negative, checked_actual));

  const board_ir::BoardSnapshot multi = test_support::Snapshot(test_support::MultiNetM1BoardData());
  const geometry_compiler::CompiledBoard prepared = test_support::CompilePreparedNet(
      multi, multi.data().nets[1].ref, test_support::DefaultCompilerProfile({0}));
  gpu::DeviceCompiledBoardResult prepared_device_result =
      gpu::BuildDeviceCompiledBoardV1(multi, prepared);
  ASSERT_TRUE(std::holds_alternative<gpu::DeviceCompiledBoardV1>(prepared_device_result));
  const GpuReplaySemanticAssociationsV1 prepared_actual = BuildGpuReplaySemanticAssociationsV1(
      multi, prepared, std::get<gpu::DeviceCompiledBoardV1>(prepared_device_result));
  EXPECT_NE(prepared_actual.routing_profile_fingerprint,
            routing::FingerprintRoutingProfile(multi.data().routing_profile));

  GpuReplayArtifactV1 prepared_artifact = *negative;
  prepared_artifact.expected_board_hash = prepared_actual.board_hash;
  prepared_artifact.expected_profile_fingerprint = prepared_actual.compiler_profile_fingerprint;
  prepared_artifact.expected_device_fingerprint = prepared_actual.device_view_fingerprint;
  prepared_artifact.expected_routing_profile_fingerprint =
      routing::FingerprintRoutingProfile(multi.data().routing_profile);
  EXPECT_FALSE(GpuReplaySemanticAssociationsMatchV1(prepared_artifact, prepared_actual));
  prepared_artifact.expected_routing_profile_fingerprint =
      prepared_actual.routing_profile_fingerprint;
  EXPECT_TRUE(GpuReplaySemanticAssociationsMatchV1(prepared_artifact, prepared_actual));
}

}  // namespace
}  // namespace apgar::tooling
