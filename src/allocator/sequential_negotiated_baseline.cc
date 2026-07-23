#include "apgar/allocator/sequential_negotiated_baseline.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <new>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

#include "apgar/board_ir/stable_hash.h"
#include "apgar/candidates/candidate_store.h"
#include "apgar/candidates/route_candidate.h"
#include "apgar/routing/candidate_policy.h"
#include "src/allocator/multi_net_workload_internal.h"
#include "src/allocator/negotiated_prices_internal.h"
#include "src/allocator/one_world_internal.h"
#include "src/allocator/sequential_negotiated_baseline_internal.h"
#include "src/operational_timestamp.h"

namespace apgar::allocator {
namespace {

using UWide = unsigned __int128;
using SWide = __int128;
using Occupancy = std::map<routing::EdgeResourceKey, std::uint64_t>;
using OperationalClock = std::chrono::steady_clock;

[[nodiscard]] std::uint64_t OperationalElapsed(OperationalClock::time_point start) noexcept {
  const auto elapsed =
      std::chrono::duration_cast<std::chrono::nanoseconds>(OperationalClock::now() - start).count();
  return elapsed <= 0 ? 0 : static_cast<std::uint64_t>(elapsed);
}

void AccumulateOperationalElapsed(std::uint64_t* target,
                                  OperationalClock::time_point start) noexcept {
  if (target == nullptr) {
    return;
  }
  const std::uint64_t value = OperationalElapsed(start);
  *target = value > std::numeric_limits<std::uint64_t>::max() - *target
                ? std::numeric_limits<std::uint64_t>::max()
                : *target + value;
}

#ifdef APGAR_SEQUENTIAL_NEGOTIATED_BASELINE_FAULT_TEST_VARIANT
std::atomic<std::uint64_t> g_admission_budget_fault_query{0};
#endif

[[nodiscard]] SequentialNegotiatedBaselineError Error(
    SequentialNegotiatedBaselineErrorCode code, std::string_view invariant_id,
    std::string_view detail, std::optional<board_ir::EntityRef> net = std::nullopt,
    std::uint64_t required = 0, std::uint64_t configured = 0) noexcept {
  return SequentialNegotiatedBaselineError{.code = code,
                                           .invariant_id = invariant_id,
                                           .detail = detail,
                                           .net = net,
                                           .required = required,
                                           .configured = configured};
}

[[nodiscard]] std::uint64_t NarrowWitness(UWide value) noexcept {
  return value > std::numeric_limits<std::uint64_t>::max()
             ? std::numeric_limits<std::uint64_t>::max()
             : static_cast<std::uint64_t>(value);
}

void AddEntity(board_ir::StableHashBuilder& hash, board_ir::EntityRef entity) noexcept {
  hash.AddU64(entity.id);
  hash.AddU32(entity.generation);
}

void AddU16(board_ir::StableHashBuilder& hash, std::uint16_t value) noexcept {
  hash.AddByte(static_cast<std::uint8_t>(value & 0xffU));
  hash.AddByte(static_cast<std::uint8_t>((value >> 8U) & 0xffU));
}

void AddResource(board_ir::StableHashBuilder& hash,
                 const routing::EdgeResourceKey& resource) noexcept {
  hash.AddU32(resource.layer);
  hash.AddI64(resource.lattice_x);
  hash.AddI64(resource.lattice_y);
  hash.AddByte(static_cast<std::uint8_t>(resource.direction));
}

void AddCandidateId(board_ir::StableHashBuilder& hash, const candidates::CandidateId& id) noexcept {
  hash.AddU64(id.high);
  hash.AddU64(id.low);
}

void AddAssociations(board_ir::StableHashBuilder& hash,
                     const candidates::CandidateAssociations& associations) noexcept {
  hash.AddU64(associations.board_content_hash);
  hash.AddU64(associations.compiler_profile_fingerprint);
  hash.AddU32(associations.geometry_compiler_version);
  hash.AddU64(associations.routing_profile_fingerprint);
  hash.AddU64(associations.rule_bucket_identity);
}

void AddProvenance(board_ir::StableHashBuilder& hash,
                   const candidates::CandidateProvenance& provenance) noexcept {
  hash.AddByte(static_cast<std::uint8_t>(provenance.generator));
  hash.AddU32(provenance.generator_version);
  hash.AddByte(static_cast<std::uint8_t>(provenance.backend));
  hash.AddString(provenance.supported_device_class);
  hash.AddU64(provenance.deterministic_seed);
  hash.AddU64(provenance.batch_identity);
  hash.AddU64(provenance.query_identity);
  hash.AddU32(provenance.candidate_ordinal);
}

void AddOptionalU64(board_ir::StableHashBuilder& hash,
                    std::optional<std::uint64_t> value) noexcept {
  hash.AddBool(value.has_value());
  if (value.has_value()) {
    hash.AddU64(*value);
  }
}

void AddRejection(board_ir::StableHashBuilder& hash,
                  const candidates::CandidateRejection& rejection) noexcept {
  hash.AddU32(rejection.schema_version);
  hash.AddBool(rejection.candidate_id.has_value());
  if (rejection.candidate_id.has_value()) {
    AddCandidateId(hash, *rejection.candidate_id);
  }
  hash.AddBool(rejection.net.has_value());
  if (rejection.net.has_value()) {
    AddEntity(hash, *rejection.net);
  }
  hash.AddByte(static_cast<std::uint8_t>(rejection.stage));
  hash.AddByte(static_cast<std::uint8_t>(rejection.code));
  hash.AddString(rejection.invariant_id);
  AddAssociations(hash, rejection.associations);
  hash.AddU64(rejection.policy_identity);
  AddProvenance(hash, rejection.provenance);
  AddOptionalU64(hash, rejection.primitive_witness_index);
  AddOptionalU64(hash, rejection.resource_witness_index);
  AddOptionalU64(hash, rejection.expected_value);
  AddOptionalU64(hash, rejection.actual_value);
  hash.AddBool(rejection.conflicting_entity.has_value());
  if (rejection.conflicting_entity.has_value()) {
    AddEntity(hash, *rejection.conflicting_entity);
  }
  AddOptionalU64(hash, rejection.candidate_payload_checksum);
  hash.AddString(rejection.detail);
  hash.AddU64(rejection.logical_bytes);
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

void AddConfig(board_ir::StableHashBuilder& hash,
               const SequentialNegotiatedBaselineConfig& config) noexcept {
  hash.AddU32(config.schema_version);
  hash.AddU64(config.deterministic_seed);
  hash.AddU64(config.intrinsic_cost_weight);
  hash.AddU32(config.maximum_sweeps);
  hash.AddU64(config.route_limits.maximum_work_units);
  hash.AddU64(config.route_limits.maximum_record_count);
  hash.AddU64(config.route_limits.maximum_queue_size);
  hash.AddU64(config.route_limits.maximum_reconstruction_states);
  hash.AddU64(config.price_config.present_step_per_overuse_unit);
  hash.AddU64(config.price_config.history_step_per_overuse_unit);
  hash.AddU64(config.price_config.maximum_price_per_resource);
  hash.AddU32(config.price_config.maximum_iterations);
  hash.AddU64(config.price_config.maximum_price_records);
  AddStoreConfig(hash, config.admission_store_config);
  hash.AddU64(config.allocator_limits.maximum_nets);
  hash.AddU64(config.allocator_limits.maximum_candidates);
  hash.AddU64(config.allocator_limits.maximum_resource_records);
  hash.AddU64(config.allocator_limits.maximum_expanded_resource_uses);
  hash.AddU64(config.limits.maximum_nets);
  hash.AddU64(config.limits.maximum_route_queries);
  hash.AddU64(config.limits.maximum_total_route_work_units);
  hash.AddU64(config.limits.maximum_policy_projection_visits);
  hash.AddU64(config.limits.maximum_policy_resource_entries);
  hash.AddU64(config.limits.maximum_expanded_resource_visits);
  hash.AddU64(config.limits.maximum_occupancy_resource_records);
  hash.AddU64(config.limits.maximum_candidate_draft_bytes);
  hash.AddU64(config.limits.maximum_retained_candidate_bytes);
  hash.AddU64(config.limits.maximum_retained_rejection_bytes);
  hash.AddU64(config.limits.maximum_trace_bytes);
  hash.AddU64(config.known_unmapped_exact_conflict_count);
}

[[nodiscard]] routing::EdgeResourceKey ResourceAt(const candidates::PhysicalEdgeSpan& span,
                                                  std::uint32_t offset) noexcept {
  const geometry_compiler::DirectionDelta delta =
      candidates::ResourceSpanStorageDelta(span.direction);
  const SWide x = static_cast<SWide>(span.lattice_x) + static_cast<SWide>(delta.x) * offset;
  const SWide y = static_cast<SWide>(span.lattice_y) + static_cast<SWide>(delta.y) * offset;
  return routing::EdgeResourceKey{.layer = span.layer,
                                  .lattice_x = static_cast<std::int64_t>(x),
                                  .lattice_y = static_cast<std::int64_t>(y),
                                  .direction = span.direction};
}

[[nodiscard]] bool SameAssociations(const AllocationAssociations& left,
                                    const AllocationAssociations& right) noexcept {
  return left == right;
}

[[nodiscard]] std::uint32_t CapacityFor(const ResourceCapacityModel& capacities,
                                        const routing::EdgeResourceKey& resource) noexcept {
  const auto found = std::ranges::lower_bound(capacities.overrides(), resource, {},
                                              &ResourceCapacityOverride::resource);
  return found != capacities.overrides().end() && found->resource == resource
             ? found->capacity_units
             : capacities.default_capacity_units();
}

[[nodiscard]] std::uint64_t HistoryFor(const NegotiatedPriceState& state,
                                       const routing::EdgeResourceKey& resource) noexcept {
  const auto found =
      std::ranges::lower_bound(state.prices(), resource, {}, &NegotiatedResourcePrice::resource);
  return found != state.prices().end() && found->resource == resource ? found->history_price : 0;
}

[[nodiscard]] bool ContainsResource(std::span<const routing::EdgeResourceKey> resources,
                                    const routing::EdgeResourceKey& resource) noexcept {
  return std::ranges::binary_search(resources, resource);
}

[[nodiscard]] bool PricesHaveSameValues(const NegotiatedPriceState& left,
                                        const NegotiatedPriceState& right) noexcept {
  if (left.prices().size() != right.prices().size()) {
    return false;
  }
  for (std::size_t index = 0; index < left.prices().size(); ++index) {
    const NegotiatedResourcePrice& a = left.prices()[index];
    const NegotiatedResourcePrice& b = right.prices()[index];
    if (a.resource != b.resource || a.present_price != b.present_price ||
        a.history_price != b.history_price || a.total_price != b.total_price ||
        a.observed_overuse_units != b.observed_overuse_units ||
        a.present_clamped != b.present_clamped || a.history_clamped != b.history_clamped ||
        a.total_clamped != b.total_clamped) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool SameWinnerSemantics(const candidates::StoredCandidate& left,
                                       const candidates::StoredCandidate& right) noexcept {
  if (left == nullptr || right == nullptr) {
    return left == right;
  }
  const candidates::GeneratedRouteCandidate& a = left->data();
  const candidates::GeneratedRouteCandidate& b = right->data();
  const candidates::CandidateMetrics& am = a.metrics;
  const candidates::CandidateMetrics& bm = b.metrics;
  return a.net == b.net && a.intended_terminals == b.intended_terminals &&
         a.schema_major == b.schema_major && a.schema_minor == b.schema_minor &&
         a.geometry_schema_version == b.geometry_schema_version &&
         a.resource_schema_version == b.resource_schema_version &&
         a.associations == b.associations && a.geometry == b.geometry &&
         a.resources == b.resources && am.intrinsic_base_cost == bm.intrinsic_base_cost &&
         am.orthogonal_step_count == bm.orthogonal_step_count &&
         am.diagonal_step_count == bm.diagonal_step_count && am.bend_count == bm.bend_count &&
         am.line_primitive_count == bm.line_primitive_count && am.via_count == bm.via_count &&
         am.axis_aligned_length_dbu == bm.axis_aligned_length_dbu &&
         am.diagonal_projection_dbu == bm.diagonal_projection_dbu && a.constraints == b.constraints;
}

[[nodiscard]] std::optional<SequentialNegotiatedBaselineError> ValidateConfigAndBounds(
    const board_ir::BoardSnapshot& board, const MultiNetWorkload& workload,
    const ResourceCapacityModel& capacities, const SequentialNegotiatedBaselineConfig& config) {
  if (config.schema_version != kSequentialNegotiatedBaselineSchemaVersion) {
    return Error(SequentialNegotiatedBaselineErrorCode::kUnsupportedSchema,
                 "allocator.sequential_negotiated.schema.v1",
                 "Sequential negotiated-routing baseline schema is unsupported");
  }
  const AllocationAssociations expected{
      .board_content_hash = workload.board_content_hash(),
      .compiler_profile_fingerprint = workload.compiler_profile_fingerprint(),
      .geometry_compiler_version = workload.geometry_compiler_version()};
  if (board.content_hash() != workload.board_content_hash() ||
      !SameAssociations(capacities.associations(), expected)) {
    return Error(SequentialNegotiatedBaselineErrorCode::kAssociationMismatch,
                 "allocator.sequential_negotiated.association.v1",
                 "Board, workload, and capacity-model associations differ");
  }
  if (workload.nets().empty()) {
    return Error(SequentialNegotiatedBaselineErrorCode::kInvalidConfiguration,
                 "allocator.sequential_negotiated.empty_workload.v1",
                 "Sequential negotiated routing requires at least one net");
  }
  if (capacities.default_capacity_units() == 0) {
    return Error(SequentialNegotiatedBaselineErrorCode::kInvalidConfiguration,
                 "allocator.sequential_negotiated.default_capacity.v1",
                 "Schema v1 requires positive default capacity; zero overrides remain supported");
  }
  const SequentialNegotiatedBaselineLimits& limits = config.limits;
  if (config.intrinsic_cost_weight == 0 || config.maximum_sweeps == 0 ||
      config.maximum_sweeps > kMaximumSequentialNegotiatedSweepsV1 || limits.maximum_nets == 0 ||
      limits.maximum_route_queries == 0 || limits.maximum_total_route_work_units == 0 ||
      limits.maximum_policy_projection_visits == 0 || limits.maximum_policy_resource_entries == 0 ||
      limits.maximum_expanded_resource_visits == 0 ||
      limits.maximum_occupancy_resource_records == 0 || limits.maximum_candidate_draft_bytes == 0 ||
      limits.maximum_retained_candidate_bytes == 0 ||
      limits.maximum_retained_rejection_bytes == 0 || limits.maximum_trace_bytes == 0 ||
      config.route_limits.maximum_work_units == 0 ||
      config.route_limits.maximum_record_count == 0 ||
      config.route_limits.maximum_queue_size == 0 ||
      config.route_limits.maximum_reconstruction_states == 0 ||
      config.price_config.maximum_iterations < config.maximum_sweeps ||
      config.price_config.maximum_price_records == 0 ||
      !internal::OneWorldAllocatorLimitsAreValidV1(config.allocator_limits)) {
    return Error(SequentialNegotiatedBaselineErrorCode::kInvalidConfiguration,
                 "allocator.sequential_negotiated.configuration.v1",
                 "Sequential negotiated-routing configuration contains an invalid zero or bound");
  }
  candidates::CandidateStore validation_store(config.admission_store_config);
  if (!validation_store.valid() || config.admission_store_config.maximum_candidates_per_net < 1 ||
      config.admission_store_config.maximum_admission_items_per_transaction < 1) {
    return Error(SequentialNegotiatedBaselineErrorCode::kInvalidConfiguration,
                 "allocator.sequential_negotiated.admission_configuration.v1",
                 "Per-query exact-admission store configuration is invalid");
  }
  const UWide net_count = workload.nets().size();
  if (net_count > limits.maximum_nets || net_count > config.allocator_limits.maximum_nets ||
      net_count > config.allocator_limits.maximum_candidates) {
    return Error(SequentialNegotiatedBaselineErrorCode::kInputBoundExceeded,
                 "allocator.sequential_negotiated.net_bound.v1",
                 "Workload net count exceeds a configured session or allocator bound", std::nullopt,
                 NarrowWitness(net_count),
                 std::min({limits.maximum_nets, config.allocator_limits.maximum_nets,
                           config.allocator_limits.maximum_candidates}));
  }
  const UWide query_count = net_count * config.maximum_sweeps;
  if (query_count > kMaximumSequentialNegotiatedQueriesV1 ||
      query_count > limits.maximum_route_queries) {
    return Error(SequentialNegotiatedBaselineErrorCode::kInputBoundExceeded,
                 "allocator.sequential_negotiated.query_bound.v1",
                 "Configured sweep schedule exceeds the route-query bound", std::nullopt,
                 NarrowWitness(query_count), limits.maximum_route_queries);
  }
  const UWide trace_bytes =
      query_count * kSequentialNegotiatedColumnTraceBytesV1 +
      static_cast<UWide>(config.maximum_sweeps) * kSequentialNegotiatedSweepTraceBytesV1;
  if (trace_bytes > limits.maximum_trace_bytes) {
    return Error(SequentialNegotiatedBaselineErrorCode::kInputBoundExceeded,
                 "allocator.sequential_negotiated.trace_byte_preflight.v1",
                 "Worst-case ordered column and sweep traces exceed their retained-byte bound",
                 std::nullopt, NarrowWitness(trace_bytes), limits.maximum_trace_bytes);
  }
  const UWide rejection_bytes = query_count * kMaximumSequentialNegotiatedRejectionBytesV1;
  if (rejection_bytes > limits.maximum_retained_rejection_bytes) {
    return Error(SequentialNegotiatedBaselineErrorCode::kInputBoundExceeded,
                 "allocator.sequential_negotiated.rejection_byte_preflight.v1",
                 "Worst-case structured rejection evidence exceeds its retained-byte bound",
                 std::nullopt, NarrowWitness(rejection_bytes),
                 limits.maximum_retained_rejection_bytes);
  }
  const UWide retained_candidate_bytes = net_count * limits.maximum_candidate_draft_bytes;
  if (retained_candidate_bytes > limits.maximum_retained_candidate_bytes) {
    return Error(SequentialNegotiatedBaselineErrorCode::kInputBoundExceeded,
                 "allocator.sequential_negotiated.candidate_byte_preflight.v1",
                 "Worst-case current-winner roster exceeds its retained candidate-byte bound",
                 std::nullopt, NarrowWitness(retained_candidate_bytes),
                 limits.maximum_retained_candidate_bytes);
  }
  const UWide total_route_work = query_count * config.route_limits.maximum_work_units;
  if (total_route_work > kMaximumSequentialNegotiatedAggregateWorkV1 ||
      total_route_work > limits.maximum_total_route_work_units) {
    return Error(SequentialNegotiatedBaselineErrorCode::kWorkBoundExceeded,
                 "allocator.sequential_negotiated.route_work_preflight.v1",
                 "Worst-case bounded CPU A* work exceeds the aggregate session bound", std::nullopt,
                 NarrowWitness(total_route_work), limits.maximum_total_route_work_units);
  }
  const UWide roster = static_cast<UWide>(config.price_config.maximum_price_records) +
                       limits.maximum_occupancy_resource_records + capacities.overrides().size();
  if (roster > config.allocator_limits.maximum_resource_records) {
    return Error(SequentialNegotiatedBaselineErrorCode::kInputBoundExceeded,
                 "allocator.sequential_negotiated.world_resource_preflight.v1",
                 "Worst-case occupancy, price, and capacity roster exceeds One-World bounds",
                 std::nullopt, NarrowWitness(roster),
                 config.allocator_limits.maximum_resource_records);
  }
  const UWide selected_expanded_uses =
      net_count * config.route_limits.maximum_reconstruction_states;
  if (selected_expanded_uses > limits.maximum_occupancy_resource_records) {
    return Error(SequentialNegotiatedBaselineErrorCode::kInputBoundExceeded,
                 "allocator.sequential_negotiated.occupancy_record_preflight.v1",
                 "Worst-case distinct current-winner edges exceed the occupancy record bound",
                 std::nullopt, NarrowWitness(selected_expanded_uses),
                 limits.maximum_occupancy_resource_records);
  }
  if (selected_expanded_uses > config.allocator_limits.maximum_expanded_resource_uses) {
    return Error(SequentialNegotiatedBaselineErrorCode::kInputBoundExceeded,
                 "allocator.sequential_negotiated.world_expansion_preflight.v1",
                 "Worst-case selected footprints exceed the One-World expansion bound",
                 std::nullopt, NarrowWitness(selected_expanded_uses),
                 config.allocator_limits.maximum_expanded_resource_uses);
  }
  UWide base_entries_per_sweep = 0;
  UWide base_penalties_per_sweep = 0;
  UWide maximum_policy_entries = 0;
  for (const PreparedNetRoutingContext& context : workload.nets()) {
    if (!routing::CandidateGenerationPolicyShapeIsWithinV1Bounds(
            context.request.candidate_policy)) {
      return Error(SequentialNegotiatedBaselineErrorCode::kInvalidConfiguration,
                   "allocator.sequential_negotiated.base_policy_shape.v1",
                   "A prepared workload base policy exceeds schema-v1 shape bounds",
                   context.request.net);
    }
    const UWide base_entries =
        static_cast<UWide>(context.request.candidate_policy.banned_resources.size()) +
        context.request.candidate_policy.resource_penalties.size();
    if (base_entries + roster > routing::kMaximumPolicyResourceEntries) {
      return Error(SequentialNegotiatedBaselineErrorCode::kWorkBoundExceeded,
                   "allocator.sequential_negotiated.policy_shape_preflight.v1",
                   "Worst-case base and congestion entries exceed one policy's schema bound",
                   context.request.net, NarrowWitness(base_entries + roster),
                   routing::kMaximumPolicyResourceEntries);
    }
    base_entries_per_sweep += base_entries;
    base_penalties_per_sweep += context.request.candidate_policy.resource_penalties.size();
    maximum_policy_entries = std::max(maximum_policy_entries, base_entries + roster);
  }
  const UWide projection_visits = static_cast<UWide>(config.maximum_sweeps) *
                                  (base_penalties_per_sweep + 2U * net_count * roster);
  if (projection_visits > limits.maximum_policy_projection_visits) {
    return Error(SequentialNegotiatedBaselineErrorCode::kWorkBoundExceeded,
                 "allocator.sequential_negotiated.policy_projection_preflight.v1",
                 "Worst-case policy projection and merge exceed the aggregate visit bound",
                 std::nullopt, NarrowWitness(projection_visits),
                 limits.maximum_policy_projection_visits);
  }
  const UWide policy_entries =
      static_cast<UWide>(config.maximum_sweeps) * (base_entries_per_sweep + net_count * roster);
  if (policy_entries > limits.maximum_policy_resource_entries) {
    return Error(SequentialNegotiatedBaselineErrorCode::kWorkBoundExceeded,
                 "allocator.sequential_negotiated.policy_entry_preflight.v1",
                 "Worst-case policy materialization exceeds the aggregate entry bound",
                 std::nullopt, NarrowWitness(policy_entries),
                 limits.maximum_policy_resource_entries);
  }
  const UWide expanded_visits =
      query_count * config.route_limits.maximum_reconstruction_states * 2U;
  if (expanded_visits > limits.maximum_expanded_resource_visits) {
    return Error(SequentialNegotiatedBaselineErrorCode::kWorkBoundExceeded,
                 "allocator.sequential_negotiated.resource_visit_preflight.v1",
                 "Worst-case rip-up and commit expansion exceeds the aggregate visit bound",
                 std::nullopt, NarrowWitness(expanded_visits),
                 limits.maximum_expanded_resource_visits);
  }
  const auto maximum_draft_bytes = internal::ComputeSequentialNegotiatedMaximumDraftBytesV1(
      config.route_limits.maximum_reconstruction_states, NarrowWitness(maximum_policy_entries));
  if (!maximum_draft_bytes.has_value() ||
      *maximum_draft_bytes > limits.maximum_candidate_draft_bytes) {
    return Error(SequentialNegotiatedBaselineErrorCode::kInputBoundExceeded,
                 "allocator.sequential_negotiated.draft_shape_preflight.v1",
                 "Worst-case bounded route and policy cannot fit the candidate draft-byte cap",
                 std::nullopt,
                 maximum_draft_bytes.value_or(std::numeric_limits<std::uint64_t>::max()),
                 limits.maximum_candidate_draft_bytes);
  }
  const auto admission_input_bytes = internal::ComputeSequentialNegotiatedAdmissionInputBytesV1(
      limits.maximum_candidate_draft_bytes, NarrowWitness(maximum_policy_entries));
  const auto admission_work = internal::ComputeSequentialNegotiatedAdmissionWorkV1(
      config.route_limits.maximum_reconstruction_states, NarrowWitness(maximum_policy_entries),
      board.data().obstacles.size(), board.data().terminals.size());
  if (!admission_input_bytes.has_value() ||
      *admission_input_bytes >
          config.admission_store_config.maximum_admission_input_bytes_per_transaction ||
      config.admission_store_config.maximum_candidate_bytes_per_net <
          limits.maximum_candidate_draft_bytes) {
    return Error(SequentialNegotiatedBaselineErrorCode::kInvalidConfiguration,
                 "allocator.sequential_negotiated.admission_byte_preflight.v1",
                 "CandidateStore cannot retain and re-account the worst-case bounded draft",
                 std::nullopt,
                 admission_input_bytes.value_or(std::numeric_limits<std::uint64_t>::max()),
                 config.admission_store_config.maximum_admission_input_bytes_per_transaction);
  }
  if (!admission_work.has_value() ||
      *admission_work >
          config.admission_store_config.maximum_admission_work_units_per_transaction) {
    return Error(SequentialNegotiatedBaselineErrorCode::kInvalidConfiguration,
                 "allocator.sequential_negotiated.admission_work_preflight.v1",
                 "CandidateStore cannot exactly validate the worst-case bounded draft",
                 std::nullopt, admission_work.value_or(std::numeric_limits<std::uint64_t>::max()),
                 config.admission_store_config.maximum_admission_work_units_per_transaction);
  }
  if (config.known_unmapped_exact_conflict_count != 0) {
    return Error(SequentialNegotiatedBaselineErrorCode::kResourceRefinementRequired,
                 "allocator.sequential_negotiated.resource_refinement.v1",
                 "Known exact conflicts are not mapped into allocator resources", std::nullopt,
                 config.known_unmapped_exact_conflict_count, 0);
  }
  return std::nullopt;
}

struct SynthesizedQueryPolicy {
  routing::NormalizedCandidateGenerationPolicy normalized;
  std::uint64_t congestion_resource_count = 0;
  std::uint64_t total_congestion_penalty = 0;
};

[[nodiscard]] std::variant<SynthesizedQueryPolicy, SequentialNegotiatedBaselineError>
BuildQueryPolicy(const PreparedNetRoutingContext& context, const ResourceCapacityModel& capacities,
                 const NegotiatedPriceState& price_state, const Occupancy& occupancy,
                 const SequentialNegotiatedBaselineConfig& config, std::uint32_t sweep_index,
                 SequentialNegotiatedBaselineCounters& counters) {
  const UWide visits = static_cast<UWide>(occupancy.size()) + price_state.prices().size() +
                       capacities.overrides().size();
  if (static_cast<UWide>(counters.policy_projection_visits) + visits >
      config.limits.maximum_policy_projection_visits) {
    return Error(
        SequentialNegotiatedBaselineErrorCode::kWorkBoundExceeded,
        "allocator.sequential_negotiated.policy_projection_runtime.v1",
        "Policy projection reached its configured aggregate visit bound", context.request.net,
        NarrowWitness(visits),
        config.limits.maximum_policy_projection_visits - counters.policy_projection_visits);
  }
  counters.policy_projection_visits += static_cast<std::uint64_t>(visits);

  std::vector<routing::EdgeResourceKey> roster;
  roster.reserve(static_cast<std::size_t>(visits));
  for (const auto& [resource, usage] : occupancy) {
    static_cast<void>(usage);
    roster.push_back(resource);
  }
  for (const NegotiatedResourcePrice& price : price_state.prices()) {
    roster.push_back(price.resource);
  }
  for (const ResourceCapacityOverride& capacity : capacities.overrides()) {
    roster.push_back(capacity.resource);
  }
  std::ranges::sort(roster);
  roster.erase(std::unique(roster.begin(), roster.end()), roster.end());

  routing::CandidatePolicyResult base_result = routing::NormalizeCandidateGenerationPolicy(
      context.compiled_board, context.request.candidate_policy);
  if (!std::holds_alternative<routing::NormalizedCandidateGenerationPolicy>(base_result)) {
    return Error(SequentialNegotiatedBaselineErrorCode::kPolicySynthesis,
                 "allocator.sequential_negotiated.base_policy.v1",
                 "Prepared workload base policy cannot be normalized", context.request.net);
  }
  routing::CandidateGenerationPolicy policy =
      std::get<routing::NormalizedCandidateGenerationPolicy>(std::move(base_result)).policy;
  board_ir::StableHashBuilder seed_hash;
  seed_hash.AddString("APGAR-SEQUENTIAL-NEGOTIATED-NET-SEED-V1");
  seed_hash.AddU64(config.deterministic_seed);
  seed_hash.AddU64(price_state.workload_checksum());
  AddEntity(seed_hash, context.request.net);
  policy.deterministic_seed = seed_hash.Finish();
  policy.candidate_ordinal = sweep_index;

  const UWide multiplier = config.intrinsic_cost_weight - 1U;
  const geometry_compiler::DeterministicCosts costs = context.compiled_board.profile().costs;
  const UWide orthogonal =
      static_cast<UWide>(policy.orthogonal_step_surcharge) + multiplier * costs.orthogonal_step;
  const UWide diagonal =
      static_cast<UWide>(policy.diagonal_step_surcharge) + multiplier * costs.diagonal_step;
  const UWide bend = static_cast<UWide>(policy.bend_surcharge) + multiplier * costs.bend;
  if (orthogonal >= std::numeric_limits<std::uint64_t>::max() ||
      diagonal >= std::numeric_limits<std::uint64_t>::max() ||
      bend >= std::numeric_limits<std::uint64_t>::max()) {
    return Error(SequentialNegotiatedBaselineErrorCode::kPolicySynthesis,
                 "allocator.sequential_negotiated.intrinsic_weight.v1",
                 "Intrinsic-cost weighting cannot be represented as finite route surcharges",
                 context.request.net);
  }
  policy.orthogonal_step_surcharge = static_cast<std::uint64_t>(orthogonal);
  policy.diagonal_step_surcharge = static_cast<std::uint64_t>(diagonal);
  policy.bend_surcharge = static_cast<std::uint64_t>(bend);

  UWide congestion_resource_count = 0;
  UWide total_congestion_penalty = 0;
  std::vector<routing::ResourcePenalty> congestion_penalties;
  congestion_penalties.reserve(roster.size());
  for (const routing::EdgeResourceKey& resource : roster) {
    if (!routing::ResourceExists(context.compiled_board, resource) ||
        ContainsResource(policy.banned_resources, resource)) {
      continue;
    }
    const auto usage = occupancy.find(resource);
    const std::uint64_t current_usage = usage == occupancy.end() ? 0 : usage->second;
    const std::uint32_t capacity = CapacityFor(capacities, resource);
    const UWide prospective_usage = static_cast<UWide>(current_usage) + 1U;
    const UWide overuse = prospective_usage > capacity ? prospective_usage - capacity : 0;
    const UWide raw_present = overuse * config.price_config.present_step_per_overuse_unit;
    const std::uint64_t present = static_cast<std::uint64_t>(
        std::min<UWide>(raw_present, config.price_config.maximum_price_per_resource));
    const UWide raw_additional = static_cast<UWide>(HistoryFor(price_state, resource)) + present;
    const std::uint64_t additional = static_cast<std::uint64_t>(
        std::min<UWide>(raw_additional, config.price_config.maximum_price_per_resource));
    if (additional == 0) {
      continue;
    }
    ++congestion_resource_count;
    total_congestion_penalty += additional;
    if (congestion_resource_count > std::numeric_limits<std::uint64_t>::max() ||
        total_congestion_penalty > std::numeric_limits<std::uint64_t>::max()) {
      return Error(SequentialNegotiatedBaselineErrorCode::kPolicySynthesis,
                   "allocator.sequential_negotiated.penalty_aggregate.v1",
                   "Prospective congestion-policy evidence exceeds unsigned 64-bit fields",
                   context.request.net);
    }
    congestion_penalties.push_back(
        routing::ResourcePenalty{.resource = resource, .additional_cost = additional});
  }
  const UWide merge_bound =
      static_cast<UWide>(policy.resource_penalties.size()) + congestion_penalties.size();
  if (static_cast<UWide>(counters.policy_projection_visits) + merge_bound >
      config.limits.maximum_policy_projection_visits) {
    return Error(
        SequentialNegotiatedBaselineErrorCode::kWorkBoundExceeded,
        "allocator.sequential_negotiated.policy_projection_runtime.v1",
        "Policy merge reached its configured aggregate visit bound", context.request.net,
        NarrowWitness(merge_bound),
        config.limits.maximum_policy_projection_visits - counters.policy_projection_visits);
  }
  std::optional<internal::SequentialNegotiatedPenaltyMergeV1> merged =
      internal::MergeSequentialNegotiatedPenaltiesV1(policy.resource_penalties,
                                                     congestion_penalties);
  if (!merged.has_value()) {
    return Error(SequentialNegotiatedBaselineErrorCode::kPolicySynthesis,
                 "allocator.sequential_negotiated.penalty_overflow.v1",
                 "Base-policy and congestion penalties do not form one finite bounded policy",
                 context.request.net);
  }
  counters.policy_projection_visits += merged->visits;
  policy.resource_penalties = std::move(merged->penalties);
  const UWide entries =
      static_cast<UWide>(policy.banned_resources.size()) + policy.resource_penalties.size();
  if (entries > routing::kMaximumPolicyResourceEntries ||
      static_cast<UWide>(counters.policy_resource_entries) + entries >
          config.limits.maximum_policy_resource_entries) {
    return Error(SequentialNegotiatedBaselineErrorCode::kWorkBoundExceeded,
                 "allocator.sequential_negotiated.policy_entry_runtime.v1",
                 "A synthesized policy exceeds its per-query or aggregate resource-entry bound",
                 context.request.net, NarrowWitness(entries),
                 config.limits.maximum_policy_resource_entries - counters.policy_resource_entries);
  }
  counters.policy_resource_entries += static_cast<std::uint64_t>(entries);
  routing::CandidatePolicyResult normalized =
      routing::NormalizeCandidateGenerationPolicy(context.compiled_board, std::move(policy));
  if (const auto* failure = std::get_if<routing::CandidatePolicyError>(&normalized);
      failure != nullptr) {
    static_cast<void>(failure);
    return Error(SequentialNegotiatedBaselineErrorCode::kPolicySynthesis,
                 "allocator.sequential_negotiated.query_policy.v1",
                 "A congestion-aware query policy cannot be normalized", context.request.net);
  }
  return SynthesizedQueryPolicy{
      .normalized = std::get<routing::NormalizedCandidateGenerationPolicy>(std::move(normalized)),
      .congestion_resource_count = static_cast<std::uint64_t>(congestion_resource_count),
      .total_congestion_penalty = static_cast<std::uint64_t>(total_congestion_penalty),
  };
}

[[nodiscard]] std::optional<SequentialNegotiatedBaselineError> ApplyCandidateUsage(
    const candidates::StoredCandidate& candidate, bool add, Occupancy& occupancy,
    const SequentialNegotiatedBaselineConfig& config,
    SequentialNegotiatedBaselineCounters& counters) {
  UWide edge_visits = 0;
  for (const candidates::PhysicalEdgeSpan& span : candidate->data().resources) {
    edge_visits += span.edge_count;
  }
  if (static_cast<UWide>(counters.expanded_resource_visits) + edge_visits >
      config.limits.maximum_expanded_resource_visits) {
    return Error(
        SequentialNegotiatedBaselineErrorCode::kWorkBoundExceeded,
        "allocator.sequential_negotiated.resource_visit_runtime.v1",
        "Rip-up or commit reached the aggregate expanded-resource visit bound", candidate->net(),
        NarrowWitness(edge_visits),
        config.limits.maximum_expanded_resource_visits - counters.expanded_resource_visits);
  }
  counters.expanded_resource_visits += static_cast<std::uint64_t>(edge_visits);
  for (const candidates::PhysicalEdgeSpan& span : candidate->data().resources) {
    for (std::uint32_t offset = 0; offset < span.edge_count; ++offset) {
      const routing::EdgeResourceKey resource = ResourceAt(span, offset);
      if (add) {
        auto [entry, inserted] = occupancy.try_emplace(resource, 0);
        if (inserted && occupancy.size() > config.limits.maximum_occupancy_resource_records) {
          return Error(SequentialNegotiatedBaselineErrorCode::kWorkBoundExceeded,
                       "allocator.sequential_negotiated.occupancy_record_bound.v1",
                       "Committed occupancy exceeds its distinct-resource record bound",
                       candidate->net(), occupancy.size(),
                       config.limits.maximum_occupancy_resource_records);
        }
        const UWide next = static_cast<UWide>(entry->second) + span.usage_units;
        if (next > std::numeric_limits<std::uint64_t>::max()) {
          return Error(SequentialNegotiatedBaselineErrorCode::kInternalInvariant,
                       "allocator.sequential_negotiated.occupancy_overflow.v1",
                       "Committed occupancy exceeds unsigned 64-bit usage units", candidate->net());
        }
        entry->second = static_cast<std::uint64_t>(next);
      } else {
        auto entry = occupancy.find(resource);
        if (entry == occupancy.end() || entry->second < span.usage_units) {
          return Error(SequentialNegotiatedBaselineErrorCode::kInternalInvariant,
                       "allocator.sequential_negotiated.ripup_underflow.v1",
                       "A prior winner cannot be removed from baseline-local occupancy",
                       candidate->net());
        }
        entry->second -= span.usage_units;
        if (entry->second == 0) {
          occupancy.erase(entry);
        }
      }
    }
  }
  return std::nullopt;
}

[[nodiscard]] std::vector<CandidatePool> PoolsFor(
    const MultiNetWorkload& workload, std::span<const candidates::StoredCandidate> winners) {
  std::vector<CandidatePool> pools;
  pools.reserve(workload.nets().size());
  for (std::size_t index = 0; index < workload.nets().size(); ++index) {
    CandidatePool pool{.net = workload.nets()[index].request.net, .candidates = {}};
    if (winners[index] != nullptr) {
      pool.candidates.push_back(winners[index]);
    }
    pools.push_back(std::move(pool));
  }
  return pools;
}

[[nodiscard]] bool WorldMatchesOccupancy(const OneWorldAllocation& world,
                                         const Occupancy& occupancy) noexcept {
  std::size_t occupancy_index = 0;
  auto entry = occupancy.begin();
  for (const ResourceUsage& usage : world.resources) {
    if (usage.usage_units == 0) {
      continue;
    }
    if (entry == occupancy.end() || entry->first != usage.resource ||
        entry->second != usage.usage_units) {
      return false;
    }
    ++entry;
    ++occupancy_index;
  }
  return entry == occupancy.end() && occupancy_index == occupancy.size();
}

[[nodiscard]] std::uint64_t RouteWork(const routing::RouteFailure& failure) noexcept {
  return failure.telemetry.has_value() ? failure.telemetry->work_units : 0;
}

[[nodiscard]] std::optional<SequentialNegotiatedBaselineError> AddRouteWork(
    std::uint64_t work, const SequentialNegotiatedBaselineConfig& config,
    SequentialNegotiatedBaselineCounters& counters, board_ir::EntityRef net) noexcept {
  const UWide total = static_cast<UWide>(counters.route_work_units) + work;
  if (total > config.limits.maximum_total_route_work_units) {
    return Error(SequentialNegotiatedBaselineErrorCode::kWorkBoundExceeded,
                 "allocator.sequential_negotiated.route_work_runtime.v1",
                 "Bounded CPU A* reached the aggregate session work bound", net,
                 NarrowWitness(total), config.limits.maximum_total_route_work_units);
  }
  counters.route_work_units = static_cast<std::uint64_t>(total);
  return std::nullopt;
}

[[nodiscard]] std::optional<SequentialNegotiatedBaselineError> RetainColumnRejection(
    candidates::CandidateRejection rejection, const SequentialNegotiatedBaselineConfig& config,
    SequentialNegotiatedBaselineCounters& counters, SequentialNegotiatedColumnRecord& column) {
  const std::optional<std::uint64_t> logical_bytes =
      candidates::ComputeRejectionLogicalBytes(rejection);
  if (!logical_bytes.has_value() || *logical_bytes != rejection.logical_bytes ||
      *logical_bytes > kMaximumSequentialNegotiatedRejectionBytesV1) {
    return Error(SequentialNegotiatedBaselineErrorCode::kInternalInvariant,
                 "allocator.sequential_negotiated.rejection_envelope.v1",
                 "Candidate rejection is not a bounded canonical rejection-v1 record", column.net);
  }
  const UWide retained = static_cast<UWide>(counters.retained_rejection_bytes) + *logical_bytes;
  if (retained > config.limits.maximum_retained_rejection_bytes) {
    return Error(SequentialNegotiatedBaselineErrorCode::kWorkBoundExceeded,
                 "allocator.sequential_negotiated.rejection_byte_runtime.v1",
                 "Structured rejection evidence reached its aggregate retained-byte bound",
                 column.net, NarrowWitness(retained),
                 config.limits.maximum_retained_rejection_bytes);
  }
  counters.retained_rejection_bytes = static_cast<std::uint64_t>(retained);
  column.rejection = std::move(rejection);
  return std::nullopt;
}

template <typename Operation>
[[nodiscard]] SequentialNegotiatedBaselineExecution WithFailureEnvelope(Operation&& operation) {
  try {
    return std::forward<Operation>(operation)();
  } catch (const std::bad_alloc&) {
    return Error(SequentialNegotiatedBaselineErrorCode::kResourceExhausted,
                 "allocator.sequential_negotiated.host_memory.v1",
                 "Host allocation failed within sequential-baseline bounds");
  } catch (const std::length_error&) {
    return Error(SequentialNegotiatedBaselineErrorCode::kResourceExhausted,
                 "allocator.sequential_negotiated.host_container.v1",
                 "Host container limits were exhausted within sequential-baseline bounds");
  }
}

}  // namespace

std::optional<internal::SequentialNegotiatedPenaltyMergeV1>
internal::MergeSequentialNegotiatedPenaltiesV1(
    std::span<const routing::ResourcePenalty> base,
    std::span<const routing::ResourcePenalty> congestion) {
  const UWide maximum_size = static_cast<UWide>(base.size()) + congestion.size();
  if (maximum_size > routing::kMaximumPolicyResourceEntries ||
      maximum_size > std::numeric_limits<std::size_t>::max()) {
    return std::nullopt;
  }
  SequentialNegotiatedPenaltyMergeV1 result;
  result.penalties.reserve(static_cast<std::size_t>(maximum_size));
  std::size_t base_index = 0;
  std::size_t congestion_index = 0;
  while (base_index < base.size() || congestion_index < congestion.size()) {
    ++result.visits;
    if (congestion_index == congestion.size() ||
        (base_index < base.size() &&
         base[base_index].resource < congestion[congestion_index].resource)) {
      result.penalties.push_back(base[base_index++]);
      continue;
    }
    if (base_index == base.size() ||
        congestion[congestion_index].resource < base[base_index].resource) {
      result.penalties.push_back(congestion[congestion_index++]);
      continue;
    }
    const UWide combined = static_cast<UWide>(base[base_index].additional_cost) +
                           congestion[congestion_index].additional_cost;
    if (combined >= std::numeric_limits<std::uint64_t>::max()) {
      return std::nullopt;
    }
    result.penalties.push_back(routing::ResourcePenalty{
        .resource = base[base_index].resource,
        .additional_cost = static_cast<std::uint64_t>(combined),
    });
    ++base_index;
    ++congestion_index;
  }
  return result;
}

std::optional<std::uint64_t> internal::ComputeSequentialNegotiatedMaximumDraftBytesV1(
    std::uint64_t maximum_reconstruction_states,
    std::uint64_t maximum_policy_resource_entries) noexcept {
  const UWide bytes = 4'096U + static_cast<UWide>(512U) * maximum_reconstruction_states +
                      static_cast<UWide>(128U) * maximum_policy_resource_entries;
  return bytes <= std::numeric_limits<std::uint64_t>::max()
             ? std::optional<std::uint64_t>(static_cast<std::uint64_t>(bytes))
             : std::nullopt;
}

std::optional<std::uint64_t> internal::ComputeSequentialNegotiatedAdmissionInputBytesV1(
    std::uint64_t maximum_candidate_draft_bytes,
    std::uint64_t maximum_policy_resource_entries) noexcept {
  const UWide policy_bytes = 57U + static_cast<UWide>(29U) * maximum_policy_resource_entries;
  const UWide total = static_cast<UWide>(maximum_candidate_draft_bytes) + policy_bytes;
  return total <= std::numeric_limits<std::uint64_t>::max()
             ? std::optional<std::uint64_t>(static_cast<std::uint64_t>(total))
             : std::nullopt;
}

std::optional<std::uint64_t> internal::ComputeSequentialNegotiatedAdmissionWorkV1(
    std::uint64_t maximum_reconstruction_states, std::uint64_t maximum_policy_resource_entries,
    std::uint64_t obstacle_count, std::uint64_t terminal_count) noexcept {
  // Candidate exact geometry spatially prunes self-clearance comparisons, but
  // CandidateStore transaction accounting deliberately charges every possible
  // primitive pair. A valid route may therefore exceed the exact validator's
  // pair-check budget while still requiring the full structural work envelope.
  const UWide primitives = std::min<std::uint64_t>(maximum_reconstruction_states,
                                                   candidates::kMaximumCandidatePrimitives);
  const UWide primitive_pairs = primitives * (primitives - (primitives == 0 ? 0U : 1U)) / 2U;
  const UWide resource_spans = std::min<std::uint64_t>(maximum_reconstruction_states,
                                                       candidates::kMaximumCandidateResourceSpans);
  const UWide work = 1U + primitives + primitive_pairs +
                     primitives * (static_cast<UWide>(obstacle_count) + terminal_count) +
                     resource_spans + static_cast<UWide>(2U) * maximum_policy_resource_entries +
                     static_cast<UWide>(2U) * maximum_reconstruction_states;
  return work <= std::numeric_limits<std::uint64_t>::max()
             ? std::optional<std::uint64_t>(static_cast<std::uint64_t>(work))
             : std::nullopt;
}

#ifdef APGAR_SEQUENTIAL_NEGOTIATED_BASELINE_FAULT_TEST_VARIANT
void internal::SetSequentialNegotiatedAdmissionBudgetFaultForTesting(
    std::uint64_t query_identity) noexcept {
  g_admission_budget_fault_query.store(query_identity);
}
#endif

std::uint64_t internal::ComputeSequentialNegotiatedCandidateSemanticChecksumV1(
    const candidates::RouteCandidate& candidate) noexcept {
  board_ir::StableHashBuilder hash;
  hash.AddString("APGAR-SEQUENTIAL-NEGOTIATED-CANDIDATE-SEMANTICS-V1");
  const candidates::GeneratedRouteCandidate& data = candidate.data();
  AddU16(hash, data.schema_major);
  AddU16(hash, data.schema_minor);
  hash.AddU32(data.geometry_schema_version);
  hash.AddU32(data.resource_schema_version);
  AddEntity(hash, data.net);
  for (const board_ir::EntityRef terminal : data.intended_terminals) {
    AddEntity(hash, terminal);
  }
  hash.AddU64(data.associations.board_content_hash);
  hash.AddU64(data.associations.compiler_profile_fingerprint);
  hash.AddU32(data.associations.geometry_compiler_version);
  hash.AddU64(data.associations.routing_profile_fingerprint);
  hash.AddU64(data.associations.rule_bucket_identity);
  hash.AddU64(static_cast<std::uint64_t>(data.geometry.size()));
  for (const candidates::CandidatePrimitive& primitive : data.geometry) {
    hash.AddByte(static_cast<std::uint8_t>(primitive.index()));
    if (const auto* line = std::get_if<candidates::ExactLinePrimitive>(&primitive);
        line != nullptr) {
      hash.AddU32(line->layer);
      hash.AddI64(line->centerline.start.x);
      hash.AddI64(line->centerline.start.y);
      hash.AddI64(line->centerline.end.x);
      hash.AddI64(line->centerline.end.y);
    } else {
      const candidates::ThroughViaPrimitive& via =
          std::get<candidates::ThroughViaPrimitive>(primitive);
      hash.AddU64(via.template_id);
      hash.AddI64(via.position.x);
      hash.AddI64(via.position.y);
      hash.AddU32(via.start_layer);
      hash.AddU32(via.end_layer);
    }
  }
  hash.AddU64(static_cast<std::uint64_t>(data.resources.size()));
  for (const candidates::PhysicalEdgeSpan& span : data.resources) {
    AddResource(hash, routing::EdgeResourceKey{.layer = span.layer,
                                               .lattice_x = span.lattice_x,
                                               .lattice_y = span.lattice_y,
                                               .direction = span.direction});
    hash.AddU32(span.edge_count);
    hash.AddU32(span.usage_units);
  }
  const candidates::CandidateMetrics& metrics = data.metrics;
  hash.AddU64(metrics.intrinsic_base_cost);
  hash.AddU64(metrics.orthogonal_step_count);
  hash.AddU64(metrics.diagonal_step_count);
  hash.AddU64(metrics.bend_count);
  hash.AddU64(metrics.line_primitive_count);
  hash.AddU64(metrics.via_count);
  hash.AddU64(metrics.axis_aligned_length_dbu);
  hash.AddU64(metrics.diagonal_projection_dbu);
  hash.AddBool(data.constraints.supported_hard_constraints_satisfied);
  hash.AddBool(data.constraints.unsupported_rules_remain);
  hash.AddU32(data.constraints.connected_intended_terminal_count);
  hash.AddByte(static_cast<std::uint8_t>(data.constraints.exact_validation_code));
  return hash.Finish();
}

std::uint64_t internal::ComputeSequentialNegotiatedBatchIdentityV1(
    std::uint64_t board_content_hash, std::uint64_t workload_checksum,
    std::uint64_t capacity_model_checksum,
    const SequentialNegotiatedBaselineConfig& config) noexcept {
  board_ir::StableHashBuilder hash;
  hash.AddString("APGAR-SEQUENTIAL-NEGOTIATED-BATCH-V1");
  hash.AddU64(board_content_hash);
  hash.AddU64(workload_checksum);
  hash.AddU64(capacity_model_checksum);
  AddConfig(hash, config);
  const std::uint64_t identity = hash.Finish();
  return identity == 0 ? 1 : identity;
}

std::uint64_t internal::ComputeSequentialNegotiatedSessionChecksumV1(
    const SequentialNegotiatedBaselineConfig& config, std::uint64_t batch_identity,
    std::uint64_t workload_checksum, std::uint64_t capacity_model_checksum,
    SequentialNegotiatedTerminalReason terminal_reason,
    const SequentialNegotiatedBaselineCounters& counters,
    std::span<const SequentialNegotiatedColumnRecord> columns,
    std::span<const SequentialNegotiatedSweepRecord> sweeps,
    std::span<const CandidatePool> final_pools, const OneWorldAllocation& final_world,
    const NegotiatedPriceState& successor_price_state) noexcept {
  board_ir::StableHashBuilder hash;
  hash.AddString("APGAR-SEQUENTIAL-NEGOTIATED-SESSION-V1");
  AddConfig(hash, config);
  hash.AddU64(batch_identity);
  hash.AddU64(workload_checksum);
  hash.AddU64(capacity_model_checksum);
  hash.AddByte(static_cast<std::uint8_t>(terminal_reason));
  hash.AddU64(counters.completed_sweeps);
  hash.AddU64(counters.route_queries);
  hash.AddU64(counters.route_work_units);
  hash.AddU64(counters.successful_routes);
  hash.AddU64(counters.admitted_candidates);
  hash.AddU64(counters.rejected_routes);
  hash.AddU64(counters.restored_prior_routes);
  hash.AddU64(counters.policy_projection_visits);
  hash.AddU64(counters.policy_resource_entries);
  hash.AddU64(counters.expanded_resource_visits);
  hash.AddU64(counters.price_updates);
  hash.AddU64(counters.peak_retained_candidate_bytes);
  hash.AddU64(counters.retained_rejection_bytes);
  hash.AddU64(static_cast<std::uint64_t>(columns.size()));
  for (const SequentialNegotiatedColumnRecord& column : columns) {
    hash.AddU32(column.sweep_index);
    AddEntity(hash, column.net);
    hash.AddU64(column.policy_identity);
    hash.AddU64(column.batch_identity);
    hash.AddU64(column.query_identity);
    hash.AddU64(column.route_work_units);
    hash.AddU64(column.prospective_congestion_resource_count);
    hash.AddU64(column.total_prospective_congestion_penalty);
    hash.AddByte(static_cast<std::uint8_t>(column.outcome));
    hash.AddBool(column.retained_prior_candidate);
    hash.AddBool(column.candidate_id.has_value());
    if (column.candidate_id.has_value()) {
      AddCandidateId(hash, *column.candidate_id);
    }
    hash.AddBool(column.candidate_payload_checksum.has_value());
    if (column.candidate_payload_checksum.has_value()) {
      hash.AddU64(*column.candidate_payload_checksum);
    }
    hash.AddBool(column.candidate_semantic_checksum.has_value());
    if (column.candidate_semantic_checksum.has_value()) {
      hash.AddU64(*column.candidate_semantic_checksum);
    }
    hash.AddBool(column.rejection.has_value());
    if (column.rejection.has_value()) {
      AddRejection(hash, *column.rejection);
    }
  }
  hash.AddU64(static_cast<std::uint64_t>(sweeps.size()));
  for (const SequentialNegotiatedSweepRecord& sweep : sweeps) {
    hash.AddU32(sweep.sweep_index);
    hash.AddU32(sweep.source_price_iteration);
    hash.AddU64(sweep.source_price_state_checksum);
    hash.AddU64(sweep.successor_price_state_checksum);
    hash.AddU64(sweep.world_checksum);
    hash.AddU64(sweep.first_query_identity);
    hash.AddU64(sweep.route_query_count);
    hash.AddU64(sweep.selected_net_count);
    hash.AddU64(sweep.no_candidate_net_count);
    hash.AddU64(sweep.overused_resource_count);
    hash.AddU64(sweep.total_overuse_units);
    hash.AddU64(sweep.total_intrinsic_cost);
    hash.AddBool(sweep.winners_unchanged);
    hash.AddBool(sweep.price_values_unchanged);
  }
  hash.AddU64(static_cast<std::uint64_t>(final_pools.size()));
  for (const CandidatePool& pool : final_pools) {
    AddEntity(hash, pool.net);
    hash.AddU64(static_cast<std::uint64_t>(pool.candidates.size()));
    for (const candidates::StoredCandidate& candidate : pool.candidates) {
      AddCandidateId(hash, candidate->id());
      hash.AddU64(candidate->data().payload_checksum);
      hash.AddU64(ComputeSequentialNegotiatedCandidateSemanticChecksumV1(*candidate));
    }
  }
  hash.AddU64(final_world.world_checksum);
  hash.AddU64(successor_price_state.state_checksum());
  return hash.Finish();
}

std::optional<std::uint64_t> internal::RecomputeSequentialNegotiatedSessionChecksumFromLiveV1(
    const SequentialNegotiatedBaselineResult& result, const board_ir::BoardSnapshot& board,
    const MultiNetWorkload& workload, const ResourceCapacityModel& capacities) noexcept {
  const AllocationAssociations expected{
      .board_content_hash = workload.board_content_hash(),
      .compiler_profile_fingerprint = workload.compiler_profile_fingerprint(),
      .geometry_compiler_version = workload.geometry_compiler_version(),
  };
  const std::uint64_t workload_checksum = internal::RecomputeMultiNetWorkloadChecksumV1(workload);
  const std::uint64_t capacity_checksum =
      internal::RecomputeResourceCapacityModelChecksumV1(capacities);
  const std::uint64_t world_checksum = internal::ComputeOneWorldChecksumV2(result.final_world());
  const std::uint64_t price_checksum =
      internal::RecomputeNegotiatedPriceStateChecksumV1(result.successor_price_state());
  const std::uint64_t batch_identity = internal::ComputeSequentialNegotiatedBatchIdentityV1(
      board.content_hash(), workload_checksum, capacity_checksum, result.config());

  if (workload_checksum == 0 || workload_checksum != workload.workload_checksum() ||
      workload_checksum != result.workload_checksum() ||
      board.content_hash() != workload.board_content_hash() ||
      capacities.associations() != expected || capacity_checksum == 0 ||
      capacity_checksum != result.capacity_model_checksum() ||
      result.successor_price_state().associations() != expected ||
      result.successor_price_state().workload_checksum() != workload_checksum ||
      result.successor_price_state().capacity_model_checksum() != capacity_checksum ||
      price_checksum == 0 || price_checksum != result.successor_price_state().state_checksum() ||
      result.final_world().associations != expected ||
      result.final_world().workload_checksum != workload_checksum || world_checksum == 0 ||
      world_checksum != result.final_world().world_checksum ||
      result.successor_price_state().source_world_checksum() != world_checksum ||
      batch_identity == 0 || batch_identity != result.batch_identity() ||
      result.final_pools().size() != workload.nets().size() ||
      result.final_world().selections.size() != result.final_pools().size()) {
    return std::nullopt;
  }

  for (std::size_t index = 0; index < result.final_pools().size(); ++index) {
    const CandidatePool& pool = result.final_pools()[index];
    const NetSelection& selection = result.final_world().selections[index];
    if (pool.net != workload.nets()[index].request.net || selection.net != pool.net) {
      return std::nullopt;
    }
    for (const candidates::StoredCandidate& candidate : pool.candidates) {
      if (!internal::CandidateHasAuthenticLivePayloadV1(candidate, workload.nets()[index])) {
        return std::nullopt;
      }
    }
    if (selection.status == NetSelectionStatus::kSelected) {
      if (selection.candidate == nullptr || !selection.candidate_id.has_value() ||
          !selection.candidate_payload_checksum.has_value() ||
          *selection.candidate_id != selection.candidate->id() ||
          *selection.candidate_payload_checksum != selection.candidate->data().payload_checksum ||
          std::ranges::none_of(pool.candidates,
                               [&selection](const candidates::StoredCandidate& candidate) {
                                 return candidate == selection.candidate;
                               })) {
        return std::nullopt;
      }
    } else if (selection.candidate != nullptr || selection.candidate_id.has_value() ||
               selection.candidate_payload_checksum.has_value()) {
      return std::nullopt;
    }
  }

  const std::uint64_t rebuilt = internal::ComputeSequentialNegotiatedSessionChecksumV1(
      result.config(), batch_identity, workload_checksum, capacity_checksum,
      result.terminal_reason(), result.counters(), result.columns(), result.sweeps(),
      result.final_pools(), result.final_world(), result.successor_price_state());
  if (rebuilt == 0 || rebuilt != result.session_checksum()) {
    return std::nullopt;
  }
  return rebuilt;
}

SequentialNegotiatedBaselineResult::SequentialNegotiatedBaselineResult(
    SequentialNegotiatedBaselineConfig config, std::uint64_t batch_identity,
    std::uint64_t workload_checksum, std::uint64_t capacity_model_checksum,
    SequentialNegotiatedTerminalReason terminal_reason,
    SequentialNegotiatedBaselineCounters counters,
    std::vector<SequentialNegotiatedColumnRecord> columns,
    std::vector<SequentialNegotiatedSweepRecord> sweeps, std::vector<CandidatePool> final_pools,
    OneWorldAllocation final_world, NegotiatedPriceState successor_price_state,
    std::uint64_t session_checksum) noexcept
    : config_(std::move(config)),
      batch_identity_(batch_identity),
      workload_checksum_(workload_checksum),
      capacity_model_checksum_(capacity_model_checksum),
      terminal_reason_(terminal_reason),
      counters_(counters),
      columns_(std::move(columns)),
      sweeps_(std::move(sweeps)),
      final_pools_(std::move(final_pools)),
      final_world_(std::move(final_world)),
      successor_price_state_(std::move(successor_price_state)),
      session_checksum_(session_checksum) {}

template <bool CaptureOperationalProfile>
SequentialNegotiatedBaselineExecution ExecuteSequentialNegotiatedBaselineImpl(
    const board_ir::BoardSnapshot& board, const MultiNetWorkload& workload,
    const ResourceCapacityModel& capacities, const SequentialNegotiatedBaselineConfig& config,
    SequentialNegotiatedBaselineOperationalProfileV1* operational_profile) {
  ::apgar::internal::OperationalTimestamp<CaptureOperationalProfile, OperationalClock>
      initialization_start;
  if constexpr (CaptureOperationalProfile) {
    *operational_profile = {};
    initialization_start =
        ::apgar::internal::OperationalNow<CaptureOperationalProfile, OperationalClock>();
  }
  return WithFailureEnvelope([&]() -> SequentialNegotiatedBaselineExecution {
    if (const std::optional<SequentialNegotiatedBaselineError> failure =
            ValidateConfigAndBounds(board, workload, capacities, config);
        failure.has_value()) {
      return *failure;
    }
    NegotiatedPriceStateResult initial_result = BuildInitialNegotiatedPriceState(
        kNegotiatedPriceStateSchemaVersion, capacities, workload, config.price_config);
    if (const auto* failure = std::get_if<NegotiatedPriceError>(&initial_result);
        failure != nullptr) {
      return Error(failure->code == NegotiatedPriceErrorCode::kResourceExhausted
                       ? SequentialNegotiatedBaselineErrorCode::kResourceExhausted
                       : SequentialNegotiatedBaselineErrorCode::kInvalidConfiguration,
                   "allocator.sequential_negotiated.initial_price_state.v1",
                   "Initial negotiated price state cannot be constructed");
    }
    NegotiatedPriceState price_state = std::get<NegotiatedPriceState>(std::move(initial_result));
    const std::uint64_t batch_identity = internal::ComputeSequentialNegotiatedBatchIdentityV1(
        board.content_hash(), workload.workload_checksum(), price_state.capacity_model_checksum(),
        config);

    const std::size_t net_count = workload.nets().size();
    std::vector<candidates::StoredCandidate> winners(net_count);
    Occupancy occupancy;
    SequentialNegotiatedBaselineCounters counters;
    std::vector<SequentialNegotiatedColumnRecord> columns;
    columns.reserve(
        static_cast<std::size_t>(static_cast<std::uint64_t>(net_count) * config.maximum_sweeps));
    std::vector<SequentialNegotiatedSweepRecord> sweeps;
    sweeps.reserve(config.maximum_sweeps);
    std::vector<CandidatePool> final_pools;
    OneWorldAllocation final_world;
    SequentialNegotiatedTerminalReason terminal_reason =
        SequentialNegotiatedTerminalReason::kSweepBudgetExhausted;
    std::uint64_t retained_candidate_bytes = 0;

    if constexpr (CaptureOperationalProfile) {
      operational_profile->validation_and_initialization_wall_nanoseconds =
          OperationalElapsed(initialization_start);
    }
    for (std::uint32_t sweep_index = 0; sweep_index < config.maximum_sweeps; ++sweep_index) {
      if (sweep_index != 0) {
        final_pools.clear();
        final_world = OneWorldAllocation{};
      }
      bool winners_unchanged = sweep_index != 0;
      const std::uint64_t first_query_identity = counters.route_queries + 1U;
      for (std::size_t net_index = 0; net_index < net_count; ++net_index) {
        const PreparedNetRoutingContext& context = workload.nets()[net_index];
        const candidates::StoredCandidate prior = winners[net_index];
        if (prior != nullptr) {
          ::apgar::internal::OperationalTimestamp<CaptureOperationalProfile, OperationalClock>
              resource_start;
          if constexpr (CaptureOperationalProfile) {
            resource_start =
                ::apgar::internal::OperationalNow<CaptureOperationalProfile, OperationalClock>();
          }
          if (const auto failure = ApplyCandidateUsage(prior, false, occupancy, config, counters);
              failure.has_value()) {
            return *failure;
          }
          if constexpr (CaptureOperationalProfile) {
            AccumulateOperationalElapsed(
                &operational_profile->incremental_resource_accumulation_wall_nanoseconds,
                resource_start);
          }
        }

        ::apgar::internal::OperationalTimestamp<CaptureOperationalProfile, OperationalClock>
            scheduling_start;
        if constexpr (CaptureOperationalProfile) {
          scheduling_start =
              ::apgar::internal::OperationalNow<CaptureOperationalProfile, OperationalClock>();
        }
        auto policy_result = BuildQueryPolicy(context, capacities, price_state, occupancy, config,
                                              sweep_index, counters);
        if (const auto* failure = std::get_if<SequentialNegotiatedBaselineError>(&policy_result);
            failure != nullptr) {
          return *failure;
        }
        SynthesizedQueryPolicy synthesized =
            std::get<SynthesizedQueryPolicy>(std::move(policy_result));
        routing::NormalizedCandidateGenerationPolicy normalized = std::move(synthesized.normalized);
        routing::PlanarRouteRequest request = context.request;
        request.candidate_policy = normalized.policy;
        if constexpr (CaptureOperationalProfile) {
          AccumulateOperationalElapsed(
              &operational_profile->scheduling_and_policy_projection_wall_nanoseconds,
              scheduling_start);
        }
        const std::uint64_t query_identity = ++counters.route_queries;
        SequentialNegotiatedColumnRecord column;
        column.sweep_index = sweep_index;
        column.net = context.request.net;
        column.policy_identity = normalized.identity;
        column.batch_identity = batch_identity;
        column.query_identity = query_identity;
        column.prospective_congestion_resource_count = synthesized.congestion_resource_count;
        column.total_prospective_congestion_penalty = synthesized.total_congestion_penalty;

        const auto restore_prior = [&]() -> std::optional<SequentialNegotiatedBaselineError> {
          if (prior == nullptr) {
            winners[net_index].reset();
            return std::nullopt;
          }
          ::apgar::internal::OperationalTimestamp<CaptureOperationalProfile, OperationalClock>
              resource_start;
          if constexpr (CaptureOperationalProfile) {
            resource_start =
                ::apgar::internal::OperationalNow<CaptureOperationalProfile, OperationalClock>();
          }
          if (const auto failure = ApplyCandidateUsage(prior, true, occupancy, config, counters);
              failure.has_value()) {
            return failure;
          }
          if constexpr (CaptureOperationalProfile) {
            AccumulateOperationalElapsed(
                &operational_profile->incremental_resource_accumulation_wall_nanoseconds,
                resource_start);
          }
          winners[net_index] = prior;
          column.retained_prior_candidate = true;
          ++counters.restored_prior_routes;
          return std::nullopt;
        };

        ::apgar::internal::OperationalTimestamp<CaptureOperationalProfile, OperationalClock>
            route_start;
        if constexpr (CaptureOperationalProfile) {
          route_start =
              ::apgar::internal::OperationalNow<CaptureOperationalProfile, OperationalClock>();
        }
        routing::CpuRouteResult route_result =
            routing::RouteWithCpuAStar(board, context.compiled_board, request, config.route_limits);
        if constexpr (CaptureOperationalProfile) {
          AccumulateOperationalElapsed(&operational_profile->candidate_generation_wall_nanoseconds,
                                       route_start);
        }
        if (auto* failure = std::get_if<routing::RouteFailure>(&route_result); failure != nullptr) {
          column.route_work_units = RouteWork(*failure);
          if (const auto work_failure =
                  AddRouteWork(column.route_work_units, config, counters, context.request.net);
              work_failure.has_value()) {
            return *work_failure;
          }
          if (failure->code == routing::RouteFailureCode::kWorkBoundExceeded) {
            return Error(SequentialNegotiatedBaselineErrorCode::kWorkBoundExceeded,
                         "allocator.sequential_negotiated.cpu_astar_bound.v1",
                         "Bounded CPU A* exhausted a per-query work or container bound",
                         context.request.net);
          }
          if (failure->code == routing::RouteFailureCode::kResourceExhausted) {
            return Error(SequentialNegotiatedBaselineErrorCode::kResourceExhausted,
                         "allocator.sequential_negotiated.cpu_astar_resource.v1",
                         "CPU A* exhausted a non-work routing resource", context.request.net);
          }
          if (failure->code != routing::RouteFailureCode::kDisconnected &&
              failure->code != routing::RouteFailureCode::kUnsupportedLayerTransition &&
              failure->code != routing::RouteFailureCode::kUnsupportedPolicy) {
            return Error(SequentialNegotiatedBaselineErrorCode::kCandidateGeneration,
                         "allocator.sequential_negotiated.cpu_astar_failure.v1",
                         "CPU A* failed outside the declared disconnected/unsupported outcomes",
                         context.request.net);
          }
          column.outcome = failure->code == routing::RouteFailureCode::kDisconnected
                               ? SequentialNegotiatedColumnOutcome::kRouteDisconnected
                               : SequentialNegotiatedColumnOutcome::kRouteUnsupported;
          ++counters.rejected_routes;
          if (const auto restore_failure = restore_prior(); restore_failure.has_value()) {
            return *restore_failure;
          }
          columns.push_back(std::move(column));
          continue;
        }

        routing::CpuRoute route = std::get<routing::CpuRoute>(std::move(route_result));
        column.route_work_units = route.telemetry.work_units;
        if (const auto work_failure =
                AddRouteWork(column.route_work_units, config, counters, context.request.net);
            work_failure.has_value()) {
          return *work_failure;
        }
        ++counters.successful_routes;
        ::apgar::internal::OperationalTimestamp<CaptureOperationalProfile, OperationalClock>
            draft_start;
        if constexpr (CaptureOperationalProfile) {
          draft_start =
              ::apgar::internal::OperationalNow<CaptureOperationalProfile, OperationalClock>();
        }
        candidates::CandidateDraftBuildResult draft_result =
            candidates::BuildGeneratedCandidateFromCpuRoute(
                board, context.compiled_board, request, normalized, route,
                candidates::CandidateSchedulingIdentity{.batch_identity = batch_identity,
                                                        .query_identity = query_identity});
        if constexpr (CaptureOperationalProfile) {
          AccumulateOperationalElapsed(&operational_profile->candidate_generation_wall_nanoseconds,
                                       draft_start);
        }
        if (auto* rejection = std::get_if<candidates::CandidateRejection>(&draft_result);
            rejection != nullptr) {
          column.outcome = SequentialNegotiatedColumnOutcome::kBuildRejected;
          column.candidate_id = rejection->candidate_id;
          column.candidate_payload_checksum = rejection->candidate_payload_checksum;
          ++counters.rejected_routes;
          if (const auto rejection_failure =
                  RetainColumnRejection(std::move(*rejection), config, counters, column);
              rejection_failure.has_value()) {
            return *rejection_failure;
          }
          if (const auto restore_failure = restore_prior(); restore_failure.has_value()) {
            return *restore_failure;
          }
          columns.push_back(std::move(column));
          continue;
        }
        candidates::GeneratedRouteCandidate generated =
            std::get<candidates::GeneratedRouteCandidate>(std::move(draft_result));
        column.candidate_id = generated.id;
        column.candidate_payload_checksum = generated.payload_checksum;
        if (generated.logical_bytes > config.limits.maximum_candidate_draft_bytes) {
          return Error(SequentialNegotiatedBaselineErrorCode::kWorkBoundExceeded,
                       "allocator.sequential_negotiated.draft_byte_bound.v1",
                       "Generated candidate exceeds the configured per-query draft-byte bound",
                       context.request.net, generated.logical_bytes,
                       config.limits.maximum_candidate_draft_bytes);
        }

        ::apgar::internal::OperationalTimestamp<CaptureOperationalProfile, OperationalClock>
            admission_start;
        if constexpr (CaptureOperationalProfile) {
          admission_start =
              ::apgar::internal::OperationalNow<CaptureOperationalProfile, OperationalClock>();
        }
        candidates::CandidateStoreConfig admission_config = config.admission_store_config;
#ifdef APGAR_SEQUENTIAL_NEGOTIATED_BASELINE_FAULT_TEST_VARIANT
        if (g_admission_budget_fault_query.load() == query_identity) {
          admission_config.maximum_admission_work_units_per_transaction = 1;
          g_admission_budget_fault_query.store(0);
        }
#endif
        candidates::CandidateStore admission_store(admission_config);
        if (!admission_store.valid()) {
          return Error(SequentialNegotiatedBaselineErrorCode::kInternalInvariant,
                       "allocator.sequential_negotiated.admission_replay.v1",
                       "Validated per-query admission configuration became invalid",
                       context.request.net);
        }
        const candidates::CandidateAdmissionContext admission_context{
            .board = board, .compiled_board = context.compiled_board, .request = request};
        candidates::CandidateStoreAdmissionResult admission =
            admission_store.Admit(admission_context, std::move(generated));
        if constexpr (CaptureOperationalProfile) {
          AccumulateOperationalElapsed(
              &operational_profile->exact_admission_and_store_publication_wall_nanoseconds,
              admission_start);
        }
        if (auto* rejection = std::get_if<candidates::CandidateRejection>(&admission);
            rejection != nullptr) {
          column.outcome = SequentialNegotiatedColumnOutcome::kAdmissionRejected;
          column.candidate_id = rejection->candidate_id;
          column.candidate_payload_checksum = rejection->candidate_payload_checksum;
          ++counters.rejected_routes;
          if (const auto rejection_failure =
                  RetainColumnRejection(std::move(*rejection), config, counters, column);
              rejection_failure.has_value()) {
            return *rejection_failure;
          }
          if (const auto restore_failure = restore_prior(); restore_failure.has_value()) {
            return *restore_failure;
          }
          columns.push_back(std::move(column));
          continue;
        }

        candidates::StoredCandidate admitted =
            std::get<candidates::StoredCandidate>(std::move(admission));
        column.outcome = SequentialNegotiatedColumnOutcome::kAdmitted;
        column.candidate_id = admitted->id();
        column.candidate_payload_checksum = admitted->data().payload_checksum;
        column.candidate_semantic_checksum =
            internal::ComputeSequentialNegotiatedCandidateSemanticChecksumV1(*admitted);
        winners_unchanged = winners_unchanged && SameWinnerSemantics(prior, admitted);
        const std::uint64_t prior_bytes = prior == nullptr ? 0 : prior->logical_bytes();
        if (retained_candidate_bytes < prior_bytes) {
          return Error(SequentialNegotiatedBaselineErrorCode::kInternalInvariant,
                       "allocator.sequential_negotiated.candidate_byte_underflow.v1",
                       "Current winner byte accounting cannot remove the prior candidate",
                       context.request.net);
        }
        const UWide next_retained =
            static_cast<UWide>(retained_candidate_bytes - prior_bytes) + admitted->logical_bytes();
        if (next_retained > config.limits.maximum_retained_candidate_bytes) {
          return Error(SequentialNegotiatedBaselineErrorCode::kWorkBoundExceeded,
                       "allocator.sequential_negotiated.candidate_byte_runtime.v1",
                       "Current winner roster reached its retained candidate-byte bound",
                       context.request.net, NarrowWitness(next_retained),
                       config.limits.maximum_retained_candidate_bytes);
        }
        retained_candidate_bytes = static_cast<std::uint64_t>(next_retained);
        counters.peak_retained_candidate_bytes =
            std::max(counters.peak_retained_candidate_bytes, retained_candidate_bytes);
        ::apgar::internal::OperationalTimestamp<CaptureOperationalProfile, OperationalClock>
            resource_start;
        if constexpr (CaptureOperationalProfile) {
          resource_start =
              ::apgar::internal::OperationalNow<CaptureOperationalProfile, OperationalClock>();
        }
        if (const auto failure = ApplyCandidateUsage(admitted, true, occupancy, config, counters);
            failure.has_value()) {
          return *failure;
        }
        if constexpr (CaptureOperationalProfile) {
          AccumulateOperationalElapsed(
              &operational_profile->incremental_resource_accumulation_wall_nanoseconds,
              resource_start);
        }
        winners[net_index] = std::move(admitted);
        ++counters.admitted_candidates;
        columns.push_back(std::move(column));
      }

      ::apgar::internal::OperationalTimestamp<CaptureOperationalProfile, OperationalClock>
          selection_start;
      if constexpr (CaptureOperationalProfile) {
        selection_start =
            ::apgar::internal::OperationalNow<CaptureOperationalProfile, OperationalClock>();
      }
      final_pools = PoolsFor(workload, winners);
      NegotiatedPriceSnapshotResult snapshot_result =
          BuildPriceSnapshotForState(capacities, price_state);
      if (const auto* failure = std::get_if<NegotiatedPriceError>(&snapshot_result);
          failure != nullptr) {
        return Error(failure->code == NegotiatedPriceErrorCode::kResourceExhausted
                         ? SequentialNegotiatedBaselineErrorCode::kResourceExhausted
                         : SequentialNegotiatedBaselineErrorCode::kPriceUpdate,
                     "allocator.sequential_negotiated.price_snapshot.v1",
                     "Source negotiated state cannot produce a one-world price snapshot");
      }
      OneWorldAllocationRequest request{
          .schema_version = kOneWorldAllocationSchemaVersion,
          .associations = capacities.associations(),
          .capacities = capacities,
          .prices = std::get<PriceSnapshot>(std::move(snapshot_result)),
          .intrinsic_cost_weight = config.intrinsic_cost_weight,
          .limits = config.allocator_limits,
          .pools = final_pools,
          .workload = &workload,
      };
      OneWorldAllocationResult world_result = AllocateOneWorld(request);
      if (const auto* failure = std::get_if<AllocationError>(&world_result); failure != nullptr) {
        return Error(failure->code == AllocationErrorCode::kResourceExhausted
                         ? SequentialNegotiatedBaselineErrorCode::kResourceExhausted
                         : SequentialNegotiatedBaselineErrorCode::kAllocation,
                     "allocator.sequential_negotiated.world.v1",
                     "Independent one-world accounting rejected the completed sweep");
      }
      final_world = std::get<OneWorldAllocation>(std::move(world_result));
      if (!WorldMatchesOccupancy(final_world, occupancy)) {
        return Error(SequentialNegotiatedBaselineErrorCode::kInternalInvariant,
                     "allocator.sequential_negotiated.occupancy_oracle.v1",
                     "Baseline-local occupancy differs from independent one-world accounting");
      }
      if constexpr (CaptureOperationalProfile) {
        AccumulateOperationalElapsed(
            &operational_profile->sweep_selection_and_resource_replay_wall_nanoseconds,
            selection_start);
      }
      ::apgar::internal::OperationalTimestamp<CaptureOperationalProfile, OperationalClock>
          price_start;
      if constexpr (CaptureOperationalProfile) {
        price_start =
            ::apgar::internal::OperationalNow<CaptureOperationalProfile, OperationalClock>();
      }
      NegotiatedPriceStateResult update_result =
          UpdateNegotiatedPrices(price_state, request, final_world);
      if (const auto* failure = std::get_if<NegotiatedPriceError>(&update_result);
          failure != nullptr) {
        return Error(failure->code == NegotiatedPriceErrorCode::kResourceExhausted
                         ? SequentialNegotiatedBaselineErrorCode::kResourceExhausted
                         : SequentialNegotiatedBaselineErrorCode::kPriceUpdate,
                     "allocator.sequential_negotiated.price_update.v1",
                     "Completed sweep cannot update negotiated prices");
      }
      NegotiatedPriceState successor = std::get<NegotiatedPriceState>(std::move(update_result));
      if constexpr (CaptureOperationalProfile) {
        AccumulateOperationalElapsed(&operational_profile->price_update_wall_nanoseconds,
                                     price_start);
      }
      ++counters.completed_sweeps;
      ++counters.price_updates;
      const bool prices_unchanged = PricesHaveSameValues(price_state, successor);
      sweeps.push_back(SequentialNegotiatedSweepRecord{
          .sweep_index = sweep_index,
          .source_price_iteration = price_state.iteration(),
          .source_price_state_checksum = price_state.state_checksum(),
          .successor_price_state_checksum = successor.state_checksum(),
          .world_checksum = final_world.world_checksum,
          .first_query_identity = first_query_identity,
          .route_query_count = net_count,
          .selected_net_count = final_world.selected_net_count,
          .no_candidate_net_count = final_world.no_candidate_net_count,
          .overused_resource_count = final_world.overused_resource_count,
          .total_overuse_units = final_world.total_overuse_units,
          .total_intrinsic_cost = final_world.total_intrinsic_cost,
          .winners_unchanged = winners_unchanged,
          .price_values_unchanged = prices_unchanged,
      });
      price_state = std::move(successor);

      if (final_world.no_candidate_net_count != 0) {
        terminal_reason = SequentialNegotiatedTerminalReason::kNoAdmissibleCandidate;
        break;
      }
      if (final_world.total_overuse_units == 0) {
        terminal_reason = SequentialNegotiatedTerminalReason::kFeasible;
        break;
      }
      if (winners_unchanged && prices_unchanged) {
        terminal_reason = SequentialNegotiatedTerminalReason::kFixedPointStalled;
        break;
      }
    }

    ::apgar::internal::OperationalTimestamp<CaptureOperationalProfile, OperationalClock>
        final_start;
    if constexpr (CaptureOperationalProfile) {
      final_start =
          ::apgar::internal::OperationalNow<CaptureOperationalProfile, OperationalClock>();
    }
    const std::uint64_t session_checksum = internal::ComputeSequentialNegotiatedSessionChecksumV1(
        config, batch_identity, workload.workload_checksum(), price_state.capacity_model_checksum(),
        terminal_reason, counters, columns, sweeps, final_pools, final_world, price_state);
    SequentialNegotiatedBaselineResult result(
        config, batch_identity, workload.workload_checksum(), price_state.capacity_model_checksum(),
        terminal_reason, counters, std::move(columns), std::move(sweeps), std::move(final_pools),
        std::move(final_world), std::move(price_state), session_checksum);
    if constexpr (CaptureOperationalProfile) {
      AccumulateOperationalElapsed(&operational_profile->final_assembly_wall_nanoseconds,
                                   final_start);
    }
    if constexpr (CaptureOperationalProfile) {
      operational_profile->component_wall_nanoseconds = OperationalElapsed(initialization_start);
      const UWide classified =
          static_cast<UWide>(operational_profile->validation_and_initialization_wall_nanoseconds) +
          operational_profile->scheduling_and_policy_projection_wall_nanoseconds +
          operational_profile->candidate_generation_wall_nanoseconds +
          operational_profile->exact_admission_and_store_publication_wall_nanoseconds +
          operational_profile->incremental_resource_accumulation_wall_nanoseconds +
          operational_profile->sweep_selection_and_resource_replay_wall_nanoseconds +
          operational_profile->price_update_wall_nanoseconds +
          operational_profile->final_assembly_wall_nanoseconds;
      operational_profile->unclassified_serial_wall_nanoseconds =
          classified <= operational_profile->component_wall_nanoseconds
              ? operational_profile->component_wall_nanoseconds -
                    static_cast<std::uint64_t>(classified)
              : 0;
    }
    return result;
  });
}

SequentialNegotiatedBaselineExecution ExecuteSequentialNegotiatedBaseline(
    const board_ir::BoardSnapshot& board, const MultiNetWorkload& workload,
    const ResourceCapacityModel& capacities, const SequentialNegotiatedBaselineConfig& config) {
  return ExecuteSequentialNegotiatedBaselineImpl<false>(board, workload, capacities, config,
                                                        nullptr);
}

SequentialNegotiatedBaselineExecution ExecuteSequentialNegotiatedBaselineWithOperationalProfileV1(
    const board_ir::BoardSnapshot& board, const MultiNetWorkload& workload,
    const ResourceCapacityModel& capacities, const SequentialNegotiatedBaselineConfig& config,
    SequentialNegotiatedBaselineOperationalProfileV1& operational_profile) {
  return ExecuteSequentialNegotiatedBaselineImpl<true>(board, workload, capacities, config,
                                                       &operational_profile);
}

}  // namespace apgar::allocator
