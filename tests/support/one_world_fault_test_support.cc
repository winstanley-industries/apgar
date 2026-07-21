#include "tests/support/one_world_fault_test_support.h"

#include <new>
#include <stdexcept>

#include "src/allocator/one_world_internal.h"

namespace apgar::test_support {

allocator::OneWorldAllocationResult ExerciseOneWorldFailureEnvelope(OneWorldFault fault) {
  return allocator::internal::RunWithAllocationFailureEnvelope(
      [fault]() -> allocator::OneWorldAllocationResult {
        if (fault == OneWorldFault::kBadAlloc) {
          throw std::bad_alloc();
        }
        throw std::length_error("");
      });
}

}  // namespace apgar::test_support
