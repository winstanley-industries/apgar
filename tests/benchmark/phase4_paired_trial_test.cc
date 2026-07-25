#include "apgar/benchmark/phase4_paired_trial.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

#include "apgar/allocator/cpu_candidate_pool_preparation.h"
#include "apgar/benchmark/phase4_operational_artifact.h"
#include "apgar/benchmark/phase4_trial_harness.h"
#include "apgar/tooling/runfiles.h"
#include "src/allocator/cpu_candidate_allocation_session_internal.h"
#include "src/allocator/sequential_negotiated_baseline_internal.h"
#include "src/benchmark/phase4_paired_trial_internal.h"
#include "tests/support/google_test.h"

namespace apgar::benchmark {
namespace {

static_assert(!std::is_convertible_v<Phase4TrialArmDiagnosticExecutionV1, Phase4TrialArmExecution>);

template <typename Value, typename Error>
[[nodiscard]] Value ValueOf(std::variant<Value, Error> result) {
  std::string failure;
  if (std::holds_alternative<Error>(result)) {
    if constexpr (std::is_same_v<Error, Phase4TrialArmFailure>) {
      failure = std::string(std::get<Error>(result).summary.invariant_id) + ": " +
                std::string(std::get<Error>(result).summary.detail);
    } else {
      failure = std::string(std::get<Error>(result).invariant_id) + ": " +
                std::string(std::get<Error>(result).detail);
    }
  }
  EXPECT_TRUE(std::holds_alternative<Value>(result)) << failure;
  if (!std::holds_alternative<Value>(result)) {
    std::abort();
  }
  return std::get<Value>(std::move(result));
}

template <typename Value, typename Error>
[[nodiscard]] Error ErrorOf(std::variant<Value, Error> result) {
  EXPECT_TRUE(std::holds_alternative<Error>(result));
  if (!std::holds_alternative<Error>(result)) {
    std::abort();
  }
  return std::get<Error>(std::move(result));
}

[[nodiscard]] Phase4PairedTrialError SummaryOf(Phase4TrialArmExecutionResult result) {
  Phase4TrialArmFailure failure = ErrorOf<Phase4TrialArmExecution>(std::move(result));
  return failure.summary;
}

[[nodiscard]] Phase4PairedTrialSpec Spec(Phase4TrialOrder order = Phase4TrialOrder::kBaselineFirst,
                                         std::uint32_t case_id = 100, std::uint64_t net_count = 6) {
  constexpr std::uint64_t pool_size = 4;
  constexpr std::uint64_t epochs = 2;
  const std::uint64_t columns_per_epoch = net_count;
  constexpr std::uint64_t sweeps = 6;
  constexpr std::uint64_t seed = 0xa911'0400'0001ULL;

  Phase4PairedTrialSpec spec;
  spec.case_id = case_id;
  spec.requested_pool_size = pool_size;
  spec.repetition_index = 7;
  spec.root_seed = seed;
  spec.execution_order = order;
  spec.preparation_worker_count = 1;
  spec.external_budget = Phase4ExternalBudget{
      .maximum_prepared_elapsed_nanoseconds = 3'600'000'000'000ULL,
      .maximum_cold_elapsed_nanoseconds = 3'600'000'000'000ULL,
      .maximum_address_space_bytes = 64ULL * 1024ULL * 1024ULL * 1024ULL,
      .maximum_peak_host_bytes = 64ULL * 1024ULL * 1024ULL * 1024ULL,
  };

  spec.baseline_config.deterministic_seed = seed;
  spec.baseline_config.maximum_sweeps = sweeps;
  spec.baseline_config.price_config.maximum_iterations = 100;
  spec.baseline_config.limits.maximum_nets = net_count;
  spec.baseline_config.limits.maximum_route_queries = net_count * sweeps;
  spec.baseline_config.limits.maximum_total_route_work_units =
      net_count * sweeps * spec.baseline_config.route_limits.maximum_work_units;

  spec.preparation_config.requested_candidates_per_net = pool_size;
  spec.preparation_config.deterministic_seed = seed;
  spec.preparation_config.route_limits = spec.baseline_config.route_limits;
  spec.preparation_config.limits.maximum_nets = net_count;
  spec.preparation_config.limits.maximum_route_queries = net_count * pool_size;
  spec.preparation_config.limits.maximum_total_route_work_units =
      net_count * pool_size * spec.baseline_config.route_limits.maximum_work_units;
  spec.preparation_config.limits.maximum_generated_candidate_bytes =
      128ULL * 1024ULL * 1024ULL * 1024ULL;
  const std::uint64_t retained_capacity = net_count * 8;
  spec.preparation_config.store_config = candidates::CandidateStoreConfig{
      .maximum_candidates_per_net = 8,
      .maximum_candidate_bytes_per_net = 64U * 1024U * 1024U,
      .maximum_rejection_records = 4U * retained_capacity + 2U * net_count,
      .maximum_rejection_items_per_transaction = retained_capacity,
      .maximum_admission_items_per_transaction = retained_capacity,
      .maximum_admission_input_bytes_per_transaction = 8ULL * 1024ULL * 1024ULL * 1024ULL,
      .maximum_admission_work_units_per_transaction = 1'000'000'000'000ULL,
      .maximum_pin_lease_items_per_transaction = retained_capacity,
      .maximum_expected_pools_per_invocation = net_count,
      .maximum_expected_candidates_per_invocation = retained_capacity,
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
  session.regeneration_execution_config.deterministic_seed = seed;
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
          .maximum_selection_rounds = 5,
      },
  };
  session.multi_world_config.maximum_worlds = 2;
  session.multi_world_config.maximum_total_selection_rounds = 6;
  session.multi_world_config.maximum_retained_worlds = 2;
  session.limits.maximum_epoch_records = epochs;
  session.limits.maximum_total_requested_columns = epochs * columns_per_epoch;
  session.limits.maximum_total_route_queries = epochs * columns_per_epoch;
  session.limits.maximum_total_route_work_units =
      epochs * columns_per_epoch * spec.baseline_config.route_limits.maximum_work_units;
  return spec;
}

[[nodiscard]] Phase4PairedTrialSpec SpecV2(
    Phase4TrialOrder order = Phase4TrialOrder::kBaselineFirst, std::uint32_t case_id = 10'100,
    std::uint64_t net_count = 6) {
  Phase4PairedTrialSpec spec = Spec(order, case_id, net_count);
  spec.baseline_config.price_config.present_step_per_overuse_unit =
      internal::kPhase4CorpusV2ProtocolV1PresentStepPerOveruseUnit;
  spec.baseline_config.price_config.history_step_per_overuse_unit =
      internal::kPhase4CorpusV2ProtocolV1HistoryStepPerOveruseUnit;
  spec.candidate_session_config.price_config = spec.baseline_config.price_config;
  spec.candidate_session_config.regeneration_plan_config.maximum_columns_per_net = 2;
  return spec;
}

[[nodiscard]] Phase4PairedTrialSpec PortalCalibrationSpecV2() {
  Phase4CanonicalCellConfig cell;
  cell.case_id = 10'200;
  cell.requested_pool_size = 8;
  cell.preparation_worker_count = 4;
  cell.repetitions = 20;
  cell.maximum_setup_elapsed_nanoseconds = 60'000'000'000ULL;
  cell.external_budget = {
      .maximum_prepared_elapsed_nanoseconds = 120'000'000'000ULL,
      .maximum_cold_elapsed_nanoseconds = 180'000'000'000ULL,
      .maximum_address_space_bytes = 64ULL * 1024ULL * 1024ULL * 1024ULL,
      .maximum_peak_host_bytes = 32ULL * 1024ULL * 1024ULL * 1024ULL,
  };
  return ValueOf<Phase4PairedTrialSpec>(
      BuildPhase4CanonicalTrialSpecForCorpusV2(cell, 0, Phase4TrialOrder::kBaselineFirst));
}

[[nodiscard]] std::unique_ptr<allocator::PersistentCpuCandidatePoolPreparer> Preparer(
    std::uint32_t workers = 1) {
  return ValueOf<std::unique_ptr<allocator::PersistentCpuCandidatePoolPreparer>>(
      allocator::CreatePersistentCpuCandidatePoolPreparer({.worker_count = workers}));
}

[[nodiscard]] std::string ReadImportedFixture() {
  return tooling::ReadRunfile("tests/fixtures/phase4_supported_multinet_v1.kicad_pcb")
      .value_or(std::string{});
}

[[nodiscard]] Phase4PerNetColumnOutcomesV1 BaselineColumnsFor(
    const allocator::SequentialNegotiatedBaselineResult& result, board_ir::EntityRef net) {
  Phase4PerNetColumnOutcomesV1 expected;
  for (const allocator::SequentialNegotiatedColumnRecord& column : result.columns()) {
    if (!(column.net == net)) {
      continue;
    }
    ++expected.requested_columns;
    ++expected.executed_route_queries;
    switch (column.outcome) {
      case allocator::SequentialNegotiatedColumnOutcome::kAdmitted:
        ++expected.admitted_candidates;
        break;
      case allocator::SequentialNegotiatedColumnOutcome::kRouteDisconnected:
        ++expected.disconnected_columns;
        break;
      case allocator::SequentialNegotiatedColumnOutcome::kRouteUnsupported:
        ++expected.unsupported_columns;
        break;
      case allocator::SequentialNegotiatedColumnOutcome::kBuildRejected:
      case allocator::SequentialNegotiatedColumnOutcome::kAdmissionRejected:
        if (column.rejection.has_value() &&
            column.rejection->code == candidates::CandidateRejectionCode::kExactValidation) {
          ++expected.exact_validation_rejections;
        } else {
          ++expected.other_rejections;
        }
        break;
    }
  }
  return expected;
}

void AddPreparationColumnIndependently(const allocator::CpuCandidatePoolColumnRecord& column,
                                       Phase4PerNetColumnOutcomesV1* expected) {
  ++expected->requested_columns;
  const bool skipped =
      column.outcome == allocator::CpuCandidatePoolColumnOutcome::kSkippedAfterDisconnectedProof ||
      column.outcome == allocator::CpuCandidatePoolColumnOutcome::kSkippedAfterUnsupportedProof;
  if (!skipped) {
    ++expected->executed_route_queries;
  }
  switch (column.outcome) {
    case allocator::CpuCandidatePoolColumnOutcome::kAdmitted:
      ++expected->admitted_candidates;
      break;
    case allocator::CpuCandidatePoolColumnOutcome::kDuplicate:
      ++expected->duplicate_candidates;
      break;
    case allocator::CpuCandidatePoolColumnOutcome::kRouteDisconnected:
      ++expected->disconnected_columns;
      break;
    case allocator::CpuCandidatePoolColumnOutcome::kRouteUnsupported:
      ++expected->unsupported_columns;
      break;
    case allocator::CpuCandidatePoolColumnOutcome::kSkippedAfterDisconnectedProof:
    case allocator::CpuCandidatePoolColumnOutcome::kSkippedAfterUnsupportedProof:
      ++expected->skipped_columns;
      break;
    case allocator::CpuCandidatePoolColumnOutcome::kBuildRejected:
    case allocator::CpuCandidatePoolColumnOutcome::kAdmissionRejected:
      if (column.rejection_code == candidates::CandidateRejectionCode::kExactValidation) {
        ++expected->exact_validation_rejections;
      } else {
        ++expected->other_rejections;
      }
      break;
  }
}

void AddRegenerationColumnIndependently(const allocator::TargetedRegenerationColumnRecord& column,
                                        Phase4PerNetColumnOutcomesV1* expected) {
  ++expected->requested_columns;
  ++expected->executed_route_queries;
  switch (column.outcome) {
    case allocator::TargetedRegenerationColumnOutcome::kAdmitted:
      ++expected->admitted_candidates;
      break;
    case allocator::TargetedRegenerationColumnOutcome::kDuplicate:
      ++expected->duplicate_candidates;
      break;
    case allocator::TargetedRegenerationColumnOutcome::kRouteDisconnected:
      ++expected->disconnected_columns;
      break;
    case allocator::TargetedRegenerationColumnOutcome::kRouteUnsupported:
      ++expected->unsupported_columns;
      break;
    case allocator::TargetedRegenerationColumnOutcome::kBuildRejected:
    case allocator::TargetedRegenerationColumnOutcome::kAdmissionRejected:
      if (column.rejection_code == candidates::CandidateRejectionCode::kExactValidation) {
        ++expected->exact_validation_rejections;
      } else {
        ++expected->other_rejections;
      }
      break;
    case allocator::TargetedRegenerationColumnOutcome::kQueryInFlight:
    case allocator::TargetedRegenerationColumnOutcome::kBuildInFlight:
    case allocator::TargetedRegenerationColumnOutcome::kGeneratedPendingPublication:
    case allocator::TargetedRegenerationColumnOutcome::kRejectionEvidenceInFlight:
    case allocator::TargetedRegenerationColumnOutcome::
        kPublicationCommittedOutcomeCorrelationPending:
      ADD_FAILURE() << "successful source session retained an in-flight regeneration column";
      break;
  }
}

[[nodiscard]] Phase4PerNetColumnOutcomesV1 CandidateColumnsFor(
    const allocator::CpuCandidateAllocationSession& session, board_ir::EntityRef net) {
  Phase4PerNetColumnOutcomesV1 expected;
  for (const allocator::CpuCandidatePoolColumnRecord& column : session.preparation().columns()) {
    if (column.net == net) {
      AddPreparationColumnIndependently(column, &expected);
    }
  }
  for (const allocator::CpuCandidateAllocationEpochRecord& epoch : session.epochs()) {
    for (const allocator::TargetedRegenerationColumnRecord& column : epoch.columns) {
      if (column.net == net) {
        AddRegenerationColumnIndependently(column, &expected);
      }
    }
  }
  return expected;
}

[[nodiscard]] const allocator::OneWorldAllocation& ChosenWorld(
    const allocator::CpuCandidateAllocationSession& session) {
  if (const std::optional<std::uint64_t> preferred =
          session.final_multi_world().preferred_world_identity();
      preferred.has_value()) {
    const auto& retained = session.final_multi_world().retained_worlds();
    const auto selected =
        std::find_if(retained.begin(), retained.end(),
                     [preferred](const auto& world) { return world.world_identity == *preferred; });
    EXPECT_NE(selected, retained.end());
    if (selected != retained.end()) {
      return selected->world;
    }
  }
  return session.final_single_world();
}

void ExpectReportMatchesPoolAndSelection(const Phase4PerNetReportV1& report,
                                         const allocator::CandidatePool& pool,
                                         const allocator::NetSelection& selection) {
  ASSERT_EQ(report.net, pool.net);
  ASSERT_EQ(report.net, selection.net);
  ASSERT_EQ(report.final_pool_size, pool.candidates.size());
  std::vector<candidates::CandidateSignature> geometry_signatures;
  std::vector<candidates::CandidateSignature> resource_signatures;
  std::optional<std::uint64_t> best_cost;
  for (const candidates::StoredCandidate& candidate : pool.candidates) {
    ASSERT_NE(candidate, nullptr);
    geometry_signatures.push_back(candidate->data().geometry_signature);
    resource_signatures.push_back(candidate->data().resource_signature);
    const std::uint64_t cost = candidate->data().metrics.intrinsic_base_cost;
    if (!best_cost.has_value() || cost < *best_cost) {
      best_cost = cost;
    }
  }
  std::sort(geometry_signatures.begin(), geometry_signatures.end());
  geometry_signatures.erase(std::unique(geometry_signatures.begin(), geometry_signatures.end()),
                            geometry_signatures.end());
  std::sort(resource_signatures.begin(), resource_signatures.end());
  resource_signatures.erase(std::unique(resource_signatures.begin(), resource_signatures.end()),
                            resource_signatures.end());
  EXPECT_EQ(report.unique_geometry_signature_count, geometry_signatures.size());
  EXPECT_EQ(report.unique_resource_signature_count, resource_signatures.size());
  EXPECT_EQ(report.pool_best_intrinsic_cost, best_cost);

  std::uint64_t pairs = 0;
  unsigned __int128 resource_sum = 0;
  unsigned __int128 geometric_sum = 0;
  std::uint64_t resource_minimum = kPhase4OverlapPartsPerMillion;
  std::uint64_t geometric_minimum = kPhase4OverlapPartsPerMillion;
  for (std::size_t left = 0; left < pool.candidates.size(); ++left) {
    for (std::size_t right = left + 1; right < pool.candidates.size(); ++right) {
      const std::optional<std::uint64_t> resource =
          candidates::ResourceJaccardOverlapPpmV1(*pool.candidates[left], *pool.candidates[right]);
      const std::optional<std::uint64_t> geometric =
          candidates::GeometricOverlapRatioPpmV1(*pool.candidates[left], *pool.candidates[right]);
      ASSERT_TRUE(resource.has_value());
      ASSERT_TRUE(geometric.has_value());
      ++pairs;
      resource_sum += *resource;
      geometric_sum += *geometric;
      resource_minimum = std::min(resource_minimum, *resource);
      geometric_minimum = std::min(geometric_minimum, *geometric);
    }
  }
  EXPECT_EQ(report.candidate_pair_count, pairs);
  if (pairs == 0) {
    EXPECT_EQ(report.mean_resource_overlap_ppm, 0U);
    EXPECT_EQ(report.minimum_resource_overlap_ppm, 0U);
    EXPECT_EQ(report.mean_geometric_overlap_ppm, 0U);
    EXPECT_EQ(report.minimum_geometric_overlap_ppm, 0U);
  } else {
    EXPECT_EQ(report.mean_resource_overlap_ppm,
              static_cast<std::uint64_t>((resource_sum + pairs / 2) / pairs));
    EXPECT_EQ(report.minimum_resource_overlap_ppm, resource_minimum);
    EXPECT_EQ(report.mean_geometric_overlap_ppm,
              static_cast<std::uint64_t>((geometric_sum + pairs / 2) / pairs));
    EXPECT_EQ(report.minimum_geometric_overlap_ppm, geometric_minimum);
  }

  EXPECT_EQ(report.selected_status, selection.status);
  EXPECT_EQ(report.selected_candidate_id, selection.candidate_id);
  EXPECT_EQ(report.selected_candidate_payload_checksum, selection.candidate_payload_checksum);
  if (selection.status == allocator::NetSelectionStatus::kSelected) {
    ASSERT_NE(selection.candidate, nullptr);
    EXPECT_EQ(report.selected_candidate_metrics, selection.candidate->data().metrics);
  } else {
    EXPECT_FALSE(report.selected_candidate_metrics.has_value());
  }
}

[[nodiscard]] allocator::CpuCandidateAllocationSession ExecuteCandidateSessionForSources(
    const Phase4PairedTrialSpec& spec,
    Phase4RepresentativeCorpusAuthority authority = Phase4RepresentativeCorpusAuthority::kV1) {
  Phase4RepresentativeCase corpus = ValueOf<Phase4RepresentativeCase>(
      BuildPhase4RepresentativeCaseForAuthority(authority, spec.case_id, {}, spec.corpus_limits));
  std::unique_ptr<allocator::PersistentCpuCandidatePoolPreparer> preparer =
      Preparer(spec.preparation_worker_count);
  allocator::PreparedCpuCandidatePools prepared =
      ValueOf<allocator::PreparedCpuCandidatePools>(allocator::PrepareInitialCpuCandidatePools(
          *preparer, corpus.board, corpus.workload, spec.preparation_config));
  return ValueOf<allocator::CpuCandidateAllocationSession>(
      allocator::ExecuteCpuCandidateAllocationSession(
          allocator::kCpuCandidateAllocationSessionSchemaVersion, std::move(corpus.board),
          std::move(corpus.workload), std::move(corpus.capacities), std::move(prepared),
          spec.candidate_session_config));
}

[[nodiscard]] Phase4ArmReportTelemetryV1 GoldenTelemetry() {
  Phase4ArmReportTelemetryV1 telemetry;
  // This is a pure byte-order fixture, not a structurally valid report. Every
  // encoded integer is deliberately distinct so field swaps cannot hide.
  telemetry.schema_version = 401;
  telemetry.associated_semantic_checksum = 0x1020'3040'5060'7080ULL;
  Phase4PerNetReportV1 selected;
  selected.schema_version = 402;
  selected.net = {.id = 11, .generation = 2};
  selected.columns = Phase4PerNetColumnOutcomesV1{
      .requested_columns = 101,
      .executed_route_queries = 102,
      .admitted_candidates = 103,
      .duplicate_candidates = 104,
      .disconnected_columns = 105,
      .unsupported_columns = 106,
      .skipped_columns = 107,
      .exact_validation_rejections = 108,
      .other_rejections = 109,
  };
  selected.final_pool_size = 110;
  selected.unique_geometry_signature_count = 111;
  selected.unique_resource_signature_count = 112;
  selected.candidate_pair_count = 113;
  selected.mean_resource_overlap_ppm = 114;
  selected.minimum_resource_overlap_ppm = 115;
  selected.mean_geometric_overlap_ppm = 116;
  selected.minimum_geometric_overlap_ppm = 117;
  selected.selected_status = allocator::NetSelectionStatus::kSelected;
  selected.selected_candidate_id = candidates::CandidateId{
      .high = 0x1111'2222'3333'4444ULL,
      .low = 0x5555'6666'7777'8888ULL,
  };
  selected.selected_candidate_payload_checksum = 0x9999'aaaa'bbbb'ccccULL;
  selected.selected_candidate_metrics = candidates::CandidateMetrics{
      .scalar_policy_cost = 201,
      .intrinsic_base_cost = 202,
      .orthogonal_step_count = 203,
      .diagonal_step_count = 204,
      .bend_count = 205,
      .line_primitive_count = 206,
      .via_count = 207,
      .axis_aligned_length_dbu = 208,
      .diagonal_projection_dbu = 209,
  };
  selected.pool_best_intrinsic_cost = 210;
  Phase4PerNetReportV1 no_candidate;
  no_candidate.schema_version = 403;
  no_candidate.net = {.id = 12, .generation = 3};
  no_candidate.columns = Phase4PerNetColumnOutcomesV1{
      .requested_columns = 301,
      .executed_route_queries = 302,
      .admitted_candidates = 303,
      .duplicate_candidates = 304,
      .disconnected_columns = 305,
      .unsupported_columns = 306,
      .skipped_columns = 307,
      .exact_validation_rejections = 308,
      .other_rejections = 309,
  };
  no_candidate.final_pool_size = 310;
  no_candidate.unique_geometry_signature_count = 311;
  no_candidate.unique_resource_signature_count = 312;
  no_candidate.candidate_pair_count = 313;
  no_candidate.mean_resource_overlap_ppm = 314;
  no_candidate.minimum_resource_overlap_ppm = 315;
  no_candidate.mean_geometric_overlap_ppm = 316;
  no_candidate.minimum_geometric_overlap_ppm = 317;
  telemetry.per_net = {selected, no_candidate};
  telemetry.telemetry_checksum = internal::ComputePhase4ArmReportTelemetryChecksumV1(telemetry);
  return telemetry;
}

// Synthetic contract input only. Canonical evidence must come from the
// process controller defined by the next slice.
[[nodiscard]] Phase4ExternalResourceObservation SyntheticAuthorityForContractTest(
    const Phase4TrialArmExecution& execution) {
  const bool candidate = execution.semantics.arm == Phase4TrialArm::kReusableCandidateAllocation;
  Phase4ExternalResourceObservation observation{
      .authority_run_identity = 0x4001,
      .controller_identity = 0x4002,
      .process_instance_identity = candidate ? 0x4004U : 0x4003U,
      .associated_semantic_checksum = execution.semantics.semantic_checksum,
      .configured_wall_limit_nanoseconds =
          execution.semantics.external_budget.maximum_cold_elapsed_nanoseconds,
      .configured_address_space_limit_bytes =
          execution.semantics.external_budget.maximum_address_space_bytes,
      .configured_peak_host_limit_bytes =
          execution.semantics.external_budget.maximum_peak_host_bytes,
      .outer_elapsed_nanoseconds = execution.cold_elapsed_nanoseconds,
      .peak_host_bytes = 1,
      .isolated_process = true,
      .wall_authority_enforced = true,
      .memory_authority_enforced = true,
      .persistent_preparer_reused = candidate,
      .preparer_lifecycle = execution.preparer_lifecycle,
      .authority_checksum = 0,
  };
  observation.authority_checksum = internal::ComputePhase4ExternalAuthorityChecksumV1(observation);
  return observation;
}

[[nodiscard]] Phase4TrialArmRecord Finalized(Phase4TrialArmExecution execution) {
  const Phase4ExternalResourceObservation observation =
      SyntheticAuthorityForContractTest(execution);
  return ValueOf<Phase4TrialArmRecord>(FinalizePhase4TrialArmV1(std::move(execution), observation));
}

void Reauthenticate(Phase4TrialArmRecord& record) {
  record.semantics.semantic_checksum =
      internal::ComputePhase4TrialArmSemanticChecksumV1(record.semantics);
  record.external_observation.associated_semantic_checksum = record.semantics.semantic_checksum;
  record.external_observation.authority_checksum =
      internal::ComputePhase4ExternalAuthorityChecksumV1(record.external_observation);
  record.artifact_checksum = internal::ComputePhase4TrialArmArtifactChecksumV1(record);
}

struct ExecutedPair {
  Phase4TrialArmRecord baseline;
  Phase4TrialArmRecord candidate;
};

[[nodiscard]] Phase4TrialArmExecution ExecuteWarmCandidate(const Phase4PairedTrialSpec& spec) {
  std::unique_ptr<allocator::PersistentCpuCandidatePoolPreparer> preparer =
      Preparer(spec.preparation_worker_count);
  // The production preparer persists across repetitions. Exercise one
  // unmeasured invocation so the measured arm can prove actual reuse.
  (void)ValueOf<Phase4TrialArmExecution>(ExecutePhase4TrialArmV1(
      Phase4TrialArm::kReusableCandidateAllocation, spec, {}, preparer.get()));
  return ValueOf<Phase4TrialArmExecution>(ExecutePhase4TrialArmV1(
      Phase4TrialArm::kReusableCandidateAllocation, spec, {}, preparer.get()));
}

[[nodiscard]] ExecutedPair ExecutePair(const Phase4PairedTrialSpec& spec) {
  Phase4TrialArmExecution baseline = ValueOf<Phase4TrialArmExecution>(
      ExecutePhase4TrialArmV1(Phase4TrialArm::kSequentialBaseline, spec, {}));
  Phase4TrialArmExecution candidate = ExecuteWarmCandidate(spec);
  return ExecutedPair{
      .baseline = Finalized(std::move(baseline)),
      .candidate = Finalized(std::move(candidate)),
  };
}

TEST(Phase4PairedTrialTest, ExecutesAndAuthenticatesEqualOpportunityPair) {
  const Phase4PairedTrialSpec spec = Spec();
  ExecutedPair arms = ExecutePair(spec);
  Phase4PairedTrialResult result = ValueOf<Phase4PairedTrialResult>(
      AssemblePhase4PairedTrialV1(std::move(arms.baseline), std::move(arms.candidate)));

  EXPECT_EQ(result.baseline.semantics.opportunity,
            (Phase4RouteOpportunity{.route_queries = 36, .route_work_units = 720'000'000}));
  EXPECT_EQ(result.candidate.semantics.opportunity, result.baseline.semantics.opportunity);
  EXPECT_LE(result.baseline.semantics.actual.route_queries, 36U);
  EXPECT_LE(result.candidate.semantics.actual.route_queries, 36U);
  EXPECT_LE(result.baseline.semantics.actual.route_work_units, 720'000'000U);
  EXPECT_LE(result.candidate.semantics.actual.route_work_units, 720'000'000U);
  EXPECT_EQ(result.baseline.semantics.root_seed, spec.root_seed);
  EXPECT_EQ(result.candidate.semantics.root_seed, spec.root_seed);
  EXPECT_NE(result.semantic_checksum, 0U);
  EXPECT_NE(result.artifact_checksum, 0U);
  EXPECT_EQ(result.semantic_checksum, 1'546'196'415'088'943'078ULL);
}

TEST(Phase4PairedTrialTest, CorpusAuthorityDispatchIsExplicitAndDisjoint) {
  EXPECT_TRUE(IsPhase4RepresentativeCorpusAuthorityValid(Phase4RepresentativeCorpusAuthority::kV1));
  EXPECT_TRUE(IsPhase4RepresentativeCorpusAuthorityValid(Phase4RepresentativeCorpusAuthority::kV2));
  EXPECT_FALSE(IsPhase4RepresentativeCorpusAuthorityValid(
      static_cast<Phase4RepresentativeCorpusAuthority>(0)));
  EXPECT_EQ(Phase4RepresentativeCorpusVersionForAuthority(Phase4RepresentativeCorpusAuthority::kV1),
            kPhase4RepresentativeCorpusVersion);
  EXPECT_EQ(Phase4RepresentativeCorpusVersionForAuthority(Phase4RepresentativeCorpusAuthority::kV2),
            kPhase4RepresentativeCorpusVersionV2);
  EXPECT_EQ(FindPhase4CaseDescriptorForAuthority(Phase4RepresentativeCorpusAuthority::kV1, 10'100),
            nullptr);
  EXPECT_EQ(FindPhase4CaseDescriptorForAuthority(Phase4RepresentativeCorpusAuthority::kV2, 100),
            nullptr);
  EXPECT_NE(
      Phase4RepresentativeCorpusChecksumForAuthority(Phase4RepresentativeCorpusAuthority::kV1),
      Phase4RepresentativeCorpusChecksumForAuthority(Phase4RepresentativeCorpusAuthority::kV2));
}

TEST(Phase4PairedTrialTest, RejectsCrossCorpusEntryPointSubstitution) {
  const Phase4PairedTrialSpec v2_spec = SpecV2();
  const Phase4PairedTrialError v1_with_v2_spec =
      SummaryOf(ExecutePhase4TrialArmV1(Phase4TrialArm::kSequentialBaseline, v2_spec, {}));
  EXPECT_EQ(v1_with_v2_spec.invariant_id, "P4PAIR-CASE-001");

  const Phase4PairedTrialError v2_with_v1_spec =
      SummaryOf(ExecutePhase4TrialArmForCorpusV2(Phase4TrialArm::kSequentialBaseline, Spec(), {}));
  EXPECT_EQ(v2_with_v1_spec.invariant_id, "P4PAIR-CASE-001");

  Phase4PairedTrialSpec v2_case_with_v1_entry_point = Spec();
  v2_case_with_v1_entry_point.case_id = 10'100;
  const Phase4PairedTrialError wrong_v1_case = SummaryOf(ExecutePhase4TrialArmV1(
      Phase4TrialArm::kSequentialBaseline, v2_case_with_v1_entry_point, {}));
  EXPECT_EQ(wrong_v1_case.invariant_id, "P4PAIR-CASE-001");

  Phase4PairedTrialSpec v1_case_with_v2_entry_point = SpecV2();
  v1_case_with_v2_entry_point.case_id = 100;
  const Phase4PairedTrialError wrong_v2_case = SummaryOf(ExecutePhase4TrialArmForCorpusV2(
      Phase4TrialArm::kSequentialBaseline, v1_case_with_v2_entry_point, {}));
  EXPECT_EQ(wrong_v2_case.invariant_id, "P4PAIR-CASE-001");

  for (const auto [case_id, net_count] : {std::pair{11'000U, 256U}, std::pair{12'000U, 1U},
                                          std::pair{13'000U, 1'024U}, std::pair{14'000U, 2U}}) {
    const Phase4PairedTrialError firewall = SummaryOf(ExecutePhase4TrialArmForCorpusV2(
        Phase4TrialArm::kSequentialBaseline,
        SpecV2(Phase4TrialOrder::kBaselineFirst, case_id, net_count), {}));
    EXPECT_EQ(firewall.invariant_id, "P4PAIR-CORPUS-V2-FIREWALL-001");
  }
}

TEST(Phase4PairedTrialTest, ExecutesCorpusV2ExactPairAndBindsStructuralAuthority) {
  const Phase4PairedTrialSpec spec = SpecV2();
  Phase4TrialArmExecution baseline = ValueOf<Phase4TrialArmExecution>(
      ExecutePhase4TrialArmForCorpusV2(Phase4TrialArm::kSequentialBaseline, spec, {}));

  std::unique_ptr<allocator::PersistentCpuCandidatePoolPreparer> preparer =
      Preparer(spec.preparation_worker_count);
  static_cast<void>(ValueOf<Phase4TrialArmExecution>(ExecutePhase4TrialArmForCorpusV2(
      Phase4TrialArm::kReusableCandidateAllocation, spec, {}, preparer.get())));
  Phase4TrialArmExecution candidate =
      ValueOf<Phase4TrialArmExecution>(ExecutePhase4TrialArmForCorpusV2(
          Phase4TrialArm::kReusableCandidateAllocation, spec, {}, preparer.get()));

  for (const Phase4TrialArmExecution* execution : {&baseline, &candidate}) {
    EXPECT_EQ(execution->semantics.corpus_version, kPhase4RepresentativeCorpusVersionV2);
    EXPECT_EQ(execution->semantics.corpus_checksum, Phase4RepresentativeCorpusChecksumV2());
    EXPECT_EQ(execution->semantics.descriptor_fingerprint,
              FingerprintPhase4CaseDescriptorV2(*FindPhase4CaseDescriptorV2(spec.case_id)));
    EXPECT_FALSE(internal::ValidatePhase4TrialArmSemanticsForAuthorityV1(
                     Phase4RepresentativeCorpusAuthority::kV2, execution->semantics)
                     .has_value());
    EXPECT_TRUE(internal::ValidatePhase4TrialArmSemanticsV1(execution->semantics).has_value());
  }

  Phase4TrialArmSemantics rehashed_substitution = baseline.semantics;
  rehashed_substitution.descriptor_fingerprint =
      FingerprintPhase4CaseDescriptorV2(*FindPhase4CaseDescriptorV2(10'101));
  rehashed_substitution.semantic_checksum =
      internal::ComputePhase4TrialArmSemanticChecksumV1(rehashed_substitution);
  EXPECT_TRUE(internal::ValidatePhase4TrialArmSemanticsForAuthorityV1(
                  Phase4RepresentativeCorpusAuthority::kV2, rehashed_substitution)
                  .has_value());

  const Phase4ExternalResourceObservation baseline_observation =
      SyntheticAuthorityForContractTest(baseline);
  EXPECT_TRUE(std::holds_alternative<Phase4PairedTrialError>(
      FinalizePhase4TrialArmV1(baseline, baseline_observation)));
}

TEST(Phase4PairedTrialTest, CorpusV2PortalCalibrationImprovesWithSoleBanReachable) {
  const Phase4PairedTrialSpec spec = PortalCalibrationSpecV2();
  std::unique_ptr<allocator::PersistentCpuCandidatePoolPreparer> preparer = Preparer(4);
  const Phase4TrialArmDiagnosticExecutionV1 baseline = ValueOf<Phase4TrialArmDiagnosticExecutionV1>(
      ExecutePhase4TrialArmDiagnosticForCorpusV2(Phase4TrialArm::kSequentialBaseline, spec, {}));
  const Phase4TrialArmDiagnosticExecutionV1 candidate =
      ValueOf<Phase4TrialArmDiagnosticExecutionV1>(ExecutePhase4TrialArmDiagnosticForCorpusV2(
          Phase4TrialArm::kReusableCandidateAllocation, spec, {}, preparer.get()));
  const Phase4TrialArmDiagnosticExecutionV1 repeated =
      ValueOf<Phase4TrialArmDiagnosticExecutionV1>(ExecutePhase4TrialArmDiagnosticForCorpusV2(
          Phase4TrialArm::kReusableCandidateAllocation, spec, {}, preparer.get()));
  EXPECT_EQ(candidate.semantics, repeated.semantics);
  EXPECT_EQ(candidate.telemetry, repeated.telemetry);
  EXPECT_EQ(candidate.semantics.outcome.selected_net_count,
            baseline.semantics.outcome.selected_net_count);
  EXPECT_EQ(baseline.semantics.outcome.selected_net_count, 64U);
  EXPECT_EQ(candidate.semantics.outcome.selected_net_count, 64U);
  EXPECT_EQ(baseline.semantics.outcome.no_candidate_net_count, 0U);
  EXPECT_EQ(candidate.semantics.outcome.no_candidate_net_count, 0U);
  EXPECT_EQ(baseline.semantics.outcome.total_overuse_units, 32U);
  EXPECT_EQ(candidate.semantics.outcome.total_overuse_units, 21U);
  Phase4PerNetColumnOutcomesV1 candidate_columns;
  for (const Phase4PerNetReportV1& report : candidate.telemetry.per_net) {
    candidate_columns.requested_columns += report.columns.requested_columns;
    candidate_columns.executed_route_queries += report.columns.executed_route_queries;
    candidate_columns.admitted_candidates += report.columns.admitted_candidates;
    candidate_columns.duplicate_candidates += report.columns.duplicate_candidates;
    candidate_columns.disconnected_columns += report.columns.disconnected_columns;
    candidate_columns.unsupported_columns += report.columns.unsupported_columns;
    candidate_columns.skipped_columns += report.columns.skipped_columns;
    candidate_columns.exact_validation_rejections += report.columns.exact_validation_rejections;
    candidate_columns.other_rejections += report.columns.other_rejections;
  }
  EXPECT_LT(candidate.semantics.outcome.total_overuse_units,
            baseline.semantics.outcome.total_overuse_units)
      << " requested=" << candidate_columns.requested_columns
      << " queries=" << candidate_columns.executed_route_queries
      << " admitted=" << candidate_columns.admitted_candidates
      << " duplicate=" << candidate_columns.duplicate_candidates
      << " disconnected=" << candidate_columns.disconnected_columns
      << " other_rejections=" << candidate_columns.other_rejections
      << " regen_queries=" << candidate.semantics.regeneration_route_queries
      << " semantic_admitted=" << candidate.semantics.admitted_candidates
      << " semantic_rejected=" << candidate.semantics.rejected_columns
      << " final_candidates=" << candidate.semantics.final_candidate_count
      << " terminal=" << static_cast<int>(candidate.semantics.terminal_reason)
      << " outcome_source=" << static_cast<int>(candidate.semantics.candidate_outcome_source);
  const auto exact_rejections = [](const Phase4TrialArmDiagnosticExecutionV1& execution) {
    std::uint64_t total = 0;
    for (const Phase4PerNetReportV1& report : execution.telemetry.per_net) {
      total += report.columns.exact_validation_rejections;
    }
    return total;
  };
  EXPECT_EQ(exact_rejections(baseline), 0U);
  EXPECT_EQ(exact_rejections(candidate), 0U);
  EXPECT_EQ(candidate.semantics.regeneration_route_queries, 128U);

  const allocator::CpuCandidateAllocationSession session =
      ExecuteCandidateSessionForSources(spec, Phase4RepresentativeCorpusAuthority::kV2);
  bool executed_hard_ban_column = false;
  for (const allocator::CpuCandidateAllocationEpochRecord& epoch : session.epochs()) {
    executed_hard_ban_column =
        executed_hard_ban_column ||
        std::ranges::any_of(epoch.columns,
                            [](const allocator::TargetedRegenerationColumnRecord& column) {
                              return column.column_index > 0 && column.route_telemetry.has_value();
                            });
  }
  EXPECT_TRUE(executed_hard_ban_column);
}

TEST(Phase4PairedTrialTest, CorpusV2DiagnosticEntryPointsRemainAuthorityBound) {
  const Phase4PairedTrialSpec spec = SpecV2();
  const Phase4RepresentativeCase corpus =
      ValueOf<Phase4RepresentativeCase>(BuildPhase4RepresentativeCaseV2(spec.case_id, {}));
  constexpr std::string_view commit = "0123456789abcdef0123456789abcdef01234567";

  const Phase4TrialArmDiagnosticExecutionV1 diagnostic =
      ValueOf<Phase4TrialArmDiagnosticExecutionV1>(ExecutePhase4TrialArmDiagnosticForCorpusV2(
          Phase4TrialArm::kSequentialBaseline, spec, {}));
  EXPECT_FALSE(internal::ValidatePhase4ArmReportTelemetryForAuthorityV1(
                   Phase4RepresentativeCorpusAuthority::kV2, diagnostic.semantics, corpus.workload,
                   diagnostic.telemetry)
                   .has_value());
  EXPECT_TRUE(internal::ValidatePhase4ArmReportTelemetryV1(diagnostic.semantics, corpus.workload,
                                                           diagnostic.telemetry)
                  .has_value());

  const Phase4TrialArmWithSameRunTelemetryExecutionV1 same_run =
      ValueOf<Phase4TrialArmWithSameRunTelemetryExecutionV1>(
          ExecutePhase4TrialArmWithSameRunTelemetryForCorpusV2(Phase4TrialArm::kSequentialBaseline,
                                                               spec, {}));
  EXPECT_FALSE(internal::ValidatePhase4SameRunArmDecisionTelemetryForAuthorityV1(
                   Phase4RepresentativeCorpusAuthority::kV2, same_run.execution.semantics,
                   corpus.workload, same_run.telemetry)
                   .has_value());

  const Phase4TrialArmOperationalProfileV1 operational =
      ValueOf<Phase4TrialArmOperationalProfileV1>(
          ExecutePhase4TrialArmOperationalProfileForCorpusV2(Phase4TrialArm::kSequentialBaseline,
                                                             spec, {}));
  EXPECT_FALSE(internal::ValidatePhase4TrialArmOperationalProfileForAuthorityV1(
                   Phase4RepresentativeCorpusAuthority::kV2, operational)
                   .has_value());
  EXPECT_TRUE(
      SerializePhase4OperationalProfileWorkerJsonForCorpusV2(operational, commit, true, false)
          .has_value());
  EXPECT_FALSE(
      SerializePhase4OperationalProfileWorkerJsonV1(operational, commit, true, false).has_value());

  const Phase4TrialArmReplayAuthorityV1 replay =
      ValueOf<Phase4TrialArmReplayAuthorityV1>(ExecutePhase4TrialArmReplayAuthorityForCorpusV2(
          Phase4TrialArm::kSequentialBaseline, spec, {}));
  EXPECT_FALSE(internal::ValidatePhase4TrialArmReplayAuthorityForAuthorityV1(
                   Phase4RepresentativeCorpusAuthority::kV2, replay)
                   .has_value());
  EXPECT_TRUE(
      SerializePhase4ReplayAuthorityWorkerJsonForCorpusV2(replay, commit, true, false).has_value());
  EXPECT_FALSE(SerializePhase4ReplayAuthorityWorkerJsonV1(replay, commit, true, false).has_value());

  std::unique_ptr<allocator::PersistentCpuCandidatePoolPreparer> preparer =
      Preparer(spec.preparation_worker_count);
  const Phase4CandidatePoolSnapshotExecutionV1 snapshot =
      ValueOf<Phase4CandidatePoolSnapshotExecutionV1>(
          ExecutePhase4CandidatePoolSnapshotForCorpusV2(spec, {}, preparer.get()));
  EXPECT_EQ(snapshot.semantics.corpus_version, kPhase4RepresentativeCorpusVersionV2);
  EXPECT_FALSE(internal::ValidatePhase4ArmReportTelemetryForAuthorityV1(
                   Phase4RepresentativeCorpusAuthority::kV2, snapshot.semantics, corpus.workload,
                   snapshot.telemetry)
                   .has_value());
}

TEST(Phase4PairedTrialTest, RejectsOneUnitQueryOpportunityMismatchBeforeExecution) {
  Phase4PairedTrialSpec spec = Spec();
  ++spec.candidate_session_config.regeneration_plan_config.maximum_total_columns;
  spec.candidate_session_config.regeneration_plan_config.maximum_columns_per_net = 2;
  const Phase4PairedTrialError error =
      SummaryOf(ExecutePhase4TrialArmV1(Phase4TrialArm::kSequentialBaseline, spec, {}));
  EXPECT_EQ(error.code, Phase4PairedTrialErrorCode::kUnequalBudget);
  EXPECT_EQ(error.invariant_id, "P4PAIR-QUERY-001");
}

TEST(Phase4PairedTrialTest, RejectsStructurallyUnreachableRegenerationOpportunity) {
  Phase4PairedTrialSpec spec = Spec();
  spec.candidate_session_config.regeneration_plan_config.maximum_target_nets = 1;
  const Phase4PairedTrialError error =
      SummaryOf(ExecutePhase4TrialArmV1(Phase4TrialArm::kSequentialBaseline, spec, {}));
  EXPECT_EQ(error.code, Phase4PairedTrialErrorCode::kUnequalBudget);
  EXPECT_EQ(error.invariant_id, "P4PAIR-PLAN-SHAPE-001");
}

TEST(Phase4PairedTrialTest, RejectsUnknownArmAndExecutionOrderBeforeDereference) {
  {
    const Phase4PairedTrialError error =
        SummaryOf(ExecutePhase4TrialArmV1(static_cast<Phase4TrialArm>(255), Spec(), {}));
    EXPECT_EQ(error.invariant_id, "P4PAIR-ENUM-001");
  }
  {
    Phase4PairedTrialSpec spec = Spec();
    spec.execution_order = static_cast<Phase4TrialOrder>(255);
    const Phase4PairedTrialError error =
        SummaryOf(ExecutePhase4TrialArmV1(Phase4TrialArm::kSequentialBaseline, spec, {}));
    EXPECT_EQ(error.invariant_id, "P4PAIR-ENUM-001");
  }
}

TEST(Phase4PairedTrialTest, RejectsFrozenV1BudgetPreimageAsAnExecutionSpec) {
  Phase4PairedTrialSpec historical = Spec();
  historical.candidate_session_config.schema_version =
      allocator::kCpuCandidateAllocationSessionSchemaVersionV3;
  const Phase4PairedTrialError error =
      SummaryOf(ExecutePhase4TrialArmV1(Phase4TrialArm::kSequentialBaseline, historical, {}));
  EXPECT_EQ(error.code, Phase4PairedTrialErrorCode::kUnsupportedSchema);
  EXPECT_EQ(error.invariant_id, "P4PAIR-SCHEMA-001");
}

TEST(Phase4PairedTrialTest, RejectsSeedAndStoppingLineageMismatch) {
  {
    Phase4PairedTrialSpec spec = Spec();
    ++spec.preparation_config.deterministic_seed;
    const Phase4PairedTrialError error =
        SummaryOf(ExecutePhase4TrialArmV1(Phase4TrialArm::kSequentialBaseline, spec, {}));
    EXPECT_EQ(error.invariant_id, "P4PAIR-SEED-001");
  }
  {
    Phase4PairedTrialSpec spec = Spec();
    ++spec.baseline_config.maximum_sweeps;
    spec.baseline_config.limits.maximum_route_queries += 6;
    spec.baseline_config.limits.maximum_total_route_work_units +=
        6 * spec.baseline_config.route_limits.maximum_work_units;
    spec.candidate_session_config.regeneration_plan_config.maximum_total_columns += 3;
    spec.candidate_session_config.regeneration_plan_config.maximum_columns_per_net = 2;
    spec.candidate_session_config.regeneration_execution_config.maximum_route_queries += 3;
    spec.candidate_session_config.regeneration_execution_config.maximum_total_route_work_units +=
        3 * spec.baseline_config.route_limits.maximum_work_units;
    spec.candidate_session_config.limits.maximum_total_requested_columns += 6;
    spec.candidate_session_config.limits.maximum_total_route_queries += 6;
    spec.candidate_session_config.limits.maximum_total_route_work_units +=
        6 * spec.baseline_config.route_limits.maximum_work_units;
    const Phase4PairedTrialError error =
        SummaryOf(ExecutePhase4TrialArmV1(Phase4TrialArm::kSequentialBaseline, spec, {}));
    EXPECT_EQ(error.invariant_id, "P4PAIR-STOP-003");
  }
}

TEST(Phase4PairedTrialTest, RejectsUnsupportedQueryShapePoolsWithoutClamping) {
  for (const std::uint32_t pool_size : {1U, 1024U}) {
    Phase4PairedTrialSpec spec = Spec();
    spec.case_id = pool_size == 1 ? 2'001 : 2'000;
    spec.requested_pool_size = pool_size;
    spec.preparation_config.requested_candidates_per_net = pool_size;
    const Phase4PairedTrialError error =
        SummaryOf(ExecutePhase4TrialArmV1(Phase4TrialArm::kSequentialBaseline, spec, {}));
    EXPECT_EQ(error.invariant_id, "P4PAIR-POOL-001");
  }
}

TEST(Phase4PairedTrialTest, PreservesTypedCorpusAndPreparationFailures) {
  {
    Phase4PairedTrialSpec spec = Spec();
    spec.corpus_limits.maximum_nets = 5;
    Phase4TrialArmFailure failure = ErrorOf<Phase4TrialArmExecution>(
        ExecutePhase4TrialArmV1(Phase4TrialArm::kSequentialBaseline, spec, {}));
    ASSERT_TRUE(std::holds_alternative<Phase4RepresentativeCorpusError>(failure.payload));
    const auto& child = std::get<Phase4RepresentativeCorpusError>(failure.payload);
    EXPECT_EQ(failure.summary.invariant_id, child.invariant_id);
    EXPECT_EQ(child.code, Phase4RepresentativeCorpusErrorCode::kInputBoundExceeded);
    EXPECT_EQ(child.requested_case_id, spec.case_id);
  }
  {
    Phase4PairedTrialSpec spec = Spec();
    spec.preparation_config.store_config.maximum_candidates_per_net = 0;
    std::unique_ptr<allocator::PersistentCpuCandidatePoolPreparer> preparer = Preparer();
    Phase4TrialArmFailure failure = ErrorOf<Phase4TrialArmExecution>(ExecutePhase4TrialArmV1(
        Phase4TrialArm::kReusableCandidateAllocation, spec, {}, preparer.get()));
    ASSERT_TRUE(std::holds_alternative<Phase4CandidatePreparationFailureState>(failure.payload));
    const auto& child = std::get<Phase4CandidatePreparationFailureState>(failure.payload);
    EXPECT_EQ(failure.summary.invariant_id, child.error.invariant_id);
    EXPECT_EQ(child.case_state.workload.nets().size(), 6U);
  }
  {
    Phase4PairedTrialSpec spec = SpecV2();
    spec.preparation_config.store_config.maximum_candidates_per_net = 0;
    std::unique_ptr<allocator::PersistentCpuCandidatePoolPreparer> preparer = Preparer();
    Phase4TrialArmFailure failure =
        ErrorOf<Phase4TrialArmExecution>(ExecutePhase4TrialArmForCorpusV2(
            Phase4TrialArm::kReusableCandidateAllocation, spec, {}, preparer.get()));
    ASSERT_TRUE(std::holds_alternative<Phase4CandidatePreparationFailureState>(failure.payload));
    const std::uint64_t expected_fingerprint =
        FingerprintPhase4CaseDescriptorV2(*FindPhase4CaseDescriptorV2(spec.case_id));
    const Phase4DurableArmFailure durable = ValueOf<Phase4DurableArmFailure>(
        TryReconcilePhase4TrialArmFailureForCorpusV2(std::move(failure)));
    EXPECT_TRUE(durable.has_case_identity);
    EXPECT_EQ(durable.case_id, spec.case_id);
    EXPECT_EQ(durable.descriptor_fingerprint, expected_fingerprint);
  }
}

TEST(Phase4PairedTrialTest, PreservesCallerStateOnSessionFailure) {
  Phase4PairedTrialSpec spec = Spec();
  std::unique_ptr<allocator::PersistentCpuCandidatePoolPreparer> preparer = Preparer();
  allocator::internal::SetCpuCandidateAllocationFinalAssemblyFailureForTesting(true);
  Phase4TrialArmExecutionResult result = ExecutePhase4TrialArmV1(
      Phase4TrialArm::kReusableCandidateAllocation, spec, {}, preparer.get());
  allocator::internal::SetCpuCandidateAllocationFinalAssemblyFailureForTesting(false);
  Phase4TrialArmFailure failure = ErrorOf<Phase4TrialArmExecution>(std::move(result));
  ASSERT_TRUE(std::holds_alternative<Phase4CandidateSessionFailureState>(failure.payload));
  const auto& child = std::get<Phase4CandidateSessionFailureState>(failure.payload);
  EXPECT_FALSE(child.error.candidate_store_publication_committed);
  EXPECT_TRUE(child.prepared.has_candidate_store());
  EXPECT_EQ(child.case_state.board.content_hash(), child.case_state.workload.board_content_hash());
  EXPECT_EQ(child.case_state.workload.nets().size(), 6U);
  EXPECT_EQ(failure.summary.invariant_id, child.error.invariant_id);
}

TEST(Phase4PairedTrialTest, CrossCorpusReconciliationRejectionPreservesOwnedCandidateStore) {
  const auto execute_v1_failure = [] {
    const Phase4PairedTrialSpec spec = Spec();
    std::unique_ptr<allocator::PersistentCpuCandidatePoolPreparer> preparer = Preparer();
    allocator::internal::SetCpuCandidateAllocationFinalAssemblyFailureForTesting(true);
    Phase4TrialArmExecutionResult result = ExecutePhase4TrialArmV1(
        Phase4TrialArm::kReusableCandidateAllocation, spec, {}, preparer.get());
    allocator::internal::SetCpuCandidateAllocationFinalAssemblyFailureForTesting(false);
    return ErrorOf<Phase4TrialArmExecution>(std::move(result));
  };
  const auto execute_v2_failure = [] {
    const Phase4PairedTrialSpec spec = SpecV2();
    std::unique_ptr<allocator::PersistentCpuCandidatePoolPreparer> preparer = Preparer();
    allocator::internal::SetCpuCandidateAllocationFinalAssemblyFailureForTesting(true);
    Phase4TrialArmExecutionResult result = ExecutePhase4TrialArmForCorpusV2(
        Phase4TrialArm::kReusableCandidateAllocation, spec, {}, preparer.get());
    allocator::internal::SetCpuCandidateAllocationFinalAssemblyFailureForTesting(false);
    return ErrorOf<Phase4TrialArmExecution>(std::move(result));
  };
  const auto expect_preserved_store =
      [](const Phase4ArmFailureReconciliationRejectionV1& rejection) {
        EXPECT_EQ(rejection.invariant_id, "P4HARNESS-FAILURE-CORPUS-001");
        ASSERT_TRUE(
            std::holds_alternative<Phase4CandidateSessionFailureState>(rejection.failure.payload));
        const auto& state = std::get<Phase4CandidateSessionFailureState>(rejection.failure.payload);
        EXPECT_TRUE(state.prepared.has_candidate_store());
        std::uint64_t candidate_count = 0;
        for (const allocator::PreparedNetRoutingContext& context :
             state.case_state.workload.nets()) {
          candidate_count += state.prepared.candidate_store().Enumerate(context.request.net).size();
        }
        EXPECT_GT(candidate_count, 0U);
      };

  expect_preserved_store(ErrorOf<Phase4DurableArmFailure>(
      TryReconcilePhase4TrialArmFailureForCorpusV2(execute_v1_failure())));
  expect_preserved_store(
      ErrorOf<Phase4DurableArmFailure>(TryReconcilePhase4TrialArmFailureV1(execute_v2_failure())));
}

TEST(Phase4PairedTrialTest, PreservationBoundaryRetainsCommittedStoreEvidence) {
  const Phase4PairedTrialSpec spec = Spec();
  Phase4RepresentativeCase corpus =
      ValueOf<Phase4RepresentativeCase>(BuildPhase4RepresentativeCaseV1(spec.case_id, {}));
  std::unique_ptr<allocator::PersistentCpuCandidatePoolPreparer> preparer = Preparer();
  allocator::PreparedCpuCandidatePools prepared =
      ValueOf<allocator::PreparedCpuCandidatePools>(allocator::PrepareInitialCpuCandidatePools(
          *preparer, corpus.board, corpus.workload, spec.preparation_config));
  allocator::CpuCandidateAllocationSessionError child;
  child.invariant_id = "test.committed.session.failure";
  child.detail = "injected committed session failure";
  child.candidate_store_publication_committed = true;

  Phase4TrialArmFailure failure = internal::PreservePhase4CandidateSessionFailureV1(
      std::move(corpus), std::move(prepared), std::move(child));

  ASSERT_TRUE(std::holds_alternative<Phase4CandidateSessionFailureState>(failure.payload));
  const auto& retained = std::get<Phase4CandidateSessionFailureState>(failure.payload);
  EXPECT_TRUE(retained.error.candidate_store_publication_committed);
  EXPECT_TRUE(retained.prepared.has_candidate_store());
  EXPECT_EQ(retained.case_state.workload.nets().size(), 6U);
  EXPECT_EQ(failure.summary.invariant_id, retained.error.invariant_id);
}

TEST(Phase4PairedTrialTest, RequiresExternalIsolationAndAllResourceAuthorities) {
  const Phase4PairedTrialSpec spec = Spec();
  Phase4TrialArmExecution execution = ValueOf<Phase4TrialArmExecution>(
      ExecutePhase4TrialArmV1(Phase4TrialArm::kSequentialBaseline, spec, {}));
  Phase4ExternalResourceObservation observation = SyntheticAuthorityForContractTest(execution);
  observation.isolated_process = false;
  observation.authority_checksum = internal::ComputePhase4ExternalAuthorityChecksumV1(observation);
  const Phase4PairedTrialError error =
      ErrorOf<Phase4TrialArmRecord>(FinalizePhase4TrialArmV1(std::move(execution), observation));
  EXPECT_EQ(error.code, Phase4PairedTrialErrorCode::kExternalAuthorityUnavailable);
}

TEST(Phase4PairedTrialTest, RequiresAuthorityLifecycleToMatchWorkerCapturedTelemetry) {
  Phase4TrialArmExecution execution = ExecuteWarmCandidate(Spec());
  Phase4ExternalResourceObservation observation = SyntheticAuthorityForContractTest(execution);
  ++observation.preparer_lifecycle.invocations_completed_before;
  observation.authority_checksum = internal::ComputePhase4ExternalAuthorityChecksumV1(observation);
  const Phase4PairedTrialError error =
      ErrorOf<Phase4TrialArmRecord>(FinalizePhase4TrialArmV1(std::move(execution), observation));
  EXPECT_EQ(error.invariant_id, "P4PAIR-FINALIZE-LIFECYCLE-001");
}

TEST(Phase4PairedTrialTest, RequiresNestedTimingIntervalsWithWidenedArithmetic) {
  const Phase4PairedTrialSpec spec = Spec();
  Phase4TrialArmExecution execution = ValueOf<Phase4TrialArmExecution>(
      ExecutePhase4TrialArmV1(Phase4TrialArm::kSequentialBaseline, spec, {}));
  const std::uint64_t interval_sum =
      execution.case_build_elapsed_nanoseconds + execution.prepared_elapsed_nanoseconds;
  ASSERT_GT(interval_sum, 0U);
  execution.cold_elapsed_nanoseconds = interval_sum - 1;
  Phase4ExternalResourceObservation observation = SyntheticAuthorityForContractTest(execution);
  observation.authority_checksum = internal::ComputePhase4ExternalAuthorityChecksumV1(observation);
  const Phase4PairedTrialError error =
      ErrorOf<Phase4TrialArmRecord>(FinalizePhase4TrialArmV1(std::move(execution), observation));
  EXPECT_EQ(error.invariant_id, "P4PAIR-FINALIZE-006");
}

TEST(Phase4PairedTrialTest, CanonicalizesNonsemanticScheduleInputOrder) {
  Phase4PairedTrialSpec ordered = Spec();
  Phase4PairedTrialSpec reversed = ordered;
  std::swap(reversed.candidate_session_config.schedules[0],
            reversed.candidate_session_config.schedules[1]);
  const Phase4TrialArmExecution ordered_execution = ValueOf<Phase4TrialArmExecution>(
      ExecutePhase4TrialArmV1(Phase4TrialArm::kSequentialBaseline, ordered, {}));
  const Phase4TrialArmExecution reversed_execution = ValueOf<Phase4TrialArmExecution>(
      ExecutePhase4TrialArmV1(Phase4TrialArm::kSequentialBaseline, reversed, {}));
  EXPECT_EQ(ordered_execution.semantics.budget_checksum,
            reversed_execution.semantics.budget_checksum);
  EXPECT_EQ(ordered_execution.semantics.semantic_checksum,
            reversed_execution.semantics.semantic_checksum);
}

TEST(Phase4PairedTrialTest, CanonicalAlgorithmBudgetChecksumBindsHiddenConfigs) {
  const Phase4PairedTrialSpec canonical = Spec();
  const std::uint64_t expected =
      internal::ComputePhase4CanonicalAlgorithmBudgetChecksumV1(canonical);
  ASSERT_NE(expected, 0U);

  Phase4PairedTrialSpec changed = canonical;
  ++changed.baseline_config.limits.maximum_trace_bytes;
  EXPECT_NE(internal::ComputePhase4CanonicalAlgorithmBudgetChecksumV1(changed), expected);

  changed = canonical;
  ++changed.preparation_config.store_config.maximum_rejection_records;
  EXPECT_NE(internal::ComputePhase4CanonicalAlgorithmBudgetChecksumV1(changed), expected);

  changed = canonical;
  ++changed.candidate_session_config.multi_world_config.maximum_pareto_comparisons;
  EXPECT_NE(internal::ComputePhase4CanonicalAlgorithmBudgetChecksumV1(changed), expected);

  changed = canonical;
  --changed.candidate_session_config.schema_version;
  EXPECT_NE(internal::ComputePhase4CanonicalAlgorithmBudgetChecksumV1(changed), expected);

  changed = canonical;
  --changed.candidate_session_config.regeneration_execution_config.maximum_policy_resource_entries;
  EXPECT_NE(internal::ComputePhase4CanonicalAlgorithmBudgetChecksumV1(changed), expected);

  changed = canonical;
  ++changed.external_budget.maximum_cold_elapsed_nanoseconds;
  --changed.corpus_limits.maximum_active_regions;
  EXPECT_EQ(internal::ComputePhase4CanonicalAlgorithmBudgetChecksumV1(changed), expected);
}

TEST(Phase4PairedTrialTest, SemanticChecksumExcludesOrderAndWorkersButArtifactIncludesThem) {
  ExecutedPair arms = ExecutePair(Spec());
  Phase4TrialArmRecord changed = arms.candidate;
  changed.semantics.execution_order = Phase4TrialOrder::kCandidateFirst;
  ++changed.semantics.preparation_worker_count;
  EXPECT_EQ(internal::ComputePhase4TrialArmSemanticChecksumV1(changed.semantics),
            arms.candidate.semantics.semantic_checksum);
  changed.artifact_checksum = internal::ComputePhase4TrialArmArtifactChecksumV1(changed);
  EXPECT_NE(changed.artifact_checksum, arms.candidate.artifact_checksum);
}

TEST(Phase4PairedTrialTest, RejectsIndividuallyAuthenticatedArmsFromDifferentPairs) {
  ExecutedPair arms = ExecutePair(Spec());
  ++arms.candidate.semantics.repetition_index;
  arms.candidate.semantics.semantic_checksum =
      internal::ComputePhase4TrialArmSemanticChecksumV1(arms.candidate.semantics);
  arms.candidate.external_observation.associated_semantic_checksum =
      arms.candidate.semantics.semantic_checksum;
  arms.candidate.external_observation.authority_checksum =
      internal::ComputePhase4ExternalAuthorityChecksumV1(arms.candidate.external_observation);
  arms.candidate.artifact_checksum =
      internal::ComputePhase4TrialArmArtifactChecksumV1(arms.candidate);
  const Phase4PairedTrialError error = ErrorOf<Phase4PairedTrialResult>(
      AssemblePhase4PairedTrialV1(std::move(arms.baseline), std::move(arms.candidate)));
  EXPECT_EQ(error.invariant_id, "P4PAIR-ASSEMBLE-002");
}

TEST(Phase4PairedTrialTest, AssemblyRevalidatesAuthorityAfterChecksumRecomputation) {
  ExecutedPair arms = ExecutePair(Spec());
  arms.candidate.external_observation.memory_authority_enforced = false;
  arms.candidate.external_observation.authority_checksum =
      internal::ComputePhase4ExternalAuthorityChecksumV1(arms.candidate.external_observation);
  arms.candidate.artifact_checksum =
      internal::ComputePhase4TrialArmArtifactChecksumV1(arms.candidate);
  const Phase4PairedTrialError error = ErrorOf<Phase4PairedTrialResult>(
      AssemblePhase4PairedTrialV1(std::move(arms.baseline), std::move(arms.candidate)));
  EXPECT_EQ(error.code, Phase4PairedTrialErrorCode::kExternalAuthorityUnavailable);
}

TEST(Phase4PairedTrialTest, AssemblyRejectsUnknownTerminalReasonAfterChecksumRecomputation) {
  ExecutedPair arms = ExecutePair(Spec());
  arms.candidate.semantics.terminal_reason = static_cast<Phase4NormalizedTerminalReason>(255);
  Reauthenticate(arms.candidate);
  const Phase4PairedTrialError error = ErrorOf<Phase4PairedTrialResult>(
      AssemblePhase4PairedTrialV1(std::move(arms.baseline), std::move(arms.candidate)));
  EXPECT_EQ(error.invariant_id, "P4PAIR-FINALIZE-001");
}

TEST(Phase4PairedTrialTest, AssemblyRejectsUnknownOutcomeSourceAfterChecksumRecomputation) {
  ExecutedPair arms = ExecutePair(Spec());
  arms.candidate.semantics.candidate_outcome_source =
      static_cast<Phase4CandidateOutcomeSource>(255);
  Reauthenticate(arms.candidate);
  const Phase4PairedTrialError error = ErrorOf<Phase4PairedTrialResult>(
      AssemblePhase4PairedTrialV1(std::move(arms.baseline), std::move(arms.candidate)));
  EXPECT_EQ(error.invariant_id, "P4PAIR-FINALIZE-001");
}

TEST(Phase4PairedTrialTest, AssemblyRejectsImpossibleAuthenticatedSemanticCounters) {
  {
    ExecutedPair arms = ExecutePair(Spec());
    arms.candidate.semantics.final_candidate_count =
        arms.candidate.semantics.outcome.selected_net_count - 1;
    Reauthenticate(arms.candidate);
    const Phase4PairedTrialError error = ErrorOf<Phase4PairedTrialResult>(
        AssemblePhase4PairedTrialV1(std::move(arms.baseline), std::move(arms.candidate)));
    EXPECT_EQ(error.invariant_id, "P4PAIR-FINALIZE-SEMANTICS-001");
  }
  {
    ExecutedPair arms = ExecutePair(Spec());
    ++arms.candidate.semantics.admitted_candidates;
    Reauthenticate(arms.candidate);
    const Phase4PairedTrialError error = ErrorOf<Phase4PairedTrialResult>(
        AssemblePhase4PairedTrialV1(std::move(arms.baseline), std::move(arms.candidate)));
    EXPECT_EQ(error.invariant_id, "P4PAIR-FINALIZE-SEMANTICS-001");
  }
  {
    ExecutedPair arms = ExecutePair(Spec());
    const std::uint64_t per_query_work = arms.candidate.semantics.opportunity.route_work_units /
                                         arms.candidate.semantics.opportunity.route_queries;
    arms.candidate.semantics.preparation_route_queries = 1;
    arms.candidate.semantics.preparation_route_work_units = per_query_work + 1;
    arms.candidate.semantics.regeneration_route_queries = 0;
    arms.candidate.semantics.regeneration_route_work_units = 0;
    arms.candidate.semantics.actual = {
        .route_queries = 1,
        .route_work_units = per_query_work + 1,
    };
    Reauthenticate(arms.candidate);
    const Phase4PairedTrialError error = ErrorOf<Phase4PairedTrialResult>(
        AssemblePhase4PairedTrialV1(std::move(arms.baseline), std::move(arms.candidate)));
    EXPECT_EQ(error.invariant_id, "P4PAIR-FINALIZE-SEMANTICS-001");
  }
}

TEST(Phase4PairedTrialTest, AssemblyRejectsZeroWorkersAfterChecksumRecomputation) {
  ExecutedPair arms = ExecutePair(Spec());
  arms.candidate.semantics.preparation_worker_count = 0;
  arms.candidate.preparer_lifecycle.workers_started_before = 0;
  arms.candidate.preparer_lifecycle.workers_started_after = 0;
  arms.candidate.external_observation.preparer_lifecycle = arms.candidate.preparer_lifecycle;
  Reauthenticate(arms.candidate);
  const Phase4PairedTrialError error = ErrorOf<Phase4PairedTrialResult>(
      AssemblePhase4PairedTrialV1(std::move(arms.baseline), std::move(arms.candidate)));
  EXPECT_EQ(error.invariant_id, "P4PAIR-FINALIZE-001");
}

TEST(Phase4PairedTrialTest, AssemblyRejectsTooManyWorkersAfterChecksumRecomputation) {
  ExecutedPair arms = ExecutePair(Spec());
  arms.candidate.semantics.preparation_worker_count =
      allocator::kMaximumPersistentCpuCandidateWorkersV1 + 1;
  arms.candidate.preparer_lifecycle.workers_started_before =
      arms.candidate.semantics.preparation_worker_count;
  arms.candidate.preparer_lifecycle.workers_started_after =
      arms.candidate.semantics.preparation_worker_count;
  arms.candidate.external_observation.preparer_lifecycle = arms.candidate.preparer_lifecycle;
  Reauthenticate(arms.candidate);
  const Phase4PairedTrialError error = ErrorOf<Phase4PairedTrialResult>(
      AssemblePhase4PairedTrialV1(std::move(arms.baseline), std::move(arms.candidate)));
  EXPECT_EQ(error.invariant_id, "P4PAIR-FINALIZE-001");
}

TEST(Phase4PairedTrialTest, AssemblyRequiresDistinctArmProcessInstances) {
  ExecutedPair arms = ExecutePair(Spec());
  arms.candidate.external_observation.process_instance_identity =
      arms.baseline.external_observation.process_instance_identity;
  arms.candidate.external_observation.authority_checksum =
      internal::ComputePhase4ExternalAuthorityChecksumV1(arms.candidate.external_observation);
  arms.candidate.artifact_checksum =
      internal::ComputePhase4TrialArmArtifactChecksumV1(arms.candidate);
  const Phase4PairedTrialError error = ErrorOf<Phase4PairedTrialResult>(
      AssemblePhase4PairedTrialV1(std::move(arms.baseline), std::move(arms.candidate)));
  EXPECT_EQ(error.invariant_id, "P4PAIR-ASSEMBLE-AUTHORITY-001");
}

TEST(Phase4PairedTrialTest, DiagnosticPerNetTelemetryClosesAuthenticBaselineAndCandidateCase) {
  const Phase4PairedTrialSpec spec = Spec();
  Phase4TrialArmDiagnosticExecutionV1 baseline = ValueOf<Phase4TrialArmDiagnosticExecutionV1>(
      ExecutePhase4TrialArmDiagnosticV1(Phase4TrialArm::kSequentialBaseline, spec, {}));
  std::unique_ptr<allocator::PersistentCpuCandidatePoolPreparer> preparer = Preparer();
  Phase4TrialArmDiagnosticExecutionV1 candidate =
      ValueOf<Phase4TrialArmDiagnosticExecutionV1>(ExecutePhase4TrialArmDiagnosticV1(
          Phase4TrialArm::kReusableCandidateAllocation, spec, {}, preparer.get()));
  const Phase4TrialArmExecution normal_baseline = ValueOf<Phase4TrialArmExecution>(
      ExecutePhase4TrialArmV1(Phase4TrialArm::kSequentialBaseline, spec, {}));
  const Phase4TrialArmExecution normal_candidate =
      ValueOf<Phase4TrialArmExecution>(ExecutePhase4TrialArmV1(
          Phase4TrialArm::kReusableCandidateAllocation, spec, {}, preparer.get()));
  Phase4RepresentativeCase corpus = ValueOf<Phase4RepresentativeCase>(
      BuildPhase4RepresentativeCaseV1(spec.case_id, {}, spec.corpus_limits));

  EXPECT_EQ(baseline.semantics, normal_baseline.semantics);
  EXPECT_EQ(candidate.semantics, normal_candidate.semantics);
  EXPECT_EQ(baseline.telemetry.per_net.size(), 6U);
  EXPECT_EQ(candidate.telemetry.per_net.size(), 6U);
  EXPECT_FALSE(internal::ValidatePhase4ArmReportTelemetryV1(baseline.semantics, corpus.workload,
                                                            baseline.telemetry)
                   .has_value());
  EXPECT_FALSE(internal::ValidatePhase4ArmReportTelemetryV1(candidate.semantics, corpus.workload,
                                                            candidate.telemetry)
                   .has_value());
  for (const Phase4PerNetReportV1& report : candidate.telemetry.per_net) {
    const std::uint64_t expected_pairs =
        report.final_pool_size < 2 ? 0 : report.final_pool_size * (report.final_pool_size - 1) / 2;
    EXPECT_EQ(report.candidate_pair_count, expected_pairs);
    EXPECT_LE(report.mean_resource_overlap_ppm, kPhase4OverlapPartsPerMillion);
    EXPECT_LE(report.mean_geometric_overlap_ppm, kPhase4OverlapPartsPerMillion);
    if (report.selected_status == allocator::NetSelectionStatus::kSelected) {
      ASSERT_TRUE(report.selected_candidate_metrics.has_value());
      ASSERT_TRUE(report.pool_best_intrinsic_cost.has_value());
      EXPECT_LE(*report.pool_best_intrinsic_cost,
                report.selected_candidate_metrics->intrinsic_base_cost);
    }
  }
}

TEST(Phase4PairedTrialTest, BaselineTelemetryMatchesIndependentAuthenticColumnsPoolsAndSelections) {
  const Phase4PairedTrialSpec spec = Spec();
  const Phase4TrialArmDiagnosticExecutionV1 diagnostic =
      ValueOf<Phase4TrialArmDiagnosticExecutionV1>(
          ExecutePhase4TrialArmDiagnosticV1(Phase4TrialArm::kSequentialBaseline, spec, {}));
  Phase4RepresentativeCase corpus = ValueOf<Phase4RepresentativeCase>(
      BuildPhase4RepresentativeCaseV1(spec.case_id, {}, spec.corpus_limits));
  allocator::SequentialNegotiatedBaselineResult source =
      ValueOf<allocator::SequentialNegotiatedBaselineResult>(
          allocator::ExecuteSequentialNegotiatedBaseline(corpus.board, corpus.workload,
                                                         corpus.capacities, spec.baseline_config));
  ASSERT_EQ(diagnostic.telemetry.per_net.size(), source.final_pools().size());
  ASSERT_EQ(diagnostic.telemetry.per_net.size(), source.final_world().selections.size());
  for (std::size_t index = 0; index < diagnostic.telemetry.per_net.size(); ++index) {
    const Phase4PerNetReportV1& report = diagnostic.telemetry.per_net[index];
    EXPECT_EQ(report.columns, BaselineColumnsFor(source, report.net));
    ExpectReportMatchesPoolAndSelection(report, source.final_pools()[index],
                                        source.final_world().selections[index]);
  }
}

TEST(Phase4PairedTrialTest, CandidateTelemetryMatchesAuthenticCase101Sources) {
  const Phase4PairedTrialSpec spec = Spec(Phase4TrialOrder::kBaselineFirst, 101, 6);
  std::unique_ptr<allocator::PersistentCpuCandidatePoolPreparer> preparer = Preparer();
  const Phase4TrialArmDiagnosticExecutionV1 diagnostic =
      ValueOf<Phase4TrialArmDiagnosticExecutionV1>(ExecutePhase4TrialArmDiagnosticV1(
          Phase4TrialArm::kReusableCandidateAllocation, spec, {}, preparer.get()));
  allocator::CpuCandidateAllocationSession source = ExecuteCandidateSessionForSources(spec);
  const allocator::OneWorldAllocation& selected_world = ChosenWorld(source);
  ASSERT_EQ(diagnostic.telemetry.per_net.size(), source.final_pools().size());
  ASSERT_EQ(diagnostic.telemetry.per_net.size(), selected_world.selections.size());
  for (std::size_t index = 0; index < diagnostic.telemetry.per_net.size(); ++index) {
    const Phase4PerNetReportV1& report = diagnostic.telemetry.per_net[index];
    EXPECT_EQ(report.columns, CandidateColumnsFor(source, report.net));
    ExpectReportMatchesPoolAndSelection(report, source.final_pools()[index],
                                        selected_world.selections[index]);
  }
}

TEST(Phase4PairedTrialTest, CandidateTelemetryMatchesAuthenticCase102Sources) {
  const Phase4PairedTrialSpec spec = Spec(Phase4TrialOrder::kBaselineFirst, 102, 6);
  std::unique_ptr<allocator::PersistentCpuCandidatePoolPreparer> preparer = Preparer();
  const Phase4TrialArmDiagnosticExecutionV1 diagnostic =
      ValueOf<Phase4TrialArmDiagnosticExecutionV1>(ExecutePhase4TrialArmDiagnosticV1(
          Phase4TrialArm::kReusableCandidateAllocation, spec, {}, preparer.get()));
  allocator::CpuCandidateAllocationSession source = ExecuteCandidateSessionForSources(spec);
  const allocator::OneWorldAllocation& selected_world = ChosenWorld(source);
  for (std::size_t index = 0; index < diagnostic.telemetry.per_net.size(); ++index) {
    const Phase4PerNetReportV1& report = diagnostic.telemetry.per_net[index];
    EXPECT_EQ(report.columns, CandidateColumnsFor(source, report.net));
    ExpectReportMatchesPoolAndSelection(report, source.final_pools()[index],
                                        selected_world.selections[index]);
  }
}

TEST(Phase4PairedTrialTest, ColumnClassificationCoversEveryTerminalAndInFlightOutcome) {
  Phase4PerNetColumnOutcomesV1 baseline;
  const auto add_baseline = [&baseline](allocator::SequentialNegotiatedColumnOutcome outcome,
                                        std::optional<candidates::CandidateRejectionCode> code =
                                            std::nullopt) {
    allocator::SequentialNegotiatedColumnRecord column;
    column.outcome = outcome;
    if (code.has_value()) {
      candidates::CandidateRejection rejection;
      rejection.code = *code;
      column.rejection = rejection;
    }
    EXPECT_TRUE(internal::AccumulatePhase4BaselineColumnV1(column, &baseline));
  };
  add_baseline(allocator::SequentialNegotiatedColumnOutcome::kAdmitted);
  add_baseline(allocator::SequentialNegotiatedColumnOutcome::kRouteDisconnected);
  add_baseline(allocator::SequentialNegotiatedColumnOutcome::kRouteUnsupported);
  add_baseline(allocator::SequentialNegotiatedColumnOutcome::kBuildRejected,
               candidates::CandidateRejectionCode::kExactValidation);
  add_baseline(allocator::SequentialNegotiatedColumnOutcome::kAdmissionRejected,
               candidates::CandidateRejectionCode::kInvalidInput);
  EXPECT_EQ(baseline, (Phase4PerNetColumnOutcomesV1{
                          .requested_columns = 5,
                          .executed_route_queries = 5,
                          .admitted_candidates = 1,
                          .disconnected_columns = 1,
                          .unsupported_columns = 1,
                          .exact_validation_rejections = 1,
                          .other_rejections = 1,
                      }));

  Phase4PerNetColumnOutcomesV1 preparation;
  const auto add_preparation =
      [&preparation](allocator::CpuCandidatePoolColumnOutcome outcome,
                     std::optional<candidates::CandidateRejectionCode> code = std::nullopt) {
        allocator::CpuCandidatePoolColumnRecord column;
        column.outcome = outcome;
        column.rejection_code = code;
        EXPECT_TRUE(internal::AccumulatePhase4PreparationColumnV1(column, &preparation));
      };
  add_preparation(allocator::CpuCandidatePoolColumnOutcome::kAdmitted);
  add_preparation(allocator::CpuCandidatePoolColumnOutcome::kDuplicate);
  add_preparation(allocator::CpuCandidatePoolColumnOutcome::kRouteDisconnected);
  add_preparation(allocator::CpuCandidatePoolColumnOutcome::kRouteUnsupported);
  add_preparation(allocator::CpuCandidatePoolColumnOutcome::kSkippedAfterDisconnectedProof);
  add_preparation(allocator::CpuCandidatePoolColumnOutcome::kSkippedAfterUnsupportedProof);
  add_preparation(allocator::CpuCandidatePoolColumnOutcome::kBuildRejected,
                  candidates::CandidateRejectionCode::kInvalidInput);
  add_preparation(allocator::CpuCandidatePoolColumnOutcome::kAdmissionRejected,
                  candidates::CandidateRejectionCode::kExactValidation);
  EXPECT_EQ(preparation, (Phase4PerNetColumnOutcomesV1{
                             .requested_columns = 8,
                             .executed_route_queries = 6,
                             .admitted_candidates = 1,
                             .duplicate_candidates = 1,
                             .disconnected_columns = 1,
                             .unsupported_columns = 1,
                             .skipped_columns = 2,
                             .exact_validation_rejections = 1,
                             .other_rejections = 1,
                         }));

  Phase4PerNetColumnOutcomesV1 regeneration;
  const auto add_regeneration =
      [&regeneration](allocator::TargetedRegenerationColumnOutcome outcome,
                      std::optional<candidates::CandidateRejectionCode> code = std::nullopt) {
        allocator::TargetedRegenerationColumnRecord column;
        column.outcome = outcome;
        column.rejection_code = code;
        EXPECT_TRUE(internal::AccumulatePhase4RegenerationColumnV1(column, &regeneration));
      };
  add_regeneration(allocator::TargetedRegenerationColumnOutcome::kAdmitted);
  add_regeneration(allocator::TargetedRegenerationColumnOutcome::kDuplicate);
  add_regeneration(allocator::TargetedRegenerationColumnOutcome::kRouteDisconnected);
  add_regeneration(allocator::TargetedRegenerationColumnOutcome::kRouteUnsupported);
  add_regeneration(allocator::TargetedRegenerationColumnOutcome::kBuildRejected,
                   candidates::CandidateRejectionCode::kExactValidation);
  add_regeneration(allocator::TargetedRegenerationColumnOutcome::kAdmissionRejected,
                   candidates::CandidateRejectionCode::kInvalidInput);
  EXPECT_EQ(regeneration, (Phase4PerNetColumnOutcomesV1{
                              .requested_columns = 6,
                              .executed_route_queries = 6,
                              .admitted_candidates = 1,
                              .duplicate_candidates = 1,
                              .disconnected_columns = 1,
                              .unsupported_columns = 1,
                              .exact_validation_rejections = 1,
                              .other_rejections = 1,
                          }));

  for (const allocator::TargetedRegenerationColumnOutcome in_flight :
       {allocator::TargetedRegenerationColumnOutcome::kQueryInFlight,
        allocator::TargetedRegenerationColumnOutcome::kBuildInFlight,
        allocator::TargetedRegenerationColumnOutcome::kGeneratedPendingPublication,
        allocator::TargetedRegenerationColumnOutcome::kRejectionEvidenceInFlight,
        allocator::TargetedRegenerationColumnOutcome::
            kPublicationCommittedOutcomeCorrelationPending}) {
    allocator::TargetedRegenerationColumnRecord column;
    column.outcome = in_flight;
    Phase4PerNetColumnOutcomesV1 rejected;
    EXPECT_FALSE(internal::AccumulatePhase4RegenerationColumnV1(column, &rejected));
  }
}

TEST(Phase4PairedTrialTest, TelemetryChecksumHasLiteralGoldenAndFieldGroupSensitivity) {
  const Phase4ArmReportTelemetryV1 golden = GoldenTelemetry();
  EXPECT_EQ(golden.telemetry_checksum, 4'209'921'871'565'787'050ULL);

  Phase4ArmReportTelemetryV1 association = golden;
  ++association.associated_semantic_checksum;
  EXPECT_NE(internal::ComputePhase4ArmReportTelemetryChecksumV1(association),
            golden.telemetry_checksum);
  Phase4ArmReportTelemetryV1 net = golden;
  ++net.per_net[0].net.generation;
  EXPECT_NE(internal::ComputePhase4ArmReportTelemetryChecksumV1(net), golden.telemetry_checksum);
  Phase4ArmReportTelemetryV1 columns = golden;
  ++columns.per_net[0].columns.other_rejections;
  EXPECT_NE(internal::ComputePhase4ArmReportTelemetryChecksumV1(columns),
            golden.telemetry_checksum);
  Phase4ArmReportTelemetryV1 diversity = golden;
  ++diversity.per_net[0].mean_geometric_overlap_ppm;
  EXPECT_NE(internal::ComputePhase4ArmReportTelemetryChecksumV1(diversity),
            golden.telemetry_checksum);
  Phase4ArmReportTelemetryV1 identity = golden;
  ++identity.per_net[0].selected_candidate_id->low;
  EXPECT_NE(internal::ComputePhase4ArmReportTelemetryChecksumV1(identity),
            golden.telemetry_checksum);
  Phase4ArmReportTelemetryV1 payload = golden;
  ++*payload.per_net[0].selected_candidate_payload_checksum;
  EXPECT_NE(internal::ComputePhase4ArmReportTelemetryChecksumV1(payload),
            golden.telemetry_checksum);
  Phase4ArmReportTelemetryV1 metrics = golden;
  ++metrics.per_net[0].selected_candidate_metrics->via_count;
  EXPECT_NE(internal::ComputePhase4ArmReportTelemetryChecksumV1(metrics),
            golden.telemetry_checksum);
  Phase4ArmReportTelemetryV1 best_cost = golden;
  ++*best_cost.per_net[0].pool_best_intrinsic_cost;
  EXPECT_NE(internal::ComputePhase4ArmReportTelemetryChecksumV1(best_cost),
            golden.telemetry_checksum);
  Phase4ArmReportTelemetryV1 no_candidate = golden;
  ++no_candidate.per_net[1].columns.skipped_columns;
  EXPECT_NE(internal::ComputePhase4ArmReportTelemetryChecksumV1(no_candidate),
            golden.telemetry_checksum);
}

TEST(Phase4PairedTrialTest, DiagnosticPerNetTelemetryIsRepeatedlyDeterministic) {
  const Phase4PairedTrialSpec spec = Spec();
  const Phase4TrialArmDiagnosticExecutionV1 first_baseline =
      ValueOf<Phase4TrialArmDiagnosticExecutionV1>(
          ExecutePhase4TrialArmDiagnosticV1(Phase4TrialArm::kSequentialBaseline, spec, {}));
  const Phase4TrialArmDiagnosticExecutionV1 second_baseline =
      ValueOf<Phase4TrialArmDiagnosticExecutionV1>(
          ExecutePhase4TrialArmDiagnosticV1(Phase4TrialArm::kSequentialBaseline, spec, {}));
  EXPECT_EQ(first_baseline.semantics, second_baseline.semantics);
  EXPECT_EQ(first_baseline.telemetry, second_baseline.telemetry);

  std::unique_ptr<allocator::PersistentCpuCandidatePoolPreparer> preparer = Preparer();
  const Phase4TrialArmDiagnosticExecutionV1 first_candidate =
      ValueOf<Phase4TrialArmDiagnosticExecutionV1>(ExecutePhase4TrialArmDiagnosticV1(
          Phase4TrialArm::kReusableCandidateAllocation, spec, {}, preparer.get()));
  const Phase4TrialArmDiagnosticExecutionV1 second_candidate =
      ValueOf<Phase4TrialArmDiagnosticExecutionV1>(ExecutePhase4TrialArmDiagnosticV1(
          Phase4TrialArm::kReusableCandidateAllocation, spec, {}, preparer.get()));
  EXPECT_EQ(first_candidate.semantics, second_candidate.semantics);
  EXPECT_EQ(first_candidate.telemetry, second_candidate.telemetry);
}

TEST(Phase4PairedTrialTest, DiagnosticValidatorRejectsReauthenticatedRosterAndClosureDrift) {
  const Phase4PairedTrialSpec spec = Spec();
  const Phase4TrialArmDiagnosticExecutionV1 diagnostic =
      ValueOf<Phase4TrialArmDiagnosticExecutionV1>(
          ExecutePhase4TrialArmDiagnosticV1(Phase4TrialArm::kSequentialBaseline, spec, {}));
  Phase4RepresentativeCase corpus = ValueOf<Phase4RepresentativeCase>(
      BuildPhase4RepresentativeCaseV1(spec.case_id, {}, spec.corpus_limits));

  {
    Phase4ArmReportTelemetryV1 changed = diagnostic.telemetry;
    std::swap(changed.per_net[0], changed.per_net[1]);
    changed.telemetry_checksum = internal::ComputePhase4ArmReportTelemetryChecksumV1(changed);
    ASSERT_TRUE(
        internal::ValidatePhase4ArmReportTelemetryV1(diagnostic.semantics, corpus.workload, changed)
            .has_value());
  }
  {
    Phase4ArmReportTelemetryV1 changed = diagnostic.telemetry;
    ++changed.per_net[0].net.id;
    changed.telemetry_checksum = internal::ComputePhase4ArmReportTelemetryChecksumV1(changed);
    ASSERT_TRUE(
        internal::ValidatePhase4ArmReportTelemetryV1(diagnostic.semantics, corpus.workload, changed)
            .has_value());
  }
  {
    Phase4ArmReportTelemetryV1 changed = diagnostic.telemetry;
    ++changed.per_net[0].columns.requested_columns;
    changed.telemetry_checksum = internal::ComputePhase4ArmReportTelemetryChecksumV1(changed);
    ASSERT_TRUE(
        internal::ValidatePhase4ArmReportTelemetryV1(diagnostic.semantics, corpus.workload, changed)
            .has_value());
  }
  {
    Phase4ArmReportTelemetryV1 changed = diagnostic.telemetry;
    auto selected =
        std::find_if(changed.per_net.begin(), changed.per_net.end(),
                     [](const auto& net) { return net.selected_candidate_metrics.has_value(); });
    ASSERT_NE(selected, changed.per_net.end());
    ++selected->selected_candidate_metrics->intrinsic_base_cost;
    changed.telemetry_checksum = internal::ComputePhase4ArmReportTelemetryChecksumV1(changed);
    ASSERT_TRUE(
        internal::ValidatePhase4ArmReportTelemetryV1(diagnostic.semantics, corpus.workload, changed)
            .has_value());
  }
  {
    Phase4ArmReportTelemetryV1 changed = diagnostic.telemetry;
    auto selected =
        std::find_if(changed.per_net.begin(), changed.per_net.end(),
                     [](const auto& net) { return net.selected_candidate_id.has_value(); });
    ASSERT_NE(selected, changed.per_net.end());
    selected->selected_candidate_id = candidates::CandidateId{};
    changed.telemetry_checksum = internal::ComputePhase4ArmReportTelemetryChecksumV1(changed);
    ASSERT_TRUE(
        internal::ValidatePhase4ArmReportTelemetryV1(diagnostic.semantics, corpus.workload, changed)
            .has_value());
  }
  {
    Phase4ArmReportTelemetryV1 oversized = diagnostic.telemetry;
    oversized.per_net.resize(kMaximumPhase4RepresentativeNetsV1 + 1U,
                             diagnostic.telemetry.per_net.front());
    const std::optional<Phase4PairedTrialError> error =
        internal::ValidatePhase4ArmReportTelemetryV1(diagnostic.semantics, corpus.workload,
                                                     oversized);
    ASSERT_TRUE(error.has_value());
    EXPECT_EQ(error->invariant_id, "P4REPORT-BOUND-001");
  }
  Phase4ArmReportTelemetryV1 checksum_changed = diagnostic.telemetry;
  ++checksum_changed.per_net[0].mean_resource_overlap_ppm;
  EXPECT_NE(internal::ComputePhase4ArmReportTelemetryChecksumV1(checksum_changed),
            diagnostic.telemetry.telemetry_checksum);
}

TEST(Phase4PairedTrialTest, ValidatorRejectsReauthenticatedPerNetPoolAdmissionMismatch) {
  const Phase4PairedTrialSpec spec = Spec();
  std::unique_ptr<allocator::PersistentCpuCandidatePoolPreparer> preparer = Preparer();
  const Phase4TrialArmDiagnosticExecutionV1 diagnostic =
      ValueOf<Phase4TrialArmDiagnosticExecutionV1>(ExecutePhase4TrialArmDiagnosticV1(
          Phase4TrialArm::kReusableCandidateAllocation, spec, {}, preparer.get()));
  Phase4RepresentativeCase corpus = ValueOf<Phase4RepresentativeCase>(
      BuildPhase4RepresentativeCaseV1(spec.case_id, {}, spec.corpus_limits));
  Phase4ArmReportTelemetryV1 changed = diagnostic.telemetry;
  auto report = std::find_if(changed.per_net.begin(), changed.per_net.end(),
                             [](const auto& net) { return net.final_pool_size != 0; });
  ASSERT_NE(report, changed.per_net.end());
  report->final_pool_size = report->columns.admitted_candidates + 1;
  report->candidate_pair_count = report->final_pool_size * (report->final_pool_size - 1) / 2;
  changed.telemetry_checksum = internal::ComputePhase4ArmReportTelemetryChecksumV1(changed);
  const std::optional<Phase4PairedTrialError> error =
      internal::ValidatePhase4ArmReportTelemetryV1(diagnostic.semantics, corpus.workload, changed);
  ASSERT_TRUE(error.has_value());
  EXPECT_EQ(error->invariant_id, "P4REPORT-CLOSURE-001");
}

TEST(Phase4PairedTrialTest, SameRunDecisionTelemetryClosesAuthenticBaselineAndCandidateExecution) {
  const Phase4PairedTrialSpec spec = Spec();
  const Phase4TrialArmWithSameRunTelemetryExecutionV1 baseline =
      ValueOf<Phase4TrialArmWithSameRunTelemetryExecutionV1>(
          ExecutePhase4TrialArmWithSameRunTelemetryV1(Phase4TrialArm::kSequentialBaseline, spec,
                                                      {}));
  std::unique_ptr<allocator::PersistentCpuCandidatePoolPreparer> preparer = Preparer();
  const Phase4TrialArmWithSameRunTelemetryExecutionV1 candidate =
      ValueOf<Phase4TrialArmWithSameRunTelemetryExecutionV1>(
          ExecutePhase4TrialArmWithSameRunTelemetryV1(Phase4TrialArm::kReusableCandidateAllocation,
                                                      spec, {}, preparer.get()));
  const Phase4RepresentativeCase corpus = ValueOf<Phase4RepresentativeCase>(
      BuildPhase4RepresentativeCaseV1(spec.case_id, {}, spec.corpus_limits));

  for (const Phase4TrialArmWithSameRunTelemetryExecutionV1* arm : {&baseline, &candidate}) {
    EXPECT_EQ(arm->telemetry.associated_semantic_checksum,
              arm->execution.semantics.semantic_checksum);
    EXPECT_EQ(arm->telemetry.outcome, arm->execution.semantics.outcome);
    EXPECT_EQ(arm->telemetry.per_net.size(), arm->execution.semantics.workload_net_count);
    EXPECT_FALSE(internal::ValidatePhase4SameRunArmDecisionTelemetryV1(
                     arm->execution.semantics, corpus.workload, arm->telemetry)
                     .has_value());
  }

  const Phase4TrialArmDiagnosticExecutionV1 diagnostic =
      ValueOf<Phase4TrialArmDiagnosticExecutionV1>(
          ExecutePhase4TrialArmDiagnosticV1(Phase4TrialArm::kSequentialBaseline, spec, {}));
  ASSERT_EQ(baseline.telemetry.per_net.size(), diagnostic.telemetry.per_net.size());
  for (std::size_t index = 0; index < baseline.telemetry.per_net.size(); ++index) {
    EXPECT_EQ(baseline.telemetry.per_net[index].net, diagnostic.telemetry.per_net[index].net);
    EXPECT_EQ(baseline.telemetry.per_net[index].columns,
              diagnostic.telemetry.per_net[index].columns);
  }
}

TEST(Phase4PairedTrialTest, SameRunDecisionValidatorRejectsReauthenticatedAssociationDrift) {
  const Phase4PairedTrialSpec spec = Spec();
  const Phase4TrialArmWithSameRunTelemetryExecutionV1 captured =
      ValueOf<Phase4TrialArmWithSameRunTelemetryExecutionV1>(
          ExecutePhase4TrialArmWithSameRunTelemetryV1(Phase4TrialArm::kSequentialBaseline, spec,
                                                      {}));
  const Phase4RepresentativeCase corpus = ValueOf<Phase4RepresentativeCase>(
      BuildPhase4RepresentativeCaseV1(spec.case_id, {}, spec.corpus_limits));

  {
    Phase4SameRunArmDecisionTelemetryV1 changed = captured.telemetry;
    std::swap(changed.per_net[0], changed.per_net[1]);
    changed.telemetry_checksum =
        internal::ComputePhase4SameRunArmDecisionTelemetryChecksumV1(changed);
    const std::optional<Phase4PairedTrialError> error =
        internal::ValidatePhase4SameRunArmDecisionTelemetryV1(captured.execution.semantics,
                                                              corpus.workload, changed);
    ASSERT_TRUE(error.has_value());
    EXPECT_EQ(error->invariant_id, "P4SAMERUN-NET-002");
  }
  {
    Phase4SameRunArmDecisionTelemetryV1 changed = captured.telemetry;
    ++changed.outcome.world_checksum;
    changed.telemetry_checksum =
        internal::ComputePhase4SameRunArmDecisionTelemetryChecksumV1(changed);
    const std::optional<Phase4PairedTrialError> error =
        internal::ValidatePhase4SameRunArmDecisionTelemetryV1(captured.execution.semantics,
                                                              corpus.workload, changed);
    ASSERT_TRUE(error.has_value());
    EXPECT_EQ(error->invariant_id, "P4SAMERUN-AUTH-001");
  }
  {
    Phase4SameRunArmDecisionTelemetryV1 changed = captured.telemetry;
    ++changed.per_net[0].columns.requested_columns;
    ++changed.per_net[0].columns.other_rejections;
    changed.telemetry_checksum =
        internal::ComputePhase4SameRunArmDecisionTelemetryChecksumV1(changed);
    const std::optional<Phase4PairedTrialError> error =
        internal::ValidatePhase4SameRunArmDecisionTelemetryV1(captured.execution.semantics,
                                                              corpus.workload, changed);
    ASSERT_TRUE(error.has_value());
    EXPECT_EQ(error->invariant_id, "P4SAMERUN-CLOSURE-001");
  }
}

TEST(Phase4PairedTrialTest, OperationalProfilesAreSeparateChecksumBoundAuthenticReplays) {
  const Phase4PairedTrialSpec spec = Spec();
  const Phase4TrialArmExecution ordinary_baseline = ValueOf<Phase4TrialArmExecution>(
      ExecutePhase4TrialArmV1(Phase4TrialArm::kSequentialBaseline, spec, {}));
  Phase4TrialArmOperationalProfileV1 baseline = ValueOf<Phase4TrialArmOperationalProfileV1>(
      ExecutePhase4TrialArmOperationalProfileV1(Phase4TrialArm::kSequentialBaseline, spec, {}));
  EXPECT_EQ(baseline.execution.semantics, ordinary_baseline.semantics);
  EXPECT_TRUE(baseline.baseline.has_value());
  EXPECT_FALSE(baseline.preparation.has_value());
  EXPECT_FALSE(baseline.candidate_session.has_value());
  EXPECT_EQ(baseline.profile_checksum,
            internal::ComputePhase4TrialArmOperationalProfileChecksumV1(baseline));
  EXPECT_NE(baseline.profile_checksum, 0U);
  EXPECT_FALSE(internal::ValidatePhase4TrialArmOperationalProfileV1(baseline).has_value());
  EXPECT_LE(baseline.unclassified_cold_scope_exit_wall_nanoseconds,
            baseline.execution.cold_elapsed_nanoseconds);
  EXPECT_EQ(baseline.case_build.fixture_import_applicability.status,
            Phase4OperationalMeasurementStatus::kNotApplicable);

  std::unique_ptr<allocator::PersistentCpuCandidatePoolPreparer> preparer = Preparer();
  const Phase4TrialArmExecution ordinary_candidate =
      ValueOf<Phase4TrialArmExecution>(ExecutePhase4TrialArmV1(
          Phase4TrialArm::kReusableCandidateAllocation, spec, {}, preparer.get()));
  Phase4TrialArmOperationalProfileV1 candidate =
      ValueOf<Phase4TrialArmOperationalProfileV1>(ExecutePhase4TrialArmOperationalProfileV1(
          Phase4TrialArm::kReusableCandidateAllocation, spec, {}, preparer.get()));
  EXPECT_EQ(candidate.execution.semantics, ordinary_candidate.semantics);
  EXPECT_FALSE(candidate.baseline.has_value());
  ASSERT_TRUE(candidate.preparation.has_value());
  ASSERT_TRUE(candidate.candidate_session.has_value());
  EXPECT_EQ(candidate.profile_checksum,
            internal::ComputePhase4TrialArmOperationalProfileChecksumV1(candidate));
  EXPECT_NE(candidate.profile_checksum, 0U);
  EXPECT_EQ(candidate.preparation->base_jobs_dispatched,
            candidate.execution.semantics.workload_net_count);
  EXPECT_FALSE(internal::ValidatePhase4TrialArmOperationalProfileV1(candidate).has_value());
  EXPECT_LE(candidate.candidate_session->regeneration_epochs.size(),
            spec.candidate_session_config.maximum_regeneration_epochs);
}

TEST(Phase4PairedTrialTest, ImportedOperationalReplayCarriesEndToEndSourceAuthority) {
  const std::string fixture = ReadImportedFixture();
  ASSERT_FALSE(fixture.empty());
  const Phase4PairedTrialSpec spec = Spec(Phase4TrialOrder::kBaselineFirst, 4'000, 2);
  const Phase4TrialArmOperationalProfileV1 profile =
      ValueOf<Phase4TrialArmOperationalProfileV1>(ExecutePhase4TrialArmOperationalProfileV1(
          Phase4TrialArm::kSequentialBaseline, spec, fixture));
  EXPECT_EQ(profile.case_build.case_source, Phase4CaseSource::kImportedFixture);
  EXPECT_EQ(profile.case_build.fixture_import_applicability.status,
            Phase4OperationalMeasurementStatus::kMeasured);
  EXPECT_EQ(profile.case_build.synthetic_materialization_applicability.status,
            Phase4OperationalMeasurementStatus::kNotApplicable);
  EXPECT_FALSE(internal::ValidatePhase4TrialArmOperationalProfileV1(profile).has_value());

  Phase4TrialArmOperationalProfileV1 changed = profile;
  changed.case_build.fixture_import_applicability.reason =
      Phase4OperationalMeasurementReason::kSyntheticCaseHasNoFixtureImport;
  changed.profile_checksum = internal::ComputePhase4TrialArmOperationalProfileChecksumV1(changed);
  const auto error = internal::ValidatePhase4TrialArmOperationalProfileV1(changed);
  ASSERT_TRUE(error.has_value());
  EXPECT_EQ(error->invariant_id, "P4OP-CASE-APPLICABILITY-001");

  changed = profile;
  ++changed.case_build.synthetic_geometry_and_board_materialization_wall_nanoseconds;
  changed.profile_checksum = internal::ComputePhase4TrialArmOperationalProfileChecksumV1(changed);
  const auto zero_field_error = internal::ValidatePhase4TrialArmOperationalProfileV1(changed);
  ASSERT_TRUE(zero_field_error.has_value());
  EXPECT_EQ(zero_field_error->invariant_id, "P4OP-CASE-APPLICABILITY-001");
}

TEST(Phase4PairedTrialTest, OperationalValidatorRejectsReauthenticatedAuthorityDrift) {
  const Phase4PairedTrialSpec spec = Spec();
  const Phase4TrialArmOperationalProfileV1 baseline = ValueOf<Phase4TrialArmOperationalProfileV1>(
      ExecutePhase4TrialArmOperationalProfileV1(Phase4TrialArm::kSequentialBaseline, spec, {}));
  EXPECT_FALSE(internal::ValidatePhase4TrialArmOperationalProfileV1(baseline).has_value());
  {
    Phase4TrialArmOperationalProfileV1 changed = baseline;
    changed.process_cpu.reason = Phase4OperationalMeasurementReason::kNone;
    changed.profile_checksum = internal::ComputePhase4TrialArmOperationalProfileChecksumV1(changed);
    const auto error = internal::ValidatePhase4TrialArmOperationalProfileV1(changed);
    ASSERT_TRUE(error.has_value());
    EXPECT_EQ(error->invariant_id, "P4OP-APPLICABILITY-001");
  }
  {
    Phase4TrialArmOperationalProfileV1 changed = baseline;
    changed.preparation.emplace();
    changed.profile_checksum = internal::ComputePhase4TrialArmOperationalProfileChecksumV1(changed);
    const auto error = internal::ValidatePhase4TrialArmOperationalProfileV1(changed);
    ASSERT_TRUE(error.has_value());
    EXPECT_EQ(error->invariant_id, "P4OP-ARM-SHAPE-001");
  }
  {
    Phase4TrialArmOperationalProfileV1 changed = baseline;
    changed.case_build.case_source = Phase4CaseSource::kImportedFixture;
    changed.profile_checksum = internal::ComputePhase4TrialArmOperationalProfileChecksumV1(changed);
    const auto error = internal::ValidatePhase4TrialArmOperationalProfileV1(changed);
    ASSERT_TRUE(error.has_value());
    EXPECT_EQ(error->invariant_id, "P4OP-CASE-SOURCE-001");
  }
  {
    Phase4TrialArmOperationalProfileV1 changed = baseline;
    ++changed.case_build.unclassified_and_release_wall_nanoseconds;
    changed.profile_checksum = internal::ComputePhase4TrialArmOperationalProfileChecksumV1(changed);
    const auto error = internal::ValidatePhase4TrialArmOperationalProfileV1(changed);
    ASSERT_TRUE(error.has_value());
    EXPECT_EQ(error->invariant_id, "P4OP-TIMING-CLOSURE-001");
  }
  {
    Phase4TrialArmOperationalProfileV1 changed = baseline;
    changed.contender_transient_release_tail_wall_nanoseconds =
        changed.execution.prepared_elapsed_nanoseconds + 1U;
    changed.profile_checksum = internal::ComputePhase4TrialArmOperationalProfileChecksumV1(changed);
    const auto error = internal::ValidatePhase4TrialArmOperationalProfileV1(changed);
    ASSERT_TRUE(error.has_value());
    EXPECT_EQ(error->invariant_id, "P4OP-TIMING-CLOSURE-001");
  }

  std::unique_ptr<allocator::PersistentCpuCandidatePoolPreparer> preparer = Preparer();
  static_cast<void>(ValueOf<Phase4TrialArmExecution>(ExecutePhase4TrialArmV1(
      Phase4TrialArm::kReusableCandidateAllocation, spec, {}, preparer.get())));
  const Phase4TrialArmOperationalProfileV1 candidate =
      ValueOf<Phase4TrialArmOperationalProfileV1>(ExecutePhase4TrialArmOperationalProfileV1(
          Phase4TrialArm::kReusableCandidateAllocation, spec, {}, preparer.get()));
  ASSERT_TRUE(candidate.candidate_session.has_value());
  ASSERT_FALSE(candidate.candidate_session->regeneration_plans.empty());
  ASSERT_EQ(candidate.candidate_session->regeneration_plans.size(),
            candidate.candidate_session->regeneration_epochs.size());
  EXPECT_EQ(candidate.candidate_session->regeneration_plans.front().price_update_operations, 1U);
  {
    Phase4TrialArmOperationalProfileV1 changed = candidate;
    ++changed.execution.preparer_lifecycle.invocations_completed_before;
    changed.profile_checksum = internal::ComputePhase4TrialArmOperationalProfileChecksumV1(changed);
    const auto error = internal::ValidatePhase4TrialArmOperationalProfileV1(changed);
    ASSERT_TRUE(error.has_value());
    EXPECT_EQ(error->invariant_id, "P4OP-LIFECYCLE-001");
  }
  {
    Phase4TrialArmOperationalProfileV1 changed = candidate;
    ++changed.preparation->alternative_jobs_dispatched;
    changed.profile_checksum = internal::ComputePhase4TrialArmOperationalProfileChecksumV1(changed);
    const auto error = internal::ValidatePhase4TrialArmOperationalProfileV1(changed);
    ASSERT_TRUE(error.has_value());
    EXPECT_EQ(error->invariant_id, "P4OP-CANDIDATE-CLOSURE-001");
  }
  {
    Phase4TrialArmOperationalProfileV1 changed = candidate;
    const std::uint64_t wave_wall = changed.preparation->base_worker_wave_wall_nanoseconds +
                                    changed.preparation->alternative_worker_wave_wall_nanoseconds;
    changed.preparation->route_and_candidate_build_worker_sum_nanoseconds =
        wave_wall * changed.execution.semantics.preparation_worker_count + 1U;
    changed.profile_checksum = internal::ComputePhase4TrialArmOperationalProfileChecksumV1(changed);
    const auto error = internal::ValidatePhase4TrialArmOperationalProfileV1(changed);
    ASSERT_TRUE(error.has_value());
    EXPECT_EQ(error->invariant_id, "P4OP-CANDIDATE-CLOSURE-001");
  }
  {
    Phase4TrialArmOperationalProfileV1 changed = candidate;
    ++changed.candidate_session->regeneration_plans.front().plan_checksum;
    ++changed.candidate_session->regeneration_epochs.front().plan_checksum;
    changed.profile_checksum = internal::ComputePhase4TrialArmOperationalProfileChecksumV1(changed);
    const auto error = internal::ValidatePhase4TrialArmOperationalProfileV1(changed);
    ASSERT_TRUE(error.has_value());
    EXPECT_EQ(error->invariant_id, "P4OP-SESSION-WITNESS-001");
  }
  {
    Phase4TrialArmOperationalProfileV1 changed = candidate;
    ++changed.candidate_session->regeneration_epochs.front().execution_checksum;
    changed.profile_checksum = internal::ComputePhase4TrialArmOperationalProfileChecksumV1(changed);
    const auto error = internal::ValidatePhase4TrialArmOperationalProfileV1(changed);
    ASSERT_TRUE(error.has_value());
    EXPECT_EQ(error->invariant_id, "P4OP-SESSION-WITNESS-001");
  }
  {
    Phase4TrialArmOperationalProfileV1 changed = candidate;
    ++changed.candidate_session->regeneration_plans.front().plan_checksum;
    ++changed.candidate_session->regeneration_epochs.front().plan_checksum;
    ++changed.candidate_session->replay_witness.epoch_association_checksum;
    changed.profile_checksum = internal::ComputePhase4TrialArmOperationalProfileChecksumV1(changed);
    const auto error = internal::ValidatePhase4TrialArmOperationalProfileV1(changed);
    ASSERT_TRUE(error.has_value());
    EXPECT_EQ(error->invariant_id, "P4OP-SESSION-WITNESS-001");
  }
  {
    Phase4TrialArmOperationalProfileV1 changed = candidate;
    ++changed.candidate_session->regeneration_epochs.front().execution_checksum;
    ++changed.candidate_session->replay_witness.epoch_association_checksum;
    changed.profile_checksum = internal::ComputePhase4TrialArmOperationalProfileChecksumV1(changed);
    const auto error = internal::ValidatePhase4TrialArmOperationalProfileV1(changed);
    ASSERT_TRUE(error.has_value());
    EXPECT_EQ(error->invariant_id, "P4OP-SESSION-WITNESS-001");
  }
  {
    Phase4TrialArmOperationalProfileV1 changed = candidate;
    changed.candidate_session->regeneration_plans.clear();
    changed.candidate_session->regeneration_epochs.clear();
    changed.candidate_session->targeted_regeneration_price_update_wall_nanoseconds = 0;
    changed.candidate_session
        ->targeted_regeneration_selection_and_target_planning_wall_nanoseconds = 0;
    changed.profile_checksum = internal::ComputePhase4TrialArmOperationalProfileChecksumV1(changed);
    const auto error = internal::ValidatePhase4TrialArmOperationalProfileV1(changed);
    ASSERT_TRUE(error.has_value());
    EXPECT_EQ(error->invariant_id, "P4OP-SESSION-WITNESS-001");
  }
  {
    Phase4TrialArmOperationalProfileV1 changed = candidate;
    ++changed.candidate_session->replay_witness.counters.admitted_candidates;
    changed.profile_checksum = internal::ComputePhase4TrialArmOperationalProfileChecksumV1(changed);
    const auto error = internal::ValidatePhase4TrialArmOperationalProfileV1(changed);
    ASSERT_TRUE(error.has_value());
    EXPECT_EQ(error->invariant_id, "P4OP-NESTED-CLOSURE-001");
  }
  {
    Phase4TrialArmOperationalProfileV1 changed = candidate;
    ++changed.candidate_session->regeneration_epochs.front().counters.successful_routes;
    changed.profile_checksum = internal::ComputePhase4TrialArmOperationalProfileChecksumV1(changed);
    const auto error = internal::ValidatePhase4TrialArmOperationalProfileV1(changed);
    ASSERT_TRUE(error.has_value());
    EXPECT_EQ(error->invariant_id, "P4OP-SESSION-WITNESS-001");
  }
  {
    Phase4TrialArmOperationalProfileV1 changed = candidate;
    ++changed.contender_transient_release_tail_wall_nanoseconds;
    changed.profile_checksum = internal::ComputePhase4TrialArmOperationalProfileChecksumV1(changed);
    const auto error = internal::ValidatePhase4TrialArmOperationalProfileV1(changed);
    ASSERT_TRUE(error.has_value());
    EXPECT_EQ(error->invariant_id, "P4OP-CANDIDATE-CLOSURE-001");
  }
  {
    Phase4TrialArmOperationalProfileV1 changed = candidate;
    ++changed.candidate_session->regeneration_epochs.front().epoch_index;
    changed.profile_checksum = internal::ComputePhase4TrialArmOperationalProfileChecksumV1(changed);
    const auto error = internal::ValidatePhase4TrialArmOperationalProfileV1(changed);
    ASSERT_TRUE(error.has_value());
    EXPECT_EQ(error->invariant_id, "P4OP-SESSION-WITNESS-001");
  }
  {
    Phase4TrialArmOperationalProfileV1 changed = candidate;
    ++changed.candidate_session->regeneration_plans.front().plan_checksum;
    changed.profile_checksum = internal::ComputePhase4TrialArmOperationalProfileChecksumV1(changed);
    const auto error = internal::ValidatePhase4TrialArmOperationalProfileV1(changed);
    ASSERT_TRUE(error.has_value());
    EXPECT_EQ(error->invariant_id, "P4OP-EPOCH-ASSOCIATION-001");
  }
}

TEST(Phase4PairedTrialTest, UnmeasuredReplayAuthoritiesBindCompleteLiveSessionPreimages) {
  const Phase4PairedTrialSpec spec = Spec();
  const Phase4TrialArmOperationalProfileV1 measured_baseline =
      ValueOf<Phase4TrialArmOperationalProfileV1>(
          ExecutePhase4TrialArmOperationalProfileV1(Phase4TrialArm::kSequentialBaseline, spec, {}));
  const Phase4TrialArmReplayAuthorityV1 baseline = ValueOf<Phase4TrialArmReplayAuthorityV1>(
      ExecutePhase4TrialArmReplayAuthorityV1(Phase4TrialArm::kSequentialBaseline, spec, {}));
  EXPECT_EQ(baseline.semantics, measured_baseline.execution.semantics);
  EXPECT_EQ(baseline.recomputed_full_preimage_session_checksum,
            baseline.semantics.algorithm_session_checksum);
  EXPECT_FALSE(baseline.candidate_session_witness.has_value());
  EXPECT_EQ(baseline.preparer_lifecycle, Phase4PreparerLifecycleObservation{});
  EXPECT_FALSE(internal::ValidatePhase4TrialArmReplayAuthorityV1(baseline).has_value());

  std::unique_ptr<allocator::PersistentCpuCandidatePoolPreparer> measured_preparer = Preparer();
  static_cast<void>(ValueOf<Phase4TrialArmExecution>(ExecutePhase4TrialArmV1(
      Phase4TrialArm::kReusableCandidateAllocation, spec, {}, measured_preparer.get())));
  const Phase4TrialArmOperationalProfileV1 measured_candidate =
      ValueOf<Phase4TrialArmOperationalProfileV1>(ExecutePhase4TrialArmOperationalProfileV1(
          Phase4TrialArm::kReusableCandidateAllocation, spec, {}, measured_preparer.get()));
  std::unique_ptr<allocator::PersistentCpuCandidatePoolPreparer> authority_preparer = Preparer();
  static_cast<void>(ValueOf<Phase4TrialArmExecution>(ExecutePhase4TrialArmV1(
      Phase4TrialArm::kReusableCandidateAllocation, spec, {}, authority_preparer.get())));
  const Phase4TrialArmReplayAuthorityV1 candidate =
      ValueOf<Phase4TrialArmReplayAuthorityV1>(ExecutePhase4TrialArmReplayAuthorityV1(
          Phase4TrialArm::kReusableCandidateAllocation, spec, {}, authority_preparer.get()));
  ASSERT_TRUE(measured_candidate.candidate_session.has_value());
  ASSERT_TRUE(candidate.candidate_session_witness.has_value());
  EXPECT_EQ(candidate.semantics, measured_candidate.execution.semantics);
  EXPECT_EQ(candidate.recomputed_full_preimage_session_checksum,
            candidate.semantics.algorithm_session_checksum);
  EXPECT_EQ(*candidate.candidate_session_witness,
            measured_candidate.candidate_session->replay_witness);
  EXPECT_FALSE(internal::ValidatePhase4TrialArmReplayAuthorityV1(candidate).has_value());

  Phase4TrialArmReplayAuthorityV1 changed = candidate;
  ++changed.candidate_session_witness->final_pool_manifest_checksum;
  changed.authority_checksum = internal::ComputePhase4TrialArmReplayAuthorityChecksumV1(changed);
  const auto error = internal::ValidatePhase4TrialArmReplayAuthorityV1(changed);
  ASSERT_TRUE(error.has_value());
  EXPECT_EQ(error->invariant_id, "P4OP-AUTHORITY-WITNESS-001");

  for (const std::uint8_t terminal_reason : {std::uint8_t{4}, std::uint8_t{255}}) {
    changed = candidate;
    changed.candidate_session_witness->terminal_reason =
        static_cast<allocator::CpuCandidateAllocationTerminalReason>(terminal_reason);
    changed.authority_checksum = internal::ComputePhase4TrialArmReplayAuthorityChecksumV1(changed);
    const auto terminal_error = internal::ValidatePhase4TrialArmReplayAuthorityV1(changed);
    ASSERT_TRUE(terminal_error.has_value());
    EXPECT_EQ(terminal_error->invariant_id, "P4OP-AUTHORITY-WITNESS-001");
  }
}

TEST(Phase4PairedTrialTest, CandidateReplayAuthorityRejectsStaleNestedLiveChecksums) {
  const Phase4PairedTrialSpec spec = Spec();
  allocator::CpuCandidateAllocationSession session = ExecuteCandidateSessionForSources(spec);
  ASSERT_TRUE(
      allocator::internal::BuildCpuCandidateAllocationSessionReplayWitnessV1(session).has_value());

  auto& workload_nets =
      const_cast<std::vector<allocator::PreparedNetRoutingContext>&>(session.workload().nets());
  ASSERT_FALSE(workload_nets.empty());
  ++workload_nets.front().request.start.x;
  EXPECT_FALSE(
      allocator::internal::BuildCpuCandidateAllocationSessionReplayWitnessV1(session).has_value());
  --workload_nets.front().request.start.x;

  allocator::AllocationAssociations& capacity_associations =
      const_cast<allocator::AllocationAssociations&>(session.capacities().associations());
  ++capacity_associations.board_content_hash;
  EXPECT_FALSE(
      allocator::internal::BuildCpuCandidateAllocationSessionReplayWitnessV1(session).has_value());
  --capacity_associations.board_content_hash;

  auto& preparation_columns = const_cast<std::vector<allocator::CpuCandidatePoolColumnRecord>&>(
      session.preparation().columns());
  ASSERT_FALSE(preparation_columns.empty());
  ++preparation_columns.front().route_work_units;
  EXPECT_FALSE(
      allocator::internal::BuildCpuCandidateAllocationSessionReplayWitnessV1(session).has_value());
  --preparation_columns.front().route_work_units;

  auto& final_pools = const_cast<std::vector<allocator::CandidatePool>&>(session.final_pools());
  ASSERT_FALSE(final_pools.empty());
  ASSERT_FALSE(final_pools.front().candidates.empty());
  candidates::GeneratedRouteCandidate& candidate = const_cast<candidates::GeneratedRouteCandidate&>(
      final_pools.front().candidates.front()->data());
  ++candidate.metrics.intrinsic_base_cost;
  EXPECT_FALSE(
      allocator::internal::BuildCpuCandidateAllocationSessionReplayWitnessV1(session).has_value());
  --candidate.metrics.intrinsic_base_cost;

  allocator::NegotiatedPriceConfig& final_price_config =
      const_cast<allocator::NegotiatedPriceConfig&>(session.final_price_state().config());
  ++final_price_config.maximum_price_per_resource;
  EXPECT_FALSE(
      allocator::internal::BuildCpuCandidateAllocationSessionReplayWitnessV1(session).has_value());
  --final_price_config.maximum_price_per_resource;

  allocator::OneWorldAllocation& single_world =
      const_cast<allocator::OneWorldAllocation&>(session.final_single_world());
  ++single_world.total_intrinsic_cost;
  EXPECT_FALSE(
      allocator::internal::BuildCpuCandidateAllocationSessionReplayWitnessV1(session).has_value());
  --single_world.total_intrinsic_cost;

  auto& retained_worlds = const_cast<std::vector<allocator::RetainedMultiWorld>&>(
      session.final_multi_world().retained_worlds());
  ASSERT_FALSE(retained_worlds.empty());
  ++retained_worlds.front().world.total_intrinsic_cost;
  EXPECT_FALSE(
      allocator::internal::BuildCpuCandidateAllocationSessionReplayWitnessV1(session).has_value());
  --retained_worlds.front().world.total_intrinsic_cost;

  allocator::NegotiatedPriceConfig& retained_price_config =
      const_cast<allocator::NegotiatedPriceConfig&>(retained_worlds.front().price_state.config());
  ++retained_price_config.maximum_price_per_resource;
  EXPECT_FALSE(
      allocator::internal::BuildCpuCandidateAllocationSessionReplayWitnessV1(session).has_value());
  --retained_price_config.maximum_price_per_resource;

  ASSERT_TRUE(
      allocator::internal::BuildCpuCandidateAllocationSessionReplayWitnessV1(session).has_value());
}

TEST(Phase4PairedTrialTest, FragmentedCalibrationReplayWitnessUsesCanonicalCandidateOrder) {
  Phase4CanonicalCellConfig cell;
  cell.case_id = 10'220;
  cell.requested_pool_size = 4;
  cell.preparation_worker_count = kPhase4CanonicalPreparationWorkersV1;
  cell.repetitions = kPhase4CanonicalRepetitionsV1;
  cell.maximum_setup_elapsed_nanoseconds = 60'000'000'000ULL;
  cell.external_budget = {
      .maximum_prepared_elapsed_nanoseconds = 120'000'000'000ULL,
      .maximum_cold_elapsed_nanoseconds = 180'000'000'000ULL,
      .maximum_address_space_bytes = 64ULL * 1024ULL * 1024ULL * 1024ULL,
      .maximum_peak_host_bytes = 32ULL * 1024ULL * 1024ULL * 1024ULL,
  };
  const Phase4PairedTrialSpec spec = ValueOf<Phase4PairedTrialSpec>(
      BuildPhase4CanonicalTrialSpecForCorpusV2(cell, 0, Phase4TrialOrder::kBaselineFirst));
  allocator::CpuCandidateAllocationSession session =
      ExecuteCandidateSessionForSources(spec, Phase4RepresentativeCorpusAuthority::kV2);
  EXPECT_TRUE(
      allocator::internal::BuildCpuCandidateAllocationSessionReplayWitnessV1(session).has_value());
}

TEST(Phase4PairedTrialTest, BaselineReplayAuthorityRejectsStaleNestedLiveChecksums) {
  const Phase4PairedTrialSpec spec = Spec();
  Phase4RepresentativeCase corpus = ValueOf<Phase4RepresentativeCase>(
      BuildPhase4RepresentativeCaseV1(spec.case_id, {}, spec.corpus_limits));
  allocator::SequentialNegotiatedBaselineResult result =
      ValueOf<allocator::SequentialNegotiatedBaselineResult>(
          allocator::ExecuteSequentialNegotiatedBaseline(corpus.board, corpus.workload,
                                                         corpus.capacities, spec.baseline_config));
  const auto rebuild = [&]() {
    return allocator::internal::RecomputeSequentialNegotiatedSessionChecksumFromLiveV1(
        result, corpus.board, corpus.workload, corpus.capacities);
  };
  ASSERT_TRUE(rebuild().has_value());

  auto& workload_nets =
      const_cast<std::vector<allocator::PreparedNetRoutingContext>&>(corpus.workload.nets());
  ASSERT_FALSE(workload_nets.empty());
  ++workload_nets.front().request.goal.y;
  EXPECT_FALSE(rebuild().has_value());
  --workload_nets.front().request.goal.y;

  allocator::OneWorldAllocation& final_world =
      const_cast<allocator::OneWorldAllocation&>(result.final_world());
  ++final_world.total_selection_score;
  EXPECT_FALSE(rebuild().has_value());
  --final_world.total_selection_score;

  allocator::NegotiatedPriceConfig& price_config =
      const_cast<allocator::NegotiatedPriceConfig&>(result.successor_price_state().config());
  ++price_config.maximum_price_per_resource;
  EXPECT_FALSE(rebuild().has_value());
  --price_config.maximum_price_per_resource;

  auto& final_pools = const_cast<std::vector<allocator::CandidatePool>&>(result.final_pools());
  ASSERT_FALSE(final_pools.empty());
  ASSERT_FALSE(final_pools.front().candidates.empty());
  candidates::GeneratedRouteCandidate& candidate = const_cast<candidates::GeneratedRouteCandidate&>(
      final_pools.front().candidates.front()->data());
  ++candidate.policy_identity;
  EXPECT_FALSE(rebuild().has_value());
  --candidate.policy_identity;
  ++candidate.provenance.deterministic_seed;
  EXPECT_FALSE(rebuild().has_value());
  --candidate.provenance.deterministic_seed;
  ++candidate.geometry_signature.low;
  EXPECT_FALSE(rebuild().has_value());
  --candidate.geometry_signature.low;
  ++candidate.resource_signature.high;
  EXPECT_FALSE(rebuild().has_value());
  --candidate.resource_signature.high;

  ASSERT_TRUE(rebuild().has_value());
}

TEST(Phase4PairedTrialTest, OperationalWorkerSerializationRejectsInvalidAuthorityInputs) {
  const Phase4PairedTrialSpec spec = Spec();
  const Phase4TrialArmOperationalProfileV1 profile = ValueOf<Phase4TrialArmOperationalProfileV1>(
      ExecutePhase4TrialArmOperationalProfileV1(Phase4TrialArm::kSequentialBaseline, spec, {}));
  Phase4TrialArmReplayAuthorityV1 authority = ValueOf<Phase4TrialArmReplayAuthorityV1>(
      ExecutePhase4TrialArmReplayAuthorityV1(Phase4TrialArm::kSequentialBaseline, spec, {}));
  constexpr std::string_view commit = "0123456789abcdef0123456789abcdef01234567";
  EXPECT_FALSE(SerializePhase4OperationalProfileWorkerJsonForCorpusV2(profile, commit, true, false)
                   .has_value());
  EXPECT_FALSE(SerializePhase4ReplayAuthorityWorkerJsonForCorpusV2(authority, commit, true, false)
                   .has_value());
  const std::optional<std::string> serialized =
      SerializePhase4ReplayAuthorityWorkerJsonV1(authority, commit, true, false);
  ASSERT_TRUE(serialized.has_value());
  EXPECT_TRUE(serialized->ends_with('\n'));
  EXPECT_NE(serialized->find("\"kind\":1"), std::string::npos);
  EXPECT_NE(serialized->find("\"candidate_session_witness\":null"), std::string::npos);
  EXPECT_FALSE(
      SerializePhase4ReplayAuthorityWorkerJsonV1(authority, "short", true, false).has_value());
  EXPECT_FALSE(SerializePhase4ReplayAuthorityWorkerJsonV1(
                   authority, "gggggggggggggggggggggggggggggggggggggggg", true, false)
                   .has_value());

  authority.recomputed_full_preimage_session_checksum ^= 1;
  authority.authority_checksum =
      internal::ComputePhase4TrialArmReplayAuthorityChecksumV1(authority);
  EXPECT_FALSE(
      SerializePhase4ReplayAuthorityWorkerJsonV1(authority, commit, true, false).has_value());
}

}  // namespace
}  // namespace apgar::benchmark
