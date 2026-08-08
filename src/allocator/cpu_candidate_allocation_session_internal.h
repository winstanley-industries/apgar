#ifndef APGAR_SRC_ALLOCATOR_CPU_CANDIDATE_ALLOCATION_SESSION_INTERNAL_H_
#define APGAR_SRC_ALLOCATOR_CPU_CANDIDATE_ALLOCATION_SESSION_INTERNAL_H_

#include <cstddef>
#include <span>

#include "apgar/allocator/cpu_candidate_allocation_session.h"

namespace apgar::allocator::internal {

using CpuCandidateAllocationBeforePlanningHook = void (*)(std::size_t planning_step,
                                                          NegotiatedPriceSnapshot* prior_snapshot,
                                                          void* context) noexcept;

struct CpuCandidateAllocationSessionTestHooks {
  CpuCandidateAllocationBeforePlanningHook before_planning = nullptr;
  void* context = nullptr;
};

[[nodiscard]] CpuCandidateAllocationSessionResult RunCpuCandidateAllocationSessionWithTestHooks(
    const board_ir::BoardSnapshot& board, const ResourceCapacityModel& capacities,
    std::span<const CpuTargetedRegenerationSourcePool> source_pools,
    std::span<const CpuTargetedRegenerationNetContext> contexts,
    CpuCandidateAllocationSessionConfig config, CpuCandidateAllocationSessionTestHooks hooks);

}  // namespace apgar::allocator::internal

#endif  // APGAR_SRC_ALLOCATOR_CPU_CANDIDATE_ALLOCATION_SESSION_INTERNAL_H_
