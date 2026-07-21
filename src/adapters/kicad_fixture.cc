#include "apgar/adapters/kicad_fixture.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <limits>
#include <map>
#include <new>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "apgar/board_ir/stable_hash.h"
#include "apgar/text/utf8.h"
#include "src/adapters/kicad_fixture_internal.h"

namespace apgar::adapters {
namespace {

using board_ir::AxisAlignedBox64;
using board_ir::BoardData;
using board_ir::DbCoord;
using board_ir::EntityId;
using board_ir::EntityRef;
using board_ir::LayerId;
using board_ir::Point64;

constexpr EntityId kEntityKeyMask = 0x0fffffffffffffffULL;
constexpr EntityId kLayerEntityDomain = 0x1000000000000000ULL;
constexpr EntityId kNetEntityDomain = 0x2000000000000000ULL;
constexpr EntityId kTerminalEntityDomain = 0x3000000000000000ULL;
constexpr EntityId kObstacleEntityDomain = 0x4000000000000000ULL;
constexpr std::size_t kMaximumParseDepth = 64;
constexpr std::size_t kMaximumNodes = 100'000;
constexpr std::size_t kMillimeterFractionDigits = 6;
constexpr DbCoord kMillimeterFractionUnits = 1'000'000;
static_assert(kKicadFixtureDbuPerMillimeter % kMillimeterFractionUnits == 0);
constexpr DbCoord kDbuPerFractionUnit = kKicadFixtureDbuPerMillimeter / kMillimeterFractionUnits;
static_assert(kDbuPerFractionUnit % 2 == 0,
              "The fixture unit scale must represent half-size pad boundaries exactly");

#if defined(APGAR_KICAD_FIXTURE_FAULT_TEST_VARIANT)
thread_local internal::KicadFixtureFaultPoint g_kicad_fixture_fault_point =
    internal::KicadFixtureFaultPoint::kNone;
#endif

void MaybeThrowKicadFixtureFaultForTesting(internal::KicadFixtureFaultPoint point) {
#if defined(APGAR_KICAD_FIXTURE_FAULT_TEST_VARIANT)
  if (g_kicad_fixture_fault_point == point) {
    g_kicad_fixture_fault_point = internal::KicadFixtureFaultPoint::kNone;
    throw std::bad_alloc();
  }
#else
  static_cast<void>(point);
#endif
}

enum class TokenKind : std::uint8_t {
  kLeftParen,
  kRightParen,
  kSymbol,
  kString,
  kEnd,
  kInvalid,
};

struct Token {
  TokenKind kind;
  std::size_t offset;
  std::string text;
  KicadFixtureErrorCode error_code = KicadFixtureErrorCode::kInvalidSyntax;
};

class Lexer {
 public:
  explicit Lexer(std::string_view input) : input_(input) {}

  [[nodiscard]] Token Next() {
    SkipTrivia();
    if (position_ == input_.size()) {
      return Token{.kind = TokenKind::kEnd, .offset = position_, .text = {}};
    }

    const std::size_t offset = position_;
    const char character = input_[position_++];
    if (character == '(') {
      return Token{.kind = TokenKind::kLeftParen, .offset = offset, .text = {}};
    }
    if (character == ')') {
      return Token{.kind = TokenKind::kRightParen, .offset = offset, .text = {}};
    }
    if (character == '"') {
      return ReadQuoted(offset);
    }

    if (static_cast<unsigned char>(character) > 0x7fU) {
      return Token{
          .kind = TokenKind::kInvalid, .offset = offset, .text = "KiCad symbols must be ASCII"};
    }
    std::string atom(1, character);
    while (position_ < input_.size()) {
      const char next = input_[position_];
      if (std::isspace(static_cast<unsigned char>(next)) != 0 || next == '(' || next == ')' ||
          next == ';') {
        break;
      }
      if (next == '"') {
        return Token{.kind = TokenKind::kInvalid,
                     .offset = position_,
                     .text = "quoted strings must be separated from KiCad symbols"};
      }
      if (static_cast<unsigned char>(next) > 0x7fU) {
        return Token{.kind = TokenKind::kInvalid,
                     .offset = position_,
                     .text = "KiCad symbols must be ASCII"};
      }
      if (atom.size() == kKicadFixtureMaximumTokenBytes) {
        return Token{.kind = TokenKind::kInvalid,
                     .offset = offset,
                     .text = "KiCad symbol exceeds the token-size limit",
                     .error_code = KicadFixtureErrorCode::kResourceLimit};
      }
      atom.push_back(next);
      ++position_;
    }
    return Token{.kind = TokenKind::kSymbol, .offset = offset, .text = std::move(atom)};
  }

 private:
  void SkipTrivia() {
    while (position_ < input_.size()) {
      const char character = input_[position_];
      if (std::isspace(static_cast<unsigned char>(character)) != 0) {
        ++position_;
        continue;
      }
      if (character == ';') {
        while (position_ < input_.size() && input_[position_] != '\n') {
          ++position_;
        }
        continue;
      }
      break;
    }
  }

  [[nodiscard]] Token ReadQuoted(std::size_t offset) {
    std::string value;
    while (position_ < input_.size()) {
      const char character = input_[position_++];
      if (character == '"') {
        if (!text::IsValidUtf8(value)) {
          return Token{.kind = TokenKind::kInvalid,
                       .offset = offset,
                       .text = "quoted string is not valid UTF-8"};
        }
        return Token{.kind = TokenKind::kString, .offset = offset, .text = std::move(value)};
      }
      if (character == '\\') {
        if (position_ == input_.size()) {
          break;
        }
        const char escaped = input_[position_++];
        if (escaped != '\\' && escaped != '"') {
          return Token{.kind = TokenKind::kInvalid,
                       .offset = position_ - 2,
                       .text = "unsupported quoted-string escape"};
        }
        if (value.size() == kKicadFixtureMaximumTokenBytes) {
          return Token{.kind = TokenKind::kInvalid,
                       .offset = offset,
                       .text = "quoted string exceeds the token-size limit",
                       .error_code = KicadFixtureErrorCode::kResourceLimit};
        }
        value.push_back(escaped);
        continue;
      }
      if (value.size() == kKicadFixtureMaximumTokenBytes) {
        return Token{.kind = TokenKind::kInvalid,
                     .offset = offset,
                     .text = "quoted string exceeds the token-size limit",
                     .error_code = KicadFixtureErrorCode::kResourceLimit};
      }
      value.push_back(character);
    }
    return Token{
        .kind = TokenKind::kInvalid, .offset = offset, .text = "unterminated quoted string"};
  }

