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

void ExpectAuthorityRejection(const Phase4PairedTrialError& error, std::uint64_t required,
                              std::uint64_t configured) {
  EXPECT_EQ(error.code, Phase4PairedTrialErrorCode::kInvalidConfiguration);
  EXPECT_EQ(error.invariant_id, "P4PAIR-CORPUS-V2-BUDGET-AUTHORITY-001");
  EXPECT_EQ(error.required, required);
  EXPECT_EQ(error.configured, configured);
}

TEST(Phase4CorpusV2BudgetFirewallTest, RejectsEveryNonProtocolV1PriceValue) {
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
    ExpectAuthorityRejection(Rejection(ExecutePhase4TrialArmForCorpusV2(
                                 Phase4TrialArm::kSequentialBaseline, spec, "must-not-be-read")),
                             internal::kPhase4CorpusV2ProtocolV1HistoryStepPerOveruseUnit, history);
  }
  for (const std::uint64_t present :
       std::array<std::uint64_t, 3>{0, 2, std::numeric_limits<std::uint64_t>::max()}) {
    Phase4PairedTrialSpec spec = H2250Spec();
    spec.baseline_config.price_config.present_step_per_overuse_unit = present;
    spec.candidate_session_config.price_config = spec.baseline_config.price_config;
    ExpectAuthorityRejection(Rejection(ExecutePhase4TrialArmForCorpusV2(
                                 Phase4TrialArm::kSequentialBaseline, spec, "must-not-be-read")),
                             internal::kPhase4CorpusV2ProtocolV1PresentStepPerOveruseUnit, present);
  }

  Phase4PairedTrialSpec unequal_history = H2250Spec();
  unequal_history.candidate_session_config.price_config.history_step_per_overuse_unit =
      internal::kPhase4CorpusV2H4096HistoryStepPerOveruseUnit;
  ExpectAuthorityRejection(
      Rejection(ExecutePhase4TrialArmForCorpusV2(Phase4TrialArm::kSequentialBaseline,
                                                 unequal_history, "must-not-be-read")),
      internal::kPhase4CorpusV2ProtocolV1HistoryStepPerOveruseUnit,
      internal::kPhase4CorpusV2H4096HistoryStepPerOveruseUnit);

  Phase4PairedTrialSpec unequal_present = H2250Spec();
  unequal_present.candidate_session_config.price_config.present_step_per_overuse_unit = 2;
  ExpectAuthorityRejection(
      Rejection(ExecutePhase4TrialArmForCorpusV2(Phase4TrialArm::kSequentialBaseline,
                                                 unequal_present, "must-not-be-read")),
      internal::kPhase4CorpusV2ProtocolV1PresentStepPerOveruseUnit, 2);
}

TEST(Phase4CorpusV2BudgetFirewallTest, BlocksH4096BeforeEveryExistingExecutionSurface) {
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

  const auto expect_h4096 = [](const Phase4PairedTrialError& error) {
    ExpectAuthorityRejection(error, internal::kPhase4CorpusV2ProtocolV1HistoryStepPerOveruseUnit,
                             internal::kPhase4CorpusV2H4096HistoryStepPerOveruseUnit);
  };
  expect_h4096(Rejection(ExecutePhase4TrialArmForCorpusV2(
      Phase4TrialArm::kReusableCandidateAllocation, spec, "must-not-be-read", nullptr)));
  expect_h4096(Rejection(ExecutePhase4TrialArmDiagnosticForCorpusV2(
      Phase4TrialArm::kSequentialBaseline, spec, "must-not-be-read")));
  expect_h4096(Rejection(ExecutePhase4TrialArmWithSameRunTelemetryForCorpusV2(
      Phase4TrialArm::kSequentialBaseline, spec, "must-not-be-read")));
  expect_h4096(Rejection(ExecutePhase4TrialArmOperationalProfileForCorpusV2(
      Phase4TrialArm::kSequentialBaseline, spec, "must-not-be-read")));
  expect_h4096(Rejection(ExecutePhase4TrialArmReplayAuthorityForCorpusV2(
      Phase4TrialArm::kSequentialBaseline, spec, "must-not-be-read")));
  expect_h4096(
      Rejection(ExecutePhase4CandidatePoolSnapshotForCorpusV2(spec, "must-not-be-read", nullptr)));
}

}  // namespace
}  // namespace apgar::benchmark
