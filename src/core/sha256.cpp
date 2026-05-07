#include "core/sha256.hpp"

#include <cstring>

namespace sonarium::core {

namespace {

// FIPS 180-4 §4.2.2 round constants.
constexpr std::array<std::uint32_t, 64> k = {
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U,
    0xab1c5ed5U, 0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU,
    0x9bdc06a7U, 0xc19bf174U, 0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU,
    0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU, 0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
    0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U, 0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU,
    0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U, 0xa2bfe8a1U, 0xa81a664bU,
    0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U, 0x19a4c116U,
    0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U,
    0xc67178f2U,
};

// Initial hash value (FIPS 180-4 §5.3.3).
constexpr std::array<std::uint32_t, 8> initial_state = {
    0x6a09e667U,
    0xbb67ae85U,
    0x3c6ef372U,
    0xa54ff53aU,
    0x510e527fU,
    0x9b05688cU,
    0x1f83d9abU,
    0x5be0cd19U,
};

[[nodiscard]] constexpr std::uint32_t rotr(std::uint32_t x, unsigned n) noexcept {
    return (x >> n) | (x << (32U - n));
}

[[nodiscard]] constexpr std::uint32_t big_sigma0(std::uint32_t x) noexcept {
    return rotr(x, 2) ^ rotr(x, 13) ^ rotr(x, 22);
}
[[nodiscard]] constexpr std::uint32_t big_sigma1(std::uint32_t x) noexcept {
    return rotr(x, 6) ^ rotr(x, 11) ^ rotr(x, 25);
}
[[nodiscard]] constexpr std::uint32_t little_sigma0(std::uint32_t x) noexcept {
    return rotr(x, 7) ^ rotr(x, 18) ^ (x >> 3U);
}
[[nodiscard]] constexpr std::uint32_t little_sigma1(std::uint32_t x) noexcept {
    return rotr(x, 17) ^ rotr(x, 19) ^ (x >> 10U);
}

[[nodiscard]] constexpr std::uint32_t
ch(std::uint32_t x, std::uint32_t y, std::uint32_t z) noexcept {
    return (x & y) ^ (~x & z);
}
[[nodiscard]] constexpr std::uint32_t
maj(std::uint32_t x, std::uint32_t y, std::uint32_t z) noexcept {
    return (x & y) ^ (x & z) ^ (y & z);
}

[[nodiscard]] constexpr std::uint32_t load_be32(std::uint8_t const* p) noexcept {
    return (static_cast<std::uint32_t>(p[0]) << 24U) | (static_cast<std::uint32_t>(p[1]) << 16U)
           | (static_cast<std::uint32_t>(p[2]) << 8U) | static_cast<std::uint32_t>(p[3]);
}

} // namespace

Sha256::Sha256() noexcept : state_{initial_state} {}

void Sha256::compress(std::uint8_t const* block) noexcept {
    std::array<std::uint32_t, 64> w{};
    for (std::size_t i = 0; i < 16; ++i) {
        w[i] = load_be32(block + (i * 4U));
    }
    for (std::size_t i = 16; i < 64; ++i) {
        w[i] = little_sigma1(w[i - 2]) + w[i - 7] + little_sigma0(w[i - 15]) + w[i - 16];
    }

    std::uint32_t a = state_[0];
    std::uint32_t b = state_[1];
    std::uint32_t c = state_[2];
    std::uint32_t d = state_[3];
    std::uint32_t e = state_[4];
    std::uint32_t f = state_[5];
    std::uint32_t g = state_[6];
    std::uint32_t h = state_[7];

    for (std::size_t i = 0; i < 64; ++i) {
        std::uint32_t const t1 = h + big_sigma1(e) + ch(e, f, g) + k[i] + w[i];
        std::uint32_t const t2 = big_sigma0(a) + maj(a, b, c);
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
}

void Sha256::update(std::uint8_t const* data, std::size_t len) noexcept {
    bit_count_ += static_cast<std::uint64_t>(len) * 8U;
    while (len > 0) {
        auto const space = buffer_.size() - buffer_len_;
        auto const take = (len < space) ? len : space;
        std::memcpy(buffer_.data() + buffer_len_, data, take);
        buffer_len_ += take;
        data += take;
        len -= take;
        if (buffer_len_ == buffer_.size()) {
            compress(buffer_.data());
            buffer_len_ = 0;
        }
    }
}

void Sha256::update(std::string_view data) noexcept {
    update(reinterpret_cast<std::uint8_t const*>(data.data()), data.size());
}

Sha256::Digest Sha256::finalize() noexcept {
    auto const bits = bit_count_;
    std::uint8_t const pad_first = 0x80;
    update(&pad_first, 1);
    std::uint8_t const zero_byte = 0;
    while (buffer_len_ != 56) {
        update(&zero_byte, 1);
    }
    // Append 64-bit big-endian length and let update() trigger the final compress.
    bit_count_ -= 64U; // we'll re-add via update() below; cancel the auto-bump
    std::uint8_t length_be[8];
    for (int i = 7; i >= 0; --i) {
        length_be[7 - i] = static_cast<std::uint8_t>((bits >> (i * 8)) & 0xFFU);
    }
    update(length_be, sizeof(length_be));

    Digest out{};
    for (std::size_t i = 0; i < 8; ++i) {
        out[(i * 4U) + 0] = static_cast<std::uint8_t>((state_[i] >> 24U) & 0xFFU);
        out[(i * 4U) + 1] = static_cast<std::uint8_t>((state_[i] >> 16U) & 0xFFU);
        out[(i * 4U) + 2] = static_cast<std::uint8_t>((state_[i] >> 8U) & 0xFFU);
        out[(i * 4U) + 3] = static_cast<std::uint8_t>(state_[i] & 0xFFU);
    }

    // Reset for reuse.
    state_ = initial_state;
    bit_count_ = 0;
    buffer_len_ = 0;
    buffer_.fill(0);

    return out;
}

Sha256::Digest Sha256::hash(std::string_view data) noexcept {
    Sha256 h;
    h.update(data);
    return h.finalize();
}

Sha256::Digest Sha256::hash(std::uint8_t const* data, std::size_t len) noexcept {
    Sha256 h;
    h.update(data, len);
    return h.finalize();
}

std::string Sha256::to_hex(Digest const& d) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string out(d.size() * 2U, '\0');
    for (std::size_t i = 0; i < d.size(); ++i) {
        out[(i * 2U) + 0] = digits[(d[i] >> 4U) & 0x0FU];
        out[(i * 2U) + 1] = digits[d[i] & 0x0FU];
    }
    return out;
}

} // namespace sonarium::core