  std::string_view input_;
  std::size_t position_ = 0;
};

struct SExpression {
  bool is_atom;
  TokenKind atom_kind;
  std::size_t offset;
  std::string atom;
  std::vector<SExpression> children;
};

using ParseResult = std::variant<SExpression, KicadFixtureError>;

class Parser {
 public:
  explicit Parser(std::string_view input) : lexer_(input), current_(lexer_.Next()) {}

  [[nodiscard]] ParseResult Parse() {
    std::optional<SExpression> root = ParseExpression(0);
    if (!root.has_value()) {
      return std::move(*error_);
    }
    if (current_.kind != TokenKind::kEnd) {
      return Error(KicadFixtureErrorCode::kInvalidSyntax, current_.offset,
                   "unexpected content after the root expression");
    }
    return std::move(*root);
  }

 private:
  [[nodiscard]] std::optional<SExpression> ParseExpression(std::size_t depth) {
    if (depth > kMaximumParseDepth) {
      Fail(KicadFixtureErrorCode::kResourceLimit, current_.offset,
           "S-expression nesting exceeds the fixture limit");
      return std::nullopt;
    }
    if (++node_count_ > kMaximumNodes) {
      Fail(KicadFixtureErrorCode::kResourceLimit, current_.offset,
           "S-expression node count exceeds the fixture limit");
      return std::nullopt;
    }
    if (current_.kind == TokenKind::kInvalid) {
      Fail(current_.error_code, current_.offset, current_.text);
      return std::nullopt;
    }
    if (current_.kind == TokenKind::kSymbol || current_.kind == TokenKind::kString) {
      SExpression atom{
          .is_atom = true,
          .atom_kind = current_.kind,
          .offset = current_.offset,
          .atom = std::move(current_.text),
          .children = {},
      };
      Advance();
      return atom;
    }
    if (current_.kind != TokenKind::kLeftParen) {
      Fail(KicadFixtureErrorCode::kInvalidSyntax, current_.offset,
           "expected an atom or left parenthesis");
      return std::nullopt;
    }

    const std::size_t offset = current_.offset;
    Advance();
    std::vector<SExpression> children;
    while (current_.kind != TokenKind::kRightParen) {
      if (current_.kind == TokenKind::kEnd) {
        Fail(KicadFixtureErrorCode::kInvalidSyntax, offset, "unterminated S-expression list");
        return std::nullopt;
      }
      std::optional<SExpression> child = ParseExpression(depth + 1);
      if (!child.has_value()) {
        return std::nullopt;
      }
      children.push_back(std::move(*child));
    }
    Advance();
    return SExpression{
        .is_atom = false,
        .atom_kind = TokenKind::kInvalid,
        .offset = offset,
        .atom = {},
        .children = std::move(children),
    };
  }

  void Advance() { current_ = lexer_.Next(); }

  void Fail(KicadFixtureErrorCode code, std::size_t offset, std::string message) {
    if (!error_.has_value()) {
      error_ = Error(code, offset, std::move(message));
    }
  }

  [[nodiscard]] static KicadFixtureError Error(KicadFixtureErrorCode code, std::size_t offset,
                                               std::string message) {
    return KicadFixtureError{
        .code = code,
        .offset = offset,
        .message = std::move(message),
    };
  }

  Lexer lexer_;
  Token current_;
  std::size_t node_count_ = 0;
  std::optional<KicadFixtureError> error_;
};

[[nodiscard]] std::string_view Head(const SExpression& expression) {
  if (expression.is_atom || expression.children.empty() || !expression.children.front().is_atom ||
      expression.children.front().atom_kind != TokenKind::kSymbol) {
    return {};
  }
  return expression.children.front().atom;
}

[[nodiscard]] const std::string* SymbolAt(const SExpression& expression, std::size_t index) {
  if (expression.is_atom || index >= expression.children.size() ||
      !expression.children[index].is_atom ||
      expression.children[index].atom_kind != TokenKind::kSymbol) {
    return nullptr;
  }
  return &expression.children[index].atom;
}

[[nodiscard]] const std::string* StringAt(const SExpression& expression, std::size_t index) {
  if (expression.is_atom || index >= expression.children.size() ||
      !expression.children[index].is_atom ||
      expression.children[index].atom_kind != TokenKind::kString) {
    return nullptr;
  }
  return &expression.children[index].atom;
}

[[nodiscard]] std::optional<std::uint32_t> ParseU32(std::string_view text) {
  std::uint32_t value = 0;
  const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
  if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) {
    return std::nullopt;
  }
  return value;
}

[[nodiscard]] std::optional<DbCoord> ParseMillimeters(std::string_view text) {
  if (text.empty()) {
    return std::nullopt;
  }
  bool negative = false;
  std::size_t position = 0;
  if (text[position] == '-') {
    negative = true;
    ++position;
  } else if (text[position] == '+') {
    return std::nullopt;
  }
  if (position == text.size()) {
    return std::nullopt;
  }

  __int128_t whole = 0;
  bool saw_digit = false;
  while (position < text.size() && text[position] != '.') {
    const char character = text[position++];
    if (character < '0' || character > '9') {
      return std::nullopt;
    }
    saw_digit = true;
    whole = whole * 10 + (character - '0');
    if (whole > board_ir::kMaxAbsDbCoord) {
      return std::nullopt;
    }
  }
  if (!saw_digit) {
    return std::nullopt;
  }

  __int128_t fraction = 0;
  std::size_t fraction_digits = 0;
  if (position < text.size()) {
    ++position;
    if (position == text.size()) {
      return std::nullopt;
    }
    while (position < text.size()) {
      const char character = text[position++];
      if (character < '0' || character > '9' || fraction_digits == kMillimeterFractionDigits) {
        return std::nullopt;
      }
      fraction = fraction * 10 + (character - '0');
      ++fraction_digits;
    }
  }
  while (fraction_digits < kMillimeterFractionDigits) {
    fraction *= 10;
    ++fraction_digits;
  }

  __int128_t dbu = whole * kKicadFixtureDbuPerMillimeter + fraction * kDbuPerFractionUnit;
  if (negative) {
    dbu = -dbu;
  }
  if (dbu < -board_ir::kMaxAbsDbCoord || dbu > board_ir::kMaxAbsDbCoord) {
    return std::nullopt;
  }
  return static_cast<DbCoord>(dbu);
}

[[nodiscard]] bool IsZeroDecimal(std::string_view text) {
  const std::optional<DbCoord> value = ParseMillimeters(text);
  return value.has_value() && *value == 0;
}

[[nodiscard]] bool IsCanonicalUuid(std::string_view value) noexcept {
  if (value.size() != 36) {
    return false;
  }
  for (std::size_t index = 0; index < value.size(); ++index) {
    if (index == 8 || index == 13 || index == 18 || index == 23) {
      if (value[index] != '-') {
        return false;
      }
      continue;
    }
    const char character = value[index];
    if (!((character >= '0' && character <= '9') || (character >= 'a' && character <= 'f'))) {
      return false;
    }
  }
  return true;
}

class FixtureImporter {
 public:
  FixtureImporter(const SExpression& root, std::string_view default_routing_net_name,
                  DbCoord nominal_width, DbCoord clearance,
                  const std::vector<std::string_view>* routable_net_names,
                  bool emit_routable_pad_obstacles)
      : root_(root),
        default_routing_net_name_(default_routing_net_name),
        nominal_width_(nominal_width),
        clearance_(clearance),
        routable_net_names_(routable_net_names),
        emit_routable_pad_obstacles_(emit_routable_pad_obstacles) {
    data_.adapter_name = "kicad-fixture";
    data_.dbu_per_millimeter = kKicadFixtureDbuPerMillimeter;
  }

