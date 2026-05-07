#pragma once

#include <cstdint>
#include <string_view>

namespace sonarium::core {

// Compare two byte strings in time independent of the position of the first
// differing byte. Use this for token / signature comparison so an attacker
// can't time their way to the right value.
[[nodiscard]] inline bool constant_time_equals(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) {
        return false;
    }
    std::uint8_t diff = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        diff |= static_cast<std::uint8_t>(static_cast<std::uint8_t>(a[i])
                                          ^ static_cast<std::uint8_t>(b[i]));
    }
    return diff == 0U;
}

} // namespace sonarium::core
