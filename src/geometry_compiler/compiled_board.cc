#include "apgar/geometry_compiler/compiled_board.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <ranges>
#include <string>
#include <tuple>
#include <utility>

#include "apgar/board_ir/stable_hash.h"
#include "src/geometry/exact_internal.h"

namespace apgar::geometry_compiler {
namespace {

using Wide = __int128_t;
using UWide = __uint128_t;

struct LatticeBounds {
  std::int64_t min_x;
  std::int64_t max_x;
  std::int64_t min_y;
  std::int64_t max_y;
};

using MutableTiles = std::map<TileKey, std::map<std::uint64_t, DirectionMask>>;

[[nodiscard]] CompileError Error(CompileErrorCode code, std::string detail) {
  return CompileError{.code = code, .detail = std::move(detail)};
}

[[nodiscard]] std::int64_t FloorDivide(std::int64_t value, std::uint32_t divisor) noexcept {
  const std::int64_t signed_divisor = static_cast<std::int64_t>(divisor);
  std::int64_t quotient = value / signed_divisor;
  if (value % signed_divisor < 0) {
    --quotient;
  }
  return quotient;
}

[[nodiscard]] std::uint32_t PositiveRemainder(std::int64_t value, std::uint32_t divisor) noexcept {
  const std::int64_t signed_divisor = static_cast<std::int64_t>(divisor);
  std::int64_t remainder = value % signed_divisor;
  if (remainder < 0) {
    remainder += signed_divisor;
  }
  return static_cast<std::uint32_t>(remainder);
}

[[nodiscard]] Wide FloorDivide(Wide numerator, Wide denominator) noexcept {
  Wide quotient = numerator / denominator;
  if (numerator % denominator < 0) {
    --quotient;
  }
  return quotient;
}

[[nodiscard]] Wide CeilDivide(Wide numerator, Wide denominator) noexcept {
  return -FloorDivide(-numerator, denominator);
}

[[nodiscard]] std::optional<LatticeBounds> BoundsForRegion(const CompilerProfile& profile,
                                                           const board_ir::AxisAlignedBox64& box) {
  const Wide step = static_cast<Wide>(profile.lattice_step);
  const Wide min_x = CeilDivide(static_cast<Wide>(box.min.x) - profile.lattice_origin.x, step);
  const Wide max_x = FloorDivide(static_cast<Wide>(box.max.x) - profile.lattice_origin.x, step);
  const Wide min_y = CeilDivide(static_cast<Wide>(box.min.y) - profile.lattice_origin.y, step);
  const Wide max_y = FloorDivide(static_cast<Wide>(box.max.y) - profile.lattice_origin.y, step);
  if (min_x > max_x || min_y > max_y) {
    return std::nullopt;
  }
  constexpr Wide kIndexMinimum = std::numeric_limits<std::int64_t>::min();
  constexpr Wide kIndexMaximum = std::numeric_limits<std::int64_t>::max();
  if (min_x < kIndexMinimum || max_x > kIndexMaximum || min_y < kIndexMinimum ||
      max_y > kIndexMaximum) {
    return std::nullopt;
  }
  return LatticeBounds{
      .min_x = static_cast<std::int64_t>(min_x),
      .max_x = static_cast<std::int64_t>(max_x),
      .min_y = static_cast<std::int64_t>(min_y),
      .max_y = static_cast<std::int64_t>(max_y),
  };
}

[[nodiscard]] DirectionMask* FindMutableNode(MutableTiles& tiles, const CompilerProfile& profile,
                                             board_ir::LayerId layer, std::int64_t lattice_x,
                                             std::int64_t lattice_y) {
  const LatticeIndex index{.x = lattice_x, .y = lattice_y};
  const auto tile = tiles.find(TileForLatticeIndex(profile, layer, index));
  if (tile == tiles.end()) {
    return nullptr;
  }
  const auto node = tile->second.find(LocalNodeIndex(profile, index));
  return node == tile->second.end() ? nullptr : &node->second;
}

[[nodiscard]] bool RegionLess(const ActiveRegion& left, const ActiveRegion& right) noexcept {
  return std::tie(left.layer, left.bounds.min.x, left.bounds.min.y, left.bounds.max.x,
                  left.bounds.max.y) < std::tie(right.layer, right.bounds.min.x, right.bounds.min.y,
                                                right.bounds.max.x, right.bounds.max.y);
}

void Normalize(CompilerProfile& profile) {
  std::ranges::sort(profile.active_regions, RegionLess);
  profile.active_regions.erase(std::ranges::unique(profile.active_regions).begin(),
                               profile.active_regions.end());
}

[[nodiscard]] std::optional<CompileError> ValidateProfile(const CompilerProfile& profile,
                                                          const board_ir::RoutingProfile& routing) {
  if (profile.schema_version != kCompilerProfileSchemaVersion) {
    return Error(CompileErrorCode::kInvalidProfile,
                 "Compiler profile schema version is not supported");
  }
  if (!board_ir::PointIsValid(profile.lattice_origin) || profile.lattice_step <= 0 ||
      profile.lattice_step > board_ir::kMaxAbsDbCoord) {
    return Error(CompileErrorCode::kInvalidProfile,
                 "Lattice origin and positive step must fit the exact coordinate envelope");
  }
  constexpr std::uint32_t kMaximumTileDimension = 4096;
  if (profile.tile_width_nodes == 0 || profile.tile_height_nodes == 0 ||
      profile.tile_width_nodes > kMaximumTileDimension ||
      profile.tile_height_nodes > kMaximumTileDimension) {
    return Error(CompileErrorCode::kInvalidProfile,
                 "Tile dimensions must each be in the inclusive range [1, 4096]");
  }
  if (!board_ir::BoxIsValid(profile.compilation_roi)) {
    return Error(CompileErrorCode::kInvalidProfile,
                 "Compilation ROI must be a normalized in-range box");
  }
  if (profile.active_regions.empty()) {
    return Error(CompileErrorCode::kInvalidProfile,
                 "At least one layer-scoped active region is required");
  }
  if (profile.heading_mask == 0) {
    return Error(CompileErrorCode::kInvalidProfile,
                 "Compiler profile heading mask must be non-empty");
  }
  if ((profile.heading_mask & static_cast<board_ir::HeadingMask>(~board_ir::kM1HeadingMask)) != 0) {
    return Error(CompileErrorCode::kUnsupported,
                 "Compiler profile heading mask contains headings outside the M1 H/V/45 set");
  }
  if ((profile.heading_mask & static_cast<board_ir::HeadingMask>(~routing.allowed_headings)) != 0) {
    return Error(CompileErrorCode::kInvalidProfile,
                 "Compiler headings are not allowed by the Board IR routing profile");
  }
  if (profile.costs.orthogonal_step == 0 || profile.costs.diagonal_step == 0) {
    return Error(CompileErrorCode::kInvalidProfile,
                 "Deterministic movement costs must be positive integers");
  }

  for (const ActiveRegion& region : profile.active_regions) {
    if (!board_ir::BoxIsValid(region.bounds)) {
      return Error(CompileErrorCode::kInvalidProfile,
                   "Every active region must be a normalized in-range box");
    }
    if (region.bounds.min.x < profile.compilation_roi.min.x ||
        region.bounds.min.y < profile.compilation_roi.min.y ||
        region.bounds.max.x > profile.compilation_roi.max.x ||
        region.bounds.max.y > profile.compilation_roi.max.y) {
      return Error(CompileErrorCode::kInvalidProfile,
                   "Every active region must be contained by the compilation ROI");
    }
    if (!std::ranges::binary_search(routing.allowed_layers, region.layer)) {
      return Error(CompileErrorCode::kInvalidProfile,
                   "Active-region layers must belong to the routing rule bucket");
    }
    if (!BoundsForRegion(profile, region.bounds).has_value()) {
      return Error(CompileErrorCode::kInvalidProfile,
                   "Every active region must contain at least one representable lattice node");
    }
  }
  return std::nullopt;
}

[[nodiscard]] std::uint64_t EstimateHostBytes(const CompiledBoard& board) noexcept {
  UWide bytes = sizeof(CompiledBoard);
  bytes += static_cast<UWide>(board.profile().active_regions.size()) * sizeof(ActiveRegion);
  bytes +=
      static_cast<UWide>(board.rule_bucket().allowed_layers.size()) * sizeof(board_ir::LayerId);
  bytes +=
      static_cast<UWide>(board.routing_profile().allowed_layers.size()) * sizeof(board_ir::LayerId);
  bytes += static_cast<UWide>(board.tiles().size()) * sizeof(SparseTile);
  for (const SparseTile& tile : board.tiles()) {
    bytes += static_cast<UWide>(tile.nodes.size()) * sizeof(CompiledNode);
  }
  return static_cast<std::uint64_t>(bytes);
}

}  // namespace

std::optional<LatticeIndex> ExactPointToLatticeIndex(const CompilerProfile& profile,
                                                     board_ir::Point64 point) noexcept {
  if (profile.lattice_step <= 0) {
    return std::nullopt;
  }
  const Wide delta_x = static_cast<Wide>(point.x) - profile.lattice_origin.x;
  const Wide delta_y = static_cast<Wide>(point.y) - profile.lattice_origin.y;
  const Wide step = profile.lattice_step;
  if (delta_x % step != 0 || delta_y % step != 0) {
    return std::nullopt;
  }
  const Wide x = delta_x / step;
  const Wide y = delta_y / step;
  constexpr Wide kMinimum = std::numeric_limits<std::int64_t>::min();
  constexpr Wide kMaximum = std::numeric_limits<std::int64_t>::max();
  if (x < kMinimum || x > kMaximum || y < kMinimum || y > kMaximum) {
    return std::nullopt;
  }
  return LatticeIndex{.x = static_cast<std::int64_t>(x), .y = static_cast<std::int64_t>(y)};
}

std::optional<board_ir::Point64> LatticeIndexToExactPoint(const CompilerProfile& profile,
                                                          LatticeIndex index) noexcept {
  if (profile.lattice_step <= 0) {
    return std::nullopt;
  }
  const Wide x = static_cast<Wide>(profile.lattice_origin.x) +
                 static_cast<Wide>(index.x) * profile.lattice_step;
  const Wide y = static_cast<Wide>(profile.lattice_origin.y) +
                 static_cast<Wide>(index.y) * profile.lattice_step;
  if (x < -static_cast<Wide>(board_ir::kMaxAbsDbCoord) ||
      x > static_cast<Wide>(board_ir::kMaxAbsDbCoord) ||
      y < -static_cast<Wide>(board_ir::kMaxAbsDbCoord) ||
      y > static_cast<Wide>(board_ir::kMaxAbsDbCoord)) {
    return std::nullopt;
  }
  return board_ir::Point64{
      .x = static_cast<board_ir::DbCoord>(x),
      .y = static_cast<board_ir::DbCoord>(y),
  };
}

TileKey TileForLatticeIndex(const CompilerProfile& profile, board_ir::LayerId layer,
                            LatticeIndex index) noexcept {
  return TileKey{
      .layer = layer,
      .coordinate =
          TileCoordinate{
              .x = FloorDivide(index.x, profile.tile_width_nodes),
              .y = FloorDivide(index.y, profile.tile_height_nodes),
          },
  };
}

std::uint64_t LocalNodeIndex(const CompilerProfile& profile, LatticeIndex index) noexcept {
  const std::uint64_t local_x = PositiveRemainder(index.x, profile.tile_width_nodes);
  const std::uint64_t local_y = PositiveRemainder(index.y, profile.tile_height_nodes);
  return local_y * profile.tile_width_nodes + local_x;
}

LatticeIndex GlobalLatticeIndex(const CompilerProfile& profile, const TileKey& key,
                                std::uint64_t local_index) noexcept {
  const std::int64_t local_x = static_cast<std::int64_t>(local_index % profile.tile_width_nodes);
  const std::int64_t local_y = static_cast<std::int64_t>(local_index / profile.tile_width_nodes);
  return LatticeIndex{
      .x = key.coordinate.x * static_cast<std::int64_t>(profile.tile_width_nodes) + local_x,
      .y = key.coordinate.y * static_cast<std::int64_t>(profile.tile_height_nodes) + local_y,
  };
}

std::uint64_t FingerprintCompilerProfile(CompilerProfile profile) {
  Normalize(profile);
  board_ir::StableHashBuilder hash;
  hash.AddString("APGAR-COMPILER-PROFILE-V1");
  hash.AddU32(profile.schema_version);
  hash.AddI64(profile.lattice_origin.x);
  hash.AddI64(profile.lattice_origin.y);
  hash.AddI64(profile.lattice_step);
  hash.AddU32(profile.tile_width_nodes);
  hash.AddU32(profile.tile_height_nodes);
  hash.AddI64(profile.compilation_roi.min.x);
  hash.AddI64(profile.compilation_roi.min.y);
  hash.AddI64(profile.compilation_roi.max.x);
  hash.AddI64(profile.compilation_roi.max.y);
  hash.AddU64(static_cast<std::uint64_t>(profile.active_regions.size()));
  for (const ActiveRegion& region : profile.active_regions) {
    hash.AddU32(region.layer);
    hash.AddI64(region.bounds.min.x);
    hash.AddI64(region.bounds.min.y);
    hash.AddI64(region.bounds.max.x);
    hash.AddI64(region.bounds.max.y);
  }
  hash.AddByte(profile.heading_mask);
  hash.AddU32(profile.costs.orthogonal_step);
  hash.AddU32(profile.costs.diagonal_step);
  hash.AddU32(profile.costs.bend);
  return hash.Finish();
}

RuleBucketV1 DeriveM1RuleBucket(const board_ir::RoutingProfile& profile) {
  RuleBucketV1 bucket{
      .identity = 0,
      .nominal_width = profile.nominal_width,
      .clearance = profile.clearance,
      .allowed_layers = profile.allowed_layers,
      .allowed_headings = profile.allowed_headings,
  };
  std::ranges::sort(bucket.allowed_layers);
  bucket.allowed_layers.erase(std::ranges::unique(bucket.allowed_layers).begin(),
                              bucket.allowed_layers.end());

  board_ir::StableHashBuilder hash;
  hash.AddString("APGAR-M1-RULE-BUCKET-V1");
  hash.AddI64(bucket.nominal_width);
  hash.AddI64(bucket.clearance);
  hash.AddU64(static_cast<std::uint64_t>(bucket.allowed_layers.size()));
  for (board_ir::LayerId layer : bucket.allowed_layers) {
    hash.AddU32(layer);
  }
  hash.AddByte(bucket.allowed_headings);
  bucket.identity = hash.Finish();
  return bucket;
}

const SparseTile* CompiledBoard::FindTile(const TileKey& key) const noexcept {
  const auto tile = std::ranges::lower_bound(tiles_, key, {}, &SparseTile::key);
  return tile != tiles_.end() && tile->key == key ? &*tile : nullptr;
}

const CompiledNode* CompiledBoard::FindNode(board_ir::LayerId layer, std::int64_t lattice_x,
                                            std::int64_t lattice_y) const noexcept {
  const LatticeIndex index{.x = lattice_x, .y = lattice_y};
  const SparseTile* tile = FindTile(TileForLatticeIndex(profile_, layer, index));
  if (tile == nullptr) {
    return nullptr;
  }
  const std::uint64_t local_index = LocalNodeIndex(profile_, index);
  const auto node =
      std::ranges::lower_bound(tile->nodes, local_index, {}, &CompiledNode::local_index);
  return node != tile->nodes.end() && node->local_index == local_index ? &*node : nullptr;
}

bool CompiledBoard::ContainsNode(board_ir::LayerId layer, std::int64_t lattice_x,
                                 std::int64_t lattice_y) const noexcept {
  return FindNode(layer, lattice_x, lattice_y) != nullptr;
}

bool CompiledBoard::EdgeIsLegal(board_ir::LayerId layer, std::int64_t lattice_x,
                                std::int64_t lattice_y, Direction direction) const noexcept {
  const CompiledNode* node = FindNode(layer, lattice_x, lattice_y);
  return node != nullptr && (node->legal_edges & MaskFor(direction)) != 0;
}

CompileResult CompileBoard(const board_ir::BoardSnapshot& board, CompilerProfile profile) {
  return CompileBoard(board, std::move(profile), board.data().routing_profile);
}

CompileResult CompileBoard(const board_ir::BoardSnapshot& board, CompilerProfile profile,
                           board_ir::RoutingProfile routing_profile) {
  Normalize(profile);
  board_ir::RoutingProfilePreparationResult prepared =
      board_ir::PrepareRoutingProfile(board, std::move(routing_profile));
  if (const auto* error = std::get_if<board_ir::BoardValidationError>(&prepared);
      error != nullptr) {
    return Error(
        CompileErrorCode::kInvalidRoutingProfile,
        "Routing profile is invalid for the supplied Board IR snapshot: " + error->message);
  }
  routing_profile = std::get<board_ir::RoutingProfile>(std::move(prepared));
  if (std::optional<CompileError> error = ValidateProfile(profile, routing_profile);
      error.has_value()) {
    return std::move(*error);
  }

  for (const ActiveRegion& region : profile.active_regions) {
    const LatticeBounds bounds = *BoundsForRegion(profile, region.bounds);
    const UWide width = static_cast<UWide>(static_cast<Wide>(bounds.max_x) - bounds.min_x + 1);
    const UWide height = static_cast<UWide>(static_cast<Wide>(bounds.max_y) - bounds.min_y + 1);
    if (width * height > kMaximumRepresentedNodes) {
      return Error(CompileErrorCode::kUnrepresentableProfile,
                   "An active region alone exceeds the M1 represented-node safety bound");
    }
  }

  MutableTiles mutable_tiles;
  std::optional<TileKey> cached_key;
  std::map<std::uint64_t, DirectionMask>* cached_nodes = nullptr;
  std::uint64_t represented_nodes = 0;
  for (const ActiveRegion& region : profile.active_regions) {
    const LatticeBounds bounds = *BoundsForRegion(profile, region.bounds);
    for (std::int64_t lattice_y = bounds.min_y;; ++lattice_y) {
      for (std::int64_t lattice_x = bounds.min_x;; ++lattice_x) {
        const LatticeIndex index{.x = lattice_x, .y = lattice_y};
        const TileKey key = TileForLatticeIndex(profile, region.layer, index);
        if (!cached_key.has_value() || *cached_key != key) {
          cached_nodes = &mutable_tiles.try_emplace(key).first->second;
          cached_key = key;
        }
        const bool inserted = cached_nodes->try_emplace(LocalNodeIndex(profile, index), 0).second;
        if (inserted) {
          if (represented_nodes == kMaximumRepresentedNodes) {
            return Error(CompileErrorCode::kUnrepresentableProfile,
                         "The sparse active-region union exceeds the M1 represented-node safety "
                         "bound");
          }
          ++represented_nodes;
        }
        if (lattice_x == bounds.max_x) {
          break;
        }
      }
      if (lattice_y == bounds.max_y) {
        break;
      }
    }
  }

  CompilerTelemetry telemetry{
      .active_tile_count = static_cast<std::uint64_t>(mutable_tiles.size()),
      .represented_nodes = represented_nodes,
  };
  constexpr std::array<Direction, 4> kForwardDirections = {
      Direction::kEast,
      Direction::kNorthEast,
      Direction::kNorth,
      Direction::kNorthWest,
  };
  for (auto& [key, nodes] : mutable_tiles) {
    for (auto& [local_index, source_mask] : nodes) {
      const LatticeIndex index = GlobalLatticeIndex(profile, key, local_index);
      const std::optional<board_ir::Point64> start = LatticeIndexToExactPoint(profile, index);
      if (!start.has_value()) {
        return Error(CompileErrorCode::kInternalInvariant,
                     "Represented lattice node escaped the validated coordinate envelope");
      }
      for (Direction direction : kForwardDirections) {
        if ((profile.heading_mask & HeadingFor(direction)) == 0) {
          continue;
        }
        const DirectionDelta delta = DeltaFor(direction);
        const LatticeIndex neighbor{
            .x = index.x + delta.x,
            .y = index.y + delta.y,
        };
        DirectionMask* neighbor_mask =
            FindMutableNode(mutable_tiles, profile, key.layer, neighbor.x, neighbor.y);
        if (neighbor_mask == nullptr) {
          continue;
        }
        telemetry.represented_directional_edges += 2;
        const std::optional<board_ir::Point64> end = LatticeIndexToExactPoint(profile, neighbor);
        if (!end.has_value()) {
          return Error(CompileErrorCode::kInternalInvariant,
                       "Represented edge endpoint escaped the validated coordinate envelope");
        }
        const geometry::MovementValidationResult exact =
            geometry::internal::ValidateMovementForPreparedProfile(
                board, routing_profile, key.layer,
                board_ir::Segment64{.start = *start, .end = *end});
        if (exact.legal()) {
          source_mask |= MaskFor(direction);
          *neighbor_mask |= MaskFor(Opposite(direction));
          telemetry.legal_directional_edges += 2;
        } else if (exact.code == geometry::MovementViolationCode::kStaticObstacleConflict) {
          telemetry.blocked_directional_edges += 2;
        } else {
          return Error(CompileErrorCode::kInternalInvariant,
                       "Exact movement oracle rejected a validated compiler edge for a "
                       "non-geometry reason: " +
                           exact.detail);
        }
      }
    }
  }

  const std::uint64_t exact_legal_edges =
      telemetry.legal_directional_edges + telemetry.false_blocked_directional_edges;
  if (exact_legal_edges != 0) {
    telemetry.false_blocked_rate_parts_per_billion =
        (telemetry.false_blocked_directional_edges * 1'000'000'000ULL) / exact_legal_edges;
  }
  if (telemetry.legal_directional_edges + telemetry.blocked_directional_edges !=
      telemetry.represented_directional_edges) {
    return Error(CompileErrorCode::kInternalInvariant,
                 "Directional-edge telemetry does not partition represented edges");
  }

  const UWide maximum_step_cost =
      static_cast<UWide>(std::max(profile.costs.orthogonal_step, profile.costs.diagonal_step)) +
      profile.costs.bend;
  const UWide maximum_simple_state_path =
      static_cast<UWide>(telemetry.represented_nodes) * kStableDirectionOrder.size();
  if (maximum_simple_state_path * maximum_step_cost >= std::numeric_limits<std::uint64_t>::max()) {
    return Error(CompileErrorCode::kUnrepresentableProfile,
                 "Profile costs can overflow or collide with the unreachable route-cost sentinel");
  }

  std::vector<SparseTile> tiles;
  tiles.reserve(mutable_tiles.size());
  for (const auto& [key, nodes] : mutable_tiles) {
    SparseTile tile{.key = key, .nodes = {}};
    tile.nodes.reserve(nodes.size());
    for (const auto& [local_index, mask] : nodes) {
      tile.nodes.push_back(CompiledNode{.local_index = local_index, .legal_edges = mask});
    }
    tiles.push_back(std::move(tile));
  }

  const std::uint64_t profile_fingerprint = FingerprintCompilerProfile(profile);
  RuleBucketV1 rule_bucket = DeriveM1RuleBucket(routing_profile);
  CompiledBoard compiled(board.content_hash(), profile_fingerprint, std::move(rule_bucket),
                         std::move(routing_profile), std::move(profile), std::move(tiles),
                         telemetry);
  compiled.telemetry_.estimated_host_bytes = EstimateHostBytes(compiled);
  return compiled;
}

}  // namespace apgar::geometry_compiler