  [[nodiscard]] KicadFixtureImportResult Import() {
    if (default_routing_net_name_.empty() || nominal_width_ <= 0 ||
        nominal_width_ > board_ir::kMaxAbsDbCoord || clearance_ < 0 ||
        clearance_ > board_ir::kMaxAbsDbCoord || !text::IsValidUtf8(default_routing_net_name_)) {
      return Error(KicadFixtureErrorCode::kInvalidSemantics, 0,
                   "Import configuration has an invalid target net, width, or "
                   "clearance");
    }

    if (root_.is_atom || Head(root_) != "kicad_pcb") {
      return Error(KicadFixtureErrorCode::kInvalidSemantics, root_.offset,
                   "Root expression must be kicad_pcb");
    }
    if (!ValidateRootConstructs() || !ParseVersion() || !ParseMetadata() || !ParseLayers() ||
        !ParseNets() || !ResolveRoutableNets() || !ParseFootprints() ||
        !ValidateRoutableTerminalCounts()) {
      return std::move(*error_);
    }

    const auto target = net_by_name_.find(default_routing_net_name_);
    if (target == net_by_name_.end()) {
      return Error(KicadFixtureErrorCode::kInvalidSemantics, root_.offset,
                   "Target net is not declared by the fixture");
    }
    data_.routing_profile = board_ir::RoutingProfile{
        .net = target->second,
        .nominal_width = nominal_width_,
        .clearance = clearance_,
        .allowed_layers = {},
        .allowed_headings = board_ir::kM1HeadingMask,
    };
    for (const board_ir::Layer& layer : data_.layers) {
      data_.routing_profile.allowed_layers.push_back(layer.routing_id);
    }

    board_ir::BoardCreationResult result = board_ir::CreateBoardSnapshot(std::move(data_));
    if (const auto* board = std::get_if<board_ir::BoardSnapshot>(&result)) {
      return std::move(*board);
    }
    const auto& board_error = std::get<board_ir::BoardValidationError>(result);
    return Error(KicadFixtureErrorCode::kBoardValidationFailed, root_.offset, board_error.message);
  }

 private:
  [[nodiscard]] bool ValidateRootConstructs() {
    static const std::set<std::string_view> kSupported = {
        "version", "generator", "general", "paper", "layers", "net", "footprint",
    };
    for (std::size_t index = 1; index < root_.children.size(); ++index) {
      const SExpression& child = root_.children[index];
      const std::string_view head = Head(child);
      if (head.empty()) {
        return Fail(KicadFixtureErrorCode::kInvalidSemantics, child.offset,
                    "Root entries must be named S-expression lists");
      }
      if (!kSupported.contains(head)) {
        return Fail(KicadFixtureErrorCode::kUnsupportedConstruct, child.offset,
                    "Unsupported root construct: " + std::string(head));
      }
    }
    return true;
  }

  [[nodiscard]] std::vector<const SExpression*> ChildrenNamed(const SExpression& parent,
                                                              std::string_view name) const {
    std::vector<const SExpression*> matches;
    for (std::size_t index = 1; index < parent.children.size(); ++index) {
      if (Head(parent.children[index]) == name) {
        matches.push_back(&parent.children[index]);
      }
    }
    return matches;
  }

  [[nodiscard]] const SExpression* UniqueChild(const SExpression& parent, std::string_view name,
                                               bool required) {
    const SExpression* match = nullptr;
    for (std::size_t index = 1; index < parent.children.size(); ++index) {
      if (Head(parent.children[index]) != name) {
        continue;
      }
      if (match != nullptr) {
        Fail(KicadFixtureErrorCode::kInvalidSemantics, parent.children[index].offset,
             std::string(name) + " must appear exactly once");
        return nullptr;
      }
      match = &parent.children[index];
    }
    if (required && match == nullptr) {
      Fail(KicadFixtureErrorCode::kInvalidSemantics, parent.offset,
           std::string(name) + " must appear exactly once");
      return nullptr;
    }
    return match;
  }

  [[nodiscard]] std::optional<EntityRef> StableEntityRef(EntityId domain, std::string_view key,
                                                         std::size_t offset) {
    const EntityId id = domain | (board_ir::StableHashString(key) & kEntityKeyMask);
    const auto [existing, inserted] = assigned_entity_keys_.emplace(id, std::string(key));
    if (!inserted && existing->second != key) {
      Fail(KicadFixtureErrorCode::kInvalidSemantics, offset,
           "Stable entity-ID hash collision in fixture input");
      return std::nullopt;
    }
    return EntityRef{.id = id, .generation = 0};
  }

  [[nodiscard]] bool RegisterObjectUuid(std::string_view uuid, std::size_t offset) {
    if (!IsCanonicalUuid(uuid)) {
      return Fail(KicadFixtureErrorCode::kInvalidSemantics, offset,
                  "Object UUID must use canonical lowercase form");
    }
    if (!object_uuids_.emplace(uuid).second) {
      return Fail(KicadFixtureErrorCode::kInvalidSemantics, offset,
                  "Object UUID is duplicated in the fixture");
    }
    return true;
  }

