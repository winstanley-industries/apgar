#ifndef APGAR_SRC_ALLOCATOR_TARGETED_REGENERATION_EXECUTION_INTERNAL_H_
#define APGAR_SRC_ALLOCATOR_TARGETED_REGENERATION_EXECUTION_INTERNAL_H_

#include <cstdint>
#include <span>

#include "apgar/allocator/targeted_regeneration_execution.h"

namespace apgar::allocator::internal {

[[nodiscard]] routing::PlanarRouteRequest BuildTargetedRegenerationRouteRequestV1(
    const routing::PlanarRouteRequest& source,
    const routing::CandidateGenerationPolicy& candidate_policy);

struct TargetedRegenerationExecutionChecksumHeaderV1 {
  std::uint32_t schema_version = 0;
  std::uint64_t plan_checksum = 0;
  TargetedRegenerationExecutionConfig config;
  std::uint64_t refreshed_request_manifest_checksum = 0;
  std::uint64_t refreshed_candidate_pool_manifest_checksum = 0;
  std::uint64_t baseline_world_checksum = 0;
  std::uint64_t refreshed_world_checksum = 0;
  TargetedRegenerationExecutionDisposition disposition =
      TargetedRegenerationExecutionDisposition::kNoWork;
  TargetedRegenerationTerminalReason terminal_reason =
      TargetedRegenerationTerminalReason::kNoTargets;
  TargetedRegenerationExecutionCounters counters;
};

[[nodiscard]] std::uint64_t ComputeTargetedRegenerationExecutionChecksumV1(
    const TargetedRegenerationExecutionChecksumHeaderV1& header,
    std::span<const TargetedRegenerationColumnRecord> columns) noexcept;

}  // namespace apgar::allocator::internal

#endif  // APGAR_SRC_ALLOCATOR_TARGETED_REGENERATION_EXECUTION_INTERNAL_H_
