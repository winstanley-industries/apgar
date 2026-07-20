#include <benchmark/benchmark.h>
#include <sys/resource.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include "apgar/benchmark/phase3_commit.h"
#include "apgar/benchmark/phase3_source_stamp.h"
#include "apgar/benchmark/planar_corpus.h"
#include "apgar/board_ir/stable_hash.h"
#include "apgar/candidates/candidate_store.h"
#include "apgar/candidates/gpu_candidate_adapter.h"
#include "apgar/candidates/route_candidate.h"
#include "apgar/gpu/cuda_backend.h"
#include "apgar/gpu/planar_router.h"
#include "apgar/routing/candidate_policy.h"
#include "apgar/routing/cpu_astar.h"
#include "apgar/tooling/runfiles.h"
#include "tools/benchmark_support.h"

namespace {

inline constexpr int kBenchmarkRepetitions = 20;
inline constexpr double kBenchmarkMinimumSeconds = 0.02;
inline constexpr double kBenchmarkWarmupSeconds = 0.01;
inline constexpr std::uint32_t kMaximumCandidateCount = 512;
inline constexpr std::array<std::uint32_t, 8> kCandidateCounts = {4, 8, 16, 32, 64, 128, 256, 512};
enum class FailureClass : std::uint8_t {
  kReached = 0,
  kInvalidInput = 1,
  kUnsupported = 2,
  kDisconnected = 3,
  kResourceExhausted = 4,
  kCancelled = 5,
  kBackendFailure = 6,
  kValidationFailure = 7,
  kInternalInvariant = 8,
  kUnknown = 9,
};

enum class ForcedGenerator : std::uint8_t {
  kSequentialCpuAStar,
  kParallelCpuAStar,
  kCudaFrontier,
  kCudaSweep,
};

enum class TimingStage : std::uint8_t {
  kExecution,
  kAdmission,
  kPreparedEndToEnd,
  kEndToEnd,
};

struct VisibleQueryOutcome {
  std::uint64_t query_id = 0;
  std::uint64_t policy_identity = 0;
  FailureClass failure = FailureClass::kUnknown;
  std::uint64_t scalar_cost = 0;
  std::uint64_t geometry_fingerprint = 0;

  friend bool operator==(const VisibleQueryOutcome&, const VisibleQueryOutcome&) = default;
};

struct SuccessfulRoute {
  // CPU evidence is owned directly. GPU evidence remains in the validated
  // batch envelope and is referenced by its stable query identity.
  std::variant<apgar::routing::CpuRoute, std::uint64_t> evidence;
};

struct QueryExecution {
  std::uint32_t input_ordinal = 0;
  VisibleQueryOutcome visible;
  std::optional<SuccessfulRoute> route;
  std::uint64_t examined_states = 0;
  std::uint64_t examined_work = 0;
  std::uint32_t rounds = 0;
};

struct BatchExecution {
  std::vector<QueryExecution> queries;
  std::optional<apgar::gpu::PlanarCandidateBatch> validated_gpu_batch;
  std::uint64_t persistent_owned_device_bytes = 0;
  std::uint64_t batch_owned_device_bytes = 0;
  std::uint64_t workspace_capacity_device_bytes = 0;
  std::uint64_t peak_owned_device_bytes = 0;
  std::uint64_t prepared_node_lookup_host_bytes = 0;
  std::uint64_t batch_owned_host_bytes = 0;
  std::uint64_t device_to_host_readback_bytes = 0;
  std::uint64_t kernel_launch_count = 0;
  std::uint64_t blocking_status_readback_count = 0;
  std::uint32_t dispatched_rounds = 0;
  std::uint32_t finalization_launch_count = 0;
  std::uint32_t chunk_rounds = 0;
  double cuda_event_milliseconds = 0.0;
  bool externally_ordered = true;
  std::uint32_t parallel_worker_count = 1;
};

struct AdmissionObservation {
  bool valid = true;
  std::string error;
  std::uint64_t requested_candidates = 0;
  std::uint64_t reached_queries = 0;
  std::uint64_t failed_queries = 0;
  std::uint64_t accepted_candidates = 0;
  std::uint64_t rejected_candidates = 0;
  std::uint64_t builder_rejections = 0;
  std::uint64_t store_rejections = 0;
  std::uint64_t retained_rejection_records = 0;
  std::uint64_t unique_geometry_signatures = 0;
  std::uint64_t unique_resource_signatures = 0;
  std::uint64_t nondominated_candidates = 0;
  std::uint64_t minimum_policy_scalar_cost = 0;
  std::uint64_t base_policy_scalar_cost = 0;
  std::uint64_t best_of_k_intrinsic_base_cost = 0;
  std::uint64_t base_candidate_intrinsic_base_cost = 0;
  std::uint64_t accepted_logical_bytes = 0;
  std::uint64_t rejection_logical_bytes = 0;
  std::uint64_t peak_deterministic_host_bytes = 0;
  double mean_resource_jaccard = 0.0;
  double minimum_resource_jaccard = 0.0;
  double mean_geometric_overlap = 0.0;
  double minimum_geometric_overlap = 0.0;
  std::uint64_t ordered_candidate_checksum = 0;

