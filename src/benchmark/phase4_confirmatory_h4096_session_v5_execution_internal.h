#ifndef APGAR_SRC_BENCHMARK_PHASE4_CONFIRMATORY_H4096_SESSION_V5_EXECUTION_INTERNAL_H_
#define APGAR_SRC_BENCHMARK_PHASE4_CONFIRMATORY_H4096_SESSION_V5_EXECUTION_INTERNAL_H_

#include <cstdint>
#include <optional>
#include <string_view>

#include "apgar/benchmark/phase4_trial_harness.h"
#include "src/benchmark/phase4_paired_trial_internal.h"

namespace apgar::benchmark::internal {

inline constexpr std::string_view kPhase4H4096SessionV5ActivationInvariant =
    "P4PAIR-H4096-SESSION-V5-ACTIVATION-001";

enum class Phase4H4096SessionV5Carrier : std::uint8_t {
  kOrdinary = 0,
  kSameRun = 1,
};

enum class Phase4H4096SessionV5Endpoint : std::uint8_t {
  kController = 0,
  kWorker = 1,
};

enum class Phase4H4096SessionV5CaseRole : std::uint8_t {
  kExact = 0,
  kCalibration = 1,
};

// Complete, immutable authority assertion reconstructed at each boundary. It
// is evidence to validate, never a selector: the separately named compiled
// entry point and execution_authority field must both name Session v5.
struct Phase4H4096SessionV5PreflightIdentity {
  Phase4TrialExecutionAuthority execution_authority =
      Phase4TrialExecutionAuthority::kCorpusV2H4096SessionV5;
  Phase4H4096SessionV5Carrier carrier = Phase4H4096SessionV5Carrier::kOrdinary;
  Phase4H4096SessionV5Endpoint endpoint = Phase4H4096SessionV5Endpoint::kController;
  Phase4H4096SessionV5CaseRole case_role = Phase4H4096SessionV5CaseRole::kCalibration;

  std::uint32_t protocol_schema_version = 0;
  std::uint64_t protocol_checksum = 0;
  std::uint32_t superseded_protocol_schema_version = 0;
  std::uint64_t superseded_protocol_checksum = 0;

  std::string_view budget_roster_authority;
  std::uint32_t budget_roster_schema_version = 0;
  std::uint64_t budget_roster_checksum = 0;
  std::string_view superseded_budget_roster_authority;
  std::uint32_t superseded_budget_roster_schema_version = 0;
  std::uint64_t superseded_budget_roster_checksum = 0;

  std::uint32_t corpus_version = 0;
  std::uint64_t corpus_checksum = 0;
  std::uint32_t representative_manifest_schema_version = 0;
  std::uint64_t representative_manifest_checksum = 0;
  std::uint32_t workload_roster_manifest_schema_version = 0;
  std::uint64_t workload_roster_manifest_checksum = 0;
  std::string_view configuration_authority;
  std::string_view superseded_configuration_authority;

  std::uint32_t candidate_session_schema_version = 0;
  std::uint32_t targeted_regeneration_plan_schema_version = 0;
  std::uint32_t targeted_regeneration_execution_schema_version = 0;
  std::uint64_t present_step_per_overuse_unit = 0;
  std::uint64_t history_step_per_overuse_unit = 0;

  std::string_view raw_authority;
  std::string_view superseded_raw_authority;
  std::uint32_t raw_schema_version = 0;
  std::uint32_t raw_wire_schema_version = 0;
  bool telemetry_required = false;
  std::string_view telemetry_authority;
  std::string_view superseded_telemetry_authority;
  std::uint32_t telemetry_schema_version = 0;
  std::uint32_t telemetry_wire_schema_version = 0;

  std::uint64_t canonical_algorithm_budget_checksum = 0;
  std::uint64_t paired_semantic_budget_checksum = 0;

  friend bool operator==(const Phase4H4096SessionV5PreflightIdentity&,
                         const Phase4H4096SessionV5PreflightIdentity&) = default;
};

struct Phase4H4096SessionV5ControllerSourceAssociation {
  std::string_view embedded_commit;
  std::string_view runtime_commit;
  bool source_stamped = false;
  bool source_tree_dirty = true;
};

struct Phase4H4096SessionV5WorkerSourceAssociation {
  std::string_view embedded_commit;
  bool source_stamped = false;
  bool source_tree_dirty = true;
};

[[nodiscard]] Phase4H4096SessionV5PreflightIdentity
BuildPhase4ConfirmatoryH4096SessionV5PreflightIdentity(
    Phase4H4096SessionV5Carrier carrier, Phase4H4096SessionV5Endpoint endpoint) noexcept;

[[nodiscard]] Phase4CanonicalCellConfig BuildPhase4ConfirmatoryH4096SessionV5CanonicalCell(
    Phase4H4096SessionV5Carrier carrier) noexcept;

// Pure specification preflight used by the controller and hidden workers.
// A valid specification returns no error; only the complete controller/worker
// preflights may proceed to the immutable activation barrier.
[[nodiscard]] std::optional<Phase4PairedTrialError> PreflightPhase4ConfirmatoryH4096SessionV5Spec(
    const Phase4H4096SessionV5PreflightIdentity& identity, const Phase4PairedTrialSpec& spec,
    std::uint32_t expected_repetition, Phase4TrialArm arm);

[[nodiscard]] std::optional<Phase4PairedTrialError>
PreflightPhase4ConfirmatoryH4096SessionV5Controller(
    const Phase4H4096SessionV5PreflightIdentity& identity, const Phase4CanonicalCellConfig& cell,
    const Phase4H4096SessionV5ControllerSourceAssociation& source);

[[nodiscard]] std::optional<Phase4PairedTrialError> PreflightPhase4ConfirmatoryH4096SessionV5Worker(
    const Phase4H4096SessionV5PreflightIdentity& identity, const Phase4CanonicalCellConfig& cell,
    Phase4TrialArm arm, const Phase4H4096SessionV5WorkerSourceAssociation& source);

}  // namespace apgar::benchmark::internal

#endif  // APGAR_SRC_BENCHMARK_PHASE4_CONFIRMATORY_H4096_SESSION_V5_EXECUTION_INTERNAL_H_
