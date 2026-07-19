#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <logspine/level.hpp>
#include <string>
#include <vector>

#include "core/env_config.hpp"
#include "core/logspine_logger.hpp"

using sonarium::core::checked_env_int;

namespace {

constexpr char const* test_var = "SONARIUM_TEST_ENV_CONFIG";

struct EnvGuard {
    EnvGuard() { ::unsetenv(test_var); }
    ~EnvGuard() { ::unsetenv(test_var); }
    EnvGuard(EnvGuard const&) = delete;
    EnvGuard& operator=(EnvGuard const&) = delete;
    EnvGuard(EnvGuard&&) = delete;
    EnvGuard& operator=(EnvGuard&&) = delete;
};

} // namespace

TEST_CASE("checked_env_int returns the fallback when unset, without an issue",
          "[core][env_config]") {
    EnvGuard const guard;
    std::vector<std::string> issues;
    REQUIRE(checked_env_int(test_var, 42, 0, 100, issues) == 42);
    REQUIRE(issues.empty());
}

TEST_CASE("checked_env_int parses valid values", "[core][env_config]") {
    EnvGuard const guard;
    ::setenv(test_var, "8200", 1);
    std::vector<std::string> issues;
    REQUIRE(checked_env_int(test_var, 42, 1, 65535, issues) == 8200);
    REQUIRE(issues.empty());
}

TEST_CASE("checked_env_int reports malformed values and falls back", "[core][env_config]") {
    EnvGuard const guard;
    ::setenv(test_var, "eight-thousand", 1);
    std::vector<std::string> issues;
    REQUIRE(checked_env_int(test_var, 42, 1, 65535, issues) == 42);
    REQUIRE(issues.size() == 1);
    REQUIRE(issues.front().contains(test_var));

    ::setenv(test_var, "8200extra", 1); // trailing junk is not a valid integer
    REQUIRE(checked_env_int(test_var, 42, 1, 65535, issues) == 42);
    REQUIRE(issues.size() == 2);
}

TEST_CASE("checked_env_int reports out-of-range values and falls back", "[core][env_config]") {
    EnvGuard const guard;
    ::setenv(test_var, "99999", 1);
    std::vector<std::string> issues;
    REQUIRE(checked_env_int(test_var, 8200, 1, 65535, issues) == 8200);
    REQUIRE(issues.size() == 1);
    REQUIRE(issues.front().contains("outside"));
}

TEST_CASE("parse_log_level maps names case-insensitively", "[core][logging]") {
    using sonarium::core::parse_log_level;
    REQUIRE(parse_log_level("debug") == ::logspine::level::debug);
    REQUIRE(parse_log_level("INFO") == ::logspine::level::info);
    REQUIRE(parse_log_level("Warn") == ::logspine::level::warn);
    REQUIRE(parse_log_level("warning") == ::logspine::level::warn);
    REQUIRE(parse_log_level("error") == ::logspine::level::error);
}

TEST_CASE("parse_log_level falls back on unknown input", "[core][logging]") {
    using sonarium::core::parse_log_level;
    REQUIRE(parse_log_level("") == ::logspine::level::info);
    REQUIRE(parse_log_level("verbose") == ::logspine::level::info);
    REQUIRE(parse_log_level("chatty", ::logspine::level::warn) == ::logspine::level::warn);
}
