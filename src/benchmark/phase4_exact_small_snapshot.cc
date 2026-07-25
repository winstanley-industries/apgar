#include "apgar/benchmark/phase4_exact_small_snapshot.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <exception>
#include <limits>
#include <map>
#include <new>
#include <ranges>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <variant>

#include "apgar/board_ir/stable_hash.h"
#include "src/allocator/negotiated_prices_internal.h"
#include "src/benchmark/phase4_paired_trial_internal.h"
#include "src/candidates/route_candidate_internal.h"

namespace apgar::benchmark {
namespace {

using Wide = unsigned __int128;
using SignedWide = __int128;

[[nodiscard]] bool FitsI64(SignedWide value) noexcept {
  return value >= std::numeric_limits<std::int64_t>::min() &&
         value <= std::numeric_limits<std::int64_t>::max();
}

[[nodiscard]] Phase4ExactSmallSnapshotError Error(Phase4ExactSmallSnapshotErrorCode code,
                                                  std::string_view invariant,
                                                  std::string_view detail,
                                                  std::uint64_t required = 0,
                                                  std::uint64_t configured = 0) {
  return Phase4ExactSmallSnapshotError{.code = code,
                                       .invariant_id = std::string(invariant),
                                       .detail = std::string(detail),
                                       .required = required,
                                       .configured = configured};
}

[[nodiscard]] bool IsLowerHexCommit(std::string_view commit) noexcept {
  return commit.size() == 40 && std::ranges::all_of(commit, [](char character) {
           return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f');
         });
}

[[nodiscard]] bool EntityBefore(board_ir::EntityRef left, board_ir::EntityRef right) noexcept {
  return std::tie(left.id, left.generation) < std::tie(right.id, right.generation);
}

void HashEntity(board_ir::StableHashBuilder* hash, board_ir::EntityRef entity) noexcept {
  hash->AddU64(entity.id);
  hash->AddU32(entity.generation);
}

void HashId(board_ir::StableHashBuilder* hash, candidates::Hash128 id) noexcept {
  hash->AddU64(id.high);
  hash->AddU64(id.low);
}

void HashResource(board_ir::StableHashBuilder* hash,
                  const routing::EdgeResourceKey& resource) noexcept {
  hash->AddU32(resource.layer);
  hash->AddI64(resource.lattice_x);
  hash->AddI64(resource.lattice_y);
  hash->AddByte(static_cast<std::uint8_t>(resource.direction));
}

void HashCell(board_ir::StableHashBuilder* hash, const Phase4CanonicalCellConfig& cell) noexcept {
  hash->AddU32(cell.schema_version);
  hash->AddU32(cell.case_id);
  hash->AddU32(cell.requested_pool_size);
  hash->AddU32(cell.preparation_worker_count);
  hash->AddU32(cell.repetitions);
  hash->AddU64(cell.maximum_setup_elapsed_nanoseconds);
  hash->AddU64(cell.external_budget.maximum_prepared_elapsed_nanoseconds);
  hash->AddU64(cell.external_budget.maximum_cold_elapsed_nanoseconds);
  hash->AddU64(cell.external_budget.maximum_address_space_bytes);
  hash->AddU64(cell.external_budget.maximum_peak_host_bytes);
  hash->AddU64(cell.corpus_limits.maximum_nets);
  hash->AddU64(cell.corpus_limits.maximum_compiled_nodes);
  hash->AddU64(cell.corpus_limits.maximum_compiled_host_bytes);
  hash->AddU64(cell.corpus_limits.maximum_active_regions);
  hash->AddU64(cell.corpus_limits.maximum_board_entities);
}

void HashRawReference(board_ir::StableHashBuilder* hash,
                      const Phase4PerNetReportRawReferenceV1& reference) noexcept {
  hash->AddU32(reference.repetition_index);
  hash->AddByte(static_cast<std::uint8_t>(reference.execution_order));
  hash->AddU64(reference.pair_attempt_checksum);
  hash->AddU64(reference.paired_semantic_checksum);
  hash->AddU64(reference.paired_artifact_checksum);
  hash->AddU64(reference.baseline_semantic_checksum);
  hash->AddU64(reference.baseline_arm_artifact_checksum);
  hash->AddU64(reference.candidate_semantic_checksum);
  hash->AddU64(reference.candidate_arm_artifact_checksum);
}

void HashMetrics(board_ir::StableHashBuilder* hash,
                 const candidates::CandidateMetrics& metrics) noexcept {
  hash->AddU64(metrics.scalar_policy_cost);
  hash->AddU64(metrics.intrinsic_base_cost);
  hash->AddU64(metrics.orthogonal_step_count);
  hash->AddU64(metrics.diagonal_step_count);
  hash->AddU64(metrics.bend_count);
  hash->AddU64(metrics.line_primitive_count);
  hash->AddU64(metrics.via_count);
  hash->AddU64(metrics.axis_aligned_length_dbu);
  hash->AddU64(metrics.diagonal_projection_dbu);
}

void HashPolicy(board_ir::StableHashBuilder* hash,
                const routing::CandidateGenerationPolicy& policy) noexcept {
  hash->AddU32(policy.schema_version);
  hash->AddByte(static_cast<std::uint8_t>(policy.objective));
  hash->AddU64(policy.deterministic_seed);
  hash->AddU32(policy.candidate_ordinal);
  hash->AddU64(policy.orthogonal_step_surcharge);
  hash->AddU64(policy.diagonal_step_surcharge);
  hash->AddU64(policy.bend_surcharge);
  hash->AddU64(policy.banned_resources.size());
  for (const auto& resource : policy.banned_resources) HashResource(hash, resource);
  hash->AddU64(policy.resource_penalties.size());
  for (const auto& penalty : policy.resource_penalties) {
    HashResource(hash, penalty.resource);
    hash->AddU64(penalty.additional_cost);
  }
}

void HashPrimitive(board_ir::StableHashBuilder* hash,
                   const candidates::CandidatePrimitive& primitive) noexcept {
  hash->AddByte(static_cast<std::uint8_t>(primitive.index()));
  if (const auto* line = std::get_if<candidates::ExactLinePrimitive>(&primitive)) {
    hash->AddU32(line->layer);
    hash->AddI64(line->centerline.start.x);
    hash->AddI64(line->centerline.start.y);
    hash->AddI64(line->centerline.end.x);
    hash->AddI64(line->centerline.end.y);
  } else {
    const auto& via = std::get<candidates::ThroughViaPrimitive>(primitive);
    hash->AddU64(via.template_id);
    hash->AddI64(via.position.x);
    hash->AddI64(via.position.y);
    hash->AddU32(via.start_layer);
    hash->AddU32(via.end_layer);
  }
}

void HashCandidate(board_ir::StableHashBuilder* hash,
                   const Phase4ExactSmallCandidateV1& candidate) noexcept {
  hash->AddU32(candidate.schema_major);
  hash->AddU32(candidate.schema_minor);
  HashId(hash, candidate.id);
  HashEntity(hash, candidate.net);
  HashEntity(hash, candidate.intended_terminals[0]);
  HashEntity(hash, candidate.intended_terminals[1]);
  const auto& associations = candidate.associations;
  hash->AddU64(associations.board_content_hash);
  hash->AddU64(associations.compiler_profile_fingerprint);
  hash->AddU32(associations.geometry_compiler_version);
  hash->AddU64(associations.routing_profile_fingerprint);
  hash->AddU64(associations.rule_bucket_identity);
  hash->AddU32(candidate.geometry_schema_version);
  hash->AddU32(candidate.resource_schema_version);
  HashPolicy(hash, candidate.policy);
  hash->AddU64(candidate.policy_identity);
  const auto& provenance = candidate.provenance;
  hash->AddByte(static_cast<std::uint8_t>(provenance.generator));
  hash->AddU32(provenance.generator_version);
  hash->AddByte(static_cast<std::uint8_t>(provenance.backend));
  hash->AddString(provenance.supported_device_class);
  hash->AddU64(provenance.deterministic_seed);
  hash->AddU64(provenance.batch_identity);
  hash->AddU64(provenance.query_identity);
  hash->AddU32(provenance.candidate_ordinal);
  hash->AddU64(candidate.geometry.size());
  for (const auto& primitive : candidate.geometry) HashPrimitive(hash, primitive);
  HashMetrics(hash, candidate.metrics);
  hash->AddBool(candidate.constraints.supported_hard_constraints_satisfied);
  hash->AddBool(candidate.constraints.unsupported_rules_remain);
  hash->AddU32(candidate.constraints.connected_intended_terminal_count);
  hash->AddByte(static_cast<std::uint8_t>(candidate.constraints.exact_validation_code));
  HashId(hash, candidate.geometry_signature);
  HashId(hash, candidate.resource_signature);
  hash->AddU64(candidate.payload_checksum);
  hash->AddU64(candidate.logical_bytes);
  hash->AddU64(candidate.intrinsic_cost);
  hash->AddU64(candidate.resource_spans.size());
  for (const candidates::PhysicalEdgeSpan& span : candidate.resource_spans) {
    hash->AddU32(span.layer);
    hash->AddI64(span.lattice_x);
    hash->AddI64(span.lattice_y);
    hash->AddByte(static_cast<std::uint8_t>(span.direction));
    hash->AddU32(span.edge_count);
    hash->AddU32(span.usage_units);
  }
}

void HashOutcome(board_ir::StableHashBuilder* hash, const Phase4BoardOutcome& outcome) noexcept {
  hash->AddU64(outcome.selected_net_count);
  hash->AddU64(outcome.no_candidate_net_count);
  hash->AddU64(outcome.overused_resource_count);
  hash->AddU64(outcome.total_overuse_units);
  hash->AddU64(outcome.total_intrinsic_cost);
  hash->AddU64(outcome.world_checksum);
}

void HashSemantics(board_ir::StableHashBuilder* hash,
                   const Phase4TrialArmSemantics& semantics) noexcept {
  hash->AddU32(semantics.schema_version);
  hash->AddByte(static_cast<std::uint8_t>(semantics.arm));
  hash->AddByte(static_cast<std::uint8_t>(semantics.execution_order));
  hash->AddU32(semantics.corpus_version);
  hash->AddU64(semantics.corpus_checksum);
  hash->AddU32(semantics.case_id);
  hash->AddU64(semantics.descriptor_fingerprint);
  hash->AddU64(semantics.case_checksum);
  hash->AddU64(semantics.board_content_hash);
  hash->AddU64(semantics.workload_checksum);
  hash->AddU64(semantics.capacity_model_checksum);
  hash->AddU64(semantics.budget_checksum);
  hash->AddU32(semantics.workload_net_count);
  hash->AddU32(semantics.requested_pool_size);
  hash->AddU32(semantics.repetition_index);
  hash->AddU64(semantics.root_seed);
  hash->AddU32(semantics.preparation_worker_count);
  hash->AddU32(semantics.baseline_sweeps);
  hash->AddU32(semantics.candidate_regeneration_epochs);
  hash->AddU64(semantics.candidate_columns_per_epoch);
  hash->AddU32(semantics.candidate_terminal_selection_rounds);
  hash->AddU64(semantics.external_budget.maximum_prepared_elapsed_nanoseconds);
  hash->AddU64(semantics.external_budget.maximum_cold_elapsed_nanoseconds);
  hash->AddU64(semantics.external_budget.maximum_address_space_bytes);
  hash->AddU64(semantics.external_budget.maximum_peak_host_bytes);
  hash->AddU64(semantics.opportunity.route_queries);
  hash->AddU64(semantics.opportunity.route_work_units);
  hash->AddU64(semantics.actual.route_queries);
  hash->AddU64(semantics.actual.route_work_units);
  hash->AddU64(semantics.preparation_route_queries);
  hash->AddU64(semantics.preparation_route_work_units);
  hash->AddU64(semantics.regeneration_route_queries);
  hash->AddU64(semantics.regeneration_route_work_units);
  hash->AddU64(semantics.requested_columns);
  hash->AddU64(semantics.admitted_candidates);
  hash->AddU64(semantics.rejected_columns);
  hash->AddU64(semantics.final_candidate_count);
  hash->AddU64(semantics.preparation_checksum);
  hash->AddU64(semantics.algorithm_session_checksum);
  hash->AddU64(semantics.final_pool_manifest_checksum);
  hash->AddU64(semantics.final_rejection_manifest_checksum);
  hash->AddByte(static_cast<std::uint8_t>(semantics.terminal_reason));
  hash->AddByte(static_cast<std::uint8_t>(semantics.candidate_outcome_source));
  HashOutcome(hash, semantics.outcome);
  hash->AddU64(semantics.semantic_checksum);
}

[[nodiscard]] std::uint64_t ComputePoolManifest(const Phase4ExactSmallPoolV1& pool) noexcept {
  board_ir::StableHashBuilder hash;
  hash.AddString("APGAR-ONE-WORLD-POOL-MANIFEST-V1");
  HashEntity(&hash, pool.net);
  hash.AddU64(pool.candidates.size());
  for (const auto& candidate : pool.candidates) {
    HashId(&hash, candidate.id);
    hash.AddU64(candidate.payload_checksum);
  }
  return hash.Finish();
}

[[nodiscard]] std::uint64_t ComputePoolsManifest(
    std::span<const Phase4ExactSmallPoolV1> pools) noexcept {
  board_ir::StableHashBuilder hash;
  hash.AddString("APGAR-ONE-WORLD-POOLS-MANIFEST-V1");
  hash.AddU64(pools.size());
  for (const auto& pool : pools) {
    HashEntity(&hash, pool.net);
    hash.AddU64(ComputePoolManifest(pool));
  }
  return hash.Finish();
}

struct ComponentTotals {
  Wide candidate_rows = 0;
  Wide geometry_primitives = 0;
  Wide resource_spans = 0;
  Wide policy_resource_records = 0;
  Wide expanded_resource_edges = 0;
  Wide candidate_logical_bytes = 0;
};

[[nodiscard]] std::optional<Phase4ExactSmallSnapshotError> ValidateComponentTotals(
    const ComponentTotals& totals) {
  if (totals.candidate_rows > kPhase4ExactSmallMaximumCandidateRowsV1 ||
      totals.geometry_primitives > kPhase4ExactSmallMaximumGeometryPrimitivesV1 ||
      totals.resource_spans > kPhase4ExactSmallMaximumResourceSpansV1 ||
      totals.policy_resource_records > kPhase4ExactSmallMaximumPolicyResourceRecordsV1 ||
      totals.expanded_resource_edges > kPhase4ExactSmallMaximumExpandedResourceEdgesV1 ||
      totals.candidate_logical_bytes > kPhase4ExactSmallMaximumAggregateCandidateLogicalBytesV1) {
    return Error(Phase4ExactSmallSnapshotErrorCode::kArtifactBoundExceeded,
                 "P4EXACT-SNAPSHOT-COMPONENT-BOUND-001",
                 "aggregate candidate rows, components, expanded edges, or logical bytes exceed "
                 "the frozen exact-small artifact bounds");
  }
  return std::nullopt;
}

void AddCandidateShape(ComponentTotals* totals,
                       const candidates::GeneratedRouteCandidate& candidate) noexcept {
  ++totals->candidate_rows;
  totals->geometry_primitives += candidate.geometry.size();
  totals->resource_spans += candidate.resources.size();
  totals->policy_resource_records += candidate.policy.banned_resources.size();
  totals->policy_resource_records += candidate.policy.resource_penalties.size();
  totals->candidate_logical_bytes += candidate.logical_bytes;
}

void AddCandidateShape(ComponentTotals* totals,
                       const Phase4ExactSmallCandidateV1& candidate) noexcept {
  ++totals->candidate_rows;
  totals->geometry_primitives += candidate.geometry.size();
  totals->resource_spans += candidate.resource_spans.size();
  totals->policy_resource_records += candidate.policy.banned_resources.size();
  totals->policy_resource_records += candidate.policy.resource_penalties.size();
  totals->candidate_logical_bytes += candidate.logical_bytes;
}

[[nodiscard]] std::optional<Phase4ExactSmallSnapshotError> AddExpandedResourceEdges(
    ComponentTotals* totals, std::span<const candidates::PhysicalEdgeSpan> spans) noexcept {
  for (const auto& span : spans) {
    totals->expanded_resource_edges += span.edge_count;
    if (totals->expanded_resource_edges > kPhase4ExactSmallMaximumExpandedResourceEdgesV1) {
      return Error(Phase4ExactSmallSnapshotErrorCode::kArtifactBoundExceeded,
                   "P4EXACT-SNAPSHOT-COMPONENT-BOUND-001",
                   "aggregate expanded resource edges exceed the frozen exact-small bound");
    }
  }
  return std::nullopt;
}

[[nodiscard]] bool NonzeroRawReference(const Phase4PerNetReportRawReferenceV1& reference) noexcept {
  return reference.repetition_index == 0 &&
         reference.execution_order == Phase4TrialOrder::kBaselineFirst &&
         reference.pair_attempt_checksum != 0 && reference.paired_semantic_checksum != 0 &&
         reference.paired_artifact_checksum != 0 && reference.baseline_semantic_checksum != 0 &&
         reference.baseline_arm_artifact_checksum != 0 &&
         reference.candidate_semantic_checksum != 0 &&
         reference.candidate_arm_artifact_checksum != 0;
}

[[nodiscard]] bool SameOutcome(const Phase4BoardOutcome& left,
                               const allocator::OneWorldAllocation& right) noexcept {
  return left.selected_net_count == right.selected_net_count &&
         left.no_candidate_net_count == right.no_candidate_net_count &&
         left.overused_resource_count == right.overused_resource_count &&
         left.total_overuse_units == right.total_overuse_units &&
         left.total_intrinsic_cost == right.total_intrinsic_cost &&
         left.world_checksum == right.world_checksum;
}

class JsonWriter {
 public:
  explicit JsonWriter(std::size_t maximum_bytes) : maximum_bytes_(maximum_bytes) {}

