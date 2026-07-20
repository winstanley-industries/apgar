#include "apgar/tooling/replay.h"

#include <cstdint>
#include <optional>

#include "apgar/board_ir/stable_hash.h"
#include "apgar/text/utf8.h"

namespace apgar::tooling {

std::optional<CanonicalReplayEnvelope> ParseCanonicalReplayEnvelope(
    std::string_view contents, std::span<const std::string_view> ordered_keys, std::string* error) {
  if (error == nullptr) {
    return std::nullopt;
  }
  error->clear();
  if (!text::IsValidUtf8(contents)) {
    *error = "canonical replay must be valid UTF-8";
    return std::nullopt;
  }
  if (contents.find('\0') != std::string_view::npos ||
      contents.find('\r') != std::string_view::npos) {
    *error = "canonical replay must not contain NUL or CR bytes";
    return std::nullopt;
  }
  if (contents.empty() || contents.back() != '\n') {
    *error = "canonical replay must end every line with LF";
    return std::nullopt;
  }

  constexpr std::string_view kChecksumKey = "checksum_fnv1a64=";
  const std::string_view without_final_lf = contents.substr(0, contents.size() - 1);
  const std::size_t final_line_separator = without_final_lf.rfind('\n');
  if (final_line_separator == std::string_view::npos) {
    *error = "missing final replay checksum";
    return std::nullopt;
  }
  const std::size_t checksum_start = final_line_separator + 1;
  const std::string_view checksum_line = without_final_lf.substr(checksum_start);
  if (!checksum_line.starts_with(kChecksumKey)) {
    *error = "missing final replay checksum";
    return std::nullopt;
  }
  const std::string_view checksum_text = checksum_line.substr(kChecksumKey.size());
  std::uint64_t recorded_checksum = 0;
  const std::string_view payload = contents.substr(0, checksum_start);
  if (!ParseCanonicalUnsignedDecimal(checksum_text, &recorded_checksum) ||
      board_ir::StableHashString(payload) != recorded_checksum) {
    *error = "replay payload checksum mismatch";
    return std::nullopt;
  }

  CanonicalReplayEnvelope envelope;
  envelope.values.reserve(ordered_keys.size());
  std::size_t offset = 0;
  for (const std::string_view key : ordered_keys) {
    const std::size_t end = payload.find('\n', offset);
    if (end == std::string_view::npos) {
      *error = "replay payload is truncated";
      return std::nullopt;
    }
    const std::string_view line = payload.substr(offset, end - offset);
    if (!line.starts_with(key)) {
      *error = "replay fields are missing or out of canonical order";
      return std::nullopt;
    }
    envelope.values.emplace_back(line.substr(key.size()));
    offset = end + 1;
  }
  if (offset != payload.size()) {
    *error = "replay payload has unknown fields";
    return std::nullopt;
  }
  return envelope;
}

}  // namespace apgar::tooling
