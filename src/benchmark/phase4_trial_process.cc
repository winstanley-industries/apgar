#include <fcntl.h>
#include <poll.h>
#include <sched.h>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "apgar/benchmark/phase4_trial_harness.h"
#include "apgar/board_ir/stable_hash.h"
#include "src/benchmark/phase4_paired_trial_internal.h"
#include "src/benchmark/phase4_trial_wire_internal.h"

namespace apgar::benchmark {
namespace {

using Clock = std::chrono::steady_clock;
using internal::Phase4TrialWireMessage;

constexpr int kWorkerRequestDescriptor = 3;
constexpr int kWorkerResponseDescriptor = 4;
constexpr int kMinimumPrivateDescriptor = 16;
constexpr std::uint8_t kLaunchGateToken = 0xA4;
constexpr std::size_t kMaximumCapturedStderrBytes = 64U * 1024U;
constexpr std::size_t kMaximumDrainBytesPerPass = 64U * 1024U;
constexpr auto kPostKillReapGrace = std::chrono::seconds(1);

#if defined(__linux__) && defined(__x86_64__) && !defined(__ILP32__)
// The hermetic Linux 4.19 UAPI headers predate pidfds. These syscall numbers
// are permanent members of the x86-64 Linux ABI implemented by newer kernels.
constexpr long kPidfdSendSignalSystemCall = 424;
constexpr long kPidfdOpenSystemCall = 434;
constexpr long kCloseRangeSystemCall = 436;
constexpr idtype_t kPidfdWaitIdType = static_cast<idtype_t>(3);
#else
#error "Phase 4 isolated process authority requires the Linux x86-64 syscall ABI"
#endif

struct WorkerProcess {
  Phase4TrialArm arm = Phase4TrialArm::kSequentialBaseline;
  pid_t pid = -1;
  int pid_descriptor = -1;
  int request_descriptor = -1;
  int response_descriptor = -1;
  int stderr_descriptor = -1;
  int exec_error_descriptor = -1;
  std::uint64_t process_instance_identity = 0;
  std::optional<internal::Phase4TrialWireReady> ready_identity;
  bool exec_confirmed = false;
  bool stop_acknowledged = false;
  bool response_eof_observed = false;
  bool response_trailing_observed = false;
  bool response_read_failed = false;
  bool exit_observed = false;
  int observed_exit_code = 0;
  int observed_exit_status = 0;
  bool reaped = false;
  int wait_error_number = 0;
  int wait_status = 0;
  struct rusage usage{};
  std::string bounded_stderr;
};

enum class AwaitKind : std::uint8_t {
  kMessage,
  kTimeout,
  kProcessExit,
  kProtocol,
  kLaunch,
};

enum class ReapState : std::uint8_t {
  kRunning,
  kExited,
  kReaped,
  kError,
};

struct ReapResult {
  bool teardown_deadline_expired = false;
  bool kill_signal_sent = false;
  bool post_kill_reap_incomplete = false;
};

struct AwaitResult {
  AwaitKind kind = AwaitKind::kProtocol;
  std::optional<Phase4TrialWireMessage> message;
  std::string invariant_id;
  std::string detail;
  std::uint64_t elapsed_nanoseconds = 0;
};

struct LaunchError {
  std::string invariant_id;
  std::string detail;
};

using LaunchResult = std::variant<WorkerProcess, LaunchError>;

[[nodiscard]] std::uint64_t ElapsedNanoseconds(Clock::time_point start,
                                               Clock::time_point finish) noexcept {
  const auto count = std::chrono::duration_cast<std::chrono::nanoseconds>(finish - start).count();
  return count <= 0 ? 0 : static_cast<std::uint64_t>(count);
}

[[nodiscard]] int RemainingMilliseconds(Clock::time_point deadline) noexcept {
  const Clock::time_point now = Clock::now();
  if (now >= deadline) {
    return 0;
  }
  const auto nanoseconds =
      std::chrono::duration_cast<std::chrono::nanoseconds>(deadline - now).count();
  const std::uint64_t rounded =
      (static_cast<std::uint64_t>(nanoseconds) + 999'999ULL) / 1'000'000ULL;
  return rounded > static_cast<std::uint64_t>(std::numeric_limits<int>::max())
             ? std::numeric_limits<int>::max()
             : static_cast<int>(rounded);
}

void CloseDescriptor(int* descriptor) noexcept {
  const int owned_descriptor = std::exchange(*descriptor, -1);
  if (owned_descriptor >= 0) {
    (void)close(owned_descriptor);
  }
}

void CloseWorkerDescriptors(WorkerProcess* worker) noexcept {
  CloseDescriptor(&worker->request_descriptor);
  CloseDescriptor(&worker->response_descriptor);
  CloseDescriptor(&worker->stderr_descriptor);
  CloseDescriptor(&worker->exec_error_descriptor);
  CloseDescriptor(&worker->pid_descriptor);
}

[[nodiscard]] int OpenPidDescriptor(pid_t pid) noexcept {
  long result;
  do {
    result = syscall(kPidfdOpenSystemCall, pid, 0U);
  } while (result < 0 && errno == EINTR);
  return result >= 0 && result <= std::numeric_limits<int>::max() ? static_cast<int>(result) : -1;
}

[[nodiscard]] bool SendPidDescriptorSignal(int pid_descriptor, int signal_number) noexcept {
  long result;
  do {
    result = syscall(kPidfdSendSignalSystemCall, pid_descriptor, signal_number, nullptr, 0U);
  } while (result < 0 && errno == EINTR);
  return result == 0;
}

[[nodiscard]] bool ValidSigchldAuthority(std::string* detail) noexcept {
  struct sigaction action{};
  if (sigaction(SIGCHLD, nullptr, &action) != 0) {
    *detail = "sigaction(SIGCHLD) failed with errno " + std::to_string(errno);
    return false;
  }
  if (action.sa_handler != SIG_DFL || (action.sa_flags & SA_NOCLDWAIT) != 0) {
    *detail = "SIGCHLD must retain its default handler without SA_NOCLDWAIT";
    return false;
  }
  return true;
}

[[nodiscard]] bool CloseDescriptorRange(unsigned int first, unsigned int last,
                                        rlim_t descriptor_limit) noexcept {
  if (first > last) {
    return true;
  }
  long result;
  do {
    result = syscall(kCloseRangeSystemCall, first, last, 0U);
  } while (result < 0 && errno == EINTR);
  if (result == 0) {
    return true;
  }
  if (errno != ENOSYS) {
    return false;
  }
  if (descriptor_limit == RLIM_INFINITY) {
    errno = EOVERFLOW;
    return false;
  }
  const std::uint64_t upper_exclusive =
      std::min<std::uint64_t>(static_cast<std::uint64_t>(descriptor_limit),
                              static_cast<std::uint64_t>(std::numeric_limits<int>::max()) + 1ULL);
  const std::uint64_t bounded_last =
      std::min<std::uint64_t>(last, upper_exclusive == 0 ? 0 : upper_exclusive - 1);
  if (static_cast<std::uint64_t>(first) >= upper_exclusive) {
    return true;
  }
  for (std::uint64_t descriptor = first; descriptor <= bounded_last; ++descriptor) {
    (void)close(static_cast<int>(descriptor));
  }
  return true;
}

[[nodiscard]] bool CloseUnrelatedChildDescriptors(int exec_error_descriptor,
                                                  rlim_t descriptor_limit) noexcept {
  return CloseDescriptorRange(0, 0, descriptor_limit) &&
         CloseDescriptorRange(5, static_cast<unsigned int>(exec_error_descriptor - 1),
                              descriptor_limit) &&
         CloseDescriptorRange(static_cast<unsigned int>(exec_error_descriptor) + 1U,
                              std::numeric_limits<unsigned int>::max(), descriptor_limit);
}

[[nodiscard]] bool SetNonblocking(int descriptor) noexcept {
  const int flags = fcntl(descriptor, F_GETFL);
  return flags >= 0 && fcntl(descriptor, F_SETFL, flags | O_NONBLOCK) == 0;
}

[[nodiscard]] int DuplicatePrivateDescriptor(int descriptor) noexcept {
  int duplicate;
  do {
    duplicate = fcntl(descriptor, F_DUPFD_CLOEXEC, kMinimumPrivateDescriptor);
  } while (duplicate < 0 && errno == EINTR);
  return duplicate;
}

[[nodiscard]] std::string Decimal(std::uint64_t value) {
  std::array<char, 32> bytes{};
  const auto [end, error] = std::to_chars(bytes.data(), bytes.data() + bytes.size(), value);
  return error == std::errc{} ? std::string(bytes.data(), end) : std::string();
}

[[nodiscard]] std::vector<std::string> BuildWorkerArguments(
    std::string_view executable, Phase4TrialArm arm, const Phase4CanonicalCellConfig& cell,
    std::string_view imported_fixture_path) {
  std::vector<std::string> arguments;
  arguments.reserve(19);
  arguments.emplace_back(executable);
  arguments.emplace_back("--phase4_worker=1");
  arguments.emplace_back(arm == Phase4TrialArm::kSequentialBaseline ? "--arm=baseline"
                                                                    : "--arm=candidate");
  arguments.emplace_back("--case_id=" + Decimal(cell.case_id));
  arguments.emplace_back("--pool_size=" + Decimal(cell.requested_pool_size));
  arguments.emplace_back("--workers=" + Decimal(cell.preparation_worker_count));
  arguments.emplace_back("--repetitions=" + Decimal(cell.repetitions));
  arguments.emplace_back("--setup_ns=" + Decimal(cell.maximum_setup_elapsed_nanoseconds));
  arguments.emplace_back("--prepared_ns=" +
                         Decimal(cell.external_budget.maximum_prepared_elapsed_nanoseconds));
  arguments.emplace_back("--cold_ns=" +
                         Decimal(cell.external_budget.maximum_cold_elapsed_nanoseconds));
  arguments.emplace_back("--address_space_bytes=" +
                         Decimal(cell.external_budget.maximum_address_space_bytes));
  arguments.emplace_back("--peak_host_bytes=" +
                         Decimal(cell.external_budget.maximum_peak_host_bytes));
  arguments.emplace_back("--maximum_nets=" + Decimal(cell.corpus_limits.maximum_nets));
  arguments.emplace_back("--maximum_compiled_nodes=" +
                         Decimal(cell.corpus_limits.maximum_compiled_nodes));
  arguments.emplace_back("--maximum_compiled_host_bytes=" +
                         Decimal(cell.corpus_limits.maximum_compiled_host_bytes));
  arguments.emplace_back("--maximum_active_regions=" +
                         Decimal(cell.corpus_limits.maximum_active_regions));
  arguments.emplace_back("--maximum_board_entities=" +
                         Decimal(cell.corpus_limits.maximum_board_entities));
  arguments.emplace_back("--fixture_path=" + std::string(imported_fixture_path));
  arguments.emplace_back("--request_fd=" + Decimal(kWorkerRequestDescriptor));
  arguments.emplace_back("--response_fd=" + Decimal(kWorkerResponseDescriptor));
  return arguments;
}

struct ChildLaunchFailure {
  std::int32_t stage = 0;
  std::int32_t error_number = 0;
};

[[noreturn]] void ReportChildLaunchFailure(int descriptor, std::int32_t stage) noexcept {
  const ChildLaunchFailure failure{.stage = stage, .error_number = errno};
  const auto* bytes = reinterpret_cast<const char*>(&failure);
  std::size_t written = 0;
  while (written < sizeof(failure)) {
    const ssize_t result = write(descriptor, bytes + written, sizeof(failure) - written);
    if (result > 0) {
      written += static_cast<std::size_t>(result);
    } else if (result < 0 && errno == EINTR) {
      continue;
    } else {
      break;
    }
  }
  _exit(120 + stage);
}

[[nodiscard]] std::uint64_t ProcessIdentity(std::uint64_t run_identity, Phase4TrialArm arm,
                                            pid_t pid) noexcept {
  board_ir::StableHashBuilder hash;
  hash.AddString("APGAR-PHASE4-WORKER-PROCESS-V1");
  hash.AddU64(run_identity);
  hash.AddByte(static_cast<std::uint8_t>(arm));
  hash.AddU64(static_cast<std::uint64_t>(pid));
  const std::uint64_t result = hash.Finish();
  return result == 0 ? 1 : result;
}

void TerminateLaunchChild(pid_t pid, int* pid_descriptor) noexcept {
  if (pid <= 0) {
    CloseDescriptor(pid_descriptor);
    return;
  }
  (void)kill(-pid, SIGKILL);
  if (*pid_descriptor >= 0) {
    (void)SendPidDescriptorSignal(*pid_descriptor, SIGKILL);
  } else {
    (void)kill(pid, SIGKILL);
  }
  const Clock::time_point deadline = Clock::now() + kPostKillReapGrace;
  for (;;) {
    pid_t result;
    do {
      result = waitpid(pid, nullptr, WNOHANG);
    } while (result < 0 && errno == EINTR && Clock::now() < deadline);
    if (result == pid || (result < 0 && errno == ECHILD) || Clock::now() >= deadline) {
      break;
    }
    pollfd descriptor{.fd = *pid_descriptor, .events = POLLIN, .revents = 0};
    (void)poll(&descriptor, 1, std::min(10, RemainingMilliseconds(deadline)));
  }
  CloseDescriptor(pid_descriptor);
}

[[nodiscard]] LaunchResult LaunchWorker(std::string_view executable, Phase4TrialArm arm,
                                        const Phase4CanonicalCellConfig& cell,
                                        std::string_view imported_fixture_path,
                                        std::uint64_t run_identity, Clock::time_point deadline) {
#if defined(APGAR_PHASE4_TRIAL_FAULT_TEST_VARIANT)
  const char* const launch_fault_mode = std::getenv("APGAR_PHASE4_TRIAL_FAULT_MODE");
  if (launch_fault_mode != nullptr && std::string_view(launch_fault_mode) == "launch_delay") {
    (void)poll(nullptr, 0, 250);
  }
#endif
  if (Clock::now() >= deadline) {
    return LaunchError{.invariant_id = "P4HARNESS-LAUNCH-DEADLINE-001",
                       .detail = "the setup deadline expired before worker launch"};
  }
  const std::vector<std::string> arguments =
      BuildWorkerArguments(executable, arm, cell, imported_fixture_path);
  std::vector<char*> argument_pointers;
  argument_pointers.reserve(arguments.size() + 1);
  for (const std::string& argument : arguments) {
    argument_pointers.push_back(const_cast<char*>(argument.c_str()));
  }
  argument_pointers.push_back(nullptr);

  rlimit descriptor_limit{};
  if (getrlimit(RLIMIT_NOFILE, &descriptor_limit) != 0) {
    return LaunchError{
        .invariant_id = "P4HARNESS-LAUNCH-NOFILE-001",
        .detail = "failed to capture the inherited descriptor bound with errno " + Decimal(errno)};
  }
  int request_pipe[2] = {-1, -1};
  int response_pipe[2] = {-1, -1};
  int stderr_pipe[2] = {-1, -1};
  int exec_error_pipe[2] = {-1, -1};
  if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, request_pipe) != 0 ||
      socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, response_pipe) != 0 ||
      pipe2(stderr_pipe, O_CLOEXEC) != 0 || pipe2(exec_error_pipe, O_CLOEXEC) != 0) {
    const int saved_errno = errno;
    for (int* pipe : {request_pipe, response_pipe, stderr_pipe, exec_error_pipe}) {
      CloseDescriptor(&pipe[0]);
      CloseDescriptor(&pipe[1]);
    }
    return LaunchError{
        .invariant_id = "P4HARNESS-LAUNCH-CHANNEL-001",
        .detail = "worker channel creation failed with errno " + Decimal(saved_errno)};
  }

