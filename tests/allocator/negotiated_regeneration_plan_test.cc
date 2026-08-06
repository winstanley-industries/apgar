#include "apgar/allocator/negotiated_regeneration_plan.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <map>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

#include "apgar/allocator/one_world_selection.h"
#include "apgar/allocator/resource_accounting.h"
#include "apgar/board_ir/board.h"
#include "apgar/candidates/route_candidate.h"
#include "apgar/geometry_compiler/compiled_board.h"
#include "apgar/routing/candidate_policy.h"
#include "apgar/routing/planar_route.h"
#include "tests/support/candidate_builder.h"
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
using candidates::CandidateMetrics;
using candidates::PhysicalEdgeSpan;
using candidates::RouteCandidate;
using geometry_compiler::ActiveRegion;
using geometry_compiler::CompiledBoard;
using geometry_compiler::CompilerProfile;
using geometry_compiler::DeterministicCosts;
using geometry_compiler::Direction;
using geometry_compiler::DirectionDelta;
using routing::EdgeResourceKey;

using UWide = __uint128_t;

inline constexpr board_ir::LayerId kLayer = 0;
inline constexpr std::array<EntityRef, 4> kNets = {
    EntityRef{.id = 10, .generation = 0},
    EntityRef{.id = 11, .generation = 0},
    EntityRef{.id = 12, .generation = 0},
    EntityRef{.id = 13, .generation = 0},
};

[[nodiscard]] AxisAlignedBox64 TerminalBox(Point64 center) {
  return AxisAlignedBox64{
      .min = Point64{.x = center.x - 1, .y = center.y - 1},
      .max = Point64{.x = center.x + 1, .y = center.y + 1},
  };
}

[[nodiscard]] Terminal MakeTerminal(std::uint64_t id, EntityRef net, Point64 center,
                                    std::string pin) {
  return Terminal{
      .ref = EntityRef{.id = id, .generation = 0},
      .net = net,
      .component = "P4R06",
      .pin = std::move(pin),
      .center = center,
      .connection_region = TerminalBox(center),
      .layers = {kLayer},
  };
}

[[nodiscard]] BoardData PlannerBoard(std::uint64_t revision = 1) {
  const std::array<EntityRef, 8> terminals = {
      EntityRef{.id = 20, .generation = 0}, EntityRef{.id = 21, .generation = 0},
      EntityRef{.id = 22, .generation = 0}, EntityRef{.id = 23, .generation = 0},
      EntityRef{.id = 24, .generation = 0}, EntityRef{.id = 25, .generation = 0},
      EntityRef{.id = 26, .generation = 0}, EntityRef{.id = 27, .generation = 0},
  };
  return BoardData{
      .schema_version = board_ir::kBoardSchemaVersion,
      .dbu_per_millimeter = 1'000'000,
      .revision = revision,
      .adapter_name = "p4r06-exact-small",
      .adapter_version = "1",
      .layers =
          {
              Layer{
                  .ref = EntityRef{.id = 1, .generation = 0},
                  .routing_id = kLayer,
                  .name = "front",
                  .physical_order = 0,
                  .type = LayerType::kSignal,
                  .routable = true,
              },
              Layer{
                  .ref = EntityRef{.id = 2, .generation = 0},
                  .routing_id = 31,
                  .name = "back",
                  .physical_order = 1,
                  .type = LayerType::kSignal,
                  .routable = true,
              },
          },
      .nets =
          {
              Net{.ref = kNets[0], .name = "A", .terminals = {terminals[0], terminals[1]}},
              Net{.ref = kNets[1], .name = "B", .terminals = {terminals[2], terminals[3]}},
              Net{.ref = kNets[2], .name = "C", .terminals = {terminals[4], terminals[5]}},
              Net{.ref = kNets[3], .name = "EMPTY", .terminals = {terminals[6], terminals[7]}},
          },
      .terminals =
          {
              MakeTerminal(20, kNets[0], Point64{.x = 0, .y = 0}, "A1"),
              MakeTerminal(21, kNets[0], Point64{.x = 40, .y = 0}, "A2"),
              MakeTerminal(22, kNets[1], Point64{.x = 10, .y = -20}, "B1"),
              MakeTerminal(23, kNets[1], Point64{.x = 30, .y = 20}, "B2"),
              MakeTerminal(24, kNets[2], Point64{.x = 0, .y = 30}, "C1"),
              MakeTerminal(25, kNets[2], Point64{.x = 40, .y = 30}, "C2"),
              MakeTerminal(26, kNets[3], Point64{.x = 0, .y = 10}, "D1"),
              MakeTerminal(27, kNets[3], Point64{.x = 40, .y = 10}, "D2"),
          },
      .obstacles = {},
      .routing_profile =
          RoutingProfile{
              .net = kNets[0],
              .nominal_width = 2,
              .clearance = 1,
              .allowed_layers = {kLayer, 31},
              .allowed_headings = board_ir::kM1HeadingMask,
          },
  };
}

[[nodiscard]] CompilerProfile PlannerProfile() {
  return CompilerProfile{
      .schema_version = geometry_compiler::kCompilerProfileSchemaVersion,
      .lattice_origin = Point64{.x = 0, .y = 0},
      .lattice_step = 10,
      .tile_width_nodes = 8,
      .tile_height_nodes = 8,
      .compilation_roi =
          AxisAlignedBox64{.min = Point64{.x = 0, .y = -20}, .max = Point64{.x = 40, .y = 30}},
      .active_regions =
          {
              ActiveRegion{
                  .layer = kLayer,
                  .bounds = AxisAlignedBox64{.min = Point64{.x = 0, .y = -20},
                                             .max = Point64{.x = 40, .y = 30}},
              },
          },
      .heading_mask = board_ir::kM1HeadingMask,
      .costs = DeterministicCosts{.orthogonal_step = 10, .diagonal_step = 14, .bend = 3},
  };
}

struct Microcase {
  board_ir::BoardSnapshot board;
  std::vector<CompiledBoard> compiled;
  std::vector<RouteCandidate> candidates;
};

[[nodiscard]] RouteCandidate Admit(const board_ir::BoardSnapshot& board,
                                   const CompiledBoard& compiled, routing::CpuRouteRequest request,
                                   std::uint64_t query_identity) {
  return test_support::AcceptedCandidate(
      candidates::CandidateAdmissionContext{
          .board = board,
          .compiled_board = compiled,
          .request = request,
      },
      test_support::CandidateDraft(board, compiled, request, 0x4600, query_identity));
}

[[nodiscard]] RouteCandidate AdmitSharedRoute(const board_ir::BoardSnapshot& board,
                                              const CompiledBoard& compiled,
                                              const routing::CpuRouteRequest& request,
                                              std::uint64_t query_identity) {
  const routing::NormalizedCandidateGenerationPolicy policy =
      test_support::NormalizePolicy(compiled, request);
  const candidates::CandidateAssociations associations =
      candidates::AssociationsFor(board, compiled);
  routing::CpuRoute route{
      .source_board_content_hash = associations.board_content_hash,
      .compiler_profile_fingerprint = associations.compiler_profile_fingerprint,
      .compiler_version = associations.geometry_compiler_version,
      .rule_bucket_identity = associations.rule_bucket_identity,
      .candidate_policy_identity = policy.identity,
      .total_cost = 66,
      .lattice_path = {},
      .segments =
          {
              routing::LayerSegment{
                  .layer = kLayer,
                  .centerline = board_ir::Segment64{.start = Point64{.x = 10, .y = -20},
                                                    .end = Point64{.x = 10, .y = 0}},
              },
              routing::LayerSegment{
                  .layer = kLayer,
                  .centerline = board_ir::Segment64{.start = Point64{.x = 10, .y = 0},
                                                    .end = Point64{.x = 30, .y = 0}},
              },
              routing::LayerSegment{
                  .layer = kLayer,
                  .centerline = board_ir::Segment64{.start = Point64{.x = 30, .y = 0},
                                                    .end = Point64{.x = 30, .y = 20}},
              },
          },
      .telemetry = {},
      .producer_evidence = {},
  };
  test_support::CpuRouteFaultDecorator::Reseal(route, compiled);
  candidates::CandidateDraftBuildResult draft = candidates::BuildGeneratedCandidateFromCpuRoute(
      board, compiled, request, policy, route,
      candidates::CandidateSchedulingIdentity{.batch_identity = 0x4600,
                                              .query_identity = query_identity});
  EXPECT_TRUE(std::holds_alternative<candidates::GeneratedRouteCandidate>(draft));
  if (!std::holds_alternative<candidates::GeneratedRouteCandidate>(draft)) {
    const candidates::CandidateRejection& rejection =
        std::get<candidates::CandidateRejection>(draft);
    ADD_FAILURE() << rejection.invariant_id << ": " << rejection.detail;
    std::abort();
  }
  return test_support::AcceptedCandidate(
      candidates::CandidateAdmissionContext{
          .board = board,
          .compiled_board = compiled,
          .request = request,
      },
      std::get<candidates::GeneratedRouteCandidate>(std::move(draft)));
}

