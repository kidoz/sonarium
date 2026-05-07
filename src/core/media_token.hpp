#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace sonarium::core {

// Mints + verifies short-lived signed tokens for direct media URLs. The token
// shape is `?expires=<unix-seconds>&sig=<hex>`; the signed message is
// `<rendition_id>|<expires>` so a leaked URL can't be reused for a different
// rendition or beyond its expiry.
//
// An empty secret disables signing — in that mode `sign(...)` returns "" and
// `verify(...)` returns true regardless. That keeps dev-mode workflows simple
// without forking the route handler.
class MediaTokenSigner {
public:
    using Clock = std::function<std::int64_t()>; // returns Unix seconds

    explicit MediaTokenSigner(std::string secret,
                              std::chrono::seconds default_ttl = std::chrono::seconds{3600},
                              Clock clock = system_clock_now);

    [[nodiscard]] bool enabled() const noexcept { return !secret_.empty(); }

    // Returns the URL-suffix `?expires=...&sig=...`. When signing is disabled
    // returns an empty string so callers can append unconditionally.
    [[nodiscard]] std::string sign(std::string_view rendition_id) const;
    [[nodiscard]] std::string sign(std::string_view rendition_id, std::chrono::seconds ttl) const;

    // Verify a token presented on the wire. `expires_str` and `sig` are the raw
    // query-param values. Returns true if signing is disabled. Constant-time
    // signature compare.
    [[nodiscard]] bool verify(std::string_view rendition_id,
                              std::string_view expires_str,
                              std::string_view sig) const noexcept;

    [[nodiscard]] static std::int64_t system_clock_now() noexcept;

private:
    std::string secret_;
    std::chrono::seconds default_ttl_;
    Clock clock_;
};

} // namespace sonarium::core
