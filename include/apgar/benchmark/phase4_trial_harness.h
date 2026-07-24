#ifndef APGAR_BENCHMARK_PHASE4_TRIAL_HARNESS_H_
#define APGAR_BENCHMARK_PHASE4_TRIAL_HARNESS_H_

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "apgar/benchmark/phase4_paired_trial.h"

namespace apgar::benchmark {

inline constexpr std::uint32_t kPhase4TrialHarnessSchemaVersion = 1;
inline constexpr std::uint32_t kPhase4TrialWireSchemaVersion = 1;
inline constexpr std::uint32_t kPhase4SameRunTrialWireSchemaVersion = 2;
inline constexpr std::uint32_t kPhase4SameRunRawEvidenceSchemaVersion = 2;
inline constexpr std::uint32_t kPhase4IsolatedSameRunTelemetrySchemaVersion = 1;
inline constexpr std::uint32_t kPhase4CanonicalRepetitionsV1 = 20;
inline constexpr std::uint32_t kPhase4CanonicalPreparationWorkersV1 = 4;
inline constexpr std::uint64_t kPhase4CanonicalRouteWorkUnitsPerQueryV1 = 1'000'000'000ULL;
inline constexpr std::uint64_t kPhase4CanonicalReconstructionStatesPerDeclaredPoolV1 = 32;
inline constexpr std::uint64_t kPhase4CanonicalCandidateDraftFixedBytesV1 = 4'096;
inline constexpr std::uint64_t kPhase4CanonicalCandidateDraftBytesPerStateV1 = 512;
inline constexpr std::uint64_t kPhase4CanonicalCandidateDraftBytesPerPolicyEntryV1 = 128;
inline constexpr std::uint64_t kPhase4CanonicalPreparationBytesPerBaseResourceV1 = 40;
inline constexpr std::uint64_t kPhase4CanonicalAdmissionFixedPolicyBytesV1 = 57;
inline constexpr std::uint64_t kPhase4CanonicalAdmissionBytesPerPolicyEntryV1 = 29;
inline constexpr std::uint64_t kPhase4CanonicalMaximumAggregatePolicyEntriesV1 = 250'000'000;
inline constexpr std::uint64_t kPhase4MaximumWatchdogNanosecondsV1 =
    24ULL * 60ULL * 60ULL * 1'000'000'000ULL;

struct Phase4CanonicalCellConfig {
  std::uint32_t schema_version = kPhase4TrialHarnessSchemaVersion;
  std::uint32_t case_id = 0;
  std::uint32_t requested_pool_size = 0;
  std::uint32_t preparation_worker_count = kPhase4CanonicalPreparationWorkersV1;
  std::uint32_t repetitions = kPhase4CanonicalRepetitionsV1;
  std::uint64_t maximum_setup_elapsed_nanoseconds = 0;
  Phase4ExternalBudget external_budget;
  Phase4RepresentativeCorpusLimits corpus_limits;

  friend bool operator==(const Phase4CanonicalCellConfig&,
                         const Phase4CanonicalCellConfig&) = default;
};

struct Phase4HostEnvironment {
  std::uint32_t schema_version = kPhase4TrialHarnessSchemaVersion;
  std::string host_os;
  std::string host_kernel;
  std::string host_architecture;
  std::string cpu_model;
  std::string compiler_identity;
  std::string monotonic_clock = "clock_monotonic";
  std::uint64_t online_cpu_count = 0;
  std::uint64_t affinity_cpu_count = 0;
  std::uint64_t total_host_memory_bytes = 0;
  std::uint64_t environment_checksum = 0;