  friend bool operator==(const AdmissionObservation&, const AdmissionObservation&) = default;
};

struct BenchmarkDeterminismBaseline {
  std::optional<std::uint64_t> ordered_outcome_checksum;
  std::optional<AdmissionObservation> admission;
};

struct CaseContext {
  apgar::benchmark::PlanarCorpusCase test_case;
  std::uint64_t deterministic_seed = 0;
  std::uint64_t resource_penalty_increment = 0;
  std::vector<apgar::routing::NormalizedCandidateGenerationPolicy> policies;
  std::vector<VisibleQueryOutcome> cpu_oracles;
  std::uint64_t persistent_device_bytes = 0;
  std::uint64_t prepared_node_lookup_host_bytes = 0;
};

struct BenchmarkContext {
  std::vector<CaseContext> cases;
  std::unique_ptr<apgar::gpu::IPlanarRouteBackend> backend;
  apgar::gpu::BackendMetadata backend_metadata;
  std::uint32_t parallel_worker_count = 1;
  std::vector<std::unique_ptr<BenchmarkDeterminismBaseline>> determinism_baselines;
};

[[nodiscard]] std::optional<std::uint64_t> ProcessLifetimePeakRssBytes() noexcept {
  struct rusage usage{};
  if (getrusage(RUSAGE_SELF, &usage) != 0 || usage.ru_maxrss < 0) {
    return std::nullopt;
  }
  const std::uint64_t platform_value = static_cast<std::uint64_t>(usage.ru_maxrss);
#if defined(__APPLE__)
  return platform_value;
#else
  if (platform_value > std::numeric_limits<std::uint64_t>::max() / 1024U) {
    return std::nullopt;
  }
  return platform_value * 1024U;
#endif
}

[[nodiscard]] std::uint64_t SaturatingAdd(std::uint64_t left, std::uint64_t right) noexcept {
  return right > std::numeric_limits<std::uint64_t>::max() - left
             ? std::numeric_limits<std::uint64_t>::max()
             : left + right;
}

[[nodiscard]] std::uint64_t SaturatingMultiply(std::uint64_t left, std::uint64_t right) noexcept {
  if (left != 0 && right > std::numeric_limits<std::uint64_t>::max() / left) {
    return std::numeric_limits<std::uint64_t>::max();
  }
  return left * right;
}

[[nodiscard]] std::uint64_t GeometryFingerprint(
    std::span<const apgar::board_ir::Point64> path,
    std::span<const apgar::routing::LayerSegment> segments) {
  return apgar::benchmark::tool_support::PlanarGeometryFingerprint(
      "APGAR-PHASE3-BENCHMARK-GEOMETRY-V1", path, segments);
}

[[nodiscard]] FailureClass CpuFailureClass(apgar::routing::RouteFailureCode code) {
  using apgar::routing::RouteFailureCode;
  switch (code) {
    case RouteFailureCode::kInvalidRequest:
      return FailureClass::kInvalidInput;
    case RouteFailureCode::kDisconnected:
      return FailureClass::kDisconnected;
    case RouteFailureCode::kUnsupportedLayerTransition:
    case RouteFailureCode::kUnsupportedPolicy:
      return FailureClass::kUnsupported;
    case RouteFailureCode::kValidationFailed:
      return FailureClass::kValidationFailure;
    case RouteFailureCode::kResourceExhausted:
      return FailureClass::kResourceExhausted;
    case RouteFailureCode::kInternalInvariant:
      return FailureClass::kInternalInvariant;
  }
  return FailureClass::kUnknown;
}

[[nodiscard]] FailureClass GpuFailureClass(apgar::gpu::PlanarGpuFailureCode code) {
  using apgar::gpu::PlanarGpuFailureCode;
  switch (code) {
    case PlanarGpuFailureCode::kInvalidInput:
      return FailureClass::kInvalidInput;
    case PlanarGpuFailureCode::kUnsupported:
      return FailureClass::kUnsupported;
    case PlanarGpuFailureCode::kDisconnected:
      return FailureClass::kDisconnected;
    case PlanarGpuFailureCode::kResourceExhausted:
      return FailureClass::kResourceExhausted;
    case PlanarGpuFailureCode::kCancelled:
      return FailureClass::kCancelled;
    case PlanarGpuFailureCode::kBackendFailure:
      return FailureClass::kBackendFailure;
    case PlanarGpuFailureCode::kValidationFailed:
      return FailureClass::kValidationFailure;
    case PlanarGpuFailureCode::kInternalInvariant:
      return FailureClass::kInternalInvariant;
  }
  return FailureClass::kUnknown;
}

[[nodiscard]] std::string_view GeneratorName(ForcedGenerator generator) {
  switch (generator) {
    case ForcedGenerator::kSequentialCpuAStar:
      return "sequential_cpu_astar";
    case ForcedGenerator::kParallelCpuAStar:
      return "parallel_cpu_astar";
    case ForcedGenerator::kCudaFrontier:
      return "batched_cuda_frontier";
    case ForcedGenerator::kCudaSweep:
      return "batched_cuda_sweep";
  }
  return "unknown";
}

[[nodiscard]] std::string_view StageName(TimingStage stage) {
  switch (stage) {
    case TimingStage::kExecution:
      return "execution_readback";
    case TimingStage::kAdmission:
      return "exact_admission_store";
    case TimingStage::kPreparedEndToEnd:
      return "prepared_end_to_end";
    case TimingStage::kEndToEnd:
      return "end_to_end";
  }
  return "unknown";
}

[[nodiscard]] bool IsGpu(ForcedGenerator generator) noexcept {
  return generator == ForcedGenerator::kCudaFrontier || generator == ForcedGenerator::kCudaSweep;
}

[[nodiscard]] std::uint64_t GpuExaminedStates(const CaseContext& context, ForcedGenerator generator,
                                              FailureClass outcome, std::uint32_t rounds) noexcept {
  if (generator == ForcedGenerator::kCudaFrontier) {
    // A disconnected frontier query reports one final round in which no winner
    // exists. Every other reported round closes one stable winner state.
    return outcome == FailureClass::kDisconnected && rounds != 0 ? rounds - 1U : rounds;
  }
  const std::uint64_t departure_states =
      SaturatingMultiply(context.test_case.compiled_board.telemetry().represented_nodes, 8U);
  return SaturatingMultiply(departure_states, rounds);
}

[[nodiscard]] std::uint64_t BatchIdentity(const CaseContext& context,
                                          std::uint32_t candidate_count) {
  apgar::board_ir::StableHashBuilder hash;
  hash.AddString("APGAR-PHASE3-BENCHMARK-BATCH-V1");
  hash.AddU64(context.test_case.board.content_hash());
  hash.AddU64(context.test_case.compiled_board.compiler_profile_fingerprint());
  hash.AddU32(candidate_count);
  return hash.Finish();
}

[[nodiscard]] std::uint64_t QueryIdentity(
    std::uint64_t batch_identity,
    const apgar::routing::NormalizedCandidateGenerationPolicy& policy) {
  apgar::board_ir::StableHashBuilder hash;
  hash.AddString("APGAR-PHASE3-BENCHMARK-QUERY-V1");
  hash.AddU64(batch_identity);
  hash.AddU64(policy.identity);
  hash.AddU32(policy.policy.candidate_ordinal);
  return hash.Finish();
}

[[nodiscard]] std::vector<std::uint64_t> QueryIdentities(const CaseContext& context,
                                                         std::uint32_t candidate_count) {
  const std::uint64_t batch_identity = BatchIdentity(context, candidate_count);
  std::vector<std::uint64_t> identities;
  identities.reserve(candidate_count);
  for (std::uint32_t index = 0; index < candidate_count; ++index) {
    identities.push_back(QueryIdentity(batch_identity, context.policies[index]));
  }
  return identities;
}

void CanonicalizeQueryOrder(BatchExecution* batch) {
  std::sort(batch->queries.begin(), batch->queries.end(),
            [](const QueryExecution& left, const QueryExecution& right) {
              return left.visible.query_id < right.visible.query_id;
            });
  for (std::size_t index = 1; index < batch->queries.size(); ++index) {
    if (batch->queries[index - 1].visible.query_id >= batch->queries[index].visible.query_id) {
      batch->externally_ordered = false;
      return;
    }
  }
}

[[nodiscard]] QueryExecution RunOneCpu(const CaseContext& context, std::uint32_t policy_index,
                                       std::uint64_t query_id) {
  apgar::routing::CpuRouteRequest request = context.test_case.request;
  request.candidate_policy = context.policies[policy_index].policy;
  apgar::routing::CpuRouteResult result = apgar::routing::RouteWithCpuAStar(
      context.test_case.board, context.test_case.compiled_board, request);
  QueryExecution execution{
      .input_ordinal = policy_index,
      .visible =
          VisibleQueryOutcome{
              .query_id = query_id,
              .policy_identity = context.policies[policy_index].identity,
          },
      .route = std::nullopt,
  };
  if (std::holds_alternative<apgar::routing::CpuRoute>(result)) {
    apgar::routing::CpuRoute route = std::get<apgar::routing::CpuRoute>(std::move(result));
    execution.visible.failure = FailureClass::kReached;
    execution.visible.scalar_cost = route.total_cost;
    execution.visible.geometry_fingerprint =
        GeometryFingerprint(route.lattice_path, route.segments);
    execution.examined_states = route.telemetry.expanded_states;
    execution.examined_work = route.telemetry.expanded_states;
    execution.route = SuccessfulRoute{.evidence = std::move(route)};
    return execution;
  }
  const apgar::routing::RouteFailure& failure = std::get<apgar::routing::RouteFailure>(result);
  execution.visible.failure = CpuFailureClass(failure.code);
  if (failure.telemetry.has_value()) {
    execution.examined_states = failure.telemetry->expanded_states;
    execution.examined_work = failure.telemetry->expanded_states;
  }
  return execution;
}

[[nodiscard]] BatchExecution RunCpuBatch(const CaseContext& context, std::uint32_t candidate_count,
                                         bool parallel, std::uint32_t maximum_workers) {
  const std::vector<std::uint64_t> query_ids = QueryIdentities(context, candidate_count);
  BatchExecution batch;
  batch.queries.resize(candidate_count);
  if (!parallel || candidate_count == 1) {
    for (std::uint32_t index = 0; index < candidate_count; ++index) {
      batch.queries[index] = RunOneCpu(context, index, query_ids[index]);
    }
    CanonicalizeQueryOrder(&batch);
    return batch;
  }

  const std::uint32_t worker_count =
      std::max<std::uint32_t>(1, std::min(candidate_count, maximum_workers));
  batch.parallel_worker_count = worker_count;
  std::atomic<std::uint32_t> next_index = 0;
  std::vector<std::thread> workers;
  workers.reserve(worker_count);
  for (std::uint32_t worker = 0; worker < worker_count; ++worker) {
    workers.emplace_back([&] {
      while (true) {
        const std::uint32_t index = next_index.fetch_add(1, std::memory_order_relaxed);
        if (index >= candidate_count) {
          return;
        }
        batch.queries[index] = RunOneCpu(context, index, query_ids[index]);
      }
    });
  }
  for (std::thread& worker : workers) {
    worker.join();
  }
  CanonicalizeQueryOrder(&batch);
  return batch;
}

[[nodiscard]] BatchExecution RunGpuBatch(const CaseContext& context, std::uint32_t candidate_count,
                                         ForcedGenerator generator,
                                         apgar::gpu::PreparedPlanarCompiledView& prepared) {
  const std::uint64_t batch_identity = BatchIdentity(context, candidate_count);
  const std::vector<std::uint64_t> query_ids = QueryIdentities(context, candidate_count);
  std::vector<apgar::gpu::PlanarCandidateBatchQuery> queries;
  queries.reserve(candidate_count);
  for (std::uint32_t index = 0; index < candidate_count; ++index) {
    apgar::routing::CpuRouteRequest request = context.test_case.request;
    request.candidate_policy = context.policies[index].policy;
    queries.push_back(apgar::gpu::PlanarCandidateBatchQuery{
        .query_id = query_ids[index],
        .input_ordinal = index,
        .request = std::move(request),
    });
  }
  const apgar::gpu::PlanarGenerator gpu_generator =
      generator == ForcedGenerator::kCudaFrontier ? apgar::gpu::PlanarGenerator::kBucketedFrontier
                                                  : apgar::gpu::PlanarGenerator::kHeadingAwareSweep;
  apgar::gpu::PlanarCandidateBatchResult result =
      apgar::gpu::RouteCandidateBatchWithPreparedPlanarGpuBackend(
          context.test_case.board, context.test_case.compiled_board, queries,
          apgar::gpu::PlanarCandidateBatchPolicy{
              .batch_id = batch_identity,
              .generator = gpu_generator,
          },
          prepared);

  BatchExecution execution;
  execution.queries.resize(candidate_count);
  if (std::holds_alternative<apgar::gpu::PlanarGpuFailure>(result)) {
    const apgar::gpu::PlanarGpuFailure& failure = std::get<apgar::gpu::PlanarGpuFailure>(result);
    for (std::uint32_t index = 0; index < candidate_count; ++index) {
      execution.queries[index] = QueryExecution{
          .input_ordinal = index,
          .visible =
              VisibleQueryOutcome{
                  .query_id = query_ids[index],
                  .policy_identity = context.policies[index].identity,
                  .failure = GpuFailureClass(failure.code),
              },
          .route = std::nullopt,
      };
      // A top-level failure predates validated device execution. Legacy
      // KernelTelemetry, when present, contains no candidate-batch envelope.
      if (index == 0 && failure.telemetry.has_value()) {
        execution.queries[index].examined_work = failure.telemetry->examined_work;
        execution.queries[index].rounds = failure.telemetry->rounds;
        execution.queries[index].examined_states =
            GpuExaminedStates(context, generator, execution.queries[index].visible.failure,
                              execution.queries[index].rounds);
      }
    }
    CanonicalizeQueryOrder(&execution);
    return execution;
  }

  apgar::gpu::PlanarCandidateBatch batch =
      std::get<apgar::gpu::PlanarCandidateBatch>(std::move(result));
  execution.persistent_owned_device_bytes = batch.telemetry.persistent_device_bytes;
  execution.batch_owned_device_bytes = batch.telemetry.batch_device_bytes;
  execution.workspace_capacity_device_bytes = batch.telemetry.workspace_capacity_device_bytes;
  execution.peak_owned_device_bytes = batch.telemetry.peak_device_bytes;
  execution.prepared_node_lookup_host_bytes = batch.prepared_node_lookup_host_bytes;
  execution.batch_owned_host_bytes = batch.telemetry.batch_host_bytes;
  execution.device_to_host_readback_bytes = batch.telemetry.device_to_host_readback_bytes;
  execution.kernel_launch_count = batch.telemetry.kernel_launch_count;
  execution.blocking_status_readback_count = batch.telemetry.blocking_status_readback_count;
  execution.dispatched_rounds = batch.telemetry.dispatched_rounds;
  execution.finalization_launch_count = batch.telemetry.finalization_launch_count;
  execution.chunk_rounds = batch.telemetry.chunk_rounds;
  execution.cuda_event_milliseconds = batch.telemetry.kernel_milliseconds;
  if (gpu_generator == apgar::gpu::PlanarGenerator::kHeadingAwareSweep) {
    execution.parallel_worker_count = static_cast<std::uint32_t>(
        apgar::gpu::CandidateCompactValidationWorkerCountV1(candidate_count));
  }
  execution.externally_ordered =
      std::is_sorted(batch.items.begin(), batch.items.end(),
                     [](const apgar::gpu::PlanarCandidateBatchItem& left,
                        const apgar::gpu::PlanarCandidateBatchItem& right) {
                       return left.query_id() < right.query_id();
                     });
  std::vector<bool> observed(candidate_count, false);
  for (const apgar::gpu::PlanarCandidateBatchItem& item : batch.items) {
    if (item.input_ordinal() >= candidate_count || observed[item.input_ordinal()] ||
        item.query_id() != query_ids[item.input_ordinal()] ||
        item.policy_identity() != context.policies[item.input_ordinal()].identity) {
      execution.externally_ordered = false;
      continue;
    }
    observed[item.input_ordinal()] = true;
    QueryExecution& query = execution.queries[item.input_ordinal()];
    query.input_ordinal = item.input_ordinal();
    query.visible.query_id = item.query_id();
    query.visible.policy_identity = item.policy_identity();
    if (std::holds_alternative<apgar::gpu::PlanarGpuRoute>(item.result())) {
      const apgar::gpu::PlanarGpuRoute& route = std::get<apgar::gpu::PlanarGpuRoute>(item.result());
      query.visible.failure = FailureClass::kReached;
      query.visible.scalar_cost = route.total_cost;
      query.visible.geometry_fingerprint = GeometryFingerprint(route.lattice_path, route.segments);
      query.examined_work = route.telemetry.examined_work;
      query.rounds = route.telemetry.rounds;
      query.examined_states =
          GpuExaminedStates(context, generator, query.visible.failure, query.rounds);
      query.route = SuccessfulRoute{.evidence = item.query_id()};
      continue;
    }
    const apgar::gpu::PlanarGpuFailure& failure =
        std::get<apgar::gpu::PlanarGpuFailure>(item.result());
    query.visible.failure = GpuFailureClass(failure.code);
    if (failure.telemetry.has_value()) {
      query.examined_work = failure.telemetry->examined_work;
      query.rounds = failure.telemetry->rounds;
      query.examined_states =
          GpuExaminedStates(context, generator, query.visible.failure, query.rounds);
    }
  }
  for (std::uint32_t index = 0; index < candidate_count; ++index) {
    if (!observed[index]) {
      execution.externally_ordered = false;
      execution.queries[index] = QueryExecution{
          .input_ordinal = index,
          .visible =
              VisibleQueryOutcome{
                  .query_id = query_ids[index],
                  .policy_identity = context.policies[index].identity,
                  .failure = FailureClass::kInternalInvariant,
              },
          .route = std::nullopt,
      };
    }
  }
  execution.validated_gpu_batch = std::move(batch);
  CanonicalizeQueryOrder(&execution);
  return execution;
}

[[nodiscard]] BatchExecution RunGenerator(const CaseContext& context, std::uint32_t candidate_count,
                                          ForcedGenerator generator, std::uint32_t parallel_workers,
                                          apgar::gpu::PreparedPlanarCompiledView* prepared) {
  switch (generator) {
    case ForcedGenerator::kSequentialCpuAStar:
      return RunCpuBatch(context, candidate_count, false, 1);
    case ForcedGenerator::kParallelCpuAStar:
      return RunCpuBatch(context, candidate_count, true, parallel_workers);
    case ForcedGenerator::kCudaFrontier:
    case ForcedGenerator::kCudaSweep:
      if (prepared == nullptr) {
        BatchExecution failure;
        failure.externally_ordered = false;
        return failure;
      }
      return RunGpuBatch(context, candidate_count, generator, *prepared);
  }
  return {};
}

struct CandidateDraftWithPolicy {
  std::uint32_t policy_index = 0;
  apgar::candidates::GeneratedRouteCandidate candidate;
};

// The benchmark has already accepted a validated backend envelope before it
// reaches candidate construction. A missing canonical provenance mapping is a
// benchmark invariant failure, not a candidate rejection that may silently be
// attributed to a different generator or device class.
struct BenchmarkAdmissionError {
  std::string detail;
};

using CandidateDraftResult =
    std::variant<CandidateDraftWithPolicy, apgar::candidates::CandidateRejection,
                 BenchmarkAdmissionError>;

[[nodiscard]] CandidateDraftResult ConvertCandidateDraftBuildResult(
    std::uint32_t policy_index, apgar::candidates::CandidateDraftBuildResult result) {
  if (std::holds_alternative<apgar::candidates::CandidateRejection>(result)) {
    return std::get<apgar::candidates::CandidateRejection>(std::move(result));
  }
  return CandidateDraftWithPolicy{
      .policy_index = policy_index,
      .candidate = std::get<apgar::candidates::GeneratedRouteCandidate>(std::move(result)),
  };
}

[[nodiscard]] const apgar::gpu::PlanarCandidateBatchItem* FindGpuBatchItem(
    const BatchExecution& execution, std::uint64_t query_id) {
  if (!execution.validated_gpu_batch.has_value()) {
    return nullptr;
  }
  const auto found = std::ranges::lower_bound(execution.validated_gpu_batch->items, query_id, {},
                                              &apgar::gpu::PlanarCandidateBatchItem::query_id);
  return found == execution.validated_gpu_batch->items.end() || found->query_id() != query_id
             ? nullptr
             : &*found;
}

[[nodiscard]] CandidateDraftResult BenchmarkBuildRejection(
    const CaseContext& context, const BatchExecution& execution, const QueryExecution& query,
    std::uint64_t batch_identity, std::string invariant_id, std::string detail) {
  const std::uint32_t policy_index =
      std::min<std::uint32_t>(query.input_ordinal, context.policies.size() - 1U);
  const apgar::routing::NormalizedCandidateGenerationPolicy& policy =
      context.policies[policy_index];
  apgar::candidates::CandidateGeneratorKind generator =
      apgar::candidates::CandidateGeneratorKind::kCpuAStar;
  apgar::candidates::CandidateBackendKind backend = apgar::candidates::CandidateBackendKind::kCpu;
  std::string device_class(apgar::candidates::kCpuReferenceDeviceClassV1);
  if (execution.validated_gpu_batch.has_value()) {
    const std::optional<apgar::candidates::CandidateGeneratorKind> mapped_generator =
        apgar::candidates::CandidateGeneratorForPlanarGenerator(
            execution.validated_gpu_batch->generator);
    const std::optional<std::string> mapped_device_class =
        apgar::candidates::CudaCandidateDeviceClass(execution.validated_gpu_batch->backend);
    if (!mapped_generator.has_value() || !mapped_device_class.has_value()) {
      return BenchmarkAdmissionError{
          .detail =
              "validated GPU benchmark envelope has no canonical Candidate Provenance v1 "
              "mapping",
      };
    }
    generator = *mapped_generator;
    backend = apgar::candidates::CandidateBackendKind::kCuda;
    device_class = *mapped_device_class;
    batch_identity = execution.validated_gpu_batch->batch_id;
  }
  apgar::candidates::CandidateRejection rejection{
      .candidate_id = std::nullopt,
      .net = context.test_case.request.net,
      .stage = apgar::candidates::CandidateLifecycleStage::kGenerated,
      .code = apgar::candidates::CandidateRejectionCode::kInternalInvariant,
      .invariant_id = std::move(invariant_id),
      .associations = apgar::candidates::AssociationsFor(context.test_case.board,
                                                         context.test_case.compiled_board),
      .policy_identity = policy.identity,
      .provenance =
          {
              .generator = generator,
              .generator_version = 1,
              .backend = backend,
              .supported_device_class = std::move(device_class),
              .deterministic_seed = policy.policy.deterministic_seed,
              .batch_identity = batch_identity,
              .query_identity = query.visible.query_id,
              .candidate_ordinal = policy.policy.candidate_ordinal,
          },
      .primitive_witness_index = std::nullopt,
      .resource_witness_index = std::nullopt,
      .expected_value = std::nullopt,
      .actual_value = std::nullopt,
      .conflicting_entity = std::nullopt,
      .candidate_payload_checksum = std::nullopt,
      .detail = std::move(detail),
      .logical_bytes = 0,
  };
  return apgar::candidates::CanonicalizeCandidateRejectionV1(std::move(rejection));
}

[[nodiscard]] CandidateDraftResult BuildCandidateDraft(const CaseContext& context,
                                                       const BatchExecution& execution,
                                                       const QueryExecution& query,
                                                       std::uint64_t batch_identity) {
  if (!query.route.has_value() || query.input_ordinal >= context.policies.size()) {
    return BenchmarkBuildRejection(
        context, execution, query, batch_identity, "candidate.benchmark.missing_route.v1",
        "Benchmark cannot build a candidate without a successful indexed route");
  }
  const SuccessfulRoute& route = *query.route;
  apgar::routing::CpuRouteRequest request = context.test_case.request;
  request.candidate_policy = context.policies[query.input_ordinal].policy;
  apgar::candidates::CandidateDraftBuildResult result;
  if (std::holds_alternative<apgar::routing::CpuRoute>(route.evidence)) {
    result = apgar::candidates::BuildGeneratedCandidateFromCpuRoute(
        context.test_case.board, context.test_case.compiled_board, request,
        context.policies[query.input_ordinal], std::get<apgar::routing::CpuRoute>(route.evidence),
        apgar::candidates::CandidateSchedulingIdentity{.batch_identity = batch_identity,
                                                       .query_identity = query.visible.query_id});
  } else {
    const std::uint64_t gpu_query_id = std::get<std::uint64_t>(route.evidence);
    const apgar::gpu::PlanarCandidateBatchItem* item = FindGpuBatchItem(execution, gpu_query_id);
    if (item == nullptr || !execution.validated_gpu_batch.has_value()) {
      return BenchmarkBuildRejection(
          context, execution, query, batch_identity, "candidate.benchmark.gpu_envelope_missing.v1",
          "Benchmark lost the validated GPU batch item before exact candidate admission");
    }
    const apgar::gpu::PlanarCandidateBatchQuery gpu_query{
        .query_id = query.visible.query_id,
        .input_ordinal = query.input_ordinal,
        .request = request,
    };
    result = apgar::candidates::BuildGeneratedCandidateFromGpuBatchItem(
        context.test_case.board, context.test_case.compiled_board, gpu_query,
        context.policies[query.input_ordinal], *execution.validated_gpu_batch, *item);
  }
  return ConvertCandidateDraftBuildResult(query.input_ordinal, std::move(result));
}

[[nodiscard]] std::uint64_t CandidateOrderingChecksum(
    std::span<const apgar::candidates::StoredCandidate> candidates) {
  apgar::board_ir::StableHashBuilder hash;
  hash.AddString("APGAR-PHASE3-BENCHMARK-CANDIDATE-ORDER-V1");
  hash.AddU64(candidates.size());
  for (const apgar::candidates::StoredCandidate& candidate : candidates) {
    const apgar::candidates::GeneratedRouteCandidate& data = candidate->data();
    hash.AddU64(data.id.high);
    hash.AddU64(data.id.low);
    hash.AddU64(data.geometry_signature.high);
    hash.AddU64(data.geometry_signature.low);
    hash.AddU64(data.resource_signature.high);
    hash.AddU64(data.resource_signature.low);
    hash.AddU64(data.metrics.scalar_policy_cost);
    hash.AddU64(data.payload_checksum);
  }
  return hash.Finish();
}

template <typename Projection>
[[nodiscard]] std::uint64_t UniqueSignatureCount(
    std::span<const apgar::candidates::StoredCandidate> candidates, Projection projection) {
  std::vector<apgar::candidates::CandidateSignature> signatures;
  signatures.reserve(candidates.size());
  for (const apgar::candidates::StoredCandidate& candidate : candidates) {
    signatures.push_back(projection(candidate->data()));
  }
  std::sort(signatures.begin(), signatures.end());
  return static_cast<std::uint64_t>(std::unique(signatures.begin(), signatures.end()) -
                                    signatures.begin());
}

void PublishPairwiseDiversity(std::span<const apgar::candidates::StoredCandidate> candidates,
                              AdmissionObservation* observation) {
  if (candidates.size() < 2) {
    return;
  }
  double resource_sum = 0.0;
  double geometric_sum = 0.0;
  double minimum_resource = 1.0;
  double minimum_geometric = 1.0;
  std::uint64_t pair_count = 0;
  for (std::size_t left = 0; left < candidates.size(); ++left) {
    for (std::size_t right = left + 1; right < candidates.size(); ++right) {
      const double resource =
          apgar::candidates::ResourceJaccardOverlap(*candidates[left], *candidates[right]);
      const double geometric =
          apgar::candidates::GeometricOverlapRatio(*candidates[left], *candidates[right]);
      resource_sum += resource;
      geometric_sum += geometric;
      minimum_resource = std::min(minimum_resource, resource);
      minimum_geometric = std::min(minimum_geometric, geometric);
      ++pair_count;
    }
  }
  observation->mean_resource_jaccard = resource_sum / static_cast<double>(pair_count);
  observation->minimum_resource_jaccard = minimum_resource;
  observation->mean_geometric_overlap = geometric_sum / static_cast<double>(pair_count);
  observation->minimum_geometric_overlap = minimum_geometric;
}

[[nodiscard]] AdmissionObservation AdmitBatch(const CaseContext& context,
                                              const BatchExecution& execution,
                                              std::uint32_t candidate_count) {
  AdmissionObservation observation{
      .valid = true,
      .error = {},
      .requested_candidates = candidate_count,
      .peak_deterministic_host_bytes =
          context.test_case.compiled_board.telemetry().estimated_host_bytes,
  };
  const std::uint64_t batch_identity = BatchIdentity(context, candidate_count);
  std::vector<CandidateDraftWithPolicy> drafts;
  drafts.reserve(candidate_count);
  std::vector<apgar::candidates::CandidateRejection> builder_rejection_records;
  builder_rejection_records.reserve(candidate_count);
  std::uint64_t gpu_batch_adapter_index_bytes = 0;
  std::vector<std::optional<apgar::candidates::CandidateDraftBuildResult>> gpu_draft_results;
  if (execution.validated_gpu_batch.has_value()) {
    gpu_batch_adapter_index_bytes =
        SaturatingMultiply(execution.validated_gpu_batch->items.size(), sizeof(std::uint64_t));
    gpu_draft_results.resize(execution.queries.size());
    std::vector<apgar::gpu::PlanarCandidateBatchQuery> gpu_queries;
    std::vector<apgar::candidates::GpuCandidateBatchBuildRequest> gpu_requests;
    std::vector<std::size_t> gpu_result_indices;
    gpu_queries.reserve(execution.queries.size());
    gpu_requests.reserve(execution.queries.size());
    gpu_result_indices.reserve(execution.queries.size());
    for (std::size_t index = 0; index < execution.queries.size(); ++index) {
      const QueryExecution& query = execution.queries[index];
      if (query.visible.failure != FailureClass::kReached || !query.route.has_value() ||
          !std::holds_alternative<std::uint64_t>(query.route->evidence) ||
          query.input_ordinal >= context.policies.size()) {
        continue;
      }
      const apgar::gpu::PlanarCandidateBatchItem* item =
          FindGpuBatchItem(execution, std::get<std::uint64_t>(query.route->evidence));
      if (item == nullptr) {
        continue;
      }
      apgar::routing::CpuRouteRequest request = context.test_case.request;
      request.candidate_policy = context.policies[query.input_ordinal].policy;
      gpu_queries.push_back(apgar::gpu::PlanarCandidateBatchQuery{
          .query_id = query.visible.query_id,
          .input_ordinal = query.input_ordinal,
          .request = std::move(request),
      });
      gpu_requests.push_back(apgar::candidates::GpuCandidateBatchBuildRequest{
          .query = std::cref(gpu_queries.back()),
          .normalized_policy = std::cref(context.policies[query.input_ordinal]),
          .item = std::cref(*item),
      });
      gpu_result_indices.push_back(index);
    }
    apgar::candidates::GpuCandidateBatchBuildResult built =
        apgar::candidates::BuildGeneratedCandidatesFromGpuBatchItems(
            context.test_case.board, context.test_case.compiled_board,
            *execution.validated_gpu_batch, gpu_requests);
    if (std::holds_alternative<apgar::candidates::GpuCandidateBatchBuildFailure>(built)) {
      observation.valid = false;
      observation.error =
          std::get<apgar::candidates::GpuCandidateBatchBuildFailure>(std::move(built)).detail;
      return observation;
    }
    std::vector<apgar::candidates::CandidateDraftBuildResult> batch_results =
        std::get<std::vector<apgar::candidates::CandidateDraftBuildResult>>(std::move(built));
    if (batch_results.size() != gpu_result_indices.size()) {
      observation.valid = false;
      observation.error = "GPU candidate batch adapter returned a noncanonical result count";
      return observation;
    }
    for (std::size_t index = 0; index < batch_results.size(); ++index) {
      gpu_draft_results[gpu_result_indices[index]] = std::move(batch_results[index]);
    }
  }
  std::uint64_t route_payload_bytes = 0;
  std::uint64_t draft_payload_bytes = 0;
  for (std::size_t query_index = 0; query_index < execution.queries.size(); ++query_index) {
    const QueryExecution& query = execution.queries[query_index];
    if (query.visible.failure != FailureClass::kReached) {
      ++observation.failed_queries;
      continue;
    }
    ++observation.reached_queries;
    if (query.input_ordinal == 0) {
      observation.base_policy_scalar_cost = query.visible.scalar_cost;
    }
    if (query.route.has_value()) {
      std::size_t point_count = 0;
      std::size_t segment_count = 0;
      if (std::holds_alternative<apgar::routing::CpuRoute>(query.route->evidence)) {
        const apgar::routing::CpuRoute& route =
            std::get<apgar::routing::CpuRoute>(query.route->evidence);
        point_count = route.lattice_path.size();
        segment_count = route.segments.size();
      } else if (const apgar::gpu::PlanarCandidateBatchItem* item =
                     FindGpuBatchItem(execution, std::get<std::uint64_t>(query.route->evidence));
                 item != nullptr &&
                 std::holds_alternative<apgar::gpu::PlanarGpuRoute>(item->result())) {
        const apgar::gpu::PlanarGpuRoute& route =
            std::get<apgar::gpu::PlanarGpuRoute>(item->result());
        point_count = route.lattice_path.size();
        segment_count = route.segments.size();
      }
      const std::uint64_t point_bytes =
          static_cast<std::uint64_t>(point_count) * sizeof(apgar::board_ir::Point64);
      const std::uint64_t segment_bytes =
          static_cast<std::uint64_t>(segment_count) * sizeof(apgar::routing::LayerSegment);
      route_payload_bytes = SaturatingAdd(route_payload_bytes, point_bytes);
      route_payload_bytes = SaturatingAdd(route_payload_bytes, segment_bytes);
    }
    CandidateDraftResult built =
        !gpu_draft_results.empty() && gpu_draft_results[query_index].has_value()
            ? ConvertCandidateDraftBuildResult(query.input_ordinal,
                                               std::move(*gpu_draft_results[query_index]))
            : BuildCandidateDraft(context, execution, query, batch_identity);
    if (std::holds_alternative<BenchmarkAdmissionError>(built)) {
      observation.valid = false;
      observation.error = std::get<BenchmarkAdmissionError>(std::move(built)).detail;
      return observation;
    }
    if (std::holds_alternative<apgar::candidates::CandidateRejection>(built)) {
      ++observation.builder_rejections;
      builder_rejection_records.push_back(
          std::get<apgar::candidates::CandidateRejection>(std::move(built)));
      continue;
    }
    CandidateDraftWithPolicy draft = std::get<CandidateDraftWithPolicy>(std::move(built));
    if (draft.policy_index == 0) {
      observation.base_candidate_intrinsic_base_cost = draft.candidate.metrics.intrinsic_base_cost;
    }
    draft_payload_bytes = SaturatingAdd(draft_payload_bytes, draft.candidate.logical_bytes);
    drafts.push_back(std::move(draft));
  }

  std::sort(drafts.begin(), drafts.end(),
            [](const CandidateDraftWithPolicy& left, const CandidateDraftWithPolicy& right) {
              if (left.candidate.id != right.candidate.id) {
                return left.candidate.id < right.candidate.id;
              }
              return left.policy_index < right.policy_index;
            });
  apgar::candidates::CandidateStore store(apgar::candidates::CandidateStoreConfig{
      .maximum_candidates_per_net = candidate_count,
      .maximum_candidate_bytes_per_net = std::numeric_limits<std::uint64_t>::max(),
      .maximum_rejection_records = std::max<std::uint64_t>(1, candidate_count * 2ULL),
      .maximum_rejection_items_per_transaction = kMaximumCandidateCount,
      .maximum_admission_items_per_transaction =
          apgar::candidates::kDefaultMaximumAdmissionItemsPerTransaction,
      .maximum_admission_input_bytes_per_transaction =
          apgar::candidates::kDefaultMaximumAdmissionInputBytesPerTransaction,
      .maximum_admission_work_units_per_transaction =
          apgar::candidates::kDefaultMaximumAdmissionWorkUnitsPerTransaction,
  });
  if (!store.valid()) {
    observation.valid = false;
    observation.error = "candidate benchmark constructed an invalid store configuration";
    observation.builder_rejections += drafts.size();
    observation.rejected_candidates = observation.reached_queries;
    return observation;
  }
  if (const std::optional<apgar::candidates::CandidateRejection> ingestion_failure =
          store.RetainRejections(builder_rejection_records);
      ingestion_failure.has_value()) {
    observation.valid = false;
    observation.error = "candidate benchmark exceeded its rejection-ingestion transaction cap";
    return observation;
  }
  std::vector<apgar::candidates::CandidateAdmissionItem> admission_items;
  admission_items.reserve(drafts.size());
  for (CandidateDraftWithPolicy& draft : drafts) {
    apgar::routing::CpuRouteRequest request = context.test_case.request;
    request.candidate_policy = context.policies[draft.policy_index].policy;
    admission_items.push_back(apgar::candidates::CandidateAdmissionItem{
        .request = request,
        .generated = std::move(draft.candidate),
    });
  }
  static_cast<void>(store.AdmitBatch(context.test_case.board, context.test_case.compiled_board,
                                     std::move(admission_items)));

  const std::vector<apgar::candidates::StoredCandidate> accepted =
      store.Enumerate(context.test_case.request.net);
  const std::vector<apgar::candidates::CandidateRejection> rejections = store.Rejections();
  observation.accepted_candidates = accepted.size();
  if (observation.accepted_candidates > observation.reached_queries) {
    observation.valid = false;
    observation.error = "candidate store retained more candidates than reached generator queries";
    return observation;
  }
  observation.rejected_candidates = observation.reached_queries - observation.accepted_candidates;
  observation.retained_rejection_records = rejections.size();
  if (observation.retained_rejection_records < observation.builder_rejections) {
    observation.valid = false;
    observation.error = "candidate store retained fewer diagnostics than the builder submitted";
    return observation;
  }
  observation.store_rejections =
      observation.retained_rejection_records - observation.builder_rejections;
  observation.accepted_logical_bytes = store.CandidateBytes(context.test_case.request.net)
                                           .value_or(std::numeric_limits<std::uint64_t>::max());
  for (const apgar::candidates::CandidateRejection& rejection : rejections) {
    observation.rejection_logical_bytes =
        SaturatingAdd(observation.rejection_logical_bytes, rejection.logical_bytes);
  }
  observation.unique_geometry_signatures = UniqueSignatureCount(
      accepted, [](const apgar::candidates::GeneratedRouteCandidate& candidate) {
        return candidate.geometry_signature;
      });
  observation.unique_resource_signatures = UniqueSignatureCount(
      accepted, [](const apgar::candidates::GeneratedRouteCandidate& candidate) {
        return candidate.resource_signature;
      });
  observation.minimum_policy_scalar_cost = std::numeric_limits<std::uint64_t>::max();
  observation.best_of_k_intrinsic_base_cost = std::numeric_limits<std::uint64_t>::max();
  for (std::size_t index = 0; index < accepted.size(); ++index) {
    observation.minimum_policy_scalar_cost = std::min(
        observation.minimum_policy_scalar_cost, accepted[index]->data().metrics.scalar_policy_cost);
    observation.best_of_k_intrinsic_base_cost =
        std::min(observation.best_of_k_intrinsic_base_cost,
                 accepted[index]->data().metrics.intrinsic_base_cost);
    bool dominated = false;
    for (std::size_t other = 0; other < accepted.size(); ++other) {
      if (index != other &&
          apgar::candidates::CandidateMetricsDominate(*accepted[other], *accepted[index])) {
        dominated = true;
        break;
      }
    }
    if (!dominated) {
      ++observation.nondominated_candidates;
    }
  }
  if (accepted.empty()) {
    observation.minimum_policy_scalar_cost = 0;
    observation.best_of_k_intrinsic_base_cost = 0;
  }
  PublishPairwiseDiversity(accepted, &observation);
  observation.ordered_candidate_checksum = CandidateOrderingChecksum(accepted);
  const std::uint64_t compiled_bytes =
      context.test_case.compiled_board.telemetry().estimated_host_bytes;
  const std::uint64_t route_phase = SaturatingAdd(compiled_bytes, route_payload_bytes);
  const std::uint64_t batch_conversion_phase =
      SaturatingAdd(route_phase, gpu_batch_adapter_index_bytes);
  const std::uint64_t draft_to_store_phase = SaturatingAdd(
      SaturatingAdd(SaturatingAdd(route_phase, gpu_batch_adapter_index_bytes), draft_payload_bytes),
      observation.rejection_logical_bytes);
  const std::uint64_t stored_phase =
      SaturatingAdd(SaturatingAdd(route_phase, observation.accepted_logical_bytes),
                    observation.rejection_logical_bytes);
  observation.peak_deterministic_host_bytes =
      std::max({route_phase, batch_conversion_phase, draft_to_store_phase, stored_phase});
  return observation;
}

[[nodiscard]] std::uint64_t VisibleBatchChecksum(const BatchExecution& execution) {
  apgar::board_ir::StableHashBuilder hash;
  hash.AddString("APGAR-PHASE3-BENCHMARK-BATCH-OUTCOME-V1");
  hash.AddBool(execution.externally_ordered);
  hash.AddU64(execution.queries.size());
  for (const QueryExecution& query : execution.queries) {
    hash.AddU32(query.input_ordinal);
    hash.AddU64(query.visible.query_id);
    hash.AddU64(query.visible.policy_identity);
    hash.AddByte(static_cast<std::uint8_t>(query.visible.failure));
    hash.AddU64(query.visible.scalar_cost);
    hash.AddU64(query.visible.geometry_fingerprint);
  }
  return hash.Finish();
}

[[nodiscard]] std::uint64_t SemanticBatchChecksum(const BatchExecution& execution) {
  apgar::board_ir::StableHashBuilder hash;
  hash.AddString("APGAR-PHASE3-BENCHMARK-SEMANTIC-OUTCOME-V1");
  hash.AddBool(execution.externally_ordered);
  hash.AddU64(execution.queries.size());
  for (const QueryExecution& query : execution.queries) {
    hash.AddU32(query.input_ordinal);
    hash.AddU64(query.visible.query_id);
    hash.AddU64(query.visible.policy_identity);
    hash.AddByte(static_cast<std::uint8_t>(query.visible.failure));
    hash.AddU64(query.visible.scalar_cost);
  }
  return hash.Finish();
}

[[nodiscard]] bool DifferentialMatches(const CaseContext& context, const BatchExecution& execution,
                                       std::uint32_t candidate_count) {
  if (!execution.externally_ordered || execution.queries.size() != candidate_count ||
      context.cpu_oracles.size() < candidate_count) {
    return false;
  }
  std::vector<bool> observed_ordinals(candidate_count, false);
  for (const QueryExecution& query : execution.queries) {
    if (query.input_ordinal >= candidate_count || observed_ordinals[query.input_ordinal]) {
      return false;
    }
    observed_ordinals[query.input_ordinal] = true;
    const VisibleQueryOutcome& observed = query.visible;
    const VisibleQueryOutcome& oracle = context.cpu_oracles[query.input_ordinal];
    if (observed.policy_identity != oracle.policy_identity || observed.failure != oracle.failure ||
        (observed.failure == FailureClass::kReached &&
         observed.scalar_cost != oracle.scalar_cost)) {
      return false;
    }
  }
  return true;
}

struct ExecutionSummary {
  std::uint64_t ordered_outcome_checksum = 0;
  std::uint64_t ordered_semantic_checksum = 0;
  std::uint64_t reached_queries = 0;
  std::array<std::uint64_t, 10> failure_counts{};
  std::uint64_t base_policy_scalar_cost = 0;
  std::uint64_t minimum_reported_policy_scalar_cost = 0;
  std::uint64_t reachable_policy_scalar_cost_sum = 0;
  std::uint64_t examined_states = 0;
  std::uint64_t examined_work = 0;
  std::uint64_t rounds_total = 0;
  std::uint32_t rounds_maximum = 0;
  std::uint64_t persistent_owned_device_bytes = 0;
  std::uint64_t batch_owned_device_bytes = 0;
  std::uint64_t workspace_capacity_device_bytes = 0;
  std::uint64_t peak_owned_device_bytes = 0;
  std::uint64_t prepared_node_lookup_host_bytes = 0;
  std::uint64_t batch_owned_host_bytes = 0;
  std::uint64_t device_to_host_readback_bytes = 0;
  std::uint64_t kernel_launch_count = 0;
  std::uint64_t blocking_status_readback_count = 0;
  std::uint32_t dispatched_rounds = 0;
  std::uint32_t finalization_launch_count = 0;
  std::uint32_t chunk_rounds = 0;
  double cuda_event_milliseconds = 0.0;
  bool externally_ordered = true;
  std::uint32_t parallel_worker_count = 1;
};

[[nodiscard]] ExecutionSummary SummarizeExecution(const BatchExecution& execution) {
  ExecutionSummary summary{
      .ordered_outcome_checksum = VisibleBatchChecksum(execution),
      .ordered_semantic_checksum = SemanticBatchChecksum(execution),
      .persistent_owned_device_bytes = execution.persistent_owned_device_bytes,
      .batch_owned_device_bytes = execution.batch_owned_device_bytes,
      .workspace_capacity_device_bytes = execution.workspace_capacity_device_bytes,
      .peak_owned_device_bytes = execution.peak_owned_device_bytes,
      .prepared_node_lookup_host_bytes = execution.prepared_node_lookup_host_bytes,
      .batch_owned_host_bytes = execution.batch_owned_host_bytes,
      .device_to_host_readback_bytes = execution.device_to_host_readback_bytes,
      .kernel_launch_count = execution.kernel_launch_count,
      .blocking_status_readback_count = execution.blocking_status_readback_count,
      .dispatched_rounds = execution.dispatched_rounds,
      .finalization_launch_count = execution.finalization_launch_count,
      .chunk_rounds = execution.chunk_rounds,
      .cuda_event_milliseconds = execution.cuda_event_milliseconds,
      .externally_ordered = execution.externally_ordered,
      .parallel_worker_count = execution.parallel_worker_count,
  };
  summary.minimum_reported_policy_scalar_cost = std::numeric_limits<std::uint64_t>::max();
  for (const QueryExecution& query : execution.queries) {
    const std::size_t failure_index = static_cast<std::size_t>(query.visible.failure);
    if (failure_index < summary.failure_counts.size()) {
      ++summary.failure_counts[failure_index];
    }
    if (query.visible.failure == FailureClass::kReached) {
      ++summary.reached_queries;
      summary.reachable_policy_scalar_cost_sum =
          SaturatingAdd(summary.reachable_policy_scalar_cost_sum, query.visible.scalar_cost);
      summary.minimum_reported_policy_scalar_cost =
          std::min(summary.minimum_reported_policy_scalar_cost, query.visible.scalar_cost);
      if (query.input_ordinal == 0) {
        summary.base_policy_scalar_cost = query.visible.scalar_cost;
      }
    }
    summary.examined_states = SaturatingAdd(summary.examined_states, query.examined_states);
    summary.examined_work = SaturatingAdd(summary.examined_work, query.examined_work);
    summary.rounds_total = SaturatingAdd(summary.rounds_total, query.rounds);
    summary.rounds_maximum = std::max(summary.rounds_maximum, query.rounds);
  }
  if (summary.reached_queries == 0) {
    summary.minimum_reported_policy_scalar_cost = 0;
  }
  return summary;
}

[[nodiscard]] bool DeterministicExecutionEqual(const ExecutionSummary& left,
                                               const ExecutionSummary& right) noexcept {
  return left.ordered_outcome_checksum == right.ordered_outcome_checksum &&
         left.ordered_semantic_checksum == right.ordered_semantic_checksum &&
         left.reached_queries == right.reached_queries &&
         left.failure_counts == right.failure_counts &&
         left.base_policy_scalar_cost == right.base_policy_scalar_cost &&
         left.minimum_reported_policy_scalar_cost == right.minimum_reported_policy_scalar_cost &&
         left.reachable_policy_scalar_cost_sum == right.reachable_policy_scalar_cost_sum &&
         left.examined_states == right.examined_states &&
         left.examined_work == right.examined_work && left.rounds_total == right.rounds_total &&
         left.rounds_maximum == right.rounds_maximum &&
         left.persistent_owned_device_bytes == right.persistent_owned_device_bytes &&
         left.batch_owned_device_bytes == right.batch_owned_device_bytes &&
         left.workspace_capacity_device_bytes == right.workspace_capacity_device_bytes &&
         left.peak_owned_device_bytes == right.peak_owned_device_bytes &&
         left.prepared_node_lookup_host_bytes == right.prepared_node_lookup_host_bytes &&
         left.batch_owned_host_bytes == right.batch_owned_host_bytes &&
         left.device_to_host_readback_bytes == right.device_to_host_readback_bytes &&
         left.kernel_launch_count == right.kernel_launch_count &&
         left.blocking_status_readback_count == right.blocking_status_readback_count &&
         left.dispatched_rounds == right.dispatched_rounds &&
         left.finalization_launch_count == right.finalization_launch_count &&
         left.chunk_rounds == right.chunk_rounds &&
         left.externally_ordered == right.externally_ordered &&
         left.parallel_worker_count == right.parallel_worker_count;
}

void PublishExecutionCounters(benchmark::State& state, const ExecutionSummary& summary,
                              std::uint32_t candidate_count, bool differential, bool deterministic,
                              double average_cuda_event_milliseconds) {
  state.counters["requested_candidate_count"] = candidate_count;
  state.counters["reached_queries"] = static_cast<double>(summary.reached_queries);
  state.counters["failed_queries"] = static_cast<double>(candidate_count - summary.reached_queries);
  state.counters["invalid_queries"] = static_cast<double>(
      summary.failure_counts[static_cast<std::size_t>(FailureClass::kInvalidInput)]);
  state.counters["unsupported_queries"] = static_cast<double>(
      summary.failure_counts[static_cast<std::size_t>(FailureClass::kUnsupported)]);
  state.counters["unreachable_queries"] = static_cast<double>(
      summary.failure_counts[static_cast<std::size_t>(FailureClass::kDisconnected)]);
  state.counters["resource_exhausted_queries"] = static_cast<double>(
      summary.failure_counts[static_cast<std::size_t>(FailureClass::kResourceExhausted)]);
  state.counters["cancelled_queries"] = static_cast<double>(
      summary.failure_counts[static_cast<std::size_t>(FailureClass::kCancelled)]);
  state.counters["backend_failure_queries"] = static_cast<double>(
      summary.failure_counts[static_cast<std::size_t>(FailureClass::kBackendFailure)]);
  state.counters["validation_failure_queries"] = static_cast<double>(
      summary.failure_counts[static_cast<std::size_t>(FailureClass::kValidationFailure)]);
  state.counters["invariant_failure_queries"] = static_cast<double>(
      summary.failure_counts[static_cast<std::size_t>(FailureClass::kInternalInvariant)]);
  state.counters["base_policy_scalar_cost"] = static_cast<double>(summary.base_policy_scalar_cost);
  state.counters["minimum_reported_policy_scalar_cost"] =
      static_cast<double>(summary.minimum_reported_policy_scalar_cost);
  state.counters["reachable_policy_scalar_cost_sum"] =
      static_cast<double>(summary.reachable_policy_scalar_cost_sum);
  state.counters["examined_states"] = static_cast<double>(summary.examined_states);
  state.counters["examined_work_items"] = static_cast<double>(summary.examined_work);
  state.counters["rounds_total"] = static_cast<double>(summary.rounds_total);
  state.counters["rounds_maximum"] = static_cast<double>(summary.rounds_maximum);
  state.counters["kernel_launch_count"] = static_cast<double>(summary.kernel_launch_count);
  state.counters["blocking_status_readback_count"] =
      static_cast<double>(summary.blocking_status_readback_count);
  state.counters["chunk_rounds"] = static_cast<double>(summary.chunk_rounds);
  state.counters["persistent_owned_vram_bytes"] =
      static_cast<double>(summary.persistent_owned_device_bytes);
  state.counters["batch_owned_vram_bytes"] = static_cast<double>(summary.batch_owned_device_bytes);
  state.counters["workspace_capacity_vram_bytes"] =
      static_cast<double>(summary.workspace_capacity_device_bytes);
  state.counters["peak_owned_vram_bytes"] = static_cast<double>(summary.peak_owned_device_bytes);
  state.counters["prepared_node_lookup_host_bytes"] =
      static_cast<double>(summary.prepared_node_lookup_host_bytes);
  state.counters["gpu_batch_owned_host_bytes"] =
      static_cast<double>(summary.batch_owned_host_bytes);
  state.counters["device_to_host_readback_bytes"] =
      static_cast<double>(summary.device_to_host_readback_bytes);
  state.counters["batch_cuda_event_milliseconds"] = average_cuda_event_milliseconds;
  state.counters["dispatched_rounds"] = static_cast<double>(summary.dispatched_rounds);
  state.counters["finalization_launch_count"] =
      static_cast<double>(summary.finalization_launch_count);
  state.counters["parallel_host_workers"] = static_cast<double>(summary.parallel_worker_count);
  state.counters["ordered_results"] = summary.externally_ordered ? 1.0 : 0.0;
  state.counters["differential_match"] = differential ? 1.0 : 0.0;
  state.counters["deterministic"] = deterministic ? 1.0 : 0.0;
  state.counters["outcome_checksum_hi"] =
      static_cast<double>(summary.ordered_outcome_checksum >> 32U);
  state.counters["outcome_checksum_lo"] = static_cast<double>(
      summary.ordered_outcome_checksum & std::numeric_limits<std::uint32_t>::max());
  state.counters["semantic_outcome_checksum_hi"] =
      static_cast<double>(summary.ordered_semantic_checksum >> 32U);
  state.counters["semantic_outcome_checksum_lo"] = static_cast<double>(
      summary.ordered_semantic_checksum & std::numeric_limits<std::uint32_t>::max());
  state.counters["candidate_queries_per_second"] =
      benchmark::Counter(candidate_count, benchmark::Counter::kIsIterationInvariantRate);
  state.counters["generated_routes_per_second"] =
      benchmark::Counter(summary.reached_queries, benchmark::Counter::kIsIterationInvariantRate);
}

void PublishAdmissionCounters(benchmark::State& state, const AdmissionObservation& observation) {
  state.counters["accepted_candidates"] = static_cast<double>(observation.accepted_candidates);
  state.counters["retained_candidate_pool_size"] =
      static_cast<double>(observation.accepted_candidates);
  state.counters["rejected_candidates"] = static_cast<double>(observation.rejected_candidates);
  state.counters["builder_rejections"] = static_cast<double>(observation.builder_rejections);
  state.counters["store_rejections"] = static_cast<double>(observation.store_rejections);
  state.counters["retained_rejection_records"] =
      static_cast<double>(observation.retained_rejection_records);
  state.counters["unique_geometry_signatures"] =
      static_cast<double>(observation.unique_geometry_signatures);
  state.counters["unique_resource_signatures"] =
      static_cast<double>(observation.unique_resource_signatures);
  state.counters["nondominated_candidates"] =
      static_cast<double>(observation.nondominated_candidates);
  state.counters["minimum_accepted_policy_scalar_cost"] =
      static_cast<double>(observation.minimum_policy_scalar_cost);
  state.counters["base_policy_scalar_cost"] =
      static_cast<double>(observation.base_policy_scalar_cost);
  state.counters["best_of_k_intrinsic_base_cost"] =
      static_cast<double>(observation.best_of_k_intrinsic_base_cost);
  state.counters["base_candidate_intrinsic_base_cost"] =
      static_cast<double>(observation.base_candidate_intrinsic_base_cost);
  state.counters["mean_resource_jaccard"] = observation.mean_resource_jaccard;
  state.counters["minimum_resource_jaccard"] = observation.minimum_resource_jaccard;
  state.counters["resource_diversity"] =
      observation.accepted_candidates < 2 ? 0.0 : 1.0 - observation.mean_resource_jaccard;
  state.counters["mean_geometric_overlap"] = observation.mean_geometric_overlap;
  state.counters["minimum_geometric_overlap"] = observation.minimum_geometric_overlap;
  state.counters["geometric_diversity"] =
      observation.accepted_candidates < 2 ? 0.0 : 1.0 - observation.mean_geometric_overlap;
  state.counters["accepted_logical_bytes"] =
      static_cast<double>(observation.accepted_logical_bytes);
  state.counters["rejection_logical_bytes"] =
      static_cast<double>(observation.rejection_logical_bytes);
  state.counters["peak_deterministic_host_bytes"] =
      static_cast<double>(observation.peak_deterministic_host_bytes);
  state.counters["accepted_candidates_per_second"] = benchmark::Counter(
      observation.accepted_candidates, benchmark::Counter::kIsIterationInvariantRate);
  state.counters["accepted_candidate_yield"] =
      observation.requested_candidates == 0
          ? 0.0
          : static_cast<double>(observation.accepted_candidates) /
                static_cast<double>(observation.requested_candidates);
  state.counters["generated_candidate_acceptance"] =
      observation.reached_queries == 0 ? 0.0
                                       : static_cast<double>(observation.accepted_candidates) /
                                             static_cast<double>(observation.reached_queries);
  state.counters["candidate_order_checksum_hi"] =
      static_cast<double>(observation.ordered_candidate_checksum >> 32U);
  state.counters["candidate_order_checksum_lo"] = static_cast<double>(
      observation.ordered_candidate_checksum & std::numeric_limits<std::uint32_t>::max());
}

void Configure(benchmark::Benchmark* registered) {
  registered->UseRealTime()
      ->Unit(benchmark::kMicrosecond)
      ->MinTime(kBenchmarkMinimumSeconds)
      ->MinWarmUpTime(kBenchmarkWarmupSeconds)
      ->Repetitions(kBenchmarkRepetitions)
      ->DisplayAggregatesOnly(true)
      ->ReportAggregatesOnly(true);
}

void CandidateStageBenchmark(benchmark::State& state, CaseContext* context,
                             BenchmarkContext* benchmark_context, ForcedGenerator generator,
                             TimingStage stage, std::uint32_t candidate_count,
                             BenchmarkDeterminismBaseline* cross_repetition_baseline) {
  std::unique_ptr<apgar::gpu::PreparedPlanarCompiledView> setup_prepared;
  if (IsGpu(generator) && stage != TimingStage::kEndToEnd) {
    apgar::gpu::PreparedPlanarCompiledViewResult prepared_result =
        apgar::gpu::PrepareCudaPlanarCompiledView(context->test_case.board,
                                                  context->test_case.compiled_board,
                                                  *benchmark_context->backend);
    if (std::holds_alternative<apgar::gpu::PlanarGpuFailure>(prepared_result)) {
      state.SkipWithError(std::get<apgar::gpu::PlanarGpuFailure>(prepared_result).detail);
      return;
    }
    setup_prepared = std::get<std::unique_ptr<apgar::gpu::PreparedPlanarCompiledView>>(
        std::move(prepared_result));
  }
  std::optional<BatchExecution> admission_source;
  if (stage == TimingStage::kAdmission) {
    admission_source = RunGenerator(*context, candidate_count, generator,
                                    benchmark_context->parallel_worker_count, setup_prepared.get());
  }
  if (stage == TimingStage::kPreparedEndToEnd) {
    BatchExecution warm_execution =
        RunGenerator(*context, candidate_count, generator, benchmark_context->parallel_worker_count,
                     setup_prepared.get());
    if (!DifferentialMatches(*context, warm_execution, candidate_count)) {
      state.SkipWithError("prepared end-to-end warm-up violated CPU differential semantics");
      return;
    }
    const AdmissionObservation warm_admission =
        AdmitBatch(*context, warm_execution, candidate_count);
    if (!warm_admission.valid) {
      state.SkipWithError(warm_admission.error);
      return;
    }
  }

  std::optional<ExecutionSummary> first_execution;
  std::optional<AdmissionObservation> first_admission;
  bool deterministic = true;
  bool differential = true;
  double cuda_event_milliseconds_sum = 0.0;
  std::uint64_t observed_iterations = 0;
  for (auto _ : state) {
    static_cast<void>(_);
    std::unique_ptr<apgar::gpu::PreparedPlanarCompiledView> iteration_prepared;
    apgar::gpu::PreparedPlanarCompiledView* prepared = setup_prepared.get();
    if (stage == TimingStage::kEndToEnd && IsGpu(generator)) {
      apgar::gpu::PreparedPlanarCompiledViewResult prepared_result =
          apgar::gpu::PrepareCudaPlanarCompiledView(context->test_case.board,
                                                    context->test_case.compiled_board,
                                                    *benchmark_context->backend);
      if (std::holds_alternative<apgar::gpu::PlanarGpuFailure>(prepared_result)) {
        state.SkipWithError(std::get<apgar::gpu::PlanarGpuFailure>(prepared_result).detail);
        break;
      }
      iteration_prepared = std::get<std::unique_ptr<apgar::gpu::PreparedPlanarCompiledView>>(
          std::move(prepared_result));
      prepared = iteration_prepared.get();
    }

    std::optional<BatchExecution> iteration_execution;
    const BatchExecution* execution = nullptr;
    if (stage == TimingStage::kAdmission) {
      execution = &*admission_source;
    } else {
      iteration_execution = RunGenerator(*context, candidate_count, generator,
                                         benchmark_context->parallel_worker_count, prepared);
      execution = &*iteration_execution;
    }
    ExecutionSummary execution_summary = SummarizeExecution(*execution);
    const bool iteration_differential = DifferentialMatches(*context, *execution, candidate_count);
    differential &= iteration_differential;
    cuda_event_milliseconds_sum += execution_summary.cuda_event_milliseconds;

    std::optional<AdmissionObservation> admission;
    if (stage != TimingStage::kExecution) {
      admission = AdmitBatch(*context, *execution, candidate_count);
    }
    // End-to-end and execution rows own their transient results for one
    // iteration. Release them in that same timed iteration so teardown is not
    // shifted into the next sample or asymmetrically omitted from the last.
    iteration_execution.reset();
    iteration_prepared.reset();
    if (admission.has_value() && !admission->valid) {
      state.SkipWithError(admission->error);
      break;
    }
    benchmark::DoNotOptimize(execution_summary.ordered_outcome_checksum);
    if (admission.has_value()) {
      benchmark::DoNotOptimize(admission->ordered_candidate_checksum);
    }
    if (!first_execution.has_value()) {
      first_execution = execution_summary;
      first_admission = admission;
    } else {
      deterministic &= DeterministicExecutionEqual(*first_execution, execution_summary);
      if (first_admission.has_value() != admission.has_value() ||
          (first_admission.has_value() && *first_admission != *admission)) {
        deterministic = false;
      }
    }
    ++observed_iterations;
  }

  if (!first_execution.has_value() || observed_iterations == 0) {
    if (!state.skipped()) {
      state.SkipWithError("benchmark produced no Phase 3 batch observation");
    }
    return;
  }
  if (!cross_repetition_baseline->ordered_outcome_checksum.has_value()) {
    cross_repetition_baseline->ordered_outcome_checksum = first_execution->ordered_outcome_checksum;
    cross_repetition_baseline->admission = first_admission;
  } else {
    deterministic &= *cross_repetition_baseline->ordered_outcome_checksum ==
                     first_execution->ordered_outcome_checksum;
    deterministic &=
        cross_repetition_baseline->admission.has_value() == first_admission.has_value();
    if (cross_repetition_baseline->admission.has_value() && first_admission.has_value()) {
      deterministic &= *cross_repetition_baseline->admission == *first_admission;
    }
  }
  PublishExecutionCounters(state, *first_execution, candidate_count, differential, deterministic,
                           cuda_event_milliseconds_sum / static_cast<double>(observed_iterations));
  if (first_admission.has_value()) {
    PublishAdmissionCounters(state, *first_admission);
  }
  const std::optional<std::uint64_t> process_peak_rss = ProcessLifetimePeakRssBytes();
  state.counters["process_lifetime_peak_rss_bytes"] =
      static_cast<double>(process_peak_rss.value_or(0));
  state.counters["process_peak_rss_available"] = process_peak_rss.has_value() ? 1.0 : 0.0;
  state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * candidate_count);
  if (!deterministic || !differential || !first_execution->externally_ordered) {
    state.SkipWithError("benchmark violated deterministic ordering or CPU differential semantics");
  }
}

