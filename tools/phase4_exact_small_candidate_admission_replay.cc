#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "apgar/benchmark/phase4_representative_corpus.h"
#include "apgar/candidates/route_candidate.h"
#if defined(APGAR_PHASE4_CONFIRMATORY_H4096_EXACT_REPLAY)
#include "src/benchmark/phase4_h4096_canonical_budget_internal.h"
#include "src/benchmark/phase4_paired_trial_internal.h"
#endif
#include "src/candidates/route_candidate_internal.h"

#if defined(APGAR_PHASE4_CONFIRMATORY_EXACT_REPLAY) && \
    defined(APGAR_PHASE4_CONFIRMATORY_H4096_EXACT_REPLAY)
#error "H=2250 and H=4096 exact-small replay authorities are mutually exclusive"
#endif

namespace {

constexpr std::array<std::uint8_t, 8> kMagic = {'A', 'P', 'G', 'A', 'R', 'P', '4', 'E'};
constexpr std::size_t kMaximumInputBytes = 64ULL * 1024ULL * 1024ULL;
constexpr std::uint32_t kPoolCount = 6;
constexpr std::uint32_t kMaximumCandidatesPerPool = 6;
constexpr std::uint32_t kMaximumCandidates = 36;
constexpr std::uint64_t kMaximumComponentRows = 100'000;
constexpr std::uint64_t kMaximumExpandedEdges = 100'000;
constexpr std::uint64_t kMaximumLogicalBytes = 32ULL * 1024ULL * 1024ULL;
constexpr std::uint32_t kMaximumDeviceClassBytes = 1024;
#if defined(APGAR_PHASE4_CONFIRMATORY_H4096_EXACT_REPLAY)
constexpr std::uint64_t kH4096ExactPairedBudgetChecksum = 5'851'813'264'366'095'594ULL;
#endif

class Reader {
 public:
  explicit Reader(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}

  [[nodiscard]] bool Done() const noexcept { return position_ == bytes_.size(); }

  [[nodiscard]] bool U8(std::uint8_t* value) noexcept {
    if (Remaining() < 1) return false;
    *value = bytes_[position_++];
    return true;
  }

  [[nodiscard]] bool U16(std::uint16_t* value) noexcept {
    std::uint64_t decoded = 0;
    if (!Unsigned(2, &decoded)) return false;
    *value = static_cast<std::uint16_t>(decoded);
    return true;
  }

  [[nodiscard]] bool U32(std::uint32_t* value) noexcept {
    std::uint64_t decoded = 0;
    if (!Unsigned(4, &decoded)) return false;
    *value = static_cast<std::uint32_t>(decoded);
    return true;
  }

  [[nodiscard]] bool U64(std::uint64_t* value) noexcept { return Unsigned(8, value); }

  [[nodiscard]] bool I64(std::int64_t* value) noexcept {
    std::uint64_t decoded = 0;
    if (!U64(&decoded)) return false;
    *value = std::bit_cast<std::int64_t>(decoded);
    return true;
  }

  [[nodiscard]] bool Bytes(std::span<const std::uint8_t> expected) noexcept {
    if (Remaining() < expected.size()) return false;
    for (std::size_t index = 0; index < expected.size(); ++index) {
      if (bytes_[position_ + index] != expected[index]) return false;
    }
    position_ += expected.size();
    return true;
  }

  [[nodiscard]] bool String(std::string* value) {
    std::uint32_t size = 0;
    if (!U32(&size) || size > kMaximumDeviceClassBytes || Remaining() < size) return false;
    value->assign(reinterpret_cast<const char*>(bytes_.data() + position_), size);
    position_ += size;
    return true;
  }

 private:
  [[nodiscard]] std::size_t Remaining() const noexcept { return bytes_.size() - position_; }

  [[nodiscard]] bool Unsigned(std::size_t size, std::uint64_t* value) noexcept {
    if (Remaining() < size) return false;
    std::uint64_t decoded = 0;
    for (std::size_t index = 0; index < size; ++index) {
      decoded |= static_cast<std::uint64_t>(bytes_[position_ + index]) << (8U * index);
    }
    position_ += size;
    *value = decoded;
    return true;
  }

