#include "apgar/allocator/cpu_candidate_pool_preparation.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <limits>
#include <mutex>
#include <new>
#include <optional>
#include <ranges>
#include <set>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "apgar/board_ir/stable_hash.h"
#include "apgar/candidates/route_candidate.h"
#include "apgar/routing/candidate_policy.h"
#include "src/allocator/cpu_candidate_pool_preparation_internal.h"
#include "src/operational_timestamp.h"

namespace apgar::allocator {
namespace {

using UWide = unsigned __int128;
using OperationalClock = std::chrono::steady_clock;

[[nodiscard]] std::uint64_t OperationalElapsed(OperationalClock::time_point start) noexcept {
  const auto elapsed =
      std::chrono::duration_cast<std::chrono::nanoseconds>(OperationalClock::now() - start).count();
  return elapsed <= 0 ? 0 : static_cast<std::uint64_t>(elapsed);
}

void AccumulateOperationalElapsed(std::uint64_t* target, std::uint64_t value) noexcept {
  if (target == nullptr) {
    return;
  }
  *target = value > std::numeric_limits<std::uint64_t>::max() - *target
                ? std::numeric_limits<std::uint64_t>::max()
                : *target + value;
}

template <bool CaptureOperationalProfile>
class ScopedOperationalElapsed;

template <>
class ScopedOperationalElapsed<true> {
 public:
  ScopedOperationalElapsed(std::vector<std::uint64_t>& elapsed, std::size_t index) noexcept
      : target_(&elapsed[index]) {
    start_ = OperationalClock::now();
  }
  ~ScopedOperationalElapsed() { AccumulateOperationalElapsed(target_, OperationalElapsed(start_)); }

