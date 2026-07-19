#include "apgar/routing/candidate_policy.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <set>
#include <utility>
#include <variant>
#include <vector>

#include "apgar/board_ir/board.h"
#include "apgar/geometry_compiler/compiled_board.h"
#include "tests/support/board_builder.h"
#include "tests/support/compiler_builder.h"
#include "tests/support/google_test.h"
#include "tests/support/routing_builder.h"

namespace apgar::routing {
namespace {

using board_ir::BoardData;
using board_ir::BoardSnapshot;
using geometry_compiler::CompiledBoard;
using geometry_compiler::Direction;
using geometry_compiler::LatticeIndex;
using test_support::Compile;
using test_support::Snapshot;

[[nodiscard]] CompiledBoard UnobstructedField() {
  BoardData data = test_support::ValidM1BoardData();
  data.obstacles.clear();
  BoardSnapshot board = Snapshot(std::move(data));
  return Compile(board, test_support::DefaultCompilerProfile({0}));
}

[[nodiscard]] std::vector<EdgeResourceKey> LegalResources(const CompiledBoard& board) {
  constexpr std::array kCanonicalDirections = {
      Direction::kEast,
      Direction::kNorthEast,
      Direction::kNorth,
      Direction::kNorthWest,
  };
  std::vector<EdgeResourceKey> resources;
  for (const geometry_compiler::SparseTile& tile : board.tiles()) {
    for (const geometry_compiler::CompiledNode& node : tile.nodes) {
      const LatticeIndex source =
          geometry_compiler::GlobalLatticeIndex(board.profile(), tile.key, node.local_index);
      for (Direction direction : kCanonicalDirections) {
        const std::optional<EdgeResourceKey> resource =
            CanonicalPhysicalEdgeResource(tile.key.layer, source, direction);
        if (resource.has_value() && ResourceExists(board, *resource)) {
          resources.push_back(*resource);
        }
      }
    }
  }
  std::ranges::sort(resources);
  resources.erase(std::ranges::unique(resources).begin(), resources.end());
  return resources;
}

[[nodiscard]] const CandidatePolicyError& PolicyError(const CandidatePolicyResult& result) {
  EXPECT_TRUE(std::holds_alternative<CandidatePolicyError>(result));
  if (!std::holds_alternative<CandidatePolicyError>(result)) {
    std::abort();
  }
  return std::get<CandidatePolicyError>(result);
}

[[nodiscard]] const CandidatePolicyError& BatchError(const CandidatePolicyBatchResult& result) {
  EXPECT_TRUE(std::holds_alternative<CandidatePolicyError>(result));
  if (!std::holds_alternative<CandidatePolicyError>(result)) {
    std::abort();
  }
  return std::get<CandidatePolicyError>(result);
}

TEST(CandidatePolicyTest, ReverseTraversalCanonicalizesToOnePhysicalResource) {
  constexpr board_ir::LayerId kLayer = 7;
  const std::array<
      std::pair<std::pair<LatticeIndex, Direction>, std::pair<LatticeIndex, Direction>>, 4>
      traversal_pairs = {{
          {{{.x = 2, .y = 3}, Direction::kEast}, {{.x = 3, .y = 3}, Direction::kWest}},
          {{{.x = 2, .y = 3}, Direction::kNorthEast}, {{.x = 3, .y = 4}, Direction::kSouthWest}},
          {{{.x = 2, .y = 3}, Direction::kNorth}, {{.x = 2, .y = 4}, Direction::kSouth}},
          {{{.x = 2, .y = 3}, Direction::kNorthWest}, {{.x = 1, .y = 4}, Direction::kSouthEast}},
      }};

  for (const auto& [forward, reverse] : traversal_pairs) {
    const std::optional<EdgeResourceKey> forward_resource =
        CanonicalPhysicalEdgeResource(kLayer, forward.first, forward.second);
    const std::optional<EdgeResourceKey> reverse_resource =
        CanonicalPhysicalEdgeResource(kLayer, reverse.first, reverse.second);
    ASSERT_TRUE(forward_resource.has_value());
    ASSERT_TRUE(reverse_resource.has_value());
    EXPECT_EQ(*forward_resource, *reverse_resource);
  }

  EXPECT_FALSE(
      CanonicalPhysicalEdgeResource(kLayer, LatticeIndex{.x = 0, .y = 0}, static_cast<Direction>(8))
          .has_value());
  EXPECT_FALSE(CanonicalPhysicalEdgeResource(
                   kLayer, LatticeIndex{.x = std::numeric_limits<std::int64_t>::min(), .y = 0},
                   Direction::kWest)
                   .has_value());
  EXPECT_FALSE(CanonicalPhysicalEdgeResource(
                   kLayer, LatticeIndex{.x = std::numeric_limits<std::int64_t>::max(), .y = 0},
                   Direction::kSouthEast)
                   .has_value());
}

TEST(CandidatePolicyTest, NormalizationIsCanonicalAndIdentityIncludesProvenance) {
  const CompiledBoard compiled = UnobstructedField();
  const std::vector<EdgeResourceKey> resources = LegalResources(compiled);
  ASSERT_GE(resources.size(), 4U);

  CandidateGenerationPolicy first{
      .objective = CandidateObjective::kResourceDiverse,
      .deterministic_seed = 0x1234U,
      .candidate_ordinal = 9,
      .orthogonal_step_surcharge = 2,
      .diagonal_step_surcharge = 3,
      .bend_surcharge = 4,
      .banned_resources = {resources[1], resources[0], resources[1]},
      .resource_penalties =
          {
              ResourcePenalty{.resource = resources[2], .additional_cost = 7},
              ResourcePenalty{.resource = resources[3], .additional_cost = 0},
              ResourcePenalty{.resource = resources[2], .additional_cost = 5},
          },
  };
  CandidateGenerationPolicy reordered = first;
  std::ranges::reverse(reordered.banned_resources);
  std::ranges::reverse(reordered.resource_penalties);

  const CandidatePolicyResult first_result =
      NormalizeCandidateGenerationPolicy(compiled, std::move(first));
  const CandidatePolicyResult reordered_result =
      NormalizeCandidateGenerationPolicy(compiled, std::move(reordered));

  ASSERT_TRUE(std::holds_alternative<NormalizedCandidateGenerationPolicy>(first_result));
  ASSERT_TRUE(std::holds_alternative<NormalizedCandidateGenerationPolicy>(reordered_result));
  const NormalizedCandidateGenerationPolicy& normalized =
      std::get<NormalizedCandidateGenerationPolicy>(first_result);
  EXPECT_EQ(normalized, std::get<NormalizedCandidateGenerationPolicy>(reordered_result));
  EXPECT_EQ(normalized.policy.banned_resources,
            (std::vector<EdgeResourceKey>{resources[0], resources[1]}));
  ASSERT_EQ(normalized.policy.resource_penalties.size(), 1U);
  EXPECT_EQ(normalized.policy.resource_penalties.front(),
            (ResourcePenalty{.resource = resources[2], .additional_cost = 12}));
  EXPECT_EQ(normalized.identity, FingerprintCandidateGenerationPolicy(normalized.policy));

  CandidateGenerationPolicy another_ordinal = normalized.policy;
  ++another_ordinal.candidate_ordinal;
  const CandidatePolicyResult another_result =
      NormalizeCandidateGenerationPolicy(compiled, std::move(another_ordinal));
  ASSERT_TRUE(std::holds_alternative<NormalizedCandidateGenerationPolicy>(another_result));
  EXPECT_NE(normalized.identity,
            std::get<NormalizedCandidateGenerationPolicy>(another_result).identity);
}

TEST(CandidatePolicyTest, RejectsInvalidConflictingUnsupportedAndOverflowingPolicies) {
  const CompiledBoard compiled = UnobstructedField();
  const std::vector<EdgeResourceKey> resources = LegalResources(compiled);
  ASSERT_FALSE(resources.empty());

  CandidateGenerationPolicy invalid_resource;
  invalid_resource.resource_penalties = {ResourcePenalty{
      .resource =
          EdgeResourceKey{
              .layer = 999,
              .lattice_x = 0,
              .lattice_y = 0,
              .direction = Direction::kEast,
          },
      .additional_cost = 0,
  }};
  EXPECT_EQ(PolicyError(NormalizeCandidateGenerationPolicy(compiled, invalid_resource)).code,
            CandidatePolicyErrorCode::kInvalidResource);

  CandidateGenerationPolicy conflicting;
  conflicting.banned_resources = {resources.front()};
  conflicting.resource_penalties = {
      ResourcePenalty{.resource = resources.front(), .additional_cost = 1}};
  EXPECT_EQ(PolicyError(NormalizeCandidateGenerationPolicy(compiled, conflicting)).code,
            CandidatePolicyErrorCode::kConflictingResourceAction);

  CandidateGenerationPolicy unsupported_schema;
  ++unsupported_schema.schema_version;
  EXPECT_EQ(PolicyError(NormalizeCandidateGenerationPolicy(compiled, unsupported_schema)).code,
            CandidatePolicyErrorCode::kUnsupportedSchema);

  CandidateGenerationPolicy unsupported_objective;
  unsupported_objective.objective = static_cast<CandidateObjective>(255);
  EXPECT_EQ(PolicyError(NormalizeCandidateGenerationPolicy(compiled, unsupported_objective)).code,
            CandidatePolicyErrorCode::kUnsupportedObjective);

  CandidateGenerationPolicy duplicate_overflow;
  duplicate_overflow.resource_penalties = {
      ResourcePenalty{.resource = resources.front(),
                      .additional_cost = std::numeric_limits<std::uint64_t>::max()},
      ResourcePenalty{.resource = resources.front(), .additional_cost = 1},
  };
  EXPECT_EQ(PolicyError(NormalizeCandidateGenerationPolicy(compiled, duplicate_overflow)).code,
            CandidatePolicyErrorCode::kCostOverflow);

  CandidateGenerationPolicy path_bound_overflow;
  path_bound_overflow.orthogonal_step_surcharge = std::numeric_limits<std::uint64_t>::max();
  EXPECT_EQ(PolicyError(NormalizeCandidateGenerationPolicy(compiled, path_bound_overflow)).code,
            CandidatePolicyErrorCode::kCostOverflow);
}

TEST(CandidatePolicyTest, StepCostUsesSurchargesBendsAndCanonicalResourcePenalty) {
  const CompiledBoard compiled = UnobstructedField();
  const EdgeResourceKey east{
      .layer = 0,
      .lattice_x = 0,
      .lattice_y = 0,
      .direction = Direction::kEast,
  };
  ASSERT_TRUE(ResourceExists(compiled, east));
  CandidateGenerationPolicy input{
      .objective = CandidateObjective::kLengthBiased,
      .orthogonal_step_surcharge = 2,
      .diagonal_step_surcharge = 4,
      .bend_surcharge = 5,
      .banned_resources = {},
      .resource_penalties = {ResourcePenalty{.resource = east, .additional_cost = 7}},
  };
  const CandidatePolicyResult result =
      NormalizeCandidateGenerationPolicy(compiled, std::move(input));
  ASSERT_TRUE(std::holds_alternative<NormalizedCandidateGenerationPolicy>(result));
  const CandidateGenerationPolicy& policy =
      std::get<NormalizedCandidateGenerationPolicy>(result).policy;

  EXPECT_EQ(StepCostUnderPolicy(compiled.profile(), Direction::kEast,
                                static_cast<std::uint8_t>(Direction::kNorth), policy, east),
            27U);
  EXPECT_EQ(StepCostUnderPolicy(compiled.profile(), Direction::kEast,
                                static_cast<std::uint8_t>(Direction::kEast), policy, east),
            19U);

  CandidateGenerationPolicy objective_only = policy;
  objective_only.objective = CandidateObjective::kBendBiased;
  const CandidatePolicyResult objective_result =
      NormalizeCandidateGenerationPolicy(compiled, std::move(objective_only));
  ASSERT_TRUE(std::holds_alternative<NormalizedCandidateGenerationPolicy>(objective_result));
  const auto& changed = std::get<NormalizedCandidateGenerationPolicy>(objective_result);
  EXPECT_NE(changed.identity, std::get<NormalizedCandidateGenerationPolicy>(result).identity);
  EXPECT_EQ(StepCostUnderPolicy(compiled.profile(), Direction::kEast,
                                static_cast<std::uint8_t>(Direction::kNorth), changed.policy, east),
            27U);
}

TEST(CandidatePolicyTest, DeterministicAlternativeScheduleProducesUniqueOrderedPolicies) {
  const CompiledBoard compiled = UnobstructedField();
  const std::vector<EdgeResourceKey> resources = LegalResources(compiled);
  ASSERT_GE(resources.size(), 2U);
  CandidateGenerationPolicy base;
  base.deterministic_seed = 0xBADC0FFEEU;
  base.candidate_ordinal = 11;
  DeterministicAlternativePolicySchedule schedule{
      .candidate_count = 9,
      .step_surcharge_increment = 2,
      .bend_surcharge_increment = 3,
      .resource_penalty_increment = 5,
      .alternative_resources = {resources[1], resources[0], resources[1]},
  };

  const CandidatePolicyBatchResult first =
      BuildDeterministicAlternativePolicies(compiled, base, schedule);
  const CandidatePolicyBatchResult second =
      BuildDeterministicAlternativePolicies(compiled, base, schedule);

  ASSERT_TRUE(std::holds_alternative<std::vector<NormalizedCandidateGenerationPolicy>>(first));
  ASSERT_TRUE(std::holds_alternative<std::vector<NormalizedCandidateGenerationPolicy>>(second));
  const auto& policies = std::get<std::vector<NormalizedCandidateGenerationPolicy>>(first);
  EXPECT_EQ(policies, std::get<std::vector<NormalizedCandidateGenerationPolicy>>(second));
  ASSERT_EQ(policies.size(), 9U);
  std::set<std::uint64_t> identities;
  for (std::size_t index = 0; index < policies.size(); ++index) {
    EXPECT_EQ(policies[index].policy.candidate_ordinal, 11U + index);
    EXPECT_EQ(policies[index].policy.deterministic_seed, base.deterministic_seed);
    EXPECT_EQ(policies[index].identity,
              FingerprintCandidateGenerationPolicy(policies[index].policy));
    EXPECT_TRUE(identities.insert(policies[index].identity).second);
  }
  EXPECT_EQ(policies[0].policy.objective, CandidateObjective::kBaseScalarCost);
  EXPECT_EQ(policies[1].policy.objective, CandidateObjective::kLengthBiased);
  EXPECT_EQ(policies[1].policy.orthogonal_step_surcharge, 2U);
  EXPECT_EQ(policies[1].policy.diagonal_step_surcharge, 2U);
  EXPECT_EQ(policies[2].policy.objective, CandidateObjective::kBendBiased);
  EXPECT_EQ(policies[2].policy.bend_surcharge, 3U);
  ASSERT_EQ(policies[3].policy.resource_penalties.size(), 1U);
  EXPECT_EQ(policies[3].policy.resource_penalties.front().additional_cost, 5U);
  ASSERT_EQ(policies[4].policy.banned_resources.size(), 1U);
  EXPECT_EQ(policies[5].policy.orthogonal_step_surcharge, 4U);
}

TEST(CandidatePolicyTest, AlternativeScheduleRejectsMissingInvalidAndOverflowingInputs) {
  const CompiledBoard compiled = UnobstructedField();
  const std::vector<EdgeResourceKey> resources = LegalResources(compiled);
  ASSERT_FALSE(resources.empty());

  DeterministicAlternativePolicySchedule missing_resources;
  missing_resources.candidate_count = 2;
  EXPECT_EQ(BatchError(BuildDeterministicAlternativePolicies(compiled, CandidateGenerationPolicy{},
                                                             missing_resources))
                .code,
            CandidatePolicyErrorCode::kInvalidAlternativeSchedule);

  DeterministicAlternativePolicySchedule invalid_resource{
      .candidate_count = 2,
      .step_surcharge_increment = 1,
      .bend_surcharge_increment = 1,
      .resource_penalty_increment = 1,
      .alternative_resources = {EdgeResourceKey{
          .layer = 999,
          .lattice_x = 0,
          .lattice_y = 0,
          .direction = Direction::kEast,
      }},
  };
  EXPECT_EQ(BatchError(BuildDeterministicAlternativePolicies(compiled, CandidateGenerationPolicy{},
                                                             invalid_resource))
                .code,
            CandidatePolicyErrorCode::kInvalidResource);

  CandidateGenerationPolicy ordinal_overflow;
  ordinal_overflow.candidate_ordinal = std::numeric_limits<std::uint32_t>::max();
  DeterministicAlternativePolicySchedule two{
      .candidate_count = 2,
      .step_surcharge_increment = 1,
      .bend_surcharge_increment = 1,
      .resource_penalty_increment = 1,
      .alternative_resources = {resources.front()},
  };
  EXPECT_EQ(BatchError(BuildDeterministicAlternativePolicies(compiled, ordinal_overflow, two)).code,
            CandidatePolicyErrorCode::kInvalidAlternativeSchedule);
}

}  // namespace
}  // namespace apgar::routing
