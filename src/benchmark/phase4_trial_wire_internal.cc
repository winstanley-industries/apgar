#include "src/benchmark/phase4_trial_wire_internal.h"

#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace apgar::benchmark::internal {
namespace {

constexpr std::array<std::uint8_t, 8> kMagic = {'A', 'P', 'G', 'A', 'R', 'P', '4', 'W'};
constexpr std::size_t kSchemaBytes = sizeof(std::uint32_t);
constexpr std::size_t kKindBytes = sizeof(std::uint8_t);
constexpr std::size_t kLengthBytes = sizeof(std::uint32_t);
constexpr std::size_t kChecksumBytes = sizeof(std::uint64_t);
constexpr std::uint64_t kFnv1aOffsetBasis = 14695981039346656037ULL;
constexpr std::uint64_t kFnv1aPrime = 1099511628211ULL;

static_assert(kPhase4TrialWireHeaderBytesV1 ==
              kMagic.size() + kSchemaBytes + kKindBytes + kLengthBytes);
static_assert(kPhase4TrialWireMaxFrameBytesV1 == kPhase4TrialWireHeaderBytesV1 +
                                                     kPhase4TrialWireMaximumPayloadBytesV1 +
                                                     kChecksumBytes);

[[nodiscard]] Phase4TrialWireError Error(std::string_view invariant_id, std::string detail) {
  return Phase4TrialWireError{
      .invariant_id = std::string(invariant_id),
      .detail = std::move(detail),
  };
}

[[nodiscard]] Phase4TrialWireError ResourceError(std::string_view detail) {
  return Error("benchmark.phase4_trial_wire.resource.v1", std::string(detail));
}

void AppendByte(std::vector<std::uint8_t>& bytes, std::uint8_t value) { bytes.push_back(value); }

void AppendU32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
  for (std::size_t index = 0; index < sizeof(value); ++index) {
    AppendByte(bytes, static_cast<std::uint8_t>(value & 0xffU));
    value >>= 8U;
  }
}

void AppendU64(std::vector<std::uint8_t>& bytes, std::uint64_t value) {
  for (std::size_t index = 0; index < sizeof(value); ++index) {
    AppendByte(bytes, static_cast<std::uint8_t>(value & 0xffU));
    value >>= 8U;
  }
}

[[nodiscard]] std::uint32_t LoadU32(std::span<const std::uint8_t> bytes,
                                    std::size_t offset) noexcept {
  std::uint32_t value = 0;
  for (std::size_t index = 0; index < sizeof(value); ++index) {
    value |= static_cast<std::uint32_t>(bytes[offset + index]) << (index * 8U);
  }
  return value;
}

[[nodiscard]] std::uint64_t LoadU64(std::span<const std::uint8_t> bytes,
                                    std::size_t offset) noexcept {
  std::uint64_t value = 0;
  for (std::size_t index = 0; index < sizeof(value); ++index) {
    value |= static_cast<std::uint64_t>(bytes[offset + index]) << (index * 8U);
  }
  return value;
}

[[nodiscard]] std::uint64_t Fnv1a(std::span<const std::uint8_t> bytes) noexcept {
  std::uint64_t value = kFnv1aOffsetBasis;
  for (const std::uint8_t byte : bytes) {
    value ^= byte;
    value *= kFnv1aPrime;
  }
  return value;
}

[[nodiscard]] bool ValidArm(Phase4TrialArm arm) noexcept {
  return arm == Phase4TrialArm::kSequentialBaseline ||
         arm == Phase4TrialArm::kReusableCandidateAllocation;
}

[[nodiscard]] bool ValidOrder(Phase4TrialOrder order) noexcept {
  return order == Phase4TrialOrder::kBaselineFirst || order == Phase4TrialOrder::kCandidateFirst;
}

[[nodiscard]] bool ValidTerminalReason(Phase4NormalizedTerminalReason reason) noexcept {
  switch (reason) {
    case Phase4NormalizedTerminalReason::kFeasible:
    case Phase4NormalizedTerminalReason::kNoAdmissibleCandidate:
    case Phase4NormalizedTerminalReason::kFixedPointStalled:
    case Phase4NormalizedTerminalReason::kStoppingBudgetExhausted:
    case Phase4NormalizedTerminalReason::kResourceRefinementRequired:
      return true;
  }
  return false;
}

[[nodiscard]] bool ValidOutcomeSource(Phase4CandidateOutcomeSource source) noexcept {
  switch (source) {
    case Phase4CandidateOutcomeSource::kNotCandidateArm:
    case Phase4CandidateOutcomeSource::kPreferredMultiWorld:
    case Phase4CandidateOutcomeSource::kCommonLineageOneWorld:
      return true;
  }
  return false;
}

[[nodiscard]] bool ValidPairedErrorCode(Phase4PairedTrialErrorCode code) noexcept {
  return static_cast<std::uint8_t>(code) <=
         static_cast<std::uint8_t>(Phase4PairedTrialErrorCode::kInternalInvariant);
}

[[nodiscard]] bool ValidFailurePayloadKind(Phase4DurableFailurePayloadKind kind) noexcept {
  switch (kind) {
    case Phase4DurableFailurePayloadKind::kSummaryOnly:
    case Phase4DurableFailurePayloadKind::kCorpus:
    case Phase4DurableFailurePayloadKind::kSequential:
    case Phase4DurableFailurePayloadKind::kCandidatePreparation:
    case Phase4DurableFailurePayloadKind::kCandidateSession:
      return true;
  }
  return false;
}

[[nodiscard]] bool ValidChildErrorCode(Phase4DurableFailurePayloadKind kind,
                                       std::uint8_t code) noexcept {
  switch (kind) {
    case Phase4DurableFailurePayloadKind::kSummaryOnly:
      return code == 0;
    case Phase4DurableFailurePayloadKind::kCorpus:
      return code <=
             static_cast<std::uint8_t>(Phase4RepresentativeCorpusErrorCode::kWorkBoundExceeded);
    case Phase4DurableFailurePayloadKind::kSequential:
      return code <= static_cast<std::uint8_t>(
                         allocator::SequentialNegotiatedBaselineErrorCode::kInternalInvariant);
    case Phase4DurableFailurePayloadKind::kCandidatePreparation:
      return code <= static_cast<std::uint8_t>(
                         allocator::CpuCandidatePoolPreparationErrorCode::kInternalInvariant);
    case Phase4DurableFailurePayloadKind::kCandidateSession:
      return code <= static_cast<std::uint8_t>(
                         allocator::CpuCandidateAllocationSessionErrorCode::kInternalInvariant);
  }
  return false;
}

[[nodiscard]] bool ValidMessageKind(Phase4TrialWireMessageKind kind) noexcept {
  switch (kind) {
    case Phase4TrialWireMessageKind::kReady:
    case Phase4TrialWireMessageKind::kRun:
    case Phase4TrialWireMessageKind::kStop:
    case Phase4TrialWireMessageKind::kSuccess:
    case Phase4TrialWireMessageKind::kFailure:
    case Phase4TrialWireMessageKind::kStopped:
      return true;
  }
  return false;
}

