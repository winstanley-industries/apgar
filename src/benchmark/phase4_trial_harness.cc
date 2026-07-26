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
      .raw_cell = std::nullopt,
  };
}

[[nodiscard]] bool FitsU64(Wide value) noexcept {
  return value <= std::numeric_limits<std::uint64_t>::max();
}

[[nodiscard]] std::uint64_t ToU64(Wide value) noexcept { return static_cast<std::uint64_t>(value); }

[[nodiscard]] std::uint64_t CanonicalRootSeed(Phase4RepresentativeCorpusAuthority authority,
                                              std::uint32_t case_id,
                                              std::uint32_t pool_size) noexcept {
  board_ir::StableHashBuilder hash;
  hash.AddString(authority == Phase4RepresentativeCorpusAuthority::kV2
                     ? "APGAR-PHASE4-CANONICAL-ROOT-V2"
                     : "APGAR-PHASE4-CANONICAL-ROOT-V1");
  hash.AddU64(Phase4RepresentativeCorpusChecksumForAuthority(authority));
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

[[nodiscard]] bool SetCaseIdentity(Phase4RepresentativeCorpusAuthority authority,
                                   Phase4DurableArmFailure* durable,
                                   const Phase4RepresentativeCase& case_state) noexcept {
  const Phase4CaseDescriptor* descriptor =
      FindPhase4CaseDescriptorForAuthority(authority, case_state.descriptor.case_id);
  if (descriptor == nullptr || !(*descriptor == case_state.descriptor)) {
    return false;
  }
  durable->has_case_identity = true;
  durable->case_id = case_state.descriptor.case_id;
  durable->descriptor_fingerprint =
      FingerprintPhase4CaseDescriptorForAuthority(authority, case_state.descriptor);
  durable->case_checksum = case_state.case_checksum;
  durable->board_content_hash = case_state.board.content_hash();
  durable->workload_checksum = case_state.workload.workload_checksum();
  durable->capacity_model_checksum = CapacityChecksum(case_state);
  return true;
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

[[nodiscard]] bool ValidCell(Phase4RepresentativeCorpusAuthority authority,
                             const Phase4CanonicalCellConfig& cell) noexcept {
  const Phase4CaseDescriptor* descriptor =
      FindPhase4CaseDescriptorForAuthority(authority, cell.case_id);
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
    Phase4TrialOrder execution_order) noexcept;

namespace {

enum class CanonicalNestedSessionAuthority : std::uint8_t {
  kCurrent = 0,
  kFrozenCorpusV1 = 1,
  kFrozenCorpusV2 = 2,
};

Phase4CanonicalSpecResult BuildPhase4CanonicalTrialSpecForAuthority(
    Phase4RepresentativeCorpusAuthority authority, const Phase4CanonicalCellConfig& cell,
    std::uint32_t repetition_index, Phase4TrialOrder execution_order,
    CanonicalNestedSessionAuthority session_authority) noexcept {
  try {
    if ((session_authority == CanonicalNestedSessionAuthority::kFrozenCorpusV1 &&
         authority != Phase4RepresentativeCorpusAuthority::kV1) ||
        (session_authority == CanonicalNestedSessionAuthority::kFrozenCorpusV2 &&
         authority != Phase4RepresentativeCorpusAuthority::kV2)) {
      return HarnessError("P4HARNESS-SPEC-001",
                          "frozen algorithm-budget preimages require their exact corpus authority");
    }
    if (!ValidCell(authority, cell) || repetition_index >= cell.repetitions ||
        (execution_order != Phase4TrialOrder::kBaselineFirst &&
         execution_order != Phase4TrialOrder::kCandidateFirst)) {
      return HarnessError("P4HARNESS-SPEC-001", "canonical cell or repetition is invalid");
    }
    const Phase4CaseDescriptor* descriptor =
        FindPhase4CaseDescriptorForAuthority(authority, cell.case_id);
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
    const std::uint64_t maximum_declared_pool = *std::max_element(
        descriptor->requested_pool_sizes.begin(),
        descriptor->requested_pool_sizes.begin() + descriptor->requested_pool_size_count);
    const std::uint64_t columns_per_epoch = net_count;
    const std::uint64_t terminal_rounds = pool_size + 1;
    const std::uint64_t sweeps = pool_size + epochs;
    const bool corpus_v2 = authority == Phase4RepresentativeCorpusAuthority::kV2;
    const std::uint64_t root_seed =
        CanonicalRootSeed(authority, cell.case_id, cell.requested_pool_size);
    const Wide route_queries = static_cast<Wide>(net_count) * sweeps;
    const Wide route_work = route_queries * kPhase4CanonicalRouteWorkUnitsPerQueryV1;
    const Wide retained_capacity = static_cast<Wide>(net_count) * (pool_size + epochs);
    const Wide initial_queries = static_cast<Wide>(net_count) * pool_size;
    const Wide regeneration_queries = static_cast<Wide>(epochs) * columns_per_epoch;
    const Wide reconstruction_states = static_cast<Wide>(maximum_declared_pool) *
                                       kPhase4CanonicalReconstructionStatesPerDeclaredPoolV1;
    const Wide selected_resource_bound = static_cast<Wide>(net_count) * reconstruction_states;
    const Wide baseline_policy_roster = 2U * selected_resource_bound + static_cast<Wide>(net_count);
    const Wide baseline_draft_bytes =
        kPhase4CanonicalCandidateDraftFixedBytesV1 +
        kPhase4CanonicalCandidateDraftBytesPerStateV1 * reconstruction_states +
        kPhase4CanonicalCandidateDraftBytesPerPolicyEntryV1 * baseline_policy_roster;
    const Wide baseline_retained_bytes = static_cast<Wide>(net_count) * baseline_draft_bytes;
    const Wide baseline_policy_projection_visits =
        static_cast<Wide>(sweeps) * 2U * net_count * baseline_policy_roster;
    const Wide baseline_policy_entries =
        static_cast<Wide>(sweeps) * net_count * baseline_policy_roster;
    const Wide baseline_expanded_visits = route_queries * reconstruction_states * 2U;
    const Wide baseline_admission_input =
        baseline_draft_bytes + kPhase4CanonicalAdmissionFixedPolicyBytesV1 +
        kPhase4CanonicalAdmissionBytesPerPolicyEntryV1 * baseline_policy_roster;
    const Wide preparation_draft_bytes =
        kPhase4CanonicalCandidateDraftFixedBytesV1 +
        kPhase4CanonicalCandidateDraftBytesPerStateV1 * reconstruction_states +
        kPhase4CanonicalCandidateDraftBytesPerPolicyEntryV1;
    const Wide initial_policy_entries = static_cast<Wide>(net_count) * (pool_size - 1U);
    const Wide preparation_generated_bytes = initial_queries * preparation_draft_bytes +
                                             static_cast<Wide>(net_count) * reconstruction_states *
                                                 kPhase4CanonicalPreparationBytesPerBaseResourceV1;
    constexpr std::uint64_t kCandidateBytesPerNet = 64ULL * 1024ULL * 1024ULL;
    const Wide preparation_retained_bytes = static_cast<Wide>(net_count) * kCandidateBytesPerNet;
    const Wide regeneration_policy_entries_per_candidate =
        selected_resource_bound + (corpus_v2 ? 1U : 0U);
    const Wide regeneration_draft_bytes =
        kPhase4CanonicalCandidateDraftFixedBytesV1 +
        kPhase4CanonicalCandidateDraftBytesPerStateV1 * reconstruction_states +
        kPhase4CanonicalCandidateDraftBytesPerPolicyEntryV1 *
            regeneration_policy_entries_per_candidate;
    const Wide regeneration_generated_bytes =
        static_cast<Wide>(columns_per_epoch) * regeneration_draft_bytes;
    const Wide regeneration_policy_projection_visits =
        static_cast<Wide>(allocator::kTargetedRegenerationPolicyProjectionPassesV2) * net_count *
        selected_resource_bound;
    const Wide regeneration_policy_entries_projection =
        static_cast<Wide>(net_count) * (selected_resource_bound + 1U);
    const bool compiled_work_bound_case =
        descriptor->case_id == 3'001 || descriptor->case_id == 3'002 ||
        descriptor->case_id == 13'001 || descriptor->case_id == 13'002;
    if (!compiled_work_bound_case &&
        regeneration_policy_entries_projection > kPhase4CanonicalMaximumAggregatePolicyEntriesV1) {
      return HarnessError("P4HARNESS-SPEC-003",
                          "successful canonical cell exceeds the public policy-entry bound");
    }
    const Wide regeneration_policy_entries = std::min<Wide>(
        regeneration_policy_entries_projection, kPhase4CanonicalMaximumAggregatePolicyEntriesV1);
    const Wide initial_admission_input =
        initial_queries * (preparation_draft_bytes + kPhase4CanonicalAdmissionFixedPolicyBytesV1) +
        kPhase4CanonicalAdmissionBytesPerPolicyEntryV1 * initial_policy_entries;
    const Wide regeneration_admission_input =
        static_cast<Wide>(columns_per_epoch) *
            (regeneration_draft_bytes + kPhase4CanonicalAdmissionFixedPolicyBytesV1) +
        kPhase4CanonicalAdmissionBytesPerPolicyEntryV1 * regeneration_policy_entries;
    const Wide maximum_admission_input =
        std::max(initial_admission_input, regeneration_admission_input);
    const Wide regeneration_rejection_bytes =
        static_cast<Wide>(columns_per_epoch) *
        allocator::kMaximumTargetedRegenerationRejectionLogicalBytesV2;
    const Wide regeneration_transient_bytes =
        static_cast<Wide>(columns_per_epoch) *
        (allocator::kTargetedRegenerationColumnBaseLogicalBytesV2 +
         2U * allocator::kMaximumTargetedRegenerationRejectionLogicalBytesV2);
    const Wide final_expanded_resource_uses = retained_capacity * reconstruction_states;
    if (!FitsU64(route_queries) || !FitsU64(route_work) || !FitsU64(retained_capacity) ||
        !FitsU64(initial_queries) || !FitsU64(regeneration_queries) ||
        !FitsU64(reconstruction_states) || !FitsU64(selected_resource_bound) ||
        !FitsU64(baseline_policy_roster) || !FitsU64(baseline_draft_bytes) ||
        !FitsU64(baseline_retained_bytes) || !FitsU64(baseline_policy_projection_visits) ||
        !FitsU64(baseline_policy_entries) || !FitsU64(baseline_expanded_visits) ||
        !FitsU64(baseline_admission_input) || !FitsU64(preparation_draft_bytes) ||
        !FitsU64(initial_policy_entries) || !FitsU64(preparation_generated_bytes) ||
        !FitsU64(preparation_retained_bytes) || !FitsU64(regeneration_draft_bytes) ||
        !FitsU64(regeneration_generated_bytes) || !FitsU64(regeneration_policy_projection_visits) ||
        !FitsU64(regeneration_policy_entries) || !FitsU64(maximum_admission_input) ||
        !FitsU64(regeneration_rejection_bytes) || !FitsU64(regeneration_transient_bytes) ||
        !FitsU64(final_expanded_resource_uses) ||
        !FitsU64(static_cast<Wide>(epochs) * regeneration_policy_projection_visits) ||
        !FitsU64(static_cast<Wide>(epochs) * regeneration_generated_bytes) ||
        !FitsU64(static_cast<Wide>(epochs) * regeneration_rejection_bytes) ||
        !FitsU64(static_cast<Wide>(epochs) * regeneration_transient_bytes) ||
        final_expanded_resource_uses > allocator::kMaximumAllocatorExpandedResourceUsesV1 ||
        retained_capacity > allocator::kMaximumAllocatorCandidatesV1) {
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
    spec.baseline_config.route_limits.maximum_reconstruction_states = ToU64(reconstruction_states);
    if (corpus_v2) {
      spec.baseline_config.price_config.present_step_per_overuse_unit = 1;
      spec.baseline_config.price_config.history_step_per_overuse_unit = 2'250;
    }
    spec.baseline_config.price_config.maximum_iterations =
        static_cast<std::uint32_t>(sweeps + terminal_rounds + epochs + 4);
    spec.baseline_config.price_config.maximum_price_records = ToU64(selected_resource_bound);
    spec.baseline_config.allocator_limits.maximum_nets = net_count;
    spec.baseline_config.allocator_limits.maximum_candidates = ToU64(retained_capacity);
    spec.baseline_config.allocator_limits.maximum_resource_records = ToU64(
        std::min<Wide>(baseline_policy_roster, allocator::kMaximumAllocatorResourceRecordsV1));
    spec.baseline_config.allocator_limits.maximum_expanded_resource_uses =
        ToU64(final_expanded_resource_uses);
    spec.baseline_config.limits.maximum_nets = net_count;
    spec.baseline_config.limits.maximum_route_queries = ToU64(route_queries);
    spec.baseline_config.limits.maximum_total_route_work_units = ToU64(route_work);
    spec.baseline_config.limits.maximum_policy_projection_visits =
        ToU64(baseline_policy_projection_visits);
    spec.baseline_config.limits.maximum_policy_resource_entries = ToU64(baseline_policy_entries);
    spec.baseline_config.limits.maximum_expanded_resource_visits = ToU64(baseline_expanded_visits);
    spec.baseline_config.limits.maximum_occupancy_resource_records = ToU64(selected_resource_bound);
    spec.baseline_config.limits.maximum_candidate_draft_bytes = ToU64(baseline_draft_bytes);
    spec.baseline_config.limits.maximum_retained_candidate_bytes = ToU64(baseline_retained_bytes);
    spec.baseline_config.admission_store_config.maximum_candidate_bytes_per_net =
        ToU64(baseline_draft_bytes);
    spec.baseline_config.admission_store_config.maximum_admission_input_bytes_per_transaction =
        ToU64(baseline_admission_input);

    spec.preparation_config.requested_candidates_per_net = cell.requested_pool_size;
    spec.preparation_config.deterministic_seed = root_seed;
    spec.preparation_config.route_limits = spec.baseline_config.route_limits;
    spec.preparation_config.limits.maximum_nets = net_count;
    spec.preparation_config.limits.maximum_route_queries = ToU64(initial_queries);
    spec.preparation_config.limits.maximum_total_route_work_units =
        ToU64(initial_queries * spec.baseline_config.route_limits.maximum_work_units);
    spec.preparation_config.limits.maximum_concurrent_reconstruction_states =
        spec.preparation_worker_count * ToU64(reconstruction_states);
    spec.preparation_config.limits.maximum_policy_resource_entries = ToU64(initial_policy_entries);
    spec.preparation_config.limits.maximum_retained_candidate_bytes =
        ToU64(preparation_retained_bytes);
    spec.preparation_config.limits.maximum_candidate_draft_bytes = ToU64(preparation_draft_bytes);
    spec.preparation_config.limits.maximum_generated_candidate_bytes =
        ToU64(preparation_generated_bytes);
    spec.preparation_config.store_config = candidates::CandidateStoreConfig{
        .maximum_candidates_per_net = pool_size + epochs,
        .maximum_candidate_bytes_per_net = kCandidateBytesPerNet,
        .maximum_rejection_records = ToU64(retained_capacity * 4 + net_count * 2),
        .maximum_rejection_items_per_transaction = ToU64(retained_capacity),
        .maximum_admission_items_per_transaction = ToU64(retained_capacity),
        .maximum_admission_input_bytes_per_transaction = ToU64(maximum_admission_input),
        .maximum_admission_work_units_per_transaction = 1'000'000'000'000ULL,
        .maximum_pin_lease_items_per_transaction = ToU64(retained_capacity),
        .maximum_expected_pools_per_invocation = net_count,
        .maximum_expected_candidates_per_invocation = ToU64(retained_capacity),
    };

    auto& session = spec.candidate_session_config;
    if (session_authority == CanonicalNestedSessionAuthority::kFrozenCorpusV1) {
      // This validation-only profile reconstructs the preserved V1 manifest
      // preimage. Executable diagnostic specs always retain current Session.
      session.schema_version = allocator::kCpuCandidateAllocationSessionSchemaVersionV3;
    } else if (session_authority == CanonicalNestedSessionAuthority::kFrozenCorpusV2) {
      // Representative Manifest v2 and its H=2250/H=4096 budget rosters bind
      // Session v4. Current Session v5 execution requires a separately
      // reviewed successor budget and consuming authority.
      session.schema_version = allocator::kCpuCandidateAllocationSessionSchemaVersionV4;
    }
    session.intrinsic_cost_weight = spec.baseline_config.intrinsic_cost_weight;
    session.maximum_regeneration_epochs = epochs;
    session.price_config = spec.baseline_config.price_config;
    session.allocator_limits = spec.baseline_config.allocator_limits;
    session.regeneration_plan_config.maximum_target_nets = net_count;
    session.regeneration_plan_config.maximum_columns_per_net = corpus_v2 ? 2 : 1;
    session.regeneration_plan_config.maximum_total_columns = columns_per_epoch;
    session.regeneration_plan_config.maximum_resource_actions_per_net = 16;
    session.regeneration_plan_config.maximum_total_resource_actions = net_count * 16;
    session.regeneration_execution_config.deterministic_seed = root_seed;
    session.regeneration_execution_config.maximum_route_queries = columns_per_epoch;
    session.regeneration_execution_config.route_limits = spec.baseline_config.route_limits;
    session.regeneration_execution_config.maximum_total_route_work_units =
        columns_per_epoch * spec.baseline_config.route_limits.maximum_work_units;
    session.regeneration_execution_config.maximum_policy_projection_visits =
        ToU64(regeneration_policy_projection_visits);
    session.regeneration_execution_config.maximum_policy_resource_entries =
        ToU64(regeneration_policy_entries);
    session.regeneration_execution_config.maximum_candidate_draft_bytes =
        ToU64(regeneration_draft_bytes);
    session.regeneration_execution_config.maximum_generated_candidate_bytes =
        ToU64(regeneration_generated_bytes);
    session.regeneration_execution_config.maximum_rejection_bytes =
        ToU64(regeneration_rejection_bytes);
    session.regeneration_execution_config.maximum_transient_result_bytes =
        ToU64(regeneration_transient_bytes);
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
    session.limits.maximum_total_planning_expanded_resource_visits =
        epochs * session.regeneration_plan_config.maximum_expanded_resource_visits;
    session.limits.maximum_total_policy_projection_visits =
        ToU64(static_cast<Wide>(epochs) * regeneration_policy_projection_visits);
    session.limits.maximum_total_generated_candidate_bytes =
        ToU64(static_cast<Wide>(epochs) * regeneration_generated_bytes);
    session.limits.maximum_total_rejection_bytes =
        ToU64(static_cast<Wide>(epochs) * regeneration_rejection_bytes);
    session.limits.maximum_total_transient_result_bytes =
        ToU64(static_cast<Wide>(epochs) * regeneration_transient_bytes);
    return spec;
  } catch (const std::bad_alloc&) {
    return HarnessError("P4HARNESS-SPEC-004", "host allocation failed building canonical spec");
  } catch (const std::exception&) {
    return HarnessError("P4HARNESS-SPEC-005", "unexpected exception building canonical spec");
  }
}

}  // namespace

Phase4CanonicalSpecResult BuildPhase4CanonicalTrialSpecV1(
    const Phase4CanonicalCellConfig& cell, std::uint32_t repetition_index,
    Phase4TrialOrder execution_order) noexcept {
  return BuildPhase4CanonicalTrialSpecForAuthority(Phase4RepresentativeCorpusAuthority::kV1, cell,
                                                   repetition_index, execution_order,
                                                   CanonicalNestedSessionAuthority::kCurrent);
}

Phase4CanonicalSpecResult BuildPhase4FrozenCanonicalBudgetPreimageV1(
    const Phase4CanonicalCellConfig& cell, std::uint32_t repetition_index,
    Phase4TrialOrder execution_order) noexcept {
  return BuildPhase4CanonicalTrialSpecForAuthority(
      Phase4RepresentativeCorpusAuthority::kV1, cell, repetition_index, execution_order,
      CanonicalNestedSessionAuthority::kFrozenCorpusV1);
}

Phase4CanonicalSpecResult BuildPhase4CanonicalTrialSpecForCorpusV2(
    const Phase4CanonicalCellConfig& cell, std::uint32_t repetition_index,
    Phase4TrialOrder execution_order) noexcept {
  return BuildPhase4CanonicalTrialSpecForAuthority(
      Phase4RepresentativeCorpusAuthority::kV2, cell, repetition_index, execution_order,
      CanonicalNestedSessionAuthority::kFrozenCorpusV2);
}

Phase4CanonicalSpecResult BuildPhase4FrozenCanonicalBudgetPreimageForCorpusV2(
    const Phase4CanonicalCellConfig& cell, std::uint32_t repetition_index,
    Phase4TrialOrder execution_order) noexcept {
  return BuildPhase4CanonicalTrialSpecForAuthority(
      Phase4RepresentativeCorpusAuthority::kV2, cell, repetition_index, execution_order,
      CanonicalNestedSessionAuthority::kFrozenCorpusV2);
}

[[nodiscard]] Phase4ArmFailureReconciliationResultV1 TryReconcilePhase4TrialArmFailureForAuthority(
    Phase4RepresentativeCorpusAuthority authority, Phase4TrialArmFailure failure) {
  const auto reject = [&failure]() -> Phase4ArmFailureReconciliationResultV1 {
    return Phase4ArmFailureReconciliationRejectionV1{
        .invariant_id = "P4HARNESS-FAILURE-CORPUS-001",
        .detail =
            "the typed failure case is absent from or differs from the selected corpus authority",
        .failure = std::move(failure),
    };
  };
  Phase4DurableArmFailure durable;
  durable.summary_code = failure.summary.code;
  durable.arm = failure.summary.arm;
  durable.summary_required = failure.summary.required;
  durable.summary_configured = failure.summary.configured;
  durable.summary_invariant_id = std::string(failure.summary.invariant_id);
  durable.summary_detail = std::string(failure.summary.detail);
  if (std::holds_alternative<Phase4RepresentativeCorpusError>(failure.payload)) {
    const auto& child = std::get<Phase4RepresentativeCorpusError>(failure.payload);
    if (FindPhase4CaseDescriptorForAuthority(authority, child.requested_case_id) == nullptr) {
      return reject();
    }
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
    if (!SetCaseIdentity(authority, &durable, state.case_state)) {
      return reject();
    }
    durable.payload_kind = Phase4DurableFailurePayloadKind::kSequential;
    SetChildError(&durable, static_cast<std::uint8_t>(state.error.code), state.error.invariant_id,
                  state.error.detail, state.error.net, state.error.required,
                  state.error.configured);
  } else if (std::holds_alternative<Phase4CandidatePreparationFailureState>(failure.payload)) {
    const auto& state = std::get<Phase4CandidatePreparationFailureState>(failure.payload);
    if (!SetCaseIdentity(authority, &durable, state.case_state)) {
      return reject();
    }
    durable.payload_kind = Phase4DurableFailurePayloadKind::kCandidatePreparation;
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
    if (!SetCaseIdentity(authority, &durable, state.case_state)) {
      return reject();
    }
    durable.payload_kind = Phase4DurableFailurePayloadKind::kCandidateSession;
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

Phase4DurableArmFailure ReconcilePhase4TrialArmFailureV1(Phase4TrialArmFailure failure) {
  Phase4ArmFailureReconciliationResultV1 result = TryReconcilePhase4TrialArmFailureForAuthority(
      Phase4RepresentativeCorpusAuthority::kV1, std::move(failure));
  if (std::holds_alternative<Phase4DurableArmFailure>(result)) {
    return std::get<Phase4DurableArmFailure>(std::move(result));
  }
  const auto& rejection = std::get<Phase4ArmFailureReconciliationRejectionV1>(result);
  Phase4DurableArmFailure durable;
  durable.summary_code = Phase4PairedTrialErrorCode::kCaseIdentityMismatch;
  durable.arm = rejection.failure.summary.arm;
  durable.summary_invariant_id = rejection.invariant_id;
  durable.summary_detail = rejection.detail;
  durable.payload_checksum = ComputePhase4DurableArmFailureChecksumV1(durable);
  return durable;
}

Phase4DurableArmFailure ReconcilePhase4TrialArmFailureForCorpusV2(Phase4TrialArmFailure failure) {
  Phase4ArmFailureReconciliationResultV1 result = TryReconcilePhase4TrialArmFailureForAuthority(
      Phase4RepresentativeCorpusAuthority::kV2, std::move(failure));
  if (std::holds_alternative<Phase4DurableArmFailure>(result)) {
    return std::get<Phase4DurableArmFailure>(std::move(result));
  }
  const auto& rejection = std::get<Phase4ArmFailureReconciliationRejectionV1>(result);
  Phase4DurableArmFailure durable;
  durable.summary_code = Phase4PairedTrialErrorCode::kCaseIdentityMismatch;
  durable.arm = rejection.failure.summary.arm;
  durable.summary_invariant_id = rejection.invariant_id;
  durable.summary_detail = rejection.detail;
  durable.payload_checksum = ComputePhase4DurableArmFailureChecksumV1(durable);
  return durable;
}

Phase4ArmFailureReconciliationResultV1 TryReconcilePhase4TrialArmFailureV1(
    Phase4TrialArmFailure failure) {
  return TryReconcilePhase4TrialArmFailureForAuthority(Phase4RepresentativeCorpusAuthority::kV1,
                                                       std::move(failure));
}

Phase4ArmFailureReconciliationResultV1 TryReconcilePhase4TrialArmFailureForCorpusV2(
    Phase4TrialArmFailure failure) {
  return TryReconcilePhase4TrialArmFailureForAuthority(Phase4RepresentativeCorpusAuthority::kV2,
                                                       std::move(failure));
}

}  // namespace apgar::benchmark
