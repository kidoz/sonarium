#include "core/media_token.hpp"

#include <charconv>
#include <chrono>
#include <string>
#include <utility>

#include "core/constant_time.hpp"
#include "core/hmac_sha256.hpp"

namespace sonarium::core {

namespace {

[[nodiscard]] std::string make_signed_message(std::string_view rendition_id, std::int64_t expires) {
    std::string message;
    message.reserve(rendition_id.size() + 24);
    message.append(rendition_id);
    message.push_back('|');
    message.append(std::to_string(expires));
    return message;
}

} // namespace

std::int64_t MediaTokenSigner::system_clock_now() noexcept {
    using clock = std::chrono::system_clock;
    return std::chrono::duration_cast<std::chrono::seconds>(clock::now().time_since_epoch())
        .count();
}

MediaTokenSigner::MediaTokenSigner(std::string secret,
                                   std::chrono::seconds default_ttl,
                                   Clock clock)
    : secret_{std::move(secret)}, default_ttl_{default_ttl}, clock_{std::move(clock)} {
    if (!clock_) {
        clock_ = system_clock_now;
    }
}

std::string MediaTokenSigner::sign(std::string_view rendition_id) const {
    return sign(rendition_id, default_ttl_);
}

std::string MediaTokenSigner::sign(std::string_view rendition_id, std::chrono::seconds ttl) const {
    if (!enabled()) {
        return {};
    }
    auto const expires = clock_() + ttl.count();
    auto const message = make_signed_message(rendition_id, expires);
    auto const sig = hmac_sha256_hex(secret_, message);

    std::string out;
    out.reserve(sig.size() + 32);
    out.append("?expires=");
    out.append(std::to_string(expires));
    out.append("&sig=");
    out.append(sig);
    return out;
}

bool MediaTokenSigner::verify(std::string_view rendition_id,
                              std::string_view expires_str,
                              std::string_view sig) const noexcept {
    if (!enabled()) {
        return true;
    }
    if (expires_str.empty() || sig.empty()) {
        return false;
    }

    std::int64_t expires{};
    auto const* first = expires_str.data();
    auto const* last = expires_str.data() + expires_str.size();
    auto const result = std::from_chars(first, last, expires);
    if (result.ec != std::errc{} || result.ptr != last) {
        return false;
    }
    if (clock_() >= expires) {
        return false;
    }

    auto const message = make_signed_message(rendition_id, expires);
    auto const expected_sig = hmac_sha256_hex(secret_, message);
    return constant_time_equals(expected_sig, sig);
}

} // namespace sonarium::core