  int parent_request = DuplicatePrivateDescriptor(request_pipe[1]);
  int child_request = DuplicatePrivateDescriptor(request_pipe[0]);
  int parent_response = DuplicatePrivateDescriptor(response_pipe[0]);
  int child_response = DuplicatePrivateDescriptor(response_pipe[1]);
  int parent_stderr = DuplicatePrivateDescriptor(stderr_pipe[0]);
  int child_stderr = DuplicatePrivateDescriptor(stderr_pipe[1]);
  int parent_exec_error = DuplicatePrivateDescriptor(exec_error_pipe[0]);
  int child_exec_error = DuplicatePrivateDescriptor(exec_error_pipe[1]);
  for (int* pipe : {request_pipe, response_pipe, stderr_pipe, exec_error_pipe}) {
    CloseDescriptor(&pipe[0]);
    CloseDescriptor(&pipe[1]);
  }
  std::array<int*, 8> private_descriptors = {
      &parent_request, &child_request, &parent_response,   &child_response,
      &parent_stderr,  &child_stderr,  &parent_exec_error, &child_exec_error,
  };
  if (std::any_of(private_descriptors.begin(), private_descriptors.end(),
                  [](const int* descriptor) { return *descriptor < 0; })) {
    for (int* descriptor : private_descriptors) {
      CloseDescriptor(descriptor);
    }
    return LaunchError{.invariant_id = "P4HARNESS-LAUNCH-FD-001",
                       .detail = "failed to reserve collision-free private descriptors"};
  }

  const pid_t pid = fork();
  if (pid < 0) {
    const int saved_errno = errno;
    for (int* descriptor : private_descriptors) {
      CloseDescriptor(descriptor);
    }
    return LaunchError{.invariant_id = "P4HARNESS-LAUNCH-FORK-001",
                       .detail = "fork failed with errno " + Decimal(saved_errno)};
  }
  if (pid == 0) {
    CloseDescriptor(&parent_request);
    CloseDescriptor(&parent_response);
    CloseDescriptor(&parent_stderr);
    CloseDescriptor(&parent_exec_error);
    std::uint8_t launch_gate = 0;
    ssize_t gate_read;
    do {
      gate_read = read(child_request, &launch_gate, sizeof(launch_gate));
    } while (gate_read < 0 && errno == EINTR);
    if (gate_read != 1 || launch_gate != kLaunchGateToken) {
      errno = EPROTO;
      ReportChildLaunchFailure(child_exec_error, 6);
    }
    if (setpgid(0, 0) != 0) {
      ReportChildLaunchFailure(child_exec_error, 1);
    }
    if (prctl(PR_SET_PDEATHSIG, SIGKILL) != 0 || getppid() == 1) {
      errno = ECHILD;
      ReportChildLaunchFailure(child_exec_error, 2);
    }
    const rlimit address_space_limit{
        .rlim_cur = static_cast<rlim_t>(cell.external_budget.maximum_address_space_bytes),
        .rlim_max = static_cast<rlim_t>(cell.external_budget.maximum_address_space_bytes),
    };
    if (static_cast<std::uint64_t>(address_space_limit.rlim_cur) !=
            cell.external_budget.maximum_address_space_bytes ||
        setrlimit(RLIMIT_AS, &address_space_limit) != 0) {
      ReportChildLaunchFailure(child_exec_error, 3);
    }
    if (dup2(child_request, kWorkerRequestDescriptor) < 0 ||
        dup2(child_response, kWorkerResponseDescriptor) < 0 ||
        dup2(child_stderr, STDOUT_FILENO) < 0 || dup2(child_stderr, STDERR_FILENO) < 0) {
      ReportChildLaunchFailure(child_exec_error, 4);
    }
    if (!CloseUnrelatedChildDescriptors(child_exec_error, descriptor_limit.rlim_cur)) {
      ReportChildLaunchFailure(child_exec_error, 7);
    }
    execv(arguments.front().c_str(), argument_pointers.data());
    ReportChildLaunchFailure(child_exec_error, 5);
  }

  CloseDescriptor(&child_request);
  CloseDescriptor(&child_response);
  CloseDescriptor(&child_stderr);
  CloseDescriptor(&child_exec_error);
  int pid_descriptor = OpenPidDescriptor(pid);
  if (pid_descriptor < 0) {
    const int saved_errno = errno;
    TerminateLaunchChild(pid, &pid_descriptor);
    for (int* descriptor : private_descriptors) {
      CloseDescriptor(descriptor);
    }
    return LaunchError{
        .invariant_id = "P4HARNESS-LAUNCH-PIDFD-001",
        .detail = "failed to acquire an exact worker pidfd with errno " + Decimal(saved_errno)};
  }

  ssize_t gate_written;
  do {
    gate_written = write(parent_request, &kLaunchGateToken, sizeof(kLaunchGateToken));
  } while (gate_written < 0 && errno == EINTR);
  if (gate_written != 1) {
    const int saved_errno = errno;
    TerminateLaunchChild(pid, &pid_descriptor);
    for (int* descriptor :
         {&parent_request, &parent_response, &parent_stderr, &parent_exec_error}) {
      CloseDescriptor(descriptor);
    }
    return LaunchError{
        .invariant_id = "P4HARNESS-LAUNCH-GATE-001",
        .detail = "failed to release the pidfd-pinned worker with errno " + Decimal(saved_errno)};
  }
  if (!SetNonblocking(parent_response) || !SetNonblocking(parent_stderr) ||
      !SetNonblocking(parent_exec_error)) {
    const int saved_errno = errno;
    TerminateLaunchChild(pid, &pid_descriptor);
    for (int* descriptor :
         {&parent_request, &parent_response, &parent_stderr, &parent_exec_error}) {
      CloseDescriptor(descriptor);
    }
    return LaunchError{
        .invariant_id = "P4HARNESS-LAUNCH-NONBLOCK-001",
        .detail = "failed to configure parent descriptors with errno " + Decimal(saved_errno)};
  }
  if (setpgid(pid, pid) != 0 && errno != EACCES && errno != ESRCH) {
    const int saved_errno = errno;
    TerminateLaunchChild(pid, &pid_descriptor);
    for (int* descriptor :
         {&parent_request, &parent_response, &parent_stderr, &parent_exec_error}) {
      CloseDescriptor(descriptor);
    }
    return LaunchError{
        .invariant_id = "P4HARNESS-LAUNCH-PGROUP-001",
        .detail = "parent process-group confirmation failed with errno " + Decimal(saved_errno)};
  }
  return WorkerProcess{
      .arm = arm,
      .pid = pid,
      .pid_descriptor = pid_descriptor,
      .request_descriptor = parent_request,
      .response_descriptor = parent_response,
      .stderr_descriptor = parent_stderr,
      .exec_error_descriptor = parent_exec_error,
      .process_instance_identity = ProcessIdentity(run_identity, arm, pid),
      .ready_identity = std::nullopt,
      .bounded_stderr = {},
  };
}

[[nodiscard]] bool DrainStderr(WorkerProcess* worker, Clock::time_point deadline) noexcept {
  std::array<char, 4096> buffer{};
  std::size_t drained = 0;
  while (drained < kMaximumDrainBytesPerPass) {
    if (Clock::now() >= deadline) {
      return false;
    }
    const ssize_t count = read(worker->stderr_descriptor, buffer.data(), buffer.size());
    if (count > 0) {
      drained += static_cast<std::size_t>(count);
      const std::size_t remaining =
          kMaximumCapturedStderrBytes -
          std::min(worker->bounded_stderr.size(), kMaximumCapturedStderrBytes);
      worker->bounded_stderr.append(buffer.data(),
                                    std::min(remaining, static_cast<std::size_t>(count)));
      if (Clock::now() >= deadline) {
        return false;
      }
      continue;
    }
    if (count < 0 && errno == EINTR) {
      continue;
    }
    break;
  }
  return Clock::now() < deadline;
}

