#include "apgar/allocator/fixed_pool_cpu_multi_world.h"

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
#include <string>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

#include "apgar/board_ir/board.h"
#include "apgar/board_ir/stable_hash.h"
#include "apgar/candidates/route_candidate.h"
#include "apgar/geometry_compiler/compiled_board.h"
#include "apgar/routing/candidate_policy.h"
#include "apgar/routing/planar_route.h"
#include "src/allocator/fixed_pool_cpu_multi_world_internal.h"
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
inline constexpr std::array<EntityRef, 3> kNets = {
    EntityRef{.id = 10, .generation = 0},
    EntityRef{.id = 11, .generation = 0},
    EntityRef{.id = 12, .generation = 0},
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
      .component = "P4R09",
      .pin = std::move(pin),
      .center = center,
      .connection_region = TerminalBox(center),
      .layers = {kLayer},
  };
}

[[nodiscard]] BoardData MultiWorldBoardData(std::uint64_t revision = 1) {
  const std::array<EntityRef, 6> terminals = {
      EntityRef{.id = 20, .generation = 0}, EntityRef{.id = 21, .generation = 0},
      EntityRef{.id = 22, .generation = 0}, EntityRef{.id = 23, .generation = 0},
      EntityRef{.id = 24, .generation = 0}, EntityRef{.id = 25, .generation = 0},
  };
  return BoardData{
      .schema_version = board_ir::kBoardSchemaVersion,
      .dbu_per_millimeter = 1'000'000,
      .revision = revision,
      .adapter_name = "p4r09-exact-small",
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
              Net{.ref = kNets[0],
                  .name = "ALTERNATIVES",
                  .terminals = {terminals[0], terminals[1]}},
              Net{.ref = kNets[1], .name = "SHARED", .terminals = {terminals[2], terminals[3]}},
              Net{.ref = kNets[2], .name = "DISJOINT", .terminals = {terminals[4], terminals[5]}},
          },
      .terminals =
          {
              MakeTerminal(20, kNets[0], Point64{.x = 0, .y = 0}, "A1"),
              MakeTerminal(21, kNets[0], Point64{.x = 100, .y = 0}, "A2"),
              MakeTerminal(22, kNets[1], Point64{.x = 30, .y = -30}, "B1"),
              MakeTerminal(23, kNets[1], Point64{.x = 70, .y = 30}, "B2"),
              MakeTerminal(24, kNets[2], Point64{.x = 0, .y = 60}, "C1"),
              MakeTerminal(25, kNets[2], Point64{.x = 100, .y = 60}, "C2"),
          },
      .obstacles = {},
      .routing_profile =
          RoutingProfile{
              .net = kNets[0],
              .nominal_width = 4,
              .clearance = 1,
              .allowed_layers = {kLayer, 31},
              .allowed_headings = board_ir::kM1HeadingMask,
          },
  };
}

[[nodiscard]] CompilerProfile MultiWorldProfile() {
  return CompilerProfile{
      .schema_version = geometry_compiler::kCompilerProfileSchemaVersion,
      .lattice_origin = Point64{.x = 0, .y = 0},
      .lattice_step = 10,
      .tile_width_nodes = 4,
      .tile_height_nodes = 4,
      .compilation_roi =
          AxisAlignedBox64{.min = Point64{.x = 0, .y = -30}, .max = Point64{.x = 100, .y = 60}},
      .active_regions =
          {
              ActiveRegion{
                  .layer = kLayer,
                  .bounds = AxisAlignedBox64{.min = Point64{.x = 0, .y = -30},
                                             .max = Point64{.x = 100, .y = 60}},
              },
          },
      .heading_mask = board_ir::kM1HeadingMask,
      .costs = DeterministicCosts{.orthogonal_step = 10, .diagonal_step = 14, .bend = 3},
  };
}

