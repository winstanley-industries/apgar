#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <ranges>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "apgar/benchmark/phase4_representative_corpus.h"
#include "apgar/benchmark/phase4_trial_harness.h"
#include "apgar/board_ir/stable_hash.h"
#include "src/allocator/negotiated_prices_internal.h"
#include "src/benchmark/phase4_paired_trial_internal.h"

namespace {

using apgar::benchmark::Phase4CaseDescriptor;

constexpr std::size_t kMaximumImportedFixtureBytes = 1U << 20;

struct CaseRow {
  const Phase4CaseDescriptor* descriptor = nullptr;
  std::string build_status;
  std::uint64_t case_checksum = 0;
  std::uint64_t board_content_hash = 0;
  std::uint64_t workload_checksum = 0;
  std::uint64_t capacity_model_checksum = 0;
  std::uint64_t required_compiled_nodes = 0;
  std::uint64_t required_compiled_host_bytes = 0;
  std::uint64_t required_active_regions = 0;
  std::uint64_t required_board_entities = 0;
  std::uint64_t roster_checksum = 0;
};

struct BudgetRow {
  std::uint32_t case_id = 0;
  std::vector<std::pair<std::uint32_t, std::uint64_t>> pool_checksums;
};

[[nodiscard]] bool IsCanonicalPool(std::uint32_t pool) {
  return pool == 4 || pool == 8 || pool == 16;
}

[[nodiscard]] bool IncludedInPairedManifest(const Phase4CaseDescriptor& descriptor) {
  for (std::uint8_t index = 0; index < descriptor.requested_pool_size_count; ++index) {
    if (!IsCanonicalPool(descriptor.requested_pool_sizes[index])) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] std::string ReadFile(const char* path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return {};
  }
  std::string contents(kMaximumImportedFixtureBytes + 1, '\0');
  input.read(contents.data(), static_cast<std::streamsize>(contents.size()));
  contents.resize(static_cast<std::size_t>(input.gcount()));
  if (input.bad() || contents.size() > kMaximumImportedFixtureBytes) {
    return {};
  }
  return contents;
}

[[nodiscard]] std::uint64_t CapacityChecksum(
    const apgar::allocator::ResourceCapacityModel& capacities) {
  return apgar::allocator::internal::ComputeResourceCapacityModelChecksumV1(
      apgar::allocator::internal::ResourceCapacityChecksumHeaderV1{
          .schema_version = capacities.schema_version(),
          .associations = capacities.associations(),
          .default_capacity_units = capacities.default_capacity_units(),
      },
      capacities.overrides());
}

[[nodiscard]] std::uint64_t ActiveRegionCount(const apgar::allocator::MultiNetWorkload& workload) {
  if (workload.nets().empty()) {
    return 0;
  }
  const auto& active_regions = workload.nets().front().compiled_board.profile().active_regions;
  if (!std::ranges::all_of(workload.nets(), [&active_regions](const auto& context) {
        return context.compiled_board.profile().active_regions == active_regions;
      })) {
    return 0;
  }
  return active_regions.size();
}

[[nodiscard]] std::uint64_t BoardEntityCount(const apgar::board_ir::BoardSnapshot& board) {
  const apgar::board_ir::BoardData& data = board.data();
  return data.layers.size() + data.nets.size() + data.terminals.size() + data.obstacles.size();
}

[[nodiscard]] std::uint64_t RosterChecksum(const CaseRow& row,
                                           const apgar::allocator::MultiNetWorkload& workload) {
  apgar::board_ir::StableHashBuilder hash;
  hash.AddString("APGAR-PHASE4-WORKLOAD-NET-ROSTER-V2");
  hash.AddU32(2);
  hash.AddU32(2);
  hash.AddU64(apgar::benchmark::Phase4RepresentativeCorpusChecksumV2());
  hash.AddU32(row.descriptor->case_id);
  hash.AddU64(apgar::benchmark::FingerprintPhase4CaseDescriptorV2(*row.descriptor));
  hash.AddU64(row.case_checksum);
  hash.AddU64(row.board_content_hash);
  hash.AddU64(row.workload_checksum);
  hash.AddU64(workload.nets().size());
  for (const apgar::allocator::PreparedNetRoutingContext& context : workload.nets()) {
    hash.AddU64(context.request.net.id);
    hash.AddU32(context.request.net.generation);
  }
  return hash.Finish();
}

void AppendCase(std::ostringstream& output, const CaseRow& row) {
  const Phase4CaseDescriptor& descriptor = *row.descriptor;
  output << "{\"case_id\":" << descriptor.case_id << ",\"requested_pool_sizes\":[";
  for (std::uint8_t index = 0; index < descriptor.requested_pool_size_count; ++index) {
    if (index != 0) {
      output << ',';
    }
    output << descriptor.requested_pool_sizes[index];
  }
  output << "],\"workload_net_count\":" << descriptor.requested_net_count
         << ",\"descriptor_fingerprint\":"
         << apgar::benchmark::FingerprintPhase4CaseDescriptorV2(descriptor)
         << ",\"build_status\":\"" << row.build_status
         << "\",\"case_checksum\":" << row.case_checksum
         << ",\"board_content_hash\":" << row.board_content_hash
         << ",\"workload_checksum\":" << row.workload_checksum
         << ",\"capacity_model_checksum\":" << row.capacity_model_checksum
         << ",\"required_compiled_nodes\":" << row.required_compiled_nodes
         << ",\"required_compiled_host_bytes\":" << row.required_compiled_host_bytes
         << ",\"required_active_regions\":" << row.required_active_regions
         << ",\"required_board_entities\":" << row.required_board_entities << '}';
}

void AppendBudget(std::ostringstream& output, const BudgetRow& row) {
  output << "{\"case_id\":" << row.case_id << ",\"pool_checksums\":[";
  for (std::size_t index = 0; index < row.pool_checksums.size(); ++index) {
    if (index != 0) {
      output << ',';
    }
    output << "{\"pool\":" << row.pool_checksums[index].first
           << ",\"checksum\":" << row.pool_checksums[index].second << '}';
  }
  output << "]}";
}

[[nodiscard]] std::string Payload(const std::vector<CaseRow>& cases,
                                  const std::vector<BudgetRow>& budgets) {
  std::ostringstream output;
  output << "{\"schema_version\":2,\"corpus_version\":2,\"corpus_checksum\":"
         << apgar::benchmark::Phase4RepresentativeCorpusChecksumV2() << ",\"cases\":[";
  for (std::size_t index = 0; index < cases.size(); ++index) {
    if (index != 0) {
      output << ',';
    }
    AppendCase(output, cases[index]);
  }
  output << "],\"canonical_algorithm_budgets\":[";
  for (std::size_t index = 0; index < budgets.size(); ++index) {
    if (index != 0) {
      output << ',';
    }
    AppendBudget(output, budgets[index]);
  }
  output << "]}";
  return output.str();
}

[[nodiscard]] std::uint64_t ManifestChecksum(std::string_view payload) {
  apgar::board_ir::StableHashBuilder hash;
  hash.AddString("APGAR-PHASE4-REPRESENTATIVE-MANIFEST-V2");
  hash.AddString(payload);
  return hash.Finish();
}

void AppendRosterRow(std::ostringstream& output, const CaseRow& row) {
  const Phase4CaseDescriptor& descriptor = *row.descriptor;
  output << "{\"schema_version\":2,\"corpus_version\":2,\"corpus_checksum\":"
         << apgar::benchmark::Phase4RepresentativeCorpusChecksumV2()
         << ",\"case_id\":" << descriptor.case_id << ",\"descriptor_fingerprint\":"
         << apgar::benchmark::FingerprintPhase4CaseDescriptorV2(descriptor)
         << ",\"case_checksum\":" << row.case_checksum
         << ",\"board_content_hash\":" << row.board_content_hash
         << ",\"workload_checksum\":" << row.workload_checksum
         << ",\"workload_net_count\":" << descriptor.requested_net_count
         << ",\"roster_checksum\":" << row.roster_checksum << '}';
}

void AppendExclusion(std::ostringstream& output, const Phase4CaseDescriptor& descriptor,
                     std::string_view disposition) {
  output << "{\"case_id\":" << descriptor.case_id << ",\"descriptor_fingerprint\":"
         << apgar::benchmark::FingerprintPhase4CaseDescriptorV2(descriptor) << ",\"disposition\":\""
         << disposition << "\"}";
}

[[nodiscard]] std::string RosterPayload(const std::vector<CaseRow>& cases,
                                        std::uint64_t representative_manifest_checksum) {
  std::ostringstream output;
  output << "{\"schema_version\":2,\"corpus_version\":2,\"corpus_checksum\":"
         << apgar::benchmark::Phase4RepresentativeCorpusChecksumV2()
         << ",\"representative_manifest_checksum\":" << representative_manifest_checksum
         << ",\"successful_cases\":[";
  bool first = true;
  for (const CaseRow& row : cases) {
    if (row.build_status != "success") {
      continue;
    }
    if (!first) {
      output << ',';
    }
    first = false;
    AppendRosterRow(output, row);
  }
  output << "],\"excluded_cases\":[";
  first = true;
  for (const Phase4CaseDescriptor& descriptor : apgar::benchmark::Phase4CaseDescriptorsV2()) {
    std::string_view disposition;
    if (!IncludedInPairedManifest(descriptor)) {
      disposition = "descriptor_only_unsupported_pool";
    } else {
      const auto found = std::ranges::find(
          cases, descriptor.case_id, [](const CaseRow& row) { return row.descriptor->case_id; });
      if (found != cases.end() && found->build_status == "compiled_work_bound") {
        disposition = "compiled_work_bound";
      }
    }
    if (disposition.empty()) {
      continue;
    }
    if (!first) {
      output << ',';
    }
    first = false;
    AppendExclusion(output, descriptor, disposition);
  }
  output << "]}";
  return output.str();
}

[[nodiscard]] std::uint64_t RosterManifestChecksum(std::string_view payload) {
  apgar::board_ir::StableHashBuilder hash;
  hash.AddString("APGAR-PHASE4-WORKLOAD-NET-ROSTER-MANIFEST-V2");
  hash.AddString(payload);
  return hash.Finish();
}

[[nodiscard]] apgar::benchmark::Phase4CanonicalCellConfig Cell(std::uint32_t case_id,
                                                               std::uint32_t pool) {
  apgar::benchmark::Phase4CanonicalCellConfig cell;
  cell.case_id = case_id;
  cell.requested_pool_size = pool;
  cell.preparation_worker_count = apgar::benchmark::kPhase4CanonicalPreparationWorkersV1;
  cell.repetitions = apgar::benchmark::kPhase4CanonicalRepetitionsV1;
  cell.maximum_setup_elapsed_nanoseconds = 300'000'000'000ULL;
  cell.external_budget = {
      .maximum_prepared_elapsed_nanoseconds = 300'000'000'000ULL,
      .maximum_cold_elapsed_nanoseconds = 300'000'000'000ULL,
      .maximum_address_space_bytes = 64ULL * 1024ULL * 1024ULL * 1024ULL,
      .maximum_peak_host_bytes = 16ULL * 1024ULL * 1024ULL * 1024ULL,
  };
  return cell;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: phase4_v2_representative_manifest IMPORTED_FIXTURE\n";
    return 2;
  }
  const std::string imported_fixture = ReadFile(argv[1]);
  if (imported_fixture.empty()) {
    std::cerr << "cannot read nonempty imported fixture\n";
    return 2;
  }

