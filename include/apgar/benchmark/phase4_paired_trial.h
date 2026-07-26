#ifndef APGAR_BENCHMARK_PHASE4_PAIRED_TRIAL_H_
#define APGAR_BENCHMARK_PHASE4_PAIRED_TRIAL_H_

#include <cstdint>
#include <optional>
#include <string_view>
#include <variant>
#include <vector>

#include "apgar/allocator/cpu_candidate_allocation_session.h"
#include "apgar/allocator/sequential_negotiated_baseline.h"
#include "apgar/benchmark/phase4_representative_corpus.h"
#include "apgar/candidates/route_candidate.h"

namespace apgar::benchmark {

inline constexpr std::uint32_t kPhase4PairedTrialSchemaVersion = 1;
inline constexpr std::uint32_t kPhase4ExternalAuthoritySchemaVersion = 1;
inline constexpr std::uint32_t kPhase4PerNetReportSchemaVersion = 1;
inline constexpr std::uint32_t kPhase4ArmReportTelemetrySchemaVersion = 1;
inline constexpr std::uint32_t kPhase4SameRunArmDecisionTelemetrySchemaVersion = 1;
inline constexpr std::uint32_t kPhase4TrialArmOperationalProfileSchemaVersion = 1;
inline constexpr std::uint32_t kPhase4TrialArmReplayAuthoritySchemaVersion = 1;
inline constexpr std::uint64_t kPhase4OverlapPartsPerMillion = 1'000'000;

enum class Phase4TrialArm : std::uint8_t {
  kSequentialBaseline = 0,
  kReusableCandidateAllocation = 1,
};

enum class Phase4TrialOrder : std::uint8_t {
  kBaselineFirst = 0,
  kCandidateFirst = 1,
};

enum class Phase4NormalizedTerminalReason : std::uint8_t {
  kFeasible = 0,
  kNoAdmissibleCandidate = 1,
  kFixedPointStalled = 2,
  kStoppingBudgetExhausted = 3,
  kResourceRefinementRequired = 4,
};

enum class Phase4CandidateOutcomeSource : std::uint8_t {
  kNotCandidateArm = 0,
  kPreferredMultiWorld = 1,
  kCommonLineageOneWorld = 2,
};

struct Phase4ExternalBudget {
  std::uint64_t maximum_prepared_elapsed_nanoseconds = 0;
  std::uint64_t maximum_cold_elapsed_nanoseconds = 0;
  std::uint64_t maximum_address_space_bytes = 0;
  std::uint64_t maximum_peak_host_bytes = 0;

  friend bool operator==(const Phase4ExternalBudget&, const Phase4ExternalBudget&) = default;
};

struct Phase4PairedTrialSpec {
  std::uint32_t schema_version = kPhase4PairedTrialSchemaVersion;
  std::uint32_t case_id = 0;
  std::uint32_t requested_pool_size = 0;
  std::uint32_t repetition_index = 0;
  std::uint64_t root_seed = 0;
  Phase4TrialOrder execution_order = Phase4TrialOrder::kBaselineFirst;
  std::uint32_t preparation_worker_count = 1;
  Phase4RepresentativeCorpusLimits corpus_limits;
  allocator::SequentialNegotiatedBaselineConfig baseline_config;
  allocator::CpuCandidatePoolPreparationConfig preparation_config;
  allocator::CpuCandidateAllocationSessionConfig candidate_session_config;
  Phase4ExternalBudget external_budget;
};

struct Phase4BoardOutcome {
  std::uint64_t selected_net_count = 0;
  std::uint64_t no_candidate_net_count = 0;
  std::uint64_t overused_resource_count = 0;
  std::uint64_t total_overuse_units = 0;
  std::uint64_t total_intrinsic_cost = 0;
  std::uint64_t world_checksum = 0;

  friend bool operator==(const Phase4BoardOutcome&, const Phase4BoardOutcome&) = default;
};

struct Phase4RouteOpportunity {
  std::uint64_t route_queries = 0;
  std::uint64_t route_work_units = 0;

