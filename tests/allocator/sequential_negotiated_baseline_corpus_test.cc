#include <cstdint>
#include <cstdlib>
#include <utility>
#include <variant>

#include "apgar/allocator/sequential_negotiated_baseline.h"
#include "apgar/benchmark/phase4_representative_corpus.h"
#include "tests/support/google_test.h"

namespace apgar::allocator {
namespace {

[[nodiscard]] benchmark::Phase4RepresentativeCase Case(
    benchmark::Phase4RepresentativeCaseResult result) {
  EXPECT_TRUE(std::holds_alternative<benchmark::Phase4RepresentativeCase>(result));
  if (!std::holds_alternative<benchmark::Phase4RepresentativeCase>(result)) {
    std::abort();
  }
  return std::get<benchmark::Phase4RepresentativeCase>(std::move(result));
}

[[nodiscard]] SequentialNegotiatedBaselineConfig CorpusConfig(
    const benchmark::Phase4RepresentativeCase& corpus) {
  SequentialNegotiatedBaselineConfig config;
  config.deterministic_seed = corpus.descriptor.deterministic_seed;
  config.maximum_sweeps = 2;
  config.price_config = NegotiatedPriceConfig{
      .present_step_per_overuse_unit = 50,
      .history_step_per_overuse_unit = 10,
      .maximum_price_per_resource = 1'000'000,
      .maximum_iterations = 2,
      .maximum_price_records = 100'000,
  };
  config.known_unmapped_exact_conflict_count =
      corpus.descriptor.known_unmapped_exact_conflicts ? 1 : 0;
  return config;
}

TEST(SequentialNegotiatedBaselineCorpusTest,
     ExecutesEveryFrozenExactOracleFamilyWithCompleteSweepEvidence) {
  for (const std::uint32_t case_id : {100U, 101U, 102U}) {
    const benchmark::Phase4RepresentativeCase corpus =
        Case(benchmark::BuildPhase4RepresentativeCaseV1(case_id, {}));
    const SequentialNegotiatedBaselineExecution execution = ExecuteSequentialNegotiatedBaseline(
        corpus.board, corpus.workload, corpus.capacities, CorpusConfig(corpus));
    ASSERT_TRUE(std::holds_alternative<SequentialNegotiatedBaselineResult>(execution))
        << (std::holds_alternative<SequentialNegotiatedBaselineError>(execution)
                ? std::string(std::get<SequentialNegotiatedBaselineError>(execution).invariant_id)
                : std::string{});
    const SequentialNegotiatedBaselineResult& result =
        std::get<SequentialNegotiatedBaselineResult>(execution);
    EXPECT_EQ(result.workload_checksum(), corpus.workload.workload_checksum());
    EXPECT_EQ(result.final_pools().size(), corpus.workload.nets().size());
    EXPECT_EQ(result.columns().size(),
              result.counters().completed_sweeps * corpus.workload.nets().size());
    EXPECT_EQ(result.sweeps().size(), result.counters().completed_sweeps);
    EXPECT_EQ(result.successor_price_state().iteration(), result.counters().completed_sweeps);
    EXPECT_NE(result.session_checksum(), 0U);
    for (const SequentialNegotiatedSweepRecord& sweep : result.sweeps()) {
      EXPECT_EQ(sweep.route_query_count, corpus.workload.nets().size());
      EXPECT_NE(sweep.world_checksum, 0U);
      EXPECT_NE(sweep.successor_price_state_checksum, 0U);
    }
  }
}

}  // namespace
}  // namespace apgar::allocator