  [[nodiscard]] std::optional<std::string> ParseObjectUuid(const SExpression& parent) {
    const SExpression* uuid = UniqueChild(parent, "uuid", true);
    if (uuid == nullptr) {
      return std::nullopt;
    }
    const std::string* value = StringAt(*uuid, 1);
    if (uuid->children.size() != 2 || value == nullptr) {
      Fail(KicadFixtureErrorCode::kInvalidSemantics, uuid->offset,
           "Object UUID must contain exactly one quoted value");
      return std::nullopt;
    }
    if (!RegisterObjectUuid(*value, uuid->offset)) {
      return std::nullopt;
    }
    return *value;
  }

  [[nodiscard]] bool ParseVersion() {
    const SExpression* version = UniqueChild(root_, "version", true);
    if (version == nullptr) {
      return false;
    }
    const std::string* value = SymbolAt(*version, 1);
    if (version->children.size() != 2 || value == nullptr || *value != "20240108") {
      return Fail(KicadFixtureErrorCode::kUnsupportedConstruct, version->offset,
                  "Fixture adapter supports KiCad schema 20240108 only");
    }
    data_.adapter_version = *value;
    return true;
  }

  [[nodiscard]] bool ParseMetadata() {
    const SExpression* generator = UniqueChild(root_, "generator", true);
    const SExpression* general = UniqueChild(root_, "general", true);
    const SExpression* paper = UniqueChild(root_, "paper", true);
    if (generator == nullptr || general == nullptr || paper == nullptr) {
      return false;
    }

    const std::string* generator_name = SymbolAt(*generator, 1);
    if (generator->children.size() != 2 || generator_name == nullptr || generator_name->empty()) {
      return Fail(KicadFixtureErrorCode::kInvalidSemantics, generator->offset,
                  "Generator must contain exactly one KiCad symbol");
    }

    if (general->children.size() != 2) {
      return Fail(KicadFixtureErrorCode::kUnsupportedConstruct, general->offset,
                  "General metadata must contain thickness only");
    }
    const SExpression& thickness = general->children[1];
    const std::string* thickness_text = SymbolAt(thickness, 1);
    const std::optional<DbCoord> thickness_value =
        thickness_text == nullptr ? std::nullopt : ParseMillimeters(*thickness_text);
    if (Head(thickness) != "thickness" || thickness.children.size() != 2 ||
        !thickness_value.has_value() || *thickness_value <= 0) {
      return Fail(KicadFixtureErrorCode::kInvalidSemantics, thickness.offset,
                  "General thickness must be one positive decimal value");
    }

    const std::string* paper_name = StringAt(*paper, 1);
    if (paper->children.size() != 2 || paper_name == nullptr || paper_name->empty()) {
      return Fail(KicadFixtureErrorCode::kInvalidSemantics, paper->offset,
                  "Paper must contain exactly one quoted name");
    }
    return true;
  }

  [[nodiscard]] bool ParseLayers() {
    const SExpression* layers = UniqueChild(root_, "layers", true);
    if (layers == nullptr) {
      return false;
    }
    if (layers->children.size() != 3 && layers->children.size() != 5) {
      return Fail(KicadFixtureErrorCode::kUnsupportedConstruct, layers->offset,
                  "Fixture boards require exactly two or four signal layers");
    }

    std::set<LayerId> routing_ids;
    for (std::size_t index = 1; index < layers->children.size(); ++index) {
      const SExpression& entry = layers->children[index];
      const std::string* id_text = SymbolAt(entry, 0);
      const std::string* name = StringAt(entry, 1);
      const std::string* type = SymbolAt(entry, 2);
      const std::optional<std::uint32_t> id =
          id_text == nullptr ? std::nullopt : ParseU32(*id_text);
      if (entry.is_atom || entry.children.size() != 3 || !id.has_value() || name == nullptr ||
          type == nullptr || *type != "signal" || !name->ends_with(".Cu")) {
        return Fail(KicadFixtureErrorCode::kUnsupportedConstruct, entry.offset,
                    "Layers must be KiCad signal copper layers");
      }
      if (layer_by_name_.contains(*name) || !routing_ids.emplace(*id).second) {
        return Fail(KicadFixtureErrorCode::kInvalidSemantics, entry.offset,
                    "KiCad copper layer IDs and names must be unique");
      }
      if (*id > 31 || (*id == 0 && *name != "F.Cu") || (*id == 31 && *name != "B.Cu") ||
          (*name == "F.Cu" && *id != 0) || (*name == "B.Cu" && *id != 31)) {
        return Fail(KicadFixtureErrorCode::kInvalidSemantics, entry.offset,
                    "KiCad copper layer ID and name do not describe the same stack position");
      }
      const LayerId routing_id = *id;
      const std::optional<EntityRef> layer_ref =
          StableEntityRef(kLayerEntityDomain, *name, entry.offset);
      if (!layer_ref.has_value()) {
        return false;
      }
      data_.layers.push_back(board_ir::Layer{
          .ref = *layer_ref,
          .routing_id = routing_id,
          .name = *name,
          .physical_order = 0,
          .type = board_ir::LayerType::kSignal,
          .routable = true,
      });
      layer_by_name_.emplace(*name, routing_id);
    }
    std::ranges::sort(data_.layers, {}, &board_ir::Layer::routing_id);
    for (std::size_t index = 0; index < data_.layers.size(); ++index) {
      data_.layers[index].physical_order = static_cast<std::int32_t>(index);
    }
    return true;
  }

  [[nodiscard]] bool ParseNets() {
    bool saw_net_zero = false;
    for (const SExpression* net_expression : ChildrenNamed(root_, "net")) {
      const std::string* number_text = SymbolAt(*net_expression, 1);
      const std::string* name = StringAt(*net_expression, 2);
      const std::optional<std::uint32_t> number =
          number_text == nullptr ? std::nullopt : ParseU32(*number_text);
      if (net_expression->children.size() != 3 || !number.has_value() || name == nullptr) {
        return Fail(KicadFixtureErrorCode::kInvalidSemantics, net_expression->offset,
                    "Net declaration is malformed");
      }
      if (*number == 0) {
        if (!name->empty() || saw_net_zero) {
          return Fail(KicadFixtureErrorCode::kInvalidSemantics, net_expression->offset,
                      "Net zero must be unnamed and declared at most once");
        }
        saw_net_zero = true;
        continue;
      }
      if (name->empty() || net_index_by_number_.contains(*number) || net_by_name_.contains(*name)) {
        return Fail(KicadFixtureErrorCode::kInvalidSemantics, net_expression->offset,
                    "Net IDs and names must be non-empty and unique");
      }
      const std::optional<EntityRef> ref =
          StableEntityRef(kNetEntityDomain, *name, net_expression->offset);
      if (!ref.has_value()) {
        return false;
      }
      net_index_by_number_.emplace(*number, data_.nets.size());
      net_by_name_.emplace(*name, *ref);
      data_.nets.push_back(board_ir::Net{
          .ref = *ref,
          .name = *name,
          .terminals = {},
      });
    }
    if (data_.nets.empty()) {
      return Fail(KicadFixtureErrorCode::kInvalidSemantics, root_.offset,
                  "Fixture must declare at least one named net");
    }
    return true;
  }

