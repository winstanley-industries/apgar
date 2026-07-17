#ifndef APGAR_VERSION_H_
#define APGAR_VERSION_H_

#include <string_view>

namespace apgar {

[[nodiscard]] std::string_view ProjectName() noexcept;
[[nodiscard]] std::string_view Version() noexcept;

}  // namespace apgar

#endif  // APGAR_VERSION_H_
