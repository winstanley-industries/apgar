#ifndef APGAR_CANDIDATES_ROUTE_CANDIDATE_H_
#define APGAR_CANDIDATES_ROUTE_CANDIDATE_H_

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "apgar/board_ir/board.h"
#include "apgar/geometry_compiler/compiled_board.h"
#include "apgar/routing/candidate_policy.h"
#include "apgar/routing/cpu_astar.h"
#include "apgar/routing/planar_route.h"

namespace apgar::gpu {

struct PlanarCandidateBatch;
struct PlanarCandidateBatchQuery;
class PlanarCandidateBatchItem;

}  // namespace apgar::gpu

namespace apgar::candidates {

inline constexpr std::uint16_t kRouteCandidateSchemaMajor = 1;
inline constexpr std::uint16_t kRouteCandidateSchemaMinor = 0;
inline constexpr std::uint32_t kCandidateGeometrySchemaVersion = 1;
inline constexpr std::uint32_t kCandidateResourceSchemaVersion = 1;
inline constexpr std::uint32_t kCandidateRejectionSchemaVersion = 1;
inline constexpr std::string_view kCpuReferenceDeviceClassV1 = "cpu-reference-v1";
inline constexpr std::string_view kCudaDeviceClassPrefixV1 = "cuda-cc-";
inline constexpr std::uint64_t kMaximumCandidatePrimitives = 1'000'000;
inline constexpr std::uint64_t kMaximumCandidateResourceSpans = 1'000'000;
inline constexpr std::uint64_t kMaximumCandidateExpandedResourceEdges = 1'000'000;
inline constexpr std::uint64_t kMaximumCandidateSelfClearancePairChecks = 1'000'000;
inline constexpr std::size_t kMaximumCandidateDiagnosticBytes = 1024;

struct Hash128 {
  std::uint64_t high = 0;
  std::uint64_t low = 0;

  [[nodiscard]] constexpr bool empty() const noexcept { return high == 0 && low == 0; }

  friend bool operator==(const Hash128&, const Hash128&) = default;
  friend auto operator<=>(const Hash128&, const Hash128&) = default;
};

using CandidateId = Hash128;
using CandidateSignature = Hash128;

class RouteCandidateAdmissionFactory;
class GeneratedRouteCandidateProducerFactory;
struct GeneratedRouteCandidate;

// Copyable opaque capability. Callers may move/copy/reset a handle, but only
// typed producer adapters can create evidence and admission requires an exact
// payload match, so moving a handle between drafts cannot authenticate either.
class CandidateProducerEvidenceHandle {
 public:
  CandidateProducerEvidenceHandle() = default;
  CandidateProducerEvidenceHandle(const CandidateProducerEvidenceHandle&) = default;
  CandidateProducerEvidenceHandle(CandidateProducerEvidenceHandle&&) noexcept = default;
  CandidateProducerEvidenceHandle& operator=(const CandidateProducerEvidenceHandle&) = default;
  CandidateProducerEvidenceHandle& operator=(CandidateProducerEvidenceHandle&&) noexcept = default;

 private:
  struct Evidence;
  std::shared_ptr<const Evidence> evidence_;

  friend class GeneratedRouteCandidateProducerFactory;
};

struct CandidateAssociations {
  std::uint64_t board_content_hash = 0;
  std::uint64_t compiler_profile_fingerprint = 0;
  std::uint32_t geometry_compiler_version = 0;
  std::uint64_t routing_profile_fingerprint = 0;
  std::uint64_t rule_bucket_identity = 0;

  friend bool operator==(const CandidateAssociations&, const CandidateAssociations&) = default;
};

enum class CandidateGeneratorKind : std::uint8_t {
  kCpuAStar = 0,
  kCudaFrontier = 1,
  kCudaSweep = 2,
};

enum class CandidateBackendKind : std::uint8_t {
  kCpu = 0,
  kCuda = 1,
};

struct CandidateProvenance {
  CandidateGeneratorKind generator = CandidateGeneratorKind::kCpuAStar;
  std::uint32_t generator_version = 0;
  CandidateBackendKind backend = CandidateBackendKind::kCpu;
  std::string supported_device_class;
  std::uint64_t deterministic_seed = 0;
  std::uint64_t batch_identity = 0;
  std::uint64_t query_identity = 0;
  std::uint32_t candidate_ordinal = 0;

