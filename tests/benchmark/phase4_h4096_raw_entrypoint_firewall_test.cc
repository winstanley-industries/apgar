#include <array>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string_view>
#include <utility>
#include <variant>

#include "apgar/benchmark/phase4_paired_trial.h"
#include "apgar/benchmark/phase4_representative_corpus.h"
#include "src/allocator/negotiated_prices_internal.h"
#include "src/benchmark/phase4_confirmatory_h4096_execution_internal.h"
#include "src/benchmark/phase4_h4096_canonical_budget_internal.h"
#include "src/benchmark/phase4_paired_trial_internal.h"
#include "tests/support/google_test.h"

namespace apgar::benchmark {
namespace {

// This helper reconstructs only the frozen configuration preimage.
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

[[nodiscard]] Phase4TrialArmSemantics AssociatedSemantics(const Phase4PairedTrialSpec& spec,
                                                          Phase4TrialArm arm) {
  Phase4RepresentativeCaseResult built =
      BuildPhase4RepresentativeCaseV2(spec.case_id, {}, spec.corpus_limits);
  EXPECT_TRUE(std::holds_alternative<Phase4RepresentativeCase>(built));
  if (!std::holds_alternative<Phase4RepresentativeCase>(built)) {
    std::abort();
  }
  const Phase4RepresentativeCase representative =
      std::get<Phase4RepresentativeCase>(std::move(built));
  const Phase4RouteOpportunity opportunity{
      .route_queries = spec.baseline_config.limits.maximum_route_queries,
      .route_work_units = spec.baseline_config.limits.maximum_total_route_work_units,
  };
  const std::uint32_t terminal_rounds =
      spec.candidate_session_config.schedules.back().maximum_selection_rounds;
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
  semantics.descriptor_fingerprint = FingerprintPhase4CaseDescriptorV2(representative.descriptor);
  semantics.case_checksum = representative.case_checksum;
  semantics.board_content_hash = representative.board.content_hash();
  semantics.workload_checksum = representative.workload.workload_checksum();
  semantics.capacity_model_checksum =
      allocator::internal::RecomputeResourceCapacityModelChecksumV1(representative.capacities);
  semantics.budget_checksum = internal::ComputePhase4PairedBudgetChecksumForAuthorityV1(
      Phase4RepresentativeCorpusAuthority::kV2, spec, opportunity,
      representative.descriptor.requested_net_count, columns_per_epoch, terminal_rounds);
  semantics.workload_net_count = representative.descriptor.requested_net_count;
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
  semantics.algorithm_session_checksum = 101;
  semantics.terminal_reason = Phase4NormalizedTerminalReason::kNoAdmissibleCandidate;
  semantics.outcome.no_candidate_net_count = representative.workload.nets().size();
  semantics.outcome.world_checksum = 103;
  if (arm == Phase4TrialArm::kReusableCandidateAllocation) {
    semantics.preparation_checksum = 107;
    semantics.final_pool_manifest_checksum = 109;
    semantics.final_rejection_manifest_checksum = 113;
    semantics.candidate_outcome_source = Phase4CandidateOutcomeSource::kPreferredMultiWorld;
  }
  semantics.semantic_checksum = internal::ComputePhase4TrialArmSemanticChecksumV1(semantics);
  EXPECT_FALSE(internal::ValidatePhase4TrialArmSemanticsForAuthorityV1(
                   Phase4RepresentativeCorpusAuthority::kV2, semantics)
                   .has_value());
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

TEST(Phase4H4096RawEntrypointFirewallTest,
     OperationalSurfacesRejectH2250BeforeWarmupProfileOrReplay) {
  constexpr std::string_view kPriceAuthority = "P4PAIR-CORPUS-V2-H4096-BUDGET-AUTHORITY-001";
  constexpr std::string_view kMustNotBeRead =
      "apgar-phase4-h4096-operational-firewall-must-not-be-read.kicad_pcb";
  const Phase4PairedTrialSpec ordinary_h2250 = H2250Spec(10'200, 8);
  const Phase4PairedTrialSpec same_run_h2250 = H2250Spec(10'100, 4);

  const Phase4TrialArmExecutionResult ordinary_warmup =
      internal::ExecutePhase4ConfirmatoryH4096OrdinaryTrialArm(Phase4TrialArm::kSequentialBaseline,
                                                               ordinary_h2250, kMustNotBeRead);
  ASSERT_TRUE(std::holds_alternative<Phase4TrialArmFailure>(ordinary_warmup));
  EXPECT_EQ(std::get<Phase4TrialArmFailure>(ordinary_warmup).summary.invariant_id, kPriceAuthority);

  const Phase4TrialArmOperationalProfileResultV1 ordinary_profile =
      internal::ExecutePhase4ConfirmatoryH4096OrdinaryTrialArmOperationalProfile(
          Phase4TrialArm::kSequentialBaseline, ordinary_h2250, kMustNotBeRead);
  ASSERT_TRUE(std::holds_alternative<Phase4TrialArmFailure>(ordinary_profile));
  EXPECT_EQ(std::get<Phase4TrialArmFailure>(ordinary_profile).summary.invariant_id,
            kPriceAuthority);

  const Phase4TrialArmReplayAuthorityResultV1 ordinary_replay =
      internal::ExecutePhase4ConfirmatoryH4096OrdinaryTrialArmReplayAuthority(
          Phase4TrialArm::kSequentialBaseline, ordinary_h2250, kMustNotBeRead);
  ASSERT_TRUE(std::holds_alternative<Phase4TrialArmFailure>(ordinary_replay));
  EXPECT_EQ(std::get<Phase4TrialArmFailure>(ordinary_replay).summary.invariant_id, kPriceAuthority);

  const Phase4TrialArmExecutionResult warmup =
      internal::ExecutePhase4ConfirmatoryH4096SameRunTrialArmForOperationalWarmup(
          Phase4TrialArm::kSequentialBaseline, same_run_h2250, kMustNotBeRead);
  ASSERT_TRUE(std::holds_alternative<Phase4TrialArmFailure>(warmup));
  EXPECT_EQ(std::get<Phase4TrialArmFailure>(warmup).summary.invariant_id, kPriceAuthority);

  const Phase4TrialArmOperationalProfileResultV1 profile =
      internal::ExecutePhase4ConfirmatoryH4096SameRunTrialArmOperationalProfile(
          Phase4TrialArm::kSequentialBaseline, same_run_h2250, kMustNotBeRead);
  ASSERT_TRUE(std::holds_alternative<Phase4TrialArmFailure>(profile));
  EXPECT_EQ(std::get<Phase4TrialArmFailure>(profile).summary.invariant_id, kPriceAuthority);

  const Phase4TrialArmReplayAuthorityResultV1 replay =
      internal::ExecutePhase4ConfirmatoryH4096SameRunTrialArmReplayAuthority(
          Phase4TrialArm::kSequentialBaseline, same_run_h2250, kMustNotBeRead);
  ASSERT_TRUE(std::holds_alternative<Phase4TrialArmFailure>(replay));
  EXPECT_EQ(std::get<Phase4TrialArmFailure>(replay).summary.invariant_id, kPriceAuthority);
}

TEST(Phase4H4096RawEntrypointFirewallTest,
     FrozenSessionExecutionSurfacesFailClosedBeforeFixtureOrPreparerAccess) {
  constexpr std::string_view kSessionAuthority = "P4PAIR-CORPUS-V2-SESSION-AUTHORITY-001";
  constexpr std::string_view kMustNotBeRead =
      "apgar-phase4-h4096-frozen-session-must-not-be-read.kicad_pcb";
  const Phase4PairedTrialSpec ordinary = H4096Spec(10'200, 8);
  const Phase4PairedTrialSpec same_run = H4096Spec(10'100, 4);
  const auto expect_closed = [kSessionAuthority](const auto& result) {
    ASSERT_TRUE(std::holds_alternative<Phase4TrialArmFailure>(result));
    const Phase4PairedTrialError& error = std::get<Phase4TrialArmFailure>(result).summary;
    EXPECT_EQ(error.code, Phase4PairedTrialErrorCode::kUnsupportedSchema);
    EXPECT_EQ(error.invariant_id, kSessionAuthority);
  };

  expect_closed(internal::ExecutePhase4ConfirmatoryH4096OrdinaryTrialArm(
      Phase4TrialArm::kSequentialBaseline, ordinary, kMustNotBeRead));
  expect_closed(internal::ExecutePhase4ConfirmatoryH4096OrdinaryTrialArmDiagnostic(
      Phase4TrialArm::kSequentialBaseline, ordinary, kMustNotBeRead));
  expect_closed(internal::ExecutePhase4ConfirmatoryH4096OrdinaryTrialArmOperationalProfile(
      Phase4TrialArm::kSequentialBaseline, ordinary, kMustNotBeRead));
  expect_closed(internal::ExecutePhase4ConfirmatoryH4096OrdinaryTrialArmReplayAuthority(
      Phase4TrialArm::kSequentialBaseline, ordinary, kMustNotBeRead));
  expect_closed(internal::ExecutePhase4ConfirmatoryH4096SameRunTrialArmForOperationalWarmup(
      Phase4TrialArm::kSequentialBaseline, same_run, kMustNotBeRead));
  expect_closed(internal::ExecutePhase4ConfirmatoryH4096SameRunTrialArmDiagnostic(
      Phase4TrialArm::kSequentialBaseline, same_run, kMustNotBeRead));
  expect_closed(internal::ExecutePhase4ConfirmatoryH4096SameRunTrialArm(
      Phase4TrialArm::kSequentialBaseline, same_run, kMustNotBeRead));
  expect_closed(internal::ExecutePhase4ConfirmatoryH4096SameRunTrialArmOperationalProfile(
      Phase4TrialArm::kSequentialBaseline, same_run, kMustNotBeRead));
  expect_closed(internal::ExecutePhase4ConfirmatoryH4096SameRunTrialArmReplayAuthority(
      Phase4TrialArm::kSequentialBaseline, same_run, kMustNotBeRead));
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
     FinalizationRechecksPriceBudgetCarrierThenClosesFrozenSessionAuthority) {
  constexpr std::string_view kPriceAuthority = "P4PAIR-CORPUS-V2-H4096-BUDGET-AUTHORITY-001";
  constexpr std::string_view kCanonicalBudget = "P4PAIR-CORPUS-V2-H4096-CANONICAL-BUDGET-001";
  constexpr std::string_view kOrdinaryScope = "P4PAIR-CORPUS-V2-H4096-ORDINARY-SCOPE-001";
  constexpr std::string_view kSameRunScope = "P4PAIR-CORPUS-V2-H4096-SAME-RUN-SCOPE-001";
  constexpr std::string_view kSessionAuthority = "P4PAIR-CORPUS-V2-SESSION-AUTHORITY-001";

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

  Phase4TrialArmExecution ordinary_execution;
  ordinary_execution.semantics = AssociatedSemantics(ordinary, Phase4TrialArm::kSequentialBaseline);
  ExpectFinalizationInvariant(
      internal::FinalizePhase4ConfirmatoryH4096OrdinaryTrialArm(
          ordinary, std::move(ordinary_execution), Phase4ExternalResourceObservation{}),
      Phase4PairedTrialErrorCode::kUnsupportedSchema, kSessionAuthority);

  Phase4TrialArmExecution same_run_execution;
  same_run_execution.semantics =
      AssociatedSemantics(same_run, Phase4TrialArm::kReusableCandidateAllocation);
  ExpectFinalizationInvariant(
      internal::FinalizePhase4ConfirmatoryH4096SameRunTrialArm(
          same_run, std::move(same_run_execution), Phase4ExternalResourceObservation{}),
      Phase4PairedTrialErrorCode::kUnsupportedSchema, kSessionAuthority);
}

TEST(Phase4H4096RawEntrypointFirewallTest,
     ControllersRejectFrozenSessionBeforeWorkerFixtureOrSerialization) {
  constexpr std::string_view kSessionAuthority = "P4PAIR-CORPUS-V2-SESSION-AUTHORITY-001";
  constexpr std::string_view kMissingWorker =
      "/nonexistent/apgar-phase4-h4096-serializer-firewall-worker";
  constexpr std::string_view kUnreadFixtureToken =
      "apgar-phase4-h4096-serializer-firewall-must-not-be-read.kicad_pcb";

  const Phase4IsolatedCellExecution ordinary_execution =
      internal::RunPhase4ConfirmatoryH4096OrdinaryCell(CanonicalCell(10'200, 8), kMissingWorker,
                                                       kUnreadFixtureToken);
  ASSERT_TRUE(std::holds_alternative<Phase4TrialHarnessError>(ordinary_execution));
  const Phase4TrialHarnessError& ordinary_error =
      std::get<Phase4TrialHarnessError>(ordinary_execution);
  EXPECT_EQ(ordinary_error.invariant_id, kSessionAuthority);
  EXPECT_FALSE(ordinary_error.raw_cell.has_value());

  const Phase4IsolatedCellWithSameRunDecisionTelemetryExecutionV1 same_run_execution =
      internal::RunPhase4ConfirmatoryH4096SameRunCell(CanonicalCell(10'100, 4), kMissingWorker,
                                                      kUnreadFixtureToken);
  ASSERT_TRUE(std::holds_alternative<Phase4TrialHarnessError>(same_run_execution));
  const Phase4TrialHarnessError& same_run_error =
      std::get<Phase4TrialHarnessError>(same_run_execution);
  EXPECT_EQ(same_run_error.invariant_id, kSessionAuthority);
  EXPECT_FALSE(same_run_error.raw_cell.has_value());
}

}  // namespace
}  // namespace apgar::benchmark
