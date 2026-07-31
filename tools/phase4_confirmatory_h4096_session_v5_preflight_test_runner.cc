#include <iostream>
#include <optional>
#include <string>
#include <string_view>

#include "src/benchmark/phase4_confirmatory_h4096_session_v5_execution_internal.h"

namespace {

using apgar::benchmark::Phase4CanonicalCellConfig;
using apgar::benchmark::Phase4PairedTrialError;
using apgar::benchmark::Phase4TrialArm;
using apgar::benchmark::internal::Phase4H4096SessionV5Carrier;
using apgar::benchmark::internal::Phase4H4096SessionV5ControllerSourceAssociation;
using apgar::benchmark::internal::Phase4H4096SessionV5Endpoint;
using apgar::benchmark::internal::Phase4H4096SessionV5PreflightIdentity;
using apgar::benchmark::internal::Phase4H4096SessionV5WorkerSourceAssociation;

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

enum class Mode {
  kControllerOrdinary,
  kControllerSameRun,
  kOrdinaryWorkerBaseline,
  kOrdinaryWorkerCandidate,
  kSameRunWorkerBaseline,
  kSameRunWorkerCandidate,
};

enum class Mutation {
  kNone,
  kIdentityCarrier,
  kIdentityEndpoint,
  kCellSchema,
  kCellCase,
  kCellPool,
  kCellWorkers,
  kCellRepetitions,
  kCellSetup,
};

struct Options {
  std::optional<Mode> mode;
  std::optional<std::string> runtime_commit;
  Mutation mutation = Mutation::kNone;
  bool mutation_seen = false;
};

[[nodiscard]] std::optional<Mode> ParseMode(std::string_view value) noexcept {
  if (value == "controller-ordinary") {
    return Mode::kControllerOrdinary;
  }
  if (value == "controller-same-run") {
    return Mode::kControllerSameRun;
  }
  if (value == "ordinary-worker-baseline") {
    return Mode::kOrdinaryWorkerBaseline;
  }
  if (value == "ordinary-worker-candidate") {
    return Mode::kOrdinaryWorkerCandidate;
  }
  if (value == "same-run-worker-baseline") {
    return Mode::kSameRunWorkerBaseline;
  }
  if (value == "same-run-worker-candidate") {
    return Mode::kSameRunWorkerCandidate;
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<Mutation> ParseMutation(std::string_view value) noexcept {
  if (value == "identity-carrier") {
    return Mutation::kIdentityCarrier;
  }
  if (value == "identity-endpoint") {
    return Mutation::kIdentityEndpoint;
  }
  if (value == "cell-schema") {
    return Mutation::kCellSchema;
  }
  if (value == "cell-case") {
    return Mutation::kCellCase;
  }
  if (value == "cell-pool") {
    return Mutation::kCellPool;
  }
  if (value == "cell-workers") {
    return Mutation::kCellWorkers;
  }
  if (value == "cell-repetitions") {
    return Mutation::kCellRepetitions;
  }
  if (value == "cell-setup") {
    return Mutation::kCellSetup;
  }
  return std::nullopt;
}

[[nodiscard]] bool IsController(Mode mode) noexcept {
  return mode == Mode::kControllerOrdinary || mode == Mode::kControllerSameRun;
}

[[nodiscard]] Phase4H4096SessionV5Carrier CarrierForMode(Mode mode) noexcept {
  switch (mode) {
    case Mode::kControllerOrdinary:
    case Mode::kOrdinaryWorkerBaseline:
    case Mode::kOrdinaryWorkerCandidate:
      return Phase4H4096SessionV5Carrier::kOrdinary;
    case Mode::kControllerSameRun:
    case Mode::kSameRunWorkerBaseline:
    case Mode::kSameRunWorkerCandidate:
      return Phase4H4096SessionV5Carrier::kSameRun;
  }
  return Phase4H4096SessionV5Carrier::kOrdinary;
}

[[nodiscard]] Phase4TrialArm ArmForMode(Mode mode) noexcept {
  switch (mode) {
    case Mode::kOrdinaryWorkerCandidate:
    case Mode::kSameRunWorkerCandidate:
      return Phase4TrialArm::kReusableCandidateAllocation;
    case Mode::kControllerOrdinary:
    case Mode::kControllerSameRun:
    case Mode::kOrdinaryWorkerBaseline:
    case Mode::kSameRunWorkerBaseline:
      return Phase4TrialArm::kSequentialBaseline;
  }
  return Phase4TrialArm::kSequentialBaseline;
}

[[nodiscard]] bool ParseOptions(int argc, char** argv, Options* options) {
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    constexpr std::string_view kModePrefix = "--mode=";
    constexpr std::string_view kCommitPrefix = "--runtime_commit=";
    constexpr std::string_view kMutationPrefix = "--mutation=";
    if (argument.starts_with(kModePrefix)) {
      if (options->mode.has_value()) {
        return false;
      }
      options->mode = ParseMode(argument.substr(kModePrefix.size()));
      if (!options->mode.has_value()) {
        return false;
      }
    } else if (argument.starts_with(kCommitPrefix)) {
      if (options->runtime_commit.has_value()) {
        return false;
      }
      const std::string_view value = argument.substr(kCommitPrefix.size());
      if (value.empty() || value.size() > 64) {
        return false;
      }
      options->runtime_commit = std::string(value);
    } else if (argument.starts_with(kMutationPrefix)) {
      if (options->mutation_seen) {
        return false;
      }
      const std::optional<Mutation> mutation =
          ParseMutation(argument.substr(kMutationPrefix.size()));
      if (!mutation.has_value()) {
        return false;
      }
      options->mutation = *mutation;
      options->mutation_seen = true;
    } else {
      return false;
    }
  }
  if (!options->mode.has_value()) {
    return false;
  }
  if (IsController(*options->mode)) {
    return options->runtime_commit.has_value();
  }
  return !options->runtime_commit.has_value();
}

[[nodiscard]] Phase4H4096SessionV5Carrier OppositeCarrier(
    Phase4H4096SessionV5Carrier carrier) noexcept {
  return carrier == Phase4H4096SessionV5Carrier::kOrdinary ? Phase4H4096SessionV5Carrier::kSameRun
                                                           : Phase4H4096SessionV5Carrier::kOrdinary;
}

[[nodiscard]] Phase4H4096SessionV5Endpoint OppositeEndpoint(
    Phase4H4096SessionV5Endpoint endpoint) noexcept {
  return endpoint == Phase4H4096SessionV5Endpoint::kController
             ? Phase4H4096SessionV5Endpoint::kWorker
             : Phase4H4096SessionV5Endpoint::kController;
}

void ApplyCellMutation(Mutation mutation, Phase4CanonicalCellConfig* cell) noexcept {
  switch (mutation) {
    case Mutation::kCellSchema:
      ++cell->schema_version;
      return;
    case Mutation::kCellCase:
      ++cell->case_id;
      return;
    case Mutation::kCellPool:
      ++cell->requested_pool_size;
      return;
    case Mutation::kCellWorkers:
      ++cell->preparation_worker_count;
      return;
    case Mutation::kCellRepetitions:
      ++cell->repetitions;
      return;
    case Mutation::kCellSetup:
      ++cell->maximum_setup_elapsed_nanoseconds;
      return;
    case Mutation::kNone:
    case Mutation::kIdentityCarrier:
    case Mutation::kIdentityEndpoint:
      return;
  }
}

[[nodiscard]] std::optional<Phase4PairedTrialError> RunPreflight(const Options& options) {
  const Mode mode = *options.mode;
  const Phase4H4096SessionV5Carrier carrier = CarrierForMode(mode);
  const Phase4H4096SessionV5Endpoint endpoint = IsController(mode)
                                                    ? Phase4H4096SessionV5Endpoint::kController
                                                    : Phase4H4096SessionV5Endpoint::kWorker;
  const Phase4H4096SessionV5Carrier identity_carrier =
      options.mutation == Mutation::kIdentityCarrier ? OppositeCarrier(carrier) : carrier;
  const Phase4H4096SessionV5Endpoint identity_endpoint =
      options.mutation == Mutation::kIdentityEndpoint ? OppositeEndpoint(endpoint) : endpoint;
  const Phase4H4096SessionV5PreflightIdentity identity =
      apgar::benchmark::internal::BuildPhase4ConfirmatoryH4096SessionV5PreflightIdentity(
          identity_carrier, identity_endpoint);
  Phase4CanonicalCellConfig cell =
      apgar::benchmark::internal::BuildPhase4ConfirmatoryH4096SessionV5CanonicalCell(carrier);
  ApplyCellMutation(options.mutation, &cell);

  if (IsController(mode)) {
    const Phase4H4096SessionV5ControllerSourceAssociation source{
        .embedded_commit = kEmbeddedCommit,
        .runtime_commit = *options.runtime_commit,
        .source_stamped = kSourceStamped,
        .source_tree_dirty = kSourceTreeDirty,
    };
    return apgar::benchmark::internal::PreflightPhase4ConfirmatoryH4096SessionV5Controller(
        identity, cell, source);
  }
  const Phase4H4096SessionV5WorkerSourceAssociation source{
      .embedded_commit = kEmbeddedCommit,
      .source_stamped = kSourceStamped,
      .source_tree_dirty = kSourceTreeDirty,
  };
  return apgar::benchmark::internal::PreflightPhase4ConfirmatoryH4096SessionV5Worker(
      identity, cell, ArmForMode(mode), source);
}

}  // namespace

int main(int argc, char** argv) {
  Options options;
  if (!ParseOptions(argc, argv, &options)) {
    std::cerr << kArgumentInvariant << '\n';
    return 2;
  }
  const std::optional<Phase4PairedTrialError> error = RunPreflight(options);
  std::cerr << (error.has_value() ? std::string_view(error->invariant_id)
                                  : kUnexpectedSuccessInvariant)
            << '\n';
  return 2;
}
