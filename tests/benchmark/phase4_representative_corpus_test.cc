#include "apgar/benchmark/phase4_representative_corpus.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <optional>
#include <ranges>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <variant>

#include "apgar/routing/cpu_astar.h"
#include "apgar/tooling/runfiles.h"
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

[[nodiscard]] std::string ReadImportedFixture() {
  return tooling::ReadRunfile("tests/fixtures/phase4_supported_multinet_v1.kicad_pcb")
      .value_or(std::string{});
}

[[nodiscard]] const Phase4RepresentativeCorpusError& Rejected(
    const Phase4RepresentativeCaseResult& result) {
  EXPECT_TRUE(std::holds_alternative<Phase4RepresentativeCorpusError>(result));
  if (!std::holds_alternative<Phase4RepresentativeCorpusError>(result)) {
    std::abort();
  }
  return std::get<Phase4RepresentativeCorpusError>(result);
}

[[nodiscard]] bool CpuPathUsesResource(const allocator::PreparedNetRoutingContext& context,
                                       const routing::CpuRoute& route,
                                       const routing::EdgeResourceKey& expected) {
  for (std::size_t index = 1; index < route.lattice_path.size(); ++index) {
    const auto start = geometry_compiler::ExactPointToLatticeIndex(context.compiled_board.profile(),
                                                                   route.lattice_path[index - 1]);
    const auto end = geometry_compiler::ExactPointToLatticeIndex(context.compiled_board.profile(),
                                                                 route.lattice_path[index]);
    if (!start.has_value() || !end.has_value()) {
      return false;
    }
    const std::int64_t dx = end->x - start->x;
    const std::int64_t dy = end->y - start->y;
    geometry_compiler::Direction direction;
    if (dx == 1 && dy == 0) {
      direction = geometry_compiler::Direction::kEast;
    } else if (dx == -1 && dy == 0) {
      direction = geometry_compiler::Direction::kWest;
    } else if (dx == 0 && dy == 1) {
      direction = geometry_compiler::Direction::kNorth;
    } else if (dx == 0 && dy == -1) {
      direction = geometry_compiler::Direction::kSouth;
    } else {
      return false;
    }
    const auto actual = routing::CanonicalPhysicalEdgeResource(0, *start, direction);
    if (actual == expected) {
      return true;
    }
  }
  return false;
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

[[nodiscard]] bool ResourcesOverlap(const std::set<routing::EdgeResourceKey>& left,
                                    const std::set<routing::EdgeResourceKey>& right) {
  for (const routing::EdgeResourceKey& resource : left) {
    if (right.contains(resource)) {
      return true;
    }
  }
  return false;
}

TEST(Phase4RepresentativeCorpusTest, FreezesCompleteLazyDescriptorRoster) {
  const std::span<const Phase4CaseDescriptor> descriptors = Phase4CaseDescriptorsV1();
  ASSERT_EQ(descriptors.size(), 42U);
  EXPECT_EQ(Phase4RepresentativeCorpusChecksumV1(), 7'311'872'938'254'494'931ULL);

  std::array<std::uint32_t, 6> roles{};
  std::array<std::uint32_t, 3> held_out_families{};
  std::set<std::uint32_t> case_ids;
  std::set<std::uint64_t> descriptor_checksums;
  Phase4CorpusFeatureMask features = 0;
  std::vector<std::pair<std::uint32_t, std::uint32_t>> query_shapes;
  std::vector<std::uint32_t> stress_tiers;
  std::uint32_t globally_coupled_cases = 0;
  for (const Phase4CaseDescriptor& descriptor : descriptors) {
    ASSERT_TRUE(case_ids.insert(descriptor.case_id).second);
    ASSERT_TRUE(descriptor_checksums.insert(FingerprintPhase4CaseDescriptorV1(descriptor)).second);
    ASSERT_LT(static_cast<std::size_t>(descriptor.role), roles.size());
    ++roles[static_cast<std::size_t>(descriptor.role)];
    features |= descriptor.features;
    ASSERT_GE(descriptor.requested_pool_size_count, 1U);
    ASSERT_LE(descriptor.requested_pool_size_count, descriptor.requested_pool_sizes.size());
    for (std::size_t index = 0; index < descriptor.requested_pool_size_count; ++index) {
      EXPECT_GT(descriptor.requested_pool_sizes[index], 0U);
    }
    EXPECT_FALSE(descriptor.known_unmapped_exact_conflicts);
    if (descriptor.globally_coupled_conflict_graph) {
      ++globally_coupled_cases;
      EXPECT_EQ(descriptor.family, Phase4SyntheticFamily::kPinFieldCrossbar);
    }
    if (descriptor.role == Phase4CaseRole::kHeldOut) {
      ASSERT_LT(static_cast<std::size_t>(descriptor.family), held_out_families.size());
      ++held_out_families[static_cast<std::size_t>(descriptor.family)];
      EXPECT_EQ(descriptor.requested_pool_sizes, (std::array<std::uint32_t, 3>{4, 8, 16}));
      EXPECT_EQ(descriptor.requested_pool_size_count, 3U);
      EXPECT_GE(descriptor.requested_net_count, 256U);
    }
    if (descriptor.role == Phase4CaseRole::kExactOracle) {
      EXPECT_EQ(descriptor.requested_net_count, 6U);
      EXPECT_EQ(descriptor.maximum_exact_candidate_products, 4'096U);
    }
    if (descriptor.role == Phase4CaseRole::kQueryShape) {
      query_shapes.emplace_back(descriptor.requested_net_count,
                                descriptor.requested_pool_sizes.front());
    }
    if (descriptor.role == Phase4CaseRole::kStress) {
      stress_tiers.push_back(descriptor.requested_net_count);
      EXPECT_EQ(descriptor.requested_pool_sizes.front(), 4U);
      EXPECT_EQ(descriptor.declared_stress_target_nets, 4'096U);
    }
  }
  EXPECT_EQ(roles, (std::array<std::uint32_t, 6>{3, 6, 24, 5, 3, 1}));
  EXPECT_EQ(held_out_families, (std::array<std::uint32_t, 3>{8, 8, 8}));
  EXPECT_EQ(features, 127U);
  EXPECT_EQ(query_shapes, (std::vector<std::pair<std::uint32_t, std::uint32_t>>{
                              {1, 1'024}, {1'024, 1}, {256, 4}, {128, 8}, {64, 16}}));
  EXPECT_EQ(stress_tiers, (std::vector<std::uint32_t>{1'024, 2'048, 4'096}));
  EXPECT_EQ(globally_coupled_cases, 11U);
}

TEST(Phase4RepresentativeCorpusTest, DescriptorFingerprintBindsEveryField) {
  const Phase4CaseDescriptor original = *FindPhase4CaseDescriptorV1(100);
  const std::uint64_t fingerprint = FingerprintPhase4CaseDescriptorV1(original);
  for (std::size_t field = 0; field < 15; ++field) {
    Phase4CaseDescriptor changed = original;
    switch (field) {
      case 0:
        ++changed.case_id;
        break;
      case 1:
        changed.source = Phase4CaseSource::kImportedFixture;
        break;
      case 2:
        changed.family = Phase4SyntheticFamily::kPinFieldCrossbar;
        break;
      case 3:
        changed.role = Phase4CaseRole::kCalibration;
        break;
      case 4:
        ++changed.deterministic_seed;
        break;
      case 5:
        ++changed.requested_net_count;
        break;
      case 6:
        --changed.declared_reachable_net_count;
        break;
      case 7:
        ++changed.requested_pool_sizes[0];
        break;
      case 8:
        changed.requested_pool_sizes[1] = 7;
        break;
      case 9:
        ++changed.requested_pool_size_count;
        break;
      case 10:
        changed.features ^= Feature(Phase4CorpusFeature::kReachability);
        break;
      case 11:
        ++changed.maximum_exact_candidate_products;
        break;
      case 12:
        ++changed.declared_stress_target_nets;
        break;
      case 13:
        changed.known_unmapped_exact_conflicts = true;
        break;
      case 14:
        changed.globally_coupled_conflict_graph = true;
        break;
    }
    EXPECT_NE(FingerprintPhase4CaseDescriptorV1(changed), fingerprint) << field;
  }
}

TEST(Phase4RepresentativeCorpusTest, BuildsEveryExactFamilyWithAuthenticCpuOracleRoutes) {
  const std::array expected_case_checksums = {
      17'177'310'953'492'740'304ULL,
      8'332'006'487'454'784'915ULL,
      3'673'964'883'945'221'241ULL,
  };
  const std::array<std::size_t, 3> expected_contested_resources = {3, 3, 3};
  const std::array expected_uses_per_resource = {
      std::array<std::uint32_t, 3>{2, 2, 2},
      std::array<std::uint32_t, 3>{2, 3, 3},
      std::array<std::uint32_t, 3>{2, 2, 2},
  };
  const std::array expected_route_uses = {
      std::array<std::uint32_t, 6>{1, 1, 1, 1, 1, 1},
      std::array<std::uint32_t, 6>{2, 1, 2, 1, 1, 1},
      std::array<std::uint32_t, 6>{1, 1, 1, 1, 1, 1},
  };
  std::size_t case_index = 0;
  for (std::uint32_t case_id : {100U, 101U, 102U}) {
    const Phase4RepresentativeCase corpus = Built(BuildPhase4RepresentativeCaseV1(case_id, {}));
    EXPECT_EQ(corpus.descriptor.requested_net_count, 6U);
    EXPECT_EQ(corpus.descriptor.declared_reachable_net_count, 6U);
    EXPECT_EQ(corpus.board.data().nets.size(), 6U);
    EXPECT_EQ(corpus.board.data().terminals.size(), 12U);
    EXPECT_EQ(corpus.workload.nets().size(), 6U);
    EXPECT_EQ(corpus.declared_contested_resources.size(), expected_contested_resources[case_index]);
    EXPECT_EQ(corpus.capacities.default_capacity_units(), 1U);
    EXPECT_EQ(corpus.capacities.associations().board_content_hash, corpus.board.content_hash());
    EXPECT_EQ(corpus.capacities.associations().compiler_profile_fingerprint,
              corpus.workload.compiler_profile_fingerprint());
    EXPECT_EQ(corpus.case_checksum, expected_case_checksums[case_index]);

    std::set<std::pair<std::uint64_t, std::uint32_t>> nets;
    std::set<std::tuple<std::int64_t, std::int64_t, std::int64_t, std::int64_t>> requests;
    std::vector<std::uint32_t> contested_resource_uses(corpus.declared_contested_resources.size());
    std::size_t net_index = 0;
    for (const allocator::PreparedNetRoutingContext& context : corpus.workload.nets()) {
      EXPECT_TRUE(nets.emplace(context.request.net.id, context.request.net.generation).second);
      EXPECT_TRUE(requests
                      .emplace(context.request.start.x, context.request.start.y,
                               context.request.goal.x, context.request.goal.y)
                      .second);
      const routing::CpuRouteResult result =
          routing::RouteWithCpuAStar(corpus.board, context.compiled_board, context.request);
      ASSERT_TRUE(std::holds_alternative<routing::CpuRoute>(result)) << case_id;
      const routing::CpuRoute& route = std::get<routing::CpuRoute>(result);
      EXPECT_TRUE(routing::CpuRouteHasAuthenticatedAStarEvidence(route));
      EXPECT_FALSE(routing::ValidateReconstructedRoute(corpus.board, context.compiled_board,
                                                       context.request, route.segments)
                       .has_value());
      std::uint32_t route_contested_resources = 0;
      for (std::size_t resource_index = 0;
           resource_index < corpus.declared_contested_resources.size(); ++resource_index) {
        if (CpuPathUsesResource(context, route,
                                corpus.declared_contested_resources[resource_index])) {
          ++contested_resource_uses[resource_index];
          ++route_contested_resources;
        }
      }
      EXPECT_EQ(route_contested_resources, expected_route_uses[case_index][net_index]);
      ++net_index;
    }
    for (std::size_t resource_index = 0;
         resource_index < corpus.declared_contested_resources.size(); ++resource_index) {
      const routing::EdgeResourceKey& resource =
          corpus.declared_contested_resources[resource_index];
      EXPECT_TRUE(routing::ResourceExists(corpus.workload.nets().front().compiled_board, resource));
      EXPECT_EQ(contested_resource_uses[resource_index],
                expected_uses_per_resource[case_index][resource_index]);
    }
    ++case_index;
  }
}

TEST(Phase4RepresentativeCorpusTest,
     PinFieldBaseConflictGraphIsConnectedAndHasKnownEdgeDisjointAssignment) {
  const Phase4RepresentativeCase corpus = Built(BuildPhase4RepresentativeCaseV1(101, {}));
  ASSERT_TRUE(corpus.descriptor.globally_coupled_conflict_graph);
  ASSERT_EQ(corpus.workload.nets().size(), 6U);
  ASSERT_EQ(corpus.declared_contested_resources.size(), 3U);

  std::vector<std::set<routing::EdgeResourceKey>> base_footprints;
  base_footprints.reserve(corpus.workload.nets().size());
  std::set<routing::EdgeResourceKey> feasible_occupancy;
  for (std::size_t net_index = 0; net_index < corpus.workload.nets().size(); ++net_index) {
    const allocator::PreparedNetRoutingContext& context = corpus.workload.nets()[net_index];
    const routing::CpuRouteResult base_result =
        routing::RouteWithCpuAStar(corpus.board, context.compiled_board, context.request);
    ASSERT_TRUE(std::holds_alternative<routing::CpuRoute>(base_result));
    const auto base_resources = CpuPathResources(context, std::get<routing::CpuRoute>(base_result));
    ASSERT_TRUE(base_resources.has_value());
    base_footprints.emplace_back(base_resources->begin(), base_resources->end());

    routing::CpuRouteRequest feasible_request = context.request;
    if (net_index % 2U == 0U) {
      feasible_request.candidate_policy.objective = routing::CandidateObjective::kResourceDiverse;
      feasible_request.candidate_policy.banned_resources = {
          corpus.declared_contested_resources[net_index / 2U]};
    }
    const routing::CpuRouteResult feasible_result =
        routing::RouteWithCpuAStar(corpus.board, context.compiled_board, feasible_request);
    ASSERT_TRUE(std::holds_alternative<routing::CpuRoute>(feasible_result)) << net_index;
    const routing::CpuRoute& feasible_route = std::get<routing::CpuRoute>(feasible_result);
    EXPECT_FALSE(routing::ValidateReconstructedRoute(corpus.board, context.compiled_board,
                                                     feasible_request, feasible_route.segments)
                     .has_value());
    const auto feasible_resources = CpuPathResources(context, feasible_route);
    ASSERT_TRUE(feasible_resources.has_value());
    for (const routing::EdgeResourceKey& resource : *feasible_resources) {
      EXPECT_TRUE(feasible_occupancy.insert(resource).second) << net_index;
    }
  }

  std::vector<bool> visited(base_footprints.size());
  std::vector<std::size_t> pending = {0};
  visited[0] = true;
  while (!pending.empty()) {
    const std::size_t current = pending.back();
    pending.pop_back();
    for (std::size_t neighbor = 0; neighbor < base_footprints.size(); ++neighbor) {
      if (!visited[neighbor] &&
          ResourcesOverlap(base_footprints[current], base_footprints[neighbor])) {
        visited[neighbor] = true;
        pending.push_back(neighbor);
      }
    }
  }
  EXPECT_TRUE(std::ranges::all_of(visited, [](bool value) { return value; }));
}

TEST(Phase4RepresentativeCorpusTest, BuildsAuthenticatedImportedGuardrailThroughSameRoster) {
  const std::string fixture = ReadImportedFixture();
  ASSERT_FALSE(fixture.empty());
  const Phase4RepresentativeCase corpus = Built(BuildPhase4RepresentativeCaseV1(4'000, fixture));
  EXPECT_EQ(corpus.descriptor.source, Phase4CaseSource::kImportedFixture);
  EXPECT_EQ(corpus.board.content_hash(), 229'027'575'659'763'193ULL);
  EXPECT_EQ(corpus.workload.workload_checksum(), 13'031'419'002'947'588'674ULL);
  EXPECT_TRUE(corpus.declared_contested_resources.empty());
  EXPECT_EQ(corpus.case_checksum, 7'603'876'739'078'231'632ULL);
}

TEST(Phase4RepresentativeCorpusTest, RejectsUnknownMissingAndBoundedInputsBeforeMaterialization) {
  {
    const Phase4RepresentativeCaseResult result = BuildPhase4RepresentativeCaseV1(99'999, {});
    EXPECT_EQ(Rejected(result).code, Phase4RepresentativeCorpusErrorCode::kUnknownCase);
    EXPECT_EQ(Rejected(result).invariant_id, "benchmark.phase4_representative.case_id.v1");
  }
  {
    Phase4RepresentativeCorpusLimits limits;
    limits.maximum_nets = 0;
    const Phase4RepresentativeCaseResult result = BuildPhase4RepresentativeCaseV1(100, {}, limits);
    EXPECT_EQ(Rejected(result).code, Phase4RepresentativeCorpusErrorCode::kInvalidLimits);
  }
  {
    Phase4RepresentativeCorpusLimits limits;
    limits.maximum_nets = 5;
    const Phase4RepresentativeCaseResult result = BuildPhase4RepresentativeCaseV1(100, {}, limits);
    EXPECT_EQ(Rejected(result).code, Phase4RepresentativeCorpusErrorCode::kInputBoundExceeded);
    EXPECT_EQ(Rejected(result).invariant_id, "benchmark.phase4_representative.input_bound.v1");
  }
  {
    Phase4RepresentativeCorpusLimits limits;
    limits.maximum_active_regions = 1;
    const Phase4RepresentativeCaseResult result = BuildPhase4RepresentativeCaseV1(100, {}, limits);
    EXPECT_EQ(Rejected(result).code, Phase4RepresentativeCorpusErrorCode::kInputBoundExceeded);
    EXPECT_EQ(Rejected(result).invariant_id,
              "benchmark.phase4_representative.active_region_bound.v1");
  }
  {
    const Phase4RepresentativeCaseResult result = BuildPhase4RepresentativeCaseV1(4'000, {});
    EXPECT_EQ(Rejected(result).code, Phase4RepresentativeCorpusErrorCode::kImportedFixtureRequired);
  }
}

TEST(Phase4RepresentativeCorpusTest, PreservesExactCompiledWorkBoundWitnesses) {
  const Phase4RepresentativeCase corpus = Built(BuildPhase4RepresentativeCaseV1(100, {}));
  ASSERT_EQ(corpus.workload.nets().size(), 6U);
  ASSERT_EQ(corpus.workload.compiled_node_count() % 6U, 0U);
  ASSERT_EQ(corpus.workload.compiled_host_bytes() % 6U, 0U);
  const std::uint64_t nodes_per_net = corpus.workload.compiled_node_count() / 6U;
  const std::uint64_t host_bytes_per_net = corpus.workload.compiled_host_bytes() / 6U;

  Phase4RepresentativeCorpusLimits node_limits;
  node_limits.maximum_compiled_nodes = nodes_per_net * 2U;
  const Phase4RepresentativeCaseResult node_result =
      BuildPhase4RepresentativeCaseV1(100, {}, node_limits);
  const Phase4RepresentativeCorpusError& node_error = Rejected(node_result);
  EXPECT_EQ(node_error.code, Phase4RepresentativeCorpusErrorCode::kWorkBoundExceeded);
  EXPECT_EQ(node_error.invariant_id, "benchmark.phase4_representative.compiled_work_bound.v1");
  EXPECT_EQ(node_error.limiting_work_bound, Phase4RepresentativeWorkBound::kCompiledNodes);
  EXPECT_EQ(node_error.maximum_preparable_net_count, 2U);
  ASSERT_TRUE(node_error.first_unpreparable_net.has_value());
  EXPECT_EQ(node_error.first_unpreparable_net, corpus.workload.nets()[2].request.net);
  EXPECT_EQ(node_error.required_compiled_nodes, corpus.workload.compiled_node_count());
  EXPECT_EQ(node_error.configured_compiled_node_limit, nodes_per_net * 2U);
  EXPECT_EQ(node_error.required_compiled_host_bytes, corpus.workload.compiled_host_bytes());
  EXPECT_EQ(node_error.configured_compiled_host_byte_limit,
            allocator::kMaximumMultiNetWorkloadCompiledHostBytesV1);

  Phase4RepresentativeCorpusLimits host_limits;
  host_limits.maximum_compiled_host_bytes = host_bytes_per_net * 3U;
  const Phase4RepresentativeCaseResult host_result =
      BuildPhase4RepresentativeCaseV1(100, {}, host_limits);
  const Phase4RepresentativeCorpusError& host_error = Rejected(host_result);
  EXPECT_EQ(host_error.code, Phase4RepresentativeCorpusErrorCode::kWorkBoundExceeded);
  EXPECT_EQ(host_error.limiting_work_bound, Phase4RepresentativeWorkBound::kCompiledHostBytes);
  EXPECT_EQ(host_error.maximum_preparable_net_count, 3U);
  ASSERT_TRUE(host_error.first_unpreparable_net.has_value());
  EXPECT_EQ(host_error.first_unpreparable_net, corpus.workload.nets()[3].request.net);
  EXPECT_EQ(host_error.required_compiled_nodes, corpus.workload.compiled_node_count());
  EXPECT_EQ(host_error.configured_compiled_node_limit,
            allocator::kMaximumMultiNetWorkloadCompiledNodesV1);
  EXPECT_EQ(host_error.required_compiled_host_bytes, corpus.workload.compiled_host_bytes());
  EXPECT_EQ(host_error.configured_compiled_host_byte_limit, host_bytes_per_net * 3U);

  Phase4RepresentativeCorpusLimits asymmetric_limits;
  asymmetric_limits.maximum_compiled_nodes = nodes_per_net * 2U;
  asymmetric_limits.maximum_compiled_host_bytes = host_bytes_per_net * 5U;
  const Phase4RepresentativeCaseResult asymmetric_result =
      BuildPhase4RepresentativeCaseV1(100, {}, asymmetric_limits);
  const Phase4RepresentativeCorpusError& asymmetric_error = Rejected(asymmetric_result);
  EXPECT_EQ(asymmetric_error.maximum_preparable_net_count, 2U);
  EXPECT_EQ(asymmetric_error.limiting_work_bound, Phase4RepresentativeWorkBound::kCompiledNodes);
  EXPECT_LT(asymmetric_error.configured_compiled_host_byte_limit,
            asymmetric_error.required_compiled_host_bytes);

  Phase4RepresentativeCorpusLimits simultaneous_limits;
  simultaneous_limits.maximum_compiled_nodes = nodes_per_net * 2U;
  simultaneous_limits.maximum_compiled_host_bytes = host_bytes_per_net * 2U;
  const Phase4RepresentativeCaseResult simultaneous_result =
      BuildPhase4RepresentativeCaseV1(100, {}, simultaneous_limits);
  const Phase4RepresentativeCorpusError& simultaneous_error = Rejected(simultaneous_result);
  EXPECT_EQ(simultaneous_error.maximum_preparable_net_count, 2U);
  EXPECT_EQ(simultaneous_error.limiting_work_bound,
            Phase4RepresentativeWorkBound::kCompiledNodesAndHostBytes);
}

}  // namespace
}  // namespace apgar::benchmark
