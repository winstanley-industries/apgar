#include "apgar/candidates/gpu_candidate_adapter.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <ranges>
#include <string>
#include <utility>
#include <variant>

#include "src/candidates/route_candidate_internal.h"

namespace apgar::candidates {

std::optional<CandidateGeneratorKind> CandidateGeneratorForPlanarGenerator(
    gpu::PlanarGenerator generator) noexcept {
  switch (generator) {
    case gpu::PlanarGenerator::kBucketedFrontier:
      return CandidateGeneratorKind::kCudaFrontier;
    case gpu::PlanarGenerator::kHeadingAwareSweep:
      return CandidateGeneratorKind::kCudaSweep;
  }
  return std::nullopt;
}

std::optional<std::string> CudaCandidateDeviceClass(const gpu::BackendMetadata& metadata) {
  if (metadata.backend != "cuda" || metadata.device_name.empty() || metadata.device_uuid.empty() ||
      metadata.compute_capability_major == 0 || metadata.runtime_version == 0 ||
      metadata.driver_version == 0 || metadata.global_memory_bytes == 0) {
    return std::nullopt;
  }
  return std::string(kCudaDeviceClassPrefixV1) + std::to_string(metadata.compute_capability_major) +
         "." + std::to_string(metadata.compute_capability_minor);
}

namespace {

enum class BatchItemMembership : std::uint8_t {
  kScanBatch,
  kUnique,
  kNotUnique,
};

CandidateDraftBuildResult BuildGeneratedCandidateFromGpuBatchItemImpl(
    const board_ir::BoardSnapshot& board, const geometry_compiler::CompiledBoard& compiled_board,
    const gpu::PlanarCandidateBatchQuery& query,
    const routing::NormalizedCandidateGenerationPolicy& normalized_policy,
    const gpu::PlanarCandidateBatch& batch, const gpu::PlanarCandidateBatchItem& item,
    BatchItemMembership membership) {
  const gpu::PlanarGpuRoute* route = std::get_if<gpu::PlanarGpuRoute>(&item.result());
  const gpu::PlanarGenerator claimed_generator =
      route == nullptr ? batch.generator : route->generator;
  const std::optional<CandidateGeneratorKind> candidate_generator =
      CandidateGeneratorForPlanarGenerator(claimed_generator);
  const std::optional<std::string> device_class =
      CudaCandidateDeviceClass(route == nullptr ? batch.backend : route->backend);
  const CandidateAssociations context_associations = AssociationsFor(board, compiled_board);
  const auto reject_without_bound_provenance = [&](std::string detail,
                                                   std::optional<std::uint64_t> actual_value =
                                                       std::nullopt) -> CandidateDraftBuildResult {
    return CanonicalizeCandidateRejectionV1(CandidateRejection{
        .candidate_id = std::nullopt,
        .net = query.request.net,
        .stage = CandidateLifecycleStage::kGenerated,
        .code = CandidateRejectionCode::kInvalidInput,
        .invariant_id = "candidate.builder.gpu_batch_envelope.v1",
        .associations = context_associations,
        .policy_identity = normalized_policy.identity,
        // Version zero plus an otherwise zeroed record explicitly means that
        // typed generator/backend provenance could not be represented.
        .provenance = {},
        .primitive_witness_index = std::nullopt,
        .resource_witness_index = std::nullopt,
        .expected_value = std::nullopt,
        .actual_value = actual_value,
        .conflicting_entity = std::nullopt,
        .candidate_payload_checksum = std::nullopt,
        .detail = std::move(detail),
        .logical_bytes = 0,
    });
  };
  if (!candidate_generator.has_value()) {
    return reject_without_bound_provenance(
        "GPU batch claims an unknown generator with no Candidate Provenance v1 mapping",
        static_cast<std::uint8_t>(claimed_generator));
  }
  if (!device_class.has_value()) {
    return reject_without_bound_provenance(
        "GPU batch contains incomplete or non-CUDA device metadata");
  }
  const CandidateProvenance provenance{
      .generator = *candidate_generator,
      .generator_version = 1,
      .backend = CandidateBackendKind::kCuda,
      .supported_device_class = *device_class,
      .deterministic_seed = normalized_policy.policy.deterministic_seed,
      .batch_identity =
          item.has_validated_route_evidence() ? item.validated_batch_id() : batch.batch_id,
      .query_identity = item.query_id(),
      .candidate_ordinal = normalized_policy.policy.candidate_ordinal,
  };
  const auto reject = [&](const CandidateAssociations& claimed_associations,
                          CandidateRejectionCode code, std::string invariant,
                          std::string detail) -> CandidateDraftBuildResult {
    return internal::RejectGeneratedCandidateDraft(
        board, compiled_board, query.request, normalized_policy, provenance, claimed_associations,
        CandidateLifecycleStage::kGenerated, code, std::move(invariant), std::move(detail));
  };
  if (!routing::CandidateGenerationPolicyShapeIsWithinV1Bounds(normalized_policy.policy)) {
    return reject(context_associations, CandidateRejectionCode::kInvalidInput,
                  "candidate.policy.resource_entry_count.v1",
                  "Candidate policy exceeds the schema-v1 resource-entry bound");
  }
  if (batch.schema_version != gpu::kDeviceCandidateBatchSchemaVersion || batch.batch_id == 0 ||
      query.query_id == 0) {
    return reject(context_associations, CandidateRejectionCode::kInvalidInput,
                  "candidate.builder.gpu_batch_envelope.v1",
                  "Validated GPU batch schema, identity, generator, or CUDA device metadata is "
                  "incomplete");
  }
  const bool uniquely_present = membership == BatchItemMembership::kUnique ||
                                (membership == BatchItemMembership::kScanBatch &&
                                 std::ranges::count(batch.items, item.query_id(),
                                                    &gpu::PlanarCandidateBatchItem::query_id) == 1);
  if (!uniquely_present) {
    return reject(context_associations, CandidateRejectionCode::kAssociationMismatch,
                  "candidate.builder.gpu_batch_item_membership.v1",
                  "Validated GPU item query identity is not unique in the supplied batch");
  }
  if (item.query_id() != query.query_id || item.input_ordinal() != query.input_ordinal ||
      item.policy_identity() != normalized_policy.identity) {
    return reject(context_associations, CandidateRejectionCode::kAssociationMismatch,
                  "candidate.builder.gpu_query_attribution.v1",
                  "Validated GPU batch item does not belong to the supplied query and policy");
  }
  if (route == nullptr) {
    return reject(context_associations, CandidateRejectionCode::kInvalidInput,
                  "candidate.builder.gpu_route_result.v1",
                  "A failed GPU query result cannot be converted into a route candidate");
  }
  CandidateAssociations route_associations = context_associations;
  route_associations.board_content_hash = route->source_board_content_hash;
  route_associations.compiler_profile_fingerprint = route->compiler_profile_fingerprint;
  route_associations.geometry_compiler_version = route->compiler_version;
  route_associations.rule_bucket_identity = route->rule_bucket_identity;
  if (!item.has_validated_route_evidence() ||
      item.validated_batch_schema_version() != batch.schema_version ||
      item.validated_batch_id() != batch.batch_id || route->generator != batch.generator ||
      route->backend != batch.backend ||
      route->device_view_fingerprint != batch.device_view_fingerprint ||
      route->policy_identity != item.policy_identity()) {
    return reject(route_associations, CandidateRejectionCode::kAssociationMismatch,
                  "candidate.builder.gpu_result_envelope.v1",
                  "GPU route lacks a host-validation seal or its sealed associations do not match "
                  "the batch and query envelope");
  }
  if (!item.has_authenticated_cuda_producer_evidence()) {
    return reject(route_associations, CandidateRejectionCode::kUnsupported,
                  "candidate.builder.gpu_producer_authentication.v1",
                  "Host-validated route did not come through the concrete checksum-pinned CUDA "
                  "producer boundary");
  }
  return internal::BuildGeneratedCandidateFromValidatedPlanarRoute(
      board, compiled_board, query.request, normalized_policy, route_associations,
      route->policy_identity, route->total_cost, route->segments, provenance,
      internal::CandidateProducerAuthority::kAuthenticatedCudaBatch);
}

}  // namespace

CandidateDraftBuildResult BuildGeneratedCandidateFromGpuBatchItem(
    const board_ir::BoardSnapshot& board, const geometry_compiler::CompiledBoard& compiled_board,
    const gpu::PlanarCandidateBatchQuery& query,
    const routing::NormalizedCandidateGenerationPolicy& normalized_policy,
    const gpu::PlanarCandidateBatch& batch, const gpu::PlanarCandidateBatchItem& item) {
  return BuildGeneratedCandidateFromGpuBatchItemImpl(board, compiled_board, query,
                                                     normalized_policy, batch, item,
                                                     BatchItemMembership::kScanBatch);
}

GpuCandidateBatchBuildResult BuildGeneratedCandidatesFromGpuBatchItems(
    const board_ir::BoardSnapshot& board, const geometry_compiler::CompiledBoard& compiled_board,
    const gpu::PlanarCandidateBatch& batch,
    std::span<const GpuCandidateBatchBuildRequest> requests) {
  if (requests.size() > routing::kMaximumAlternativePolicyCount ||
      batch.items.size() > routing::kMaximumAlternativePolicyCount) {
    return GpuCandidateBatchBuildFailure{
        .code = CandidateRejectionCode::kBudgetExhausted,
        .invariant_id = "candidate.builder.gpu_batch_conversion_bound.v1",
        .detail = "GPU candidate batch conversion exceeds the bounded v1 item count",
    };
  }

  std::vector<std::uint64_t> batch_query_ids;
  batch_query_ids.reserve(batch.items.size());
  for (const gpu::PlanarCandidateBatchItem& item : batch.items) {
    batch_query_ids.push_back(item.query_id());
  }
  std::ranges::sort(batch_query_ids);

  std::vector<CandidateDraftBuildResult> results;
  results.reserve(requests.size());
  for (const GpuCandidateBatchBuildRequest& request : requests) {
    const std::uint64_t query_id = request.item.get().query_id();
    const auto matches = std::ranges::equal_range(batch_query_ids, query_id);
    const BatchItemMembership membership = std::ranges::distance(matches) == 1
                                               ? BatchItemMembership::kUnique
                                               : BatchItemMembership::kNotUnique;
    results.push_back(BuildGeneratedCandidateFromGpuBatchItemImpl(
        board, compiled_board, request.query.get(), request.normalized_policy.get(), batch,
        request.item.get(), membership));
  }
  return results;
}

}  // namespace apgar::candidates
