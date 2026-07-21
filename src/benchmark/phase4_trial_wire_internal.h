#ifndef APGAR_SRC_BENCHMARK_PHASE4_TRIAL_WIRE_INTERNAL_H_
#define APGAR_SRC_BENCHMARK_PHASE4_TRIAL_WIRE_INTERNAL_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

#include "apgar/benchmark/phase4_trial_harness.h"

namespace apgar::benchmark::internal {

inline constexpr std::size_t kPhase4TrialWireMaximumPayloadBytesV1 = 64U * 1024U;
inline constexpr std::size_t kPhase4TrialWireHeaderBytesV1 =
    8U + sizeof(std::uint32_t) + sizeof(std::uint8_t) + sizeof(std::uint32_t);
inline constexpr std::size_t kPhase4TrialWireMaxFrameBytesV1 =
    kPhase4TrialWireHeaderBytesV1 + kPhase4TrialWireMaximumPayloadBytesV1 + sizeof(std::uint64_t);

enum class Phase4TrialWireMessageKind : std::uint8_t {
  kReady = 0,
  kRun = 1,
  kStop = 2,
  kSuccess = 3,
  kFailure = 4,
  kStopped = 5,
};

struct Phase4TrialWireReady {
  std::uint32_t case_id = 0;
  std::uint64_t descriptor_fingerprint = 0;
  std::uint64_t case_checksum = 0;
  std::uint64_t board_content_hash = 0;
  std::uint64_t workload_checksum = 0;
  std::uint64_t capacity_model_checksum = 0;

  friend bool operator==(const Phase4TrialWireReady&, const Phase4TrialWireReady&) = default;
};

struct Phase4TrialWireRunCommand {
  std::uint32_t repetition_index = 0;
  Phase4TrialOrder execution_order = Phase4TrialOrder::kBaselineFirst;

  friend bool operator==(const Phase4TrialWireRunCommand&,
                         const Phase4TrialWireRunCommand&) = default;
};

struct Phase4TrialWireStop {
  friend bool operator==(const Phase4TrialWireStop&, const Phase4TrialWireStop&) = default;
};

struct Phase4TrialWireStopped {
  friend bool operator==(const Phase4TrialWireStopped&, const Phase4TrialWireStopped&) = default;
};

struct Phase4TrialWireSuccess {
  Phase4TrialArmExecution execution;

  friend bool operator==(const Phase4TrialWireSuccess& left, const Phase4TrialWireSuccess& right) {
    return left.execution.semantics == right.execution.semantics &&
           left.execution.case_build_elapsed_nanoseconds ==
               right.execution.case_build_elapsed_nanoseconds &&
           left.execution.prepared_elapsed_nanoseconds ==
               right.execution.prepared_elapsed_nanoseconds &&
           left.execution.cold_elapsed_nanoseconds == right.execution.cold_elapsed_nanoseconds &&
           left.execution.preparer_lifecycle == right.execution.preparer_lifecycle;
  }
};

struct Phase4TrialWireFailure {
  Phase4DurableArmFailure failure;

  friend bool operator==(const Phase4TrialWireFailure&, const Phase4TrialWireFailure&) = default;
};

using Phase4TrialWireMessage =
    std::variant<Phase4TrialWireReady, Phase4TrialWireRunCommand, Phase4TrialWireStop,
                 Phase4TrialWireSuccess, Phase4TrialWireFailure, Phase4TrialWireStopped>;

struct Phase4TrialWireError {
  std::string invariant_id;
  std::string detail;

  friend bool operator==(const Phase4TrialWireError&, const Phase4TrialWireError&) = default;
};

using Phase4TrialWireEncodeResult = std::variant<std::vector<std::uint8_t>, Phase4TrialWireError>;
using Phase4TrialWireDecodeResult = std::variant<Phase4TrialWireMessage, Phase4TrialWireError>;
using Phase4TrialWireWriteResult = std::variant<std::monostate, Phase4TrialWireError>;
using Phase4TrialWireExpectedFrameSizeResult =
    std::variant<std::optional<std::size_t>, Phase4TrialWireError>;

// Returns no size until prefix contains the complete fixed header. Once the
// header is complete, validates its magic, schema, kind, and bounded payload
// length before returning the exact frame size. Bytes after that frame, if any,
// are intentionally outside this prefix-inspection contract.
[[nodiscard]] Phase4TrialWireExpectedFrameSizeResult Phase4TrialWireExpectedFrameSizeV1(
    std::span<const std::uint8_t> prefix);

// Encodes or decodes exactly one complete frame. Decode rejects both truncated
// frames and bytes trailing the one declared frame.
[[nodiscard]] Phase4TrialWireEncodeResult EncodePhase4TrialWireMessageV1(
    const Phase4TrialWireMessage& message);

[[nodiscard]] Phase4TrialWireDecodeResult DecodePhase4TrialWireMessageV1(
    std::span<const std::uint8_t> frame);

// Stream helpers read or write exactly one frame and retry interrupted system
// calls. A stream may contain later frames, so trailing-byte rejection belongs
// to DecodePhase4TrialWireMessageV1 rather than the descriptor reader.
[[nodiscard]] Phase4TrialWireDecodeResult ReadPhase4TrialWireMessageV1(int descriptor);

[[nodiscard]] Phase4TrialWireWriteResult WritePhase4TrialWireMessageV1(
    int descriptor, const Phase4TrialWireMessage& message);

}  // namespace apgar::benchmark::internal

#endif  // APGAR_SRC_BENCHMARK_PHASE4_TRIAL_WIRE_INTERNAL_H_
