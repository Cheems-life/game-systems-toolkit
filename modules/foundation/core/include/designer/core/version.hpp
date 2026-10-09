#pragma once

#include <string_view>

namespace designer::core {

// Library version only; save-data and protocol versions are separate contracts.
[[nodiscard]] std::string_view version() noexcept;

} // namespace designer::core
