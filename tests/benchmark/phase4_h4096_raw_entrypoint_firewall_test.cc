#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string_view>
#include <utility>
#include <variant>

#include "apgar/benchmark/phase4_paired_trial.h"
#include "apgar/benchmark/phase4_representative_corpus.h"
#include "src/benchmark/phase4_confirmatory_h4096_execution_internal.h"
#include "src/benchmark/phase4_h4096_canonical_budget_internal.h"
#include "src/benchmark/phase4_paired_trial_internal.h"
#include "tests/support/google_test.h"

namespace apgar::benchmark {
namespace {

// This test reconstructs configuration and semantic preimages only. It never
// calls an arm, case, fixture, preparer, or allocator execution surface.
[[nodiscard]] Phase4CanonicalCellConfig CanonicalCell(std::uint32_t case_id,
                                                      std::uint32_t pool_size) {
  Phase4CanonicalCellConfig cell;
  cell.case_id = case_id;
  cell.requested_pool_size = pool_size;
  cell.maximum_setup_elapsed_nanoseconds = 300'000'000'000ULL;
  cell.external_budget = {
      .maximum_prepared_elapsed_nanoseconds = 300'000'000'000ULL,
      .maximum_cold_elapsed_nanoseconds = 300'000'000'000ULL,
      .maximum_address_space_bytes = 64ULL * 1024ULL * 1024ULL * 1024ULL,
      .maximum_peak_host_bytes = 16ULL * 1024ULL * 1024ULL * 1024ULL,
  };
  return cell;
}

[[nodiscard]] Phase4PairedTrialSpec Built(Phase4CanonicalSpecResult result) {
  EXPECT_TRUE(std::holds_alternative<Phase4PairedTrialSpec>(result))
      << (std::holds_alternative<Phase4TrialHarnessError>(result)
              ? std::get<Phase4TrialHarnessError>(result).detail
              : "");
  if (!std::holds_alternative<Phase4PairedTrialSpec>(result)) {
    std::abort();
  }
  return std::get<Phase4PairedTrialSpec>(std::move(result));
}

[[nodiscard]] Phase4PairedTrialSpec H4096Spec(std::uint32_t case_id, std::uint32_t pool_size) {
  return Built(internal::BuildPhase4CanonicalTrialSpecForCorpusV2H4096(
      CanonicalCell(case_id, pool_size), 0, Phase4TrialOrder::kBaselineFirst));
}

[[nodiscard]] Phase4PairedTrialSpec H2250Spec(std::uint32_t case_id, std::uint32_t pool_size) {
  return Built(BuildPhase4CanonicalTrialSpecForCorpusV2(CanonicalCell(case_id, pool_size), 0,
                                                        Phase4TrialOrder::kBaselineFirst));
}

void ExpectPreflightInvariant(std::optional<Phase4PairedTrialError> error,
                              std::string_view invariant_id) {
  ASSERT_TRUE(error.has_value());
  EXPECT_EQ(error->code, Phase4PairedTrialErrorCode::kInvalidConfiguration);
  EXPECT_EQ(error->invariant_id, invariant_id);
}

void ExpectFinalizationInvariant(Phase4TrialArmRecordResult result, Phase4PairedTrialErrorCode code,
                                 std::string_view invariant_id) {
  ASSERT_TRUE(std::holds_alternative<Phase4PairedTrialError>(result));
  const Phase4PairedTrialError& error = std::get<Phase4PairedTrialError>(result);
  EXPECT_EQ(error.code, code);
  EXPECT_EQ(error.invariant_id, invariant_id);
}

void ExpectNoSuccessfulArmWitness(const Phase4IsolatedCellResult& cell) {
  EXPECT_TRUE(std::none_of(
      cell.attempts.begin(), cell.attempts.end(), [](const Phase4IsolatedPairAttempt& pair) {
        return pair.baseline.record.has_value() || pair.candidate.record.has_value();
      }));
}

[[nodiscard]] Phase4TrialArmSemantics AssociatedSemantics(const Phase4PairedTrialSpec& spec,
                                                          Phase4TrialArm arm) {
  const Phase4CaseDescriptor* descriptor =
      FindPhase4CaseDescriptorForAuthority(Phase4RepresentativeCorpusAuthority::kV2, spec.case_id);
  EXPECT_NE(descriptor, nullptr);
  if (descriptor == nullptr) {
    std::abort();
  }
  const std::uint64_t route_queries = static_cast<std::uint64_t>(descriptor->requested_net_count) *
                                      spec.baseline_config.maximum_sweeps;
  const Phase4RouteOpportunity opportunity{
      .route_queries = route_queries,
      .route_work_units = route_queries * spec.baseline_config.route_limits.maximum_work_units,
  };
  std::uint32_t terminal_rounds = 0;
  for (const allocator::MultiWorldSchedule& schedule : spec.candidate_session_config.schedules) {
    terminal_rounds = std::max(terminal_rounds, schedule.maximum_selection_rounds);
  }
  const std::uint64_t columns_per_epoch =
      spec.candidate_session_config.regeneration_plan_config.maximum_total_columns;

  Phase4TrialArmSemantics semantics;
  semantics.arm = arm;
  semantics.execution_order = spec.execution_order;
  semantics.corpus_version =
      Phase4RepresentativeCorpusVersionForAuthority(Phase4RepresentativeCorpusAuthority::kV2);
  semantics.corpus_checksum =
      Phase4RepresentativeCorpusChecksumForAuthority(Phase4RepresentativeCorpusAuthority::kV2);
  semantics.case_id = spec.case_id;
  semantics.descriptor_fingerprint = FingerprintPhase4CaseDescriptorForAuthority(
      Phase4RepresentativeCorpusAuthority::kV2, *descriptor);
  semantics.budget_checksum = internal::ComputePhase4PairedBudgetChecksumForAuthorityV1(
      Phase4RepresentativeCorpusAuthority::kV2, spec, opportunity, descriptor->requested_net_count,
      columns_per_epoch, terminal_rounds);
  semantics.workload_net_count = descriptor->requested_net_count;
  semantics.requested_pool_size = spec.requested_pool_size;
  semantics.repetition_index = spec.repetition_index;
  semantics.root_seed = spec.root_seed;
  semantics.preparation_worker_count = spec.preparation_worker_count;
  semantics.baseline_sweeps = spec.baseline_config.maximum_sweeps;
  semantics.candidate_regeneration_epochs =
      spec.candidate_session_config.maximum_regeneration_epochs;
  semantics.candidate_columns_per_epoch = columns_per_epoch;
  semantics.candidate_terminal_selection_rounds = terminal_rounds;
  semantics.external_budget = spec.external_budget;
  semantics.opportunity = opportunity;
  return semantics;
}

TEST(Phase4H4096RawEntrypointFirewallTest, AcceptsOnlyTheTwoPureProtocolV2Preflights) {
  const Phase4PairedTrialSpec ordinary = H4096Spec(10'200, 8);
  const Phase4PairedTrialSpec same_run = H4096Spec(10'100, 4);
  for (const Phase4TrialArm arm : std::array{Phase4TrialArm::kSequentialBaseline,
                                             Phase4TrialArm::kReusableCandidateAllocation}) {
    EXPECT_FALSE(internal::PreflightPhase4ConfirmatoryH4096OrdinarySpec(ordinary, arm).has_value());
    EXPECT_FALSE(internal::PreflightPhase4ConfirmatoryH4096SameRunSpec(same_run, arm).has_value());
  }
}

TEST(Phase4H4096RawEntrypointFirewallTest, RejectsH2250AndCanonicalBudgetDrift) {
  constexpr std::string_view kPriceAuthority = "P4PAIR-CORPUS-V2-H4096-BUDGET-AUTHORITY-001";
  constexpr std::string_view kCanonicalBudget = "P4PAIR-CORPUS-V2-H4096-CANONICAL-BUDGET-001";
  ExpectPreflightInvariant(internal::PreflightPhase4ConfirmatoryH4096OrdinarySpec(
                               H2250Spec(10'200, 8), Phase4TrialArm::kSequentialBaseline),
                           kPriceAuthority);
  ExpectPreflightInvariant(internal::PreflightPhase4ConfirmatoryH4096SameRunSpec(
                               H2250Spec(10'100, 4), Phase4TrialArm::kSequentialBaseline),
                           kPriceAuthority);

  Phase4PairedTrialSpec ordinary_drift = H4096Spec(10'200, 8);
  ++ordinary_drift.baseline_config.maximum_sweeps;
  ExpectPreflightInvariant(internal::PreflightPhase4ConfirmatoryH4096OrdinarySpec(
                               ordinary_drift, Phase4TrialArm::kSequentialBaseline),
                           kCanonicalBudget);
  Phase4PairedTrialSpec same_run_drift = H4096Spec(10'100, 4);
  ++same_run_drift.candidate_session_config.maximum_regeneration_epochs;
  ExpectPreflightInvariant(internal::PreflightPhase4ConfirmatoryH4096SameRunSpec(
                               same_run_drift, Phase4TrialArm::kReusableCandidateAllocation),
                           kCanonicalBudget);
}

TEST(Phase4H4096RawEntrypointFirewallTest, RejectsScopeAndCarrierDrift) {
  constexpr std::string_view kOrdinaryScope = "P4PAIR-CORPUS-V2-H4096-ORDINARY-SCOPE-001";
  constexpr std::string_view kSameRunScope = "P4PAIR-CORPUS-V2-H4096-SAME-RUN-SCOPE-001";
  const Phase4PairedTrialSpec ordinary = H4096Spec(10'200, 8);
  const Phase4PairedTrialSpec same_run = H4096Spec(10'100, 4);

  ExpectPreflightInvariant(internal::PreflightPhase4ConfirmatoryH4096OrdinarySpec(
                               same_run, Phase4TrialArm::kSequentialBaseline),
                           kOrdinaryScope);
  ExpectPreflightInvariant(internal::PreflightPhase4ConfirmatoryH4096SameRunSpec(
                               ordinary, Phase4TrialArm::kSequentialBaseline),
                           kSameRunScope);

  Phase4PairedTrialSpec wrong_ordinary_cell = ordinary;
  wrong_ordinary_cell.requested_pool_size = 4;
  ExpectPreflightInvariant(internal::PreflightPhase4ConfirmatoryH4096OrdinarySpec(
                               wrong_ordinary_cell, Phase4TrialArm::kSequentialBaseline),
                           kOrdinaryScope);
  Phase4PairedTrialSpec wrong_same_run_cell = same_run;
  wrong_same_run_cell.case_id = 10'101;
  ExpectPreflightInvariant(internal::PreflightPhase4ConfirmatoryH4096SameRunSpec(
                               wrong_same_run_cell, Phase4TrialArm::kSequentialBaseline),
                           kSameRunScope);
}

TEST(Phase4H4096RawEntrypointFirewallTest,
     FinalizationRechecksPriceBudgetCarrierAndCellAssociation) {
  constexpr std::string_view kPriceAuthority = "P4PAIR-CORPUS-V2-H4096-BUDGET-AUTHORITY-001";
  constexpr std::string_view kCanonicalBudget = "P4PAIR-CORPUS-V2-H4096-CANONICAL-BUDGET-001";
  constexpr std::string_view kOrdinaryScope = "P4PAIR-CORPUS-V2-H4096-ORDINARY-SCOPE-001";
  constexpr std::string_view kSameRunScope = "P4PAIR-CORPUS-V2-H4096-SAME-RUN-SCOPE-001";
  constexpr std::string_view kAssociation = "P4PAIR-CORPUS-V2-H4096-ASSOCIATION-001";

  const Phase4PairedTrialSpec ordinary = H4096Spec(10'200, 8);
  const Phase4PairedTrialSpec same_run = H4096Spec(10'100, 4);
  ExpectFinalizationInvariant(
      internal::FinalizePhase4ConfirmatoryH4096OrdinaryTrialArm(
          H2250Spec(10'200, 8), Phase4TrialArmExecution{}, Phase4ExternalResourceObservation{}),
      Phase4PairedTrialErrorCode::kInvalidConfiguration, kPriceAuthority);
  ExpectFinalizationInvariant(
      internal::FinalizePhase4ConfirmatoryH4096SameRunTrialArm(
          H2250Spec(10'100, 4), Phase4TrialArmExecution{}, Phase4ExternalResourceObservation{}),
      Phase4PairedTrialErrorCode::kInvalidConfiguration, kPriceAuthority);

  Phase4PairedTrialSpec ordinary_budget_drift = ordinary;
  ++ordinary_budget_drift.baseline_config.maximum_sweeps;
  ExpectFinalizationInvariant(
      internal::FinalizePhase4ConfirmatoryH4096OrdinaryTrialArm(
          ordinary_budget_drift, Phase4TrialArmExecution{}, Phase4ExternalResourceObservation{}),
      Phase4PairedTrialErrorCode::kInvalidConfiguration, kCanonicalBudget);
  Phase4PairedTrialSpec same_run_budget_drift = same_run;
  ++same_run_budget_drift.candidate_session_config.maximum_regeneration_epochs;
  ExpectFinalizationInvariant(
      internal::FinalizePhase4ConfirmatoryH4096SameRunTrialArm(
          same_run_budget_drift, Phase4TrialArmExecution{}, Phase4ExternalResourceObservation{}),
      Phase4PairedTrialErrorCode::kInvalidConfiguration, kCanonicalBudget);

  ExpectFinalizationInvariant(
      internal::FinalizePhase4ConfirmatoryH4096OrdinaryTrialArm(
          same_run, Phase4TrialArmExecution{}, Phase4ExternalResourceObservation{}),
      Phase4PairedTrialErrorCode::kInvalidConfiguration, kOrdinaryScope);
  ExpectFinalizationInvariant(
      internal::FinalizePhase4ConfirmatoryH4096SameRunTrialArm(ordinary, Phase4TrialArmExecution{},
                                                               Phase4ExternalResourceObservation{}),
      Phase4PairedTrialErrorCode::kInvalidConfiguration, kSameRunScope);

  Phase4TrialArmExecution ordinary_cell_drift;
  ordinary_cell_drift.semantics =
      AssociatedSemantics(ordinary, Phase4TrialArm::kSequentialBaseline);
  ++ordinary_cell_drift.semantics.case_id;
  ExpectFinalizationInvariant(
      internal::FinalizePhase4ConfirmatoryH4096OrdinaryTrialArm(
          ordinary, std::move(ordinary_cell_drift), Phase4ExternalResourceObservation{}),
      Phase4PairedTrialErrorCode::kMeasurementAssociation, kAssociation);

  Phase4TrialArmExecution same_run_cell_drift;
  same_run_cell_drift.semantics =
      AssociatedSemantics(same_run, Phase4TrialArm::kReusableCandidateAllocation);
  ++same_run_cell_drift.semantics.requested_pool_size;
  ExpectFinalizationInvariant(
      internal::FinalizePhase4ConfirmatoryH4096SameRunTrialArm(
          same_run, std::move(same_run_cell_drift), Phase4ExternalResourceObservation{}),
      Phase4PairedTrialErrorCode::kMeasurementAssociation, kAssociation);
}

TEST(Phase4H4096RawEntrypointFirewallTest,
     ProducerRejectsChecksumValidCellsWithoutASuccessfulH4096ArmWitness) {
  constexpr std::string_view kMissingWorker =
      "/nonexistent/apgar-phase4-h4096-serializer-firewall-worker";
  constexpr std::string_view kUnreadFixtureToken =
      "apgar-phase4-h4096-serializer-firewall-must-not-be-read.kicad_pcb";
  constexpr std::string_view kCleanCommit = "1111111111111111111111111111111111111111";

  // The worker cannot exec, so neither controller can read the fixture token or
  // reach a case, preparer, or allocator. Both still return checksum-valid Raw
  // total-attempt state whose only missing publication witness is a successful
  // H=4096 arm record.
  Phase4IsolatedCellExecution ordinary_execution = internal::RunPhase4ConfirmatoryH4096OrdinaryCell(
      CanonicalCell(10'200, 8), kMissingWorker, kUnreadFixtureToken);
  ASSERT_TRUE(std::holds_alternative<Phase4IsolatedCellResult>(ordinary_execution));
  const Phase4IsolatedCellResult& ordinary = std::get<Phase4IsolatedCellResult>(ordinary_execution);
  ExpectNoSuccessfulArmWitness(ordinary);
  EXPECT_TRUE(SerializePhase4IsolatedCellJsonV1(ordinary, kCleanCommit, true, false).has_value());
  EXPECT_FALSE(internal::SerializePhase4ConfirmatoryH4096OrdinaryCellJsonV1(ordinary, kCleanCommit,
                                                                            true, false)
                   .has_value());

  Phase4IsolatedCellWithSameRunDecisionTelemetryExecutionV1 same_run_execution =
      internal::RunPhase4ConfirmatoryH4096SameRunCell(CanonicalCell(10'100, 4), kMissingWorker,
                                                      kUnreadFixtureToken);
  ASSERT_TRUE(std::holds_alternative<Phase4TrialHarnessError>(same_run_execution));
  const Phase4TrialHarnessError& same_run_error =
      std::get<Phase4TrialHarnessError>(same_run_execution);
  ASSERT_TRUE(same_run_error.raw_cell.has_value());
  ExpectNoSuccessfulArmWitness(*same_run_error.raw_cell);
  EXPECT_TRUE(
      SerializePhase4SameRunIsolatedCellJsonV2(*same_run_error.raw_cell, kCleanCommit, true, false)
          .has_value());
  EXPECT_FALSE(internal::SerializePhase4ConfirmatoryH4096SameRunCellJsonV2(
                   *same_run_error.raw_cell, kCleanCommit, true, false)
                   .has_value());
}

}  // namespace
}  // namespace apgar::benchmark
