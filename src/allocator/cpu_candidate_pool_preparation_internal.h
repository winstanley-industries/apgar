#ifndef APGAR_SRC_ALLOCATOR_CPU_CANDIDATE_POOL_PREPARATION_INTERNAL_H_
#define APGAR_SRC_ALLOCATOR_CPU_CANDIDATE_POOL_PREPARATION_INTERNAL_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "apgar/allocator/cpu_candidate_pool_preparation.h"

namespace apgar::allocator::internal {

struct CpuCandidatePoolChecksumCandidateV1 {
  candidates::CandidateId id;
  std::uint64_t payload_checksum = 0;
};

struct CpuCandidatePoolChecksumPoolV1 {
  board_ir::EntityRef net;
  std::span<const CpuCandidatePoolChecksumCandidateV1> candidates;
};

using CpuCandidatePoolAlternativeResourceResultV1 =
    std::variant<std::vector<routing::EdgeResourceKey>, CpuCandidatePoolPreparationError>;

[[nodiscard]] CpuCandidatePoolAlternativeResourceResultV1
ExtractCpuCandidatePoolAlternativeResourcesV1(const routing::CpuRoute& route,
                                              const geometry_compiler::CompiledBoard& board,
                                              const routing::PlanarRouteRequest& expected_request,
                                              std::uint64_t expected_policy_identity,
                                              std::uint64_t maximum_edges);

[[nodiscard]] std::uint64_t ComputeCpuCandidatePoolNetSeedV1(std::uint64_t configured_seed,
                                                             std::uint64_t workload_checksum,
                                                             board_ir::EntityRef net) noexcept;

[[nodiscard]] std::uint64_t ComputeCpuCandidatePoolBatchIdentityV2(
    std::uint64_t board_content_hash, std::uint64_t workload_checksum,
    const CpuCandidatePoolPreparationConfig& config) noexcept;

[[nodiscard]] std::uint64_t ComputeCpuCandidatePoolPreparationChecksumV2(
    const CpuCandidatePoolPreparationConfig& config, std::uint64_t batch_identity,
    const CpuCandidatePoolPreparationCounters& counters,
    std::span<const CpuCandidatePoolColumnRecord> columns,
    std::span<const CpuCandidatePoolChecksumPoolV1> pools) noexcept;

// Rebuilds the production checksum from the retained live pool handles and
// rejects structurally invalid null handles before dereferencing them.
[[nodiscard]] std::optional<std::uint64_t> RecomputeCpuCandidatePoolPreparationChecksumV2(
    const PreparedCpuCandidatePools& preparation);

[[nodiscard]] std::uint64_t ComputeCpuCandidatePoolFailedPreparationChecksumV2(
    std::uint64_t board_content_hash, std::uint64_t workload_checksum,
    const CpuCandidatePoolPreparationConfig& config, std::uint64_t batch_identity,
    CpuCandidatePoolPreparationErrorCode error_code, bool candidate_store_publication_committed,
    const CpuCandidatePoolFailedPreparationCounters& counters,
    std::span<const CpuCandidatePoolAttemptedColumnRecord> attempted_columns) noexcept;

enum class CpuCandidatePoolPreparationFaultForTesting : std::uint8_t {
  kNone = 0,
  kThreadCreationBadAlloc = 1,
  kThreadCreationSystemError = 2,
  kWorkerBadAlloc = 3,
  kWorkerLengthError = 4,
  kWorkerUnexpectedException = 5,
  kBlockWorker = 6,
  kPostBaseWaveBadAlloc = 7,
  kPostPublicationBadAlloc = 8,
  kPostBaseWaveUnexpectedException = 9,
  kPostPublicationUnexpectedException = 10,
};

// Defined only by the fault-test variant.
void SetCpuCandidatePoolPreparationFaultForTesting(CpuCandidatePoolPreparationFaultForTesting fault,
                                                   std::size_t index) noexcept;
void WaitForCpuCandidatePoolPreparationWorkerBlockForTesting();
void ReleaseCpuCandidatePoolPreparationWorkerBlockForTesting() noexcept;

}  // namespace apgar::allocator::internal

#endif  // APGAR_SRC_ALLOCATOR_CPU_CANDIDATE_POOL_PREPARATION_INTERNAL_H_