[[nodiscard]] Microcase MakeMicrocase(std::uint64_t revision = 1) {
  board_ir::BoardSnapshot board = test_support::Snapshot(PlannerBoard(revision));
  std::vector<CompiledBoard> compiled;
  compiled.reserve(kNets.size());
  for (EntityRef net : kNets) {
    compiled.push_back(test_support::CompilePreparedNet(board, net, PlannerProfile()));
  }

  std::vector<RouteCandidate> candidates;
  candidates.reserve(4);
  routing::CpuRouteRequest a_request = test_support::RequestForNet(board, kNets[0], kLayer, kLayer);
  candidates.push_back(Admit(board, compiled[0], a_request, 1));
  routing::CpuRouteRequest a_detour = a_request;
  const PhysicalEdgeSpan& first_span = candidates.front().data().resources.front();
  a_detour.candidate_policy.banned_resources.push_back(EdgeResourceKey{
      .layer = first_span.layer,
      .lattice_x = first_span.lattice_x,
      .lattice_y = first_span.lattice_y,
      .direction = first_span.direction,
  });
  candidates.push_back(Admit(board, compiled[0], a_detour, 2));
  const routing::CpuRouteRequest b_request =
      test_support::RequestForNet(board, kNets[1], kLayer, kLayer);
  candidates.push_back(AdmitSharedRoute(board, compiled[1], b_request, 3));
  candidates.push_back(
      Admit(board, compiled[2], test_support::RequestForNet(board, kNets[2], kLayer, kLayer), 4));
  return Microcase{
      .board = std::move(board),
      .compiled = std::move(compiled),
      .candidates = std::move(candidates),
  };
}

using PoolStorage = std::array<std::vector<const RouteCandidate*>, 4>;

[[nodiscard]] PoolStorage FullPoolStorage(const Microcase& microcase) {
  return PoolStorage{
      std::vector<const RouteCandidate*>{&microcase.candidates[0], &microcase.candidates[1]},
      std::vector<const RouteCandidate*>{&microcase.candidates[2]},
      std::vector<const RouteCandidate*>{&microcase.candidates[3]},
      std::vector<const RouteCandidate*>{},
  };
}

[[nodiscard]] std::array<OneWorldCandidatePool, 4> PoolViews(const PoolStorage& storage) {
  return {
      OneWorldCandidatePool{.net = kNets[0], .candidates = storage[0]},
      OneWorldCandidatePool{.net = kNets[1], .candidates = storage[1]},
      OneWorldCandidatePool{.net = kNets[2], .candidates = storage[2]},
      OneWorldCandidatePool{.net = kNets[3], .candidates = storage[3]},
  };
}

[[nodiscard]] ResourceCapacityModel Capacities(const Microcase& microcase,
                                               std::uint32_t capacity = 1) {
  ResourceCapacityModelResult result =
      BuildResourceCapacityModel(microcase.board, microcase.compiled.front(), capacity);
  EXPECT_TRUE(std::holds_alternative<ResourceCapacityModel>(result));
  if (!std::holds_alternative<ResourceCapacityModel>(result)) {
    std::abort();
  }
  return std::get<ResourceCapacityModel>(std::move(result));
}

[[nodiscard]] NegotiatedRegenerationPlan RequirePlan(NegotiatedRegenerationPlanResult result) {
  EXPECT_TRUE(std::holds_alternative<NegotiatedRegenerationPlan>(result));
  if (!std::holds_alternative<NegotiatedRegenerationPlan>(result)) {
    const NegotiatedRegenerationPlanError& error =
        std::get<NegotiatedRegenerationPlanError>(result);
    ADD_FAILURE() << error.invariant_id << ": " << error.detail;
    std::abort();
  }
  return std::get<NegotiatedRegenerationPlan>(std::move(result));
}

[[nodiscard]] NegotiatedRegenerationPlanError RequireError(NegotiatedRegenerationPlanResult result,
                                                           NegotiatedRegenerationPlanErrorCode code,
                                                           std::string_view invariant_id = {}) {
  EXPECT_TRUE(std::holds_alternative<NegotiatedRegenerationPlanError>(result));
  if (!std::holds_alternative<NegotiatedRegenerationPlanError>(result)) {
    std::abort();
  }
  NegotiatedRegenerationPlanError error =
      std::get<NegotiatedRegenerationPlanError>(std::move(result));
  EXPECT_EQ(error.code, code);
  if (!invariant_id.empty()) {
    EXPECT_EQ(error.invariant_id, invariant_id);
  }
  return error;
}

[[nodiscard]] bool OracleCandidateBefore(const RouteCandidate* left, const RouteCandidate* right) {
  const CandidateMetrics& left_metrics = left->data().metrics;
  const CandidateMetrics& right_metrics = right->data().metrics;
  const UWide left_steps =
      static_cast<UWide>(left_metrics.orthogonal_step_count) + left_metrics.diagonal_step_count;
  const UWide right_steps =
      static_cast<UWide>(right_metrics.orthogonal_step_count) + right_metrics.diagonal_step_count;
  return std::tuple{left_metrics.intrinsic_base_cost,
                    left_metrics.via_count,
                    left_metrics.bend_count,
                    left_steps,
                    left_metrics.axis_aligned_length_dbu,
                    left_metrics.diagonal_projection_dbu,
                    left->id()} < std::tuple{right_metrics.intrinsic_base_cost,
                                             right_metrics.via_count,
                                             right_metrics.bend_count,
                                             right_steps,
                                             right_metrics.axis_aligned_length_dbu,
                                             right_metrics.diagonal_projection_dbu,
                                             right->id()};
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
  ADD_FAILURE() << "Noncanonical direction reached independent P4R-06 oracle";
  return DirectionDelta{};
}

[[nodiscard]] std::vector<EdgeResourceKey> OracleExpand(const RouteCandidate& candidate) {
  std::vector<EdgeResourceKey> resources;
  for (const PhysicalEdgeSpan& span : candidate.data().resources) {
    const DirectionDelta delta = OracleStorageDelta(span.direction);
    for (std::uint32_t offset = 0; offset < span.edge_count; ++offset) {
      resources.push_back(EdgeResourceKey{
          .layer = span.layer,
          .lattice_x = span.lattice_x + static_cast<std::int64_t>(delta.x) * offset,
          .lattice_y = span.lattice_y + static_cast<std::int64_t>(delta.y) * offset,
          .direction = span.direction,
      });
    }
  }
  return resources;
}

struct OracleTarget {
  EntityRef net;
  NegotiatedRegenerationTargetReason reason;
  std::optional<candidates::CandidateId> selected_candidate_id;
  std::vector<EdgeResourceKey> triggers;
  std::uint64_t selected_resource_uses = 0;
  std::uint64_t total_trigger_price = 0;
};