 private:
  std::uint64_t* target_;
  OperationalClock::time_point start_{};
};

template <>
class ScopedOperationalElapsed<false> {
 public:
  struct EmptyElapsed {};
  constexpr ScopedOperationalElapsed(EmptyElapsed&, std::size_t) noexcept {}
};

template <bool CaptureOperationalProfile>
using OperationalColumnElapsed =
    std::conditional_t<CaptureOperationalProfile, std::vector<std::uint64_t>,
                       ScopedOperationalElapsed<false>::EmptyElapsed>;

[[nodiscard]] std::uint64_t NarrowWitness(UWide value) noexcept {
  return value > std::numeric_limits<std::uint64_t>::max()
             ? std::numeric_limits<std::uint64_t>::max()
             : static_cast<std::uint64_t>(value);
}

#ifdef APGAR_CPU_CANDIDATE_POOL_PREPARATION_FAULT_TEST_VARIANT
struct FaultTestState {
  std::atomic<internal::CpuCandidatePoolPreparationFaultForTesting> fault{
      internal::CpuCandidatePoolPreparationFaultForTesting::kNone};
  std::atomic<std::size_t> index{0};
  std::mutex mutex;
  std::condition_variable condition;
  bool worker_blocked = false;
  bool release_worker = false;
};

FaultTestState g_fault_test_state;

void MaybeInjectThreadCreationFault(std::size_t index) {
  if (g_fault_test_state.index.load() != index) {
    return;
  }
  const internal::CpuCandidatePoolPreparationFaultForTesting fault =
      g_fault_test_state.fault.load();
  if (fault == internal::CpuCandidatePoolPreparationFaultForTesting::kThreadCreationBadAlloc) {
    g_fault_test_state.fault.store(internal::CpuCandidatePoolPreparationFaultForTesting::kNone);
    throw std::bad_alloc();
  }
  if (fault == internal::CpuCandidatePoolPreparationFaultForTesting::kThreadCreationSystemError) {
    g_fault_test_state.fault.store(internal::CpuCandidatePoolPreparationFaultForTesting::kNone);
    throw std::system_error(std::make_error_code(std::errc::resource_unavailable_try_again));
  }
}

void MaybeInjectWorkerFault(std::size_t index) {
  if (g_fault_test_state.index.load() != index) {
    return;
  }
  const internal::CpuCandidatePoolPreparationFaultForTesting fault =
      g_fault_test_state.fault.load();
  switch (fault) {
    case internal::CpuCandidatePoolPreparationFaultForTesting::kWorkerBadAlloc:
      g_fault_test_state.fault.store(internal::CpuCandidatePoolPreparationFaultForTesting::kNone);
      throw std::bad_alloc();
    case internal::CpuCandidatePoolPreparationFaultForTesting::kWorkerLengthError:
      g_fault_test_state.fault.store(internal::CpuCandidatePoolPreparationFaultForTesting::kNone);
      throw std::length_error("injected worker container exhaustion");
    case internal::CpuCandidatePoolPreparationFaultForTesting::kWorkerUnexpectedException:
      g_fault_test_state.fault.store(internal::CpuCandidatePoolPreparationFaultForTesting::kNone);
      throw std::runtime_error("injected unexpected worker exception");
    case internal::CpuCandidatePoolPreparationFaultForTesting::kBlockWorker: {
      g_fault_test_state.fault.store(internal::CpuCandidatePoolPreparationFaultForTesting::kNone);
      std::unique_lock lock(g_fault_test_state.mutex);
      g_fault_test_state.worker_blocked = true;
      g_fault_test_state.condition.notify_all();
      g_fault_test_state.condition.wait(lock, [] { return g_fault_test_state.release_worker; });
      return;
    }
    case internal::CpuCandidatePoolPreparationFaultForTesting::kNone:
    case internal::CpuCandidatePoolPreparationFaultForTesting::kThreadCreationBadAlloc:
    case internal::CpuCandidatePoolPreparationFaultForTesting::kThreadCreationSystemError:
    case internal::CpuCandidatePoolPreparationFaultForTesting::kPostBaseWaveBadAlloc:
    case internal::CpuCandidatePoolPreparationFaultForTesting::kPostPublicationBadAlloc:
    case internal::CpuCandidatePoolPreparationFaultForTesting::kPostBaseWaveUnexpectedException:
    case internal::CpuCandidatePoolPreparationFaultForTesting::kPostPublicationUnexpectedException:
      return;
  }
}

void MaybeInjectPreparationStageFault(
    internal::CpuCandidatePoolPreparationFaultForTesting expected) {
  if (g_fault_test_state.fault.load() != expected) {
    return;
  }
  g_fault_test_state.fault.store(internal::CpuCandidatePoolPreparationFaultForTesting::kNone);
  if (expected ==
          internal::CpuCandidatePoolPreparationFaultForTesting::kPostBaseWaveUnexpectedException ||
      expected == internal::CpuCandidatePoolPreparationFaultForTesting::
                      kPostPublicationUnexpectedException) {
    throw std::runtime_error("injected unexpected preparation-stage exception");
  }
  throw std::bad_alloc();
}
#endif

[[nodiscard]] CpuCandidatePoolPreparationError Error(
    CpuCandidatePoolPreparationErrorCode code, std::string_view invariant_id,
    std::string_view detail, std::optional<board_ir::EntityRef> net = std::nullopt,
    std::uint64_t required = 0, std::uint64_t configured = 0) noexcept {
  return CpuCandidatePoolPreparationError{
      .code = code,
      .invariant_id = invariant_id,
      .detail = detail,
      .net = net,
      .required = required,
      .configured = configured,
      .failed_preparation = std::nullopt,
  };
}

[[nodiscard]] bool NetBefore(board_ir::EntityRef left, board_ir::EntityRef right) noexcept {
  return std::tie(left.id, left.generation) < std::tie(right.id, right.generation);
}

[[nodiscard]] bool IsSupportedPoolSize(std::uint32_t size) noexcept {
  return size == 4 || size == 8 || size == 16;
}

[[nodiscard]] bool IsDuplicateCode(candidates::CandidateRejectionCode code) noexcept {
  return code == candidates::CandidateRejectionCode::kDuplicateIdentity ||
         code == candidates::CandidateRejectionCode::kDuplicateGeometry ||
         code == candidates::CandidateRejectionCode::kDuplicateResources;
}

[[nodiscard]] bool RouteLimitsAreValid(const routing::CpuRouteWorkLimits& limits) noexcept {
  return limits.maximum_work_units != 0 &&
         limits.maximum_work_units <= kMaximumCpuCandidatePoolRouteWorkUnitsV1 &&
         limits.maximum_record_count != 0 && limits.maximum_queue_size != 0 &&
         limits.maximum_reconstruction_states != 0;
}

[[nodiscard]] UWide CandidateDraftLogicalByteUpperBound(
    const routing::CpuRouteWorkLimits& route_limits,
    UWide maximum_policy_entries_per_candidate) noexcept {
  // Candidate v1 encodes substantially less than these fixed charges. Keep the
  // derivation deliberately conservative so the bound also covers vector
  // backing storage retained before atomic publication.
  constexpr UWide kFixedDraftBytes = 4'096;
  constexpr UWide kBytesPerReconstructedState = 512;
  constexpr UWide kBytesPerPolicyEntry = 128;
  return kFixedDraftBytes +
         static_cast<UWide>(route_limits.maximum_reconstruction_states) *
             kBytesPerReconstructedState +
         maximum_policy_entries_per_candidate * kBytesPerPolicyEntry;
}

[[nodiscard]] routing::PlanarRouteRequest RequestWithPolicy(
    const routing::PlanarRouteRequest& source, const routing::CandidateGenerationPolicy& policy) {
  return routing::PlanarRouteRequest{
      .net = source.net,
      .start = source.start,
      .goal = source.goal,
      .start_layer = source.start_layer,
      .goal_layer = source.goal_layer,
      .candidate_policy = policy,
  };
}

[[nodiscard]] std::uint64_t NetSeedEncoding(std::uint64_t seed, std::uint64_t workload_checksum,
                                            board_ir::EntityRef net) noexcept {
  board_ir::StableHashBuilder hash;
  hash.AddString("APGAR-CPU-CANDIDATE-POOL-NET-SEED-V1");
  hash.AddU64(seed);
  hash.AddU64(workload_checksum);
  hash.AddU64(net.id);
  hash.AddU32(net.generation);
  const std::uint64_t result = hash.Finish();
  return result == 0 ? 1 : result;
}

void AddStoreConfig(board_ir::StableHashBuilder& hash,
                    const candidates::CandidateStoreConfig& config) noexcept {
  hash.AddU64(config.maximum_candidates_per_net);
  hash.AddU64(config.maximum_candidate_bytes_per_net);
  hash.AddU64(config.maximum_rejection_records);
  hash.AddU64(config.maximum_rejection_items_per_transaction);
  hash.AddU64(config.maximum_admission_items_per_transaction);
  hash.AddU64(config.maximum_admission_input_bytes_per_transaction);
  hash.AddU64(config.maximum_admission_work_units_per_transaction);
  hash.AddU64(config.maximum_pin_lease_items_per_transaction);
  hash.AddU64(config.maximum_expected_pools_per_invocation);
  hash.AddU64(config.maximum_expected_candidates_per_invocation);
}

void AddPreparationConfig(board_ir::StableHashBuilder& hash,
                          const CpuCandidatePoolPreparationConfig& config) noexcept {
  hash.AddU32(config.schema_version);
  hash.AddU32(config.requested_candidates_per_net);
  hash.AddU64(config.deterministic_seed);
  hash.AddU64(config.step_surcharge_increment);
  hash.AddU64(config.bend_surcharge_increment);
  hash.AddU64(config.resource_penalty_increment);
  hash.AddU64(config.route_limits.maximum_work_units);
  hash.AddU64(config.route_limits.maximum_record_count);
  hash.AddU64(config.route_limits.maximum_queue_size);
  hash.AddU64(config.route_limits.maximum_reconstruction_states);
  hash.AddU64(config.limits.maximum_nets);
  hash.AddU64(config.limits.maximum_route_queries);
  hash.AddU64(config.limits.maximum_total_route_work_units);
  hash.AddU64(config.limits.maximum_concurrent_route_records);
  hash.AddU64(config.limits.maximum_concurrent_queue_entries);
  hash.AddU64(config.limits.maximum_concurrent_reconstruction_states);
  hash.AddU64(config.limits.maximum_policy_resource_entries);
  hash.AddU64(config.limits.maximum_retained_candidate_bytes);
  hash.AddU64(config.limits.maximum_candidate_draft_bytes);
  hash.AddU64(config.limits.maximum_generated_candidate_bytes);
  AddStoreConfig(hash, config.store_config);
}

[[nodiscard]] std::uint64_t BatchIdentity(
    const board_ir::BoardSnapshot& board, const MultiNetWorkload& workload,
    const CpuCandidatePoolPreparationConfig& config) noexcept {
  return internal::ComputeCpuCandidatePoolBatchIdentityV2(board.content_hash(),
                                                          workload.workload_checksum(), config);
}

[[nodiscard]] candidates::CandidateRejection RouteFailureRejection(
    const candidates::CandidateAssociations& associations, const routing::RouteFailure& failure,
    board_ir::EntityRef net, const routing::NormalizedCandidateGenerationPolicy& policy,
    candidates::CandidateSchedulingIdentity scheduling) {
  candidates::CandidateRejection rejection;
  rejection.net = net;
  rejection.stage = candidates::CandidateLifecycleStage::kGenerated;
  rejection.code = failure.code == routing::RouteFailureCode::kDisconnected
                       ? candidates::CandidateRejectionCode::kBackendFailure
                       : candidates::CandidateRejectionCode::kUnsupported;
  rejection.invariant_id = failure.code == routing::RouteFailureCode::kDisconnected
                               ? "allocator.cpu_candidate_pool.route_disconnected.v1"
                               : "allocator.cpu_candidate_pool.route_unsupported.v1";
  rejection.associations = associations;
  rejection.policy_identity = policy.identity;
  rejection.provenance = candidates::CandidateProvenance{
      .generator = candidates::CandidateGeneratorKind::kCpuAStar,
      .generator_version = 1,
      .backend = candidates::CandidateBackendKind::kCpu,
      .supported_device_class = std::string(candidates::kCpuReferenceDeviceClassV1),
      .deterministic_seed = policy.policy.deterministic_seed,
      .batch_identity = scheduling.batch_identity,
      .query_identity = scheduling.query_identity,
      .candidate_ordinal = policy.policy.candidate_ordinal,
  };
  rejection.detail = failure.code == routing::RouteFailureCode::kDisconnected
                         ? "CPU candidate preparation proved the net disconnected"
                         : "CPU candidate preparation encountered an unsupported request";
  return candidates::CanonicalizeCandidateRejectionV1(rejection);
}

[[nodiscard]] std::variant<std::vector<routing::EdgeResourceKey>, CpuCandidatePoolPreparationError>
ResourcesForAuthenticatedRoute(const routing::CpuRoute& route,
                               const geometry_compiler::CompiledBoard& board,
                               const routing::PlanarRouteRequest& expected_request,
                               std::uint64_t expected_policy_identity,
                               std::uint64_t maximum_edges) {
  if (!routing::CpuRouteHasAuthenticatedAStarEvidence(route) || route.segments.empty() ||
      route.source_board_content_hash != board.source_board_content_hash() ||
      route.compiler_profile_fingerprint != board.compiler_profile_fingerprint() ||
      route.compiler_version != board.compiler_version() ||
      route.routing_profile_fingerprint !=
          routing::FingerprintRoutingProfile(board.routing_profile()) ||
      route.rule_bucket_identity != board.rule_bucket().identity ||
      route.net != expected_request.net || route.net != board.routing_profile().net ||
      route.requested_start != expected_request.start ||
      route.requested_goal != expected_request.goal ||
      route.requested_start_layer != expected_request.start_layer ||
      route.requested_goal_layer != expected_request.goal_layer ||
      route.candidate_policy_identity != expected_policy_identity || maximum_edges == 0) {
    return Error(CpuCandidatePoolPreparationErrorCode::kInternalInvariant,
                 "allocator.cpu_candidate_pool.base_footprint.v1",
                 "A reached CPU base route has no authentic bounded exact footprint", route.net);
  }

  struct DecodedSegment {
    geometry_compiler::LatticeIndex start;
    geometry_compiler::Direction direction;
    std::uint64_t steps;
  };
  const auto decode_segment = [&](const routing::LayerSegment& segment)
      -> std::variant<DecodedSegment, CpuCandidatePoolPreparationError> {
    const auto start =
        geometry_compiler::ExactPointToLatticeIndex(board.profile(), segment.centerline.start);
    const auto end =
        geometry_compiler::ExactPointToLatticeIndex(board.profile(), segment.centerline.end);
    if (!start.has_value() || !end.has_value() || segment.layer != route.requested_start_layer) {
      return Error(CpuCandidatePoolPreparationErrorCode::kInternalInvariant,
                   "allocator.cpu_candidate_pool.base_resource_segment.v1",
                   "An authenticated base segment is outside its exact compiler lattice",
                   route.net);
    }
    const __int128_t dx = static_cast<__int128_t>(end->x) - start->x;
    const __int128_t dy = static_cast<__int128_t>(end->y) - start->y;
    const __int128_t absolute_x = dx < 0 ? -dx : dx;
    const __int128_t absolute_y = dy < 0 ? -dy : dy;
    if ((dx == 0 && dy == 0) || (dx != 0 && dy != 0 && absolute_x != absolute_y)) {
      return Error(CpuCandidatePoolPreparationErrorCode::kInternalInvariant,
                   "allocator.cpu_candidate_pool.base_resource_heading.v1",
                   "An authenticated base segment has a non-H/V/45 heading", route.net);
    }
    const geometry_compiler::DirectionDelta unit{
        .x = static_cast<std::int8_t>(dx == 0 ? 0 : (dx > 0 ? 1 : -1)),
        .y = static_cast<std::int8_t>(dy == 0 ? 0 : (dy > 0 ? 1 : -1)),
    };
    const auto direction = std::ranges::find_if(
        geometry_compiler::kStableDirectionOrder, [&unit](geometry_compiler::Direction candidate) {
          const geometry_compiler::DirectionDelta delta = geometry_compiler::DeltaFor(candidate);
          return delta.x == unit.x && delta.y == unit.y;
        });
    if (direction == geometry_compiler::kStableDirectionOrder.end()) {
      return Error(CpuCandidatePoolPreparationErrorCode::kInternalInvariant,
                   "allocator.cpu_candidate_pool.base_resource_direction.v1",
                   "An authenticated base segment has no canonical direction", route.net);
    }
    const UWide steps = static_cast<UWide>(std::max(absolute_x, absolute_y));
    return DecodedSegment{
        .start = *start,
        .direction = *direction,
        .steps = static_cast<std::uint64_t>(steps),
    };
  };

  UWide total_steps = 0;
  for (const routing::LayerSegment& segment : route.segments) {
    auto decoded = decode_segment(segment);
    if (auto* failure = std::get_if<CpuCandidatePoolPreparationError>(&decoded);
        failure != nullptr) {
      return std::move(*failure);
    }
    total_steps += std::get<DecodedSegment>(decoded).steps;
    if (total_steps > maximum_edges) {
      return Error(CpuCandidatePoolPreparationErrorCode::kInternalInvariant,
                   "allocator.cpu_candidate_pool.base_resource_bound.v1",
                   "An authenticated base footprint exceeds its reconstruction bound", route.net);
    }
  }
  if (total_steps == 0 || total_steps > std::numeric_limits<std::size_t>::max()) {
    return Error(CpuCandidatePoolPreparationErrorCode::kInternalInvariant,
                 "allocator.cpu_candidate_pool.base_resource_empty.v1",
                 "An authenticated base route expanded to no bounded physical resource", route.net);
  }

  static_assert(sizeof(routing::EdgeResourceKey) <= 40U);
  std::vector<routing::EdgeResourceKey> resources(static_cast<std::size_t>(total_steps));
  if (resources.capacity() != resources.size()) {
    return Error(CpuCandidatePoolPreparationErrorCode::kResourceExhausted,
                 "allocator.cpu_candidate_pool.base_resource_capacity.v1",
                 "The host vector implementation did not provide an exact-sized edge roster",
                 route.net, resources.capacity(), resources.size());
  }
  std::size_t write_index = 0;
  for (const routing::LayerSegment& segment : route.segments) {
    auto decoded_result = decode_segment(segment);
    if (auto* failure = std::get_if<CpuCandidatePoolPreparationError>(&decoded_result);
        failure != nullptr) {
      return std::move(*failure);
    }
    const DecodedSegment& decoded = std::get<DecodedSegment>(decoded_result);
    const geometry_compiler::DirectionDelta unit = geometry_compiler::DeltaFor(decoded.direction);
    for (std::uint64_t index = 0; index < decoded.steps; ++index) {
      const __int128_t x =
          static_cast<__int128_t>(decoded.start.x) + static_cast<__int128_t>(unit.x) * index;
      const __int128_t y =
          static_cast<__int128_t>(decoded.start.y) + static_cast<__int128_t>(unit.y) * index;
      const auto resource = routing::CanonicalPhysicalEdgeResource(
          segment.layer,
          geometry_compiler::LatticeIndex{.x = static_cast<std::int64_t>(x),
                                          .y = static_cast<std::int64_t>(y)},
          decoded.direction);
      if (!resource.has_value()) {
        return Error(CpuCandidatePoolPreparationErrorCode::kInternalInvariant,
                     "allocator.cpu_candidate_pool.base_resource_coordinate.v1",
                     "An authenticated base segment cannot produce a canonical edge", route.net);
      }
      resources[write_index++] = *resource;
    }
  }
  if (write_index != resources.size()) {
    return Error(CpuCandidatePoolPreparationErrorCode::kInternalInvariant,
                 "allocator.cpu_candidate_pool.base_resource_fill.v1",
                 "The exact-sized edge roster was not filled deterministically", route.net);
  }
  std::ranges::sort(resources);
  resources.erase(std::ranges::unique(resources).begin(), resources.end());
  return resources;
}

struct WorkColumn {
  const PreparedNetRoutingContext* context = nullptr;
  std::optional<routing::NormalizedCandidateGenerationPolicy> policy;
  std::optional<routing::PlanarRouteRequest> request;
  std::optional<std::vector<routing::EdgeResourceKey>> base_resources;
  std::optional<candidates::GeneratedRouteCandidate> draft;
  std::optional<candidates::CandidateRejection> rejection;
  std::optional<CpuCandidatePoolPreparationError> fatal_error;
  bool executed = false;
  bool query_started = false;
};

[[nodiscard]] std::uint64_t ComputeFailedPreparationChecksumEncoding(
    std::uint64_t board_content_hash, std::uint64_t workload_checksum,
    const CpuCandidatePoolPreparationConfig& config, std::uint64_t batch_identity,
    CpuCandidatePoolPreparationErrorCode error_code, bool candidate_store_publication_committed,
    const CpuCandidatePoolFailedPreparationCounters& counters,
    std::span<const CpuCandidatePoolAttemptedColumnRecord> attempted_columns) noexcept {
  board_ir::StableHashBuilder hash;
  hash.AddString("APGAR-CPU-CANDIDATE-POOL-FAILED-PREPARATION-V2");
  hash.AddU32(kCpuCandidatePoolPreparationSchemaVersion);
  hash.AddU64(board_content_hash);
  hash.AddU64(workload_checksum);
  AddPreparationConfig(hash, config);
  hash.AddU64(batch_identity);
  hash.AddByte(static_cast<std::uint8_t>(error_code));
  hash.AddBool(candidate_store_publication_committed);
  hash.AddU64(counters.requested_columns);
  hash.AddU64(counters.route_queries);
  hash.AddU64(counters.route_work_units);
  hash.AddU64(attempted_columns.size());
  for (const CpuCandidatePoolAttemptedColumnRecord& column : attempted_columns) {
    hash.AddU64(column.net.id);
    hash.AddU32(column.net.generation);
    hash.AddU32(column.candidate_ordinal);
    hash.AddU64(column.policy_identity);
    hash.AddU64(column.batch_identity);
    hash.AddU64(column.query_identity);
    hash.AddU64(column.route_work_units);
    hash.AddByte(static_cast<std::uint8_t>(column.state));
    hash.AddBool(column.route_failure_code.has_value());
    if (column.route_failure_code.has_value()) {
      hash.AddByte(static_cast<std::uint8_t>(*column.route_failure_code));
    }
  }
  return hash.Finish();
}

[[nodiscard]] CpuCandidatePoolPreparationError* FirstFatal(std::span<WorkColumn> work) noexcept {
  for (WorkColumn& column : work) {
    if (column.fatal_error.has_value()) {
      return &*column.fatal_error;
    }
  }
  return nullptr;
}

[[nodiscard]] CpuCandidatePoolPreparationError WithFailedPreparationObservation(
    CpuCandidatePoolPreparationError error, const board_ir::BoardSnapshot& board,
    const MultiNetWorkload& workload, const CpuCandidatePoolPreparationConfig& config,
    std::uint64_t batch_identity, std::span<const WorkColumn> work,
    std::vector<CpuCandidatePoolAttemptedColumnRecord> attempted_columns,
    bool candidate_store_publication_committed = false,
    std::unique_ptr<candidates::CandidateStore> authoritative_candidate_store = nullptr) {
  std::size_t write_index = 0;
  UWide route_work = 0;
  for (std::size_t index = 0; index < work.size(); ++index) {
    if (!work[index].query_started) {
      continue;
    }
    attempted_columns[write_index++] = attempted_columns[index];
    route_work += attempted_columns[index].route_work_units;
  }
  if (write_index == 0) {
    return error;
  }
  attempted_columns.resize(write_index);
  CpuCandidatePoolFailedPreparationObservation observation{
      .schema_version = kCpuCandidatePoolPreparationSchemaVersion,
      .board_content_hash = board.content_hash(),
      .workload_checksum = workload.workload_checksum(),
      .config = config,
      .batch_identity = batch_identity,
      .counters =
          CpuCandidatePoolFailedPreparationCounters{
              .requested_columns = work.size(),
              .route_queries = write_index,
              .route_work_units = NarrowWitness(route_work),
          },
      .attempted_columns = std::move(attempted_columns),
      .candidate_store_publication_committed = candidate_store_publication_committed,
      .authoritative_candidate_store = std::move(authoritative_candidate_store),
      .observation_checksum = 0,
  };
  observation.observation_checksum = internal::ComputeCpuCandidatePoolFailedPreparationChecksumV2(
      observation.board_content_hash, observation.workload_checksum, observation.config,
      observation.batch_identity, error.code, observation.candidate_store_publication_committed,
      observation.counters, observation.attempted_columns);
  error.failed_preparation = std::move(observation);
  return error;
}

[[nodiscard]] std::uint64_t ComputePreparationChecksumEncoding(
    const CpuCandidatePoolPreparationConfig& config, std::uint64_t batch_identity,
    const CpuCandidatePoolPreparationCounters& counters,
    std::span<const CpuCandidatePoolColumnRecord> columns,
    std::span<const internal::CpuCandidatePoolChecksumPoolV1> pools) noexcept {
  board_ir::StableHashBuilder hash;
  hash.AddString("APGAR-CPU-CANDIDATE-POOL-PREPARATION-V2");
  AddPreparationConfig(hash, config);
  hash.AddU64(batch_identity);
  hash.AddU64(counters.requested_columns);
  hash.AddU64(counters.executed_route_queries);
  hash.AddU64(counters.route_work_units);
  hash.AddU64(counters.successful_routes);
  hash.AddU64(counters.disconnected_proofs);
  hash.AddU64(counters.unsupported_proofs);
  hash.AddU64(counters.skipped_columns);
  hash.AddU64(counters.built_candidates);
  hash.AddU64(counters.admitted_candidates);
  hash.AddU64(counters.duplicate_candidates);
  hash.AddU64(counters.rejected_columns);
  hash.AddU64(counters.retained_candidates);
  hash.AddU64(columns.size());
  for (const CpuCandidatePoolColumnRecord& column : columns) {
    hash.AddU64(column.net.id);
    hash.AddU32(column.net.generation);
    hash.AddU32(column.candidate_ordinal);
    hash.AddU64(column.policy_identity);
    hash.AddU64(column.batch_identity);
    hash.AddU64(column.query_identity);
    hash.AddU64(column.route_work_units);
    hash.AddByte(static_cast<std::uint8_t>(column.outcome));
    hash.AddBool(column.candidate_id.has_value());
    if (column.candidate_id.has_value()) {
      hash.AddU64(column.candidate_id->high);
      hash.AddU64(column.candidate_id->low);
    }
    hash.AddBool(column.candidate_payload_checksum.has_value());
    if (column.candidate_payload_checksum.has_value()) {
      hash.AddU64(*column.candidate_payload_checksum);
    }
    hash.AddBool(column.rejection_code.has_value());
    if (column.rejection_code.has_value()) {
      hash.AddByte(static_cast<std::uint8_t>(*column.rejection_code));
    }
  }
  hash.AddU64(pools.size());
  for (const internal::CpuCandidatePoolChecksumPoolV1& pool : pools) {
    hash.AddU64(pool.net.id);
    hash.AddU32(pool.net.generation);
    hash.AddU64(pool.candidates.size());
    for (const internal::CpuCandidatePoolChecksumCandidateV1& candidate : pool.candidates) {
      hash.AddU64(candidate.id.high);
      hash.AddU64(candidate.id.low);
      hash.AddU64(candidate.payload_checksum);
    }
  }
  return hash.Finish();
}

[[nodiscard]] std::uint64_t PreparationChecksum(
    const CpuCandidatePoolPreparationConfig& config, std::uint64_t batch_identity,
    const CpuCandidatePoolPreparationCounters& counters,
    std::span<const CpuCandidatePoolColumnRecord> columns, std::span<const CandidatePool> pools) {
  std::vector<std::vector<internal::CpuCandidatePoolChecksumCandidateV1>> candidate_records;
  candidate_records.reserve(pools.size());
  for (const CandidatePool& pool : pools) {
    std::vector<internal::CpuCandidatePoolChecksumCandidateV1>& records =
        candidate_records.emplace_back();
    records.reserve(pool.candidates.size());
    for (const candidates::StoredCandidate& candidate : pool.candidates) {
      records.push_back(internal::CpuCandidatePoolChecksumCandidateV1{
          .id = candidate->id(),
          .payload_checksum = candidate->data().payload_checksum,
      });
    }
  }
  std::vector<internal::CpuCandidatePoolChecksumPoolV1> pool_records;
  pool_records.reserve(pools.size());
  for (std::size_t index = 0; index < pools.size(); ++index) {
    pool_records.push_back(internal::CpuCandidatePoolChecksumPoolV1{
        .net = pools[index].net,
        .candidates = candidate_records[index],
    });
  }
  return internal::ComputeCpuCandidatePoolPreparationChecksumV2(config, batch_identity, counters,
                                                                columns, pool_records);
}

}  // namespace

std::uint64_t internal::ComputeCpuCandidatePoolBatchIdentityV2(
    std::uint64_t board_content_hash, std::uint64_t workload_checksum,
    const CpuCandidatePoolPreparationConfig& config) noexcept {
  board_ir::StableHashBuilder hash;
  hash.AddString("APGAR-CPU-CANDIDATE-POOL-BATCH-V2");
  hash.AddU64(board_content_hash);
  hash.AddU64(workload_checksum);
  AddPreparationConfig(hash, config);
  const std::uint64_t result = hash.Finish();
  return result == 0 ? 1 : result;
}

std::uint64_t internal::ComputeCpuCandidatePoolNetSeedV1(std::uint64_t configured_seed,
                                                         std::uint64_t workload_checksum,
                                                         board_ir::EntityRef net) noexcept {
  return NetSeedEncoding(configured_seed, workload_checksum, net);
}

std::uint64_t internal::ComputeCpuCandidatePoolPreparationChecksumV2(
    const CpuCandidatePoolPreparationConfig& config, std::uint64_t batch_identity,
    const CpuCandidatePoolPreparationCounters& counters,
    std::span<const CpuCandidatePoolColumnRecord> columns,
    std::span<const CpuCandidatePoolChecksumPoolV1> pools) noexcept {
  return ComputePreparationChecksumEncoding(config, batch_identity, counters, columns, pools);
}

std::uint64_t internal::ComputeCpuCandidatePoolFailedPreparationChecksumV2(
    std::uint64_t board_content_hash, std::uint64_t workload_checksum,
    const CpuCandidatePoolPreparationConfig& config, std::uint64_t batch_identity,
    CpuCandidatePoolPreparationErrorCode error_code, bool candidate_store_publication_committed,
    const CpuCandidatePoolFailedPreparationCounters& counters,
    std::span<const CpuCandidatePoolAttemptedColumnRecord> attempted_columns) noexcept {
  return ComputeFailedPreparationChecksumEncoding(
      board_content_hash, workload_checksum, config, batch_identity, error_code,
      candidate_store_publication_committed, counters, attempted_columns);
}

internal::CpuCandidatePoolAlternativeResourceResultV1
internal::ExtractCpuCandidatePoolAlternativeResourcesV1(
    const routing::CpuRoute& route, const geometry_compiler::CompiledBoard& board,
    const routing::PlanarRouteRequest& expected_request, std::uint64_t expected_policy_identity,
    std::uint64_t maximum_edges) {
  return ResourcesForAuthenticatedRoute(route, board, expected_request, expected_policy_identity,
                                        maximum_edges);
}

#ifdef APGAR_CPU_CANDIDATE_POOL_PREPARATION_FAULT_TEST_VARIANT
void internal::SetCpuCandidatePoolPreparationFaultForTesting(
    CpuCandidatePoolPreparationFaultForTesting fault, std::size_t index) noexcept {
  {
    std::scoped_lock lock(g_fault_test_state.mutex);
    g_fault_test_state.worker_blocked = false;
    g_fault_test_state.release_worker = false;
  }
  g_fault_test_state.index.store(index);
  g_fault_test_state.fault.store(fault);
}

void internal::WaitForCpuCandidatePoolPreparationWorkerBlockForTesting() {
  std::unique_lock lock(g_fault_test_state.mutex);
  g_fault_test_state.condition.wait(lock, [] { return g_fault_test_state.worker_blocked; });
}

void internal::ReleaseCpuCandidatePoolPreparationWorkerBlockForTesting() noexcept {
  {
    std::scoped_lock lock(g_fault_test_state.mutex);
    g_fault_test_state.release_worker = true;
  }
  g_fault_test_state.condition.notify_all();
}
#endif

struct PersistentCpuCandidatePoolPreparer::Impl {
  static void SaturatingAdd(std::uint64_t amount, std::uint64_t* value) noexcept {
    *value = amount > std::numeric_limits<std::uint64_t>::max() - *value
                 ? std::numeric_limits<std::uint64_t>::max()
                 : *value + amount;
  }

