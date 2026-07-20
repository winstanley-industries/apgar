#include "apgar/benchmark/phase3_commit.h"

#include <string>

#include "tests/support/google_test.h"

namespace apgar::benchmark {
namespace {

TEST(Phase3CommitTest, AcceptsExactlyFortyLowercaseHexCharacters) {
  EXPECT_TRUE(IsFullLowercaseGitCommit("0123456789abcdef0123456789abcdef01234567"));
  EXPECT_TRUE(IsFullLowercaseGitCommit("ffffffffffffffffffffffffffffffffffffffff"));
}

TEST(Phase3CommitTest, RejectsAbbreviatedOrExtendedCommit) {
  EXPECT_FALSE(IsFullLowercaseGitCommit("0123456789abcdef0123456789abcdef0123456"));
  EXPECT_FALSE(IsFullLowercaseGitCommit("0123456789abcdef0123456789abcdef012345678"));
  EXPECT_FALSE(IsFullLowercaseGitCommit(""));
}

TEST(Phase3CommitTest, RejectsUppercaseAndNonHexCharacters) {
  std::string uppercase = "0123456789abcdef0123456789abcdef01234567";
  uppercase.front() = 'A';
  EXPECT_FALSE(IsFullLowercaseGitCommit(uppercase));

  std::string non_hex = "0123456789abcdef0123456789abcdef01234567";
  non_hex.back() = 'g';
  EXPECT_FALSE(IsFullLowercaseGitCommit(non_hex));
}

TEST(Phase3CommitTest, RequiresRuntimeEvidenceLabelToMatchBuiltSource) {
  constexpr std::string_view kBuiltCommit = "0123456789abcdef0123456789abcdef01234567";
  EXPECT_TRUE(CommitMatchesBuiltSource(kBuiltCommit, kBuiltCommit));
  EXPECT_FALSE(CommitMatchesBuiltSource("1123456789abcdef0123456789abcdef01234567", kBuiltCommit));
  EXPECT_FALSE(CommitMatchesBuiltSource("01234567", kBuiltCommit));
  EXPECT_FALSE(CommitMatchesBuiltSource(kBuiltCommit, "01234567"));
}

TEST(Phase3CommitTest, PublishableSourceMustBeStampedCleanAndMatching) {
  constexpr std::string_view kBuiltCommit = "0123456789abcdef0123456789abcdef01234567";
  EXPECT_TRUE(IsPublishableBenchmarkSource(kBuiltCommit, kBuiltCommit, true, false));
  EXPECT_FALSE(IsPublishableBenchmarkSource(kBuiltCommit, kBuiltCommit, false, false));
  EXPECT_FALSE(IsPublishableBenchmarkSource(kBuiltCommit, kBuiltCommit, true, true));
  EXPECT_FALSE(IsPublishableBenchmarkSource("1123456789abcdef0123456789abcdef01234567",
                                            kBuiltCommit, true, false));
}

TEST(Phase3CommitTest, ValidatesOperatorRecordedDriverVersionLabels) {
  EXPECT_TRUE(IsDriverVersionLabel("610.62"));
  EXPECT_TRUE(IsDriverVersionLabel("1"));
  EXPECT_FALSE(IsDriverVersionLabel(""));
  EXPECT_FALSE(IsDriverVersionLabel(".610"));
  EXPECT_FALSE(IsDriverVersionLabel("610."));
  EXPECT_FALSE(IsDriverVersionLabel("610..62"));
  EXPECT_FALSE(IsDriverVersionLabel("driver-610.62"));
}

}  // namespace
}  // namespace apgar::benchmark
