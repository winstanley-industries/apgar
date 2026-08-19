#ifndef APGAR_SRC_ALLOCATOR_CPU_CANDIDATE_ALLOCATION_SESSION_INTERNAL_H_
#define APGAR_SRC_ALLOCATOR_CPU_CANDIDATE_ALLOCATION_SESSION_INTERNAL_H_

#include <cstddef>
#include <cstdint>
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

enum class CpuCandidateAllocationSessionReplayFault : std::uint8_t {
  kShape = 0,
  kPoolOrder = 1,
  kCandidateOrder = 2,
  kFinalPoolIdentity = 3,
  kFinalSelection = 4,
  kStepLineage = 5,
  kCounterShape = 6,
  kCounterAgreement = 7,
  kTerminalStep = 8,
  kFixedPointShape = 9,
  kSessionBoundShape = 10,
  kEpochBoundShape = 11,
  kTerminalPlan = 12,
  kSessionIdentity = 13,
};

// Test-only private-state fault injection for direct replay-validator branch
// coverage. Returns false when the requested fault does not apply to the
// supplied valid session shape.
[[nodiscard]] bool InjectCpuCandidateAllocationSessionReplayFaultForTesting(
    CpuCandidateAllocationSession* session,
    CpuCandidateAllocationSessionReplayFault fault) noexcept;

[[nodiscard]] CpuCandidateAllocationSessionResult RunCpuCandidateAllocationSessionWithTestHooks(
    const board_ir::BoardSnapshot& board, const ResourceCapacityModel& capacities,
    std::span<const CpuTargetedRegenerationSourcePool> source_pools,
    std::span<const CpuTargetedRegenerationNetContext> contexts,
    CpuCandidateAllocationSessionConfig config, CpuCandidateAllocationSessionTestHooks hooks);

}  // namespace apgar::allocator::internal

#endif  // APGAR_SRC_ALLOCATOR_CPU_CANDIDATE_ALLOCATION_SESSION_INTERNAL_H_
