#ifndef APGAR_SRC_ROUTING_PLANAR_ROUTE_INTERNAL_H_
#define APGAR_SRC_ROUTING_PLANAR_ROUTE_INTERNAL_H_

#include <cstdint>

#include "apgar/routing/planar_route.h"

namespace apgar::routing::internal {

// Source-private, read-only association view used by the production validator
// and its corruption tables. Tests may alter copied scalar/profile values;
// they never receive a capability to mutate a CompiledBoard.
struct CompiledBoardAssociationView {
  std::uint64_t source_board_content_hash = 0;
  std::uint64_t compiler_profile_fingerprint = 0;
  std::uint32_t compiler_version = 0;
  const geometry_compiler::RuleBucketV1& rule_bucket;
  const board_ir::RoutingProfile& routing_profile;
  const geometry_compiler::CompilerProfile& profile;
};

[[nodiscard]] std::optional<CompiledBoardAssociationIssue> ValidateCompiledBoardAssociationView(
    const board_ir::BoardSnapshot& board, const CompiledBoardAssociationView& compiled) noexcept;

}  // namespace apgar::routing::internal

#endif  // APGAR_SRC_ROUTING_PLANAR_ROUTE_INTERNAL_H_
