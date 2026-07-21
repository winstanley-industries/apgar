#include "apgar/benchmark/phase4_per_net_report_artifact.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

#include "apgar/allocator/cpu_candidate_pool_preparation.h"
#include "apgar/tooling/runfiles.h"
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

[[nodiscard]] Phase4CanonicalCellConfig Cell(std::uint32_t case_id = 100) {
  Phase4CanonicalCellConfig cell;
  cell.case_id = case_id;
  cell.requested_pool_size = 4;
  cell.preparation_worker_count = 4;
  cell.repetitions = 20;
  cell.maximum_setup_elapsed_nanoseconds = 60'000'000'000ULL;
  cell.external_budget = {
      .maximum_prepared_elapsed_nanoseconds = 120'000'000'000ULL,
      .maximum_cold_elapsed_nanoseconds = 180'000'000'000ULL,
      .maximum_address_space_bytes = 64ULL * 1024ULL * 1024ULL * 1024ULL,
      .maximum_peak_host_bytes = 32ULL * 1024ULL * 1024ULL * 1024ULL,
  };
  return cell;
}

[[nodiscard]] std::string Fixture() {
  return tooling::ReadRunfile("tests/fixtures/phase4_supported_multinet_v1.kicad_pcb")
      .value_or(std::string{});
}

[[nodiscard]] Phase4PerNetReportArtifactV1 Artifact() {
  const Phase4CanonicalCellConfig cell = Cell();
  const Phase4PairedTrialSpec spec = ValueOf<Phase4PairedTrialSpec>(
      BuildPhase4CanonicalTrialSpecV1(cell, 0, Phase4TrialOrder::kBaselineFirst));
  Phase4TrialArmDiagnosticExecutionV1 baseline = ValueOf<Phase4TrialArmDiagnosticExecutionV1>(
      ExecutePhase4TrialArmDiagnosticV1(Phase4TrialArm::kSequentialBaseline, spec, {}));
  std::unique_ptr<allocator::PersistentCpuCandidatePoolPreparer> preparer =
      ValueOf<std::unique_ptr<allocator::PersistentCpuCandidatePoolPreparer>>(
          allocator::CreatePersistentCpuCandidatePoolPreparer({.worker_count = 4}));
  Phase4TrialArmDiagnosticExecutionV1 candidate =
      ValueOf<Phase4TrialArmDiagnosticExecutionV1>(ExecutePhase4TrialArmDiagnosticV1(
          Phase4TrialArm::kReusableCandidateAllocation, spec, {}, preparer.get()));
  const std::uint64_t raw_artifact_checksum = 101;
  const Phase4PerNetReportRawReferenceV1 raw{
      .repetition_index = 0,
      .execution_order = Phase4TrialOrder::kBaselineFirst,
      .pair_attempt_checksum = 103,
      .paired_semantic_checksum = 107,
      .paired_artifact_checksum = 109,
      .baseline_semantic_checksum = baseline.semantics.semantic_checksum,
      .baseline_arm_artifact_checksum = 113,
      .candidate_semantic_checksum = candidate.semantics.semantic_checksum,
      .candidate_arm_artifact_checksum = 127,
  };
  return ValueOf<Phase4PerNetReportArtifactV1>(BuildPhase4PerNetReportArtifactV1(
      cell, kCommit, true, false, ComputePhase4CanonicalCellPlanChecksumV1(cell),
      raw_artifact_checksum,
      ComputePhase4SourceEnvelopeChecksumV1(kPhase4TrialWireSchemaVersion, kCommit, true, false,
                                            raw_artifact_checksum),
      raw, {std::move(baseline), std::move(candidate)}, {}));
}

