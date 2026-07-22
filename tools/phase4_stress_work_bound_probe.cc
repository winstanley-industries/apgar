#include <cstdint>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <variant>

#include "apgar/benchmark/phase3_commit.h"
#include "apgar/benchmark/phase3_source_stamp.h"
#include "apgar/benchmark/phase4_representative_corpus.h"
#include "apgar/board_ir/stable_hash.h"

namespace {

using apgar::benchmark::Phase4CaseDescriptor;
using apgar::benchmark::Phase4RepresentativeCorpusError;
using apgar::benchmark::Phase4RepresentativeCorpusErrorCode;
using apgar::benchmark::Phase4RepresentativeCorpusLimits;
using apgar::benchmark::Phase4RepresentativeWorkBound;

struct Options {
  std::string runtime_commit;
  bool testing_allow_unstamped = false;
};

struct Row {
  std::uint32_t case_id = 0;
  std::uint64_t descriptor_fingerprint = 0;
  std::uint32_t requested_net_count = 0;
  std::uint32_t requested_pool_size = 0;
  std::uint32_t declared_stress_target_nets = 0;
  Phase4RepresentativeCorpusError error;
  std::uint64_t per_net_compiled_nodes = 0;
  std::uint64_t per_net_compiled_host_bytes = 0;
};

[[nodiscard]] std::optional<Options> ParseOptions(int argc, char** argv) {
  Options options;
  bool saw_commit = false;
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    constexpr std::string_view kCommit = "--runtime-commit=";
    if (argument.starts_with(kCommit) && !saw_commit) {
      options.runtime_commit = std::string(argument.substr(kCommit.size()));
      saw_commit = true;
    } else if (argument == "--testing-allow-unstamped" && !options.testing_allow_unstamped) {
      options.testing_allow_unstamped = true;
    } else {
      return std::nullopt;
    }
  }
  if (!saw_commit || !apgar::benchmark::IsFullLowercaseGitCommit(options.runtime_commit)) {
    return std::nullopt;
  }
#ifndef APGAR_PHASE4_STRESS_PROBE_TESTING
  if (options.testing_allow_unstamped) {
    return std::nullopt;
  }
#endif
  return options;
}

[[nodiscard]] std::optional<Row> Probe(std::uint32_t case_id,
                                       const Phase4RepresentativeCorpusLimits& limits) {
  const Phase4CaseDescriptor* descriptor = apgar::benchmark::FindPhase4CaseDescriptorV1(case_id);
  if (descriptor == nullptr || descriptor->requested_pool_size_count != 1 ||
      descriptor->requested_pool_sizes.front() != 4 ||
      descriptor->declared_stress_target_nets != 4096) {
    return std::nullopt;
  }
  apgar::benchmark::Phase4RepresentativeCaseResult result =
      apgar::benchmark::BuildPhase4RepresentativeCaseV1(case_id, {}, limits);
  if (!std::holds_alternative<Phase4RepresentativeCorpusError>(result)) {
    return std::nullopt;
  }
  Phase4RepresentativeCorpusError error = std::get<Phase4RepresentativeCorpusError>(result);
  if (error.code != Phase4RepresentativeCorpusErrorCode::kWorkBoundExceeded ||
      error.invariant_id != "benchmark.phase4_representative.compiled_work_bound.v1" ||
      error.limiting_work_bound == Phase4RepresentativeWorkBound::kNone ||
      !error.first_unpreparable_net.has_value() || error.maximum_preparable_net_count == 0 ||
      error.maximum_preparable_net_count >= descriptor->requested_net_count ||
      error.required_compiled_nodes % descriptor->requested_net_count != 0 ||
      error.required_compiled_host_bytes % descriptor->requested_net_count != 0) {
    return std::nullopt;
  }
  return Row{
      .case_id = case_id,
      .descriptor_fingerprint = apgar::benchmark::FingerprintPhase4CaseDescriptorV1(*descriptor),
      .requested_net_count = descriptor->requested_net_count,
      .requested_pool_size = descriptor->requested_pool_sizes.front(),
      .declared_stress_target_nets = descriptor->declared_stress_target_nets,
      .error = error,
      .per_net_compiled_nodes = error.required_compiled_nodes / descriptor->requested_net_count,
      .per_net_compiled_host_bytes =
          error.required_compiled_host_bytes / descriptor->requested_net_count,
  };
}