  [[nodiscard]] bool ParseFootprints() {
    for (const SExpression* footprint : ChildrenNamed(root_, "footprint")) {
      if (!ParseFootprint(*footprint)) {
        return false;
      }
    }
    return true;
  }

  [[nodiscard]] bool ResolveRoutableNets() {
    if (routable_net_names_ == nullptr) {
      const auto target = net_by_name_.find(default_routing_net_name_);
      if (target == net_by_name_.end()) {
        return Fail(KicadFixtureErrorCode::kInvalidSemantics, root_.offset,
                    "Target net is not declared by the fixture");
      }
      routable_net_refs_.push_back(target->second);
      return true;
    }

    routable_net_refs_.reserve(routable_net_names_->size());
    for (std::string_view name : *routable_net_names_) {
      const auto net = net_by_name_.find(name);
      if (net == net_by_name_.end()) {
        return Fail(KicadFixtureErrorCode::kInvalidSemantics, root_.offset,
                    "Routable net roster names an undeclared fixture net: " + std::string(name));
      }
      routable_net_refs_.push_back(net->second);
    }
    std::ranges::sort(routable_net_refs_, [](EntityRef left, EntityRef right) {
      return std::tie(left.id, left.generation) < std::tie(right.id, right.generation);
    });
    return true;
  }

  [[nodiscard]] bool IsRoutableNet(EntityRef net) const {
    return std::ranges::binary_search(routable_net_refs_, net, [](EntityRef left, EntityRef right) {
      return std::tie(left.id, left.generation) < std::tie(right.id, right.generation);
    });
  }

  [[nodiscard]] bool ValidateRoutableTerminalCounts() {
    for (EntityRef net_ref : routable_net_refs_) {
      const auto net = std::ranges::find(data_.nets, net_ref, &board_ir::Net::ref);
      if (net == data_.nets.end() || net->terminals.size() != 2) {
        return Fail(KicadFixtureErrorCode::kInvalidSemantics, root_.offset,
                    "Every routable fixture net must contain exactly two terminal pads");
      }
    }
    return true;
  }

  [[nodiscard]] bool ParseFootprint(const SExpression& footprint) {
    const std::string* component = StringAt(footprint, 1);
    if (component == nullptr || component->empty()) {
      return Fail(KicadFixtureErrorCode::kInvalidSemantics, footprint.offset,
                  "Footprint name is required");
    }
    static const std::set<std::string_view> kSupported = {"layer", "at", "uuid", "pad"};
    for (std::size_t index = 2; index < footprint.children.size(); ++index) {
      const std::string_view head = Head(footprint.children[index]);
      if (!kSupported.contains(head)) {
        return Fail(KicadFixtureErrorCode::kUnsupportedConstruct, footprint.children[index].offset,
                    "Unsupported footprint construct: " + std::string(head));
      }
    }

    const SExpression* layer = UniqueChild(footprint, "layer", true);
    const SExpression* at = UniqueChild(footprint, "at", true);
    const std::optional<std::string> uuid = ParseObjectUuid(footprint);
    if (layer == nullptr || at == nullptr || !uuid.has_value()) {
      return false;
    }
    const std::string* layer_name = StringAt(*layer, 1);
    if (layer->children.size() != 2 || layer_name == nullptr ||
        !layer_by_name_.contains(*layer_name)) {
      return Fail(KicadFixtureErrorCode::kUnsupportedConstruct, layer->offset,
                  "Footprint must be placed on an imported copper layer");
    }
    std::optional<Point64> position = ParseAt(*at);
    if (!position.has_value()) {
      return false;
    }

    const std::vector<const SExpression*> pads = ChildrenNamed(footprint, "pad");
    if (pads.empty()) {
      return Fail(KicadFixtureErrorCode::kInvalidSemantics, footprint.offset,
                  "Fixture footprints must contain at least one pad");
    }
    for (const SExpression* pad : pads) {
      if (!ParsePad(*pad, *component, *layer_name, *position)) {
        return false;
      }
    }
    return true;
  }

  [[nodiscard]] std::optional<Point64> ParseAt(const SExpression& at) {
    const std::string* x_text = SymbolAt(at, 1);
    const std::string* y_text = SymbolAt(at, 2);
    const std::optional<DbCoord> x = x_text == nullptr ? std::nullopt : ParseMillimeters(*x_text);
    const std::optional<DbCoord> y = y_text == nullptr ? std::nullopt : ParseMillimeters(*y_text);
    if ((at.children.size() != 3 && at.children.size() != 4) || !x.has_value() || !y.has_value()) {
      Fail(KicadFixtureErrorCode::kInvalidSemantics, at.offset,
           "Position must contain exact millimeter X and Y values");
      return std::nullopt;
    }
    if (at.children.size() == 4) {
      const std::string* rotation = SymbolAt(at, 3);
      if (rotation == nullptr || !IsZeroDecimal(*rotation)) {
        Fail(KicadFixtureErrorCode::kUnsupportedConstruct, at.offset,
             "Rotated fixture objects are not supported");
        return std::nullopt;
      }
    }
    return Point64{.x = *x, .y = *y};
  }

