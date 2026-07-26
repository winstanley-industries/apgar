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
#include "apgar/benchmark/phase4_exact_small_snapshot.h"
#if !defined(APGAR_PHASE4_CONFIRMATORY_H4096_EXACT_SNAPSHOT_RUNNER)
#include "apgar/tooling/runfiles.h"
#endif
#if defined(APGAR_PHASE4_CONFIRMATORY_H4096_EXACT_SNAPSHOT_RUNNER)
#include "src/benchmark/phase4_confirmatory_h4096_exact_small_snapshot_internal.h"
#include "src/benchmark/phase4_h4096_canonical_budget_internal.h"
#include "src/benchmark/phase4_paired_trial_internal.h"
#endif

#if defined(APGAR_PHASE4_CONFIRMATORY_H4096_EXACT_SNAPSHOT_RUNNER) && \
    defined(APGAR_PHASE4_CONFIRMATORY_EXACT_SNAPSHOT_RUNNER)
#error "H=2250 and H=4096 exact-small snapshot authorities are mutually exclusive"
#endif

namespace {

struct Options {
  bool testing_allow_unstamped = false;
  std::optional<std::string> runtime_commit;
  std::uint32_t corpus_version = apgar::benchmark::kPhase4RepresentativeCorpusVersion;
  std::uint32_t raw_evidence_schema_version = 1;
  std::uint32_t raw_wire_schema_version = apgar::benchmark::kPhase4TrialWireSchemaVersion;
  apgar::benchmark::Phase4CanonicalCellConfig cell;
  std::uint64_t raw_cell_plan_checksum = 0;
  std::uint64_t raw_cell_artifact_checksum = 0;
  std::uint64_t raw_source_envelope_checksum = 0;
  apgar::benchmark::Phase4PerNetReportRawReferenceV1 raw_reference;
  std::uint64_t per_net_report_artifact_checksum = 0;
  std::uint64_t per_net_report_source_envelope_checksum = 0;
#ifdef APGAR_PHASE4_EXACT_SNAPSHOT_RUNNER_TESTING
  std::optional<std::uint64_t> testing_maximum_serialized_bytes;
#endif
};

[[nodiscard]] bool ParseU64(std::string_view text, std::uint64_t* output) noexcept {
  if (text.empty() || (text.size() > 1 && text.front() == '0')) return false;
  const char* begin = text.data();
  const auto [end, error] = std::from_chars(begin, begin + text.size(), *output);
  return error == std::errc{} && end == begin + text.size();
}

[[nodiscard]] bool ParseU32(std::string_view text, std::uint32_t* output) noexcept {
  std::uint64_t wide = 0;
  if (!ParseU64(text, &wide) || wide > std::numeric_limits<std::uint32_t>::max()) return false;
  *output = static_cast<std::uint32_t>(wide);
  return true;
}

[[nodiscard]] std::optional<Options> ParseOptions(int argc, char** argv) {
#ifdef APGAR_PHASE4_EXACT_SNAPSHOT_RUNNER_TESTING
  constexpr int kMaximumArgumentCount = 33;
#else
  constexpr int kMaximumArgumentCount = 32;
#endif
  if (argc < 2 || argc > kMaximumArgumentCount) return std::nullopt;
  Options options;
  options.raw_reference.execution_order = apgar::benchmark::Phase4TrialOrder::kBaselineFirst;
  std::unordered_set<std::string> seen;
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    const std::size_t separator = argument.find('=');
    if (argument.size() > 160 || !argument.starts_with("--") ||
        separator == std::string_view::npos || separator == 2)
      return std::nullopt;
    const std::string key(argument.substr(2, separator - 2));
    const std::string_view value = argument.substr(separator + 1);
    if (key.size() > 64 || value.size() > 64 || !seen.insert(key).second) return std::nullopt;
    bool valid = true;
    if (key == "testing_allow_unstamped") {
#ifdef APGAR_PHASE4_EXACT_SNAPSHOT_RUNNER_TESTING
      options.testing_allow_unstamped = value == "1";
      valid = options.testing_allow_unstamped;
#else
      valid = false;
#endif
#ifdef APGAR_PHASE4_EXACT_SNAPSHOT_RUNNER_TESTING
    } else if (key == "testing_maximum_serialized_bytes") {
      std::uint64_t parsed_cap = 0;
      valid = ParseU64(value, &parsed_cap) && parsed_cap != 0 &&
              parsed_cap <= apgar::benchmark::kPhase4ExactSmallMaximumSerializedBytesV1;
      if (valid) options.testing_maximum_serialized_bytes = parsed_cap;
#endif
    } else if (key == "apgar_commit") {
      options.runtime_commit = std::string(value);
    } else if (key == "corpus_version") {
#if defined(APGAR_PHASE4_CONFIRMATORY_EXACT_SNAPSHOT_RUNNER) || \
    defined(APGAR_PHASE4_CONFIRMATORY_H4096_EXACT_SNAPSHOT_RUNNER)
      valid = ParseU32(value, &options.corpus_version);
#else
      valid = false;
#endif
    } else if (key == "raw_evidence_schema_version")
      valid = ParseU32(value, &options.raw_evidence_schema_version);
    else if (key == "raw_wire_schema_version") {
#if defined(APGAR_PHASE4_CONFIRMATORY_EXACT_SNAPSHOT_RUNNER) || \
    defined(APGAR_PHASE4_CONFIRMATORY_H4096_EXACT_SNAPSHOT_RUNNER)
      valid = ParseU32(value, &options.raw_wire_schema_version);
#else
      valid = false;
#endif
    } else if (key == "case_id")
      valid = ParseU32(value, &options.cell.case_id);
    else if (key == "pool_size")
      valid = ParseU32(value, &options.cell.requested_pool_size);
    else if (key == "workers")
      valid = ParseU32(value, &options.cell.preparation_worker_count);
    else if (key == "repetitions")
      valid = ParseU32(value, &options.cell.repetitions);
    else if (key == "setup_ns")
      valid = ParseU64(value, &options.cell.maximum_setup_elapsed_nanoseconds);
    else if (key == "prepared_ns")
      valid = ParseU64(value, &options.cell.external_budget.maximum_prepared_elapsed_nanoseconds);
    else if (key == "cold_ns")
      valid = ParseU64(value, &options.cell.external_budget.maximum_cold_elapsed_nanoseconds);
    else if (key == "address_space_bytes")
      valid = ParseU64(value, &options.cell.external_budget.maximum_address_space_bytes);
    else if (key == "peak_host_bytes")
      valid = ParseU64(value, &options.cell.external_budget.maximum_peak_host_bytes);
    else if (key == "maximum_nets")
      valid = ParseU64(value, &options.cell.corpus_limits.maximum_nets);
    else if (key == "maximum_compiled_nodes")
      valid = ParseU64(value, &options.cell.corpus_limits.maximum_compiled_nodes);
    else if (key == "maximum_compiled_host_bytes")
      valid = ParseU64(value, &options.cell.corpus_limits.maximum_compiled_host_bytes);
    else if (key == "maximum_active_regions")
      valid = ParseU64(value, &options.cell.corpus_limits.maximum_active_regions);
    else if (key == "maximum_board_entities")
      valid = ParseU64(value, &options.cell.corpus_limits.maximum_board_entities);
    else if (key == "raw_cell_plan_checksum")
      valid = ParseU64(value, &options.raw_cell_plan_checksum);
    else if (key == "raw_cell_artifact_checksum")
      valid = ParseU64(value, &options.raw_cell_artifact_checksum);
    else if (key == "raw_source_envelope_checksum")
      valid = ParseU64(value, &options.raw_source_envelope_checksum);
    else if (key == "pair_attempt_checksum")
      valid = ParseU64(value, &options.raw_reference.pair_attempt_checksum);
    else if (key == "paired_semantic_checksum")
      valid = ParseU64(value, &options.raw_reference.paired_semantic_checksum);
    else if (key == "paired_artifact_checksum")
      valid = ParseU64(value, &options.raw_reference.paired_artifact_checksum);
    else if (key == "baseline_semantic_checksum")
      valid = ParseU64(value, &options.raw_reference.baseline_semantic_checksum);
    else if (key == "baseline_arm_artifact_checksum")
      valid = ParseU64(value, &options.raw_reference.baseline_arm_artifact_checksum);
    else if (key == "candidate_semantic_checksum")
      valid = ParseU64(value, &options.raw_reference.candidate_semantic_checksum);
    else if (key == "candidate_arm_artifact_checksum")
      valid = ParseU64(value, &options.raw_reference.candidate_arm_artifact_checksum);
    else if (key == "per_net_report_artifact_checksum")
      valid = ParseU64(value, &options.per_net_report_artifact_checksum);
    else if (key == "per_net_report_source_envelope_checksum")
      valid = ParseU64(value, &options.per_net_report_source_envelope_checksum);
    else
      valid = false;
    if (!valid) return std::nullopt;
  }
  constexpr std::string_view required[] = {"apgar_commit",
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
                                           "per_net_report_artifact_checksum",
                                           "per_net_report_source_envelope_checksum"};
  for (const std::string_view key : required)
    if (!seen.contains(std::string(key))) return std::nullopt;
#if defined(APGAR_PHASE4_CONFIRMATORY_EXACT_SNAPSHOT_RUNNER) || \
    defined(APGAR_PHASE4_CONFIRMATORY_H4096_EXACT_SNAPSHOT_RUNNER)
  if (!seen.contains("corpus_version") || !seen.contains("raw_evidence_schema_version") ||
      !seen.contains("raw_wire_schema_version")) {
    return std::nullopt;
  }
#endif
  return options;
}

