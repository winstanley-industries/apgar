#include "apgar/benchmark/phase4_exact_small_snapshot.h"

#include <algorithm>
#include <cstdlib>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "apgar/allocator/cpu_candidate_pool_preparation.h"
#include "src/allocator/negotiated_prices_internal.h"
#include "src/allocator/one_world_internal.h"
#include "src/benchmark/phase4_confirmatory_h4096_exact_small_snapshot_internal.h"
#include "src/benchmark/phase4_h4096_canonical_budget_internal.h"
#include "src/benchmark/phase4_paired_trial_internal.h"
#include "tests/support/google_test.h"

namespace apgar::benchmark {
namespace {

constexpr std::string_view kCommit = "0123456789abcdef0123456789abcdef01234567";

template <typename Value, typename Error>
[[nodiscard]] Value ValueOf(std::variant<Value, Error> result) {
  EXPECT_TRUE(std::holds_alternative<Value>(result));
  if (!std::holds_alternative<Value>(result)) std::abort();
  return std::get<Value>(std::move(result));
}

[[nodiscard]] Phase4CanonicalCellConfig Cell(std::uint32_t case_id = 100) {
  Phase4CanonicalCellConfig cell;
  cell.case_id = case_id;
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
  return cell;
}

[[nodiscard]] Phase4ExactSmallSnapshotArtifactV1 Artifact(
    std::uint32_t case_id = 100, std::uint32_t raw_evidence_schema_version = 1) {
  const Phase4CanonicalCellConfig cell = Cell(case_id);
  const Phase4PairedTrialSpec spec = ValueOf<Phase4PairedTrialSpec>(
      BuildPhase4CanonicalTrialSpecV1(cell, 0, Phase4TrialOrder::kBaselineFirst));
  auto preparer = ValueOf<std::unique_ptr<allocator::PersistentCpuCandidatePoolPreparer>>(
      allocator::CreatePersistentCpuCandidatePoolPreparer(
          {.worker_count = kPhase4CanonicalPreparationWorkersV1}));
  Phase4CandidatePoolSnapshotExecutionV1 capture = ValueOf<Phase4CandidatePoolSnapshotExecutionV1>(
      ExecutePhase4CandidatePoolSnapshotV1(spec, {}, preparer.get()));
  const std::uint64_t raw_artifact_checksum = 101;
  const std::uint64_t report_artifact_checksum = 103;
  const Phase4PerNetReportRawReferenceV1 raw{
      .repetition_index = 0,
      .execution_order = Phase4TrialOrder::kBaselineFirst,
      .pair_attempt_checksum = 107,
      .paired_semantic_checksum = 109,
      .paired_artifact_checksum = 113,
      .baseline_semantic_checksum = 127,
      .baseline_arm_artifact_checksum = 131,
      .candidate_semantic_checksum = capture.semantics.semantic_checksum,
      .candidate_arm_artifact_checksum = 137,
  };
  return ValueOf<Phase4ExactSmallSnapshotArtifactV1>(BuildPhase4ExactSmallSnapshotArtifactV1(
      cell, kCommit, true, false, ComputePhase4CanonicalCellPlanChecksumV1(cell),
      raw_artifact_checksum,
      raw_evidence_schema_version == kPhase4SameRunRawEvidenceSchemaVersion
          ? ComputePhase4SourceEnvelopeChecksumV2(kPhase4SameRunRawEvidenceSchemaVersion,
                                                  kPhase4SameRunTrialWireSchemaVersion, kCommit,
                                                  true, false, raw_artifact_checksum)
          : ComputePhase4SourceEnvelopeChecksumV1(kPhase4TrialWireSchemaVersion, kCommit, true,
                                                  false, raw_artifact_checksum),
      raw, report_artifact_checksum,
      ComputePhase4PerNetReportSourceEnvelopeChecksumV1(kCommit, true, false,
                                                        report_artifact_checksum),
      std::move(capture), {}, raw_evidence_schema_version));
}

[[nodiscard]] Phase4CandidatePoolSnapshotExecutionV1 SyntheticCorpusV2Capture(
    const Phase4PairedTrialSpec& spec) {
  const Phase4RepresentativeCase representative = ValueOf<Phase4RepresentativeCase>(
      BuildPhase4RepresentativeCaseV2(spec.case_id, {}, spec.corpus_limits));
  Phase4CandidatePoolSnapshotExecutionV1 capture;
  capture.capacity_schema_version = representative.capacities.schema_version();
  capture.capacity_associations = representative.capacities.associations();
  capture.default_capacity_units = representative.capacities.default_capacity_units();
  capture.capacity_overrides = representative.capacities.overrides();
  capture.final_pools.reserve(representative.workload.nets().size());
  capture.production_world.selections.reserve(representative.workload.nets().size());
  for (const allocator::PreparedNetRoutingContext& context : representative.workload.nets()) {
    allocator::CandidatePool pool;
    pool.net = context.request.net;
    capture.final_pools.push_back(std::move(pool));
    allocator::NetSelection selection;
    selection.net = context.request.net;
    selection.status = allocator::NetSelectionStatus::kNoAdmissibleCandidate;
    capture.production_world.selections.push_back(std::move(selection));
  }

  const std::uint64_t columns_per_epoch =
      spec.candidate_session_config.regeneration_plan_config.maximum_total_columns;
  const std::uint32_t terminal_rounds =
      spec.candidate_session_config.schedules.back().maximum_selection_rounds;
  const Phase4RouteOpportunity opportunity{
      .route_queries = spec.baseline_config.limits.maximum_route_queries,
      .route_work_units = spec.baseline_config.limits.maximum_total_route_work_units,
  };
  Phase4TrialArmSemantics& semantics = capture.semantics;
  semantics.arm = Phase4TrialArm::kReusableCandidateAllocation;
  semantics.execution_order = spec.execution_order;
  semantics.corpus_version = kPhase4RepresentativeCorpusVersionV2;
  semantics.corpus_checksum = Phase4RepresentativeCorpusChecksumV2();
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
  semantics.preparation_checksum = 101;
  semantics.algorithm_session_checksum = 103;
  semantics.final_pool_manifest_checksum =
      allocator::internal::RecomputeOneWorldPoolManifestChecksumV1(capture.final_pools);
  semantics.final_rejection_manifest_checksum = 107;
  semantics.terminal_reason = Phase4NormalizedTerminalReason::kNoAdmissibleCandidate;
  semantics.candidate_outcome_source = Phase4CandidateOutcomeSource::kCommonLineageOneWorld;
  semantics.outcome.no_candidate_net_count = representative.workload.nets().size();
  semantics.outcome.world_checksum = 109;
  semantics.semantic_checksum = internal::ComputePhase4TrialArmSemanticChecksumV1(semantics);
  EXPECT_FALSE(internal::ValidatePhase4TrialArmSemanticsForAuthorityV1(
                   Phase4RepresentativeCorpusAuthority::kV2, semantics)
                   .has_value());

  capture.telemetry.associated_semantic_checksum = semantics.semantic_checksum;
  capture.telemetry.per_net.reserve(representative.workload.nets().size());
  for (const allocator::PreparedNetRoutingContext& context : representative.workload.nets()) {
    Phase4PerNetReportV1 report;
    report.net = context.request.net;
    capture.telemetry.per_net.push_back(std::move(report));
  }
  capture.telemetry.telemetry_checksum =
      internal::ComputePhase4ArmReportTelemetryChecksumV1(capture.telemetry);
  EXPECT_FALSE(internal::ValidatePhase4ArmReportTelemetryForAuthorityV1(
                   Phase4RepresentativeCorpusAuthority::kV2, semantics, representative.workload,
                   capture.telemetry)
                   .has_value());
  capture.production_world.no_candidate_net_count = representative.workload.nets().size();
  capture.production_world.world_checksum = semantics.outcome.world_checksum;
  return capture;
}

[[nodiscard]] Phase4ExactSmallSnapshotArtifactV1 CorpusV2Artifact() {
  const Phase4CanonicalCellConfig cell = Cell(10'100);
  const Phase4PairedTrialSpec spec = ValueOf<Phase4PairedTrialSpec>(
      BuildPhase4CanonicalTrialSpecForCorpusV2(cell, 0, Phase4TrialOrder::kBaselineFirst));
  Phase4CandidatePoolSnapshotExecutionV1 capture = SyntheticCorpusV2Capture(spec);
  const std::uint64_t raw_artifact_checksum = 201;
  const std::uint64_t report_artifact_checksum = 203;
  const Phase4PerNetReportRawReferenceV1 raw{
      .repetition_index = 0,
      .execution_order = Phase4TrialOrder::kBaselineFirst,
      .pair_attempt_checksum = 207,
      .paired_semantic_checksum = 209,
      .paired_artifact_checksum = 211,
      .baseline_semantic_checksum = 223,
      .baseline_arm_artifact_checksum = 227,
      .candidate_semantic_checksum = capture.semantics.semantic_checksum,
      .candidate_arm_artifact_checksum = 229,
  };
  return ValueOf<Phase4ExactSmallSnapshotArtifactV1>(
      BuildPhase4ExactSmallSnapshotArtifactForCorpusV2(
          cell, kCommit, true, false, ComputePhase4CanonicalCellPlanChecksumForCorpusV2(cell),
          raw_artifact_checksum,
          ComputePhase4SourceEnvelopeChecksumV2(kPhase4SameRunRawEvidenceSchemaVersion,
                                                kPhase4SameRunTrialWireSchemaVersion, kCommit, true,
                                                false, raw_artifact_checksum),
          raw, report_artifact_checksum,
          ComputePhase4PerNetReportSourceEnvelopeChecksumV1(kCommit, true, false,
                                                            report_artifact_checksum),
          std::move(capture), {}, kPhase4SameRunRawEvidenceSchemaVersion));
}

[[nodiscard]] Phase4ExactSmallSnapshotArtifactV1 H4096CorpusV2Artifact() {
  Phase4CanonicalCellConfig cell = Cell(10'100);
  cell.maximum_setup_elapsed_nanoseconds = 300'000'000'000ULL;
  cell.external_budget = {
      .maximum_prepared_elapsed_nanoseconds = 300'000'000'000ULL,
      .maximum_cold_elapsed_nanoseconds = 300'000'000'000ULL,
      .maximum_address_space_bytes = 64ULL * 1024ULL * 1024ULL * 1024ULL,
      .maximum_peak_host_bytes = 16ULL * 1024ULL * 1024ULL * 1024ULL,
  };
  const Phase4PairedTrialSpec spec =
      ValueOf<Phase4PairedTrialSpec>(internal::BuildPhase4CanonicalTrialSpecForCorpusV2H4096(
          cell, 0, Phase4TrialOrder::kBaselineFirst));
  EXPECT_FALSE(internal::PreflightPhase4ConfirmatoryH4096SameRunSpec(
                   spec, Phase4TrialArm::kReusableCandidateAllocation)
                   .has_value());
  Phase4CandidatePoolSnapshotExecutionV1 capture = SyntheticCorpusV2Capture(spec);
  const std::uint64_t raw_artifact_checksum = 301;
  const std::uint64_t report_artifact_checksum = 307;
  const Phase4PerNetReportRawReferenceV1 raw{
      .repetition_index = 0,
      .execution_order = Phase4TrialOrder::kBaselineFirst,
      .pair_attempt_checksum = 311,
      .paired_semantic_checksum = 313,
      .paired_artifact_checksum = 317,
      .baseline_semantic_checksum = 331,
      .baseline_arm_artifact_checksum = 337,
      .candidate_semantic_checksum = capture.semantics.semantic_checksum,
      .candidate_arm_artifact_checksum = 347,
  };
  auto artifact_result = internal::BuildPhase4ConfirmatoryH4096ExactSmallSnapshotArtifact(
      cell, kCommit, true, false, ComputePhase4CanonicalCellPlanChecksumForCorpusV2(cell),
      raw_artifact_checksum,
      ComputePhase4SourceEnvelopeChecksumV2(kPhase4SameRunRawEvidenceSchemaVersion,
                                            kPhase4SameRunTrialWireSchemaVersion, kCommit, true,
                                            false, raw_artifact_checksum),
      raw, report_artifact_checksum,
      ComputePhase4PerNetReportSourceEnvelopeChecksumV1(kCommit, true, false,
                                                        report_artifact_checksum),
      std::move(capture), {}, kPhase4SameRunRawEvidenceSchemaVersion);
  if (const auto* error = std::get_if<Phase4ExactSmallSnapshotError>(&artifact_result);
      error != nullptr) {
    ADD_FAILURE() << error->invariant_id << ": " << error->detail << " required=" << error->required
                  << " configured=" << error->configured;
    std::abort();
  }
  return std::get<Phase4ExactSmallSnapshotArtifactV1>(std::move(artifact_result));
}

void Reauthenticate(Phase4ExactSmallSnapshotArtifactV1* artifact) {
  artifact->artifact_checksum = ComputePhase4ExactSmallSnapshotArtifactChecksumV1(*artifact);
  artifact->source_envelope_checksum = ComputePhase4ExactSmallSnapshotSourceEnvelopeChecksumV1(
      artifact->source_commit, artifact->source_stamped, artifact->source_tree_dirty,
      artifact->artifact_checksum);
}

[[nodiscard]] Phase4ExactSmallSnapshotError Rejected(
    const Phase4ExactSmallSnapshotArtifactV1& artifact) {
  auto result = ValidatePhase4ExactSmallSnapshotArtifactV1(artifact, {});
  EXPECT_TRUE(std::holds_alternative<Phase4ExactSmallSnapshotError>(result));
  if (!std::holds_alternative<Phase4ExactSmallSnapshotError>(result)) std::abort();
  return std::get<Phase4ExactSmallSnapshotError>(std::move(result));
}

TEST(Phase4ExactSmallSnapshotTest, BuildsCanonicalCompleteSnapshotAndRejectsCorruption) {
  Phase4ExactSmallSnapshotArtifactV1 artifact = Artifact();
  EXPECT_TRUE(std::holds_alternative<std::monostate>(
      ValidatePhase4ExactSmallSnapshotArtifactV1(artifact, {})));
  EXPECT_FALSE(artifact.decision_eligible);
  EXPECT_EQ(artifact.pools.size(), 6U);
  EXPECT_EQ(artifact.workload_roster.size(), 6U);
  EXPECT_EQ(artifact.production_selections.size(), 6U);
  EXPECT_GE(artifact.cartesian_product, 1U);
  EXPECT_LE(artifact.cartesian_product, kPhase4ExactSmallMaximumCartesianProductV1);
  for (const auto& pool : artifact.pools) {
    EXPECT_TRUE(std::ranges::is_sorted(pool.candidates, {}, &Phase4ExactSmallCandidateV1::id));
  }
  const std::string json =
      ValueOf<std::string>(SerializePhase4ExactSmallSnapshotArtifactJsonV1(artifact));
  EXPECT_EQ(json.back(), '\n');
  EXPECT_EQ(std::count(json.begin(), json.end(), '\n'), 1);
  EXPECT_NE(json.find("\"decision_eligible\":false"), std::string::npos);
  EXPECT_NE(json.find("\"resource_spans\":"), std::string::npos);
  EXPECT_NE(json.find("\"geometry\":"), std::string::npos);
  EXPECT_NE(json.find("\"production_selections\":"), std::string::npos);
  EXPECT_NE(json.find("\"candidate_semantics\":"), std::string::npos);

  Phase4ExactSmallSnapshotArtifactV1 changed = artifact;
  changed.decision_eligible = true;
  Reauthenticate(&changed);
  EXPECT_EQ(Rejected(changed).invariant_id, "P4EXACT-SNAPSHOT-DIAGNOSTIC-001");

  changed = artifact;
  ASSERT_FALSE(changed.pools.front().candidates.empty());
  ASSERT_FALSE(changed.pools.front().candidates.front().resource_spans.empty());
  ++changed.pools.front().candidates.front().resource_spans.front().usage_units;
  Reauthenticate(&changed);
  EXPECT_EQ(Rejected(changed).invariant_id, "P4EXACT-SNAPSHOT-PAYLOAD-001");

  changed = artifact;
  const auto selected =
      std::ranges::find(changed.production_selections, allocator::NetSelectionStatus::kSelected,
                        &Phase4ExactSmallSelectionV1::status);
  ASSERT_NE(selected, changed.production_selections.end());
  ++selected->intrinsic_cost;
  Reauthenticate(&changed);
  EXPECT_EQ(Rejected(changed).invariant_id, "P4EXACT-SNAPSHOT-SELECTION-002");
}

TEST(Phase4ExactSmallSnapshotTest, AcceptsOnlyVersionedRawSourceEnvelopeDomains) {
  const Phase4ExactSmallSnapshotArtifactV1 raw_v1 = Artifact();
  const Phase4ExactSmallSnapshotArtifactV1 raw_v2 =
      Artifact(100, kPhase4SameRunRawEvidenceSchemaVersion);
  EXPECT_TRUE(std::holds_alternative<std::monostate>(
      ValidatePhase4ExactSmallSnapshotArtifactV1(raw_v1, {})));
  EXPECT_TRUE(std::holds_alternative<std::monostate>(
      ValidatePhase4ExactSmallSnapshotArtifactV1(raw_v2, {})));
  EXPECT_NE(raw_v1.raw_source_envelope_checksum, raw_v2.raw_source_envelope_checksum);

  Phase4ExactSmallSnapshotArtifactV1 foreign = raw_v2;
  ++foreign.raw_source_envelope_checksum;
  Reauthenticate(&foreign);
  EXPECT_EQ(Rejected(foreign).invariant_id, "P4EXACT-SNAPSHOT-AUTHORITY-002");
}

TEST(Phase4ExactSmallSnapshotTest, CorpusV2SnapshotExecutionIsClosedBeforeWork) {
  const Phase4CanonicalCellConfig cell = Cell(10'100);
  const Phase4PairedTrialSpec spec = ValueOf<Phase4PairedTrialSpec>(
      BuildPhase4CanonicalTrialSpecForCorpusV2(cell, 0, Phase4TrialOrder::kBaselineFirst));
  auto result = ExecutePhase4CandidatePoolSnapshotForCorpusV2(spec, "must-not-be-read", nullptr);
  ASSERT_TRUE(std::holds_alternative<Phase4TrialArmFailure>(result));
  const Phase4PairedTrialError& error = std::get<Phase4TrialArmFailure>(result).summary;
  EXPECT_EQ(error.code, Phase4PairedTrialErrorCode::kUnsupportedSchema);
  EXPECT_EQ(error.invariant_id, "P4PAIR-CORPUS-V2-SESSION-AUTHORITY-001");
}

TEST(Phase4ExactSmallSnapshotTest, H4096SnapshotExecutionIsClosedBeforeWork) {
  Phase4CanonicalCellConfig cell = Cell(10'100);
  cell.maximum_setup_elapsed_nanoseconds = 300'000'000'000ULL;
  cell.external_budget = {
      .maximum_prepared_elapsed_nanoseconds = 300'000'000'000ULL,
      .maximum_cold_elapsed_nanoseconds = 300'000'000'000ULL,
      .maximum_address_space_bytes = 64ULL * 1024ULL * 1024ULL * 1024ULL,
      .maximum_peak_host_bytes = 16ULL * 1024ULL * 1024ULL * 1024ULL,
  };
  const Phase4PairedTrialSpec spec =
      ValueOf<Phase4PairedTrialSpec>(internal::BuildPhase4CanonicalTrialSpecForCorpusV2H4096(
          cell, 0, Phase4TrialOrder::kBaselineFirst));
  auto result = internal::ExecutePhase4ConfirmatoryH4096SameRunCandidatePoolSnapshot(
      spec, "must-not-be-read", nullptr);
  ASSERT_TRUE(std::holds_alternative<Phase4TrialArmFailure>(result));
  const Phase4PairedTrialError& error = std::get<Phase4TrialArmFailure>(result).summary;
  EXPECT_EQ(error.code, Phase4PairedTrialErrorCode::kUnsupportedSchema);
  EXPECT_EQ(error.invariant_id, "P4PAIR-CORPUS-V2-SESSION-AUTHORITY-001");
}

TEST(Phase4ExactSmallSnapshotTest, H4096AuthorityPinsBudgetOutcomeAndCrossRejectsH2250) {
  constexpr std::uint64_t kH4096ExactPairedBudgetChecksum = 5'851'813'264'366'095'594ULL;
  const Phase4ExactSmallSnapshotArtifactV1 h2250 = CorpusV2Artifact();
  const Phase4ExactSmallSnapshotArtifactV1 h4096 = H4096CorpusV2Artifact();

  EXPECT_TRUE(std::holds_alternative<std::monostate>(
      internal::ValidatePhase4ConfirmatoryH4096ExactSmallSnapshotArtifact(h4096, {})));
  EXPECT_TRUE(std::holds_alternative<Phase4ExactSmallSnapshotError>(
      ValidatePhase4ExactSmallSnapshotArtifactForCorpusV2(h4096, {})));
  EXPECT_TRUE(std::holds_alternative<Phase4ExactSmallSnapshotError>(
      internal::ValidatePhase4ConfirmatoryH4096ExactSmallSnapshotArtifact(h2250, {})));

  EXPECT_EQ(h4096.budget_checksum, kH4096ExactPairedBudgetChecksum);
  EXPECT_EQ(h4096.candidate_semantics.budget_checksum, kH4096ExactPairedBudgetChecksum);
  EXPECT_EQ(h4096.cartesian_product, 1U);
  ASSERT_EQ(h4096.pools.size(), 6U);
  EXPECT_TRUE(
      std::ranges::all_of(h4096.pools, [](const auto& pool) { return pool.candidates.empty(); }));
  EXPECT_TRUE(std::ranges::all_of(h4096.production_selections, [](const auto& selection) {
    return selection.status == allocator::NetSelectionStatus::kNoAdmissibleCandidate;
  }));
  EXPECT_EQ(h4096.production_outcome, (Phase4BoardOutcome{
                                          .selected_net_count = 0,
                                          .no_candidate_net_count = 6,
                                          .overused_resource_count = 0,
                                          .total_overuse_units = 0,
                                          .total_intrinsic_cost = 0,
                                          .world_checksum = 109,
                                      }));
}

TEST(Phase4ExactSmallSnapshotTest, CorpusV2SnapshotAuthorityKeepsUnopenedCasesClosed) {
  for (const std::uint32_t case_id : {10'101U, 10'102U}) {
    SCOPED_TRACE(case_id);
    const Phase4CanonicalCellConfig cell = Cell(case_id);
    constexpr std::uint64_t kRawArtifactChecksum = 201;
    constexpr std::uint64_t kReportArtifactChecksum = 203;
    const Phase4PerNetReportRawReferenceV1 raw{
        .repetition_index = 0,
        .execution_order = Phase4TrialOrder::kBaselineFirst,
        .pair_attempt_checksum = 207,
        .paired_semantic_checksum = 209,
        .paired_artifact_checksum = 211,
        .baseline_semantic_checksum = 223,
        .baseline_arm_artifact_checksum = 227,
        .candidate_semantic_checksum = 229,
        .candidate_arm_artifact_checksum = 233,
    };
    auto build_result = BuildPhase4ExactSmallSnapshotArtifactForCorpusV2(
        cell, kCommit, true, false, ComputePhase4CanonicalCellPlanChecksumForCorpusV2(cell),
        kRawArtifactChecksum,
        ComputePhase4SourceEnvelopeChecksumV2(kPhase4SameRunRawEvidenceSchemaVersion,
                                              kPhase4SameRunTrialWireSchemaVersion, kCommit, true,
                                              false, kRawArtifactChecksum),
        raw, kReportArtifactChecksum,
        ComputePhase4PerNetReportSourceEnvelopeChecksumV1(kCommit, true, false,
                                                          kReportArtifactChecksum),
        {}, {}, kPhase4SameRunRawEvidenceSchemaVersion);
    ASSERT_TRUE(std::holds_alternative<Phase4ExactSmallSnapshotError>(build_result));
    const auto& build_error = std::get<Phase4ExactSmallSnapshotError>(build_result);
    EXPECT_EQ(build_error.code, Phase4ExactSmallSnapshotErrorCode::kUnsupportedCase);
    EXPECT_EQ(build_error.invariant_id, "P4EXACT-SNAPSHOT-CASE-001");

    Phase4ExactSmallSnapshotArtifactV1 closed = Artifact();
    closed.config.case_id = case_id;
    auto validation = ValidatePhase4ExactSmallSnapshotArtifactForCorpusV2(closed, {});
    ASSERT_TRUE(std::holds_alternative<Phase4ExactSmallSnapshotError>(validation));
    const auto& validation_error = std::get<Phase4ExactSmallSnapshotError>(validation);
    EXPECT_EQ(validation_error.code, Phase4ExactSmallSnapshotErrorCode::kUnsupportedCase);
    EXPECT_EQ(validation_error.invariant_id, "P4EXACT-SNAPSHOT-CASE-003");
  }
}

TEST(Phase4ExactSmallSnapshotTest, RejectsCartesianOverflowBeforeCandidateTraversal) {
  const Phase4CanonicalCellConfig cell = Cell();
  Phase4CandidatePoolSnapshotExecutionV1 capture;
  capture.final_pools.resize(6);
  for (auto& pool : capture.final_pools) pool.candidates.resize(5);
  const std::uint64_t raw_artifact_checksum = 1;
  const std::uint64_t report_artifact_checksum = 2;
  const Phase4PerNetReportRawReferenceV1 raw{
      .repetition_index = 0,
      .execution_order = Phase4TrialOrder::kBaselineFirst,
      .pair_attempt_checksum = 1,
      .paired_semantic_checksum = 2,
      .paired_artifact_checksum = 3,
      .baseline_semantic_checksum = 4,
      .baseline_arm_artifact_checksum = 5,
      .candidate_semantic_checksum = 6,
      .candidate_arm_artifact_checksum = 7,
  };
  auto result = BuildPhase4ExactSmallSnapshotArtifactV1(
      cell, kCommit, true, false, ComputePhase4CanonicalCellPlanChecksumV1(cell),
      raw_artifact_checksum,
      ComputePhase4SourceEnvelopeChecksumV1(kPhase4TrialWireSchemaVersion, kCommit, true, false,
                                            raw_artifact_checksum),
      raw, report_artifact_checksum,
      ComputePhase4PerNetReportSourceEnvelopeChecksumV1(kCommit, true, false,
                                                        report_artifact_checksum),
      std::move(capture), {});
  ASSERT_TRUE(std::holds_alternative<Phase4ExactSmallSnapshotError>(result));
  const auto& error = std::get<Phase4ExactSmallSnapshotError>(result);
  EXPECT_EQ(error.code, Phase4ExactSmallSnapshotErrorCode::kCartesianProductExceeded);
  EXPECT_EQ(error.invariant_id, "P4EXACT-SNAPSHOT-PRODUCT-001");
  EXPECT_GT(error.required, kPhase4ExactSmallMaximumCartesianProductV1);
}

TEST(Phase4ExactSmallSnapshotTest, RejectsRawCarrierBeforeCandidateTraversal) {
  const Phase4CanonicalCellConfig cell = Cell();
  Phase4CandidatePoolSnapshotExecutionV1 capture;
  capture.final_pools.resize(6);
  capture.final_pools.front().candidates.resize(1);
  const std::uint64_t raw_artifact_checksum = 1;
  const std::uint64_t report_artifact_checksum = 2;
  const Phase4PerNetReportRawReferenceV1 raw{
      .repetition_index = 0,
      .execution_order = Phase4TrialOrder::kBaselineFirst,
      .pair_attempt_checksum = 1,
      .paired_semantic_checksum = 2,
      .paired_artifact_checksum = 3,
      .baseline_semantic_checksum = 4,
      .baseline_arm_artifact_checksum = 5,
      .candidate_semantic_checksum = 6,
      .candidate_arm_artifact_checksum = 7,
  };
  auto result = BuildPhase4ExactSmallSnapshotArtifactV1(
      cell, kCommit, true, false, ComputePhase4CanonicalCellPlanChecksumV1(cell),
      raw_artifact_checksum,
      ComputePhase4SourceEnvelopeChecksumV1(kPhase4TrialWireSchemaVersion, kCommit, true, false,
                                            raw_artifact_checksum),
      raw, report_artifact_checksum,
      ComputePhase4PerNetReportSourceEnvelopeChecksumV1(kCommit, true, false,
                                                        report_artifact_checksum),
      std::move(capture), {}, 3);
  ASSERT_TRUE(std::holds_alternative<Phase4ExactSmallSnapshotError>(result));
  const auto& error = std::get<Phase4ExactSmallSnapshotError>(result);
  EXPECT_EQ(error.code, Phase4ExactSmallSnapshotErrorCode::kAuthorityAssociation);
  EXPECT_EQ(error.invariant_id, "P4EXACT-SNAPSHOT-AUTHORITY-001");
}

TEST(Phase4ExactSmallSnapshotTest, SupportsAllCanonicalExactCases) {
  for (const std::uint32_t case_id : {101U, 102U}) {
    const Phase4ExactSmallSnapshotArtifactV1 artifact = Artifact(case_id);
    EXPECT_EQ(artifact.config.case_id, case_id);
    EXPECT_EQ(artifact.pools.size(), 6U);
    EXPECT_LE(artifact.cartesian_product, kPhase4ExactSmallMaximumCartesianProductV1);
    EXPECT_TRUE(std::holds_alternative<std::monostate>(
        ValidatePhase4ExactSmallSnapshotArtifactV1(artifact, {})));
  }
}

TEST(Phase4ExactSmallSnapshotTest, PureCartesianPreflightHasFrozenRosterAndEarlyBound) {
  const std::array<std::uint64_t, 6> boundary{4, 4, 4, 4, 4, 4};
  EXPECT_EQ(ValueOf<std::uint64_t>(PreflightPhase4ExactSmallCartesianProductV1(
                boundary, kPhase4ExactSmallMaximumCartesianProductV1)),
            4096U);

  const std::array<std::uint64_t, 6> first_factor_over{
      4097, 1, 1, 1, 1, std::numeric_limits<std::uint64_t>::max()};
  auto over = PreflightPhase4ExactSmallCartesianProductV1(
      first_factor_over, kPhase4ExactSmallMaximumCartesianProductV1);
  ASSERT_TRUE(std::holds_alternative<Phase4ExactSmallSnapshotError>(over));
  EXPECT_EQ(std::get<Phase4ExactSmallSnapshotError>(over).required, 4097U);

  const std::array<std::uint64_t, 6> empty_pools{};
  EXPECT_EQ(ValueOf<std::uint64_t>(PreflightPhase4ExactSmallCartesianProductV1(
                empty_pools, kPhase4ExactSmallMaximumCartesianProductV1)),
            1U);
  const std::vector<std::uint64_t> wrong_roster(7, 0);
  auto wrong = PreflightPhase4ExactSmallCartesianProductV1(
      wrong_roster, kPhase4ExactSmallMaximumCartesianProductV1);
  ASSERT_TRUE(std::holds_alternative<Phase4ExactSmallSnapshotError>(wrong));
  EXPECT_EQ(std::get<Phase4ExactSmallSnapshotError>(wrong).invariant_id,
            "P4EXACT-SNAPSHOT-PRODUCT-ROSTER-001");
}

TEST(Phase4ExactSmallSnapshotTest, RejectsReauthenticatedManifestAndSemanticCorruption) {
  Phase4ExactSmallSnapshotArtifactV1 changed = Artifact(102);
  bool erased = false;
  for (std::size_t index = 0; index < changed.pools.size() && !erased; ++index) {
    const auto selected = changed.production_selections[index].candidate_id;
    auto unselected = std::ranges::find_if(
        changed.pools[index].candidates,
        [selected](const auto& candidate) { return !selected || candidate.id != *selected; });
    if (unselected != changed.pools[index].candidates.end()) {
      changed.pools[index].candidates.erase(unselected);
      erased = true;
    }
  }
  ASSERT_TRUE(erased);
  std::array<std::uint64_t, 6> sizes{};
  for (std::size_t index = 0; index < changed.pools.size(); ++index) {
    sizes[index] = changed.pools[index].candidates.size();
  }
  changed.cartesian_product = ValueOf<std::uint64_t>(PreflightPhase4ExactSmallCartesianProductV1(
      sizes, kPhase4ExactSmallMaximumCartesianProductV1));
  Reauthenticate(&changed);
  EXPECT_EQ(Rejected(changed).invariant_id, "P4EXACT-SNAPSHOT-POOL-MANIFEST-001");

  changed = Artifact();
  ++changed.candidate_session_checksum;
  Reauthenticate(&changed);
  EXPECT_EQ(Rejected(changed).invariant_id, "P4EXACT-SNAPSHOT-SEMANTIC-PROJECTION-001");
}

TEST(Phase4ExactSmallSnapshotTest, IndependentlyRejectsOutcomeAndCandidatePayloadCorruption) {
  Phase4ExactSmallSnapshotArtifactV1 changed = Artifact();
  ++changed.production_outcome.total_overuse_units;
  changed.candidate_semantics.outcome = changed.production_outcome;
  changed.candidate_semantics.semantic_checksum =
      internal::ComputePhase4TrialArmSemanticChecksumV1(changed.candidate_semantics);
  changed.candidate_semantic_checksum = changed.candidate_semantics.semantic_checksum;
  changed.raw_reference.candidate_semantic_checksum = changed.candidate_semantic_checksum;
  Reauthenticate(&changed);
  EXPECT_EQ(Rejected(changed).invariant_id, "P4EXACT-SNAPSHOT-OVERUSE-001");

  changed = Artifact();
  ASSERT_FALSE(changed.pools.front().candidates.empty());
  changed.pools.front().candidates.front().provenance.supported_device_class.assign(1, '\xff');
  Reauthenticate(&changed);
  EXPECT_EQ(Rejected(changed).invariant_id, "P4EXACT-SNAPSHOT-PAYLOAD-001");

  changed = Artifact();
  ASSERT_FALSE(changed.pools.front().candidates.empty());
  ASSERT_FALSE(changed.pools.front().candidates.front().resource_spans.empty());
  changed.pools.front().candidates.front().resource_spans.front().direction =
      static_cast<geometry_compiler::Direction>(255);
  Reauthenticate(&changed);
  EXPECT_EQ(Rejected(changed).invariant_id, "P4EXACT-SNAPSHOT-PAYLOAD-001");

  changed = Artifact();
  ASSERT_FALSE(changed.pools.front().candidates.empty());
  ASSERT_FALSE(changed.pools.front().candidates.front().resource_spans.empty());
  auto& overflowing_span = changed.pools.front().candidates.front().resource_spans.front();
  overflowing_span.lattice_x = std::numeric_limits<std::int64_t>::max();
  overflowing_span.direction = geometry_compiler::Direction::kEast;
  overflowing_span.edge_count = 2;
  Reauthenticate(&changed);
  EXPECT_EQ(Rejected(changed).invariant_id, "P4EXACT-SNAPSHOT-PAYLOAD-001");

  changed = Artifact();
  ASSERT_FALSE(changed.pools.front().candidates.empty());
  ASSERT_FALSE(changed.pools.front().candidates.front().resource_spans.empty());
  changed.pools.front().candidates.front().resource_spans.push_back(
      changed.pools.front().candidates.front().resource_spans.front());
  Reauthenticate(&changed);
  EXPECT_EQ(Rejected(changed).invariant_id, "P4EXACT-SNAPSHOT-PAYLOAD-001");

  changed = Artifact();
  auto candidate = std::ranges::find_if(changed.pools.front().candidates, [](const auto& row) {
    return std::ranges::any_of(row.geometry, [](const auto& primitive) {
      return std::holds_alternative<candidates::ExactLinePrimitive>(primitive);
    });
  });
  ASSERT_NE(candidate, changed.pools.front().candidates.end());
  auto primitive = std::ranges::find_if(candidate->geometry, [](const auto& value) {
    return std::holds_alternative<candidates::ExactLinePrimitive>(value);
  });
  ASSERT_NE(primitive, candidate->geometry.end());
  auto& line = std::get<candidates::ExactLinePrimitive>(*primitive);
  std::swap(line.centerline.start, line.centerline.end);
  Reauthenticate(&changed);
  EXPECT_EQ(Rejected(changed).invariant_id, "P4EXACT-SNAPSHOT-PAYLOAD-001");
}

TEST(Phase4ExactSmallSnapshotTest, RejectsComponentAndSerializedOutputBounds) {
  Phase4ExactSmallSnapshotArtifactV1 changed = Artifact();
  ASSERT_FALSE(changed.pools.front().candidates.empty());
  changed.pools.front().candidates.front().geometry.resize(
      kPhase4ExactSmallMaximumGeometryPrimitivesV1 + 1U);
  EXPECT_EQ(Rejected(changed).invariant_id, "P4EXACT-SNAPSHOT-COMPONENT-BOUND-001");

  changed = Artifact();
  ASSERT_FALSE(changed.pools.front().candidates.empty());
  changed.pools.front().candidates.front().resource_spans.resize(
      kPhase4ExactSmallMaximumResourceSpansV1 + 1U);
  EXPECT_EQ(Rejected(changed).invariant_id, "P4EXACT-SNAPSHOT-COMPONENT-BOUND-001");

  changed = Artifact();
  changed.capacity_overrides.resize(kPhase4ExactSmallMaximumCapacityOverridesV1 + 1U);
  EXPECT_EQ(Rejected(changed).invariant_id, "P4EXACT-SNAPSHOT-CAPACITY-BOUND-001");

  changed = Artifact();
  changed.maximum_serialized_bytes = 1;
  auto serialized = SerializePhase4ExactSmallSnapshotArtifactJsonV1(changed);
  ASSERT_TRUE(std::holds_alternative<Phase4ExactSmallSnapshotError>(serialized));
  EXPECT_EQ(std::get<Phase4ExactSmallSnapshotError>(serialized).invariant_id,
            "P4EXACT-SNAPSHOT-OUTPUT-BOUND-002");
}

}  // namespace
}  // namespace apgar::benchmark
