#include "apgar/allocator/multi_net_workload.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <memory>
#include <ranges>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "apgar/allocator/one_world.h"
#include "apgar/candidates/candidate_store.h"
#include "apgar/candidates/route_candidate.h"
#include "apgar/routing/candidate_policy.h"
#include "apgar/routing/cpu_astar.h"
#include "src/allocator/multi_net_workload_internal.h"
#include "tests/support/board_builder.h"
#include "tests/support/candidate_builder.h"
#include "tests/support/compiler_builder.h"
#include "tests/support/google_test.h"
#include "tests/support/routing_builder.h"

namespace apgar::allocator {
namespace {

static_assert(std::is_nothrow_copy_constructible_v<MultiNetWorkloadError>);

[[nodiscard]] MultiNetWorkload Built(MultiNetWorkloadResult result) {
  EXPECT_TRUE(std::holds_alternative<MultiNetWorkload>(result))
      << (std::holds_alternative<MultiNetWorkloadError>(result)
              ? std::get<MultiNetWorkloadError>(result).detail
              : "");
  if (!std::holds_alternative<MultiNetWorkload>(result)) {
    std::abort();
  }
  return std::get<MultiNetWorkload>(std::move(result));
}

[[nodiscard]] PreparedNetRoutingContext PreparedContext(
    const board_ir::BoardSnapshot& board,
    const geometry_compiler::CompilerProfile& compiler_profile,
    board_ir::RoutingProfile routing_profile) {
  geometry_compiler::CompileResult compiled_result =
      geometry_compiler::CompileBoard(board, compiler_profile, routing_profile);
  EXPECT_TRUE(std::holds_alternative<geometry_compiler::CompiledBoard>(compiled_result));
  if (!std::holds_alternative<geometry_compiler::CompiledBoard>(compiled_result)) {
    std::abort();
  }
  geometry_compiler::CompiledBoard compiled =
      std::get<geometry_compiler::CompiledBoard>(std::move(compiled_result));
  routing::TwoTerminalRequestResult request_result =
      routing::BuildTwoTerminalRouteRequest(board, routing_profile, 0, 0);
  EXPECT_TRUE(std::holds_alternative<routing::PlanarRouteRequest>(request_result));
  if (!std::holds_alternative<routing::PlanarRouteRequest>(request_result)) {
    std::abort();
  }
  return PreparedNetRoutingContext{
      .routing_profile = compiled.routing_profile(),
      .compiled_board = std::move(compiled),
      .request = std::get<routing::PlanarRouteRequest>(std::move(request_result)),
      .routing_profile_fingerprint = routing::FingerprintRoutingProfile(routing_profile),
  };
}

[[nodiscard]] candidates::GeneratedRouteCandidate DraftFor(const board_ir::BoardSnapshot& board,
                                                           const PreparedNetRoutingContext& context,
                                                           std::uint64_t query_identity) {
  const routing::NormalizedCandidateGenerationPolicy policy =
      test_support::NormalizePolicy(context.compiled_board, context.request);
  const routing::CpuRoute route =
      test_support::CpuRouteForCandidate(board, context.compiled_board, context.request);
  candidates::CandidateDraftBuildResult draft = candidates::BuildGeneratedCandidateFromCpuRoute(
      board, context.compiled_board, context.request, policy, route,
      candidates::CandidateSchedulingIdentity{.batch_identity = 1,
                                              .query_identity = query_identity});
  EXPECT_TRUE(std::holds_alternative<candidates::GeneratedRouteCandidate>(draft));
  if (!std::holds_alternative<candidates::GeneratedRouteCandidate>(draft)) {
    std::abort();
  }
  return std::get<candidates::GeneratedRouteCandidate>(std::move(draft));
}

[[nodiscard]] candidates::StoredCandidate CandidateFor(const board_ir::BoardSnapshot& board,
                                                       const PreparedNetRoutingContext& context,
                                                       std::uint64_t query_identity) {
  candidates::RouteCandidate accepted = test_support::AcceptedCandidate(
      candidates::CandidateAdmissionContext{
          .board = board, .compiled_board = context.compiled_board, .request = context.request},
      DraftFor(board, context, query_identity));
  return std::make_shared<const candidates::RouteCandidate>(std::move(accepted));
}

TEST(MultiNetWorkloadTest, CanonicalizesDistinctAuthenticNetContextsAndChecksum) {
  board_ir::BoardData board_data = test_support::ValidM1TwoNetBoardData();
  board_data.obstacles.clear();
  const board_ir::BoardSnapshot board = test_support::Snapshot(std::move(board_data));
  const geometry_compiler::CompilerProfile compiler_profile =
      test_support::DefaultCompilerProfile({0});
  board_ir::RoutingProfile second_profile = board.data().routing_profile;
  second_profile.net = board.data().nets[1].ref;
  std::array specs = {
      MultiNetRoutingSpec{.routing_profile = second_profile, .start_layer = 0, .goal_layer = 0},
      MultiNetRoutingSpec{
          .routing_profile = board.data().routing_profile, .start_layer = 0, .goal_layer = 0},
  };
  const MultiNetWorkload first = Built(BuildMultiNetWorkload(
      kMultiNetWorkloadSchemaVersion, board, compiler_profile, specs, specs.size()));
  std::ranges::reverse(specs);
  const MultiNetWorkload second = Built(BuildMultiNetWorkload(
      kMultiNetWorkloadSchemaVersion, board, compiler_profile, specs, specs.size()));

  ASSERT_EQ(first.nets().size(), 2U);
  EXPECT_EQ(first.workload_checksum(), second.workload_checksum());
  EXPECT_EQ(first.compiler_profile(), compiler_profile);
  EXPECT_EQ(second.compiler_profile(), compiler_profile);
  EXPECT_EQ(first.nets(), second.nets());
  EXPECT_LT(first.nets()[0].request.net.id, first.nets()[1].request.net.id);
  EXPECT_NE(first.nets()[0].request.net, first.nets()[1].request.net);
  EXPECT_NE(first.nets()[0].routing_profile_fingerprint,
            first.nets()[1].routing_profile_fingerprint);
  for (const PreparedNetRoutingContext& context : first.nets()) {
    EXPECT_EQ(context.request.net, context.routing_profile.net);
    EXPECT_EQ(context.compiled_board.routing_profile(), context.routing_profile);
    EXPECT_EQ(context.compiled_board.source_board_content_hash(), board.content_hash());
    EXPECT_FALSE(
        routing::ValidateCompiledBoardAssociation(board, context.compiled_board).has_value());
    EXPECT_FALSE(
        routing::ValidateTwoTerminalRouteRequest(board, context.compiled_board, context.request)
            .has_value());
  }
}

TEST(MultiNetWorkloadTest, ChecksumEncodingHasGoldenAndFieldSensitivity) {
  const std::array records = {
      internal::MultiNetWorkloadChecksumRecordV1{
          .net = {.id = 11, .generation = 13},
          .routing_profile_fingerprint = 17,
          .rule_bucket_identity = 19,
          .start = {.x = 23, .y = -29},
          .goal = {.x = 31, .y = -37},
          .start_layer = 41,
          .goal_layer = 43,
      },
      internal::MultiNetWorkloadChecksumRecordV1{
          .net = {.id = 47, .generation = 53},
          .routing_profile_fingerprint = 59,
          .rule_bucket_identity = 61,
          .start = {.x = 67, .y = -71},
          .goal = {.x = 73, .y = -79},
          .start_layer = 83,
          .goal_layer = 89,
      },
  };
  const std::uint64_t golden = internal::ComputeMultiNetWorkloadChecksumV1(1, 2, 3, 5, records);
  EXPECT_EQ(golden, 15'373'136'221'856'123'448ULL);

  for (std::size_t field = 0; field < 10; ++field) {
    std::array changed = records;
    switch (field) {
      case 0:
        ++changed[0].net.id;
        break;
      case 1:
        ++changed[0].net.generation;
        break;
      case 2:
        ++changed[0].routing_profile_fingerprint;
        break;
      case 3:
        ++changed[0].rule_bucket_identity;
        break;
      case 4:
        ++changed[0].start.x;
        break;
      case 5:
        ++changed[0].start.y;
        break;
      case 6:
        ++changed[0].goal.x;
        break;
      case 7:
        ++changed[0].goal.y;
        break;
      case 8:
        ++changed[0].start_layer;
        break;
      case 9:
        ++changed[0].goal_layer;
        break;
    }
    EXPECT_NE(internal::ComputeMultiNetWorkloadChecksumV1(1, 2, 3, 5, changed), golden);
  }
  EXPECT_NE(internal::ComputeMultiNetWorkloadChecksumV1(2, 2, 3, 5, records), golden);
  EXPECT_NE(internal::ComputeMultiNetWorkloadChecksumV1(1, 3, 3, 5, records), golden);
  EXPECT_NE(internal::ComputeMultiNetWorkloadChecksumV1(1, 2, 5, 5, records), golden);
  EXPECT_NE(internal::ComputeMultiNetWorkloadChecksumV1(1, 2, 3, 7, records), golden);
}

TEST(MultiNetWorkloadTest, BindsOneWorldToEveryDistinctWorkloadNet) {
  board_ir::BoardData board_data = test_support::ValidM1TwoNetBoardData();
  board_data.obstacles.clear();
  const board_ir::BoardSnapshot board = test_support::Snapshot(std::move(board_data));
  const geometry_compiler::CompilerProfile compiler_profile =
      test_support::DefaultCompilerProfile({0});
  board_ir::RoutingProfile second_profile = board.data().routing_profile;
  second_profile.net = board.data().nets[1].ref;
  const std::array specs = {
      MultiNetRoutingSpec{
          .routing_profile = board.data().routing_profile, .start_layer = 0, .goal_layer = 0},
      MultiNetRoutingSpec{.routing_profile = second_profile, .start_layer = 0, .goal_layer = 0},
  };
  const MultiNetWorkload workload = Built(BuildMultiNetWorkload(
      kMultiNetWorkloadSchemaVersion, board, compiler_profile, specs, specs.size()));

  std::vector<CandidatePool> pools;
  for (std::size_t index = 0; index < workload.nets().size(); ++index) {
    const PreparedNetRoutingContext& context = workload.nets()[index];
    pools.push_back(CandidatePool{
        .net = context.request.net,
        .candidates = {CandidateFor(board, context, index + 1U)},
    });
  }
  const PreparedNetRoutingContext& first_context = workload.nets().front();
  ResourceCapacityModelResult capacity_result = BuildResourceCapacityModel(
      kResourceCapacityModelSchemaVersion, board, first_context.compiled_board, 1, {});
  ASSERT_TRUE(std::holds_alternative<ResourceCapacityModel>(capacity_result));
  ResourceCapacityModel capacities = std::get<ResourceCapacityModel>(std::move(capacity_result));
  PriceSnapshotResult price_result =
      BuildPriceSnapshot(kPriceSnapshotSchemaVersion, capacities, 0, {});
  ASSERT_TRUE(std::holds_alternative<PriceSnapshot>(price_result));
  const OneWorldAllocationRequest request{
      .associations =
          AllocationAssociations{
              .board_content_hash = workload.board_content_hash(),
              .compiler_profile_fingerprint = workload.compiler_profile_fingerprint(),
              .geometry_compiler_version = workload.geometry_compiler_version(),
          },
      .capacities = capacities,
      .prices = std::get<PriceSnapshot>(std::move(price_result)),
      .intrinsic_cost_weight = 1,
      .limits = OneWorldAllocatorLimits{},
      .pools = pools,
      .workload = &workload,
  };
  const OneWorldAllocationResult result = AllocateOneWorld(request);
  ASSERT_TRUE(std::holds_alternative<OneWorldAllocation>(result));
  const OneWorldAllocation& world = std::get<OneWorldAllocation>(result);
  ASSERT_EQ(world.selections.size(), 2U);
  EXPECT_EQ(world.selected_net_count, 2U);
  EXPECT_EQ(world.no_candidate_net_count, 0U);
  EXPECT_EQ(world.workload_checksum, workload.workload_checksum());
  EXPECT_EQ(world.schema_version, kOneWorldAllocationSchemaVersion);
  EXPECT_NE(world.selections[0].net, world.selections[1].net);

  OneWorldAllocationRequest missing = request;
  missing.pools.pop_back();
  const OneWorldAllocationResult rejected = AllocateOneWorld(missing);
  ASSERT_TRUE(std::holds_alternative<AllocationError>(rejected));
  EXPECT_EQ(std::get<AllocationError>(rejected).invariant_id, "allocator.pool.workload_roster.v1");

  OneWorldAllocationRequest legacy = request;
  legacy.schema_version = kOneWorldAllocationSchemaVersionV1;
  const OneWorldAllocationResult legacy_rejected = AllocateOneWorld(legacy);
  ASSERT_TRUE(std::holds_alternative<AllocationError>(legacy_rejected));
  EXPECT_EQ(std::get<AllocationError>(legacy_rejected).invariant_id,
            "allocator.workload.schema.v2");
}

TEST(MultiNetWorkloadTest, CandidateStoreWorkIsLocalToAuthenticNetPool) {
  board_ir::BoardData board_data = test_support::ValidM1TwoNetBoardData();
  board_data.obstacles.clear();
  const board_ir::BoardSnapshot board = test_support::Snapshot(std::move(board_data));
  const geometry_compiler::CompilerProfile compiler_profile =
      test_support::DefaultCompilerProfile({0});
  board_ir::RoutingProfile second_profile = board.data().routing_profile;
  second_profile.net = board.data().nets[1].ref;
  const std::array specs = {
      MultiNetRoutingSpec{
          .routing_profile = board.data().routing_profile, .start_layer = 0, .goal_layer = 0},
      MultiNetRoutingSpec{.routing_profile = second_profile, .start_layer = 0, .goal_layer = 0},
  };
  const MultiNetWorkload workload = Built(BuildMultiNetWorkload(
      kMultiNetWorkloadSchemaVersion, board, compiler_profile, specs, specs.size()));
  const PreparedNetRoutingContext& first = workload.nets()[0];
  const PreparedNetRoutingContext& second = workload.nets()[1];
  const candidates::CandidateStoreConfig config{
      .maximum_candidates_per_net = 4,
      .maximum_candidate_bytes_per_net = 1U << 20U,
      .maximum_rejection_records = 32,
  };

  const auto admit = [&board](candidates::CandidateStore& store,
                              const PreparedNetRoutingContext& context,
                              std::uint64_t query_identity) {
    return store.Admit(
        candidates::CandidateAdmissionContext{
            .board = board,
            .compiled_board = context.compiled_board,
            .request = context.request,
        },
        DraftFor(board, context, query_identity));
  };

  candidates::CandidateStore isolated(config);
  ASSERT_TRUE(std::holds_alternative<candidates::StoredCandidate>(admit(isolated, first, 1)));
  const candidates::CandidateStoreAdmissionResult isolated_result = admit(isolated, first, 2);
  const candidates::CandidateStoreTelemetry isolated_telemetry = isolated.telemetry();

  candidates::CandidateStore crowded(config);
  ASSERT_TRUE(std::holds_alternative<candidates::StoredCandidate>(admit(crowded, first, 1)));
  ASSERT_TRUE(std::holds_alternative<candidates::StoredCandidate>(admit(crowded, second, 3)));
  const candidates::CandidateStoreAdmissionResult crowded_result = admit(crowded, first, 2);
  const candidates::CandidateStoreTelemetry crowded_telemetry = crowded.telemetry();

  EXPECT_EQ(isolated_result.index(), crowded_result.index());
  EXPECT_EQ(isolated_telemetry.last_publication_candidate_inspections,
            crowded_telemetry.last_publication_candidate_inspections);
  EXPECT_EQ(isolated_telemetry.last_duplicate_equality_checks,
            crowded_telemetry.last_duplicate_equality_checks);
  EXPECT_EQ(crowded.Enumerate(second.request.net).size(), 1U);
}

TEST(MultiNetWorkloadTest, CandidateStorePersistsNetContextWithoutRetainedCandidate) {
  board_ir::BoardData board_data = test_support::ValidM1TwoNetBoardData();
  board_data.obstacles.clear();
  const board_ir::BoardSnapshot board = test_support::Snapshot(std::move(board_data));
  const geometry_compiler::CompilerProfile compiler_profile =
      test_support::DefaultCompilerProfile({0});
  board_ir::RoutingProfile first_profile = board.data().routing_profile;
  board_ir::RoutingProfile changed_profile = first_profile;
  ++changed_profile.clearance;
  const PreparedNetRoutingContext first =
      PreparedContext(board, compiler_profile, std::move(first_profile));
  const PreparedNetRoutingContext changed =
      PreparedContext(board, compiler_profile, std::move(changed_profile));
  candidates::CandidateStore store(candidates::CandidateStoreConfig{
      .maximum_candidates_per_net = 1,
      .maximum_candidate_bytes_per_net = 1,
      .maximum_rejection_records = 8,
  });

  const candidates::CandidateStoreAdmissionResult budget = store.Admit(
      candidates::CandidateAdmissionContext{
          .board = board,
          .compiled_board = first.compiled_board,
          .request = first.request,
      },
      DraftFor(board, first, 1));
  ASSERT_TRUE(std::holds_alternative<candidates::CandidateRejection>(budget));
  EXPECT_EQ(std::get<candidates::CandidateRejection>(budget).code,
            candidates::CandidateRejectionCode::kBudgetExhausted);
  EXPECT_TRUE(store.Enumerate(first.request.net).empty());

  const candidates::CandidateStoreAdmissionResult drift = store.Admit(
      candidates::CandidateAdmissionContext{
          .board = board,
          .compiled_board = changed.compiled_board,
          .request = changed.request,
      },
      DraftFor(board, changed, 2));
  ASSERT_TRUE(std::holds_alternative<candidates::CandidateRejection>(drift));
  EXPECT_EQ(std::get<candidates::CandidateRejection>(drift).invariant_id,
            "candidate.store.net_context_drift.v1");
}

TEST(MultiNetWorkloadTest, CpuProducerEvidenceCannotMoveBetweenCoincidentAuthenticNets) {
  board_ir::BoardData board_data = test_support::ValidM1TwoNetBoardData();
  board_data.obstacles.clear();
  board_data.terminals[2].center = board_data.terminals[0].center;
  board_data.terminals[2].connection_region = board_data.terminals[0].connection_region;
  board_data.terminals[3].center = board_data.terminals[1].center;
  board_data.terminals[3].connection_region = board_data.terminals[1].connection_region;
  const board_ir::BoardSnapshot board = test_support::Snapshot(std::move(board_data));
  const geometry_compiler::CompilerProfile compiler_profile =
      test_support::DefaultCompilerProfile({0});
  board_ir::RoutingProfile second_profile = board.data().routing_profile;
  second_profile.net = board.data().nets[1].ref;
  const std::array specs = {
      MultiNetRoutingSpec{
          .routing_profile = board.data().routing_profile, .start_layer = 0, .goal_layer = 0},
      MultiNetRoutingSpec{.routing_profile = second_profile, .start_layer = 0, .goal_layer = 0},
  };
  const MultiNetWorkload workload = Built(BuildMultiNetWorkload(
      kMultiNetWorkloadSchemaVersion, board, compiler_profile, specs, specs.size()));
  const PreparedNetRoutingContext& first = workload.nets()[0];
  const PreparedNetRoutingContext& second = workload.nets()[1];
  const routing::NormalizedCandidateGenerationPolicy first_policy =
      test_support::NormalizePolicy(first.compiled_board, first.request);
  const routing::NormalizedCandidateGenerationPolicy second_policy =
      test_support::NormalizePolicy(second.compiled_board, second.request);
  const routing::CpuRoute route =
      test_support::CpuRouteForCandidate(board, first.compiled_board, first.request);

  const candidates::CandidateDraftBuildResult moved =
      candidates::BuildGeneratedCandidateFromCpuRoute(
          board, second.compiled_board, second.request, second_policy, route,
          candidates::CandidateSchedulingIdentity{.batch_identity = 7, .query_identity = 11});

  ASSERT_TRUE(std::holds_alternative<candidates::CandidateRejection>(moved));
  EXPECT_EQ(std::get<candidates::CandidateRejection>(moved).invariant_id,
            "candidate.builder.cpu_request_attribution.v1");
  EXPECT_EQ(first_policy.identity, second_policy.identity);
}

TEST(MultiNetWorkloadTest, RejectsDuplicateOrForeignProfilesBeforePublication) {
  board_ir::BoardData board_data = test_support::ValidM1TwoNetBoardData();
  board_data.obstacles.clear();
  const board_ir::BoardSnapshot board = test_support::Snapshot(std::move(board_data));
  const geometry_compiler::CompilerProfile compiler_profile =
      test_support::DefaultCompilerProfile({0});
  const MultiNetRoutingSpec first{
      .routing_profile = board.data().routing_profile, .start_layer = 0, .goal_layer = 0};
  const std::array duplicates = {first, first};
  const MultiNetWorkloadResult duplicate = BuildMultiNetWorkload(
      kMultiNetWorkloadSchemaVersion, board, compiler_profile, duplicates, duplicates.size());
  ASSERT_TRUE(std::holds_alternative<MultiNetWorkloadError>(duplicate));
  EXPECT_EQ(std::get<MultiNetWorkloadError>(duplicate).code,
            MultiNetWorkloadErrorCode::kDuplicateNet);

  MultiNetRoutingSpec foreign = first;
  foreign.routing_profile.net = board_ir::EntityRef{.id = 999, .generation = 0};
  const MultiNetWorkloadResult rejected =
      BuildMultiNetWorkload(kMultiNetWorkloadSchemaVersion, board, compiler_profile,
                            std::span<const MultiNetRoutingSpec>(&foreign, 1), 1);
  ASSERT_TRUE(std::holds_alternative<MultiNetWorkloadError>(rejected));
  EXPECT_EQ(std::get<MultiNetWorkloadError>(rejected).code,
            MultiNetWorkloadErrorCode::kInvalidRoutingProfile);
}

TEST(MultiNetWorkloadTest, EnforcesCumulativeCompilationWorkBounds) {
  board_ir::BoardData board_data = test_support::ValidM1TwoNetBoardData();
  board_data.obstacles.clear();
  const board_ir::BoardSnapshot board = test_support::Snapshot(std::move(board_data));
  const geometry_compiler::CompilerProfile compiler_profile =
      test_support::DefaultCompilerProfile({0});
  const std::array specs = {MultiNetRoutingSpec{
      .routing_profile = board.data().routing_profile,
      .start_layer = 0,
      .goal_layer = 0,
  }};

  const MultiNetWorkloadResult result = BuildMultiNetWorkload(
      kMultiNetWorkloadSchemaVersion, board, compiler_profile, specs,
      MultiNetWorkloadLimits{
          .maximum_nets = 1,
          .maximum_compiled_nodes = 1,
          .maximum_compiled_host_bytes = kMaximumMultiNetWorkloadCompiledHostBytesV1});

  ASSERT_TRUE(std::holds_alternative<MultiNetWorkloadError>(result));
  EXPECT_EQ(std::get<MultiNetWorkloadError>(result).code,
            MultiNetWorkloadErrorCode::kWorkBoundExceeded);
}

TEST(MultiNetWorkloadTest, FailureSelectionIsCanonicalAcrossSpecPermutations) {
  board_ir::BoardData board_data = test_support::ValidM1TwoNetBoardData();
  board_data.obstacles.clear();
  const board_ir::BoardSnapshot board = test_support::Snapshot(std::move(board_data));
  const geometry_compiler::CompilerProfile compiler_profile =
      test_support::DefaultCompilerProfile({0});
  board_ir::RoutingProfile second_profile = board.data().routing_profile;
  second_profile.net = board.data().nets[1].ref;
  std::array specs = {
      MultiNetRoutingSpec{.routing_profile = second_profile, .start_layer = 0, .goal_layer = 0},
      MultiNetRoutingSpec{
          .routing_profile = board.data().routing_profile, .start_layer = 0, .goal_layer = 0},
  };
  const std::array first_only = {specs[1]};
  const MultiNetWorkload one = Built(BuildMultiNetWorkload(
      kMultiNetWorkloadSchemaVersion, board, compiler_profile, first_only, first_only.size()));
  const MultiNetWorkloadLimits one_context_limit{
      .maximum_nets = specs.size(),
      .maximum_compiled_nodes = one.compiled_node_count(),
      .maximum_compiled_host_bytes = one.compiled_host_bytes(),
  };
  const auto build_error = [&](const auto& ordered_specs, const MultiNetWorkloadLimits& limits) {
    const MultiNetWorkloadResult result = BuildMultiNetWorkload(
        kMultiNetWorkloadSchemaVersion, board, compiler_profile, ordered_specs, limits);
    EXPECT_TRUE(std::holds_alternative<MultiNetWorkloadError>(result));
    return std::get<MultiNetWorkloadError>(result);
  };

  const MultiNetWorkloadError forward_bound = build_error(specs, one_context_limit);
  std::ranges::reverse(specs);
  const MultiNetWorkloadError reverse_bound = build_error(specs, one_context_limit);
  EXPECT_EQ(forward_bound, reverse_bound);
  EXPECT_EQ(forward_bound.code, MultiNetWorkloadErrorCode::kWorkBoundExceeded);
  EXPECT_EQ(forward_bound.net, board.data().nets[1].ref);

  for (MultiNetRoutingSpec& spec : specs) {
    spec.routing_profile.nominal_width = 0;
  }
  const MultiNetWorkloadLimits hard_limits{
      .maximum_nets = specs.size(),
      .maximum_compiled_nodes = kMaximumMultiNetWorkloadCompiledNodesV1,
      .maximum_compiled_host_bytes = kMaximumMultiNetWorkloadCompiledHostBytesV1,
  };
  const MultiNetWorkloadError forward_invalid = build_error(specs, hard_limits);
  std::ranges::reverse(specs);
  const MultiNetWorkloadError reverse_invalid = build_error(specs, hard_limits);
  EXPECT_EQ(forward_invalid, reverse_invalid);
  EXPECT_EQ(forward_invalid.code, MultiNetWorkloadErrorCode::kInvalidRoutingProfile);
  EXPECT_EQ(forward_invalid.net, board.data().nets[0].ref);
}

TEST(MultiNetWorkloadTest, AllocationFailureFallbackIsStableAndAllocationFree) {
  board_ir::BoardData board_data = test_support::ValidM1TwoNetBoardData();
  board_data.obstacles.clear();
  const board_ir::BoardSnapshot board = test_support::Snapshot(std::move(board_data));
  const geometry_compiler::CompilerProfile compiler_profile =
      test_support::DefaultCompilerProfile({0});
  const std::array specs = {MultiNetRoutingSpec{
      .routing_profile = board.data().routing_profile,
      .start_layer = 0,
      .goal_layer = 0,
  }};

  for (const internal::MultiNetWorkloadFaultForTesting fault :
       {internal::MultiNetWorkloadFaultForTesting::kBadAlloc,
        internal::MultiNetWorkloadFaultForTesting::kLengthError}) {
    internal::SetMultiNetWorkloadFaultForTesting(fault);
    const MultiNetWorkloadResult result = BuildMultiNetWorkload(
        kMultiNetWorkloadSchemaVersion, board, compiler_profile, specs, specs.size());
    ASSERT_TRUE(std::holds_alternative<MultiNetWorkloadError>(result));
    EXPECT_EQ(std::get<MultiNetWorkloadError>(result).code,
              MultiNetWorkloadErrorCode::kResourceExhausted);
    EXPECT_FALSE(std::get<MultiNetWorkloadError>(result).invariant_id.empty());
  }
}

}  // namespace
}  // namespace apgar::allocator
