#include <array>
#include <string>
#include <variant>

#include "apgar/adapters/kicad_fixture.h"
#include "apgar/tooling/runfiles.h"
#include "src/adapters/kicad_fixture_internal.h"
#include "tests/support/google_test.h"

namespace apgar::adapters {
namespace {

TEST(KicadFixtureFaultTest, ReturnsAllocationFreeFallbackAcrossMultiNetStages) {
  const std::string contents =
      tooling::ReadRunfile("tests/fixtures/phase4_supported_multinet_v1.kicad_pcb")
          .value_or(std::string{});
  ASSERT_FALSE(contents.empty());
  const KicadMultiNetFixtureImportConfig config{
      .routable_net_names = {"ROUTE_A", "ROUTE_B"},
      .default_routing_net_name = "ROUTE_A",
      .nominal_width = 500'000,
      .clearance = 500'000,
  };
  constexpr std::array fault_points = {
      internal::KicadFixtureFaultPoint::kMultiNetPreflight,
      internal::KicadFixtureFaultPoint::kMultiNetParse,
      internal::KicadFixtureFaultPoint::kMultiNetImport,
  };

  for (internal::KicadFixtureFaultPoint point : fault_points) {
    internal::SetKicadFixtureFaultPointForTesting(point);
    KicadFixtureImportResult result = ImportKicadMultiNetFixture(contents, config);
    ASSERT_TRUE(std::holds_alternative<KicadFixtureError>(result));
    const KicadFixtureError& error = std::get<KicadFixtureError>(result);
    EXPECT_EQ(error.code, KicadFixtureErrorCode::kResourceLimit);
    EXPECT_TRUE(error.message.empty());
    EXPECT_EQ(error.invariant_id, "adapter.kicad.import.host_memory.v1");
  }
}

}  // namespace
}  // namespace apgar::adapters
