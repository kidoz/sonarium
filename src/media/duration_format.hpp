#pragma once

#include <chrono>
#include <cstdint>
#include <string>

namespace sonarium::media {

// Format a duration as ISO-8601-ish "H:MM:SS.mmm" used by DIDL-Lite `<res duration=...>`.
[[nodiscard]] std::string format_didl_duration(std::chrono::milliseconds total) noexcept;

// Convert milliseconds (must be non-negative) into the same format.
[[nodiscard]] std::string format_didl_duration_ms(std::int64_t total_ms) noexcept;

} // namespace sonarium::media
