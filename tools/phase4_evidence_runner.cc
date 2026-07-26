#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <variant>

#include "apgar/benchmark/phase3_commit.h"
#include "apgar/benchmark/phase3_source_stamp.h"
#include "apgar/benchmark/phase4_same_run_decision_telemetry.h"
#include "apgar/benchmark/phase4_trial_harness.h"
#include "apgar/tooling/runfiles.h"
#include "src/benchmark/phase4_confirmatory_h4096_execution_internal.h"
#include "src/benchmark/phase4_paired_trial_internal.h"

namespace {

using apgar::benchmark::Phase4CanonicalCellConfig;

#if defined(APGAR_PHASE4_CONFIRMATORY_H4096_RUNNER) && !defined(APGAR_PHASE4_CONFIRMATORY_RUNNER)
#error "the H4096 runner requires the explicit confirmatory Corpus-v2 authority"
#endif

#if defined(APGAR_PHASE4_CONFIRMATORY_H4096_FORCE_UNPUBLISHABLE_SOURCE_FOR_TESTING) && \
    (!defined(APGAR_PHASE4_CONFIRMATORY_H4096_RUNNER) ||                               \
     defined(APGAR_PHASE4_CONFIRMATORY_RUNNER_TESTING))
#error "the forced-unpublishable source probe requires a production-shaped H4096 runner"
#endif

#if defined(APGAR_PHASE4_CONFIRMATORY_RUNNER)
constexpr apgar::benchmark::Phase4RepresentativeCorpusAuthority kCorpusAuthority =
    apgar::benchmark::Phase4RepresentativeCorpusAuthority::kV2;
#else
constexpr apgar::benchmark::Phase4RepresentativeCorpusAuthority kCorpusAuthority =
    apgar::benchmark::Phase4RepresentativeCorpusAuthority::kV1;
#endif

#if defined(APGAR_PHASE4_CONFIRMATORY_H4096_FORCE_UNPUBLISHABLE_SOURCE_FOR_TESTING)
constexpr bool kPublishabilitySourceStamped = false;
constexpr bool kPublishabilitySourceTreeDirty = true;
#else
constexpr bool kPublishabilitySourceStamped = apgar::benchmark::kPhase3SourceStamped;
constexpr bool kPublishabilitySourceTreeDirty = apgar::benchmark::kPhase3BuiltFromDirtyTree;
#endif

#if defined(APGAR_PHASE4_CONFIRMATORY_H4096_RUNNER) && \
    defined(APGAR_PHASE4_CONFIRMATORY_RUNNER_TESTING)
// Test H4096 output is structurally useful but can never be relabeled as
// publishable evidence, even if the test target happens to be built from a
// clean stamped checkout.
constexpr bool kArtifactSourceStamped = false;
constexpr bool kArtifactSourceTreeDirty = true;
#else
constexpr bool kArtifactSourceStamped = apgar::benchmark::kPhase3SourceStamped;
constexpr bool kArtifactSourceTreeDirty = apgar::benchmark::kPhase3BuiltFromDirtyTree;
#endif

struct Options {
  bool worker_mode = false;
  bool same_run_worker_mode = false;
  bool testing_allow_unstamped = false;
  std::optional<apgar::benchmark::Phase4TrialArm> arm;
  std::optional<std::string> runtime_commit;
  std::optional<std::string> same_run_telemetry_output;
  bool corpus_version_seen = false;
  std::string fixture_path;
  int request_descriptor = -1;
  int response_descriptor = -1;
  Phase4CanonicalCellConfig cell;
};

[[nodiscard]] bool ParseUnsigned(std::string_view text, std::uint64_t* value) noexcept {
  if (text.empty() || (text.size() > 1 && text.front() == '0')) {
    return false;
  }
  const char* const begin = text.data();
  const auto [end, error] = std::from_chars(begin, begin + text.size(), *value);
  return error == std::errc{} && end == begin + text.size();
}

[[nodiscard]] bool ParseU32(std::string_view text, std::uint32_t* value) noexcept {
  std::uint64_t wide = 0;
  if (!ParseUnsigned(text, &wide) || wide > std::numeric_limits<std::uint32_t>::max()) {
    return false;
  }
  *value = static_cast<std::uint32_t>(wide);
  return true;
}

[[nodiscard]] bool ParseDescriptor(std::string_view text, int* value) noexcept {
  std::uint64_t wide = 0;
  if (!ParseUnsigned(text, &wide) ||
      wide > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) {
    return false;
  }
  *value = static_cast<int>(wide);
  return true;
}

[[nodiscard]] std::optional<Options> ParseOptions(int argc, char** argv) {
  Options options;
  options.cell.preparation_worker_count = apgar::benchmark::kPhase4CanonicalPreparationWorkersV1;
  options.cell.repetitions = apgar::benchmark::kPhase4CanonicalRepetitionsV1;
  options.cell.maximum_setup_elapsed_nanoseconds = 300'000'000'000ULL;
  options.cell.external_budget.maximum_prepared_elapsed_nanoseconds = 300'000'000'000ULL;
  options.cell.external_budget.maximum_cold_elapsed_nanoseconds = 300'000'000'000ULL;
  options.cell.external_budget.maximum_address_space_bytes = 64ULL * 1024ULL * 1024ULL * 1024ULL;
  options.cell.external_budget.maximum_peak_host_bytes = 16ULL * 1024ULL * 1024ULL * 1024ULL;
#if defined(APGAR_PHASE4_CONFIRMATORY_H4096_RUNNER)
  // The H4096 targets intentionally carry no fixture runfile. A caller must
  // cross the complete source and observation firewall before naming one.
  options.fixture_path.clear();
#else
  options.fixture_path =
      apgar::tooling::ResolveRunfile("tests/fixtures/phase4_supported_multinet_v1.kicad_pcb");
#endif

  std::unordered_set<std::string> seen;
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    const std::size_t separator = argument.find('=');
    if (!argument.starts_with("--") || separator == std::string_view::npos || separator == 2) {
      return std::nullopt;
    }
    const std::string key(argument.substr(2, separator - 2));
    const std::string_view value = argument.substr(separator + 1);
    if (!seen.insert(key).second) {
      return std::nullopt;
    }
    if (key == "phase4_worker") {
#if defined(APGAR_PHASE4_CONFIRMATORY_H4096_RUNNER)
      return std::nullopt;
#else
      options.worker_mode = value == "1";
      if (!options.worker_mode) {
        return std::nullopt;
      }
#endif
    } else if (key == "phase4_same_run_worker") {
#if defined(APGAR_PHASE4_CONFIRMATORY_H4096_RUNNER)
      return std::nullopt;
#else
      options.same_run_worker_mode = value == "1";
      if (!options.same_run_worker_mode) {
        return std::nullopt;
      }
#endif
    } else if (key == "phase4_h4096_ordinary_worker") {
#if defined(APGAR_PHASE4_CONFIRMATORY_H4096_RUNNER)
      options.worker_mode = value == "1";
      if (!options.worker_mode) {
        return std::nullopt;
      }
#else
      return std::nullopt;
#endif
    } else if (key == "phase4_h4096_same_run_worker") {
#if defined(APGAR_PHASE4_CONFIRMATORY_H4096_RUNNER)
      options.same_run_worker_mode = value == "1";
      if (!options.same_run_worker_mode) {
        return std::nullopt;
      }
#else
      return std::nullopt;
#endif
    } else if (key == "testing_allow_unstamped") {
#if defined(APGAR_PHASE4_CONFIRMATORY_RUNNER) && !defined(APGAR_PHASE4_CONFIRMATORY_RUNNER_TESTING)
      return std::nullopt;
#else
      options.testing_allow_unstamped = value == "1";
      if (!options.testing_allow_unstamped) {
        return std::nullopt;
      }
#endif
    } else if (key == "corpus_version") {
      const std::string_view expected =
          kCorpusAuthority == apgar::benchmark::Phase4RepresentativeCorpusAuthority::kV1 ? "1"
                                                                                         : "2";
      if (value != expected) {
        return std::nullopt;
      }
      options.corpus_version_seen = true;
    } else if (key == "arm") {
      if (value == "baseline") {
        options.arm = apgar::benchmark::Phase4TrialArm::kSequentialBaseline;
      } else if (value == "candidate") {
        options.arm = apgar::benchmark::Phase4TrialArm::kReusableCandidateAllocation;
      } else {
        return std::nullopt;
      }
    } else if (key == "apgar_commit") {
      options.runtime_commit = std::string(value);
    } else if (key == "same_run_telemetry_output") {
      if (value.empty()) {
        return std::nullopt;
      }
      options.same_run_telemetry_output = std::string(value);
    } else if (key == "fixture_path") {
      if (value.empty()) {
        return std::nullopt;
      }
      options.fixture_path = std::string(value);
    } else if (key == "case_id") {
      if (!ParseU32(value, &options.cell.case_id)) {
        return std::nullopt;
      }
    } else if (key == "pool_size") {
      if (!ParseU32(value, &options.cell.requested_pool_size)) {
        return std::nullopt;
      }
    } else if (key == "workers") {
      if (!ParseU32(value, &options.cell.preparation_worker_count)) {
        return std::nullopt;
      }
    } else if (key == "repetitions") {
      if (!ParseU32(value, &options.cell.repetitions)) {
        return std::nullopt;
      }
    } else if (key == "setup_ns") {
      if (!ParseUnsigned(value, &options.cell.maximum_setup_elapsed_nanoseconds)) {
        return std::nullopt;
      }
    } else if (key == "prepared_ns") {
      if (!ParseUnsigned(value,
                         &options.cell.external_budget.maximum_prepared_elapsed_nanoseconds)) {
        return std::nullopt;
      }
    } else if (key == "cold_ns") {
      if (!ParseUnsigned(value, &options.cell.external_budget.maximum_cold_elapsed_nanoseconds)) {
        return std::nullopt;
      }
    } else if (key == "address_space_bytes") {
      if (!ParseUnsigned(value, &options.cell.external_budget.maximum_address_space_bytes)) {
        return std::nullopt;
      }
    } else if (key == "peak_host_bytes") {
      if (!ParseUnsigned(value, &options.cell.external_budget.maximum_peak_host_bytes)) {
        return std::nullopt;
      }
    } else if (key == "maximum_nets") {
      if (!ParseUnsigned(value, &options.cell.corpus_limits.maximum_nets)) {
        return std::nullopt;
      }
    } else if (key == "maximum_compiled_nodes") {
      if (!ParseUnsigned(value, &options.cell.corpus_limits.maximum_compiled_nodes)) {
        return std::nullopt;
      }
    } else if (key == "maximum_compiled_host_bytes") {
      if (!ParseUnsigned(value, &options.cell.corpus_limits.maximum_compiled_host_bytes)) {
        return std::nullopt;
      }
    } else if (key == "maximum_active_regions") {
      if (!ParseUnsigned(value, &options.cell.corpus_limits.maximum_active_regions)) {
        return std::nullopt;
      }
    } else if (key == "maximum_board_entities") {
      if (!ParseUnsigned(value, &options.cell.corpus_limits.maximum_board_entities)) {
        return std::nullopt;
      }
    } else if (key == "request_fd") {
      if (!ParseDescriptor(value, &options.request_descriptor)) {
        return std::nullopt;
      }
    } else if (key == "response_fd") {
      if (!ParseDescriptor(value, &options.response_descriptor)) {
        return std::nullopt;
      }
    } else {
      return std::nullopt;
    }
  }
#if defined(APGAR_PHASE4_CONFIRMATORY_RUNNER)
  if (!options.corpus_version_seen) {
    return std::nullopt;
  }
#if !defined(APGAR_PHASE4_CONFIRMATORY_RUNNER_TESTING) && \
    !defined(APGAR_PHASE4_CONFIRMATORY_H4096_RUNNER)
  if (options.cell.preparation_worker_count !=
          apgar::benchmark::kPhase4CanonicalPreparationWorkersV1 ||
      options.cell.repetitions != apgar::benchmark::kPhase4CanonicalRepetitionsV1) {
    return std::nullopt;
  }
#endif
#endif
  return options;
}