[[nodiscard]] bool DrainTerminalResponse(WorkerProcess* worker,
                                         Clock::time_point deadline) noexcept {
  std::array<std::uint8_t, 4096> bytes{};
  std::size_t drained = 0;
  while (drained < kMaximumDrainBytesPerPass && !worker->response_eof_observed) {
    if (Clock::now() >= deadline) {
      return false;
    }
    const ssize_t count = read(worker->response_descriptor, bytes.data(), bytes.size());
    if (count > 0) {
      drained += static_cast<std::size_t>(count);
      if (worker->stop_acknowledged) {
        worker->response_trailing_observed = true;
      }
      if (Clock::now() >= deadline) {
        return false;
      }
      continue;
    }
    if (count == 0) {
      worker->response_eof_observed = true;
      return Clock::now() < deadline;
    }
    if (errno == EINTR) {
      continue;
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      break;
    }
    worker->response_read_failed = true;
    break;
  }
  return Clock::now() < deadline;
}

[[nodiscard]] ReapState ObserveWorkerExit(WorkerProcess* worker) noexcept {
  if (worker->reaped) {
    return ReapState::kReaped;
  }
  if (worker->wait_error_number != 0) {
    return ReapState::kError;
  }
  if (worker->exit_observed) {
    return ReapState::kExited;
  }
  siginfo_t information{};
  int result;
  do {
    result = waitid(kPidfdWaitIdType, static_cast<id_t>(worker->pid_descriptor), &information,
                    WEXITED | WNOHANG | WNOWAIT);
  } while (result < 0 && errno == EINTR);
  if (result < 0) {
    worker->wait_error_number = errno;
    return ReapState::kError;
  }
  if (information.si_pid == 0) {
    return ReapState::kRunning;
  }
  if (information.si_pid != worker->pid) {
    worker->wait_error_number = ECHILD;
    return ReapState::kError;
  }
  worker->exit_observed = true;
  worker->observed_exit_code = information.si_code;
  worker->observed_exit_status = information.si_status;
  return ReapState::kExited;
}

[[nodiscard]] ReapState FinalReapNonblocking(WorkerProcess* worker) noexcept {
  if (worker->reaped) {
    return ReapState::kReaped;
  }
  if (worker->wait_error_number != 0) {
    return ReapState::kError;
  }
  pid_t result;
  do {
    result = wait4(worker->pid, &worker->wait_status, WNOHANG, &worker->usage);
  } while (result < 0 && errno == EINTR);
  if (result == worker->pid) {
    worker->reaped = true;
    return ReapState::kReaped;
  }
  if (result == 0) {
    return worker->exit_observed ? ReapState::kExited : ReapState::kRunning;
  }
  worker->wait_error_number = errno;
  return ReapState::kError;
}

[[nodiscard]] AwaitResult AwaitMessage(WorkerProcess* worker, Clock::time_point start,
                                       Clock::time_point deadline) {
  std::vector<std::uint8_t> frame;
  frame.reserve(internal::kPhase4TrialWireMaxFrameBytesV1);
  std::optional<std::size_t> expected_size;
  const auto timed_out = [&] {
    return AwaitResult{.kind = AwaitKind::kTimeout,
                       .message = std::nullopt,
                       .invariant_id = "P4HARNESS-WATCHDOG-001",
                       .detail = "the absolute monotonic deadline expired",
                       .elapsed_nanoseconds = ElapsedNanoseconds(start, Clock::now())};
  };
  for (;;) {
    if (Clock::now() >= deadline) {
      return timed_out();
    }
    std::array<pollfd, 3> descriptors = {
        pollfd{.fd = worker->response_descriptor, .events = POLLIN, .revents = 0},
        pollfd{.fd = worker->stderr_descriptor, .events = POLLIN, .revents = 0},
        pollfd{.fd = worker->exec_error_descriptor, .events = POLLIN, .revents = 0},
    };
    const int poll_result =
        poll(descriptors.data(), descriptors.size(), RemainingMilliseconds(deadline));
    if (poll_result < 0 && errno == EINTR) {
      continue;
    }
    if (poll_result < 0) {
      return AwaitResult{.kind = AwaitKind::kProtocol,
                         .message = std::nullopt,
                         .invariant_id = "P4HARNESS-POLL-001",
                         .detail = "poll failed with errno " + Decimal(errno),
                         .elapsed_nanoseconds = ElapsedNanoseconds(start, Clock::now())};
    }
    if (Clock::now() >= deadline) {
      return timed_out();
    }
    if ((descriptors[1].revents & (POLLIN | POLLHUP | POLLERR)) != 0) {
      if (!DrainStderr(worker, deadline)) {
        return timed_out();
      }
    }
    if (!worker->exec_confirmed && (descriptors[2].revents & (POLLIN | POLLHUP | POLLERR)) != 0) {
      ChildLaunchFailure failure;
      const ssize_t count = read(worker->exec_error_descriptor, &failure, sizeof(failure));
      if (Clock::now() >= deadline) {
        return timed_out();
      }
      if (count == 0) {
        worker->exec_confirmed = true;
        CloseDescriptor(&worker->exec_error_descriptor);
      } else if (count > 0) {
        return AwaitResult{.kind = AwaitKind::kLaunch,
                           .message = std::nullopt,
                           .invariant_id = "P4HARNESS-EXEC-001",
                           .detail = "worker pre-exec stage " + Decimal(failure.stage) +
                                     " failed with errno " + Decimal(failure.error_number),
                           .elapsed_nanoseconds = ElapsedNanoseconds(start, Clock::now())};
      }
    }
    if ((descriptors[0].revents & (POLLIN | POLLHUP | POLLERR)) != 0) {
      std::array<std::uint8_t, 4096> bytes{};
      for (;;) {
        if (Clock::now() >= deadline) {
          return timed_out();
        }
        const std::size_t remaining =
            expected_size.has_value() ? *expected_size - frame.size()
                                      : internal::kPhase4TrialWireMaxFrameBytesV1 - frame.size();
        if (remaining == 0) {
          break;
        }
        const ssize_t count =
            read(worker->response_descriptor, bytes.data(), std::min(remaining, bytes.size()));
        if (count > 0) {
          frame.insert(frame.end(), bytes.begin(), bytes.begin() + count);
          if (Clock::now() >= deadline) {
            return timed_out();
          }
          const auto inspected = internal::Phase4TrialWireExpectedFrameSizeV1(frame);
          if (std::holds_alternative<internal::Phase4TrialWireError>(inspected)) {
            const auto& error = std::get<internal::Phase4TrialWireError>(inspected);
            return AwaitResult{.kind = AwaitKind::kProtocol,
                               .message = std::nullopt,
                               .invariant_id = error.invariant_id,
                               .detail = error.detail,
                               .elapsed_nanoseconds = ElapsedNanoseconds(start, Clock::now())};
          }
          expected_size = std::get<std::optional<std::size_t>>(inspected);
          if (expected_size.has_value() && frame.size() > *expected_size) {
            return AwaitResult{
                .kind = AwaitKind::kProtocol,
                .message = std::nullopt,
                .invariant_id = "P4HARNESS-FRAME-TRAILING-001",
                .detail = "response channel coalesced bytes after the declared frame",
                .elapsed_nanoseconds = ElapsedNanoseconds(start, Clock::now())};
          }
          if (expected_size.has_value() && frame.size() == *expected_size) {
            auto decoded = internal::DecodePhase4TrialWireMessageV1(frame);
            if (std::holds_alternative<internal::Phase4TrialWireError>(decoded)) {
              const auto& error = std::get<internal::Phase4TrialWireError>(decoded);
              return AwaitResult{.kind = AwaitKind::kProtocol,
                                 .message = std::nullopt,
                                 .invariant_id = error.invariant_id,
                                 .detail = error.detail,
                                 .elapsed_nanoseconds = ElapsedNanoseconds(start, Clock::now())};
            }
            if (Clock::now() >= deadline) {
              return timed_out();
            }
            return AwaitResult{.kind = AwaitKind::kMessage,
                               .message = std::get<Phase4TrialWireMessage>(std::move(decoded)),
                               .invariant_id = {},
                               .detail = {},
                               .elapsed_nanoseconds = ElapsedNanoseconds(start, Clock::now())};
          }
          continue;
        }
        if (count < 0 && errno == EINTR) {
          continue;
        }
        if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
          break;
        }
        if (count == 0) {
          break;
        }
        return AwaitResult{.kind = AwaitKind::kProtocol,
                           .message = std::nullopt,
                           .invariant_id = "P4HARNESS-READ-001",
                           .detail = "protocol read failed with errno " + Decimal(errno),
                           .elapsed_nanoseconds = ElapsedNanoseconds(start, Clock::now())};
      }
    }
    if (Clock::now() >= deadline) {
      return timed_out();
    }
    const ReapState reap_state = ObserveWorkerExit(worker);
    if (Clock::now() >= deadline) {
      return timed_out();
    }
    if (reap_state == ReapState::kError) {
      return AwaitResult{
          .kind = AwaitKind::kProcessExit,
          .message = std::nullopt,
          .invariant_id = "P4HARNESS-PIDFD-WAIT-001",
          .detail = "pidfd wait authority failed with errno " + Decimal(worker->wait_error_number),
          .elapsed_nanoseconds = ElapsedNanoseconds(start, Clock::now())};
    }
    if (reap_state == ReapState::kExited || reap_state == ReapState::kReaped) {
      return AwaitResult{.kind = AwaitKind::kProcessExit,
                         .message = std::nullopt,
                         .invariant_id = "P4HARNESS-EXIT-001",
                         .detail = "worker exited before producing a complete response frame",
                         .elapsed_nanoseconds = ElapsedNanoseconds(start, Clock::now())};
    }
  }
}

[[nodiscard]] bool KillWorker(WorkerProcess* worker) noexcept {
  if (worker->pid <= 0 || worker->pid_descriptor < 0 || worker->reaped ||
      worker->wait_error_number != 0) {
    return false;
  }
  const ReapState state = ObserveWorkerExit(worker);
  if (state == ReapState::kReaped || state == ReapState::kError) {
    return false;
  }
  // No wait4 has run while this pidfd-observed child remains unreaped, so its
  // process-group number cannot have been recycled between observation and kill.
  const bool group_signalled = kill(-worker->pid, SIGKILL) == 0;
  const bool leader_signalled =
      state == ReapState::kRunning && SendPidDescriptorSignal(worker->pid_descriptor, SIGKILL);
  return group_signalled || leader_signalled;
}

