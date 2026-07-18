#include "apgar/board_ir/board.h"

#include <algorithm>
#include <cstdint>
#include <variant>

#include "tests/support/board_builder.h"
#include "tests/support/google_test.h"

namespace apgar::board_ir {
namespace {

[[nodiscard]] BoardData ValidBoardData() { return test_support::ValidM1BoardData(); }

TEST(BoardSnapshotTest, NormalizesObjectTablesBeforeFingerprinting) {
  BoardData first = ValidBoardData();
  BoardData reordered = first;
  std::ranges::reverse(reordered.layers);
  std::ranges::reverse(reordered.nets);
  std::ranges::reverse(reordered.terminals);
  std::ranges::reverse(reordered.nets.back().terminals);
  std::ranges::reverse(reordered.routing_profile.allowed_layers);

  BoardCreationResult first_result = CreateBoardSnapshot(std::move(first));
  BoardCreationResult reordered_result = CreateBoardSnapshot(std::move(reordered));

  ASSERT_TRUE(std::holds_alternative<BoardSnapshot>(first_result));
  ASSERT_TRUE(std::holds_alternative<BoardSnapshot>(reordered_result));
  const BoardSnapshot& first_board = std::get<BoardSnapshot>(first_result);
  const BoardSnapshot& reordered_board = std::get<BoardSnapshot>(reordered_result);
  EXPECT_EQ(first_board.content_hash(), reordered_board.content_hash());
  EXPECT_EQ(first_board.data(), reordered_board.data());
  EXPECT_NE(first_board.content_hash(), 0U);
}

TEST(BoardSnapshotTest, RejectsEntityIdsReusedAcrossTables) {
  BoardData data = ValidBoardData();
  data.obstacles.front().ref = data.terminals.front().ref;

  BoardCreationResult result = CreateBoardSnapshot(std::move(data));

  ASSERT_TRUE(std::holds_alternative<BoardValidationError>(result));
  EXPECT_EQ(std::get<BoardValidationError>(result).code, BoardValidationCode::kDuplicateEntityId);
}

TEST(BoardSnapshotTest, RejectsStaleGenerationalReferences) {
  BoardData data = ValidBoardData();
  ++data.routing_profile.net.generation;

  BoardCreationResult result = CreateBoardSnapshot(std::move(data));

  ASSERT_TRUE(std::holds_alternative<BoardValidationError>(result));
  EXPECT_EQ(std::get<BoardValidationError>(result).code,
            BoardValidationCode::kInvalidRoutingProfile);
}

TEST(BoardSnapshotTest, RejectsCoordinatesOutsideTheExactArithmeticEnvelope) {
  BoardData data = ValidBoardData();
  data.obstacles.front().bounds.max.x = kMaxAbsDbCoord + 1;

  BoardCreationResult result = CreateBoardSnapshot(std::move(data));

  ASSERT_TRUE(std::holds_alternative<BoardValidationError>(result));
  EXPECT_EQ(std::get<BoardValidationError>(result).code, BoardValidationCode::kInvalidCoordinate);
}

TEST(BoardSnapshotTest, DistinguishesMalformedGeometryFromOutOfRangeCoordinates) {
  BoardData data = ValidBoardData();
  data.obstacles.front().bounds.min.x = data.obstacles.front().bounds.max.x + 1;

  BoardCreationResult result = CreateBoardSnapshot(std::move(data));

  ASSERT_TRUE(std::holds_alternative<BoardValidationError>(result));
  EXPECT_EQ(std::get<BoardValidationError>(result).code, BoardValidationCode::kInvalidGeometry);
}

TEST(BoardSnapshotTest, RequiresAdaptersToDeclareTheirUnitScale) {
  BoardData data = ValidBoardData();
  data.dbu_per_millimeter = 0;

  BoardCreationResult result = CreateBoardSnapshot(std::move(data));

  ASSERT_TRUE(std::holds_alternative<BoardValidationError>(result));
  EXPECT_EQ(std::get<BoardValidationError>(result).code, BoardValidationCode::kInvalidUnits);
}

TEST(BoardSnapshotTest, RequiresExactlyTwoTargetTerminalsForM1) {
  BoardData data = ValidBoardData();
  data.nets.front().terminals.pop_back();
  data.terminals.pop_back();

  BoardCreationResult result = CreateBoardSnapshot(std::move(data));

  ASSERT_TRUE(std::holds_alternative<BoardValidationError>(result));
  EXPECT_EQ(std::get<BoardValidationError>(result).code, BoardValidationCode::kNotM1Board);
}

TEST(BoardSnapshotTest, RequiresEveryTargetTerminalOnAnAllowedLayer) {
  BoardData data = ValidBoardData();
  data.terminals[0].layers = {0};
  data.terminals[1].layers = {0};
  data.routing_profile.allowed_layers = {31};

  BoardCreationResult result = CreateBoardSnapshot(std::move(data));

  ASSERT_TRUE(std::holds_alternative<BoardValidationError>(result));
  EXPECT_EQ(std::get<BoardValidationError>(result).code,
            BoardValidationCode::kInvalidRoutingProfile);
}

TEST(BoardSnapshotTest, RejectsDuplicatePhysicalLayerOrders) {
  BoardData data = ValidBoardData();
  data.layers[1].physical_order = data.layers[0].physical_order;

  BoardCreationResult result = CreateBoardSnapshot(std::move(data));

  ASSERT_TRUE(std::holds_alternative<BoardValidationError>(result));
  EXPECT_EQ(std::get<BoardValidationError>(result).code, BoardValidationCode::kInvalidLayerStack);
}

TEST(BoardSnapshotTest, RejectsUnknownLayerTypes) {
  BoardData data = ValidBoardData();
  data.layers[0].type = static_cast<LayerType>(255);

  BoardCreationResult result = CreateBoardSnapshot(std::move(data));

  ASSERT_TRUE(std::holds_alternative<BoardValidationError>(result));
  EXPECT_EQ(std::get<BoardValidationError>(result).code, BoardValidationCode::kInvalidLayer);
}

TEST(BoardSnapshotTest, RejectsInvalidUtf8InSchemaStrings) {
  BoardData data = ValidBoardData();
  data.nets[0].name = std::string("TARGET") + static_cast<char>(0xff);

  BoardCreationResult result = CreateBoardSnapshot(std::move(data));

  ASSERT_TRUE(std::holds_alternative<BoardValidationError>(result));
  EXPECT_EQ(std::get<BoardValidationError>(result).code, BoardValidationCode::kInvalidEncoding);
}

}  // namespace
}  // namespace apgar::board_ir
