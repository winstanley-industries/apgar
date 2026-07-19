#ifndef APGAR_ROUTING_CPU_ASTAR_H_
#define APGAR_ROUTING_CPU_ASTAR_H_

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

#include "apgar/board_ir/board.h"
#include "apgar/geometry_compiler/compiled_board.h"

namespace apgar::routing {

struct CpuRouteRequest {
  board_ir::EntityRef net;
  board_ir::Point64 start;
  board_ir::Point64 goal;
  board_ir::LayerId start_layer;
  board_ir::LayerId goal_layer;

  friend bool operator==(const CpuRouteRequest&, const CpuRouteRequest&) = default;
};

struct LayerSegment {
  board_ir::LayerId layer;
  board_ir::Segment64 centerline;

  friend bool operator==(const LayerSegment&, const LayerSegment&) = default;
};

enum class RouteFailureCode : std::uint8_t {
  kInvalidRequest,
  kDisconnected,
  kUnsupportedLayerTransition,
  kValidationFailed,
  kResourceExhausted,
  kInternalInvariant,
};

struct CpuRouteTelemetry {
  std::uint64_t queue_pops = 0;
  std::uint64_t expanded_states = 0;
  std::uint64_t attempted_relaxations = 0;
  std::uint64_t accepted_relaxations = 0;
  std::uint64_t peak_record_count = 0;
  std::uint64_t peak_queue_size = 0;

  friend bool operator==(const CpuRouteTelemetry&, const CpuRouteTelemetry&) = default;
};

struct RouteFailure {
  RouteFailureCode code;
  std::string detail;
  std::optional<board_ir::EntityRef> obstacle;
  std::optional<CpuRouteTelemetry> telemetry;

  friend bool operator==(const RouteFailure&, const RouteFailure&) = default;
};

struct CpuRoute {
  std::uint64_t source_board_content_hash;
  std::uint64_t compiler_profile_fingerprint;
  std::uint32_t compiler_version;
  std::uint64_t rule_bucket_identity;
  std::uint64_t total_cost;
  std::vector<board_ir::Point64> lattice_path;
  std::vector<LayerSegment> segments;
  CpuRouteTelemetry telemetry;

  friend bool operator==(const CpuRoute&, const CpuRoute&) = default;
};

using CpuRouteResult = std::variant<CpuRoute, RouteFailure>;

// M1 routes one exact planar layer. start_layer != goal_layer is retained in
// the request contract so a future exact through-via transition provider can
// extend adjacency without changing terminal semantics; today it returns a
// structured unsupported result.
[[nodiscard]] CpuRouteResult RouteWithCpuAStar(
    const board_ir::BoardSnapshot& board, const geometry_compiler::CompiledBoard& compiled_board,
    const CpuRouteRequest& request);

// Public for differential and corruption tests. Every returned route passes
// this exact validator after reconstruction; compiled masks are never accepted
// as legality authority.
[[nodiscard]] std::optional<RouteFailure> ValidateReconstructedRoute(
    const board_ir::BoardSnapshot& board, const geometry_compiler::CompiledBoard& compiled_board,
    const CpuRouteRequest& request, std::span<const LayerSegment> segments);

}  // namespace apgar::routing

#endif  // APGAR_ROUTING_CPU_ASTAR_H_
