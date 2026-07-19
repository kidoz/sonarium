#include "core/operator_mode.hpp"

#include <cctype>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

namespace sonarium::core {

namespace {

[[nodiscard]] bool iequals(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        auto const ca = std::tolower(static_cast<unsigned char>(a[i]));
        auto const cb = std::tolower(static_cast<unsigned char>(b[i]));
        if (ca != cb) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool is_wildcard_bind(std::string_view bind_host) noexcept {
    return bind_host.empty() || bind_host == "0.0.0.0" || bind_host == "::" || bind_host == "*";
}

} // namespace

OperatorMode parse_operator_mode(std::string_view raw) noexcept {
    if (iequals(raw, "production") || iequals(raw, "prod")) {
        return OperatorMode::production;
    }
    return OperatorMode::development;
}

OperatorMode current_operator_mode() noexcept {
    auto const* v = std::getenv("SONARIUM_MODE");
    return parse_operator_mode(v == nullptr ? std::string_view{} : std::string_view{v});
}

std::string_view to_string(OperatorMode mode) noexcept {
    return mode == OperatorMode::production ? std::string_view{"production"}
                                            : std::string_view{"development"};
}

std::vector<std::string> check_startup_invariants(StartupInvariants const& inv) {
    std::vector<std::string> violations;

    if (is_wildcard_bind(inv.bind_host) && !inv.allow_public_bind) {
        violations.emplace_back(
            "bind: bind_host '" + std::string{inv.bind_host}
            + "' exposes the service on all interfaces — set a LAN-only address (e.g. via "
              "SONARIUM_DLNA_BIND_HOST / SONARIUM_SERVER_BIND_HOST) or acknowledge with "
              "SONARIUM_ALLOW_PUBLIC_BIND=1");
    }

    if (inv.media_token_secret.empty()) {
        violations.emplace_back(
            "media_tokens: SONARIUM_MEDIA_TOKEN_SECRET is empty — direct media URLs will be "
            "served without authentication");
    }

    if (inv.pg_conninfo.empty() && inv.sqlite_path.empty()) {
        violations.emplace_back(
            "catalog: SONARIUM_PG_CONNINFO and SONARIUM_SQLITE_PATH are both empty — service "
            "would fall back to the in-memory demo catalog");
    }

    if (inv.media_root.empty()) {
        violations.emplace_back(
            "media_root: SONARIUM_MEDIA_ROOT is empty — served file paths are not contained to a "
            "library root, so a poisoned catalog row could expose arbitrary files");
    }

    return violations;
}

} // namespace sonarium::core
