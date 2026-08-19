#include "apgar/allocator/cpu_candidate_allocation_session.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "apgar/allocator/negotiated_regeneration_plan.h"
#include "apgar/allocator/resource_accounting.h"
#include "apgar/board_ir/board.h"
#include "apgar/board_ir/stable_hash.h"
#include "apgar/candidates/route_candidate.h"
#include "apgar/geometry_compiler/compiled_board.h"
#include "apgar/routing/candidate_policy.h"
#include "apgar/routing/planar_route.h"
#include "src/allocator/cpu_candidate_allocation_session_internal.h"
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
using candidates::CandidateId;
using candidates::RouteCandidate;
using geometry_compiler::ActiveRegion;
using geometry_compiler::CompiledBoard;
using geometry_compiler::CompilerProfile;
using geometry_compiler::DeterministicCosts;
using routing::EdgeResourceKey;

inline constexpr board_ir::LayerId kLayer = 0;
inline constexpr std::array<EntityRef, 4> kNets = {
    EntityRef{.id = 10, .generation = 0},
    EntityRef{.id = 11, .generation = 0},
    EntityRef{.id = 12, .generation = 0},
    EntityRef{.id = 13, .generation = 0},
};

[[nodiscard]] auto NetKey(EntityRef net) noexcept { return std::pair{net.id, net.generation}; }

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
      .component = "P4R08",
      .pin = std::move(pin),
      .center = center,
      .connection_region = TerminalBox(center),
      .layers = {kLayer},
  };
}

