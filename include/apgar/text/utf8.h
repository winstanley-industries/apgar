#ifndef APGAR_TEXT_UTF8_H_
#define APGAR_TEXT_UTF8_H_

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace apgar::text {

[[nodiscard]] inline bool IsValidUtf8(std::string_view value) noexcept {
  const auto byte_at = [&value](std::size_t index) {
    return static_cast<std::uint8_t>(value[index]);
  };
  const auto is_continuation = [](std::uint8_t byte) { return byte >= 0x80U && byte <= 0xbfU; };

  std::size_t index = 0;
  while (index < value.size()) {
    const std::uint8_t first = byte_at(index);
    if (first <= 0x7fU) {
      ++index;
      continue;
    }
    if (first >= 0xc2U && first <= 0xdfU) {
      if (index + 1 >= value.size() || !is_continuation(byte_at(index + 1))) {
        return false;
      }
      index += 2;
      continue;
    }
    if (first >= 0xe0U && first <= 0xefU) {
      if (index + 2 >= value.size()) {
        return false;
      }
      const std::uint8_t second = byte_at(index + 1);
      const std::uint8_t third = byte_at(index + 2);
      const bool second_is_valid = first == 0xe0U   ? second >= 0xa0U && second <= 0xbfU
                                   : first == 0xedU ? second >= 0x80U && second <= 0x9fU
                                                    : is_continuation(second);
      if (!second_is_valid || !is_continuation(third)) {
        return false;
      }
      index += 3;
      continue;
    }
    if (first >= 0xf0U && first <= 0xf4U) {
      if (index + 3 >= value.size()) {
        return false;
      }
      const std::uint8_t second = byte_at(index + 1);
      const bool second_is_valid = first == 0xf0U   ? second >= 0x90U && second <= 0xbfU
                                   : first == 0xf4U ? second >= 0x80U && second <= 0x8fU
                                                    : is_continuation(second);
      if (!second_is_valid || !is_continuation(byte_at(index + 2)) ||
          !is_continuation(byte_at(index + 3))) {
        return false;
      }
      index += 4;
      continue;
    }
    return false;
  }
  return true;
}

// Returns the largest prefix no longer than max_bytes that ends on a UTF-8
// code-point boundary. Callers validate the complete input first.
[[nodiscard]] inline std::size_t Utf8PrefixBytes(std::string_view value,
                                                 std::size_t max_bytes) noexcept {
  if (value.size() <= max_bytes) {
    return value.size();
  }
  std::size_t prefix = max_bytes;
  while (prefix > 0) {
    const std::uint8_t next = static_cast<std::uint8_t>(value[prefix]);
    if (next < 0x80U || next > 0xbfU) {
      break;
    }
    --prefix;
  }
  return prefix;
}

}  // namespace apgar::text

#endif  // APGAR_TEXT_UTF8_H_