  explicit Impl(std::uint32_t worker_count) {
    try {
      workers.reserve(worker_count);
      for (std::uint32_t index = 0; index < worker_count; ++index) {
#ifdef APGAR_CPU_CANDIDATE_POOL_PREPARATION_FAULT_TEST_VARIANT
        MaybeInjectThreadCreationFault(index);
#endif
        workers.emplace_back([this] { WorkerLoop(); });
      }
      std::scoped_lock lock(mutex);
      telemetry.workers_started = workers.size();
    } catch (...) {
      {
        std::scoped_lock lock(mutex);
        stop = true;
      }
      start.notify_all();
      for (std::thread& worker : workers) {
        if (worker.joinable()) {
          worker.join();
        }
      }
      throw;
    }
  }

  ~Impl() {
    {
      std::scoped_lock lock(mutex);
      stop = true;
    }
    start.notify_all();
    for (std::thread& worker : workers) {
      worker.join();
    }
  }

  void Run(std::size_t count, std::function<void(std::size_t)> operation) {
    if (count == 0) {
      return;
    }
    std::unique_lock lock(mutex);
    task = std::move(operation);
    next_job = 0;
    total_jobs = count;
    completed_jobs = 0;
    SaturatingAdd(count, &telemetry.jobs_dispatched);
    ++generation;
    start.notify_all();
    done.wait(lock, [this] { return completed_jobs == total_jobs; });
    task = {};
  }