[[nodiscard]] ReapResult ReapWorker(WorkerProcess* worker, Clock::time_point deadline) noexcept {
  ReapResult result;
  CloseDescriptor(&worker->request_descriptor);
  ReapState state = ReapState::kRunning;
  const auto terminal_resolved = [&] {
    return worker->response_eof_observed || worker->response_trailing_observed ||
           worker->response_read_failed;
  };
  for (;;) {
    if (Clock::now() >= deadline) {
      break;
    }
    if (!DrainStderr(worker, deadline) || !DrainTerminalResponse(worker, deadline)) {
      break;
    }
    if (worker->response_trailing_observed || worker->response_read_failed) {
      break;
    }
    state = ObserveWorkerExit(worker);
    if (Clock::now() >= deadline || state == ReapState::kError) {
      break;
    }
    if (terminal_resolved() && state == ReapState::kExited) {
      state = FinalReapNonblocking(worker);
    }
    if (state == ReapState::kReaped || state == ReapState::kError) {
      break;
    }
    std::array<pollfd, 3> descriptors = {
        pollfd{.fd = worker->pid_descriptor, .events = POLLIN, .revents = 0},
        pollfd{.fd = worker->stderr_descriptor, .events = POLLIN, .revents = 0},
        pollfd{.fd = worker->response_descriptor, .events = POLLIN, .revents = 0},
    };
    (void)poll(descriptors.data(), descriptors.size(),
               std::min(10, RemainingMilliseconds(deadline)));
  }

  const bool protocol_violation =
      worker->response_trailing_observed || worker->response_read_failed;
  const bool clean_complete =
      state == ReapState::kReaped && worker->response_eof_observed && !protocol_violation;
  if (!clean_complete) {
    result.teardown_deadline_expired =
        Clock::now() >= deadline && (state != ReapState::kReaped || !terminal_resolved());
    result.kill_signal_sent = KillWorker(worker);
  }

  if (!clean_complete) {
    const Clock::time_point cleanup_deadline = Clock::now() + kPostKillReapGrace;
    while (Clock::now() < cleanup_deadline) {
      (void)DrainStderr(worker, cleanup_deadline);
      (void)DrainTerminalResponse(worker, cleanup_deadline);
      state = ObserveWorkerExit(worker);
      if (state == ReapState::kExited && terminal_resolved()) {
        state = FinalReapNonblocking(worker);
      }
      if (state == ReapState::kReaped || state == ReapState::kError) {
        break;
      }
      std::array<pollfd, 3> descriptors = {
          pollfd{.fd = worker->pid_descriptor, .events = POLLIN, .revents = 0},
          pollfd{.fd = worker->stderr_descriptor, .events = POLLIN, .revents = 0},
          pollfd{.fd = worker->response_descriptor, .events = POLLIN, .revents = 0},
      };
      (void)poll(descriptors.data(), descriptors.size(),
                 std::min(10, RemainingMilliseconds(cleanup_deadline)));
    }
  }
  (void)DrainStderr(worker, Clock::time_point::max());
  (void)DrainTerminalResponse(worker, Clock::time_point::max());
  if (state == ReapState::kExited) {
    // A missing terminal EOF at the end of the bounded grace is itself the
    // resolved protocol failure; only now may exact wait4 release the PID.
    state = FinalReapNonblocking(worker);
  }
  result.post_kill_reap_incomplete = state != ReapState::kReaped;
  CloseWorkerDescriptors(worker);
  return result;
}

[[nodiscard]] std::uint64_t PeakHostBytes(const WorkerProcess& worker) noexcept {
  if (!worker.reaped || worker.wait_error_number != 0 || worker.usage.ru_maxrss <= 0) {
    return 0;
  }
  const std::uint64_t kibibytes = static_cast<std::uint64_t>(worker.usage.ru_maxrss);
  return kibibytes > std::numeric_limits<std::uint64_t>::max() / 1024ULL
             ? std::numeric_limits<std::uint64_t>::max()
             : kibibytes * 1024ULL;
}

[[nodiscard]] std::int32_t ExitCode(const WorkerProcess& worker) noexcept {
  if (worker.reaped && worker.wait_error_number == 0 && WIFEXITED(worker.wait_status)) {
    return WEXITSTATUS(worker.wait_status);
  }
  return worker.exit_observed && worker.observed_exit_code == CLD_EXITED
             ? worker.observed_exit_status
             : -1;
}

[[nodiscard]] std::int32_t TerminatingSignal(const WorkerProcess& worker) noexcept {
  if (worker.reaped && worker.wait_error_number == 0 && WIFSIGNALED(worker.wait_status)) {
    return WTERMSIG(worker.wait_status);
  }
  return worker.exit_observed && (worker.observed_exit_code == CLD_KILLED ||
                                  worker.observed_exit_code == CLD_DUMPED)
             ? worker.observed_exit_status
             : 0;
}

[[nodiscard]] Phase4IsolatedAttemptDisposition DispositionForAwaitFailure(
    const AwaitResult& failure, const WorkerProcess& worker, bool setup) noexcept {
  if (failure.kind == AwaitKind::kTimeout) {
    return setup ? Phase4IsolatedAttemptDisposition::kSetupTimeout
                 : Phase4IsolatedAttemptDisposition::kWallTimeout;
  }
  if (failure.kind == AwaitKind::kLaunch) {
    return Phase4IsolatedAttemptDisposition::kLaunchFailure;
  }
  if (failure.kind == AwaitKind::kProcessExit) {
    if (TerminatingSignal(worker) != 0) {
      return Phase4IsolatedAttemptDisposition::kSignalTermination;
    }
    if (ExitCode(worker) != 0) {
      return Phase4IsolatedAttemptDisposition::kNonzeroExit;
    }
  }
  return Phase4IsolatedAttemptDisposition::kProtocolFailure;
}

[[nodiscard]] Phase4HostEnvironment CaptureHostEnvironment() {
  Phase4HostEnvironment environment;
  utsname identity{};
  if (uname(&identity) == 0) {
    environment.host_os = identity.sysname;
    environment.host_kernel = identity.release;
    environment.host_architecture = identity.machine;
  }
  std::ifstream cpuinfo("/proc/cpuinfo");
  std::string line;
  while (std::getline(cpuinfo, line)) {
    constexpr std::string_view prefix = "model name";
    if (line.starts_with(prefix)) {
      const std::size_t separator = line.find(':');
      if (separator != std::string::npos) {
        environment.cpu_model = line.substr(separator + 1);
        environment.cpu_model.erase(
            environment.cpu_model.begin(),
            std::find_if(environment.cpu_model.begin(), environment.cpu_model.end(),
                         [](unsigned char value) { return value != ' ' && value != '\t'; }));
      }
      break;
    }
  }
#if defined(__clang__)
  environment.compiler_identity = "clang-" __clang_version__;
#elif defined(__GNUC__)
  environment.compiler_identity = "gcc-" __VERSION__;
#else
  environment.compiler_identity = "unknown";
#endif
  const long online = sysconf(_SC_NPROCESSORS_ONLN);
  environment.online_cpu_count = online > 0 ? static_cast<std::uint64_t>(online) : 0;
  cpu_set_t affinity;
  CPU_ZERO(&affinity);
  if (sched_getaffinity(0, sizeof(affinity), &affinity) == 0) {
    environment.affinity_cpu_count = static_cast<std::uint64_t>(CPU_COUNT(&affinity));
  }
  std::ifstream meminfo("/proc/meminfo");
  while (std::getline(meminfo, line)) {
    constexpr std::string_view prefix = "MemTotal:";
    if (line.starts_with(prefix)) {
      const std::size_t first_digit = line.find_first_of("0123456789");
      std::uint64_t kibibytes = 0;
      if (first_digit != std::string::npos) {
        const char* begin = line.data() + first_digit;
        const auto [end, error] = std::from_chars(begin, line.data() + line.size(), kibibytes);
        if (error == std::errc{} && end != begin &&
            kibibytes <= std::numeric_limits<std::uint64_t>::max() / 1024ULL) {
          environment.total_host_memory_bytes = kibibytes * 1024ULL;
        }
      }
      break;
    }
  }
  board_ir::StableHashBuilder hash;
  hash.AddString("APGAR-PHASE4-HOST-ENVIRONMENT-V1");
  hash.AddU32(environment.schema_version);
  hash.AddString(environment.host_os);
  hash.AddString(environment.host_kernel);
  hash.AddString(environment.host_architecture);
  hash.AddString(environment.cpu_model);
  hash.AddString(environment.compiler_identity);
  hash.AddString(environment.monotonic_clock);
  hash.AddU64(environment.online_cpu_count);
  hash.AddU64(environment.affinity_cpu_count);
  hash.AddU64(environment.total_host_memory_bytes);
  environment.environment_checksum = hash.Finish();
  return environment;
}

[[nodiscard]] std::uint64_t ControllerIdentity() noexcept {
  board_ir::StableHashBuilder hash;
  hash.AddString("APGAR-PHASE4-CONTROLLER-V1");
  hash.AddU64(static_cast<std::uint64_t>(getpid()));
  hash.AddU64(static_cast<std::uint64_t>(Clock::now().time_since_epoch().count()));
  const std::uint64_t value = hash.Finish();
  return value == 0 ? 1 : value;
}

[[nodiscard]] std::uint64_t RunIdentity(const Phase4CanonicalCellConfig& cell,
                                        std::uint64_t controller_identity) noexcept {
  board_ir::StableHashBuilder hash;
  hash.AddString("APGAR-PHASE4-AUTHORITY-RUN-V1");
  hash.AddU64(controller_identity);
  hash.AddU32(cell.case_id);
  hash.AddU32(cell.requested_pool_size);
  const std::uint64_t value = hash.Finish();
  return value == 0 ? 1 : value;
}

void InitializeAttempts(Phase4IsolatedCellResult* result) {
  result->attempts.reserve(result->config.repetitions);
  for (std::uint32_t repetition = 0; repetition < result->config.repetitions; ++repetition) {
    const Phase4TrialOrder order =
        repetition % 2 == 0 ? Phase4TrialOrder::kBaselineFirst : Phase4TrialOrder::kCandidateFirst;
    auto spec_result = BuildPhase4CanonicalTrialSpecV1(result->config, repetition, order);
    const std::uint64_t root_seed = std::holds_alternative<Phase4PairedTrialSpec>(spec_result)
                                        ? std::get<Phase4PairedTrialSpec>(spec_result).root_seed
                                        : 0;
    Phase4IsolatedPairAttempt attempt;
    attempt.case_id = result->config.case_id;
    attempt.requested_pool_size = result->config.requested_pool_size;
    attempt.repetition_index = repetition;
    attempt.root_seed = root_seed;
    attempt.execution_order = order;
    attempt.baseline.arm = Phase4TrialArm::kSequentialBaseline;
    attempt.baseline.repetition_index = repetition;
    attempt.baseline.execution_order = order;
    attempt.baseline.disposition = Phase4IsolatedAttemptDisposition::kNotRunAfterFatal;
    attempt.candidate.arm = Phase4TrialArm::kReusableCandidateAllocation;
    attempt.candidate.repetition_index = repetition;
    attempt.candidate.execution_order = order;
    attempt.candidate.disposition = Phase4IsolatedAttemptDisposition::kNotRunAfterFatal;
    result->attempts.push_back(std::move(attempt));
  }
}

[[nodiscard]] Phase4IsolatedArmAttempt* ArmAttempt(Phase4IsolatedPairAttempt* pair,
                                                   Phase4TrialArm arm) noexcept {
  return arm == Phase4TrialArm::kSequentialBaseline ? &pair->baseline : &pair->candidate;
}

void SetControllerFailure(Phase4IsolatedArmAttempt* attempt,
                          Phase4IsolatedAttemptDisposition disposition, const AwaitResult& failure,
                          const WorkerProcess* worker, std::uint64_t dispatch_ordinal) {
  attempt->disposition = disposition;
  attempt->dispatch_ordinal = dispatch_ordinal;
  attempt->outer_elapsed_nanoseconds = failure.elapsed_nanoseconds;
  attempt->controller_invariant_id = failure.invariant_id;
  attempt->controller_detail = failure.detail;
  if (worker != nullptr) {
    attempt->process_instance_identity = worker->process_instance_identity;
  }
}

