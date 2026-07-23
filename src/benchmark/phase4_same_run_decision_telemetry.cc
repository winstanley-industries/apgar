#include "apgar/benchmark/phase4_same_run_decision_telemetry.h"

#include <cstdint>
#include <cstdlib>
#include <new>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "apgar/board_ir/stable_hash.h"
#include "src/benchmark/phase4_trial_wire_internal.h"

namespace apgar::benchmark {
namespace {

class JsonWriter {
 public:
  void BeginObject() {
    output_.push_back('{');
    first_.push_back(true);
  }
  void EndObject() {
    output_.push_back('}');
    first_.pop_back();
  }
  void BeginArray() {
    output_.push_back('[');
    first_.push_back(true);
  }
  void EndArray() {
    output_.push_back(']');
    first_.pop_back();
  }
  void Key(std::string_view key) {
    Separate();
    String(key);
    output_.push_back(':');
  }
  void Element() { Separate(); }
  void String(std::string_view value) {
    constexpr char kHex[] = "0123456789abcdef";
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
            output_.push_back(kHex[(character >> 4U) & 0x0fU]);
            output_.push_back(kHex[character & 0x0fU]);
          } else {
            output_.push_back(static_cast<char>(character));
          }
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
    IntegerValue(static_cast<std::underlying_type_t<Enum>>(value));
  }
  void Bool(bool value) { output_ += value ? "true" : "false"; }
  [[nodiscard]] std::string Finish() && {
    output_.push_back('\n');
    return std::move(output_);
  }

 private:
  void Separate() {
    if (!first_.back()) {
      output_.push_back(',');
    }
    first_.back() = false;
  }

