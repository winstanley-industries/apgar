#ifndef APGAR_ROUTING_CPU_ASTAR_H_
#define APGAR_ROUTING_CPU_ASTAR_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

#include "apgar/board_ir/board.h"
#include "apgar/geometry_compiler/compiled_board.h"
#include "apgar/routing/planar_route.h"

namespace apgar::routing {

enum class RouteFailureCode : std::uint8_t {
  kInvalidRequest,
  kDisconnected,
  kUnsupportedLayerTransition,
  kValidationFailed,
  kResourceExhausted,
  kInternalInvariant,
  kUnsupportedPolicy,
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

struct CpuRoute;
class CpuRouteEvidenceAccess;

// Opaque, copyable evidence created only by the CPU A* implementation. It is
// not serialized and equality deliberately ignores handle identity.
class CpuRouteProducerEvidenceHandle {
 public:
  CpuRouteProducerEvidenceHandle() = default;
  CpuRouteProducerEvidenceHandle(const CpuRouteProducerEvidenceHandle&) = default;
  CpuRouteProducerEvidenceHandle(CpuRouteProducerEvidenceHandle&&) noexcept = default;
  CpuRouteProducerEvidenceHandle& operator=(const CpuRouteProducerEvidenceHandle&) = default;
  CpuRouteProducerEvidenceHandle& operator=(CpuRouteProducerEvidenceHandle&&) noexcept = default;

  friend bool operator==(const CpuRouteProducerEvidenceHandle&,
                         const CpuRouteProducerEvidenceHandle&) noexcept {
    return true;
  }

 private:
  struct Evidence;
  std::shared_ptr<const Evidence> evidence_;

  friend class CpuRouteEvidenceAccess;
};

struct CpuRoute {
  std::uint64_t source_board_content_hash;
  std::uint64_t compiler_profile_fingerprint;
  std::uint32_t compiler_version;
  std::uint64_t routing_profile_fingerprint;
  std::uint64_t rule_bucket_identity;
  board_ir::EntityRef net;
  board_ir::Point64 requested_start;
  board_ir::Point64 requested_goal;
  board_ir::LayerId requested_start_layer;
  board_ir::LayerId requested_goal_layer;
  std::uint64_t candidate_policy_identity;
  std::uint64_t total_cost;
  std::vector<board_ir::Point64> lattice_path;
  std::vector<LayerSegment> segments;
  CpuRouteTelemetry telemetry;
  CpuRouteProducerEvidenceHandle producer_evidence;

  friend bool operator==(const CpuRoute&, const CpuRoute&) = default;
};

using CpuRouteResult = std::variant<CpuRoute, RouteFailure>;

// Fully defined, non-extensible access class. Only the concrete out-of-line A*
// producer can seal evidence; consumers cannot complete a friend type or reach
// the private evidence storage.
class CpuRouteEvidenceAccess final {
 private:
  CpuRouteEvidenceAccess() = delete;

  static void Seal(CpuRoute& route);
  [[nodiscard]] static bool Authenticates(const CpuRoute& route) noexcept;

  friend CpuRouteResult RouteWithCpuAStar(const board_ir::BoardSnapshot&,
                                          const geometry_compiler::CompiledBoard&,
                                          const CpuRouteRequest&);
  friend bool CpuRouteHasAuthenticatedAStarEvidence(const CpuRoute&) noexcept;
};

// M1 routes one exact planar layer. start_layer != goal_layer is retained in
// the request contract so a future exact through-via transition provider can
// extend adjacency without changing terminal semantics; today it returns a
// structured unsupported result.
[[nodiscard]] CpuRouteResult RouteWithCpuAStar(
    const board_ir::BoardSnapshot& board, const geometry_compiler::CompiledBoard& compiled_board,
    const CpuRouteRequest& request);

// True only when the exact Board/compiler/routing/rule associations, request,
// policy identity, scalar cost, and candidate-authoritative segment sequence
// still match evidence sealed by RouteWithCpuAStar. `lattice_path` is a
// redundant reconstruction trace and `telemetry` is diagnostic; neither is
// producer-authenticated or consumed by candidate construction. Publicly
// fabricated aggregates or aggregates whose authenticated fields were
// subsequently relabeled return false.
[[nodiscard]] bool CpuRouteHasAuthenticatedAStarEvidence(const CpuRoute& route) noexcept;

// Public for differential and corruption tests. Every returned route passes
// this exact validator after reconstruction; compiled masks are never accepted
// as legality authority.
[[nodiscard]] std::optional<RouteFailure> ValidateReconstructedRoute(
    const board_ir::BoardSnapshot& board, const geometry_compiler::CompiledBoard& compiled_board,
    const CpuRouteRequest& request, std::span<const LayerSegment> segments);

}  // namespace apgar::routing

#endif  // APGAR_ROUTING_CPU_ASTAR_H_