  friend bool operator==(const Phase4RouteOpportunity&, const Phase4RouteOpportunity&) = default;
};

struct Phase4TrialArmSemantics {
  std::uint32_t schema_version = kPhase4PairedTrialSchemaVersion;
  Phase4TrialArm arm = Phase4TrialArm::kSequentialBaseline;
  Phase4TrialOrder execution_order = Phase4TrialOrder::kBaselineFirst;
  std::uint32_t corpus_version = kPhase4RepresentativeCorpusVersion;
  std::uint64_t corpus_checksum = 0;
  std::uint32_t case_id = 0;
  std::uint64_t descriptor_fingerprint = 0;
  std::uint64_t case_checksum = 0;
  std::uint64_t board_content_hash = 0;
  std::uint64_t workload_checksum = 0;
  std::uint64_t capacity_model_checksum = 0;
  std::uint64_t budget_checksum = 0;
  std::uint32_t workload_net_count = 0;
  std::uint32_t requested_pool_size = 0;
  std::uint32_t repetition_index = 0;
  std::uint64_t root_seed = 0;
  std::uint32_t preparation_worker_count = 0;
  std::uint32_t baseline_sweeps = 0;
  std::uint32_t candidate_regeneration_epochs = 0;
  std::uint64_t candidate_columns_per_epoch = 0;
  std::uint32_t candidate_terminal_selection_rounds = 0;
  Phase4ExternalBudget external_budget;
  Phase4RouteOpportunity opportunity;
  Phase4RouteOpportunity actual;
  std::uint64_t preparation_route_queries = 0;
  std::uint64_t preparation_route_work_units = 0;
  std::uint64_t regeneration_route_queries = 0;
  std::uint64_t regeneration_route_work_units = 0;
  std::uint64_t requested_columns = 0;
  std::uint64_t admitted_candidates = 0;
  std::uint64_t rejected_columns = 0;
  std::uint64_t final_candidate_count = 0;
  std::uint64_t preparation_checksum = 0;
  std::uint64_t algorithm_session_checksum = 0;
  std::uint64_t final_pool_manifest_checksum = 0;
  std::uint64_t final_rejection_manifest_checksum = 0;
  Phase4NormalizedTerminalReason terminal_reason =
      Phase4NormalizedTerminalReason::kStoppingBudgetExhausted;
  Phase4CandidateOutcomeSource candidate_outcome_source =
      Phase4CandidateOutcomeSource::kNotCandidateArm;
  Phase4BoardOutcome outcome;
  std::uint64_t semantic_checksum = 0;

  friend bool operator==(const Phase4TrialArmSemantics&, const Phase4TrialArmSemantics&) = default;
};

enum class Phase4ExternalAuthorityKind : std::uint8_t {
  kLinuxParentWatchdogRlimitAndWait4 = 0,
};

struct Phase4PreparerLifecycleObservation {
  std::uint64_t workers_started_before = 0;
  std::uint64_t workers_started_after = 0;
  std::uint64_t invocations_started_before = 0;
  std::uint64_t invocations_started_after = 0;
  std::uint64_t invocations_completed_before = 0;
  std::uint64_t invocations_completed_after = 0;

  friend bool operator==(const Phase4PreparerLifecycleObservation&,
                         const Phase4PreparerLifecycleObservation&) = default;
};

struct Phase4TrialArmExecution {
  Phase4TrialArmSemantics semantics;
  std::uint64_t case_build_elapsed_nanoseconds = 0;
  std::uint64_t prepared_elapsed_nanoseconds = 0;
  std::uint64_t cold_elapsed_nanoseconds = 0;
  Phase4PreparerLifecycleObservation preparer_lifecycle;

