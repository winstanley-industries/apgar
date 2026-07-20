#ifndef APGAR_SRC_ROUTING_CPU_ASTAR_INTERNAL_H_
#define APGAR_SRC_ROUTING_CPU_ASTAR_INTERNAL_H_

#include <vector>

#include "apgar/routing/cpu_astar.h"

namespace apgar::routing {

struct CpuRouteProducerEvidence {
  std::uint64_t source_board_content_hash = 0;
  std::uint64_t compiler_profile_fingerprint = 0;
  std::uint32_t compiler_version = 0;
  std::uint64_t rule_bucket_identity = 0;
  std::uint64_t candidate_policy_identity = 0;
  std::uint64_t total_cost = 0;
  std::vector<LayerSegment> segments;
};

}  // namespace apgar::routing

#endif  // APGAR_SRC_ROUTING_CPU_ASTAR_INTERNAL_H_
