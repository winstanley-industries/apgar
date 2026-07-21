#ifndef APGAR_BENCHMARK_PHASE4_PAIRED_TRIAL_H_
#define APGAR_BENCHMARK_PHASE4_PAIRED_TRIAL_H_

#include <cstdint>
#include <string_view>
#include <variant>

#include "apgar/allocator/cpu_candidate_allocation_session.h"
#include "apgar/allocator/sequential_negotiated_baseline.h"
#include "apgar/benchmark/phase4_representative_corpus.h"

namespace apgar::benchmark {

inline constexpr std::uint32_t kPhase4PairedTrialSchemaVersion = 1;
inline constexpr std::uint32_t kPhase4ExternalAuthoritySchemaVersion = 1;

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
  std::uint64_t configured_memory_limit_bytes = 0;
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

// Converts an independently measured arm execution into a decision-eligible
// record only when isolated wall and memory authorities were both enforced.
[[nodiscard]] Phase4TrialArmRecordResult FinalizePhase4TrialArmV1(
    Phase4TrialArmExecution execution, const Phase4ExternalResourceObservation& observation);

// Requires one baseline and one candidate record with identical case, root,
// opportunity, stopping-depth, order, and external-budget identities.
[[nodiscard]] Phase4PairedTrialAssemblyResult AssemblePhase4PairedTrialV1(
    Phase4TrialArmRecord baseline, Phase4TrialArmRecord candidate);

}  // namespace apgar::benchmark

#endif  // APGAR_BENCHMARK_PHASE4_PAIRED_TRIAL_H_