  friend bool operator==(const CandidateProvenance&, const CandidateProvenance&) = default;
};

struct ExactLinePrimitive {
  board_ir::LayerId layer = 0;
  board_ir::Segment64 centerline{};

  friend bool operator==(const ExactLinePrimitive&, const ExactLinePrimitive&) = default;
};

// Reserved by geometry schema v1. Admission deliberately rejects this variant
// until exact via-template compilation and validation exist.
struct ThroughViaPrimitive {
  std::uint64_t template_id = 0;
  board_ir::Point64 position{};
  board_ir::LayerId start_layer = 0;
  board_ir::LayerId end_layer = 0;

  friend bool operator==(const ThroughViaPrimitive&, const ThroughViaPrimitive&) = default;
};

using CandidatePrimitive = std::variant<ExactLinePrimitive, ThroughViaPrimitive>;

struct PhysicalEdgeSpan {
  board_ir::LayerId layer = 0;
  std::int64_t lattice_x = 0;
  std::int64_t lattice_y = 0;
  geometry_compiler::Direction direction = geometry_compiler::Direction::kEast;
  std::uint32_t edge_count = 0;
  std::uint32_t usage_units = 0;

  friend bool operator==(const PhysicalEdgeSpan&, const PhysicalEdgeSpan&) = default;
  friend auto operator<=>(const PhysicalEdgeSpan&, const PhysicalEdgeSpan&) = default;
};

// Span elements are stored in ascending EdgeResourceKey order. North-west
// canonical edge sources therefore advance south-east inside a span; the
// other three canonical directions advance in their movement direction.
[[nodiscard]] constexpr geometry_compiler::DirectionDelta ResourceSpanStorageDelta(
    geometry_compiler::Direction direction) noexcept {
  return direction == geometry_compiler::Direction::kNorthWest
             ? geometry_compiler::DirectionDelta{.x = 1, .y = -1}
             : geometry_compiler::DeltaFor(direction);
}

struct CandidateMetrics {
  std::uint64_t scalar_policy_cost = 0;
  std::uint64_t intrinsic_base_cost = 0;
  std::uint64_t orthogonal_step_count = 0;
  std::uint64_t diagonal_step_count = 0;
  std::uint64_t bend_count = 0;
  std::uint64_t line_primitive_count = 0;
  std::uint64_t via_count = 0;
  std::uint64_t axis_aligned_length_dbu = 0;
  std::uint64_t diagonal_projection_dbu = 0;

  friend bool operator==(const CandidateMetrics&, const CandidateMetrics&) = default;
};

enum class CandidateExactValidationCode : std::uint8_t {
  kPassed = 0,
  kUnsupportedGeometry = 1,
  kInvalidGeometry = 2,
  kExactRuleViolation = 3,
};

struct ConstraintAssessment {
  bool supported_hard_constraints_satisfied = false;
  bool unsupported_rules_remain = false;
  std::uint32_t connected_intended_terminal_count = 0;
  CandidateExactValidationCode exact_validation_code =
      CandidateExactValidationCode::kInvalidGeometry;

  friend bool operator==(const ConstraintAssessment&, const ConstraintAssessment&) = default;
};

// Generator-owned and intentionally mutable until it crosses admission. Every
// derived field is independently recomputed before an immutable RouteCandidate
// can be constructed. Successful typed builders additionally attach private,
// exact producer evidence. Public mutation cannot relabel one producer as
// another: admission compares the complete finalized payload with that
// immutable evidence before accepting it.
struct GeneratedRouteCandidate {
  std::uint16_t schema_major = kRouteCandidateSchemaMajor;
  std::uint16_t schema_minor = kRouteCandidateSchemaMinor;
  CandidateId id;
  board_ir::EntityRef net{};
  std::array<board_ir::EntityRef, 2> intended_terminals{};
  CandidateAssociations associations;
  std::uint32_t geometry_schema_version = kCandidateGeometrySchemaVersion;
  std::uint32_t resource_schema_version = kCandidateResourceSchemaVersion;
  routing::CandidateGenerationPolicy policy;
  std::uint64_t policy_identity = 0;
  CandidateProvenance provenance;
  std::vector<CandidatePrimitive> geometry;
  std::vector<PhysicalEdgeSpan> resources;
  CandidateMetrics metrics;
  ConstraintAssessment constraints;
  CandidateSignature geometry_signature;
  CandidateSignature resource_signature;
  std::uint64_t payload_checksum = 0;
  std::uint64_t logical_bytes = 0;
  CandidateProducerEvidenceHandle producer_evidence;

