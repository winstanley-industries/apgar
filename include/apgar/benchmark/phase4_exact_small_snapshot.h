#ifndef APGAR_BENCHMARK_PHASE4_EXACT_SMALL_SNAPSHOT_H_
#define APGAR_BENCHMARK_PHASE4_EXACT_SMALL_SNAPSHOT_H_

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "apgar/benchmark/phase4_per_net_report_artifact.h"

namespace apgar::benchmark {

inline constexpr std::uint32_t kPhase4ExactSmallSnapshotSchemaVersion = 1;
inline constexpr std::uint64_t kPhase4ExactSmallMaximumCartesianProductV1 = 4'096;
inline constexpr std::uint64_t kPhase4ExactSmallPoolCountV1 = 6;
inline constexpr std::uint64_t kPhase4ExactSmallMaximumCandidatesPerPoolV1 = 6;
inline constexpr std::uint64_t kPhase4ExactSmallMaximumCandidateRowsV1 = 36;
inline constexpr std::uint64_t kPhase4ExactSmallMaximumGeometryPrimitivesV1 = 100'000;
inline constexpr std::uint64_t kPhase4ExactSmallMaximumResourceSpansV1 = 100'000;
inline constexpr std::uint64_t kPhase4ExactSmallMaximumPolicyResourceRecordsV1 = 100'000;
inline constexpr std::uint64_t kPhase4ExactSmallMaximumExpandedResourceEdgesV1 = 100'000;
inline constexpr std::uint64_t kPhase4ExactSmallMaximumCapacityOverridesV1 = 100'000;
inline constexpr std::uint64_t kPhase4ExactSmallMaximumAggregateCandidateLogicalBytesV1 =
    32ULL * 1024ULL * 1024ULL;
inline constexpr std::uint64_t kPhase4ExactSmallMaximumSerializedBytesV1 =
    64ULL * 1024ULL * 1024ULL;

struct Phase4ExactSmallCandidateV1 {
  std::uint16_t schema_major = candidates::kRouteCandidateSchemaMajor;
  std::uint16_t schema_minor = candidates::kRouteCandidateSchemaMinor;
  candidates::CandidateId id;
  board_ir::EntityRef net{};
  std::array<board_ir::EntityRef, 2> intended_terminals{};
  candidates::CandidateAssociations associations;
  std::uint32_t geometry_schema_version = candidates::kCandidateGeometrySchemaVersion;
  std::uint32_t resource_schema_version = candidates::kCandidateResourceSchemaVersion;
  routing::CandidateGenerationPolicy policy;
  std::uint64_t policy_identity = 0;
  candidates::CandidateProvenance provenance;
  std::vector<candidates::CandidatePrimitive> geometry;
  candidates::CandidateMetrics metrics;
  candidates::ConstraintAssessment constraints;
  candidates::CandidateSignature geometry_signature;
  candidates::CandidateSignature resource_signature;
  std::uint64_t payload_checksum = 0;
  std::uint64_t logical_bytes = 0;
  std::uint64_t intrinsic_cost = 0;
  std::vector<candidates::PhysicalEdgeSpan> resource_spans;

  friend bool operator==(const Phase4ExactSmallCandidateV1&,
                         const Phase4ExactSmallCandidateV1&) = default;
};

struct Phase4ExactSmallPoolV1 {
  board_ir::EntityRef net{};
  std::vector<Phase4ExactSmallCandidateV1> candidates;

  friend bool operator==(const Phase4ExactSmallPoolV1&, const Phase4ExactSmallPoolV1&) = default;
};

struct Phase4ExactSmallSelectionV1 {
  board_ir::EntityRef net{};
  allocator::NetSelectionStatus status = allocator::NetSelectionStatus::kNoAdmissibleCandidate;
  std::optional<candidates::CandidateId> candidate_id;
  std::optional<std::uint64_t> candidate_payload_checksum;
  std::uint64_t intrinsic_cost = 0;

