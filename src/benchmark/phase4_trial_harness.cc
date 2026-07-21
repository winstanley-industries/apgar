#include "apgar/benchmark/phase4_trial_harness.h"

#include <fcntl.h>
#include <poll.h>
#include <sched.h>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <exception>
#include <fstream>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

#include "apgar/board_ir/stable_hash.h"
#include "src/allocator/negotiated_prices_internal.h"
#include "src/benchmark/phase4_paired_trial_internal.h"
#include "src/benchmark/phase4_trial_wire_internal.h"

namespace apgar::benchmark {
namespace {

using Wide = unsigned __int128;

[[nodiscard]] Phase4TrialHarnessError HarnessError(std::string_view invariant,
                                                   std::string_view detail) {
  return Phase4TrialHarnessError{
      .invariant_id = std::string(invariant),
      .detail = std::string(detail),
  };
}

[[nodiscard]] bool FitsU64(Wide value) noexcept {
  return value <= std::numeric_limits<std::uint64_t>::max();
}

[[nodiscard]] std::uint64_t ToU64(Wide value) noexcept { return static_cast<std::uint64_t>(value); }

[[nodiscard]] std::uint64_t CanonicalRootSeed(std::uint32_t case_id,
                                              std::uint32_t pool_size) noexcept {
  board_ir::StableHashBuilder hash;
  hash.AddString("APGAR-PHASE4-CANONICAL-ROOT-V1");
  hash.AddU64(Phase4RepresentativeCorpusChecksumV1());
  hash.AddU32(case_id);
  hash.AddU32(pool_size);
  const std::uint64_t seed = hash.Finish();
  return seed == 0 ? 1 : seed;
}

[[nodiscard]] std::uint64_t CapacityChecksum(const Phase4RepresentativeCase& case_state) noexcept {
  return allocator::internal::ComputeResourceCapacityModelChecksumV1(
      allocator::internal::ResourceCapacityChecksumHeaderV1{
          .schema_version = case_state.capacities.schema_version(),
          .associations = case_state.capacities.associations(),
          .default_capacity_units = case_state.capacities.default_capacity_units(),
      },
      case_state.capacities.overrides());
}

void AddOptionalU64(board_ir::StableHashBuilder& hash,
                    const std::optional<std::uint64_t>& value) noexcept {
  hash.AddBool(value.has_value());
  if (value.has_value()) {
    hash.AddU64(*value);
  }
}

void AddOptionalEntity(board_ir::StableHashBuilder& hash,
                       const std::optional<board_ir::EntityRef>& value) noexcept {
  hash.AddBool(value.has_value());
  if (value.has_value()) {
    hash.AddU64(value->id);
    hash.AddU32(value->generation);
  }
}

[[nodiscard]] std::uint64_t CandidateStoreReconciliationChecksum(
    const candidates::CandidateStore& store, const allocator::MultiNetWorkload& workload,
    std::uint64_t* candidate_count, std::uint64_t* rejection_count) {
  board_ir::StableHashBuilder hash;
  hash.AddString("APGAR-PHASE4-RECONCILED-CANDIDATE-STORE-V1");
  hash.AddU64(workload.workload_checksum());
  hash.AddU64(workload.nets().size());
  Wide candidates_total = 0;
  for (const allocator::PreparedNetRoutingContext& net : workload.nets()) {
    const board_ir::EntityRef net_ref = net.request.net;
    hash.AddU64(net_ref.id);
    hash.AddU32(net_ref.generation);
    const std::vector<candidates::StoredCandidate> candidates = store.Enumerate(net_ref);
    hash.AddU64(candidates.size());
    candidates_total += candidates.size();
    for (const candidates::StoredCandidate& candidate : candidates) {
      hash.AddBool(candidate != nullptr);
      if (candidate != nullptr) {
        hash.AddU64(candidate->id().high);
        hash.AddU64(candidate->id().low);
        hash.AddU64(candidate->data().payload_checksum);
        hash.AddU64(candidate->logical_bytes());
      }
    }
  }
  const std::vector<candidates::CandidateRejection> rejections = store.Rejections();
  hash.AddU64(rejections.size());
  for (const candidates::CandidateRejection& rejection : rejections) {
    hash.AddU32(rejection.schema_version);
    hash.AddBool(rejection.candidate_id.has_value());
    if (rejection.candidate_id.has_value()) {
      hash.AddU64(rejection.candidate_id->high);
      hash.AddU64(rejection.candidate_id->low);
    }
    AddOptionalEntity(hash, rejection.net);
    hash.AddByte(static_cast<std::uint8_t>(rejection.stage));
    hash.AddByte(static_cast<std::uint8_t>(rejection.code));
    hash.AddString(rejection.invariant_id);
    hash.AddU64(rejection.associations.board_content_hash);
    hash.AddU64(rejection.associations.compiler_profile_fingerprint);
    hash.AddU32(rejection.associations.geometry_compiler_version);
    hash.AddU64(rejection.associations.routing_profile_fingerprint);
    hash.AddU64(rejection.associations.rule_bucket_identity);
    hash.AddU64(rejection.policy_identity);
    hash.AddByte(static_cast<std::uint8_t>(rejection.provenance.generator));
    hash.AddU32(rejection.provenance.generator_version);
    hash.AddByte(static_cast<std::uint8_t>(rejection.provenance.backend));
    hash.AddString(rejection.provenance.supported_device_class);
    hash.AddU64(rejection.provenance.deterministic_seed);
    hash.AddU64(rejection.provenance.batch_identity);
    hash.AddU64(rejection.provenance.query_identity);
    hash.AddU32(rejection.provenance.candidate_ordinal);
    AddOptionalU64(hash, rejection.primitive_witness_index);
    AddOptionalU64(hash, rejection.resource_witness_index);
    AddOptionalU64(hash, rejection.expected_value);
    AddOptionalU64(hash, rejection.actual_value);
    AddOptionalEntity(hash, rejection.conflicting_entity);
    AddOptionalU64(hash, rejection.candidate_payload_checksum);
    hash.AddString(rejection.detail);
    hash.AddU64(rejection.logical_bytes);
  }
  *candidate_count = FitsU64(candidates_total) ? ToU64(candidates_total)
                                               : std::numeric_limits<std::uint64_t>::max();
  *rejection_count = rejections.size();
  return hash.Finish();
}

void SetCaseIdentity(Phase4DurableArmFailure* durable,
                     const Phase4RepresentativeCase& case_state) noexcept {
  durable->has_case_identity = true;
  durable->case_id = case_state.descriptor.case_id;
  durable->descriptor_fingerprint = FingerprintPhase4CaseDescriptorV1(case_state.descriptor);
  durable->case_checksum = case_state.case_checksum;
  durable->board_content_hash = case_state.board.content_hash();
  durable->workload_checksum = case_state.workload.workload_checksum();
  durable->capacity_model_checksum = CapacityChecksum(case_state);
}

void SetChildError(Phase4DurableArmFailure* durable, std::uint8_t code, std::string_view invariant,
                   std::string_view detail, const std::optional<board_ir::EntityRef>& net,
                   std::uint64_t required, std::uint64_t configured) {
  durable->child_error_code = code;
  durable->child_invariant_id = std::string(invariant);
  durable->child_detail = std::string(detail);
  durable->has_child_net = net.has_value();
  if (net.has_value()) {
    durable->child_net_id = net->id;
    durable->child_net_generation = net->generation;
  }
  durable->child_required = required;
  durable->child_configured = configured;
}

[[nodiscard]] std::uint64_t DurableFailureChecksum(const Phase4DurableArmFailure& value) noexcept {
  board_ir::StableHashBuilder hash;
  hash.AddString("APGAR-PHASE4-DURABLE-ARM-FAILURE-V1");
  hash.AddU32(value.schema_version);
  hash.AddByte(static_cast<std::uint8_t>(value.summary_code));
  hash.AddByte(static_cast<std::uint8_t>(value.arm));
  hash.AddU64(value.summary_required);
  hash.AddU64(value.summary_configured);
  hash.AddString(value.summary_invariant_id);
  hash.AddString(value.summary_detail);
  hash.AddByte(static_cast<std::uint8_t>(value.payload_kind));
  hash.AddByte(value.child_error_code);
  hash.AddString(value.child_invariant_id);
  hash.AddString(value.child_detail);
  hash.AddBool(value.has_child_net);
  hash.AddU64(value.child_net_id);
  hash.AddU32(value.child_net_generation);
  hash.AddU64(value.child_required);
  hash.AddU64(value.child_configured);
  hash.AddByte(value.child_bound_kind);
  hash.AddU64(value.child_secondary_required);
  hash.AddU64(value.child_secondary_configured);
  hash.AddBool(value.has_case_identity);
  hash.AddU32(value.case_id);
  hash.AddU64(value.descriptor_fingerprint);
  hash.AddU64(value.case_checksum);
  hash.AddU64(value.board_content_hash);
  hash.AddU64(value.workload_checksum);
  hash.AddU64(value.capacity_model_checksum);
  hash.AddBool(value.has_epoch_index);
  hash.AddU32(value.epoch_index);
  hash.AddBool(value.has_failed_observation);
  hash.AddU64(value.failed_observation_checksum);
  hash.AddU64(value.attempted_column_count);
  hash.AddU64(value.attempted_route_queries);
  hash.AddU64(value.attempted_route_work_units);
  hash.AddBool(value.candidate_store_publication_committed);
  hash.AddBool(value.authoritative_candidate_store_present);
  hash.AddU64(value.reconciled_candidate_count);
  hash.AddU64(value.reconciled_rejection_count);
  hash.AddU64(value.reconciled_candidate_store_checksum);
  return hash.Finish();
}

[[nodiscard]] bool ValidCell(const Phase4CanonicalCellConfig& cell) noexcept {
  const Phase4CaseDescriptor* descriptor = FindPhase4CaseDescriptorV1(cell.case_id);
  const std::uint64_t address_space_limit = cell.external_budget.maximum_address_space_bytes;
  const bool address_space_representable =
      address_space_limit <= static_cast<std::uint64_t>(std::numeric_limits<rlim_t>::max()) &&
      static_cast<rlim_t>(address_space_limit) != RLIM_INFINITY;
  if (cell.schema_version != kPhase4TrialHarnessSchemaVersion || descriptor == nullptr ||
      cell.preparation_worker_count == 0 ||
      cell.preparation_worker_count > allocator::kMaximumPersistentCpuCandidateWorkersV1 ||
      cell.repetitions == 0 || cell.repetitions > kPhase4CanonicalRepetitionsV1 ||
      cell.maximum_setup_elapsed_nanoseconds == 0 ||
      cell.maximum_setup_elapsed_nanoseconds > kPhase4MaximumWatchdogNanosecondsV1 ||
      cell.external_budget.maximum_prepared_elapsed_nanoseconds == 0 ||
      cell.external_budget.maximum_cold_elapsed_nanoseconds == 0 ||
      cell.external_budget.maximum_cold_elapsed_nanoseconds > kPhase4MaximumWatchdogNanosecondsV1 ||
      cell.external_budget.maximum_prepared_elapsed_nanoseconds >
          cell.external_budget.maximum_cold_elapsed_nanoseconds ||
      address_space_limit == 0 || !address_space_representable ||
      cell.external_budget.maximum_peak_host_bytes == 0 || cell.corpus_limits.maximum_nets == 0 ||
      cell.corpus_limits.maximum_nets > kMaximumPhase4RepresentativeNetsV1 ||
      cell.corpus_limits.maximum_compiled_nodes == 0 ||
      cell.corpus_limits.maximum_compiled_nodes >
          allocator::kMaximumMultiNetWorkloadCompiledNodesV1 ||
      cell.corpus_limits.maximum_compiled_host_bytes == 0 ||
      cell.corpus_limits.maximum_compiled_host_bytes >
          allocator::kMaximumMultiNetWorkloadCompiledHostBytesV1 ||
      cell.corpus_limits.maximum_active_regions == 0 ||
      cell.corpus_limits.maximum_active_regions > kMaximumPhase4ActiveRegionsV1 ||
      cell.corpus_limits.maximum_board_entities == 0 ||
      cell.corpus_limits.maximum_board_entities > kMaximumPhase4BoardEntitiesV1) {
    return false;
  }
  return cell.requested_pool_size == 4 || cell.requested_pool_size == 8 ||
         cell.requested_pool_size == 16;
}

}  // namespace

std::uint64_t ComputePhase4DurableArmFailureChecksumV1(
    const Phase4DurableArmFailure& failure) noexcept {
  return DurableFailureChecksum(failure);
}

Phase4CanonicalSpecResult BuildPhase4CanonicalTrialSpecV1(
    const Phase4CanonicalCellConfig& cell, std::uint32_t repetition_index,
    Phase4TrialOrder execution_order) noexcept {
  try {
    if (!ValidCell(cell) || repetition_index >= cell.repetitions ||
        (execution_order != Phase4TrialOrder::kBaselineFirst &&
         execution_order != Phase4TrialOrder::kCandidateFirst)) {
      return HarnessError("P4HARNESS-SPEC-001", "canonical cell or repetition is invalid");
    }
    const Phase4CaseDescriptor* descriptor = FindPhase4CaseDescriptorV1(cell.case_id);
    if (descriptor == nullptr ||
        std::find(descriptor->requested_pool_sizes.begin(),
                  descriptor->requested_pool_sizes.begin() + descriptor->requested_pool_size_count,
                  cell.requested_pool_size) ==
            descriptor->requested_pool_sizes.begin() + descriptor->requested_pool_size_count) {
      return HarnessError("P4HARNESS-SPEC-002",
                          "case descriptor does not declare the requested decision pool");
    }
    const std::uint64_t net_count = descriptor->requested_net_count;
    const std::uint64_t pool_size = cell.requested_pool_size;
    constexpr std::uint64_t epochs = 2;
    const std::uint64_t columns_per_epoch = net_count;
    const std::uint64_t terminal_rounds = pool_size + 1;
    const std::uint64_t sweeps = pool_size + epochs;
    const std::uint64_t root_seed = CanonicalRootSeed(cell.case_id, cell.requested_pool_size);
    const Wide route_queries = static_cast<Wide>(net_count) * sweeps;
    const Wide route_work = route_queries * kPhase4CanonicalRouteWorkUnitsPerQueryV1;
    const Wide retained_capacity = static_cast<Wide>(net_count) * (pool_size + epochs);
    const Wide initial_queries = static_cast<Wide>(net_count) * pool_size;
    const Wide regeneration_queries = static_cast<Wide>(epochs) * columns_per_epoch;
    if (!FitsU64(route_queries) || !FitsU64(route_work) || !FitsU64(retained_capacity) ||
        !FitsU64(initial_queries) || !FitsU64(regeneration_queries)) {
      return HarnessError("P4HARNESS-SPEC-003", "canonical spec arithmetic exceeds uint64");
    }

    Phase4PairedTrialSpec spec;
    spec.case_id = cell.case_id;
    spec.requested_pool_size = cell.requested_pool_size;
    spec.repetition_index = repetition_index;
    spec.root_seed = root_seed;
    spec.execution_order = execution_order;
    spec.preparation_worker_count = cell.preparation_worker_count;
    spec.corpus_limits = cell.corpus_limits;
    spec.external_budget = cell.external_budget;

    spec.baseline_config.deterministic_seed = root_seed;
    spec.baseline_config.maximum_sweeps = static_cast<std::uint32_t>(sweeps);
    spec.baseline_config.route_limits.maximum_work_units = kPhase4CanonicalRouteWorkUnitsPerQueryV1;
    spec.baseline_config.price_config.maximum_iterations =
        static_cast<std::uint32_t>(sweeps + terminal_rounds + epochs + 4);
    spec.baseline_config.limits.maximum_nets = net_count;
    spec.baseline_config.limits.maximum_route_queries = ToU64(route_queries);
    spec.baseline_config.limits.maximum_total_route_work_units = ToU64(route_work);

    spec.preparation_config.requested_candidates_per_net = cell.requested_pool_size;
    spec.preparation_config.deterministic_seed = root_seed;
    spec.preparation_config.route_limits = spec.baseline_config.route_limits;
    spec.preparation_config.limits.maximum_nets = net_count;
    spec.preparation_config.limits.maximum_route_queries = ToU64(initial_queries);
    spec.preparation_config.limits.maximum_total_route_work_units =
        ToU64(initial_queries * spec.baseline_config.route_limits.maximum_work_units);
    spec.preparation_config.limits.maximum_generated_candidate_bytes =
        64ULL * 1024ULL * 1024ULL * 1024ULL;
    spec.preparation_config.store_config = candidates::CandidateStoreConfig{
        .maximum_candidates_per_net = pool_size + epochs,
        .maximum_candidate_bytes_per_net = 64ULL * 1024ULL * 1024ULL,
        .maximum_rejection_records = ToU64(retained_capacity * 4 + net_count * 2),
        .maximum_rejection_items_per_transaction = ToU64(retained_capacity),
        .maximum_admission_items_per_transaction = ToU64(retained_capacity),
        .maximum_admission_input_bytes_per_transaction = 8ULL * 1024ULL * 1024ULL * 1024ULL,
        .maximum_admission_work_units_per_transaction = 1'000'000'000'000ULL,
        .maximum_pin_lease_items_per_transaction = ToU64(retained_capacity),
        .maximum_expected_pools_per_invocation = net_count,
        .maximum_expected_candidates_per_invocation = ToU64(retained_capacity),
    };

    auto& session = spec.candidate_session_config;
    session.intrinsic_cost_weight = spec.baseline_config.intrinsic_cost_weight;
    session.maximum_regeneration_epochs = epochs;
    session.price_config = spec.baseline_config.price_config;
    session.allocator_limits = spec.baseline_config.allocator_limits;
    session.regeneration_plan_config.maximum_target_nets = net_count;
    session.regeneration_plan_config.maximum_columns_per_net = 1;
    session.regeneration_plan_config.maximum_total_columns = columns_per_epoch;
    session.regeneration_plan_config.maximum_resource_actions_per_net = 16;
    session.regeneration_plan_config.maximum_total_resource_actions = net_count * 16;
    session.regeneration_execution_config.deterministic_seed = root_seed;
    session.regeneration_execution_config.maximum_route_queries = columns_per_epoch;
    session.regeneration_execution_config.route_limits = spec.baseline_config.route_limits;
    session.regeneration_execution_config.maximum_total_route_work_units =
        columns_per_epoch * spec.baseline_config.route_limits.maximum_work_units;
    session.schedules = {
        allocator::MultiWorldSchedule{
            .schedule_key = 1,
            .search_intrinsic_cost_weight = session.intrinsic_cost_weight,
            .maximum_selection_rounds = 1,
        },
        allocator::MultiWorldSchedule{
            .schedule_key = 2,
            .search_intrinsic_cost_weight = session.intrinsic_cost_weight,
            .maximum_selection_rounds = static_cast<std::uint32_t>(terminal_rounds),
        },
    };
    session.multi_world_config.maximum_worlds = 2;
    session.multi_world_config.maximum_total_selection_rounds = terminal_rounds + 1;
    session.multi_world_config.maximum_retained_worlds = 2;
    session.limits.maximum_epoch_records = epochs;
    session.limits.maximum_total_requested_columns = ToU64(regeneration_queries);
    session.limits.maximum_total_route_queries = ToU64(regeneration_queries);
    session.limits.maximum_total_route_work_units =
        ToU64(regeneration_queries * spec.baseline_config.route_limits.maximum_work_units);
    return spec;
  } catch (const std::bad_alloc&) {
    return HarnessError("P4HARNESS-SPEC-004", "host allocation failed building canonical spec");
  } catch (const std::exception&) {
    return HarnessError("P4HARNESS-SPEC-005", "unexpected exception building canonical spec");
  }
}

Phase4DurableArmFailure ReconcilePhase4TrialArmFailureV1(Phase4TrialArmFailure failure) {
  Phase4DurableArmFailure durable;
  durable.summary_code = failure.summary.code;
  durable.arm = failure.summary.arm;
  durable.summary_required = failure.summary.required;
  durable.summary_configured = failure.summary.configured;
  durable.summary_invariant_id = std::string(failure.summary.invariant_id);
  durable.summary_detail = std::string(failure.summary.detail);
  if (std::holds_alternative<Phase4RepresentativeCorpusError>(failure.payload)) {
    const auto& child = std::get<Phase4RepresentativeCorpusError>(failure.payload);
    durable.payload_kind = Phase4DurableFailurePayloadKind::kCorpus;
    SetChildError(&durable, static_cast<std::uint8_t>(child.code), child.invariant_id, child.detail,
                  child.first_unpreparable_net, child.required_compiled_nodes,
                  child.configured_compiled_node_limit);
    durable.case_id = child.requested_case_id;
    durable.child_bound_kind = static_cast<std::uint8_t>(child.limiting_work_bound);
    durable.child_secondary_required = child.required_compiled_host_bytes;
    durable.child_secondary_configured = child.configured_compiled_host_byte_limit;
    durable.attempted_column_count = child.maximum_preparable_net_count;
  } else if (std::holds_alternative<Phase4SequentialFailureState>(failure.payload)) {
    const auto& state = std::get<Phase4SequentialFailureState>(failure.payload);
    durable.payload_kind = Phase4DurableFailurePayloadKind::kSequential;
    SetCaseIdentity(&durable, state.case_state);
    SetChildError(&durable, static_cast<std::uint8_t>(state.error.code), state.error.invariant_id,
                  state.error.detail, state.error.net, state.error.required,
                  state.error.configured);
  } else if (std::holds_alternative<Phase4CandidatePreparationFailureState>(failure.payload)) {
    const auto& state = std::get<Phase4CandidatePreparationFailureState>(failure.payload);
    durable.payload_kind = Phase4DurableFailurePayloadKind::kCandidatePreparation;
    SetCaseIdentity(&durable, state.case_state);
    SetChildError(&durable, static_cast<std::uint8_t>(state.error.code), state.error.invariant_id,
                  state.error.detail, state.error.net, state.error.required,
                  state.error.configured);
    if (state.error.failed_preparation.has_value()) {
      const auto& observation = *state.error.failed_preparation;
      durable.has_failed_observation = true;
      durable.failed_observation_checksum = observation.observation_checksum;
      durable.attempted_column_count = observation.attempted_columns.size();
      durable.attempted_route_queries = observation.counters.route_queries;
      durable.attempted_route_work_units = observation.counters.route_work_units;
      durable.candidate_store_publication_committed =
          observation.candidate_store_publication_committed;
      durable.authoritative_candidate_store_present =
          observation.authoritative_candidate_store != nullptr;
      if (observation.authoritative_candidate_store != nullptr) {
        durable.reconciled_candidate_store_checksum = CandidateStoreReconciliationChecksum(
            *observation.authoritative_candidate_store, state.case_state.workload,
            &durable.reconciled_candidate_count, &durable.reconciled_rejection_count);
      }
    }
  } else if (std::holds_alternative<Phase4CandidateSessionFailureState>(failure.payload)) {
    const auto& state = std::get<Phase4CandidateSessionFailureState>(failure.payload);
    durable.payload_kind = Phase4DurableFailurePayloadKind::kCandidateSession;
    SetCaseIdentity(&durable, state.case_state);
    durable.child_error_code = static_cast<std::uint8_t>(state.error.code);
    durable.child_invariant_id = std::string(state.error.invariant_id);
    durable.child_detail = std::string(state.error.detail);
    durable.has_epoch_index = state.error.epoch_index.has_value();
    durable.epoch_index = state.error.epoch_index.value_or(0);
    durable.candidate_store_publication_committed =
        state.error.candidate_store_publication_committed;
    durable.authoritative_candidate_store_present = state.prepared.has_candidate_store();
    if (state.error.failed_regeneration.has_value()) {
      const auto& observation = *state.error.failed_regeneration;
      durable.has_failed_observation = true;
      durable.failed_observation_checksum = observation.observation_checksum;
      durable.attempted_column_count = observation.columns.size();
      durable.attempted_route_queries = observation.counters.route_queries;
      durable.attempted_route_work_units = observation.counters.route_work_units;
      durable.candidate_store_publication_committed =
          durable.candidate_store_publication_committed ||
          observation.candidate_store_publication_committed;
    }
    if (state.prepared.has_candidate_store()) {
      durable.reconciled_candidate_store_checksum = CandidateStoreReconciliationChecksum(
          state.prepared.candidate_store(), state.case_state.workload,
          &durable.reconciled_candidate_count, &durable.reconciled_rejection_count);
    }
  }
  durable.payload_checksum = ComputePhase4DurableArmFailureChecksumV1(durable);
  return durable;
}

}  // namespace apgar::benchmark
