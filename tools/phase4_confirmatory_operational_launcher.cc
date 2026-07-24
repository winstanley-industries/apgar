#include <limits.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#ifndef APGAR_PHASE4_CONFIRMATORY_OPERATIONAL_INNER
#error "confirmatory operational launcher requires one fixed inner target"
#endif

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
  const std::string runfiles = std::string(resolved) + ".runfiles/";
  const std::string target = APGAR_PHASE4_CONFIRMATORY_OPERATIONAL_INNER;
  const std::string inner = runfiles + "_main/" + target;
  const std::string interpreter = runfiles + "_main/_" + target + ".venv/bin/python3";
  for (const char* name : {"RUNFILES_DIR", "RUNFILES_MANIFEST_FILE", "JAVA_RUNFILES"}) {
    if (unsetenv(name) != 0) {
      std::cerr << "confirmatory operational launcher cannot clear " << name << '\n';
      return 2;
    }
  }
  // The rules_python stage-one bootstrap re-executes its stage-two bootstrap.
  // Isolate both interpreters: do not let PATH choose stage one, and force
  // stage two to ignore ambient Python import/configuration variables too.
  if (setenv("RULES_PYTHON_ADDITIONAL_INTERPRETER_ARGS", "-I", 1) != 0) {
    std::cerr << "confirmatory operational launcher cannot isolate its Python authority\n";
    return 2;
  }
  std::vector<char*> delegated;
  delegated.reserve(static_cast<std::size_t>(argc) + 3U);
  delegated.push_back(const_cast<char*>(interpreter.c_str()));
  delegated.push_back(const_cast<char*>("-I"));
  delegated.push_back(const_cast<char*>(inner.c_str()));
  for (int index = 1; index < argc; ++index) {
    delegated.push_back(argv[index]);
  }
  delegated.push_back(nullptr);
  execv(interpreter.c_str(), delegated.data());
  std::cerr << "confirmatory operational launcher cannot execute its bundled authority: "
            << std::strerror(errno) << '\n';
  return 2;
}
