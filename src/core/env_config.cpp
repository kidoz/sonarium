#include "core/env_config.hpp"

#include <charconv>
#include <cstdlib>
#include <system_error>

namespace sonarium::core {

std::int64_t checked_env_int(std::string_view name,
                             std::int64_t fallback,
                             std::int64_t min,
                             std::int64_t max,
                             std::vector<std::string>& issues) {
    auto const* raw = std::getenv(std::string{name}.c_str());
    if (raw == nullptr || *raw == '\0') {
        return fallback;
    }

    std::string_view const value{raw};
    std::int64_t parsed = 0;
    auto const result = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != value.data() + value.size()) {
        issues.push_back("config: " + std::string{name} + "='" + std::string{value}
                         + "' is not an integer — using " + std::to_string(fallback));
        return fallback;
    }
    if (parsed < min || parsed > max) {
        issues.push_back("config: " + std::string{name} + "=" + std::to_string(parsed)
                         + " is outside [" + std::to_string(min) + ", " + std::to_string(max)
                         + "] — using " + std::to_string(fallback));
        return fallback;
    }
    return parsed;
}

} // namespace sonarium::core