void PreparedUploadBenchmark(benchmark::State& state, CaseContext* context,
                             BenchmarkContext* benchmark_context) {
  std::uint64_t successful_uploads = 0;
  for (auto _ : state) {
    static_cast<void>(_);
    apgar::gpu::PreparedPlanarCompiledViewResult prepared_result =
        apgar::gpu::PrepareCudaPlanarCompiledView(context->test_case.board,
                                                  context->test_case.compiled_board,
                                                  *benchmark_context->backend);
    if (std::holds_alternative<apgar::gpu::PlanarGpuFailure>(prepared_result)) {
      state.SkipWithError(std::get<apgar::gpu::PlanarGpuFailure>(prepared_result).detail);
      break;
    }
    std::unique_ptr<apgar::gpu::PreparedPlanarCompiledView> prepared =
        std::get<std::unique_ptr<apgar::gpu::PreparedPlanarCompiledView>>(
            std::move(prepared_result));
    benchmark::DoNotOptimize(prepared.get());
    if (prepared->prepared_node_lookup_host_bytes() != context->prepared_node_lookup_host_bytes) {
      state.SkipWithError("prepared node-lookup accounting changed across identical uploads");
      break;
    }
    ++successful_uploads;
    state.PauseTiming();
    prepared.reset();
    state.ResumeTiming();
  }
  state.counters["persistent_owned_vram_bytes"] =
      static_cast<double>(context->persistent_device_bytes);
  state.counters["prepared_node_lookup_host_bytes"] =
      static_cast<double>(context->prepared_node_lookup_host_bytes);
  state.counters["prepared_uploads_per_second"] =
      benchmark::Counter(1, benchmark::Counter::kIsIterationInvariantRate);
  state.SetItemsProcessed(static_cast<std::int64_t>(successful_uploads));
}