  friend bool operator==(const GeneratedRouteCandidate&, const GeneratedRouteCandidate&);
};

class RouteCandidate {
 public:
  RouteCandidate(const RouteCandidate&) = default;
  RouteCandidate(RouteCandidate&&) noexcept = default;
  RouteCandidate& operator=(const RouteCandidate&) = default;
  RouteCandidate& operator=(RouteCandidate&&) noexcept = default;

  [[nodiscard]] const GeneratedRouteCandidate& data() const noexcept { return data_; }
  [[nodiscard]] const CandidateId& id() const noexcept { return data_.id; }
  [[nodiscard]] board_ir::EntityRef net() const noexcept { return data_.net; }
  [[nodiscard]] std::uint64_t logical_bytes() const noexcept { return data_.logical_bytes; }

  friend bool operator==(const RouteCandidate&, const RouteCandidate&) = default;

 private:
  explicit RouteCandidate(GeneratedRouteCandidate data) : data_(std::move(data)) {
    data_.producer_evidence = {};
  }

  GeneratedRouteCandidate data_;

  friend class RouteCandidateAdmissionFactory;
};

enum class CandidateLifecycleStage : std::uint8_t {
  kGenerated = 0,
  kNormalized = 1,
  kExactValidated = 2,
  kResourceAccounted = 3,
  kSignedAndDeduplicated = 4,
  kStored = 5,
};

enum class CandidateRejectionCode : std::uint8_t {
  kInvalidInput = 0,
  kUnsupported = 1,
  kExactValidation = 2,
  kAssociationMismatch = 3,
  kResourceMismatch = 4,
  kMetricMismatch = 5,
  kSignatureMismatch = 6,
  kDuplicateIdentity = 7,
  kDuplicateGeometry = 8,
  kDuplicateResources = 9,
  kBudgetExhausted = 10,
  kMemoryAccountingOverflow = 11,
  kMemoryAccountingMismatch = 12,
  kInternalInvariant = 13,
  kCancelled = 14,
  kBackendFailure = 15,
};

struct CandidateRejection {
  std::uint32_t schema_version = kCandidateRejectionSchemaVersion;
  std::optional<CandidateId> candidate_id;
  std::optional<board_ir::EntityRef> net;
  CandidateLifecycleStage stage = CandidateLifecycleStage::kGenerated;
  CandidateRejectionCode code = CandidateRejectionCode::kInvalidInput;
  std::string invariant_id;
  CandidateAssociations associations;
  std::uint64_t policy_identity = 0;
  CandidateProvenance provenance;
  std::optional<std::uint64_t> primitive_witness_index;
  std::optional<std::uint64_t> resource_witness_index;
  std::optional<std::uint64_t> expected_value;
  std::optional<std::uint64_t> actual_value;
  std::optional<board_ir::EntityRef> conflicting_entity;
  std::optional<std::uint64_t> candidate_payload_checksum;
  std::string detail;
  std::uint64_t logical_bytes = 0;

  friend bool operator==(const CandidateRejection&, const CandidateRejection&) = default;
};

struct CandidateAdmissionContext {
  const board_ir::BoardSnapshot& board;
  const geometry_compiler::CompiledBoard& compiled_board;
  const routing::PlanarRouteRequest& request;
};

using CandidateAdmissionResult = std::variant<RouteCandidate, CandidateRejection>;

namespace internal {

enum class CandidateProducerAuthority : std::uint8_t;

[[nodiscard]] CandidateAdmissionResult AdmitRouteCandidateWithVerifiedRequestPolicy(
    const CandidateAdmissionContext& context,
    const routing::CandidatePolicyResult& verified_request_policy,
    GeneratedRouteCandidate&& generated);

}  // namespace internal

// Fully defined access class: consumers cannot complete or extend it to reach
// RouteCandidate's private constructor. Every public method runs complete
// exact admission and clamps hostile test budgets to schema maxima.
class RouteCandidateAdmissionFactory final {
 public:
  RouteCandidateAdmissionFactory() = delete;

