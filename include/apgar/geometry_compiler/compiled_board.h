#ifndef APGAR_GEOMETRY_COMPILER_COMPILED_BOARD_H_
#define APGAR_GEOMETRY_COMPILER_COMPILED_BOARD_H_

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "apgar/board_ir/board.h"

namespace apgar::geometry_compiler {

inline constexpr std::uint32_t kCompilerProfileSchemaVersion = 1;
inline constexpr std::uint32_t kGeometryCompilerVersion = 1;
inline constexpr std::uint64_t kMaximumRepresentedNodes = 10'000'000;

enum class Direction : std::uint8_t {
  kEast = 0,
  kNorthEast = 1,
  kNorth = 2,
  kNorthWest = 3,
  kWest = 4,
  kSouthWest = 5,
  kSouth = 6,
  kSouthEast = 7,
};

using DirectionMask = std::uint8_t;

inline constexpr std::array<Direction, 8> kStableDirectionOrder = {
    Direction::kEast, Direction::kNorthEast, Direction::kNorth, Direction::kNorthWest,
    Direction::kWest, Direction::kSouthWest, Direction::kSouth, Direction::kSouthEast,
};

struct DirectionDelta {
  std::int8_t x;
  std::int8_t y;
};

[[nodiscard]] constexpr DirectionDelta DeltaFor(Direction direction) noexcept {
  constexpr std::array<DirectionDelta, 8> kDeltas = {
      DirectionDelta{.x = 1, .y = 0},  DirectionDelta{.x = 1, .y = 1},
      DirectionDelta{.x = 0, .y = 1},  DirectionDelta{.x = -1, .y = 1},
      DirectionDelta{.x = -1, .y = 0}, DirectionDelta{.x = -1, .y = -1},
      DirectionDelta{.x = 0, .y = -1}, DirectionDelta{.x = 1, .y = -1},
  };
  return kDeltas[static_cast<std::size_t>(direction)];
}

[[nodiscard]] constexpr DirectionMask MaskFor(Direction direction) noexcept {
  return static_cast<DirectionMask>(1U << static_cast<std::uint8_t>(direction));
}

[[nodiscard]] constexpr Direction Opposite(Direction direction) noexcept {
  return static_cast<Direction>((static_cast<std::uint8_t>(direction) + 4U) % 8U);
}

[[nodiscard]] constexpr bool IsDiagonal(Direction direction) noexcept {
  const DirectionDelta delta = DeltaFor(direction);
  return delta.x != 0 && delta.y != 0;
}

[[nodiscard]] constexpr board_ir::HeadingMask HeadingFor(Direction direction) noexcept {
  const DirectionDelta delta = DeltaFor(direction);
  if (delta.y == 0) {
    return static_cast<board_ir::HeadingMask>(board_ir::Heading::kHorizontal);
  }
  if (delta.x == 0) {
    return static_cast<board_ir::HeadingMask>(board_ir::Heading::kVertical);
  }
  return static_cast<board_ir::HeadingMask>(board_ir::Heading::kDiagonal45);
}

struct ActiveRegion {
  board_ir::LayerId layer;
  board_ir::AxisAlignedBox64 bounds;

  friend bool operator==(const ActiveRegion&, const ActiveRegion&) = default;
};

struct DeterministicCosts {
  std::uint32_t orthogonal_step;
  std::uint32_t diagonal_step;
  std::uint32_t bend;

  friend bool operator==(const DeterministicCosts&, const DeterministicCosts&) = default;
};

struct CompilerProfile {
  std::uint32_t schema_version = kCompilerProfileSchemaVersion;
  board_ir::Point64 lattice_origin;
  board_ir::DbCoord lattice_step;
  std::uint32_t tile_width_nodes;
  std::uint32_t tile_height_nodes;
  board_ir::AxisAlignedBox64 compilation_roi;
  std::vector<ActiveRegion> active_regions;
  board_ir::HeadingMask heading_mask;
  DeterministicCosts costs;

  friend bool operator==(const CompilerProfile&, const CompilerProfile&) = default;
};

struct LatticeIndex {
  std::int64_t x;
  std::int64_t y;

  friend bool operator==(const LatticeIndex&, const LatticeIndex&) = default;
};

[[nodiscard]] std::optional<LatticeIndex> ExactPointToLatticeIndex(
    const CompilerProfile& profile, board_ir::Point64 point) noexcept;
[[nodiscard]] std::optional<board_ir::Point64> LatticeIndexToExactPoint(
    const CompilerProfile& profile, LatticeIndex index) noexcept;

struct RuleBucketV1 {
  std::uint64_t identity;
  board_ir::DbCoord nominal_width;
  board_ir::DbCoord clearance;
  std::vector<board_ir::LayerId> allowed_layers;
  board_ir::HeadingMask allowed_headings;

  friend bool operator==(const RuleBucketV1&, const RuleBucketV1&) = default;
};

struct TileCoordinate {
  std::int64_t x;
  std::int64_t y;

  friend bool operator==(const TileCoordinate&, const TileCoordinate&) = default;
  friend auto operator<=>(const TileCoordinate&, const TileCoordinate&) = default;
};

struct TileKey {
  board_ir::LayerId layer;
  TileCoordinate coordinate;