  friend bool operator==(const Phase4HostEnvironment&, const Phase4HostEnvironment&) = default;
};

enum class Phase4DurableFailurePayloadKind : std::uint8_t {
  kSummaryOnly = 0,
  kCorpus = 1,
  kSequential = 2,
  kCandidatePreparation = 3,
  kCandidateSession = 4,
};

// Bounded, reconciled fingerprint of stable child diagnostics and bound
// witnesses. Heavyweight CandidateStore ownership is reconciled in the child
// into a checksum-covered canonical roster before process exit.
struct Phase4DurableArmFailure {
  std::uint32_t schema_version = kPhase4TrialHarnessSchemaVersion;
  Phase4PairedTrialErrorCode summary_code = Phase4PairedTrialErrorCode::kInternalInvariant;
  Phase4TrialArm arm = Phase4TrialArm::kSequentialBaseline;
  std::uint64_t summary_required = 0;
  std::uint64_t summary_configured = 0;
  std::string summary_invariant_id;
  std::string summary_detail;
  Phase4DurableFailurePayloadKind payload_kind = Phase4DurableFailurePayloadKind::kSummaryOnly;
  std::uint8_t child_error_code = 0;
  std::string child_invariant_id;
  std::string child_detail;
  bool has_child_net = false;
  std::uint64_t child_net_id = 0;
  std::uint32_t child_net_generation = 0;
  std::uint64_t child_required = 0;
  std::uint64_t child_configured = 0;
  std::uint8_t child_bound_kind = 0;
  std::uint64_t child_secondary_required = 0;
  std::uint64_t child_secondary_configured = 0;
  bool has_case_identity = false;
  std::uint32_t case_id = 0;
  std::uint64_t descriptor_fingerprint = 0;
  std::uint64_t case_checksum = 0;
  std::uint64_t board_content_hash = 0;
  std::uint64_t workload_checksum = 0;
  std::uint64_t capacity_model_checksum = 0;
  bool has_epoch_index = false;
  std::uint32_t epoch_index = 0;
  bool has_failed_observation = false;
  std::uint64_t failed_observation_checksum = 0;
  std::uint64_t attempted_column_count = 0;
  std::uint64_t attempted_route_queries = 0;
  std::uint64_t attempted_route_work_units = 0;
  bool candidate_store_publication_committed = false;
  bool authoritative_candidate_store_present = false;
  std::uint64_t reconciled_candidate_count = 0;
  std::uint64_t reconciled_rejection_count = 0;
  std::uint64_t reconciled_candidate_store_checksum = 0;
  std::uint64_t payload_checksum = 0;

  friend bool operator==(const Phase4DurableArmFailure&, const Phase4DurableArmFailure&) = default;
};

// A strict authority profile could not associate the retained typed failure
// with its selected corpus. The original move-only failure remains owned here,
// including any authoritative CandidateStore, so no state is discarded.
struct Phase4ArmFailureReconciliationRejectionV1 {
  std::string invariant_id;
  std::string detail;
  Phase4TrialArmFailure failure;
};

using Phase4ArmFailureReconciliationResultV1 =
    std::variant<Phase4DurableArmFailure, Phase4ArmFailureReconciliationRejectionV1>;

enum class Phase4IsolatedAttemptDisposition : std::uint8_t {
  kSuccess = 0,
  kTypedChildFailure = 1,
  kSetupTimeout = 2,
  kWallTimeout = 3,
  kSignalTermination = 4,
  kNonzeroExit = 5,
  kProtocolFailure = 6,
  kLaunchFailure = 7,
  kExternalBudgetExceeded = 8,
  kTeardownTimeout = 9,
  kNotRunAfterFatal = 10,
};

enum class Phase4IsolatedCellCarrier : std::uint8_t {
  kUnspecified = 0,
  kRawWireV1 = 1,
  kSameRunWireV2 = 2,
};

struct Phase4IsolatedArmAttempt {
  std::uint32_t schema_version = kPhase4TrialHarnessSchemaVersion;
  Phase4TrialArm arm = Phase4TrialArm::kSequentialBaseline;
  std::uint32_t repetition_index = 0;
  Phase4TrialOrder execution_order = Phase4TrialOrder::kBaselineFirst;
  Phase4IsolatedAttemptDisposition disposition = Phase4IsolatedAttemptDisposition::kProtocolFailure;
  std::uint64_t dispatch_ordinal = 0;
  std::uint64_t process_instance_identity = 0;
  std::uint64_t outer_elapsed_nanoseconds = 0;
  std::uint64_t process_lifetime_peak_host_bytes = 0;
  std::int32_t raw_wait_status = 0;
  std::int32_t process_exit_code = -1;
  std::int32_t terminating_signal = 0;
  bool watchdog_kill_sent = false;
  std::string controller_invariant_id;
  std::string controller_detail;
  std::optional<Phase4TrialArmRecord> record;
  std::optional<Phase4DurableArmFailure> child_failure;
  std::uint64_t attempt_checksum = 0;

  friend bool operator==(const Phase4IsolatedArmAttempt&,
                         const Phase4IsolatedArmAttempt&) = default;
};

struct Phase4IsolatedPairAttempt {
  std::uint32_t schema_version = kPhase4TrialHarnessSchemaVersion;
  std::uint32_t case_id = 0;
  std::uint32_t requested_pool_size = 0;
  std::uint32_t repetition_index = 0;
  std::uint64_t root_seed = 0;
  Phase4TrialOrder execution_order = Phase4TrialOrder::kBaselineFirst;
  Phase4IsolatedArmAttempt baseline;
  Phase4IsolatedArmAttempt candidate;
  std::optional<Phase4PairedTrialResult> result;
  std::uint64_t attempt_checksum = 0;

