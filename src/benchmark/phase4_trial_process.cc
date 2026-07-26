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
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "apgar/benchmark/phase4_trial_harness.h"
#include "apgar/board_ir/stable_hash.h"
#include "src/allocator/negotiated_prices_internal.h"
#include "src/benchmark/phase4_confirmatory_h4096_execution_internal.h"
#include "src/benchmark/phase4_h4096_canonical_budget_internal.h"
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

enum class WorkerWireMode : std::uint8_t {
  kRawV1,
  kSameRunTelemetryV2,
};

using Phase4TrialExecutionAuthority = internal::Phase4TrialExecutionAuthority;

[[nodiscard]] std::optional<Phase4RepresentativeCorpusAuthority> CorpusAuthority(
    Phase4TrialExecutionAuthority authority) noexcept {
  switch (authority) {
    case Phase4TrialExecutionAuthority::kCorpusV1:
      return Phase4RepresentativeCorpusAuthority::kV1;
    case Phase4TrialExecutionAuthority::kCorpusV2H2250:
    case Phase4TrialExecutionAuthority::kCorpusV2H4096:
      return Phase4RepresentativeCorpusAuthority::kV2;
  }
  return std::nullopt;
}

[[nodiscard]] Phase4CanonicalSpecResult BuildCanonicalSpecForAuthority(
    Phase4TrialExecutionAuthority authority, const Phase4CanonicalCellConfig& cell,
    std::uint32_t repetition, Phase4TrialOrder order) noexcept {
  switch (authority) {
    case Phase4TrialExecutionAuthority::kCorpusV1:
      return BuildPhase4CanonicalTrialSpecV1(cell, repetition, order);
    case Phase4TrialExecutionAuthority::kCorpusV2H2250:
      return BuildPhase4CanonicalTrialSpecForCorpusV2(cell, repetition, order);
    case Phase4TrialExecutionAuthority::kCorpusV2H4096:
      return internal::BuildPhase4CanonicalTrialSpecForCorpusV2H4096(cell, repetition, order);
  }
  return Phase4TrialHarnessError{
      .invariant_id = "P4HARNESS-EXECUTION-AUTHORITY-001",
      .detail = "the private trial execution authority is invalid",
      .raw_cell = std::nullopt,
  };
}

[[nodiscard]] std::optional<Phase4TrialHarnessError> PreflightConfirmatoryH4096Cell(
    Phase4TrialExecutionAuthority authority, WorkerWireMode wire_mode,
    const Phase4CanonicalCellConfig& cell, std::optional<Phase4TrialArm> worker_arm) {
  if (authority != Phase4TrialExecutionAuthority::kCorpusV2H4096) {
    return Phase4TrialHarnessError{
        .invariant_id = "P4HARNESS-H4096-AUTHORITY-001",
        .detail = "the fixed H=4096 preflight requires its private execution authority",
        .raw_cell = std::nullopt,
    };
  }
  const bool ordinary = wire_mode == WorkerWireMode::kRawV1;
  const std::uint32_t expected_case = ordinary ? 10'200U : 10'100U;
  const std::uint32_t expected_pool = ordinary ? 8U : 4U;
  if (cell.case_id != expected_case || cell.requested_pool_size != expected_pool) {
    return Phase4TrialHarnessError{
        .invariant_id =
            ordinary ? "P4HARNESS-H4096-ORDINARY-SCOPE-001" : "P4HARNESS-H4096-SAME-RUN-SCOPE-001",
        .detail = ordinary ? "H=4096 ordinary execution is restricted to calibration cell (10200,8)"
                           : "H=4096 same-run execution is restricted to exact cell (10100,4)",
        .raw_cell = std::nullopt,
    };
  }
  if (cell.repetitions != kPhase4CanonicalRepetitionsV1 ||
      cell.preparation_worker_count != kPhase4CanonicalPreparationWorkersV1) {
    return Phase4TrialHarnessError{
        .invariant_id = "P4HARNESS-H4096-CANONICAL-CARDINALITY-001",
        .detail = "H=4096 execution requires exactly twenty repetitions and four preparer workers",
        .raw_cell = std::nullopt,
    };
  }
  for (std::uint32_t repetition = 0; repetition < cell.repetitions; ++repetition) {
    const Phase4TrialOrder order = repetition % 2U == 0U ? Phase4TrialOrder::kBaselineFirst
                                                         : Phase4TrialOrder::kCandidateFirst;
    Phase4CanonicalSpecResult built =
        BuildCanonicalSpecForAuthority(authority, cell, repetition, order);
    if (std::holds_alternative<Phase4TrialHarnessError>(built)) {
      return std::get<Phase4TrialHarnessError>(std::move(built));
    }
    const Phase4PairedTrialSpec& spec = std::get<Phase4PairedTrialSpec>(built);
    const auto preflight_arm = [&](Phase4TrialArm arm) {
      return ordinary ? internal::PreflightPhase4ConfirmatoryH4096OrdinarySpec(spec, arm)
                      : internal::PreflightPhase4ConfirmatoryH4096SameRunSpec(spec, arm);
    };
    const auto convert = [](const Phase4PairedTrialError& error) {
      return Phase4TrialHarnessError{
          .invariant_id = std::string(error.invariant_id),
          .detail = std::string(error.detail),
          .raw_cell = std::nullopt,
      };
    };
    if (worker_arm.has_value()) {
      if (std::optional<Phase4PairedTrialError> error = preflight_arm(*worker_arm);
          error.has_value()) {
        return convert(*error);
      }
    } else {
      for (Phase4TrialArm arm :
           {Phase4TrialArm::kSequentialBaseline, Phase4TrialArm::kReusableCandidateAllocation}) {
        if (std::optional<Phase4PairedTrialError> error = preflight_arm(arm); error.has_value()) {
          return convert(*error);
        }
      }
    }
  }
  return std::nullopt;
}

[[nodiscard]] Phase4TrialArmExecutionResult ExecuteArmForAuthority(
    Phase4TrialExecutionAuthority authority, Phase4TrialArm arm, const Phase4PairedTrialSpec& spec,
    std::string_view imported_fixture, allocator::PersistentCpuCandidatePoolPreparer* preparer) {
  switch (authority) {
    case Phase4TrialExecutionAuthority::kCorpusV1:
      return ExecutePhase4TrialArmV1(arm, spec, imported_fixture, preparer);
    case Phase4TrialExecutionAuthority::kCorpusV2H2250:
      return ExecutePhase4TrialArmForCorpusV2(arm, spec, imported_fixture, preparer);
    case Phase4TrialExecutionAuthority::kCorpusV2H4096:
      return internal::ExecutePhase4ConfirmatoryH4096OrdinaryTrialArm(arm, spec, imported_fixture,
                                                                      preparer);
  }
  return Phase4TrialArmFailure{
      .summary =
          Phase4PairedTrialError{
              .code = Phase4PairedTrialErrorCode::kInvalidConfiguration,
              .invariant_id = "P4HARNESS-EXECUTION-AUTHORITY-001",
              .detail = "the private trial execution authority is invalid",
              .arm = arm,
          },
      .payload = std::monostate{},
  };
}

[[nodiscard]] Phase4TrialArmWithSameRunTelemetryExecutionResultV1 ExecuteSameRunArmForAuthority(
    Phase4TrialExecutionAuthority authority, Phase4TrialArm arm, const Phase4PairedTrialSpec& spec,
    std::string_view imported_fixture, allocator::PersistentCpuCandidatePoolPreparer* preparer) {
  switch (authority) {
    case Phase4TrialExecutionAuthority::kCorpusV1:
      return ExecutePhase4TrialArmWithSameRunTelemetryV1(arm, spec, imported_fixture, preparer);
    case Phase4TrialExecutionAuthority::kCorpusV2H2250:
      return ExecutePhase4TrialArmWithSameRunTelemetryForCorpusV2(arm, spec, imported_fixture,
                                                                  preparer);
    case Phase4TrialExecutionAuthority::kCorpusV2H4096:
      return internal::ExecutePhase4ConfirmatoryH4096SameRunTrialArm(arm, spec, imported_fixture,
                                                                     preparer);
  }
  return Phase4TrialArmFailure{
      .summary =
          Phase4PairedTrialError{
              .code = Phase4PairedTrialErrorCode::kInvalidConfiguration,
              .invariant_id = "P4HARNESS-EXECUTION-AUTHORITY-001",
              .detail = "the private trial execution authority is invalid",
              .arm = arm,
          },
      .payload = std::monostate{},
  };
}

[[nodiscard]] Phase4DurableArmFailure ReconcileFailureForAuthority(
    Phase4TrialExecutionAuthority authority, Phase4TrialArmFailure failure) {
  return authority == Phase4TrialExecutionAuthority::kCorpusV1
             ? ReconcilePhase4TrialArmFailureV1(std::move(failure))
             : ReconcilePhase4TrialArmFailureForCorpusV2(std::move(failure));
}

[[nodiscard]] Phase4TrialArmRecordResult FinalizeArmForAuthority(
    Phase4TrialExecutionAuthority authority, WorkerWireMode wire_mode,
    const Phase4PairedTrialSpec* expected_spec, Phase4TrialArmExecution execution,
    const Phase4ExternalResourceObservation& observation) {
  switch (authority) {
    case Phase4TrialExecutionAuthority::kCorpusV1:
      return FinalizePhase4TrialArmV1(std::move(execution), observation);
    case Phase4TrialExecutionAuthority::kCorpusV2H2250:
      return FinalizePhase4TrialArmForCorpusV2(std::move(execution), observation);
    case Phase4TrialExecutionAuthority::kCorpusV2H4096:
      if (expected_spec == nullptr) {
        return Phase4PairedTrialError{
            .code = Phase4PairedTrialErrorCode::kMeasurementAssociation,
            .invariant_id = "P4HARNESS-H4096-EXPECTED-SPEC-001",
            .detail = "H=4096 finalization requires the rebuilt expected canonical spec",
            .arm = execution.semantics.arm,
        };
      }
      return wire_mode == WorkerWireMode::kRawV1
                 ? internal::FinalizePhase4ConfirmatoryH4096OrdinaryTrialArm(
                       *expected_spec, std::move(execution), observation)
                 : internal::FinalizePhase4ConfirmatoryH4096SameRunTrialArm(
                       *expected_spec, std::move(execution), observation);
  }
  return Phase4PairedTrialError{
      .code = Phase4PairedTrialErrorCode::kInvalidConfiguration,
      .invariant_id = "P4HARNESS-EXECUTION-AUTHORITY-001",
      .detail = "the private trial execution authority is invalid",
      .arm = execution.semantics.arm,
  };
}

[[nodiscard]] Phase4PairedTrialAssemblyResult AssemblePairForAuthority(
    Phase4TrialExecutionAuthority authority, WorkerWireMode wire_mode,
    const Phase4PairedTrialSpec* expected_spec, Phase4TrialArmRecord baseline,
    Phase4TrialArmRecord candidate) {
  switch (authority) {
    case Phase4TrialExecutionAuthority::kCorpusV1:
      return AssemblePhase4PairedTrialV1(std::move(baseline), std::move(candidate));
    case Phase4TrialExecutionAuthority::kCorpusV2H2250:
      return AssemblePhase4PairedTrialForCorpusV2(std::move(baseline), std::move(candidate));
    case Phase4TrialExecutionAuthority::kCorpusV2H4096:
      if (expected_spec == nullptr) {
        return Phase4PairedTrialError{
            .code = Phase4PairedTrialErrorCode::kMeasurementAssociation,
            .invariant_id = "P4HARNESS-H4096-EXPECTED-SPEC-001",
            .detail = "H=4096 assembly requires the rebuilt expected canonical spec",
        };
      }
      return wire_mode == WorkerWireMode::kRawV1
                 ? internal::AssemblePhase4ConfirmatoryH4096OrdinaryPairedTrial(
                       *expected_spec, std::move(baseline), std::move(candidate))
                 : internal::AssemblePhase4ConfirmatoryH4096SameRunPairedTrial(
                       *expected_spec, std::move(baseline), std::move(candidate));
  }
  return Phase4PairedTrialError{
      .code = Phase4PairedTrialErrorCode::kInvalidConfiguration,
      .invariant_id = "P4HARNESS-EXECUTION-AUTHORITY-001",
      .detail = "the private trial execution authority is invalid",
  };
}

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

template <typename Message>
struct BasicAwaitResult {
  AwaitKind kind = AwaitKind::kProtocol;
  std::optional<Message> message;
  std::string invariant_id;
  std::string detail;
  std::uint64_t elapsed_nanoseconds = 0;
};

using AwaitResult = BasicAwaitResult<Phase4TrialWireMessage>;
using AwaitResultV2 = BasicAwaitResult<internal::Phase4TrialWireMessageV2>;

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

