#ifndef APGAR_SRC_ALLOCATOR_MULTI_WORLD_INTERNAL_H_
#define APGAR_SRC_ALLOCATOR_MULTI_WORLD_INTERNAL_H_

#include <cstdint>
#include <optional>
#include <span>

#include "apgar/allocator/multi_world.h"

namespace apgar::allocator::internal {

[[nodiscard]] bool MultiWorldExecutionConfigIsValidV1(
    const MultiWorldExecutionConfig& config) noexcept;

struct MultiWorldTerminalEnvelopeV1 {
  std::uint64_t buffered_selection_records = 0;
  std::uint64_t buffered_resource_records = 0;
  std::uint64_t buffered_price_records = 0;
  std::uint64_t retained_worlds = 0;
  std::uint64_t retained_selection_records = 0;
  std::uint64_t retained_resource_records = 0;
  std::uint64_t retained_price_records = 0;
  std::uint64_t retained_winner_pins = 0;

  friend bool operator==(const MultiWorldTerminalEnvelopeV1&,
                         const MultiWorldTerminalEnvelopeV1&) = default;
};

[[nodiscard]] bool MultiWorldTerminalEnvelopeFitsV1(
    std::uint64_t world_count, std::uint64_t source_net_count,
    std::uint64_t maximum_source_candidate_count, std::uint64_t maximum_resource_records_per_world,
    std::uint64_t maximum_price_records_per_world, const MultiWorldExecutionConfig& config,
    MultiWorldTerminalEnvelopeV1* projection) noexcept;

struct MultiWorldKnownWorkV1 {
  std::uint64_t world_count = 0;
  std::uint64_t total_selection_rounds = 0;
  std::uint64_t total_price_updates = 0;
  std::uint64_t source_net_count = 0;
  std::uint64_t source_candidate_count = 0;
  std::uint64_t source_candidate_span_count = 0;
  std::uint64_t source_resource_work_units_per_candidate_pass = 0;
};

struct MultiWorldKnownWorkProjectionV1 {
  // One fixed source-authentication pass plus every explicit and hidden
  // One-World candidate-processing pass.
  std::uint64_t charged_candidate_passes = 0;
  std::uint64_t candidate_evaluations = 0;
  std::uint64_t candidate_span_visits = 0;
  std::uint64_t resource_work_units = 0;
  std::uint64_t net_outcomes = 0;
  std::uint64_t pareto_comparisons = 0;

  friend bool operator==(const MultiWorldKnownWorkProjectionV1&,
                         const MultiWorldKnownWorkProjectionV1&) = default;
};

// Allocation-free checked projection for all work known before any pool or
// schedule bulk is copied. Round/update tuples must describe one initial round
// per world and one update between subsequent rounds; the all-zero refinement
// tuple still charges one source-authentication pass. Equality is accepted;
// invalid tuples, overflow, and one-over fail.
[[nodiscard]] bool MultiWorldKnownWorkFitsV1(const MultiWorldKnownWorkV1& work,
                                             const MultiWorldExecutionConfig& config,
                                             MultiWorldKnownWorkProjectionV1* projection) noexcept;

// Source-private observation of the most recent production preflight on this
// thread. It proves adversarial resource-span scans stop at the first
// deterministically rejected record without changing public telemetry.
[[nodiscard]] std::uint64_t MultiWorldPreflightSpanInspectionsForTesting() noexcept;

struct MultiWorldObjectiveV1 {
  std::uint64_t selected_net_count = 0;
  std::uint64_t total_overuse_units = 0;
  std::uint64_t total_intrinsic_cost = 0;
  std::uint64_t schedule_key = 0;

  friend bool operator==(const MultiWorldObjectiveV1&, const MultiWorldObjectiveV1&) = default;
};

[[nodiscard]] bool MultiWorldDominatesV1(const MultiWorldObjectiveV1& left,
                                         const MultiWorldObjectiveV1& right) noexcept;
[[nodiscard]] bool MultiWorldPreferredBeforeV1(const MultiWorldObjectiveV1& left,
                                               const MultiWorldObjectiveV1& right) noexcept;

struct MultiWorldPoolSnapshotChecksumHeaderV1 {
  std::uint32_t schema_version = 0;
  AllocationAssociations associations;
  std::uint64_t workload_checksum = 0;
  std::uint64_t capacity_model_checksum = 0;
  OneWorldAllocatorLimits allocator_limits;
  std::uint64_t candidate_pool_manifest_checksum = 0;
  std::uint64_t source_pool_count = 0;
  std::uint64_t source_candidate_count = 0;
};

[[nodiscard]] std::uint64_t ComputeMultiWorldPoolSnapshotChecksumV1(
    const MultiWorldPoolSnapshotChecksumHeaderV1& header) noexcept;

[[nodiscard]] std::uint64_t ComputeMultiWorldIdentityV1(
    std::uint64_t source_snapshot_checksum, std::uint64_t branch_state_checksum,
    const MultiWorldSchedule& schedule) noexcept;

struct MultiWorldRetainedReplayV1 {
  std::uint64_t world_identity = 0;
  std::uint64_t price_state_checksum = 0;
  std::uint64_t world_checksum = 0;

  friend bool operator==(const MultiWorldRetainedReplayV1&,
                         const MultiWorldRetainedReplayV1&) = default;
};

struct MultiWorldExecutionChecksumHeaderV1 {
  std::uint32_t schema_version = 0;
  std::uint64_t source_snapshot_checksum = 0;
  std::uint64_t branch_state_checksum = 0;
  MultiWorldExecutionConfig config;
  MultiWorldExecutionDisposition disposition = MultiWorldExecutionDisposition::kCompleted;
  MultiWorldExecutionCounters counters;
  std::optional<std::uint64_t> preferred_world_identity;
};

// Stable representation encoder used by production and golden tests. Runtime
// lease identities, addresses, timings, and execution interleaving are absent.
[[nodiscard]] std::uint64_t ComputeMultiWorldExecutionChecksumV1(
    const MultiWorldExecutionChecksumHeaderV1& header,
    std::span<const MultiWorldSchedule> schedules, std::span<const MultiWorldSummary> summaries,
    std::span<const MultiWorldRetainedReplayV1> retained_worlds) noexcept;

}  // namespace apgar::allocator::internal

#endif  // APGAR_SRC_ALLOCATOR_MULTI_WORLD_INTERNAL_H_
