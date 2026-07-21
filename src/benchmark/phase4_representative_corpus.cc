#include "apgar/benchmark/phase4_representative_corpus.h"

#include <algorithm>
#include <array>
#include <limits>
#include <new>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <utility>

#include "apgar/board_ir/stable_hash.h"
#include "apgar/geometry_compiler/compiled_board.h"
#include "src/benchmark/phase4_representative_corpus_internal.h"

namespace apgar::benchmark {
namespace {

using allocator::MultiNetRoutingSpec;
using board_ir::AxisAlignedBox64;
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
using geometry_compiler::CompilerProfile;
using geometry_compiler::DeterministicCosts;

constexpr std::uint64_t kMaximumActiveRegionsV1 = 1'000'000;
constexpr std::uint64_t kMaximumBoardEntitiesV1 = 1'000'000;
constexpr std::size_t kDescriptorCountV1 = 42;
constexpr std::array<std::uint32_t, 3> kPrimaryPools = {4, 8, 16};
constexpr board_ir::HeadingMask kOrthogonalHeadings =
    static_cast<board_ir::HeadingMask>(board_ir::Heading::kHorizontal) |
    static_cast<board_ir::HeadingMask>(board_ir::Heading::kVertical);

#if defined(APGAR_PHASE4_REPRESENTATIVE_CORPUS_FAULT_TEST_VARIANT)
thread_local internal::Phase4RepresentativeCorpusFaultForTesting
    g_phase4_representative_corpus_fault_for_testing =
        internal::Phase4RepresentativeCorpusFaultForTesting::kNone;
#endif

void MaybeThrowRepresentativeCorpusFaultForTesting() {
#if defined(APGAR_PHASE4_REPRESENTATIVE_CORPUS_FAULT_TEST_VARIANT)
  const internal::Phase4RepresentativeCorpusFaultForTesting fault =
      g_phase4_representative_corpus_fault_for_testing;
  g_phase4_representative_corpus_fault_for_testing =
      internal::Phase4RepresentativeCorpusFaultForTesting::kNone;
  if (fault == internal::Phase4RepresentativeCorpusFaultForTesting::kBadAlloc) {
    throw std::bad_alloc();
  }
  if (fault == internal::Phase4RepresentativeCorpusFaultForTesting::kLengthError) {
    throw std::length_error("injected Phase 4 representative corpus length error");
  }
#endif
}

constexpr Phase4CorpusFeatureMask kPortalFeatures =
    Feature(Phase4CorpusFeature::kRouteLength) | Feature(Phase4CorpusFeature::kOccupancy) |
    Feature(Phase4CorpusFeature::kRuleBucket) | Feature(Phase4CorpusFeature::kRoiSize);
constexpr Phase4CorpusFeatureMask kPinFieldFeatures =
    Feature(Phase4CorpusFeature::kRouteLength) | Feature(Phase4CorpusFeature::kOccupancy) |
    Feature(Phase4CorpusFeature::kRuleBucket) | Feature(Phase4CorpusFeature::kRoiSize);
constexpr Phase4CorpusFeatureMask kFragmentedFeatures =
    Feature(Phase4CorpusFeature::kRouteLength) | Feature(Phase4CorpusFeature::kOccupancy) |
    Feature(Phase4CorpusFeature::kFragmentation) | Feature(Phase4CorpusFeature::kTurnComplexity) |
    Feature(Phase4CorpusFeature::kReachability) | Feature(Phase4CorpusFeature::kRuleBucket) |
    Feature(Phase4CorpusFeature::kRoiSize);

[[nodiscard]] constexpr Phase4CorpusFeatureMask FeaturesFor(Phase4SyntheticFamily family) noexcept {
  switch (family) {
    case Phase4SyntheticFamily::kPortalChannels:
      return kPortalFeatures;
    case Phase4SyntheticFamily::kPinFieldCrossbar:
      return kPinFieldFeatures;
    case Phase4SyntheticFamily::kFragmentedMaze:
      return kFragmentedFeatures;
    case Phase4SyntheticFamily::kImportedGuardrail:
      return Feature(Phase4CorpusFeature::kRouteLength) |
             Feature(Phase4CorpusFeature::kRuleBucket) | Feature(Phase4CorpusFeature::kRoiSize);
  }
  return 0;
}

[[nodiscard]] constexpr std::uint32_t ReachableCount(Phase4SyntheticFamily family,
                                                     std::uint32_t net_count) noexcept {
  return family == Phase4SyntheticFamily::kFragmentedMaze ? net_count - (net_count / 32U)
                                                          : net_count;
}

[[nodiscard]] constexpr Phase4CaseDescriptor SyntheticDescriptor(
    std::uint32_t case_id, Phase4SyntheticFamily family, Phase4CaseRole role, std::uint64_t seed,
    std::uint32_t net_count, std::array<std::uint32_t, 3> pools, std::uint8_t pool_count,
    std::uint64_t exact_products = 0, std::uint32_t stress_target = 0) noexcept {
  return Phase4CaseDescriptor{
      .case_id = case_id,
      .source = Phase4CaseSource::kSynthetic,
      .family = family,
      .role = role,
      .deterministic_seed = seed,
      .requested_net_count = net_count,
      .declared_reachable_net_count = ReachableCount(family, net_count),
      .requested_pool_sizes = pools,
      .requested_pool_size_count = pool_count,
      .features = FeaturesFor(family),
      .maximum_exact_candidate_products = exact_products,
      .declared_stress_target_nets = stress_target,
      .known_unmapped_exact_conflicts = false,
      .globally_coupled_conflict_graph = family == Phase4SyntheticFamily::kPinFieldCrossbar,
  };
}

[[nodiscard]] constexpr std::array<Phase4CaseDescriptor, kDescriptorCountV1>
MakeDescriptors() noexcept {
  std::array<Phase4CaseDescriptor, kDescriptorCountV1> descriptors{};
  std::size_t write = 0;
  for (std::uint32_t family_index = 0; family_index < 3; ++family_index) {
    const auto family = static_cast<Phase4SyntheticFamily>(family_index);
    descriptors[write++] =
        SyntheticDescriptor(100U + family_index, family, Phase4CaseRole::kExactOracle,
                            0x1000ULL + family_index, 6, {4, 0, 0}, 1, 4'096);
  }
  for (std::uint32_t family_index = 0; family_index < 3; ++family_index) {
    const auto family = static_cast<Phase4SyntheticFamily>(family_index);
    for (std::uint32_t instance = 0; instance < 2; ++instance) {
      descriptors[write++] = SyntheticDescriptor(
          200U + family_index * 10U + instance, family, Phase4CaseRole::kCalibration,
          0xCA110000ULL + family_index * 0x100ULL + instance, 64, kPrimaryPools, 3);
    }
  }
  for (std::uint32_t family_index = 0; family_index < 3; ++family_index) {
    const auto family = static_cast<Phase4SyntheticFamily>(family_index);
    const std::uint32_t net_count = family == Phase4SyntheticFamily::kFragmentedMaze ? 384U : 256U;
    for (std::uint32_t instance = 0; instance < 8; ++instance) {
      descriptors[write++] = SyntheticDescriptor(
          1'000U + family_index * 100U + instance, family, Phase4CaseRole::kHeldOut,
          0xE1D00000ULL + family_index * 0x100ULL + instance, net_count, kPrimaryPools, 3);
    }
  }
  descriptors[write++] =
      SyntheticDescriptor(2'000, Phase4SyntheticFamily::kPortalChannels,
                          Phase4CaseRole::kQueryShape, 0x51510000ULL, 1, {1'024, 0, 0}, 1);
  descriptors[write++] =
      SyntheticDescriptor(2'001, Phase4SyntheticFamily::kPortalChannels,
                          Phase4CaseRole::kQueryShape, 0x51510001ULL, 1'024, {1, 0, 0}, 1);
  descriptors[write++] =
      SyntheticDescriptor(2'002, Phase4SyntheticFamily::kPortalChannels,
                          Phase4CaseRole::kQueryShape, 0x51510002ULL, 256, {4, 0, 0}, 1);
  descriptors[write++] =
      SyntheticDescriptor(2'003, Phase4SyntheticFamily::kPortalChannels,
                          Phase4CaseRole::kQueryShape, 0x51510003ULL, 128, {8, 0, 0}, 1);
  descriptors[write++] =
      SyntheticDescriptor(2'004, Phase4SyntheticFamily::kPortalChannels,
                          Phase4CaseRole::kQueryShape, 0x51510004ULL, 64, {16, 0, 0}, 1);
  for (std::uint32_t tier = 0; tier < 3; ++tier) {
    descriptors[write++] = SyntheticDescriptor(
        3'000U + tier, Phase4SyntheticFamily::kPortalChannels, Phase4CaseRole::kStress,
        0x57E55000ULL + tier, 1'024U << tier, {4, 0, 0}, 1, 0, 4'096);
  }
  descriptors[write++] = Phase4CaseDescriptor{
      .case_id = 4'000,
      .source = Phase4CaseSource::kImportedFixture,
      .family = Phase4SyntheticFamily::kImportedGuardrail,
      .role = Phase4CaseRole::kImportedGuardrail,
      .deterministic_seed = 0,
      .requested_net_count = 2,
      .declared_reachable_net_count = 2,
      .requested_pool_sizes = kPrimaryPools,
      .requested_pool_size_count = 3,
      .features = FeaturesFor(Phase4SyntheticFamily::kImportedGuardrail),
      .maximum_exact_candidate_products = 0,
      .declared_stress_target_nets = 0,
      .known_unmapped_exact_conflicts = false,
      .globally_coupled_conflict_graph = false,
  };
  return descriptors;
}

constexpr auto kDescriptorsV1 = MakeDescriptors();
static_assert(kDescriptorsV1.back().case_id == 4'000);

[[nodiscard]] Phase4RepresentativeCorpusError Error(Phase4RepresentativeCorpusErrorCode code,
                                                    std::string_view invariant_id,
                                                    std::string_view detail,
                                                    std::uint32_t case_id) noexcept {
  return Phase4RepresentativeCorpusError{
      .code = code,
      .invariant_id = invariant_id,
      .detail = detail,
      .requested_case_id = case_id,
      .limiting_work_bound = Phase4RepresentativeWorkBound::kNone,
      .maximum_preparable_net_count = 0,
      .first_unpreparable_net = std::nullopt,
      .required_compiled_nodes = 0,
      .configured_compiled_node_limit = 0,
      .required_compiled_host_bytes = 0,
      .configured_compiled_host_byte_limit = 0,
  };
}

[[nodiscard]] Phase4RepresentativeCorpusError WorkBoundError(
    std::uint32_t case_id, Phase4RepresentativeWorkBound bound,
    std::uint64_t maximum_preparable_net_count, board_ir::EntityRef first_unpreparable_net,
    std::uint64_t required_compiled_nodes, std::uint64_t configured_compiled_node_limit,
    std::uint64_t required_compiled_host_bytes,
    std::uint64_t configured_compiled_host_byte_limit) noexcept {
  return Phase4RepresentativeCorpusError{
      .code = Phase4RepresentativeCorpusErrorCode::kWorkBoundExceeded,
      .invariant_id = "benchmark.phase4_representative.compiled_work_bound.v1",
      .detail = "Representative case exceeds a compiled-node or logical-host-byte bound",
      .requested_case_id = case_id,
      .limiting_work_bound = bound,
      .maximum_preparable_net_count = maximum_preparable_net_count,
      .first_unpreparable_net = first_unpreparable_net,
      .required_compiled_nodes = required_compiled_nodes,
      .configured_compiled_node_limit = configured_compiled_node_limit,
      .required_compiled_host_bytes = required_compiled_host_bytes,
      .configured_compiled_host_byte_limit = configured_compiled_host_byte_limit,
  };
}

[[nodiscard]] Phase4RepresentativeWorkBound LimitingWorkBound(bool nodes,
                                                              bool host_bytes) noexcept {
  if (nodes && host_bytes) {
    return Phase4RepresentativeWorkBound::kCompiledNodesAndHostBytes;
  }
  return nodes ? Phase4RepresentativeWorkBound::kCompiledNodes
               : Phase4RepresentativeWorkBound::kCompiledHostBytes;
}

[[nodiscard]] std::uint64_t CounterValue(std::uint64_t seed, std::uint64_t counter) noexcept {
  board_ir::StableHashBuilder hash;
  hash.AddString("APGAR-PHASE4-CORPUS-COUNTER-V1");
  hash.AddU64(seed);
  hash.AddU64(counter);
  return hash.Finish();
}

[[nodiscard]] Point64 Transform(Phase4SyntheticFamily family, Point64 point) noexcept {
  if (family == Phase4SyntheticFamily::kPinFieldCrossbar) {
    return Point64{.x = point.y, .y = -point.x};
  }
  return point;
}

struct SyntheticGeometry {
  CompilerProfile profile;
  std::vector<std::pair<Point64, Point64>> endpoints;
  std::vector<routing::EdgeResourceKey> contested_resources;
  bool active_region_bound_exceeded = false;
  bool has_bounds = false;
  AxisAlignedBox64 bounds{};
};

void ExtendBounds(SyntheticGeometry* geometry, Point64 point) noexcept {
  if (!geometry->has_bounds) {
    geometry->bounds = AxisAlignedBox64{.min = point, .max = point};
    geometry->has_bounds = true;
    return;
  }
  geometry->bounds.min.x = std::min(geometry->bounds.min.x, point.x);
  geometry->bounds.min.y = std::min(geometry->bounds.min.y, point.y);
  geometry->bounds.max.x = std::max(geometry->bounds.max.x, point.x);
  geometry->bounds.max.y = std::max(geometry->bounds.max.y, point.y);
}

void AddRegion(SyntheticGeometry* geometry, AxisAlignedBox64 bounds,
               std::uint64_t maximum_active_regions) {
  if (geometry->profile.active_regions.size() >= maximum_active_regions) {
    geometry->active_region_bound_exceeded = true;
    return;
  }
  geometry->profile.active_regions.push_back(ActiveRegion{.layer = 0, .bounds = bounds});
  ExtendBounds(geometry, bounds.min);
  ExtendBounds(geometry, bounds.max);
}

void AddPoint(SyntheticGeometry* geometry, Phase4SyntheticFamily family, Point64 point,
              std::uint64_t maximum_active_regions) {
  point = Transform(family, point);
  AddRegion(geometry, AxisAlignedBox64{.min = point, .max = point}, maximum_active_regions);
}

void AddOrthogonalSegment(SyntheticGeometry* geometry, Phase4SyntheticFamily family, Point64 start,
                          Point64 end, std::uint64_t maximum_active_regions) {
  start = Transform(family, start);
  end = Transform(family, end);
  const AxisAlignedBox64 bounds{
      .min = Point64{.x = std::min(start.x, end.x), .y = std::min(start.y, end.y)},
      .max = Point64{.x = std::max(start.x, end.x), .y = std::max(start.y, end.y)},
  };
  AddRegion(geometry, bounds, maximum_active_regions);
}

[[nodiscard]] std::optional<routing::EdgeResourceKey> ResourceBetween(Phase4SyntheticFamily family,
                                                                      Point64 start,
                                                                      Point64 end) noexcept {
  start = Transform(family, start);
  end = Transform(family, end);
  const std::int64_t dx = end.x - start.x;
  const std::int64_t dy = end.y - start.y;
  geometry_compiler::Direction direction;
  if (dx == 1 && dy == 0) {
    direction = geometry_compiler::Direction::kEast;
  } else if (dx == -1 && dy == 0) {
    direction = geometry_compiler::Direction::kWest;
  } else if (dx == 0 && dy == 1) {
    direction = geometry_compiler::Direction::kNorth;
  } else if (dx == 0 && dy == -1) {
    direction = geometry_compiler::Direction::kSouth;
  } else {
    return std::nullopt;
  }
  return routing::CanonicalPhysicalEdgeResource(
      0, geometry_compiler::LatticeIndex{.x = start.x, .y = start.y}, direction);
}

[[nodiscard]] std::uint32_t MaximumRequestedPoolSize(
    const Phase4CaseDescriptor& descriptor) noexcept {
  std::uint32_t maximum = 1;
  for (std::size_t index = 0; index < descriptor.requested_pool_size_count; ++index) {
    maximum = std::max(maximum, descriptor.requested_pool_sizes[index]);
  }
  return maximum;
}

void AddPortalMotif(const Phase4CaseDescriptor& descriptor, std::uint32_t motif_index,
                    bool has_constrained_net, bool flexible_reachable, board_ir::DbCoord origin_y,
                    std::uint32_t channel_count, SyntheticGeometry* geometry,
                    std::uint64_t maximum_active_regions) {
  const std::uint64_t variation = CounterValue(descriptor.deterministic_seed, motif_index);
  const board_ir::DbCoord spacing = 10 + static_cast<board_ir::DbCoord>(variation % 3U);
  constexpr board_ir::DbCoord left_backbone = -5;
  constexpr board_ir::DbCoord right_backbone = 6;
  const board_ir::DbCoord left_terminal =
      left_backbone - 4 - static_cast<board_ir::DbCoord>((variation >> 8U) % 4U);
  const board_ir::DbCoord right_terminal =
      right_backbone + 4 + static_cast<board_ir::DbCoord>((variation >> 16U) % 4U);
  const board_ir::DbCoord top = origin_y + spacing * (channel_count - 1U);

  const Point64 flexible_start{.x = left_terminal, .y = origin_y};
  const Point64 flexible_goal{.x = right_terminal, .y = origin_y};
  AddPoint(geometry, descriptor.family, flexible_start, maximum_active_regions);
  AddPoint(geometry, descriptor.family, flexible_goal, maximum_active_regions);
  if (flexible_reachable) {
    AddOrthogonalSegment(geometry, descriptor.family, flexible_start,
                         Point64{.x = left_backbone, .y = origin_y}, maximum_active_regions);
  }
  AddOrthogonalSegment(geometry, descriptor.family, Point64{.x = right_backbone, .y = origin_y},
                       flexible_goal, maximum_active_regions);
  AddOrthogonalSegment(geometry, descriptor.family, Point64{.x = left_backbone, .y = origin_y},
                       Point64{.x = left_backbone, .y = top}, maximum_active_regions);
  AddOrthogonalSegment(geometry, descriptor.family, Point64{.x = right_backbone, .y = origin_y},
                       Point64{.x = right_backbone, .y = top}, maximum_active_regions);

  const board_ir::DbCoord detour = 2 + static_cast<board_ir::DbCoord>((variation >> 24U) % 2U);
  for (std::uint32_t channel = 0; channel < channel_count; ++channel) {
    const board_ir::DbCoord y = origin_y + spacing * channel;
    if (descriptor.family != Phase4SyntheticFamily::kFragmentedMaze) {
      AddOrthogonalSegment(geometry, descriptor.family, Point64{.x = left_backbone, .y = y},
                           Point64{.x = right_backbone, .y = y}, maximum_active_regions);
      continue;
    }
    const std::array<Point64, 10> corners = {
        Point64{.x = left_backbone, .y = y},
        Point64{.x = -2, .y = y},
        Point64{.x = -2, .y = y + detour},
        Point64{.x = 0, .y = y + detour},
        Point64{.x = 0, .y = y},
        Point64{.x = 1, .y = y},
        Point64{.x = 2, .y = y},
        Point64{.x = 2, .y = y + detour},
        Point64{.x = 3, .y = y + detour},
        Point64{.x = 3, .y = y},
    };
    for (std::size_t index = 1; index < corners.size(); ++index) {
      AddOrthogonalSegment(geometry, descriptor.family, corners[index - 1], corners[index],
                           maximum_active_regions);
    }
    AddOrthogonalSegment(geometry, descriptor.family, corners.back(),
                         Point64{.x = right_backbone, .y = y}, maximum_active_regions);
  }

  geometry->endpoints.emplace_back(Transform(descriptor.family, flexible_start),
                                   Transform(descriptor.family, flexible_goal));
  if (!has_constrained_net) {
    return;
  }

  const Point64 constrained_start = descriptor.family == Phase4SyntheticFamily::kFragmentedMaze
                                        ? Point64{.x = -1, .y = origin_y}
                                        : Point64{.x = -3, .y = origin_y - 4};
  if (descriptor.family == Phase4SyntheticFamily::kFragmentedMaze) {
    AddOrthogonalSegment(geometry, descriptor.family, constrained_start,
                         Point64{.x = 0, .y = origin_y}, maximum_active_regions);
  } else {
    AddOrthogonalSegment(geometry, descriptor.family, constrained_start,
                         Point64{.x = 0, .y = origin_y - 4}, maximum_active_regions);
    AddOrthogonalSegment(geometry, descriptor.family, Point64{.x = 0, .y = origin_y - 4},
                         Point64{.x = 0, .y = origin_y}, maximum_active_regions);
  }
  Point64 constrained_goal;
  if (descriptor.family == Phase4SyntheticFamily::kFragmentedMaze) {
    constrained_goal = Point64{.x = 4, .y = origin_y - 4};
    AddOrthogonalSegment(geometry, descriptor.family, Point64{.x = 1, .y = origin_y},
                         Point64{.x = 1, .y = origin_y - 4}, maximum_active_regions);
    AddOrthogonalSegment(geometry, descriptor.family, Point64{.x = 1, .y = origin_y - 4},
                         constrained_goal, maximum_active_regions);
  } else {
    constrained_goal = Point64{.x = 3, .y = origin_y + 4};
    AddOrthogonalSegment(geometry, descriptor.family, Point64{.x = 1, .y = origin_y},
                         Point64{.x = 1, .y = origin_y + 4}, maximum_active_regions);
    AddOrthogonalSegment(geometry, descriptor.family, Point64{.x = 1, .y = origin_y + 4},
                         constrained_goal, maximum_active_regions);
  }
  geometry->endpoints.emplace_back(Transform(descriptor.family, constrained_start),
                                   Transform(descriptor.family, constrained_goal));
  const std::optional<routing::EdgeResourceKey> portal = ResourceBetween(
      descriptor.family, Point64{.x = 0, .y = origin_y}, Point64{.x = 1, .y = origin_y});
  if (portal.has_value()) {
    geometry->contested_resources.push_back(*portal);
  }
}

void AddPinFieldPortalLadder(const Phase4CaseDescriptor& descriptor, SyntheticGeometry* geometry,
                             std::uint64_t maximum_active_regions) {
  const std::uint64_t variation = CounterValue(descriptor.deterministic_seed, 0);
  const board_ir::DbCoord spacing = 20 + 2 * static_cast<board_ir::DbCoord>(variation % 2U);
  constexpr board_ir::DbCoord private_rail_x = -12;
  constexpr board_ir::DbCoord shared_rail_x = 3;
  const std::uint32_t motif_count = descriptor.requested_net_count / 2U;
  AddOrthogonalSegment(geometry, descriptor.family, Point64{.x = shared_rail_x, .y = 0},
                       Point64{.x = shared_rail_x, .y = spacing * motif_count},
                       maximum_active_regions);
  for (std::uint32_t portal_index = 0; portal_index <= motif_count; ++portal_index) {
    const board_ir::DbCoord portal_y = spacing * portal_index;
    AddOrthogonalSegment(geometry, descriptor.family, Point64{.x = 0, .y = portal_y},
                         Point64{.x = 1, .y = portal_y}, maximum_active_regions);
    AddOrthogonalSegment(geometry, descriptor.family, Point64{.x = 1, .y = portal_y},
                         Point64{.x = shared_rail_x, .y = portal_y}, maximum_active_regions);
    if (portal_index < motif_count) {
      if (const auto portal = ResourceBetween(descriptor.family, Point64{.x = 0, .y = portal_y},
                                              Point64{.x = 1, .y = portal_y});
          portal.has_value()) {
        geometry->contested_resources.push_back(*portal);
      }
    }
  }
  for (std::uint32_t motif = 0; motif < motif_count; ++motif) {
    const board_ir::DbCoord lower_y = spacing * motif;
    const board_ir::DbCoord upper_y = spacing * (motif + 1U);
    const Point64 flexible_start{.x = 0, .y = lower_y + 4};
    const Point64 flexible_goal{.x = 0, .y = upper_y - 4};
    AddOrthogonalSegment(geometry, descriptor.family, flexible_start, Point64{.x = 0, .y = lower_y},
                         maximum_active_regions);
    AddOrthogonalSegment(geometry, descriptor.family, Point64{.x = 0, .y = upper_y}, flexible_goal,
                         maximum_active_regions);
    AddOrthogonalSegment(geometry, descriptor.family, flexible_start,
                         Point64{.x = private_rail_x, .y = flexible_start.y},
                         maximum_active_regions);
    AddOrthogonalSegment(
        geometry, descriptor.family, Point64{.x = private_rail_x, .y = flexible_start.y},
        Point64{.x = private_rail_x, .y = flexible_goal.y}, maximum_active_regions);
    AddOrthogonalSegment(geometry, descriptor.family,
                         Point64{.x = private_rail_x, .y = flexible_goal.y}, flexible_goal,
                         maximum_active_regions);
    geometry->endpoints.emplace_back(Transform(descriptor.family, flexible_start),
                                     Transform(descriptor.family, flexible_goal));

    const Point64 constrained_start{.x = -4, .y = lower_y};
    const Point64 constrained_goal{.x = 1, .y = lower_y};
    AddOrthogonalSegment(geometry, descriptor.family, constrained_start,
                         Point64{.x = 0, .y = lower_y}, maximum_active_regions);
    geometry->endpoints.emplace_back(Transform(descriptor.family, constrained_start),
                                     Transform(descriptor.family, constrained_goal));
  }
}

[[nodiscard]] std::optional<SyntheticGeometry> BuildSyntheticGeometry(
    const Phase4CaseDescriptor& descriptor, const Phase4RepresentativeCorpusLimits& limits) {
  SyntheticGeometry geometry{
      .profile =
          CompilerProfile{
              .schema_version = geometry_compiler::kCompilerProfileSchemaVersion,
              .lattice_origin = Point64{.x = 0, .y = 0},
              .lattice_step = 1,
              .tile_width_nodes = 8,
              .tile_height_nodes = 8,
              .compilation_roi = {},
              .active_regions = {},
              .heading_mask = kOrthogonalHeadings,
              .costs =
                  DeterministicCosts{.orthogonal_step = 1'000, .diagonal_step = 1'414, .bend = 100},
          },
      .endpoints = {},
      .contested_resources = {},
      .active_region_bound_exceeded = false,
      .has_bounds = false,
      .bounds = {},
  };
  geometry.endpoints.reserve(descriptor.requested_net_count);
  geometry.contested_resources.reserve(descriptor.requested_net_count / 2U);
  const std::uint32_t channel_count = MaximumRequestedPoolSize(descriptor);
  if (descriptor.family == Phase4SyntheticFamily::kPinFieldCrossbar) {
    AddPinFieldPortalLadder(descriptor, &geometry, limits.maximum_active_regions);
    if (geometry.active_region_bound_exceeded || !geometry.has_bounds ||
        geometry.endpoints.size() != descriptor.requested_net_count) {
      return std::nullopt;
    }
    geometry.profile.compilation_roi = geometry.bounds;
    return geometry;
  }
  const std::uint32_t motif_count = (descriptor.requested_net_count + 1U) / 2U;
  const std::uint32_t unreachable_count =
      descriptor.requested_net_count - descriptor.declared_reachable_net_count;
  const std::uint32_t unreachable_offset =
      motif_count == 0 ? 0
                       : static_cast<std::uint32_t>(descriptor.deterministic_seed % motif_count);
  board_ir::DbCoord cursor = 0;
  for (std::uint32_t motif = 0; motif < motif_count; ++motif) {
    const bool has_constrained_net = motif * 2U + 1U < descriptor.requested_net_count;
    const bool unreachable =
        descriptor.family == Phase4SyntheticFamily::kFragmentedMaze && unreachable_count != 0 &&
        ((motif + motif_count - unreachable_offset) % motif_count) < unreachable_count;
    AddPortalMotif(descriptor, motif, has_constrained_net, !unreachable, cursor, channel_count,
                   &geometry, limits.maximum_active_regions);
    const std::uint64_t variation = CounterValue(descriptor.deterministic_seed, motif);
    const board_ir::DbCoord spacing = 10 + static_cast<board_ir::DbCoord>(variation % 3U);
    const board_ir::DbCoord detour_margin =
        descriptor.family == Phase4SyntheticFamily::kFragmentedMaze ? 4 : 0;
    cursor += spacing * (channel_count - 1U) + 16 + detour_margin;
  }
  if (geometry.active_region_bound_exceeded || !geometry.has_bounds ||
      geometry.endpoints.size() != descriptor.requested_net_count) {
    return std::nullopt;
  }
  geometry.profile.compilation_roi = geometry.bounds;
  return geometry;
}

[[nodiscard]] RoutingProfile ProfileFor(const Phase4CaseDescriptor& descriptor, EntityRef net,
                                        std::uint32_t net_index) noexcept {
  const std::uint64_t variation = CounterValue(descriptor.deterministic_seed, net_index);
  return RoutingProfile{
      .net = net,
      .nominal_width = 1 + static_cast<board_ir::DbCoord>(variation % 3U),
      .clearance = static_cast<board_ir::DbCoord>((variation >> 8U) % 2U),
      .allowed_layers = {0},
      .allowed_headings = kOrthogonalHeadings,
  };
}

[[nodiscard]] BoardData BuildSyntheticBoardData(const Phase4CaseDescriptor& descriptor,
                                                const SyntheticGeometry& geometry) {
  BoardData board{
      .schema_version = board_ir::kBoardSchemaVersion,
      .dbu_per_millimeter = 1'000'000,
      .revision = descriptor.deterministic_seed,
      .adapter_name = "phase4-representative-synthetic-corpus",
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
      .nets = {},
      .terminals = {},
      .obstacles = {},
      .routing_profile = {},
  };
  board.nets.reserve(descriptor.requested_net_count);
  board.terminals.reserve(static_cast<std::size_t>(descriptor.requested_net_count) * 2U);
  for (std::uint32_t index = 0; index < descriptor.requested_net_count; ++index) {
    const EntityRef net{.id = 1'000U + index, .generation = 0};
    const EntityRef start_terminal{.id = 1'000'000U + index * 2U, .generation = 0};
    const EntityRef goal_terminal{.id = start_terminal.id + 1U, .generation = 0};
    const auto [start, goal] = geometry.endpoints[index];
    board.nets.push_back(Net{
        .ref = net,
        .name = "SYNTH_NET_" + std::to_string(index),
        .terminals = {start_terminal, goal_terminal},
    });
    board.terminals.push_back(Terminal{
        .ref = start_terminal,
        .net = net,
        .component = "SYNTH_START_" + std::to_string(index),
        .pin = "1",
        .center = start,
        .connection_region = AxisAlignedBox64{.min = start, .max = start},
        .layers = {0},
    });
    board.terminals.push_back(Terminal{
        .ref = goal_terminal,
        .net = net,
        .component = "SYNTH_GOAL_" + std::to_string(index),
        .pin = "1",
        .center = goal,
        .connection_region = AxisAlignedBox64{.min = goal, .max = goal},
        .layers = {0},
    });
  }
  board.routing_profile = ProfileFor(descriptor, board.nets.front().ref, 0);
  return board;
}

[[nodiscard]] std::uint64_t ComputeCaseChecksum(
    const Phase4CaseDescriptor& descriptor, const BoardSnapshot& board,
    const allocator::MultiNetWorkload& workload, const allocator::ResourceCapacityModel& capacities,
    std::span<const routing::EdgeResourceKey> contested_resources) noexcept {
  board_ir::StableHashBuilder hash;
  hash.AddString("APGAR-PHASE4-REPRESENTATIVE-CASE-V1");
  hash.AddU64(FingerprintPhase4CaseDescriptorV1(descriptor));
  hash.AddU64(board.content_hash());
  hash.AddU64(workload.workload_checksum());
  hash.AddU64(workload.compiled_node_count());
  hash.AddU64(workload.compiled_host_bytes());
  hash.AddU32(capacities.schema_version());
  hash.AddU64(capacities.associations().board_content_hash);
  hash.AddU64(capacities.associations().compiler_profile_fingerprint);
  hash.AddU32(capacities.associations().geometry_compiler_version);
  hash.AddU32(capacities.default_capacity_units());
  hash.AddU64(static_cast<std::uint64_t>(capacities.overrides().size()));
  for (const allocator::ResourceCapacityOverride& capacity : capacities.overrides()) {
    hash.AddU32(capacity.resource.layer);
    hash.AddI64(capacity.resource.lattice_x);
    hash.AddI64(capacity.resource.lattice_y);
    hash.AddByte(static_cast<std::uint8_t>(capacity.resource.direction));
    hash.AddU32(capacity.capacity_units);
  }
  hash.AddU64(static_cast<std::uint64_t>(contested_resources.size()));
  for (const routing::EdgeResourceKey& resource : contested_resources) {
    hash.AddU32(resource.layer);
    hash.AddI64(resource.lattice_x);
    hash.AddI64(resource.lattice_y);
    hash.AddByte(static_cast<std::uint8_t>(resource.direction));
  }
  return hash.Finish();
}

[[nodiscard]] std::variant<allocator::ResourceCapacityModel, Phase4RepresentativeCorpusError>
BuildCapacities(std::uint32_t case_id, const BoardSnapshot& board,
                const allocator::MultiNetWorkload& workload) {
  if (workload.nets().empty()) {
    return Error(Phase4RepresentativeCorpusErrorCode::kInternalInvariant,
                 "benchmark.phase4_representative.empty_workload.v1",
                 "Representative case produced an empty prepared workload", case_id);
  }
  allocator::ResourceCapacityModelResult result =
      allocator::BuildResourceCapacityModel(allocator::kResourceCapacityModelSchemaVersion, board,
                                            workload.nets().front().compiled_board, 1, {});
  if (std::holds_alternative<allocator::AllocationError>(result)) {
    const allocator::AllocationError& capacity_error = std::get<allocator::AllocationError>(result);
    const bool resource_error =
        capacity_error.code == allocator::AllocationErrorCode::kResourceExhausted;
    const std::string_view invariant_id =
        capacity_error.invariant_id.empty()
            ? (resource_error ? "benchmark.phase4_representative.capacity_resource.v1"
                              : "benchmark.phase4_representative.capacity.v1")
            : capacity_error.invariant_id;
    return Error(resource_error ? Phase4RepresentativeCorpusErrorCode::kResourceExhausted
                                : Phase4RepresentativeCorpusErrorCode::kCapacityBuildFailed,
                 invariant_id,
                 resource_error ? "Capacity construction exhausted a bounded resource"
                                : "Representative case capacity construction failed",
                 case_id);
  }
  return std::get<allocator::ResourceCapacityModel>(std::move(result));
}

[[nodiscard]] bool LimitsAreValid(const Phase4RepresentativeCorpusLimits& limits) noexcept {
  return limits.maximum_nets != 0 && limits.maximum_nets <= kMaximumPhase4RepresentativeNetsV1 &&
         limits.maximum_compiled_nodes != 0 &&
         limits.maximum_compiled_nodes <= allocator::kMaximumMultiNetWorkloadCompiledNodesV1 &&
         limits.maximum_compiled_host_bytes != 0 &&
         limits.maximum_compiled_host_bytes <=
             allocator::kMaximumMultiNetWorkloadCompiledHostBytesV1 &&
         limits.maximum_active_regions != 0 &&
         limits.maximum_active_regions <= kMaximumActiveRegionsV1 &&
         limits.maximum_board_entities != 0 &&
         limits.maximum_board_entities <= kMaximumBoardEntitiesV1;
}

[[nodiscard]] std::optional<Phase4RepresentativeCorpusError> PreparedWorkBoundError(
    std::uint32_t case_id, const allocator::MultiNetWorkload& workload,
    const Phase4RepresentativeCorpusLimits& limits) noexcept {
  const bool nodes_exceeded = workload.compiled_node_count() > limits.maximum_compiled_nodes;
  const bool host_exceeded = workload.compiled_host_bytes() > limits.maximum_compiled_host_bytes;
  if (!nodes_exceeded && !host_exceeded) {
    return std::nullopt;
  }
  std::uint64_t accumulated_nodes = 0;
  std::uint64_t accumulated_host_bytes = 0;
  std::size_t preparable = 0;
  for (const allocator::PreparedNetRoutingContext& context : workload.nets()) {
    const std::uint64_t nodes = context.compiled_board.telemetry().represented_nodes;
    const std::uint64_t host_bytes = context.compiled_board.telemetry().estimated_host_bytes;
    if (nodes > limits.maximum_compiled_nodes - accumulated_nodes ||
        host_bytes > limits.maximum_compiled_host_bytes - accumulated_host_bytes) {
      break;
    }
    accumulated_nodes += nodes;
    accumulated_host_bytes += host_bytes;
    ++preparable;
  }
  const allocator::PreparedNetRoutingContext& first_unpreparable = workload.nets()[preparable];
  const bool nodes_stop = first_unpreparable.compiled_board.telemetry().represented_nodes >
                          limits.maximum_compiled_nodes - accumulated_nodes;
  const bool host_stop = first_unpreparable.compiled_board.telemetry().estimated_host_bytes >
                         limits.maximum_compiled_host_bytes - accumulated_host_bytes;
  return WorkBoundError(case_id, LimitingWorkBound(nodes_stop, host_stop), preparable,
                        first_unpreparable.request.net, workload.compiled_node_count(),
                        limits.maximum_compiled_nodes, workload.compiled_host_bytes(),
                        limits.maximum_compiled_host_bytes);
}

[[nodiscard]] std::optional<Phase4RepresentativeCorpusError> UniformWorkBoundError(
    std::uint32_t case_id, const Phase4CaseDescriptor& descriptor,
    const board_ir::BoardSnapshot& board, const geometry_compiler::CompiledBoard& compiled,
    const Phase4RepresentativeCorpusLimits& limits) noexcept {
  const std::uint64_t nodes_per_net = compiled.telemetry().represented_nodes;
  const std::uint64_t host_bytes_per_net = compiled.telemetry().estimated_host_bytes;
  const __uint128_t required_nodes_wide =
      static_cast<__uint128_t>(nodes_per_net) * descriptor.requested_net_count;
  const __uint128_t required_host_bytes_wide =
      static_cast<__uint128_t>(host_bytes_per_net) * descriptor.requested_net_count;
  if (required_nodes_wide <= limits.maximum_compiled_nodes &&
      required_host_bytes_wide <= limits.maximum_compiled_host_bytes) {
    return std::nullopt;
  }
  const std::uint64_t required_nodes = static_cast<std::uint64_t>(required_nodes_wide);
  const std::uint64_t required_host_bytes = static_cast<std::uint64_t>(required_host_bytes_wide);
  const bool nodes_exceeded = required_nodes > limits.maximum_compiled_nodes;
  const bool host_exceeded = required_host_bytes > limits.maximum_compiled_host_bytes;
  const std::uint64_t node_count = nodes_exceeded ? limits.maximum_compiled_nodes / nodes_per_net
                                                  : descriptor.requested_net_count;
  const std::uint64_t host_count = host_exceeded
                                       ? limits.maximum_compiled_host_bytes / host_bytes_per_net
                                       : descriptor.requested_net_count;
  const std::uint64_t preparable =
      std::min<std::uint64_t>({descriptor.requested_net_count, node_count, host_count});
  const bool nodes_stop = nodes_exceeded && node_count == preparable;
  const bool host_stop = host_exceeded && host_count == preparable;
  return WorkBoundError(case_id, LimitingWorkBound(nodes_stop, host_stop), preparable,
                        board.data().nets[preparable].ref, required_nodes,
                        limits.maximum_compiled_nodes, required_host_bytes,
                        limits.maximum_compiled_host_bytes);
}

}  // namespace

void internal::SetPhase4RepresentativeCorpusFaultForTesting(
    Phase4RepresentativeCorpusFaultForTesting fault) noexcept {
#if defined(APGAR_PHASE4_REPRESENTATIVE_CORPUS_FAULT_TEST_VARIANT)
  g_phase4_representative_corpus_fault_for_testing = fault;
#else
  (void)fault;
#endif
}

std::span<const Phase4CaseDescriptor> Phase4CaseDescriptorsV1() noexcept { return kDescriptorsV1; }

const Phase4CaseDescriptor* FindPhase4CaseDescriptorV1(std::uint32_t case_id) noexcept {
  const auto found = std::ranges::find(kDescriptorsV1, case_id, &Phase4CaseDescriptor::case_id);
  return found == kDescriptorsV1.end() ? nullptr : &*found;
}

std::uint64_t FingerprintPhase4CaseDescriptorV1(const Phase4CaseDescriptor& descriptor) noexcept {
  board_ir::StableHashBuilder hash;
  hash.AddString("APGAR-PHASE4-CASE-DESCRIPTOR-V1");
  hash.AddU32(kPhase4RepresentativeCorpusVersion);
  hash.AddU32(descriptor.case_id);
  hash.AddByte(static_cast<std::uint8_t>(descriptor.source));
  hash.AddByte(static_cast<std::uint8_t>(descriptor.family));
  hash.AddByte(static_cast<std::uint8_t>(descriptor.role));
  hash.AddU64(descriptor.deterministic_seed);
  hash.AddU32(descriptor.requested_net_count);
  hash.AddU32(descriptor.declared_reachable_net_count);
  hash.AddByte(descriptor.requested_pool_size_count);
  for (std::uint32_t pool_size : descriptor.requested_pool_sizes) {
    hash.AddU32(pool_size);
  }
  hash.AddU32(descriptor.features);
  hash.AddU64(descriptor.maximum_exact_candidate_products);
  hash.AddU32(descriptor.declared_stress_target_nets);
  hash.AddBool(descriptor.known_unmapped_exact_conflicts);
  hash.AddBool(descriptor.globally_coupled_conflict_graph);
  return hash.Finish();
}

std::uint64_t Phase4RepresentativeCorpusChecksumV1() noexcept {
  board_ir::StableHashBuilder hash;
  hash.AddString("APGAR-PHASE4-REPRESENTATIVE-CORPUS-V1");
  hash.AddU32(kPhase4RepresentativeCorpusVersion);
  hash.AddU64(static_cast<std::uint64_t>(kDescriptorsV1.size()));
  for (const Phase4CaseDescriptor& descriptor : kDescriptorsV1) {
    hash.AddU64(FingerprintPhase4CaseDescriptorV1(descriptor));
  }
  hash.AddU64(kPhase4ImportedMultiNetFixtureBytesV1);
  hash.AddU64(kPhase4ImportedMultiNetFixtureFnv1a64V1);
  return hash.Finish();
}

Phase4RepresentativeCaseResult BuildPhase4RepresentativeCaseV1(
    std::uint32_t case_id, std::string_view imported_fixture,
    const Phase4RepresentativeCorpusLimits& limits) {
  const Phase4CaseDescriptor* descriptor = FindPhase4CaseDescriptorV1(case_id);
  if (descriptor == nullptr) {
    return Error(Phase4RepresentativeCorpusErrorCode::kUnknownCase,
                 "benchmark.phase4_representative.case_id.v1",
                 "Representative corpus case ID is unknown", case_id);
  }
  if (!LimitsAreValid(limits)) {
    return Error(Phase4RepresentativeCorpusErrorCode::kInvalidLimits,
                 "benchmark.phase4_representative.limits.v1",
                 "Representative corpus limits are invalid", case_id);
  }
  const __uint128_t entity_count =
      descriptor->source == Phase4CaseSource::kImportedFixture
          ? 18U
          : static_cast<__uint128_t>(descriptor->requested_net_count) * 3U + 2U;
  if (descriptor->requested_net_count > limits.maximum_nets ||
      entity_count > limits.maximum_board_entities) {
    return Error(Phase4RepresentativeCorpusErrorCode::kInputBoundExceeded,
                 "benchmark.phase4_representative.input_bound.v1",
                 "Representative case exceeds the caller's net or Board-entity bound", case_id);
  }

  try {
    MaybeThrowRepresentativeCorpusFaultForTesting();
    if (descriptor->source == Phase4CaseSource::kImportedFixture) {
      if (imported_fixture.empty()) {
        return Error(Phase4RepresentativeCorpusErrorCode::kImportedFixtureRequired,
                     "benchmark.phase4_representative.imported_fixture.v1",
                     "Imported guardrail case requires the authenticated fixture bytes", case_id);
      }
      Phase4ImportedMultiNetCorpusResult imported_result =
          BuildPhase4ImportedMultiNetCorpusV1(imported_fixture);
      if (!std::holds_alternative<Phase4ImportedMultiNetCorpus>(imported_result)) {
        const Phase4CorpusError& imported_error = std::get<Phase4CorpusError>(imported_result);
        const std::string_view invariant_id =
            imported_error.invariant_id.empty()
                ? (imported_error.code == Phase4CorpusErrorCode::kResourceExhausted
                       ? "benchmark.phase4_representative.imported_resource.v1"
                       : "benchmark.phase4_representative.imported.v1")
                : imported_error.invariant_id;
        if (imported_error.code == Phase4CorpusErrorCode::kResourceExhausted) {
          return Error(Phase4RepresentativeCorpusErrorCode::kResourceExhausted, invariant_id,
                       "Imported guardrail case exhausted a bounded resource", case_id);
        }
        return Error(Phase4RepresentativeCorpusErrorCode::kImportedCorpusFailed, invariant_id,
                     "Imported guardrail corpus construction failed", case_id);
      }
      Phase4ImportedMultiNetCorpus imported =
          std::get<Phase4ImportedMultiNetCorpus>(std::move(imported_result));
      if (const auto work_bound = PreparedWorkBoundError(case_id, imported.workload, limits);
          work_bound.has_value()) {
        return *work_bound;
      }
      auto capacity_result = BuildCapacities(case_id, imported.board, imported.workload);
      if (std::holds_alternative<Phase4RepresentativeCorpusError>(capacity_result)) {
        return std::get<Phase4RepresentativeCorpusError>(capacity_result);
      }
      allocator::ResourceCapacityModel capacities =
          std::get<allocator::ResourceCapacityModel>(std::move(capacity_result));
      const std::uint64_t checksum =
          ComputeCaseChecksum(*descriptor, imported.board, imported.workload, capacities, {});
      return Phase4RepresentativeCase{
          .descriptor = *descriptor,
          .board = std::move(imported.board),
          .workload = std::move(imported.workload),
          .capacities = std::move(capacities),
          .declared_contested_resources = {},
          .case_checksum = checksum,
      };
    }

    std::optional<SyntheticGeometry> geometry = BuildSyntheticGeometry(*descriptor, limits);
    if (!geometry.has_value()) {
      return Error(Phase4RepresentativeCorpusErrorCode::kInputBoundExceeded,
                   "benchmark.phase4_representative.active_region_bound.v1",
                   "Synthetic case exceeds its active-region bound", case_id);
    }
    BoardData board_data = BuildSyntheticBoardData(*descriptor, *geometry);
    board_ir::BoardCreationResult board_result =
        board_ir::CreateBoardSnapshot(std::move(board_data));
    if (!std::holds_alternative<BoardSnapshot>(board_result)) {
      return Error(Phase4RepresentativeCorpusErrorCode::kBoardBuildFailed,
                   "benchmark.phase4_representative.board.v1",
                   "Synthetic representative Board IR construction failed", case_id);
    }
    BoardSnapshot board = std::get<BoardSnapshot>(std::move(board_result));
    std::vector<MultiNetRoutingSpec> specs;
    specs.reserve(descriptor->requested_net_count);
    for (std::uint32_t index = 0; index < descriptor->requested_net_count; ++index) {
      specs.push_back(MultiNetRoutingSpec{
          .routing_profile = ProfileFor(*descriptor, board.data().nets[index].ref, index),
          .start_layer = 0,
          .goal_layer = 0,
      });
    }
    std::uint64_t expected_compiled_nodes = 0;
    std::uint64_t expected_compiled_host_bytes = 0;
    {
      geometry_compiler::CompileResult probe_result =
          geometry_compiler::CompileBoard(board, geometry->profile, specs.front().routing_profile);
      if (!std::holds_alternative<geometry_compiler::CompiledBoard>(probe_result)) {
        return Error(Phase4RepresentativeCorpusErrorCode::kWorkloadBuildFailed,
                     "benchmark.phase4_representative.work_probe.v1",
                     "Synthetic compiled-work preflight could not compile its first net", case_id);
      }
      const geometry_compiler::CompiledBoard& probe =
          std::get<geometry_compiler::CompiledBoard>(probe_result);
      expected_compiled_nodes =
          probe.telemetry().represented_nodes * descriptor->requested_net_count;
      expected_compiled_host_bytes =
          probe.telemetry().estimated_host_bytes * descriptor->requested_net_count;
      if (const auto work_bound = UniformWorkBoundError(case_id, *descriptor, board, probe, limits);
          work_bound.has_value()) {
        return *work_bound;
      }
    }
    allocator::MultiNetWorkloadResult workload_result = allocator::BuildMultiNetWorkload(
        allocator::kMultiNetWorkloadSchemaVersion, board, geometry->profile, specs,
        allocator::MultiNetWorkloadLimits{
            .maximum_nets = limits.maximum_nets,
            .maximum_compiled_nodes = limits.maximum_compiled_nodes,
            .maximum_compiled_host_bytes = limits.maximum_compiled_host_bytes,
        });
    if (!std::holds_alternative<allocator::MultiNetWorkload>(workload_result)) {
      const allocator::MultiNetWorkloadError& workload_error =
          std::get<allocator::MultiNetWorkloadError>(workload_result);
      const bool resource_error =
          workload_error.code == allocator::MultiNetWorkloadErrorCode::kResourceExhausted;
      const bool unexpected_work_bound =
          workload_error.code == allocator::MultiNetWorkloadErrorCode::kWorkBoundExceeded;
      const std::string_view invariant_id =
          workload_error.invariant_id.empty()
              ? (resource_error ? "benchmark.phase4_representative.workload_resource.v1"
                 : unexpected_work_bound
                     ? "benchmark.phase4_representative.workload_preflight_drift.v1"
                     : "benchmark.phase4_representative.workload.v1")
              : workload_error.invariant_id;
      return Error(
          resource_error          ? Phase4RepresentativeCorpusErrorCode::kResourceExhausted
          : unexpected_work_bound ? Phase4RepresentativeCorpusErrorCode::kInternalInvariant
                                  : Phase4RepresentativeCorpusErrorCode::kWorkloadBuildFailed,
          invariant_id,
          resource_error          ? "Synthetic workload exhausted a bounded resource"
          : unexpected_work_bound ? "Synthetic workload disagreed with compiled-work preflight"
                                  : "Synthetic workload preparation failed",
          case_id);
    }
    allocator::MultiNetWorkload workload =
        std::get<allocator::MultiNetWorkload>(std::move(workload_result));
    if (workload.compiled_node_count() != expected_compiled_nodes ||
        workload.compiled_host_bytes() != expected_compiled_host_bytes) {
      return Error(Phase4RepresentativeCorpusErrorCode::kInternalInvariant,
                   "benchmark.phase4_representative.uniform_compiled_work.v1",
                   "Synthetic per-net compiled-work accounting is not uniform", case_id);
    }
    for (const routing::EdgeResourceKey& resource : geometry->contested_resources) {
      if (!routing::ResourceExists(workload.nets().front().compiled_board, resource)) {
        return Error(Phase4RepresentativeCorpusErrorCode::kInternalInvariant,
                     "benchmark.phase4_representative.contested_resource.v1",
                     "Declared contested resource is absent from the compiled lattice", case_id);
      }
    }
    auto capacity_result = BuildCapacities(case_id, board, workload);
    if (std::holds_alternative<Phase4RepresentativeCorpusError>(capacity_result)) {
      return std::get<Phase4RepresentativeCorpusError>(capacity_result);
    }
    allocator::ResourceCapacityModel capacities =
        std::get<allocator::ResourceCapacityModel>(std::move(capacity_result));
    const std::uint64_t checksum = ComputeCaseChecksum(*descriptor, board, workload, capacities,
                                                       geometry->contested_resources);
    return Phase4RepresentativeCase{
        .descriptor = *descriptor,
        .board = std::move(board),
        .workload = std::move(workload),
        .capacities = std::move(capacities),
        .declared_contested_resources = std::move(geometry->contested_resources),
        .case_checksum = checksum,
    };
  } catch (const std::bad_alloc&) {
    return Error(Phase4RepresentativeCorpusErrorCode::kResourceExhausted,
                 "benchmark.phase4_representative.host_memory.v1",
                 "Representative corpus host allocation failed", case_id);
  } catch (const std::length_error&) {
    return Error(Phase4RepresentativeCorpusErrorCode::kResourceExhausted,
                 "benchmark.phase4_representative.host_container.v1",
                 "Representative corpus host container bound was exhausted", case_id);
  }
}

}  // namespace apgar::benchmark
