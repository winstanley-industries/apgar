#ifndef APGAR_ALLOCATOR_CPU_SEQUENTIAL_NEGOTIATED_ROUTING_H_
#define APGAR_ALLOCATOR_CPU_SEQUENTIAL_NEGOTIATED_ROUTING_H_

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <variant>
#include <vector>

#include "apgar/allocator/resource_accounting.h"
#include "apgar/board_ir/board.h"
#include "apgar/candidates/route_candidate.h"
#include "apgar/geometry_compiler/compiled_board.h"
#include "apgar/routing/candidate_policy.h"
#include "apgar/routing/cpu_astar.h"
#include "apgar/routing/planar_route.h"

namespace apgar::allocator {

inline constexpr std::uint64_t kMaximumCpuSequentialNets = 1'000'000;
inline constexpr std::uint32_t kMaximumCpuSequentialPasses = 1'000'000;
inline constexpr std::uint64_t kMaximumCpuSequentialAttempts = 1'000'000;
inline constexpr std::uint64_t kMaximumCpuSequentialDiagnostics = 1'000'000;
inline constexpr std::uint64_t kMaximumCpuSequentialCandidateBytes = 1ULL << 40U;
inline constexpr std::uint64_t kMaximumCpuSequentialGeneratedBytes = 1ULL << 44U;
inline constexpr std::uint64_t kMaximumCpuSequentialRetainedBytes = 1ULL << 44U;
inline constexpr std::uint64_t kMaximumCpuSequentialExpandedUses = 100'000'000;

// One requested net and its retained prepared compiler/request context. The
// CompiledBoard address is borrowed only for the call and is never part of an
// ordering, identity, diagnostic, or final outcome.
struct CpuSequentialNetRequest {
  const geometry_compiler::CompiledBoard* compiled_board = nullptr;
  routing::PlanarRouteRequest request;
};

// Canonical Sequential Rip-Up-and-Reroute v1 (CS-RR-v1). Pass zero starts
// with no routes, occupancy, or history. Before pass p > 0, every resource
// overused at the prior pass boundary receives
// historical_cost_increment * overuse_units. During pass p, an immutable
// per-query snapshot charges:
//
//   history[r] + present_factor(p) * max(0, usage[r] + 1 - capacity[r])
//
// where present_factor(p) is initial_present_cost plus p increments and usage
// excludes the net's removed incumbent. All terms are checked integers.
struct CanonicalSequentialRipUpAndReroutePolicyV1 {
  std::uint32_t maximum_passes = 8;
  std::uint64_t initial_present_cost = 0;
  std::uint64_t present_cost_increment = 100;
  std::uint64_t historical_cost_increment = 10;

  friend bool operator==(const CanonicalSequentialRipUpAndReroutePolicyV1&,
                         const CanonicalSequentialRipUpAndReroutePolicyV1&) = default;
};

struct CpuSequentialNegotiatedRoutingLimits {
  std::uint64_t maximum_nets = 64;
  std::uint64_t maximum_total_attempts = 1'024;
  // Total deterministic per-query CPU work: congestion-snapshot map entries,
  // zero-capacity lattice-direction probes, cost evaluations, and production
  // CPU A* telemetry. CPU A* receives exactly the remaining query budget.
  std::uint64_t maximum_cpu_work_units_per_query = 10'000'000;
  // Independent total bound enforced against actual cumulative query work.
  std::uint64_t maximum_aggregate_cpu_work_units = 10'240'000'000;
  std::uint64_t maximum_candidate_bytes_per_attempt = 1U * 1024U * 1024U;
  std::uint64_t maximum_aggregate_generated_candidate_bytes = 1U * 1024U * 1024U * 1024U;
  std::uint64_t maximum_retained_candidate_bytes = 64U * 1024U * 1024U;
  std::uint64_t maximum_expanded_resource_uses_per_candidate = 100'000;
  std::uint64_t maximum_aggregate_expanded_resource_uses = 100'000'000;
  std::uint64_t maximum_retained_diagnostics = 1'024;
  std::uint64_t maximum_congestion_cost_value = 10'000'000;

  friend bool operator==(const CpuSequentialNegotiatedRoutingLimits&,
                         const CpuSequentialNegotiatedRoutingLimits&) = default;
};

struct CpuSequentialNegotiatedRoutingConfig {
  CanonicalSequentialRipUpAndReroutePolicyV1 policy;
  CpuSequentialNegotiatedRoutingLimits limits;

