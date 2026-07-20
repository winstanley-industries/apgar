#ifndef APGAR_SRC_CANDIDATES_ROUTE_CANDIDATE_INTERNAL_H_
#define APGAR_SRC_CANDIDATES_ROUTE_CANDIDATE_INTERNAL_H_

#include <cstdint>
#include <span>
#include <string>

#include "apgar/candidates/route_candidate.h"

namespace apgar::candidates::internal {

enum class CandidateProducerAuthority : std::uint8_t {
  kCpuRoute = 0,
  kAuthenticatedCudaBatch = 1,
};

// Source-private bridge shared by typed evidence adapters. This deliberately
// remains outside the public include tree so callers cannot supply arbitrary
// generator/backend provenance through the candidate contract API.
[[nodiscard]] CandidateDraftBuildResult BuildGeneratedCandidateFromValidatedPlanarRoute(
    const board_ir::BoardSnapshot& board, const geometry_compiler::CompiledBoard& compiled_board,
    const routing::PlanarRouteRequest& request,
    const routing::NormalizedCandidateGenerationPolicy& normalized_policy,
    const CandidateAssociations& route_associations, std::uint64_t route_policy_identity,
    std::uint64_t reported_scalar_cost, std::span<const routing::LayerSegment> segments,
    CandidateProvenance provenance, CandidateProducerAuthority producer_authority);

[[nodiscard]] CandidateDraftBuildResult RejectGeneratedCandidateDraft(
    const board_ir::BoardSnapshot& board, const geometry_compiler::CompiledBoard& compiled_board,
    const routing::PlanarRouteRequest& request,
    const routing::NormalizedCandidateGenerationPolicy& normalized_policy,
    CandidateProvenance provenance, const CandidateAssociations& claimed_associations,
    CandidateLifecycleStage stage, CandidateRejectionCode code, std::string invariant_id,
    std::string detail);

// CandidateStore's shared-request overload normalizes the independently
// supplied request policy once, then reuses that immutable result for every
// candidate. The candidate's own policy, associations, exact geometry,
// resources, metrics, signatures, checksum, and accounting remain independently
// validated per item.
[[nodiscard]] CandidateAdmissionResult AdmitRouteCandidateWithVerifiedRequestPolicy(
    const CandidateAdmissionContext& context,
    const routing::CandidatePolicyResult& verified_request_policy,
    GeneratedRouteCandidate&& generated);

}  // namespace apgar::candidates::internal

#endif  // APGAR_SRC_CANDIDATES_ROUTE_CANDIDATE_INTERNAL_H_