  friend bool operator==(const Phase4TrialArmExecution&, const Phase4TrialArmExecution&) = default;
};

// A separately instrumented replay of one authentic contender. Raw decision
// timing does not call this seam. Exactly one baseline profile or the pair of
// candidate preparation/session profiles is present according to the arm.
// This measured capture is not standalone publication authority; the
// operational publication contract joins it to an independent unmeasured
// full-preimage replay and parent-owned process observation.
struct Phase4TrialArmOperationalProfileV1 {
  std::uint32_t schema_version = kPhase4TrialArmOperationalProfileSchemaVersion;
  Phase4TrialArmExecution execution;
  Phase4RepresentativeCaseOperationalProfileV1 case_build;
  Phase4OperationalApplicabilityV1 process_cpu;
  Phase4OperationalApplicabilityV1 peak_host_memory;
  Phase4OperationalApplicabilityV1 compatible_batch_formation_and_fill;
  Phase4OperationalApplicabilityV1 compact_readback;
  Phase4OperationalApplicabilityV1 prepared_view_cache_and_cache_misses;
  Phase4OperationalApplicabilityV1 initial_device_upload;
  Phase4OperationalApplicabilityV1 gpu_utilization;
  Phase4OperationalApplicabilityV1 peak_device_memory;
  std::uint64_t contender_transient_release_tail_wall_nanoseconds = 0;
  std::uint64_t unclassified_prepared_scope_wall_nanoseconds = 0;
  std::uint64_t unclassified_cold_scope_exit_wall_nanoseconds = 0;
  std::optional<allocator::SequentialNegotiatedBaselineOperationalProfileV1> baseline;
  std::optional<allocator::CpuCandidatePoolPreparationOperationalProfileV1> preparation;
  std::optional<allocator::CpuCandidateAllocationSessionOperationalProfileV1> candidate_session;
  std::uint64_t profile_checksum = 0;

  friend bool operator==(const Phase4TrialArmOperationalProfileV1&,
                         const Phase4TrialArmOperationalProfileV1&) = default;
};

// Independently replays one contender with all operational clocks compiled
// out. Candidate authority is distilled from the live, complete allocation
// session before its full config, epoch columns, pools, price state, and worlds
// are destroyed. Publication compares this authority with the compact witness
// returned by the separately measured operational replay.
struct Phase4TrialArmReplayAuthorityV1 {
  std::uint32_t schema_version = kPhase4TrialArmReplayAuthoritySchemaVersion;
  Phase4TrialArmSemantics semantics;
  Phase4PreparerLifecycleObservation preparer_lifecycle;
  std::uint64_t recomputed_full_preimage_session_checksum = 0;
  std::optional<allocator::CpuCandidateAllocationSessionReplayWitnessV1> candidate_session_witness;
  std::uint64_t authority_checksum = 0;

  friend bool operator==(const Phase4TrialArmReplayAuthorityV1&,
                         const Phase4TrialArmReplayAuthorityV1&) = default;
};

// One closed partition of every column requested for one net. Executed route
// queries exclude only explicit proof-backed skipped columns. Duplicate,
// disconnected, unsupported, skipped, exact-validation, and other rejections
// together with admissions exactly partition requested_columns.
struct Phase4PerNetColumnOutcomesV1 {
  std::uint64_t requested_columns = 0;
  std::uint64_t executed_route_queries = 0;
  std::uint64_t admitted_candidates = 0;
  std::uint64_t duplicate_candidates = 0;
  std::uint64_t disconnected_columns = 0;
  std::uint64_t unsupported_columns = 0;
  std::uint64_t skipped_columns = 0;
  std::uint64_t exact_validation_rejections = 0;
  std::uint64_t other_rejections = 0;

  friend bool operator==(const Phase4PerNetColumnOutcomesV1&,
                         const Phase4PerNetColumnOutcomesV1&) = default;
};

struct Phase4PerNetReportV1 {
  std::uint32_t schema_version = kPhase4PerNetReportSchemaVersion;
  board_ir::EntityRef net{};
  Phase4PerNetColumnOutcomesV1 columns;
  std::uint64_t final_pool_size = 0;
  std::uint64_t unique_geometry_signature_count = 0;
  std::uint64_t unique_resource_signature_count = 0;
  std::uint64_t candidate_pair_count = 0;
  std::uint64_t mean_resource_overlap_ppm = 0;
  std::uint64_t minimum_resource_overlap_ppm = 0;
  std::uint64_t mean_geometric_overlap_ppm = 0;
  std::uint64_t minimum_geometric_overlap_ppm = 0;
  allocator::NetSelectionStatus selected_status =
      allocator::NetSelectionStatus::kNoAdmissibleCandidate;
  std::optional<candidates::CandidateId> selected_candidate_id;
  std::optional<std::uint64_t> selected_candidate_payload_checksum;
  std::optional<candidates::CandidateMetrics> selected_candidate_metrics;
  std::optional<std::uint64_t> pool_best_intrinsic_cost;