[[nodiscard]] RouteCandidate AdmitExactRoute(const board_ir::BoardSnapshot& board,
                                             const CompiledBoard& compiled,
                                             const routing::CpuRouteRequest& request,
                                             std::uint64_t query_identity, std::uint64_t total_cost,
                                             std::vector<routing::LayerSegment> segments) {
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
      .total_cost = total_cost,
      .lattice_path = {},
      .segments = std::move(segments),
      .telemetry = {},
      .producer_evidence = {},
  };
  test_support::CpuRouteFaultDecorator::Reseal(route, compiled);
  candidates::CandidateDraftBuildResult draft = candidates::BuildGeneratedCandidateFromCpuRoute(
      board, compiled, request, policy, route,
      candidates::CandidateSchedulingIdentity{.batch_identity = 0x4900,
                                              .query_identity = query_identity});
  EXPECT_TRUE(std::holds_alternative<candidates::GeneratedRouteCandidate>(draft));
  if (!std::holds_alternative<candidates::GeneratedRouteCandidate>(draft)) {
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

class Fixture {
 public:
  explicit Fixture(std::uint64_t revision = 1)
      : board_(test_support::Snapshot(MultiWorldBoardData(revision))) {
    compiled_.reserve(kNets.size());
    contexts_.reserve(kNets.size());
    for (EntityRef net : kNets) {
      compiled_.push_back(test_support::CompilePreparedNet(board_, net, MultiWorldProfile()));
    }
    for (std::size_t index = 0; index < kNets.size(); ++index) {
      contexts_.push_back(CpuTargetedRegenerationNetContext{
          .compiled_board = &compiled_[index],
          .request = test_support::RequestForNet(board_, kNets[index], kLayer, kLayer),
      });
    }
    const routing::CpuRouteRequest first = contexts_[0].request;
    const routing::CpuRouteRequest second = contexts_[1].request;
    const routing::CpuRouteRequest third = contexts_[2].request;
    candidates_.push_back(std::make_shared<const RouteCandidate>(
        AdmitExactRoute(board_, compiled_[0], first, 1, 100,
                        {routing::LayerSegment{
                            .layer = kLayer,
                            .centerline = board_ir::Segment64{.start = Point64{.x = 0, .y = 0},
                                                              .end = Point64{.x = 100, .y = 0}},
                        }})));
    candidates_.push_back(std::make_shared<const RouteCandidate>(AdmitExactRoute(
        board_, compiled_[0], first, 2, 126,
        {
            routing::LayerSegment{
                .layer = kLayer,
                .centerline = board_ir::Segment64{.start = Point64{.x = 0, .y = 0},
                                                  .end = Point64{.x = 0, .y = 10}},
            },
            routing::LayerSegment{
                .layer = kLayer,
                .centerline = board_ir::Segment64{.start = Point64{.x = 0, .y = 10},
                                                  .end = Point64{.x = 100, .y = 10}},
            },
            routing::LayerSegment{
                .layer = kLayer,
                .centerline = board_ir::Segment64{.start = Point64{.x = 100, .y = 10},
                                                  .end = Point64{.x = 100, .y = 0}},
            },
        })));
    candidates_.push_back(std::make_shared<const RouteCandidate>(AdmitExactRoute(
        board_, compiled_[1], second, 3, 106,
        {
            routing::LayerSegment{
                .layer = kLayer,
                .centerline = board_ir::Segment64{.start = Point64{.x = 30, .y = -30},
                                                  .end = Point64{.x = 30, .y = 0}},
            },
            routing::LayerSegment{
                .layer = kLayer,
                .centerline = board_ir::Segment64{.start = Point64{.x = 30, .y = 0},
                                                  .end = Point64{.x = 70, .y = 0}},
            },
            routing::LayerSegment{
                .layer = kLayer,
                .centerline = board_ir::Segment64{.start = Point64{.x = 70, .y = 0},
                                                  .end = Point64{.x = 70, .y = 30}},
            },
        })));
    candidates_.push_back(std::make_shared<const RouteCandidate>(
        AdmitExactRoute(board_, compiled_[2], third, 4, 100,
                        {routing::LayerSegment{
                            .layer = kLayer,
                            .centerline = board_ir::Segment64{.start = Point64{.x = 0, .y = 60},
                                                              .end = Point64{.x = 100, .y = 60}},
                        }})));
    ResourceCapacityModelResult capacity = BuildResourceCapacityModel(board_, compiled_[0], 1);
    EXPECT_TRUE(std::holds_alternative<ResourceCapacityModel>(capacity));
    if (!std::holds_alternative<ResourceCapacityModel>(capacity)) {
      std::abort();
    }
    capacities_.emplace(std::get<ResourceCapacityModel>(std::move(capacity)));
  }

  [[nodiscard]] const board_ir::BoardSnapshot& board() const noexcept { return board_; }
  [[nodiscard]] const ResourceCapacityModel& capacities() const noexcept { return *capacities_; }

  struct Source {
    CpuCandidateAllocationSessionResult result;
    CpuCandidateAllocationSessionConfig config;
  };

  [[nodiscard]] Source MainSource(bool reverse_inputs = false) const {
    std::vector<CpuTargetedRegenerationSourcePool> pools = {
        CpuTargetedRegenerationSourcePool{.net = kNets[0],
                                          .candidates = std::span(candidates_).subspan(0, 2)},
        CpuTargetedRegenerationSourcePool{.net = kNets[1],
                                          .candidates = std::span(candidates_).subspan(2, 1)},
        CpuTargetedRegenerationSourcePool{.net = kNets[2],
                                          .candidates = std::span(candidates_).subspan(3, 1)},
    };
    std::vector<CpuTargetedRegenerationNetContext> contexts = contexts_;
    if (reverse_inputs) {
      std::ranges::reverse(pools);
      std::ranges::reverse(contexts);
    }
    CpuCandidateAllocationSessionConfig config;
    config.limits.maximum_reserved_cpu_work_units = 1;
    return Source{
        .result = RunCpuCandidateAllocationSession(board_, *capacities_, pools, contexts, config),
        .config = config,
    };
  }

  [[nodiscard]] Source FixedPointSource() const {
    const std::array pools = {
        CpuTargetedRegenerationSourcePool{
            .net = kNets[2],
            .candidates = std::span(candidates_).subspan(3, 1),
        },
    };
    const std::array contexts = {contexts_[2]};
    CpuCandidateAllocationSessionConfig config;
    return Source{
        .result = RunCpuCandidateAllocationSession(board_, *capacities_, pools, contexts, config),
        .config = config,
    };
  }

  [[nodiscard]] Source EmptyEpochBoundSource() const {
    const std::array pools = {
        CpuTargetedRegenerationSourcePool{.net = kNets[0], .candidates = {}},
    };
    const std::array contexts = {contexts_[0]};
    CpuCandidateAllocationSessionConfig config;
    config.epoch.limits.maximum_cpu_work_units_per_query = 1;
    config.epoch.limits.maximum_aggregate_cpu_work_units = 1;
    config.limits.maximum_reserved_cpu_work_units = 1;
    return Source{
        .result = RunCpuCandidateAllocationSession(board_, *capacities_, pools, contexts, config),
        .config = config,
    };
  }

 private:
  board_ir::BoardSnapshot board_;
  std::vector<CompiledBoard> compiled_;
  std::vector<candidates::StoredCandidate> candidates_;
  std::vector<CpuTargetedRegenerationNetContext> contexts_;
  std::optional<ResourceCapacityModel> capacities_;
};

[[nodiscard]] const CpuCandidateAllocationSession& RequireSource(const Fixture::Source& source) {
  EXPECT_TRUE(std::holds_alternative<CpuCandidateAllocationSession>(source.result))
      << (std::holds_alternative<CpuCandidateAllocationSessionError>(source.result)
              ? std::get<CpuCandidateAllocationSessionError>(source.result).detail
              : std::string_view{});
  if (!std::holds_alternative<CpuCandidateAllocationSession>(source.result)) {
    std::abort();
  }
  return std::get<CpuCandidateAllocationSession>(source.result);
}

[[nodiscard]] const FixedPoolCpuMultiWorldExecution& RequireExecution(
    const FixedPoolCpuMultiWorldResult& result) {
  EXPECT_TRUE(std::holds_alternative<FixedPoolCpuMultiWorldExecution>(result))
      << (std::holds_alternative<FixedPoolCpuMultiWorldError>(result)
              ? std::get<FixedPoolCpuMultiWorldError>(result).detail
              : std::string_view{});
  if (!std::holds_alternative<FixedPoolCpuMultiWorldExecution>(result)) {
    std::abort();
  }
  return std::get<FixedPoolCpuMultiWorldExecution>(result);
}

[[nodiscard]] const FixedPoolCpuMultiWorldError& RequireFailure(
    const FixedPoolCpuMultiWorldResult& result) {
  EXPECT_TRUE(std::holds_alternative<FixedPoolCpuMultiWorldError>(result));
  if (!std::holds_alternative<FixedPoolCpuMultiWorldError>(result)) {
    std::abort();
  }
  return std::get<FixedPoolCpuMultiWorldError>(result);
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
  ADD_FAILURE() << "Noncanonical candidate resource reached P4R-09 oracle";
  return DirectionDelta{};
}

[[nodiscard]] EdgeResourceKey OracleResource(const PhysicalEdgeSpan& span, std::uint32_t offset) {
  const DirectionDelta delta = OracleStorageDelta(span.direction);
  return EdgeResourceKey{
      .layer = span.layer,
      .lattice_x = span.lattice_x + static_cast<std::int64_t>(delta.x) * offset,
      .lattice_y = span.lattice_y + static_cast<std::int64_t>(delta.y) * offset,
      .direction = span.direction,
  };
}

void OracleHashResource(board_ir::StableHashBuilder* hash, const EdgeResourceKey& resource) {
  hash->AddU32(resource.layer);
  hash->AddI64(resource.lattice_x);
  hash->AddI64(resource.lattice_y);
  hash->AddU32(static_cast<std::uint32_t>(resource.direction));
}

void OracleHashCandidateId(board_ir::StableHashBuilder* hash, CandidateId id) {
  hash->AddU64(id.high);
  hash->AddU64(id.low);
}

[[nodiscard]] std::uint64_t OracleNonzeroHash(board_ir::StableHashBuilder* hash) {
  const std::uint64_t value = hash->Finish();
  return value == 0 ? 1 : value;
}

void OracleHashSelection(board_ir::StableHashBuilder* hash, const OneWorldSelection& selection) {
  hash->AddU64(selection.associations.board_content_hash);
  hash->AddU64(selection.associations.compiler_profile_fingerprint);
  hash->AddU32(selection.associations.geometry_compiler_version);
  hash->AddU64(selection.input_candidate_count);
  hash->AddU64(selection.nets.size());
  for (const OneWorldNetOutcome& outcome : selection.nets) {
    if (const auto* selected = std::get_if<OneWorldSelectedCandidate>(&outcome);
        selected != nullptr) {
      hash->AddBool(true);
      hash->AddU64(selected->net.id);
      hash->AddU32(selected->net.generation);
      OracleHashCandidateId(hash, selected->candidate_id);
    } else {
      const OneWorldCandidateAbsence& absence = std::get<OneWorldCandidateAbsence>(outcome);
      hash->AddBool(false);
      hash->AddU64(absence.net.id);
      hash->AddU32(absence.net.generation);
      hash->AddU32(static_cast<std::uint32_t>(absence.reason));
    }
  }
  hash->AddU64(selection.accounting.associations.board_content_hash);
  hash->AddU64(selection.accounting.associations.compiler_profile_fingerprint);
  hash->AddU32(selection.accounting.associations.geometry_compiler_version);
  hash->AddU64(selection.accounting.candidate_count);
  hash->AddU64(selection.accounting.expanded_resource_uses);
  hash->AddU64(selection.accounting.overused_resource_count);
  hash->AddU64(selection.accounting.total_overuse_units);
  hash->AddU64(selection.accounting.resources.size());
  for (const ResourceUsage& usage : selection.accounting.resources) {
    OracleHashResource(hash, usage.resource);
    hash->AddU32(usage.capacity_units);
    hash->AddU64(usage.usage_units);
    hash->AddU64(usage.overuse_units);
  }
}

void OracleHashSchedule(board_ir::StableHashBuilder* hash,
                        const FixedPoolCpuWorldSchedule& schedule) {
  hash->AddU64(schedule.schedule_key);
  hash->AddU64(schedule.price_policy.initial_present_factor);
  hash->AddU64(schedule.price_policy.present_factor_increment);
  hash->AddU64(schedule.price_policy.historical_price_increment);
  hash->AddU32(schedule.maximum_selection_rounds);
}

void OracleHashConfig(board_ir::StableHashBuilder* hash,
                      const FixedPoolCpuMultiWorldConfig& config) {
  const FixedPoolCpuMultiWorldLimits& limits = config.limits;
  hash->AddU64(limits.selection.maximum_net_pools);
  hash->AddU64(limits.selection.maximum_total_candidates);
  hash->AddU64(limits.selection.accounting.maximum_candidates);
  hash->AddU64(limits.selection.accounting.maximum_expanded_resource_uses);
  hash->AddU64(limits.selection.accounting.maximum_usage_units_per_resource);
  hash->AddU64(limits.maximum_worlds);
  hash->AddU64(limits.maximum_total_selection_rounds);
  hash->AddU64(limits.maximum_total_price_updates);
  hash->AddU64(limits.maximum_candidate_evaluations);
  hash->AddU64(limits.maximum_candidate_resource_visits);
  hash->AddU64(limits.maximum_selected_resource_uses);
  hash->AddU64(limits.maximum_net_outcomes);
  hash->AddU64(limits.maximum_emitted_price_entries);
  hash->AddU64(limits.maximum_trace_records);
  hash->AddU64(limits.maximum_buffered_terminal_selection_records);
  hash->AddU64(limits.maximum_buffered_terminal_resource_records);
  hash->AddU64(limits.maximum_buffered_terminal_price_records);
  hash->AddU64(limits.maximum_pareto_comparisons);
  hash->AddU64(limits.maximum_retained_worlds);
  hash->AddU64(limits.maximum_retained_selection_records);
  hash->AddU64(limits.maximum_retained_resource_records);
  hash->AddU64(limits.maximum_retained_price_records);
  hash->AddU64(limits.maximum_price_entries_per_world);
  hash->AddU64(limits.maximum_price_value);
  hash->AddU64(limits.maximum_aggregate_price_per_world);
  hash->AddU64(limits.maximum_candidate_score);
  hash->AddU64(config.maximum_near_feasible_missing_nets);
  hash->AddU64(config.maximum_near_feasible_overuse_units);
}

void OracleHashCounters(board_ir::StableHashBuilder* hash,
                        const FixedPoolCpuMultiWorldCounters& counters) {
  hash->AddU64(counters.scheduled_worlds);
  hash->AddU64(counters.completed_worlds);
  hash->AddU64(counters.selection_rounds);
  hash->AddU64(counters.price_updates);
  hash->AddU64(counters.candidate_evaluations);
  hash->AddU64(counters.candidate_resource_visits);
  hash->AddU64(counters.selected_resource_uses);
  hash->AddU64(counters.net_outcomes);
  hash->AddU64(counters.emitted_price_entries);
  hash->AddU64(counters.trace_records);
  hash->AddU64(counters.pareto_comparisons);
  hash->AddU64(counters.pareto_eligible_worlds);
  hash->AddU64(counters.retained_worlds);
}

[[nodiscard]] std::uint64_t OraclePolicyIdentity(const NegotiatedPriceUpdatePolicyV1& policy) {
  board_ir::StableHashBuilder hash;
  hash.AddString("APGAR-P4R06-NEGOTIATED-PRICE-POLICY-V1");
  hash.AddU64(policy.initial_present_factor);
  hash.AddU64(policy.present_factor_increment);
  hash.AddU64(policy.historical_price_increment);
  return OracleNonzeroHash(&hash);
}

[[nodiscard]] std::uint64_t OracleCommonStateIdentity(const CpuCandidateAllocationSession& source,
                                                      const ResourceCapacityModel& capacities) {
  board_ir::StableHashBuilder hash;
  hash.AddString("APGAR-P4R09-FIXED-POOL-COMMON-STATE-V1");
  hash.AddU64(source.session_identity());
  hash.AddU64(source.final_pool_identity());
  hash.AddU64(capacities.associations().board_content_hash);
  hash.AddU64(capacities.associations().compiler_profile_fingerprint);
  hash.AddU32(capacities.associations().geometry_compiler_version);
  hash.AddU32(capacities.capacity_units());
  OracleHashSelection(&hash, source.final_selection());
  hash.AddU64(0);
  return OracleNonzeroHash(&hash);
}

[[nodiscard]] std::uint64_t OracleWorldIdentity(std::uint64_t common,
                                                const FixedPoolCpuWorldSchedule& schedule) {
  board_ir::StableHashBuilder hash;
  hash.AddString("APGAR-P4R09-FIXED-POOL-WORLD-V1");
  hash.AddU64(common);
  OracleHashSchedule(&hash, schedule);
  return OracleNonzeroHash(&hash);
}

[[nodiscard]] std::uint64_t OracleSelectionIdentity(std::uint64_t world, std::uint32_t round,
                                                    std::uint64_t price,
                                                    const OneWorldSelection& selection) {
  board_ir::StableHashBuilder hash;
  hash.AddString("APGAR-P4R09-FIXED-POOL-SELECTION-V1");
  hash.AddU64(world);
  hash.AddU32(round);
  hash.AddU64(price);
  OracleHashSelection(&hash, selection);
  return OracleNonzeroHash(&hash);
}

[[nodiscard]] std::uint64_t OraclePriceStateIdentity(std::uint64_t common, std::uint64_t world,
                                                     const FixedPoolCpuWorldSchedule& schedule,
                                                     const FixedPoolCpuWorldPriceState& state) {
  board_ir::StableHashBuilder hash;
  hash.AddString("APGAR-P4R09-FIXED-POOL-PRICE-STATE-V1");
  hash.AddU64(common);
  hash.AddU64(world);
  OracleHashSchedule(&hash, schedule);
  hash.AddU64(state.policy_identity);
  hash.AddU64(state.prior_state_identity);
  hash.AddU64(state.completed_updates);
  hash.AddU64(state.present_factor);
  hash.AddU64(state.prices.size());
  for (const NegotiatedResourcePrice& price : state.prices) {
    OracleHashResource(&hash, price.resource);
    hash.AddU64(price.present_price);
    hash.AddU64(price.historical_price);
    hash.AddU64(price.total_price);
  }
  return OracleNonzeroHash(&hash);
}

[[nodiscard]] bool OracleCandidateBefore(const RouteCandidate* left, std::uint64_t left_score,
                                         const RouteCandidate* right, std::uint64_t right_score) {
  const CandidateMetrics& lm = left->data().metrics;
  const CandidateMetrics& rm = right->data().metrics;
  const UWide left_steps = static_cast<UWide>(lm.orthogonal_step_count) + lm.diagonal_step_count;
  const UWide right_steps = static_cast<UWide>(rm.orthogonal_step_count) + rm.diagonal_step_count;
  return std::tuple{left_score,
                    lm.intrinsic_base_cost,
                    lm.via_count,
                    lm.bend_count,
                    left_steps,
                    lm.axis_aligned_length_dbu,
                    lm.diagonal_projection_dbu,
                    left->id()} < std::tuple{right_score,
                                             rm.intrinsic_base_cost,
                                             rm.via_count,
                                             rm.bend_count,
                                             right_steps,
                                             rm.axis_aligned_length_dbu,
                                             rm.diagonal_projection_dbu,
                                             right->id()};
}

struct OracleRound {
  OneWorldSelection selection;
  std::uint64_t total_intrinsic_cost = 0;
  std::uint64_t identity = 0;
};

[[nodiscard]] OracleRound OracleSelect(const ResourceCapacityModel& capacities,
                                       std::span<const CpuCandidateAllocationPool> pools,
                                       const FixedPoolCpuWorldPriceState& price_state,
                                       std::uint64_t world, std::uint32_t round) {
  std::map<EdgeResourceKey, std::uint64_t> prices;
  for (const NegotiatedResourcePrice& price : price_state.prices) {
    prices.emplace(price.resource, price.total_price);
  }
  std::vector<OneWorldNetOutcome> outcomes;
  std::vector<const RouteCandidate*> selected;
  std::uint64_t input_candidates = 0;
  std::uint64_t intrinsic = 0;
  for (const CpuCandidateAllocationPool& pool : pools) {
    input_candidates += pool.candidates().size();
    if (pool.candidates().empty()) {
      outcomes.emplace_back(OneWorldCandidateAbsence{
          .net = pool.net(), .reason = OneWorldCandidateAbsenceReason::kEmptyPool});
      continue;
    }
    const RouteCandidate* best = nullptr;
    std::uint64_t best_score = 0;
    for (const candidates::StoredCandidate& stored : pool.candidates()) {
      std::uint64_t score = stored->data().metrics.intrinsic_base_cost;
      for (const PhysicalEdgeSpan& span : stored->data().resources) {
        for (std::uint32_t offset = 0; offset < span.edge_count; ++offset) {
          const auto found = prices.find(OracleResource(span, offset));
          if (found != prices.end()) {
            score += found->second * span.usage_units;
          }
        }
      }
      if (best == nullptr || OracleCandidateBefore(stored.get(), score, best, best_score)) {
        best = stored.get();
        best_score = score;
      }
    }
    selected.push_back(best);
    intrinsic += best->data().metrics.intrinsic_base_cost;
    outcomes.emplace_back(OneWorldSelectedCandidate{.net = pool.net(), .candidate_id = best->id()});
  }
  std::map<EdgeResourceKey, std::uint64_t> usage;
  std::uint64_t expanded = 0;
  for (const RouteCandidate* candidate : selected) {
    for (const PhysicalEdgeSpan& span : candidate->data().resources) {
      for (std::uint32_t offset = 0; offset < span.edge_count; ++offset) {
        usage[OracleResource(span, offset)] += span.usage_units;
        ++expanded;
      }
    }
  }
  ResourceAccounting accounting{
      .associations = capacities.associations(),
      .resources = {},
      .candidate_count = selected.size(),
      .expanded_resource_uses = expanded,
      .overused_resource_count = 0,
      .total_overuse_units = 0,
  };
  for (const auto& [resource, count] : usage) {
    const std::uint64_t overuse =
        count > capacities.capacity_units() ? count - capacities.capacity_units() : 0;
    accounting.resources.push_back(ResourceUsage{.resource = resource,
                                                 .capacity_units = capacities.capacity_units(),
                                                 .usage_units = count,
                                                 .overuse_units = overuse});
    if (overuse != 0) {
      ++accounting.overused_resource_count;
      accounting.total_overuse_units += overuse;
    }
  }
  OneWorldSelection selection{.associations = capacities.associations(),
                              .nets = std::move(outcomes),
                              .accounting = std::move(accounting),
                              .input_candidate_count = input_candidates};
  return OracleRound{
      .selection = selection,
      .total_intrinsic_cost = intrinsic,
      .identity = OracleSelectionIdentity(world, round, price_state.state_identity, selection)};
}

[[nodiscard]] FixedPoolCpuWorldPriceState OracleUpdate(const FixedPoolCpuWorldPriceState& prior,
                                                       const ResourceAccounting& accounting,
                                                       const FixedPoolCpuWorldSchedule& schedule,
                                                       std::uint64_t common, std::uint64_t world) {
  const std::uint64_t factor =
      schedule.price_policy.initial_present_factor +
      prior.completed_updates * schedule.price_policy.present_factor_increment;
  std::map<EdgeResourceKey, NegotiatedResourcePrice> prices;
  for (const NegotiatedResourcePrice& price : prior.prices) {
    if (price.historical_price != 0) {
      prices.emplace(price.resource,
                     NegotiatedResourcePrice{.resource = price.resource,
                                             .present_price = 0,
                                             .historical_price = price.historical_price,
                                             .total_price = price.historical_price});
    }
  }
  for (const ResourceUsage& usage : accounting.resources) {
    if (usage.overuse_units == 0) {
      continue;
    }
    NegotiatedResourcePrice& price = prices[usage.resource];
    price.resource = usage.resource;
    price.present_price = factor * usage.overuse_units;
    price.historical_price +=
        schedule.price_policy.historical_price_increment * usage.overuse_units;
    price.total_price = price.present_price + price.historical_price;
  }
  FixedPoolCpuWorldPriceState state{.policy_identity = OraclePolicyIdentity(schedule.price_policy),
                                    .prior_state_identity = prior.state_identity,
                                    .completed_updates = prior.completed_updates + 1U,
                                    .present_factor = factor,
                                    .prices = {},
                                    .state_identity = 0};
  for (const auto& [resource, price] : prices) {
    static_cast<void>(resource);
    state.prices.push_back(price);
  }
  state.state_identity = OraclePriceStateIdentity(common, world, schedule, state);
  return state;
}

[[nodiscard]] std::uint64_t OracleOutcomeIdentity(const FixedPoolCpuWorldSummary& summary,
                                                  const OneWorldSelection& selection) {
  board_ir::StableHashBuilder hash;
  hash.AddString("APGAR-P4R09-FIXED-POOL-WORLD-OUTCOME-V1");
  hash.AddU64(summary.world_identity);
  OracleHashSchedule(&hash, summary.schedule);
  hash.AddU32(static_cast<std::uint32_t>(summary.terminal_reason));
  hash.AddU64(summary.objective.selected_net_count);
  hash.AddU64(summary.objective.missing_net_count);
  hash.AddU64(summary.objective.total_overuse_units);
  hash.AddU64(summary.objective.total_intrinsic_cost);
  hash.AddU64(summary.final_price_state_identity);
  hash.AddU64(summary.final_selection_identity);
  hash.AddBool(summary.pareto_eligible);
  hash.AddU64(summary.trace.size());
  for (const FixedPoolCpuWorldRoundTrace& trace : summary.trace) {
    hash.AddU32(trace.round_index);
    hash.AddU64(trace.input_price_state_identity);
    hash.AddU64(trace.selection_identity);
    hash.AddBool(trace.output_price_state_identity.has_value());
    if (trace.output_price_state_identity.has_value()) {
      hash.AddU64(*trace.output_price_state_identity);
    }
  }
  OracleHashSelection(&hash, selection);
  return OracleNonzeroHash(&hash);
}

[[nodiscard]] bool OracleDominates(const FixedPoolCpuWorldObjective& left,
                                   const FixedPoolCpuWorldObjective& right) {
  return left.selected_net_count >= right.selected_net_count &&
         left.total_overuse_units <= right.total_overuse_units &&
         left.total_intrinsic_cost <= right.total_intrinsic_cost &&
         (left.selected_net_count > right.selected_net_count ||
          left.total_overuse_units < right.total_overuse_units ||
          left.total_intrinsic_cost < right.total_intrinsic_cost);
}

[[nodiscard]] bool OraclePreferred(const FixedPoolCpuWorldSummary& left,
                                   const FixedPoolCpuWorldSummary& right) {
  return std::tuple{
             left.objective.missing_net_count,    left.objective.total_overuse_units,
             left.objective.total_intrinsic_cost, static_cast<std::uint8_t>(left.terminal_reason),
             left.schedule.schedule_key,          left.world_identity} <
         std::tuple{
             right.objective.missing_net_count,    right.objective.total_overuse_units,
             right.objective.total_intrinsic_cost, static_cast<std::uint8_t>(right.terminal_reason),
             right.schedule.schedule_key,          right.world_identity};
}

struct OracleExecution {
  FixedPoolCpuMultiWorldDisposition disposition;
  std::uint64_t common_state_identity = 0;
  std::vector<FixedPoolCpuWorldSchedule> schedules;
  std::vector<FixedPoolCpuWorldSummary> summaries;
  std::vector<RetainedFixedPoolCpuWorld> retained;
  std::optional<std::uint64_t> preferred;
  FixedPoolCpuMultiWorldCounters counters;
  std::uint64_t identity = 0;
};

[[nodiscard]] std::uint64_t OracleExecutionIdentity(const OracleExecution& execution,
                                                    const CpuCandidateAllocationSession& source,
                                                    const FixedPoolCpuMultiWorldConfig& config) {
  board_ir::StableHashBuilder hash;
  hash.AddString("APGAR-P4R09-FIXED-POOL-MULTI-WORLD-EXECUTION-V1");
  OracleHashConfig(&hash, config);
  hash.AddU32(static_cast<std::uint32_t>(execution.disposition));
  hash.AddU64(source.session_identity());
  hash.AddU64(source.final_pool_identity());
  hash.AddU64(execution.common_state_identity);
  hash.AddU64(execution.schedules.size());
  for (const FixedPoolCpuWorldSchedule& schedule : execution.schedules) {
    OracleHashSchedule(&hash, schedule);
  }
  OracleHashCounters(&hash, execution.counters);
  hash.AddBool(execution.preferred.has_value());
  if (execution.preferred.has_value()) {
    hash.AddU64(*execution.preferred);
  }
  hash.AddU64(execution.summaries.size());
  for (const FixedPoolCpuWorldSummary& summary : execution.summaries) {
    hash.AddU64(summary.world_identity);
    OracleHashSchedule(&hash, summary.schedule);
    hash.AddU32(static_cast<std::uint32_t>(summary.terminal_reason));
    hash.AddU64(summary.objective.selected_net_count);
    hash.AddU64(summary.objective.missing_net_count);
    hash.AddU64(summary.objective.total_overuse_units);
    hash.AddU64(summary.objective.total_intrinsic_cost);
    hash.AddU64(summary.final_price_state_identity);
    hash.AddU64(summary.final_selection_identity);
    hash.AddU64(summary.outcome_identity);
    hash.AddBool(summary.pareto_eligible);
    hash.AddBool(summary.pareto_retained);
    hash.AddU64(summary.trace.size());
    for (const FixedPoolCpuWorldRoundTrace& trace : summary.trace) {
      hash.AddU32(trace.round_index);
      hash.AddU64(trace.input_price_state_identity);
      hash.AddU64(trace.selection_identity);
      hash.AddBool(trace.output_price_state_identity.has_value());
      if (trace.output_price_state_identity.has_value()) {
        hash.AddU64(*trace.output_price_state_identity);
      }
    }
  }
  hash.AddU64(execution.retained.size());
  for (const RetainedFixedPoolCpuWorld& retained : execution.retained) {
    hash.AddU64(retained.world_identity);
    hash.AddU64(retained.outcome_identity);
    hash.AddU64(retained.price_state.state_identity);
    OracleHashSelection(&hash, retained.selection);
  }
  return OracleNonzeroHash(&hash);
}

// Independent exact-small fixed-pool Multi-World oracle. It does not call the
// executor or any production selection, accounting, price, retention,
// preferred-order, counter-projection, or identity helper.
[[nodiscard]] OracleExecution OracleExecute(
    const CpuCandidateAllocationSession& source, const ResourceCapacityModel& capacities,
    std::span<const FixedPoolCpuWorldSchedule> submitted_schedules,
    const FixedPoolCpuMultiWorldConfig& config) {
  OracleExecution result{.disposition = FixedPoolCpuMultiWorldDisposition::kCompleted,
                         .common_state_identity = OracleCommonStateIdentity(source, capacities),
                         .schedules = {submitted_schedules.begin(), submitted_schedules.end()},
                         .summaries = {},
                         .retained = {},
                         .preferred = std::nullopt,
                         .counters = {},
                         .identity = 0};
  std::ranges::sort(result.schedules, {}, &FixedPoolCpuWorldSchedule::schedule_key);
  result.counters.scheduled_worlds = result.schedules.size();
  std::uint64_t source_candidates = 0;
  std::uint64_t source_resources = 0;
  for (const CpuCandidateAllocationPool& pool : source.pools()) {
    source_candidates += pool.candidates().size();
    for (const candidates::StoredCandidate& candidate : pool.candidates()) {
      for (const PhysicalEdgeSpan& span : candidate->data().resources) {
        source_resources += span.edge_count;
      }
    }
  }
  std::vector<RetainedFixedPoolCpuWorld> terminal;
  for (const FixedPoolCpuWorldSchedule& schedule : result.schedules) {
    const std::uint64_t world = OracleWorldIdentity(result.common_state_identity, schedule);
    FixedPoolCpuWorldPriceState prices{.policy_identity = 0,
                                       .prior_state_identity = 0,
                                       .completed_updates = 0,
                                       .present_factor = 0,
                                       .prices = {},
                                       .state_identity = result.common_state_identity};
    FixedPoolCpuWorldSummary summary{
        .world_identity = world,
        .schedule = schedule,
        .terminal_reason = FixedPoolCpuWorldTerminalReason::kSelectionRoundBound,
        .trace = {},
        .objective = {},
        .final_price_state_identity = 0,
        .final_selection_identity = 0,
        .outcome_identity = 0,
        .pareto_eligible = false,
        .pareto_retained = false};
    std::optional<OneWorldSelection> final;
    for (std::uint32_t round = 0; round < schedule.maximum_selection_rounds; ++round) {
      OracleRound selected = OracleSelect(capacities, source.pools(), prices, world, round);
      summary.trace.push_back(
          FixedPoolCpuWorldRoundTrace{.round_index = round,
                                      .input_price_state_identity = prices.state_identity,
                                      .selection_identity = selected.identity,
                                      .output_price_state_identity = std::nullopt});
      ++result.counters.selection_rounds;
      result.counters.candidate_evaluations += source_candidates;
      result.counters.candidate_resource_visits += source_resources;
      result.counters.selected_resource_uses +=
          selected.selection.accounting.expanded_resource_uses;
      result.counters.net_outcomes += source.pools().size();
      ++result.counters.trace_records;
      const std::uint64_t selected_count = selected.selection.accounting.candidate_count;
      const std::uint64_t missing = source.pools().size() - selected_count;
      const bool feasible = missing == 0 && selected.selection.accounting.total_overuse_units == 0;
      const bool no_candidate = missing != 0;
      const bool final_round = round + 1U == schedule.maximum_selection_rounds;
      if (feasible || no_candidate || final_round) {
        summary.terminal_reason =
            feasible       ? FixedPoolCpuWorldTerminalReason::kFeasible
            : no_candidate ? FixedPoolCpuWorldTerminalReason::kNoCandidateWithoutRegeneration
                           : FixedPoolCpuWorldTerminalReason::kSelectionRoundBound;
        summary.objective = FixedPoolCpuWorldObjective{
            .selected_net_count = selected_count,
            .missing_net_count = missing,
            .total_overuse_units = selected.selection.accounting.total_overuse_units,
            .total_intrinsic_cost = selected.total_intrinsic_cost};
        summary.final_price_state_identity = prices.state_identity;
        summary.final_selection_identity = selected.identity;
        summary.pareto_eligible = missing <= config.maximum_near_feasible_missing_nets &&
                                  selected.selection.accounting.total_overuse_units <=
                                      config.maximum_near_feasible_overuse_units;
        final = std::move(selected.selection);
        summary.outcome_identity = OracleOutcomeIdentity(summary, *final);
        break;
      }
      prices = OracleUpdate(prices, selected.selection.accounting, schedule,
                            result.common_state_identity, world);
      summary.trace.back().output_price_state_identity = prices.state_identity;
      ++result.counters.price_updates;
      result.counters.emitted_price_entries += prices.prices.size();
    }
    ++result.counters.completed_worlds;
    if (summary.pareto_eligible) {
      ++result.counters.pareto_eligible_worlds;
    }
    terminal.push_back(RetainedFixedPoolCpuWorld{.world_identity = world,
                                                 .schedule = schedule,
                                                 .terminal_reason = summary.terminal_reason,
                                                 .objective = summary.objective,
                                                 .price_state = std::move(prices),
                                                 .selection = std::move(*final),
                                                 .trace = summary.trace,
                                                 .outcome_identity = summary.outcome_identity});
    result.summaries.push_back(std::move(summary));
  }
  std::vector<bool> dominated(result.summaries.size(), false);
  for (std::size_t left = 0; left < result.summaries.size(); ++left) {
    if (!result.summaries[left].pareto_eligible) {
      continue;
    }
    for (std::size_t right = left + 1U; right < result.summaries.size(); ++right) {
      if (!result.summaries[right].pareto_eligible) {
        continue;
      }
      ++result.counters.pareto_comparisons;
      if (OracleDominates(result.summaries[left].objective, result.summaries[right].objective)) {
        dominated[right] = true;
      } else if (OracleDominates(result.summaries[right].objective,
                                 result.summaries[left].objective)) {
        dominated[left] = true;
      }
    }
  }
  std::optional<std::size_t> preferred;
  for (std::size_t index = 0; index < result.summaries.size(); ++index) {
    if (!result.summaries[index].pareto_eligible || dominated[index]) {
      continue;
    }
    result.summaries[index].pareto_retained = true;
    ++result.counters.retained_worlds;
    result.retained.push_back(std::move(terminal[index]));
    if (!preferred.has_value() ||
        OraclePreferred(result.summaries[index], result.summaries[*preferred])) {
      preferred = index;
    }
  }
  if (preferred.has_value()) {
    result.preferred = result.summaries[*preferred].world_identity;
  }
  result.disposition = result.retained.empty()
                           ? FixedPoolCpuMultiWorldDisposition::kNoEligibleWorlds
                           : FixedPoolCpuMultiWorldDisposition::kCompleted;
  result.identity = OracleExecutionIdentity(result, source, config);
  return result;
}

[[nodiscard]] std::array<FixedPoolCpuWorldSchedule, 3> MainSchedules() {
  return {
      FixedPoolCpuWorldSchedule{
          .schedule_key = 30,
          .price_policy = NegotiatedPriceUpdatePolicyV1{.initial_present_factor = 1,
                                                        .present_factor_increment = 1,
                                                        .historical_price_increment = 1},
          .maximum_selection_rounds = 1,
      },
      FixedPoolCpuWorldSchedule{
          .schedule_key = 10,
          .price_policy = NegotiatedPriceUpdatePolicyV1{.initial_present_factor = 100,
                                                        .present_factor_increment = 100,
                                                        .historical_price_increment = 10},
          .maximum_selection_rounds = 2,
      },
      FixedPoolCpuWorldSchedule{
          .schedule_key = 20,
          .price_policy = NegotiatedPriceUpdatePolicyV1{.initial_present_factor = 200,
                                                        .present_factor_increment = 50,
                                                        .historical_price_increment = 20},
          .maximum_selection_rounds = 3,
      },
  };
}

void ExpectMatchesOracle(const FixedPoolCpuMultiWorldExecution& production,
                         const OracleExecution& oracle) {
  EXPECT_EQ(production.disposition(), oracle.disposition);
  EXPECT_EQ(production.common_state_identity(), oracle.common_state_identity);
  EXPECT_TRUE(std::ranges::equal(production.schedules(), oracle.schedules));
  EXPECT_TRUE(std::ranges::equal(production.summaries(), oracle.summaries));
  EXPECT_TRUE(std::ranges::equal(production.retained_worlds(), oracle.retained));
  EXPECT_EQ(production.preferred_world_identity(), oracle.preferred);
  EXPECT_EQ(production.counters(), oracle.counters);
  EXPECT_EQ(production.execution_identity(), oracle.identity);
}

TEST(FixedPoolCpuMultiWorldTest, MatchesIndependentExactSmallOracleAndIsolatesWorldState) {
  const Fixture fixture;
  const Fixture::Source source_case = fixture.MainSource();
  const CpuCandidateAllocationSession& source = RequireSource(source_case);
  EXPECT_EQ(source.stop().reason, CpuCandidateAllocationStopReason::kSessionBoundExhausted);
  const std::array schedules = MainSchedules();
  FixedPoolCpuMultiWorldConfig config;
  config.maximum_near_feasible_overuse_units = 4;

  const FixedPoolCpuMultiWorldResult result = ExecuteFixedPoolCpuMultiWorld(
      fixture.board(), fixture.capacities(), source, source_case.config, schedules, config);
  const FixedPoolCpuMultiWorldExecution& execution = RequireExecution(result);
  const OracleExecution oracle = OracleExecute(source, fixture.capacities(), schedules, config);
  ExpectMatchesOracle(execution, oracle);
  ASSERT_EQ(execution.summaries().size(), 3U);
  EXPECT_EQ(execution.summaries()[0].schedule.schedule_key, 10U);
  EXPECT_EQ(execution.summaries()[0].terminal_reason, FixedPoolCpuWorldTerminalReason::kFeasible);
  EXPECT_EQ(execution.summaries()[1].terminal_reason, FixedPoolCpuWorldTerminalReason::kFeasible);
  EXPECT_EQ(execution.summaries()[2].terminal_reason,
            FixedPoolCpuWorldTerminalReason::kSelectionRoundBound);
  EXPECT_EQ(execution.summaries()[0].trace.front().input_price_state_identity,
            execution.common_state_identity());
  EXPECT_EQ(execution.summaries()[1].trace.front().input_price_state_identity,
            execution.common_state_identity());

  for (const FixedPoolCpuWorldSchedule& schedule : execution.schedules()) {
    const std::array single = {schedule};
    const FixedPoolCpuMultiWorldResult alone_result = ExecuteFixedPoolCpuMultiWorld(
        fixture.board(), fixture.capacities(), source, source_case.config, single, config);
    const FixedPoolCpuMultiWorldExecution& alone = RequireExecution(alone_result);
    const auto found = std::ranges::find(
        execution.summaries(), schedule.schedule_key,
        [](const FixedPoolCpuWorldSummary& summary) { return summary.schedule.schedule_key; });
    ASSERT_NE(found, execution.summaries().end());
    ASSERT_EQ(alone.summaries().size(), 1U);
    EXPECT_EQ(alone.summaries().front().world_identity, found->world_identity);
    EXPECT_EQ(alone.summaries().front().trace, found->trace);
    EXPECT_EQ(alone.summaries().front().objective, found->objective);
    EXPECT_EQ(alone.summaries().front().outcome_identity, found->outcome_identity);
  }
}

TEST(FixedPoolCpuMultiWorldTest, AcceptsEveryP4R08TerminalAndCoversAllWorldStops) {
  const Fixture fixture;
  const Fixture::Source session_bound = fixture.MainSource();
  const Fixture::Source fixed_point = fixture.FixedPointSource();
  const Fixture::Source epoch_bound = fixture.EmptyEpochBoundSource();
  EXPECT_EQ(RequireSource(session_bound).stop().reason,
            CpuCandidateAllocationStopReason::kSessionBoundExhausted);
  EXPECT_EQ(RequireSource(fixed_point).stop().reason,
            CpuCandidateAllocationStopReason::kFixedPoint);
  EXPECT_EQ(RequireSource(epoch_bound).stop().reason,
            CpuCandidateAllocationStopReason::kEpochBoundExhausted);

  const FixedPoolCpuWorldSchedule one_round{
      .schedule_key = 1,
      .price_policy = {},
      .maximum_selection_rounds = 1,
  };
  const auto session_result = ExecuteFixedPoolCpuMultiWorld(
      fixture.board(), fixture.capacities(), RequireSource(session_bound), session_bound.config,
      std::span(&one_round, 1));
  const auto fixed_result = ExecuteFixedPoolCpuMultiWorld(
      fixture.board(), fixture.capacities(), RequireSource(fixed_point), fixed_point.config,
      std::span(&one_round, 1));
  FixedPoolCpuMultiWorldConfig empty_config;
  empty_config.maximum_near_feasible_missing_nets = 1;
  const auto empty_result = ExecuteFixedPoolCpuMultiWorld(
      fixture.board(), fixture.capacities(), RequireSource(epoch_bound), epoch_bound.config,
      std::span(&one_round, 1), empty_config);
  EXPECT_EQ(RequireExecution(session_result).summaries().front().terminal_reason,
            FixedPoolCpuWorldTerminalReason::kSelectionRoundBound);
  EXPECT_EQ(RequireExecution(fixed_result).summaries().front().terminal_reason,
            FixedPoolCpuWorldTerminalReason::kFeasible);
  EXPECT_EQ(RequireExecution(empty_result).summaries().front().terminal_reason,
            FixedPoolCpuWorldTerminalReason::kNoCandidateWithoutRegeneration);
}

TEST(FixedPoolCpuMultiWorldTest, CanonicalizesSchedulesAndSourceInputsAndRepeatsExactly) {
  const Fixture fixture;
  const Fixture::Source canonical_case = fixture.MainSource();
  const Fixture::Source reversed_source_case = fixture.MainSource(true);
  const CpuCandidateAllocationSession& canonical = RequireSource(canonical_case);
  const CpuCandidateAllocationSession& reversed_source = RequireSource(reversed_source_case);
  EXPECT_EQ(canonical.session_identity(), reversed_source.session_identity());
  EXPECT_EQ(canonical.final_pool_identity(), reversed_source.final_pool_identity());
  std::array schedules = MainSchedules();
  std::array reversed_schedules = schedules;
  std::ranges::reverse(reversed_schedules);
  FixedPoolCpuMultiWorldConfig config;
  config.maximum_near_feasible_overuse_units = 4;

  const FixedPoolCpuMultiWorldResult first_result = ExecuteFixedPoolCpuMultiWorld(
      fixture.board(), fixture.capacities(), canonical, canonical_case.config, schedules, config);
  const FixedPoolCpuMultiWorldResult permuted_result =
      ExecuteFixedPoolCpuMultiWorld(fixture.board(), fixture.capacities(), reversed_source,
                                    reversed_source_case.config, reversed_schedules, config);
  const FixedPoolCpuMultiWorldResult repeated_result = ExecuteFixedPoolCpuMultiWorld(
      fixture.board(), fixture.capacities(), canonical, canonical_case.config, schedules, config);
  const FixedPoolCpuMultiWorldExecution& first = RequireExecution(first_result);
  const FixedPoolCpuMultiWorldExecution& permuted = RequireExecution(permuted_result);
  const FixedPoolCpuMultiWorldExecution& repeated = RequireExecution(repeated_result);
  EXPECT_EQ(first.execution_identity(), permuted.execution_identity());
  EXPECT_EQ(first.execution_identity(), repeated.execution_identity());
  EXPECT_TRUE(std::ranges::equal(first.summaries(), permuted.summaries()));
  EXPECT_TRUE(std::ranges::equal(first.retained_worlds(), repeated.retained_worlds()));
}

TEST(FixedPoolCpuMultiWorldTest, EnforcesExactWholeExecutionBoundsAndCheckedOverflow) {
  internal::FixedPoolCpuMultiWorldKnownWork work{
      .world_count = 3,
      .total_selection_rounds = 6,
      .total_price_updates = 3,
      .source_net_count = 3,
      .source_candidate_count = 4,
      .source_candidate_resource_uses = 42,
      .maximum_selected_resource_uses_per_round = 32,
      .maximum_price_entries_per_world = 40,
  };
  FixedPoolCpuMultiWorldConfig exact;
  exact.limits.maximum_worlds = 3;
  exact.limits.maximum_total_selection_rounds = 6;
  exact.limits.maximum_total_price_updates = 3;
  exact.limits.maximum_candidate_evaluations = 24;
  exact.limits.maximum_candidate_resource_visits = 252;
  exact.limits.maximum_selected_resource_uses = 192;
  exact.limits.maximum_net_outcomes = 18;
  exact.limits.maximum_emitted_price_entries = 120;
  exact.limits.maximum_trace_records = 6;
  exact.limits.maximum_buffered_terminal_selection_records = 9;
  exact.limits.maximum_buffered_terminal_resource_records = 96;
  exact.limits.maximum_buffered_terminal_price_records = 120;
  exact.limits.maximum_pareto_comparisons = 3;
  exact.limits.maximum_price_entries_per_world = 40;
  internal::FixedPoolCpuMultiWorldWorkProjection projection;
  EXPECT_EQ(internal::ProjectFixedPoolCpuMultiWorldKnownWork(work, exact, &projection),
            internal::FixedPoolCpuMultiWorldProjectionResult::kFits);
  EXPECT_EQ(projection.candidate_evaluations, 24U);
  EXPECT_EQ(projection.candidate_resource_visits, 252U);
  EXPECT_EQ(projection.selected_resource_uses, 192U);
  EXPECT_EQ(projection.pareto_comparisons, 3U);

  const auto expect_one_under = [&](auto member) {
    FixedPoolCpuMultiWorldConfig under = exact;
    --(under.limits.*member);
    EXPECT_EQ(internal::ProjectFixedPoolCpuMultiWorldKnownWork(work, under, nullptr),
              internal::FixedPoolCpuMultiWorldProjectionResult::kBoundExhausted);
  };
  expect_one_under(&FixedPoolCpuMultiWorldLimits::maximum_worlds);
  expect_one_under(&FixedPoolCpuMultiWorldLimits::maximum_total_selection_rounds);
  expect_one_under(&FixedPoolCpuMultiWorldLimits::maximum_total_price_updates);
  expect_one_under(&FixedPoolCpuMultiWorldLimits::maximum_candidate_evaluations);
  expect_one_under(&FixedPoolCpuMultiWorldLimits::maximum_candidate_resource_visits);
  expect_one_under(&FixedPoolCpuMultiWorldLimits::maximum_selected_resource_uses);
  expect_one_under(&FixedPoolCpuMultiWorldLimits::maximum_net_outcomes);
  expect_one_under(&FixedPoolCpuMultiWorldLimits::maximum_emitted_price_entries);
  expect_one_under(&FixedPoolCpuMultiWorldLimits::maximum_trace_records);
  expect_one_under(&FixedPoolCpuMultiWorldLimits::maximum_buffered_terminal_selection_records);
  expect_one_under(&FixedPoolCpuMultiWorldLimits::maximum_buffered_terminal_resource_records);
  expect_one_under(&FixedPoolCpuMultiWorldLimits::maximum_buffered_terminal_price_records);
  expect_one_under(&FixedPoolCpuMultiWorldLimits::maximum_pareto_comparisons);
  expect_one_under(&FixedPoolCpuMultiWorldLimits::maximum_price_entries_per_world);

  work.total_selection_rounds = std::numeric_limits<std::uint64_t>::max();
  work.total_price_updates = work.total_selection_rounds - work.world_count;
  work.source_candidate_count = std::numeric_limits<std::uint64_t>::max();
  EXPECT_EQ(internal::ProjectFixedPoolCpuMultiWorldKnownWork(work, exact, nullptr),
            internal::FixedPoolCpuMultiWorldProjectionResult::kArithmeticOverflow);
}

TEST(FixedPoolCpuMultiWorldTest, RetainsExactParetoSetEqualPointsAndThresholds) {
  const Fixture fixture;
  const Fixture::Source source_case = fixture.MainSource();
  const CpuCandidateAllocationSession& source = RequireSource(source_case);
  const std::array equal_schedules = {
      FixedPoolCpuWorldSchedule{.schedule_key = 1,
                                .price_policy = {.initial_present_factor = 1,
                                                 .present_factor_increment = 1,
                                                 .historical_price_increment = 1},
                                .maximum_selection_rounds = 1},
      FixedPoolCpuWorldSchedule{.schedule_key = 2,
                                .price_policy = {.initial_present_factor = 2,
                                                 .present_factor_increment = 1,
                                                 .historical_price_increment = 1},
                                .maximum_selection_rounds = 1},
  };
  FixedPoolCpuMultiWorldConfig ineligible;
  const auto no_eligible =
      ExecuteFixedPoolCpuMultiWorld(fixture.board(), fixture.capacities(), source,
                                    source_case.config, equal_schedules, ineligible);
  EXPECT_EQ(RequireExecution(no_eligible).disposition(),
            FixedPoolCpuMultiWorldDisposition::kNoEligibleWorlds);

  FixedPoolCpuMultiWorldConfig eligible;
  eligible.maximum_near_feasible_overuse_units = 4;
  const FixedPoolCpuMultiWorldResult equal_result = ExecuteFixedPoolCpuMultiWorld(
      fixture.board(), fixture.capacities(), source, source_case.config, equal_schedules, eligible);
  const FixedPoolCpuMultiWorldExecution& equal = RequireExecution(equal_result);
  ASSERT_EQ(equal.retained_worlds().size(), 2U);
  EXPECT_EQ(equal.retained_worlds()[0].objective, equal.retained_worlds()[1].objective);
  EXPECT_EQ(equal.preferred_world_identity(), equal.retained_worlds()[0].world_identity);

  FixedPoolCpuMultiWorldConfig exhausted = eligible;
  exhausted.limits.maximum_retained_worlds = 1;
  EXPECT_EQ(
      RequireFailure(ExecuteFixedPoolCpuMultiWorld(fixture.board(), fixture.capacities(), source,
                                                   source_case.config, equal_schedules, exhausted))
          .code,
      FixedPoolCpuMultiWorldErrorCode::kRetentionBoundExhausted);

  const std::array schedules = MainSchedules();
  const FixedPoolCpuMultiWorldResult dominated_result = ExecuteFixedPoolCpuMultiWorld(
      fixture.board(), fixture.capacities(), source, source_case.config, schedules, eligible);
  const FixedPoolCpuMultiWorldExecution& dominated = RequireExecution(dominated_result);
  ASSERT_EQ(dominated.retained_worlds().size(), 3U);
  EXPECT_TRUE(dominated.summaries().back().pareto_retained);
  EXPECT_LT(dominated.summaries().back().objective.total_intrinsic_cost,
            dominated.summaries().front().objective.total_intrinsic_cost);
  EXPECT_GT(dominated.summaries().back().objective.total_overuse_units,
            dominated.summaries().front().objective.total_overuse_units);
}

TEST(FixedPoolCpuMultiWorldTest, PreferredOrderingIsTotalAndMatchesIndependentOrder) {
  FixedPoolCpuWorldSummary left;
  FixedPoolCpuWorldSummary right;
  left.schedule.schedule_key = 1;
  right.schedule.schedule_key = 2;
  left.world_identity = 10;
  right.world_identity = 20;
  const auto expect_left = [&] {
    EXPECT_TRUE(OraclePreferred(left, right));
    EXPECT_TRUE(internal::FixedPoolCpuWorldPreferredBeforeForTesting(left, right));
    EXPECT_FALSE(internal::FixedPoolCpuWorldPreferredBeforeForTesting(right, left));
  };
  left.objective.missing_net_count = 0;
  right.objective.missing_net_count = 1;
  expect_left();
  right.objective.missing_net_count = 0;
  left.objective.total_overuse_units = 1;
  right.objective.total_overuse_units = 2;
  expect_left();
  right.objective.total_overuse_units = 1;
  left.objective.total_intrinsic_cost = 3;
  right.objective.total_intrinsic_cost = 4;
  expect_left();
  right.objective.total_intrinsic_cost = 3;
  left.terminal_reason = FixedPoolCpuWorldTerminalReason::kFeasible;
  right.terminal_reason = FixedPoolCpuWorldTerminalReason::kSelectionRoundBound;
  expect_left();
  right.terminal_reason = FixedPoolCpuWorldTerminalReason::kFeasible;
  expect_left();
  right.schedule.schedule_key = 1;
  expect_left();

  const FixedPoolCpuWorldObjective better{.selected_net_count = 3,
                                          .missing_net_count = 0,
                                          .total_overuse_units = 0,
                                          .total_intrinsic_cost = 100};
  const FixedPoolCpuWorldObjective worse{.selected_net_count = 2,
                                         .missing_net_count = 1,
                                         .total_overuse_units = 1,
                                         .total_intrinsic_cost = 110};
  EXPECT_TRUE(OracleDominates(better, worse));
  EXPECT_TRUE(internal::FixedPoolCpuWorldDominatesForTesting(better, worse));
  EXPECT_FALSE(internal::FixedPoolCpuWorldDominatesForTesting(better, better));
}

TEST(FixedPoolCpuMultiWorldTest, RejectsInvalidSchedulesLineageAndAssociations) {
  const Fixture fixture;
  const Fixture::Source source_case = fixture.MainSource();
  const CpuCandidateAllocationSession& source = RequireSource(source_case);
  FixedPoolCpuWorldSchedule invalid{
      .schedule_key = 0, .price_policy = {}, .maximum_selection_rounds = 1};
  EXPECT_EQ(
      RequireFailure(ExecuteFixedPoolCpuMultiWorld(fixture.board(), fixture.capacities(), source,
                                                   source_case.config, std::span(&invalid, 1)))
          .code,
      FixedPoolCpuMultiWorldErrorCode::kInvalidSchedule);

  const std::array duplicate_keys = {
      FixedPoolCpuWorldSchedule{
          .schedule_key = 1, .price_policy = {}, .maximum_selection_rounds = 1},
      FixedPoolCpuWorldSchedule{.schedule_key = 1,
                                .price_policy = {.initial_present_factor = 2,
                                                 .present_factor_increment = 1,
                                                 .historical_price_increment = 1},
                                .maximum_selection_rounds = 1},
  };
  EXPECT_EQ(
      RequireFailure(ExecuteFixedPoolCpuMultiWorld(fixture.board(), fixture.capacities(), source,
                                                   source_case.config, duplicate_keys))
          .code,
      FixedPoolCpuMultiWorldErrorCode::kDuplicateSchedule);

  FixedPoolCpuMultiWorldConfig source_precedence;
  source_precedence.limits.selection.maximum_net_pools = 1;
  const FixedPoolCpuMultiWorldResult source_first_result =
      ExecuteFixedPoolCpuMultiWorld(fixture.board(), fixture.capacities(), source,
                                    source_case.config, duplicate_keys, source_precedence);
  const FixedPoolCpuMultiWorldError& source_first = RequireFailure(source_first_result);
  EXPECT_EQ(source_first.code, FixedPoolCpuMultiWorldErrorCode::kBoundExhausted);
  EXPECT_EQ(source_first.invariant_id, "allocator.fixed_pool_multi_world.source_net_bound.v1");

  const std::array duplicate_exact = {
      FixedPoolCpuWorldSchedule{
          .schedule_key = 1, .price_policy = {}, .maximum_selection_rounds = 1},
      FixedPoolCpuWorldSchedule{
          .schedule_key = 2, .price_policy = {}, .maximum_selection_rounds = 1},
  };
  EXPECT_EQ(
      RequireFailure(ExecuteFixedPoolCpuMultiWorld(fixture.board(), fixture.capacities(), source,
                                                   source_case.config, duplicate_exact))
          .code,
      FixedPoolCpuMultiWorldErrorCode::kDuplicateSchedule);

  CpuCandidateAllocationSessionConfig wrong_config = source_case.config;
  ++wrong_config.limits.maximum_cumulative_targets;
  const FixedPoolCpuWorldSchedule valid{
      .schedule_key = 1, .price_policy = {}, .maximum_selection_rounds = 1};
  const FixedPoolCpuMultiWorldResult lineage_result = ExecuteFixedPoolCpuMultiWorld(
      fixture.board(), fixture.capacities(), source, wrong_config, std::span(&valid, 1));
  const FixedPoolCpuMultiWorldError& lineage = RequireFailure(lineage_result);
  EXPECT_EQ(lineage.code, FixedPoolCpuMultiWorldErrorCode::kSourceReplayFailure);
  ASSERT_TRUE(lineage.source_error.has_value());
  EXPECT_EQ(lineage.source_error->invariant_id,
            "allocator.cpu_allocation_session.replay_identity.v1");

  const Fixture other(2);
  const FixedPoolCpuMultiWorldResult association_result = ExecuteFixedPoolCpuMultiWorld(
      other.board(), other.capacities(), source, source_case.config, std::span(&valid, 1));
  const FixedPoolCpuMultiWorldError& association = RequireFailure(association_result);
  EXPECT_EQ(association.code, FixedPoolCpuMultiWorldErrorCode::kSourceReplayFailure);
  ASSERT_TRUE(association.source_error.has_value());
  EXPECT_EQ(association.source_error->code,
            CpuCandidateAllocationSessionErrorCode::kAssociationMismatch);
}

[[nodiscard]] bool FailSecondWorld(std::size_t world_index, void*) noexcept {
  return world_index != 1;
}

TEST(FixedPoolCpuMultiWorldTest, FatalFailuresAreAtomicAndInputsRemainImmutable) {
  const Fixture fixture;
  const Fixture::Source source_case = fixture.MainSource();
  const CpuCandidateAllocationSession& source = RequireSource(source_case);
  const std::uint64_t source_identity = source.session_identity();
  const std::uint64_t pool_identity = source.final_pool_identity();
  const OneWorldSelection source_selection = source.final_selection();
  const std::array schedules = MainSchedules();
  const std::array schedules_before = schedules;
  const CpuCandidateAllocationSessionConfig source_config_before = source_case.config;
  FixedPoolCpuMultiWorldConfig config;
  config.maximum_near_feasible_overuse_units = 4;
  const FixedPoolCpuMultiWorldConfig config_before = config;

  const FixedPoolCpuMultiWorldResult injected =
      internal::ExecuteFixedPoolCpuMultiWorldWithTestHooks(
          fixture.board(), fixture.capacities(), source, source_case.config, schedules, config,
          internal::FixedPoolCpuMultiWorldTestHooks{.before_world = FailSecondWorld,
                                                    .context = nullptr});
  EXPECT_EQ(RequireFailure(injected).code, FixedPoolCpuMultiWorldErrorCode::kInternalInvariant);
  EXPECT_EQ(source.session_identity(), source_identity);
  EXPECT_EQ(source.final_pool_identity(), pool_identity);
  EXPECT_EQ(source.final_selection(), source_selection);
  EXPECT_EQ(schedules, schedules_before);
  EXPECT_EQ(source_case.config, source_config_before);
  EXPECT_EQ(config, config_before);
}

TEST(FixedPoolCpuMultiWorldTest, CheckedPriceOverflowFailsClosed) {
  const Fixture fixture;
  const Fixture::Source source_case = fixture.MainSource();
  const CpuCandidateAllocationSession& source = RequireSource(source_case);
  const FixedPoolCpuWorldSchedule overflow{
      .schedule_key = 1,
      .price_policy =
          NegotiatedPriceUpdatePolicyV1{
              .initial_present_factor = std::numeric_limits<std::uint64_t>::max(),
              .present_factor_increment = 1,
              .historical_price_increment = 1,
          },
      .maximum_selection_rounds = 2,
  };
  FixedPoolCpuMultiWorldConfig config;
  config.limits.maximum_price_value = std::numeric_limits<std::uint64_t>::max();
  config.limits.maximum_aggregate_price_per_world = std::numeric_limits<std::uint64_t>::max();
  config.limits.maximum_candidate_score = std::numeric_limits<std::uint64_t>::max();
  EXPECT_EQ(RequireFailure(ExecuteFixedPoolCpuMultiWorld(fixture.board(), fixture.capacities(),
                                                         source, source_case.config,
                                                         std::span(&overflow, 1), config))
                .code,
            FixedPoolCpuMultiWorldErrorCode::kArithmeticOverflow);
}

TEST(FixedPoolCpuMultiWorldTest, StableIdentitiesBindSourceSchedulesConfigAndOutcomes) {
  const Fixture fixture;
  const Fixture::Source source_case = fixture.MainSource();
  const Fixture::Source fixed_case = fixture.FixedPointSource();
  const CpuCandidateAllocationSession& source = RequireSource(source_case);
  std::array schedules = MainSchedules();
  FixedPoolCpuMultiWorldConfig config;
  config.maximum_near_feasible_overuse_units = 4;
  const FixedPoolCpuMultiWorldResult baseline_result = ExecuteFixedPoolCpuMultiWorld(
      fixture.board(), fixture.capacities(), source, source_case.config, schedules, config);
  const FixedPoolCpuMultiWorldExecution& baseline = RequireExecution(baseline_result);

  std::array changed_schedule = schedules;
  changed_schedule[0].schedule_key = 31;
  const FixedPoolCpuMultiWorldResult schedule_changed_result = ExecuteFixedPoolCpuMultiWorld(
      fixture.board(), fixture.capacities(), source, source_case.config, changed_schedule, config);
  const FixedPoolCpuMultiWorldExecution& schedule_changed =
      RequireExecution(schedule_changed_result);
  EXPECT_NE(baseline.execution_identity(), schedule_changed.execution_identity());
  EXPECT_NE(baseline.summaries().back().world_identity,
            schedule_changed.summaries().back().world_identity);

  FixedPoolCpuMultiWorldConfig changed_config = config;
  ++changed_config.maximum_near_feasible_overuse_units;
  const FixedPoolCpuMultiWorldResult config_changed_result = ExecuteFixedPoolCpuMultiWorld(
      fixture.board(), fixture.capacities(), source, source_case.config, schedules, changed_config);
  const FixedPoolCpuMultiWorldExecution& config_changed = RequireExecution(config_changed_result);
  EXPECT_NE(baseline.execution_identity(), config_changed.execution_identity());

  const std::array one = {schedules[0]};
  const FixedPoolCpuMultiWorldResult source_changed_result =
      ExecuteFixedPoolCpuMultiWorld(fixture.board(), fixture.capacities(),
                                    RequireSource(fixed_case), fixed_case.config, one, config);
  const FixedPoolCpuMultiWorldExecution& source_changed = RequireExecution(source_changed_result);
  EXPECT_NE(baseline.common_state_identity(), source_changed.common_state_identity());
  EXPECT_NE(baseline.execution_identity(), source_changed.execution_identity());
}

}  // namespace
}  // namespace apgar::allocator
