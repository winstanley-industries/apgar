#include <array>
#include <cstdlib>
#include <memory>
#include <optional>
#include <ranges>
#include <set>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "apgar/allocator/cpu_candidate_pool_preparation.h"
#include "apgar/benchmark/phase4_representative_corpus.h"
#include "apgar/candidates/route_candidate.h"
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

[[nodiscard]] std::unique_ptr<allocator::PersistentCpuCandidatePoolPreparer> Preparer() {
  allocator::PersistentCpuCandidatePoolPreparerResult result =
      allocator::CreatePersistentCpuCandidatePoolPreparer({.worker_count = 4});
  EXPECT_TRUE(
      std::holds_alternative<std::unique_ptr<allocator::PersistentCpuCandidatePoolPreparer>>(
          result));
  if (!std::holds_alternative<std::unique_ptr<allocator::PersistentCpuCandidatePoolPreparer>>(
          result)) {
    std::abort();
  }
  return std::get<std::unique_ptr<allocator::PersistentCpuCandidatePoolPreparer>>(
      std::move(result));
}

[[nodiscard]] allocator::CpuCandidatePoolPreparationConfig PoolConfig(std::size_t net_count,
                                                                      std::uint32_t pool_size) {
  const std::uint64_t columns = net_count * pool_size;
  allocator::CpuCandidatePoolPreparationConfig config;
  config.requested_candidates_per_net = pool_size;
  config.deterministic_seed = 0x2202'2026'0723'0001ULL;
  config.limits.maximum_generated_candidate_bytes = 128ULL * 1024ULL * 1024ULL * 1024ULL;
  config.store_config = candidates::CandidateStoreConfig{
      .maximum_candidates_per_net = pool_size,
      .maximum_candidate_bytes_per_net = 8U * 1024U * 1024U,
      .maximum_rejection_records = columns,
      .maximum_rejection_items_per_transaction = columns,
      .maximum_admission_items_per_transaction = columns,
      .maximum_admission_input_bytes_per_transaction = 64U * 1024U * 1024U,
      .maximum_admission_work_units_per_transaction = 100'000'000,
      .maximum_pin_lease_items_per_transaction = columns,
      .maximum_expected_pools_per_invocation = net_count,
      .maximum_expected_candidates_per_invocation = columns,
  };
  return config;
}

[[nodiscard]] allocator::PreparedCpuCandidatePools Prepared(
    allocator::PreparedCpuCandidatePoolsResult result) {
  EXPECT_TRUE(std::holds_alternative<allocator::PreparedCpuCandidatePools>(result))
      << (std::holds_alternative<allocator::CpuCandidatePoolPreparationError>(result)
              ? std::string(std::get<allocator::CpuCandidatePoolPreparationError>(result).detail)
              : "");
  if (!std::holds_alternative<allocator::PreparedCpuCandidatePools>(result)) {
    std::abort();
  }
  return std::get<allocator::PreparedCpuCandidatePools>(std::move(result));
}

