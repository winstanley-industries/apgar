#include "apgar/allocator/negotiated_prices.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <limits>
#include <memory>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "apgar/candidates/route_candidate.h"
#include "src/allocator/negotiated_prices_internal.h"
#include "src/allocator/one_world_internal.h"
#include "tests/support/board_builder.h"
#include "tests/support/candidate_builder.h"
#include "tests/support/compiler_builder.h"
#include "tests/support/google_test.h"
#include "tests/support/routing_builder.h"

namespace apgar::allocator {
namespace {

static_assert(std::is_nothrow_copy_constructible_v<NegotiatedPriceError>);

[[nodiscard]] MultiNetWorkload BuiltWorkload(MultiNetWorkloadResult result) {
  EXPECT_TRUE(std::holds_alternative<MultiNetWorkload>(result));
  if (!std::holds_alternative<MultiNetWorkload>(result)) {
    std::abort();
  }
  return std::get<MultiNetWorkload>(std::move(result));
}

[[nodiscard]] ResourceCapacityModel BuiltCapacities(ResourceCapacityModelResult result) {
  EXPECT_TRUE(std::holds_alternative<ResourceCapacityModel>(result));
  if (!std::holds_alternative<ResourceCapacityModel>(result)) {
    std::abort();
  }
  return std::get<ResourceCapacityModel>(std::move(result));
}

[[nodiscard]] NegotiatedPriceState BuiltState(NegotiatedPriceStateResult result) {
  EXPECT_TRUE(std::holds_alternative<NegotiatedPriceState>(result));
  if (!std::holds_alternative<NegotiatedPriceState>(result)) {
    std::abort();
  }
  return std::get<NegotiatedPriceState>(std::move(result));
}

[[nodiscard]] PriceSnapshot BuiltSnapshot(NegotiatedPriceSnapshotResult result) {
  EXPECT_TRUE(std::holds_alternative<PriceSnapshot>(result));
  if (!std::holds_alternative<PriceSnapshot>(result)) {
    std::abort();
  }
  return std::get<PriceSnapshot>(std::move(result));
}

[[nodiscard]] OneWorldAllocation BuiltWorld(OneWorldAllocationResult result) {
  EXPECT_TRUE(std::holds_alternative<OneWorldAllocation>(result));
  if (!std::holds_alternative<OneWorldAllocation>(result)) {
    std::abort();
  }
  return std::get<OneWorldAllocation>(std::move(result));
}

struct PricingFixture {
  board_ir::BoardSnapshot board;
  MultiNetWorkload workload;
  ResourceCapacityModel capacities;
  std::vector<CandidatePool> pools;
};

[[nodiscard]] PricingFixture BuildPricingFixture(std::uint32_t default_capacity_units) {
  board_ir::BoardData board_data = test_support::ValidM1TwoNetBoardData();
  board_data.obstacles.clear();
  board_ir::BoardSnapshot board = test_support::Snapshot(std::move(board_data));
  const geometry_compiler::CompilerProfile compiler_profile =
      test_support::DefaultCompilerProfile({0});
  board_ir::RoutingProfile second_profile = board.data().routing_profile;
  second_profile.net = board.data().nets[1].ref;
  const std::array specs = {
      MultiNetRoutingSpec{
          .routing_profile = board.data().routing_profile, .start_layer = 0, .goal_layer = 0},
      MultiNetRoutingSpec{.routing_profile = second_profile, .start_layer = 0, .goal_layer = 0},
  };
  MultiNetWorkload workload = BuiltWorkload(BuildMultiNetWorkload(
      kMultiNetWorkloadSchemaVersion, board, compiler_profile, specs, specs.size()));
  ResourceCapacityModel capacities = BuiltCapacities(BuildResourceCapacityModel(
      kResourceCapacityModelSchemaVersion, board, workload.nets().front().compiled_board,
      default_capacity_units, {}));

  std::vector<CandidatePool> pools;
  pools.reserve(workload.nets().size());
  std::uint64_t query = 1;
  for (const PreparedNetRoutingContext& context : workload.nets()) {
    candidates::GeneratedRouteCandidate draft =
        test_support::CandidateDraft(board, context.compiled_board, context.request, 1, query++);
    candidates::RouteCandidate accepted = test_support::AcceptedCandidate(
        candidates::CandidateAdmissionContext{
            .board = board,
            .compiled_board = context.compiled_board,
            .request = context.request,
        },
        std::move(draft));
    pools.push_back(CandidatePool{
        .net = context.request.net,
        .candidates = {std::make_shared<const candidates::RouteCandidate>(std::move(accepted))},
    });
  }
  return PricingFixture{.board = std::move(board),
                        .workload = std::move(workload),
                        .capacities = std::move(capacities),
                        .pools = std::move(pools)};
}

struct AllocationRun {
  OneWorldAllocationRequest request;
  OneWorldAllocation world;
};

[[nodiscard]] AllocationRun Allocate(const PricingFixture& fixture, PriceSnapshot prices) {
  OneWorldAllocationRequest request{
      .associations = fixture.capacities.associations(),
      .capacities = fixture.capacities,
      .prices = std::move(prices),
      .intrinsic_cost_weight = 1,
      .limits = OneWorldAllocatorLimits{},
      .pools = fixture.pools,
      .workload = &fixture.workload,
  };
  OneWorldAllocation world = BuiltWorld(AllocateOneWorld(request));
  return AllocationRun{.request = std::move(request), .world = std::move(world)};
}

TEST(NegotiatedPricesTest, AuthenticWorkloadUpdatesPresentAndHistoryReplayably) {
  const PricingFixture fixture = BuildPricingFixture(0);
  const NegotiatedPriceConfig config{
      .present_step_per_overuse_unit = 3,
      .history_step_per_overuse_unit = 5,
      .maximum_price_per_resource = 100,
      .maximum_iterations = 4,
      .maximum_price_records = 1'000,
  };
  const NegotiatedPriceState initial = BuiltState(BuildInitialNegotiatedPriceState(
      kNegotiatedPriceStateSchemaVersion, fixture.capacities, fixture.workload, config));
  EXPECT_EQ(initial.iteration(), 0U);
  EXPECT_TRUE(initial.prices().empty());
  EXPECT_NE(initial.state_checksum(), 0U);

  const PriceSnapshot initial_snapshot =
      BuiltSnapshot(BuildPriceSnapshotForState(fixture.capacities, initial));
  EXPECT_EQ(initial_snapshot.iteration(), 0U);
  EXPECT_TRUE(initial_snapshot.prices().empty());
  const AllocationRun first_run = Allocate(fixture, initial_snapshot);
  ASSERT_GT(first_run.world.total_overuse_units, 0U);
  ASSERT_EQ(first_run.world.workload_checksum, fixture.workload.workload_checksum());

  const NegotiatedPriceState first =
      BuiltState(UpdateNegotiatedPrices(initial, first_run.request, first_run.world));
  const NegotiatedPriceState repeated =
      BuiltState(UpdateNegotiatedPrices(initial, first_run.request, first_run.world));
  EXPECT_EQ(first, repeated);
  EXPECT_EQ(first.iteration(), 1U);
  EXPECT_EQ(first.predecessor_state_checksum(), initial.state_checksum());
  EXPECT_EQ(first.source_world_checksum(), first_run.world.world_checksum);
  ASSERT_FALSE(first.prices().empty());
  for (const NegotiatedResourcePrice& price : first.prices()) {
    EXPECT_EQ(price.observed_overuse_units, 1U);
    EXPECT_EQ(price.present_price, 3U);
    EXPECT_EQ(price.history_price, 5U);
    EXPECT_EQ(price.total_price, 8U);
    EXPECT_FALSE(price.present_clamped);
    EXPECT_FALSE(price.history_clamped);
    EXPECT_FALSE(price.total_clamped);
  }

  const PriceSnapshot first_snapshot =
      BuiltSnapshot(BuildPriceSnapshotForState(fixture.capacities, first));
  ASSERT_EQ(first_snapshot.prices().size(), first.prices().size());
  const AllocationRun second_run = Allocate(fixture, first_snapshot);
  const NegotiatedPriceState second =
      BuiltState(UpdateNegotiatedPrices(first, second_run.request, second_run.world));
  EXPECT_EQ(second.iteration(), 2U);
  ASSERT_EQ(second.prices().size(), first.prices().size());
  for (const NegotiatedResourcePrice& price : second.prices()) {
    EXPECT_EQ(price.present_price, 3U);
    EXPECT_EQ(price.history_price, 10U);
    EXPECT_EQ(price.total_price, 13U);
  }
  EXPECT_NE(second.state_checksum(), first.state_checksum());
}

TEST(NegotiatedPricesTest, ClampsExplicitlyAndRejectsBoundsOrTamperedWorlds) {
  const PricingFixture fixture = BuildPricingFixture(0);
  const NegotiatedPriceConfig config{
      .present_step_per_overuse_unit = 9,
      .history_step_per_overuse_unit = 9,
      .maximum_price_per_resource = 10,
      .maximum_iterations = 1,
      .maximum_price_records = 1'000,
  };
  const NegotiatedPriceState initial = BuiltState(BuildInitialNegotiatedPriceState(
      kNegotiatedPriceStateSchemaVersion, fixture.capacities, fixture.workload, config));
  const AllocationRun run =
      Allocate(fixture, BuiltSnapshot(BuildPriceSnapshotForState(fixture.capacities, initial)));
  const NegotiatedPriceState clamped =
      BuiltState(UpdateNegotiatedPrices(initial, run.request, run.world));
  ASSERT_FALSE(clamped.prices().empty());
  EXPECT_EQ(clamped.clamped_resource_count(), clamped.prices().size());
  for (const NegotiatedResourcePrice& price : clamped.prices()) {
    EXPECT_EQ(price.present_price, 9U);
    EXPECT_EQ(price.history_price, 9U);
    EXPECT_EQ(price.total_price, 10U);
    EXPECT_FALSE(price.present_clamped);
    EXPECT_FALSE(price.history_clamped);
    EXPECT_TRUE(price.total_clamped);
  }

  const AllocationRun next_run =
      Allocate(fixture, BuiltSnapshot(BuildPriceSnapshotForState(fixture.capacities, clamped)));
  const NegotiatedPriceStateResult iteration_bound =
      UpdateNegotiatedPrices(clamped, next_run.request, next_run.world);
  ASSERT_TRUE(std::holds_alternative<NegotiatedPriceError>(iteration_bound));
  EXPECT_EQ(std::get<NegotiatedPriceError>(iteration_bound).code,
            NegotiatedPriceErrorCode::kIterationBoundExceeded);

  OneWorldAllocation tampered = run.world;
  ASSERT_FALSE(tampered.resources.empty());
  ++tampered.resources.front().usage_units;
  const NegotiatedPriceStateResult invalid_world =
      UpdateNegotiatedPrices(initial, run.request, tampered);
  ASSERT_TRUE(std::holds_alternative<NegotiatedPriceError>(invalid_world));
  EXPECT_EQ(std::get<NegotiatedPriceError>(invalid_world).code,
            NegotiatedPriceErrorCode::kInvalidWorld);

  NegotiatedPriceConfig one_record = config;
  one_record.maximum_price_records = 1;
  const NegotiatedPriceState bounded = BuiltState(BuildInitialNegotiatedPriceState(
      kNegotiatedPriceStateSchemaVersion, fixture.capacities, fixture.workload, one_record));
  const NegotiatedPriceStateResult record_bound =
      UpdateNegotiatedPrices(bounded, run.request, run.world);
  ASSERT_TRUE(std::holds_alternative<NegotiatedPriceError>(record_bound));
  EXPECT_EQ(std::get<NegotiatedPriceError>(record_bound).code,
            NegotiatedPriceErrorCode::kRecordBoundExceeded);
}

TEST(NegotiatedPricesTest, RejectsSelfChecksummedFabricatedOccupancyAndSelectionEvidence) {
  const PricingFixture fixture = BuildPricingFixture(0);
  const NegotiatedPriceConfig config{
      .maximum_iterations = 2,
      .maximum_price_records = 1'000,
  };
  const NegotiatedPriceState initial = BuiltState(BuildInitialNegotiatedPriceState(
      kNegotiatedPriceStateSchemaVersion, fixture.capacities, fixture.workload, config));
  const AllocationRun run =
      Allocate(fixture, BuiltSnapshot(BuildPriceSnapshotForState(fixture.capacities, initial)));

  OneWorldAllocation fabricated_usage = run.world;
  auto used = std::ranges::find_if(
      fabricated_usage.resources, [](const ResourceUsage& usage) { return usage.usage_units > 0; });
  ASSERT_NE(used, fabricated_usage.resources.end());
  ++used->usage_units;
  ++used->overuse_units;
  ++fabricated_usage.total_overuse_units;
  fabricated_usage.world_checksum = internal::ComputeOneWorldChecksumV2(fabricated_usage);
  const NegotiatedPriceStateResult usage_result =
      UpdateNegotiatedPrices(initial, run.request, fabricated_usage);
  ASSERT_TRUE(std::holds_alternative<NegotiatedPriceError>(usage_result));
  EXPECT_EQ(std::get<NegotiatedPriceError>(usage_result).code,
            NegotiatedPriceErrorCode::kInvalidWorld);

  OneWorldAllocation fabricated_selection = run.world;
  ASSERT_FALSE(fabricated_selection.selections.empty());
  ASSERT_TRUE(fabricated_selection.selections.front().candidate_payload_checksum.has_value());
  ++*fabricated_selection.selections.front().candidate_payload_checksum;
  fabricated_selection.world_checksum = internal::ComputeOneWorldChecksumV2(fabricated_selection);
  const NegotiatedPriceStateResult selection_result =
      UpdateNegotiatedPrices(initial, run.request, fabricated_selection);
  ASSERT_TRUE(std::holds_alternative<NegotiatedPriceError>(selection_result));
  EXPECT_EQ(std::get<NegotiatedPriceError>(selection_result).code,
            NegotiatedPriceErrorCode::kInvalidWorld);

  OneWorldAllocation fabricated_default = run.world;
  ++fabricated_default.default_capacity_units;
  fabricated_default.world_checksum = internal::ComputeOneWorldChecksumV2(fabricated_default);
  const NegotiatedPriceStateResult default_result =
      UpdateNegotiatedPrices(initial, run.request, fabricated_default);
  ASSERT_TRUE(std::holds_alternative<NegotiatedPriceError>(default_result));
  EXPECT_EQ(std::get<NegotiatedPriceError>(default_result).code,
            NegotiatedPriceErrorCode::kInvalidWorld);
}

TEST(NegotiatedPricesTest, RejectsCandidateOutsideTheCompleteSourcePoolRequest) {
  const PricingFixture fixture = BuildPricingFixture(0);
  const NegotiatedPriceConfig config{
      .maximum_iterations = 2,
      .maximum_price_records = 1'000,
  };
  const NegotiatedPriceState initial = BuiltState(BuildInitialNegotiatedPriceState(
      kNegotiatedPriceStateSchemaVersion, fixture.capacities, fixture.workload, config));
  const AllocationRun authentic =
      Allocate(fixture, BuiltSnapshot(BuildPriceSnapshotForState(fixture.capacities, initial)));

  const PreparedNetRoutingContext& context = fixture.workload.nets().front();
  routing::PlanarRouteRequest outsider_request = context.request;
  outsider_request.candidate_policy.deterministic_seed = 101;
  outsider_request.candidate_policy.candidate_ordinal = 103;
  candidates::GeneratedRouteCandidate outsider_draft = test_support::CandidateDraft(
      fixture.board, context.compiled_board, outsider_request, 107, 109);
  candidates::RouteCandidate outsider = test_support::AcceptedCandidate(
      candidates::CandidateAdmissionContext{
          .board = fixture.board,
          .compiled_board = context.compiled_board,
          .request = outsider_request,
      },
      std::move(outsider_draft));
  const candidates::StoredCandidate outsider_handle =
      std::make_shared<const candidates::RouteCandidate>(std::move(outsider));
  ASSERT_NE(outsider_handle->id(), authentic.request.pools.front().candidates.front()->id());

  OneWorldAllocationRequest fabricated_request = authentic.request;
  fabricated_request.pools.front().candidates = {outsider_handle};
  const OneWorldAllocation fabricated_world = BuiltWorld(AllocateOneWorld(fabricated_request));
  ASSERT_EQ(fabricated_world.selections.front().candidate_id, outsider_handle->id());
  const NegotiatedPriceStateResult result =
      UpdateNegotiatedPrices(initial, authentic.request, fabricated_world);
  ASSERT_TRUE(std::holds_alternative<NegotiatedPriceError>(result));
  EXPECT_EQ(std::get<NegotiatedPriceError>(result).code, NegotiatedPriceErrorCode::kInvalidWorld);
}

TEST(NegotiatedPricesTest, BindsExactCapacityModelAndCombinedSnapshotRecordBound) {
  const PricingFixture fixture = BuildPricingFixture(0);
  const NegotiatedPriceConfig config{
      .maximum_iterations = 2,
      .maximum_price_records = kMaximumAllocatorResourceRecordsV1,
  };
  const NegotiatedPriceState initial = BuiltState(BuildInitialNegotiatedPriceState(
      kNegotiatedPriceStateSchemaVersion, fixture.capacities, fixture.workload, config));
  EXPECT_NE(initial.capacity_model_checksum(), 0U);

  const ResourceCapacityModel changed_capacities = BuiltCapacities(
      BuildResourceCapacityModel(kResourceCapacityModelSchemaVersion, fixture.board,
                                 fixture.workload.nets().front().compiled_board, 1, {}));
  const NegotiatedPriceSnapshotResult changed_snapshot =
      BuildPriceSnapshotForState(changed_capacities, initial);
  ASSERT_TRUE(std::holds_alternative<NegotiatedPriceError>(changed_snapshot));
  EXPECT_EQ(std::get<NegotiatedPriceError>(changed_snapshot).code,
            NegotiatedPriceErrorCode::kInvalidState);

  EXPECT_TRUE(internal::NegotiatedPriceRosterFitsV1(kMaximumAllocatorResourceRecordsV1 - 1, 1,
                                                    kMaximumAllocatorResourceRecordsV1));
  EXPECT_FALSE(internal::NegotiatedPriceRosterFitsV1(kMaximumAllocatorResourceRecordsV1, 1,
                                                     kMaximumAllocatorResourceRecordsV1));
  EXPECT_FALSE(internal::NegotiatedPriceRosterFitsV1(0, 2, 1));
}

TEST(NegotiatedPricesTest, DistinguishesComponentClampEqualityAndHistoryAccumulation) {
  const PricingFixture fixture = BuildPricingFixture(0);
  auto allocate_initial = [&](const NegotiatedPriceConfig& config) {
    const NegotiatedPriceState initial = BuiltState(BuildInitialNegotiatedPriceState(
        kNegotiatedPriceStateSchemaVersion, fixture.capacities, fixture.workload, config));
    AllocationRun run =
        Allocate(fixture, BuiltSnapshot(BuildPriceSnapshotForState(fixture.capacities, initial)));
    return std::pair{initial, std::move(run)};
  };

  const NegotiatedPriceConfig component_config{
      .present_step_per_overuse_unit = 11,
      .history_step_per_overuse_unit = 11,
      .maximum_price_per_resource = 10,
      .maximum_iterations = 2,
      .maximum_price_records = 1'000,
  };
  const auto [component_initial, component_run] = allocate_initial(component_config);
  const NegotiatedPriceState component = BuiltState(
      UpdateNegotiatedPrices(component_initial, component_run.request, component_run.world));
  ASSERT_FALSE(component.prices().empty());
  for (const NegotiatedResourcePrice& price : component.prices()) {
    EXPECT_TRUE(price.present_clamped);
    EXPECT_TRUE(price.history_clamped);
    EXPECT_TRUE(price.total_clamped);
  }

  const NegotiatedPriceConfig equality_config{
      .present_step_per_overuse_unit = 10,
      .history_step_per_overuse_unit = 10,
      .maximum_price_per_resource = 10,
      .maximum_iterations = 2,
      .maximum_price_records = 1'000,
  };
  const auto [equality_initial, equality_run] = allocate_initial(equality_config);
  const NegotiatedPriceState equality = BuiltState(
      UpdateNegotiatedPrices(equality_initial, equality_run.request, equality_run.world));
  ASSERT_FALSE(equality.prices().empty());
  for (const NegotiatedResourcePrice& price : equality.prices()) {
    EXPECT_FALSE(price.present_clamped);
    EXPECT_FALSE(price.history_clamped);
    EXPECT_TRUE(price.total_clamped);
  }

  const NegotiatedPriceConfig history_config{
      .present_step_per_overuse_unit = 1,
      .history_step_per_overuse_unit = 6,
      .maximum_price_per_resource = 10,
      .maximum_iterations = 3,
      .maximum_price_records = 1'000,
  };
  const auto [history_initial, history_run] = allocate_initial(history_config);
  const NegotiatedPriceState history_first =
      BuiltState(UpdateNegotiatedPrices(history_initial, history_run.request, history_run.world));
  const AllocationRun history_second_run = Allocate(
      fixture, BuiltSnapshot(BuildPriceSnapshotForState(fixture.capacities, history_first)));
  const NegotiatedPriceState history_second = BuiltState(
      UpdateNegotiatedPrices(history_first, history_second_run.request, history_second_run.world));
  ASSERT_EQ(history_first.prices().size(), history_second.prices().size());
  for (std::size_t index = 0; index < history_first.prices().size(); ++index) {
    EXPECT_FALSE(history_first.prices()[index].history_clamped);
    EXPECT_TRUE(history_second.prices()[index].history_clamped);
    EXPECT_EQ(history_second.prices()[index].history_price, 10U);
  }
}

TEST(NegotiatedPricesTest, RejectsAggregateReplayTotalOverflow) {
  const PricingFixture fixture = BuildPricingFixture(0);
  const NegotiatedPriceConfig config{
      .present_step_per_overuse_unit = std::numeric_limits<std::uint64_t>::max(),
      .history_step_per_overuse_unit = std::numeric_limits<std::uint64_t>::max(),
      .maximum_price_per_resource = std::numeric_limits<std::uint64_t>::max(),
      .maximum_iterations = 2,
      .maximum_price_records = 1'000,
  };
  const NegotiatedPriceState initial = BuiltState(BuildInitialNegotiatedPriceState(
      kNegotiatedPriceStateSchemaVersion, fixture.capacities, fixture.workload, config));
  const AllocationRun run =
      Allocate(fixture, BuiltSnapshot(BuildPriceSnapshotForState(fixture.capacities, initial)));
  ASSERT_GT(run.world.overused_resource_count, 1U);
  const NegotiatedPriceStateResult result = UpdateNegotiatedPrices(initial, run.request, run.world);
  ASSERT_TRUE(std::holds_alternative<NegotiatedPriceError>(result));
  EXPECT_EQ(std::get<NegotiatedPriceError>(result).code,
            NegotiatedPriceErrorCode::kArithmeticOverflow);
}

TEST(NegotiatedPricesTest, CapacityChecksumAndNestedExhaustionTranslationAreStable) {
  internal::ResourceCapacityChecksumHeaderV1 capacity_header{
      .schema_version = 2,
      .associations = {.board_content_hash = 3,
                       .compiler_profile_fingerprint = 5,
                       .geometry_compiler_version = 7},
      .default_capacity_units = 11,
  };
  std::array overrides = {
      ResourceCapacityOverride{
          .resource = {.layer = 13,
                       .lattice_x = -17,
                       .lattice_y = 19,
                       .direction = geometry_compiler::Direction::kEast},
          .capacity_units = 0,
      },
      ResourceCapacityOverride{
          .resource = {.layer = 23,
                       .lattice_x = 29,
                       .lattice_y = -31,
                       .direction = geometry_compiler::Direction::kNorthWest},
          .capacity_units = 1,
      },
  };
  const std::uint64_t golden =
      internal::ComputeResourceCapacityModelChecksumV1(capacity_header, overrides);
  EXPECT_EQ(golden, 8'594'357'387'633'211'926ULL);
  ++capacity_header.default_capacity_units;
  EXPECT_NE(internal::ComputeResourceCapacityModelChecksumV1(capacity_header, overrides), golden);
  --capacity_header.default_capacity_units;
  overrides.front().capacity_units = 1;
  EXPECT_NE(internal::ComputeResourceCapacityModelChecksumV1(capacity_header, overrides), golden);

  const AllocationError exhausted{
      .code = AllocationErrorCode::kResourceExhausted,
      .invariant_id = "test.nested.exhausted.v1",
      .detail = "nested allocation exhausted",
  };
  const NegotiatedPriceError translated = internal::TranslateNestedAllocationError(
      exhausted, NegotiatedPriceErrorCode::kInvalidWorld, "test.fallback.v1", "fallback");
  EXPECT_EQ(translated.code, NegotiatedPriceErrorCode::kResourceExhausted);
  EXPECT_EQ(translated.invariant_id, exhausted.invariant_id);
  EXPECT_EQ(translated.detail, exhausted.detail);

  AllocationError invalid = exhausted;
  invalid.code = AllocationErrorCode::kInvalidConfiguration;
  const NegotiatedPriceError fallback = internal::TranslateNestedAllocationError(
      invalid, NegotiatedPriceErrorCode::kInvalidWorld, "test.fallback.v1", "fallback");
  EXPECT_EQ(fallback.code, NegotiatedPriceErrorCode::kInvalidWorld);
  EXPECT_EQ(fallback.invariant_id, "test.fallback.v1");
  EXPECT_EQ(fallback.detail, "fallback");
}

TEST(NegotiatedPricesTest, StateChecksumHasGoldenAndFieldSensitivity) {
  internal::NegotiatedPriceChecksumHeaderV1 header{
      .schema_version = 1,
      .associations = {.board_content_hash = 2,
                       .compiler_profile_fingerprint = 3,
                       .geometry_compiler_version = 5},
      .workload_checksum = 7,
      .capacity_model_checksum = 11,
      .config = {.present_step_per_overuse_unit = 13,
                 .history_step_per_overuse_unit = 17,
                 .maximum_price_per_resource = 19,
                 .maximum_iterations = 23,
                 .maximum_price_records = 29},
      .iteration = 31,
      .predecessor_state_checksum = 37,
      .source_world_checksum = 41,
      .clamped_resource_count = 43,
      .total_present_price = 47,
      .total_history_price = 53,
      .total_price = 59,
  };
  std::array prices = {
      NegotiatedResourcePrice{
          .resource = {.layer = 61,
                       .lattice_x = -67,
                       .lattice_y = 71,
                       .direction = geometry_compiler::Direction::kNorthEast},
          .present_price = 73,
          .history_price = 79,
          .total_price = 83,
          .observed_overuse_units = 89,
          .present_clamped = true,
          .history_clamped = false,
          .total_clamped = true,
      },
      NegotiatedResourcePrice{
          .resource = {.layer = 97,
                       .lattice_x = 101,
                       .lattice_y = -103,
                       .direction = geometry_compiler::Direction::kNorth},
          .present_price = 107,
          .history_price = 109,
          .total_price = 113,
          .observed_overuse_units = 127,
          .present_clamped = false,
          .history_clamped = true,
          .total_clamped = false,
      },
  };
  const std::uint64_t golden = internal::ComputeNegotiatedPriceStateChecksumV1(header, prices);
  EXPECT_EQ(golden, 2'909'744'929'431'564'223ULL);
  ++header.iteration;
  EXPECT_NE(internal::ComputeNegotiatedPriceStateChecksumV1(header, prices), golden);
  --header.iteration;
  ++header.predecessor_state_checksum;
  EXPECT_NE(internal::ComputeNegotiatedPriceStateChecksumV1(header, prices), golden);
  --header.predecessor_state_checksum;
  ++prices[1].total_price;
  EXPECT_NE(internal::ComputeNegotiatedPriceStateChecksumV1(header, prices), golden);
}

}  // namespace
}  // namespace apgar::allocator
