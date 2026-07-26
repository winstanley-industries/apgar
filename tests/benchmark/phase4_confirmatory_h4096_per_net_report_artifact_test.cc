#include <array>
#include <cstdlib>
#include <memory>
#include <string>
#include <utility>
#include <variant>

#include "apgar/allocator/cpu_candidate_pool_preparation.h"
#include "apgar/benchmark/phase4_per_net_report_artifact.h"
#include "src/benchmark/phase4_confirmatory_h4096_per_net_report_internal.h"
#include "src/benchmark/phase4_h4096_canonical_budget_internal.h"
#include "src/benchmark/phase4_paired_trial_internal.h"
#include "tests/support/google_test.h"

namespace apgar::benchmark {
namespace {

constexpr std::string_view kCommit = "0123456789abcdef0123456789abcdef01234567";

template <typename Value, typename Error>
[[nodiscard]] Value ValueOf(std::variant<Value, Error> result) {
  EXPECT_TRUE(std::holds_alternative<Value>(result));
  if (!std::holds_alternative<Value>(result)) {
    std::abort();
  }
  return std::get<Value>(std::move(result));
}

[[nodiscard]] Phase4CanonicalCellConfig CanonicalCell() {
  Phase4CanonicalCellConfig cell;
  cell.case_id = 10'200;
  cell.requested_pool_size = 8;
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

[[nodiscard]] std::unique_ptr<allocator::PersistentCpuCandidatePoolPreparer> Preparer() {
  return ValueOf<std::unique_ptr<allocator::PersistentCpuCandidatePoolPreparer>>(
      allocator::CreatePersistentCpuCandidatePoolPreparer(
          {.worker_count = kPhase4CanonicalPreparationWorkersV1}));
}

[[nodiscard]] std::array<Phase4TrialArmDiagnosticExecutionV1, 2> H4096Diagnostics(
    const Phase4PairedTrialSpec& spec) {
  Phase4TrialArmDiagnosticExecutionV1 baseline = ValueOf<Phase4TrialArmDiagnosticExecutionV1>(
      internal::ExecutePhase4ConfirmatoryH4096OrdinaryTrialArmDiagnostic(
          Phase4TrialArm::kSequentialBaseline, spec, {}));
  std::unique_ptr<allocator::PersistentCpuCandidatePoolPreparer> preparer = Preparer();
  Phase4TrialArmDiagnosticExecutionV1 candidate = ValueOf<Phase4TrialArmDiagnosticExecutionV1>(
      internal::ExecutePhase4ConfirmatoryH4096OrdinaryTrialArmDiagnostic(
          Phase4TrialArm::kReusableCandidateAllocation, spec, {}, preparer.get()));
  return {std::move(baseline), std::move(candidate)};
}

[[nodiscard]] Phase4PerNetReportRawReferenceV1 RawReference(
    const std::array<Phase4TrialArmDiagnosticExecutionV1, 2>& diagnostics) {
  return {
      .repetition_index = 0,
      .execution_order = Phase4TrialOrder::kBaselineFirst,
      .pair_attempt_checksum = 101,
      .paired_semantic_checksum = 103,
      .paired_artifact_checksum = 107,
      .baseline_semantic_checksum = diagnostics[0].semantics.semantic_checksum,
      .baseline_arm_artifact_checksum = 109,
      .candidate_semantic_checksum = diagnostics[1].semantics.semantic_checksum,
      .candidate_arm_artifact_checksum = 113,
  };
}

[[nodiscard]] Phase4PerNetReportArtifactV1 H4096Artifact() {
  const Phase4CanonicalCellConfig cell = CanonicalCell();
  const Phase4PairedTrialSpec spec =
      ValueOf<Phase4PairedTrialSpec>(internal::BuildPhase4CanonicalTrialSpecForCorpusV2H4096(
          cell, 0, Phase4TrialOrder::kBaselineFirst));
  auto diagnostics = H4096Diagnostics(spec);
  const Phase4PerNetReportRawReferenceV1 raw = RawReference(diagnostics);
  constexpr std::uint64_t kRawArtifactChecksum = 127;
  return ValueOf<Phase4PerNetReportArtifactV1>(
      internal::BuildPhase4ConfirmatoryH4096OrdinaryPerNetReportArtifact(
          cell, kCommit, true, false, ComputePhase4CanonicalCellPlanChecksumForCorpusV2(cell),
          kRawArtifactChecksum,
          ComputePhase4SourceEnvelopeChecksumV1(kPhase4TrialWireSchemaVersion, kCommit, true, false,
                                                kRawArtifactChecksum),
          raw, std::move(diagnostics), {}));
}

[[nodiscard]] Phase4PerNetReportArtifactError RejectedH4096(
    const Phase4PerNetReportArtifactV1& artifact) {
  auto result = internal::ValidatePhase4ConfirmatoryH4096OrdinaryPerNetReportArtifact(artifact, {});
  EXPECT_TRUE(std::holds_alternative<Phase4PerNetReportArtifactError>(result));
  if (!std::holds_alternative<Phase4PerNetReportArtifactError>(result)) {
    std::abort();
  }
  return std::get<Phase4PerNetReportArtifactError>(std::move(result));
}

void Reauthenticate(Phase4PerNetReportArtifactV1* artifact) {
  artifact->artifact_checksum = ComputePhase4PerNetReportArtifactChecksumV1(*artifact);
  artifact->source_envelope_checksum = ComputePhase4PerNetReportSourceEnvelopeChecksumV1(
      artifact->source_commit, artifact->source_stamped, artifact->source_tree_dirty,
      artifact->artifact_checksum);
}

TEST(Phase4ConfirmatoryH4096PerNetReportArtifactTest,
     BuildsDeterministicDiagnosticOnlyCompanionForExactOrdinaryScope) {
  const Phase4PerNetReportArtifactV1 artifact = H4096Artifact();
  EXPECT_TRUE(std::holds_alternative<std::monostate>(
      internal::ValidatePhase4ConfirmatoryH4096OrdinaryPerNetReportArtifact(artifact, {})));
  EXPECT_FALSE(artifact.decision_eligible);
  EXPECT_EQ(artifact.raw_wire_schema_version, kPhase4TrialWireSchemaVersion);
  EXPECT_EQ(artifact.config.case_id, 10'200U);
  EXPECT_EQ(artifact.config.requested_pool_size, 8U);
  EXPECT_EQ(artifact.arms[0].diagnostic.telemetry.per_net.size(), 64U);
  EXPECT_EQ(artifact.arms[1].diagnostic.telemetry.per_net.size(), 64U);
  EXPECT_EQ(SerializePhase4PerNetReportArtifactJsonV1(artifact),
            SerializePhase4PerNetReportArtifactJsonV1(artifact));
}

TEST(Phase4ConfirmatoryH4096PerNetReportArtifactTest,
     KeepsH2250ScopeAndPriceAuthoritiesNonSubstitutable) {
  Phase4PerNetReportArtifactV1 h4096 = H4096Artifact();
  auto h2250_validation = ValidatePhase4PerNetReportArtifactForCorpusV2(h4096, {});
  ASSERT_TRUE(std::holds_alternative<Phase4PerNetReportArtifactError>(h2250_validation));
  EXPECT_EQ(std::get<Phase4PerNetReportArtifactError>(h2250_validation).invariant_id,
            "P4REPORT-ARM-IDENTITY-001");

  h4096.config.requested_pool_size = 4;
  h4096.raw_cell_plan_checksum = ComputePhase4CanonicalCellPlanChecksumForCorpusV2(h4096.config);
  Reauthenticate(&h4096);
  EXPECT_EQ(RejectedH4096(h4096).invariant_id, "P4REPORT-H4096-SCOPE-001");

  const Phase4PairedTrialSpec h2250_spec =
      ValueOf<Phase4PairedTrialSpec>(BuildPhase4CanonicalTrialSpecForCorpusV2(
          CanonicalCell(), 0, Phase4TrialOrder::kBaselineFirst));
  auto rejected = internal::ExecutePhase4ConfirmatoryH4096OrdinaryTrialArmDiagnostic(
      Phase4TrialArm::kSequentialBaseline, h2250_spec, {});
  ASSERT_TRUE(std::holds_alternative<Phase4TrialArmFailure>(rejected));
  EXPECT_EQ(std::get<Phase4TrialArmFailure>(rejected).summary.invariant_id,
            "P4PAIR-CORPUS-V2-H4096-BUDGET-AUTHORITY-001");
}

}  // namespace
}  // namespace apgar::benchmark
