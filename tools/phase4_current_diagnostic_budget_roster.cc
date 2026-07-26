#include <array>
#include <cstdint>
#include <iostream>
#include <variant>

#include "apgar/benchmark/phase4_trial_harness.h"
#include "src/benchmark/phase4_paired_trial_internal.h"

int main() {
  constexpr std::array<std::uint32_t, 5> kCaseIds = {100, 101, 102, 200, 4'000};
  for (const std::uint32_t case_id : kCaseIds) {
    apgar::benchmark::Phase4CanonicalCellConfig cell;
    cell.case_id = case_id;
    cell.requested_pool_size = 4;
    cell.preparation_worker_count = apgar::benchmark::kPhase4CanonicalPreparationWorkersV1;
    cell.repetitions = apgar::benchmark::kPhase4CanonicalRepetitionsV1;
    cell.maximum_setup_elapsed_nanoseconds = 60'000'000'000ULL;
    cell.external_budget = {
        .maximum_prepared_elapsed_nanoseconds = 60'000'000'000ULL,
        .maximum_cold_elapsed_nanoseconds = 120'000'000'000ULL,
        .maximum_address_space_bytes = 64ULL * 1024ULL * 1024ULL * 1024ULL,
        .maximum_peak_host_bytes = 16ULL * 1024ULL * 1024ULL * 1024ULL,
    };
    apgar::benchmark::Phase4CanonicalSpecResult built =
        apgar::benchmark::BuildPhase4CanonicalTrialSpecV1(
            cell, 0, apgar::benchmark::Phase4TrialOrder::kBaselineFirst);
    if (!std::holds_alternative<apgar::benchmark::Phase4PairedTrialSpec>(built)) {
      const auto& error = std::get<apgar::benchmark::Phase4TrialHarnessError>(built);
      std::cerr << case_id << ' ' << error.invariant_id << ": " << error.detail << '\n';
      return 1;
    }
    const apgar::benchmark::Phase4PairedTrialSpec& spec =
        std::get<apgar::benchmark::Phase4PairedTrialSpec>(built);
    std::cout << case_id << " 4 "
              << apgar::benchmark::internal::ComputePhase4CanonicalAlgorithmBudgetChecksumV1(spec)
              << '\n';
  }
  return std::cout.good() ? 0 : 1;
}
