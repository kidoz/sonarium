#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace sonarium::core {

// Read an integral environment variable with bounds checking.
//
// Unset or empty returns `fallback` silently — absence is a valid
// configuration. A set-but-malformed or out-of-range value also returns
// `fallback`, but appends a human-readable line to `issues` so the caller can
// surface it: mains print issues as WARN in development and refuse to start
// in production, where a silently ignored typo (e.g. a port of 99999) would
// otherwise deploy with the wrong value.
[[nodiscard]] std::int64_t checked_env_int(std::string_view name,
                                           std::int64_t fallback,
                                           std::int64_t min,
                                           std::int64_t max,
                                           std::vector<std::string>& issues);

} // namespace sonarium::core