  [[nodiscard]] PersistentCpuCandidatePoolTelemetry Telemetry() const noexcept {
    std::scoped_lock lock(mutex);
    return telemetry;
  }

  void InvocationStarted() noexcept {
    std::scoped_lock lock(mutex);
    SaturatingAdd(1, &telemetry.invocations_started);
  }

  void InvocationCompleted() noexcept {
    std::scoped_lock lock(mutex);
    SaturatingAdd(1, &telemetry.invocations_completed);
  }

  void WorkerLoop() {
    std::uint64_t observed_generation = 0;
    std::unique_lock lock(mutex);
    while (true) {
      start.wait(
          lock, [this, &observed_generation] { return stop || generation != observed_generation; });
      if (stop) {
        return;
      }
      observed_generation = generation;
      while (next_job < total_jobs) {
        const std::size_t job = next_job++;
        const std::function<void(std::size_t)>* current_task = &task;
        lock.unlock();
        (*current_task)(job);
        lock.lock();
        ++completed_jobs;
        if (completed_jobs == total_jobs) {
          done.notify_one();
        }
      }
    }
  }

  mutable std::mutex mutex;
  std::condition_variable start;
  std::condition_variable done;
  bool stop = false;
  std::uint64_t generation = 0;
  std::size_t next_job = 0;
  std::size_t total_jobs = 0;
  std::size_t completed_jobs = 0;
  std::function<void(std::size_t)> task;
  std::vector<std::thread> workers;
  PersistentCpuCandidatePoolTelemetry telemetry;
  std::mutex invocation_mutex;
};

PersistentCpuCandidatePoolPreparer::PersistentCpuCandidatePoolPreparer(
    PersistentCpuCandidatePoolPreparerConfig config, std::unique_ptr<Impl> impl) noexcept
    : config_(config), impl_(std::move(impl)) {}

PersistentCpuCandidatePoolPreparer::~PersistentCpuCandidatePoolPreparer() = default;

PersistentCpuCandidatePoolTelemetry PersistentCpuCandidatePoolPreparer::telemetry() const noexcept {
  return impl_->Telemetry();
}

PersistentCpuCandidatePoolPreparerResult CreatePersistentCpuCandidatePoolPreparer(
    const PersistentCpuCandidatePoolPreparerConfig& config) {
  if (config.schema_version != kPersistentCpuCandidatePoolPreparerSchemaVersion) {
    return Error(CpuCandidatePoolPreparationErrorCode::kUnsupportedSchema,
                 "allocator.cpu_candidate_pool.preparer_schema.v1",
                 "Persistent CPU preparer schema is unsupported");
  }
  if (config.worker_count == 0 || config.worker_count > kMaximumPersistentCpuCandidateWorkersV1) {
    return Error(CpuCandidatePoolPreparationErrorCode::kInvalidConfiguration,
                 "allocator.cpu_candidate_pool.worker_count.v1",
                 "Persistent CPU preparer worker count is outside the version-1 bound",
                 std::nullopt, config.worker_count, kMaximumPersistentCpuCandidateWorkersV1);
  }
  try {
    auto impl = std::make_unique<PersistentCpuCandidatePoolPreparer::Impl>(config.worker_count);
    return std::unique_ptr<PersistentCpuCandidatePoolPreparer>(
        new PersistentCpuCandidatePoolPreparer(config, std::move(impl)));
  } catch (const std::bad_alloc&) {
    return Error(CpuCandidatePoolPreparationErrorCode::kResourceExhausted,
                 "allocator.cpu_candidate_pool.preparer_memory.v1",
                 "Host allocation failed while creating persistent CPU workers");
  } catch (const std::system_error&) {
    return Error(CpuCandidatePoolPreparationErrorCode::kResourceExhausted,
                 "allocator.cpu_candidate_pool.preparer_thread.v1",
                 "The bounded persistent CPU worker set could not be created");
  } catch (const std::length_error&) {
    return Error(CpuCandidatePoolPreparationErrorCode::kResourceExhausted,
                 "allocator.cpu_candidate_pool.preparer_container.v1",
                 "Host container limits were exhausted while creating persistent CPU workers");
  }
}

template <bool CaptureOperationalProfile>
PreparedCpuCandidatePoolsResult PrepareInitialCpuCandidatePoolsImpl(
    PersistentCpuCandidatePoolPreparer& preparer, const board_ir::BoardSnapshot& board,
    const MultiNetWorkload& workload, const CpuCandidatePoolPreparationConfig& config,
    CpuCandidatePoolPreparationOperationalProfileV1* operational_profile) {
  ::apgar::internal::OperationalTimestamp<CaptureOperationalProfile, OperationalClock>
      validation_start;
  if constexpr (CaptureOperationalProfile) {
    *operational_profile = {};
    validation_start =
        ::apgar::internal::OperationalNow<CaptureOperationalProfile, OperationalClock>();
  }
  std::unique_lock invocation(preparer.impl_->invocation_mutex, std::try_to_lock);
  if (!invocation.owns_lock()) {
    return Error(CpuCandidatePoolPreparationErrorCode::kBusy,
                 "allocator.cpu_candidate_pool.non_reentrant.v1",
                 "A preparation is already active on this persistent CPU preparer");
  }
  preparer.impl_->InvocationStarted();
  struct Completion {
    PersistentCpuCandidatePoolPreparer::Impl* impl;
    ~Completion() { impl->InvocationCompleted(); }
  } completion{.impl = preparer.impl_.get()};
  try {
    if (config.schema_version != kCpuCandidatePoolPreparationSchemaVersion ||
        workload.schema_version() != kMultiNetWorkloadSchemaVersion) {
      return Error(CpuCandidatePoolPreparationErrorCode::kUnsupportedSchema,
                   "allocator.cpu_candidate_pool.schema.v2",
                   "Preparation or workload schema is unsupported");
    }
    if (!IsSupportedPoolSize(config.requested_candidates_per_net) ||
        config.step_surcharge_increment == 0 || config.bend_surcharge_increment == 0 ||
        config.resource_penalty_increment == 0 || !RouteLimitsAreValid(config.route_limits) ||
        config.limits.maximum_nets == 0 || config.limits.maximum_route_queries == 0 ||
        config.limits.maximum_route_queries > kMaximumCpuCandidatePoolQueriesV1 ||
        config.limits.maximum_total_route_work_units == 0 ||
        config.limits.maximum_total_route_work_units > kMaximumCpuCandidatePoolRouteWorkUnitsV1 ||
        config.limits.maximum_concurrent_route_records == 0 ||
        config.limits.maximum_concurrent_queue_entries == 0 ||
        config.limits.maximum_concurrent_reconstruction_states == 0 ||
        config.limits.maximum_policy_resource_entries == 0 ||
        config.limits.maximum_policy_resource_entries > 100'000'000 ||
        config.limits.maximum_retained_candidate_bytes == 0 ||
        config.limits.maximum_candidate_draft_bytes == 0 ||
        config.limits.maximum_generated_candidate_bytes == 0) {
      return Error(CpuCandidatePoolPreparationErrorCode::kInvalidConfiguration,
                   "allocator.cpu_candidate_pool.configuration.v2",
                   "CPU candidate preparation configuration is outside version-2 bounds");
    }
    const std::size_t net_count = workload.nets().size();
    if (net_count == 0) {
      return Error(CpuCandidatePoolPreparationErrorCode::kInvalidConfiguration,
                   "allocator.cpu_candidate_pool.empty_workload.v1",
                   "CPU candidate preparation requires at least one workload net");
    }
    if (board.content_hash() != workload.board_content_hash()) {
      return Error(CpuCandidatePoolPreparationErrorCode::kAssociationMismatch,
                   "allocator.cpu_candidate_pool.board_association.v1",
                   "Board and workload content hashes differ");
    }
    if (static_cast<UWide>(net_count) > config.limits.maximum_nets) {
      return Error(CpuCandidatePoolPreparationErrorCode::kInputBoundExceeded,
                   "allocator.cpu_candidate_pool.net_bound.v1",
                   "Workload net count exceeds the configured preparation bound", std::nullopt,
                   net_count, config.limits.maximum_nets);
    }
    const UWide requested_columns =
        static_cast<UWide>(net_count) * config.requested_candidates_per_net;
    if (requested_columns > config.limits.maximum_route_queries ||
        requested_columns > kMaximumCpuCandidatePoolQueriesV1) {
      return Error(CpuCandidatePoolPreparationErrorCode::kInputBoundExceeded,
                   "allocator.cpu_candidate_pool.query_bound.v1",
                   "Requested candidate columns exceed the configured query bound", std::nullopt,
                   NarrowWitness(requested_columns), config.limits.maximum_route_queries);
    }
    const UWide route_work = requested_columns * config.route_limits.maximum_work_units;
    if (route_work > config.limits.maximum_total_route_work_units) {
      return Error(CpuCandidatePoolPreparationErrorCode::kWorkBoundExceeded,
                   "allocator.cpu_candidate_pool.route_work_bound.v1",
                   "Conservative aggregate CPU route work exceeds the configured bound",
                   std::nullopt, NarrowWitness(route_work),
                   config.limits.maximum_total_route_work_units);
    }
    const UWide concurrent_records = static_cast<UWide>(preparer.config().worker_count) *
                                     config.route_limits.maximum_record_count;
    if (concurrent_records > config.limits.maximum_concurrent_route_records) {
      return Error(CpuCandidatePoolPreparationErrorCode::kWorkBoundExceeded,
                   "allocator.cpu_candidate_pool.concurrent_record_bound.v1",
                   "Concurrent CPU A* records exceed the configured host-memory proxy bound",
                   std::nullopt, NarrowWitness(concurrent_records),
                   config.limits.maximum_concurrent_route_records);
    }
    const UWide concurrent_queue =
        static_cast<UWide>(preparer.config().worker_count) * config.route_limits.maximum_queue_size;
    if (concurrent_queue > config.limits.maximum_concurrent_queue_entries) {
      return Error(CpuCandidatePoolPreparationErrorCode::kWorkBoundExceeded,
                   "allocator.cpu_candidate_pool.concurrent_queue_bound.v1",
                   "Concurrent CPU A* queue entries exceed the configured host-memory proxy bound",
                   std::nullopt, NarrowWitness(concurrent_queue),
                   config.limits.maximum_concurrent_queue_entries);
    }
    const UWide concurrent_reconstruction = static_cast<UWide>(preparer.config().worker_count) *
                                            config.route_limits.maximum_reconstruction_states;
    if (concurrent_reconstruction > config.limits.maximum_concurrent_reconstruction_states) {
      return Error(
          CpuCandidatePoolPreparationErrorCode::kWorkBoundExceeded,
          "allocator.cpu_candidate_pool.concurrent_reconstruction_bound.v1",
          "Concurrent CPU A* reconstruction states exceed the configured host-memory proxy bound",
          std::nullopt, NarrowWitness(concurrent_reconstruction),
          config.limits.maximum_concurrent_reconstruction_states);
    }

    UWide policy_entries = 0;
    UWide maximum_policy_entries_per_candidate = 0;
    for (const PreparedNetRoutingContext& context : workload.nets()) {
      if (context.compiled_board.source_board_content_hash() != board.content_hash() ||
          context.compiled_board.compiler_profile_fingerprint() !=
              workload.compiler_profile_fingerprint() ||
          context.compiled_board.compiler_version() != workload.geometry_compiler_version() ||
          context.request.net != context.routing_profile.net ||
          context.routing_profile_fingerprint !=
              routing::FingerprintRoutingProfile(context.routing_profile) ||
          !routing::CandidateGenerationPolicyShapeIsWithinV1Bounds(
              context.request.candidate_policy)) {
        return Error(CpuCandidatePoolPreparationErrorCode::kAssociationMismatch,
                     "allocator.cpu_candidate_pool.workload_context.v1",
                     "A workload context is not authentic or has an unbounded base policy",
                     context.request.net);
      }
      const UWide base_entries = context.request.candidate_policy.banned_resources.size() +
                                 context.request.candidate_policy.resource_penalties.size();
      policy_entries += static_cast<UWide>(config.requested_candidates_per_net) * base_entries +
                        (config.requested_candidates_per_net - 1U);
      maximum_policy_entries_per_candidate =
          std::max(maximum_policy_entries_per_candidate, base_entries + 1U);
    }
    if (policy_entries > config.limits.maximum_policy_resource_entries) {
      return Error(CpuCandidatePoolPreparationErrorCode::kWorkBoundExceeded,
                   "allocator.cpu_candidate_pool.policy_entry_bound.v1",
                   "Conservative alternative-policy entries exceed the configured bound",
                   std::nullopt, NarrowWitness(policy_entries),
                   config.limits.maximum_policy_resource_entries);
    }

    auto candidate_store = std::make_unique<candidates::CandidateStore>(config.store_config);
    if (!candidate_store->valid() ||
        config.store_config.maximum_candidates_per_net < config.requested_candidates_per_net ||
        config.store_config.maximum_admission_items_per_transaction < requested_columns ||
        config.store_config.maximum_rejection_items_per_transaction < requested_columns ||
        config.store_config.maximum_rejection_records < requested_columns ||
        config.store_config.maximum_expected_pools_per_invocation < net_count) {
      return Error(CpuCandidatePoolPreparationErrorCode::kInvalidConfiguration,
                   "allocator.cpu_candidate_pool.store_capacity.v1",
                   "Fresh CandidateStore configuration cannot publish the complete invocation");
    }
    const UWide draft_byte_upper = CandidateDraftLogicalByteUpperBound(
        config.route_limits, maximum_policy_entries_per_candidate);
    if (draft_byte_upper > config.limits.maximum_candidate_draft_bytes) {
      return Error(CpuCandidatePoolPreparationErrorCode::kInputBoundExceeded,
                   "allocator.cpu_candidate_pool.draft_byte_bound.v1",
                   "One candidate draft can exceed the configured pre-generation byte bound",
                   std::nullopt, NarrowWitness(draft_byte_upper),
                   config.limits.maximum_candidate_draft_bytes);
    }
    const UWide retained_candidate_bytes =
        static_cast<UWide>(net_count) * config.store_config.maximum_candidate_bytes_per_net;
    const UWide base_resource_bytes =
        static_cast<UWide>(net_count) * config.route_limits.maximum_reconstruction_states * 40U;
    const UWide generated_candidate_bytes =
        requested_columns * config.limits.maximum_candidate_draft_bytes + base_resource_bytes;
    const bool retained_bytes_exceeded =
        retained_candidate_bytes > config.limits.maximum_retained_candidate_bytes;
    if (retained_bytes_exceeded ||
        generated_candidate_bytes > config.limits.maximum_generated_candidate_bytes) {
      return Error(CpuCandidatePoolPreparationErrorCode::kInputBoundExceeded,
                   "allocator.cpu_candidate_pool.output_byte_bound.v1",
                   "Conservative candidate output bytes exceed the configured preparation bound",
                   std::nullopt,
                   NarrowWitness(retained_bytes_exceeded ? retained_candidate_bytes
                                                         : generated_candidate_bytes),
                   retained_bytes_exceeded ? config.limits.maximum_retained_candidate_bytes
                                           : config.limits.maximum_generated_candidate_bytes);
    }

    const std::uint64_t batch_identity = BatchIdentity(board, workload, config);
    const std::size_t column_count = static_cast<std::size_t>(requested_columns);
    std::vector<WorkColumn> work(column_count);
    OperationalColumnElapsed<CaptureOperationalProfile> operational_column_elapsed;
    if constexpr (CaptureOperationalProfile) {
      operational_column_elapsed.resize(column_count);
    }
    std::vector<CpuCandidatePoolColumnRecord> columns(column_count);
    std::vector<CpuCandidatePoolAttemptedColumnRecord> attempted_columns(column_count);
    for (std::size_t net_index = 0; net_index < net_count; ++net_index) {
      const PreparedNetRoutingContext& context = workload.nets()[net_index];
      for (std::uint32_t ordinal = 0; ordinal < config.requested_candidates_per_net; ++ordinal) {
        const std::size_t flat = net_index * config.requested_candidates_per_net + ordinal;
        work[flat].context = &context;
        columns[flat] = CpuCandidatePoolColumnRecord{
            .net = context.request.net,
            .candidate_ordinal = ordinal,
            .policy_identity = 0,
            .batch_identity = batch_identity,
            .query_identity = flat + 1U,
            .route_work_units = 0,
            .outcome = CpuCandidatePoolColumnOutcome::kAdmissionRejected,
            .candidate_id = std::nullopt,
            .candidate_payload_checksum = std::nullopt,
            .rejection_code = std::nullopt,
        };
        attempted_columns[flat] = CpuCandidatePoolAttemptedColumnRecord{
            .net = context.request.net,
            .candidate_ordinal = ordinal,
            .policy_identity = 0,
            .batch_identity = batch_identity,
            .query_identity = flat + 1U,
            .route_work_units = 0,
            .state = CpuCandidatePoolAttemptState::kQueryInFlight,
            .route_failure_code = std::nullopt,
        };
      }
      routing::CandidateGenerationPolicy base = context.request.candidate_policy;
      base.deterministic_seed = internal::ComputeCpuCandidatePoolNetSeedV1(
          config.deterministic_seed, workload.workload_checksum(), context.request.net);
      base.candidate_ordinal = 0;
      routing::CandidatePolicyResult normalized =
          routing::NormalizeCandidateGenerationPolicy(context.compiled_board, std::move(base));
      if (const auto* failure = std::get_if<routing::CandidatePolicyError>(&normalized);
          failure != nullptr) {
        return Error(CpuCandidatePoolPreparationErrorCode::kPolicySynthesis,
                     "allocator.cpu_candidate_pool.base_policy.v1",
                     "A workload base policy cannot be normalized", context.request.net);
      }
      work[net_index * config.requested_candidates_per_net].policy =
          std::get<routing::NormalizedCandidateGenerationPolicy>(std::move(normalized));
    }

    if constexpr (CaptureOperationalProfile) {
      operational_profile->validation_and_scheduling_wall_nanoseconds =
          OperationalElapsed(validation_start);
      operational_profile->base_jobs_dispatched = net_count;
    }
    ::apgar::internal::OperationalTimestamp<CaptureOperationalProfile, OperationalClock>
        base_wave_start;
    if constexpr (CaptureOperationalProfile) {
      base_wave_start =
          ::apgar::internal::OperationalNow<CaptureOperationalProfile, OperationalClock>();
    }
    preparer.impl_->Run(net_count, [&](std::size_t net_index) {
      const std::size_t flat = net_index * config.requested_candidates_per_net;
      WorkColumn& column = work[flat];
      ScopedOperationalElapsed<CaptureOperationalProfile> operational_timer(
          operational_column_elapsed, flat);
      try {
#ifdef APGAR_CPU_CANDIDATE_POOL_PREPARATION_FAULT_TEST_VARIANT
        MaybeInjectWorkerFault(net_index);
#endif
        column.executed = true;
        columns[flat].policy_identity = column.policy->identity;
        attempted_columns[flat].policy_identity = column.policy->identity;
        column.request = RequestWithPolicy(column.context->request, column.policy->policy);
        column.query_started = true;
        routing::CpuRouteResult route_result = routing::RouteWithCpuAStar(
            board, column.context->compiled_board, *column.request, config.route_limits);
        if (auto* failure = std::get_if<routing::RouteFailure>(&route_result); failure != nullptr) {
          columns[flat].route_work_units =
              failure->telemetry.has_value() ? failure->telemetry->work_units : 0;
          attempted_columns[flat].route_work_units = columns[flat].route_work_units;
          attempted_columns[flat].state = CpuCandidatePoolAttemptState::kRouteFailed;
          attempted_columns[flat].route_failure_code = failure->code;
          if (failure->code == routing::RouteFailureCode::kDisconnected ||
              failure->code == routing::RouteFailureCode::kUnsupportedLayerTransition ||
              failure->code == routing::RouteFailureCode::kUnsupportedPolicy) {
            columns[flat].outcome = failure->code == routing::RouteFailureCode::kDisconnected
                                        ? CpuCandidatePoolColumnOutcome::kRouteDisconnected
                                        : CpuCandidatePoolColumnOutcome::kRouteUnsupported;
            column.rejection = RouteFailureRejection(
                candidates::AssociationsFor(board, column.context->compiled_board), *failure,
                column.context->request.net, *column.policy,
                candidates::CandidateSchedulingIdentity{.batch_identity = batch_identity,
                                                        .query_identity = flat + 1U});
            columns[flat].rejection_code = column.rejection->code;
            return;
          }
          column.fatal_error =
              Error(failure->code == routing::RouteFailureCode::kWorkBoundExceeded
                        ? CpuCandidatePoolPreparationErrorCode::kWorkBoundExceeded
                    : failure->code == routing::RouteFailureCode::kResourceExhausted
                        ? CpuCandidatePoolPreparationErrorCode::kResourceExhausted
                        : CpuCandidatePoolPreparationErrorCode::kCandidateGeneration,
                    "allocator.cpu_candidate_pool.base_route.v1",
                    "Bounded CPU A* failed while generating a base candidate",
                    column.context->request.net);
          return;
        }
        routing::CpuRoute route = std::get<routing::CpuRoute>(std::move(route_result));
        columns[flat].route_work_units = route.telemetry.work_units;
        attempted_columns[flat].route_work_units = route.telemetry.work_units;
        attempted_columns[flat].state = CpuCandidatePoolAttemptState::kRouteSucceeded;
        auto resources_result = internal::ExtractCpuCandidatePoolAlternativeResourcesV1(
            route, column.context->compiled_board, *column.request, column.policy->identity,
            config.route_limits.maximum_reconstruction_states);
        if (auto* failure = std::get_if<CpuCandidatePoolPreparationError>(&resources_result);
            failure != nullptr) {
          column.fatal_error = std::move(*failure);
          return;
        }
        column.base_resources =
            std::get<std::vector<routing::EdgeResourceKey>>(std::move(resources_result));
        candidates::CandidateDraftBuildResult draft =
            candidates::BuildGeneratedCandidateFromCpuRoute(
                board, column.context->compiled_board, *column.request, *column.policy, route,
                candidates::CandidateSchedulingIdentity{.batch_identity = batch_identity,
                                                        .query_identity = flat + 1U});
        if (auto* rejection = std::get_if<candidates::CandidateRejection>(&draft);
            rejection != nullptr) {
          columns[flat].outcome = CpuCandidatePoolColumnOutcome::kBuildRejected;
          columns[flat].candidate_id = rejection->candidate_id;
          columns[flat].candidate_payload_checksum = rejection->candidate_payload_checksum;
          columns[flat].rejection_code = rejection->code;
          column.rejection = std::move(*rejection);
        } else {
          candidates::GeneratedRouteCandidate generated =
              std::get<candidates::GeneratedRouteCandidate>(std::move(draft));
          if (generated.logical_bytes > config.limits.maximum_candidate_draft_bytes) {
            column.fatal_error =
                Error(CpuCandidatePoolPreparationErrorCode::kInternalInvariant,
                      "allocator.cpu_candidate_pool.draft_byte_replay.v1",
                      "A generated base draft exceeded its conservative pre-generation byte bound",
                      column.context->request.net, generated.logical_bytes,
                      config.limits.maximum_candidate_draft_bytes);
            return;
          }
          column.draft = std::move(generated);
          columns[flat].candidate_id = column.draft->id;
          columns[flat].candidate_payload_checksum = column.draft->payload_checksum;
        }
      } catch (const std::bad_alloc&) {
        column.fatal_error =
            Error(CpuCandidatePoolPreparationErrorCode::kResourceExhausted,
                  "allocator.cpu_candidate_pool.worker_memory.v1",
                  "A base CPU worker exhausted bounded host memory", column.context->request.net);
      } catch (const std::length_error&) {
        column.fatal_error =
            Error(CpuCandidatePoolPreparationErrorCode::kResourceExhausted,
                  "allocator.cpu_candidate_pool.worker_container.v1",
                  "A base CPU worker exhausted host container limits", column.context->request.net);
      } catch (...) {
        column.fatal_error =
            Error(CpuCandidatePoolPreparationErrorCode::kInternalInvariant,
                  "allocator.cpu_candidate_pool.worker_exception.v1",
                  "A base CPU worker raised an unexpected exception", column.context->request.net);
      }
    });
    if constexpr (CaptureOperationalProfile) {
      operational_profile->base_worker_wave_wall_nanoseconds = OperationalElapsed(base_wave_start);
    }
    if (CpuCandidatePoolPreparationError* fatal = FirstFatal(work); fatal != nullptr) {
      return WithFailedPreparationObservation(std::move(*fatal), board, workload, config,
                                              batch_identity, work, std::move(attempted_columns));
    }

    bool candidate_store_publication_committed = false;
    const auto fail_after_queries = [&](CpuCandidatePoolPreparationError error) {
      return WithFailedPreparationObservation(
          std::move(error), board, workload, config, batch_identity, work,
          std::move(attempted_columns), candidate_store_publication_committed,
          candidate_store_publication_committed ? std::move(candidate_store) : nullptr);
    };
    try {
#ifdef APGAR_CPU_CANDIDATE_POOL_PREPARATION_FAULT_TEST_VARIANT
      MaybeInjectPreparationStageFault(
          internal::CpuCandidatePoolPreparationFaultForTesting::kPostBaseWaveBadAlloc);
      MaybeInjectPreparationStageFault(
          internal::CpuCandidatePoolPreparationFaultForTesting::kPostBaseWaveUnexpectedException);
#endif
      ::apgar::internal::OperationalTimestamp<CaptureOperationalProfile, OperationalClock>
          alternative_policy_start;
      if constexpr (CaptureOperationalProfile) {
        alternative_policy_start =
            ::apgar::internal::OperationalNow<CaptureOperationalProfile, OperationalClock>();
      }
      std::vector<std::size_t> alternative_columns;
      alternative_columns.reserve(column_count - net_count);
      for (std::size_t net_index = 0; net_index < net_count; ++net_index) {
        const std::size_t base_flat = net_index * config.requested_candidates_per_net;
        WorkColumn& base = work[base_flat];
        if (!base.base_resources.has_value()) {
          const bool disconnected =
              columns[base_flat].outcome == CpuCandidatePoolColumnOutcome::kRouteDisconnected;
          for (std::uint32_t ordinal = 1; ordinal < config.requested_candidates_per_net;
               ++ordinal) {
            const std::size_t flat = base_flat + ordinal;
            columns[flat].query_identity = 0;
            columns[flat].outcome =
                disconnected ? CpuCandidatePoolColumnOutcome::kSkippedAfterDisconnectedProof
                             : CpuCandidatePoolColumnOutcome::kSkippedAfterUnsupportedProof;
          }
          continue;
        }
        routing::DeterministicAlternativePolicySchedule schedule{
            .candidate_count = config.requested_candidates_per_net,
            .step_surcharge_increment = config.step_surcharge_increment,
            .bend_surcharge_increment = config.bend_surcharge_increment,
            .resource_penalty_increment = config.resource_penalty_increment,
            .alternative_resources = std::move(*base.base_resources),
        };
        routing::CandidatePolicyBatchResult policies_result =
            routing::BuildDeterministicAlternativePolicies(
                base.context->compiled_board, base.policy->policy, std::move(schedule));
        if (const auto* failure = std::get_if<routing::CandidatePolicyError>(&policies_result);
            failure != nullptr) {
          return fail_after_queries(
              Error(CpuCandidatePoolPreparationErrorCode::kPolicySynthesis,
                    "allocator.cpu_candidate_pool.alternative_policy.v1",
                    "A reached base route cannot synthesize its alternative policy schedule",
                    base.context->request.net));
        }
        std::vector<routing::NormalizedCandidateGenerationPolicy> policies =
            std::get<std::vector<routing::NormalizedCandidateGenerationPolicy>>(
                std::move(policies_result));
        if (policies.size() != config.requested_candidates_per_net || policies[0] != *base.policy) {
          return fail_after_queries(
              Error(CpuCandidatePoolPreparationErrorCode::kInternalInvariant,
                    "allocator.cpu_candidate_pool.policy_replay.v1",
                    "Alternative policy synthesis did not replay the exact base policy",
                    base.context->request.net));
        }
        for (std::uint32_t ordinal = 1; ordinal < config.requested_candidates_per_net; ++ordinal) {
          const std::size_t flat = base_flat + ordinal;
          work[flat].policy = std::move(policies[ordinal]);
          columns[flat].policy_identity = work[flat].policy->identity;
          attempted_columns[flat].policy_identity = work[flat].policy->identity;
          alternative_columns.push_back(flat);
        }
      }

      if constexpr (CaptureOperationalProfile) {
        operational_profile->alternative_policy_wall_nanoseconds =
            OperationalElapsed(alternative_policy_start);
        operational_profile->alternative_jobs_dispatched = alternative_columns.size();
      }
      ::apgar::internal::OperationalTimestamp<CaptureOperationalProfile, OperationalClock>
          alternative_wave_start;
      if constexpr (CaptureOperationalProfile) {
        alternative_wave_start =
            ::apgar::internal::OperationalNow<CaptureOperationalProfile, OperationalClock>();
      }
      preparer.impl_->Run(alternative_columns.size(), [&](std::size_t job_index) {
        const std::size_t flat = alternative_columns[job_index];
        WorkColumn& column = work[flat];
        ScopedOperationalElapsed<CaptureOperationalProfile> operational_timer(
            operational_column_elapsed, flat);
        try {
#ifdef APGAR_CPU_CANDIDATE_POOL_PREPARATION_FAULT_TEST_VARIANT
          MaybeInjectWorkerFault(job_index);
#endif
          column.executed = true;
          column.request = RequestWithPolicy(column.context->request, column.policy->policy);
          column.query_started = true;
          routing::CpuRouteResult route_result = routing::RouteWithCpuAStar(
              board, column.context->compiled_board, *column.request, config.route_limits);
          if (auto* failure = std::get_if<routing::RouteFailure>(&route_result);
              failure != nullptr) {
            columns[flat].route_work_units =
                failure->telemetry.has_value() ? failure->telemetry->work_units : 0;
            attempted_columns[flat].route_work_units = columns[flat].route_work_units;
            attempted_columns[flat].state = CpuCandidatePoolAttemptState::kRouteFailed;
            attempted_columns[flat].route_failure_code = failure->code;
            if (failure->code == routing::RouteFailureCode::kDisconnected ||
                failure->code == routing::RouteFailureCode::kUnsupportedLayerTransition ||
                failure->code == routing::RouteFailureCode::kUnsupportedPolicy) {
              columns[flat].outcome = failure->code == routing::RouteFailureCode::kDisconnected
                                          ? CpuCandidatePoolColumnOutcome::kRouteDisconnected
                                          : CpuCandidatePoolColumnOutcome::kRouteUnsupported;
              column.rejection = RouteFailureRejection(
                  candidates::AssociationsFor(board, column.context->compiled_board), *failure,
                  column.context->request.net, *column.policy,
                  candidates::CandidateSchedulingIdentity{.batch_identity = batch_identity,
                                                          .query_identity = flat + 1U});
              columns[flat].rejection_code = column.rejection->code;
              return;
            }
            column.fatal_error =
                Error(failure->code == routing::RouteFailureCode::kWorkBoundExceeded
                          ? CpuCandidatePoolPreparationErrorCode::kWorkBoundExceeded
                      : failure->code == routing::RouteFailureCode::kResourceExhausted
                          ? CpuCandidatePoolPreparationErrorCode::kResourceExhausted
                          : CpuCandidatePoolPreparationErrorCode::kCandidateGeneration,
                      "allocator.cpu_candidate_pool.alternative_route.v1",
                      "Bounded CPU A* failed while generating an alternative candidate",
                      column.context->request.net);
            return;
          }
          routing::CpuRoute route = std::get<routing::CpuRoute>(std::move(route_result));
          columns[flat].route_work_units = route.telemetry.work_units;
          attempted_columns[flat].route_work_units = route.telemetry.work_units;
          attempted_columns[flat].state = CpuCandidatePoolAttemptState::kRouteSucceeded;
          candidates::CandidateDraftBuildResult draft =
              candidates::BuildGeneratedCandidateFromCpuRoute(
                  board, column.context->compiled_board, *column.request, *column.policy, route,
                  candidates::CandidateSchedulingIdentity{.batch_identity = batch_identity,
                                                          .query_identity = flat + 1U});
          if (auto* rejection = std::get_if<candidates::CandidateRejection>(&draft);
              rejection != nullptr) {
            columns[flat].outcome = CpuCandidatePoolColumnOutcome::kBuildRejected;
            columns[flat].candidate_id = rejection->candidate_id;
            columns[flat].candidate_payload_checksum = rejection->candidate_payload_checksum;
            columns[flat].rejection_code = rejection->code;
            column.rejection = std::move(*rejection);
          } else {
            candidates::GeneratedRouteCandidate generated =
                std::get<candidates::GeneratedRouteCandidate>(std::move(draft));
            if (generated.logical_bytes > config.limits.maximum_candidate_draft_bytes) {
              column.fatal_error =
                  Error(CpuCandidatePoolPreparationErrorCode::kInternalInvariant,
                        "allocator.cpu_candidate_pool.draft_byte_replay.v1",
                        "An alternative draft exceeded its conservative pre-generation byte bound",
                        column.context->request.net, generated.logical_bytes,
                        config.limits.maximum_candidate_draft_bytes);
              return;
            }
            column.draft = std::move(generated);
            columns[flat].candidate_id = column.draft->id;
            columns[flat].candidate_payload_checksum = column.draft->payload_checksum;
          }
        } catch (const std::bad_alloc&) {
          column.fatal_error = Error(CpuCandidatePoolPreparationErrorCode::kResourceExhausted,
                                     "allocator.cpu_candidate_pool.worker_memory.v1",
                                     "An alternative CPU worker exhausted bounded host memory",
                                     column.context->request.net);
        } catch (const std::length_error&) {
          column.fatal_error = Error(CpuCandidatePoolPreparationErrorCode::kResourceExhausted,
                                     "allocator.cpu_candidate_pool.worker_container.v1",
                                     "An alternative CPU worker exhausted host container limits",
                                     column.context->request.net);
        } catch (...) {
          column.fatal_error = Error(CpuCandidatePoolPreparationErrorCode::kInternalInvariant,
                                     "allocator.cpu_candidate_pool.worker_exception.v1",
                                     "An alternative CPU worker raised an unexpected exception",
                                     column.context->request.net);
        }
      });
      if constexpr (CaptureOperationalProfile) {
        operational_profile->alternative_worker_wave_wall_nanoseconds =
            OperationalElapsed(alternative_wave_start);
        for (std::uint64_t elapsed : operational_column_elapsed) {
          AccumulateOperationalElapsed(
              &operational_profile->route_and_candidate_build_worker_sum_nanoseconds, elapsed);
        }
      }
      if (CpuCandidatePoolPreparationError* fatal = FirstFatal(work); fatal != nullptr) {
        return WithFailedPreparationObservation(std::move(*fatal), board, workload, config,
                                                batch_identity, work, std::move(attempted_columns));
      }

      UWide actual_route_work = 0;
      for (const CpuCandidatePoolColumnRecord& column : columns) {
        actual_route_work += column.route_work_units;
        if (actual_route_work > config.limits.maximum_total_route_work_units) {
          return fail_after_queries(Error(
              CpuCandidatePoolPreparationErrorCode::kInternalInvariant,
              "allocator.cpu_candidate_pool.route_work_replay.v2",
              "Actual CPU route work exceeded its accepted aggregate preflight bound", column.net,
              NarrowWitness(actual_route_work), config.limits.maximum_total_route_work_units));
        }
      }
      const std::uint64_t actual_route_work_units = static_cast<std::uint64_t>(actual_route_work);

      std::vector<candidates::CandidateStoreExpectedPool> expected_pools;
      expected_pools.reserve(net_count);
      for (const PreparedNetRoutingContext& context : workload.nets()) {
        expected_pools.push_back(candidates::CandidateStoreExpectedPool{
            .net = context.request.net,
            .associations = candidates::AssociationsFor(board, context.compiled_board),
            .candidates = {},
        });
      }
      std::vector<candidates::CandidateInvocationItem> invocation_items;
      invocation_items.reserve(column_count);
      for (WorkColumn& column : work) {
        if (!column.executed) {
          continue;
        }
        if (column.draft.has_value()) {
          invocation_items.emplace_back(candidates::CandidateInvocationGeneratedItem{
              .compiled_board = std::cref(column.context->compiled_board),
              .request = std::move(*column.request),
              .generated = std::move(*column.draft),
          });
        } else if (column.rejection.has_value()) {
          invocation_items.emplace_back(std::move(*column.rejection));
        } else {
          return fail_after_queries(Error(CpuCandidatePoolPreparationErrorCode::kInternalInvariant,
                                          "allocator.cpu_candidate_pool.worker_result.v1",
                                          "An executed CPU column produced no publication item",
                                          column.context->request.net));
        }
      }
      ::apgar::internal::OperationalTimestamp<CaptureOperationalProfile, OperationalClock>
          publication_start;
      if constexpr (CaptureOperationalProfile) {
        publication_start =
            ::apgar::internal::OperationalNow<CaptureOperationalProfile, OperationalClock>();
      }
      candidates::CandidateStoreInvocationAdmissionResult publication_result =
          candidate_store->AdmitInvocationIfSourcePoolsMatch(board, std::move(expected_pools),
                                                             std::move(invocation_items));
      if constexpr (CaptureOperationalProfile) {
        operational_profile->exact_admission_and_store_publication_wall_nanoseconds =
            OperationalElapsed(publication_start);
      }
      if (const auto* failure = std::get_if<candidates::CandidateStoreError>(&publication_result);
          failure != nullptr) {
        return fail_after_queries(
            Error(failure->code == candidates::CandidateStoreErrorCode::kResourceExhausted
                      ? CpuCandidatePoolPreparationErrorCode::kResourceExhausted
                      : CpuCandidatePoolPreparationErrorCode::kCandidateStore,
                  "allocator.cpu_candidate_pool.store_publication.v1",
                  "Fresh CandidateStore rejected the atomic CPU preparation invocation"));
      }
      candidate_store_publication_committed = true;
#ifdef APGAR_CPU_CANDIDATE_POOL_PREPARATION_FAULT_TEST_VARIANT
      MaybeInjectPreparationStageFault(
          internal::CpuCandidatePoolPreparationFaultForTesting::kPostPublicationBadAlloc);
      MaybeInjectPreparationStageFault(internal::CpuCandidatePoolPreparationFaultForTesting::
                                           kPostPublicationUnexpectedException);
#endif
      ::apgar::internal::OperationalTimestamp<CaptureOperationalProfile, OperationalClock>
          correlation_start;
      if constexpr (CaptureOperationalProfile) {
        correlation_start =
            ::apgar::internal::OperationalNow<CaptureOperationalProfile, OperationalClock>();
      }
      std::vector<candidates::CandidateStoreAdmissionResult> publication =
          std::get<std::vector<candidates::CandidateStoreAdmissionResult>>(
              std::move(publication_result));
      for (const candidates::CandidateStoreAdmissionResult& result : publication) {
        std::uint64_t query_identity = 0;
        if (const auto* stored = std::get_if<candidates::StoredCandidate>(&result);
            stored != nullptr) {
          query_identity = (*stored)->data().provenance.query_identity;
        } else {
          query_identity =
              std::get<candidates::CandidateRejection>(result).provenance.query_identity;
        }
        if (query_identity == 0 || query_identity > columns.size() ||
            !work[query_identity - 1U].executed) {
          return fail_after_queries(
              Error(CpuCandidatePoolPreparationErrorCode::kInternalInvariant,
                    "allocator.cpu_candidate_pool.publication_correlation.v1",
                    "CandidateStore returned an outcome without its canonical CPU column"));
        }
        CpuCandidatePoolColumnRecord& column = columns[query_identity - 1U];
        if (const auto* stored = std::get_if<candidates::StoredCandidate>(&result);
            stored != nullptr) {
          column.outcome = CpuCandidatePoolColumnOutcome::kAdmitted;
          column.candidate_id = (*stored)->id();
          column.candidate_payload_checksum = (*stored)->data().payload_checksum;
          column.rejection_code.reset();
        } else {
          const candidates::CandidateRejection& rejection =
              std::get<candidates::CandidateRejection>(result);
          column.candidate_id = rejection.candidate_id;
          column.candidate_payload_checksum = rejection.candidate_payload_checksum;
          column.rejection_code = rejection.code;
          if (IsDuplicateCode(rejection.code)) {
            column.outcome = CpuCandidatePoolColumnOutcome::kDuplicate;
          } else if (column.outcome != CpuCandidatePoolColumnOutcome::kRouteDisconnected &&
                     column.outcome != CpuCandidatePoolColumnOutcome::kRouteUnsupported &&
                     column.outcome != CpuCandidatePoolColumnOutcome::kBuildRejected) {
            column.outcome = CpuCandidatePoolColumnOutcome::kAdmissionRejected;
          }
        }
      }

      CpuCandidatePoolPreparationCounters counters;
      counters.requested_columns = column_count;
      counters.route_work_units = actual_route_work_units;
      std::vector<CandidatePool> pools;
      pools.reserve(net_count);
      for (const PreparedNetRoutingContext& context : workload.nets()) {
        std::vector<candidates::StoredCandidate> retained =
            candidate_store->Enumerate(context.request.net);
        counters.retained_candidates += retained.size();
        pools.push_back(
            CandidatePool{.net = context.request.net, .candidates = std::move(retained)});
      }
      std::ranges::sort(pools, [](const CandidatePool& left, const CandidatePool& right) {
        return NetBefore(left.net, right.net);
      });
      for (const WorkColumn& column : work) {
        if (column.executed) {
          ++counters.executed_route_queries;
        }
      }
      for (const CpuCandidatePoolColumnRecord& column : columns) {
        switch (column.outcome) {
          case CpuCandidatePoolColumnOutcome::kAdmitted:
            ++counters.successful_routes;
            ++counters.built_candidates;
            ++counters.admitted_candidates;
            break;
          case CpuCandidatePoolColumnOutcome::kDuplicate:
            ++counters.successful_routes;
            ++counters.built_candidates;
            ++counters.duplicate_candidates;
            ++counters.rejected_columns;
            break;
          case CpuCandidatePoolColumnOutcome::kRouteDisconnected:
            ++counters.disconnected_proofs;
            ++counters.rejected_columns;
            break;
          case CpuCandidatePoolColumnOutcome::kRouteUnsupported:
            ++counters.unsupported_proofs;
            ++counters.rejected_columns;
            break;
          case CpuCandidatePoolColumnOutcome::kSkippedAfterDisconnectedProof:
          case CpuCandidatePoolColumnOutcome::kSkippedAfterUnsupportedProof:
            ++counters.skipped_columns;
            ++counters.rejected_columns;
            break;
          case CpuCandidatePoolColumnOutcome::kBuildRejected:
            ++counters.successful_routes;
            ++counters.rejected_columns;
            break;
          case CpuCandidatePoolColumnOutcome::kAdmissionRejected:
            ++counters.successful_routes;
            ++counters.built_candidates;
            ++counters.rejected_columns;
            break;
        }
      }
      const std::uint64_t checksum =
          PreparationChecksum(config, batch_identity, counters, columns, pools);
      if constexpr (CaptureOperationalProfile) {
        operational_profile->publication_correlation_and_pool_materialization_wall_nanoseconds =
            OperationalElapsed(correlation_start);
        operational_profile->component_wall_nanoseconds = OperationalElapsed(validation_start);
        const UWide classified =
            static_cast<UWide>(operational_profile->validation_and_scheduling_wall_nanoseconds) +
            operational_profile->base_worker_wave_wall_nanoseconds +
            operational_profile->alternative_policy_wall_nanoseconds +
            operational_profile->alternative_worker_wave_wall_nanoseconds +
            operational_profile->exact_admission_and_store_publication_wall_nanoseconds +
            operational_profile->publication_correlation_and_pool_materialization_wall_nanoseconds;
        operational_profile->unclassified_serial_wall_nanoseconds =
            classified <= operational_profile->component_wall_nanoseconds
                ? operational_profile->component_wall_nanoseconds -
                      static_cast<std::uint64_t>(classified)
                : 0;
      }
      return PreparedCpuCandidatePools(config, batch_identity, counters, std::move(columns),
                                       std::move(pools), std::move(candidate_store), checksum);
    } catch (const std::bad_alloc&) {
      return fail_after_queries(Error(CpuCandidatePoolPreparationErrorCode::kResourceExhausted,
                                      "allocator.cpu_candidate_pool.post_query_memory.v2",
                                      "Host allocation failed after CPU routing work began"));
    } catch (const std::length_error&) {
      return fail_after_queries(
          Error(CpuCandidatePoolPreparationErrorCode::kResourceExhausted,
                "allocator.cpu_candidate_pool.post_query_container.v2",
                "Host container limits were exhausted after CPU routing work began"));
    } catch (...) {
      return fail_after_queries(
          Error(CpuCandidatePoolPreparationErrorCode::kInternalInvariant,
                "allocator.cpu_candidate_pool.post_query_unexpected_exception.v2",
                "An unexpected exception escaped after CPU routing work began"));
    }
  } catch (const std::bad_alloc&) {
    return Error(CpuCandidatePoolPreparationErrorCode::kResourceExhausted,
                 "allocator.cpu_candidate_pool.host_memory.v1",
                 "Host allocation failed within CPU candidate preparation bounds");
  } catch (const std::length_error&) {
    return Error(CpuCandidatePoolPreparationErrorCode::kResourceExhausted,
                 "allocator.cpu_candidate_pool.host_container.v1",
                 "Host container limits were exhausted within CPU preparation bounds");
  } catch (...) {
    return Error(CpuCandidatePoolPreparationErrorCode::kInternalInvariant,
                 "allocator.cpu_candidate_pool.host_unexpected_exception.v2",
                 "An unexpected exception escaped before CPU routing work began");
  }
}

PreparedCpuCandidatePoolsResult PrepareInitialCpuCandidatePools(
    PersistentCpuCandidatePoolPreparer& preparer, const board_ir::BoardSnapshot& board,
    const MultiNetWorkload& workload, const CpuCandidatePoolPreparationConfig& config) {
  return PrepareInitialCpuCandidatePoolsImpl<false>(preparer, board, workload, config, nullptr);
}

PreparedCpuCandidatePoolsResult PrepareInitialCpuCandidatePoolsWithOperationalProfileV1(
    PersistentCpuCandidatePoolPreparer& preparer, const board_ir::BoardSnapshot& board,
    const MultiNetWorkload& workload, const CpuCandidatePoolPreparationConfig& config,
    CpuCandidatePoolPreparationOperationalProfileV1& operational_profile) {
  return PrepareInitialCpuCandidatePoolsImpl<true>(preparer, board, workload, config,
                                                   &operational_profile);
}

}  // namespace apgar::allocator
