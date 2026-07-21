#ifndef APGAR_ADAPTERS_KICAD_FIXTURE_H_
#define APGAR_ADAPTERS_KICAD_FIXTURE_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "apgar/board_ir/board.h"

namespace apgar::adapters {

inline constexpr std::size_t kKicadFixtureMaximumInputBytes = 4U * 1024U * 1024U;
inline constexpr std::size_t kKicadFixtureMaximumTokenBytes = 4096U;
inline constexpr std::size_t kKicadFixtureMaximumRoutableNets = 25'000U;
inline constexpr std::size_t kKicadFixtureMaximumRoutableNetNameBytes =
    kKicadFixtureMaximumInputBytes;
inline constexpr board_ir::DbCoord kKicadFixtureDbuPerMillimeter = 2'000'000;

struct KicadFixtureImportConfig {
  std::string target_net_name;
  // Already-quantized Board IR database units. Configuration is not fixture
  // syntax and therefore does not pass through the KiCad decimal parser.
  board_ir::DbCoord nominal_width;
  board_ir::DbCoord clearance;
};

// Opt-in Phase 4 fixture profile for importing more than one authentic
// two-terminal net from the same supported-rule board. The roster is complete,
// bounded, order-insensitive, and must contain default_routing_net_name.
// Roster pads remain owner-aware obstacles in addition to becoming terminals,
// so another routed net cannot observe their copper as false-free space.
struct KicadMultiNetFixtureImportConfig {
  std::vector<std::string> routable_net_names;
  std::string default_routing_net_name;
  // Already-quantized Board IR database units, shared by every roster net.
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
  // Static allocation-free fallback identity. This is populated when host
  // exhaustion prevents construction of an owning diagnostic message.
  std::string_view invariant_id = {};
};

using KicadFixtureImportResult = std::variant<board_ir::BoardSnapshot, KicadFixtureError>;

// Imports the deliberately strict KiCad M1 fixture profile. The profile accepts
// two or four signal layers and UUID-bearing rectangular unrotated pads, and no
// existing tracks, vias, zones, or rule areas. Unsupported constructs, invalid
// UTF-8, non-KiCad quoting, and resource-limit violations are errors.
[[nodiscard]] KicadFixtureImportResult ImportKicadFixture(std::string_view contents,
                                                          const KicadFixtureImportConfig& config);

// Imports the same deliberately strict KiCad syntax as ImportKicadFixture, but
// promotes every net in the explicit roster to an authentic terminal-bearing
// Board IR net. This is not a general KiCad importer and does not infer per-net
// endpoint layers or rule overrides.
[[nodiscard]] KicadFixtureImportResult ImportKicadMultiNetFixture(
    std::string_view contents, const KicadMultiNetFixtureImportConfig& config);

}  // namespace apgar::adapters

#endif  // APGAR_ADAPTERS_KICAD_FIXTURE_H_