[[nodiscard]] std::uint64_t ArmAttemptChecksum(const Phase4IsolatedArmAttempt& attempt) noexcept {
  board_ir::StableHashBuilder hash;
  hash.AddString("APGAR-PHASE4-ISOLATED-ARM-ATTEMPT-V1");
  hash.AddU32(attempt.schema_version);
  hash.AddByte(static_cast<std::uint8_t>(attempt.arm));
  hash.AddU32(attempt.repetition_index);
  hash.AddByte(static_cast<std::uint8_t>(attempt.execution_order));
  hash.AddByte(static_cast<std::uint8_t>(attempt.disposition));
  hash.AddU64(attempt.dispatch_ordinal);
  hash.AddU64(attempt.process_instance_identity);
  hash.AddU64(attempt.outer_elapsed_nanoseconds);
  hash.AddU64(attempt.process_lifetime_peak_host_bytes);
  hash.AddU32(static_cast<std::uint32_t>(attempt.raw_wait_status));
  hash.AddU32(static_cast<std::uint32_t>(attempt.process_exit_code));
  hash.AddU32(static_cast<std::uint32_t>(attempt.terminating_signal));
  hash.AddBool(attempt.watchdog_kill_sent);
  hash.AddString(attempt.controller_invariant_id);
  hash.AddString(attempt.controller_detail);
  hash.AddBool(attempt.record.has_value());
  hash.AddU64(attempt.record.has_value() ? attempt.record->artifact_checksum : 0);
  hash.AddBool(attempt.child_failure.has_value());
  hash.AddU64(attempt.child_failure.has_value() ? attempt.child_failure->payload_checksum : 0);
  return hash.Finish();
}

void FinalizeChecksums(Phase4IsolatedCellResult* result) noexcept {
  board_ir::StableHashBuilder artifact;
  artifact.AddString("APGAR-PHASE4-ISOLATED-CELL-ARTIFACT-V1");
  artifact.AddU32(result->schema_version);
  artifact.AddU64(result->corpus_checksum);
  artifact.AddU64(result->cell_plan_checksum);
  artifact.AddU64(result->environment.environment_checksum);
  artifact.AddU64(result->authority_run_identity);
  artifact.AddU64(result->controller_identity);
  artifact.AddU64(result->attempts.size());
  for (Phase4IsolatedPairAttempt& pair : result->attempts) {
    pair.baseline.attempt_checksum = ArmAttemptChecksum(pair.baseline);
    pair.candidate.attempt_checksum = ArmAttemptChecksum(pair.candidate);
    board_ir::StableHashBuilder pair_hash;
    pair_hash.AddString("APGAR-PHASE4-ISOLATED-PAIR-ATTEMPT-V1");
    pair_hash.AddU32(pair.schema_version);
    pair_hash.AddU32(pair.case_id);
    pair_hash.AddU32(pair.requested_pool_size);
    pair_hash.AddU32(pair.repetition_index);
    pair_hash.AddU64(pair.root_seed);
    pair_hash.AddByte(static_cast<std::uint8_t>(pair.execution_order));
    pair_hash.AddU64(pair.baseline.attempt_checksum);
    pair_hash.AddU64(pair.candidate.attempt_checksum);
    pair_hash.AddBool(pair.result.has_value());
    pair_hash.AddU64(pair.result.has_value() ? pair.result->artifact_checksum : 0);
    pair.attempt_checksum = pair_hash.Finish();
    artifact.AddU64(pair.attempt_checksum);
  }
  result->artifact_checksum = artifact.Finish();
}

void InvalidateWorkerSuccesses(Phase4IsolatedCellResult* result, const WorkerProcess& worker,
                               std::vector<std::optional<Phase4TrialArmExecution>>* executions,
                               Phase4IsolatedAttemptDisposition disposition,
                               std::string_view invariant, std::string_view detail,
                               bool watchdog_kill_sent) {
  for (std::size_t index = 0; index < result->attempts.size(); ++index) {
    Phase4IsolatedArmAttempt* attempt = ArmAttempt(&result->attempts[index], worker.arm);
    if (attempt->process_instance_identity == worker.process_instance_identity &&
        attempt->disposition == Phase4IsolatedAttemptDisposition::kSuccess) {
      attempt->disposition = disposition;
      attempt->controller_invariant_id = std::string(invariant);
      attempt->controller_detail = std::string(detail);
      attempt->watchdog_kill_sent = watchdog_kill_sent;
      (*executions)[index].reset();
    }
  }
}

void ApplyProcessExit(Phase4IsolatedCellResult* result, WorkerProcess* worker,
                      std::vector<std::optional<Phase4TrialArmExecution>>* executions) {
  const std::uint64_t peak = PeakHostBytes(*worker);
  const std::int32_t exit_code = ExitCode(*worker);
  const std::int32_t signal = TerminatingSignal(*worker);
  for (std::size_t index = 0; index < result->attempts.size(); ++index) {
    Phase4IsolatedArmAttempt* attempt = ArmAttempt(&result->attempts[index], worker->arm);
    if (attempt->process_instance_identity != worker->process_instance_identity) {
      continue;
    }
    attempt->process_lifetime_peak_host_bytes = peak;
    attempt->raw_wait_status = worker->wait_status;
    attempt->process_exit_code = exit_code;
    attempt->terminating_signal = signal;
    if ((worker->wait_error_number != 0 || exit_code != 0 || signal != 0) &&
        attempt->disposition == Phase4IsolatedAttemptDisposition::kSuccess) {
      attempt->disposition =
          worker->wait_error_number != 0
              ? Phase4IsolatedAttemptDisposition::kProtocolFailure
              : (signal != 0 ? Phase4IsolatedAttemptDisposition::kSignalTermination
                             : Phase4IsolatedAttemptDisposition::kNonzeroExit);
      attempt->controller_invariant_id = worker->wait_error_number != 0
                                             ? "P4HARNESS-WAIT4-AUTHORITY-001"
                                             : "P4HARNESS-LIFETIME-EXIT-001";
      attempt->controller_detail = worker->wait_error_number != 0
                                       ? "the controller could not reap the exact worker PID"
                                       : "a later abnormal worker exit invalidated this "
                                         "process-lifetime observation";
      (*executions)[index].reset();
    }
  }
  if (!worker->bounded_stderr.empty()) {
    InvalidateWorkerSuccesses(
        result, *worker, executions, Phase4IsolatedAttemptDisposition::kProtocolFailure,
        "P4HARNESS-WORKER-OUTPUT-001",
        "worker wrote unexpected stdout or stderr during a successful cell", false);
  }
}

[[nodiscard]] Phase4ExternalResourceObservation MakeObservation(
    const Phase4IsolatedCellResult& cell, const Phase4IsolatedArmAttempt& attempt,
    const Phase4TrialArmExecution& execution) noexcept {
  Phase4ExternalResourceObservation observation{
      .authority_run_identity = cell.authority_run_identity,
      .controller_identity = cell.controller_identity,
      .process_instance_identity = attempt.process_instance_identity,
      .associated_semantic_checksum = execution.semantics.semantic_checksum,
      .configured_wall_limit_nanoseconds =
          cell.config.external_budget.maximum_cold_elapsed_nanoseconds,
      .configured_address_space_limit_bytes =
          cell.config.external_budget.maximum_address_space_bytes,
      .configured_peak_host_limit_bytes = cell.config.external_budget.maximum_peak_host_bytes,
      .outer_elapsed_nanoseconds = attempt.outer_elapsed_nanoseconds,
      .peak_host_bytes = attempt.process_lifetime_peak_host_bytes,
      .process_exit_code = attempt.process_exit_code,
      .isolated_process = true,
      .wall_authority_enforced = true,
      .memory_authority_enforced = true,
      .persistent_preparer_reused = attempt.arm == Phase4TrialArm::kReusableCandidateAllocation,
      .preparer_lifecycle = execution.preparer_lifecycle,
  };
  observation.authority_checksum = internal::ComputePhase4ExternalAuthorityChecksumV1(observation);
  return observation;
}

[[nodiscard]] Phase4DurableArmFailure SummaryFailure(
    Phase4TrialArm arm, Phase4PairedTrialErrorCode code, std::string_view invariant,
    std::string_view detail, std::uint64_t required = 0, std::uint64_t configured = 0) {
  return ReconcilePhase4TrialArmFailureV1(Phase4TrialArmFailure{
      .summary = Phase4PairedTrialError{.code = code,
                                        .invariant_id = invariant,
                                        .detail = detail,
                                        .arm = arm,
                                        .required = required,
                                        .configured = configured},
      .payload = std::monostate{},
  });
}

[[nodiscard]] internal::Phase4TrialWireReady ReadyIdentity(
    const Phase4TrialArmSemantics& semantics) noexcept {
  return internal::Phase4TrialWireReady{
      .case_id = semantics.case_id,
      .descriptor_fingerprint = semantics.descriptor_fingerprint,
      .case_checksum = semantics.case_checksum,
      .board_content_hash = semantics.board_content_hash,
      .workload_checksum = semantics.workload_checksum,
      .capacity_model_checksum = semantics.capacity_model_checksum,
  };
}

[[nodiscard]] bool MatchesReadyIdentity(const internal::Phase4TrialWireReady& ready,
                                        const Phase4TrialArmSemantics& semantics) noexcept {
  return ready == ReadyIdentity(semantics);
}

[[nodiscard]] bool MatchesReadyIdentity(const internal::Phase4TrialWireReady& ready,
                                        const Phase4DurableArmFailure& failure) noexcept {
  if (failure.payload_kind == Phase4DurableFailurePayloadKind::kSummaryOnly) {
    return true;
  }
  if (failure.payload_kind == Phase4DurableFailurePayloadKind::kCorpus) {
    return failure.case_id == ready.case_id;
  }
  return failure.has_case_identity && failure.case_id == ready.case_id &&
         failure.descriptor_fingerprint == ready.descriptor_fingerprint &&
         failure.case_checksum == ready.case_checksum &&
         failure.board_content_hash == ready.board_content_hash &&
         failure.workload_checksum == ready.workload_checksum &&
         failure.capacity_model_checksum == ready.capacity_model_checksum;
}

[[nodiscard]] bool ReadyMatchesCell(const internal::Phase4TrialWireReady& ready,
                                    const Phase4CanonicalCellConfig& cell) noexcept {
  const Phase4CaseDescriptor* descriptor = FindPhase4CaseDescriptorV1(cell.case_id);
  return descriptor != nullptr && ready.case_id == cell.case_id &&
         ready.descriptor_fingerprint == FingerprintPhase4CaseDescriptorV1(*descriptor);
}

[[nodiscard]] bool FailureMatchesCell(const Phase4DurableArmFailure& failure,
                                      const Phase4CanonicalCellConfig& cell,
                                      const internal::Phase4TrialWireReady* peer_ready) noexcept {
  if (failure.payload_kind == Phase4DurableFailurePayloadKind::kSummaryOnly) {
    return true;
  }
  const Phase4CaseDescriptor* descriptor = FindPhase4CaseDescriptorV1(cell.case_id);
  const bool matches_active_cell =
      failure.case_id == cell.case_id &&
      (failure.payload_kind == Phase4DurableFailurePayloadKind::kCorpus ||
       (failure.has_case_identity && descriptor != nullptr &&
        failure.descriptor_fingerprint == FingerprintPhase4CaseDescriptorV1(*descriptor)));
  return matches_active_cell &&
         (peer_ready == nullptr || MatchesReadyIdentity(*peer_ready, failure));
}

[[nodiscard]] bool SendWorkerMessage(int descriptor,
                                     const Phase4TrialWireMessage& message) noexcept {
  auto result = internal::WritePhase4TrialWireMessageV1(descriptor, message);
  return std::holds_alternative<std::monostate>(result);
}

}  // namespace

