#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace sonarium::core {

// FIPS 180-4 SHA-256. Hand-rolled, validated against the published test
// vectors in `tests/core/sha256_tests.cpp`. For high-stakes contexts (PII,
// financial data) prefer libsodium; here we only sign LAN-scoped media URLs.
class Sha256 {
public:
    static constexpr std::size_t digest_size = 32;
    using Digest = std::array<std::uint8_t, digest_size>;

    Sha256() noexcept;

    void update(std::string_view data) noexcept;
    void update(std::uint8_t const* data, std::size_t len) noexcept;

    // Returns the final digest. After this call the object is reset and may
    // be reused for a new hash.
    [[nodiscard]] Digest finalize() noexcept;

    // One-shot helpers.
    [[nodiscard]] static Digest hash(std::string_view data) noexcept;
    [[nodiscard]] static Digest hash(std::uint8_t const* data, std::size_t len) noexcept;

    // Lower-case hex encoding of a digest (always 64 chars).
    [[nodiscard]] static std::string to_hex(Digest const& d);

private:
    void compress(std::uint8_t const* block) noexcept;

    std::array<std::uint32_t, 8> state_{};
    std::uint64_t bit_count_ = 0;
    std::array<std::uint8_t, 64> buffer_{};
    std::size_t buffer_len_ = 0;
};

} // namespace sonarium::core
