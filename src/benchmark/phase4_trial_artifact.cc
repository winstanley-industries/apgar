#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "apgar/benchmark/phase4_trial_harness.h"
#include "apgar/board_ir/stable_hash.h"

namespace apgar::benchmark {
namespace {

class JsonWriter {
 public:
  void BeginObject() {
    output_.push_back('{');
    scopes_.push_back({ScopeKind::kObject, true});
  }

  void EndObject() {
    output_.push_back('}');
    scopes_.pop_back();
  }

  void BeginArray() {
    output_.push_back('[');
    scopes_.push_back({ScopeKind::kArray, true});
  }

  void EndArray() {
    output_.push_back(']');
    scopes_.pop_back();
  }

  void Key(std::string_view key) {
    SeparateScopeValue();
    String(key);
    output_.push_back(':');
  }

  void Element() { SeparateScopeValue(); }

  void String(std::string_view value) {
    constexpr char kHexDigits[] = "0123456789abcdef";
    output_.push_back('"');
    for (const unsigned char character : value) {
      switch (character) {
        case '"':
          output_ += "\\\"";
          break;
        case '\\':
          output_ += "\\\\";
          break;
        case '\b':
          output_ += "\\b";
          break;
        case '\f':
          output_ += "\\f";
          break;
        case '\n':
          output_ += "\\n";
          break;
        case '\r':
          output_ += "\\r";
          break;
        case '\t':
          output_ += "\\t";
          break;
        default:
          if (character < 0x20U) {
            output_ += "\\u00";
            output_.push_back(kHexDigits[(character >> 4U) & 0x0fU]);
            output_.push_back(kHexDigits[character & 0x0fU]);
          } else {
            output_.push_back(static_cast<char>(character));
          }
          break;
      }
    }
    output_.push_back('"');
  }

  template <typename Integer>
  void IntegerValue(Integer value) {
    static_assert(std::is_integral_v<Integer>);
    output_ += std::to_string(value);
  }

  template <typename Enum>
  void EnumValue(Enum value) {
    static_assert(std::is_enum_v<Enum>);
    using Underlying = std::underlying_type_t<Enum>;
    IntegerValue(static_cast<Underlying>(value));
  }

  void Bool(bool value) { output_ += value ? "true" : "false"; }

  void Null() { output_ += "null"; }

  [[nodiscard]] std::string Finish() && {
    output_.push_back('\n');
    return std::move(output_);
  }

 private:
  enum class ScopeKind : std::uint8_t { kObject, kArray };

  struct Scope {
    ScopeKind kind;
    bool first;
  };

  void SeparateScopeValue() {
    Scope& scope = scopes_.back();
    if (!scope.first) {
      output_.push_back(',');
    }
    scope.first = false;
  }

