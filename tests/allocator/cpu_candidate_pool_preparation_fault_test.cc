#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <future>
#include <memory>
#include <utility>
#include <variant>

#include "apgar/allocator/cpu_candidate_pool_preparation.h"
#include "apgar/allocator/multi_net_workload.h"
#include "src/allocator/cpu_candidate_pool_preparation_internal.h"
#include "tests/support/board_builder.h"
#include "tests/support/compiler_builder.h"
#include "tests/support/google_test.h"

namespace apgar::allocator {
namespace {

struct Fixture {
  board_ir::BoardSnapshot board;
  MultiNetWorkload workload;
};

[[nodiscard]] board_ir::BoardSnapshot Snapshot(board_ir::BoardData data) {
  board_ir::BoardCreationResult result = board_ir::CreateBoardSnapshot(std::move(data));
  EXPECT_TRUE(std::holds_alternative<board_ir::BoardSnapshot>(result));
  if (!std::holds_alternative<board_ir::BoardSnapshot>(result)) {
    std::abort();
  }
  return std::get<board_ir::BoardSnapshot>(std::move(result));
}

[[nodiscard]] Fixture MakeFixture() {
  board_ir::BoardData data = test_support::ValidM1BoardData();
  data.obstacles.clear();
  board_ir::BoardSnapshot board = Snapshot(std::move(data));
  const std::array specs = {MultiNetRoutingSpec{
      .routing_profile = board.data().routing_profile,
      .start_layer = 0,
      .goal_layer = 0,
  }};
  MultiNetWorkloadResult workload_result =
      BuildMultiNetWorkload(kMultiNetWorkloadSchemaVersion, board,
                            test_support::DefaultCompilerProfile({0}), specs, specs.size());
  EXPECT_TRUE(std::holds_alternative<MultiNetWorkload>(workload_result));
  if (!std::holds_alternative<MultiNetWorkload>(workload_result)) {
    std::abort();
  }
  return Fixture{
      .board = std::move(board),
      .workload = std::get<MultiNetWorkload>(std::move(workload_result)),
  };
}

[[nodiscard]] CpuCandidatePoolPreparationConfig Config() {
  CpuCandidatePoolPreparationConfig config;
  config.store_config = candidates::CandidateStoreConfig{
      .maximum_candidates_per_net = 4,
      .maximum_candidate_bytes_per_net = 8U * 1024U * 1024U,
      .maximum_rejection_records = 4,
      .maximum_rejection_items_per_transaction = 4,
      .maximum_admission_items_per_transaction = 4,
      .maximum_admission_input_bytes_per_transaction = 64U * 1024U * 1024U,
      .maximum_admission_work_units_per_transaction = 100'000'000,
      .maximum_pin_lease_items_per_transaction = 4,
      .maximum_expected_pools_per_invocation = 1,
      .maximum_expected_candidates_per_invocation = 4,
  };
  return config;
}

[[nodiscard]] std::unique_ptr<PersistentCpuCandidatePoolPreparer> Preparer(std::uint32_t workers) {
  PersistentCpuCandidatePoolPreparerResult result =
      CreatePersistentCpuCandidatePoolPreparer({.worker_count = workers});
  EXPECT_TRUE(std::holds_alternative<std::unique_ptr<PersistentCpuCandidatePoolPreparer>>(result));
  if (!std::holds_alternative<std::unique_ptr<PersistentCpuCandidatePoolPreparer>>(result)) {
    std::abort();
  }
  return std::get<std::unique_ptr<PersistentCpuCandidatePoolPreparer>>(std::move(result));
}

TEST(CpuCandidatePoolPreparationFaultTest, BusyCallDispatchesNoSecondWork) {
  const Fixture fixture = MakeFixture();
  const CpuCandidatePoolPreparationConfig config = Config();
  std::unique_ptr<PersistentCpuCandidatePoolPreparer> preparer = Preparer(2);
  internal::SetCpuCandidatePoolPreparationFaultForTesting(
      internal::CpuCandidatePoolPreparationFaultForTesting::kBlockWorker, 0);
  auto active = std::async(std::launch::async, [&] {
    return PrepareInitialCpuCandidatePools(*preparer, fixture.board, fixture.workload, config);
  });
  internal::WaitForCpuCandidatePoolPreparationWorkerBlockForTesting();
  struct BlockRelease {
    bool active = true;
    ~BlockRelease() {
      if (active) {
        internal::ReleaseCpuCandidatePoolPreparationWorkerBlockForTesting();
      }
    }
    void Release() noexcept {
      if (active) {
        internal::ReleaseCpuCandidatePoolPreparationWorkerBlockForTesting();
        active = false;
      }
    }
  } release;
  const std::uint64_t dispatched = preparer->telemetry().jobs_dispatched;

  auto busy_call = std::async(std::launch::async, [&] {
    return PrepareInitialCpuCandidatePools(*preparer, fixture.board, fixture.workload, config);
  });
  const std::future_status busy_status = busy_call.wait_for(std::chrono::seconds(1));
  const std::uint64_t dispatched_after_busy_attempt = preparer->telemetry().jobs_dispatched;
  release.Release();
  const PreparedCpuCandidatePoolsResult busy = busy_call.get();
  const PreparedCpuCandidatePoolsResult completed = active.get();

  ASSERT_EQ(busy_status, std::future_status::ready);
  ASSERT_TRUE(std::holds_alternative<CpuCandidatePoolPreparationError>(busy));
  EXPECT_EQ(std::get<CpuCandidatePoolPreparationError>(busy).code,
            CpuCandidatePoolPreparationErrorCode::kBusy);
  EXPECT_EQ(dispatched_after_busy_attempt, dispatched);
  EXPECT_TRUE(std::holds_alternative<PreparedCpuCandidatePools>(completed));
}

TEST(CpuCandidatePoolPreparationFaultTest, PartialThreadCreationFailureIsTypedAndJoins) {
  for (const auto fault : {
           internal::CpuCandidatePoolPreparationFaultForTesting::kThreadCreationBadAlloc,
           internal::CpuCandidatePoolPreparationFaultForTesting::kThreadCreationSystemError,
       }) {
    internal::SetCpuCandidatePoolPreparationFaultForTesting(fault, 1);
    const PersistentCpuCandidatePoolPreparerResult result =
        CreatePersistentCpuCandidatePoolPreparer({.worker_count = 4});
    ASSERT_TRUE(std::holds_alternative<CpuCandidatePoolPreparationError>(result));
    EXPECT_EQ(std::get<CpuCandidatePoolPreparationError>(result).code,
              CpuCandidatePoolPreparationErrorCode::kResourceExhausted);
  }
  internal::SetCpuCandidatePoolPreparationFaultForTesting(
      internal::CpuCandidatePoolPreparationFaultForTesting::kNone, 0);
  EXPECT_NE(Preparer(2), nullptr);
}

TEST(CpuCandidatePoolPreparationFaultTest, WorkerExceptionsAreTypedAndPreparerRecovers) {
  const Fixture fixture = MakeFixture();
  const CpuCandidatePoolPreparationConfig config = Config();
  std::unique_ptr<PersistentCpuCandidatePoolPreparer> preparer = Preparer(2);
  for (const auto fault : {
           internal::CpuCandidatePoolPreparationFaultForTesting::kWorkerBadAlloc,
           internal::CpuCandidatePoolPreparationFaultForTesting::kWorkerLengthError,
           internal::CpuCandidatePoolPreparationFaultForTesting::kWorkerUnexpectedException,
       }) {
    internal::SetCpuCandidatePoolPreparationFaultForTesting(fault, 0);
    const PreparedCpuCandidatePoolsResult failed =
        PrepareInitialCpuCandidatePools(*preparer, fixture.board, fixture.workload, config);
    ASSERT_TRUE(std::holds_alternative<CpuCandidatePoolPreparationError>(failed));
    const CpuCandidatePoolPreparationErrorCode expected =
        fault == internal::CpuCandidatePoolPreparationFaultForTesting::kWorkerUnexpectedException
            ? CpuCandidatePoolPreparationErrorCode::kInternalInvariant
            : CpuCandidatePoolPreparationErrorCode::kResourceExhausted;
    EXPECT_EQ(std::get<CpuCandidatePoolPreparationError>(failed).code, expected);
  }

  internal::SetCpuCandidatePoolPreparationFaultForTesting(
      internal::CpuCandidatePoolPreparationFaultForTesting::kNone, 0);
  const PreparedCpuCandidatePoolsResult recovered =
      PrepareInitialCpuCandidatePools(*preparer, fixture.board, fixture.workload, config);
  EXPECT_TRUE(std::holds_alternative<PreparedCpuCandidatePools>(recovered));
  EXPECT_EQ(preparer->telemetry().invocations_started, 4U);
  EXPECT_EQ(preparer->telemetry().invocations_completed, 4U);
}

}  // namespace
}  // namespace apgar::allocator
