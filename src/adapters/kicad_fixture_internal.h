#ifndef APGAR_SRC_ADAPTERS_KICAD_FIXTURE_INTERNAL_H_
#define APGAR_SRC_ADAPTERS_KICAD_FIXTURE_INTERNAL_H_

#include <cstdint>

namespace apgar::adapters::internal {

enum class KicadFixtureFaultPoint : std::uint8_t {
  kNone,
  kMultiNetPreflight,
  kMultiNetParse,
  kMultiNetImport,
};

// Test-only one-shot host-allocation failure injection. Production builds keep
// this source-private seam inert.
void SetKicadFixtureFaultPointForTesting(KicadFixtureFaultPoint point) noexcept;

}  // namespace apgar::adapters::internal

#endif  // APGAR_SRC_ADAPTERS_KICAD_FIXTURE_INTERNAL_H_
