#ifndef APGAR_BOARD_IR_STABLE_HASH_H_
#define APGAR_BOARD_IR_STABLE_HASH_H_

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace apgar::board_ir {

// Board IR v1 uses this byte-stable FNV-1a encoding for persistent semantic
// identities and normalized content fingerprints. It is not cryptographic.
class StableHashBuilder {
 public:
  void AddByte(std::uint8_t value) noexcept {
    value_ ^= value;
    value_ *= kPrime;
  }

  void AddBool(bool value) noexcept { AddByte(value ? 1U : 0U); }

  void AddU32(std::uint32_t value) noexcept {
    for (std::size_t index = 0; index < sizeof(value); ++index) {
      AddByte(static_cast<std::uint8_t>(value & 0xffU));
      value >>= 8U;
    }
  }

  void AddU64(std::uint64_t value) noexcept {
    for (std::size_t index = 0; index < sizeof(value); ++index) {
      AddByte(static_cast<std::uint8_t>(value & 0xffU));
      value >>= 8U;
    }
  }

  void AddI32(std::int32_t value) noexcept { AddU32(static_cast<std::uint32_t>(value)); }

  void AddI64(std::int64_t value) noexcept { AddU64(static_cast<std::uint64_t>(value)); }

  void AddString(std::string_view value) noexcept {
    AddU64(static_cast<std::uint64_t>(value.size()));
    for (const char character : value) {
      AddByte(static_cast<std::uint8_t>(character));
    }
  }

  [[nodiscard]] std::uint64_t Finish() const noexcept { return value_; }

 private:
  static constexpr std::uint64_t kOffsetBasis = 14695981039346656037ULL;
  static constexpr std::uint64_t kPrime = 1099511628211ULL;

  std::uint64_t value_ = kOffsetBasis;
};

[[nodiscard]] inline std::uint64_t StableHashString(std::string_view value) noexcept {
  StableHashBuilder hash;
  for (const char character : value) {
    hash.AddByte(static_cast<std::uint8_t>(character));
  }
  return hash.Finish();
}

}  // namespace apgar::board_ir

#endif  // APGAR_BOARD_IR_STABLE_HASH_H_
