#ifndef APGAR_ADAPTERS_KICAD_FIXTURE_H_
#define APGAR_ADAPTERS_KICAD_FIXTURE_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>

#include "apgar/board_ir/board.h"

namespace apgar::adapters {

inline constexpr std::size_t kKicadFixtureMaximumInputBytes = 4U * 1024U * 1024U;
inline constexpr std::size_t kKicadFixtureMaximumTokenBytes = 4096U;
inline constexpr board_ir::DbCoord kKicadFixtureDbuPerMillimeter = 2'000'000;

struct KicadFixtureImportConfig {
  std::string target_net_name;
  // Already-quantized Board IR database units. Configuration is not fixture
  // syntax and therefore does not pass through the KiCad decimal parser.
  board_ir::DbCoord nominal_width;
  board_ir::DbCoord clearance;
};

enum class KicadFixtureErrorCode : std::uint8_t {
  kInvalidSyntax,
  kUnsupportedConstruct,
  kInvalidSemantics,
  kResourceLimit,
  kBoardValidationFailed,
};

struct KicadFixtureError {
  KicadFixtureErrorCode code;
  std::size_t offset;
  std::string message;
};

using KicadFixtureImportResult = std::variant<board_ir::BoardSnapshot, KicadFixtureError>;

// Imports the deliberately strict KiCad M1 fixture profile. The profile accepts
// two or four signal layers and UUID-bearing rectangular unrotated pads, and no
// existing tracks, vias, zones, or rule areas. Unsupported constructs, invalid
// UTF-8, non-KiCad quoting, and resource-limit violations are errors.
[[nodiscard]] KicadFixtureImportResult ImportKicadFixture(std::string_view contents,
                                                          const KicadFixtureImportConfig& config);

}  // namespace apgar::adapters

#endif  // APGAR_ADAPTERS_KICAD_FIXTURE_H_
