#ifndef APGAR_SRC_BENCHMARK_PHASE4_CONFIRMATORY_H4096_EXACT_SMALL_SNAPSHOT_INTERNAL_H_
#define APGAR_SRC_BENCHMARK_PHASE4_CONFIRMATORY_H4096_EXACT_SMALL_SNAPSHOT_INTERNAL_H_

#include <cstdint>
#include <string_view>
#include <variant>

#include "apgar/benchmark/phase4_exact_small_snapshot.h"

namespace apgar::benchmark::internal {

// Private Protocol-v2 H=4096 snapshot execution surface. The entry point
// preflights the exact same-run authority before selecting the H=4096
// execution path; public Corpus-v2 snapshot execution remains H=2250-only.
[[nodiscard]] Phase4CandidatePoolSnapshotExecutionResultV1
ExecutePhase4ConfirmatoryH4096SameRunCandidatePoolSnapshot(
    const Phase4PairedTrialSpec& spec, std::string_view imported_fixture,
    allocator::PersistentCpuCandidatePoolPreparer* candidate_preparer = nullptr);

// Exact-Small Snapshot v1's DTO and checksum domains are intentionally
// unchanged. These private entry points independently reconstruct and pin the
// H=4096 exact-cell spec instead of accepting the public H=2250 authority.
[[nodiscard]] Phase4ExactSmallSnapshotArtifactResultV1
BuildPhase4ConfirmatoryH4096ExactSmallSnapshotArtifact(
    const Phase4CanonicalCellConfig& config, std::string_view source_commit, bool source_stamped,
    bool source_tree_dirty, std::uint64_t raw_cell_plan_checksum,
    std::uint64_t raw_cell_artifact_checksum, std::uint64_t raw_source_envelope_checksum,
    Phase4PerNetReportRawReferenceV1 raw_reference, std::uint64_t per_net_report_artifact_checksum,
    std::uint64_t per_net_report_source_envelope_checksum,
    Phase4CandidatePoolSnapshotExecutionV1 capture, std::string_view imported_fixture,
    std::uint32_t raw_evidence_schema_version);

[[nodiscard]] std::variant<std::monostate, Phase4ExactSmallSnapshotError>
ValidatePhase4ConfirmatoryH4096ExactSmallSnapshotArtifact(
    const Phase4ExactSmallSnapshotArtifactV1& artifact, std::string_view imported_fixture);

}  // namespace apgar::benchmark::internal

#endif  // APGAR_SRC_BENCHMARK_PHASE4_CONFIRMATORY_H4096_EXACT_SMALL_SNAPSHOT_INTERNAL_H_
