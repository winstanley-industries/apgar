#ifndef APGAR_BENCHMARK_PHASE4_PER_NET_REPORT_ARTIFACT_H_
#define APGAR_BENCHMARK_PHASE4_PER_NET_REPORT_ARTIFACT_H_

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <variant>

#include "apgar/benchmark/phase4_trial_harness.h"

namespace apgar::benchmark {

inline constexpr std::uint32_t kPhase4WorkloadNetRosterManifestSchemaVersion = 1;
inline constexpr std::uint32_t kPhase4PerNetReportArtifactSchemaVersion = 1;
inline constexpr std::uint64_t kPhase4WorkloadNetRosterManifestChecksumV1 = 3143811343998575433ULL;

enum class Phase4WorkloadNetRosterExclusionDispositionV1 : std::uint8_t {
  kDescriptorOnlyUnsupportedPool = 0,
  kCompiledWorkBound = 1,
};

// One independently frozen successful-case row mirrored by
// phase4_workload_net_roster_manifest_v1.json. The roster checksum binds every
// complete EntityRef, not only the net count or workload checksum.
struct Phase4WorkloadNetRosterManifestEntryV1 {
  std::uint32_t schema_version = kPhase4WorkloadNetRosterManifestSchemaVersion;
  std::uint32_t corpus_version = kPhase4RepresentativeCorpusVersion;
  std::uint64_t corpus_checksum = 0;
  std::uint32_t case_id = 0;
  std::uint64_t descriptor_fingerprint = 0;
  std::uint64_t case_checksum = 0;
  std::uint64_t board_content_hash = 0;
  std::uint64_t workload_checksum = 0;
  std::uint32_t workload_net_count = 0;
  std::uint64_t roster_checksum = 0;

  friend bool operator==(const Phase4WorkloadNetRosterManifestEntryV1&,
                         const Phase4WorkloadNetRosterManifestEntryV1&) = default;
};

struct Phase4WorkloadNetRosterManifestExclusionV1 {
  std::uint32_t case_id = 0;
  std::uint64_t descriptor_fingerprint = 0;
  Phase4WorkloadNetRosterExclusionDispositionV1 disposition =
      Phase4WorkloadNetRosterExclusionDispositionV1::kDescriptorOnlyUnsupportedPool;

  friend bool operator==(const Phase4WorkloadNetRosterManifestExclusionV1&,
                         const Phase4WorkloadNetRosterManifestExclusionV1&) = default;
};

struct Phase4PerNetReportRawReferenceV1 {
  std::uint32_t repetition_index = 0;
  Phase4TrialOrder execution_order = Phase4TrialOrder::kBaselineFirst;
  std::uint64_t pair_attempt_checksum = 0;
  std::uint64_t paired_semantic_checksum = 0;
  std::uint64_t paired_artifact_checksum = 0;
  std::uint64_t baseline_semantic_checksum = 0;
  std::uint64_t baseline_arm_artifact_checksum = 0;
  std::uint64_t candidate_semantic_checksum = 0;
  std::uint64_t candidate_arm_artifact_checksum = 0;

  friend bool operator==(const Phase4PerNetReportRawReferenceV1&,
                         const Phase4PerNetReportRawReferenceV1&) = default;
};

struct Phase4PerNetReportArmArtifactV1 {
  Phase4TrialArm arm = Phase4TrialArm::kSequentialBaseline;
  std::uint64_t raw_semantic_checksum = 0;
  Phase4TrialArmDiagnosticExecutionV1 diagnostic;

  friend bool operator==(const Phase4PerNetReportArmArtifactV1&,
                         const Phase4PerNetReportArmArtifactV1&) = default;
};

// This is diagnostic evidence. It deliberately contains neither timing nor
// resource observations and can never be treated as decision-eligible raw
// evidence. The fixed array is validated in baseline-then-candidate order.
struct Phase4PerNetReportArtifactV1 {
  std::string source_commit;
  bool source_stamped = false;
  bool source_tree_dirty = true;
  std::uint64_t source_envelope_checksum = 0;
  std::uint32_t schema_version = kPhase4PerNetReportArtifactSchemaVersion;
  std::uint32_t raw_wire_schema_version = kPhase4TrialWireSchemaVersion;
  Phase4CanonicalCellConfig config;
  std::uint64_t corpus_checksum = 0;
  std::uint64_t raw_cell_plan_checksum = 0;
  std::uint64_t raw_cell_artifact_checksum = 0;
  std::uint64_t raw_source_envelope_checksum = 0;
  Phase4PerNetReportRawReferenceV1 raw_reference;
  bool decision_eligible = false;
  std::uint64_t workload_net_roster_checksum = 0;
  std::array<Phase4PerNetReportArmArtifactV1, 2> arms;
  std::uint64_t artifact_checksum = 0;

