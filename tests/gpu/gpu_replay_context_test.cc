#include "tools/gpu_replay_context.h"

#include <string>
#include <variant>

#include "tests/support/board_builder.h"
#include "tests/support/compiler_builder.h"
#include "tests/support/google_test.h"
#include "tests/support/routing_builder.h"

namespace apgar::tooling {
namespace {

TEST(GpuReplayContextTest, KeepsV1ReplayDefaultOnlyBeforeContextIdentityLands) {
  const board_ir::BoardSnapshot board = test_support::Snapshot(test_support::MultiNetM1BoardData());
  const geometry_compiler::CompilerProfile compiler_profile =
      test_support::DefaultCompilerProfile({0});
  const geometry_compiler::CompiledBoard default_compiled =
      test_support::Compile(board, compiler_profile);
  EXPECT_TRUE(std::holds_alternative<gpu::DeviceCompiledBoardV1>(
      PrepareDefaultGpuReplayDeviceContextV1(board, default_compiled)));

  const geometry_compiler::CompiledBoard prepared =
      test_support::CompilePreparedNet(board, board.data().nets[1].ref, compiler_profile);
  const GpuReplayDeviceContextV1 rejected = PrepareDefaultGpuReplayDeviceContextV1(board, prepared);
  ASSERT_TRUE(std::holds_alternative<std::string>(rejected));
  EXPECT_EQ(std::get<std::string>(rejected),
            "replay requires the Board IR default routing context");
}

}  // namespace
}  // namespace apgar::tooling
