#ifndef APGAR_SRC_ALLOCATOR_CPU_SEQUENTIAL_NEGOTIATED_ROUTING_INTERNAL_H_
#define APGAR_SRC_ALLOCATOR_CPU_SEQUENTIAL_NEGOTIATED_ROUTING_INTERNAL_H_

#include <cstddef>
#include <cstdint>

#include "apgar/allocator/cpu_sequential_negotiated_routing.h"

namespace apgar::allocator::internal {

enum class CpuSequentialInjectedFailure : std::uint8_t {
  kNone = 0,
  kResourceExhausted = 1,
  kInternalInvariant = 2,
};

struct CpuSequentialNegotiatedRoutingTestHooks {
  void* context = nullptr;
  void (*after_cpu_route)(std::size_t attempt_index, std::uint32_t pass_index,
                          board_ir::EntityRef net, routing::CpuRoute& route,
                          void* context) = nullptr;
  void (*after_candidate_build)(std::size_t attempt_index, std::uint32_t pass_index,
                                board_ir::EntityRef net,
                                candidates::CandidateDraftBuildResult& draft,
                                void* context) = nullptr;
  CpuSequentialInjectedFailure (*injected_failure)(std::size_t attempt_index,
                                                   std::uint32_t pass_index,
                                                   board_ir::EntityRef net,
                                                   void* context) = nullptr;
};

[[nodiscard]] CpuSequentialNegotiatedRoutingResult RouteCpuSequentialNegotiatedWithTestHooks(
    const board_ir::BoardSnapshot& board, const ResourceCapacityModel& capacities,
    std::span<const CpuSequentialNetRequest> nets, CpuSequentialNegotiatedRoutingConfig config,
    CpuSequentialNegotiatedRoutingTestHooks hooks);

}  // namespace apgar::allocator::internal

#endif  // APGAR_SRC_ALLOCATOR_CPU_SEQUENTIAL_NEGOTIATED_ROUTING_INTERNAL_H_
