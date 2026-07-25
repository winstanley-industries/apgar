#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#ifndef APGAR_PHASE4_CONFIRMATORY_OPERATIONAL_INNER
#error "confirmatory operational launcher requires one fixed inner target"
#endif

namespace {

constexpr char kHandshakeEnvironment[] = "APGAR_PHASE4_CONFIRMATORY_OPERATIONAL_LAUNCH_FD";
constexpr char kHandshakePrefix[] = "APGAR-PHASE4-CONFIRMATORY-OPERATIONAL-LAUNCH-V1\n";
constexpr char kHermeticPythonRepository[] =
    "rules_python++python+python_3_13_x86_64-unknown-linux-gnu";
constexpr std::size_t kMaximumRunfilesEntries = 16'384;

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
  bool in_cache = false;
  for (const auto& component : relative) {
    if (component == "__pycache__") {
      in_cache = true;
    }
  }
  return in_cache && relative.extension() == ".pyc";
}

[[nodiscard]] bool IsCompletelyTraversable(const std::filesystem::path& root) {
  std::error_code error;
  std::filesystem::recursive_directory_iterator iterator(
      root, std::filesystem::directory_options::none, error);
  const std::filesystem::recursive_directory_iterator end;
  std::size_t entries = 0;
  while (!error && iterator != end) {
    if (++entries > kMaximumRunfilesEntries) {
      return false;
    }
    (void)iterator->symlink_status(error);
    if (error) {
      return false;
    }
    iterator.increment(error);
  }
  return !error;
}

