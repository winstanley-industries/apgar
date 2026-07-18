#include "apgar/version.h"

#include "tests/support/google_test.h"

namespace apgar {
namespace {

TEST(VersionTest, ReportsProjectName) { EXPECT_EQ(ProjectName(), "APGAR"); }

TEST(VersionTest, ReportsDevelopmentVersion) { EXPECT_EQ(Version(), "0.0.0-dev"); }

}  // namespace
}  // namespace apgar
