#include "core/hmac_sha256.hpp"

#include <array>
#include <cstdint>
#include <cstring>

namespace sonarium::core {

namespace {

constexpr std::size_t block_size = 64;

} // namespace

Sha256::Digest hmac_sha256(std::string_view key, std::string_view message) noexcept {
    std::array<std::uint8_t, block_size> key_block{};
    if (key.size() > block_size) {
        auto const digest = Sha256::hash(key);
        std::memcpy(key_block.data(), digest.data(), digest.size());
    } else {
        std::memcpy(key_block.data(), key.data(), key.size());
    }

    std::array<std::uint8_t, block_size> ipad{};
    std::array<std::uint8_t, block_size> opad{};
    for (std::size_t i = 0; i < block_size; ++i) {
        ipad[i] = key_block[i] ^ 0x36U;
        opad[i] = key_block[i] ^ 0x5cU;
    }

    Sha256 inner;
    inner.update(ipad.data(), ipad.size());
    inner.update(message);
    auto const inner_digest = inner.finalize();

    Sha256 outer;
    outer.update(opad.data(), opad.size());
    outer.update(inner_digest.data(), inner_digest.size());
    return outer.finalize();
}

std::string hmac_sha256_hex(std::string_view key, std::string_view message) {
    return Sha256::to_hex(hmac_sha256(key, message));
}

} // namespace sonarium::core