void Usage() {
#ifdef APGAR_PHASE4_CONFIRMATORY_H4096_EXACT_SNAPSHOT_RUNNER
  std::cerr << "phase4_confirmatory_h4096_exact_small_snapshot_runner requires explicit Corpus 2, "
               "Raw schema 2/Wire 2, exact development cell (10100,4), the complete canonical "
               "H=4096 cell, a clean stamped source, complete Raw repetition-zero references, "
               "and the associated H=4096 per-net report artifact/envelope checksums.\n";
#elif defined(APGAR_PHASE4_CONFIRMATORY_EXACT_SNAPSHOT_RUNNER)
  std::cerr << "phase4_confirmatory_exact_small_snapshot_runner requires explicit Corpus 2, "
               "Raw schema 2/Wire 2, exact development cell (10100,4), a clean stamped source, "
               "complete Raw repetition-zero references, and the associated per-net report "
               "artifact/envelope checksums.\n";
#else
  std::cerr << "phase4_exact_small_snapshot_runner requires canonical exact case 100/101/102, "
               "pool 4, a clean stamped source, complete Raw repetition-zero references, and "
               "the associated per-net report artifact/envelope checksums.\n";
#endif
}

}  // namespace

int main(int argc, char** argv) try {
  std::optional<Options> parsed = ParseOptions(argc, argv);
  if (!parsed.has_value()) {
    Usage();
    return 2;
  }
  Options& options = *parsed;
#if defined(APGAR_PHASE4_CONFIRMATORY_EXACT_SNAPSHOT_RUNNER) || \
    defined(APGAR_PHASE4_CONFIRMATORY_H4096_EXACT_SNAPSHOT_RUNNER)
  const bool corpus_scope_valid =
      options.corpus_version == apgar::benchmark::kPhase4RepresentativeCorpusVersionV2 &&
      options.raw_evidence_schema_version ==
          apgar::benchmark::kPhase4SameRunRawEvidenceSchemaVersion &&
      options.raw_wire_schema_version == apgar::benchmark::kPhase4SameRunTrialWireSchemaVersion &&
      options.cell.case_id == 10'100;
#else
  const bool corpus_scope_valid =
      options.corpus_version == apgar::benchmark::kPhase4RepresentativeCorpusVersion &&
      options.raw_wire_schema_version == apgar::benchmark::kPhase4TrialWireSchemaVersion &&
      (options.cell.case_id == 100 || options.cell.case_id == 101 || options.cell.case_id == 102);
#endif
  if (!options.runtime_commit.has_value() ||
      !apgar::benchmark::IsFullLowercaseGitCommit(*options.runtime_commit) || !corpus_scope_valid ||
      options.cell.requested_pool_size != 4 ||
      options.cell.preparation_worker_count !=
          apgar::benchmark::kPhase4CanonicalPreparationWorkersV1 ||
      options.cell.repetitions != apgar::benchmark::kPhase4CanonicalRepetitionsV1 ||
      options.raw_cell_plan_checksum == 0 || options.raw_cell_artifact_checksum == 0 ||
      options.raw_source_envelope_checksum == 0 || options.raw_reference.repetition_index != 0 ||
      options.raw_reference.execution_order != apgar::benchmark::Phase4TrialOrder::kBaselineFirst ||
      options.raw_reference.pair_attempt_checksum == 0 ||
      options.raw_reference.paired_semantic_checksum == 0 ||
      options.raw_reference.paired_artifact_checksum == 0 ||
      options.raw_reference.baseline_semantic_checksum == 0 ||
      options.raw_reference.baseline_arm_artifact_checksum == 0 ||
      options.raw_reference.candidate_semantic_checksum == 0 ||
      options.raw_reference.candidate_arm_artifact_checksum == 0 ||
      options.per_net_report_artifact_checksum == 0 ||
      options.per_net_report_source_envelope_checksum == 0) {
    Usage();
    return 2;
  }
  if (options.raw_evidence_schema_version != 1 &&
      options.raw_evidence_schema_version !=
          apgar::benchmark::kPhase4SameRunRawEvidenceSchemaVersion) {
    Usage();
    return 2;
  }
#ifdef APGAR_PHASE4_CONFIRMATORY_H4096_EXACT_SNAPSHOT_RUNNER
  auto spec_result = apgar::benchmark::internal::BuildPhase4CanonicalTrialSpecForCorpusV2H4096(
      options.cell, 0, apgar::benchmark::Phase4TrialOrder::kBaselineFirst);
  if (!std::holds_alternative<apgar::benchmark::Phase4PairedTrialSpec>(spec_result)) {
    const auto& error = std::get<apgar::benchmark::Phase4TrialHarnessError>(spec_result);
    std::cerr << error.invariant_id << ": " << error.detail << '\n';
    return 2;
  }
  const auto& spec = std::get<apgar::benchmark::Phase4PairedTrialSpec>(spec_result);
  if (std::optional<apgar::benchmark::Phase4PairedTrialError> error =
          apgar::benchmark::internal::PreflightPhase4ConfirmatoryH4096SameRunSpec(
              spec, apgar::benchmark::Phase4TrialArm::kReusableCandidateAllocation);
      error.has_value()) {
    std::cerr << error->invariant_id << ": " << error->detail << '\n';
    return 2;
  }
  const apgar::benchmark::Phase4CaseDescriptor* descriptor =
      apgar::benchmark::FindPhase4CaseDescriptorForAuthority(
          apgar::benchmark::Phase4RepresentativeCorpusAuthority::kV2, options.cell.case_id);
  constexpr std::uint64_t kExpectedPairedBudgetChecksum = 5'851'813'264'366'095'594ULL;
  const std::uint64_t paired_budget_checksum =
      descriptor == nullptr || spec.candidate_session_config.schedules.empty()
          ? 0
          : apgar::benchmark::internal::ComputePhase4PairedBudgetChecksumForAuthorityV1(
                apgar::benchmark::Phase4RepresentativeCorpusAuthority::kV2, spec,
                apgar::benchmark::Phase4RouteOpportunity{
                    .route_queries = spec.baseline_config.limits.maximum_route_queries,
                    .route_work_units = spec.baseline_config.limits.maximum_total_route_work_units,
                },
                descriptor->requested_net_count,
                spec.candidate_session_config.regeneration_plan_config.maximum_total_columns,
                spec.candidate_session_config.schedules.back().maximum_selection_rounds);
  if (options.cell.maximum_setup_elapsed_nanoseconds != 300'000'000'000ULL ||
      paired_budget_checksum != kExpectedPairedBudgetChecksum) {
    std::cerr << "the H=4096 exact snapshot cell differs from its frozen setup or paired-budget "
                 "authority\n";
    return 2;
  }
#ifdef APGAR_PHASE4_EXACT_SNAPSHOT_RUNNER_TESTING
  if (!options.testing_allow_unstamped) {
    Usage();
    return 2;
  }
  std::cerr << "the H=4096 exact-small snapshot test runner is preflight-only and cannot access "
               "a board fixture, create a preparer, execute a candidate arm, or emit a snapshot\n";
  return 2;
#endif
#endif
  bool source_stamped = apgar::benchmark::kPhase3SourceStamped;
  bool source_tree_dirty = apgar::benchmark::kPhase3BuiltFromDirtyTree;
  const bool publishable = apgar::benchmark::IsPublishableBenchmarkSource(
      *options.runtime_commit, apgar::benchmark::kPhase3BuiltCommit, source_stamped,
      source_tree_dirty);
#ifdef APGAR_PHASE4_EXACT_SNAPSHOT_RUNNER_TESTING
  if (!publishable && options.testing_allow_unstamped) {
    source_stamped = true;
    source_tree_dirty = false;
  } else
#endif
      if (!publishable) {
    Usage();
    return 2;
  }
#if defined(APGAR_PHASE4_CONFIRMATORY_EXACT_SNAPSHOT_RUNNER) || \
    defined(APGAR_PHASE4_CONFIRMATORY_H4096_EXACT_SNAPSHOT_RUNNER)
  const std::uint64_t expected_cell_plan_checksum =
      apgar::benchmark::ComputePhase4CanonicalCellPlanChecksumForCorpusV2(options.cell);
#else
  const std::uint64_t expected_cell_plan_checksum =
      apgar::benchmark::ComputePhase4CanonicalCellPlanChecksumV1(options.cell);
#endif
  if (options.raw_cell_plan_checksum != expected_cell_plan_checksum ||
      options.raw_source_envelope_checksum !=
          (options.raw_evidence_schema_version ==
                   apgar::benchmark::kPhase4SameRunRawEvidenceSchemaVersion
               ? apgar::benchmark::ComputePhase4SourceEnvelopeChecksumV2(
                     apgar::benchmark::kPhase4SameRunRawEvidenceSchemaVersion,
                     apgar::benchmark::kPhase4SameRunTrialWireSchemaVersion,
                     *options.runtime_commit, source_stamped, source_tree_dirty,
                     options.raw_cell_artifact_checksum)
               : apgar::benchmark::ComputePhase4SourceEnvelopeChecksumV1(
                     apgar::benchmark::kPhase4TrialWireSchemaVersion, *options.runtime_commit,
                     source_stamped, source_tree_dirty, options.raw_cell_artifact_checksum)) ||
      options.per_net_report_source_envelope_checksum !=
          apgar::benchmark::ComputePhase4PerNetReportSourceEnvelopeChecksumV1(
              *options.runtime_commit, source_stamped, source_tree_dirty,
              options.per_net_report_artifact_checksum)) {
    std::cerr << "Raw or per-net claimed-association preflight failed before candidate "
                 "execution\n";
    return 2;
  }
#ifdef APGAR_PHASE4_CONFIRMATORY_H4096_EXACT_SNAPSHOT_RUNNER
  const std::string fixture;
#else
  const std::optional<std::string> fixture = apgar::tooling::ReadFile(
      apgar::tooling::ResolveRunfile("tests/fixtures/phase4_supported_multinet_v1.kicad_pcb"));
  if (!fixture.has_value()) {
    std::cerr << "failed to authenticate fixture runfile\n";
    return 2;
  }
#endif
#ifndef APGAR_PHASE4_CONFIRMATORY_H4096_EXACT_SNAPSHOT_RUNNER
#ifdef APGAR_PHASE4_CONFIRMATORY_EXACT_SNAPSHOT_RUNNER
  auto spec_result = apgar::benchmark::BuildPhase4CanonicalTrialSpecForCorpusV2(
      options.cell, 0, apgar::benchmark::Phase4TrialOrder::kBaselineFirst);
#else
  auto spec_result = apgar::benchmark::BuildPhase4CanonicalTrialSpecV1(
      options.cell, 0, apgar::benchmark::Phase4TrialOrder::kBaselineFirst);
#endif
  if (!std::holds_alternative<apgar::benchmark::Phase4PairedTrialSpec>(spec_result)) {
    const auto& error = std::get<apgar::benchmark::Phase4TrialHarnessError>(spec_result);
    std::cerr << error.invariant_id << ": " << error.detail << '\n';
    return 2;
  }
#endif
  auto preparer_result = apgar::allocator::CreatePersistentCpuCandidatePoolPreparer(
      {.worker_count = options.cell.preparation_worker_count});
  if (!std::holds_alternative<
          std::unique_ptr<apgar::allocator::PersistentCpuCandidatePoolPreparer>>(preparer_result)) {
    const auto& error =
        std::get<apgar::allocator::CpuCandidatePoolPreparationError>(preparer_result);
    std::cerr << error.invariant_id << ": " << error.detail << '\n';
    return 1;
  }
  auto preparer = std::get<std::unique_ptr<apgar::allocator::PersistentCpuCandidatePoolPreparer>>(
      std::move(preparer_result));
#ifdef APGAR_PHASE4_CONFIRMATORY_H4096_EXACT_SNAPSHOT_RUNNER
  auto capture_result =
      apgar::benchmark::internal::ExecutePhase4ConfirmatoryH4096SameRunCandidatePoolSnapshot(
          std::get<apgar::benchmark::Phase4PairedTrialSpec>(spec_result), fixture, preparer.get());
#elif defined(APGAR_PHASE4_CONFIRMATORY_EXACT_SNAPSHOT_RUNNER)
  auto capture_result = apgar::benchmark::ExecutePhase4CandidatePoolSnapshotForCorpusV2(
      std::get<apgar::benchmark::Phase4PairedTrialSpec>(spec_result), *fixture, preparer.get());
#else
  auto capture_result = apgar::benchmark::ExecutePhase4CandidatePoolSnapshotV1(
      std::get<apgar::benchmark::Phase4PairedTrialSpec>(spec_result), *fixture, preparer.get());
#endif
  if (std::holds_alternative<apgar::benchmark::Phase4TrialArmFailure>(capture_result)) {
    const auto& error = std::get<apgar::benchmark::Phase4TrialArmFailure>(capture_result).summary;
    std::cerr << error.invariant_id << ": " << error.detail << '\n';
    return 1;
  }
#ifdef APGAR_PHASE4_CONFIRMATORY_H4096_EXACT_SNAPSHOT_RUNNER
  auto artifact_result =
      apgar::benchmark::internal::BuildPhase4ConfirmatoryH4096ExactSmallSnapshotArtifact(
#elif defined(APGAR_PHASE4_CONFIRMATORY_EXACT_SNAPSHOT_RUNNER)
  auto artifact_result = apgar::benchmark::BuildPhase4ExactSmallSnapshotArtifactForCorpusV2(
#else
  auto artifact_result = apgar::benchmark::BuildPhase4ExactSmallSnapshotArtifactV1(
#endif
          options.cell, *options.runtime_commit, source_stamped, source_tree_dirty,
          options.raw_cell_plan_checksum, options.raw_cell_artifact_checksum,
          options.raw_source_envelope_checksum, options.raw_reference,
          options.per_net_report_artifact_checksum, options.per_net_report_source_envelope_checksum,
          std::get<apgar::benchmark::Phase4CandidatePoolSnapshotExecutionV1>(
              std::move(capture_result)),
#ifdef APGAR_PHASE4_CONFIRMATORY_H4096_EXACT_SNAPSHOT_RUNNER
          fixture, options.raw_evidence_schema_version);
#else
      *fixture, options.raw_evidence_schema_version);
#endif
  if (std::holds_alternative<apgar::benchmark::Phase4ExactSmallSnapshotError>(artifact_result)) {
    const auto& error = std::get<apgar::benchmark::Phase4ExactSmallSnapshotError>(artifact_result);
    std::cerr << error.invariant_id << ": " << error.detail << '\n';
    return 1;
  }
  auto& artifact = std::get<apgar::benchmark::Phase4ExactSmallSnapshotArtifactV1>(artifact_result);
#ifdef APGAR_PHASE4_EXACT_SNAPSHOT_RUNNER_TESTING
  if (options.testing_maximum_serialized_bytes.has_value()) {
    artifact.maximum_serialized_bytes = *options.testing_maximum_serialized_bytes;
  }
#endif
  auto serialization = apgar::benchmark::SerializePhase4ExactSmallSnapshotArtifactJsonV1(artifact);
  if (std::holds_alternative<apgar::benchmark::Phase4ExactSmallSnapshotError>(serialization)) {
    const auto& error = std::get<apgar::benchmark::Phase4ExactSmallSnapshotError>(serialization);
    std::cerr << error.invariant_id << ": " << error.detail << '\n';
    return 1;
  }
  const std::string& output = std::get<std::string>(serialization);
  std::cout.write(output.data(), static_cast<std::streamsize>(output.size()));
  return 0;
} catch (const std::bad_alloc&) {
  std::cerr << "host allocation failed in exact-small snapshot runner\n";
  return 2;
} catch (const std::exception&) {
  std::cerr << "unexpected standard exception in exact-small snapshot runner\n";
  return 2;
} catch (...) {
  std::cerr << "unexpected non-standard exception in exact-small snapshot runner\n";
  return 2;
}
