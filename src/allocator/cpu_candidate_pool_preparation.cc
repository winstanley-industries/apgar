#include "apgar/allocator/cpu_candidate_pool_preparation.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <ranges>
#include <set>
#include <system_error>
#include <thread>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

#include "apgar/board_ir/stable_hash.h"
#include "src/allocator/cpu_candidate_pool_preparation_internal.h"
#include "src/candidates/route_candidate_internal.h"

namespace apgar::allocator {
namespace {

using UWide = __uint128_t;

[[nodiscard]] auto NetKey(board_ir::EntityRef net) noexcept {
  return std::pair{net.id, net.generation};
}

[[nodiscard]] CpuCandidatePoolPreparationError Error(
    CpuCandidatePoolPreparationErrorCode code, std::string_view invariant_id,
    std::string_view detail, std::optional<board_ir::EntityRef> net = std::nullopt) noexcept {
  return CpuCandidatePoolPreparationError{
      .code = code,
      .invariant_id = invariant_id,
      .detail = detail,
      .net = net,
      .policy_identity = std::nullopt,
      .query_identity = std::nullopt,
      .expected_value = std::nullopt,
      .actual_value = std::nullopt,
      .policy_error_code = std::nullopt,
      .route_failure_code = std::nullopt,
      .candidate_rejection_code = std::nullopt,
  };
}

[[nodiscard]] bool CheckedAdd(std::uint64_t term, std::uint64_t& total) noexcept {
  if (term > std::numeric_limits<std::uint64_t>::max() - total) {
    return false;
  }
  total += term;
  return true;
}

[[nodiscard]] std::optional<std::uint64_t> CheckedMultiply(std::uint64_t left,
                                                           std::uint64_t right) noexcept {
  const UWide product = static_cast<UWide>(left) * right;
  if (product > std::numeric_limits<std::uint64_t>::max()) {
    return std::nullopt;
  }
  return static_cast<std::uint64_t>(product);
}

[[nodiscard]] bool CandidateStoreConfigIsValid(
    const candidates::CandidateStoreConfig& config) noexcept {
  return config.maximum_candidates_per_net > 0 && config.maximum_candidate_bytes_per_net > 0 &&
         config.maximum_rejection_records > 0 &&
         config.maximum_rejection_items_per_transaction > 0 &&
         config.maximum_admission_items_per_transaction > 0 &&
         config.maximum_admission_input_bytes_per_transaction > 0 &&
         config.maximum_admission_work_units_per_transaction > 0;
}

[[nodiscard]] bool ConfigurationIsValid(const CpuCandidatePoolPreparationConfig& config) noexcept {
  const CpuCandidatePoolPreparationLimits& limits = config.limits;
  return config.worker_count > 0 && config.worker_count <= kMaximumCpuCandidatePoolWorkers &&
         limits.maximum_nets > 0 && limits.maximum_nets <= kMaximumCpuCandidatePoolNets &&
         limits.maximum_candidates_per_net > 0 &&
         limits.maximum_candidates_per_net <= kMaximumCpuCandidatesPerNet &&
         limits.maximum_total_queries > 0 &&
         limits.maximum_total_queries <= kMaximumCpuCandidatePoolQueries &&
         limits.maximum_cpu_work_units_per_query > 0 &&
         limits.maximum_aggregate_cpu_work_units > 0 &&
         limits.maximum_generated_bytes_per_query > 0 &&
         limits.maximum_aggregate_generated_bytes > 0 &&
         limits.maximum_retained_candidate_bytes > 0 &&
         CandidateStoreConfigIsValid(limits.candidate_store);
}

[[nodiscard]] bool SameResourceLattice(const geometry_compiler::CompiledBoard& left,
                                       const geometry_compiler::CompiledBoard& right) noexcept {
  return left.source_board_content_hash() == right.source_board_content_hash() &&
         left.compiler_profile_fingerprint() == right.compiler_profile_fingerprint() &&
         left.compiler_version() == right.compiler_version();
}

[[nodiscard]] std::uint64_t NonzeroHash(board_ir::StableHashBuilder& hash) noexcept {
  const std::uint64_t value = hash.Finish();
  return value == 0 ? 1 : value;
}

struct NetPlan {
  const CpuCandidatePoolNetSchedule* submitted = nullptr;
  std::vector<routing::NormalizedCandidateGenerationPolicy> policies;
};

[[nodiscard]] std::uint64_t BatchIdentity(const board_ir::BoardSnapshot& board,
                                          std::span<const NetPlan> plans) noexcept {
  board_ir::StableHashBuilder hash;
  hash.AddString("APGAR-P4R04-CPU-CANDIDATE-POOL-BATCH-V1");
  hash.AddU64(board.content_hash());
  hash.AddU64(static_cast<std::uint64_t>(plans.size()));
  for (const NetPlan& plan : plans) {
    const routing::PlanarRouteRequest& request = plan.submitted->request;
    hash.AddU64(request.net.id);
    hash.AddU32(request.net.generation);
    hash.AddI64(request.start.x);
    hash.AddI64(request.start.y);
    hash.AddI64(request.goal.x);
    hash.AddI64(request.goal.y);
    hash.AddU32(request.start_layer);
    hash.AddU32(request.goal_layer);
    hash.AddU64(plan.submitted->compiled_board->compiler_profile_fingerprint());
    hash.AddU32(plan.submitted->compiled_board->compiler_version());
    hash.AddU64(static_cast<std::uint64_t>(plan.policies.size()));
    for (const routing::NormalizedCandidateGenerationPolicy& policy : plan.policies) {
      hash.AddU64(policy.identity);
      hash.AddU32(policy.policy.candidate_ordinal);
    }
  }
  return NonzeroHash(hash);
}

[[nodiscard]] std::uint64_t QueryIdentity(
    std::uint64_t batch_identity, board_ir::EntityRef net,
    const routing::NormalizedCandidateGenerationPolicy& policy) noexcept {
  board_ir::StableHashBuilder hash;
  hash.AddString("APGAR-P4R04-CPU-CANDIDATE-POOL-QUERY-V1");
  hash.AddU64(batch_identity);
  hash.AddU64(net.id);
  hash.AddU32(net.generation);
  hash.AddU64(policy.identity);
  hash.AddU32(policy.policy.candidate_ordinal);
  return NonzeroHash(hash);
}

enum class DraftOrigin : std::uint8_t {
  kReached = 0,
  kDisconnected = 1,
  kUnsupported = 2,
  kBuildRejected = 3,
};

enum class WorkerFailure : std::uint8_t {
  kNone = 0,
  kWorkBound = 1,
  kResourceExhausted = 2,
  kInternalInvariant = 3,
};

struct QuerySlot {
  std::size_t net_index = 0;
  board_ir::EntityRef net;
  const geometry_compiler::CompiledBoard* compiled_board = nullptr;
  routing::PlanarRouteRequest request;
  routing::NormalizedCandidateGenerationPolicy normalized_policy;
  std::uint64_t query_identity = 0;
  std::optional<candidates::CandidateDraftBuildResult> draft;
  DraftOrigin draft_origin = DraftOrigin::kReached;
  std::optional<routing::RouteFailureCode> route_failure_code;
  WorkerFailure worker_failure = WorkerFailure::kNone;
  std::uint64_t work_units = 0;
};

[[nodiscard]] candidates::CandidateProvenance CpuProvenance(const QuerySlot& slot,
                                                            std::uint64_t batch_identity) {
  return candidates::CandidateProvenance{
      .generator = candidates::CandidateGeneratorKind::kCpuAStar,
      .generator_version = 1,
      .backend = candidates::CandidateBackendKind::kCpu,
      .supported_device_class = std::string(candidates::kCpuReferenceDeviceClassV1),
      .deterministic_seed = slot.normalized_policy.policy.deterministic_seed,
      .batch_identity = batch_identity,
      .query_identity = slot.query_identity,
      .candidate_ordinal = slot.normalized_policy.policy.candidate_ordinal,
  };
}

[[nodiscard]] candidates::CandidateDraftBuildResult RouteDiagnostic(
    const board_ir::BoardSnapshot& board, const QuerySlot& slot, std::uint64_t batch_identity,
    const routing::RouteFailure& failure, DraftOrigin origin) {
  const bool unsupported = origin == DraftOrigin::kUnsupported;
  return candidates::internal::RejectGeneratedCandidateDraft(
      board, *slot.compiled_board, slot.request, slot.normalized_policy,
      CpuProvenance(slot, batch_identity), candidates::AssociationsFor(board, *slot.compiled_board),
      candidates::CandidateLifecycleStage::kGenerated,
      unsupported ? candidates::CandidateRejectionCode::kUnsupported
                  : candidates::CandidateRejectionCode::kExactValidation,
      unsupported ? "allocator.cpu_pool.route_unsupported.v1"
                  : "allocator.cpu_pool.route_disconnected.v1",
      failure.detail);
}

[[nodiscard]] CpuCandidateColumnOutcomeCode ColumnOutcomeFor(DraftOrigin origin) noexcept {
  switch (origin) {
    case DraftOrigin::kReached:
      return CpuCandidateColumnOutcomeCode::kAdmissionRejected;
    case DraftOrigin::kDisconnected:
      return CpuCandidateColumnOutcomeCode::kDisconnectedRoute;
    case DraftOrigin::kUnsupported:
      return CpuCandidateColumnOutcomeCode::kUnsupportedRoute;
    case DraftOrigin::kBuildRejected:
      return CpuCandidateColumnOutcomeCode::kCandidateBuildRejected;
  }
  return CpuCandidateColumnOutcomeCode::kAdmissionRejected;
}

[[nodiscard]] CpuCandidatePoolPreparationError WorkerError(const QuerySlot& slot) noexcept {
  CpuCandidatePoolPreparationError error;
  error.net = slot.net;
  error.policy_identity = slot.normalized_policy.identity;
  error.query_identity = slot.query_identity;
  error.route_failure_code = slot.route_failure_code;
  switch (slot.worker_failure) {
    case WorkerFailure::kWorkBound:
      error.code = CpuCandidatePoolPreparationErrorCode::kBoundExhausted;
      error.invariant_id = "allocator.cpu_pool.query_work_budget.v1";
      error.detail = "A CPU candidate query exhausted its deterministic work bound";
      return error;
    case WorkerFailure::kResourceExhausted:
      error.code = CpuCandidatePoolPreparationErrorCode::kResourceExhausted;
      error.invariant_id = "allocator.cpu_pool.worker_resource.v1";
      error.detail = "CPU candidate generation exhausted a required host resource";
      return error;
    case WorkerFailure::kInternalInvariant:
      error.code = CpuCandidatePoolPreparationErrorCode::kInternalInvariant;
      error.invariant_id = "allocator.cpu_pool.worker_invariant.v1";
      error.detail = "Production CPU candidate generation reported an invariant failure";
      return error;
    case WorkerFailure::kNone:
      break;
  }
  return Error(CpuCandidatePoolPreparationErrorCode::kInternalInvariant,
               "allocator.cpu_pool.worker_state.v1",
               "Worker error mapping reached a successful query slot", slot.net);
}

[[nodiscard]] CpuCandidatePoolPreparationError StoreTransactionError(
    const candidates::CandidateRejection& rejection) noexcept {
  CpuCandidatePoolPreparationError error;
  error.invariant_id = "allocator.cpu_pool.store_transaction.v1";
  error.detail = "CandidateStore rejected the complete mixed draft transaction";
  error.candidate_rejection_code = rejection.code;
  error.expected_value = rejection.expected_value;
  error.actual_value = rejection.actual_value;
  switch (rejection.code) {
    case candidates::CandidateRejectionCode::kBudgetExhausted:
      error.code = CpuCandidatePoolPreparationErrorCode::kBoundExhausted;
      break;
    case candidates::CandidateRejectionCode::kMemoryAccountingOverflow:
      error.code = CpuCandidatePoolPreparationErrorCode::kArithmeticOverflow;
      break;
    case candidates::CandidateRejectionCode::kInvalidInput:
      error.code = CpuCandidatePoolPreparationErrorCode::kInvalidConfiguration;
      break;
    case candidates::CandidateRejectionCode::kBackendFailure:
      error.code = CpuCandidatePoolPreparationErrorCode::kResourceExhausted;
      break;
    case candidates::CandidateRejectionCode::kUnsupported:
    case candidates::CandidateRejectionCode::kExactValidation:
    case candidates::CandidateRejectionCode::kAssociationMismatch:
    case candidates::CandidateRejectionCode::kResourceMismatch:
    case candidates::CandidateRejectionCode::kMetricMismatch:
    case candidates::CandidateRejectionCode::kSignatureMismatch:
    case candidates::CandidateRejectionCode::kDuplicateIdentity:
    case candidates::CandidateRejectionCode::kDuplicateGeometry:
    case candidates::CandidateRejectionCode::kDuplicateResources:
    case candidates::CandidateRejectionCode::kMemoryAccountingMismatch:
    case candidates::CandidateRejectionCode::kInternalInvariant:
    case candidates::CandidateRejectionCode::kCancelled:
      error.code = CpuCandidatePoolPreparationErrorCode::kInternalInvariant;
      break;
  }
  return error;
}

}  // namespace

struct PreparedCpuCandidatePoolsFactory {
  [[nodiscard]] static PreparedCpuCandidatePools Make(std::span<const NetPlan> plans,
                                                      std::uint64_t batch_identity,
                                                      std::uint64_t query_count,
                                                      std::uint64_t cpu_work_units,
                                                      std::uint64_t generated_bytes) {
    PreparedCpuCandidatePools prepared;
    prepared.batch_identity_ = batch_identity;
    prepared.query_count_ = query_count;
    prepared.cpu_work_units_ = cpu_work_units;
    prepared.generated_bytes_ = generated_bytes;
    prepared.pools_.reserve(plans.size());
    for (const NetPlan& plan : plans) {
      prepared.pools_.push_back(PreparedCpuCandidatePool(plan.submitted->request.net));
      const std::size_t reserve_count = static_cast<std::size_t>(std::min<std::uint64_t>(
          plan.policies.size(), plan.submitted->candidate_schedule.candidate_count));
      prepared.pools_.back().candidates_.reserve(reserve_count);
      prepared.pools_.back().one_world_candidates_.reserve(reserve_count);
    }
    prepared.columns_.reserve(static_cast<std::size_t>(query_count));
    prepared.diagnostics_.reserve(static_cast<std::size_t>(query_count));
    return prepared;
  }