[[nodiscard]] std::vector<apgar::routing::EdgeResourceKey> AllPhysicalResources(
    const apgar::geometry_compiler::CompiledBoard& compiled_board) {
  std::vector<apgar::routing::EdgeResourceKey> resources;
  resources.reserve(
      static_cast<std::size_t>(compiled_board.telemetry().legal_directional_edges / 2U));
  for (const apgar::geometry_compiler::SparseTile& tile : compiled_board.tiles()) {
    for (const apgar::geometry_compiler::CompiledNode& node : tile.nodes) {
      const apgar::geometry_compiler::LatticeIndex source =
          apgar::geometry_compiler::GlobalLatticeIndex(compiled_board.profile(), tile.key,
                                                       node.local_index);
      for (std::uint8_t raw_direction = 0; raw_direction < 4; ++raw_direction) {
        const auto direction = static_cast<apgar::geometry_compiler::Direction>(raw_direction);
        if (!compiled_board.EdgeIsLegal(tile.key.layer, source.x, source.y, direction)) {
          continue;
        }
        const std::optional<apgar::routing::EdgeResourceKey> resource =
            apgar::routing::CanonicalPhysicalEdgeResource(tile.key.layer, source, direction);
        if (resource.has_value()) {
          resources.push_back(*resource);
        }
      }
    }
  }
  std::sort(resources.begin(), resources.end());
  resources.erase(std::unique(resources.begin(), resources.end()), resources.end());
  return resources;
}

