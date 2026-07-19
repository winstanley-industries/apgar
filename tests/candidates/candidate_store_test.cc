#include "apgar/candidates/candidate_store.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <memory>
#include <set>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include "apgar/board_ir/board.h"
#include "apgar/candidates/route_candidate.h"
#include "apgar/geometry_compiler/compiled_board.h"
#include "apgar/routing/candidate_policy.h"
#include "tests/support/board_builder.h"
#include "tests/support/candidate_builder.h"
#include "tests/support/compiler_builder.h"
#include "tests/support/google_test.h"
#include "tests/support/routing_builder.h"

namespace apgar::candidates {

class CandidateStoreTestPeer {
 public:
  [[nodiscard]] static RouteCandidate ForceSignatures(const RouteCandidate& candidate,
                                                      CandidateSignature geometry_signature,
                                                      CandidateSignature resource_signature) {
    GeneratedRouteCandidate data = candidate.data_;
    data.geometry_signature = geometry_signature;
    data.resource_signature = resource_signature;
    return RouteCandidate(std::move(data));
  }

  [[nodiscard]] static RouteCandidate ForceResources(const RouteCandidate& candidate,
                                                     const RouteCandidate& resource_source) {
    GeneratedRouteCandidate data = candidate.data_;
    data.resources = resource_source.data_.resources;
    data.resource_signature = resource_source.data_.resource_signature;
    return RouteCandidate(std::move(data));
  }

  [[nodiscard]] static RouteCandidate ForceLogicalBytes(const RouteCandidate& candidate,
                                                        std::uint64_t logical_bytes) {
    GeneratedRouteCandidate data = candidate.data_;
    data.logical_bytes = logical_bytes;
    return RouteCandidate(std::move(data));
  }

  [[nodiscard]] static CandidateStoreAdmissionResult Publish(CandidateStore& store,
                                                             RouteCandidate candidate) {
    std::scoped_lock lock(store.mutex_);
    return store.PublishAcceptedLocked(std::move(candidate));
  }

  static void ForcePool(CandidateStore& store, std::vector<RouteCandidate> candidates) {
    std::scoped_lock lock(store.mutex_);
    store.candidates_.clear();
    for (RouteCandidate& candidate : candidates) {
      store.candidates_.push_back(std::make_shared<const RouteCandidate>(std::move(candidate)));
    }
  }