[[nodiscard]] bool IsAuthorityTree(const std::filesystem::path& root,
                                   const std::filesystem::path& self, const std::string& target) {
  const std::optional<std::filesystem::path> execroot = ExecrootFor(self);
  if (!execroot.has_value() || !IsCompletelyTraversable(root)) {
    return false;
  }
  const std::filesystem::path main = root / "_main";
  const std::filesystem::path interpreter = main / ("_" + target + ".venv/bin/python3");
  const std::filesystem::path expected_interpreter =
      *execroot / "external" / kHermeticPythonRepository / "bin/python3";
  if (access((main / target).c_str(), R_OK) != 0 || access(interpreter.c_str(), X_OK) != 0 ||
      !ResolvesTo(main / target, self.parent_path() / target) ||
      !ResolvesTo(interpreter, expected_interpreter)) {
    return false;
  }
  std::error_code error;
  std::filesystem::recursive_directory_iterator iterator(
      main, std::filesystem::directory_options::none, error);
  const std::filesystem::recursive_directory_iterator end;
  std::size_t entries = 0;
  while (!error && iterator != end) {
    if (++entries > kMaximumRunfilesEntries) {
      return false;
    }
    const std::filesystem::directory_entry& entry = *iterator;
    const std::filesystem::path relative = entry.path().lexically_relative(main);
    const std::filesystem::file_status status = entry.symlink_status(error);
    if (error) {
      return false;
    }
    if (std::filesystem::is_directory(status) || IsIgnoredBytecode(relative)) {
      iterator.increment(error);
      continue;
    }
    if (!std::filesystem::is_symlink(status)) {
      return false;
    }
    if (!ResolvesTo(entry.path(), expected_interpreter) &&
        !ResolvesTo(entry.path(), *execroot / relative) &&
        !ResolvesTo(entry.path(), self.parent_path() / relative)) {
      return false;
    }
    iterator.increment(error);
  }
  return !error;
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
  const std::filesystem::path main = invoked.parent_path();
  const std::filesystem::path enclosing = main.parent_path();
  if (main.filename() == "_main" && enclosing.filename().string().ends_with(".runfiles")) {
    if (IsAuthorityTree(enclosing, self, target)) {
      return enclosing;
    }
    return std::nullopt;
  }
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
  for (const char* name : {"LD_PRELOAD", "LD_AUDIT"}) {
    const char* value = std::getenv(name);
    if (value != nullptr && value[0] != '\0') {
      std::cerr << "confirmatory operational launcher rejects ambient " << name << '\n';
      return 2;
    }
  }
  char executable[PATH_MAX];
  const ssize_t executable_size = readlink("/proc/self/exe", executable, sizeof(executable));
  if (executable_size <= 0 || static_cast<std::size_t>(executable_size) >= sizeof(executable)) {
    std::cerr << "confirmatory operational launcher cannot identify its executable: "
              << std::strerror(errno) << '\n';
    return 2;
  }
  executable[executable_size] = '\0';
  char resolved[PATH_MAX];
  if (realpath(executable, resolved) == nullptr) {
    std::cerr << "confirmatory operational launcher cannot resolve its executable: "
              << std::strerror(errno) << '\n';
    return 2;
  }
  const std::string target = APGAR_PHASE4_CONFIRMATORY_OPERATIONAL_INNER;
  const std::optional<std::filesystem::path> runfiles = ResolveRunfiles(resolved, argv[0], target);
  if (!runfiles.has_value()) {
    std::cerr << "confirmatory operational launcher cannot authenticate its bundled runfiles\n";
    return 2;
  }
  const std::string inner = (*runfiles / "_main" / target).string();
  const std::string interpreter =
      (*runfiles / "_main" / ("_" + target + ".venv/bin/python3")).string();
  for (const char* name : {"RUNFILES_DIR", "RUNFILES_MANIFEST_FILE", "JAVA_RUNFILES"}) {
    if (unsetenv(name) != 0) {
      std::cerr << "confirmatory operational launcher cannot clear " << name << '\n';
      return 2;
    }
  }
  if (!NormalizeChildReaping()) {
    std::cerr << "confirmatory operational launcher cannot establish exact child reaping: "
              << std::strerror(errno) << '\n';
    return 2;
  }
  char cache_template[] = "/tmp/apgar-phase4-python-cache-XXXXXX";
  const char* const cache_directory = mkdtemp(cache_template);
  if (cache_directory == nullptr) {
    std::cerr << "confirmatory operational launcher cannot create an isolated bytecode cache: "
              << std::strerror(errno) << '\n';
    return 2;
  }
  const std::string pycache_option = std::string("pycache_prefix=") + cache_directory;
  if (rmdir(cache_directory) != 0) {
    std::cerr << "confirmatory operational launcher cannot reserve an isolated bytecode cache "
                 "path: "
              << std::strerror(errno) << '\n';
    return 2;
  }
  const std::string stage_two_arguments = "-I -B -X " + pycache_option;
  // The rules_python stage-one bootstrap re-executes its stage-two bootstrap.
  // Isolate both interpreters: do not let PATH choose stage one, and force
  // stage two to ignore ambient Python import/configuration and cached code.
  if (setenv("RULES_PYTHON_ADDITIONAL_INTERPRETER_ARGS", stage_two_arguments.c_str(), 1) != 0) {
    std::cerr << "confirmatory operational launcher cannot isolate its Python authority\n";
    return 2;
  }
  int handshake_descriptor = -1;
  if (!InstallHandshake(target, &handshake_descriptor)) {
    std::cerr << "confirmatory operational launcher cannot establish its authority handshake\n";
    return 2;
  }
  std::vector<char*> delegated;
  delegated.reserve(static_cast<std::size_t>(argc) + 6U);
  delegated.push_back(const_cast<char*>(interpreter.c_str()));
  delegated.push_back(const_cast<char*>("-I"));
  delegated.push_back(const_cast<char*>("-B"));
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
    std::cerr << "confirmatory operational launcher cannot fork its bundled authority: "
              << std::strerror(errno) << '\n';
    return 2;
  }
  if (child == 0) {
    if (prctl(PR_SET_PDEATHSIG, SIGKILL) != 0) {
      std::cerr << "confirmatory operational launcher cannot bind its authority lifetime: "
                << std::strerror(errno) << '\n';
      _exit(2);
    }
    if (getppid() != launcher_pid) {
      std::cerr << "confirmatory operational launcher lost its authority parent\n";
      _exit(2);
    }
    execv(interpreter.c_str(), delegated.data());
    std::cerr << "confirmatory operational launcher cannot execute its bundled authority: "
              << std::strerror(errno) << '\n';
    _exit(2);
  }
  close(handshake_descriptor);
  int status = 0;
  while (waitpid(child, &status, 0) < 0) {
    if (errno == EINTR) {
      continue;
    }
    std::cerr << "confirmatory operational launcher cannot reap its bundled authority: "
              << std::strerror(errno) << '\n';
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