[[nodiscard]] std::vector<std::string> BuildWorkerArguments(std::string_view executable,
                                                            Phase4TrialExecutionAuthority authority,
                                                            Phase4TrialArm arm,
                                                            const Phase4CanonicalCellConfig& cell,
                                                            std::string_view imported_fixture_path,
                                                            WorkerWireMode wire_mode) {
  std::vector<std::string> arguments;
  arguments.reserve(21);
  arguments.emplace_back(executable);
  if (authority == Phase4TrialExecutionAuthority::kCorpusV2H4096) {
    arguments.emplace_back(wire_mode == WorkerWireMode::kRawV1
                               ? "--phase4_h4096_ordinary_worker=1"
                               : "--phase4_h4096_same_run_worker=1");
  } else {
    arguments.emplace_back(wire_mode == WorkerWireMode::kRawV1 ? "--phase4_worker=1"
                                                               : "--phase4_same_run_worker=1");
  }
  const std::optional<Phase4RepresentativeCorpusAuthority> corpus = CorpusAuthority(authority);
  if (!corpus.has_value()) {
    return {};
  }
  arguments.emplace_back(*corpus == Phase4RepresentativeCorpusAuthority::kV1
                             ? "--corpus_version=1"
                             : "--corpus_version=2");
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
                                        Phase4TrialExecutionAuthority authority,
                                        const Phase4CanonicalCellConfig& cell,
                                        std::string_view imported_fixture_path,
                                        std::uint64_t run_identity, Clock::time_point deadline,
                                        WorkerWireMode wire_mode = WorkerWireMode::kRawV1) {
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
      BuildWorkerArguments(executable, authority, arm, cell, imported_fixture_path, wire_mode);
  if (arguments.empty()) {
    return LaunchError{.invariant_id = "P4HARNESS-EXECUTION-AUTHORITY-001",
                       .detail = "the private trial execution authority is invalid"};
  }
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
#if defined(APGAR_PHASE4_TRIAL_FAULT_TEST_VARIANT)
    const char* const launch_fault_mode = std::getenv("APGAR_PHASE4_TRIAL_FAULT_MODE");
    if (launch_fault_mode != nullptr &&
        std::string_view(launch_fault_mode) == "exit_before_launch_gate") {
      _exit(12);
    }
#endif
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

#if defined(APGAR_PHASE4_TRIAL_FAULT_TEST_VARIANT)
  if (launch_fault_mode != nullptr &&
      std::string_view(launch_fault_mode) == "exit_before_launch_gate") {
    pollfd exited{.fd = pid_descriptor, .events = POLLIN, .revents = 0};
    while (poll(&exited, 1, 1000) < 0 && errno == EINTR) {
    }
  }
#endif
  ssize_t gate_written;
  do {
    gate_written = send(parent_request, &kLaunchGateToken, sizeof(kLaunchGateToken), MSG_NOSIGNAL);
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

template <typename Message, typename InspectFrame, typename DecodeFrame>
[[nodiscard]] BasicAwaitResult<Message> AwaitMessageImpl(
    WorkerProcess* worker, Clock::time_point start, Clock::time_point deadline,
    std::size_t maximum_frame_bytes, InspectFrame inspect_frame, DecodeFrame decode_frame) {
  using Result = BasicAwaitResult<Message>;
  std::vector<std::uint8_t> frame;
  frame.reserve(maximum_frame_bytes);
  std::optional<std::size_t> expected_size;
  const auto timed_out = [&] {
    return Result{.kind = AwaitKind::kTimeout,
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
      return Result{.kind = AwaitKind::kProtocol,
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
        return Result{.kind = AwaitKind::kLaunch,
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
        const std::size_t remaining = expected_size.has_value()
                                          ? *expected_size - frame.size()
                                          : maximum_frame_bytes - frame.size();
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
          const auto inspected = inspect_frame(frame);
          if (std::holds_alternative<internal::Phase4TrialWireError>(inspected)) {
            const auto& error = std::get<internal::Phase4TrialWireError>(inspected);
            return Result{.kind = AwaitKind::kProtocol,
                          .message = std::nullopt,
                          .invariant_id = error.invariant_id,
                          .detail = error.detail,
                          .elapsed_nanoseconds = ElapsedNanoseconds(start, Clock::now())};
          }
          expected_size = std::get<std::optional<std::size_t>>(inspected);
          if (expected_size.has_value() && frame.size() > *expected_size) {
            return Result{.kind = AwaitKind::kProtocol,
                          .message = std::nullopt,
                          .invariant_id = "P4HARNESS-FRAME-TRAILING-001",
                          .detail = "response channel coalesced bytes after the declared frame",
                          .elapsed_nanoseconds = ElapsedNanoseconds(start, Clock::now())};
          }
          if (expected_size.has_value() && frame.size() == *expected_size) {
            auto decoded = decode_frame(frame);
            if (std::holds_alternative<internal::Phase4TrialWireError>(decoded)) {
              const auto& error = std::get<internal::Phase4TrialWireError>(decoded);
              return Result{.kind = AwaitKind::kProtocol,
                            .message = std::nullopt,
                            .invariant_id = error.invariant_id,
                            .detail = error.detail,
                            .elapsed_nanoseconds = ElapsedNanoseconds(start, Clock::now())};
            }
            if (Clock::now() >= deadline) {
              return timed_out();
            }
            return Result{.kind = AwaitKind::kMessage,
                          .message = std::get<Message>(std::move(decoded)),
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
        return Result{.kind = AwaitKind::kProtocol,
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
      return Result{
          .kind = AwaitKind::kProcessExit,
          .message = std::nullopt,
          .invariant_id = "P4HARNESS-PIDFD-WAIT-001",
          .detail = "pidfd wait authority failed with errno " + Decimal(worker->wait_error_number),
          .elapsed_nanoseconds = ElapsedNanoseconds(start, Clock::now())};
    }
    if (reap_state == ReapState::kExited || reap_state == ReapState::kReaped) {
      return Result{.kind = AwaitKind::kProcessExit,
                    .message = std::nullopt,
                    .invariant_id = "P4HARNESS-EXIT-001",
                    .detail = "worker exited before producing a complete response frame",
                    .elapsed_nanoseconds = ElapsedNanoseconds(start, Clock::now())};
    }
  }
}

[[nodiscard]] AwaitResult AwaitMessage(WorkerProcess* worker, Clock::time_point start,
                                       Clock::time_point deadline,
                                       Phase4TrialExecutionAuthority authority) {
  const std::optional<Phase4RepresentativeCorpusAuthority> corpus = CorpusAuthority(authority);
  if (!corpus.has_value()) {
    return AwaitResult{.kind = AwaitKind::kProtocol,
                       .message = std::nullopt,
                       .invariant_id = "P4HARNESS-EXECUTION-AUTHORITY-001",
                       .detail = "the private trial execution authority is invalid",
                       .elapsed_nanoseconds = ElapsedNanoseconds(start, Clock::now())};
  }
  return AwaitMessageImpl<Phase4TrialWireMessage>(
      worker, start, deadline, internal::kPhase4TrialWireMaxFrameBytesV1,
      internal::Phase4TrialWireExpectedFrameSizeV1,
      *corpus == Phase4RepresentativeCorpusAuthority::kV1
          ? internal::DecodePhase4TrialWireMessageV1
          : internal::DecodePhase4TrialWireMessageForCorpusV2);
}

[[nodiscard]] AwaitResultV2 AwaitMessageV2(WorkerProcess* worker, Clock::time_point start,
                                           Clock::time_point deadline,
                                           Phase4TrialExecutionAuthority authority) {
  const std::optional<Phase4RepresentativeCorpusAuthority> corpus = CorpusAuthority(authority);
  if (!corpus.has_value()) {
    return AwaitResultV2{.kind = AwaitKind::kProtocol,
                         .message = std::nullopt,
                         .invariant_id = "P4HARNESS-EXECUTION-AUTHORITY-001",
                         .detail = "the private trial execution authority is invalid",
                         .elapsed_nanoseconds = ElapsedNanoseconds(start, Clock::now())};
  }
  return AwaitMessageImpl<internal::Phase4TrialWireMessageV2>(
      worker, start, deadline, internal::kPhase4TrialWireMaxFrameBytesV2,
      internal::Phase4TrialWireExpectedFrameSizeV2,
      *corpus == Phase4RepresentativeCorpusAuthority::kV1
          ? internal::DecodePhase4TrialWireMessageV2
          : internal::DecodePhase4TrialWireMessageV2ForCorpusV2);
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
#if defined(APGAR_PHASE4_TRIAL_FAULT_TEST_VARIANT)
  const char* const reap_fault_mode = std::getenv("APGAR_PHASE4_TRIAL_FAULT_MODE");
  if (reap_fault_mode != nullptr &&
      std::string_view(reap_fault_mode) == "preparer_factory_resource_failure_reap_unavailable") {
    worker->reaped = false;
    worker->wait_error_number = ECHILD;
    worker->wait_status = 0;
    worker->exit_observed = false;
    worker->usage = {};
    state = ReapState::kError;
  }
#endif
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

template <typename Message>
[[nodiscard]] Phase4IsolatedAttemptDisposition DispositionForAwaitFailure(
    const BasicAwaitResult<Message>& failure, const WorkerProcess& worker, bool setup) noexcept {
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

[[nodiscard]] std::uint64_t HostEnvironmentChecksum(
    const Phase4HostEnvironment& environment) noexcept {
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
  return hash.Finish();
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
  environment.environment_checksum = HostEnvironmentChecksum(environment);
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

[[nodiscard]] std::uint64_t CellPlanChecksum(Phase4TrialExecutionAuthority authority,
                                             const Phase4CanonicalCellConfig& cell) noexcept {
  const std::optional<Phase4RepresentativeCorpusAuthority> corpus = CorpusAuthority(authority);
  if (!corpus.has_value()) {
    return 0;
  }
  board_ir::StableHashBuilder hash;
  hash.AddString(*corpus == Phase4RepresentativeCorpusAuthority::kV1
                     ? "APGAR-PHASE4-CANONICAL-CELL-PLAN-V1"
                     : "APGAR-PHASE4-CANONICAL-CELL-PLAN-V2");
  hash.AddU32(cell.schema_version);
  hash.AddU64(Phase4RepresentativeCorpusChecksumForAuthority(*corpus));
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
}

void InitializeAttempts(Phase4TrialExecutionAuthority authority, Phase4IsolatedCellResult* result) {
  result->attempts.reserve(result->config.repetitions);
  for (std::uint32_t repetition = 0; repetition < result->config.repetitions; ++repetition) {
    const Phase4TrialOrder order =
        repetition % 2 == 0 ? Phase4TrialOrder::kBaselineFirst : Phase4TrialOrder::kCandidateFirst;
    auto spec_result = BuildCanonicalSpecForAuthority(authority, result->config, repetition, order);
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

template <typename Message>
void SetControllerFailure(Phase4IsolatedArmAttempt* attempt,
                          Phase4IsolatedAttemptDisposition disposition,
                          const BasicAwaitResult<Message>& failure, const WorkerProcess* worker,
                          std::uint64_t dispatch_ordinal) {
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

[[nodiscard]] std::uint64_t PairAttemptChecksum(const Phase4IsolatedPairAttempt& pair) noexcept {
  board_ir::StableHashBuilder hash;
  hash.AddString("APGAR-PHASE4-ISOLATED-PAIR-ATTEMPT-V1");
  hash.AddU32(pair.schema_version);
  hash.AddU32(pair.case_id);
  hash.AddU32(pair.requested_pool_size);
  hash.AddU32(pair.repetition_index);
  hash.AddU64(pair.root_seed);
  hash.AddByte(static_cast<std::uint8_t>(pair.execution_order));
  hash.AddU64(pair.baseline.attempt_checksum);
  hash.AddU64(pair.candidate.attempt_checksum);
  hash.AddBool(pair.result.has_value());
  hash.AddU64(pair.result.has_value() ? pair.result->artifact_checksum : 0);
  return hash.Finish();
}

[[nodiscard]] Phase4LexicographicComparison CompareBoardOutcomes(
    const Phase4BoardOutcome& baseline, const Phase4BoardOutcome& candidate) noexcept {
  if (baseline.selected_net_count != candidate.selected_net_count) {
    return baseline.selected_net_count > candidate.selected_net_count
               ? Phase4LexicographicComparison::kBaselinePreferred
               : Phase4LexicographicComparison::kCandidatePreferred;
  }
  if (baseline.total_overuse_units != candidate.total_overuse_units) {
    return baseline.total_overuse_units < candidate.total_overuse_units
               ? Phase4LexicographicComparison::kBaselinePreferred
               : Phase4LexicographicComparison::kCandidatePreferred;
  }
  if (baseline.total_intrinsic_cost != candidate.total_intrinsic_cost) {
    return baseline.total_intrinsic_cost < candidate.total_intrinsic_cost
               ? Phase4LexicographicComparison::kBaselinePreferred
               : Phase4LexicographicComparison::kCandidatePreferred;
  }
  return Phase4LexicographicComparison::kTie;
}

void FinalizeChecksums(Phase4IsolatedCellResult* result) noexcept {
  for (Phase4IsolatedPairAttempt& pair : result->attempts) {
    pair.baseline.attempt_checksum = ArmAttemptChecksum(pair.baseline);
    pair.candidate.attempt_checksum = ArmAttemptChecksum(pair.candidate);
    pair.attempt_checksum = PairAttemptChecksum(pair);
  }
  if (result->carrier == Phase4IsolatedCellCarrier::kRawWireV1) {
    result->artifact_checksum = ComputePhase4IsolatedCellArtifactChecksumV1(*result);
  } else if (result->carrier == Phase4IsolatedCellCarrier::kSameRunWireV2) {
    result->artifact_checksum = ComputePhase4SameRunIsolatedCellArtifactChecksumV2(*result);
  } else {
    result->artifact_checksum = 0;
  }
}

template <typename Execution>
void InvalidateWorkerSuccesses(Phase4IsolatedCellResult* result, const WorkerProcess& worker,
                               std::vector<std::optional<Execution>>* executions,
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

void AttachUnavailableProcessAuthority(Phase4IsolatedCellResult* result,
                                       const WorkerProcess& worker, std::string_view invariant,
                                       std::string_view detail) {
  for (auto pair = result->attempts.rbegin(); pair != result->attempts.rend(); ++pair) {
    Phase4IsolatedArmAttempt* attempt = ArmAttempt(&*pair, worker.arm);
    if (attempt->process_instance_identity != worker.process_instance_identity ||
        attempt->disposition == Phase4IsolatedAttemptDisposition::kNotRunAfterFatal) {
      continue;
    }
    if (attempt->controller_invariant_id == "P4HARNESS-REAP-BOUNDED-001" ||
        attempt->controller_invariant_id == "P4HARNESS-WAIT4-AUTHORITY-001") {
      return;
    }
    std::string retained_detail;
    if (!attempt->controller_invariant_id.empty() || !attempt->controller_detail.empty()) {
      retained_detail = " after retained failure [" + attempt->controller_invariant_id +
                        "]: " + attempt->controller_detail;
    }
    attempt->controller_invariant_id = std::string(invariant);
    attempt->controller_detail = std::string(detail) + retained_detail;
    return;
  }
}

template <typename Execution>
void ApplyProcessExit(Phase4IsolatedCellResult* result, WorkerProcess* worker,
                      std::vector<std::optional<Execution>>* executions) {
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
  if (worker->wait_error_number != 0) {
    AttachUnavailableProcessAuthority(result, *worker, "P4HARNESS-WAIT4-AUTHORITY-001",
                                      "the controller could not reap the exact worker PID");
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
    Phase4TrialExecutionAuthority authority, Phase4TrialArm arm, Phase4PairedTrialErrorCode code,
    std::string_view invariant, std::string_view detail, std::uint64_t required = 0,
    std::uint64_t configured = 0) {
  return ReconcileFailureForAuthority(
      authority, Phase4TrialArmFailure{
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

[[nodiscard]] bool ReadyMatchesCell(Phase4TrialExecutionAuthority authority,
                                    const internal::Phase4TrialWireReady& ready,
                                    const Phase4CanonicalCellConfig& cell) noexcept {
  const std::optional<Phase4RepresentativeCorpusAuthority> corpus = CorpusAuthority(authority);
  if (!corpus.has_value()) {
    return false;
  }
  const Phase4CaseDescriptor* descriptor =
      FindPhase4CaseDescriptorForAuthority(*corpus, cell.case_id);
  return descriptor != nullptr && ready.case_id == cell.case_id &&
         ready.descriptor_fingerprint ==
             FingerprintPhase4CaseDescriptorForAuthority(*corpus, *descriptor);
}

[[nodiscard]] bool FailureMatchesCell(Phase4TrialExecutionAuthority authority,
                                      const Phase4DurableArmFailure& failure,
                                      const Phase4CanonicalCellConfig& cell,
                                      const internal::Phase4TrialWireReady* peer_ready) noexcept {
  if (failure.payload_kind == Phase4DurableFailurePayloadKind::kSummaryOnly) {
    return true;
  }
  const std::optional<Phase4RepresentativeCorpusAuthority> corpus = CorpusAuthority(authority);
  if (!corpus.has_value()) {
    return false;
  }
  const Phase4CaseDescriptor* descriptor =
      FindPhase4CaseDescriptorForAuthority(*corpus, cell.case_id);
  const bool matches_active_cell =
      failure.case_id == cell.case_id &&
      (failure.payload_kind == Phase4DurableFailurePayloadKind::kCorpus ||
       (failure.has_case_identity && descriptor != nullptr &&
        failure.descriptor_fingerprint ==
            FingerprintPhase4CaseDescriptorForAuthority(*corpus, *descriptor)));
  return matches_active_cell &&
         (peer_ready == nullptr || MatchesReadyIdentity(*peer_ready, failure));
}

[[nodiscard]] bool SendWorkerMessage(int descriptor,
                                     const Phase4TrialWireMessage& message) noexcept {
  auto result = internal::WritePhase4TrialWireMessageV1(descriptor, message);
  return std::holds_alternative<std::monostate>(result);
}

[[nodiscard]] bool SendWorkerMessageV2(int descriptor,
                                       const internal::Phase4TrialWireMessageV2& message) noexcept {
  auto result = internal::WritePhase4TrialWireMessageV2(descriptor, message);
  return std::holds_alternative<std::monostate>(result);
}

struct ControllerArmExecution {
  Phase4TrialArmExecution execution;
  std::optional<Phase4SameRunArmDecisionTelemetryV1> same_run_telemetry;
};

[[nodiscard]] bool H4096SemanticsMatchExpectedSpec(WorkerWireMode wire_mode,
                                                   const Phase4PairedTrialSpec& expected_spec,
                                                   const Phase4TrialArmSemantics& semantics) {
  const bool ordinary = wire_mode == WorkerWireMode::kRawV1;
  const std::optional<Phase4PairedTrialError> preflight =
      ordinary
          ? internal::PreflightPhase4ConfirmatoryH4096OrdinarySpec(expected_spec, semantics.arm)
          : internal::PreflightPhase4ConfirmatoryH4096SameRunSpec(expected_spec, semantics.arm);
  if (preflight.has_value() ||
      internal::ValidatePhase4TrialArmSemanticsForAuthorityV1(
          Phase4RepresentativeCorpusAuthority::kV2, semantics)
          .has_value() ||
      expected_spec.candidate_session_config.schedules.size() != 2) {
    return false;
  }
  const Phase4CaseDescriptor* descriptor = FindPhase4CaseDescriptorForAuthority(
      Phase4RepresentativeCorpusAuthority::kV2, expected_spec.case_id);
  if (descriptor == nullptr) {
    return false;
  }
  const Phase4RouteOpportunity expected_opportunity{
      .route_queries = expected_spec.baseline_config.limits.maximum_route_queries,
      .route_work_units = expected_spec.baseline_config.limits.maximum_total_route_work_units,
  };
  const std::uint64_t expected_columns_per_epoch =
      expected_spec.candidate_session_config.regeneration_execution_config.maximum_route_queries;
  const std::uint32_t expected_terminal_rounds =
      expected_spec.candidate_session_config.schedules[1].maximum_selection_rounds;
  const std::uint64_t expected_budget_checksum =
      internal::ComputePhase4PairedBudgetChecksumForAuthorityV1(
          Phase4RepresentativeCorpusAuthority::kV2, expected_spec, expected_opportunity,
          descriptor->requested_net_count, expected_columns_per_epoch, expected_terminal_rounds);
  return semantics.execution_order == expected_spec.execution_order &&
         semantics.corpus_version == Phase4RepresentativeCorpusVersionForAuthority(
                                         Phase4RepresentativeCorpusAuthority::kV2) &&
         semantics.corpus_checksum == Phase4RepresentativeCorpusChecksumForAuthority(
                                          Phase4RepresentativeCorpusAuthority::kV2) &&
         semantics.case_id == expected_spec.case_id &&
         semantics.descriptor_fingerprint ==
             FingerprintPhase4CaseDescriptorForAuthority(Phase4RepresentativeCorpusAuthority::kV2,
                                                         *descriptor) &&
         semantics.budget_checksum == expected_budget_checksum &&
         semantics.workload_net_count == descriptor->requested_net_count &&
         semantics.requested_pool_size == expected_spec.requested_pool_size &&
         semantics.repetition_index == expected_spec.repetition_index &&
         semantics.root_seed == expected_spec.root_seed &&
         semantics.preparation_worker_count == expected_spec.preparation_worker_count &&
         semantics.baseline_sweeps == expected_spec.baseline_config.maximum_sweeps &&
         semantics.candidate_regeneration_epochs ==
             expected_spec.candidate_session_config.maximum_regeneration_epochs &&
         semantics.candidate_columns_per_epoch == expected_columns_per_epoch &&
         semantics.candidate_terminal_selection_rounds == expected_terminal_rounds &&
         semantics.external_budget == expected_spec.external_budget &&
         semantics.opportunity == expected_opportunity;
}

[[nodiscard]] bool DecodedH4096SuccessMatchesExpectedSpec(
    WorkerWireMode wire_mode, const Phase4PairedTrialSpec& expected_spec,
    const ControllerArmExecution& execution) {
  const Phase4TrialArmSemantics& semantics = execution.execution.semantics;
  return H4096SemanticsMatchExpectedSpec(wire_mode, expected_spec, semantics) &&
         (wire_mode == WorkerWireMode::kRawV1
              ? !execution.same_run_telemetry.has_value()
              : execution.same_run_telemetry.has_value() &&
                    execution.same_run_telemetry->associated_semantic_checksum ==
                        semantics.semantic_checksum);
}

struct ControllerWireSuccess {
  ControllerArmExecution execution;
};

using ControllerWireMessage =
    std::variant<internal::Phase4TrialWireReady, internal::Phase4TrialWireRunCommand,
                 internal::Phase4TrialWireStop, ControllerWireSuccess,
                 internal::Phase4TrialWireFailure, internal::Phase4TrialWireStopped>;
using ControllerAwaitResult = BasicAwaitResult<ControllerWireMessage>;

[[nodiscard]] ControllerAwaitResult NormalizeAwaitResult(AwaitResult result) {
  ControllerAwaitResult normalized{.kind = result.kind,
                                   .message = std::nullopt,
                                   .invariant_id = std::move(result.invariant_id),
                                   .detail = std::move(result.detail),
                                   .elapsed_nanoseconds = result.elapsed_nanoseconds};
  if (!result.message.has_value()) {
    return normalized;
  }
  normalized.message = std::visit(
      [](auto payload) -> ControllerWireMessage {
        using Payload = decltype(payload);
        if constexpr (std::is_same_v<Payload, internal::Phase4TrialWireSuccess>) {
          return ControllerWireSuccess{
              .execution = ControllerArmExecution{.execution = std::move(payload.execution),
                                                  .same_run_telemetry = std::nullopt}};
        } else {
          return std::move(payload);
        }
      },
      std::move(*result.message));
  return normalized;
}

[[nodiscard]] ControllerAwaitResult NormalizeAwaitResult(AwaitResultV2 result) {
  ControllerAwaitResult normalized{.kind = result.kind,
                                   .message = std::nullopt,
                                   .invariant_id = std::move(result.invariant_id),
                                   .detail = std::move(result.detail),
                                   .elapsed_nanoseconds = result.elapsed_nanoseconds};
  if (!result.message.has_value()) {
    return normalized;
  }
  normalized.message = std::visit(
      [](auto payload) -> ControllerWireMessage {
        using Payload = decltype(payload);
        if constexpr (std::is_same_v<Payload, internal::Phase4TrialWireSuccessV2>) {
          return ControllerWireSuccess{
              .execution = ControllerArmExecution{
                  .execution = std::move(payload.decision_execution.execution),
                  .same_run_telemetry = std::move(payload.decision_execution.telemetry)}};
        } else {
          return std::move(payload);
        }
      },
      std::move(*result.message));
  return normalized;
}

[[nodiscard]] ControllerAwaitResult AwaitControllerMessage(
    WorkerProcess* worker, Clock::time_point start, Clock::time_point deadline,
    WorkerWireMode wire_mode, Phase4TrialExecutionAuthority authority) {
  return wire_mode == WorkerWireMode::kRawV1
             ? NormalizeAwaitResult(AwaitMessage(worker, start, deadline, authority))
             : NormalizeAwaitResult(AwaitMessageV2(worker, start, deadline, authority));
}

template <typename Control>
[[nodiscard]] bool SendControllerMessage(int descriptor, Control control,
                                         WorkerWireMode wire_mode) noexcept {
  if (wire_mode == WorkerWireMode::kRawV1) {
    return SendWorkerMessage(descriptor, Phase4TrialWireMessage{std::move(control)});
  }
  return SendWorkerMessageV2(descriptor, internal::Phase4TrialWireMessageV2{std::move(control)});
}

using ControllerWireReadResult =
    std::variant<ControllerWireMessage, internal::Phase4TrialWireError>;

[[nodiscard]] ControllerWireReadResult ReadControllerMessage(int descriptor,
                                                             WorkerWireMode wire_mode) {
  if (wire_mode == WorkerWireMode::kRawV1) {
    internal::Phase4TrialWireDecodeResult decoded =
        internal::ReadPhase4TrialWireMessageV1(descriptor);
    if (std::holds_alternative<internal::Phase4TrialWireError>(decoded)) {
      return std::get<internal::Phase4TrialWireError>(std::move(decoded));
    }
    AwaitResult result{.kind = AwaitKind::kMessage,
                       .message = std::get<Phase4TrialWireMessage>(std::move(decoded)),
                       .invariant_id = {},
                       .detail = {},
                       .elapsed_nanoseconds = 0};
    return *NormalizeAwaitResult(std::move(result)).message;
  }
  internal::Phase4TrialWireDecodeResultV2 decoded =
      internal::ReadPhase4TrialWireMessageV2(descriptor);
  if (std::holds_alternative<internal::Phase4TrialWireError>(decoded)) {
    return std::get<internal::Phase4TrialWireError>(std::move(decoded));
  }
  AwaitResultV2 result{.kind = AwaitKind::kMessage,
                       .message = std::get<internal::Phase4TrialWireMessageV2>(std::move(decoded)),
                       .invariant_id = {},
                       .detail = {},
                       .elapsed_nanoseconds = 0};
  return *NormalizeAwaitResult(std::move(result)).message;
}

[[nodiscard]] bool SendControllerSuccess(int descriptor, ControllerArmExecution execution,
                                         WorkerWireMode wire_mode,
                                         Phase4TrialExecutionAuthority authority) noexcept {
  const std::optional<Phase4RepresentativeCorpusAuthority> corpus = CorpusAuthority(authority);
  if (!corpus.has_value()) {
    return false;
  }
  if (wire_mode == WorkerWireMode::kRawV1) {
    if (execution.same_run_telemetry.has_value()) {
      return false;
    }
    const Phase4TrialWireMessage message =
        internal::Phase4TrialWireSuccess{std::move(execution.execution)};
    const auto result = *corpus == Phase4RepresentativeCorpusAuthority::kV1
                            ? internal::WritePhase4TrialWireMessageV1(descriptor, message)
                            : internal::WritePhase4TrialWireMessageForCorpusV2(descriptor, message);
    return std::holds_alternative<std::monostate>(result);
  }
  if (!execution.same_run_telemetry.has_value()) {
    return false;
  }
  const internal::Phase4TrialWireMessageV2 message = internal::Phase4TrialWireSuccessV2{
      .decision_execution = Phase4TrialArmWithSameRunTelemetryExecutionV1{
          .execution = std::move(execution.execution),
          .telemetry = std::move(*execution.same_run_telemetry),
      }};
  const auto result = *corpus == Phase4RepresentativeCorpusAuthority::kV1
                          ? internal::WritePhase4TrialWireMessageV2(descriptor, message)
                          : internal::WritePhase4TrialWireMessageV2ForCorpusV2(descriptor, message);
  return std::holds_alternative<std::monostate>(result);
}

}  // namespace

std::uint64_t ComputePhase4IsolatedSameRunArmCaptureChecksumV1(
    const Phase4IsolatedSameRunArmDecisionTelemetryV1& capture) noexcept {
  board_ir::StableHashBuilder hash;
  hash.AddString("APGAR-PHASE4-ISOLATED-SAME-RUN-ARM-CAPTURE-V1");
  hash.AddU32(capture.schema_version);
  hash.AddByte(static_cast<std::uint8_t>(capture.arm));
  hash.AddU32(capture.repetition_index);
  hash.AddByte(static_cast<std::uint8_t>(capture.execution_order));
  hash.AddU64(capture.dispatch_ordinal);
  hash.AddU64(capture.process_instance_identity);
  hash.AddU64(capture.associated_semantic_checksum);
  hash.AddU64(capture.associated_arm_artifact_checksum);
  hash.AddU64(capture.associated_authority_checksum);
  hash.AddU64(capture.associated_arm_attempt_checksum);
  hash.AddU64(capture.telemetry.telemetry_checksum);
  return hash.Finish();
}

std::uint64_t ComputePhase4IsolatedSameRunPairCaptureChecksumV1(
    const Phase4IsolatedSameRunPairDecisionTelemetryV1& capture) noexcept {
  board_ir::StableHashBuilder hash;
  hash.AddString("APGAR-PHASE4-ISOLATED-SAME-RUN-PAIR-CAPTURE-V1");
  hash.AddU32(capture.schema_version);
  hash.AddU32(capture.case_id);
  hash.AddU32(capture.requested_pool_size);
  hash.AddU32(capture.repetition_index);
  hash.AddU64(capture.root_seed);
  hash.AddByte(static_cast<std::uint8_t>(capture.execution_order));
  hash.AddU64(capture.associated_raw_pair_attempt_checksum);
  hash.AddU64(capture.associated_paired_semantic_checksum);
  hash.AddU64(capture.associated_paired_artifact_checksum);
  hash.AddU64(capture.baseline.capture_checksum);
  hash.AddU64(capture.candidate.capture_checksum);
  return hash.Finish();
}

std::uint64_t ComputePhase4IsolatedSameRunCellCaptureChecksumV1(
    const Phase4IsolatedCellWithSameRunDecisionTelemetryV1& capture,
    std::uint64_t raw_source_envelope_checksum) noexcept {
  board_ir::StableHashBuilder hash;
  hash.AddString("APGAR-PHASE4-ISOLATED-SAME-RUN-CELL-CAPTURE-V1");
  hash.AddU32(capture.schema_version);
  hash.AddU32(kPhase4SameRunRawEvidenceSchemaVersion);
  hash.AddU32(kPhase4SameRunTrialWireSchemaVersion);
  hash.AddU32(internal::kPhase4TrialWireSchemaVersionV2);
  const Phase4IsolatedCellResult& raw = capture.raw_cell;
  const Phase4CanonicalCellConfig& config = raw.config;
  hash.AddU32(config.schema_version);
  hash.AddU32(config.case_id);
  hash.AddU32(config.requested_pool_size);
  hash.AddU32(config.preparation_worker_count);
  hash.AddU32(config.repetitions);
  hash.AddU64(config.maximum_setup_elapsed_nanoseconds);
  hash.AddU64(config.external_budget.maximum_prepared_elapsed_nanoseconds);
  hash.AddU64(config.external_budget.maximum_cold_elapsed_nanoseconds);
  hash.AddU64(config.external_budget.maximum_address_space_bytes);
  hash.AddU64(config.external_budget.maximum_peak_host_bytes);
  hash.AddU64(config.corpus_limits.maximum_nets);
  hash.AddU64(config.corpus_limits.maximum_compiled_nodes);
  hash.AddU64(config.corpus_limits.maximum_compiled_host_bytes);
  hash.AddU64(config.corpus_limits.maximum_active_regions);
  hash.AddU64(config.corpus_limits.maximum_board_entities);
  hash.AddU64(raw.corpus_checksum);
  hash.AddU64(raw.cell_plan_checksum);
  hash.AddU64(raw.environment.environment_checksum);
  hash.AddU64(raw.authority_run_identity);
  hash.AddU64(raw.controller_identity);
  hash.AddU64(raw.artifact_checksum);
  hash.AddU64(raw_source_envelope_checksum);
  hash.AddU64(capture.same_run_attempts.size());
  for (const auto& pair : capture.same_run_attempts) {
    hash.AddU64(pair.capture_checksum);
  }
  return hash.Finish();
}

#if defined(APGAR_PHASE4_TRIAL_FAULT_TEST_VARIANT)
void RehashForeignPhase4SameRunRecordForTesting(
    Phase4IsolatedCellWithSameRunDecisionTelemetryV1* capture) noexcept {
  if (capture == nullptr || capture->raw_cell.attempts.empty() ||
      capture->same_run_attempts.empty()) {
    return;
  }
  Phase4IsolatedPairAttempt& raw_pair = capture->raw_cell.attempts.front();
  if (!raw_pair.baseline.record.has_value() || !raw_pair.result.has_value()) {
    return;
  }
  Phase4TrialArmRecord& record = *raw_pair.baseline.record;
  ++record.semantics.case_id;
  record.semantics.semantic_checksum =
      internal::ComputePhase4TrialArmSemanticChecksumV1(record.semantics);
  record.external_observation.associated_semantic_checksum = record.semantics.semantic_checksum;
  record.external_observation.authority_checksum =
      internal::ComputePhase4ExternalAuthorityChecksumV1(record.external_observation);
  record.artifact_checksum = internal::ComputePhase4TrialArmArtifactChecksumV1(record);

  raw_pair.result->baseline = record;
  raw_pair.result->semantic_checksum =
      internal::ComputePhase4PairedTrialSemanticChecksumV1(*raw_pair.result);
  raw_pair.result->artifact_checksum =
      internal::ComputePhase4PairedTrialArtifactChecksumV1(*raw_pair.result);
  raw_pair.baseline.attempt_checksum = ArmAttemptChecksum(raw_pair.baseline);
  raw_pair.attempt_checksum = PairAttemptChecksum(raw_pair);
  capture->raw_cell.artifact_checksum =
      ComputePhase4SameRunIsolatedCellArtifactChecksumV2(capture->raw_cell);

  Phase4IsolatedSameRunPairDecisionTelemetryV1& pair = capture->same_run_attempts.front();
  Phase4IsolatedSameRunArmDecisionTelemetryV1& arm = pair.baseline;
  arm.associated_semantic_checksum = record.semantics.semantic_checksum;
  arm.associated_arm_artifact_checksum = record.artifact_checksum;
  arm.associated_authority_checksum = record.external_observation.authority_checksum;
  arm.associated_arm_attempt_checksum = raw_pair.baseline.attempt_checksum;
  arm.telemetry.associated_semantic_checksum = record.semantics.semantic_checksum;
  arm.telemetry.telemetry_checksum =
      internal::ComputePhase4SameRunArmDecisionTelemetryChecksumV1(arm.telemetry);
  arm.capture_checksum = ComputePhase4IsolatedSameRunArmCaptureChecksumV1(arm);
  pair.associated_raw_pair_attempt_checksum = raw_pair.attempt_checksum;
  pair.associated_paired_semantic_checksum = raw_pair.result->semantic_checksum;
  pair.associated_paired_artifact_checksum = raw_pair.result->artifact_checksum;
  pair.capture_checksum = ComputePhase4IsolatedSameRunPairCaptureChecksumV1(pair);
}

void RehashWrongPhase4SameRunComparisonForTesting(
    Phase4IsolatedCellWithSameRunDecisionTelemetryV1* capture) noexcept {
  if (capture == nullptr || capture->raw_cell.attempts.empty() ||
      capture->same_run_attempts.empty() ||
      !capture->raw_cell.attempts.front().result.has_value()) {
    return;
  }
  Phase4IsolatedPairAttempt& raw_pair = capture->raw_cell.attempts.front();
  Phase4PairedTrialResult& result = *raw_pair.result;
  const Phase4LexicographicComparison authentic =
      CompareBoardOutcomes(result.baseline.semantics.outcome, result.candidate.semantics.outcome);
  result.comparison = authentic == Phase4LexicographicComparison::kTie
                          ? Phase4LexicographicComparison::kBaselinePreferred
                          : Phase4LexicographicComparison::kTie;
  result.semantic_checksum = internal::ComputePhase4PairedTrialSemanticChecksumV1(result);
  result.artifact_checksum = internal::ComputePhase4PairedTrialArtifactChecksumV1(result);
  raw_pair.attempt_checksum = PairAttemptChecksum(raw_pair);
  capture->raw_cell.artifact_checksum =
      ComputePhase4SameRunIsolatedCellArtifactChecksumV2(capture->raw_cell);

  Phase4IsolatedSameRunPairDecisionTelemetryV1& pair = capture->same_run_attempts.front();
  pair.associated_raw_pair_attempt_checksum = raw_pair.attempt_checksum;
  pair.associated_paired_semantic_checksum = result.semantic_checksum;
  pair.associated_paired_artifact_checksum = result.artifact_checksum;
  pair.capture_checksum = ComputePhase4IsolatedSameRunPairCaptureChecksumV1(pair);
}

void RehashNondeterministicPhase4SameRunRecordForTesting(
    Phase4IsolatedCellWithSameRunDecisionTelemetryV1* capture) noexcept {
  constexpr std::size_t kDivergentRepetition = 1;
  if (capture == nullptr || capture->raw_cell.attempts.size() <= kDivergentRepetition ||
      capture->same_run_attempts.size() <= kDivergentRepetition) {
    return;
  }
  Phase4IsolatedPairAttempt& raw_pair = capture->raw_cell.attempts[kDivergentRepetition];
  if (!raw_pair.baseline.record.has_value() || !raw_pair.result.has_value()) {
    return;
  }
  Phase4TrialArmRecord& record = *raw_pair.baseline.record;
  ++record.semantics.outcome.total_intrinsic_cost;
  record.semantics.semantic_checksum =
      internal::ComputePhase4TrialArmSemanticChecksumV1(record.semantics);
  record.external_observation.associated_semantic_checksum = record.semantics.semantic_checksum;
  record.external_observation.authority_checksum =
      internal::ComputePhase4ExternalAuthorityChecksumV1(record.external_observation);
  record.artifact_checksum = internal::ComputePhase4TrialArmArtifactChecksumV1(record);

  raw_pair.result->baseline = record;
  raw_pair.result->comparison = CompareBoardOutcomes(raw_pair.result->baseline.semantics.outcome,
                                                     raw_pair.result->candidate.semantics.outcome);
  raw_pair.result->semantic_checksum =
      internal::ComputePhase4PairedTrialSemanticChecksumV1(*raw_pair.result);
  raw_pair.result->artifact_checksum =
      internal::ComputePhase4PairedTrialArtifactChecksumV1(*raw_pair.result);
  raw_pair.baseline.attempt_checksum = ArmAttemptChecksum(raw_pair.baseline);
  raw_pair.attempt_checksum = PairAttemptChecksum(raw_pair);
  capture->raw_cell.artifact_checksum =
      ComputePhase4SameRunIsolatedCellArtifactChecksumV2(capture->raw_cell);

  Phase4IsolatedSameRunPairDecisionTelemetryV1& pair =
      capture->same_run_attempts[kDivergentRepetition];
  Phase4IsolatedSameRunArmDecisionTelemetryV1& arm = pair.baseline;
  arm.associated_semantic_checksum = record.semantics.semantic_checksum;
  arm.associated_arm_artifact_checksum = record.artifact_checksum;
  arm.associated_authority_checksum = record.external_observation.authority_checksum;
  arm.associated_arm_attempt_checksum = raw_pair.baseline.attempt_checksum;
  arm.telemetry.associated_semantic_checksum = record.semantics.semantic_checksum;
  arm.telemetry.outcome = record.semantics.outcome;
  arm.telemetry.telemetry_checksum =
      internal::ComputePhase4SameRunArmDecisionTelemetryChecksumV1(arm.telemetry);
  arm.capture_checksum = ComputePhase4IsolatedSameRunArmCaptureChecksumV1(arm);
  pair.associated_raw_pair_attempt_checksum = raw_pair.attempt_checksum;
  pair.associated_paired_semantic_checksum = raw_pair.result->semantic_checksum;
  pair.associated_paired_artifact_checksum = raw_pair.result->artifact_checksum;
  pair.capture_checksum = ComputePhase4IsolatedSameRunPairCaptureChecksumV1(pair);
}
#endif

[[nodiscard]] static bool ValidatePhase4IsolatedSameRunCellCaptureForAuthority(
    Phase4TrialExecutionAuthority execution_authority,
    const Phase4IsolatedCellWithSameRunDecisionTelemetryV1& capture,
    std::string_view imported_fixture) {
  const Phase4IsolatedCellResult& raw = capture.raw_cell;
  const std::optional<Phase4RepresentativeCorpusAuthority> corpus =
      CorpusAuthority(execution_authority);
  if (!corpus.has_value()) {
    return false;
  }
  if (execution_authority == Phase4TrialExecutionAuthority::kCorpusV2H4096 &&
      PreflightConfirmatoryH4096Cell(execution_authority, WorkerWireMode::kSameRunTelemetryV2,
                                     raw.config, std::nullopt)
          .has_value()) {
    return false;
  }
  if (capture.schema_version != kPhase4IsolatedSameRunTelemetrySchemaVersion ||
      raw.schema_version != kPhase4TrialHarnessSchemaVersion ||
      raw.carrier != Phase4IsolatedCellCarrier::kSameRunWireV2 ||
      raw.config.schema_version != kPhase4TrialHarnessSchemaVersion ||
      raw.config.repetitions != kPhase4CanonicalRepetitionsV1 ||
      raw.config.preparation_worker_count != kPhase4CanonicalPreparationWorkersV1 ||
      raw.corpus_checksum != Phase4RepresentativeCorpusChecksumForAuthority(*corpus) ||
      raw.cell_plan_checksum != CellPlanChecksum(execution_authority, raw.config) ||
      raw.environment.environment_checksum == 0 ||
      raw.environment.environment_checksum != HostEnvironmentChecksum(raw.environment) ||
      raw.controller_identity == 0 || raw.authority_run_identity == 0 ||
      raw.authority_run_identity != RunIdentity(raw.config, raw.controller_identity) ||
      raw.attempts.size() != kPhase4CanonicalRepetitionsV1 ||
      capture.same_run_attempts.size() != kPhase4CanonicalRepetitionsV1 ||
      raw.artifact_checksum == 0 ||
      raw.artifact_checksum != ComputePhase4SameRunIsolatedCellArtifactChecksumV2(raw)) {
    return false;
  }
  Phase4RepresentativeCaseResult built_case = BuildPhase4RepresentativeCaseForAuthority(
      *corpus, raw.config.case_id, imported_fixture, raw.config.corpus_limits);
  if (!std::holds_alternative<Phase4RepresentativeCase>(built_case)) {
    return false;
  }
  const Phase4RepresentativeCase& representative_case =
      std::get<Phase4RepresentativeCase>(built_case);
  const std::uint64_t expected_capacity_checksum =
      allocator::internal::ComputeResourceCapacityModelChecksumV1(
          allocator::internal::ResourceCapacityChecksumHeaderV1{
              .schema_version = representative_case.capacities.schema_version(),
              .associations = representative_case.capacities.associations(),
              .default_capacity_units = representative_case.capacities.default_capacity_units(),
          },
          representative_case.capacities.overrides());

  std::uint64_t root_seed = 0;
  std::uint64_t next_dispatch_ordinal = 1;
  std::array<std::uint64_t, 2> process_identities{};
  std::array<std::uint64_t, 2> process_peaks{};
  std::array<std::optional<Phase4TrialArmSemantics>, 2> deterministic_semantics;
  std::optional<Phase4LexicographicComparison> deterministic_comparison;
  const auto valid_arm = [&](const Phase4IsolatedArmAttempt& attempt,
                             const Phase4IsolatedSameRunArmDecisionTelemetryV1& arm_capture,
                             const Phase4TrialArmRecord& paired_record,
                             const Phase4PairedTrialSpec& spec, Phase4TrialArm expected_arm,
                             std::uint32_t repetition, Phase4TrialOrder order) {
    const std::size_t arm_index = expected_arm == Phase4TrialArm::kSequentialBaseline ? 0U : 1U;
    if (attempt.schema_version != kPhase4TrialHarnessSchemaVersion || attempt.arm != expected_arm ||
        attempt.repetition_index != repetition || attempt.execution_order != order ||
        attempt.disposition != Phase4IsolatedAttemptDisposition::kSuccess ||
        attempt.dispatch_ordinal != next_dispatch_ordinal++ ||
        attempt.process_instance_identity == 0 || attempt.process_lifetime_peak_host_bytes == 0 ||
        attempt.raw_wait_status != 0 || attempt.process_exit_code != 0 ||
        attempt.terminating_signal != 0 || attempt.watchdog_kill_sent ||
        !attempt.controller_invariant_id.empty() || !attempt.controller_detail.empty() ||
        !attempt.record.has_value() || attempt.child_failure.has_value() ||
        attempt.attempt_checksum != ArmAttemptChecksum(attempt)) {
      return false;
    }
    if (process_identities[arm_index] == 0) {
      process_identities[arm_index] = attempt.process_instance_identity;
      process_peaks[arm_index] = attempt.process_lifetime_peak_host_bytes;
    } else if (process_identities[arm_index] != attempt.process_instance_identity ||
               process_peaks[arm_index] != attempt.process_lifetime_peak_host_bytes) {
      return false;
    }

    const Phase4TrialArmRecord& record = *attempt.record;
    const Phase4TrialArmSemantics& semantics = record.semantics;
    const Phase4ExternalResourceObservation& authority = record.external_observation;
    if (spec.candidate_session_config.schedules.size() != 2) {
      return false;
    }
    const Phase4RouteOpportunity expected_opportunity{
        .route_queries = spec.baseline_config.limits.maximum_route_queries,
        .route_work_units = spec.baseline_config.limits.maximum_total_route_work_units,
    };
    const std::uint64_t expected_columns_per_epoch =
        spec.candidate_session_config.regeneration_execution_config.maximum_route_queries;
    const std::uint32_t expected_terminal_rounds =
        spec.candidate_session_config.schedules[1].maximum_selection_rounds;
    const std::uint64_t expected_budget_checksum =
        internal::ComputePhase4PairedBudgetChecksumForAuthorityV1(
            *corpus, spec, expected_opportunity,
            static_cast<std::uint32_t>(representative_case.workload.nets().size()),
            expected_columns_per_epoch, expected_terminal_rounds);
    const Phase4PreparerLifecycleObservation expected_lifecycle =
        expected_arm == Phase4TrialArm::kSequentialBaseline
            ? Phase4PreparerLifecycleObservation{}
            : Phase4PreparerLifecycleObservation{
                  .workers_started_before = raw.config.preparation_worker_count,
                  .workers_started_after = raw.config.preparation_worker_count,
                  .invocations_started_before = static_cast<std::uint64_t>(repetition) + 1U,
                  .invocations_started_after = static_cast<std::uint64_t>(repetition) + 2U,
                  .invocations_completed_before = static_cast<std::uint64_t>(repetition) + 1U,
                  .invocations_completed_after = static_cast<std::uint64_t>(repetition) + 2U,
              };
    if (record != paired_record ||
        internal::ValidatePhase4TrialArmSemanticsForAuthorityV1(*corpus, semantics).has_value() ||
        semantics.arm != expected_arm || semantics.execution_order != order ||
        semantics.corpus_version != Phase4RepresentativeCorpusVersionForAuthority(*corpus) ||
        semantics.corpus_checksum != raw.corpus_checksum ||
        semantics.case_id != raw.config.case_id ||
        semantics.descriptor_fingerprint !=
            FingerprintPhase4CaseDescriptorForAuthority(*corpus, representative_case.descriptor) ||
        semantics.case_checksum != representative_case.case_checksum ||
        semantics.board_content_hash != representative_case.board.content_hash() ||
        semantics.workload_checksum != representative_case.workload.workload_checksum() ||
        semantics.capacity_model_checksum != expected_capacity_checksum ||
        semantics.budget_checksum != expected_budget_checksum ||
        semantics.workload_net_count != representative_case.workload.nets().size() ||
        semantics.requested_pool_size != raw.config.requested_pool_size ||
        semantics.repetition_index != repetition || semantics.root_seed != spec.root_seed ||
        semantics.preparation_worker_count != raw.config.preparation_worker_count ||
        semantics.baseline_sweeps != spec.baseline_config.maximum_sweeps ||
        semantics.candidate_regeneration_epochs !=
            spec.candidate_session_config.maximum_regeneration_epochs ||
        semantics.candidate_columns_per_epoch != expected_columns_per_epoch ||
        semantics.candidate_terminal_selection_rounds != expected_terminal_rounds ||
        semantics.external_budget != raw.config.external_budget ||
        semantics.opportunity != expected_opportunity || semantics.semantic_checksum == 0 ||
        semantics.semantic_checksum !=
            internal::ComputePhase4TrialArmSemanticChecksumV1(semantics) ||
        record.preparer_lifecycle != expected_lifecycle ||
        authority.schema_version != kPhase4ExternalAuthoritySchemaVersion ||
        authority.authority_kind !=
            Phase4ExternalAuthorityKind::kLinuxParentWatchdogRlimitAndWait4 ||
        authority.authority_run_identity != raw.authority_run_identity ||
        authority.controller_identity != raw.controller_identity ||
        authority.associated_semantic_checksum != semantics.semantic_checksum ||
        authority.process_instance_identity != attempt.process_instance_identity ||
        authority.configured_wall_limit_nanoseconds !=
            raw.config.external_budget.maximum_cold_elapsed_nanoseconds ||
        authority.configured_address_space_limit_bytes !=
            raw.config.external_budget.maximum_address_space_bytes ||
        authority.configured_peak_host_limit_bytes !=
            raw.config.external_budget.maximum_peak_host_bytes ||
        authority.outer_elapsed_nanoseconds != attempt.outer_elapsed_nanoseconds ||
        authority.peak_host_bytes != attempt.process_lifetime_peak_host_bytes ||
        authority.process_exit_code != 0 || !authority.isolated_process ||
        !authority.wall_authority_enforced || !authority.memory_authority_enforced ||
        authority.persistent_preparer_reused !=
            (expected_arm == Phase4TrialArm::kReusableCandidateAllocation) ||
        authority.preparer_lifecycle != expected_lifecycle || authority.authority_checksum == 0 ||
        authority.authority_checksum !=
            internal::ComputePhase4ExternalAuthorityChecksumV1(authority) ||
        record.artifact_checksum == 0 ||
        record.artifact_checksum != internal::ComputePhase4TrialArmArtifactChecksumV1(record)) {
      return false;
    }

    const Phase4SameRunArmDecisionTelemetryV1& telemetry = arm_capture.telemetry;
    if (arm_capture.schema_version != kPhase4IsolatedSameRunTelemetrySchemaVersion ||
        arm_capture.arm != expected_arm || arm_capture.repetition_index != repetition ||
        arm_capture.execution_order != order ||
        arm_capture.dispatch_ordinal != attempt.dispatch_ordinal ||
        arm_capture.process_instance_identity != attempt.process_instance_identity ||
        arm_capture.associated_semantic_checksum != semantics.semantic_checksum ||
        arm_capture.associated_arm_artifact_checksum != record.artifact_checksum ||
        arm_capture.associated_authority_checksum != authority.authority_checksum ||
        arm_capture.associated_arm_attempt_checksum != attempt.attempt_checksum ||
        internal::ValidatePhase4SameRunArmDecisionTelemetryForAuthorityV1(
            *corpus, semantics, representative_case.workload, telemetry)
            .has_value() ||
        arm_capture.capture_checksum == 0 ||
        arm_capture.capture_checksum !=
            ComputePhase4IsolatedSameRunArmCaptureChecksumV1(arm_capture)) {
      return false;
    }
    Phase4TrialArmSemantics normalized = semantics;
    normalized.execution_order = Phase4TrialOrder::kBaselineFirst;
    normalized.repetition_index = 0;
    normalized.semantic_checksum = 0;
    if (!deterministic_semantics[arm_index].has_value()) {
      deterministic_semantics[arm_index] = std::move(normalized);
    } else if (*deterministic_semantics[arm_index] != normalized) {
      return false;
    }
    return true;
  };

  for (std::uint32_t repetition = 0; repetition < kPhase4CanonicalRepetitionsV1; ++repetition) {
    const Phase4IsolatedPairAttempt& pair = raw.attempts[repetition];
    const Phase4IsolatedSameRunPairDecisionTelemetryV1& pair_capture =
        capture.same_run_attempts[repetition];
    const Phase4TrialOrder order = repetition % 2U == 0U ? Phase4TrialOrder::kBaselineFirst
                                                         : Phase4TrialOrder::kCandidateFirst;
    Phase4CanonicalSpecResult spec_result =
        BuildCanonicalSpecForAuthority(execution_authority, raw.config, repetition, order);
    if (!std::holds_alternative<Phase4PairedTrialSpec>(spec_result)) {
      return false;
    }
    const Phase4PairedTrialSpec& spec = std::get<Phase4PairedTrialSpec>(spec_result);
    if (root_seed == 0) {
      root_seed = pair.root_seed;
    }
    if (pair.schema_version != kPhase4TrialHarnessSchemaVersion ||
        pair.case_id != raw.config.case_id ||
        pair.requested_pool_size != raw.config.requested_pool_size ||
        pair.repetition_index != repetition || pair.root_seed == 0 || pair.root_seed != root_seed ||
        pair.root_seed != spec.root_seed || pair.execution_order != order ||
        !pair.result.has_value()) {
      return false;
    }
    const Phase4PairedTrialResult& paired = *pair.result;
    const bool arms_valid =
        order == Phase4TrialOrder::kBaselineFirst
            ? valid_arm(pair.baseline, pair_capture.baseline, paired.baseline, spec,
                        Phase4TrialArm::kSequentialBaseline, repetition, order) &&
                  valid_arm(pair.candidate, pair_capture.candidate, paired.candidate, spec,
                            Phase4TrialArm::kReusableCandidateAllocation, repetition, order)
            : valid_arm(pair.candidate, pair_capture.candidate, paired.candidate, spec,
                        Phase4TrialArm::kReusableCandidateAllocation, repetition, order) &&
                  valid_arm(pair.baseline, pair_capture.baseline, paired.baseline, spec,
                            Phase4TrialArm::kSequentialBaseline, repetition, order);
    const Phase4LexicographicComparison expected_comparison =
        CompareBoardOutcomes(paired.baseline.semantics.outcome, paired.candidate.semantics.outcome);
    const bool comparison_is_deterministic =
        !deterministic_comparison.has_value() || *deterministic_comparison == paired.comparison;
    if (!arms_valid || paired.schema_version != kPhase4PairedTrialSchemaVersion ||
        paired.comparison != expected_comparison || !comparison_is_deterministic ||
        paired.semantic_checksum == 0 ||
        paired.semantic_checksum != internal::ComputePhase4PairedTrialSemanticChecksumV1(paired) ||
        paired.artifact_checksum == 0 ||
        paired.artifact_checksum != internal::ComputePhase4PairedTrialArtifactChecksumV1(paired) ||
        pair.attempt_checksum == 0 || pair.attempt_checksum != PairAttemptChecksum(pair) ||
        pair_capture.schema_version != kPhase4IsolatedSameRunTelemetrySchemaVersion ||
        pair_capture.case_id != pair.case_id ||
        pair_capture.requested_pool_size != pair.requested_pool_size ||
        pair_capture.repetition_index != repetition || pair_capture.root_seed != pair.root_seed ||
        pair_capture.execution_order != order ||
        pair_capture.associated_raw_pair_attempt_checksum != pair.attempt_checksum ||
        pair_capture.associated_paired_semantic_checksum != paired.semantic_checksum ||
        pair_capture.associated_paired_artifact_checksum != paired.artifact_checksum ||
        pair_capture.capture_checksum == 0 ||
        pair_capture.capture_checksum !=
            ComputePhase4IsolatedSameRunPairCaptureChecksumV1(pair_capture)) {
      return false;
    }
    if (!deterministic_comparison.has_value()) {
      deterministic_comparison = paired.comparison;
    }
  }
  return process_identities[0] != process_identities[1];
}

bool ValidatePhase4IsolatedSameRunCellCaptureV1(
    const Phase4IsolatedCellWithSameRunDecisionTelemetryV1& capture,
    std::string_view imported_fixture) {
  return ValidatePhase4IsolatedSameRunCellCaptureForAuthority(
      Phase4TrialExecutionAuthority::kCorpusV1, capture, imported_fixture);
}

bool ValidatePhase4IsolatedSameRunCellCaptureForCorpusV2(
    const Phase4IsolatedCellWithSameRunDecisionTelemetryV1& capture,
    std::string_view imported_fixture) {
  return ValidatePhase4IsolatedSameRunCellCaptureForAuthority(
      Phase4TrialExecutionAuthority::kCorpusV2H2250, capture, imported_fixture);
}

namespace {

[[nodiscard]] Phase4IsolatedCellExecution RunPhase4IsolatedCellImpl(
    Phase4TrialExecutionAuthority execution_authority, const Phase4CanonicalCellConfig& cell,
    std::string_view worker_executable, std::string_view imported_fixture_path,
    WorkerWireMode wire_mode,
    std::vector<std::optional<Phase4SameRunArmDecisionTelemetryV1>>* baseline_telemetry,
    std::vector<std::optional<Phase4SameRunArmDecisionTelemetryV1>>* candidate_telemetry) {
  const std::optional<Phase4RepresentativeCorpusAuthority> corpus =
      CorpusAuthority(execution_authority);
  if (!corpus.has_value()) {
    return Phase4TrialHarnessError{
        .invariant_id = "P4HARNESS-EXECUTION-AUTHORITY-001",
        .detail = "the private trial execution authority is invalid",
        .raw_cell = std::nullopt,
    };
  }
  if (execution_authority == Phase4TrialExecutionAuthority::kCorpusV2H4096) {
    if (std::optional<Phase4TrialHarnessError> error =
            PreflightConfirmatoryH4096Cell(execution_authority, wire_mode, cell, std::nullopt);
        error.has_value()) {
      return *error;
    }
  }
  auto initial_spec = BuildCanonicalSpecForAuthority(execution_authority, cell, 0,
                                                     Phase4TrialOrder::kBaselineFirst);
  if (std::holds_alternative<Phase4TrialHarnessError>(initial_spec)) {
    return std::get<Phase4TrialHarnessError>(std::move(initial_spec));
  }
  if (worker_executable.empty() || imported_fixture_path.empty()) {
    return Phase4TrialHarnessError{
        .invariant_id = "P4HARNESS-CELL-001",
        .detail = "cell, worker executable, or imported fixture path is invalid",
        .raw_cell = std::nullopt,
    };
  }
  const bool decision_mode = wire_mode == WorkerWireMode::kSameRunTelemetryV2;
  if (decision_mode && (baseline_telemetry == nullptr || candidate_telemetry == nullptr)) {
    return Phase4TrialHarnessError{
        .invariant_id = "P4HARNESS-SAME-RUN-SINK-001",
        .detail = "wire v2 requires both controller-owned telemetry sinks",
        .raw_cell = std::nullopt,
    };
  }
  if (decision_mode) {
    baseline_telemetry->assign(cell.repetitions, std::nullopt);
    candidate_telemetry->assign(cell.repetitions, std::nullopt);
  }
  std::string sigchld_detail;
  if (!ValidSigchldAuthority(&sigchld_detail)) {
    return Phase4TrialHarnessError{
        .invariant_id = "P4HARNESS-SIGCHLD-POLICY-001",
        .detail = std::move(sigchld_detail),
        .raw_cell = std::nullopt,
    };
  }
  int controller_pid_descriptor = OpenPidDescriptor(getpid());
  if (controller_pid_descriptor < 0) {
    return Phase4TrialHarnessError{
        .invariant_id = "P4HARNESS-PIDFD-AUTHORITY-001",
        .detail = "Linux pidfd authority is unavailable with errno " + Decimal(errno),
        .raw_cell = std::nullopt,
    };
  }
  CloseDescriptor(&controller_pid_descriptor);

  Phase4IsolatedCellResult result;
  result.carrier = decision_mode ? Phase4IsolatedCellCarrier::kSameRunWireV2
                                 : Phase4IsolatedCellCarrier::kRawWireV1;
  result.config = cell;
  result.environment = CaptureHostEnvironment();
  result.corpus_checksum = Phase4RepresentativeCorpusChecksumForAuthority(*corpus);
  result.cell_plan_checksum = CellPlanChecksum(execution_authority, cell);
  result.controller_identity = ControllerIdentity();
  result.authority_run_identity = RunIdentity(cell, result.controller_identity);
  InitializeAttempts(execution_authority, &result);
  std::vector<std::optional<ControllerArmExecution>> baseline_executions(cell.repetitions);
  std::vector<std::optional<ControllerArmExecution>> candidate_executions(cell.repetitions);

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
    LaunchResult launched =
        LaunchWorker(worker_executable, arm, execution_authority, cell, imported_fixture_path,
                     result.authority_run_identity, deadline, wire_mode);
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
    ControllerAwaitResult ready =
        AwaitControllerMessage(&**destination, start, deadline, wire_mode, execution_authority);
    if (ready.kind == AwaitKind::kMessage && ready.message.has_value() &&
        std::holds_alternative<internal::Phase4TrialWireReady>(*ready.message)) {
      const internal::Phase4TrialWireReady identity =
          std::get<internal::Phase4TrialWireReady>(*ready.message);
      if (!ReadyMatchesCell(execution_authority, identity, cell)) {
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
      const bool associated =
          failure.arm == arm && FailureMatchesCell(execution_authority, failure, cell, peer_ready);
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
    if (ready.kind == AwaitKind::kMessage) {
      ready.invariant_id = "P4HARNESS-READY-STATE-001";
      ready.detail = "worker emitted a valid but state-inappropriate setup response";
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
      if (!SendControllerMessage(worker->request_descriptor, command, wire_mode)) {
        ControllerAwaitResult failure;
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
      ControllerAwaitResult response = AwaitControllerMessage(
          worker, start,
          start + std::chrono::nanoseconds(cell.external_budget.maximum_cold_elapsed_nanoseconds),
          wire_mode, execution_authority);
      attempt->outer_elapsed_nanoseconds = response.elapsed_nanoseconds;
      if (response.kind == AwaitKind::kMessage && response.message.has_value() &&
          std::holds_alternative<ControllerWireSuccess>(*response.message)) {
        ControllerArmExecution execution =
            std::get<ControllerWireSuccess>(std::move(*response.message)).execution;
        bool fixed_authority_associated = true;
        if (execution_authority == Phase4TrialExecutionAuthority::kCorpusV2H4096) {
          Phase4CanonicalSpecResult expected = BuildCanonicalSpecForAuthority(
              execution_authority, cell, repetition, pair.execution_order);
          fixed_authority_associated =
              std::holds_alternative<Phase4PairedTrialSpec>(expected) &&
              DecodedH4096SuccessMatchesExpectedSpec(
                  wire_mode, std::get<Phase4PairedTrialSpec>(expected), execution);
        }
        if (!worker->ready_identity.has_value() ||
            !MatchesReadyIdentity(*worker->ready_identity, execution.execution.semantics) ||
            execution.execution.semantics.arm != arm ||
            execution.execution.semantics.case_id != cell.case_id ||
            execution.execution.semantics.requested_pool_size != cell.requested_pool_size ||
            execution.execution.semantics.repetition_index != repetition ||
            execution.execution.semantics.execution_order != pair.execution_order ||
            (wire_mode == WorkerWireMode::kSameRunTelemetryV2) !=
                execution.same_run_telemetry.has_value() ||
            (execution.same_run_telemetry.has_value() &&
             execution.same_run_telemetry->associated_semantic_checksum !=
                 execution.execution.semantics.semantic_checksum) ||
            !fixed_authority_associated) {
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
        if (response.kind == AwaitKind::kMessage) {
          response.invariant_id = "P4HARNESS-RUN-STATE-001";
          response.detail = "worker emitted a valid but state-inappropriate measured response";
        }
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
                           std::vector<std::optional<ControllerArmExecution>>* executions) {
      const Clock::time_point start = Clock::now();
      if (!SendControllerMessage(worker->request_descriptor, internal::Phase4TrialWireStop{},
                                 wire_mode)) {
        InvalidateWorkerSuccesses(
            &result, *worker, executions, Phase4IsolatedAttemptDisposition::kProtocolFailure,
            "P4HARNESS-STOP-WRITE-001", "failed to send the authenticated stop command", false);
        (void)KillWorker(worker);
        return;
      }
      ControllerAwaitResult stopped = AwaitControllerMessage(
          worker, start, start + std::chrono::nanoseconds(cell.maximum_setup_elapsed_nanoseconds),
          wire_mode, execution_authority);
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
      AttachUnavailableProcessAuthority(
          &result, *baseline, "P4HARNESS-REAP-BOUNDED-001",
          "exact wait4 authority remained unavailable after the bounded post-kill grace");
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
      AttachUnavailableProcessAuthority(
          &result, *candidate, "P4HARNESS-REAP-BOUNDED-001",
          "exact wait4 authority remained unavailable after the bounded post-kill grace");
    }
    ApplyProcessExit(&result, &*candidate, &candidate_executions);
  }

  for (std::uint32_t repetition = 0; repetition < cell.repetitions; ++repetition) {
    Phase4IsolatedPairAttempt& pair = result.attempts[repetition];
    std::optional<Phase4PairedTrialSpec> expected_spec;
    if (execution_authority == Phase4TrialExecutionAuthority::kCorpusV2H4096) {
      Phase4CanonicalSpecResult rebuilt = BuildCanonicalSpecForAuthority(
          execution_authority, cell, repetition, pair.execution_order);
      if (!std::holds_alternative<Phase4PairedTrialSpec>(rebuilt)) {
        for (Phase4IsolatedArmAttempt* attempt : {&pair.baseline, &pair.candidate}) {
          attempt->disposition = Phase4IsolatedAttemptDisposition::kProtocolFailure;
          attempt->controller_invariant_id = "P4HARNESS-H4096-EXPECTED-SPEC-001";
          attempt->controller_detail =
              "the controller could not rebuild the expected H=4096 canonical spec";
          attempt->record.reset();
        }
        baseline_executions[repetition].reset();
        candidate_executions[repetition].reset();
        continue;
      }
      expected_spec = std::get<Phase4PairedTrialSpec>(std::move(rebuilt));
    }
    auto finalize = [&](Phase4IsolatedArmAttempt* attempt,
                        std::optional<ControllerArmExecution>* execution,
                        std::optional<Phase4SameRunArmDecisionTelemetryV1>* telemetry) {
      if (attempt->disposition != Phase4IsolatedAttemptDisposition::kSuccess ||
          !execution->has_value()) {
        return;
      }
      const Phase4ExternalResourceObservation observation =
          MakeObservation(result, *attempt, (*execution)->execution);
      Phase4TrialArmRecordResult finalized = FinalizeArmForAuthority(
          execution_authority, wire_mode, expected_spec.has_value() ? &*expected_spec : nullptr,
          std::move((*execution)->execution), observation);
      if (std::holds_alternative<Phase4TrialArmRecord>(finalized)) {
        if (decision_mode) {
          if (telemetry == nullptr || !(*execution)->same_run_telemetry.has_value()) {
            attempt->disposition = Phase4IsolatedAttemptDisposition::kProtocolFailure;
            attempt->controller_invariant_id = "P4HARNESS-SAME-RUN-MISSING-001";
            attempt->controller_detail =
                "wire v2 success did not retain its same-run telemetry through finalization";
            execution->reset();
            return;
          }
          *telemetry = std::move((*execution)->same_run_telemetry);
        }
        execution->reset();
        attempt->record = std::get<Phase4TrialArmRecord>(std::move(finalized));
        return;
      }
      execution->reset();
      const Phase4PairedTrialError& error = std::get<Phase4PairedTrialError>(finalized);
      attempt->disposition = error.code == Phase4PairedTrialErrorCode::kExternalBudgetExceeded
                                 ? Phase4IsolatedAttemptDisposition::kExternalBudgetExceeded
                                 : Phase4IsolatedAttemptDisposition::kProtocolFailure;
      attempt->controller_invariant_id = std::string(error.invariant_id);
      attempt->controller_detail = std::string(error.detail);
    };
    finalize(&pair.baseline, &baseline_executions[repetition],
             decision_mode ? &(*baseline_telemetry)[repetition] : nullptr);
    finalize(&pair.candidate, &candidate_executions[repetition],
             decision_mode ? &(*candidate_telemetry)[repetition] : nullptr);
    if (pair.baseline.record.has_value() && pair.candidate.record.has_value()) {
      Phase4PairedTrialAssemblyResult assembled = AssemblePairForAuthority(
          execution_authority, wire_mode, expected_spec.has_value() ? &*expected_spec : nullptr,
          std::move(*pair.baseline.record), std::move(*pair.candidate.record));
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

}  // namespace

Phase4IsolatedCellExecution RunPhase4IsolatedCellV1(const Phase4CanonicalCellConfig& cell,
                                                    std::string_view worker_executable,
                                                    std::string_view imported_fixture_path) {
  return RunPhase4IsolatedCellImpl(Phase4TrialExecutionAuthority::kCorpusV1, cell,
                                   worker_executable, imported_fixture_path, WorkerWireMode::kRawV1,
                                   nullptr, nullptr);
}

Phase4IsolatedCellExecution RunPhase4IsolatedCellForCorpusV2(
    const Phase4CanonicalCellConfig& cell, std::string_view worker_executable,
    std::string_view imported_fixture_path) {
  return RunPhase4IsolatedCellImpl(Phase4TrialExecutionAuthority::kCorpusV2H2250, cell,
                                   worker_executable, imported_fixture_path, WorkerWireMode::kRawV1,
                                   nullptr, nullptr);
}

[[nodiscard]] static Phase4IsolatedCellWithSameRunDecisionTelemetryExecutionV1
RunPhase4IsolatedCellWithSameRunDecisionTelemetryForAuthority(
    Phase4TrialExecutionAuthority execution_authority, const Phase4CanonicalCellConfig& cell,
    std::string_view worker_executable, std::string_view imported_fixture_path) {
  std::vector<std::optional<Phase4SameRunArmDecisionTelemetryV1>> baseline_telemetry;
  std::vector<std::optional<Phase4SameRunArmDecisionTelemetryV1>> candidate_telemetry;
  Phase4IsolatedCellExecution executed = RunPhase4IsolatedCellImpl(
      execution_authority, cell, worker_executable, imported_fixture_path,
      WorkerWireMode::kSameRunTelemetryV2, &baseline_telemetry, &candidate_telemetry);
  if (std::holds_alternative<Phase4TrialHarnessError>(executed)) {
    return std::get<Phase4TrialHarnessError>(std::move(executed));
  }

  Phase4IsolatedCellWithSameRunDecisionTelemetryV1 capture;
  capture.raw_cell = std::get<Phase4IsolatedCellResult>(std::move(executed));
  if (capture.raw_cell.config.repetitions != kPhase4CanonicalRepetitionsV1 ||
      capture.raw_cell.attempts.size() != kPhase4CanonicalRepetitionsV1 ||
      baseline_telemetry.size() != kPhase4CanonicalRepetitionsV1 ||
      candidate_telemetry.size() != kPhase4CanonicalRepetitionsV1) {
    return Phase4TrialHarnessError{
        .invariant_id = "P4HARNESS-SAME-RUN-CARDINALITY-001",
        .detail =
            "wire v2 did not retain exactly twenty paired attempts and forty telemetry leaves",
        .raw_cell = std::move(capture.raw_cell),
    };
  }

  capture.same_run_attempts.reserve(kPhase4CanonicalRepetitionsV1);
  for (std::uint32_t repetition = 0; repetition < kPhase4CanonicalRepetitionsV1; ++repetition) {
    const Phase4IsolatedPairAttempt& raw_pair = capture.raw_cell.attempts[repetition];
    if (raw_pair.repetition_index != repetition || !raw_pair.result.has_value() ||
        raw_pair.baseline.disposition != Phase4IsolatedAttemptDisposition::kSuccess ||
        raw_pair.candidate.disposition != Phase4IsolatedAttemptDisposition::kSuccess ||
        !raw_pair.baseline.record.has_value() || !raw_pair.candidate.record.has_value() ||
        !baseline_telemetry[repetition].has_value() ||
        !candidate_telemetry[repetition].has_value()) {
      return Phase4TrialHarnessError{
          .invariant_id = "P4HARNESS-SAME-RUN-COMPLETE-001",
          .detail =
              "wire v2 evidence is unavailable because at least one arm did not survive "
              "successful process teardown and finalization",
          .raw_cell = std::move(capture.raw_cell),
      };
    }

    const auto build_arm = [&](const Phase4IsolatedArmAttempt& raw_arm,
                               Phase4SameRunArmDecisionTelemetryV1 telemetry) {
      const Phase4TrialArmRecord& record = *raw_arm.record;
      Phase4IsolatedSameRunArmDecisionTelemetryV1 arm_capture{
          .arm = raw_arm.arm,
          .repetition_index = raw_arm.repetition_index,
          .execution_order = raw_arm.execution_order,
          .dispatch_ordinal = raw_arm.dispatch_ordinal,
          .process_instance_identity = raw_arm.process_instance_identity,
          .associated_semantic_checksum = record.semantics.semantic_checksum,
          .associated_arm_artifact_checksum = record.artifact_checksum,
          .associated_authority_checksum = record.external_observation.authority_checksum,
          .associated_arm_attempt_checksum = raw_arm.attempt_checksum,
          .telemetry = std::move(telemetry),
      };
      arm_capture.capture_checksum = ComputePhase4IsolatedSameRunArmCaptureChecksumV1(arm_capture);
      return arm_capture;
    };

    Phase4IsolatedSameRunPairDecisionTelemetryV1 pair_capture{
        .case_id = raw_pair.case_id,
        .requested_pool_size = raw_pair.requested_pool_size,
        .repetition_index = raw_pair.repetition_index,
        .root_seed = raw_pair.root_seed,
        .execution_order = raw_pair.execution_order,
        .associated_raw_pair_attempt_checksum = raw_pair.attempt_checksum,
        .associated_paired_semantic_checksum = raw_pair.result->semantic_checksum,
        .associated_paired_artifact_checksum = raw_pair.result->artifact_checksum,
        .baseline = build_arm(raw_pair.baseline, std::move(*baseline_telemetry[repetition])),
        .candidate = build_arm(raw_pair.candidate, std::move(*candidate_telemetry[repetition])),
    };

    const auto valid_arm = [](const Phase4IsolatedArmAttempt& raw_arm,
                              const Phase4IsolatedSameRunArmDecisionTelemetryV1& arm_capture) {
      const Phase4TrialArmRecord& record = *raw_arm.record;
      return arm_capture.schema_version == kPhase4IsolatedSameRunTelemetrySchemaVersion &&
             arm_capture.arm == raw_arm.arm &&
             arm_capture.repetition_index == raw_arm.repetition_index &&
             arm_capture.execution_order == raw_arm.execution_order &&
             arm_capture.dispatch_ordinal == raw_arm.dispatch_ordinal &&
             arm_capture.dispatch_ordinal != 0 &&
             arm_capture.process_instance_identity == raw_arm.process_instance_identity &&
             arm_capture.process_instance_identity != 0 &&
             arm_capture.associated_semantic_checksum == record.semantics.semantic_checksum &&
             arm_capture.associated_arm_artifact_checksum == record.artifact_checksum &&
             arm_capture.associated_authority_checksum ==
                 record.external_observation.authority_checksum &&
             arm_capture.associated_arm_attempt_checksum == raw_arm.attempt_checksum &&
             arm_capture.telemetry.associated_semantic_checksum ==
                 record.semantics.semantic_checksum &&
             arm_capture.telemetry.outcome == record.semantics.outcome &&
             arm_capture.telemetry.telemetry_checksum ==
                 internal::ComputePhase4SameRunArmDecisionTelemetryChecksumV1(
                     arm_capture.telemetry) &&
             arm_capture.capture_checksum ==
                 ComputePhase4IsolatedSameRunArmCaptureChecksumV1(arm_capture);
    };
    if (!valid_arm(raw_pair.baseline, pair_capture.baseline) ||
        !valid_arm(raw_pair.candidate, pair_capture.candidate)) {
      return Phase4TrialHarnessError{
          .invariant_id = "P4HARNESS-SAME-RUN-ASSOCIATION-001",
          .detail = "same-run telemetry did not authenticate against its exact finalized arm",
          .raw_cell = std::move(capture.raw_cell),
      };
    }
    pair_capture.capture_checksum = ComputePhase4IsolatedSameRunPairCaptureChecksumV1(pair_capture);
    capture.same_run_attempts.push_back(std::move(pair_capture));
  }
  return capture;
}

Phase4IsolatedCellWithSameRunDecisionTelemetryExecutionV1
RunPhase4IsolatedCellWithSameRunDecisionTelemetryV1(const Phase4CanonicalCellConfig& cell,
                                                    std::string_view worker_executable,
                                                    std::string_view imported_fixture_path) {
  return RunPhase4IsolatedCellWithSameRunDecisionTelemetryForAuthority(
      Phase4TrialExecutionAuthority::kCorpusV1, cell, worker_executable, imported_fixture_path);
}

Phase4IsolatedCellWithSameRunDecisionTelemetryExecutionV1
RunPhase4IsolatedCellWithSameRunDecisionTelemetryForCorpusV2(
    const Phase4CanonicalCellConfig& cell, std::string_view worker_executable,
    std::string_view imported_fixture_path) {
  return RunPhase4IsolatedCellWithSameRunDecisionTelemetryForAuthority(
      Phase4TrialExecutionAuthority::kCorpusV2H2250, cell, worker_executable,
      imported_fixture_path);
}

namespace {

[[nodiscard]] int RunPhase4TrialWorkerImpl(Phase4TrialArm arm,
                                           Phase4TrialExecutionAuthority execution_authority,
                                           const Phase4CanonicalCellConfig& cell,
                                           std::string_view imported_fixture,
                                           int request_descriptor, int response_descriptor,
                                           WorkerWireMode wire_mode) noexcept {
  try {
    if (execution_authority == Phase4TrialExecutionAuthority::kCorpusV2H4096) {
      if (std::optional<Phase4TrialHarnessError> error =
              PreflightConfirmatoryH4096Cell(execution_authority, wire_mode, cell, arm);
          error.has_value()) {
        const Phase4DurableArmFailure failure = SummaryFailure(
            execution_authority, arm, Phase4PairedTrialErrorCode::kInvalidConfiguration,
            error->invariant_id, error->detail);
        (void)SendControllerMessage(response_descriptor, internal::Phase4TrialWireFailure{failure},
                                    wire_mode);
        return 10;
      }
    }
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
          execution_authority, arm, Phase4PairedTrialErrorCode::kExternalAuthorityUnavailable,
          "P4HARNESS-WORKER-RLIMIT-001", "worker did not observe the exact RLIMIT_AS contract",
          cell.external_budget.maximum_address_space_bytes,
          static_cast<std::uint64_t>(address_space_limit.rlim_cur));
      (void)SendControllerMessage(response_descriptor, internal::Phase4TrialWireFailure{failure},
                                  wire_mode);
      return 10;
    }
    std::unique_ptr<allocator::PersistentCpuCandidatePoolPreparer> preparer;
    if (arm == Phase4TrialArm::kReusableCandidateAllocation) {
      const allocator::PersistentCpuCandidatePoolPreparerConfig preparer_config{
          .worker_count = cell.preparation_worker_count,
      };
      allocator::PersistentCpuCandidatePoolPreparerResult created = [&] {
#if defined(APGAR_PHASE4_TRIAL_FAULT_TEST_VARIANT)
        if (fault == "preparer_factory_resource_failure" ||
            fault == "preparer_factory_resource_failure_reap_unavailable") {
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
        const auto failure =
            SummaryFailure(execution_authority, arm, summary_code, error.invariant_id, error.detail,
                           error.required, error.configured);
        (void)SendControllerMessage(response_descriptor, internal::Phase4TrialWireFailure{failure},
                                    wire_mode);
        return 10;
      }
      preparer = std::get<std::unique_ptr<allocator::PersistentCpuCandidatePoolPreparer>>(
          std::move(created));
    }
    auto warmup_spec = BuildCanonicalSpecForAuthority(execution_authority, cell, 0,
                                                      Phase4TrialOrder::kBaselineFirst);
    if (std::holds_alternative<Phase4TrialHarnessError>(warmup_spec)) {
      const auto& error = std::get<Phase4TrialHarnessError>(warmup_spec);
      const auto failure = SummaryFailure(execution_authority, arm,
                                          Phase4PairedTrialErrorCode::kInvalidConfiguration,
                                          error.invariant_id, error.detail);
      (void)SendControllerMessage(response_descriptor, internal::Phase4TrialWireFailure{failure},
                                  wire_mode);
      return 10;
    }
    Phase4TrialArmExecutionResult warmup = [&]() -> Phase4TrialArmExecutionResult {
      const Phase4PairedTrialSpec& spec = std::get<Phase4PairedTrialSpec>(warmup_spec);
      if (execution_authority == Phase4TrialExecutionAuthority::kCorpusV2H4096 &&
          wire_mode == WorkerWireMode::kSameRunTelemetryV2) {
        Phase4TrialArmWithSameRunTelemetryExecutionResultV1 decision =
            ExecuteSameRunArmForAuthority(execution_authority, arm, spec, imported_fixture,
                                          preparer.get());
        if (std::holds_alternative<Phase4TrialArmFailure>(decision)) {
          return std::get<Phase4TrialArmFailure>(std::move(decision));
        }
        return std::get<Phase4TrialArmWithSameRunTelemetryExecutionV1>(std::move(decision))
            .execution;
      }
      return ExecuteArmForAuthority(execution_authority, arm, spec, imported_fixture,
                                    preparer.get());
    }();
    if (std::holds_alternative<Phase4TrialArmFailure>(warmup)) {
      const auto failure = ReconcileFailureForAuthority(
          execution_authority, std::get<Phase4TrialArmFailure>(std::move(warmup)));
      (void)SendControllerMessage(response_descriptor, internal::Phase4TrialWireFailure{failure},
                                  wire_mode);
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
      (void)SendControllerMessage(response_descriptor,
                                  internal::Phase4TrialWireFailure{std::move(failure)}, wire_mode);
      return 10;
    }
    if (fault == "ready_bad_identity" && arm == Phase4TrialArm::kReusableCandidateAllocation) {
      ++ready.case_checksum;
    }
    if (fault == "double_ready") {
      std::optional<std::vector<std::uint8_t>> encoded;
      if (wire_mode == WorkerWireMode::kRawV1) {
        auto raw = internal::EncodePhase4TrialWireMessageV1(ready);
        if (std::holds_alternative<std::vector<std::uint8_t>>(raw)) {
          encoded = std::get<std::vector<std::uint8_t>>(std::move(raw));
        }
      } else {
        auto decision = internal::EncodePhase4TrialWireMessageV2(ready);
        if (std::holds_alternative<std::vector<std::uint8_t>>(decision)) {
          encoded = std::get<std::vector<std::uint8_t>>(std::move(decision));
        }
      }
      if (encoded.has_value()) {
        std::vector<std::uint8_t> doubled = std::move(*encoded);
        const std::vector<std::uint8_t> second = doubled;
        doubled.insert(doubled.end(), second.begin(), second.end());
        (void)send(response_descriptor, doubled.data(), doubled.size(), MSG_NOSIGNAL);
      }
      for (;;) {
        (void)pause();
      }
    }
#endif
    if (!SendControllerMessage(response_descriptor, ready, wire_mode)) {
      return 11;
    }
#if defined(APGAR_PHASE4_TRIAL_FAULT_TEST_VARIANT)
    if (fault == "delayed_double_ready") {
      (void)poll(nullptr, 0, 100);
      if (!SendControllerMessage(response_descriptor, ready, wire_mode)) {
        return 11;
      }
    }
#endif
    for (;;) {
      ControllerWireReadResult request = ReadControllerMessage(request_descriptor, wire_mode);
      if (std::holds_alternative<internal::Phase4TrialWireError>(request)) {
        return 11;
      }
      ControllerWireMessage message = std::get<ControllerWireMessage>(std::move(request));
      if (std::holds_alternative<internal::Phase4TrialWireStop>(message)) {
        const bool acknowledged = SendControllerMessage(
            response_descriptor, internal::Phase4TrialWireStopped{}, wire_mode);
#if defined(APGAR_PHASE4_TRIAL_FAULT_TEST_VARIANT)
        if (acknowledged && fault == "teardown_hang") {
          for (;;) {
            (void)pause();
          }
        }
        if (acknowledged && fault == "delayed_extra_frame") {
          (void)poll(nullptr, 0, 500);
          (void)SendControllerMessage(response_descriptor, ready, wire_mode);
        }
#endif
        return acknowledged ? 0 : 11;
      }
      if (!std::holds_alternative<internal::Phase4TrialWireRunCommand>(message)) {
        return 11;
      }
      const auto command = std::get<internal::Phase4TrialWireRunCommand>(message);
      auto spec = BuildCanonicalSpecForAuthority(execution_authority, cell,
                                                 command.repetition_index, command.execution_order);
      if (std::holds_alternative<Phase4TrialHarnessError>(spec)) {
        const auto& error = std::get<Phase4TrialHarnessError>(spec);
        const auto failure = SummaryFailure(execution_authority, arm,
                                            Phase4PairedTrialErrorCode::kInvalidConfiguration,
                                            error.invariant_id, error.detail);
        (void)SendControllerMessage(response_descriptor, internal::Phase4TrialWireFailure{failure},
                                    wire_mode);
        return 10;
      }
      std::variant<ControllerArmExecution, Phase4TrialArmFailure> execution = [&] {
        if (wire_mode == WorkerWireMode::kRawV1) {
          Phase4TrialArmExecutionResult raw = ExecuteArmForAuthority(
              execution_authority, arm, std::get<Phase4PairedTrialSpec>(spec), imported_fixture,
              preparer.get());
          if (std::holds_alternative<Phase4TrialArmFailure>(raw)) {
            return std::variant<ControllerArmExecution, Phase4TrialArmFailure>{
                std::get<Phase4TrialArmFailure>(std::move(raw))};
          }
          return std::variant<ControllerArmExecution, Phase4TrialArmFailure>{ControllerArmExecution{
              .execution = std::get<Phase4TrialArmExecution>(std::move(raw)),
              .same_run_telemetry = std::nullopt,
          }};
        }
        Phase4TrialArmWithSameRunTelemetryExecutionResultV1 decision =
            ExecuteSameRunArmForAuthority(execution_authority, arm,
                                          std::get<Phase4PairedTrialSpec>(spec), imported_fixture,
                                          preparer.get());
        if (std::holds_alternative<Phase4TrialArmFailure>(decision)) {
          return std::variant<ControllerArmExecution, Phase4TrialArmFailure>{
              std::get<Phase4TrialArmFailure>(std::move(decision))};
        }
        Phase4TrialArmWithSameRunTelemetryExecutionV1 successful =
            std::get<Phase4TrialArmWithSameRunTelemetryExecutionV1>(std::move(decision));
        return std::variant<ControllerArmExecution, Phase4TrialArmFailure>{ControllerArmExecution{
            .execution = std::move(successful.execution),
            .same_run_telemetry = std::move(successful.telemetry),
        }};
      }();
      if (std::holds_alternative<Phase4TrialArmFailure>(execution)) {
        const auto failure = ReconcileFailureForAuthority(
            execution_authority, std::get<Phase4TrialArmFailure>(std::move(execution)));
        (void)SendControllerMessage(response_descriptor, internal::Phase4TrialWireFailure{failure},
                                    wire_mode);
        return 10;
      }
      ControllerArmExecution successful_execution =
          std::get<ControllerArmExecution>(std::move(execution));
#if defined(APGAR_PHASE4_TRIAL_FAULT_TEST_VARIANT)
      const bool bad_second_association =
          fault == "second_run_bad_association" && command.repetition_index == 1;
      if (bad_second_association) {
        successful_execution.execution.semantics.repetition_index = 0;
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
        (void)SendControllerMessage(
            response_descriptor, internal::Phase4TrialWireFailure{std::move(failure)}, wire_mode);
        return 10;
      }
#endif
      if (!SendControllerSuccess(response_descriptor, std::move(successful_execution), wire_mode,
                                 execution_authority)) {
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
    const auto failure =
        SummaryFailure(execution_authority, arm, Phase4PairedTrialErrorCode::kInternalInvariant,
                       "P4HARNESS-WORKER-EXCEPTION-001",
                       "unexpected exception escaped the isolated worker controller");
    (void)SendControllerMessage(response_descriptor, internal::Phase4TrialWireFailure{failure},
                                wire_mode);
    return 12;
  }
}

}  // namespace

int RunPhase4TrialWorkerV1(Phase4TrialArm arm, const Phase4CanonicalCellConfig& cell,
                           std::string_view imported_fixture, int request_descriptor,
                           int response_descriptor) noexcept {
  return RunPhase4TrialWorkerImpl(arm, Phase4TrialExecutionAuthority::kCorpusV1, cell,
                                  imported_fixture, request_descriptor, response_descriptor,
                                  WorkerWireMode::kRawV1);
}

int RunPhase4TrialWorkerForCorpusV2(Phase4TrialArm arm, const Phase4CanonicalCellConfig& cell,
                                    std::string_view imported_fixture, int request_descriptor,
                                    int response_descriptor) noexcept {
  return RunPhase4TrialWorkerImpl(arm, Phase4TrialExecutionAuthority::kCorpusV2H2250, cell,
                                  imported_fixture, request_descriptor, response_descriptor,
                                  WorkerWireMode::kRawV1);
}

int RunPhase4TrialWorkerWithSameRunTelemetryV1(Phase4TrialArm arm,
                                               const Phase4CanonicalCellConfig& cell,
                                               std::string_view imported_fixture,
                                               int request_descriptor,
                                               int response_descriptor) noexcept {
  return RunPhase4TrialWorkerImpl(arm, Phase4TrialExecutionAuthority::kCorpusV1, cell,
                                  imported_fixture, request_descriptor, response_descriptor,
                                  WorkerWireMode::kSameRunTelemetryV2);
}

int RunPhase4TrialWorkerWithSameRunTelemetryForCorpusV2(Phase4TrialArm arm,
                                                        const Phase4CanonicalCellConfig& cell,
                                                        std::string_view imported_fixture,
                                                        int request_descriptor,
                                                        int response_descriptor) noexcept {
  return RunPhase4TrialWorkerImpl(arm, Phase4TrialExecutionAuthority::kCorpusV2H2250, cell,
                                  imported_fixture, request_descriptor, response_descriptor,
                                  WorkerWireMode::kSameRunTelemetryV2);
}

namespace {

[[nodiscard]] bool IsCleanSourceCommit(std::string_view source_commit, bool source_stamped,
                                       bool source_tree_dirty) noexcept {
  if (!source_stamped || source_tree_dirty || source_commit.size() != 40) {
    return false;
  }
  return std::all_of(source_commit.begin(), source_commit.end(), [](char character) {
    return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f');
  });
}

[[nodiscard]] bool H4096RecordMatchesExpectedSpec(WorkerWireMode wire_mode,
                                                  const Phase4IsolatedCellResult& cell,
                                                  const Phase4PairedTrialSpec& spec,
                                                  const Phase4IsolatedArmAttempt& attempt,
                                                  const Phase4TrialArmRecord& record) {
  const Phase4TrialArmSemantics& semantics = record.semantics;
  const Phase4ExternalResourceObservation& observation = record.external_observation;
  const bool candidate = semantics.arm == Phase4TrialArm::kReusableCandidateAllocation;
  const Phase4PreparerLifecycleObservation expected_lifecycle =
      candidate ? Phase4PreparerLifecycleObservation{
                      .workers_started_before = spec.preparation_worker_count,
                      .workers_started_after = spec.preparation_worker_count,
                      .invocations_started_before =
                          static_cast<std::uint64_t>(spec.repetition_index) + 1U,
                      .invocations_started_after =
                          static_cast<std::uint64_t>(spec.repetition_index) + 2U,
                      .invocations_completed_before =
                          static_cast<std::uint64_t>(spec.repetition_index) + 1U,
                      .invocations_completed_after =
                          static_cast<std::uint64_t>(spec.repetition_index) + 2U,
                  }
                : Phase4PreparerLifecycleObservation{};
  const bool nested_intervals =
      record.case_build_elapsed_nanoseconds <=
          std::numeric_limits<std::uint64_t>::max() - record.prepared_elapsed_nanoseconds &&
      record.case_build_elapsed_nanoseconds + record.prepared_elapsed_nanoseconds <=
          record.cold_elapsed_nanoseconds &&
      observation.outer_elapsed_nanoseconds >= record.cold_elapsed_nanoseconds;
  return H4096SemanticsMatchExpectedSpec(wire_mode, spec, semantics) &&
         semantics.arm == attempt.arm && record.preparer_lifecycle == expected_lifecycle &&
         record.case_build_elapsed_nanoseconds + record.prepared_elapsed_nanoseconds >=
             record.case_build_elapsed_nanoseconds &&
         record.prepared_elapsed_nanoseconds <=
             spec.external_budget.maximum_prepared_elapsed_nanoseconds &&
         record.cold_elapsed_nanoseconds <= spec.external_budget.maximum_cold_elapsed_nanoseconds &&
         observation.schema_version == kPhase4ExternalAuthoritySchemaVersion &&
         observation.authority_kind ==
             Phase4ExternalAuthorityKind::kLinuxParentWatchdogRlimitAndWait4 &&
         observation.authority_run_identity == cell.authority_run_identity &&
         observation.controller_identity == cell.controller_identity &&
         observation.process_instance_identity == attempt.process_instance_identity &&
         observation.associated_semantic_checksum == semantics.semantic_checksum &&
         observation.configured_wall_limit_nanoseconds ==
             spec.external_budget.maximum_cold_elapsed_nanoseconds &&
         observation.configured_address_space_limit_bytes ==
             spec.external_budget.maximum_address_space_bytes &&
         observation.configured_peak_host_limit_bytes ==
             spec.external_budget.maximum_peak_host_bytes &&
         observation.outer_elapsed_nanoseconds == attempt.outer_elapsed_nanoseconds &&
         observation.peak_host_bytes == attempt.process_lifetime_peak_host_bytes &&
         observation.peak_host_bytes != 0 &&
         observation.peak_host_bytes <= spec.external_budget.maximum_peak_host_bytes &&
         observation.process_exit_code == 0 && observation.isolated_process &&
         observation.wall_authority_enforced && observation.memory_authority_enforced &&
         observation.persistent_preparer_reused == candidate &&
         observation.preparer_lifecycle == expected_lifecycle &&
         observation.authority_checksum != 0 &&
         observation.authority_checksum ==
             internal::ComputePhase4ExternalAuthorityChecksumV1(observation) &&
         nested_intervals && record.artifact_checksum != 0 &&
         record.artifact_checksum == internal::ComputePhase4TrialArmArtifactChecksumV1(record);
}

[[nodiscard]] bool H4096ArmAttemptMatchesExpectedSpec(WorkerWireMode wire_mode,
                                                      const Phase4IsolatedCellResult& cell,
                                                      const Phase4PairedTrialSpec& spec,
                                                      const Phase4IsolatedArmAttempt& attempt,
                                                      Phase4TrialArm expected_arm) {
  if (attempt.schema_version != kPhase4TrialHarnessSchemaVersion || attempt.arm != expected_arm ||
      attempt.repetition_index != spec.repetition_index ||
      attempt.execution_order != spec.execution_order || attempt.attempt_checksum == 0 ||
      attempt.attempt_checksum != ArmAttemptChecksum(attempt)) {
    return false;
  }
  if (attempt.record.has_value()) {
    return attempt.disposition == Phase4IsolatedAttemptDisposition::kSuccess &&
           attempt.process_instance_identity != 0 &&
           attempt.process_lifetime_peak_host_bytes != 0 && attempt.raw_wait_status == 0 &&
           attempt.process_exit_code == 0 && attempt.terminating_signal == 0 &&
           !attempt.watchdog_kill_sent && attempt.controller_invariant_id.empty() &&
           attempt.controller_detail.empty() && !attempt.child_failure.has_value() &&
           H4096RecordMatchesExpectedSpec(wire_mode, cell, spec, attempt, *attempt.record);
  }
  if (attempt.disposition == Phase4IsolatedAttemptDisposition::kSuccess) {
    return false;
  }
  if (!attempt.child_failure.has_value()) {
    return true;
  }
  return attempt.disposition == Phase4IsolatedAttemptDisposition::kTypedChildFailure &&
         attempt.child_failure->arm == expected_arm &&
         attempt.child_failure->payload_checksum != 0 &&
         attempt.child_failure->payload_checksum ==
             ComputePhase4DurableArmFailureChecksumV1(*attempt.child_failure);
}

[[nodiscard]] bool AuthenticatePhase4ConfirmatoryH4096RawCell(
    const Phase4IsolatedCellResult& result, WorkerWireMode wire_mode) {
  constexpr Phase4TrialExecutionAuthority kAuthority =
      Phase4TrialExecutionAuthority::kCorpusV2H4096;
  if (PreflightConfirmatoryH4096Cell(kAuthority, wire_mode, result.config, std::nullopt)
          .has_value()) {
    return false;
  }
  const Phase4IsolatedCellCarrier expected_carrier =
      wire_mode == WorkerWireMode::kRawV1 ? Phase4IsolatedCellCarrier::kRawWireV1
                                          : Phase4IsolatedCellCarrier::kSameRunWireV2;
  const std::uint64_t expected_artifact_checksum =
      wire_mode == WorkerWireMode::kRawV1
          ? ComputePhase4IsolatedCellArtifactChecksumV1(result)
          : ComputePhase4SameRunIsolatedCellArtifactChecksumV2(result);
  if (result.schema_version != kPhase4TrialHarnessSchemaVersion ||
      result.carrier != expected_carrier ||
      result.corpus_checksum != Phase4RepresentativeCorpusChecksumForAuthority(
                                    Phase4RepresentativeCorpusAuthority::kV2) ||
      result.cell_plan_checksum != CellPlanChecksum(kAuthority, result.config) ||
      result.environment.environment_checksum == 0 ||
      result.environment.environment_checksum != HostEnvironmentChecksum(result.environment) ||
      result.controller_identity == 0 || result.authority_run_identity == 0 ||
      result.authority_run_identity != RunIdentity(result.config, result.controller_identity) ||
      result.attempts.size() != kPhase4CanonicalRepetitionsV1 || result.artifact_checksum == 0 ||
      result.artifact_checksum != expected_artifact_checksum) {
    return false;
  }
  bool has_authenticated_success = false;
  for (std::uint32_t repetition = 0; repetition < kPhase4CanonicalRepetitionsV1; ++repetition) {
    const Phase4TrialOrder order = repetition % 2U == 0U ? Phase4TrialOrder::kBaselineFirst
                                                         : Phase4TrialOrder::kCandidateFirst;
    Phase4CanonicalSpecResult rebuilt =
        BuildCanonicalSpecForAuthority(kAuthority, result.config, repetition, order);
    if (!std::holds_alternative<Phase4PairedTrialSpec>(rebuilt)) {
      return false;
    }
    const Phase4PairedTrialSpec& spec = std::get<Phase4PairedTrialSpec>(rebuilt);
    const Phase4IsolatedPairAttempt& pair = result.attempts[repetition];
    const bool baseline_matches = H4096ArmAttemptMatchesExpectedSpec(
        wire_mode, result, spec, pair.baseline, Phase4TrialArm::kSequentialBaseline);
    const bool candidate_matches = H4096ArmAttemptMatchesExpectedSpec(
        wire_mode, result, spec, pair.candidate, Phase4TrialArm::kReusableCandidateAllocation);
    if (pair.schema_version != kPhase4TrialHarnessSchemaVersion || pair.case_id != spec.case_id ||
        pair.requested_pool_size != spec.requested_pool_size ||
        pair.repetition_index != repetition || pair.root_seed != spec.root_seed ||
        pair.execution_order != order || !baseline_matches || !candidate_matches ||
        pair.attempt_checksum == 0 || pair.attempt_checksum != PairAttemptChecksum(pair)) {
      return false;
    }
    has_authenticated_success = has_authenticated_success || pair.baseline.record.has_value() ||
                                pair.candidate.record.has_value();
    if (!pair.result.has_value()) {
      if (pair.baseline.record.has_value() && pair.candidate.record.has_value()) {
        return false;
      }
      continue;
    }
    if (!pair.baseline.record.has_value() || !pair.candidate.record.has_value() ||
        pair.result->baseline != *pair.baseline.record ||
        pair.result->candidate != *pair.candidate.record) {
      return false;
    }
    Phase4PairedTrialAssemblyResult assembled = AssemblePairForAuthority(
        kAuthority, wire_mode, &spec, *pair.baseline.record, *pair.candidate.record);
    if (!std::holds_alternative<Phase4PairedTrialResult>(assembled) ||
        std::get<Phase4PairedTrialResult>(assembled) != *pair.result) {
      return false;
    }
  }
  return has_authenticated_success;
}

}  // namespace

namespace internal {

std::optional<Phase4TrialHarnessError> PreflightPhase4ConfirmatoryH4096OrdinaryCell(
    const Phase4CanonicalCellConfig& cell) {
  return PreflightConfirmatoryH4096Cell(Phase4TrialExecutionAuthority::kCorpusV2H4096,
                                        WorkerWireMode::kRawV1, cell, std::nullopt);
}

std::optional<Phase4TrialHarnessError> PreflightPhase4ConfirmatoryH4096SameRunCell(
    const Phase4CanonicalCellConfig& cell) {
  return PreflightConfirmatoryH4096Cell(Phase4TrialExecutionAuthority::kCorpusV2H4096,
                                        WorkerWireMode::kSameRunTelemetryV2, cell, std::nullopt);
}

Phase4IsolatedCellExecution RunPhase4ConfirmatoryH4096OrdinaryCell(
    const Phase4CanonicalCellConfig& cell, std::string_view worker_executable,
    std::string_view imported_fixture_path) {
  return RunPhase4IsolatedCellImpl(Phase4TrialExecutionAuthority::kCorpusV2H4096, cell,
                                   worker_executable, imported_fixture_path, WorkerWireMode::kRawV1,
                                   nullptr, nullptr);
}

Phase4IsolatedCellWithSameRunDecisionTelemetryExecutionV1 RunPhase4ConfirmatoryH4096SameRunCell(
    const Phase4CanonicalCellConfig& cell, std::string_view worker_executable,
    std::string_view imported_fixture_path) {
  return RunPhase4IsolatedCellWithSameRunDecisionTelemetryForAuthority(
      Phase4TrialExecutionAuthority::kCorpusV2H4096, cell, worker_executable,
      imported_fixture_path);
}

int RunPhase4ConfirmatoryH4096OrdinaryWorker(Phase4TrialArm arm,
                                             const Phase4CanonicalCellConfig& cell,
                                             std::string_view imported_fixture,
                                             int request_descriptor,
                                             int response_descriptor) noexcept {
  return RunPhase4TrialWorkerImpl(arm, Phase4TrialExecutionAuthority::kCorpusV2H4096, cell,
                                  imported_fixture, request_descriptor, response_descriptor,
                                  WorkerWireMode::kRawV1);
}

int RunPhase4ConfirmatoryH4096SameRunWorker(Phase4TrialArm arm,
                                            const Phase4CanonicalCellConfig& cell,
                                            std::string_view imported_fixture,
                                            int request_descriptor,
                                            int response_descriptor) noexcept {
  return RunPhase4TrialWorkerImpl(arm, Phase4TrialExecutionAuthority::kCorpusV2H4096, cell,
                                  imported_fixture, request_descriptor, response_descriptor,
                                  WorkerWireMode::kSameRunTelemetryV2);
}

bool ValidatePhase4ConfirmatoryH4096SameRunCellCapture(
    const Phase4IsolatedCellWithSameRunDecisionTelemetryV1& capture,
    std::string_view imported_fixture) {
  return ValidatePhase4IsolatedSameRunCellCaptureForAuthority(
      Phase4TrialExecutionAuthority::kCorpusV2H4096, capture, imported_fixture);
}

std::optional<std::string> SerializePhase4ConfirmatoryH4096OrdinaryCellJsonV1(
    const Phase4IsolatedCellResult& result, std::string_view source_commit, bool source_stamped,
    bool source_tree_dirty) {
  if (!IsCleanSourceCommit(source_commit, source_stamped, source_tree_dirty) ||
      !AuthenticatePhase4ConfirmatoryH4096RawCell(result, WorkerWireMode::kRawV1)) {
    return std::nullopt;
  }
  return SerializePhase4IsolatedCellJsonV1(result, source_commit, source_stamped,
                                           source_tree_dirty);
}

std::optional<std::string> SerializePhase4ConfirmatoryH4096SameRunCellJsonV2(
    const Phase4IsolatedCellResult& result, std::string_view source_commit, bool source_stamped,
    bool source_tree_dirty) {
  if (!IsCleanSourceCommit(source_commit, source_stamped, source_tree_dirty) ||
      !AuthenticatePhase4ConfirmatoryH4096RawCell(result, WorkerWireMode::kSameRunTelemetryV2)) {
    return std::nullopt;
  }
  return SerializePhase4SameRunIsolatedCellJsonV2(result, source_commit, source_stamped,
                                                  source_tree_dirty);
}

}  // namespace internal

}  // namespace apgar::benchmark
