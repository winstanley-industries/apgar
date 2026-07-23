#ifndef APGAR_BENCHMARK_PHASE4_SAME_RUN_DECISION_TELEMETRY_H_
#define APGAR_BENCHMARK_PHASE4_SAME_RUN_DECISION_TELEMETRY_H_

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "apgar/benchmark/phase4_trial_harness.h"

namespace apgar::benchmark {

inline constexpr std::uint32_t kPhase4SameRunDecisionTelemetryArtifactSchemaVersion = 1;

[[nodiscard]] std::uint64_t ComputePhase4SameRunDecisionTelemetrySourceEnvelopeChecksumV1(
    std::string_view source_commit, bool source_stamped, bool source_tree_dirty,
    std::uint64_t artifact_checksum) noexcept;

// Canonical one-line sidecar for the exact telemetry-aware isolated execution.
// Same-Run Raw Evidence v2 is serialized independently from capture.raw_cell;
// this companion binds that Wire-v2 source envelope but does not duplicate Raw
// timing or resource observations. Legacy Wire-v1 cells remain Raw v1.
[[nodiscard]] std::optional<std::string> SerializePhase4SameRunDecisionTelemetryJsonV1(
    const Phase4IsolatedCellWithSameRunDecisionTelemetryV1& capture,
    std::string_view imported_fixture, std::string_view source_commit, bool source_stamped,
    bool source_tree_dirty);

}  // namespace apgar::benchmark

#endif  // APGAR_BENCHMARK_PHASE4_SAME_RUN_DECISION_TELEMETRY_H_
