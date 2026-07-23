#ifndef APGAR_BENCHMARK_PHASE4_OPERATIONAL_TYPES_H_
#define APGAR_BENCHMARK_PHASE4_OPERATIONAL_TYPES_H_

#include <cstdint>

namespace apgar::benchmark {

enum class Phase4OperationalMeasurementStatus : std::uint8_t {
  kMeasured = 0,
  kNotApplicable = 1,
  kRequiresParentProcessAuthority = 2,
};

enum class Phase4OperationalMeasurementReason : std::uint8_t {
  kNone = 0,
  kCpuDirectJobsNotCompatibilityBatches = 1,
  kCpuExecutionHasNoCompactDeviceReadback = 2,
  kPrecompiledCaseOwnedViewsHaveNoRuntimePreparedViewCache = 3,
  kCpuExecutionHasNoDeviceUpload = 4,
  kCpuExecutionHasNoGpuDispatch = 5,
  kCpuExecutionHasNoDeviceMemory = 6,
  kSyntheticCaseHasNoFixtureImport = 7,
  kImportedCaseHasNoSyntheticMaterialization = 8,
  kImportedWorkloadHasNoSeparateSyntheticCompileProbe = 9,
  kProcessCpuRequiresIsolatedParentWait4 = 10,
  kPeakHostMemoryRequiresIsolatedParentWait4 = 11,
};

struct Phase4OperationalApplicabilityV1 {
  Phase4OperationalMeasurementStatus status = Phase4OperationalMeasurementStatus::kNotApplicable;
  Phase4OperationalMeasurementReason reason =
      Phase4OperationalMeasurementReason::kCpuExecutionHasNoGpuDispatch;

  friend bool operator==(const Phase4OperationalApplicabilityV1&,
                         const Phase4OperationalApplicabilityV1&) = default;
};

}  // namespace apgar::benchmark

#endif  // APGAR_BENCHMARK_PHASE4_OPERATIONAL_TYPES_H_
