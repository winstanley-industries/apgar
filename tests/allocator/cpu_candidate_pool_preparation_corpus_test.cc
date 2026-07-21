#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <utility>
#include <variant>

#include "apgar/allocator/cpu_candidate_pool_preparation.h"
#include "apgar/benchmark/phase4_representative_corpus.h"
#include "apgar/candidates/route_candidate.h"
#include "tests/support/google_test.h"

namespace apgar::allocator {
namespace {

[[nodiscard]] benchmark::Phase4RepresentativeCase RequireCase(
    benchmark::Phase4RepresentativeCaseResult result) {
  EXPECT_TRUE(std::holds_alternative<benchmark::Phase4RepresentativeCase>(result));
  if (!std::holds_alternative<benchmark::Phase4RepresentativeCase>(result)) {
    std::abort();
  }
  return std::get<benchmark::Phase4RepresentativeCase>(std::move(result));
}

[[nodiscard]] std::unique_ptr<PersistentCpuCandidatePoolPreparer> RequirePreparer() {
  PersistentCpuCandidatePoolPreparerResult result =
      CreatePersistentCpuCandidatePoolPreparer({.worker_count = 4});
  EXPECT_TRUE(std::holds_alternative<std::unique_ptr<PersistentCpuCandidatePoolPreparer>>(result));
  if (!std::holds_alternative<std::unique_ptr<PersistentCpuCandidatePoolPreparer>>(result)) {
    std::abort();
  }
  return std::get<std::unique_ptr<PersistentCpuCandidatePoolPreparer>>(std::move(result));
}

[[nodiscard]] CpuCandidatePoolPreparationConfig CorpusConfig(std::uint64_t net_count,
                                                             std::uint32_t pool_size) {
  const std::uint64_t columns = net_count * pool_size;
  CpuCandidatePoolPreparationConfig config;
  config.requested_candidates_per_net = pool_size;
  config.deterministic_seed = 0xa911'0ca1ULL;
  config.limits.maximum_generated_candidate_bytes = 128ULL * 1024ULL * 1024ULL * 1024ULL;
  config.store_config = candidates::CandidateStoreConfig{
      .maximum_candidates_per_net = pool_size,
      .maximum_candidate_bytes_per_net = 32U * 1024U * 1024U,
      .maximum_rejection_records = columns,
      .maximum_rejection_items_per_transaction = columns,
      .maximum_admission_items_per_transaction = columns,
      .maximum_admission_input_bytes_per_transaction = 256U * 1024U * 1024U,
      .maximum_admission_work_units_per_transaction = 500'000'000,
      .maximum_pin_lease_items_per_transaction = columns,
      .maximum_expected_pools_per_invocation = net_count,
      .maximum_expected_candidates_per_invocation = columns,
  };
  return config;
}

TEST(CpuCandidatePoolPreparationCorpusTest,
     PreparesEveryFrozenExactFamilyThroughAuthenticAdmission) {
  std::unique_ptr<PersistentCpuCandidatePoolPreparer> preparer = RequirePreparer();
  for (const std::uint32_t case_id : {100U, 101U, 102U}) {
    const benchmark::Phase4RepresentativeCase corpus =
        RequireCase(benchmark::BuildPhase4RepresentativeCaseV1(case_id, {}));
    const CpuCandidatePoolPreparationConfig config = CorpusConfig(corpus.workload.nets().size(), 4);

    PreparedCpuCandidatePoolsResult prepared_result =
        PrepareInitialCpuCandidatePools(*preparer, corpus.board, corpus.workload, config);

    ASSERT_TRUE(std::holds_alternative<PreparedCpuCandidatePools>(prepared_result))
        << (std::holds_alternative<CpuCandidatePoolPreparationError>(prepared_result)
                ? std::get<CpuCandidatePoolPreparationError>(prepared_result).detail
                : "");
    const PreparedCpuCandidatePools& prepared =
        std::get<PreparedCpuCandidatePools>(prepared_result);
    const std::uint64_t net_count = corpus.workload.nets().size();
    const std::uint64_t unreachable = net_count - corpus.descriptor.declared_reachable_net_count;
    EXPECT_EQ(prepared.columns().size(), net_count * 4U);
    EXPECT_EQ(prepared.pools().size(), net_count);
    EXPECT_EQ(prepared.counters().requested_columns, net_count * 4U);
    EXPECT_EQ(prepared.counters().executed_route_queries,
              corpus.descriptor.declared_reachable_net_count * 4U + unreachable);
    EXPECT_EQ(prepared.counters().disconnected_proofs + prepared.counters().unsupported_proofs,
              unreachable);
    EXPECT_EQ(prepared.counters().skipped_columns, unreachable * 3U);
    for (const CandidatePool& pool : prepared.pools()) {
      const PreparedNetRoutingContext* context = corpus.workload.FindNet(pool.net);
      ASSERT_NE(context, nullptr);
      for (const candidates::StoredCandidate& candidate : pool.candidates) {
        ASSERT_NE(candidate, nullptr);
        EXPECT_EQ(candidate->net(), pool.net);
        EXPECT_EQ(candidate->data().associations,
                  candidates::AssociationsFor(corpus.board, context->compiled_board));
        EXPECT_TRUE(candidate->data().constraints.supported_hard_constraints_satisfied);
        EXPECT_EQ(candidate->data().constraints.exact_validation_code,
                  candidates::CandidateExactValidationCode::kPassed);
      }
    }
  }
  EXPECT_EQ(preparer->telemetry().workers_started, 4U);
  EXPECT_EQ(preparer->telemetry().invocations_completed, 3U);
}

void ExpectSemanticallyEqual(const PreparedCpuCandidatePools& left,
                             const PreparedCpuCandidatePools& right) {
  EXPECT_EQ(left.batch_identity(), right.batch_identity());
  EXPECT_EQ(left.counters(), right.counters());
  EXPECT_EQ(left.columns(), right.columns());
  EXPECT_EQ(left.preparation_checksum(), right.preparation_checksum());
  ASSERT_EQ(left.pools().size(), right.pools().size());
  for (std::size_t pool_index = 0; pool_index < left.pools().size(); ++pool_index) {
    ASSERT_EQ(left.pools()[pool_index].net, right.pools()[pool_index].net);
    ASSERT_EQ(left.pools()[pool_index].candidates.size(),
              right.pools()[pool_index].candidates.size());
    for (std::size_t candidate_index = 0;
         candidate_index < left.pools()[pool_index].candidates.size(); ++candidate_index) {
      EXPECT_EQ(*left.pools()[pool_index].candidates[candidate_index],
                *right.pools()[pool_index].candidates[candidate_index]);
    }
  }
}

TEST(CpuCandidatePoolPreparationCorpusTest,
     PreparesRepresentativeCalibrationAtFourEightAndSixteen) {
  const benchmark::Phase4RepresentativeCase corpus =
      RequireCase(benchmark::BuildPhase4RepresentativeCaseV1(200, {}));
  std::unique_ptr<PersistentCpuCandidatePoolPreparer> parallel = RequirePreparer();
  for (const std::uint32_t pool_size : {4U, 8U, 16U}) {
    const CpuCandidatePoolPreparationConfig config =
        CorpusConfig(corpus.workload.nets().size(), pool_size);
    PreparedCpuCandidatePoolsResult result =
        PrepareInitialCpuCandidatePools(*parallel, corpus.board, corpus.workload, config);
    ASSERT_TRUE(std::holds_alternative<PreparedCpuCandidatePools>(result))
        << (std::holds_alternative<CpuCandidatePoolPreparationError>(result)
                ? std::get<CpuCandidatePoolPreparationError>(result).detail
                : "");
    const PreparedCpuCandidatePools& prepared = std::get<PreparedCpuCandidatePools>(result);
    EXPECT_EQ(prepared.columns().size(), corpus.workload.nets().size() * pool_size);
    EXPECT_EQ(prepared.pools().size(), corpus.workload.nets().size());
    for (const CandidatePool& pool : prepared.pools()) {
      const PreparedNetRoutingContext* context = corpus.workload.FindNet(pool.net);
      ASSERT_NE(context, nullptr);
      for (const candidates::StoredCandidate& candidate : pool.candidates) {
        EXPECT_EQ(candidate->data().associations,
                  candidates::AssociationsFor(corpus.board, context->compiled_board));
        EXPECT_EQ(candidate->data().constraints.exact_validation_code,
                  candidates::CandidateExactValidationCode::kPassed);
      }
    }
    if (pool_size == 8) {
      PersistentCpuCandidatePoolPreparerResult serial_result =
          CreatePersistentCpuCandidatePoolPreparer({.worker_count = 1});
      ASSERT_TRUE(std::holds_alternative<std::unique_ptr<PersistentCpuCandidatePoolPreparer>>(
          serial_result));
      std::unique_ptr<PersistentCpuCandidatePoolPreparer> serial =
          std::get<std::unique_ptr<PersistentCpuCandidatePoolPreparer>>(std::move(serial_result));
      PreparedCpuCandidatePoolsResult serial_prepared =
          PrepareInitialCpuCandidatePools(*serial, corpus.board, corpus.workload, config);
      ASSERT_TRUE(std::holds_alternative<PreparedCpuCandidatePools>(serial_prepared));
      ExpectSemanticallyEqual(prepared, std::get<PreparedCpuCandidatePools>(serial_prepared));
    }
  }
}

}  // namespace
}  // namespace apgar::allocator
