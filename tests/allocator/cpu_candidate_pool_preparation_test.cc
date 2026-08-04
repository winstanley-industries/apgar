#include "apgar/allocator/cpu_candidate_pool_preparation.h"

#include <algorithm>
#include <array>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <map>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

#include "apgar/allocator/resource_accounting.h"
#include "apgar/board_ir/board.h"
#include "apgar/candidates/route_candidate.h"
#include "apgar/geometry_compiler/compiled_board.h"
#include "apgar/routing/candidate_policy.h"
#include "apgar/routing/cpu_astar.h"
#include "src/allocator/cpu_candidate_pool_preparation_internal.h"
#include "tests/support/google_test.h"
#include "tests/support/routing_builder.h"

namespace apgar::allocator {
namespace {

using board_ir::AxisAlignedBox64;
using board_ir::BoardData;
using board_ir::EntityRef;
using board_ir::Layer;
using board_ir::LayerType;
using board_ir::Net;
using board_ir::Point64;
using board_ir::RoutingProfile;
using board_ir::Terminal;
using geometry_compiler::ActiveRegion;
using geometry_compiler::CompiledBoard;
using geometry_compiler::CompilerProfile;
using geometry_compiler::DeterministicCosts;
using geometry_compiler::Direction;
using routing::EdgeResourceKey;

inline constexpr board_ir::LayerId kFront = 0;
inline constexpr board_ir::LayerId kBack = 1;
inline constexpr std::array<EntityRef, 3> kNets = {
    EntityRef{.id = 10, .generation = 0},
    EntityRef{.id = 11, .generation = 0},
    EntityRef{.id = 12, .generation = 0},
};

[[nodiscard]] AxisAlignedBox64 TerminalBox(Point64 center) {
  return AxisAlignedBox64{
      .min = Point64{.x = center.x - 2, .y = center.y - 2},
      .max = Point64{.x = center.x + 2, .y = center.y + 2},
  };
}

[[nodiscard]] Terminal MakeTerminal(std::uint64_t id, EntityRef net, Point64 center,
                                    std::string pin) {
  return Terminal{
      .ref = EntityRef{.id = id, .generation = 0},
      .net = net,
      .component = "POOL",
      .pin = std::move(pin),
      .center = center,
      .connection_region = TerminalBox(center),
      .layers = {kFront, kBack},
  };
}

[[nodiscard]] BoardData PoolBoardData(std::uint64_t revision = 1) {
  const std::array<EntityRef, 6> terminals = {
      EntityRef{.id = 20, .generation = 0}, EntityRef{.id = 21, .generation = 0},
      EntityRef{.id = 22, .generation = 0}, EntityRef{.id = 23, .generation = 0},
      EntityRef{.id = 24, .generation = 0}, EntityRef{.id = 25, .generation = 0},
  };
  return BoardData{
      .schema_version = board_ir::kBoardSchemaVersion,
      .dbu_per_millimeter = 1'000'000,
      .revision = revision,
      .adapter_name = "p4r04-pool-microcase",
      .adapter_version = "1",
      .layers =
          {
              Layer{
                  .ref = EntityRef{.id = 1, .generation = 0},
                  .routing_id = kFront,
                  .name = "front",
                  .physical_order = 0,
                  .type = LayerType::kSignal,
                  .routable = true,
              },
              Layer{
                  .ref = EntityRef{.id = 2, .generation = 0},
                  .routing_id = kBack,
                  .name = "back",
                  .physical_order = 1,
                  .type = LayerType::kSignal,
                  .routable = true,
              },
          },
      .nets =
          {
              Net{.ref = kNets[0],
                  .name = "ALTERNATIVES",
                  .terminals = {terminals[0], terminals[1]}},
              Net{.ref = kNets[1], .name = "CORRIDOR", .terminals = {terminals[2], terminals[3]}},
              Net{.ref = kNets[2],
                  .name = "DISCONNECTED",
                  .terminals = {terminals[4], terminals[5]}},
          },
      .terminals =
          {
              MakeTerminal(20, kNets[0], Point64{.x = 0, .y = 0}, "A1"),
              MakeTerminal(21, kNets[0], Point64{.x = 40, .y = 0}, "A2"),
              MakeTerminal(22, kNets[1], Point64{.x = 0, .y = 40}, "B1"),
              MakeTerminal(23, kNets[1], Point64{.x = 40, .y = 40}, "B2"),
              MakeTerminal(24, kNets[2], Point64{.x = 0, .y = 60}, "C1"),
              MakeTerminal(25, kNets[2], Point64{.x = 40, .y = 60}, "C2"),
          },
      .obstacles = {},
      .routing_profile =
          RoutingProfile{
              .net = kNets[0],
              .nominal_width = 4,
              .clearance = 1,
              .allowed_layers = {kFront, kBack},
              .allowed_headings = board_ir::kM1HeadingMask,
          },
  };
}

[[nodiscard]] CompilerProfile PoolCompilerProfile() {
  return CompilerProfile{
      .schema_version = geometry_compiler::kCompilerProfileSchemaVersion,
      .lattice_origin = Point64{.x = 0, .y = 0},
      .lattice_step = 10,
      .tile_width_nodes = 4,
      .tile_height_nodes = 4,
      .compilation_roi =
          AxisAlignedBox64{.min = Point64{.x = 0, .y = 0}, .max = Point64{.x = 40, .y = 60}},
      .active_regions =
          {
              ActiveRegion{.layer = kFront,
                           .bounds = AxisAlignedBox64{.min = Point64{.x = 0, .y = 0},
                                                      .max = Point64{.x = 40, .y = 20}}},
              ActiveRegion{.layer = kFront,
                           .bounds = AxisAlignedBox64{.min = Point64{.x = 0, .y = 40},
                                                      .max = Point64{.x = 40, .y = 40}}},
              ActiveRegion{.layer = kFront,
                           .bounds = AxisAlignedBox64{.min = Point64{.x = 0, .y = 60},
                                                      .max = Point64{.x = 0, .y = 60}}},
              ActiveRegion{.layer = kFront,
                           .bounds = AxisAlignedBox64{.min = Point64{.x = 40, .y = 60},
                                                      .max = Point64{.x = 40, .y = 60}}},
          },
      .heading_mask = board_ir::kM1HeadingMask,
      .costs = DeterministicCosts{.orthogonal_step = 10, .diagonal_step = 14, .bend = 3},
  };
}

struct PoolContext {
  board_ir::BoardSnapshot board;
  std::vector<CompiledBoard> compiled;
};

[[nodiscard]] PoolContext MakePoolContext() {
  PoolContext context{.board = test_support::Snapshot(PoolBoardData()), .compiled = {}};
  context.compiled.reserve(kNets.size());
  for (EntityRef net : kNets) {
    context.compiled.push_back(
        test_support::CompilePreparedNet(context.board, net, PoolCompilerProfile()));
  }
  return context;
}

[[nodiscard]] EdgeResourceKey EastResource(std::int64_t lattice_y, std::int64_t lattice_x = 0) {
  return EdgeResourceKey{
      .layer = kFront,
      .lattice_x = lattice_x,
      .lattice_y = lattice_y,
      .direction = Direction::kEast,
  };
}

[[nodiscard]] std::vector<CpuCandidatePoolNetSchedule> Schedules(
    const PoolContext& context, std::array<std::uint32_t, 3> counts = {5, 2, 1}) {
  std::vector<CpuCandidatePoolNetSchedule> schedules;
  schedules.reserve(kNets.size());
  for (std::size_t index = 0; index < kNets.size(); ++index) {
    routing::PlanarRouteRequest request =
        test_support::RequestForNet(context.board, kNets[index], kFront, kFront);
    request.candidate_policy.deterministic_seed = 700 + index;
    std::vector<EdgeResourceKey> resources;
    if (counts[index] > 1) {
      resources = index == 0 ? std::vector{EastResource(0), EastResource(0, 1)}
                             : std::vector{EastResource(4), EastResource(4, 1)};
    }
    schedules.push_back(CpuCandidatePoolNetSchedule{
        .compiled_board = &context.compiled[index],
        .request = std::move(request),
        .candidate_schedule =
            routing::DeterministicAlternativePolicySchedule{
                .candidate_count = counts[index],
                .step_surcharge_increment = 1,
                .bend_surcharge_increment = 1,
                .resource_penalty_increment = 1'000,
                .alternative_resources = std::move(resources),
            },
    });
  }
  return schedules;
}

[[nodiscard]] const PreparedCpuCandidatePools& Prepared(
    const CpuCandidatePoolPreparationResult& result) {
  EXPECT_TRUE(std::holds_alternative<PreparedCpuCandidatePools>(result));
  if (!std::holds_alternative<PreparedCpuCandidatePools>(result)) {
    std::abort();
  }
  return std::get<PreparedCpuCandidatePools>(result);
}

[[nodiscard]] CpuCandidatePoolPreparationError Failure(
    const CpuCandidatePoolPreparationResult& result) {
  EXPECT_TRUE(std::holds_alternative<CpuCandidatePoolPreparationError>(result));
  if (!std::holds_alternative<CpuCandidatePoolPreparationError>(result)) {
    std::abort();
  }
  return std::get<CpuCandidatePoolPreparationError>(result);
}

struct VisiblePool {
  EntityRef net;
  std::vector<candidates::CandidateId> candidates;