  friend bool operator==(const Phase4PerNetReportV1&, const Phase4PerNetReportV1&) = default;
};

// Diagnostic telemetry is intentionally separate from the decision-eligible
// raw-evidence wire. Its association names the exact semantic checksum returned
// by the same in-process contender execution.
struct Phase4ArmReportTelemetryV1 {
  std::uint32_t schema_version = kPhase4ArmReportTelemetrySchemaVersion;
  std::uint64_t associated_semantic_checksum = 0;
  std::vector<Phase4PerNetReportV1> per_net;
  std::uint64_t telemetry_checksum = 0;

  friend bool operator==(const Phase4ArmReportTelemetryV1&,
                         const Phase4ArmReportTelemetryV1&) = default;
};

struct Phase4TrialArmDiagnosticExecutionV1 {
  // Deliberately not a Phase4TrialArmExecution: diagnostic overlap work is not
  // an isolated measured arm and cannot be passed to FinalizePhase4TrialArmV1.
  Phase4TrialArmSemantics semantics;
  Phase4ArmReportTelemetryV1 telemetry;

  friend bool operator==(const Phase4TrialArmDiagnosticExecutionV1&,
                         const Phase4TrialArmDiagnosticExecutionV1&) = default;
};

// Minimal decision columns captured while the contender's authentic result
// objects are still alive. Unlike Phase4PerNetReportV1, this leaf deliberately
// excludes pool-diversity and selection-detail derivation.
struct Phase4SameRunPerNetColumnOutcomesV1 {
  board_ir::EntityRef net{};
  Phase4PerNetColumnOutcomesV1 columns;

  friend bool operator==(const Phase4SameRunPerNetColumnOutcomesV1&,
                         const Phase4SameRunPerNetColumnOutcomesV1&) = default;
};

struct Phase4SameRunArmDecisionTelemetryV1 {
  std::uint32_t schema_version = kPhase4SameRunArmDecisionTelemetrySchemaVersion;
  std::uint64_t associated_semantic_checksum = 0;
  Phase4BoardOutcome outcome;
  std::vector<Phase4SameRunPerNetColumnOutcomesV1> per_net;
  std::uint64_t telemetry_checksum = 0;

  friend bool operator==(const Phase4SameRunArmDecisionTelemetryV1&,
                         const Phase4SameRunArmDecisionTelemetryV1&) = default;
};

struct Phase4TrialArmWithSameRunTelemetryExecutionV1 {
  Phase4TrialArmExecution execution;
  Phase4SameRunArmDecisionTelemetryV1 telemetry;

  friend bool operator==(const Phase4TrialArmWithSameRunTelemetryExecutionV1&,
                         const Phase4TrialArmWithSameRunTelemetryExecutionV1&) = default;
};

// Candidate-only diagnostic capture. The immutable StoredCandidate handles are
// copied while the authoritative allocation session and CandidateStore are
// alive, so the complete frozen pool membership remains available after the
// session is destroyed. This is not decision-eligible timing evidence.
struct Phase4CandidatePoolSnapshotExecutionV1 {
  Phase4TrialArmSemantics semantics;
  Phase4ArmReportTelemetryV1 telemetry;
  std::uint32_t capacity_schema_version = allocator::kResourceCapacityModelSchemaVersion;
  allocator::AllocationAssociations capacity_associations;
  std::uint32_t default_capacity_units = 0;
  std::vector<allocator::ResourceCapacityOverride> capacity_overrides;
  std::vector<allocator::CandidatePool> final_pools;
  allocator::OneWorldAllocation production_world;
};

struct Phase4ExternalResourceObservation {
  std::uint32_t schema_version = kPhase4ExternalAuthoritySchemaVersion;
  Phase4ExternalAuthorityKind authority_kind =
      Phase4ExternalAuthorityKind::kLinuxParentWatchdogRlimitAndWait4;
  std::uint64_t authority_run_identity = 0;
  std::uint64_t controller_identity = 0;
  std::uint64_t process_instance_identity = 0;
  std::uint64_t associated_semantic_checksum = 0;
  std::uint64_t configured_wall_limit_nanoseconds = 0;
  std::uint64_t configured_address_space_limit_bytes = 0;
  std::uint64_t configured_peak_host_limit_bytes = 0;
  std::uint64_t outer_elapsed_nanoseconds = 0;
  std::uint64_t peak_host_bytes = 0;
  std::int32_t process_exit_code = 0;
  bool isolated_process = false;
  bool wall_authority_enforced = false;
  bool memory_authority_enforced = false;
  bool persistent_preparer_reused = false;
  Phase4PreparerLifecycleObservation preparer_lifecycle;
  std::uint64_t authority_checksum = 0;

