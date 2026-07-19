#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace sonarium::core {

enum class OperatorMode : std::uint8_t { development, production };

// Recognises "production"/"prod" (case-insensitive). Anything else (including
// "", "development", "dev") returns Development. Defaults stay safe-for-dev so
// `meson test` and `just dev` work without setting env.
[[nodiscard]] OperatorMode parse_operator_mode(std::string_view raw) noexcept;

// Reads SONARIUM_MODE from the environment.
[[nodiscard]] OperatorMode current_operator_mode() noexcept;

[[nodiscard]] std::string_view to_string(OperatorMode mode) noexcept;

struct StartupInvariants {
    std::string_view bind_host;
    std::string_view media_token_secret;
    // Durable catalog backends — production requires at least one of these.
    std::string_view pg_conninfo;
    std::string_view sqlite_path;
    // SONARIUM_MEDIA_ROOT — library root that served file paths must resolve
    // into. Empty disables containment, which is a violation in production.
    std::string_view media_root;
    // SONARIUM_ALLOW_PUBLIC_BIND=1 lets an operator opt in to 0.0.0.0 in
    // production after acknowledging the LAN-only policy. Off by default.
    bool allow_public_bind = false;
};

// Pure check — returns one human-readable line per violation. Empty vector
// means the operator config is safe for production. Callers decide whether to
// treat violations as fatal (production) or as warnings (development).
[[nodiscard]] std::vector<std::string> check_startup_invariants(StartupInvariants const& inv);

} // namespace sonarium::core
