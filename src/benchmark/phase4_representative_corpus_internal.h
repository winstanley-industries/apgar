#ifndef APGAR_SRC_BENCHMARK_PHASE4_REPRESENTATIVE_CORPUS_INTERNAL_H_
#define APGAR_SRC_BENCHMARK_PHASE4_REPRESENTATIVE_CORPUS_INTERNAL_H_

#include <cstdint>

namespace apgar::benchmark::internal {

enum class Phase4RepresentativeCorpusFaultForTesting : std::uint8_t {
  kNone = 0,
  kBadAlloc = 1,
  kLengthError = 2,
};

void SetPhase4RepresentativeCorpusFaultForTesting(
    Phase4RepresentativeCorpusFaultForTesting fault) noexcept;

}  // namespace apgar::benchmark::internal

#endif  // APGAR_SRC_BENCHMARK_PHASE4_REPRESENTATIVE_CORPUS_INTERNAL_H_