  friend bool operator==(const Phase4ExternalResourceObservation&,
                         const Phase4ExternalResourceObservation&) = default;
};

struct Phase4TrialArmRecord {
  Phase4TrialArmSemantics semantics;
  std::uint64_t case_build_elapsed_nanoseconds = 0;
  std::uint64_t prepared_elapsed_nanoseconds = 0;
  std::uint64_t cold_elapsed_nanoseconds = 0;
  Phase4PreparerLifecycleObservation preparer_lifecycle;
  Phase4ExternalResourceObservation external_observation;
  std::uint64_t artifact_checksum = 0;

  friend bool operator==(const Phase4TrialArmRecord&, const Phase4TrialArmRecord&) = default;
};

enum class Phase4LexicographicComparison : std::uint8_t {
  kTie = 0,
  kBaselinePreferred = 1,
  kCandidatePreferred = 2,
};

struct Phase4PairedTrialResult {
  std::uint32_t schema_version = kPhase4PairedTrialSchemaVersion;
  Phase4TrialArmRecord baseline;
  Phase4TrialArmRecord candidate;
  Phase4LexicographicComparison comparison = Phase4LexicographicComparison::kTie;
  std::uint64_t semantic_checksum = 0;
  std::uint64_t artifact_checksum = 0;

  friend bool operator==(const Phase4PairedTrialResult&, const Phase4PairedTrialResult&) = default;
};

enum class Phase4PairedTrialErrorCode : std::uint8_t {
  kUnsupportedSchema = 0,
  kUnknownCase = 1,
  kInvalidConfiguration = 2,
  kUnequalBudget = 3,
  kCaseBuild = 4,
  kCaseIdentityMismatch = 5,
  kSequentialExecution = 6,
  kCandidatePreparation = 7,
  kCandidateSession = 8,
  kMeasurementAssociation = 9,
  kExternalAuthorityUnavailable = 10,
  kExternalBudgetExceeded = 11,
  kPairMismatch = 12,
  kResourceExhausted = 13,
  kInternalInvariant = 14,
};

struct Phase4PairedTrialError {
  Phase4PairedTrialErrorCode code = Phase4PairedTrialErrorCode::kInvalidConfiguration;
  std::string_view invariant_id;
  std::string_view detail;
  Phase4TrialArm arm = Phase4TrialArm::kSequentialBaseline;
  std::uint64_t required = 0;
  std::uint64_t configured = 0;

