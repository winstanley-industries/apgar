#ifndef APGAR_TOOLS_BENCHMARK_SUPPORT_H_
#define APGAR_TOOLS_BENCHMARK_SUPPORT_H_

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "apgar/board_ir/board.h"
#include "apgar/routing/planar_route.h"

namespace apgar::benchmark::tool_support {

struct ExtractedArgument {
  std::optional<std::string> value;
  bool duplicate = false;
};

[[nodiscard]] ExtractedArgument ExtractSingleArgument(int* argc, char** argv,
                                                      std::string_view prefix);

[[nodiscard]] std::uint64_t PlanarGeometryFingerprint(
    std::string_view domain, std::span<const board_ir::Point64> path,
    std::span<const routing::LayerSegment> segments);

[[nodiscard]] std::string JoinUnsignedDecimal(std::span<const std::uint32_t> values);

// Adds the host kernel, architecture, OS, and CPU-model context shared by the
// Phase 2 and Phase 3 Google Benchmark executables.
void AddHostContext();

}  // namespace apgar::benchmark::tool_support

#endif  // APGAR_TOOLS_BENCHMARK_SUPPORT_H_
