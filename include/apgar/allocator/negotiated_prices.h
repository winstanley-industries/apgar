#ifndef APGAR_ALLOCATOR_NEGOTIATED_PRICES_H_
#define APGAR_ALLOCATOR_NEGOTIATED_PRICES_H_

#include <cstdint>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "apgar/allocator/multi_net_workload.h"
#include "apgar/allocator/one_world.h"

namespace apgar::allocator {

inline constexpr std::uint32_t kNegotiatedPriceStateSchemaVersion = 1;
inline constexpr std::uint32_t kMaximumNegotiatedPriceIterationsV1 = 1'000'000;

struct NegotiatedPriceConfig {
  std::uint64_t present_step_per_overuse_unit = 1;
  std::uint64_t history_step_per_overuse_unit = 1;
  std::uint64_t maximum_price_per_resource = 1'000'000'000;
  std::uint32_t maximum_iterations = 10'000;
  std::uint64_t maximum_price_records = 1'000'000;

  friend bool operator==(const NegotiatedPriceConfig&, const NegotiatedPriceConfig&) = default;
};

struct NegotiatedResourcePrice {
  routing::EdgeResourceKey resource;
  std::uint64_t present_price = 0;
  std::uint64_t history_price = 0;
  std::uint64_t total_price = 0;
  std::uint64_t observed_overuse_units = 0;
  bool present_clamped = false;
  bool history_clamped = false;
  bool total_clamped = false;

  friend bool operator==(const NegotiatedResourcePrice&, const NegotiatedResourcePrice&) = default;
};

enum class NegotiatedPriceErrorCode : std::uint8_t {
  kUnsupportedSchema = 0,
  kInvalidConfiguration = 1,
  kAssociationMismatch = 2,
  kWorkloadMismatch = 3,
  kInvalidState = 4,
  kInvalidWorld = 5,
  kIterationBoundExceeded = 6,
  kRecordBoundExceeded = 7,
  kArithmeticOverflow = 8,
  kResourceExhausted = 9,
};

struct NegotiatedPriceError {
  NegotiatedPriceErrorCode code = NegotiatedPriceErrorCode::kInvalidConfiguration;
  std::string_view invariant_id;
  std::string_view detail;

  friend bool operator==(const NegotiatedPriceError&, const NegotiatedPriceError&) = default;
};

class NegotiatedPriceState {
 public:
  NegotiatedPriceState(const NegotiatedPriceState&) = default;
  NegotiatedPriceState(NegotiatedPriceState&&) noexcept = default;
  NegotiatedPriceState& operator=(const NegotiatedPriceState&) = default;
  NegotiatedPriceState& operator=(NegotiatedPriceState&&) noexcept = default;

  [[nodiscard]] std::uint32_t schema_version() const noexcept { return schema_version_; }
  [[nodiscard]] const AllocationAssociations& associations() const noexcept {
    return associations_;
  }
  [[nodiscard]] std::uint64_t workload_checksum() const noexcept { return workload_checksum_; }
  [[nodiscard]] std::uint64_t capacity_model_checksum() const noexcept {
    return capacity_model_checksum_;
  }
  [[nodiscard]] const NegotiatedPriceConfig& config() const noexcept { return config_; }
  [[nodiscard]] std::uint32_t iteration() const noexcept { return iteration_; }
  [[nodiscard]] std::uint64_t predecessor_state_checksum() const noexcept {
    return predecessor_state_checksum_;
  }
  [[nodiscard]] std::uint64_t source_world_checksum() const noexcept {
    return source_world_checksum_;
  }
  [[nodiscard]] const std::vector<NegotiatedResourcePrice>& prices() const noexcept {
    return prices_;
  }
  [[nodiscard]] std::uint64_t clamped_resource_count() const noexcept {
    return clamped_resource_count_;
  }
  [[nodiscard]] std::uint64_t total_present_price() const noexcept { return total_present_price_; }
  [[nodiscard]] std::uint64_t total_history_price() const noexcept { return total_history_price_; }
  [[nodiscard]] std::uint64_t total_price() const noexcept { return total_price_; }
  [[nodiscard]] std::uint64_t state_checksum() const noexcept { return state_checksum_; }

  friend bool operator==(const NegotiatedPriceState&, const NegotiatedPriceState&) = default;

 private:
  NegotiatedPriceState(std::uint32_t schema_version, AllocationAssociations associations,
                       std::uint64_t workload_checksum, std::uint64_t capacity_model_checksum,
                       NegotiatedPriceConfig config, std::uint32_t iteration,
                       std::uint64_t predecessor_state_checksum,
                       std::uint64_t source_world_checksum,
                       std::vector<NegotiatedResourcePrice> prices,
                       std::uint64_t clamped_resource_count, std::uint64_t total_present_price,
                       std::uint64_t total_history_price, std::uint64_t total_price,
                       std::uint64_t state_checksum)
      : schema_version_(schema_version),
        associations_(associations),
        workload_checksum_(workload_checksum),
        capacity_model_checksum_(capacity_model_checksum),
        config_(config),
        iteration_(iteration),
        predecessor_state_checksum_(predecessor_state_checksum),
        source_world_checksum_(source_world_checksum),
        prices_(std::move(prices)),
        clamped_resource_count_(clamped_resource_count),
        total_present_price_(total_present_price),
        total_history_price_(total_history_price),
        total_price_(total_price),
        state_checksum_(state_checksum) {}

  std::uint32_t schema_version_ = kNegotiatedPriceStateSchemaVersion;
  AllocationAssociations associations_;
  std::uint64_t workload_checksum_ = 0;
  std::uint64_t capacity_model_checksum_ = 0;
  NegotiatedPriceConfig config_;
  std::uint32_t iteration_ = 0;
  std::uint64_t predecessor_state_checksum_ = 0;
  std::uint64_t source_world_checksum_ = 0;
  std::vector<NegotiatedResourcePrice> prices_;
  std::uint64_t clamped_resource_count_ = 0;
  std::uint64_t total_present_price_ = 0;
  std::uint64_t total_history_price_ = 0;
  std::uint64_t total_price_ = 0;
  std::uint64_t state_checksum_ = 0;

  friend std::variant<NegotiatedPriceState, NegotiatedPriceError> BuildInitialNegotiatedPriceState(
      std::uint32_t, const ResourceCapacityModel&, const MultiNetWorkload&,
      const NegotiatedPriceConfig&);
  friend std::variant<NegotiatedPriceState, NegotiatedPriceError> UpdateNegotiatedPrices(
      const NegotiatedPriceState&, const OneWorldAllocationRequest&, const OneWorldAllocation&);
};

using NegotiatedPriceStateResult = std::variant<NegotiatedPriceState, NegotiatedPriceError>;
using NegotiatedPriceSnapshotResult = std::variant<PriceSnapshot, NegotiatedPriceError>;

[[nodiscard]] NegotiatedPriceStateResult BuildInitialNegotiatedPriceState(
    std::uint32_t schema_version, const ResourceCapacityModel& capacities,
    const MultiNetWorkload& workload, const NegotiatedPriceConfig& config);

[[nodiscard]] NegotiatedPriceSnapshotResult BuildPriceSnapshotForState(
    const ResourceCapacityModel& capacities, const NegotiatedPriceState& state);

[[nodiscard]] NegotiatedPriceStateResult UpdateNegotiatedPrices(
    const NegotiatedPriceState& previous, const OneWorldAllocationRequest& source_request,
    const OneWorldAllocation& world);

}  // namespace apgar::allocator

#endif  // APGAR_ALLOCATOR_NEGOTIATED_PRICES_H_
