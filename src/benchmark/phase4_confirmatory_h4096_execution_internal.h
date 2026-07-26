#ifndef APGAR_SRC_BENCHMARK_PHASE4_CONFIRMATORY_H4096_EXECUTION_INTERNAL_H_
#define APGAR_SRC_BENCHMARK_PHASE4_CONFIRMATORY_H4096_EXECUTION_INTERNAL_H_

#include <optional>
#include <string>
#include <string_view>

#include "apgar/benchmark/phase4_trial_harness.h"

namespace apgar::benchmark::internal {

// Frozen Protocol-v2 development authorities. Pure preflight preserves the
// ordinary calibration (10200,8) and same-run exact (10100,4) carrier
// distinctions. Their controller and worker execution surfaces now fail
// closed before fixture access because the budget preimages name Session v4.
[[nodiscard]] std::optional<Phase4TrialHarnessError> PreflightPhase4ConfirmatoryH4096OrdinaryCell(
    const Phase4CanonicalCellConfig& cell);

[[nodiscard]] std::optional<Phase4TrialHarnessError> PreflightPhase4ConfirmatoryH4096SameRunCell(
    const Phase4CanonicalCellConfig& cell);

[[nodiscard]] Phase4IsolatedCellExecution RunPhase4ConfirmatoryH4096OrdinaryCell(
    const Phase4CanonicalCellConfig& cell, std::string_view worker_executable,
    std::string_view imported_fixture_path);

[[nodiscard]] Phase4IsolatedCellWithSameRunDecisionTelemetryExecutionV1
RunPhase4ConfirmatoryH4096SameRunCell(const Phase4CanonicalCellConfig& cell,
                                      std::string_view worker_executable,
                                      std::string_view imported_fixture_path);

[[nodiscard]] int RunPhase4ConfirmatoryH4096OrdinaryWorker(Phase4TrialArm arm,
                                                           const Phase4CanonicalCellConfig& cell,
                                                           std::string_view imported_fixture,
                                                           int request_descriptor,
                                                           int response_descriptor) noexcept;

[[nodiscard]] int RunPhase4ConfirmatoryH4096SameRunWorker(Phase4TrialArm arm,
                                                          const Phase4CanonicalCellConfig& cell,
                                                          std::string_view imported_fixture,
                                                          int request_descriptor,
                                                          int response_descriptor) noexcept;

[[nodiscard]] bool ValidatePhase4ConfirmatoryH4096SameRunCellCapture(
    const Phase4IsolatedCellWithSameRunDecisionTelemetryV1& capture,
    std::string_view imported_fixture);

[[nodiscard]] std::optional<std::string> SerializePhase4ConfirmatoryH4096OrdinaryCellJsonV1(
    const Phase4IsolatedCellResult& result, std::string_view source_commit, bool source_stamped,
    bool source_tree_dirty);

[[nodiscard]] std::optional<std::string> SerializePhase4ConfirmatoryH4096SameRunCellJsonV2(
    const Phase4IsolatedCellResult& result, std::string_view source_commit, bool source_stamped,
    bool source_tree_dirty);

// Implemented by phase4_same_run_decision_telemetry.cc so the companion
// serializer shares this fixed execution authority without widening the public
// Corpus-v2 telemetry surface.
[[nodiscard]] std::optional<std::string>
SerializePhase4ConfirmatoryH4096SameRunDecisionTelemetryJsonV1(
    const Phase4IsolatedCellWithSameRunDecisionTelemetryV1& capture,
    std::string_view imported_fixture, std::string_view source_commit, bool source_stamped,
    bool source_tree_dirty);

}  // namespace apgar::benchmark::internal

#endif  // APGAR_SRC_BENCHMARK_PHASE4_CONFIRMATORY_H4096_EXECUTION_INTERNAL_H_
