#ifndef APGAR_TESTS_SUPPORT_CANDIDATE_BUILDER_H_
#define APGAR_TESTS_SUPPORT_CANDIDATE_BUILDER_H_

#include <algorithm>
#include <cstdlib>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <variant>

#include "apgar/candidates/route_candidate.h"
#include "apgar/routing/candidate_policy.h"
#include "apgar/routing/cpu_astar.h"
#include "src/candidates/route_candidate_internal.h"
#include "src/routing/cpu_astar_internal.h"
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

// Restricts the real CPU A* producer to an exact test path by clearing every
// copied compiled edge outside that path. The production entry point still
// performs search and exact validation before it creates producer evidence;
// no caller-authored route is ever resealed.
[[nodiscard]] inline routing::CpuRoute CpuRouteAlongSegments(
    const board_ir::BoardSnapshot& board, const geometry_compiler::CompiledBoard& compiled,
    routing::CpuRouteRequest& request, std::span<const routing::LayerSegment> segments) {
  std::set<routing::EdgeResourceKey> desired;
  for (const routing::LayerSegment& segment : segments) {
    const std::optional<geometry_compiler::LatticeIndex> start =
        geometry_compiler::ExactPointToLatticeIndex(compiled.profile(), segment.centerline.start);
    const std::optional<geometry_compiler::LatticeIndex> goal =
        geometry_compiler::ExactPointToLatticeIndex(compiled.profile(), segment.centerline.end);
    EXPECT_TRUE(start.has_value());
    EXPECT_TRUE(goal.has_value());
    if (!start.has_value() || !goal.has_value()) {
      std::abort();
    }
    geometry_compiler::LatticeIndex cursor = *start;
    std::uint64_t steps = 0;
    while (cursor != *goal) {
      const std::int64_t dx = goal->x - cursor.x;
      const std::int64_t dy = goal->y - cursor.y;
      std::optional<geometry_compiler::Direction> direction;
      if (dy == 0) {
        direction =
            dx > 0 ? geometry_compiler::Direction::kEast : geometry_compiler::Direction::kWest;
      } else if (dx == 0) {
        direction =
            dy > 0 ? geometry_compiler::Direction::kNorth : geometry_compiler::Direction::kSouth;
      } else if (dx == dy) {
        direction = dx > 0 ? geometry_compiler::Direction::kNorthEast
                           : geometry_compiler::Direction::kSouthWest;
      } else if (dx == -dy) {
        direction = dx > 0 ? geometry_compiler::Direction::kSouthEast
                           : geometry_compiler::Direction::kNorthWest;
      }
      EXPECT_TRUE(direction.has_value());
      if (!direction.has_value() || steps++ > compiled.telemetry().represented_nodes) {
        std::abort();
      }
      const std::optional<routing::EdgeResourceKey> resource =
          routing::CanonicalPhysicalEdgeResource(segment.layer, cursor, *direction);
      EXPECT_TRUE(resource.has_value());
      if (!resource.has_value()) {
        std::abort();
      }
      desired.insert(*resource);
      const geometry_compiler::DirectionDelta delta = geometry_compiler::DeltaFor(*direction);
      cursor.x += delta.x;
      cursor.y += delta.y;
    }
  }

  std::vector<geometry_compiler::SparseTile> restricted(compiled.tiles().begin(),
                                                        compiled.tiles().end());
  for (geometry_compiler::SparseTile& tile : restricted) {
    for (geometry_compiler::CompiledNode& node : tile.nodes) {
      const geometry_compiler::LatticeIndex source =
          geometry_compiler::GlobalLatticeIndex(compiled.profile(), tile.key, node.local_index);
      for (geometry_compiler::Direction direction : geometry_compiler::kStableDirectionOrder) {
        if ((node.legal_edges & geometry_compiler::MaskFor(direction)) == 0) {
          continue;
        }
        const std::optional<routing::EdgeResourceKey> resource =
            routing::CanonicalPhysicalEdgeResource(tile.key.layer, source, direction);
        if (!resource.has_value() || !desired.contains(*resource)) {
          node.legal_edges &=
              static_cast<geometry_compiler::DirectionMask>(~geometry_compiler::MaskFor(direction));
        }
      }
    }
  }
  routing::CpuRouteResult result =
      routing::internal::RouteWithCpuAStarUsingRestrictedTileViewForTesting(board, compiled,
                                                                            request, restricted);
  EXPECT_TRUE(std::holds_alternative<routing::CpuRoute>(result));
  if (!std::holds_alternative<routing::CpuRoute>(result)) {
    std::abort();
  }
  return std::get<routing::CpuRoute>(std::move(result));
}

