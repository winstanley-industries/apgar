#include "apgar/benchmark/phase4_operational_artifact.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "apgar/board_ir/stable_hash.h"
#include "src/benchmark/phase4_paired_trial_internal.h"

namespace apgar::benchmark {
namespace {

constexpr std::size_t kMaximumWorkerOutputBytes = 4U * 1024U * 1024U;

[[nodiscard]] bool IsLowerHexCommit(std::string_view value) noexcept {
  if (value.size() != 40) {
    return false;
  }
  for (const char character : value) {
    if (!((character >= '0' && character <= '9') || (character >= 'a' && character <= 'f'))) {
      return false;
    }
  }
  return true;
}

class JsonWriter {
 public:
  void BeginObject() {
    output_.push_back('{');
    scopes_.push_back(true);
  }
  void EndObject() {
    output_.push_back('}');
    scopes_.pop_back();
  }
  void BeginArray() {
    output_.push_back('[');
    scopes_.push_back(true);
  }
  void EndArray() {
    output_.push_back(']');
    scopes_.pop_back();
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
    IntegerValue(static_cast<std::underlying_type_t<Enum>>(value));
  }
  void Bool(bool value) { output_ += value ? "true" : "false"; }
  void Null() { output_ += "null"; }
  [[nodiscard]] std::optional<std::string> Finish() && {
    output_.push_back('\n');
    if (!scopes_.empty() || output_.size() > kMaximumWorkerOutputBytes) {
      return std::nullopt;
    }
    return std::move(output_);
  }

 private:
  void Separate() {
    if (scopes_.back() == 0) {
      output_.push_back(',');
    }
    scopes_.back() = 0;
  }