  friend bool operator==(const Phase4IsolatedPairAttempt&,
                         const Phase4IsolatedPairAttempt&) = default;
};

struct Phase4IsolatedCellResult {
  std::uint32_t schema_version = kPhase4TrialHarnessSchemaVersion;
  // Controller-owned, non-serialized carrier authority. Raw-v1 and Raw-v2
  // serializers reject results produced by the other worker protocol.
  Phase4IsolatedCellCarrier carrier = Phase4IsolatedCellCarrier::kUnspecified;
  Phase4CanonicalCellConfig config;
  Phase4HostEnvironment environment;
  std::uint64_t corpus_checksum = 0;
  std::uint64_t cell_plan_checksum = 0;
  std::uint64_t authority_run_identity = 0;
  std::uint64_t controller_identity = 0;
  std::vector<Phase4IsolatedPairAttempt> attempts;
  std::uint64_t artifact_checksum = 0;

  friend bool operator==(const Phase4IsolatedCellResult&,
                         const Phase4IsolatedCellResult&) = default;
};

// Controller-bound decision telemetry returned only after the exact worker
// process has exited cleanly and its ordinary measured arm has been finalized.
// The attempt, semantic, artifact, and authority checksums make this leaf
// unusable with any other dispatch or independently rerun observation.
struct Phase4IsolatedSameRunArmDecisionTelemetryV1 {
  std::uint32_t schema_version = kPhase4IsolatedSameRunTelemetrySchemaVersion;
  Phase4TrialArm arm = Phase4TrialArm::kSequentialBaseline;
  std::uint32_t repetition_index = 0;
  Phase4TrialOrder execution_order = Phase4TrialOrder::kBaselineFirst;
  std::uint64_t dispatch_ordinal = 0;
  std::uint64_t process_instance_identity = 0;
  std::uint64_t associated_semantic_checksum = 0;
  std::uint64_t associated_arm_artifact_checksum = 0;
  std::uint64_t associated_authority_checksum = 0;
  std::uint64_t associated_arm_attempt_checksum = 0;
  Phase4SameRunArmDecisionTelemetryV1 telemetry;
  std::uint64_t capture_checksum = 0;

  friend bool operator==(const Phase4IsolatedSameRunArmDecisionTelemetryV1&,
                         const Phase4IsolatedSameRunArmDecisionTelemetryV1&) = default;
};

struct Phase4IsolatedSameRunPairDecisionTelemetryV1 {
  std::uint32_t schema_version = kPhase4IsolatedSameRunTelemetrySchemaVersion;
  std::uint32_t case_id = 0;
  std::uint32_t requested_pool_size = 0;
  std::uint32_t repetition_index = 0;
  std::uint64_t root_seed = 0;
  Phase4TrialOrder execution_order = Phase4TrialOrder::kBaselineFirst;
  std::uint64_t associated_raw_pair_attempt_checksum = 0;
  std::uint64_t associated_paired_semantic_checksum = 0;
  std::uint64_t associated_paired_artifact_checksum = 0;
  Phase4IsolatedSameRunArmDecisionTelemetryV1 baseline;
  Phase4IsolatedSameRunArmDecisionTelemetryV1 candidate;
  std::uint64_t capture_checksum = 0;

  friend bool operator==(const Phase4IsolatedSameRunPairDecisionTelemetryV1&,
                         const Phase4IsolatedSameRunPairDecisionTelemetryV1&) = default;
};

struct Phase4IsolatedCellWithSameRunDecisionTelemetryV1 {
  std::uint32_t schema_version = kPhase4IsolatedSameRunTelemetrySchemaVersion;
  Phase4IsolatedCellResult raw_cell;
  std::vector<Phase4IsolatedSameRunPairDecisionTelemetryV1> same_run_attempts;
  friend bool operator==(const Phase4IsolatedCellWithSameRunDecisionTelemetryV1&,
                         const Phase4IsolatedCellWithSameRunDecisionTelemetryV1&) = default;
};

struct Phase4TrialHarnessError {
  std::string invariant_id;
  std::string detail;
  // Present when the controller completed and authenticated a Raw cell but a
  // higher-level companion could not be completed. Callers must preserve this
  // total-attempt evidence even though it is not decision-eligible.
  std::optional<Phase4IsolatedCellResult> raw_cell;

