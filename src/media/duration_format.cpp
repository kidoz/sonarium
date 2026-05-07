#include "media/duration_format.hpp"

#include <array>
#include <cstdint>
#include <cstdio>

namespace sonarium::media {

namespace {

constexpr std::int64_t ms_per_second = 1000;
constexpr std::int64_t ms_per_minute = 60 * ms_per_second;
constexpr std::int64_t ms_per_hour = 60 * ms_per_minute;

} // namespace

std::string format_didl_duration_ms(std::int64_t total_ms) noexcept {
    if (total_ms < 0) {
        total_ms = 0;
    }

    auto const hours = total_ms / ms_per_hour;
    auto const minutes = (total_ms % ms_per_hour) / ms_per_minute;
    auto const seconds = (total_ms % ms_per_minute) / ms_per_second;
    auto const millis = total_ms % ms_per_second;

    // Buffer big enough for any 64-bit duration.
    std::array<char, 48> buf{};
    auto const written = std::snprintf(buf.data(),
                                       buf.size(),
                                       "%lld:%02lld:%02lld.%03lld",
                                       static_cast<long long>(hours),
                                       static_cast<long long>(minutes),
                                       static_cast<long long>(seconds),
                                       static_cast<long long>(millis));

    if (written <= 0) {
        return "0:00:00.000";
    }
    return std::string(buf.data(), static_cast<std::size_t>(written));
}

std::string format_didl_duration(std::chrono::milliseconds total) noexcept {
    return format_didl_duration_ms(static_cast<std::int64_t>(total.count()));
}

} // namespace sonarium::media
