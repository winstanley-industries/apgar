#include <sys/resource.h>

#include <charconv>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <variant>

#include "apgar/allocator/cpu_candidate_pool_preparation.h"
#include "apgar/benchmark/phase3_commit.h"
#include "apgar/benchmark/phase3_source_stamp.h"
#include "apgar/benchmark/phase4_operational_artifact.h"
#include "apgar/benchmark/phase4_trial_harness.h"
#include "apgar/tooling/runfiles.h"

namespace {

enum class Mode : std::uint8_t {
  kMeasured,
  kAuthority,
};

struct Options {
  std::optional<Mode> mode;
  std::optional<apgar::benchmark::Phase4TrialArm> arm;
  std::optional<std::string> runtime_commit;
  bool testing_allow_unstamped = false;
  std::string fixture_path;
  apgar::benchmark::Phase4CanonicalCellConfig cell;
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
    if (key == "mode") {
      if (value == "measured") {
        options.mode = Mode::kMeasured;
      } else if (value == "authority") {
        options.mode = Mode::kAuthority;
      } else {
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
    } else if (key == "testing_allow_unstamped") {
      options.testing_allow_unstamped = value == "1";
      if (!options.testing_allow_unstamped) {
        return std::nullopt;
      }
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
    } else {
      return std::nullopt;
    }
  }
  if (!options.mode.has_value() || !options.arm.has_value() || options.cell.case_id == 0 ||
      options.cell.requested_pool_size == 0 || options.fixture_path.empty()) {
    return std::nullopt;
  }
  return options;
}

void PrintUsage() {
  std::cerr << "phase4_operational_replay_worker requires --mode=measured|authority "
               "--arm=baseline|candidate --case_id=N --pool_size=4|8|16 and a clean "
               "--apgar_commit=<40 lowercase hex>; numeric bounds use strict decimal\n";
}

[[nodiscard]] bool ExactAddressSpaceLimit(std::uint64_t expected) noexcept {
  rlimit limit{};
  return getrlimit(RLIMIT_AS, &limit) == 0 &&
         static_cast<std::uint64_t>(limit.rlim_cur) == expected &&
         static_cast<std::uint64_t>(limit.rlim_max) == expected;
}

[[nodiscard]] bool WriteStdout(std::string_view value) noexcept {
  std::cout.write(value.data(), static_cast<std::streamsize>(value.size()));
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
  const bool publishable =
      options.runtime_commit.has_value() &&
      apgar::benchmark::IsPublishableBenchmarkSource(
          *options.runtime_commit, apgar::benchmark::kPhase3BuiltCommit,
          apgar::benchmark::kPhase3SourceStamped, apgar::benchmark::kPhase3BuiltFromDirtyTree);
  if (!publishable && !options.testing_allow_unstamped) {
    PrintUsage();
    return 2;
  }
  if (!ExactAddressSpaceLimit(options.cell.external_budget.maximum_address_space_bytes)) {
    std::cerr << "operational worker did not observe the exact RLIMIT_AS contract\n";
    return 2;
  }
  const std::optional<std::string> fixture = apgar::tooling::ReadFile(options.fixture_path);
  if (!fixture.has_value()) {
    std::cerr << "failed to read the imported Phase 4 fixture\n";
    return 2;
  }
  auto spec_result = apgar::benchmark::BuildPhase4CanonicalTrialSpecV1(
      options.cell, 0, apgar::benchmark::Phase4TrialOrder::kBaselineFirst);
  if (std::holds_alternative<apgar::benchmark::Phase4TrialHarnessError>(spec_result)) {
    const auto& error = std::get<apgar::benchmark::Phase4TrialHarnessError>(spec_result);
    std::cerr << error.invariant_id << ": " << error.detail << '\n';
    return 1;
  }
  const auto& spec = std::get<apgar::benchmark::Phase4PairedTrialSpec>(spec_result);
  std::unique_ptr<apgar::allocator::PersistentCpuCandidatePoolPreparer> preparer;
  if (*options.arm == apgar::benchmark::Phase4TrialArm::kReusableCandidateAllocation) {
    auto created = apgar::allocator::CreatePersistentCpuCandidatePoolPreparer(
        {.worker_count = options.cell.preparation_worker_count});
    if (std::holds_alternative<apgar::allocator::CpuCandidatePoolPreparationError>(created)) {
      const auto& error = std::get<apgar::allocator::CpuCandidatePoolPreparationError>(created);
      std::cerr << error.invariant_id << ": " << error.detail << '\n';
      return 1;
    }
    preparer = std::get<std::unique_ptr<apgar::allocator::PersistentCpuCandidatePoolPreparer>>(
        std::move(created));
  }
  auto warmup =
      apgar::benchmark::ExecutePhase4TrialArmV1(*options.arm, spec, *fixture, preparer.get());
  if (std::holds_alternative<apgar::benchmark::Phase4TrialArmFailure>(warmup)) {
    const auto& failure = std::get<apgar::benchmark::Phase4TrialArmFailure>(warmup);
    std::cerr << failure.summary.invariant_id << ": " << failure.summary.detail << '\n';
    return 1;
  }
  const std::string source_commit =
      options.runtime_commit.value_or(apgar::benchmark::kPhase3BuiltCommit.empty()
                                          ? std::string(40, '0')
                                          : std::string(apgar::benchmark::kPhase3BuiltCommit));
  std::optional<std::string> serialized;
  if (*options.mode == Mode::kMeasured) {
    auto result = apgar::benchmark::ExecutePhase4TrialArmOperationalProfileV1(
        *options.arm, spec, *fixture, preparer.get());
    if (std::holds_alternative<apgar::benchmark::Phase4TrialArmFailure>(result)) {
      const auto& failure = std::get<apgar::benchmark::Phase4TrialArmFailure>(result);
      std::cerr << failure.summary.invariant_id << ": " << failure.summary.detail << '\n';
      return 1;
    }
    serialized = apgar::benchmark::SerializePhase4OperationalProfileWorkerJsonV1(
        std::get<apgar::benchmark::Phase4TrialArmOperationalProfileV1>(result), source_commit,
        apgar::benchmark::kPhase3SourceStamped, apgar::benchmark::kPhase3BuiltFromDirtyTree);
  } else {
    auto result = apgar::benchmark::ExecutePhase4TrialArmReplayAuthorityV1(
        *options.arm, spec, *fixture, preparer.get());
    if (std::holds_alternative<apgar::benchmark::Phase4TrialArmFailure>(result)) {
      const auto& failure = std::get<apgar::benchmark::Phase4TrialArmFailure>(result);
      std::cerr << failure.summary.invariant_id << ": " << failure.summary.detail << '\n';
      return 1;
    }
    serialized = apgar::benchmark::SerializePhase4ReplayAuthorityWorkerJsonV1(
        std::get<apgar::benchmark::Phase4TrialArmReplayAuthorityV1>(result), source_commit,
        apgar::benchmark::kPhase3SourceStamped, apgar::benchmark::kPhase3BuiltFromDirtyTree);
  }
  if (!serialized.has_value() || !WriteStdout(*serialized)) {
    std::cerr << "failed to serialize operational worker output\n";
    return 2;
  }
  return 0;
}
