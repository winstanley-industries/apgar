#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "src/benchmark/phase4_confirmatory_h4096_session_v5_per_net_report_producer_preflight_internal.h"
#include "tests/support/google_test.h"

namespace apgar::benchmark {
namespace {

using Identity = internal::Phase4H4096SessionV5OrdinaryPerNetReportProducerIdentity;
using Source = internal::Phase4H4096SessionV5ControllerSourceAssociation;

constexpr std::string_view kCleanCommit = "0123456789abcdef0123456789abcdef01234567";

[[nodiscard]] Source CleanSource() {
  return Source{
      .embedded_commit = kCleanCommit,
      .runtime_commit = kCleanCommit,
      .source_stamped = true,
      .source_tree_dirty = false,
  };
}

[[nodiscard]] Source UnpublishableSource() {
  return Source{
      .embedded_commit = "0123456789ABCDEF0123456789ABCDEF01234567",
      .runtime_commit = "1123456789abcdef0123456789abcdef01234567",
      .source_stamped = false,
      .source_tree_dirty = true,
  };
}

void ExpectInvariant(const std::optional<Phase4PairedTrialError>& error,
                     Phase4PairedTrialErrorCode code, std::string_view invariant_id) {
  ASSERT_TRUE(error.has_value());
  EXPECT_EQ(error->code, code);
  EXPECT_EQ(error->invariant_id, invariant_id);
}

void ExpectProducerIdentityError(const Identity& identity,
                                 const Source& source = UnpublishableSource()) {
  ExpectInvariant(internal::PreflightPhase4ConfirmatoryH4096SessionV5OrdinaryPerNetReportProducer(
                      identity, source),
                  Phase4PairedTrialErrorCode::kMeasurementAssociation,
                  internal::kPhase4H4096SessionV5OrdinaryPerNetReportProducerInvariant);
}

TEST(Phase4H4096SessionV5OrdinaryPerNetReportProducerPreflightTest,
     CanonicalBuilderBindsEveryFrozenIdentityFieldExactly) {
  const Identity identity =
      internal::BuildPhase4ConfirmatoryH4096SessionV5OrdinaryPerNetReportProducerIdentity();

  EXPECT_EQ(identity.identity_schema_version, 1U);
  EXPECT_EQ(identity.execution_authority,
            internal::Phase4TrialExecutionAuthority::kCorpusV2H4096SessionV5);
  EXPECT_EQ(identity.configuration_authority, "phase4_confirmatory_corpus_v2_h4096_session_v5");
  EXPECT_EQ(identity.superseded_configuration_authority, "phase4_confirmatory_corpus_v2_h4096");
  EXPECT_EQ(identity.protocol_authority, "phase4_confirmatory_decision_protocol_v3");
  EXPECT_EQ(identity.protocol_checksum, 4'963'299'999'381'388'941ULL);
  EXPECT_EQ(identity.superseded_protocol_authority, "phase4_confirmatory_decision_protocol_v2");
  EXPECT_EQ(identity.superseded_protocol_checksum, 11'520'586'171'987'743'043ULL);
  EXPECT_EQ(identity.budget_roster_authority,
            "phase4_confirmatory_canonical_algorithm_budget_roster_v4");
  EXPECT_EQ(identity.budget_roster_checksum, 12'316'700'735'749'461'907ULL);
  EXPECT_EQ(identity.superseded_budget_roster_authority,
            "phase4_confirmatory_canonical_algorithm_budget_roster_v3");
  EXPECT_EQ(identity.superseded_budget_roster_checksum, 18'429'170'436'700'418'962ULL);
  EXPECT_EQ(identity.corpus_version, 2U);
  EXPECT_EQ(identity.corpus_checksum, 4'182'833'841'936'446'798ULL);
  EXPECT_EQ(identity.representative_manifest_schema_version, 2U);
  EXPECT_EQ(identity.representative_manifest_checksum, 9'613'362'670'139'358'355ULL);
  EXPECT_EQ(identity.workload_roster_manifest_schema_version, 2U);
  EXPECT_EQ(identity.workload_roster_manifest_checksum, 14'986'327'048'461'036'142ULL);
  EXPECT_EQ(identity.raw_authority, "phase4_confirmatory_raw_evidence_v3");
  EXPECT_EQ(identity.superseded_raw_authority, "phase4_confirmatory_raw_evidence_v2");
  EXPECT_EQ(identity.retained_raw_predecessor_authority, "phase4_confirmatory_raw_evidence_v1");
  EXPECT_EQ(identity.report_authority, "phase4_confirmatory_per_net_report_publication_join_v3");
  EXPECT_EQ(identity.superseded_report_authority,
            "phase4_confirmatory_per_net_report_publication_join_v2");
  EXPECT_EQ(identity.retained_report_predecessor_authority,
            "phase4_confirmatory_per_net_report_publication_join_v1");
  EXPECT_EQ(identity.case_id, 10'200U);
  EXPECT_EQ(identity.requested_pool_size, 8U);
  EXPECT_EQ(identity.preparation_worker_count, 4U);
  EXPECT_EQ(identity.repetitions, 20U);
  EXPECT_EQ(identity.carrier, internal::Phase4H4096SessionV5Carrier::kOrdinary);
  EXPECT_EQ(identity.raw_schema_version, 1U);
  EXPECT_EQ(identity.raw_wire_schema_version, 1U);
  EXPECT_EQ(identity.report_schema_version, 1U);
  EXPECT_EQ(identity.report_raw_wire_schema_version, 1U);
  EXPECT_EQ(identity.report_reference_repetition, 0U);
  EXPECT_EQ(identity.report_reference_execution_order, Phase4TrialOrder::kBaselineFirst);
  EXPECT_EQ(identity.report_arm_count, 2U);
  EXPECT_EQ(identity.workload_net_count_per_arm, 64U);
  EXPECT_EQ(identity.total_report_per_net_row_count, 128U);
  EXPECT_EQ(identity.workload_net_roster_checksum, 718'781'758'134'362'332ULL);
  EXPECT_FALSE(identity.report_decision_eligible);
  EXPECT_EQ(identity.candidate_session_schema_version, 5U);
  EXPECT_EQ(identity.targeted_regeneration_plan_schema_version, 3U);
  EXPECT_EQ(identity.targeted_regeneration_execution_schema_version, 6U);
  EXPECT_EQ(identity.baseline_present_step_per_overuse_unit, 1U);
  EXPECT_EQ(identity.baseline_history_step_per_overuse_unit, 4'096U);
  EXPECT_EQ(identity.candidate_present_step_per_overuse_unit, 1U);
  EXPECT_EQ(identity.candidate_history_step_per_overuse_unit, 4'096U);
  EXPECT_EQ(identity.canonical_algorithm_budget_checksum, 7'657'176'792'159'702'821ULL);
  EXPECT_EQ(identity.paired_semantic_budget_checksum, 13'340'538'727'848'385'478ULL);
  EXPECT_FALSE(identity.telemetry_required);
  EXPECT_TRUE(identity.telemetry_authority.empty());
  EXPECT_TRUE(identity.superseded_telemetry_authority.empty());
  EXPECT_EQ(identity.telemetry_schema_version, 0U);
  EXPECT_EQ(identity.telemetry_wire_schema_version, 0U);
}

struct IdentityMutation {
  std::string_view name;
  void (*apply)(Identity*);
};

constexpr std::array<IdentityMutation, 54> kIdentityMutations = {{
    {"identity-schema-version", +[](Identity* value) { ++value->identity_schema_version; }},
    {"execution-authority",
     +[](Identity* value) {
       value->execution_authority = internal::Phase4TrialExecutionAuthority::kCorpusV2H4096;
     }},
    {"configuration-authority",
     +[](Identity* value) { value->configuration_authority = "mutated"; }},
    {"superseded-configuration-authority",
     +[](Identity* value) { value->superseded_configuration_authority = "mutated"; }},
    {"protocol-authority", +[](Identity* value) { value->protocol_authority = "mutated"; }},
    {"protocol-checksum", +[](Identity* value) { ++value->protocol_checksum; }},
    {"superseded-protocol-authority",
     +[](Identity* value) { value->superseded_protocol_authority = "mutated"; }},
    {"superseded-protocol-checksum",
     +[](Identity* value) { ++value->superseded_protocol_checksum; }},
    {"budget-roster-authority",
     +[](Identity* value) { value->budget_roster_authority = "mutated"; }},
    {"budget-roster-checksum", +[](Identity* value) { ++value->budget_roster_checksum; }},
    {"superseded-budget-roster-authority",
     +[](Identity* value) { value->superseded_budget_roster_authority = "mutated"; }},
    {"superseded-budget-roster-checksum",
     +[](Identity* value) { ++value->superseded_budget_roster_checksum; }},
    {"corpus-version", +[](Identity* value) { ++value->corpus_version; }},
    {"corpus-checksum", +[](Identity* value) { ++value->corpus_checksum; }},
    {"representative-manifest-schema-version",
     +[](Identity* value) { ++value->representative_manifest_schema_version; }},
    {"representative-manifest-checksum",
     +[](Identity* value) { ++value->representative_manifest_checksum; }},
    {"workload-roster-manifest-schema-version",
     +[](Identity* value) { ++value->workload_roster_manifest_schema_version; }},
    {"workload-roster-manifest-checksum",
     +[](Identity* value) { ++value->workload_roster_manifest_checksum; }},
    {"raw-authority", +[](Identity* value) { value->raw_authority = "mutated"; }},
    {"superseded-raw-authority",
     +[](Identity* value) { value->superseded_raw_authority = "mutated"; }},
    {"retained-raw-predecessor-authority",
     +[](Identity* value) { value->retained_raw_predecessor_authority = "mutated"; }},
    {"report-authority", +[](Identity* value) { value->report_authority = "mutated"; }},
    {"superseded-report-authority",
     +[](Identity* value) { value->superseded_report_authority = "mutated"; }},
    {"retained-report-predecessor-authority",
     +[](Identity* value) { value->retained_report_predecessor_authority = "mutated"; }},
    {"case-id", +[](Identity* value) { ++value->case_id; }},
    {"requested-pool-size", +[](Identity* value) { ++value->requested_pool_size; }},
    {"preparation-worker-count", +[](Identity* value) { ++value->preparation_worker_count; }},
    {"repetitions", +[](Identity* value) { ++value->repetitions; }},
    {"carrier",
     +[](Identity* value) { value->carrier = internal::Phase4H4096SessionV5Carrier::kSameRun; }},
    {"raw-schema-version", +[](Identity* value) { ++value->raw_schema_version; }},
    {"raw-wire-schema-version", +[](Identity* value) { ++value->raw_wire_schema_version; }},
    {"report-schema-version", +[](Identity* value) { ++value->report_schema_version; }},
    {"report-raw-wire-schema-version",
     +[](Identity* value) { ++value->report_raw_wire_schema_version; }},
    {"report-reference-repetition", +[](Identity* value) { ++value->report_reference_repetition; }},
    {"report-reference-execution-order",
     +[](Identity* value) {
       value->report_reference_execution_order = Phase4TrialOrder::kCandidateFirst;
     }},
    {"report-arm-count", +[](Identity* value) { ++value->report_arm_count; }},
    {"workload-net-count-per-arm", +[](Identity* value) { ++value->workload_net_count_per_arm; }},
    {"total-report-per-net-row-count",
     +[](Identity* value) { ++value->total_report_per_net_row_count; }},
    {"workload-net-roster-checksum",
     +[](Identity* value) { ++value->workload_net_roster_checksum; }},
    {"report-decision-eligible",
     +[](Identity* value) { value->report_decision_eligible = !value->report_decision_eligible; }},
    {"candidate-session-schema-version",
     +[](Identity* value) { ++value->candidate_session_schema_version; }},
    {"targeted-regeneration-plan-schema-version",
     +[](Identity* value) { ++value->targeted_regeneration_plan_schema_version; }},
    {"targeted-regeneration-execution-schema-version",
     +[](Identity* value) { ++value->targeted_regeneration_execution_schema_version; }},
    {"baseline-present-step-per-overuse-unit",
     +[](Identity* value) { ++value->baseline_present_step_per_overuse_unit; }},
    {"baseline-history-step-per-overuse-unit",
     +[](Identity* value) { ++value->baseline_history_step_per_overuse_unit; }},
    {"candidate-present-step-per-overuse-unit",
     +[](Identity* value) { ++value->candidate_present_step_per_overuse_unit; }},
    {"candidate-history-step-per-overuse-unit",
     +[](Identity* value) { ++value->candidate_history_step_per_overuse_unit; }},
    {"canonical-algorithm-budget-checksum",
     +[](Identity* value) { ++value->canonical_algorithm_budget_checksum; }},
    {"paired-semantic-budget-checksum",
     +[](Identity* value) { ++value->paired_semantic_budget_checksum; }},
    {"telemetry-required",
     +[](Identity* value) { value->telemetry_required = !value->telemetry_required; }},
    {"telemetry-authority", +[](Identity* value) { value->telemetry_authority = "mutated"; }},
    {"superseded-telemetry-authority",
     +[](Identity* value) { value->superseded_telemetry_authority = "mutated"; }},
    {"telemetry-schema-version", +[](Identity* value) { ++value->telemetry_schema_version; }},
    {"telemetry-wire-schema-version",
     +[](Identity* value) { ++value->telemetry_wire_schema_version; }},
}};

static_assert(kIdentityMutations.size() == 54);

TEST(Phase4H4096SessionV5OrdinaryPerNetReportProducerPreflightTest,
     EveryIdentityFieldIndependentlyRejectsBeforeSourceValidationOrActivation) {
  for (const IdentityMutation& mutation : kIdentityMutations) {
    SCOPED_TRACE(std::string(mutation.name));
    Identity identity =
        internal::BuildPhase4ConfirmatoryH4096SessionV5OrdinaryPerNetReportProducerIdentity();
    mutation.apply(&identity);
    ExpectProducerIdentityError(identity);
  }
}

struct ProfileMutation {
  std::string_view name;
  void (*apply)(Identity*);
};

constexpr std::array<ProfileMutation, 7> kForeignProfiles = {{
    {"same-run-complete-artifact-and-budget-shape",
     +[](Identity* value) {
       value->carrier = internal::Phase4H4096SessionV5Carrier::kSameRun;
       value->case_id = 10'100;
       value->requested_pool_size = 4;
       value->raw_authority = "phase4_confirmatory_same_run_raw_evidence_v3";
       value->superseded_raw_authority = "phase4_confirmatory_same_run_raw_evidence_v2";
       value->retained_raw_predecessor_authority = "phase4_confirmatory_same_run_raw_evidence_v1";
       value->report_authority = "phase4_confirmatory_same_run_per_net_report_publication_join_v3";
       value->superseded_report_authority =
           "phase4_confirmatory_same_run_per_net_report_publication_join_v2";
       value->retained_report_predecessor_authority =
           "phase4_confirmatory_same_run_per_net_report_publication_join_v1";
       value->raw_schema_version = 2;
       value->raw_wire_schema_version = 2;
       value->report_raw_wire_schema_version = 2;
       value->workload_net_count_per_arm = 6;
       value->total_report_per_net_row_count = 12;
       value->workload_net_roster_checksum = 12'521'697'377'381'992'336ULL;
       value->canonical_algorithm_budget_checksum = 13'645'569'624'513'409'309ULL;
       value->paired_semantic_budget_checksum = 12'493'092'620'111'240'227ULL;
       value->telemetry_required = true;
       value->telemetry_authority = "phase4_confirmatory_same_run_decision_telemetry_v3";
       value->superseded_telemetry_authority = "phase4_confirmatory_same_run_decision_telemetry_v2";
       value->telemetry_schema_version = 1;
       value->telemetry_wire_schema_version = 2;
     }},
    {"session-v4-protocol-v2-roster-v3-predecessor",
     +[](Identity* value) {
       value->configuration_authority = "phase4_confirmatory_corpus_v2_h4096";
       value->protocol_authority = "phase4_confirmatory_decision_protocol_v2";
       value->protocol_checksum = 11'520'586'171'987'743'043ULL;
       value->budget_roster_authority = "phase4_confirmatory_canonical_algorithm_budget_roster_v3";
       value->budget_roster_checksum = 18'429'170'436'700'418'962ULL;
       value->raw_authority = "phase4_confirmatory_raw_evidence_v2";
       value->report_authority = "phase4_confirmatory_per_net_report_publication_join_v2";
       value->candidate_session_schema_version = 4;
       value->targeted_regeneration_plan_schema_version = 2;
       value->targeted_regeneration_execution_schema_version = 5;
       value->canonical_algorithm_budget_checksum = 8'230'401'457'668'518'004ULL;
       value->paired_semantic_budget_checksum = 12'108'149'041'077'564'710ULL;
     }},
    {"v1-raw-and-report-authorities",
     +[](Identity* value) {
       value->raw_authority = "phase4_confirmatory_raw_evidence_v1";
       value->report_authority = "phase4_confirmatory_per_net_report_publication_join_v1";
     }},
    {"predecessor-canonical-budget",
     +[](Identity* value) {
       value->canonical_algorithm_budget_checksum = 8'230'401'457'668'518'004ULL;
     }},
    {"predecessor-paired-budget",
     +[](Identity* value) {
       value->paired_semantic_budget_checksum = 12'108'149'041'077'564'710ULL;
     }},
    {"successor-budget-checksums-swapped",
     +[](Identity* value) {
       const std::uint64_t canonical = value->canonical_algorithm_budget_checksum;
       value->canonical_algorithm_budget_checksum = value->paired_semantic_budget_checksum;
       value->paired_semantic_budget_checksum = canonical;
     }},
    {"telemetry-presence-cross-substitution",
     +[](Identity* value) {
       value->telemetry_required = true;
       value->telemetry_authority = "phase4_confirmatory_same_run_decision_telemetry_v3";
       value->superseded_telemetry_authority = "phase4_confirmatory_same_run_decision_telemetry_v2";
       value->telemetry_schema_version = 1;
       value->telemetry_wire_schema_version = 2;
     }},
}};

TEST(Phase4H4096SessionV5OrdinaryPerNetReportProducerPreflightTest,
     OrdinarySameRunPredecessorBudgetAndTelemetryProfilesCrossRejectAtomically) {
  for (const ProfileMutation& mutation : kForeignProfiles) {
    SCOPED_TRACE(std::string(mutation.name));
    Identity identity =
        internal::BuildPhase4ConfirmatoryH4096SessionV5OrdinaryPerNetReportProducerIdentity();
    mutation.apply(&identity);
    ExpectProducerIdentityError(identity, CleanSource());
  }
}

TEST(Phase4H4096SessionV5OrdinaryPerNetReportProducerPreflightTest,
     IdentityThenSourceThenCanonicalActivationOrderIsExact) {
  const Identity identity =
      internal::BuildPhase4ConfirmatoryH4096SessionV5OrdinaryPerNetReportProducerIdentity();

  Identity invalid_identity = identity;
  ++invalid_identity.identity_schema_version;
  ExpectProducerIdentityError(invalid_identity, UnpublishableSource());

  for (const auto mutate : {
           +[](Source* source) { source->source_stamped = false; },
           +[](Source* source) { source->source_tree_dirty = true; },
           +[](Source* source) { source->embedded_commit = ""; },
           +[](Source* source) {
             source->embedded_commit = "0123456789ABCDEF0123456789ABCDEF01234567";
           },
       }) {
    Source source = CleanSource();
    mutate(&source);
    ExpectInvariant(internal::PreflightPhase4ConfirmatoryH4096SessionV5OrdinaryPerNetReportProducer(
                        identity, source),
                    Phase4PairedTrialErrorCode::kMeasurementAssociation,
                    "P4PAIR-H4096-SESSION-V5-SOURCE-001");
  }

  for (const std::string_view runtime_commit : {
           std::string_view{},
           std::string_view{"1123456789abcdef0123456789abcdef01234567"},
           std::string_view{"0123456789ABCDEF0123456789ABCDEF01234567"},
       }) {
    Source source = CleanSource();
    source.runtime_commit = runtime_commit;
    ExpectInvariant(internal::PreflightPhase4ConfirmatoryH4096SessionV5OrdinaryPerNetReportProducer(
                        identity, source),
                    Phase4PairedTrialErrorCode::kMeasurementAssociation,
                    "P4PAIR-H4096-SESSION-V5-SOURCE-002");
  }

  const Source clean_source = CleanSource();
  const auto direct_controller_result =
      internal::PreflightPhase4ConfirmatoryH4096SessionV5Controller(
          internal::BuildPhase4ConfirmatoryH4096SessionV5PreflightIdentity(
              internal::Phase4H4096SessionV5Carrier::kOrdinary,
              internal::Phase4H4096SessionV5Endpoint::kController),
          internal::BuildPhase4ConfirmatoryH4096SessionV5CanonicalCell(
              internal::Phase4H4096SessionV5Carrier::kOrdinary),
          clean_source);
  const auto producer_result =
      internal::PreflightPhase4ConfirmatoryH4096SessionV5OrdinaryPerNetReportProducer(identity,
                                                                                      clean_source);
  EXPECT_EQ(producer_result, direct_controller_result);
  ExpectInvariant(producer_result, Phase4PairedTrialErrorCode::kUnsupportedSchema,
                  internal::kPhase4H4096SessionV5ActivationInvariant);
}

}  // namespace
}  // namespace apgar::benchmark
