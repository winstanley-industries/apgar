#include "apgar/allocator/cpu_targeted_regeneration_epoch.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <ranges>
#include <set>
#include <span>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

#include "apgar/allocator/negotiated_regeneration_plan.h"
#include "apgar/allocator/resource_accounting.h"
#include "apgar/board_ir/board.h"
#include "apgar/candidates/route_candidate.h"
#include "apgar/geometry_compiler/compiled_board.h"
#include "apgar/routing/candidate_policy.h"
#include "apgar/routing/cpu_astar.h"
#include "src/allocator/cpu_targeted_regeneration_epoch_internal.h"
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
                                    std::string pin, board_ir::LayerId layer = kLayer) {
  return Terminal{
      .ref = EntityRef{.id = id, .generation = 0},
      .net = net,
      .component = "P4R07",
      .pin = std::move(pin),
      .center = center,
      .connection_region = TerminalBox(center),
      .layers = {layer},
  };
}

[[nodiscard]] BoardData EpochBoardData(std::uint64_t revision = 1) {
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
      .adapter_name = "p4r07-exact-small",
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

[[nodiscard]] CompilerProfile EpochProfile() {
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
      : board_(test_support::Snapshot(EpochBoardData())),
        compiled_(),
        source_candidates_(),
        hot_a_{},
        hot_b_{},
        contexts_(),
        capacities_(std::nullopt) {
    compiled_.reserve(kNets.size());
    for (EntityRef net : kNets) {
      compiled_.push_back(test_support::CompilePreparedNet(board_, net, EpochProfile()));
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
          test_support::CandidateDraft(board_, compiled_[index], request, 0x700, 0x710 + index));
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
  [[nodiscard]] const std::vector<CompiledBoard>& compiled() const noexcept { return compiled_; }
  [[nodiscard]] const std::vector<candidates::StoredCandidate>& source_candidates() const noexcept {
    return source_candidates_;
  }
  [[nodiscard]] std::vector<OneWorldCandidatePool> AllPools() const {
    return {
        OneWorldCandidatePool{.net = kNets[0], .candidates = hot_a_},
        OneWorldCandidatePool{.net = kNets[1], .candidates = hot_b_},
        OneWorldCandidatePool{.net = kNets[2], .candidates = {}},
        OneWorldCandidatePool{.net = kNets[3], .candidates = {}},
    };
  }
  [[nodiscard]] std::vector<OneWorldCandidatePool> NoOpPools() const {
    return {OneWorldCandidatePool{.net = kNets[0], .candidates = hot_a_}};
  }
  [[nodiscard]] std::vector<CpuTargetedRegenerationSourcePool> AllSourcePools() const {
    return {
        CpuTargetedRegenerationSourcePool{
            .net = kNets[0], .candidates = std::span(source_candidates_).subspan(0, 1)},
        CpuTargetedRegenerationSourcePool{
            .net = kNets[1], .candidates = std::span(source_candidates_).subspan(1, 1)},
        CpuTargetedRegenerationSourcePool{.net = kNets[2], .candidates = {}},
        CpuTargetedRegenerationSourcePool{.net = kNets[3], .candidates = {}},
    };
  }
  [[nodiscard]] std::vector<CpuTargetedRegenerationSourcePool> NoOpSourcePools() const {
    return {CpuTargetedRegenerationSourcePool{
        .net = kNets[0], .candidates = std::span(source_candidates_).subspan(0, 1)}};
  }
  [[nodiscard]] std::span<const CpuTargetedRegenerationNetContext> contexts() const noexcept {
    return contexts_;
  }
  [[nodiscard]] std::span<const CpuTargetedRegenerationNetContext> NoOpContexts() const noexcept {
    return std::span(contexts_).first(1);
  }

 private:
  board_ir::BoardSnapshot board_;
  std::vector<CompiledBoard> compiled_;
  std::vector<candidates::StoredCandidate> source_candidates_;
  std::array<const RouteCandidate*, 1> hot_a_;
  std::array<const RouteCandidate*, 1> hot_b_;
  std::vector<CpuTargetedRegenerationNetContext> contexts_;
  std::optional<ResourceCapacityModel> capacities_;
};

[[nodiscard]] NegotiatedRegenerationPlan MakePlan(const Scenario& scenario,
                                                  std::span<const OneWorldCandidatePool> pools,
                                                  NegotiatedRegenerationPlanConfig config = {}) {
  NegotiatedRegenerationPlanResult result =
      PlanNegotiatedRegeneration(scenario.board(), scenario.capacities(), pools, nullptr, config);
  EXPECT_TRUE(std::holds_alternative<NegotiatedRegenerationPlan>(result))
      << (std::holds_alternative<NegotiatedRegenerationPlanError>(result)
              ? std::get<NegotiatedRegenerationPlanError>(result).detail
              : std::string_view{});
  if (!std::holds_alternative<NegotiatedRegenerationPlan>(result)) {
    std::abort();
  }
  return std::get<NegotiatedRegenerationPlan>(std::move(result));
}

[[nodiscard]] const CpuTargetedRegenerationEpoch& Success(
    const CpuTargetedRegenerationEpochResult& result) {
  EXPECT_TRUE(std::holds_alternative<CpuTargetedRegenerationEpoch>(result))
      << (std::holds_alternative<CpuTargetedRegenerationEpochError>(result)
              ? std::get<CpuTargetedRegenerationEpochError>(result).detail
              : std::string_view{})
      << " invariant="
      << (std::holds_alternative<CpuTargetedRegenerationEpochError>(result)
              ? std::get<CpuTargetedRegenerationEpochError>(result).invariant_id
              : std::string_view{})
      << " rejection="
      << (std::holds_alternative<CpuTargetedRegenerationEpochError>(result) &&
                  std::get<CpuTargetedRegenerationEpochError>(result)
                      .candidate_rejection_code.has_value()
              ? static_cast<int>(
                    *std::get<CpuTargetedRegenerationEpochError>(result).candidate_rejection_code)
              : -1)
      << " candidate_invariant="
      << (std::holds_alternative<CpuTargetedRegenerationEpochError>(result)
              ? std::get<CpuTargetedRegenerationEpochError>(result).candidate_rejection_invariant
              : std::string{});
  if (!std::holds_alternative<CpuTargetedRegenerationEpoch>(result)) {
    std::abort();
  }
  return std::get<CpuTargetedRegenerationEpoch>(result);
}

[[nodiscard]] CpuTargetedRegenerationEpoch Success(CpuTargetedRegenerationEpochResult&& result) {
  static_cast<void>(Success(result));
  return std::get<CpuTargetedRegenerationEpoch>(std::move(result));
}

[[nodiscard]] CpuTargetedRegenerationEpochError Failure(
    const CpuTargetedRegenerationEpochResult& result) {
  EXPECT_TRUE(std::holds_alternative<CpuTargetedRegenerationEpochError>(result));
  if (!std::holds_alternative<CpuTargetedRegenerationEpochError>(result)) {
    std::abort();
  }
  return std::get<CpuTargetedRegenerationEpochError>(result);
}

[[nodiscard]] std::vector<std::vector<CandidateId>> PoolIds(
    const CpuTargetedRegenerationEpoch& epoch) {
  std::vector<std::vector<CandidateId>> ids;
  ids.reserve(epoch.pools().size());
  for (const CpuTargetedRegenerationPool& pool : epoch.pools()) {
    std::vector<CandidateId>& pool_ids = ids.emplace_back();
    for (const candidates::StoredCandidate& candidate : pool.candidates()) {
      pool_ids.push_back(candidate->id());
    }
  }
  return ids;
}

[[nodiscard]] bool IndependentRank(const RouteCandidate& left,
                                   const RouteCandidate& right) noexcept {
  const candidates::CandidateMetrics& lhs = left.data().metrics;
  const candidates::CandidateMetrics& rhs = right.data().metrics;
  const UWide left_steps = static_cast<UWide>(lhs.orthogonal_step_count) + lhs.diagonal_step_count;
  const UWide right_steps = static_cast<UWide>(rhs.orthogonal_step_count) + rhs.diagonal_step_count;
  return std::tuple{lhs.intrinsic_base_cost,
                    lhs.via_count,
                    lhs.bend_count,
                    left_steps,
                    lhs.axis_aligned_length_dbu,
                    lhs.diagonal_projection_dbu,
                    left.data().resource_signature,
                    left.data().geometry_signature,
                    left.data().policy_identity,
                    lhs.scalar_policy_cost,
                    left.id()} < std::tuple{rhs.intrinsic_base_cost,
                                            rhs.via_count,
                                            rhs.bend_count,
                                            right_steps,
                                            rhs.axis_aligned_length_dbu,
                                            rhs.diagonal_projection_dbu,
                                            right.data().resource_signature,
                                            right.data().geometry_signature,
                                            right.data().policy_identity,
                                            rhs.scalar_policy_cost,
                                            right.id()};
}

[[nodiscard]] bool IndependentDuplicate(const RouteCandidate& left, const RouteCandidate& right) {
  if (left.net() != right.net()) {
    return false;
  }
  return left.id() == right.id() || left.data().geometry == right.data().geometry ||
         left.data().resources == right.data().resources;
}

struct OracleOutcome {
  std::vector<std::vector<RouteCandidate>> pools;
  OneWorldSelection selection;
};

[[nodiscard]] OracleOutcome IndependentExactSmallOracle(
    const Scenario& scenario, std::span<const OneWorldCandidatePool> source_pools,
    const NegotiatedRegenerationPlan& plan,
    std::span<const CpuTargetedRegenerationNetContext> contexts) {
  std::map<EntityRef, const CpuTargetedRegenerationNetContext*,
           decltype([](EntityRef left, EntityRef right) {
             return std::pair{left.id, left.generation} < std::pair{right.id, right.generation};
           })>
      context_by_net;
  for (const CpuTargetedRegenerationNetContext& context : contexts) {
    context_by_net.emplace(context.request.net, &context);
  }
  std::map<EntityRef, std::vector<RouteCandidate>, decltype([](EntityRef left, EntityRef right) {
             return std::pair{left.id, left.generation} < std::pair{right.id, right.generation};
           })>
      candidates_by_net;
  for (const OneWorldCandidatePool& pool : source_pools) {
    for (const RouteCandidate* candidate : pool.candidates) {
      candidates_by_net[pool.net].push_back(*candidate);
    }
    candidates_by_net.try_emplace(pool.net);
  }

  std::uint64_t query_identity = 0x900;
  for (const NegotiatedRegenerationTarget& target : plan.targets) {
    const CpuTargetedRegenerationNetContext& context = *context_by_net.at(target.net);
    routing::CpuRouteRequest request = context.request;
    for (const NegotiatedResourcePrice& price : plan.price_snapshot.prices) {
      if (routing::ResourceExists(*context.compiled_board, price.resource)) {
        request.candidate_policy.resource_penalties.push_back(routing::ResourcePenalty{
            .resource = price.resource, .additional_cost = price.total_price});
      }
    }
    routing::CandidatePolicyResult policy_result = routing::NormalizeCandidateGenerationPolicy(
        *context.compiled_board, request.candidate_policy);
    EXPECT_TRUE(
        std::holds_alternative<routing::NormalizedCandidateGenerationPolicy>(policy_result));
    if (!std::holds_alternative<routing::NormalizedCandidateGenerationPolicy>(policy_result)) {
      std::abort();
    }
    routing::NormalizedCandidateGenerationPolicy policy =
        std::get<routing::NormalizedCandidateGenerationPolicy>(std::move(policy_result));
    request.candidate_policy = policy.policy;
    routing::CpuRouteResult route_result =
        routing::RouteWithCpuAStar(scenario.board(), *context.compiled_board, request,
                                   routing::CpuRouteWorkLimits{.maximum_work_units = 100'000'000});
    if (!std::holds_alternative<routing::CpuRoute>(route_result)) {
      continue;
    }
    candidates::CandidateDraftBuildResult draft = candidates::BuildGeneratedCandidateFromCpuRoute(
        scenario.board(), *context.compiled_board, request, policy,
        std::get<routing::CpuRoute>(route_result),
        candidates::CandidateSchedulingIdentity{.batch_identity = 0x901,
                                                .query_identity = ++query_identity});
    EXPECT_TRUE(std::holds_alternative<candidates::GeneratedRouteCandidate>(draft));
    if (!std::holds_alternative<candidates::GeneratedRouteCandidate>(draft)) {
      std::abort();
    }
    candidates::CandidateAdmissionResult admission = candidates::AdmitRouteCandidate(
        candidates::CandidateAdmissionContext{
            .board = scenario.board(),
            .compiled_board = *context.compiled_board,
            .request = request,
        },
        std::get<candidates::GeneratedRouteCandidate>(std::move(draft)));
    EXPECT_TRUE(std::holds_alternative<RouteCandidate>(admission));
    if (!std::holds_alternative<RouteCandidate>(admission)) {
      std::abort();
    }
    candidates_by_net[target.net].push_back(std::get<RouteCandidate>(std::move(admission)));
  }

  OracleOutcome oracle;
  std::vector<const RouteCandidate*> selected;
  for (auto& [net, submitted] : candidates_by_net) {
    std::ranges::sort(submitted, IndependentRank);
    std::vector<RouteCandidate> retained;
    for (const RouteCandidate& candidate : submitted) {
      if (std::ranges::none_of(retained, [&candidate](const RouteCandidate& current) {
            return IndependentDuplicate(current, candidate);
          })) {
        retained.push_back(candidate);
      }
    }
    if (!retained.empty()) {
      selected.push_back(&retained.front());
    }
    oracle.pools.push_back(std::move(retained));
    static_cast<void>(net);
  }

  oracle.selection.associations = scenario.capacities().associations();
  oracle.selection.input_candidate_count = 0;
  oracle.selection.nets.reserve(oracle.pools.size());
  std::map<EdgeResourceKey, std::uint64_t> usage;
  for (std::size_t index = 0; index < oracle.pools.size(); ++index) {
    const EntityRef net = kNets[index];
    oracle.selection.input_candidate_count += oracle.pools[index].size();
    if (oracle.pools[index].empty()) {
      oracle.selection.nets.emplace_back(OneWorldCandidateAbsence{
          .net = net, .reason = OneWorldCandidateAbsenceReason::kEmptyPool});
      continue;
    }
    const RouteCandidate& candidate = oracle.pools[index].front();
    oracle.selection.nets.emplace_back(
        OneWorldSelectedCandidate{.net = net, .candidate_id = candidate.id()});
    for (const candidates::PhysicalEdgeSpan& span : candidate.data().resources) {
      const geometry_compiler::DirectionDelta delta =
          candidates::ResourceSpanStorageDelta(span.direction);
      for (std::uint32_t offset = 0; offset < span.edge_count; ++offset) {
        const EdgeResourceKey resource{
            .layer = span.layer,
            .lattice_x = span.lattice_x + static_cast<std::int64_t>(delta.x) * offset,
            .lattice_y = span.lattice_y + static_cast<std::int64_t>(delta.y) * offset,
            .direction = span.direction,
        };
        usage[resource] += span.usage_units;
        ++oracle.selection.accounting.expanded_resource_uses;
      }
    }
    ++oracle.selection.accounting.candidate_count;
  }
  oracle.selection.accounting.associations = scenario.capacities().associations();
  for (const auto& [resource, units] : usage) {
    const std::uint64_t overuse = units > scenario.capacities().capacity_units()
                                      ? units - scenario.capacities().capacity_units()
                                      : 0;
    oracle.selection.accounting.resources.push_back(ResourceUsage{
        .resource = resource,
        .capacity_units = scenario.capacities().capacity_units(),
        .usage_units = units,
        .overuse_units = overuse,
    });
    if (overuse != 0) {
      ++oracle.selection.accounting.overused_resource_count;
      oracle.selection.accounting.total_overuse_units += overuse;
    }
  }
  return oracle;
}

TEST(CpuTargetedRegenerationEpochTest,
     AuthenticEpochMatchesIndependentPublicationSelectionAndAccountingOracle) {
  Scenario scenario;
  std::vector<OneWorldCandidatePool> pools = scenario.AllPools();
  std::vector<CpuTargetedRegenerationSourcePool> source_pools = scenario.AllSourcePools();
  NegotiatedRegenerationPlan plan = MakePlan(scenario, pools);

  CpuTargetedRegenerationEpochResult result = ExecuteCpuTargetedRegenerationEpoch(
      scenario.board(), scenario.capacities(), source_pools, plan, nullptr, scenario.contexts());
  const CpuTargetedRegenerationEpoch& epoch = Success(result);
  OracleOutcome oracle = IndependentExactSmallOracle(scenario, pools, plan, scenario.contexts());

  ASSERT_EQ(epoch.pools().size(), oracle.pools.size());
  for (std::size_t index = 0; index < oracle.pools.size(); ++index) {
    using PublishedValue = std::tuple<candidates::CandidateSignature,
                                      candidates::CandidateSignature, candidates::CandidateMetrics>;
    std::vector<PublishedValue> expected;
    for (const RouteCandidate& candidate : oracle.pools[index]) {
      expected.emplace_back(candidate.data().geometry_signature,
                            candidate.data().resource_signature, candidate.data().metrics);
    }
    std::vector<PublishedValue> actual;
    for (const candidates::StoredCandidate& candidate : epoch.pools()[index].candidates()) {
      actual.emplace_back(candidate->data().geometry_signature,
                          candidate->data().resource_signature, candidate->data().metrics);
    }
    EXPECT_EQ(actual, expected);
  }
  EXPECT_EQ(epoch.refreshed_selection().input_candidate_count,
            oracle.selection.input_candidate_count);
  EXPECT_EQ(epoch.refreshed_selection().accounting, oracle.selection.accounting);
  ASSERT_EQ(epoch.refreshed_selection().nets.size(), oracle.selection.nets.size());
  for (std::size_t index = 0; index < oracle.selection.nets.size(); ++index) {
    const OneWorldNetOutcome& actual = epoch.refreshed_selection().nets[index];
    const OneWorldNetOutcome& expected = oracle.selection.nets[index];
    EXPECT_EQ(actual.index(), expected.index());
    if (std::holds_alternative<OneWorldCandidateAbsence>(expected)) {
      continue;
    }
    const CandidateId actual_id = std::get<OneWorldSelectedCandidate>(actual).candidate_id;
    const CandidateId expected_id = std::get<OneWorldSelectedCandidate>(expected).candidate_id;
    const auto actual_candidate = std::ranges::find_if(
        epoch.pools()[index].candidates(),
        [actual_id](const auto& candidate) { return candidate->id() == actual_id; });
    const auto expected_candidate = std::ranges::find_if(
        oracle.pools[index],
        [expected_id](const RouteCandidate& candidate) { return candidate.id() == expected_id; });
    ASSERT_NE(actual_candidate, epoch.pools()[index].candidates().end());
    ASSERT_NE(expected_candidate, oracle.pools[index].end());
    EXPECT_EQ((*actual_candidate)->data().geometry_signature,
              expected_candidate->data().geometry_signature);
    EXPECT_EQ((*actual_candidate)->data().resource_signature,
              expected_candidate->data().resource_signature);
  }
  EXPECT_EQ(epoch.columns().size(), plan.targets.size());
  EXPECT_NE(epoch.batch_identity(), 0U);
  EXPECT_NE(epoch.epoch_identity(), 0U);
  EXPECT_EQ(epoch.counters().route_query_count, plan.targets.size());
  EXPECT_EQ(epoch.counters().retained_candidate_count,
            epoch.refreshed_selection().input_candidate_count);
  EXPECT_EQ(epoch.counters().admitted_columns + epoch.counters().duplicate_columns +
                epoch.counters().rejected_columns,
            epoch.counters().target_count);
}

void ReplaceWithOrdinaryBuildRejection(candidates::CandidateDraftBuildResult& draft) {
  if (!std::holds_alternative<candidates::GeneratedRouteCandidate>(draft)) {
    return;
  }
  const candidates::GeneratedRouteCandidate& generated =
      std::get<candidates::GeneratedRouteCandidate>(draft);
  draft = candidates::CanonicalizeCandidateRejectionV1(candidates::CandidateRejection{
      .candidate_id = generated.id,
      .net = generated.net,
      .stage = candidates::CandidateLifecycleStage::kExactValidated,
      .code = candidates::CandidateRejectionCode::kExactValidation,
      .invariant_id = "candidate.exact_validation.injected_reference_case.v1",
      .associations = generated.associations,
      .policy_identity = generated.policy_identity,
      .provenance = generated.provenance,
      .primitive_witness_index = std::nullopt,
      .resource_witness_index = std::nullopt,
      .expected_value = std::nullopt,
      .actual_value = std::nullopt,
      .conflicting_entity = std::nullopt,
      .candidate_payload_checksum = generated.payload_checksum,
      .detail = "Source-private test hook models an ordinary exact-validation rejection",
      .logical_bytes = generated.logical_bytes,
  });
}

void InjectSecondOrdinaryBuildRejection(std::size_t index,
                                        candidates::CandidateDraftBuildResult& draft, void*) {
  if (index == 1) {
    ReplaceWithOrdinaryBuildRejection(draft);
  }
}

void InjectFirstOrdinaryBuildRejection(std::size_t index,
                                       candidates::CandidateDraftBuildResult& draft, void*) {
  if (index == 0) {
    ReplaceWithOrdinaryBuildRejection(draft);
  }
}

TEST(CpuTargetedRegenerationEpochTest, EmptyHotAndNoOpPlansHaveExactBoundedColumnBehavior) {
  Scenario scenario;
  std::vector<OneWorldCandidatePool> pools = scenario.AllPools();
  std::vector<CpuTargetedRegenerationSourcePool> source_pools = scenario.AllSourcePools();
  NegotiatedRegenerationPlan plan = MakePlan(scenario, pools);
  ASSERT_EQ(plan.targets.size(), 4U);
  EXPECT_EQ(plan.targets[0].reason, NegotiatedRegenerationTargetReason::kEmptyPool);
  EXPECT_EQ(plan.targets[1].reason, NegotiatedRegenerationTargetReason::kEmptyPool);
  EXPECT_EQ(plan.targets[2].reason, NegotiatedRegenerationTargetReason::kHotResource);
  EXPECT_EQ(plan.targets[3].reason, NegotiatedRegenerationTargetReason::kHotResource);

  CpuTargetedRegenerationEpochResult epoch_result = ExecuteCpuTargetedRegenerationEpoch(
      scenario.board(), scenario.capacities(), source_pools, plan, nullptr, scenario.contexts());
  const CpuTargetedRegenerationEpoch& epoch = Success(epoch_result);
  ASSERT_EQ(epoch.columns().size(), 4U);
  EXPECT_EQ(epoch.columns()[0].outcome, CpuTargetedRegenerationColumnOutcome::kAdmitted);
  EXPECT_EQ(epoch.columns()[1].outcome, CpuTargetedRegenerationColumnOutcome::kAdmitted);
  EXPECT_TRUE(std::ranges::any_of(epoch.columns(), [](const auto& column) {
    return column.outcome == CpuTargetedRegenerationColumnOutcome::kAdmitted;
  }));

  const CpuTargetedRegenerationEpoch& rejected =
      Success(internal::ExecuteCpuTargetedRegenerationEpochWithTestHooks(
          scenario.board(), scenario.capacities(), source_pools, plan, nullptr, scenario.contexts(),
          {},
          internal::CpuTargetedRegenerationEpochTestHooks{
              .after_candidate_draft = InjectSecondOrdinaryBuildRejection,
          }));
  EXPECT_EQ(rejected.columns()[1].outcome,
            CpuTargetedRegenerationColumnOutcome::kCandidateBuildRejected);
  ASSERT_TRUE(rejected.columns()[1].rejection.has_value());
  EXPECT_EQ(rejected.columns()[1].rejection->code,
            candidates::CandidateRejectionCode::kExactValidation);

  std::vector<OneWorldCandidatePool> no_op_pools = scenario.NoOpPools();
  std::vector<CpuTargetedRegenerationSourcePool> no_op_source_pools = scenario.NoOpSourcePools();
  NegotiatedRegenerationPlan no_op_plan = MakePlan(scenario, no_op_pools);
  ASSERT_TRUE(no_op_plan.targets.empty());
  CpuTargetedRegenerationEpochResult no_op_result = ExecuteCpuTargetedRegenerationEpoch(
      scenario.board(), scenario.capacities(), no_op_source_pools, no_op_plan, nullptr,
      scenario.NoOpContexts());
  const CpuTargetedRegenerationEpoch& no_op = Success(no_op_result);
  EXPECT_TRUE(no_op.columns().empty());
  EXPECT_EQ(PoolIds(no_op),
            std::vector<std::vector<CandidateId>>{{scenario.source_candidates()[0]->id()}});
  ASSERT_EQ(no_op.refreshed_selection().nets.size(), 1U);
  EXPECT_TRUE(
      std::holds_alternative<OneWorldSelectedCandidate>(no_op.refreshed_selection().nets.front()));
}

TEST(CpuTargetedRegenerationEpochTest,
     LowPricesProduceCanonicalDuplicateColumnsWithoutFabrication) {
  Scenario scenario(1);
  std::vector<OneWorldCandidatePool> pools = scenario.AllPools();
  std::vector<CpuTargetedRegenerationSourcePool> source_pools = scenario.AllSourcePools();
  NegotiatedRegenerationPlanConfig planning;
  planning.price_policy = NegotiatedPriceUpdatePolicyV1{
      .initial_present_factor = 0,
      .present_factor_increment = 1,
      .historical_price_increment = 1,
  };
  NegotiatedRegenerationPlan plan = MakePlan(scenario, pools, planning);
  CpuTargetedRegenerationEpochConfig config;
  config.planning = planning;

  CpuTargetedRegenerationEpochResult epoch_result =
      ExecuteCpuTargetedRegenerationEpoch(scenario.board(), scenario.capacities(), source_pools,
                                          plan, nullptr, scenario.contexts(), config);
  const CpuTargetedRegenerationEpoch& epoch = Success(epoch_result);
  EXPECT_TRUE(std::ranges::any_of(epoch.columns(), [](const auto& column) {
    return column.outcome == CpuTargetedRegenerationColumnOutcome::kDuplicate &&
           column.rejection.has_value() &&
           (column.rejection->code == candidates::CandidateRejectionCode::kDuplicateGeometry ||
            column.rejection->code == candidates::CandidateRejectionCode::kDuplicateResources);
  }));
  EXPECT_GT(epoch.counters().duplicate_columns, 0U);
}

TEST(CpuTargetedRegenerationEpochTest, CallerPermutationAndRepeatedRunsPreserveEveryStableOutcome) {
  Scenario scenario;
  std::vector<OneWorldCandidatePool> pools = scenario.AllPools();
  std::vector<CpuTargetedRegenerationSourcePool> source_pools = scenario.AllSourcePools();
  NegotiatedRegenerationPlan plan = MakePlan(scenario, pools);
  CpuTargetedRegenerationEpochResult first_result = ExecuteCpuTargetedRegenerationEpoch(
      scenario.board(), scenario.capacities(), source_pools, plan, nullptr, scenario.contexts());
  const CpuTargetedRegenerationEpoch& first = Success(first_result);

  std::ranges::reverse(source_pools);
  std::vector<CpuTargetedRegenerationNetContext> contexts(scenario.contexts().begin(),
                                                          scenario.contexts().end());
  std::ranges::reverse(contexts);
  CpuTargetedRegenerationEpochResult second_result = ExecuteCpuTargetedRegenerationEpoch(
      scenario.board(), scenario.capacities(), source_pools, plan, nullptr, contexts);
  const CpuTargetedRegenerationEpoch& second = Success(second_result);
  CpuTargetedRegenerationEpochResult third_result = ExecuteCpuTargetedRegenerationEpoch(
      scenario.board(), scenario.capacities(), source_pools, plan, nullptr, contexts);
  const CpuTargetedRegenerationEpoch& third = Success(third_result);

  EXPECT_EQ(first.batch_identity(), second.batch_identity());
  EXPECT_EQ(first.epoch_identity(), second.epoch_identity());
  EXPECT_EQ(second.epoch_identity(), third.epoch_identity());
  EXPECT_EQ(
      std::vector<CpuTargetedRegenerationColumn>(first.columns().begin(), first.columns().end()),
      std::vector<CpuTargetedRegenerationColumn>(second.columns().begin(), second.columns().end()));
  EXPECT_EQ(
      std::vector<CpuTargetedRegenerationColumn>(second.columns().begin(), second.columns().end()),
      std::vector<CpuTargetedRegenerationColumn>(third.columns().begin(), third.columns().end()));
  EXPECT_EQ(PoolIds(first), PoolIds(second));
  EXPECT_EQ(PoolIds(second), PoolIds(third));
  EXPECT_EQ(first.refreshed_selection(), second.refreshed_selection());
  EXPECT_EQ(second.refreshed_selection(), third.refreshed_selection());
  EXPECT_EQ(first.counters(), second.counters());
}

TEST(CpuTargetedRegenerationEpochTest, ExactConfiguredBoundsPassAndOneUnderFailsBeforeExecution) {
  Scenario scenario;
  std::vector<OneWorldCandidatePool> pools = scenario.AllPools();
  std::vector<CpuTargetedRegenerationSourcePool> source_pools = scenario.AllSourcePools();
  NegotiatedRegenerationPlan plan = MakePlan(scenario, pools);
  CpuTargetedRegenerationEpochResult baseline_result = ExecuteCpuTargetedRegenerationEpoch(
      scenario.board(), scenario.capacities(), source_pools, plan, nullptr, scenario.contexts());
  const CpuTargetedRegenerationEpoch& baseline = Success(baseline_result);

  CpuTargetedRegenerationEpochConfig exact;
  exact.limits.maximum_targets = plan.targets.size();
  exact.limits.maximum_price_projection_visits =
      plan.targets.size() * plan.price_snapshot.prices.size();
  exact.limits.maximum_price_entries_per_target = plan.price_snapshot.prices.size();
  exact.limits.maximum_aggregate_price_entries = baseline.counters().projected_price_entries;
  exact.limits.maximum_aggregate_cpu_work_units =
      exact.limits.maximum_cpu_work_units_per_query * plan.targets.size();
  EXPECT_TRUE(std::holds_alternative<CpuTargetedRegenerationEpoch>(
      ExecuteCpuTargetedRegenerationEpoch(scenario.board(), scenario.capacities(), source_pools,
                                          plan, nullptr, scenario.contexts(), exact)));

  CpuTargetedRegenerationEpochConfig one_under = exact;
  --one_under.limits.maximum_aggregate_cpu_work_units;
  const CpuTargetedRegenerationEpochError& error = Failure(
      ExecuteCpuTargetedRegenerationEpoch(scenario.board(), scenario.capacities(), source_pools,
                                          plan, nullptr, scenario.contexts(), one_under));
  EXPECT_EQ(error.code, CpuTargetedRegenerationEpochErrorCode::kBoundExhausted);
  EXPECT_EQ(error.invariant_id, "allocator.targeted_epoch.aggregate_work_bound.v1");
}

TEST(CpuTargetedRegenerationEpochTest,
     AssociationPlanAndArithmeticFailuresAreTypedAndPrecedeQueries) {
  Scenario scenario;
  std::vector<OneWorldCandidatePool> pools = scenario.AllPools();
  std::vector<CpuTargetedRegenerationSourcePool> source_pools = scenario.AllSourcePools();
  NegotiatedRegenerationPlan plan = MakePlan(scenario, pools);

  NegotiatedRegenerationPlan corrupted = plan;
  ++corrupted.plan_identity;
  const CpuTargetedRegenerationEpochError& plan_error = Failure(
      ExecuteCpuTargetedRegenerationEpoch(scenario.board(), scenario.capacities(), source_pools,
                                          corrupted, nullptr, scenario.contexts()));
  EXPECT_EQ(plan_error.code, CpuTargetedRegenerationEpochErrorCode::kPlanMismatch);

  std::vector<CpuTargetedRegenerationNetContext> contexts(scenario.contexts().begin(),
                                                          scenario.contexts().end());
  board_ir::BoardSnapshot revised = test_support::Snapshot(EpochBoardData(2));
  CompiledBoard wrong = test_support::CompilePreparedNet(revised, kNets[0], EpochProfile());
  contexts[0].compiled_board = &wrong;
  const CpuTargetedRegenerationEpochError& association_error =
      Failure(ExecuteCpuTargetedRegenerationEpoch(scenario.board(), scenario.capacities(),
                                                  source_pools, plan, nullptr, contexts));
  EXPECT_EQ(association_error.code, CpuTargetedRegenerationEpochErrorCode::kAssociationMismatch);

  CpuTargetedRegenerationEpochConfig overflow;
  overflow.limits.maximum_cpu_work_units_per_query = std::numeric_limits<std::uint64_t>::max();
  overflow.limits.maximum_aggregate_cpu_work_units = std::numeric_limits<std::uint64_t>::max();
  const CpuTargetedRegenerationEpochError& arithmetic_error = Failure(
      ExecuteCpuTargetedRegenerationEpoch(scenario.board(), scenario.capacities(), source_pools,
                                          plan, nullptr, scenario.contexts(), overflow));
  EXPECT_EQ(arithmetic_error.code, CpuTargetedRegenerationEpochErrorCode::kArithmeticOverflow);
  EXPECT_EQ(arithmetic_error.invariant_id, "allocator.targeted_epoch.aggregate_work_overflow.v1");
}

TEST(CpuTargetedRegenerationEpochTest, CallerAuthoredResourcePolicyIsRejectedBeforeExecution) {
  Scenario scenario;
  std::vector<OneWorldCandidatePool> pools = scenario.AllPools();
  std::vector<CpuTargetedRegenerationSourcePool> source_pools = scenario.AllSourcePools();
  NegotiatedRegenerationPlan plan = MakePlan(scenario, pools);
  ASSERT_FALSE(plan.price_snapshot.prices.empty());

  std::vector<CpuTargetedRegenerationNetContext> contexts(scenario.contexts().begin(),
                                                          scenario.contexts().end());
  contexts.front().request.candidate_policy.banned_resources.push_back(
      plan.price_snapshot.prices.front().resource);
  const CpuTargetedRegenerationEpochError& error = Failure(ExecuteCpuTargetedRegenerationEpoch(
      scenario.board(), scenario.capacities(), source_pools, plan, nullptr, contexts));
  EXPECT_EQ(error.code, CpuTargetedRegenerationEpochErrorCode::kInvalidInput);
  EXPECT_EQ(error.invariant_id, "allocator.targeted_epoch.caller_resource_policy.v1");
}

void CorruptDraftAssociation(std::size_t index, candidates::CandidateDraftBuildResult& draft,
                             void*) {
  if (index == 1 && std::holds_alternative<candidates::GeneratedRouteCandidate>(draft)) {
    ++std::get<candidates::GeneratedRouteCandidate>(draft).associations.board_content_hash;
  }
}

TEST(CpuTargetedRegenerationEpochTest,
     GeneratedAssociationCorruptionIsFatalRatherThanAColumnDiagnostic) {
  Scenario scenario;
  std::vector<OneWorldCandidatePool> pools = scenario.AllPools();
  std::vector<CpuTargetedRegenerationSourcePool> source_pools = scenario.AllSourcePools();
  NegotiatedRegenerationPlan plan = MakePlan(scenario, pools);
  const CpuTargetedRegenerationEpochError& error =
      Failure(internal::ExecuteCpuTargetedRegenerationEpochWithTestHooks(
          scenario.board(), scenario.capacities(), source_pools, plan, nullptr, scenario.contexts(),
          {},
          internal::CpuTargetedRegenerationEpochTestHooks{
              .after_candidate_draft = CorruptDraftAssociation,
          }));
  EXPECT_EQ(error.code, CpuTargetedRegenerationEpochErrorCode::kAssociationMismatch);
}

struct TargetVisitState {
  std::vector<std::size_t> visited;
};

internal::CpuTargetedRegenerationInjectedFailure FailSecondTarget(std::size_t index, void* opaque) {
  auto& state = *static_cast<TargetVisitState*>(opaque);
  state.visited.push_back(index);
  return index == 1 ? internal::CpuTargetedRegenerationInjectedFailure::kInternalInvariant
                    : internal::CpuTargetedRegenerationInjectedFailure::kNone;
}

TEST(CpuTargetedRegenerationEpochTest,
     CanonicalDiagnosticPrecedenceReturnsNoPartialPrefixAfterLaterFatalError) {
  Scenario scenario;
  std::vector<OneWorldCandidatePool> pools = scenario.AllPools();
  std::vector<CpuTargetedRegenerationSourcePool> source_pools = scenario.AllSourcePools();
  NegotiatedRegenerationPlan plan = MakePlan(scenario, pools);
  ASSERT_EQ(plan.targets[0].net, kNets[2]);
  TargetVisitState state;
  CpuTargetedRegenerationEpochResult result =
      internal::ExecuteCpuTargetedRegenerationEpochWithTestHooks(
          scenario.board(), scenario.capacities(), source_pools, plan, nullptr, scenario.contexts(),
          {},
          internal::CpuTargetedRegenerationEpochTestHooks{
              .before_target_route = FailSecondTarget,
              .after_candidate_draft = InjectFirstOrdinaryBuildRejection,
              .context = &state,
          });
  const CpuTargetedRegenerationEpochError& error = Failure(result);
  EXPECT_EQ(error.code, CpuTargetedRegenerationEpochErrorCode::kInternalInvariant);
  EXPECT_EQ(state.visited, (std::vector<std::size_t>{0, 1}));
}

internal::CpuTargetedRegenerationInjectedFailure FailAfterPublication(void* opaque) {
  *static_cast<bool*>(opaque) = true;
  return internal::CpuTargetedRegenerationInjectedFailure::kResourceExhausted;
}

TEST(CpuTargetedRegenerationEpochTest,
     PostPublicationFatalFailureReturnsNoResultAndLeavesEveryInputImmutable) {
  Scenario scenario;
  std::vector<OneWorldCandidatePool> pools = scenario.AllPools();
  std::vector<CpuTargetedRegenerationSourcePool> source_pools = scenario.AllSourcePools();
  NegotiatedRegenerationPlan plan = MakePlan(scenario, pools);
  const NegotiatedRegenerationPlan plan_before = plan;
  std::vector<candidates::GeneratedRouteCandidate> candidates_before;
  for (const candidates::StoredCandidate& candidate : scenario.source_candidates()) {
    candidates_before.push_back(candidate->data());
  }
  std::vector<routing::PlanarRouteRequest> requests_before;
  for (const CpuTargetedRegenerationNetContext& context : scenario.contexts()) {
    requests_before.push_back(context.request);
  }
  bool publication_reached = false;

  CpuTargetedRegenerationEpochResult result =
      internal::ExecuteCpuTargetedRegenerationEpochWithTestHooks(
          scenario.board(), scenario.capacities(), source_pools, plan, nullptr, scenario.contexts(),
          {},
          internal::CpuTargetedRegenerationEpochTestHooks{
              .after_local_publication = FailAfterPublication,
              .context = &publication_reached,
          });
  const CpuTargetedRegenerationEpochError& error = Failure(result);
  EXPECT_TRUE(publication_reached);
  EXPECT_EQ(error.code, CpuTargetedRegenerationEpochErrorCode::kResourceExhausted);
  EXPECT_EQ(plan, plan_before);
  for (std::size_t index = 0; index < candidates_before.size(); ++index) {
    EXPECT_EQ(scenario.source_candidates()[index]->data(), candidates_before[index]);
  }
  for (std::size_t index = 0; index < requests_before.size(); ++index) {
    EXPECT_EQ(scenario.contexts()[index].request, requests_before[index]);
  }
}

}  // namespace
}  // namespace apgar::allocator
