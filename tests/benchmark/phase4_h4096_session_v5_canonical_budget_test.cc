#include <cstdint>
#include <cstdlib>
#include <string>
#include <utility>
#include <variant>

#include "apgar/allocator/cpu_candidate_allocation_session.h"
#include "apgar/allocator/targeted_regeneration.h"
#include "apgar/allocator/targeted_regeneration_execution.h"
#include "apgar/benchmark/phase4_representative_corpus.h"
#include "src/benchmark/phase4_h4096_canonical_budget_internal.h"
#include "src/benchmark/phase4_h4096_session_v5_canonical_budget_internal.h"
#include "src/benchmark/phase4_paired_trial_internal.h"
#include "tests/support/google_test.h"

namespace apgar::benchmark {
namespace {

[[nodiscard]] Phase4CanonicalCellConfig CanonicalCell(std::uint32_t case_id,
                                                      std::uint32_t pool_size) {
  Phase4CanonicalCellConfig cell;
  cell.case_id = case_id;
  cell.requested_pool_size = pool_size;
  cell.preparation_worker_count = kPhase4CanonicalPreparationWorkersV1;
  cell.repetitions = kPhase4CanonicalRepetitionsV1;
  cell.maximum_setup_elapsed_nanoseconds = 300'000'000'000ULL;
  cell.external_budget = {
      .maximum_prepared_elapsed_nanoseconds = 300'000'000'000ULL,
      .maximum_cold_elapsed_nanoseconds = 300'000'000'000ULL,
      .maximum_address_space_bytes = 64ULL * 1024ULL * 1024ULL * 1024ULL,
      .maximum_peak_host_bytes = 16ULL * 1024ULL * 1024ULL * 1024ULL,
  };
  return cell;
}

[[nodiscard]] Phase4PairedTrialSpec Built(Phase4CanonicalSpecResult result) {
  EXPECT_TRUE(std::holds_alternative<Phase4PairedTrialSpec>(result))
      << (std::holds_alternative<Phase4TrialHarnessError>(result)
              ? std::get<Phase4TrialHarnessError>(result).detail
              : "");
  if (!std::holds_alternative<Phase4PairedTrialSpec>(result)) {
    std::abort();
  }
  return std::get<Phase4PairedTrialSpec>(std::move(result));
}

TEST(Phase4H4096SessionV5CanonicalBudgetTest, PinsExplicitSessionPlannerAndExecutorVersions) {
  EXPECT_EQ(allocator::kCpuCandidateAllocationSessionSchemaVersion,
            allocator::kCpuCandidateAllocationSessionSchemaVersionV5);
  EXPECT_EQ(allocator::kTargetedRegenerationPlanSchemaVersion,
            allocator::kTargetedRegenerationPlanSchemaVersionV3);
  EXPECT_EQ(allocator::kTargetedRegenerationExecutionSchemaVersion,
            allocator::kTargetedRegenerationExecutionSchemaVersionV6);
  EXPECT_EQ(internal::kPhase4H4096SessionV5PlanSchemaVersion,
            allocator::kTargetedRegenerationPlanSchemaVersionV3);
  EXPECT_EQ(internal::kPhase4H4096SessionV5ExecutionSchemaVersion,
            allocator::kTargetedRegenerationExecutionSchemaVersionV6);
}

TEST(Phase4H4096SessionV5CanonicalBudgetTest, RejectsAnyParentOtherThanExplicitSessionV4) {
  Phase4CanonicalSpecResult parent = internal::BuildPhase4CanonicalTrialSpecForCorpusV2H4096(
      CanonicalCell(10'100, 4), 0, Phase4TrialOrder::kBaselineFirst);
  ASSERT_TRUE(std::holds_alternative<Phase4PairedTrialSpec>(parent));
  std::get<Phase4PairedTrialSpec>(parent).candidate_session_config.schema_version =
      allocator::kCpuCandidateAllocationSessionSchemaVersionV3;

  Phase4CanonicalSpecResult rejected =
      internal::ApplyPhase4H4096SessionV5Authority(std::move(parent));
  ASSERT_TRUE(std::holds_alternative<Phase4TrialHarnessError>(rejected));
  EXPECT_EQ(std::get<Phase4TrialHarnessError>(rejected).invariant_id,
            "P4HARNESS-H4096-SESSION-V5-PREIMAGE-001");
}

TEST(Phase4H4096SessionV5CanonicalBudgetTest,
     ChangesExactlyTheSessionSchemaForEveryCanonicalBudgetCell) {
  std::uint64_t canonical_cells = 0;
  for (const Phase4CaseDescriptor& descriptor : Phase4CaseDescriptorsV2()) {
    for (std::uint8_t pool_index = 0; pool_index < descriptor.requested_pool_size_count;
         ++pool_index) {
      const std::uint32_t pool = descriptor.requested_pool_sizes[pool_index];
      if (pool != 4 && pool != 8 && pool != 16) {
        continue;
      }
      SCOPED_TRACE("case=" + std::to_string(descriptor.case_id) + " pool=" + std::to_string(pool));
      ++canonical_cells;
      const Phase4CanonicalCellConfig cell = CanonicalCell(descriptor.case_id, pool);
      const Phase4PairedTrialSpec session_v4 =
          Built(internal::BuildPhase4CanonicalTrialSpecForCorpusV2H4096(
              cell, 0, Phase4TrialOrder::kBaselineFirst));
      const Phase4PairedTrialSpec session_v5 =
          Built(internal::BuildPhase4CanonicalTrialSpecForCorpusV2H4096SessionV5(
              cell, 0, Phase4TrialOrder::kBaselineFirst));

      EXPECT_EQ(session_v4.candidate_session_config.schema_version,
                allocator::kCpuCandidateAllocationSessionSchemaVersionV4);
      EXPECT_EQ(session_v5.candidate_session_config.schema_version,
                allocator::kCpuCandidateAllocationSessionSchemaVersionV5);

      EXPECT_EQ(session_v5.schema_version, session_v4.schema_version);
      EXPECT_EQ(session_v5.case_id, session_v4.case_id);
      EXPECT_EQ(session_v5.requested_pool_size, session_v4.requested_pool_size);
      EXPECT_EQ(session_v5.repetition_index, session_v4.repetition_index);
      EXPECT_EQ(session_v5.root_seed, session_v4.root_seed);
      EXPECT_EQ(session_v5.execution_order, session_v4.execution_order);
      EXPECT_EQ(session_v5.preparation_worker_count, session_v4.preparation_worker_count);
      EXPECT_EQ(session_v5.corpus_limits, session_v4.corpus_limits);
      EXPECT_EQ(session_v5.baseline_config, session_v4.baseline_config);
      EXPECT_EQ(session_v5.preparation_config, session_v4.preparation_config);
      EXPECT_EQ(session_v5.external_budget, session_v4.external_budget);

      const auto& v4_config = session_v4.candidate_session_config;
      const auto& v5_config = session_v5.candidate_session_config;
      EXPECT_EQ(v5_config.intrinsic_cost_weight, v4_config.intrinsic_cost_weight);
      EXPECT_EQ(v5_config.maximum_regeneration_epochs, v4_config.maximum_regeneration_epochs);
      EXPECT_EQ(v5_config.price_config, v4_config.price_config);
      EXPECT_EQ(v5_config.allocator_limits, v4_config.allocator_limits);
      EXPECT_EQ(v5_config.regeneration_plan_config, v4_config.regeneration_plan_config);
      EXPECT_EQ(v5_config.regeneration_execution_config, v4_config.regeneration_execution_config);
      EXPECT_EQ(v5_config.schedules, v4_config.schedules);
      EXPECT_EQ(v5_config.multi_world_config, v4_config.multi_world_config);
      EXPECT_EQ(v5_config.limits, v4_config.limits);
      EXPECT_EQ(v5_config.known_unmapped_exact_conflict_count,
                v4_config.known_unmapped_exact_conflict_count);

      allocator::CpuCandidateAllocationSessionConfig expected_v5 = v4_config;
      expected_v5.schema_version = allocator::kCpuCandidateAllocationSessionSchemaVersionV5;
      EXPECT_EQ(v5_config, expected_v5);

      const std::uint64_t v4_checksum =
          internal::ComputePhase4CanonicalAlgorithmBudgetChecksumV1(session_v4);
      const std::uint64_t v5_checksum =
          internal::ComputePhase4CanonicalAlgorithmBudgetChecksumV1(session_v5);
      EXPECT_NE(v4_checksum, 0U);
      EXPECT_NE(v5_checksum, 0U);
      EXPECT_NE(v5_checksum, v4_checksum);
    }
  }
  EXPECT_EQ(canonical_cells, 102U);
}

}  // namespace
}  // namespace apgar::benchmark
