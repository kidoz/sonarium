#include <catch2/catch_test_macros.hpp>
#include <string>

#include "core/operator_mode.hpp"

using sonarium::core::check_startup_invariants;
using sonarium::core::OperatorMode;
using sonarium::core::parse_operator_mode;
using sonarium::core::StartupInvariants;
using sonarium::core::to_string;

TEST_CASE("parse_operator_mode recognises production aliases", "[core][operator_mode]") {
    REQUIRE(parse_operator_mode("production") == OperatorMode::production);
    REQUIRE(parse_operator_mode("PRODUCTION") == OperatorMode::production);
    REQUIRE(parse_operator_mode("Prod") == OperatorMode::production);
}

TEST_CASE("parse_operator_mode defaults to development", "[core][operator_mode]") {
    REQUIRE(parse_operator_mode("") == OperatorMode::development);
    REQUIRE(parse_operator_mode("development") == OperatorMode::development);
    REQUIRE(parse_operator_mode("dev") == OperatorMode::development);
    REQUIRE(parse_operator_mode("staging") == OperatorMode::development); // unknown → safe
}

TEST_CASE("to_string round-trips both modes", "[core][operator_mode]") {
    REQUIRE(to_string(OperatorMode::production) == "production");
    REQUIRE(to_string(OperatorMode::development) == "development");
}

TEST_CASE("safe production config produces no violations", "[core][operator_mode]") {
    StartupInvariants const inv{
        .bind_host = "192.168.1.10",
        .media_token_secret = "rotating-32-byte-secret",
        .pg_conninfo = "host=db user=sonarium dbname=sonarium",
        .media_root = "/srv/music",
        .allow_public_bind = false,
    };
    REQUIRE(check_startup_invariants(inv).empty());
}

TEST_CASE("wildcard bind_host triggers a violation by default", "[core][operator_mode]") {
    StartupInvariants inv{
        .bind_host = "0.0.0.0",
        .media_token_secret = "ok",
        .pg_conninfo = "ok",
        .media_root = "/srv/music",
        .allow_public_bind = false,
    };
    auto const v = check_startup_invariants(inv);
    REQUIRE(v.size() == 1);
    REQUIRE(v.front().starts_with("bind:"));

    inv.bind_host = "::";
    REQUIRE(check_startup_invariants(inv).size() == 1);

    inv.bind_host = "";
    REQUIRE(check_startup_invariants(inv).size() == 1);
}

TEST_CASE("allow_public_bind suppresses the bind violation", "[core][operator_mode]") {
    StartupInvariants const inv{
        .bind_host = "0.0.0.0",
        .media_token_secret = "ok",
        .pg_conninfo = "ok",
        .media_root = "/srv/music",
        .allow_public_bind = true,
    };
    REQUIRE(check_startup_invariants(inv).empty());
}

TEST_CASE("empty media token secret triggers a violation", "[core][operator_mode]") {
    StartupInvariants const inv{
        .bind_host = "192.168.1.10",
        .media_token_secret = "",
        .pg_conninfo = "ok",
        .media_root = "/srv/music",
        .allow_public_bind = false,
    };
    auto const v = check_startup_invariants(inv);
    REQUIRE(v.size() == 1);
    REQUIRE(v.front().starts_with("media_tokens:"));
}

TEST_CASE("empty pg_conninfo triggers a violation", "[core][operator_mode]") {
    StartupInvariants const inv{
        .bind_host = "192.168.1.10",
        .media_token_secret = "ok",
        .pg_conninfo = "",
        .media_root = "/srv/music",
        .allow_public_bind = false,
    };
    auto const v = check_startup_invariants(inv);
    REQUIRE(v.size() == 1);
    REQUIRE(v.front().starts_with("catalog:"));
}

TEST_CASE("multiple violations are reported together", "[core][operator_mode]") {
    StartupInvariants const inv{
        .bind_host = "0.0.0.0",
        .media_token_secret = "",
        .pg_conninfo = "",
        .allow_public_bind = false,
    };
    REQUIRE(check_startup_invariants(inv).size() == 4);
}

TEST_CASE("a SQLite path satisfies the catalog invariant", "[core][operator_mode]") {
    StartupInvariants const inv{
        .bind_host = "192.168.1.10",
        .media_token_secret = "rotating-32-byte-secret",
        .pg_conninfo = "",
        .sqlite_path = "/var/lib/sonarium/catalog.db",
        .media_root = "/srv/music",
        .allow_public_bind = false,
    };
    REQUIRE(check_startup_invariants(inv).empty());
}
