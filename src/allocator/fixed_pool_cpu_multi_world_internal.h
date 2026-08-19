#ifndef APGAR_SRC_ALLOCATOR_FIXED_POOL_CPU_MULTI_WORLD_INTERNAL_H_
#define APGAR_SRC_ALLOCATOR_FIXED_POOL_CPU_MULTI_WORLD_INTERNAL_H_

#include <cstddef>
#include <cstdint>
#include <span>

#include "apgar/allocator/fixed_pool_cpu_multi_world.h"

namespace apgar::allocator::internal {

struct FixedPoolCpuMultiWorldKnownWork {
  std::uint64_t world_count = 0;
  std::uint64_t total_selection_rounds = 0;
  std::uint64_t total_price_updates = 0;
  std::uint64_t source_net_count = 0;
  std::uint64_t source_candidate_count = 0;
  std::uint64_t source_candidate_resource_uses = 0;
  std::uint64_t maximum_selected_resource_uses_per_round = 0;
  std::uint64_t maximum_price_entries_per_world = 0;
};

struct FixedPoolCpuMultiWorldWorkProjection {
  std::uint64_t candidate_evaluations = 0;
  std::uint64_t candidate_resource_visits = 0;
  std::uint64_t selected_resource_uses = 0;
  std::uint64_t net_outcomes = 0;
  std::uint64_t emitted_price_entries = 0;
  std::uint64_t trace_records = 0;
  std::uint64_t buffered_terminal_selection_records = 0;
  std::uint64_t buffered_terminal_resource_records = 0;
  std::uint64_t buffered_terminal_price_records = 0;
  std::uint64_t pareto_comparisons = 0;
};

enum class FixedPoolCpuMultiWorldProjectionResult : std::uint8_t {
  kFits = 0,
  kBoundExhausted = 1,
  kArithmeticOverflow = 2,
};

// Allocation-free widened projection used by production preflight and exact
// equality/overflow tests.
[[nodiscard]] FixedPoolCpuMultiWorldProjectionResult ProjectFixedPoolCpuMultiWorldKnownWork(
    const FixedPoolCpuMultiWorldKnownWork& work, const FixedPoolCpuMultiWorldConfig& config,
    FixedPoolCpuMultiWorldWorkProjection* projection) noexcept;

[[nodiscard]] bool FixedPoolCpuWorldDominatesForTesting(
    const FixedPoolCpuWorldObjective& left, const FixedPoolCpuWorldObjective& right) noexcept;

[[nodiscard]] bool FixedPoolCpuWorldPreferredBeforeForTesting(
    const FixedPoolCpuWorldSummary& left, const FixedPoolCpuWorldSummary& right) noexcept;

using FixedPoolCpuMultiWorldBeforeWorldHook = bool (*)(std::size_t world_index,
                                                       void* context) noexcept;

struct FixedPoolCpuMultiWorldTestHooks {
  FixedPoolCpuMultiWorldBeforeWorldHook before_world = nullptr;
  void* context = nullptr;
};

[[nodiscard]] FixedPoolCpuMultiWorldResult ExecuteFixedPoolCpuMultiWorldWithTestHooks(
    const board_ir::BoardSnapshot& board, const ResourceCapacityModel& capacities,
    const CpuCandidateAllocationSession& source, CpuCandidateAllocationSessionConfig source_config,
    std::span<const FixedPoolCpuWorldSchedule> schedules, FixedPoolCpuMultiWorldConfig config,
    FixedPoolCpuMultiWorldTestHooks hooks);

}  // namespace apgar::allocator::internal

#endif  // APGAR_SRC_ALLOCATOR_FIXED_POOL_CPU_MULTI_WORLD_INTERNAL_H_
