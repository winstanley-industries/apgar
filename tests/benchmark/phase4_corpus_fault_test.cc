#include <string>
#include <variant>

#include "apgar/benchmark/phase4_corpus.h"
#include "apgar/tooling/runfiles.h"
#include "src/adapters/kicad_fixture_internal.h"
#include "src/allocator/multi_net_workload_internal.h"
#include "src/benchmark/phase4_corpus_internal.h"
#include "tests/support/google_test.h"

namespace apgar::benchmark {
namespace {

TEST(Phase4CorpusFaultTest, ReturnsAllocationFreeFallbackDuringCorpusBuild) {
  const std::string contents =
      tooling::ReadRunfile("tests/fixtures/phase4_supported_multinet_v1.kicad_pcb")
          .value_or(std::string{});
  ASSERT_FALSE(contents.empty());

  internal::FailNextPhase4CorpusBuildForTesting();
  Phase4ImportedMultiNetCorpusResult result = BuildPhase4ImportedMultiNetCorpusV1(contents);

  ASSERT_TRUE(std::holds_alternative<Phase4CorpusError>(result));
  const Phase4CorpusError& error = std::get<Phase4CorpusError>(result);
  EXPECT_EQ(error.code, Phase4CorpusErrorCode::kResourceExhausted);
  EXPECT_TRUE(error.detail.empty());
  EXPECT_EQ(error.invariant_id, "benchmark.phase4_corpus.host_memory.v1");
}

TEST(Phase4CorpusFaultTest, PropagatesNestedAdapterAndWorkloadExhaustion) {
  const std::string contents =
      tooling::ReadRunfile("tests/fixtures/phase4_supported_multinet_v1.kicad_pcb")
          .value_or(std::string{});
  ASSERT_FALSE(contents.empty());

  adapters::internal::SetKicadFixtureFaultPointForTesting(
      adapters::internal::KicadFixtureFaultPoint::kMultiNetParse);
  Phase4ImportedMultiNetCorpusResult adapter_result = BuildPhase4ImportedMultiNetCorpusV1(contents);
  ASSERT_TRUE(std::holds_alternative<Phase4CorpusError>(adapter_result));
  const Phase4CorpusError& adapter_error = std::get<Phase4CorpusError>(adapter_result);
  EXPECT_EQ(adapter_error.code, Phase4CorpusErrorCode::kResourceExhausted);
  EXPECT_TRUE(adapter_error.detail.empty());
  EXPECT_EQ(adapter_error.invariant_id, "adapter.kicad.import.host_memory.v1");

  allocator::internal::SetMultiNetWorkloadFaultForTesting(
      allocator::internal::MultiNetWorkloadFaultForTesting::kBadAlloc);
  Phase4ImportedMultiNetCorpusResult workload_result =
      BuildPhase4ImportedMultiNetCorpusV1(contents);
  ASSERT_TRUE(std::holds_alternative<Phase4CorpusError>(workload_result));
  const Phase4CorpusError& workload_error = std::get<Phase4CorpusError>(workload_result);
  EXPECT_EQ(workload_error.code, Phase4CorpusErrorCode::kResourceExhausted);
  EXPECT_TRUE(workload_error.detail.empty());
  EXPECT_EQ(workload_error.invariant_id, "allocator.workload.host_memory_exhausted.v1");
}

}  // namespace
}  // namespace apgar::benchmark
