#include <algorithm>
#include <cstdlib>
#include <optional>
#include <set>
#include <variant>
#include <vector>

#include "apgar/benchmark/phase4_representative_corpus.h"
#include "apgar/routing/cpu_astar.h"
#include "tests/support/google_test.h"

namespace apgar::benchmark {
namespace {

[[nodiscard]] Phase4RepresentativeCase Built(Phase4RepresentativeCaseResult result) {
  EXPECT_TRUE(std::holds_alternative<Phase4RepresentativeCase>(result))
      << (std::holds_alternative<Phase4RepresentativeCorpusError>(result)
              ? std::string(std::get<Phase4RepresentativeCorpusError>(result).invariant_id)
              : "");
  if (!std::holds_alternative<Phase4RepresentativeCase>(result)) {
    std::abort();
  }
  return std::get<Phase4RepresentativeCase>(std::move(result));
}

[[nodiscard]] std::optional<std::vector<routing::EdgeResourceKey>> CpuPathResources(
    const allocator::PreparedNetRoutingContext& context, const routing::CpuRoute& route) {
  std::vector<routing::EdgeResourceKey> resources;
  resources.reserve(route.lattice_path.size());
  for (std::size_t index = 1; index < route.lattice_path.size(); ++index) {
    const auto start = geometry_compiler::ExactPointToLatticeIndex(context.compiled_board.profile(),
                                                                   route.lattice_path[index - 1]);
    const auto end = geometry_compiler::ExactPointToLatticeIndex(context.compiled_board.profile(),
                                                                 route.lattice_path[index]);
    if (!start.has_value() || !end.has_value()) {
      return std::nullopt;
    }
    const auto direction = routing::DirectionBetween(*start, *end);
    if (!direction.has_value()) {
      return std::nullopt;
    }
    const auto resource =
        routing::CanonicalPhysicalEdgeResource(context.request.start_layer, *start, *direction);
    if (!resource.has_value()) {
      return std::nullopt;
    }
    resources.push_back(*resource);
  }
  return resources;
}

TEST(Phase4RepresentativeCorpusScaleTest, MaterializesHundredsOfDistinctAuthenticNetsLazily) {
  const Phase4RepresentativeCase corpus = Built(BuildPhase4RepresentativeCaseV1(1'000, {}));
  ASSERT_EQ(corpus.workload.nets().size(), 256U);
  EXPECT_EQ(corpus.board.data().nets.size(), 256U);
  EXPECT_EQ(corpus.board.data().terminals.size(), 512U);
  EXPECT_EQ(corpus.declared_contested_resources.size(), 128U);
  EXPECT_EQ(corpus.descriptor.requested_pool_sizes, (std::array<std::uint32_t, 3>{4, 8, 16}));
  EXPECT_EQ(corpus.workload.compiled_node_count(), 16'885'760U);
  EXPECT_EQ(corpus.workload.compiled_host_bytes(), 376'889'344U);
  EXPECT_EQ(corpus.case_checksum, 12'455'736'942'815'589'650ULL);

  const allocator::PreparedNetRoutingContext& first = corpus.workload.nets().front();
  const allocator::PreparedNetRoutingContext& last = corpus.workload.nets().back();
  EXPECT_NE(first.request.net, last.request.net);
  EXPECT_NE(first.request.start, last.request.start);
  std::uint32_t reached = 0;
  for (const allocator::PreparedNetRoutingContext& context : corpus.workload.nets()) {
    const routing::CpuRouteResult result =
        routing::RouteWithCpuAStar(corpus.board, context.compiled_board, context.request);
    ASSERT_TRUE(std::holds_alternative<routing::CpuRoute>(result));
    EXPECT_TRUE(
        routing::CpuRouteHasAuthenticatedAStarEvidence(std::get<routing::CpuRoute>(result)));
    EXPECT_FALSE(routing::ValidateReconstructedRoute(corpus.board, context.compiled_board,
                                                     context.request,
                                                     std::get<routing::CpuRoute>(result).segments)
                     .has_value());
    ++reached;
  }
  EXPECT_EQ(reached, corpus.descriptor.declared_reachable_net_count);
}

TEST(Phase4RepresentativeCorpusScaleTest, RetainsDeclaredReachabilityVariationExactly) {
  const Phase4RepresentativeCase corpus = Built(BuildPhase4RepresentativeCaseV1(220, {}));
  ASSERT_EQ(corpus.workload.nets().size(), 64U);
  EXPECT_EQ(corpus.descriptor.declared_reachable_net_count, 62U);
  std::uint32_t reached = 0;
  std::uint32_t unreachable = 0;
  for (const allocator::PreparedNetRoutingContext& context : corpus.workload.nets()) {
    const routing::CpuRouteResult result =
        routing::RouteWithCpuAStar(corpus.board, context.compiled_board, context.request);
    if (std::holds_alternative<routing::CpuRoute>(result)) {
      ++reached;
      EXPECT_TRUE(
          routing::CpuRouteHasAuthenticatedAStarEvidence(std::get<routing::CpuRoute>(result)));
    } else {
      ++unreachable;
      EXPECT_EQ(std::get<routing::RouteFailure>(result).code,
                routing::RouteFailureCode::kDisconnected);
    }
  }
  EXPECT_EQ(reached, corpus.descriptor.declared_reachable_net_count);
  EXPECT_EQ(unreachable,
            corpus.descriptor.requested_net_count - corpus.descriptor.declared_reachable_net_count);
}

TEST(Phase4RepresentativeCorpusScaleTest,
     HeldOutPinFieldRetainsConnectedIncidenceAndKnownFeasibleAssignment) {
  const Phase4RepresentativeCase corpus = Built(BuildPhase4RepresentativeCaseV1(1'100, {}));
  ASSERT_EQ(corpus.workload.nets().size(), 256U);
  ASSERT_EQ(corpus.declared_contested_resources.size(), 128U);
  ASSERT_TRUE(corpus.descriptor.globally_coupled_conflict_graph);

  std::vector<std::uint32_t> base_uses(corpus.declared_contested_resources.size());
  std::set<routing::EdgeResourceKey> feasible_occupancy;
  for (std::size_t net_index = 0; net_index < corpus.workload.nets().size(); ++net_index) {
    const allocator::PreparedNetRoutingContext& context = corpus.workload.nets()[net_index];
    const routing::CpuRouteResult base_result =
        routing::RouteWithCpuAStar(corpus.board, context.compiled_board, context.request);
    ASSERT_TRUE(std::holds_alternative<routing::CpuRoute>(base_result)) << net_index;
    const auto base_resources = CpuPathResources(context, std::get<routing::CpuRoute>(base_result));
    ASSERT_TRUE(base_resources.has_value());
    std::uint32_t declared_uses = 0;
    for (std::size_t resource_index = 0;
         resource_index < corpus.declared_contested_resources.size(); ++resource_index) {
      if (std::ranges::find(*base_resources, corpus.declared_contested_resources[resource_index]) !=
          base_resources->end()) {
        ++base_uses[resource_index];
        ++declared_uses;
      }
    }
    const bool flexible = net_index % 2U == 0U;
    const bool last_flexible = net_index + 2U == corpus.workload.nets().size();
    const std::size_t motif_index = net_index / 2U;
    EXPECT_NE(std::ranges::find(*base_resources, corpus.declared_contested_resources[motif_index]),
              base_resources->end())
        << net_index;
    if (flexible && !last_flexible) {
      EXPECT_NE(
          std::ranges::find(*base_resources, corpus.declared_contested_resources[motif_index + 1U]),
          base_resources->end())
          << net_index;
    }
    EXPECT_EQ(declared_uses, flexible && !last_flexible ? 2U : 1U) << net_index;

    routing::CpuRouteRequest feasible_request = context.request;
    if (flexible) {
      feasible_request.candidate_policy.objective = routing::CandidateObjective::kResourceDiverse;
      feasible_request.candidate_policy.banned_resources = {
          corpus.declared_contested_resources[net_index / 2U]};
    }
    const routing::CpuRouteResult feasible_result =
        routing::RouteWithCpuAStar(corpus.board, context.compiled_board, feasible_request);
    ASSERT_TRUE(std::holds_alternative<routing::CpuRoute>(feasible_result)) << net_index;
    const auto feasible_resources =
        CpuPathResources(context, std::get<routing::CpuRoute>(feasible_result));
    ASSERT_TRUE(feasible_resources.has_value());
    for (const routing::EdgeResourceKey& resource : *feasible_resources) {
      EXPECT_TRUE(feasible_occupancy.insert(resource).second) << net_index;
    }
  }
  for (std::size_t resource_index = 0; resource_index < base_uses.size(); ++resource_index) {
    EXPECT_EQ(base_uses[resource_index], resource_index == 0U ? 2U : 3U) << resource_index;
  }
}

TEST(Phase4RepresentativeCorpusScaleTest, PredeclaresThousandsLadderWithoutEagerBuild) {
  for (std::uint32_t case_id : {3'000U, 3'001U, 3'002U}) {
    const Phase4CaseDescriptor* descriptor = FindPhase4CaseDescriptorV1(case_id);
    ASSERT_NE(descriptor, nullptr);
    EXPECT_EQ(descriptor->role, Phase4CaseRole::kStress);
    EXPECT_EQ(descriptor->declared_stress_target_nets, 4'096U);
    EXPECT_EQ(descriptor->requested_pool_sizes.front(), 4U);
  }
  EXPECT_EQ(FindPhase4CaseDescriptorV1(3'000)->requested_net_count, 1'024U);
  EXPECT_EQ(FindPhase4CaseDescriptorV1(3'001)->requested_net_count, 2'048U);
  EXPECT_EQ(FindPhase4CaseDescriptorV1(3'002)->requested_net_count, 4'096U);

  Phase4RepresentativeCorpusLimits bounded;
  bounded.maximum_nets = 1'023;
  const Phase4RepresentativeCaseResult rejected =
      BuildPhase4RepresentativeCaseV1(3'000, {}, bounded);
  ASSERT_TRUE(std::holds_alternative<Phase4RepresentativeCorpusError>(rejected));
  EXPECT_EQ(std::get<Phase4RepresentativeCorpusError>(rejected).code,
            Phase4RepresentativeCorpusErrorCode::kInputBoundExceeded);
}

}  // namespace
}  // namespace apgar::benchmark
