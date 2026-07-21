#include "apgar/board_ir/board.h"

#include <algorithm>
#include <map>
#include <set>
#include <string_view>
#include <utility>

#include "apgar/board_ir/stable_hash.h"
#include "apgar/text/utf8.h"

namespace apgar::board_ir {
namespace {

[[nodiscard]] bool EntityRefLess(const EntityRef& left, const EntityRef& right) noexcept {
  if (left.id != right.id) {
    return left.id < right.id;
  }
  return left.generation < right.generation;
}

void Normalize(BoardData& data) {
  std::ranges::sort(data.layers, {}, &Layer::routing_id);
  std::ranges::sort(data.nets, {}, [](const Net& net) { return net.ref.id; });
  std::ranges::sort(data.terminals, {}, [](const Terminal& terminal) { return terminal.ref.id; });
  std::ranges::sort(data.obstacles, [](const Obstacle& left, const Obstacle& right) {
    if (left.layer != right.layer) {
      return left.layer < right.layer;
    }
    return left.ref.id < right.ref.id;
  });

  for (Net& net : data.nets) {
    std::ranges::sort(net.terminals, EntityRefLess);
  }
  for (Terminal& terminal : data.terminals) {
    std::ranges::sort(terminal.layers);
  }
  std::ranges::sort(data.routing_profile.allowed_layers);
}

[[nodiscard]] BoardValidationError Error(BoardValidationCode code, std::string message) {
  return BoardValidationError{.code = code, .message = std::move(message)};
}

[[nodiscard]] std::optional<BoardValidationError> Validate(const BoardData& data) {
  if (data.schema_version != kBoardSchemaVersion) {
    return Error(BoardValidationCode::kUnsupportedSchema,
                 "Board IR schema version is not supported");
  }
  if (data.dbu_per_millimeter <= 0 || data.dbu_per_millimeter > kMaxAbsDbCoord) {
    return Error(BoardValidationCode::kInvalidUnits,
                 "Board IR unit scale must be positive and bounded");
  }
  if (data.adapter_name.empty() || data.adapter_version.empty()) {
    return Error(BoardValidationCode::kMissingAdapterMetadata,
                 "Board IR adapter name and version are required");
  }
  if (!text::IsValidUtf8(data.adapter_name) || !text::IsValidUtf8(data.adapter_version)) {
    return Error(BoardValidationCode::kInvalidEncoding,
                 "Board IR adapter metadata must be valid UTF-8");
  }

  std::map<EntityId, Generation> all_entities;
  const auto register_entity = [&all_entities](EntityRef ref) {
    return all_entities.emplace(ref.id, ref.generation).second;
  };

  std::map<LayerId, const Layer*> layers;
  std::set<std::string_view> layer_names;
  std::set<std::int32_t> physical_orders;
  std::size_t routable_signal_layers = 0;
  for (const Layer& layer : data.layers) {
    if (!text::IsValidUtf8(layer.name)) {
      return Error(BoardValidationCode::kInvalidEncoding, "Layer names must be valid UTF-8");
    }
    if (!register_entity(layer.ref)) {
      return Error(BoardValidationCode::kDuplicateEntityId, "Layer reuses an existing entity ID");
    }
    if (!layers.emplace(layer.routing_id, &layer).second ||
        !layer_names.emplace(layer.name).second || layer.name.empty()) {
      return Error(BoardValidationCode::kInvalidLayer,
                   "Layer routing IDs and names must be unique and non-empty");
    }
    if (layer.type != LayerType::kSignal) {
      return Error(BoardValidationCode::kInvalidLayer, "Board IR v1 supports signal layers only");
    }
    if (layer.physical_order < 0 ||
        static_cast<std::size_t>(layer.physical_order) >= data.layers.size() ||
        !physical_orders.emplace(layer.physical_order).second) {
      return Error(BoardValidationCode::kInvalidLayerStack,
                   "Layer physical orders must be unique and contiguous");
    }
    if (layer.routable && layer.type == LayerType::kSignal) {
      ++routable_signal_layers;
    }
  }
  if (routable_signal_layers != 2 && routable_signal_layers != 4) {
    return Error(BoardValidationCode::kNotM1Board,
                 "M1 boards require exactly two or four routable signal layers");
  }

  std::map<EntityId, const Net*> nets;
  std::set<std::string_view> net_names;
  for (const Net& net : data.nets) {
    if (!text::IsValidUtf8(net.name)) {
      return Error(BoardValidationCode::kInvalidEncoding, "Net names must be valid UTF-8");
    }
    if (!register_entity(net.ref)) {
      return Error(BoardValidationCode::kDuplicateEntityId, "Net reuses an existing entity ID");
    }
    if (!nets.emplace(net.ref.id, &net).second || net.name.empty() ||
        !net_names.emplace(net.name).second) {
      return Error(BoardValidationCode::kInvalidReference,
                   "Net names must be non-empty and unique");
    }
  }

  std::map<EntityId, const Terminal*> terminals;
  for (const Terminal& terminal : data.terminals) {
    if (!text::IsValidUtf8(terminal.component) || !text::IsValidUtf8(terminal.pin)) {
      return Error(BoardValidationCode::kInvalidEncoding,
                   "Terminal provenance must be valid UTF-8");
    }
    if (!register_entity(terminal.ref)) {
      return Error(BoardValidationCode::kDuplicateEntityId,
                   "Terminal reuses an existing entity ID");
    }
    if (!PointIsValid(terminal.center) || !PointIsValid(terminal.connection_region.min) ||
        !PointIsValid(terminal.connection_region.max)) {
      return Error(BoardValidationCode::kInvalidCoordinate,
                   "Terminal coordinates exceed the exact arithmetic envelope");
    }
    if (!terminals.emplace(terminal.ref.id, &terminal).second ||
        !BoxIsValid(terminal.connection_region) || terminal.layers.empty() ||
        terminal.center.x < terminal.connection_region.min.x ||
        terminal.center.x > terminal.connection_region.max.x ||
        terminal.center.y < terminal.connection_region.min.y ||
        terminal.center.y > terminal.connection_region.max.y) {
      return Error(BoardValidationCode::kInvalidGeometry,
                   "Terminal geometry or layer membership is invalid");
    }
    const auto net = nets.find(terminal.net.id);
    if (net == nets.end() || net->second->ref != terminal.net) {
      return Error(BoardValidationCode::kInvalidReference,
                   "Terminal refers to an unknown or stale net");
    }
    if (terminal.component.empty() || terminal.pin.empty()) {
      return Error(BoardValidationCode::kInvalidReference,
                   "Terminal component and pin provenance are required");
    }
    for (LayerId layer : terminal.layers) {
      if (!layers.contains(layer)) {
        return Error(BoardValidationCode::kInvalidReference, "Terminal refers to an unknown layer");
      }
    }
    if (std::ranges::adjacent_find(terminal.layers) != terminal.layers.end()) {
      return Error(BoardValidationCode::kInvalidReference,
                   "Terminal layer membership contains duplicates");
    }
  }

  for (const Net& net : data.nets) {
    if (std::ranges::adjacent_find(net.terminals) != net.terminals.end()) {
      return Error(BoardValidationCode::kInvalidReference,
                   "Net terminal membership contains duplicates");
    }
    for (EntityRef terminal_ref : net.terminals) {
      const auto terminal = terminals.find(terminal_ref.id);
      if (terminal == terminals.end() || terminal->second->ref != terminal_ref ||
          terminal->second->net != net.ref) {
        return Error(BoardValidationCode::kInvalidReference,
                     "Net refers to an unknown, stale, or foreign terminal");
      }
    }
  }
  for (const Terminal& terminal : data.terminals) {
    const Net& owner = *nets.at(terminal.net.id);
    if (!std::ranges::binary_search(owner.terminals, terminal.ref, EntityRefLess)) {
      return Error(BoardValidationCode::kInvalidReference,
                   "Terminal is not listed by its owning net");
    }
  }

  for (const Obstacle& obstacle : data.obstacles) {
    if (!text::IsValidUtf8(obstacle.provenance)) {
      return Error(BoardValidationCode::kInvalidEncoding,
                   "Obstacle provenance must be valid UTF-8");
    }
    if (!register_entity(obstacle.ref)) {
      return Error(BoardValidationCode::kDuplicateEntityId,
                   "Obstacle reuses an existing entity ID");
    }
    if (!PointIsValid(obstacle.bounds.min) || !PointIsValid(obstacle.bounds.max)) {
      return Error(BoardValidationCode::kInvalidCoordinate,
                   "Obstacle coordinates exceed the exact arithmetic envelope");
    }
    if (!layers.contains(obstacle.layer) || !BoxIsValid(obstacle.bounds) ||
        obstacle.provenance.empty()) {
      return Error(BoardValidationCode::kInvalidGeometry,
                   "Obstacle geometry, layer, or provenance is invalid");
    }
    if (obstacle.owner_net.has_value()) {
      const auto owner = nets.find(obstacle.owner_net->id);
      if (owner == nets.end() || owner->second->ref != *obstacle.owner_net) {
        return Error(BoardValidationCode::kInvalidReference,
                     "Obstacle refers to an unknown or stale owner net");
      }
    }
  }

  const RoutingProfile& profile = data.routing_profile;
  const auto target_net = nets.find(profile.net.id);
  if (target_net == nets.end() || target_net->second->ref != profile.net) {
    return Error(BoardValidationCode::kInvalidRoutingProfile,
                 "Routing profile refers to an unknown or stale net");
  }
  if (target_net->second->terminals.size() != 2) {
    return Error(BoardValidationCode::kNotM1Board,
                 "M1 routing profiles require exactly two terminals");
  }
  if (profile.nominal_width <= 0 || profile.nominal_width > kMaxAbsDbCoord ||
      profile.clearance < 0 || profile.clearance > kMaxAbsDbCoord ||
      profile.allowed_layers.empty() || profile.allowed_headings == 0 ||
      (profile.allowed_headings & static_cast<HeadingMask>(~kM1HeadingMask)) != 0) {
    return Error(BoardValidationCode::kInvalidRoutingProfile,
                 "Routing profile dimensions, layers, or headings are invalid");
  }
  if (std::ranges::adjacent_find(profile.allowed_layers) != profile.allowed_layers.end()) {
    return Error(BoardValidationCode::kInvalidRoutingProfile,
                 "Routing profile layers contain duplicates");
  }
  for (LayerId layer_id : profile.allowed_layers) {
    const auto layer = layers.find(layer_id);
    if (layer == layers.end() || !layer->second->routable ||
        layer->second->type != LayerType::kSignal) {
      return Error(BoardValidationCode::kInvalidRoutingProfile,
                   "Routing profile contains an unavailable signal layer");
    }
  }
  for (EntityRef terminal_ref : target_net->second->terminals) {
    const Terminal& terminal = *terminals.at(terminal_ref.id);
    const bool has_routable_connection =
        std::ranges::any_of(terminal.layers, [&](LayerId terminal_layer) {
          return std::ranges::binary_search(profile.allowed_layers, terminal_layer);
        });
    if (!has_routable_connection) {
      return Error(BoardValidationCode::kInvalidRoutingProfile,
                   "Every routed-net terminal must intersect an allowed routing layer");
    }
  }

  return std::nullopt;
}

void AddEntityRef(StableHashBuilder& hash, EntityRef ref) {
  hash.AddU64(ref.id);
  hash.AddU32(ref.generation);
}

void AddPoint(StableHashBuilder& hash, Point64 point) {
  hash.AddI64(point.x);
  hash.AddI64(point.y);
}

void AddBox(StableHashBuilder& hash, const AxisAlignedBox64& box) {
  AddPoint(hash, box.min);
  AddPoint(hash, box.max);
}

[[nodiscard]] std::uint64_t ContentHash(const BoardData& data) {
  StableHashBuilder hash;
  hash.AddString("APGAR-BOARD-IR");
  hash.AddU32(data.schema_version);
  hash.AddI64(data.dbu_per_millimeter);
  hash.AddU64(data.revision);
  hash.AddString(data.adapter_name);
  hash.AddString(data.adapter_version);

  hash.AddU64(static_cast<std::uint64_t>(data.layers.size()));
  for (const Layer& layer : data.layers) {
    AddEntityRef(hash, layer.ref);
    hash.AddU32(layer.routing_id);
    hash.AddString(layer.name);
    hash.AddI32(layer.physical_order);
    hash.AddByte(static_cast<std::uint8_t>(layer.type));
    hash.AddBool(layer.routable);
  }

  hash.AddU64(static_cast<std::uint64_t>(data.nets.size()));
  for (const Net& net : data.nets) {
    AddEntityRef(hash, net.ref);
    hash.AddString(net.name);
    hash.AddU64(static_cast<std::uint64_t>(net.terminals.size()));
    for (EntityRef terminal : net.terminals) {
      AddEntityRef(hash, terminal);
    }
  }

  hash.AddU64(static_cast<std::uint64_t>(data.terminals.size()));
  for (const Terminal& terminal : data.terminals) {
    AddEntityRef(hash, terminal.ref);
    AddEntityRef(hash, terminal.net);
    hash.AddString(terminal.component);
    hash.AddString(terminal.pin);
    AddPoint(hash, terminal.center);
    AddBox(hash, terminal.connection_region);
    hash.AddU64(static_cast<std::uint64_t>(terminal.layers.size()));
    for (LayerId layer : terminal.layers) {
      hash.AddU32(layer);
    }
  }

  hash.AddU64(static_cast<std::uint64_t>(data.obstacles.size()));
  for (const Obstacle& obstacle : data.obstacles) {
    AddEntityRef(hash, obstacle.ref);
    hash.AddU32(obstacle.layer);
    AddBox(hash, obstacle.bounds);
    hash.AddBool(obstacle.owner_net.has_value());
    if (obstacle.owner_net.has_value()) {
      AddEntityRef(hash, *obstacle.owner_net);
    }
    hash.AddString(obstacle.provenance);
  }

  AddEntityRef(hash, data.routing_profile.net);
  hash.AddI64(data.routing_profile.nominal_width);
  hash.AddI64(data.routing_profile.clearance);
  hash.AddU64(static_cast<std::uint64_t>(data.routing_profile.allowed_layers.size()));
  for (LayerId layer : data.routing_profile.allowed_layers) {
    hash.AddU32(layer);
  }
  hash.AddByte(data.routing_profile.allowed_headings);
  return hash.Finish();
}

}  // namespace

const Layer* BoardSnapshot::FindLayer(LayerId id) const noexcept {
  const auto layer = std::ranges::lower_bound(data_.layers, id, {}, &Layer::routing_id);
  return layer != data_.layers.end() && layer->routing_id == id ? &*layer : nullptr;
}

const Net* BoardSnapshot::FindNet(EntityRef ref) const noexcept {
  const auto net = std::ranges::lower_bound(data_.nets, ref.id, {},
                                            [](const Net& value) { return value.ref.id; });
  return net != data_.nets.end() && net->ref == ref ? &*net : nullptr;
}

const Terminal* BoardSnapshot::FindTerminal(EntityRef ref) const noexcept {
  const auto terminal = std::ranges::lower_bound(
      data_.terminals, ref.id, {}, [](const Terminal& value) { return value.ref.id; });
  return terminal != data_.terminals.end() && terminal->ref == ref ? &*terminal : nullptr;
}

std::span<const Obstacle> BoardSnapshot::ObstaclesOnLayer(LayerId layer) const noexcept {
  const auto first = std::ranges::lower_bound(data_.obstacles, layer, {}, &Obstacle::layer);
  const auto last =
      std::ranges::upper_bound(first, data_.obstacles.end(), layer, {}, &Obstacle::layer);
  return std::span<const Obstacle>(first, last);
}

BoardCreationResult CreateBoardSnapshot(BoardData data) {
  Normalize(data);
  if (std::optional<BoardValidationError> error = Validate(data); error.has_value()) {
    return std::move(*error);
  }
  const std::uint64_t content_hash = ContentHash(data);
  return BoardSnapshot(std::move(data), content_hash);
}

RoutingProfilePreparationResult PrepareRoutingProfile(const BoardSnapshot& board,
                                                      RoutingProfile profile) {
  std::ranges::sort(profile.allowed_layers);
  const Net* target_net = board.FindNet(profile.net);
  if (target_net == nullptr) {
    return Error(BoardValidationCode::kInvalidRoutingProfile,
                 "Routing profile refers to an unknown or stale net");
  }
  if (target_net->terminals.size() != 2) {
    return Error(BoardValidationCode::kNotM1Board,
                 "M1 routing profiles require exactly two terminals");
  }
  if (profile.nominal_width <= 0 || profile.nominal_width > kMaxAbsDbCoord ||
      profile.clearance < 0 || profile.clearance > kMaxAbsDbCoord ||
      profile.allowed_layers.empty() || profile.allowed_headings == 0 ||
      (profile.allowed_headings & static_cast<HeadingMask>(~kM1HeadingMask)) != 0) {
    return Error(BoardValidationCode::kInvalidRoutingProfile,
                 "Routing profile dimensions, layers, or headings are invalid");
  }
  if (std::ranges::adjacent_find(profile.allowed_layers) != profile.allowed_layers.end()) {
    return Error(BoardValidationCode::kInvalidRoutingProfile,
                 "Routing profile layers contain duplicates");
  }
  for (LayerId layer_id : profile.allowed_layers) {
    const Layer* layer = board.FindLayer(layer_id);
    if (layer == nullptr || !layer->routable || layer->type != LayerType::kSignal) {
      return Error(BoardValidationCode::kInvalidRoutingProfile,
                   "Routing profile contains an unavailable signal layer");
    }
  }
  for (EntityRef terminal_ref : target_net->terminals) {
    const Terminal* terminal = board.FindTerminal(terminal_ref);
    if (terminal == nullptr) {
      return Error(BoardValidationCode::kInvalidReference,
                   "Routing profile terminal reference is stale");
    }
    const bool has_routable_connection =
        std::ranges::any_of(terminal->layers, [&](LayerId terminal_layer) {
          return std::ranges::binary_search(profile.allowed_layers, terminal_layer);
        });
    if (!has_routable_connection) {
      return Error(BoardValidationCode::kInvalidRoutingProfile,
                   "Every routed-net terminal must intersect an allowed routing layer");
    }
  }
  return profile;
}

}  // namespace apgar::board_ir
