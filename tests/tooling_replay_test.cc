#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "apgar/board_ir/stable_hash.h"
#include "apgar/tooling/replay.h"
#include "tests/support/google_test.h"

namespace apgar::tooling {
namespace {

constexpr std::array<std::string_view, 2> kKeys{{"format=", "count="}};

[[nodiscard]] std::string WithChecksum(std::string payload) {
  return payload + "checksum_fnv1a64=" + std::to_string(board_ir::StableHashString(payload)) + '\n';
}

TEST(CanonicalUnsignedDecimalTest, AcceptsOnlyCanonicalUnsignedGrammar) {
  std::uint64_t value = std::numeric_limits<std::uint64_t>::max();
  EXPECT_TRUE(ParseCanonicalUnsignedDecimal("0", &value));
  EXPECT_EQ(value, 0U);
  EXPECT_TRUE(ParseCanonicalUnsignedDecimal("18446744073709551615", &value));
  EXPECT_EQ(value, std::numeric_limits<std::uint64_t>::max());

  for (const std::string_view invalid : {"", "00", "01", "+1", "-1", " 1", "1 ", "1x"}) {
    value = 7;
    EXPECT_FALSE(ParseCanonicalUnsignedDecimal(invalid, &value)) << invalid;
    EXPECT_EQ(value, 7U) << invalid;
  }
  value = 7;
  EXPECT_FALSE(ParseCanonicalUnsignedDecimal("18446744073709551616", &value));
  EXPECT_EQ(value, 7U);
}

TEST(CanonicalUnsignedDecimalTest, EnforcesExplicitUnsignedDestinationWidths) {
  std::uint16_t value16 = 7;
  EXPECT_TRUE(ParseCanonicalUnsignedDecimal("65535", &value16));
  EXPECT_EQ(value16, std::numeric_limits<std::uint16_t>::max());
  value16 = 7;
  EXPECT_FALSE(ParseCanonicalUnsignedDecimal("65536", &value16));
  EXPECT_EQ(value16, 7U);

  std::uint32_t value32 = 7;
  EXPECT_TRUE(ParseCanonicalUnsignedDecimal("4294967295", &value32));
  EXPECT_EQ(value32, std::numeric_limits<std::uint32_t>::max());
  value32 = 7;
  EXPECT_FALSE(ParseCanonicalUnsignedDecimal("4294967296", &value32));
  EXPECT_EQ(value32, 7U);

  std::uint64_t value64 = 7;
  EXPECT_TRUE(ParseCanonicalUnsignedDecimal("18446744073709551615", &value64));
  EXPECT_EQ(value64, std::numeric_limits<std::uint64_t>::max());
  value64 = 7;
  EXPECT_FALSE(ParseCanonicalUnsignedDecimal("18446744073709551616", &value64));
  EXPECT_EQ(value64, 7U);
}

TEST(CanonicalReplayEnvelopeTest, ParsesExactOrderedFields) {
  const std::string replay = WithChecksum("format=example\ncount=7\n");
  std::string error;
  const std::optional<CanonicalReplayEnvelope> envelope =
      ParseCanonicalReplayEnvelope(replay, kKeys, &error);
  ASSERT_TRUE(envelope.has_value()) << error;
  ASSERT_EQ(envelope->values.size(), 2U);
  EXPECT_EQ(envelope->values[0], "example");
  EXPECT_EQ(envelope->values[1], "7");
}

TEST(CanonicalReplayEnvelopeTest, RejectsEmptyAndEmptyChecksum) {
  std::string error;
  EXPECT_FALSE(ParseCanonicalReplayEnvelope("", kKeys, &error).has_value());
  EXPECT_FALSE(
      ParseCanonicalReplayEnvelope("format=example\ncount=7\nchecksum_fnv1a64=\n", kKeys, &error)
          .has_value());
}

TEST(CanonicalReplayEnvelopeTest, RejectsNoncanonicalChecksumSpelling) {
  const std::string payload = "format=example\ncount=7\n";
  const std::string replay =
      payload + "checksum_fnv1a64=0" + std::to_string(board_ir::StableHashString(payload)) + '\n';
  std::string error;
  EXPECT_FALSE(ParseCanonicalReplayEnvelope(replay, kKeys, &error).has_value());
}

TEST(CanonicalReplayEnvelopeTest, RejectsReorderedAndUnknownFields) {
  std::string error;
  EXPECT_FALSE(
      ParseCanonicalReplayEnvelope(WithChecksum("count=7\nformat=example\n"), kKeys, &error)
          .has_value());
  EXPECT_FALSE(ParseCanonicalReplayEnvelope(
                   WithChecksum("format=example\ncount=7\nunknown=value\n"), kKeys, &error)
                   .has_value());
}

TEST(CanonicalReplayEnvelopeTest, RejectsInvalidUtf8NulAndCarriageReturn) {
  std::string error;

  std::string invalid_utf8 = "format=example";
  invalid_utf8.push_back(static_cast<char>(0xff));
  invalid_utf8 += "\ncount=7\n";
  EXPECT_FALSE(ParseCanonicalReplayEnvelope(WithChecksum(std::move(invalid_utf8)), kKeys, &error)
                   .has_value());

  std::string embedded_nul = "format=example";
  embedded_nul.push_back('\0');
  embedded_nul += "suffix\ncount=7\n";
  EXPECT_FALSE(ParseCanonicalReplayEnvelope(WithChecksum(std::move(embedded_nul)), kKeys, &error)
                   .has_value());

  EXPECT_FALSE(
      ParseCanonicalReplayEnvelope(WithChecksum("format=example\r\ncount=7\r\n"), kKeys, &error)
          .has_value());
}

}  // namespace
}  // namespace apgar::tooling
