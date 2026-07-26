#ifndef APGAR_SRC_BENCHMARK_PHASE4_H4096_SESSION_V5_CANONICAL_BUDGET_INTERNAL_H_
#define APGAR_SRC_BENCHMARK_PHASE4_H4096_SESSION_V5_CANONICAL_BUDGET_INTERNAL_H_

#include <cstdint>
#include <optional>
#include <variant>

#include "apgar/allocator/cpu_candidate_allocation_session.h"
#include "apgar/allocator/targeted_regeneration.h"
#include "apgar/allocator/targeted_regeneration_execution.h"
#include "apgar/benchmark/phase4_trial_harness.h"
#include "src/benchmark/phase4_h4096_canonical_budget_internal.h"

namespace apgar::benchmark::internal {

// Session v5 normatively composes exactly Plan v3 and Execution v6. Keep this
// historical configuration authority on explicit fixed constants rather than
// moving current-version aliases.
inline constexpr std::uint32_t kPhase4H4096SessionV5PlanSchemaVersion =
    allocator::kTargetedRegenerationPlanSchemaVersionV3;
inline constexpr std::uint32_t kPhase4H4096SessionV5ExecutionSchemaVersion =
    allocator::kTargetedRegenerationExecutionSchemaVersionV6;
static_assert(kPhase4H4096SessionV5PlanSchemaVersion == 3);
static_assert(kPhase4H4096SessionV5ExecutionSchemaVersion == 6);

// Configuration-preimage capability only. This successor derives the frozen
// roster-v3 H=4096/Session-v4 preimage and changes exactly the nested session
// schema. It creates no execution authority.
[[nodiscard]] static inline Phase4CanonicalSpecResult ApplyPhase4H4096SessionV5Authority(
    Phase4CanonicalSpecResult result) {
  Phase4PairedTrialSpec* spec = std::get_if<Phase4PairedTrialSpec>(&result);
  if (spec == nullptr) {
    return result;
  }
  if (spec->candidate_session_config.schema_version !=
      allocator::kCpuCandidateAllocationSessionSchemaVersionV4) {
    return Phase4TrialHarnessError{
        .invariant_id = "P4HARNESS-H4096-SESSION-V5-PREIMAGE-001",
        .detail = "Session-v5 H=4096 preimages require the frozen Session-v4 parent",
        .raw_cell = std::nullopt,
    };
  }
  spec->candidate_session_config.schema_version =
      allocator::kCpuCandidateAllocationSessionSchemaVersionV5;
  return result;
}

[[nodiscard]] static inline Phase4CanonicalSpecResult
BuildPhase4CanonicalTrialSpecForCorpusV2H4096SessionV5(const Phase4CanonicalCellConfig& cell,
                                                       std::uint32_t repetition_index,
                                                       Phase4TrialOrder execution_order) {
  return ApplyPhase4H4096SessionV5Authority(
      BuildPhase4CanonicalTrialSpecForCorpusV2H4096(cell, repetition_index, execution_order));
}

}  // namespace apgar::benchmark::internal

#endif  // APGAR_SRC_BENCHMARK_PHASE4_H4096_SESSION_V5_CANONICAL_BUDGET_INTERNAL_H_
