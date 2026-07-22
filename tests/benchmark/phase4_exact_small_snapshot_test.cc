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

[[nodiscard]] Phase4ExactSmallSnapshotArtifactV1 Artifact(std::uint32_t case_id = 100) {
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
      ComputePhase4SourceEnvelopeChecksumV1(kPhase4TrialWireSchemaVersion, kCommit, true, false,
                                            raw_artifact_checksum),
      raw, report_artifact_checksum,
      ComputePhase4PerNetReportSourceEnvelopeChecksumV1(kCommit, true, false,
                                                        report_artifact_checksum),
      std::move(capture), {}));
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

TEST(Phase4ExactSmallSnapshotTest, RejectsCartesianOverflowBeforeCandidateTraversal) {
  const Phase4CanonicalCellConfig cell = Cell();
  Phase4CandidatePoolSnapshotExecutionV1 capture;
  capture.final_pools.resize(6);
  for (auto& pool : capture.final_pools) pool.candidates.resize(5);
  Phase4PerNetReportRawReferenceV1 raw;
  auto result = BuildPhase4ExactSmallSnapshotArtifactV1(cell, kCommit, true, false, 1, 1, 1, raw, 1,
                                                        1, std::move(capture), {});
  ASSERT_TRUE(std::holds_alternative<Phase4ExactSmallSnapshotError>(result));
  const auto& error = std::get<Phase4ExactSmallSnapshotError>(result);
  EXPECT_EQ(error.code, Phase4ExactSmallSnapshotErrorCode::kCartesianProductExceeded);
  EXPECT_EQ(error.invariant_id, "P4EXACT-SNAPSHOT-PRODUCT-001");
  EXPECT_GT(error.required, kPhase4ExactSmallMaximumCartesianProductV1);
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
