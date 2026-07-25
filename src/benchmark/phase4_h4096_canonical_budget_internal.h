#ifndef APGAR_SRC_BENCHMARK_PHASE4_H4096_CANONICAL_BUDGET_INTERNAL_H_
#define APGAR_SRC_BENCHMARK_PHASE4_H4096_CANONICAL_BUDGET_INTERNAL_H_

#include <optional>
#include <variant>

#include "apgar/benchmark/phase4_trial_harness.h"
#include "src/benchmark/phase4_paired_trial_internal.h"

namespace apgar::benchmark::internal {

// Configuration-preimage capability only. Existing Corpus-v2 execution entry
// points positively require the Protocol-v1 H=2250 authority and reject this
// result before case construction.
[[nodiscard]] static inline Phase4CanonicalSpecResult BuildPhase4CanonicalTrialSpecForCorpusV2H4096(
    const Phase4CanonicalCellConfig& cell, std::uint32_t repetition_index,
    Phase4TrialOrder execution_order) {
  Phase4CanonicalSpecResult result =
      BuildPhase4CanonicalTrialSpecForCorpusV2(cell, repetition_index, execution_order);
  Phase4PairedTrialSpec* spec = std::get_if<Phase4PairedTrialSpec>(&result);
  if (spec == nullptr) {
    return result;
  }
  if (spec->baseline_config.price_config.present_step_per_overuse_unit !=
          kPhase4CorpusV2ProtocolV1PresentStepPerOveruseUnit ||
      spec->baseline_config.price_config.history_step_per_overuse_unit !=
          kPhase4CorpusV2ProtocolV1HistoryStepPerOveruseUnit ||
      !(spec->candidate_session_config.price_config == spec->baseline_config.price_config)) {
    return Phase4TrialHarnessError{
        .invariant_id = "P4HARNESS-H4096-PREIMAGE-001",
        .detail = "H=4096 preimages require the preserved equal-arm H=2250 authority",
        .raw_cell = std::nullopt,
    };
  }
  spec->baseline_config.price_config.history_step_per_overuse_unit =
      kPhase4CorpusV2H4096HistoryStepPerOveruseUnit;
  spec->candidate_session_config.price_config = spec->baseline_config.price_config;
  return result;
}

}  // namespace apgar::benchmark::internal

#endif  // APGAR_SRC_BENCHMARK_PHASE4_H4096_CANONICAL_BUDGET_INTERNAL_H_
