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

#if defined(APGAR_PHASE4_CONFIRMATORY_H4096_SAME_RUN_OPERATIONAL_WORKER)
#include "src/benchmark/phase4_h4096_canonical_budget_internal.h"
#include "src/benchmark/phase4_paired_trial_internal.h"
#endif

#if (defined(APGAR_PHASE4_CONFIRMATORY_OPERATIONAL_WORKER) &&                 \
     defined(APGAR_PHASE4_CONFIRMATORY_SAME_RUN_OPERATIONAL_WORKER)) ||       \
    (defined(APGAR_PHASE4_CONFIRMATORY_OPERATIONAL_WORKER) &&                 \
     defined(APGAR_PHASE4_CONFIRMATORY_H4096_SAME_RUN_OPERATIONAL_WORKER)) || \
    (defined(APGAR_PHASE4_CONFIRMATORY_SAME_RUN_OPERATIONAL_WORKER) &&        \
     defined(APGAR_PHASE4_CONFIRMATORY_H4096_SAME_RUN_OPERATIONAL_WORKER))
#error "The confirmatory operational worker must select exactly one Raw authority"
#endif

#if defined(APGAR_PHASE4_CONFIRMATORY_OPERATIONAL_WORKER) ||          \
    defined(APGAR_PHASE4_CONFIRMATORY_SAME_RUN_OPERATIONAL_WORKER) || \
    defined(APGAR_PHASE4_CONFIRMATORY_H4096_SAME_RUN_OPERATIONAL_WORKER)
#define APGAR_PHASE4_CONFIRMATORY_OPERATIONAL_WORKER_ACTIVE
#endif

#if defined(APGAR_PHASE4_CONFIRMATORY_OPERATIONAL_WORKER_TESTING) && \
    !defined(APGAR_PHASE4_CONFIRMATORY_OPERATIONAL_WORKER_ACTIVE)
#error "The confirmatory operational worker testing escape requires the confirmatory worker"
#endif

