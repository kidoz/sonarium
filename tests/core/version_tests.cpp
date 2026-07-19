#include <catch2/catch_test_macros.hpp>

#include "core/version.hpp"

using sonarium::core::current_version;
using sonarium::core::product_name;
using sonarium::core::server_signature;
using sonarium::core::Version;

TEST_CASE("current_version returns the project semver", "[core][version]") {
    auto const v = current_version();
    REQUIRE(v.major >= 0);
    REQUIRE(v.minor >= 0);
    REQUIRE(v.patch >= 0);
}

TEST_CASE("product_name is the canonical identifier", "[core][version]") {
    REQUIRE(product_name() == "Sonarium");
}

TEST_CASE("server signature is SSDP-friendly", "[core][version]") {
    auto const sig = server_signature();
    REQUIRE(sig.starts_with("Sonarium/"));
    REQUIRE(sig.contains("UPnP/1.0"));
    REQUIRE(sig.contains("dlna/"));
}

TEST_CASE("Version equality is value-based", "[core][version]") {
    Version const a{1, 2, 3};
    Version const b{1, 2, 3};
    Version const c{1, 2, 4};
    REQUIRE(a == b);
    REQUIRE_FALSE(a == c);
}
