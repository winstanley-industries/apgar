#ifndef APGAR_TOOLING_REPLAY_H_
#define APGAR_TOOLING_REPLAY_H_

#include <charconv>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <vector>

namespace apgar::tooling {

struct CanonicalReplayEnvelope {
  std::vector<std::string> values;
};

// Parses the shared replay envelope: exact ordered keys, LF line endings, no
// trailing or unknown fields, and a final canonical FNV-1a checksum line.
[[nodiscard]] std::optional<CanonicalReplayEnvelope> ParseCanonicalReplayEnvelope(
    std::string_view contents, std::span<const std::string_view> ordered_keys, std::string* error);

// Canonical replay integers use the grammar 0|[1-9][0-9]* and must fit the
// destination unsigned integer type. The destination is unchanged on failure.
template <typename Integer>
[[nodiscard]] bool ParseCanonicalUnsignedDecimal(std::string_view text, Integer* value) {
  static_assert(std::is_integral_v<Integer> && std::is_unsigned_v<Integer>);
  if (value == nullptr || text.empty() || (text.size() > 1 && text.front() == '0')) {
    return false;
  }
  for (const char character : text) {
    if (character < '0' || character > '9') {
      return false;
    }
  }
  Integer parsed = 0;
  const char* const begin = text.data();
  const char* const end = text.data() + text.size();
  const auto [next, parse_error] = std::from_chars(begin, end, parsed);
  if (parse_error != std::errc{} || next != end) {
    return false;
  }
  *value = parsed;
  return true;
}

}  // namespace apgar::tooling

#endif  // APGAR_TOOLING_REPLAY_H_
