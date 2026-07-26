#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#if defined(APGAR_PHASE4_EXACT_SMALL_ORACLE_SOURCE_BOUND) && \
    !defined(APGAR_PHASE4_EXACT_SMALL_ORACLE_TEST_SOURCE_COMMIT)
#include "apgar/benchmark/phase3_source_stamp.h"
#endif

namespace {

#if defined(APGAR_PHASE4_CONFIRMATORY_OPERATIONAL_INNER) && \
    defined(APGAR_PHASE4_EXACT_SMALL_ORACLE_INNER)
#error "Python authority launcher requires exactly one fixed inner target"
#elif defined(APGAR_PHASE4_CONFIRMATORY_OPERATIONAL_INNER)
constexpr char kInnerTarget[] = APGAR_PHASE4_CONFIRMATORY_OPERATIONAL_INNER;
constexpr char kLauncherName[] = "confirmatory operational";
constexpr char kHandshakeEnvironment[] = "APGAR_PHASE4_CONFIRMATORY_OPERATIONAL_LAUNCH_FD";
constexpr char kHandshakePrefix[] = "APGAR-PHASE4-CONFIRMATORY-OPERATIONAL-LAUNCH-V1\n";
#elif defined(APGAR_PHASE4_EXACT_SMALL_ORACLE_INNER)
constexpr char kInnerTarget[] = APGAR_PHASE4_EXACT_SMALL_ORACLE_INNER;
constexpr char kLauncherName[] = "exact-small oracle";
constexpr char kHandshakeEnvironment[] = "APGAR_PHASE4_EXACT_SMALL_ORACLE_LAUNCH_FD";
constexpr char kHandshakePrefix[] = "APGAR-PHASE4-EXACT-SMALL-ORACLE-LAUNCH-V1\n";
#else
#error "Python authority launcher requires one fixed inner target"
#endif

#if defined(APGAR_PHASE4_EXACT_SMALL_ORACLE_SOURCE_BOUND)
#if !defined(APGAR_PHASE4_EXACT_SMALL_ORACLE_INNER)
#error "Source binding is restricted to an exact-small Oracle launcher"
#elif defined(APGAR_PHASE4_EXACT_SMALL_ORACLE_TEST_SOURCE_COMMIT)
#if !defined(APGAR_PHASE4_EXACT_SMALL_ORACLE_TEST_SOURCE_STAMPED) || \
    !defined(APGAR_PHASE4_EXACT_SMALL_ORACLE_TEST_SOURCE_DIRTY)
#error "Test source binding requires complete fixed source state"
#endif
constexpr std::string_view kBuiltCommit = APGAR_PHASE4_EXACT_SMALL_ORACLE_TEST_SOURCE_COMMIT;
constexpr bool kSourceStamped = APGAR_PHASE4_EXACT_SMALL_ORACLE_TEST_SOURCE_STAMPED != 0;
constexpr bool kSourceTreeDirty = APGAR_PHASE4_EXACT_SMALL_ORACLE_TEST_SOURCE_DIRTY != 0;
#else
constexpr std::string_view kBuiltCommit = apgar::benchmark::kPhase3BuiltCommit;
constexpr bool kSourceStamped = apgar::benchmark::kPhase3SourceStamped;
constexpr bool kSourceTreeDirty = apgar::benchmark::kPhase3BuiltFromDirtyTree;
#endif
#endif

constexpr char kHermeticPythonRepository[] =
    "rules_python++python+python_3_13_x86_64-unknown-linux-gnu";
constexpr std::size_t kMaximumRunfilesEntries = 16'384;
constexpr std::uintmax_t kMaximumRunfilesManifestBytes = 8ULL * 1024ULL * 1024ULL;
constexpr std::uintmax_t kMaximumRepositoryMappingBytes = 1024ULL * 1024ULL;

#if defined(APGAR_PHASE4_EXACT_SMALL_ORACLE_SOURCE_BOUND)
[[nodiscard]] bool IsLowerHexCommit(std::string_view value) {
  return value.size() == 40U && std::all_of(value.begin(), value.end(), [](char character) {
           return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f');
         });
}

[[nodiscard]] std::optional<std::string_view> ParseExpectedCommit(int argc, char** argv) {
  std::optional<std::string_view> expected_commit;
  constexpr std::string_view kOption = "--expected-commit";
  constexpr std::string_view kEqualsPrefix = "--expected-commit=";
  for (int index = 1; index < argc; ++index) {
    if (argv[index] == nullptr) {
      return std::nullopt;
    }
    const std::string_view argument(argv[index]);
    const std::size_t equals = argument.find('=');
    const std::string_view option = argument.substr(0, equals);
    // argparse accepts unambiguous long-option prefixes by default. Reject
    // every possible abbreviation here as well as disabling that behavior in
    // the inner parser, so another spelling cannot override the checked
    // commit after delegation.
    if (option.size() > 2U && option != kOption && kOption.starts_with(option)) {
      return std::nullopt;
    }
    std::optional<std::string_view> value;
    if (argument == kOption) {
      if (++index >= argc || argv[index] == nullptr) {
        return std::nullopt;
      }
      value = std::string_view(argv[index]);
    } else if (argument.starts_with(kEqualsPrefix)) {
      value = argument.substr(kEqualsPrefix.size());
    }
    if (value.has_value()) {
      if (expected_commit.has_value() || !IsLowerHexCommit(*value)) {
        return std::nullopt;
      }
      expected_commit = *value;
    }
  }
  return expected_commit;
}

[[nodiscard]] bool ValidateSourceBinding(int argc, char** argv) {
  if (!kSourceStamped) {
    std::cerr << kLauncherName << " launcher rejects unstamped source\n";
    return false;
  }
  if (kSourceTreeDirty) {
    std::cerr << kLauncherName << " launcher rejects dirty source\n";
    return false;
  }
  const std::optional<std::string_view> expected_commit = ParseExpectedCommit(argc, argv);
  if (!expected_commit.has_value()) {
    std::cerr << kLauncherName << " launcher requires exactly one valid --expected-commit\n";
    return false;
  }
  if (!IsLowerHexCommit(kBuiltCommit) || *expected_commit != kBuiltCommit) {
    std::cerr << kLauncherName << " launcher rejects expected commit mismatch\n";
    return false;
  }
  return true;
}
#endif

[[nodiscard]] bool ResolvesTo(const std::filesystem::path& path,
                              const std::filesystem::path& expected) {
  char resolved_path[PATH_MAX];
  char resolved_expected[PATH_MAX];
  return realpath(path.c_str(), resolved_path) != nullptr &&
         realpath(expected.c_str(), resolved_expected) != nullptr &&
         std::strcmp(resolved_path, resolved_expected) == 0;
}

[[nodiscard]] std::optional<std::filesystem::path> ExecrootFor(const std::filesystem::path& self) {
  for (std::filesystem::path current = self.parent_path(); !current.empty();
       current = current.parent_path()) {
    if (current.filename() == "bazel-out") {
      return current.parent_path();
    }
    if (current == current.root_path()) {
      break;
    }
  }
  return std::nullopt;
}

[[nodiscard]] bool IsIgnoredBytecode(const std::filesystem::path& relative) {
  if (relative.empty() || *relative.begin() != "_main") {
    return false;
  }
  bool in_cache = false;
  for (const auto& component : relative) {
    if (component == "__pycache__") {
      in_cache = true;
    }
  }
  return in_cache && relative.extension() == ".pyc";
}

[[nodiscard]] bool IsIgnoredBytecodeDirectory(const std::filesystem::path& relative) {
  return !relative.empty() && *relative.begin() == "_main" && relative.filename() == "__pycache__";
}

struct RunfilesManifest {
  std::map<std::string, std::string> entries;
  std::set<std::string> directories;
};

[[nodiscard]] std::optional<std::filesystem::path> SelectedManifestFor(
    const std::filesystem::path& root, const std::filesystem::path& self) {
  const std::filesystem::path manifest = root / "MANIFEST";
  std::error_code error;
  const std::filesystem::file_status manifest_status =
      std::filesystem::symlink_status(manifest, error);
  const std::filesystem::path expected =
      self.parent_path() / (self.filename().string() + ".runfiles_manifest");
  if (error || !std::filesystem::is_symlink(manifest_status)) {
    return std::nullopt;
  }
  const std::filesystem::path target = std::filesystem::read_symlink(manifest, error);
  if (error || target != expected || !std::filesystem::is_regular_file(expected, error) || error ||
      !ResolvesTo(manifest, expected)) {
    return std::nullopt;
  }
  return expected;
}

[[nodiscard]] std::optional<RunfilesManifest> LoadRunfilesManifest(
    const std::filesystem::path& manifest) {
  std::error_code error;
  const std::uintmax_t size = std::filesystem::file_size(manifest, error);
  if (error || size == 0 || size > kMaximumRunfilesManifestBytes) {
    return std::nullopt;
  }
  std::ifstream input(manifest);
  if (!input) {
    return std::nullopt;
  }
  RunfilesManifest result;
  std::string line;
  while (std::getline(input, line)) {
    if (result.entries.size() >= kMaximumRunfilesEntries || line.empty() || line.front() == ' ' ||
        line.find('\0') != std::string::npos || (!line.empty() && line.back() == '\r')) {
      return std::nullopt;
    }
    const std::size_t delimiter = line.find(' ');
    if (delimiter == std::string::npos || delimiter == 0 || delimiter + 1 >= line.size()) {
      return std::nullopt;
    }
    const std::string logical = line.substr(0, delimiter);
    const std::string target = line.substr(delimiter + 1);
    const std::filesystem::path relative(logical);
    if (logical == "MANIFEST" || relative.is_absolute() ||
        relative.lexically_normal().generic_string() != logical ||
        !result.entries.emplace(logical, target).second) {
      return std::nullopt;
    }
    for (std::filesystem::path parent = relative.parent_path(); !parent.empty();
         parent = parent.parent_path()) {
      result.directories.insert(parent.generic_string());
    }
  }
  if (input.bad() || result.entries.empty() || !result.entries.contains("_repo_mapping")) {
    return std::nullopt;
  }
  return result;
}

[[nodiscard]] bool FilesEqualBounded(const std::filesystem::path& left,
                                     const std::filesystem::path& right,
                                     std::uintmax_t maximum_bytes) {
  std::error_code error;
  const std::uintmax_t left_size = std::filesystem::file_size(left, error);
  if (error || left_size == 0 || left_size > maximum_bytes) {
    return false;
  }
  const std::uintmax_t right_size = std::filesystem::file_size(right, error);
  if (error || left_size != right_size) {
    return false;
  }
  std::ifstream left_input(left, std::ios::binary);
  std::ifstream right_input(right, std::ios::binary);
  if (!left_input || !right_input) {
    return false;
  }
  constexpr std::size_t kBlockBytes = 4096;
  std::vector<char> left_block(kBlockBytes);
  std::vector<char> right_block(kBlockBytes);
  while (left_input && right_input) {
    left_input.read(left_block.data(), static_cast<std::streamsize>(left_block.size()));
    right_input.read(right_block.data(), static_cast<std::streamsize>(right_block.size()));
    const std::streamsize left_count = left_input.gcount();
    const std::streamsize right_count = right_input.gcount();
    if (left_count != right_count ||
        !std::equal(left_block.begin(), left_block.begin() + left_count, right_block.begin())) {
      return false;
    }
  }
  return left_input.eof() && right_input.eof();
}

[[nodiscard]] bool MatchesManifestTarget(const std::filesystem::path& entry,
                                         std::string_view declared_target) {
  std::error_code error;
  const std::filesystem::path actual_target = std::filesystem::read_symlink(entry, error);
  const std::filesystem::path declared_path(declared_target);
  if (error ||
      (actual_target.generic_string() != declared_target &&
       (!declared_path.is_absolute() || !ResolvesTo(entry, declared_path))) ||
      !std::filesystem::is_regular_file(entry, error) || error) {
    return false;
  }
  char resolved[PATH_MAX];
  return realpath(entry.c_str(), resolved) != nullptr;
}

[[nodiscard]] bool IsAuthorityTree(const std::filesystem::path& root,
                                   const std::filesystem::path& self,
                                   const std::string& target) try {
  const std::optional<std::filesystem::path> execroot = ExecrootFor(self);
  const std::optional<std::filesystem::path> selected_manifest_path =
      SelectedManifestFor(root, self);
  const std::filesystem::path public_manifest_path =
      self.parent_path() / (self.filename().string() + ".runfiles_manifest");
  const std::optional<RunfilesManifest> selected_manifest =
      selected_manifest_path.has_value() ? LoadRunfilesManifest(*selected_manifest_path)
                                         : std::nullopt;
  if (!execroot.has_value() || !selected_manifest_path.has_value() ||
      *selected_manifest_path != public_manifest_path || !selected_manifest.has_value()) {
    return false;
  }

  const std::filesystem::path public_repository_mapping =
      self.parent_path() / (self.filename().string() + ".repo_mapping");
  const auto selected_mapping_entry = selected_manifest->entries.find("_repo_mapping");
  if (selected_mapping_entry == selected_manifest->entries.end() ||
      selected_mapping_entry->second != public_repository_mapping.string() ||
      !std::filesystem::path(selected_mapping_entry->second).is_absolute() ||
      !FilesEqualBounded(public_repository_mapping, root / "_repo_mapping",
                         kMaximumRepositoryMappingBytes)) {
    return false;
  }

  const std::filesystem::path main = root / "_main";
  const std::filesystem::path interpreter = main / ("_" + target + ".venv/bin/python3");
  const std::filesystem::path expected_interpreter = execroot->parent_path().parent_path() /
                                                     "external" / kHermeticPythonRepository /
                                                     "bin/python3";
  const std::filesystem::path expected_interpreter_link =
      std::filesystem::path("../../../") / kHermeticPythonRepository / "bin/python3";
  if (access((main / target).c_str(), R_OK) != 0 || access(interpreter.c_str(), X_OK) != 0 ||
      !ResolvesTo(main / target, self.parent_path() / target) ||
      !ResolvesTo(main / self.filename(), self) ||
      std::filesystem::read_symlink(interpreter) != expected_interpreter_link ||
      !ResolvesTo(interpreter, expected_interpreter)) {
    return false;
  }

  std::map<std::string, std::string> remaining = selected_manifest->entries;
  std::error_code error;
  std::filesystem::recursive_directory_iterator iterator(
      root, std::filesystem::directory_options::none, error);
  const std::filesystem::recursive_directory_iterator end;
  std::size_t entries = 0;
  while (!error && iterator != end) {
    if (++entries > kMaximumRunfilesEntries) {
      return false;
    }
    const std::filesystem::directory_entry& entry = *iterator;
    const std::filesystem::path relative = entry.path().lexically_relative(root);
    const std::string logical = relative.generic_string();
    const std::filesystem::file_status status = entry.symlink_status(error);
    if (error) {
      return false;
    }
    if (std::filesystem::is_directory(status)) {
      if (!selected_manifest->directories.contains(logical) &&
          !IsIgnoredBytecodeDirectory(relative)) {
        return false;
      }
      iterator.increment(error);
      continue;
    }
    // Both bootstrap stages use -I -B and a nonexistent redirected
    // pycache_prefix, so stale conventional caches cannot affect imports.
    if (IsIgnoredBytecode(relative) && std::filesystem::is_regular_file(status)) {
      iterator.increment(error);
      continue;
    }
    if (!std::filesystem::is_symlink(status)) {
      return false;
    }
    if (logical == "MANIFEST") {
      if (!selected_manifest_path.has_value() ||
          std::filesystem::read_symlink(entry.path(), error) != *selected_manifest_path || error) {
        return false;
      }
      iterator.increment(error);
      continue;
    }
    const auto declared = remaining.find(logical);
    if (declared == remaining.end() || !MatchesManifestTarget(entry.path(), declared->second)) {
      return false;
    }
    remaining.erase(declared);
    iterator.increment(error);
  }
  return !error && remaining.empty();
} catch (const std::exception&) {
  return false;
}

[[nodiscard]] std::optional<std::filesystem::path> ResolveRunfiles(
    const std::filesystem::path& self, const char* invoked_as, const std::string& target) {
  std::error_code error;
  std::filesystem::path invoked(invoked_as);
  if (invoked.is_relative()) {
    invoked = std::filesystem::current_path(error) / invoked;
    if (error) {
      return std::nullopt;
    }
  }
  invoked = invoked.lexically_normal();
  if (!ResolvesTo(invoked, self)) {
    return std::nullopt;
  }
  // The adjacent target-specific tree is the sole Python/data authority.
  // argv[0] may be a Bazel test-tree symlink to this launcher, but that
  // enclosing tree is neither inspected nor selected for delegated imports.
  const std::filesystem::path standalone(self.string() + ".runfiles");
  if (IsAuthorityTree(standalone, self, target)) {
    return standalone;
  }
  return std::nullopt;
}

[[nodiscard]] bool WriteAll(int descriptor, std::string_view value) {
  while (!value.empty()) {
    const ssize_t written = write(descriptor, value.data(), value.size());
    if (written < 0) {
      if (errno == EINTR) {
        continue;
      }
      return false;
    }
    value.remove_prefix(static_cast<std::size_t>(written));
  }
  return true;
}

[[nodiscard]] bool NormalizeChildReaping() {
  struct sigaction action{};
  action.sa_handler = SIG_DFL;
  return sigemptyset(&action.sa_mask) == 0 && sigaction(SIGCHLD, &action, nullptr) == 0;
}

[[nodiscard]] bool InstallHandshake(const std::string& target, int* inherited_descriptor) {
  int descriptors[2];
  if (pipe(descriptors) != 0) {
    return false;
  }
  if (descriptors[0] <= STDERR_FILENO) {
    int promoted_descriptor;
    do {
      promoted_descriptor = fcntl(descriptors[0], F_DUPFD_CLOEXEC, STDERR_FILENO + 1);
    } while (promoted_descriptor < 0 && errno == EINTR);
    if (promoted_descriptor < 0) {
      close(descriptors[0]);
      close(descriptors[1]);
      return false;
    }
    const int reserved_descriptor = descriptors[0];
    descriptors[0] = promoted_descriptor;
    if (close(reserved_descriptor) != 0) {
      close(descriptors[0]);
      close(descriptors[1]);
      return false;
    }
  }
  const int descriptor_flags = fcntl(descriptors[0], F_GETFD);
  const int status_flags = fcntl(descriptors[0], F_GETFL);
  if (descriptor_flags < 0 || status_flags < 0 ||
      fcntl(descriptors[0], F_SETFD, descriptor_flags & ~FD_CLOEXEC) != 0 ||
      fcntl(descriptors[0], F_SETFL, status_flags | O_NONBLOCK) != 0) {
    close(descriptors[0]);
    close(descriptors[1]);
    return false;
  }
  const std::string token = std::string(kHandshakePrefix) + target + "\n";
  const bool written = WriteAll(descriptors[1], token);
  const bool writer_closed = close(descriptors[1]) == 0;
  const std::string descriptor = std::to_string(descriptors[0]);
  if (!written || !writer_closed || setenv(kHandshakeEnvironment, descriptor.c_str(), 1) != 0) {
    close(descriptors[0]);
    return false;
  }
  *inherited_descriptor = descriptors[0];
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc <= 0 || argv == nullptr || argv[0] == nullptr) {
    return 2;
  }
#if defined(APGAR_PHASE4_EXACT_SMALL_ORACLE_SOURCE_BOUND)
  // This preflight is deliberately argv-only. It precedes runfiles discovery,
  // delegation, and every publisher input open.
  if (!ValidateSourceBinding(argc, argv)) {
    return 2;
  }
#endif
  for (const char* name : {"LD_PRELOAD", "LD_AUDIT"}) {
    const char* value = std::getenv(name);
    if (value != nullptr && value[0] != '\0') {
      std::cerr << kLauncherName << " launcher rejects ambient " << name << '\n';
      return 2;
    }
  }
  char executable[PATH_MAX];
  const ssize_t executable_size = readlink("/proc/self/exe", executable, sizeof(executable));
  if (executable_size <= 0 || static_cast<std::size_t>(executable_size) >= sizeof(executable)) {
    std::cerr << kLauncherName
              << " launcher cannot identify its executable: " << std::strerror(errno) << '\n';
    return 2;
  }
  executable[executable_size] = '\0';
  char resolved[PATH_MAX];
  if (realpath(executable, resolved) == nullptr) {
    std::cerr << kLauncherName
              << " launcher cannot resolve its executable: " << std::strerror(errno) << '\n';
    return 2;
  }
  const std::string target = kInnerTarget;
  const std::optional<std::filesystem::path> runfiles = ResolveRunfiles(resolved, argv[0], target);
  if (!runfiles.has_value()) {
    std::cerr << kLauncherName << " launcher cannot authenticate its bundled runfiles\n";
    return 2;
  }
  const std::string inner = (*runfiles / "_main" / target).string();
  const std::string interpreter =
      (*runfiles / "_main" / ("_" + target + ".venv/bin/python3")).string();
  for (const char* name : {"RUNFILES_DIR", "RUNFILES_MANIFEST_FILE", "JAVA_RUNFILES"}) {
    if (unsetenv(name) != 0) {
      std::cerr << kLauncherName << " launcher cannot clear " << name << '\n';
      return 2;
    }
  }
  if (!NormalizeChildReaping()) {
    std::cerr << kLauncherName
              << " launcher cannot establish exact child reaping: " << std::strerror(errno) << '\n';
    return 2;
  }
  char cache_template[] = "/tmp/apgar-phase4-python-cache-XXXXXX";
  const char* const cache_directory = mkdtemp(cache_template);
  if (cache_directory == nullptr) {
    std::cerr << kLauncherName
              << " launcher cannot create an isolated bytecode cache: " << std::strerror(errno)
              << '\n';
    return 2;
  }
  const std::string pycache_option = std::string("pycache_prefix=") + cache_directory;
  if (rmdir(cache_directory) != 0) {
    std::cerr << kLauncherName << " launcher cannot reserve an isolated bytecode cache path: "
              << std::strerror(errno) << '\n';
    return 2;
  }
  const std::string stage_two_arguments = "-I -B -X " + pycache_option;
  // The rules_python stage-one bootstrap re-executes its stage-two bootstrap.
  // Stage one must not import site before that bootstrap establishes the
  // authenticated standalone runfiles root. Stage two retains isolated site
  // initialization after the bootstrap selects that root.
  if (setenv("RULES_PYTHON_ADDITIONAL_INTERPRETER_ARGS", stage_two_arguments.c_str(), 1) != 0) {
    std::cerr << kLauncherName << " launcher cannot isolate its Python authority\n";
    return 2;
  }
  int handshake_descriptor = -1;
  if (!InstallHandshake(target, &handshake_descriptor)) {
    std::cerr << kLauncherName << " launcher cannot establish its authority handshake\n";
    return 2;
  }
  std::vector<char*> delegated;
  delegated.reserve(static_cast<std::size_t>(argc) + 7U);
  delegated.push_back(const_cast<char*>(interpreter.c_str()));
  delegated.push_back(const_cast<char*>("-I"));
  delegated.push_back(const_cast<char*>("-B"));
  delegated.push_back(const_cast<char*>("-S"));
  delegated.push_back(const_cast<char*>("-X"));
  delegated.push_back(const_cast<char*>(pycache_option.c_str()));
  delegated.push_back(const_cast<char*>(inner.c_str()));
  for (int index = 1; index < argc; ++index) {
    delegated.push_back(argv[index]);
  }
  delegated.push_back(nullptr);
  const pid_t launcher_pid = getpid();
  const pid_t child = fork();
  if (child < 0) {
    close(handshake_descriptor);
    std::cerr << kLauncherName
              << " launcher cannot fork its bundled authority: " << std::strerror(errno) << '\n';
    return 2;
  }
  if (child == 0) {
    if (prctl(PR_SET_PDEATHSIG, SIGKILL) != 0) {
      std::cerr << kLauncherName
                << " launcher cannot bind its authority lifetime: " << std::strerror(errno) << '\n';
      _exit(2);
    }
    if (getppid() != launcher_pid) {
      std::cerr << kLauncherName << " launcher lost its authority parent\n";
      _exit(2);
    }
    execv(interpreter.c_str(), delegated.data());
    std::cerr << kLauncherName
              << " launcher cannot execute its bundled authority: " << std::strerror(errno) << '\n';
    _exit(2);
  }
  close(handshake_descriptor);
  int status = 0;
  while (waitpid(child, &status, 0) < 0) {
    if (errno == EINTR) {
      continue;
    }
    std::cerr << kLauncherName
              << " launcher cannot reap its bundled authority: " << std::strerror(errno) << '\n';
    return 2;
  }
  if (WIFEXITED(status)) {
    return WEXITSTATUS(status);
  }
  if (WIFSIGNALED(status)) {
    const int signal = WTERMSIG(status);
    struct sigaction action{};
    action.sa_handler = SIG_DFL;
    sigemptyset(&action.sa_mask);
    sigaction(signal, &action, nullptr);
    sigset_t unblocked;
    sigemptyset(&unblocked);
    sigaddset(&unblocked, signal);
    sigprocmask(SIG_UNBLOCK, &unblocked, nullptr);
    kill(getpid(), signal);
    return 128 + signal;
  }
  return 2;
}
