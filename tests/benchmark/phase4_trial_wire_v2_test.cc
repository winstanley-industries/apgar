#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <utility>
#include <variant>
#include <vector>

#include "src/benchmark/phase4_paired_trial_internal.h"
#include "src/benchmark/phase4_trial_wire_internal.h"
#include "tests/support/google_test.h"

namespace apgar::benchmark::internal {
namespace {

constexpr std::size_t kChecksumBytes = sizeof(std::uint64_t);
constexpr std::uint64_t kFnv1aOffsetBasis = 14695981039346656037ULL;
constexpr std::uint64_t kFnv1aPrime = 1099511628211ULL;

[[nodiscard]] Phase4TrialArmWithSameRunTelemetryExecutionV1 DecisionExecution() {
  Phase4TrialArmWithSameRunTelemetryExecutionV1 result;
  Phase4TrialArmSemantics& semantics = result.execution.semantics;
  semantics.arm = Phase4TrialArm::kSequentialBaseline;
  semantics.execution_order = Phase4TrialOrder::kCandidateFirst;
  semantics.corpus_checksum = Phase4RepresentativeCorpusChecksumV1();
  semantics.case_id = 101;
  semantics.descriptor_fingerprint = 0x1001;
  semantics.case_checksum = 0x1002;
  semantics.board_content_hash = 0x1003;
  semantics.workload_checksum = 0x1004;
  semantics.capacity_model_checksum = 0x1005;
  semantics.budget_checksum = 0x1006;
  semantics.workload_net_count = 2;
  semantics.requested_pool_size = 4;
  semantics.repetition_index = 7;
  semantics.root_seed = 0x1007;
  semantics.preparation_worker_count = 1;
  semantics.baseline_sweeps = 1;
  semantics.candidate_regeneration_epochs = 1;
  semantics.candidate_columns_per_epoch = 2;
  semantics.candidate_terminal_selection_rounds = 1;
  semantics.external_budget = {
      .maximum_prepared_elapsed_nanoseconds = 1'000,
      .maximum_cold_elapsed_nanoseconds = 2'000,
      .maximum_address_space_bytes = 3'000,
      .maximum_peak_host_bytes = 4'000,
  };
  semantics.opportunity = {.route_queries = 2, .route_work_units = 20};
  semantics.actual = semantics.opportunity;
  semantics.requested_columns = 2;
  semantics.admitted_candidates = 1;
  semantics.rejected_columns = 1;
  semantics.final_candidate_count = 1;
  semantics.algorithm_session_checksum = 0x1008;
  semantics.terminal_reason = Phase4NormalizedTerminalReason::kFixedPointStalled;
  semantics.candidate_outcome_source = Phase4CandidateOutcomeSource::kNotCandidateArm;
  semantics.outcome = {
      .selected_net_count = 1,
      .no_candidate_net_count = 1,
      .world_checksum = 0x1009,
  };
  semantics.semantic_checksum = ComputePhase4TrialArmSemanticChecksumV1(semantics);
  result.execution.case_build_elapsed_nanoseconds = 11;
  result.execution.prepared_elapsed_nanoseconds = 12;
  result.execution.cold_elapsed_nanoseconds = 23;
  result.execution.preparer_lifecycle = {
      .workers_started_before = 1,
      .workers_started_after = 1,
      .invocations_started_before = 2,
      .invocations_started_after = 3,
      .invocations_completed_before = 2,
      .invocations_completed_after = 3,
  };

  result.telemetry.associated_semantic_checksum = semantics.semantic_checksum;
  result.telemetry.outcome = semantics.outcome;
  result.telemetry.per_net = {
      Phase4SameRunPerNetColumnOutcomesV1{
          .net = {.id = 10, .generation = 1},
          .columns =
              Phase4PerNetColumnOutcomesV1{
                  .requested_columns = 1,
                  .executed_route_queries = 1,
                  .admitted_candidates = 1,
              },
      },
      Phase4SameRunPerNetColumnOutcomesV1{
          .net = {.id = 20, .generation = 2},
          .columns =
              Phase4PerNetColumnOutcomesV1{
                  .requested_columns = 1,
                  .executed_route_queries = 1,
                  .disconnected_columns = 1,
              },
      },
  };
  result.telemetry.telemetry_checksum =
      ComputePhase4SameRunArmDecisionTelemetryChecksumV1(result.telemetry);
  return result;
}

[[nodiscard]] std::vector<std::uint8_t> Encoded(const Phase4TrialWireMessageV2& message) {
  Phase4TrialWireEncodeResultV2 result = EncodePhase4TrialWireMessageV2(message);
  EXPECT_TRUE(std::holds_alternative<std::vector<std::uint8_t>>(result))
      << (std::holds_alternative<Phase4TrialWireError>(result)
              ? std::get<Phase4TrialWireError>(result).detail
              : "");
  if (!std::holds_alternative<std::vector<std::uint8_t>>(result)) {
    std::abort();
  }
  return std::get<std::vector<std::uint8_t>>(std::move(result));
}

[[nodiscard]] Phase4TrialWireMessageV2 Decoded(std::span<const std::uint8_t> frame) {
  Phase4TrialWireDecodeResultV2 result = DecodePhase4TrialWireMessageV2(frame);
  EXPECT_TRUE(std::holds_alternative<Phase4TrialWireMessageV2>(result))
      << (std::holds_alternative<Phase4TrialWireError>(result)
              ? std::get<Phase4TrialWireError>(result).detail
              : "");
  if (!std::holds_alternative<Phase4TrialWireMessageV2>(result)) {
    std::abort();
  }
  return std::get<Phase4TrialWireMessageV2>(std::move(result));
}

[[nodiscard]] const Phase4TrialWireError& Rejected(const Phase4TrialWireDecodeResultV2& result) {
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

TEST(Phase4TrialWireV2Test, RoundTripsControlAndSameRunSuccessMessages) {
  Phase4DurableArmFailure durable_failure;
  durable_failure.payload_checksum = ComputePhase4DurableArmFailureChecksumV1(durable_failure);
  const std::array<Phase4TrialWireMessageV2, 6> messages = {
      Phase4TrialWireReady{
          .case_id = 101,
          .descriptor_fingerprint = 1,
          .case_checksum = 2,
          .board_content_hash = 3,
          .workload_checksum = 4,
          .capacity_model_checksum = 5,
      },
      Phase4TrialWireRunCommand{
          .repetition_index = 7,
          .execution_order = Phase4TrialOrder::kCandidateFirst,
      },
      Phase4TrialWireStop{},
      Phase4TrialWireSuccessV2{.decision_execution = DecisionExecution()},
      Phase4TrialWireFailure{.failure = std::move(durable_failure)},
      Phase4TrialWireStopped{},
  };
  for (const Phase4TrialWireMessageV2& message : messages) {
    EXPECT_EQ(Decoded(Encoded(message)), message);
  }
}

TEST(Phase4TrialWireV2Test, RejectsEveryTruncationAndAnyTrailingByte) {
  const std::vector<std::uint8_t> frame =
      Encoded(Phase4TrialWireSuccessV2{.decision_execution = DecisionExecution()});
  for (std::size_t size = 0; size < frame.size(); ++size) {
    EXPECT_EQ(Rejected(DecodePhase4TrialWireMessageV2(std::span(frame).first(size))).invariant_id,
              "benchmark.phase4_trial_wire.truncated.v2")
        << "prefix size " << size;
  }
  std::vector<std::uint8_t> trailing = frame;
  trailing.push_back(0);
  EXPECT_EQ(Rejected(DecodePhase4TrialWireMessageV2(trailing)).invariant_id,
            "benchmark.phase4_trial_wire.trailing.v2");
}

TEST(Phase4TrialWireV2Test, SchemaVersionsCannotBeCrossDecoded) {
  const std::vector<std::uint8_t> v2 = Encoded(Phase4TrialWireStop{});
  ASSERT_TRUE(std::holds_alternative<Phase4TrialWireError>(DecodePhase4TrialWireMessageV1(v2)));
  EXPECT_EQ(std::get<Phase4TrialWireError>(DecodePhase4TrialWireMessageV1(v2)).invariant_id,
            "benchmark.phase4_trial_wire.schema.v1");

  Phase4TrialWireEncodeResult v1_result = EncodePhase4TrialWireMessageV1(Phase4TrialWireStop{});
  ASSERT_TRUE(std::holds_alternative<std::vector<std::uint8_t>>(v1_result));
  EXPECT_EQ(Rejected(DecodePhase4TrialWireMessageV2(
                         std::get<std::vector<std::uint8_t>>(std::move(v1_result))))
                .invariant_id,
            "benchmark.phase4_trial_wire.schema.v2");
}

TEST(Phase4TrialWireV2Test, RejectsAuthenticatedEnumAndTelemetryCorruption) {
  std::vector<std::uint8_t> frame =
      Encoded(Phase4TrialWireSuccessV2{.decision_execution = DecisionExecution()});
  frame[kPhase4TrialWireHeaderBytesV2 + sizeof(std::uint32_t)] = 0xffU;
  Reauthenticate(&frame);
  EXPECT_EQ(Rejected(DecodePhase4TrialWireMessageV2(frame)).invariant_id,
            "benchmark.phase4_trial_wire.success_enum.v2");

  frame = Encoded(Phase4TrialWireSuccessV2{.decision_execution = DecisionExecution()});
  frame[frame.size() - 2U * kChecksumBytes] ^= 1U;
  Reauthenticate(&frame);
  EXPECT_EQ(Rejected(DecodePhase4TrialWireMessageV2(frame)).invariant_id,
            "benchmark.phase4_trial_wire.telemetry_checksum.v2");
}

TEST(Phase4TrialWireV2Test, PreflightsTelemetryCountBeforeAllocatingRows) {
  // The v1 execution payload is 404 fixed bytes; telemetry then carries schema,
  // association, outcome, and finally its u32 row count.
  constexpr std::size_t kExecutionPayloadBytes = 404;
  constexpr std::size_t kTelemetryCountPrefixBytes =
      sizeof(std::uint32_t) + sizeof(std::uint64_t) + 6U * sizeof(std::uint64_t);
  std::vector<std::uint8_t> frame =
      Encoded(Phase4TrialWireSuccessV2{.decision_execution = DecisionExecution()});
  StoreU32(&frame,
           kPhase4TrialWireHeaderBytesV2 + kExecutionPayloadBytes + kTelemetryCountPrefixBytes,
           kMaximumPhase4RepresentativeNetsV1);
  Reauthenticate(&frame);
  EXPECT_EQ(Rejected(DecodePhase4TrialWireMessageV2(frame)).invariant_id,
            "benchmark.phase4_trial_wire.telemetry_bound.v2");
}

TEST(Phase4TrialWireV2Test, EncodeRejectsTelemetryAssociationAndPartitionDrift) {
  Phase4TrialArmWithSameRunTelemetryExecutionV1 execution = DecisionExecution();
  execution.telemetry.associated_semantic_checksum ^= 1U;
  execution.telemetry.telemetry_checksum =
      ComputePhase4SameRunArmDecisionTelemetryChecksumV1(execution.telemetry);
  Phase4TrialWireEncodeResultV2 result =
      EncodePhase4TrialWireMessageV2(Phase4TrialWireSuccessV2{.decision_execution = execution});
  ASSERT_TRUE(std::holds_alternative<Phase4TrialWireError>(result));
  EXPECT_EQ(std::get<Phase4TrialWireError>(result).invariant_id,
            "benchmark.phase4_trial_wire.telemetry_association.v2");

  execution = DecisionExecution();
  ++execution.telemetry.per_net.front().columns.other_rejections;
  execution.telemetry.telemetry_checksum =
      ComputePhase4SameRunArmDecisionTelemetryChecksumV1(execution.telemetry);
  result =
      EncodePhase4TrialWireMessageV2(Phase4TrialWireSuccessV2{.decision_execution = execution});
  ASSERT_TRUE(std::holds_alternative<Phase4TrialWireError>(result));
  EXPECT_EQ(std::get<Phase4TrialWireError>(result).invariant_id,
            "benchmark.phase4_trial_wire.telemetry_partition.v2");
}

TEST(Phase4TrialWireV2Test, RejectsOversizedDeclaredPayloadDuringHeaderPreflight) {
  std::vector<std::uint8_t> frame = Encoded(Phase4TrialWireStop{});
  StoreU32(&frame, 13, static_cast<std::uint32_t>(kPhase4TrialWireMaximumPayloadBytesV2 + 1U));
  Phase4TrialWireExpectedFrameSizeResult result = Phase4TrialWireExpectedFrameSizeV2(frame);
  ASSERT_TRUE(std::holds_alternative<Phase4TrialWireError>(result));
  EXPECT_EQ(std::get<Phase4TrialWireError>(result).invariant_id,
            "benchmark.phase4_trial_wire.payload_bound.v2");
}

}  // namespace
}  // namespace apgar::benchmark::internal
