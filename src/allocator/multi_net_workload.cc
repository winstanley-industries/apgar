#include "apgar/allocator/multi_net_workload.h"

#include <algorithm>
#include <new>
#include <ranges>
#include <stdexcept>
#include <tuple>
#include <utility>

#include "apgar/board_ir/stable_hash.h"
#include "apgar/routing/candidate_policy.h"
#include "src/allocator/multi_net_workload_internal.h"

namespace apgar::allocator {
namespace {

thread_local internal::MultiNetWorkloadFaultForTesting g_workload_fault_for_testing =
    internal::MultiNetWorkloadFaultForTesting::kNone;

void MaybeThrowWorkloadFaultForTesting() {
  const internal::MultiNetWorkloadFaultForTesting fault = g_workload_fault_for_testing;
  g_workload_fault_for_testing = internal::MultiNetWorkloadFaultForTesting::kNone;
  if (fault == internal::MultiNetWorkloadFaultForTesting::kBadAlloc) {
    throw std::bad_alloc();
  }
  if (fault == internal::MultiNetWorkloadFaultForTesting::kLengthError) {
    throw std::length_error("injected multi-net workload length error");
  }
}

[[nodiscard]] MultiNetWorkloadError Error(
    MultiNetWorkloadErrorCode code, std::string_view invariant_id, std::string_view detail,
    std::optional<board_ir::EntityRef> net = std::nullopt) noexcept {
  return MultiNetWorkloadError{
      .code = code, .invariant_id = invariant_id, .detail = detail, .net = net};
}

void AddWorkloadChecksumHeader(board_ir::StableHashBuilder& hash, std::uint32_t schema_version,
                               std::uint64_t board_content_hash,
                               std::uint64_t compiler_profile_fingerprint,
                               std::uint32_t compiler_version, std::uint64_t net_count) noexcept {
  hash.AddString("APGAR-MULTI-NET-WORKLOAD-V1");
  hash.AddU32(schema_version);
  hash.AddU64(board_content_hash);
  hash.AddU64(compiler_profile_fingerprint);
  hash.AddU32(compiler_version);
  hash.AddU64(net_count);
}

void AddWorkloadChecksumRecord(board_ir::StableHashBuilder& hash,
                               const internal::MultiNetWorkloadChecksumRecordV1& context) noexcept {
  hash.AddU64(context.net.id);
  hash.AddU32(context.net.generation);
  hash.AddU64(context.routing_profile_fingerprint);
  hash.AddU64(context.rule_bucket_identity);
  hash.AddI64(context.start.x);
  hash.AddI64(context.start.y);
  hash.AddI64(context.goal.x);
  hash.AddI64(context.goal.y);
  hash.AddU32(context.start_layer);
  hash.AddU32(context.goal_layer);
}

[[nodiscard]] std::uint64_t ComputeWorkloadChecksumForContexts(
    std::uint32_t schema_version, std::uint64_t board_content_hash,
    std::uint64_t compiler_profile_fingerprint, std::uint32_t compiler_version,
    std::span<const PreparedNetRoutingContext> nets) noexcept {
  board_ir::StableHashBuilder hash;
  AddWorkloadChecksumHeader(hash, schema_version, board_content_hash, compiler_profile_fingerprint,
                            compiler_version, static_cast<std::uint64_t>(nets.size()));
  for (const PreparedNetRoutingContext& context : nets) {
    AddWorkloadChecksumRecord(
        hash, internal::MultiNetWorkloadChecksumRecordV1{
                  .net = context.request.net,
                  .routing_profile_fingerprint = context.routing_profile_fingerprint,
                  .rule_bucket_identity = context.compiled_board.rule_bucket().identity,
                  .start = context.request.start,
                  .goal = context.request.goal,
                  .start_layer = context.request.start_layer,
                  .goal_layer = context.request.goal_layer,
              });
  }
  return hash.Finish();
}

}  // namespace

void internal::SetMultiNetWorkloadFaultForTesting(MultiNetWorkloadFaultForTesting fault) noexcept {
  g_workload_fault_for_testing = fault;
}

std::uint64_t internal::ComputeMultiNetWorkloadChecksumV1(
    std::uint32_t schema_version, std::uint64_t board_content_hash,
    std::uint64_t compiler_profile_fingerprint, std::uint32_t compiler_version,
    std::span<const MultiNetWorkloadChecksumRecordV1> nets) noexcept {
  board_ir::StableHashBuilder hash;
  AddWorkloadChecksumHeader(hash, schema_version, board_content_hash, compiler_profile_fingerprint,
                            compiler_version, static_cast<std::uint64_t>(nets.size()));
  for (const MultiNetWorkloadChecksumRecordV1& context : nets) {
    AddWorkloadChecksumRecord(hash, context);
  }
  return hash.Finish();
}

const PreparedNetRoutingContext* MultiNetWorkload::FindNet(board_ir::EntityRef net) const noexcept {
  const std::pair key{net.id, net.generation};
  const auto found = std::ranges::lower_bound(nets_, key, {}, [](const auto& context) {
    return std::pair{context.request.net.id, context.request.net.generation};
  });
  return found != nets_.end() && found->request.net == net ? &*found : nullptr;
}

MultiNetWorkloadResult BuildMultiNetWorkload(
    std::uint32_t schema_version, const board_ir::BoardSnapshot& board,
    const geometry_compiler::CompilerProfile& compiler_profile,
    std::span<const MultiNetRoutingSpec> specs, std::uint64_t maximum_nets) {
  return BuildMultiNetWorkload(
      schema_version, board, compiler_profile, specs,
      MultiNetWorkloadLimits{
          .maximum_nets = maximum_nets,
          .maximum_compiled_nodes = kMaximumMultiNetWorkloadCompiledNodesV1,
          .maximum_compiled_host_bytes = kMaximumMultiNetWorkloadCompiledHostBytesV1});
}

MultiNetWorkloadResult BuildMultiNetWorkload(
    std::uint32_t schema_version, const board_ir::BoardSnapshot& board,
    const geometry_compiler::CompilerProfile& compiler_profile,
    std::span<const MultiNetRoutingSpec> specs, const MultiNetWorkloadLimits& limits) {
  if (schema_version != kMultiNetWorkloadSchemaVersion) {
    return Error(MultiNetWorkloadErrorCode::kUnsupportedSchema, "allocator.workload.schema.v1",
                 "Multi-net workload schema is unsupported");
  }
  if (limits.maximum_nets == 0 || limits.maximum_nets > kMaximumMultiNetWorkloadNetsV1 ||
      limits.maximum_compiled_nodes == 0 ||
      limits.maximum_compiled_nodes > kMaximumMultiNetWorkloadCompiledNodesV1 ||
      limits.maximum_compiled_host_bytes == 0 ||
      limits.maximum_compiled_host_bytes > kMaximumMultiNetWorkloadCompiledHostBytesV1) {
    return Error(MultiNetWorkloadErrorCode::kInvalidConfiguration,
                 "allocator.workload.configuration.v1",
                 "Multi-net workload maximum must be positive and within the schema-v1 bound");
  }
  if (specs.empty() || specs.size() > limits.maximum_nets ||
      specs.size() > kMaximumMultiNetWorkloadNetsV1) {
    return Error(MultiNetWorkloadErrorCode::kInputBoundExceeded, "allocator.workload.net_count.v1",
                 "Multi-net workload requires a nonempty bounded net roster");
  }

  try {
    MaybeThrowWorkloadFaultForTesting();
    std::vector<const MultiNetRoutingSpec*> ordered_specs;
    ordered_specs.reserve(specs.size());
    for (const MultiNetRoutingSpec& spec : specs) {
      ordered_specs.push_back(&spec);
    }
    std::ranges::sort(
        ordered_specs, [](const MultiNetRoutingSpec* left, const MultiNetRoutingSpec* right) {
          return std::tie(left->routing_profile.net.id, left->routing_profile.net.generation) <
                 std::tie(right->routing_profile.net.id, right->routing_profile.net.generation);
        });
    if (std::ranges::adjacent_find(
            ordered_specs, [](const MultiNetRoutingSpec* left, const MultiNetRoutingSpec* right) {
              return left->routing_profile.net == right->routing_profile.net;
            }) != ordered_specs.end()) {
      return Error(MultiNetWorkloadErrorCode::kDuplicateNet, "allocator.workload.duplicate_net.v1",
                   "Multi-net workload contains more than one context for a net");
    }

    std::vector<PreparedNetRoutingContext> contexts;
    contexts.reserve(specs.size());
    __uint128_t compiled_node_count = 0;
    __uint128_t compiled_host_bytes = 0;
    for (const MultiNetRoutingSpec* ordered_spec : ordered_specs) {
      const MultiNetRoutingSpec& spec = *ordered_spec;
      board_ir::RoutingProfilePreparationResult prepared =
          board_ir::PrepareRoutingProfile(board, spec.routing_profile);
      if (const auto* profile_error = std::get_if<board_ir::BoardValidationError>(&prepared);
          profile_error != nullptr) {
        return Error(MultiNetWorkloadErrorCode::kInvalidRoutingProfile,
                     "allocator.workload.routing_profile.v1",
                     "A workload routing profile is invalid for the supplied Board snapshot",
                     spec.routing_profile.net);
      }
      board_ir::RoutingProfile routing_profile =
          std::get<board_ir::RoutingProfile>(std::move(prepared));
      geometry_compiler::CompileResult compiled_result =
          geometry_compiler::CompileBoard(board, compiler_profile, routing_profile);
      if (const auto* compile_error =
              std::get_if<geometry_compiler::CompileError>(&compiled_result);
          compile_error != nullptr) {
        return Error(MultiNetWorkloadErrorCode::kCompileFailure, "allocator.workload.compile.v1",
                     "A workload routing profile could not be compiled", routing_profile.net);
      }
      geometry_compiler::CompiledBoard compiled =
          std::get<geometry_compiler::CompiledBoard>(std::move(compiled_result));
      compiled_node_count += compiled.telemetry().represented_nodes;
      compiled_host_bytes += compiled.telemetry().estimated_host_bytes;
      if (compiled_node_count > limits.maximum_compiled_nodes ||
          compiled_host_bytes > limits.maximum_compiled_host_bytes) {
        return Error(MultiNetWorkloadErrorCode::kWorkBoundExceeded,
                     "allocator.workload.compiled_work.v1",
                     "Prepared workload exceeds its cumulative compiled-node or host-byte bound",
                     routing_profile.net);
      }
      routing::TwoTerminalRequestResult request_result = routing::BuildTwoTerminalRouteRequest(
          board, routing_profile, spec.start_layer, spec.goal_layer);
      if (!std::holds_alternative<routing::PlanarRouteRequest>(request_result)) {
        return Error(
            MultiNetWorkloadErrorCode::kInvalidRouteRequest, "allocator.workload.route_request.v1",
            "Workload net cannot produce an exact two-terminal route request", routing_profile.net);
      }
      routing::PlanarRouteRequest request =
          std::get<routing::PlanarRouteRequest>(std::move(request_result));
      if (routing::ValidateCompiledBoardAssociation(board, compiled).has_value() ||
          routing::ValidateTwoTerminalRouteRequest(board, compiled, request).has_value()) {
        return Error(MultiNetWorkloadErrorCode::kAssociationMismatch,
                     "allocator.workload.context_association.v1",
                     "Prepared workload context failed compiled-board or request association",
                     routing_profile.net);
      }
      contexts.push_back(PreparedNetRoutingContext{
          .routing_profile = std::move(routing_profile),
          .compiled_board = std::move(compiled),
          .request = std::move(request),
          .routing_profile_fingerprint = 0,
      });
      contexts.back().routing_profile_fingerprint =
          routing::FingerprintRoutingProfile(contexts.back().routing_profile);
    }

    const geometry_compiler::CompiledBoard& first = contexts.front().compiled_board;
    for (const PreparedNetRoutingContext& context : contexts) {
      if (context.compiled_board.source_board_content_hash() != board.content_hash() ||
          context.compiled_board.compiler_profile_fingerprint() !=
              first.compiler_profile_fingerprint() ||
          context.compiled_board.compiler_version() != first.compiler_version() ||
          context.request.net != context.routing_profile.net) {
        return Error(
            MultiNetWorkloadErrorCode::kAssociationMismatch, "allocator.workload.common_context.v1",
            "Workload contexts do not share one board/compiler association", context.request.net);
      }
    }
    const std::uint64_t checksum = ComputeWorkloadChecksumForContexts(
        schema_version, board.content_hash(), first.compiler_profile_fingerprint(),
        first.compiler_version(), contexts);
    const std::uint64_t retained_compiler_fingerprint = first.compiler_profile_fingerprint();
    const std::uint32_t retained_compiler_version = first.compiler_version();
    geometry_compiler::CompilerProfile retained_compiler_profile = first.profile();
    return MultiNetWorkload(schema_version, board.content_hash(), retained_compiler_fingerprint,
                            retained_compiler_version, std::move(retained_compiler_profile),
                            std::move(contexts), checksum,
                            static_cast<std::uint64_t>(compiled_node_count),
                            static_cast<std::uint64_t>(compiled_host_bytes));
  } catch (const std::bad_alloc&) {
    return Error(MultiNetWorkloadErrorCode::kResourceExhausted,
                 "allocator.workload.host_memory_exhausted.v1",
                 "Host allocation failed while preparing the multi-net workload");
  } catch (const std::length_error&) {
    return Error(MultiNetWorkloadErrorCode::kResourceExhausted,
                 "allocator.workload.host_container_exhausted.v1",
                 "Host container limits were exhausted while preparing the multi-net workload");
  }
}

}  // namespace apgar::allocator
