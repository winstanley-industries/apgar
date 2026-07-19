#ifndef APGAR_BENCHMARK_PLANAR_CORPUS_H_
#define APGAR_BENCHMARK_PLANAR_CORPUS_H_

#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "apgar/board_ir/board.h"
#include "apgar/geometry_compiler/compiled_board.h"
#include "apgar/routing/cpu_astar.h"

namespace apgar::benchmark {

inline constexpr std::uint32_t kPlanarBakeoffCorpusVersion = 1;
inline constexpr std::uint32_t kPhase3CandidateCorpusVersion = 1;

struct PlanarCorpusCase {
  std::string name;
  std::string family;
  board_ir::BoardSnapshot board;
  geometry_compiler::CompiledBoard compiled_board;
  routing::CpuRouteRequest request;
};

using PlanarCorpusCaseResult = std::variant<PlanarCorpusCase, std::string>;
using PlanarCorpusResult = std::variant<std::vector<PlanarCorpusCase>, std::string>;

// Builds the version-1 KiCad fixture case using the same import, profile, and
// request construction as the full bakeoff corpus. Replay tools consume this
// entry point so their pinned associations cannot drift from the corpus.
[[nodiscard]] PlanarCorpusCaseResult BuildPlanarBakeoffKicadCaseV1(std::string_view kicad_fixture);

// Builds the fixed Phase 2 planar corpus. The KiCad contents are supplied by
// the caller so tests and benchmarks use Bazel runfiles rather than host paths.
[[nodiscard]] PlanarCorpusResult BuildPlanarBakeoffCorpus(std::string_view kicad_fixture);

// Extends the exact Phase 2 corpus with symmetric dual-corridor,
// multi-channel/resource-bottleneck, and ban/penalty-driven alternative cases.
// Existing cases retain their version-1 identities and ordering.
[[nodiscard]] PlanarCorpusResult BuildPhase3CandidateCorpus(std::string_view kicad_fixture);

}  // namespace apgar::benchmark

#endif  // APGAR_BENCHMARK_PLANAR_CORPUS_H_