  friend bool operator==(const TileKey&, const TileKey&) = default;
  friend auto operator<=>(const TileKey&, const TileKey&) = default;
};

[[nodiscard]] TileKey TileForLatticeIndex(const CompilerProfile& profile, board_ir::LayerId layer,
                                          LatticeIndex index) noexcept;
[[nodiscard]] std::uint64_t LocalNodeIndex(const CompilerProfile& profile,
                                           LatticeIndex index) noexcept;
[[nodiscard]] LatticeIndex GlobalLatticeIndex(const CompilerProfile& profile, const TileKey& key,
                                              std::uint64_t local_index) noexcept;

struct CompiledNode {
  // Row-major position inside its tile. Sparse tiles store only represented
  // nodes, so gaps do not allocate placeholder entries.
  std::uint64_t local_index;
  DirectionMask legal_edges;

  friend bool operator==(const CompiledNode&, const CompiledNode&) = default;
};

struct SparseTile {
  TileKey key;
  std::vector<CompiledNode> nodes;

  friend bool operator==(const SparseTile&, const SparseTile&) = default;
};

struct CompilerTelemetry {
  std::uint64_t active_tile_count = 0;
  std::uint64_t represented_nodes = 0;
  std::uint64_t represented_directional_edges = 0;
  std::uint64_t legal_directional_edges = 0;
  std::uint64_t blocked_directional_edges = 0;
  std::uint64_t false_blocked_directional_edges = 0;
  std::uint64_t false_blocked_rate_parts_per_billion = 0;
  // Deterministic logical payload estimate: sizeof(CompiledBoard) plus the
  // sizes (not capacities) of its owned vector elements. Allocator metadata,
  // spare capacity, and the source BoardSnapshot are excluded.
  std::uint64_t estimated_host_bytes = 0;

  friend bool operator==(const CompilerTelemetry&, const CompilerTelemetry&) = default;
};

enum class CompileErrorCode : std::uint8_t {
  kInvalidProfile,
  kUnrepresentableProfile,
  kUnsupported,
  kInternalInvariant,
};

struct CompileError {
  CompileErrorCode code;
  std::string detail;
};

class CompiledBoard {
 public:
  CompiledBoard(const CompiledBoard&) = default;
  CompiledBoard(CompiledBoard&&) noexcept = default;
  CompiledBoard& operator=(const CompiledBoard&) = default;
  CompiledBoard& operator=(CompiledBoard&&) noexcept = default;

  [[nodiscard]] std::uint64_t source_board_content_hash() const noexcept {
    return source_board_content_hash_;
  }
  [[nodiscard]] std::uint64_t compiler_profile_fingerprint() const noexcept {
    return compiler_profile_fingerprint_;
  }
  [[nodiscard]] std::uint32_t compiler_version() const noexcept { return compiler_version_; }
  [[nodiscard]] const RuleBucketV1& rule_bucket() const noexcept { return rule_bucket_; }
  [[nodiscard]] const CompilerProfile& profile() const noexcept { return profile_; }
  [[nodiscard]] const CompilerTelemetry& telemetry() const noexcept { return telemetry_; }
  [[nodiscard]] std::span<const SparseTile> tiles() const noexcept { return tiles_; }

  [[nodiscard]] const SparseTile* FindTile(const TileKey& key) const noexcept;
  [[nodiscard]] const CompiledNode* FindNode(board_ir::LayerId layer, std::int64_t lattice_x,
                                             std::int64_t lattice_y) const noexcept;
  [[nodiscard]] bool ContainsNode(board_ir::LayerId layer, std::int64_t lattice_x,
                                  std::int64_t lattice_y) const noexcept;
  [[nodiscard]] bool EdgeIsLegal(board_ir::LayerId layer, std::int64_t lattice_x,
                                 std::int64_t lattice_y, Direction direction) const noexcept;

  friend bool operator==(const CompiledBoard&, const CompiledBoard&) = default;

 private:
  CompiledBoard(std::uint64_t source_board_content_hash, std::uint64_t compiler_profile_fingerprint,
                RuleBucketV1 rule_bucket, CompilerProfile profile, std::vector<SparseTile> tiles,
                CompilerTelemetry telemetry)
      : source_board_content_hash_(source_board_content_hash),
        compiler_profile_fingerprint_(compiler_profile_fingerprint),
        compiler_version_(kGeometryCompilerVersion),
        rule_bucket_(std::move(rule_bucket)),
        profile_(std::move(profile)),
        tiles_(std::move(tiles)),
        telemetry_(telemetry) {}

  std::uint64_t source_board_content_hash_;
  std::uint64_t compiler_profile_fingerprint_;
  std::uint32_t compiler_version_;
  RuleBucketV1 rule_bucket_;
  CompilerProfile profile_;
  std::vector<SparseTile> tiles_;
  CompilerTelemetry telemetry_;

  friend std::variant<CompiledBoard, CompileError> CompileBoard(const board_ir::BoardSnapshot&,
                                                                CompilerProfile);
  friend class CompiledBoardTestPeer;
};

using CompileResult = std::variant<CompiledBoard, CompileError>;

[[nodiscard]] CompileResult CompileBoard(const board_ir::BoardSnapshot& board,
                                         CompilerProfile profile);

[[nodiscard]] std::uint64_t FingerprintCompilerProfile(CompilerProfile profile);
[[nodiscard]] RuleBucketV1 DeriveM1RuleBucket(const board_ir::RoutingProfile& profile);

}  // namespace apgar::geometry_compiler

#endif  // APGAR_GEOMETRY_COMPILER_COMPILED_BOARD_H_