// Do not resolve this link in the controller. Each forked child must dereference
// its own procfs link at exec time so every worker reopens the controller's
// already-running executable inode even if its output pathname is replaced.
constexpr std::string_view kPinnedSelfExecutable = "/proc/self/exe";

void PrintUsage() {
#if defined(APGAR_PHASE4_CONFIRMATORY_H4096_RUNNER)
  std::cerr << "phase4_confirmatory_h4096_evidence_runner requires --corpus_version=2, an explicit "
               "--fixture_path, a clean --apgar_commit=<40 lowercase hex>, and exactly either "
               "same-run (10100,4) or ordinary (10200,8).\n";
#elif defined(APGAR_PHASE4_CONFIRMATORY_RUNNER)
  std::cerr
      << "phase4_confirmatory_evidence_runner requires --corpus_version=2 --case_id=N "
         "--pool_size=4|8|16 and a clean --apgar_commit=<40 lowercase hex>; optional same-run "
         "capture requires --same_run_telemetry_output=<new path>; only exact/calibration "
         "development cases are accepted.\n";
#else
  std::cerr << "phase4_evidence_runner requires --case_id=N --pool_size=4|8|16 and a clean "
               "--apgar_commit=<40 lowercase hex>; optional same-run capture requires "
               "--same_run_telemetry_output=<new path>; all numeric options are strict decimal.\n";
#endif
}