void HashLimits(apgar::board_ir::StableHashBuilder* hash,
                const Phase4RepresentativeCorpusLimits& limits) {
  hash->AddU64(limits.maximum_nets);
  hash->AddU64(limits.maximum_compiled_nodes);
  hash->AddU64(limits.maximum_compiled_host_bytes);
  hash->AddU64(limits.maximum_active_regions);
  hash->AddU64(limits.maximum_board_entities);
}

void HashRow(apgar::board_ir::StableHashBuilder* hash, const Row& row) {
  hash->AddU32(row.case_id);
  hash->AddU64(row.descriptor_fingerprint);
  hash->AddU32(row.requested_net_count);
  hash->AddU32(row.requested_pool_size);
  hash->AddU32(row.declared_stress_target_nets);
  hash->AddString("compiled_work_bound");
  hash->AddString("descriptor_board_and_one_representative_compiled_net_only");
  hash->AddBool(false);
  hash->AddBool(false);
  hash->AddBool(false);
  hash->AddByte(static_cast<std::uint8_t>(row.error.code));
  hash->AddString(row.error.invariant_id);
  hash->AddByte(static_cast<std::uint8_t>(row.error.limiting_work_bound));
  hash->AddU64(row.error.maximum_preparable_net_count);
  hash->AddU64(row.error.first_unpreparable_net->id);
  hash->AddU32(row.error.first_unpreparable_net->generation);
  hash->AddU64(row.error.required_compiled_nodes);
  hash->AddU64(row.error.configured_compiled_node_limit);
  hash->AddU64(row.error.required_compiled_host_bytes);
  hash->AddU64(row.error.configured_compiled_host_byte_limit);
  hash->AddU64(row.per_net_compiled_nodes);
  hash->AddU64(row.per_net_compiled_host_bytes);
}

[[nodiscard]] std::uint64_t ArtifactChecksum(std::string_view source_commit, bool source_stamped,
                                             bool source_tree_dirty,
                                             const Phase4RepresentativeCorpusLimits& limits,
                                             const Row& first, const Row& second) {
  apgar::board_ir::StableHashBuilder hash;
  hash.AddString("APGAR-PHASE4-STRESS-WORK-BOUND-PROBE-ARTIFACT-V1");
  hash.AddU32(1);
  hash.AddString(source_commit);
  hash.AddBool(source_stamped);
  hash.AddBool(source_tree_dirty);
  hash.AddU32(apgar::benchmark::kPhase4RepresentativeCorpusVersion);
  hash.AddU64(apgar::benchmark::Phase4RepresentativeCorpusChecksumV1());
  HashLimits(&hash, limits);
  hash.AddU64(2);
  HashRow(&hash, first);
  HashRow(&hash, second);
  return hash.Finish();
}

[[nodiscard]] std::uint64_t SourceEnvelopeChecksum(std::string_view source_commit,
                                                   bool source_stamped, bool source_tree_dirty,
                                                   std::uint64_t artifact_checksum) {
  apgar::board_ir::StableHashBuilder hash;
  hash.AddString("APGAR-PHASE4-STRESS-WORK-BOUND-PROBE-SOURCE-ENVELOPE-V1");
  hash.AddU32(1);
  hash.AddString(source_commit);
  hash.AddBool(source_stamped);
  hash.AddBool(source_tree_dirty);
  hash.AddU64(artifact_checksum);
  return hash.Finish();
}

