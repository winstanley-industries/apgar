#ifndef APGAR_SRC_OPERATIONAL_TIMESTAMP_H_
#define APGAR_SRC_OPERATIONAL_TIMESTAMP_H_

namespace apgar::internal {

// Raw specializations instantiate the empty type and therefore carry neither a
// clock value nor a clock operation. Instrumented specializations retain the
// timestamp needed across the operation being measured.
template <bool Capture, typename Clock>
class OperationalTimestamp {};

template <typename Clock>
class OperationalTimestamp<true, Clock> {
 public:
  OperationalTimestamp& operator=(typename Clock::time_point value) noexcept {
    value_ = value;
    return *this;
  }

  [[nodiscard]] operator typename Clock::time_point() const noexcept { return value_; }

 private:
  typename Clock::time_point value_;
};

template <bool Capture, typename Clock>
class OperationalClockAccess {
 public:
  static typename Clock::time_point Now() = delete;
};

template <typename Clock>
class OperationalClockAccess<true, Clock> {
 public:
  [[nodiscard]] static typename Clock::time_point Now() noexcept { return Clock::now(); }
};

template <bool Capture, typename Clock>
[[nodiscard]] typename Clock::time_point OperationalNow() noexcept {
  return OperationalClockAccess<Capture, Clock>::Now();
}

}  // namespace apgar::internal

#endif  // APGAR_SRC_OPERATIONAL_TIMESTAMP_H_