  void BeginObject() {
    Append('{');
    scopes_.push_back(true);
  }
  void EndObject() {
    Append('}');
    scopes_.pop_back();
  }
  void BeginArray() {
    Append('[');
    scopes_.push_back(true);
  }
  void EndArray() {
    Append(']');
    scopes_.pop_back();
  }
  void Key(std::string_view key) {
    Separate();
    String(key);
    Append(':');
  }
  void Element() { Separate(); }
  void String(std::string_view value) {
    constexpr char kHex[] = "0123456789abcdef";
    const std::size_t remaining = maximum_bytes_ - std::min(maximum_bytes_, output_.size());
    if (failed_ || value.size() > remaining || remaining - value.size() < 2U) {
      failed_ = true;
      return;
    }
    Append('"');
    for (const unsigned char character : value) {
      if (character == '"')
        Append("\\\"");
      else if (character == '\\')
        Append("\\\\");
      else if (character < 0x20U) {
        Append("\\u00");
        Append(kHex[character >> 4U]);
        Append(kHex[character & 0xfU]);
      } else
        Append(static_cast<char>(character));
      if (failed_) return;
    }
    Append('"');
  }
  void U64(std::uint64_t value) { Append(std::to_string(value)); }
  void I64(std::int64_t value) { Append(std::to_string(value)); }
  void Bool(bool value) { Append(value ? "true" : "false"); }
  void Null() { Append("null"); }
  [[nodiscard]] bool ok() const noexcept { return !failed_; }
  [[nodiscard]] std::string Finish() {
    Append('\n');
    return std::move(output_);
  }

