#include "apgar/allocator/one_world_selection.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <ranges>
#include <span>
#include <string>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

#include "apgar/allocator/resource_accounting.h"
#include "apgar/board_ir/board.h"
#include "apgar/candidates/route_candidate.h"
#include "apgar/geometry_compiler/compiled_board.h"
#include "apgar/routing/candidate_policy.h"
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
inline constexpr std::array<EntityRef, 4> kNets = {
    EntityRef{.id = 10, .generation = 0},
    EntityRef{.id = 11, .generation = 0},
    EntityRef{.id = 12, .generation = 0},
    EntityRef{.id = 13, .generation = 0},
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
      .component = "ONE-WORLD",
      .pin = std::move(pin),
      .center = center,
      .connection_region = TerminalBox(center),
      .layers = {kLayer},
  };
}

[[nodiscard]] BoardData GeneratedSelectionBoard(std::uint64_t revision = 1) {
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
      .adapter_name = "generated-one-world-selection-microcase",
      .adapter_version = "1",
      .layers =
          {
              Layer{
                  .ref = EntityRef{.id = 1, .generation = 0},
                  .routing_id = kLayer,
                  .name = "front-signal",
                  .physical_order = 0,
                  .type = LayerType::kSignal,
                  .routable = true,
              },
              Layer{
                  .ref = EntityRef{.id = 2, .generation = 0},
                  .routing_id = 31,
                  .name = "back-signal",
                  .physical_order = 1,
                  .type = LayerType::kSignal,
                  .routable = true,
              },
          },
      .nets =
          {
              Net{.ref = kNets[0], .name = "MULTIPLE", .terminals = {terminals[0], terminals[1]}},
              Net{.ref = kNets[1], .name = "SHARED", .terminals = {terminals[2], terminals[3]}},
              Net{.ref = kNets[2], .name = "DISJOINT", .terminals = {terminals[4], terminals[5]}},
              Net{.ref = kNets[3], .name = "ABSENT", .terminals = {terminals[6], terminals[7]}},
          },
      .terminals =
          {
              MakeTerminal(20, kNets[0], Point64{.x = 0, .y = 0}, "A1"),
              MakeTerminal(21, kNets[0], Point64{.x = 100, .y = 0}, "A2"),
              MakeTerminal(22, kNets[1], Point64{.x = 30, .y = -30}, "B1"),
              MakeTerminal(23, kNets[1], Point64{.x = 70, .y = 30}, "B2"),
              MakeTerminal(24, kNets[2], Point64{.x = 0, .y = 60}, "C1"),
              MakeTerminal(25, kNets[2], Point64{.x = 100, .y = 60}, "C2"),
              MakeTerminal(26, kNets[3], Point64{.x = 40, .y = 100}, "D1"),
              MakeTerminal(27, kNets[3], Point64{.x = 10, .y = 130}, "D2"),
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

[[nodiscard]] CompilerProfile GeneratedSelectionProfile() {
  return CompilerProfile{
      .schema_version = geometry_compiler::kCompilerProfileSchemaVersion,
      .lattice_origin = Point64{.x = 0, .y = 0},
      .lattice_step = 10,
      .tile_width_nodes = 4,
      .tile_height_nodes = 4,
      .compilation_roi =
          AxisAlignedBox64{.min = Point64{.x = 0, .y = -30}, .max = Point64{.x = 100, .y = 130}},
      .active_regions =
          {
              ActiveRegion{.layer = kLayer,
                           .bounds = AxisAlignedBox64{.min = Point64{.x = 0, .y = 0},
                                                      .max = Point64{.x = 100, .y = 0}}},
              ActiveRegion{.layer = kLayer,
                           .bounds = AxisAlignedBox64{.min = Point64{.x = 0, .y = 10},
                                                      .max = Point64{.x = 100, .y = 10}}},
              ActiveRegion{.layer = kLayer,
                           .bounds = AxisAlignedBox64{.min = Point64{.x = 0, .y = 0},
                                                      .max = Point64{.x = 0, .y = 10}}},
              ActiveRegion{.layer = kLayer,
                           .bounds = AxisAlignedBox64{.min = Point64{.x = 100, .y = 0},
                                                      .max = Point64{.x = 100, .y = 10}}},
              ActiveRegion{.layer = kLayer,
                           .bounds = AxisAlignedBox64{.min = Point64{.x = 30, .y = -30},
                                                      .max = Point64{.x = 30, .y = 0}}},
              ActiveRegion{.layer = kLayer,
                           .bounds = AxisAlignedBox64{.min = Point64{.x = 70, .y = 0},
                                                      .max = Point64{.x = 70, .y = 30}}},
              ActiveRegion{.layer = kLayer,
                           .bounds = AxisAlignedBox64{.min = Point64{.x = 0, .y = 60},
                                                      .max = Point64{.x = 100, .y = 60}}},
              ActiveRegion{.layer = kLayer,
                           .bounds = AxisAlignedBox64{.min = Point64{.x = 40, .y = 100},
                                                      .max = Point64{.x = 40, .y = 100}}},
              ActiveRegion{.layer = kLayer,
                           .bounds = AxisAlignedBox64{.min = Point64{.x = 30, .y = 110},
                                                      .max = Point64{.x = 30, .y = 110}}},
              ActiveRegion{.layer = kLayer,
                           .bounds = AxisAlignedBox64{.min = Point64{.x = 20, .y = 120},
                                                      .max = Point64{.x = 20, .y = 120}}},
              ActiveRegion{.layer = kLayer,
                           .bounds = AxisAlignedBox64{.min = Point64{.x = 10, .y = 130},
                                                      .max = Point64{.x = 10, .y = 130}}},
          },
      .heading_mask = board_ir::kM1HeadingMask,
      .costs = DeterministicCosts{.orthogonal_step = 10, .diagonal_step = 30, .bend = 3},
  };
}

struct GeneratedMicrocase {
  board_ir::BoardSnapshot board;
  std::vector<CompiledBoard> compiled_boards;
  std::vector<RouteCandidate> candidates;
};

[[nodiscard]] RouteCandidate AdmitCandidate(const board_ir::BoardSnapshot& board,
                                            const CompiledBoard& compiled,
                                            routing::CpuRouteRequest request,
                                            std::uint64_t query_identity) {
  candidates::GeneratedRouteCandidate generated =
      test_support::CandidateDraft(board, compiled, request, 0x4100, query_identity);
  return test_support::AcceptedCandidate(
      candidates::CandidateAdmissionContext{
          .board = board,
          .compiled_board = compiled,
          .request = request,
      },
      std::move(generated));
}

[[nodiscard]] RouteCandidate AdmitSharedExactRoute(const board_ir::BoardSnapshot& board,
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
      .total_cost = 106,
      .lattice_path = {},
      .segments =
          {
              routing::LayerSegment{
                  .layer = kLayer,
                  .centerline =
                      board_ir::Segment64{
                          .start = Point64{.x = 30, .y = -30},
                          .end = Point64{.x = 30, .y = 0},
                      },
              },
              routing::LayerSegment{
                  .layer = kLayer,
                  .centerline =
                      board_ir::Segment64{
                          .start = Point64{.x = 30, .y = 0},
                          .end = Point64{.x = 70, .y = 0},
                      },
              },
              routing::LayerSegment{
                  .layer = kLayer,
                  .centerline =
                      board_ir::Segment64{
                          .start = Point64{.x = 70, .y = 0},
                          .end = Point64{.x = 70, .y = 30},
                      },
              },
          },
      .telemetry = {},
      .producer_evidence = {},
  };
  test_support::CpuRouteFaultDecorator::Reseal(route, compiled);
  candidates::CandidateDraftBuildResult draft =
      candidates::BuildGeneratedCandidateFromCpuRoute(board, compiled, request, policy, route,
                                                      candidates::CandidateSchedulingIdentity{
                                                          .batch_identity = 0x4100,
                                                          .query_identity = query_identity,
                                                      });
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

[[nodiscard]] GeneratedMicrocase GenerateMicrocase(std::uint64_t revision = 1) {
  board_ir::BoardSnapshot board = test_support::Snapshot(GeneratedSelectionBoard(revision));
  std::vector<CompiledBoard> compiled_boards;
  compiled_boards.reserve(kNets.size());
  for (EntityRef net : kNets) {
    compiled_boards.push_back(
        test_support::CompilePreparedNet(board, net, GeneratedSelectionProfile()));
  }

  std::vector<RouteCandidate> candidates;
  candidates.reserve(6);
  const routing::CpuRouteRequest first_request =
      test_support::RequestForNet(board, kNets[0], kLayer, kLayer);
  candidates.push_back(AdmitCandidate(board, compiled_boards[0], first_request, 1));
  candidates.push_back(AdmitCandidate(board, compiled_boards[0], first_request, 2));

  routing::CpuRouteRequest detour_request = first_request;
  const PhysicalEdgeSpan& first_span = candidates.front().data().resources.front();
  detour_request.candidate_policy.banned_resources.push_back(EdgeResourceKey{
      .layer = first_span.layer,
      .lattice_x = first_span.lattice_x,
      .lattice_y = first_span.lattice_y,
      .direction = first_span.direction,
  });
  candidates.push_back(AdmitCandidate(board, compiled_boards[0], detour_request, 3));

  const routing::CpuRouteRequest shared_request =
      test_support::RequestForNet(board, kNets[1], kLayer, kLayer);
  candidates.push_back(AdmitSharedExactRoute(board, compiled_boards[1], shared_request, 4));

  for (std::size_t net_index = 2; net_index < kNets.size(); ++net_index) {
    const routing::CpuRouteRequest request =
        test_support::RequestForNet(board, kNets[net_index], kLayer, kLayer);
    candidates.push_back(AdmitCandidate(board, compiled_boards[net_index], request,
                                        static_cast<std::uint64_t>(net_index + 3)));
  }
  return GeneratedMicrocase{
      .board = std::move(board),
      .compiled_boards = std::move(compiled_boards),
      .candidates = std::move(candidates),
  };
}

[[nodiscard]] ResourceCapacityModel CapacityModel(const GeneratedMicrocase& microcase) {
  ResourceCapacityModelResult result =
      BuildResourceCapacityModel(microcase.board, microcase.compiled_boards.front(), 1);
  EXPECT_TRUE(std::holds_alternative<ResourceCapacityModel>(result));
  if (!std::holds_alternative<ResourceCapacityModel>(result)) {
    std::abort();
  }
  return std::get<ResourceCapacityModel>(std::move(result));
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
  ADD_FAILURE() << "Noncanonical direction reached independent One-World oracle";
  return DirectionDelta{};
}

// Independent exact-small oracle. It selects directly from the submitted
// immutable pools and expands atomic resources itself; it never calls the
// production selector, its comparator, or the P4R-02B accounting helper.
[[nodiscard]] OneWorldSelection OracleSelection(
    const ResourceCapacityModel& capacities,
    std::span<const OneWorldCandidatePool> submitted_pools) {
  std::vector<const OneWorldCandidatePool*> pools;
  pools.reserve(submitted_pools.size());
  std::uint64_t input_candidate_count = 0;
  for (const OneWorldCandidatePool& pool : submitted_pools) {
    pools.push_back(&pool);
    input_candidate_count += pool.candidates.size();
  }
  std::ranges::sort(pools,
                    [](const OneWorldCandidatePool* left, const OneWorldCandidatePool* right) {
                      return std::pair{left->net.id, left->net.generation} <
                             std::pair{right->net.id, right->net.generation};
                    });

  std::vector<OneWorldNetOutcome> outcomes;
  std::vector<const RouteCandidate*> selected;
  outcomes.reserve(pools.size());
  selected.reserve(pools.size());
  for (const OneWorldCandidatePool* pool : pools) {
    if (pool->candidates.empty()) {
      outcomes.emplace_back(OneWorldCandidateAbsence{
          .net = pool->net,
          .reason = OneWorldCandidateAbsenceReason::kEmptyPool,
      });
      continue;
    }
    const RouteCandidate* best = pool->candidates.front();
    for (const RouteCandidate* candidate : pool->candidates.subspan(1)) {
      if (OracleCandidateBefore(candidate, best)) {
        best = candidate;
      }
    }
    selected.push_back(best);
    outcomes.emplace_back(OneWorldSelectedCandidate{.net = pool->net, .candidate_id = best->id()});
  }

  std::map<EdgeResourceKey, std::uint64_t> usage;
  std::uint64_t expanded_uses = 0;
  for (const RouteCandidate* candidate : selected) {
    for (const PhysicalEdgeSpan& span : candidate->data().resources) {
      const DirectionDelta delta = OracleStorageDelta(span.direction);
      for (std::uint32_t offset = 0; offset < span.edge_count; ++offset) {
        usage[EdgeResourceKey{
            .layer = span.layer,
            .lattice_x = span.lattice_x + static_cast<std::int64_t>(delta.x) * offset,
            .lattice_y = span.lattice_y + static_cast<std::int64_t>(delta.y) * offset,
            .direction = span.direction,
        }] += span.usage_units;
        ++expanded_uses;
      }
    }
  }

  ResourceAccounting accounting{
      .associations = capacities.associations(),
      .resources = {},
      .candidate_count = static_cast<std::uint64_t>(selected.size()),
      .expanded_resource_uses = expanded_uses,
      .overused_resource_count = 0,
      .total_overuse_units = 0,
  };
  for (const auto& [resource, usage_units] : usage) {
    const std::uint64_t overuse_units =
        usage_units > capacities.capacity_units() ? usage_units - capacities.capacity_units() : 0;
    accounting.resources.push_back(ResourceUsage{
        .resource = resource,
        .capacity_units = capacities.capacity_units(),
        .usage_units = usage_units,
        .overuse_units = overuse_units,
    });
    if (overuse_units > 0) {
      ++accounting.overused_resource_count;
      accounting.total_overuse_units += overuse_units;
    }
  }
  return OneWorldSelection{
      .associations = capacities.associations(),
      .nets = std::move(outcomes),
      .accounting = std::move(accounting),
      .input_candidate_count = input_candidate_count,
  };
}

[[nodiscard]] OneWorldSelection RequireSelection(OneWorldSelectionResult result) {
  EXPECT_TRUE(std::holds_alternative<OneWorldSelection>(result));
  if (!std::holds_alternative<OneWorldSelection>(result)) {
    std::abort();
  }
  return std::get<OneWorldSelection>(std::move(result));
}

void RequireError(const OneWorldSelectionResult& result, OneWorldSelectionErrorCode code) {
  EXPECT_TRUE(std::holds_alternative<OneWorldSelectionError>(result));
  if (!std::holds_alternative<OneWorldSelectionError>(result)) {
    std::abort();
  }
  EXPECT_EQ(std::get<OneWorldSelectionError>(result).code, code);
}

struct PoolStorage {
  std::array<const RouteCandidate*, 3> first;
  std::array<const RouteCandidate*, 1> second;
  std::array<const RouteCandidate*, 1> third;
  std::array<const RouteCandidate*, 0> fourth;
};

[[nodiscard]] PoolStorage CandidatePools(const GeneratedMicrocase& microcase) {
  return PoolStorage{
      .first = {&microcase.candidates[2], &microcase.candidates[1], &microcase.candidates[0]},
      .second = {&microcase.candidates[3]},
      .third = {&microcase.candidates[4]},
      .fourth = {},
  };
}

[[nodiscard]] std::array<OneWorldCandidatePool, 4> PoolViews(PoolStorage& storage) {
  return {
      OneWorldCandidatePool{.net = kNets[0], .candidates = storage.first},
      OneWorldCandidatePool{.net = kNets[1], .candidates = storage.second},
      OneWorldCandidatePool{.net = kNets[2], .candidates = storage.third},
      OneWorldCandidatePool{.net = kNets[3], .candidates = storage.fourth},
  };
}

TEST(OneWorldSelectionTest, SelectsMultipleCandidatesAndAccountsSharedAndDisjointResources) {
  const GeneratedMicrocase microcase = GenerateMicrocase();
  const ResourceCapacityModel capacities = CapacityModel(microcase);
  PoolStorage storage = CandidatePools(microcase);
  const std::array pools = PoolViews(storage);

  const OneWorldSelection selection =
      RequireSelection(SelectOneWorldZeroPrice(microcase.board, capacities, pools));
  EXPECT_EQ(selection, OracleSelection(capacities, pools));
  EXPECT_EQ(selection.input_candidate_count, 5U);
  EXPECT_EQ(selection.accounting.candidate_count, 3U);
  EXPECT_EQ(selection.accounting.expanded_resource_uses, 30U);
  EXPECT_EQ(selection.accounting.resources.size(), 26U);
  EXPECT_EQ(selection.accounting.overused_resource_count, 4U);
  EXPECT_EQ(selection.accounting.total_overuse_units, 4U);
  EXPECT_TRUE(std::holds_alternative<OneWorldCandidateAbsence>(selection.nets[3]));
  EXPECT_EQ(std::get<OneWorldCandidateAbsence>(selection.nets[3]).reason,
            OneWorldCandidateAbsenceReason::kEmptyPool);

  const CandidateId expected_first =
      std::min(microcase.candidates[0].id(), microcase.candidates[1].id());
  ASSERT_TRUE(std::holds_alternative<OneWorldSelectedCandidate>(selection.nets[0]));
  EXPECT_EQ(std::get<OneWorldSelectedCandidate>(selection.nets[0]).candidate_id, expected_first);
  EXPECT_LT(microcase.candidates[0].data().metrics.intrinsic_base_cost,
            microcase.candidates[2].data().metrics.intrinsic_base_cost);
}

TEST(OneWorldSelectionTest, SelectedDisjointCandidatesHaveNoOveruse) {
  const GeneratedMicrocase microcase = GenerateMicrocase();
  const ResourceCapacityModel capacities = CapacityModel(microcase);
  const std::array<const RouteCandidate*, 1> first = {&microcase.candidates[0]};
  const std::array<const RouteCandidate*, 1> third = {&microcase.candidates[4]};
  const std::array pools = {
      OneWorldCandidatePool{.net = kNets[0], .candidates = first},
      OneWorldCandidatePool{.net = kNets[2], .candidates = third},
  };

  const OneWorldSelection selection =
      RequireSelection(SelectOneWorldZeroPrice(microcase.board, capacities, pools));
  EXPECT_EQ(selection, OracleSelection(capacities, pools));
  EXPECT_EQ(selection.accounting.candidate_count, 2U);
  EXPECT_EQ(selection.accounting.overused_resource_count, 0U);
  EXPECT_EQ(selection.accounting.total_overuse_units, 0U);
}

TEST(OneWorldSelectionTest, CanonicalCandidateIdBreaksCompleteMetricTies) {
  const GeneratedMicrocase microcase = GenerateMicrocase();
  const ResourceCapacityModel capacities = CapacityModel(microcase);
  EXPECT_EQ(microcase.candidates[0].data().metrics, microcase.candidates[1].data().metrics);
  EXPECT_NE(microcase.candidates[0].id(), microcase.candidates[1].id());
  const std::array<const RouteCandidate*, 2> tied = {&microcase.candidates[1],
                                                     &microcase.candidates[0]};
  const std::array pools = {
      OneWorldCandidatePool{.net = kNets[0], .candidates = tied},
  };

  const OneWorldSelection selection =
      RequireSelection(SelectOneWorldZeroPrice(microcase.board, capacities, pools));
  ASSERT_TRUE(std::holds_alternative<OneWorldSelectedCandidate>(selection.nets.front()));
  EXPECT_EQ(std::get<OneWorldSelectedCandidate>(selection.nets.front()).candidate_id,
            std::min(microcase.candidates[0].id(), microcase.candidates[1].id()));
}

TEST(OneWorldSelectionTest, EmptyPoolProducesCanonicalStructuredAbsence) {
  const GeneratedMicrocase microcase = GenerateMicrocase();
  const ResourceCapacityModel capacities = CapacityModel(microcase);
  const std::array<const RouteCandidate*, 0> empty = {};
  const std::array pools = {
      OneWorldCandidatePool{.net = kNets[3], .candidates = empty},
  };

  const OneWorldSelection selection =
      RequireSelection(SelectOneWorldZeroPrice(microcase.board, capacities, pools));
  ASSERT_EQ(selection.nets.size(), 1U);
  EXPECT_EQ(selection.nets.front(), (OneWorldNetOutcome{OneWorldCandidateAbsence{
                                        .net = kNets[3],
                                        .reason = OneWorldCandidateAbsenceReason::kEmptyPool,
                                    }}));
  EXPECT_EQ(selection.accounting, OracleSelection(capacities, pools).accounting);
  EXPECT_EQ(selection.accounting.candidate_count, 0U);
  EXPECT_TRUE(selection.accounting.resources.empty());
}

TEST(OneWorldSelectionTest, PoolNetAndCandidatePermutationsAreNotSemantic) {
  const GeneratedMicrocase microcase = GenerateMicrocase();
  const ResourceCapacityModel capacities = CapacityModel(microcase);
  PoolStorage baseline_storage = CandidatePools(microcase);
  const std::array baseline_pools = PoolViews(baseline_storage);
  const OneWorldSelection baseline =
      RequireSelection(SelectOneWorldZeroPrice(microcase.board, capacities, baseline_pools));
  for (int repeat = 0; repeat < 8; ++repeat) {
    EXPECT_EQ(
        RequireSelection(SelectOneWorldZeroPrice(microcase.board, capacities, baseline_pools)),
        baseline);
  }

  std::array<std::size_t, 4> net_order = {0, 1, 2, 3};
  do {
    PoolStorage storage = CandidatePools(microcase);
    do {
      const std::array canonical_views = PoolViews(storage);
      std::array<OneWorldCandidatePool, 4> permuted_pools;
      for (std::size_t index = 0; index < net_order.size(); ++index) {
        permuted_pools[index] = canonical_views[net_order[index]];
      }
      EXPECT_EQ(
          RequireSelection(SelectOneWorldZeroPrice(microcase.board, capacities, permuted_pools)),
          baseline);
    } while (std::ranges::next_permutation(storage.first).found);
  } while (std::ranges::next_permutation(net_order).found);
}

TEST(OneWorldSelectionTest, InvalidPoolsIdentitiesAndAssociationsFailClosed) {
  const GeneratedMicrocase microcase = GenerateMicrocase();
  const ResourceCapacityModel capacities = CapacityModel(microcase);
  const std::array<const RouteCandidate*, 0> empty = {};
  const std::array duplicate_net = {
      OneWorldCandidatePool{.net = kNets[0], .candidates = empty},
      OneWorldCandidatePool{.net = kNets[0], .candidates = empty},
  };
  RequireError(SelectOneWorldZeroPrice(microcase.board, capacities, duplicate_net),
               OneWorldSelectionErrorCode::kDuplicateNetPool);

  const std::array unknown_net = {
      OneWorldCandidatePool{.net = EntityRef{.id = 999, .generation = 0}, .candidates = empty},
  };
  RequireError(SelectOneWorldZeroPrice(microcase.board, capacities, unknown_net),
               OneWorldSelectionErrorCode::kUnknownNet);

  const std::array<const RouteCandidate*, 1> null_candidate = {nullptr};
  const std::array null_pool = {
      OneWorldCandidatePool{.net = kNets[0], .candidates = null_candidate},
  };
  RequireError(SelectOneWorldZeroPrice(microcase.board, capacities, null_pool),
               OneWorldSelectionErrorCode::kNullCandidate);

  const std::array<const RouteCandidate*, 1> wrong_net = {&microcase.candidates[3]};
  const std::array wrong_net_pool = {
      OneWorldCandidatePool{.net = kNets[0], .candidates = wrong_net},
  };
  RequireError(SelectOneWorldZeroPrice(microcase.board, capacities, wrong_net_pool),
               OneWorldSelectionErrorCode::kCandidateNetMismatch);

  const std::array<const RouteCandidate*, 2> duplicate_candidate = {&microcase.candidates[0],
                                                                    &microcase.candidates[0]};
  const std::array duplicate_candidate_pool = {
      OneWorldCandidatePool{.net = kNets[0], .candidates = duplicate_candidate},
  };
  RequireError(SelectOneWorldZeroPrice(microcase.board, capacities, duplicate_candidate_pool),
               OneWorldSelectionErrorCode::kDuplicateCandidateIdentity);

  RouteCandidate invalid_identity = microcase.candidates[0];
  // Deliberate test-only corruption of a non-const copy; production callers
  // can only inspect the immutable accepted payload.
  candidates::GeneratedRouteCandidate& invalid_data =
      const_cast<candidates::GeneratedRouteCandidate&>(invalid_identity.data());
  invalid_data.id.low ^= 1U;
  const std::array<const RouteCandidate*, 1> invalid_candidate = {&invalid_identity};
  const std::array invalid_identity_pool = {
      OneWorldCandidatePool{.net = kNets[0], .candidates = invalid_candidate},
  };
  RequireError(SelectOneWorldZeroPrice(microcase.board, capacities, invalid_identity_pool),
               OneWorldSelectionErrorCode::kInvalidCandidateIdentity);

  const GeneratedMicrocase foreign = GenerateMicrocase(2);
  const ResourceCapacityModel foreign_capacities = CapacityModel(foreign);
  const std::array<const RouteCandidate*, 1> first_candidate = {&microcase.candidates[0]};
  const std::array first_pool = {
      OneWorldCandidatePool{.net = kNets[0], .candidates = first_candidate},
  };
  RequireError(SelectOneWorldZeroPrice(microcase.board, foreign_capacities, first_pool),
               OneWorldSelectionErrorCode::kAssociationMismatch);

  const std::array<const RouteCandidate*, 1> foreign_candidate = {&foreign.candidates[0]};
  const std::array foreign_pool = {
      OneWorldCandidatePool{.net = kNets[0], .candidates = foreign_candidate},
  };
  RequireError(SelectOneWorldZeroPrice(microcase.board, capacities, foreign_pool),
               OneWorldSelectionErrorCode::kCandidateAssociationMismatch);
}

TEST(OneWorldSelectionTest, SelectionAndAccountingArithmeticStayWithinDeclaredBounds) {
  const GeneratedMicrocase microcase = GenerateMicrocase();
  const ResourceCapacityModel capacities = CapacityModel(microcase);
  PoolStorage storage = CandidatePools(microcase);
  const std::array pools = PoolViews(storage);

  RequireError(
      SelectOneWorldZeroPrice(microcase.board, capacities, pools,
                              OneWorldSelectionLimits{
                                  .maximum_net_pools = 3,
                                  .maximum_total_candidates = kMaximumOneWorldPoolCandidates,
                                  .accounting = {},
                              }),
      OneWorldSelectionErrorCode::kInputBoundExceeded);
  RequireError(SelectOneWorldZeroPrice(microcase.board, capacities, pools,
                                       OneWorldSelectionLimits{
                                           .maximum_net_pools = kMaximumOneWorldNetPools,
                                           .maximum_total_candidates = 4,
                                           .accounting = {},
                                       }),
               OneWorldSelectionErrorCode::kInputBoundExceeded);
  RequireError(
      SelectOneWorldZeroPrice(microcase.board, capacities, pools,
                              OneWorldSelectionLimits{
                                  .maximum_net_pools = 0,
                                  .maximum_total_candidates = kMaximumOneWorldPoolCandidates,
                                  .accounting = {},
                              }),
      OneWorldSelectionErrorCode::kInvalidLimits);

  const OneWorldSelectionResult overflow = SelectOneWorldZeroPrice(
      microcase.board, capacities, pools,
      OneWorldSelectionLimits{
          .accounting =
              ResourceAccountingLimits{
                  .maximum_candidates = kMaximumResourceAccountingCandidates,
                  .maximum_expanded_resource_uses = kMaximumResourceAccountingExpandedUses,
                  .maximum_usage_units_per_resource = 1,
              },
      });
  RequireError(overflow, OneWorldSelectionErrorCode::kAccountingFailure);
  ASSERT_TRUE(std::holds_alternative<OneWorldSelectionError>(overflow));
  EXPECT_EQ(std::get<OneWorldSelectionError>(overflow).accounting_error_code,
            ResourceAccountingErrorCode::kUsageOverflow);
}

}  // namespace
}  // namespace apgar::allocator