  std::vector<CaseRow> cases;
  std::vector<BudgetRow> budgets;
  for (const Phase4CaseDescriptor& descriptor : apgar::benchmark::Phase4CaseDescriptorsV2()) {
    if (!IncludedInPairedManifest(descriptor)) {
      continue;
    }
    const std::string_view fixture =
        descriptor.source == apgar::benchmark::Phase4CaseSource::kImportedFixture
            ? std::string_view(imported_fixture)
            : std::string_view{};
    apgar::benchmark::Phase4RepresentativeCaseResult built =
        apgar::benchmark::BuildPhase4RepresentativeCaseV2(descriptor.case_id, fixture);
    CaseRow row{
        .descriptor = &descriptor,
        .build_status = {},
        .case_checksum = 0,
        .board_content_hash = 0,
        .workload_checksum = 0,
        .capacity_model_checksum = 0,
        .required_compiled_nodes = 0,
        .required_compiled_host_bytes = 0,
        .required_active_regions = 0,
        .required_board_entities = 0,
        .roster_checksum = 0,
    };
    if (std::holds_alternative<apgar::benchmark::Phase4RepresentativeCase>(built)) {
      const auto& value = std::get<apgar::benchmark::Phase4RepresentativeCase>(built);
      row.build_status = "success";
      row.case_checksum = value.case_checksum;
      row.board_content_hash = value.board.content_hash();
      row.workload_checksum = value.workload.workload_checksum();
      row.capacity_model_checksum = CapacityChecksum(value.capacities);
      row.required_compiled_nodes = value.workload.compiled_node_count();
      row.required_compiled_host_bytes = value.workload.compiled_host_bytes();
      row.required_active_regions = ActiveRegionCount(value.workload);
      row.required_board_entities = BoardEntityCount(value.board);
      if (row.required_active_regions == 0) {
        std::cerr << descriptor.case_id
                  << " does not have one nonempty common compiler active-region roster\n";
        return 1;
      }
      row.roster_checksum = RosterChecksum(row, value.workload);
    } else {
      const auto& error = std::get<apgar::benchmark::Phase4RepresentativeCorpusError>(built);
      if (error.code != apgar::benchmark::Phase4RepresentativeCorpusErrorCode::kWorkBoundExceeded) {
        std::cerr << descriptor.case_id << ' ' << error.invariant_id << ": " << error.detail
                  << '\n';
        return 1;
      }
      row.build_status = "compiled_work_bound";
      row.required_compiled_nodes = error.required_compiled_nodes;
      row.required_compiled_host_bytes = error.required_compiled_host_bytes;
    }
    cases.push_back(row);

    BudgetRow budget{.case_id = descriptor.case_id, .pool_checksums = {}};
    for (std::uint8_t index = 0; index < descriptor.requested_pool_size_count; ++index) {
      const std::uint32_t pool = descriptor.requested_pool_sizes[index];
      auto spec = apgar::benchmark::BuildPhase4FrozenCanonicalBudgetPreimageForCorpusV2(
          Cell(descriptor.case_id, pool), 0, apgar::benchmark::Phase4TrialOrder::kBaselineFirst);
      if (!std::holds_alternative<apgar::benchmark::Phase4PairedTrialSpec>(spec)) {
        const auto& error = std::get<apgar::benchmark::Phase4TrialHarnessError>(spec);
        std::cerr << descriptor.case_id << ':' << pool << ' ' << error.invariant_id << ": "
                  << error.detail << '\n';
        return 1;
      }
      budget.pool_checksums.emplace_back(
          pool, apgar::benchmark::internal::ComputePhase4CanonicalAlgorithmBudgetChecksumV1(
                    std::get<apgar::benchmark::Phase4PairedTrialSpec>(spec)));
    }
    budgets.push_back(std::move(budget));
  }