[[nodiscard]] inline candidates::GeneratedRouteCandidate CandidateDraftAlongSegments(
    const board_ir::BoardSnapshot& board, const geometry_compiler::CompiledBoard& compiled,
    routing::CpuRouteRequest& request, std::span<const routing::LayerSegment> segments,
    candidates::CandidateSchedulingIdentity scheduling) {
  const routing::CpuRoute route = CpuRouteAlongSegments(board, compiled, request, segments);
  const routing::NormalizedCandidateGenerationPolicy policy = NormalizePolicy(compiled, request);
  candidates::CandidateDraftBuildResult result = candidates::BuildGeneratedCandidateFromCpuRoute(
      board, compiled, request, policy, route, scheduling);
  EXPECT_TRUE(std::holds_alternative<candidates::GeneratedRouteCandidate>(result));
  if (!std::holds_alternative<candidates::GeneratedRouteCandidate>(result)) {
    std::abort();
  }
  return std::get<candidates::GeneratedRouteCandidate>(std::move(result));
}

// Exact validation probes use the production source-private planar candidate
// builder directly. Successful drafts remain deliberately unsealed and cannot
// cross public admission; rejection diagnostics exercise the same validation
// implementation without a test-only producer-evidence mint.
[[nodiscard]] inline candidates::CandidateDraftBuildResult BuildUnsealedCandidateFromSegments(
    const board_ir::BoardSnapshot& board, const geometry_compiler::CompiledBoard& compiled,
    const routing::CpuRouteRequest& request,
    const routing::NormalizedCandidateGenerationPolicy& policy,
    std::span<const routing::LayerSegment> segments, std::uint64_t reported_cost,
    candidates::CandidateSchedulingIdentity scheduling,
    candidates::CandidateAssociations associations) {
  const candidates::CandidateProvenance provenance{
      .generator = candidates::CandidateGeneratorKind::kCpuAStar,
      .generator_version = 1,
      .backend = candidates::CandidateBackendKind::kCpu,
      .supported_device_class = std::string(candidates::kCpuReferenceDeviceClassV1),
      .deterministic_seed = policy.policy.deterministic_seed,
      .batch_identity = scheduling.batch_identity,
      .query_identity = scheduling.query_identity,
      .candidate_ordinal = policy.policy.candidate_ordinal,
  };
  return candidates::internal::BuildGeneratedCandidateFromValidatedPlanarRoute(
      board, compiled, request, policy, associations, policy.identity, reported_cost, segments,
      provenance);
}

[[nodiscard]] inline candidates::GeneratedRouteCandidate CandidateDraft(
    const board_ir::BoardSnapshot& board, const geometry_compiler::CompiledBoard& compiled,
    const routing::CpuRouteRequest& request, std::uint64_t batch_identity = 1,
    std::uint64_t query_identity = 1) {
  const routing::NormalizedCandidateGenerationPolicy policy = NormalizePolicy(compiled, request);
  const routing::CpuRoute route = CpuRouteForCandidate(board, compiled, request);
  candidates::CandidateDraftBuildResult result = candidates::BuildGeneratedCandidateFromCpuRoute(
      board, compiled, request, policy, route,
      candidates::CandidateSchedulingIdentity{.batch_identity = batch_identity,
                                              .query_identity = query_identity});
  EXPECT_TRUE(std::holds_alternative<candidates::GeneratedRouteCandidate>(result))
      << (std::holds_alternative<candidates::CandidateRejection>(result)
              ? std::get<candidates::CandidateRejection>(result).detail
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
