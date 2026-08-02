#ifndef APGAR_GEOMETRY_EXACT_INTERNAL_H_
#define APGAR_GEOMETRY_EXACT_INTERNAL_H_

#include "apgar/board_ir/board.h"
#include "apgar/geometry/exact.h"

namespace apgar::geometry::internal {

// Source-private exact oracle for a routing profile whose type proves Board IR
// preparation. The implementation also verifies its source-snapshot binding.
[[nodiscard]] MovementValidationResult ValidateMovementForPreparedProfile(
    const board_ir::BoardSnapshot& board, const board_ir::PreparedRoutingProfile& routing_profile,
    board_ir::LayerId layer, board_ir::Segment64 centerline);

}  // namespace apgar::geometry::internal

#endif  // APGAR_GEOMETRY_EXACT_INTERNAL_H_
