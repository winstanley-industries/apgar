#include "apgar/allocator/one_world.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <map>
#include <memory>
#include <ranges>
#include <set>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "apgar/candidates/route_candidate.h"
#include "apgar/geometry_compiler/compiled_board.h"
#include "apgar/routing/candidate_policy.h"
#include "src/allocator/one_world_internal.h"
#include "tests/support/candidate_builder.h"
#include "tests/support/compiler_builder.h"
#include "tests/support/google_test.h"
#include "tests/support/one_world_fault_test_support.h"
#include "tests/support/routing_builder.h"

namespace apgar::allocator {
namespace {

using candidates::StoredCandidate;
using routing::EdgeResourceKey;

static_assert(std::is_nothrow_copy_constructible_v<AllocationError>);
static_assert(std::is_nothrow_move_constructible_v<AllocationError>);
static_assert(std::is_nothrow_constructible_v<OneWorldAllocationResult, AllocationError>);
static_assert(!std::is_default_constructible_v<ResourceCapacityModel>);
static_assert(!std::is_default_constructible_v<PriceSnapshot>);
static_assert(internal::ResourceRecordCountFitsV1(kMaximumAllocatorResourceRecordsV1));
static_assert(!internal::ResourceRecordCountFitsV1(kMaximumAllocatorResourceRecordsV1 + 1U));
static_assert(internal::CombinedResourceRecordCountFitsV1(kMaximumAllocatorResourceRecordsV1, 0));
static_assert(internal::CombinedResourceRecordCountFitsV1(kMaximumAllocatorResourceRecordsV1 - 1U,
                                                          1));
static_assert(!internal::CombinedResourceRecordCountFitsV1(kMaximumAllocatorResourceRecordsV1, 1));

using CapacityLvalueFactory = ResourceCapacityModelResult (*)(
    std::uint32_t, const board_ir::BoardSnapshot&, const geometry_compiler::CompiledBoard&,
    std::uint32_t, const std::vector<ResourceCapacityOverride>&);
using PriceLvalueFactory = PriceSnapshotResult (*)(std::uint32_t, const ResourceCapacityModel&,
                                                   std::uint32_t,
                                                   const std::vector<ResourcePrice>&);
static_assert(
    std::is_same_v<decltype(static_cast<CapacityLvalueFactory>(&BuildResourceCapacityModel)),
                   CapacityLvalueFactory>);
static_assert(std::is_same_v<decltype(static_cast<PriceLvalueFactory>(&BuildPriceSnapshot)),
                             PriceLvalueFactory>);

struct CandidateFixture {
  board_ir::BoardSnapshot board;
  geometry_compiler::CompiledBoard compiled;
  routing::CpuRouteRequest base_request;
  StoredCandidate first;
  StoredCandidate alternate;
};

[[nodiscard]] std::vector<EdgeResourceKey> AtomicResources(const StoredCandidate& candidate) {
  std::vector<EdgeResourceKey> resources;
  for (const candidates::PhysicalEdgeSpan& span : candidate->data().resources) {
    const geometry_compiler::DirectionDelta delta =
        candidates::ResourceSpanStorageDelta(span.direction);
    for (std::uint32_t offset = 0; offset < span.edge_count; ++offset) {
      resources.push_back(EdgeResourceKey{
          .layer = span.layer,
          .lattice_x = span.lattice_x + static_cast<std::int64_t>(delta.x) * offset,
          .lattice_y = span.lattice_y + static_cast<std::int64_t>(delta.y) * offset,
          .direction = span.direction,
      });
    }
  }
  return resources;
}

[[nodiscard]] StoredCandidate Admit(const board_ir::BoardSnapshot& board,
                                    const geometry_compiler::CompiledBoard& compiled,
                                    const routing::CpuRouteRequest& request,
                                    std::uint64_t query_identity) {
  candidates::GeneratedRouteCandidate draft =
      test_support::CandidateDraft(board, compiled, request, 1, query_identity);
  candidates::RouteCandidate admitted = test_support::AcceptedCandidate(
      candidates::CandidateAdmissionContext{
          .board = board,
          .compiled_board = compiled,
          .request = request,
      },
      std::move(draft));
  return std::make_shared<const candidates::RouteCandidate>(std::move(admitted));
}

[[nodiscard]] CandidateFixture ProductionCandidates() {
  board_ir::BoardSnapshot board = test_support::Snapshot();
  geometry_compiler::CompiledBoard compiled =
      test_support::Compile(board, test_support::DefaultCompilerProfile({0}));
  routing::CpuRouteRequest first_request = test_support::TwoTerminalRequest(board, 0, 0);
  first_request.candidate_policy.deterministic_seed = 0x1234U;
  StoredCandidate first = Admit(board, compiled, first_request, 1);
  const std::vector<EdgeResourceKey> first_resources = AtomicResources(first);
  EXPECT_FALSE(first_resources.empty());
  if (first_resources.empty()) {
    std::abort();
  }

  routing::CpuRouteRequest alternate_request = first_request;
  alternate_request.candidate_policy.candidate_ordinal = 1;
  alternate_request.candidate_policy.banned_resources = {
      first_resources[first_resources.size() / 2U]};
  StoredCandidate alternate = Admit(board, compiled, alternate_request, 2);
  EXPECT_NE(first->data().resources, alternate->data().resources);
  return CandidateFixture{
      .board = std::move(board),
      .compiled = std::move(compiled),
      .base_request = std::move(first_request),
      .first = std::move(first),
      .alternate = std::move(alternate),
  };
}

[[nodiscard]] AllocationAssociations Associations(const CandidateFixture& fixture) {
  const candidates::CandidateAssociations& source = fixture.first->data().associations;
  return AllocationAssociations{
      .board_content_hash = source.board_content_hash,
      .compiler_profile_fingerprint = source.compiler_profile_fingerprint,
      .geometry_compiler_version = source.geometry_compiler_version,
  };
}

template <typename State>
[[nodiscard]] State Built(std::variant<State, AllocationError> result) {
  EXPECT_TRUE(std::holds_alternative<State>(result));
  if (!std::holds_alternative<State>(result)) {
    std::abort();
  }
  return std::get<State>(std::move(result));
}

[[nodiscard]] ResourceCapacityModel Capacities(
    const CandidateFixture& fixture, std::uint32_t default_capacity_units = 1,
    std::vector<ResourceCapacityOverride> overrides = {}) {
  return Built(BuildResourceCapacityModel(kResourceCapacityModelSchemaVersion, fixture.board,
                                          fixture.compiled, default_capacity_units,
                                          std::move(overrides)));
}

[[nodiscard]] PriceSnapshot Prices(const ResourceCapacityModel& capacities,
                                   std::uint32_t iteration = 0,
                                   std::vector<ResourcePrice> prices = {}) {
  return Built(
      BuildPriceSnapshot(kPriceSnapshotSchemaVersion, capacities, iteration, std::move(prices)));
}

[[nodiscard]] OneWorldAllocationRequest BaseRequest(const CandidateFixture& fixture) {
  const AllocationAssociations associations = Associations(fixture);
  ResourceCapacityModel capacities = Capacities(fixture);
  return OneWorldAllocationRequest{
      .associations = associations,
      .capacities = capacities,
      .prices = Prices(capacities),
      .intrinsic_cost_weight = 1,
      .limits = OneWorldAllocatorLimits{},
      .pools = {},
      .workload = nullptr,
  };
}

[[nodiscard]] OneWorldAllocation Allocated(OneWorldAllocationResult result) {
  EXPECT_TRUE(std::holds_alternative<OneWorldAllocation>(result));
  if (!std::holds_alternative<OneWorldAllocation>(result)) {
    std::abort();
  }
  return std::get<OneWorldAllocation>(std::move(result));
}

[[nodiscard]] AllocationError Failed(OneWorldAllocationResult result) {
  EXPECT_TRUE(std::holds_alternative<AllocationError>(result));
  if (!std::holds_alternative<AllocationError>(result)) {
    std::abort();
  }
  return std::get<AllocationError>(std::move(result));
}

[[nodiscard]] const ResourceUsage* FindUsage(const OneWorldAllocation& world,
                                             EdgeResourceKey resource) {
  const auto found = std::ranges::find(world.resources, resource, &ResourceUsage::resource);
  return found == world.resources.end() ? nullptr : &*found;
}

[[nodiscard]] StoredCandidate IntrinsicBest(const CandidateFixture& fixture) {
  const auto rank = [](const StoredCandidate& candidate) {
    return std::tuple{candidate->data().metrics.intrinsic_base_cost, candidate->id()};
  };
  return rank(fixture.first) < rank(fixture.alternate) ? fixture.first : fixture.alternate;
}

[[nodiscard]] StoredCandidate Other(const CandidateFixture& fixture,
                                    const StoredCandidate& selected) {
  return selected == fixture.first ? fixture.alternate : fixture.first;
}

[[nodiscard]] std::uint64_t FlatPriceCost(const StoredCandidate& candidate,
                                          const std::vector<ResourcePrice>& prices) {
  std::map<EdgeResourceKey, std::uint64_t> price_by_resource;
  for (const ResourcePrice& price : prices) {
    price_by_resource.emplace(price.resource, price.price_per_usage_unit);
  }
  std::uint64_t total = 0;
  for (const EdgeResourceKey& resource : AtomicResources(candidate)) {
    const auto price = price_by_resource.find(resource);
    if (price != price_by_resource.end()) {
      total += price->second;
    }
  }
  return total;
}

[[nodiscard]] std::vector<internal::AccumulatedResourceUse> FlatAccumulationOracle(
    const std::vector<StoredCandidate>& candidates) {
  std::map<EdgeResourceKey, std::uint64_t> usage;
  for (const StoredCandidate& candidate : candidates) {
    for (const candidates::PhysicalEdgeSpan& span : candidate->data().resources) {
      const geometry_compiler::DirectionDelta delta =
          candidates::ResourceSpanStorageDelta(span.direction);
      for (std::uint32_t offset = 0; offset < span.edge_count; ++offset) {
        const EdgeResourceKey resource{
            .layer = span.layer,
            .lattice_x = span.lattice_x + static_cast<std::int64_t>(delta.x) * offset,
            .lattice_y = span.lattice_y + static_cast<std::int64_t>(delta.y) * offset,
            .direction = span.direction,
        };
        usage[resource] += span.usage_units;
      }
    }
  }
  std::vector<internal::AccumulatedResourceUse> result;
  result.reserve(usage.size());
  for (const auto& [resource, usage_units] : usage) {
    result.push_back(
        internal::AccumulatedResourceUse{.resource = resource, .usage_units = usage_units});
  }
  return result;
}

[[nodiscard]] EdgeResourceKey LiteralResourceAt(const candidates::PhysicalEdgeSpan& span,
                                                std::uint32_t offset) {
  std::int64_t x = span.lattice_x;
  std::int64_t y = span.lattice_y;
  switch (span.direction) {
    case geometry_compiler::Direction::kEast:
      x += offset;
      break;
    case geometry_compiler::Direction::kNorthEast:
      x += offset;
      y += offset;
      break;
    case geometry_compiler::Direction::kNorth:
      y += offset;
      break;
    case geometry_compiler::Direction::kNorthWest:
      x += offset;
      y -= offset;
      break;
    default:
      std::abort();
  }
  return EdgeResourceKey{
      .layer = span.layer, .lattice_x = x, .lattice_y = y, .direction = span.direction};
}

[[nodiscard]] std::vector<internal::AccumulatedResourceUse> FlatSpanOracle(
    const std::vector<candidates::PhysicalEdgeSpan>& spans) {
  std::map<EdgeResourceKey, std::uint64_t> usage;
  for (const candidates::PhysicalEdgeSpan& span : spans) {
    for (std::uint32_t offset = 0; offset < span.edge_count; ++offset) {
      usage[LiteralResourceAt(span, offset)] += span.usage_units;
    }
  }
  std::vector<internal::AccumulatedResourceUse> result;
  result.reserve(usage.size());
  for (const auto& [resource, usage_units] : usage) {
    result.push_back(
        internal::AccumulatedResourceUse{.resource = resource, .usage_units = usage_units});
  }
  return result;
}

[[nodiscard]] EdgeResourceKey ResourceUniqueTo(const StoredCandidate& candidate,
                                               const StoredCandidate& other) {
  const std::vector<EdgeResourceKey> candidate_resources = AtomicResources(candidate);
  const std::vector<EdgeResourceKey> other_atomic_resources = AtomicResources(other);
  const std::set<EdgeResourceKey> other_resources(other_atomic_resources.begin(),
                                                  other_atomic_resources.end());
  const auto unique = std::ranges::find_if(
      candidate_resources,
      [&](const EdgeResourceKey& resource) { return !other_resources.contains(resource); });
  EXPECT_NE(unique, candidate_resources.end());
  if (unique == candidate_resources.end()) {
    std::abort();
  }
  return *unique;
}

TEST(OneWorldAllocatorTest, SelectsProductionCandidateAndReportsExactOveruse) {
  const CandidateFixture fixture = ProductionCandidates();
  const StoredCandidate best = IntrinsicBest(fixture);
  const EdgeResourceKey unavailable = AtomicResources(best).front();
  constexpr board_ir::EntityRef kNoCandidateNet{.id = 999, .generation = 0};
  OneWorldAllocationRequest request = BaseRequest(fixture);
  request.capacities = Capacities(
      fixture, 1, {ResourceCapacityOverride{.resource = unavailable, .capacity_units = 0}});
  request.prices = Prices(request.capacities);
  request.pools = {
      CandidatePool{.net = kNoCandidateNet, .candidates = {}},
      CandidatePool{.net = fixture.base_request.net,
                    .candidates = {fixture.alternate, fixture.first}},
  };

  const OneWorldAllocation world = Allocated(AllocateOneWorld(request));
  ASSERT_EQ(world.selections.size(), 2U);
  EXPECT_EQ(world.selections[0].net, fixture.base_request.net);
  EXPECT_EQ(world.selections[0].candidate_id, best->id());
  EXPECT_EQ(world.selections[1].status, NetSelectionStatus::kNoAdmissibleCandidate);
  EXPECT_EQ(world.selected_net_count, 1U);
  EXPECT_EQ(world.no_candidate_net_count, 1U);
  EXPECT_EQ(world.scoring_span_queries, 0U);
  EXPECT_EQ(world.scoring_price_matches, 0U);
  EXPECT_EQ(world.selected_logical_resource_uses, AtomicResources(best).size());
  EXPECT_EQ(world.accounting_materialized_resource_edges, AtomicResources(best).size());
  const ResourceUsage* unavailable_usage = FindUsage(world, unavailable);
  ASSERT_NE(unavailable_usage, nullptr);
  EXPECT_TRUE(unavailable_usage->has_capacity_override);
  EXPECT_EQ(unavailable_usage->capacity_units, 0U);
  EXPECT_EQ(unavailable_usage->usage_units, 1U);
  EXPECT_EQ(unavailable_usage->overuse_units, 1U);
  EXPECT_EQ(world.total_overuse_units, 1U);
}

TEST(OneWorldAllocatorTest, PriceSnapshotSelectsProductionResourceDistinctAlternative) {
  const CandidateFixture fixture = ProductionCandidates();
  const StoredCandidate intrinsic_best = IntrinsicBest(fixture);
  const StoredCandidate alternate = Other(fixture, intrinsic_best);
  const EdgeResourceKey penalized = ResourceUniqueTo(intrinsic_best, alternate);
  const std::uint64_t cost_difference = alternate->data().metrics.intrinsic_base_cost -
                                        intrinsic_best->data().metrics.intrinsic_base_cost;
  const std::vector<ResourcePrice> prices = {
      ResourcePrice{.resource = penalized, .price_per_usage_unit = cost_difference + 1U}};
  OneWorldAllocationRequest request = BaseRequest(fixture);
  request.prices = Prices(request.capacities, 7, prices);
  request.pools = {CandidatePool{.net = fixture.base_request.net,
                                 .candidates = {fixture.first, fixture.alternate}}};

  const OneWorldAllocation world = Allocated(AllocateOneWorld(request));
  ASSERT_EQ(world.selections.size(), 1U);
  EXPECT_EQ(world.selections[0].candidate_id, alternate->id());
  EXPECT_EQ(world.price_iteration, 7U);
  EXPECT_EQ(world.scoring_span_queries,
            fixture.first->data().resources.size() + fixture.alternate->data().resources.size());
  EXPECT_EQ(world.scoring_price_matches, 1U);
  EXPECT_EQ(world.selected_logical_resource_uses, AtomicResources(alternate).size());
  EXPECT_EQ(world.selections[0].price_cost, FlatPriceCost(alternate, prices));
  const ResourceUsage* price = FindUsage(world, penalized);
  ASSERT_NE(price, nullptr);
  EXPECT_TRUE(price->has_explicit_price);
}

TEST(OneWorldAllocatorTest, SelectionOnlyEvidenceMatchesAllocatorAndBindsCanonicalRequest) {
  const CandidateFixture fixture = ProductionCandidates();
  OneWorldAllocationRequest request = BaseRequest(fixture);
  request.pools = {CandidatePool{.net = fixture.base_request.net,
                                 .candidates = {fixture.alternate, fixture.first}}};
  const OneWorldAllocation world = Allocated(AllocateOneWorld(request));
  internal::OneWorldSelectionEvidenceResult evidence_result =
      internal::SelectOneWorldWithoutAccounting(request);
  ASSERT_TRUE(std::holds_alternative<internal::OneWorldSelectionEvidence>(evidence_result));
  const internal::OneWorldSelectionEvidence evidence =
      std::get<internal::OneWorldSelectionEvidence>(std::move(evidence_result));
  ASSERT_EQ(evidence.pools.size(), 1U);
  ASSERT_EQ(world.selections.size(), 1U);
  EXPECT_EQ(evidence.source_pool_count, 1U);
  EXPECT_EQ(evidence.source_candidate_count, 2U);
  EXPECT_NE(evidence.request_manifest_checksum, 0U);
  EXPECT_NE(evidence.candidate_pool_manifest_checksum, 0U);
  EXPECT_EQ(evidence.pools.front().candidate_count, 2U);
  EXPECT_NE(evidence.pools.front().pool_manifest_checksum, 0U);
  EXPECT_EQ(evidence.pools.front().selection.candidate_id, world.selections.front().candidate_id);
  EXPECT_EQ(evidence.pools.front().selection.candidate_payload_checksum,
            world.selections.front().candidate_payload_checksum);
  EXPECT_EQ(evidence.pools.front().selection.selection_score,
            world.selections.front().selection_score);
  EXPECT_EQ(evidence.selection_projection.selected_net_count, world.selected_net_count);
  EXPECT_EQ(evidence.selection_projection.total_intrinsic_cost, world.total_intrinsic_cost);
  EXPECT_EQ(evidence.selection_projection.total_selection_score, world.total_selection_score);
  EXPECT_EQ(evidence.selection_projection.scoring_span_queries, world.scoring_span_queries);
  EXPECT_EQ(evidence.selection_projection.scoring_price_matches, world.scoring_price_matches);
  EXPECT_TRUE(evidence.selection_projection.resources.empty());
  EXPECT_EQ(evidence.selection_projection.accounting_materialized_resource_edges, 0U);
  EXPECT_EQ(evidence.selected_expanded_resource_uses, world.selected_logical_resource_uses);

  std::ranges::reverse(request.pools.front().candidates);
  internal::OneWorldSelectionEvidenceResult reordered_result =
      internal::SelectOneWorldWithoutAccounting(request);
  ASSERT_TRUE(std::holds_alternative<internal::OneWorldSelectionEvidence>(reordered_result));
  const internal::OneWorldSelectionEvidence reordered =
      std::get<internal::OneWorldSelectionEvidence>(std::move(reordered_result));
  EXPECT_EQ(reordered.candidate_pool_manifest_checksum, evidence.candidate_pool_manifest_checksum);
  EXPECT_EQ(reordered.request_manifest_checksum, evidence.request_manifest_checksum);

  OneWorldAllocationRequest reduced_pool_request = request;
  reduced_pool_request.pools.front().candidates = {world.selections.front().candidate};
  internal::OneWorldSelectionEvidenceResult reduced_pool_result =
      internal::SelectOneWorldWithoutAccounting(reduced_pool_request);
  ASSERT_TRUE(std::holds_alternative<internal::OneWorldSelectionEvidence>(reduced_pool_result));
  const internal::OneWorldSelectionEvidence reduced_pool =
      std::get<internal::OneWorldSelectionEvidence>(std::move(reduced_pool_result));
  EXPECT_EQ(reduced_pool.pools.front().selection.candidate_id,
            world.selections.front().candidate_id);
  EXPECT_NE(reduced_pool.candidate_pool_manifest_checksum,
            evidence.candidate_pool_manifest_checksum);
  EXPECT_NE(reduced_pool.request_manifest_checksum, evidence.request_manifest_checksum);

  --request.limits.maximum_resource_records;
  internal::OneWorldSelectionEvidenceResult changed_limit_result =
      internal::SelectOneWorldWithoutAccounting(request);
  ASSERT_TRUE(std::holds_alternative<internal::OneWorldSelectionEvidence>(changed_limit_result));
  const internal::OneWorldSelectionEvidence changed_limit =
      std::get<internal::OneWorldSelectionEvidence>(std::move(changed_limit_result));
  EXPECT_EQ(changed_limit.candidate_pool_manifest_checksum,
            evidence.candidate_pool_manifest_checksum);
  EXPECT_NE(changed_limit.request_manifest_checksum, evidence.request_manifest_checksum);
}

TEST(OneWorldAllocatorTest, RejectsCandidateFromDifferentAuthenticEndpointLayer) {
  const board_ir::BoardSnapshot board = test_support::Snapshot();
  const geometry_compiler::CompilerProfile compiler_profile =
      test_support::DefaultCompilerProfile({0, 31});
  const geometry_compiler::CompiledBoard compiled = test_support::Compile(board, compiler_profile);
  const routing::CpuRouteRequest layer_zero_request = test_support::TwoTerminalRequest(board, 0, 0);
  const StoredCandidate layer_zero_candidate = Admit(board, compiled, layer_zero_request, 1);
  const std::array specs = {MultiNetRoutingSpec{
      .routing_profile = board.data().routing_profile, .start_layer = 31, .goal_layer = 31}};
  MultiNetWorkloadResult workload_result = BuildMultiNetWorkload(
      kMultiNetWorkloadSchemaVersion, board, compiler_profile, specs, specs.size());
  ASSERT_TRUE(std::holds_alternative<MultiNetWorkload>(workload_result));
  const MultiNetWorkload layer_31_workload = std::get<MultiNetWorkload>(std::move(workload_result));
  const ResourceCapacityModel capacities = Built<ResourceCapacityModel>(
      BuildResourceCapacityModel(kResourceCapacityModelSchemaVersion, board, compiled, 1, {}));
  const PriceSnapshot prices =
      Built<PriceSnapshot>(BuildPriceSnapshot(kPriceSnapshotSchemaVersion, capacities, 0, {}));
  const candidates::CandidateAssociations& candidate_associations =
      layer_zero_candidate->data().associations;
  const OneWorldAllocationRequest request{
      .associations =
          AllocationAssociations{
              .board_content_hash = candidate_associations.board_content_hash,
              .compiler_profile_fingerprint = candidate_associations.compiler_profile_fingerprint,
              .geometry_compiler_version = candidate_associations.geometry_compiler_version,
          },
      .capacities = capacities,
      .prices = prices,
      .limits = OneWorldAllocatorLimits{},
      .pools = {CandidatePool{.net = layer_zero_request.net, .candidates = {layer_zero_candidate}}},
      .workload = &layer_31_workload,
  };

  const internal::OneWorldSelectionEvidenceResult result =
      internal::SelectOneWorldWithoutAccounting(request);
  ASSERT_TRUE(std::holds_alternative<AllocationError>(result));
  const AllocationError& error = std::get<AllocationError>(result);
  EXPECT_EQ(error.code, AllocationErrorCode::kCandidateAssociationMismatch);
  EXPECT_EQ(error.invariant_id, "allocator.pool.workload_request.v1");
}

TEST(OneWorldAllocatorTest, RequestAndPoolManifestEncodersHaveGoldenRepresentations) {
  const CandidateFixture fixture = ProductionCandidates();
  const EdgeResourceKey capacity_resource{.layer = 0,
                                          .lattice_x = -127,
                                          .lattice_y = -131,
                                          .direction = geometry_compiler::Direction::kNorthWest};
  const EdgeResourceKey zero_price_resource{.layer = 0,
                                            .lattice_x = -109,
                                            .lattice_y = -113,
                                            .direction = geometry_compiler::Direction::kEast};
  ResourceCapacityModel capacities = Capacities(
      fixture, 1, {ResourceCapacityOverride{.resource = capacity_resource, .capacity_units = 0}});
  OneWorldAllocationRequest request{
      .associations = Associations(fixture),
      .capacities = capacities,
      .prices = Prices(capacities, 17,
                       {ResourcePrice{.resource = zero_price_resource, .price_per_usage_unit = 0}}),
      .intrinsic_cost_weight = 19,
      .limits = OneWorldAllocatorLimits{.maximum_nets = 3,
                                        .maximum_candidates = 5,
                                        .maximum_resource_records = 7,
                                        .maximum_expanded_resource_uses = 1'009},
      .pools =
          {
              CandidatePool{.net = {.id = 9, .generation = 4}, .candidates = {}},
              CandidatePool{.net = fixture.base_request.net,
                            .candidates = {fixture.alternate, fixture.first}},
          },
      .workload = nullptr,
  };
  internal::OneWorldSelectionEvidenceResult result =
      internal::SelectOneWorldWithoutAccounting(request);
  ASSERT_TRUE(std::holds_alternative<internal::OneWorldSelectionEvidence>(result));
  const internal::OneWorldSelectionEvidence evidence =
      std::get<internal::OneWorldSelectionEvidence>(std::move(result));
  EXPECT_EQ(request.schema_version, 2U);
  EXPECT_EQ(request.associations.board_content_hash, 11'254'834'395'910'409'746ULL);
  EXPECT_EQ(request.associations.compiler_profile_fingerprint, 15'213'476'683'192'819'267ULL);
  EXPECT_EQ(request.associations.geometry_compiler_version, 1U);
  EXPECT_EQ(request.capacities.schema_version(), 1U);
  EXPECT_EQ(request.prices.schema_version(), 1U);
  EXPECT_EQ(fixture.base_request.net.id, 10U);
  EXPECT_EQ(fixture.base_request.net.generation, 0U);
  EXPECT_EQ(fixture.first->id().high, 16'905'596'407'620'462'294ULL);
  EXPECT_EQ(fixture.first->id().low, 13'442'482'538'882'174'867ULL);
  EXPECT_EQ(fixture.first->data().payload_checksum, 12'239'829'871'499'795'965ULL);
  EXPECT_EQ(fixture.alternate->id().high, 9'094'886'488'943'244'682ULL);
  EXPECT_EQ(fixture.alternate->id().low, 6'553'997'329'807'649'295ULL);
  EXPECT_EQ(fixture.alternate->data().payload_checksum, 1'481'378'841'828'407'483ULL);
  ASSERT_EQ(evidence.pools.size(), 2U);
  EXPECT_EQ(evidence.pools[0].pool_manifest_checksum, 4'665'966'256'940'749'526ULL);
  EXPECT_EQ(evidence.pools[1].pool_manifest_checksum, 964'588'783'859'553'204ULL);
  EXPECT_EQ(evidence.candidate_pool_manifest_checksum, 9'837'338'791'036'826'863ULL);
  EXPECT_EQ(evidence.request_manifest_checksum, 14'547'234'282'927'751'318ULL);
}

TEST(OneWorldAllocatorTest, SparseNonzeroPriceUsesSpanQueriesRatherThanEdgeExpansion) {
  const CandidateFixture fixture = ProductionCandidates();
  const EdgeResourceKey far_price{
      .layer = 0,
      .lattice_x = -1'000'000'000,
      .lattice_y = -1'000'000'000,
      .direction = geometry_compiler::Direction::kEast,
  };
  OneWorldAllocationRequest request = BaseRequest(fixture);
  request.prices = Prices(request.capacities, 1,
                          {ResourcePrice{.resource = far_price, .price_per_usage_unit = 1}});
  request.pools = {CandidatePool{.net = fixture.base_request.net,
                                 .candidates = {fixture.first, fixture.alternate}}};

  const OneWorldAllocation world = Allocated(AllocateOneWorld(request));
  EXPECT_EQ(world.scoring_span_queries,
            fixture.first->data().resources.size() + fixture.alternate->data().resources.size());
  EXPECT_EQ(world.scoring_price_matches, 0U);
  EXPECT_LT(world.scoring_span_queries,
            AtomicResources(fixture.first).size() + AtomicResources(fixture.alternate).size());
  ASSERT_EQ(world.selections.size(), 1U);
  EXPECT_EQ(world.selections[0].price_cost, 0U);
}

TEST(OneWorldAllocatorTest, InputPermutationDoesNotChangeExternallyVisibleWorld) {
  const CandidateFixture fixture = ProductionCandidates();
  const EdgeResourceKey first_resource = AtomicResources(fixture.first).front();
  const EdgeResourceKey alternate_resource = ResourceUniqueTo(fixture.alternate, fixture.first);
  std::vector<ResourceCapacityOverride> capacity_inputs = {
      ResourceCapacityOverride{.resource = alternate_resource, .capacity_units = 1},
      ResourceCapacityOverride{.resource = first_resource, .capacity_units = 1},
  };
  std::vector<ResourcePrice> price_inputs = {
      ResourcePrice{.resource = alternate_resource, .price_per_usage_unit = 3},
      ResourcePrice{.resource = first_resource, .price_per_usage_unit = 3},
  };
  OneWorldAllocationRequest first = BaseRequest(fixture);
  first.capacities = Capacities(fixture, 1, capacity_inputs);
  first.prices = Prices(first.capacities, 0, price_inputs);
  first.pools = {CandidatePool{.net = fixture.base_request.net,
                               .candidates = {fixture.alternate, fixture.first}}};
  OneWorldAllocationRequest second = first;
  std::ranges::reverse(capacity_inputs);
  std::ranges::reverse(price_inputs);
  second.capacities = Capacities(fixture, 1, capacity_inputs);
  second.prices = Prices(second.capacities, 0, price_inputs);
  std::ranges::reverse(second.pools[0].candidates);

  const OneWorldAllocation first_world = Allocated(AllocateOneWorld(first));
  const OneWorldAllocation second_world = Allocated(AllocateOneWorld(second));
  EXPECT_EQ(first_world.world_checksum, second_world.world_checksum);
  EXPECT_EQ(first_world.selections[0].candidate_id, second_world.selections[0].candidate_id);
  EXPECT_EQ(first_world.resources, second_world.resources);
}

TEST(OneWorldAllocatorTest, RejectsAssociationDriftAndDuplicateResourceRecords) {
  const CandidateFixture fixture = ProductionCandidates();
  OneWorldAllocationRequest drifted = BaseRequest(fixture);
  drifted.associations.board_content_hash = 999;
  drifted.pools = {CandidatePool{.net = fixture.base_request.net, .candidates = {fixture.first}}};
  const AllocationError association_failure = Failed(AllocateOneWorld(drifted));
  EXPECT_EQ(association_failure.invariant_id, "allocator.resource_state.association.v1");

  const EdgeResourceKey resource = AtomicResources(fixture.first).front();
  const ResourceCapacityModelResult duplicated = BuildResourceCapacityModel(
      kResourceCapacityModelSchemaVersion, fixture.board, fixture.compiled, 1,
      {ResourceCapacityOverride{.resource = resource, .capacity_units = 0},
       ResourceCapacityOverride{.resource = resource, .capacity_units = 1}});
  ASSERT_TRUE(std::holds_alternative<AllocationError>(duplicated));
  EXPECT_EQ(std::get<AllocationError>(duplicated).code, AllocationErrorCode::kDuplicateResource);

  board_ir::BoardData changed_data = fixture.board.data();
  ++changed_data.revision;
  const board_ir::BoardSnapshot changed_board = test_support::Snapshot(std::move(changed_data));
  const ResourceCapacityModelResult relabel_attempt = BuildResourceCapacityModel(
      kResourceCapacityModelSchemaVersion, changed_board, fixture.compiled, 1, {});
  ASSERT_TRUE(std::holds_alternative<AllocationError>(relabel_attempt));
  EXPECT_EQ(std::get<AllocationError>(relabel_attempt).invariant_id,
            "allocator.capacity.compiled_board_association.v1");
}

TEST(OneWorldAllocatorTest, LvalueStateFactoriesCopyInsideTheirResultEnvelope) {
  const CandidateFixture fixture = ProductionCandidates();
  const EdgeResourceKey resource = AtomicResources(fixture.first).front();
  const std::vector<ResourceCapacityOverride> overrides = {
      ResourceCapacityOverride{.resource = resource, .capacity_units = 0},
  };
  const ResourceCapacityModelResult capacity_result = BuildResourceCapacityModel(
      kResourceCapacityModelSchemaVersion, fixture.board, fixture.compiled, 1, overrides);
  ASSERT_TRUE(std::holds_alternative<ResourceCapacityModel>(capacity_result));
  EXPECT_EQ(overrides.size(), 1U);

  const ResourceCapacityModel& capacities = std::get<ResourceCapacityModel>(capacity_result);
  const std::vector<ResourcePrice> prices = {
      ResourcePrice{.resource = resource, .price_per_usage_unit = 7},
  };
  const PriceSnapshotResult price_result =
      BuildPriceSnapshot(kPriceSnapshotSchemaVersion, capacities, 3, prices);
  ASSERT_TRUE(std::holds_alternative<PriceSnapshot>(price_result));
  EXPECT_EQ(prices.size(), 1U);
  EXPECT_EQ(std::get<PriceSnapshot>(price_result).prices(), prices);
}

TEST(OneWorldAllocatorTest, EnforcesBoundsAndCheckedCostArithmetic) {
  const CandidateFixture fixture = ProductionCandidates();
  OneWorldAllocationRequest bounded = BaseRequest(fixture);
  bounded.limits.maximum_candidates = 1;
  bounded.pools = {CandidatePool{.net = fixture.base_request.net,
                                 .candidates = {fixture.first, fixture.alternate}}};
  EXPECT_EQ(Failed(AllocateOneWorld(bounded)).invariant_id, "allocator.input.candidate_budget.v1");

  const EdgeResourceKey resource = AtomicResources(fixture.first).front();
  OneWorldAllocationRequest resource_bounded = BaseRequest(fixture);
  resource_bounded.capacities =
      Capacities(fixture, 1, {ResourceCapacityOverride{.resource = resource, .capacity_units = 1}});
  resource_bounded.prices =
      Prices(resource_bounded.capacities, 0,
             {ResourcePrice{.resource = resource, .price_per_usage_unit = 1}});
  resource_bounded.limits.maximum_resource_records = 1;
  EXPECT_EQ(Failed(AllocateOneWorld(resource_bounded)).invariant_id,
            "allocator.input.resource_record_budget.v1");

  OneWorldAllocationRequest overflowing = BaseRequest(fixture);
  overflowing.intrinsic_cost_weight = std::numeric_limits<std::uint64_t>::max();
  overflowing.pools = {
      CandidatePool{.net = fixture.base_request.net, .candidates = {fixture.first}}};
  EXPECT_EQ(Failed(AllocateOneWorld(overflowing)).code, AllocationErrorCode::kCostOverflow);
}

TEST(OneWorldAllocatorTest, RejectsStaleResourceStateAndEveryIncompatibleInputSchema) {
  const CandidateFixture fixture = ProductionCandidates();
  OneWorldAllocationRequest stale = BaseRequest(fixture);
  stale.associations.board_content_hash = 999;
  EXPECT_EQ(Failed(AllocateOneWorld(stale)).invariant_id,
            "allocator.resource_state.association.v1");

  OneWorldAllocationRequest incompatible = BaseRequest(fixture);
  incompatible.schema_version = 3;
  EXPECT_EQ(Failed(AllocateOneWorld(incompatible)).code, AllocationErrorCode::kUnsupportedSchema);
  EXPECT_TRUE(std::holds_alternative<AllocationError>(
      BuildResourceCapacityModel(2, fixture.board, fixture.compiled, 1, {})));
  EXPECT_TRUE(std::holds_alternative<AllocationError>(
      BuildPriceSnapshot(2, incompatible.capacities, 0, {})));
}

TEST(OneWorldAllocatorTest, RetainsExplicitResourceRecordProvenanceInReplayIdentity) {
  const CandidateFixture fixture = ProductionCandidates();
  const EdgeResourceKey resource = AtomicResources(fixture.first).front();
  OneWorldAllocationRequest implicit = BaseRequest(fixture);
  implicit.pools = {CandidatePool{.net = fixture.base_request.net, .candidates = {fixture.first}}};
  OneWorldAllocationRequest explicit_zero_price = implicit;
  explicit_zero_price.prices =
      Prices(explicit_zero_price.capacities, 0,
             {ResourcePrice{.resource = resource, .price_per_usage_unit = 0}});
  OneWorldAllocationRequest explicit_default_capacity = implicit;
  explicit_default_capacity.capacities =
      Capacities(fixture, 1, {ResourceCapacityOverride{.resource = resource, .capacity_units = 1}});
  explicit_default_capacity.prices = Prices(explicit_default_capacity.capacities);

  const OneWorldAllocation implicit_world = Allocated(AllocateOneWorld(implicit));
  const OneWorldAllocation price_world = Allocated(AllocateOneWorld(explicit_zero_price));
  const OneWorldAllocation capacity_world = Allocated(AllocateOneWorld(explicit_default_capacity));
  const ResourceUsage* implicit_usage = FindUsage(implicit_world, resource);
  const ResourceUsage* price_usage = FindUsage(price_world, resource);
  const ResourceUsage* capacity_usage = FindUsage(capacity_world, resource);
  ASSERT_NE(implicit_usage, nullptr);
  ASSERT_NE(price_usage, nullptr);
  ASSERT_NE(capacity_usage, nullptr);
  EXPECT_FALSE(implicit_usage->has_explicit_price);
  EXPECT_TRUE(price_usage->has_explicit_price);
  EXPECT_FALSE(implicit_usage->has_capacity_override);
  EXPECT_TRUE(capacity_usage->has_capacity_override);
  EXPECT_NE(implicit_world.world_checksum, price_world.world_checksum);
  EXPECT_NE(implicit_world.world_checksum, capacity_world.world_checksum);
}

TEST(OneWorldAllocatorTest, PreservesWorkloadFreeV1ReplayEncoding) {
  const CandidateFixture fixture = ProductionCandidates();
  OneWorldAllocationRequest legacy = BaseRequest(fixture);
  legacy.schema_version = kOneWorldAllocationSchemaVersionV1;
  legacy.pools = {
      CandidatePool{.net = fixture.base_request.net, .candidates = {fixture.first}},
  };
  OneWorldAllocationRequest current = legacy;
  current.schema_version = kOneWorldAllocationSchemaVersion;

  const OneWorldAllocation legacy_world = Allocated(AllocateOneWorld(legacy));
  const OneWorldAllocation current_world = Allocated(AllocateOneWorld(current));

  EXPECT_EQ(legacy_world.schema_version, kOneWorldAllocationSchemaVersionV1);
  EXPECT_EQ(legacy_world.workload_checksum, 0U);
  EXPECT_EQ(legacy_world.world_checksum, internal::ComputeOneWorldChecksumV1(legacy_world));
  EXPECT_EQ(current_world.world_checksum, internal::ComputeOneWorldChecksumV2(current_world));
  EXPECT_NE(legacy_world.world_checksum, current_world.world_checksum);
}

TEST(OneWorldAllocatorTest, RejectsNonbinaryCapacityAndNonphysicalEdgeEndpoints) {
  const CandidateFixture fixture = ProductionCandidates();
  const ResourceCapacityModelResult nonbinary_default = BuildResourceCapacityModel(
      kResourceCapacityModelSchemaVersion, fixture.board, fixture.compiled, 2, {});
  ASSERT_TRUE(std::holds_alternative<AllocationError>(nonbinary_default));
  EXPECT_EQ(std::get<AllocationError>(nonbinary_default).code,
            AllocationErrorCode::kInvalidConfiguration);

  const ResourceCapacityModelResult nonbinary_override = BuildResourceCapacityModel(
      kResourceCapacityModelSchemaVersion, fixture.board, fixture.compiled, 1,
      {ResourceCapacityOverride{.resource = AtomicResources(fixture.first).front(),
                                .capacity_units = 2}});
  ASSERT_TRUE(std::holds_alternative<AllocationError>(nonbinary_override));
  EXPECT_EQ(std::get<AllocationError>(nonbinary_override).invariant_id,
            "allocator.capacity.binary_physical_edge.v1");

  const PriceSnapshotResult overflowing_edge =
      BuildPriceSnapshot(kPriceSnapshotSchemaVersion, Capacities(fixture), 0,
                         {ResourcePrice{
                             .resource =
                                 EdgeResourceKey{
                                     .layer = 0,
                                     .lattice_x = std::numeric_limits<std::int64_t>::max(),
                                     .lattice_y = 0,
                                     .direction = geometry_compiler::Direction::kEast,
                                 },
                             .price_per_usage_unit = 1,
                         }});
  ASSERT_TRUE(std::holds_alternative<AllocationError>(overflowing_edge));
  EXPECT_EQ(std::get<AllocationError>(overflowing_edge).code,
            AllocationErrorCode::kInvalidResource);
}

TEST(OneWorldAllocatorTest, CompressedMultiStreamSweepMatchesIndependentFlatOracle) {
  const CandidateFixture fixture = ProductionCandidates();
  std::vector<StoredCandidate> selected;
  for (int repeat = 0; repeat < 32; ++repeat) {
    selected.push_back(fixture.first);
    selected.push_back(fixture.alternate);
  }
  const std::vector<internal::AccumulatedResourceUse> expected = FlatAccumulationOracle(selected);

  internal::SelectedResourceAccumulationResult result =
      internal::AccumulateSelectedResources(selected, kMaximumAllocatorExpandedResourceUsesV1);
  ASSERT_TRUE(std::holds_alternative<internal::SelectedResourceAccumulation>(result));
  const internal::SelectedResourceAccumulation accumulation =
      std::get<internal::SelectedResourceAccumulation>(std::move(result));
  EXPECT_EQ(accumulation.resources, expected);
  EXPECT_TRUE(std::ranges::any_of(accumulation.resources,
                                  [](const auto& use) { return use.usage_units == 32U; }));
  const std::uint64_t expected_uses =
      32U * (AtomicResources(fixture.first).size() + AtomicResources(fixture.alternate).size());
  const std::uint64_t expected_events =
      64U * (fixture.first->data().resources.size() + fixture.alternate->data().resources.size());
  EXPECT_EQ(accumulation.logical_resource_uses, expected_uses);
  EXPECT_EQ(accumulation.materialized_resource_edges, expected.size());
  EXPECT_EQ(accumulation.span_boundary_events, expected_events);
  EXPECT_LT(accumulation.span_boundary_events, accumulation.logical_resource_uses);

  std::ranges::reverse(selected);
  const auto reversed = std::get<internal::SelectedResourceAccumulation>(
      internal::AccumulateSelectedResources(selected, kMaximumAllocatorExpandedResourceUsesV1));
  EXPECT_EQ(reversed.resources, accumulation.resources);
  EXPECT_EQ(reversed.logical_resource_uses, accumulation.logical_resource_uses);
  EXPECT_EQ(reversed.materialized_resource_edges, accumulation.materialized_resource_edges);
  EXPECT_EQ(reversed.span_boundary_events, accumulation.span_boundary_events);
}

TEST(OneWorldAllocatorTest, SpanSweepCoversEveryDirectionOverlapAdjacencyAndSparseGaps) {
  using geometry_compiler::Direction;
  const std::vector<candidates::PhysicalEdgeSpan> spans = {
      {.layer = 0,
       .lattice_x = 0,
       .lattice_y = 0,
       .direction = Direction::kEast,
       .edge_count = 3,
       .usage_units = 1},
      {.layer = 0,
       .lattice_x = 1,
       .lattice_y = 0,
       .direction = Direction::kEast,
       .edge_count = 1,
       .usage_units = 2},
      {.layer = 0,
       .lattice_x = 3,
       .lattice_y = 0,
       .direction = Direction::kEast,
       .edge_count = 2,
       .usage_units = 1},
      {.layer = 0,
       .lattice_x = 1'000'000'000,
       .lattice_y = 0,
       .direction = Direction::kEast,
       .edge_count = 1,
       .usage_units = 1},
      {.layer = 1,
       .lattice_x = 5,
       .lattice_y = 0,
       .direction = Direction::kNorth,
       .edge_count = 3,
       .usage_units = 1},
      {.layer = 1,
       .lattice_x = 5,
       .lattice_y = 2,
       .direction = Direction::kNorth,
       .edge_count = 3,
       .usage_units = 2},
      {.layer = 2,
       .lattice_x = 0,
       .lattice_y = 10,
       .direction = Direction::kNorthEast,
       .edge_count = 3,
       .usage_units = 1},
      {.layer = 2,
       .lattice_x = 1,
       .lattice_y = 11,
       .direction = Direction::kNorthEast,
       .edge_count = 1,
       .usage_units = 3},
      {.layer = 3,
       .lattice_x = 0,
       .lattice_y = 10,
       .direction = Direction::kNorthWest,
       .edge_count = 3,
       .usage_units = 1},
      {.layer = 3,
       .lattice_x = 1,
       .lattice_y = 9,
       .direction = Direction::kNorthWest,
       .edge_count = 3,
       .usage_units = 2},
  };
  const std::array<internal::ResourceSpanStream, 2> streams = {
      internal::ResourceSpanStream(spans.data(), 5),
      internal::ResourceSpanStream(spans.data() + 5, spans.size() - 5),
  };
  const auto result =
      internal::AccumulateResourceSpanStreams(streams, kMaximumAllocatorExpandedResourceUsesV1);
  ASSERT_TRUE(std::holds_alternative<internal::SelectedResourceAccumulation>(result));
  const auto& accumulation = std::get<internal::SelectedResourceAccumulation>(result);
  const std::vector<internal::AccumulatedResourceUse> expected = FlatSpanOracle(spans);
  EXPECT_EQ(accumulation.resources, expected);
  EXPECT_EQ(accumulation.materialized_resource_edges, expected.size());
  EXPECT_EQ(accumulation.span_boundary_events, spans.size() * 2U);

  const std::array invalid = {candidates::PhysicalEdgeSpan{
      .layer = 0,
      .lattice_x = std::numeric_limits<std::int64_t>::max(),
      .lattice_y = 0,
      .direction = Direction::kEast,
      .edge_count = 1,
      .usage_units = 1,
  }};
  const std::array invalid_streams = {internal::ResourceSpanStream(invalid)};
  const auto invalid_result = internal::AccumulateResourceSpanStreams(
      invalid_streams, kMaximumAllocatorExpandedResourceUsesV1);
  ASSERT_TRUE(std::holds_alternative<AllocationError>(invalid_result));
  EXPECT_EQ(std::get<AllocationError>(invalid_result).code,
            AllocationErrorCode::kCandidateInvariant);
}

TEST(OneWorldAllocatorTest, ExceptionEnvelopeReturnsStableAllocationFreeErrors) {
  const AllocationError bad_alloc =
      Failed(test_support::ExerciseOneWorldFailureEnvelope(test_support::OneWorldFault::kBadAlloc));
  EXPECT_EQ(bad_alloc.code, AllocationErrorCode::kResourceExhausted);
  EXPECT_EQ(bad_alloc.invariant_id, "allocator.host_memory_exhausted.v1");
  const AllocationError length_error = Failed(
      test_support::ExerciseOneWorldFailureEnvelope(test_support::OneWorldFault::kLengthError));
  EXPECT_EQ(length_error.code, AllocationErrorCode::kResourceExhausted);
  EXPECT_EQ(length_error.invariant_id, "allocator.host_container_exhausted.v1");
}

TEST(OneWorldAllocatorTest, WorldChecksumEncodingHasDiscriminatingGoldenVector) {
  OneWorldAllocation world{
      .schema_version = 1,
      .associations = AllocationAssociations{.board_content_hash = 2,
                                             .compiler_profile_fingerprint = 3,
                                             .geometry_compiler_version = 5},
      .workload_checksum = 67,
      .price_iteration = 7,
      .default_capacity_units = 1,
      .intrinsic_cost_weight = 11,
      .selections =
          {
              NetSelection{
                  .net = board_ir::EntityRef{.id = 13, .generation = 17},
                  .status = NetSelectionStatus::kSelected,
                  .candidate_id = candidates::CandidateId{.high = 19, .low = 23},
                  .candidate_payload_checksum = 29,
                  .candidate = nullptr,
                  .intrinsic_cost = 31,
                  .price_cost = 37,
                  .selection_score = 41,
              },
              NetSelection{
                  .net = board_ir::EntityRef{.id = 43, .generation = 47},
                  .status = NetSelectionStatus::kNoAdmissibleCandidate,
                  .candidate_id = std::nullopt,
                  .candidate_payload_checksum = std::nullopt,
                  .candidate = nullptr,
                  .intrinsic_cost = 53,
                  .price_cost = 59,
                  .selection_score = 61,
              },
          },
      .resources =
          {
              ResourceUsage{
                  .resource =
                      EdgeResourceKey{.layer = 67,
                                      .lattice_x = -71,
                                      .lattice_y = 73,
                                      .direction = geometry_compiler::Direction::kNorthEast},
                  .has_capacity_override = true,
                  .capacity_units = 0,
                  .usage_units = 79,
                  .overuse_units = 83,
                  .has_explicit_price = true,
                  .price_per_usage_unit = 89,
              },
              ResourceUsage{
                  .resource =
                      EdgeResourceKey{.layer = 97,
                                      .lattice_x = 101,
                                      .lattice_y = -103,
                                      .direction = geometry_compiler::Direction::kNorthWest},
                  .has_capacity_override = false,
                  .capacity_units = 1,
                  .usage_units = 107,
                  .overuse_units = 109,
                  .has_explicit_price = false,
                  .price_per_usage_unit = 113,
              },
          },
      .selected_net_count = 127,
      .no_candidate_net_count = 131,
      .overused_resource_count = 137,
      .total_overuse_units = 139,
      .total_intrinsic_cost = 149,
      .total_selection_score = 151,
      .scoring_span_queries = 157,
      .scoring_price_matches = 163,
      .selected_logical_resource_uses = 167,
      .accounting_materialized_resource_edges = 173,
      .accounting_span_boundary_events = 179,
      .world_checksum = 181,
  };

  EXPECT_EQ(internal::ComputeOneWorldChecksumV1(world), 7'843'367'258'925'855'534ULL);
  world.schema_version = kOneWorldAllocationSchemaVersion;
  EXPECT_EQ(internal::ComputeOneWorldChecksumV2(world), 6'143'441'878'869'362'651ULL);
}

}  // namespace
}  // namespace apgar::allocator
