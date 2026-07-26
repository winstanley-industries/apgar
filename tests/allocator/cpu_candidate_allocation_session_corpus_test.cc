#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "apgar/allocator/cpu_candidate_allocation_session.h"
#include "apgar/benchmark/phase4_representative_corpus.h"
#include "src/allocator/targeted_regeneration_internal.h"
#include "src/benchmark/phase4_h4096_canonical_budget_internal.h"
#include "tests/support/google_test.h"

namespace apgar::allocator {
namespace {

template <typename Value, typename Error>
[[nodiscard]] Value Built(std::variant<Value, Error> result) {
  EXPECT_TRUE(std::holds_alternative<Value>(result))
      << (std::holds_alternative<Error>(result) ? std::string(std::get<Error>(result).invariant_id)
                                                : std::string{});
  if (!std::holds_alternative<Value>(result)) {
    std::abort();
  }
  return std::get<Value>(std::move(result));
}

[[nodiscard]] benchmark::Phase4RepresentativeCase Case(
    benchmark::Phase4RepresentativeCaseResult result) {
  return Built<benchmark::Phase4RepresentativeCase>(std::move(result));
}

[[nodiscard]] std::unique_ptr<PersistentCpuCandidatePoolPreparer> Preparer() {
  return Built<std::unique_ptr<PersistentCpuCandidatePoolPreparer>>(
      CreatePersistentCpuCandidatePoolPreparer({.worker_count = 4}));
}

[[nodiscard]] benchmark::Phase4CanonicalCellConfig H4096CalibrationCell() {
  benchmark::Phase4CanonicalCellConfig cell;
  cell.case_id = 10'200;
  cell.requested_pool_size = 8;
  cell.preparation_worker_count = 4;
  cell.repetitions = 20;
  cell.maximum_setup_elapsed_nanoseconds = 60'000'000'000ULL;
  cell.external_budget = {
      .maximum_prepared_elapsed_nanoseconds = 120'000'000'000ULL,
      .maximum_cold_elapsed_nanoseconds = 180'000'000'000ULL,
      .maximum_address_space_bytes = 64ULL * 1024ULL * 1024ULL * 1024ULL,
      .maximum_peak_host_bytes = 32ULL * 1024ULL * 1024ULL * 1024ULL,
  };
  return cell;
}

[[nodiscard]] CpuCandidatePoolPreparationConfig PreparationConfig(
    std::uint64_t net_count, std::uint32_t pool_size = 4,
    std::uint64_t maximum_candidates_per_net = 8) {
  const std::uint64_t initial_columns = net_count * pool_size;
  const std::uint64_t retained_capacity = net_count * maximum_candidates_per_net;
  CpuCandidatePoolPreparationConfig config;
  config.requested_candidates_per_net = pool_size;
  config.deterministic_seed = 0xa911'ca11'0ca1ULL;
  config.limits.maximum_generated_candidate_bytes = 128ULL * 1024ULL * 1024ULL * 1024ULL;
  config.store_config = candidates::CandidateStoreConfig{
      .maximum_candidates_per_net = maximum_candidates_per_net,
      .maximum_candidate_bytes_per_net = 64U * 1024U * 1024U,
      .maximum_rejection_records = 4U * retained_capacity + 2U * net_count,
      .maximum_rejection_items_per_transaction = retained_capacity,
      .maximum_admission_items_per_transaction = retained_capacity,
      .maximum_admission_input_bytes_per_transaction = 8ULL * 1024ULL * 1024ULL * 1024ULL,
      .maximum_admission_work_units_per_transaction = 1'000'000'000'000ULL,
      .maximum_pin_lease_items_per_transaction = retained_capacity,
      .maximum_expected_pools_per_invocation = net_count,
      .maximum_expected_candidates_per_invocation = retained_capacity,
  };
  EXPECT_LE(initial_columns, config.store_config.maximum_admission_items_per_transaction);
  return config;
}

[[nodiscard]] CpuCandidateAllocationSessionConfig SessionConfig(
    const benchmark::Phase4RepresentativeCase& corpus) {
  const std::uint64_t net_count = corpus.workload.nets().size();
  CpuCandidateAllocationSessionConfig config;
  config.maximum_regeneration_epochs = 2;
  config.price_config.maximum_iterations = 2;
  config.price_config.maximum_price_records = 100'000;
  config.regeneration_plan_config.maximum_target_nets = net_count;
  config.regeneration_plan_config.maximum_columns_per_net = 1;
  config.regeneration_plan_config.maximum_total_columns = net_count;
  config.regeneration_plan_config.maximum_resource_actions_per_net = 16;
  config.regeneration_plan_config.maximum_total_resource_actions = net_count * 16U;
  config.regeneration_execution_config.maximum_route_queries = net_count;
  config.regeneration_execution_config.maximum_total_route_work_units =
      net_count * config.regeneration_execution_config.route_limits.maximum_work_units;
  config.schedules = {MultiWorldSchedule{
      .schedule_key = 1,
      .search_intrinsic_cost_weight = config.intrinsic_cost_weight,
      .maximum_selection_rounds = 1,
  }};
  config.known_unmapped_exact_conflict_count =
      corpus.descriptor.known_unmapped_exact_conflicts ? 1 : 0;
  return config;
}

[[nodiscard]] OneWorldAllocation ManualInitialWorld(
    const benchmark::Phase4RepresentativeCase& corpus, const PreparedCpuCandidatePools& prepared,
    const CpuCandidateAllocationSessionConfig& config) {
  NegotiatedPriceState state = Built<NegotiatedPriceState>(BuildInitialNegotiatedPriceState(
      kNegotiatedPriceStateSchemaVersion, corpus.capacities, corpus.workload, config.price_config));
  PriceSnapshot prices = Built<PriceSnapshot>(BuildPriceSnapshotForState(corpus.capacities, state));
  const OneWorldAllocationRequest request{
      .schema_version = kOneWorldAllocationSchemaVersion,
      .associations =
          AllocationAssociations{
              .board_content_hash = corpus.workload.board_content_hash(),
              .compiler_profile_fingerprint = corpus.workload.compiler_profile_fingerprint(),
              .geometry_compiler_version = corpus.workload.geometry_compiler_version(),
          },
      .capacities = corpus.capacities,
      .prices = std::move(prices),
      .intrinsic_cost_weight = config.intrinsic_cost_weight,
      .limits = config.allocator_limits,
      .pools = prepared.pools(),
      .workload = &corpus.workload,
  };
  return Built<OneWorldAllocation>(AllocateOneWorld(request));
}

[[nodiscard]] OneWorldAllocationRequest AllocationRequest(
    const benchmark::Phase4RepresentativeCase& corpus,
    const CpuCandidateAllocationSessionConfig& config, const NegotiatedPriceState& state,
    const std::vector<CandidatePool>& pools) {
  PriceSnapshot prices = Built<PriceSnapshot>(BuildPriceSnapshotForState(corpus.capacities, state));
  return OneWorldAllocationRequest{
      .schema_version = kOneWorldAllocationSchemaVersion,
      .associations = corpus.capacities.associations(),
      .capacities = corpus.capacities,
      .prices = std::move(prices),
      .intrinsic_cost_weight = config.intrinsic_cost_weight,
      .limits = config.allocator_limits,
      .pools = pools,
      .workload = &corpus.workload,
  };
}

struct ManualComposition {
  TargetedRegenerationExecution regeneration;
  MultiWorldExecution terminal_worlds;
};

[[nodiscard]] ManualComposition ComposeOneRegenerationEpoch(
    const benchmark::Phase4RepresentativeCase& corpus, PreparedCpuCandidatePools& prepared,
    const CpuCandidateAllocationSessionConfig& config) {
  NegotiatedPriceState initial_state = Built<NegotiatedPriceState>(BuildInitialNegotiatedPriceState(
      kNegotiatedPriceStateSchemaVersion, corpus.capacities, corpus.workload, config.price_config));
  OneWorldAllocationRequest initial_request =
      AllocationRequest(corpus, config, initial_state, prepared.pools());
  OneWorldAllocation initial_world = Built<OneWorldAllocation>(AllocateOneWorld(initial_request));
  EXPECT_GT(initial_world.total_overuse_units, 0U);
  TargetedRegenerationPlan plan = Built<TargetedRegenerationPlan>(BuildTargetedRegenerationPlan(
      kTargetedRegenerationPlanSchemaVersion, initial_state, initial_request, initial_world,
      prepared.candidate_store(), config.regeneration_plan_config));
  TargetedRegenerationExecution regeneration =
      Built<TargetedRegenerationExecution>(ExecuteTargetedRegenerationPlanCpu(
          kTargetedRegenerationExecutionSchemaVersion, corpus.board, initial_request,
          std::move(plan), prepared.candidate_store(), config.regeneration_execution_config));
  MultiWorldPoolSnapshot frozen{
      .capacities = corpus.capacities,
      .allocator_limits = config.allocator_limits,
      .pools = regeneration.refreshed_pools(),
      .workload = &corpus.workload,
  };
  MultiWorldExecution terminal = Built<MultiWorldExecution>(ExecuteMultiWorldCpu(
      kMultiWorldExecutionSchemaVersion, frozen, regeneration.plan().price_state(),
      config.schedules, prepared.candidate_store(), config.multi_world_config));
  return ManualComposition{.regeneration = std::move(regeneration),
                           .terminal_worlds = std::move(terminal)};
}

void ExpectPoolsEqual(const std::vector<CandidatePool>& left,
                      const std::vector<CandidatePool>& right) {
  ASSERT_EQ(left.size(), right.size());
  for (std::size_t pool_index = 0; pool_index < left.size(); ++pool_index) {
    EXPECT_EQ(left[pool_index].net, right[pool_index].net);
    ASSERT_EQ(left[pool_index].candidates.size(), right[pool_index].candidates.size());
    for (std::size_t candidate_index = 0; candidate_index < left[pool_index].candidates.size();
         ++candidate_index) {
      ASSERT_NE(left[pool_index].candidates[candidate_index], nullptr);
      ASSERT_NE(right[pool_index].candidates[candidate_index], nullptr);
      EXPECT_EQ(*left[pool_index].candidates[candidate_index],
                *right[pool_index].candidates[candidate_index]);
    }
  }
}

void ExpectStoresEqual(const candidates::CandidateStore& left,
                       const candidates::CandidateStore& right, const MultiNetWorkload& workload) {
  for (const PreparedNetRoutingContext& context : workload.nets()) {
    const auto left_pool = left.Enumerate(context.request.net);
    const auto right_pool = right.Enumerate(context.request.net);
    ASSERT_EQ(left_pool.size(), right_pool.size());
    for (std::size_t index = 0; index < left_pool.size(); ++index) {
      EXPECT_EQ(*left_pool[index], *right_pool[index]);
    }
  }
  EXPECT_EQ(left.Rejections(), right.Rejections());
}

TEST(CpuCandidateAllocationSessionCorpusTest,
     ExecutesEveryExactFamilyAndMatchesManualInitialComposition) {
  std::unique_ptr<PersistentCpuCandidatePoolPreparer> preparer = Preparer();
  for (const std::uint32_t case_id : {100U, 101U, 102U}) {
    benchmark::Phase4RepresentativeCase corpus =
        Case(benchmark::BuildPhase4RepresentativeCaseV1(case_id, {}));
    const CpuCandidatePoolPreparationConfig preparation_config =
        PreparationConfig(corpus.workload.nets().size());
    PreparedCpuCandidatePools prepared =
        Built<PreparedCpuCandidatePools>(PrepareInitialCpuCandidatePools(
            *preparer, corpus.board, corpus.workload, preparation_config));
    const CpuCandidateAllocationSessionConfig session_config = SessionConfig(corpus);
    const OneWorldAllocation manual = ManualInitialWorld(corpus, prepared, session_config);
    const std::uint64_t workload_checksum = corpus.workload.workload_checksum();
    const bool globally_coupled = corpus.descriptor.globally_coupled_conflict_graph;

    CpuCandidateAllocationSession session =
        Built<CpuCandidateAllocationSession>(ExecuteCpuCandidateAllocationSession(
            kCpuCandidateAllocationSessionSchemaVersion, std::move(corpus.board),
            std::move(corpus.workload), std::move(corpus.capacities), std::move(prepared),
            session_config));

    EXPECT_EQ(session.workload().workload_checksum(), workload_checksum);
    EXPECT_EQ(session.final_pools().size(), session.workload().nets().size());
    EXPECT_TRUE(session.final_multi_world().has_active_retention_lease());
    EXPECT_TRUE(session.final_multi_world().retention_lease_belongs_to(
        session.preparation().candidate_store()));
    EXPECT_NE(session.session_checksum(), 0U);
    if (session.epochs().empty()) {
      EXPECT_EQ(session.final_single_world().world_checksum, manual.world_checksum);
    } else {
      EXPECT_EQ(session.epochs().front().source_world_checksum, manual.world_checksum);
    }
    if (case_id == 101U) {
      EXPECT_TRUE(globally_coupled);
    }
  }
  EXPECT_EQ(preparer->telemetry().invocations_completed, 3U);
}

TEST(CpuCandidateAllocationSessionCorpusTest,
     RegeneratingGloballyCoupledCaseMatchesIndependentFullComposition) {
  benchmark::Phase4RepresentativeCase manual_corpus =
      Case(benchmark::BuildPhase4RepresentativeCaseV1(101, {}));
  benchmark::Phase4RepresentativeCase session_corpus =
      Case(benchmark::BuildPhase4RepresentativeCaseV1(101, {}));
  ASSERT_TRUE(manual_corpus.descriptor.globally_coupled_conflict_graph);
  manual_corpus.capacities = Built<ResourceCapacityModel>(
      BuildResourceCapacityModel(kResourceCapacityModelSchemaVersion, manual_corpus.board,
                                 manual_corpus.workload.nets().front().compiled_board, 0, {}));
  session_corpus.capacities = manual_corpus.capacities;
  CpuCandidateAllocationSessionConfig config = SessionConfig(manual_corpus);
  config.maximum_regeneration_epochs = 1;
  config.price_config.maximum_iterations = 1;
  const CpuCandidatePoolPreparationConfig preparation_config =
      PreparationConfig(manual_corpus.workload.nets().size());
  std::unique_ptr<PersistentCpuCandidatePoolPreparer> manual_preparer = Preparer();
  std::unique_ptr<PersistentCpuCandidatePoolPreparer> session_preparer = Preparer();
  PreparedCpuCandidatePools manual_prepared =
      Built<PreparedCpuCandidatePools>(PrepareInitialCpuCandidatePools(
          *manual_preparer, manual_corpus.board, manual_corpus.workload, preparation_config));
  PreparedCpuCandidatePools session_prepared =
      Built<PreparedCpuCandidatePools>(PrepareInitialCpuCandidatePools(
          *session_preparer, session_corpus.board, session_corpus.workload, preparation_config));
  ManualComposition manual = ComposeOneRegenerationEpoch(manual_corpus, manual_prepared, config);

  CpuCandidateAllocationSession session =
      Built<CpuCandidateAllocationSession>(ExecuteCpuCandidateAllocationSession(
          kCpuCandidateAllocationSessionSchemaVersion, std::move(session_corpus.board),
          std::move(session_corpus.workload), std::move(session_corpus.capacities),
          std::move(session_prepared), config));

  ASSERT_EQ(session.epochs().size(), 1U);
  EXPECT_EQ(session.epochs().front().plan_checksum, manual.regeneration.plan().plan_checksum());
  EXPECT_EQ(session.epochs().front().execution_checksum, manual.regeneration.execution_checksum());
  EXPECT_EQ(session.final_price_state(), manual.regeneration.plan().price_state());
  EXPECT_EQ(session.final_single_world().world_checksum,
            manual.regeneration.refreshed_world().world_checksum);
  ExpectPoolsEqual(session.final_pools(), manual.regeneration.refreshed_pools());
  EXPECT_EQ(session.final_multi_world().execution_checksum(),
            manual.terminal_worlds.execution_checksum());
  ExpectStoresEqual(session.preparation().candidate_store(), manual_prepared.candidate_store(),
                    session.workload());
}

TEST(CpuCandidateAllocationSessionCorpusTest,
     ExecutesRepresentativeCalibrationAtFourEightAndSixteen) {
  for (const std::uint32_t pool_size : {4U, 8U, 16U}) {
    benchmark::Phase4RepresentativeCase corpus =
        Case(benchmark::BuildPhase4RepresentativeCaseV1(200, {}));
    const CpuCandidatePoolPreparationConfig preparation_config =
        PreparationConfig(corpus.workload.nets().size(), pool_size, pool_size);
    std::unique_ptr<PersistentCpuCandidatePoolPreparer> preparer = Preparer();
    PreparedCpuCandidatePools prepared =
        Built<PreparedCpuCandidatePools>(PrepareInitialCpuCandidatePools(
            *preparer, corpus.board, corpus.workload, preparation_config));
    const CpuCandidateAllocationSessionConfig config = SessionConfig(corpus);

    CpuCandidateAllocationSession session =
        Built<CpuCandidateAllocationSession>(ExecuteCpuCandidateAllocationSession(
            kCpuCandidateAllocationSessionSchemaVersion, std::move(corpus.board),
            std::move(corpus.workload), std::move(corpus.capacities), std::move(prepared), config));

    EXPECT_EQ(session.preparation().config().requested_candidates_per_net, pool_size);
    EXPECT_EQ(session.final_pools().size(), 64U);
    EXPECT_TRUE(session.final_multi_world().has_active_retention_lease());
    EXPECT_NE(session.session_checksum(), 0U);
  }
}

TEST(CpuCandidateAllocationSessionCorpusTest,
     H4096CalibrationEpochZeroPlansThirtyTwoDistinctPrimaryConflictSeeds) {
  const benchmark::Phase4PairedTrialSpec spec = Built<benchmark::Phase4PairedTrialSpec>(
      benchmark::internal::BuildPhase4CanonicalTrialSpecForCorpusV2H4096(
          H4096CalibrationCell(), 0, benchmark::Phase4TrialOrder::kBaselineFirst));
  benchmark::Phase4RepresentativeCase corpus =
      Case(benchmark::BuildPhase4RepresentativeCaseV2(spec.case_id, {}));
  std::unique_ptr<PersistentCpuCandidatePoolPreparer> preparer = Preparer();
  PreparedCpuCandidatePools prepared =
      Built<PreparedCpuCandidatePools>(PrepareInitialCpuCandidatePools(
          *preparer, corpus.board, corpus.workload, spec.preparation_config));
  const CpuCandidateAllocationSessionConfig& config = spec.candidate_session_config;
  NegotiatedPriceState initial_state = Built<NegotiatedPriceState>(BuildInitialNegotiatedPriceState(
      kNegotiatedPriceStateSchemaVersion, corpus.capacities, corpus.workload, config.price_config));
  OneWorldAllocationRequest request =
      AllocationRequest(corpus, config, initial_state, prepared.pools());
  const OneWorldAllocation world = Built<OneWorldAllocation>(AllocateOneWorld(request));
  ASSERT_GT(world.total_overuse_units, 0U);

  const TargetedRegenerationPlan plan =
      Built<TargetedRegenerationPlan>(BuildTargetedRegenerationPlan(
          kTargetedRegenerationPlanSchemaVersion, initial_state, request, world,
          prepared.candidate_store(), config.regeneration_plan_config));
  EXPECT_EQ(plan.coverage_seed_target_count(), 32U);
  EXPECT_EQ(plan.targets().size(), 32U);
  EXPECT_EQ(plan.total_requested_columns(), 64U);
  std::set<routing::EdgeResourceKey> primary_resources;
  for (std::size_t target_index = 0;
       target_index < static_cast<std::size_t>(plan.coverage_seed_target_count()); ++target_index) {
    const TargetedRegenerationNet& target = plan.targets()[target_index];
    ASSERT_FALSE(target.resource_actions.empty());
    EXPECT_EQ(target.requested_columns, 2U);
    EXPECT_TRUE(primary_resources.insert(target.resource_actions.front().resource).second);
  }
  EXPECT_EQ(primary_resources.size(), 32U);

  TargetedRegenerationConfig cap_plus_one = config.regeneration_plan_config;
  ++cap_plus_one.maximum_total_columns;
  TargetedRegenerationPlan two_lane_plan = Built<TargetedRegenerationPlan>(
      BuildTargetedRegenerationPlan(kTargetedRegenerationPlanSchemaVersion, initial_state, request,
                                    world, prepared.candidate_store(), cap_plus_one));
  ASSERT_EQ(two_lane_plan.coverage_seed_target_count(), 32U);
  ASSERT_EQ(two_lane_plan.targets().size(), 33U);
  EXPECT_EQ(two_lane_plan.total_requested_columns(), 65U);
  const auto seed_end = two_lane_plan.targets().begin() +
                        static_cast<std::ptrdiff_t>(two_lane_plan.coverage_seed_target_count());
  EXPECT_TRUE(std::ranges::is_sorted(two_lane_plan.targets().begin(), seed_end,
                                     internal::TargetedRegenerationTargetRanksBeforeV1));
  EXPECT_TRUE(std::ranges::is_sorted(seed_end, two_lane_plan.targets().end(),
                                     internal::TargetedRegenerationTargetRanksBeforeV1));
  EXPECT_FALSE(std::ranges::is_sorted(two_lane_plan.targets(),
                                      internal::TargetedRegenerationTargetRanksBeforeV1));
  ASSERT_EQ(std::distance(seed_end, two_lane_plan.targets().end()), 1);
  EXPECT_EQ(seed_end->requested_columns, 1U);
  EXPECT_TRUE(std::ranges::any_of(
      two_lane_plan.targets().begin(), seed_end, [&](const TargetedRegenerationNet& seed) {
        return internal::TargetedRegenerationTargetRanksBeforeV1(*seed_end, seed);
      }));

  TargetedRegenerationExecutionConfig refinement = config.regeneration_execution_config;
  refinement.known_unmapped_exact_conflict_count = 1;
  const TargetedRegenerationExecution execution =
      Built<TargetedRegenerationExecution>(ExecuteTargetedRegenerationPlanCpu(
          kTargetedRegenerationExecutionSchemaVersion, corpus.board, request,
          std::move(two_lane_plan), prepared.candidate_store(), refinement));
  EXPECT_EQ(execution.disposition(),
            TargetedRegenerationExecutionDisposition::kResourceRefinementRequired);
  EXPECT_TRUE(execution.columns().empty());
}

}  // namespace
}  // namespace apgar::allocator
