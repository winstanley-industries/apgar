#ifndef APGAR_BENCHMARK_PHASE4_REPRESENTATIVE_CORPUS_H_
#define APGAR_BENCHMARK_PHASE4_REPRESENTATIVE_CORPUS_H_

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <variant>
#include <vector>

#include "apgar/allocator/multi_net_workload.h"
#include "apgar/allocator/one_world.h"
#include "apgar/benchmark/phase4_corpus.h"
#include "apgar/benchmark/phase4_operational_types.h"
#include "apgar/board_ir/board.h"
#include "apgar/routing/candidate_policy.h"

namespace apgar::benchmark {

inline constexpr std::uint32_t kPhase4RepresentativeCorpusVersion = 1;
inline constexpr std::uint32_t kMaximumPhase4RepresentativeNetsV1 = 4'096;
inline constexpr std::uint64_t kMaximumPhase4ActiveRegionsV1 = 1'000'000;
inline constexpr std::uint64_t kMaximumPhase4BoardEntitiesV1 = 1'000'000;
inline constexpr std::uint32_t kPhase4PrimaryPoolSizeV1 = 8;

enum class Phase4CaseSource : std::uint8_t {
  kSynthetic = 0,
  kImportedFixture = 1,
};

enum class Phase4SyntheticFamily : std::uint8_t {
  kPortalChannels = 0,
  kPinFieldCrossbar = 1,
  kFragmentedMaze = 2,
  kImportedGuardrail = 3,
};

enum class Phase4CaseRole : std::uint8_t {
  kExactOracle = 0,
  kCalibration = 1,
  kHeldOut = 2,
  kQueryShape = 3,
  kStress = 4,
  kImportedGuardrail = 5,
};

enum class Phase4CorpusFeature : std::uint32_t {
  kRouteLength = 1U << 0U,
  kOccupancy = 1U << 1U,
  kFragmentation = 1U << 2U,
  kTurnComplexity = 1U << 3U,
  kReachability = 1U << 4U,
  kRuleBucket = 1U << 5U,
  kRoiSize = 1U << 6U,
};

using Phase4CorpusFeatureMask = std::uint32_t;

[[nodiscard]] constexpr Phase4CorpusFeatureMask Feature(Phase4CorpusFeature feature) noexcept {
  return static_cast<Phase4CorpusFeatureMask>(feature);
}

struct Phase4CaseDescriptor {
  std::uint32_t case_id = 0;
  Phase4CaseSource source = Phase4CaseSource::kSynthetic;
  Phase4SyntheticFamily family = Phase4SyntheticFamily::kPortalChannels;
  Phase4CaseRole role = Phase4CaseRole::kExactOracle;
  std::uint64_t deterministic_seed = 0;
  std::uint32_t requested_net_count = 0;
  std::uint32_t declared_reachable_net_count = 0;
  std::array<std::uint32_t, 3> requested_pool_sizes{};
  std::uint8_t requested_pool_size_count = 0;
  Phase4CorpusFeatureMask features = 0;
  std::uint64_t maximum_exact_candidate_products = 0;
  std::uint32_t declared_stress_target_nets = 0;
  bool known_unmapped_exact_conflicts = false;
  bool globally_coupled_conflict_graph = false;

  friend bool operator==(const Phase4CaseDescriptor&, const Phase4CaseDescriptor&) = default;
};

struct Phase4RepresentativeCorpusLimits {
  std::uint64_t maximum_nets = kMaximumPhase4RepresentativeNetsV1;
  std::uint64_t maximum_compiled_nodes = allocator::kMaximumMultiNetWorkloadCompiledNodesV1;
  std::uint64_t maximum_compiled_host_bytes =
      allocator::kMaximumMultiNetWorkloadCompiledHostBytesV1;
  std::uint64_t maximum_active_regions = 250'000;
  std::uint64_t maximum_board_entities = 100'000;

