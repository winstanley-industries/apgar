#include <array>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <string>
#include <utility>
#include <variant>

#include "apgar/benchmark/phase4_paired_trial.h"
#include "src/benchmark/phase4_h4096_canonical_budget_internal.h"
#include "src/benchmark/phase4_paired_trial_internal.h"
#include "tests/support/google_test.h"

namespace apgar::benchmark {
namespace {

template <typename Value>
[[nodiscard]] Phase4PairedTrialError Rejection(std::variant<Value, Phase4TrialArmFailure> result) {
  EXPECT_TRUE(std::holds_alternative<Phase4TrialArmFailure>(result));
  if (!std::holds_alternative<Phase4TrialArmFailure>(result)) {
    std::abort();
  }
  return std::get<Phase4TrialArmFailure>(std::move(result)).summary;
}

[[nodiscard]] Phase4PairedTrialSpec H2250Spec() {
  Phase4CanonicalCellConfig cell;
  cell.case_id = 10'100;
  cell.requested_pool_size = 4;
  cell.repetitions = 1;
  cell.maximum_setup_elapsed_nanoseconds = 1;
  cell.external_budget = {
      .maximum_prepared_elapsed_nanoseconds = 1,
      .maximum_cold_elapsed_nanoseconds = 1,
      .maximum_address_space_bytes = 1,
      .maximum_peak_host_bytes = 1,
  };
  Phase4CanonicalSpecResult built =
      BuildPhase4CanonicalTrialSpecForCorpusV2(cell, 0, Phase4TrialOrder::kBaselineFirst);
  EXPECT_TRUE(std::holds_alternative<Phase4PairedTrialSpec>(built));
  if (!std::holds_alternative<Phase4PairedTrialSpec>(built)) {
    std::abort();
  }
  Phase4PairedTrialSpec spec = std::get<Phase4PairedTrialSpec>(std::move(built));
  // If the authority guard regresses, case construction must still fail
  // closed before an allocator arm can begin.
  spec.corpus_limits.maximum_board_entities = 0;
  return spec;
}

void ExpectSessionClosure(const Phase4PairedTrialError& error) {
  EXPECT_EQ(error.code, Phase4PairedTrialErrorCode::kUnsupportedSchema);
  EXPECT_EQ(error.invariant_id, "P4PAIR-CORPUS-V2-SESSION-AUTHORITY-001");
}

TEST(Phase4CorpusV2BudgetFirewallTest,
     FrozenH2250PriceFieldsRemainBudgetIdentityButExecutionClosesFirst) {
  const Phase4PairedTrialSpec canonical = H2250Spec();
  const std::uint64_t canonical_budget =
      internal::ComputePhase4CanonicalAlgorithmBudgetChecksumV1(canonical);
  EXPECT_EQ(canonical.baseline_config.price_config.present_step_per_overuse_unit,
            internal::kPhase4CorpusV2ProtocolV1PresentStepPerOveruseUnit);
  EXPECT_EQ(canonical.baseline_config.price_config.history_step_per_overuse_unit,
            internal::kPhase4CorpusV2ProtocolV1HistoryStepPerOveruseUnit);
  const auto expect_closed_drift = [canonical_budget](Phase4PairedTrialSpec spec) {
    EXPECT_NE(internal::ComputePhase4CanonicalAlgorithmBudgetChecksumV1(spec), canonical_budget);
    ExpectSessionClosure(Rejection(ExecutePhase4TrialArmForCorpusV2(
        Phase4TrialArm::kSequentialBaseline, spec, "must-not-be-read")));
  };

  for (const std::uint64_t history : std::array<std::uint64_t, 6>{
           0,
           1,
           internal::kPhase4CorpusV2ProtocolV1HistoryStepPerOveruseUnit - 1,
           internal::kPhase4CorpusV2ProtocolV1HistoryStepPerOveruseUnit + 1,
           internal::kPhase4CorpusV2H4096HistoryStepPerOveruseUnit,
           std::numeric_limits<std::uint64_t>::max(),
       }) {
    Phase4PairedTrialSpec spec = H2250Spec();
    spec.baseline_config.price_config.history_step_per_overuse_unit = history;
    spec.candidate_session_config.price_config = spec.baseline_config.price_config;
    expect_closed_drift(std::move(spec));
  }
  for (const std::uint64_t present :
       std::array<std::uint64_t, 3>{0, 2, std::numeric_limits<std::uint64_t>::max()}) {
    Phase4PairedTrialSpec spec = H2250Spec();
    spec.baseline_config.price_config.present_step_per_overuse_unit = present;
    spec.candidate_session_config.price_config = spec.baseline_config.price_config;
    expect_closed_drift(std::move(spec));
  }

  Phase4PairedTrialSpec unequal_history = H2250Spec();
  unequal_history.candidate_session_config.price_config.history_step_per_overuse_unit =
      internal::kPhase4CorpusV2H4096HistoryStepPerOveruseUnit;
  expect_closed_drift(std::move(unequal_history));

  Phase4PairedTrialSpec unequal_present = H2250Spec();
  unequal_present.candidate_session_config.price_config.present_step_per_overuse_unit = 2;
  expect_closed_drift(std::move(unequal_present));
}

TEST(Phase4CorpusV2BudgetFirewallTest,
     PureH4096PreflightSurvivesButSessionClosurePrecedesEveryExecutionSurface) {
  Phase4CanonicalCellConfig cell;
  cell.case_id = 10'100;
  cell.requested_pool_size = 4;
  cell.repetitions = 1;
  cell.maximum_setup_elapsed_nanoseconds = 1;
  cell.external_budget = {
      .maximum_prepared_elapsed_nanoseconds = 1,
      .maximum_cold_elapsed_nanoseconds = 1,
      .maximum_address_space_bytes = 1,
      .maximum_peak_host_bytes = 1,
  };
  Phase4CanonicalSpecResult built = internal::BuildPhase4CanonicalTrialSpecForCorpusV2H4096(
      cell, 0, Phase4TrialOrder::kBaselineFirst);
  ASSERT_TRUE(std::holds_alternative<Phase4PairedTrialSpec>(built));
  Phase4PairedTrialSpec spec = std::get<Phase4PairedTrialSpec>(std::move(built));
  spec.corpus_limits.maximum_board_entities = 0;

  EXPECT_FALSE(internal::PreflightPhase4ConfirmatoryH4096SameRunSpec(
                   spec, Phase4TrialArm::kSequentialBaseline)
                   .has_value());
  EXPECT_FALSE(internal::PreflightPhase4ConfirmatoryH4096SameRunSpec(
                   spec, Phase4TrialArm::kReusableCandidateAllocation)
                   .has_value());

  ExpectSessionClosure(Rejection(ExecutePhase4TrialArmForCorpusV2(
      Phase4TrialArm::kReusableCandidateAllocation, spec, "must-not-be-read", nullptr)));
  ExpectSessionClosure(Rejection(ExecutePhase4TrialArmDiagnosticForCorpusV2(
      Phase4TrialArm::kSequentialBaseline, spec, "must-not-be-read")));
  ExpectSessionClosure(Rejection(ExecutePhase4TrialArmWithSameRunTelemetryForCorpusV2(
      Phase4TrialArm::kSequentialBaseline, spec, "must-not-be-read")));
  ExpectSessionClosure(Rejection(ExecutePhase4TrialArmOperationalProfileForCorpusV2(
      Phase4TrialArm::kSequentialBaseline, spec, "must-not-be-read")));
  ExpectSessionClosure(Rejection(ExecutePhase4TrialArmReplayAuthorityForCorpusV2(
      Phase4TrialArm::kSequentialBaseline, spec, "must-not-be-read")));
  ExpectSessionClosure(
      Rejection(ExecutePhase4CandidatePoolSnapshotForCorpusV2(spec, "must-not-be-read", nullptr)));
}

}  // namespace
}  // namespace apgar::benchmark