void Reauthenticate(Phase4PerNetReportArtifactV1* artifact) {
  for (std::size_t index = 0; index < artifact->arms.size(); ++index) {
    Phase4PerNetReportArmArtifactV1& arm = artifact->arms[index];
    arm.diagnostic.semantics.semantic_checksum =
        internal::ComputePhase4TrialArmSemanticChecksumV1(arm.diagnostic.semantics);
    arm.diagnostic.telemetry.associated_semantic_checksum =
        arm.diagnostic.semantics.semantic_checksum;
    arm.diagnostic.telemetry.telemetry_checksum =
        internal::ComputePhase4ArmReportTelemetryChecksumV1(arm.diagnostic.telemetry);
    arm.raw_semantic_checksum = arm.diagnostic.semantics.semantic_checksum;
    if (index == 0) {
      artifact->raw_reference.baseline_semantic_checksum = arm.raw_semantic_checksum;
    } else {
      artifact->raw_reference.candidate_semantic_checksum = arm.raw_semantic_checksum;
    }
  }
  artifact->artifact_checksum = ComputePhase4PerNetReportArtifactChecksumV1(*artifact);
  artifact->source_envelope_checksum = ComputePhase4PerNetReportSourceEnvelopeChecksumV1(
      artifact->source_commit, artifact->source_stamped, artifact->source_tree_dirty,
      artifact->artifact_checksum);
}

[[nodiscard]] Phase4PerNetReportArtifactError Rejected(const Phase4PerNetReportArtifactV1& artifact,
                                                       std::string_view fixture = {}) {
  auto result = ValidatePhase4PerNetReportArtifactV1(artifact, fixture);
  EXPECT_TRUE(std::holds_alternative<Phase4PerNetReportArtifactError>(result));
  if (!std::holds_alternative<Phase4PerNetReportArtifactError>(result)) {
    std::abort();
  }
  return std::get<Phase4PerNetReportArtifactError>(std::move(result));
}

TEST(Phase4PerNetReportArtifactTest, FrozenManifestHasGoldenCoverageAndLiveRosterWitnesses) {
  EXPECT_EQ(ComputePhase4WorkloadNetRosterManifestChecksumV1(),
            kPhase4WorkloadNetRosterManifestChecksumV1);
  EXPECT_EQ(kPhase4WorkloadNetRosterManifestChecksumV1, 3143811343998575433ULL);
  ASSERT_EQ(Phase4WorkloadNetRosterManifestV1().size(), 38U);
  ASSERT_EQ(Phase4WorkloadNetRosterManifestExclusionsV1().size(), 4U);
  const std::string sidecar =
      tooling::ReadRunfile("schemas/benchmark/phase4_workload_net_roster_manifest_v1.json")
          .value_or(std::string{});
  ASSERT_FALSE(sidecar.empty());
  EXPECT_NE(sidecar.find("\"manifest_checksum\": 3143811343998575433"), std::string::npos);
  std::size_t roster_rows = 0;
  for (std::size_t offset = 0;
       (offset = sidecar.find("\"roster_checksum\"", offset)) != std::string::npos; offset += 1) {
    ++roster_rows;
  }
  EXPECT_EQ(roster_rows, 38U);
  EXPECT_NE(sidecar.find("\"case_id\": 3001"), std::string::npos);
  EXPECT_NE(sidecar.find("\"disposition\": \"compiled_work_bound\""), std::string::npos);
  for (const Phase4CaseDescriptor& descriptor : Phase4CaseDescriptorsV1()) {
    const auto successful =
        std::ranges::find(Phase4WorkloadNetRosterManifestV1(), descriptor.case_id,
                          &Phase4WorkloadNetRosterManifestEntryV1::case_id);
    const auto excluded =
        std::ranges::find(Phase4WorkloadNetRosterManifestExclusionsV1(), descriptor.case_id,
                          &Phase4WorkloadNetRosterManifestExclusionV1::case_id);
    ASSERT_NE(successful != Phase4WorkloadNetRosterManifestV1().end(),
              excluded != Phase4WorkloadNetRosterManifestExclusionsV1().end());
    EXPECT_EQ(successful != Phase4WorkloadNetRosterManifestV1().end()
                  ? successful->descriptor_fingerprint
                  : excluded->descriptor_fingerprint,
              FingerprintPhase4CaseDescriptorV1(descriptor));
  }

  for (const std::uint32_t case_id : {100U, 4000U}) {
    Phase4RepresentativeCase representative = ValueOf<Phase4RepresentativeCase>(
        BuildPhase4RepresentativeCaseV1(case_id, case_id == 4000 ? Fixture() : std::string{}));
    const Phase4WorkloadNetRosterManifestEntryV1* frozen =
        FindPhase4WorkloadNetRosterManifestEntryV1(case_id);
    ASSERT_NE(frozen, nullptr);
    EXPECT_EQ(ComputePhase4WorkloadNetRosterChecksumV1(representative), frozen->roster_checksum);
    EXPECT_EQ(representative.workload.nets().size(), frozen->workload_net_count);
  }
  EXPECT_EQ(FindPhase4WorkloadNetRosterManifestEntryV1(2000), nullptr);
  EXPECT_EQ(FindPhase4WorkloadNetRosterManifestEntryV1(3001), nullptr);
}