[[nodiscard]] std::optional<Phase4TrialWireError> ValidateReady(const Phase4TrialWireReady& ready) {
  if (ready.case_id == 0 || ready.descriptor_fingerprint == 0 || ready.case_checksum == 0 ||
      ready.board_content_hash == 0 || ready.workload_checksum == 0 ||
      ready.capacity_model_checksum == 0) {
    return Error("benchmark.phase4_trial_wire.ready_identity.v1",
                 "ready payload must carry one complete nonzero warmup case identity");
  }
  return std::nullopt;
}

class PayloadWriter {
 public:
  void AddByte(std::uint8_t value) { AppendByte(bytes_, value); }
  void AddBool(bool value) { AddByte(value ? 1U : 0U); }
  void AddU32(std::uint32_t value) { AppendU32(bytes_, value); }
  void AddU64(std::uint64_t value) { AppendU64(bytes_, value); }

  [[nodiscard]] bool AddString(std::string_view value) {
    if (value.size() > kPhase4TrialWireMaximumPayloadBytesV1 ||
        value.size() > std::numeric_limits<std::uint32_t>::max()) {
      return false;
    }
    AddU32(static_cast<std::uint32_t>(value.size()));
    bytes_.insert(bytes_.end(), value.begin(), value.end());
    return true;
  }

  [[nodiscard]] const std::vector<std::uint8_t>& bytes() const noexcept { return bytes_; }
  [[nodiscard]] std::vector<std::uint8_t> Take() && { return std::move(bytes_); }

 private:
  std::vector<std::uint8_t> bytes_;
};

class PayloadReader {
 public:
  explicit PayloadReader(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}

  [[nodiscard]] bool ReadByte(std::uint8_t* value, std::string_view field) {
    if (remaining() < sizeof(*value)) {
      return Reject("benchmark.phase4_trial_wire.payload_truncated.v1",
                    "wire payload ended while reading " + std::string(field));
    }
    *value = bytes_[position_++];
    return true;
  }

  [[nodiscard]] bool ReadBool(bool* value, std::string_view field) {
    std::uint8_t encoded = 0;
    if (!ReadByte(&encoded, field)) {
      return false;
    }
    if (encoded > 1U) {
      return Reject("benchmark.phase4_trial_wire.bool.v1",
                    "wire boolean is noncanonical for " + std::string(field));
    }
    *value = encoded != 0;
    return true;
  }

  [[nodiscard]] bool ReadU32(std::uint32_t* value, std::string_view field) {
    if (remaining() < sizeof(*value)) {
      return Reject("benchmark.phase4_trial_wire.payload_truncated.v1",
                    "wire payload ended while reading " + std::string(field));
    }
    *value = LoadU32(bytes_, position_);
    position_ += sizeof(*value);
    return true;
  }

  [[nodiscard]] bool ReadU64(std::uint64_t* value, std::string_view field) {
    if (remaining() < sizeof(*value)) {
      return Reject("benchmark.phase4_trial_wire.payload_truncated.v1",
                    "wire payload ended while reading " + std::string(field));
    }
    *value = LoadU64(bytes_, position_);
    position_ += sizeof(*value);
    return true;
  }

  [[nodiscard]] bool ReadString(std::string* value, std::string_view field) {
    std::uint32_t size = 0;
    if (!ReadU32(&size, field)) {
      return false;
    }
    if (size > remaining()) {
      return Reject("benchmark.phase4_trial_wire.payload_truncated.v1",
                    "wire string exceeds the remaining payload for " + std::string(field));
    }
    const char* begin = reinterpret_cast<const char*>(bytes_.data() + position_);
    value->assign(begin, size);
    position_ += size;
    return true;
  }

  [[nodiscard]] bool Finish() {
    if (remaining() != 0) {
      return Reject("benchmark.phase4_trial_wire.payload_trailing.v1",
                    "wire payload has trailing bytes after its declared message");
    }
    return true;
  }

  [[nodiscard]] bool Reject(std::string_view invariant_id, std::string detail) {
    if (!error_.has_value()) {
      error_ = Error(invariant_id, std::move(detail));
    }
    return false;
  }

  [[nodiscard]] const Phase4TrialWireError& error() const { return *error_; }

 private:
  [[nodiscard]] std::size_t remaining() const noexcept { return bytes_.size() - position_; }

  std::span<const std::uint8_t> bytes_;
  std::size_t position_ = 0;
  std::optional<Phase4TrialWireError> error_;
};

void EncodeExternalBudget(PayloadWriter& writer, const Phase4ExternalBudget& budget) {
  writer.AddU64(budget.maximum_prepared_elapsed_nanoseconds);
  writer.AddU64(budget.maximum_cold_elapsed_nanoseconds);
  writer.AddU64(budget.maximum_address_space_bytes);
  writer.AddU64(budget.maximum_peak_host_bytes);
}

[[nodiscard]] bool DecodeExternalBudget(PayloadReader& reader, Phase4ExternalBudget* budget) {
  return reader.ReadU64(&budget->maximum_prepared_elapsed_nanoseconds,
                        "maximum_prepared_elapsed_nanoseconds") &&
         reader.ReadU64(&budget->maximum_cold_elapsed_nanoseconds,
                        "maximum_cold_elapsed_nanoseconds") &&
         reader.ReadU64(&budget->maximum_address_space_bytes, "maximum_address_space_bytes") &&
         reader.ReadU64(&budget->maximum_peak_host_bytes, "maximum_peak_host_bytes");
}

void EncodeOpportunity(PayloadWriter& writer, const Phase4RouteOpportunity& opportunity) {
  writer.AddU64(opportunity.route_queries);
  writer.AddU64(opportunity.route_work_units);
}

[[nodiscard]] bool DecodeOpportunity(PayloadReader& reader, Phase4RouteOpportunity* opportunity,
                                     std::string_view prefix) {
  return reader.ReadU64(&opportunity->route_queries, std::string(prefix) + ".route_queries") &&
         reader.ReadU64(&opportunity->route_work_units, std::string(prefix) + ".route_work_units");
}

void EncodeBoardOutcome(PayloadWriter& writer, const Phase4BoardOutcome& outcome) {
  writer.AddU64(outcome.selected_net_count);
  writer.AddU64(outcome.no_candidate_net_count);
  writer.AddU64(outcome.overused_resource_count);
  writer.AddU64(outcome.total_overuse_units);
  writer.AddU64(outcome.total_intrinsic_cost);
  writer.AddU64(outcome.world_checksum);
}

[[nodiscard]] bool DecodeBoardOutcome(PayloadReader& reader, Phase4BoardOutcome* outcome) {
  return reader.ReadU64(&outcome->selected_net_count, "outcome.selected_net_count") &&
         reader.ReadU64(&outcome->no_candidate_net_count, "outcome.no_candidate_net_count") &&
         reader.ReadU64(&outcome->overused_resource_count, "outcome.overused_resource_count") &&
         reader.ReadU64(&outcome->total_overuse_units, "outcome.total_overuse_units") &&
         reader.ReadU64(&outcome->total_intrinsic_cost, "outcome.total_intrinsic_cost") &&
         reader.ReadU64(&outcome->world_checksum, "outcome.world_checksum");
}

