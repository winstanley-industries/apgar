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

// Compares the minimum Euclidean distance between two closed centerline
// segments with min_distance. Equality satisfies clearance. This is the exact
// swept-trace self-clearance authority when min_distance is the sum of the two
// trace half-widths.
[[nodiscard]] SegmentClearanceResult SegmentToSegmentClearanceAtLeast(
    board_ir::Segment64 first, board_ir::Segment64 second, board_ir::DbCoord min_distance);

// Compares the complete swept trace envelope against a closed box without
// rounding an odd nominal width. The required centerline distance is
// nominal_width / 2 + clearance, evaluated exactly in half-DBU units.
// Boundary equality satisfies clearance.
[[nodiscard]] SegmentClearanceResult SweptTraceClearanceAtLeast(
    board_ir::Segment64 centerline, const board_ir::AxisAlignedBox64& obstacle,
    board_ir::DbCoord nominal_width, board_ir::DbCoord clearance);

enum class MovementViolationCode : std::uint8_t {
  kNone,
  kCoordinateOutOfRange,
  kDegenerateSegment,
  kUnsupportedHeading,
  kUnknownLayer,
  kLayerNotAllowed,
  kStaticObstacleConflict,
  kPreparedProfileSnapshotMismatch,
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

namespace internal {

// Exact oracle for a routing profile whose type proves Board IR preparation.
// The source-snapshot binding is checked before any geometry is evaluated.
[[nodiscard]] MovementValidationResult ValidateMovementForPreparedProfile(
    const board_ir::BoardSnapshot& board, const board_ir::PreparedRoutingProfile& routing_profile,
    board_ir::LayerId layer, board_ir::Segment64 centerline);

}  // namespace internal

}  // namespace apgar::geometry

#endif  // APGAR_GEOMETRY_EXACT_H_
