#include "tools/benchmark_support.h"

#include <benchmark/benchmark.h>
#include <sys/utsname.h>

#include <optional>
#include <string>
#include <string_view>

#include "apgar/board_ir/stable_hash.h"
#include "apgar/tooling/runfiles.h"

namespace apgar::benchmark::tool_support {
namespace {

void AddPoint(board_ir::StableHashBuilder* hash, board_ir::Point64 point) {
  hash->AddI64(point.x);
  hash->AddI64(point.y);
}

[[nodiscard]] std::optional<std::string> LineValue(std::string_view contents,
                                                   std::string_view key) {
  const std::size_t key_offset = contents.find(key);
  if (key_offset == std::string_view::npos) {
    return std::nullopt;
  }
  std::string_view value = contents.substr(key_offset + key.size());
  value = value.substr(0, value.find('\n'));
  while (!value.empty() && (value.front() == ' ' || value.front() == '\t' || value.front() == ':' ||
                            value.front() == '"')) {
    value.remove_prefix(1);
  }
  while (!value.empty() && (value.back() == ' ' || value.back() == '\t' || value.back() == '"')) {
    value.remove_suffix(1);
  }
  return std::string(value);
}

}  // namespace

ExtractedArgument ExtractSingleArgument(int* argc, char** argv, std::string_view prefix) {
  ExtractedArgument result;
  int destination = 1;
  for (int source = 1; source < *argc; ++source) {
    const std::string_view argument(argv[source]);
    if (argument.starts_with(prefix)) {
      if (result.value.has_value()) {
        result.duplicate = true;
      }
      result.value = std::string(argument.substr(prefix.size()));
    } else {
      argv[destination++] = argv[source];
    }
  }
  *argc = destination;
  return result;
}

std::uint64_t PlanarGeometryFingerprint(std::string_view domain,
                                        std::span<const board_ir::Point64> path,
                                        std::span<const routing::LayerSegment> segments) {
  board_ir::StableHashBuilder hash;
  hash.AddString(domain);
  hash.AddU64(path.size());
  for (const board_ir::Point64 point : path) {
    AddPoint(&hash, point);
  }
  hash.AddU64(segments.size());
  for (const routing::LayerSegment& segment : segments) {
    hash.AddI32(segment.layer);
    AddPoint(&hash, segment.centerline.start);
    AddPoint(&hash, segment.centerline.end);
  }
  return hash.Finish();
}

std::string JoinUnsignedDecimal(std::span<const std::uint32_t> values) {
  std::string joined;
  for (std::size_t index = 0; index < values.size(); ++index) {
    if (index != 0) {
      joined.push_back(',');
    }
    joined += std::to_string(values[index]);
  }
  return joined;
}

void AddHostContext() {
  struct utsname host{};
  if (uname(&host) == 0) {
    ::benchmark::AddCustomContext("apgar_host_kernel",
                                  std::string(host.sysname) + " " + host.release);
    ::benchmark::AddCustomContext("apgar_host_architecture", host.machine);
  }
  if (const std::optional<std::string> os_release = tooling::ReadFile("/etc/os-release");
      os_release.has_value()) {
    if (const std::optional<std::string> pretty_name = LineValue(*os_release, "PRETTY_NAME=");
        pretty_name.has_value()) {
      ::benchmark::AddCustomContext("apgar_host_os", *pretty_name);
    }
  }
  if (const std::optional<std::string> cpu_info = tooling::ReadFile("/proc/cpuinfo");
      cpu_info.has_value()) {
    if (const std::optional<std::string> model = LineValue(*cpu_info, "model name");
        model.has_value()) {
      ::benchmark::AddCustomContext("apgar_cpu_model", *model);
    }
  }
}

}  // namespace apgar::benchmark::tool_support
