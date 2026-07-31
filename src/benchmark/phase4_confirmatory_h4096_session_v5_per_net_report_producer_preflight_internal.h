#ifndef APGAR_SRC_BENCHMARK_PHASE4_CONFIRMATORY_H4096_SESSION_V5_PER_NET_REPORT_PRODUCER_PREFLIGHT_INTERNAL_H_
#define APGAR_SRC_BENCHMARK_PHASE4_CONFIRMATORY_H4096_SESSION_V5_PER_NET_REPORT_PRODUCER_PREFLIGHT_INTERNAL_H_

#include <cstdint>
#include <optional>
#include <string_view>

#include "src/benchmark/phase4_confirmatory_h4096_session_v5_execution_internal.h"

namespace apgar::benchmark::internal {

inline constexpr std::string_view kPhase4H4096SessionV5OrdinaryPerNetReportProducerInvariant =
    "P4PAIR-H4096-SESSION-V5-ORDINARY-REPORT-PRODUCER-001";

// Complete, immutable authority assertion reconstructed by the separately
// named ordinary producer boundary. It is compiled identity, not serialized
// evidence and not a caller-selectable report configuration.
struct Phase4H4096SessionV5OrdinaryPerNetReportProducerIdentity {
  std::uint32_t identity_schema_version = 0;
  Phase4TrialExecutionAuthority execution_authority =
      Phase4TrialExecutionAuthority::kCorpusV2H4096SessionV5;

  std::string_view configuration_authority;
  std::string_view superseded_configuration_authority;
  std::string_view protocol_authority;
  std::uint64_t protocol_checksum = 0;
  std::string_view superseded_protocol_authority;
  std::uint64_t superseded_protocol_checksum = 0;
  std::string_view budget_roster_authority;
  std::uint64_t budget_roster_checksum = 0;
  std::string_view superseded_budget_roster_authority;
  std::uint64_t superseded_budget_roster_checksum = 0;

  std::uint32_t corpus_version = 0;
  std::uint64_t corpus_checksum = 0;
  std::uint32_t representative_manifest_schema_version = 0;
  std::uint64_t representative_manifest_checksum = 0;
  std::uint32_t workload_roster_manifest_schema_version = 0;
  std::uint64_t workload_roster_manifest_checksum = 0;

  std::string_view raw_authority;
  std::string_view superseded_raw_authority;
  std::string_view retained_raw_predecessor_authority;
  std::string_view report_authority;
  std::string_view superseded_report_authority;
  std::string_view retained_report_predecessor_authority;

  std::uint32_t case_id = 0;
  std::uint32_t requested_pool_size = 0;
  std::uint32_t preparation_worker_count = 0;
  std::uint32_t repetitions = 0;
  Phase4H4096SessionV5Carrier carrier = Phase4H4096SessionV5Carrier::kOrdinary;

  std::uint32_t raw_schema_version = 0;
  std::uint32_t raw_wire_schema_version = 0;
  std::uint32_t report_schema_version = 0;
  std::uint32_t report_raw_wire_schema_version = 0;
  std::uint32_t report_reference_repetition = 0;
  Phase4TrialOrder report_reference_execution_order = Phase4TrialOrder::kBaselineFirst;
  std::uint32_t report_arm_count = 0;
  std::uint32_t workload_net_count_per_arm = 0;
  std::uint32_t total_report_per_net_row_count = 0;
  std::uint64_t workload_net_roster_checksum = 0;
  bool report_decision_eligible = false;

  std::uint32_t candidate_session_schema_version = 0;
  std::uint32_t targeted_regeneration_plan_schema_version = 0;
  std::uint32_t targeted_regeneration_execution_schema_version = 0;
  std::uint64_t baseline_present_step_per_overuse_unit = 0;
  std::uint64_t baseline_history_step_per_overuse_unit = 0;
  std::uint64_t candidate_present_step_per_overuse_unit = 0;
  std::uint64_t candidate_history_step_per_overuse_unit = 0;
  std::uint64_t canonical_algorithm_budget_checksum = 0;
  std::uint64_t paired_semantic_budget_checksum = 0;

  bool telemetry_required = false;
  std::string_view telemetry_authority;
  std::string_view superseded_telemetry_authority;
  std::uint32_t telemetry_schema_version = 0;
  std::uint32_t telemetry_wire_schema_version = 0;

  friend bool operator==(const Phase4H4096SessionV5OrdinaryPerNetReportProducerIdentity&,
                         const Phase4H4096SessionV5OrdinaryPerNetReportProducerIdentity&) = default;
};

[[nodiscard]] Phase4H4096SessionV5OrdinaryPerNetReportProducerIdentity
BuildPhase4ConfirmatoryH4096SessionV5OrdinaryPerNetReportProducerIdentity() noexcept;

[[nodiscard]] std::optional<Phase4PairedTrialError>
PreflightPhase4ConfirmatoryH4096SessionV5OrdinaryPerNetReportProducer(
    const Phase4H4096SessionV5OrdinaryPerNetReportProducerIdentity& identity,
    const Phase4H4096SessionV5ControllerSourceAssociation& source);

}  // namespace apgar::benchmark::internal

#endif  // APGAR_SRC_BENCHMARK_PHASE4_CONFIRMATORY_H4096_SESSION_V5_PER_NET_REPORT_PRODUCER_PREFLIGHT_INTERNAL_H_
