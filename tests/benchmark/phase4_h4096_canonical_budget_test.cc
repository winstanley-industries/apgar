#include <cstdint>
#include <cstdlib>
#include <string>
#include <utility>
#include <variant>

#include "apgar/benchmark/phase4_representative_corpus.h"
#include "src/benchmark/phase4_h4096_canonical_budget_internal.h"
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

TEST(Phase4H4096CanonicalBudgetTest, ChangesOnlyEqualArmHistoryPriceForEveryCanonicalBudgetCell) {
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
      const Phase4PairedTrialSpec protocol_v1 = Built(
          BuildPhase4CanonicalTrialSpecForCorpusV2(cell, 0, Phase4TrialOrder::kBaselineFirst));
      const Phase4PairedTrialSpec h4096 =
          Built(internal::BuildPhase4CanonicalTrialSpecForCorpusV2H4096(
              cell, 0, Phase4TrialOrder::kBaselineFirst));
      Phase4PairedTrialSpec expected = protocol_v1;
      expected.baseline_config.price_config.history_step_per_overuse_unit =
          internal::kPhase4CorpusV2H4096HistoryStepPerOveruseUnit;
      expected.candidate_session_config.price_config = expected.baseline_config.price_config;

      EXPECT_EQ(protocol_v1.baseline_config.price_config.present_step_per_overuse_unit,
                internal::kPhase4CorpusV2ProtocolV1PresentStepPerOveruseUnit);
      EXPECT_EQ(protocol_v1.baseline_config.price_config.history_step_per_overuse_unit,
                internal::kPhase4CorpusV2ProtocolV1HistoryStepPerOveruseUnit);
      EXPECT_EQ(protocol_v1.candidate_session_config.price_config,
                protocol_v1.baseline_config.price_config);
      EXPECT_EQ(h4096.baseline_config.price_config.present_step_per_overuse_unit,
                internal::kPhase4CorpusV2ProtocolV1PresentStepPerOveruseUnit);
      EXPECT_EQ(h4096.baseline_config.price_config.history_step_per_overuse_unit,
                internal::kPhase4CorpusV2H4096HistoryStepPerOveruseUnit);
      EXPECT_EQ(h4096.candidate_session_config.price_config, h4096.baseline_config.price_config);
      EXPECT_EQ(h4096.schema_version, expected.schema_version);
      EXPECT_EQ(h4096.case_id, expected.case_id);
      EXPECT_EQ(h4096.requested_pool_size, expected.requested_pool_size);
      EXPECT_EQ(h4096.repetition_index, expected.repetition_index);
      EXPECT_EQ(h4096.root_seed, expected.root_seed);
      EXPECT_EQ(h4096.execution_order, expected.execution_order);
      EXPECT_EQ(h4096.preparation_worker_count, expected.preparation_worker_count);
      EXPECT_EQ(h4096.corpus_limits, expected.corpus_limits);
      EXPECT_EQ(h4096.baseline_config, expected.baseline_config);
      EXPECT_EQ(h4096.preparation_config, expected.preparation_config);
      EXPECT_EQ(h4096.candidate_session_config, expected.candidate_session_config);
      EXPECT_EQ(h4096.external_budget, expected.external_budget);
      EXPECT_NE(internal::ComputePhase4CanonicalAlgorithmBudgetChecksumV1(protocol_v1),
                internal::ComputePhase4CanonicalAlgorithmBudgetChecksumV1(h4096));
    }
  }
  EXPECT_EQ(canonical_cells, 102U);
}

}  // namespace
}  // namespace apgar::benchmark