  if (cases.size() != 40 || budgets.size() != 40) {
    std::cerr << "V2 representative manifest did not produce 40 paired case rows\n";
    return 1;
  }
  const std::string payload = Payload(cases, budgets);
  const std::uint64_t representative_manifest_checksum = ManifestChecksum(payload);
  const std::string prefix =
      "{\"schema_version\":2,\"corpus_version\":2,\"corpus_checksum\":" +
      std::to_string(apgar::benchmark::Phase4RepresentativeCorpusChecksumV2()) +
      ",\"manifest_checksum\":" + std::to_string(representative_manifest_checksum) + ',';
  const std::string payload_prefix =
      "{\"schema_version\":2,\"corpus_version\":2,\"corpus_checksum\":" +
      std::to_string(apgar::benchmark::Phase4RepresentativeCorpusChecksumV2()) + ',';
  if (!payload.starts_with(payload_prefix)) {
    std::cerr << "internal manifest prefix mismatch\n";
    return 1;
  }
  const std::string roster_payload = RosterPayload(cases, representative_manifest_checksum);
  const std::string roster_prefix =
      "{\"schema_version\":2,\"corpus_version\":2,\"corpus_checksum\":" +
      std::to_string(apgar::benchmark::Phase4RepresentativeCorpusChecksumV2()) +
      ",\"representative_manifest_checksum\":" + std::to_string(representative_manifest_checksum) +
      ",\"manifest_checksum\":" + std::to_string(RosterManifestChecksum(roster_payload)) + ',';
  const std::string roster_payload_prefix =
      "{\"schema_version\":2,\"corpus_version\":2,\"corpus_checksum\":" +
      std::to_string(apgar::benchmark::Phase4RepresentativeCorpusChecksumV2()) +
      ",\"representative_manifest_checksum\":" + std::to_string(representative_manifest_checksum) +
      ',';
  if (!roster_payload.starts_with(roster_payload_prefix)) {
    std::cerr << "internal roster-manifest prefix mismatch\n";
    return 1;
  }
  std::cout << prefix << payload.substr(payload_prefix.size()) << '\n'
            << roster_prefix << roster_payload.substr(roster_payload_prefix.size()) << '\n';
  return std::cout.good() ? 0 : 1;
}
