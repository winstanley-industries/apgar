#include "apgar/geometry/exact.h"

#include <cstdint>
#include <string>
#include <utility>
#include <variant>

#include "apgar/board_ir/board.h"
#include "tests/support/board_builder.h"
#include "tests/support/google_test.h"

namespace apgar::geometry {
namespace {

using board_ir::AxisAlignedBox64;
using board_ir::BoardCreationResult;
using board_ir::BoardSnapshot;
using board_ir::Point64;
using board_ir::Segment64;

[[nodiscard]] BoardCreationResult MovementBoard() {
  return board_ir::CreateBoardSnapshot(test_support::ValidM1BoardData());
}

[[nodiscard]] bool CheckedClearance(Segment64 segment, const AxisAlignedBox64& obstacle,
                                    board_ir::DbCoord min_distance) {
  const SegmentClearanceResult result = SegmentClearanceAtLeast(segment, obstacle, min_distance);
  EXPECT_TRUE(result.ok()) << result.detail;
  return result.ok() && result.clearance_satisfied;
}

TEST(ExactClearanceTest, TreatsBoundaryEqualityAsLegal) {
  const AxisAlignedBox64 obstacle{
      .min = Point64{.x = 0, .y = 0},
      .max = Point64{.x = 100, .y = 100},
  };

  EXPECT_TRUE(CheckedClearance(
      Segment64{.start = Point64{.x = -100, .y = 150}, .end = Point64{.x = 200, .y = 150}},
      obstacle, 50));
  EXPECT_FALSE(CheckedClearance(
      Segment64{.start = Point64{.x = -100, .y = 149}, .end = Point64{.x = 200, .y = 149}},
      obstacle, 50));
  EXPECT_TRUE(CheckedClearance(
      Segment64{.start = Point64{.x = -100, .y = 151}, .end = Point64{.x = 200, .y = 151}},
      obstacle, 50));
}

TEST(ExactClearanceTest, UsesEuclideanDistanceAtObstacleCorners) {
  const AxisAlignedBox64 point_obstacle{
      .min = Point64{.x = 0, .y = 0},
      .max = Point64{.x = 0, .y = 0},
  };
  const Segment64 point_segment{
      .start = Point64{.x = 3, .y = 4},
      .end = Point64{.x = 3, .y = 4},
  };

  EXPECT_TRUE(CheckedClearance(point_segment, point_obstacle, 5));
  EXPECT_FALSE(CheckedClearance(point_segment, point_obstacle, 6));
}

TEST(ExactClearanceTest, ComparesRationalDiagonalDistanceWithoutRounding) {
  const AxisAlignedBox64 point_obstacle{
      .min = Point64{.x = 0, .y = 0},
      .max = Point64{.x = 0, .y = 0},
  };
  const Segment64 diagonal{
      .start = Point64{.x = -10, .y = 0},
      .end = Point64{.x = 0, .y = 10},
  };
  const Segment64 reversed{
      .start = diagonal.end,
      .end = diagonal.start,
  };

  EXPECT_TRUE(CheckedClearance(diagonal, point_obstacle, 7));
  EXPECT_FALSE(CheckedClearance(diagonal, point_obstacle, 8));
  EXPECT_EQ(CheckedClearance(diagonal, point_obstacle, 7),
            CheckedClearance(reversed, point_obstacle, 7));
}

TEST(ExactClearanceTest, RejectsCenterlinesThatCrossTheObstacle) {
  const AxisAlignedBox64 obstacle{
      .min = Point64{.x = 0, .y = 0},
      .max = Point64{.x = 100, .y = 100},
  };
  EXPECT_FALSE(CheckedClearance(
      Segment64{.start = Point64{.x = -50, .y = 50}, .end = Point64{.x = 150, .y = 50}}, obstacle,
      1));
}

TEST(ExactClearanceTest, IsTranslationAndReflectionInvariant) {
  for (std::int64_t translation = -1'000; translation <= 1'000; translation += 37) {
    const AxisAlignedBox64 obstacle{
        .min = Point64{.x = translation, .y = translation},
        .max = Point64{.x = translation + 100, .y = translation + 100},
    };
    const Segment64 above{
        .start = Point64{.x = translation - 50, .y = translation + 150},
        .end = Point64{.x = translation + 150, .y = translation + 150},
    };
    const Segment64 below{
        .start = Point64{.x = translation - 50, .y = translation - 50},
        .end = Point64{.x = translation + 150, .y = translation - 50},
    };
    EXPECT_TRUE(CheckedClearance(above, obstacle, 50));
    EXPECT_TRUE(CheckedClearance(below, obstacle, 50));
  }
}

TEST(ExactClearanceTest, RemainsExactNearTheCoordinateEnvelope) {
  constexpr std::int64_t kLimit = board_ir::kMaxAbsDbCoord;
  const AxisAlignedBox64 obstacle{
      .min = Point64{.x = kLimit - 1'000, .y = kLimit - 1'000},
      .max = Point64{.x = kLimit - 900, .y = kLimit - 900},
  };
  const Segment64 segment{
      .start = Point64{.x = kLimit - 1'100, .y = kLimit - 850},
      .end = Point64{.x = kLimit - 800, .y = kLimit - 850},
  };

  EXPECT_TRUE(CheckedClearance(segment, obstacle, 50));
  EXPECT_FALSE(CheckedClearance(segment, obstacle, 51));
}

TEST(ExactClearanceTest, ReportsInvalidInputsSeparatelyFromCollisions) {
  const AxisAlignedBox64 valid_obstacle{
      .min = Point64{.x = 0, .y = 0},
      .max = Point64{.x = 10, .y = 10},
  };
  const Segment64 valid_segment{
      .start = Point64{.x = -10, .y = 5},
      .end = Point64{.x = 20, .y = 5},
  };

  const SegmentClearanceResult coordinate_error =
      SegmentClearanceAtLeast(Segment64{.start = Point64{.x = board_ir::kMaxAbsDbCoord + 1, .y = 0},
                                        .end = Point64{.x = 0, .y = 0}},
                              valid_obstacle, 1);
  const SegmentClearanceResult obstacle_error = SegmentClearanceAtLeast(
      valid_segment,
      AxisAlignedBox64{.min = Point64{.x = 10, .y = 0}, .max = Point64{.x = 0, .y = 10}}, 1);
  const SegmentClearanceResult distance_error =
      SegmentClearanceAtLeast(valid_segment, valid_obstacle, -1);
  const SegmentClearanceResult collision =
      SegmentClearanceAtLeast(valid_segment, valid_obstacle, 1);

  EXPECT_EQ(coordinate_error.error, ExactGeometryErrorCode::kCoordinateOutOfRange);
  EXPECT_EQ(obstacle_error.error, ExactGeometryErrorCode::kInvalidObstacle);
  EXPECT_EQ(distance_error.error, ExactGeometryErrorCode::kInvalidDistance);
  EXPECT_TRUE(collision.ok());
  EXPECT_FALSE(collision.clearance_satisfied);
}

TEST(MovementValidationTest, ReportsTheConflictingObstacleProvenance) {
  BoardCreationResult board_result = MovementBoard();
  ASSERT_TRUE(std::holds_alternative<BoardSnapshot>(board_result));
  const BoardSnapshot& board = std::get<BoardSnapshot>(board_result);

  const MovementValidationResult result = ValidateMovement(
      board, 0, Segment64{.start = Point64{.x = 0, .y = 19}, .end = Point64{.x = 100, .y = 19}});

  EXPECT_FALSE(result.legal());
  EXPECT_EQ(result.code, MovementViolationCode::kStaticObstacleConflict);
  ASSERT_TRUE(result.obstacle.has_value());
  EXPECT_EQ(result.obstacle->id, 30U);
  EXPECT_NE(result.detail.find("U3/pad-1"), std::string::npos);
}

TEST(MovementValidationTest, AcceptsExactTraceAndClearanceEquality) {
  BoardCreationResult board_result = MovementBoard();
  ASSERT_TRUE(std::holds_alternative<BoardSnapshot>(board_result));
  const BoardSnapshot& board = std::get<BoardSnapshot>(board_result);

  const MovementValidationResult result = ValidateMovement(
      board, 0, Segment64{.start = Point64{.x = 0, .y = 20}, .end = Point64{.x = 100, .y = 20}});

  EXPECT_TRUE(result.legal()) << result.detail;
}

TEST(MovementValidationTest, RejectsHeadingsOutsideTheM1Contract) {
  BoardCreationResult board_result = MovementBoard();
  ASSERT_TRUE(std::holds_alternative<BoardSnapshot>(board_result));
  const BoardSnapshot& board = std::get<BoardSnapshot>(board_result);

  const MovementValidationResult result = ValidateMovement(
      board, 0, Segment64{.start = Point64{.x = 0, .y = 0}, .end = Point64{.x = 10, .y = 5}});

  EXPECT_EQ(result.code, MovementViolationCode::kUnsupportedHeading);
}

}  // namespace
}  // namespace apgar::geometry