  friend bool operator==(const VisiblePool&, const VisiblePool&) = default;
};

struct VisiblePreparation {
  std::vector<VisiblePool> pools;
  std::vector<CpuCandidatePoolColumn> columns;
  std::vector<CpuCandidatePoolDiagnostic> diagnostics;
  std::uint64_t batch_identity = 0;
  std::uint64_t query_count = 0;
  std::uint64_t cpu_work_units = 0;
  std::uint64_t generated_bytes = 0;
  std::uint64_t retained_bytes = 0;

  friend bool operator==(const VisiblePreparation&, const VisiblePreparation&) = default;
};

[[nodiscard]] VisiblePreparation Visible(const PreparedCpuCandidatePools& prepared) {
  VisiblePreparation visible{
      .pools = {},
      .columns = {},
      .diagnostics = {},
      .batch_identity = prepared.batch_identity(),
      .query_count = prepared.query_count(),
      .cpu_work_units = prepared.cpu_work_units(),
      .generated_bytes = prepared.generated_bytes(),
      .retained_bytes = prepared.retained_candidate_bytes(),
  };
  for (const PreparedCpuCandidatePool& pool : prepared.pools()) {
    VisiblePool item{.net = pool.net(), .candidates = {}};
    for (const candidates::StoredCandidate& candidate : pool.candidates()) {
      item.candidates.push_back(candidate->id());
    }
    visible.pools.push_back(std::move(item));
  }
  visible.columns.assign(prepared.columns().begin(), prepared.columns().end());
  visible.diagnostics.assign(prepared.diagnostics().begin(), prepared.diagnostics().end());
  return visible;
}

struct EntityRefLess {
  [[nodiscard]] bool operator()(EntityRef left, EntityRef right) const noexcept {
    return std::pair{left.id, left.generation} < std::pair{right.id, right.generation};
  }
};

[[nodiscard]] std::map<EntityRef, candidates::GeneratedRouteCandidate, EntityRefLess>
SequentialReference(const PoolContext& context,
                    std::span<const CpuCandidatePoolNetSchedule> schedules) {
  std::map<EntityRef, candidates::GeneratedRouteCandidate, EntityRefLess> accepted;
  std::uint64_t query_identity = 1;
  for (const CpuCandidatePoolNetSchedule& scheduled : schedules) {
    routing::CandidatePolicyResult normalized_result = routing::NormalizeCandidateGenerationPolicy(
        *scheduled.compiled_board, scheduled.request.candidate_policy);
    EXPECT_TRUE(
        std::holds_alternative<routing::NormalizedCandidateGenerationPolicy>(normalized_result));
    const auto& normalized =
        std::get<routing::NormalizedCandidateGenerationPolicy>(normalized_result);
    routing::CpuRouteResult route =
        routing::RouteWithCpuAStar(context.board, *scheduled.compiled_board, scheduled.request);
    if (std::holds_alternative<routing::RouteFailure>(route)) {
      EXPECT_EQ(std::get<routing::RouteFailure>(route).code,
                routing::RouteFailureCode::kDisconnected);
      ++query_identity;
      continue;
    }
    candidates::CandidateDraftBuildResult draft = candidates::BuildGeneratedCandidateFromCpuRoute(
        context.board, *scheduled.compiled_board, scheduled.request, normalized,
        std::get<routing::CpuRoute>(route),
        candidates::CandidateSchedulingIdentity{.batch_identity = 9,
                                                .query_identity = query_identity++});
    EXPECT_TRUE(std::holds_alternative<candidates::GeneratedRouteCandidate>(draft));
    if (!std::holds_alternative<candidates::GeneratedRouteCandidate>(draft)) {
      std::abort();
    }
    candidates::CandidateAdmissionResult admission = candidates::AdmitRouteCandidate(
        candidates::CandidateAdmissionContext{.board = context.board,
                                              .compiled_board = *scheduled.compiled_board,
                                              .request = scheduled.request},
        std::get<candidates::GeneratedRouteCandidate>(std::move(draft)));
    EXPECT_TRUE(std::holds_alternative<candidates::RouteCandidate>(admission));
    if (!std::holds_alternative<candidates::RouteCandidate>(admission)) {
      std::abort();
    }
    accepted.emplace(scheduled.request.net,
                     std::get<candidates::RouteCandidate>(std::move(admission)).data());
  }
  return accepted;
}

TEST(CpuCandidatePoolPreparationTest,
     MultipleNetsDuplicatesEmptyPoolsAndOneWorldConsumptionAreCanonical) {
  const PoolContext context = MakePoolContext();
  const std::vector<CpuCandidatePoolNetSchedule> schedules = Schedules(context);
  const CpuCandidatePoolPreparationResult result =
      PrepareCpuCandidatePools(context.board, schedules);
  const PreparedCpuCandidatePools& prepared = Prepared(result);

  ASSERT_EQ(prepared.pools().size(), 3U);
  EXPECT_EQ(prepared.query_count(), 8U);
  EXPECT_EQ(prepared.pools()[0].net(), kNets[0]);
  EXPECT_GE(prepared.pools()[0].candidates().size(), 2U);
  EXPECT_EQ(prepared.pools()[1].net(), kNets[1]);
  EXPECT_FALSE(prepared.pools()[1].candidates().empty());
  EXPECT_EQ(prepared.pools()[2].net(), kNets[2]);
  EXPECT_TRUE(prepared.pools()[2].candidates().empty());
  EXPECT_TRUE(std::ranges::any_of(prepared.columns(), [](const CpuCandidatePoolColumn& column) {
    return column.outcome == CpuCandidateColumnOutcomeCode::kDisconnectedRoute;
  }));
  EXPECT_TRUE(std::ranges::any_of(prepared.columns(), [](const CpuCandidatePoolColumn& column) {
    return column.outcome == CpuCandidateColumnOutcomeCode::kAdmissionRejected;
  }));

  std::vector<OneWorldCandidatePool> one_world_pools;
  for (const PreparedCpuCandidatePool& pool : prepared.pools()) {
    one_world_pools.push_back(pool.one_world_pool());
  }
  ResourceCapacityModelResult capacity =
      BuildResourceCapacityModel(context.board, context.compiled.front(), 1);
  ASSERT_TRUE(std::holds_alternative<ResourceCapacityModel>(capacity));
  OneWorldSelectionResult selection = SelectOneWorldZeroPrice(
      context.board, std::get<ResourceCapacityModel>(capacity), one_world_pools);
  ASSERT_TRUE(std::holds_alternative<OneWorldSelection>(selection));
  EXPECT_EQ(std::get<OneWorldSelection>(selection).nets.size(), 3U);
  EXPECT_TRUE(std::holds_alternative<OneWorldCandidateAbsence>(
      std::get<OneWorldSelection>(selection).nets.back()));
}

TEST(CpuCandidatePoolPreparationTest,
     SequentialExactSmallReferenceComposesRouteBuildAndAdmissionWithoutPreparer) {
  const PoolContext context = MakePoolContext();
  const std::vector<CpuCandidatePoolNetSchedule> schedules = Schedules(context, {1, 1, 1});
  const auto reference = SequentialReference(context, schedules);
  const CpuCandidatePoolPreparationResult result =
      PrepareCpuCandidatePools(context.board, schedules);
  const PreparedCpuCandidatePools& prepared = Prepared(result);

  ASSERT_EQ(prepared.pools().size(), 3U);
  for (const PreparedCpuCandidatePool& pool : prepared.pools()) {
    const auto expected = reference.find(pool.net());
    if (expected == reference.end()) {
      EXPECT_TRUE(pool.candidates().empty());
      continue;
    }
    ASSERT_EQ(pool.candidates().size(), 1U);
    const candidates::GeneratedRouteCandidate& actual = pool.candidates().front()->data();
    EXPECT_EQ(actual.geometry, expected->second.geometry);
    EXPECT_EQ(actual.resources, expected->second.resources);
    EXPECT_EQ(actual.metrics, expected->second.metrics);
    EXPECT_EQ(actual.constraints, expected->second.constraints);
  }
}

struct ReverseCompletion {
  std::mutex mutex;
  std::condition_variable ready;
  std::size_t arrived = 0;
  std::size_t total = 0;
  std::size_t next = 0;
  std::vector<std::size_t> order;
};

void CompleteInReverse(std::size_t index, void* opaque) noexcept {
  auto& state = *static_cast<ReverseCompletion*>(opaque);
  std::unique_lock lock(state.mutex);
  ++state.arrived;
  state.ready.notify_all();
  state.ready.wait(lock,
                   [&state, index] { return state.arrived == state.total && index == state.next; });
  state.order.push_back(index);
  if (state.next > 0) {
    --state.next;
  }
  state.ready.notify_all();
}

TEST(CpuCandidatePoolPreparationTest,
     NetPolicyInputWorkerAndDeliberatelyReversedCompletionOrderAreInvariant) {
  const PoolContext context = MakePoolContext();
  std::vector<CpuCandidatePoolNetSchedule> schedules = Schedules(context, {2, 1, 1});
  const VisiblePreparation serial =
      Visible(Prepared(PrepareCpuCandidatePools(context.board, schedules)));

  std::ranges::reverse(schedules);
  std::ranges::reverse(schedules.back().candidate_schedule.alternative_resources);
  schedules.back().candidate_schedule.alternative_resources.push_back(
      schedules.back().candidate_schedule.alternative_resources.front());
  CpuCandidatePoolPreparationConfig parallel;
  parallel.worker_count = 4;
  const VisiblePreparation permuted =
      Visible(Prepared(PrepareCpuCandidatePools(context.board, schedules, parallel)));
  EXPECT_EQ(permuted, serial);

  std::vector<CpuCandidatePoolNetSchedule> two_queries = Schedules(context, {2, 0, 0});
  two_queries.resize(1);
  CpuCandidatePoolPreparationConfig two_workers;
  two_workers.worker_count = 2;
  two_workers.limits.maximum_nets = 1;
  two_workers.limits.maximum_candidates_per_net = 2;
  two_workers.limits.maximum_total_queries = 2;
  ReverseCompletion reverse;
  reverse.total = 2;
  reverse.next = 1;
  reverse.order.reserve(2);
  const CpuCandidatePoolPreparationResult reversed =
      internal::PrepareCpuCandidatePoolsWithTestHooks(
          context.board, two_queries, two_workers,
          internal::CpuCandidatePoolPreparationTestHooks{
              .after_cpu_route = nullptr,
              .before_worker_completion = CompleteInReverse,
              .injected_worker_failure = nullptr,
              .context = &reverse,
          });
  const VisiblePreparation ordinary =
      Visible(Prepared(PrepareCpuCandidatePools(context.board, two_queries, two_workers)));
  EXPECT_EQ(Visible(Prepared(reversed)), ordinary);
  EXPECT_EQ(reverse.order, (std::vector<std::size_t>{1, 0}));

  for (int repeat = 0; repeat < 4; ++repeat) {
    EXPECT_EQ(Visible(Prepared(PrepareCpuCandidatePools(context.board, schedules, parallel))),
              serial);
  }

  PoolContext relocated{.board = context.board, .compiled = context.compiled};
  const std::vector<CpuCandidatePoolNetSchedule> relocated_schedules =
      Schedules(relocated, {2, 1, 1});
  EXPECT_EQ(Visible(Prepared(PrepareCpuCandidatePools(relocated.board, relocated_schedules))),
            serial);
}

void RemoveCpuEvidence(std::size_t index, routing::CpuRoute& route, void*) noexcept {
  if (index == 0) {
    route.producer_evidence.evidence.reset();
  }
}

TEST(CpuCandidatePoolPreparationTest,
     CandidateBuildRejectionIsAnOrdinaryStoredDiagnosticAndLeavesAnEmptyPool) {
  const PoolContext context = MakePoolContext();
  std::vector<CpuCandidatePoolNetSchedule> schedules = Schedules(context, {1, 1, 1});
  schedules.resize(1);
  CpuCandidatePoolPreparationConfig config;
  config.limits.maximum_nets = 1;
  config.limits.maximum_candidates_per_net = 1;
  config.limits.maximum_total_queries = 1;
  const CpuCandidatePoolPreparationResult result = internal::PrepareCpuCandidatePoolsWithTestHooks(
      context.board, schedules, config,
      internal::CpuCandidatePoolPreparationTestHooks{
          .after_cpu_route = RemoveCpuEvidence,
          .before_worker_completion = nullptr,
          .injected_worker_failure = nullptr,
          .context = nullptr,
      });
  const PreparedCpuCandidatePools& prepared = Prepared(result);
  ASSERT_EQ(prepared.pools().size(), 1U);
  EXPECT_TRUE(prepared.pools().front().candidates().empty());
  ASSERT_EQ(prepared.columns().size(), 1U);
  EXPECT_EQ(prepared.columns().front().outcome,
            CpuCandidateColumnOutcomeCode::kCandidateBuildRejected);
  ASSERT_EQ(prepared.diagnostics().size(), 1U);
  EXPECT_EQ(prepared.diagnostics().front().rejection.invariant_id,
            "candidate.builder.cpu_producer_authentication.v1");
}

TEST(CpuCandidatePoolPreparationTest,
     InvalidAndMismatchedBoardCompilerAndRequestAssociationsFailBeforeWorkers) {
  const PoolContext context = MakePoolContext();
  std::vector<CpuCandidatePoolNetSchedule> schedules = Schedules(context, {1, 1, 1});
  CpuCandidatePoolPreparationConfig invalid;
  invalid.worker_count = 0;
  EXPECT_EQ(Failure(PrepareCpuCandidatePools(context.board, schedules, invalid)).code,
            CpuCandidatePoolPreparationErrorCode::kInvalidConfiguration);

  std::vector<CpuCandidatePoolNetSchedule> empty_schedule = schedules;
  empty_schedule.front().candidate_schedule.candidate_count = 0;
  EXPECT_EQ(Failure(PrepareCpuCandidatePools(context.board, empty_schedule)).code,
            CpuCandidatePoolPreparationErrorCode::kInvalidConfiguration);

  std::vector<CpuCandidatePoolNetSchedule> duplicate = schedules;
  duplicate.push_back(duplicate.front());
  EXPECT_EQ(Failure(PrepareCpuCandidatePools(context.board, duplicate)).code,
            CpuCandidatePoolPreparationErrorCode::kInvalidInput);

  const board_ir::BoardSnapshot changed = test_support::Snapshot(PoolBoardData(2));
  EXPECT_EQ(Failure(PrepareCpuCandidatePools(changed, schedules)).code,
            CpuCandidatePoolPreparationErrorCode::kAssociationMismatch);

  CompilerProfile other_profile = PoolCompilerProfile();
  ++other_profile.costs.bend;
  CompiledBoard other =
      test_support::CompilePreparedNet(context.board, kNets[1], std::move(other_profile));
  schedules[1].compiled_board = &other;
  EXPECT_EQ(Failure(PrepareCpuCandidatePools(context.board, schedules)).code,
            CpuCandidatePoolPreparationErrorCode::kAssociationMismatch);

  schedules.resize(1);
  schedules.front().request = test_support::RequestForNet(context.board, kNets[1], kFront, kFront);
  EXPECT_EQ(Failure(PrepareCpuCandidatePools(context.board, schedules)).code,
            CpuCandidatePoolPreparationErrorCode::kAssociationMismatch);
}

TEST(CpuCandidatePoolPreparationTest,
     UnsupportedCrossLayerRouteIsTypedAndDoesNotMakePreparationFatal) {
  const PoolContext context = MakePoolContext();
  std::vector<CpuCandidatePoolNetSchedule> schedules = Schedules(context, {1, 1, 1});
  schedules.resize(1);
  schedules.front().request.goal_layer = kBack;
  const CpuCandidatePoolPreparationResult result =
      PrepareCpuCandidatePools(context.board, schedules);
  const PreparedCpuCandidatePools& prepared = Prepared(result);
  ASSERT_EQ(prepared.columns().size(), 1U);
  EXPECT_EQ(prepared.columns().front().outcome, CpuCandidateColumnOutcomeCode::kUnsupportedRoute);
  EXPECT_EQ(prepared.columns().front().route_failure_code,
            routing::RouteFailureCode::kUnsupportedLayerTransition);
  EXPECT_TRUE(prepared.pools().front().candidates().empty());
}

TEST(CpuCandidatePoolPreparationTest,
     EqualityPassesAndOneUnderFailsForWorkGeneratedRetainedAndStoreBounds) {
  const PoolContext context = MakePoolContext();
  std::vector<CpuCandidatePoolNetSchedule> schedules = Schedules(context, {1, 1, 1});
  schedules.resize(1);
  CpuCandidatePoolPreparationConfig baseline_config;
  baseline_config.limits.maximum_nets = 1;
  baseline_config.limits.maximum_candidates_per_net = 1;
  baseline_config.limits.maximum_total_queries = 1;
  const CpuCandidatePoolPreparationResult baseline_result =
      PrepareCpuCandidatePools(context.board, schedules, baseline_config);
  const PreparedCpuCandidatePools& baseline = Prepared(baseline_result);
  ASSERT_GT(baseline.cpu_work_units(), 1U);
  ASSERT_GT(baseline.generated_bytes(), 1U);

  CpuCandidatePoolPreparationConfig exact = baseline_config;
  exact.limits.maximum_cpu_work_units_per_query = baseline.cpu_work_units();
  exact.limits.maximum_aggregate_cpu_work_units = baseline.cpu_work_units();
  exact.limits.maximum_generated_bytes_per_query = baseline.generated_bytes();
  exact.limits.maximum_aggregate_generated_bytes = baseline.generated_bytes();
  exact.limits.maximum_retained_candidate_bytes =
      exact.limits.candidate_store.maximum_candidate_bytes_per_net;
  exact.limits.candidate_store.maximum_admission_items_per_transaction = 1;
  exact.limits.candidate_store.maximum_rejection_items_per_transaction = 1;
  EXPECT_TRUE(std::holds_alternative<PreparedCpuCandidatePools>(
      PrepareCpuCandidatePools(context.board, schedules, exact)));

  CpuCandidatePoolPreparationConfig one_under_work = exact;
  --one_under_work.limits.maximum_cpu_work_units_per_query;
  --one_under_work.limits.maximum_aggregate_cpu_work_units;
  EXPECT_EQ(Failure(PrepareCpuCandidatePools(context.board, schedules, one_under_work)).code,
            CpuCandidatePoolPreparationErrorCode::kBoundExhausted);

  CpuCandidatePoolPreparationConfig one_under_generated = exact;
  --one_under_generated.limits.maximum_generated_bytes_per_query;
  --one_under_generated.limits.maximum_aggregate_generated_bytes;
  EXPECT_EQ(Failure(PrepareCpuCandidatePools(context.board, schedules, one_under_generated)).code,
            CpuCandidatePoolPreparationErrorCode::kBoundExhausted);

  CpuCandidatePoolPreparationConfig one_under_retained = exact;
  --one_under_retained.limits.maximum_retained_candidate_bytes;
  EXPECT_EQ(Failure(PrepareCpuCandidatePools(context.board, schedules, one_under_retained)).code,
            CpuCandidatePoolPreparationErrorCode::kBoundExhausted);

  CpuCandidatePoolPreparationConfig store_input_exhausted = baseline_config;
  store_input_exhausted.limits.candidate_store.maximum_admission_input_bytes_per_transaction = 1;
  EXPECT_EQ(Failure(PrepareCpuCandidatePools(context.board, schedules, store_input_exhausted)).code,
            CpuCandidatePoolPreparationErrorCode::kBoundExhausted);

  schedules.front().candidate_schedule.candidate_count = 2;
  schedules.front().candidate_schedule.alternative_resources = {EastResource(0)};
  CpuCandidatePoolPreparationConfig exact_candidate_count;
  exact_candidate_count.limits.maximum_nets = 1;
  exact_candidate_count.limits.maximum_candidates_per_net = 2;
  exact_candidate_count.limits.maximum_total_queries = 2;
  EXPECT_TRUE(std::holds_alternative<PreparedCpuCandidatePools>(
      PrepareCpuCandidatePools(context.board, schedules, exact_candidate_count)));

  CpuCandidatePoolPreparationConfig one_under_candidate_count = exact_candidate_count;
  --one_under_candidate_count.limits.maximum_candidates_per_net;
  EXPECT_EQ(
      Failure(PrepareCpuCandidatePools(context.board, schedules, one_under_candidate_count)).code,
      CpuCandidatePoolPreparationErrorCode::kBoundExhausted);

  CpuCandidatePoolPreparationConfig store_one_under;
  store_one_under.limits.maximum_nets = 1;
  store_one_under.limits.maximum_candidates_per_net = 2;
  store_one_under.limits.maximum_total_queries = 2;
  store_one_under.limits.candidate_store.maximum_admission_items_per_transaction = 1;
  EXPECT_EQ(Failure(PrepareCpuCandidatePools(context.board, schedules, store_one_under)).code,
            CpuCandidatePoolPreparationErrorCode::kBoundExhausted);
}

TEST(CpuCandidatePoolPreparationTest, AggregateOverflowIsTypedBeforeExecution) {
  const PoolContext context = MakePoolContext();
  std::vector<CpuCandidatePoolNetSchedule> schedules = Schedules(context, {2, 1, 1});
  schedules.resize(1);
  CpuCandidatePoolPreparationConfig config;
  config.limits.maximum_nets = 1;
  config.limits.maximum_candidates_per_net = 2;
  config.limits.maximum_total_queries = 2;
  config.limits.maximum_cpu_work_units_per_query = std::numeric_limits<std::uint64_t>::max();
  config.limits.maximum_aggregate_cpu_work_units = std::numeric_limits<std::uint64_t>::max();
  const CpuCandidatePoolPreparationError error =
      Failure(PrepareCpuCandidatePools(context.board, schedules, config));
  EXPECT_EQ(error.code, CpuCandidatePoolPreparationErrorCode::kArithmeticOverflow);
  EXPECT_EQ(error.invariant_id, "allocator.cpu_pool.aggregate_work_overflow.v1");
}

internal::CpuCandidatePoolInjectedWorkerFailure InjectFailure(std::size_t, void* opaque) noexcept {
  return *static_cast<internal::CpuCandidatePoolInjectedWorkerFailure*>(opaque);
}

TEST(CpuCandidatePoolPreparationTest, WorkerResourceAndInternalFailuresAreTypedAndPublishNoPools) {
  const PoolContext context = MakePoolContext();
  std::vector<CpuCandidatePoolNetSchedule> schedules = Schedules(context, {1, 1, 1});
  schedules.resize(1);
  CpuCandidatePoolPreparationConfig config;
  config.limits.maximum_nets = 1;
  config.limits.maximum_candidates_per_net = 1;
  config.limits.maximum_total_queries = 1;
  for (const auto [injected, expected] :
       {std::pair{internal::CpuCandidatePoolInjectedWorkerFailure::kResourceExhausted,
                  CpuCandidatePoolPreparationErrorCode::kResourceExhausted},
        std::pair{internal::CpuCandidatePoolInjectedWorkerFailure::kInternalInvariant,
                  CpuCandidatePoolPreparationErrorCode::kInternalInvariant}}) {
    internal::CpuCandidatePoolInjectedWorkerFailure mutable_injected = injected;
    const CpuCandidatePoolPreparationResult result =
        internal::PrepareCpuCandidatePoolsWithTestHooks(
            context.board, schedules, config,
            internal::CpuCandidatePoolPreparationTestHooks{
                .after_cpu_route = nullptr,
                .before_worker_completion = nullptr,
                .injected_worker_failure = InjectFailure,
                .context = &mutable_injected,
            });
    EXPECT_EQ(Failure(result).code, expected);
  }
}

}  // namespace
}  // namespace apgar::allocator