  std::span<const std::uint8_t> bytes_;
  std::size_t position_ = 0;
};

struct Bounds {
  std::uint32_t candidates = 0;
  std::uint64_t geometry_rows = 0;
  std::uint64_t span_rows = 0;
  std::uint64_t policy_rows = 0;
  std::uint64_t expanded_edges = 0;
  std::uint64_t geometry_steps = 0;
  std::uint64_t logical_bytes = 0;
};

template <typename Value>
[[nodiscard]] bool AddWithin(Value* total, Value value, Value maximum) noexcept {
  if (value > maximum || *total > maximum - value) return false;
  *total += value;
  return true;
}

[[nodiscard]] bool ReadEntity(Reader* reader, apgar::board_ir::EntityRef* entity) noexcept {
  return reader->U64(&entity->id) && reader->U32(&entity->generation);
}

[[nodiscard]] bool ReadId(Reader* reader, apgar::candidates::Hash128* id) noexcept {
  return reader->U64(&id->high) && reader->U64(&id->low);
}

[[nodiscard]] bool ReadResource(Reader* reader,
                                apgar::routing::EdgeResourceKey* resource) noexcept {
  std::uint8_t direction = 0;
  if (!reader->U32(&resource->layer) || !reader->I64(&resource->lattice_x) ||
      !reader->I64(&resource->lattice_y) || !reader->U8(&direction) || direction > 3) {
    return false;
  }
  resource->direction = static_cast<apgar::geometry_compiler::Direction>(direction);
  return true;
}

[[nodiscard]] bool ReadPolicy(Reader* reader, apgar::routing::CandidateGenerationPolicy* policy,
                              Bounds* bounds) {
  std::uint8_t objective = 0;
  if (!reader->U32(&policy->schema_version) || !reader->U8(&objective) || objective > 3 ||
      !reader->U64(&policy->deterministic_seed) || !reader->U32(&policy->candidate_ordinal) ||
      !reader->U64(&policy->orthogonal_step_surcharge) ||
      !reader->U64(&policy->diagonal_step_surcharge) || !reader->U64(&policy->bend_surcharge)) {
    return false;
  }
  policy->objective = static_cast<apgar::routing::CandidateObjective>(objective);
  std::uint32_t banned_count = 0;
  if (!reader->U32(&banned_count) ||
      !AddWithin(&bounds->policy_rows, static_cast<std::uint64_t>(banned_count),
                 kMaximumComponentRows)) {
    return false;
  }
  policy->banned_resources.resize(banned_count);
  for (auto& resource : policy->banned_resources) {
    if (!ReadResource(reader, &resource)) return false;
  }
  std::uint32_t penalty_count = 0;
  if (!reader->U32(&penalty_count) ||
      !AddWithin(&bounds->policy_rows, static_cast<std::uint64_t>(penalty_count),
                 kMaximumComponentRows)) {
    return false;
  }
  policy->resource_penalties.resize(penalty_count);
  for (auto& penalty : policy->resource_penalties) {
    if (!ReadResource(reader, &penalty.resource) || !reader->U64(&penalty.additional_cost)) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool ReadCandidate(
    Reader* reader, apgar::candidates::GeneratedRouteCandidate* value,
    std::uint64_t* intrinsic_cost, Bounds* bounds,
    const apgar::geometry_compiler::CompilerProfile& compiler_profile) {
  if (!AddWithin(&bounds->candidates, 1U, kMaximumCandidates) ||
      !reader->U16(&value->schema_major) || !reader->U16(&value->schema_minor) ||
      !ReadId(reader, &value->id) || !ReadEntity(reader, &value->net) ||
      !ReadEntity(reader, &value->intended_terminals[0]) ||
      !ReadEntity(reader, &value->intended_terminals[1]) ||
      !reader->U64(&value->associations.board_content_hash) ||
      !reader->U64(&value->associations.compiler_profile_fingerprint) ||
      !reader->U32(&value->associations.geometry_compiler_version) ||
      !reader->U64(&value->associations.routing_profile_fingerprint) ||
      !reader->U64(&value->associations.rule_bucket_identity) ||
      !reader->U32(&value->geometry_schema_version) ||
      !reader->U32(&value->resource_schema_version) ||
      !ReadPolicy(reader, &value->policy, bounds) || !reader->U64(&value->policy_identity)) {
    return false;
  }
  std::uint8_t generator = 0;
  std::uint8_t backend = 0;
  if (!reader->U8(&generator) || generator > 2 ||
      !reader->U32(&value->provenance.generator_version) || !reader->U8(&backend) || backend > 1 ||
      !reader->String(&value->provenance.supported_device_class) ||
      !reader->U64(&value->provenance.deterministic_seed) ||
      !reader->U64(&value->provenance.batch_identity) ||
      !reader->U64(&value->provenance.query_identity) ||
      !reader->U32(&value->provenance.candidate_ordinal)) {
    return false;
  }
  value->provenance.generator = static_cast<apgar::candidates::CandidateGeneratorKind>(generator);
  value->provenance.backend = static_cast<apgar::candidates::CandidateBackendKind>(backend);

  std::uint32_t geometry_count = 0;
  if (!reader->U32(&geometry_count) ||
      !AddWithin(&bounds->geometry_rows, static_cast<std::uint64_t>(geometry_count),
                 kMaximumComponentRows)) {
    return false;
  }
  value->geometry.reserve(geometry_count);
  for (std::uint32_t index = 0; index < geometry_count; ++index) {
    std::uint8_t kind = 0;
    apgar::candidates::ExactLinePrimitive line;
    if (!reader->U8(&kind) || kind != 0 || !reader->U32(&line.layer) ||
        !reader->I64(&line.centerline.start.x) || !reader->I64(&line.centerline.start.y) ||
        !reader->I64(&line.centerline.end.x) || !reader->I64(&line.centerline.end.y)) {
      return false;
    }
    const std::optional<apgar::geometry_compiler::LatticeIndex> start =
        apgar::geometry_compiler::ExactPointToLatticeIndex(compiler_profile, line.centerline.start);
    const std::optional<apgar::geometry_compiler::LatticeIndex> end =
        apgar::geometry_compiler::ExactPointToLatticeIndex(compiler_profile, line.centerline.end);
    if (!start.has_value() || !end.has_value()) return false;
    using SignedWide = __int128;
    const SignedWide delta_x = static_cast<SignedWide>(end->x) - static_cast<SignedWide>(start->x);
    const SignedWide delta_y = static_cast<SignedWide>(end->y) - static_cast<SignedWide>(start->y);
    const std::uint64_t step_count = static_cast<std::uint64_t>(
        std::max(delta_x < 0 ? -delta_x : delta_x, delta_y < 0 ? -delta_y : delta_y));
    if (!AddWithin(&bounds->geometry_steps, step_count, kMaximumExpandedEdges)) return false;
    value->geometry.emplace_back(line);
  }

  std::uint32_t span_count = 0;
  if (!reader->U32(&span_count) ||
      !AddWithin(&bounds->span_rows, static_cast<std::uint64_t>(span_count),
                 kMaximumComponentRows)) {
    return false;
  }
  value->resources.resize(span_count);
  for (auto& span : value->resources) {
    apgar::routing::EdgeResourceKey resource;
    if (!ReadResource(reader, &resource) || !reader->U32(&span.edge_count) ||
        !reader->U32(&span.usage_units) ||
        !AddWithin(&bounds->expanded_edges, static_cast<std::uint64_t>(span.edge_count),
                   kMaximumExpandedEdges)) {
      return false;
    }
    span.layer = resource.layer;
    span.lattice_x = resource.lattice_x;
    span.lattice_y = resource.lattice_y;
    span.direction = resource.direction;
  }
  auto& metrics = value->metrics;
  if (!reader->U64(&metrics.scalar_policy_cost) || !reader->U64(&metrics.intrinsic_base_cost) ||
      !reader->U64(&metrics.orthogonal_step_count) || !reader->U64(&metrics.diagonal_step_count) ||
      !reader->U64(&metrics.bend_count) || !reader->U64(&metrics.line_primitive_count) ||
      !reader->U64(&metrics.via_count) || !reader->U64(&metrics.axis_aligned_length_dbu) ||
      !reader->U64(&metrics.diagonal_projection_dbu)) {
    return false;
  }
  std::uint8_t supported = 0;
  std::uint8_t unsupported = 0;
  std::uint8_t validation_code = 0;
  if (!reader->U8(&supported) || supported > 1 || !reader->U8(&unsupported) || unsupported > 1 ||
      !reader->U32(&value->constraints.connected_intended_terminal_count) ||
      !reader->U8(&validation_code) || validation_code > 3 ||
      !ReadId(reader, &value->geometry_signature) || !ReadId(reader, &value->resource_signature) ||
      !reader->U64(&value->payload_checksum) || !reader->U64(&value->logical_bytes) ||
      !reader->U64(intrinsic_cost) ||
      !AddWithin(&bounds->logical_bytes, value->logical_bytes, kMaximumLogicalBytes)) {
    return false;
  }
  value->constraints.supported_hard_constraints_satisfied = supported != 0;
  value->constraints.unsupported_rules_remain = unsupported != 0;
  value->constraints.exact_validation_code =
      static_cast<apgar::candidates::CandidateExactValidationCode>(validation_code);
  return true;
}

[[nodiscard]] int Fail(std::string_view message) {
  std::cerr << "phase4 exact-small candidate admission replay failed: " << message << '\n';
  return 1;
}

[[nodiscard]] int Run(std::span<const std::uint8_t> input) {
  Reader reader(input);
  std::uint32_t version = 0;
#if defined(APGAR_PHASE4_CONFIRMATORY_EXACT_REPLAY) || \
    defined(APGAR_PHASE4_CONFIRMATORY_H4096_EXACT_REPLAY)
  std::uint32_t corpus_version = 0;
#endif
  std::uint32_t case_id = 0;
#if defined(APGAR_PHASE4_CONFIRMATORY_H4096_EXACT_REPLAY)
  std::uint32_t pool_size = 0;
  std::uint64_t present_step_per_overuse_unit = 0;
  std::uint64_t history_step_per_overuse_unit = 0;
  std::uint64_t canonical_algorithm_budget_checksum = 0;
  std::uint64_t paired_budget_checksum = 0;
#endif
  apgar::benchmark::Phase4RepresentativeCorpusLimits limits;
  if (!reader.Bytes(kMagic) || !reader.U32(&version) ||
#if defined(APGAR_PHASE4_CONFIRMATORY_H4096_EXACT_REPLAY)
      version != 3 || !reader.U32(&corpus_version) ||
      corpus_version != apgar::benchmark::kPhase4RepresentativeCorpusVersionV2 ||
      !reader.U32(&case_id) || case_id != 10'100 || !reader.U32(&pool_size) || pool_size != 4 ||
      !reader.U64(&present_step_per_overuse_unit) ||
      present_step_per_overuse_unit !=
          apgar::benchmark::internal::kPhase4CorpusV2ProtocolV1PresentStepPerOveruseUnit ||
      !reader.U64(&history_step_per_overuse_unit) ||
      history_step_per_overuse_unit !=
          apgar::benchmark::internal::kPhase4CorpusV2H4096HistoryStepPerOveruseUnit ||
      !reader.U64(&canonical_algorithm_budget_checksum) ||
      canonical_algorithm_budget_checksum !=
          apgar::benchmark::internal::
              kPhase4ConfirmatoryH4096ExactCanonicalAlgorithmBudgetChecksum ||
      !reader.U64(&paired_budget_checksum) ||
      paired_budget_checksum != kH4096ExactPairedBudgetChecksum ||
#elif defined(APGAR_PHASE4_CONFIRMATORY_EXACT_REPLAY)
      version != 2 || !reader.U32(&corpus_version) ||
      corpus_version != apgar::benchmark::kPhase4RepresentativeCorpusVersionV2 ||
      !reader.U32(&case_id) || case_id != 10'100 ||
#else
      version != 1 || !reader.U32(&case_id) ||
      (case_id != 100 && case_id != 101 && case_id != 102) ||
#endif
      !reader.U64(&limits.maximum_nets) || !reader.U64(&limits.maximum_compiled_nodes) ||
      !reader.U64(&limits.maximum_compiled_host_bytes) ||
      !reader.U64(&limits.maximum_active_regions) || !reader.U64(&limits.maximum_board_entities)) {
    return Fail("invalid magic, version, case, or corpus limits");
  }
#if defined(APGAR_PHASE4_CONFIRMATORY_H4096_EXACT_REPLAY)
  apgar::benchmark::Phase4CanonicalCellConfig cell;
  cell.case_id = case_id;
  cell.requested_pool_size = pool_size;
  cell.preparation_worker_count = apgar::benchmark::kPhase4CanonicalPreparationWorkersV1;
  cell.repetitions = apgar::benchmark::kPhase4CanonicalRepetitionsV1;
  cell.maximum_setup_elapsed_nanoseconds = 300'000'000'000ULL;
  cell.external_budget = {
      .maximum_prepared_elapsed_nanoseconds = 300'000'000'000ULL,
      .maximum_cold_elapsed_nanoseconds = 300'000'000'000ULL,
      .maximum_address_space_bytes = 68'719'476'736ULL,
      .maximum_peak_host_bytes = 17'179'869'184ULL,
  };
  cell.corpus_limits = limits;
  apgar::benchmark::Phase4CanonicalSpecResult spec_result =
      apgar::benchmark::internal::BuildPhase4CanonicalTrialSpecForCorpusV2H4096(
          cell, 0, apgar::benchmark::Phase4TrialOrder::kBaselineFirst);
  const auto* spec = std::get_if<apgar::benchmark::Phase4PairedTrialSpec>(&spec_result);
  if (spec == nullptr || apgar::benchmark::internal::PreflightPhase4ConfirmatoryH4096SameRunSpec(
                             *spec, apgar::benchmark::Phase4TrialArm::kReusableCandidateAllocation)
                             .has_value()) {
    return Fail("the H=4096 exact canonical spec could not be independently reconstructed");
  }
  const apgar::benchmark::Phase4CaseDescriptor* descriptor =
      apgar::benchmark::FindPhase4CaseDescriptorForAuthority(
          apgar::benchmark::Phase4RepresentativeCorpusAuthority::kV2, case_id);
  if (descriptor == nullptr || spec->candidate_session_config.schedules.empty()) {
    return Fail("the H=4096 exact descriptor or terminal schedule is unavailable");
  }
  const std::uint64_t rebuilt_algorithm_budget =
      apgar::benchmark::internal::ComputePhase4CanonicalAlgorithmBudgetChecksumV1(*spec);
  const std::uint64_t rebuilt_paired_budget =
      apgar::benchmark::internal::ComputePhase4PairedBudgetChecksumForAuthorityV1(
          apgar::benchmark::Phase4RepresentativeCorpusAuthority::kV2, *spec,
          apgar::benchmark::Phase4RouteOpportunity{
              .route_queries = spec->baseline_config.limits.maximum_route_queries,
              .route_work_units = spec->baseline_config.limits.maximum_total_route_work_units,
          },
          descriptor->requested_net_count,
          spec->candidate_session_config.regeneration_plan_config.maximum_total_columns,
          spec->candidate_session_config.schedules.back().maximum_selection_rounds);
  if (spec->baseline_config.price_config.present_step_per_overuse_unit !=
          present_step_per_overuse_unit ||
      spec->candidate_session_config.price_config.present_step_per_overuse_unit !=
          present_step_per_overuse_unit ||
      spec->baseline_config.price_config.history_step_per_overuse_unit !=
          history_step_per_overuse_unit ||
      spec->candidate_session_config.price_config.history_step_per_overuse_unit !=
          history_step_per_overuse_unit ||
      rebuilt_algorithm_budget != canonical_algorithm_budget_checksum ||
      rebuilt_paired_budget != paired_budget_checksum ||
      rebuilt_paired_budget != kH4096ExactPairedBudgetChecksum) {
    return Fail("the H=4096 price or budget authority does not match its canonical preimage");
  }
  auto rebuilt = apgar::benchmark::BuildPhase4RepresentativeCaseV2(case_id, {}, limits);
#elif defined(APGAR_PHASE4_CONFIRMATORY_EXACT_REPLAY)
  auto rebuilt = apgar::benchmark::BuildPhase4RepresentativeCaseV2(case_id, {}, limits);
#else
  auto rebuilt = apgar::benchmark::BuildPhase4RepresentativeCaseV1(case_id, {}, limits);
#endif
  if (!std::holds_alternative<apgar::benchmark::Phase4RepresentativeCase>(rebuilt)) {
    return Fail("representative exact case could not be rebuilt");
  }
  const auto& representative = std::get<apgar::benchmark::Phase4RepresentativeCase>(rebuilt);
  std::uint32_t pool_count = 0;
  if (!reader.U32(&pool_count) || pool_count != kPoolCount ||
      representative.workload.nets().size() != kPoolCount) {
    return Fail("pool count differs from the exact-six rebuilt roster");
  }
  Bounds bounds;
  for (std::uint32_t pool_index = 0; pool_index < pool_count; ++pool_index) {
    apgar::board_ir::EntityRef pool_net;
    std::uint32_t candidate_count = 0;
    const auto& prepared = representative.workload.nets()[pool_index];
    if (!ReadEntity(&reader, &pool_net) || !(pool_net == prepared.request.net) ||
        !reader.U32(&candidate_count) || candidate_count > kMaximumCandidatesPerPool) {
      return Fail("pool roster or candidate count is invalid");
    }
    for (std::uint32_t candidate_index = 0; candidate_index < candidate_count; ++candidate_index) {
      apgar::candidates::GeneratedRouteCandidate candidate;
      std::uint64_t intrinsic_cost = 0;
      if (!ReadCandidate(&reader, &candidate, &intrinsic_cost, &bounds,
                         prepared.compiled_board.profile())) {
        return Fail("candidate wire payload is malformed or exceeds a bound");
      }
      apgar::routing::PlanarRouteRequest request = prepared.request;
      request.candidate_policy = candidate.policy;
      const apgar::candidates::CandidateAdmissionContext context{
          .board = representative.board,
          .compiled_board = prepared.compiled_board,
          .request = request,
      };
      if (std::holds_alternative<apgar::candidates::CandidateRejection>(
              apgar::candidates::internal::ValidateCandidatePayloadWithoutProducerEvidence(
                  context, candidate)) ||
          intrinsic_cost != candidate.metrics.intrinsic_base_cost) {
        return Fail("candidate failed exact non-authenticating admission");
      }
    }
  }
  if (!reader.Done()) return Fail("trailing bytes follow the exact candidate roster");
  return 0;
}

}  // namespace

int main() try {
  std::vector<std::uint8_t> input;
  input.reserve(1024 * 1024);
  std::array<char, 4096> chunk{};
  while (std::cin) {
    std::cin.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
    const std::streamsize count = std::cin.gcount();
    if (count <= 0) break;
    if (input.size() > kMaximumInputBytes - static_cast<std::size_t>(count)) {
      return Fail("stdin exceeds the 64 MiB bound");
    }
    input.insert(input.end(), chunk.begin(), chunk.begin() + count);
  }
  if (std::cin.bad()) return Fail("stdin read failed");
  return Run(input);
} catch (const std::exception&) {
  return Fail("bounded replay terminated with an exception");
}
