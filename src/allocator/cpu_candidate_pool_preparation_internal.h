#ifndef APGAR_SRC_ALLOCATOR_CPU_CANDIDATE_POOL_PREPARATION_INTERNAL_H_
#define APGAR_SRC_ALLOCATOR_CPU_CANDIDATE_POOL_PREPARATION_INTERNAL_H_

#include <cstddef>
#include <span>

#include "apgar/allocator/cpu_candidate_pool_preparation.h"

namespace apgar::allocator::internal {

enum class CpuCandidatePoolInjectedWorkerFailure : std::uint8_t {
  kNone = 0,
  kResourceExhausted = 1,
  kInternalInvariant = 2,
};

struct CpuCandidatePoolPreparationTestHooks {
  void (*after_cpu_route)(std::size_t canonical_query_index, routing::CpuRoute& route,
                          void* context) noexcept = nullptr;
  void (*before_worker_completion)(std::size_t canonical_query_index,
                                   void* context) noexcept = nullptr;
  CpuCandidatePoolInjectedWorkerFailure (*injected_worker_failure)(
      std::size_t canonical_query_index, void* context) noexcept = nullptr;
  void* context = nullptr;
};

[[nodiscard]] CpuCandidatePoolPreparationResult PrepareCpuCandidatePoolsWithTestHooks(
    const board_ir::BoardSnapshot& board, std::span<const CpuCandidatePoolNetSchedule> schedules,
    CpuCandidatePoolPreparationConfig config, CpuCandidatePoolPreparationTestHooks hooks);

}  // namespace apgar::allocator::internal

#endif  // APGAR_SRC_ALLOCATOR_CPU_CANDIDATE_POOL_PREPARATION_INTERNAL_H_
