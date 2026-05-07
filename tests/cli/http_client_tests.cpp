#include <catch2/catch_test_macros.hpp>

#include "cli/http_client.hpp"

TEST_CASE("parse_http_url accepts host only", "[cli][http]") {
    auto const u = sonarium::cli::parse_http_url("http://example.com");
    REQUIRE(u.has_value());
    REQUIRE(u->host == "example.com");
    REQUIRE(u->port == 80);
    REQUIRE(u->path == "/");
}

TEST_CASE("parse_http_url accepts host:port/path", "[cli][http]") {
    auto const u = sonarium::cli::parse_http_url("http://127.0.0.1:18200/description.xml");
    REQUIRE(u.has_value());
    REQUIRE(u->host == "127.0.0.1");
    REQUIRE(u->port == 18200);
    REQUIRE(u->path == "/description.xml");
}

TEST_CASE("parse_http_url accepts host with path no port", "[cli][http]") {
    auto const u = sonarium::cli::parse_http_url("http://nas.local/foo/bar");
    REQUIRE(u.has_value());
    REQUIRE(u->host == "nas.local");
    REQUIRE(u->port == 80);
    REQUIRE(u->path == "/foo/bar");
}

TEST_CASE("parse_http_url is case-insensitive on the scheme", "[cli][http]") {
    auto const u = sonarium::cli::parse_http_url("HTTP://example.com");
    REQUIRE(u.has_value());
    REQUIRE(u->host == "example.com");
}

TEST_CASE("parse_http_url rejects https", "[cli][http]") {
    auto const u = sonarium::cli::parse_http_url("https://example.com");
    REQUIRE_FALSE(u.has_value());
}

TEST_CASE("parse_http_url rejects empty input", "[cli][http]") {
    auto const u = sonarium::cli::parse_http_url("");
    REQUIRE_FALSE(u.has_value());
}

TEST_CASE("parse_http_url rejects empty host", "[cli][http]") {
    auto const u = sonarium::cli::parse_http_url("http:///foo");
    REQUIRE_FALSE(u.has_value());
}

TEST_CASE("parse_http_url rejects invalid port", "[cli][http]") {
    auto const u1 = sonarium::cli::parse_http_url("http://x:0");
    REQUIRE_FALSE(u1.has_value());

    auto const u2 = sonarium::cli::parse_http_url("http://x:99999");
    REQUIRE_FALSE(u2.has_value());

    auto const u3 = sonarium::cli::parse_http_url("http://x:abc");
    REQUIRE_FALSE(u3.has_value());
}
