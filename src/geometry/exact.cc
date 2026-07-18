#include "apgar/geometry/exact.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

namespace apgar::geometry {
namespace {

using Wide = __int128_t;
using UWide = __uint128_t;

struct UInt256 {
  std::array<std::uint64_t, 4> limbs{};
};

[[nodiscard]] bool GreaterThanOrEqual(const UInt256& left, const UInt256& right) noexcept {
  for (std::size_t index = left.limbs.size(); index > 0; --index) {
    const std::size_t limb = index - 1;
    if (left.limbs[limb] != right.limbs[limb]) {
      return left.limbs[limb] > right.limbs[limb];
    }
  }
  return true;
}

[[nodiscard]] UInt256 Multiply(UWide left, UWide right) noexcept {
  const std::array<std::uint64_t, 2> left_limbs = {
      static_cast<std::uint64_t>(left),
      static_cast<std::uint64_t>(left >> 64U),
  };
  const std::array<std::uint64_t, 2> right_limbs = {
      static_cast<std::uint64_t>(right),
      static_cast<std::uint64_t>(right >> 64U),
  };

  UInt256 result;
  for (std::size_t left_index = 0; left_index < left_limbs.size(); ++left_index) {
    UWide carry = 0;
    for (std::size_t right_index = 0; right_index < right_limbs.size(); ++right_index) {
      const std::size_t output_index = left_index + right_index;
      const UWide product =
          static_cast<UWide>(left_limbs[left_index]) * static_cast<UWide>(right_limbs[right_index]);
      const UWide accumulated = product + static_cast<UWide>(result.limbs[output_index]) + carry;
      result.limbs[output_index] = static_cast<std::uint64_t>(accumulated);
      carry = accumulated >> 64U;
    }

    std::size_t output_index = left_index + right_limbs.size();
    while (carry != 0 && output_index < result.limbs.size()) {
      const UWide accumulated = static_cast<UWide>(result.limbs[output_index]) + carry;
      result.limbs[output_index] = static_cast<std::uint64_t>(accumulated);
      carry = accumulated >> 64U;
      ++output_index;
    }
  }
  return result;
}

[[nodiscard]] UWide Absolute(Wide value) noexcept {
  return value < 0 ? static_cast<UWide>(-value) : static_cast<UWide>(value);
}

[[nodiscard]] UInt256 Square(Wide value) noexcept {
  const UWide magnitude = Absolute(value);
  return Multiply(magnitude, magnitude);
}

struct ScaledPoint {
  Wide x;
  Wide y;
};

struct ScaledBox {
  ScaledPoint min;
  ScaledPoint max;
};

[[nodiscard]] ScaledPoint Scale(board_ir::Point64 point) noexcept {
  return ScaledPoint{
      .x = static_cast<Wide>(point.x) * 2,
      .y = static_cast<Wide>(point.y) * 2,
  };
}

[[nodiscard]] Wide Orientation(ScaledPoint first, ScaledPoint second, ScaledPoint third) noexcept {
  return (second.x - first.x) * (third.y - first.y) - (second.y - first.y) * (third.x - first.x);
}

[[nodiscard]] bool Between(Wide value, Wide first, Wide second) noexcept {
  return value >= std::min(first, second) && value <= std::max(first, second);
}

[[nodiscard]] bool OnSegment(ScaledPoint first, ScaledPoint second, ScaledPoint point) noexcept {
  return Orientation(first, second, point) == 0 && Between(point.x, first.x, second.x) &&
         Between(point.y, first.y, second.y);
}

[[nodiscard]] int Sign(Wide value) noexcept { return (value > 0) - (value < 0); }

[[nodiscard]] bool SegmentsIntersect(ScaledPoint first_start, ScaledPoint first_end,
                                     ScaledPoint second_start, ScaledPoint second_end) noexcept {
  const Wide first_side_start = Orientation(first_start, first_end, second_start);
  const Wide first_side_end = Orientation(first_start, first_end, second_end);
  const Wide second_side_start = Orientation(second_start, second_end, first_start);
  const Wide second_side_end = Orientation(second_start, second_end, first_end);

  if (Sign(first_side_start) * Sign(first_side_end) < 0 &&
      Sign(second_side_start) * Sign(second_side_end) < 0) {
    return true;
  }
  return (first_side_start == 0 && OnSegment(first_start, first_end, second_start)) ||
         (first_side_end == 0 && OnSegment(first_start, first_end, second_end)) ||
         (second_side_start == 0 && OnSegment(second_start, second_end, first_start)) ||
         (second_side_end == 0 && OnSegment(second_start, second_end, first_end));
}

[[nodiscard]] bool PointInsideClosedBox(ScaledPoint point, const ScaledBox& box) noexcept {
  return point.x >= box.min.x && point.x <= box.max.x && point.y >= box.min.y &&
         point.y <= box.max.y;
}

[[nodiscard]] bool VectorLengthAtLeast(Wide x, Wide y, Wide required_distance) noexcept {
  const UWide x_magnitude = Absolute(x);
  const UWide y_magnitude = Absolute(y);
  const UWide distance_squared = x_magnitude * x_magnitude + y_magnitude * y_magnitude;
  const UWide required = static_cast<UWide>(required_distance);
  return distance_squared >= required * required;
}

[[nodiscard]] bool PointSegmentDistanceAtLeast(ScaledPoint point, ScaledPoint segment_start,
                                               ScaledPoint segment_end,
                                               Wide required_distance) noexcept {
  const Wide segment_x = segment_end.x - segment_start.x;
  const Wide segment_y = segment_end.y - segment_start.y;
  const Wide point_x = point.x - segment_start.x;
  const Wide point_y = point.y - segment_start.y;
  const UWide segment_x_magnitude = Absolute(segment_x);
  const UWide segment_y_magnitude = Absolute(segment_y);
  const UWide segment_length_squared =
      segment_x_magnitude * segment_x_magnitude + segment_y_magnitude * segment_y_magnitude;

  if (segment_length_squared == 0) {
    return VectorLengthAtLeast(point_x, point_y, required_distance);
  }

  const Wide projection = point_x * segment_x + point_y * segment_y;
  if (projection <= 0) {
    return VectorLengthAtLeast(point_x, point_y, required_distance);
  }
  if (static_cast<UWide>(projection) >= segment_length_squared) {
    return VectorLengthAtLeast(point.x - segment_end.x, point.y - segment_end.y, required_distance);
  }

  const Wide cross = segment_x * point_y - segment_y * point_x;
  const UWide required = static_cast<UWide>(required_distance);
  const UInt256 left = Square(cross);
  const UInt256 right = Multiply(required * required, segment_length_squared);
  return GreaterThanOrEqual(left, right);
}

[[nodiscard]] bool SegmentClearanceAtLeastScaled(board_ir::Segment64 segment,
                                                 const board_ir::AxisAlignedBox64& obstacle,
                                                 Wide required_distance_twice) noexcept {
  if (required_distance_twice == 0) {
    return true;
  }

  const ScaledPoint segment_start = Scale(segment.start);
  const ScaledPoint segment_end = Scale(segment.end);
  const ScaledBox box = {
      .min = Scale(obstacle.min),
      .max = Scale(obstacle.max),
  };
  const Wide segment_min_x = std::min(segment_start.x, segment_end.x);
  const Wide segment_max_x = std::max(segment_start.x, segment_end.x);
  const Wide segment_min_y = std::min(segment_start.y, segment_end.y);
  const Wide segment_max_y = std::max(segment_start.y, segment_end.y);
  if (segment_max_x <= box.min.x - required_distance_twice ||
      segment_min_x >= box.max.x + required_distance_twice ||
      segment_max_y <= box.min.y - required_distance_twice ||
      segment_min_y >= box.max.y + required_distance_twice) {
    return true;
  }
  if (PointInsideClosedBox(segment_start, box) || PointInsideClosedBox(segment_end, box)) {
    return false;
  }

  const std::array<ScaledPoint, 4> corners = {
      box.min,
      ScaledPoint{.x = box.max.x, .y = box.min.y},
      box.max,
      ScaledPoint{.x = box.min.x, .y = box.max.y},
  };

  for (std::size_t index = 0; index < corners.size(); ++index) {
    const ScaledPoint edge_start = corners[index];
    const ScaledPoint edge_end = corners[(index + 1) % corners.size()];
    if (SegmentsIntersect(segment_start, segment_end, edge_start, edge_end)) {
      return false;
    }
    if (!PointSegmentDistanceAtLeast(segment_start, edge_start, edge_end,
                                     required_distance_twice) ||
        !PointSegmentDistanceAtLeast(segment_end, edge_start, edge_end, required_distance_twice) ||
        !PointSegmentDistanceAtLeast(edge_start, segment_start, segment_end,
                                     required_distance_twice) ||
        !PointSegmentDistanceAtLeast(edge_end, segment_start, segment_end,
                                     required_distance_twice)) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] board_ir::HeadingMask HeadingFor(board_ir::Segment64 segment) noexcept {
  const board_ir::DbCoord delta_x = segment.end.x - segment.start.x;
  const board_ir::DbCoord delta_y = segment.end.y - segment.start.y;
  if (delta_y == 0) {
    return static_cast<board_ir::HeadingMask>(board_ir::Heading::kHorizontal);
  }
  if (delta_x == 0) {
    return static_cast<board_ir::HeadingMask>(board_ir::Heading::kVertical);
  }
  if (Absolute(delta_x) == Absolute(delta_y)) {
    return static_cast<board_ir::HeadingMask>(board_ir::Heading::kDiagonal45);
  }
  return 0;
}

[[nodiscard]] MovementValidationResult Failure(MovementViolationCode code, std::string detail) {
  return MovementValidationResult{
      .code = code,
      .obstacle = std::nullopt,
      .detail = std::move(detail),
  };
}

}  // namespace

SegmentClearanceResult SegmentClearanceAtLeast(board_ir::Segment64 segment,
                                               const board_ir::AxisAlignedBox64& obstacle,
                                               board_ir::DbCoord min_distance) {
  if (!board_ir::PointIsValid(segment.start) || !board_ir::PointIsValid(segment.end) ||
      !board_ir::PointIsValid(obstacle.min) || !board_ir::PointIsValid(obstacle.max)) {
    return SegmentClearanceResult{
        .error = ExactGeometryErrorCode::kCoordinateOutOfRange,
        .clearance_satisfied = false,
        .detail = "Exact geometry input exceeds the validated coordinate range",
    };
  }
  if (!board_ir::BoxIsValid(obstacle)) {
    return SegmentClearanceResult{
        .error = ExactGeometryErrorCode::kInvalidObstacle,
        .clearance_satisfied = false,
        .detail = "Obstacle bounds are not normalized",
    };
  }
  if (min_distance < 0 || min_distance > board_ir::kMaxAbsDbCoord) {
    return SegmentClearanceResult{
        .error = ExactGeometryErrorCode::kInvalidDistance,
        .clearance_satisfied = false,
        .detail = "Minimum distance is negative or exceeds the validated range",
    };
  }
  return SegmentClearanceResult{
      .error = ExactGeometryErrorCode::kNone,
      .clearance_satisfied =
          SegmentClearanceAtLeastScaled(segment, obstacle, static_cast<Wide>(min_distance) * 2),
      .detail = {},
  };
}

MovementValidationResult ValidateMovement(const board_ir::BoardSnapshot& board,
                                          board_ir::LayerId layer, board_ir::Segment64 centerline) {
  if (!board_ir::PointIsValid(centerline.start) || !board_ir::PointIsValid(centerline.end)) {
    return Failure(MovementViolationCode::kCoordinateOutOfRange,
                   "Movement endpoint exceeds the validated coordinate range");
  }
  if (centerline.start == centerline.end) {
    return Failure(MovementViolationCode::kDegenerateSegment,
                   "Movement centerline has zero length");
  }

  const board_ir::HeadingMask heading = HeadingFor(centerline);
  const board_ir::RoutingProfile& profile = board.data().routing_profile;
  if (heading == 0 || (profile.allowed_headings & heading) == 0) {
    return Failure(MovementViolationCode::kUnsupportedHeading,
                   "Movement heading is not supported by the routing profile");
  }
  if (board.FindLayer(layer) == nullptr) {
    return Failure(MovementViolationCode::kUnknownLayer, "Movement refers to an unknown layer");
  }
  if (!std::ranges::binary_search(profile.allowed_layers, layer)) {
    return Failure(MovementViolationCode::kLayerNotAllowed,
                   "Movement layer is not allowed by the routing profile");
  }

  const Wide required_distance_twice =
      static_cast<Wide>(profile.nominal_width) + static_cast<Wide>(profile.clearance) * 2;
  for (const board_ir::Obstacle& obstacle : board.ObstaclesOnLayer(layer)) {
    if (obstacle.owner_net.has_value() && *obstacle.owner_net == profile.net) {
      continue;
    }
    if (!SegmentClearanceAtLeastScaled(centerline, obstacle.bounds, required_distance_twice)) {
      return MovementValidationResult{
          .code = MovementViolationCode::kStaticObstacleConflict,
          .obstacle = obstacle.ref,
          .detail = "Movement conflicts with " + obstacle.provenance,
      };
    }
  }
  return {};
}

}  // namespace apgar::geometry
