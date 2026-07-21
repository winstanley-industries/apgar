#ifndef APGAR_TESTS_SUPPORT_ONE_WORLD_FAULT_TEST_SUPPORT_H_
#define APGAR_TESTS_SUPPORT_ONE_WORLD_FAULT_TEST_SUPPORT_H_

#include <cstdint>

#include "apgar/allocator/one_world.h"

namespace apgar::test_support {

enum class OneWorldFault : std::uint8_t {
  kBadAlloc = 0,
  kLengthError = 1,
};

[[nodiscard]] allocator::OneWorldAllocationResult ExerciseOneWorldFailureEnvelope(
    OneWorldFault fault);

}  // namespace apgar::test_support

#endif  // APGAR_TESTS_SUPPORT_ONE_WORLD_FAULT_TEST_SUPPORT_H_
