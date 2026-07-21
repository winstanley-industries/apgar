#include <unistd.h>

#include <charconv>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <variant>

#include "apgar/benchmark/phase3_commit.h"
#include "apgar/benchmark/phase3_source_stamp.h"
#include "apgar/benchmark/phase4_trial_harness.h"
#include "apgar/tooling/runfiles.h"

namespace {

using apgar::benchmark::Phase4CanonicalCellConfig;

struct Options {
  bool worker_mode = false;
  bool testing_allow_unstamped = false;
  std::optional<apgar::benchmark::Phase4TrialArm> arm;
  std::optional<std::string> runtime_commit;
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
    } else if (key == "testing_allow_unstamped") {
      options.testing_allow_unstamped = value == "1";
      if (!options.testing_allow_unstamped) {
        return std::nullopt;
      }
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
  std::cerr << "phase4_evidence_runner requires --case_id=N --pool_size=4|8|16 and a clean "
               "--apgar_commit=<40 lowercase hex>; all numeric options are strict decimal.\n";
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
  if (options.worker_mode) {
    if (!options.arm.has_value() || options.request_descriptor < 0 ||
        options.response_descriptor < 0 || options.runtime_commit.has_value() ||
        options.testing_allow_unstamped) {
      PrintUsage();
      return 2;
    }
    return apgar::benchmark::RunPhase4TrialWorkerV1(*options.arm, options.cell, *fixture,
                                                    options.request_descriptor,
                                                    options.response_descriptor);
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
  const std::string executable = SelfExecutable();
  if (executable.empty()) {
    std::cerr << "failed to resolve the source-identical worker executable\n";
    return 2;
  }
  auto execution =
      apgar::benchmark::RunPhase4IsolatedCellV1(options.cell, executable, options.fixture_path);
  if (std::holds_alternative<apgar::benchmark::Phase4TrialHarnessError>(execution)) {
    const auto& error = std::get<apgar::benchmark::Phase4TrialHarnessError>(execution);
    std::cerr << error.invariant_id << ": " << error.detail << '\n';
    return 2;
  }
  const auto& result = std::get<apgar::benchmark::Phase4IsolatedCellResult>(execution);
  const std::string source_commit =
      options.runtime_commit.value_or(std::string(apgar::benchmark::kPhase3BuiltCommit));
  std::cout << apgar::benchmark::SerializePhase4IsolatedCellJsonV1(
      result, source_commit, apgar::benchmark::kPhase3SourceStamped,
      apgar::benchmark::kPhase3BuiltFromDirtyTree);
  for (const auto& attempt : result.attempts) {
    if (!attempt.result.has_value()) {
      return 1;
    }
  }
  return 0;
}