#if defined(APGAR_PHASE4_CONFIRMATORY_RUNNER)
[[nodiscard]] bool ConfirmatoryDevelopmentScopeAllowed(const Options& options,
                                                       bool worker_process) noexcept {
#if defined(APGAR_PHASE4_CONFIRMATORY_H4096_RUNNER)
  const bool same_run_mode =
      worker_process ? options.same_run_worker_mode : options.same_run_telemetry_output.has_value();
  return same_run_mode ? options.cell.case_id == 10'100 && options.cell.requested_pool_size == 4
                       : options.cell.case_id == 10'200 && options.cell.requested_pool_size == 8;
#else
  const apgar::benchmark::Phase4CaseDescriptor* descriptor =
      apgar::benchmark::FindPhase4CaseDescriptorForAuthority(kCorpusAuthority,
                                                             options.cell.case_id);
  const bool same_run_mode =
      worker_process ? options.same_run_worker_mode : options.same_run_telemetry_output.has_value();
  return descriptor != nullptr &&
         ((descriptor->role == apgar::benchmark::Phase4CaseRole::kExactOracle && same_run_mode) ||
          (descriptor->role == apgar::benchmark::Phase4CaseRole::kCalibration && !same_run_mode));
#endif
}

void PrintConfirmatoryDevelopmentScopeError() {
#if defined(APGAR_PHASE4_CONFIRMATORY_H4096_RUNNER)
  std::cerr << "H4096 development acquisition is restricted to same-run (10100,4) and ordinary "
               "(10200,8) under their Protocol-v2 Raw authorities\n";
#else
  std::cerr << "confirmatory development acquisition is restricted to frozen exact and "
               "calibration cases using their protocol-assigned raw authority\n";
#endif
}
#endif