  [[nodiscard]] bool ParsePad(const SExpression& pad, const std::string& component,
                              const std::string& footprint_layer, Point64 footprint_position) {
    const std::string* pin = StringAt(pad, 1);
    const std::string* pad_type = SymbolAt(pad, 2);
    const std::string* shape = SymbolAt(pad, 3);
    if (pin == nullptr || pin->empty() || pad_type == nullptr || shape == nullptr ||
        *shape != "rect" || (*pad_type != "smd" && *pad_type != "thru_hole")) {
      return Fail(KicadFixtureErrorCode::kUnsupportedConstruct, pad.offset,
                  "Fixture pads must be unrotated smd or thru_hole rectangles");
    }

    static const std::set<std::string_view> kSupported = {"at",     "size", "drill",
                                                          "layers", "net",  "uuid"};
    for (std::size_t index = 4; index < pad.children.size(); ++index) {
      const std::string_view head = Head(pad.children[index]);
      if (!kSupported.contains(head)) {
        return Fail(KicadFixtureErrorCode::kUnsupportedConstruct, pad.children[index].offset,
                    "Unsupported pad construct: " + std::string(head));
      }
    }

    const SExpression* at = UniqueChild(pad, "at", true);
    const SExpression* size = UniqueChild(pad, "size", true);
    const SExpression* layers = UniqueChild(pad, "layers", true);
    const SExpression* net = UniqueChild(pad, "net", true);
    const SExpression* drill = UniqueChild(pad, "drill", *pad_type == "thru_hole");
    const std::optional<std::string> uuid = ParseObjectUuid(pad);
    if (at == nullptr || size == nullptr || layers == nullptr || net == nullptr ||
        !uuid.has_value() || (*pad_type == "thru_hole" && drill == nullptr)) {
      return false;
    }
    if (*pad_type == "smd" && drill != nullptr) {
      return Fail(KicadFixtureErrorCode::kUnsupportedConstruct, drill->offset,
                  "SMD fixture pads cannot declare a drill");
    }
    std::optional<Point64> local_position = ParseAt(*at);
    if (!local_position.has_value()) {
      return false;
    }
    const std::optional<Point64> center =
        CheckedAdd(footprint_position, *local_position, at->offset);
    if (!center.has_value()) {
      return false;
    }

    const std::string* width_text = SymbolAt(*size, 1);
    const std::string* height_text = SymbolAt(*size, 2);
    const std::optional<DbCoord> width =
        width_text == nullptr ? std::nullopt : ParseMillimeters(*width_text);
    const std::optional<DbCoord> height =
        height_text == nullptr ? std::nullopt : ParseMillimeters(*height_text);
    if (size->children.size() != 3 || !width.has_value() || *width <= 0 || !height.has_value() ||
        *height <= 0) {
      return Fail(KicadFixtureErrorCode::kInvalidSemantics, size->offset,
                  "Pad size must be positive and exactly representable in Board IR");
    }
    if (drill != nullptr) {
      const std::string* drill_text = SymbolAt(*drill, 1);
      const std::optional<DbCoord> drill_size =
          drill_text == nullptr ? std::nullopt : ParseMillimeters(*drill_text);
      if (drill->children.size() != 2 || !drill_size.has_value() || *drill_size <= 0 ||
          *drill_size >= std::min(*width, *height)) {
        return Fail(KicadFixtureErrorCode::kInvalidSemantics, drill->offset,
                    "Through-hole drill must be positive and smaller than the pad");
      }
    }
    const std::optional<AxisAlignedBox64> bounds =
        CheckedPadBounds(*center, *width, *height, pad.offset, component, *pin);
    if (!bounds.has_value()) {
      return false;
    }

    const std::string* net_number_text = SymbolAt(*net, 1);
    const std::string* net_name = StringAt(*net, 2);
    const std::optional<std::uint32_t> net_number =
        net_number_text == nullptr ? std::nullopt : ParseU32(*net_number_text);
    if (net->children.size() != 3 || !net_number.has_value() || net_name == nullptr) {
      return Fail(KicadFixtureErrorCode::kInvalidSemantics, net->offset,
                  "Pad net declaration is malformed");
    }
    std::optional<EntityRef> owner_net;
    std::optional<std::size_t> owner_net_index;
    if (*net_number == 0) {
      if (!net_name->empty()) {
        return Fail(KicadFixtureErrorCode::kInvalidSemantics, net->offset,
                    "Net-zero pads must use an empty net name");
      }
    } else {
      const auto net_index = net_index_by_number_.find(*net_number);
      if (net_index == net_index_by_number_.end() ||
          data_.nets[net_index->second].name != *net_name) {
        return Fail(KicadFixtureErrorCode::kInvalidSemantics, net->offset,
                    "Pad refers to an undeclared or mismatched net");
      }
      owner_net = data_.nets[net_index->second].ref;
      owner_net_index = net_index->second;
    }

    const std::optional<std::vector<LayerId>> pad_layers =
        ParsePadLayers(*layers, *pad_type, footprint_layer);
    if (!pad_layers.has_value()) {
      return false;
    }

    const bool routable_pad = owner_net.has_value() && IsRoutableNet(*owner_net);
    if (routable_pad) {
      const std::optional<EntityRef> terminal_ref =
          StableEntityRef(kTerminalEntityDomain, *uuid, pad.offset);
      if (!terminal_ref.has_value()) {
        return false;
      }
      data_.terminals.push_back(board_ir::Terminal{
          .ref = *terminal_ref,
          .net = *owner_net,
          .component = component,
          .pin = *pin,
          .center = *center,
          .connection_region = *bounds,
          .layers = *pad_layers,
      });
      data_.nets[*owner_net_index].terminals.push_back(*terminal_ref);
      if (!emit_routable_pad_obstacles_) {
        return true;
      }
    }

    for (LayerId layer : *pad_layers) {
      const auto layer_entry = std::ranges::find(data_.layers, layer, &board_ir::Layer::routing_id);
      if (layer_entry == data_.layers.end()) {
        return Fail(KicadFixtureErrorCode::kInvalidSemantics, layers->offset,
                    "Pad resolved to an unknown imported copper layer");
      }
      const std::string obstacle_key = *uuid + "/" + layer_entry->name;
      const std::optional<EntityRef> obstacle_ref =
          StableEntityRef(kObstacleEntityDomain, obstacle_key, pad.offset);
      if (!obstacle_ref.has_value()) {
        return false;
      }
      data_.obstacles.push_back(board_ir::Obstacle{
          .ref = *obstacle_ref,
          .layer = layer,
          .bounds = *bounds,
          .owner_net = owner_net,
          .provenance = component + "/pad-" + *pin,
      });
    }
    return true;
  }

