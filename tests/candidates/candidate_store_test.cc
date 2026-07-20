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
#include <type_traits>
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

  [[nodiscard]] static RouteCandidate ForceMetrics(const RouteCandidate& candidate,
                                                   CandidateMetrics metrics) {
    GeneratedRouteCandidate data = candidate.data_;
    data.metrics = metrics;
    return RouteCandidate(std::move(data));
  }

  [[nodiscard]] static CandidateStoreAdmissionResult Publish(CandidateStore& store,
                                                             RouteCandidate candidate) {
    std::scoped_lock lock(store.mutex_);
    return store.PublishAcceptedLocked(std::move(candidate));
  }

  [[nodiscard]] static std::vector<CandidateStoreAdmissionResult> PublishBatch(
      CandidateStore& store, std::vector<RouteCandidate> candidates) {
    std::scoped_lock lock(store.mutex_);
    return store.PublishAcceptedBatchLocked(std::move(candidates));
  }

  [[nodiscard]] static RouteCandidate ForceNetAndId(const RouteCandidate& candidate,
                                                    board_ir::EntityRef net, CandidateId id) {
    GeneratedRouteCandidate data = candidate.data_;
    data.net = net;
    data.id = id;
    return RouteCandidate(std::move(data));
  }

  static void ForcePool(CandidateStore& store, std::vector<RouteCandidate> candidates) {
    std::scoped_lock lock(store.mutex_);
    store.pools_.clear();
    store.candidate_id_index_.clear();
    for (RouteCandidate& candidate : candidates) {
      StoredCandidate stored = std::make_shared<const RouteCandidate>(std::move(candidate));
      store.pools_[stored->net()].push_back(stored);
      store.candidate_id_index_.emplace(stored->id(), stored);
    }
    for (auto& [net, pool] : store.pools_) {
      static_cast<void>(net);
      std::ranges::sort(pool, [](const StoredCandidate& left, const StoredCandidate& right) {
        return CandidateRanksBefore(*left, *right);
      });
    }
  }

  static void RetainRejection(CandidateStore& store, CandidateRejection rejection) {
    std::scoped_lock lock(store.mutex_);
    store.RetainRejectionLocked(std::move(rejection));
  }

  [[nodiscard]] static std::uint64_t LastPublicationCandidateInspections(
      const CandidateStore& store) {
    std::scoped_lock lock(store.mutex_);
    return store.last_publication_candidate_inspections_;
  }

  [[nodiscard]] static std::uint64_t LastDuplicateEqualityChecks(const CandidateStore& store) {
    std::scoped_lock lock(store.mutex_);
    return store.last_duplicate_equality_checks_;
  }

  [[nodiscard]] static std::uint64_t RejectionBatchMerges(const CandidateStore& store) {
    std::scoped_lock lock(store.mutex_);
    return store.rejection_batch_merges_;
  }

  [[nodiscard]] static std::uint64_t SharedRequestPolicyNormalizations(
      const CandidateStore& store) {
    std::scoped_lock lock(store.mutex_);
    return store.shared_request_policy_normalizations_;
  }
};

