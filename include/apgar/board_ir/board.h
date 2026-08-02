#ifndef APGAR_BOARD_IR_BOARD_H_
#define APGAR_BOARD_IR_BOARD_H_

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace apgar::board_ir {

using DbCoord = std::int64_t;
using EntityId = std::uint64_t;
using Generation = std::uint32_t;
using LayerId = std::uint32_t;

inline constexpr std::uint32_t kBoardSchemaVersion = 1;
inline constexpr DbCoord kMaxAbsDbCoord = 1'000'000'000'000;

struct EntityRef {
  EntityId id;
  Generation generation;

  friend bool operator==(const EntityRef&, const EntityRef&) = default;
};

struct Point64 {
  DbCoord x;
  DbCoord y;

  friend bool operator==(const Point64&, const Point64&) = default;
};

struct Segment64 {
  Point64 start;
  Point64 end;

  friend bool operator==(const Segment64&, const Segment64&) = default;
};

struct AxisAlignedBox64 {
  Point64 min;
  Point64 max;

  friend bool operator==(const AxisAlignedBox64&, const AxisAlignedBox64&) = default;
};

[[nodiscard]] constexpr bool CoordinateIsValid(DbCoord coordinate) noexcept {
  return coordinate >= -kMaxAbsDbCoord && coordinate <= kMaxAbsDbCoord;
}

[[nodiscard]] constexpr bool PointIsValid(Point64 point) noexcept {
  return CoordinateIsValid(point.x) && CoordinateIsValid(point.y);
}

[[nodiscard]] constexpr bool BoxIsValid(const AxisAlignedBox64& box) noexcept {
  return PointIsValid(box.min) && PointIsValid(box.max) && box.min.x <= box.max.x &&
         box.min.y <= box.max.y;
}

enum class LayerType : std::uint8_t {
  kSignal,
};

struct Layer {
  EntityRef ref;
  LayerId routing_id;
  std::string name;
  std::int32_t physical_order;
  LayerType type;
  bool routable;

  friend bool operator==(const Layer&, const Layer&) = default;
};

struct Net {
  EntityRef ref;
  std::string name;
  std::vector<EntityRef> terminals;

  friend bool operator==(const Net&, const Net&) = default;
};

struct Terminal {
  EntityRef ref;
  EntityRef net;
  std::string component;
  std::string pin;
  Point64 center;
  AxisAlignedBox64 connection_region;
  std::vector<LayerId> layers;

  friend bool operator==(const Terminal&, const Terminal&) = default;
};

struct Obstacle {
  EntityRef ref;
  LayerId layer;
  AxisAlignedBox64 bounds;
  std::optional<EntityRef> owner_net;
  std::string provenance;

  friend bool operator==(const Obstacle&, const Obstacle&) = default;
};

enum class Heading : std::uint8_t {
  kHorizontal = 1U << 0U,
  kVertical = 1U << 1U,
  kDiagonal45 = 1U << 2U,
};

using HeadingMask = std::uint8_t;

inline constexpr HeadingMask kM1HeadingMask = static_cast<HeadingMask>(Heading::kHorizontal) |
                                              static_cast<HeadingMask>(Heading::kVertical) |
                                              static_cast<HeadingMask>(Heading::kDiagonal45);

struct RoutingProfile {
  EntityRef net;
  DbCoord nominal_width;
  DbCoord clearance;
  std::vector<LayerId> allowed_layers;
  HeadingMask allowed_headings;

  friend bool operator==(const RoutingProfile&, const RoutingProfile&) = default;
};

struct BoardData {
  std::uint32_t schema_version = kBoardSchemaVersion;
  DbCoord dbu_per_millimeter = 0;
  std::uint64_t revision = 0;
  std::string adapter_name;
  std::string adapter_version;
  std::vector<Layer> layers;
  std::vector<Net> nets;
  std::vector<Terminal> terminals;
  std::vector<Obstacle> obstacles;
  RoutingProfile routing_profile;

  friend bool operator==(const BoardData&, const BoardData&) = default;
};

enum class BoardValidationCode : std::uint8_t {
  kUnsupportedSchema,
  kInvalidUnits,
  kMissingAdapterMetadata,
  kInvalidEncoding,
  kDuplicateEntityId,
  kInvalidCoordinate,
  kInvalidLayer,
  kInvalidLayerStack,
  kInvalidReference,
  kInvalidGeometry,
  kInvalidRoutingProfile,
  kNotM1Board,
};

struct BoardValidationError {
  BoardValidationCode code;
  std::string message;
};

class BoardSnapshot;

// Immutable capability proving that one routing profile was canonicalized and
// validated against the identified BoardSnapshot. Callers can inspect the
// retained profile but cannot construct prepared state directly.
class PreparedRoutingProfile {
 public:
  [[nodiscard]] const RoutingProfile& profile() const noexcept { return profile_; }
  [[nodiscard]] std::uint64_t source_board_content_hash() const noexcept {
    return source_board_content_hash_;
  }

  friend bool operator==(const PreparedRoutingProfile&, const PreparedRoutingProfile&) = default;

 private:
  PreparedRoutingProfile(RoutingProfile profile, std::uint64_t source_board_content_hash)
      : profile_(std::move(profile)), source_board_content_hash_(source_board_content_hash) {}

  RoutingProfile profile_;
  std::uint64_t source_board_content_hash_;

  friend std::variant<PreparedRoutingProfile, BoardValidationError> PrepareRoutingProfile(
      const BoardSnapshot& board, RoutingProfile profile);
};

class BoardSnapshot {
 public:
  [[nodiscard]] const BoardData& data() const noexcept { return data_; }
  [[nodiscard]] std::uint64_t content_hash() const noexcept { return content_hash_; }

  [[nodiscard]] const Layer* FindLayer(LayerId id) const noexcept;
  [[nodiscard]] const Net* FindNet(EntityRef ref) const noexcept;
  [[nodiscard]] const Terminal* FindTerminal(EntityRef ref) const noexcept;
  [[nodiscard]] std::span<const Obstacle> ObstaclesOnLayer(LayerId layer) const noexcept;

 private:
  explicit BoardSnapshot(BoardData data, std::uint64_t content_hash)
      : data_(std::move(data)), content_hash_(content_hash) {}

  BoardData data_;
  std::uint64_t content_hash_;

  friend std::variant<BoardSnapshot, BoardValidationError> CreateBoardSnapshot(BoardData data);
};

using BoardCreationResult = std::variant<BoardSnapshot, BoardValidationError>;
using RoutingProfilePreparationResult = std::variant<PreparedRoutingProfile, BoardValidationError>;

[[nodiscard]] BoardCreationResult CreateBoardSnapshot(BoardData data);

// Canonicalizes and validates a per-net M1 routing profile against one
// immutable BoardSnapshot, returning prepared state bound to that snapshot's
// content hash. Board IR v1 retains one authoritative rule profile; additional
// prepared contexts may change only its routed net and therefore its obstacle
// ownership semantics.
[[nodiscard]] RoutingProfilePreparationResult PrepareRoutingProfile(const BoardSnapshot& board,
                                                                    RoutingProfile profile);

}  // namespace apgar::board_ir

#endif  // APGAR_BOARD_IR_BOARD_H_
