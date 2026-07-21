#ifndef APGAR_BENCHMARK_PHASE4_CORPUS_H_
#define APGAR_BENCHMARK_PHASE4_CORPUS_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>

#include "apgar/allocator/multi_net_workload.h"
#include "apgar/board_ir/board.h"

namespace apgar::benchmark {

inline constexpr std::uint32_t kPhase4ImportedMultiNetCorpusVersion = 1;
inline constexpr std::size_t kPhase4ImportedMultiNetFixtureBytesV1 = 1'781;
inline constexpr std::uint64_t kPhase4ImportedMultiNetFixtureFnv1a64V1 =
    8'729'721'683'359'945'012ULL;

struct Phase4ImportedMultiNetCorpus {
  board_ir::BoardSnapshot board;
  allocator::MultiNetWorkload workload;
};

enum class Phase4CorpusErrorCode : std::uint8_t {
  kFixtureIdentityMismatch,
  kFixtureImportFailed,
  kMissingRosterNet,
  kWorkloadBuildFailed,
  kResourceExhausted,
};

struct Phase4CorpusError {
  Phase4CorpusErrorCode code;
  std::string detail;
  // Static allocation-free fallback identity for host-exhaustion paths.
  std::string_view invariant_id = {};
};

using Phase4ImportedMultiNetCorpusResult =
    std::variant<Phase4ImportedMultiNetCorpus, Phase4CorpusError>;

// Builds the version-1 supported-rule imported Phase 4 seed. The fixture
// contents are supplied by the caller so tests and evidence tools consume a
// Bazel runfile or another explicitly authenticated source, never a host path.
[[nodiscard]] Phase4ImportedMultiNetCorpusResult BuildPhase4ImportedMultiNetCorpusV1(
    std::string_view kicad_fixture);

}  // namespace apgar::benchmark

#endif  // APGAR_BENCHMARK_PHASE4_CORPUS_H_