namespace {

static_assert(std::is_same_v<decltype(&CandidateStore::RetainRejection),
                             void (CandidateStore::*)(const CandidateRejection&)>);
static_assert(std::is_same_v<decltype(&CandidateStore::Admit),
                             CandidateStoreAdmissionResult (CandidateStore::*)(
                                 const CandidateAdmissionContext&, GeneratedRouteCandidate&&)>);
static_assert(CandidateStoreConfig{}.maximum_rejection_items_per_transaction ==
              kDefaultMaximumRejectionItemsPerTransaction);
static_assert(CandidateStoreConfig{}.maximum_admission_items_per_transaction ==
              kDefaultMaximumAdmissionItemsPerTransaction);
static_assert(CandidateStoreConfig{}.maximum_admission_input_bytes_per_transaction ==
              kDefaultMaximumAdmissionInputBytesPerTransaction);
static_assert(CandidateStoreConfig{}.maximum_admission_work_units_per_transaction ==
              kDefaultMaximumAdmissionWorkUnitsPerTransaction);

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

[[nodiscard]] GeneratedRouteCandidate CandidateCopy(const GeneratedRouteCandidate& candidate) {
  return candidate;
}

[[nodiscard]] CandidateCase MakeCase(const BoardSnapshot& board, const CompiledBoard& compiled,
                                     std::uint32_t ordinal, std::uint64_t query_identity,
                                     std::span<const LayerSegment> segments,
                                     std::uint64_t reported_cost) {
  CpuRouteRequest request = TwoTerminalRequest(board, 0, 0);
  request.candidate_policy.deterministic_seed = 0x1234U;
  request.candidate_policy.candidate_ordinal = ordinal;
  const routing::NormalizedCandidateGenerationPolicy policy = NormalizePolicy(compiled, request);
  const CandidateAssociations associations = AssociationsFor(board, compiled);
  routing::CpuRoute route{
      .source_board_content_hash = associations.board_content_hash,
      .compiler_profile_fingerprint = associations.compiler_profile_fingerprint,
      .compiler_version = associations.geometry_compiler_version,
      .rule_bucket_identity = associations.rule_bucket_identity,
      .candidate_policy_identity = policy.identity,
      .total_cost = reported_cost,
      .lattice_path = {},
      .segments = std::vector<LayerSegment>(segments.begin(), segments.end()),
      .telemetry = {},
      .producer_evidence = {},
  };
  test_support::CpuRouteFaultDecorator::Reseal(route);
  CandidateDraftBuildResult result = BuildGeneratedCandidateFromCpuRoute(
      board, compiled, request, policy, route,
      CandidateSchedulingIdentity{.batch_identity = 99, .query_identity = query_identity});
  EXPECT_TRUE(std::holds_alternative<GeneratedRouteCandidate>(result))
      << (std::holds_alternative<CandidateRejection>(result)
              ? std::get<CandidateRejection>(result).detail
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

[[nodiscard]] GeneratedRouteCandidate RescheduleCpuCandidate(const BoardSnapshot& board,
                                                             const CompiledBoard& compiled,
                                                             const CandidateCase& candidate_case,
                                                             std::uint64_t query_identity) {
  const routing::NormalizedCandidateGenerationPolicy policy =
      NormalizePolicy(compiled, candidate_case.request);
  std::vector<LayerSegment> segments;
  segments.reserve(candidate_case.generated.geometry.size());
  for (const CandidatePrimitive& primitive : candidate_case.generated.geometry) {
    const auto* line = std::get_if<ExactLinePrimitive>(&primitive);
    EXPECT_NE(line, nullptr);
    if (line == nullptr) {
      std::abort();
    }
    segments.push_back(LayerSegment{.layer = line->layer, .centerline = line->centerline});
  }
  const CandidateAssociations associations = AssociationsFor(board, compiled);
  routing::CpuRoute route{
      .source_board_content_hash = associations.board_content_hash,
      .compiler_profile_fingerprint = associations.compiler_profile_fingerprint,
      .compiler_version = associations.geometry_compiler_version,
      .rule_bucket_identity = associations.rule_bucket_identity,
      .candidate_policy_identity = policy.identity,
      .total_cost = candidate_case.generated.metrics.scalar_policy_cost,
      .lattice_path = {},
      .segments = std::move(segments),
      .telemetry = {},
      .producer_evidence = {},
  };
  test_support::CpuRouteFaultDecorator::Reseal(route);
  CandidateDraftBuildResult result = BuildGeneratedCandidateFromCpuRoute(
      board, compiled, candidate_case.request, policy, route,
      CandidateSchedulingIdentity{.batch_identity = 99, .query_identity = query_identity});
  EXPECT_TRUE(std::holds_alternative<GeneratedRouteCandidate>(result));
  if (!std::holds_alternative<GeneratedRouteCandidate>(result)) {
    std::abort();
  }
  return std::get<GeneratedRouteCandidate>(std::move(result));
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

TEST(CandidateStoreTest, BatchPermutationsAreStableAndCommutativeSinglesAreRaceSafe) {
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
      (void)store.Admit(context, CandidateCopy(cases[index].generated));
    }
    EXPECT_EQ(Ids(store.Enumerate(net)), expected);
  } while (std::ranges::next_permutation(order).found);

  // These three distinct ranked alternatives happen to commute under this
  // retention budget. This section is a race-safety/linearizability check, not
  // a promise that separate bounded transactions commute in general; CAN-004
  // requires one AdmitBatch for a schedule-independent invocation.
  CandidateStore concurrent(StoreConfig(2));
  std::vector<std::thread> threads;
  for (const CandidateCase& candidate_case : cases) {
    CandidateCase candidate_copy = candidate_case;
    threads.emplace_back(
        [&board, &compiled, &concurrent, candidate_case = std::move(candidate_copy)]() mutable {
          const CandidateAdmissionContext context{
              .board = board,
              .compiled_board = compiled,
              .request = candidate_case.request,
          };
          (void)concurrent.Admit(context, std::move(candidate_case.generated));
        });
  }
  for (std::thread& thread : threads) {
    thread.join();
  }
  EXPECT_EQ(Ids(concurrent.Enumerate(net)), expected);
}

TEST(CandidateStoreTest, SharedRequestPolicyIsNormalizedOncePerNonemptyTransaction) {
  BoardData data = test_support::ValidM1BoardData();
  data.obstacles.clear();
  const BoardSnapshot board = Snapshot(std::move(data));
  const CompiledBoard compiled = Compile(board, test_support::DefaultCompilerProfile({0}));
  const std::array<CandidateCase, 3> cases = ThreeCandidates(board, compiled);
  const CandidateCase& candidate = cases[0];
  const CandidateAdmissionContext context{
      .board = board, .compiled_board = compiled, .request = candidate.request};
  CandidateStore store(StoreConfig(2));

  std::vector<GeneratedRouteCandidate> generated;
  generated.push_back(CandidateCopy(candidate.generated));
  generated.push_back(CandidateCopy(candidate.generated));
  const std::vector<CandidateStoreAdmissionResult> results =
      store.AdmitBatch(context, std::move(generated));

  ASSERT_EQ(results.size(), 2U);
  EXPECT_EQ(CandidateStoreTestPeer::SharedRequestPolicyNormalizations(store), 1U);
  EXPECT_EQ(store.Enumerate(candidate.request.net).size(), 1U);
  EXPECT_TRUE(std::ranges::any_of(results, [](const CandidateStoreAdmissionResult& result) {
    return std::holds_alternative<CandidateRejection>(result) &&
           std::get<CandidateRejection>(result).code == CandidateRejectionCode::kDuplicateIdentity;
  }));

  CandidateStore mismatch_store(StoreConfig(2));
  const std::vector<CandidateStoreAdmissionResult> mismatch = mismatch_store.AdmitBatch(
      CandidateAdmissionContext{
          .board = board, .compiled_board = compiled, .request = cases[1].request},
      std::vector<GeneratedRouteCandidate>{CandidateCopy(candidate.generated)});
  ASSERT_EQ(mismatch.size(), 1U);
  ASSERT_TRUE(std::holds_alternative<CandidateRejection>(mismatch.front()));
  EXPECT_EQ(std::get<CandidateRejection>(mismatch.front()).invariant_id,
            "candidate.policy.request_association.v1");
  EXPECT_EQ(CandidateStoreTestPeer::SharedRequestPolicyNormalizations(mismatch_store), 1U);
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
    worse = RescheduleCpuCandidate(board, compiled, cases[1], query);
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

TEST(CandidateStoreTest, RejectionHeavyAdmissionSortsAndMergesHistoryExactlyOnce) {
  BoardData data = test_support::ValidM1BoardData();
  data.obstacles.clear();
  const BoardSnapshot board = Snapshot(std::move(data));
  const CompiledBoard compiled = Compile(board, test_support::DefaultCompilerProfile({0}));
  const CandidateCase candidate = ThreeCandidates(board, compiled)[0];
  const CandidateAdmissionContext context{
      .board = board, .compiled_board = compiled, .request = candidate.request};

  struct RunSnapshot {
    std::vector<std::variant<CandidateId, CandidateRejection>> results;
    std::vector<CandidateRejection> history;
    std::vector<CandidateId> retained;
    std::uint64_t merge_count = 0;
  };
  const auto run = [&](bool reverse) {
    CandidateStore store(StoreConfig(1));
    std::vector<CandidateRejection> seed_history(32);
    for (std::size_t index = 0; index < seed_history.size(); ++index) {
      seed_history[index].candidate_id = CandidateId{.high = 0x51eedU, .low = index + 1U};
      seed_history[index].invariant_id = "candidate.test.seed." + std::to_string(index) + ".v1";
      seed_history[index].detail = "preexisting deterministic rejection history";
    }
    EXPECT_FALSE(store.RetainRejections(seed_history).has_value());
    const std::uint64_t merges_before = CandidateStoreTestPeer::RejectionBatchMerges(store);

    std::vector<GeneratedRouteCandidate> generated;
    generated.reserve(64);
    for (std::size_t index = 0; index < 64; ++index) {
      GeneratedRouteCandidate item = CandidateCopy(candidate.generated);
      if ((index & 1U) != 0U) {
        ++item.metrics.scalar_policy_cost;
      }
      generated.push_back(std::move(item));
    }
    if (reverse) {
      std::ranges::reverse(generated);
    }
    const std::vector<CandidateStoreAdmissionResult> admission =
        store.AdmitBatch(context, std::move(generated));
    EXPECT_EQ(admission.size(), 64U);

    RunSnapshot snapshot;
    snapshot.results.reserve(admission.size());
    for (const CandidateStoreAdmissionResult& result : admission) {
      if (const auto* stored = std::get_if<StoredCandidate>(&result); stored != nullptr) {
        snapshot.results.emplace_back((*stored)->id());
      } else {
        snapshot.results.emplace_back(std::get<CandidateRejection>(result));
      }
    }
    snapshot.history = store.Rejections();
    snapshot.retained = Ids(store.Enumerate(candidate.request.net));
    snapshot.merge_count = CandidateStoreTestPeer::RejectionBatchMerges(store) - merges_before;
    return snapshot;
  };

  const RunSnapshot forward = run(false);
  const RunSnapshot reverse = run(true);
  EXPECT_EQ(forward.merge_count, 1U);
  EXPECT_EQ(reverse.merge_count, 1U);
  EXPECT_EQ(forward.results, reverse.results);
  EXPECT_EQ(forward.history, reverse.history);
  EXPECT_EQ(forward.retained, reverse.retained);
  ASSERT_EQ(forward.retained.size(), 1U);
  ASSERT_EQ(forward.history.size(), 95U);
}

TEST(CandidateStoreTest, ExactAdmissionBindsSessionBeforeBudgetRetention) {
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
  const CandidateCase larger = ThreeCandidates(first_board, first_compiled)[1];
  const CandidateCase smaller = ThreeCandidates(second_board, second_compiled)[0];
  ASSERT_GT(larger.generated.logical_bytes, smaller.generated.logical_bytes);

  const CandidateStoreConfig config = StoreConfig(1, smaller.generated.logical_bytes);
  CandidateStore control(config);
  const CandidateStoreAdmissionResult control_result = control.Admit(
      CandidateAdmissionContext{
          .board = second_board, .compiled_board = second_compiled, .request = smaller.request},
      CandidateCopy(smaller.generated));
  ASSERT_TRUE(std::holds_alternative<StoredCandidate>(control_result));

  CandidateStore store(config);
  const CandidateStoreAdmissionResult budget = store.Admit(
      CandidateAdmissionContext{
          .board = first_board, .compiled_board = first_compiled, .request = larger.request},
      CandidateCopy(larger.generated));
  ASSERT_TRUE(std::holds_alternative<CandidateRejection>(budget));
  EXPECT_EQ(std::get<CandidateRejection>(budget).code, CandidateRejectionCode::kBudgetExhausted);
  EXPECT_TRUE(store.Enumerate(larger.request.net).empty());

  const CandidateStoreAdmissionResult drift = store.Admit(
      CandidateAdmissionContext{
          .board = second_board, .compiled_board = second_compiled, .request = smaller.request},
      CandidateCopy(smaller.generated));
  ASSERT_TRUE(std::holds_alternative<CandidateRejection>(drift));
  EXPECT_EQ(std::get<CandidateRejection>(drift).code, CandidateRejectionCode::kAssociationMismatch);
  EXPECT_EQ(std::get<CandidateRejection>(drift).invariant_id,
            "candidate.store.association_drift.v1");
  EXPECT_TRUE(store.Enumerate(smaller.request.net).empty());
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
      candidate = RescheduleCpuCandidate(board, compiled, cases[index], query_identity);
      if ((candidate.id.high >> 62U) == index) {
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

  CandidateStore immutable_identity_store(StoreConfig(2));
  const CandidateStoreAdmissionResult immutable_initial =
      immutable_identity_store.Admit(duplicate_context, CandidateCopy(duplicate.generated));
  ASSERT_TRUE(std::holds_alternative<StoredCandidate>(immutable_initial));
  const CandidateStoreAdmissionResult forged_better_payload =
      immutable_identity_store.Admit(preferred_context, CandidateCopy(preferred.generated));
  ASSERT_TRUE(std::holds_alternative<CandidateRejection>(forged_better_payload));
  EXPECT_EQ(std::get<CandidateRejection>(forged_better_payload).code,
            CandidateRejectionCode::kDuplicateIdentity);
  const std::vector<StoredCandidate> immutable_retained =
      immutable_identity_store.Enumerate(duplicate.request.net);
  ASSERT_EQ(immutable_retained.size(), 1U);
  EXPECT_EQ(immutable_retained.front()->data().payload_checksum,
            duplicate.generated.payload_checksum);

  CandidateStore pinned_store(StoreConfig(2));
  const CandidateStoreAdmissionResult initial =
      pinned_store.Admit(duplicate_context, CandidateCopy(duplicate.generated));
  ASSERT_TRUE(std::holds_alternative<StoredCandidate>(initial));
  ASSERT_FALSE(pinned_store.Pin(17, duplicate.generated.id).has_value());
  const CandidateStoreAdmissionResult pinned_duplicate =
      pinned_store.Admit(preferred_context, CandidateCopy(preferred.generated));
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
  const CandidateStoreAdmissionResult first =
      store.Admit(worse_context, CandidateCopy(cases[1].generated));
  ASSERT_TRUE(std::holds_alternative<StoredCandidate>(first));
  const CandidateId pinned_id = std::get<StoredCandidate>(first)->id();
  EXPECT_FALSE(store.Pin(7, pinned_id).has_value());
  EXPECT_FALSE(store.Pin(7, pinned_id).has_value());

  const CandidateStoreAdmissionResult blocked =
      store.Admit(better_context, CandidateCopy(cases[0].generated));
  ASSERT_TRUE(std::holds_alternative<CandidateRejection>(blocked));
  EXPECT_EQ(std::get<CandidateRejection>(blocked).code, CandidateRejectionCode::kBudgetExhausted);
  ASSERT_EQ(store.Enumerate(cases[0].request.net).size(), 1U);
  EXPECT_EQ(store.Enumerate(cases[0].request.net).front()->id(), pinned_id);

  EXPECT_FALSE(store.Unpin(7, pinned_id).has_value());
  const CandidateStoreAdmissionResult admitted =
      store.Admit(better_context, CandidateCopy(cases[0].generated));
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
  ASSERT_TRUE(std::holds_alternative<StoredCandidate>(
      store.Admit(context, CandidateCopy(cases[0].generated))));
  const CandidateStoreAdmissionResult duplicate_id =
      store.Admit(context, CandidateCopy(cases[0].generated));
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
  (void)store.Admit(same_context, CandidateCopy(same_geometry.generated));
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
  EXPECT_EQ(CandidateStoreTestPeer::LastDuplicateEqualityChecks(store), 2U);
}

TEST(CandidateStoreTest, DistinctSignaturesSkipCanonicalDuplicateEquality) {
  BoardData data = test_support::ValidM1BoardData();
  data.obstacles.clear();
  const BoardSnapshot board = Snapshot(std::move(data));
  const CompiledBoard compiled = Compile(board, test_support::DefaultCompilerProfile({0}));
  const std::array<CandidateCase, 3> cases = ThreeCandidates(board, compiled);
  std::vector<RouteCandidate> candidates;
  for (const CandidateCase& candidate : cases) {
    candidates.push_back(test_support::AcceptedCandidate(
        CandidateAdmissionContext{
            .board = board, .compiled_board = compiled, .request = candidate.request},
        candidate.generated));
  }
  ASSERT_NE(candidates[0].data().geometry_signature, candidates[1].data().geometry_signature);
  ASSERT_NE(candidates[0].data().geometry_signature, candidates[2].data().geometry_signature);
  ASSERT_NE(candidates[1].data().geometry_signature, candidates[2].data().geometry_signature);
  ASSERT_NE(candidates[0].data().resource_signature, candidates[1].data().resource_signature);
  ASSERT_NE(candidates[0].data().resource_signature, candidates[2].data().resource_signature);
  ASSERT_NE(candidates[1].data().resource_signature, candidates[2].data().resource_signature);

  CandidateStore store(StoreConfig(3));
  const std::vector<CandidateStoreAdmissionResult> results =
      CandidateStoreTestPeer::PublishBatch(store, std::move(candidates));

  ASSERT_EQ(results.size(), 3U);
  EXPECT_EQ(CandidateStoreTestPeer::LastDuplicateEqualityChecks(store), 0U);
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
  ASSERT_TRUE(std::holds_alternative<StoredCandidate>(
      byte_limited.Admit(top_context, CandidateCopy(cases[1].generated))));
  ASSERT_TRUE(std::holds_alternative<StoredCandidate>(
      byte_limited.Admit(straight_context, CandidateCopy(cases[0].generated))));
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

TEST(CandidateStoreTest, StableRankComparesTotalStepCountWithoutUint64Wraparound) {
  BoardData data = test_support::ValidM1BoardData();
  data.obstacles.clear();
  const BoardSnapshot board = Snapshot(std::move(data));
  const CompiledBoard compiled = Compile(board, test_support::DefaultCompilerProfile({0}));
  const std::array<CandidateCase, 3> cases = ThreeCandidates(board, compiled);
  const CandidateAdmissionContext first_context{
      .board = board, .compiled_board = compiled, .request = cases[0].request};
  const CandidateAdmissionContext second_context{
      .board = board, .compiled_board = compiled, .request = cases[1].request};
  const RouteCandidate first = test_support::AcceptedCandidate(first_context, cases[0].generated);
  const RouteCandidate second = test_support::AcceptedCandidate(second_context, cases[1].generated);
  CandidateMetrics larger_total = first.data().metrics;
  CandidateMetrics smaller_total = larger_total;
  larger_total.orthogonal_step_count = std::numeric_limits<std::uint64_t>::max();
  larger_total.diagonal_step_count = 1;
  smaller_total.orthogonal_step_count = std::numeric_limits<std::uint64_t>::max();
  smaller_total.diagonal_step_count = 0;
  const RouteCandidate larger = CandidateStoreTestPeer::ForceMetrics(first, larger_total);
  const RouteCandidate smaller = CandidateStoreTestPeer::ForceMetrics(second, smaller_total);

  EXPECT_TRUE(CandidateRanksBefore(smaller, larger));
  EXPECT_FALSE(CandidateRanksBefore(larger, smaller));
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

TEST(CandidateStoreTest, AdmissionTransactionPreflightRejectsBeforeExactWorkAndRetainsEvidence) {
  BoardData data = test_support::ValidM1BoardData();
  data.obstacles.clear();
  const BoardSnapshot board = Snapshot(std::move(data));
  const CompiledBoard compiled = Compile(board, test_support::DefaultCompilerProfile({0}));
  const std::array<CandidateCase, 3> cases = ThreeCandidates(board, compiled);

  CandidateStoreConfig count_config = StoreConfig();
  count_config.maximum_admission_items_per_transaction = 1;
  CandidateStore count_limited(count_config);
  GeneratedRouteCandidate corrupt = cases[1].generated;
  corrupt.geometry.clear();
  std::vector<CandidateAdmissionItem> too_many = {
      CandidateAdmissionItem{.request = cases[0].request, .generated = cases[0].generated},
      CandidateAdmissionItem{.request = cases[1].request, .generated = std::move(corrupt)},
  };
  const std::vector<CandidateStoreAdmissionResult> count_results =
      count_limited.AdmitBatch(board, compiled, std::move(too_many));
  ASSERT_EQ(count_results.size(), 1U);
  const CandidateRejection& count_rejection = std::get<CandidateRejection>(count_results.front());
  EXPECT_EQ(count_rejection.stage, CandidateLifecycleStage::kGenerated);
  EXPECT_EQ(count_rejection.code, CandidateRejectionCode::kBudgetExhausted);
  EXPECT_EQ(count_rejection.invariant_id, "candidate.store.transaction.item_budget.v1");
  EXPECT_EQ(count_rejection.expected_value, 1U);
  EXPECT_EQ(count_rejection.actual_value, 2U);
  EXPECT_FALSE(count_rejection.candidate_id.has_value());
  EXPECT_TRUE(count_limited.Enumerate(cases[0].request.net).empty());
  ASSERT_EQ(count_limited.Rejections().size(), 1U);
  EXPECT_EQ(count_limited.Rejections().front(), count_rejection);

  CandidateStoreConfig bytes_config = StoreConfig();
  bytes_config.maximum_admission_input_bytes_per_transaction = cases[0].generated.logical_bytes - 1;
  CandidateStore bytes_limited(bytes_config);
  const CandidateAdmissionContext context{
      .board = board, .compiled_board = compiled, .request = cases[0].request};
  const CandidateStoreAdmissionResult bytes_result =
      bytes_limited.Admit(context, CandidateCopy(cases[0].generated));
  ASSERT_TRUE(std::holds_alternative<CandidateRejection>(bytes_result));
  EXPECT_EQ(std::get<CandidateRejection>(bytes_result).invariant_id,
            "candidate.store.transaction.input_byte_budget.v1");
  EXPECT_TRUE(bytes_limited.Enumerate(cases[0].request.net).empty());

  CandidateStoreConfig work_config = StoreConfig();
  work_config.maximum_admission_work_units_per_transaction = 1;
  CandidateStore work_limited(work_config);
  const CandidateStoreAdmissionResult work_result =
      work_limited.Admit(context, CandidateCopy(cases[0].generated));
  ASSERT_TRUE(std::holds_alternative<CandidateRejection>(work_result));
  EXPECT_EQ(std::get<CandidateRejection>(work_result).invariant_id,
            "candidate.store.transaction.work_budget.v1");

  CandidateStoreConfig derived_work_config = StoreConfig();
  derived_work_config.maximum_admission_work_units_per_transaction = 4;
  CandidateStore derived_work_limited(derived_work_config);
  GeneratedRouteCandidate understated_footprint = cases[0].generated;
  ASSERT_EQ(understated_footprint.resources.size(), 1U);
  understated_footprint.resources.front().edge_count = 1;
  const CandidateStoreAdmissionResult derived_work_result =
      derived_work_limited.Admit(context, std::move(understated_footprint));
  ASSERT_TRUE(std::holds_alternative<CandidateRejection>(derived_work_result));
  EXPECT_EQ(std::get<CandidateRejection>(derived_work_result).stage,
            CandidateLifecycleStage::kGenerated);
  EXPECT_EQ(std::get<CandidateRejection>(derived_work_result).invariant_id,
            "candidate.store.transaction.work_budget.v1");

  CandidateStoreConfig request_bytes_config = StoreConfig();
  request_bytes_config.maximum_admission_input_bytes_per_transaction =
      cases[0].generated.logical_bytes + 1;
  CandidateStore request_bytes_limited(request_bytes_config);
  CpuRouteRequest request_with_large_policy = cases[0].request;
  request_with_large_policy.candidate_policy.banned_resources.push_back(routing::EdgeResourceKey{});
  std::vector<CandidateAdmissionItem> mismatched_request_items = {
      CandidateAdmissionItem{.request = std::move(request_with_large_policy),
                             .generated = cases[0].generated},
  };
  const std::vector<CandidateStoreAdmissionResult> request_bytes_results =
      request_bytes_limited.AdmitBatch(board, compiled, std::move(mismatched_request_items));
  ASSERT_EQ(request_bytes_results.size(), 1U);
  const CandidateRejection& request_bytes_rejection =
      std::get<CandidateRejection>(request_bytes_results.front());
  EXPECT_EQ(request_bytes_rejection.stage, CandidateLifecycleStage::kGenerated);
  EXPECT_EQ(request_bytes_rejection.invariant_id,
            "candidate.store.transaction.input_byte_budget.v1");
  ASSERT_TRUE(request_bytes_rejection.actual_value.has_value());
  EXPECT_GT(*request_bytes_rejection.actual_value,
            request_bytes_config.maximum_admission_input_bytes_per_transaction);

  CandidateStoreConfig shared_request_config = StoreConfig();
  shared_request_config.maximum_admission_work_units_per_transaction = 200;
  CandidateStore shared_request_limited(shared_request_config);
  CpuRouteRequest shared_request = cases[0].request;
  shared_request.candidate_policy.resource_penalties.resize(128);
  const CandidateAdmissionContext shared_context{
      .board = board, .compiled_board = compiled, .request = shared_request};
  const std::vector<CandidateStoreAdmissionResult> shared_request_results =
      shared_request_limited.AdmitBatch(
          shared_context,
          std::vector<GeneratedRouteCandidate>{cases[0].generated, cases[0].generated});
  ASSERT_EQ(shared_request_results.size(), 1U);
  const CandidateRejection& shared_request_rejection =
      std::get<CandidateRejection>(shared_request_results.front());
  EXPECT_EQ(shared_request_rejection.stage, CandidateLifecycleStage::kGenerated);
  EXPECT_EQ(shared_request_rejection.invariant_id, "candidate.store.transaction.work_budget.v1");
  EXPECT_EQ(shared_request_rejection.actual_value, 256U);
  EXPECT_TRUE(shared_request_limited.Enumerate(cases[0].request.net).empty());

  CandidateStoreConfig invalid_config = StoreConfig();
  invalid_config.maximum_admission_items_per_transaction = 0;
  CandidateStore invalid(invalid_config);
  const CandidateStoreAdmissionResult invalid_result =
      invalid.Admit(context, CandidateCopy(cases[0].generated));
  ASSERT_TRUE(std::holds_alternative<CandidateRejection>(invalid_result));
  EXPECT_EQ(std::get<CandidateRejection>(invalid_result).code,
            CandidateRejectionCode::kInvalidInput);
  EXPECT_EQ(std::get<CandidateRejection>(invalid_result).invariant_id,
            "candidate.store.configuration.v1");
  ASSERT_EQ(invalid.Rejections().size(), 1U);
}

TEST(CandidateStoreTest, AdmissionTransactionPreflightsPolicyShapeBeforeBulkAccountingOrCopies) {
  BoardData data = test_support::ValidM1BoardData();
  data.obstacles.clear();
  const BoardSnapshot board = Snapshot(std::move(data));
  const CompiledBoard compiled = Compile(board, test_support::DefaultCompilerProfile({0}));
  const std::array<CandidateCase, 3> cases = ThreeCandidates(board, compiled);

  GeneratedRouteCandidate oversized_generated = cases[0].generated;
  oversized_generated.policy.banned_resources.resize(routing::kMaximumPolicyResourceEntries + 1U);
  CandidateStore generated_store(StoreConfig());
  const CandidateAdmissionContext valid_context{
      .board = board, .compiled_board = compiled, .request = cases[0].request};
  const CandidateStoreAdmissionResult generated_result =
      generated_store.Admit(valid_context, std::move(oversized_generated));
  ASSERT_TRUE(std::holds_alternative<CandidateRejection>(generated_result));
  const CandidateRejection& generated_rejection = std::get<CandidateRejection>(generated_result);
  EXPECT_EQ(generated_rejection.invariant_id,
            "candidate.store.transaction.policy_resource_entry_count.v1");
  EXPECT_EQ(generated_rejection.expected_value, routing::kMaximumPolicyResourceEntries);
  EXPECT_EQ(generated_rejection.actual_value, routing::kMaximumPolicyResourceEntries + 1U);
  EXPECT_FALSE(generated_rejection.candidate_payload_checksum.has_value());

  CpuRouteRequest oversized_request = cases[0].request;
  oversized_request.candidate_policy.resource_penalties.resize(
      routing::kMaximumPolicyResourceEntries + 1U);
  CandidateStore request_store(StoreConfig());
  const CandidateAdmissionContext oversized_context{
      .board = board, .compiled_board = compiled, .request = oversized_request};
  const CandidateStoreAdmissionResult request_result =
      request_store.Admit(oversized_context, CandidateCopy(cases[0].generated));
  ASSERT_TRUE(std::holds_alternative<CandidateRejection>(request_result));
  const CandidateRejection& request_rejection = std::get<CandidateRejection>(request_result);
  EXPECT_EQ(request_rejection.invariant_id,
            "candidate.store.transaction.policy_resource_entry_count.v1");
  EXPECT_EQ(request_rejection.expected_value, routing::kMaximumPolicyResourceEntries);
  EXPECT_EQ(request_rejection.actual_value, routing::kMaximumPolicyResourceEntries + 1U);
  EXPECT_FALSE(request_rejection.candidate_payload_checksum.has_value());
}

TEST(CandidateStoreTest, AdmissionTransactionPreflightsEveryCandidateBulkShape) {
  BoardData data = test_support::ValidM1BoardData();
  data.obstacles.clear();
  const BoardSnapshot board = Snapshot(std::move(data));
  const CompiledBoard compiled = Compile(board, test_support::DefaultCompilerProfile({0}));
  const std::array<CandidateCase, 3> cases = ThreeCandidates(board, compiled);
  const CandidateAdmissionContext context{
      .board = board, .compiled_board = compiled, .request = cases[0].request};
  CandidateStoreConfig config = StoreConfig();
  config.maximum_admission_input_bytes_per_transaction = std::numeric_limits<std::uint64_t>::max();
  config.maximum_admission_work_units_per_transaction = std::numeric_limits<std::uint64_t>::max();

  const auto reject = [&](GeneratedRouteCandidate&& generated) {
    CandidateStore store(config);
    const CandidateStoreAdmissionResult result = store.Admit(context, std::move(generated));
    EXPECT_TRUE(std::holds_alternative<CandidateRejection>(result));
    return std::get<CandidateRejection>(result);
  };

  GeneratedRouteCandidate geometry = CandidateCopy(cases[0].generated);
  geometry.geometry.resize(kMaximumCandidatePrimitives + 1U);
  const CandidateRejection geometry_rejection = reject(std::move(geometry));
  EXPECT_EQ(geometry_rejection.invariant_id,
            "candidate.store.transaction.geometry_primitive_count.v1");
  EXPECT_EQ(geometry_rejection.actual_value, kMaximumCandidatePrimitives + 1U);
  EXPECT_FALSE(geometry_rejection.candidate_payload_checksum.has_value());

  GeneratedRouteCandidate resources = CandidateCopy(cases[0].generated);
  resources.resources.resize(kMaximumCandidateResourceSpans + 1U);
  const CandidateRejection resources_rejection = reject(std::move(resources));
  EXPECT_EQ(resources_rejection.invariant_id, "candidate.store.transaction.resource_span_count.v1");
  EXPECT_EQ(resources_rejection.actual_value, kMaximumCandidateResourceSpans + 1U);
  EXPECT_FALSE(resources_rejection.candidate_payload_checksum.has_value());

  GeneratedRouteCandidate device_class = CandidateCopy(cases[0].generated);
  device_class.provenance.supported_device_class.assign(kMaximumCandidateDiagnosticBytes + 1U, 'd');
  const CandidateRejection device_rejection = reject(std::move(device_class));
  EXPECT_EQ(device_rejection.invariant_id, "candidate.store.transaction.device_class_bytes.v1");
  EXPECT_EQ(device_rejection.actual_value, kMaximumCandidateDiagnosticBytes + 1U);
  EXPECT_FALSE(device_rejection.candidate_payload_checksum.has_value());

  CpuRouteRequest large_shared_request = cases[0].request;
  large_shared_request.candidate_policy.resource_penalties.resize(128);
  CandidateStoreConfig shared_config = config;
  shared_config.maximum_admission_work_units_per_transaction = 1;
  CandidateStore shared_store(shared_config);
  GeneratedRouteCandidate shared_geometry = CandidateCopy(cases[0].generated);
  shared_geometry.geometry.resize(kMaximumCandidatePrimitives + 1U);
  std::vector<GeneratedRouteCandidate> shared_generated;
  shared_generated.push_back(std::move(shared_geometry));
  const std::vector<CandidateStoreAdmissionResult> shared_results = shared_store.AdmitBatch(
      CandidateAdmissionContext{
          .board = board, .compiled_board = compiled, .request = large_shared_request},
      std::move(shared_generated));
  ASSERT_EQ(shared_results.size(), 1U);
  ASSERT_TRUE(std::holds_alternative<CandidateRejection>(shared_results.front()));
  EXPECT_EQ(std::get<CandidateRejection>(shared_results.front()).invariant_id,
            "candidate.store.transaction.geometry_primitive_count.v1");
}

TEST(CandidateStoreTest, AdmissionTransactionWorkBudgetCountsAllAuthoritativeTerminals) {
  BoardData data = test_support::ValidM1BoardData();
  data.obstacles.clear();
  ASSERT_EQ(data.nets.size(), 2U);
  constexpr std::size_t kAdditionalTerminals = 64;
  for (std::size_t index = 0; index < kAdditionalTerminals; ++index) {
    const board_ir::EntityRef terminal_ref{.id = 1'000U + index, .generation = 0};
    data.nets[1].terminals.push_back(terminal_ref);
    const std::int64_t x = 1'000 + static_cast<std::int64_t>(index) * 20;
    data.terminals.push_back(board_ir::Terminal{
        .ref = terminal_ref,
        .net = data.nets[1].ref,
        .component = "UX",
        .pin = std::to_string(index + 1U),
        .center = {.x = x, .y = 1'000},
        .connection_region = {.min = {.x = x - 1, .y = 999}, .max = {.x = x + 1, .y = 1'001}},
        .layers = {0},
    });
  }
  const BoardSnapshot board = Snapshot(std::move(data));
  const CompiledBoard compiled = Compile(board, test_support::DefaultCompilerProfile({0}));
  const CpuRouteRequest request = TwoTerminalRequest(board, 0, 0);
  const GeneratedRouteCandidate generated = test_support::CandidateDraft(board, compiled, request);
  CandidateStoreConfig config = StoreConfig();
  // The same one-segment draft requires less than this without the P*T term,
  // but exact geometry inspects every Board IR terminal for that primitive.
  config.maximum_admission_work_units_per_transaction = 50;
  CandidateStore store(config);

  const CandidateStoreAdmissionResult result = store.Admit(
      CandidateAdmissionContext{.board = board, .compiled_board = compiled, .request = request},
      CandidateCopy(generated));

  ASSERT_TRUE(std::holds_alternative<CandidateRejection>(result));
  const CandidateRejection& rejection = std::get<CandidateRejection>(result);
  EXPECT_EQ(rejection.stage, CandidateLifecycleStage::kGenerated);
  EXPECT_EQ(rejection.code, CandidateRejectionCode::kBudgetExhausted);
  EXPECT_EQ(rejection.invariant_id, "candidate.store.transaction.work_budget.v1");
  EXPECT_TRUE(store.Enumerate(request.net).empty());
}

TEST(CandidateStoreTest, PublicRejectionRetentionIsCanonicalBoundedAndBatchOrderIndependent) {
  CandidateRejection first;
  first.candidate_id = CandidateId{.high = 4, .low = 5};
  first.invariant_id = "candidate.test.z.v1";
  first.detail = "z";
  CandidateRejection second;
  second.candidate_id = CandidateId{.high = 1, .low = 2};
  second.invariant_id = "candidate.test.a.v1";
  second.detail = "a";

  const auto run = [&](bool reverse) {
    CandidateStore store(CandidateStoreConfig{
        .maximum_candidates_per_net = 1,
        .maximum_candidate_bytes_per_net = 1,
        .maximum_rejection_records = 1,
    });
    if (reverse) {
      const std::array records = {first, second};
      store.RetainRejections(records);
      EXPECT_EQ(CandidateStoreTestPeer::RejectionBatchMerges(store), 1U);
    } else {
      store.RetainRejection(second);
      store.RetainRejection(first);
      EXPECT_EQ(CandidateStoreTestPeer::RejectionBatchMerges(store), 0U);
    }
    return store.Rejections();
  };

  const std::vector<CandidateRejection> forward = run(false);
  const std::vector<CandidateRejection> reverse = run(true);
  ASSERT_EQ(forward.size(), 1U);
  EXPECT_EQ(forward, reverse);
  EXPECT_EQ(forward.front().candidate_id, second.candidate_id);
  EXPECT_NE(forward.front().logical_bytes, 0U);
}

TEST(CandidateStoreTest, RejectionRetentionUsesCanonicalLengthThenByteStringOrder) {
  const auto retain_best = [](CandidateRejection first, CandidateRejection second) {
    CandidateStore store(CandidateStoreConfig{
        .maximum_candidates_per_net = 1,
        .maximum_candidate_bytes_per_net = 1,
        .maximum_rejection_records = 1,
    });
    const std::array records = {std::move(first), std::move(second)};
    store.RetainRejections(records);
    const std::vector<CandidateRejection> retained = store.Rejections();
    EXPECT_EQ(retained.size(), 1U);
    return retained.front();
  };

  CandidateRejection invariant_short;
  invariant_short.invariant_id = "z";
  invariant_short.provenance.supported_device_class = "same";
  invariant_short.detail = "same";
  CandidateRejection invariant_long = invariant_short;
  invariant_long.invariant_id = "aa";
  EXPECT_EQ(retain_best(invariant_long, invariant_short).invariant_id, "z");

  CandidateRejection device_short;
  device_short.invariant_id = "same";
  device_short.provenance.supported_device_class = "z";
  device_short.detail = "same";
  CandidateRejection device_long = device_short;
  device_long.provenance.supported_device_class = "aa";
  EXPECT_EQ(retain_best(device_long, device_short).provenance.supported_device_class, "z");

  CandidateRejection detail_short;
  detail_short.invariant_id = "same";
  detail_short.provenance.supported_device_class = "same";
  detail_short.detail = "z";
  CandidateRejection detail_long = detail_short;
  detail_long.detail = "aa";
  EXPECT_EQ(retain_best(detail_long, detail_short).detail, "z");
}

TEST(CandidateStoreTest, OverCapRejectionBatchProducesOneDeterministicDiagnosticWithoutInspection) {
  const auto retain = [](bool reverse) {
    CandidateStore store(CandidateStoreConfig{
        .maximum_candidates_per_net = 1,
        .maximum_candidate_bytes_per_net = 1,
        .maximum_rejection_records = 8,
        .maximum_rejection_items_per_transaction = 2,
    });
    std::array<CandidateRejection, 3> records;
    for (std::size_t index = 0; index < records.size(); ++index) {
      records[index].candidate_id = CandidateId{.high = reverse ? 99U : 11U, .low = index + 1U};
      records[index].invariant_id =
          reverse ? "candidate.unread.reverse.v1" : "candidate.unread.forward.v1";
      records[index].detail.assign(index + 1U, reverse ? 'r' : 'f');
    }
    const std::optional<CandidateRejection> result = store.RetainRejections(records);
    EXPECT_TRUE(result.has_value());
    return std::pair{result, store.Rejections()};
  };

  const auto [forward_result, forward] = retain(false);
  const auto [reverse_result, reverse] = retain(true);
  ASSERT_TRUE(forward_result.has_value());
  EXPECT_EQ(forward_result, reverse_result);
  ASSERT_EQ(forward.size(), 1U);
  EXPECT_EQ(forward, reverse);
  const CandidateRejection& rejection = forward.front();
  EXPECT_EQ(*forward_result, rejection);
  EXPECT_FALSE(rejection.candidate_id.has_value());
  EXPECT_FALSE(rejection.net.has_value());
  EXPECT_EQ(rejection.stage, CandidateLifecycleStage::kGenerated);
  EXPECT_EQ(rejection.code, CandidateRejectionCode::kBudgetExhausted);
  EXPECT_EQ(rejection.invariant_id, "candidate.store.rejection_transaction.item_budget.v1");
  EXPECT_EQ(rejection.expected_value, 2U);
  EXPECT_EQ(rejection.actual_value, 3U);
  EXPECT_FALSE(rejection.candidate_payload_checksum.has_value());
}

TEST(CandidateStoreTest, HostilePublicRejectionIngestionRetainsOnlyBoundedV1Replacements) {
  CandidateRejection base;
  base.net = board_ir::EntityRef{.id = 77, .generation = 3};
  base.invariant_id = "candidate.test.external.v1";
  base.associations = CandidateAssociations{
      .board_content_hash = 11,
      .compiler_profile_fingerprint = 13,
      .geometry_compiler_version = 17,
      .routing_profile_fingerprint = 19,
      .rule_bucket_identity = 23,
  };
  base.provenance.supported_device_class = "cpu-reference-v1";
  base.candidate_payload_checksum = 29;
  base.detail = "external rejection";

  std::vector<CandidateRejection> malformed;
  const auto append = [&](CandidateRejection rejection) {
    rejection.candidate_id = CandidateId{.high = 31, .low = malformed.size() + 1U};
    malformed.push_back(std::move(rejection));
  };
  CandidateRejection wrong_schema = base;
  wrong_schema.schema_version = 2;
  append(std::move(wrong_schema));
  CandidateRejection wrong_stage = base;
  wrong_stage.stage = static_cast<CandidateLifecycleStage>(255);
  append(std::move(wrong_stage));
  CandidateRejection wrong_code = base;
  wrong_code.code = static_cast<CandidateRejectionCode>(255);
  append(std::move(wrong_code));
  CandidateRejection wrong_generator = base;
  wrong_generator.provenance.generator = static_cast<CandidateGeneratorKind>(255);
  append(std::move(wrong_generator));
  CandidateRejection wrong_backend = base;
  wrong_backend.provenance.backend = static_cast<CandidateBackendKind>(255);
  append(std::move(wrong_backend));
  CandidateRejection invalid_utf8 = base;
  invalid_utf8.invariant_id.assign(1, static_cast<char>(0xff));
  append(std::move(invalid_utf8));
  CandidateRejection long_invariant = base;
  long_invariant.invariant_id.assign(kMaximumCandidateDiagnosticBytes + 1U, 'i');
  append(std::move(long_invariant));
  CandidateRejection long_device = base;
  long_device.provenance.supported_device_class.assign(kMaximumCandidateDiagnosticBytes + 1U, 'd');
  append(std::move(long_device));
  CandidateRejection long_detail = base;
  long_detail.detail.assign(kMaximumCandidateDiagnosticBytes + 1U, 'x');
  append(std::move(long_detail));

  CandidateStore store(CandidateStoreConfig{
      .maximum_candidates_per_net = 1,
      .maximum_candidate_bytes_per_net = 1,
      .maximum_rejection_records = 32,
  });
  store.RetainRejections(malformed);
  const std::vector<CandidateRejection> retained = store.Rejections();
  ASSERT_EQ(retained.size(), malformed.size());
  for (std::size_t index = 0; index < retained.size(); ++index) {
    const CandidateRejection& replacement = retained[index];
    EXPECT_EQ(replacement.schema_version, 1U);
    EXPECT_EQ(replacement.candidate_id,
              std::optional<CandidateId>(CandidateId{.high = 31, .low = index + 1U}));
    EXPECT_EQ(replacement.net, base.net);
    EXPECT_EQ(replacement.stage, CandidateLifecycleStage::kGenerated);
    EXPECT_EQ(replacement.code, CandidateRejectionCode::kInvalidInput);
    EXPECT_EQ(replacement.invariant_id, "candidate.rejection.ingestion.v1");
    EXPECT_EQ(replacement.associations, base.associations);
    EXPECT_EQ(replacement.candidate_payload_checksum, base.candidate_payload_checksum);
    EXPECT_LE(replacement.invariant_id.size(), kMaximumCandidateDiagnosticBytes);
    EXPECT_LE(replacement.provenance.supported_device_class.size(),
              kMaximumCandidateDiagnosticBytes);
    EXPECT_LE(replacement.detail.size(), kMaximumCandidateDiagnosticBytes);
    const std::optional<std::uint64_t> logical_bytes = ComputeRejectionLogicalBytes(replacement);
    ASSERT_TRUE(logical_bytes.has_value());
    EXPECT_EQ(replacement.logical_bytes, *logical_bytes);
    EXPECT_NE(replacement.logical_bytes, 0U);
  }

  CandidateRejection boundary = base;
  boundary.candidate_id = CandidateId{.high = 37, .low = 41};
  boundary.invariant_id.assign(kMaximumCandidateDiagnosticBytes, 'i');
  boundary.provenance.supported_device_class.assign(kMaximumCandidateDiagnosticBytes, 'd');
  boundary.detail.assign(kMaximumCandidateDiagnosticBytes, 'x');
  boundary.logical_bytes = std::numeric_limits<std::uint64_t>::max();
  CandidateStore boundary_store(CandidateStoreConfig{
      .maximum_candidates_per_net = 1,
      .maximum_candidate_bytes_per_net = 1,
      .maximum_rejection_records = 1,
  });
  boundary_store.RetainRejection(boundary);
  const CandidateRejection retained_boundary = boundary_store.Rejections().front();
  EXPECT_EQ(retained_boundary.invariant_id, boundary.invariant_id);
  EXPECT_NE(retained_boundary.logical_bytes, boundary.logical_bytes);
  EXPECT_EQ(retained_boundary.logical_bytes,
            ComputeRejectionLogicalBytes(retained_boundary).value());
}

TEST(CandidateStoreTest, PublicationWorkOnlyDependsOnTouchedPools) {
  BoardData data = test_support::ValidM1BoardData();
  data.obstacles.clear();
  const BoardSnapshot board = Snapshot(std::move(data));
  const CompiledBoard compiled = Compile(board, test_support::DefaultCompilerProfile({0}));
  const std::array<CandidateCase, 3> cases = ThreeCandidates(board, compiled);
  const CandidateAdmissionContext first_context{
      .board = board, .compiled_board = compiled, .request = cases[0].request};
  const CandidateAdmissionContext second_context{
      .board = board, .compiled_board = compiled, .request = cases[1].request};
  const RouteCandidate existing =
      test_support::AcceptedCandidate(first_context, cases[0].generated);
  const RouteCandidate incoming =
      test_support::AcceptedCandidate(second_context, cases[1].generated);

  CandidateStore baseline(StoreConfig());
  CandidateStoreTestPeer::ForcePool(baseline, {existing});
  ASSERT_TRUE(
      std::holds_alternative<StoredCandidate>(CandidateStoreTestPeer::Publish(baseline, incoming)));
  const std::uint64_t baseline_work =
      CandidateStoreTestPeer::LastPublicationCandidateInspections(baseline);

  std::vector<RouteCandidate> crowded_pool = {existing};
  for (std::uint64_t index = 0; index < 128; ++index) {
    crowded_pool.push_back(CandidateStoreTestPeer::ForceNetAndId(
        existing, board_ir::EntityRef{.id = 10'000 + index, .generation = 0},
        CandidateId{.high = 0xfeed'faceU, .low = index + 1}));
  }
  CandidateStore crowded(StoreConfig());
  CandidateStoreTestPeer::ForcePool(crowded, std::move(crowded_pool));
  ASSERT_TRUE(
      std::holds_alternative<StoredCandidate>(CandidateStoreTestPeer::Publish(crowded, incoming)));
  EXPECT_EQ(CandidateStoreTestPeer::LastPublicationCandidateInspections(crowded), baseline_work);
  ASSERT_EQ(crowded.Enumerate(board_ir::EntityRef{.id = 10'064, .generation = 0}).size(), 1U);
}

TEST(CandidateStoreTest, GlobalIdIndexRejectsCrossNetCollisionWithoutTouchingEitherPool) {
  BoardData data = test_support::ValidM1BoardData();
  data.obstacles.clear();
  const BoardSnapshot board = Snapshot(std::move(data));
  const CompiledBoard compiled = Compile(board, test_support::DefaultCompilerProfile({0}));
  const std::array<CandidateCase, 3> cases = ThreeCandidates(board, compiled);
  const CandidateAdmissionContext first_context{
      .board = board, .compiled_board = compiled, .request = cases[0].request};
  const CandidateAdmissionContext second_context{
      .board = board, .compiled_board = compiled, .request = cases[1].request};
  const RouteCandidate existing =
      test_support::AcceptedCandidate(first_context, cases[0].generated);
  const RouteCandidate incoming = CandidateStoreTestPeer::ForceNetAndId(
      test_support::AcceptedCandidate(second_context, cases[1].generated),
      board_ir::EntityRef{.id = 88, .generation = 0}, existing.id());

  CandidateStore store(StoreConfig());
  CandidateStoreTestPeer::ForcePool(store, {existing});
  const CandidateStoreAdmissionResult result = CandidateStoreTestPeer::Publish(store, incoming);
  ASSERT_TRUE(std::holds_alternative<CandidateRejection>(result));
  EXPECT_EQ(std::get<CandidateRejection>(result).code, CandidateRejectionCode::kDuplicateIdentity);
  EXPECT_EQ(store.Enumerate(existing.net()).size(), 1U);
  EXPECT_TRUE(store.Enumerate(incoming.net()).empty());
}

TEST(CandidateStoreTest, BytePressureNeverPromotesLowerRankedDuplicate) {
  BoardData data = test_support::ValidM1BoardData();
  data.obstacles.clear();
  const BoardSnapshot board = Snapshot(std::move(data));
  const CompiledBoard compiled = Compile(board, test_support::DefaultCompilerProfile({0}));
  const std::array<CandidateCase, 3> cases = ThreeCandidates(board, compiled);
  const std::array straight = {
      LayerSegment{.layer = 0,
                   .centerline = {.start = {.x = 0, .y = 0}, .end = {.x = 100, .y = 0}}},
  };
  const CandidateCase first_duplicate = MakeCase(board, compiled, 20, 200, straight, 100);
  const CandidateCase second_duplicate = MakeCase(board, compiled, 21, 201, straight, 100);
  const CandidateAdmissionContext pin_context{
      .board = board, .compiled_board = compiled, .request = cases[1].request};
  const CandidateAdmissionContext first_context{
      .board = board, .compiled_board = compiled, .request = first_duplicate.request};
  const CandidateAdmissionContext second_context{
      .board = board, .compiled_board = compiled, .request = second_duplicate.request};
  const RouteCandidate pin_source =
      test_support::AcceptedCandidate(pin_context, cases[1].generated);
  const RouteCandidate first =
      test_support::AcceptedCandidate(first_context, first_duplicate.generated);
  const RouteCandidate second =
      test_support::AcceptedCandidate(second_context, second_duplicate.generated);
  const RouteCandidate& preferred_source = CandidateRanksBefore(first, second) ? first : second;
  const RouteCandidate& lower_ranked_source = CandidateRanksBefore(first, second) ? second : first;
  const RouteCandidate pinned = CandidateStoreTestPeer::ForceLogicalBytes(pin_source, 100);
  const RouteCandidate preferred = CandidateStoreTestPeer::ForceLogicalBytes(preferred_source, 200);
  const RouteCandidate lower_ranked =
      CandidateStoreTestPeer::ForceLogicalBytes(lower_ranked_source, 50);

  CandidateStore store(StoreConfig(3, 250));
  ASSERT_TRUE(
      std::holds_alternative<StoredCandidate>(CandidateStoreTestPeer::Publish(store, pinned)));
  ASSERT_FALSE(store.Pin(44, pinned.id()).has_value());
  const std::vector<CandidateStoreAdmissionResult> results =
      CandidateStoreTestPeer::PublishBatch(store, {lower_ranked, preferred});
  ASSERT_EQ(results.size(), 2U);
  EXPECT_TRUE(std::ranges::all_of(results, [](const CandidateStoreAdmissionResult& result) {
    return std::holds_alternative<CandidateRejection>(result);
  }));
  EXPECT_TRUE(std::ranges::any_of(results, [](const CandidateStoreAdmissionResult& result) {
    return std::get<CandidateRejection>(result).code == CandidateRejectionCode::kBudgetExhausted;
  }));
  EXPECT_TRUE(std::ranges::any_of(results, [](const CandidateStoreAdmissionResult& result) {
    return std::get<CandidateRejection>(result).code == CandidateRejectionCode::kDuplicateGeometry;
  }));
  const std::vector<StoredCandidate> retained = store.Enumerate(pinned.net());
  ASSERT_EQ(retained.size(), 1U);
  EXPECT_EQ(retained.front()->id(), pinned.id());
}

TEST(CandidateStoreTest, OversizedStableWinnerNeverPromotesSmallerDuplicateLoser) {
  BoardData data = test_support::ValidM1BoardData();
  data.obstacles.clear();
  const BoardSnapshot board = Snapshot(std::move(data));
  const CompiledBoard compiled = Compile(board, test_support::DefaultCompilerProfile({0}));
  const std::array<CandidateCase, 3> cases = ThreeCandidates(board, compiled);
  const std::array straight_segments = {
      LayerSegment{.layer = 0,
                   .centerline = {.start = {.x = 0, .y = 0}, .end = {.x = 100, .y = 0}}},
  };
  const std::array shoulder_segments = {
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

  const auto accept = [&](const CandidateCase& candidate_case) {
    return test_support::AcceptedCandidate(
        CandidateAdmissionContext{
            .board = board, .compiled_board = compiled, .request = candidate_case.request},
        candidate_case.generated);
  };
  const auto run = [&](RouteCandidate first, RouteCandidate second,
                       CandidateRejectionCode duplicate_code) {
    if (CandidateRanksBefore(second, first)) {
      std::swap(first, second);
    }
    first = CandidateStoreTestPeer::ForceLogicalBytes(first, 251);
    second = CandidateStoreTestPeer::ForceLogicalBytes(second, 1);
    CandidateStore store(StoreConfig(4, 250));
    const std::vector<CandidateStoreAdmissionResult> results =
        CandidateStoreTestPeer::PublishBatch(store, {second, first});
    ASSERT_EQ(results.size(), 2U);
    EXPECT_TRUE(std::ranges::all_of(results, [](const CandidateStoreAdmissionResult& result) {
      return std::holds_alternative<CandidateRejection>(result);
    }));
    EXPECT_TRUE(std::ranges::any_of(results, [](const CandidateStoreAdmissionResult& result) {
      return std::get<CandidateRejection>(result).code == CandidateRejectionCode::kBudgetExhausted;
    }));
    EXPECT_TRUE(
        std::ranges::any_of(results, [duplicate_code](const CandidateStoreAdmissionResult& result) {
          return std::get<CandidateRejection>(result).code == duplicate_code;
        }));
    EXPECT_TRUE(store.Enumerate(first.net()).empty());
  };

  const CandidateCase same_id_first = MakeCase(board, compiled, 40, 400, straight_segments, 100);
  const CandidateCase same_id_second = MakeCase(board, compiled, 40, 400, shoulder_segments, 152);
  ASSERT_EQ(same_id_first.generated.id, same_id_second.generated.id);
  run(accept(same_id_first), accept(same_id_second), CandidateRejectionCode::kDuplicateIdentity);

  const CandidateCase same_geometry_first =
      MakeCase(board, compiled, 41, 401, straight_segments, 100);
  const CandidateCase same_geometry_second =
      MakeCase(board, compiled, 42, 402, straight_segments, 100);
  run(accept(same_geometry_first), accept(same_geometry_second),
      CandidateRejectionCode::kDuplicateGeometry);

  const CandidateAdmissionContext straight_context{
      .board = board, .compiled_board = compiled, .request = cases[0].request};
  const CandidateAdmissionContext shoulder_context{
      .board = board, .compiled_board = compiled, .request = cases[1].request};
  const RouteCandidate straight =
      test_support::AcceptedCandidate(straight_context, cases[0].generated);
  const RouteCandidate shoulder = CandidateStoreTestPeer::ForceResources(
      test_support::AcceptedCandidate(shoulder_context, cases[1].generated), straight);
  run(straight, shoulder, CandidateRejectionCode::kDuplicateResources);
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
  ASSERT_TRUE(std::holds_alternative<StoredCandidate>(
      store.Admit(first_context, CandidateCopy(first.generated))));
  const CandidateStoreAdmissionResult drift =
      store.Admit(second_context, CandidateCopy(second.generated));
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
