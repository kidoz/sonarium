#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <string>

#include "core/sha256.hpp"

using sonarium::core::Sha256;

namespace {

[[nodiscard]] std::string hash_hex(std::string_view data) {
    return Sha256::to_hex(Sha256::hash(data));
}

} // namespace

// FIPS 180-2 / NIST published SHA-256 test vectors.

TEST_CASE("SHA-256: empty string", "[core][sha256]") {
    REQUIRE(hash_hex("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

TEST_CASE("SHA-256: 'abc'", "[core][sha256]") {
    REQUIRE(hash_hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST_CASE("SHA-256: 448-bit message", "[core][sha256]") {
    REQUIRE(hash_hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")
            == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

TEST_CASE("SHA-256: 896-bit message", "[core][sha256]") {
    REQUIRE(hash_hex("abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmn"
                     "hijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu")
            == "cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1");
}

TEST_CASE("SHA-256: million 'a'", "[core][sha256]") {
    Sha256 h;
    std::string const block(1000, 'a');
    for (int i = 0; i < 1000; ++i) {
        h.update(block);
    }
    REQUIRE(Sha256::to_hex(h.finalize())
            == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

TEST_CASE("SHA-256: incremental update matches one-shot", "[core][sha256]") {
    Sha256 h;
    h.update("Sonarium ");
    h.update("DLNA ");
    h.update("server");
    REQUIRE(Sha256::to_hex(h.finalize()) == hash_hex("Sonarium DLNA server"));
}

TEST_CASE("SHA-256: object reset after finalize", "[core][sha256]") {
    Sha256 h;
    h.update("first");
    auto const d1 = h.finalize();
    h.update("second");
    auto const d2 = h.finalize();
    REQUIRE(Sha256::to_hex(d1) == hash_hex("first"));
    REQUIRE(Sha256::to_hex(d2) == hash_hex("second"));
}
