#include "apgar/allocator/cpu_candidate_allocation_session.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <ranges>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "src/allocator/cpu_candidate_allocation_session_internal.h"
#include "src/allocator/multi_world_internal.h"
#include "src/allocator/targeted_regeneration_execution_internal.h"
#include "tests/support/board_builder.h"
#include "tests/support/compiler_builder.h"
#include "tests/support/google_test.h"

namespace apgar::allocator {
namespace {

static_assert(std::is_move_constructible_v<CpuCandidateAllocationSession>);
static_assert(!std::is_move_assignable_v<CpuCandidateAllocationSession>);

struct Fixture {
  board_ir::BoardSnapshot board;
  MultiNetWorkload workload;
  ResourceCapacityModel capacities;
  PreparedCpuCandidatePools prepared;
};

template <typename Value, typename Error>
[[nodiscard]] Value Built(std::variant<Value, Error> result) {
  if constexpr (requires(const Error& error) { error.detail; }) {
    EXPECT_TRUE(std::holds_alternative<Value>(result))
        << (std::holds_alternative<Error>(result) ? std::get<Error>(result).detail : "");
  } else {
    EXPECT_TRUE(std::holds_alternative<Value>(result));
  }
  if (!std::holds_alternative<Value>(result)) {
    std::abort();
  }
  return std::get<Value>(std::move(result));
}

[[nodiscard]] board_ir::BoardSnapshot Snapshot(board_ir::BoardData data) {
  return Built<board_ir::BoardSnapshot>(board_ir::CreateBoardSnapshot(std::move(data)));
}

[[nodiscard]] std::unique_ptr<PersistentCpuCandidatePoolPreparer> Preparer(
    std::uint32_t worker_count) {
  return Built<std::unique_ptr<PersistentCpuCandidatePoolPreparer>>(
      CreatePersistentCpuCandidatePoolPreparer({.worker_count = worker_count}));
}

[[nodiscard]] CpuCandidatePoolPreparationConfig PreparationConfig(
    std::uint32_t requested_candidates_per_net = 4, std::uint64_t maximum_candidates_per_net = 8,
    std::uint64_t maximum_admission_input_bytes = 1024ULL * 1024ULL * 1024ULL,
    std::uint64_t maximum_admission_work_units = 100'000'000'000ULL,
    std::uint64_t maximum_rejection_records = 128) {
  CpuCandidatePoolPreparationConfig config;
  config.requested_candidates_per_net = requested_candidates_per_net;
  config.deterministic_seed = 0x1234'5678'9abc'def0ULL;
  config.store_config = candidates::CandidateStoreConfig{
      .maximum_candidates_per_net = maximum_candidates_per_net,
      .maximum_candidate_bytes_per_net = 64U * 1024U * 1024U,
      .maximum_rejection_records = maximum_rejection_records,
      .maximum_rejection_items_per_transaction = 32,
      .maximum_admission_items_per_transaction = 32,
      .maximum_admission_input_bytes_per_transaction = maximum_admission_input_bytes,
      .maximum_admission_work_units_per_transaction = maximum_admission_work_units,
      .maximum_pin_lease_items_per_transaction = 32,
      .maximum_expected_pools_per_invocation = 2,
      .maximum_expected_candidates_per_invocation =
          std::max<std::uint64_t>(16, 2U * maximum_candidates_per_net),
  };
  return config;
}

[[nodiscard]] Fixture MakeFixture(std::uint32_t worker_count, std::uint32_t default_capacity_units,
                                  std::uint32_t requested_candidates_per_net = 4,
                                  std::uint64_t maximum_candidates_per_net = 8,
                                  bool single_path_per_net = false,
                                  bool overlapping_net_endpoints = false,
                                  bool keep_obstacle = false,
                                  std::uint64_t maximum_admission_input_bytes = 1024ULL * 1024ULL *
                                                                                1024ULL,
                                  std::uint64_t maximum_admission_work_units = 100'000'000'000ULL,
                                  std::uint64_t maximum_rejection_records = 128) {
  board_ir::BoardData data = test_support::ValidM1TwoNetBoardData();
  if (!keep_obstacle) {
    data.obstacles.clear();
  } else {
    data.obstacles.front().bounds.max.y = 0;
  }
  if (single_path_per_net) {
    data.terminals[0].center = {.x = 0, .y = 0};
    data.terminals[0].connection_region = {.min = {.x = 0, .y = 0}, .max = {.x = 0, .y = 0}};
    data.terminals[1].center = {.x = 10, .y = 0};
    data.terminals[1].connection_region = {.min = {.x = 10, .y = 0}, .max = {.x = 10, .y = 0}};
    data.terminals[2].center = {.x = 0, .y = 20};
    data.terminals[2].connection_region = {.min = {.x = 0, .y = 20}, .max = {.x = 0, .y = 20}};
    data.terminals[3].center = {.x = 10, .y = 20};
    data.terminals[3].connection_region = {.min = {.x = 10, .y = 20}, .max = {.x = 10, .y = 20}};
  } else if (overlapping_net_endpoints) {
    data.terminals[2].center = data.terminals[0].center;
    data.terminals[2].connection_region = data.terminals[0].connection_region;
    data.terminals[3].center = data.terminals[1].center;
    data.terminals[3].connection_region = data.terminals[1].connection_region;
  }
  board_ir::BoardSnapshot board = Snapshot(std::move(data));
  const geometry_compiler::CompilerProfile compiler_profile =
      test_support::DefaultCompilerProfile({0});
  board_ir::RoutingProfile second_profile = board.data().routing_profile;
  second_profile.net = board.data().nets[1].ref;
  const std::array specs = {
      MultiNetRoutingSpec{
          .routing_profile = board.data().routing_profile, .start_layer = 0, .goal_layer = 0},
      MultiNetRoutingSpec{.routing_profile = second_profile, .start_layer = 0, .goal_layer = 0},
  };
  MultiNetWorkload workload = Built<MultiNetWorkload>(BuildMultiNetWorkload(
      kMultiNetWorkloadSchemaVersion, board, compiler_profile, specs, specs.size()));
  ResourceCapacityModel capacities = Built<ResourceCapacityModel>(BuildResourceCapacityModel(
      kResourceCapacityModelSchemaVersion, board, workload.nets().front().compiled_board,
      default_capacity_units, {}));
  std::unique_ptr<PersistentCpuCandidatePoolPreparer> preparer = Preparer(worker_count);
  PreparedCpuCandidatePools prepared =
      Built<PreparedCpuCandidatePools>(PrepareInitialCpuCandidatePools(
          *preparer, board, workload,
          PreparationConfig(requested_candidates_per_net, maximum_candidates_per_net,
                            maximum_admission_input_bytes, maximum_admission_work_units,
                            maximum_rejection_records)));
  return Fixture{.board = std::move(board),
                 .workload = std::move(workload),
                 .capacities = std::move(capacities),
                 .prepared = std::move(prepared)};
}

[[nodiscard]] CpuCandidateAllocationSessionConfig SessionConfig() {
  CpuCandidateAllocationSessionConfig config;
  config.maximum_regeneration_epochs = 2;
  config.price_config.maximum_iterations = 4;
  config.regeneration_plan_config.maximum_target_nets = 2;
  config.regeneration_plan_config.maximum_columns_per_net = 1;
  config.regeneration_plan_config.maximum_total_columns = 2;
  config.regeneration_plan_config.maximum_resource_actions_per_net = 4;
  config.regeneration_plan_config.maximum_total_resource_actions = 8;
  config.schedules = {
      MultiWorldSchedule{
          .schedule_key = 2, .search_intrinsic_cost_weight = 2, .maximum_selection_rounds = 2},
      MultiWorldSchedule{
          .schedule_key = 1, .search_intrinsic_cost_weight = 1, .maximum_selection_rounds = 1},
  };
  return config;
}

[[nodiscard]] OneWorldAllocation AllocateFixtureWorld(
    const Fixture& fixture, const CpuCandidateAllocationSessionConfig& config) {
  NegotiatedPriceState state = Built<NegotiatedPriceState>(
      BuildInitialNegotiatedPriceState(kNegotiatedPriceStateSchemaVersion, fixture.capacities,
                                       fixture.workload, config.price_config));
  PriceSnapshot prices =
      Built<PriceSnapshot>(BuildPriceSnapshotForState(fixture.capacities, state));
  const OneWorldAllocationRequest request{
      .schema_version = kOneWorldAllocationSchemaVersion,
      .associations = fixture.capacities.associations(),
      .capacities = fixture.capacities,
      .prices = std::move(prices),
      .intrinsic_cost_weight = config.intrinsic_cost_weight,
      .limits = config.allocator_limits,
      .pools = fixture.prepared.pools(),
      .workload = &fixture.workload,
  };
  return Built<OneWorldAllocation>(AllocateOneWorld(request));
}

void BlockInitiallySelectedResources(Fixture& fixture,
                                     const CpuCandidateAllocationSessionConfig& config) {
  const OneWorldAllocation initial = AllocateFixtureWorld(fixture, config);
  const auto expand = [](const candidates::StoredCandidate& candidate) {
    std::vector<routing::EdgeResourceKey> expanded;
    for (const candidates::PhysicalEdgeSpan& span : candidate->data().resources) {
      const geometry_compiler::DirectionDelta delta =
          candidates::ResourceSpanStorageDelta(span.direction);
      for (std::uint32_t offset = 0; offset < span.edge_count; ++offset) {
        expanded.push_back(routing::EdgeResourceKey{
            .layer = span.layer,
            .lattice_x = span.lattice_x + static_cast<std::int64_t>(delta.x) * offset,
            .lattice_y = span.lattice_y + static_cast<std::int64_t>(delta.y) * offset,
            .direction = span.direction,
        });
      }
    }
    std::ranges::sort(expanded);
    expanded.erase(std::ranges::unique(expanded).begin(), expanded.end());
    return expanded;
  };
  ASSERT_EQ(initial.selections.size(), 2U);
  std::array<std::vector<routing::EdgeResourceKey>, 2> selected_resources;
  std::array<std::vector<std::vector<routing::EdgeResourceKey>>, 2> candidate_resources;
  for (std::size_t net_index = 0; net_index < initial.selections.size(); ++net_index) {
    const NetSelection& selection = initial.selections[net_index];
    ASSERT_NE(selection.candidate, nullptr);
    const auto pool =
        std::ranges::find(fixture.prepared.pools(), selection.net, &CandidatePool::net);
    ASSERT_NE(pool, fixture.prepared.pools().end());
    selected_resources[net_index] = expand(selection.candidate);
    for (const candidates::StoredCandidate& candidate : pool->candidates) {
      candidate_resources[net_index].push_back(expand(candidate));
    }
  }
  std::optional<routing::EdgeResourceKey> blocker;
  for (std::size_t net_index = 0; net_index < selected_resources.size() && !blocker.has_value();
       ++net_index) {
    for (const routing::EdgeResourceKey& resource : selected_resources[net_index]) {
      const bool this_net_has_alternative = std::ranges::any_of(
          candidate_resources[net_index],
          [&](const auto& route) { return !std::ranges::binary_search(route, resource); });
      bool every_other_net_has_route = true;
      for (std::size_t other = 0; other < candidate_resources.size(); ++other) {
        if (other == net_index) {
          continue;
        }
        every_other_net_has_route =
            every_other_net_has_route &&
            std::ranges::any_of(candidate_resources[other], [&](const auto& route) {
              return !std::ranges::binary_search(route, resource);
            });
      }
      if (this_net_has_alternative && every_other_net_has_route) {
        blocker = resource;
        break;
      }
    }
  }
  ASSERT_TRUE(blocker.has_value());
  std::vector<routing::EdgeResourceKey> resources{*blocker};
  std::ranges::sort(resources);
  resources.erase(std::ranges::unique(resources).begin(), resources.end());
  ASSERT_FALSE(resources.empty());
  std::vector<ResourceCapacityOverride> overrides;
  overrides.reserve(resources.size());
  for (const routing::EdgeResourceKey& resource : resources) {
    overrides.push_back(ResourceCapacityOverride{.resource = resource, .capacity_units = 0});
  }
  fixture.capacities = Built<ResourceCapacityModel>(BuildResourceCapacityModel(
      kResourceCapacityModelSchemaVersion, fixture.board,
      fixture.workload.nets().front().compiled_board, 1, std::move(overrides)));
}

[[nodiscard]] std::uint64_t ExpandedResourceUses(const PreparedCpuCandidatePools& prepared) {
  std::uint64_t count = 0;
  for (const CandidatePool& pool : prepared.pools()) {
    for (const candidates::StoredCandidate& candidate : pool.candidates) {
      for (const candidates::PhysicalEdgeSpan& span : candidate->data().resources) {
        count += span.edge_count;
      }
    }
  }
  return count;
}

[[nodiscard]] CpuCandidateAllocationSession Executed(
    Fixture fixture, const CpuCandidateAllocationSessionConfig& config) {
  CpuCandidateAllocationSessionResult result = ExecuteCpuCandidateAllocationSession(
      kCpuCandidateAllocationSessionSchemaVersion, std::move(fixture.board),
      std::move(fixture.workload), std::move(fixture.capacities), std::move(fixture.prepared),
      config);
  EXPECT_TRUE(std::holds_alternative<CpuCandidateAllocationSession>(result))
      << (std::holds_alternative<CpuCandidateAllocationSessionError>(result)
              ? std::string(std::get<CpuCandidateAllocationSessionError>(result).invariant_id)
              : std::string{});
  if (!std::holds_alternative<CpuCandidateAllocationSession>(result)) {
    std::abort();
  }
  return std::get<CpuCandidateAllocationSession>(std::move(result));
}

void ExpectStoresEqual(const candidates::CandidateStore& left,
                       const candidates::CandidateStore& right, const MultiNetWorkload& workload) {
  for (const PreparedNetRoutingContext& context : workload.nets()) {
    const std::vector<candidates::StoredCandidate> left_pool = left.Enumerate(context.request.net);
    const std::vector<candidates::StoredCandidate> right_pool =
        right.Enumerate(context.request.net);
    ASSERT_EQ(left_pool.size(), right_pool.size());
    for (std::size_t index = 0; index < left_pool.size(); ++index) {
      ASSERT_NE(left_pool[index], nullptr);
      ASSERT_NE(right_pool[index], nullptr);
      EXPECT_EQ(*left_pool[index], *right_pool[index]);
    }
  }
  EXPECT_EQ(left.Rejections(), right.Rejections());
}

struct CandidateStoreState {
  std::vector<std::pair<board_ir::EntityRef, std::vector<candidates::StoredCandidate>>> pools;
  std::vector<candidates::CandidateRejection> rejections;
  candidates::CandidateStoreTelemetry telemetry;
};

[[nodiscard]] CandidateStoreState CaptureStoreState(const candidates::CandidateStore& store,
                                                    const MultiNetWorkload& workload) {
  CandidateStoreState state;
  state.pools.reserve(workload.nets().size());
  for (const PreparedNetRoutingContext& context : workload.nets()) {
    state.pools.emplace_back(context.request.net, store.Enumerate(context.request.net));
  }
  state.rejections = store.Rejections();
  state.telemetry = store.telemetry();
  return state;
}

void ExpectStoreState(const candidates::CandidateStore& store,
                      const CandidateStoreState& expected) {
  for (const auto& [net, expected_pool] : expected.pools) {
    const std::vector<candidates::StoredCandidate> actual = store.Enumerate(net);
    ASSERT_EQ(actual.size(), expected_pool.size());
    for (std::size_t index = 0; index < actual.size(); ++index) {
      ASSERT_NE(actual[index], nullptr);
      ASSERT_NE(expected_pool[index], nullptr);
      EXPECT_EQ(*actual[index], *expected_pool[index]);
    }
  }
  EXPECT_EQ(store.Rejections(), expected.rejections);
  EXPECT_EQ(store.telemetry(), expected.telemetry);
}

[[nodiscard]] candidates::CandidateRejection PrimeRejection() {
  return candidates::CandidateRejection{
      .schema_version = 2,
      .candidate_id = candidates::CandidateId{.high = 3, .low = 5},
      .net = board_ir::EntityRef{.id = 7, .generation = 11},
      .stage = candidates::CandidateLifecycleStage::kStored,
      .code = candidates::CandidateRejectionCode::kBackendFailure,
      .invariant_id = "prime.invariant.13",
      .associations =
          candidates::CandidateAssociations{
              .board_content_hash = 17,
              .compiler_profile_fingerprint = 19,
              .geometry_compiler_version = 23,
              .routing_profile_fingerprint = 29,
              .rule_bucket_identity = 31,
          },
      .policy_identity = 37,
      .provenance =
          candidates::CandidateProvenance{
              .generator = candidates::CandidateGeneratorKind::kCudaSweep,
              .generator_version = 41,
              .backend = candidates::CandidateBackendKind::kCuda,
              .supported_device_class = "prime-device-43",
              .deterministic_seed = 47,
              .batch_identity = 53,
              .query_identity = 59,
              .candidate_ordinal = 61,
          },
      .primitive_witness_index = 67,
      .resource_witness_index = 71,
      .expected_value = 73,
      .actual_value = 79,
      .conflicting_entity = board_ir::EntityRef{.id = 83, .generation = 89},
      .candidate_payload_checksum = 97,
      .detail = "prime detail 101",
      .logical_bytes = 103,
  };
}

[[nodiscard]] CpuCandidateAllocationEpochRecord PrimeEpoch() {
  const candidates::CandidateRejection rejection = PrimeRejection();
  return CpuCandidateAllocationEpochRecord{
      .epoch_index = 107,
      .source_pool_manifest_checksum = 109,
      .successor_pool_manifest_checksum = 113,
      .source_world_checksum = 127,
      .successor_world_checksum = 131,
      .successor_price_state_checksum = 137,
      .plan_checksum = 139,
      .execution_checksum = 149,
      .disposition = TargetedRegenerationExecutionDisposition::kStalled,
      .terminal_reason = TargetedRegenerationTerminalReason::kNoNovelRetainedColumns,
      .counters =
          TargetedRegenerationExecutionCounters{
              .requested_columns = 151,
              .route_queries = 157,
              .route_work_units = 163,
              .policy_projection_visits = 167,
              .peak_route_record_count = 173,
              .peak_route_queue_size = 179,
              .generated_candidate_bytes = 181,
              .rejection_record_bytes = 191,
              .transient_result_bytes = 193,
              .successful_routes = 197,
              .built_candidates = 199,
              .admitted_candidates = 211,
              .duplicate_candidates = 223,
              .rejected_columns = 227,
              .novel_retained_candidates = 229,
              .changed_selections = 233,
              .successor_pinned_candidates = 239,
          },
      .columns = {TargetedRegenerationColumnRecord{
          .net = board_ir::EntityRef{.id = 241, .generation = 251},
          .column_index = 257,
          .policy_identity = 263,
          .batch_identity = 269,
          .query_identity = 271,
          .route_telemetry =
              routing::CpuRouteTelemetry{
                  .queue_pops = 277,
                  .expanded_states = 281,
                  .attempted_relaxations = 283,
                  .accepted_relaxations = 293,
                  .peak_record_count = 307,
                  .peak_queue_size = 311,
                  .work_units = 313,
              },
          .candidate_draft_logical_bytes = 317,
          .outcome = TargetedRegenerationColumnOutcome::kAdmissionRejected,
          .candidate_id = candidates::CandidateId{.high = 331, .low = 337},
          .candidate_payload_checksum = 347,
          .rejection_code = candidates::CandidateRejectionCode::kBackendFailure,
          .rejection = rejection,
      }},
      .pool_manifest_changed = true,
      .selected_route_roster_changed = false,
      .complete_price_values_changed = true,
      .fixed_point = false,
  };
}

[[nodiscard]] CpuCandidateAllocationSessionConfig PrimeSessionConfig() {
  CpuCandidateAllocationSessionConfig config = SessionConfig();
  std::ranges::sort(config.schedules, {}, &MultiWorldSchedule::schedule_key);
  return config;
}

[[nodiscard]] CpuCandidateAllocationSessionCounters PrimeSessionCounters() {
  return CpuCandidateAllocationSessionCounters{
      .completed_regeneration_epochs = 353,
      .planning_expanded_resource_visits = 359,
      .requested_columns = 367,
      .route_queries = 373,
      .route_work_units = 379,
      .policy_projection_visits = 383,
      .generated_candidate_bytes = 389,
      .rejection_record_bytes = 397,
      .transient_result_bytes = 401,
      .admitted_candidates = 409,
      .duplicate_candidates = 419,
      .rejected_columns = 421,
      .novel_retained_candidates = 431,
      .changed_selections = 433,
  };
}

[[nodiscard]] std::uint64_t PrimeRejectionManifest() {
  const std::array rejections = {PrimeRejection()};
  return internal::ComputeCpuCandidateAllocationRejectionManifestChecksumV1(rejections);
}

[[nodiscard]] std::uint64_t PrimeSessionChecksum(
    const CpuCandidateAllocationSessionConfig& config,
    const CpuCandidateAllocationSessionCounters& counters,
    std::span<const CpuCandidateAllocationEpochRecord> epochs,
    CpuCandidateAllocationTerminalReason terminal_reason =
        CpuCandidateAllocationTerminalReason::kFixedPoint,
    std::uint64_t board_content_hash = 439, std::uint64_t workload_checksum = 443,
    std::uint64_t capacity_model_checksum = 449, std::uint64_t preparation_checksum = 457,
    std::uint64_t final_pool_manifest_checksum = 461,
    std::uint64_t final_rejection_manifest_checksum = PrimeRejectionManifest(),
    std::uint64_t final_price_state_checksum = 463, std::uint64_t final_single_world_checksum = 467,
    std::uint64_t final_multi_world_checksum = 479) {
  return internal::ComputeCpuCandidateAllocationSessionChecksumV1(
      config, board_content_hash, workload_checksum, capacity_model_checksum, preparation_checksum,
      terminal_reason, counters, epochs, final_pool_manifest_checksum,
      final_rejection_manifest_checksum, final_price_state_checksum, final_single_world_checksum,
      final_multi_world_checksum);
}

TEST(CpuCandidateAllocationSessionTest, OwnsCompleteFeasibleSessionAndTerminalLease) {
  CpuCandidateAllocationSession session = Executed(MakeFixture(2, 1), SessionConfig());

  EXPECT_EQ(session.terminal_reason(), CpuCandidateAllocationTerminalReason::kFeasible);
  EXPECT_TRUE(session.epochs().empty());
  EXPECT_EQ(session.final_pools().size(), session.workload().nets().size());
  EXPECT_TRUE(session.final_multi_world().has_active_retention_lease());
  EXPECT_TRUE(session.final_multi_world().retention_lease_belongs_to(
      session.preparation().candidate_store()));
  EXPECT_NE(session.final_pool_manifest_checksum(), 0U);
  EXPECT_NE(session.session_checksum(), 0U);

  CpuCandidateAllocationSession moved = std::move(session);
  EXPECT_TRUE(moved.final_multi_world().has_active_retention_lease());
  EXPECT_TRUE(
      moved.final_multi_world().retention_lease_belongs_to(moved.preparation().candidate_store()));
}

TEST(CpuCandidateAllocationSessionTest, PersistentPreparationWorkerCountIsNonSemantic) {
  CpuCandidateAllocationSession serial = Executed(MakeFixture(1, 1), SessionConfig());
  CpuCandidateAllocationSession parallel = Executed(MakeFixture(4, 1), SessionConfig());

  EXPECT_EQ(serial.terminal_reason(), parallel.terminal_reason());
  EXPECT_EQ(serial.counters(), parallel.counters());
  EXPECT_EQ(serial.epochs(), parallel.epochs());
  EXPECT_EQ(serial.final_pool_manifest_checksum(), parallel.final_pool_manifest_checksum());
  EXPECT_EQ(serial.final_rejection_manifest_checksum(),
            parallel.final_rejection_manifest_checksum());
  EXPECT_EQ(serial.final_price_state(), parallel.final_price_state());
  EXPECT_EQ(serial.final_single_world().world_checksum,
            parallel.final_single_world().world_checksum);
  EXPECT_EQ(serial.final_multi_world().execution_checksum(),
            parallel.final_multi_world().execution_checksum());
  EXPECT_EQ(serial.session_checksum(), parallel.session_checksum());
}

TEST(CpuCandidateAllocationSessionTest, RegenerationEvidenceIsIndependentOfPreparationWorkerCount) {
  CpuCandidateAllocationSession serial = Executed(MakeFixture(1, 0), SessionConfig());
  CpuCandidateAllocationSession parallel = Executed(MakeFixture(4, 0), SessionConfig());

  ASSERT_FALSE(serial.epochs().empty());
  EXPECT_EQ(serial.terminal_reason(), parallel.terminal_reason());
  EXPECT_EQ(serial.counters(), parallel.counters());
  EXPECT_EQ(serial.epochs(), parallel.epochs());
  EXPECT_EQ(serial.final_pool_manifest_checksum(), parallel.final_pool_manifest_checksum());
  EXPECT_EQ(serial.final_rejection_manifest_checksum(),
            parallel.final_rejection_manifest_checksum());
  EXPECT_EQ(serial.final_price_state(), parallel.final_price_state());
  EXPECT_EQ(serial.final_single_world().world_checksum,
            parallel.final_single_world().world_checksum);
  EXPECT_EQ(serial.final_multi_world().execution_checksum(),
            parallel.final_multi_world().execution_checksum());
  EXPECT_EQ(serial.session_checksum(), parallel.session_checksum());
  ExpectStoresEqual(serial.preparation().candidate_store(),
                    parallel.preparation().candidate_store(), serial.workload());
}

TEST(CpuCandidateAllocationSessionTest, ScheduleInputOrderIsNonSemantic) {
  CpuCandidateAllocationSessionConfig first_config = SessionConfig();
  CpuCandidateAllocationSessionConfig second_config = SessionConfig();
  std::ranges::reverse(second_config.schedules);
  CpuCandidateAllocationSession first = Executed(MakeFixture(1, 1), first_config);
  CpuCandidateAllocationSession second = Executed(MakeFixture(1, 1), second_config);

  EXPECT_EQ(first.config(), second.config());
  EXPECT_TRUE(
      std::ranges::is_sorted(first.config().schedules, {}, &MultiWorldSchedule::schedule_key));
  EXPECT_EQ(first.final_multi_world().execution_checksum(),
            second.final_multi_world().execution_checksum());
  EXPECT_EQ(first.session_checksum(), second.session_checksum());
}

TEST(CpuCandidateAllocationSessionTest,
     ParetoOverBudgetRosterRejectsBeforeCanonicalCopyOrSourceInspection) {
  Fixture fixture = MakeFixture(1, 1);
  CpuCandidateAllocationSessionConfig config = SessionConfig();
  constexpr std::uint64_t kScheduleCount = 20'000;
  config.schedules.clear();
  config.schedules.reserve(kScheduleCount);
  for (std::uint64_t index = 0; index < kScheduleCount; ++index) {
    config.schedules.push_back(MultiWorldSchedule{
        .schedule_key = index + 1U,
        .search_intrinsic_cost_weight = index + 1U,
        .maximum_selection_rounds = 1,
    });
  }
  config.multi_world_config.maximum_worlds = kScheduleCount;
  config.multi_world_config.maximum_total_selection_rounds = kScheduleCount;
  config.multi_world_config.maximum_pareto_comparisons = kMaximumMultiWorldParetoComparisonsV1;
  CpuCandidateAllocationSessionResult result = ExecuteCpuCandidateAllocationSession(
      kCpuCandidateAllocationSessionSchemaVersion, std::move(fixture.board),
      std::move(fixture.workload), std::move(fixture.capacities), std::move(fixture.prepared),
      config);

  ASSERT_TRUE(std::holds_alternative<CpuCandidateAllocationSessionError>(result));
  const CpuCandidateAllocationSessionError& error =
      std::get<CpuCandidateAllocationSessionError>(result);
  EXPECT_EQ(error.code, CpuCandidateAllocationSessionErrorCode::kWorkBoundExceeded);
  EXPECT_EQ(error.invariant_id, "allocator.cpu_candidate_session.schedule_work_bound.v1");
  EXPECT_EQ(internal::CpuCandidateAllocationSourceSpanInspectionsForTesting(), 0U);
}

TEST(CpuCandidateAllocationSessionTest, PoolComparisonUsesTypedValuesAndDetectsManifestMismatch) {
  Fixture fixture = MakeFixture(1, 1);
  std::vector<CandidatePool> same = fixture.prepared.pools();
  constexpr std::uint64_t kManifest = 17;
  EXPECT_EQ(internal::CompareCpuCandidatePoolSemanticsV1(fixture.prepared.pools(), same, kManifest,
                                                         kManifest),
            internal::CpuCandidatePoolComparisonV1::kUnchanged);
  EXPECT_EQ(
      internal::CompareCpuCandidatePoolSemanticsV1(fixture.prepared.pools(), same, kManifest, 19),
      internal::CpuCandidatePoolComparisonV1::kManifestCollisionOrDrift);

  ASSERT_FALSE(same.front().candidates.empty());
  same.front().candidates.pop_back();
  EXPECT_EQ(internal::CompareCpuCandidatePoolSemanticsV1(fixture.prepared.pools(), same, kManifest,
                                                         kManifest),
            internal::CpuCandidatePoolComparisonV1::kManifestCollisionOrDrift);
  EXPECT_EQ(
      internal::CompareCpuCandidatePoolSemanticsV1(fixture.prepared.pools(), same, kManifest, 19),
      internal::CpuCandidatePoolComparisonV1::kChanged);
}

TEST(CpuCandidateAllocationSessionTest, FixedPointRequiresAllThreeSemanticPredicates) {
  using Comparison = internal::CpuCandidatePoolComparisonV1;
  EXPECT_TRUE(internal::CpuCandidateAllocationFixedPointV1(Comparison::kUnchanged, true, true));
  EXPECT_FALSE(internal::CpuCandidateAllocationFixedPointV1(Comparison::kChanged, true, true));
  EXPECT_FALSE(internal::CpuCandidateAllocationFixedPointV1(Comparison::kUnchanged, false, true));
  EXPECT_FALSE(internal::CpuCandidateAllocationFixedPointV1(Comparison::kUnchanged, true, false));
}

TEST(CpuCandidateAllocationSessionTest, PreferredFeasibleWorldUpgradesTerminalReason) {
  EXPECT_EQ(internal::ResolveCpuCandidateAllocationTerminalReasonV1(
                CpuCandidateAllocationTerminalReason::kRegenerationEpochLimit, false, true),
            CpuCandidateAllocationTerminalReason::kFeasible);
  EXPECT_EQ(internal::ResolveCpuCandidateAllocationTerminalReasonV1(
                CpuCandidateAllocationTerminalReason::kFixedPoint, true, true),
            CpuCandidateAllocationTerminalReason::kResourceRefinementRequired);
}

TEST(CpuCandidateAllocationSessionTest, TerminalNonAnchorFeasibleWorldUpgradesRuntimeReason) {
  CpuCandidateAllocationSessionConfig config = SessionConfig();
  config.intrinsic_cost_weight = 1'000;
  config.maximum_regeneration_epochs = 1;
  config.price_config.present_step_per_overuse_unit = 1'000;
  config.price_config.history_step_per_overuse_unit = 1'000;
  config.price_config.maximum_iterations = 1;
  config.price_config.maximum_price_records = 10'000;
  config.allocator_limits.maximum_resource_records = 20'000;
  config.regeneration_plan_config.maximum_target_nets = 1;
  config.regeneration_plan_config.maximum_columns_per_net = 1;
  config.regeneration_plan_config.maximum_total_columns = 1;
  config.regeneration_plan_config.maximum_resource_actions_per_net = 4;
  config.regeneration_plan_config.maximum_total_resource_actions = 4;
  config.schedules = {
      MultiWorldSchedule{
          .schedule_key = 1, .search_intrinsic_cost_weight = 1'000, .maximum_selection_rounds = 1},
      MultiWorldSchedule{
          .schedule_key = 2, .search_intrinsic_cost_weight = 1, .maximum_selection_rounds = 1},
  };
  Fixture fixture = MakeFixture(1, 1, 16, 16, false, false, true);
  BlockInitiallySelectedResources(fixture, config);
  EXPECT_GT(AllocateFixtureWorld(fixture, config).total_overuse_units, 0U);
  internal::SetTargetedRegenerationRouteFailureForTesting(true);
  CpuCandidateAllocationSession session = Executed(std::move(fixture), config);

  EXPECT_GT(session.final_single_world().total_overuse_units, 0U);
  EXPECT_EQ(session.terminal_reason(), CpuCandidateAllocationTerminalReason::kFeasible);
  const auto anchor = std::ranges::find(
      session.final_multi_world().summaries(), 1U,
      [](const MultiWorldSummary& summary) { return summary.schedule.schedule_key; });
  const auto non_anchor = std::ranges::find(
      session.final_multi_world().summaries(), 2U,
      [](const MultiWorldSummary& summary) { return summary.schedule.schedule_key; });
  ASSERT_NE(anchor, session.final_multi_world().summaries().end());
  ASSERT_NE(non_anchor, session.final_multi_world().summaries().end());
  EXPECT_NE(anchor->terminal_reason, MultiWorldTerminalReason::kFeasible);
  EXPECT_EQ(non_anchor->terminal_reason, MultiWorldTerminalReason::kFeasible);
}

TEST(CpuCandidateAllocationSessionTest, StableSessionRepresentationIsGolden) {
  CpuCandidateAllocationSession session = Executed(MakeFixture(1, 1), SessionConfig());

  EXPECT_EQ(session.session_checksum(), 10365567761333396631ULL);
}

TEST(CpuCandidateAllocationSessionTest,
     PrimeEpochAndRejectionRepresentationsAreGoldenAndFieldSensitive) {
  CpuCandidateAllocationSessionConfig config = SessionConfig();
  std::ranges::sort(config.schedules, {}, &MultiWorldSchedule::schedule_key);
  const candidates::CandidateRejection rejection = PrimeRejection();
  const std::array rejections = {rejection};
  const std::uint64_t rejection_manifest =
      internal::ComputeCpuCandidateAllocationRejectionManifestChecksumV1(rejections);
  EXPECT_EQ(rejection_manifest, 15185300074143548732ULL);

  candidates::CandidateRejection changed_rejection = rejection;
  changed_rejection.detail.push_back('!');
  const std::array changed_rejections = {changed_rejection};
  EXPECT_NE(internal::ComputeCpuCandidateAllocationRejectionManifestChecksumV1(changed_rejections),
            rejection_manifest);

  const CpuCandidateAllocationSessionCounters counters{
      .completed_regeneration_epochs = 353,
      .planning_expanded_resource_visits = 359,
      .requested_columns = 367,
      .route_queries = 373,
      .route_work_units = 379,
      .policy_projection_visits = 383,
      .generated_candidate_bytes = 389,
      .rejection_record_bytes = 397,
      .transient_result_bytes = 401,
      .admitted_candidates = 409,
      .duplicate_candidates = 419,
      .rejected_columns = 421,
      .novel_retained_candidates = 431,
      .changed_selections = 433,
  };
  const std::array epochs = {PrimeEpoch()};
  const std::uint64_t session_checksum = internal::ComputeCpuCandidateAllocationSessionChecksumV1(
      config, 439, 443, 449, 457, CpuCandidateAllocationTerminalReason::kFixedPoint, counters,
      epochs, 461, rejection_manifest, 463, 467, 479);
  EXPECT_EQ(session_checksum, 12912908603899075077ULL);

  std::array changed_epochs = epochs;
  ASSERT_TRUE(changed_epochs.front().columns.front().route_telemetry.has_value());
  ++changed_epochs.front().columns.front().route_telemetry->queue_pops;
  EXPECT_NE(internal::ComputeCpuCandidateAllocationSessionChecksumV1(
                config, 439, 443, 449, 457, CpuCandidateAllocationTerminalReason::kFixedPoint,
                counters, changed_epochs, 461, rejection_manifest, 463, 467, 479),
            session_checksum);
}

TEST(CpuCandidateAllocationSessionTest, SessionChecksumIsSensitiveToEveryConfigurationField) {
  const CpuCandidateAllocationSessionConfig config = PrimeSessionConfig();
  const CpuCandidateAllocationSessionCounters counters = PrimeSessionCounters();
  const std::array epochs = {PrimeEpoch()};
  const std::uint64_t baseline = PrimeSessionChecksum(config, counters, epochs);
  const auto expect_sensitive = [&](auto mutate) {
    CpuCandidateAllocationSessionConfig changed = config;
    mutate(changed);
    EXPECT_NE(PrimeSessionChecksum(changed, counters, epochs), baseline);
  };
  expect_sensitive([](auto& changed) { ++changed.schema_version; });
  expect_sensitive([](auto& changed) { ++changed.intrinsic_cost_weight; });
  expect_sensitive([](auto& changed) { ++changed.maximum_regeneration_epochs; });

  const auto expect_price = [&](auto member) {
    expect_sensitive([&](auto& changed) { ++(changed.price_config.*member); });
  };
  expect_price(&NegotiatedPriceConfig::present_step_per_overuse_unit);
  expect_price(&NegotiatedPriceConfig::history_step_per_overuse_unit);
  expect_price(&NegotiatedPriceConfig::maximum_price_per_resource);
  expect_price(&NegotiatedPriceConfig::maximum_iterations);
  expect_price(&NegotiatedPriceConfig::maximum_price_records);

  const auto expect_allocator = [&](auto member) {
    expect_sensitive([&](auto& changed) { ++(changed.allocator_limits.*member); });
  };
  expect_allocator(&OneWorldAllocatorLimits::maximum_nets);
  expect_allocator(&OneWorldAllocatorLimits::maximum_candidates);
  expect_allocator(&OneWorldAllocatorLimits::maximum_resource_records);
  expect_allocator(&OneWorldAllocatorLimits::maximum_expanded_resource_uses);

  const auto expect_plan = [&](auto member) {
    expect_sensitive([&](auto& changed) { ++(changed.regeneration_plan_config.*member); });
  };
  expect_plan(&TargetedRegenerationConfig::maximum_target_nets);
  expect_plan(&TargetedRegenerationConfig::maximum_columns_per_net);
  expect_plan(&TargetedRegenerationConfig::maximum_total_columns);
  expect_plan(&TargetedRegenerationConfig::maximum_resource_actions_per_net);
  expect_plan(&TargetedRegenerationConfig::maximum_total_resource_actions);
  expect_plan(&TargetedRegenerationConfig::maximum_expanded_resource_visits);

  const auto expect_execution = [&](auto member) {
    expect_sensitive([&](auto& changed) { ++(changed.regeneration_execution_config.*member); });
  };
  expect_execution(&TargetedRegenerationExecutionConfig::maximum_route_queries);
  expect_sensitive([](auto& changed) {
    ++changed.regeneration_execution_config.route_limits.maximum_work_units;
  });
  expect_sensitive([](auto& changed) {
    ++changed.regeneration_execution_config.route_limits.maximum_record_count;
  });
  expect_sensitive([](auto& changed) {
    ++changed.regeneration_execution_config.route_limits.maximum_queue_size;
  });
  expect_sensitive([](auto& changed) {
    ++changed.regeneration_execution_config.route_limits.maximum_reconstruction_states;
  });
  expect_execution(&TargetedRegenerationExecutionConfig::maximum_total_route_work_units);
  expect_execution(&TargetedRegenerationExecutionConfig::maximum_policy_projection_visits);
  expect_execution(&TargetedRegenerationExecutionConfig::maximum_policy_resource_entries);
  expect_execution(&TargetedRegenerationExecutionConfig::maximum_candidate_draft_bytes);
  expect_execution(&TargetedRegenerationExecutionConfig::maximum_generated_candidate_bytes);
  expect_execution(&TargetedRegenerationExecutionConfig::maximum_rejection_bytes);
  expect_execution(&TargetedRegenerationExecutionConfig::maximum_transient_result_bytes);
  expect_execution(&TargetedRegenerationExecutionConfig::known_unmapped_exact_conflict_count);

  expect_sensitive([](auto& changed) { ++changed.schedules.front().schedule_key; });
  expect_sensitive([](auto& changed) { ++changed.schedules.front().search_intrinsic_cost_weight; });
  expect_sensitive([](auto& changed) { ++changed.schedules.front().maximum_selection_rounds; });
  expect_sensitive([](auto& changed) {
    changed.schedules.push_back(MultiWorldSchedule{
        .schedule_key = 997,
        .search_intrinsic_cost_weight = 991,
        .maximum_selection_rounds = 983,
    });
  });

  const auto expect_worlds = [&](auto member) {
    expect_sensitive([&](auto& changed) { ++(changed.multi_world_config.*member); });
  };
  expect_worlds(&MultiWorldExecutionConfig::maximum_worlds);
  expect_worlds(&MultiWorldExecutionConfig::maximum_total_selection_rounds);
  expect_worlds(&MultiWorldExecutionConfig::maximum_total_candidate_evaluations);
  expect_worlds(&MultiWorldExecutionConfig::maximum_total_candidate_span_visits);
  expect_worlds(&MultiWorldExecutionConfig::maximum_total_resource_work_units);
  expect_worlds(&MultiWorldExecutionConfig::maximum_total_net_outcomes);
  expect_worlds(&MultiWorldExecutionConfig::maximum_pareto_comparisons);
  expect_worlds(&MultiWorldExecutionConfig::maximum_buffered_terminal_selection_records);
  expect_worlds(&MultiWorldExecutionConfig::maximum_buffered_terminal_resource_records);
  expect_worlds(&MultiWorldExecutionConfig::maximum_buffered_terminal_price_records);
  expect_worlds(&MultiWorldExecutionConfig::maximum_retained_worlds);
  expect_worlds(&MultiWorldExecutionConfig::maximum_retained_selection_records);
  expect_worlds(&MultiWorldExecutionConfig::maximum_retained_resource_records);
  expect_worlds(&MultiWorldExecutionConfig::maximum_retained_price_records);
  expect_worlds(&MultiWorldExecutionConfig::maximum_retained_winner_pins);
  expect_worlds(&MultiWorldExecutionConfig::maximum_near_feasible_missing_nets);
  expect_worlds(&MultiWorldExecutionConfig::maximum_near_feasible_overuse_units);
  expect_worlds(&MultiWorldExecutionConfig::known_unmapped_exact_conflict_count);

  const auto expect_limit = [&](auto member) {
    expect_sensitive([&](auto& changed) { ++(changed.limits.*member); });
  };
  expect_limit(&CpuCandidateAllocationSessionLimits::maximum_epoch_records);
  expect_limit(
      &CpuCandidateAllocationSessionLimits::maximum_total_planning_expanded_resource_visits);
  expect_limit(&CpuCandidateAllocationSessionLimits::maximum_total_requested_columns);
  expect_limit(&CpuCandidateAllocationSessionLimits::maximum_total_route_queries);
  expect_limit(&CpuCandidateAllocationSessionLimits::maximum_total_route_work_units);
  expect_limit(&CpuCandidateAllocationSessionLimits::maximum_total_policy_projection_visits);
  expect_limit(&CpuCandidateAllocationSessionLimits::maximum_total_generated_candidate_bytes);
  expect_limit(&CpuCandidateAllocationSessionLimits::maximum_total_rejection_bytes);
  expect_limit(&CpuCandidateAllocationSessionLimits::maximum_total_transient_result_bytes);
  expect_sensitive([](auto& changed) { ++changed.known_unmapped_exact_conflict_count; });
}

TEST(CpuCandidateAllocationSessionTest,
     SessionChecksumIsSensitiveToEveryIdentityCounterEpochAndColumnField) {
  const CpuCandidateAllocationSessionConfig config = PrimeSessionConfig();
  const CpuCandidateAllocationSessionCounters counters = PrimeSessionCounters();
  const std::array epochs = {PrimeEpoch()};
  const std::uint64_t baseline = PrimeSessionChecksum(config, counters, epochs);

  EXPECT_NE(PrimeSessionChecksum(config, counters, epochs,
                                 CpuCandidateAllocationTerminalReason::kFeasible),
            baseline);
  EXPECT_NE(PrimeSessionChecksum(config, counters, epochs,
                                 CpuCandidateAllocationTerminalReason::kFixedPoint, 440),
            baseline);
  EXPECT_NE(PrimeSessionChecksum(config, counters, epochs,
                                 CpuCandidateAllocationTerminalReason::kFixedPoint, 439, 444),
            baseline);
  EXPECT_NE(PrimeSessionChecksum(config, counters, epochs,
                                 CpuCandidateAllocationTerminalReason::kFixedPoint, 439, 443, 450),
            baseline);
  EXPECT_NE(
      PrimeSessionChecksum(config, counters, epochs,
                           CpuCandidateAllocationTerminalReason::kFixedPoint, 439, 443, 449, 458),
      baseline);
  EXPECT_NE(PrimeSessionChecksum(config, counters, epochs,
                                 CpuCandidateAllocationTerminalReason::kFixedPoint, 439, 443, 449,
                                 457, 462),
            baseline);
  EXPECT_NE(PrimeSessionChecksum(config, counters, epochs,
                                 CpuCandidateAllocationTerminalReason::kFixedPoint, 439, 443, 449,
                                 457, 461, PrimeRejectionManifest() + 1U),
            baseline);
  EXPECT_NE(PrimeSessionChecksum(config, counters, epochs,
                                 CpuCandidateAllocationTerminalReason::kFixedPoint, 439, 443, 449,
                                 457, 461, PrimeRejectionManifest(), 464),
            baseline);
  EXPECT_NE(PrimeSessionChecksum(config, counters, epochs,
                                 CpuCandidateAllocationTerminalReason::kFixedPoint, 439, 443, 449,
                                 457, 461, PrimeRejectionManifest(), 463, 468),
            baseline);
  EXPECT_NE(PrimeSessionChecksum(config, counters, epochs,
                                 CpuCandidateAllocationTerminalReason::kFixedPoint, 439, 443, 449,
                                 457, 461, PrimeRejectionManifest(), 463, 467, 480),
            baseline);

  const auto expect_counter = [&](auto member) {
    CpuCandidateAllocationSessionCounters changed = counters;
    ++(changed.*member);
    EXPECT_NE(PrimeSessionChecksum(config, changed, epochs), baseline);
  };
  expect_counter(&CpuCandidateAllocationSessionCounters::completed_regeneration_epochs);
  expect_counter(&CpuCandidateAllocationSessionCounters::planning_expanded_resource_visits);
  expect_counter(&CpuCandidateAllocationSessionCounters::requested_columns);
  expect_counter(&CpuCandidateAllocationSessionCounters::route_queries);
  expect_counter(&CpuCandidateAllocationSessionCounters::route_work_units);
  expect_counter(&CpuCandidateAllocationSessionCounters::policy_projection_visits);
  expect_counter(&CpuCandidateAllocationSessionCounters::generated_candidate_bytes);
  expect_counter(&CpuCandidateAllocationSessionCounters::rejection_record_bytes);
  expect_counter(&CpuCandidateAllocationSessionCounters::transient_result_bytes);
  expect_counter(&CpuCandidateAllocationSessionCounters::admitted_candidates);
  expect_counter(&CpuCandidateAllocationSessionCounters::duplicate_candidates);
  expect_counter(&CpuCandidateAllocationSessionCounters::rejected_columns);
  expect_counter(&CpuCandidateAllocationSessionCounters::novel_retained_candidates);
  expect_counter(&CpuCandidateAllocationSessionCounters::changed_selections);

  const auto expect_epoch_sensitive = [&](auto mutate) {
    std::array changed = epochs;
    mutate(changed.front());
    EXPECT_NE(PrimeSessionChecksum(config, counters, changed), baseline);
  };
  expect_epoch_sensitive([](auto& changed) { ++changed.epoch_index; });
  expect_epoch_sensitive([](auto& changed) { ++changed.source_pool_manifest_checksum; });
  expect_epoch_sensitive([](auto& changed) { ++changed.successor_pool_manifest_checksum; });
  expect_epoch_sensitive([](auto& changed) { ++changed.source_world_checksum; });
  expect_epoch_sensitive([](auto& changed) { ++changed.successor_world_checksum; });
  expect_epoch_sensitive([](auto& changed) { ++changed.successor_price_state_checksum; });
  expect_epoch_sensitive([](auto& changed) { ++changed.plan_checksum; });
  expect_epoch_sensitive([](auto& changed) { ++changed.execution_checksum; });
  expect_epoch_sensitive([](auto& changed) {
    changed.disposition = TargetedRegenerationExecutionDisposition::kProgress;
  });
  expect_epoch_sensitive([](auto& changed) {
    changed.terminal_reason = TargetedRegenerationTerminalReason::kNoTargets;
  });

  const auto expect_epoch_counter = [&](auto member) {
    expect_epoch_sensitive([&](auto& changed) { ++(changed.counters.*member); });
  };
  expect_epoch_counter(&TargetedRegenerationExecutionCounters::requested_columns);
  expect_epoch_counter(&TargetedRegenerationExecutionCounters::route_queries);
  expect_epoch_counter(&TargetedRegenerationExecutionCounters::route_work_units);
  expect_epoch_counter(&TargetedRegenerationExecutionCounters::policy_projection_visits);
  expect_epoch_counter(&TargetedRegenerationExecutionCounters::peak_route_record_count);
  expect_epoch_counter(&TargetedRegenerationExecutionCounters::peak_route_queue_size);
  expect_epoch_counter(&TargetedRegenerationExecutionCounters::generated_candidate_bytes);
  expect_epoch_counter(&TargetedRegenerationExecutionCounters::rejection_record_bytes);
  expect_epoch_counter(&TargetedRegenerationExecutionCounters::transient_result_bytes);
  expect_epoch_counter(&TargetedRegenerationExecutionCounters::successful_routes);
  expect_epoch_counter(&TargetedRegenerationExecutionCounters::built_candidates);
  expect_epoch_counter(&TargetedRegenerationExecutionCounters::admitted_candidates);
  expect_epoch_counter(&TargetedRegenerationExecutionCounters::duplicate_candidates);
  expect_epoch_counter(&TargetedRegenerationExecutionCounters::rejected_columns);
  expect_epoch_counter(&TargetedRegenerationExecutionCounters::novel_retained_candidates);
  expect_epoch_counter(&TargetedRegenerationExecutionCounters::changed_selections);
  expect_epoch_counter(&TargetedRegenerationExecutionCounters::successor_pinned_candidates);

  const auto expect_column = [&](auto mutate) {
    expect_epoch_sensitive([&](auto& changed) { mutate(changed.columns.front()); });
  };
  expect_column([](auto& changed) { ++changed.net.id; });
  expect_column([](auto& changed) { ++changed.net.generation; });
  expect_column([](auto& changed) { ++changed.column_index; });
  expect_column([](auto& changed) { ++changed.policy_identity; });
  expect_column([](auto& changed) { ++changed.batch_identity; });
  expect_column([](auto& changed) { ++changed.query_identity; });
  expect_column([](auto& changed) { changed.route_telemetry.reset(); });
  const auto expect_telemetry = [&](auto member) {
    expect_column([&](auto& changed) { ++(changed.route_telemetry.value().*member); });
  };
  expect_telemetry(&routing::CpuRouteTelemetry::queue_pops);
  expect_telemetry(&routing::CpuRouteTelemetry::expanded_states);
  expect_telemetry(&routing::CpuRouteTelemetry::attempted_relaxations);
  expect_telemetry(&routing::CpuRouteTelemetry::accepted_relaxations);
  expect_telemetry(&routing::CpuRouteTelemetry::peak_record_count);
  expect_telemetry(&routing::CpuRouteTelemetry::peak_queue_size);
  expect_telemetry(&routing::CpuRouteTelemetry::work_units);
  expect_column([](auto& changed) { ++changed.candidate_draft_logical_bytes; });
  expect_column(
      [](auto& changed) { changed.outcome = TargetedRegenerationColumnOutcome::kBuildRejected; });
  expect_column([](auto& changed) { changed.candidate_id.reset(); });
  expect_column([](auto& changed) { ++changed.candidate_id->high; });
  expect_column([](auto& changed) { ++changed.candidate_id->low; });
  expect_column([](auto& changed) { changed.candidate_payload_checksum.reset(); });
  expect_column([](auto& changed) { ++*changed.candidate_payload_checksum; });
  expect_column([](auto& changed) { changed.rejection_code.reset(); });
  expect_column([](auto& changed) {
    changed.rejection_code = candidates::CandidateRejectionCode::kCancelled;
  });
  expect_column([](auto& changed) { changed.rejection.reset(); });
  expect_column([](auto& changed) { changed.rejection->detail.push_back('!'); });
  expect_epoch_sensitive([](auto& changed) { changed.columns.push_back(changed.columns.front()); });
  expect_epoch_sensitive([](auto& changed) { changed.pool_manifest_changed = false; });
  expect_epoch_sensitive([](auto& changed) { changed.selected_route_roster_changed = true; });
  expect_epoch_sensitive([](auto& changed) { changed.complete_price_values_changed = false; });
  expect_epoch_sensitive([](auto& changed) { changed.fixed_point = true; });

  std::array<CpuCandidateAllocationEpochRecord, 2> extra_epochs{epochs.front(), epochs.front()};
  EXPECT_NE(PrimeSessionChecksum(config, counters, extra_epochs), baseline);
}

TEST(CpuCandidateAllocationSessionTest,
     RejectionManifestIsSensitiveToEveryFieldAndOptionalPresence) {
  const candidates::CandidateRejection rejection = PrimeRejection();
  const std::array baseline_records = {rejection};
  const std::uint64_t baseline =
      internal::ComputeCpuCandidateAllocationRejectionManifestChecksumV1(baseline_records);
  const auto expect_sensitive = [&](auto mutate) {
    candidates::CandidateRejection changed = rejection;
    mutate(changed);
    const std::array records = {changed};
    EXPECT_NE(internal::ComputeCpuCandidateAllocationRejectionManifestChecksumV1(records),
              baseline);
  };
  expect_sensitive([](auto& changed) { ++changed.schema_version; });
  expect_sensitive([](auto& changed) { changed.candidate_id.reset(); });
  expect_sensitive([](auto& changed) { ++changed.candidate_id->high; });
  expect_sensitive([](auto& changed) { ++changed.candidate_id->low; });
  expect_sensitive([](auto& changed) { changed.net.reset(); });
  expect_sensitive([](auto& changed) { ++changed.net->id; });
  expect_sensitive([](auto& changed) { ++changed.net->generation; });
  expect_sensitive(
      [](auto& changed) { changed.stage = candidates::CandidateLifecycleStage::kGenerated; });
  expect_sensitive(
      [](auto& changed) { changed.code = candidates::CandidateRejectionCode::kCancelled; });
  expect_sensitive([](auto& changed) { changed.invariant_id.push_back('!'); });
  expect_sensitive([](auto& changed) { ++changed.associations.board_content_hash; });
  expect_sensitive([](auto& changed) { ++changed.associations.compiler_profile_fingerprint; });
  expect_sensitive([](auto& changed) { ++changed.associations.geometry_compiler_version; });
  expect_sensitive([](auto& changed) { ++changed.associations.routing_profile_fingerprint; });
  expect_sensitive([](auto& changed) { ++changed.associations.rule_bucket_identity; });
  expect_sensitive([](auto& changed) { ++changed.policy_identity; });
  expect_sensitive([](auto& changed) {
    changed.provenance.generator = candidates::CandidateGeneratorKind::kCpuAStar;
  });
  expect_sensitive([](auto& changed) { ++changed.provenance.generator_version; });
  expect_sensitive(
      [](auto& changed) { changed.provenance.backend = candidates::CandidateBackendKind::kCpu; });
  expect_sensitive([](auto& changed) { changed.provenance.supported_device_class.push_back('!'); });
  expect_sensitive([](auto& changed) { ++changed.provenance.deterministic_seed; });
  expect_sensitive([](auto& changed) { ++changed.provenance.batch_identity; });
  expect_sensitive([](auto& changed) { ++changed.provenance.query_identity; });
  expect_sensitive([](auto& changed) { ++changed.provenance.candidate_ordinal; });

  expect_sensitive([](auto& changed) { changed.primitive_witness_index.reset(); });
  expect_sensitive([](auto& changed) { ++*changed.primitive_witness_index; });
  expect_sensitive([](auto& changed) { changed.resource_witness_index.reset(); });
  expect_sensitive([](auto& changed) { ++*changed.resource_witness_index; });
  expect_sensitive([](auto& changed) { changed.expected_value.reset(); });
  expect_sensitive([](auto& changed) { ++*changed.expected_value; });
  expect_sensitive([](auto& changed) { changed.actual_value.reset(); });
  expect_sensitive([](auto& changed) { ++*changed.actual_value; });
  expect_sensitive([](auto& changed) { changed.conflicting_entity.reset(); });
  expect_sensitive([](auto& changed) { ++changed.conflicting_entity->id; });
  expect_sensitive([](auto& changed) { ++changed.conflicting_entity->generation; });
  expect_sensitive([](auto& changed) { changed.candidate_payload_checksum.reset(); });
  expect_sensitive([](auto& changed) { ++*changed.candidate_payload_checksum; });
  expect_sensitive([](auto& changed) { changed.detail.push_back('!'); });
  expect_sensitive([](auto& changed) { ++changed.logical_bytes; });

  const std::array<candidates::CandidateRejection, 2> two_records{rejection, rejection};
  EXPECT_NE(internal::ComputeCpuCandidateAllocationRejectionManifestChecksumV1(two_records),
            baseline);
  const std::array<candidates::CandidateRejection, 0> no_records{};
  EXPECT_NE(internal::ComputeCpuCandidateAllocationRejectionManifestChecksumV1(no_records),
            baseline);
}

TEST(CpuCandidateAllocationSessionTest, ChainsBoundedRegenerationEpochsBeforePoolFreeze) {
  CpuCandidateAllocationSession session = Executed(MakeFixture(2, 0), SessionConfig());

  ASSERT_FALSE(session.epochs().empty());
  EXPECT_LE(session.epochs().size(), SessionConfig().maximum_regeneration_epochs);
  EXPECT_EQ(session.counters().completed_regeneration_epochs, session.epochs().size());
  for (std::size_t index = 1; index < session.epochs().size(); ++index) {
    EXPECT_EQ(session.epochs()[index - 1].successor_pool_manifest_checksum,
              session.epochs()[index].source_pool_manifest_checksum);
  }
  EXPECT_EQ(session.epochs().back().successor_pool_manifest_checksum,
            session.final_pool_manifest_checksum());
  EXPECT_TRUE(session.final_multi_world().has_active_retention_lease());
}

TEST(CpuCandidateAllocationSessionTest, RuntimeEpochLimitDoesNotMasqueradeAsFixedPoint) {
  CpuCandidateAllocationSessionConfig config = SessionConfig();
  config.maximum_regeneration_epochs = 1;
  CpuCandidateAllocationSession session = Executed(MakeFixture(1, 0), config);

  ASSERT_EQ(session.epochs().size(), 1U);
  EXPECT_FALSE(session.epochs().front().fixed_point);
  EXPECT_EQ(session.terminal_reason(),
            CpuCandidateAllocationTerminalReason::kRegenerationEpochLimit);
}

TEST(CpuCandidateAllocationSessionTest, RuntimeSameRouteDifferentPriceIsNotAFixedPoint) {
  Fixture fixture = MakeFixture(1, 0, 4, 4, true);
  CpuCandidateAllocationSessionConfig config = SessionConfig();
  config.maximum_regeneration_epochs = 1;
  config.regeneration_plan_config.maximum_target_nets = 1;
  config.regeneration_plan_config.maximum_columns_per_net = 1;
  config.regeneration_plan_config.maximum_total_columns = 1;
  config.regeneration_plan_config.maximum_resource_actions_per_net = 4;
  config.regeneration_plan_config.maximum_total_resource_actions = 4;
  internal::SetTargetedRegenerationRouteFailureForTesting(true);
  CpuCandidateAllocationSession session = Executed(std::move(fixture), config);

  ASSERT_EQ(session.epochs().size(), 1U);
  EXPECT_FALSE(session.epochs().front().pool_manifest_changed);
  EXPECT_FALSE(session.epochs().front().selected_route_roster_changed);
  EXPECT_TRUE(session.epochs().front().complete_price_values_changed);
  EXPECT_FALSE(session.epochs().front().fixed_point);
}

TEST(CpuCandidateAllocationSessionTest, RuntimeConvergesToCompleteFixedPoint) {
  CpuCandidateAllocationSessionConfig config = SessionConfig();
  config.maximum_regeneration_epochs = 4;
  config.price_config.maximum_iterations = 4;
  config.price_config.maximum_price_per_resource = 1;
  config.schedules = {MultiWorldSchedule{
      .schedule_key = 1,
      .search_intrinsic_cost_weight = config.intrinsic_cost_weight,
      .maximum_selection_rounds = 1,
  }};
  CpuCandidateAllocationSession session = Executed(MakeFixture(1, 0, 4, 4), config);

  ASSERT_FALSE(session.epochs().empty());
  EXPECT_TRUE(session.epochs().back().fixed_point);
  EXPECT_EQ(session.terminal_reason(), CpuCandidateAllocationTerminalReason::kFixedPoint);
}

TEST(CpuCandidateAllocationSessionTest, ResourceRefinementPrecedesApparentFeasibility) {
  CpuCandidateAllocationSessionConfig config = SessionConfig();
  config.known_unmapped_exact_conflict_count = 1;
  config.limits.maximum_total_planning_expanded_resource_visits = 1;
  config.limits.maximum_total_requested_columns = 1;
  config.limits.maximum_total_route_queries = 1;
  config.limits.maximum_total_route_work_units = 1;
  config.limits.maximum_total_policy_projection_visits = 1;
  config.limits.maximum_total_generated_candidate_bytes = 1;
  config.limits.maximum_total_rejection_bytes = 1;
  config.limits.maximum_total_transient_result_bytes = 1;
  CpuCandidateAllocationSession session = Executed(MakeFixture(1, 1), config);

  EXPECT_EQ(session.terminal_reason(),
            CpuCandidateAllocationTerminalReason::kResourceRefinementRequired);
  EXPECT_TRUE(session.epochs().empty());
  EXPECT_EQ(session.final_multi_world().disposition(),
            MultiWorldExecutionDisposition::kResourceRefinementRequired);
  EXPECT_TRUE(session.final_multi_world().has_active_retention_lease());
}

TEST(CpuCandidateAllocationSessionTest, RefinementWithMaximumEpochLimitDoesNotReserveEpochStorage) {
  CpuCandidateAllocationSessionConfig config = SessionConfig();
  config.maximum_regeneration_epochs = kMaximumCpuCandidateAllocationEpochsV1;
  config.limits.maximum_epoch_records = kMaximumCpuCandidateAllocationEpochsV1;
  config.price_config.maximum_iterations = kMaximumCpuCandidateAllocationEpochsV1;
  config.schedules = {MultiWorldSchedule{
      .schedule_key = 1,
      .search_intrinsic_cost_weight = config.intrinsic_cost_weight,
      .maximum_selection_rounds = 1,
  }};
  config.known_unmapped_exact_conflict_count = 1;
  CpuCandidateAllocationSession session = Executed(MakeFixture(1, 1), config);

  EXPECT_EQ(session.terminal_reason(),
            CpuCandidateAllocationTerminalReason::kResourceRefinementRequired);
  EXPECT_TRUE(session.epochs().empty());
  EXPECT_EQ(internal::CpuCandidateAllocationEpochReservationForTesting(), 0U);
}

TEST(CpuCandidateAllocationSessionTest,
     RefinementDoesNotReserveHypotheticalRegenerationTransactionWork) {
  Fixture fixture =
      MakeFixture(1, 1, 4, 8, false, false, false, 128ULL * 1024ULL * 1024ULL, 1'000'000'000ULL);
  CpuCandidateAllocationSessionConfig config = SessionConfig();
  config.known_unmapped_exact_conflict_count = 1;

  CpuCandidateAllocationSession session = Executed(std::move(fixture), config);

  EXPECT_EQ(session.terminal_reason(),
            CpuCandidateAllocationTerminalReason::kResourceRefinementRequired);
  EXPECT_TRUE(session.epochs().empty());
  EXPECT_EQ(session.counters().route_queries, 0U);
}

TEST(CpuCandidateAllocationSessionTest,
     RefinementDoesNotChargeTerminalBranchRoundsOrParetoComparisons) {
  Fixture fixture = MakeFixture(1, 1);
  CpuCandidateAllocationSessionConfig config = SessionConfig();
  config.known_unmapped_exact_conflict_count = 1;
  config.schedules = {
      MultiWorldSchedule{
          .schedule_key = 1, .search_intrinsic_cost_weight = 1, .maximum_selection_rounds = 1},
      MultiWorldSchedule{
          .schedule_key = 2, .search_intrinsic_cost_weight = 2, .maximum_selection_rounds = 1},
      MultiWorldSchedule{
          .schedule_key = 3, .search_intrinsic_cost_weight = 3, .maximum_selection_rounds = 1},
  };
  config.multi_world_config.maximum_total_selection_rounds = 2;
  config.multi_world_config.maximum_pareto_comparisons = 2;

  CpuCandidateAllocationSession session = Executed(std::move(fixture), config);

  EXPECT_EQ(session.terminal_reason(),
            CpuCandidateAllocationTerminalReason::kResourceRefinementRequired);
  EXPECT_EQ(session.final_multi_world().counters().completed_worlds, 0U);
  EXPECT_EQ(session.final_multi_world().counters().pareto_comparisons, 0U);
}

TEST(CpuCandidateAllocationSessionTest, AggregateEnvelopeAcceptsEqualityAndRejectsOneUnder) {
  Fixture equality_fixture = MakeFixture(1, 0);
  CpuCandidateAllocationSessionConfig equality_config = SessionConfig();
  const auto envelope = internal::ProjectCpuCandidateAllocationSessionEnvelopeV1(
      equality_fixture.prepared.pools().size(),
      equality_fixture.prepared.counters().retained_candidates,
      ExpandedResourceUses(equality_fixture.prepared),
      equality_fixture.prepared.candidate_store().config(), equality_config);
  ASSERT_TRUE(envelope.has_value());
  equality_config.limits.maximum_total_requested_columns = envelope->maximum_requested_columns;
  equality_config.limits.maximum_total_route_queries = envelope->maximum_requested_columns;
  equality_config.limits.maximum_total_route_work_units = envelope->maximum_route_work_units;
  equality_config.limits.maximum_total_planning_expanded_resource_visits =
      envelope->maximum_planning_expanded_resource_visits;
  equality_config.limits.maximum_total_policy_projection_visits =
      envelope->maximum_policy_projection_visits;
  equality_config.limits.maximum_total_generated_candidate_bytes =
      envelope->maximum_generated_candidate_bytes;
  equality_config.limits.maximum_total_rejection_bytes = envelope->maximum_rejection_bytes;
  equality_config.limits.maximum_total_transient_result_bytes =
      envelope->maximum_transient_result_bytes;
  static_cast<void>(Executed(std::move(equality_fixture), equality_config));

  Fixture rejected_fixture = MakeFixture(1, 0);
  CpuCandidateAllocationSessionConfig rejected_config = equality_config;
  ASSERT_GT(rejected_config.limits.maximum_total_requested_columns, 0U);
  --rejected_config.limits.maximum_total_requested_columns;
  CpuCandidateAllocationSessionResult rejected = ExecuteCpuCandidateAllocationSession(
      kCpuCandidateAllocationSessionSchemaVersion, std::move(rejected_fixture.board),
      std::move(rejected_fixture.workload), std::move(rejected_fixture.capacities),
      std::move(rejected_fixture.prepared), rejected_config);
  ASSERT_TRUE(std::holds_alternative<CpuCandidateAllocationSessionError>(rejected));
  EXPECT_EQ(std::get<CpuCandidateAllocationSessionError>(rejected).code,
            CpuCandidateAllocationSessionErrorCode::kWorkBoundExceeded);
}

TEST(CpuCandidateAllocationSessionTest,
     TerminalMultiWorldEnvelopeAcceptsEqualityAndEveryOneUnderFails) {
  MultiWorldExecutionConfig equality;
  equality.maximum_worlds = 2;
  equality.maximum_buffered_terminal_selection_records = 4;
  equality.maximum_buffered_terminal_resource_records = 14;
  equality.maximum_buffered_terminal_price_records = 10;
  equality.maximum_retained_worlds = 2;
  equality.maximum_retained_selection_records = 4;
  equality.maximum_retained_resource_records = 14;
  equality.maximum_retained_price_records = 10;
  equality.maximum_retained_winner_pins = 4;
  internal::MultiWorldTerminalEnvelopeV1 projection;
  EXPECT_TRUE(internal::MultiWorldTerminalEnvelopeFitsV1(2, 2, 16, 7, 5, equality, &projection));
  EXPECT_EQ(projection, (internal::MultiWorldTerminalEnvelopeV1{
                            .buffered_selection_records = 4,
                            .buffered_resource_records = 14,
                            .buffered_price_records = 10,
                            .retained_worlds = 2,
                            .retained_selection_records = 4,
                            .retained_resource_records = 14,
                            .retained_price_records = 10,
                            .retained_winner_pins = 4,
                        }));

  const auto expect_rejected = [](const MultiWorldExecutionConfig& config) {
    EXPECT_FALSE(internal::MultiWorldTerminalEnvelopeFitsV1(2, 2, 16, 7, 5, config, nullptr));
  };
  MultiWorldExecutionConfig one_under = equality;
  --one_under.maximum_buffered_terminal_selection_records;
  expect_rejected(one_under);
  one_under = equality;
  --one_under.maximum_buffered_terminal_resource_records;
  expect_rejected(one_under);
  one_under = equality;
  --one_under.maximum_buffered_terminal_price_records;
  expect_rejected(one_under);
  one_under = equality;
  --one_under.maximum_retained_worlds;
  expect_rejected(one_under);
  one_under = equality;
  --one_under.maximum_retained_selection_records;
  expect_rejected(one_under);
  one_under = equality;
  --one_under.maximum_retained_resource_records;
  expect_rejected(one_under);
  one_under = equality;
  --one_under.maximum_retained_price_records;
  expect_rejected(one_under);
  one_under = equality;
  --one_under.maximum_retained_winner_pins;
  expect_rejected(one_under);
}

TEST(CpuCandidateAllocationSessionTest, TerminalBufferOneUnderFailsBeforeRoutingOrStoreMutation) {
  Fixture fixture = MakeFixture(1, 0);
  const CandidateStoreState store_before =
      CaptureStoreState(fixture.prepared.candidate_store(), fixture.workload);
  const std::uint64_t queries_before = internal::TargetedRegenerationRouteQueriesForTesting();
  CpuCandidateAllocationSessionConfig config = SessionConfig();
  config.multi_world_config.maximum_buffered_terminal_selection_records = 3;
  CpuCandidateAllocationSessionResult result = ExecuteCpuCandidateAllocationSession(
      kCpuCandidateAllocationSessionSchemaVersion, std::move(fixture.board),
      std::move(fixture.workload), std::move(fixture.capacities), std::move(fixture.prepared),
      config);

  ASSERT_TRUE(std::holds_alternative<CpuCandidateAllocationSessionError>(result));
  EXPECT_EQ(std::get<CpuCandidateAllocationSessionError>(result).code,
            CpuCandidateAllocationSessionErrorCode::kWorkBoundExceeded);
  EXPECT_EQ(internal::TargetedRegenerationRouteQueriesForTesting(), queries_before);
  ExpectStoreState(fixture.prepared.candidate_store(), store_before);
}

TEST(CpuCandidateAllocationSessionTest,
     TerminalResourceBufferCountsSelectedEdgesAndAcceptsEquality) {
  Fixture fixture = MakeFixture(1, 0);
  CpuCandidateAllocationSessionConfig config = SessionConfig();
  std::uint64_t resources_per_world = config.allocator_limits.maximum_resource_records;
  for (const CandidatePool& pool : fixture.prepared.pools()) {
    std::uint64_t greatest_candidate_edges =
        config.regeneration_execution_config.route_limits.maximum_reconstruction_states;
    for (const candidates::StoredCandidate& candidate : pool.candidates) {
      std::uint64_t candidate_edges = 0;
      for (const candidates::PhysicalEdgeSpan& span : candidate->data().resources) {
        candidate_edges += span.edge_count;
      }
      greatest_candidate_edges = std::max(greatest_candidate_edges, candidate_edges);
    }
    resources_per_world += greatest_candidate_edges;
  }
  const std::uint64_t terminal_resource_records = config.schedules.size() * resources_per_world;
  config.multi_world_config.maximum_buffered_terminal_resource_records = terminal_resource_records;
  config.multi_world_config.maximum_retained_resource_records = terminal_resource_records;

  static_cast<void>(Executed(std::move(fixture), config));
}

TEST(CpuCandidateAllocationSessionTest,
     TerminalResourceBufferOneUnderFailsBeforeRoutingOrStoreMutation) {
  Fixture fixture = MakeFixture(1, 0);
  const CandidateStoreState store_before =
      CaptureStoreState(fixture.prepared.candidate_store(), fixture.workload);
  const std::uint64_t queries_before = internal::TargetedRegenerationRouteQueriesForTesting();
  CpuCandidateAllocationSessionConfig config = SessionConfig();
  std::uint64_t resources_per_world = config.allocator_limits.maximum_resource_records;
  for (const CandidatePool& pool : fixture.prepared.pools()) {
    std::uint64_t greatest_candidate_edges =
        config.regeneration_execution_config.route_limits.maximum_reconstruction_states;
    for (const candidates::StoredCandidate& candidate : pool.candidates) {
      std::uint64_t candidate_edges = 0;
      for (const candidates::PhysicalEdgeSpan& span : candidate->data().resources) {
        candidate_edges += span.edge_count;
      }
      greatest_candidate_edges = std::max(greatest_candidate_edges, candidate_edges);
    }
    resources_per_world += greatest_candidate_edges;
  }
  const std::uint64_t terminal_resource_records = config.schedules.size() * resources_per_world;
  ASSERT_GT(terminal_resource_records, 0U);
  config.multi_world_config.maximum_buffered_terminal_resource_records =
      terminal_resource_records - 1U;
  CpuCandidateAllocationSessionResult result = ExecuteCpuCandidateAllocationSession(
      kCpuCandidateAllocationSessionSchemaVersion, std::move(fixture.board),
      std::move(fixture.workload), std::move(fixture.capacities), std::move(fixture.prepared),
      config);

  ASSERT_TRUE(std::holds_alternative<CpuCandidateAllocationSessionError>(result));
  EXPECT_EQ(std::get<CpuCandidateAllocationSessionError>(result).code,
            CpuCandidateAllocationSessionErrorCode::kWorkBoundExceeded);
  EXPECT_EQ(internal::TargetedRegenerationRouteQueriesForTesting(), queries_before);
  ExpectStoreState(fixture.prepared.candidate_store(), store_before);
}

TEST(CpuCandidateAllocationSessionTest,
     CandidateStoreSessionRejectionBoundAcceptsEqualityAndOneUnderFailsBeforeRouting) {
  const CpuCandidateAllocationSessionConfig config = SessionConfig();
  Fixture projection_fixture = MakeFixture(1, 0);
  std::uint64_t source_candidates = 0;
  for (const CandidatePool& pool : projection_fixture.prepared.pools()) {
    source_candidates += pool.candidates.size();
  }
  const auto envelope = internal::ProjectCpuCandidateAllocationSessionEnvelopeV1(
      projection_fixture.prepared.pools().size(), source_candidates, 0,
      projection_fixture.prepared.candidate_store().config(), config);
  ASSERT_TRUE(envelope.has_value());
  const std::uint64_t rejection_bound =
      projection_fixture.prepared.candidate_store().RejectionCount() +
      static_cast<std::uint64_t>(config.maximum_regeneration_epochs) *
          (envelope->maximum_final_candidate_count +
           config.regeneration_plan_config.maximum_total_columns);

  Fixture equality_fixture =
      MakeFixture(1, 0, 4, 8, false, false, false, 1024ULL * 1024ULL * 1024ULL, 100'000'000'000ULL,
                  rejection_bound);
  static_cast<void>(Executed(std::move(equality_fixture), config));

  ASSERT_GT(rejection_bound, 0U);
  Fixture rejected_fixture =
      MakeFixture(1, 0, 4, 8, false, false, false, 1024ULL * 1024ULL * 1024ULL, 100'000'000'000ULL,
                  rejection_bound - 1U);
  const CandidateStoreState store_before =
      CaptureStoreState(rejected_fixture.prepared.candidate_store(), rejected_fixture.workload);
  const std::uint64_t queries_before = internal::TargetedRegenerationRouteQueriesForTesting();
  CpuCandidateAllocationSessionResult result = ExecuteCpuCandidateAllocationSession(
      kCpuCandidateAllocationSessionSchemaVersion, std::move(rejected_fixture.board),
      std::move(rejected_fixture.workload), std::move(rejected_fixture.capacities),
      std::move(rejected_fixture.prepared), config);

  ASSERT_TRUE(std::holds_alternative<CpuCandidateAllocationSessionError>(result));
  EXPECT_EQ(std::get<CpuCandidateAllocationSessionError>(result).code,
            CpuCandidateAllocationSessionErrorCode::kWorkBoundExceeded);
  EXPECT_EQ(internal::TargetedRegenerationRouteQueriesForTesting(), queries_before);
  ExpectStoreState(rejected_fixture.prepared.candidate_store(), store_before);
}

TEST(CpuCandidateAllocationSessionTest,
     CandidateStoreEpochInputByteBoundAcceptsEqualityAndOneUnderFailsBeforeRouting) {
  const CpuCandidateAllocationSessionConfig config = SessionConfig();
  const std::uint64_t maximum_policy_entries =
      std::min(config.regeneration_execution_config.maximum_policy_resource_entries,
               config.regeneration_plan_config.maximum_total_columns *
                       config.price_config.maximum_price_records +
                   config.regeneration_plan_config.maximum_total_columns);
  const std::optional<std::uint64_t> input_bound =
      internal::ComputeTargetedRegenerationAdmissionInputBytesV2(
          config.regeneration_plan_config.maximum_total_columns,
          config.regeneration_execution_config.maximum_candidate_draft_bytes,
          maximum_policy_entries);
  ASSERT_TRUE(input_bound.has_value());

  Fixture equality_fixture =
      MakeFixture(1, 0, 4, 8, false, false, false, *input_bound, 100'000'000'000ULL);
  static_cast<void>(Executed(std::move(equality_fixture), config));

  ASSERT_GT(*input_bound, 0U);
  Fixture rejected_fixture =
      MakeFixture(1, 0, 4, 8, false, false, false, *input_bound - 1U, 100'000'000'000ULL);
  const CandidateStoreState store_before =
      CaptureStoreState(rejected_fixture.prepared.candidate_store(), rejected_fixture.workload);
  const std::uint64_t queries_before = internal::TargetedRegenerationRouteQueriesForTesting();
  CpuCandidateAllocationSessionResult result = ExecuteCpuCandidateAllocationSession(
      kCpuCandidateAllocationSessionSchemaVersion, std::move(rejected_fixture.board),
      std::move(rejected_fixture.workload), std::move(rejected_fixture.capacities),
      std::move(rejected_fixture.prepared), config);

  ASSERT_TRUE(std::holds_alternative<CpuCandidateAllocationSessionError>(result));
  EXPECT_EQ(std::get<CpuCandidateAllocationSessionError>(result).code,
            CpuCandidateAllocationSessionErrorCode::kWorkBoundExceeded);
  EXPECT_EQ(internal::TargetedRegenerationRouteQueriesForTesting(), queries_before);
  ExpectStoreState(rejected_fixture.prepared.candidate_store(), store_before);
}

TEST(CpuCandidateAllocationSessionTest,
     CandidateStoreEpochWorkBoundAcceptsEqualityAndOneUnderFailsBeforeRouting) {
  const CpuCandidateAllocationSessionConfig config = SessionConfig();
  const std::uint64_t maximum_policy_entries =
      std::min(config.regeneration_execution_config.maximum_policy_resource_entries,
               config.regeneration_plan_config.maximum_total_columns *
                       config.price_config.maximum_price_records +
                   config.regeneration_plan_config.maximum_total_columns);
  const std::optional<std::uint64_t> work_bound =
      internal::ComputeTargetedRegenerationAdmissionWorkV2(
          config.regeneration_plan_config.maximum_total_columns,
          config.regeneration_execution_config.route_limits.maximum_reconstruction_states,
          maximum_policy_entries, 0, 4);
  ASSERT_TRUE(work_bound.has_value());

  Fixture equality_fixture =
      MakeFixture(1, 0, 4, 8, false, false, false, 1024ULL * 1024ULL * 1024ULL, *work_bound);
  static_cast<void>(Executed(std::move(equality_fixture), config));

  ASSERT_GT(*work_bound, 0U);
  Fixture rejected_fixture =
      MakeFixture(1, 0, 4, 8, false, false, false, 1024ULL * 1024ULL * 1024ULL, *work_bound - 1U);
  const CandidateStoreState store_before =
      CaptureStoreState(rejected_fixture.prepared.candidate_store(), rejected_fixture.workload);
  const std::uint64_t queries_before = internal::TargetedRegenerationRouteQueriesForTesting();
  CpuCandidateAllocationSessionResult result = ExecuteCpuCandidateAllocationSession(
      kCpuCandidateAllocationSessionSchemaVersion, std::move(rejected_fixture.board),
      std::move(rejected_fixture.workload), std::move(rejected_fixture.capacities),
      std::move(rejected_fixture.prepared), config);

  ASSERT_TRUE(std::holds_alternative<CpuCandidateAllocationSessionError>(result));
  EXPECT_EQ(std::get<CpuCandidateAllocationSessionError>(result).code,
            CpuCandidateAllocationSessionErrorCode::kWorkBoundExceeded);
  EXPECT_EQ(internal::TargetedRegenerationRouteQueriesForTesting(), queries_before);
  ExpectStoreState(rejected_fixture.prepared.candidate_store(), store_before);
}

TEST(CpuCandidateAllocationSessionTest,
     RefinementAuthenticationOneUnderFailsBeforeRoutingOrStoreMutation) {
  Fixture fixture = MakeFixture(1, 1);
  const CandidateStoreState store_before =
      CaptureStoreState(fixture.prepared.candidate_store(), fixture.workload);
  const std::uint64_t queries_before = internal::TargetedRegenerationRouteQueriesForTesting();
  CpuCandidateAllocationSessionConfig config = SessionConfig();
  config.known_unmapped_exact_conflict_count = 1;
  ASSERT_GT(fixture.prepared.counters().retained_candidates, 0U);
  config.multi_world_config.maximum_total_candidate_evaluations =
      fixture.prepared.counters().retained_candidates - 1U;
  CpuCandidateAllocationSessionResult result = ExecuteCpuCandidateAllocationSession(
      kCpuCandidateAllocationSessionSchemaVersion, std::move(fixture.board),
      std::move(fixture.workload), std::move(fixture.capacities), std::move(fixture.prepared),
      config);

  ASSERT_TRUE(std::holds_alternative<CpuCandidateAllocationSessionError>(result));
  EXPECT_EQ(std::get<CpuCandidateAllocationSessionError>(result).code,
            CpuCandidateAllocationSessionErrorCode::kWorkBoundExceeded);
  EXPECT_EQ(internal::TargetedRegenerationRouteQueriesForTesting(), queries_before);
  ExpectStoreState(fixture.prepared.candidate_store(), store_before);
}

TEST(CpuCandidateAllocationSessionTest,
     BaseResourceWorkOneUnderRejectsBeforeAnySourceSpanInspection) {
  Fixture fixture = MakeFixture(1, 1);
  const CandidateStoreState store_before =
      CaptureStoreState(fixture.prepared.candidate_store(), fixture.workload);
  CpuCandidateAllocationSessionConfig config = SessionConfig();
  config.known_unmapped_exact_conflict_count = 1;
  ASSERT_GT(config.allocator_limits.maximum_resource_records, 1U);
  config.multi_world_config.maximum_total_resource_work_units =
      config.allocator_limits.maximum_resource_records - 1U;
  CpuCandidateAllocationSessionResult result = ExecuteCpuCandidateAllocationSession(
      kCpuCandidateAllocationSessionSchemaVersion, std::move(fixture.board),
      std::move(fixture.workload), std::move(fixture.capacities), std::move(fixture.prepared),
      config);

  ASSERT_TRUE(std::holds_alternative<CpuCandidateAllocationSessionError>(result));
  EXPECT_EQ(std::get<CpuCandidateAllocationSessionError>(result).code,
            CpuCandidateAllocationSessionErrorCode::kWorkBoundExceeded);
  EXPECT_EQ(internal::CpuCandidateAllocationSourceSpanInspectionsForTesting(), 0U);
  ExpectStoreState(fixture.prepared.candidate_store(), store_before);
}

TEST(CpuCandidateAllocationSessionTest,
     ChargedSpanQuotaStopsAtFirstRejectedSpanWithoutScanningSuffix) {
  Fixture fixture = MakeFixture(1, 1);
  std::uint64_t total_spans = 0;
  for (const CandidatePool& pool : fixture.prepared.pools()) {
    for (const candidates::StoredCandidate& candidate : pool.candidates) {
      total_spans += candidate->data().resources.size();
    }
  }
  ASSERT_GT(total_spans, 2U);
  const CandidateStoreState store_before =
      CaptureStoreState(fixture.prepared.candidate_store(), fixture.workload);
  CpuCandidateAllocationSessionConfig config = SessionConfig();
  config.known_unmapped_exact_conflict_count = 1;
  config.multi_world_config.maximum_total_candidate_span_visits = 1;
  CpuCandidateAllocationSessionResult result = ExecuteCpuCandidateAllocationSession(
      kCpuCandidateAllocationSessionSchemaVersion, std::move(fixture.board),
      std::move(fixture.workload), std::move(fixture.capacities), std::move(fixture.prepared),
      config);

  ASSERT_TRUE(std::holds_alternative<CpuCandidateAllocationSessionError>(result));
  EXPECT_EQ(std::get<CpuCandidateAllocationSessionError>(result).code,
            CpuCandidateAllocationSessionErrorCode::kWorkBoundExceeded);
  EXPECT_EQ(internal::CpuCandidateAllocationSourceSpanInspectionsForTesting(), 2U);
  EXPECT_LT(internal::CpuCandidateAllocationSourceSpanInspectionsForTesting(), total_spans);
  ExpectStoreState(fixture.prepared.candidate_store(), store_before);
}

TEST(CpuCandidateAllocationSessionTest, RejectsSchedulesWithoutTheCanonicalAnchor) {
  Fixture fixture = MakeFixture(1, 1);
  const std::uint64_t board_hash = fixture.board.content_hash();
  const std::uint64_t workload_checksum = fixture.workload.workload_checksum();
  const ResourceCapacityModel capacities_before = fixture.capacities;
  const std::uint64_t preparation_checksum = fixture.prepared.preparation_checksum();
  const CandidateStoreState store_before =
      CaptureStoreState(fixture.prepared.candidate_store(), fixture.workload);
  CpuCandidateAllocationSessionConfig config = SessionConfig();
  config.schedules = {MultiWorldSchedule{
      .schedule_key = 7, .search_intrinsic_cost_weight = 2, .maximum_selection_rounds = 1}};
  CpuCandidateAllocationSessionResult result = ExecuteCpuCandidateAllocationSession(
      kCpuCandidateAllocationSessionSchemaVersion, std::move(fixture.board),
      std::move(fixture.workload), std::move(fixture.capacities), std::move(fixture.prepared),
      config);
  ASSERT_TRUE(std::holds_alternative<CpuCandidateAllocationSessionError>(result));
  EXPECT_EQ(std::get<CpuCandidateAllocationSessionError>(result).code,
            CpuCandidateAllocationSessionErrorCode::kInvalidConfiguration);
  EXPECT_EQ(fixture.board.content_hash(), board_hash);
  EXPECT_EQ(fixture.workload.workload_checksum(), workload_checksum);
  EXPECT_EQ(fixture.capacities, capacities_before);
  EXPECT_EQ(fixture.prepared.preparation_checksum(), preparation_checksum);
  ExpectStoreState(fixture.prepared.candidate_store(), store_before);
}

TEST(CpuCandidateAllocationSessionTest, MovedFromPreparationRejectsWithoutConsumingOtherInputs) {
  Fixture fixture = MakeFixture(1, 1);
  PreparedCpuCandidatePools owner = std::move(fixture.prepared);
  ASSERT_TRUE(owner.has_candidate_store());
  ASSERT_FALSE(fixture.prepared.has_candidate_store());
  const std::uint64_t board_hash = fixture.board.content_hash();
  const std::uint64_t workload_checksum = fixture.workload.workload_checksum();
  const ResourceCapacityModel capacities_before = fixture.capacities;
  CpuCandidateAllocationSessionResult result = ExecuteCpuCandidateAllocationSession(
      kCpuCandidateAllocationSessionSchemaVersion, std::move(fixture.board),
      std::move(fixture.workload), std::move(fixture.capacities), std::move(fixture.prepared),
      SessionConfig());

  ASSERT_TRUE(std::holds_alternative<CpuCandidateAllocationSessionError>(result));
  EXPECT_EQ(std::get<CpuCandidateAllocationSessionError>(result).code,
            CpuCandidateAllocationSessionErrorCode::kAssociationMismatch);
  EXPECT_EQ(fixture.board.content_hash(), board_hash);
  EXPECT_EQ(fixture.workload.workload_checksum(), workload_checksum);
  EXPECT_EQ(fixture.capacities, capacities_before);
  EXPECT_TRUE(owner.candidate_store().valid());
}

TEST(CpuCandidateAllocationSessionTest, FinalAssemblyFailureLeavesEveryCallerInputUnconsumed) {
  Fixture fixture = MakeFixture(1, 1);
  const std::uint64_t board_hash = fixture.board.content_hash();
  const std::uint64_t workload_checksum = fixture.workload.workload_checksum();
  const ResourceCapacityModel capacities_before = fixture.capacities;
  const std::uint64_t preparation_checksum = fixture.prepared.preparation_checksum();
  const CandidateStoreState store_before =
      CaptureStoreState(fixture.prepared.candidate_store(), fixture.workload);
  internal::SetCpuCandidateAllocationFinalAssemblyFailureForTesting(true);
  CpuCandidateAllocationSessionResult result = ExecuteCpuCandidateAllocationSession(
      kCpuCandidateAllocationSessionSchemaVersion, std::move(fixture.board),
      std::move(fixture.workload), std::move(fixture.capacities), std::move(fixture.prepared),
      SessionConfig());
  internal::SetCpuCandidateAllocationFinalAssemblyFailureForTesting(false);

  ASSERT_TRUE(std::holds_alternative<CpuCandidateAllocationSessionError>(result));
  const CpuCandidateAllocationSessionError& error =
      std::get<CpuCandidateAllocationSessionError>(result);
  EXPECT_EQ(error.code, CpuCandidateAllocationSessionErrorCode::kResourceExhausted);
  EXPECT_FALSE(error.candidate_store_publication_committed);
  EXPECT_EQ(fixture.board.content_hash(), board_hash);
  EXPECT_EQ(fixture.workload.workload_checksum(), workload_checksum);
  EXPECT_EQ(fixture.capacities, capacities_before);
  EXPECT_EQ(fixture.prepared.preparation_checksum(), preparation_checksum);
  ExpectStoreState(fixture.prepared.candidate_store(), store_before);
}

TEST(CpuCandidateAllocationSessionTest,
     FinalAssemblyFailureAfterPublicationPreservesCommitAndReleasesPins) {
  CpuCandidateAllocationSessionConfig config = SessionConfig();
  config.maximum_regeneration_epochs = 1;
  CpuCandidateAllocationSession expected = Executed(MakeFixture(1, 0), config);
  Fixture fixture = MakeFixture(1, 0);
  const std::uint64_t board_hash = fixture.board.content_hash();
  const std::uint64_t workload_checksum = fixture.workload.workload_checksum();
  internal::SetCpuCandidateAllocationFinalAssemblyFailureForTesting(true);
  CpuCandidateAllocationSessionResult result = ExecuteCpuCandidateAllocationSession(
      kCpuCandidateAllocationSessionSchemaVersion, std::move(fixture.board),
      std::move(fixture.workload), std::move(fixture.capacities), std::move(fixture.prepared),
      config);
  internal::SetCpuCandidateAllocationFinalAssemblyFailureForTesting(false);

  ASSERT_TRUE(std::holds_alternative<CpuCandidateAllocationSessionError>(result));
  const CpuCandidateAllocationSessionError& error =
      std::get<CpuCandidateAllocationSessionError>(result);
  EXPECT_EQ(error.code, CpuCandidateAllocationSessionErrorCode::kResourceExhausted);
  EXPECT_TRUE(error.candidate_store_publication_committed);
  EXPECT_EQ(fixture.board.content_hash(), board_hash);
  EXPECT_EQ(fixture.workload.workload_checksum(), workload_checksum);
  ExpectStoresEqual(fixture.prepared.candidate_store(), expected.preparation().candidate_store(),
                    fixture.workload);
  for (const PreparedNetRoutingContext& context : fixture.workload.nets()) {
    for (const candidates::StoredCandidate& candidate :
         fixture.prepared.candidate_store().Enumerate(context.request.net)) {
      ASSERT_NE(candidate, nullptr);
      EXPECT_FALSE(fixture.prepared.candidate_store().IsPinned(candidate->id()));
    }
  }
}

TEST(CpuCandidateAllocationSessionTest,
     PrePublicationFailureLeavesEveryCallerInputAndStoreUnchanged) {
  Fixture fixture = MakeFixture(1, 0);
  const std::uint64_t board_hash = fixture.board.content_hash();
  const std::uint64_t workload_checksum = fixture.workload.workload_checksum();
  const ResourceCapacityModel capacities_before = fixture.capacities;
  const std::uint64_t preparation_checksum = fixture.prepared.preparation_checksum();
  const CandidateStoreState store_before =
      CaptureStoreState(fixture.prepared.candidate_store(), fixture.workload);
  internal::SetTargetedRegenerationPostQueryHostFailureForTesting(0);
  CpuCandidateAllocationSessionResult result = ExecuteCpuCandidateAllocationSession(
      kCpuCandidateAllocationSessionSchemaVersion, std::move(fixture.board),
      std::move(fixture.workload), std::move(fixture.capacities), std::move(fixture.prepared),
      SessionConfig());
  internal::SetTargetedRegenerationPostQueryHostFailureForTesting(std::nullopt);

  ASSERT_TRUE(std::holds_alternative<CpuCandidateAllocationSessionError>(result));
  const CpuCandidateAllocationSessionError& error =
      std::get<CpuCandidateAllocationSessionError>(result);
  ASSERT_TRUE(error.failed_regeneration.has_value());
  EXPECT_FALSE(error.failed_regeneration->candidate_store_publication_committed);
  EXPECT_FALSE(error.candidate_store_publication_committed);
  EXPECT_EQ(fixture.board.content_hash(), board_hash);
  EXPECT_EQ(fixture.workload.workload_checksum(), workload_checksum);
  EXPECT_EQ(fixture.capacities, capacities_before);
  EXPECT_EQ(fixture.prepared.preparation_checksum(), preparation_checksum);
  ExpectStoreState(fixture.prepared.candidate_store(), store_before);
}

TEST(CpuCandidateAllocationSessionTest,
     PostPublicationFailurePreservesCallerOwnedAuthoritativeStore) {
  CpuCandidateAllocationSessionConfig config = SessionConfig();
  config.maximum_regeneration_epochs = 1;
  CpuCandidateAllocationSession expected = Executed(MakeFixture(1, 0), config);
  Fixture fixture = MakeFixture(1, 0);
  const std::uint64_t board_hash = fixture.board.content_hash();
  const std::uint64_t workload_checksum = fixture.workload.workload_checksum();
  const candidates::CandidateStoreTelemetry telemetry_before =
      fixture.prepared.candidate_store().telemetry();
  internal::SetTargetedRegenerationPostPublicationHostFailureForTesting(
      internal::TargetedRegenerationHostFailureForTesting::kBadAlloc);
  CpuCandidateAllocationSessionResult result = ExecuteCpuCandidateAllocationSession(
      kCpuCandidateAllocationSessionSchemaVersion, std::move(fixture.board),
      std::move(fixture.workload), std::move(fixture.capacities), std::move(fixture.prepared),
      config);
  internal::SetTargetedRegenerationPostPublicationHostFailureForTesting(std::nullopt);

  ASSERT_TRUE(std::holds_alternative<CpuCandidateAllocationSessionError>(result));
  const CpuCandidateAllocationSessionError& error =
      std::get<CpuCandidateAllocationSessionError>(result);
  ASSERT_TRUE(error.failed_regeneration.has_value());
  EXPECT_TRUE(error.failed_regeneration->candidate_store_publication_committed);
  EXPECT_TRUE(error.candidate_store_publication_committed);
  EXPECT_EQ(fixture.board.content_hash(), board_hash);
  EXPECT_EQ(fixture.workload.workload_checksum(), workload_checksum);
  EXPECT_TRUE(fixture.prepared.candidate_store().valid());
  EXPECT_NE(fixture.prepared.candidate_store().telemetry(), telemetry_before);
  ExpectStoresEqual(fixture.prepared.candidate_store(), expected.preparation().candidate_store(),
                    fixture.workload);
  for (const PreparedNetRoutingContext& context : fixture.workload.nets()) {
    for (const candidates::StoredCandidate& candidate :
         fixture.prepared.candidate_store().Enumerate(context.request.net)) {
      ASSERT_NE(candidate, nullptr);
      EXPECT_FALSE(fixture.prepared.candidate_store().IsPinned(candidate->id()));
    }
  }
}

}  // namespace
}  // namespace apgar::allocator