Phase4IsolatedCellExecution RunPhase4IsolatedCellV1(const Phase4CanonicalCellConfig& cell,
                                                    std::string_view worker_executable,
                                                    std::string_view imported_fixture_path) {
  auto initial_spec = BuildPhase4CanonicalTrialSpecV1(cell, 0, Phase4TrialOrder::kBaselineFirst);
  if (std::holds_alternative<Phase4TrialHarnessError>(initial_spec)) {
    return std::get<Phase4TrialHarnessError>(std::move(initial_spec));
  }
  if (worker_executable.empty() || imported_fixture_path.empty()) {
    return Phase4TrialHarnessError{
        .invariant_id = "P4HARNESS-CELL-001",
        .detail = "cell, worker executable, or imported fixture path is invalid",
    };
  }
  std::string sigchld_detail;
  if (!ValidSigchldAuthority(&sigchld_detail)) {
    return Phase4TrialHarnessError{
        .invariant_id = "P4HARNESS-SIGCHLD-POLICY-001",
        .detail = std::move(sigchld_detail),
    };
  }
  int controller_pid_descriptor = OpenPidDescriptor(getpid());
  if (controller_pid_descriptor < 0) {
    return Phase4TrialHarnessError{
        .invariant_id = "P4HARNESS-PIDFD-AUTHORITY-001",
        .detail = "Linux pidfd authority is unavailable with errno " + Decimal(errno),
    };
  }
  CloseDescriptor(&controller_pid_descriptor);

  Phase4IsolatedCellResult result;
  result.config = cell;
  result.environment = CaptureHostEnvironment();
  result.corpus_checksum = Phase4RepresentativeCorpusChecksumV1();
  result.cell_plan_checksum = [&cell] {
    board_ir::StableHashBuilder hash;
    hash.AddString("APGAR-PHASE4-CANONICAL-CELL-PLAN-V1");
    hash.AddU32(cell.schema_version);
    hash.AddU64(Phase4RepresentativeCorpusChecksumV1());
    hash.AddU32(cell.case_id);
    hash.AddU32(cell.requested_pool_size);
    hash.AddU32(cell.preparation_worker_count);
    hash.AddU32(cell.repetitions);
    hash.AddU64(cell.maximum_setup_elapsed_nanoseconds);
    hash.AddU64(cell.external_budget.maximum_prepared_elapsed_nanoseconds);
    hash.AddU64(cell.external_budget.maximum_cold_elapsed_nanoseconds);
    hash.AddU64(cell.external_budget.maximum_address_space_bytes);
    hash.AddU64(cell.external_budget.maximum_peak_host_bytes);
    hash.AddU64(cell.corpus_limits.maximum_nets);
    hash.AddU64(cell.corpus_limits.maximum_compiled_nodes);
    hash.AddU64(cell.corpus_limits.maximum_compiled_host_bytes);
    hash.AddU64(cell.corpus_limits.maximum_active_regions);
    hash.AddU64(cell.corpus_limits.maximum_board_entities);
    return hash.Finish();
  }();
  result.controller_identity = ControllerIdentity();
  result.authority_run_identity = RunIdentity(cell, result.controller_identity);
  InitializeAttempts(&result);
  std::vector<std::optional<Phase4TrialArmExecution>> baseline_executions(cell.repetitions);
  std::vector<std::optional<Phase4TrialArmExecution>> candidate_executions(cell.repetitions);

  std::optional<WorkerProcess> baseline;
  std::optional<WorkerProcess> candidate;
  bool fatal = false;
  std::uint64_t dispatch_ordinal = 0;
  auto launch_and_ready = [&](Phase4TrialArm arm,
                              std::optional<WorkerProcess>* destination) -> bool {
    Phase4IsolatedArmAttempt* attempt = ArmAttempt(&result.attempts.front(), arm);
    const Clock::time_point start = Clock::now();
    const Clock::time_point deadline =
        start + std::chrono::nanoseconds(cell.maximum_setup_elapsed_nanoseconds);
    LaunchResult launched = LaunchWorker(worker_executable, arm, cell, imported_fixture_path,
                                         result.authority_run_identity, deadline);
    if (Clock::now() >= deadline) {
      attempt->disposition = Phase4IsolatedAttemptDisposition::kSetupTimeout;
      attempt->controller_invariant_id = "P4HARNESS-SETUP-DEADLINE-001";
      attempt->controller_detail = "worker launch exceeded the absolute setup deadline";
      attempt->outer_elapsed_nanoseconds = ElapsedNanoseconds(start, Clock::now());
      if (std::holds_alternative<WorkerProcess>(launched)) {
        destination->emplace(std::get<WorkerProcess>(std::move(launched)));
        attempt->process_instance_identity = (*destination)->process_instance_identity;
        attempt->watchdog_kill_sent = KillWorker(&**destination);
      }
      return false;
    }
    if (std::holds_alternative<LaunchError>(launched)) {
      const LaunchError& error = std::get<LaunchError>(launched);
      attempt->disposition = Phase4IsolatedAttemptDisposition::kLaunchFailure;
      attempt->controller_invariant_id = error.invariant_id;
      attempt->controller_detail = error.detail;
      return false;
    }
    destination->emplace(std::get<WorkerProcess>(std::move(launched)));
    AwaitResult ready = AwaitMessage(&**destination, start, deadline);
    if (ready.kind == AwaitKind::kMessage && ready.message.has_value() &&
        std::holds_alternative<internal::Phase4TrialWireReady>(*ready.message)) {
      const internal::Phase4TrialWireReady identity =
          std::get<internal::Phase4TrialWireReady>(*ready.message);
      if (!ReadyMatchesCell(identity, cell)) {
        attempt->disposition = Phase4IsolatedAttemptDisposition::kProtocolFailure;
        attempt->process_instance_identity = (*destination)->process_instance_identity;
        attempt->outer_elapsed_nanoseconds = ready.elapsed_nanoseconds;
        attempt->controller_invariant_id = "P4HARNESS-READY-ASSOCIATION-001";
        attempt->controller_detail = "worker ready identity names another canonical cell";
        return false;
      }
      (*destination)->ready_identity = identity;
      return true;
    }
    if (ready.kind == AwaitKind::kMessage && ready.message.has_value() &&
        std::holds_alternative<internal::Phase4TrialWireFailure>(*ready.message)) {
      const Phase4DurableArmFailure& failure =
          std::get<internal::Phase4TrialWireFailure>(*ready.message).failure;
      const internal::Phase4TrialWireReady* peer_ready = nullptr;
      if (arm == Phase4TrialArm::kReusableCandidateAllocation && baseline.has_value() &&
          baseline->ready_identity.has_value()) {
        peer_ready = &*baseline->ready_identity;
      } else if (arm == Phase4TrialArm::kSequentialBaseline && candidate.has_value() &&
                 candidate->ready_identity.has_value()) {
        peer_ready = &*candidate->ready_identity;
      }
      const bool associated = failure.arm == arm && FailureMatchesCell(failure, cell, peer_ready);
      attempt->disposition = associated ? Phase4IsolatedAttemptDisposition::kTypedChildFailure
                                        : Phase4IsolatedAttemptDisposition::kProtocolFailure;
      attempt->process_instance_identity = (*destination)->process_instance_identity;
      attempt->outer_elapsed_nanoseconds = ready.elapsed_nanoseconds;
      if (associated) {
        attempt->child_failure = failure;
      } else {
        attempt->controller_invariant_id = "P4HARNESS-FAILURE-ASSOCIATION-001";
        attempt->controller_detail =
            "worker failure does not match its contender arm or active case identity";
      }
      return false;
    }
    const Phase4IsolatedAttemptDisposition disposition =
        DispositionForAwaitFailure(ready, **destination, true);
    SetControllerFailure(ArmAttempt(&result.attempts.front(), arm), disposition, ready,
                         &**destination, 0);
    if (ready.kind == AwaitKind::kTimeout) {
      ArmAttempt(&result.attempts.front(), arm)->watchdog_kill_sent = KillWorker(&**destination);
    }
    return false;
  };

  if (!launch_and_ready(Phase4TrialArm::kSequentialBaseline, &baseline) ||
      !launch_and_ready(Phase4TrialArm::kReusableCandidateAllocation, &candidate)) {
    fatal = true;
  }
  if (!fatal && baseline->ready_identity != candidate->ready_identity) {
    for (auto* worker : {&*baseline, &*candidate}) {
      Phase4IsolatedArmAttempt* attempt = ArmAttempt(&result.attempts.front(), worker->arm);
      attempt->disposition = Phase4IsolatedAttemptDisposition::kProtocolFailure;
      attempt->process_instance_identity = worker->process_instance_identity;
      attempt->controller_invariant_id = "P4HARNESS-READY-PAIR-001";
      attempt->controller_detail = "contender warmups produced different full case identities";
    }
    fatal = true;
  }

  for (std::uint32_t repetition = 0; repetition < cell.repetitions && !fatal; ++repetition) {
    Phase4IsolatedPairAttempt& pair = result.attempts[repetition];
    const std::array<Phase4TrialArm, 2> order =
        pair.execution_order == Phase4TrialOrder::kBaselineFirst
            ? std::array{Phase4TrialArm::kSequentialBaseline,
                         Phase4TrialArm::kReusableCandidateAllocation}
            : std::array{Phase4TrialArm::kReusableCandidateAllocation,
                         Phase4TrialArm::kSequentialBaseline};
    for (Phase4TrialArm arm : order) {
      WorkerProcess* worker = arm == Phase4TrialArm::kSequentialBaseline ? &*baseline : &*candidate;
      auto* worker_executions =
          arm == Phase4TrialArm::kSequentialBaseline ? &baseline_executions : &candidate_executions;
      Phase4IsolatedArmAttempt* attempt = ArmAttempt(&pair, arm);
      attempt->dispatch_ordinal = ++dispatch_ordinal;
      attempt->process_instance_identity = worker->process_instance_identity;
      const Clock::time_point start = Clock::now();
      const internal::Phase4TrialWireRunCommand command{
          .repetition_index = repetition,
          .execution_order = pair.execution_order,
      };
      if (!SendWorkerMessage(worker->request_descriptor, command)) {
        AwaitResult failure;
        failure.kind = AwaitKind::kProtocol;
        failure.invariant_id = "P4HARNESS-WRITE-001";
        failure.detail = "failed to dispatch the run command";
        failure.elapsed_nanoseconds = ElapsedNanoseconds(start, Clock::now());
        SetControllerFailure(attempt, Phase4IsolatedAttemptDisposition::kProtocolFailure, failure,
                             worker, attempt->dispatch_ordinal);
        InvalidateWorkerSuccesses(
            &result, *worker, worker_executions, Phase4IsolatedAttemptDisposition::kProtocolFailure,
            failure.invariant_id,
            "a later worker protocol failure invalidated this process-lifetime observation", false);
        fatal = true;
        break;
      }
      AwaitResult response = AwaitMessage(
          worker, start,
          start + std::chrono::nanoseconds(cell.external_budget.maximum_cold_elapsed_nanoseconds));
      attempt->outer_elapsed_nanoseconds = response.elapsed_nanoseconds;
      if (response.kind == AwaitKind::kMessage && response.message.has_value() &&
          std::holds_alternative<internal::Phase4TrialWireSuccess>(*response.message)) {
        Phase4TrialArmExecution execution =
            std::get<internal::Phase4TrialWireSuccess>(std::move(*response.message)).execution;
        if (!worker->ready_identity.has_value() ||
            !MatchesReadyIdentity(*worker->ready_identity, execution.semantics) ||
            execution.semantics.arm != arm || execution.semantics.case_id != cell.case_id ||
            execution.semantics.requested_pool_size != cell.requested_pool_size ||
            execution.semantics.repetition_index != repetition ||
            execution.semantics.execution_order != pair.execution_order) {
          attempt->disposition = Phase4IsolatedAttemptDisposition::kProtocolFailure;
          attempt->controller_invariant_id = "P4HARNESS-ASSOCIATION-001";
          attempt->controller_detail = "worker success is associated with another command";
          InvalidateWorkerSuccesses(
              &result, *worker, worker_executions,
              Phase4IsolatedAttemptDisposition::kProtocolFailure, attempt->controller_invariant_id,
              "a later worker association failure invalidated this process-lifetime observation",
              false);
          fatal = true;
          break;
        }
        attempt->disposition = Phase4IsolatedAttemptDisposition::kSuccess;
        if (arm == Phase4TrialArm::kSequentialBaseline) {
          baseline_executions[repetition] = std::move(execution);
        } else {
          candidate_executions[repetition] = std::move(execution);
        }
        continue;
      }
      if (response.kind == AwaitKind::kMessage && response.message.has_value() &&
          std::holds_alternative<internal::Phase4TrialWireFailure>(*response.message)) {
        const Phase4DurableArmFailure& failure =
            std::get<internal::Phase4TrialWireFailure>(*response.message).failure;
        if (failure.arm == arm && worker->ready_identity.has_value() &&
            MatchesReadyIdentity(*worker->ready_identity, failure)) {
          attempt->disposition = Phase4IsolatedAttemptDisposition::kTypedChildFailure;
          attempt->child_failure = failure;
        } else {
          attempt->disposition = Phase4IsolatedAttemptDisposition::kProtocolFailure;
          attempt->controller_invariant_id = "P4HARNESS-FAILURE-ASSOCIATION-001";
          attempt->controller_detail =
              "worker failure names another contender arm or warmup case identity";
          InvalidateWorkerSuccesses(
              &result, *worker, worker_executions,
              Phase4IsolatedAttemptDisposition::kProtocolFailure, attempt->controller_invariant_id,
              "a later worker association failure invalidated this process-lifetime observation",
              false);
        }
      } else {
        const Phase4IsolatedAttemptDisposition disposition =
            DispositionForAwaitFailure(response, *worker, false);
        SetControllerFailure(attempt, disposition, response, worker, attempt->dispatch_ordinal);
        if (response.kind == AwaitKind::kTimeout) {
          attempt->watchdog_kill_sent = KillWorker(worker);
          InvalidateWorkerSuccesses(
              &result, *worker, worker_executions, Phase4IsolatedAttemptDisposition::kWallTimeout,
              "P4HARNESS-WATCHDOG-LIFETIME-001",
              "a later worker watchdog timeout invalidated this process-lifetime observation",
              attempt->watchdog_kill_sent);
        } else if (disposition == Phase4IsolatedAttemptDisposition::kProtocolFailure) {
          InvalidateWorkerSuccesses(
              &result, *worker, worker_executions,
              Phase4IsolatedAttemptDisposition::kProtocolFailure, response.invariant_id,
              "a later worker protocol failure invalidated this process-lifetime observation",
              false);
        }
      }
      fatal = true;
      break;
    }
  }

  if (fatal) {
    if (baseline.has_value()) {
      (void)KillWorker(&*baseline);
    }
    if (candidate.has_value()) {
      (void)KillWorker(&*candidate);
    }
  } else {
    auto stop_worker = [&](WorkerProcess* worker,
                           std::vector<std::optional<Phase4TrialArmExecution>>* executions) {
      const Clock::time_point start = Clock::now();
      if (!SendWorkerMessage(worker->request_descriptor, internal::Phase4TrialWireStop{})) {
        InvalidateWorkerSuccesses(
            &result, *worker, executions, Phase4IsolatedAttemptDisposition::kProtocolFailure,
            "P4HARNESS-STOP-WRITE-001", "failed to send the authenticated stop command", false);
        (void)KillWorker(worker);
        return;
      }
      AwaitResult stopped = AwaitMessage(
          worker, start, start + std::chrono::nanoseconds(cell.maximum_setup_elapsed_nanoseconds));
      if (stopped.kind == AwaitKind::kMessage && stopped.message.has_value() &&
          std::holds_alternative<internal::Phase4TrialWireStopped>(*stopped.message)) {
        worker->stop_acknowledged = true;
        return;
      }
      const bool timed_out = stopped.kind == AwaitKind::kTimeout;
      const bool kill_signal_sent = KillWorker(worker);
      InvalidateWorkerSuccesses(
          &result, *worker, executions,
          timed_out ? Phase4IsolatedAttemptDisposition::kTeardownTimeout
                    : DispositionForAwaitFailure(stopped, *worker, false),
          timed_out ? "P4HARNESS-STOP-TIMEOUT-001" : "P4HARNESS-STOP-ACK-001",
          timed_out ? "worker did not acknowledge stop before the teardown deadline"
                    : "worker did not produce the authenticated stop acknowledgement",
          timed_out && kill_signal_sent);
    };
    stop_worker(&*baseline, &baseline_executions);
    stop_worker(&*candidate, &candidate_executions);
  }
  if (baseline.has_value()) {
    const ReapResult reaped =
        ReapWorker(&*baseline,
                   Clock::now() + std::chrono::nanoseconds(cell.maximum_setup_elapsed_nanoseconds));
    if (baseline->response_trailing_observed || baseline->response_read_failed) {
      InvalidateWorkerSuccesses(
          &result, *baseline, &baseline_executions,
          Phase4IsolatedAttemptDisposition::kProtocolFailure, "P4HARNESS-STOP-TRAILING-001",
          "worker emitted bytes after its authenticated stop acknowledgement", false);
    } else if (reaped.teardown_deadline_expired) {
      InvalidateWorkerSuccesses(&result, *baseline, &baseline_executions,
                                Phase4IsolatedAttemptDisposition::kTeardownTimeout,
                                "P4HARNESS-REAP-TIMEOUT-001",
                                "worker did not exit and close its response channel before the "
                                "teardown deadline",
                                reaped.kill_signal_sent);
    }
    if (reaped.post_kill_reap_incomplete) {
      InvalidateWorkerSuccesses(
          &result, *baseline, &baseline_executions,
          Phase4IsolatedAttemptDisposition::kProtocolFailure, "P4HARNESS-REAP-BOUNDED-001",
          "exact wait4 authority remained unavailable after the bounded post-kill grace", false);
    }
    ApplyProcessExit(&result, &*baseline, &baseline_executions);
  }
  if (candidate.has_value()) {
    const ReapResult reaped =
        ReapWorker(&*candidate,
                   Clock::now() + std::chrono::nanoseconds(cell.maximum_setup_elapsed_nanoseconds));
    if (candidate->response_trailing_observed || candidate->response_read_failed) {
      InvalidateWorkerSuccesses(
          &result, *candidate, &candidate_executions,
          Phase4IsolatedAttemptDisposition::kProtocolFailure, "P4HARNESS-STOP-TRAILING-001",
          "worker emitted bytes after its authenticated stop acknowledgement", false);
    } else if (reaped.teardown_deadline_expired) {
      InvalidateWorkerSuccesses(&result, *candidate, &candidate_executions,
                                Phase4IsolatedAttemptDisposition::kTeardownTimeout,
                                "P4HARNESS-REAP-TIMEOUT-001",
                                "worker did not exit and close its response channel before the "
                                "teardown deadline",
                                reaped.kill_signal_sent);
    }
    if (reaped.post_kill_reap_incomplete) {
      InvalidateWorkerSuccesses(
          &result, *candidate, &candidate_executions,
          Phase4IsolatedAttemptDisposition::kProtocolFailure, "P4HARNESS-REAP-BOUNDED-001",
          "exact wait4 authority remained unavailable after the bounded post-kill grace", false);
    }
    ApplyProcessExit(&result, &*candidate, &candidate_executions);
  }

  for (std::uint32_t repetition = 0; repetition < cell.repetitions; ++repetition) {
    Phase4IsolatedPairAttempt& pair = result.attempts[repetition];
    auto finalize = [&](Phase4IsolatedArmAttempt* attempt,
                        std::optional<Phase4TrialArmExecution>* execution) {
      if (attempt->disposition != Phase4IsolatedAttemptDisposition::kSuccess ||
          !execution->has_value()) {
        return;
      }
      const Phase4ExternalResourceObservation observation =
          MakeObservation(result, *attempt, **execution);
      auto finalized = FinalizePhase4TrialArmV1(std::move(**execution), observation);
      execution->reset();
      if (std::holds_alternative<Phase4TrialArmRecord>(finalized)) {
        attempt->record = std::get<Phase4TrialArmRecord>(std::move(finalized));
        return;
      }
      const Phase4PairedTrialError& error = std::get<Phase4PairedTrialError>(finalized);
      attempt->disposition = error.code == Phase4PairedTrialErrorCode::kExternalBudgetExceeded
                                 ? Phase4IsolatedAttemptDisposition::kExternalBudgetExceeded
                                 : Phase4IsolatedAttemptDisposition::kProtocolFailure;
      attempt->controller_invariant_id = std::string(error.invariant_id);
      attempt->controller_detail = std::string(error.detail);
    };
    finalize(&pair.baseline, &baseline_executions[repetition]);
    finalize(&pair.candidate, &candidate_executions[repetition]);
    if (pair.baseline.record.has_value() && pair.candidate.record.has_value()) {
      auto assembled = AssemblePhase4PairedTrialV1(std::move(*pair.baseline.record),
                                                   std::move(*pair.candidate.record));
      if (std::holds_alternative<Phase4PairedTrialResult>(assembled)) {
        pair.result = std::get<Phase4PairedTrialResult>(std::move(assembled));
        pair.baseline.record = pair.result->baseline;
        pair.candidate.record = pair.result->candidate;
      } else {
        const Phase4PairedTrialError& error = std::get<Phase4PairedTrialError>(assembled);
        pair.baseline.disposition = Phase4IsolatedAttemptDisposition::kProtocolFailure;
        pair.candidate.disposition = Phase4IsolatedAttemptDisposition::kProtocolFailure;
        pair.baseline.controller_invariant_id = std::string(error.invariant_id);
        pair.baseline.controller_detail = std::string(error.detail);
        pair.candidate.controller_invariant_id = std::string(error.invariant_id);
        pair.candidate.controller_detail = std::string(error.detail);
        pair.baseline.record.reset();
        pair.candidate.record.reset();
      }
    }
  }
  FinalizeChecksums(&result);
  return result;
}