namespace {

enum class Mode : std::uint8_t {
  kMeasured,
  kAuthority,
};

#if defined(APGAR_PHASE4_CONFIRMATORY_OPERATIONAL_WORKER_ACTIVE)
#if defined(APGAR_PHASE4_CONFIRMATORY_SAME_RUN_OPERATIONAL_WORKER) || \
    defined(APGAR_PHASE4_CONFIRMATORY_H4096_SAME_RUN_OPERATIONAL_WORKER)
inline constexpr std::uint32_t kConfirmatoryCaseId = 10'100;
inline constexpr std::uint32_t kConfirmatoryRawWireSchemaVersion = 2;
#else
inline constexpr std::uint32_t kConfirmatoryCaseId = 10'200;
inline constexpr std::uint32_t kConfirmatoryRawWireSchemaVersion = 1;
#endif
inline constexpr std::uint32_t kConfirmatoryPoolSize = 4;
inline constexpr std::uint64_t kConfirmatorySetupNanoseconds = 300'000'000'000ULL;
inline constexpr std::uint64_t kConfirmatoryPreparedNanoseconds = 300'000'000'000ULL;
inline constexpr std::uint64_t kConfirmatoryColdNanoseconds = 300'000'000'000ULL;
inline constexpr std::uint64_t kConfirmatoryAddressSpaceBytes = 64ULL * 1024ULL * 1024ULL * 1024ULL;
inline constexpr std::uint64_t kConfirmatoryPeakHostBytes = 16ULL * 1024ULL * 1024ULL * 1024ULL;
inline constexpr std::uint64_t kConfirmatoryMaximumNets = 4096;
inline constexpr std::uint64_t kConfirmatoryMaximumCompiledNodes = 100'000'000;
inline constexpr std::uint64_t kConfirmatoryMaximumCompiledHostBytes =
    8ULL * 1024ULL * 1024ULL * 1024ULL;
inline constexpr std::uint64_t kConfirmatoryMaximumActiveRegions = 250'000;
inline constexpr std::uint64_t kConfirmatoryMaximumBoardEntities = 100'000;
#endif

struct Options {
  std::optional<Mode> mode;
  std::optional<apgar::benchmark::Phase4TrialArm> arm;
  std::optional<std::string> runtime_commit;
  bool testing_allow_unstamped = false;
#if !defined(APGAR_PHASE4_CONFIRMATORY_OPERATIONAL_WORKER_ACTIVE)
  std::string fixture_path;
#endif
  apgar::benchmark::Phase4CanonicalCellConfig cell;
#if defined(APGAR_PHASE4_CONFIRMATORY_OPERATIONAL_WORKER_ACTIVE)
  std::optional<std::uint32_t> corpus_version;
  std::optional<std::uint32_t> raw_wire_schema_version;
#endif
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
#if !defined(APGAR_PHASE4_CONFIRMATORY_OPERATIONAL_WORKER_ACTIVE)
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
#if !defined(APGAR_PHASE4_CONFIRMATORY_OPERATIONAL_WORKER_ACTIVE) || \
    defined(APGAR_PHASE4_CONFIRMATORY_OPERATIONAL_WORKER_TESTING)
      options.testing_allow_unstamped = value == "1";
      if (!options.testing_allow_unstamped) {
        return std::nullopt;
      }
#else
      return std::nullopt;
#endif
    } else if (key == "fixture_path") {
#if defined(APGAR_PHASE4_CONFIRMATORY_OPERATIONAL_WORKER_ACTIVE)
      return std::nullopt;
#else
      if (value.empty()) {
        return std::nullopt;
      }
      options.fixture_path = std::string(value);
#endif
#if defined(APGAR_PHASE4_CONFIRMATORY_OPERATIONAL_WORKER_ACTIVE)
    } else if (key == "corpus_version") {
      std::uint32_t parsed_version = 0;
      if (!ParseU32(value, &parsed_version)) {
        return std::nullopt;
      }
      options.corpus_version = parsed_version;
    } else if (key == "raw_wire_schema_version") {
      std::uint32_t parsed_version = 0;
      if (!ParseU32(value, &parsed_version)) {
        return std::nullopt;
      }
      options.raw_wire_schema_version = parsed_version;
#endif
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
      options.cell.requested_pool_size == 0
#if !defined(APGAR_PHASE4_CONFIRMATORY_OPERATIONAL_WORKER_ACTIVE)
      || options.fixture_path.empty()
#endif
  ) {
    return std::nullopt;
  }
#if defined(APGAR_PHASE4_CONFIRMATORY_OPERATIONAL_WORKER_ACTIVE)
  if (!options.corpus_version.has_value() ||
      *options.corpus_version != apgar::benchmark::kPhase4RepresentativeCorpusVersionV2 ||
      !options.raw_wire_schema_version.has_value() ||
      *options.raw_wire_schema_version != kConfirmatoryRawWireSchemaVersion ||
      !options.runtime_commit.has_value() ||
      !apgar::benchmark::IsFullLowercaseGitCommit(*options.runtime_commit) ||
      options.cell.case_id != kConfirmatoryCaseId ||
      options.cell.requested_pool_size != kConfirmatoryPoolSize ||
      options.cell.preparation_worker_count !=
          apgar::benchmark::kPhase4CanonicalPreparationWorkersV1 ||
      options.cell.repetitions != apgar::benchmark::kPhase4CanonicalRepetitionsV1 ||
      options.cell.maximum_setup_elapsed_nanoseconds != kConfirmatorySetupNanoseconds ||
      options.cell.external_budget.maximum_prepared_elapsed_nanoseconds !=
          kConfirmatoryPreparedNanoseconds ||
      options.cell.external_budget.maximum_cold_elapsed_nanoseconds !=
          kConfirmatoryColdNanoseconds ||
      options.cell.external_budget.maximum_address_space_bytes != kConfirmatoryAddressSpaceBytes ||
      options.cell.external_budget.maximum_peak_host_bytes != kConfirmatoryPeakHostBytes ||
      options.cell.corpus_limits.maximum_nets != kConfirmatoryMaximumNets ||
      options.cell.corpus_limits.maximum_compiled_nodes != kConfirmatoryMaximumCompiledNodes ||
      options.cell.corpus_limits.maximum_compiled_host_bytes !=
          kConfirmatoryMaximumCompiledHostBytes ||
      options.cell.corpus_limits.maximum_active_regions != kConfirmatoryMaximumActiveRegions ||
      options.cell.corpus_limits.maximum_board_entities != kConfirmatoryMaximumBoardEntities) {
    return std::nullopt;
  }
#endif
  return options;
}

