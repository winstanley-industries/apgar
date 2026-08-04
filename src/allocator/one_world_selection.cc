#include "apgar/allocator/one_world_selection.h"

#include <algorithm>
#include <cstddef>
#include <new>
#include <set>
#include <tuple>
#include <utility>
#include <vector>

namespace apgar::allocator {
namespace {

using UWide = __uint128_t;

[[nodiscard]] OneWorldSelectionError Error(OneWorldSelectionErrorCode code,
                                           std::string_view invariant_id,
                                           std::string_view detail) noexcept {
  return OneWorldSelectionError{
      .code = code,
      .invariant_id = invariant_id,
      .detail = detail,
      .accounting_error_code = std::nullopt,
  };
}

[[nodiscard]] auto NetKey(board_ir::EntityRef net) noexcept {
  return std::pair{net.id, net.generation};
}

[[nodiscard]] bool LimitsAreValid(const OneWorldSelectionLimits& limits) noexcept {
  return limits.maximum_net_pools > 0 && limits.maximum_net_pools <= kMaximumOneWorldNetPools &&
         limits.maximum_total_candidates > 0 &&
         limits.maximum_total_candidates <= kMaximumOneWorldPoolCandidates;
}

[[nodiscard]] bool CandidateMatchesCapacityModel(const candidates::RouteCandidate& candidate,
                                                 const ResourceCapacityModel& capacities) noexcept {
  const candidates::CandidateAssociations& candidate_associations = candidate.data().associations;
  const ResourceLatticeAssociations& model_associations = capacities.associations();
  return candidate_associations.board_content_hash == model_associations.board_content_hash &&
         candidate_associations.compiler_profile_fingerprint ==
             model_associations.compiler_profile_fingerprint &&
         candidate_associations.geometry_compiler_version ==
             model_associations.geometry_compiler_version;
}

[[nodiscard]] bool CandidateIdentityIsValid(const candidates::RouteCandidate& candidate) noexcept {
  const candidates::GeneratedRouteCandidate& data = candidate.data();
  return !candidate.id().empty() && data.schema_major == candidates::kRouteCandidateSchemaMajor &&
         data.schema_minor == candidates::kRouteCandidateSchemaMinor &&
         candidate.id() == candidates::DeriveCandidateId(candidate.net(), data.associations,
                                                         data.policy_identity, data.provenance);
}

[[nodiscard]] bool CandidateRanksBefore(const candidates::RouteCandidate* left,
                                        const candidates::RouteCandidate* right) noexcept {
  const candidates::CandidateMetrics& left_metrics = left->data().metrics;
  const candidates::CandidateMetrics& right_metrics = right->data().metrics;
  const UWide left_steps =
      static_cast<UWide>(left_metrics.orthogonal_step_count) + left_metrics.diagonal_step_count;
  const UWide right_steps =
      static_cast<UWide>(right_metrics.orthogonal_step_count) + right_metrics.diagonal_step_count;
  return std::tuple{left_metrics.intrinsic_base_cost,
                    left_metrics.via_count,
                    left_metrics.bend_count,
                    left_steps,
                    left_metrics.axis_aligned_length_dbu,
                    left_metrics.diagonal_projection_dbu,
                    left->id()} < std::tuple{right_metrics.intrinsic_base_cost,
                                             right_metrics.via_count,
                                             right_metrics.bend_count,
                                             right_steps,
                                             right_metrics.axis_aligned_length_dbu,
                                             right_metrics.diagonal_projection_dbu,
                                             right->id()};
}

}  // namespace

OneWorldSelectionResult SelectOneWorldZeroPrice(
    const board_ir::BoardSnapshot& board, const ResourceCapacityModel& capacities,
    std::span<const OneWorldCandidatePool> submitted_pools, OneWorldSelectionLimits limits) {
  if (!LimitsAreValid(limits)) {
    return Error(OneWorldSelectionErrorCode::kInvalidLimits, "allocator.one_world.limits.v1",
                 "One-World selection limits must be positive and within hard bounds");
  }
  if (board.content_hash() != capacities.associations().board_content_hash) {
    return Error(OneWorldSelectionErrorCode::kAssociationMismatch,
                 "allocator.one_world.board_association.v1",
                 "Board IR does not match the resource-capacity model");
  }
  if (submitted_pools.size() > limits.maximum_net_pools) {
    return Error(OneWorldSelectionErrorCode::kInputBoundExceeded,
                 "allocator.one_world.net_pool_count.v1",
                 "Explicit net pools exceed the configured pool bound");
  }

  try {
    std::vector<const OneWorldCandidatePool*> ordered_pools;
    ordered_pools.reserve(submitted_pools.size());
    for (const OneWorldCandidatePool& pool : submitted_pools) {
      ordered_pools.push_back(&pool);
    }
    std::ranges::sort(ordered_pools,
                      [](const OneWorldCandidatePool* left, const OneWorldCandidatePool* right) {
                        return NetKey(left->net) < NetKey(right->net);
                      });

    UWide total_candidates = 0;
    const board_ir::EntityRef* previous_net = nullptr;
    for (const OneWorldCandidatePool* pool : ordered_pools) {
      if (previous_net != nullptr && *previous_net == pool->net) {
        return Error(OneWorldSelectionErrorCode::kDuplicateNetPool,
                     "allocator.one_world.duplicate_net_pool.v1",
                     "Each explicitly selected net must have exactly one candidate pool");
      }
      previous_net = &pool->net;
      if (board.FindNet(pool->net) == nullptr) {
        return Error(OneWorldSelectionErrorCode::kUnknownNet, "allocator.one_world.unknown_net.v1",
                     "Candidate pool names a net absent from the Board IR snapshot");
      }
      total_candidates += pool->candidates.size();
      if (total_candidates > limits.maximum_total_candidates) {
        return Error(OneWorldSelectionErrorCode::kInputBoundExceeded,
                     "allocator.one_world.total_candidate_count.v1",
                     "Explicit candidate pools exceed the configured candidate bound");
      }
    }

    OneWorldSelection selection{
        .associations = capacities.associations(),
        .nets = {},
        .accounting = {},
        .input_candidate_count = static_cast<std::uint64_t>(total_candidates),
    };
    selection.nets.reserve(ordered_pools.size());
    std::vector<const candidates::RouteCandidate*> selected_candidates;
    selected_candidates.reserve(ordered_pools.size());
    std::set<candidates::CandidateId> candidate_ids;

    for (const OneWorldCandidatePool* pool : ordered_pools) {
      if (pool->candidates.empty()) {
        selection.nets.emplace_back(OneWorldCandidateAbsence{
            .net = pool->net,
            .reason = OneWorldCandidateAbsenceReason::kEmptyPool,
        });
        continue;
      }

      std::vector<const candidates::RouteCandidate*> ordered_candidates;
      ordered_candidates.reserve(pool->candidates.size());
      for (const candidates::RouteCandidate* candidate : pool->candidates) {
        if (candidate == nullptr) {
          return Error(OneWorldSelectionErrorCode::kNullCandidate,
                       "allocator.one_world.null_candidate.v1",
                       "Explicit candidate pool contains a null candidate");
        }
        ordered_candidates.push_back(candidate);
      }
      std::ranges::sort(ordered_candidates, [](const candidates::RouteCandidate* left,
                                               const candidates::RouteCandidate* right) {
        return left->id() < right->id();
      });

      for (const candidates::RouteCandidate* candidate : ordered_candidates) {
        if (candidate->net() != pool->net) {
          return Error(OneWorldSelectionErrorCode::kCandidateNetMismatch,
                       "allocator.one_world.candidate_net.v1",
                       "Candidate identity does not belong to its explicit net pool");
        }
        if (!CandidateIdentityIsValid(*candidate)) {
          return Error(OneWorldSelectionErrorCode::kInvalidCandidateIdentity,
                       "allocator.one_world.candidate_identity.v1",
                       "Candidate ID does not match its immutable identity fields");
        }
        if (!candidate_ids.insert(candidate->id()).second) {
          return Error(OneWorldSelectionErrorCode::kDuplicateCandidateIdentity,
                       "allocator.one_world.duplicate_candidate_identity.v1",
                       "Candidate identity appears more than once across explicit pools");
        }
        if (!CandidateMatchesCapacityModel(*candidate, capacities)) {
          return Error(OneWorldSelectionErrorCode::kCandidateAssociationMismatch,
                       "allocator.one_world.candidate_association.v1",
                       "Candidate Board IR or compiler lattice does not match the capacity model");
        }
      }

      const candidates::RouteCandidate* selected =
          *std::ranges::min_element(ordered_candidates, CandidateRanksBefore);
      selected_candidates.push_back(selected);
      selection.nets.emplace_back(OneWorldSelectedCandidate{
          .net = pool->net,
          .candidate_id = selected->id(),
      });
    }

    ResourceAccountingResult accounting =
        AccumulateResourceUsage(capacities, selected_candidates, limits.accounting);
    if (std::holds_alternative<ResourceAccountingError>(accounting)) {
      const ResourceAccountingError& accounting_error =
          std::get<ResourceAccountingError>(accounting);
      return OneWorldSelectionError{
          .code = OneWorldSelectionErrorCode::kAccountingFailure,
          .invariant_id = accounting_error.invariant_id,
          .detail = accounting_error.detail,
          .accounting_error_code = accounting_error.code,
      };
    }
    selection.accounting = std::get<ResourceAccounting>(std::move(accounting));
    return selection;
  } catch (const std::bad_alloc&) {
    return Error(OneWorldSelectionErrorCode::kResourceExhausted,
                 "allocator.one_world.allocation.v1",
                 "One-World selection scratch allocation failed");
  }
}

}  // namespace apgar::allocator