  static void AddCandidate(PreparedCpuCandidatePools& prepared, std::size_t net_index,
                           candidates::StoredCandidate candidate) {
    prepared.pools_[net_index].candidates_.push_back(std::move(candidate));
  }

  static void AddColumn(PreparedCpuCandidatePools& prepared, CpuCandidatePoolColumn column) {
    prepared.columns_.push_back(std::move(column));
  }

  static void AddDiagnostic(PreparedCpuCandidatePools& prepared,
                            CpuCandidatePoolDiagnostic diagnostic) {
    prepared.diagnostics_.push_back(std::move(diagnostic));
  }

  [[nodiscard]] static bool FinalizePools(PreparedCpuCandidatePools& prepared) {
    for (PreparedCpuCandidatePool& pool : prepared.pools_) {
      std::ranges::sort(pool.candidates_, [](const candidates::StoredCandidate& left,
                                             const candidates::StoredCandidate& right) {
        return candidates::CandidateRanksBefore(*left, *right);
      });
      for (const candidates::StoredCandidate& candidate : pool.candidates_) {
        if (!CheckedAdd(candidate->logical_bytes(), prepared.retained_candidate_bytes_)) {
          return false;
        }
        pool.one_world_candidates_.push_back(candidate.get());
      }
    }
    return true;
  }
};

namespace {

CpuCandidatePoolPreparationResult PrepareImpl(
    const board_ir::BoardSnapshot& board,
    std::span<const CpuCandidatePoolNetSchedule> submitted_schedules,
    CpuCandidatePoolPreparationConfig config,
    internal::CpuCandidatePoolPreparationTestHooks hooks) {
  if (!ConfigurationIsValid(config)) {
    return Error(CpuCandidatePoolPreparationErrorCode::kInvalidConfiguration,
                 "allocator.cpu_pool.configuration.v1",
                 "CPU candidate-pool configuration must be positive and within hard bounds");
  }
  if (submitted_schedules.empty()) {
    return Error(CpuCandidatePoolPreparationErrorCode::kInvalidInput,
                 "allocator.cpu_pool.empty_request.v1",
                 "CPU candidate-pool preparation requires at least one explicit net schedule");
  }
  if (submitted_schedules.size() > config.limits.maximum_nets) {
    CpuCandidatePoolPreparationError error = Error(
        CpuCandidatePoolPreparationErrorCode::kBoundExhausted, "allocator.cpu_pool.net_count.v1",
        "Explicit net schedules exceed the configured net bound");
    error.expected_value = config.limits.maximum_nets;
    error.actual_value = submitted_schedules.size();
    return error;
  }

  try {
    std::vector<const CpuCandidatePoolNetSchedule*> ordered_schedules;
    ordered_schedules.reserve(submitted_schedules.size());
    for (const CpuCandidatePoolNetSchedule& schedule : submitted_schedules) {
      ordered_schedules.push_back(&schedule);
    }
    std::ranges::sort(ordered_schedules, [](const CpuCandidatePoolNetSchedule* left,
                                            const CpuCandidatePoolNetSchedule* right) {
      return NetKey(left->request.net) < NetKey(right->request.net);
    });

    std::uint64_t total_queries = 0;
    const geometry_compiler::CompiledBoard* common_compiled = nullptr;
    const board_ir::EntityRef* previous_net = nullptr;
    for (const CpuCandidatePoolNetSchedule* schedule : ordered_schedules) {
      const board_ir::EntityRef net = schedule->request.net;
      if (previous_net != nullptr && *previous_net == net) {
        return Error(CpuCandidatePoolPreparationErrorCode::kInvalidInput,
                     "allocator.cpu_pool.duplicate_net.v1",
                     "Each requested net must have exactly one candidate schedule", net);
      }
      previous_net = &schedule->request.net;
      if (schedule->compiled_board == nullptr) {
        return Error(CpuCandidatePoolPreparationErrorCode::kInvalidInput,
                     "allocator.cpu_pool.null_compiled_context.v1",
                     "A candidate schedule has no explicit prepared compiler context", net);
      }
      if (board.FindNet(net) == nullptr) {
        return Error(CpuCandidatePoolPreparationErrorCode::kInvalidInput,
                     "allocator.cpu_pool.unknown_net.v1",
                     "A candidate schedule names a net absent from the Board IR snapshot", net);
      }
      if (routing::ValidatePreparedCompiledBoardAssociation(board, *schedule->compiled_board)
              .has_value()) {
        return Error(CpuCandidatePoolPreparationErrorCode::kAssociationMismatch,
                     "allocator.cpu_pool.compiled_association.v1",
                     "A prepared compiler context does not match the Board IR snapshot", net);
      }
      if (common_compiled == nullptr) {
        common_compiled = schedule->compiled_board;
      } else if (!SameResourceLattice(*common_compiled, *schedule->compiled_board)) {
        return Error(CpuCandidatePoolPreparationErrorCode::kAssociationMismatch,
                     "allocator.cpu_pool.resource_lattice.v1",
                     "Requested prepared contexts do not share one physical resource lattice", net);
      }
      if (const std::optional<routing::RouteRequestAdmissionIssue> request_issue =
              routing::ValidateTwoTerminalRouteRequest(board, *schedule->compiled_board,
                                                       schedule->request);
          request_issue.has_value()) {
        const bool association =
            *request_issue == routing::RouteRequestAdmissionIssue::kRoutingProfileNetMismatch;
        return Error(association ? CpuCandidatePoolPreparationErrorCode::kAssociationMismatch
                                 : CpuCandidatePoolPreparationErrorCode::kInvalidInput,
                     association ? "allocator.cpu_pool.request_association.v1"
                                 : "allocator.cpu_pool.route_request.v1",
                     association ? "A route request does not match its prepared net context"
                                 : "A route request is invalid for bounded M1 CPU preparation",
                     net);
      }
      const std::uint64_t candidate_count = schedule->candidate_schedule.candidate_count;
      if (candidate_count == 0) {
        return Error(CpuCandidatePoolPreparationErrorCode::kInvalidConfiguration,
                     "allocator.cpu_pool.empty_candidate_schedule.v1",
                     "A deterministic per-net candidate schedule must request at least one query",
                     net);
      }
      if (candidate_count > config.limits.maximum_candidates_per_net ||
          candidate_count > kMaximumCpuCandidatesPerNet) {
        CpuCandidatePoolPreparationError error =
            Error(CpuCandidatePoolPreparationErrorCode::kBoundExhausted,
                  "allocator.cpu_pool.candidates_per_net.v1",
                  "A per-net candidate schedule exceeds its configured bound", net);
        error.expected_value = config.limits.maximum_candidates_per_net;
        error.actual_value = candidate_count;
        return error;
      }
      if (!CheckedAdd(candidate_count, total_queries)) {
        return Error(CpuCandidatePoolPreparationErrorCode::kArithmeticOverflow,
                     "allocator.cpu_pool.query_count_overflow.v1",
                     "Aggregate candidate query count overflowed uint64", net);
      }
      if (total_queries > config.limits.maximum_total_queries) {
        CpuCandidatePoolPreparationError error =
            Error(CpuCandidatePoolPreparationErrorCode::kBoundExhausted,
                  "allocator.cpu_pool.query_count.v1",
                  "Aggregate candidate queries exceed the configured bound", net);
        error.expected_value = config.limits.maximum_total_queries;
        error.actual_value = total_queries;
        return error;
      }
    }

    const std::optional<std::uint64_t> maximum_aggregate_work =
        CheckedMultiply(total_queries, config.limits.maximum_cpu_work_units_per_query);
    if (!maximum_aggregate_work.has_value()) {
      return Error(CpuCandidatePoolPreparationErrorCode::kArithmeticOverflow,
                   "allocator.cpu_pool.aggregate_work_overflow.v1",
                   "Preflight aggregate CPU work overflowed uint64");
    }
    if (*maximum_aggregate_work > config.limits.maximum_aggregate_cpu_work_units) {
      CpuCandidatePoolPreparationError error =
          Error(CpuCandidatePoolPreparationErrorCode::kBoundExhausted,
                "allocator.cpu_pool.aggregate_work_budget.v1",
                "Preflight aggregate CPU work exceeds the configured bound");
      error.expected_value = config.limits.maximum_aggregate_cpu_work_units;
      error.actual_value = *maximum_aggregate_work;
      return error;
    }
    const std::optional<std::uint64_t> maximum_generated_bytes =
        CheckedMultiply(total_queries, config.limits.maximum_generated_bytes_per_query);
    if (!maximum_generated_bytes.has_value()) {
      return Error(CpuCandidatePoolPreparationErrorCode::kArithmeticOverflow,
                   "allocator.cpu_pool.generated_bytes_overflow.v1",
                   "Preflight aggregate generated bytes overflowed uint64");
    }
    if (*maximum_generated_bytes > config.limits.maximum_aggregate_generated_bytes) {
      CpuCandidatePoolPreparationError error =
          Error(CpuCandidatePoolPreparationErrorCode::kBoundExhausted,
                "allocator.cpu_pool.generated_bytes_budget.v1",
                "Preflight aggregate generated bytes exceed the configured bound");
      error.expected_value = config.limits.maximum_aggregate_generated_bytes;
      error.actual_value = *maximum_generated_bytes;
      return error;
    }
    const std::optional<std::uint64_t> maximum_retained_bytes = CheckedMultiply(
        submitted_schedules.size(), config.limits.candidate_store.maximum_candidate_bytes_per_net);
    if (!maximum_retained_bytes.has_value()) {
      return Error(CpuCandidatePoolPreparationErrorCode::kArithmeticOverflow,
                   "allocator.cpu_pool.retained_bytes_overflow.v1",
                   "Preflight retained candidate bytes overflowed uint64");
    }
    if (*maximum_retained_bytes > config.limits.maximum_retained_candidate_bytes) {
      CpuCandidatePoolPreparationError error =
          Error(CpuCandidatePoolPreparationErrorCode::kBoundExhausted,
                "allocator.cpu_pool.retained_bytes_budget.v1",
                "Preflight retained candidate capacity exceeds the configured aggregate bound");
      error.expected_value = config.limits.maximum_retained_candidate_bytes;
      error.actual_value = *maximum_retained_bytes;
      return error;
    }
    if (total_queries > config.limits.candidate_store.maximum_admission_items_per_transaction ||
        total_queries > config.limits.candidate_store.maximum_rejection_items_per_transaction ||
        total_queries >
            config.limits.candidate_store.maximum_admission_work_units_per_transaction) {
      return Error(CpuCandidatePoolPreparationErrorCode::kBoundExhausted,
                   "allocator.cpu_pool.store_transaction_limits.v1",
                   "CandidateStore transaction limits cannot contain every scheduled query");
    }

    std::vector<NetPlan> plans;
    plans.reserve(ordered_schedules.size());
    for (const CpuCandidatePoolNetSchedule* schedule : ordered_schedules) {
      routing::CandidatePolicyBatchResult policy_result =
          routing::BuildDeterministicAlternativePolicies(*schedule->compiled_board,
                                                         schedule->request.candidate_policy,
                                                         schedule->candidate_schedule);
      if (auto* policy_error = std::get_if<routing::CandidatePolicyError>(&policy_result);
          policy_error != nullptr) {
        CpuCandidatePoolPreparationError error = Error(
            CpuCandidatePoolPreparationErrorCode::kInvalidConfiguration,
            "allocator.cpu_pool.policy_schedule.v1",
            "A deterministic per-net candidate policy schedule is invalid", schedule->request.net);
        error.policy_error_code = policy_error->code;
        return error;
      }
      plans.push_back(NetPlan{
          .submitted = schedule,
          .policies = std::get<std::vector<routing::NormalizedCandidateGenerationPolicy>>(
              std::move(policy_result)),
      });
    }

    const std::uint64_t batch_identity = BatchIdentity(board, plans);
    std::vector<QuerySlot> slots;
    slots.reserve(static_cast<std::size_t>(total_queries));
    std::set<std::uint64_t> query_identities;
    for (std::size_t net_index = 0; net_index < plans.size(); ++net_index) {
      NetPlan& plan = plans[net_index];
      for (routing::NormalizedCandidateGenerationPolicy& normalized : plan.policies) {
        const std::uint64_t query_identity =
            QueryIdentity(batch_identity, plan.submitted->request.net, normalized);
        if (!query_identities.insert(query_identity).second) {
          return Error(CpuCandidatePoolPreparationErrorCode::kInternalInvariant,
                       "allocator.cpu_pool.query_identity_collision.v1",
                       "Canonical candidate queries produced a duplicate query identity",
                       plan.submitted->request.net);
        }
        routing::PlanarRouteRequest request = plan.submitted->request;
        request.candidate_policy = normalized.policy;
        slots.push_back(QuerySlot{
            .net_index = net_index,
            .net = plan.submitted->request.net,
            .compiled_board = plan.submitted->compiled_board,
            .request = std::move(request),
            .normalized_policy = std::move(normalized),
            .query_identity = query_identity,
            .draft = std::nullopt,
            .draft_origin = DraftOrigin::kReached,
            .route_failure_code = std::nullopt,
            .worker_failure = WorkerFailure::kNone,
            .work_units = 0,
        });
      }
    }

    const std::uint32_t worker_count =
        static_cast<std::uint32_t>(std::min<std::uint64_t>(config.worker_count, total_queries));
    std::atomic<std::size_t> next_slot = 0;
    std::vector<std::jthread> workers;
    workers.reserve(worker_count);
    for (std::uint32_t worker_index = 0; worker_index < worker_count; ++worker_index) {
      static_cast<void>(worker_index);
      workers.emplace_back([&board, &slots, &next_slot, config, hooks, batch_identity] {
        while (true) {
          const std::size_t index = next_slot.fetch_add(1, std::memory_order_relaxed);
          if (index >= slots.size()) {
            return;
          }
          QuerySlot& slot = slots[index];
          try {
            routing::CpuRouteResult route_result = routing::RouteWithCpuAStar(
                board, *slot.compiled_board, slot.request,
                routing::CpuRouteWorkLimits{
                    .maximum_work_units = config.limits.maximum_cpu_work_units_per_query,
                });
            if (auto* route = std::get_if<routing::CpuRoute>(&route_result); route != nullptr) {
              slot.work_units = route->telemetry.work_units;
              if (hooks.after_cpu_route != nullptr) {
                hooks.after_cpu_route(index, *route, hooks.context);
              }
              slot.draft.emplace(candidates::BuildGeneratedCandidateFromCpuRoute(
                  board, *slot.compiled_board, slot.request, slot.normalized_policy, *route,
                  candidates::CandidateSchedulingIdentity{
                      .batch_identity = batch_identity,
                      .query_identity = slot.query_identity,
                  }));
              if (std::holds_alternative<candidates::CandidateRejection>(*slot.draft)) {
                slot.draft_origin = DraftOrigin::kBuildRejected;
              }
            } else {
              const routing::RouteFailure& failure = std::get<routing::RouteFailure>(route_result);
              slot.route_failure_code = failure.code;
              if (failure.telemetry.has_value()) {
                slot.work_units = failure.telemetry->work_units;
              }
              switch (failure.code) {
                case routing::RouteFailureCode::kDisconnected:
                  slot.draft_origin = DraftOrigin::kDisconnected;
                  slot.draft.emplace(
                      RouteDiagnostic(board, slot, batch_identity, failure, slot.draft_origin));
                  break;
                case routing::RouteFailureCode::kUnsupportedLayerTransition:
                case routing::RouteFailureCode::kUnsupportedPolicy:
                  slot.draft_origin = DraftOrigin::kUnsupported;
                  slot.draft.emplace(
                      RouteDiagnostic(board, slot, batch_identity, failure, slot.draft_origin));
                  break;
                case routing::RouteFailureCode::kResourceExhausted:
                  slot.worker_failure =
                      failure.telemetry.has_value() && failure.telemetry->work_limit_exhausted
                          ? WorkerFailure::kWorkBound
                          : WorkerFailure::kResourceExhausted;
                  break;
                case routing::RouteFailureCode::kInvalidRequest:
                case routing::RouteFailureCode::kValidationFailed:
                case routing::RouteFailureCode::kInternalInvariant:
                  slot.worker_failure = WorkerFailure::kInternalInvariant;
                  break;
              }
            }
          } catch (const std::bad_alloc&) {
            slot.worker_failure = WorkerFailure::kResourceExhausted;
          } catch (const std::length_error&) {
            slot.worker_failure = WorkerFailure::kResourceExhausted;
          } catch (...) {
            slot.worker_failure = WorkerFailure::kInternalInvariant;
          }
          if (hooks.injected_worker_failure != nullptr) {
            switch (hooks.injected_worker_failure(index, hooks.context)) {
              case internal::CpuCandidatePoolInjectedWorkerFailure::kNone:
                break;
              case internal::CpuCandidatePoolInjectedWorkerFailure::kResourceExhausted:
                slot.worker_failure = WorkerFailure::kResourceExhausted;
                break;
              case internal::CpuCandidatePoolInjectedWorkerFailure::kInternalInvariant:
                slot.worker_failure = WorkerFailure::kInternalInvariant;
                break;
            }
          }
          if (hooks.before_worker_completion != nullptr) {
            hooks.before_worker_completion(index, hooks.context);
          }
        }
      });
    }
    for (std::jthread& worker : workers) {
      worker.join();
    }

    for (const QuerySlot& slot : slots) {
      if (slot.worker_failure != WorkerFailure::kNone) {
        return WorkerError(slot);
      }
      if (!slot.draft.has_value()) {
        return Error(CpuCandidatePoolPreparationErrorCode::kInternalInvariant,
                     "allocator.cpu_pool.missing_worker_result.v1",
                     "A worker completed without filling its canonical query slot", slot.net);
      }
    }

    std::uint64_t aggregate_work_units = 0;
    std::uint64_t aggregate_generated_bytes = 0;
    for (const QuerySlot& slot : slots) {
      if (!CheckedAdd(slot.work_units, aggregate_work_units)) {
        return Error(CpuCandidatePoolPreparationErrorCode::kArithmeticOverflow,
                     "allocator.cpu_pool.actual_work_overflow.v1",
                     "Actual aggregate CPU work overflowed uint64", slot.net);
      }
      const std::uint64_t bytes =
          std::holds_alternative<candidates::GeneratedRouteCandidate>(*slot.draft)
              ? std::get<candidates::GeneratedRouteCandidate>(*slot.draft).logical_bytes
              : std::get<candidates::CandidateRejection>(*slot.draft).logical_bytes;
      if (bytes > config.limits.maximum_generated_bytes_per_query) {
        CpuCandidatePoolPreparationError error =
            Error(CpuCandidatePoolPreparationErrorCode::kBoundExhausted,
                  "allocator.cpu_pool.query_generated_bytes.v1",
                  "One query result exceeds the configured generated-byte bound", slot.net);
        error.expected_value = config.limits.maximum_generated_bytes_per_query;
        error.actual_value = bytes;
        return error;
      }
      if (!CheckedAdd(bytes, aggregate_generated_bytes)) {
        return Error(CpuCandidatePoolPreparationErrorCode::kArithmeticOverflow,
                     "allocator.cpu_pool.actual_generated_bytes_overflow.v1",
                     "Actual aggregate generated bytes overflowed uint64", slot.net);
      }
    }
    if (aggregate_work_units > config.limits.maximum_aggregate_cpu_work_units ||
        aggregate_generated_bytes > config.limits.maximum_aggregate_generated_bytes) {
      return Error(CpuCandidatePoolPreparationErrorCode::kBoundExhausted,
                   "allocator.cpu_pool.actual_aggregate_budget.v1",
                   "Actual query work or generated bytes exceed the configured aggregate bound");
    }

    PreparedCpuCandidatePools prepared = PreparedCpuCandidatePoolsFactory::Make(
        plans, batch_identity, total_queries, aggregate_work_units, aggregate_generated_bytes);
    std::vector<candidates::CandidateStoreDraftItem> store_items;
    store_items.reserve(slots.size());
    std::vector<std::pair<std::uint64_t, std::size_t>> query_lookup;
    query_lookup.reserve(slots.size());
    std::vector<candidates::CandidateStoreAdmissionResult*> result_by_slot(slots.size(), nullptr);
    for (std::size_t index = 0; index < slots.size(); ++index) {
      query_lookup.emplace_back(slots[index].query_identity, index);
      store_items.push_back(candidates::CandidateStoreDraftItem{
          .compiled_board = slots[index].compiled_board,
          .request = std::move(slots[index].request),
          .draft = std::move(*slots[index].draft),
      });
    }
    std::ranges::sort(query_lookup);

    candidates::CandidateStore store(config.limits.candidate_store);
    std::vector<candidates::CandidateStoreAdmissionResult> store_results =
        store.AdmitDraftBatch(board, std::move(store_items));
    for (candidates::CandidateStoreAdmissionResult& result : store_results) {
      std::uint64_t query_identity = 0;
      if (auto* stored = std::get_if<candidates::StoredCandidate>(&result); stored != nullptr) {
        query_identity = (*stored)->data().provenance.query_identity;
      } else {
        query_identity = std::get<candidates::CandidateRejection>(result).provenance.query_identity;
      }
      if (query_identity == 0) {
        return StoreTransactionError(std::get<candidates::CandidateRejection>(result));
      }
      const auto found = std::ranges::lower_bound(query_lookup, query_identity, {},
                                                  &std::pair<std::uint64_t, std::size_t>::first);
      if (found == query_lookup.end() || found->first != query_identity ||
          result_by_slot[found->second] != nullptr) {
        return Error(CpuCandidatePoolPreparationErrorCode::kInternalInvariant,
                     "allocator.cpu_pool.store_result_identity.v1",
                     "CandidateStore returned a foreign or duplicate query result");
      }
      result_by_slot[found->second] = &result;
    }
    if (store_results.size() != slots.size() ||
        std::ranges::any_of(result_by_slot, [](const auto* result) { return result == nullptr; })) {
      return Error(CpuCandidatePoolPreparationErrorCode::kInternalInvariant,
                   "allocator.cpu_pool.store_result_count.v1",
                   "CandidateStore omitted or added a mixed-transaction result");
    }

    for (std::size_t index = 0; index < slots.size(); ++index) {
      QuerySlot& slot = slots[index];
      candidates::CandidateStoreAdmissionResult& result = *result_by_slot[index];
      CpuCandidatePoolColumn column{
          .net = slot.net,
          .policy_identity = slot.normalized_policy.identity,
          .query_identity = slot.query_identity,
          .candidate_ordinal = slot.normalized_policy.policy.candidate_ordinal,
          .outcome = CpuCandidateColumnOutcomeCode::kAdmissionRejected,
          .candidate_id = std::nullopt,
          .route_failure_code = std::nullopt,
          .candidate_rejection_code = std::nullopt,
      };
      if (auto* stored = std::get_if<candidates::StoredCandidate>(&result); stored != nullptr) {
        column.outcome = CpuCandidateColumnOutcomeCode::kAdmitted;
        column.candidate_id = (*stored)->id();
        PreparedCpuCandidatePoolsFactory::AddCandidate(prepared, slot.net_index,
                                                       std::move(*stored));
      } else {
        candidates::CandidateRejection rejection =
            std::get<candidates::CandidateRejection>(std::move(result));
        column.outcome = ColumnOutcomeFor(slot.draft_origin);
        column.route_failure_code = slot.route_failure_code;
        column.candidate_rejection_code = rejection.code;
        PreparedCpuCandidatePoolsFactory::AddDiagnostic(
            prepared, CpuCandidatePoolDiagnostic{
                          .net = slot.net,
                          .policy_identity = slot.normalized_policy.identity,
                          .query_identity = slot.query_identity,
                          .candidate_ordinal = slot.normalized_policy.policy.candidate_ordinal,
                          .outcome = column.outcome,
                          .route_failure_code = slot.route_failure_code,
                          .rejection = std::move(rejection),
                      });
      }
      PreparedCpuCandidatePoolsFactory::AddColumn(prepared, std::move(column));
    }
    if (!PreparedCpuCandidatePoolsFactory::FinalizePools(prepared)) {
      return Error(CpuCandidatePoolPreparationErrorCode::kArithmeticOverflow,
                   "allocator.cpu_pool.actual_retained_bytes_overflow.v1",
                   "Published immutable pool byte accounting overflowed uint64");
    }
    if (prepared.retained_candidate_bytes() > config.limits.maximum_retained_candidate_bytes) {
      return Error(CpuCandidatePoolPreparationErrorCode::kBoundExhausted,
                   "allocator.cpu_pool.actual_retained_bytes.v1",
                   "Published immutable pools exceed the retained-byte bound");
    }
    return prepared;
  } catch (const std::bad_alloc&) {
    return Error(CpuCandidatePoolPreparationErrorCode::kResourceExhausted,
                 "allocator.cpu_pool.allocation.v1",
                 "CPU candidate-pool scratch allocation failed");
  } catch (const std::length_error&) {
    return Error(CpuCandidatePoolPreparationErrorCode::kResourceExhausted,
                 "allocator.cpu_pool.container_capacity.v1",
                 "CPU candidate-pool container capacity was exceeded");
  } catch (const std::system_error&) {
    return Error(CpuCandidatePoolPreparationErrorCode::kResourceExhausted,
                 "allocator.cpu_pool.worker_creation.v1",
                 "CPU candidate-pool worker creation exhausted a required host resource");
  } catch (...) {
    return Error(CpuCandidatePoolPreparationErrorCode::kInternalInvariant,
                 "allocator.cpu_pool.exception.v1",
                 "CPU candidate-pool preparation raised an unexpected exception");
  }
}

}  // namespace

CpuCandidatePoolPreparationResult PrepareCpuCandidatePools(
    const board_ir::BoardSnapshot& board, std::span<const CpuCandidatePoolNetSchedule> schedules,
    CpuCandidatePoolPreparationConfig config) {
  return PrepareImpl(board, schedules, std::move(config), {});
}

CpuCandidatePoolPreparationResult internal::PrepareCpuCandidatePoolsWithTestHooks(
    const board_ir::BoardSnapshot& board, std::span<const CpuCandidatePoolNetSchedule> schedules,
    CpuCandidatePoolPreparationConfig config, CpuCandidatePoolPreparationTestHooks hooks) {
  return PrepareImpl(board, schedules, std::move(config), hooks);
}

}  // namespace apgar::allocator