void EncodeLifecycle(PayloadWriter& writer, const Phase4PreparerLifecycleObservation& lifecycle) {
  writer.AddU64(lifecycle.workers_started_before);
  writer.AddU64(lifecycle.workers_started_after);
  writer.AddU64(lifecycle.invocations_started_before);
  writer.AddU64(lifecycle.invocations_started_after);
  writer.AddU64(lifecycle.invocations_completed_before);
  writer.AddU64(lifecycle.invocations_completed_after);
}

[[nodiscard]] bool DecodeLifecycle(PayloadReader& reader,
                                   Phase4PreparerLifecycleObservation* lifecycle) {
  return reader.ReadU64(&lifecycle->workers_started_before, "lifecycle.workers_started_before") &&
         reader.ReadU64(&lifecycle->workers_started_after, "lifecycle.workers_started_after") &&
         reader.ReadU64(&lifecycle->invocations_started_before,
                        "lifecycle.invocations_started_before") &&
         reader.ReadU64(&lifecycle->invocations_started_after,
                        "lifecycle.invocations_started_after") &&
         reader.ReadU64(&lifecycle->invocations_completed_before,
                        "lifecycle.invocations_completed_before") &&
         reader.ReadU64(&lifecycle->invocations_completed_after,
                        "lifecycle.invocations_completed_after");
}