struct OraclePlan {
  ResourceAccounting accounting;
  std::vector<NegotiatedResourcePrice> prices;
  std::vector<NegotiatedHotResource> hot;
  std::vector<OracleTarget> targets;
  std::uint64_t selected_resource_uses = 0;
  std::uint64_t target_links = 0;
  std::uint64_t target_price = 0;
};

// Independent exact-small price/hotset/target oracle. It selects directly,
// expands every atomic resource itself, and never calls production selection,
// accounting, or P4R-06 planning.
[[nodiscard]] OraclePlan Oracle(const ResourceCapacityModel& capacities,
                                std::span<const OneWorldCandidatePool> pools,
                                const NegotiatedPriceSnapshot* prior,
                                const NegotiatedPriceUpdatePolicyV1& policy = {}) {
  std::vector<const OneWorldCandidatePool*> ordered;
  for (const OneWorldCandidatePool& pool : pools) {
    ordered.push_back(&pool);
  }
  std::ranges::sort(ordered,
                    [](const OneWorldCandidatePool* left, const OneWorldCandidatePool* right) {
                      return std::pair{left->net.id, left->net.generation} <
                             std::pair{right->net.id, right->net.generation};
                    });

  std::vector<std::pair<EntityRef, const RouteCandidate*>> selected;
  std::map<EdgeResourceKey, std::uint64_t> usage;
  std::uint64_t expanded = 0;
  std::vector<OracleTarget> empty_targets;
  for (const OneWorldCandidatePool* pool : ordered) {
    if (pool->candidates.empty()) {
      empty_targets.push_back(OracleTarget{
          .net = pool->net,
          .reason = NegotiatedRegenerationTargetReason::kEmptyPool,
          .selected_candidate_id = std::nullopt,
          .triggers = {},
      });
      continue;
    }
    const RouteCandidate* best = pool->candidates.front();
    for (const RouteCandidate* candidate : pool->candidates.subspan(1)) {
      if (OracleCandidateBefore(candidate, best)) {
        best = candidate;
      }
    }
    selected.emplace_back(pool->net, best);
    for (const EdgeResourceKey& resource : OracleExpand(*best)) {
      ++usage[resource];
      ++expanded;
    }
  }

  ResourceAccounting accounting{
      .associations = capacities.associations(),
      .resources = {},
      .candidate_count = static_cast<std::uint64_t>(selected.size()),
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

  const std::uint64_t epoch = prior == nullptr ? 0 : prior->epoch_index + 1U;
  const std::uint64_t present_factor =
      policy.initial_present_factor + epoch * policy.present_factor_increment;
  std::map<EdgeResourceKey, std::uint64_t> history;
  if (prior != nullptr) {
    for (const NegotiatedResourcePrice& price : prior->prices) {
      history.emplace(price.resource, price.historical_price);
    }
  }
  std::map<EdgeResourceKey, NegotiatedResourcePrice> prices;
  for (const auto& [resource, historical] : history) {
    if (historical != 0) {
      prices.emplace(resource, NegotiatedResourcePrice{
                                   .resource = resource,
                                   .present_price = 0,
                                   .historical_price = historical,
                                   .total_price = historical,
                               });
    }
  }
  for (const ResourceUsage& resource_usage : accounting.resources) {
    if (resource_usage.overuse_units == 0) {
      continue;
    }
    NegotiatedResourcePrice& price = prices[resource_usage.resource];
    price.resource = resource_usage.resource;
    price.present_price = present_factor * resource_usage.overuse_units;
    price.historical_price += policy.historical_price_increment * resource_usage.overuse_units;
    price.total_price = price.present_price + price.historical_price;
  }

  OraclePlan oracle{
      .accounting = accounting,
      .prices = {},
      .hot = {},
      .targets = {},
      .selected_resource_uses = 0,
      .target_links = 0,
      .target_price = 0,
  };
  for (const auto& [resource, price] : prices) {
    static_cast<void>(resource);
    oracle.prices.push_back(price);
  }
  for (const ResourceUsage& resource_usage : accounting.resources) {
    if (resource_usage.overuse_units == 0) {
      continue;
    }
    const NegotiatedResourcePrice& price = prices.at(resource_usage.resource);
    oracle.hot.push_back(NegotiatedHotResource{
        .resource = resource_usage.resource,
        .capacity_units = resource_usage.capacity_units,
        .usage_units = resource_usage.usage_units,
        .overuse_units = resource_usage.overuse_units,
        .present_price = price.present_price,
        .historical_price = price.historical_price,
        .total_price = price.total_price,
    });
  }
  std::ranges::sort(oracle.hot,
                    [](const NegotiatedHotResource& left, const NegotiatedHotResource& right) {
                      return std::tuple{left.total_price, left.overuse_units} !=
                                     std::tuple{right.total_price, right.overuse_units}
                                 ? std::tuple{left.total_price, left.overuse_units} >
                                       std::tuple{right.total_price, right.overuse_units}
                                 : left.resource < right.resource;
                    });
  std::map<EdgeResourceKey, std::size_t> hot_rank;
  for (std::size_t index = 0; index < oracle.hot.size(); ++index) {
    hot_rank.emplace(oracle.hot[index].resource, index);
  }

  oracle.targets = std::move(empty_targets);
  for (const auto& [net, candidate] : selected) {
    std::vector<EdgeResourceKey> resources = OracleExpand(*candidate);
    oracle.selected_resource_uses += resources.size();
    OracleTarget target{
        .net = net,
        .reason = NegotiatedRegenerationTargetReason::kHotResource,
        .selected_candidate_id = candidate->id(),
        .triggers = {},
        .selected_resource_uses = static_cast<std::uint64_t>(resources.size()),
        .total_trigger_price = 0,
    };
    for (const EdgeResourceKey& resource : resources) {
      const auto rank = hot_rank.find(resource);
      if (rank != hot_rank.end()) {
        target.triggers.push_back(resource);
        target.total_trigger_price += oracle.hot[rank->second].total_price;
      }
    }
    if (!target.triggers.empty()) {
      std::ranges::sort(target.triggers,
                        [&hot_rank](const EdgeResourceKey& left, const EdgeResourceKey& right) {
                          return hot_rank.at(left) < hot_rank.at(right);
                        });
      oracle.target_links += target.triggers.size();
      oracle.target_price += target.total_trigger_price;
      oracle.targets.push_back(std::move(target));
    }
  }
  std::ranges::sort(oracle.targets, [](const OracleTarget& left, const OracleTarget& right) {
    if (left.reason != right.reason) {
      return left.reason == NegotiatedRegenerationTargetReason::kEmptyPool;
    }
    if (left.reason == NegotiatedRegenerationTargetReason::kHotResource) {
      if (left.total_trigger_price != right.total_trigger_price) {
        return left.total_trigger_price > right.total_trigger_price;
      }
      if (left.triggers.size() != right.triggers.size()) {
        return left.triggers.size() > right.triggers.size();
      }
    }
    return std::pair{left.net.id, left.net.generation} <
           std::pair{right.net.id, right.net.generation};
  });
  return oracle;
}

void ExpectOracleAgreement(const NegotiatedRegenerationPlan& plan, const OraclePlan& oracle) {
  EXPECT_EQ(plan.selection.accounting, oracle.accounting);
  EXPECT_EQ(plan.price_snapshot.prices, oracle.prices);
  EXPECT_EQ(plan.hot_resources, oracle.hot);
  EXPECT_EQ(plan.selected_resource_uses, oracle.selected_resource_uses);
  EXPECT_EQ(plan.target_resource_links, oracle.target_links);
  EXPECT_EQ(plan.aggregate_target_price, oracle.target_price);
  ASSERT_EQ(plan.targets.size(), oracle.targets.size());
  for (std::size_t index = 0; index < oracle.targets.size(); ++index) {
    EXPECT_EQ(plan.targets[index].net, oracle.targets[index].net);
    EXPECT_EQ(plan.targets[index].reason, oracle.targets[index].reason);
    EXPECT_EQ(plan.targets[index].selected_candidate_id,
              oracle.targets[index].selected_candidate_id);
    EXPECT_EQ(plan.targets[index].triggering_hot_resources, oracle.targets[index].triggers);
    EXPECT_EQ(plan.targets[index].selected_resource_uses,
              oracle.targets[index].selected_resource_uses);
    EXPECT_EQ(plan.targets[index].total_trigger_price, oracle.targets[index].total_trigger_price);
    EXPECT_NE(plan.targets[index].target_identity, 0U);
  }
}

TEST(NegotiatedRegenerationPlanTest, IndependentOracleCoversSharedDisjointAndEmptyPoolTargets) {
  const Microcase microcase = MakeMicrocase();
  const ResourceCapacityModel capacities = Capacities(microcase);
  const PoolStorage storage = FullPoolStorage(microcase);
  const std::array pools = PoolViews(storage);

  const NegotiatedRegenerationPlan plan =
      RequirePlan(PlanNegotiatedRegeneration(microcase.board, capacities, pools));
  const OraclePlan oracle = Oracle(capacities, pools, nullptr);
  ExpectOracleAgreement(plan, oracle);
  EXPECT_EQ(plan.disposition, NegotiatedRegenerationDisposition::kRegenerationRequired);
  ASSERT_EQ(plan.targets.size(), 3U);
  EXPECT_EQ(plan.targets[0].net, kNets[3]);
  EXPECT_EQ(plan.targets[0].reason, NegotiatedRegenerationTargetReason::kEmptyPool);
  EXPECT_EQ(plan.targets[1].net, kNets[0]);
  EXPECT_EQ(plan.targets[2].net, kNets[1]);
  EXPECT_EQ(plan.selection.accounting.overused_resource_count, 2U);
  EXPECT_EQ(plan.selection.accounting.total_overuse_units, 2U);
  EXPECT_EQ(plan.hot_resources.size(), 2U);
  EXPECT_EQ(plan.price_snapshot.prices.size(), 2U);
  EXPECT_NE(plan.price_snapshot.snapshot_identity, 0U);
  EXPECT_NE(plan.plan_identity, 0U);
}

TEST(NegotiatedRegenerationPlanTest, ZeroOveruseProducesNoPricesHotsetOrTargets) {
  const Microcase microcase = MakeMicrocase();
  const ResourceCapacityModel capacities = Capacities(microcase);
  const std::array<const RouteCandidate*, 1> a = {&microcase.candidates[0]};
  const std::array<const RouteCandidate*, 1> c = {&microcase.candidates[3]};
  const std::array pools = {
      OneWorldCandidatePool{.net = kNets[0], .candidates = a},
      OneWorldCandidatePool{.net = kNets[2], .candidates = c},
  };

  const NegotiatedRegenerationPlan plan =
      RequirePlan(PlanNegotiatedRegeneration(microcase.board, capacities, pools));
  ExpectOracleAgreement(plan, Oracle(capacities, pools, nullptr));
  EXPECT_EQ(plan.disposition, NegotiatedRegenerationDisposition::kNoRegenerationRequired);
  EXPECT_TRUE(plan.price_snapshot.prices.empty());
  EXPECT_TRUE(plan.hot_resources.empty());
  EXPECT_TRUE(plan.targets.empty());
}

TEST(NegotiatedRegenerationPlanTest, PoolCandidatePermutationsAndRepeatsAreNotSemantic) {
  const Microcase microcase = MakeMicrocase();
  const ResourceCapacityModel capacities = Capacities(microcase);
  PoolStorage storage = FullPoolStorage(microcase);
  const std::array baseline_pools = PoolViews(storage);
  const NegotiatedRegenerationPlan baseline =
      RequirePlan(PlanNegotiatedRegeneration(microcase.board, capacities, baseline_pools));
  for (int repeat = 0; repeat < 8; ++repeat) {
    EXPECT_EQ(RequirePlan(PlanNegotiatedRegeneration(microcase.board, capacities, baseline_pools)),
              baseline);
  }

  std::array<std::size_t, 4> order = {0, 1, 2, 3};
  do {
    const std::array canonical = PoolViews(storage);
    std::array<OneWorldCandidatePool, 4> permuted;
    for (std::size_t index = 0; index < order.size(); ++index) {
      permuted[index] = canonical[order[index]];
    }
    EXPECT_EQ(RequirePlan(PlanNegotiatedRegeneration(microcase.board, capacities, permuted)),
              baseline);
  } while (std::ranges::next_permutation(order).found);

  std::ranges::reverse(storage[0]);
  EXPECT_EQ(
      RequirePlan(PlanNegotiatedRegeneration(microcase.board, capacities, PoolViews(storage))),
      baseline);
}

TEST(NegotiatedRegenerationPlanTest, ReplayIdentitiesBindPolicyConfigPoolsAndTargets) {
  const Microcase microcase = MakeMicrocase();
  const ResourceCapacityModel capacities = Capacities(microcase);
  const PoolStorage storage = FullPoolStorage(microcase);
  const std::array pools = PoolViews(storage);
  const NegotiatedRegenerationPlan baseline =
      RequirePlan(PlanNegotiatedRegeneration(microcase.board, capacities, pools));

  NegotiatedRegenerationPlanConfig changed_limit;
  --changed_limit.limits.maximum_epoch_index;
  const NegotiatedRegenerationPlan limit_plan = RequirePlan(
      PlanNegotiatedRegeneration(microcase.board, capacities, pools, nullptr, changed_limit));
  EXPECT_EQ(limit_plan.price_snapshot, baseline.price_snapshot);
  EXPECT_EQ(limit_plan.targets, baseline.targets);
  EXPECT_NE(limit_plan.plan_identity, baseline.plan_identity);

  const auto selected_a =
      std::ranges::find_if(baseline.selection.nets, [](const OneWorldNetOutcome& outcome) {
        const auto* selected = std::get_if<OneWorldSelectedCandidate>(&outcome);
        return selected != nullptr && selected->net == kNets[0];
      });
  ASSERT_NE(selected_a, baseline.selection.nets.end());
  const candidates::CandidateId selected_a_id =
      std::get<OneWorldSelectedCandidate>(*selected_a).candidate_id;
  PoolStorage reduced_storage = storage;
  std::erase_if(reduced_storage[0], [selected_a_id](const RouteCandidate* candidate) {
    return candidate->id() != selected_a_id;
  });
  ASSERT_EQ(reduced_storage[0].size(), 1U);
  const NegotiatedRegenerationPlan reduced = RequirePlan(
      PlanNegotiatedRegeneration(microcase.board, capacities, PoolViews(reduced_storage)));
  EXPECT_EQ(reduced.selection.nets, baseline.selection.nets);
  EXPECT_EQ(reduced.selection.accounting, baseline.selection.accounting);
  EXPECT_NE(reduced.selection.input_candidate_count, baseline.selection.input_candidate_count);
  EXPECT_EQ(reduced.price_snapshot, baseline.price_snapshot);
  EXPECT_EQ(reduced.targets, baseline.targets);
  EXPECT_NE(reduced.plan_identity, baseline.plan_identity);

  NegotiatedRegenerationPlanConfig changed_policy;
  ++changed_policy.price_policy.historical_price_increment;
  const NegotiatedRegenerationPlan policy_plan = RequirePlan(
      PlanNegotiatedRegeneration(microcase.board, capacities, pools, nullptr, changed_policy));
  EXPECT_NE(policy_plan.price_snapshot.policy_identity, baseline.price_snapshot.policy_identity);
  EXPECT_NE(policy_plan.price_snapshot.snapshot_identity,
            baseline.price_snapshot.snapshot_identity);
  ASSERT_EQ(policy_plan.targets.size(), baseline.targets.size());
  for (std::size_t index = 0; index < policy_plan.targets.size(); ++index) {
    EXPECT_NE(policy_plan.targets[index].target_identity, baseline.targets[index].target_identity);
  }
  EXPECT_NE(policy_plan.plan_identity, baseline.plan_identity);
}

TEST(NegotiatedRegenerationPlanTest, ReplayGuardRejectsRepresentativeSingleFieldCorruptions) {
  const Microcase microcase = MakeMicrocase();
  const ResourceCapacityModel capacities = Capacities(microcase);
  const PoolStorage storage = FullPoolStorage(microcase);
  const std::array pools = PoolViews(storage);
  const NegotiatedRegenerationPlan baseline =
      RequirePlan(PlanNegotiatedRegeneration(microcase.board, capacities, pools));
  ASSERT_GE(baseline.price_snapshot.prices.size(), 2U);
  ASSERT_GE(baseline.hot_resources.size(), 2U);
  ASSERT_GE(baseline.targets.size(), 3U);
  EXPECT_FALSE(ValidateNegotiatedRegenerationPlanReplay(capacities, pools, baseline).has_value());

  const auto expect_error = [&](const NegotiatedRegenerationPlan& replay,
                                std::string_view invariant,
                                NegotiatedRegenerationPlanConfig config = {}) {
    const std::optional<NegotiatedRegenerationPlanError> error =
        ValidateNegotiatedRegenerationPlanReplay(capacities, pools, replay, nullptr, config);
    ASSERT_TRUE(error.has_value());
    EXPECT_EQ(error->invariant_id, invariant);
  };

  NegotiatedRegenerationPlan corrupted = baseline;
  ++corrupted.price_snapshot.epoch_index;
  expect_error(corrupted, "allocator.negotiated_plan.replay_chain.v1");

  corrupted = baseline;
  ++corrupted.price_snapshot.present_factor;
  expect_error(corrupted, "allocator.negotiated_plan.replay_present_factor.v1");

  corrupted = baseline;
  std::ranges::swap(corrupted.price_snapshot.prices[0], corrupted.price_snapshot.prices[1]);
  expect_error(corrupted, "allocator.negotiated_plan.replay_price_order.v1");

  corrupted = baseline;
  ++corrupted.price_snapshot.prices.front().total_price;
  expect_error(corrupted, "allocator.negotiated_plan.replay_price_total.v1");

  NegotiatedRegenerationPlanConfig price_bound;
  price_bound.limits.maximum_price_value = baseline.price_snapshot.prices.front().total_price - 1U;
  expect_error(baseline, "allocator.negotiated_plan.replay_price_bound.v1", price_bound);

  NegotiatedRegenerationPlanConfig aggregate_bound;
  aggregate_bound.limits.maximum_aggregate_price =
      baseline.price_snapshot.prices.front().total_price - 1U;
  expect_error(baseline, "allocator.negotiated_plan.replay_aggregate_price_bound.v1",
               aggregate_bound);

  corrupted = baseline;
  const auto selected =
      std::ranges::find_if(corrupted.selection.nets, [](const OneWorldNetOutcome& outcome) {
        const auto* value = std::get_if<OneWorldSelectedCandidate>(&outcome);
        return value != nullptr && value->net == kNets[0];
      });
  ASSERT_NE(selected, corrupted.selection.nets.end());
  std::get<OneWorldSelectedCandidate>(*selected).candidate_id = microcase.candidates[3].id();
  expect_error(corrupted, "allocator.negotiated_plan.replay_selected_candidate.v1");

  corrupted = baseline;
  std::ranges::swap(corrupted.hot_resources[0], corrupted.hot_resources[1]);
  expect_error(corrupted, "allocator.negotiated_plan.replay_hot_order.v1");

  corrupted = baseline;
  corrupted.disposition = NegotiatedRegenerationDisposition::kNoRegenerationRequired;
  expect_error(corrupted, "allocator.negotiated_plan.replay_target_shape.v1");

  corrupted = baseline;
  ++corrupted.targets.front().target_identity;
  expect_error(corrupted, "allocator.negotiated_plan.replay_target_identity.v1");

  corrupted = baseline;
  std::ranges::swap(corrupted.targets[1], corrupted.targets[2]);
  expect_error(corrupted, "allocator.negotiated_plan.replay_target_order.v1");
}

TEST(NegotiatedRegenerationPlanTest, PriorSnapshotAdvancesExactPresentAndHistoricalSchedule) {
  const Microcase microcase = MakeMicrocase();
  const ResourceCapacityModel capacities = Capacities(microcase);
  const PoolStorage storage = FullPoolStorage(microcase);
  const std::array pools = PoolViews(storage);
  const NegotiatedRegenerationPlan epoch_zero =
      RequirePlan(PlanNegotiatedRegeneration(microcase.board, capacities, pools));
  const NegotiatedRegenerationPlan epoch_one = RequirePlan(
      PlanNegotiatedRegeneration(microcase.board, capacities, pools, &epoch_zero.price_snapshot));

  EXPECT_EQ(epoch_zero.price_snapshot.epoch_index, 0U);
  EXPECT_EQ(epoch_zero.price_snapshot.present_factor, 100U);
  EXPECT_EQ(epoch_one.price_snapshot.epoch_index, 1U);
  EXPECT_EQ(epoch_one.price_snapshot.present_factor, 200U);
  EXPECT_EQ(epoch_one.price_snapshot.prior_snapshot_identity,
            epoch_zero.price_snapshot.snapshot_identity);
  ASSERT_EQ(epoch_one.price_snapshot.prices.size(), epoch_zero.price_snapshot.prices.size());
  for (const NegotiatedResourcePrice& price : epoch_zero.price_snapshot.prices) {
    EXPECT_EQ(price.present_price, 100U);
    EXPECT_EQ(price.historical_price, 10U);
    EXPECT_EQ(price.total_price, 110U);
  }
  for (const NegotiatedResourcePrice& price : epoch_one.price_snapshot.prices) {
    EXPECT_EQ(price.present_price, 200U);
    EXPECT_EQ(price.historical_price, 20U);
    EXPECT_EQ(price.total_price, 220U);
  }
  ExpectOracleAgreement(epoch_one, Oracle(capacities, pools, &epoch_zero.price_snapshot));
  EXPECT_NE(epoch_zero.plan_identity, epoch_one.plan_identity);
}

TEST(NegotiatedRegenerationPlanTest, HistoricalPricesPersistWhenCurrentOveruseBecomesZero) {
  const Microcase microcase = MakeMicrocase();
  const ResourceCapacityModel capacities = Capacities(microcase);
  const PoolStorage storage = FullPoolStorage(microcase);
  const std::array conflicted_pools = PoolViews(storage);
  const NegotiatedRegenerationPlan conflicted =
      RequirePlan(PlanNegotiatedRegeneration(microcase.board, capacities, conflicted_pools));

  const std::array<const RouteCandidate*, 1> a = {&microcase.candidates[0]};
  const std::array<const RouteCandidate*, 1> c = {&microcase.candidates[3]};
  const std::array disjoint_pools = {
      OneWorldCandidatePool{.net = kNets[0], .candidates = a},
      OneWorldCandidatePool{.net = kNets[2], .candidates = c},
  };
  const NegotiatedRegenerationPlan cold = RequirePlan(PlanNegotiatedRegeneration(
      microcase.board, capacities, disjoint_pools, &conflicted.price_snapshot));

  EXPECT_EQ(cold.disposition, NegotiatedRegenerationDisposition::kNoRegenerationRequired);
  EXPECT_TRUE(cold.hot_resources.empty());
  EXPECT_TRUE(cold.targets.empty());
  ASSERT_EQ(cold.price_snapshot.prices.size(), conflicted.price_snapshot.prices.size());
  for (const NegotiatedResourcePrice& price : cold.price_snapshot.prices) {
    EXPECT_EQ(price.present_price, 0U);
    EXPECT_EQ(price.historical_price, 10U);
    EXPECT_EQ(price.total_price, 10U);
  }
  ExpectOracleAgreement(cold, Oracle(capacities, disjoint_pools, &conflicted.price_snapshot));
}

TEST(NegotiatedRegenerationPlanTest, IndependentOracleDiscriminatesEveryPriorityTier) {
  const Microcase microcase = MakeMicrocase();
  const ResourceCapacityModel capacities = Capacities(microcase, 0);
  const PoolStorage storage = FullPoolStorage(microcase);
  const std::array full_pools = PoolViews(storage);
  const std::array a_only = {
      OneWorldCandidatePool{.net = kNets[0], .candidates = std::span(storage[0])},
  };
  const std::array c_only = {
      OneWorldCandidatePool{.net = kNets[2], .candidates = std::span(storage[2])},
  };
  NegotiatedRegenerationPlanConfig config;
  config.price_policy.historical_price_increment = 700;

  std::optional<NegotiatedPriceSnapshot> prior;
  for (int epoch = 0; epoch < 2; ++epoch) {
    prior = RequirePlan(PlanNegotiatedRegeneration(microcase.board, capacities, a_only,
                                                   prior.has_value() ? &*prior : nullptr, config))
                .price_snapshot;
  }
  for (int epoch = 0; epoch < 4; ++epoch) {
    prior = RequirePlan(
                PlanNegotiatedRegeneration(microcase.board, capacities, c_only, &*prior, config))
                .price_snapshot;
  }
  const NegotiatedRegenerationPlan plan = RequirePlan(
      PlanNegotiatedRegeneration(microcase.board, capacities, full_pools, &*prior, config));
  ExpectOracleAgreement(plan, Oracle(capacities, full_pools, &*prior, config.price_policy));

  ASSERT_GE(plan.hot_resources.size(), 3U);
  EXPECT_EQ(plan.hot_resources[0].total_price, plan.hot_resources[2].total_price);
  EXPECT_GT(plan.hot_resources[0].overuse_units, plan.hot_resources[2].overuse_units);
  ASSERT_EQ(plan.targets.size(), 4U);
  EXPECT_EQ(plan.targets[0].net, kNets[3]);
  EXPECT_EQ(plan.targets[1].net, kNets[2]);
  EXPECT_EQ(plan.targets[2].net, kNets[1]);
  EXPECT_EQ(plan.targets[3].net, kNets[0]);
  EXPECT_GT(plan.targets[1].total_trigger_price, plan.targets[2].total_trigger_price);
  EXPECT_EQ(plan.targets[2].total_trigger_price, plan.targets[3].total_trigger_price);
  EXPECT_GT(plan.targets[2].triggering_hot_resources.size(),
            plan.targets[3].triggering_hot_resources.size());
}

TEST(NegotiatedRegenerationPlanTest, ExactBoundsAcceptEqualityAndOneUnderFailsAtomically) {
  const Microcase microcase = MakeMicrocase();
  const ResourceCapacityModel capacities = Capacities(microcase);
  const PoolStorage storage = FullPoolStorage(microcase);
  const std::array pools = PoolViews(storage);
  const NegotiatedRegenerationPlan baseline =
      RequirePlan(PlanNegotiatedRegeneration(microcase.board, capacities, pools));

  NegotiatedRegenerationPlanConfig exact;
  exact.limits.maximum_price_entries = baseline.price_snapshot.prices.size();
  exact.limits.maximum_epoch_index = 0;
  exact.limits.maximum_hot_resources = baseline.hot_resources.size();
  exact.limits.maximum_targets = baseline.targets.size();
  exact.limits.maximum_selected_resource_uses_per_net = 6;
  exact.limits.maximum_aggregate_selected_resource_uses = baseline.selected_resource_uses;
  exact.limits.maximum_hot_resources_per_target = 2;
  exact.limits.maximum_aggregate_target_resource_links = baseline.target_resource_links;
  exact.limits.maximum_price_value = 110;
  exact.limits.maximum_aggregate_price = 220;
  exact.limits.maximum_target_price_per_net = 220;
  exact.limits.maximum_aggregate_target_price = baseline.aggregate_target_price;
  EXPECT_TRUE(std::holds_alternative<NegotiatedRegenerationPlan>(
      PlanNegotiatedRegeneration(microcase.board, capacities, pools, nullptr, exact)));

  NegotiatedRegenerationPlanConfig one_under = exact;
  one_under.limits.maximum_price_entries = baseline.price_snapshot.prices.size() - 1U;
  static_cast<void>(RequireError(
      PlanNegotiatedRegeneration(microcase.board, capacities, pools, nullptr, one_under),
      NegotiatedRegenerationPlanErrorCode::kBoundExhausted,
      "allocator.negotiated_plan.price_entry_bound.v1"));
  one_under = exact;
  one_under.limits.maximum_hot_resources = baseline.hot_resources.size() - 1U;
  static_cast<void>(RequireError(
      PlanNegotiatedRegeneration(microcase.board, capacities, pools, nullptr, one_under),
      NegotiatedRegenerationPlanErrorCode::kBoundExhausted,
      "allocator.negotiated_plan.hot_resource_bound.v1"));
  one_under = exact;
  one_under.limits.maximum_hot_resources_per_target = 1;
  static_cast<void>(RequireError(
      PlanNegotiatedRegeneration(microcase.board, capacities, pools, nullptr, one_under),
      NegotiatedRegenerationPlanErrorCode::kBoundExhausted,
      "allocator.negotiated_plan.net_hot_resource_bound.v1"));
  one_under = exact;
  one_under.limits.maximum_selected_resource_uses_per_net = 5;
  static_cast<void>(RequireError(
      PlanNegotiatedRegeneration(microcase.board, capacities, pools, nullptr, one_under),
      NegotiatedRegenerationPlanErrorCode::kBoundExhausted,
      "allocator.negotiated_plan.net_resource_bound.v1"));
  one_under = exact;
  one_under.limits.maximum_aggregate_selected_resource_uses = baseline.selected_resource_uses - 1U;
  static_cast<void>(RequireError(
      PlanNegotiatedRegeneration(microcase.board, capacities, pools, nullptr, one_under),
      NegotiatedRegenerationPlanErrorCode::kBoundExhausted,
      "allocator.negotiated_plan.aggregate_resource_bound.v1"));
  one_under = exact;
  one_under.limits.selection.accounting.maximum_expanded_resource_uses =
      baseline.selected_resource_uses - 1U;
  const NegotiatedRegenerationPlanError accounting_bound = RequireError(
      PlanNegotiatedRegeneration(microcase.board, capacities, pools, nullptr, one_under),
      NegotiatedRegenerationPlanErrorCode::kBoundExhausted,
      "allocator.accounting.expanded_resource_uses.v1");
  EXPECT_EQ(accounting_bound.selection_error_code, OneWorldSelectionErrorCode::kAccountingFailure);
  EXPECT_EQ(accounting_bound.accounting_error_code,
            ResourceAccountingErrorCode::kInputBoundExceeded);
  one_under = exact;
  one_under.limits.maximum_aggregate_target_resource_links = baseline.target_resource_links - 1U;
  static_cast<void>(RequireError(
      PlanNegotiatedRegeneration(microcase.board, capacities, pools, nullptr, one_under),
      NegotiatedRegenerationPlanErrorCode::kBoundExhausted,
      "allocator.negotiated_plan.aggregate_target_link_bound.v1"));
  one_under = exact;
  one_under.limits.maximum_targets = baseline.targets.size() - 1U;
  static_cast<void>(RequireError(
      PlanNegotiatedRegeneration(microcase.board, capacities, pools, nullptr, one_under),
      NegotiatedRegenerationPlanErrorCode::kBoundExhausted,
      "allocator.negotiated_plan.target_count_bound.v1"));
  one_under = exact;
  one_under.limits.maximum_price_value = 109;
  static_cast<void>(RequireError(
      PlanNegotiatedRegeneration(microcase.board, capacities, pools, nullptr, one_under),
      NegotiatedRegenerationPlanErrorCode::kBoundExhausted,
      "allocator.negotiated_plan.price_value_bound.v1"));
  one_under = exact;
  one_under.limits.maximum_aggregate_price = 219;
  static_cast<void>(RequireError(
      PlanNegotiatedRegeneration(microcase.board, capacities, pools, nullptr, one_under),
      NegotiatedRegenerationPlanErrorCode::kBoundExhausted,
      "allocator.negotiated_plan.aggregate_price_bound.v1"));
  one_under = exact;
  one_under.limits.maximum_target_price_per_net = 219;
  static_cast<void>(RequireError(
      PlanNegotiatedRegeneration(microcase.board, capacities, pools, nullptr, one_under),
      NegotiatedRegenerationPlanErrorCode::kBoundExhausted,
      "allocator.negotiated_plan.net_target_price_bound.v1"));
  one_under = exact;
  one_under.limits.maximum_aggregate_target_price = baseline.aggregate_target_price - 1U;
  static_cast<void>(RequireError(
      PlanNegotiatedRegeneration(microcase.board, capacities, pools, nullptr, one_under),
      NegotiatedRegenerationPlanErrorCode::kBoundExhausted,
      "allocator.negotiated_plan.aggregate_target_price_bound.v1"));

  static_cast<void>(RequireError(PlanNegotiatedRegeneration(microcase.board, capacities, pools,
                                                            &baseline.price_snapshot, exact),
                                 NegotiatedRegenerationPlanErrorCode::kBoundExhausted,
                                 "allocator.negotiated_plan.epoch_bound.v1"));
}

TEST(NegotiatedRegenerationPlanTest, AssociationsAndPriorSnapshotIntegrityFailClosed) {
  const Microcase microcase = MakeMicrocase();
  const Microcase foreign = MakeMicrocase(2);
  const ResourceCapacityModel capacities = Capacities(microcase);
  const ResourceCapacityModel foreign_capacities = Capacities(foreign);
  const PoolStorage storage = FullPoolStorage(microcase);
  const std::array pools = PoolViews(storage);

  static_cast<void>(
      RequireError(PlanNegotiatedRegeneration(microcase.board, foreign_capacities, pools),
                   NegotiatedRegenerationPlanErrorCode::kAssociationMismatch,
                   "allocator.negotiated_plan.capacity_board_association.v1"));
  const PoolStorage foreign_storage = FullPoolStorage(foreign);
  const NegotiatedRegenerationPlan foreign_plan = RequirePlan(
      PlanNegotiatedRegeneration(foreign.board, foreign_capacities, PoolViews(foreign_storage)));
  const std::array<const RouteCandidate*, 1> foreign_candidate = {&foreign.candidates.front()};
  const std::array foreign_candidate_pool = {
      OneWorldCandidatePool{.net = kNets[0], .candidates = foreign_candidate},
  };
  const NegotiatedRegenerationPlanError candidate_error =
      RequireError(PlanNegotiatedRegeneration(microcase.board, capacities, foreign_candidate_pool),
                   NegotiatedRegenerationPlanErrorCode::kAssociationMismatch,
                   "allocator.one_world.candidate_association.v1");
  EXPECT_EQ(candidate_error.selection_error_code,
            OneWorldSelectionErrorCode::kCandidateAssociationMismatch);
  static_cast<void>(RequireError(
      PlanNegotiatedRegeneration(microcase.board, capacities, pools, &foreign_plan.price_snapshot),
      NegotiatedRegenerationPlanErrorCode::kAssociationMismatch,
      "allocator.negotiated_plan.prior_association.v1"));

  NegotiatedRegenerationPlan valid =
      RequirePlan(PlanNegotiatedRegeneration(microcase.board, capacities, pools));
  NegotiatedPriceSnapshot corrupted = valid.price_snapshot;
  corrupted.policy_identity ^= 1U;
  static_cast<void>(
      RequireError(PlanNegotiatedRegeneration(microcase.board, capacities, pools, &corrupted),
                   NegotiatedRegenerationPlanErrorCode::kInvalidInput,
                   "allocator.negotiated_plan.prior_policy.v1"));
  corrupted = valid.price_snapshot;
  corrupted.prior_snapshot_identity = 1U;
  static_cast<void>(
      RequireError(PlanNegotiatedRegeneration(microcase.board, capacities, pools, &corrupted),
                   NegotiatedRegenerationPlanErrorCode::kInvalidInput,
                   "allocator.negotiated_plan.prior_chain.v1"));
  corrupted = valid.price_snapshot;
  ASSERT_GE(corrupted.prices.size(), 2U);
  corrupted.prices[1].resource = corrupted.prices[0].resource;
  static_cast<void>(
      RequireError(PlanNegotiatedRegeneration(microcase.board, capacities, pools, &corrupted),
                   NegotiatedRegenerationPlanErrorCode::kInvalidInput,
                   "allocator.negotiated_plan.prior_price_order.v1"));
  corrupted = valid.price_snapshot;
  corrupted.prices.front().historical_price += 1U;
  static_cast<void>(
      RequireError(PlanNegotiatedRegeneration(microcase.board, capacities, pools, &corrupted),
                   NegotiatedRegenerationPlanErrorCode::kInvalidInput,
                   "allocator.negotiated_plan.prior_price_total.v1"));
  corrupted = valid.price_snapshot;
  corrupted.prices.front().historical_price += 1U;
  corrupted.prices.front().total_price += 1U;
  static_cast<void>(
      RequireError(PlanNegotiatedRegeneration(microcase.board, capacities, pools, &corrupted),
                   NegotiatedRegenerationPlanErrorCode::kInvalidInput,
                   "allocator.negotiated_plan.prior_history_schedule.v1"));
  corrupted = valid.price_snapshot;
  corrupted.prices.front().present_price += 1U;
  corrupted.prices.front().total_price += 1U;
  static_cast<void>(
      RequireError(PlanNegotiatedRegeneration(microcase.board, capacities, pools, &corrupted),
                   NegotiatedRegenerationPlanErrorCode::kInvalidInput,
                   "allocator.negotiated_plan.prior_present_schedule.v1"));
  corrupted = valid.price_snapshot;
  corrupted.prices.front().historical_price = 0;
  corrupted.prices.front().total_price = corrupted.prices.front().present_price;
  static_cast<void>(
      RequireError(PlanNegotiatedRegeneration(microcase.board, capacities, pools, &corrupted),
                   NegotiatedRegenerationPlanErrorCode::kInvalidInput,
                   "allocator.negotiated_plan.prior_history_contribution.v1"));
  corrupted = valid.price_snapshot;
  constexpr std::uint64_t kUnreachableCurrentOveruse = 1'024;
  corrupted.prices.front().present_price = corrupted.present_factor * kUnreachableCurrentOveruse;
  corrupted.prices.front().historical_price = 10 * kUnreachableCurrentOveruse;
  corrupted.prices.front().total_price =
      corrupted.prices.front().present_price + corrupted.prices.front().historical_price;
  static_cast<void>(
      RequireError(PlanNegotiatedRegeneration(microcase.board, capacities, pools, &corrupted),
                   NegotiatedRegenerationPlanErrorCode::kBoundExhausted,
                   "allocator.negotiated_plan.prior_current_overuse_bound.v1"));

  const NegotiatedRegenerationPlan valid_epoch_one = RequirePlan(
      PlanNegotiatedRegeneration(microcase.board, capacities, pools, &valid.price_snapshot));
  corrupted = valid_epoch_one.price_snapshot;
  constexpr std::uint64_t kUnreachableCumulativeOveruse = 1'025;
  corrupted.prices.front().historical_price = 10 * kUnreachableCumulativeOveruse;
  corrupted.prices.front().total_price =
      corrupted.prices.front().present_price + corrupted.prices.front().historical_price;
  static_cast<void>(
      RequireError(PlanNegotiatedRegeneration(microcase.board, capacities, pools, &corrupted),
                   NegotiatedRegenerationPlanErrorCode::kBoundExhausted,
                   "allocator.negotiated_plan.prior_cumulative_overuse_bound.v1"));
  corrupted = valid.price_snapshot;
  corrupted.snapshot_identity ^= 1U;
  static_cast<void>(
      RequireError(PlanNegotiatedRegeneration(microcase.board, capacities, pools, &corrupted),
                   NegotiatedRegenerationPlanErrorCode::kInvalidInput,
                   "allocator.negotiated_plan.prior_identity.v1"));
}

TEST(NegotiatedRegenerationPlanTest, ZeroInitialPresentFactorRetainsReachableHistory) {
  const Microcase microcase = MakeMicrocase();
  const ResourceCapacityModel capacities = Capacities(microcase);
  const PoolStorage storage = FullPoolStorage(microcase);
  const std::array pools = PoolViews(storage);
  NegotiatedRegenerationPlanConfig config;
  config.price_policy.initial_present_factor = 0;

  const NegotiatedRegenerationPlan epoch_zero =
      RequirePlan(PlanNegotiatedRegeneration(microcase.board, capacities, pools, nullptr, config));
  ASSERT_FALSE(epoch_zero.price_snapshot.prices.empty());
  for (const NegotiatedResourcePrice& price : epoch_zero.price_snapshot.prices) {
    EXPECT_EQ(price.present_price, 0U);
    EXPECT_GT(price.historical_price, 0U);
  }
  NegotiatedPriceSnapshot unreachable = epoch_zero.price_snapshot;
  unreachable.prices.front().historical_price = 10'240;
  unreachable.prices.front().total_price = 10'240;
  static_cast<void>(RequireError(
      PlanNegotiatedRegeneration(microcase.board, capacities, pools, &unreachable, config),
      NegotiatedRegenerationPlanErrorCode::kBoundExhausted,
      "allocator.negotiated_plan.prior_current_overuse_bound.v1"));
  const NegotiatedRegenerationPlan epoch_one = RequirePlan(PlanNegotiatedRegeneration(
      microcase.board, capacities, pools, &epoch_zero.price_snapshot, config));
  EXPECT_EQ(epoch_one.price_snapshot.present_factor, 100U);
}

TEST(NegotiatedRegenerationPlanTest, CheckedPriceArithmeticRejectsOverflow) {
  const Microcase microcase = MakeMicrocase();
  const ResourceCapacityModel zero_capacity = Capacities(microcase, 0);
  const PoolStorage storage = FullPoolStorage(microcase);
  const std::array pools = PoolViews(storage);
  NegotiatedRegenerationPlanConfig config;
  config.price_policy.initial_present_factor = std::numeric_limits<std::uint64_t>::max();
  config.limits.maximum_price_value = std::numeric_limits<std::uint64_t>::max();
  config.limits.maximum_aggregate_price = std::numeric_limits<std::uint64_t>::max();
  config.limits.maximum_target_price_per_net = std::numeric_limits<std::uint64_t>::max();
  config.limits.maximum_aggregate_target_price = std::numeric_limits<std::uint64_t>::max();
  static_cast<void>(RequireError(
      PlanNegotiatedRegeneration(microcase.board, zero_capacity, pools, nullptr, config),
      NegotiatedRegenerationPlanErrorCode::kArithmeticOverflow,
      "allocator.negotiated_plan.total_price_overflow.v1"));
}

TEST(NegotiatedRegenerationPlanTest, FatalDiagnosticsPrecedeDispositionAndInputsRemainImmutable) {
  const Microcase microcase = MakeMicrocase();
  const Microcase foreign = MakeMicrocase(2);
  const ResourceCapacityModel capacities = Capacities(microcase);
  const ResourceCapacityModel foreign_capacities = Capacities(foreign);
  PoolStorage storage = FullPoolStorage(microcase);
  const PoolStorage original_storage = storage;
  const std::vector<RouteCandidate> original_candidates = microcase.candidates;
  const std::array pools = PoolViews(storage);
  const NegotiatedRegenerationPlan first =
      RequirePlan(PlanNegotiatedRegeneration(microcase.board, capacities, pools));
  const NegotiatedPriceSnapshot original_snapshot = first.price_snapshot;

  NegotiatedRegenerationPlanConfig bounded;
  bounded.limits.maximum_targets = 2;
  const NegotiatedRegenerationPlanError error =
      RequireError(PlanNegotiatedRegeneration(microcase.board, capacities, pools,
                                              &first.price_snapshot, bounded),
                   NegotiatedRegenerationPlanErrorCode::kBoundExhausted,
                   "allocator.negotiated_plan.target_count_bound.v1");
  EXPECT_EQ(error.net, kNets[3]);
  EXPECT_FALSE(std::holds_alternative<NegotiatedRegenerationPlan>(PlanNegotiatedRegeneration(
      microcase.board, capacities, pools, &first.price_snapshot, bounded)));
  EXPECT_EQ(storage, original_storage);
  EXPECT_EQ(microcase.candidates, original_candidates);
  EXPECT_EQ(first.price_snapshot, original_snapshot);

  NegotiatedRegenerationPlanConfig invalid_config;
  invalid_config.price_policy.present_factor_increment = 0;
  static_cast<void>(RequireError(PlanNegotiatedRegeneration(microcase.board, foreign_capacities,
                                                            pools, nullptr, invalid_config),
                                 NegotiatedRegenerationPlanErrorCode::kInvalidConfiguration,
                                 "allocator.negotiated_plan.configuration.v1"));

  NegotiatedPriceSnapshot corrupted_prior = first.price_snapshot;
  ++corrupted_prior.prices.front().historical_price;
  const std::array duplicate_pools = {pools[0], pools[0]};
  static_cast<void>(RequireError(
      PlanNegotiatedRegeneration(microcase.board, capacities, duplicate_pools, &corrupted_prior),
      NegotiatedRegenerationPlanErrorCode::kInvalidInput,
      "allocator.negotiated_plan.prior_price_total.v1"));

  const ResourceCapacityModel zero_capacity = Capacities(microcase, 0);
  NegotiatedRegenerationPlanConfig price_before_target;
  price_before_target.price_policy.initial_present_factor =
      std::numeric_limits<std::uint64_t>::max();
  price_before_target.limits.maximum_targets = 1;
  price_before_target.limits.maximum_price_value = std::numeric_limits<std::uint64_t>::max();
  price_before_target.limits.maximum_aggregate_price = std::numeric_limits<std::uint64_t>::max();
  static_cast<void>(RequireError(PlanNegotiatedRegeneration(microcase.board, zero_capacity, pools,
                                                            nullptr, price_before_target),
                                 NegotiatedRegenerationPlanErrorCode::kArithmeticOverflow,
                                 "allocator.negotiated_plan.total_price_overflow.v1"));

  NegotiatedRegenerationPlanConfig bound_before_later_overflow;
  bound_before_later_overflow.price_policy.initial_present_factor =
      std::numeric_limits<std::uint64_t>::max() / 4U;
  bound_before_later_overflow.price_policy.historical_price_increment = 1;
  bound_before_later_overflow.limits.maximum_price_value =
      std::numeric_limits<std::uint64_t>::max();
  bound_before_later_overflow.limits.maximum_aggregate_price = 1;
  static_cast<void>(RequireError(PlanNegotiatedRegeneration(microcase.board, zero_capacity, pools,
                                                            nullptr, bound_before_later_overflow),
                                 NegotiatedRegenerationPlanErrorCode::kBoundExhausted,
                                 "allocator.negotiated_plan.aggregate_price_bound.v1"));

  NegotiatedRegenerationPlanConfig overflowing;
  overflowing.price_policy.initial_present_factor = std::numeric_limits<std::uint64_t>::max();
  const NegotiatedRegenerationPlanError selection_first =
      RequireError(PlanNegotiatedRegeneration(microcase.board, capacities, duplicate_pools, nullptr,
                                              overflowing),
                   NegotiatedRegenerationPlanErrorCode::kSelectionFailure,
                   "allocator.one_world.duplicate_net_pool.v1");
  EXPECT_EQ(selection_first.selection_error_code, OneWorldSelectionErrorCode::kDuplicateNetPool);
}

}  // namespace
}  // namespace apgar::allocator