  friend bool operator==(const Phase4PerNetReportArtifactV1&,
                         const Phase4PerNetReportArtifactV1&) = default;
};

struct Phase4PerNetReportArtifactError {
  std::string invariant_id;
  std::string detail;

  friend bool operator==(const Phase4PerNetReportArtifactError&,
                         const Phase4PerNetReportArtifactError&) = default;
};

using Phase4PerNetReportArtifactResultV1 =
    std::variant<Phase4PerNetReportArtifactV1, Phase4PerNetReportArtifactError>;

[[nodiscard]] std::span<const Phase4WorkloadNetRosterManifestEntryV1>
Phase4WorkloadNetRosterManifestV1() noexcept;

[[nodiscard]] std::span<const Phase4WorkloadNetRosterManifestExclusionV1>
Phase4WorkloadNetRosterManifestExclusionsV1() noexcept;

[[nodiscard]] std::uint64_t ComputePhase4WorkloadNetRosterManifestChecksumV1() noexcept;

[[nodiscard]] const Phase4WorkloadNetRosterManifestEntryV1*
FindPhase4WorkloadNetRosterManifestEntryV1(std::uint32_t case_id) noexcept;

[[nodiscard]] std::uint64_t ComputePhase4WorkloadNetRosterChecksumV1(
    const Phase4RepresentativeCase& representative_case) noexcept;

[[nodiscard]] std::uint64_t ComputePhase4CanonicalCellPlanChecksumV1(
    const Phase4CanonicalCellConfig& config) noexcept;

[[nodiscard]] std::uint64_t ComputePhase4PerNetReportArtifactChecksumV1(
    const Phase4PerNetReportArtifactV1& artifact) noexcept;

[[nodiscard]] std::uint64_t ComputePhase4PerNetReportSourceEnvelopeChecksumV1(
    std::string_view source_commit, bool source_stamped, bool source_tree_dirty,
    std::uint64_t artifact_checksum) noexcept;

// Builds and authenticates one report companion. raw_reference names the exact
// successful repetition-zero baseline-first pair in the separate Raw cell.
// The carrier is restricted to the versioned Raw-v1/Wire-v1 or Raw-v2/Wire-v2
// publication paths.
[[nodiscard]] Phase4PerNetReportArtifactResultV1 BuildPhase4PerNetReportArtifactV1(
    const Phase4CanonicalCellConfig& config, std::string_view source_commit, bool source_stamped,
    bool source_tree_dirty, std::uint64_t raw_cell_plan_checksum,
    std::uint64_t raw_cell_artifact_checksum, std::uint64_t raw_source_envelope_checksum,
    Phase4PerNetReportRawReferenceV1 raw_reference,
    std::array<Phase4TrialArmDiagnosticExecutionV1, 2> diagnostics,
    std::string_view imported_fixture,
    std::uint32_t raw_wire_schema_version = kPhase4TrialWireSchemaVersion);

[[nodiscard]] std::variant<std::monostate, Phase4PerNetReportArtifactError>
ValidatePhase4PerNetReportArtifactV1(const Phase4PerNetReportArtifactV1& artifact,
                                     std::string_view imported_fixture);

// Canonical compact UTF-8 JSON: one object, fixed key order, exactly one LF.
[[nodiscard]] std::string SerializePhase4PerNetReportArtifactJsonV1(
    const Phase4PerNetReportArtifactV1& artifact);

}  // namespace apgar::benchmark

#endif  // APGAR_BENCHMARK_PHASE4_PER_NET_REPORT_ARTIFACT_H_