  friend bool operator==(const Phase4PairedTrialError&, const Phase4PairedTrialError&) = default;
};

struct Phase4SequentialFailureState {
  Phase4RepresentativeCase case_state;
  allocator::SequentialNegotiatedBaselineError error;
};

struct Phase4CandidatePreparationFailureState {
  Phase4RepresentativeCase case_state;
  allocator::CpuCandidatePoolPreparationError error;
};

struct Phase4CandidateSessionFailureState {
  Phase4RepresentativeCase case_state;
  allocator::PreparedCpuCandidatePools prepared;
  allocator::CpuCandidateAllocationSessionError error;
};

using Phase4TrialArmFailurePayload =
    std::variant<std::monostate, Phase4RepresentativeCorpusError, Phase4SequentialFailureState,
                 Phase4CandidatePreparationFailureState, Phase4CandidateSessionFailureState>;

// Move-only so a failed child observation can retain its authoritative
// committed CandidateStore and the independently built case needed to
// reconcile it. The summary is indexing metadata, never a replacement for the
// typed payload.
struct Phase4TrialArmFailure {
  Phase4PairedTrialError summary;
  Phase4TrialArmFailurePayload payload;
};

using Phase4TrialArmExecutionResult = std::variant<Phase4TrialArmExecution, Phase4TrialArmFailure>;
using Phase4TrialArmDiagnosticExecutionResultV1 =
    std::variant<Phase4TrialArmDiagnosticExecutionV1, Phase4TrialArmFailure>;
using Phase4TrialArmWithSameRunTelemetryExecutionResultV1 =
    std::variant<Phase4TrialArmWithSameRunTelemetryExecutionV1, Phase4TrialArmFailure>;
using Phase4TrialArmOperationalProfileResultV1 =
    std::variant<Phase4TrialArmOperationalProfileV1, Phase4TrialArmFailure>;
using Phase4TrialArmReplayAuthorityResultV1 =
    std::variant<Phase4TrialArmReplayAuthorityV1, Phase4TrialArmFailure>;
using Phase4CandidatePoolSnapshotExecutionResultV1 =
    std::variant<Phase4CandidatePoolSnapshotExecutionV1, Phase4TrialArmFailure>;
using Phase4TrialArmRecordResult = std::variant<Phase4TrialArmRecord, Phase4PairedTrialError>;
using Phase4PairedTrialAssemblyResult =
    std::variant<Phase4PairedTrialResult, Phase4PairedTrialError>;

// Builds one independent representative case, executes exactly one contender,
// distills compact semantic evidence, and destroys heavyweight state before
// returning. Process isolation, watchdog enforcement, and peak-memory
// measurement belong to the external observation finalized below.
[[nodiscard]] Phase4TrialArmExecutionResult ExecutePhase4TrialArmV1(
    Phase4TrialArm arm, const Phase4PairedTrialSpec& spec, std::string_view imported_fixture,
    allocator::PersistentCpuCandidatePoolPreparer* candidate_preparer = nullptr);

// Executes the same contender path and additionally derives checksum-bound
// per-net reporting telemetry while the authentic final pools, columns, and
// selected world are still alive. It does not alter the raw evidence wire.
[[nodiscard]] Phase4TrialArmDiagnosticExecutionResultV1 ExecutePhase4TrialArmDiagnosticV1(
    Phase4TrialArm arm, const Phase4PairedTrialSpec& spec, std::string_view imported_fixture,
    allocator::PersistentCpuCandidatePoolPreparer* candidate_preparer = nullptr);

// Executes one authentic contender and returns its ordinary measured execution
// together with the board outcome and per-net column partition captured from
// that exact execution. External process authority remains responsible for
// finalizing the measured arm.
[[nodiscard]] Phase4TrialArmWithSameRunTelemetryExecutionResultV1
ExecutePhase4TrialArmWithSameRunTelemetryV1(
    Phase4TrialArm arm, const Phase4PairedTrialSpec& spec, std::string_view imported_fixture,
    allocator::PersistentCpuCandidatePoolPreparer* candidate_preparer = nullptr);

// Executes a separate diagnostic replay with allocator-stage clocks enabled.
// Isolated parent wait4 authority supplies process CPU and peak-memory values
// in the later publication layer. This timing is operational context only and
// is never substituted for Raw decision timing.
[[nodiscard]] Phase4TrialArmOperationalProfileResultV1 ExecutePhase4TrialArmOperationalProfileV1(
    Phase4TrialArm arm, const Phase4PairedTrialSpec& spec, std::string_view imported_fixture,
    allocator::PersistentCpuCandidatePoolPreparer* candidate_preparer = nullptr);

// Executes the independent unmeasured replay used to authenticate an
// operational profile. It is a distinct invocation and must never be relabeled
// as the measured replay or as a Raw evidence attempt.
[[nodiscard]] Phase4TrialArmReplayAuthorityResultV1 ExecutePhase4TrialArmReplayAuthorityV1(
    Phase4TrialArm arm, const Phase4PairedTrialSpec& spec, std::string_view imported_fixture,
    allocator::PersistentCpuCandidatePoolPreparer* candidate_preparer = nullptr);

// Executes the reusable-candidate contender and captures its complete frozen
// final pools, current capacity vocabulary, and production-selected world
// before the allocation session is destroyed.
[[nodiscard]] Phase4CandidatePoolSnapshotExecutionResultV1 ExecutePhase4CandidatePoolSnapshotV1(
    const Phase4PairedTrialSpec& spec, std::string_view imported_fixture,
    allocator::PersistentCpuCandidatePoolPreparer* candidate_preparer);

// Converts an independently measured arm execution into a decision-eligible
// record only when isolated wall and memory authorities were both enforced.
[[nodiscard]] Phase4TrialArmRecordResult FinalizePhase4TrialArmV1(
    Phase4TrialArmExecution execution, const Phase4ExternalResourceObservation& observation);

// Requires one baseline and one candidate record with identical case, root,
// opportunity, stopping-depth, order, and external-budget identities.
[[nodiscard]] Phase4PairedTrialAssemblyResult AssemblePhase4PairedTrialV1(
    Phase4TrialArmRecord baseline, Phase4TrialArmRecord candidate);

// Preserved Corpus-v2 validation/finalization surfaces remain separate from
// the v1 entry points. Corpus authority is selected out of band and is never
// added to the frozen Phase4PairedTrialSpec v1 field set. Every Corpus-v2
// execution surface below fails closed after Session-v5 activation until a
// separately reviewed successor budget and consuming authority exists.
[[nodiscard]] Phase4TrialArmRecordResult FinalizePhase4TrialArmForCorpusV2(
    Phase4TrialArmExecution execution, const Phase4ExternalResourceObservation& observation);

[[nodiscard]] Phase4PairedTrialAssemblyResult AssemblePhase4PairedTrialForCorpusV2(
    Phase4TrialArmRecord baseline, Phase4TrialArmRecord candidate);

[[nodiscard]] Phase4TrialArmExecutionResult ExecutePhase4TrialArmForCorpusV2(
    Phase4TrialArm arm, const Phase4PairedTrialSpec& spec, std::string_view imported_fixture,
    allocator::PersistentCpuCandidatePoolPreparer* candidate_preparer = nullptr);

[[nodiscard]] Phase4TrialArmDiagnosticExecutionResultV1 ExecutePhase4TrialArmDiagnosticForCorpusV2(
    Phase4TrialArm arm, const Phase4PairedTrialSpec& spec, std::string_view imported_fixture,
    allocator::PersistentCpuCandidatePoolPreparer* candidate_preparer = nullptr);

[[nodiscard]] Phase4TrialArmWithSameRunTelemetryExecutionResultV1
ExecutePhase4TrialArmWithSameRunTelemetryForCorpusV2(
    Phase4TrialArm arm, const Phase4PairedTrialSpec& spec, std::string_view imported_fixture,
    allocator::PersistentCpuCandidatePoolPreparer* candidate_preparer = nullptr);

[[nodiscard]] Phase4TrialArmOperationalProfileResultV1
ExecutePhase4TrialArmOperationalProfileForCorpusV2(
    Phase4TrialArm arm, const Phase4PairedTrialSpec& spec, std::string_view imported_fixture,
    allocator::PersistentCpuCandidatePoolPreparer* candidate_preparer = nullptr);

[[nodiscard]] Phase4TrialArmReplayAuthorityResultV1 ExecutePhase4TrialArmReplayAuthorityForCorpusV2(
    Phase4TrialArm arm, const Phase4PairedTrialSpec& spec, std::string_view imported_fixture,
    allocator::PersistentCpuCandidatePoolPreparer* candidate_preparer = nullptr);

[[nodiscard]] Phase4CandidatePoolSnapshotExecutionResultV1
ExecutePhase4CandidatePoolSnapshotForCorpusV2(
    const Phase4PairedTrialSpec& spec, std::string_view imported_fixture,
    allocator::PersistentCpuCandidatePoolPreparer* candidate_preparer);

}  // namespace apgar::benchmark

#endif  // APGAR_BENCHMARK_PHASE4_PAIRED_TRIAL_H_