  friend bool operator==(const Phase4RepresentativeCorpusLimits&,
                         const Phase4RepresentativeCorpusLimits&) = default;
};

struct Phase4RepresentativeCase {
  Phase4CaseDescriptor descriptor;
  board_ir::BoardSnapshot board;
  allocator::MultiNetWorkload workload;
  allocator::ResourceCapacityModel capacities;
  std::vector<routing::EdgeResourceKey> declared_contested_resources;
  std::uint64_t case_checksum = 0;
};

enum class Phase4RepresentativeCorpusErrorCode : std::uint8_t {
  kUnknownCase = 0,
  kInvalidLimits = 1,
  kInputBoundExceeded = 2,
  kImportedFixtureRequired = 3,
  kImportedCorpusFailed = 4,
  kBoardBuildFailed = 5,
  kWorkloadBuildFailed = 6,
  kCapacityBuildFailed = 7,
  kInternalInvariant = 8,
  kResourceExhausted = 9,
  kWorkBoundExceeded = 10,
};

enum class Phase4RepresentativeWorkBound : std::uint8_t {
  kNone = 0,
  kCompiledNodes = 1,
  kCompiledHostBytes = 2,
  kCompiledNodesAndHostBytes = 3,
};

struct Phase4RepresentativeCorpusError {
  Phase4RepresentativeCorpusErrorCode code =
      Phase4RepresentativeCorpusErrorCode::kInternalInvariant;
  std::string_view invariant_id;
  std::string_view detail;
  std::uint32_t requested_case_id = 0;
  Phase4RepresentativeWorkBound limiting_work_bound = Phase4RepresentativeWorkBound::kNone;
  std::uint64_t maximum_preparable_net_count = 0;
  std::optional<board_ir::EntityRef> first_unpreparable_net;
  std::uint64_t required_compiled_nodes = 0;
  std::uint64_t configured_compiled_node_limit = 0;
  std::uint64_t required_compiled_host_bytes = 0;
  std::uint64_t configured_compiled_host_byte_limit = 0;

  friend bool operator==(const Phase4RepresentativeCorpusError&,
                         const Phase4RepresentativeCorpusError&) = default;
};

using Phase4RepresentativeCaseResult =
    std::variant<Phase4RepresentativeCase, Phase4RepresentativeCorpusError>;

struct Phase4RepresentativeCaseOperationalProfileV1 {
  Phase4CaseSource case_source = Phase4CaseSource::kSynthetic;
  Phase4OperationalApplicabilityV1 fixture_import_applicability;
  Phase4OperationalApplicabilityV1 synthetic_materialization_applicability;
  Phase4OperationalApplicabilityV1 compile_probe_applicability;
  std::uint64_t descriptor_validation_and_bound_preflight_wall_nanoseconds = 0;
  std::uint64_t fixture_identity_and_import_wall_nanoseconds = 0;
  std::uint64_t synthetic_geometry_and_board_materialization_wall_nanoseconds = 0;
  std::uint64_t geometry_compilation_probe_wall_nanoseconds = 0;
  std::uint64_t workload_geometry_compilation_wall_nanoseconds = 0;
  std::uint64_t capacity_and_case_assembly_wall_nanoseconds = 0;
  std::uint64_t component_wall_nanoseconds = 0;
  std::uint64_t unclassified_and_release_wall_nanoseconds = 0;

  friend bool operator==(const Phase4RepresentativeCaseOperationalProfileV1&,
                         const Phase4RepresentativeCaseOperationalProfileV1&) = default;
};

// The returned roster is static and descriptor-only. Building one case never
// materializes any other case, which keeps the thousands-net ladder honest and
// bounded by the caller's explicit limits.
[[nodiscard]] std::span<const Phase4CaseDescriptor> Phase4CaseDescriptorsV1() noexcept;

[[nodiscard]] const Phase4CaseDescriptor* FindPhase4CaseDescriptorV1(
    std::uint32_t case_id) noexcept;

[[nodiscard]] std::uint64_t FingerprintPhase4CaseDescriptorV1(
    const Phase4CaseDescriptor& descriptor) noexcept;

[[nodiscard]] std::uint64_t Phase4RepresentativeCorpusChecksumV1() noexcept;

[[nodiscard]] Phase4RepresentativeCaseResult BuildPhase4RepresentativeCaseV1(
    std::uint32_t case_id, std::string_view imported_fixture,
    const Phase4RepresentativeCorpusLimits& limits = {});

[[nodiscard]] Phase4RepresentativeCaseResult BuildPhase4RepresentativeCaseWithOperationalProfileV1(
    std::uint32_t case_id, std::string_view imported_fixture,
    const Phase4RepresentativeCorpusLimits& limits,
    Phase4RepresentativeCaseOperationalProfileV1& operational_profile);

}  // namespace apgar::benchmark

#endif  // APGAR_BENCHMARK_PHASE4_REPRESENTATIVE_CORPUS_H_
