#ifndef APGAR_SRC_ALLOCATOR_CPU_TARGETED_REGENERATION_EPOCH_INTERNAL_H_
#define APGAR_SRC_ALLOCATOR_CPU_TARGETED_REGENERATION_EPOCH_INTERNAL_H_

#include <cstddef>
#include <span>

#include "apgar/allocator/cpu_targeted_regeneration_epoch.h"
#include "apgar/candidates/route_candidate.h"
#include "apgar/routing/cpu_astar.h"

namespace apgar::allocator::internal {

enum class CpuTargetedRegenerationInjectedFailure : std::uint8_t {
  kNone = 0,
  kResourceExhausted = 1,
  kInternalInvariant = 2,
};

struct CpuTargetedRegenerationEpochTestHooks {
  CpuTargetedRegenerationInjectedFailure (*before_target_route)(std::size_t, void*) = nullptr;
  void (*after_cpu_route)(std::size_t, routing::CpuRoute&, void*) = nullptr;
  void (*after_candidate_draft)(std::size_t, candidates::CandidateDraftBuildResult&,
                                void*) = nullptr;
  CpuTargetedRegenerationInjectedFailure (*after_local_publication)(void*) = nullptr;
  void* context = nullptr;
};

[[nodiscard]] CpuTargetedRegenerationEpochResult ExecuteCpuTargetedRegenerationEpochWithTestHooks(
    const board_ir::BoardSnapshot& board, const ResourceCapacityModel& capacities,
    std::span<const CpuTargetedRegenerationSourcePool> source_pools,
    const NegotiatedRegenerationPlan& plan, const NegotiatedPriceSnapshot* prior_prices,
    std::span<const CpuTargetedRegenerationNetContext> contexts,
    CpuTargetedRegenerationEpochConfig config, CpuTargetedRegenerationEpochTestHooks hooks);

}  // namespace apgar::allocator::internal

#endif  // APGAR_SRC_ALLOCATOR_CPU_TARGETED_REGENERATION_EPOCH_INTERNAL_H_