  friend bool operator==(const Phase4TrialHarnessError&, const Phase4TrialHarnessError&) = default;
};

using Phase4CanonicalSpecResult = std::variant<Phase4PairedTrialSpec, Phase4TrialHarnessError>;
using Phase4IsolatedCellExecution = std::variant<Phase4IsolatedCellResult, Phase4TrialHarnessError>;
using Phase4IsolatedCellWithSameRunDecisionTelemetryExecutionV1 =
    std::variant<Phase4IsolatedCellWithSameRunDecisionTelemetryV1, Phase4TrialHarnessError>;

[[nodiscard]] Phase4CanonicalSpecResult BuildPhase4CanonicalTrialSpecV1(
    const Phase4CanonicalCellConfig& cell, std::uint32_t repetition_index,
    Phase4TrialOrder execution_order) noexcept;

// Reconstructs only the frozen Representative Corpus v1 algorithm-budget
// preimage. The returned spec deliberately names legacy Session v3 and must
// never be passed to an execution entry point.
[[nodiscard]] Phase4CanonicalSpecResult BuildPhase4FrozenCanonicalBudgetPreimageV1(
    const Phase4CanonicalCellConfig& cell, std::uint32_t repetition_index,
    Phase4TrialOrder execution_order) noexcept;

[[nodiscard]] Phase4CanonicalSpecResult BuildPhase4CanonicalTrialSpecForCorpusV2(
    const Phase4CanonicalCellConfig& cell, std::uint32_t repetition_index,
    Phase4TrialOrder execution_order) noexcept;

[[nodiscard]] Phase4DurableArmFailure ReconcilePhase4TrialArmFailureV1(
    Phase4TrialArmFailure failure);

[[nodiscard]] Phase4DurableArmFailure ReconcilePhase4TrialArmFailureForCorpusV2(
    Phase4TrialArmFailure failure);

[[nodiscard]] Phase4ArmFailureReconciliationResultV1 TryReconcilePhase4TrialArmFailureV1(
    Phase4TrialArmFailure failure);

[[nodiscard]] Phase4ArmFailureReconciliationResultV1 TryReconcilePhase4TrialArmFailureForCorpusV2(
    Phase4TrialArmFailure failure);

[[nodiscard]] std::uint64_t ComputePhase4DurableArmFailureChecksumV1(
    const Phase4DurableArmFailure& failure) noexcept;

// Authenticates the source provenance fields carried by a raw evidence
// envelope together with the already-validated isolated cell artifact.
[[nodiscard]] std::uint64_t ComputePhase4SourceEnvelopeChecksumV1(
    std::uint32_t wire_schema_version, std::string_view source_commit, bool source_stamped,
    bool source_tree_dirty, std::uint64_t cell_artifact_checksum) noexcept;

// Authenticates the distinct Raw Evidence v2 envelope emitted only by the
// telemetry-aware Wire-v2 controller. The underlying isolated-cell payload
// remains harness schema v1 and cannot be relabeled as Raw Evidence v1.
[[nodiscard]] std::uint64_t ComputePhase4SourceEnvelopeChecksumV2(
    std::uint32_t raw_evidence_schema_version, std::uint32_t wire_schema_version,
    std::string_view source_commit, bool source_stamped, bool source_tree_dirty,
    std::uint64_t cell_artifact_checksum) noexcept;

// Raw-v1 preserves its frozen cell domain. Raw-v2 binds the telemetry-aware
// carrier versions before the otherwise identical cell payload.
[[nodiscard]] std::uint64_t ComputePhase4IsolatedCellArtifactChecksumV1(
    const Phase4IsolatedCellResult& result) noexcept;

[[nodiscard]] std::uint64_t ComputePhase4SameRunIsolatedCellArtifactChecksumV2(
    const Phase4IsolatedCellResult& result) noexcept;

// Linux v1 launches two separately exec'd, long-lived contender workers. All
// repetitions in a cell share their contender process and its wait4 peak RSS;
// measured arms run serially in the prescribed AB/BA order.
[[nodiscard]] Phase4IsolatedCellExecution RunPhase4IsolatedCellV1(
    const Phase4CanonicalCellConfig& cell, std::string_view worker_executable,
    std::string_view imported_fixture_path);

[[nodiscard]] Phase4IsolatedCellExecution RunPhase4IsolatedCellForCorpusV2(
    const Phase4CanonicalCellConfig& cell, std::string_view worker_executable,
    std::string_view imported_fixture_path);

[[nodiscard]] std::uint64_t ComputePhase4IsolatedSameRunArmCaptureChecksumV1(
    const Phase4IsolatedSameRunArmDecisionTelemetryV1& capture) noexcept;

[[nodiscard]] std::uint64_t ComputePhase4IsolatedSameRunPairCaptureChecksumV1(
    const Phase4IsolatedSameRunPairDecisionTelemetryV1& capture) noexcept;

[[nodiscard]] std::uint64_t ComputePhase4IsolatedSameRunCellCaptureChecksumV1(
    const Phase4IsolatedCellWithSameRunDecisionTelemetryV1& capture,
    std::uint64_t raw_source_envelope_checksum) noexcept;

// Verifies the complete in-memory Raw-v2/capture join before a serializer can
// publish it. This repeats the immutable checksum, command association,
// process-lifetime, and canonical-cardinality checks at the publication seam.
[[nodiscard]] bool ValidatePhase4IsolatedSameRunCellCaptureV1(
    const Phase4IsolatedCellWithSameRunDecisionTelemetryV1& capture,
    std::string_view imported_fixture);

[[nodiscard]] bool ValidatePhase4IsolatedSameRunCellCaptureForCorpusV2(
    const Phase4IsolatedCellWithSameRunDecisionTelemetryV1& capture,
    std::string_view imported_fixture);

#if defined(APGAR_PHASE4_TRIAL_FAULT_TEST_VARIANT)
// Test-only adversarial splice used to prove that publication validates
// command/cell associations after every exposed structural checksum is
// recomputed.
void RehashForeignPhase4SameRunRecordForTesting(
    Phase4IsolatedCellWithSameRunDecisionTelemetryV1* capture) noexcept;
void RehashWrongPhase4SameRunComparisonForTesting(
    Phase4IsolatedCellWithSameRunDecisionTelemetryV1* capture) noexcept;
void RehashNondeterministicPhase4SameRunRecordForTesting(
    Phase4IsolatedCellWithSameRunDecisionTelemetryV1* capture) noexcept;
#endif

// Runs the canonical cell through wire v2. Success is all-or-nothing: exactly
// twenty finalized pairs and their forty same-run telemetry leaves are returned
// only after both persistent worker processes exit cleanly.
[[nodiscard]] Phase4IsolatedCellWithSameRunDecisionTelemetryExecutionV1
RunPhase4IsolatedCellWithSameRunDecisionTelemetryV1(const Phase4CanonicalCellConfig& cell,
                                                    std::string_view worker_executable,
                                                    std::string_view imported_fixture_path);

[[nodiscard]] Phase4IsolatedCellWithSameRunDecisionTelemetryExecutionV1
RunPhase4IsolatedCellWithSameRunDecisionTelemetryForCorpusV2(
    const Phase4CanonicalCellConfig& cell, std::string_view worker_executable,
    std::string_view imported_fixture_path);

[[nodiscard]] std::optional<std::string> SerializePhase4IsolatedCellJsonV1(
    const Phase4IsolatedCellResult& result, std::string_view source_commit, bool source_stamped,
    bool source_tree_dirty);

// Serializes the ordinary measured half of a telemetry-aware Wire-v2 run.
// Publication requires its same-run telemetry companion; Raw-v1 validators
// intentionally reject this distinct envelope.
[[nodiscard]] std::optional<std::string> SerializePhase4SameRunIsolatedCellJsonV2(
    const Phase4IsolatedCellResult& result, std::string_view source_commit, bool source_stamped,
    bool source_tree_dirty);

// Hidden worker entry point used by the source-identical harness executable.
// The request and response descriptors are dedicated protocol sockets.
[[nodiscard]] int RunPhase4TrialWorkerV1(Phase4TrialArm arm, const Phase4CanonicalCellConfig& cell,
                                         std::string_view imported_fixture, int request_descriptor,
                                         int response_descriptor) noexcept;

[[nodiscard]] int RunPhase4TrialWorkerForCorpusV2(Phase4TrialArm arm,
                                                  const Phase4CanonicalCellConfig& cell,
                                                  std::string_view imported_fixture,
                                                  int request_descriptor,
                                                  int response_descriptor) noexcept;

// Hidden wire-v2 worker entry point. Control and failure messages retain their
// v1 shape; every success atomically carries the ordinary measured execution
// and telemetry derived from that same execution.
[[nodiscard]] int RunPhase4TrialWorkerWithSameRunTelemetryV1(Phase4TrialArm arm,
                                                             const Phase4CanonicalCellConfig& cell,
                                                             std::string_view imported_fixture,
                                                             int request_descriptor,
                                                             int response_descriptor) noexcept;

[[nodiscard]] int RunPhase4TrialWorkerWithSameRunTelemetryForCorpusV2(
    Phase4TrialArm arm, const Phase4CanonicalCellConfig& cell, std::string_view imported_fixture,
    int request_descriptor, int response_descriptor) noexcept;

}  // namespace apgar::benchmark

#endif  // APGAR_BENCHMARK_PHASE4_TRIAL_HARNESS_H_
