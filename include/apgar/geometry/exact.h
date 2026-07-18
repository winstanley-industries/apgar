#ifndef APGAR_GEOMETRY_EXACT_H_
#define APGAR_GEOMETRY_EXACT_H_

#include <cstdint>
#include <optional>
#include <string>

#include "apgar/board_ir/board.h"

namespace apgar::geometry {

enum class ExactGeometryErrorCode : std::uint8_t {
  kNone,
  kCoordinateOutOfRange,
  kInvalidObstacle,
  kInvalidDistance,
};

struct SegmentClearanceResult {
  ExactGeometryErrorCode error = ExactGeometryErrorCode::kNone;
  bool clearance_satisfied = false;
  std::string detail;

  [[nodiscard]] bool ok() const noexcept { return error == ExactGeometryErrorCode::kNone; }
};

// Compares the minimum Euclidean distance between the closed centerline segment
// and closed obstacle box with min_distance. Equality satisfies clearance.
// Invalid or out-of-envelope inputs are reported separately from a collision.
[[nodiscard]] SegmentClearanceResult SegmentClearanceAtLeast(
    board_ir::Segment64 segment, const board_ir::AxisAlignedBox64& obstacle,
    board_ir::DbCoord min_distance);

enum class MovementViolationCode : std::uint8_t {
  kNone,
  kCoordinateOutOfRange,
  kDegenerateSegment,
  kUnsupportedHeading,
  kUnknownLayer,
  kLayerNotAllowed,
  kStaticObstacleConflict,
};

struct MovementValidationResult {
  MovementViolationCode code = MovementViolationCode::kNone;
  std::optional<board_ir::EntityRef> obstacle;
  std::string detail;

  [[nodiscard]] bool legal() const noexcept { return code == MovementViolationCode::kNone; }
};

[[nodiscard]] MovementValidationResult ValidateMovement(const board_ir::BoardSnapshot& board,
                                                        board_ir::LayerId layer,
                                                        board_ir::Segment64 centerline);

}  // namespace apgar::geometry

#endif  // APGAR_GEOMETRY_EXACT_H_
