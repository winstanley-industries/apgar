#ifndef APGAR_TOOLS_GPU_REPLAY_PARSER_H_
#define APGAR_TOOLS_GPU_REPLAY_PARSER_H_

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "apgar/board_ir/board.h"
#include "apgar/geometry_compiler/compiled_board.h"
#include "apgar/gpu/fault_injecting_backend.h"
#include "apgar/gpu/planar_router.h"

namespace apgar::tooling {

struct GpuReplayArtifactV1 {
  std::string fixture;
  std::uint64_t fixture_checksum = 0;
  gpu::PlanarGenerator generator = gpu::PlanarGenerator::kBucketedFrontier;
  gpu::UntrustedResultFault fault = gpu::UntrustedResultFault::kGoalPredecessorSelfCycle;
  board_ir::LayerId layer = 0;
  std::uint32_t maximum_rounds = 0;
  std::uint64_t expected_board_hash = 0;
  std::uint64_t expected_profile_fingerprint = 0;
  std::uint64_t expected_routing_profile_fingerprint = 0;
  std::uint64_t expected_device_fingerprint = 0;
  std::string expected_invariant;
};

struct GpuReplaySemanticAssociationsV1 {
  std::uint64_t board_hash = 0;
  std::uint64_t compiler_profile_fingerprint = 0;
  std::uint64_t routing_profile_fingerprint = 0;
  std::uint64_t device_view_fingerprint = 0;
};

struct GpuReplaySemanticAssociationMismatchesV1 {
  bool board = false;
  bool compiler_profile = false;
  bool routing_profile = false;
  bool device_view = false;

  [[nodiscard]] bool any() const noexcept {
    return board || compiler_profile || routing_profile || device_view;
  }

  bool operator==(const GpuReplaySemanticAssociationMismatchesV1&) const = default;
};

[[nodiscard]] std::optional<GpuReplayArtifactV1> ParseGpuReplayArtifactV1(std::string_view contents,
                                                                          std::string* error);

[[nodiscard]] GpuReplaySemanticAssociationsV1 BuildGpuReplaySemanticAssociationsV1(
    const board_ir::BoardSnapshot& board, const geometry_compiler::CompiledBoard& compiled_board,
    const gpu::DeviceCompiledBoardV1& device_board) noexcept;

[[nodiscard]] GpuReplaySemanticAssociationMismatchesV1 FindGpuReplaySemanticAssociationMismatchesV1(
    const GpuReplayArtifactV1& artifact, const GpuReplaySemanticAssociationsV1& actual) noexcept;

[[nodiscard]] bool GpuReplaySemanticAssociationsMatchV1(
    const GpuReplayArtifactV1& artifact, const GpuReplaySemanticAssociationsV1& actual) noexcept;

}  // namespace apgar::tooling

#endif  // APGAR_TOOLS_GPU_REPLAY_PARSER_H_
