#include <iostream>
#include <optional>
#include <string_view>

#include "src/benchmark/phase4_confirmatory_h4096_session_v5_per_net_report_producer_preflight_internal.h"

namespace {

using apgar::benchmark::Phase4PairedTrialError;
using apgar::benchmark::internal::Phase4H4096SessionV5ControllerSourceAssociation;
using apgar::benchmark::internal::Phase4H4096SessionV5OrdinaryPerNetReportProducerIdentity;

constexpr std::string_view kArgumentInvariant = "P4PAIR-H4096-SESSION-V5-ARGUMENT-001";
constexpr std::string_view kUnexpectedSuccessInvariant = "P4PAIR-H4096-SESSION-V5-BYPASS-001";
constexpr std::string_view kEmbeddedCommit = "0123456789abcdef0123456789abcdef01234567";

#if defined(APGAR_PHASE4_CONFIRMATORY_H4096_SESSION_V5_FORCE_UNPUBLISHABLE_SOURCE_FOR_TESTING)
constexpr bool kSourceStamped = false;
constexpr bool kSourceTreeDirty = true;
#else
constexpr bool kSourceStamped = true;
constexpr bool kSourceTreeDirty = false;
#endif

[[nodiscard]] bool IsLowercaseCommit(std::string_view value) noexcept {
  if (value.size() != 40U) {
    return false;
  }
  for (const char character : value) {
    if (!((character >= '0' && character <= '9') || (character >= 'a' && character <= 'f'))) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] std::optional<std::string_view> ParseRuntimeCommit(int argc, char** argv) noexcept {
  if (argc != 2) {
    return std::nullopt;
  }
  constexpr std::string_view kPrefix = "--runtime_commit=";
  const std::string_view argument(argv[1]);
  if (!argument.starts_with(kPrefix)) {
    return std::nullopt;
  }
  const std::string_view value = argument.substr(kPrefix.size());
  if (!IsLowercaseCommit(value)) {
    return std::nullopt;
  }
  return value;
}

[[nodiscard]] std::optional<Phase4PairedTrialError> RunPreflight(std::string_view runtime_commit) {
  const Phase4H4096SessionV5OrdinaryPerNetReportProducerIdentity identity = apgar::benchmark::
      internal::BuildPhase4ConfirmatoryH4096SessionV5OrdinaryPerNetReportProducerIdentity();
  const Phase4H4096SessionV5ControllerSourceAssociation source{
      .embedded_commit = kEmbeddedCommit,
      .runtime_commit = runtime_commit,
      .source_stamped = kSourceStamped,
      .source_tree_dirty = kSourceTreeDirty,
  };
  return apgar::benchmark::internal::
      PreflightPhase4ConfirmatoryH4096SessionV5OrdinaryPerNetReportProducer(identity, source);
}

}  // namespace

int main(int argc, char** argv) {
  const std::optional<std::string_view> runtime_commit = ParseRuntimeCommit(argc, argv);
  if (!runtime_commit.has_value()) {
    std::cerr << kArgumentInvariant << '\n';
    return 2;
  }
  const std::optional<Phase4PairedTrialError> error = RunPreflight(*runtime_commit);
  std::cerr << (error.has_value() ? std::string_view(error->invariant_id)
                                  : kUnexpectedSuccessInvariant)
            << '\n';
  return 2;
}
