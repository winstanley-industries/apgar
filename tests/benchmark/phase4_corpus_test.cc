#include "apgar/benchmark/phase4_corpus.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <ranges>
#include <string>
#include <variant>

#include "apgar/adapters/kicad_fixture.h"
#include "apgar/board_ir/stable_hash.h"
#include "apgar/routing/cpu_astar.h"
#include "apgar/tooling/runfiles.h"
#include "tests/support/google_test.h"

namespace apgar::benchmark {
namespace {

[[nodiscard]] std::string ReadFixture() {
  return tooling::ReadRunfile("tests/fixtures/phase4_supported_multinet_v1.kicad_pcb")
      .value_or(std::string{});
}

[[nodiscard]] Phase4ImportedMultiNetCorpus Built(Phase4ImportedMultiNetCorpusResult result) {
  EXPECT_TRUE(std::holds_alternative<Phase4ImportedMultiNetCorpus>(result))
      << (std::holds_alternative<Phase4CorpusError>(result)
              ? std::get<Phase4CorpusError>(result).detail
              : "");
  if (!std::holds_alternative<Phase4ImportedMultiNetCorpus>(result)) {
    std::abort();
  }
  return std::get<Phase4ImportedMultiNetCorpus>(std::move(result));
}

[[nodiscard]] Phase4ImportedMultiNetCorpus BuiltUnpinned(
    std::string_view contents, const geometry_compiler::CompilerProfile& compiler_profile) {
  adapters::KicadFixtureImportResult imported = adapters::ImportKicadMultiNetFixture(
      contents, adapters::KicadMultiNetFixtureImportConfig{
                    .routable_net_names = {"ROUTE_A", "ROUTE_B"},
                    .default_routing_net_name = "ROUTE_A",
                    .nominal_width = 500'000,
                    .clearance = 500'000,
                });
  EXPECT_TRUE(std::holds_alternative<board_ir::BoardSnapshot>(imported));
  if (!std::holds_alternative<board_ir::BoardSnapshot>(imported)) {
    std::abort();
  }
  board_ir::BoardSnapshot board = std::get<board_ir::BoardSnapshot>(std::move(imported));
  const auto route_a = std::ranges::find(board.data().nets, "ROUTE_A", &board_ir::Net::name);
  const auto route_b = std::ranges::find(board.data().nets, "ROUTE_B", &board_ir::Net::name);
  EXPECT_NE(route_a, board.data().nets.end());
  EXPECT_NE(route_b, board.data().nets.end());
  if (route_a == board.data().nets.end() || route_b == board.data().nets.end()) {
    std::abort();
  }
  std::array specs = {
      allocator::MultiNetRoutingSpec{
          .routing_profile = board.data().routing_profile, .start_layer = 0, .goal_layer = 0},
      allocator::MultiNetRoutingSpec{
          .routing_profile = board.data().routing_profile, .start_layer = 0, .goal_layer = 0},
  };
  specs[0].routing_profile.net = route_a->ref;
  specs[1].routing_profile.net = route_b->ref;
  allocator::MultiNetWorkloadResult workload = allocator::BuildMultiNetWorkload(
      allocator::kMultiNetWorkloadSchemaVersion, board, compiler_profile, specs, specs.size());
  EXPECT_TRUE(std::holds_alternative<allocator::MultiNetWorkload>(workload));
  if (!std::holds_alternative<allocator::MultiNetWorkload>(workload)) {
    std::abort();
  }
  return Phase4ImportedMultiNetCorpus{
      .board = std::move(board),
      .workload = std::get<allocator::MultiNetWorkload>(std::move(workload)),
  };
}

TEST(Phase4ImportedMultiNetCorpusTest, PinsAuthenticImportedWorkloadIdentity) {
  const std::string contents = ReadFixture();
  ASSERT_FALSE(contents.empty());

  const Phase4ImportedMultiNetCorpus corpus = Built(BuildPhase4ImportedMultiNetCorpusV1(contents));

  EXPECT_EQ(corpus.board.data().terminals.size(), 4U);
  EXPECT_EQ(corpus.board.data().obstacles.size(), 9U);
  ASSERT_EQ(corpus.workload.nets().size(), 2U);
  EXPECT_EQ(corpus.workload.board_content_hash(), corpus.board.content_hash());
  EXPECT_EQ(corpus.workload.compiled_node_count(), 13'122U);
  EXPECT_EQ(board_ir::StableHashString(contents), 8'729'721'683'359'945'012ULL);
  EXPECT_EQ(corpus.board.content_hash(), 229'027'575'659'763'193ULL);
  EXPECT_EQ(corpus.workload.compiler_profile_fingerprint(), 14'092'128'556'178'973'098ULL);
  EXPECT_EQ(corpus.workload.workload_checksum(), 13'031'419'002'947'588'674ULL);

  const allocator::PreparedNetRoutingContext& first = corpus.workload.nets()[0];
  const allocator::PreparedNetRoutingContext& second = corpus.workload.nets()[1];
  EXPECT_NE(first.request.net, second.request.net);
  EXPECT_NE(first.routing_profile_fingerprint, second.routing_profile_fingerprint);
  EXPECT_NE(first.request.start, second.request.start);
  EXPECT_NE(first.request.goal, second.request.goal);
  EXPECT_EQ(first.request.net.id, 3'033'425'279'953'999'715ULL);
  EXPECT_EQ(first.routing_profile_fingerprint, 9'653'149'663'899'239'486ULL);
  EXPECT_EQ(first.request.start.x, 36'000'000);
  EXPECT_EQ(first.request.start.y, 28'000'000);
  EXPECT_EQ(first.request.goal.x, 4'000'000);
  EXPECT_EQ(first.request.goal.y, 28'000'000);
  EXPECT_EQ(first.compiled_board.rule_bucket().identity, 16'006'352'534'979'059'238ULL);
  EXPECT_EQ(second.request.start.x, 4'000'000);
  EXPECT_EQ(second.request.start.y, 12'000'000);
  EXPECT_EQ(second.request.goal.x, 36'000'000);
  EXPECT_EQ(second.request.goal.y, 12'000'000);
  EXPECT_EQ(second.compiled_board.rule_bucket().identity, 16'006'352'534'979'059'238ULL);
  EXPECT_EQ(second.request.net.id, 3'033'426'379'465'627'926ULL);
  EXPECT_EQ(second.routing_profile_fingerprint, 10'553'003'216'368'397'518ULL);
  EXPECT_EQ(corpus.workload.geometry_compiler_version(), 1U);
  for (const allocator::PreparedNetRoutingContext& context : corpus.workload.nets()) {
    EXPECT_EQ(context.request.net, context.routing_profile.net);
    EXPECT_EQ(context.request.start_layer, 0U);
    EXPECT_EQ(context.request.goal_layer, 0U);
    EXPECT_EQ(context.compiled_board.source_board_content_hash(), corpus.board.content_hash());
  }
}

TEST(Phase4ImportedMultiNetCorpusTest, CanonicalizesImportedSpecOrder) {
  const std::string contents = ReadFixture();
  ASSERT_FALSE(contents.empty());
  const Phase4ImportedMultiNetCorpus corpus = Built(BuildPhase4ImportedMultiNetCorpusV1(contents));
  ASSERT_EQ(corpus.workload.nets().size(), 2U);

  const std::array reversed_specs = {
      allocator::MultiNetRoutingSpec{
          .routing_profile = corpus.workload.nets()[1].routing_profile,
          .start_layer = corpus.workload.nets()[1].request.start_layer,
          .goal_layer = corpus.workload.nets()[1].request.goal_layer,
      },
      allocator::MultiNetRoutingSpec{
          .routing_profile = corpus.workload.nets()[0].routing_profile,
          .start_layer = corpus.workload.nets()[0].request.start_layer,
          .goal_layer = corpus.workload.nets()[0].request.goal_layer,
      },
  };
  allocator::MultiNetWorkloadResult rebuilt = allocator::BuildMultiNetWorkload(
      allocator::kMultiNetWorkloadSchemaVersion, corpus.board, corpus.workload.compiler_profile(),
      reversed_specs, reversed_specs.size());

  ASSERT_TRUE(std::holds_alternative<allocator::MultiNetWorkload>(rebuilt));
  const allocator::MultiNetWorkload& reordered = std::get<allocator::MultiNetWorkload>(rebuilt);
  EXPECT_EQ(reordered.workload_checksum(), corpus.workload.workload_checksum());
  EXPECT_EQ(reordered.nets(), corpus.workload.nets());
}

TEST(Phase4ImportedMultiNetCorpusTest, CpuOracleRoutesEveryImportedContextExactly) {
  const std::string contents = ReadFixture();
  ASSERT_FALSE(contents.empty());
  const Phase4ImportedMultiNetCorpus corpus = Built(BuildPhase4ImportedMultiNetCorpusV1(contents));

  for (const allocator::PreparedNetRoutingContext& context : corpus.workload.nets()) {
    routing::CpuRouteResult result =
        routing::RouteWithCpuAStar(corpus.board, context.compiled_board, context.request);
    ASSERT_TRUE(std::holds_alternative<routing::CpuRoute>(result))
        << (std::holds_alternative<routing::RouteFailure>(result)
                ? std::get<routing::RouteFailure>(result).detail
                : "");
    const routing::CpuRoute& route = std::get<routing::CpuRoute>(result);
    EXPECT_TRUE(routing::CpuRouteHasAuthenticatedAStarEvidence(route));
    EXPECT_FALSE(routing::ValidateReconstructedRoute(corpus.board, context.compiled_board,
                                                     context.request, route.segments)
                     .has_value());
  }
}

TEST(Phase4ImportedMultiNetCorpusTest, CompiledContextsBlockForeignPadsSymmetrically) {
  const std::string contents = ReadFixture();
  ASSERT_FALSE(contents.empty());
  const Phase4ImportedMultiNetCorpus corpus = Built(BuildPhase4ImportedMultiNetCorpusV1(contents));
  ASSERT_EQ(corpus.workload.nets().size(), 2U);
  const allocator::PreparedNetRoutingContext& route_b = corpus.workload.nets()[0];
  const allocator::PreparedNetRoutingContext& route_a = corpus.workload.nets()[1];

  EXPECT_TRUE(route_b.compiled_board.EdgeIsLegal(0, 72, 56, geometry_compiler::Direction::kWest));
  EXPECT_FALSE(route_b.compiled_board.EdgeIsLegal(0, 8, 24, geometry_compiler::Direction::kEast));
  EXPECT_TRUE(route_a.compiled_board.EdgeIsLegal(0, 8, 24, geometry_compiler::Direction::kEast));
  EXPECT_FALSE(route_a.compiled_board.EdgeIsLegal(0, 8, 56, geometry_compiler::Direction::kEast));
}

TEST(Phase4ImportedMultiNetCorpusTest, RejectsSemanticAndNormalizedSourceDrift) {
  const std::string contents = ReadFixture();
  ASSERT_FALSE(contents.empty());
  std::string semantic_change = contents;
  const std::string needle = "(at 2 6)";
  const std::size_t offset = semantic_change.find(needle);
  ASSERT_NE(offset, std::string::npos);
  semantic_change.replace(offset, needle.size(), "(at 3 6)");
  std::string normalized_change = contents;
  normalized_change.insert(normalized_change.find("(version"), "; source drift\n  ");

  Phase4ImportedMultiNetCorpusResult semantic =
      BuildPhase4ImportedMultiNetCorpusV1(semantic_change);
  Phase4ImportedMultiNetCorpusResult normalized =
      BuildPhase4ImportedMultiNetCorpusV1(normalized_change);

  ASSERT_TRUE(std::holds_alternative<Phase4CorpusError>(semantic));
  ASSERT_TRUE(std::holds_alternative<Phase4CorpusError>(normalized));
  EXPECT_EQ(std::get<Phase4CorpusError>(semantic).code,
            Phase4CorpusErrorCode::kFixtureIdentityMismatch);
  EXPECT_EQ(std::get<Phase4CorpusError>(normalized).code,
            Phase4CorpusErrorCode::kFixtureIdentityMismatch);
}

TEST(Phase4ImportedMultiNetCorpusTest, TerminalUuidMutationChangesBoardAndWorkloadIdentity) {
  const std::string contents = ReadFixture();
  ASSERT_FALSE(contents.empty());
  const Phase4ImportedMultiNetCorpus canonical =
      Built(BuildPhase4ImportedMultiNetCorpusV1(contents));
  std::string changed = contents;
  const std::string uuid = "10000000-0000-4000-8000-000000000101";
  const std::size_t offset = changed.find(uuid);
  ASSERT_NE(offset, std::string::npos);
  changed.replace(offset, uuid.size(), "10000000-0000-4000-8000-000000000111");

  const Phase4ImportedMultiNetCorpus original =
      BuiltUnpinned(contents, canonical.workload.compiler_profile());
  const Phase4ImportedMultiNetCorpus mutated =
      BuiltUnpinned(changed, canonical.workload.compiler_profile());

  EXPECT_EQ(original.workload.workload_checksum(), canonical.workload.workload_checksum());
  EXPECT_NE(original.board.data().terminals, mutated.board.data().terminals);
  EXPECT_NE(original.board.data().obstacles, mutated.board.data().obstacles);
  EXPECT_NE(original.board.content_hash(), mutated.board.content_hash());
  EXPECT_NE(original.workload.workload_checksum(), mutated.workload.workload_checksum());
}

TEST(Phase4ImportedMultiNetCorpusTest, RejectsFixtureWithoutExactRoster) {
  Phase4ImportedMultiNetCorpusResult result = BuildPhase4ImportedMultiNetCorpusV1("(kicad_pcb)");

  ASSERT_TRUE(std::holds_alternative<Phase4CorpusError>(result));
  EXPECT_EQ(std::get<Phase4CorpusError>(result).code,
            Phase4CorpusErrorCode::kFixtureIdentityMismatch);
}

}  // namespace
}  // namespace apgar::benchmark