  friend bool operator==(const CpuSequentialNegotiatedRoutingConfig&,
                         const CpuSequentialNegotiatedRoutingConfig&) = default;
};

enum class CpuSequentialAttemptOutcome : std::uint8_t {
  kAdmitted = 0,
  kDisconnected = 1,
  kUnsupported = 2,
  kCandidateBuildRejected = 3,
  kAdmissionRejected = 4,
};

enum class CpuSequentialAbsenceReason : std::uint8_t {
  kDisconnected = 0,
  kUnsupported = 1,
  kCandidateBuildRejected = 2,
  kAdmissionRejected = 3,
};

struct CpuSequentialAttemptDiagnostic {
  board_ir::EntityRef net;
  std::uint32_t pass_index = 0;
  std::uint64_t query_identity = 0;
  std::uint64_t policy_identity = 0;
  CpuSequentialAttemptOutcome outcome = CpuSequentialAttemptOutcome::kDisconnected;
  bool incumbent_retained = false;
  std::optional<routing::RouteFailureCode> route_failure_code;
  std::optional<candidates::CandidateRejectionCode> candidate_rejection_code;

  friend bool operator==(const CpuSequentialAttemptDiagnostic&,
                         const CpuSequentialAttemptDiagnostic&) = default;
};

struct CpuSequentialRetainedRoute {
  board_ir::EntityRef net;
  candidates::RouteCandidate candidate;
  std::uint32_t installed_pass_index = 0;
  std::uint64_t installed_query_identity = 0;

  friend bool operator==(const CpuSequentialRetainedRoute&,
                         const CpuSequentialRetainedRoute&) = default;
};

struct CpuSequentialRouteAbsence {
  board_ir::EntityRef net;
  CpuSequentialAbsenceReason reason = CpuSequentialAbsenceReason::kDisconnected;
  std::uint32_t final_attempt_pass_index = 0;
  std::uint64_t final_query_identity = 0;

  friend bool operator==(const CpuSequentialRouteAbsence&,
                         const CpuSequentialRouteAbsence&) = default;
};

using CpuSequentialNetOutcome = std::variant<CpuSequentialRetainedRoute, CpuSequentialRouteAbsence>;

enum class CpuSequentialTerminationReason : std::uint8_t {
  kFeasible = 0,
  kStalled = 1,
  kPassLimit = 2,
};

struct CpuSequentialNegotiatedRouting {
  ResourceLatticeAssociations associations;
  std::vector<CpuSequentialNetOutcome> nets;
  ResourceAccounting accounting;
  std::vector<CpuSequentialAttemptDiagnostic> diagnostics;
  CpuSequentialTerminationReason termination_reason = CpuSequentialTerminationReason::kPassLimit;
  std::uint64_t run_identity = 0;
  std::uint32_t completed_passes = 0;
  std::uint64_t route_attempts = 0;
  // Sum of deterministic congestion-snapshot work and production CPU A* work.
  std::uint64_t cpu_work_units = 0;
  std::uint64_t generated_candidate_bytes = 0;
  std::uint64_t retained_candidate_bytes = 0;
  std::uint64_t expanded_resource_uses = 0;

  friend bool operator==(const CpuSequentialNegotiatedRouting&,
                         const CpuSequentialNegotiatedRouting&) = default;
};

enum class CpuSequentialNegotiatedRoutingErrorCode : std::uint8_t {
  kInvalidConfiguration = 0,
  kInvalidInput = 1,
  kAssociationMismatch = 2,
  kBoundExhausted = 3,
  kArithmeticOverflow = 4,
  kResourceExhausted = 5,
  kInternalInvariant = 6,
};

struct CpuSequentialNegotiatedRoutingError {
  CpuSequentialNegotiatedRoutingErrorCode code =
      CpuSequentialNegotiatedRoutingErrorCode::kInvalidConfiguration;
  std::string_view invariant_id;
  std::string_view detail;
  std::optional<board_ir::EntityRef> net;
  std::optional<std::uint32_t> pass_index;
  std::optional<std::uint64_t> query_identity;
  std::optional<std::uint64_t> expected_value;
  std::optional<std::uint64_t> actual_value;
  std::optional<routing::CandidatePolicyErrorCode> policy_error_code;
  std::optional<routing::RouteFailureCode> route_failure_code;
  std::optional<candidates::CandidateRejectionCode> candidate_rejection_code;
  std::optional<ResourceAccountingErrorCode> accounting_error_code;

  friend bool operator==(const CpuSequentialNegotiatedRoutingError&,
                         const CpuSequentialNegotiatedRoutingError&) = default;
};

using CpuSequentialNegotiatedRoutingResult =
    std::variant<CpuSequentialNegotiatedRouting, CpuSequentialNegotiatedRoutingError>;

// Runs only CS-RR-v1 over baseline-local routes, occupancy, and congestion
// costs. It neither builds nor consumes candidate pools, CandidateStore,
// One-World selection, allocator prices, or reusable allocation sessions.
[[nodiscard]] CpuSequentialNegotiatedRoutingResult RouteCpuSequentialNegotiated(
    const board_ir::BoardSnapshot& board, const ResourceCapacityModel& capacities,
    std::span<const CpuSequentialNetRequest> nets,
    CpuSequentialNegotiatedRoutingConfig config = {});

}  // namespace apgar::allocator

#endif  // APGAR_ALLOCATOR_CPU_SEQUENTIAL_NEGOTIATED_ROUTING_H_
