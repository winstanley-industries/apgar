#ifndef APGAR_SRC_BENCHMARK_PHASE4_CONFIRMATORY_H4096_PER_NET_REPORT_INTERNAL_H_
#define APGAR_SRC_BENCHMARK_PHASE4_CONFIRMATORY_H4096_PER_NET_REPORT_INTERNAL_H_

#include <array>
#include <cstdint>
#include <string_view>
#include <variant>

#include "apgar/benchmark/phase4_per_net_report_artifact.h"

namespace apgar::benchmark::internal {

// Fixed Protocol-v2 ordinary diagnostic authority. These entry points select
// H=4096 out of band and accept only calibration cell (10200,8) over Raw
// Wire 1. Existing public Corpus-v2 report entry points remain H=2250-only.
[[nodiscard]] Phase4PerNetReportArtifactResultV1
BuildPhase4ConfirmatoryH4096OrdinaryPerNetReportArtifact(
    const Phase4CanonicalCellConfig& config, std::string_view source_commit, bool source_stamped,
    bool source_tree_dirty, std::uint64_t raw_cell_plan_checksum,
    std::uint64_t raw_cell_artifact_checksum, std::uint64_t raw_source_envelope_checksum,
    Phase4PerNetReportRawReferenceV1 raw_reference,
    std::array<Phase4TrialArmDiagnosticExecutionV1, 2> diagnostics,
    std::string_view imported_fixture);

[[nodiscard]] std::variant<std::monostate, Phase4PerNetReportArtifactError>
ValidatePhase4ConfirmatoryH4096OrdinaryPerNetReportArtifact(
    const Phase4PerNetReportArtifactV1& artifact, std::string_view imported_fixture);

}  // namespace apgar::benchmark::internal

#endif  // APGAR_SRC_BENCHMARK_PHASE4_CONFIRMATORY_H4096_PER_NET_REPORT_INTERNAL_H_
