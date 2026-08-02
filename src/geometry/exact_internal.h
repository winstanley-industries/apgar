#ifndef APGAR_GEOMETRY_EXACT_INTERNAL_H_
#define APGAR_GEOMETRY_EXACT_INTERNAL_H_

#include "apgar/board_ir/board.h"
#include "apgar/geometry/exact.h"

namespace apgar::geometry::internal {

// Source-private exact oracle for an already prepared routing profile. This
// internal header is a layering convention rather than a capability boundary;
// trusted callers must obtain the profile from Board IR's preparation API.
[[nodiscard]] MovementValidationResult ValidateMovementForPreparedProfile(
    const board_ir::BoardSnapshot& board, const board_ir::RoutingProfile& routing_profile,
    board_ir::LayerId layer, board_ir::Segment64 centerline);

}  // namespace apgar::geometry::internal

#endif  // APGAR_GEOMETRY_EXACT_INTERNAL_H_