void WriteRow(std::ostringstream* output, const Row& row) {
  *output << "{\"case_id\":" << row.case_id
          << ",\"descriptor_fingerprint\":" << row.descriptor_fingerprint
          << ",\"requested_net_count\":" << row.requested_net_count
          << ",\"requested_pool_size\":" << row.requested_pool_size
          << ",\"declared_stress_target_nets\":" << row.declared_stress_target_nets
          << ",\"build_status\":\"compiled_work_bound\""
             ",\"materialization_scope\":\"descriptor_board_and_one_representative_compiled_"
             "net_only\",\"full_workload_materialized\":false,\"capacity_model_reached\":false"
             ",\"allocator_reached\":false,\"error_code\":"
          << static_cast<unsigned int>(row.error.code)
          << ",\"invariant_id\":\"benchmark.phase4_representative.compiled_work_bound.v1\""
             ",\"limiting_work_bound\":"
          << static_cast<unsigned int>(row.error.limiting_work_bound)
          << ",\"maximum_preparable_net_count\":" << row.error.maximum_preparable_net_count
          << ",\"first_unpreparable_net\":{\"id\":" << row.error.first_unpreparable_net->id
          << ",\"generation\":" << row.error.first_unpreparable_net->generation
          << "},\"required_compiled_nodes\":" << row.error.required_compiled_nodes
          << ",\"configured_compiled_node_limit\":" << row.error.configured_compiled_node_limit
          << ",\"required_compiled_host_bytes\":" << row.error.required_compiled_host_bytes
          << ",\"configured_compiled_host_byte_limit\":"
          << row.error.configured_compiled_host_byte_limit
          << ",\"per_net_compiled_nodes\":" << row.per_net_compiled_nodes
          << ",\"per_net_compiled_host_bytes\":" << row.per_net_compiled_host_bytes << '}';
}

}  // namespace

int main(int argc, char** argv) {
  const std::optional<Options> options = ParseOptions(argc, argv);
  if (!options.has_value()) {
    std::cerr << "usage: phase4_stress_work_bound_probe --runtime-commit=<40-lower-hex>\n";
    return 2;
  }
  bool source_stamped = apgar::benchmark::kPhase3SourceStamped;
  bool source_tree_dirty = apgar::benchmark::kPhase3BuiltFromDirtyTree;
  const bool publishable = apgar::benchmark::IsPublishableBenchmarkSource(
      options->runtime_commit, apgar::benchmark::kPhase3BuiltCommit, source_stamped,
      source_tree_dirty);
#ifdef APGAR_PHASE4_STRESS_PROBE_TESTING
  if (!publishable && options->testing_allow_unstamped) {
    source_stamped = true;
    source_tree_dirty = false;
  } else
#endif
      if (!publishable) {
    std::cerr << "stress probe requires a clean source-identical commit\n";
    return 2;
  }

  const Phase4RepresentativeCorpusLimits limits;
  const std::optional<Row> first = Probe(3001, limits);
  const std::optional<Row> second = Probe(3002, limits);
  if (!first.has_value() || !second.has_value()) {
    std::cerr << "stress probe did not reproduce the frozen work-bound witnesses\n";
    return 1;
  }
  const std::uint64_t artifact_checksum = ArtifactChecksum(
      options->runtime_commit, source_stamped, source_tree_dirty, limits, *first, *second);
  const std::uint64_t source_envelope_checksum = SourceEnvelopeChecksum(
      options->runtime_commit, source_stamped, source_tree_dirty, artifact_checksum);
  std::ostringstream output;
  output << "{\"schema_version\":1,\"source_commit\":\"" << options->runtime_commit
         << "\",\"source_stamped\":" << (source_stamped ? "true" : "false")
         << ",\"source_tree_dirty\":" << (source_tree_dirty ? "true" : "false")
         << ",\"corpus_version\":" << apgar::benchmark::kPhase4RepresentativeCorpusVersion
         << ",\"corpus_checksum\":" << apgar::benchmark::Phase4RepresentativeCorpusChecksumV1()
         << ",\"limits\":{\"maximum_nets\":" << limits.maximum_nets
         << ",\"maximum_compiled_nodes\":" << limits.maximum_compiled_nodes
         << ",\"maximum_compiled_host_bytes\":" << limits.maximum_compiled_host_bytes
         << ",\"maximum_active_regions\":" << limits.maximum_active_regions
         << ",\"maximum_board_entities\":" << limits.maximum_board_entities << "},\"cases\":[";
  WriteRow(&output, *first);
  output << ',';
  WriteRow(&output, *second);
  output << "],\"artifact_checksum\":" << artifact_checksum
         << ",\"source_envelope_checksum\":" << source_envelope_checksum << "}\n";
  std::cout << output.str();
  return 0;
}