[[nodiscard]] std::vector<apgar::routing::EdgeResourceKey> BasePathResources(
    const apgar::benchmark::PlanarCorpusCase& test_case,
    const apgar::routing::NormalizedCandidateGenerationPolicy& base_policy) {
  apgar::routing::CpuRouteRequest request = test_case.request;
  request.candidate_policy = base_policy.policy;
  const apgar::routing::CpuRouteResult result =
      apgar::routing::RouteWithCpuAStar(test_case.board, test_case.compiled_board, request);
  if (!std::holds_alternative<apgar::routing::CpuRoute>(result)) {
    return {};
  }
  const apgar::routing::CpuRoute& route = std::get<apgar::routing::CpuRoute>(result);
  std::vector<apgar::routing::EdgeResourceKey> resources;
  resources.reserve(route.lattice_path.size());
  for (std::size_t index = 1; index < route.lattice_path.size(); ++index) {
    const std::optional<apgar::geometry_compiler::LatticeIndex> source =
        apgar::geometry_compiler::ExactPointToLatticeIndex(test_case.compiled_board.profile(),
                                                           route.lattice_path[index - 1]);
    const std::optional<apgar::geometry_compiler::LatticeIndex> destination =
        apgar::geometry_compiler::ExactPointToLatticeIndex(test_case.compiled_board.profile(),
                                                           route.lattice_path[index]);
    if (!source.has_value() || !destination.has_value()) {
      return {};
    }
    const std::optional<apgar::geometry_compiler::Direction> direction =
        apgar::routing::DirectionBetween(*source, *destination);
    if (!direction.has_value()) {
      return {};
    }
    const std::optional<apgar::routing::EdgeResourceKey> resource =
        apgar::routing::CanonicalPhysicalEdgeResource(request.start_layer, *source, *direction);
    if (!resource.has_value()) {
      return {};
    }
    resources.push_back(*resource);
  }
  std::sort(resources.begin(), resources.end());
  resources.erase(std::unique(resources.begin(), resources.end()), resources.end());
  return resources;
}