  [[nodiscard]] std::optional<std::vector<LayerId>> ParsePadLayers(
      const SExpression& layers, const std::string& pad_type, const std::string& footprint_layer) {
    if (layers.children.size() < 2) {
      Fail(KicadFixtureErrorCode::kInvalidSemantics, layers.offset, "Pad layers list is empty");
      return std::nullopt;
    }
    std::vector<LayerId> copper_layers;
    std::set<std::string, std::less<>> declared_layers;
    bool all_copper = false;
    const std::string expected_mask =
        footprint_layer.substr(0, footprint_layer.size() - std::string_view("Cu").size()) + "Mask";
    const std::string expected_paste =
        footprint_layer.substr(0, footprint_layer.size() - std::string_view("Cu").size()) + "Paste";
    for (std::size_t index = 1; index < layers.children.size(); ++index) {
      const std::string* layer_name = StringAt(layers, index);
      if (layer_name == nullptr) {
        Fail(KicadFixtureErrorCode::kInvalidSemantics, layers.offset,
             "Pad layer names must be atoms");
        return std::nullopt;
      }
      if (!declared_layers.emplace(*layer_name).second) {
        Fail(KicadFixtureErrorCode::kInvalidSemantics, layers.children[index].offset,
             "Pad layer declarations must be unique");
        return std::nullopt;
      }
      if (*layer_name == "*.Cu") {
        all_copper = true;
        continue;
      }
      const auto layer = layer_by_name_.find(*layer_name);
      if (layer != layer_by_name_.end()) {
        copper_layers.push_back(layer->second);
        continue;
      }
      const bool auxiliary_layer_is_valid =
          (pad_type == "thru_hole" && *layer_name == "*.Mask") ||
          (pad_type == "smd" && (*layer_name == expected_mask || *layer_name == expected_paste));
      if (auxiliary_layer_is_valid) {
        continue;
      }
      Fail(KicadFixtureErrorCode::kUnsupportedConstruct, layers.children[index].offset,
           "Pad auxiliary layer is unsupported or on the wrong copper side: " + *layer_name);
      return std::nullopt;
    }

    if (pad_type == "thru_hole") {
      if (!all_copper || !copper_layers.empty()) {
        Fail(KicadFixtureErrorCode::kUnsupportedConstruct, layers.offset,
             "Through-hole fixture pads must use *.Cu");
        return std::nullopt;
      }
      for (const board_ir::Layer& layer : data_.layers) {
        copper_layers.push_back(layer.routing_id);
      }
    } else if (all_copper || copper_layers.size() != 1 ||
               layer_by_name_.at(footprint_layer) != copper_layers.front()) {
      Fail(KicadFixtureErrorCode::kUnsupportedConstruct, layers.offset,
           "SMD fixture pads must use their footprint copper layer");
      return std::nullopt;
    }
    return copper_layers;
  }

  [[nodiscard]] std::optional<Point64> CheckedAdd(Point64 left, Point64 right, std::size_t offset) {
    const __int128_t x = static_cast<__int128_t>(left.x) + right.x;
    const __int128_t y = static_cast<__int128_t>(left.y) + right.y;
    if (x < -board_ir::kMaxAbsDbCoord || x > board_ir::kMaxAbsDbCoord ||
        y < -board_ir::kMaxAbsDbCoord || y > board_ir::kMaxAbsDbCoord) {
      Fail(KicadFixtureErrorCode::kInvalidSemantics, offset,
           "Absolute pad coordinate exceeds the Board IR range");
      return std::nullopt;
    }
    return Point64{.x = static_cast<DbCoord>(x), .y = static_cast<DbCoord>(y)};
  }

  [[nodiscard]] std::optional<AxisAlignedBox64> CheckedPadBounds(Point64 center, DbCoord width,
                                                                 DbCoord height, std::size_t offset,
                                                                 std::string_view component,
                                                                 std::string_view pin) {
    const __int128_t minimum_x = static_cast<__int128_t>(center.x) - width / 2;
    const __int128_t maximum_x = static_cast<__int128_t>(center.x) + width / 2;
    const __int128_t minimum_y = static_cast<__int128_t>(center.y) - height / 2;
    const __int128_t maximum_y = static_cast<__int128_t>(center.y) + height / 2;
    if (minimum_x < -board_ir::kMaxAbsDbCoord || maximum_x > board_ir::kMaxAbsDbCoord ||
        minimum_y < -board_ir::kMaxAbsDbCoord || maximum_y > board_ir::kMaxAbsDbCoord) {
      Fail(KicadFixtureErrorCode::kInvalidSemantics, offset,
           "Pad " + std::string(component) + "/pad-" + std::string(pin) +
               " extends beyond the Board IR coordinate envelope");
      return std::nullopt;
    }
    return AxisAlignedBox64{
        .min = Point64{.x = static_cast<DbCoord>(minimum_x), .y = static_cast<DbCoord>(minimum_y)},
        .max = Point64{.x = static_cast<DbCoord>(maximum_x), .y = static_cast<DbCoord>(maximum_y)},
    };
  }

  bool Fail(KicadFixtureErrorCode code, std::size_t offset, std::string message) {
    if (!error_.has_value()) {
      error_ = Error(code, offset, std::move(message));
    }
    return false;
  }

  [[nodiscard]] static KicadFixtureError Error(KicadFixtureErrorCode code, std::size_t offset,
                                               std::string message) {
    return KicadFixtureError{
        .code = code,
        .offset = offset,
        .message = std::move(message),
    };
  }

  const SExpression& root_;
  std::string_view default_routing_net_name_;
  DbCoord nominal_width_;
  DbCoord clearance_;
  const std::vector<std::string_view>* routable_net_names_;
  bool emit_routable_pad_obstacles_;
  BoardData data_;
  std::map<std::string, LayerId, std::less<>> layer_by_name_;
  std::map<std::uint32_t, std::size_t> net_index_by_number_;
  std::map<std::string, EntityRef, std::less<>> net_by_name_;
  std::vector<EntityRef> routable_net_refs_;
  std::map<EntityId, std::string> assigned_entity_keys_;
  std::set<std::string, std::less<>> object_uuids_;
  std::optional<KicadFixtureError> error_;
};

}  // namespace

