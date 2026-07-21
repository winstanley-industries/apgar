#include "apgar/benchmark/phase4_corpus.h"

#include <array>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "apgar/adapters/kicad_fixture.h"
#include "apgar/board_ir/stable_hash.h"
#include "apgar/geometry_compiler/compiled_board.h"
#include "src/benchmark/phase4_corpus_internal.h"

namespace apgar::benchmark {
namespace {

using board_ir::AxisAlignedBox64;
using board_ir::BoardSnapshot;
using board_ir::EntityRef;
using board_ir::Point64;
using geometry_compiler::ActiveRegion;
using geometry_compiler::CompilerProfile;
using geometry_compiler::DeterministicCosts;

constexpr board_ir::DbCoord kCorpusLatticeStep = 500'000;
constexpr std::uint64_t kCorpusCompiledNodeLimit = 13'122;
constexpr std::uint64_t kCorpusCompiledHostByteLimit = 64ULL * 1024ULL * 1024ULL;

#if defined(APGAR_PHASE4_CORPUS_FAULT_TEST_VARIANT)
thread_local bool g_fail_next_phase4_corpus_build = false;
#endif

void MaybeFailPhase4CorpusBuildForTesting() {
#if defined(APGAR_PHASE4_CORPUS_FAULT_TEST_VARIANT)
  if (g_fail_next_phase4_corpus_build) {
    g_fail_next_phase4_corpus_build = false;
    throw std::bad_alloc();
  }
#endif
}

[[nodiscard]] Phase4CorpusError Error(Phase4CorpusErrorCode code, std::string detail) {
  return Phase4CorpusError{.code = code, .detail = std::move(detail)};
}

[[nodiscard]] Phase4CorpusError ResourceError(std::string_view invariant_id) noexcept {
  return Phase4CorpusError{.code = Phase4CorpusErrorCode::kResourceExhausted,
                           .detail = {},
                           .invariant_id = invariant_id};
}

[[nodiscard]] const board_ir::Net* FindNetByName(const BoardSnapshot& board,
                                                 std::string_view name) {
  const auto net = std::ranges::find(board.data().nets, name, &board_ir::Net::name);
  return net == board.data().nets.end() ? nullptr : &*net;
}

[[nodiscard]] CompilerProfile CorpusCompilerProfile() {
  const AxisAlignedBox64 bounds{
      .min = Point64{.x = 0, .y = 0},
      .max = Point64{.x = 40'000'000, .y = 40'000'000},
  };
  return CompilerProfile{
      .schema_version = geometry_compiler::kCompilerProfileSchemaVersion,
      .lattice_origin = Point64{.x = 0, .y = 0},
      .lattice_step = kCorpusLatticeStep,
      .tile_width_nodes = 8,
      .tile_height_nodes = 8,
      .compilation_roi = bounds,
      .active_regions = {ActiveRegion{.layer = 0, .bounds = bounds}},
      .heading_mask = board_ir::kM1HeadingMask,
      .costs =
          DeterministicCosts{
              .orthogonal_step = 1000,
              .diagonal_step = 1414,
              .bend = 100,
          },
  };
}

}  // namespace

Phase4ImportedMultiNetCorpusResult BuildPhase4ImportedMultiNetCorpusV1(
    std::string_view kicad_fixture) {
  try {
    if (kicad_fixture.size() != kPhase4ImportedMultiNetFixtureBytesV1 ||
        board_ir::StableHashString(kicad_fixture) != kPhase4ImportedMultiNetFixtureFnv1a64V1) {
      return Error(Phase4CorpusErrorCode::kFixtureIdentityMismatch,
                   "Phase 4 imported corpus v1 fixture identity does not match the pinned source");
    }
    MaybeFailPhase4CorpusBuildForTesting();
    adapters::KicadFixtureImportResult imported = adapters::ImportKicadMultiNetFixture(
        kicad_fixture, adapters::KicadMultiNetFixtureImportConfig{
                           .routable_net_names = {"ROUTE_A", "ROUTE_B"},
                           .default_routing_net_name = "ROUTE_A",
                           .nominal_width = 500'000,
                           .clearance = 500'000,
                       });
    if (const auto* import_error = std::get_if<adapters::KicadFixtureError>(&imported);
        import_error != nullptr) {
      if (import_error->code == adapters::KicadFixtureErrorCode::kResourceLimit) {
        return ResourceError(import_error->invariant_id.empty()
                                 ? std::string_view("benchmark.phase4_corpus.fixture_resource.v1")
                                 : import_error->invariant_id);
      }
      return Error(Phase4CorpusErrorCode::kFixtureImportFailed,
                   "Phase 4 imported fixture failed: " + import_error->message);
    }
    BoardSnapshot board = std::get<BoardSnapshot>(std::move(imported));
    const board_ir::Net* route_a = FindNetByName(board, "ROUTE_A");
    const board_ir::Net* route_b = FindNetByName(board, "ROUTE_B");
    if (route_a == nullptr || route_b == nullptr || route_a->ref == route_b->ref) {
      return Error(Phase4CorpusErrorCode::kMissingRosterNet,
                   "Phase 4 imported fixture does not contain its exact distinct net roster");
    }

    std::array<allocator::MultiNetRoutingSpec, 2> specs;
    specs[0] = allocator::MultiNetRoutingSpec{
        .routing_profile = board.data().routing_profile,
        .start_layer = 0,
        .goal_layer = 0,
    };
    specs[0].routing_profile.net = route_a->ref;
    specs[1] = allocator::MultiNetRoutingSpec{
        .routing_profile = board.data().routing_profile,
        .start_layer = 0,
        .goal_layer = 0,
    };
    specs[1].routing_profile.net = route_b->ref;

    const allocator::MultiNetWorkloadLimits limits{
        .maximum_nets = specs.size(),
        .maximum_compiled_nodes = kCorpusCompiledNodeLimit,
        .maximum_compiled_host_bytes = kCorpusCompiledHostByteLimit,
    };
    allocator::MultiNetWorkloadResult workload_result = allocator::BuildMultiNetWorkload(
        allocator::kMultiNetWorkloadSchemaVersion, board, CorpusCompilerProfile(), specs, limits);
    if (const auto* workload_error =
            std::get_if<allocator::MultiNetWorkloadError>(&workload_result);
        workload_error != nullptr) {
      if (workload_error->code == allocator::MultiNetWorkloadErrorCode::kResourceExhausted) {
        return ResourceError(workload_error->invariant_id);
      }
      return Error(Phase4CorpusErrorCode::kWorkloadBuildFailed,
                   "Phase 4 imported workload failed: " + std::string(workload_error->detail));
    }
    return Phase4ImportedMultiNetCorpus{
        .board = std::move(board),
        .workload = std::get<allocator::MultiNetWorkload>(std::move(workload_result)),
    };
  } catch (const std::bad_alloc&) {
    return ResourceError("benchmark.phase4_corpus.host_memory.v1");
  } catch (const std::length_error&) {
    return ResourceError("benchmark.phase4_corpus.host_container.v1");
  }
}

namespace internal {

void FailNextPhase4CorpusBuildForTesting() noexcept {
#if defined(APGAR_PHASE4_CORPUS_FAULT_TEST_VARIANT)
  g_fail_next_phase4_corpus_build = true;
#endif
}

}  // namespace internal

}  // namespace apgar::benchmark