  std::string output_;
  std::vector<Scope> scopes_;
};

void WriteExternalBudget(JsonWriter* writer, const Phase4ExternalBudget& budget) {
  writer->BeginObject();
  writer->Key("maximum_prepared_elapsed_nanoseconds");
  writer->IntegerValue(budget.maximum_prepared_elapsed_nanoseconds);
  writer->Key("maximum_cold_elapsed_nanoseconds");
  writer->IntegerValue(budget.maximum_cold_elapsed_nanoseconds);
  writer->Key("maximum_address_space_bytes");
  writer->IntegerValue(budget.maximum_address_space_bytes);
  writer->Key("maximum_peak_host_bytes");
  writer->IntegerValue(budget.maximum_peak_host_bytes);
  writer->EndObject();
}

void WriteCorpusLimits(JsonWriter* writer, const Phase4RepresentativeCorpusLimits& limits) {
  writer->BeginObject();
  writer->Key("maximum_nets");
  writer->IntegerValue(limits.maximum_nets);
  writer->Key("maximum_compiled_nodes");
  writer->IntegerValue(limits.maximum_compiled_nodes);
  writer->Key("maximum_compiled_host_bytes");
  writer->IntegerValue(limits.maximum_compiled_host_bytes);
  writer->Key("maximum_active_regions");
  writer->IntegerValue(limits.maximum_active_regions);
  writer->Key("maximum_board_entities");
  writer->IntegerValue(limits.maximum_board_entities);
  writer->EndObject();
}

void WriteCellConfig(JsonWriter* writer, const Phase4CanonicalCellConfig& config) {
  writer->BeginObject();
  writer->Key("schema_version");
  writer->IntegerValue(config.schema_version);
  writer->Key("case_id");
  writer->IntegerValue(config.case_id);
  writer->Key("requested_pool_size");
  writer->IntegerValue(config.requested_pool_size);
  writer->Key("preparation_worker_count");
  writer->IntegerValue(config.preparation_worker_count);
  writer->Key("repetitions");
  writer->IntegerValue(config.repetitions);
  writer->Key("maximum_setup_elapsed_nanoseconds");
  writer->IntegerValue(config.maximum_setup_elapsed_nanoseconds);
  writer->Key("external_budget");
  WriteExternalBudget(writer, config.external_budget);
  writer->Key("corpus_limits");
  WriteCorpusLimits(writer, config.corpus_limits);
  writer->EndObject();
}

void WriteHostEnvironment(JsonWriter* writer, const Phase4HostEnvironment& environment) {
  writer->BeginObject();
  writer->Key("schema_version");
  writer->IntegerValue(environment.schema_version);
  writer->Key("host_os");
  writer->String(environment.host_os);
  writer->Key("host_kernel");
  writer->String(environment.host_kernel);
  writer->Key("host_architecture");
  writer->String(environment.host_architecture);
  writer->Key("cpu_model");
  writer->String(environment.cpu_model);
  writer->Key("compiler_identity");
  writer->String(environment.compiler_identity);
  writer->Key("monotonic_clock");
  writer->String(environment.monotonic_clock);
  writer->Key("online_cpu_count");
  writer->IntegerValue(environment.online_cpu_count);
  writer->Key("affinity_cpu_count");
  writer->IntegerValue(environment.affinity_cpu_count);
  writer->Key("total_host_memory_bytes");
  writer->IntegerValue(environment.total_host_memory_bytes);
  writer->Key("environment_checksum");
  writer->IntegerValue(environment.environment_checksum);
  writer->EndObject();
}

void WriteRouteOpportunity(JsonWriter* writer, const Phase4RouteOpportunity& opportunity) {
  writer->BeginObject();
  writer->Key("route_queries");
  writer->IntegerValue(opportunity.route_queries);
  writer->Key("route_work_units");
  writer->IntegerValue(opportunity.route_work_units);
  writer->EndObject();
}

void WriteBoardOutcome(JsonWriter* writer, const Phase4BoardOutcome& outcome) {
  writer->BeginObject();
  writer->Key("selected_net_count");
  writer->IntegerValue(outcome.selected_net_count);
  writer->Key("no_candidate_net_count");
  writer->IntegerValue(outcome.no_candidate_net_count);
  writer->Key("overused_resource_count");
  writer->IntegerValue(outcome.overused_resource_count);
  writer->Key("total_overuse_units");
  writer->IntegerValue(outcome.total_overuse_units);
  writer->Key("total_intrinsic_cost");
  writer->IntegerValue(outcome.total_intrinsic_cost);
  writer->Key("world_checksum");
  writer->IntegerValue(outcome.world_checksum);
  writer->EndObject();
}

void WriteLifecycleObservation(JsonWriter* writer,
                               const Phase4PreparerLifecycleObservation& observation) {
  writer->BeginObject();
  writer->Key("workers_started_before");
  writer->IntegerValue(observation.workers_started_before);
  writer->Key("workers_started_after");
  writer->IntegerValue(observation.workers_started_after);
  writer->Key("invocations_started_before");
  writer->IntegerValue(observation.invocations_started_before);
  writer->Key("invocations_started_after");
  writer->IntegerValue(observation.invocations_started_after);
  writer->Key("invocations_completed_before");
  writer->IntegerValue(observation.invocations_completed_before);
  writer->Key("invocations_completed_after");
  writer->IntegerValue(observation.invocations_completed_after);
  writer->EndObject();
}

void WriteArmSemantics(JsonWriter* writer, const Phase4TrialArmSemantics& semantics) {
  writer->BeginObject();
  writer->Key("schema_version");
  writer->IntegerValue(semantics.schema_version);
  writer->Key("arm");
  writer->EnumValue(semantics.arm);
  writer->Key("execution_order");
  writer->EnumValue(semantics.execution_order);
  writer->Key("corpus_version");
  writer->IntegerValue(semantics.corpus_version);
  writer->Key("corpus_checksum");
  writer->IntegerValue(semantics.corpus_checksum);
  writer->Key("case_id");
  writer->IntegerValue(semantics.case_id);
  writer->Key("descriptor_fingerprint");
  writer->IntegerValue(semantics.descriptor_fingerprint);
  writer->Key("case_checksum");
  writer->IntegerValue(semantics.case_checksum);
  writer->Key("board_content_hash");
  writer->IntegerValue(semantics.board_content_hash);
  writer->Key("workload_checksum");
  writer->IntegerValue(semantics.workload_checksum);
  writer->Key("capacity_model_checksum");
  writer->IntegerValue(semantics.capacity_model_checksum);
  writer->Key("budget_checksum");
  writer->IntegerValue(semantics.budget_checksum);
  writer->Key("workload_net_count");
  writer->IntegerValue(semantics.workload_net_count);
  writer->Key("requested_pool_size");
  writer->IntegerValue(semantics.requested_pool_size);
  writer->Key("repetition_index");
  writer->IntegerValue(semantics.repetition_index);
  writer->Key("root_seed");
  writer->IntegerValue(semantics.root_seed);
  writer->Key("preparation_worker_count");
  writer->IntegerValue(semantics.preparation_worker_count);
  writer->Key("baseline_sweeps");
  writer->IntegerValue(semantics.baseline_sweeps);
  writer->Key("candidate_regeneration_epochs");
  writer->IntegerValue(semantics.candidate_regeneration_epochs);
  writer->Key("candidate_columns_per_epoch");
  writer->IntegerValue(semantics.candidate_columns_per_epoch);
  writer->Key("candidate_terminal_selection_rounds");
  writer->IntegerValue(semantics.candidate_terminal_selection_rounds);
  writer->Key("external_budget");
  WriteExternalBudget(writer, semantics.external_budget);
  writer->Key("opportunity");
  WriteRouteOpportunity(writer, semantics.opportunity);
  writer->Key("actual");
  WriteRouteOpportunity(writer, semantics.actual);
  writer->Key("preparation_route_queries");
  writer->IntegerValue(semantics.preparation_route_queries);
  writer->Key("preparation_route_work_units");
  writer->IntegerValue(semantics.preparation_route_work_units);
  writer->Key("regeneration_route_queries");
  writer->IntegerValue(semantics.regeneration_route_queries);
  writer->Key("regeneration_route_work_units");
  writer->IntegerValue(semantics.regeneration_route_work_units);
  writer->Key("requested_columns");
  writer->IntegerValue(semantics.requested_columns);
  writer->Key("admitted_candidates");
  writer->IntegerValue(semantics.admitted_candidates);
  writer->Key("rejected_columns");
  writer->IntegerValue(semantics.rejected_columns);
  writer->Key("final_candidate_count");
  writer->IntegerValue(semantics.final_candidate_count);
  writer->Key("preparation_checksum");
  writer->IntegerValue(semantics.preparation_checksum);
  writer->Key("algorithm_session_checksum");
  writer->IntegerValue(semantics.algorithm_session_checksum);
  writer->Key("final_pool_manifest_checksum");
  writer->IntegerValue(semantics.final_pool_manifest_checksum);
  writer->Key("final_rejection_manifest_checksum");
  writer->IntegerValue(semantics.final_rejection_manifest_checksum);
  writer->Key("terminal_reason");
  writer->EnumValue(semantics.terminal_reason);
  writer->Key("candidate_outcome_source");
  writer->EnumValue(semantics.candidate_outcome_source);
  writer->Key("outcome");
  WriteBoardOutcome(writer, semantics.outcome);
  writer->Key("semantic_checksum");
  writer->IntegerValue(semantics.semantic_checksum);
  writer->EndObject();
}

void WriteExternalObservation(JsonWriter* writer,
                              const Phase4ExternalResourceObservation& observation) {
  writer->BeginObject();
  writer->Key("schema_version");
  writer->IntegerValue(observation.schema_version);
  writer->Key("authority_kind");
  writer->EnumValue(observation.authority_kind);
  writer->Key("authority_run_identity");
  writer->IntegerValue(observation.authority_run_identity);
  writer->Key("controller_identity");
  writer->IntegerValue(observation.controller_identity);
  writer->Key("process_instance_identity");
  writer->IntegerValue(observation.process_instance_identity);
  writer->Key("associated_semantic_checksum");
  writer->IntegerValue(observation.associated_semantic_checksum);
  writer->Key("configured_wall_limit_nanoseconds");
  writer->IntegerValue(observation.configured_wall_limit_nanoseconds);
  writer->Key("configured_address_space_limit_bytes");
  writer->IntegerValue(observation.configured_address_space_limit_bytes);
  writer->Key("configured_peak_host_limit_bytes");
  writer->IntegerValue(observation.configured_peak_host_limit_bytes);
  writer->Key("outer_elapsed_nanoseconds");
  writer->IntegerValue(observation.outer_elapsed_nanoseconds);
  writer->Key("peak_host_bytes");
  writer->IntegerValue(observation.peak_host_bytes);
  writer->Key("process_exit_code");
  writer->IntegerValue(observation.process_exit_code);
  writer->Key("isolated_process");
  writer->Bool(observation.isolated_process);
  writer->Key("wall_authority_enforced");
  writer->Bool(observation.wall_authority_enforced);
  writer->Key("memory_authority_enforced");
  writer->Bool(observation.memory_authority_enforced);
  writer->Key("persistent_preparer_reused");
  writer->Bool(observation.persistent_preparer_reused);
  writer->Key("preparer_lifecycle");
  WriteLifecycleObservation(writer, observation.preparer_lifecycle);
  writer->Key("authority_checksum");
  writer->IntegerValue(observation.authority_checksum);
  writer->EndObject();
}

void WriteArmRecord(JsonWriter* writer, const Phase4TrialArmRecord& record) {
  writer->BeginObject();
  writer->Key("semantics");
  WriteArmSemantics(writer, record.semantics);
  writer->Key("case_build_elapsed_nanoseconds");
  writer->IntegerValue(record.case_build_elapsed_nanoseconds);
  writer->Key("prepared_elapsed_nanoseconds");
  writer->IntegerValue(record.prepared_elapsed_nanoseconds);
  writer->Key("cold_elapsed_nanoseconds");
  writer->IntegerValue(record.cold_elapsed_nanoseconds);
  writer->Key("preparer_lifecycle");
  WriteLifecycleObservation(writer, record.preparer_lifecycle);
  writer->Key("external_observation");
  WriteExternalObservation(writer, record.external_observation);
  writer->Key("artifact_checksum");
  writer->IntegerValue(record.artifact_checksum);
  writer->EndObject();
}

void WriteDurableFailure(JsonWriter* writer, const Phase4DurableArmFailure& failure) {
  writer->BeginObject();
  writer->Key("schema_version");
  writer->IntegerValue(failure.schema_version);
  writer->Key("summary_code");
  writer->EnumValue(failure.summary_code);
  writer->Key("arm");
  writer->EnumValue(failure.arm);
  writer->Key("summary_required");
  writer->IntegerValue(failure.summary_required);
  writer->Key("summary_configured");
  writer->IntegerValue(failure.summary_configured);
  writer->Key("summary_invariant_id");
  writer->String(failure.summary_invariant_id);
  writer->Key("summary_detail");
  writer->String(failure.summary_detail);
  writer->Key("payload_kind");
  writer->EnumValue(failure.payload_kind);
  writer->Key("child_error_code");
  writer->IntegerValue(failure.child_error_code);
  writer->Key("child_invariant_id");
  writer->String(failure.child_invariant_id);
  writer->Key("child_detail");
  writer->String(failure.child_detail);
  writer->Key("has_child_net");
  writer->Bool(failure.has_child_net);
  writer->Key("child_net_id");
  writer->IntegerValue(failure.child_net_id);
  writer->Key("child_net_generation");
  writer->IntegerValue(failure.child_net_generation);
  writer->Key("child_required");
  writer->IntegerValue(failure.child_required);
  writer->Key("child_configured");
  writer->IntegerValue(failure.child_configured);
  writer->Key("child_bound_kind");
  writer->IntegerValue(failure.child_bound_kind);
  writer->Key("child_secondary_required");
  writer->IntegerValue(failure.child_secondary_required);
  writer->Key("child_secondary_configured");
  writer->IntegerValue(failure.child_secondary_configured);
  writer->Key("has_case_identity");
  writer->Bool(failure.has_case_identity);
  writer->Key("case_id");
  writer->IntegerValue(failure.case_id);
  writer->Key("descriptor_fingerprint");
  writer->IntegerValue(failure.descriptor_fingerprint);
  writer->Key("case_checksum");
  writer->IntegerValue(failure.case_checksum);
  writer->Key("board_content_hash");
  writer->IntegerValue(failure.board_content_hash);
  writer->Key("workload_checksum");
  writer->IntegerValue(failure.workload_checksum);
  writer->Key("capacity_model_checksum");
  writer->IntegerValue(failure.capacity_model_checksum);
  writer->Key("has_epoch_index");
  writer->Bool(failure.has_epoch_index);
  writer->Key("epoch_index");
  writer->IntegerValue(failure.epoch_index);
  writer->Key("has_failed_observation");
  writer->Bool(failure.has_failed_observation);
  writer->Key("failed_observation_checksum");
  writer->IntegerValue(failure.failed_observation_checksum);
  writer->Key("attempted_column_count");
  writer->IntegerValue(failure.attempted_column_count);
  writer->Key("attempted_route_queries");
  writer->IntegerValue(failure.attempted_route_queries);
  writer->Key("attempted_route_work_units");
  writer->IntegerValue(failure.attempted_route_work_units);
  writer->Key("candidate_store_publication_committed");
  writer->Bool(failure.candidate_store_publication_committed);
  writer->Key("authoritative_candidate_store_present");
  writer->Bool(failure.authoritative_candidate_store_present);
  writer->Key("reconciled_candidate_count");
  writer->IntegerValue(failure.reconciled_candidate_count);
  writer->Key("reconciled_rejection_count");
  writer->IntegerValue(failure.reconciled_rejection_count);
  writer->Key("reconciled_candidate_store_checksum");
  writer->IntegerValue(failure.reconciled_candidate_store_checksum);
  writer->Key("payload_checksum");
  writer->IntegerValue(failure.payload_checksum);
  writer->EndObject();
}

void WritePairedResult(JsonWriter* writer, const Phase4PairedTrialResult& result) {
  writer->BeginObject();
  writer->Key("schema_version");
  writer->IntegerValue(result.schema_version);
  writer->Key("baseline");
  WriteArmRecord(writer, result.baseline);
  writer->Key("candidate");
  WriteArmRecord(writer, result.candidate);
  writer->Key("comparison");
  writer->EnumValue(result.comparison);
  writer->Key("semantic_checksum");
  writer->IntegerValue(result.semantic_checksum);
  writer->Key("artifact_checksum");
  writer->IntegerValue(result.artifact_checksum);
  writer->EndObject();
}

void WriteArmAttempt(JsonWriter* writer, const Phase4IsolatedArmAttempt& attempt) {
  writer->BeginObject();
  writer->Key("schema_version");
  writer->IntegerValue(attempt.schema_version);
  writer->Key("arm");
  writer->EnumValue(attempt.arm);
  writer->Key("repetition_index");
  writer->IntegerValue(attempt.repetition_index);
  writer->Key("execution_order");
  writer->EnumValue(attempt.execution_order);
  writer->Key("disposition");
  writer->EnumValue(attempt.disposition);
  writer->Key("dispatch_ordinal");
  writer->IntegerValue(attempt.dispatch_ordinal);
  writer->Key("process_instance_identity");
  writer->IntegerValue(attempt.process_instance_identity);
  writer->Key("outer_elapsed_nanoseconds");
  writer->IntegerValue(attempt.outer_elapsed_nanoseconds);
  writer->Key("process_lifetime_peak_host_bytes");
  writer->IntegerValue(attempt.process_lifetime_peak_host_bytes);
  writer->Key("raw_wait_status");
  writer->IntegerValue(attempt.raw_wait_status);
  writer->Key("process_exit_code");
  writer->IntegerValue(attempt.process_exit_code);
  writer->Key("terminating_signal");
  writer->IntegerValue(attempt.terminating_signal);
  writer->Key("watchdog_kill_sent");
  writer->Bool(attempt.watchdog_kill_sent);
  writer->Key("controller_invariant_id");
  writer->String(attempt.controller_invariant_id);
  writer->Key("controller_detail");
  writer->String(attempt.controller_detail);
  writer->Key("record");
  if (attempt.record.has_value()) {
    WriteArmRecord(writer, *attempt.record);
  } else {
    writer->Null();
  }
  writer->Key("child_failure");
  if (attempt.child_failure.has_value()) {
    WriteDurableFailure(writer, *attempt.child_failure);
  } else {
    writer->Null();
  }
  writer->Key("attempt_checksum");
  writer->IntegerValue(attempt.attempt_checksum);
  writer->EndObject();
}

void WritePairAttempt(JsonWriter* writer, const Phase4IsolatedPairAttempt& attempt) {
  writer->BeginObject();
  writer->Key("schema_version");
  writer->IntegerValue(attempt.schema_version);
  writer->Key("case_id");
  writer->IntegerValue(attempt.case_id);
  writer->Key("requested_pool_size");
  writer->IntegerValue(attempt.requested_pool_size);
  writer->Key("repetition_index");
  writer->IntegerValue(attempt.repetition_index);
  writer->Key("root_seed");
  writer->IntegerValue(attempt.root_seed);
  writer->Key("execution_order");
  writer->EnumValue(attempt.execution_order);
  writer->Key("baseline");
  WriteArmAttempt(writer, attempt.baseline);
  writer->Key("candidate");
  WriteArmAttempt(writer, attempt.candidate);
  writer->Key("result");
  if (attempt.result.has_value()) {
    WritePairedResult(writer, *attempt.result);
  } else {
    writer->Null();
  }
  writer->Key("attempt_checksum");
  writer->IntegerValue(attempt.attempt_checksum);
  writer->EndObject();
}

void WriteIsolatedCell(JsonWriter* writer, const Phase4IsolatedCellResult& result) {
  writer->Key("schema_version");
  writer->IntegerValue(result.schema_version);
  writer->Key("config");
  WriteCellConfig(writer, result.config);
  writer->Key("environment");
  WriteHostEnvironment(writer, result.environment);
  writer->Key("corpus_checksum");
  writer->IntegerValue(result.corpus_checksum);
  writer->Key("cell_plan_checksum");
  writer->IntegerValue(result.cell_plan_checksum);
  writer->Key("authority_run_identity");
  writer->IntegerValue(result.authority_run_identity);
  writer->Key("controller_identity");
  writer->IntegerValue(result.controller_identity);
  writer->Key("attempts");
  writer->BeginArray();
  for (const Phase4IsolatedPairAttempt& attempt : result.attempts) {
    writer->Element();
    WritePairAttempt(writer, attempt);
  }
  writer->EndArray();
  writer->Key("artifact_checksum");
  writer->IntegerValue(result.artifact_checksum);
}

}  // namespace

std::uint64_t ComputePhase4SourceEnvelopeChecksumV1(std::uint32_t wire_schema_version,
                                                    std::string_view source_commit,
                                                    bool source_stamped, bool source_tree_dirty,
                                                    std::uint64_t cell_artifact_checksum) noexcept {
  board_ir::StableHashBuilder hash;
  hash.AddString("APGAR-PHASE4-SOURCE-ENVELOPE-V1");
  hash.AddU32(wire_schema_version);
  hash.AddString(source_commit);
  hash.AddBool(source_stamped);
  hash.AddBool(source_tree_dirty);
  hash.AddU64(cell_artifact_checksum);
  return hash.Finish();
}

std::uint64_t ComputePhase4SourceEnvelopeChecksumV2(std::uint32_t raw_evidence_schema_version,
                                                    std::uint32_t wire_schema_version,
                                                    std::string_view source_commit,
                                                    bool source_stamped, bool source_tree_dirty,
                                                    std::uint64_t cell_artifact_checksum) noexcept {
  board_ir::StableHashBuilder hash;
  hash.AddString("APGAR-PHASE4-SOURCE-ENVELOPE-V2");
  hash.AddU32(raw_evidence_schema_version);
  hash.AddU32(wire_schema_version);
  hash.AddString(source_commit);
  hash.AddBool(source_stamped);
  hash.AddBool(source_tree_dirty);
  hash.AddU64(cell_artifact_checksum);
  return hash.Finish();
}

namespace {

std::uint64_t ComputeIsolatedCellArtifactChecksum(const Phase4IsolatedCellResult& result,
                                                  std::string_view domain,
                                                  bool include_same_run_versions) noexcept {
  board_ir::StableHashBuilder hash;
  hash.AddString(domain);
  if (include_same_run_versions) {
    hash.AddU32(kPhase4SameRunRawEvidenceSchemaVersion);
    hash.AddU32(kPhase4SameRunTrialWireSchemaVersion);
  }
  hash.AddU32(result.schema_version);
  hash.AddU64(result.corpus_checksum);
  hash.AddU64(result.cell_plan_checksum);
  hash.AddU64(result.environment.environment_checksum);
  hash.AddU64(result.authority_run_identity);
  hash.AddU64(result.controller_identity);
  hash.AddU64(result.attempts.size());
  for (const Phase4IsolatedPairAttempt& attempt : result.attempts) {
    hash.AddU64(attempt.attempt_checksum);
  }
  return hash.Finish();
}

}  // namespace

std::uint64_t ComputePhase4IsolatedCellArtifactChecksumV1(
    const Phase4IsolatedCellResult& result) noexcept {
  return ComputeIsolatedCellArtifactChecksum(result, "APGAR-PHASE4-ISOLATED-CELL-ARTIFACT-V1",
                                             false);
}

std::uint64_t ComputePhase4SameRunIsolatedCellArtifactChecksumV2(
    const Phase4IsolatedCellResult& result) noexcept {
  return ComputeIsolatedCellArtifactChecksum(result, "APGAR-PHASE4-ISOLATED-CELL-ARTIFACT-V2",
                                             true);
}

std::optional<std::string> SerializePhase4IsolatedCellJsonV1(const Phase4IsolatedCellResult& result,
                                                             std::string_view source_commit,
                                                             bool source_stamped,
                                                             bool source_tree_dirty) {
  if (result.carrier != Phase4IsolatedCellCarrier::kRawWireV1 || result.artifact_checksum == 0 ||
      result.artifact_checksum != ComputePhase4IsolatedCellArtifactChecksumV1(result)) {
    return std::nullopt;
  }
  JsonWriter writer;
  writer.BeginObject();
  writer.Key("wire_schema_version");
  writer.IntegerValue(kPhase4TrialWireSchemaVersion);
  writer.Key("source_commit");
  writer.String(source_commit);
  writer.Key("source_stamped");
  writer.Bool(source_stamped);
  writer.Key("source_tree_dirty");
  writer.Bool(source_tree_dirty);
  writer.Key("source_envelope_checksum");
  writer.IntegerValue(ComputePhase4SourceEnvelopeChecksumV1(
      kPhase4TrialWireSchemaVersion, source_commit, source_stamped, source_tree_dirty,
      result.artifact_checksum));
  WriteIsolatedCell(&writer, result);
  writer.EndObject();
  return std::move(writer).Finish();
}

std::optional<std::string> SerializePhase4SameRunIsolatedCellJsonV2(
    const Phase4IsolatedCellResult& result, std::string_view source_commit, bool source_stamped,
    bool source_tree_dirty) {
  if (result.carrier != Phase4IsolatedCellCarrier::kSameRunWireV2 ||
      result.artifact_checksum == 0 ||
      result.artifact_checksum != ComputePhase4SameRunIsolatedCellArtifactChecksumV2(result)) {
    return std::nullopt;
  }
  JsonWriter writer;
  writer.BeginObject();
  writer.Key("raw_evidence_schema_version");
  writer.IntegerValue(kPhase4SameRunRawEvidenceSchemaVersion);
  writer.Key("wire_schema_version");
  writer.IntegerValue(kPhase4SameRunTrialWireSchemaVersion);
  writer.Key("source_commit");
  writer.String(source_commit);
  writer.Key("source_stamped");
  writer.Bool(source_stamped);
  writer.Key("source_tree_dirty");
  writer.Bool(source_tree_dirty);
  writer.Key("source_envelope_checksum");
  writer.IntegerValue(ComputePhase4SourceEnvelopeChecksumV2(
      kPhase4SameRunRawEvidenceSchemaVersion, kPhase4SameRunTrialWireSchemaVersion, source_commit,
      source_stamped, source_tree_dirty, result.artifact_checksum));
  WriteIsolatedCell(&writer, result);
  writer.EndObject();
  return std::move(writer).Finish();
}

}  // namespace apgar::benchmark
