#include "apgar/allocator/cpu_candidate_pool_preparation.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

#include "apgar/allocator/multi_net_workload.h"
#include "apgar/board_ir/board.h"
#include "apgar/geometry_compiler/compiled_board.h"
#include "src/allocator/cpu_candidate_pool_preparation_internal.h"
#include "tests/support/board_builder.h"
#include "tests/support/compiler_builder.h"
#include "tests/support/google_test.h"

namespace apgar::allocator {
namespace {

struct Fixture {
  board_ir::BoardSnapshot board;
  MultiNetWorkload workload;
};

[[nodiscard]] board_ir::BoardSnapshot Snapshot(board_ir::BoardData data) {
  board_ir::BoardCreationResult result = board_ir::CreateBoardSnapshot(std::move(data));
  EXPECT_TRUE(std::holds_alternative<board_ir::BoardSnapshot>(result));
  if (!std::holds_alternative<board_ir::BoardSnapshot>(result)) {
    std::abort();
  }
  return std::get<board_ir::BoardSnapshot>(std::move(result));
}

[[nodiscard]] MultiNetWorkload RequireWorkload(MultiNetWorkloadResult result) {
  EXPECT_TRUE(std::holds_alternative<MultiNetWorkload>(result));
  if (!std::holds_alternative<MultiNetWorkload>(result)) {
    std::abort();
  }
  return std::get<MultiNetWorkload>(std::move(result));
}

[[nodiscard]] Fixture MakeTwoNetFixture() {
  board_ir::BoardData data = test_support::ValidM1TwoNetBoardData();
  data.obstacles.clear();
  board_ir::BoardSnapshot board = Snapshot(std::move(data));
  const geometry_compiler::CompilerProfile profile = test_support::DefaultCompilerProfile({0});
  board_ir::RoutingProfile second = board.data().routing_profile;
  second.net = board.data().nets[1].ref;
  const std::array specs = {
      MultiNetRoutingSpec{
          .routing_profile = board.data().routing_profile,
          .start_layer = 0,
          .goal_layer = 0,
      },
      MultiNetRoutingSpec{
          .routing_profile = second,
          .start_layer = 0,
          .goal_layer = 0,
      },
  };
  MultiNetWorkload workload = RequireWorkload(
      BuildMultiNetWorkload(kMultiNetWorkloadSchemaVersion, board, profile, specs, specs.size()));
  return Fixture{.board = std::move(board), .workload = std::move(workload)};
}

[[nodiscard]] Fixture MakeDisconnectedFixture() {
  board_ir::BoardSnapshot board = Snapshot(test_support::ValidM1BoardData());
  geometry_compiler::CompilerProfile profile = test_support::DefaultCompilerProfile({0});
  profile.heading_mask = static_cast<board_ir::HeadingMask>(board_ir::Heading::kHorizontal);
  profile.compilation_roi = {
      .min = {.x = 0, .y = 0},
      .max = {.x = 100, .y = 0},
  };
  profile.active_regions = {{.layer = 0, .bounds = profile.compilation_roi}};
  const std::array specs = {MultiNetRoutingSpec{
      .routing_profile = board.data().routing_profile,
      .start_layer = 0,
      .goal_layer = 0,
  }};
  MultiNetWorkload workload = RequireWorkload(
      BuildMultiNetWorkload(kMultiNetWorkloadSchemaVersion, board, profile, specs, specs.size()));
  return Fixture{.board = std::move(board), .workload = std::move(workload)};
}

[[nodiscard]] Fixture MakeDirectionalFixture(board_ir::Point64 goal) {
  board_ir::BoardData data = test_support::ValidM1BoardData();
  data.obstacles.clear();
  data.terminals[1].center = goal;
  data.terminals[1].connection_region = {.min = goal, .max = goal};
  board_ir::BoardSnapshot board = Snapshot(std::move(data));
  const std::array specs = {MultiNetRoutingSpec{
      .routing_profile = board.data().routing_profile,
      .start_layer = 0,
      .goal_layer = 0,
  }};
  MultiNetWorkload workload = RequireWorkload(
      BuildMultiNetWorkload(kMultiNetWorkloadSchemaVersion, board,
                            test_support::DefaultCompilerProfile({0}), specs, specs.size()));
  return Fixture{.board = std::move(board), .workload = std::move(workload)};
}

[[nodiscard]] std::uint64_t PolicyIdentity(const geometry_compiler::CompiledBoard& board,
                                           const routing::PlanarRouteRequest& request) {
  routing::CandidatePolicyResult result =
      routing::NormalizeCandidateGenerationPolicy(board, request.candidate_policy);
  EXPECT_TRUE(std::holds_alternative<routing::NormalizedCandidateGenerationPolicy>(result));
  if (!std::holds_alternative<routing::NormalizedCandidateGenerationPolicy>(result)) {
    std::abort();
  }
  return std::get<routing::NormalizedCandidateGenerationPolicy>(result).identity;
}

[[nodiscard]] CpuCandidatePoolPreparationConfig Config(std::size_t net_count,
                                                       std::uint32_t pool_size = 4) {
  const std::uint64_t columns = net_count * pool_size;
  CpuCandidatePoolPreparationConfig config;
  config.requested_candidates_per_net = pool_size;
  config.deterministic_seed = 0x1234'5678'9abc'def0ULL;
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

[[nodiscard]] std::unique_ptr<PersistentCpuCandidatePoolPreparer> Preparer(std::uint32_t workers) {
  PersistentCpuCandidatePoolPreparerResult result =
      CreatePersistentCpuCandidatePoolPreparer({.worker_count = workers});
  EXPECT_TRUE(std::holds_alternative<std::unique_ptr<PersistentCpuCandidatePoolPreparer>>(result));
  if (!std::holds_alternative<std::unique_ptr<PersistentCpuCandidatePoolPreparer>>(result)) {
    std::abort();
  }
  return std::get<std::unique_ptr<PersistentCpuCandidatePoolPreparer>>(std::move(result));
}

[[nodiscard]] PreparedCpuCandidatePools Prepared(PreparedCpuCandidatePoolsResult result) {
  EXPECT_TRUE(std::holds_alternative<PreparedCpuCandidatePools>(result))
      << (std::holds_alternative<CpuCandidatePoolPreparationError>(result)
              ? std::get<CpuCandidatePoolPreparationError>(result).detail
              : "");
  if (!std::holds_alternative<PreparedCpuCandidatePools>(result)) {
    std::abort();
  }
  return std::get<PreparedCpuCandidatePools>(std::move(result));
}

void ExpectPoolSemanticsEqual(const PreparedCpuCandidatePools& left,
                              const PreparedCpuCandidatePools& right) {
  EXPECT_EQ(left.config(), right.config());
  EXPECT_EQ(left.batch_identity(), right.batch_identity());
  EXPECT_EQ(left.counters(), right.counters());
  EXPECT_EQ(left.columns(), right.columns());
  EXPECT_EQ(left.preparation_checksum(), right.preparation_checksum());
  ASSERT_EQ(left.pools().size(), right.pools().size());
  for (std::size_t pool_index = 0; pool_index < left.pools().size(); ++pool_index) {
    const CandidatePool& left_pool = left.pools()[pool_index];
    const CandidatePool& right_pool = right.pools()[pool_index];
    EXPECT_EQ(left_pool.net, right_pool.net);
    ASSERT_EQ(left_pool.candidates.size(), right_pool.candidates.size());
    for (std::size_t candidate_index = 0; candidate_index < left_pool.candidates.size();
         ++candidate_index) {
      EXPECT_EQ(*left_pool.candidates[candidate_index], *right_pool.candidates[candidate_index]);
    }
  }
}

TEST(CpuCandidatePoolPreparationTest, WorkerCountDoesNotChangeSemanticOutput) {
  const Fixture fixture = MakeTwoNetFixture();
  const CpuCandidatePoolPreparationConfig config = Config(fixture.workload.nets().size());
  std::unique_ptr<PersistentCpuCandidatePoolPreparer> serial = Preparer(1);
  std::unique_ptr<PersistentCpuCandidatePoolPreparer> parallel = Preparer(4);

  PreparedCpuCandidatePools serial_result =
      Prepared(PrepareInitialCpuCandidatePools(*serial, fixture.board, fixture.workload, config));
  PreparedCpuCandidatePools parallel_result =
      Prepared(PrepareInitialCpuCandidatePools(*parallel, fixture.board, fixture.workload, config));

  ExpectPoolSemanticsEqual(serial_result, parallel_result);
  EXPECT_EQ(serial->telemetry().workers_started, 1U);
  EXPECT_EQ(parallel->telemetry().workers_started, 4U);
  EXPECT_EQ(serial_result.counters().requested_columns, 8U);
  EXPECT_EQ(serial_result.counters().executed_route_queries, 8U);
  EXPECT_EQ(serial_result.pools().size(), 2U);
  EXPECT_EQ(
      serial_result.counters().retained_candidates,
      serial_result.pools()[0].candidates.size() + serial_result.pools()[1].candidates.size());
}

TEST(CpuCandidatePoolPreparationTest, ReusesPersistentWorkersAcrossFreshOwnedStores) {
  const Fixture fixture = MakeTwoNetFixture();
  const CpuCandidatePoolPreparationConfig config = Config(fixture.workload.nets().size());
  std::unique_ptr<PersistentCpuCandidatePoolPreparer> preparer = Preparer(2);

  PreparedCpuCandidatePools first =
      Prepared(PrepareInitialCpuCandidatePools(*preparer, fixture.board, fixture.workload, config));
  const PersistentCpuCandidatePoolTelemetry after_first = preparer->telemetry();
  PreparedCpuCandidatePools second =
      Prepared(PrepareInitialCpuCandidatePools(*preparer, fixture.board, fixture.workload, config));
  const PersistentCpuCandidatePoolTelemetry after_second = preparer->telemetry();

  ExpectPoolSemanticsEqual(first, second);
  EXPECT_EQ(after_first.workers_started, 2U);
  EXPECT_EQ(after_second.workers_started, 2U);
  EXPECT_EQ(after_first.invocations_started, 1U);
  EXPECT_EQ(after_first.invocations_completed, 1U);
  EXPECT_EQ(after_second.invocations_started, 2U);
  EXPECT_EQ(after_second.invocations_completed, 2U);
  EXPECT_EQ(after_second.jobs_dispatched, 2U * after_first.jobs_dispatched);
  EXPECT_NE(&first.candidate_store(), &second.candidate_store());
}

TEST(CpuCandidatePoolPreparationTest, WorkBoundFailureLeavesPersistentWorkersReusable) {
  const Fixture fixture = MakeTwoNetFixture();
  std::unique_ptr<PersistentCpuCandidatePoolPreparer> preparer = Preparer(2);
  CpuCandidatePoolPreparationConfig bounded = Config(fixture.workload.nets().size());
  bounded.route_limits.maximum_work_units = 1;

  const PreparedCpuCandidatePoolsResult failed =
      PrepareInitialCpuCandidatePools(*preparer, fixture.board, fixture.workload, bounded);
  ASSERT_TRUE(std::holds_alternative<CpuCandidatePoolPreparationError>(failed));
  EXPECT_EQ(std::get<CpuCandidatePoolPreparationError>(failed).code,
            CpuCandidatePoolPreparationErrorCode::kWorkBoundExceeded);
  const PersistentCpuCandidatePoolTelemetry after_failure = preparer->telemetry();
  EXPECT_EQ(after_failure.workers_started, 2U);
  EXPECT_EQ(after_failure.invocations_started, 1U);
  EXPECT_EQ(after_failure.invocations_completed, 1U);

  const CpuCandidatePoolPreparationConfig unbounded = Config(fixture.workload.nets().size());
  const PreparedCpuCandidatePools recovered = Prepared(
      PrepareInitialCpuCandidatePools(*preparer, fixture.board, fixture.workload, unbounded));
  EXPECT_EQ(recovered.pools().size(), fixture.workload.nets().size());
  const PersistentCpuCandidatePoolTelemetry after_recovery = preparer->telemetry();
  EXPECT_EQ(after_recovery.workers_started, 2U);
  EXPECT_EQ(after_recovery.invocations_started, 2U);
  EXPECT_EQ(after_recovery.invocations_completed, 2U);
}

TEST(CpuCandidatePoolPreparationTest, RecordsOneDisconnectedProofAndSkipsAlternatives) {
  const Fixture fixture = MakeDisconnectedFixture();
  const CpuCandidatePoolPreparationConfig config = Config(1);
  std::unique_ptr<PersistentCpuCandidatePoolPreparer> preparer = Preparer(2);

  const PreparedCpuCandidatePools result =
      Prepared(PrepareInitialCpuCandidatePools(*preparer, fixture.board, fixture.workload, config));

  ASSERT_EQ(result.columns().size(), 4U);
  EXPECT_EQ(result.columns()[0].outcome, CpuCandidatePoolColumnOutcome::kRouteDisconnected);
  EXPECT_NE(result.columns()[0].query_identity, 0U);
  for (std::size_t index = 1; index < result.columns().size(); ++index) {
    EXPECT_EQ(result.columns()[index].outcome,
              CpuCandidatePoolColumnOutcome::kSkippedAfterDisconnectedProof);
    EXPECT_EQ(result.columns()[index].query_identity, 0U);
    EXPECT_EQ(result.columns()[index].policy_identity, 0U);
  }
  EXPECT_EQ(result.counters().executed_route_queries, 1U);
  EXPECT_EQ(result.counters().disconnected_proofs, 1U);
  EXPECT_EQ(result.counters().skipped_columns, 3U);
  EXPECT_EQ(result.counters().retained_candidates, 0U);
  ASSERT_EQ(result.pools().size(), 1U);
  EXPECT_TRUE(result.pools()[0].candidates.empty());
  ASSERT_EQ(result.candidate_store().Rejections().size(), 1U);
}

TEST(CpuCandidatePoolPreparationTest, PreflightsAggregateAndStoreBoundsBeforeDispatch) {
  const Fixture fixture = MakeTwoNetFixture();
  std::unique_ptr<PersistentCpuCandidatePoolPreparer> preparer = Preparer(2);
  CpuCandidatePoolPreparationConfig equality = Config(fixture.workload.nets().size());
  constexpr std::uint64_t kNetCount = 2;
  constexpr std::uint64_t kColumnCount = 8;
  constexpr std::uint64_t kPolicyEntries = 6;
  const std::uint64_t draft_byte_upper =
      4'096U + equality.route_limits.maximum_reconstruction_states * 512U + 128U;
  equality.limits.maximum_nets = kNetCount;
  equality.limits.maximum_route_queries = kColumnCount;
  equality.limits.maximum_total_route_work_units =
      kColumnCount * equality.route_limits.maximum_work_units;
  equality.limits.maximum_concurrent_route_records =
      2U * equality.route_limits.maximum_record_count;
  equality.limits.maximum_concurrent_queue_entries = 2U * equality.route_limits.maximum_queue_size;
  equality.limits.maximum_concurrent_reconstruction_states =
      2U * equality.route_limits.maximum_reconstruction_states;
  equality.limits.maximum_policy_resource_entries = kPolicyEntries;
  equality.limits.maximum_retained_candidate_bytes =
      kNetCount * equality.store_config.maximum_candidate_bytes_per_net;
  equality.limits.maximum_candidate_draft_bytes = draft_byte_upper;
  equality.limits.maximum_generated_candidate_bytes =
      kColumnCount * draft_byte_upper +
      kNetCount * equality.route_limits.maximum_reconstruction_states * 40U;

  const PreparedCpuCandidatePools accepted = Prepared(
      PrepareInitialCpuCandidatePools(*preparer, fixture.board, fixture.workload, equality));
  EXPECT_EQ(accepted.columns().size(), kColumnCount);
  const std::uint64_t dispatched_after_equality = preparer->telemetry().jobs_dispatched;

  const auto expect_preflight_rejection = [&](const CpuCandidatePoolPreparationConfig& config,
                                              std::string_view invariant) {
    const PreparedCpuCandidatePoolsResult result =
        PrepareInitialCpuCandidatePools(*preparer, fixture.board, fixture.workload, config);
    ASSERT_TRUE(std::holds_alternative<CpuCandidatePoolPreparationError>(result));
    EXPECT_EQ(std::get<CpuCandidatePoolPreparationError>(result).invariant_id, invariant);
    EXPECT_EQ(preparer->telemetry().jobs_dispatched, dispatched_after_equality);
  };

  CpuCandidatePoolPreparationConfig one_over = equality;
  --one_over.limits.maximum_nets;
  expect_preflight_rejection(one_over, "allocator.cpu_candidate_pool.net_bound.v1");
  one_over = equality;
  --one_over.limits.maximum_route_queries;
  expect_preflight_rejection(one_over, "allocator.cpu_candidate_pool.query_bound.v1");
  one_over = equality;
  --one_over.limits.maximum_total_route_work_units;
  expect_preflight_rejection(one_over, "allocator.cpu_candidate_pool.route_work_bound.v1");
  one_over = equality;
  --one_over.limits.maximum_concurrent_route_records;
  expect_preflight_rejection(one_over, "allocator.cpu_candidate_pool.concurrent_record_bound.v1");
  one_over = equality;
  --one_over.limits.maximum_concurrent_queue_entries;
  expect_preflight_rejection(one_over, "allocator.cpu_candidate_pool.concurrent_queue_bound.v1");
  one_over = equality;
  --one_over.limits.maximum_concurrent_reconstruction_states;
  expect_preflight_rejection(one_over,
                             "allocator.cpu_candidate_pool.concurrent_reconstruction_bound.v1");
  one_over = equality;
  --one_over.limits.maximum_policy_resource_entries;
  expect_preflight_rejection(one_over, "allocator.cpu_candidate_pool.policy_entry_bound.v1");
  one_over = equality;
  --one_over.limits.maximum_retained_candidate_bytes;
  expect_preflight_rejection(one_over, "allocator.cpu_candidate_pool.output_byte_bound.v1");
  one_over = equality;
  --one_over.limits.maximum_candidate_draft_bytes;
  expect_preflight_rejection(one_over, "allocator.cpu_candidate_pool.draft_byte_bound.v1");
  one_over = equality;
  --one_over.limits.maximum_generated_candidate_bytes;
  expect_preflight_rejection(one_over, "allocator.cpu_candidate_pool.output_byte_bound.v1");
  one_over = equality;
  --one_over.store_config.maximum_candidates_per_net;
  expect_preflight_rejection(one_over, "allocator.cpu_candidate_pool.store_capacity.v1");
  one_over = equality;
  --one_over.store_config.maximum_admission_items_per_transaction;
  expect_preflight_rejection(one_over, "allocator.cpu_candidate_pool.store_capacity.v1");
  one_over = equality;
  --one_over.store_config.maximum_rejection_items_per_transaction;
  expect_preflight_rejection(one_over, "allocator.cpu_candidate_pool.store_capacity.v1");
  one_over = equality;
  --one_over.store_config.maximum_rejection_records;
  expect_preflight_rejection(one_over, "allocator.cpu_candidate_pool.store_capacity.v1");
  one_over = equality;
  --one_over.store_config.maximum_expected_pools_per_invocation;
  expect_preflight_rejection(one_over, "allocator.cpu_candidate_pool.store_capacity.v1");
}

TEST(CpuCandidatePoolPreparationTest, DiagnosticLatticeTraceCannotChangeAlternativeSchedule) {
  const Fixture fixture = MakeTwoNetFixture();
  const PreparedNetRoutingContext& context = fixture.workload.nets().front();
  const routing::CpuRouteResult route_result =
      routing::RouteWithCpuAStar(fixture.board, context.compiled_board, context.request);
  ASSERT_TRUE(std::holds_alternative<routing::CpuRoute>(route_result));
  const routing::CpuRoute& route = std::get<routing::CpuRoute>(route_result);
  routing::CpuRoute corrupted = route;
  corrupted.lattice_path.clear();
  ASSERT_TRUE(routing::CpuRouteHasAuthenticatedAStarEvidence(corrupted));

  const auto original = internal::ExtractCpuCandidatePoolAlternativeResourcesV1(
      route, context.compiled_board, context.request,
      PolicyIdentity(context.compiled_board, context.request), route.segments.size() * 1'000U);
  const auto changed = internal::ExtractCpuCandidatePoolAlternativeResourcesV1(
      corrupted, context.compiled_board, context.request,
      PolicyIdentity(context.compiled_board, context.request), route.segments.size() * 1'000U);

  ASSERT_TRUE(std::holds_alternative<std::vector<routing::EdgeResourceKey>>(original));
  ASSERT_TRUE(std::holds_alternative<std::vector<routing::EdgeResourceKey>>(changed));
  EXPECT_EQ(std::get<std::vector<routing::EdgeResourceKey>>(original),
            std::get<std::vector<routing::EdgeResourceKey>>(changed));
}

TEST(CpuCandidatePoolPreparationTest,
     ExpandsForwardAndReverseHVDiagonalRoutesAtExactNonPowerOfTwoBounds) {
  for (const board_ir::Point64 goal : {
           board_ir::Point64{.x = 100, .y = 0},
           board_ir::Point64{.x = 0, .y = 30},
           board_ir::Point64{.x = 30, .y = 30},
           board_ir::Point64{.x = 30, .y = -30},
       }) {
    const Fixture fixture = MakeDirectionalFixture(goal);
    const PreparedNetRoutingContext& context = fixture.workload.nets().front();
    std::optional<std::vector<routing::EdgeResourceKey>> forward_resources;
    for (const bool reverse : {false, true}) {
      routing::PlanarRouteRequest request = context.request;
      if (reverse) {
        std::swap(request.start, request.goal);
        std::swap(request.start_layer, request.goal_layer);
      }
      const routing::CpuRouteResult route_result =
          routing::RouteWithCpuAStar(fixture.board, context.compiled_board, request);
      ASSERT_TRUE(std::holds_alternative<routing::CpuRoute>(route_result));
      const routing::CpuRoute& route = std::get<routing::CpuRoute>(route_result);
      const std::uint64_t policy_identity = PolicyIdentity(context.compiled_board, request);
      const auto generous = internal::ExtractCpuCandidatePoolAlternativeResourcesV1(
          route, context.compiled_board, request, policy_identity, 1'000U);
      ASSERT_TRUE(std::holds_alternative<std::vector<routing::EdgeResourceKey>>(generous));
      const auto& resources = std::get<std::vector<routing::EdgeResourceKey>>(generous);
      ASSERT_FALSE(resources.empty());
      EXPECT_FALSE(std::has_single_bit(resources.size()));
      EXPECT_EQ(resources.capacity(), resources.size());
      if (!reverse) {
        forward_resources = resources;
      } else {
        ASSERT_TRUE(forward_resources.has_value());
        EXPECT_EQ(resources, *forward_resources);
      }

      const auto equality = internal::ExtractCpuCandidatePoolAlternativeResourcesV1(
          route, context.compiled_board, request, policy_identity, resources.size());
      ASSERT_TRUE(std::holds_alternative<std::vector<routing::EdgeResourceKey>>(equality));
      EXPECT_EQ(std::get<std::vector<routing::EdgeResourceKey>>(equality), resources);
      const auto one_under = internal::ExtractCpuCandidatePoolAlternativeResourcesV1(
          route, context.compiled_board, request, policy_identity, resources.size() - 1U);
      ASSERT_TRUE(std::holds_alternative<CpuCandidatePoolPreparationError>(one_under));
      EXPECT_EQ(std::get<CpuCandidatePoolPreparationError>(one_under).invariant_id,
                "allocator.cpu_candidate_pool.base_resource_bound.v1");
    }
  }
}

TEST(CpuCandidatePoolPreparationTest, RejectsForeignRequestAndRoutingProfileAtExtraction) {
  const Fixture fixture = MakeTwoNetFixture();
  const PreparedNetRoutingContext& source = fixture.workload.nets()[0];
  const PreparedNetRoutingContext& foreign = fixture.workload.nets()[1];
  const routing::CpuRouteResult route_result =
      routing::RouteWithCpuAStar(fixture.board, source.compiled_board, source.request);
  ASSERT_TRUE(std::holds_alternative<routing::CpuRoute>(route_result));

  const auto result = internal::ExtractCpuCandidatePoolAlternativeResourcesV1(
      std::get<routing::CpuRoute>(route_result), foreign.compiled_board, foreign.request,
      PolicyIdentity(foreign.compiled_board, foreign.request), 1'000U);

  ASSERT_TRUE(std::holds_alternative<CpuCandidatePoolPreparationError>(result));
  EXPECT_EQ(std::get<CpuCandidatePoolPreparationError>(result).invariant_id,
            "allocator.cpu_candidate_pool.base_footprint.v1");
}

TEST(CpuCandidatePoolPreparationTest, EnforcesPersistentWorkerCountBoundaries) {
  for (const std::uint32_t accepted : {1U, kMaximumPersistentCpuCandidateWorkersV1}) {
    PersistentCpuCandidatePoolPreparerResult result =
        CreatePersistentCpuCandidatePoolPreparer({.worker_count = accepted});
    ASSERT_TRUE(
        std::holds_alternative<std::unique_ptr<PersistentCpuCandidatePoolPreparer>>(result));
    EXPECT_EQ(std::get<std::unique_ptr<PersistentCpuCandidatePoolPreparer>>(result)
                  ->config()
                  .worker_count,
              accepted);
  }
  for (const std::uint32_t rejected : {0U, kMaximumPersistentCpuCandidateWorkersV1 + 1U}) {
    const PersistentCpuCandidatePoolPreparerResult result =
        CreatePersistentCpuCandidatePoolPreparer({.worker_count = rejected});
    ASSERT_TRUE(std::holds_alternative<CpuCandidatePoolPreparationError>(result));
    EXPECT_EQ(std::get<CpuCandidatePoolPreparationError>(result).invariant_id,
              "allocator.cpu_candidate_pool.worker_count.v1");
  }
}

TEST(CpuCandidatePoolPreparationTest, ReplayEncodingHasVersionOneGoldens) {
  const CpuCandidatePoolPreparationConfig config{
      .schema_version = 1,
      .requested_candidates_per_net = 4,
      .deterministic_seed = 5,
      .step_surcharge_increment = 7,
      .bend_surcharge_increment = 11,
      .resource_penalty_increment = 13,
      .route_limits =
          {
              .maximum_work_units = 17,
              .maximum_record_count = 19,
              .maximum_queue_size = 23,
              .maximum_reconstruction_states = 29,
          },
      .limits =
          {
              .maximum_nets = 31,
              .maximum_route_queries = 37,
              .maximum_total_route_work_units = 41,
              .maximum_concurrent_route_records = 43,
              .maximum_concurrent_queue_entries = 47,
              .maximum_concurrent_reconstruction_states = 53,
              .maximum_policy_resource_entries = 59,
              .maximum_retained_candidate_bytes = 61,
              .maximum_candidate_draft_bytes = 67,
              .maximum_generated_candidate_bytes = 71,
          },
      .store_config =
          {
              .maximum_candidates_per_net = 73,
              .maximum_candidate_bytes_per_net = 79,
              .maximum_rejection_records = 83,
              .maximum_rejection_items_per_transaction = 89,
              .maximum_admission_items_per_transaction = 97,
              .maximum_admission_input_bytes_per_transaction = 101,
              .maximum_admission_work_units_per_transaction = 103,
              .maximum_pin_lease_items_per_transaction = 107,
              .maximum_expected_pools_per_invocation = 109,
              .maximum_expected_candidates_per_invocation = 113,
          },
  };
  const std::uint64_t batch = internal::ComputeCpuCandidatePoolBatchIdentityV1(127, 131, config);
  const CpuCandidatePoolPreparationCounters counters{
      .requested_columns = 137,
      .executed_route_queries = 139,
      .successful_routes = 149,
      .disconnected_proofs = 151,
      .unsupported_proofs = 157,
      .skipped_columns = 163,
      .built_candidates = 167,
      .admitted_candidates = 173,
      .duplicate_candidates = 179,
      .rejected_columns = 181,
      .retained_candidates = 191,
  };
  const std::array columns = {
      CpuCandidatePoolColumnRecord{
          .net = {.id = 193, .generation = 197},
          .candidate_ordinal = 199,
          .policy_identity = 211,
          .batch_identity = 223,
          .query_identity = 227,
          .outcome = CpuCandidatePoolColumnOutcome::kAdmitted,
          .candidate_id = candidates::CandidateId{.high = 229, .low = 233},
          .candidate_payload_checksum = 239,
          .rejection_code = std::nullopt,
      },
      CpuCandidatePoolColumnRecord{
          .net = {.id = 241, .generation = 251},
          .candidate_ordinal = 257,
          .policy_identity = 263,
          .batch_identity = 269,
          .query_identity = 271,
          .outcome = CpuCandidatePoolColumnOutcome::kRouteDisconnected,
          .candidate_id = std::nullopt,
          .candidate_payload_checksum = std::nullopt,
          .rejection_code = candidates::CandidateRejectionCode::kBackendFailure,
      },
  };
  const std::array first_candidates = {
      internal::CpuCandidatePoolChecksumCandidateV1{
          .id = {.high = 277, .low = 281},
          .payload_checksum = 283,
      },
  };
  const std::array second_candidates = {
      internal::CpuCandidatePoolChecksumCandidateV1{
          .id = {.high = 293, .low = 307},
          .payload_checksum = 311,
      },
      internal::CpuCandidatePoolChecksumCandidateV1{
          .id = {.high = 313, .low = 317},
          .payload_checksum = 331,
      },
  };
  const std::array pools = {
      internal::CpuCandidatePoolChecksumPoolV1{
          .net = {.id = 337, .generation = 347},
          .candidates = first_candidates,
      },
      internal::CpuCandidatePoolChecksumPoolV1{
          .net = {.id = 349, .generation = 353},
          .candidates = second_candidates,
      },
  };
  const std::uint64_t checksum = internal::ComputeCpuCandidatePoolPreparationChecksumV1(
      config, batch, counters, columns, pools);
  const std::uint64_t net_seed = internal::ComputeCpuCandidatePoolNetSeedV1(
      373, 379, board_ir::EntityRef{.id = 383, .generation = 389});

  EXPECT_EQ(batch, 5'572'961'570'777'735'953ULL);
  EXPECT_EQ(checksum, 15'531'823'249'808'920'296ULL);
  EXPECT_EQ(net_seed, 4'652'715'695'970'407'240ULL);
}

TEST(CpuCandidatePoolPreparationTest, TinyStoreByteCapProducesExplicitAtomicRejections) {
  const Fixture fixture = MakeTwoNetFixture();
  CpuCandidatePoolPreparationConfig config = Config(fixture.workload.nets().size());
  config.store_config.maximum_candidate_bytes_per_net = 1;
  config.limits.maximum_retained_candidate_bytes = fixture.workload.nets().size();
  std::unique_ptr<PersistentCpuCandidatePoolPreparer> preparer = Preparer(2);

  const PreparedCpuCandidatePools result =
      Prepared(PrepareInitialCpuCandidatePools(*preparer, fixture.board, fixture.workload, config));

  EXPECT_EQ(result.counters().executed_route_queries, 8U);
  EXPECT_EQ(result.counters().retained_candidates, 0U);
  EXPECT_EQ(result.counters().admitted_candidates, 0U);
  EXPECT_GT(result.counters().rejected_columns, 0U);
}

TEST(CpuCandidatePoolPreparationTest, SupportsAllFrozenPhaseFourPoolSizes) {
  const Fixture fixture = MakeTwoNetFixture();
  std::unique_ptr<PersistentCpuCandidatePoolPreparer> preparer = Preparer(4);
  for (const std::uint32_t pool_size : {4U, 8U, 16U}) {
    const CpuCandidatePoolPreparationConfig config =
        Config(fixture.workload.nets().size(), pool_size);
    const PreparedCpuCandidatePools result = Prepared(
        PrepareInitialCpuCandidatePools(*preparer, fixture.board, fixture.workload, config));
    EXPECT_EQ(result.columns().size(), fixture.workload.nets().size() * pool_size);
    EXPECT_EQ(result.counters().executed_route_queries, result.columns().size());
    EXPECT_EQ(result.pools().size(), fixture.workload.nets().size());
  }
}

}  // namespace
}  // namespace apgar::allocator