[[nodiscard]] BoardData SessionBoardData(std::uint64_t revision = 1) {
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
      .adapter_name = "p4r08-exact-small",
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
              Net{.ref = kNets[0], .name = "HOT-A", .terminals = {terminals[0], terminals[1]}},
              Net{.ref = kNets[1], .name = "HOT-B", .terminals = {terminals[2], terminals[3]}},
              Net{.ref = kNets[2],
                  .name = "EMPTY-DISCONNECTED",
                  .terminals = {terminals[4], terminals[5]}},
              Net{.ref = kNets[3],
                  .name = "EMPTY-REACHABLE",
                  .terminals = {terminals[6], terminals[7]}},
          },
      .terminals =
          {
              MakeTerminal(20, kNets[0], Point64{.x = 0, .y = 0}, "A1"),
              MakeTerminal(21, kNets[0], Point64{.x = 40, .y = 0}, "A2"),
              MakeTerminal(22, kNets[1], Point64{.x = 10, .y = -20}, "B1"),
              MakeTerminal(23, kNets[1], Point64{.x = 30, .y = 20}, "B2"),
              MakeTerminal(24, kNets[2], Point64{.x = 0, .y = 40}, "D1"),
              MakeTerminal(25, kNets[2], Point64{.x = 40, .y = 40}, "D2"),
              MakeTerminal(26, kNets[3], Point64{.x = 0, .y = 30}, "C1"),
              MakeTerminal(27, kNets[3], Point64{.x = 40, .y = 30}, "C2"),
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

[[nodiscard]] CompilerProfile SessionProfile() {
  return CompilerProfile{
      .schema_version = geometry_compiler::kCompilerProfileSchemaVersion,
      .lattice_origin = Point64{.x = 0, .y = 0},
      .lattice_step = 10,
      .tile_width_nodes = 1,
      .tile_height_nodes = 1,
      .compilation_roi =
          AxisAlignedBox64{.min = Point64{.x = 0, .y = -20}, .max = Point64{.x = 40, .y = 40}},
      .active_regions =
          {
              ActiveRegion{
                  .layer = kLayer,
                  .bounds = AxisAlignedBox64{.min = Point64{.x = 0, .y = 0},
                                             .max = Point64{.x = 40, .y = 0}},
              },
              ActiveRegion{
                  .layer = kLayer,
                  .bounds = AxisAlignedBox64{.min = Point64{.x = 10, .y = -20},
                                             .max = Point64{.x = 10, .y = 0}},
              },
              ActiveRegion{
                  .layer = kLayer,
                  .bounds = AxisAlignedBox64{.min = Point64{.x = 30, .y = 0},
                                             .max = Point64{.x = 30, .y = 20}},
              },
              ActiveRegion{
                  .layer = kLayer,
                  .bounds = AxisAlignedBox64{.min = Point64{.x = 0, .y = 30},
                                             .max = Point64{.x = 40, .y = 30}},
              },
              ActiveRegion{
                  .layer = kLayer,
                  .bounds = AxisAlignedBox64{.min = Point64{.x = 0, .y = 40},
                                             .max = Point64{.x = 0, .y = 40}},
              },
              ActiveRegion{
                  .layer = kLayer,
                  .bounds = AxisAlignedBox64{.min = Point64{.x = 40, .y = 40},
                                             .max = Point64{.x = 40, .y = 40}},
              },
          },
      .heading_mask = board_ir::kM1HeadingMask,
      .costs = DeterministicCosts{.orthogonal_step = 10, .diagonal_step = 14, .bend = 3},
  };
}

[[nodiscard]] routing::CpuRouteRequest ForcedSharedRequest(const board_ir::BoardSnapshot& board,
                                                           const CompiledBoard& compiled) {
  routing::CpuRouteRequest request = test_support::RequestForNet(board, kNets[1]);
  const std::set<EdgeResourceKey> desired = {
      EdgeResourceKey{.layer = kLayer,
                      .lattice_x = 1,
                      .lattice_y = -2,
                      .direction = geometry_compiler::Direction::kNorth},
      EdgeResourceKey{.layer = kLayer,
                      .lattice_x = 1,
                      .lattice_y = -1,
                      .direction = geometry_compiler::Direction::kNorth},
      EdgeResourceKey{.layer = kLayer,
                      .lattice_x = 1,
                      .lattice_y = 0,
                      .direction = geometry_compiler::Direction::kEast},
      EdgeResourceKey{.layer = kLayer,
                      .lattice_x = 2,
                      .lattice_y = 0,
                      .direction = geometry_compiler::Direction::kEast},
      EdgeResourceKey{.layer = kLayer,
                      .lattice_x = 3,
                      .lattice_y = 0,
                      .direction = geometry_compiler::Direction::kNorth},
      EdgeResourceKey{.layer = kLayer,
                      .lattice_x = 3,
                      .lattice_y = 1,
                      .direction = geometry_compiler::Direction::kNorth},
  };
  std::set<EdgeResourceKey> banned;
  for (std::int64_t y = -2; y <= 4; ++y) {
    for (std::int64_t x = 0; x <= 4; ++x) {
      for (geometry_compiler::Direction direction : geometry_compiler::kStableDirectionOrder) {
        const std::optional<EdgeResourceKey> resource = routing::CanonicalPhysicalEdgeResource(
            kLayer, geometry_compiler::LatticeIndex{.x = x, .y = y}, direction);
        if (resource.has_value() && routing::ResourceExists(compiled, *resource) &&
            !desired.contains(*resource)) {
          banned.insert(*resource);
        }
      }
    }
  }
  request.candidate_policy.banned_resources.assign(banned.begin(), banned.end());
  return request;
}

[[nodiscard]] ResourceCapacityModel Capacity(const board_ir::BoardSnapshot& board,
                                             const CompiledBoard& compiled) {
  ResourceCapacityModelResult result = BuildResourceCapacityModel(board, compiled, 1);
  EXPECT_TRUE(std::holds_alternative<ResourceCapacityModel>(result));
  if (!std::holds_alternative<ResourceCapacityModel>(result)) {
    std::abort();
  }
  return std::get<ResourceCapacityModel>(std::move(result));
}

class Scenario {
 public:
  explicit Scenario(std::uint64_t source_price = 1)
      : board_(test_support::Snapshot(SessionBoardData())), capacities_(std::nullopt) {
    compiled_.reserve(kNets.size());
    for (EntityRef net : kNets) {
      compiled_.push_back(test_support::CompilePreparedNet(board_, net, SessionProfile()));
    }
    source_candidates_.reserve(2);
    for (std::size_t index = 0; index < 2; ++index) {
      routing::CpuRouteRequest request = index == 0
                                             ? test_support::RequestForNet(board_, kNets[index])
                                             : ForcedSharedRequest(board_, compiled_[index]);
      if (index == 0) {
        request.candidate_policy.resource_penalties = {
            routing::ResourcePenalty{
                .resource = EdgeResourceKey{.layer = kLayer,
                                            .lattice_x = 1,
                                            .lattice_y = 0,
                                            .direction = geometry_compiler::Direction::kEast},
                .additional_cost = source_price},
            routing::ResourcePenalty{
                .resource = EdgeResourceKey{.layer = kLayer,
                                            .lattice_x = 2,
                                            .lattice_y = 0,
                                            .direction = geometry_compiler::Direction::kEast},
                .additional_cost = source_price},
        };
      }
      RouteCandidate candidate = test_support::AcceptedCandidate(
          candidates::CandidateAdmissionContext{
              .board = board_, .compiled_board = compiled_[index], .request = request},
          test_support::CandidateDraft(board_, compiled_[index], request, 0x800, 0x810 + index));
      source_candidates_.push_back(std::make_shared<const RouteCandidate>(std::move(candidate)));
    }
    hot_a_[0] = source_candidates_[0].get();
    hot_b_[0] = source_candidates_[1].get();
    contexts_.reserve(kNets.size());
    for (std::size_t index = 0; index < kNets.size(); ++index) {
      contexts_.push_back(CpuTargetedRegenerationNetContext{
          .compiled_board = &compiled_[index],
          .request = test_support::RequestForNet(board_, kNets[index]),
      });
    }
    capacities_.emplace(Capacity(board_, compiled_[0]));
  }

  [[nodiscard]] const board_ir::BoardSnapshot& board() const noexcept { return board_; }
  [[nodiscard]] const ResourceCapacityModel& capacities() const noexcept { return *capacities_; }
  [[nodiscard]] const std::vector<candidates::StoredCandidate>& source_candidates() const noexcept {
    return source_candidates_;
  }
  [[nodiscard]] std::vector<OneWorldCandidatePool> all_pools() const {
    return {
        OneWorldCandidatePool{.net = kNets[0], .candidates = hot_a_},
        OneWorldCandidatePool{.net = kNets[1], .candidates = hot_b_},
        OneWorldCandidatePool{.net = kNets[2], .candidates = {}},
        OneWorldCandidatePool{.net = kNets[3], .candidates = {}},
    };
  }
  [[nodiscard]] std::vector<CpuTargetedRegenerationSourcePool> all_source_pools() const {
    return {
        CpuTargetedRegenerationSourcePool{
            .net = kNets[0], .candidates = std::span(source_candidates_).subspan(0, 1)},
        CpuTargetedRegenerationSourcePool{
            .net = kNets[1], .candidates = std::span(source_candidates_).subspan(1, 1)},
        CpuTargetedRegenerationSourcePool{.net = kNets[2], .candidates = {}},
        CpuTargetedRegenerationSourcePool{.net = kNets[3], .candidates = {}},
    };
  }
  [[nodiscard]] std::vector<CpuTargetedRegenerationSourcePool> no_op_source_pools() const {
    return {CpuTargetedRegenerationSourcePool{
        .net = kNets[0], .candidates = std::span(source_candidates_).subspan(0, 1)}};
  }
  [[nodiscard]] std::span<const CpuTargetedRegenerationNetContext> contexts() const noexcept {
    return contexts_;
  }
  [[nodiscard]] std::span<const CpuTargetedRegenerationNetContext> no_op_contexts() const noexcept {
    return std::span(contexts_).first(1);
  }

 private:
  board_ir::BoardSnapshot board_;
  std::vector<CompiledBoard> compiled_;
  std::vector<candidates::StoredCandidate> source_candidates_;
  std::array<const RouteCandidate*, 1> hot_a_{};
  std::array<const RouteCandidate*, 1> hot_b_{};
  std::vector<CpuTargetedRegenerationNetContext> contexts_;
  std::optional<ResourceCapacityModel> capacities_;
};

[[nodiscard]] const CpuCandidateAllocationSession& Success(
    const CpuCandidateAllocationSessionResult& result) {
  EXPECT_TRUE(std::holds_alternative<CpuCandidateAllocationSession>(result))
      << (std::holds_alternative<CpuCandidateAllocationSessionError>(result)
              ? std::get<CpuCandidateAllocationSessionError>(result).detail
              : std::string_view{});
  if (!std::holds_alternative<CpuCandidateAllocationSession>(result)) {
    std::abort();
  }
  return std::get<CpuCandidateAllocationSession>(result);
}

[[nodiscard]] const CpuCandidateAllocationSessionError& Failure(
    const CpuCandidateAllocationSessionResult& result) {
  EXPECT_TRUE(std::holds_alternative<CpuCandidateAllocationSessionError>(result));
  if (!std::holds_alternative<CpuCandidateAllocationSessionError>(result)) {
    std::abort();
  }
  return std::get<CpuCandidateAllocationSessionError>(result);
}

[[nodiscard]] NegotiatedRegenerationPlan RequirePlan(NegotiatedRegenerationPlanResult result) {
  EXPECT_TRUE(std::holds_alternative<NegotiatedRegenerationPlan>(result))
      << (std::holds_alternative<NegotiatedRegenerationPlanError>(result)
              ? std::get<NegotiatedRegenerationPlanError>(result).detail
              : std::string_view{});
  if (!std::holds_alternative<NegotiatedRegenerationPlan>(result)) {
    std::abort();
  }
  return std::get<NegotiatedRegenerationPlan>(std::move(result));
}

[[nodiscard]] CpuTargetedRegenerationEpoch RequireEpoch(CpuTargetedRegenerationEpochResult result) {
  EXPECT_TRUE(std::holds_alternative<CpuTargetedRegenerationEpoch>(result))
      << (std::holds_alternative<CpuTargetedRegenerationEpochError>(result)
              ? std::get<CpuTargetedRegenerationEpochError>(result).detail
              : std::string_view{});
  if (!std::holds_alternative<CpuTargetedRegenerationEpoch>(result)) {
    std::abort();
  }
  return std::get<CpuTargetedRegenerationEpoch>(std::move(result));
}

struct OwnedPool {
  EntityRef net;
  std::vector<candidates::StoredCandidate> candidates;
};

[[nodiscard]] std::vector<OwnedPool> CopyPools(
    std::span<const CpuTargetedRegenerationSourcePool> source_pools) {
  std::vector<OwnedPool> copied;
  copied.reserve(source_pools.size());
  for (const CpuTargetedRegenerationSourcePool& pool : source_pools) {
    OwnedPool& destination =
        copied.emplace_back(OwnedPool{.net = pool.net,
                                      .candidates = std::vector<candidates::StoredCandidate>(
                                          pool.candidates.begin(), pool.candidates.end())});
    std::ranges::sort(destination.candidates,
                      [](const auto& left, const auto& right) { return left->id() < right->id(); });
  }
  std::ranges::sort(copied, [](const OwnedPool& left, const OwnedPool& right) {
    return NetKey(left.net) < NetKey(right.net);
  });
  return copied;
}

[[nodiscard]] std::vector<CpuTargetedRegenerationSourcePool> SourcePoolViews(
    std::span<const OwnedPool> pools) {
  std::vector<CpuTargetedRegenerationSourcePool> views;
  views.reserve(pools.size());
  for (const OwnedPool& pool : pools) {
    views.push_back(CpuTargetedRegenerationSourcePool{
        .net = pool.net,
        .candidates = pool.candidates,
    });
  }
  return views;
}

struct OracleStep {
  std::uint64_t input_pool_identity = 0;
  NegotiatedRegenerationPlan plan;
  std::optional<std::uint64_t> batch_identity;
  std::optional<std::uint64_t> epoch_identity;
  std::optional<std::uint64_t> output_pool_identity;
  CpuTargetedRegenerationEpochCounters counters;
};

struct ManualComposition {
  std::vector<OwnedPool> pools;
  OneWorldSelection final_selection;
  std::optional<NegotiatedRegenerationPlan> terminal_plan;
  std::vector<OracleStep> steps;
  CpuCandidateAllocationSessionCounters counters;
  CpuCandidateAllocationStop stop;
  std::uint64_t initial_pool_identity = 0;
  std::uint64_t final_pool_identity = 0;
};

[[nodiscard]] std::uint64_t IndependentPoolIdentity(std::span<const OwnedPool> pools) {
  board_ir::StableHashBuilder hash;
  hash.AddString("APGAR-P4R08-CPU-CANDIDATE-ALLOCATION-POOLS-V1");
  hash.AddU64(static_cast<std::uint64_t>(pools.size()));
  for (const OwnedPool& pool : pools) {
    hash.AddU64(pool.net.id);
    hash.AddU32(pool.net.generation);
    std::vector<candidates::StoredCandidate> ordered = pool.candidates;
    std::ranges::sort(ordered,
                      [](const auto& left, const auto& right) { return left->id() < right->id(); });
    hash.AddU64(static_cast<std::uint64_t>(ordered.size()));
    for (const candidates::StoredCandidate& candidate : ordered) {
      hash.AddU64(candidate->id().high);
      hash.AddU64(candidate->id().low);
      hash.AddU64(candidate->data().payload_checksum);
      hash.AddU64(candidate->logical_bytes());
    }
  }
  const std::uint64_t value = hash.Finish();
  return value == 0 ? 1 : value;
}

// This reference never calls RunCpuCandidateAllocationSession. It composes the
// independently covered P4R-06 and P4R-07 public boundaries and owns every
// intermediate retained pool before constructing the next set of borrowed views.
[[nodiscard]] ManualComposition ComposeManually(
    const Scenario& scenario, std::span<const CpuTargetedRegenerationSourcePool> source_pools,
    std::span<const CpuTargetedRegenerationNetContext> contexts,
    const CpuCandidateAllocationSessionConfig& config, std::uint64_t execution_limit,
    bool plan_terminal_step = true) {
  ManualComposition oracle{
      .pools = CopyPools(source_pools),
      .final_selection = {},
      .terminal_plan = std::nullopt,
      .steps = {},
      .counters = {},
      .stop = {},
      .initial_pool_identity = 0,
      .final_pool_identity = 0,
  };
  oracle.initial_pool_identity = IndependentPoolIdentity(oracle.pools);
  std::optional<NegotiatedPriceSnapshot> prior;
  std::uint64_t executed = 0;
  while (true) {
    std::vector<std::vector<const RouteCandidate*>> candidate_views(oracle.pools.size());
    std::vector<OneWorldCandidatePool> planning_pools;
    std::vector<CpuTargetedRegenerationSourcePool> execution_pools;
    planning_pools.reserve(oracle.pools.size());
    execution_pools.reserve(oracle.pools.size());
    for (std::size_t index = 0; index < oracle.pools.size(); ++index) {
      OwnedPool& pool = oracle.pools[index];
      candidate_views[index].reserve(pool.candidates.size());
      for (const candidates::StoredCandidate& candidate : pool.candidates) {
        candidate_views[index].push_back(candidate.get());
      }
      planning_pools.push_back(
          OneWorldCandidatePool{.net = pool.net, .candidates = candidate_views[index]});
      execution_pools.push_back(
          CpuTargetedRegenerationSourcePool{.net = pool.net, .candidates = pool.candidates});
    }

    NegotiatedRegenerationPlan plan = RequirePlan(
        PlanNegotiatedRegeneration(scenario.board(), scenario.capacities(), planning_pools,
                                   prior.has_value() ? &*prior : nullptr, config.epoch.planning));
    ++oracle.counters.planning_steps;
    OracleStep step{
        .input_pool_identity = IndependentPoolIdentity(oracle.pools),
        .plan = plan,
        .batch_identity = std::nullopt,
        .epoch_identity = std::nullopt,
        .output_pool_identity = std::nullopt,
        .counters = {},
    };
    if (plan.disposition == NegotiatedRegenerationDisposition::kNoRegenerationRequired) {
      oracle.final_selection = plan.selection;
      oracle.terminal_plan = plan;
      oracle.steps.push_back(std::move(step));
      oracle.stop = CpuCandidateAllocationStop{
          .reason = CpuCandidateAllocationStopReason::kFixedPoint,
          .invariant_id = "allocator.cpu_allocation_session.fixed_point.v1",
          .detail = "The authenticated P4R-06 plan has no regeneration targets",
          .expected_value = std::nullopt,
          .actual_value = std::nullopt,
          .plan_error_code = std::nullopt,
          .epoch_error_code = std::nullopt,
      };
      oracle.final_pool_identity = IndependentPoolIdentity(oracle.pools);
      return oracle;
    }
    if (executed == execution_limit) {
      oracle.final_selection = plan.selection;
      oracle.terminal_plan = plan;
      oracle.steps.push_back(std::move(step));
      oracle.stop = CpuCandidateAllocationStop{
          .reason = CpuCandidateAllocationStopReason::kSessionBoundExhausted,
          .invariant_id = "allocator.cpu_allocation_session.epoch_count_bound.v1",
          .detail = "Another regeneration epoch exceeds the session epoch bound",
          .expected_value = execution_limit,
          .actual_value = execution_limit + 1U,
          .plan_error_code = std::nullopt,
          .epoch_error_code = std::nullopt,
      };
      oracle.final_pool_identity = IndependentPoolIdentity(oracle.pools);
      return oracle;
    }

    std::uint64_t source_candidates = 0;
    std::uint64_t source_bytes = 0;
    for (const OwnedPool& pool : oracle.pools) {
      source_candidates += pool.candidates.size();
      for (const candidates::StoredCandidate& candidate : pool.candidates) {
        source_bytes += candidate->logical_bytes();
      }
    }
    const std::uint64_t targets = plan.targets.size();
    const std::uint64_t price_visits = targets * plan.price_snapshot.prices.size();
    const std::uint64_t reserved_work =
        targets * config.epoch.limits.maximum_cpu_work_units_per_query;
    const std::uint64_t reserved_bytes =
        targets * config.epoch.limits.maximum_generated_bytes_per_column;

    CpuTargetedRegenerationEpoch epoch = RequireEpoch(ExecuteCpuTargetedRegenerationEpoch(
        scenario.board(), scenario.capacities(), execution_pools, plan,
        prior.has_value() ? &*prior : nullptr, contexts, config.epoch));
    step.batch_identity = epoch.batch_identity();
    step.epoch_identity = epoch.epoch_identity();
    step.counters = epoch.counters();
    oracle.final_selection = epoch.refreshed_selection();

    std::vector<OwnedPool> next;
    next.reserve(epoch.pools().size());
    for (const CpuTargetedRegenerationPool& pool : epoch.pools()) {
      next.push_back(OwnedPool{
          .net = pool.net(),
          .candidates = std::vector<candidates::StoredCandidate>(pool.candidates().begin(),
                                                                 pool.candidates().end()),
      });
      std::ranges::sort(next.back().candidates, [](const auto& left, const auto& right) {
        return left->id() < right->id();
      });
    }
    std::ranges::sort(next, [](const OwnedPool& left, const OwnedPool& right) {
      return NetKey(left.net) < NetKey(right.net);
    });
    step.output_pool_identity = IndependentPoolIdentity(next);
    oracle.steps.push_back(std::move(step));

    ++oracle.counters.executed_epochs;
    oracle.counters.source_candidate_visits += source_candidates;
    oracle.counters.source_candidate_bytes += source_bytes;
    oracle.counters.target_count += targets;
    oracle.counters.route_query_count += epoch.counters().route_query_count;
    oracle.counters.price_projection_visits += price_visits;
    oracle.counters.reserved_cpu_work_units += reserved_work;
    oracle.counters.actual_cpu_work_units += epoch.counters().cpu_work_units;
    oracle.counters.reserved_generated_bytes += reserved_bytes;
    oracle.counters.actual_generated_bytes += epoch.counters().generated_bytes;
    oracle.counters.transaction_items += source_candidates + targets;
    oracle.counters.admitted_columns += epoch.counters().admitted_columns;
    oracle.counters.duplicate_columns += epoch.counters().duplicate_columns;
    oracle.counters.rejected_columns += epoch.counters().rejected_columns;
    prior = plan.price_snapshot;
    oracle.pools = std::move(next);
    ++executed;
    if (!plan_terminal_step && executed == execution_limit) {
      oracle.final_pool_identity = IndependentPoolIdentity(oracle.pools);
      return oracle;
    }
  }
}

[[nodiscard]] std::vector<std::vector<CandidateId>> PoolRoster(
    std::span<const CpuCandidateAllocationPool> pools) {
  std::vector<std::vector<CandidateId>> roster;
  roster.reserve(pools.size());
  for (const CpuCandidateAllocationPool& pool : pools) {
    std::vector<CandidateId>& ids = roster.emplace_back();
    ids.reserve(pool.candidates().size());
    for (const candidates::StoredCandidate& candidate : pool.candidates()) {
      ids.push_back(candidate->id());
    }
  }
  return roster;
}

[[nodiscard]] std::vector<std::vector<CandidateId>> PoolRoster(std::span<const OwnedPool> pools) {
  std::vector<std::vector<CandidateId>> roster;
  roster.reserve(pools.size());
  for (const OwnedPool& pool : pools) {
    std::vector<CandidateId>& ids = roster.emplace_back();
    ids.reserve(pool.candidates.size());
    for (const candidates::StoredCandidate& candidate : pool.candidates) {
      ids.push_back(candidate->id());
    }
  }
  return roster;
}

void HashSelectionForOracle(board_ir::StableHashBuilder* hash, const OneWorldSelection& selection) {
  hash->AddU64(selection.associations.board_content_hash);
  hash->AddU64(selection.associations.compiler_profile_fingerprint);
  hash->AddU32(selection.associations.geometry_compiler_version);
  hash->AddU64(selection.input_candidate_count);
  hash->AddU64(static_cast<std::uint64_t>(selection.nets.size()));
  for (const OneWorldNetOutcome& outcome : selection.nets) {
    if (const auto* selected = std::get_if<OneWorldSelectedCandidate>(&outcome);
        selected != nullptr) {
      hash->AddBool(true);
      hash->AddU64(selected->net.id);
      hash->AddU32(selected->net.generation);
      hash->AddU64(selected->candidate_id.high);
      hash->AddU64(selected->candidate_id.low);
    } else {
      const OneWorldCandidateAbsence& absence = std::get<OneWorldCandidateAbsence>(outcome);
      hash->AddBool(false);
      hash->AddU64(absence.net.id);
      hash->AddU32(absence.net.generation);
      hash->AddU32(static_cast<std::uint32_t>(absence.reason));
    }
  }
  const ResourceAccounting& accounting = selection.accounting;
  hash->AddU64(accounting.associations.board_content_hash);
  hash->AddU64(accounting.associations.compiler_profile_fingerprint);
  hash->AddU32(accounting.associations.geometry_compiler_version);
  hash->AddU64(accounting.candidate_count);
  hash->AddU64(accounting.expanded_resource_uses);
  hash->AddU64(accounting.overused_resource_count);
  hash->AddU64(accounting.total_overuse_units);
  hash->AddU64(static_cast<std::uint64_t>(accounting.resources.size()));
  for (const ResourceUsage& usage : accounting.resources) {
    hash->AddU32(usage.resource.layer);
    hash->AddI64(usage.resource.lattice_x);
    hash->AddI64(usage.resource.lattice_y);
    hash->AddU32(static_cast<std::uint32_t>(usage.resource.direction));
    hash->AddU32(usage.capacity_units);
    hash->AddU64(usage.usage_units);
    hash->AddU64(usage.overuse_units);
  }
}

void HashConfigForOracle(board_ir::StableHashBuilder* hash,
                         const CpuCandidateAllocationSessionConfig& config) {
  const NegotiatedRegenerationPlanConfig& planning = config.epoch.planning;
  hash->AddU64(planning.price_policy.initial_present_factor);
  hash->AddU64(planning.price_policy.present_factor_increment);
  hash->AddU64(planning.price_policy.historical_price_increment);
  const NegotiatedRegenerationPlanLimits& plan = planning.limits;
  hash->AddU64(plan.selection.maximum_net_pools);
  hash->AddU64(plan.selection.maximum_total_candidates);
  hash->AddU64(plan.selection.accounting.maximum_candidates);
  hash->AddU64(plan.selection.accounting.maximum_expanded_resource_uses);
  hash->AddU64(plan.selection.accounting.maximum_usage_units_per_resource);
  hash->AddU64(plan.maximum_epoch_index);
  hash->AddU64(plan.maximum_price_entries);
  hash->AddU64(plan.maximum_hot_resources);
  hash->AddU64(plan.maximum_targets);
  hash->AddU64(plan.maximum_selected_resource_uses_per_net);
  hash->AddU64(plan.maximum_aggregate_selected_resource_uses);
  hash->AddU64(plan.maximum_hot_resources_per_target);
  hash->AddU64(plan.maximum_aggregate_target_resource_links);
  hash->AddU64(plan.maximum_price_value);
  hash->AddU64(plan.maximum_aggregate_price);
  hash->AddU64(plan.maximum_target_price_per_net);
  hash->AddU64(plan.maximum_aggregate_target_price);

  const CpuTargetedRegenerationEpochLimits& epoch = config.epoch.limits;
  hash->AddU64(epoch.maximum_nets);
  hash->AddU64(epoch.maximum_source_candidates);
  hash->AddU64(epoch.maximum_source_candidate_bytes);
  hash->AddU64(epoch.maximum_targets);
  hash->AddU64(epoch.maximum_price_projection_visits);
  hash->AddU64(epoch.maximum_price_entries_per_target);
  hash->AddU64(epoch.maximum_aggregate_price_entries);
  hash->AddU64(epoch.maximum_cpu_work_units_per_query);
  hash->AddU64(epoch.maximum_aggregate_cpu_work_units);
  hash->AddU64(epoch.maximum_generated_bytes_per_column);
  hash->AddU64(epoch.maximum_aggregate_generated_bytes);
  hash->AddU64(epoch.maximum_retained_candidate_bytes);
  hash->AddU64(epoch.candidate_store.maximum_candidates_per_net);
  hash->AddU64(epoch.candidate_store.maximum_candidate_bytes_per_net);
  hash->AddU64(epoch.candidate_store.maximum_rejection_records);
  hash->AddU64(epoch.candidate_store.maximum_rejection_items_per_transaction);
  hash->AddU64(epoch.candidate_store.maximum_admission_items_per_transaction);
  hash->AddU64(epoch.candidate_store.maximum_admission_input_bytes_per_transaction);
  hash->AddU64(epoch.candidate_store.maximum_admission_work_units_per_transaction);
  hash->AddU64(epoch.refreshed_selection.maximum_net_pools);
  hash->AddU64(epoch.refreshed_selection.maximum_total_candidates);
  hash->AddU64(epoch.refreshed_selection.accounting.maximum_candidates);
  hash->AddU64(epoch.refreshed_selection.accounting.maximum_expanded_resource_uses);
  hash->AddU64(epoch.refreshed_selection.accounting.maximum_usage_units_per_resource);

  const CpuCandidateAllocationSessionLimits& limits = config.limits;
  hash->AddU64(limits.maximum_executed_epochs);
  hash->AddU64(limits.maximum_cumulative_source_candidate_visits);
  hash->AddU64(limits.maximum_cumulative_source_candidate_bytes);
  hash->AddU64(limits.maximum_cumulative_targets);
  hash->AddU64(limits.maximum_cumulative_price_projection_visits);
  hash->AddU64(limits.maximum_reserved_cpu_work_units);
  hash->AddU64(limits.maximum_reserved_generated_bytes);
  hash->AddU64(limits.maximum_cumulative_transaction_items);
}

[[nodiscard]] std::uint64_t IndependentSessionIdentity(
    const ManualComposition& oracle, const CpuCandidateAllocationSessionConfig& config) {
  board_ir::StableHashBuilder hash;
  hash.AddString("APGAR-P4R08-CPU-CANDIDATE-ALLOCATION-SESSION-V1");
  HashConfigForOracle(&hash, config);
  hash.AddU64(oracle.final_selection.associations.board_content_hash);
  hash.AddU64(oracle.final_selection.associations.compiler_profile_fingerprint);
  hash.AddU32(oracle.final_selection.associations.geometry_compiler_version);
  hash.AddU64(oracle.initial_pool_identity);
  hash.AddU64(oracle.final_pool_identity);
  hash.AddU64(static_cast<std::uint64_t>(oracle.steps.size()));
  for (const OracleStep& step : oracle.steps) {
    hash.AddU64(step.input_pool_identity);
    hash.AddU64(step.plan.plan_identity);
    hash.AddU64(step.plan.price_snapshot.prior_snapshot_identity);
    hash.AddU64(step.plan.price_snapshot.snapshot_identity);
    hash.AddU64(step.plan.price_snapshot.epoch_index);
    hash.AddU32(static_cast<std::uint32_t>(step.plan.disposition));
    hash.AddU64(step.plan.targets.size());
    hash.AddBool(step.batch_identity.has_value());
    if (step.batch_identity.has_value()) {
      hash.AddU64(*step.batch_identity);
    }
    hash.AddBool(step.epoch_identity.has_value());
    if (step.epoch_identity.has_value()) {
      hash.AddU64(*step.epoch_identity);
    }
    hash.AddBool(step.output_pool_identity.has_value());
    if (step.output_pool_identity.has_value()) {
      hash.AddU64(*step.output_pool_identity);
    }
    hash.AddU64(step.counters.source_candidate_count);
    hash.AddU64(step.counters.source_candidate_bytes);
    hash.AddU64(step.counters.target_count);
    hash.AddU64(step.counters.route_query_count);
    hash.AddU64(step.counters.price_projection_visits);
    hash.AddU64(step.counters.projected_price_entries);
    hash.AddU64(step.counters.cpu_work_units);
    hash.AddU64(step.counters.generated_bytes);
    hash.AddU64(step.counters.admitted_columns);
    hash.AddU64(step.counters.duplicate_columns);
    hash.AddU64(step.counters.rejected_columns);
    hash.AddU64(step.counters.retained_candidate_count);
    hash.AddU64(step.counters.retained_candidate_bytes);
  }
  const CpuCandidateAllocationSessionCounters& total = oracle.counters;
  hash.AddU64(total.planning_steps);
  hash.AddU64(total.executed_epochs);
  hash.AddU64(total.source_candidate_visits);
  hash.AddU64(total.source_candidate_bytes);
  hash.AddU64(total.target_count);
  hash.AddU64(total.route_query_count);
  hash.AddU64(total.price_projection_visits);
  hash.AddU64(total.reserved_cpu_work_units);
  hash.AddU64(total.actual_cpu_work_units);
  hash.AddU64(total.reserved_generated_bytes);
  hash.AddU64(total.actual_generated_bytes);
  hash.AddU64(total.transaction_items);
  hash.AddU64(total.admitted_columns);
  hash.AddU64(total.duplicate_columns);
  hash.AddU64(total.rejected_columns);
  hash.AddU32(static_cast<std::uint32_t>(oracle.stop.reason));
  hash.AddString(oracle.stop.invariant_id);
  hash.AddString(oracle.stop.detail);
  hash.AddBool(oracle.stop.expected_value.has_value());
  if (oracle.stop.expected_value.has_value()) {
    hash.AddU64(*oracle.stop.expected_value);
  }
  hash.AddBool(oracle.stop.actual_value.has_value());
  if (oracle.stop.actual_value.has_value()) {
    hash.AddU64(*oracle.stop.actual_value);
  }
  hash.AddBool(oracle.stop.plan_error_code.has_value());
  if (oracle.stop.plan_error_code.has_value()) {
    hash.AddU32(static_cast<std::uint32_t>(*oracle.stop.plan_error_code));
  }
  hash.AddBool(oracle.stop.epoch_error_code.has_value());
  if (oracle.stop.epoch_error_code.has_value()) {
    hash.AddU32(static_cast<std::uint32_t>(*oracle.stop.epoch_error_code));
  }
  hash.AddBool(oracle.terminal_plan.has_value());
  if (oracle.terminal_plan.has_value()) {
    hash.AddU64(oracle.terminal_plan->plan_identity);
  }
  HashSelectionForOracle(&hash, oracle.final_selection);
  const std::uint64_t value = hash.Finish();
  return value == 0 ? 1 : value;
}

void ExpectManualAgreement(const CpuCandidateAllocationSession& session,
                           const ManualComposition& oracle,
                           const CpuCandidateAllocationSessionConfig& config) {
  EXPECT_EQ(PoolRoster(session.pools()), PoolRoster(oracle.pools));
  EXPECT_EQ(session.final_selection(), oracle.final_selection);
  EXPECT_EQ(session.counters(), oracle.counters);
  EXPECT_EQ(session.stop(), oracle.stop);
  EXPECT_EQ(session.initial_pool_identity(), oracle.initial_pool_identity);
  EXPECT_EQ(session.final_pool_identity(), oracle.final_pool_identity);
  EXPECT_EQ(session.session_identity(), IndependentSessionIdentity(oracle, config));
  ASSERT_EQ(session.terminal_plan().has_value(), oracle.terminal_plan.has_value());
  if (oracle.terminal_plan.has_value()) {
    EXPECT_EQ(session.terminal_plan(), oracle.terminal_plan);
  }
  ASSERT_EQ(session.steps().size(), oracle.steps.size());
  for (std::size_t index = 0; index < oracle.steps.size(); ++index) {
    const CpuCandidateAllocationStep& actual = session.steps()[index];
    const OracleStep& expected = oracle.steps[index];
    EXPECT_EQ(actual.input_pool_identity, expected.input_pool_identity);
    EXPECT_EQ(actual.plan_identity, expected.plan.plan_identity);
    EXPECT_EQ(actual.prior_snapshot_identity, expected.plan.price_snapshot.prior_snapshot_identity);
    EXPECT_EQ(actual.price_snapshot_identity, expected.plan.price_snapshot.snapshot_identity);
    EXPECT_EQ(actual.price_epoch_index, expected.plan.price_snapshot.epoch_index);
    EXPECT_EQ(actual.disposition, expected.plan.disposition);
    EXPECT_EQ(actual.target_count, expected.plan.targets.size());
    EXPECT_EQ(actual.batch_identity, expected.batch_identity);
    EXPECT_EQ(actual.epoch_identity, expected.epoch_identity);
    EXPECT_EQ(actual.output_pool_identity, expected.output_pool_identity);
    EXPECT_EQ(actual.epoch_counters, expected.counters);
    if (index == 0) {
      EXPECT_EQ(actual.prior_snapshot_identity, 0U);
    } else {
      EXPECT_EQ(actual.prior_snapshot_identity, session.steps()[index - 1].price_snapshot_identity);
      ASSERT_TRUE(session.steps()[index - 1].output_pool_identity.has_value());
      EXPECT_EQ(actual.input_pool_identity, *session.steps()[index - 1].output_pool_identity);
    }
  }
}

struct InputSnapshot {
  std::vector<candidates::GeneratedRouteCandidate> candidates;
  std::vector<routing::PlanarRouteRequest> requests;
};

[[nodiscard]] InputSnapshot SnapshotInputs(const Scenario& scenario) {
  InputSnapshot snapshot;
  for (const candidates::StoredCandidate& candidate : scenario.source_candidates()) {
    snapshot.candidates.push_back(candidate->data());
  }
  for (const CpuTargetedRegenerationNetContext& context : scenario.contexts()) {
    snapshot.requests.push_back(context.request);
  }
  return snapshot;
}

void ExpectInputsUnchanged(const Scenario& scenario, const InputSnapshot& before) {
  ASSERT_EQ(scenario.source_candidates().size(), before.candidates.size());
  ASSERT_EQ(scenario.contexts().size(), before.requests.size());
  for (std::size_t index = 0; index < before.candidates.size(); ++index) {
    EXPECT_EQ(scenario.source_candidates()[index]->data(), before.candidates[index]);
  }
  for (std::size_t index = 0; index < before.requests.size(); ++index) {
    EXPECT_EQ(scenario.contexts()[index].request, before.requests[index]);
  }
}

TEST(CpuCandidateAllocationSessionTest, ZeroEpochFixedPointPreservesAuthoritativeInputPools) {
  Scenario scenario;
  std::vector<CpuTargetedRegenerationSourcePool> source_pools = scenario.no_op_source_pools();
  CpuCandidateAllocationSessionConfig config;
  const ManualComposition oracle =
      ComposeManually(scenario, source_pools, scenario.no_op_contexts(), config,
                      config.limits.maximum_executed_epochs);
  CpuCandidateAllocationSessionResult result = RunCpuCandidateAllocationSession(
      scenario.board(), scenario.capacities(), source_pools, scenario.no_op_contexts(), config);
  const CpuCandidateAllocationSession& session = Success(result);

  EXPECT_EQ(session.stop().reason, CpuCandidateAllocationStopReason::kFixedPoint);
  EXPECT_EQ(session.counters().planning_steps, 1U);
  EXPECT_EQ(session.counters().executed_epochs, 0U);
  ASSERT_EQ(session.steps().size(), 1U);
  EXPECT_FALSE(session.steps().front().batch_identity.has_value());
  EXPECT_FALSE(session.steps().front().epoch_identity.has_value());
  EXPECT_FALSE(session.steps().front().output_pool_identity.has_value());
  ASSERT_TRUE(session.terminal_plan().has_value());
  EXPECT_TRUE(session.terminal_plan()->targets.empty());
  EXPECT_EQ(session.initial_pool_identity(), session.final_pool_identity());
  EXPECT_EQ(PoolRoster(session.pools()),
            std::vector<std::vector<CandidateId>>{{scenario.source_candidates()[0]->id()}});
  ExpectManualAgreement(session, oracle, config);
}

TEST(CpuCandidateAllocationSessionTest,
     TwoEpochCompositionMatchesManualOracleAndRetainsCompletePools) {
  Scenario scenario;
  std::vector<CpuTargetedRegenerationSourcePool> source_pools = scenario.all_source_pools();
  CpuCandidateAllocationSessionConfig config;
  config.limits.maximum_executed_epochs = 2;
  ManualComposition oracle =
      ComposeManually(scenario, source_pools, scenario.contexts(), config, 2);

  CpuCandidateAllocationSessionResult result = RunCpuCandidateAllocationSession(
      scenario.board(), scenario.capacities(), source_pools, scenario.contexts(), config);
  const CpuCandidateAllocationSession& session = Success(result);
  EXPECT_EQ(session.stop().reason, CpuCandidateAllocationStopReason::kSessionBoundExhausted);
  EXPECT_EQ(session.stop().invariant_id, "allocator.cpu_allocation_session.epoch_count_bound.v1");
  EXPECT_EQ(session.counters().executed_epochs, 2U);
  EXPECT_EQ(session.counters().planning_steps, 3U);
  ExpectManualAgreement(session, oracle, config);

  EXPECT_TRUE(std::ranges::any_of(session.pools(), [](const CpuCandidateAllocationPool& pool) {
    return pool.candidates().size() > 1;
  }));
  for (const OneWorldNetOutcome& outcome : session.final_selection().nets) {
    const auto* selected = std::get_if<OneWorldSelectedCandidate>(&outcome);
    if (selected == nullptr) {
      continue;
    }
    const auto pool = std::ranges::find_if(session.pools(), [selected](const auto& candidate_pool) {
      return candidate_pool.net() == selected->net;
    });
    ASSERT_NE(pool, session.pools().end());
    EXPECT_TRUE(std::ranges::any_of(pool->candidates(), [selected](const auto& candidate) {
      return candidate->id() == selected->candidate_id;
    }));
  }
}

TEST(CpuCandidateAllocationSessionTest,
     ReplayValidatorDirectlyRejectsEveryMalformedPrivateStateClass) {
  Scenario scenario;
  const std::vector<CpuTargetedRegenerationSourcePool> source_pools = scenario.all_source_pools();
  CpuCandidateAllocationSessionConfig config;
  config.limits.maximum_executed_epochs = 2;
  const auto make_session = [&] {
    return RunCpuCandidateAllocationSession(scenario.board(), scenario.capacities(), source_pools,
                                            scenario.contexts(), config);
  };

  CpuCandidateAllocationSessionResult valid_result = make_session();
  const CpuCandidateAllocationSession& valid = Success(valid_result);
  EXPECT_FALSE(ValidateCpuCandidateAllocationSessionReplay(scenario.board(), scenario.capacities(),
                                                           valid, config)
                   .has_value());

  CpuCandidateAllocationSessionConfig invalid_config = config;
  invalid_config.limits.maximum_executed_epochs = 0;
  const auto configuration_failure = ValidateCpuCandidateAllocationSessionReplay(
      scenario.board(), scenario.capacities(), valid, invalid_config);
  ASSERT_TRUE(configuration_failure.has_value());
  EXPECT_EQ(configuration_failure->invariant_id,
            "allocator.cpu_allocation_session.replay_configuration.v1");

  const board_ir::BoardSnapshot other_board =
      test_support::Snapshot(SessionBoardData(/*revision=*/2));
  const auto association_failure = ValidateCpuCandidateAllocationSessionReplay(
      other_board, scenario.capacities(), valid, config);
  ASSERT_TRUE(association_failure.has_value());
  EXPECT_EQ(association_failure->invariant_id,
            "allocator.cpu_allocation_session.replay_capacity_board.v1");

  struct FaultCase {
    internal::CpuCandidateAllocationSessionReplayFault fault;
    std::string_view invariant_id;
  };
  const std::array<FaultCase, 15> faults = {
      FaultCase{internal::CpuCandidateAllocationSessionReplayFault::kShape,
                "allocator.cpu_allocation_session.replay_shape.v1"},
      FaultCase{internal::CpuCandidateAllocationSessionReplayFault::kPoolOrder,
                "allocator.cpu_allocation_session.replay_pool_order.v1"},
      FaultCase{internal::CpuCandidateAllocationSessionReplayFault::kCandidateOrder,
                "allocator.cpu_allocation_session.replay_candidate_order.v1"},
      FaultCase{internal::CpuCandidateAllocationSessionReplayFault::kFinalPoolIdentity,
                "allocator.cpu_allocation_session.replay_final_pool_identity.v1"},
      FaultCase{internal::CpuCandidateAllocationSessionReplayFault::kFinalSelection,
                "allocator.cpu_allocation_session.replay_final_selection.v1"},
      FaultCase{internal::CpuCandidateAllocationSessionReplayFault::kStepLineage,
                "allocator.cpu_allocation_session.replay_step_lineage.v1"},
      FaultCase{internal::CpuCandidateAllocationSessionReplayFault::kCounterShape,
                "allocator.cpu_allocation_session.replay_counter_shape.v1"},
      FaultCase{internal::CpuCandidateAllocationSessionReplayFault::kCounterAgreement,
                "allocator.cpu_allocation_session.replay_counter_agreement.v1"},
      FaultCase{internal::CpuCandidateAllocationSessionReplayFault::kTerminalStep,
                "allocator.cpu_allocation_session.replay_terminal_step.v1"},
      FaultCase{internal::CpuCandidateAllocationSessionReplayFault::kFixedPointShape,
                "allocator.cpu_allocation_session.replay_fixed_point.v1"},
      FaultCase{internal::CpuCandidateAllocationSessionReplayFault::kSessionBoundShape,
                "allocator.cpu_allocation_session.replay_session_bound.v1"},
      FaultCase{internal::CpuCandidateAllocationSessionReplayFault::kEpochBoundShape,
                "allocator.cpu_allocation_session.replay_epoch_bound.v1"},
      FaultCase{internal::CpuCandidateAllocationSessionReplayFault::kTerminalPlan,
                "allocator.cpu_allocation_session.replay_terminal_plan.v1"},
      FaultCase{internal::CpuCandidateAllocationSessionReplayFault::kSessionIdentity,
                "allocator.cpu_allocation_session.replay_identity.v1"},
      FaultCase{internal::CpuCandidateAllocationSessionReplayFault::kAggregateCounters,
                "allocator.cpu_allocation_session.replay_counters.v1"},
  };
  for (const FaultCase& fault : faults) {
    SCOPED_TRACE(fault.invariant_id);
    CpuCandidateAllocationSessionResult corrupted_result = make_session();
    auto* corrupted = std::get_if<CpuCandidateAllocationSession>(&corrupted_result);
    ASSERT_NE(corrupted, nullptr);
    ASSERT_TRUE(
        internal::InjectCpuCandidateAllocationSessionReplayFaultForTesting(corrupted, fault.fault));
    const auto failure = ValidateCpuCandidateAllocationSessionReplay(
        scenario.board(), scenario.capacities(), *corrupted, config);
    ASSERT_TRUE(failure.has_value());
    EXPECT_EQ(failure->invariant_id, fault.invariant_id);
  }
}

TEST(CpuCandidateAllocationSessionTest, EpochAndSessionExhaustionHaveDistinctTypedStops) {
  Scenario scenario;
  std::vector<CpuTargetedRegenerationSourcePool> source_pools = scenario.all_source_pools();
  const NegotiatedRegenerationPlan first_plan = RequirePlan(
      PlanNegotiatedRegeneration(scenario.board(), scenario.capacities(), scenario.all_pools()));

  CpuCandidateAllocationSessionConfig epoch_bound;
  const std::uint64_t reserved_work =
      first_plan.targets.size() * epoch_bound.epoch.limits.maximum_cpu_work_units_per_query;
  epoch_bound.epoch.limits.maximum_aggregate_cpu_work_units = reserved_work - 1U;
  CpuCandidateAllocationSessionResult epoch_result = RunCpuCandidateAllocationSession(
      scenario.board(), scenario.capacities(), source_pools, scenario.contexts(), epoch_bound);
  const CpuCandidateAllocationSession& epoch_session = Success(epoch_result);
  EXPECT_EQ(epoch_session.stop().reason, CpuCandidateAllocationStopReason::kEpochBoundExhausted);
  EXPECT_EQ(epoch_session.stop().invariant_id, "allocator.targeted_epoch.aggregate_work_bound.v1");
  EXPECT_EQ(epoch_session.stop().epoch_error_code,
            CpuTargetedRegenerationEpochErrorCode::kBoundExhausted);
  EXPECT_EQ(epoch_session.counters().executed_epochs, 0U);

  CpuCandidateAllocationSessionConfig session_bound;
  session_bound.limits.maximum_executed_epochs = 1;
  CpuCandidateAllocationSessionResult session_result = RunCpuCandidateAllocationSession(
      scenario.board(), scenario.capacities(), source_pools, scenario.contexts(), session_bound);
  const CpuCandidateAllocationSession& bounded = Success(session_result);
  EXPECT_EQ(bounded.stop().reason, CpuCandidateAllocationStopReason::kSessionBoundExhausted);
  EXPECT_EQ(bounded.stop().invariant_id, "allocator.cpu_allocation_session.epoch_count_bound.v1");
  EXPECT_EQ(bounded.counters().executed_epochs, 1U);

  CpuCandidateAllocationSessionConfig planner_bound;
  planner_bound.epoch.planning.limits.maximum_epoch_index = 0;
  const ManualComposition prefix =
      ComposeManually(scenario, source_pools, scenario.contexts(), planner_bound, 1, false);
  CpuCandidateAllocationSessionResult planner_result = RunCpuCandidateAllocationSession(
      scenario.board(), scenario.capacities(), source_pools, scenario.contexts(), planner_bound);
  const CpuCandidateAllocationSession& planner = Success(planner_result);
  EXPECT_EQ(planner.stop().reason, CpuCandidateAllocationStopReason::kEpochBoundExhausted);
  EXPECT_EQ(planner.stop().invariant_id, "allocator.negotiated_plan.epoch_bound.v1");
  EXPECT_EQ(planner.stop().plan_error_code, NegotiatedRegenerationPlanErrorCode::kBoundExhausted);
  EXPECT_EQ(planner.counters().executed_epochs, 1U);
  EXPECT_EQ(planner.counters().planning_steps, 1U);
  EXPECT_FALSE(planner.terminal_plan().has_value());
  ASSERT_EQ(planner.steps().size(), 1U);
  ASSERT_EQ(prefix.steps.size(), 1U);
  EXPECT_EQ(planner.steps().front().output_pool_identity,
            prefix.steps.front().output_pool_identity);
  EXPECT_EQ(PoolRoster(planner.pools()), PoolRoster(prefix.pools));
  EXPECT_EQ(planner.final_selection(), prefix.final_selection);
  EXPECT_EQ(planner.final_pool_identity(), prefix.final_pool_identity);
  EXPECT_EQ(planner.counters(), prefix.counters);
}

TEST(CpuCandidateAllocationSessionTest,
     InitialPlannerBoundIsFatalUntilAnAuthoritativeEpochSelectionExists) {
  Scenario scenario;
  std::vector<CpuTargetedRegenerationSourcePool> source_pools = scenario.all_source_pools();
  const NegotiatedRegenerationPlan first_plan = RequirePlan(
      PlanNegotiatedRegeneration(scenario.board(), scenario.capacities(), scenario.all_pools()));
  ASSERT_GT(first_plan.hot_resources.size(), 1U);

  CpuCandidateAllocationSessionConfig config;
  config.epoch.planning.limits.maximum_hot_resources = first_plan.hot_resources.size() - 1U;
  CpuCandidateAllocationSessionResult result = RunCpuCandidateAllocationSession(
      scenario.board(), scenario.capacities(), source_pools, scenario.contexts(), config);
  const CpuCandidateAllocationSessionError& error = Failure(result);
  EXPECT_EQ(error.code, CpuCandidateAllocationSessionErrorCode::kPlanningFailure);
  EXPECT_EQ(error.invariant_id, "allocator.negotiated_plan.hot_resource_bound.v1");
  ASSERT_TRUE(error.plan_error.has_value());
  EXPECT_EQ(error.plan_error->code, NegotiatedRegenerationPlanErrorCode::kBoundExhausted);
  EXPECT_EQ(error.expected_value, first_plan.hot_resources.size() - 1U);
  EXPECT_EQ(error.actual_value, first_plan.hot_resources.size());
}

TEST(CpuCandidateAllocationSessionTest,
     InvalidCallerResourcePolicyAndNonUniqueNetRosterFailBeforePlanning) {
  Scenario scenario;
  std::vector<CpuTargetedRegenerationSourcePool> source_pools = scenario.all_source_pools();
  const NegotiatedRegenerationPlan first_plan = RequirePlan(
      PlanNegotiatedRegeneration(scenario.board(), scenario.capacities(), scenario.all_pools()));
  ASSERT_FALSE(first_plan.price_snapshot.prices.empty());

  std::vector<CpuTargetedRegenerationNetContext> policy_contexts(scenario.contexts().begin(),
                                                                 scenario.contexts().end());
  policy_contexts.front().request.candidate_policy.banned_resources.push_back(
      first_plan.price_snapshot.prices.front().resource);
  CpuCandidateAllocationSessionResult policy_result = RunCpuCandidateAllocationSession(
      scenario.board(), scenario.capacities(), source_pools, policy_contexts);
  const CpuCandidateAllocationSessionError& policy_error = Failure(policy_result);
  EXPECT_EQ(policy_error.code, CpuCandidateAllocationSessionErrorCode::kInvalidInput);
  EXPECT_EQ(policy_error.invariant_id,
            "allocator.cpu_allocation_session.caller_resource_policy.v1");

  std::vector<CpuTargetedRegenerationNetContext> duplicate_contexts(scenario.contexts().begin(),
                                                                    scenario.contexts().end());
  duplicate_contexts.back().request.net = duplicate_contexts.front().request.net;
  CpuCandidateAllocationSessionResult duplicate_result = RunCpuCandidateAllocationSession(
      scenario.board(), scenario.capacities(), source_pools, duplicate_contexts);
  const CpuCandidateAllocationSessionError& duplicate_error = Failure(duplicate_result);
  EXPECT_EQ(duplicate_error.code, CpuCandidateAllocationSessionErrorCode::kInvalidInput);
  EXPECT_EQ(duplicate_error.invariant_id,
            "allocator.cpu_allocation_session.duplicate_or_missing_net.v1");
}

TEST(CpuCandidateAllocationSessionTest,
     EverySessionReservationBoundAcceptsEqualityAndStopsOneUnderBeforeRouting) {
  Scenario scenario;
  std::vector<CpuTargetedRegenerationSourcePool> source_pools = scenario.all_source_pools();
  const NegotiatedRegenerationPlan first_plan = RequirePlan(
      PlanNegotiatedRegeneration(scenario.board(), scenario.capacities(), scenario.all_pools()));
  ASSERT_FALSE(first_plan.targets.empty());

  std::uint64_t source_count = 0;
  std::uint64_t source_bytes = 0;
  for (const CpuTargetedRegenerationSourcePool& pool : source_pools) {
    source_count += pool.candidates.size();
    for (const candidates::StoredCandidate& candidate : pool.candidates) {
      source_bytes += candidate->logical_bytes();
    }
  }
  CpuCandidateAllocationSessionConfig defaults;
  const std::uint64_t targets = first_plan.targets.size();
  struct BoundCase {
    std::uint64_t CpuCandidateAllocationSessionLimits::* limit;
    std::uint64_t CpuCandidateAllocationSessionCounters::* counter;
    std::uint64_t contribution;
    std::string_view invariant_id;
  };
  const std::array<BoundCase, 7> bounds = {
      BoundCase{&CpuCandidateAllocationSessionLimits::maximum_cumulative_source_candidate_visits,
                &CpuCandidateAllocationSessionCounters::source_candidate_visits, source_count,
                "allocator.cpu_allocation_session.source_candidate_visit_bound.v1"},
      BoundCase{&CpuCandidateAllocationSessionLimits::maximum_cumulative_source_candidate_bytes,
                &CpuCandidateAllocationSessionCounters::source_candidate_bytes, source_bytes,
                "allocator.cpu_allocation_session.source_candidate_byte_bound.v1"},
      BoundCase{&CpuCandidateAllocationSessionLimits::maximum_cumulative_targets,
                &CpuCandidateAllocationSessionCounters::target_count, targets,
                "allocator.cpu_allocation_session.target_bound.v1"},
      BoundCase{&CpuCandidateAllocationSessionLimits::maximum_cumulative_price_projection_visits,
                &CpuCandidateAllocationSessionCounters::price_projection_visits,
                targets * first_plan.price_snapshot.prices.size(),
                "allocator.cpu_allocation_session.price_projection_visit_bound.v1"},
      BoundCase{&CpuCandidateAllocationSessionLimits::maximum_reserved_cpu_work_units,
                &CpuCandidateAllocationSessionCounters::reserved_cpu_work_units,
                targets * defaults.epoch.limits.maximum_cpu_work_units_per_query,
                "allocator.cpu_allocation_session.cpu_work_reservation_bound.v1"},
      BoundCase{&CpuCandidateAllocationSessionLimits::maximum_reserved_generated_bytes,
                &CpuCandidateAllocationSessionCounters::reserved_generated_bytes,
                targets * defaults.epoch.limits.maximum_generated_bytes_per_column,
                "allocator.cpu_allocation_session.generated_byte_reservation_bound.v1"},
      BoundCase{&CpuCandidateAllocationSessionLimits::maximum_cumulative_transaction_items,
                &CpuCandidateAllocationSessionCounters::transaction_items, source_count + targets,
                "allocator.cpu_allocation_session.transaction_item_bound.v1"},
  };

  for (const BoundCase& bound : bounds) {
    SCOPED_TRACE(bound.invariant_id);
    CpuCandidateAllocationSessionConfig exact = defaults;
    exact.limits.maximum_executed_epochs = 1;
    exact.limits.*bound.limit = bound.contribution;
    CpuCandidateAllocationSessionResult exact_result = RunCpuCandidateAllocationSession(
        scenario.board(), scenario.capacities(), source_pools, scenario.contexts(), exact);
    const CpuCandidateAllocationSession& exact_session = Success(exact_result);
    EXPECT_EQ(exact_session.counters().executed_epochs, 1U);
    EXPECT_EQ(exact_session.counters().*bound.counter, bound.contribution);

    CpuCandidateAllocationSessionConfig one_under = exact;
    --(one_under.limits.*bound.limit);
    CpuCandidateAllocationSessionResult under_result = RunCpuCandidateAllocationSession(
        scenario.board(), scenario.capacities(), source_pools, scenario.contexts(), one_under);
    const CpuCandidateAllocationSession& under = Success(under_result);
    EXPECT_EQ(under.stop().reason, CpuCandidateAllocationStopReason::kSessionBoundExhausted);
    EXPECT_EQ(under.stop().invariant_id, bound.invariant_id);
    EXPECT_EQ(under.stop().expected_value, bound.contribution - 1U);
    EXPECT_EQ(under.stop().actual_value, bound.contribution);
    EXPECT_EQ(under.counters().executed_epochs, 0U);
    EXPECT_EQ(under.counters().route_query_count, 0U);
    EXPECT_EQ(under.initial_pool_identity(), under.final_pool_identity());
    ASSERT_EQ(under.steps().size(), 1U);
    EXPECT_FALSE(under.steps().front().output_pool_identity.has_value());
  }

  CpuCandidateAllocationSessionConfig two_epoch_config = defaults;
  two_epoch_config.limits.maximum_executed_epochs = 2;
  const ManualComposition two_epoch =
      ComposeManually(scenario, source_pools, scenario.contexts(), two_epoch_config, 2);
  ASSERT_EQ(two_epoch.counters.executed_epochs, 2U);
  for (const BoundCase& bound : bounds) {
    SCOPED_TRACE(std::string{"cumulative: "} + std::string{bound.invariant_id});
    const std::uint64_t cumulative = two_epoch.counters.*bound.counter;
    ASSERT_GT(cumulative, bound.contribution);

    CpuCandidateAllocationSessionConfig exact = two_epoch_config;
    exact.limits.*bound.limit = cumulative;
    CpuCandidateAllocationSessionResult exact_result = RunCpuCandidateAllocationSession(
        scenario.board(), scenario.capacities(), source_pools, scenario.contexts(), exact);
    const CpuCandidateAllocationSession& exact_session = Success(exact_result);
    EXPECT_EQ(exact_session.counters().executed_epochs, 2U);
    EXPECT_EQ(exact_session.counters().*bound.counter, cumulative);

    CpuCandidateAllocationSessionConfig one_under = exact;
    --(one_under.limits.*bound.limit);
    CpuCandidateAllocationSessionResult under_result = RunCpuCandidateAllocationSession(
        scenario.board(), scenario.capacities(), source_pools, scenario.contexts(), one_under);
    const CpuCandidateAllocationSession& under = Success(under_result);
    EXPECT_EQ(under.stop().reason, CpuCandidateAllocationStopReason::kSessionBoundExhausted);
    EXPECT_EQ(under.stop().invariant_id, bound.invariant_id);
    EXPECT_EQ(under.stop().expected_value, cumulative - 1U);
    EXPECT_EQ(under.stop().actual_value, cumulative);
    EXPECT_EQ(under.counters().executed_epochs, 1U);
    EXPECT_LT(under.counters().*bound.counter, cumulative);
    EXPECT_EQ(under.counters().route_query_count,
              two_epoch.steps.front().counters.route_query_count);
    ASSERT_EQ(under.steps().size(), 2U);
    ASSERT_TRUE(under.steps().front().output_pool_identity.has_value());
    EXPECT_EQ(under.final_pool_identity(), *under.steps().front().output_pool_identity);
    EXPECT_FALSE(under.steps().back().output_pool_identity.has_value());
  }

  CpuCandidateAllocationSessionConfig byte_diagnostic = defaults;
  byte_diagnostic.epoch.limits.maximum_source_candidate_bytes = source_bytes - 1U;
  CpuCandidateAllocationSessionResult diagnostic_result = RunCpuCandidateAllocationSession(
      scenario.board(), scenario.capacities(), source_pools, scenario.contexts(), byte_diagnostic);
  const CpuCandidateAllocationSessionError& diagnostic = Failure(diagnostic_result);
  EXPECT_EQ(diagnostic.invariant_id,
            "allocator.cpu_allocation_session.source_aggregate_byte_bound.v1");
  EXPECT_EQ(diagnostic.expected_value, source_bytes - 1U);
  EXPECT_EQ(diagnostic.actual_value, source_bytes);
}

void CorruptRetainedPriorChain(std::size_t planning_step, NegotiatedPriceSnapshot* prior_snapshot,
                               void* opaque) noexcept {
  if (planning_step == 1 && prior_snapshot != nullptr) {
    ++prior_snapshot->prior_snapshot_identity;
    *static_cast<bool*>(opaque) = true;
  }
}

TEST(CpuCandidateAllocationSessionTest,
     InvalidReplayChainAfterSuccessfulPrefixIsFatalAtomicAndLeavesInputsImmutable) {
  Scenario scenario;
  const InputSnapshot before = SnapshotInputs(scenario);
  std::vector<CpuTargetedRegenerationSourcePool> source_pools = scenario.all_source_pools();
  bool corrupted = false;
  CpuCandidateAllocationSessionResult result =
      internal::RunCpuCandidateAllocationSessionWithTestHooks(
          scenario.board(), scenario.capacities(), source_pools, scenario.contexts(), {},
          internal::CpuCandidateAllocationSessionTestHooks{
              .before_planning = CorruptRetainedPriorChain,
              .context = &corrupted,
          });
  const CpuCandidateAllocationSessionError& error = Failure(result);

  EXPECT_TRUE(corrupted);
  EXPECT_EQ(error.code, CpuCandidateAllocationSessionErrorCode::kInvalidInput);
  EXPECT_EQ(error.invariant_id, "allocator.negotiated_plan.prior_chain.v1");
  ASSERT_TRUE(error.plan_error.has_value());
  EXPECT_EQ(error.plan_error->code, NegotiatedRegenerationPlanErrorCode::kInvalidInput);
  ExpectInputsUnchanged(scenario, before);
}

TEST(CpuCandidateAllocationSessionTest,
     CallerPermutationsAndRepeatedRunsPreserveIdentityStepsPoolsAndInputs) {
  Scenario scenario;
  const InputSnapshot before = SnapshotInputs(scenario);
  CpuCandidateAllocationSessionConfig config;
  config.limits.maximum_executed_epochs = 2;
  std::vector<CpuTargetedRegenerationSourcePool> source_pools = scenario.all_source_pools();
  CpuCandidateAllocationSessionResult first_result = RunCpuCandidateAllocationSession(
      scenario.board(), scenario.capacities(), source_pools, scenario.contexts(), config);
  const CpuCandidateAllocationSession& first = Success(first_result);

  std::vector<CpuTargetedRegenerationNetContext> contexts(scenario.contexts().begin(),
                                                          scenario.contexts().end());
  std::vector<CpuTargetedRegenerationSourcePool> reversed_pools = source_pools;
  std::ranges::reverse(reversed_pools);
  CpuCandidateAllocationSessionResult pool_permutation_result = RunCpuCandidateAllocationSession(
      scenario.board(), scenario.capacities(), reversed_pools, contexts, config);
  const CpuCandidateAllocationSession& pool_permutation = Success(pool_permutation_result);

  std::ranges::reverse(contexts);
  CpuCandidateAllocationSessionResult context_permutation_result = RunCpuCandidateAllocationSession(
      scenario.board(), scenario.capacities(), source_pools, contexts, config);
  const CpuCandidateAllocationSession& context_permutation = Success(context_permutation_result);
  CpuCandidateAllocationSessionResult both_permuted_result = RunCpuCandidateAllocationSession(
      scenario.board(), scenario.capacities(), reversed_pools, contexts, config);
  const CpuCandidateAllocationSession& both_permuted = Success(both_permuted_result);
  CpuCandidateAllocationSessionResult repeated_result = RunCpuCandidateAllocationSession(
      scenario.board(), scenario.capacities(), reversed_pools, contexts, config);
  const CpuCandidateAllocationSession& repeated = Success(repeated_result);

  for (const CpuCandidateAllocationSession* equivalent :
       {&pool_permutation, &context_permutation, &both_permuted, &repeated}) {
    EXPECT_EQ(first.session_identity(), equivalent->session_identity());
    EXPECT_EQ(first.initial_pool_identity(), equivalent->initial_pool_identity());
    EXPECT_EQ(first.final_pool_identity(), equivalent->final_pool_identity());
    EXPECT_TRUE(std::ranges::equal(first.steps(), equivalent->steps()));
    EXPECT_EQ(PoolRoster(first.pools()), PoolRoster(equivalent->pools()));
    EXPECT_EQ(first.final_selection(), equivalent->final_selection());
    EXPECT_EQ(first.counters(), equivalent->counters());
    EXPECT_EQ(first.stop(), equivalent->stop());
    EXPECT_EQ(first.terminal_plan(), equivalent->terminal_plan());
  }

  CpuCandidateAllocationSessionConfig identity_variant = config;
  ++identity_variant.limits.maximum_cumulative_targets;
  CpuCandidateAllocationSessionResult variant_result = RunCpuCandidateAllocationSession(
      scenario.board(), scenario.capacities(), source_pools, scenario.contexts(), identity_variant);
  const CpuCandidateAllocationSession& variant = Success(variant_result);
  EXPECT_EQ(first.final_pool_identity(), variant.final_pool_identity());
  EXPECT_EQ(first.counters(), variant.counters());
  EXPECT_NE(first.session_identity(), variant.session_identity());

  CpuCandidateAllocationSessionConfig retained_config;
  retained_config.limits.maximum_executed_epochs = 1;
  ManualComposition retained =
      ComposeManually(scenario, source_pools, scenario.contexts(), retained_config, 1);
  std::vector<CpuTargetedRegenerationSourcePool> retained_views = SourcePoolViews(retained.pools);
  CpuCandidateAllocationSessionResult retained_result =
      RunCpuCandidateAllocationSession(scenario.board(), scenario.capacities(), retained_views,
                                       scenario.contexts(), retained_config);
  const CpuCandidateAllocationSession& retained_order = Success(retained_result);
  auto multiple = std::ranges::find_if(
      retained.pools, [](const OwnedPool& pool) { return pool.candidates.size() > 1; });
  ASSERT_NE(multiple, retained.pools.end());
  std::ranges::reverse(multiple->candidates);
  retained_views = SourcePoolViews(retained.pools);
  CpuCandidateAllocationSessionResult reversed_candidates_result =
      RunCpuCandidateAllocationSession(scenario.board(), scenario.capacities(), retained_views,
                                       scenario.contexts(), retained_config);
  const CpuCandidateAllocationSession& reversed_candidates = Success(reversed_candidates_result);
  EXPECT_EQ(retained_order.initial_pool_identity(), reversed_candidates.initial_pool_identity());
  EXPECT_EQ(retained_order.session_identity(), reversed_candidates.session_identity());
  EXPECT_TRUE(std::ranges::equal(retained_order.steps(), reversed_candidates.steps()));
  EXPECT_EQ(PoolRoster(retained_order.pools()), PoolRoster(reversed_candidates.pools()));
  ExpectInputsUnchanged(scenario, before);
}

}  // namespace
}  // namespace apgar::allocator
