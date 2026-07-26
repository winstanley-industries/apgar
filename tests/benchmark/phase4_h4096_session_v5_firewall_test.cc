#include <array>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string_view>
#include <utility>
#include <variant>

#include "apgar/benchmark/phase4_paired_trial.h"
#include "src/benchmark/phase4_confirmatory_h4096_exact_small_snapshot_internal.h"
#include "src/benchmark/phase4_confirmatory_h4096_execution_internal.h"
#include "src/benchmark/phase4_h4096_session_v5_canonical_budget_internal.h"
#include "src/benchmark/phase4_paired_trial_internal.h"
#include "tests/support/google_test.h"

namespace apgar::benchmark {
namespace {

[[nodiscard]] Phase4CanonicalCellConfig CanonicalCell(std::uint32_t case_id,
                                                      std::uint32_t pool_size) {
  Phase4CanonicalCellConfig cell;
  cell.case_id = case_id;
  cell.requested_pool_size = pool_size;
  cell.preparation_worker_count = kPhase4CanonicalPreparationWorkersV1;
  cell.repetitions = kPhase4CanonicalRepetitionsV1;
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

[[nodiscard]] Phase4PairedTrialSpec SessionV5Spec(std::uint32_t case_id, std::uint32_t pool_size) {
  return Built(internal::BuildPhase4CanonicalTrialSpecForCorpusV2H4096SessionV5(
      CanonicalCell(case_id, pool_size), 0, Phase4TrialOrder::kBaselineFirst));
}

void ExpectError(const std::optional<Phase4PairedTrialError>& error,
                 Phase4PairedTrialErrorCode code, std::string_view invariant_id) {
  ASSERT_TRUE(error.has_value());
  EXPECT_EQ(error->code, code);
  EXPECT_EQ(error->invariant_id, invariant_id);
}

template <typename Result>
void ExpectFailure(const Result& result, Phase4PairedTrialErrorCode code,
                   std::string_view invariant_id) {
  ASSERT_TRUE(std::holds_alternative<Phase4TrialArmFailure>(result));
  const Phase4PairedTrialError& error = std::get<Phase4TrialArmFailure>(result).summary;
  EXPECT_EQ(error.code, code);
  EXPECT_EQ(error.invariant_id, invariant_id);
}

TEST(Phase4H4096SessionV5FirewallTest, ExistingPureAuthoritiesDoNotAuthorizeSessionV5) {
  constexpr std::string_view kSessionAuthority = "P4PAIR-CORPUS-V2-SESSION-AUTHORITY-001";
  constexpr std::string_view kFrozenRawSchema = "P4PAIR-SCHEMA-001";
  const Phase4PairedTrialSpec ordinary = SessionV5Spec(10'200, 8);
  const Phase4PairedTrialSpec same_run = SessionV5Spec(10'100, 4);

  for (const Phase4TrialArm arm : std::array{Phase4TrialArm::kSequentialBaseline,
                                             Phase4TrialArm::kReusableCandidateAllocation}) {
    for (const internal::Phase4TrialExecutionAuthority authority :
         std::array{internal::Phase4TrialExecutionAuthority::kCorpusV2H2250,
                    internal::Phase4TrialExecutionAuthority::kCorpusV2H4096}) {
      ExpectError(internal::PreflightPhase4CorpusV2SessionExecutionAuthority(authority, arm),
                  Phase4PairedTrialErrorCode::kUnsupportedSchema, kSessionAuthority);
    }
    ExpectError(internal::PreflightPhase4ConfirmatoryH4096OrdinarySpec(ordinary, arm),
                Phase4PairedTrialErrorCode::kUnsupportedSchema, kFrozenRawSchema);
    ExpectError(internal::PreflightPhase4ConfirmatoryH4096SameRunSpec(same_run, arm),
                Phase4PairedTrialErrorCode::kUnsupportedSchema, kFrozenRawSchema);
  }
}

TEST(Phase4H4096SessionV5FirewallTest,
     ExistingGenericExecutionSurfacesCloseBeforeFixtureOrPreparerAccess) {
  constexpr std::string_view kSessionAuthority = "P4PAIR-CORPUS-V2-SESSION-AUTHORITY-001";
  constexpr std::string_view kMustNotBeRead =
      "apgar-phase4-h4096-session-v5-generic-must-not-be-read.kicad_pcb";
  const Phase4PairedTrialSpec spec = SessionV5Spec(10'100, 4);

  ExpectFailure(ExecutePhase4TrialArmForCorpusV2(Phase4TrialArm::kReusableCandidateAllocation, spec,
                                                 kMustNotBeRead, nullptr),
                Phase4PairedTrialErrorCode::kUnsupportedSchema, kSessionAuthority);
  ExpectFailure(ExecutePhase4TrialArmDiagnosticForCorpusV2(Phase4TrialArm::kSequentialBaseline,
                                                           spec, kMustNotBeRead, nullptr),
                Phase4PairedTrialErrorCode::kUnsupportedSchema, kSessionAuthority);
  ExpectFailure(ExecutePhase4TrialArmWithSameRunTelemetryForCorpusV2(
                    Phase4TrialArm::kSequentialBaseline, spec, kMustNotBeRead, nullptr),
                Phase4PairedTrialErrorCode::kUnsupportedSchema, kSessionAuthority);
  ExpectFailure(ExecutePhase4TrialArmOperationalProfileForCorpusV2(
                    Phase4TrialArm::kSequentialBaseline, spec, kMustNotBeRead, nullptr),
                Phase4PairedTrialErrorCode::kUnsupportedSchema, kSessionAuthority);
  ExpectFailure(ExecutePhase4TrialArmReplayAuthorityForCorpusV2(Phase4TrialArm::kSequentialBaseline,
                                                                spec, kMustNotBeRead, nullptr),
                Phase4PairedTrialErrorCode::kUnsupportedSchema, kSessionAuthority);
  ExpectFailure(ExecutePhase4CandidatePoolSnapshotForCorpusV2(spec, kMustNotBeRead, nullptr),
                Phase4PairedTrialErrorCode::kUnsupportedSchema, kSessionAuthority);
}

TEST(Phase4H4096SessionV5FirewallTest, FrozenH4096RawSurfacesCloseBeforeFixtureOrPreparerAccess) {
  constexpr std::string_view kFrozenRawSchema = "P4PAIR-SCHEMA-001";
  constexpr std::string_view kMustNotBeRead =
      "apgar-phase4-h4096-session-v5-raw-must-not-be-read.kicad_pcb";
  const Phase4PairedTrialSpec ordinary = SessionV5Spec(10'200, 8);
  const Phase4PairedTrialSpec same_run = SessionV5Spec(10'100, 4);

  ExpectFailure(internal::ExecutePhase4ConfirmatoryH4096OrdinaryTrialArm(
                    Phase4TrialArm::kSequentialBaseline, ordinary, kMustNotBeRead, nullptr),
                Phase4PairedTrialErrorCode::kUnsupportedSchema, kFrozenRawSchema);
  ExpectFailure(internal::ExecutePhase4ConfirmatoryH4096OrdinaryTrialArmDiagnostic(
                    Phase4TrialArm::kSequentialBaseline, ordinary, kMustNotBeRead, nullptr),
                Phase4PairedTrialErrorCode::kUnsupportedSchema, kFrozenRawSchema);
  ExpectFailure(internal::ExecutePhase4ConfirmatoryH4096OrdinaryTrialArmOperationalProfile(
                    Phase4TrialArm::kSequentialBaseline, ordinary, kMustNotBeRead, nullptr),
                Phase4PairedTrialErrorCode::kUnsupportedSchema, kFrozenRawSchema);
  ExpectFailure(internal::ExecutePhase4ConfirmatoryH4096OrdinaryTrialArmReplayAuthority(
                    Phase4TrialArm::kSequentialBaseline, ordinary, kMustNotBeRead, nullptr),
                Phase4PairedTrialErrorCode::kUnsupportedSchema, kFrozenRawSchema);
  ExpectFailure(internal::ExecutePhase4ConfirmatoryH4096SameRunTrialArmForOperationalWarmup(
                    Phase4TrialArm::kSequentialBaseline, same_run, kMustNotBeRead, nullptr),
                Phase4PairedTrialErrorCode::kUnsupportedSchema, kFrozenRawSchema);
  ExpectFailure(internal::ExecutePhase4ConfirmatoryH4096SameRunTrialArmDiagnostic(
                    Phase4TrialArm::kSequentialBaseline, same_run, kMustNotBeRead, nullptr),
                Phase4PairedTrialErrorCode::kUnsupportedSchema, kFrozenRawSchema);
  ExpectFailure(internal::ExecutePhase4ConfirmatoryH4096SameRunTrialArm(
                    Phase4TrialArm::kSequentialBaseline, same_run, kMustNotBeRead, nullptr),
                Phase4PairedTrialErrorCode::kUnsupportedSchema, kFrozenRawSchema);
  ExpectFailure(internal::ExecutePhase4ConfirmatoryH4096SameRunTrialArmOperationalProfile(
                    Phase4TrialArm::kSequentialBaseline, same_run, kMustNotBeRead, nullptr),
                Phase4PairedTrialErrorCode::kUnsupportedSchema, kFrozenRawSchema);
  ExpectFailure(internal::ExecutePhase4ConfirmatoryH4096SameRunTrialArmReplayAuthority(
                    Phase4TrialArm::kSequentialBaseline, same_run, kMustNotBeRead, nullptr),
                Phase4PairedTrialErrorCode::kUnsupportedSchema, kFrozenRawSchema);
  ExpectFailure(internal::ExecutePhase4ConfirmatoryH4096SameRunCandidatePoolSnapshot(
                    same_run, kMustNotBeRead, nullptr),
                Phase4PairedTrialErrorCode::kUnsupportedSchema, kFrozenRawSchema);
}

}  // namespace
}  // namespace apgar::benchmark
