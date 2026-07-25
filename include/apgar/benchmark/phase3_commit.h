#ifndef APGAR_BENCHMARK_PHASE3_COMMIT_H_
#define APGAR_BENCHMARK_PHASE3_COMMIT_H_

#include <cstddef>
#include <string_view>

namespace apgar::benchmark {

inline constexpr std::size_t kFullGitCommitHexCharacters = 40;
inline constexpr std::size_t kMaximumDriverVersionLabelCharacters = 32;

[[nodiscard]] constexpr bool IsFullLowercaseGitCommit(std::string_view commit) noexcept {
  if (commit.size() != kFullGitCommitHexCharacters) {
    return false;
  }
  for (const char character : commit) {
    if (!((character >= '0' && character <= '9') || (character >= 'a' && character <= 'f'))) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] constexpr bool CommitMatchesBuiltSource(std::string_view runtime_commit,
                                                      std::string_view built_commit) noexcept {
  return IsFullLowercaseGitCommit(runtime_commit) && IsFullLowercaseGitCommit(built_commit) &&
         runtime_commit == built_commit;
}

[[nodiscard]] constexpr bool IsPublishableEmbeddedBenchmarkSource(
    std::string_view built_commit, bool source_stamped, bool built_from_dirty_tree) noexcept {
  return source_stamped && !built_from_dirty_tree && IsFullLowercaseGitCommit(built_commit);
}

[[nodiscard]] constexpr bool IsPublishableBenchmarkSource(std::string_view runtime_commit,
                                                          std::string_view built_commit,
                                                          bool source_stamped,
                                                          bool built_from_dirty_tree) noexcept {
  return IsPublishableEmbeddedBenchmarkSource(built_commit, source_stamped,
                                              built_from_dirty_tree) &&
         CommitMatchesBuiltSource(runtime_commit, built_commit);
}

[[nodiscard]] constexpr bool IsDriverVersionLabel(std::string_view version) noexcept {
  if (version.empty() || version.size() > kMaximumDriverVersionLabelCharacters ||
      version.front() == '.' || version.back() == '.') {
    return false;
  }
  bool previous_was_dot = false;
  for (const char character : version) {
    if (character == '.') {
      if (previous_was_dot) {
        return false;
      }
      previous_was_dot = true;
      continue;
    }
    if (character < '0' || character > '9') {
      return false;
    }
    previous_was_dot = false;
  }
  return true;
}

}  // namespace apgar::benchmark

#endif  // APGAR_BENCHMARK_PHASE3_COMMIT_H_