  std::string output_;
  std::vector<std::uint8_t> scopes_;
};

void WriteExternalBudget(JsonWriter* writer, const Phase4ExternalBudget& value) {
  writer->BeginObject();
  writer->Key("maximum_prepared_elapsed_nanoseconds");
  writer->IntegerValue(value.maximum_prepared_elapsed_nanoseconds);
  writer->Key("maximum_cold_elapsed_nanoseconds");
  writer->IntegerValue(value.maximum_cold_elapsed_nanoseconds);
  writer->Key("maximum_address_space_bytes");
  writer->IntegerValue(value.maximum_address_space_bytes);
  writer->Key("maximum_peak_host_bytes");
  writer->IntegerValue(value.maximum_peak_host_bytes);
  writer->EndObject();
}

void WriteOpportunity(JsonWriter* writer, const Phase4RouteOpportunity& value) {
  writer->BeginObject();
  writer->Key("route_queries");
  writer->IntegerValue(value.route_queries);
  writer->Key("route_work_units");
  writer->IntegerValue(value.route_work_units);
  writer->EndObject();
}

void WriteOutcome(JsonWriter* writer, const Phase4BoardOutcome& value) {
  writer->BeginObject();
  writer->Key("selected_net_count");
  writer->IntegerValue(value.selected_net_count);
  writer->Key("no_candidate_net_count");
  writer->IntegerValue(value.no_candidate_net_count);
  writer->Key("overused_resource_count");
  writer->IntegerValue(value.overused_resource_count);
  writer->Key("total_overuse_units");
  writer->IntegerValue(value.total_overuse_units);
  writer->Key("total_intrinsic_cost");
  writer->IntegerValue(value.total_intrinsic_cost);
  writer->Key("world_checksum");
  writer->IntegerValue(value.world_checksum);
  writer->EndObject();
}

void WriteSemantics(JsonWriter* writer, const Phase4TrialArmSemantics& value) {
  writer->BeginObject();
  writer->Key("schema_version");
  writer->IntegerValue(value.schema_version);
  writer->Key("arm");
  writer->EnumValue(value.arm);
  writer->Key("execution_order");
  writer->EnumValue(value.execution_order);
  writer->Key("corpus_version");
  writer->IntegerValue(value.corpus_version);
  writer->Key("corpus_checksum");
  writer->IntegerValue(value.corpus_checksum);
  writer->Key("case_id");
  writer->IntegerValue(value.case_id);
  writer->Key("descriptor_fingerprint");
  writer->IntegerValue(value.descriptor_fingerprint);
  writer->Key("case_checksum");
  writer->IntegerValue(value.case_checksum);
  writer->Key("board_content_hash");
  writer->IntegerValue(value.board_content_hash);
  writer->Key("workload_checksum");
  writer->IntegerValue(value.workload_checksum);
  writer->Key("capacity_model_checksum");
  writer->IntegerValue(value.capacity_model_checksum);
  writer->Key("budget_checksum");
  writer->IntegerValue(value.budget_checksum);
  writer->Key("workload_net_count");
  writer->IntegerValue(value.workload_net_count);
  writer->Key("requested_pool_size");
  writer->IntegerValue(value.requested_pool_size);
  writer->Key("repetition_index");
  writer->IntegerValue(value.repetition_index);
  writer->Key("root_seed");
  writer->IntegerValue(value.root_seed);
  writer->Key("preparation_worker_count");
  writer->IntegerValue(value.preparation_worker_count);
  writer->Key("baseline_sweeps");
  writer->IntegerValue(value.baseline_sweeps);
  writer->Key("candidate_regeneration_epochs");
  writer->IntegerValue(value.candidate_regeneration_epochs);
  writer->Key("candidate_columns_per_epoch");
  writer->IntegerValue(value.candidate_columns_per_epoch);
  writer->Key("candidate_terminal_selection_rounds");
  writer->IntegerValue(value.candidate_terminal_selection_rounds);
  writer->Key("external_budget");
  WriteExternalBudget(writer, value.external_budget);
  writer->Key("opportunity");
  WriteOpportunity(writer, value.opportunity);
  writer->Key("actual");
  WriteOpportunity(writer, value.actual);
  writer->Key("preparation_route_queries");
  writer->IntegerValue(value.preparation_route_queries);
  writer->Key("preparation_route_work_units");
  writer->IntegerValue(value.preparation_route_work_units);
  writer->Key("regeneration_route_queries");
  writer->IntegerValue(value.regeneration_route_queries);
  writer->Key("regeneration_route_work_units");
  writer->IntegerValue(value.regeneration_route_work_units);
  writer->Key("requested_columns");
  writer->IntegerValue(value.requested_columns);
  writer->Key("admitted_candidates");
  writer->IntegerValue(value.admitted_candidates);
  writer->Key("rejected_columns");
  writer->IntegerValue(value.rejected_columns);
  writer->Key("final_candidate_count");
  writer->IntegerValue(value.final_candidate_count);
  writer->Key("preparation_checksum");
  writer->IntegerValue(value.preparation_checksum);
  writer->Key("algorithm_session_checksum");
  writer->IntegerValue(value.algorithm_session_checksum);
  writer->Key("final_pool_manifest_checksum");
  writer->IntegerValue(value.final_pool_manifest_checksum);
  writer->Key("final_rejection_manifest_checksum");
  writer->IntegerValue(value.final_rejection_manifest_checksum);
  writer->Key("terminal_reason");
  writer->EnumValue(value.terminal_reason);
  writer->Key("candidate_outcome_source");
  writer->EnumValue(value.candidate_outcome_source);
  writer->Key("outcome");
  WriteOutcome(writer, value.outcome);
  writer->Key("semantic_checksum");
  writer->IntegerValue(value.semantic_checksum);
  writer->EndObject();
}

void WriteLifecycle(JsonWriter* writer, const Phase4PreparerLifecycleObservation& value) {
  writer->BeginObject();
  writer->Key("workers_started_before");
  writer->IntegerValue(value.workers_started_before);
  writer->Key("workers_started_after");
  writer->IntegerValue(value.workers_started_after);
  writer->Key("invocations_started_before");
  writer->IntegerValue(value.invocations_started_before);
  writer->Key("invocations_started_after");
  writer->IntegerValue(value.invocations_started_after);
  writer->Key("invocations_completed_before");
  writer->IntegerValue(value.invocations_completed_before);
  writer->Key("invocations_completed_after");
  writer->IntegerValue(value.invocations_completed_after);
  writer->EndObject();
}

void WriteExecution(JsonWriter* writer, const Phase4TrialArmExecution& value) {
  writer->BeginObject();
  writer->Key("semantics");
  WriteSemantics(writer, value.semantics);
  writer->Key("case_build_elapsed_nanoseconds");
  writer->IntegerValue(value.case_build_elapsed_nanoseconds);
  writer->Key("prepared_elapsed_nanoseconds");
  writer->IntegerValue(value.prepared_elapsed_nanoseconds);
  writer->Key("cold_elapsed_nanoseconds");
  writer->IntegerValue(value.cold_elapsed_nanoseconds);
  writer->Key("preparer_lifecycle");
  WriteLifecycle(writer, value.preparer_lifecycle);
  writer->EndObject();
}

void WriteApplicability(JsonWriter* writer, const Phase4OperationalApplicabilityV1& value) {
  writer->BeginObject();
  writer->Key("status");
  writer->EnumValue(value.status);
  writer->Key("reason");
  writer->EnumValue(value.reason);
  writer->EndObject();
}

void WriteCaseBuild(JsonWriter* writer, const Phase4RepresentativeCaseOperationalProfileV1& value) {
  writer->BeginObject();
  writer->Key("case_source");
  writer->EnumValue(value.case_source);
  writer->Key("fixture_import_applicability");
  WriteApplicability(writer, value.fixture_import_applicability);
  writer->Key("synthetic_materialization_applicability");
  WriteApplicability(writer, value.synthetic_materialization_applicability);
  writer->Key("compile_probe_applicability");
  WriteApplicability(writer, value.compile_probe_applicability);
  writer->Key("descriptor_validation_and_bound_preflight_wall_nanoseconds");
  writer->IntegerValue(value.descriptor_validation_and_bound_preflight_wall_nanoseconds);
  writer->Key("fixture_identity_and_import_wall_nanoseconds");
  writer->IntegerValue(value.fixture_identity_and_import_wall_nanoseconds);
  writer->Key("synthetic_geometry_and_board_materialization_wall_nanoseconds");
  writer->IntegerValue(value.synthetic_geometry_and_board_materialization_wall_nanoseconds);
  writer->Key("geometry_compilation_probe_wall_nanoseconds");
  writer->IntegerValue(value.geometry_compilation_probe_wall_nanoseconds);
  writer->Key("workload_geometry_compilation_wall_nanoseconds");
  writer->IntegerValue(value.workload_geometry_compilation_wall_nanoseconds);
  writer->Key("capacity_and_case_assembly_wall_nanoseconds");
  writer->IntegerValue(value.capacity_and_case_assembly_wall_nanoseconds);
  writer->Key("component_wall_nanoseconds");
  writer->IntegerValue(value.component_wall_nanoseconds);
  writer->Key("unclassified_and_release_wall_nanoseconds");
  writer->IntegerValue(value.unclassified_and_release_wall_nanoseconds);
  writer->EndObject();
}

void WriteBaseline(JsonWriter* writer,
                   const allocator::SequentialNegotiatedBaselineOperationalProfileV1& value) {
  writer->BeginObject();
#define APGAR_WRITE_BASELINE_FIELD(name) \
  writer->Key(#name);                    \
  writer->IntegerValue(value.name)
  APGAR_WRITE_BASELINE_FIELD(component_wall_nanoseconds);
  APGAR_WRITE_BASELINE_FIELD(validation_and_initialization_wall_nanoseconds);
  APGAR_WRITE_BASELINE_FIELD(scheduling_and_policy_projection_wall_nanoseconds);
  APGAR_WRITE_BASELINE_FIELD(candidate_generation_wall_nanoseconds);
  APGAR_WRITE_BASELINE_FIELD(exact_admission_and_store_publication_wall_nanoseconds);
  APGAR_WRITE_BASELINE_FIELD(incremental_resource_accumulation_wall_nanoseconds);
  APGAR_WRITE_BASELINE_FIELD(sweep_selection_and_resource_replay_wall_nanoseconds);
  APGAR_WRITE_BASELINE_FIELD(price_update_wall_nanoseconds);
  APGAR_WRITE_BASELINE_FIELD(final_assembly_wall_nanoseconds);
  APGAR_WRITE_BASELINE_FIELD(unclassified_serial_wall_nanoseconds);
#undef APGAR_WRITE_BASELINE_FIELD
  writer->EndObject();
}

void WritePreparation(JsonWriter* writer,
                      const allocator::CpuCandidatePoolPreparationOperationalProfileV1& value) {
  writer->BeginObject();
#define APGAR_WRITE_PREPARATION_FIELD(name) \
  writer->Key(#name);                       \
  writer->IntegerValue(value.name)
  APGAR_WRITE_PREPARATION_FIELD(component_wall_nanoseconds);
  APGAR_WRITE_PREPARATION_FIELD(validation_and_scheduling_wall_nanoseconds);
  APGAR_WRITE_PREPARATION_FIELD(base_worker_wave_wall_nanoseconds);
  APGAR_WRITE_PREPARATION_FIELD(alternative_policy_wall_nanoseconds);
  APGAR_WRITE_PREPARATION_FIELD(alternative_worker_wave_wall_nanoseconds);
  APGAR_WRITE_PREPARATION_FIELD(route_and_candidate_build_worker_sum_nanoseconds);
  APGAR_WRITE_PREPARATION_FIELD(exact_admission_and_store_publication_wall_nanoseconds);
  APGAR_WRITE_PREPARATION_FIELD(publication_correlation_and_pool_materialization_wall_nanoseconds);
  APGAR_WRITE_PREPARATION_FIELD(unclassified_serial_wall_nanoseconds);
  APGAR_WRITE_PREPARATION_FIELD(base_jobs_dispatched);
  APGAR_WRITE_PREPARATION_FIELD(alternative_jobs_dispatched);
#undef APGAR_WRITE_PREPARATION_FIELD
  writer->EndObject();
}

void WritePlanningProfile(
    JsonWriter* writer, const allocator::TargetedRegenerationPlanningOperationalProfileV1& value) {
  writer->BeginObject();
#define APGAR_WRITE_PLAN_FIELD(name) \
  writer->Key(#name);                \
  writer->IntegerValue(value.name)
  APGAR_WRITE_PLAN_FIELD(epoch_index);
  APGAR_WRITE_PLAN_FIELD(plan_checksum);
  APGAR_WRITE_PLAN_FIELD(component_wall_nanoseconds);
  APGAR_WRITE_PLAN_FIELD(source_selection_and_resource_accumulation_wall_nanoseconds);
  APGAR_WRITE_PLAN_FIELD(price_update_wall_nanoseconds);
  APGAR_WRITE_PLAN_FIELD(next_price_selection_and_resource_accumulation_wall_nanoseconds);
  APGAR_WRITE_PLAN_FIELD(target_ranking_retention_and_assembly_wall_nanoseconds);
  APGAR_WRITE_PLAN_FIELD(unclassified_serial_wall_nanoseconds);
  APGAR_WRITE_PLAN_FIELD(price_update_operations);
#undef APGAR_WRITE_PLAN_FIELD
  writer->EndObject();
}

void WriteExecutionCounters(JsonWriter* writer,
                            const allocator::TargetedRegenerationExecutionCounters& value) {
  writer->BeginObject();
#define APGAR_WRITE_EXECUTION_COUNTER(name) \
  writer->Key(#name);                       \
  writer->IntegerValue(value.name)
  APGAR_WRITE_EXECUTION_COUNTER(requested_columns);
  APGAR_WRITE_EXECUTION_COUNTER(route_queries);
  APGAR_WRITE_EXECUTION_COUNTER(route_work_units);
  APGAR_WRITE_EXECUTION_COUNTER(policy_projection_visits);
  APGAR_WRITE_EXECUTION_COUNTER(peak_route_record_count);
  APGAR_WRITE_EXECUTION_COUNTER(peak_route_queue_size);
  APGAR_WRITE_EXECUTION_COUNTER(generated_candidate_bytes);
  APGAR_WRITE_EXECUTION_COUNTER(rejection_record_bytes);
  APGAR_WRITE_EXECUTION_COUNTER(transient_result_bytes);
  APGAR_WRITE_EXECUTION_COUNTER(successful_routes);
  APGAR_WRITE_EXECUTION_COUNTER(built_candidates);
  APGAR_WRITE_EXECUTION_COUNTER(admitted_candidates);
  APGAR_WRITE_EXECUTION_COUNTER(duplicate_candidates);
  APGAR_WRITE_EXECUTION_COUNTER(rejected_columns);
  APGAR_WRITE_EXECUTION_COUNTER(novel_retained_candidates);
  APGAR_WRITE_EXECUTION_COUNTER(changed_selections);
  APGAR_WRITE_EXECUTION_COUNTER(successor_pinned_candidates);
#undef APGAR_WRITE_EXECUTION_COUNTER
  writer->EndObject();
}

void WriteEpochProfile(JsonWriter* writer,
                       const allocator::TargetedRegenerationOperationalProfileV1& value) {
  writer->BeginObject();
  writer->Key("epoch_index");
  writer->IntegerValue(value.epoch_index);
  writer->Key("plan_checksum");
  writer->IntegerValue(value.plan_checksum);
  writer->Key("execution_checksum");
  writer->IntegerValue(value.execution_checksum);
  writer->Key("counters");
  WriteExecutionCounters(writer, value.counters);
#define APGAR_WRITE_EPOCH_FIELD(name) \
  writer->Key(#name);                 \
  writer->IntegerValue(value.name)
  APGAR_WRITE_EPOCH_FIELD(component_wall_nanoseconds);
  APGAR_WRITE_EPOCH_FIELD(validation_and_preflight_wall_nanoseconds);
  APGAR_WRITE_EPOCH_FIELD(baseline_selection_and_source_store_preflight_wall_nanoseconds);
  APGAR_WRITE_EPOCH_FIELD(policy_projection_and_candidate_generation_wall_nanoseconds);
  APGAR_WRITE_EPOCH_FIELD(exact_admission_and_store_publication_wall_nanoseconds);
  APGAR_WRITE_EPOCH_FIELD(publication_correlation_wall_nanoseconds);
  APGAR_WRITE_EPOCH_FIELD(refreshed_selection_and_resource_accumulation_wall_nanoseconds);
  APGAR_WRITE_EPOCH_FIELD(successor_retention_wall_nanoseconds);
  APGAR_WRITE_EPOCH_FIELD(final_assembly_wall_nanoseconds);
  APGAR_WRITE_EPOCH_FIELD(unclassified_serial_wall_nanoseconds);
#undef APGAR_WRITE_EPOCH_FIELD
  writer->EndObject();
}

void WriteSessionCounters(JsonWriter* writer,
                          const allocator::CpuCandidateAllocationSessionCounters& value) {
  writer->BeginObject();
#define APGAR_WRITE_SESSION_COUNTER(name) \
  writer->Key(#name);                     \
  writer->IntegerValue(value.name)
  APGAR_WRITE_SESSION_COUNTER(completed_regeneration_epochs);
  APGAR_WRITE_SESSION_COUNTER(planning_expanded_resource_visits);
  APGAR_WRITE_SESSION_COUNTER(requested_columns);
  APGAR_WRITE_SESSION_COUNTER(route_queries);
  APGAR_WRITE_SESSION_COUNTER(route_work_units);
  APGAR_WRITE_SESSION_COUNTER(policy_projection_visits);
  APGAR_WRITE_SESSION_COUNTER(generated_candidate_bytes);
  APGAR_WRITE_SESSION_COUNTER(rejection_record_bytes);
  APGAR_WRITE_SESSION_COUNTER(transient_result_bytes);
  APGAR_WRITE_SESSION_COUNTER(admitted_candidates);
  APGAR_WRITE_SESSION_COUNTER(duplicate_candidates);
  APGAR_WRITE_SESSION_COUNTER(rejected_columns);
  APGAR_WRITE_SESSION_COUNTER(novel_retained_candidates);
  APGAR_WRITE_SESSION_COUNTER(changed_selections);
#undef APGAR_WRITE_SESSION_COUNTER
  writer->EndObject();
}

void WriteWitness(JsonWriter* writer,
                  const allocator::CpuCandidateAllocationSessionReplayWitnessV1& value) {
  writer->BeginObject();
  writer->Key("session_checksum");
  writer->IntegerValue(value.session_checksum);
  writer->Key("board_content_hash");
  writer->IntegerValue(value.board_content_hash);
  writer->Key("workload_checksum");
  writer->IntegerValue(value.workload_checksum);
  writer->Key("capacity_model_checksum");
  writer->IntegerValue(value.capacity_model_checksum);
  writer->Key("preparation_checksum");
  writer->IntegerValue(value.preparation_checksum);
  writer->Key("maximum_regeneration_epochs");
  writer->IntegerValue(value.maximum_regeneration_epochs);
  writer->Key("terminal_reason");
  writer->EnumValue(value.terminal_reason);
  writer->Key("counters");
  WriteSessionCounters(writer, value.counters);
  writer->Key("epoch_record_count");
  writer->IntegerValue(value.epoch_record_count);
  writer->Key("epoch_association_checksum");
  writer->IntegerValue(value.epoch_association_checksum);
  writer->Key("final_pool_manifest_checksum");
  writer->IntegerValue(value.final_pool_manifest_checksum);
  writer->Key("final_rejection_manifest_checksum");
  writer->IntegerValue(value.final_rejection_manifest_checksum);
  writer->EndObject();
}

void WriteMultiWorld(JsonWriter* writer, const allocator::MultiWorldOperationalProfileV1& value) {
  writer->BeginObject();
#define APGAR_WRITE_WORLD_FIELD(name) \
  writer->Key(#name);                 \
  writer->IntegerValue(value.name)
  APGAR_WRITE_WORLD_FIELD(component_wall_nanoseconds);
  APGAR_WRITE_WORLD_FIELD(validation_and_source_preflight_wall_nanoseconds);
  APGAR_WRITE_WORLD_FIELD(selection_and_resource_accumulation_wall_nanoseconds);
  APGAR_WRITE_WORLD_FIELD(price_update_and_snapshot_wall_nanoseconds);
  APGAR_WRITE_WORLD_FIELD(terminal_retention_and_assembly_wall_nanoseconds);
  APGAR_WRITE_WORLD_FIELD(unclassified_serial_wall_nanoseconds);
#undef APGAR_WRITE_WORLD_FIELD
  writer->EndObject();
}

void WriteSession(JsonWriter* writer,
                  const allocator::CpuCandidateAllocationSessionOperationalProfileV1& value) {
  writer->BeginObject();
#define APGAR_WRITE_SESSION_FIELD(name) \
  writer->Key(#name);                   \
  writer->IntegerValue(value.name)
  APGAR_WRITE_SESSION_FIELD(component_wall_nanoseconds);
  APGAR_WRITE_SESSION_FIELD(validation_and_source_inspection_wall_nanoseconds);
  APGAR_WRITE_SESSION_FIELD(initial_price_state_wall_nanoseconds);
  APGAR_WRITE_SESSION_FIELD(initial_selection_and_resource_accumulation_wall_nanoseconds);
  APGAR_WRITE_SESSION_FIELD(targeted_regeneration_planning_wall_nanoseconds);
  APGAR_WRITE_SESSION_FIELD(targeted_regeneration_price_update_wall_nanoseconds);
  APGAR_WRITE_SESSION_FIELD(targeted_regeneration_selection_and_target_planning_wall_nanoseconds);
  APGAR_WRITE_SESSION_FIELD(targeted_regeneration_execution_wall_nanoseconds);
  APGAR_WRITE_SESSION_FIELD(successor_correlation_wall_nanoseconds);
  APGAR_WRITE_SESSION_FIELD(terminal_multi_world_component_wall_nanoseconds);
  APGAR_WRITE_SESSION_FIELD(terminal_multi_world_price_update_wall_nanoseconds);
  APGAR_WRITE_SESSION_FIELD(final_manifest_and_assembly_wall_nanoseconds);
  APGAR_WRITE_SESSION_FIELD(unclassified_serial_wall_nanoseconds);
  APGAR_WRITE_SESSION_FIELD(planning_expanded_resource_visits);
#undef APGAR_WRITE_SESSION_FIELD
  writer->Key("regeneration_plans");
  writer->BeginArray();
  for (const auto& plan : value.regeneration_plans) {
    writer->Element();
    WritePlanningProfile(writer, plan);
  }
  writer->EndArray();
  writer->Key("regeneration_epochs");
  writer->BeginArray();
  for (const auto& epoch : value.regeneration_epochs) {
    writer->Element();
    WriteEpochProfile(writer, epoch);
  }
  writer->EndArray();
  writer->Key("terminal_multi_world");
  WriteMultiWorld(writer, value.terminal_multi_world);
  writer->Key("replay_witness");
  WriteWitness(writer, value.replay_witness);
  writer->EndObject();
}

void WriteOperationalProfile(JsonWriter* writer, const Phase4TrialArmOperationalProfileV1& value) {
  writer->BeginObject();
  writer->Key("schema_version");
  writer->IntegerValue(value.schema_version);
  writer->Key("execution");
  WriteExecution(writer, value.execution);
  writer->Key("case_build");
  WriteCaseBuild(writer, value.case_build);
  writer->Key("process_cpu");
  WriteApplicability(writer, value.process_cpu);
  writer->Key("peak_host_memory");
  WriteApplicability(writer, value.peak_host_memory);
  writer->Key("compatible_batch_formation_and_fill");
  WriteApplicability(writer, value.compatible_batch_formation_and_fill);
  writer->Key("compact_readback");
  WriteApplicability(writer, value.compact_readback);
  writer->Key("prepared_view_cache_and_cache_misses");
  WriteApplicability(writer, value.prepared_view_cache_and_cache_misses);
  writer->Key("initial_device_upload");
  WriteApplicability(writer, value.initial_device_upload);
  writer->Key("gpu_utilization");
  WriteApplicability(writer, value.gpu_utilization);
  writer->Key("peak_device_memory");
  WriteApplicability(writer, value.peak_device_memory);
  writer->Key("contender_transient_release_tail_wall_nanoseconds");
  writer->IntegerValue(value.contender_transient_release_tail_wall_nanoseconds);
  writer->Key("unclassified_prepared_scope_wall_nanoseconds");
  writer->IntegerValue(value.unclassified_prepared_scope_wall_nanoseconds);
  writer->Key("unclassified_cold_scope_exit_wall_nanoseconds");
  writer->IntegerValue(value.unclassified_cold_scope_exit_wall_nanoseconds);
  writer->Key("baseline");
  if (value.baseline.has_value()) {
    WriteBaseline(writer, *value.baseline);
  } else {
    writer->Null();
  }
  writer->Key("preparation");
  if (value.preparation.has_value()) {
    WritePreparation(writer, *value.preparation);
  } else {
    writer->Null();
  }
  writer->Key("candidate_session");
  if (value.candidate_session.has_value()) {
    WriteSession(writer, *value.candidate_session);
  } else {
    writer->Null();
  }
  writer->Key("profile_checksum");
  writer->IntegerValue(value.profile_checksum);
  writer->EndObject();
}

void WriteReplayAuthority(JsonWriter* writer, const Phase4TrialArmReplayAuthorityV1& value) {
  writer->BeginObject();
  writer->Key("schema_version");
  writer->IntegerValue(value.schema_version);
  writer->Key("semantics");
  WriteSemantics(writer, value.semantics);
  writer->Key("preparer_lifecycle");
  WriteLifecycle(writer, value.preparer_lifecycle);
  writer->Key("recomputed_full_preimage_session_checksum");
  writer->IntegerValue(value.recomputed_full_preimage_session_checksum);
  writer->Key("candidate_session_witness");
  if (value.candidate_session_witness.has_value()) {
    WriteWitness(writer, *value.candidate_session_witness);
  } else {
    writer->Null();
  }
  writer->Key("authority_checksum");
  writer->IntegerValue(value.authority_checksum);
  writer->EndObject();
}

[[nodiscard]] std::uint64_t WorkerArtifactChecksum(Phase4OperationalWorkerOutputKind kind,
                                                   std::uint64_t payload_checksum) noexcept {
  board_ir::StableHashBuilder hash;
  hash.AddString("APGAR-PHASE4-OPERATIONAL-WORKER-OUTPUT-ARTIFACT-V1");
  hash.AddU32(kPhase4OperationalWorkerOutputSchemaVersion);
  hash.AddByte(static_cast<std::uint8_t>(kind));
  hash.AddString(__VERSION__);
  hash.AddU64(payload_checksum);
  return hash.Finish();
}

[[nodiscard]] std::uint64_t WorkerSourceChecksum(std::string_view source_commit,
                                                 bool source_stamped, bool source_tree_dirty,
                                                 std::uint64_t artifact_checksum) noexcept {
  board_ir::StableHashBuilder hash;
  hash.AddString("APGAR-PHASE4-OPERATIONAL-WORKER-OUTPUT-SOURCE-V1");
  hash.AddU32(kPhase4OperationalWorkerOutputSchemaVersion);
  hash.AddString(source_commit);
  hash.AddBool(source_stamped);
  hash.AddBool(source_tree_dirty);
  hash.AddU64(artifact_checksum);
  return hash.Finish();
}

template <typename Payload, typename Writer>
[[nodiscard]] std::optional<std::string> SerializeWorker(
    Phase4OperationalWorkerOutputKind kind, const Payload& payload, std::uint64_t payload_checksum,
    std::string_view source_commit, bool source_stamped, bool source_tree_dirty,
    Writer write_payload) {
  const std::uint64_t artifact_checksum = WorkerArtifactChecksum(kind, payload_checksum);
  const std::uint64_t source_envelope_checksum =
      WorkerSourceChecksum(source_commit, source_stamped, source_tree_dirty, artifact_checksum);
  JsonWriter writer;
  writer.BeginObject();
  writer.Key("schema_version");
  writer.IntegerValue(kPhase4OperationalWorkerOutputSchemaVersion);
  writer.Key("kind");
  writer.EnumValue(kind);
  writer.Key("source_commit");
  writer.String(source_commit);
  writer.Key("source_stamped");
  writer.Bool(source_stamped);
  writer.Key("source_tree_dirty");
  writer.Bool(source_tree_dirty);
  writer.Key("compiler_identity");
  writer.String(__VERSION__);
  writer.Key("payload");
  write_payload(&writer, payload);
  writer.Key("artifact_checksum");
  writer.IntegerValue(artifact_checksum);
  writer.Key("source_envelope_checksum");
  writer.IntegerValue(source_envelope_checksum);
  writer.EndObject();
  return std::move(writer).Finish();
}

}  // namespace

std::optional<std::string> SerializePhase4OperationalProfileWorkerJsonV1(
    const Phase4TrialArmOperationalProfileV1& profile, std::string_view source_commit,
    bool source_stamped, bool source_tree_dirty) {
  if (!IsLowerHexCommit(source_commit) || profile.profile_checksum == 0 ||
      internal::ValidatePhase4TrialArmOperationalProfileV1(profile).has_value()) {
    return std::nullopt;
  }
  return SerializeWorker(Phase4OperationalWorkerOutputKind::kMeasuredProfile, profile,
                         profile.profile_checksum, source_commit, source_stamped, source_tree_dirty,
                         WriteOperationalProfile);
}

std::optional<std::string> SerializePhase4ReplayAuthorityWorkerJsonV1(
    const Phase4TrialArmReplayAuthorityV1& authority, std::string_view source_commit,
    bool source_stamped, bool source_tree_dirty) {
  if (!IsLowerHexCommit(source_commit) || authority.authority_checksum == 0 ||
      internal::ValidatePhase4TrialArmReplayAuthorityV1(authority).has_value()) {
    return std::nullopt;
  }
  return SerializeWorker(Phase4OperationalWorkerOutputKind::kUnmeasuredReplayAuthority, authority,
                         authority.authority_checksum, source_commit, source_stamped,
                         source_tree_dirty, WriteReplayAuthority);
}

}  // namespace apgar::benchmark
