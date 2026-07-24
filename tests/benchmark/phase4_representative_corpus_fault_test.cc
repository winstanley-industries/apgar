#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include "apgar/benchmark/phase4_representative_corpus.h"
#include "apgar/tooling/runfiles.h"
#include "src/allocator/multi_net_workload_internal.h"
#include "src/benchmark/phase4_corpus_internal.h"
#include "src/benchmark/phase4_representative_corpus_internal.h"
#include "tests/support/google_test.h"

namespace apgar::benchmark {
namespace {

TEST(Phase4RepresentativeCorpusFaultTest, ConvertsInjectedHostFailuresAndRecovers) {
  for (const auto [fault, invariant] : {
           std::pair{internal::Phase4RepresentativeCorpusFaultForTesting::kBadAlloc,
                     std::string_view("benchmark.phase4_representative.host_memory.v1")},
           std::pair{internal::Phase4RepresentativeCorpusFaultForTesting::kLengthError,
                     std::string_view("benchmark.phase4_representative.host_container.v1")},
       }) {
    internal::SetPhase4RepresentativeCorpusFaultForTesting(fault);
    const Phase4RepresentativeCaseResult rejected = BuildPhase4RepresentativeCaseV1(100, {});
    ASSERT_TRUE(std::holds_alternative<Phase4RepresentativeCorpusError>(rejected));
    const Phase4RepresentativeCorpusError& error =
        std::get<Phase4RepresentativeCorpusError>(rejected);
    EXPECT_EQ(error.code, Phase4RepresentativeCorpusErrorCode::kResourceExhausted);
    EXPECT_EQ(error.invariant_id, invariant);

    const Phase4RepresentativeCaseResult recovered = BuildPhase4RepresentativeCaseV1(100, {});
    EXPECT_TRUE(std::holds_alternative<Phase4RepresentativeCase>(recovered));
  }
}

TEST(Phase4RepresentativeCorpusFaultTest, ConvertsInjectedV2HostFailuresAndRecovers) {
  for (const auto [fault, invariant] : {
           std::pair{internal::Phase4RepresentativeCorpusFaultForTesting::kBadAlloc,
                     std::string_view("benchmark.phase4_representative.host_memory.v2")},
           std::pair{internal::Phase4RepresentativeCorpusFaultForTesting::kLengthError,
                     std::string_view("benchmark.phase4_representative.host_container.v2")},
       }) {
    internal::SetPhase4RepresentativeCorpusFaultForTesting(fault);
    const Phase4RepresentativeCaseResult rejected = BuildPhase4RepresentativeCaseV2(10'100, {});
    ASSERT_TRUE(std::holds_alternative<Phase4RepresentativeCorpusError>(rejected));
    const Phase4RepresentativeCorpusError& error =
        std::get<Phase4RepresentativeCorpusError>(rejected);
    EXPECT_EQ(error.code, Phase4RepresentativeCorpusErrorCode::kResourceExhausted);
    EXPECT_EQ(error.invariant_id, invariant);

    EXPECT_TRUE(std::holds_alternative<Phase4RepresentativeCase>(
        BuildPhase4RepresentativeCaseV2(10'100, {})));
  }
}

TEST(Phase4RepresentativeCorpusFaultTest, PreservesNestedStaticResourceInvariantIds) {
  allocator::internal::SetMultiNetWorkloadFaultForTesting(
      allocator::internal::MultiNetWorkloadFaultForTesting::kBadAlloc);
  const Phase4RepresentativeCaseResult workload_rejected = BuildPhase4RepresentativeCaseV1(100, {});
  ASSERT_TRUE(std::holds_alternative<Phase4RepresentativeCorpusError>(workload_rejected));
  EXPECT_EQ(std::get<Phase4RepresentativeCorpusError>(workload_rejected).code,
            Phase4RepresentativeCorpusErrorCode::kResourceExhausted);
  EXPECT_EQ(std::get<Phase4RepresentativeCorpusError>(workload_rejected).invariant_id,
            "allocator.workload.host_memory_exhausted.v1");

  const std::string fixture =
      tooling::ReadRunfile("tests/fixtures/phase4_supported_multinet_v1.kicad_pcb")
          .value_or(std::string{});
  ASSERT_FALSE(fixture.empty());
  internal::FailNextPhase4CorpusBuildForTesting();
  const Phase4RepresentativeCaseResult imported_rejected =
      BuildPhase4RepresentativeCaseV1(4'000, fixture);
  ASSERT_TRUE(std::holds_alternative<Phase4RepresentativeCorpusError>(imported_rejected));
  EXPECT_EQ(std::get<Phase4RepresentativeCorpusError>(imported_rejected).code,
            Phase4RepresentativeCorpusErrorCode::kResourceExhausted);
  EXPECT_EQ(std::get<Phase4RepresentativeCorpusError>(imported_rejected).invariant_id,
            "benchmark.phase4_corpus.host_memory.v1");

  allocator::internal::SetMultiNetWorkloadFaultForTesting(
      allocator::internal::MultiNetWorkloadFaultForTesting::kBadAlloc);
  const Phase4RepresentativeCaseResult v2_workload_rejected =
      BuildPhase4RepresentativeCaseV2(10'100, {});
  ASSERT_TRUE(std::holds_alternative<Phase4RepresentativeCorpusError>(v2_workload_rejected));
  EXPECT_EQ(std::get<Phase4RepresentativeCorpusError>(v2_workload_rejected).invariant_id,
            "allocator.workload.host_memory_exhausted.v1");

  internal::FailNextPhase4CorpusBuildForTesting();
  const Phase4RepresentativeCaseResult v2_imported_rejected =
      BuildPhase4RepresentativeCaseV2(14'000, fixture);
  ASSERT_TRUE(std::holds_alternative<Phase4RepresentativeCorpusError>(v2_imported_rejected));
  EXPECT_EQ(std::get<Phase4RepresentativeCorpusError>(v2_imported_rejected).invariant_id,
            "benchmark.phase4_corpus.host_memory.v1");
}

}  // namespace
}  // namespace apgar::benchmark