int RunPhase4TrialWorkerV1(Phase4TrialArm arm, const Phase4CanonicalCellConfig& cell,
                           std::string_view imported_fixture, int request_descriptor,
                           int response_descriptor) noexcept {
  try {
#if defined(APGAR_PHASE4_TRIAL_FAULT_TEST_VARIANT)
    const char* const fault_mode = std::getenv("APGAR_PHASE4_TRIAL_FAULT_MODE");
    const std::string_view fault = fault_mode == nullptr ? std::string_view{} : fault_mode;
    if (fault == "inherited_fd_sentinel") {
      const char* const descriptor_text = std::getenv("APGAR_PHASE4_TRIAL_SENTINEL_FD");
      int descriptor = -1;
      if (descriptor_text == nullptr) {
        return 12;
      }
      const std::string_view encoded_descriptor(descriptor_text);
      const auto [end, error] =
          std::from_chars(encoded_descriptor.data(),
                          encoded_descriptor.data() + encoded_descriptor.size(), descriptor);
      if (error != std::errc{} || end != encoded_descriptor.data() + encoded_descriptor.size() ||
          descriptor <= kWorkerResponseDescriptor) {
        return 12;
      }
      errno = 0;
      if (fcntl(descriptor, F_GETFD) >= 0 || errno != EBADF) {
        return 12;
      }
    }
    if (fault == "setup_hang") {
      for (;;) {
        (void)pause();
      }
    }
    if (fault == "continuous_output") {
      constexpr std::array<char, 4096> output = [] {
        std::array<char, 4096> value{};
        value.fill('x');
        return value;
      }();
      for (;;) {
        if (write(STDOUT_FILENO, output.data(), output.size()) <= 0) {
          (void)pause();
        }
      }
    }
#endif
    rlimit address_space_limit{};
    if (getrlimit(RLIMIT_AS, &address_space_limit) != 0 ||
        static_cast<std::uint64_t>(address_space_limit.rlim_cur) !=
            cell.external_budget.maximum_address_space_bytes) {
      const auto failure = SummaryFailure(
          arm, Phase4PairedTrialErrorCode::kExternalAuthorityUnavailable,
          "P4HARNESS-WORKER-RLIMIT-001", "worker did not observe the exact RLIMIT_AS contract",
          cell.external_budget.maximum_address_space_bytes,
          static_cast<std::uint64_t>(address_space_limit.rlim_cur));
      (void)SendWorkerMessage(response_descriptor, internal::Phase4TrialWireFailure{failure});
      return 10;
    }
    std::unique_ptr<allocator::PersistentCpuCandidatePoolPreparer> preparer;
    if (arm == Phase4TrialArm::kReusableCandidateAllocation) {
      const allocator::PersistentCpuCandidatePoolPreparerConfig preparer_config{
          .worker_count = cell.preparation_worker_count,
      };
      allocator::PersistentCpuCandidatePoolPreparerResult created = [&] {
#if defined(APGAR_PHASE4_TRIAL_FAULT_TEST_VARIANT)
        if (fault == "preparer_factory_resource_failure") {
          return allocator::PersistentCpuCandidatePoolPreparerResult{
              allocator::CpuCandidatePoolPreparationError{
                  .code = allocator::CpuCandidatePoolPreparationErrorCode::kResourceExhausted,
                  .invariant_id = "allocator.cpu_candidate_pool.preparer_thread.v1",
                  .detail = "injected persistent preparer resource exhaustion",
                  .net = std::nullopt,
                  .required = 0,
                  .configured = 0,
                  .failed_preparation = std::nullopt,
              }};
        }
#endif
        return allocator::CreatePersistentCpuCandidatePoolPreparer(preparer_config);
      }();
      if (std::holds_alternative<allocator::CpuCandidatePoolPreparationError>(created)) {
        const auto& error = std::get<allocator::CpuCandidatePoolPreparationError>(created);
        const Phase4PairedTrialErrorCode summary_code =
            error.code == allocator::CpuCandidatePoolPreparationErrorCode::kResourceExhausted
                ? Phase4PairedTrialErrorCode::kResourceExhausted
                : Phase4PairedTrialErrorCode::kInvalidConfiguration;
        const auto failure = SummaryFailure(arm, summary_code, error.invariant_id, error.detail,
                                            error.required, error.configured);
        (void)SendWorkerMessage(response_descriptor, internal::Phase4TrialWireFailure{failure});
        return 10;
      }
      preparer = std::get<std::unique_ptr<allocator::PersistentCpuCandidatePoolPreparer>>(
          std::move(created));
    }
    auto warmup_spec = BuildPhase4CanonicalTrialSpecV1(cell, 0, Phase4TrialOrder::kBaselineFirst);
    if (std::holds_alternative<Phase4TrialHarnessError>(warmup_spec)) {
      const auto& error = std::get<Phase4TrialHarnessError>(warmup_spec);
      const auto failure = SummaryFailure(arm, Phase4PairedTrialErrorCode::kInvalidConfiguration,
                                          error.invariant_id, error.detail);
      (void)SendWorkerMessage(response_descriptor, internal::Phase4TrialWireFailure{failure});
      return 10;
    }
    auto warmup = ExecutePhase4TrialArmV1(arm, std::get<Phase4PairedTrialSpec>(warmup_spec),
                                          imported_fixture, preparer.get());
    if (std::holds_alternative<Phase4TrialArmFailure>(warmup)) {
      const auto failure =
          ReconcilePhase4TrialArmFailureV1(std::get<Phase4TrialArmFailure>(std::move(warmup)));
      (void)SendWorkerMessage(response_descriptor, internal::Phase4TrialWireFailure{failure});
      return 10;
    }
    internal::Phase4TrialWireReady ready =
        ReadyIdentity(std::get<Phase4TrialArmExecution>(warmup).semantics);
#if defined(APGAR_PHASE4_TRIAL_FAULT_TEST_VARIANT)
    if (fault == "warmup_failure_bad_identity" &&
        arm == Phase4TrialArm::kReusableCandidateAllocation) {
      Phase4DurableArmFailure failure;
      failure.summary_code = Phase4PairedTrialErrorCode::kCandidatePreparation;
      failure.arm = arm;
      failure.summary_invariant_id = "injected.warmup.failure.identity";
      failure.summary_detail = "injected warm-up failure with a mismatched peer identity";
      failure.payload_kind = Phase4DurableFailurePayloadKind::kCandidatePreparation;
      failure.child_error_code = static_cast<std::uint8_t>(
          allocator::CpuCandidatePoolPreparationErrorCode::kResourceExhausted);
      failure.child_invariant_id = "injected.child.warmup.failure.identity";
      failure.child_detail = "injected pre-Ready typed failure identity mismatch";
      failure.has_case_identity = true;
      failure.case_id = ready.case_id;
      failure.descriptor_fingerprint = ready.descriptor_fingerprint;
      failure.case_checksum = ready.case_checksum + 1;
      failure.board_content_hash = ready.board_content_hash;
      failure.workload_checksum = ready.workload_checksum;
      failure.capacity_model_checksum = ready.capacity_model_checksum;
      failure.payload_checksum = ComputePhase4DurableArmFailureChecksumV1(failure);
      (void)SendWorkerMessage(response_descriptor,
                              internal::Phase4TrialWireFailure{std::move(failure)});
      return 10;
    }
    if (fault == "ready_bad_identity" && arm == Phase4TrialArm::kReusableCandidateAllocation) {
      ++ready.case_checksum;
    }
    if (fault == "double_ready") {
      auto encoded = internal::EncodePhase4TrialWireMessageV1(ready);
      if (std::holds_alternative<std::vector<std::uint8_t>>(encoded)) {
        std::vector<std::uint8_t> doubled = std::get<std::vector<std::uint8_t>>(std::move(encoded));
        const std::vector<std::uint8_t> second = doubled;
        doubled.insert(doubled.end(), second.begin(), second.end());
        (void)send(response_descriptor, doubled.data(), doubled.size(), MSG_NOSIGNAL);
      }
      for (;;) {
        (void)pause();
      }
    }
#endif
    if (!SendWorkerMessage(response_descriptor, ready)) {
      return 11;
    }
    for (;;) {
      auto request = internal::ReadPhase4TrialWireMessageV1(request_descriptor);
      if (std::holds_alternative<internal::Phase4TrialWireError>(request)) {
        return 11;
      }
      Phase4TrialWireMessage message = std::get<Phase4TrialWireMessage>(std::move(request));
      if (std::holds_alternative<internal::Phase4TrialWireStop>(message)) {
        const bool acknowledged =
            SendWorkerMessage(response_descriptor, internal::Phase4TrialWireStopped{});
#if defined(APGAR_PHASE4_TRIAL_FAULT_TEST_VARIANT)
        if (acknowledged && fault == "teardown_hang") {
          for (;;) {
            (void)pause();
          }
        }
        if (acknowledged && fault == "delayed_extra_frame") {
          (void)poll(nullptr, 0, 500);
          (void)SendWorkerMessage(response_descriptor, ready);
        }
#endif
        return acknowledged ? 0 : 11;
      }
      if (!std::holds_alternative<internal::Phase4TrialWireRunCommand>(message)) {
        return 11;
      }
      const auto command = std::get<internal::Phase4TrialWireRunCommand>(message);
      auto spec =
          BuildPhase4CanonicalTrialSpecV1(cell, command.repetition_index, command.execution_order);
      if (std::holds_alternative<Phase4TrialHarnessError>(spec)) {
        const auto& error = std::get<Phase4TrialHarnessError>(spec);
        const auto failure = SummaryFailure(arm, Phase4PairedTrialErrorCode::kInvalidConfiguration,
                                            error.invariant_id, error.detail);
        (void)SendWorkerMessage(response_descriptor, internal::Phase4TrialWireFailure{failure});
        return 10;
      }
      auto execution = ExecutePhase4TrialArmV1(arm, std::get<Phase4PairedTrialSpec>(spec),
                                               imported_fixture, preparer.get());
      if (std::holds_alternative<Phase4TrialArmFailure>(execution)) {
        const auto failure =
            ReconcilePhase4TrialArmFailureV1(std::get<Phase4TrialArmFailure>(std::move(execution)));
        (void)SendWorkerMessage(response_descriptor, internal::Phase4TrialWireFailure{failure});
        return 10;
      }
      Phase4TrialArmExecution successful_execution =
          std::get<Phase4TrialArmExecution>(std::move(execution));
#if defined(APGAR_PHASE4_TRIAL_FAULT_TEST_VARIANT)
      const bool bad_second_association =
          fault == "second_run_bad_association" && command.repetition_index == 1;
      if (bad_second_association) {
        successful_execution.semantics.repetition_index = 0;
      }
      if (fault == "failure_bad_ready_identity" &&
          arm == Phase4TrialArm::kReusableCandidateAllocation) {
        Phase4DurableArmFailure failure;
        failure.summary_code = Phase4PairedTrialErrorCode::kCandidatePreparation;
        failure.arm = arm;
        failure.summary_invariant_id = "injected.failure.identity";
        failure.summary_detail = "injected failure with a mismatched warmup identity";
        failure.payload_kind = Phase4DurableFailurePayloadKind::kCandidatePreparation;
        failure.child_error_code = static_cast<std::uint8_t>(
            allocator::CpuCandidatePoolPreparationErrorCode::kResourceExhausted);
        failure.child_invariant_id = "injected.child.failure.identity";
        failure.child_detail = "injected typed failure identity mismatch";
        failure.has_case_identity = true;
        failure.case_id = ready.case_id;
        failure.descriptor_fingerprint = ready.descriptor_fingerprint;
        failure.case_checksum = ready.case_checksum + 1;
        failure.board_content_hash = ready.board_content_hash;
        failure.workload_checksum = ready.workload_checksum;
        failure.capacity_model_checksum = ready.capacity_model_checksum;
        failure.payload_checksum = ComputePhase4DurableArmFailureChecksumV1(failure);
        (void)SendWorkerMessage(response_descriptor,
                                internal::Phase4TrialWireFailure{std::move(failure)});
        return 10;
      }
#endif
      if (!SendWorkerMessage(response_descriptor,
                             internal::Phase4TrialWireSuccess{std::move(successful_execution)})) {
        return 11;
      }
#if defined(APGAR_PHASE4_TRIAL_FAULT_TEST_VARIANT)
      if (bad_second_association) {
        _exit(0);
      }
      if (fault == "unexpected_output") {
        constexpr std::string_view output = "unexpected worker output\n";
        (void)write(STDOUT_FILENO, output.data(), output.size());
      }
      if (fault == "exit_before_stop" && command.repetition_index + 1 == cell.repetitions) {
        return 0;
      }
#endif
    }
  } catch (...) {
    const auto failure = SummaryFailure(
        arm, Phase4PairedTrialErrorCode::kInternalInvariant, "P4HARNESS-WORKER-EXCEPTION-001",
        "unexpected exception escaped the isolated worker controller");
    (void)SendWorkerMessage(response_descriptor, internal::Phase4TrialWireFailure{failure});
    return 12;
  }
}

}  // namespace apgar::benchmark
