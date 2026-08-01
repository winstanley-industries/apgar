#ifndef APGAR_GEOMETRY_EXACT_INTERNAL_H_
#define APGAR_GEOMETRY_EXACT_INTERNAL_H_

#include "apgar/board_ir/board.h"
#include "apgar/geometry/exact.h"

namespace apgar::geometry::internal {

// Source-private exact oracle for an already prepared routing profile. Public
// callers cannot substitute an unvalidated net identity to change obstacle
// ownership semantics; trusted compilers obtain this profile from Board IR's
// preparation boundary.
[[nodiscard]] MovementValidationResult ValidateMovementForPreparedProfile(
    const board_ir::BoardSnapshot& board, const board_ir::RoutingProfile& routing_profile,
    board_ir::LayerId layer, board_ir::Segment64 centerline);

}  // namespace apgar::geometry::internal

#endif  // APGAR_GEOMETRY_EXACT_INTERNAL_H_
