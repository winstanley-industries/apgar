#ifndef APGAR_SRC_BENCHMARK_PHASE4_CORPUS_INTERNAL_H_
#define APGAR_SRC_BENCHMARK_PHASE4_CORPUS_INTERNAL_H_

namespace apgar::benchmark::internal {

// Test-only one-shot host-allocation failure injection. Production builds keep
// this source-private seam inert.
void FailNextPhase4CorpusBuildForTesting() noexcept;

}  // namespace apgar::benchmark::internal

#endif  // APGAR_SRC_BENCHMARK_PHASE4_CORPUS_INTERNAL_H_
