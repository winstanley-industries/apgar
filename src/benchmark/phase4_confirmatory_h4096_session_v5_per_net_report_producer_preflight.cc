#include "src/benchmark/phase4_confirmatory_h4096_session_v5_per_net_report_producer_preflight_internal.h"

namespace apgar::benchmark::internal {

Phase4H4096SessionV5OrdinaryPerNetReportProducerIdentity
BuildPhase4ConfirmatoryH4096SessionV5OrdinaryPerNetReportProducerIdentity() noexcept {
  return Phase4H4096SessionV5OrdinaryPerNetReportProducerIdentity{
      .identity_schema_version = 1,
      .execution_authority = Phase4TrialExecutionAuthority::kCorpusV2H4096SessionV5,
      .configuration_authority = "phase4_confirmatory_corpus_v2_h4096_session_v5",
      .superseded_configuration_authority = "phase4_confirmatory_corpus_v2_h4096",
      .protocol_authority = "phase4_confirmatory_decision_protocol_v3",
      .protocol_checksum = 4'963'299'999'381'388'941ULL,
      .superseded_protocol_authority = "phase4_confirmatory_decision_protocol_v2",
      .superseded_protocol_checksum = 11'520'586'171'987'743'043ULL,
      .budget_roster_authority = "phase4_confirmatory_canonical_algorithm_budget_roster_v4",
      .budget_roster_checksum = 12'316'700'735'749'461'907ULL,
      .superseded_budget_roster_authority =
          "phase4_confirmatory_canonical_algorithm_budget_roster_v3",
      .superseded_budget_roster_checksum = 18'429'170'436'700'418'962ULL,
      .corpus_version = 2,
      .corpus_checksum = 4'182'833'841'936'446'798ULL,
      .representative_manifest_schema_version = 2,
      .representative_manifest_checksum = 9'613'362'670'139'358'355ULL,
      .workload_roster_manifest_schema_version = 2,
      .workload_roster_manifest_checksum = 14'986'327'048'461'036'142ULL,
      .raw_authority = "phase4_confirmatory_raw_evidence_v3",
      .superseded_raw_authority = "phase4_confirmatory_raw_evidence_v2",
      .retained_raw_predecessor_authority = "phase4_confirmatory_raw_evidence_v1",
      .report_authority = "phase4_confirmatory_per_net_report_publication_join_v3",
      .superseded_report_authority = "phase4_confirmatory_per_net_report_publication_join_v2",
      .retained_report_predecessor_authority =
          "phase4_confirmatory_per_net_report_publication_join_v1",
      .case_id = 10'200,
      .requested_pool_size = 8,
      .preparation_worker_count = 4,
      .repetitions = 20,
      .carrier = Phase4H4096SessionV5Carrier::kOrdinary,
      .raw_schema_version = 1,
      .raw_wire_schema_version = 1,
      .report_schema_version = 1,
      .report_raw_wire_schema_version = 1,
      .report_reference_repetition = 0,
      .report_reference_execution_order = Phase4TrialOrder::kBaselineFirst,
      .report_arm_count = 2,
      .workload_net_count_per_arm = 64,
      .total_report_per_net_row_count = 128,
      .workload_net_roster_checksum = 718'781'758'134'362'332ULL,
      .report_decision_eligible = false,
      .candidate_session_schema_version = 5,
      .targeted_regeneration_plan_schema_version = 3,
      .targeted_regeneration_execution_schema_version = 6,
      .baseline_present_step_per_overuse_unit = 1,
      .baseline_history_step_per_overuse_unit = 4'096,
      .candidate_present_step_per_overuse_unit = 1,
      .candidate_history_step_per_overuse_unit = 4'096,
      .canonical_algorithm_budget_checksum = 7'657'176'792'159'702'821ULL,
      .paired_semantic_budget_checksum = 13'340'538'727'848'385'478ULL,
      .telemetry_required = false,
      .telemetry_authority = "",
      .superseded_telemetry_authority = "",
      .telemetry_schema_version = 0,
      .telemetry_wire_schema_version = 0,
  };
}

std::optional<Phase4PairedTrialError>
PreflightPhase4ConfirmatoryH4096SessionV5OrdinaryPerNetReportProducer(
    const Phase4H4096SessionV5OrdinaryPerNetReportProducerIdentity& identity,
    const Phase4H4096SessionV5ControllerSourceAssociation& source) {
  if (!(identity == BuildPhase4ConfirmatoryH4096SessionV5OrdinaryPerNetReportProducerIdentity())) {
    return Phase4PairedTrialError{
        .code = Phase4PairedTrialErrorCode::kMeasurementAssociation,
        .invariant_id = kPhase4H4096SessionV5OrdinaryPerNetReportProducerInvariant,
        .detail =
            "the complete ordinary Session-v5 report-producer identity does not match its "
            "compiled authority",
    };
  }
  return PreflightPhase4ConfirmatoryH4096SessionV5Controller(
      BuildPhase4ConfirmatoryH4096SessionV5PreflightIdentity(
          Phase4H4096SessionV5Carrier::kOrdinary, Phase4H4096SessionV5Endpoint::kController),
      BuildPhase4ConfirmatoryH4096SessionV5CanonicalCell(Phase4H4096SessionV5Carrier::kOrdinary),
      source);
}

}  // namespace apgar::benchmark::internal
