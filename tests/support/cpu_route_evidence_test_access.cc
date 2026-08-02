#include "tests/support/cpu_route_evidence_test_access.h"

#include <memory>

#include "apgar/routing/candidate_policy.h"
#include "src/routing/cpu_astar_internal.h"

namespace apgar::test_support {

void ResealCpuRouteEvidenceForTest(
    routing::CpuRoute& route, const geometry_compiler::CompiledBoard& producing_compiled_board) {
  route.producer_evidence.evidence =
      std::make_shared<const routing::CpuRouteProducerEvidence>(routing::CpuRouteProducerEvidence{
          .source_board_content_hash = route.source_board_content_hash,
          .compiler_profile_fingerprint = route.compiler_profile_fingerprint,
          .compiler_version = route.compiler_version,
          .routing_profile_fingerprint = routing::FingerprintRoutingProfile(
              producing_compiled_board.prepared_routing_profile().profile()),
          .rule_bucket_identity = route.rule_bucket_identity,
          .candidate_policy_identity = route.candidate_policy_identity,
          .total_cost = route.total_cost,
          .segments = route.segments,
      });
}

}  // namespace apgar::test_support