[[nodiscard]] std::optional<std::uint64_t> ResourcePenaltyIncrement(
    const apgar::geometry_compiler::CompiledBoard& compiled_board) {
  const apgar::geometry_compiler::DeterministicCosts costs = compiled_board.profile().costs;
  const std::uint64_t maximum_step =
      std::max<std::uint64_t>(costs.orthogonal_step, costs.diagonal_step);
  if (costs.bend > std::numeric_limits<std::uint64_t>::max() - maximum_step) {
    return std::nullopt;
  }
  const std::uint64_t maximum_transition = maximum_step + costs.bend;
  const std::uint64_t represented_nodes = compiled_board.telemetry().represented_nodes;
  if (maximum_transition != 0 &&
      represented_nodes > (std::numeric_limits<std::uint64_t>::max() - 1U) / maximum_transition) {
    return std::nullopt;
  }
  return maximum_transition * represented_nodes + 1U;
}

[[nodiscard]] std::uint64_t BenchmarkSeed(const apgar::benchmark::PlanarCorpusCase& test_case) {
  apgar::board_ir::StableHashBuilder hash;
  hash.AddString("APGAR-PHASE3-BENCHMARK-SEED-V1");
  hash.AddU64(test_case.board.content_hash());
  hash.AddU64(test_case.compiled_board.compiler_profile_fingerprint());
  hash.AddU64(test_case.compiled_board.rule_bucket().identity);
  return hash.Finish();
}

