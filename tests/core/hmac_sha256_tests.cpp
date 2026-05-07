#include <catch2/catch_test_macros.hpp>
#include <string>

#include "core/hmac_sha256.hpp"

using sonarium::core::hmac_sha256_hex;

// RFC 4231 HMAC-SHA256 test vectors.

TEST_CASE("HMAC-SHA256 RFC 4231 case 1", "[core][hmac]") {
    std::string const key(20, '\x0b');
    REQUIRE(hmac_sha256_hex(key, "Hi There")
            == "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7");
}

TEST_CASE("HMAC-SHA256 RFC 4231 case 2 (key='Jefe')", "[core][hmac]") {
    REQUIRE(hmac_sha256_hex("Jefe", "what do ya want for nothing?")
            == "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");
}

TEST_CASE("HMAC-SHA256 RFC 4231 case 3", "[core][hmac]") {
    std::string const key(20, '\xaa');
    std::string const data(50, '\xdd');
    REQUIRE(hmac_sha256_hex(key, data)
            == "773ea91e36800e46854db8ebd09181a72959098b3ef8c122d9635514ced565fe");
}

TEST_CASE("HMAC-SHA256 RFC 4231 case 6 (long key)", "[core][hmac]") {
    std::string const key(131, '\xaa');
    REQUIRE(hmac_sha256_hex(key, "Test Using Larger Than Block-Size Key - Hash Key First")
            == "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54");
}

TEST_CASE("HMAC-SHA256 RFC 4231 case 7 (long key + long message)", "[core][hmac]") {
    std::string const key(131, '\xaa');
    std::string const message =
        "This is a test using a larger than block-size key and a larger than "
        "block-size data. The key needs to be hashed before being used by the "
        "HMAC algorithm.";
    REQUIRE(hmac_sha256_hex(key, message)
            == "9b09ffa71b942fcb27635fbcd5b0e944bfdc63644f0713938a7f51535c3a35e2");
}