 private:
  void Separate() {
    if (!scopes_.back()) Append(',');
    scopes_.back() = false;
  }
  void Append(char value) {
    if (failed_ || output_.size() == maximum_bytes_) {
      failed_ = true;
      return;
    }
    output_ += value;
  }
  void Append(std::string_view value) {
    if (failed_ || value.size() > maximum_bytes_ - std::min(maximum_bytes_, output_.size())) {
      failed_ = true;
      return;
    }
    output_ += value;
  }
  std::string output_;
  std::vector<bool> scopes_;
  std::size_t maximum_bytes_ = 0;
  bool failed_ = false;
};

void WriteEntity(JsonWriter* writer, board_ir::EntityRef value) {
  writer->BeginObject();
  writer->Key("id");
  writer->U64(value.id);
  writer->Key("generation");
  writer->U64(value.generation);
  writer->EndObject();
}

void WriteId(JsonWriter* writer, candidates::Hash128 value) {
  writer->BeginObject();
  writer->Key("high");
  writer->U64(value.high);
  writer->Key("low");
  writer->U64(value.low);
  writer->EndObject();
}

void WriteResource(JsonWriter* writer, const routing::EdgeResourceKey& value) {
  writer->BeginObject();
  writer->Key("layer");
  writer->U64(value.layer);
  writer->Key("lattice_x");
  writer->I64(value.lattice_x);
  writer->Key("lattice_y");
  writer->I64(value.lattice_y);
  writer->Key("direction");
  writer->U64(static_cast<std::uint8_t>(value.direction));
  writer->EndObject();
}

void WriteCell(JsonWriter* writer, const Phase4CanonicalCellConfig& value) {
  writer->BeginObject();
  writer->Key("schema_version");
  writer->U64(value.schema_version);
  writer->Key("case_id");
  writer->U64(value.case_id);
  writer->Key("requested_pool_size");
  writer->U64(value.requested_pool_size);
  writer->Key("preparation_worker_count");
  writer->U64(value.preparation_worker_count);
  writer->Key("repetitions");
  writer->U64(value.repetitions);
  writer->Key("maximum_setup_elapsed_nanoseconds");
  writer->U64(value.maximum_setup_elapsed_nanoseconds);
  writer->Key("external_budget");
  writer->BeginObject();
  writer->Key("maximum_prepared_elapsed_nanoseconds");
  writer->U64(value.external_budget.maximum_prepared_elapsed_nanoseconds);
  writer->Key("maximum_cold_elapsed_nanoseconds");
  writer->U64(value.external_budget.maximum_cold_elapsed_nanoseconds);
  writer->Key("maximum_address_space_bytes");
  writer->U64(value.external_budget.maximum_address_space_bytes);
  writer->Key("maximum_peak_host_bytes");
  writer->U64(value.external_budget.maximum_peak_host_bytes);
  writer->EndObject();
  writer->Key("corpus_limits");
  writer->BeginObject();
  writer->Key("maximum_nets");
  writer->U64(value.corpus_limits.maximum_nets);
  writer->Key("maximum_compiled_nodes");
  writer->U64(value.corpus_limits.maximum_compiled_nodes);
  writer->Key("maximum_compiled_host_bytes");
  writer->U64(value.corpus_limits.maximum_compiled_host_bytes);
  writer->Key("maximum_active_regions");
  writer->U64(value.corpus_limits.maximum_active_regions);
  writer->Key("maximum_board_entities");
  writer->U64(value.corpus_limits.maximum_board_entities);
  writer->EndObject();
  writer->EndObject();
}

void WriteMetrics(JsonWriter* writer, const candidates::CandidateMetrics& value) {
  writer->BeginObject();
  writer->Key("scalar_policy_cost");
  writer->U64(value.scalar_policy_cost);
  writer->Key("intrinsic_base_cost");
  writer->U64(value.intrinsic_base_cost);
  writer->Key("orthogonal_step_count");
  writer->U64(value.orthogonal_step_count);
  writer->Key("diagonal_step_count");
  writer->U64(value.diagonal_step_count);
  writer->Key("bend_count");
  writer->U64(value.bend_count);
  writer->Key("line_primitive_count");
  writer->U64(value.line_primitive_count);
  writer->Key("via_count");
  writer->U64(value.via_count);
  writer->Key("axis_aligned_length_dbu");
  writer->U64(value.axis_aligned_length_dbu);
  writer->Key("diagonal_projection_dbu");
  writer->U64(value.diagonal_projection_dbu);
  writer->EndObject();
}

void WriteOutcome(JsonWriter* writer, const Phase4BoardOutcome& value) {
  writer->BeginObject();
  writer->Key("selected_net_count");
  writer->U64(value.selected_net_count);
  writer->Key("no_candidate_net_count");
  writer->U64(value.no_candidate_net_count);
  writer->Key("overused_resource_count");
  writer->U64(value.overused_resource_count);
  writer->Key("total_overuse_units");
  writer->U64(value.total_overuse_units);
  writer->Key("total_intrinsic_cost");
  writer->U64(value.total_intrinsic_cost);
  writer->Key("world_checksum");
  writer->U64(value.world_checksum);
  writer->EndObject();
}

void WriteSemantics(JsonWriter* writer, const Phase4TrialArmSemantics& value) {
  writer->BeginObject();
  writer->Key("schema_version");
  writer->U64(value.schema_version);
  writer->Key("arm");
  writer->U64(static_cast<std::uint8_t>(value.arm));
  writer->Key("execution_order");
  writer->U64(static_cast<std::uint8_t>(value.execution_order));
  writer->Key("corpus_version");
  writer->U64(value.corpus_version);
  writer->Key("corpus_checksum");
  writer->U64(value.corpus_checksum);
  writer->Key("case_id");
  writer->U64(value.case_id);
  writer->Key("descriptor_fingerprint");
  writer->U64(value.descriptor_fingerprint);
  writer->Key("case_checksum");
  writer->U64(value.case_checksum);
  writer->Key("board_content_hash");
  writer->U64(value.board_content_hash);
  writer->Key("workload_checksum");
  writer->U64(value.workload_checksum);
  writer->Key("capacity_model_checksum");
  writer->U64(value.capacity_model_checksum);
  writer->Key("budget_checksum");
  writer->U64(value.budget_checksum);
  writer->Key("workload_net_count");
  writer->U64(value.workload_net_count);
  writer->Key("requested_pool_size");
  writer->U64(value.requested_pool_size);
  writer->Key("repetition_index");
  writer->U64(value.repetition_index);
  writer->Key("root_seed");
  writer->U64(value.root_seed);
  writer->Key("preparation_worker_count");
  writer->U64(value.preparation_worker_count);
  writer->Key("baseline_sweeps");
  writer->U64(value.baseline_sweeps);
  writer->Key("candidate_regeneration_epochs");
  writer->U64(value.candidate_regeneration_epochs);
  writer->Key("candidate_columns_per_epoch");
  writer->U64(value.candidate_columns_per_epoch);
  writer->Key("candidate_terminal_selection_rounds");
  writer->U64(value.candidate_terminal_selection_rounds);
  writer->Key("external_budget");
  writer->BeginObject();
  writer->Key("maximum_prepared_elapsed_nanoseconds");
  writer->U64(value.external_budget.maximum_prepared_elapsed_nanoseconds);
  writer->Key("maximum_cold_elapsed_nanoseconds");
  writer->U64(value.external_budget.maximum_cold_elapsed_nanoseconds);
  writer->Key("maximum_address_space_bytes");
  writer->U64(value.external_budget.maximum_address_space_bytes);
  writer->Key("maximum_peak_host_bytes");
  writer->U64(value.external_budget.maximum_peak_host_bytes);
  writer->EndObject();
  const auto write_opportunity = [writer](std::string_view key,
                                          const Phase4RouteOpportunity& opportunity) {
    writer->Key(key);
    writer->BeginObject();
    writer->Key("route_queries");
    writer->U64(opportunity.route_queries);
    writer->Key("route_work_units");
    writer->U64(opportunity.route_work_units);
    writer->EndObject();
  };
  write_opportunity("opportunity", value.opportunity);
  write_opportunity("actual", value.actual);
  writer->Key("preparation_route_queries");
  writer->U64(value.preparation_route_queries);
  writer->Key("preparation_route_work_units");
  writer->U64(value.preparation_route_work_units);
  writer->Key("regeneration_route_queries");
  writer->U64(value.regeneration_route_queries);
  writer->Key("regeneration_route_work_units");
  writer->U64(value.regeneration_route_work_units);
  writer->Key("requested_columns");
  writer->U64(value.requested_columns);
  writer->Key("admitted_candidates");
  writer->U64(value.admitted_candidates);
  writer->Key("rejected_columns");
  writer->U64(value.rejected_columns);
  writer->Key("final_candidate_count");
  writer->U64(value.final_candidate_count);
  writer->Key("preparation_checksum");
  writer->U64(value.preparation_checksum);
  writer->Key("algorithm_session_checksum");
  writer->U64(value.algorithm_session_checksum);
  writer->Key("final_pool_manifest_checksum");
  writer->U64(value.final_pool_manifest_checksum);
  writer->Key("final_rejection_manifest_checksum");
  writer->U64(value.final_rejection_manifest_checksum);
  writer->Key("terminal_reason");
  writer->U64(static_cast<std::uint8_t>(value.terminal_reason));
  writer->Key("candidate_outcome_source");
  writer->U64(static_cast<std::uint8_t>(value.candidate_outcome_source));
  writer->Key("outcome");
  WriteOutcome(writer, value.outcome);
  writer->Key("semantic_checksum");
  writer->U64(value.semantic_checksum);
  writer->EndObject();
}

}  // namespace

namespace {

[[nodiscard]] bool IsExactSmallCaseForAuthority(Phase4RepresentativeCorpusAuthority authority,
                                                std::uint32_t case_id) noexcept {
  switch (authority) {
    case Phase4RepresentativeCorpusAuthority::kV1:
      return case_id == 100 || case_id == 101 || case_id == 102;
    case Phase4RepresentativeCorpusAuthority::kV2:
      return case_id == 10'100;
  }
  return false;
}

[[nodiscard]] std::uint64_t ComputeExactSmallCellPlanChecksum(
    Phase4RepresentativeCorpusAuthority authority,
    const Phase4CanonicalCellConfig& config) noexcept {
  switch (authority) {
    case Phase4RepresentativeCorpusAuthority::kV1:
      return ComputePhase4CanonicalCellPlanChecksumV1(config);
    case Phase4RepresentativeCorpusAuthority::kV2:
      return ComputePhase4CanonicalCellPlanChecksumForCorpusV2(config);
  }
  return 0;
}

[[nodiscard]] Phase4CanonicalSpecResult BuildExactSmallCanonicalSpec(
    Phase4RepresentativeCorpusAuthority authority, const Phase4CanonicalCellConfig& config) {
  switch (authority) {
    case Phase4RepresentativeCorpusAuthority::kV1:
      return BuildPhase4CanonicalTrialSpecV1(config, 0, Phase4TrialOrder::kBaselineFirst);
    case Phase4RepresentativeCorpusAuthority::kV2:
      return BuildPhase4CanonicalTrialSpecForCorpusV2(config, 0, Phase4TrialOrder::kBaselineFirst);
  }
  return Phase4TrialHarnessError{};
}

[[nodiscard]] std::uint64_t ComputeExactSmallWorkloadRosterChecksum(
    Phase4RepresentativeCorpusAuthority authority,
    const Phase4RepresentativeCase& representative_case) noexcept {
  switch (authority) {
    case Phase4RepresentativeCorpusAuthority::kV1:
      return ComputePhase4WorkloadNetRosterChecksumV1(representative_case);
    case Phase4RepresentativeCorpusAuthority::kV2:
      return ComputePhase4WorkloadNetRosterChecksumV2(representative_case);
  }
  return 0;
}

[[nodiscard]] std::variant<std::monostate, Phase4ExactSmallSnapshotError>
ValidatePhase4ExactSmallSnapshotArtifactForAuthority(
    Phase4RepresentativeCorpusAuthority authority,
    const Phase4ExactSmallSnapshotArtifactV1& artifact, std::string_view imported_fixture);

}  // namespace

Phase4ExactSmallCartesianPreflightResultV1 PreflightPhase4ExactSmallCartesianProductV1(
    std::span<const std::uint64_t> pool_sizes, std::uint64_t declared_maximum) noexcept {
  if (pool_sizes.size() != kPhase4ExactSmallPoolCountV1) {
    return Error(Phase4ExactSmallSnapshotErrorCode::kRosterMismatch,
                 "P4EXACT-SNAPSHOT-PRODUCT-ROSTER-001",
                 "the exact-small Cartesian preflight requires exactly six explicit pools",
                 kPhase4ExactSmallPoolCountV1, pool_sizes.size());
  }
  Wide product = 1;
  for (const std::uint64_t size : pool_sizes) {
    product *= static_cast<Wide>(std::max<std::uint64_t>(1, size));
    if (product > static_cast<Wide>(kPhase4ExactSmallMaximumCartesianProductV1) ||
        product > static_cast<Wide>(declared_maximum)) {
      return Error(Phase4ExactSmallSnapshotErrorCode::kCartesianProductExceeded,
                   "P4EXACT-SNAPSHOT-PRODUCT-001",
                   "the complete final-pool Cartesian product exceeds the exact-case bound",
                   product > std::numeric_limits<std::uint64_t>::max()
                       ? std::numeric_limits<std::uint64_t>::max()
                       : static_cast<std::uint64_t>(product),
                   std::min(declared_maximum, kPhase4ExactSmallMaximumCartesianProductV1));
    }
  }
  return static_cast<std::uint64_t>(product);
}

std::uint64_t ComputePhase4ExactSmallSnapshotArtifactChecksumV1(
    const Phase4ExactSmallSnapshotArtifactV1& artifact) noexcept {
  board_ir::StableHashBuilder hash;
  hash.AddString("apgar.phase4.exact_small_snapshot.artifact.v1");
  hash.AddU32(artifact.schema_version);
  hash.AddBool(artifact.decision_eligible);
  HashCell(&hash, artifact.config);
  hash.AddByte(static_cast<std::uint8_t>(artifact.execution_order));
  hash.AddU64(artifact.root_seed);
  hash.AddU64(artifact.corpus_checksum);
  hash.AddU64(artifact.descriptor_fingerprint);
  hash.AddU64(artifact.case_checksum);
  hash.AddU64(artifact.board_content_hash);
  hash.AddU64(artifact.workload_checksum);
  hash.AddU64(artifact.workload_roster.size());
  for (const auto net : artifact.workload_roster) HashEntity(&hash, net);
  hash.AddU64(artifact.workload_net_roster_checksum);
  hash.AddU64(artifact.budget_checksum);
  hash.AddU64(artifact.raw_cell_plan_checksum);
  hash.AddU64(artifact.raw_cell_artifact_checksum);
  hash.AddU64(artifact.raw_source_envelope_checksum);
  HashRawReference(&hash, artifact.raw_reference);
  hash.AddU64(artifact.per_net_report_artifact_checksum);
  hash.AddU64(artifact.per_net_report_source_envelope_checksum);
  hash.AddU64(artifact.per_net_candidate_telemetry_checksum);
  HashSemantics(&hash, artifact.candidate_semantics);
  hash.AddU64(artifact.candidate_semantic_checksum);
  hash.AddU64(artifact.candidate_session_checksum);
  hash.AddU64(artifact.final_pool_manifest_checksum);
  hash.AddU64(artifact.final_rejection_manifest_checksum);
  hash.AddByte(static_cast<std::uint8_t>(artifact.production_outcome_source));
  hash.AddU32(artifact.capacity_schema_version);
  hash.AddU64(artifact.capacity_associations.board_content_hash);
  hash.AddU64(artifact.capacity_associations.compiler_profile_fingerprint);
  hash.AddU32(artifact.capacity_associations.geometry_compiler_version);
  hash.AddU32(artifact.default_capacity_units);
  hash.AddU64(artifact.capacity_overrides.size());
  for (const auto& override : artifact.capacity_overrides) {
    HashResource(&hash, override.resource);
    hash.AddU32(override.capacity_units);
  }
  hash.AddU64(artifact.capacity_model_checksum);
  hash.AddU64(artifact.cartesian_product);
  hash.AddU64(artifact.pools.size());
  for (const auto& pool : artifact.pools) {
    HashEntity(&hash, pool.net);
    hash.AddU64(pool.candidates.size());
    for (const auto& candidate : pool.candidates) HashCandidate(&hash, candidate);
  }
  hash.AddU64(artifact.production_selections.size());
  for (const auto& selection : artifact.production_selections) {
    HashEntity(&hash, selection.net);
    hash.AddByte(static_cast<std::uint8_t>(selection.status));
    hash.AddBool(selection.candidate_id.has_value());
    if (selection.candidate_id) HashId(&hash, *selection.candidate_id);
    hash.AddBool(selection.candidate_payload_checksum.has_value());
    if (selection.candidate_payload_checksum) hash.AddU64(*selection.candidate_payload_checksum);
    hash.AddU64(selection.intrinsic_cost);
  }
  hash.AddU64(artifact.production_outcome.selected_net_count);
  hash.AddU64(artifact.production_outcome.no_candidate_net_count);
  hash.AddU64(artifact.production_outcome.overused_resource_count);
  hash.AddU64(artifact.production_outcome.total_overuse_units);
  hash.AddU64(artifact.production_outcome.total_intrinsic_cost);
  hash.AddU64(artifact.production_outcome.world_checksum);
  hash.AddU64(artifact.maximum_serialized_bytes);
  return hash.Finish();
}

std::uint64_t ComputePhase4ExactSmallSnapshotSourceEnvelopeChecksumV1(
    std::string_view source_commit, bool source_stamped, bool source_tree_dirty,
    std::uint64_t artifact_checksum) noexcept {
  board_ir::StableHashBuilder hash;
  hash.AddString("apgar.phase4.exact_small_snapshot.source_envelope.v1");
  hash.AddString(source_commit);
  hash.AddBool(source_stamped);
  hash.AddBool(source_tree_dirty);
  hash.AddU64(artifact_checksum);
  return hash.Finish();
}

namespace {

Phase4ExactSmallSnapshotArtifactResultV1 BuildPhase4ExactSmallSnapshotArtifactForAuthority(
    Phase4RepresentativeCorpusAuthority authority, const Phase4CanonicalCellConfig& config,
    std::string_view source_commit, bool source_stamped, bool source_tree_dirty,
    std::uint64_t raw_cell_plan_checksum, std::uint64_t raw_cell_artifact_checksum,
    std::uint64_t raw_source_envelope_checksum, Phase4PerNetReportRawReferenceV1 raw_reference,
    std::uint64_t per_net_report_artifact_checksum,
    std::uint64_t per_net_report_source_envelope_checksum,
    Phase4CandidatePoolSnapshotExecutionV1 capture, std::string_view imported_fixture,
    std::uint32_t raw_evidence_schema_version) try {
  if (!IsLowerHexCommit(source_commit) || !source_stamped || source_tree_dirty) {
    return Error(Phase4ExactSmallSnapshotErrorCode::kAuthorityAssociation,
                 "P4EXACT-SNAPSHOT-SOURCE-001",
                 "a clean stamped 40-character lowercase source commit is required");
  }
  if ((authority == Phase4RepresentativeCorpusAuthority::kV2 &&
       raw_evidence_schema_version != kPhase4SameRunRawEvidenceSchemaVersion) ||
      (raw_evidence_schema_version != 1 &&
       raw_evidence_schema_version != kPhase4SameRunRawEvidenceSchemaVersion) ||
      raw_cell_plan_checksum != ComputeExactSmallCellPlanChecksum(authority, config) ||
      raw_cell_artifact_checksum == 0 ||
      raw_source_envelope_checksum !=
          (raw_evidence_schema_version == kPhase4SameRunRawEvidenceSchemaVersion
               ? ComputePhase4SourceEnvelopeChecksumV2(
                     kPhase4SameRunRawEvidenceSchemaVersion, kPhase4SameRunTrialWireSchemaVersion,
                     source_commit, source_stamped, source_tree_dirty, raw_cell_artifact_checksum)
               : ComputePhase4SourceEnvelopeChecksumV1(kPhase4TrialWireSchemaVersion, source_commit,
                                                       source_stamped, source_tree_dirty,
                                                       raw_cell_artifact_checksum)) ||
      !NonzeroRawReference(raw_reference) || per_net_report_artifact_checksum == 0 ||
      per_net_report_source_envelope_checksum !=
          ComputePhase4PerNetReportSourceEnvelopeChecksumV1(
              source_commit, source_stamped, source_tree_dirty, per_net_report_artifact_checksum)) {
    return Error(Phase4ExactSmallSnapshotErrorCode::kAuthorityAssociation,
                 "P4EXACT-SNAPSHOT-AUTHORITY-001",
                 "Raw or per-net report claimed associations are absent or locally "
                 "inconsistent; publication must perform the external artifact join");
  }
  const Phase4CaseDescriptor* descriptor =
      FindPhase4CaseDescriptorForAuthority(authority, config.case_id);
  if (descriptor == nullptr || descriptor->role != Phase4CaseRole::kExactOracle ||
      !IsExactSmallCaseForAuthority(authority, config.case_id) ||
      descriptor->maximum_exact_candidate_products == 0) {
    return Error(Phase4ExactSmallSnapshotErrorCode::kUnsupportedCase, "P4EXACT-SNAPSHOT-CASE-001",
                 "the case is not exact-small under the explicitly selected corpus authority");
  }
  if (config.requested_pool_size != 4 ||
      capture.final_pools.size() != descriptor->requested_net_count) {
    return Error(Phase4ExactSmallSnapshotErrorCode::kInvalidConfiguration,
                 "P4EXACT-SNAPSHOT-CONFIG-001",
                 "the canonical exact cell requires pool size four and exactly six final pools");
  }

  // Product preflight is intentionally before every candidate dereference,
  // candidate-field check, or resource-span traversal.
  std::array<std::uint64_t, kPhase4ExactSmallPoolCountV1> pool_sizes{};
  for (std::size_t index = 0; index < capture.final_pools.size(); ++index) {
    pool_sizes[index] = capture.final_pools[index].candidates.size();
  }
  auto product_result = PreflightPhase4ExactSmallCartesianProductV1(
      pool_sizes, descriptor->maximum_exact_candidate_products);
  if (std::holds_alternative<Phase4ExactSmallSnapshotError>(product_result)) {
    return std::get<Phase4ExactSmallSnapshotError>(std::move(product_result));
  }
  const std::uint64_t product = std::get<std::uint64_t>(product_result);

  if (capture.capacity_overrides.size() > kPhase4ExactSmallMaximumCapacityOverridesV1) {
    return Error(Phase4ExactSmallSnapshotErrorCode::kArtifactBoundExceeded,
                 "P4EXACT-SNAPSHOT-CAPACITY-BOUND-001",
                 "capacity override rows exceed the frozen exact-small bound",
                 capture.capacity_overrides.size(), kPhase4ExactSmallMaximumCapacityOverridesV1);
  }

  ComponentTotals capture_totals;
  for (const allocator::CandidatePool& pool : capture.final_pools) {
    if (pool.candidates.size() > kPhase4ExactSmallMaximumCandidatesPerPoolV1) {
      return Error(Phase4ExactSmallSnapshotErrorCode::kArtifactBoundExceeded,
                   "P4EXACT-SNAPSHOT-POOL-BOUND-001",
                   "one final pool exceeds the canonical pool-plus-epochs candidate cap",
                   pool.candidates.size(), kPhase4ExactSmallMaximumCandidatesPerPoolV1);
    }
    for (const candidates::StoredCandidate& stored : pool.candidates) {
      if (stored == nullptr) {
        return Error(Phase4ExactSmallSnapshotErrorCode::kCandidateInvariant,
                     "P4EXACT-SNAPSHOT-CANDIDATE-001",
                     "a final pool contains a null immutable candidate");
      }
      AddCandidateShape(&capture_totals, stored->data());
    }
  }
  if (auto bound = ValidateComponentTotals(capture_totals); bound.has_value()) return *bound;
  for (const allocator::CandidatePool& pool : capture.final_pools) {
    for (const candidates::StoredCandidate& stored : pool.candidates) {
      if (auto bound = AddExpandedResourceEdges(&capture_totals, stored->data().resources);
          bound.has_value()) {
        return *bound;
      }
    }
  }

  Phase4CanonicalSpecResult spec_result = BuildExactSmallCanonicalSpec(authority, config);
  if (std::holds_alternative<Phase4TrialHarnessError>(spec_result)) {
    return Error(Phase4ExactSmallSnapshotErrorCode::kInvalidConfiguration,
                 "P4EXACT-SNAPSHOT-CONFIG-002", "the canonical cell configuration is invalid");
  }
  Phase4PairedTrialSpec spec = std::get<Phase4PairedTrialSpec>(std::move(spec_result));
  Phase4RepresentativeCaseResult case_result = BuildPhase4RepresentativeCaseForAuthority(
      authority, config.case_id, imported_fixture, config.corpus_limits);
  if (!std::holds_alternative<Phase4RepresentativeCase>(case_result)) {
    return Error(Phase4ExactSmallSnapshotErrorCode::kInvalidConfiguration,
                 "P4EXACT-SNAPSHOT-CASE-002", "the exact representative case cannot be built");
  }
  Phase4RepresentativeCase representative_case =
      std::get<Phase4RepresentativeCase>(std::move(case_result));
  const Phase4TrialArmSemantics& semantics = capture.semantics;
  if (internal::ValidatePhase4TrialArmSemanticsForAuthorityV1(authority, semantics).has_value() ||
      semantics.arm != Phase4TrialArm::kReusableCandidateAllocation ||
      semantics.execution_order != Phase4TrialOrder::kBaselineFirst ||
      semantics.case_id != config.case_id || semantics.requested_pool_size != 4 ||
      semantics.repetition_index != 0 || semantics.root_seed != spec.root_seed ||
      semantics.corpus_checksum != Phase4RepresentativeCorpusChecksumForAuthority(authority) ||
      semantics.descriptor_fingerprint !=
          FingerprintPhase4CaseDescriptorForAuthority(authority, *descriptor) ||
      semantics.case_checksum != representative_case.case_checksum ||
      semantics.board_content_hash != representative_case.board.content_hash() ||
      semantics.workload_checksum != representative_case.workload.workload_checksum() ||
      semantics.capacity_model_checksum !=
          allocator::internal::ComputeResourceCapacityModelChecksumV1(
              allocator::internal::ResourceCapacityChecksumHeaderV1{
                  .schema_version = representative_case.capacities.schema_version(),
                  .associations = representative_case.capacities.associations(),
                  .default_capacity_units =
                      representative_case.capacities.default_capacity_units()},
              representative_case.capacities.overrides()) ||
      semantics.semantic_checksum == 0 ||
      semantics.semantic_checksum != internal::ComputePhase4TrialArmSemanticChecksumV1(semantics)) {
    return Error(Phase4ExactSmallSnapshotErrorCode::kAuthorityAssociation,
                 "P4EXACT-SNAPSHOT-SEMANTIC-001",
                 "the captured candidate semantics do not authenticate this canonical cell");
  }
  if (auto telemetry_error = internal::ValidatePhase4ArmReportTelemetryForAuthorityV1(
          authority, semantics, representative_case.workload, capture.telemetry);
      telemetry_error.has_value()) {
    return Error(Phase4ExactSmallSnapshotErrorCode::kAuthorityAssociation,
                 "P4EXACT-SNAPSHOT-TELEMETRY-001",
                 "the captured per-net telemetry is not associated with the candidate execution");
  }
  if (raw_reference.candidate_semantic_checksum != semantics.semantic_checksum) {
    return Error(Phase4ExactSmallSnapshotErrorCode::kAuthorityAssociation,
                 "P4EXACT-SNAPSHOT-AUTHORITY-001",
                 "Raw or per-net report claimed associations are absent or locally "
                 "inconsistent; publication must perform the external artifact join");
  }
  if (capture.capacity_schema_version != representative_case.capacities.schema_version() ||
      !(capture.capacity_associations == representative_case.capacities.associations()) ||
      capture.default_capacity_units != representative_case.capacities.default_capacity_units() ||
      capture.capacity_overrides != representative_case.capacities.overrides()) {
    return Error(Phase4ExactSmallSnapshotErrorCode::kAuthorityAssociation,
                 "P4EXACT-SNAPSHOT-CAPACITY-001",
                 "the captured current capacity vocabulary does not match the representative case");
  }
  if (capture.production_world.selections.size() != capture.final_pools.size() ||
      !SameOutcome(semantics.outcome, capture.production_world)) {
    return Error(Phase4ExactSmallSnapshotErrorCode::kRosterMismatch, "P4EXACT-SNAPSHOT-WORLD-001",
                 "the production world does not cover the complete final-pool roster");
  }

  Phase4ExactSmallSnapshotArtifactV1 artifact;
  artifact.source_commit = std::string(source_commit);
  artifact.source_stamped = source_stamped;
  artifact.source_tree_dirty = source_tree_dirty;
  artifact.config = config;
  artifact.execution_order = semantics.execution_order;
  artifact.root_seed = semantics.root_seed;
  artifact.corpus_checksum = semantics.corpus_checksum;
  artifact.descriptor_fingerprint = semantics.descriptor_fingerprint;
  artifact.case_checksum = semantics.case_checksum;
  artifact.board_content_hash = semantics.board_content_hash;
  artifact.workload_checksum = semantics.workload_checksum;
  artifact.workload_roster.reserve(representative_case.workload.nets().size());
  for (const auto& context : representative_case.workload.nets()) {
    artifact.workload_roster.push_back(context.request.net);
  }
  artifact.workload_net_roster_checksum =
      ComputeExactSmallWorkloadRosterChecksum(authority, representative_case);
  artifact.budget_checksum = semantics.budget_checksum;
  artifact.raw_cell_plan_checksum = raw_cell_plan_checksum;
  artifact.raw_cell_artifact_checksum = raw_cell_artifact_checksum;
  artifact.raw_source_envelope_checksum = raw_source_envelope_checksum;
  artifact.raw_reference = raw_reference;
  artifact.per_net_report_artifact_checksum = per_net_report_artifact_checksum;
  artifact.per_net_report_source_envelope_checksum = per_net_report_source_envelope_checksum;
  artifact.per_net_candidate_telemetry_checksum = capture.telemetry.telemetry_checksum;
  artifact.candidate_semantics = semantics;
  artifact.candidate_semantic_checksum = semantics.semantic_checksum;
  artifact.candidate_session_checksum = semantics.algorithm_session_checksum;
  artifact.final_pool_manifest_checksum = semantics.final_pool_manifest_checksum;
  artifact.final_rejection_manifest_checksum = semantics.final_rejection_manifest_checksum;
  artifact.production_outcome_source = semantics.candidate_outcome_source;
  artifact.capacity_schema_version = capture.capacity_schema_version;
  artifact.capacity_associations = capture.capacity_associations;
  artifact.default_capacity_units = capture.default_capacity_units;
  artifact.capacity_overrides = std::move(capture.capacity_overrides);
  artifact.capacity_model_checksum = semantics.capacity_model_checksum;
  artifact.cartesian_product = product;
  artifact.pools.reserve(capture.final_pools.size());
  for (const allocator::CandidatePool& pool : capture.final_pools) {
    Phase4ExactSmallPoolV1 output_pool{.net = pool.net, .candidates = {}};
    output_pool.candidates.reserve(pool.candidates.size());
    for (const candidates::StoredCandidate& stored : pool.candidates) {
      if (stored == nullptr || !(stored->net() == pool.net)) {
        return Error(Phase4ExactSmallSnapshotErrorCode::kCandidateInvariant,
                     "P4EXACT-SNAPSHOT-CANDIDATE-001",
                     "a final pool contains a null or wrong-net immutable candidate");
      }
      const candidates::GeneratedRouteCandidate& data = stored->data();
      output_pool.candidates.push_back(
          Phase4ExactSmallCandidateV1{.schema_major = data.schema_major,
                                      .schema_minor = data.schema_minor,
                                      .id = data.id,
                                      .net = data.net,
                                      .intended_terminals = data.intended_terminals,
                                      .associations = data.associations,
                                      .geometry_schema_version = data.geometry_schema_version,
                                      .resource_schema_version = data.resource_schema_version,
                                      .policy = data.policy,
                                      .policy_identity = data.policy_identity,
                                      .provenance = data.provenance,
                                      .geometry = data.geometry,
                                      .metrics = data.metrics,
                                      .constraints = data.constraints,
                                      .geometry_signature = data.geometry_signature,
                                      .resource_signature = data.resource_signature,
                                      .payload_checksum = data.payload_checksum,
                                      .logical_bytes = data.logical_bytes,
                                      .intrinsic_cost = data.metrics.intrinsic_base_cost,
                                      .resource_spans = data.resources});
    }
    std::sort(output_pool.candidates.begin(), output_pool.candidates.end(),
              [](const auto& left, const auto& right) { return left.id < right.id; });
    artifact.pools.push_back(std::move(output_pool));
  }
  artifact.production_selections.reserve(capture.production_world.selections.size());
  for (const allocator::NetSelection& selection : capture.production_world.selections) {
    const auto pool =
        std::ranges::find(artifact.pools, selection.net, &Phase4ExactSmallPoolV1::net);
    std::uint64_t intrinsic_cost = 0;
    if (selection.candidate_id.has_value() && pool != artifact.pools.end()) {
      const auto selected_candidate = std::ranges::find(pool->candidates, *selection.candidate_id,
                                                        &Phase4ExactSmallCandidateV1::id);
      if (selected_candidate != pool->candidates.end()) {
        intrinsic_cost = selected_candidate->metrics.intrinsic_base_cost;
      }
    }
    artifact.production_selections.push_back(Phase4ExactSmallSelectionV1{
        .net = selection.net,
        .status = selection.status,
        .candidate_id = selection.candidate_id,
        .candidate_payload_checksum = selection.candidate_payload_checksum,
        .intrinsic_cost = intrinsic_cost});
  }
  artifact.production_outcome = semantics.outcome;
  artifact.artifact_checksum = ComputePhase4ExactSmallSnapshotArtifactChecksumV1(artifact);
  artifact.source_envelope_checksum = ComputePhase4ExactSmallSnapshotSourceEnvelopeChecksumV1(
      artifact.source_commit, artifact.source_stamped, artifact.source_tree_dirty,
      artifact.artifact_checksum);
  if (auto error = ValidatePhase4ExactSmallSnapshotArtifactForAuthority(authority, artifact,
                                                                        imported_fixture);
      std::holds_alternative<Phase4ExactSmallSnapshotError>(error)) {
    return std::get<Phase4ExactSmallSnapshotError>(std::move(error));
  }
  return artifact;
} catch (const std::bad_alloc&) {
  return Error(Phase4ExactSmallSnapshotErrorCode::kArithmeticOverflow, "P4EXACT-SNAPSHOT-HOST-001",
               "host allocation failed while building snapshot");
} catch (const std::exception&) {
  return Error(Phase4ExactSmallSnapshotErrorCode::kCandidateInvariant, "P4EXACT-SNAPSHOT-HOST-002",
               "an unexpected exception escaped snapshot build");
}

}  // namespace

Phase4ExactSmallSnapshotArtifactResultV1 BuildPhase4ExactSmallSnapshotArtifactV1(
    const Phase4CanonicalCellConfig& config, std::string_view source_commit, bool source_stamped,
    bool source_tree_dirty, std::uint64_t raw_cell_plan_checksum,
    std::uint64_t raw_cell_artifact_checksum, std::uint64_t raw_source_envelope_checksum,
    Phase4PerNetReportRawReferenceV1 raw_reference, std::uint64_t per_net_report_artifact_checksum,
    std::uint64_t per_net_report_source_envelope_checksum,
    Phase4CandidatePoolSnapshotExecutionV1 capture, std::string_view imported_fixture,
    std::uint32_t raw_evidence_schema_version) {
  return BuildPhase4ExactSmallSnapshotArtifactForAuthority(
      Phase4RepresentativeCorpusAuthority::kV1, config, source_commit, source_stamped,
      source_tree_dirty, raw_cell_plan_checksum, raw_cell_artifact_checksum,
      raw_source_envelope_checksum, raw_reference, per_net_report_artifact_checksum,
      per_net_report_source_envelope_checksum, std::move(capture), imported_fixture,
      raw_evidence_schema_version);
}

Phase4ExactSmallSnapshotArtifactResultV1 BuildPhase4ExactSmallSnapshotArtifactForCorpusV2(
    const Phase4CanonicalCellConfig& config, std::string_view source_commit, bool source_stamped,
    bool source_tree_dirty, std::uint64_t raw_cell_plan_checksum,
    std::uint64_t raw_cell_artifact_checksum, std::uint64_t raw_source_envelope_checksum,
    Phase4PerNetReportRawReferenceV1 raw_reference, std::uint64_t per_net_report_artifact_checksum,
    std::uint64_t per_net_report_source_envelope_checksum,
    Phase4CandidatePoolSnapshotExecutionV1 capture, std::string_view imported_fixture,
    std::uint32_t raw_evidence_schema_version) {
  return BuildPhase4ExactSmallSnapshotArtifactForAuthority(
      Phase4RepresentativeCorpusAuthority::kV2, config, source_commit, source_stamped,
      source_tree_dirty, raw_cell_plan_checksum, raw_cell_artifact_checksum,
      raw_source_envelope_checksum, raw_reference, per_net_report_artifact_checksum,
      per_net_report_source_envelope_checksum, std::move(capture), imported_fixture,
      raw_evidence_schema_version);
}

namespace {

std::variant<std::monostate, Phase4ExactSmallSnapshotError>
ValidatePhase4ExactSmallSnapshotArtifactForAuthority(
    Phase4RepresentativeCorpusAuthority authority,
    const Phase4ExactSmallSnapshotArtifactV1& artifact, std::string_view imported_fixture) {
  if (artifact.schema_version != kPhase4ExactSmallSnapshotSchemaVersion) {
    return Error(Phase4ExactSmallSnapshotErrorCode::kUnsupportedSchema,
                 "P4EXACT-SNAPSHOT-SCHEMA-001", "unsupported exact-small snapshot schema");
  }
  const Phase4CaseDescriptor* descriptor =
      FindPhase4CaseDescriptorForAuthority(authority, artifact.config.case_id);
  if (descriptor == nullptr || descriptor->role != Phase4CaseRole::kExactOracle ||
      !IsExactSmallCaseForAuthority(authority, artifact.config.case_id)) {
    return Error(Phase4ExactSmallSnapshotErrorCode::kUnsupportedCase, "P4EXACT-SNAPSHOT-CASE-003",
                 "snapshot case is not a canonical exact case");
  }
  if (artifact.decision_eligible || artifact.execution_order != Phase4TrialOrder::kBaselineFirst) {
    return Error(Phase4ExactSmallSnapshotErrorCode::kAuthorityAssociation,
                 "P4EXACT-SNAPSHOT-DIAGNOSTIC-001",
                 "an exact-small snapshot must remain a baseline-first diagnostic rerun");
  }
  if (artifact.maximum_serialized_bytes != kPhase4ExactSmallMaximumSerializedBytesV1) {
    return Error(Phase4ExactSmallSnapshotErrorCode::kArtifactBoundExceeded,
                 "P4EXACT-SNAPSHOT-OUTPUT-BOUND-001",
                 "the v1 serialized-output cap must equal the frozen exact-small bound",
                 artifact.maximum_serialized_bytes, kPhase4ExactSmallMaximumSerializedBytesV1);
  }
  if (artifact.pools.size() != kPhase4ExactSmallPoolCountV1 ||
      artifact.production_selections.size() != kPhase4ExactSmallPoolCountV1 ||
      artifact.workload_roster.size() != kPhase4ExactSmallPoolCountV1) {
    return Error(Phase4ExactSmallSnapshotErrorCode::kRosterMismatch, "P4EXACT-SNAPSHOT-ROSTER-001",
                 "snapshot must contain exactly six explicit pools, selections, and roster rows");
  }
  std::array<std::uint64_t, kPhase4ExactSmallPoolCountV1> pool_sizes{};
  for (std::size_t index = 0; index < artifact.pools.size(); ++index) {
    pool_sizes[index] = artifact.pools[index].candidates.size();
  }
  auto product_result = PreflightPhase4ExactSmallCartesianProductV1(
      pool_sizes, descriptor->maximum_exact_candidate_products);
  if (const auto* error = std::get_if<Phase4ExactSmallSnapshotError>(&product_result);
      error != nullptr) {
    return *error;
  }
  if (artifact.cartesian_product != std::get<std::uint64_t>(product_result)) {
    return Error(Phase4ExactSmallSnapshotErrorCode::kChecksumMismatch,
                 "P4EXACT-SNAPSHOT-PRODUCT-003", "serialized Cartesian product is inconsistent");
  }
  if (artifact.capacity_overrides.size() > kPhase4ExactSmallMaximumCapacityOverridesV1) {
    return Error(Phase4ExactSmallSnapshotErrorCode::kArtifactBoundExceeded,
                 "P4EXACT-SNAPSHOT-CAPACITY-BOUND-001",
                 "capacity override rows exceed the frozen exact-small bound",
                 artifact.capacity_overrides.size(), kPhase4ExactSmallMaximumCapacityOverridesV1);
  }
  ComponentTotals artifact_totals;
  for (const auto& pool : artifact.pools) {
    if (pool.candidates.size() > kPhase4ExactSmallMaximumCandidatesPerPoolV1) {
      return Error(Phase4ExactSmallSnapshotErrorCode::kArtifactBoundExceeded,
                   "P4EXACT-SNAPSHOT-POOL-BOUND-002",
                   "a serialized pool exceeds the canonical pool-plus-epochs candidate cap",
                   pool.candidates.size(), kPhase4ExactSmallMaximumCandidatesPerPoolV1);
    }
    for (const auto& candidate : pool.candidates) {
      AddCandidateShape(&artifact_totals, candidate);
    }
  }
  if (auto bound = ValidateComponentTotals(artifact_totals); bound.has_value()) return *bound;
  for (const auto& pool : artifact.pools) {
    for (const auto& candidate : pool.candidates) {
      if (auto bound = AddExpandedResourceEdges(&artifact_totals, candidate.resource_spans);
          bound.has_value()) {
        return *bound;
      }
    }
  }
  if (ComputePoolsManifest(artifact.pools) != artifact.final_pool_manifest_checksum) {
    return Error(Phase4ExactSmallSnapshotErrorCode::kChecksumMismatch,
                 "P4EXACT-SNAPSHOT-POOL-MANIFEST-001",
                 "the serialized complete final pools do not reproduce the session manifest");
  }
  Phase4RepresentativeCaseResult case_result = BuildPhase4RepresentativeCaseForAuthority(
      authority, artifact.config.case_id, imported_fixture, artifact.config.corpus_limits);
  if (!std::holds_alternative<Phase4RepresentativeCase>(case_result)) {
    return Error(Phase4ExactSmallSnapshotErrorCode::kInvalidConfiguration,
                 "P4EXACT-SNAPSHOT-CASE-004", "snapshot representative case cannot be rebuilt");
  }
  const Phase4RepresentativeCase& representative_case =
      std::get<Phase4RepresentativeCase>(case_result);
  Phase4CanonicalSpecResult spec_result = BuildExactSmallCanonicalSpec(authority, artifact.config);
  if (!std::holds_alternative<Phase4PairedTrialSpec>(spec_result)) {
    return Error(Phase4ExactSmallSnapshotErrorCode::kInvalidConfiguration,
                 "P4EXACT-SNAPSHOT-CONFIG-003", "snapshot canonical cell is invalid");
  }
  const Phase4PairedTrialSpec& spec = std::get<Phase4PairedTrialSpec>(spec_result);
  const Phase4TrialArmSemantics& semantics = artifact.candidate_semantics;
  if (internal::ValidatePhase4TrialArmSemanticsForAuthorityV1(authority, semantics).has_value() ||
      semantics.semantic_checksum != internal::ComputePhase4TrialArmSemanticChecksumV1(semantics) ||
      semantics.arm != Phase4TrialArm::kReusableCandidateAllocation ||
      semantics.execution_order != artifact.execution_order ||
      semantics.corpus_checksum != artifact.corpus_checksum ||
      semantics.case_id != artifact.config.case_id ||
      semantics.descriptor_fingerprint != artifact.descriptor_fingerprint ||
      semantics.case_checksum != artifact.case_checksum ||
      semantics.board_content_hash != artifact.board_content_hash ||
      semantics.workload_checksum != artifact.workload_checksum ||
      semantics.capacity_model_checksum != artifact.capacity_model_checksum ||
      semantics.budget_checksum != artifact.budget_checksum ||
      semantics.workload_net_count != artifact.workload_roster.size() ||
      semantics.requested_pool_size != artifact.config.requested_pool_size ||
      semantics.repetition_index != 0 || semantics.root_seed != artifact.root_seed ||
      semantics.preparation_worker_count != artifact.config.preparation_worker_count ||
      semantics.baseline_sweeps != spec.baseline_config.maximum_sweeps ||
      semantics.candidate_regeneration_epochs !=
          spec.candidate_session_config.maximum_regeneration_epochs ||
      semantics.candidate_columns_per_epoch !=
          spec.candidate_session_config.regeneration_plan_config.maximum_total_columns ||
      semantics.candidate_terminal_selection_rounds !=
          spec.candidate_session_config.schedules.back().maximum_selection_rounds ||
      !(semantics.external_budget == artifact.config.external_budget) ||
      semantics.semantic_checksum != artifact.candidate_semantic_checksum ||
      semantics.algorithm_session_checksum != artifact.candidate_session_checksum ||
      semantics.final_pool_manifest_checksum != artifact.final_pool_manifest_checksum ||
      semantics.final_rejection_manifest_checksum != artifact.final_rejection_manifest_checksum ||
      semantics.candidate_outcome_source != artifact.production_outcome_source ||
      !(semantics.outcome == artifact.production_outcome) ||
      semantics.final_candidate_count != artifact_totals.candidate_rows) {
    return Error(Phase4ExactSmallSnapshotErrorCode::kAuthorityAssociation,
                 "P4EXACT-SNAPSHOT-SEMANTIC-PROJECTION-001",
                 "complete candidate semantics are invalid or disagree with a projected field");
  }
  if (artifact.pools.size() != representative_case.workload.nets().size()) {
    return Error(Phase4ExactSmallSnapshotErrorCode::kRosterMismatch, "P4EXACT-SNAPSHOT-ROSTER-003",
                 "snapshot pool roster differs from the rebuilt representative workload");
  }
  std::set<candidates::CandidateId> candidate_ids;
  std::map<routing::EdgeResourceKey, std::uint64_t> selected_usage;
  std::uint64_t selected_count = 0;
  std::uint64_t no_candidate_count = 0;
  Wide selected_intrinsic_cost = 0;
  for (std::size_t index = 0; index < artifact.pools.size(); ++index) {
    const board_ir::EntityRef expected = representative_case.workload.nets()[index].request.net;
    if (!(artifact.workload_roster[index] == expected) ||
        !(artifact.pools[index].net == expected) ||
        !(artifact.production_selections[index].net == expected) ||
        (index != 0 && !EntityBefore(artifact.pools[index - 1].net, artifact.pools[index].net))) {
      return Error(Phase4ExactSmallSnapshotErrorCode::kRosterMismatch,
                   "P4EXACT-SNAPSHOT-ROSTER-002",
                   "pool and production-selection rosters are not canonical full EntityRefs");
    }
    const auto& selection = artifact.production_selections[index];
    for (std::size_t candidate_index = 0; candidate_index < artifact.pools[index].candidates.size();
         ++candidate_index) {
      const auto& candidate = artifact.pools[index].candidates[candidate_index];
      if (!(candidate.net == expected) || candidate.id.empty() || candidate.payload_checksum == 0 ||
          candidate.logical_bytes == 0 ||
          (candidate_index != 0 &&
           !(artifact.pools[index].candidates[candidate_index - 1].id < candidate.id)) ||
          !candidate_ids.insert(candidate.id).second) {
        return Error(Phase4ExactSmallSnapshotErrorCode::kCandidateInvariant,
                     "P4EXACT-SNAPSHOT-CANDIDATE-002",
                     "serialized candidate identity or association is invalid");
      }
      candidates::GeneratedRouteCandidate reconstructed;
      reconstructed.schema_major = candidate.schema_major;
      reconstructed.schema_minor = candidate.schema_minor;
      reconstructed.id = candidate.id;
      reconstructed.net = candidate.net;
      reconstructed.intended_terminals = candidate.intended_terminals;
      reconstructed.associations = candidate.associations;
      reconstructed.geometry_schema_version = candidate.geometry_schema_version;
      reconstructed.resource_schema_version = candidate.resource_schema_version;
      reconstructed.policy = candidate.policy;
      reconstructed.policy_identity = candidate.policy_identity;
      reconstructed.provenance = candidate.provenance;
      reconstructed.geometry = candidate.geometry;
      reconstructed.resources = candidate.resource_spans;
      reconstructed.metrics = candidate.metrics;
      reconstructed.constraints = candidate.constraints;
      reconstructed.geometry_signature = candidate.geometry_signature;
      reconstructed.resource_signature = candidate.resource_signature;
      reconstructed.payload_checksum = candidate.payload_checksum;
      reconstructed.logical_bytes = candidate.logical_bytes;
      routing::PlanarRouteRequest candidate_request =
          representative_case.workload.nets()[index].request;
      candidate_request.candidate_policy = reconstructed.policy;
      const candidates::CandidateAdmissionContext admission_context{
          .board = representative_case.board,
          .compiled_board = representative_case.workload.nets()[index].compiled_board,
          .request = candidate_request,
      };
      if (std::holds_alternative<candidates::CandidateRejection>(
              candidates::internal::ValidateCandidatePayloadWithoutProducerEvidence(
                  admission_context, reconstructed)) ||
          candidate.intrinsic_cost != candidate.metrics.intrinsic_base_cost) {
        return Error(
            Phase4ExactSmallSnapshotErrorCode::kCandidateInvariant, "P4EXACT-SNAPSHOT-PAYLOAD-001",
            "candidate full payload fails exact non-auth admission replay or intrinsic-base cost");
      }
      std::set<routing::EdgeResourceKey> candidate_edges;
      for (std::size_t span_index = 0; span_index < candidate.resource_spans.size(); ++span_index) {
        const auto& span = candidate.resource_spans[span_index];
        const std::uint8_t direction = static_cast<std::uint8_t>(span.direction);
        if (direction > static_cast<std::uint8_t>(geometry_compiler::Direction::kNorthWest) ||
            span.edge_count == 0 || span.usage_units != 1 ||
            (span_index != 0 && !(candidate.resource_spans[span_index - 1] < span))) {
          return Error(Phase4ExactSmallSnapshotErrorCode::kCandidateInvariant,
                       "P4EXACT-SNAPSHOT-SPAN-001",
                       "candidate resource spans use an invalid direction, usage, or ordering");
        }
        const geometry_compiler::DirectionDelta storage_delta =
            candidates::ResourceSpanStorageDelta(span.direction);
        const geometry_compiler::DirectionDelta movement_delta =
            geometry_compiler::DeltaFor(span.direction);
        const SignedWide last_x = static_cast<SignedWide>(span.lattice_x) +
                                  static_cast<SignedWide>(storage_delta.x) *
                                      static_cast<SignedWide>(span.edge_count - 1U);
        const SignedWide last_y = static_cast<SignedWide>(span.lattice_y) +
                                  static_cast<SignedWide>(storage_delta.y) *
                                      static_cast<SignedWide>(span.edge_count - 1U);
        if (!FitsI64(last_x) || !FitsI64(last_y) ||
            !FitsI64(static_cast<SignedWide>(span.lattice_x) + movement_delta.x) ||
            !FitsI64(static_cast<SignedWide>(span.lattice_y) + movement_delta.y) ||
            !FitsI64(last_x + movement_delta.x) || !FitsI64(last_y + movement_delta.y)) {
          return Error(Phase4ExactSmallSnapshotErrorCode::kCandidateInvariant,
                       "P4EXACT-SNAPSHOT-SPAN-ENDPOINT-001",
                       "a compressed resource span source or physical endpoint exceeds int64");
        }
        for (std::uint32_t offset = 0; offset < span.edge_count; ++offset) {
          const SignedWide x = static_cast<SignedWide>(span.lattice_x) +
                               static_cast<SignedWide>(storage_delta.x) * offset;
          const SignedWide y = static_cast<SignedWide>(span.lattice_y) +
                               static_cast<SignedWide>(storage_delta.y) * offset;
          if (!FitsI64(x) || !FitsI64(y)) {
            return Error(Phase4ExactSmallSnapshotErrorCode::kCandidateInvariant,
                         "P4EXACT-SNAPSHOT-SPAN-ENDPOINT-002",
                         "an expanded resource-span source exceeds int64");
          }
          const routing::EdgeResourceKey resource{
              .layer = span.layer,
              .lattice_x = static_cast<std::int64_t>(x),
              .lattice_y = static_cast<std::int64_t>(y),
              .direction = span.direction,
          };
          if (!candidate_edges.insert(resource).second) {
            return Error(Phase4ExactSmallSnapshotErrorCode::kCandidateInvariant,
                         "P4EXACT-SNAPSHOT-SPAN-OVERLAP-001",
                         "one candidate footprint repeats an atomic resource edge");
          }
          if (selection.candidate_id.has_value() && *selection.candidate_id == candidate.id) {
            std::uint64_t& usage = selected_usage[resource];
            if (usage == std::numeric_limits<std::uint64_t>::max()) {
              return Error(Phase4ExactSmallSnapshotErrorCode::kArithmeticOverflow,
                           "P4EXACT-SNAPSHOT-USAGE-OVERFLOW-001",
                           "selected atomic resource usage exceeds uint64");
            }
            ++usage;
          }
        }
      }
    }
    if (selection.status == allocator::NetSelectionStatus::kNoAdmissibleCandidate) {
      if (!artifact.pools[index].candidates.empty() || selection.candidate_id.has_value() ||
          selection.candidate_payload_checksum.has_value() || selection.intrinsic_cost != 0) {
        return Error(Phase4ExactSmallSnapshotErrorCode::kRosterMismatch,
                     "P4EXACT-SNAPSHOT-SELECTION-001",
                     "a no-candidate selection must correspond to an explicit empty pool");
      }
      ++no_candidate_count;
    } else if (selection.status == allocator::NetSelectionStatus::kSelected &&
               selection.candidate_id.has_value() &&
               selection.candidate_payload_checksum.has_value()) {
      const auto found =
          std::ranges::find(artifact.pools[index].candidates, *selection.candidate_id,
                            &Phase4ExactSmallCandidateV1::id);
      if (found == artifact.pools[index].candidates.end() ||
          found->payload_checksum != *selection.candidate_payload_checksum ||
          found->intrinsic_cost != selection.intrinsic_cost) {
        return Error(Phase4ExactSmallSnapshotErrorCode::kRosterMismatch,
                     "P4EXACT-SNAPSHOT-SELECTION-002",
                     "the production selection is not a full-identity member of its final pool");
      }
      ++selected_count;
      selected_intrinsic_cost += selection.intrinsic_cost;
    } else {
      return Error(Phase4ExactSmallSnapshotErrorCode::kRosterMismatch,
                   "P4EXACT-SNAPSHOT-SELECTION-003",
                   "the production selection status or optional identity is malformed");
    }
  }
  if (selected_intrinsic_cost > std::numeric_limits<std::uint64_t>::max() ||
      artifact.production_outcome.selected_net_count != selected_count ||
      artifact.production_outcome.no_candidate_net_count != no_candidate_count ||
      artifact.production_outcome.total_intrinsic_cost !=
          static_cast<std::uint64_t>(selected_intrinsic_cost)) {
    return Error(Phase4ExactSmallSnapshotErrorCode::kRosterMismatch, "P4EXACT-SNAPSHOT-OUTCOME-001",
                 "production selection totals do not reproduce the board outcome");
  }
  if (artifact.default_capacity_units > 1U ||
      std::ranges::any_of(artifact.capacity_overrides,
                          [](const auto& override) { return override.capacity_units > 1U; })) {
    return Error(Phase4ExactSmallSnapshotErrorCode::kCandidateInvariant,
                 "P4EXACT-SNAPSHOT-CAPACITY-BINARY-001",
                 "the exact-small capacity vocabulary must remain binary");
  }
  for (std::size_t index = 1; index < artifact.capacity_overrides.size(); ++index) {
    if (!(artifact.capacity_overrides[index - 1].resource <
          artifact.capacity_overrides[index].resource)) {
      return Error(Phase4ExactSmallSnapshotErrorCode::kCandidateInvariant,
                   "P4EXACT-SNAPSHOT-CAPACITY-002",
                   "capacity overrides are not in strict canonical resource order");
    }
  }
  std::uint64_t overused_resource_count = 0;
  Wide total_overuse_units = 0;
  for (const auto& [resource, usage] : selected_usage) {
    const auto override = std::ranges::lower_bound(artifact.capacity_overrides, resource, {},
                                                   &allocator::ResourceCapacityOverride::resource);
    const std::uint64_t capacity =
        override != artifact.capacity_overrides.end() && override->resource == resource
            ? override->capacity_units
            : artifact.default_capacity_units;
    if (usage > capacity) {
      ++overused_resource_count;
      total_overuse_units += static_cast<Wide>(usage - capacity);
    }
  }
  if (total_overuse_units > std::numeric_limits<std::uint64_t>::max() ||
      artifact.production_outcome.overused_resource_count != overused_resource_count ||
      artifact.production_outcome.total_overuse_units !=
          static_cast<std::uint64_t>(total_overuse_units)) {
    return Error(Phase4ExactSmallSnapshotErrorCode::kRosterMismatch, "P4EXACT-SNAPSHOT-OVERUSE-001",
                 "selected candidate footprints and capacities do not reproduce overuse totals");
  }
  const std::uint64_t rebuilt_capacity_checksum =
      allocator::internal::ComputeResourceCapacityModelChecksumV1(
          allocator::internal::ResourceCapacityChecksumHeaderV1{
              .schema_version = artifact.capacity_schema_version,
              .associations = artifact.capacity_associations,
              .default_capacity_units = artifact.default_capacity_units},
          artifact.capacity_overrides);
  if (artifact.capacity_model_checksum != rebuilt_capacity_checksum ||
      artifact.capacity_schema_version != representative_case.capacities.schema_version() ||
      !(artifact.capacity_associations == representative_case.capacities.associations()) ||
      artifact.default_capacity_units != representative_case.capacities.default_capacity_units() ||
      artifact.capacity_overrides != representative_case.capacities.overrides() ||
      artifact.root_seed != spec.root_seed ||
      artifact.budget_checksum !=
          internal::ComputePhase4PairedBudgetChecksumForAuthorityV1(
              authority, spec,
              Phase4RouteOpportunity{
                  .route_queries = spec.baseline_config.limits.maximum_route_queries,
                  .route_work_units = spec.baseline_config.limits.maximum_total_route_work_units},
              descriptor->requested_net_count,
              spec.candidate_session_config.regeneration_plan_config.maximum_total_columns,
              spec.candidate_session_config.schedules.back().maximum_selection_rounds) ||
      artifact.corpus_checksum != Phase4RepresentativeCorpusChecksumForAuthority(authority) ||
      artifact.descriptor_fingerprint !=
          FingerprintPhase4CaseDescriptorForAuthority(authority, *descriptor) ||
      artifact.case_checksum != representative_case.case_checksum ||
      artifact.board_content_hash != representative_case.board.content_hash() ||
      artifact.workload_checksum != representative_case.workload.workload_checksum()) {
    return Error(Phase4ExactSmallSnapshotErrorCode::kAuthorityAssociation,
                 "P4EXACT-SNAPSHOT-IDENTITY-001",
                 "snapshot cell, case, workload, capacity, budget, or seed identity is invalid");
  }
  const bool raw_v1_source_envelope =
      artifact.raw_source_envelope_checksum ==
      ComputePhase4SourceEnvelopeChecksumV1(kPhase4TrialWireSchemaVersion, artifact.source_commit,
                                            artifact.source_stamped, artifact.source_tree_dirty,
                                            artifact.raw_cell_artifact_checksum);
  const bool raw_v2_source_envelope =
      artifact.raw_source_envelope_checksum ==
      ComputePhase4SourceEnvelopeChecksumV2(
          kPhase4SameRunRawEvidenceSchemaVersion, kPhase4SameRunTrialWireSchemaVersion,
          artifact.source_commit, artifact.source_stamped, artifact.source_tree_dirty,
          artifact.raw_cell_artifact_checksum);
  const bool raw_source_envelope_valid = authority == Phase4RepresentativeCorpusAuthority::kV2
                                             ? raw_v2_source_envelope
                                             : (raw_v1_source_envelope || raw_v2_source_envelope);
  if (!IsLowerHexCommit(artifact.source_commit) || !artifact.source_stamped ||
      artifact.source_tree_dirty || !NonzeroRawReference(artifact.raw_reference) ||
      artifact.raw_reference.candidate_semantic_checksum != artifact.candidate_semantic_checksum ||
      artifact.raw_cell_artifact_checksum == 0 || artifact.per_net_report_artifact_checksum == 0 ||
      artifact.per_net_candidate_telemetry_checksum == 0 ||
      artifact.candidate_semantic_checksum == 0 || artifact.candidate_session_checksum == 0 ||
      artifact.final_pool_manifest_checksum == 0 ||
      artifact.final_rejection_manifest_checksum == 0 ||
      (artifact.production_outcome_source != Phase4CandidateOutcomeSource::kPreferredMultiWorld &&
       artifact.production_outcome_source !=
           Phase4CandidateOutcomeSource::kCommonLineageOneWorld) ||
      artifact.production_outcome.world_checksum == 0 ||
      artifact.raw_cell_plan_checksum !=
          ComputeExactSmallCellPlanChecksum(authority, artifact.config) ||
      !raw_source_envelope_valid ||
      artifact.per_net_report_source_envelope_checksum !=
          ComputePhase4PerNetReportSourceEnvelopeChecksumV1(
              artifact.source_commit, artifact.source_stamped, artifact.source_tree_dirty,
              artifact.per_net_report_artifact_checksum) ||
      artifact.workload_net_roster_checksum !=
          ComputeExactSmallWorkloadRosterChecksum(authority, representative_case)) {
    return Error(Phase4ExactSmallSnapshotErrorCode::kAuthorityAssociation,
                 "P4EXACT-SNAPSHOT-AUTHORITY-002",
                 "snapshot local authority projections or claimed external associations are "
                 "invalid");
  }
  if (artifact.artifact_checksum == 0 ||
      artifact.artifact_checksum != ComputePhase4ExactSmallSnapshotArtifactChecksumV1(artifact) ||
      artifact.source_envelope_checksum != ComputePhase4ExactSmallSnapshotSourceEnvelopeChecksumV1(
                                               artifact.source_commit, artifact.source_stamped,
                                               artifact.source_tree_dirty,
                                               artifact.artifact_checksum)) {
    return Error(Phase4ExactSmallSnapshotErrorCode::kChecksumMismatch,
                 "P4EXACT-SNAPSHOT-CHECKSUM-001", "snapshot checksum or envelope is invalid");
  }
  return std::monostate{};
}

}  // namespace

std::variant<std::monostate, Phase4ExactSmallSnapshotError>
ValidatePhase4ExactSmallSnapshotArtifactV1(const Phase4ExactSmallSnapshotArtifactV1& artifact,
                                           std::string_view imported_fixture) {
  return ValidatePhase4ExactSmallSnapshotArtifactForAuthority(
      Phase4RepresentativeCorpusAuthority::kV1, artifact, imported_fixture);
}

std::variant<std::monostate, Phase4ExactSmallSnapshotError>
ValidatePhase4ExactSmallSnapshotArtifactForCorpusV2(
    const Phase4ExactSmallSnapshotArtifactV1& artifact, std::string_view imported_fixture) {
  return ValidatePhase4ExactSmallSnapshotArtifactForAuthority(
      Phase4RepresentativeCorpusAuthority::kV2, artifact, imported_fixture);
}

Phase4ExactSmallSnapshotSerializationResultV1 SerializePhase4ExactSmallSnapshotArtifactJsonV1(
    const Phase4ExactSmallSnapshotArtifactV1& value) try {
  JsonWriter writer(static_cast<std::size_t>(
      std::min(value.maximum_serialized_bytes, kPhase4ExactSmallMaximumSerializedBytesV1)));
  writer.BeginObject();
  writer.Key("source_commit");
  writer.String(value.source_commit);
  writer.Key("source_stamped");
  writer.Bool(value.source_stamped);
  writer.Key("source_tree_dirty");
  writer.Bool(value.source_tree_dirty);
  writer.Key("source_envelope_checksum");
  writer.U64(value.source_envelope_checksum);
  writer.Key("schema_version");
  writer.U64(value.schema_version);
  writer.Key("decision_eligible");
  writer.Bool(value.decision_eligible);
  writer.Key("config");
  WriteCell(&writer, value.config);
  writer.Key("execution_order");
  writer.U64(static_cast<std::uint8_t>(value.execution_order));
  writer.Key("root_seed");
  writer.U64(value.root_seed);
  writer.Key("corpus_checksum");
  writer.U64(value.corpus_checksum);
  writer.Key("descriptor_fingerprint");
  writer.U64(value.descriptor_fingerprint);
  writer.Key("case_checksum");
  writer.U64(value.case_checksum);
  writer.Key("board_content_hash");
  writer.U64(value.board_content_hash);
  writer.Key("workload_checksum");
  writer.U64(value.workload_checksum);
  writer.Key("workload_roster");
  writer.BeginArray();
  for (const auto net : value.workload_roster) {
    writer.Element();
    WriteEntity(&writer, net);
  }
  writer.EndArray();
  writer.Key("workload_net_roster_checksum");
  writer.U64(value.workload_net_roster_checksum);
  writer.Key("budget_checksum");
  writer.U64(value.budget_checksum);
  writer.Key("raw_cell_plan_checksum");
  writer.U64(value.raw_cell_plan_checksum);
  writer.Key("raw_cell_artifact_checksum");
  writer.U64(value.raw_cell_artifact_checksum);
  writer.Key("raw_source_envelope_checksum");
  writer.U64(value.raw_source_envelope_checksum);
  writer.Key("raw_reference");
  writer.BeginObject();
  writer.Key("repetition_index");
  writer.U64(value.raw_reference.repetition_index);
  writer.Key("execution_order");
  writer.U64(static_cast<std::uint8_t>(value.raw_reference.execution_order));
  writer.Key("pair_attempt_checksum");
  writer.U64(value.raw_reference.pair_attempt_checksum);
  writer.Key("paired_semantic_checksum");
  writer.U64(value.raw_reference.paired_semantic_checksum);
  writer.Key("paired_artifact_checksum");
  writer.U64(value.raw_reference.paired_artifact_checksum);
  writer.Key("baseline_semantic_checksum");
  writer.U64(value.raw_reference.baseline_semantic_checksum);
  writer.Key("baseline_arm_artifact_checksum");
  writer.U64(value.raw_reference.baseline_arm_artifact_checksum);
  writer.Key("candidate_semantic_checksum");
  writer.U64(value.raw_reference.candidate_semantic_checksum);
  writer.Key("candidate_arm_artifact_checksum");
  writer.U64(value.raw_reference.candidate_arm_artifact_checksum);
  writer.EndObject();
  writer.Key("per_net_report_artifact_checksum");
  writer.U64(value.per_net_report_artifact_checksum);
  writer.Key("per_net_report_source_envelope_checksum");
  writer.U64(value.per_net_report_source_envelope_checksum);
  writer.Key("per_net_candidate_telemetry_checksum");
  writer.U64(value.per_net_candidate_telemetry_checksum);
  writer.Key("candidate_semantics");
  WriteSemantics(&writer, value.candidate_semantics);
  writer.Key("candidate_semantic_checksum");
  writer.U64(value.candidate_semantic_checksum);
  writer.Key("candidate_session_checksum");
  writer.U64(value.candidate_session_checksum);
  writer.Key("final_pool_manifest_checksum");
  writer.U64(value.final_pool_manifest_checksum);
  writer.Key("final_rejection_manifest_checksum");
  writer.U64(value.final_rejection_manifest_checksum);
  writer.Key("production_outcome_source");
  writer.U64(static_cast<std::uint8_t>(value.production_outcome_source));
  writer.Key("capacity");
  writer.BeginObject();
  writer.Key("schema_version");
  writer.U64(value.capacity_schema_version);
  writer.Key("associations");
  writer.BeginObject();
  writer.Key("board_content_hash");
  writer.U64(value.capacity_associations.board_content_hash);
  writer.Key("compiler_profile_fingerprint");
  writer.U64(value.capacity_associations.compiler_profile_fingerprint);
  writer.Key("geometry_compiler_version");
  writer.U64(value.capacity_associations.geometry_compiler_version);
  writer.EndObject();
  writer.Key("default_capacity_units");
  writer.U64(value.default_capacity_units);
  writer.Key("overrides");
  writer.BeginArray();
  for (const auto& override : value.capacity_overrides) {
    writer.Element();
    writer.BeginObject();
    writer.Key("resource");
    WriteResource(&writer, override.resource);
    writer.Key("capacity_units");
    writer.U64(override.capacity_units);
    writer.EndObject();
  }
  writer.EndArray();
  writer.EndObject();
  writer.Key("capacity_model_checksum");
  writer.U64(value.capacity_model_checksum);
  writer.Key("cartesian_product");
  writer.U64(value.cartesian_product);
  writer.Key("pools");
  writer.BeginArray();
  for (const auto& pool : value.pools) {
    writer.Element();
    writer.BeginObject();
    writer.Key("net");
    WriteEntity(&writer, pool.net);
    writer.Key("candidates");
    writer.BeginArray();
    for (const auto& candidate : pool.candidates) {
      writer.Element();
      writer.BeginObject();
      writer.Key("schema_major");
      writer.U64(candidate.schema_major);
      writer.Key("schema_minor");
      writer.U64(candidate.schema_minor);
      writer.Key("id");
      WriteId(&writer, candidate.id);
      writer.Key("net");
      WriteEntity(&writer, candidate.net);
      writer.Key("intended_terminals");
      writer.BeginArray();
      writer.Element();
      WriteEntity(&writer, candidate.intended_terminals[0]);
      writer.Element();
      WriteEntity(&writer, candidate.intended_terminals[1]);
      writer.EndArray();
      writer.Key("associations");
      writer.BeginObject();
      writer.Key("board_content_hash");
      writer.U64(candidate.associations.board_content_hash);
      writer.Key("compiler_profile_fingerprint");
      writer.U64(candidate.associations.compiler_profile_fingerprint);
      writer.Key("geometry_compiler_version");
      writer.U64(candidate.associations.geometry_compiler_version);
      writer.Key("routing_profile_fingerprint");
      writer.U64(candidate.associations.routing_profile_fingerprint);
      writer.Key("rule_bucket_identity");
      writer.U64(candidate.associations.rule_bucket_identity);
      writer.EndObject();
      writer.Key("geometry_schema_version");
      writer.U64(candidate.geometry_schema_version);
      writer.Key("resource_schema_version");
      writer.U64(candidate.resource_schema_version);
      writer.Key("policy");
      writer.BeginObject();
      writer.Key("schema_version");
      writer.U64(candidate.policy.schema_version);
      writer.Key("objective");
      writer.U64(static_cast<std::uint8_t>(candidate.policy.objective));
      writer.Key("deterministic_seed");
      writer.U64(candidate.policy.deterministic_seed);
      writer.Key("candidate_ordinal");
      writer.U64(candidate.policy.candidate_ordinal);
      writer.Key("orthogonal_step_surcharge");
      writer.U64(candidate.policy.orthogonal_step_surcharge);
      writer.Key("diagonal_step_surcharge");
      writer.U64(candidate.policy.diagonal_step_surcharge);
      writer.Key("bend_surcharge");
      writer.U64(candidate.policy.bend_surcharge);
      writer.Key("banned_resources");
      writer.BeginArray();
      for (const auto& resource : candidate.policy.banned_resources) {
        writer.Element();
        WriteResource(&writer, resource);
      }
      writer.EndArray();
      writer.Key("resource_penalties");
      writer.BeginArray();
      for (const auto& penalty : candidate.policy.resource_penalties) {
        writer.Element();
        writer.BeginObject();
        writer.Key("resource");
        WriteResource(&writer, penalty.resource);
        writer.Key("additional_cost");
        writer.U64(penalty.additional_cost);
        writer.EndObject();
      }
      writer.EndArray();
      writer.EndObject();
      writer.Key("policy_identity");
      writer.U64(candidate.policy_identity);
      writer.Key("provenance");
      writer.BeginObject();
      writer.Key("generator");
      writer.U64(static_cast<std::uint8_t>(candidate.provenance.generator));
      writer.Key("generator_version");
      writer.U64(candidate.provenance.generator_version);
      writer.Key("backend");
      writer.U64(static_cast<std::uint8_t>(candidate.provenance.backend));
      writer.Key("supported_device_class");
      writer.String(candidate.provenance.supported_device_class);
      writer.Key("deterministic_seed");
      writer.U64(candidate.provenance.deterministic_seed);
      writer.Key("batch_identity");
      writer.U64(candidate.provenance.batch_identity);
      writer.Key("query_identity");
      writer.U64(candidate.provenance.query_identity);
      writer.Key("candidate_ordinal");
      writer.U64(candidate.provenance.candidate_ordinal);
      writer.EndObject();
      writer.Key("geometry");
      writer.BeginArray();
      for (const auto& primitive : candidate.geometry) {
        writer.Element();
        writer.BeginObject();
        if (const auto* line = std::get_if<candidates::ExactLinePrimitive>(&primitive)) {
          writer.Key("kind");
          writer.String("line");
          writer.Key("layer");
          writer.U64(line->layer);
          writer.Key("start");
          writer.BeginObject();
          writer.Key("x");
          writer.I64(line->centerline.start.x);
          writer.Key("y");
          writer.I64(line->centerline.start.y);
          writer.EndObject();
          writer.Key("end");
          writer.BeginObject();
          writer.Key("x");
          writer.I64(line->centerline.end.x);
          writer.Key("y");
          writer.I64(line->centerline.end.y);
          writer.EndObject();
        } else {
          const auto& via = std::get<candidates::ThroughViaPrimitive>(primitive);
          writer.Key("kind");
          writer.String("through_via");
          writer.Key("template_id");
          writer.U64(via.template_id);
          writer.Key("position");
          writer.BeginObject();
          writer.Key("x");
          writer.I64(via.position.x);
          writer.Key("y");
          writer.I64(via.position.y);
          writer.EndObject();
          writer.Key("start_layer");
          writer.U64(via.start_layer);
          writer.Key("end_layer");
          writer.U64(via.end_layer);
        }
        writer.EndObject();
      }
      writer.EndArray();
      writer.Key("metrics");
      WriteMetrics(&writer, candidate.metrics);
      writer.Key("constraints");
      writer.BeginObject();
      writer.Key("supported_hard_constraints_satisfied");
      writer.Bool(candidate.constraints.supported_hard_constraints_satisfied);
      writer.Key("unsupported_rules_remain");
      writer.Bool(candidate.constraints.unsupported_rules_remain);
      writer.Key("connected_intended_terminal_count");
      writer.U64(candidate.constraints.connected_intended_terminal_count);
      writer.Key("exact_validation_code");
      writer.U64(static_cast<std::uint8_t>(candidate.constraints.exact_validation_code));
      writer.EndObject();
      writer.Key("geometry_signature");
      WriteId(&writer, candidate.geometry_signature);
      writer.Key("resource_signature");
      WriteId(&writer, candidate.resource_signature);
      writer.Key("payload_checksum");
      writer.U64(candidate.payload_checksum);
      writer.Key("logical_bytes");
      writer.U64(candidate.logical_bytes);
      writer.Key("intrinsic_cost");
      writer.U64(candidate.intrinsic_cost);
      writer.Key("resource_spans");
      writer.BeginArray();
      for (const auto& span : candidate.resource_spans) {
        writer.Element();
        writer.BeginObject();
        writer.Key("layer");
        writer.U64(span.layer);
        writer.Key("lattice_x");
        writer.I64(span.lattice_x);
        writer.Key("lattice_y");
        writer.I64(span.lattice_y);
        writer.Key("direction");
        writer.U64(static_cast<std::uint8_t>(span.direction));
        writer.Key("edge_count");
        writer.U64(span.edge_count);
        writer.Key("usage_units");
        writer.U64(span.usage_units);
        writer.EndObject();
      }
      writer.EndArray();
      writer.EndObject();
    }
    writer.EndArray();
    writer.EndObject();
  }
  writer.EndArray();
  writer.Key("production_selections");
  writer.BeginArray();
  for (const auto& selection : value.production_selections) {
    writer.Element();
    writer.BeginObject();
    writer.Key("net");
    WriteEntity(&writer, selection.net);
    writer.Key("status");
    writer.U64(static_cast<std::uint8_t>(selection.status));
    writer.Key("candidate_id");
    if (selection.candidate_id)
      WriteId(&writer, *selection.candidate_id);
    else
      writer.Null();
    writer.Key("candidate_payload_checksum");
    if (selection.candidate_payload_checksum)
      writer.U64(*selection.candidate_payload_checksum);
    else
      writer.Null();
    writer.Key("intrinsic_cost");
    writer.U64(selection.intrinsic_cost);
    writer.EndObject();
  }
  writer.EndArray();
  writer.Key("production_outcome");
  WriteOutcome(&writer, value.production_outcome);
  writer.Key("maximum_serialized_bytes");
  writer.U64(value.maximum_serialized_bytes);
  writer.Key("artifact_checksum");
  writer.U64(value.artifact_checksum);
  writer.EndObject();
  std::string output = writer.Finish();
  if (!writer.ok() || output.size() > kPhase4ExactSmallMaximumSerializedBytesV1) {
    return Error(Phase4ExactSmallSnapshotErrorCode::kArtifactBoundExceeded,
                 "P4EXACT-SNAPSHOT-OUTPUT-BOUND-002",
                 "canonical JSON exceeds the frozen exact-small serialized-output cap",
                 output.size(), kPhase4ExactSmallMaximumSerializedBytesV1);
  }
  return output;
} catch (const std::bad_alloc&) {
  return Error(Phase4ExactSmallSnapshotErrorCode::kArtifactBoundExceeded,
               "P4EXACT-SNAPSHOT-OUTPUT-BOUND-003",
               "host allocation failed while serializing bounded exact-small JSON");
} catch (const std::exception&) {
  return Error(Phase4ExactSmallSnapshotErrorCode::kCandidateInvariant,
               "P4EXACT-SNAPSHOT-SERIALIZE-001",
               "an unexpected exception escaped exact-small JSON serialization");
}

}  // namespace apgar::benchmark
