#include "apgar/tooling/runfiles.h"

#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

namespace apgar::tooling {

std::string ResolveRunfile(std::string_view relative) {
  if (const char* source_directory = std::getenv("TEST_SRCDIR"); source_directory != nullptr) {
    if (const char* workspace = std::getenv("TEST_WORKSPACE"); workspace != nullptr) {
      return std::string(source_directory) + "/" + workspace + "/" + std::string(relative);
    }
  }
  if (const char* runfiles = std::getenv("RUNFILES_DIR"); runfiles != nullptr) {
    return std::string(runfiles) + "/_main/" + std::string(relative);
  }
  if (const char* workspace = std::getenv("BUILD_WORKSPACE_DIRECTORY"); workspace != nullptr) {
    return std::string(workspace) + "/" + std::string(relative);
  }
  return std::string(relative);
}

std::optional<std::string> ReadFile(std::string_view path) {
  std::ifstream input(std::string(path), std::ios::binary);
  if (!input) {
    return std::nullopt;
  }
  return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

std::optional<std::string> ReadRunfile(std::string_view relative) {
  return ReadFile(ResolveRunfile(relative));
}

}  // namespace apgar::tooling
