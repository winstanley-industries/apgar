#ifndef APGAR_TESTS_SUPPORT_CANDIDATE_BUILDER_H_
#define APGAR_TESTS_SUPPORT_CANDIDATE_BUILDER_H_

#include <cstdlib>
#include <string>
#include <utility>
#include <variant>

#include "apgar/candidates/route_candidate.h"
#include "apgar/routing/candidate_policy.h"
#include "apgar/routing/cpu_astar.h"
#include "tests/support/google_test.h"

namespace apgar::test_support {

[[nodiscard]] inline routing::NormalizedCandidateGenerationPolicy NormalizePolicy(
    const geometry_compiler::CompiledBoard& compiled, const routing::CpuRouteRequest& request) {
  routing::CandidatePolicyResult result =
      routing::NormalizeCandidateGenerationPolicy(compiled, request.candidate_policy);
  EXPECT_TRUE(std::holds_alternative<routing::NormalizedCandidateGenerationPolicy>(result));
  if (!std::holds_alternative<routing::NormalizedCandidateGenerationPolicy>(result)) {
    std::abort();
  }
  return std::get<routing::NormalizedCandidateGenerationPolicy>(std::move(result));
}

[[nodiscard]] inline candidates::CandidateProvenance CpuCandidateProvenance(
    const routing::CandidateGenerationPolicy& policy, std::uint64_t batch_identity = 1,
    std::uint64_t query_identity = 1) {
  return candidates::CandidateProvenance{
      .generator = candidates::CandidateGeneratorKind::kCpuAStar,
      .generator_version = 1,
      .backend = candidates::CandidateBackendKind::kCpu,
      .supported_device_class = "cpu-reference-v1",
      .deterministic_seed = policy.deterministic_seed,
      .batch_identity = batch_identity,
      .query_identity = query_identity,
      .candidate_ordinal = policy.candidate_ordinal,
  };
}

[[nodiscard]] inline routing::CpuRoute CpuRouteForCandidate(
    const board_ir::BoardSnapshot& board, const geometry_compiler::CompiledBoard& compiled,
    const routing::CpuRouteRequest& request) {
  routing::CpuRouteResult result = routing::RouteWithCpuAStar(board, compiled, request);
  EXPECT_TRUE(std::holds_alternative<routing::CpuRoute>(result))
      << (std::holds_alternative<routing::RouteFailure>(result)
              ? std::get<routing::RouteFailure>(result).detail
              : std::string{});
  if (!std::holds_alternative<routing::CpuRoute>(result)) {
    std::abort();
  }
  return std::get<routing::CpuRoute>(std::move(result));
}

[[nodiscard]] inline candidates::GeneratedRouteCandidate CandidateDraft(
    const board_ir::BoardSnapshot& board, const geometry_compiler::CompiledBoard& compiled,
    const routing::CpuRouteRequest& request, std::uint64_t batch_identity = 1,
    std::uint64_t query_identity = 1) {
  const routing::NormalizedCandidateGenerationPolicy policy = NormalizePolicy(compiled, request);
  const routing::CpuRoute route = CpuRouteForCandidate(board, compiled, request);
  candidates::CandidateDraftBuildResult result = candidates::BuildGeneratedCandidateFromCpuRoute(
      board, compiled, request, policy, route,
      CpuCandidateProvenance(policy.policy, batch_identity, query_identity));
  EXPECT_TRUE(std::holds_alternative<candidates::GeneratedRouteCandidate>(result))
      << (std::holds_alternative<candidates::CandidateBuildError>(result)
              ? std::get<candidates::CandidateBuildError>(result).detail
              : std::string{});
  if (!std::holds_alternative<candidates::GeneratedRouteCandidate>(result)) {
    std::abort();
  }
  return std::get<candidates::GeneratedRouteCandidate>(std::move(result));
}

[[nodiscard]] inline candidates::RouteCandidate AcceptedCandidate(
    const candidates::CandidateAdmissionContext& context,
    candidates::GeneratedRouteCandidate generated) {
  candidates::CandidateAdmissionResult result =
      candidates::AdmitRouteCandidate(context, std::move(generated));
  EXPECT_TRUE(std::holds_alternative<candidates::RouteCandidate>(result))
      << (std::holds_alternative<candidates::CandidateRejection>(result)
              ? std::get<candidates::CandidateRejection>(result).detail
              : std::string{});
  if (!std::holds_alternative<candidates::RouteCandidate>(result)) {
    std::abort();
  }
  return std::get<candidates::RouteCandidate>(std::move(result));
}

}  // namespace apgar::test_support

#endif  // APGAR_TESTS_SUPPORT_CANDIDATE_BUILDER_H_