TEST(Phase4PerNetReportArtifactTest, BuildsValidDiagnosticCompanionWithCanonicalJson) {
  const Phase4PerNetReportArtifactV1 artifact = Artifact();
  EXPECT_TRUE(
      std::holds_alternative<std::monostate>(ValidatePhase4PerNetReportArtifactV1(artifact, {})));
  EXPECT_FALSE(artifact.decision_eligible);
  EXPECT_EQ(artifact.arms[0].arm, Phase4TrialArm::kSequentialBaseline);
  EXPECT_EQ(artifact.arms[1].arm, Phase4TrialArm::kReusableCandidateAllocation);
  EXPECT_NE(artifact.artifact_checksum, 0U);
  EXPECT_EQ(artifact.artifact_checksum, 9252848537064506260ULL);
  EXPECT_NE(artifact.source_envelope_checksum, 0U);
  EXPECT_EQ(artifact.source_envelope_checksum, 16587125904713458945ULL);

  const std::string first = SerializePhase4PerNetReportArtifactJsonV1(artifact);
  const std::string second = SerializePhase4PerNetReportArtifactJsonV1(artifact);
  EXPECT_EQ(first, second);
  ASSERT_FALSE(first.empty());
  EXPECT_EQ(first.front(), '{');
  EXPECT_EQ(first.back(), '\n');
  EXPECT_EQ(std::count(first.begin(), first.end(), '\n'), 1);
  EXPECT_NE(first.find("\"decision_eligible\":false"), std::string::npos);
  EXPECT_NE(first.find("\"per_net\":["), std::string::npos);
  EXPECT_EQ(first.find("case_build_elapsed_nanoseconds"), std::string::npos);
  EXPECT_EQ(first.find("outer_elapsed_nanoseconds"), std::string::npos);
  EXPECT_EQ(first.find("process_lifetime_peak_host_bytes"), std::string::npos);
  EXPECT_LT(first.find("\"raw_reference\":"), first.find("\"arms\":"));
  EXPECT_LT(first.find("\"arm\":0"), first.find("\"arm\":1"));
}

TEST(Phase4PerNetReportArtifactTest, RejectsReauthenticatedComponentAndEnumDrift) {
  Phase4PerNetReportArtifactV1 artifact = Artifact();
  artifact.arms[0].diagnostic.semantics.preparation_checksum = 1;
  Reauthenticate(&artifact);
  EXPECT_EQ(Rejected(artifact).invariant_id, "P4REPORT-ARM-TELEMETRY-001");

  artifact = Artifact();
  artifact.arms[1].diagnostic.semantics.final_pool_manifest_checksum = 0;
  Reauthenticate(&artifact);
  EXPECT_EQ(Rejected(artifact).invariant_id, "P4REPORT-ARM-TELEMETRY-001");

  artifact = Artifact();
  artifact.arms[0].diagnostic.semantics.terminal_reason =
      static_cast<Phase4NormalizedTerminalReason>(255);
  Reauthenticate(&artifact);
  EXPECT_EQ(Rejected(artifact).invariant_id, "P4REPORT-ARM-TELEMETRY-001");

  artifact = Artifact();
  artifact.arms[1].diagnostic.semantics.candidate_outcome_source =
      Phase4CandidateOutcomeSource::kNotCandidateArm;
  Reauthenticate(&artifact);
  EXPECT_EQ(Rejected(artifact).invariant_id, "P4REPORT-ARM-TELEMETRY-001");
}

