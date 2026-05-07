#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>

#include "core/media_token.hpp"

using sonarium::core::MediaTokenSigner;

namespace {

// Returns a fixed-time clock so token generation is deterministic across runs.
[[nodiscard]] MediaTokenSigner::Clock fixed_clock(std::int64_t now) {
    return [now] { return now; };
}

// Pull `?expires=...&sig=...` apart into the two values for verify().
struct ParsedSuffix {
    std::string expires;
    std::string sig;
};

[[nodiscard]] ParsedSuffix parse_suffix(std::string_view suffix) {
    ParsedSuffix p;
    auto const exp_marker = std::string_view{"?expires="};
    auto const sig_marker = std::string_view{"&sig="};
    auto const exp_pos = suffix.find(exp_marker);
    auto const sig_pos = suffix.find(sig_marker);
    if (exp_pos != std::string_view::npos && sig_pos != std::string_view::npos) {
        p.expires.assign(suffix.substr(exp_pos + exp_marker.size(), sig_pos - exp_marker.size()));
        p.sig.assign(suffix.substr(sig_pos + sig_marker.size()));
    }
    return p;
}

} // namespace

TEST_CASE("Disabled signer mints empty suffix and accepts any verify", "[core][media_token]") {
    MediaTokenSigner const s{"", std::chrono::seconds{60}};
    REQUIRE_FALSE(s.enabled());
    REQUIRE(s.sign("rendition-1").empty());
    REQUIRE(s.verify("rendition-1", "", ""));
    REQUIRE(s.verify("rendition-1", "999999", "deadbeef"));
}

TEST_CASE("Enabled signer mints a verifiable suffix", "[core][media_token]") {
    MediaTokenSigner const s{"super-secret", std::chrono::seconds{60}, fixed_clock(1'700'000'000)};
    auto const suffix = s.sign("rendition-1");
    REQUIRE(suffix.starts_with("?expires=1700000060&sig="));

    auto const parsed = parse_suffix(suffix);
    REQUIRE(s.verify("rendition-1", parsed.expires, parsed.sig));
}

TEST_CASE("Verify rejects expired token", "[core][media_token]") {
    auto const minted =
        MediaTokenSigner{"super-secret", std::chrono::seconds{1}, fixed_clock(1'700'000'000)}.sign(
            "rendition-1");
    auto const parsed = parse_suffix(minted);

    // Verifier's clock is past the expiry — reject.
    MediaTokenSigner const verifier{
        "super-secret", std::chrono::seconds{60}, fixed_clock(1'700'000'050)};
    REQUIRE_FALSE(verifier.verify("rendition-1", parsed.expires, parsed.sig));
}

TEST_CASE("Verify rejects forged sig", "[core][media_token]") {
    MediaTokenSigner const s{"super-secret", std::chrono::seconds{60}, fixed_clock(1'700'000'000)};
    auto const parsed = parse_suffix(s.sign("rendition-1"));
    auto forged = parsed;
    forged.sig.back() = (forged.sig.back() == '0') ? '1' : '0';
    REQUIRE_FALSE(s.verify("rendition-1", forged.expires, forged.sig));
}

TEST_CASE("Verify rejects token bound to a different rendition", "[core][media_token]") {
    MediaTokenSigner const s{"super-secret", std::chrono::seconds{60}, fixed_clock(1'700'000'000)};
    auto const parsed = parse_suffix(s.sign("rendition-1"));
    REQUIRE_FALSE(s.verify("rendition-2", parsed.expires, parsed.sig));
}

TEST_CASE("Verify rejects malformed expires", "[core][media_token]") {
    MediaTokenSigner const s{"super-secret", std::chrono::seconds{60}, fixed_clock(1'700'000'000)};
    auto const parsed = parse_suffix(s.sign("rendition-1"));
    REQUIRE_FALSE(s.verify("rendition-1", "not-a-number", parsed.sig));
    REQUIRE_FALSE(s.verify("rendition-1", "", parsed.sig));
    REQUIRE_FALSE(s.verify("rendition-1", parsed.expires, ""));
}

TEST_CASE("Sig changes when secret changes", "[core][media_token]") {
    auto const a =
        MediaTokenSigner{"secret-a", std::chrono::seconds{60}, fixed_clock(1'700'000'000)}.sign(
            "r");
    auto const b =
        MediaTokenSigner{"secret-b", std::chrono::seconds{60}, fixed_clock(1'700'000'000)}.sign(
            "r");
    REQUIRE(a != b);
    REQUIRE(parse_suffix(a).expires == parse_suffix(b).expires);
    REQUIRE(parse_suffix(a).sig != parse_suffix(b).sig);
}
