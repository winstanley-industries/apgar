#include "apgar/allocator/resource_accounting.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "apgar/board_ir/board.h"
#include "apgar/candidates/route_candidate.h"
#include "apgar/geometry_compiler/compiled_board.h"
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
using candidates::PhysicalEdgeSpan;
using candidates::RouteCandidate;
using geometry_compiler::ActiveRegion;
using geometry_compiler::CompiledBoard;
using geometry_compiler::CompilerProfile;
using geometry_compiler::DeterministicCosts;
using geometry_compiler::Direction;
using geometry_compiler::DirectionDelta;
using routing::EdgeResourceKey;

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
      .component = "MICRO",
      .pin = std::move(pin),
      .center = center,
      .connection_region = TerminalBox(center),
      .layers = {kLayer},
  };
}

[[nodiscard]] BoardData GeneratedDistinctNetBoard(std::uint64_t revision = 1) {
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
      .adapter_name = "generated-resource-accounting-microcase",
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
              Net{.ref = kNets[0], .name = "SHARED-A", .terminals = {terminals[0], terminals[1]}},
              Net{.ref = kNets[1], .name = "SHARED-B", .terminals = {terminals[2], terminals[3]}},
              Net{.ref = kNets[2], .name = "DISJOINT", .terminals = {terminals[4], terminals[5]}},
              Net{.ref = kNets[3], .name = "NORTH-WEST", .terminals = {terminals[6], terminals[7]}},
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

[[nodiscard]] CompilerProfile GeneratedCorridorProfile() {
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

[[nodiscard]] GeneratedMicrocase GenerateAndAdmitMicrocase(std::uint64_t revision = 1) {
  board_ir::BoardSnapshot board = test_support::Snapshot(GeneratedDistinctNetBoard(revision));
  std::vector<CompiledBoard> compiled_boards;
  std::vector<RouteCandidate> candidates;
  compiled_boards.reserve(kNets.size());
  candidates.reserve(kNets.size());
  for (std::size_t index = 0; index < kNets.size(); ++index) {
    compiled_boards.push_back(
        test_support::CompilePreparedNet(board, kNets[index], GeneratedCorridorProfile()));
    const routing::CpuRouteRequest request =
        test_support::RequestForNet(board, kNets[index], kLayer, kLayer);
    candidates::GeneratedRouteCandidate generated = test_support::CandidateDraft(
        board, compiled_boards.back(), request, 1, static_cast<std::uint64_t>(index + 1));
    candidates.push_back(test_support::AcceptedCandidate(
        candidates::CandidateAdmissionContext{
            .board = board,
            .compiled_board = compiled_boards.back(),
            .request = request,
        },
        std::move(generated)));
  }
  return GeneratedMicrocase{
      .board = std::move(board),
      .compiled_boards = std::move(compiled_boards),
      .candidates = std::move(candidates),
  };
}

[[nodiscard]] ResourceCapacityModel CapacityModel(const GeneratedMicrocase& microcase,
                                                  std::uint32_t capacity_units = 1) {
  ResourceCapacityModelResult result = BuildResourceCapacityModel(
      microcase.board, microcase.compiled_boards.front(), capacity_units);
  EXPECT_TRUE(std::holds_alternative<ResourceCapacityModel>(result));
  if (!std::holds_alternative<ResourceCapacityModel>(result)) {
    std::abort();
  }
  return std::get<ResourceCapacityModel>(std::move(result));
}

[[nodiscard]] std::vector<const RouteCandidate*> CandidatePointers(
    const GeneratedMicrocase& microcase, std::span<const std::size_t> indices) {
  std::vector<const RouteCandidate*> candidates;
  candidates.reserve(indices.size());
  for (std::size_t index : indices) {
    candidates.push_back(&microcase.candidates[index]);
  }
  return candidates;
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
  ADD_FAILURE() << "Noncanonical direction reached independent accounting oracle";
  return DirectionDelta{};
}

// Independent test oracle: directly expands every admitted span into the
// canonical atomic EdgeResourceKey vocabulary and performs its own addition.
// It does not call the production accumulator or share an expansion helper.
[[nodiscard]] ResourceAccounting OracleAccounting(
    const ResourceCapacityModel& capacities,
    std::span<const RouteCandidate* const> selected_candidates) {
  std::map<EdgeResourceKey, std::uint64_t> usage;
  std::uint64_t expanded_resource_uses = 0;
  for (const RouteCandidate* candidate : selected_candidates) {
    for (const PhysicalEdgeSpan& span : candidate->data().resources) {
      const DirectionDelta delta = OracleStorageDelta(span.direction);
      for (std::uint32_t offset = 0; offset < span.edge_count; ++offset) {
        const EdgeResourceKey resource{
            .layer = span.layer,
            .lattice_x = span.lattice_x + static_cast<std::int64_t>(delta.x) * offset,
            .lattice_y = span.lattice_y + static_cast<std::int64_t>(delta.y) * offset,
            .direction = span.direction,
        };
        usage[resource] += span.usage_units;
        ++expanded_resource_uses;
      }
    }
  }
  ResourceAccounting expected{
      .associations = capacities.associations(),
      .resources = {},
      .candidate_count = static_cast<std::uint64_t>(selected_candidates.size()),
      .expanded_resource_uses = expanded_resource_uses,
      .overused_resource_count = 0,
      .total_overuse_units = 0,
  };
  for (const auto& [resource, usage_units] : usage) {
    const std::uint64_t overuse_units =
        usage_units > capacities.capacity_units() ? usage_units - capacities.capacity_units() : 0;
    expected.resources.push_back(ResourceUsage{
        .resource = resource,
        .capacity_units = capacities.capacity_units(),
        .usage_units = usage_units,
        .overuse_units = overuse_units,
    });
    if (overuse_units > 0) {
      ++expected.overused_resource_count;
      expected.total_overuse_units += overuse_units;
    }
  }
  return expected;
}

[[nodiscard]] const ResourceAccounting& RequireAccounting(const ResourceAccountingResult& result) {
  EXPECT_TRUE(std::holds_alternative<ResourceAccounting>(result));
  if (!std::holds_alternative<ResourceAccounting>(result)) {
    std::abort();
  }
  return std::get<ResourceAccounting>(result);
}

void RequireError(const ResourceAccountingResult& result, ResourceAccountingErrorCode code) {
  EXPECT_TRUE(std::holds_alternative<ResourceAccountingError>(result));
  if (!std::holds_alternative<ResourceAccountingError>(result)) {
    std::abort();
  }
  const ResourceAccountingError& error = std::get<ResourceAccountingError>(result);
  EXPECT_EQ(error.code, code);
}

TEST(ResourceAccountingTest, GeneratedDistinctNetsExposeSharedAndDisjointCapacity) {
  const GeneratedMicrocase microcase = GenerateAndAdmitMicrocase();
  const ResourceCapacityModel capacities = CapacityModel(microcase);
  constexpr std::array<std::size_t, 4> kAll = {0, 1, 2, 3};
  const std::vector<const RouteCandidate*> candidates = CandidatePointers(microcase, kAll);

  const ResourceAccountingResult result = AccumulateResourceUsage(capacities, candidates);
  const ResourceAccounting& accounting = RequireAccounting(result);
  EXPECT_EQ(accounting, OracleAccounting(capacities, candidates));
  EXPECT_EQ(accounting.candidate_count, 4U);
  EXPECT_EQ(accounting.expanded_resource_uses, 33U);
  EXPECT_EQ(accounting.resources.size(), 29U);
  EXPECT_EQ(accounting.overused_resource_count, 4U);
  EXPECT_EQ(accounting.total_overuse_units, 4U);
  EXPECT_EQ(std::ranges::count_if(accounting.resources,
                                  [](const ResourceUsage& usage) {
                                    return usage.usage_units == 2 && usage.capacity_units == 1 &&
                                           usage.overuse_units == 1;
                                  }),
            4);

  EXPECT_TRUE(std::ranges::any_of(
      microcase.candidates[1].data().resources, [](const PhysicalEdgeSpan& span) {
        return span.direction == Direction::kEast && span.edge_count == 4 && span.usage_units == 1;
      }));
  EXPECT_TRUE(std::ranges::any_of(microcase.candidates[3].data().resources,
                                  [](const PhysicalEdgeSpan& span) {
                                    return span.direction == Direction::kNorthWest &&
                                           span.edge_count == 3 && span.usage_units == 1;
                                  }));
}

TEST(ResourceAccountingTest, DisjointGeneratedCandidatesHaveNoOveruse) {
  const GeneratedMicrocase microcase = GenerateAndAdmitMicrocase();
  const ResourceCapacityModel capacities = CapacityModel(microcase);
  constexpr std::array<std::size_t, 2> kDisjoint = {0, 2};
  const std::vector<const RouteCandidate*> candidates = CandidatePointers(microcase, kDisjoint);

  const ResourceAccountingResult result = AccumulateResourceUsage(capacities, candidates);
  const ResourceAccounting& accounting = RequireAccounting(result);
  EXPECT_EQ(accounting, OracleAccounting(capacities, candidates));
  EXPECT_EQ(accounting.overused_resource_count, 0U);
  EXPECT_EQ(accounting.total_overuse_units, 0U);
}

TEST(ResourceAccountingTest, CandidatePermutationIsNotSemantic) {
  const GeneratedMicrocase microcase = GenerateAndAdmitMicrocase();
  const ResourceCapacityModel capacities = CapacityModel(microcase);
  std::array<std::size_t, 4> order = {0, 1, 2, 3};
  const std::vector<const RouteCandidate*> baseline_candidates =
      CandidatePointers(microcase, order);
  const ResourceAccounting baseline =
      RequireAccounting(AccumulateResourceUsage(capacities, baseline_candidates));

  do {
    const std::vector<const RouteCandidate*> permuted = CandidatePointers(microcase, order);
    EXPECT_EQ(RequireAccounting(AccumulateResourceUsage(capacities, permuted)), baseline);
  } while (std::ranges::next_permutation(order).found);
}

TEST(ResourceAccountingTest, CapacityZeroMakesEveryAtomicUseOverCapacity) {
  const GeneratedMicrocase microcase = GenerateAndAdmitMicrocase();
  const ResourceCapacityModel capacities = CapacityModel(microcase, 0);
  constexpr std::array<std::size_t, 1> kOne = {2};
  const std::vector<const RouteCandidate*> candidates = CandidatePointers(microcase, kOne);

  const ResourceAccountingResult result = AccumulateResourceUsage(capacities, candidates);
  const ResourceAccounting& accounting = RequireAccounting(result);
  EXPECT_EQ(accounting, OracleAccounting(capacities, candidates));
  EXPECT_EQ(accounting.overused_resource_count, 10U);
  EXPECT_EQ(accounting.total_overuse_units, 10U);
}

TEST(ResourceAccountingTest, InvalidModelsAndCandidateSetsFailClosed) {
  const GeneratedMicrocase microcase = GenerateAndAdmitMicrocase();
  const ResourceCapacityModelResult invalid_capacity =
      BuildResourceCapacityModel(microcase.board, microcase.compiled_boards.front(), 2);
  ASSERT_TRUE(std::holds_alternative<ResourceAccountingError>(invalid_capacity));
  EXPECT_EQ(std::get<ResourceAccountingError>(invalid_capacity).code,
            ResourceAccountingErrorCode::kInvalidCapacity);

  const GeneratedMicrocase foreign = GenerateAndAdmitMicrocase(2);
  const ResourceCapacityModelResult mismatched_source =
      BuildResourceCapacityModel(foreign.board, microcase.compiled_boards.front(), 1);
  ASSERT_TRUE(std::holds_alternative<ResourceAccountingError>(mismatched_source));
  EXPECT_EQ(std::get<ResourceAccountingError>(mismatched_source).code,
            ResourceAccountingErrorCode::kAssociationMismatch);

  const ResourceCapacityModel capacities = CapacityModel(microcase);
  const std::array<const RouteCandidate*, 1> null_candidate = {nullptr};
  RequireError(AccumulateResourceUsage(capacities, null_candidate),
               ResourceAccountingErrorCode::kNullCandidate);

  const std::array<const RouteCandidate*, 2> duplicate = {&microcase.candidates[0],
                                                          &microcase.candidates[0]};
  RequireError(AccumulateResourceUsage(capacities, duplicate),
               ResourceAccountingErrorCode::kDuplicateCandidate);

  const std::array<const RouteCandidate*, 1> foreign_candidate = {&foreign.candidates[0]};
  RequireError(AccumulateResourceUsage(capacities, foreign_candidate),
               ResourceAccountingErrorCode::kCandidateAssociationMismatch);
}

TEST(ResourceAccountingTest, WorkAndArithmeticBoundsFailBeforeWrapping) {
  const GeneratedMicrocase microcase = GenerateAndAdmitMicrocase();
  const ResourceCapacityModel capacities = CapacityModel(microcase);
  constexpr std::array<std::size_t, 4> kAll = {0, 1, 2, 3};
  const std::vector<const RouteCandidate*> candidates = CandidatePointers(microcase, kAll);

  RequireError(AccumulateResourceUsage(
                   capacities, candidates,
                   ResourceAccountingLimits{
                       .maximum_candidates = 2,
                       .maximum_expanded_resource_uses = kMaximumResourceAccountingExpandedUses,
                   }),
               ResourceAccountingErrorCode::kInputBoundExceeded);
  RequireError(
      AccumulateResourceUsage(capacities, candidates,
                              ResourceAccountingLimits{
                                  .maximum_candidates = kMaximumResourceAccountingCandidates,
                                  .maximum_expanded_resource_uses = 1,
                              }),
      ResourceAccountingErrorCode::kInputBoundExceeded);
  RequireError(AccumulateResourceUsage(
                   capacities, candidates,
                   ResourceAccountingLimits{
                       .maximum_candidates = kMaximumResourceAccountingCandidates,
                       .maximum_expanded_resource_uses = kMaximumResourceAccountingExpandedUses,
                       .maximum_usage_units_per_resource = 1,
                   }),
               ResourceAccountingErrorCode::kUsageOverflow);
  RequireError(AccumulateResourceUsage(
                   capacities, candidates,
                   ResourceAccountingLimits{
                       .maximum_candidates = 0,
                       .maximum_expanded_resource_uses = kMaximumResourceAccountingExpandedUses,
                   }),
               ResourceAccountingErrorCode::kInvalidLimits);
}

}  // namespace
}  // namespace apgar::allocator