  std::string output_;
  std::vector<bool> first_;
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

void WriteConfig(JsonWriter* writer, const Phase4CanonicalCellConfig& config) {
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

void WriteOutcome(JsonWriter* writer, const Phase4BoardOutcome& outcome) {
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

void WriteColumnOutcomes(JsonWriter* writer, const Phase4PerNetColumnOutcomesV1& columns) {
  writer->BeginObject();
  writer->Key("requested_columns");
  writer->IntegerValue(columns.requested_columns);
  writer->Key("executed_route_queries");
  writer->IntegerValue(columns.executed_route_queries);
  writer->Key("admitted_candidates");
  writer->IntegerValue(columns.admitted_candidates);
  writer->Key("duplicate_candidates");
  writer->IntegerValue(columns.duplicate_candidates);
  writer->Key("disconnected_columns");
  writer->IntegerValue(columns.disconnected_columns);
  writer->Key("unsupported_columns");
  writer->IntegerValue(columns.unsupported_columns);
  writer->Key("skipped_columns");
  writer->IntegerValue(columns.skipped_columns);
  writer->Key("exact_validation_rejections");
  writer->IntegerValue(columns.exact_validation_rejections);
  writer->Key("other_rejections");
  writer->IntegerValue(columns.other_rejections);
  writer->EndObject();
}

void WriteTelemetry(JsonWriter* writer, const Phase4SameRunArmDecisionTelemetryV1& telemetry) {
  writer->BeginObject();
  writer->Key("schema_version");
  writer->IntegerValue(telemetry.schema_version);
  writer->Key("associated_semantic_checksum");
  writer->IntegerValue(telemetry.associated_semantic_checksum);
  writer->Key("outcome");
  WriteOutcome(writer, telemetry.outcome);
  writer->Key("per_net");
  writer->BeginArray();
  for (const Phase4SameRunPerNetColumnOutcomesV1& row : telemetry.per_net) {
    writer->Element();
    writer->BeginObject();
    writer->Key("net");
    writer->BeginObject();
    writer->Key("id");
    writer->IntegerValue(row.net.id);
    writer->Key("generation");
    writer->IntegerValue(row.net.generation);
    writer->EndObject();
    writer->Key("columns");
    WriteColumnOutcomes(writer, row.columns);
    writer->EndObject();
  }
  writer->EndArray();
  writer->Key("telemetry_checksum");
  writer->IntegerValue(telemetry.telemetry_checksum);
  writer->EndObject();
}

void WriteArmCapture(JsonWriter* writer,
                     const Phase4IsolatedSameRunArmDecisionTelemetryV1& capture) {
  writer->BeginObject();
  writer->Key("schema_version");
  writer->IntegerValue(capture.schema_version);
  writer->Key("arm");
  writer->EnumValue(capture.arm);
  writer->Key("repetition_index");
  writer->IntegerValue(capture.repetition_index);
  writer->Key("execution_order");
  writer->EnumValue(capture.execution_order);
  writer->Key("dispatch_ordinal");
  writer->IntegerValue(capture.dispatch_ordinal);
  writer->Key("process_instance_identity");
  writer->IntegerValue(capture.process_instance_identity);
  writer->Key("associated_semantic_checksum");
  writer->IntegerValue(capture.associated_semantic_checksum);
  writer->Key("associated_arm_artifact_checksum");
  writer->IntegerValue(capture.associated_arm_artifact_checksum);
  writer->Key("associated_authority_checksum");
  writer->IntegerValue(capture.associated_authority_checksum);
  writer->Key("associated_arm_attempt_checksum");
  writer->IntegerValue(capture.associated_arm_attempt_checksum);
  writer->Key("telemetry");
  WriteTelemetry(writer, capture.telemetry);
  writer->Key("capture_checksum");
  writer->IntegerValue(capture.capture_checksum);
  writer->EndObject();
}

void WritePairCapture(JsonWriter* writer,
                      const Phase4IsolatedSameRunPairDecisionTelemetryV1& capture) {
  writer->BeginObject();
  writer->Key("schema_version");
  writer->IntegerValue(capture.schema_version);
  writer->Key("case_id");
  writer->IntegerValue(capture.case_id);
  writer->Key("requested_pool_size");
  writer->IntegerValue(capture.requested_pool_size);
  writer->Key("repetition_index");
  writer->IntegerValue(capture.repetition_index);
  writer->Key("root_seed");
  writer->IntegerValue(capture.root_seed);
  writer->Key("execution_order");
  writer->EnumValue(capture.execution_order);
  writer->Key("associated_raw_pair_attempt_checksum");
  writer->IntegerValue(capture.associated_raw_pair_attempt_checksum);
  writer->Key("associated_paired_semantic_checksum");
  writer->IntegerValue(capture.associated_paired_semantic_checksum);
  writer->Key("associated_paired_artifact_checksum");
  writer->IntegerValue(capture.associated_paired_artifact_checksum);
  writer->Key("baseline");
  WriteArmCapture(writer, capture.baseline);
  writer->Key("candidate");
  WriteArmCapture(writer, capture.candidate);
  writer->Key("capture_checksum");
  writer->IntegerValue(capture.capture_checksum);
  writer->EndObject();
}

}  // namespace

std::uint64_t ComputePhase4SameRunDecisionTelemetrySourceEnvelopeChecksumV1(
    std::string_view source_commit, bool source_stamped, bool source_tree_dirty,
    std::uint64_t artifact_checksum) noexcept {
  board_ir::StableHashBuilder hash;
  hash.AddString("APGAR-PHASE4-SAME-RUN-DECISION-TELEMETRY-SOURCE-ENVELOPE-V1");
  hash.AddString(source_commit);
  hash.AddBool(source_stamped);
  hash.AddBool(source_tree_dirty);
  hash.AddU64(artifact_checksum);
  return hash.Finish();
}

std::optional<std::string> SerializePhase4SameRunDecisionTelemetryJsonV1(
    const Phase4IsolatedCellWithSameRunDecisionTelemetryV1& capture,
    std::string_view imported_fixture, std::string_view source_commit, bool source_stamped,
    bool source_tree_dirty) {
  try {
    const Phase4IsolatedCellResult& raw = capture.raw_cell;
    if (!ValidatePhase4IsolatedSameRunCellCaptureV1(capture, imported_fixture)) {
      return std::nullopt;
    }
#if defined(APGAR_PHASE4_TRIAL_FAULT_TEST_VARIANT)
    const char* const fault_mode = std::getenv("APGAR_PHASE4_TRIAL_FAULT_MODE");
    if (fault_mode != nullptr && std::string_view(fault_mode) == "sidecar_serialize_bad_alloc") {
      throw std::bad_alloc();
    }
#endif
    const std::uint64_t raw_source_envelope_checksum = ComputePhase4SourceEnvelopeChecksumV2(
        kPhase4SameRunRawEvidenceSchemaVersion, kPhase4SameRunTrialWireSchemaVersion, source_commit,
        source_stamped, source_tree_dirty, raw.artifact_checksum);
    const std::uint64_t artifact_checksum =
        ComputePhase4IsolatedSameRunCellCaptureChecksumV1(capture, raw_source_envelope_checksum);
    JsonWriter writer;
    writer.BeginObject();
    writer.Key("source_commit");
    writer.String(source_commit);
    writer.Key("source_stamped");
    writer.Bool(source_stamped);
    writer.Key("source_tree_dirty");
    writer.Bool(source_tree_dirty);
    writer.Key("source_envelope_checksum");
    writer.IntegerValue(ComputePhase4SameRunDecisionTelemetrySourceEnvelopeChecksumV1(
        source_commit, source_stamped, source_tree_dirty, artifact_checksum));
    writer.Key("schema_version");
    writer.IntegerValue(kPhase4SameRunDecisionTelemetryArtifactSchemaVersion);
    writer.Key("raw_evidence_schema_version");
    writer.IntegerValue(kPhase4SameRunRawEvidenceSchemaVersion);
    writer.Key("raw_wire_schema_version");
    writer.IntegerValue(kPhase4SameRunTrialWireSchemaVersion);
    writer.Key("telemetry_wire_schema_version");
    writer.IntegerValue(internal::kPhase4TrialWireSchemaVersionV2);
    writer.Key("config");
    WriteConfig(&writer, raw.config);
    writer.Key("corpus_checksum");
    writer.IntegerValue(raw.corpus_checksum);
    writer.Key("raw_cell_plan_checksum");
    writer.IntegerValue(raw.cell_plan_checksum);
    writer.Key("raw_environment_checksum");
    writer.IntegerValue(raw.environment.environment_checksum);
    writer.Key("raw_authority_run_identity");
    writer.IntegerValue(raw.authority_run_identity);
    writer.Key("raw_controller_identity");
    writer.IntegerValue(raw.controller_identity);
    writer.Key("raw_cell_artifact_checksum");
    writer.IntegerValue(raw.artifact_checksum);
    writer.Key("raw_source_envelope_checksum");
    writer.IntegerValue(raw_source_envelope_checksum);
    writer.Key("attempts");
    writer.BeginArray();
    for (const Phase4IsolatedSameRunPairDecisionTelemetryV1& attempt : capture.same_run_attempts) {
      writer.Element();
      WritePairCapture(&writer, attempt);
    }
    writer.EndArray();
    writer.Key("artifact_checksum");
    writer.IntegerValue(artifact_checksum);
    writer.EndObject();
    return std::move(writer).Finish();
  } catch (...) {
    return std::nullopt;
  }
}

}  // namespace apgar::benchmark
