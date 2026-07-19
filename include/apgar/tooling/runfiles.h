#ifndef APGAR_TOOLING_RUNFILES_H_
#define APGAR_TOOLING_RUNFILES_H_

#include <optional>
#include <string>
#include <string_view>

namespace apgar::tooling {

// Resolves a workspace-relative file through Bazel's test/binary runfiles or,
// when invoked via `bazel run`, the source workspace.
[[nodiscard]] std::string ResolveRunfile(std::string_view relative);

[[nodiscard]] std::optional<std::string> ReadFile(std::string_view path);
[[nodiscard]] std::optional<std::string> ReadRunfile(std::string_view relative);

}  // namespace apgar::tooling

#endif  // APGAR_TOOLING_RUNFILES_H_
