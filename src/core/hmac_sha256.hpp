#pragma once

#include <string>
#include <string_view>

#include "core/sha256.hpp"

namespace sonarium::core {

// HMAC-SHA256 (RFC 2104). Key length is unbounded; keys longer than 64 bytes
// are pre-hashed per spec.
[[nodiscard]] Sha256::Digest hmac_sha256(std::string_view key, std::string_view message) noexcept;

// Convenience: hex-encoded HMAC-SHA256.
[[nodiscard]] std::string hmac_sha256_hex(std::string_view key, std::string_view message);

} // namespace sonarium::core