  friend bool operator==(const Phase4ExactSmallSelectionV1&,
                         const Phase4ExactSmallSelectionV1&) = default;
};

// Diagnostic-only snapshot for the canonical exact cases. It captures every
// final-pool member and the current allocator capacity vocabulary, but makes no
// claim that the frozen pools are globally route-complete.
struct Phase4ExactSmallSnapshotArtifactV1 {
  std::string source_commit;
  bool source_stamped = false;
  bool source_tree_dirty = true;
  std::uint64_t source_envelope_checksum = 0;
  std::uint32_t schema_version = kPhase4ExactSmallSnapshotSchemaVersion;
  bool decision_eligible = false;
  Phase4CanonicalCellConfig config;
  Phase4TrialOrder execution_order = Phase4TrialOrder::kBaselineFirst;
  std::uint64_t root_seed = 0;
  std::uint64_t corpus_checksum = 0;
  std::uint64_t descriptor_fingerprint = 0;
  std::uint64_t case_checksum = 0;
  std::uint64_t board_content_hash = 0;
  std::uint64_t workload_checksum = 0;
  std::vector<board_ir::EntityRef> workload_roster;
  std::uint64_t workload_net_roster_checksum = 0;
  std::uint64_t budget_checksum = 0;
  std::uint64_t raw_cell_plan_checksum = 0;
  std::uint64_t raw_cell_artifact_checksum = 0;
  std::uint64_t raw_source_envelope_checksum = 0;
  Phase4PerNetReportRawReferenceV1 raw_reference;
  std::uint64_t per_net_report_artifact_checksum = 0;
  std::uint64_t per_net_report_source_envelope_checksum = 0;
  std::uint64_t per_net_candidate_telemetry_checksum = 0;
  Phase4TrialArmSemantics candidate_semantics;
  std::uint64_t candidate_semantic_checksum = 0;
  std::uint64_t candidate_session_checksum = 0;
  std::uint64_t final_pool_manifest_checksum = 0;
  std::uint64_t final_rejection_manifest_checksum = 0;
  Phase4CandidateOutcomeSource production_outcome_source =
      Phase4CandidateOutcomeSource::kCommonLineageOneWorld;
  std::uint32_t capacity_schema_version = 0;
  allocator::AllocationAssociations capacity_associations;
  std::uint32_t default_capacity_units = 0;
  std::uint64_t capacity_model_checksum = 0;
  std::vector<allocator::ResourceCapacityOverride> capacity_overrides;
  std::uint64_t cartesian_product = 0;
  std::vector<Phase4ExactSmallPoolV1> pools;
  std::vector<Phase4ExactSmallSelectionV1> production_selections;
  Phase4BoardOutcome production_outcome;
  std::uint64_t maximum_serialized_bytes = kPhase4ExactSmallMaximumSerializedBytesV1;
  std::uint64_t artifact_checksum = 0;

  friend bool operator==(const Phase4ExactSmallSnapshotArtifactV1&,
                         const Phase4ExactSmallSnapshotArtifactV1&) = default;
};

enum class Phase4ExactSmallSnapshotErrorCode : std::uint8_t {
  kUnsupportedSchema = 0,
  kUnsupportedCase = 1,
  kInvalidConfiguration = 2,
  kAuthorityAssociation = 3,
  kRosterMismatch = 4,
  kCartesianProductExceeded = 5,
  kCandidateInvariant = 6,
  kArithmeticOverflow = 7,
  kChecksumMismatch = 8,
  kArtifactBoundExceeded = 9,
};

struct Phase4ExactSmallSnapshotError {
  Phase4ExactSmallSnapshotErrorCode code = Phase4ExactSmallSnapshotErrorCode::kInvalidConfiguration;
  std::string invariant_id;
  std::string detail;
  std::uint64_t required = 0;
  std::uint64_t configured = 0;

