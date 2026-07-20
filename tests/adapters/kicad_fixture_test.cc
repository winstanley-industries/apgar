#include "apgar/adapters/kicad_fixture.h"

#include <algorithm>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

#include "apgar/board_ir/board.h"
#include "apgar/geometry/exact.h"
#include "apgar/tooling/runfiles.h"
#include "tests/support/google_test.h"

namespace apgar::adapters {
namespace {

[[nodiscard]] std::string ReadFixture() {
  return tooling::ReadRunfile("tests/fixtures/m1_exactness.kicad_pcb").value_or(std::string{});
}

[[nodiscard]] KicadFixtureImportConfig Config() {
  return KicadFixtureImportConfig{
      .target_net_name = "TARGET",
      .nominal_width = 500'000,
      .clearance = 500'000,
  };
}

[[nodiscard]] std::optional<std::size_t> ExpressionEnd(std::string_view contents,
                                                       std::size_t start) {
  std::size_t depth = 0;
  bool quoted = false;
  bool escaped = false;
  for (std::size_t index = start; index < contents.size(); ++index) {
    const char character = contents[index];
    if (quoted) {
      if (escaped) {
        escaped = false;
      } else if (character == '\\') {
        escaped = true;
      } else if (character == '"') {
        quoted = false;
      }
      continue;
    }
    if (character == '"') {
      quoted = true;
    } else if (character == '(') {
      ++depth;
    } else if (character == ')') {
      if (depth == 0) {
        return std::nullopt;
      }
      --depth;
      if (depth == 0) {
        return index + 1;
      }
    }
  }
  return std::nullopt;
}

[[nodiscard]] std::string ReorderTargetFootprints(const std::string& contents) {
  const std::string first_marker = "(footprint \"APGAR:terminal-a\"";
  const std::string second_marker = "(footprint \"APGAR:terminal-b\"";
  const std::size_t first_start = contents.find(first_marker);
  const std::size_t second_start = contents.find(second_marker);
  if (first_start == std::string::npos || second_start == std::string::npos ||
      first_start >= second_start) {
    return {};
  }
  const std::optional<std::size_t> first_end = ExpressionEnd(contents, first_start);
  const std::optional<std::size_t> second_end = ExpressionEnd(contents, second_start);
  if (!first_end.has_value() || !second_end.has_value()) {
    return {};
  }
  return contents.substr(0, first_start) +
         contents.substr(second_start, *second_end - second_start) +
         contents.substr(*first_end, second_start - *first_end) +
         contents.substr(first_start, *first_end - first_start) + contents.substr(*second_end);
}

void ReplaceAll(std::string& contents, std::string_view needle, std::string_view replacement) {
  std::size_t position = 0;
  while ((position = contents.find(needle, position)) != std::string::npos) {
    contents.replace(position, needle.size(), replacement);
    position += replacement.size();
  }
}

[[nodiscard]] bool ReplaceOnce(std::string& contents, std::string_view needle,
                               std::string_view replacement) {
  const std::size_t position = contents.find(needle);
  if (position == std::string::npos) {
    return false;
  }
  contents.replace(position, needle.size(), replacement);
  return true;
}

[[nodiscard]] std::map<std::string, board_ir::EntityId> TerminalIds(
    const board_ir::BoardSnapshot& board) {
  std::map<std::string, board_ir::EntityId> ids;
  for (const board_ir::Terminal& terminal : board.data().terminals) {
    ids.emplace(terminal.component, terminal.ref.id);
  }
  return ids;
}

[[nodiscard]] std::map<std::string, board_ir::EntityId> ObstacleIds(
    const board_ir::BoardSnapshot& board) {
  std::map<std::string, board_ir::EntityId> ids;
  for (const board_ir::Obstacle& obstacle : board.data().obstacles) {
    ids.emplace(obstacle.provenance, obstacle.ref.id);
  }
  return ids;
}

TEST(KicadFixtureAdapterTest, ImportsAndNormalizesTheM1Microboard) {
  const std::string contents = ReadFixture();
  ASSERT_FALSE(contents.empty());

  KicadFixtureImportResult result = ImportKicadFixture(contents, Config());

  ASSERT_TRUE(std::holds_alternative<board_ir::BoardSnapshot>(result));
  const board_ir::BoardSnapshot& board = std::get<board_ir::BoardSnapshot>(result);
  EXPECT_EQ(board.data().schema_version, board_ir::kBoardSchemaVersion);
  EXPECT_EQ(board.data().dbu_per_millimeter, kKicadFixtureDbuPerMillimeter);
  EXPECT_EQ(board.data().layers.size(), 2U);
  EXPECT_EQ(board.data().terminals.size(), 2U);
  ASSERT_EQ(board.data().obstacles.size(), 1U);
  EXPECT_EQ(board.data().obstacles.front().provenance, "APGAR:blocker/pad-1");
  const auto terminal_a =
      std::ranges::find(board.data().terminals, "APGAR:terminal-a", &board_ir::Terminal::component);
  ASSERT_NE(terminal_a, board.data().terminals.end());
  EXPECT_EQ(terminal_a->center, (board_ir::Point64{.x = 2'000'000, .y = 10'000'000}));
}

TEST(KicadFixtureAdapterTest, ProducesAStableNormalizedFingerprint) {
  const std::string contents = ReadFixture();
  ASSERT_FALSE(contents.empty());

  KicadFixtureImportResult first = ImportKicadFixture(contents, Config());
  KicadFixtureImportResult second = ImportKicadFixture(contents, Config());

  ASSERT_TRUE(std::holds_alternative<board_ir::BoardSnapshot>(first));
  ASSERT_TRUE(std::holds_alternative<board_ir::BoardSnapshot>(second));
  EXPECT_EQ(std::get<board_ir::BoardSnapshot>(first).content_hash(),
            std::get<board_ir::BoardSnapshot>(second).content_hash());
}

TEST(KicadFixtureAdapterTest, PreservesEntityIdsAndFingerprintWhenFootprintsAreReordered) {
  const std::string contents = ReadFixture();
  const std::string reordered = ReorderTargetFootprints(contents);
  ASSERT_FALSE(contents.empty());
  ASSERT_FALSE(reordered.empty());

  KicadFixtureImportResult original_result = ImportKicadFixture(contents, Config());
  KicadFixtureImportResult reordered_result = ImportKicadFixture(reordered, Config());

  ASSERT_TRUE(std::holds_alternative<board_ir::BoardSnapshot>(original_result));
  ASSERT_TRUE(std::holds_alternative<board_ir::BoardSnapshot>(reordered_result));
  const board_ir::BoardSnapshot& original = std::get<board_ir::BoardSnapshot>(original_result);
  const board_ir::BoardSnapshot& reordered_board =
      std::get<board_ir::BoardSnapshot>(reordered_result);
  EXPECT_EQ(TerminalIds(original), TerminalIds(reordered_board));
  EXPECT_EQ(original.content_hash(), reordered_board.content_hash());
}

TEST(KicadFixtureAdapterTest, DerivesPhysicalLayerOrderFromKiCadIds) {
  const std::string contents = ReadFixture();
  std::string reordered = contents;
  ASSERT_FALSE(contents.empty());
  ASSERT_TRUE(ReplaceOnce(reordered, "    (0 \"F.Cu\" signal)\n    (31 \"B.Cu\" signal)",
                          "    (31 \"B.Cu\" signal)\n    (0 \"F.Cu\" signal)"));

  KicadFixtureImportResult original_result = ImportKicadFixture(contents, Config());
  KicadFixtureImportResult reordered_result = ImportKicadFixture(reordered, Config());

  ASSERT_TRUE(std::holds_alternative<board_ir::BoardSnapshot>(original_result));
  ASSERT_TRUE(std::holds_alternative<board_ir::BoardSnapshot>(reordered_result));
  const board_ir::BoardSnapshot& original = std::get<board_ir::BoardSnapshot>(original_result);
  const board_ir::BoardSnapshot& reordered_board =
      std::get<board_ir::BoardSnapshot>(reordered_result);
  EXPECT_EQ(original.data(), reordered_board.data());
  EXPECT_EQ(original.content_hash(), reordered_board.content_hash());
  ASSERT_EQ(reordered_board.data().layers.size(), 2U);
  EXPECT_EQ(reordered_board.data().layers[0].physical_order, 0);
  EXPECT_EQ(reordered_board.data().layers[1].physical_order, 1);
}

TEST(KicadFixtureAdapterTest, PreservesExistingEntityIdsWhenAnEarlierObstacleIsInserted) {
  const std::string contents = ReadFixture();
  ASSERT_FALSE(contents.empty());
  std::string with_extra_obstacle = contents;
  const std::size_t insertion = with_extra_obstacle.find("(footprint \"APGAR:blocker\"");
  ASSERT_NE(insertion, std::string::npos);
  with_extra_obstacle.insert(insertion,
                             "(footprint \"APGAR:extra-blocker\"\n"
                             "  (layer \"F.Cu\")\n"
                             "  (at 7 8)\n"
                             "  (uuid \"00000000-0000-4000-8000-000000000004\")\n"
                             "  (pad \"1\" smd rect\n"
                             "    (at 0 0)\n"
                             "    (size 1 1)\n"
                             "    (layers \"F.Cu\" \"F.Mask\" \"F.Paste\")\n"
                             "    (net 2 \"BLOCKER\")\n"
                             "    (uuid \"00000000-0000-4000-8000-000000000104\")\n"
                             "  )\n"
                             ")\n");

  KicadFixtureImportResult original_result = ImportKicadFixture(contents, Config());
  KicadFixtureImportResult inserted_result = ImportKicadFixture(with_extra_obstacle, Config());

  ASSERT_TRUE(std::holds_alternative<board_ir::BoardSnapshot>(original_result));
  ASSERT_TRUE(std::holds_alternative<board_ir::BoardSnapshot>(inserted_result));
  const board_ir::BoardSnapshot& original = std::get<board_ir::BoardSnapshot>(original_result);
  const board_ir::BoardSnapshot& inserted = std::get<board_ir::BoardSnapshot>(inserted_result);
  EXPECT_EQ(TerminalIds(original), TerminalIds(inserted));
  EXPECT_EQ(ObstacleIds(original).at("APGAR:blocker/pad-1"),
            ObstacleIds(inserted).at("APGAR:blocker/pad-1"));
  EXPECT_NE(original.content_hash(), inserted.content_hash());
}

TEST(KicadFixtureAdapterTest, FeedsImportedGeometryToTheExactOracle) {
  const std::string contents = ReadFixture();
  ASSERT_FALSE(contents.empty());
  KicadFixtureImportResult imported = ImportKicadFixture(contents, Config());
  ASSERT_TRUE(std::holds_alternative<board_ir::BoardSnapshot>(imported));
  const board_ir::BoardSnapshot& board = std::get<board_ir::BoardSnapshot>(imported);
  ASSERT_EQ(board.data().terminals.size(), 2U);
  const board_ir::Segment64 direct{
      .start = board.data().terminals[0].center,
      .end = board.data().terminals[1].center,
  };

  const geometry::MovementValidationResult front = geometry::ValidateMovement(board, 0, direct);
  const geometry::MovementValidationResult back = geometry::ValidateMovement(board, 31, direct);

  EXPECT_EQ(front.code, geometry::MovementViolationCode::kStaticObstacleConflict);
  EXPECT_TRUE(back.legal()) << back.detail;
}

TEST(KicadFixtureAdapterTest, RejectsUnsupportedCopperConstructsExplicitly) {
  std::string contents = ReadFixture();
  ASSERT_FALSE(contents.empty());
  const std::size_t root_end = contents.rfind(')');
  ASSERT_NE(root_end, std::string::npos);
  contents.insert(root_end,
                  "  (segment (start 1 5) (end 9 5) (width 0.25) "
                  "(layer \"F.Cu\") (net 1))\n");

  KicadFixtureImportResult result = ImportKicadFixture(contents, Config());

  ASSERT_TRUE(std::holds_alternative<KicadFixtureError>(result));
  const KicadFixtureError& error = std::get<KicadFixtureError>(result);
  EXPECT_EQ(error.code, KicadFixtureErrorCode::kUnsupportedConstruct);
  EXPECT_NE(error.message.find("segment"), std::string::npos);
}

TEST(KicadFixtureAdapterTest, RejectsNonCanonicalDecimalSpellings) {
  std::string trailing_decimal = ReadFixture();
  std::string leading_plus = ReadFixture();
  ASSERT_TRUE(ReplaceOnce(trailing_decimal, "(at 1 5)", "(at 1. 5)"));
  ASSERT_TRUE(ReplaceOnce(leading_plus, "(at 1 5)", "(at +1 5)"));

  KicadFixtureImportResult trailing_result = ImportKicadFixture(trailing_decimal, Config());
  KicadFixtureImportResult plus_result = ImportKicadFixture(leading_plus, Config());

  ASSERT_TRUE(std::holds_alternative<KicadFixtureError>(trailing_result));
  ASSERT_TRUE(std::holds_alternative<KicadFixtureError>(plus_result));
  EXPECT_EQ(std::get<KicadFixtureError>(trailing_result).code,
            KicadFixtureErrorCode::kInvalidSemantics);
  EXPECT_EQ(std::get<KicadFixtureError>(plus_result).code,
            KicadFixtureErrorCode::kInvalidSemantics);
}

TEST(KicadFixtureAdapterTest, RejectsInputAndTokensOverTheirResourceLimits) {
  const std::string oversized_input(kKicadFixtureMaximumInputBytes + 1, ' ');
  const std::string oversized_token =
      "(" + std::string(kKicadFixtureMaximumTokenBytes + 1, 'a') + ")";

  KicadFixtureImportResult input_result = ImportKicadFixture(oversized_input, Config());
  KicadFixtureImportResult token_result = ImportKicadFixture(oversized_token, Config());

  ASSERT_TRUE(std::holds_alternative<KicadFixtureError>(input_result));
  ASSERT_TRUE(std::holds_alternative<KicadFixtureError>(token_result));
  EXPECT_EQ(std::get<KicadFixtureError>(input_result).code, KicadFixtureErrorCode::kResourceLimit);
  EXPECT_EQ(std::get<KicadFixtureError>(token_result).code, KicadFixtureErrorCode::kResourceLimit);
}

TEST(KicadFixtureAdapterTest, RequiresQuotedKiCadStrings) {
  std::string contents = ReadFixture();
  ASSERT_FALSE(contents.empty());
  ReplaceAll(contents, "\"TARGET\"", "TARGET");

  KicadFixtureImportResult result = ImportKicadFixture(contents, Config());

  ASSERT_TRUE(std::holds_alternative<KicadFixtureError>(result));
  EXPECT_EQ(std::get<KicadFixtureError>(result).code, KicadFixtureErrorCode::kInvalidSemantics);
}

TEST(KicadFixtureAdapterTest, RejectsQuotesEmbeddedInKiCadSymbolsAtTheLexerBoundary) {
  std::string contents = ReadFixture();
  ASSERT_TRUE(ReplaceOnce(contents, "(generator apgar_fixture)", "(generator pcb\"new\")"));
  const std::size_t quote_offset = contents.find('"', contents.find("(generator"));
  ASSERT_NE(quote_offset, std::string::npos);

  KicadFixtureImportResult result = ImportKicadFixture(contents, Config());

  ASSERT_TRUE(std::holds_alternative<KicadFixtureError>(result));
  const KicadFixtureError& error = std::get<KicadFixtureError>(result);
  EXPECT_EQ(error.code, KicadFixtureErrorCode::kInvalidSyntax);
  EXPECT_EQ(error.offset, quote_offset);
}

TEST(KicadFixtureAdapterTest, RejectsDuplicateNetZeroDeclarations) {
  std::string contents = ReadFixture();
  ASSERT_TRUE(ReplaceOnce(contents, "  (net 0 \"\")", "  (net 0 \"\")\n  (net 0 \"\")"));

  KicadFixtureImportResult result = ImportKicadFixture(contents, Config());

  ASSERT_TRUE(std::holds_alternative<KicadFixtureError>(result));
  EXPECT_EQ(std::get<KicadFixtureError>(result).code, KicadFixtureErrorCode::kInvalidSemantics);
}

TEST(KicadFixtureAdapterTest, ReportsDuplicateLayerIdsAtTheOffendingLayer) {
  std::string contents = ReadFixture();
  ASSERT_TRUE(ReplaceOnce(contents, "(31 \"B.Cu\" signal)", "(0 \"B.Cu\" signal)"));
  const std::size_t duplicate_offset = contents.find("(0 \"B.Cu\" signal)");
  ASSERT_NE(duplicate_offset, std::string::npos);

  KicadFixtureImportResult result = ImportKicadFixture(contents, Config());

  ASSERT_TRUE(std::holds_alternative<KicadFixtureError>(result));
  const KicadFixtureError& error = std::get<KicadFixtureError>(result);
  EXPECT_EQ(error.code, KicadFixtureErrorCode::kInvalidSemantics);
  EXPECT_EQ(error.offset, duplicate_offset);
  EXPECT_NE(error.message.find("unique"), std::string::npos);
}

TEST(KicadFixtureAdapterTest, ReportsPadExtentsOutsideTheCoordinateEnvelopeAtThePad) {
  std::string contents = ReadFixture();
  ASSERT_TRUE(ReplaceOnce(contents, "(at 1 5)", "(at 500000 5)"));
  ASSERT_TRUE(ReplaceOnce(contents, "(size 1 1)", "(size 500000 1)"));
  const std::size_t footprint = contents.find("(footprint \"APGAR:terminal-a\"");
  const std::size_t pad_offset = contents.find("(pad \"1\"", footprint);
  ASSERT_NE(pad_offset, std::string::npos);

  KicadFixtureImportResult result = ImportKicadFixture(contents, Config());

  ASSERT_TRUE(std::holds_alternative<KicadFixtureError>(result));
  const KicadFixtureError& error = std::get<KicadFixtureError>(result);
  EXPECT_EQ(error.code, KicadFixtureErrorCode::kInvalidSemantics);
  EXPECT_EQ(error.offset, pad_offset);
  EXPECT_NE(error.message.find("APGAR:terminal-a/pad-1"), std::string::npos);
}

TEST(KicadFixtureAdapterTest, RejectsWrongSideAndUnknownSmdAuxiliaryLayers) {
  std::string wrong_side = ReadFixture();
  std::string unknown_side = ReadFixture();
  ASSERT_TRUE(ReplaceOnce(wrong_side, "(layers \"F.Cu\" \"F.Mask\" \"F.Paste\")",
                          "(layers \"F.Cu\" \"B.Mask\" \"B.Paste\")"));
  ASSERT_TRUE(ReplaceOnce(unknown_side, "(layers \"F.Cu\" \"F.Mask\" \"F.Paste\")",
                          "(layers \"F.Cu\" \"Garbage.Mask\")"));

  KicadFixtureImportResult wrong_result = ImportKicadFixture(wrong_side, Config());
  KicadFixtureImportResult unknown_result = ImportKicadFixture(unknown_side, Config());

  ASSERT_TRUE(std::holds_alternative<KicadFixtureError>(wrong_result));
  ASSERT_TRUE(std::holds_alternative<KicadFixtureError>(unknown_result));
  EXPECT_EQ(std::get<KicadFixtureError>(wrong_result).code,
            KicadFixtureErrorCode::kUnsupportedConstruct);
  EXPECT_EQ(std::get<KicadFixtureError>(unknown_result).code,
            KicadFixtureErrorCode::kUnsupportedConstruct);
}

TEST(KicadFixtureAdapterTest, RejectsInvalidUtf8BeforeBoardPublication) {
  std::string contents = ReadFixture();
  ASSERT_FALSE(contents.empty());
  const std::size_t blocker = contents.find("BLOCKER");
  ASSERT_NE(blocker, std::string::npos);
  contents.insert(blocker + 1, 1, static_cast<char>(0xff));

  KicadFixtureImportResult result = ImportKicadFixture(contents, Config());

  ASSERT_TRUE(std::holds_alternative<KicadFixtureError>(result));
  EXPECT_EQ(std::get<KicadFixtureError>(result).code, KicadFixtureErrorCode::kInvalidSyntax);
}

TEST(KicadFixtureAdapterTest, ValidatesTheShapeOfAcceptedMetadata) {
  std::string contents = ReadFixture();
  ASSERT_FALSE(contents.empty());
  ReplaceAll(contents, "(paper \"A4\")", "(paper \"A4\" extra)");

  KicadFixtureImportResult result = ImportKicadFixture(contents, Config());

  ASSERT_TRUE(std::holds_alternative<KicadFixtureError>(result));
  EXPECT_EQ(std::get<KicadFixtureError>(result).code, KicadFixtureErrorCode::kInvalidSemantics);
}

TEST(KicadFixtureAdapterTest, RejectsDuplicatePersistentObjectUuids) {
  std::string contents = ReadFixture();
  ASSERT_FALSE(contents.empty());
  ReplaceAll(contents, "00000000-0000-4000-8000-000000000102",
             "00000000-0000-4000-8000-000000000101");

  KicadFixtureImportResult result = ImportKicadFixture(contents, Config());

  ASSERT_TRUE(std::holds_alternative<KicadFixtureError>(result));
  EXPECT_EQ(std::get<KicadFixtureError>(result).code, KicadFixtureErrorCode::kInvalidSemantics);
}

TEST(KicadFixtureAdapterTest, RejectsMalformedSExpressions) {
  KicadFixtureImportResult result =
      ImportKicadFixture("(kicad_pcb (version 20240108) (generator \"unterminated)", Config());

  ASSERT_TRUE(std::holds_alternative<KicadFixtureError>(result));
  EXPECT_EQ(std::get<KicadFixtureError>(result).code, KicadFixtureErrorCode::kInvalidSyntax);
}

}  // namespace
}  // namespace apgar::adapters
