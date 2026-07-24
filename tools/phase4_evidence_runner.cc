#include <fcntl.h>
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
#include "src/benchmark/phase4_paired_trial_internal.h"

namespace {

using apgar::benchmark::Phase4CanonicalCellConfig;

#if defined(APGAR_PHASE4_CONFIRMATORY_RUNNER)
constexpr apgar::benchmark::Phase4RepresentativeCorpusAuthority kCorpusAuthority =
    apgar::benchmark::Phase4RepresentativeCorpusAuthority::kV2;
#else
constexpr apgar::benchmark::Phase4RepresentativeCorpusAuthority kCorpusAuthority =
    apgar::benchmark::Phase4RepresentativeCorpusAuthority::kV1;
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
  options.fixture_path =
      apgar::tooling::ResolveRunfile("tests/fixtures/phase4_supported_multinet_v1.kicad_pcb");

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
      options.worker_mode = value == "1";
      if (!options.worker_mode) {
        return std::nullopt;
      }
    } else if (key == "phase4_same_run_worker") {
      options.same_run_worker_mode = value == "1";
      if (!options.same_run_worker_mode) {
        return std::nullopt;
      }
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
#if !defined(APGAR_PHASE4_CONFIRMATORY_RUNNER_TESTING)
  if (options.cell.preparation_worker_count !=
          apgar::benchmark::kPhase4CanonicalPreparationWorkersV1 ||
      options.cell.repetitions != apgar::benchmark::kPhase4CanonicalRepetitionsV1) {
    return std::nullopt;
  }
#endif
#endif
  return options;
}

[[nodiscard]] std::string SelfExecutable() {
  std::string path(4096, '\0');
  const ssize_t length = readlink("/proc/self/exe", path.data(), path.size());
  if (length <= 0 || static_cast<std::size_t>(length) == path.size()) {
    return {};
  }
  path.resize(static_cast<std::size_t>(length));
  return path;
}

void PrintUsage() {
#if defined(APGAR_PHASE4_CONFIRMATORY_RUNNER)
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
  const std::optional<std::string> fixture = apgar::tooling::ReadFile(options.fixture_path);
  if (!fixture.has_value()) {
    std::cerr << "failed to read the imported Phase 4 fixture\n";
    return 2;
  }
  if (options.worker_mode || options.same_run_worker_mode) {
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
    if (options.same_run_worker_mode) {
#if defined(APGAR_PHASE4_CONFIRMATORY_RUNNER)
      return apgar::benchmark::RunPhase4TrialWorkerWithSameRunTelemetryForCorpusV2(
          *options.arm, options.cell, *fixture, options.request_descriptor,
          options.response_descriptor);
#else
      return apgar::benchmark::RunPhase4TrialWorkerWithSameRunTelemetryV1(
          *options.arm, options.cell, *fixture, options.request_descriptor,
          options.response_descriptor);
#endif
    }
#if defined(APGAR_PHASE4_CONFIRMATORY_RUNNER)
    return apgar::benchmark::RunPhase4TrialWorkerForCorpusV2(*options.arm, options.cell, *fixture,
                                                             options.request_descriptor,
                                                             options.response_descriptor);
#else
    return apgar::benchmark::RunPhase4TrialWorkerV1(*options.arm, options.cell, *fixture,
                                                    options.request_descriptor,
                                                    options.response_descriptor);
#endif
  }
  if (options.arm.has_value() || options.request_descriptor >= 0 ||
      options.response_descriptor >= 0 || options.cell.case_id == 0 ||
      options.cell.requested_pool_size == 0) {
    PrintUsage();
    return 2;
  }
  const bool publishable =
      options.runtime_commit.has_value() &&
      apgar::benchmark::IsPublishableBenchmarkSource(
          *options.runtime_commit, apgar::benchmark::kPhase3BuiltCommit,
          apgar::benchmark::kPhase3SourceStamped, apgar::benchmark::kPhase3BuiltFromDirtyTree);
  if (!publishable && !options.testing_allow_unstamped) {
    PrintUsage();
    return 2;
  }
#if defined(APGAR_PHASE4_CONFIRMATORY_RUNNER)
  const apgar::benchmark::Phase4CaseDescriptor* descriptor =
      apgar::benchmark::FindPhase4CaseDescriptorForAuthority(kCorpusAuthority,
                                                             options.cell.case_id);
  const bool same_run_mode = options.same_run_telemetry_output.has_value();
  if ((!publishable && !options.testing_allow_unstamped) || descriptor == nullptr ||
      (descriptor->role != apgar::benchmark::Phase4CaseRole::kExactOracle &&
       descriptor->role != apgar::benchmark::Phase4CaseRole::kCalibration) ||
      (descriptor->role == apgar::benchmark::Phase4CaseRole::kExactOracle && !same_run_mode) ||
      (descriptor->role == apgar::benchmark::Phase4CaseRole::kCalibration && same_run_mode)) {
    std::cerr << "confirmatory development acquisition is restricted to frozen exact and "
                 "calibration cases using their protocol-assigned raw authority\n";
    return 2;
  }
#else
  if (publishable) {
    std::cerr << "Phase 4 Raw-v1/Wire-v2 publication is frozen at the Session-v3 budget "
                 "authority; current Session-v4 execution is diagnostic-only until a new "
                 "manifest and protocol are frozen\n";
    return 2;
  }
#endif
  const std::string executable = SelfExecutable();
  if (executable.empty()) {
    std::cerr << "failed to resolve the source-identical worker executable\n";
    return 2;
  }
  if (options.same_run_telemetry_output.has_value()) {
    const std::string source_commit =
        options.runtime_commit.value_or(std::string(apgar::benchmark::kPhase3BuiltCommit));
    auto execution =
#if defined(APGAR_PHASE4_CONFIRMATORY_RUNNER)
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
        const auto raw = apgar::benchmark::SerializePhase4SameRunIsolatedCellJsonV2(
            *error.raw_cell, source_commit, apgar::benchmark::kPhase3SourceStamped,
            apgar::benchmark::kPhase3BuiltFromDirtyTree);
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
    const auto raw = apgar::benchmark::SerializePhase4SameRunIsolatedCellJsonV2(
        capture.raw_cell, source_commit, apgar::benchmark::kPhase3SourceStamped,
        apgar::benchmark::kPhase3BuiltFromDirtyTree);
    const auto telemetry =
#if defined(APGAR_PHASE4_CONFIRMATORY_RUNNER)
        apgar::benchmark::SerializePhase4SameRunDecisionTelemetryForCorpusV2JsonV1(
            capture, *fixture, source_commit, apgar::benchmark::kPhase3SourceStamped,
            apgar::benchmark::kPhase3BuiltFromDirtyTree);
#else
        apgar::benchmark::SerializePhase4SameRunDecisionTelemetryJsonV1(
            capture, *fixture, source_commit, apgar::benchmark::kPhase3SourceStamped,
            apgar::benchmark::kPhase3BuiltFromDirtyTree);
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
#if defined(APGAR_PHASE4_CONFIRMATORY_RUNNER)
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
  const auto raw = apgar::benchmark::SerializePhase4IsolatedCellJsonV1(
      result, source_commit, apgar::benchmark::kPhase3SourceStamped,
      apgar::benchmark::kPhase3BuiltFromDirtyTree);
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