void PrintUsage() {
#if defined(APGAR_PHASE4_CONFIRMATORY_H4096_SAME_RUN_OPERATIONAL_WORKER)
  std::cerr << "phase4_confirmatory_h4096_same_run_operational_replay_worker requires "
               "--corpus_version=2 --raw_wire_schema_version=2 --mode=measured|authority "
               "--arm=baseline|candidate --case_id=10100 --pool_size=4 --workers=4 and a clean "
               "--apgar_commit=<40 lowercase hex>; numeric bounds use strict decimal\n";
#elif defined(APGAR_PHASE4_CONFIRMATORY_SAME_RUN_OPERATIONAL_WORKER)
  std::cerr << "phase4_confirmatory_same_run_operational_replay_worker requires "
               "--corpus_version=2 --raw_wire_schema_version=2 --mode=measured|authority "
               "--arm=baseline|candidate --case_id=10100 --pool_size=4 --workers=4 and a clean "
               "--apgar_commit=<40 lowercase hex>; numeric bounds use strict decimal\n";
#elif defined(APGAR_PHASE4_CONFIRMATORY_OPERATIONAL_WORKER_ACTIVE)
  std::cerr << "phase4_confirmatory_operational_replay_worker requires "
               "--corpus_version=2 --raw_wire_schema_version=1 --mode=measured|authority "
               "--arm=baseline|candidate --case_id=10200 --pool_size=4 --workers=4 and a clean "
               "--apgar_commit=<40 lowercase hex>; numeric bounds use strict decimal\n";
#else
  std::cerr << "phase4_operational_replay_worker requires --mode=measured|authority "
               "--arm=baseline|candidate --case_id=N --pool_size=4|8|16 and a clean "
               "--apgar_commit=<40 lowercase hex>; numeric bounds use strict decimal\n";
#endif
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
#if defined(APGAR_PHASE4_CONFIRMATORY_H4096_SAME_RUN_OPERATIONAL_WORKER)
  auto spec_result = apgar::benchmark::internal::BuildPhase4CanonicalTrialSpecForCorpusV2H4096(
      options.cell, 0, apgar::benchmark::Phase4TrialOrder::kBaselineFirst);
  if (std::holds_alternative<apgar::benchmark::Phase4TrialHarnessError>(spec_result)) {
    const auto& error = std::get<apgar::benchmark::Phase4TrialHarnessError>(spec_result);
    std::cerr << error.invariant_id << ": " << error.detail << '\n';
    return 1;
  }
  const auto& spec = std::get<apgar::benchmark::Phase4PairedTrialSpec>(spec_result);
  if (const std::optional<apgar::benchmark::Phase4PairedTrialError> error =
          apgar::benchmark::internal::PreflightPhase4ConfirmatoryH4096SameRunSpec(spec,
                                                                                  *options.arm);
      error.has_value()) {
    std::cerr << error->invariant_id << ": " << error->detail << '\n';
    return 1;
  }
#endif
  const bool publishable =
      options.runtime_commit.has_value() &&
      apgar::benchmark::IsPublishableBenchmarkSource(
          *options.runtime_commit, apgar::benchmark::kPhase3BuiltCommit,
          apgar::benchmark::kPhase3SourceStamped, apgar::benchmark::kPhase3BuiltFromDirtyTree);
#if defined(APGAR_PHASE4_CONFIRMATORY_OPERATIONAL_WORKER_TESTING)
  constexpr bool embedded_source_publishable =
      apgar::benchmark::IsPublishableEmbeddedBenchmarkSource(
          apgar::benchmark::kPhase3BuiltCommit, apgar::benchmark::kPhase3SourceStamped,
          apgar::benchmark::kPhase3BuiltFromDirtyTree);
  if (embedded_source_publishable) {
    std::cerr << "confirmatory operational test worker refuses a publishable source build\n";
    return 2;
  }
#endif
  if (!publishable && !options.testing_allow_unstamped) {
    PrintUsage();
    return 2;
  }
  if (!ExactAddressSpaceLimit(options.cell.external_budget.maximum_address_space_bytes)) {
    std::cerr << "operational worker did not observe the exact RLIMIT_AS contract\n";
    return 2;
  }
#if defined(APGAR_PHASE4_CONFIRMATORY_OPERATIONAL_WORKER_ACTIVE)
  constexpr std::string_view imported_fixture;
#else
  const std::optional<std::string> fixture = apgar::tooling::ReadFile(options.fixture_path);
  if (!fixture.has_value()) {
    std::cerr << "failed to read the imported Phase 4 fixture\n";
    return 2;
  }
  const std::string_view imported_fixture = *fixture;
#endif
#if !defined(APGAR_PHASE4_CONFIRMATORY_H4096_SAME_RUN_OPERATIONAL_WORKER)
#if defined(APGAR_PHASE4_CONFIRMATORY_OPERATIONAL_WORKER_ACTIVE)
  auto spec_result = apgar::benchmark::BuildPhase4CanonicalTrialSpecForCorpusV2(
      options.cell, 0, apgar::benchmark::Phase4TrialOrder::kBaselineFirst);
#else
  auto spec_result = apgar::benchmark::BuildPhase4CanonicalTrialSpecV1(
      options.cell, 0, apgar::benchmark::Phase4TrialOrder::kBaselineFirst);
#endif
  if (std::holds_alternative<apgar::benchmark::Phase4TrialHarnessError>(spec_result)) {
    const auto& error = std::get<apgar::benchmark::Phase4TrialHarnessError>(spec_result);
    std::cerr << error.invariant_id << ": " << error.detail << '\n';
    return 1;
  }
  const auto& spec = std::get<apgar::benchmark::Phase4PairedTrialSpec>(spec_result);
#endif
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
#if defined(APGAR_PHASE4_CONFIRMATORY_H4096_SAME_RUN_OPERATIONAL_WORKER)
  auto warmup =
      apgar::benchmark::internal::ExecutePhase4ConfirmatoryH4096SameRunTrialArmForOperationalWarmup(
          *options.arm, spec, imported_fixture, preparer.get());
#elif defined(APGAR_PHASE4_CONFIRMATORY_OPERATIONAL_WORKER_ACTIVE)
  auto warmup = apgar::benchmark::ExecutePhase4TrialArmForCorpusV2(
      *options.arm, spec, imported_fixture, preparer.get());
#else
  auto warmup = apgar::benchmark::ExecutePhase4TrialArmV1(*options.arm, spec, imported_fixture,
                                                          preparer.get());
#endif
  if (std::holds_alternative<apgar::benchmark::Phase4TrialArmFailure>(warmup)) {
    const auto& failure = std::get<apgar::benchmark::Phase4TrialArmFailure>(warmup);
    std::cerr << failure.summary.invariant_id << ": " << failure.summary.detail << '\n';
    return 1;
  }
#if defined(APGAR_PHASE4_CONFIRMATORY_OPERATIONAL_WORKER_TESTING)
  const std::string source_commit =
      apgar::benchmark::kPhase3BuiltCommit.empty()
          ? std::string(apgar::benchmark::kFullGitCommitHexCharacters, '0')
          : std::string(apgar::benchmark::kPhase3BuiltCommit);
#else
  const std::string source_commit = options.runtime_commit.value_or(
      apgar::benchmark::kPhase3BuiltCommit.empty()
          ? std::string(apgar::benchmark::kFullGitCommitHexCharacters, '0')
          : std::string(apgar::benchmark::kPhase3BuiltCommit));
#endif
  constexpr bool source_stamped = apgar::benchmark::kPhase3SourceStamped;
  constexpr bool source_tree_dirty = apgar::benchmark::kPhase3BuiltFromDirtyTree;
  std::optional<std::string> serialized;
  if (*options.mode == Mode::kMeasured) {
#if defined(APGAR_PHASE4_CONFIRMATORY_H4096_SAME_RUN_OPERATIONAL_WORKER)
    auto result =
        apgar::benchmark::internal::ExecutePhase4ConfirmatoryH4096SameRunTrialArmOperationalProfile(
            *options.arm, spec, imported_fixture, preparer.get());
#elif defined(APGAR_PHASE4_CONFIRMATORY_OPERATIONAL_WORKER_ACTIVE)
    auto result = apgar::benchmark::ExecutePhase4TrialArmOperationalProfileForCorpusV2(
        *options.arm, spec, imported_fixture, preparer.get());
#else
    auto result = apgar::benchmark::ExecutePhase4TrialArmOperationalProfileV1(
        *options.arm, spec, imported_fixture, preparer.get());
#endif
    if (std::holds_alternative<apgar::benchmark::Phase4TrialArmFailure>(result)) {
      const auto& failure = std::get<apgar::benchmark::Phase4TrialArmFailure>(result);
      std::cerr << failure.summary.invariant_id << ": " << failure.summary.detail << '\n';
      return 1;
    }
#if defined(APGAR_PHASE4_CONFIRMATORY_OPERATIONAL_WORKER_ACTIVE)
    serialized = apgar::benchmark::SerializePhase4OperationalProfileWorkerJsonForCorpusV2(
        std::get<apgar::benchmark::Phase4TrialArmOperationalProfileV1>(result), source_commit,
        source_stamped, source_tree_dirty);
#else
    serialized = apgar::benchmark::SerializePhase4OperationalProfileWorkerJsonV1(
        std::get<apgar::benchmark::Phase4TrialArmOperationalProfileV1>(result), source_commit,
        source_stamped, source_tree_dirty);
#endif
  } else {
#if defined(APGAR_PHASE4_CONFIRMATORY_H4096_SAME_RUN_OPERATIONAL_WORKER)
    auto result =
        apgar::benchmark::internal::ExecutePhase4ConfirmatoryH4096SameRunTrialArmReplayAuthority(
            *options.arm, spec, imported_fixture, preparer.get());
#elif defined(APGAR_PHASE4_CONFIRMATORY_OPERATIONAL_WORKER_ACTIVE)
    auto result = apgar::benchmark::ExecutePhase4TrialArmReplayAuthorityForCorpusV2(
        *options.arm, spec, imported_fixture, preparer.get());
#else
    auto result = apgar::benchmark::ExecutePhase4TrialArmReplayAuthorityV1(
        *options.arm, spec, imported_fixture, preparer.get());
#endif
    if (std::holds_alternative<apgar::benchmark::Phase4TrialArmFailure>(result)) {
      const auto& failure = std::get<apgar::benchmark::Phase4TrialArmFailure>(result);
      std::cerr << failure.summary.invariant_id << ": " << failure.summary.detail << '\n';
      return 1;
    }
#if defined(APGAR_PHASE4_CONFIRMATORY_OPERATIONAL_WORKER_ACTIVE)
    serialized = apgar::benchmark::SerializePhase4ReplayAuthorityWorkerJsonForCorpusV2(
        std::get<apgar::benchmark::Phase4TrialArmReplayAuthorityV1>(result), source_commit,
        source_stamped, source_tree_dirty);
#else
    serialized = apgar::benchmark::SerializePhase4ReplayAuthorityWorkerJsonV1(
        std::get<apgar::benchmark::Phase4TrialArmReplayAuthorityV1>(result), source_commit,
        source_stamped, source_tree_dirty);
#endif
  }
  if (!serialized.has_value() || !WriteStdout(*serialized)) {
    std::cerr << "failed to serialize operational worker output\n";
    return 2;
  }
  return 0;
}