  [[nodiscard]] static CandidateAdmissionResult AdmitWithBudgets(
      const CandidateAdmissionContext& context, GeneratedRouteCandidate&& generated,
      std::uint64_t maximum_pair_checks, std::uint64_t maximum_expanded_resource_edges);

 private:
  [[nodiscard]] static CandidateAdmissionResult AdmitWithVerifiedPolicy(
      const CandidateAdmissionContext& context, GeneratedRouteCandidate generated,
      std::uint64_t maximum_pair_checks, std::uint64_t maximum_expanded_resource_edges,
      const routing::CandidatePolicyResult* verified_request_policy);
  [[nodiscard]] static RouteCandidate Make(GeneratedRouteCandidate data);

  friend CandidateAdmissionResult internal::AdmitRouteCandidateWithVerifiedRequestPolicy(
      const CandidateAdmissionContext&, const routing::CandidatePolicyResult&,
      GeneratedRouteCandidate&&);
};

// Batch/query identities describe deterministic scheduling and remain distinct
// from a policy's candidate ordinal. Generator/backend/device provenance is
// derived by typed evidence builders rather than accepted from callers.
struct CandidateSchedulingIdentity {
  std::uint64_t batch_identity = 0;
  std::uint64_t query_identity = 0;

  friend bool operator==(const CandidateSchedulingIdentity&,
                         const CandidateSchedulingIdentity&) = default;
};

using CandidateDraftBuildResult = std::variant<GeneratedRouteCandidate, CandidateRejection>;

struct GpuCandidateBatchBuildRequest {
  std::reference_wrapper<const gpu::PlanarCandidateBatchQuery> query;
  std::reference_wrapper<const routing::NormalizedCandidateGenerationPolicy> normalized_policy;
  std::reference_wrapper<const gpu::PlanarCandidateBatchItem> item;
};

struct GpuCandidateBatchBuildFailure {
  CandidateRejectionCode code = CandidateRejectionCode::kInternalInvariant;
  std::string invariant_id;
  std::string detail;
};

using GpuCandidateBatchBuildResult =
    std::variant<std::vector<CandidateDraftBuildResult>, GpuCandidateBatchBuildFailure>;

// Fully defined and non-extensible. Sealing remains private to the concrete
// out-of-line typed-producer adapter; authentication is read-only.
class GeneratedRouteCandidateProducerFactory final {
 public:
  GeneratedRouteCandidateProducerFactory() = delete;

  [[nodiscard]] static bool Authenticates(const GeneratedRouteCandidate& candidate);

 private:
  static void Seal(GeneratedRouteCandidate& candidate,
                   internal::CandidateProducerAuthority authority);