  static void RetainRejection(CandidateStore& store, CandidateRejection rejection) {
    std::scoped_lock lock(store.mutex_);
    store.RetainRejectionLocked(std::move(rejection));
  }
};

namespace {

using board_ir::BoardData;
using board_ir::BoardSnapshot;
using geometry_compiler::CompiledBoard;
using routing::CpuRouteRequest;
using routing::LayerSegment;
using test_support::Compile;
using test_support::NormalizePolicy;
using test_support::Snapshot;
using test_support::TwoTerminalRequest;

struct CandidateCase {
  CpuRouteRequest request;
  GeneratedRouteCandidate generated;
};

[[nodiscard]] CandidateCase MakeCase(const BoardSnapshot& board, const CompiledBoard& compiled,
                                     std::uint32_t ordinal, std::uint64_t query_identity,
                                     std::span<const LayerSegment> segments,
                                     std::uint64_t reported_cost) {
  CpuRouteRequest request = TwoTerminalRequest(board, 0, 0);
  request.candidate_policy.deterministic_seed = 0x1234U;
  request.candidate_policy.candidate_ordinal = ordinal;
  const routing::NormalizedCandidateGenerationPolicy policy = NormalizePolicy(compiled, request);
  CandidateDraftBuildResult result = BuildGeneratedCandidateFromPlanarRoute(
      board, compiled, request, policy, AssociationsFor(board, compiled), policy.identity,
      reported_cost, segments,
      test_support::CpuCandidateProvenance(policy.policy, 99, query_identity));
  EXPECT_TRUE(std::holds_alternative<GeneratedRouteCandidate>(result))
      << (std::holds_alternative<CandidateBuildError>(result)
              ? std::get<CandidateBuildError>(result).detail
              : std::string{});
  if (!std::holds_alternative<GeneratedRouteCandidate>(result)) {
    std::abort();
  }
  return CandidateCase{
      .request = std::move(request),
      .generated = std::get<GeneratedRouteCandidate>(std::move(result)),
  };
}

[[nodiscard]] std::array<CandidateCase, 3> ThreeCandidates(const BoardSnapshot& board,
                                                           const CompiledBoard& compiled) {
  const std::array straight = {
      LayerSegment{.layer = 0,
                   .centerline = {.start = {.x = 0, .y = 0}, .end = {.x = 100, .y = 0}}},
  };
  const std::array top = {
      LayerSegment{.layer = 0, .centerline = {.start = {.x = 0, .y = 0}, .end = {.x = 20, .y = 0}}},
      LayerSegment{.layer = 0,
                   .centerline = {.start = {.x = 20, .y = 0}, .end = {.x = 20, .y = 20}}},
      LayerSegment{.layer = 0,
                   .centerline = {.start = {.x = 20, .y = 20}, .end = {.x = 80, .y = 20}}},
      LayerSegment{.layer = 0,
                   .centerline = {.start = {.x = 80, .y = 20}, .end = {.x = 80, .y = 0}}},
      LayerSegment{.layer = 0,
                   .centerline = {.start = {.x = 80, .y = 0}, .end = {.x = 100, .y = 0}}},
  };
  const std::array bottom = {
      LayerSegment{.layer = 0, .centerline = {.start = {.x = 0, .y = 0}, .end = {.x = 20, .y = 0}}},
      LayerSegment{.layer = 0,
                   .centerline = {.start = {.x = 20, .y = 0}, .end = {.x = 20, .y = -20}}},
      LayerSegment{.layer = 0,
                   .centerline = {.start = {.x = 20, .y = -20}, .end = {.x = 80, .y = -20}}},
      LayerSegment{.layer = 0,
                   .centerline = {.start = {.x = 80, .y = -20}, .end = {.x = 80, .y = 0}}},
      LayerSegment{.layer = 0,
                   .centerline = {.start = {.x = 80, .y = 0}, .end = {.x = 100, .y = 0}}},
  };
  return {
      MakeCase(board, compiled, 0, 1, straight, 100),
      MakeCase(board, compiled, 1, 2, top, 152),
      MakeCase(board, compiled, 2, 3, bottom, 152),
  };
}

[[nodiscard]] std::vector<CandidateId> Ids(std::span<const StoredCandidate> candidates) {
  std::vector<CandidateId> ids;
  ids.reserve(candidates.size());
  for (const StoredCandidate& candidate : candidates) {
    ids.push_back(candidate->id());
  }
  return ids;
}

[[nodiscard]] CandidateStoreConfig StoreConfig(std::uint64_t count = 8,
                                               std::uint64_t bytes = 1'000'000) {
  return CandidateStoreConfig{.maximum_candidates_per_net = count,
                              .maximum_candidate_bytes_per_net = bytes,
                              .maximum_rejection_records = 128};
}

TEST(CandidateStoreTest, MultiPolicyBatchAndInsertionPermutationsHaveStableRetention) {
  BoardData data = test_support::ValidM1BoardData();
  data.obstacles.clear();
  const BoardSnapshot board = Snapshot(std::move(data));
  const CompiledBoard compiled = Compile(board, test_support::DefaultCompilerProfile({0}));
  const std::array<CandidateCase, 3> cases = ThreeCandidates(board, compiled);
  const board_ir::EntityRef net = cases[0].request.net;

  CandidateStore batch_store(StoreConfig(2));
  std::vector<CandidateAdmissionItem> items;
  for (std::size_t index : std::array<std::size_t, 3>{2, 0, 1}) {
    items.push_back(CandidateAdmissionItem{.request = cases[index].request,
                                           .generated = cases[index].generated});
  }
  const std::vector<CandidateStoreAdmissionResult> batch_results =
      batch_store.AdmitBatch(board, compiled, std::move(items));
  ASSERT_EQ(batch_results.size(), 3U);
  const std::vector<CandidateId> expected = Ids(batch_store.Enumerate(net));
  ASSERT_EQ(expected.size(), 2U);
  std::set<CandidateSignature> retained_resource_signatures;
  for (const StoredCandidate& candidate : batch_store.Enumerate(net)) {
    retained_resource_signatures.insert(candidate->data().resource_signature);
  }
  EXPECT_EQ(retained_resource_signatures.size(), 2U);

  std::array<std::size_t, 3> order = {0, 1, 2};
  do {
    CandidateStore store(StoreConfig(2));
    for (std::size_t index : order) {
      const CandidateAdmissionContext context{
          .board = board, .compiled_board = compiled, .request = cases[index].request};
      (void)store.Admit(context, cases[index].generated);
    }
    EXPECT_EQ(Ids(store.Enumerate(net)), expected);
  } while (std::ranges::next_permutation(order).found);

  CandidateStore concurrent(StoreConfig(2));
  std::vector<std::thread> threads;
  for (const CandidateCase& candidate_case : cases) {
    threads.emplace_back([&board, &compiled, &concurrent, candidate_case] {
      const CandidateAdmissionContext context{
          .board = board,
          .compiled_board = compiled,
          .request = candidate_case.request,
      };
      (void)concurrent.Admit(context, candidate_case.generated);
    });
  }
  for (std::thread& thread : threads) {
    thread.join();
  }
  EXPECT_EQ(Ids(concurrent.Enumerate(net)), expected);
}

TEST(CandidateStoreTest, BatchResultsReflectFinalRetentionAfterLaterEviction) {
  BoardData data = test_support::ValidM1BoardData();
  data.obstacles.clear();
  const BoardSnapshot board = Snapshot(std::move(data));
  const CompiledBoard compiled = Compile(board, test_support::DefaultCompilerProfile({0}));
  std::array<CandidateCase, 3> cases = ThreeCandidates(board, compiled);
  GeneratedRouteCandidate& worse = cases[1].generated;
  const GeneratedRouteCandidate& better = cases[0].generated;
  bool sorted_before_better = worse.id < better.id;
  for (std::uint64_t query = 4; !sorted_before_better && query < 4096; ++query) {
    worse.provenance.query_identity = query;
    worse.id =
        DeriveCandidateId(worse.net, worse.associations, worse.policy_identity, worse.provenance);
    ASSERT_FALSE(FinalizeGeneratedCandidateDraft(worse).has_value());
    sorted_before_better = worse.id < better.id;
  }
  ASSERT_TRUE(sorted_before_better);

  CandidateStore store(StoreConfig(1));
  std::vector<CandidateAdmissionItem> items = {
      CandidateAdmissionItem{.request = cases[0].request, .generated = cases[0].generated},
      CandidateAdmissionItem{.request = cases[1].request, .generated = cases[1].generated},
  };
  const std::vector<CandidateStoreAdmissionResult> results =
      store.AdmitBatch(board, compiled, std::move(items));
  ASSERT_EQ(results.size(), 2U);
  ASSERT_TRUE(std::holds_alternative<CandidateRejection>(results[0]));
  EXPECT_EQ(std::get<CandidateRejection>(results[0]).candidate_id,
            std::optional<CandidateId>(worse.id));
  EXPECT_EQ(std::get<CandidateRejection>(results[0]).code,
            CandidateRejectionCode::kBudgetExhausted);
  ASSERT_TRUE(std::holds_alternative<StoredCandidate>(results[1]));
  EXPECT_EQ(std::get<StoredCandidate>(results[1])->id(), better.id);
  ASSERT_EQ(store.Enumerate(better.net).size(), 1U);
  EXPECT_EQ(store.Enumerate(better.net).front()->id(), better.id);
}

TEST(CandidateStoreTest, BatchRetentionUsesTheCompletePoolNotPrefixHistory) {
  BoardData data = test_support::ValidM1BoardData();
  data.obstacles.clear();
  const BoardSnapshot board = Snapshot(std::move(data));
  geometry_compiler::CompilerProfile profile = test_support::DefaultCompilerProfile({0});
  profile.compilation_roi.min.y = -80;
  profile.compilation_roi.max.y = 80;
  profile.active_regions.front().bounds = profile.compilation_roi;
  const CompiledBoard compiled = Compile(board, std::move(profile));

  const std::array low_orthogonal_high_diagonal = {
      LayerSegment{.layer = 0, .centerline = {.start = {.x = 0, .y = 0}, .end = {.x = 0, .y = 10}}},
      LayerSegment{.layer = 0,
                   .centerline = {.start = {.x = 0, .y = 10}, .end = {.x = 10, .y = 20}}},
      LayerSegment{.layer = 0,
                   .centerline = {.start = {.x = 10, .y = 20}, .end = {.x = 20, .y = 10}}},
      LayerSegment{.layer = 0,
                   .centerline = {.start = {.x = 20, .y = 10}, .end = {.x = 30, .y = 20}}},
      LayerSegment{.layer = 0,
                   .centerline = {.start = {.x = 30, .y = 20}, .end = {.x = 40, .y = 10}}},
      LayerSegment{.layer = 0,
                   .centerline = {.start = {.x = 40, .y = 10}, .end = {.x = 50, .y = 20}}},
      LayerSegment{.layer = 0,
                   .centerline = {.start = {.x = 50, .y = 20}, .end = {.x = 60, .y = 10}}},
      LayerSegment{.layer = 0,
                   .centerline = {.start = {.x = 60, .y = 10}, .end = {.x = 70, .y = 20}}},
      LayerSegment{.layer = 0,
                   .centerline = {.start = {.x = 70, .y = 20}, .end = {.x = 80, .y = 10}}},
      LayerSegment{.layer = 0,
                   .centerline = {.start = {.x = 80, .y = 10}, .end = {.x = 90, .y = 20}}},
      LayerSegment{.layer = 0,
                   .centerline = {.start = {.x = 90, .y = 20}, .end = {.x = 100, .y = 10}}},
      LayerSegment{.layer = 0,
                   .centerline = {.start = {.x = 100, .y = 10}, .end = {.x = 100, .y = 0}}},
  };
  const std::array first_dominated = {
      LayerSegment{.layer = 0, .centerline = {.start = {.x = 0, .y = 0}, .end = {.x = 0, .y = 20}}},
      LayerSegment{.layer = 0,
                   .centerline = {.start = {.x = 0, .y = 20}, .end = {.x = 10, .y = 30}}},
      LayerSegment{.layer = 0,
                   .centerline = {.start = {.x = 10, .y = 30}, .end = {.x = 90, .y = 30}}},
      LayerSegment{.layer = 0,
                   .centerline = {.start = {.x = 90, .y = 30}, .end = {.x = 100, .y = 20}}},
      LayerSegment{.layer = 0,
                   .centerline = {.start = {.x = 100, .y = 20}, .end = {.x = 100, .y = 0}}},
  };
  const std::array second_dominated = {
      LayerSegment{.layer = 0, .centerline = {.start = {.x = 0, .y = 0}, .end = {.x = 0, .y = 20}}},
      LayerSegment{.layer = 0,
                   .centerline = {.start = {.x = 0, .y = 20}, .end = {.x = 20, .y = 40}}},
      LayerSegment{.layer = 0,
                   .centerline = {.start = {.x = 20, .y = 40}, .end = {.x = 80, .y = 40}}},
      LayerSegment{.layer = 0,
                   .centerline = {.start = {.x = 80, .y = 40}, .end = {.x = 100, .y = 20}}},
      LayerSegment{.layer = 0,
                   .centerline = {.start = {.x = 100, .y = 20}, .end = {.x = 100, .y = 0}}},
  };
  const std::array straight = {
      LayerSegment{.layer = 0,
                   .centerline = {.start = {.x = 0, .y = 0}, .end = {.x = 100, .y = 0}}},
  };

  std::array<CandidateCase, 4> cases = {
      MakeCase(board, compiled, 0, 10, first_dominated, 160),
      MakeCase(board, compiled, 1, 20, second_dominated, 168),
      MakeCase(board, compiled, 2, 30, low_orthogonal_high_diagonal, 193),
      MakeCase(board, compiled, 3, 40, straight, 100),
  };
  for (std::size_t index = 0; index < cases.size(); ++index) {
    bool found_bucket = false;
    for (std::uint64_t query_identity = index * 100'000U + 1U;
         query_identity < (index + 1U) * 100'000U; ++query_identity) {
      GeneratedRouteCandidate& candidate = cases[index].generated;
      candidate.provenance.query_identity = query_identity;
      candidate.id = DeriveCandidateId(candidate.net, candidate.associations,
                                       candidate.policy_identity, candidate.provenance);
      if ((candidate.id.high >> 62U) == index) {
        ASSERT_FALSE(FinalizeGeneratedCandidateDraft(candidate).has_value());
        found_bucket = true;
        break;
      }
    }
    ASSERT_TRUE(found_bucket);
  }

  CandidateStore store(StoreConfig(2));
  std::vector<CandidateAdmissionItem> items;
  for (const std::size_t index : std::array<std::size_t, 4>{3, 2, 1, 0}) {
    items.push_back(CandidateAdmissionItem{.request = cases[index].request,
                                           .generated = cases[index].generated});
  }
  const std::vector<CandidateStoreAdmissionResult> results =
      store.AdmitBatch(board, compiled, std::move(items));
  ASSERT_EQ(results.size(), 4U);
  const std::vector<StoredCandidate> retained = store.Enumerate(cases.front().request.net);
  ASSERT_EQ(retained.size(), 2U);
  EXPECT_EQ(retained[0]->data().metrics.intrinsic_base_cost, 100U);
  EXPECT_EQ(retained[1]->data().metrics.intrinsic_base_cost, 193U);
  EXPECT_FALSE(CandidateMetricsDominate(*retained[0], *retained[1]));
}

TEST(CandidateStoreTest, SameIdDifferentPayloadBatchResultsIgnoreInputOrder) {
  BoardData data = test_support::ValidM1BoardData();
  data.obstacles.clear();
  const BoardSnapshot board = Snapshot(std::move(data));
  const CompiledBoard compiled = Compile(board, test_support::DefaultCompilerProfile({0}));
  const std::array straight = {
      LayerSegment{.layer = 0,
                   .centerline = {.start = {.x = 0, .y = 0}, .end = {.x = 100, .y = 0}}},
  };
  const std::array shoulder = {
      LayerSegment{.layer = 0, .centerline = {.start = {.x = 0, .y = 0}, .end = {.x = 20, .y = 0}}},
      LayerSegment{.layer = 0,
                   .centerline = {.start = {.x = 20, .y = 0}, .end = {.x = 20, .y = 20}}},
      LayerSegment{.layer = 0,
                   .centerline = {.start = {.x = 20, .y = 20}, .end = {.x = 80, .y = 20}}},
      LayerSegment{.layer = 0,
                   .centerline = {.start = {.x = 80, .y = 20}, .end = {.x = 80, .y = 0}}},
      LayerSegment{.layer = 0,
                   .centerline = {.start = {.x = 80, .y = 0}, .end = {.x = 100, .y = 0}}},
  };
  const CandidateCase preferred = MakeCase(board, compiled, 0, 77, straight, 100);
  const CandidateCase duplicate = MakeCase(board, compiled, 0, 77, shoulder, 152);
  ASSERT_EQ(preferred.generated.id, duplicate.generated.id);
  ASSERT_NE(preferred.generated.payload_checksum, duplicate.generated.payload_checksum);

  const auto run = [&](std::array<std::size_t, 2> order) {
    CandidateStore store(StoreConfig());
    const std::array<const CandidateCase*, 2> candidates = {&preferred, &duplicate};
    std::vector<CandidateAdmissionItem> items;
    for (const std::size_t index : order) {
      items.push_back(CandidateAdmissionItem{
          .request = candidates[index]->request,
          .generated = candidates[index]->generated,
      });
    }
    const std::vector<CandidateStoreAdmissionResult> results =
        store.AdmitBatch(board, compiled, std::move(items));
    EXPECT_EQ(results.size(), 2U);
    EXPECT_TRUE(std::holds_alternative<StoredCandidate>(results[0]));
    EXPECT_TRUE(std::holds_alternative<CandidateRejection>(results[1]));
    if (results.size() != 2U || !std::holds_alternative<StoredCandidate>(results[0]) ||
        !std::holds_alternative<CandidateRejection>(results[1])) {
      return std::array<std::uint64_t, 3>{};
    }
    const StoredCandidate& stored = std::get<StoredCandidate>(results[0]);
    const CandidateRejection& rejection = std::get<CandidateRejection>(results[1]);
    EXPECT_EQ(stored->data().payload_checksum, preferred.generated.payload_checksum);
    EXPECT_EQ(rejection.code, CandidateRejectionCode::kDuplicateIdentity);
    EXPECT_EQ(rejection.candidate_payload_checksum,
              std::optional<std::uint64_t>(duplicate.generated.payload_checksum));
    return std::array{
        stored->data().payload_checksum,
        rejection.candidate_payload_checksum.value_or(0),
        static_cast<std::uint64_t>(rejection.code),
    };
  };

  EXPECT_EQ(run({0, 1}), run({1, 0}));

  CandidateStore pinned_store(StoreConfig(2));
  const CandidateAdmissionContext duplicate_context{
      .board = board,
      .compiled_board = compiled,
      .request = duplicate.request,
  };
  const CandidateAdmissionContext preferred_context{
      .board = board,
      .compiled_board = compiled,
      .request = preferred.request,
  };
  const CandidateStoreAdmissionResult initial =
      pinned_store.Admit(duplicate_context, duplicate.generated);
  ASSERT_TRUE(std::holds_alternative<StoredCandidate>(initial));
  ASSERT_FALSE(pinned_store.Pin(17, duplicate.generated.id).has_value());
  const CandidateStoreAdmissionResult pinned_duplicate =
      pinned_store.Admit(preferred_context, preferred.generated);
  ASSERT_TRUE(std::holds_alternative<CandidateRejection>(pinned_duplicate));
  EXPECT_EQ(std::get<CandidateRejection>(pinned_duplicate).code,
            CandidateRejectionCode::kDuplicateIdentity);
  const std::vector<StoredCandidate> retained = pinned_store.Enumerate(duplicate.request.net);
  ASSERT_EQ(retained.size(), 1U);
  EXPECT_EQ(retained.front()->data().payload_checksum, duplicate.generated.payload_checksum);
}

TEST(CandidateStoreTest, PinsFailClosedThenUnpinnedBetterCandidateEvicts) {
  BoardData data = test_support::ValidM1BoardData();
  data.obstacles.clear();
  const BoardSnapshot board = Snapshot(std::move(data));
  const CompiledBoard compiled = Compile(board, test_support::DefaultCompilerProfile({0}));
  const std::array<CandidateCase, 3> cases = ThreeCandidates(board, compiled);
  CandidateStore store(StoreConfig(1));
  const CandidateAdmissionContext worse_context{
      .board = board, .compiled_board = compiled, .request = cases[1].request};
  const CandidateAdmissionContext better_context{
      .board = board, .compiled_board = compiled, .request = cases[0].request};
  const CandidateStoreAdmissionResult first = store.Admit(worse_context, cases[1].generated);
  ASSERT_TRUE(std::holds_alternative<StoredCandidate>(first));
  const CandidateId pinned_id = std::get<StoredCandidate>(first)->id();
  EXPECT_FALSE(store.Pin(7, pinned_id).has_value());
  EXPECT_FALSE(store.Pin(7, pinned_id).has_value());

  const CandidateStoreAdmissionResult blocked = store.Admit(better_context, cases[0].generated);
  ASSERT_TRUE(std::holds_alternative<CandidateRejection>(blocked));
  EXPECT_EQ(std::get<CandidateRejection>(blocked).code, CandidateRejectionCode::kBudgetExhausted);
  ASSERT_EQ(store.Enumerate(cases[0].request.net).size(), 1U);
  EXPECT_EQ(store.Enumerate(cases[0].request.net).front()->id(), pinned_id);

  EXPECT_FALSE(store.Unpin(7, pinned_id).has_value());
  const CandidateStoreAdmissionResult admitted = store.Admit(better_context, cases[0].generated);
  ASSERT_TRUE(std::holds_alternative<StoredCandidate>(admitted));
  ASSERT_EQ(store.Enumerate(cases[0].request.net).size(), 1U);
  EXPECT_EQ(store.Enumerate(cases[0].request.net).front()->id(), cases[0].generated.id);
  ASSERT_TRUE(store.CandidateBytes(cases[0].request.net).has_value());
  EXPECT_EQ(*store.CandidateBytes(cases[0].request.net), cases[0].generated.logical_bytes);
}

TEST(CandidateStoreTest, DuplicateIdsGeometryAndResourcesRetainStableRepresentatives) {
  BoardData data = test_support::ValidM1BoardData();
  data.obstacles.clear();
  const BoardSnapshot board = Snapshot(std::move(data));
  const CompiledBoard compiled = Compile(board, test_support::DefaultCompilerProfile({0}));
  const std::array<CandidateCase, 3> cases = ThreeCandidates(board, compiled);
  CandidateStore store(StoreConfig());
  const CandidateAdmissionContext context{
      .board = board, .compiled_board = compiled, .request = cases[0].request};
  ASSERT_TRUE(std::holds_alternative<StoredCandidate>(store.Admit(context, cases[0].generated)));
  const CandidateStoreAdmissionResult duplicate_id = store.Admit(context, cases[0].generated);
  ASSERT_TRUE(std::holds_alternative<CandidateRejection>(duplicate_id));
  EXPECT_EQ(std::get<CandidateRejection>(duplicate_id).code,
            CandidateRejectionCode::kDuplicateIdentity);

  const std::array straight = {
      LayerSegment{.layer = 0,
                   .centerline = {.start = {.x = 0, .y = 0}, .end = {.x = 100, .y = 0}}},
  };
  const CandidateCase same_geometry = MakeCase(board, compiled, 9, 9, straight, 100);
  const CandidateAdmissionContext same_context{
      .board = board, .compiled_board = compiled, .request = same_geometry.request};
  (void)store.Admit(same_context, same_geometry.generated);
  EXPECT_EQ(store.Enumerate(cases[0].request.net).size(), 1U);
  EXPECT_TRUE(std::ranges::any_of(store.Rejections(), [](const CandidateRejection& rejection) {
    return rejection.code == CandidateRejectionCode::kDuplicateGeometry;
  }));

  const RouteCandidate straight_candidate =
      test_support::AcceptedCandidate(context, cases[0].generated);
  const CandidateAdmissionContext shoulder_context{
      .board = board, .compiled_board = compiled, .request = cases[1].request};
  RouteCandidate shoulder_candidate =
      test_support::AcceptedCandidate(shoulder_context, cases[1].generated);
  shoulder_candidate =
      CandidateStoreTestPeer::ForceResources(shoulder_candidate, straight_candidate);
  const CandidateStoreAdmissionResult duplicate_resources =
      CandidateStoreTestPeer::Publish(store, std::move(shoulder_candidate));
  ASSERT_TRUE(std::holds_alternative<CandidateRejection>(duplicate_resources));
  EXPECT_EQ(std::get<CandidateRejection>(duplicate_resources).code,
            CandidateRejectionCode::kDuplicateResources);
}

TEST(CandidateStoreTest, SignatureCollisionsUseFullEqualityAndNeverFalseDeduplicate) {
  BoardData data = test_support::ValidM1BoardData();
  data.obstacles.clear();
  const BoardSnapshot board = Snapshot(std::move(data));
  const CompiledBoard compiled = Compile(board, test_support::DefaultCompilerProfile({0}));
  const std::array<CandidateCase, 3> cases = ThreeCandidates(board, compiled);
  const CandidateAdmissionContext first_context{
      .board = board, .compiled_board = compiled, .request = cases[1].request};
  const CandidateAdmissionContext second_context{
      .board = board, .compiled_board = compiled, .request = cases[2].request};
  const RouteCandidate first = test_support::AcceptedCandidate(first_context, cases[1].generated);
  RouteCandidate collision = test_support::AcceptedCandidate(second_context, cases[2].generated);
  collision = CandidateStoreTestPeer::ForceSignatures(collision, first.data().geometry_signature,
                                                      first.data().resource_signature);

  CandidateStore store(StoreConfig(2));
  ASSERT_TRUE(
      std::holds_alternative<StoredCandidate>(CandidateStoreTestPeer::Publish(store, first)));
  ASSERT_TRUE(std::holds_alternative<StoredCandidate>(
      CandidateStoreTestPeer::Publish(store, std::move(collision))));
  EXPECT_EQ(store.Enumerate(cases[0].request.net).size(), 2U);
}

TEST(CandidateStoreTest, DiversityOverlapDominanceByteCapsAndExplicitPruneAreDeterministic) {
  BoardData data = test_support::ValidM1BoardData();
  data.obstacles.clear();
  const BoardSnapshot board = Snapshot(std::move(data));
  const CompiledBoard compiled = Compile(board, test_support::DefaultCompilerProfile({0}));
  const std::array<CandidateCase, 3> cases = ThreeCandidates(board, compiled);
  const CandidateAdmissionContext straight_context{
      .board = board, .compiled_board = compiled, .request = cases[0].request};
  const CandidateAdmissionContext top_context{
      .board = board, .compiled_board = compiled, .request = cases[1].request};
  const RouteCandidate straight =
      test_support::AcceptedCandidate(straight_context, cases[0].generated);
  const RouteCandidate top = test_support::AcceptedCandidate(top_context, cases[1].generated);
  EXPECT_TRUE(CandidateMetricsDominate(straight, top));
  EXPECT_FALSE(CandidateMetricsDominate(top, straight));
  EXPECT_NEAR(ResourceJaccardOverlap(straight, top), 0.2, 1e-12);
  EXPECT_NEAR(GeometricOverlapRatio(straight, top), 0.4, 1e-12);

  CandidateStore byte_limited(StoreConfig(2, straight.logical_bytes() + top.logical_bytes() - 1));
  ASSERT_TRUE(
      std::holds_alternative<StoredCandidate>(byte_limited.Admit(top_context, cases[1].generated)));
  ASSERT_TRUE(std::holds_alternative<StoredCandidate>(
      byte_limited.Admit(straight_context, cases[0].generated)));
  ASSERT_EQ(byte_limited.Enumerate(cases[0].request.net).size(), 1U);
  EXPECT_EQ(byte_limited.Enumerate(cases[0].request.net).front()->id(), straight.id());

  CandidateStore forced_overfull(StoreConfig(1));
  CandidateStoreTestPeer::ForcePool(forced_overfull, {straight, top});
  const std::optional<CandidateRejection> pruned = forced_overfull.Prune(cases[0].request.net);
  ASSERT_TRUE(pruned.has_value());
  EXPECT_EQ(pruned->code, CandidateRejectionCode::kBudgetExhausted);
  ASSERT_EQ(forced_overfull.Enumerate(cases[0].request.net).size(), 1U);
  EXPECT_EQ(forced_overfull.Enumerate(cases[0].request.net).front()->id(), straight.id());
}

TEST(CandidateStoreTest, GeometricOverlapUsesExactDbuProjectionNotResourceEdgeCounts) {
  BoardData data = test_support::ValidM1BoardData();
  data.obstacles.clear();
  const BoardSnapshot board = Snapshot(std::move(data));
  const CompiledBoard compiled = Compile(board, test_support::DefaultCompilerProfile({0}));
  const std::array<CandidateCase, 3> cases = ThreeCandidates(board, compiled);
  const CandidateAdmissionContext straight_context{
      .board = board,
      .compiled_board = compiled,
      .request = cases[0].request,
  };
  const CandidateAdmissionContext shoulder_context{
      .board = board,
      .compiled_board = compiled,
      .request = cases[1].request,
  };
  const RouteCandidate straight =
      test_support::AcceptedCandidate(straight_context, cases[0].generated);
  const RouteCandidate shoulder =
      test_support::AcceptedCandidate(shoulder_context, cases[1].generated);

  // Keep the shoulder's nonuniform 20/60/20 DBU exact geometry while making
  // its injected resource footprint indistinguishable from the straight path.
  // Geometric overlap remains the two 20 DBU collinear intervals over the
  // smaller path's 100 DBU projection; resource-edge counts are not its unit.
  const RouteCandidate misleading_resources =
      CandidateStoreTestPeer::ForceResources(shoulder, straight);
  EXPECT_NEAR(ResourceJaccardOverlap(straight, misleading_resources), 1.0, 1e-12);
  EXPECT_NEAR(GeometricOverlapRatio(straight, misleading_resources), 0.4, 1e-12);
}

TEST(CandidateStoreTest, CheckedByteEnumerationDetectsInjectedOverflow) {
  BoardData data = test_support::ValidM1BoardData();
  data.obstacles.clear();
  const BoardSnapshot board = Snapshot(std::move(data));
  const CompiledBoard compiled = Compile(board, test_support::DefaultCompilerProfile({0}));
  const std::array<CandidateCase, 3> cases = ThreeCandidates(board, compiled);
  const CandidateAdmissionContext first_context{
      .board = board, .compiled_board = compiled, .request = cases[1].request};
  const CandidateAdmissionContext second_context{
      .board = board, .compiled_board = compiled, .request = cases[2].request};
  RouteCandidate first = test_support::AcceptedCandidate(first_context, cases[1].generated);
  RouteCandidate second = test_support::AcceptedCandidate(second_context, cases[2].generated);
  first =
      CandidateStoreTestPeer::ForceLogicalBytes(first, std::numeric_limits<std::uint64_t>::max());
  second = CandidateStoreTestPeer::ForceLogicalBytes(second, 1);
  CandidateStore store(StoreConfig(2, std::numeric_limits<std::uint64_t>::max()));
  CandidateStoreTestPeer::ForcePool(store, {std::move(first), std::move(second)});
  EXPECT_FALSE(store.CandidateBytes(cases[0].request.net).has_value());
}

TEST(CandidateStoreTest, RejectionCapUsesCanonicalSchemaFieldOrder) {
  CandidateRejection missing_candidate;
  missing_candidate.candidate_id = std::nullopt;
  missing_candidate.net = board_ir::EntityRef{.id = 2, .generation = 0};
  missing_candidate.stage = CandidateLifecycleStage::kGenerated;
  missing_candidate.code = CandidateRejectionCode::kInvalidInput;
  missing_candidate.invariant_id = "candidate.test.missing_identity.v1";
  missing_candidate.detail = "missing candidate identity";
  CandidateRejection present_candidate;
  present_candidate.candidate_id = CandidateId{.high = 1, .low = 1};
  present_candidate.net = board_ir::EntityRef{.id = 1, .generation = 0};
  present_candidate.stage = CandidateLifecycleStage::kGenerated;
  present_candidate.code = CandidateRejectionCode::kInvalidInput;
  present_candidate.invariant_id = "candidate.test.present_identity.v1";
  present_candidate.detail = "present candidate identity";
  for (const bool reverse : {false, true}) {
    CandidateStore store(CandidateStoreConfig{
        .maximum_candidates_per_net = 1,
        .maximum_candidate_bytes_per_net = 1,
        .maximum_rejection_records = 1,
    });
    CandidateStoreTestPeer::RetainRejection(store, reverse ? present_candidate : missing_candidate);
    CandidateStoreTestPeer::RetainRejection(store, reverse ? missing_candidate : present_candidate);
    const std::vector<CandidateRejection> retained = store.Rejections();
    ASSERT_EQ(retained.size(), 1U);
    EXPECT_FALSE(retained.front().candidate_id.has_value());
    EXPECT_EQ(retained.front().net, missing_candidate.net);
  }
}

TEST(CandidateStoreTest, StoreRejectsAssociationDriftAcrossBoardSnapshots) {
  BoardData first_data = test_support::ValidM1BoardData();
  first_data.obstacles.clear();
  BoardData second_data = first_data;
  ++second_data.revision;
  const BoardSnapshot first_board = Snapshot(std::move(first_data));
  const BoardSnapshot second_board = Snapshot(std::move(second_data));
  const CompiledBoard first_compiled =
      Compile(first_board, test_support::DefaultCompilerProfile({0}));
  const CompiledBoard second_compiled =
      Compile(second_board, test_support::DefaultCompilerProfile({0}));
  const CandidateCase first = ThreeCandidates(first_board, first_compiled)[0];
  const CandidateCase second = ThreeCandidates(second_board, second_compiled)[0];
  CandidateStore store(StoreConfig());
  const CandidateAdmissionContext first_context{
      .board = first_board, .compiled_board = first_compiled, .request = first.request};
  const CandidateAdmissionContext second_context{
      .board = second_board, .compiled_board = second_compiled, .request = second.request};
  ASSERT_TRUE(std::holds_alternative<StoredCandidate>(store.Admit(first_context, first.generated)));
  const CandidateStoreAdmissionResult drift = store.Admit(second_context, second.generated);
  ASSERT_TRUE(std::holds_alternative<CandidateRejection>(drift));
  EXPECT_EQ(std::get<CandidateRejection>(drift).code, CandidateRejectionCode::kAssociationMismatch);
  EXPECT_EQ(std::get<CandidateRejection>(drift).invariant_id,
            "candidate.store.association_drift.v1");
  ASSERT_EQ(store.Enumerate(first.request.net).size(), 1U);
  EXPECT_EQ(store.Enumerate(first.request.net).front()->data().associations,
            AssociationsFor(first_board, first_compiled));
}

}  // namespace
}  // namespace apgar::candidates
