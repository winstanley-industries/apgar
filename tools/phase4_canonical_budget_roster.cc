#include <cstdint>
#include <iostream>
#include <variant>

#include "apgar/benchmark/phase4_representative_corpus.h"
#include "apgar/benchmark/phase4_trial_harness.h"
#include "src/benchmark/phase4_paired_trial_internal.h"

int main() {
  for (const apgar::benchmark::Phase4CaseDescriptor& descriptor :
       apgar::benchmark::Phase4CaseDescriptorsV1()) {
    for (std::uint8_t index = 0; index < descriptor.requested_pool_size_count; ++index) {
      const std::uint32_t requested_pool_size = descriptor.requested_pool_sizes[index];
      if (requested_pool_size != 4 && requested_pool_size != 8 && requested_pool_size != 16) {
        continue;
      }
      apgar::benchmark::Phase4CanonicalCellConfig cell;
      cell.case_id = descriptor.case_id;
      cell.requested_pool_size = requested_pool_size;
      cell.preparation_worker_count = apgar::benchmark::kPhase4CanonicalPreparationWorkersV1;
      cell.repetitions = apgar::benchmark::kPhase4CanonicalRepetitionsV1;
      cell.maximum_setup_elapsed_nanoseconds = 300'000'000'000ULL;
      cell.external_budget = {
          .maximum_prepared_elapsed_nanoseconds = 300'000'000'000ULL,
          .maximum_cold_elapsed_nanoseconds = 300'000'000'000ULL,
          .maximum_address_space_bytes = 64ULL * 1024ULL * 1024ULL * 1024ULL,
          .maximum_peak_host_bytes = 16ULL * 1024ULL * 1024ULL * 1024ULL,
      };
      auto built = apgar::benchmark::BuildPhase4CanonicalTrialSpecV1(
          cell, 0, apgar::benchmark::Phase4TrialOrder::kBaselineFirst);
      if (!std::holds_alternative<apgar::benchmark::Phase4PairedTrialSpec>(built)) {
        const auto& error = std::get<apgar::benchmark::Phase4TrialHarnessError>(built);
        std::cerr << error.invariant_id << ": " << error.detail << '\n';
        return 1;
      }
      std::cout << descriptor.case_id << ' ' << cell.requested_pool_size << ' '
                << apgar::benchmark::internal::ComputePhase4CanonicalAlgorithmBudgetChecksumV1(
                       std::get<apgar::benchmark::Phase4PairedTrialSpec>(built))
                << '\n';
    }
  }
  return std::cout.good() ? 0 : 1;
}
