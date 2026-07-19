#include "apgar/benchmark/planar_corpus.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "apgar/adapters/kicad_fixture.h"
#include "apgar/routing/planar_route.h"

namespace apgar::benchmark {
namespace {

using board_ir::AxisAlignedBox64;
using board_ir::BoardCreationResult;
using board_ir::BoardData;
using board_ir::BoardSnapshot;
using board_ir::EntityRef;
using board_ir::Layer;
using board_ir::LayerType;
using board_ir::Net;
using board_ir::Point64;
using board_ir::RoutingProfile;
using board_ir::Terminal;
using geometry_compiler::ActiveRegion;
using geometry_compiler::CompiledBoard;
using geometry_compiler::CompilerProfile;
using geometry_compiler::DeterministicCosts;
using geometry_compiler::LatticeIndex;

constexpr EntityRef kTargetNet{.id = 10, .generation = 0};
constexpr EntityRef kFirstTerminal{.id = 20, .generation = 0};
constexpr EntityRef kSecondTerminal{.id = 21, .generation = 0};

struct GeneratedDescription {
  std::string name;
  std::string family;
  Point64 start;
  Point64 goal;
  CompilerProfile profile;
};

[[nodiscard]] std::string RequestIssueName(routing::TwoTerminalRequestIssue issue) {
  switch (issue) {
    case routing::TwoTerminalRequestIssue::kMissingRoutingProfileNet:
      return "routing-profile net is missing";
    case routing::TwoTerminalRequestIssue::kRequiresExactlyTwoTerminals:
      return "target net does not have exactly two terminals";
    case routing::TwoTerminalRequestIssue::kMissingTerminal:
      return "target terminal is missing";
  }
  return "unknown two-terminal request issue";
}

[[nodiscard]] AxisAlignedBox64 TerminalBox(Point64 center) {
  return AxisAlignedBox64{
      .min = Point64{.x = center.x - 1, .y = center.y - 1},
      .max = Point64{.x = center.x + 1, .y = center.y + 1},
  };
}

[[nodiscard]] BoardData GeneratedBoard(Point64 start, Point64 goal) {
  return BoardData{
      .schema_version = board_ir::kBoardSchemaVersion,
      .dbu_per_millimeter = 1'000'000,
      .revision = 1,
      .adapter_name = "phase2-planar-corpus",
      .adapter_version = "1",
      .layers =
          {
              Layer{.ref = EntityRef{.id = 1, .generation = 0},
                    .routing_id = 0,
                    .name = "front-signal",
                    .physical_order = 0,
                    .type = LayerType::kSignal,
                    .routable = true},
              Layer{.ref = EntityRef{.id = 2, .generation = 0},
                    .routing_id = 31,
                    .name = "back-signal",
                    .physical_order = 1,
                    .type = LayerType::kSignal,
                    .routable = true},
          },
      .nets = {Net{
          .ref = kTargetNet, .name = "TARGET", .terminals = {kFirstTerminal, kSecondTerminal}}},
      .terminals =
          {
              Terminal{.ref = kFirstTerminal,
                       .net = kTargetNet,
                       .component = "START",
                       .pin = "1",
                       .center = start,
                       .connection_region = TerminalBox(start),
                       .layers = {0}},
              Terminal{.ref = kSecondTerminal,
                       .net = kTargetNet,
                       .component = "GOAL",
                       .pin = "1",
                       .center = goal,
                       .connection_region = TerminalBox(goal),
                       .layers = {0}},
          },
      .obstacles = {},
      .routing_profile =
          RoutingProfile{
              .net = kTargetNet,
              .nominal_width = 2,
              .clearance = 1,
              .allowed_layers = {0},
              .allowed_headings = board_ir::kM1HeadingMask,
          },
  };
}

[[nodiscard]] CompilerProfile BaseProfile(
    AxisAlignedBox64 bounds, board_ir::DbCoord step, std::uint32_t tile_width,
    std::uint32_t tile_height, DeterministicCosts costs,
    board_ir::HeadingMask headings = board_ir::kM1HeadingMask) {
  return CompilerProfile{
      .schema_version = geometry_compiler::kCompilerProfileSchemaVersion,
      .lattice_origin = Point64{.x = 0, .y = 0},
      .lattice_step = step,
      .tile_width_nodes = tile_width,
      .tile_height_nodes = tile_height,
      .compilation_roi = bounds,
      .active_regions = {},
      .heading_mask = headings,
      .costs = costs,
  };
}

void AddPoint(CompilerProfile* profile, LatticeIndex point) {
  const Point64 exact{.x = profile->lattice_origin.x + point.x * profile->lattice_step,
                      .y = profile->lattice_origin.y + point.y * profile->lattice_step};
  profile->active_regions.push_back(
      ActiveRegion{.layer = 0, .bounds = AxisAlignedBox64{.min = exact, .max = exact}});
}

[[nodiscard]] constexpr std::uint64_t CoordinateDistance(std::int64_t left,
                                                         std::int64_t right) noexcept {
  return left >= right ? static_cast<std::uint64_t>(left) - static_cast<std::uint64_t>(right)
                       : static_cast<std::uint64_t>(right) - static_cast<std::uint64_t>(left);
}

[[nodiscard]] constexpr bool IsSupportedSegment(LatticeIndex start, LatticeIndex end) noexcept {
  const std::uint64_t distance_x = CoordinateDistance(start.x, end.x);
  const std::uint64_t distance_y = CoordinateDistance(start.y, end.y);
  return distance_x == 0 || distance_y == 0 || distance_x == distance_y;
}

static_assert(IsSupportedSegment({0, 0}, {3, 0}));
static_assert(IsSupportedSegment({0, 0}, {3, 3}));
static_assert(!IsSupportedSegment({0, 0}, {3, 1}));

[[nodiscard]] std::optional<std::string> AddInclusiveLine(CompilerProfile* profile,
                                                          LatticeIndex start, LatticeIndex end) {
  const std::int64_t delta_x = (end.x > start.x) - (end.x < start.x);
  const std::int64_t delta_y = (end.y > start.y) - (end.y < start.y);
  const std::uint64_t distance_x = CoordinateDistance(start.x, end.x);
  const std::uint64_t distance_y = CoordinateDistance(start.y, end.y);
  if (!IsSupportedSegment(start, end)) {
    return "corpus segment is not H/V/45: (" + std::to_string(start.x) + "," +
           std::to_string(start.y) + ")->(" + std::to_string(end.x) + "," + std::to_string(end.y) +
           ")";
  }
  const std::uint64_t steps = std::max(distance_x, distance_y);
  if (steps > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
    return "corpus segment exceeds the signed lattice iteration range";
  }
  for (std::uint64_t step = 0; step <= steps; ++step) {
    AddPoint(profile, LatticeIndex{.x = start.x + delta_x * static_cast<std::int64_t>(step),
                                   .y = start.y + delta_y * static_cast<std::int64_t>(step)});
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<std::string> AddPolyline(CompilerProfile* profile,
                                                     const std::vector<LatticeIndex>& vertices) {
  for (std::size_t index = 1; index < vertices.size(); ++index) {
    if (std::optional<std::string> error =
            AddInclusiveLine(profile, vertices[index - 1], vertices[index]);
        error.has_value()) {
      return error;
    }
  }
  return std::nullopt;
}

using GeneratedDescriptionsResult = std::variant<std::vector<GeneratedDescription>, std::string>;

[[nodiscard]] GeneratedDescriptionsResult GeneratedDescriptions() {
  constexpr auto kHv = static_cast<board_ir::HeadingMask>(board_ir::Heading::kHorizontal) |
                       static_cast<board_ir::HeadingMask>(board_ir::Heading::kVertical);
  std::vector<GeneratedDescription> descriptions;

  CompilerProfile dense = BaseProfile(
      AxisAlignedBox64{.min = Point64{.x = 0, .y = -25}, .max = Point64{.x = 100, .y = 25}}, 5, 8,
      8, DeterministicCosts{.orthogonal_step = 7, .diagonal_step = 11, .bend = 13});
  dense.active_regions = {ActiveRegion{.layer = 0, .bounds = dense.compilation_roi}};
  descriptions.push_back(GeneratedDescription{.name = "dense_corridors",
                                              .family = "dense-corridors",
                                              .start = Point64{.x = 0, .y = 0},
                                              .goal = Point64{.x = 100, .y = 0},
                                              .profile = std::move(dense)});

  CompilerProfile sparse = BaseProfile(
      AxisAlignedBox64{.min = Point64{.x = 0, .y = -40}, .max = Point64{.x = 100, .y = 40}}, 10, 4,
      3, DeterministicCosts{.orthogonal_step = 19, .diagonal_step = 3, .bend = 29});
  if (std::optional<std::string> error =
          AddPolyline(&sparse, {{0, 0}, {2, 2}, {4, 2}, {6, 0}, {8, -2}, {10, 0}});
      error.has_value()) {
    return *error;
  }
  descriptions.push_back(GeneratedDescription{.name = "sparse_regions",
                                              .family = "sparse-regions",
                                              .start = Point64{.x = 0, .y = 0},
                                              .goal = Point64{.x = 100, .y = 0},
                                              .profile = std::move(sparse)});

  CompilerProfile fragmented = BaseProfile(
      AxisAlignedBox64{.min = Point64{.x = 0, .y = -20}, .max = Point64{.x = 100, .y = 30}}, 10, 5,
      2, DeterministicCosts{.orthogonal_step = 17, .diagonal_step = 23, .bend = 31});
  for (LatticeIndex point : std::array{
           LatticeIndex{0, 0},
           LatticeIndex{1, 1},
           LatticeIndex{2, 1},
           LatticeIndex{3, 2},
           LatticeIndex{4, 2},
           LatticeIndex{5, 1},
           LatticeIndex{6, 1},
           LatticeIndex{7, 0},
           LatticeIndex{8, -1},
           LatticeIndex{9, -1},
           LatticeIndex{10, 0},
       }) {
    AddPoint(&fragmented, point);
  }
  descriptions.push_back(GeneratedDescription{.name = "fragmented_runs",
                                              .family = "fragmented-runs",
                                              .start = Point64{.x = 0, .y = 0},
                                              .goal = Point64{.x = 100, .y = 0},
                                              .profile = std::move(fragmented)});

  CompilerProfile maze = BaseProfile(
      AxisAlignedBox64{.min = Point64{.x = 0, .y = -40}, .max = Point64{.x = 100, .y = 40}}, 10, 3,
      3, DeterministicCosts{.orthogonal_step = 10, .diagonal_step = 14, .bend = 101}, kHv);
  if (std::optional<std::string> error = AddPolyline(
          &maze,
          {{0, 0}, {2, 0}, {2, 4}, {4, 4}, {4, -4}, {6, -4}, {6, 4}, {8, 4}, {8, 0}, {10, 0}});
      error.has_value()) {
    return *error;
  }
  descriptions.push_back(GeneratedDescription{.name = "high_turn_maze",
                                              .family = "high-turn-maze",
                                              .start = Point64{.x = 0, .y = 0},
                                              .goal = Point64{.x = 100, .y = 0},
                                              .profile = std::move(maze)});

  CompilerProfile cross_tile = BaseProfile(
      AxisAlignedBox64{.min = Point64{.x = 0, .y = -10}, .max = Point64{.x = 100, .y = 10}}, 10, 3,
      2, DeterministicCosts{.orthogonal_step = 5, .diagonal_step = 8, .bend = 2});
  cross_tile.active_regions = {ActiveRegion{.layer = 0, .bounds = cross_tile.compilation_roi}};
  descriptions.push_back(GeneratedDescription{.name = "cross_tile_edges",
                                              .family = "cross-tile-edges",
                                              .start = Point64{.x = 0, .y = 0},
                                              .goal = Point64{.x = 100, .y = 0},
                                              .profile = std::move(cross_tile)});

  CompilerProfile negative = BaseProfile(
      AxisAlignedBox64{.min = Point64{.x = -120, .y = -70}, .max = Point64{.x = 20, .y = -30}}, 10,
      4, 3, DeterministicCosts{.orthogonal_step = 37, .diagonal_step = 41, .bend = 43});
  negative.active_regions = {ActiveRegion{.layer = 0, .bounds = negative.compilation_roi}};
  descriptions.push_back(GeneratedDescription{.name = "negative_coordinates",
                                              .family = "negative-coordinates",
                                              .start = Point64{.x = -100, .y = -50},
                                              .goal = Point64{.x = 0, .y = -50},
                                              .profile = std::move(negative)});

  CompilerProfile disconnected = BaseProfile(
      AxisAlignedBox64{.min = Point64{.x = 0, .y = 0}, .max = Point64{.x = 100, .y = 0}}, 10, 4, 2,
      DeterministicCosts{.orthogonal_step = 10, .diagonal_step = 14, .bend = 3}, kHv);
  if (std::optional<std::string> error = AddInclusiveLine(&disconnected, {0, 0}, {4, 0});
      error.has_value()) {
    return *error;
  }
  if (std::optional<std::string> error = AddInclusiveLine(&disconnected, {6, 0}, {10, 0});
      error.has_value()) {
    return *error;
  }
  descriptions.push_back(GeneratedDescription{.name = "disconnected_fields",
                                              .family = "disconnected-fields",
                                              .start = Point64{.x = 0, .y = 0},
                                              .goal = Point64{.x = 100, .y = 0},
                                              .profile = std::move(disconnected)});
  return descriptions;
}

[[nodiscard]] GeneratedDescriptionsResult Phase3AdditionalDescriptions() {
  std::vector<GeneratedDescription> descriptions;

  CompilerProfile symmetric = BaseProfile(
      AxisAlignedBox64{.min = Point64{.x = 0, .y = -20},
                       .max = Point64{.x = 100, .y = 20}},
      10, 4, 3, DeterministicCosts{.orthogonal_step = 10, .diagonal_step = 14, .bend = 7});
  for (const std::vector<LatticeIndex>& path : {
           std::vector<LatticeIndex>{{0, 0}, {2, 2}, {8, 2}, {10, 0}},
           std::vector<LatticeIndex>{{0, 0}, {2, -2}, {8, -2}, {10, 0}},
       }) {
    if (std::optional<std::string> error = AddPolyline(&symmetric, path); error.has_value()) {
      return *error;
    }
  }
  descriptions.push_back(GeneratedDescription{
      .name = "symmetric_dual_corridor",
      .family = "symmetric-dual-corridor",
      .start = Point64{.x = 0, .y = 0},
      .goal = Point64{.x = 100, .y = 0},
      .profile = std::move(symmetric),
  });

  CompilerProfile channels = BaseProfile(
      AxisAlignedBox64{.min = Point64{.x = 0, .y = -20},
                       .max = Point64{.x = 120, .y = 20}},
      10, 5, 3, DeterministicCosts{.orthogonal_step = 11, .diagonal_step = 16, .bend = 9});
  for (std::int64_t channel_y : {-2, 0, 2}) {
    if (std::optional<std::string> error = AddPolyline(
            &channels, {{0, 0}, {2, 0}, {4, channel_y}, {8, channel_y}, {10, 0}, {12, 0}});
        error.has_value()) {
      return *error;
    }
  }
  descriptions.push_back(GeneratedDescription{
      .name = "multi_channel_bottleneck",
      .family = "multi-channel-resource-bottleneck",
      .start = Point64{.x = 0, .y = 0},
      .goal = Point64{.x = 120, .y = 0},
      .profile = std::move(channels),
  });

  CompilerProfile alternatives = BaseProfile(
      AxisAlignedBox64{.min = Point64{.x = 0, .y = -40},
                       .max = Point64{.x = 120, .y = 40}},
      10, 4, 4, DeterministicCosts{.orthogonal_step = 13, .diagonal_step = 18, .bend = 5});
  if (std::optional<std::string> error = AddInclusiveLine(&alternatives, {0, 0}, {2, 0});
      error.has_value()) {
    return *error;
  }
  if (std::optional<std::string> error = AddInclusiveLine(&alternatives, {10, 0}, {12, 0});
      error.has_value()) {
    return *error;
  }
  if (std::optional<std::string> error = AddInclusiveLine(&alternatives, {2, -4}, {2, 4});
      error.has_value()) {
    return *error;
  }
  if (std::optional<std::string> error = AddInclusiveLine(&alternatives, {10, -4}, {10, 4});
      error.has_value()) {
    return *error;
  }
  for (std::int64_t channel_y : {-4, -2, 0, 2, 4}) {
    if (std::optional<std::string> error =
            AddInclusiveLine(&alternatives, {2, channel_y}, {10, channel_y});
        error.has_value()) {
      return *error;
    }
  }
  descriptions.push_back(GeneratedDescription{
      .name = "policy_alternatives",
      .family = "ban-penalty-alternatives",
      .start = Point64{.x = 0, .y = 0},
      .goal = Point64{.x = 120, .y = 0},
      .profile = std::move(alternatives),
  });
  return descriptions;
}

[[nodiscard]] std::variant<PlanarCorpusCase, std::string> BuildGenerated(
    GeneratedDescription description) {
  BoardCreationResult board_result =
      board_ir::CreateBoardSnapshot(GeneratedBoard(description.start, description.goal));
  if (!std::holds_alternative<BoardSnapshot>(board_result)) {
    return std::string("generated corpus Board IR creation failed: ") + description.name + ": " +
           std::get<board_ir::BoardValidationError>(board_result).message;
  }
  BoardSnapshot board = std::get<BoardSnapshot>(std::move(board_result));
  routing::TwoTerminalRequestResult request_result =
      routing::BuildTwoTerminalRouteRequest(board, 0, 0);
  if (!std::holds_alternative<routing::CpuRouteRequest>(request_result)) {
    return std::string("generated corpus request construction failed: ") + description.name + ": " +
           RequestIssueName(std::get<routing::TwoTerminalRequestIssue>(request_result));
  }
  geometry_compiler::CompileResult compiled_result =
      geometry_compiler::CompileBoard(board, std::move(description.profile));
  if (!std::holds_alternative<CompiledBoard>(compiled_result)) {
    return std::string("generated corpus compilation failed: ") + description.name + ": " +
           std::get<geometry_compiler::CompileError>(compiled_result).detail;
  }
  return PlanarCorpusCase{
      .name = std::move(description.name),
      .family = std::move(description.family),
      .board = board,
      .compiled_board = std::get<CompiledBoard>(std::move(compiled_result)),
      .request = std::get<routing::CpuRouteRequest>(std::move(request_result)),
  };
}

[[nodiscard]] PlanarCorpusCaseResult BuildKicad(std::string_view contents) {
  adapters::KicadFixtureImportResult imported = adapters::ImportKicadFixture(
      contents, adapters::KicadFixtureImportConfig{
                    .target_net_name = "TARGET", .nominal_width = 500'000, .clearance = 500'000});
  if (!std::holds_alternative<BoardSnapshot>(imported)) {
    return std::string("KiCad corpus fixture import failed");
  }
  BoardSnapshot board = std::get<BoardSnapshot>(std::move(imported));
  routing::TwoTerminalRequestResult request_result =
      routing::BuildTwoTerminalRouteRequest(board, 0, 0);
  if (!std::holds_alternative<routing::CpuRouteRequest>(request_result)) {
    return std::string("KiCad corpus request construction failed: ") +
           RequestIssueName(std::get<routing::TwoTerminalRequestIssue>(request_result));
  }
  const AxisAlignedBox64 bounds{.min = Point64{.x = 0, .y = 6'000'000},
                                .max = Point64{.x = 20'000'000, .y = 14'000'000}};
  CompilerProfile profile =
      BaseProfile(bounds, 500'000, 8, 8,
                  DeterministicCosts{.orthogonal_step = 1000, .diagonal_step = 1414, .bend = 100});
  profile.active_regions = {ActiveRegion{.layer = 0, .bounds = bounds}};
  geometry_compiler::CompileResult compiled_result =
      geometry_compiler::CompileBoard(board, std::move(profile));
  if (!std::holds_alternative<CompiledBoard>(compiled_result)) {
    return std::string("KiCad corpus fixture compilation failed");
  }
  return PlanarCorpusCase{
      .name = "kicad_fixture",
      .family = "kicad-fixture",
      .board = board,
      .compiled_board = std::get<CompiledBoard>(std::move(compiled_result)),
      .request = std::get<routing::CpuRouteRequest>(std::move(request_result)),
  };
}

}  // namespace

PlanarCorpusCaseResult BuildPlanarBakeoffKicadCaseV1(std::string_view kicad_fixture) {
  return BuildKicad(kicad_fixture);
}

PlanarCorpusResult BuildPlanarBakeoffCorpus(std::string_view kicad_fixture) {
  std::vector<PlanarCorpusCase> corpus;
  GeneratedDescriptionsResult descriptions = GeneratedDescriptions();
  if (std::holds_alternative<std::string>(descriptions)) {
    return std::get<std::string>(std::move(descriptions));
  }
  for (GeneratedDescription& description :
       std::get<std::vector<GeneratedDescription>>(descriptions)) {
    std::variant<PlanarCorpusCase, std::string> result = BuildGenerated(std::move(description));
    if (std::holds_alternative<std::string>(result)) {
      return std::get<std::string>(std::move(result));
    }
    corpus.push_back(std::get<PlanarCorpusCase>(std::move(result)));
  }
  PlanarCorpusCaseResult kicad = BuildPlanarBakeoffKicadCaseV1(kicad_fixture);
  if (std::holds_alternative<std::string>(kicad)) {
    return std::get<std::string>(std::move(kicad));
  }
  corpus.push_back(std::get<PlanarCorpusCase>(std::move(kicad)));
  return corpus;
}

PlanarCorpusResult BuildPhase3CandidateCorpus(std::string_view kicad_fixture) {
  PlanarCorpusResult base = BuildPlanarBakeoffCorpus(kicad_fixture);
  if (std::holds_alternative<std::string>(base)) {
    return std::get<std::string>(std::move(base));
  }
  std::vector<PlanarCorpusCase> corpus =
      std::get<std::vector<PlanarCorpusCase>>(std::move(base));
  GeneratedDescriptionsResult descriptions = Phase3AdditionalDescriptions();
  if (std::holds_alternative<std::string>(descriptions)) {
    return std::get<std::string>(std::move(descriptions));
  }
  for (GeneratedDescription& description :
       std::get<std::vector<GeneratedDescription>>(descriptions)) {
    std::variant<PlanarCorpusCase, std::string> result = BuildGenerated(std::move(description));
    if (std::holds_alternative<std::string>(result)) {
      return std::get<std::string>(std::move(result));
    }
    corpus.push_back(std::get<PlanarCorpusCase>(std::move(result)));
  }
  return corpus;
}

}  // namespace apgar::benchmark