namespace {

using MultiNetConfigPreflightResult =
    std::variant<std::vector<std::string_view>, KicadFixtureError>;

[[nodiscard]] MultiNetConfigPreflightResult PreflightMultiNetConfig(
    const KicadMultiNetFixtureImportConfig& config) {
  if (config.routable_net_names.empty()) {
    return KicadFixtureError{.code = KicadFixtureErrorCode::kInvalidSemantics,
                             .offset = 0,
                             .message = "Multi-net fixture roster must not be empty"};
  }
  if (config.routable_net_names.size() > kKicadFixtureMaximumRoutableNets) {
    return KicadFixtureError{.code = KicadFixtureErrorCode::kResourceLimit,
                             .offset = 0,
                             .message = "Multi-net fixture roster exceeds the net-count limit"};
  }
  if (config.default_routing_net_name.size() > kKicadFixtureMaximumTokenBytes) {
    return KicadFixtureError{
        .code = KicadFixtureErrorCode::kResourceLimit,
        .offset = 0,
        .message = "Multi-net fixture default routing net exceeds the name-size limit"};
  }
  if (config.default_routing_net_name.empty() || config.nominal_width <= 0 ||
      config.nominal_width > board_ir::kMaxAbsDbCoord || config.clearance < 0 ||
      config.clearance > board_ir::kMaxAbsDbCoord ||
      !text::IsValidUtf8(config.default_routing_net_name)) {
    return KicadFixtureError{
        .code = KicadFixtureErrorCode::kInvalidSemantics,
        .offset = 0,
        .message =
            "Multi-net import configuration has an invalid default net, width, or clearance"};
  }

  try {
    std::size_t aggregate_name_bytes = 0;
    for (const std::string& name : config.routable_net_names) {
      if (name.size() > kKicadFixtureMaximumTokenBytes ||
          aggregate_name_bytes > kKicadFixtureMaximumRoutableNetNameBytes - name.size()) {
        return KicadFixtureError{.code = KicadFixtureErrorCode::kResourceLimit,
                                 .offset = 0,
                                 .message = "Multi-net fixture roster exceeds a name-size limit"};
      }
      aggregate_name_bytes += name.size();
    }

    std::vector<std::string_view> names;
    names.reserve(config.routable_net_names.size());
    for (const std::string& name : config.routable_net_names) {
      names.push_back(name);
    }
    std::ranges::sort(names);
    bool contains_default = false;
    for (std::string_view name : names) {
      if (name.empty() || !text::IsValidUtf8(name)) {
        return KicadFixtureError{
            .code = KicadFixtureErrorCode::kInvalidSemantics,
            .offset = 0,
            .message = "Multi-net fixture roster names must be nonempty valid UTF-8"};
      }
      contains_default = contains_default || name == config.default_routing_net_name;
    }
    if (std::ranges::adjacent_find(names) != names.end()) {
      return KicadFixtureError{.code = KicadFixtureErrorCode::kInvalidSemantics,
                               .offset = 0,
                               .message = "Multi-net fixture roster contains a duplicate net"};
    }
    if (!contains_default) {
      return KicadFixtureError{
          .code = KicadFixtureErrorCode::kInvalidSemantics,
          .offset = 0,
          .message = "Multi-net fixture roster must contain the default routing net"};
    }
    return names;
  } catch (const std::bad_alloc&) {
    return KicadFixtureError{.code = KicadFixtureErrorCode::kResourceLimit,
                             .offset = 0,
                             .message = {},
                             .invariant_id = "adapter.kicad.roster.host_memory.v1"};
  } catch (const std::length_error&) {
    return KicadFixtureError{.code = KicadFixtureErrorCode::kResourceLimit,
                             .offset = 0,
                             .message = {},
                             .invariant_id = "adapter.kicad.roster.host_container.v1"};
  }
}

[[nodiscard]] std::optional<KicadFixtureError> ValidateInputSize(std::string_view contents) {
  if (contents.size() <= kKicadFixtureMaximumInputBytes) {
    return std::nullopt;
  }
  return KicadFixtureError{
      .code = KicadFixtureErrorCode::kResourceLimit,
      .offset = kKicadFixtureMaximumInputBytes,
      .message = "KiCad fixture exceeds the input-size limit",
  };
}

}  // namespace

KicadFixtureImportResult ImportKicadFixture(std::string_view contents,
                                            const KicadFixtureImportConfig& config) {
  if (std::optional<KicadFixtureError> error = ValidateInputSize(contents); error.has_value()) {
    return std::move(*error);
  }
  ParseResult parsed = Parser(contents).Parse();
  if (const auto* error = std::get_if<KicadFixtureError>(&parsed)) {
    return *error;
  }
  const SExpression& root = std::get<SExpression>(parsed);
  return FixtureImporter(root, config.target_net_name, config.nominal_width, config.clearance,
                         nullptr, false)
      .Import();
}

KicadFixtureImportResult ImportKicadMultiNetFixture(
    std::string_view contents, const KicadMultiNetFixtureImportConfig& config) {
  try {
    if (std::optional<KicadFixtureError> error = ValidateInputSize(contents); error.has_value()) {
      return std::move(*error);
    }
    MaybeThrowKicadFixtureFaultForTesting(internal::KicadFixtureFaultPoint::kMultiNetPreflight);
    MultiNetConfigPreflightResult preflight = PreflightMultiNetConfig(config);
    if (const auto* error = std::get_if<KicadFixtureError>(&preflight); error != nullptr) {
      return *error;
    }
    const std::vector<std::string_view>& routable_net_names =
        std::get<std::vector<std::string_view>>(preflight);
    MaybeThrowKicadFixtureFaultForTesting(internal::KicadFixtureFaultPoint::kMultiNetParse);
    ParseResult parsed = Parser(contents).Parse();
    if (const auto* error = std::get_if<KicadFixtureError>(&parsed)) {
      return *error;
    }
    const SExpression& root = std::get<SExpression>(parsed);
    MaybeThrowKicadFixtureFaultForTesting(internal::KicadFixtureFaultPoint::kMultiNetImport);
    return FixtureImporter(root, config.default_routing_net_name, config.nominal_width,
                           config.clearance, &routable_net_names, true)
        .Import();
  } catch (const std::bad_alloc&) {
    return KicadFixtureError{.code = KicadFixtureErrorCode::kResourceLimit,
                             .offset = 0,
                             .message = {},
                             .invariant_id = "adapter.kicad.import.host_memory.v1"};
  } catch (const std::length_error&) {
    return KicadFixtureError{.code = KicadFixtureErrorCode::kResourceLimit,
                             .offset = 0,
                             .message = {},
                             .invariant_id = "adapter.kicad.import.host_container.v1"};
  }
}

namespace internal {

void SetKicadFixtureFaultPointForTesting(KicadFixtureFaultPoint point) noexcept {
#if defined(APGAR_KICAD_FIXTURE_FAULT_TEST_VARIANT)
  g_kicad_fixture_fault_point = point;
#else
  static_cast<void>(point);
#endif
}

}  // namespace internal

}  // namespace apgar::adapters