[[nodiscard]] std::optional<std::vector<apgar::routing::NormalizedCandidateGenerationPolicy>>
BuildPolicies(const apgar::benchmark::PlanarCorpusCase& test_case, std::uint64_t deterministic_seed,
              std::uint64_t penalty_increment) {
  apgar::routing::CandidateGenerationPolicy base_policy = test_case.request.candidate_policy;
  base_policy.deterministic_seed = deterministic_seed;
  base_policy.candidate_ordinal = 0;
  const apgar::routing::CandidatePolicyResult normalized_base_result =
      apgar::routing::NormalizeCandidateGenerationPolicy(test_case.compiled_board, base_policy);
  if (!std::holds_alternative<apgar::routing::NormalizedCandidateGenerationPolicy>(
          normalized_base_result)) {
    std::cerr << "unable to normalize Phase 3 base candidate policy for " << test_case.name << ": "
              << std::get<apgar::routing::CandidatePolicyError>(normalized_base_result).detail
              << '\n';
    return std::nullopt;
  }
  const apgar::routing::NormalizedCandidateGenerationPolicy& normalized_base =
      std::get<apgar::routing::NormalizedCandidateGenerationPolicy>(normalized_base_result);
  std::vector<apgar::routing::EdgeResourceKey> resources =
      BasePathResources(test_case, normalized_base);
  if (resources.empty()) {
    resources = AllPhysicalResources(test_case.compiled_board);
  }
  if (resources.empty()) {
    std::cerr << "Phase 3 policy schedule has no physical resources for " << test_case.name << '\n';
    return std::nullopt;
  }
  apgar::routing::CandidatePolicyBatchResult policies =
      apgar::routing::BuildDeterministicAlternativePolicies(
          test_case.compiled_board, base_policy,
          apgar::routing::DeterministicAlternativePolicySchedule{
              .candidate_count = kMaximumCandidateCount,
              .step_surcharge_increment = 1,
              .bend_surcharge_increment = 1,
              .resource_penalty_increment = penalty_increment,
              .alternative_resources = std::move(resources),
          });
  if (!std::holds_alternative<std::vector<apgar::routing::NormalizedCandidateGenerationPolicy>>(
          policies)) {
    std::cerr << "unable to build Phase 3 policy schedule for " << test_case.name << ": "
              << std::get<apgar::routing::CandidatePolicyError>(policies).detail << '\n';
    return std::nullopt;
  }
  return std::get<std::vector<apgar::routing::NormalizedCandidateGenerationPolicy>>(
      std::move(policies));
}

struct EvidenceLabels {
  std::optional<std::string> commit;
  std::optional<std::string> nvidia_kmd_driver;
  bool duplicate = false;
};

[[nodiscard]] EvidenceLabels ExtractEvidenceLabels(int* argc, char** argv) {
  EvidenceLabels labels;
  int destination = 1;
  for (int source = 1; source < *argc; ++source) {
    const std::string_view argument(argv[source]);
    if (argument.starts_with("--apgar_commit=")) {
      labels.duplicate |= labels.commit.has_value();
      labels.commit = argument.substr(15);
    } else if (argument.starts_with("--apgar_nvidia_kmd_driver=")) {
      labels.duplicate |= labels.nvidia_kmd_driver.has_value();
      labels.nvidia_kmd_driver = argument.substr(26);
    } else {
      argv[destination++] = argv[source];
    }
  }
  *argc = destination;
  return labels;
}

[[nodiscard]] std::optional<BenchmarkContext> BuildContext() {
  const std::optional<std::string> fixture =
      apgar::tooling::ReadRunfile("tests/fixtures/m1_exactness.kicad_pcb");
  if (!fixture.has_value()) {
    std::cerr << "unable to read the KiCad Phase 3 corpus fixture\n";
    return std::nullopt;
  }
  apgar::benchmark::PlanarCorpusResult corpus_result =
      apgar::benchmark::BuildPhase3CandidateCorpus(*fixture);
  if (!std::holds_alternative<std::vector<apgar::benchmark::PlanarCorpusCase>>(corpus_result)) {
    std::cerr << std::get<std::string>(corpus_result) << '\n';
    return std::nullopt;
  }
  BenchmarkContext context{
      .cases = {},
      .backend = apgar::gpu::CreateCudaPlanarRouteBackend(),
      .backend_metadata = {},
      .parallel_worker_count = std::max(1U, std::thread::hardware_concurrency()),
      .determinism_baselines = {},
  };
  const apgar::gpu::BackendMetadataResult metadata_result = context.backend->QueryMetadata();
  if (!std::holds_alternative<apgar::gpu::BackendMetadata>(metadata_result)) {
    std::cerr << "unable to query required Phase 3 benchmark backend metadata: "
              << std::get<apgar::gpu::BackendError>(metadata_result).detail << '\n';
    return std::nullopt;
  }
  context.backend_metadata = std::get<apgar::gpu::BackendMetadata>(metadata_result);

  std::vector<apgar::benchmark::PlanarCorpusCase> corpus =
      std::get<std::vector<apgar::benchmark::PlanarCorpusCase>>(std::move(corpus_result));
  context.cases.reserve(corpus.size());
  for (apgar::benchmark::PlanarCorpusCase& test_case : corpus) {
    const std::optional<std::uint64_t> penalty_increment =
        ResourcePenaltyIncrement(test_case.compiled_board);
    if (!penalty_increment.has_value()) {
      std::cerr << "Phase 3 resource penalty bound overflowed for " << test_case.name << '\n';
      return std::nullopt;
    }
    CaseContext case_context{
        .test_case = std::move(test_case),
        .deterministic_seed = 0,
        .resource_penalty_increment = *penalty_increment,
        .policies = {},
        .cpu_oracles = {},
        .persistent_device_bytes = 0,
        .prepared_node_lookup_host_bytes = 0,
    };
    case_context.deterministic_seed = BenchmarkSeed(case_context.test_case);
    std::optional<std::vector<apgar::routing::NormalizedCandidateGenerationPolicy>> policies =
        BuildPolicies(case_context.test_case, case_context.deterministic_seed,
                      case_context.resource_penalty_increment);
    if (!policies.has_value()) {
      return std::nullopt;
    }
    case_context.policies = std::move(*policies);
    const BatchExecution oracles = RunCpuBatch(case_context, kMaximumCandidateCount, false, 1);
    case_context.cpu_oracles.resize(oracles.queries.size());
    std::vector<bool> observed_oracle_ordinals(oracles.queries.size(), false);
    for (const QueryExecution& oracle : oracles.queries) {
      if (oracle.input_ordinal >= case_context.cpu_oracles.size() ||
          observed_oracle_ordinals[oracle.input_ordinal]) {
        std::cerr << "Phase 3 CPU oracle ordering was invalid for " << case_context.test_case.name
                  << '\n';
        return std::nullopt;
      }
      observed_oracle_ordinals[oracle.input_ordinal] = true;
      case_context.cpu_oracles[oracle.input_ordinal] = oracle.visible;
    }

    const apgar::gpu::DeviceCompiledBoardResult device_board_result =
        apgar::gpu::BuildDeviceCompiledBoardV1(case_context.test_case.board,
                                               case_context.test_case.compiled_board);
    if (!std::holds_alternative<apgar::gpu::DeviceCompiledBoardV1>(device_board_result)) {
      std::cerr << "unable to build Phase 3 benchmark device view for "
                << case_context.test_case.name << ": "
                << std::get<apgar::gpu::PlanarGpuFailure>(device_board_result).detail << '\n';
      return std::nullopt;
    }
    const apgar::gpu::DeviceCompiledBoardV1& device_board =
        std::get<apgar::gpu::DeviceCompiledBoardV1>(device_board_result);
    case_context.persistent_device_bytes = device_board.header.estimated_persistent_device_bytes;
    case_context.prepared_node_lookup_host_bytes =
        static_cast<std::uint64_t>(device_board.nodes.size()) * sizeof(std::uint32_t);
    context.cases.push_back(std::move(case_context));
  }
  return context;
}

