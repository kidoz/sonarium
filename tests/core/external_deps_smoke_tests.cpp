#include <catch2/catch_test_macros.hpp>

#include "core/external_deps_smoke.hpp"

using sonarium::core::external_deps_smoke;

TEST_CASE("Atria, ctorwire, logspine, asterorm link cleanly from core", "[core][external_deps]") {
    auto const info = external_deps_smoke();

    REQUIRE(info.atria_ok_reason == "OK");
    REQUIRE(info.logspine_info_level == 2);
    REQUIRE(info.logspine_info_label == "info");
    REQUIRE(info.ctorwire_scope_constructible);
    // asterorm::classify_sqlstate("") == db_error_kind::unknown == 8 (last enum
    // value). The exact number isn't load-bearing — the assertion only needs
    // to confirm the symbol resolves.
    REQUIRE(info.asterorm_unknown_kind == 8);
}
