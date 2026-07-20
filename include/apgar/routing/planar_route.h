#ifndef APGAR_ROUTING_PLANAR_ROUTE_H_
#define APGAR_ROUTING_PLANAR_ROUTE_H_

#include <cstdint>
#include <optional>
#include <span>
#include <variant>
#include <vector>

#include "apgar/board_ir/board.h"
#include "apgar/geometry_compiler/compiled_board.h"
#include "apgar/routing/candidate_policy.h"

namespace apgar::routing {

// Backend-neutral immutable request shared by the CPU oracle and every planar
// GPU candidate explorer. The legacy alias below remains source-compatible
// with the Phase 1/2 CPU API.
struct PlanarRouteRequest {
  board_ir::EntityRef net;
  board_ir::Point64 start;
  board_ir::Point64 goal;
  board_ir::LayerId start_layer;
  board_ir::LayerId goal_layer;
  CandidateGenerationPolicy candidate_policy;

  friend bool operator==(const PlanarRouteRequest&, const PlanarRouteRequest&) = default;
};

using CpuRouteRequest = PlanarRouteRequest;

struct LayerSegment {
  board_ir::LayerId layer;
  board_ir::Segment64 centerline;

  friend bool operator==(const LayerSegment&, const LayerSegment&) = default;
};

enum class CompiledBoardAssociationIssue : std::uint8_t {
  kSourceBoardMismatch,
  kCompilerVersionMismatch,
  kProfileFingerprintMismatch,
  kRuleBucketMismatch,
};

[[nodiscard]] std::optional<CompiledBoardAssociationIssue> ValidateCompiledBoardAssociation(
    const board_ir::BoardSnapshot& board,
    const geometry_compiler::CompiledBoard& compiled_board) noexcept;

enum class RouteRequestAdmissionIssue : std::uint8_t {
  kRoutingProfileNetMismatch,
  kInvalidOrCoincidentEndpoints,
  kRequiresExactlyTwoTerminals,
  kMissingTerminal,
  kEndpointsNotTerminalCenters,
  kUnsupportedTerminalLayer,
  kLayerOutsideRuleBucket,
};

[[nodiscard]] std::optional<RouteRequestAdmissionIssue> ValidateTwoTerminalRouteRequest(
    const board_ir::BoardSnapshot& board, const geometry_compiler::CompiledBoard& compiled_board,
    const CpuRouteRequest& request) noexcept;

enum class TwoTerminalRequestIssue : std::uint8_t {
  kMissingRoutingProfileNet,
  kRequiresExactlyTwoTerminals,
  kMissingTerminal,
};

using TwoTerminalRequestResult = std::variant<PlanarRouteRequest, TwoTerminalRequestIssue>;

[[nodiscard]] TwoTerminalRequestResult BuildTwoTerminalRouteRequest(
    const board_ir::BoardSnapshot& board, board_ir::LayerId start_layer,
    board_ir::LayerId goal_layer) noexcept;

enum class PlanarEndpointIssue : std::uint8_t {
  kNotOnCompilerLattice,
  kNotRepresented,
};

struct ResolvedPlanarEndpoints {
  geometry_compiler::LatticeIndex start;
  geometry_compiler::LatticeIndex goal;

  friend bool operator==(const ResolvedPlanarEndpoints&, const ResolvedPlanarEndpoints&) = default;
};

using PlanarEndpointResult = std::variant<ResolvedPlanarEndpoints, PlanarEndpointIssue>;

[[nodiscard]] PlanarEndpointResult ResolvePlanarEndpoints(
    const geometry_compiler::CompiledBoard& compiled_board,
    const CpuRouteRequest& request) noexcept;

[[nodiscard]] std::optional<std::uint64_t> CheckedAdd(std::uint64_t left,
                                                      std::uint64_t right) noexcept;

// Route labels reserve UINT64_MAX as the unreachable sentinel. Unlike the
// generic checked integer helper above, this operation rejects a sum equal to
// UINT64_MAX as well as arithmetic overflow.
[[nodiscard]] std::optional<std::uint64_t> CheckedAddFiniteRouteCost(std::uint64_t left,
                                                                     std::uint64_t right) noexcept;

[[nodiscard]] std::uint64_t StepCost(const geometry_compiler::CompilerProfile& profile,
                                     geometry_compiler::Direction direction,
                                     std::uint8_t incoming_direction) noexcept;

[[nodiscard]] std::optional<geometry_compiler::Direction> DirectionBetween(
    geometry_compiler::LatticeIndex start, geometry_compiler::LatticeIndex end) noexcept;

[[nodiscard]] std::vector<LayerSegment> CoalesceSegments(board_ir::LayerId layer,
                                                         std::span<const board_ir::Point64> points);

}  // namespace apgar::routing

#endif  // APGAR_ROUTING_PLANAR_ROUTE_H_
