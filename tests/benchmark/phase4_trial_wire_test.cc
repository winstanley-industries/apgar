#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "src/benchmark/phase4_trial_wire_internal.h"
#include "tests/support/google_test.h"

namespace apgar::benchmark::internal {
namespace {

constexpr std::size_t kChecksumBytes = sizeof(std::uint64_t);
constexpr std::uint64_t kFnv1aOffsetBasis = 14695981039346656037ULL;
constexpr std::uint64_t kFnv1aPrime = 1099511628211ULL;

[[nodiscard]] std::vector<std::uint8_t> Encoded(const Phase4TrialWireMessage& message) {
  Phase4TrialWireEncodeResult result = EncodePhase4TrialWireMessageV1(message);
  EXPECT_TRUE(std::holds_alternative<std::vector<std::uint8_t>>(result))
      << (std::holds_alternative<Phase4TrialWireError>(result)
              ? std::get<Phase4TrialWireError>(result).detail
              : "");
  if (!std::holds_alternative<std::vector<std::uint8_t>>(result)) {
    std::abort();
  }
  return std::get<std::vector<std::uint8_t>>(std::move(result));
}

[[nodiscard]] Phase4TrialWireMessage Decoded(std::span<const std::uint8_t> frame) {
  Phase4TrialWireDecodeResult result = DecodePhase4TrialWireMessageV1(frame);
  EXPECT_TRUE(std::holds_alternative<Phase4TrialWireMessage>(result))
      << (std::holds_alternative<Phase4TrialWireError>(result)
              ? std::get<Phase4TrialWireError>(result).detail
              : "");
  if (!std::holds_alternative<Phase4TrialWireMessage>(result)) {
    std::abort();
  }
  return std::get<Phase4TrialWireMessage>(std::move(result));
}

[[nodiscard]] const Phase4TrialWireError& Rejected(const Phase4TrialWireDecodeResult& result) {
  EXPECT_TRUE(std::holds_alternative<Phase4TrialWireError>(result));
  if (!std::holds_alternative<Phase4TrialWireError>(result)) {
    std::abort();
  }
  return std::get<Phase4TrialWireError>(result);
}

void StoreU32(std::vector<std::uint8_t>* bytes, std::size_t offset, std::uint32_t value) {
  for (std::size_t index = 0; index < sizeof(value); ++index) {
    (*bytes)[offset + index] = static_cast<std::uint8_t>(value & 0xffU);
    value >>= 8U;
  }
}

void Reauthenticate(std::vector<std::uint8_t>* frame) {
  std::uint64_t checksum = kFnv1aOffsetBasis;
  for (const std::uint8_t byte : std::span(*frame).first(frame->size() - kChecksumBytes)) {
    checksum ^= byte;
    checksum *= kFnv1aPrime;
  }
  for (std::size_t index = 0; index < sizeof(checksum); ++index) {
    (*frame)[frame->size() - kChecksumBytes + index] = static_cast<std::uint8_t>(checksum & 0xffU);
    checksum >>= 8U;
  }
}

[[nodiscard]] Phase4TrialArmExecution Execution() {
  Phase4TrialArmExecution execution;
  execution.semantics.arm = Phase4TrialArm::kReusableCandidateAllocation;
  execution.semantics.execution_order = Phase4TrialOrder::kCandidateFirst;
  execution.semantics.corpus_checksum = 11;
  execution.semantics.case_id = 1'007;
  execution.semantics.descriptor_fingerprint = 12;
  execution.semantics.case_checksum = 13;
  execution.semantics.board_content_hash = 14;
  execution.semantics.workload_checksum = 15;
  execution.semantics.capacity_model_checksum = 16;
  execution.semantics.budget_checksum = 17;
  execution.semantics.workload_net_count = 96;
  execution.semantics.requested_pool_size = 16;
  execution.semantics.repetition_index = 19;
  execution.semantics.root_seed = 18;
  execution.semantics.preparation_worker_count = 4;
  execution.semantics.baseline_sweeps = 18;
  execution.semantics.candidate_regeneration_epochs = 2;
  execution.semantics.candidate_columns_per_epoch = 96;
  execution.semantics.candidate_terminal_selection_rounds = 17;
  execution.semantics.external_budget = {
      .maximum_prepared_elapsed_nanoseconds = 19,
      .maximum_cold_elapsed_nanoseconds = 20,
      .maximum_address_space_bytes = 21,
      .maximum_peak_host_bytes = 22,
  };
  execution.semantics.opportunity = {.route_queries = 23, .route_work_units = 24};
  execution.semantics.actual = {.route_queries = 25, .route_work_units = 26};
  execution.semantics.preparation_route_queries = 27;
  execution.semantics.preparation_route_work_units = 28;
  execution.semantics.regeneration_route_queries = 29;
  execution.semantics.regeneration_route_work_units = 30;
  execution.semantics.requested_columns = 31;
  execution.semantics.admitted_candidates = 32;
  execution.semantics.rejected_columns = 33;
  execution.semantics.final_candidate_count = 34;
  execution.semantics.preparation_checksum = 35;
  execution.semantics.algorithm_session_checksum = 36;
  execution.semantics.final_pool_manifest_checksum = 37;
  execution.semantics.final_rejection_manifest_checksum = 38;
  execution.semantics.terminal_reason = Phase4NormalizedTerminalReason::kFixedPointStalled;
  execution.semantics.candidate_outcome_source =
      Phase4CandidateOutcomeSource::kCommonLineageOneWorld;
  execution.semantics.outcome = {
      .selected_net_count = 39,
      .no_candidate_net_count = 40,
      .overused_resource_count = 41,
      .total_overuse_units = 42,
      .total_intrinsic_cost = 43,
      .world_checksum = 44,
  };
  execution.semantics.semantic_checksum = 45;
  execution.case_build_elapsed_nanoseconds = 46;
  execution.prepared_elapsed_nanoseconds = 47;
  execution.cold_elapsed_nanoseconds = 48;
  execution.preparer_lifecycle = {
      .workers_started_before = 49,
      .workers_started_after = 50,
      .invocations_started_before = 51,
      .invocations_started_after = 52,
      .invocations_completed_before = 53,
      .invocations_completed_after = 54,
  };
  return execution;
}

[[nodiscard]] Phase4TrialWireReady Ready() {
  return Phase4TrialWireReady{
      .case_id = 1'007,
      .descriptor_fingerprint = 12,
      .case_checksum = 13,
      .board_content_hash = 14,
      .workload_checksum = 15,
      .capacity_model_checksum = 16,
  };
}

[[nodiscard]] Phase4DurableArmFailure Failure() {
  Phase4DurableArmFailure failure;
  failure.summary_code = Phase4PairedTrialErrorCode::kCandidatePreparation;
  failure.arm = Phase4TrialArm::kReusableCandidateAllocation;
  failure.summary_required = 101;
  failure.summary_configured = 99;
  failure.summary_invariant_id = "summary.invariant";
  failure.summary_detail = "summary detail";
  failure.payload_kind = Phase4DurableFailurePayloadKind::kCandidatePreparation;
  failure.child_error_code = static_cast<std::uint8_t>(
      allocator::CpuCandidatePoolPreparationErrorCode::kWorkBoundExceeded);
  failure.child_invariant_id = "child.invariant";
  failure.child_detail = "child detail";
  failure.has_child_net = true;
  failure.child_net_id = 77;
  failure.child_net_generation = 3;
  failure.child_required = 102;
  failure.child_configured = 100;
  failure.has_case_identity = true;
  failure.case_id = 1'000;
  failure.descriptor_fingerprint = 201;
  failure.case_checksum = 202;
  failure.board_content_hash = 203;
  failure.workload_checksum = 204;
  failure.capacity_model_checksum = 205;
  failure.has_failed_observation = true;
  failure.failed_observation_checksum = 206;
  failure.attempted_column_count = 208;
  failure.attempted_route_queries = 208;
  failure.attempted_route_work_units = 209;
  failure.candidate_store_publication_committed = true;
  failure.authoritative_candidate_store_present = true;
  failure.reconciled_candidate_count = 210;
  failure.reconciled_rejection_count = 211;
  failure.reconciled_candidate_store_checksum = 212;
  failure.payload_checksum = ComputePhase4DurableArmFailureChecksumV1(failure);
  return failure;
}

[[nodiscard]] Phase4DurableArmFailure FailureForKind(Phase4DurableFailurePayloadKind kind) {
  Phase4DurableArmFailure failure;
  failure.payload_kind = kind;
  switch (kind) {
    case Phase4DurableFailurePayloadKind::kSummaryOnly:
      failure.summary_code = Phase4PairedTrialErrorCode::kInternalInvariant;
      break;
    case Phase4DurableFailurePayloadKind::kCorpus:
      failure.summary_code = Phase4PairedTrialErrorCode::kCaseBuild;
      failure.child_error_code =
          static_cast<std::uint8_t>(Phase4RepresentativeCorpusErrorCode::kUnknownCase);
      failure.child_invariant_id = "corpus.unknown";
      failure.child_detail = "unknown case";
      failure.case_id = 9'999;
      break;
    case Phase4DurableFailurePayloadKind::kSequential:
      failure.summary_code = Phase4PairedTrialErrorCode::kSequentialExecution;
      failure.child_error_code = static_cast<std::uint8_t>(
          allocator::SequentialNegotiatedBaselineErrorCode::kWorkBoundExceeded);
      failure.child_invariant_id = "sequential.work";
      failure.child_detail = "work bound";
      failure.has_case_identity = true;
      failure.case_id = 100;
      failure.descriptor_fingerprint = 1;
      failure.case_checksum = 2;
      failure.board_content_hash = 3;
      failure.workload_checksum = 4;
      failure.capacity_model_checksum = 5;
      break;
    case Phase4DurableFailurePayloadKind::kCandidatePreparation:
      return Failure();
    case Phase4DurableFailurePayloadKind::kCandidateSession:
      failure.summary_code = Phase4PairedTrialErrorCode::kCandidateSession;
      failure.arm = Phase4TrialArm::kReusableCandidateAllocation;
      failure.child_error_code = static_cast<std::uint8_t>(
          allocator::CpuCandidateAllocationSessionErrorCode::kWorkBoundExceeded);
      failure.child_invariant_id = "session.work";
      failure.child_detail = "work bound";
      failure.has_case_identity = true;
      failure.case_id = 100;
      failure.descriptor_fingerprint = 1;
      failure.case_checksum = 2;
      failure.board_content_hash = 3;
      failure.workload_checksum = 4;
      failure.capacity_model_checksum = 5;
      failure.has_epoch_index = true;
      failure.epoch_index = 1;
      failure.has_failed_observation = true;
      failure.failed_observation_checksum = 6;
      failure.attempted_column_count = 8;
      failure.attempted_route_queries = 8;
      failure.attempted_route_work_units = 9;
      failure.candidate_store_publication_committed = true;
      failure.authoritative_candidate_store_present = true;
      failure.reconciled_candidate_count = 10;
      failure.reconciled_rejection_count = 11;
      failure.reconciled_candidate_store_checksum = 12;
      break;
  }
  failure.payload_checksum = ComputePhase4DurableArmFailureChecksumV1(failure);
  return failure;
}

void ExpectFailureStateRejected(Phase4DurableArmFailure failure) {
  failure.payload_checksum = ComputePhase4DurableArmFailureChecksumV1(failure);
  const Phase4TrialWireEncodeResult result =
      EncodePhase4TrialWireMessageV1(Phase4TrialWireFailure{.failure = std::move(failure)});
  ASSERT_TRUE(std::holds_alternative<Phase4TrialWireError>(result));
  EXPECT_EQ(std::get<Phase4TrialWireError>(result).invariant_id,
            "benchmark.phase4_trial_wire.failure_state.v1");
}

TEST(Phase4TrialWireTest, PinsReadyFrameGoldenBytesAndHeaderSizeContract) {
  constexpr std::array<std::uint8_t, 69> expected = {
      0x41, 0x50, 0x47, 0x41, 0x52, 0x50, 0x34, 0x57, 0x01, 0x00, 0x00, 0x00, 0x00, 0x2c,
      0x00, 0x00, 0x00, 0xef, 0x03, 0x00, 0x00, 0x0c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x0d, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0e, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x0f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0xe0, 0xd4, 0xf1, 0xb6, 0x9d, 0x9e, 0x17, 0x71,
  };
  const std::vector<std::uint8_t> frame = Encoded(Ready());
  EXPECT_EQ(frame, std::vector<std::uint8_t>(expected.begin(), expected.end()));

  for (std::size_t size = 0; size < kPhase4TrialWireHeaderBytesV1; ++size) {
    Phase4TrialWireExpectedFrameSizeResult inspected =
        Phase4TrialWireExpectedFrameSizeV1(std::span(frame).first(size));
    ASSERT_TRUE(std::holds_alternative<std::optional<std::size_t>>(inspected));
    EXPECT_FALSE(std::get<std::optional<std::size_t>>(inspected).has_value());
  }
  Phase4TrialWireExpectedFrameSizeResult inspected = Phase4TrialWireExpectedFrameSizeV1(frame);
  ASSERT_TRUE(std::holds_alternative<std::optional<std::size_t>>(inspected));
  EXPECT_EQ(std::get<std::optional<std::size_t>>(inspected), frame.size());
}

TEST(Phase4TrialWireTest, RoundTripsEveryMessageKindLosslessly) {
  const std::array<Phase4TrialWireMessage, 6> messages = {
      Ready(),
      Phase4TrialWireRunCommand{
          .repetition_index = 19,
          .execution_order = Phase4TrialOrder::kCandidateFirst,
      },
      Phase4TrialWireStop{},
      Phase4TrialWireSuccess{.execution = Execution()},
      Phase4TrialWireFailure{.failure = Failure()},
      Phase4TrialWireStopped{},
  };
  for (const Phase4TrialWireMessage& message : messages) {
    EXPECT_EQ(Decoded(Encoded(message)), message);
  }
}

TEST(Phase4TrialWireTest, RoundTripsCanonicalFailureStateForEveryPayloadKind) {
  for (const Phase4DurableFailurePayloadKind kind : {
           Phase4DurableFailurePayloadKind::kSummaryOnly,
           Phase4DurableFailurePayloadKind::kCorpus,
           Phase4DurableFailurePayloadKind::kSequential,
           Phase4DurableFailurePayloadKind::kCandidatePreparation,
           Phase4DurableFailurePayloadKind::kCandidateSession,
       }) {
    Phase4DurableArmFailure failure = FailureForKind(kind);
    const Phase4TrialWireMessage message = Phase4TrialWireFailure{.failure = std::move(failure)};
    EXPECT_EQ(Decoded(Encoded(message)), message);
  }
}

TEST(Phase4TrialWireTest, AcceptsAllCanonicalReconciledObservationAndStoreShapes) {
  std::vector<Phase4DurableArmFailure> failures;

  Phase4DurableArmFailure preparation =
      FailureForKind(Phase4DurableFailurePayloadKind::kCandidatePreparation);
  preparation.candidate_store_publication_committed = false;
  preparation.authoritative_candidate_store_present = false;
  preparation.reconciled_candidate_count = 0;
  preparation.reconciled_rejection_count = 0;
  preparation.reconciled_candidate_store_checksum = 0;
  preparation.payload_checksum = ComputePhase4DurableArmFailureChecksumV1(preparation);
  failures.push_back(std::move(preparation));

  preparation = FailureForKind(Phase4DurableFailurePayloadKind::kCandidatePreparation);
  preparation.has_failed_observation = false;
  preparation.failed_observation_checksum = 0;
  preparation.attempted_column_count = 0;
  preparation.attempted_route_queries = 0;
  preparation.attempted_route_work_units = 0;
  preparation.candidate_store_publication_committed = false;
  preparation.authoritative_candidate_store_present = false;
  preparation.reconciled_candidate_count = 0;
  preparation.reconciled_rejection_count = 0;
  preparation.reconciled_candidate_store_checksum = 0;
  preparation.payload_checksum = ComputePhase4DurableArmFailureChecksumV1(preparation);
  failures.push_back(std::move(preparation));

  Phase4DurableArmFailure session =
      FailureForKind(Phase4DurableFailurePayloadKind::kCandidateSession);
  session.has_epoch_index = false;
  session.epoch_index = 0;
  session.has_failed_observation = false;
  session.failed_observation_checksum = 0;
  session.attempted_column_count = 0;
  session.attempted_route_queries = 0;
  session.attempted_route_work_units = 0;
  session.candidate_store_publication_committed = false;
  session.payload_checksum = ComputePhase4DurableArmFailureChecksumV1(session);
  failures.push_back(std::move(session));

  Phase4DurableArmFailure corpus = FailureForKind(Phase4DurableFailurePayloadKind::kCorpus);
  corpus.child_error_code =
      static_cast<std::uint8_t>(Phase4RepresentativeCorpusErrorCode::kWorkBoundExceeded);
  corpus.has_child_net = true;
  corpus.child_net_id = 13;
  corpus.child_net_generation = 2;
  corpus.child_required = 100;
  corpus.child_configured = 99;
  corpus.child_bound_kind =
      static_cast<std::uint8_t>(Phase4RepresentativeWorkBound::kCompiledNodesAndHostBytes);
  corpus.child_secondary_required = 200;
  corpus.child_secondary_configured = 199;
  corpus.attempted_column_count = 12;
  corpus.payload_checksum = ComputePhase4DurableArmFailureChecksumV1(corpus);
  failures.push_back(std::move(corpus));

  for (Phase4DurableArmFailure& failure : failures) {
    const Phase4TrialWireMessage message = Phase4TrialWireFailure{.failure = std::move(failure)};
    EXPECT_EQ(Decoded(Encoded(message)), message);
  }
}

TEST(Phase4TrialWireTest, RejectsContradictoryFailurePayloadKindsAndArms) {
  Phase4DurableArmFailure failure =
      FailureForKind(Phase4DurableFailurePayloadKind::kCandidatePreparation);
  failure.arm = Phase4TrialArm::kSequentialBaseline;
  ExpectFailureStateRejected(std::move(failure));

  failure = FailureForKind(Phase4DurableFailurePayloadKind::kSequential);
  failure.summary_code = Phase4PairedTrialErrorCode::kCandidatePreparation;
  ExpectFailureStateRejected(std::move(failure));

  failure = FailureForKind(Phase4DurableFailurePayloadKind::kSummaryOnly);
  failure.child_detail = "smuggled child state";
  ExpectFailureStateRejected(std::move(failure));

  for (const auto [summary_code, arm] : {
           std::pair{Phase4PairedTrialErrorCode::kCaseBuild, Phase4TrialArm::kSequentialBaseline},
           std::pair{Phase4PairedTrialErrorCode::kSequentialExecution,
                     Phase4TrialArm::kSequentialBaseline},
           std::pair{Phase4PairedTrialErrorCode::kCandidatePreparation,
                     Phase4TrialArm::kReusableCandidateAllocation},
           std::pair{Phase4PairedTrialErrorCode::kCandidateSession,
                     Phase4TrialArm::kReusableCandidateAllocation},
       }) {
    failure = FailureForKind(Phase4DurableFailurePayloadKind::kSummaryOnly);
    failure.summary_code = summary_code;
    failure.arm = arm;
    ExpectFailureStateRejected(std::move(failure));
  }

  for (const Phase4PairedTrialErrorCode summary_code : {
           Phase4PairedTrialErrorCode::kMeasurementAssociation,
           Phase4PairedTrialErrorCode::kExternalBudgetExceeded,
           Phase4PairedTrialErrorCode::kPairMismatch,
       }) {
    failure = FailureForKind(Phase4DurableFailurePayloadKind::kSummaryOnly);
    failure.summary_code = summary_code;
    ExpectFailureStateRejected(std::move(failure));
  }

  for (const Phase4PairedTrialErrorCode summary_code : {
           Phase4PairedTrialErrorCode::kExternalAuthorityUnavailable,
           Phase4PairedTrialErrorCode::kResourceExhausted,
       }) {
    failure = FailureForKind(Phase4DurableFailurePayloadKind::kSummaryOnly);
    failure.summary_code = summary_code;
    failure.payload_checksum = ComputePhase4DurableArmFailureChecksumV1(failure);
    const Phase4TrialWireMessage message = Phase4TrialWireFailure{.failure = std::move(failure)};
    EXPECT_EQ(Decoded(Encoded(message)), message);
  }

  failure = FailureForKind(Phase4DurableFailurePayloadKind::kCorpus);
  failure.has_epoch_index = true;
  ExpectFailureStateRejected(std::move(failure));

  failure = FailureForKind(Phase4DurableFailurePayloadKind::kCandidateSession);
  failure.authoritative_candidate_store_present = false;
  failure.reconciled_candidate_count = 0;
  failure.reconciled_rejection_count = 0;
  failure.reconciled_candidate_store_checksum = 0;
  failure.candidate_store_publication_committed = false;
  ExpectFailureStateRejected(std::move(failure));
}

TEST(Phase4TrialWireTest, RejectsContradictoryFailurePresenceAndCanonicalZeroState) {
  Phase4DurableArmFailure failure = FailureForKind(Phase4DurableFailurePayloadKind::kSequential);
  failure.has_child_net = false;
  failure.child_net_id = 99;
  ExpectFailureStateRejected(std::move(failure));

  failure = FailureForKind(Phase4DurableFailurePayloadKind::kSequential);
  failure.has_case_identity = false;
  ExpectFailureStateRejected(std::move(failure));

  failure = FailureForKind(Phase4DurableFailurePayloadKind::kCandidateSession);
  failure.has_epoch_index = false;
  ExpectFailureStateRejected(std::move(failure));

  failure = FailureForKind(Phase4DurableFailurePayloadKind::kCandidatePreparation);
  failure.has_failed_observation = false;
  ExpectFailureStateRejected(std::move(failure));

  failure = FailureForKind(Phase4DurableFailurePayloadKind::kCandidatePreparation);
  failure.has_failed_observation = false;
  failure.failed_observation_checksum = 0;
  failure.attempted_route_queries = 0;
  failure.attempted_route_work_units = 0;
  ExpectFailureStateRejected(std::move(failure));

  failure = FailureForKind(Phase4DurableFailurePayloadKind::kCandidatePreparation);
  failure.candidate_store_publication_committed = false;
  ExpectFailureStateRejected(std::move(failure));

  failure = FailureForKind(Phase4DurableFailurePayloadKind::kSequential);
  failure.reconciled_candidate_count = 1;
  ExpectFailureStateRejected(std::move(failure));
}

TEST(Phase4TrialWireTest, RejectsEmptyOrImpossibleCorpusWorkBoundWitnesses) {
  const auto canonical_work_bound = [] {
    Phase4DurableArmFailure failure = FailureForKind(Phase4DurableFailurePayloadKind::kCorpus);
    failure.child_error_code =
        static_cast<std::uint8_t>(Phase4RepresentativeCorpusErrorCode::kWorkBoundExceeded);
    failure.has_child_net = true;
    failure.child_net_id = 13;
    failure.child_net_generation = 2;
    failure.child_required = 100;
    failure.child_configured = 99;
    failure.child_bound_kind =
        static_cast<std::uint8_t>(Phase4RepresentativeWorkBound::kCompiledNodes);
    failure.attempted_column_count = 12;
    return failure;
  };

  Phase4DurableArmFailure failure = canonical_work_bound();
  failure.child_net_id = 0;
  ExpectFailureStateRejected(std::move(failure));

  failure = canonical_work_bound();
  failure.child_required = failure.child_configured;
  ExpectFailureStateRejected(std::move(failure));

  failure = canonical_work_bound();
  failure.child_bound_kind =
      static_cast<std::uint8_t>(Phase4RepresentativeWorkBound::kCompiledNodesAndHostBytes);
  failure.child_secondary_required = 200;
  failure.child_secondary_configured = 200;
  ExpectFailureStateRejected(std::move(failure));
}

TEST(Phase4TrialWireTest, RejectsImpossibleObservedFailureWorkCounters) {
  Phase4DurableArmFailure failure = Failure();
  --failure.attempted_column_count;
  ExpectFailureStateRejected(std::move(failure));

  failure = Failure();
  failure.attempted_route_work_units =
      failure.attempted_route_queries * kPhase4CanonicalRouteWorkUnitsPerQueryV1 + 1;
  ExpectFailureStateRejected(std::move(failure));

  failure = Failure();
  failure.attempted_route_queries = std::numeric_limits<std::uint64_t>::max();
  failure.attempted_column_count = failure.attempted_route_queries;
  failure.attempted_route_work_units = std::numeric_limits<std::uint64_t>::max();
  failure.payload_checksum = ComputePhase4DurableArmFailureChecksumV1(failure);
  const Phase4TrialWireMessage message = Phase4TrialWireFailure{.failure = std::move(failure)};
  EXPECT_EQ(Decoded(Encoded(message)), message);
}

TEST(Phase4TrialWireTest, RejectsEveryTruncationAndAnyTrailingByte) {
  const std::vector<std::uint8_t> frame = Encoded(Phase4TrialWireSuccess{.execution = Execution()});
  for (std::size_t size = 0; size < frame.size(); ++size) {
    const Phase4TrialWireDecodeResult result =
        DecodePhase4TrialWireMessageV1(std::span(frame).first(size));
    EXPECT_EQ(Rejected(result).invariant_id, "benchmark.phase4_trial_wire.truncated.v1")
        << "prefix size " << size;
  }

  std::vector<std::uint8_t> trailing = frame;
  trailing.push_back(0);
  EXPECT_EQ(Rejected(DecodePhase4TrialWireMessageV1(trailing)).invariant_id,
            "benchmark.phase4_trial_wire.trailing.v1");
}

TEST(Phase4TrialWireTest, AuthenticatesEnvelopeAndPayloadBeforeDecoding) {
  std::vector<std::uint8_t> frame = Encoded(Phase4TrialWireRunCommand{.repetition_index = 4});
  frame[kPhase4TrialWireHeaderBytesV1] ^= 0x80U;
  EXPECT_EQ(Rejected(DecodePhase4TrialWireMessageV1(frame)).invariant_id,
            "benchmark.phase4_trial_wire.checksum.v1");

  frame = Encoded(Phase4TrialWireRunCommand{.repetition_index = 4});
  frame[kPhase4TrialWireHeaderBytesV1 + sizeof(std::uint32_t)] = 0xffU;
  Reauthenticate(&frame);
  EXPECT_EQ(Rejected(DecodePhase4TrialWireMessageV1(frame)).invariant_id,
            "benchmark.phase4_trial_wire.run_enum.v1");

  // The first boolean in an otherwise empty failure payload follows forty
  // fixed or zero-length bytes. Reauthenticate the deliberate corruption so
  // this exercises canonical payload decoding rather than envelope auth.
  Phase4DurableArmFailure failure;
  failure.payload_checksum = ComputePhase4DurableArmFailureChecksumV1(failure);
  frame = Encoded(Phase4TrialWireFailure{.failure = std::move(failure)});
  frame[kPhase4TrialWireHeaderBytesV1 + 40] = 2U;
  Reauthenticate(&frame);
  EXPECT_EQ(Rejected(DecodePhase4TrialWireMessageV1(frame)).invariant_id,
            "benchmark.phase4_trial_wire.bool.v1");

  frame = Encoded(Phase4TrialWireFailure{.failure = Failure()});
  frame[frame.size() - 2 * kChecksumBytes] ^= 1U;
  Reauthenticate(&frame);
  EXPECT_EQ(Rejected(DecodePhase4TrialWireMessageV1(frame)).invariant_id,
            "benchmark.phase4_trial_wire.failure_checksum.v1");
}

TEST(Phase4TrialWireTest, RejectsMalformedAndOversizedHeadersBeforeWaitingForPayload) {
  std::vector<std::uint8_t> frame = Encoded(Ready());
  frame[0] ^= 1U;
  auto malformed = Phase4TrialWireExpectedFrameSizeV1(frame);
  ASSERT_TRUE(std::holds_alternative<Phase4TrialWireError>(malformed));
  EXPECT_EQ(std::get<Phase4TrialWireError>(malformed).invariant_id,
            "benchmark.phase4_trial_wire.magic.v1");

  frame = Encoded(Ready());
  StoreU32(&frame, 8, kPhase4TrialWireSchemaVersion + 1);
  EXPECT_EQ(Rejected(DecodePhase4TrialWireMessageV1(frame)).invariant_id,
            "benchmark.phase4_trial_wire.schema.v1");

  frame = Encoded(Ready());
  frame[12] = 0xffU;
  malformed = Phase4TrialWireExpectedFrameSizeV1(frame);
  ASSERT_TRUE(std::holds_alternative<Phase4TrialWireError>(malformed));
  EXPECT_EQ(std::get<Phase4TrialWireError>(malformed).invariant_id,
            "benchmark.phase4_trial_wire.kind.v1");

  frame = Encoded(Ready());
  StoreU32(&frame, 13, static_cast<std::uint32_t>(kPhase4TrialWireMaximumPayloadBytesV1 + 1));
  malformed = Phase4TrialWireExpectedFrameSizeV1(frame);
  ASSERT_TRUE(std::holds_alternative<Phase4TrialWireError>(malformed));
  EXPECT_EQ(std::get<Phase4TrialWireError>(malformed).invariant_id,
            "benchmark.phase4_trial_wire.payload_bound.v1");
}

TEST(Phase4TrialWireTest, EncodeRejectsUnknownEnumsAndUnboundedStrings) {
  for (int field = 0; field < 6; ++field) {
    Phase4TrialWireReady ready = Ready();
    switch (field) {
      case 0:
        ready.case_id = 0;
        break;
      case 1:
        ready.descriptor_fingerprint = 0;
        break;
      case 2:
        ready.case_checksum = 0;
        break;
      case 3:
        ready.board_content_hash = 0;
        break;
      case 4:
        ready.workload_checksum = 0;
        break;
      case 5:
        ready.capacity_model_checksum = 0;
        break;
    }
    Phase4TrialWireEncodeResult invalid_ready = EncodePhase4TrialWireMessageV1(ready);
    ASSERT_TRUE(std::holds_alternative<Phase4TrialWireError>(invalid_ready));
    EXPECT_EQ(std::get<Phase4TrialWireError>(invalid_ready).invariant_id,
              "benchmark.phase4_trial_wire.ready_identity.v1");
  }

  Phase4TrialWireRunCommand run;
  run.execution_order = static_cast<Phase4TrialOrder>(255);
  Phase4TrialWireEncodeResult invalid_enum = EncodePhase4TrialWireMessageV1(run);
  ASSERT_TRUE(std::holds_alternative<Phase4TrialWireError>(invalid_enum));
  EXPECT_EQ(std::get<Phase4TrialWireError>(invalid_enum).invariant_id,
            "benchmark.phase4_trial_wire.run_enum.v1");

  Phase4DurableArmFailure failure = Failure();
  failure.summary_detail.assign(kPhase4TrialWireMaximumPayloadBytesV1 + 1, 'x');
  failure.payload_checksum = ComputePhase4DurableArmFailureChecksumV1(failure);
  Phase4TrialWireEncodeResult oversized =
      EncodePhase4TrialWireMessageV1(Phase4TrialWireFailure{.failure = std::move(failure)});
  ASSERT_TRUE(std::holds_alternative<Phase4TrialWireError>(oversized));
  EXPECT_EQ(std::get<Phase4TrialWireError>(oversized).invariant_id,
            "benchmark.phase4_trial_wire.string_bound.v1");
}

}  // namespace
}  // namespace apgar::benchmark::internal
