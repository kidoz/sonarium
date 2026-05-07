#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <span>
#include <string>
#include <string_view>

#include "catalog/postgres_repository.hpp"
#include "core/version.hpp"
#include "scanner/media_scanner.hpp"

namespace {

void print_usage(std::string_view argv0) {
    std::cout << "usage: " << argv0 << " <command> [args]\n"
              << "commands:\n"
              << "  version                       — print sonariumctl version\n"
              << "  import <path>                 — scan <path> into the postgres catalog\n"
              << "                                  (uses SONARIUM_PG_CONNINFO)\n"
              << "  scan                          — TODO\n"
              << "  transcode --track-id <id>     — TODO\n"
              << "  dlna status                   — TODO\n";
}

[[nodiscard]] std::string env_or(std::string_view name, std::string fallback) {
    if (auto const* v = std::getenv(std::string{name}.c_str()); v != nullptr) {
        return std::string{v};
    }
    return fallback;
}

int cmd_import(std::string_view path) {
    auto const conninfo = env_or("SONARIUM_PG_CONNINFO", "");
    if (conninfo.empty()) {
        std::cerr << "sonariumctl import: SONARIUM_PG_CONNINFO must be set\n";
        return 2;
    }

    auto repo_result = sonarium::catalog::PostgresRepository::open(conninfo);
    if (!repo_result.has_value()) {
        std::cerr << "sonariumctl import: postgres open failed: " << repo_result.error() << '\n';
        return 3;
    }
    auto& repo = *repo_result;
    if (auto schema = repo->ensure_schema(); !schema.has_value()) {
        std::cerr << "sonariumctl import: ensure_schema failed: " << schema.error() << '\n';
        return 3;
    }

    std::cout << "sonariumctl import: scanning " << path << '\n';
    auto const report = sonarium::scanner::scan(std::filesystem::path{std::string{path}}, *repo);
    std::cout << "  artists=" << report.artists_upserted << " albums=" << report.albums_upserted
              << " tracks=" << report.tracks_upserted
              << " renditions=" << report.renditions_upserted
              << " covers=" << report.covers_upserted << " skipped=" << report.skipped_files
              << '\n';
    if (!report.errors.empty()) {
        std::cerr << "  errors (" << report.errors.size() << "):\n";
        for (auto const& e : report.errors) {
            std::cerr << "    - " << e << '\n';
        }
        return 1;
    }
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    auto args = std::span<char*>(argv, static_cast<std::size_t>(argc));
    std::string_view const argv0 = (!args.empty()) ? args[0] : "sonariumctl";

    if (args.size() < 2) {
        print_usage(argv0);
        return 1;
    }

    std::string_view const cmd = args[1];
    if (cmd == "version") {
        auto const v = sonarium::core::current_version();
        std::cout << sonarium::core::product_name() << " " << v.major << '.' << v.minor << '.'
                  << v.patch << '\n';
        return 0;
    }
    if (cmd == "import") {
        if (args.size() < 3) {
            std::cerr << "sonariumctl import: missing <path>\n";
            return 2;
        }
        return cmd_import(std::string_view{args[2]});
    }

    std::cerr << "sonariumctl: command '" << cmd << "' not yet implemented\n";
    return 2;
}
