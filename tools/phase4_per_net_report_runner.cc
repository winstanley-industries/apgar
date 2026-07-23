#include <array>
#include <charconv>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_set>
#include <utility>
#include <variant>

#include "apgar/allocator/cpu_candidate_pool_preparation.h"
#include "apgar/benchmark/phase3_commit.h"
#include "apgar/benchmark/phase3_source_stamp.h"
#include "apgar/benchmark/phase4_per_net_report_artifact.h"
#include "apgar/tooling/runfiles.h"

namespace {

using apgar::benchmark::Phase4CanonicalCellConfig;

struct Options {
  bool testing_allow_unstamped = false;
  std::optional<std::string> runtime_commit;
  std::uint32_t raw_wire_schema_version = apgar::benchmark::kPhase4TrialWireSchemaVersion;
  Phase4CanonicalCellConfig cell;
  std::uint64_t raw_cell_plan_checksum = 0;
  std::uint64_t raw_cell_artifact_checksum = 0;
  std::uint64_t raw_source_envelope_checksum = 0;
  apgar::benchmark::Phase4PerNetReportRawReferenceV1 raw_reference;
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
  if (argc < 2 || argc > 28) {
    return std::nullopt;
  }
  Options options;
  options.cell.preparation_worker_count = apgar::benchmark::kPhase4CanonicalPreparationWorkersV1;
  options.cell.repetitions = apgar::benchmark::kPhase4CanonicalRepetitionsV1;
  options.cell.maximum_setup_elapsed_nanoseconds = 300'000'000'000ULL;
  options.cell.external_budget.maximum_prepared_elapsed_nanoseconds = 300'000'000'000ULL;
  options.cell.external_budget.maximum_cold_elapsed_nanoseconds = 300'000'000'000ULL;
  options.cell.external_budget.maximum_address_space_bytes = 64ULL * 1024ULL * 1024ULL * 1024ULL;
  options.cell.external_budget.maximum_peak_host_bytes = 16ULL * 1024ULL * 1024ULL * 1024ULL;
  options.raw_reference.execution_order = apgar::benchmark::Phase4TrialOrder::kBaselineFirst;

  std::unordered_set<std::string> seen;
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    if (argument.size() > 128) {
      return std::nullopt;
    }
    const std::size_t separator = argument.find('=');
    if (!argument.starts_with("--") || separator == std::string_view::npos || separator == 2) {
      return std::nullopt;
    }
    const std::string key(argument.substr(2, separator - 2));
    const std::string_view value = argument.substr(separator + 1);
    if (key.size() > 64 || value.size() > 64 || !seen.insert(key).second) {
      return std::nullopt;
    }
    if (key == "testing_allow_unstamped") {
#ifdef APGAR_PHASE4_REPORT_RUNNER_TESTING
      options.testing_allow_unstamped = value == "1";
      if (!options.testing_allow_unstamped) {
        return std::nullopt;
      }
#else
      return std::nullopt;
#endif
    } else if (key == "apgar_commit") {
      if (value.size() > apgar::benchmark::kFullGitCommitHexCharacters) {
        return std::nullopt;
      }
      options.runtime_commit = std::string(value);
    } else if (key == "raw_wire_schema_version") {
      if (!ParseU32(value, &options.raw_wire_schema_version)) {
        return std::nullopt;
      }
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
    } else if (key == "raw_cell_plan_checksum") {
      if (!ParseUnsigned(value, &options.raw_cell_plan_checksum)) {
        return std::nullopt;
      }
    } else if (key == "raw_cell_artifact_checksum") {
      if (!ParseUnsigned(value, &options.raw_cell_artifact_checksum)) {
        return std::nullopt;
      }
    } else if (key == "raw_source_envelope_checksum") {
      if (!ParseUnsigned(value, &options.raw_source_envelope_checksum)) {
        return std::nullopt;
      }
    } else if (key == "pair_attempt_checksum") {
      if (!ParseUnsigned(value, &options.raw_reference.pair_attempt_checksum)) {
        return std::nullopt;
      }
    } else if (key == "paired_semantic_checksum") {
      if (!ParseUnsigned(value, &options.raw_reference.paired_semantic_checksum)) {
        return std::nullopt;
      }
    } else if (key == "paired_artifact_checksum") {
      if (!ParseUnsigned(value, &options.raw_reference.paired_artifact_checksum)) {
        return std::nullopt;
      }
    } else if (key == "baseline_semantic_checksum") {
      if (!ParseUnsigned(value, &options.raw_reference.baseline_semantic_checksum)) {
        return std::nullopt;
      }
    } else if (key == "baseline_arm_artifact_checksum") {
      if (!ParseUnsigned(value, &options.raw_reference.baseline_arm_artifact_checksum)) {
        return std::nullopt;
      }
    } else if (key == "candidate_semantic_checksum") {
      if (!ParseUnsigned(value, &options.raw_reference.candidate_semantic_checksum)) {
        return std::nullopt;
      }
    } else if (key == "candidate_arm_artifact_checksum") {
      if (!ParseUnsigned(value, &options.raw_reference.candidate_arm_artifact_checksum)) {
        return std::nullopt;
      }
    } else {
      return std::nullopt;
    }
  }
  constexpr std::array<std::string_view, 25> kRequired = {
      "apgar_commit",
      "case_id",
      "pool_size",
      "workers",
      "repetitions",
      "setup_ns",
      "prepared_ns",
      "cold_ns",
      "address_space_bytes",
      "peak_host_bytes",
      "maximum_nets",
      "maximum_compiled_nodes",
      "maximum_compiled_host_bytes",
      "maximum_active_regions",
      "maximum_board_entities",
      "raw_cell_plan_checksum",
      "raw_cell_artifact_checksum",
      "raw_source_envelope_checksum",
      "pair_attempt_checksum",
      "paired_semantic_checksum",
      "paired_artifact_checksum",
      "baseline_semantic_checksum",
      "baseline_arm_artifact_checksum",
      "candidate_semantic_checksum",
      "candidate_arm_artifact_checksum",
  };
  for (const std::string_view required : kRequired) {
    if (!seen.contains(std::string(required))) {
      return std::nullopt;
    }
  }
  return options;
}