[[nodiscard]] std::optional<Phase4TrialWireError> ValidateExecution(
    const Phase4TrialArmExecution& execution) {
  const Phase4TrialArmSemantics& semantics = execution.semantics;
  if (semantics.schema_version != kPhase4PairedTrialSchemaVersion ||
      semantics.corpus_version != kPhase4RepresentativeCorpusVersion) {
    return Error("benchmark.phase4_trial_wire.success_schema.v1",
                 "success payload has an unsupported semantic or corpus schema");
  }
  if (!ValidArm(semantics.arm) || !ValidOrder(semantics.execution_order) ||
      !ValidTerminalReason(semantics.terminal_reason) ||
      !ValidOutcomeSource(semantics.candidate_outcome_source)) {
    return Error("benchmark.phase4_trial_wire.success_enum.v1",
                 "success payload contains an unknown enum value");
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<Phase4TrialWireError> ValidateFailure(
    const Phase4DurableArmFailure& failure) {
  if (failure.schema_version != kPhase4TrialHarnessSchemaVersion) {
    return Error("benchmark.phase4_trial_wire.failure_schema.v1",
                 "failure payload has an unsupported harness schema");
  }
  if (!ValidPairedErrorCode(failure.summary_code) || !ValidArm(failure.arm) ||
      !ValidFailurePayloadKind(failure.payload_kind) ||
      !ValidChildErrorCode(failure.payload_kind, failure.child_error_code)) {
    return Error("benchmark.phase4_trial_wire.failure_enum.v1",
                 "failure payload contains an unknown enum value");
  }
  if (failure.payload_kind == Phase4DurableFailurePayloadKind::kCorpus &&
      failure.child_bound_kind >
          static_cast<std::uint8_t>(Phase4RepresentativeWorkBound::kCompiledNodesAndHostBytes)) {
    return Error("benchmark.phase4_trial_wire.failure_enum.v1",
                 "failure payload contains an unknown corpus work-bound enum value");
  }

  const auto reject_state = [](std::string detail) {
    return Error("benchmark.phase4_trial_wire.failure_state.v1", std::move(detail));
  };
  if ((!failure.has_child_net &&
       (failure.child_net_id != 0 || failure.child_net_generation != 0)) ||
      (!failure.has_epoch_index && failure.epoch_index != 0)) {
    return reject_state("failure payload has noncanonical absent optional identity fields");
  }
  if (!failure.has_case_identity &&
      (failure.descriptor_fingerprint != 0 || failure.case_checksum != 0 ||
       failure.board_content_hash != 0 || failure.workload_checksum != 0 ||
       failure.capacity_model_checksum != 0 ||
       (failure.payload_kind != Phase4DurableFailurePayloadKind::kCorpus &&
        failure.case_id != 0))) {
    return reject_state("failure payload has noncanonical absent case-identity fields");
  }
  if (failure.has_case_identity &&
      (failure.case_id == 0 || failure.descriptor_fingerprint == 0 || failure.case_checksum == 0 ||
       failure.board_content_hash == 0 || failure.workload_checksum == 0 ||
       failure.capacity_model_checksum == 0)) {
    return reject_state("failure payload claims an incomplete case identity");
  }
  if (!failure.has_failed_observation &&
      (failure.failed_observation_checksum != 0 || failure.attempted_route_queries != 0 ||
       failure.attempted_route_work_units != 0 ||
       (failure.payload_kind != Phase4DurableFailurePayloadKind::kCorpus &&
        failure.attempted_column_count != 0))) {
    return reject_state("failure payload has noncanonical absent observation fields");
  }
  if (failure.has_failed_observation && failure.failed_observation_checksum == 0) {
    return reject_state("failure payload claims an observation without its checksum");
  }
  if (failure.has_failed_observation &&
      (failure.attempted_column_count != failure.attempted_route_queries ||
       static_cast<unsigned __int128>(failure.attempted_route_work_units) >
           static_cast<unsigned __int128>(failure.attempted_route_queries) *
               kPhase4CanonicalRouteWorkUnitsPerQueryV1)) {
    return reject_state("failure observation counters exceed canonical attempted-query work");
  }
  if (!failure.authoritative_candidate_store_present &&
      (failure.reconciled_candidate_count != 0 || failure.reconciled_rejection_count != 0 ||
       failure.reconciled_candidate_store_checksum != 0)) {
    return reject_state("failure payload has a roster without an authoritative candidate store");
  }
  if (failure.authoritative_candidate_store_present &&
      failure.reconciled_candidate_store_checksum == 0) {
    return reject_state("failure payload claims an authoritative store without its checksum");
  }
  if (failure.candidate_store_publication_committed &&
      !failure.authoritative_candidate_store_present) {
    return reject_state("failure payload claims publication without an authoritative store");
  }
  if ((failure.summary_code == Phase4PairedTrialErrorCode::kSequentialExecution &&
       failure.arm != Phase4TrialArm::kSequentialBaseline) ||
      ((failure.summary_code == Phase4PairedTrialErrorCode::kCandidatePreparation ||
        failure.summary_code == Phase4PairedTrialErrorCode::kCandidateSession) &&
       failure.arm != Phase4TrialArm::kReusableCandidateAllocation)) {
    return reject_state("failure summary code contradicts its arm");
  }

  const bool has_child_diagnostic =
      failure.child_error_code != 0 || !failure.child_invariant_id.empty() ||
      !failure.child_detail.empty() || failure.has_child_net || failure.child_required != 0 ||
      failure.child_configured != 0 || failure.child_bound_kind != 0 ||
      failure.child_secondary_required != 0 || failure.child_secondary_configured != 0;
  const bool has_case_fingerprint =
      failure.has_case_identity || failure.case_id != 0 || failure.descriptor_fingerprint != 0 ||
      failure.case_checksum != 0 || failure.board_content_hash != 0 ||
      failure.workload_checksum != 0 || failure.capacity_model_checksum != 0;
  const bool has_observation =
      failure.has_failed_observation || failure.failed_observation_checksum != 0 ||
      failure.attempted_column_count != 0 || failure.attempted_route_queries != 0 ||
      failure.attempted_route_work_units != 0;
  const bool has_store =
      failure.candidate_store_publication_committed ||
      failure.authoritative_candidate_store_present || failure.reconciled_candidate_count != 0 ||
      failure.reconciled_rejection_count != 0 || failure.reconciled_candidate_store_checksum != 0;
  switch (failure.payload_kind) {
    case Phase4DurableFailurePayloadKind::kSummaryOnly:
      if (failure.summary_code == Phase4PairedTrialErrorCode::kCaseBuild ||
          failure.summary_code == Phase4PairedTrialErrorCode::kSequentialExecution ||
          failure.summary_code == Phase4PairedTrialErrorCode::kCandidatePreparation ||
          failure.summary_code == Phase4PairedTrialErrorCode::kCandidateSession ||
          failure.summary_code == Phase4PairedTrialErrorCode::kMeasurementAssociation ||
          failure.summary_code == Phase4PairedTrialErrorCode::kExternalBudgetExceeded ||
          failure.summary_code == Phase4PairedTrialErrorCode::kPairMismatch ||
          has_child_diagnostic || has_case_fingerprint || failure.has_epoch_index ||
          failure.epoch_index != 0 || has_observation || has_store) {
        return reject_state("summary-only failure contains typed child state");
      }
      break;
    case Phase4DurableFailurePayloadKind::kCorpus: {
      const bool work_bound =
          failure.child_error_code ==
          static_cast<std::uint8_t>(Phase4RepresentativeCorpusErrorCode::kWorkBoundExceeded);
      const auto bound = static_cast<Phase4RepresentativeWorkBound>(failure.child_bound_kind);
      const bool node_witness = failure.child_required > failure.child_configured;
      const bool host_witness =
          failure.child_secondary_required > failure.child_secondary_configured;
      const bool valid_work_bound_witness =
          (bound == Phase4RepresentativeWorkBound::kCompiledNodes && node_witness) ||
          (bound == Phase4RepresentativeWorkBound::kCompiledHostBytes && host_witness) ||
          (bound == Phase4RepresentativeWorkBound::kCompiledNodesAndHostBytes && node_witness &&
           host_witness);
      if (failure.summary_code != Phase4PairedTrialErrorCode::kCaseBuild ||
          failure.has_case_identity || failure.has_epoch_index || failure.has_failed_observation ||
          failure.attempted_route_queries != 0 || failure.attempted_route_work_units != 0 ||
          has_store ||
          (work_bound &&
           (!failure.has_child_net || failure.child_net_id == 0 || !valid_work_bound_witness)) ||
          (!work_bound &&
           (failure.has_child_net || failure.child_required != 0 || failure.child_configured != 0 ||
            failure.child_bound_kind != 0 || failure.child_secondary_required != 0 ||
            failure.child_secondary_configured != 0 || failure.attempted_column_count != 0))) {
        return reject_state("corpus failure contradicts its bound-witness state");
      }
      break;
    }
    case Phase4DurableFailurePayloadKind::kSequential:
      if (failure.summary_code != Phase4PairedTrialErrorCode::kSequentialExecution ||
          failure.arm != Phase4TrialArm::kSequentialBaseline || !failure.has_case_identity ||
          failure.child_bound_kind != 0 || failure.child_secondary_required != 0 ||
          failure.child_secondary_configured != 0 || failure.has_epoch_index || has_observation ||
          has_store) {
        return reject_state("sequential failure contains state belonging to another payload kind");
      }
      break;
    case Phase4DurableFailurePayloadKind::kCandidatePreparation:
      if (failure.summary_code != Phase4PairedTrialErrorCode::kCandidatePreparation ||
          failure.arm != Phase4TrialArm::kReusableCandidateAllocation ||
          !failure.has_case_identity || failure.child_bound_kind != 0 ||
          failure.child_secondary_required != 0 || failure.child_secondary_configured != 0 ||
          failure.has_epoch_index ||
          (failure.has_failed_observation && failure.candidate_store_publication_committed !=
                                                 failure.authoritative_candidate_store_present) ||
          (!failure.has_failed_observation && has_store)) {
        return reject_state(
            "candidate-preparation failure contradicts observation or store ownership");
      }
      break;
    case Phase4DurableFailurePayloadKind::kCandidateSession:
      if (failure.summary_code != Phase4PairedTrialErrorCode::kCandidateSession ||
          failure.arm != Phase4TrialArm::kReusableCandidateAllocation ||
          !failure.has_case_identity || failure.has_child_net || failure.child_required != 0 ||
          failure.child_configured != 0 || failure.child_bound_kind != 0 ||
          failure.child_secondary_required != 0 || failure.child_secondary_configured != 0 ||
          !failure.authoritative_candidate_store_present) {
        return reject_state("candidate-session failure contradicts child or store ownership");
      }
      break;
  }
  if (failure.payload_checksum == 0 ||
      failure.payload_checksum != ComputePhase4DurableArmFailureChecksumV1(failure)) {
    return Error("benchmark.phase4_trial_wire.failure_checksum.v1",
                 "failure payload checksum is absent or invalid");
  }
  return std::nullopt;
}

void EncodeExecution(PayloadWriter& writer, const Phase4TrialArmExecution& execution) {
  const Phase4TrialArmSemantics& semantics = execution.semantics;
  writer.AddU32(semantics.schema_version);
  writer.AddByte(static_cast<std::uint8_t>(semantics.arm));
  writer.AddByte(static_cast<std::uint8_t>(semantics.execution_order));
  writer.AddU32(semantics.corpus_version);
  writer.AddU64(semantics.corpus_checksum);
  writer.AddU32(semantics.case_id);
  writer.AddU64(semantics.descriptor_fingerprint);
  writer.AddU64(semantics.case_checksum);
  writer.AddU64(semantics.board_content_hash);
  writer.AddU64(semantics.workload_checksum);
  writer.AddU64(semantics.capacity_model_checksum);
  writer.AddU64(semantics.budget_checksum);
  writer.AddU32(semantics.workload_net_count);
  writer.AddU32(semantics.requested_pool_size);
  writer.AddU32(semantics.repetition_index);
  writer.AddU64(semantics.root_seed);
  writer.AddU32(semantics.preparation_worker_count);
  writer.AddU32(semantics.baseline_sweeps);
  writer.AddU32(semantics.candidate_regeneration_epochs);
  writer.AddU64(semantics.candidate_columns_per_epoch);
  writer.AddU32(semantics.candidate_terminal_selection_rounds);
  EncodeExternalBudget(writer, semantics.external_budget);
  EncodeOpportunity(writer, semantics.opportunity);
  EncodeOpportunity(writer, semantics.actual);
  writer.AddU64(semantics.preparation_route_queries);
  writer.AddU64(semantics.preparation_route_work_units);
  writer.AddU64(semantics.regeneration_route_queries);
  writer.AddU64(semantics.regeneration_route_work_units);
  writer.AddU64(semantics.requested_columns);
  writer.AddU64(semantics.admitted_candidates);
  writer.AddU64(semantics.rejected_columns);
  writer.AddU64(semantics.final_candidate_count);
  writer.AddU64(semantics.preparation_checksum);
  writer.AddU64(semantics.algorithm_session_checksum);
  writer.AddU64(semantics.final_pool_manifest_checksum);
  writer.AddU64(semantics.final_rejection_manifest_checksum);
  writer.AddByte(static_cast<std::uint8_t>(semantics.terminal_reason));
  writer.AddByte(static_cast<std::uint8_t>(semantics.candidate_outcome_source));
  EncodeBoardOutcome(writer, semantics.outcome);
  writer.AddU64(semantics.semantic_checksum);
  writer.AddU64(execution.case_build_elapsed_nanoseconds);
  writer.AddU64(execution.prepared_elapsed_nanoseconds);
  writer.AddU64(execution.cold_elapsed_nanoseconds);
  EncodeLifecycle(writer, execution.preparer_lifecycle);
}

[[nodiscard]] bool DecodeExecution(PayloadReader& reader, Phase4TrialArmExecution* execution) {
  Phase4TrialArmSemantics& semantics = execution->semantics;
  std::uint8_t arm = 0;
  std::uint8_t order = 0;
  std::uint8_t reason = 0;
  std::uint8_t source = 0;
  if (!reader.ReadU32(&semantics.schema_version, "semantics.schema_version") ||
      !reader.ReadByte(&arm, "semantics.arm") ||
      !reader.ReadByte(&order, "semantics.execution_order") ||
      !reader.ReadU32(&semantics.corpus_version, "semantics.corpus_version") ||
      !reader.ReadU64(&semantics.corpus_checksum, "semantics.corpus_checksum") ||
      !reader.ReadU32(&semantics.case_id, "semantics.case_id") ||
      !reader.ReadU64(&semantics.descriptor_fingerprint, "semantics.descriptor_fingerprint") ||
      !reader.ReadU64(&semantics.case_checksum, "semantics.case_checksum") ||
      !reader.ReadU64(&semantics.board_content_hash, "semantics.board_content_hash") ||
      !reader.ReadU64(&semantics.workload_checksum, "semantics.workload_checksum") ||
      !reader.ReadU64(&semantics.capacity_model_checksum, "semantics.capacity_model_checksum") ||
      !reader.ReadU64(&semantics.budget_checksum, "semantics.budget_checksum") ||
      !reader.ReadU32(&semantics.workload_net_count, "semantics.workload_net_count") ||
      !reader.ReadU32(&semantics.requested_pool_size, "semantics.requested_pool_size") ||
      !reader.ReadU32(&semantics.repetition_index, "semantics.repetition_index") ||
      !reader.ReadU64(&semantics.root_seed, "semantics.root_seed") ||
      !reader.ReadU32(&semantics.preparation_worker_count, "semantics.preparation_worker_count") ||
      !reader.ReadU32(&semantics.baseline_sweeps, "semantics.baseline_sweeps") ||
      !reader.ReadU32(&semantics.candidate_regeneration_epochs,
                      "semantics.candidate_regeneration_epochs") ||
      !reader.ReadU64(&semantics.candidate_columns_per_epoch,
                      "semantics.candidate_columns_per_epoch") ||
      !reader.ReadU32(&semantics.candidate_terminal_selection_rounds,
                      "semantics.candidate_terminal_selection_rounds") ||
      !DecodeExternalBudget(reader, &semantics.external_budget) ||
      !DecodeOpportunity(reader, &semantics.opportunity, "semantics.opportunity") ||
      !DecodeOpportunity(reader, &semantics.actual, "semantics.actual") ||
      !reader.ReadU64(&semantics.preparation_route_queries,
                      "semantics.preparation_route_queries") ||
      !reader.ReadU64(&semantics.preparation_route_work_units,
                      "semantics.preparation_route_work_units") ||
      !reader.ReadU64(&semantics.regeneration_route_queries,
                      "semantics.regeneration_route_queries") ||
      !reader.ReadU64(&semantics.regeneration_route_work_units,
                      "semantics.regeneration_route_work_units") ||
      !reader.ReadU64(&semantics.requested_columns, "semantics.requested_columns") ||
      !reader.ReadU64(&semantics.admitted_candidates, "semantics.admitted_candidates") ||
      !reader.ReadU64(&semantics.rejected_columns, "semantics.rejected_columns") ||
      !reader.ReadU64(&semantics.final_candidate_count, "semantics.final_candidate_count") ||
      !reader.ReadU64(&semantics.preparation_checksum, "semantics.preparation_checksum") ||
      !reader.ReadU64(&semantics.algorithm_session_checksum,
                      "semantics.algorithm_session_checksum") ||
      !reader.ReadU64(&semantics.final_pool_manifest_checksum,
                      "semantics.final_pool_manifest_checksum") ||
      !reader.ReadU64(&semantics.final_rejection_manifest_checksum,
                      "semantics.final_rejection_manifest_checksum") ||
      !reader.ReadByte(&reason, "semantics.terminal_reason") ||
      !reader.ReadByte(&source, "semantics.candidate_outcome_source") ||
      !DecodeBoardOutcome(reader, &semantics.outcome) ||
      !reader.ReadU64(&semantics.semantic_checksum, "semantics.semantic_checksum") ||
      !reader.ReadU64(&execution->case_build_elapsed_nanoseconds,
                      "case_build_elapsed_nanoseconds") ||
      !reader.ReadU64(&execution->prepared_elapsed_nanoseconds, "prepared_elapsed_nanoseconds") ||
      !reader.ReadU64(&execution->cold_elapsed_nanoseconds, "cold_elapsed_nanoseconds") ||
      !DecodeLifecycle(reader, &execution->preparer_lifecycle)) {
    return false;
  }
  semantics.arm = static_cast<Phase4TrialArm>(arm);
  semantics.execution_order = static_cast<Phase4TrialOrder>(order);
  semantics.terminal_reason = static_cast<Phase4NormalizedTerminalReason>(reason);
  semantics.candidate_outcome_source = static_cast<Phase4CandidateOutcomeSource>(source);
  if (std::optional<Phase4TrialWireError> error = ValidateExecution(*execution);
      error.has_value()) {
    return reader.Reject(error->invariant_id, error->detail);
  }
  return true;
}

[[nodiscard]] bool EncodeFailure(PayloadWriter& writer, const Phase4DurableArmFailure& failure) {
  writer.AddU32(failure.schema_version);
  writer.AddByte(static_cast<std::uint8_t>(failure.summary_code));
  writer.AddByte(static_cast<std::uint8_t>(failure.arm));
  writer.AddU64(failure.summary_required);
  writer.AddU64(failure.summary_configured);
  if (!writer.AddString(failure.summary_invariant_id) ||
      !writer.AddString(failure.summary_detail)) {
    return false;
  }
  writer.AddByte(static_cast<std::uint8_t>(failure.payload_kind));
  writer.AddByte(failure.child_error_code);
  if (!writer.AddString(failure.child_invariant_id) || !writer.AddString(failure.child_detail)) {
    return false;
  }
  writer.AddBool(failure.has_child_net);
  writer.AddU64(failure.child_net_id);
  writer.AddU32(failure.child_net_generation);
  writer.AddU64(failure.child_required);
  writer.AddU64(failure.child_configured);
  writer.AddByte(failure.child_bound_kind);
  writer.AddU64(failure.child_secondary_required);
  writer.AddU64(failure.child_secondary_configured);
  writer.AddBool(failure.has_case_identity);
  writer.AddU32(failure.case_id);
  writer.AddU64(failure.descriptor_fingerprint);
  writer.AddU64(failure.case_checksum);
  writer.AddU64(failure.board_content_hash);
  writer.AddU64(failure.workload_checksum);
  writer.AddU64(failure.capacity_model_checksum);
  writer.AddBool(failure.has_epoch_index);
  writer.AddU32(failure.epoch_index);
  writer.AddBool(failure.has_failed_observation);
  writer.AddU64(failure.failed_observation_checksum);
  writer.AddU64(failure.attempted_column_count);
  writer.AddU64(failure.attempted_route_queries);
  writer.AddU64(failure.attempted_route_work_units);
  writer.AddBool(failure.candidate_store_publication_committed);
  writer.AddBool(failure.authoritative_candidate_store_present);
  writer.AddU64(failure.reconciled_candidate_count);
  writer.AddU64(failure.reconciled_rejection_count);
  writer.AddU64(failure.reconciled_candidate_store_checksum);
  writer.AddU64(failure.payload_checksum);
  return true;
}

[[nodiscard]] bool DecodeFailure(PayloadReader& reader, Phase4DurableArmFailure* failure) {
  std::uint8_t summary_code = 0;
  std::uint8_t arm = 0;
  std::uint8_t payload_kind = 0;
  if (!reader.ReadU32(&failure->schema_version, "failure.schema_version") ||
      !reader.ReadByte(&summary_code, "failure.summary_code") ||
      !reader.ReadByte(&arm, "failure.arm") ||
      !reader.ReadU64(&failure->summary_required, "failure.summary_required") ||
      !reader.ReadU64(&failure->summary_configured, "failure.summary_configured") ||
      !reader.ReadString(&failure->summary_invariant_id, "failure.summary_invariant_id") ||
      !reader.ReadString(&failure->summary_detail, "failure.summary_detail") ||
      !reader.ReadByte(&payload_kind, "failure.payload_kind") ||
      !reader.ReadByte(&failure->child_error_code, "failure.child_error_code") ||
      !reader.ReadString(&failure->child_invariant_id, "failure.child_invariant_id") ||
      !reader.ReadString(&failure->child_detail, "failure.child_detail") ||
      !reader.ReadBool(&failure->has_child_net, "failure.has_child_net") ||
      !reader.ReadU64(&failure->child_net_id, "failure.child_net_id") ||
      !reader.ReadU32(&failure->child_net_generation, "failure.child_net_generation") ||
      !reader.ReadU64(&failure->child_required, "failure.child_required") ||
      !reader.ReadU64(&failure->child_configured, "failure.child_configured") ||
      !reader.ReadByte(&failure->child_bound_kind, "failure.child_bound_kind") ||
      !reader.ReadU64(&failure->child_secondary_required, "failure.child_secondary_required") ||
      !reader.ReadU64(&failure->child_secondary_configured, "failure.child_secondary_configured") ||
      !reader.ReadBool(&failure->has_case_identity, "failure.has_case_identity") ||
      !reader.ReadU32(&failure->case_id, "failure.case_id") ||
      !reader.ReadU64(&failure->descriptor_fingerprint, "failure.descriptor_fingerprint") ||
      !reader.ReadU64(&failure->case_checksum, "failure.case_checksum") ||
      !reader.ReadU64(&failure->board_content_hash, "failure.board_content_hash") ||
      !reader.ReadU64(&failure->workload_checksum, "failure.workload_checksum") ||
      !reader.ReadU64(&failure->capacity_model_checksum, "failure.capacity_model_checksum") ||
      !reader.ReadBool(&failure->has_epoch_index, "failure.has_epoch_index") ||
      !reader.ReadU32(&failure->epoch_index, "failure.epoch_index") ||
      !reader.ReadBool(&failure->has_failed_observation, "failure.has_failed_observation") ||
      !reader.ReadU64(&failure->failed_observation_checksum,
                      "failure.failed_observation_checksum") ||
      !reader.ReadU64(&failure->attempted_column_count, "failure.attempted_column_count") ||
      !reader.ReadU64(&failure->attempted_route_queries, "failure.attempted_route_queries") ||
      !reader.ReadU64(&failure->attempted_route_work_units, "failure.attempted_route_work_units") ||
      !reader.ReadBool(&failure->candidate_store_publication_committed,
                       "failure.candidate_store_publication_committed") ||
      !reader.ReadBool(&failure->authoritative_candidate_store_present,
                       "failure.authoritative_candidate_store_present") ||
      !reader.ReadU64(&failure->reconciled_candidate_count, "failure.reconciled_candidate_count") ||
      !reader.ReadU64(&failure->reconciled_rejection_count, "failure.reconciled_rejection_count") ||
      !reader.ReadU64(&failure->reconciled_candidate_store_checksum,
                      "failure.reconciled_candidate_store_checksum") ||
      !reader.ReadU64(&failure->payload_checksum, "failure.payload_checksum")) {
    return false;
  }
  failure->summary_code = static_cast<Phase4PairedTrialErrorCode>(summary_code);
  failure->arm = static_cast<Phase4TrialArm>(arm);
  failure->payload_kind = static_cast<Phase4DurableFailurePayloadKind>(payload_kind);
  if (std::optional<Phase4TrialWireError> error = ValidateFailure(*failure); error.has_value()) {
    return reader.Reject(error->invariant_id, error->detail);
  }
  return true;
}

[[nodiscard]] Phase4TrialWireMessageKind MessageKind(const Phase4TrialWireMessage& message) {
  return std::visit(
      []<typename Message>(const Message&) {
        if constexpr (std::is_same_v<Message, Phase4TrialWireReady>) {
          return Phase4TrialWireMessageKind::kReady;
        } else if constexpr (std::is_same_v<Message, Phase4TrialWireRunCommand>) {
          return Phase4TrialWireMessageKind::kRun;
        } else if constexpr (std::is_same_v<Message, Phase4TrialWireStop>) {
          return Phase4TrialWireMessageKind::kStop;
        } else if constexpr (std::is_same_v<Message, Phase4TrialWireSuccess>) {
          return Phase4TrialWireMessageKind::kSuccess;
        } else if constexpr (std::is_same_v<Message, Phase4TrialWireFailure>) {
          return Phase4TrialWireMessageKind::kFailure;
        } else {
          return Phase4TrialWireMessageKind::kStopped;
        }
      },
      message);
}

[[nodiscard]] std::variant<std::vector<std::uint8_t>, Phase4TrialWireError> EncodePayload(
    const Phase4TrialWireMessage& message) {
  PayloadWriter writer;
  const std::optional<Phase4TrialWireError> validation = std::visit(
      [](const auto& value) -> std::optional<Phase4TrialWireError> {
        using Message = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<Message, Phase4TrialWireReady>) {
          return ValidateReady(value);
        } else if constexpr (std::is_same_v<Message, Phase4TrialWireRunCommand>) {
          if (!ValidOrder(value.execution_order)) {
            return Error("benchmark.phase4_trial_wire.run_enum.v1",
                         "run command contains an unknown execution order");
          }
        } else if constexpr (std::is_same_v<Message, Phase4TrialWireSuccess>) {
          return ValidateExecution(value.execution);
        } else if constexpr (std::is_same_v<Message, Phase4TrialWireFailure>) {
          return ValidateFailure(value.failure);
        }
        return std::nullopt;
      },
      message);
  if (validation.has_value()) {
    return *validation;
  }

  const bool encoded = std::visit(
      [&writer](const auto& value) {
        using Message = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<Message, Phase4TrialWireReady>) {
          writer.AddU32(value.case_id);
          writer.AddU64(value.descriptor_fingerprint);
          writer.AddU64(value.case_checksum);
          writer.AddU64(value.board_content_hash);
          writer.AddU64(value.workload_checksum);
          writer.AddU64(value.capacity_model_checksum);
          return true;
        } else if constexpr (std::is_same_v<Message, Phase4TrialWireStop> ||
                             std::is_same_v<Message, Phase4TrialWireStopped>) {
          return true;
        } else if constexpr (std::is_same_v<Message, Phase4TrialWireRunCommand>) {
          writer.AddU32(value.repetition_index);
          writer.AddByte(static_cast<std::uint8_t>(value.execution_order));
          return true;
        } else if constexpr (std::is_same_v<Message, Phase4TrialWireSuccess>) {
          EncodeExecution(writer, value.execution);
          return true;
        } else {
          return EncodeFailure(writer, value.failure);
        }
      },
      message);
  if (!encoded) {
    return Error("benchmark.phase4_trial_wire.string_bound.v1",
                 "wire string exceeds the bounded payload representation");
  }
  if (writer.bytes().size() > kPhase4TrialWireMaximumPayloadBytesV1) {
    return Error("benchmark.phase4_trial_wire.payload_bound.v1",
                 "encoded wire payload exceeds 64 KiB");
  }
  return std::move(writer).Take();
}