[[nodiscard]] bool CandidateUsesResource(const candidates::RouteCandidate& candidate,
                                         const routing::EdgeResourceKey& expected) {
  for (const candidates::PhysicalEdgeSpan& span : candidate.data().resources) {
    const geometry_compiler::DirectionDelta delta =
        candidates::ResourceSpanStorageDelta(span.direction);
    for (std::uint32_t edge = 0; edge < span.edge_count; ++edge) {
      const routing::EdgeResourceKey actual{
          .layer = span.layer,
          .lattice_x = span.lattice_x + delta.x * static_cast<std::int64_t>(edge),
          .lattice_y = span.lattice_y + delta.y * static_cast<std::int64_t>(edge),
          .direction = span.direction,
      };
      if (actual == expected) {
        return true;
      }
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
  return std::ranges::any_of(
      left, [&](const routing::EdgeResourceKey& resource) { return right.contains(resource); });
}

[[nodiscard]] std::uint32_t LegalDegreeAt(const geometry_compiler::CompiledBoard& board,
                                          board_ir::Point64 point) {
  const std::optional<geometry_compiler::LatticeIndex> index =
      geometry_compiler::ExactPointToLatticeIndex(board.profile(), point);
  EXPECT_TRUE(index.has_value());
  if (!index.has_value()) {
    return 0;
  }
  return static_cast<std::uint32_t>(std::ranges::count_if(
      geometry_compiler::kStableDirectionOrder, [&](geometry_compiler::Direction direction) {
        return board.EdgeIsLegal(0, index->x, index->y, direction);
      }));
}

void ExpectEveryBaseRouteExactAdmits(const Phase4RepresentativeCase& corpus,
                                     bool require_fragmented_bends) {
  std::uint64_t query_identity = 1;
  for (std::size_t net_index = 0; net_index < corpus.workload.nets().size(); ++net_index) {
    const allocator::PreparedNetRoutingContext& context = corpus.workload.nets()[net_index];
    const routing::CpuRouteResult route_result =
        routing::RouteWithCpuAStar(corpus.board, context.compiled_board, context.request);
    ASSERT_TRUE(std::holds_alternative<routing::CpuRoute>(route_result))
        << corpus.descriptor.case_id << ":" << net_index;
    const routing::CpuRoute& route = std::get<routing::CpuRoute>(route_result);
    if (require_fragmented_bends && net_index % 2U == 0U) {
      ASSERT_GE(route.segments.size(), 9U) << net_index;
    }
    const routing::CandidatePolicyResult policy = routing::NormalizeCandidateGenerationPolicy(
        context.compiled_board, context.request.candidate_policy);
    ASSERT_TRUE(std::holds_alternative<routing::NormalizedCandidateGenerationPolicy>(policy));
    candidates::CandidateDraftBuildResult draft = candidates::BuildGeneratedCandidateFromCpuRoute(
        corpus.board, context.compiled_board, context.request,
        std::get<routing::NormalizedCandidateGenerationPolicy>(policy), route,
        candidates::CandidateSchedulingIdentity{
            .batch_identity = corpus.case_checksum,
            .query_identity = query_identity++,
        });
    ASSERT_TRUE(std::holds_alternative<candidates::GeneratedRouteCandidate>(draft))
        << corpus.descriptor.case_id << ":" << net_index << ":"
        << (std::holds_alternative<candidates::CandidateRejection>(draft)
                ? std::string(std::get<candidates::CandidateRejection>(draft).invariant_id)
                : "");
    const candidates::CandidateAdmissionResult admitted = candidates::AdmitRouteCandidate(
        candidates::CandidateAdmissionContext{
            .board = corpus.board,
            .compiled_board = context.compiled_board,
            .request = context.request,
        },
        std::get<candidates::GeneratedRouteCandidate>(std::move(draft)));
    EXPECT_TRUE(std::holds_alternative<candidates::RouteCandidate>(admitted))
        << corpus.descriptor.case_id << ":" << net_index;
  }
}

void ExpectPortalAlternativesExactAdmit(const Phase4RepresentativeCase& corpus) {
  ASSERT_EQ(corpus.declared_contested_resources.size(), corpus.workload.nets().size() / 2U);
  std::uint64_t query_identity = 10'000;
  for (std::size_t net_index = 0; net_index < corpus.workload.nets().size(); net_index += 2U) {
    const allocator::PreparedNetRoutingContext& context = corpus.workload.nets()[net_index];
    const routing::EdgeResourceKey& portal = corpus.declared_contested_resources[net_index / 2U];
    routing::CpuRouteRequest request = context.request;
    request.candidate_policy.banned_resources = {portal};
    const routing::CpuRouteResult route_result =
        routing::RouteWithCpuAStar(corpus.board, context.compiled_board, request);
    ASSERT_TRUE(std::holds_alternative<routing::CpuRoute>(route_result)) << net_index;
    const routing::CandidatePolicyResult policy = routing::NormalizeCandidateGenerationPolicy(
        context.compiled_board, request.candidate_policy);
    ASSERT_TRUE(std::holds_alternative<routing::NormalizedCandidateGenerationPolicy>(policy));
    candidates::CandidateDraftBuildResult draft = candidates::BuildGeneratedCandidateFromCpuRoute(
        corpus.board, context.compiled_board, request,
        std::get<routing::NormalizedCandidateGenerationPolicy>(policy),
        std::get<routing::CpuRoute>(route_result),
        candidates::CandidateSchedulingIdentity{
            .batch_identity = corpus.case_checksum,
            .query_identity = query_identity++,
        });
    ASSERT_TRUE(std::holds_alternative<candidates::GeneratedRouteCandidate>(draft)) << net_index;
    candidates::CandidateAdmissionResult admitted = candidates::AdmitRouteCandidate(
        candidates::CandidateAdmissionContext{
            .board = corpus.board,
            .compiled_board = context.compiled_board,
            .request = request,
        },
        std::get<candidates::GeneratedRouteCandidate>(std::move(draft)));
    ASSERT_TRUE(std::holds_alternative<candidates::RouteCandidate>(admitted)) << net_index;
    EXPECT_FALSE(CandidateUsesResource(std::get<candidates::RouteCandidate>(admitted), portal));
  }
}

TEST(Phase4RepresentativeCorpusV2Test, FreezesFreshDisjointDescriptorRosterAndIdentityDomains) {
  const std::span<const Phase4CaseDescriptor> v1 = Phase4CaseDescriptorsV1();
  const std::span<const Phase4CaseDescriptor> v2 = Phase4CaseDescriptorsV2();
  ASSERT_EQ(v2.size(), 42U);
  EXPECT_EQ(Phase4RepresentativeCorpusChecksumV1(), 7'311'872'938'254'494'931ULL);
  EXPECT_EQ(Phase4RepresentativeCorpusChecksumV2(), 4'182'833'841'936'446'798ULL);

  std::set<std::uint32_t> v1_ids;
  std::set<std::uint64_t> v1_seeds;
  for (const Phase4CaseDescriptor& descriptor : v1) {
    v1_ids.insert(descriptor.case_id);
    if (descriptor.source == Phase4CaseSource::kSynthetic) {
      v1_seeds.insert(descriptor.deterministic_seed);
    }
  }
  std::array<std::uint32_t, 6> roles{};
  std::array<std::uint32_t, 3> held_out_families{};
  std::set<std::uint32_t> v2_ids;
  for (const Phase4CaseDescriptor& descriptor : v2) {
    EXPECT_TRUE(v2_ids.insert(descriptor.case_id).second);
    EXPECT_FALSE(v1_ids.contains(descriptor.case_id));
    if (descriptor.source == Phase4CaseSource::kSynthetic) {
      EXPECT_FALSE(v1_seeds.contains(descriptor.deterministic_seed));
    }
    ++roles[static_cast<std::size_t>(descriptor.role)];
    if (descriptor.role == Phase4CaseRole::kHeldOut) {
      ++held_out_families[static_cast<std::size_t>(descriptor.family)];
    }
  }
  EXPECT_EQ(roles, (std::array<std::uint32_t, 6>{3, 6, 24, 5, 3, 1}));
  EXPECT_EQ(held_out_families, (std::array<std::uint32_t, 3>{8, 8, 8}));
  EXPECT_EQ(FindPhase4CaseDescriptorV1(10'100), nullptr);
  EXPECT_EQ(FindPhase4CaseDescriptorV2(100), nullptr);
  ASSERT_NE(FindPhase4CaseDescriptorV2(10'100), nullptr);
  EXPECT_NE(FingerprintPhase4CaseDescriptorV2(*FindPhase4CaseDescriptorV2(10'100)),
            FingerprintPhase4CaseDescriptorV1(*FindPhase4CaseDescriptorV2(10'100)));
}

TEST(Phase4RepresentativeCorpusV2Test, ExactCasesUseLeafTerminalsAndEveryBaseRouteExactAdmits) {
  const std::array expected_case_checksums = {
      3'217'116'157'854'000'498ULL,
      10'937'201'691'349'586'908ULL,
      13'971'517'485'574'820'650ULL,
  };
  std::size_t case_index = 0;
  for (std::uint32_t case_id : {10'100U, 10'101U, 10'102U}) {
    const Phase4RepresentativeCase corpus = Built(BuildPhase4RepresentativeCaseV2(case_id, {}));
    EXPECT_EQ(corpus.case_checksum, expected_case_checksums[case_index]);
    EXPECT_EQ(corpus.board.data().nets.size(), 6U);
    EXPECT_EQ(corpus.board.data().terminals.size(), 12U);
    EXPECT_TRUE(corpus.board.data().obstacles.empty());
    EXPECT_EQ(corpus.board.data().adapter_version, "2");
    EXPECT_EQ(corpus.workload.nets().front().compiled_board.profile().lattice_step,
              kPhase4RepresentativeLatticeStepV2);

    for (const allocator::PreparedNetRoutingContext& context : corpus.workload.nets()) {
      EXPECT_EQ(LegalDegreeAt(context.compiled_board, context.request.start), 1U)
          << case_id << ":" << context.request.net.id;
      EXPECT_EQ(LegalDegreeAt(context.compiled_board, context.request.goal), 1U)
          << case_id << ":" << context.request.net.id;
    }
    ExpectEveryBaseRouteExactAdmits(
        corpus, corpus.descriptor.family == Phase4SyntheticFamily::kFragmentedMaze);
    if (corpus.descriptor.family == Phase4SyntheticFamily::kPortalChannels) {
      ExpectPortalAlternativesExactAdmit(corpus);
    }
    ++case_index;
  }
}

TEST(Phase4RepresentativeCorpusV2Test, ExactCasesPreserveDeclaredPortalIncidence) {
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
  for (std::uint32_t case_id : {10'100U, 10'101U, 10'102U}) {
    const Phase4RepresentativeCase corpus = Built(BuildPhase4RepresentativeCaseV2(case_id, {}));
    ASSERT_EQ(corpus.declared_contested_resources.size(), 3U);
    std::vector<std::uint32_t> resource_uses(3);
    for (std::size_t net_index = 0; net_index < corpus.workload.nets().size(); ++net_index) {
      const allocator::PreparedNetRoutingContext& context = corpus.workload.nets()[net_index];
      const routing::CpuRouteResult result =
          routing::RouteWithCpuAStar(corpus.board, context.compiled_board, context.request);
      ASSERT_TRUE(std::holds_alternative<routing::CpuRoute>(result));
      const auto resources = CpuPathResources(context, std::get<routing::CpuRoute>(result));
      ASSERT_TRUE(resources.has_value());
      std::uint32_t route_uses = 0;
      for (std::size_t resource_index = 0; resource_index < 3; ++resource_index) {
        if (std::ranges::find(*resources, corpus.declared_contested_resources[resource_index]) !=
            resources->end()) {
          ++resource_uses[resource_index];
          ++route_uses;
        }
      }
      EXPECT_EQ(route_uses, expected_route_uses[case_index][net_index]);
    }
    for (std::size_t resource_index = 0; resource_index < resource_uses.size(); ++resource_index) {
      EXPECT_EQ(resource_uses[resource_index],
                expected_uses_per_resource[case_index][resource_index]);
    }
    ++case_index;
  }
}

TEST(Phase4RepresentativeCorpusV2Test,
     PinFieldConflictGraphIsConnectedAndHasExactAdmissibleDisjointAssignment) {
  const Phase4RepresentativeCase corpus = Built(BuildPhase4RepresentativeCaseV2(10'101, {}));
  ASSERT_TRUE(corpus.descriptor.globally_coupled_conflict_graph);
  ASSERT_EQ(corpus.declared_contested_resources.size(), 3U);

  std::vector<std::set<routing::EdgeResourceKey>> base_footprints;
  std::set<routing::EdgeResourceKey> feasible_occupancy;
  std::uint64_t query_identity = 20'000;
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
    const auto feasible_resources = CpuPathResources(context, feasible_route);
    ASSERT_TRUE(feasible_resources.has_value());
    for (const routing::EdgeResourceKey& resource : *feasible_resources) {
      EXPECT_TRUE(feasible_occupancy.insert(resource).second) << net_index;
    }

    const routing::CandidatePolicyResult policy = routing::NormalizeCandidateGenerationPolicy(
        context.compiled_board, feasible_request.candidate_policy);
    ASSERT_TRUE(std::holds_alternative<routing::NormalizedCandidateGenerationPolicy>(policy));
    candidates::CandidateDraftBuildResult draft = candidates::BuildGeneratedCandidateFromCpuRoute(
        corpus.board, context.compiled_board, feasible_request,
        std::get<routing::NormalizedCandidateGenerationPolicy>(policy), feasible_route,
        candidates::CandidateSchedulingIdentity{
            .batch_identity = corpus.case_checksum,
            .query_identity = query_identity++,
        });
    ASSERT_TRUE(std::holds_alternative<candidates::GeneratedRouteCandidate>(draft));
    EXPECT_TRUE(std::holds_alternative<candidates::RouteCandidate>(candidates::AdmitRouteCandidate(
        candidates::CandidateAdmissionContext{
            .board = corpus.board,
            .compiled_board = context.compiled_board,
            .request = feasible_request,
        },
        std::get<candidates::GeneratedRouteCandidate>(std::move(draft)))));
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

TEST(Phase4RepresentativeCorpusV2Test,
     ExactAndCalibrationPoolPreparationHaveNoExactValidationRejections) {
  std::unique_ptr<allocator::PersistentCpuCandidatePoolPreparer> preparer = Preparer();
  for (const auto [case_id, pool_size] :
       {std::pair{10'100U, 4U}, std::pair{10'101U, 4U}, std::pair{10'102U, 4U},
        std::pair{10'200U, 16U}, std::pair{10'210U, 16U}, std::pair{10'220U, 16U}}) {
    const Phase4RepresentativeCase corpus = Built(BuildPhase4RepresentativeCaseV2(case_id, {}));
    allocator::PreparedCpuCandidatePools prepared =
        Prepared(allocator::PrepareInitialCpuCandidatePools(
            *preparer, corpus.board, corpus.workload,
            PoolConfig(corpus.workload.nets().size(), pool_size)));
    const std::size_t expected_columns = corpus.workload.nets().size() * pool_size;
    ASSERT_EQ(prepared.columns().size(), expected_columns);
    ASSERT_EQ(prepared.pools().size(), corpus.workload.nets().size());
    EXPECT_EQ(prepared.counters().requested_columns, expected_columns);
    std::uint32_t base_reached = 0;
    std::uint32_t base_disconnected = 0;
    std::uint32_t skipped_after_disconnected = 0;
    for (std::size_t column_index = 0; column_index < prepared.columns().size(); ++column_index) {
      const allocator::CpuCandidatePoolColumnRecord& column = prepared.columns()[column_index];
      EXPECT_NE(column.rejection_code, candidates::CandidateRejectionCode::kExactValidation)
          << case_id << ":" << column.net.id << ":" << column.candidate_ordinal;
      if (column.outcome ==
          allocator::CpuCandidatePoolColumnOutcome::kSkippedAfterDisconnectedProof) {
        ++skipped_after_disconnected;
      }
      if (column.candidate_ordinal != 0) {
        continue;
      }
      const std::size_t net_index = column_index / pool_size;
      EXPECT_EQ(column.net, corpus.workload.nets()[net_index].request.net);
      EXPECT_EQ(prepared.pools()[net_index].net, column.net);
      if (column.outcome == allocator::CpuCandidatePoolColumnOutcome::kAdmitted ||
          column.outcome == allocator::CpuCandidatePoolColumnOutcome::kDuplicate) {
        ++base_reached;
        EXPECT_TRUE(column.candidate_id.has_value());
        EXPECT_FALSE(prepared.pools()[net_index].candidates.empty());
      } else {
        EXPECT_EQ(column.outcome, allocator::CpuCandidatePoolColumnOutcome::kRouteDisconnected);
        ++base_disconnected;
        EXPECT_TRUE(prepared.pools()[net_index].candidates.empty());
      }
    }
    const std::uint32_t declared_disconnected =
        corpus.descriptor.requested_net_count - corpus.descriptor.declared_reachable_net_count;
    EXPECT_EQ(base_reached, corpus.descriptor.declared_reachable_net_count);
    EXPECT_EQ(base_disconnected, declared_disconnected);
    EXPECT_EQ(skipped_after_disconnected, declared_disconnected * (pool_size - 1U));
    EXPECT_GE(prepared.counters().successful_routes, base_reached);
  }
}

TEST(Phase4RepresentativeCorpusV2Test,
     ImportedAncestryOperationalProfileAndFailureBoundariesAreVersioned) {
  const std::string fixture =
      tooling::ReadRunfile("tests/fixtures/phase4_supported_multinet_v1.kicad_pcb")
          .value_or(std::string{});
  ASSERT_FALSE(fixture.empty());
  Phase4RepresentativeCaseOperationalProfileV1 profile;
  const Phase4RepresentativeCase corpus =
      Built(BuildPhase4RepresentativeCaseV2WithOperationalProfileV1(14'000, fixture, {}, profile));
  EXPECT_EQ(corpus.board.content_hash(), 229'027'575'659'763'193ULL);
  EXPECT_EQ(corpus.workload.workload_checksum(), 13'031'419'002'947'588'674ULL);
  EXPECT_EQ(corpus.case_checksum, 4'993'717'029'123'835'713ULL);
  EXPECT_EQ(profile.case_source, Phase4CaseSource::kImportedFixture);
  EXPECT_EQ(profile.fixture_import_applicability.status,
            Phase4OperationalMeasurementStatus::kMeasured);
  EXPECT_EQ(profile.synthetic_materialization_applicability.reason,
            Phase4OperationalMeasurementReason::kImportedCaseHasNoSyntheticMaterialization);
  EXPECT_EQ(
      profile.compile_probe_applicability.reason,
      Phase4OperationalMeasurementReason::kImportedWorkloadHasNoSeparateSyntheticCompileProbe);

  const Phase4RepresentativeCaseResult missing = BuildPhase4RepresentativeCaseV2(14'000, {});
  ASSERT_TRUE(std::holds_alternative<Phase4RepresentativeCorpusError>(missing));
  EXPECT_EQ(std::get<Phase4RepresentativeCorpusError>(missing).invariant_id,
            "benchmark.phase4_representative.imported_fixture.v2");

  Phase4RepresentativeCorpusLimits active_region_limits;
  active_region_limits.maximum_active_regions = 1;
  const Phase4RepresentativeCaseResult active_region_rejected =
      BuildPhase4RepresentativeCaseV2(10'100, {}, active_region_limits);
  ASSERT_TRUE(std::holds_alternative<Phase4RepresentativeCorpusError>(active_region_rejected));
  EXPECT_EQ(std::get<Phase4RepresentativeCorpusError>(active_region_rejected).invariant_id,
            "benchmark.phase4_representative.active_region_bound.v2");

  const Phase4RepresentativeCase exact = Built(BuildPhase4RepresentativeCaseV2(10'100, {}));
  const std::uint64_t nodes_per_net =
      exact.workload.compiled_node_count() / exact.workload.nets().size();
  Phase4RepresentativeCorpusLimits work_limits;
  work_limits.maximum_compiled_nodes = nodes_per_net * 2U;
  const Phase4RepresentativeCaseResult work_rejected =
      BuildPhase4RepresentativeCaseV2(10'100, {}, work_limits);
  ASSERT_TRUE(std::holds_alternative<Phase4RepresentativeCorpusError>(work_rejected));
  const Phase4RepresentativeCorpusError& work_error =
      std::get<Phase4RepresentativeCorpusError>(work_rejected);
  EXPECT_EQ(work_error.code, Phase4RepresentativeCorpusErrorCode::kWorkBoundExceeded);
  EXPECT_EQ(work_error.invariant_id, "benchmark.phase4_representative.compiled_work_bound.v2");
  EXPECT_EQ(work_error.maximum_preparable_net_count, 2U);
}

TEST(Phase4RepresentativeCorpusV2Test, BoardEntityPreflightUsesV2Invariants) {
  Phase4RepresentativeCorpusLimits limits;
  limits.maximum_board_entities = 19;
  const Phase4RepresentativeCaseResult rejected =
      BuildPhase4RepresentativeCaseV2(10'100, {}, limits);
  ASSERT_TRUE(std::holds_alternative<Phase4RepresentativeCorpusError>(rejected));
  EXPECT_EQ(std::get<Phase4RepresentativeCorpusError>(rejected).invariant_id,
            "benchmark.phase4_representative.input_bound.v2");

  limits.maximum_board_entities = 20;
  EXPECT_TRUE(std::holds_alternative<Phase4RepresentativeCase>(
      BuildPhase4RepresentativeCaseV2(10'100, {}, limits)));

  const Phase4RepresentativeCaseResult unknown = BuildPhase4RepresentativeCaseV2(100, {});
  ASSERT_TRUE(std::holds_alternative<Phase4RepresentativeCorpusError>(unknown));
  EXPECT_EQ(std::get<Phase4RepresentativeCorpusError>(unknown).invariant_id,
            "benchmark.phase4_representative.case_id.v2");
}

}  // namespace
}  // namespace apgar::benchmark