[[nodiscard]] bool AddContext(const std::string& commit, const std::string& nvidia_kmd_driver,
                              const BenchmarkContext& context) {
  benchmark::AddCustomContext("apgar_commit", commit);
  benchmark::AddCustomContext("apgar_source_identity", "bazel_stable_workspace_status_v1");
  benchmark::AddCustomContext("apgar_source_identity_trust", "canonical_checked_in_invocation_v1");
  benchmark::AddCustomContext("apgar_source_tree_dirty", "false");
  benchmark::AddCustomContext("apgar_nvidia_kmd_driver", nvidia_kmd_driver);
  benchmark::AddCustomContext("apgar_nvidia_kmd_driver_source",
                              "operator_supplied_nvidia_smi_query_gpu_driver_version");
  benchmark::AddCustomContext("apgar_result_schema", "phase3_candidate_bakeoff_v3");
  benchmark::AddCustomContext("apgar_corpus_version",
                              std::to_string(apgar::benchmark::kPhase3CandidateCorpusVersion));
  benchmark::AddCustomContext("apgar_board_ir_schema_version",
                              std::to_string(apgar::board_ir::kBoardSchemaVersion));
  benchmark::AddCustomContext(
      "apgar_compiled_board_schema_version",
      std::to_string(apgar::geometry_compiler::kCompilerProfileSchemaVersion));
  benchmark::AddCustomContext("apgar_candidate_schema_version",
                              std::to_string(apgar::candidates::kRouteCandidateSchemaMajor) + "." +
                                  std::to_string(apgar::candidates::kRouteCandidateSchemaMinor));
  benchmark::AddCustomContext("apgar_candidate_geometry_schema_version",
                              std::to_string(apgar::candidates::kCandidateGeometrySchemaVersion));
  benchmark::AddCustomContext("apgar_candidate_resource_schema_version",
                              std::to_string(apgar::candidates::kCandidateResourceSchemaVersion));
  benchmark::AddCustomContext(
      "apgar_candidate_policy_schema_version",
      std::to_string(apgar::routing::kCandidateGenerationPolicySchemaVersion));
  benchmark::AddCustomContext("apgar_device_candidate_batch_schema_version",
                              std::to_string(apgar::gpu::kDeviceCandidateBatchSchemaVersion));
  benchmark::AddCustomContext("apgar_device_candidate_compact_path_schema_version",
                              std::to_string(apgar::gpu::kDeviceCandidateCompactPathSchemaVersion));
  benchmark::AddCustomContext("apgar_google_benchmark_version", "1.9.5");
  benchmark::AddCustomContext("apgar_google_benchmark_module_lock_sha256",
                              "0bd357fd9db30ee31d5eb4c78b1086ce3d79b4423ce76de19e8a2fa7b2fa2e10");
  benchmark::AddCustomContext("apgar_baseline", "sequential_cpu_astar");
  benchmark::AddCustomContext(
      "apgar_candidate_counts",
      apgar::benchmark::tool_support::JoinUnsignedDecimal(kCandidateCounts));
  benchmark::AddCustomContext(
      "apgar_store_maximum_admission_items",
      std::to_string(apgar::candidates::kDefaultMaximumAdmissionItemsPerTransaction));
  benchmark::AddCustomContext("apgar_store_maximum_rejection_items",
                              std::to_string(kMaximumCandidateCount));
  benchmark::AddCustomContext(
      "apgar_store_maximum_admission_input_bytes",
      std::to_string(apgar::candidates::kDefaultMaximumAdmissionInputBytesPerTransaction));
  benchmark::AddCustomContext(
      "apgar_store_maximum_admission_work_units",
      std::to_string(apgar::candidates::kDefaultMaximumAdmissionWorkUnitsPerTransaction));
  benchmark::AddCustomContext("apgar_policy_schedule",
                              "base_then_length_bend_strong_resource_penalty_resource_ban_v1");
  benchmark::AddCustomContext(
      "apgar_best_of_k_evaluation",
      "candidate_intrinsic_base_cost_reconstructed_from_compiled_base_costs");
  benchmark::AddCustomContext(
      "apgar_policy_cost_comparability",
      "reported_policy_scalar_costs_are_per_policy_and_not_cross_policy_quality_metrics");
  benchmark::AddCustomContext(
      "apgar_timing_scopes",
      "shared_prepare_upload;execution_readback;exact_admission_store;prepared_end_to_end;"
      "end_to_end");
  benchmark::AddCustomContext(
      "apgar_prepared_end_to_end_scope",
      "preexisting_prepare_upload_one_untimed_identical_full_pipeline_warmup_then_reuse_workspace_"
      "execute_readback_validate_exact_admit_store_metrics_result_release_timed_retained_view_"
      "release_excluded");
  benchmark::AddCustomContext(
      "apgar_end_to_end_gpu_scope",
      "prepare_flatten_upload_execute_readback_validate_exact_admit_store_metrics_and_release");
  benchmark::AddCustomContext("apgar_prepared_upload_scope",
                              "prepare_flatten_upload_only_prepared_view_release_excluded");
  benchmark::AddCustomContext(
      "apgar_host_memory_scope",
      "candidate_admission_phase_max_plus_separate_prepared_lookup_and_gpu_batch_ownership");
  benchmark::AddCustomContext(
      "apgar_process_peak_rss_scope",
      "getrusage_RUSAGE_SELF_process_lifetime_high_water_not_row_attributable");
  benchmark::AddCustomContext(
      "apgar_vram_scope",
      "backend_owned_persistent_plus_actual_workspace_capacity_not_driver_or_allocator_pool");
  benchmark::AddCustomContext(
      "apgar_cuda_frontier_launch_model",
      "one_256_thread_block_per_query_cooperative_stable_astar_32_round_device_chunks");
  benchmark::AddCustomContext(
      "apgar_cuda_sweep_launch_model",
      "query_node_heading_departures_plus_query_run_parallel_atomic_min_8_round_host_chunks");
  constexpr std::optional<apgar::gpu::PlanarGeneratorDescriptor> kFrontierDescriptor =
      apgar::gpu::DescribePlanarGenerator(apgar::gpu::PlanarGenerator::kBucketedFrontier);
  constexpr std::optional<apgar::gpu::PlanarGeneratorDescriptor> kSweepDescriptor =
      apgar::gpu::DescribePlanarGenerator(apgar::gpu::PlanarGenerator::kHeadingAwareSweep);
  static_assert(kFrontierDescriptor.has_value() && kSweepDescriptor.has_value());
  benchmark::AddCustomContext("apgar_cuda_frontier_chunk_rounds",
                              std::to_string(kFrontierDescriptor->chunk_rounds));
  benchmark::AddCustomContext("apgar_cuda_sweep_chunk_rounds",
                              std::to_string(kSweepDescriptor->chunk_rounds));
  benchmark::AddCustomContext(
      "apgar_cuda_frontier_launches_per_chunk",
      std::to_string(kFrontierDescriptor->launches_once_per_chunk ? 1U : 0U));
  benchmark::AddCustomContext("apgar_cuda_sweep_kernels_per_round_without_runs",
                              std::to_string(kSweepDescriptor->kernels_per_round_without_runs));
  benchmark::AddCustomContext("apgar_cuda_sweep_kernels_per_round_with_runs",
                              std::to_string(kSweepDescriptor->kernels_per_round_with_runs));
  benchmark::AddCustomContext("apgar_cuda_fixed_batch_launches",
                              std::to_string(apgar::gpu::kCandidateFrontierFixedLaunches));
  benchmark::AddCustomContext("apgar_cuda_frontier_fixed_batch_launches",
                              std::to_string(apgar::gpu::kCandidateFrontierFixedLaunches));
  benchmark::AddCustomContext("apgar_cuda_sweep_fixed_batch_launches",
                              std::to_string(apgar::gpu::kCandidateSweepFixedLaunches));
  benchmark::AddCustomContext(
      "apgar_cuda_sweep_workspace_model",
      "prepared_view_cached_bounded_capacity_with_exclusive_execution_lease");
  benchmark::AddCustomContext(
      "apgar_cuda_sweep_readback_model",
      "result_headers_plus_compact_path_headers_plus_used_packed_path_states");
  benchmark::AddCustomContext(
      "apgar_device_to_host_readback_bytes_scope",
      "final_result_payload_excluding_separately_counted_blocking_status_copies");
  benchmark::AddCustomContext(
      "apgar_cuda_maximum_finalization_launches",
      std::to_string(apgar::gpu::kCandidateBatchMaximumFinalizationLaunches));
  benchmark::AddCustomContext(
      "apgar_cuda_round_synchronization_model",
      "one_blocking_query_status_readback_after_each_32_frontier_or_8_sweep_round_chunk");
  benchmark::AddCustomContext(
      "apgar_cuda_event_timing_scope",
      "initialization_search_chunks_status_copies_finalization_and_final_path_production_envelope");
  benchmark::AddCustomContext(
      "apgar_round_sync_cost_visibility",
      "exact_launch_and_blocking_status_readback_counts_plus_cuda_event_and_execution_wall_time");
  benchmark::AddCustomContext("apgar_examined_state_units",
                              "cpu_expanded_astar_states;cuda_frontier_closed_winner_states;"
                              "cuda_sweep_node_heading_departure_evaluations");
  benchmark::AddCustomContext(
      "apgar_examined_work_units",
      "cpu_expanded_astar_states;cuda_frontier_legal_edge_relaxation_attempts;"
      "cuda_sweep_run_edge_propagation_attempts");
  benchmark::AddCustomContext(
      "apgar_cross_generator_work_comparability",
      "state_and_work_counts_are_generator_specific_diagnostics_not_raw_cross_generator_scores");
  benchmark::AddCustomContext(
      "apgar_dispatch_evidence_eligibility",
      "eligible_for_phase3_gpu_heavy_batch_measurement_not_dispatch_promotion");
  benchmark::AddCustomContext("apgar_repetitions", std::to_string(kBenchmarkRepetitions));
  benchmark::AddCustomContext("apgar_min_time_seconds", std::to_string(kBenchmarkMinimumSeconds));
  benchmark::AddCustomContext("apgar_min_warmup_seconds", std::to_string(kBenchmarkWarmupSeconds));
  benchmark::AddCustomContext("apgar_parallel_host_workers",
                              std::to_string(context.parallel_worker_count));
  benchmark::AddCustomContext(
      "apgar_default_cpu_toolchain",
      "LLVM 22.1.8 via hermetic-llvm module llvm@0.8.11 checksum-pinned zero-sysroot "
      "module_lock_sha256=b40edb2bb2ed271bf613b396d245cb473c42fb057a2b26a3bc7d7e8bfcf6aa71");
  benchmark::AddCustomContext("apgar_benchmark_cpp_toolchain",
                              "GCC 15.2.0 checksum-pinned distribution/sysroot archive_sha256="
                              "ed6a74810fe42979493f3b0ef188f1b7a388817f496f4c9cee3f7183415ab821");
  benchmark::AddCustomContext(
      "apgar_cuda_toolkit_manifest",
      "13.0.2 sha256=fce66717a81c510ffeb89ecc3e79849ab34af3b80139f750876d9033e31d71c2");
  benchmark::AddCustomContext(
      "apgar_nvcc_component",
      "13.0.88 sha256=48e35be3cfbf4b4fbc16828eaec8a7048ee789403049dc409f7b643d6259cf7a");
  benchmark::AddCustomContext(
      "apgar_cudart_component",
      "13.0.96 sha256=25b8071951baba827be1580b841d363464f6ee6c39f48d33a81646f90cc95ed1");

  apgar::benchmark::tool_support::AddHostContext();

  const apgar::gpu::BackendMetadata& metadata = context.backend_metadata;
  benchmark::AddCustomContext("apgar_backend", metadata.backend);
  benchmark::AddCustomContext("apgar_device_name", metadata.device_name);
  benchmark::AddCustomContext("apgar_device_uuid", metadata.device_uuid);
  benchmark::AddCustomContext("apgar_supported_device_class",
                              std::to_string(metadata.compute_capability_major) + "." +
                                  std::to_string(metadata.compute_capability_minor));
  benchmark::AddCustomContext("apgar_compute_capability",
                              std::to_string(metadata.compute_capability_major) + "." +
                                  std::to_string(metadata.compute_capability_minor));
  benchmark::AddCustomContext("apgar_cuda_runtime", std::to_string(metadata.runtime_version));
  benchmark::AddCustomContext("apgar_cuda_driver", std::to_string(metadata.driver_version));
  benchmark::AddCustomContext("apgar_global_memory_bytes",
                              std::to_string(metadata.global_memory_bytes));

  for (const CaseContext& case_context : context.cases) {
    const apgar::benchmark::PlanarCorpusCase& test_case = case_context.test_case;
    const apgar::geometry_compiler::CompilerProfile& profile = test_case.compiled_board.profile();
    benchmark::AddCustomContext(
        "apgar_case_" + test_case.name,
        "family=" + test_case.family + ";board_hash=" +
            std::to_string(test_case.board.content_hash()) + ";compiler_profile_fingerprint=" +
            std::to_string(test_case.compiled_board.compiler_profile_fingerprint()) +
            ";compiler_version=" + std::to_string(test_case.compiled_board.compiler_version()) +
            ";routing_profile_fingerprint=" +
            std::to_string(
                apgar::routing::FingerprintRoutingProfile(test_case.board.data().routing_profile)) +
            ";rule_bucket=" + std::to_string(test_case.compiled_board.rule_bucket().identity) +
            ";nodes=" + std::to_string(test_case.compiled_board.telemetry().represented_nodes) +
            ";legal_edges=" +
            std::to_string(test_case.compiled_board.telemetry().legal_directional_edges) +
            ";lattice_step=" + std::to_string(profile.lattice_step) +
            ";tile=" + std::to_string(profile.tile_width_nodes) + "x" +
            std::to_string(profile.tile_height_nodes) +
            ";heading_mask=" + std::to_string(profile.heading_mask) +
            ";base_costs=" + std::to_string(profile.costs.orthogonal_step) + "," +
            std::to_string(profile.costs.diagonal_step) + "," + std::to_string(profile.costs.bend) +
            ";seed=" + std::to_string(case_context.deterministic_seed) +
            ";resource_penalty_increment=" +
            std::to_string(case_context.resource_penalty_increment) +
            ";persistent_device_bytes=" + std::to_string(case_context.persistent_device_bytes) +
            ";prepared_node_lookup_host_bytes=" +
            std::to_string(case_context.prepared_node_lookup_host_bytes));
    std::string policy_identities;
    for (std::size_t index = 0; index < case_context.policies.size(); ++index) {
      if (index != 0) {
        policy_identities.push_back(',');
      }
      policy_identities += std::to_string(case_context.policies[index].identity);
    }
    benchmark::AddCustomContext("apgar_case_" + test_case.name + "_policy_identities",
                                std::move(policy_identities));
  }
  return true;
}

void RegisterBenchmarks(BenchmarkContext* context) {
  constexpr std::array<ForcedGenerator, 4> kGenerators = {
      ForcedGenerator::kSequentialCpuAStar,
      ForcedGenerator::kParallelCpuAStar,
      ForcedGenerator::kCudaFrontier,
      ForcedGenerator::kCudaSweep,
  };
  constexpr std::array<TimingStage, 4> kStages = {
      TimingStage::kExecution,
      TimingStage::kAdmission,
      TimingStage::kPreparedEndToEnd,
      TimingStage::kEndToEnd,
  };
  for (CaseContext& case_context : context->cases) {
    Configure(benchmark::RegisterBenchmark(
        ("phase3/shared_cuda/prepared_upload/" + case_context.test_case.name).c_str(),
        PreparedUploadBenchmark, &case_context, context));
    for (const std::uint32_t candidate_count : kCandidateCounts) {
      for (const ForcedGenerator generator : kGenerators) {
        for (const TimingStage stage : kStages) {
          const std::string name = "phase3/" + std::string(GeneratorName(generator)) + "/" +
                                   std::string(StageName(stage)) + "/" +
                                   case_context.test_case.name + "/k_" +
                                   std::to_string(candidate_count);
          auto baseline = std::make_unique<BenchmarkDeterminismBaseline>();
          BenchmarkDeterminismBaseline* baseline_pointer = baseline.get();
          context->determinism_baselines.push_back(std::move(baseline));
          Configure(benchmark::RegisterBenchmark(name.c_str(), CandidateStageBenchmark,
                                                 &case_context, context, generator, stage,
                                                 candidate_count, baseline_pointer));
        }
      }
    }
  }
}

}  // namespace

int main(int argc, char** argv) {
  benchmark::MaybeReenterWithoutASLR(argc, argv);
  const EvidenceLabels labels = ExtractEvidenceLabels(&argc, argv);
  if (labels.duplicate || !labels.commit.has_value() ||
      !apgar::benchmark::IsPublishableBenchmarkSource(
          *labels.commit, apgar::benchmark::kPhase3BuiltCommit,
          apgar::benchmark::kPhase3SourceStamped, apgar::benchmark::kPhase3BuiltFromDirtyTree) ||
      !labels.nvidia_kmd_driver.has_value() ||
      !apgar::benchmark::IsDriverVersionLabel(*labels.nvidia_kmd_driver)) {
    std::cerr << "phase3_candidate_benchmark requires one --apgar_commit=<exactly 40 lowercase "
                 "hexadecimal characters> matching a clean VCS-derived --config=benchmark "
                 "source stamp and one "
                 "--apgar_nvidia_kmd_driver=<numeric dotted version from nvidia-smi>\n";
    return 2;
  }
  std::optional<BenchmarkContext> context = BuildContext();
  if (!context.has_value()) {
    std::cerr << "failed to build Phase 3 candidate benchmark context\n";
    return 2;
  }
  if (!AddContext(*labels.commit, *labels.nvidia_kmd_driver, *context)) {
    return 2;
  }
  RegisterBenchmarks(&*context);
  benchmark::Initialize(&argc, argv);
  if (benchmark::ReportUnrecognizedArguments(argc, argv)) {
    return 2;
  }
  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();
  return 0;
}
