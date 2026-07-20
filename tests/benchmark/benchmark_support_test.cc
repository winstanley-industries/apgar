#include "tools/benchmark_support.h"

#include <array>
#include <cstdint>
#include <string>

#include "tests/support/google_test.h"

namespace apgar::benchmark::tool_support {
namespace {

constexpr std::array<board_ir::Point64, 3> kGeometryPath{{
    {.x = -5, .y = 7},
    {.x = 0, .y = 7},
    {.x = 4, .y = 11},
}};

constexpr std::array<routing::LayerSegment, 2> kGeometrySegments{{
    {.layer = 3, .centerline = {.start = {.x = -5, .y = 7}, .end = {.x = 0, .y = 7}}},
    {.layer = 3, .centerline = {.start = {.x = 0, .y = 7}, .end = {.x = 4, .y = 11}}},
}};

TEST(BenchmarkSupportTest, ExtractSingleArgumentRemovesOneValueAndPreservesOtherArguments) {
  std::array<std::string, 4> storage = {"benchmark", "--other=left",
                                        "--apgar_commit=0123456789abcdef", "--other=right"};
  std::array<char*, 4> argv{};
  for (std::size_t index = 0; index < storage.size(); ++index) {
    argv[index] = storage[index].data();
  }
  int argc = static_cast<int>(argv.size());

  const ExtractedArgument result = ExtractSingleArgument(&argc, argv.data(), "--apgar_commit=");

  ASSERT_TRUE(result.value.has_value());
  EXPECT_EQ(*result.value, "0123456789abcdef");
  EXPECT_FALSE(result.duplicate);
  ASSERT_EQ(argc, 3);
  EXPECT_STREQ(argv[1], "--other=left");
  EXPECT_STREQ(argv[2], "--other=right");
}

TEST(BenchmarkSupportTest, ExtractSingleArgumentReportsDuplicates) {
  std::array<std::string, 3> storage = {"benchmark", "--apgar_commit=first",
                                        "--apgar_commit=second"};
  std::array<char*, 3> argv{};
  for (std::size_t index = 0; index < storage.size(); ++index) {
    argv[index] = storage[index].data();
  }
  int argc = static_cast<int>(argv.size());

  const ExtractedArgument result = ExtractSingleArgument(&argc, argv.data(), "--apgar_commit=");

  ASSERT_TRUE(result.value.has_value());
  EXPECT_EQ(*result.value, "second");
  EXPECT_TRUE(result.duplicate);
  EXPECT_EQ(argc, 1);
}

TEST(BenchmarkSupportTest, PlanarGeometryFingerprintHasStableVersionedEncoding) {
  EXPECT_EQ(PlanarGeometryFingerprint("benchmark-fingerprint-v1", kGeometryPath, kGeometrySegments),
            17534922064806850111ULL);
}

TEST(BenchmarkSupportTest, PlanarGeometryFingerprintSeparatesDomainAndSequenceOrder) {
  const std::uint64_t canonical =
      PlanarGeometryFingerprint("benchmark-fingerprint-v1", kGeometryPath, kGeometrySegments);
  EXPECT_NE(PlanarGeometryFingerprint("benchmark-fingerprint-v2", kGeometryPath, kGeometrySegments),
            canonical);

  const std::array<board_ir::Point64, 3> reversed_path{{
      kGeometryPath[2],
      kGeometryPath[1],
      kGeometryPath[0],
  }};
  EXPECT_NE(PlanarGeometryFingerprint("benchmark-fingerprint-v1", reversed_path, kGeometrySegments),
            canonical);

  const std::array<routing::LayerSegment, 2> reversed_segments{{
      kGeometrySegments[1],
      kGeometrySegments[0],
  }};
  EXPECT_NE(PlanarGeometryFingerprint("benchmark-fingerprint-v1", kGeometryPath, reversed_segments),
            canonical);
}

TEST(BenchmarkSupportTest, JoinUnsignedDecimalPreservesPublishedCountOrder) {
  constexpr std::array<std::uint32_t, 6> kCounts{{4, 8, 16, 32, 64, 128}};
  EXPECT_EQ(JoinUnsignedDecimal(kCounts), "4,8,16,32,64,128");
  EXPECT_EQ(JoinUnsignedDecimal(std::span<const std::uint32_t>{}), "");
}

}  // namespace
}  // namespace apgar::benchmark::tool_support
