#include <catch2/catch_test_macros.hpp>
#include <chrono>

#include "media/duration_format.hpp"

using sonarium::media::format_didl_duration;
using sonarium::media::format_didl_duration_ms;

TEST_CASE("format_didl_duration_ms produces H:MM:SS.mmm", "[media][duration]") {
    REQUIRE(format_didl_duration_ms(0) == "0:00:00.000");
    REQUIRE(format_didl_duration_ms(1) == "0:00:00.001");
    REQUIRE(format_didl_duration_ms(999) == "0:00:00.999");
    REQUIRE(format_didl_duration_ms(1'000) == "0:00:01.000");
    REQUIRE(format_didl_duration_ms(60'000) == "0:01:00.000");
    REQUIRE(format_didl_duration_ms(3'600'000) == "1:00:00.000");
    REQUIRE(format_didl_duration_ms(36'000'000) == "10:00:00.000");
}

TEST_CASE("format_didl_duration_ms negative becomes zero", "[media][duration]") {
    REQUIRE(format_didl_duration_ms(-1) == "0:00:00.000");
    REQUIRE(format_didl_duration_ms(-12345) == "0:00:00.000");
}

TEST_CASE("format_didl_duration matches the ms helper", "[media][duration]") {
    auto const d = std::chrono::milliseconds{(4 * 60 * 1000) + (10 * 1000) + 500};
    REQUIRE(format_didl_duration(d) == "0:04:10.500");
}
