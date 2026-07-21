#include "apgar/benchmark/phase4_paired_trial.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <new>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

#include "apgar/board_ir/stable_hash.h"
#include "apgar/candidates/candidate_store.h"
#include "src/benchmark/phase4_paired_trial_internal.h"

namespace apgar::benchmark {
namespace {

using Wide = unsigned __int128;
using Clock = std::chrono::steady_clock;

struct ValidatedTrialSpec {
  const Phase4CaseDescriptor* descriptor = nullptr;
  Phase4RouteOpportunity opportunity;
  std::uint64_t candidate_columns_per_epoch = 0;
  std::uint32_t candidate_terminal_selection_rounds = 0;
  std::uint64_t budget_checksum = 0;
};

using ValidationResult = std::variant<ValidatedTrialSpec, Phase4PairedTrialError>;

[[nodiscard]] Phase4PairedTrialError Error(Phase4PairedTrialErrorCode code,
                                           std::string_view invariant_id, std::string_view detail,
                                           Phase4TrialArm arm = Phase4TrialArm::kSequentialBaseline,
                                           std::uint64_t required = 0,
                                           std::uint64_t configured = 0) noexcept {
  return Phase4PairedTrialError{
      .code = code,
      .invariant_id = invariant_id,
      .detail = detail,
      .arm = arm,
      .required = required,
      .configured = configured,
  };
}

[[nodiscard]] Phase4TrialArmFailure ArmFailure(Phase4PairedTrialError summary) {
  return Phase4TrialArmFailure{
      .summary = summary,
      .payload = std::monostate{},
  };
}

template <typename Payload>
[[nodiscard]] Phase4TrialArmFailure ArmFailure(Phase4PairedTrialError summary, Payload&& payload) {
  return Phase4TrialArmFailure{
      .summary = summary,
      .payload = std::forward<Payload>(payload),
  };
}

[[nodiscard]] bool FitsU64(Wide value) noexcept {
  return value <= static_cast<Wide>(std::numeric_limits<std::uint64_t>::max());
}

[[nodiscard]] std::uint64_t ToU64(Wide value) noexcept { return static_cast<std::uint64_t>(value); }

[[nodiscard]] std::uint64_t ElapsedNanoseconds(Clock::time_point start,
                                               Clock::time_point finish) noexcept {
  const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(finish - start).count();
  return elapsed <= 0 ? 0 : static_cast<std::uint64_t>(elapsed);
}

[[nodiscard]] bool DescriptorSupportsPool(const Phase4CaseDescriptor& descriptor,
                                          std::uint32_t requested_pool_size) noexcept {
  for (std::uint8_t index = 0; index < descriptor.requested_pool_size_count; ++index) {
    if (descriptor.requested_pool_sizes[index] == requested_pool_size) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] bool ValidArm(Phase4TrialArm arm) noexcept {
  return arm == Phase4TrialArm::kSequentialBaseline ||
         arm == Phase4TrialArm::kReusableCandidateAllocation;
}

[[nodiscard]] bool ValidOrder(Phase4TrialOrder order) noexcept {
  return order == Phase4TrialOrder::kBaselineFirst || order == Phase4TrialOrder::kCandidateFirst;
}

[[nodiscard]] bool ValidTerminalReason(Phase4NormalizedTerminalReason reason) noexcept {
  return reason == Phase4NormalizedTerminalReason::kFeasible ||
         reason == Phase4NormalizedTerminalReason::kNoAdmissibleCandidate ||
         reason == Phase4NormalizedTerminalReason::kFixedPointStalled ||
         reason == Phase4NormalizedTerminalReason::kStoppingBudgetExhausted ||
         reason == Phase4NormalizedTerminalReason::kResourceRefinementRequired;
}

[[nodiscard]] bool ValidOutcomeSource(Phase4CandidateOutcomeSource source) noexcept {
  return source == Phase4CandidateOutcomeSource::kNotCandidateArm ||
         source == Phase4CandidateOutcomeSource::kPreferredMultiWorld ||
         source == Phase4CandidateOutcomeSource::kCommonLineageOneWorld;
}

[[nodiscard]] Phase4PreparerLifecycleObservation LifecycleObservation(
    const allocator::PersistentCpuCandidatePoolTelemetry& before,
    const allocator::PersistentCpuCandidatePoolTelemetry& after) noexcept {
  return Phase4PreparerLifecycleObservation{
      .workers_started_before = before.workers_started,
      .workers_started_after = after.workers_started,
      .invocations_started_before = before.invocations_started,
      .invocations_started_after = after.invocations_started,
      .invocations_completed_before = before.invocations_completed,
      .invocations_completed_after = after.invocations_completed,
  };
}

[[nodiscard]] std::uint32_t MaximumTerminalSelectionRounds(
    const allocator::CpuCandidateAllocationSessionConfig& config) noexcept {
  std::uint32_t maximum = 0;
  for (const allocator::MultiWorldSchedule& schedule : config.schedules) {
    maximum = std::max(maximum, schedule.maximum_selection_rounds);
  }
  return maximum;
}

[[nodiscard]] std::uint64_t ExpectedKnownConflictCount(
    const Phase4CaseDescriptor& descriptor) noexcept {
  return descriptor.known_unmapped_exact_conflicts ? 1U : 0U;
}

[[nodiscard]] ValidationResult ValidateSpec(const Phase4PairedTrialSpec& spec, Phase4TrialArm arm) {
  if (!ValidArm(arm) || !ValidOrder(spec.execution_order)) {
    return Error(Phase4PairedTrialErrorCode::kInvalidConfiguration, "P4PAIR-ENUM-001",
                 "the trial arm and prescribed execution order must be known v1 values", arm);
  }
  if (spec.schema_version != kPhase4PairedTrialSchemaVersion ||
      spec.baseline_config.schema_version !=
          allocator::kSequentialNegotiatedBaselineSchemaVersion ||
      spec.preparation_config.schema_version !=
          allocator::kCpuCandidatePoolPreparationSchemaVersion ||
      spec.candidate_session_config.schema_version !=
          allocator::kCpuCandidateAllocationSessionSchemaVersion) {
    return Error(Phase4PairedTrialErrorCode::kUnsupportedSchema, "P4PAIR-SCHEMA-001",
                 "the trial or one of its contender configurations has an unsupported schema", arm);
  }
  const Phase4CaseDescriptor* descriptor = FindPhase4CaseDescriptorV1(spec.case_id);
  if (descriptor == nullptr) {
    return Error(Phase4PairedTrialErrorCode::kUnknownCase, "P4PAIR-CASE-001",
                 "the representative case is not in corpus v1", arm);
  }
  if ((spec.requested_pool_size != 4 && spec.requested_pool_size != 8 &&
       spec.requested_pool_size != 16) ||
      !DescriptorSupportsPool(*descriptor, spec.requested_pool_size)) {
    return Error(Phase4PairedTrialErrorCode::kInvalidConfiguration, "P4PAIR-POOL-001",
                 "the requested pool size is not declared by the case descriptor", arm,
                 spec.requested_pool_size, 0);
  }
  if (spec.requested_pool_size == 0 ||
      spec.preparation_config.requested_candidates_per_net != spec.requested_pool_size) {
    return Error(Phase4PairedTrialErrorCode::kInvalidConfiguration, "P4PAIR-POOL-002",
                 "the preparation pool size must equal the trial pool size", arm,
                 spec.requested_pool_size, spec.preparation_config.requested_candidates_per_net);
  }
  const auto& baseline = spec.baseline_config;
  const auto& preparation = spec.preparation_config;
  const auto& session = spec.candidate_session_config;
  if (spec.root_seed != baseline.deterministic_seed ||
      spec.root_seed != preparation.deterministic_seed ||
      spec.root_seed != session.regeneration_execution_config.deterministic_seed) {
    return Error(Phase4PairedTrialErrorCode::kUnequalBudget, "P4PAIR-SEED-001",
                 "every contender seed must equal the paired root seed", arm);
  }
  if (!(baseline.route_limits == preparation.route_limits) ||
      !(baseline.route_limits == session.regeneration_execution_config.route_limits)) {
    return Error(Phase4PairedTrialErrorCode::kUnequalBudget, "P4PAIR-WORK-001",
                 "all four per-query CPU A-star limits must be identical", arm);
  }
  if (!(baseline.price_config == session.price_config) ||
      baseline.intrinsic_cost_weight != session.intrinsic_cost_weight ||
      !(baseline.allocator_limits == session.allocator_limits)) {
    return Error(Phase4PairedTrialErrorCode::kUnequalBudget, "P4PAIR-OBJECTIVE-001",
                 "price, intrinsic-cost, and allocator boundaries must be identical", arm);
  }
  const std::uint64_t expected_conflicts = ExpectedKnownConflictCount(*descriptor);
  if (baseline.known_unmapped_exact_conflict_count != expected_conflicts ||
      session.known_unmapped_exact_conflict_count != expected_conflicts ||
      session.regeneration_execution_config.known_unmapped_exact_conflict_count != 0 ||
      session.multi_world_config.known_unmapped_exact_conflict_count != 0) {
    return Error(Phase4PairedTrialErrorCode::kInvalidConfiguration, "P4PAIR-CONFLICT-001",
                 "the session must be sole nested exact-conflict authority and match the corpus",
                 arm, expected_conflicts, session.known_unmapped_exact_conflict_count);
  }
  if (session.maximum_regeneration_epochs == 0 || session.schedules.empty()) {
    return Error(Phase4PairedTrialErrorCode::kInvalidConfiguration, "P4PAIR-STOP-001",
                 "candidate epochs and terminal schedules must both be nonzero", arm);
  }
  if (session.schedules.size() > allocator::kMaximumMultiWorldsV1) {
    return Error(Phase4PairedTrialErrorCode::kInvalidConfiguration, "P4PAIR-SCHEDULE-003",
                 "the terminal schedule roster exceeds the v1 world bound", arm);
  }
  std::vector<allocator::MultiWorldSchedule> canonical_schedules = session.schedules;
  std::sort(
      canonical_schedules.begin(), canonical_schedules.end(),
      [](const auto& left, const auto& right) { return left.schedule_key < right.schedule_key; });
  for (std::size_t index = 0; index < canonical_schedules.size(); ++index) {
    const allocator::MultiWorldSchedule& schedule = canonical_schedules[index];
    if (schedule.schedule_key == 0 || schedule.search_intrinsic_cost_weight == 0 ||
        schedule.maximum_selection_rounds == 0) {
      return Error(Phase4PairedTrialErrorCode::kInvalidConfiguration, "P4PAIR-SCHEDULE-001",
                   "every terminal schedule key, weight, and round count must be nonzero", arm);
    }
    if (index != 0 && schedule.schedule_key == canonical_schedules[index - 1].schedule_key) {
      return Error(Phase4PairedTrialErrorCode::kInvalidConfiguration, "P4PAIR-SCHEDULE-002",
                   "terminal schedule keys must be unique", arm);
    }
  }
  const std::uint32_t terminal_rounds = MaximumTerminalSelectionRounds(session);
  if (terminal_rounds == 0) {
    return Error(Phase4PairedTrialErrorCode::kInvalidConfiguration, "P4PAIR-STOP-002",
                 "at least one terminal schedule selection round is required", arm);
  }

  const Wide net_count = descriptor->requested_net_count;
  const Wide sweeps = baseline.maximum_sweeps;
  const Wide pool_size = spec.requested_pool_size;
  const Wide epochs = session.maximum_regeneration_epochs;
  const Wide columns_per_epoch = session.regeneration_plan_config.maximum_total_columns;
  const Wide maximum_initial_candidates = net_count * pool_size;
  const Wide allocator_candidate_limit = session.allocator_limits.maximum_candidates;
  const Wide candidate_headroom = allocator_candidate_limit >= maximum_initial_candidates
                                      ? allocator_candidate_limit - maximum_initial_candidates
                                      : 0;
  const Wide maximum_targets =
      std::min({net_count, static_cast<Wide>(session.regeneration_plan_config.maximum_target_nets),
                static_cast<Wide>(session.regeneration_plan_config.maximum_total_resource_actions),
                candidate_headroom});
  const Wide maximum_columns_per_target =
      session.regeneration_plan_config.maximum_resource_actions_per_net == 0
          ? 0
          : std::min(static_cast<Wide>(session.regeneration_plan_config.maximum_columns_per_net),
                     static_cast<Wide>(
                         session.regeneration_plan_config.maximum_resource_actions_per_net) +
                         1);
  const Wide structurally_reachable_columns =
      std::min({columns_per_epoch, candidate_headroom, maximum_targets * maximum_columns_per_target,
                static_cast<Wide>(session.regeneration_plan_config.maximum_total_resource_actions) +
                    maximum_targets});
  if (columns_per_epoch == 0 || structurally_reachable_columns != columns_per_epoch) {
    return Error(Phase4PairedTrialErrorCode::kUnequalBudget, "P4PAIR-PLAN-SHAPE-001",
                 "the declared per-epoch columns exceed deterministic planner shape headroom", arm,
                 ToU64(columns_per_epoch), ToU64(structurally_reachable_columns));
  }
  const Wide baseline_queries = net_count * sweeps;
  const Wide candidate_queries = net_count * pool_size + epochs * columns_per_epoch;
  if (!FitsU64(baseline_queries) || !FitsU64(candidate_queries)) {
    return Error(Phase4PairedTrialErrorCode::kResourceExhausted, "P4PAIR-ARITH-001",
                 "the derived route-query opportunity exceeds uint64", arm);
  }
  if (baseline_queries != candidate_queries) {
    return Error(Phase4PairedTrialErrorCode::kUnequalBudget, "P4PAIR-QUERY-001",
                 "baseline and candidate route-query opportunities differ", arm,
                 ToU64(baseline_queries), ToU64(candidate_queries));
  }
  const Wide work = baseline_queries * baseline.route_limits.maximum_work_units;
  if (!FitsU64(work)) {
    return Error(Phase4PairedTrialErrorCode::kResourceExhausted, "P4PAIR-ARITH-002",
                 "the derived route-work opportunity exceeds uint64", arm);
  }
  const Wide initial_queries = net_count * pool_size;
  const Wide regeneration_queries = epochs * columns_per_epoch;
  const Wide initial_work = initial_queries * baseline.route_limits.maximum_work_units;
  const Wide regeneration_work = regeneration_queries * baseline.route_limits.maximum_work_units;
  const Wide per_epoch_work = columns_per_epoch * baseline.route_limits.maximum_work_units;
  if (!FitsU64(initial_queries) || !FitsU64(regeneration_queries) || !FitsU64(initial_work) ||
      !FitsU64(regeneration_work) || !FitsU64(per_epoch_work)) {
    return Error(Phase4PairedTrialErrorCode::kResourceExhausted, "P4PAIR-ARITH-003",
                 "a derived contender sub-budget exceeds uint64", arm);
  }
  if (baseline.limits.maximum_route_queries != ToU64(baseline_queries) ||
      baseline.limits.maximum_total_route_work_units != ToU64(work) ||
      preparation.limits.maximum_route_queries != ToU64(initial_queries) ||
      preparation.limits.maximum_total_route_work_units != ToU64(initial_work) ||
      session.limits.maximum_total_requested_columns != ToU64(regeneration_queries) ||
      session.limits.maximum_total_route_queries != ToU64(regeneration_queries) ||
      session.limits.maximum_total_route_work_units != ToU64(regeneration_work) ||
      session.regeneration_execution_config.maximum_route_queries != ToU64(columns_per_epoch) ||
      session.regeneration_execution_config.maximum_total_route_work_units !=
          ToU64(per_epoch_work)) {
    return Error(Phase4PairedTrialErrorCode::kUnequalBudget, "P4PAIR-CAP-001",
                 "all aggregate and nested route caps must equal their derived opportunity", arm);
  }
  const Wide normalized_stopping_depth =
      epochs + static_cast<Wide>(terminal_rounds) - static_cast<Wide>(1);
  if (!FitsU64(normalized_stopping_depth) ||
      baseline.maximum_sweeps != ToU64(normalized_stopping_depth)) {
    return Error(Phase4PairedTrialErrorCode::kUnequalBudget, "P4PAIR-STOP-003",
                 "baseline sweeps must equal candidate epochs plus terminal rounds minus one", arm,
                 FitsU64(normalized_stopping_depth) ? ToU64(normalized_stopping_depth) : 0,
                 baseline.maximum_sweeps);
  }
  if (spec.preparation_worker_count == 0 ||
      spec.preparation_worker_count > allocator::kMaximumPersistentCpuCandidateWorkersV1) {
    return Error(Phase4PairedTrialErrorCode::kInvalidConfiguration, "P4PAIR-WORKER-001",
                 "the preparation worker count is outside the supported range", arm);
  }
  if (spec.external_budget.maximum_prepared_elapsed_nanoseconds == 0 ||
      spec.external_budget.maximum_cold_elapsed_nanoseconds == 0 ||
      spec.external_budget.maximum_address_space_bytes == 0 ||
      spec.external_budget.maximum_peak_host_bytes == 0 ||
      spec.external_budget.maximum_prepared_elapsed_nanoseconds >
          spec.external_budget.maximum_cold_elapsed_nanoseconds) {
    return Error(
        Phase4PairedTrialErrorCode::kInvalidConfiguration, "P4PAIR-EXTERNAL-001",
        "positive prepared, cold, and peak caps are required and prepared cannot exceed cold", arm);
  }

  const Phase4RouteOpportunity opportunity{
      .route_queries = ToU64(baseline_queries),
      .route_work_units = ToU64(work),
  };
  return ValidatedTrialSpec{
      .descriptor = descriptor,
      .opportunity = opportunity,
      .candidate_columns_per_epoch = ToU64(columns_per_epoch),
      .candidate_terminal_selection_rounds = terminal_rounds,
      .budget_checksum = internal::ComputePhase4PairedBudgetChecksumV1(
          spec, opportunity, descriptor->requested_net_count, ToU64(columns_per_epoch),
          terminal_rounds),
  };
}

[[nodiscard]] Phase4BoardOutcome Outcome(const allocator::OneWorldAllocation& world) noexcept {
  return Phase4BoardOutcome{
      .selected_net_count = world.selected_net_count,
      .no_candidate_net_count = world.no_candidate_net_count,
      .overused_resource_count = world.overused_resource_count,
      .total_overuse_units = world.total_overuse_units,
      .total_intrinsic_cost = world.total_intrinsic_cost,
      .world_checksum = world.world_checksum,
  };
}

[[nodiscard]] Phase4NormalizedTerminalReason Normalize(
    allocator::SequentialNegotiatedTerminalReason reason) noexcept {
  switch (reason) {
    case allocator::SequentialNegotiatedTerminalReason::kFeasible:
      return Phase4NormalizedTerminalReason::kFeasible;
    case allocator::SequentialNegotiatedTerminalReason::kNoAdmissibleCandidate:
      return Phase4NormalizedTerminalReason::kNoAdmissibleCandidate;
    case allocator::SequentialNegotiatedTerminalReason::kFixedPointStalled:
      return Phase4NormalizedTerminalReason::kFixedPointStalled;
    case allocator::SequentialNegotiatedTerminalReason::kSweepBudgetExhausted:
      return Phase4NormalizedTerminalReason::kStoppingBudgetExhausted;
  }
  return Phase4NormalizedTerminalReason::kStoppingBudgetExhausted;
}

[[nodiscard]] Phase4NormalizedTerminalReason Normalize(
    allocator::CpuCandidateAllocationTerminalReason reason) noexcept {
  switch (reason) {
    case allocator::CpuCandidateAllocationTerminalReason::kFeasible:
      return Phase4NormalizedTerminalReason::kFeasible;
    case allocator::CpuCandidateAllocationTerminalReason::kFixedPoint:
      return Phase4NormalizedTerminalReason::kFixedPointStalled;
    case allocator::CpuCandidateAllocationTerminalReason::kRegenerationEpochLimit:
      return Phase4NormalizedTerminalReason::kStoppingBudgetExhausted;
    case allocator::CpuCandidateAllocationTerminalReason::kResourceRefinementRequired:
      return Phase4NormalizedTerminalReason::kResourceRefinementRequired;
  }
  return Phase4NormalizedTerminalReason::kStoppingBudgetExhausted;
}

[[nodiscard]] std::optional<std::uint64_t> CandidateCount(
    const std::vector<allocator::CandidatePool>& pools) noexcept {
  Wide count = 0;
  for (const allocator::CandidatePool& pool : pools) {
    count += pool.candidates.size();
  }
  if (!FitsU64(count)) {
    return std::nullopt;
  }
  return ToU64(count);
}

[[nodiscard]] Phase4TrialArmSemantics CommonSemantics(
    Phase4TrialArm arm, const Phase4PairedTrialSpec& spec, const Phase4RepresentativeCase& corpus,
    const ValidatedTrialSpec& validated) noexcept {
  Phase4TrialArmSemantics semantics;
  semantics.arm = arm;
  semantics.execution_order = spec.execution_order;
  semantics.corpus_checksum = Phase4RepresentativeCorpusChecksumV1();
  semantics.case_id = spec.case_id;
  semantics.descriptor_fingerprint = FingerprintPhase4CaseDescriptorV1(corpus.descriptor);
  semantics.case_checksum = corpus.case_checksum;
  semantics.board_content_hash = corpus.board.content_hash();
  semantics.workload_checksum = corpus.workload.workload_checksum();
  semantics.budget_checksum = validated.budget_checksum;
  semantics.workload_net_count = corpus.descriptor.requested_net_count;
  semantics.requested_pool_size = spec.requested_pool_size;
  semantics.repetition_index = spec.repetition_index;
  semantics.root_seed = spec.root_seed;
  semantics.preparation_worker_count = spec.preparation_worker_count;
  semantics.baseline_sweeps = spec.baseline_config.maximum_sweeps;
  semantics.candidate_regeneration_epochs =
      spec.candidate_session_config.maximum_regeneration_epochs;
  semantics.candidate_columns_per_epoch = validated.candidate_columns_per_epoch;
  semantics.candidate_terminal_selection_rounds = validated.candidate_terminal_selection_rounds;
  semantics.external_budget = spec.external_budget;
  semantics.opportunity = validated.opportunity;
  return semantics;
}

[[nodiscard]] bool EntityRefBefore(board_ir::EntityRef left, board_ir::EntityRef right) noexcept {
  return std::tie(left.id, left.generation) < std::tie(right.id, right.generation);
}

[[nodiscard]] bool Increment(std::uint64_t* value) noexcept {
  if (*value == std::numeric_limits<std::uint64_t>::max()) {
    return false;
  }
  ++*value;
  return true;
}

[[nodiscard]] Phase4PerNetReportV1* FindPerNet(std::vector<Phase4PerNetReportV1>* reports,
                                               board_ir::EntityRef net) noexcept {
  const auto iterator =
      std::lower_bound(reports->begin(), reports->end(), net,
                       [](const Phase4PerNetReportV1& report, board_ir::EntityRef key) {
                         return EntityRefBefore(report.net, key);
                       });
  if (iterator == reports->end() || !(iterator->net == net)) {
    return nullptr;
  }
  return &*iterator;
}

[[nodiscard]] bool IsExactValidationRejection(
    const std::optional<candidates::CandidateRejectionCode>& code) noexcept {
  return code.has_value() && *code == candidates::CandidateRejectionCode::kExactValidation;
}

[[nodiscard]] bool IsExactValidationRejection(
    const std::optional<candidates::CandidateRejection>& rejection) noexcept {
  return rejection.has_value() &&
         rejection->code == candidates::CandidateRejectionCode::kExactValidation;
}

[[nodiscard]] bool AddBaselineColumn(const allocator::SequentialNegotiatedColumnRecord& column,
                                     Phase4PerNetColumnOutcomesV1* outcomes) noexcept {
  if (!Increment(&outcomes->requested_columns) || !Increment(&outcomes->executed_route_queries)) {
    return false;
  }
  switch (column.outcome) {
    case allocator::SequentialNegotiatedColumnOutcome::kAdmitted:
      return Increment(&outcomes->admitted_candidates);
    case allocator::SequentialNegotiatedColumnOutcome::kRouteDisconnected:
      return Increment(&outcomes->disconnected_columns);
    case allocator::SequentialNegotiatedColumnOutcome::kRouteUnsupported:
      return Increment(&outcomes->unsupported_columns);
    case allocator::SequentialNegotiatedColumnOutcome::kBuildRejected:
    case allocator::SequentialNegotiatedColumnOutcome::kAdmissionRejected:
      return Increment(IsExactValidationRejection(column.rejection)
                           ? &outcomes->exact_validation_rejections
                           : &outcomes->other_rejections);
  }
  return false;
}

[[nodiscard]] bool AddPreparationColumn(const allocator::CpuCandidatePoolColumnRecord& column,
                                        Phase4PerNetColumnOutcomesV1* outcomes) noexcept {
  if (!Increment(&outcomes->requested_columns)) {
    return false;
  }
  const bool skipped =
      column.outcome == allocator::CpuCandidatePoolColumnOutcome::kSkippedAfterDisconnectedProof ||
      column.outcome == allocator::CpuCandidatePoolColumnOutcome::kSkippedAfterUnsupportedProof;
  if (!skipped && !Increment(&outcomes->executed_route_queries)) {
    return false;
  }
  switch (column.outcome) {
    case allocator::CpuCandidatePoolColumnOutcome::kAdmitted:
      return Increment(&outcomes->admitted_candidates);
    case allocator::CpuCandidatePoolColumnOutcome::kDuplicate:
      return Increment(&outcomes->duplicate_candidates);
    case allocator::CpuCandidatePoolColumnOutcome::kRouteDisconnected:
      return Increment(&outcomes->disconnected_columns);
    case allocator::CpuCandidatePoolColumnOutcome::kRouteUnsupported:
      return Increment(&outcomes->unsupported_columns);
    case allocator::CpuCandidatePoolColumnOutcome::kSkippedAfterDisconnectedProof:
    case allocator::CpuCandidatePoolColumnOutcome::kSkippedAfterUnsupportedProof:
      return Increment(&outcomes->skipped_columns);
    case allocator::CpuCandidatePoolColumnOutcome::kBuildRejected:
    case allocator::CpuCandidatePoolColumnOutcome::kAdmissionRejected:
      return Increment(IsExactValidationRejection(column.rejection_code)
                           ? &outcomes->exact_validation_rejections
                           : &outcomes->other_rejections);
  }
  return false;
}

[[nodiscard]] bool AddRegenerationColumn(const allocator::TargetedRegenerationColumnRecord& column,
                                         Phase4PerNetColumnOutcomesV1* outcomes) noexcept {
  if (!Increment(&outcomes->requested_columns) || !Increment(&outcomes->executed_route_queries)) {
    return false;
  }
  switch (column.outcome) {
    case allocator::TargetedRegenerationColumnOutcome::kAdmitted:
      return Increment(&outcomes->admitted_candidates);
    case allocator::TargetedRegenerationColumnOutcome::kDuplicate:
      return Increment(&outcomes->duplicate_candidates);
    case allocator::TargetedRegenerationColumnOutcome::kRouteDisconnected:
      return Increment(&outcomes->disconnected_columns);
    case allocator::TargetedRegenerationColumnOutcome::kRouteUnsupported:
      return Increment(&outcomes->unsupported_columns);
    case allocator::TargetedRegenerationColumnOutcome::kBuildRejected:
    case allocator::TargetedRegenerationColumnOutcome::kAdmissionRejected:
      return Increment(IsExactValidationRejection(column.rejection_code)
                           ? &outcomes->exact_validation_rejections
                           : &outcomes->other_rejections);
    case allocator::TargetedRegenerationColumnOutcome::kQueryInFlight:
    case allocator::TargetedRegenerationColumnOutcome::kBuildInFlight:
    case allocator::TargetedRegenerationColumnOutcome::kGeneratedPendingPublication:
    case allocator::TargetedRegenerationColumnOutcome::kRejectionEvidenceInFlight:
    case allocator::TargetedRegenerationColumnOutcome::
        kPublicationCommittedOutcomeCorrelationPending:
      return false;
  }
  return false;
}

using TelemetryBuildResult = std::variant<Phase4ArmReportTelemetryV1, Phase4PairedTrialError>;

[[nodiscard]] TelemetryBuildResult BuildPerNetTelemetry(
    const Phase4TrialArmSemantics& semantics, const allocator::MultiNetWorkload& workload,
    const std::vector<allocator::CandidatePool>& pools,
    const allocator::OneWorldAllocation& selected_world,
    const std::vector<allocator::SequentialNegotiatedColumnRecord>* baseline_columns,
    const allocator::PreparedCpuCandidatePools* preparation,
    const std::vector<allocator::CpuCandidateAllocationEpochRecord>* epochs) {
  Phase4ArmReportTelemetryV1 telemetry;
  telemetry.associated_semantic_checksum = semantics.semantic_checksum;
  telemetry.per_net.reserve(workload.nets().size());
  for (const allocator::PreparedNetRoutingContext& context : workload.nets()) {
    Phase4PerNetReportV1 report;
    report.net = context.request.net;
    telemetry.per_net.push_back(std::move(report));
  }
  std::sort(telemetry.per_net.begin(), telemetry.per_net.end(),
            [](const Phase4PerNetReportV1& left, const Phase4PerNetReportV1& right) {
              return EntityRefBefore(left.net, right.net);
            });
  for (std::size_t index = 1; index < telemetry.per_net.size(); ++index) {
    if (telemetry.per_net[index - 1].net == telemetry.per_net[index].net) {
      return Error(Phase4PairedTrialErrorCode::kInternalInvariant, "P4REPORT-NET-001",
                   "the workload contains a duplicate full EntityRef", semantics.arm);
    }
  }
  if (pools.size() != telemetry.per_net.size() ||
      selected_world.selections.size() != telemetry.per_net.size()) {
    return Error(Phase4PairedTrialErrorCode::kInternalInvariant, "P4REPORT-ROSTER-001",
                 "final pools and selected-world outcomes must each contain exactly N nets",
                 semantics.arm);
  }
  for (std::size_t index = 0; index < telemetry.per_net.size(); ++index) {
    if (!(pools[index].net == telemetry.per_net[index].net) ||
        !(selected_world.selections[index].net == telemetry.per_net[index].net)) {
      return Error(Phase4PairedTrialErrorCode::kInternalInvariant, "P4REPORT-ROSTER-002",
                   "final pools and selections must be strictly sorted by the full EntityRef",
                   semantics.arm);
    }
  }

  const auto report_for =
      [&telemetry, &semantics](
          board_ir::EntityRef net) -> std::variant<Phase4PerNetReportV1*, Phase4PairedTrialError> {
    Phase4PerNetReportV1* report = FindPerNet(&telemetry.per_net, net);
    if (report == nullptr) {
      return Error(Phase4PairedTrialErrorCode::kInternalInvariant, "P4REPORT-COLUMN-NET-001",
                   "a column, pool, or selection names a net outside the workload", semantics.arm);
    }
    return report;
  };
  if (baseline_columns != nullptr) {
    for (const allocator::SequentialNegotiatedColumnRecord& column : *baseline_columns) {
      auto found = report_for(column.net);
      if (std::holds_alternative<Phase4PairedTrialError>(found)) {
        return std::get<Phase4PairedTrialError>(found);
      }
      if (!AddBaselineColumn(column, &std::get<Phase4PerNetReportV1*>(found)->columns)) {
        return Error(Phase4PairedTrialErrorCode::kInternalInvariant, "P4REPORT-COLUMN-001",
                     "a baseline column outcome is unknown or a counter overflowed", semantics.arm);
      }
    }
  }
  if (preparation != nullptr) {
    for (const allocator::CpuCandidatePoolColumnRecord& column : preparation->columns()) {
      auto found = report_for(column.net);
      if (std::holds_alternative<Phase4PairedTrialError>(found)) {
        return std::get<Phase4PairedTrialError>(found);
      }
      if (!internal::AccumulatePhase4PreparationColumnV1(
              column, &std::get<Phase4PerNetReportV1*>(found)->columns)) {
        return Error(Phase4PairedTrialErrorCode::kInternalInvariant, "P4REPORT-COLUMN-002",
                     "a preparation column outcome is unknown or a counter overflowed",
                     semantics.arm);
      }
    }
  }
  if (epochs != nullptr) {
    for (const allocator::CpuCandidateAllocationEpochRecord& epoch : *epochs) {
      for (const allocator::TargetedRegenerationColumnRecord& column : epoch.columns) {
        auto found = report_for(column.net);
        if (std::holds_alternative<Phase4PairedTrialError>(found)) {
          return std::get<Phase4PairedTrialError>(found);
        }
        if (!internal::AccumulatePhase4RegenerationColumnV1(
                column, &std::get<Phase4PerNetReportV1*>(found)->columns)) {
          return Error(Phase4PairedTrialErrorCode::kInternalInvariant, "P4REPORT-COLUMN-003",
                       "a successful regeneration retained an in-flight outcome or overflowed",
                       semantics.arm);
        }
      }
    }
  }

  for (const allocator::CandidatePool& pool : pools) {
    auto found = report_for(pool.net);
    if (std::holds_alternative<Phase4PairedTrialError>(found)) {
      return std::get<Phase4PairedTrialError>(found);
    }
    Phase4PerNetReportV1& report = *std::get<Phase4PerNetReportV1*>(found);
    if (pool.candidates.size() > std::numeric_limits<std::uint64_t>::max()) {
      return Error(Phase4PairedTrialErrorCode::kInternalInvariant, "P4REPORT-POOL-001",
                   "a final per-net candidate pool exceeds uint64", semantics.arm);
    }
    report.final_pool_size = static_cast<std::uint64_t>(pool.candidates.size());
    std::vector<candidates::CandidateSignature> geometry_signatures;
    std::vector<candidates::CandidateSignature> resource_signatures;
    geometry_signatures.reserve(pool.candidates.size());
    resource_signatures.reserve(pool.candidates.size());
    for (const candidates::StoredCandidate& candidate : pool.candidates) {
      if (candidate == nullptr || !(candidate->net() == pool.net)) {
        return Error(Phase4PairedTrialErrorCode::kInternalInvariant, "P4REPORT-POOL-002",
                     "a final pool contains a null or wrong-net candidate", semantics.arm);
      }
      geometry_signatures.push_back(candidate->data().geometry_signature);
      resource_signatures.push_back(candidate->data().resource_signature);
      const std::uint64_t cost = candidate->data().metrics.intrinsic_base_cost;
      if (!report.pool_best_intrinsic_cost.has_value() || cost < *report.pool_best_intrinsic_cost) {
        report.pool_best_intrinsic_cost = cost;
      }
    }
    std::sort(geometry_signatures.begin(), geometry_signatures.end());
    geometry_signatures.erase(std::unique(geometry_signatures.begin(), geometry_signatures.end()),
                              geometry_signatures.end());
    std::sort(resource_signatures.begin(), resource_signatures.end());
    resource_signatures.erase(std::unique(resource_signatures.begin(), resource_signatures.end()),
                              resource_signatures.end());
    report.unique_geometry_signature_count = geometry_signatures.size();
    report.unique_resource_signature_count = resource_signatures.size();

    Wide resource_sum = 0;
    Wide geometric_sum = 0;
    std::uint64_t resource_minimum = kPhase4OverlapPartsPerMillion;
    std::uint64_t geometric_minimum = kPhase4OverlapPartsPerMillion;
    for (std::size_t left = 0; left < pool.candidates.size(); ++left) {
      for (std::size_t right = left + 1; right < pool.candidates.size(); ++right) {
        const std::optional<std::uint64_t> resource_ppm = candidates::ResourceJaccardOverlapPpmV1(
            *pool.candidates[left], *pool.candidates[right]);
        const std::optional<std::uint64_t> geometric_ppm =
            candidates::GeometricOverlapRatioPpmV1(*pool.candidates[left], *pool.candidates[right]);
        if (!resource_ppm.has_value() || !geometric_ppm.has_value() ||
            *resource_ppm > kPhase4OverlapPartsPerMillion ||
            *geometric_ppm > kPhase4OverlapPartsPerMillion ||
            !Increment(&report.candidate_pair_count)) {
          return Error(Phase4PairedTrialErrorCode::kInternalInvariant, "P4REPORT-OVERLAP-001",
                       "a candidate overlap is outside [0,1] or pair accounting overflowed",
                       semantics.arm);
        }
        resource_sum += *resource_ppm;
        geometric_sum += *geometric_ppm;
        resource_minimum = std::min(resource_minimum, *resource_ppm);
        geometric_minimum = std::min(geometric_minimum, *geometric_ppm);
      }
    }
    if (report.candidate_pair_count != 0) {
      const Wide half = report.candidate_pair_count / 2;
      const Wide resource_mean = (resource_sum + half) / report.candidate_pair_count;
      const Wide geometric_mean = (geometric_sum + half) / report.candidate_pair_count;
      if (!FitsU64(resource_mean) || !FitsU64(geometric_mean)) {
        return Error(Phase4PairedTrialErrorCode::kResourceExhausted, "P4REPORT-ARITH-001",
                     "mean overlap accounting exceeds uint64", semantics.arm);
      }
      report.mean_resource_overlap_ppm = ToU64(resource_mean);
      report.minimum_resource_overlap_ppm = resource_minimum;
      report.mean_geometric_overlap_ppm = ToU64(geometric_mean);
      report.minimum_geometric_overlap_ppm = geometric_minimum;
    }
  }

  for (std::size_t index = 0; index < selected_world.selections.size(); ++index) {
    const allocator::NetSelection& selection = selected_world.selections[index];
    auto found = report_for(selection.net);
    if (std::holds_alternative<Phase4PairedTrialError>(found)) {
      return std::get<Phase4PairedTrialError>(found);
    }
    Phase4PerNetReportV1& report = *std::get<Phase4PerNetReportV1*>(found);
    report.selected_status = selection.status;
    if (selection.status == allocator::NetSelectionStatus::kNoAdmissibleCandidate) {
      if (selection.candidate != nullptr || selection.candidate_id.has_value() ||
          selection.candidate_payload_checksum.has_value() || report.final_pool_size != 0) {
        return Error(Phase4PairedTrialErrorCode::kInternalInvariant, "P4REPORT-SELECTION-002",
                     "a no-candidate selection has candidate state or a nonempty final pool",
                     semantics.arm);
      }
      continue;
    }
    if (selection.status != allocator::NetSelectionStatus::kSelected ||
        selection.candidate == nullptr || !selection.candidate_id.has_value() ||
        !selection.candidate_payload_checksum.has_value() ||
        !(selection.candidate->net() == selection.net) ||
        !(selection.candidate->id() == *selection.candidate_id) ||
        selection.candidate->data().payload_checksum != *selection.candidate_payload_checksum ||
        selection.intrinsic_cost != selection.candidate->data().metrics.intrinsic_base_cost) {
      return Error(Phase4PairedTrialErrorCode::kInternalInvariant, "P4REPORT-SELECTION-003",
                   "a selected candidate has incomplete or inconsistent identity and metrics",
                   semantics.arm);
    }
    const allocator::CandidatePool& pool = pools[index];
    const bool belongs_to_pool =
        std::any_of(pool.candidates.begin(), pool.candidates.end(),
                    [&selection](const candidates::StoredCandidate& candidate) {
                      return candidate != nullptr && *candidate == *selection.candidate;
                    });
    if (!belongs_to_pool) {
      return Error(Phase4PairedTrialErrorCode::kInternalInvariant, "P4REPORT-SELECTION-004",
                   "the selected full immutable candidate does not belong to its final pool",
                   semantics.arm);
    }
    report.selected_candidate_id = selection.candidate_id;
    report.selected_candidate_payload_checksum = selection.candidate_payload_checksum;
    report.selected_candidate_metrics = selection.candidate->data().metrics;
  }

  telemetry.telemetry_checksum = internal::ComputePhase4ArmReportTelemetryChecksumV1(telemetry);
  if (std::optional<Phase4PairedTrialError> error =
          internal::ValidatePhase4ArmReportTelemetryV1(semantics, workload, telemetry);
      error.has_value()) {
    return *error;
  }
  return telemetry;
}

using ArmSemanticsResult = std::variant<Phase4TrialArmSemantics, Phase4TrialArmFailure>;

[[nodiscard]] ArmSemanticsResult ExecuteBaseline(const Phase4PairedTrialSpec& spec,
                                                 Phase4RepresentativeCase corpus,
                                                 const ValidatedTrialSpec& validated,
                                                 Phase4ArmReportTelemetryV1* telemetry) {
  Phase4TrialArmSemantics semantics =
      CommonSemantics(Phase4TrialArm::kSequentialBaseline, spec, corpus, validated);
  allocator::SequentialNegotiatedBaselineExecution execution =
      allocator::ExecuteSequentialNegotiatedBaseline(corpus.board, corpus.workload,
                                                     corpus.capacities, spec.baseline_config);
  if (std::holds_alternative<allocator::SequentialNegotiatedBaselineError>(execution)) {
    allocator::SequentialNegotiatedBaselineError child =
        std::get<allocator::SequentialNegotiatedBaselineError>(std::move(execution));
    const Phase4PairedTrialError summary =
        Error(Phase4PairedTrialErrorCode::kSequentialExecution, child.invariant_id, child.detail,
              Phase4TrialArm::kSequentialBaseline, child.required, child.configured);
    return ArmFailure(summary, Phase4SequentialFailureState{
                                   .case_state = std::move(corpus),
                                   .error = std::move(child),
                               });
  }
  allocator::SequentialNegotiatedBaselineResult result =
      std::get<allocator::SequentialNegotiatedBaselineResult>(std::move(execution));
  const std::optional<std::uint64_t> candidate_count = CandidateCount(result.final_pools());
  if (!candidate_count.has_value()) {
    return ArmFailure(Error(Phase4PairedTrialErrorCode::kResourceExhausted, "P4PAIR-ARITH-004",
                            "the final baseline candidate count exceeds uint64",
                            Phase4TrialArm::kSequentialBaseline));
  }
  semantics.capacity_model_checksum = result.capacity_model_checksum();
  semantics.actual = Phase4RouteOpportunity{
      .route_queries = result.counters().route_queries,
      .route_work_units = result.counters().route_work_units,
  };
  semantics.requested_columns = result.counters().route_queries;
  semantics.admitted_candidates = result.counters().admitted_candidates;
  semantics.rejected_columns = result.counters().rejected_routes;
  semantics.final_candidate_count = *candidate_count;
  semantics.algorithm_session_checksum = result.session_checksum();
  semantics.terminal_reason = Normalize(result.terminal_reason());
  semantics.outcome = Outcome(result.final_world());
  if (semantics.actual.route_queries > semantics.opportunity.route_queries ||
      semantics.actual.route_work_units > semantics.opportunity.route_work_units) {
    return ArmFailure(Error(Phase4PairedTrialErrorCode::kInternalInvariant, "P4PAIR-ACTUAL-001",
                            "baseline actual route work exceeds its declared opportunity",
                            Phase4TrialArm::kSequentialBaseline));
  }
  semantics.semantic_checksum = internal::ComputePhase4TrialArmSemanticChecksumV1(semantics);
  if (telemetry != nullptr) {
    TelemetryBuildResult built =
        BuildPerNetTelemetry(semantics, corpus.workload, result.final_pools(), result.final_world(),
                             &result.columns(), nullptr, nullptr);
    if (std::holds_alternative<Phase4PairedTrialError>(built)) {
      return ArmFailure(std::get<Phase4PairedTrialError>(built));
    }
    *telemetry = std::get<Phase4ArmReportTelemetryV1>(std::move(built));
  }
  return semantics;
}

[[nodiscard]] ArmSemanticsResult ExecuteCandidate(
    const Phase4PairedTrialSpec& spec, Phase4RepresentativeCase corpus,
    const ValidatedTrialSpec& validated, allocator::PersistentCpuCandidatePoolPreparer& preparer,
    Phase4ArmReportTelemetryV1* telemetry) {
  Phase4TrialArmSemantics semantics =
      CommonSemantics(Phase4TrialArm::kReusableCandidateAllocation, spec, corpus, validated);
  allocator::PreparedCpuCandidatePoolsResult preparation =
      allocator::PrepareInitialCpuCandidatePools(preparer, corpus.board, corpus.workload,
                                                 spec.preparation_config);
  if (std::holds_alternative<allocator::CpuCandidatePoolPreparationError>(preparation)) {
    allocator::CpuCandidatePoolPreparationError child =
        std::get<allocator::CpuCandidatePoolPreparationError>(std::move(preparation));
    const Phase4PairedTrialError summary =
        Error(Phase4PairedTrialErrorCode::kCandidatePreparation, child.invariant_id, child.detail,
              Phase4TrialArm::kReusableCandidateAllocation, child.required, child.configured);
    return ArmFailure(summary, Phase4CandidatePreparationFailureState{
                                   .case_state = std::move(corpus),
                                   .error = std::move(child),
                               });
  }
  allocator::PreparedCpuCandidatePools prepared =
      std::get<allocator::PreparedCpuCandidatePools>(std::move(preparation));
  const allocator::CpuCandidatePoolPreparationCounters preparation_counters = prepared.counters();
  const std::uint64_t preparation_checksum = prepared.preparation_checksum();
  allocator::CpuCandidateAllocationSessionResult execution =
      allocator::ExecuteCpuCandidateAllocationSession(
          allocator::kCpuCandidateAllocationSessionSchemaVersion, std::move(corpus.board),
          std::move(corpus.workload), std::move(corpus.capacities), std::move(prepared),
          spec.candidate_session_config);
  if (std::holds_alternative<allocator::CpuCandidateAllocationSessionError>(execution)) {
    allocator::CpuCandidateAllocationSessionError child =
        std::get<allocator::CpuCandidateAllocationSessionError>(std::move(execution));
    return internal::PreservePhase4CandidateSessionFailureV1(std::move(corpus), std::move(prepared),
                                                             std::move(child));
  }
  allocator::CpuCandidateAllocationSession session =
      std::get<allocator::CpuCandidateAllocationSession>(std::move(execution));
  const auto& counters = session.counters();
  const Wide actual_queries =
      static_cast<Wide>(preparation_counters.executed_route_queries) + counters.route_queries;
  const Wide actual_work =
      static_cast<Wide>(preparation_counters.route_work_units) + counters.route_work_units;
  const Wide requested_columns =
      static_cast<Wide>(preparation_counters.requested_columns) + counters.requested_columns;
  const Wide admitted_candidates =
      static_cast<Wide>(preparation_counters.admitted_candidates) + counters.admitted_candidates;
  const Wide rejected_columns =
      static_cast<Wide>(preparation_counters.rejected_columns) + counters.rejected_columns;
  if (!FitsU64(actual_queries) || !FitsU64(actual_work) || !FitsU64(requested_columns) ||
      !FitsU64(admitted_candidates) || !FitsU64(rejected_columns)) {
    return ArmFailure(Error(Phase4PairedTrialErrorCode::kResourceExhausted, "P4PAIR-ARITH-005",
                            "candidate actual counters exceed uint64",
                            Phase4TrialArm::kReusableCandidateAllocation));
  }
  const std::optional<std::uint64_t> candidate_count = CandidateCount(session.final_pools());
  if (!candidate_count.has_value()) {
    return ArmFailure(Error(Phase4PairedTrialErrorCode::kResourceExhausted, "P4PAIR-ARITH-006",
                            "the final candidate-pool count exceeds uint64",
                            Phase4TrialArm::kReusableCandidateAllocation));
  }

  const allocator::OneWorldAllocation* chosen_world = &session.final_single_world();
  Phase4CandidateOutcomeSource outcome_source =
      Phase4CandidateOutcomeSource::kCommonLineageOneWorld;
  if (const std::optional<std::uint64_t> preferred =
          session.final_multi_world().preferred_world_identity();
      preferred.has_value()) {
    const auto& retained = session.final_multi_world().retained_worlds();
    const auto iterator =
        std::find_if(retained.begin(), retained.end(),
                     [preferred](const auto& world) { return world.world_identity == *preferred; });
    if (iterator == retained.end()) {
      return ArmFailure(Error(Phase4PairedTrialErrorCode::kInternalInvariant,
                              "P4PAIR-CANDIDATE-WORLD-001",
                              "the preferred multi-world identity is not retained",
                              Phase4TrialArm::kReusableCandidateAllocation));
    }
    chosen_world = &iterator->world;
    outcome_source = Phase4CandidateOutcomeSource::kPreferredMultiWorld;
  }

  semantics.board_content_hash = session.board().content_hash();
  semantics.workload_checksum = session.workload().workload_checksum();
  semantics.capacity_model_checksum = session.final_price_state().capacity_model_checksum();
  semantics.actual = Phase4RouteOpportunity{
      .route_queries = ToU64(actual_queries),
      .route_work_units = ToU64(actual_work),
  };
  semantics.preparation_route_queries = preparation_counters.executed_route_queries;
  semantics.preparation_route_work_units = preparation_counters.route_work_units;
  semantics.regeneration_route_queries = counters.route_queries;
  semantics.regeneration_route_work_units = counters.route_work_units;
  semantics.requested_columns = ToU64(requested_columns);
  semantics.admitted_candidates = ToU64(admitted_candidates);
  semantics.rejected_columns = ToU64(rejected_columns);
  semantics.final_candidate_count = *candidate_count;
  semantics.preparation_checksum = preparation_checksum;
  semantics.algorithm_session_checksum = session.session_checksum();
  semantics.final_pool_manifest_checksum = session.final_pool_manifest_checksum();
  semantics.final_rejection_manifest_checksum = session.final_rejection_manifest_checksum();
  semantics.terminal_reason = Normalize(session.terminal_reason());
  semantics.candidate_outcome_source = outcome_source;
  semantics.outcome = Outcome(*chosen_world);
  if (semantics.actual.route_queries > semantics.opportunity.route_queries ||
      semantics.actual.route_work_units > semantics.opportunity.route_work_units) {
    return ArmFailure(Error(Phase4PairedTrialErrorCode::kInternalInvariant, "P4PAIR-ACTUAL-002",
                            "candidate actual route work exceeds its declared opportunity",
                            Phase4TrialArm::kReusableCandidateAllocation));
  }
  semantics.semantic_checksum = internal::ComputePhase4TrialArmSemanticChecksumV1(semantics);
  if (telemetry != nullptr) {
    TelemetryBuildResult built =
        BuildPerNetTelemetry(semantics, session.workload(), session.final_pools(), *chosen_world,
                             nullptr, &session.preparation(), &session.epochs());
    if (std::holds_alternative<Phase4PairedTrialError>(built)) {
      return ArmFailure(std::get<Phase4PairedTrialError>(built));
    }
    *telemetry = std::get<Phase4ArmReportTelemetryV1>(std::move(built));
  }
  return semantics;
}

void HashRouteLimits(board_ir::StableHashBuilder& hash,
                     const routing::CpuRouteWorkLimits& limits) noexcept {
  hash.AddU64(limits.maximum_work_units);
  hash.AddU64(limits.maximum_record_count);
  hash.AddU64(limits.maximum_queue_size);
  hash.AddU64(limits.maximum_reconstruction_states);
}

void HashPriceConfig(board_ir::StableHashBuilder& hash,
                     const allocator::NegotiatedPriceConfig& config) noexcept {
  hash.AddU64(config.present_step_per_overuse_unit);
  hash.AddU64(config.history_step_per_overuse_unit);
  hash.AddU64(config.maximum_price_per_resource);
  hash.AddU32(config.maximum_iterations);
  hash.AddU64(config.maximum_price_records);
}

void HashAllocatorLimits(board_ir::StableHashBuilder& hash,
                         const allocator::OneWorldAllocatorLimits& limits) noexcept {
  hash.AddU64(limits.maximum_nets);
  hash.AddU64(limits.maximum_candidates);
  hash.AddU64(limits.maximum_resource_records);
  hash.AddU64(limits.maximum_expanded_resource_uses);
}

void HashStoreConfig(board_ir::StableHashBuilder& hash,
                     const candidates::CandidateStoreConfig& config) noexcept {
  hash.AddU64(config.maximum_candidates_per_net);
  hash.AddU64(config.maximum_candidate_bytes_per_net);
  hash.AddU64(config.maximum_rejection_records);
  hash.AddU64(config.maximum_rejection_items_per_transaction);
  hash.AddU64(config.maximum_admission_items_per_transaction);
  hash.AddU64(config.maximum_admission_input_bytes_per_transaction);
  hash.AddU64(config.maximum_admission_work_units_per_transaction);
  hash.AddU64(config.maximum_pin_lease_items_per_transaction);
  hash.AddU64(config.maximum_expected_pools_per_invocation);
  hash.AddU64(config.maximum_expected_candidates_per_invocation);
}

void HashBaselineConfig(board_ir::StableHashBuilder& hash,
                        const allocator::SequentialNegotiatedBaselineConfig& config) noexcept {
  hash.AddU32(config.schema_version);
  hash.AddU64(config.deterministic_seed);
  hash.AddU64(config.intrinsic_cost_weight);
  hash.AddU32(config.maximum_sweeps);
  HashRouteLimits(hash, config.route_limits);
  HashPriceConfig(hash, config.price_config);
  HashStoreConfig(hash, config.admission_store_config);
  HashAllocatorLimits(hash, config.allocator_limits);
  const auto& limits = config.limits;
  hash.AddU64(limits.maximum_nets);
  hash.AddU64(limits.maximum_route_queries);
  hash.AddU64(limits.maximum_total_route_work_units);
  hash.AddU64(limits.maximum_policy_projection_visits);
  hash.AddU64(limits.maximum_policy_resource_entries);
  hash.AddU64(limits.maximum_expanded_resource_visits);
  hash.AddU64(limits.maximum_occupancy_resource_records);
  hash.AddU64(limits.maximum_candidate_draft_bytes);
  hash.AddU64(limits.maximum_retained_candidate_bytes);
  hash.AddU64(limits.maximum_retained_rejection_bytes);
  hash.AddU64(limits.maximum_trace_bytes);
  hash.AddU64(config.known_unmapped_exact_conflict_count);
}

void HashPreparationConfig(board_ir::StableHashBuilder& hash,
                           const allocator::CpuCandidatePoolPreparationConfig& config) noexcept {
  hash.AddU32(config.schema_version);
  hash.AddU32(config.requested_candidates_per_net);
  hash.AddU64(config.deterministic_seed);
  hash.AddU64(config.step_surcharge_increment);
  hash.AddU64(config.bend_surcharge_increment);
  hash.AddU64(config.resource_penalty_increment);
  HashRouteLimits(hash, config.route_limits);
  const auto& limits = config.limits;
  hash.AddU64(limits.maximum_nets);
  hash.AddU64(limits.maximum_route_queries);
  hash.AddU64(limits.maximum_total_route_work_units);
  hash.AddU64(limits.maximum_concurrent_route_records);
  hash.AddU64(limits.maximum_concurrent_queue_entries);
  hash.AddU64(limits.maximum_concurrent_reconstruction_states);
  hash.AddU64(limits.maximum_policy_resource_entries);
  hash.AddU64(limits.maximum_retained_candidate_bytes);
  hash.AddU64(limits.maximum_candidate_draft_bytes);
  hash.AddU64(limits.maximum_generated_candidate_bytes);
  HashStoreConfig(hash, config.store_config);
}

void HashMultiWorldConfig(board_ir::StableHashBuilder& hash,
                          const allocator::MultiWorldExecutionConfig& config) noexcept {
  hash.AddU64(config.maximum_worlds);
  hash.AddU64(config.maximum_total_selection_rounds);
  hash.AddU64(config.maximum_total_candidate_evaluations);
  hash.AddU64(config.maximum_total_candidate_span_visits);
  hash.AddU64(config.maximum_total_resource_work_units);
  hash.AddU64(config.maximum_total_net_outcomes);
  hash.AddU64(config.maximum_pareto_comparisons);
  hash.AddU64(config.maximum_buffered_terminal_selection_records);
  hash.AddU64(config.maximum_buffered_terminal_resource_records);
  hash.AddU64(config.maximum_buffered_terminal_price_records);
  hash.AddU64(config.maximum_retained_worlds);
  hash.AddU64(config.maximum_retained_selection_records);
  hash.AddU64(config.maximum_retained_resource_records);
  hash.AddU64(config.maximum_retained_price_records);
  hash.AddU64(config.maximum_retained_winner_pins);
  hash.AddU64(config.maximum_near_feasible_missing_nets);
  hash.AddU64(config.maximum_near_feasible_overuse_units);
  hash.AddU64(config.known_unmapped_exact_conflict_count);
}

void HashSessionConfig(board_ir::StableHashBuilder& hash,
                       const allocator::CpuCandidateAllocationSessionConfig& config) {
  hash.AddU32(config.schema_version);
  hash.AddU64(config.intrinsic_cost_weight);
  hash.AddU32(config.maximum_regeneration_epochs);
  HashPriceConfig(hash, config.price_config);
  HashAllocatorLimits(hash, config.allocator_limits);
  const auto& plan = config.regeneration_plan_config;
  hash.AddU64(plan.maximum_target_nets);
  hash.AddU64(plan.maximum_columns_per_net);
  hash.AddU64(plan.maximum_total_columns);
  hash.AddU64(plan.maximum_resource_actions_per_net);
  hash.AddU64(plan.maximum_total_resource_actions);
  hash.AddU64(plan.maximum_expanded_resource_visits);
  const auto& execution = config.regeneration_execution_config;
  hash.AddU64(execution.deterministic_seed);
  hash.AddU64(execution.maximum_route_queries);
  HashRouteLimits(hash, execution.route_limits);
  hash.AddU64(execution.maximum_total_route_work_units);
  hash.AddU64(execution.maximum_policy_projection_visits);
  hash.AddU64(execution.maximum_policy_resource_entries);
  hash.AddU64(execution.maximum_candidate_draft_bytes);
  hash.AddU64(execution.maximum_generated_candidate_bytes);
  hash.AddU64(execution.maximum_rejection_bytes);
  hash.AddU64(execution.maximum_transient_result_bytes);
  hash.AddU64(execution.known_unmapped_exact_conflict_count);
  hash.AddU64(config.schedules.size());
  std::vector<allocator::MultiWorldSchedule> canonical_schedules = config.schedules;
  std::sort(
      canonical_schedules.begin(), canonical_schedules.end(),
      [](const auto& left, const auto& right) { return left.schedule_key < right.schedule_key; });
  for (const allocator::MultiWorldSchedule& schedule : canonical_schedules) {
    hash.AddU64(schedule.schedule_key);
    hash.AddU64(schedule.search_intrinsic_cost_weight);
    hash.AddU32(schedule.maximum_selection_rounds);
  }
  HashMultiWorldConfig(hash, config.multi_world_config);
  const auto& limits = config.limits;
  hash.AddU64(limits.maximum_epoch_records);
  hash.AddU64(limits.maximum_total_planning_expanded_resource_visits);
  hash.AddU64(limits.maximum_total_requested_columns);
  hash.AddU64(limits.maximum_total_route_queries);
  hash.AddU64(limits.maximum_total_route_work_units);
  hash.AddU64(limits.maximum_total_policy_projection_visits);
  hash.AddU64(limits.maximum_total_generated_candidate_bytes);
  hash.AddU64(limits.maximum_total_rejection_bytes);
  hash.AddU64(limits.maximum_total_transient_result_bytes);
  hash.AddU64(config.known_unmapped_exact_conflict_count);
}

void HashOutcome(board_ir::StableHashBuilder& hash, const Phase4BoardOutcome& outcome) noexcept {
  hash.AddU64(outcome.selected_net_count);
  hash.AddU64(outcome.no_candidate_net_count);
  hash.AddU64(outcome.overused_resource_count);
  hash.AddU64(outcome.total_overuse_units);
  hash.AddU64(outcome.total_intrinsic_cost);
  hash.AddU64(outcome.world_checksum);
}

[[nodiscard]] bool SamePairIdentity(const Phase4TrialArmSemantics& baseline,
                                    const Phase4TrialArmSemantics& candidate) noexcept {
  return baseline.schema_version == candidate.schema_version &&
         baseline.execution_order == candidate.execution_order &&
         baseline.corpus_version == candidate.corpus_version &&
         baseline.corpus_checksum == candidate.corpus_checksum &&
         baseline.case_id == candidate.case_id &&
         baseline.descriptor_fingerprint == candidate.descriptor_fingerprint &&
         baseline.case_checksum == candidate.case_checksum &&
         baseline.board_content_hash == candidate.board_content_hash &&
         baseline.workload_checksum == candidate.workload_checksum &&
         baseline.capacity_model_checksum == candidate.capacity_model_checksum &&
         baseline.budget_checksum == candidate.budget_checksum &&
         baseline.workload_net_count == candidate.workload_net_count &&
         baseline.requested_pool_size == candidate.requested_pool_size &&
         baseline.repetition_index == candidate.repetition_index &&
         baseline.root_seed == candidate.root_seed &&
         baseline.preparation_worker_count == candidate.preparation_worker_count &&
         baseline.baseline_sweeps == candidate.baseline_sweeps &&
         baseline.candidate_regeneration_epochs == candidate.candidate_regeneration_epochs &&
         baseline.candidate_columns_per_epoch == candidate.candidate_columns_per_epoch &&
         baseline.candidate_terminal_selection_rounds ==
             candidate.candidate_terminal_selection_rounds &&
         baseline.external_budget == candidate.external_budget &&
         baseline.opportunity == candidate.opportunity;
}

[[nodiscard]] Phase4LexicographicComparison Compare(const Phase4BoardOutcome& baseline,
                                                    const Phase4BoardOutcome& candidate) noexcept {
  if (baseline.selected_net_count != candidate.selected_net_count) {
    return baseline.selected_net_count > candidate.selected_net_count
               ? Phase4LexicographicComparison::kBaselinePreferred
               : Phase4LexicographicComparison::kCandidatePreferred;
  }
  if (baseline.total_overuse_units != candidate.total_overuse_units) {
    return baseline.total_overuse_units < candidate.total_overuse_units
               ? Phase4LexicographicComparison::kBaselinePreferred
               : Phase4LexicographicComparison::kCandidatePreferred;
  }
  if (baseline.total_intrinsic_cost != candidate.total_intrinsic_cost) {
    return baseline.total_intrinsic_cost < candidate.total_intrinsic_cost
               ? Phase4LexicographicComparison::kBaselinePreferred
               : Phase4LexicographicComparison::kCandidatePreferred;
  }
  return Phase4LexicographicComparison::kTie;
}

}  // namespace

namespace internal {

Phase4TrialArmFailure PreservePhase4CandidateSessionFailureV1(
    Phase4RepresentativeCase case_state, allocator::PreparedCpuCandidatePools prepared,
    allocator::CpuCandidateAllocationSessionError error) {
  const Phase4PairedTrialError summary =
      Error(Phase4PairedTrialErrorCode::kCandidateSession, error.invariant_id, error.detail,
            Phase4TrialArm::kReusableCandidateAllocation);
  return ArmFailure(summary, Phase4CandidateSessionFailureState{
                                 .case_state = std::move(case_state),
                                 .prepared = std::move(prepared),
                                 .error = std::move(error),
                             });
}

std::uint64_t ComputePhase4CanonicalAlgorithmBudgetChecksumV1(const Phase4PairedTrialSpec& spec) {
  board_ir::StableHashBuilder hash;
  hash.AddString("APGAR-PHASE4-CANONICAL-ALGORITHM-BUDGET-V1");
  HashBaselineConfig(hash, spec.baseline_config);
  HashPreparationConfig(hash, spec.preparation_config);
  HashSessionConfig(hash, spec.candidate_session_config);
  return hash.Finish();
}

std::uint64_t ComputePhase4PairedBudgetChecksumV1(
    const Phase4PairedTrialSpec& spec, const Phase4RouteOpportunity& opportunity,
    std::uint32_t workload_net_count, std::uint64_t candidate_columns_per_epoch,
    std::uint32_t candidate_terminal_selection_rounds) {
  board_ir::StableHashBuilder hash;
  hash.AddString("APGAR-PHASE4-PAIRED-BUDGET-V1");
  hash.AddU32(spec.schema_version);
  hash.AddU32(kPhase4RepresentativeCorpusVersion);
  hash.AddU64(Phase4RepresentativeCorpusChecksumV1());
  hash.AddU32(spec.case_id);
  const Phase4CaseDescriptor* descriptor = FindPhase4CaseDescriptorV1(spec.case_id);
  hash.AddU64(descriptor == nullptr ? 0 : FingerprintPhase4CaseDescriptorV1(*descriptor));
  hash.AddU32(spec.requested_pool_size);
  hash.AddU64(spec.root_seed);
  hash.AddU64(spec.corpus_limits.maximum_nets);
  hash.AddU64(spec.corpus_limits.maximum_compiled_nodes);
  hash.AddU64(spec.corpus_limits.maximum_compiled_host_bytes);
  hash.AddU64(spec.corpus_limits.maximum_active_regions);
  hash.AddU64(spec.corpus_limits.maximum_board_entities);
  hash.AddU32(workload_net_count);
  hash.AddU64(opportunity.route_queries);
  hash.AddU64(opportunity.route_work_units);
  hash.AddU64(candidate_columns_per_epoch);
  hash.AddU32(candidate_terminal_selection_rounds);
  hash.AddU64(ComputePhase4CanonicalAlgorithmBudgetChecksumV1(spec));
  hash.AddU64(spec.external_budget.maximum_prepared_elapsed_nanoseconds);
  hash.AddU64(spec.external_budget.maximum_cold_elapsed_nanoseconds);
  hash.AddU64(spec.external_budget.maximum_address_space_bytes);
  hash.AddU64(spec.external_budget.maximum_peak_host_bytes);
  return hash.Finish();
}

std::uint64_t ComputePhase4TrialArmSemanticChecksumV1(
    const Phase4TrialArmSemantics& semantics) noexcept {
  board_ir::StableHashBuilder hash;
  hash.AddString("APGAR-PHASE4-TRIAL-ARM-SEMANTIC-V1");
  hash.AddU32(semantics.schema_version);
  hash.AddByte(static_cast<std::uint8_t>(semantics.arm));
  // Execution order and worker count are operational artifact identity. They
  // deliberately do not alter deterministic algorithm semantics.
  hash.AddU32(semantics.corpus_version);
  hash.AddU64(semantics.corpus_checksum);
  hash.AddU32(semantics.case_id);
  hash.AddU64(semantics.descriptor_fingerprint);
  hash.AddU64(semantics.case_checksum);
  hash.AddU64(semantics.board_content_hash);
  hash.AddU64(semantics.workload_checksum);
  hash.AddU64(semantics.capacity_model_checksum);
  hash.AddU64(semantics.budget_checksum);
  hash.AddU32(semantics.workload_net_count);
  hash.AddU32(semantics.requested_pool_size);
  hash.AddU32(semantics.repetition_index);
  hash.AddU64(semantics.root_seed);
  hash.AddU32(semantics.baseline_sweeps);
  hash.AddU32(semantics.candidate_regeneration_epochs);
  hash.AddU64(semantics.candidate_columns_per_epoch);
  hash.AddU32(semantics.candidate_terminal_selection_rounds);
  hash.AddU64(semantics.external_budget.maximum_prepared_elapsed_nanoseconds);
  hash.AddU64(semantics.external_budget.maximum_cold_elapsed_nanoseconds);
  hash.AddU64(semantics.external_budget.maximum_address_space_bytes);
  hash.AddU64(semantics.external_budget.maximum_peak_host_bytes);
  hash.AddU64(semantics.opportunity.route_queries);
  hash.AddU64(semantics.opportunity.route_work_units);
  hash.AddU64(semantics.actual.route_queries);
  hash.AddU64(semantics.actual.route_work_units);
  hash.AddU64(semantics.preparation_route_queries);
  hash.AddU64(semantics.preparation_route_work_units);
  hash.AddU64(semantics.regeneration_route_queries);
  hash.AddU64(semantics.regeneration_route_work_units);
  hash.AddU64(semantics.requested_columns);
  hash.AddU64(semantics.admitted_candidates);
  hash.AddU64(semantics.rejected_columns);
  hash.AddU64(semantics.final_candidate_count);
  hash.AddU64(semantics.preparation_checksum);
  hash.AddU64(semantics.algorithm_session_checksum);
  hash.AddU64(semantics.final_pool_manifest_checksum);
  hash.AddU64(semantics.final_rejection_manifest_checksum);
  hash.AddByte(static_cast<std::uint8_t>(semantics.terminal_reason));
  hash.AddByte(static_cast<std::uint8_t>(semantics.candidate_outcome_source));
  HashOutcome(hash, semantics.outcome);
  return hash.Finish();
}

std::optional<Phase4PairedTrialError> ValidatePhase4TrialArmSemanticsV1(
    const Phase4TrialArmSemantics& semantics) noexcept {
  if (semantics.schema_version != kPhase4PairedTrialSchemaVersion || !ValidArm(semantics.arm) ||
      !ValidOrder(semantics.execution_order) || !ValidTerminalReason(semantics.terminal_reason) ||
      !ValidOutcomeSource(semantics.candidate_outcome_source) ||
      semantics.preparation_worker_count == 0 ||
      semantics.preparation_worker_count > allocator::kMaximumPersistentCpuCandidateWorkersV1 ||
      semantics.corpus_version != kPhase4RepresentativeCorpusVersion ||
      semantics.corpus_checksum != Phase4RepresentativeCorpusChecksumV1() ||
      semantics.semantic_checksum == 0 ||
      semantics.semantic_checksum != ComputePhase4TrialArmSemanticChecksumV1(semantics)) {
    return Error(Phase4PairedTrialErrorCode::kMeasurementAssociation, "P4PAIR-FINALIZE-001",
                 "the arm semantic identity, enum, or checksum is invalid", semantics.arm);
  }
  const Wide roster_count = static_cast<Wide>(semantics.outcome.selected_net_count) +
                            semantics.outcome.no_candidate_net_count;
  const bool baseline = semantics.arm == Phase4TrialArm::kSequentialBaseline;
  const Wide partition_queries =
      static_cast<Wide>(semantics.preparation_route_queries) + semantics.regeneration_route_queries;
  const Wide partition_work = static_cast<Wide>(semantics.preparation_route_work_units) +
                              semantics.regeneration_route_work_units;
  const Wide terminal_columns =
      static_cast<Wide>(semantics.admitted_candidates) + semantics.rejected_columns;
  const bool valid_per_query_work =
      semantics.opportunity.route_queries != 0 &&
      semantics.opportunity.route_work_units % semantics.opportunity.route_queries == 0 &&
      semantics.opportunity.route_work_units / semantics.opportunity.route_queries != 0;
  const std::uint64_t per_query_work =
      valid_per_query_work
          ? semantics.opportunity.route_work_units / semantics.opportunity.route_queries
          : 0;
  const auto work_within_query_count = [per_query_work](std::uint64_t route_queries,
                                                        std::uint64_t route_work_units) {
    return static_cast<Wide>(route_work_units) <= static_cast<Wide>(route_queries) * per_query_work;
  };
  const bool baseline_component_shape = semantics.preparation_checksum == 0 &&
                                        semantics.algorithm_session_checksum != 0 &&
                                        semantics.final_pool_manifest_checksum == 0 &&
                                        semantics.final_rejection_manifest_checksum == 0;
  const bool candidate_component_shape = semantics.preparation_checksum != 0 &&
                                         semantics.algorithm_session_checksum != 0 &&
                                         semantics.final_pool_manifest_checksum != 0 &&
                                         semantics.final_rejection_manifest_checksum != 0;
  if (roster_count != semantics.workload_net_count || !valid_per_query_work ||
      semantics.capacity_model_checksum == 0 || semantics.outcome.world_checksum == 0 ||
      semantics.actual.route_queries > semantics.opportunity.route_queries ||
      semantics.actual.route_work_units > semantics.opportunity.route_work_units ||
      !work_within_query_count(semantics.actual.route_queries, semantics.actual.route_work_units) ||
      semantics.actual.route_queries > semantics.requested_columns || !FitsU64(terminal_columns) ||
      ToU64(terminal_columns) != semantics.requested_columns ||
      semantics.final_candidate_count > semantics.admitted_candidates ||
      semantics.outcome.selected_net_count > semantics.final_candidate_count ||
      semantics.outcome.overused_resource_count > semantics.outcome.total_overuse_units ||
      (baseline &&
       (semantics.candidate_outcome_source != Phase4CandidateOutcomeSource::kNotCandidateArm ||
        semantics.preparation_route_queries != 0 || semantics.preparation_route_work_units != 0 ||
        semantics.regeneration_route_queries != 0 || semantics.regeneration_route_work_units != 0 ||
        semantics.requested_columns != semantics.actual.route_queries ||
        !baseline_component_shape)) ||
      (!baseline &&
       (semantics.candidate_outcome_source == Phase4CandidateOutcomeSource::kNotCandidateArm ||
        !FitsU64(partition_queries) || !FitsU64(partition_work) ||
        ToU64(partition_queries) != semantics.actual.route_queries ||
        ToU64(partition_work) != semantics.actual.route_work_units ||
        !work_within_query_count(semantics.preparation_route_queries,
                                 semantics.preparation_route_work_units) ||
        !work_within_query_count(semantics.regeneration_route_queries,
                                 semantics.regeneration_route_work_units) ||
        !candidate_component_shape)) ||
      (semantics.terminal_reason == Phase4NormalizedTerminalReason::kFeasible &&
       (semantics.outcome.selected_net_count != semantics.workload_net_count ||
        semantics.outcome.no_candidate_net_count != 0 ||
        semantics.outcome.total_overuse_units != 0))) {
    return Error(Phase4PairedTrialErrorCode::kMeasurementAssociation,
                 "P4PAIR-FINALIZE-SEMANTICS-001",
                 "the arm counters, component checksums, outcome roster, or source are invalid",
                 semantics.arm);
  }
  return std::nullopt;
}

bool AccumulatePhase4BaselineColumnV1(const allocator::SequentialNegotiatedColumnRecord& column,
                                      Phase4PerNetColumnOutcomesV1* outcomes) noexcept {
  return outcomes != nullptr && AddBaselineColumn(column, outcomes);
}

bool AccumulatePhase4PreparationColumnV1(const allocator::CpuCandidatePoolColumnRecord& column,
                                         Phase4PerNetColumnOutcomesV1* outcomes) noexcept {
  return outcomes != nullptr && AddPreparationColumn(column, outcomes);
}

bool AccumulatePhase4RegenerationColumnV1(const allocator::TargetedRegenerationColumnRecord& column,
                                          Phase4PerNetColumnOutcomesV1* outcomes) noexcept {
  return outcomes != nullptr && AddRegenerationColumn(column, outcomes);
}

std::uint64_t ComputePhase4ArmReportTelemetryChecksumV1(
    const Phase4ArmReportTelemetryV1& telemetry) noexcept {
  board_ir::StableHashBuilder hash;
  hash.AddString("APGAR-PHASE4-ARM-REPORT-TELEMETRY-V1");
  hash.AddU32(telemetry.schema_version);
  hash.AddU64(telemetry.associated_semantic_checksum);
  hash.AddU64(telemetry.per_net.size());
  for (const Phase4PerNetReportV1& report : telemetry.per_net) {
    hash.AddU32(report.schema_version);
    hash.AddU64(report.net.id);
    hash.AddU32(report.net.generation);
    hash.AddU64(report.columns.requested_columns);
    hash.AddU64(report.columns.executed_route_queries);
    hash.AddU64(report.columns.admitted_candidates);
    hash.AddU64(report.columns.duplicate_candidates);
    hash.AddU64(report.columns.disconnected_columns);
    hash.AddU64(report.columns.unsupported_columns);
    hash.AddU64(report.columns.skipped_columns);
    hash.AddU64(report.columns.exact_validation_rejections);
    hash.AddU64(report.columns.other_rejections);
    hash.AddU64(report.final_pool_size);
    hash.AddU64(report.unique_geometry_signature_count);
    hash.AddU64(report.unique_resource_signature_count);
    hash.AddU64(report.candidate_pair_count);
    hash.AddU64(report.mean_resource_overlap_ppm);
    hash.AddU64(report.minimum_resource_overlap_ppm);
    hash.AddU64(report.mean_geometric_overlap_ppm);
    hash.AddU64(report.minimum_geometric_overlap_ppm);
    hash.AddByte(static_cast<std::uint8_t>(report.selected_status));
    hash.AddBool(report.selected_candidate_id.has_value());
    if (report.selected_candidate_id.has_value()) {
      hash.AddU64(report.selected_candidate_id->high);
      hash.AddU64(report.selected_candidate_id->low);
    }
    hash.AddBool(report.selected_candidate_payload_checksum.has_value());
    if (report.selected_candidate_payload_checksum.has_value()) {
      hash.AddU64(*report.selected_candidate_payload_checksum);
    }
    hash.AddBool(report.selected_candidate_metrics.has_value());
    if (report.selected_candidate_metrics.has_value()) {
      const candidates::CandidateMetrics& metrics = *report.selected_candidate_metrics;
      hash.AddU64(metrics.scalar_policy_cost);
      hash.AddU64(metrics.intrinsic_base_cost);
      hash.AddU64(metrics.orthogonal_step_count);
      hash.AddU64(metrics.diagonal_step_count);
      hash.AddU64(metrics.bend_count);
      hash.AddU64(metrics.line_primitive_count);
      hash.AddU64(metrics.via_count);
      hash.AddU64(metrics.axis_aligned_length_dbu);
      hash.AddU64(metrics.diagonal_projection_dbu);
    }
    hash.AddBool(report.pool_best_intrinsic_cost.has_value());
    if (report.pool_best_intrinsic_cost.has_value()) {
      hash.AddU64(*report.pool_best_intrinsic_cost);
    }
  }
  return hash.Finish();
}

std::optional<Phase4PairedTrialError> ValidatePhase4ArmReportTelemetryV1(
    const Phase4TrialArmSemantics& semantics, const allocator::MultiNetWorkload& workload,
    const Phase4ArmReportTelemetryV1& telemetry) noexcept {
  if (std::optional<Phase4PairedTrialError> error = ValidatePhase4TrialArmSemanticsV1(semantics);
      error.has_value()) {
    return error;
  }
  if (workload.nets().size() > kMaximumPhase4RepresentativeNetsV1 ||
      telemetry.per_net.size() > kMaximumPhase4RepresentativeNetsV1 ||
      workload.nets().size() != semantics.workload_net_count ||
      telemetry.per_net.size() != semantics.workload_net_count) {
    return Error(Phase4PairedTrialErrorCode::kMeasurementAssociation, "P4REPORT-BOUND-001",
                 "the authentic workload or telemetry roster count is invalid or out of bounds",
                 semantics.arm);
  }
  if (workload.board_content_hash() != semantics.board_content_hash ||
      workload.workload_checksum() != semantics.workload_checksum ||
      telemetry.schema_version != kPhase4ArmReportTelemetrySchemaVersion ||
      telemetry.associated_semantic_checksum == 0 ||
      telemetry.associated_semantic_checksum != semantics.semantic_checksum ||
      telemetry.telemetry_checksum == 0 ||
      telemetry.telemetry_checksum != ComputePhase4ArmReportTelemetryChecksumV1(telemetry)) {
    return Error(
        Phase4PairedTrialErrorCode::kMeasurementAssociation, "P4REPORT-AUTH-001",
        "the workload, telemetry schema, count, semantic association, or checksum is invalid",
        semantics.arm);
  }

  Wide requested = 0;
  Wide executed = 0;
  Wide admitted = 0;
  Wide rejected = 0;
  Wide final_candidates = 0;
  Wide selected = 0;
  Wide no_candidate = 0;
  Wide selected_intrinsic_cost = 0;
  for (std::size_t index = 0; index < telemetry.per_net.size(); ++index) {
    const Phase4PerNetReportV1& report = telemetry.per_net[index];
    if (report.schema_version != kPhase4PerNetReportSchemaVersion ||
        !(report.net == workload.nets()[index].request.net) ||
        (index != 0 && (!EntityRefBefore(telemetry.per_net[index - 1].net, report.net) ||
                        !EntityRefBefore(workload.nets()[index - 1].request.net,
                                         workload.nets()[index].request.net)))) {
      return Error(Phase4PairedTrialErrorCode::kMeasurementAssociation, "P4REPORT-NET-002",
                   "per-net telemetry must exactly match the sorted workload full-EntityRef roster",
                   semantics.arm);
    }
    const Phase4PerNetColumnOutcomesV1& columns = report.columns;
    const Wide terminal_columns = static_cast<Wide>(columns.admitted_candidates) +
                                  columns.duplicate_candidates + columns.disconnected_columns +
                                  columns.unsupported_columns + columns.skipped_columns +
                                  columns.exact_validation_rejections + columns.other_rejections;
    const Wide executed_and_skipped =
        static_cast<Wide>(columns.executed_route_queries) + columns.skipped_columns;
    const Wide expected_pairs =
        report.final_pool_size < 2
            ? 0
            : static_cast<Wide>(report.final_pool_size) * (report.final_pool_size - 1) / 2;
    const bool pairless_overlap =
        report.candidate_pair_count == 0 && report.mean_resource_overlap_ppm == 0 &&
        report.minimum_resource_overlap_ppm == 0 && report.mean_geometric_overlap_ppm == 0 &&
        report.minimum_geometric_overlap_ppm == 0;
    const bool paired_overlap =
        report.candidate_pair_count != 0 &&
        report.mean_resource_overlap_ppm <= kPhase4OverlapPartsPerMillion &&
        report.minimum_resource_overlap_ppm <= report.mean_resource_overlap_ppm &&
        report.mean_geometric_overlap_ppm <= kPhase4OverlapPartsPerMillion &&
        report.minimum_geometric_overlap_ppm <= report.mean_geometric_overlap_ppm;
    if (!FitsU64(terminal_columns) || ToU64(terminal_columns) != columns.requested_columns ||
        !FitsU64(executed_and_skipped) ||
        ToU64(executed_and_skipped) != columns.requested_columns || !FitsU64(expected_pairs) ||
        ToU64(expected_pairs) != report.candidate_pair_count ||
        report.unique_geometry_signature_count > report.final_pool_size ||
        report.unique_resource_signature_count > report.final_pool_size ||
        report.final_pool_size > columns.admitted_candidates ||
        (report.final_pool_size == 0 && (report.unique_geometry_signature_count != 0 ||
                                         report.unique_resource_signature_count != 0 ||
                                         report.pool_best_intrinsic_cost.has_value())) ||
        (report.final_pool_size != 0 && (report.unique_geometry_signature_count == 0 ||
                                         report.unique_resource_signature_count == 0 ||
                                         !report.pool_best_intrinsic_cost.has_value())) ||
        (report.candidate_pair_count == 0 ? !pairless_overlap : !paired_overlap)) {
      return Error(Phase4PairedTrialErrorCode::kMeasurementAssociation, "P4REPORT-CLOSURE-001",
                   "a per-net column, pool, signature, pair, or overlap partition does not close",
                   semantics.arm);
    }

    const bool selected_shape =
        report.selected_status == allocator::NetSelectionStatus::kSelected &&
        report.final_pool_size != 0 && report.selected_candidate_id.has_value() &&
        !report.selected_candidate_id->empty() &&
        report.selected_candidate_payload_checksum.has_value() &&
        report.selected_candidate_metrics.has_value() &&
        report.pool_best_intrinsic_cost.has_value() &&
        *report.pool_best_intrinsic_cost <= report.selected_candidate_metrics->intrinsic_base_cost;
    const bool no_candidate_shape =
        report.selected_status == allocator::NetSelectionStatus::kNoAdmissibleCandidate &&
        report.final_pool_size == 0 && !report.selected_candidate_id.has_value() &&
        !report.selected_candidate_payload_checksum.has_value() &&
        !report.selected_candidate_metrics.has_value() &&
        !report.pool_best_intrinsic_cost.has_value();
    if (!selected_shape && !no_candidate_shape) {
      return Error(
          Phase4PairedTrialErrorCode::kMeasurementAssociation, "P4REPORT-SELECTION-CLOSURE-001",
          "selected status, identity, metrics, pool, and best cost do not close", semantics.arm);
    }
    requested += columns.requested_columns;
    executed += columns.executed_route_queries;
    admitted += columns.admitted_candidates;
    rejected += static_cast<Wide>(columns.duplicate_candidates) + columns.disconnected_columns +
                columns.unsupported_columns + columns.skipped_columns +
                columns.exact_validation_rejections + columns.other_rejections;
    final_candidates += report.final_pool_size;
    if (selected_shape) {
      ++selected;
      selected_intrinsic_cost += report.selected_candidate_metrics->intrinsic_base_cost;
    } else {
      ++no_candidate;
    }
  }
  if (!FitsU64(requested) || ToU64(requested) != semantics.requested_columns ||
      !FitsU64(executed) || ToU64(executed) != semantics.actual.route_queries ||
      !FitsU64(admitted) || ToU64(admitted) != semantics.admitted_candidates ||
      !FitsU64(rejected) || ToU64(rejected) != semantics.rejected_columns ||
      !FitsU64(final_candidates) || ToU64(final_candidates) != semantics.final_candidate_count ||
      !FitsU64(selected) || ToU64(selected) != semantics.outcome.selected_net_count ||
      !FitsU64(no_candidate) || ToU64(no_candidate) != semantics.outcome.no_candidate_net_count ||
      !FitsU64(selected_intrinsic_cost) ||
      ToU64(selected_intrinsic_cost) != semantics.outcome.total_intrinsic_cost) {
    return Error(Phase4PairedTrialErrorCode::kMeasurementAssociation, "P4REPORT-CLOSURE-002",
                 "per-net column, pool, selection, or cost totals do not close arm semantics",
                 semantics.arm);
  }
  return std::nullopt;
}

std::uint64_t ComputePhase4ExternalAuthorityChecksumV1(
    const Phase4ExternalResourceObservation& observation) noexcept {
  board_ir::StableHashBuilder hash;
  hash.AddString("APGAR-PHASE4-EXTERNAL-AUTHORITY-V1");
  hash.AddU32(observation.schema_version);
  hash.AddByte(static_cast<std::uint8_t>(observation.authority_kind));
  hash.AddU64(observation.authority_run_identity);
  hash.AddU64(observation.controller_identity);
  hash.AddU64(observation.process_instance_identity);
  hash.AddU64(observation.associated_semantic_checksum);
  hash.AddU64(observation.configured_wall_limit_nanoseconds);
  hash.AddU64(observation.configured_address_space_limit_bytes);
  hash.AddU64(observation.configured_peak_host_limit_bytes);
  hash.AddU64(observation.outer_elapsed_nanoseconds);
  hash.AddU64(observation.peak_host_bytes);
  hash.AddI32(observation.process_exit_code);
  hash.AddBool(observation.isolated_process);
  hash.AddBool(observation.wall_authority_enforced);
  hash.AddBool(observation.memory_authority_enforced);
  hash.AddBool(observation.persistent_preparer_reused);
  hash.AddU64(observation.preparer_lifecycle.workers_started_before);
  hash.AddU64(observation.preparer_lifecycle.workers_started_after);
  hash.AddU64(observation.preparer_lifecycle.invocations_started_before);
  hash.AddU64(observation.preparer_lifecycle.invocations_started_after);
  hash.AddU64(observation.preparer_lifecycle.invocations_completed_before);
  hash.AddU64(observation.preparer_lifecycle.invocations_completed_after);
  return hash.Finish();
}

std::uint64_t ComputePhase4TrialArmArtifactChecksumV1(const Phase4TrialArmRecord& record) noexcept {
  board_ir::StableHashBuilder hash;
  hash.AddString("APGAR-PHASE4-TRIAL-ARM-ARTIFACT-V1");
  hash.AddU64(record.semantics.semantic_checksum);
  hash.AddByte(static_cast<std::uint8_t>(record.semantics.execution_order));
  hash.AddU32(record.semantics.preparation_worker_count);
  hash.AddU64(record.case_build_elapsed_nanoseconds);
  hash.AddU64(record.prepared_elapsed_nanoseconds);
  hash.AddU64(record.cold_elapsed_nanoseconds);
  hash.AddU64(record.preparer_lifecycle.workers_started_before);
  hash.AddU64(record.preparer_lifecycle.workers_started_after);
  hash.AddU64(record.preparer_lifecycle.invocations_started_before);
  hash.AddU64(record.preparer_lifecycle.invocations_started_after);
  hash.AddU64(record.preparer_lifecycle.invocations_completed_before);
  hash.AddU64(record.preparer_lifecycle.invocations_completed_after);
  hash.AddU64(record.external_observation.authority_checksum);
  return hash.Finish();
}

std::uint64_t ComputePhase4PairedTrialSemanticChecksumV1(
    const Phase4PairedTrialResult& result) noexcept {
  board_ir::StableHashBuilder hash;
  hash.AddString("APGAR-PHASE4-PAIRED-TRIAL-SEMANTIC-V1");
  hash.AddU32(result.schema_version);
  hash.AddU64(result.baseline.semantics.semantic_checksum);
  hash.AddU64(result.candidate.semantics.semantic_checksum);
  hash.AddByte(static_cast<std::uint8_t>(result.comparison));
  return hash.Finish();
}

std::uint64_t ComputePhase4PairedTrialArtifactChecksumV1(
    const Phase4PairedTrialResult& result) noexcept {
  board_ir::StableHashBuilder hash;
  hash.AddString("APGAR-PHASE4-PAIRED-TRIAL-ARTIFACT-V1");
  hash.AddU64(result.semantic_checksum);
  hash.AddByte(static_cast<std::uint8_t>(result.baseline.semantics.execution_order));
  hash.AddU64(result.baseline.artifact_checksum);
  hash.AddU64(result.candidate.artifact_checksum);
  return hash.Finish();
}

}  // namespace internal

namespace {

[[nodiscard]] bool ValidAuthorityKind(Phase4ExternalAuthorityKind kind) noexcept {
  return kind == Phase4ExternalAuthorityKind::kLinuxParentWatchdogRlimitAndWait4;
}

[[nodiscard]] bool ZeroLifecycle(const Phase4PreparerLifecycleObservation& lifecycle) noexcept {
  return lifecycle == Phase4PreparerLifecycleObservation{};
}

[[nodiscard]] std::optional<Phase4PairedTrialError> ValidateExecutionObservation(
    const Phase4TrialArmExecution& execution,
    const Phase4ExternalResourceObservation& observation) noexcept {
  const Phase4TrialArmSemantics& semantics = execution.semantics;
  if (std::optional<Phase4PairedTrialError> error =
          internal::ValidatePhase4TrialArmSemanticsV1(semantics);
      error.has_value()) {
    return error;
  }
  const bool baseline = semantics.arm == Phase4TrialArm::kSequentialBaseline;
  if (observation.schema_version != kPhase4ExternalAuthoritySchemaVersion ||
      !ValidAuthorityKind(observation.authority_kind) || observation.authority_run_identity == 0 ||
      observation.controller_identity == 0 || observation.process_instance_identity == 0 ||
      observation.process_exit_code != 0 || observation.authority_checksum == 0 ||
      observation.authority_checksum !=
          internal::ComputePhase4ExternalAuthorityChecksumV1(observation)) {
    return Error(
        Phase4PairedTrialErrorCode::kExternalAuthorityUnavailable, "P4PAIR-FINALIZE-AUTHORITY-001",
        "the external authority provenance is absent, failed, or unauthenticated", semantics.arm);
  }
  if (observation.associated_semantic_checksum != semantics.semantic_checksum) {
    return Error(Phase4PairedTrialErrorCode::kMeasurementAssociation, "P4PAIR-FINALIZE-002",
                 "the external measurement is associated with another semantic result",
                 semantics.arm, semantics.semantic_checksum,
                 observation.associated_semantic_checksum);
  }
  if (!observation.isolated_process || !observation.wall_authority_enforced ||
      !observation.memory_authority_enforced || observation.peak_host_bytes == 0 ||
      observation.configured_wall_limit_nanoseconds !=
          semantics.external_budget.maximum_cold_elapsed_nanoseconds ||
      observation.configured_address_space_limit_bytes !=
          semantics.external_budget.maximum_address_space_bytes ||
      observation.configured_peak_host_limit_bytes !=
          semantics.external_budget.maximum_peak_host_bytes) {
    return Error(Phase4PairedTrialErrorCode::kExternalAuthorityUnavailable, "P4PAIR-FINALIZE-003",
                 "isolated wall, address-space, and peak-RSS authorities must attest exact caps",
                 semantics.arm);
  }
  const bool candidate = !baseline;
  if (observation.preparer_lifecycle != execution.preparer_lifecycle) {
    return Error(
        Phase4PairedTrialErrorCode::kMeasurementAssociation, "P4PAIR-FINALIZE-LIFECYCLE-001",
        "external preparer lifecycle does not match worker-captured telemetry", semantics.arm);
  }
  const Phase4PreparerLifecycleObservation& lifecycle = execution.preparer_lifecycle;
  const Wide started_after_expected = static_cast<Wide>(lifecycle.invocations_started_before) + 1;
  const Wide completed_after_expected =
      static_cast<Wide>(lifecycle.invocations_completed_before) + 1;
  const bool valid_candidate_lifecycle =
      lifecycle.workers_started_before == semantics.preparation_worker_count &&
      lifecycle.workers_started_after == semantics.preparation_worker_count &&
      lifecycle.invocations_started_before == lifecycle.invocations_completed_before &&
      lifecycle.invocations_completed_before > 0 && FitsU64(started_after_expected) &&
      FitsU64(completed_after_expected) &&
      lifecycle.invocations_started_after == ToU64(started_after_expected) &&
      lifecycle.invocations_completed_after == ToU64(completed_after_expected);
  if (observation.persistent_preparer_reused != candidate ||
      (candidate && !valid_candidate_lifecycle) || (baseline && !ZeroLifecycle(lifecycle))) {
    return Error(Phase4PairedTrialErrorCode::kMeasurementAssociation, "P4PAIR-FINALIZE-004",
                 "preparer lifecycle evidence does not prove candidate-only persistent reuse",
                 semantics.arm);
  }
  const Phase4ExternalBudget& budget = semantics.external_budget;
  if (execution.prepared_elapsed_nanoseconds > budget.maximum_prepared_elapsed_nanoseconds ||
      execution.cold_elapsed_nanoseconds > budget.maximum_cold_elapsed_nanoseconds ||
      observation.outer_elapsed_nanoseconds > budget.maximum_cold_elapsed_nanoseconds ||
      observation.peak_host_bytes > budget.maximum_peak_host_bytes) {
    return Error(Phase4PairedTrialErrorCode::kExternalBudgetExceeded, "P4PAIR-FINALIZE-005",
                 "one or more external trial budgets were exceeded", semantics.arm);
  }
  const Wide inner_intervals = static_cast<Wide>(execution.case_build_elapsed_nanoseconds) +
                               execution.prepared_elapsed_nanoseconds;
  if (!FitsU64(inner_intervals) || ToU64(inner_intervals) > execution.cold_elapsed_nanoseconds ||
      observation.outer_elapsed_nanoseconds < execution.cold_elapsed_nanoseconds) {
    return Error(
        Phase4PairedTrialErrorCode::kMeasurementAssociation, "P4PAIR-FINALIZE-006",
        "case-build and prepared intervals must nest inside cold and authoritative outer time",
        semantics.arm);
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<Phase4PairedTrialError> ValidateFinalizedRecord(
    const Phase4TrialArmRecord& record) noexcept {
  const Phase4TrialArmExecution execution{
      .semantics = record.semantics,
      .case_build_elapsed_nanoseconds = record.case_build_elapsed_nanoseconds,
      .prepared_elapsed_nanoseconds = record.prepared_elapsed_nanoseconds,
      .cold_elapsed_nanoseconds = record.cold_elapsed_nanoseconds,
      .preparer_lifecycle = record.preparer_lifecycle,
  };
  if (std::optional<Phase4PairedTrialError> error =
          ValidateExecutionObservation(execution, record.external_observation);
      error.has_value()) {
    return error;
  }
  if (record.artifact_checksum == 0 ||
      record.artifact_checksum != internal::ComputePhase4TrialArmArtifactChecksumV1(record)) {
    return Error(Phase4PairedTrialErrorCode::kPairMismatch, "P4PAIR-ASSEMBLE-ARTIFACT-001",
                 "the finalized arm artifact checksum is absent or invalid", record.semantics.arm);
  }
  return std::nullopt;
}

}  // namespace

namespace {

struct ArmExecutionWithOptionalTelemetry {
  Phase4TrialArmExecution execution;
  std::optional<Phase4ArmReportTelemetryV1> telemetry;
};

using ArmExecutionWithOptionalTelemetryResult =
    std::variant<ArmExecutionWithOptionalTelemetry, Phase4TrialArmFailure>;

[[nodiscard]] ArmExecutionWithOptionalTelemetryResult ExecutePhase4TrialArmImpl(
    Phase4TrialArm arm, const Phase4PairedTrialSpec& spec, std::string_view imported_fixture,
    allocator::PersistentCpuCandidatePoolPreparer* candidate_preparer, bool capture_telemetry) {
  try {
    const ValidationResult validation = ValidateSpec(spec, arm);
    if (std::holds_alternative<Phase4PairedTrialError>(validation)) {
      return ArmFailure(std::get<Phase4PairedTrialError>(validation));
    }
    const ValidatedTrialSpec validated = std::get<ValidatedTrialSpec>(validation);
    if (arm == Phase4TrialArm::kReusableCandidateAllocation) {
      if (candidate_preparer == nullptr ||
          candidate_preparer->config().schema_version !=
              allocator::kPersistentCpuCandidatePoolPreparerSchemaVersion ||
          candidate_preparer->config().worker_count != spec.preparation_worker_count) {
        return ArmFailure(
            Error(Phase4PairedTrialErrorCode::kInvalidConfiguration, "P4PAIR-WORKER-002",
                  "candidate execution requires a matching persistent preparer", arm,
                  spec.preparation_worker_count,
                  candidate_preparer == nullptr ? 0 : candidate_preparer->config().worker_count));
      }
    } else if (candidate_preparer != nullptr) {
      return ArmFailure(Error(Phase4PairedTrialErrorCode::kInvalidConfiguration,
                              "P4PAIR-WORKER-003",
                              "baseline execution must not receive a candidate preparer", arm));
    }

    const Clock::time_point cold_start = Clock::now();
    allocator::PersistentCpuCandidatePoolTelemetry lifecycle_before;
    if (candidate_preparer != nullptr) {
      lifecycle_before = candidate_preparer->telemetry();
    }
    Phase4TrialArmSemantics semantics;
    Phase4ArmReportTelemetryV1 telemetry;
    std::uint64_t case_build_elapsed = 0;
    std::uint64_t prepared_elapsed = 0;
    {
      const Clock::time_point case_start = Clock::now();
      Phase4RepresentativeCaseResult case_result =
          BuildPhase4RepresentativeCaseV1(spec.case_id, imported_fixture, spec.corpus_limits);
      case_build_elapsed = ElapsedNanoseconds(case_start, Clock::now());
      if (std::holds_alternative<Phase4RepresentativeCorpusError>(case_result)) {
        Phase4RepresentativeCorpusError child =
            std::get<Phase4RepresentativeCorpusError>(std::move(case_result));
        const Phase4PairedTrialError summary =
            Error(Phase4PairedTrialErrorCode::kCaseBuild, child.invariant_id, child.detail, arm);
        return ArmFailure(summary, std::move(child));
      }
      Phase4RepresentativeCase corpus = std::get<Phase4RepresentativeCase>(std::move(case_result));
      if (!(corpus.descriptor == *validated.descriptor) ||
          corpus.workload.nets().size() != validated.descriptor->requested_net_count ||
          corpus.board.content_hash() != corpus.workload.board_content_hash()) {
        return ArmFailure(Error(
            Phase4PairedTrialErrorCode::kCaseIdentityMismatch, "P4PAIR-CASE-IDENTITY-001",
            "the built case does not match its descriptor or Board/workload association", arm));
      }
      const Clock::time_point prepared_start = Clock::now();
      auto arm_result =
          arm == Phase4TrialArm::kSequentialBaseline
              ? ExecuteBaseline(spec, std::move(corpus), validated,
                                capture_telemetry ? &telemetry : nullptr)
              : ExecuteCandidate(spec, std::move(corpus), validated, *candidate_preparer,
                                 capture_telemetry ? &telemetry : nullptr);
      prepared_elapsed = ElapsedNanoseconds(prepared_start, Clock::now());
      if (std::holds_alternative<Phase4TrialArmFailure>(arm_result)) {
        return std::get<Phase4TrialArmFailure>(std::move(arm_result));
      }
      semantics = std::get<Phase4TrialArmSemantics>(std::move(arm_result));
    }
    const std::uint64_t cold_elapsed = ElapsedNanoseconds(cold_start, Clock::now());
    allocator::PersistentCpuCandidatePoolTelemetry lifecycle_after;
    if (candidate_preparer != nullptr) {
      lifecycle_after = candidate_preparer->telemetry();
    }
    ArmExecutionWithOptionalTelemetry output{
        .execution =
            Phase4TrialArmExecution{
                .semantics = std::move(semantics),
                .case_build_elapsed_nanoseconds = case_build_elapsed,
                .prepared_elapsed_nanoseconds = prepared_elapsed,
                .cold_elapsed_nanoseconds = cold_elapsed,
                .preparer_lifecycle = LifecycleObservation(lifecycle_before, lifecycle_after),
            },
        .telemetry = std::nullopt,
    };
    if (capture_telemetry) {
      output.telemetry = std::move(telemetry);
    }
    return output;
  } catch (const std::bad_alloc&) {
    return ArmFailure(Error(Phase4PairedTrialErrorCode::kResourceExhausted, "P4PAIR-HOST-001",
                            "host allocation failed while executing the trial arm", arm));
  } catch (const std::length_error&) {
    return ArmFailure(Error(Phase4PairedTrialErrorCode::kResourceExhausted, "P4PAIR-HOST-002",
                            "host container length failed while executing the trial arm", arm));
  } catch (const std::exception&) {
    return ArmFailure(Error(Phase4PairedTrialErrorCode::kInternalInvariant, "P4PAIR-HOST-003",
                            "an unexpected standard exception escaped a contender", arm));
  } catch (...) {
    return ArmFailure(Error(Phase4PairedTrialErrorCode::kInternalInvariant, "P4PAIR-HOST-004",
                            "an unexpected non-standard exception escaped a contender", arm));
  }
}

}  // namespace

Phase4TrialArmExecutionResult ExecutePhase4TrialArmV1(
    Phase4TrialArm arm, const Phase4PairedTrialSpec& spec, std::string_view imported_fixture,
    allocator::PersistentCpuCandidatePoolPreparer* candidate_preparer) {
  ArmExecutionWithOptionalTelemetryResult result =
      ExecutePhase4TrialArmImpl(arm, spec, imported_fixture, candidate_preparer, false);
  if (std::holds_alternative<Phase4TrialArmFailure>(result)) {
    return std::get<Phase4TrialArmFailure>(std::move(result));
  }
  return std::get<ArmExecutionWithOptionalTelemetry>(std::move(result)).execution;
}

Phase4TrialArmDiagnosticExecutionResultV1 ExecutePhase4TrialArmDiagnosticV1(
    Phase4TrialArm arm, const Phase4PairedTrialSpec& spec, std::string_view imported_fixture,
    allocator::PersistentCpuCandidatePoolPreparer* candidate_preparer) {
  ArmExecutionWithOptionalTelemetryResult result =
      ExecutePhase4TrialArmImpl(arm, spec, imported_fixture, candidate_preparer, true);
  if (std::holds_alternative<Phase4TrialArmFailure>(result)) {
    return std::get<Phase4TrialArmFailure>(std::move(result));
  }
  ArmExecutionWithOptionalTelemetry output =
      std::get<ArmExecutionWithOptionalTelemetry>(std::move(result));
  if (!output.telemetry.has_value()) {
    return ArmFailure(Error(Phase4PairedTrialErrorCode::kInternalInvariant, "P4REPORT-INTERNAL-001",
                            "diagnostic execution completed without per-net telemetry", arm));
  }
  return Phase4TrialArmDiagnosticExecutionV1{
      .semantics = std::move(output.execution.semantics),
      .telemetry = std::move(*output.telemetry),
  };
}

Phase4TrialArmRecordResult FinalizePhase4TrialArmV1(
    Phase4TrialArmExecution execution, const Phase4ExternalResourceObservation& observation) {
  if (std::optional<Phase4PairedTrialError> error =
          ValidateExecutionObservation(execution, observation);
      error.has_value()) {
    return *error;
  }
  Phase4TrialArmRecord record{
      .semantics = std::move(execution.semantics),
      .case_build_elapsed_nanoseconds = execution.case_build_elapsed_nanoseconds,
      .prepared_elapsed_nanoseconds = execution.prepared_elapsed_nanoseconds,
      .cold_elapsed_nanoseconds = execution.cold_elapsed_nanoseconds,
      .preparer_lifecycle = execution.preparer_lifecycle,
      .external_observation = observation,
  };
  record.artifact_checksum = internal::ComputePhase4TrialArmArtifactChecksumV1(record);
  return record;
}

Phase4PairedTrialAssemblyResult AssemblePhase4PairedTrialV1(Phase4TrialArmRecord baseline,
                                                            Phase4TrialArmRecord candidate) {
  if (baseline.semantics.arm != Phase4TrialArm::kSequentialBaseline ||
      candidate.semantics.arm != Phase4TrialArm::kReusableCandidateAllocation) {
    return Error(Phase4PairedTrialErrorCode::kPairMismatch, "P4PAIR-ASSEMBLE-001",
                 "the pair must contain one baseline and one candidate arm");
  }
  if (std::optional<Phase4PairedTrialError> error = ValidateFinalizedRecord(baseline);
      error.has_value()) {
    return *error;
  }
  if (std::optional<Phase4PairedTrialError> error = ValidateFinalizedRecord(candidate);
      error.has_value()) {
    return *error;
  }
  if (!SamePairIdentity(baseline.semantics, candidate.semantics)) {
    return Error(Phase4PairedTrialErrorCode::kPairMismatch, "P4PAIR-ASSEMBLE-002",
                 "the two arm records do not share one paired identity");
  }
  const Phase4ExternalResourceObservation& baseline_authority = baseline.external_observation;
  const Phase4ExternalResourceObservation& candidate_authority = candidate.external_observation;
  if (baseline_authority.authority_kind != candidate_authority.authority_kind ||
      baseline_authority.authority_run_identity != candidate_authority.authority_run_identity ||
      baseline_authority.controller_identity != candidate_authority.controller_identity ||
      baseline_authority.process_instance_identity ==
          candidate_authority.process_instance_identity) {
    return Error(Phase4PairedTrialErrorCode::kPairMismatch, "P4PAIR-ASSEMBLE-AUTHORITY-001",
                 "one controller run must attest two distinct arm process instances");
  }
  Phase4PairedTrialResult result{
      .baseline = std::move(baseline),
      .candidate = std::move(candidate),
  };
  result.comparison =
      Compare(result.baseline.semantics.outcome, result.candidate.semantics.outcome);
  result.semantic_checksum = internal::ComputePhase4PairedTrialSemanticChecksumV1(result);
  result.artifact_checksum = internal::ComputePhase4PairedTrialArtifactChecksumV1(result);
  return result;
}

}  // namespace apgar::benchmark