[[nodiscard]] bool WriteNewFile(std::string_view path, std::string_view contents) noexcept {
  try {
#if defined(APGAR_PHASE4_TRIAL_FAULT_TEST_VARIANT)
    const char* const fault_mode = std::getenv("APGAR_PHASE4_TRIAL_FAULT_MODE");
    if (fault_mode != nullptr && std::string_view(fault_mode) == "sidecar_publish_bad_alloc") {
      throw std::bad_alloc();
    }
#endif
    const std::string final_path(path);
    const std::size_t separator = final_path.find_last_of('/');
    const std::string parent = separator == std::string::npos
                                   ? "."
                                   : (separator == 0 ? "/" : final_path.substr(0, separator));
    const std::string name =
        separator == std::string::npos ? final_path : final_path.substr(separator + 1);
    if (name.empty()) {
      return false;
    }
    const int parent_descriptor = open(parent.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (parent_descriptor < 0) {
      return false;
    }
    std::string temporary_path;
    int descriptor = -1;
    for (std::uint32_t attempt = 0; attempt < 128 && descriptor < 0; ++attempt) {
      temporary_path =
          parent + "/." + name + ".tmp." + std::to_string(getpid()) + "." + std::to_string(attempt);
      descriptor = open(temporary_path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC,
                        S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
      if (descriptor < 0 && errno != EEXIST) {
        (void)close(parent_descriptor);
        return false;
      }
    }
    if (descriptor < 0) {
      (void)close(parent_descriptor);
      return false;
    }
    std::size_t written = 0;
    while (written < contents.size()) {
      const ssize_t result =
          write(descriptor, contents.data() + written, contents.size() - written);
      if (result < 0 && errno == EINTR) {
        continue;
      }
      if (result <= 0) {
        (void)close(descriptor);
        (void)unlink(temporary_path.c_str());
        (void)close(parent_descriptor);
        return false;
      }
      written += static_cast<std::size_t>(result);
    }
    const bool sync_failed = fsync(descriptor) != 0;
    const bool close_failed = close(descriptor) != 0;
    if (sync_failed || close_failed) {
      (void)unlink(temporary_path.c_str());
      (void)close(parent_descriptor);
      return false;
    }
#if defined(APGAR_PHASE4_TRIAL_FAULT_TEST_VARIANT)
    if (fault_mode != nullptr && std::string_view(fault_mode) == "sidecar_before_publish") {
      (void)unlink(temporary_path.c_str());
      (void)close(parent_descriptor);
      return false;
    }
#endif
    if (link(temporary_path.c_str(), final_path.c_str()) != 0) {
      (void)unlink(temporary_path.c_str());
      (void)close(parent_descriptor);
      return false;
    }
    if (unlink(temporary_path.c_str()) != 0 || fsync(parent_descriptor) != 0) {
      (void)unlink(final_path.c_str());
      (void)unlink(temporary_path.c_str());
      (void)fsync(parent_descriptor);
      (void)close(parent_descriptor);
      return false;
    }
    (void)close(parent_descriptor);
    return true;
  } catch (...) {
    return false;
  }
}

[[nodiscard]] bool WriteStdout(std::string_view contents) noexcept {
  std::cout.write(contents.data(), static_cast<std::streamsize>(contents.size()));
  std::cout.flush();
  return std::cout.good();
}

}  // namespace

int main(int argc, char** argv) {
  std::optional<Options> parsed = ParseOptions(argc, argv);
  if (!parsed.has_value()) {
    PrintUsage();
    return 2;
  }
  Options& options = *parsed;
  const bool worker_process = options.worker_mode || options.same_run_worker_mode;
  if (worker_process) {
    if (options.worker_mode == options.same_run_worker_mode) {
      PrintUsage();
      return 2;
    }
    if (!options.arm.has_value() || options.request_descriptor < 0 ||
        options.response_descriptor < 0 || options.runtime_commit.has_value() ||
        options.same_run_telemetry_output.has_value() || options.testing_allow_unstamped) {
      PrintUsage();
      return 2;
    }
  } else if (options.arm.has_value() || options.request_descriptor >= 0 ||
             options.response_descriptor >= 0 || options.cell.case_id == 0 ||
             options.cell.requested_pool_size == 0) {
    PrintUsage();
    return 2;
  }
#if defined(APGAR_PHASE4_CONFIRMATORY_RUNNER)
  if (!ConfirmatoryDevelopmentScopeAllowed(options, worker_process)) {
    PrintConfirmatoryDevelopmentScopeError();
    return 2;
  }
#endif
#if defined(APGAR_PHASE4_CONFIRMATORY_H4096_RUNNER)
  const bool same_run_mode =
      worker_process ? options.same_run_worker_mode : options.same_run_telemetry_output.has_value();
  const std::optional<apgar::benchmark::Phase4TrialHarnessError> preflight_error =
      same_run_mode
          ? apgar::benchmark::internal::PreflightPhase4ConfirmatoryH4096SameRunCell(options.cell)
          : apgar::benchmark::internal::PreflightPhase4ConfirmatoryH4096OrdinaryCell(options.cell);
  if (preflight_error.has_value()) {
    std::cerr << preflight_error->invariant_id << ": " << preflight_error->detail << '\n';
    return 2;
  }
#endif
  [[maybe_unused]] const bool publishable =
      options.runtime_commit.has_value() &&
      apgar::benchmark::IsPublishableBenchmarkSource(
          *options.runtime_commit, apgar::benchmark::kPhase3BuiltCommit,
          kPublishabilitySourceStamped, kPublishabilitySourceTreeDirty);
#if defined(APGAR_PHASE4_CONFIRMATORY_H4096_RUNNER)
  if (options.fixture_path.empty()) {
    PrintUsage();
    return 2;
  }
#if defined(APGAR_PHASE4_CONFIRMATORY_RUNNER_TESTING)
  if (!worker_process && (!options.testing_allow_unstamped || options.runtime_commit.has_value())) {
    PrintUsage();
    return 2;
  }
  std::cerr << "the H4096 test runner is preflight-only and cannot execute a board fixture\n";
  return 2;
#else
  if ((worker_process && !apgar::benchmark::IsPublishableEmbeddedBenchmarkSource(
                             apgar::benchmark::kPhase3BuiltCommit, kPublishabilitySourceStamped,
                             kPublishabilitySourceTreeDirty)) ||
      (!worker_process && !publishable)) {
    PrintUsage();
    return 2;
  }
#endif
#endif
  const std::optional<std::string> fixture = apgar::tooling::ReadFile(options.fixture_path);
  if (!fixture.has_value()) {
    std::cerr << "failed to read the imported Phase 4 fixture\n";
    return 2;
  }
  if (worker_process) {
    if (options.same_run_worker_mode) {
#if defined(APGAR_PHASE4_CONFIRMATORY_H4096_RUNNER)
      return apgar::benchmark::internal::RunPhase4ConfirmatoryH4096SameRunWorker(
          *options.arm, options.cell, *fixture, options.request_descriptor,
          options.response_descriptor);
#elif defined(APGAR_PHASE4_CONFIRMATORY_RUNNER)
      return apgar::benchmark::RunPhase4TrialWorkerWithSameRunTelemetryForCorpusV2(
          *options.arm, options.cell, *fixture, options.request_descriptor,
          options.response_descriptor);
#else
      return apgar::benchmark::RunPhase4TrialWorkerWithSameRunTelemetryV1(
          *options.arm, options.cell, *fixture, options.request_descriptor,
          options.response_descriptor);
#endif
    }
#if defined(APGAR_PHASE4_CONFIRMATORY_H4096_RUNNER)
    return apgar::benchmark::internal::RunPhase4ConfirmatoryH4096OrdinaryWorker(
        *options.arm, options.cell, *fixture, options.request_descriptor,
        options.response_descriptor);
#elif defined(APGAR_PHASE4_CONFIRMATORY_RUNNER)
    return apgar::benchmark::RunPhase4TrialWorkerForCorpusV2(*options.arm, options.cell, *fixture,
                                                             options.request_descriptor,
                                                             options.response_descriptor);
#else
    return apgar::benchmark::RunPhase4TrialWorkerV1(*options.arm, options.cell, *fixture,
                                                    options.request_descriptor,
                                                    options.response_descriptor);
#endif
  }
#if !defined(APGAR_PHASE4_CONFIRMATORY_H4096_RUNNER)
  if (!publishable && !options.testing_allow_unstamped) {
    PrintUsage();
    return 2;
  }
#endif
#if !defined(APGAR_PHASE4_CONFIRMATORY_RUNNER)
  if (publishable) {
    std::cerr << "Phase 4 Raw-v1/Wire-v2 publication is frozen at the Session-v3 budget "
                 "authority; current Session-v4 execution is diagnostic-only until a new "
                 "manifest and protocol are frozen\n";
    return 2;
  }
#endif
  const std::string executable(kPinnedSelfExecutable);
#if defined(APGAR_PHASE4_TRIAL_FAULT_TEST_VARIANT)
  const char* const executable_fault_mode = std::getenv("APGAR_PHASE4_TRIAL_FAULT_MODE");
  if (executable_fault_mode != nullptr &&
      std::string_view(executable_fault_mode) == "executable_path_replacement" &&
      raise(SIGSTOP) != 0) {
    std::cerr << "failed to enter the executable-path replacement test barrier\n";
    return 2;
  }
#endif
  if (options.same_run_telemetry_output.has_value()) {
    const std::string source_commit =
        options.runtime_commit.value_or(std::string(apgar::benchmark::kPhase3BuiltCommit));
    auto execution =
#if defined(APGAR_PHASE4_CONFIRMATORY_H4096_RUNNER)
        apgar::benchmark::internal::RunPhase4ConfirmatoryH4096SameRunCell(options.cell, executable,
                                                                          options.fixture_path);
#elif defined(APGAR_PHASE4_CONFIRMATORY_RUNNER)
        apgar::benchmark::RunPhase4IsolatedCellWithSameRunDecisionTelemetryForCorpusV2(
            options.cell, executable, options.fixture_path);
#else
        apgar::benchmark::RunPhase4IsolatedCellWithSameRunDecisionTelemetryV1(
            options.cell, executable, options.fixture_path);
#endif
    if (std::holds_alternative<apgar::benchmark::Phase4TrialHarnessError>(execution)) {
      const auto& error = std::get<apgar::benchmark::Phase4TrialHarnessError>(execution);
      std::cerr << error.invariant_id << ": " << error.detail << '\n';
      if (error.raw_cell.has_value()) {
        const auto raw =
#if defined(APGAR_PHASE4_CONFIRMATORY_H4096_RUNNER)
            apgar::benchmark::internal::SerializePhase4ConfirmatoryH4096SameRunCellJsonV2(
#else
            apgar::benchmark::SerializePhase4SameRunIsolatedCellJsonV2(
#endif
                *error.raw_cell, source_commit, kArtifactSourceStamped, kArtifactSourceTreeDirty);
        if (!raw.has_value() || !WriteStdout(*raw)) {
          std::cerr << "failed to write the same-run Raw evidence artifact\n";
          return 2;
        }
        return 1;
      }
      return 2;
    }
    auto& capture =
        std::get<apgar::benchmark::Phase4IsolatedCellWithSameRunDecisionTelemetryV1>(execution);
#if defined(APGAR_PHASE4_TRIAL_FAULT_TEST_VARIANT)
    const char* const same_run_fault_mode = std::getenv("APGAR_PHASE4_TRIAL_FAULT_MODE");
    if (same_run_fault_mode != nullptr &&
        std::string_view(same_run_fault_mode) == "sidecar_corrupt_capture" &&
        !capture.same_run_attempts.empty()) {
      capture.same_run_attempts.pop_back();
    }
    if (same_run_fault_mode != nullptr &&
        std::string_view(same_run_fault_mode) == "sidecar_rehashed_partition_drift" &&
        !capture.same_run_attempts.empty() &&
        !capture.same_run_attempts.front().baseline.telemetry.per_net.empty()) {
      auto& pair = capture.same_run_attempts.front();
      auto& arm = pair.baseline;
      ++arm.telemetry.per_net.front().columns.requested_columns;
      arm.telemetry.telemetry_checksum =
          apgar::benchmark::internal::ComputePhase4SameRunArmDecisionTelemetryChecksumV1(
              arm.telemetry);
      arm.capture_checksum =
          apgar::benchmark::ComputePhase4IsolatedSameRunArmCaptureChecksumV1(arm);
      pair.capture_checksum =
          apgar::benchmark::ComputePhase4IsolatedSameRunPairCaptureChecksumV1(pair);
    }
    if (same_run_fault_mode != nullptr &&
        std::string_view(same_run_fault_mode) == "sidecar_rehashed_foreign_record") {
      apgar::benchmark::RehashForeignPhase4SameRunRecordForTesting(&capture);
    }
    if (same_run_fault_mode != nullptr &&
        std::string_view(same_run_fault_mode) == "sidecar_rehashed_wrong_comparison") {
      apgar::benchmark::RehashWrongPhase4SameRunComparisonForTesting(&capture);
    }
    if (same_run_fault_mode != nullptr &&
        std::string_view(same_run_fault_mode) == "sidecar_rehashed_nondeterministic_record") {
      apgar::benchmark::RehashNondeterministicPhase4SameRunRecordForTesting(&capture);
    }
#endif
    const auto raw =
#if defined(APGAR_PHASE4_CONFIRMATORY_H4096_RUNNER)
        apgar::benchmark::internal::SerializePhase4ConfirmatoryH4096SameRunCellJsonV2(
#else
        apgar::benchmark::SerializePhase4SameRunIsolatedCellJsonV2(
#endif
            capture.raw_cell, source_commit, kArtifactSourceStamped, kArtifactSourceTreeDirty);
    const auto telemetry =
#if defined(APGAR_PHASE4_CONFIRMATORY_H4096_RUNNER)
        apgar::benchmark::internal::SerializePhase4ConfirmatoryH4096SameRunDecisionTelemetryJsonV1(
            capture, *fixture, source_commit, kArtifactSourceStamped, kArtifactSourceTreeDirty);
#elif defined(APGAR_PHASE4_CONFIRMATORY_RUNNER)
        apgar::benchmark::SerializePhase4SameRunDecisionTelemetryForCorpusV2JsonV1(
            capture, *fixture, source_commit, kArtifactSourceStamped, kArtifactSourceTreeDirty);
#else
        apgar::benchmark::SerializePhase4SameRunDecisionTelemetryJsonV1(
            capture, *fixture, source_commit, kArtifactSourceStamped, kArtifactSourceTreeDirty);
#endif
    if (!raw.has_value() || !WriteStdout(*raw)) {
      std::cerr << "failed to write the same-run Raw evidence artifact\n";
      return 2;
    }
    if (!telemetry.has_value() || !WriteNewFile(*options.same_run_telemetry_output, *telemetry)) {
      std::cerr << "failed to create the same-run decision telemetry artifact\n";
      return 2;
    }
    return 0;
  }
  auto execution =
#if defined(APGAR_PHASE4_CONFIRMATORY_H4096_RUNNER)
      apgar::benchmark::internal::RunPhase4ConfirmatoryH4096OrdinaryCell(options.cell, executable,
                                                                         options.fixture_path);
#elif defined(APGAR_PHASE4_CONFIRMATORY_RUNNER)
      apgar::benchmark::RunPhase4IsolatedCellForCorpusV2(options.cell, executable,
                                                         options.fixture_path);
#else
      apgar::benchmark::RunPhase4IsolatedCellV1(options.cell, executable, options.fixture_path);
#endif
  if (std::holds_alternative<apgar::benchmark::Phase4TrialHarnessError>(execution)) {
    const auto& error = std::get<apgar::benchmark::Phase4TrialHarnessError>(execution);
    std::cerr << error.invariant_id << ": " << error.detail << '\n';
    return 2;
  }
  const auto& result = std::get<apgar::benchmark::Phase4IsolatedCellResult>(execution);
  const std::string source_commit =
      options.runtime_commit.value_or(std::string(apgar::benchmark::kPhase3BuiltCommit));
  const auto raw =
#if defined(APGAR_PHASE4_CONFIRMATORY_H4096_RUNNER)
      apgar::benchmark::internal::SerializePhase4ConfirmatoryH4096OrdinaryCellJsonV1(
#else
      apgar::benchmark::SerializePhase4IsolatedCellJsonV1(
#endif
          result, source_commit, kArtifactSourceStamped, kArtifactSourceTreeDirty);
  if (!raw.has_value() || !WriteStdout(*raw)) {
    std::cerr << "failed to write the Raw evidence artifact\n";
    return 2;
  }
  for (const auto& attempt : result.attempts) {
    if (!attempt.result.has_value()) {
      return 1;
    }
  }
  return 0;
}