  friend bool operator==(const Phase4ExactSmallSnapshotError&,
                         const Phase4ExactSmallSnapshotError&) = default;
};

using Phase4ExactSmallSnapshotArtifactResultV1 =
    std::variant<Phase4ExactSmallSnapshotArtifactV1, Phase4ExactSmallSnapshotError>;
using Phase4ExactSmallCartesianPreflightResultV1 =
    std::variant<std::uint64_t, Phase4ExactSmallSnapshotError>;
using Phase4ExactSmallSnapshotSerializationResultV1 =
    std::variant<std::string, Phase4ExactSmallSnapshotError>;

// Pure shape-only preflight. It validates the exact six-pool roster before
// multiplying and stops at the first factor that exceeds either bound.
[[nodiscard]] Phase4ExactSmallCartesianPreflightResultV1
PreflightPhase4ExactSmallCartesianProductV1(std::span<const std::uint64_t> pool_sizes,
                                            std::uint64_t declared_maximum) noexcept;

[[nodiscard]] std::uint64_t ComputePhase4ExactSmallSnapshotArtifactChecksumV1(
    const Phase4ExactSmallSnapshotArtifactV1& artifact) noexcept;

[[nodiscard]] std::uint64_t ComputePhase4ExactSmallSnapshotSourceEnvelopeChecksumV1(
    std::string_view source_commit, bool source_stamped, bool source_tree_dirty,
    std::uint64_t artifact_checksum) noexcept;

// Preflights prod(max(1, pool_size)) from pool sizes alone before visiting any
// candidate payload or resource span. No bounded prefix is ever emitted.
[[nodiscard]] Phase4ExactSmallSnapshotArtifactResultV1 BuildPhase4ExactSmallSnapshotArtifactV1(
    const Phase4CanonicalCellConfig& config, std::string_view source_commit, bool source_stamped,
    bool source_tree_dirty, std::uint64_t raw_cell_plan_checksum,
    std::uint64_t raw_cell_artifact_checksum, std::uint64_t raw_source_envelope_checksum,
    Phase4PerNetReportRawReferenceV1 raw_reference, std::uint64_t per_net_report_artifact_checksum,
    std::uint64_t per_net_report_source_envelope_checksum,
    Phase4CandidatePoolSnapshotExecutionV1 capture, std::string_view imported_fixture,
    std::uint32_t raw_evidence_schema_version = 1);

// Corpus authority is selected by the entry point, never inferred from case ID.
// The carrier and checksum domains remain snapshot v1 because corpus version,
// corpus checksum, case identity, and complete arm semantics are already hashed.
[[nodiscard]] Phase4ExactSmallSnapshotArtifactResultV1
BuildPhase4ExactSmallSnapshotArtifactForCorpusV2(
    const Phase4CanonicalCellConfig& config, std::string_view source_commit, bool source_stamped,
    bool source_tree_dirty, std::uint64_t raw_cell_plan_checksum,
    std::uint64_t raw_cell_artifact_checksum, std::uint64_t raw_source_envelope_checksum,
    Phase4PerNetReportRawReferenceV1 raw_reference, std::uint64_t per_net_report_artifact_checksum,
    std::uint64_t per_net_report_source_envelope_checksum,
    Phase4CandidatePoolSnapshotExecutionV1 capture, std::string_view imported_fixture,
    std::uint32_t raw_evidence_schema_version);

[[nodiscard]] std::variant<std::monostate, Phase4ExactSmallSnapshotError>
ValidatePhase4ExactSmallSnapshotArtifactV1(const Phase4ExactSmallSnapshotArtifactV1& artifact,
                                           std::string_view imported_fixture);

[[nodiscard]] std::variant<std::monostate, Phase4ExactSmallSnapshotError>
ValidatePhase4ExactSmallSnapshotArtifactForCorpusV2(
    const Phase4ExactSmallSnapshotArtifactV1& artifact, std::string_view imported_fixture);

// Canonical compact UTF-8 JSON: one object, fixed key order, exactly one LF.
[[nodiscard]] Phase4ExactSmallSnapshotSerializationResultV1
SerializePhase4ExactSmallSnapshotArtifactJsonV1(const Phase4ExactSmallSnapshotArtifactV1& artifact);

}  // namespace apgar::benchmark

#endif  // APGAR_BENCHMARK_PHASE4_EXACT_SMALL_SNAPSHOT_H_