TEST(Phase4PerNetReportArtifactTest, RejectsRawAssociationAndOperationalIdentityDrift) {
  Phase4PerNetReportArtifactV1 artifact = Artifact();
  ++artifact.raw_reference.pair_attempt_checksum;
  artifact.artifact_checksum = ComputePhase4PerNetReportArtifactChecksumV1(artifact);
  artifact.source_envelope_checksum = ComputePhase4PerNetReportSourceEnvelopeChecksumV1(
      artifact.source_commit, true, false, artifact.artifact_checksum);
  // The C++ core authenticates nonzero raw-reference shape. Exact equality to
  // the separate raw file is deliberately the next slice's independent join.
  EXPECT_TRUE(
      std::holds_alternative<std::monostate>(ValidatePhase4PerNetReportArtifactV1(artifact, {})));

  artifact = Artifact();
  artifact.raw_reference.repetition_index = 1;
  artifact.artifact_checksum = ComputePhase4PerNetReportArtifactChecksumV1(artifact);
  artifact.source_envelope_checksum = ComputePhase4PerNetReportSourceEnvelopeChecksumV1(
      artifact.source_commit, true, false, artifact.artifact_checksum);
  EXPECT_EQ(Rejected(artifact).invariant_id, "P4REPORT-RAW-REFERENCE-002");

  artifact = Artifact();
  artifact.arms[0].diagnostic.semantics.execution_order = Phase4TrialOrder::kCandidateFirst;
  Reauthenticate(&artifact);
  EXPECT_EQ(Rejected(artifact).invariant_id, "P4REPORT-ARM-IDENTITY-001");

  artifact = Artifact();
  artifact.arms[1].diagnostic.semantics.preparation_worker_count = 3;
  Reauthenticate(&artifact);
  EXPECT_EQ(Rejected(artifact).invariant_id, "P4REPORT-ARM-IDENTITY-001");
}

TEST(Phase4PerNetReportArtifactTest, RejectsRechecksummedSameCountCrossCaseSubstitution) {
  Phase4PerNetReportArtifactV1 artifact = Artifact();
  artifact.config.case_id = 101;
  artifact.raw_cell_plan_checksum = ComputePhase4CanonicalCellPlanChecksumV1(artifact.config);
  artifact.raw_source_envelope_checksum = ComputePhase4SourceEnvelopeChecksumV1(
      artifact.raw_wire_schema_version, artifact.source_commit, true, false,
      artifact.raw_cell_artifact_checksum);
  artifact.workload_net_roster_checksum =
      FindPhase4WorkloadNetRosterManifestEntryV1(101)->roster_checksum;
  Reauthenticate(&artifact);
  EXPECT_EQ(Rejected(artifact).invariant_id, "P4REPORT-ARM-IDENTITY-001");
}

TEST(Phase4PerNetReportArtifactTest, RejectsDirtyUnstampedAndForeignRawSourceEnvelope) {
  Phase4PerNetReportArtifactV1 artifact = Artifact();
  artifact.source_tree_dirty = true;
  Reauthenticate(&artifact);
  EXPECT_EQ(Rejected(artifact).invariant_id, "P4REPORT-ARTIFACT-ENVELOPE-001");

  artifact = Artifact();
  artifact.source_stamped = false;
  Reauthenticate(&artifact);
  EXPECT_EQ(Rejected(artifact).invariant_id, "P4REPORT-ARTIFACT-ENVELOPE-001");

  artifact = Artifact();
  ++artifact.raw_source_envelope_checksum;
  Reauthenticate(&artifact);
  EXPECT_EQ(Rejected(artifact).invariant_id, "P4REPORT-RAW-REFERENCE-001");
}

TEST(Phase4PerNetReportArtifactTest, BuilderPreflightsInvalidSourceBeforeCopyOrDiagnostics) {
  for (const std::string& source : {std::string("short"), std::string(1U << 20U, 'a')}) {
    Phase4PerNetReportArtifactResultV1 result =
        BuildPhase4PerNetReportArtifactV1(Cell(), source, true, false, 0, 0, 0, {}, {}, {});
    ASSERT_TRUE(std::holds_alternative<Phase4PerNetReportArtifactError>(result));
    EXPECT_EQ(std::get<Phase4PerNetReportArtifactError>(result).invariant_id,
              "P4REPORT-BUILD-SOURCE-001");
  }
}

}  // namespace
}  // namespace apgar::benchmark
