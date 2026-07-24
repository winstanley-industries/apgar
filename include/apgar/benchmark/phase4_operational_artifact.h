#ifndef APGAR_BENCHMARK_PHASE4_OPERATIONAL_ARTIFACT_H_
#define APGAR_BENCHMARK_PHASE4_OPERATIONAL_ARTIFACT_H_

#include <optional>
#include <string>
#include <string_view>

#include "apgar/benchmark/phase4_paired_trial.h"

namespace apgar::benchmark {

inline constexpr std::uint32_t kPhase4OperationalWorkerOutputSchemaVersion = 1;

enum class Phase4OperationalWorkerOutputKind : std::uint8_t {
  kMeasuredProfile = 0,
  kUnmeasuredReplayAuthority = 1,
};

// Bounded canonical worker payloads consumed only by the separately exec'd
// operational controller. The controller owns process identity, wait4 CPU/RSS,
// watchdog state, hardware provenance, and final publication assembly.
[[nodiscard]] std::optional<std::string> SerializePhase4OperationalProfileWorkerJsonV1(
    const Phase4TrialArmOperationalProfileV1& profile, std::string_view source_commit,
    bool source_stamped, bool source_tree_dirty);

[[nodiscard]] std::optional<std::string> SerializePhase4OperationalProfileWorkerJsonForCorpusV2(
    const Phase4TrialArmOperationalProfileV1& profile, std::string_view source_commit,
    bool source_stamped, bool source_tree_dirty);

[[nodiscard]] std::optional<std::string> SerializePhase4ReplayAuthorityWorkerJsonV1(
    const Phase4TrialArmReplayAuthorityV1& authority, std::string_view source_commit,
    bool source_stamped, bool source_tree_dirty);

[[nodiscard]] std::optional<std::string> SerializePhase4ReplayAuthorityWorkerJsonForCorpusV2(
    const Phase4TrialArmReplayAuthorityV1& authority, std::string_view source_commit,
    bool source_stamped, bool source_tree_dirty);

}  // namespace apgar::benchmark

#endif  // APGAR_BENCHMARK_PHASE4_OPERATIONAL_ARTIFACT_H_
