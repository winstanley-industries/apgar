#ifndef APGAR_ALLOCATOR_MULTI_NET_WORKLOAD_H_
#define APGAR_ALLOCATOR_MULTI_NET_WORKLOAD_H_

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "apgar/board_ir/board.h"
#include "apgar/geometry_compiler/compiled_board.h"
#include "apgar/routing/planar_route.h"

namespace apgar::allocator {

inline constexpr std::uint32_t kMultiNetWorkloadSchemaVersion = 1;
inline constexpr std::uint64_t kMaximumMultiNetWorkloadNetsV1 = 1'000'000;
inline constexpr std::uint64_t kMaximumMultiNetWorkloadCompiledNodesV1 = 100'000'000;
inline constexpr std::uint64_t kMaximumMultiNetWorkloadCompiledHostBytesV1 =
    8ULL * 1024ULL * 1024ULL * 1024ULL;

struct MultiNetWorkloadLimits {
  std::uint64_t maximum_nets = 100'000;
  std::uint64_t maximum_compiled_nodes = kMaximumMultiNetWorkloadCompiledNodesV1;
  std::uint64_t maximum_compiled_host_bytes = kMaximumMultiNetWorkloadCompiledHostBytesV1;

  friend bool operator==(const MultiNetWorkloadLimits&, const MultiNetWorkloadLimits&) = default;
};

struct MultiNetRoutingSpec {
  board_ir::RoutingProfile routing_profile;
  board_ir::LayerId start_layer = 0;
  board_ir::LayerId goal_layer = 0;

  friend bool operator==(const MultiNetRoutingSpec&, const MultiNetRoutingSpec&) = default;
};

struct PreparedNetRoutingContext {
  board_ir::RoutingProfile routing_profile;
  geometry_compiler::CompiledBoard compiled_board;
  routing::PlanarRouteRequest request;
  std::uint64_t routing_profile_fingerprint = 0;

  friend bool operator==(const PreparedNetRoutingContext&,
                         const PreparedNetRoutingContext&) = default;
};

enum class MultiNetWorkloadErrorCode : std::uint8_t {
  kUnsupportedSchema = 0,
  kInvalidConfiguration = 1,
  kInputBoundExceeded = 2,
  kDuplicateNet = 3,
  kInvalidRoutingProfile = 4,
  kCompileFailure = 5,
  kInvalidRouteRequest = 6,
  kAssociationMismatch = 7,
  kResourceExhausted = 8,
  kWorkBoundExceeded = 9,
};

struct MultiNetWorkloadError {
  MultiNetWorkloadErrorCode code = MultiNetWorkloadErrorCode::kInvalidConfiguration;
  // Stable static diagnostics keep every error path, including allocation
  // failure handling, allocation-free.
  std::string_view invariant_id;
  std::string_view detail;
  std::optional<board_ir::EntityRef> net;

  friend bool operator==(const MultiNetWorkloadError&, const MultiNetWorkloadError&) = default;
};

class MultiNetWorkload {
 public:
  MultiNetWorkload(const MultiNetWorkload&) = default;
  MultiNetWorkload(MultiNetWorkload&&) noexcept = default;
  MultiNetWorkload& operator=(const MultiNetWorkload&) = default;
  MultiNetWorkload& operator=(MultiNetWorkload&&) noexcept = default;

  [[nodiscard]] std::uint32_t schema_version() const noexcept { return schema_version_; }
  [[nodiscard]] std::uint64_t board_content_hash() const noexcept { return board_content_hash_; }
  [[nodiscard]] std::uint64_t compiler_profile_fingerprint() const noexcept {
    return compiler_profile_fingerprint_;
  }
  [[nodiscard]] std::uint32_t geometry_compiler_version() const noexcept {
    return geometry_compiler_version_;
  }
  [[nodiscard]] const geometry_compiler::CompilerProfile& compiler_profile() const noexcept {
    return compiler_profile_;
  }
  [[nodiscard]] const std::vector<PreparedNetRoutingContext>& nets() const noexcept {
    return nets_;
  }
  [[nodiscard]] std::uint64_t workload_checksum() const noexcept { return workload_checksum_; }
  [[nodiscard]] std::uint64_t compiled_node_count() const noexcept { return compiled_node_count_; }
  [[nodiscard]] std::uint64_t compiled_host_bytes() const noexcept { return compiled_host_bytes_; }

  [[nodiscard]] const PreparedNetRoutingContext* FindNet(board_ir::EntityRef net) const noexcept;

  friend bool operator==(const MultiNetWorkload&, const MultiNetWorkload&) = default;

 private:
  MultiNetWorkload(std::uint32_t schema_version, std::uint64_t board_content_hash,
                   std::uint64_t compiler_profile_fingerprint,
                   std::uint32_t geometry_compiler_version,
                   geometry_compiler::CompilerProfile compiler_profile,
                   std::vector<PreparedNetRoutingContext> nets, std::uint64_t workload_checksum,
                   std::uint64_t compiled_node_count, std::uint64_t compiled_host_bytes)
      : schema_version_(schema_version),
        board_content_hash_(board_content_hash),
        compiler_profile_fingerprint_(compiler_profile_fingerprint),
        geometry_compiler_version_(geometry_compiler_version),
        compiler_profile_(std::move(compiler_profile)),
        nets_(std::move(nets)),
        workload_checksum_(workload_checksum),
        compiled_node_count_(compiled_node_count),
        compiled_host_bytes_(compiled_host_bytes) {}

  std::uint32_t schema_version_ = kMultiNetWorkloadSchemaVersion;
  std::uint64_t board_content_hash_ = 0;
  std::uint64_t compiler_profile_fingerprint_ = 0;
  std::uint32_t geometry_compiler_version_ = 0;
  geometry_compiler::CompilerProfile compiler_profile_;
  std::vector<PreparedNetRoutingContext> nets_;
  std::uint64_t workload_checksum_ = 0;
  std::uint64_t compiled_node_count_ = 0;
  std::uint64_t compiled_host_bytes_ = 0;

  friend std::variant<MultiNetWorkload, MultiNetWorkloadError> BuildMultiNetWorkload(
      std::uint32_t, const board_ir::BoardSnapshot&, const geometry_compiler::CompilerProfile&,
      std::span<const MultiNetRoutingSpec>, std::uint64_t);
  friend std::variant<MultiNetWorkload, MultiNetWorkloadError> BuildMultiNetWorkload(
      std::uint32_t, const board_ir::BoardSnapshot&, const geometry_compiler::CompilerProfile&,
      std::span<const MultiNetRoutingSpec>, const MultiNetWorkloadLimits&);
};

using MultiNetWorkloadResult = std::variant<MultiNetWorkload, MultiNetWorkloadError>;

[[nodiscard]] MultiNetWorkloadResult BuildMultiNetWorkload(
    std::uint32_t schema_version, const board_ir::BoardSnapshot& board,
    const geometry_compiler::CompilerProfile& compiler_profile,
    std::span<const MultiNetRoutingSpec> specs, std::uint64_t maximum_nets);
[[nodiscard]] MultiNetWorkloadResult BuildMultiNetWorkload(
    std::uint32_t schema_version, const board_ir::BoardSnapshot& board,
    const geometry_compiler::CompilerProfile& compiler_profile,
    std::span<const MultiNetRoutingSpec> specs, const MultiNetWorkloadLimits& limits);

}  // namespace apgar::allocator

#endif  // APGAR_ALLOCATOR_MULTI_NET_WORKLOAD_H_