  friend CandidateDraftBuildResult BuildGeneratedCandidateFromCpuRoute(
      const board_ir::BoardSnapshot&, const geometry_compiler::CompiledBoard&,
      const routing::CpuRouteRequest&, const routing::NormalizedCandidateGenerationPolicy&,
      const routing::CpuRoute&, CandidateSchedulingIdentity);
  friend CandidateDraftBuildResult BuildGeneratedCandidateFromGpuBatchItem(
      const board_ir::BoardSnapshot&, const geometry_compiler::CompiledBoard&,
      const gpu::PlanarCandidateBatchQuery&, const routing::NormalizedCandidateGenerationPolicy&,
      const gpu::PlanarCandidateBatch&, const gpu::PlanarCandidateBatchItem&);
  friend GpuCandidateBatchBuildResult BuildGeneratedCandidatesFromGpuBatchItems(
      const board_ir::BoardSnapshot&, const geometry_compiler::CompiledBoard&,
      const gpu::PlanarCandidateBatch&, std::span<const GpuCandidateBatchBuildRequest>);
};

[[nodiscard]] CandidateAssociations AssociationsFor(
    const board_ir::BoardSnapshot& board,
    const geometry_compiler::CompiledBoard& compiled_board) noexcept;

[[nodiscard]] CandidateId DeriveCandidateId(board_ir::EntityRef net,
                                            const CandidateAssociations& associations,
                                            std::uint64_t policy_identity,
                                            const CandidateProvenance& provenance) noexcept;

[[nodiscard]] CandidateSignature ComputeGeometrySignature(
    std::span<const CandidatePrimitive> geometry) noexcept;
[[nodiscard]] CandidateSignature ComputeResourceSignature(
    std::span<const PhysicalEdgeSpan> resources) noexcept;
[[nodiscard]] std::uint64_t ComputeCandidatePayloadChecksum(
    const GeneratedRouteCandidate& candidate) noexcept;
[[nodiscard]] std::optional<std::uint64_t> ComputeCandidateLogicalBytes(
    const GeneratedRouteCandidate& candidate) noexcept;
[[nodiscard]] std::optional<std::uint64_t> ComputeRejectionLogicalBytes(
    const CandidateRejection& rejection) noexcept;

// Validates the public Candidate Rejection v1 envelope and recomputes its
// canonical logical-byte count. Malformed external records are replaced by one
// bounded candidate.rejection.ingestion.v1 diagnostic rather than retained as
// if they were valid schema-v1 evidence. The envelope and O(1) text-size bounds
// are checked by reference before any caller-owned string is copied.
[[nodiscard]] CandidateRejection CanonicalizeCandidateRejectionV1(
    const CandidateRejection& submitted_rejection);

// Populates signatures, checksum, and logical bytes for a canonical draft.
// Admission still recomputes and checks each value independently.
[[nodiscard]] std::optional<CandidateRejection> FinalizeGeneratedCandidateDraft(
    GeneratedRouteCandidate& candidate) noexcept;

// CPU evidence cannot claim GPU provenance. The builder derives the CPU A*,
// CPU backend, and cpu-reference-v1 fields and accepts only scheduling IDs.
[[nodiscard]] CandidateDraftBuildResult BuildGeneratedCandidateFromCpuRoute(
    const board_ir::BoardSnapshot& board, const geometry_compiler::CompiledBoard& compiled_board,
    const routing::CpuRouteRequest& request,
    const routing::NormalizedCandidateGenerationPolicy& normalized_policy,
    const routing::CpuRoute& route, CandidateSchedulingIdentity scheduling);

// Lvalue admission performs every O(1) payload-shape check by reference before
// making a bounded owned copy. Rvalue admission checks before consuming the
// caller-owned payload. Neither public seam performs an implicit hostile bulk
// copy before shape preflight.
[[nodiscard]] CandidateAdmissionResult AdmitRouteCandidate(
    const CandidateAdmissionContext& context, const GeneratedRouteCandidate& generated);
[[nodiscard]] CandidateAdmissionResult AdmitRouteCandidate(const CandidateAdmissionContext& context,
                                                           GeneratedRouteCandidate&& generated);

// Test-only hostile-input seam. The supplied budget is clamped to the fixed
// production maximum and therefore cannot weaken admission bounds.
[[nodiscard]] CandidateAdmissionResult AdmitRouteCandidateWithReducedSelfClearanceBudgetForTesting(
    const CandidateAdmissionContext& context, GeneratedRouteCandidate&& generated,
    std::uint64_t maximum_pair_checks);

// Test/replay decorator seam for the production expanded-resource bound. The
// supplied value is clamped to the fixed schema-v1 maximum.
[[nodiscard]] CandidateAdmissionResult AdmitRouteCandidateWithReducedResourceEdgeBudgetForTesting(
    const CandidateAdmissionContext& context, GeneratedRouteCandidate&& generated,
    std::uint64_t maximum_expanded_resource_edges);

[[nodiscard]] bool CanonicalGeometryEqual(const RouteCandidate& left,
                                          const RouteCandidate& right) noexcept;
[[nodiscard]] bool CanonicalResourcesEqual(const RouteCandidate& left,
                                           const RouteCandidate& right) noexcept;

}  // namespace apgar::candidates

#endif  // APGAR_CANDIDATES_ROUTE_CANDIDATE_H_
