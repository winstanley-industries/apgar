#include "apgar/allocator/negotiated_prices.h"

#include <algorithm>
#include <limits>
#include <new>
#include <ranges>
#include <stdexcept>
#include <utility>

#include "apgar/board_ir/stable_hash.h"
#include "src/allocator/negotiated_prices_internal.h"
#include "src/allocator/one_world_internal.h"

namespace apgar::allocator {
namespace {

using UWide = __uint128_t;

[[nodiscard]] NegotiatedPriceError Error(NegotiatedPriceErrorCode code,
                                         std::string_view invariant_id,
                                         std::string_view detail) noexcept {
  return NegotiatedPriceError{.code = code, .invariant_id = invariant_id, .detail = detail};
}

[[nodiscard]] bool ConfigIsValid(const NegotiatedPriceConfig& config) noexcept {
  return config.present_step_per_overuse_unit > 0 && config.history_step_per_overuse_unit > 0 &&
         config.maximum_price_per_resource > 0 && config.maximum_iterations > 0 &&
         config.maximum_iterations <= kMaximumNegotiatedPriceIterationsV1 &&
         config.maximum_price_records > 0 &&
         config.maximum_price_records <= kMaximumAllocatorResourceRecordsV1;
}

[[nodiscard]] bool AssociationsMatch(const AllocationAssociations& left,
                                     const AllocationAssociations& right) noexcept {
  return left == right && left.board_content_hash != 0 && left.compiler_profile_fingerprint != 0 &&
         left.geometry_compiler_version != 0;
}

[[nodiscard]] bool IsCanonicalResource(const routing::EdgeResourceKey& resource) noexcept {
  if (static_cast<std::uint8_t>(resource.direction) >
      static_cast<std::uint8_t>(geometry_compiler::Direction::kNorthWest)) {
    return false;
  }
  const geometry_compiler::DirectionDelta delta = geometry_compiler::DeltaFor(resource.direction);
  const __int128_t endpoint_x =
      static_cast<__int128_t>(resource.lattice_x) + static_cast<__int128_t>(delta.x);
  const __int128_t endpoint_y =
      static_cast<__int128_t>(resource.lattice_y) + static_cast<__int128_t>(delta.y);
  return endpoint_x >= std::numeric_limits<std::int64_t>::min() &&
         endpoint_x <= std::numeric_limits<std::int64_t>::max() &&
         endpoint_y >= std::numeric_limits<std::int64_t>::min() &&
         endpoint_y <= std::numeric_limits<std::int64_t>::max();
}

void AddResourceKey(board_ir::StableHashBuilder& hash,
                    const routing::EdgeResourceKey& resource) noexcept {
  hash.AddU32(resource.layer);
  hash.AddI64(resource.lattice_x);
  hash.AddI64(resource.lattice_y);
  hash.AddByte(static_cast<std::uint8_t>(resource.direction));
}

[[nodiscard]] std::uint64_t ComputeCapacityModelChecksumV1(
    const ResourceCapacityModel& capacities) noexcept {
  return internal::ComputeResourceCapacityModelChecksumV1(
      internal::ResourceCapacityChecksumHeaderV1{
          .schema_version = capacities.schema_version(),
          .associations = capacities.associations(),
          .default_capacity_units = capacities.default_capacity_units(),
      },
      capacities.overrides());
}

[[nodiscard]] internal::NegotiatedPriceChecksumHeaderV1 ChecksumHeader(
    const NegotiatedPriceState& state) noexcept {
  return internal::NegotiatedPriceChecksumHeaderV1{
      .schema_version = state.schema_version(),
      .associations = state.associations(),
      .workload_checksum = state.workload_checksum(),
      .capacity_model_checksum = state.capacity_model_checksum(),
      .config = state.config(),
      .iteration = state.iteration(),
      .predecessor_state_checksum = state.predecessor_state_checksum(),
      .source_world_checksum = state.source_world_checksum(),
      .clamped_resource_count = state.clamped_resource_count(),
      .total_present_price = state.total_present_price(),
      .total_history_price = state.total_history_price(),
      .total_price = state.total_price(),
  };
}

[[nodiscard]] std::optional<NegotiatedPriceError> ValidateState(
    const ResourceCapacityModel& capacities, const NegotiatedPriceState& state) noexcept {
  if (state.schema_version() != kNegotiatedPriceStateSchemaVersion) {
    return Error(NegotiatedPriceErrorCode::kUnsupportedSchema,
                 "allocator.negotiated_price.schema.v1",
                 "Negotiated price-state schema is unsupported");
  }
  if (!ConfigIsValid(state.config())) {
    return Error(NegotiatedPriceErrorCode::kInvalidConfiguration,
                 "allocator.negotiated_price.configuration.v1",
                 "Negotiated price configuration is outside schema-v1 bounds");
  }
  if (!AssociationsMatch(capacities.associations(), state.associations())) {
    return Error(NegotiatedPriceErrorCode::kAssociationMismatch,
                 "allocator.negotiated_price.capacity_association.v1",
                 "Negotiated price state and capacity model associations differ");
  }
  if (state.capacity_model_checksum() != ComputeCapacityModelChecksumV1(capacities)) {
    return Error(NegotiatedPriceErrorCode::kInvalidState,
                 "allocator.negotiated_price.capacity_identity.v1",
                 "Negotiated price state belongs to a different exact capacity model");
  }
  if (state.workload_checksum() == 0 || state.iteration() > state.config().maximum_iterations ||
      state.prices().size() > state.config().maximum_price_records ||
      !internal::NegotiatedPriceRosterFitsV1(capacities.overrides().size(), state.prices().size(),
                                             state.config().maximum_price_records) ||
      (state.iteration() == 0 &&
       (state.predecessor_state_checksum() != 0 || state.source_world_checksum() != 0))) {
    return Error(NegotiatedPriceErrorCode::kInvalidState,
                 "allocator.negotiated_price.state_shape.v1",
                 "Negotiated price state has an invalid workload, iteration, or record count");
  }

  UWide total_present = 0;
  UWide total_history = 0;
  UWide total_price = 0;
  std::uint64_t clamped_count = 0;
  const routing::EdgeResourceKey* previous_resource = nullptr;
  for (const NegotiatedResourcePrice& price : state.prices()) {
    if (!IsCanonicalResource(price.resource) ||
        (previous_resource != nullptr && !(*previous_resource < price.resource)) ||
        price.total_price == 0 || price.present_price > state.config().maximum_price_per_resource ||
        price.history_price > state.config().maximum_price_per_resource ||
        price.total_price > state.config().maximum_price_per_resource) {
      return Error(NegotiatedPriceErrorCode::kInvalidState, "allocator.negotiated_price.record.v1",
                   "Negotiated resource-price records are not canonical, unique, or bounded");
    }
    const UWide raw_total =
        static_cast<UWide>(price.present_price) + static_cast<UWide>(price.history_price);
    const std::uint64_t expected_total = static_cast<std::uint64_t>(
        std::min<UWide>(raw_total, state.config().maximum_price_per_resource));
    if (price.total_price != expected_total ||
        price.total_clamped != (raw_total > state.config().maximum_price_per_resource)) {
      return Error(NegotiatedPriceErrorCode::kInvalidState, "allocator.negotiated_price.total.v1",
                   "Negotiated resource total is inconsistent with present/history prices");
    }
    total_present += price.present_price;
    total_history += price.history_price;
    total_price += price.total_price;
    if (price.present_clamped || price.history_clamped || price.total_clamped) {
      ++clamped_count;
    }
    previous_resource = &price.resource;
  }
  if (total_present > std::numeric_limits<std::uint64_t>::max() ||
      total_history > std::numeric_limits<std::uint64_t>::max() ||
      total_price > std::numeric_limits<std::uint64_t>::max() ||
      state.total_present_price() != static_cast<std::uint64_t>(total_present) ||
      state.total_history_price() != static_cast<std::uint64_t>(total_history) ||
      state.total_price() != static_cast<std::uint64_t>(total_price) ||
      state.clamped_resource_count() != clamped_count ||
      state.state_checksum() !=
          internal::ComputeNegotiatedPriceStateChecksumV1(ChecksumHeader(state), state.prices())) {
    return Error(NegotiatedPriceErrorCode::kInvalidState,
                 "allocator.negotiated_price.state_integrity.v1",
                 "Negotiated price totals or replay checksum are inconsistent");
  }
  return std::nullopt;
}

[[nodiscard]] std::uint32_t CapacityFor(const ResourceCapacityModel& capacities,
                                        const routing::EdgeResourceKey& resource,
                                        bool& explicit_override) noexcept {
  const auto found = std::ranges::lower_bound(capacities.overrides(), resource, {},
                                              &ResourceCapacityOverride::resource);
  explicit_override = found != capacities.overrides().end() && found->resource == resource;
  return explicit_override ? found->capacity_units : capacities.default_capacity_units();
}

[[nodiscard]] std::optional<NegotiatedPriceError> ValidateWorld(
    const NegotiatedPriceState& state, const OneWorldAllocationRequest& source_request,
    const OneWorldAllocation& world) {
  // Reject public vector sizes before traversing either vector for a replay
  // checksum or constructing any validation scratch state.
  if (world.selections.size() > kMaximumAllocatorNetsV1 ||
      world.resources.size() > kMaximumAllocatorResourceRecordsV1) {
    return Error(NegotiatedPriceErrorCode::kRecordBoundExceeded,
                 "allocator.negotiated_price.world_input_count.v1",
                 "One-world selections or resources exceed schema-v1 hard bounds");
  }
  if (world.schema_version != kOneWorldAllocationSchemaVersion ||
      source_request.schema_version != kOneWorldAllocationSchemaVersion ||
      source_request.associations != state.associations() ||
      !AssociationsMatch(state.associations(), world.associations)) {
    return Error(NegotiatedPriceErrorCode::kAssociationMismatch,
                 "allocator.negotiated_price.world_association.v1",
                 "One-world result schema or association differs from the price state");
  }
  if (source_request.workload == nullptr ||
      source_request.workload->schema_version() != kMultiNetWorkloadSchemaVersion ||
      source_request.workload->board_content_hash() != state.associations().board_content_hash ||
      source_request.workload->compiler_profile_fingerprint() !=
          state.associations().compiler_profile_fingerprint ||
      source_request.workload->geometry_compiler_version() !=
          state.associations().geometry_compiler_version ||
      source_request.workload->workload_checksum() != state.workload_checksum() ||
      world.workload_checksum != state.workload_checksum() ||
      world.selections.size() != source_request.workload->nets().size()) {
    return Error(NegotiatedPriceErrorCode::kWorkloadMismatch,
                 "allocator.negotiated_price.workload.v1",
                 "One-world selections do not belong to the supplied authentic workload");
  }
  if (world.default_capacity_units != source_request.capacities.default_capacity_units()) {
    return Error(NegotiatedPriceErrorCode::kInvalidWorld,
                 "allocator.negotiated_price.world_default_capacity.v1",
                 "One-world default capacity differs from the exact capacity model");
  }
  if (world.price_iteration != state.iteration() ||
      world.world_checksum != internal::ComputeOneWorldChecksumV2(world)) {
    return Error(NegotiatedPriceErrorCode::kInvalidWorld,
                 "allocator.negotiated_price.world_replay.v1",
                 "One-world iteration or replay checksum is inconsistent");
  }

  std::size_t state_index = 0;
  std::size_t override_matches = 0;
  UWide total_overuse = 0;
  std::uint64_t overused_count = 0;
  const routing::EdgeResourceKey* previous_resource = nullptr;
  for (const ResourceUsage& usage : world.resources) {
    if (!IsCanonicalResource(usage.resource) ||
        (previous_resource != nullptr && !(*previous_resource < usage.resource))) {
      return Error(NegotiatedPriceErrorCode::kInvalidWorld,
                   "allocator.negotiated_price.world_resource_order.v1",
                   "One-world resources are not canonical and strictly ordered");
    }
    bool capacity_explicit = false;
    const std::uint32_t capacity =
        CapacityFor(source_request.capacities, usage.resource, capacity_explicit);
    if (usage.has_capacity_override != capacity_explicit || usage.capacity_units != capacity ||
        usage.overuse_units != (usage.usage_units > capacity ? usage.usage_units - capacity : 0)) {
      return Error(NegotiatedPriceErrorCode::kInvalidWorld,
                   "allocator.negotiated_price.world_capacity.v1",
                   "One-world capacity or overuse accounting is inconsistent");
    }
    if (capacity_explicit) {
      ++override_matches;
    }
    while (state_index < state.prices().size() &&
           state.prices()[state_index].resource < usage.resource) {
      return Error(NegotiatedPriceErrorCode::kInvalidWorld,
                   "allocator.negotiated_price.world_price_roster.v1",
                   "One-world resources omit an explicit negotiated price");
    }
    const bool has_state_price = state_index < state.prices().size() &&
                                 state.prices()[state_index].resource == usage.resource;
    const std::uint64_t expected_price =
        has_state_price ? state.prices()[state_index].total_price : 0;
    if (usage.has_explicit_price != has_state_price ||
        usage.price_per_usage_unit != expected_price) {
      return Error(NegotiatedPriceErrorCode::kInvalidWorld,
                   "allocator.negotiated_price.world_price.v1",
                   "One-world explicit prices do not match the immutable price state");
    }
    if (has_state_price) {
      ++state_index;
    }
    total_overuse += usage.overuse_units;
    if (usage.overuse_units > 0) {
      ++overused_count;
    }
    previous_resource = &usage.resource;
  }
  if (state_index != state.prices().size() ||
      override_matches != source_request.capacities.overrides().size() ||
      total_overuse > std::numeric_limits<std::uint64_t>::max() ||
      world.total_overuse_units != static_cast<std::uint64_t>(total_overuse) ||
      world.overused_resource_count != overused_count) {
    return Error(NegotiatedPriceErrorCode::kInvalidWorld,
                 "allocator.negotiated_price.world_totals.v1",
                 "One-world resource roster or overuse totals are inconsistent");
  }

  // Rebuild the exact prior snapshot, require it in the original request, and
  // rerun the complete candidate pools. This authenticates not only the
  // claimed immutable winners and their footprints, but pool membership and
  // minimum-score selection against every original alternative.
  std::vector<ResourcePrice> state_prices;
  state_prices.reserve(state.prices().size());
  for (const NegotiatedResourcePrice& price : state.prices()) {
    state_prices.push_back(
        ResourcePrice{.resource = price.resource, .price_per_usage_unit = price.total_price});
  }
  PriceSnapshotResult snapshot_result =
      BuildPriceSnapshot(kPriceSnapshotSchemaVersion, source_request.capacities, state.iteration(),
                         std::move(state_prices));
  if (const auto* failure = std::get_if<AllocationError>(&snapshot_result); failure != nullptr) {
    return internal::TranslateNestedAllocationError(
        *failure, NegotiatedPriceErrorCode::kInvalidState,
        "allocator.negotiated_price.reconstruction_snapshot.v1",
        "The source request cannot reproduce the immutable price snapshot");
  }
  if (std::get<PriceSnapshot>(snapshot_result) != source_request.prices) {
    return Error(NegotiatedPriceErrorCode::kInvalidWorld,
                 "allocator.negotiated_price.source_price_snapshot.v1",
                 "The source request does not contain the exact prior price snapshot");
  }

  OneWorldAllocationResult reconstruction_result = AllocateOneWorld(source_request);
  if (const auto* failure = std::get_if<AllocationError>(&reconstruction_result);
      failure != nullptr) {
    return internal::TranslateNestedAllocationError(
        *failure, NegotiatedPriceErrorCode::kInvalidWorld,
        "allocator.negotiated_price.world_request_reconstruction.v1",
        "The original one-world request cannot be reconstructed");
  }
  const OneWorldAllocation& reconstructed = std::get<OneWorldAllocation>(reconstruction_result);
  if (reconstructed.selections.size() != world.selections.size()) {
    return Error(NegotiatedPriceErrorCode::kInvalidWorld,
                 "allocator.negotiated_price.world_selection_reconstruction.v1",
                 "One-world selection roster cannot be reconstructed");
  }
  for (std::size_t index = 0; index < world.selections.size(); ++index) {
    const NetSelection& expected = reconstructed.selections[index];
    const NetSelection& actual = world.selections[index];
    if (actual.net != expected.net || actual.status != expected.status ||
        actual.candidate_id != expected.candidate_id ||
        actual.candidate_payload_checksum != expected.candidate_payload_checksum ||
        (actual.candidate != nullptr) != (expected.candidate != nullptr) ||
        (actual.candidate != nullptr && actual.candidate->data() != expected.candidate->data()) ||
        actual.intrinsic_cost != expected.intrinsic_cost ||
        actual.price_cost != expected.price_cost ||
        actual.selection_score != expected.selection_score) {
      return Error(NegotiatedPriceErrorCode::kInvalidWorld,
                   "allocator.negotiated_price.world_selection_reconstruction.v1",
                   "One-world selection metadata differs from immutable candidate evidence");
    }
  }
  if (world.schema_version != reconstructed.schema_version ||
      world.associations != reconstructed.associations ||
      world.workload_checksum != reconstructed.workload_checksum ||
      world.price_iteration != reconstructed.price_iteration ||
      world.default_capacity_units != reconstructed.default_capacity_units ||
      world.intrinsic_cost_weight != reconstructed.intrinsic_cost_weight ||
      world.resources != reconstructed.resources ||
      world.selected_net_count != reconstructed.selected_net_count ||
      world.no_candidate_net_count != reconstructed.no_candidate_net_count ||
      world.overused_resource_count != reconstructed.overused_resource_count ||
      world.total_overuse_units != reconstructed.total_overuse_units ||
      world.total_intrinsic_cost != reconstructed.total_intrinsic_cost ||
      world.total_selection_score != reconstructed.total_selection_score ||
      world.scoring_span_queries != reconstructed.scoring_span_queries ||
      world.scoring_price_matches != reconstructed.scoring_price_matches ||
      world.selected_logical_resource_uses != reconstructed.selected_logical_resource_uses ||
      world.accounting_materialized_resource_edges !=
          reconstructed.accounting_materialized_resource_edges ||
      world.accounting_span_boundary_events != reconstructed.accounting_span_boundary_events ||
      world.world_checksum != reconstructed.world_checksum) {
    return Error(NegotiatedPriceErrorCode::kInvalidWorld,
                 "allocator.negotiated_price.world_resource_reconstruction.v1",
                 "One-world result does not reconstruct from the complete original request");
  }
  return std::nullopt;
}

template <typename Operation>
[[nodiscard]] auto WithFailureEnvelope(Operation&& operation)
    -> decltype(std::forward<Operation>(operation)()) {
  using Result = decltype(std::forward<Operation>(operation)());
  try {
    return std::forward<Operation>(operation)();
  } catch (const std::bad_alloc&) {
    return Result{Error(NegotiatedPriceErrorCode::kResourceExhausted,
                        "allocator.negotiated_price.host_memory_exhausted.v1",
                        "Host allocation failed within negotiated price bounds")};
  } catch (const std::length_error&) {
    return Result{Error(NegotiatedPriceErrorCode::kResourceExhausted,
                        "allocator.negotiated_price.host_container_exhausted.v1",
                        "Host container limits were exhausted within negotiated price bounds")};
  }
}

}  // namespace

bool internal::NegotiatedPriceConfigIsValidV1(const NegotiatedPriceConfig& config) noexcept {
  return ConfigIsValid(config);
}

std::uint64_t internal::ComputeNegotiatedPriceStateChecksumV1(
    const NegotiatedPriceChecksumHeaderV1& header,
    std::span<const NegotiatedResourcePrice> prices) noexcept {
  board_ir::StableHashBuilder hash;
  hash.AddString("APGAR-NEGOTIATED-PRICE-STATE-V1");
  hash.AddU32(header.schema_version);
  hash.AddU64(header.associations.board_content_hash);
  hash.AddU64(header.associations.compiler_profile_fingerprint);
  hash.AddU32(header.associations.geometry_compiler_version);
  hash.AddU64(header.workload_checksum);
  hash.AddU64(header.capacity_model_checksum);
  hash.AddU64(header.config.present_step_per_overuse_unit);
  hash.AddU64(header.config.history_step_per_overuse_unit);
  hash.AddU64(header.config.maximum_price_per_resource);
  hash.AddU32(header.config.maximum_iterations);
  hash.AddU64(header.config.maximum_price_records);
  hash.AddU32(header.iteration);
  hash.AddU64(header.predecessor_state_checksum);
  hash.AddU64(header.source_world_checksum);
  hash.AddU64(static_cast<std::uint64_t>(prices.size()));
  hash.AddU64(header.clamped_resource_count);
  hash.AddU64(header.total_present_price);
  hash.AddU64(header.total_history_price);
  hash.AddU64(header.total_price);
  for (const NegotiatedResourcePrice& price : prices) {
    AddResourceKey(hash, price.resource);
    hash.AddU64(price.present_price);
    hash.AddU64(price.history_price);
    hash.AddU64(price.total_price);
    hash.AddU64(price.observed_overuse_units);
    hash.AddBool(price.present_clamped);
    hash.AddBool(price.history_clamped);
    hash.AddBool(price.total_clamped);
  }
  return hash.Finish();
}

std::uint64_t internal::ComputeResourceCapacityModelChecksumV1(
    const ResourceCapacityChecksumHeaderV1& header,
    std::span<const ResourceCapacityOverride> overrides) noexcept {
  board_ir::StableHashBuilder hash;
  hash.AddString("APGAR-RESOURCE-CAPACITY-MODEL-V1");
  hash.AddU32(header.schema_version);
  hash.AddU64(header.associations.board_content_hash);
  hash.AddU64(header.associations.compiler_profile_fingerprint);
  hash.AddU32(header.associations.geometry_compiler_version);
  hash.AddU32(header.default_capacity_units);
  hash.AddU64(static_cast<std::uint64_t>(overrides.size()));
  for (const ResourceCapacityOverride& capacity : overrides) {
    AddResourceKey(hash, capacity.resource);
    hash.AddU32(capacity.capacity_units);
  }
  return hash.Finish();
}

NegotiatedPriceError internal::TranslateNestedAllocationError(
    const AllocationError& failure, NegotiatedPriceErrorCode fallback_code,
    std::string_view fallback_invariant, std::string_view fallback_detail) noexcept {
  if (failure.code == AllocationErrorCode::kResourceExhausted) {
    return Error(NegotiatedPriceErrorCode::kResourceExhausted, failure.invariant_id,
                 failure.detail);
  }
  return Error(fallback_code, fallback_invariant, fallback_detail);
}

bool internal::NegotiatedPriceRosterFitsV1(std::uint64_t capacity_records,
                                           std::uint64_t price_records,
                                           std::uint64_t configured_price_limit) noexcept {
  return price_records <= configured_price_limit &&
         internal::CombinedResourceRecordCountFitsV1(capacity_records, price_records);
}

NegotiatedPriceStateResult BuildInitialNegotiatedPriceState(std::uint32_t schema_version,
                                                            const ResourceCapacityModel& capacities,
                                                            const MultiNetWorkload& workload,
                                                            const NegotiatedPriceConfig& config) {
  if (schema_version != kNegotiatedPriceStateSchemaVersion) {
    return Error(NegotiatedPriceErrorCode::kUnsupportedSchema,
                 "allocator.negotiated_price.schema.v1",
                 "Negotiated price-state schema is unsupported");
  }
  if (!ConfigIsValid(config)) {
    return Error(NegotiatedPriceErrorCode::kInvalidConfiguration,
                 "allocator.negotiated_price.configuration.v1",
                 "Negotiated price configuration is outside schema-v1 bounds");
  }
  const AllocationAssociations workload_associations{
      .board_content_hash = workload.board_content_hash(),
      .compiler_profile_fingerprint = workload.compiler_profile_fingerprint(),
      .geometry_compiler_version = workload.geometry_compiler_version(),
  };
  if (!AssociationsMatch(capacities.associations(), workload_associations) ||
      workload.schema_version() != kMultiNetWorkloadSchemaVersion ||
      workload.workload_checksum() == 0) {
    return Error(NegotiatedPriceErrorCode::kWorkloadMismatch,
                 "allocator.negotiated_price.initial_workload.v1",
                 "Capacity model and authentic workload association differ");
  }
  const internal::NegotiatedPriceChecksumHeaderV1 header{
      .schema_version = schema_version,
      .associations = capacities.associations(),
      .workload_checksum = workload.workload_checksum(),
      .capacity_model_checksum = ComputeCapacityModelChecksumV1(capacities),
      .config = config,
  };
  const std::uint64_t checksum = internal::ComputeNegotiatedPriceStateChecksumV1(header, {});
  return NegotiatedPriceState(schema_version, capacities.associations(),
                              workload.workload_checksum(), header.capacity_model_checksum, config,
                              0, 0, 0, {}, 0, 0, 0, 0, checksum);
}

NegotiatedPriceSnapshotResult BuildPriceSnapshotForState(const ResourceCapacityModel& capacities,
                                                         const NegotiatedPriceState& state) {
  if (const std::optional<NegotiatedPriceError> error = ValidateState(capacities, state);
      error.has_value()) {
    return *error;
  }
  return WithFailureEnvelope([&]() -> NegotiatedPriceSnapshotResult {
    std::vector<ResourcePrice> prices;
    prices.reserve(state.prices().size());
    for (const NegotiatedResourcePrice& price : state.prices()) {
      prices.push_back(
          ResourcePrice{.resource = price.resource, .price_per_usage_unit = price.total_price});
    }
    PriceSnapshotResult result = BuildPriceSnapshot(kPriceSnapshotSchemaVersion, capacities,
                                                    state.iteration(), std::move(prices));
    if (auto* error = std::get_if<AllocationError>(&result); error != nullptr) {
      return internal::TranslateNestedAllocationError(
          *error, NegotiatedPriceErrorCode::kInvalidState, "allocator.negotiated_price.snapshot.v1",
          "Negotiated state cannot produce its immutable price snapshot");
    }
    return std::get<PriceSnapshot>(std::move(result));
  });
}

NegotiatedPriceStateResult UpdateNegotiatedPrices(const NegotiatedPriceState& previous,
                                                  const OneWorldAllocationRequest& source_request,
                                                  const OneWorldAllocation& world) {
  const ResourceCapacityModel& capacities = source_request.capacities;
  if (const std::optional<NegotiatedPriceError> error = ValidateState(capacities, previous);
      error.has_value()) {
    return *error;
  }
  if (previous.iteration() >= previous.config().maximum_iterations) {
    return Error(NegotiatedPriceErrorCode::kIterationBoundExceeded,
                 "allocator.negotiated_price.iteration_bound.v1",
                 "Negotiated price update reached its configured iteration bound");
  }

  return WithFailureEnvelope([&]() -> NegotiatedPriceStateResult {
    if (const std::optional<NegotiatedPriceError> error =
            ValidateWorld(previous, source_request, world);
        error.has_value()) {
      return *error;
    }
    std::vector<NegotiatedResourcePrice> prices;
    prices.reserve(
        std::min<std::size_t>(world.resources.size(), previous.config().maximum_price_records));
    std::size_t previous_index = 0;
    UWide total_present = 0;
    UWide total_history = 0;
    UWide total_price = 0;
    std::uint64_t clamped_count = 0;
    for (const ResourceUsage& usage : world.resources) {
      while (previous_index < previous.prices().size() &&
             previous.prices()[previous_index].resource < usage.resource) {
        ++previous_index;
      }
      const bool has_previous = previous_index < previous.prices().size() &&
                                previous.prices()[previous_index].resource == usage.resource;
      const std::uint64_t previous_history =
          has_previous ? previous.prices()[previous_index].history_price : 0;
      if (has_previous) {
        ++previous_index;
      }
      const UWide raw_present =
          static_cast<UWide>(usage.overuse_units) * previous.config().present_step_per_overuse_unit;
      const UWide raw_history =
          static_cast<UWide>(previous_history) +
          static_cast<UWide>(usage.overuse_units) * previous.config().history_step_per_overuse_unit;
      const std::uint64_t present = static_cast<std::uint64_t>(
          std::min<UWide>(raw_present, previous.config().maximum_price_per_resource));
      const std::uint64_t history = static_cast<std::uint64_t>(
          std::min<UWide>(raw_history, previous.config().maximum_price_per_resource));
      const UWide raw_total = static_cast<UWide>(present) + static_cast<UWide>(history);
      const std::uint64_t combined = static_cast<std::uint64_t>(
          std::min<UWide>(raw_total, previous.config().maximum_price_per_resource));
      if (combined == 0) {
        continue;
      }
      if (!internal::NegotiatedPriceRosterFitsV1(capacities.overrides().size(), prices.size() + 1U,
                                                 previous.config().maximum_price_records)) {
        return Error(NegotiatedPriceErrorCode::kRecordBoundExceeded,
                     "allocator.negotiated_price.record_bound.v1",
                     "Negotiated price update exceeds its configured sparse-record bound");
      }
      const bool present_clamped = raw_present > previous.config().maximum_price_per_resource;
      const bool history_clamped = raw_history > previous.config().maximum_price_per_resource;
      const bool total_clamped = raw_total > previous.config().maximum_price_per_resource;
      prices.push_back(NegotiatedResourcePrice{
          .resource = usage.resource,
          .present_price = present,
          .history_price = history,
          .total_price = combined,
          .observed_overuse_units = usage.overuse_units,
          .present_clamped = present_clamped,
          .history_clamped = history_clamped,
          .total_clamped = total_clamped,
      });
      total_present += present;
      total_history += history;
      total_price += combined;
      if (present_clamped || history_clamped || total_clamped) {
        ++clamped_count;
      }
    }
    if (total_present > std::numeric_limits<std::uint64_t>::max() ||
        total_history > std::numeric_limits<std::uint64_t>::max() ||
        total_price > std::numeric_limits<std::uint64_t>::max()) {
      return Error(NegotiatedPriceErrorCode::kArithmeticOverflow,
                   "allocator.negotiated_price.aggregate_overflow.v1",
                   "Negotiated price aggregate exceeds unsigned 64-bit replay fields");
    }
    const std::uint32_t iteration = previous.iteration() + 1U;
    const internal::NegotiatedPriceChecksumHeaderV1 header{
        .schema_version = kNegotiatedPriceStateSchemaVersion,
        .associations = previous.associations(),
        .workload_checksum = previous.workload_checksum(),
        .capacity_model_checksum = previous.capacity_model_checksum(),
        .config = previous.config(),
        .iteration = iteration,
        .predecessor_state_checksum = previous.state_checksum(),
        .source_world_checksum = world.world_checksum,
        .clamped_resource_count = clamped_count,
        .total_present_price = static_cast<std::uint64_t>(total_present),
        .total_history_price = static_cast<std::uint64_t>(total_history),
        .total_price = static_cast<std::uint64_t>(total_price),
    };
    const std::uint64_t checksum = internal::ComputeNegotiatedPriceStateChecksumV1(header, prices);
    return NegotiatedPriceState(
        kNegotiatedPriceStateSchemaVersion, previous.associations(), previous.workload_checksum(),
        previous.capacity_model_checksum(), previous.config(), iteration, previous.state_checksum(),
        world.world_checksum, std::move(prices), clamped_count,
        static_cast<std::uint64_t>(total_present), static_cast<std::uint64_t>(total_history),
        static_cast<std::uint64_t>(total_price), checksum);
  });
}

}  // namespace apgar::allocator
