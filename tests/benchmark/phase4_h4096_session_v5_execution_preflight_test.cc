#include <array>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include "apgar/allocator/cpu_candidate_allocation_session.h"
#include "apgar/allocator/targeted_regeneration.h"
#include "apgar/allocator/targeted_regeneration_execution.h"
#include "apgar/benchmark/phase4_representative_corpus.h"
#include "src/benchmark/phase4_confirmatory_h4096_execution_internal.h"
#include "src/benchmark/phase4_confirmatory_h4096_session_v5_execution_internal.h"
#include "src/benchmark/phase4_h4096_session_v5_canonical_budget_internal.h"
#include "src/benchmark/phase4_paired_trial_internal.h"
#include "tests/support/google_test.h"

namespace apgar::benchmark {
namespace {

using Carrier = internal::Phase4H4096SessionV5Carrier;
using Endpoint = internal::Phase4H4096SessionV5Endpoint;

constexpr std::string_view kActivationInvariant = "P4PAIR-H4096-SESSION-V5-ACTIVATION-001";
constexpr std::string_view kCleanCommit = "0123456789abcdef0123456789abcdef01234567";

constexpr std::array kCarriers = {Carrier::kOrdinary, Carrier::kSameRun};
constexpr std::array kArms = {Phase4TrialArm::kSequentialBaseline,
                              Phase4TrialArm::kReusableCandidateAllocation};

[[nodiscard]] std::string CarrierName(Carrier carrier) {
  return carrier == Carrier::kOrdinary ? "ordinary" : "same-run";
}

[[nodiscard]] Endpoint WorkerEndpoint(Phase4TrialArm arm) {
  static_cast<void>(arm);
  return Endpoint::kWorker;
}

[[nodiscard]] Phase4PairedTrialSpec Built(Phase4CanonicalSpecResult result) {
  EXPECT_TRUE(std::holds_alternative<Phase4PairedTrialSpec>(result))
      << (std::holds_alternative<Phase4TrialHarnessError>(result)
              ? std::get<Phase4TrialHarnessError>(result).detail
              : "");
  if (!std::holds_alternative<Phase4PairedTrialSpec>(result)) {
    std::abort();
  }
  return std::get<Phase4PairedTrialSpec>(std::move(result));
}

[[nodiscard]] Phase4PairedTrialSpec CanonicalSpec(
    Carrier carrier, std::uint32_t repetition = 0,
    Phase4TrialOrder order = Phase4TrialOrder::kBaselineFirst) {
  return Built(internal::BuildPhase4CanonicalTrialSpecForCorpusV2H4096SessionV5(
      internal::BuildPhase4ConfirmatoryH4096SessionV5CanonicalCell(carrier), repetition, order));
}

[[nodiscard]] internal::Phase4H4096SessionV5ControllerSourceAssociation CleanControllerSource() {
  return internal::Phase4H4096SessionV5ControllerSourceAssociation{
      .embedded_commit = kCleanCommit,
      .runtime_commit = kCleanCommit,
      .source_stamped = true,
      .source_tree_dirty = false,
  };
}

[[nodiscard]] internal::Phase4H4096SessionV5WorkerSourceAssociation CleanWorkerSource() {
  return internal::Phase4H4096SessionV5WorkerSourceAssociation{
      .embedded_commit = kCleanCommit,
      .source_stamped = true,
      .source_tree_dirty = false,
  };
}

void ExpectActivation(const std::optional<Phase4PairedTrialError>& error) {
  ASSERT_TRUE(error.has_value());
  EXPECT_EQ(error->code, Phase4PairedTrialErrorCode::kUnsupportedSchema);
  EXPECT_EQ(error->invariant_id, kActivationInvariant);
}

void ExpectSuccess(const std::optional<Phase4PairedTrialError>& error) {
  EXPECT_FALSE(error.has_value()) << (error.has_value() ? std::string(error->invariant_id)
                                                        : std::string());
}

void ExpectBeforeActivation(const std::optional<Phase4PairedTrialError>& error) {
  ASSERT_TRUE(error.has_value());
  EXPECT_NE(error->invariant_id, kActivationInvariant);
}

template <typename Result>
void ExpectUnsupportedInvariant(const Result& result, std::string_view invariant_id) {
  ASSERT_TRUE(std::holds_alternative<Phase4PairedTrialError>(result));
  const Phase4PairedTrialError& error = std::get<Phase4PairedTrialError>(result);
  EXPECT_EQ(error.code, Phase4PairedTrialErrorCode::kUnsupportedSchema);
  EXPECT_EQ(error.invariant_id, invariant_id);
}

TEST(Phase4H4096SessionV5ExecutionPreflightTest,
     CanonicalControllerAndEveryWorkerReachOnlyTheImmutableActivationBarrier) {
  for (const Carrier carrier : kCarriers) {
    SCOPED_TRACE(CarrierName(carrier));
    const Phase4CanonicalCellConfig cell =
        internal::BuildPhase4ConfirmatoryH4096SessionV5CanonicalCell(carrier);
    const auto controller_source = CleanControllerSource();
    ExpectActivation(internal::PreflightPhase4ConfirmatoryH4096SessionV5Controller(
        internal::BuildPhase4ConfirmatoryH4096SessionV5PreflightIdentity(carrier,
                                                                         Endpoint::kController),
        cell, controller_source));

    for (const Phase4TrialArm arm : kArms) {
      SCOPED_TRACE(static_cast<int>(arm));
      const auto identity = internal::BuildPhase4ConfirmatoryH4096SessionV5PreflightIdentity(
          carrier, WorkerEndpoint(arm));
      for (std::uint32_t repetition = 0; repetition < kPhase4CanonicalRepetitionsV1; ++repetition) {
        const Phase4TrialOrder order = repetition % 2U == 0U ? Phase4TrialOrder::kBaselineFirst
                                                             : Phase4TrialOrder::kCandidateFirst;
        const Phase4PairedTrialSpec spec = CanonicalSpec(carrier, repetition, order);
        ExpectSuccess(internal::PreflightPhase4ConfirmatoryH4096SessionV5Spec(identity, spec,
                                                                              repetition, arm));
      }
      ExpectActivation(internal::PreflightPhase4ConfirmatoryH4096SessionV5Worker(
          identity, cell, arm, CleanWorkerSource()));
    }
  }
}

TEST(Phase4H4096SessionV5ExecutionPreflightTest,
     SelfConsistentUnknownCarrierAndEndpointValuesFailAtTheEnumBoundary) {
  constexpr Carrier kUnknownCarrier = static_cast<Carrier>(255);
  constexpr Endpoint kUnknownEndpoint = static_cast<Endpoint>(255);
  constexpr std::string_view kEnumInvariant = "P4PAIR-H4096-SESSION-V5-ENUM-001";

  const Phase4CanonicalCellConfig unknown_carrier_cell =
      internal::BuildPhase4ConfirmatoryH4096SessionV5CanonicalCell(kUnknownCarrier);
  const auto unknown_controller = internal::BuildPhase4ConfirmatoryH4096SessionV5PreflightIdentity(
      kUnknownCarrier, Endpoint::kController);
  const auto controller_error = internal::PreflightPhase4ConfirmatoryH4096SessionV5Controller(
      unknown_controller, unknown_carrier_cell, CleanControllerSource());
  ASSERT_TRUE(controller_error.has_value());
  EXPECT_EQ(controller_error->invariant_id, kEnumInvariant);

  const auto unknown_worker = internal::BuildPhase4ConfirmatoryH4096SessionV5PreflightIdentity(
      kUnknownCarrier, Endpoint::kWorker);
  for (const Phase4TrialArm arm : kArms) {
    const auto worker_error = internal::PreflightPhase4ConfirmatoryH4096SessionV5Worker(
        unknown_worker, unknown_carrier_cell, arm, CleanWorkerSource());
    ASSERT_TRUE(worker_error.has_value());
    EXPECT_EQ(worker_error->invariant_id, kEnumInvariant);
  }

  for (const Carrier carrier : kCarriers) {
    const auto unknown_endpoint =
        internal::BuildPhase4ConfirmatoryH4096SessionV5PreflightIdentity(carrier, kUnknownEndpoint);
    const Phase4PairedTrialSpec spec = CanonicalSpec(carrier);
    for (const Phase4TrialArm arm : kArms) {
      const auto spec_error =
          internal::PreflightPhase4ConfirmatoryH4096SessionV5Spec(unknown_endpoint, spec, 0, arm);
      ASSERT_TRUE(spec_error.has_value());
      EXPECT_EQ(spec_error->invariant_id, kEnumInvariant);
    }
  }
}

TEST(Phase4H4096SessionV5ExecutionPreflightTest,
     CanonicalAndPairedBudgetConstantsAreIndependentAndMatchBothFixedCells) {
  for (const Carrier carrier : kCarriers) {
    SCOPED_TRACE(CarrierName(carrier));
    const Phase4PairedTrialSpec spec = CanonicalSpec(carrier);
    const std::uint64_t canonical = internal::ComputePhase4CanonicalAlgorithmBudgetChecksumV1(spec);
    const Phase4CaseDescriptor* descriptor = FindPhase4CaseDescriptorForAuthority(
        Phase4RepresentativeCorpusAuthority::kV2, spec.case_id);
    ASSERT_NE(descriptor, nullptr);
    const Phase4RouteOpportunity opportunity{
        .route_queries = spec.baseline_config.limits.maximum_route_queries,
        .route_work_units = spec.baseline_config.limits.maximum_total_route_work_units,
    };
    ASSERT_FALSE(spec.candidate_session_config.schedules.empty());
    const std::uint64_t paired = internal::ComputePhase4PairedBudgetChecksumForAuthorityV1(
        Phase4RepresentativeCorpusAuthority::kV2, spec, opportunity,
        descriptor->requested_net_count,
        spec.candidate_session_config.regeneration_plan_config.maximum_total_columns,
        spec.candidate_session_config.schedules.back().maximum_selection_rounds);

    if (carrier == Carrier::kSameRun) {
      EXPECT_EQ(internal::kPhase4ConfirmatoryH4096SessionV5ExactCanonicalAlgorithmBudgetChecksum,
                13'645'569'624'513'409'309ULL);
      EXPECT_EQ(internal::kPhase4ConfirmatoryH4096SessionV5ExactPairedBudgetChecksum,
                12'493'092'620'111'240'227ULL);
      EXPECT_EQ(canonical,
                internal::kPhase4ConfirmatoryH4096SessionV5ExactCanonicalAlgorithmBudgetChecksum);
      EXPECT_EQ(paired, internal::kPhase4ConfirmatoryH4096SessionV5ExactPairedBudgetChecksum);
    } else {
      EXPECT_EQ(
          internal::kPhase4ConfirmatoryH4096SessionV5CalibrationCanonicalAlgorithmBudgetChecksum,
          7'657'176'792'159'702'821ULL);
      EXPECT_EQ(internal::kPhase4ConfirmatoryH4096SessionV5CalibrationPairedBudgetChecksum,
                13'340'538'727'848'385'478ULL);
      EXPECT_EQ(
          canonical,
          internal::kPhase4ConfirmatoryH4096SessionV5CalibrationCanonicalAlgorithmBudgetChecksum);
      EXPECT_EQ(paired, internal::kPhase4ConfirmatoryH4096SessionV5CalibrationPairedBudgetChecksum);
    }
    EXPECT_NE(canonical, paired);
  }
}

struct CellMutation {
  std::string_view name;
  void (*apply)(Phase4CanonicalCellConfig*);
};

TEST(Phase4H4096SessionV5ExecutionPreflightTest,
     EveryCanonicalCellFieldMutationFailsBeforeSourceAndActivation) {
  constexpr std::array mutations{
      CellMutation{"schema", +[](Phase4CanonicalCellConfig* cell) { ++cell->schema_version; }},
      CellMutation{"case", +[](Phase4CanonicalCellConfig* cell) { ++cell->case_id; }},
      CellMutation{"pool", +[](Phase4CanonicalCellConfig* cell) { ++cell->requested_pool_size; }},
      CellMutation{"workers",
                   +[](Phase4CanonicalCellConfig* cell) { ++cell->preparation_worker_count; }},
      CellMutation{"repetitions", +[](Phase4CanonicalCellConfig* cell) { --cell->repetitions; }},
      CellMutation{
          "setup",
          +[](Phase4CanonicalCellConfig* cell) { ++cell->maximum_setup_elapsed_nanoseconds; }},
      CellMutation{"prepared",
                   +[](Phase4CanonicalCellConfig* cell) {
                     ++cell->external_budget.maximum_prepared_elapsed_nanoseconds;
                   }},
      CellMutation{"cold",
                   +[](Phase4CanonicalCellConfig* cell) {
                     ++cell->external_budget.maximum_cold_elapsed_nanoseconds;
                   }},
      CellMutation{"address-space",
                   +[](Phase4CanonicalCellConfig* cell) {
                     ++cell->external_budget.maximum_address_space_bytes;
                   }},
      CellMutation{"peak-host",
                   +[](Phase4CanonicalCellConfig* cell) {
                     ++cell->external_budget.maximum_peak_host_bytes;
                   }},
      CellMutation{"maximum-nets",
                   +[](Phase4CanonicalCellConfig* cell) { ++cell->corpus_limits.maximum_nets; }},
      CellMutation{
          "compiled-nodes",
          +[](Phase4CanonicalCellConfig* cell) { ++cell->corpus_limits.maximum_compiled_nodes; }},
      CellMutation{"compiled-host",
                   +[](Phase4CanonicalCellConfig* cell) {
                     ++cell->corpus_limits.maximum_compiled_host_bytes;
                   }},
      CellMutation{
          "active-regions",
          +[](Phase4CanonicalCellConfig* cell) { ++cell->corpus_limits.maximum_active_regions; }},
      CellMutation{
          "board-entities",
          +[](Phase4CanonicalCellConfig* cell) { ++cell->corpus_limits.maximum_board_entities; }},
  };

  for (const Carrier carrier : kCarriers) {
    for (const CellMutation& mutation : mutations) {
      SCOPED_TRACE(CarrierName(carrier) + "/" + std::string(mutation.name));
      Phase4CanonicalCellConfig cell =
          internal::BuildPhase4ConfirmatoryH4096SessionV5CanonicalCell(carrier);
      mutation.apply(&cell);
      const auto identity = internal::BuildPhase4ConfirmatoryH4096SessionV5PreflightIdentity(
          carrier, Endpoint::kController);
      ExpectBeforeActivation(internal::PreflightPhase4ConfirmatoryH4096SessionV5Controller(
          identity, cell, CleanControllerSource()));

      auto bad_source = CleanControllerSource();
      bad_source.source_stamped = false;
      const auto ordered =
          internal::PreflightPhase4ConfirmatoryH4096SessionV5Controller(identity, cell, bad_source);
      ExpectBeforeActivation(ordered);
      ASSERT_TRUE(ordered.has_value());
      EXPECT_NE(ordered->invariant_id, "P4PAIR-H4096-SESSION-V5-SOURCE-001");
    }
  }
}

struct SpecMutation {
  std::string_view name;
  void (*apply)(Phase4PairedTrialSpec*);
};

TEST(Phase4H4096SessionV5ExecutionPreflightTest,
     CompleteCanonicalSpecPerturbationsFailBeforeActivation) {
  constexpr std::array mutations{
      SpecMutation{"schema", +[](Phase4PairedTrialSpec* spec) { ++spec->schema_version; }},
      SpecMutation{"case", +[](Phase4PairedTrialSpec* spec) { ++spec->case_id; }},
      SpecMutation{"pool", +[](Phase4PairedTrialSpec* spec) { ++spec->requested_pool_size; }},
      SpecMutation{"repetition", +[](Phase4PairedTrialSpec* spec) { ++spec->repetition_index; }},
      SpecMutation{"root-seed", +[](Phase4PairedTrialSpec* spec) { ++spec->root_seed; }},
      SpecMutation{"order",
                   +[](Phase4PairedTrialSpec* spec) {
                     spec->execution_order = static_cast<Phase4TrialOrder>(255);
                   }},
      SpecMutation{"workers",
                   +[](Phase4PairedTrialSpec* spec) { ++spec->preparation_worker_count; }},
      SpecMutation{"corpus-maximum-nets",
                   +[](Phase4PairedTrialSpec* spec) { ++spec->corpus_limits.maximum_nets; }},
      SpecMutation{
          "corpus-compiled-nodes",
          +[](Phase4PairedTrialSpec* spec) { ++spec->corpus_limits.maximum_compiled_nodes; }},
      SpecMutation{
          "corpus-compiled-host",
          +[](Phase4PairedTrialSpec* spec) { ++spec->corpus_limits.maximum_compiled_host_bytes; }},
      SpecMutation{
          "corpus-active-regions",
          +[](Phase4PairedTrialSpec* spec) { ++spec->corpus_limits.maximum_active_regions; }},
      SpecMutation{
          "corpus-board-entities",
          +[](Phase4PairedTrialSpec* spec) { ++spec->corpus_limits.maximum_board_entities; }},
      SpecMutation{"external-prepared",
                   +[](Phase4PairedTrialSpec* spec) {
                     ++spec->external_budget.maximum_prepared_elapsed_nanoseconds;
                   }},
      SpecMutation{"external-cold",
                   +[](Phase4PairedTrialSpec* spec) {
                     ++spec->external_budget.maximum_cold_elapsed_nanoseconds;
                   }},
      SpecMutation{"external-address-space",
                   +[](Phase4PairedTrialSpec* spec) {
                     ++spec->external_budget.maximum_address_space_bytes;
                   }},
      SpecMutation{
          "external-peak-host",
          +[](Phase4PairedTrialSpec* spec) { ++spec->external_budget.maximum_peak_host_bytes; }},
      SpecMutation{"baseline-schema",
                   +[](Phase4PairedTrialSpec* spec) { ++spec->baseline_config.schema_version; }},
      SpecMutation{
          "baseline-seed",
          +[](Phase4PairedTrialSpec* spec) { ++spec->baseline_config.deterministic_seed; }},
      SpecMutation{
          "baseline-weight",
          +[](Phase4PairedTrialSpec* spec) { ++spec->baseline_config.intrinsic_cost_weight; }},
      SpecMutation{"baseline-sweeps",
                   +[](Phase4PairedTrialSpec* spec) { ++spec->baseline_config.maximum_sweeps; }},
      SpecMutation{"baseline-route",
                   +[](Phase4PairedTrialSpec* spec) {
                     ++spec->baseline_config.route_limits.maximum_work_units;
                   }},
      SpecMutation{"baseline-price",
                   +[](Phase4PairedTrialSpec* spec) {
                     ++spec->baseline_config.price_config.present_step_per_overuse_unit;
                   }},
      SpecMutation{"baseline-store",
                   +[](Phase4PairedTrialSpec* spec) {
                     ++spec->baseline_config.admission_store_config.maximum_candidate_bytes_per_net;
                   }},
      SpecMutation{"baseline-allocator",
                   +[](Phase4PairedTrialSpec* spec) {
                     ++spec->baseline_config.allocator_limits.maximum_candidates;
                   }},
      SpecMutation{"baseline-limits",
                   +[](Phase4PairedTrialSpec* spec) {
                     ++spec->baseline_config.limits.maximum_route_queries;
                   }},
      SpecMutation{"baseline-conflicts",
                   +[](Phase4PairedTrialSpec* spec) {
                     ++spec->baseline_config.known_unmapped_exact_conflict_count;
                   }},
      SpecMutation{"preparation-schema",
                   +[](Phase4PairedTrialSpec* spec) { ++spec->preparation_config.schema_version; }},
      SpecMutation{"preparation-pool",
                   +[](Phase4PairedTrialSpec* spec) {
                     ++spec->preparation_config.requested_candidates_per_net;
                   }},
      SpecMutation{
          "preparation-seed",
          +[](Phase4PairedTrialSpec* spec) { ++spec->preparation_config.deterministic_seed; }},
      SpecMutation{"preparation-policy",
                   +[](Phase4PairedTrialSpec* spec) {
                     ++spec->preparation_config.step_surcharge_increment;
                   }},
      SpecMutation{"preparation-route",
                   +[](Phase4PairedTrialSpec* spec) {
                     ++spec->preparation_config.route_limits.maximum_reconstruction_states;
                   }},
      SpecMutation{"preparation-limits",
                   +[](Phase4PairedTrialSpec* spec) {
                     ++spec->preparation_config.limits.maximum_generated_candidate_bytes;
                   }},
      SpecMutation{
          "preparation-store",
          +[](Phase4PairedTrialSpec* spec) {
            ++spec->preparation_config.store_config.maximum_expected_candidates_per_invocation;
          }},
      SpecMutation{"session-schema",
                   +[](Phase4PairedTrialSpec* spec) {
                     spec->candidate_session_config.schema_version =
                         allocator::kCpuCandidateAllocationSessionSchemaVersionV4;
                   }},
      SpecMutation{"session-weight",
                   +[](Phase4PairedTrialSpec* spec) {
                     ++spec->candidate_session_config.intrinsic_cost_weight;
                   }},
      SpecMutation{"session-epochs",
                   +[](Phase4PairedTrialSpec* spec) {
                     ++spec->candidate_session_config.maximum_regeneration_epochs;
                   }},
      SpecMutation{"session-price",
                   +[](Phase4PairedTrialSpec* spec) {
                     ++spec->candidate_session_config.price_config.history_step_per_overuse_unit;
                   }},
      SpecMutation{"session-allocator",
                   +[](Phase4PairedTrialSpec* spec) {
                     ++spec->candidate_session_config.allocator_limits.maximum_resource_records;
                   }},
      SpecMutation{"plan-config",
                   +[](Phase4PairedTrialSpec* spec) {
                     ++spec->candidate_session_config.regeneration_plan_config
                           .maximum_total_resource_actions;
                   }},
      SpecMutation{"execution-config",
                   +[](Phase4PairedTrialSpec* spec) {
                     ++spec->candidate_session_config.regeneration_execution_config
                           .maximum_policy_resource_entries;
                   }},
      SpecMutation{"schedules",
                   +[](Phase4PairedTrialSpec* spec) {
                     ++spec->candidate_session_config.schedules.front().schedule_key;
                   }},
      SpecMutation{
          "multi-world",
          +[](Phase4PairedTrialSpec* spec) {
            ++spec->candidate_session_config.multi_world_config.maximum_total_selection_rounds;
          }},
      SpecMutation{"session-limits",
                   +[](Phase4PairedTrialSpec* spec) {
                     ++spec->candidate_session_config.limits.maximum_total_route_work_units;
                   }},
      SpecMutation{"session-conflicts",
                   +[](Phase4PairedTrialSpec* spec) {
                     ++spec->candidate_session_config.known_unmapped_exact_conflict_count;
                   }},
  };

  for (const Carrier carrier : kCarriers) {
    for (const SpecMutation& mutation : mutations) {
      SCOPED_TRACE(CarrierName(carrier) + "/" + std::string(mutation.name));
      Phase4PairedTrialSpec spec = CanonicalSpec(carrier, 7, Phase4TrialOrder::kCandidateFirst);
      mutation.apply(&spec);
      const auto identity = internal::BuildPhase4ConfirmatoryH4096SessionV5PreflightIdentity(
          carrier, Endpoint::kWorker);
      for (const Phase4TrialArm arm : kArms) {
        ExpectBeforeActivation(
            internal::PreflightPhase4ConfirmatoryH4096SessionV5Spec(identity, spec, 7, arm));
      }
    }
  }

  for (const Carrier carrier : kCarriers) {
    const Phase4PairedTrialSpec spec = CanonicalSpec(carrier);
    const auto identity = internal::BuildPhase4ConfirmatoryH4096SessionV5PreflightIdentity(
        carrier, Endpoint::kWorker);
    ExpectBeforeActivation(internal::PreflightPhase4ConfirmatoryH4096SessionV5Spec(
        identity, spec, 0, static_cast<Phase4TrialArm>(255)));
  }
}

struct IdentityMutation {
  std::string_view name;
  void (*apply)(internal::Phase4H4096SessionV5PreflightIdentity*);
};

TEST(Phase4H4096SessionV5ExecutionPreflightTest,
     EveryExecutionIdentityAuthorityAndArtifactProfileFieldIsRequired) {
  constexpr std::array mutations{
      IdentityMutation{"execution-authority",
                       +[](internal::Phase4H4096SessionV5PreflightIdentity* identity) {
                         identity->execution_authority =
                             internal::Phase4TrialExecutionAuthority::kCorpusV2H4096;
                       }},
      IdentityMutation{"carrier",
                       +[](internal::Phase4H4096SessionV5PreflightIdentity* identity) {
                         identity->carrier = identity->carrier == Carrier::kOrdinary
                                                 ? Carrier::kSameRun
                                                 : Carrier::kOrdinary;
                       }},
      IdentityMutation{"endpoint",
                       +[](internal::Phase4H4096SessionV5PreflightIdentity* identity) {
                         identity->endpoint = Endpoint::kWorker;
                       }},
      IdentityMutation{"case-role",
                       +[](internal::Phase4H4096SessionV5PreflightIdentity* identity) {
                         identity->case_role =
                             identity->case_role == internal::Phase4H4096SessionV5CaseRole::kExact
                                 ? internal::Phase4H4096SessionV5CaseRole::kCalibration
                                 : internal::Phase4H4096SessionV5CaseRole::kExact;
                       }},
      IdentityMutation{"protocol-schema",
                       +[](internal::Phase4H4096SessionV5PreflightIdentity* identity) {
                         ++identity->protocol_schema_version;
                       }},
      IdentityMutation{"protocol-checksum",
                       +[](internal::Phase4H4096SessionV5PreflightIdentity* identity) {
                         ++identity->protocol_checksum;
                       }},
      IdentityMutation{"superseded-protocol-schema",
                       +[](internal::Phase4H4096SessionV5PreflightIdentity* identity) {
                         ++identity->superseded_protocol_schema_version;
                       }},
      IdentityMutation{"superseded-protocol-checksum",
                       +[](internal::Phase4H4096SessionV5PreflightIdentity* identity) {
                         ++identity->superseded_protocol_checksum;
                       }},
      IdentityMutation{"budget-roster-authority",
                       +[](internal::Phase4H4096SessionV5PreflightIdentity* identity) {
                         identity->budget_roster_authority = "mutated";
                       }},
      IdentityMutation{"budget-roster-schema",
                       +[](internal::Phase4H4096SessionV5PreflightIdentity* identity) {
                         ++identity->budget_roster_schema_version;
                       }},
      IdentityMutation{"budget-roster-checksum",
                       +[](internal::Phase4H4096SessionV5PreflightIdentity* identity) {
                         ++identity->budget_roster_checksum;
                       }},
      IdentityMutation{"superseded-budget-roster-authority",
                       +[](internal::Phase4H4096SessionV5PreflightIdentity* identity) {
                         identity->superseded_budget_roster_authority = "mutated";
                       }},
      IdentityMutation{"superseded-budget-roster-schema",
                       +[](internal::Phase4H4096SessionV5PreflightIdentity* identity) {
                         ++identity->superseded_budget_roster_schema_version;
                       }},
      IdentityMutation{"superseded-budget-roster-checksum",
                       +[](internal::Phase4H4096SessionV5PreflightIdentity* identity) {
                         ++identity->superseded_budget_roster_checksum;
                       }},
      IdentityMutation{"corpus-version",
                       +[](internal::Phase4H4096SessionV5PreflightIdentity* identity) {
                         ++identity->corpus_version;
                       }},
      IdentityMutation{"corpus-checksum",
                       +[](internal::Phase4H4096SessionV5PreflightIdentity* identity) {
                         ++identity->corpus_checksum;
                       }},
      IdentityMutation{"representative-manifest-schema",
                       +[](internal::Phase4H4096SessionV5PreflightIdentity* identity) {
                         ++identity->representative_manifest_schema_version;
                       }},
      IdentityMutation{"representative-manifest-checksum",
                       +[](internal::Phase4H4096SessionV5PreflightIdentity* identity) {
                         ++identity->representative_manifest_checksum;
                       }},
      IdentityMutation{"workload-roster-schema",
                       +[](internal::Phase4H4096SessionV5PreflightIdentity* identity) {
                         ++identity->workload_roster_manifest_schema_version;
                       }},
      IdentityMutation{"workload-roster-checksum",
                       +[](internal::Phase4H4096SessionV5PreflightIdentity* identity) {
                         ++identity->workload_roster_manifest_checksum;
                       }},
      IdentityMutation{"configuration-authority",
                       +[](internal::Phase4H4096SessionV5PreflightIdentity* identity) {
                         identity->configuration_authority = "mutated";
                       }},
      IdentityMutation{"superseded-configuration-authority",
                       +[](internal::Phase4H4096SessionV5PreflightIdentity* identity) {
                         identity->superseded_configuration_authority = "mutated";
                       }},
      IdentityMutation{"session-schema",
                       +[](internal::Phase4H4096SessionV5PreflightIdentity* identity) {
                         ++identity->candidate_session_schema_version;
                       }},
      IdentityMutation{"plan-schema",
                       +[](internal::Phase4H4096SessionV5PreflightIdentity* identity) {
                         ++identity->targeted_regeneration_plan_schema_version;
                       }},
      IdentityMutation{"execution-schema",
                       +[](internal::Phase4H4096SessionV5PreflightIdentity* identity) {
                         ++identity->targeted_regeneration_execution_schema_version;
                       }},
      IdentityMutation{"present-price",
                       +[](internal::Phase4H4096SessionV5PreflightIdentity* identity) {
                         ++identity->present_step_per_overuse_unit;
                       }},
      IdentityMutation{"history-price",
                       +[](internal::Phase4H4096SessionV5PreflightIdentity* identity) {
                         ++identity->history_step_per_overuse_unit;
                       }},
      IdentityMutation{"raw-authority",
                       +[](internal::Phase4H4096SessionV5PreflightIdentity* identity) {
                         identity->raw_authority = "mutated";
                       }},
      IdentityMutation{"superseded-raw-authority",
                       +[](internal::Phase4H4096SessionV5PreflightIdentity* identity) {
                         identity->superseded_raw_authority = "mutated";
                       }},
      IdentityMutation{"raw-schema",
                       +[](internal::Phase4H4096SessionV5PreflightIdentity* identity) {
                         ++identity->raw_schema_version;
                       }},
      IdentityMutation{"raw-wire",
                       +[](internal::Phase4H4096SessionV5PreflightIdentity* identity) {
                         ++identity->raw_wire_schema_version;
                       }},
      IdentityMutation{"telemetry-required",
                       +[](internal::Phase4H4096SessionV5PreflightIdentity* identity) {
                         identity->telemetry_required = !identity->telemetry_required;
                       }},
      IdentityMutation{"telemetry-authority",
                       +[](internal::Phase4H4096SessionV5PreflightIdentity* identity) {
                         identity->telemetry_authority = "mutated";
                       }},
      IdentityMutation{"superseded-telemetry-authority",
                       +[](internal::Phase4H4096SessionV5PreflightIdentity* identity) {
                         identity->superseded_telemetry_authority = "mutated";
                       }},
      IdentityMutation{"telemetry-schema",
                       +[](internal::Phase4H4096SessionV5PreflightIdentity* identity) {
                         ++identity->telemetry_schema_version;
                       }},
      IdentityMutation{"telemetry-wire",
                       +[](internal::Phase4H4096SessionV5PreflightIdentity* identity) {
                         ++identity->telemetry_wire_schema_version;
                       }},
      IdentityMutation{"canonical-budget",
                       +[](internal::Phase4H4096SessionV5PreflightIdentity* identity) {
                         ++identity->canonical_algorithm_budget_checksum;
                       }},
      IdentityMutation{"paired-budget",
                       +[](internal::Phase4H4096SessionV5PreflightIdentity* identity) {
                         ++identity->paired_semantic_budget_checksum;
                       }},
  };

  for (const Carrier carrier : kCarriers) {
    const Phase4CanonicalCellConfig cell =
        internal::BuildPhase4ConfirmatoryH4096SessionV5CanonicalCell(carrier);
    for (const IdentityMutation& mutation : mutations) {
      SCOPED_TRACE(CarrierName(carrier) + "/" + std::string(mutation.name));
      auto identity = internal::BuildPhase4ConfirmatoryH4096SessionV5PreflightIdentity(
          carrier, Endpoint::kController);
      mutation.apply(&identity);
      ExpectBeforeActivation(internal::PreflightPhase4ConfirmatoryH4096SessionV5Controller(
          identity, cell, CleanControllerSource()));
    }

    for (const Phase4TrialArm arm : kArms) {
      const Endpoint endpoint = WorkerEndpoint(arm);
      {
        auto identity =
            internal::BuildPhase4ConfirmatoryH4096SessionV5PreflightIdentity(carrier, endpoint);
        identity.endpoint = Endpoint::kController;
        ExpectBeforeActivation(internal::PreflightPhase4ConfirmatoryH4096SessionV5Worker(
            identity, cell, arm, CleanWorkerSource()));
      }
      {
        auto identity =
            internal::BuildPhase4ConfirmatoryH4096SessionV5PreflightIdentity(carrier, endpoint);
        identity.carrier = carrier == Carrier::kOrdinary ? Carrier::kSameRun : Carrier::kOrdinary;
        ExpectBeforeActivation(internal::PreflightPhase4ConfirmatoryH4096SessionV5Worker(
            identity, cell, arm, CleanWorkerSource()));
      }
    }
  }
}

TEST(Phase4H4096SessionV5ExecutionPreflightTest,
     SourceAssociationRejectsUnstampedDirtyMismatchedAndMalformedCommitsAfterConfiguration) {
  for (const Carrier carrier : kCarriers) {
    SCOPED_TRACE(CarrierName(carrier));
    const Phase4CanonicalCellConfig cell =
        internal::BuildPhase4ConfirmatoryH4096SessionV5CanonicalCell(carrier);
    const auto controller_identity =
        internal::BuildPhase4ConfirmatoryH4096SessionV5PreflightIdentity(carrier,
                                                                         Endpoint::kController);

    for (const auto mutate :
         {+[](internal::Phase4H4096SessionV5ControllerSourceAssociation* source) {
            source->source_stamped = false;
          },
          +[](internal::Phase4H4096SessionV5ControllerSourceAssociation* source) {
            source->source_tree_dirty = true;
          },
          +[](internal::Phase4H4096SessionV5ControllerSourceAssociation* source) {
            source->embedded_commit = "";
          }}) {
      auto source = CleanControllerSource();
      mutate(&source);
      const auto error = internal::PreflightPhase4ConfirmatoryH4096SessionV5Controller(
          controller_identity, cell, source);
      ExpectBeforeActivation(error);
      ASSERT_TRUE(error.has_value());
      EXPECT_EQ(error->invariant_id, "P4PAIR-H4096-SESSION-V5-SOURCE-001");
    }
    for (std::string_view runtime_commit :
         {"1123456789abcdef0123456789abcdef01234567", "0123456789ABCDEF0123456789ABCDEF01234567"}) {
      auto source = CleanControllerSource();
      source.runtime_commit = runtime_commit;
      const auto error = internal::PreflightPhase4ConfirmatoryH4096SessionV5Controller(
          controller_identity, cell, source);
      ExpectBeforeActivation(error);
      ASSERT_TRUE(error.has_value());
      EXPECT_EQ(error->invariant_id, "P4PAIR-H4096-SESSION-V5-SOURCE-002");
    }

    const auto worker_identity = internal::BuildPhase4ConfirmatoryH4096SessionV5PreflightIdentity(
        carrier, Endpoint::kWorker);
    for (const auto mutate : {+[](internal::Phase4H4096SessionV5WorkerSourceAssociation* source) {
                                source->source_stamped = false;
                              },
                              +[](internal::Phase4H4096SessionV5WorkerSourceAssociation* source) {
                                source->source_tree_dirty = true;
                              },
                              +[](internal::Phase4H4096SessionV5WorkerSourceAssociation* source) {
                                source->embedded_commit = "";
                              },
                              +[](internal::Phase4H4096SessionV5WorkerSourceAssociation* source) {
                                source->embedded_commit =
                                    "0123456789ABCDEF0123456789ABCDEF01234567";
                              }}) {
      auto source = CleanWorkerSource();
      mutate(&source);
      for (const Phase4TrialArm arm : kArms) {
        const auto error = internal::PreflightPhase4ConfirmatoryH4096SessionV5Worker(
            worker_identity, cell, arm, source);
        ExpectBeforeActivation(error);
        ASSERT_TRUE(error.has_value());
        EXPECT_EQ(error->invariant_id, "P4PAIR-H4096-SESSION-V5-SOURCE-001");
      }
    }

    Phase4CanonicalCellConfig bad_cell = cell;
    ++bad_cell.case_id;
    auto bad_source = CleanControllerSource();
    bad_source.source_tree_dirty = true;
    const auto ordered = internal::PreflightPhase4ConfirmatoryH4096SessionV5Controller(
        controller_identity, bad_cell, bad_source);
    ExpectBeforeActivation(ordered);
    ASSERT_TRUE(ordered.has_value());
    EXPECT_NE(ordered->invariant_id, "P4PAIR-H4096-SESSION-V5-SOURCE-001");
  }
}

TEST(Phase4H4096SessionV5ExecutionPreflightTest,
     HistoricalFinalizersAndAssemblersCloseV4AndCrossRejectV5ShapesExactly) {
  const Phase4ExternalResourceObservation observation;
  for (const Carrier carrier : kCarriers) {
    SCOPED_TRACE(CarrierName(carrier));
    const Phase4CanonicalCellConfig cell =
        internal::BuildPhase4ConfirmatoryH4096SessionV5CanonicalCell(carrier);
    const Phase4PairedTrialSpec v5_spec = CanonicalSpec(carrier);
    const Phase4PairedTrialSpec v4_spec =
        Built(internal::BuildPhase4CanonicalTrialSpecForCorpusV2H4096(
            cell, 0, Phase4TrialOrder::kBaselineFirst));

    if (carrier == Carrier::kOrdinary) {
      for (const Phase4TrialArm arm : kArms) {
        Phase4TrialArmExecution execution;
        execution.semantics.arm = arm;
        ExpectUnsupportedInvariant(internal::FinalizePhase4ConfirmatoryH4096OrdinaryTrialArm(
                                       v5_spec, execution, observation),
                                   "P4PAIR-SCHEMA-001");
        ExpectUnsupportedInvariant(internal::FinalizePhase4ConfirmatoryH4096OrdinaryTrialArm(
                                       v4_spec, execution, observation),
                                   "P4PAIR-CORPUS-V2-SESSION-AUTHORITY-001");
      }
      ExpectUnsupportedInvariant(internal::AssemblePhase4ConfirmatoryH4096OrdinaryPairedTrial(
                                     v5_spec, Phase4TrialArmRecord{}, Phase4TrialArmRecord{}),
                                 "P4PAIR-SCHEMA-001");
      ExpectUnsupportedInvariant(internal::AssemblePhase4ConfirmatoryH4096OrdinaryPairedTrial(
                                     v4_spec, Phase4TrialArmRecord{}, Phase4TrialArmRecord{}),
                                 "P4PAIR-CORPUS-V2-SESSION-AUTHORITY-001");
    } else {
      for (const Phase4TrialArm arm : kArms) {
        Phase4TrialArmExecution execution;
        execution.semantics.arm = arm;
        ExpectUnsupportedInvariant(internal::FinalizePhase4ConfirmatoryH4096SameRunTrialArm(
                                       v5_spec, execution, observation),
                                   "P4PAIR-SCHEMA-001");
        ExpectUnsupportedInvariant(internal::FinalizePhase4ConfirmatoryH4096SameRunTrialArm(
                                       v4_spec, execution, observation),
                                   "P4PAIR-CORPUS-V2-SESSION-AUTHORITY-001");
      }
      ExpectUnsupportedInvariant(internal::AssemblePhase4ConfirmatoryH4096SameRunPairedTrial(
                                     v5_spec, Phase4TrialArmRecord{}, Phase4TrialArmRecord{}),
                                 "P4PAIR-SCHEMA-001");
      ExpectUnsupportedInvariant(internal::AssemblePhase4ConfirmatoryH4096SameRunPairedTrial(
                                     v4_spec, Phase4TrialArmRecord{}, Phase4TrialArmRecord{}),
                                 "P4PAIR-CORPUS-V2-SESSION-AUTHORITY-001");
    }
  }
}

}  // namespace
}  // namespace apgar::benchmark