[[nodiscard]] Phase4TrialWireDecodeResult DecodePayload(Phase4TrialWireMessageKind kind,
                                                        std::span<const std::uint8_t> payload) {
  PayloadReader reader(payload);
  Phase4TrialWireMessage message;
  switch (kind) {
    case Phase4TrialWireMessageKind::kReady: {
      Phase4TrialWireReady ready;
      if (!reader.ReadU32(&ready.case_id, "ready.case_id") ||
          !reader.ReadU64(&ready.descriptor_fingerprint, "ready.descriptor_fingerprint") ||
          !reader.ReadU64(&ready.case_checksum, "ready.case_checksum") ||
          !reader.ReadU64(&ready.board_content_hash, "ready.board_content_hash") ||
          !reader.ReadU64(&ready.workload_checksum, "ready.workload_checksum") ||
          !reader.ReadU64(&ready.capacity_model_checksum, "ready.capacity_model_checksum")) {
        return reader.error();
      }
      if (std::optional<Phase4TrialWireError> error = ValidateReady(ready); error.has_value()) {
        return *error;
      }
      message = ready;
      break;
    }
    case Phase4TrialWireMessageKind::kRun: {
      Phase4TrialWireRunCommand run;
      std::uint8_t order = 0;
      if (!reader.ReadU32(&run.repetition_index, "run.repetition_index") ||
          !reader.ReadByte(&order, "run.execution_order")) {
        return reader.error();
      }
      run.execution_order = static_cast<Phase4TrialOrder>(order);
      if (!ValidOrder(run.execution_order)) {
        return Error("benchmark.phase4_trial_wire.run_enum.v1",
                     "run command contains an unknown execution order");
      }
      message = run;
      break;
    }
    case Phase4TrialWireMessageKind::kStop:
      message = Phase4TrialWireStop{};
      break;
    case Phase4TrialWireMessageKind::kSuccess: {
      Phase4TrialWireSuccess success;
      if (!DecodeExecution(reader, &success.execution)) {
        return reader.error();
      }
      message = std::move(success);
      break;
    }
    case Phase4TrialWireMessageKind::kFailure: {
      Phase4TrialWireFailure failure;
      if (!DecodeFailure(reader, &failure.failure)) {
        return reader.error();
      }
      message = std::move(failure);
      break;
    }
    case Phase4TrialWireMessageKind::kStopped:
      message = Phase4TrialWireStopped{};
      break;
  }
  if (!reader.Finish()) {
    return reader.error();
  }
  return message;
}

