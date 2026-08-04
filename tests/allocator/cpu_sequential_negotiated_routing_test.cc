#include "apgar/allocator/cpu_sequential_negotiated_routing.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <map>
#include <optional>
#include <ranges>
#include <set>
#include <span>
#include <string>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

#include "apgar/board_ir/board.h"
#include "apgar/candidates/route_candidate.h"
#include "apgar/geometry_compiler/compiled_board.h"
#include "apgar/routing/candidate_policy.h"
#include "apgar/routing/cpu_astar.h"
#include "src/allocator/cpu_sequential_negotiated_routing_internal.h"
#include "tests/support/google_test.h"
#include "tests/support/routing_builder.h"

namespace apgar::allocator {
namespace {

using board_ir::AxisAlignedBox64;
using board_ir::BoardData;
using board_ir::EntityRef;
using board_ir::Layer;
using board_ir::LayerType;
using board_ir::Net;
using board_ir::Point64;
using board_ir::RoutingProfile;
using board_ir::Terminal;
using candidates::PhysicalEdgeSpan;
using candidates::RouteCandidate;
using geometry_compiler::ActiveRegion;
using geometry_compiler::CompiledBoard;
using geometry_compiler::CompilerProfile;
using geometry_compiler::DeterministicCosts;
using geometry_compiler::Direction;
using geometry_compiler::DirectionDelta;
using routing::EdgeResourceKey;

inline constexpr board_ir::LayerId kFront = 0;
inline constexpr board_ir::LayerId kBack = 1;
inline constexpr std::array<EntityRef, 5> kNets = {
    EntityRef{.id = 10, .generation = 0}, EntityRef{.id = 11, .generation = 0},
    EntityRef{.id = 12, .generation = 0}, EntityRef{.id = 13, .generation = 0},
    EntityRef{.id = 14, .generation = 0},
};

[[nodiscard]] AxisAlignedBox64 TerminalBox(Point64 center) {
  return AxisAlignedBox64{
      .min = Point64{.x = center.x - 2, .y = center.y - 2},
      .max = Point64{.x = center.x + 2, .y = center.y + 2},
  };
}

[[nodiscard]] Terminal MakeTerminal(std::uint64_t id, EntityRef net, Point64 center,
                                    std::string pin) {
  return Terminal{
      .ref = EntityRef{.id = id, .generation = 0},
      .net = net,
      .component = "SEQ",
      .pin = std::move(pin),
      .center = center,
      .connection_region = TerminalBox(center),
      .layers = {kFront, kBack},
  };
}

[[nodiscard]] BoardData SequentialBoardData(std::uint64_t revision = 1) {
  const std::array<EntityRef, 10> terminals = {
      EntityRef{.id = 20, .generation = 0}, EntityRef{.id = 21, .generation = 0},
      EntityRef{.id = 22, .generation = 0}, EntityRef{.id = 23, .generation = 0},
      EntityRef{.id = 24, .generation = 0}, EntityRef{.id = 25, .generation = 0},
      EntityRef{.id = 26, .generation = 0}, EntityRef{.id = 27, .generation = 0},
      EntityRef{.id = 28, .generation = 0}, EntityRef{.id = 29, .generation = 0},
  };
  return BoardData{
      .schema_version = board_ir::kBoardSchemaVersion,
      .dbu_per_millimeter = 1'000'000,
      .revision = revision,
      .adapter_name = "p4r05-sequential-microcase",
      .adapter_version = "1",
      .layers =
          {
              Layer{
                  .ref = EntityRef{.id = 1, .generation = 0},
                  .routing_id = kFront,
                  .name = "front",
                  .physical_order = 0,
                  .type = LayerType::kSignal,
                  .routable = true,
              },
              Layer{
                  .ref = EntityRef{.id = 2, .generation = 0},
                  .routing_id = kBack,
                  .name = "back",
                  .physical_order = 1,
                  .type = LayerType::kSignal,
                  .routable = true,
              },
          },
      .nets =
          {
              Net{.ref = kNets[0], .name = "FLEX", .terminals = {terminals[0], terminals[1]}},
              Net{.ref = kNets[1], .name = "CROSS", .terminals = {terminals[2], terminals[3]}},
              Net{.ref = kNets[2], .name = "DISJOINT", .terminals = {terminals[4], terminals[5]}},
              Net{.ref = kNets[3],
                  .name = "DISCONNECTED",
                  .terminals = {terminals[6], terminals[7]}},
              Net{.ref = kNets[4],
                  .name = "UNSUPPORTED",
                  .terminals = {terminals[8], terminals[9]}},
          },
      .terminals =
          {
              MakeTerminal(20, kNets[0], Point64{.x = 0, .y = 0}, "A1"),
              MakeTerminal(21, kNets[0], Point64{.x = 100, .y = 0}, "A2"),
              MakeTerminal(22, kNets[1], Point64{.x = 30, .y = -30}, "B1"),
              MakeTerminal(23, kNets[1], Point64{.x = 70, .y = 30}, "B2"),
              MakeTerminal(24, kNets[2], Point64{.x = 0, .y = 100}, "C1"),
              MakeTerminal(25, kNets[2], Point64{.x = 100, .y = 100}, "C2"),
              MakeTerminal(26, kNets[3], Point64{.x = 0, .y = 130}, "D1"),
              MakeTerminal(27, kNets[3], Point64{.x = 100, .y = 130}, "D2"),
              MakeTerminal(28, kNets[4], Point64{.x = 0, .y = 160}, "E1"),
              MakeTerminal(29, kNets[4], Point64{.x = 100, .y = 160}, "E2"),
          },
      .obstacles = {},
      .routing_profile =
          RoutingProfile{
              .net = kNets[0],
              .nominal_width = 4,
              .clearance = 1,
              .allowed_layers = {kFront, kBack},
              .allowed_headings = board_ir::kM1HeadingMask,
          },
  };
}

[[nodiscard]] CompilerProfile SequentialCompilerProfile() {
  return CompilerProfile{
      .schema_version = geometry_compiler::kCompilerProfileSchemaVersion,
      .lattice_origin = Point64{.x = 0, .y = 0},
      .lattice_step = 10,
      .tile_width_nodes = 4,
      .tile_height_nodes = 4,
      .compilation_roi =
          AxisAlignedBox64{.min = Point64{.x = 0, .y = -30}, .max = Point64{.x = 100, .y = 160}},
      .active_regions =
          {
              // Shared direct corridor.
              ActiveRegion{.layer = kFront,
                           .bounds = AxisAlignedBox64{.min = Point64{.x = 0, .y = 0},
                                                      .max = Point64{.x = 100, .y = 0}}},
              // FLEX's legal longer alternative; CROSS cannot enter it.
              ActiveRegion{.layer = kFront,
                           .bounds = AxisAlignedBox64{.min = Point64{.x = 0, .y = 60},
                                                      .max = Point64{.x = 100, .y = 60}}},
              ActiveRegion{.layer = kFront,
                           .bounds = AxisAlignedBox64{.min = Point64{.x = 0, .y = 0},
                                                      .max = Point64{.x = 0, .y = 60}}},
              ActiveRegion{.layer = kFront,
                           .bounds = AxisAlignedBox64{.min = Point64{.x = 100, .y = 0},
                                                      .max = Point64{.x = 100, .y = 60}}},
              // CROSS must traverse the middle of the shared corridor.
              ActiveRegion{.layer = kFront,
                           .bounds = AxisAlignedBox64{.min = Point64{.x = 30, .y = -30},
                                                      .max = Point64{.x = 30, .y = 0}}},
              ActiveRegion{.layer = kFront,
                           .bounds = AxisAlignedBox64{.min = Point64{.x = 70, .y = 0},
                                                      .max = Point64{.x = 70, .y = 30}}},
              // Independent capacity-bearing route.
              ActiveRegion{.layer = kFront,
                           .bounds = AxisAlignedBox64{.min = Point64{.x = 0, .y = 100},
                                                      .max = Point64{.x = 100, .y = 100}}},
              // Two represented but disconnected endpoints.
              ActiveRegion{.layer = kFront,
                           .bounds = AxisAlignedBox64{.min = Point64{.x = 0, .y = 130},
                                                      .max = Point64{.x = 0, .y = 130}}},
              ActiveRegion{.layer = kFront,
                           .bounds = AxisAlignedBox64{.min = Point64{.x = 100, .y = 130},
                                                      .max = Point64{.x = 100, .y = 130}}},
              ActiveRegion{.layer = kFront,
                           .bounds = AxisAlignedBox64{.min = Point64{.x = 0, .y = 160},
                                                      .max = Point64{.x = 100, .y = 160}}},
          },
      .heading_mask = static_cast<board_ir::HeadingMask>(board_ir::Heading::kHorizontal) |
                      static_cast<board_ir::HeadingMask>(board_ir::Heading::kVertical),
      .costs = DeterministicCosts{.orthogonal_step = 10, .diagonal_step = 14, .bend = 3},
  };
}

struct SequentialContext {
  board_ir::BoardSnapshot board;
  std::vector<CompiledBoard> compiled;
};

[[nodiscard]] SequentialContext MakeContext(std::uint64_t revision = 1,
                                            CompilerProfile profile = SequentialCompilerProfile()) {
  SequentialContext context{
      .board = test_support::Snapshot(SequentialBoardData(revision)),
      .compiled = {},
  };
  context.compiled.reserve(kNets.size());
  for (EntityRef net : kNets) {
    context.compiled.push_back(test_support::CompilePreparedNet(context.board, net, profile));
  }
  return context;
}

[[nodiscard]] std::vector<CpuSequentialNetRequest> Requests(const SequentialContext& context,
                                                            std::span<const std::size_t> indices) {
  std::vector<CpuSequentialNetRequest> requests;
  requests.reserve(indices.size());
  for (std::size_t index : indices) {
    requests.push_back(CpuSequentialNetRequest{
        .compiled_board = &context.compiled[index],
        .request = test_support::RequestForNet(context.board, kNets[index], kFront, kFront),
    });
  }
  return requests;
}

[[nodiscard]] ResourceCapacityModel Capacity(const SequentialContext& context,
                                             std::uint32_t units = 1) {
  ResourceCapacityModelResult result =
      BuildResourceCapacityModel(context.board, context.compiled.front(), units);
  EXPECT_TRUE(std::holds_alternative<ResourceCapacityModel>(result));
  if (!std::holds_alternative<ResourceCapacityModel>(result)) {
    std::abort();
  }
  return std::get<ResourceCapacityModel>(std::move(result));
}

[[nodiscard]] CpuSequentialNegotiatedRoutingConfig SmallConfig(std::uint32_t passes = 4) {
  return CpuSequentialNegotiatedRoutingConfig{
      .policy =
          CanonicalSequentialRipUpAndReroutePolicyV1{
              .maximum_passes = passes,
              .initial_present_cost = 0,
              .present_cost_increment = 100,
              .historical_cost_increment = 10,
          },
      .limits =
          CpuSequentialNegotiatedRoutingLimits{
              .maximum_nets = 16,
              .maximum_total_attempts = 64,
              .maximum_cpu_work_units_per_query = 1'000'000,
              .maximum_aggregate_cpu_work_units = 64'000'000,
              .maximum_candidate_bytes_per_attempt = 1U * 1024U * 1024U,
              .maximum_aggregate_generated_candidate_bytes = 64U * 1024U * 1024U,
              .maximum_retained_candidate_bytes = 16U * 1024U * 1024U,
              .maximum_expanded_resource_uses_per_candidate = 10'000,
              .maximum_aggregate_expanded_resource_uses = 640'000,
              .maximum_retained_diagnostics = 64,
              .maximum_congestion_cost_value = 1'000'000,
          },
  };
}

[[nodiscard]] CpuSequentialNegotiatedRouting Success(
    const CpuSequentialNegotiatedRoutingResult& result) {
  EXPECT_TRUE(std::holds_alternative<CpuSequentialNegotiatedRouting>(result));
  if (!std::holds_alternative<CpuSequentialNegotiatedRouting>(result)) {
    std::abort();
  }
  return std::get<CpuSequentialNegotiatedRouting>(result);
}

[[nodiscard]] CpuSequentialNegotiatedRoutingError Failure(
    const CpuSequentialNegotiatedRoutingResult& result) {
  EXPECT_TRUE(std::holds_alternative<CpuSequentialNegotiatedRoutingError>(result));
  if (!std::holds_alternative<CpuSequentialNegotiatedRoutingError>(result)) {
    std::abort();
  }
  return std::get<CpuSequentialNegotiatedRoutingError>(result);
}

[[nodiscard]] DirectionDelta OracleStorageDelta(Direction direction) {
  switch (direction) {
    case Direction::kEast:
      return DirectionDelta{.x = 1, .y = 0};
    case Direction::kNorthEast:
      return DirectionDelta{.x = 1, .y = 1};
    case Direction::kNorth:
      return DirectionDelta{.x = 0, .y = 1};
    case Direction::kNorthWest:
      return DirectionDelta{.x = 1, .y = -1};
    case Direction::kWest:
    case Direction::kSouthWest:
    case Direction::kSouth:
    case Direction::kSouthEast:
      break;
  }
  ADD_FAILURE() << "Noncanonical direction reached independent resource oracle";
  return DirectionDelta{};
}

using OracleUses = std::map<EdgeResourceKey, std::uint64_t>;

[[nodiscard]] OracleUses OracleExpand(const RouteCandidate& candidate) {
  OracleUses uses;
  for (const PhysicalEdgeSpan& span : candidate.data().resources) {
    const DirectionDelta delta = OracleStorageDelta(span.direction);
    for (std::uint32_t offset = 0; offset < span.edge_count; ++offset) {
      const EdgeResourceKey resource{
          .layer = span.layer,
          .lattice_x = span.lattice_x + static_cast<std::int64_t>(delta.x) * offset,
          .lattice_y = span.lattice_y + static_cast<std::int64_t>(delta.y) * offset,
          .direction = span.direction,
      };
      uses[resource] += span.usage_units;
    }
  }
  return uses;
}

[[nodiscard]] ResourceAccounting OracleAccounting(
    const ResourceCapacityModel& capacities, std::span<const RouteCandidate* const> candidates) {
  OracleUses usage;
  std::uint64_t expanded = 0;
  for (const RouteCandidate* candidate : candidates) {
    for (const auto& [resource, units] : OracleExpand(*candidate)) {
      usage[resource] += units;
      ++expanded;
    }
  }
  ResourceAccounting accounting{
      .associations = capacities.associations(),
      .resources = {},
      .candidate_count = static_cast<std::uint64_t>(candidates.size()),
      .expanded_resource_uses = expanded,
      .overused_resource_count = 0,
      .total_overuse_units = 0,
  };
  for (const auto& [resource, units] : usage) {
    const std::uint64_t overuse =
        units > capacities.capacity_units() ? units - capacities.capacity_units() : 0;
    accounting.resources.push_back(ResourceUsage{
        .resource = resource,
        .capacity_units = capacities.capacity_units(),
        .usage_units = units,
        .overuse_units = overuse,
    });
    if (overuse != 0) {
      ++accounting.overused_resource_count;
      accounting.total_overuse_units += overuse;
    }
  }
  return accounting;
}

struct OracleResult {
  std::vector<std::pair<candidates::CandidateSignature, candidates::CandidateSignature>> routes;
  ResourceAccounting accounting;
  CpuSequentialTerminationReason termination = CpuSequentialTerminationReason::kPassLimit;
  std::uint32_t completed_passes = 0;
  std::uint64_t attempts = 0;
  std::uint64_t work = 0;
  std::uint64_t snapshot_work = 0;
  std::uint64_t astar_work = 0;
  std::uint64_t maximum_query_work = 0;
};

// Exact-small independent reference. This directly composes Phase 3
// route/build/admit primitives, owns its own canonical state machine, and
// expands admitted spans itself. It never calls the production P4R-05 entry.
[[nodiscard]] OracleResult ExactSmallOracle(const SequentialContext& context,
                                            const ResourceCapacityModel& capacities,
                                            std::span<const CpuSequentialNetRequest> submitted,
                                            const CpuSequentialNegotiatedRoutingConfig& config) {
  struct OracleState {
    const CpuSequentialNetRequest* submitted = nullptr;
    routing::NormalizedCandidateGenerationPolicy base_policy;
    std::optional<RouteCandidate> candidate;
  };
  std::vector<OracleState> states;
  for (const CpuSequentialNetRequest& request : submitted) {
    routing::CandidateGenerationPolicy base = request.request.candidate_policy;
    base.candidate_ordinal = 0;
    routing::CandidatePolicyResult normalized =
        routing::NormalizeCandidateGenerationPolicy(*request.compiled_board, base);
    EXPECT_TRUE(std::holds_alternative<routing::NormalizedCandidateGenerationPolicy>(normalized));
    states.push_back(OracleState{
        .submitted = &request,
        .base_policy =
            std::get<routing::NormalizedCandidateGenerationPolicy>(std::move(normalized)),
        .candidate = std::nullopt,
    });
  }
  std::ranges::sort(states, [](const OracleState& left, const OracleState& right) {
    return std::pair{left.submitted->request.net.id, left.submitted->request.net.generation} <
           std::pair{right.submitted->request.net.id, right.submitted->request.net.generation};
  });

  OracleUses occupancy;
  OracleUses history;
  std::optional<
      std::vector<std::pair<candidates::CandidateSignature, candidates::CandidateSignature>>>
      prior_key;
  OracleResult result;
  for (std::uint32_t pass = 0; pass < config.policy.maximum_passes; ++pass) {
    for (OracleState& state : states) {
      std::uint64_t query_work = 0;
      if (state.candidate.has_value()) {
        for (const auto& [resource, units] : OracleExpand(*state.candidate)) {
          occupancy[resource] -= units;
          if (occupancy[resource] == 0) {
            occupancy.erase(resource);
          }
        }
      }
      routing::CandidateGenerationPolicy policy = state.base_policy.policy;
      policy.candidate_ordinal = pass;
      const std::uint64_t present =
          config.policy.initial_present_cost + pass * config.policy.present_cost_increment;
      std::set<EdgeResourceKey> resources;
      for (const auto& [resource, units] : occupancy) {
        ++query_work;
        static_cast<void>(units);
        if (!std::ranges::binary_search(policy.banned_resources, resource)) {
          resources.insert(resource);
        }
      }
      for (const auto& [resource, cost] : history) {
        ++query_work;
        static_cast<void>(cost);
        if (!std::ranges::binary_search(policy.banned_resources, resource)) {
          resources.insert(resource);
        }
      }
      if (capacities.capacity_units() == 0 && present != 0) {
        constexpr std::array<Direction, 4> kCanonicalDirections = {
            Direction::kEast,
            Direction::kNorthEast,
            Direction::kNorth,
            Direction::kNorthWest,
        };
        const CompiledBoard& compiled = *state.submitted->compiled_board;
        for (const geometry_compiler::SparseTile& tile : compiled.tiles()) {
          for (const geometry_compiler::CompiledNode& node : tile.nodes) {
            const geometry_compiler::LatticeIndex source = geometry_compiler::GlobalLatticeIndex(
                compiled.profile(), tile.key, node.local_index);
            for (Direction direction : kCanonicalDirections) {
              ++query_work;
              const std::optional<EdgeResourceKey> resource =
                  routing::CanonicalPhysicalEdgeResource(tile.key.layer, source, direction);
              if (resource.has_value() && routing::ResourceExists(compiled, *resource) &&
                  !std::ranges::binary_search(policy.banned_resources, *resource)) {
                resources.insert(*resource);
              }
            }
          }
        }
      }
      for (const EdgeResourceKey& resource : resources) {
        ++query_work;
        if (std::ranges::binary_search(policy.banned_resources, resource)) {
          continue;
        }
        const auto occupancy_found = occupancy.find(resource);
        const std::uint64_t predicted =
            (occupancy_found == occupancy.end() ? 0 : occupancy_found->second) + 1;
        const std::uint64_t overuse =
            predicted > capacities.capacity_units() ? predicted - capacities.capacity_units() : 0;
        const auto history_found = history.find(resource);
        const std::uint64_t cost =
            (history_found == history.end() ? 0 : history_found->second) + present * overuse;
        if (cost != 0) {
          policy.resource_penalties.push_back(
              routing::ResourcePenalty{.resource = resource, .additional_cost = cost});
        }
      }
      routing::CandidatePolicyResult normalized =
          routing::NormalizeCandidateGenerationPolicy(*state.submitted->compiled_board, policy);
      EXPECT_TRUE(std::holds_alternative<routing::NormalizedCandidateGenerationPolicy>(normalized));
      const auto& query_policy = std::get<routing::NormalizedCandidateGenerationPolicy>(normalized);
      routing::PlanarRouteRequest query = state.submitted->request;
      query.candidate_policy = query_policy.policy;
      routing::CpuRouteResult route = routing::RouteWithCpuAStar(
          context.board, *state.submitted->compiled_board, query,
          routing::CpuRouteWorkLimits{
              .maximum_work_units = config.limits.maximum_cpu_work_units_per_query,
          });
      EXPECT_TRUE(std::holds_alternative<routing::CpuRoute>(route));
      if (!std::holds_alternative<routing::CpuRoute>(route)) {
        std::abort();
      }
      const std::uint64_t astar_work = std::get<routing::CpuRoute>(route).telemetry.work_units;
      result.snapshot_work += query_work;
      result.astar_work += astar_work;
      query_work += astar_work;
      result.work += query_work;
      result.maximum_query_work = std::max(result.maximum_query_work, query_work);
      ++result.attempts;
      candidates::CandidateDraftBuildResult draft = candidates::BuildGeneratedCandidateFromCpuRoute(
          context.board, *state.submitted->compiled_board, query, query_policy,
          std::get<routing::CpuRoute>(route),
          candidates::CandidateSchedulingIdentity{
              .batch_identity = 17,
              .query_identity = result.attempts,
          });
      EXPECT_TRUE(std::holds_alternative<candidates::GeneratedRouteCandidate>(draft));
      if (!std::holds_alternative<candidates::GeneratedRouteCandidate>(draft)) {
        std::abort();
      }
      candidates::CandidateAdmissionResult admission = candidates::AdmitRouteCandidate(
          candidates::CandidateAdmissionContext{
              .board = context.board,
              .compiled_board = *state.submitted->compiled_board,
              .request = query,
          },
          std::get<candidates::GeneratedRouteCandidate>(std::move(draft)));
      EXPECT_TRUE(std::holds_alternative<RouteCandidate>(admission));
      if (!std::holds_alternative<RouteCandidate>(admission)) {
        std::abort();
      }
      state.candidate = std::get<RouteCandidate>(std::move(admission));
      for (const auto& [resource, units] : OracleExpand(*state.candidate)) {
        occupancy[resource] += units;
      }
    }

    std::vector<const RouteCandidate*> candidates;
    std::vector<std::pair<candidates::CandidateSignature, candidates::CandidateSignature>> key;
    for (const OracleState& state : states) {
      candidates.push_back(&*state.candidate);
      key.emplace_back(state.candidate->data().geometry_signature,
                       state.candidate->data().resource_signature);
    }
    result.accounting = OracleAccounting(capacities, candidates);
    result.completed_passes = pass + 1U;
    if (result.accounting.total_overuse_units == 0) {
      result.termination = CpuSequentialTerminationReason::kFeasible;
      result.routes = std::move(key);
      return result;
    }
    if (prior_key.has_value() && *prior_key == key) {
      result.termination = CpuSequentialTerminationReason::kStalled;
      result.routes = std::move(key);
      return result;
    }
    if (result.completed_passes == config.policy.maximum_passes) {
      result.termination = CpuSequentialTerminationReason::kPassLimit;
      result.routes = std::move(key);
      return result;
    }
    prior_key = key;
    for (const ResourceUsage& use : result.accounting.resources) {
      if (use.overuse_units == 0) {
        continue;
      }
      history[use.resource] += use.overuse_units * config.policy.historical_cost_increment;
    }
  }
  std::abort();
}

[[nodiscard]] std::vector<std::pair<candidates::CandidateSignature, candidates::CandidateSignature>>
FinalRouteSignatures(const CpuSequentialNegotiatedRouting& result) {
  std::vector<std::pair<candidates::CandidateSignature, candidates::CandidateSignature>> routes;
  for (const CpuSequentialNetOutcome& outcome : result.nets) {
    if (const auto* retained = std::get_if<CpuSequentialRetainedRoute>(&outcome);
        retained != nullptr) {
      routes.emplace_back(retained->candidate.data().geometry_signature,
                          retained->candidate.data().resource_signature);
    }
  }
  return routes;
}

TEST(CpuSequentialNegotiatedRoutingTest,
     NegotiationSelectsLegalAlternativeAndMatchesIndependentStateAndAccountingOracle) {
  const SequentialContext context = MakeContext();
  constexpr std::array<std::size_t, 3> kRequested = {0, 1, 2};
  const std::vector<CpuSequentialNetRequest> requests = Requests(context, kRequested);
  const ResourceCapacityModel capacities = Capacity(context);
  const CpuSequentialNegotiatedRoutingConfig config = SmallConfig();

  const CpuSequentialNegotiatedRoutingResult production_result =
      RouteCpuSequentialNegotiated(context.board, capacities, requests, config);
  const CpuSequentialNegotiatedRouting& production = Success(production_result);
  const OracleResult oracle = ExactSmallOracle(context, capacities, requests, config);

  EXPECT_EQ(production.termination_reason, CpuSequentialTerminationReason::kFeasible);
  EXPECT_EQ(production.completed_passes, 2U);
  EXPECT_EQ(production.accounting.total_overuse_units, 0U);
  EXPECT_EQ(production.accounting.candidate_count, 3U);
  EXPECT_EQ(production.accounting, oracle.accounting);
  EXPECT_EQ(FinalRouteSignatures(production), oracle.routes);
  EXPECT_EQ(production.termination_reason, oracle.termination);
  EXPECT_EQ(production.completed_passes, oracle.completed_passes);
  EXPECT_EQ(production.route_attempts, oracle.attempts);
  EXPECT_EQ(production.cpu_work_units, oracle.work);
  EXPECT_GT(oracle.snapshot_work, 0U);
  EXPECT_EQ(oracle.work, oracle.snapshot_work + oracle.astar_work);
  const CpuSequentialRetainedRoute& flex =
      std::get<CpuSequentialRetainedRoute>(production.nets.front());
  EXPECT_TRUE(OracleExpand(flex.candidate)
                  .contains(EdgeResourceKey{
                      .layer = kFront,
                      .lattice_x = 1,
                      .lattice_y = 6,
                      .direction = Direction::kEast,
                  }));

  std::vector<const RouteCandidate*> final_candidates;
  for (const CpuSequentialNetOutcome& outcome : production.nets) {
    final_candidates.push_back(&std::get<CpuSequentialRetainedRoute>(outcome).candidate);
  }
  EXPECT_EQ(production.accounting, OracleAccounting(capacities, final_candidates));

  CpuSequentialNegotiatedRoutingConfig exact_query_work = config;
  exact_query_work.limits.maximum_cpu_work_units_per_query = oracle.maximum_query_work;
  EXPECT_EQ(
      Success(RouteCpuSequentialNegotiated(context.board, capacities, requests, exact_query_work))
          .accounting,
      production.accounting);
  --exact_query_work.limits.maximum_cpu_work_units_per_query;
  EXPECT_EQ(
      Failure(RouteCpuSequentialNegotiated(context.board, capacities, requests, exact_query_work))
          .code,
      CpuSequentialNegotiatedRoutingErrorCode::kBoundExhausted);
}

TEST(CpuSequentialNegotiatedRoutingTest,
     IrreducibleConflictTerminatesStalledWithDeterministicPrecedence) {
  const SequentialContext context = MakeContext();
  constexpr std::array<std::size_t, 2> kRequested = {0, 1};
  std::vector<CpuSequentialNetRequest> requests = Requests(context, kRequested);
  requests.front().request.candidate_policy.banned_resources.push_back(EdgeResourceKey{
      .layer = kFront,
      .lattice_x = 1,
      .lattice_y = 6,
      .direction = Direction::kEast,
  });
  CpuSequentialNegotiatedRoutingConfig config = SmallConfig(2);
  const CpuSequentialNegotiatedRouting& result =
      Success(RouteCpuSequentialNegotiated(context.board, Capacity(context), requests, config));
  const OracleResult oracle = ExactSmallOracle(context, Capacity(context), requests, config);

  EXPECT_EQ(result.termination_reason, CpuSequentialTerminationReason::kStalled);
  EXPECT_EQ(result.completed_passes, 2U);
  EXPECT_GT(result.accounting.total_overuse_units, 0U);
  EXPECT_EQ(result.termination_reason, oracle.termination);
  EXPECT_EQ(result.completed_passes, oracle.completed_passes);
  EXPECT_EQ(result.accounting, oracle.accounting);
  EXPECT_EQ(FinalRouteSignatures(result), oracle.routes);

  config.policy.maximum_passes = 1;
  const CpuSequentialNegotiatedRouting& pass_limited =
      Success(RouteCpuSequentialNegotiated(context.board, Capacity(context), requests, config));
  EXPECT_EQ(pass_limited.termination_reason, CpuSequentialTerminationReason::kPassLimit);

  constexpr std::array<std::size_t, 1> kDisjoint = {2};
  const std::vector<CpuSequentialNetRequest> disjoint = Requests(context, kDisjoint);
  const CpuSequentialNegotiatedRouting& feasible_on_last_pass =
      Success(RouteCpuSequentialNegotiated(context.board, Capacity(context), disjoint, config));
  EXPECT_EQ(feasible_on_last_pass.termination_reason, CpuSequentialTerminationReason::kFeasible);
}

void CorruptBuiltRoute(std::size_t, std::uint32_t, EntityRef net, routing::CpuRoute& route, void*) {
  if (net == kNets[0]) {
    ++route.total_cost;
  }
}

void CorruptGeneratedDraft(std::size_t, std::uint32_t, EntityRef net,
                           candidates::CandidateDraftBuildResult& draft, void*) {
  if (net == kNets[1] && std::holds_alternative<candidates::GeneratedRouteCandidate>(draft)) {
    ++std::get<candidates::GeneratedRouteCandidate>(draft).metrics.bend_count;
  }
}

TEST(CpuSequentialNegotiatedRoutingTest,
     OrdinaryRouteBuildAndAdmissionOutcomesAreStructuredAndNonfatal) {
  const SequentialContext context = MakeContext();
  constexpr std::array<std::size_t, 4> kRequested = {0, 1, 3, 4};
  std::vector<CpuSequentialNetRequest> requests = Requests(context, kRequested);
  requests.back().request.goal_layer = kBack;
  internal::CpuSequentialNegotiatedRoutingTestHooks hooks{
      .context = nullptr,
      .after_cpu_route = &CorruptBuiltRoute,
      .after_candidate_build = &CorruptGeneratedDraft,
      .injected_failure = nullptr,
  };
  const CpuSequentialNegotiatedRouting& result =
      Success(internal::RouteCpuSequentialNegotiatedWithTestHooks(context.board, Capacity(context),
                                                                  requests, SmallConfig(1), hooks));

  ASSERT_EQ(result.nets.size(), 4U);
  EXPECT_EQ(std::get<CpuSequentialRouteAbsence>(result.nets[0]).reason,
            CpuSequentialAbsenceReason::kCandidateBuildRejected);
  EXPECT_EQ(std::get<CpuSequentialRouteAbsence>(result.nets[1]).reason,
            CpuSequentialAbsenceReason::kAdmissionRejected);
  EXPECT_EQ(std::get<CpuSequentialRouteAbsence>(result.nets[2]).reason,
            CpuSequentialAbsenceReason::kDisconnected);
  EXPECT_EQ(std::get<CpuSequentialRouteAbsence>(result.nets[3]).reason,
            CpuSequentialAbsenceReason::kUnsupported);
  EXPECT_EQ(result.diagnostics.size(), 4U);
  EXPECT_EQ(result.accounting.candidate_count, 0U);
  EXPECT_EQ(result.termination_reason, CpuSequentialTerminationReason::kPassLimit);
}

struct RejectOnSecondPassContext {
  bool saw_second_pass = false;
};

void RejectSecondPassRoute(std::size_t, std::uint32_t pass, EntityRef, routing::CpuRoute& route,
                           void* raw_context) {
  auto* context = static_cast<RejectOnSecondPassContext*>(raw_context);
  if (pass == 1) {
    context->saw_second_pass = true;
    ++route.total_cost;
  }
}

TEST(CpuSequentialNegotiatedRoutingTest, RejectedRerouteRestoresAndRetainsTheRemovedIncumbent) {
  const SequentialContext context = MakeContext();
  constexpr std::array<std::size_t, 1> kRequested = {2};
  const std::vector<CpuSequentialNetRequest> requests = Requests(context, kRequested);
  RejectOnSecondPassContext hook_context;
  internal::CpuSequentialNegotiatedRoutingTestHooks hooks{
      .context = &hook_context,
      .after_cpu_route = &RejectSecondPassRoute,
      .after_candidate_build = nullptr,
      .injected_failure = nullptr,
  };
  const CpuSequentialNegotiatedRouting& result =
      Success(internal::RouteCpuSequentialNegotiatedWithTestHooks(
          context.board, Capacity(context, 0), requests, SmallConfig(3), hooks));

  EXPECT_TRUE(hook_context.saw_second_pass);
  ASSERT_TRUE(std::holds_alternative<CpuSequentialRetainedRoute>(result.nets.front()));
  const CpuSequentialRetainedRoute& retained =
      std::get<CpuSequentialRetainedRoute>(result.nets.front());
  EXPECT_EQ(retained.installed_pass_index, 0U);
  ASSERT_FALSE(result.diagnostics.empty());
  EXPECT_TRUE(result.diagnostics.back().incumbent_retained);
  EXPECT_EQ(result.diagnostics.back().outcome,
            CpuSequentialAttemptOutcome::kCandidateBuildRejected);
  EXPECT_EQ(result.accounting,
            OracleAccounting(Capacity(context, 0),
                             std::array<const RouteCandidate*, 1>{&retained.candidate}));
}

void RejectSecondPassAdmission(std::size_t, std::uint32_t pass, EntityRef,
                               candidates::CandidateDraftBuildResult& draft, void*) {
  if (pass == 1 && std::holds_alternative<candidates::GeneratedRouteCandidate>(draft)) {
    ++std::get<candidates::GeneratedRouteCandidate>(draft).metrics.bend_count;
  }
}

TEST(CpuSequentialNegotiatedRoutingTest,
     AdmissionRejectedRerouteAlsoRestoresAndRetainsTheRemovedIncumbent) {
  const SequentialContext context = MakeContext();
  constexpr std::array<std::size_t, 1> kRequested = {2};
  const std::vector<CpuSequentialNetRequest> requests = Requests(context, kRequested);
  internal::CpuSequentialNegotiatedRoutingTestHooks hooks{
      .context = nullptr,
      .after_cpu_route = nullptr,
      .after_candidate_build = &RejectSecondPassAdmission,
      .injected_failure = nullptr,
  };
  const CpuSequentialNegotiatedRouting& result =
      Success(internal::RouteCpuSequentialNegotiatedWithTestHooks(
          context.board, Capacity(context, 0), requests, SmallConfig(3), hooks));

  const CpuSequentialRetainedRoute& retained =
      std::get<CpuSequentialRetainedRoute>(result.nets.front());
  EXPECT_EQ(retained.installed_pass_index, 0U);
  ASSERT_FALSE(result.diagnostics.empty());
  EXPECT_TRUE(result.diagnostics.back().incumbent_retained);
  EXPECT_EQ(result.diagnostics.back().outcome, CpuSequentialAttemptOutcome::kAdmissionRejected);
}

TEST(CpuSequentialNegotiatedRoutingTest,
     ZeroCapacitySnapshotPricesAReachableEdgeAbsentFromOccupancyAndHistory) {
  const SequentialContext context = MakeContext();
  constexpr std::array<std::size_t, 1> kRequested = {0};
  std::vector<CpuSequentialNetRequest> requests = Requests(context, kRequested);
  const EdgeResourceKey unused_alternative{
      .layer = kFront,
      .lattice_x = 1,
      .lattice_y = 6,
      .direction = Direction::kEast,
  };
  requests.front().request.candidate_policy.resource_penalties.push_back(
      routing::ResourcePenalty{.resource = unused_alternative, .additional_cost = 5});
  CpuSequentialNegotiatedRoutingConfig config = SmallConfig(1);
  config.policy.initial_present_cost = 7;
  config.limits.maximum_congestion_cost_value = 12;
  const CpuSequentialNegotiatedRouting& result =
      Success(RouteCpuSequentialNegotiated(context.board, Capacity(context, 0), requests, config));

  const CpuSequentialRetainedRoute& retained =
      std::get<CpuSequentialRetainedRoute>(result.nets.front());
  const auto penalty = std::ranges::find(retained.candidate.data().policy.resource_penalties,
                                         unused_alternative, &routing::ResourcePenalty::resource);
  ASSERT_NE(penalty, retained.candidate.data().policy.resource_penalties.end());
  EXPECT_EQ(penalty->additional_cost, 12U);

  config.limits.maximum_cpu_work_units_per_query = result.cpu_work_units;
  config.limits.maximum_aggregate_cpu_work_units = result.cpu_work_units;
  EXPECT_EQ(
      Success(RouteCpuSequentialNegotiated(context.board, Capacity(context, 0), requests, config))
          .cpu_work_units,
      result.cpu_work_units);
  --config.limits.maximum_cpu_work_units_per_query;
  EXPECT_EQ(
      Failure(RouteCpuSequentialNegotiated(context.board, Capacity(context, 0), requests, config))
          .code,
      CpuSequentialNegotiatedRoutingErrorCode::kBoundExhausted);

  config.limits.maximum_cpu_work_units_per_query = result.cpu_work_units;
  config.limits.maximum_congestion_cost_value = 11;
  EXPECT_EQ(
      Failure(RouteCpuSequentialNegotiated(context.board, Capacity(context, 0), requests, config))
          .code,
      CpuSequentialNegotiatedRoutingErrorCode::kBoundExhausted);
}

TEST(CpuSequentialNegotiatedRoutingTest,
     NetInputOrderPointerIdentityAndRepeatedExecutionAreNotSemantic) {
  const SequentialContext context = MakeContext();
  constexpr std::array<std::size_t, 3> kRequested = {0, 1, 2};
  std::vector<CpuSequentialNetRequest> requests = Requests(context, kRequested);
  const ResourceCapacityModel capacities = Capacity(context);
  const CpuSequentialNegotiatedRouting baseline =
      Success(RouteCpuSequentialNegotiated(context.board, capacities, requests, SmallConfig()));
  EXPECT_EQ(
      Success(RouteCpuSequentialNegotiated(context.board, capacities, requests, SmallConfig())),
      baseline);

  std::ranges::reverse(requests);
  EXPECT_EQ(
      Success(RouteCpuSequentialNegotiated(context.board, capacities, requests, SmallConfig())),
      baseline);

  SequentialContext copied_context = context;
  std::vector<CpuSequentialNetRequest> copied_requests = Requests(copied_context, kRequested);
  EXPECT_EQ(Success(RouteCpuSequentialNegotiated(copied_context.board, capacities, copied_requests,
                                                 SmallConfig())),
            baseline);
}

TEST(CpuSequentialNegotiatedRoutingTest,
     InvalidNetsAndBoardCompilerCapacityAssociationDriftFailBeforeRouting) {
  const SequentialContext context = MakeContext();
  constexpr std::array<std::size_t, 1> kOne = {0};
  const std::vector<CpuSequentialNetRequest> one = Requests(context, kOne);
  const ResourceCapacityModel capacities = Capacity(context);

  CpuSequentialNegotiatedRoutingConfig invalid_config = SmallConfig();
  invalid_config.policy.maximum_passes = 0;
  EXPECT_EQ(
      Failure(RouteCpuSequentialNegotiated(context.board, capacities, one, invalid_config)).code,
      CpuSequentialNegotiatedRoutingErrorCode::kInvalidConfiguration);

  std::vector<CpuSequentialNetRequest> missing_compiler = one;
  missing_compiler.front().compiled_board = nullptr;
  EXPECT_EQ(Failure(RouteCpuSequentialNegotiated(context.board, capacities, missing_compiler,
                                                 SmallConfig()))
                .code,
            CpuSequentialNegotiatedRoutingErrorCode::kInvalidInput);

  std::vector<CpuSequentialNetRequest> duplicate = one;
  duplicate.push_back(one.front());
  EXPECT_EQ(
      Failure(RouteCpuSequentialNegotiated(context.board, capacities, duplicate, SmallConfig()))
          .code,
      CpuSequentialNegotiatedRoutingErrorCode::kInvalidInput);

  std::vector<CpuSequentialNetRequest> unknown = one;
  unknown.front().request.net = EntityRef{.id = 999, .generation = 0};
  EXPECT_EQ(
      Failure(RouteCpuSequentialNegotiated(context.board, capacities, unknown, SmallConfig())).code,
      CpuSequentialNegotiatedRoutingErrorCode::kInvalidInput);

  const SequentialContext changed_board = MakeContext(2);
  EXPECT_EQ(
      Failure(RouteCpuSequentialNegotiated(changed_board.board, capacities, one, SmallConfig()))
          .code,
      CpuSequentialNegotiatedRoutingErrorCode::kAssociationMismatch);

  CompilerProfile changed_profile = SequentialCompilerProfile();
  changed_profile.tile_width_nodes = 8;
  const SequentialContext changed_compiler = MakeContext(1, changed_profile);
  std::vector<CpuSequentialNetRequest> changed_requests = Requests(changed_compiler, kOne);
  EXPECT_EQ(Failure(RouteCpuSequentialNegotiated(context.board, capacities, changed_requests,
                                                 SmallConfig()))
                .code,
            CpuSequentialNegotiatedRoutingErrorCode::kAssociationMismatch);
}

TEST(CpuSequentialNegotiatedRoutingTest,
     OversizedCallerPolicyIsRejectedBeforeTheBaselineTakesAnOwnedCopy) {
  const SequentialContext context = MakeContext();
  constexpr std::array<std::size_t, 1> kOne = {0};
  std::vector<CpuSequentialNetRequest> one = Requests(context, kOne);
  one.front().request.candidate_policy.banned_resources.resize(
      routing::kMaximumPolicyResourceEntries + 1U);

  const CpuSequentialNegotiatedRoutingError& error =
      Failure(RouteCpuSequentialNegotiated(context.board, Capacity(context), one, SmallConfig()));
  EXPECT_EQ(error.code, CpuSequentialNegotiatedRoutingErrorCode::kBoundExhausted);
  EXPECT_EQ(error.invariant_id, "allocator.cpu_sequential.base_policy_shape_bound.v1");
  EXPECT_EQ(error.expected_value, routing::kMaximumPolicyResourceEntries);
  EXPECT_EQ(error.actual_value, routing::kMaximumPolicyResourceEntries + 1U);
  EXPECT_EQ(error.policy_error_code, routing::CandidatePolicyErrorCode::kTooManyResources);
}

TEST(CpuSequentialNegotiatedRoutingTest,
     ExactConfiguredBoundsAcceptEqualityAndOneUnderFailsDeterministically) {
  const SequentialContext context = MakeContext();
  constexpr std::array<std::size_t, 1> kOne = {2};
  const std::vector<CpuSequentialNetRequest> one = Requests(context, kOne);
  const ResourceCapacityModel capacities = Capacity(context);
  CpuSequentialNegotiatedRoutingConfig generous = SmallConfig(1);
  const CpuSequentialNegotiatedRouting observed =
      Success(RouteCpuSequentialNegotiated(context.board, capacities, one, generous));

  CpuSequentialNegotiatedRoutingConfig exact = generous;
  exact.limits.maximum_nets = 1;
  exact.limits.maximum_total_attempts = 1;
  exact.limits.maximum_cpu_work_units_per_query = observed.cpu_work_units;
  exact.limits.maximum_aggregate_cpu_work_units = observed.cpu_work_units;
  exact.limits.maximum_candidate_bytes_per_attempt = observed.generated_candidate_bytes;
  exact.limits.maximum_aggregate_generated_candidate_bytes = observed.generated_candidate_bytes;
  exact.limits.maximum_retained_candidate_bytes = observed.retained_candidate_bytes;
  exact.limits.maximum_expanded_resource_uses_per_candidate = observed.expanded_resource_uses;
  exact.limits.maximum_aggregate_expanded_resource_uses = observed.expanded_resource_uses;
  exact.limits.maximum_retained_diagnostics = 0;
  exact.limits.maximum_congestion_cost_value = 0;
  const CpuSequentialNegotiatedRouting bounded =
      Success(RouteCpuSequentialNegotiated(context.board, capacities, one, exact));
  EXPECT_EQ(bounded.termination_reason, observed.termination_reason);
  EXPECT_EQ(bounded.completed_passes, observed.completed_passes);
  EXPECT_EQ(bounded.route_attempts, observed.route_attempts);
  EXPECT_EQ(bounded.cpu_work_units, observed.cpu_work_units);
  EXPECT_EQ(bounded.generated_candidate_bytes, observed.generated_candidate_bytes);
  EXPECT_EQ(bounded.retained_candidate_bytes, observed.retained_candidate_bytes);
  EXPECT_EQ(bounded.expanded_resource_uses, observed.expanded_resource_uses);
  EXPECT_EQ(bounded.accounting, observed.accounting);

  CpuSequentialNegotiatedRoutingConfig one_under = exact;
  --one_under.limits.maximum_cpu_work_units_per_query;
  EXPECT_EQ(Failure(RouteCpuSequentialNegotiated(context.board, capacities, one, one_under)).code,
            CpuSequentialNegotiatedRoutingErrorCode::kBoundExhausted);
  one_under = exact;
  --one_under.limits.maximum_aggregate_cpu_work_units;
  EXPECT_EQ(Failure(RouteCpuSequentialNegotiated(context.board, capacities, one, one_under)).code,
            CpuSequentialNegotiatedRoutingErrorCode::kBoundExhausted);
  one_under = exact;
  --one_under.limits.maximum_candidate_bytes_per_attempt;
  EXPECT_EQ(Failure(RouteCpuSequentialNegotiated(context.board, capacities, one, one_under)).code,
            CpuSequentialNegotiatedRoutingErrorCode::kBoundExhausted);
  one_under = exact;
  --one_under.limits.maximum_aggregate_generated_candidate_bytes;
  EXPECT_EQ(Failure(RouteCpuSequentialNegotiated(context.board, capacities, one, one_under)).code,
            CpuSequentialNegotiatedRoutingErrorCode::kBoundExhausted);
  one_under = exact;
  --one_under.limits.maximum_retained_candidate_bytes;
  EXPECT_EQ(Failure(RouteCpuSequentialNegotiated(context.board, capacities, one, one_under)).code,
            CpuSequentialNegotiatedRoutingErrorCode::kBoundExhausted);
  one_under = exact;
  --one_under.limits.maximum_expanded_resource_uses_per_candidate;
  EXPECT_EQ(Failure(RouteCpuSequentialNegotiated(context.board, capacities, one, one_under)).code,
            CpuSequentialNegotiatedRoutingErrorCode::kBoundExhausted);
  one_under = exact;
  --one_under.limits.maximum_aggregate_expanded_resource_uses;
  EXPECT_EQ(Failure(RouteCpuSequentialNegotiated(context.board, capacities, one, one_under)).code,
            CpuSequentialNegotiatedRoutingErrorCode::kBoundExhausted);
}

TEST(CpuSequentialNegotiatedRoutingTest,
     CountDiagnosticAndCongestionBoundsAcceptEqualityAndRejectOneUnder) {
  const SequentialContext context = MakeContext();
  constexpr std::array<std::size_t, 2> kTwo = {0, 1};
  const std::vector<CpuSequentialNetRequest> two = Requests(context, kTwo);
  CpuSequentialNegotiatedRoutingConfig exact = SmallConfig(2);
  exact.limits.maximum_nets = 2;
  exact.limits.maximum_total_attempts = 4;
  exact.limits.maximum_congestion_cost_value = 220;
  EXPECT_TRUE(std::holds_alternative<CpuSequentialNegotiatedRouting>(
      RouteCpuSequentialNegotiated(context.board, Capacity(context), two, exact)));

  CpuSequentialNegotiatedRoutingConfig one_under = exact;
  one_under.limits.maximum_nets = 1;
  EXPECT_EQ(
      Failure(RouteCpuSequentialNegotiated(context.board, Capacity(context), two, one_under)).code,
      CpuSequentialNegotiatedRoutingErrorCode::kBoundExhausted);
  one_under = exact;
  one_under.limits.maximum_total_attempts = 3;
  EXPECT_EQ(
      Failure(RouteCpuSequentialNegotiated(context.board, Capacity(context), two, one_under)).code,
      CpuSequentialNegotiatedRoutingErrorCode::kBoundExhausted);
  one_under = exact;
  one_under.limits.maximum_congestion_cost_value = 219;
  EXPECT_EQ(
      Failure(RouteCpuSequentialNegotiated(context.board, Capacity(context), two, one_under)).code,
      CpuSequentialNegotiatedRoutingErrorCode::kBoundExhausted);

  constexpr std::array<std::size_t, 1> kDisconnected = {3};
  const std::vector<CpuSequentialNetRequest> disconnected = Requests(context, kDisconnected);
  CpuSequentialNegotiatedRoutingConfig diagnostic_exact = SmallConfig(1);
  diagnostic_exact.limits.maximum_retained_diagnostics = 1;
  EXPECT_EQ(Success(RouteCpuSequentialNegotiated(context.board, Capacity(context), disconnected,
                                                 diagnostic_exact))
                .diagnostics.size(),
            1U);
  diagnostic_exact.limits.maximum_retained_diagnostics = 0;
  EXPECT_EQ(Failure(RouteCpuSequentialNegotiated(context.board, Capacity(context), disconnected,
                                                 diagnostic_exact))
                .code,
            CpuSequentialNegotiatedRoutingErrorCode::kBoundExhausted);

  constexpr std::array<std::size_t, 1> kOne = {0};
  std::vector<CpuSequentialNetRequest> penalized = Requests(context, kOne);
  penalized.front().request.candidate_policy.resource_penalties.push_back(routing::ResourcePenalty{
      .resource =
          EdgeResourceKey{
              .layer = kFront,
              .lattice_x = 0,
              .lattice_y = 0,
              .direction = Direction::kEast,
          },
      .additional_cost = 5,
  });
  CpuSequentialNegotiatedRoutingConfig base_penalty_exact = SmallConfig(1);
  base_penalty_exact.limits.maximum_congestion_cost_value = 5;
  EXPECT_TRUE(std::holds_alternative<CpuSequentialNegotiatedRouting>(RouteCpuSequentialNegotiated(
      context.board, Capacity(context), penalized, base_penalty_exact)));
  base_penalty_exact.limits.maximum_congestion_cost_value = 4;
  EXPECT_EQ(Failure(RouteCpuSequentialNegotiated(context.board, Capacity(context), penalized,
                                                 base_penalty_exact))
                .code,
            CpuSequentialNegotiatedRoutingErrorCode::kBoundExhausted);
}

TEST(CpuSequentialNegotiatedRoutingTest,
     AggregateWorkAndCongestionScheduleOverflowReturnTypedFailure) {
  const SequentialContext context = MakeContext();
  constexpr std::array<std::size_t, 2> kTwo = {0, 1};
  const std::vector<CpuSequentialNetRequest> two = Requests(context, kTwo);

  CpuSequentialNegotiatedRoutingConfig work_overflow = SmallConfig(1);
  work_overflow.limits.maximum_cpu_work_units_per_query = std::numeric_limits<std::uint64_t>::max();
  work_overflow.limits.maximum_aggregate_cpu_work_units = std::numeric_limits<std::uint64_t>::max();
  EXPECT_EQ(
      Failure(RouteCpuSequentialNegotiated(context.board, Capacity(context), two, work_overflow))
          .code,
      CpuSequentialNegotiatedRoutingErrorCode::kArithmeticOverflow);

  CpuSequentialNegotiatedRoutingConfig cost_overflow = SmallConfig(2);
  cost_overflow.policy.present_cost_increment = std::numeric_limits<std::uint64_t>::max();
  cost_overflow.limits.maximum_congestion_cost_value = std::numeric_limits<std::uint64_t>::max();
  EXPECT_EQ(
      Failure(RouteCpuSequentialNegotiated(context.board, Capacity(context), two, cost_overflow))
          .code,
      CpuSequentialNegotiatedRoutingErrorCode::kArithmeticOverflow);
}

internal::CpuSequentialInjectedFailure InjectFatalOnSecondAttempt(std::size_t attempt,
                                                                  std::uint32_t, EntityRef,
                                                                  void* raw_context) {
  const auto failure = *static_cast<internal::CpuSequentialInjectedFailure*>(raw_context);
  return attempt == 1 ? failure : internal::CpuSequentialInjectedFailure::kNone;
}

TEST(CpuSequentialNegotiatedRoutingTest,
     FatalResourceAndInvariantFailuresNeverExposePartialBoardOutcome) {
  const SequentialContext context = MakeContext();
  constexpr std::array<std::size_t, 2> kTwo = {0, 2};
  const std::vector<CpuSequentialNetRequest> two = Requests(context, kTwo);
  for (internal::CpuSequentialInjectedFailure injected :
       {internal::CpuSequentialInjectedFailure::kResourceExhausted,
        internal::CpuSequentialInjectedFailure::kInternalInvariant}) {
    internal::CpuSequentialNegotiatedRoutingTestHooks hooks{
        .context = &injected,
        .after_cpu_route = nullptr,
        .after_candidate_build = nullptr,
        .injected_failure = &InjectFatalOnSecondAttempt,
    };
    const CpuSequentialNegotiatedRoutingResult result =
        internal::RouteCpuSequentialNegotiatedWithTestHooks(context.board, Capacity(context), two,
                                                            SmallConfig(1), hooks);
    ASSERT_TRUE(std::holds_alternative<CpuSequentialNegotiatedRoutingError>(result));
    EXPECT_EQ(std::get<CpuSequentialNegotiatedRoutingError>(result).code,
              injected == internal::CpuSequentialInjectedFailure::kResourceExhausted
                  ? CpuSequentialNegotiatedRoutingErrorCode::kResourceExhausted
                  : CpuSequentialNegotiatedRoutingErrorCode::kInternalInvariant);
  }
}

TEST(CpuSequentialNegotiatedRoutingTest,
     BoardCompilerCapacityAndAdmittedCandidatesRemainImmutable) {
  const SequentialContext context = MakeContext();
  constexpr std::array<std::size_t, 3> kRequested = {0, 1, 2};
  const std::vector<CpuSequentialNetRequest> requests = Requests(context, kRequested);
  const BoardData board_before = context.board.data();
  const std::vector<CompiledBoard> compiled_before = context.compiled;
  const ResourceCapacityModel capacities = Capacity(context);
  const ResourceCapacityModel capacity_before = capacities;

  const CpuSequentialNegotiatedRouting& result =
      Success(RouteCpuSequentialNegotiated(context.board, capacities, requests, SmallConfig()));
  std::vector<candidates::GeneratedRouteCandidate> candidates_before;
  for (const CpuSequentialNetOutcome& outcome : result.nets) {
    candidates_before.push_back(std::get<CpuSequentialRetainedRoute>(outcome).candidate.data());
  }
  std::vector<const RouteCandidate*> candidates;
  for (const CpuSequentialNetOutcome& outcome : result.nets) {
    candidates.push_back(&std::get<CpuSequentialRetainedRoute>(outcome).candidate);
  }
  EXPECT_EQ(OracleAccounting(capacities, candidates), result.accounting);

  EXPECT_EQ(context.board.data(), board_before);
  EXPECT_EQ(context.compiled, compiled_before);
  EXPECT_EQ(capacities, capacity_before);
  for (std::size_t index = 0; index < result.nets.size(); ++index) {
    EXPECT_EQ(std::get<CpuSequentialRetainedRoute>(result.nets[index]).candidate.data(),
              candidates_before[index]);
  }
}

}  // namespace
}  // namespace apgar::allocator