void PrintUsage() {
  std::cerr << "phase4_per_net_report_runner requires a complete canonical cell, clean "
               "--apgar_commit=<40 lowercase hex>, and every repetition-zero Raw reference "
               "checksum as strict --name=decimal arguments.\n";
}

[[nodiscard]] int PrintArmFailure(const apgar::benchmark::Phase4TrialArmFailure& failure) {
  std::cerr << failure.summary.invariant_id << ": " << failure.summary.detail << '\n';
  return 1;
}

}  // namespace

int main(int argc, char** argv) try {
  std::optional<Options> parsed = ParseOptions(argc, argv);
  if (!parsed.has_value()) {
    PrintUsage();
    return 2;
  }
  Options& options = *parsed;
  if (!options.runtime_commit.has_value() ||
      !apgar::benchmark::IsFullLowercaseGitCommit(*options.runtime_commit) ||
      options.cell.case_id == 0 || options.cell.requested_pool_size == 0 ||
      options.cell.preparation_worker_count !=
          apgar::benchmark::kPhase4CanonicalPreparationWorkersV1 ||
      options.cell.repetitions != apgar::benchmark::kPhase4CanonicalRepetitionsV1 ||
      apgar::benchmark::FindPhase4WorkloadNetRosterManifestEntryV1(options.cell.case_id) ==
          nullptr ||
      options.raw_cell_plan_checksum == 0 || options.raw_cell_artifact_checksum == 0 ||
      options.raw_source_envelope_checksum == 0 ||
      options.raw_reference.pair_attempt_checksum == 0 ||
      options.raw_reference.paired_semantic_checksum == 0 ||
      options.raw_reference.paired_artifact_checksum == 0 ||
      options.raw_reference.baseline_semantic_checksum == 0 ||
      options.raw_reference.baseline_arm_artifact_checksum == 0 ||
      options.raw_reference.candidate_semantic_checksum == 0 ||
      options.raw_reference.candidate_arm_artifact_checksum == 0) {
    PrintUsage();
    return 2;
  }
  if (options.raw_wire_schema_version != apgar::benchmark::kPhase4TrialWireSchemaVersion &&
      options.raw_wire_schema_version != apgar::benchmark::kPhase4SameRunTrialWireSchemaVersion) {
    PrintUsage();
    return 2;
  }
  bool source_stamped = apgar::benchmark::kPhase3SourceStamped;
  bool source_tree_dirty = apgar::benchmark::kPhase3BuiltFromDirtyTree;
  const bool publishable = apgar::benchmark::IsPublishableBenchmarkSource(
      *options.runtime_commit, apgar::benchmark::kPhase3BuiltCommit, source_stamped,
      source_tree_dirty);
#ifdef APGAR_PHASE4_REPORT_RUNNER_TESTING
  if (!publishable && options.testing_allow_unstamped) {
    source_stamped = true;
    source_tree_dirty = false;
  } else
#endif
      if (!publishable) {
    PrintUsage();
    return 2;
  }
  if (options.raw_cell_plan_checksum !=
          apgar::benchmark::ComputePhase4CanonicalCellPlanChecksumV1(options.cell) ||
      options.raw_source_envelope_checksum !=
          (options.raw_wire_schema_version == apgar::benchmark::kPhase4SameRunTrialWireSchemaVersion
               ? apgar::benchmark::ComputePhase4SourceEnvelopeChecksumV2(
                     apgar::benchmark::kPhase4SameRunRawEvidenceSchemaVersion,
                     options.raw_wire_schema_version, *options.runtime_commit, source_stamped,
                     source_tree_dirty, options.raw_cell_artifact_checksum)
               : apgar::benchmark::ComputePhase4SourceEnvelopeChecksumV1(
                     options.raw_wire_schema_version, *options.runtime_commit, source_stamped,
                     source_tree_dirty, options.raw_cell_artifact_checksum))) {
    std::cerr << "raw cell plan or source-envelope association failed before diagnostics\n";
    return 2;
  }

  const std::optional<std::string> fixture = apgar::tooling::ReadFile(
      apgar::tooling::ResolveRunfile("tests/fixtures/phase4_supported_multinet_v1.kicad_pcb"));
  if (!fixture.has_value()) {
    std::cerr << "failed to authenticate the imported Phase 4 fixture runfile\n";
    return 2;
  }
  apgar::benchmark::Phase4CanonicalSpecResult spec_result =
      apgar::benchmark::BuildPhase4CanonicalTrialSpecV1(
          options.cell, 0, apgar::benchmark::Phase4TrialOrder::kBaselineFirst);
  if (!std::holds_alternative<apgar::benchmark::Phase4PairedTrialSpec>(spec_result)) {
    const auto& error = std::get<apgar::benchmark::Phase4TrialHarnessError>(spec_result);
    std::cerr << error.invariant_id << ": " << error.detail << '\n';
    return 2;
  }
  const auto& spec = std::get<apgar::benchmark::Phase4PairedTrialSpec>(spec_result);
  auto baseline_result = apgar::benchmark::ExecutePhase4TrialArmDiagnosticV1(
      apgar::benchmark::Phase4TrialArm::kSequentialBaseline, spec, *fixture);
  if (std::holds_alternative<apgar::benchmark::Phase4TrialArmFailure>(baseline_result)) {
    return PrintArmFailure(std::get<apgar::benchmark::Phase4TrialArmFailure>(baseline_result));
  }
  auto preparer_result = apgar::allocator::CreatePersistentCpuCandidatePoolPreparer(
      {.worker_count = options.cell.preparation_worker_count});
  if (std::holds_alternative<apgar::allocator::CpuCandidatePoolPreparationError>(preparer_result)) {
    const auto& error =
        std::get<apgar::allocator::CpuCandidatePoolPreparationError>(preparer_result);
    std::cerr << error.invariant_id << ": " << error.detail << '\n';
    return 1;
  }
  std::unique_ptr<apgar::allocator::PersistentCpuCandidatePoolPreparer> preparer =
      std::get<std::unique_ptr<apgar::allocator::PersistentCpuCandidatePoolPreparer>>(
          std::move(preparer_result));
  auto candidate_result = apgar::benchmark::ExecutePhase4TrialArmDiagnosticV1(
      apgar::benchmark::Phase4TrialArm::kReusableCandidateAllocation, spec, *fixture,
      preparer.get());
  if (std::holds_alternative<apgar::benchmark::Phase4TrialArmFailure>(candidate_result)) {
    return PrintArmFailure(std::get<apgar::benchmark::Phase4TrialArmFailure>(candidate_result));
  }
  std::array<apgar::benchmark::Phase4TrialArmDiagnosticExecutionV1, 2> diagnostics = {
      std::get<apgar::benchmark::Phase4TrialArmDiagnosticExecutionV1>(std::move(baseline_result)),
      std::get<apgar::benchmark::Phase4TrialArmDiagnosticExecutionV1>(std::move(candidate_result)),
  };
  auto artifact_result = apgar::benchmark::BuildPhase4PerNetReportArtifactV1(
      options.cell, *options.runtime_commit, source_stamped, source_tree_dirty,
      options.raw_cell_plan_checksum, options.raw_cell_artifact_checksum,
      options.raw_source_envelope_checksum, options.raw_reference, std::move(diagnostics), *fixture,
      options.raw_wire_schema_version);
  if (std::holds_alternative<apgar::benchmark::Phase4PerNetReportArtifactError>(artifact_result)) {
    const auto& error =
        std::get<apgar::benchmark::Phase4PerNetReportArtifactError>(artifact_result);
    std::cerr << error.invariant_id << ": " << error.detail << '\n';
    return 1;
  }
  std::cout << apgar::benchmark::SerializePhase4PerNetReportArtifactJsonV1(
      std::get<apgar::benchmark::Phase4PerNetReportArtifactV1>(artifact_result));
  return 0;
} catch (const std::bad_alloc&) {
  std::cerr << "host allocation failed in the per-net report runner\n";
  return 2;
} catch (const std::exception&) {
  std::cerr << "unexpected standard exception in the per-net report runner\n";
  return 2;
} catch (...) {
  std::cerr << "unexpected non-standard exception in the per-net report runner\n";
  return 2;
}