[[nodiscard]] std::optional<Phase4TrialWireError> ValidateHeader(
    std::span<const std::uint8_t> header, Phase4TrialWireMessageKind* kind,
    std::uint32_t* payload_length) {
  if (header.size() < kPhase4TrialWireHeaderBytesV1) {
    return Error("benchmark.phase4_trial_wire.truncated.v1",
                 "wire frame is shorter than its fixed header");
  }
  if (!std::equal(kMagic.begin(), kMagic.end(), header.begin())) {
    return Error("benchmark.phase4_trial_wire.magic.v1", "wire frame magic is invalid");
  }
  std::size_t offset = kMagic.size();
  const std::uint32_t schema = LoadU32(header, offset);
  offset += kSchemaBytes;
  if (schema != kPhase4TrialWireSchemaVersion) {
    return Error("benchmark.phase4_trial_wire.schema.v1", "wire frame schema is unsupported");
  }
  *kind = static_cast<Phase4TrialWireMessageKind>(header[offset]);
  offset += kKindBytes;
  if (!ValidMessageKind(*kind)) {
    return Error("benchmark.phase4_trial_wire.kind.v1",
                 "wire frame contains an unknown message kind");
  }
  *payload_length = LoadU32(header, offset);
  if (*payload_length > kPhase4TrialWireMaximumPayloadBytesV1) {
    return Error("benchmark.phase4_trial_wire.payload_bound.v1",
                 "wire frame declares a payload larger than 64 KiB");
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<Phase4TrialWireError> ReadExact(int descriptor,
                                                            std::span<std::uint8_t> bytes,
                                                            std::string_view portion) {
  std::size_t offset = 0;
  while (offset < bytes.size()) {
    const ssize_t count = ::read(descriptor, bytes.data() + offset, bytes.size() - offset);
    if (count > 0) {
      offset += static_cast<std::size_t>(count);
      continue;
    }
    if (count == 0) {
      return Error("benchmark.phase4_trial_wire.io_truncated.v1",
                   "descriptor reached EOF while reading " + std::string(portion));
    }
    if (errno == EINTR) {
      continue;
    }
    const int read_error = errno;
    return Error("benchmark.phase4_trial_wire.io_read.v1",
                 "descriptor read failed for " + std::string(portion) + ": " +
                     std::error_code(read_error, std::generic_category()).message());
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<Phase4TrialWireError> WriteExact(int descriptor,
                                                             std::span<const std::uint8_t> bytes) {
  std::size_t offset = 0;
  while (offset < bytes.size()) {
    const ssize_t count =
        ::send(descriptor, bytes.data() + offset, bytes.size() - offset, MSG_NOSIGNAL);
    if (count > 0) {
      offset += static_cast<std::size_t>(count);
      continue;
    }
    if (count == 0) {
      return Error("benchmark.phase4_trial_wire.io_write.v1", "descriptor write made no progress");
    }
    if (errno == EINTR) {
      continue;
    }
    const int write_error = errno;
    return Error("benchmark.phase4_trial_wire.io_write.v1",
                 "descriptor write failed: " +
                     std::error_code(write_error, std::generic_category()).message());
  }
  return std::nullopt;
}

}  // namespace

Phase4TrialWireExpectedFrameSizeResult Phase4TrialWireExpectedFrameSizeV1(
    std::span<const std::uint8_t> prefix) {
  try {
    if (prefix.size() < kPhase4TrialWireHeaderBytesV1) {
      return std::optional<std::size_t>{};
    }
    Phase4TrialWireMessageKind kind = Phase4TrialWireMessageKind::kReady;
    std::uint32_t payload_length = 0;
    if (std::optional<Phase4TrialWireError> error =
            ValidateHeader(prefix.first(kPhase4TrialWireHeaderBytesV1), &kind, &payload_length);
        error.has_value()) {
      return *error;
    }
    return std::optional<std::size_t>{kPhase4TrialWireHeaderBytesV1 +
                                      static_cast<std::size_t>(payload_length) + kChecksumBytes};
  } catch (const std::bad_alloc&) {
    return ResourceError("host allocation failed while inspecting a wire frame header");
  } catch (const std::length_error&) {
    return ResourceError("host container length failed while inspecting a wire frame header");
  }
}

Phase4TrialWireEncodeResult EncodePhase4TrialWireMessageV1(const Phase4TrialWireMessage& message) {
  try {
    auto payload_result = EncodePayload(message);
    if (std::holds_alternative<Phase4TrialWireError>(payload_result)) {
      return std::get<Phase4TrialWireError>(std::move(payload_result));
    }
    std::vector<std::uint8_t> payload =
        std::get<std::vector<std::uint8_t>>(std::move(payload_result));
    std::vector<std::uint8_t> frame;
    frame.reserve(kPhase4TrialWireHeaderBytesV1 + payload.size() + kChecksumBytes);
    frame.insert(frame.end(), kMagic.begin(), kMagic.end());
    AppendU32(frame, kPhase4TrialWireSchemaVersion);
    AppendByte(frame, static_cast<std::uint8_t>(MessageKind(message)));
    AppendU32(frame, static_cast<std::uint32_t>(payload.size()));
    frame.insert(frame.end(), payload.begin(), payload.end());
    const std::uint64_t checksum = Fnv1a(frame);
    AppendU64(frame, checksum);
    return frame;
  } catch (const std::bad_alloc&) {
    return ResourceError("host allocation failed while encoding a wire frame");
  } catch (const std::length_error&) {
    return ResourceError("host container length failed while encoding a wire frame");
  }
}

Phase4TrialWireDecodeResult DecodePhase4TrialWireMessageV1(std::span<const std::uint8_t> frame) {
  try {
    if (frame.size() < kPhase4TrialWireHeaderBytesV1) {
      return Error("benchmark.phase4_trial_wire.truncated.v1",
                   "wire frame is shorter than its fixed header");
    }
    Phase4TrialWireMessageKind kind = Phase4TrialWireMessageKind::kReady;
    std::uint32_t payload_length = 0;
    if (std::optional<Phase4TrialWireError> error =
            ValidateHeader(frame.first(kPhase4TrialWireHeaderBytesV1), &kind, &payload_length);
        error.has_value()) {
      return *error;
    }
    const std::size_t expected_size =
        kPhase4TrialWireHeaderBytesV1 + static_cast<std::size_t>(payload_length) + kChecksumBytes;
    if (frame.size() < expected_size) {
      return Error("benchmark.phase4_trial_wire.truncated.v1",
                   "wire frame is shorter than its declared payload");
    }
    if (frame.size() > expected_size) {
      return Error("benchmark.phase4_trial_wire.trailing.v1",
                   "wire frame has bytes trailing its declared checksum");
    }
    const std::size_t checksum_offset = expected_size - kChecksumBytes;
    const std::uint64_t recorded_checksum = LoadU64(frame, checksum_offset);
    const std::uint64_t computed_checksum = Fnv1a(frame.first(checksum_offset));
    if (recorded_checksum != computed_checksum) {
      return Error("benchmark.phase4_trial_wire.checksum.v1",
                   "wire frame FNV-1a checksum does not match its envelope and payload");
    }
    return DecodePayload(kind, frame.subspan(kPhase4TrialWireHeaderBytesV1, payload_length));
  } catch (const std::bad_alloc&) {
    return ResourceError("host allocation failed while decoding a wire frame");
  } catch (const std::length_error&) {
    return ResourceError("host container length failed while decoding a wire frame");
  }
}

Phase4TrialWireDecodeResult ReadPhase4TrialWireMessageV1(int descriptor) {
  try {
    if (descriptor < 0) {
      return Error("benchmark.phase4_trial_wire.io_descriptor.v1",
                   "wire read descriptor is negative");
    }
    std::array<std::uint8_t, kPhase4TrialWireHeaderBytesV1> header{};
    if (std::optional<Phase4TrialWireError> error = ReadExact(descriptor, header, "wire header");
        error.has_value()) {
      return *error;
    }
    Phase4TrialWireMessageKind kind = Phase4TrialWireMessageKind::kReady;
    std::uint32_t payload_length = 0;
    if (std::optional<Phase4TrialWireError> error = ValidateHeader(header, &kind, &payload_length);
        error.has_value()) {
      return *error;
    }
    const std::size_t frame_size =
        kPhase4TrialWireHeaderBytesV1 + static_cast<std::size_t>(payload_length) + kChecksumBytes;
    std::vector<std::uint8_t> frame(frame_size);
    std::copy(header.begin(), header.end(), frame.begin());
    if (std::optional<Phase4TrialWireError> error =
            ReadExact(descriptor, std::span(frame).subspan(kPhase4TrialWireHeaderBytesV1),
                      "wire payload and checksum");
        error.has_value()) {
      return *error;
    }
    return DecodePhase4TrialWireMessageV1(frame);
  } catch (const std::bad_alloc&) {
    return ResourceError("host allocation failed while reading a wire frame");
  } catch (const std::length_error&) {
    return ResourceError("host container length failed while reading a wire frame");
  }
}

Phase4TrialWireWriteResult WritePhase4TrialWireMessageV1(int descriptor,
                                                         const Phase4TrialWireMessage& message) {
  if (descriptor < 0) {
    return Error("benchmark.phase4_trial_wire.io_descriptor.v1",
                 "wire write descriptor is negative");
  }
  Phase4TrialWireEncodeResult encoded = EncodePhase4TrialWireMessageV1(message);
  if (std::holds_alternative<Phase4TrialWireError>(encoded)) {
    return std::get<Phase4TrialWireError>(std::move(encoded));
  }
  std::vector<std::uint8_t> frame = std::get<std::vector<std::uint8_t>>(std::move(encoded));
  if (std::optional<Phase4TrialWireError> error = WriteExact(descriptor, frame);
      error.has_value()) {
    return *error;
  }
  return std::monostate{};
}

}  // namespace apgar::benchmark::internal
