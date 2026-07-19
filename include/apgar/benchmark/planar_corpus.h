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

struct PlanarCorpusCase {
  std::string name;
  std::string family;
  board_ir::BoardSnapshot board;
  geometry_compiler::CompiledBoard compiled_board;
  routing::CpuRouteRequest request;
};

using PlanarCorpusResult = std::variant<std::vector<PlanarCorpusCase>, std::string>;

// Builds the fixed Phase 2 planar corpus. The KiCad contents are supplied by
// the caller so tests and benchmarks use Bazel runfiles rather than host paths.
[[nodiscard]] PlanarCorpusResult BuildPlanarBakeoffCorpus(std::string_view kicad_fixture);

}  // namespace apgar::benchmark

#endif  // APGAR_BENCHMARK_PLANAR_CORPUS_H_
