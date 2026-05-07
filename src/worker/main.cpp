#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "catalog/postgres_repository.hpp"
#include "core/version.hpp"
#include "scanner/media_scanner.hpp"

namespace {

[[nodiscard]] std::string env_or(std::string_view name, std::string fallback) {
    if (auto const* v = std::getenv(std::string{name}.c_str()); v != nullptr) {
        return std::string{v};
    }
    return fallback;
}

[[nodiscard]] std::optional<std::string> flag_value(std::span<char* const> args,
                                                    std::string_view flag) noexcept {
    for (std::size_t i = 1; i + 1 < args.size(); ++i) {
        if (std::strcmp(args[i], std::string{flag}.c_str()) == 0) {
            return std::string{args[i + 1]};
        }
    }
    return std::nullopt;
}

void print_report(sonarium::scanner::ScanReport const& r) {
    std::cout << "  artists=" << r.artists_upserted << " albums=" << r.albums_upserted
              << " tracks=" << r.tracks_upserted << " renditions=" << r.renditions_upserted
              << " covers=" << r.covers_upserted << " skipped=" << r.skipped_files << '\n';
    if (!r.errors.empty()) {
        std::cerr << "  errors (" << r.errors.size() << "):\n";
        for (auto const& e : r.errors) {
            std::cerr << "    - " << e << '\n';
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    auto args = std::span<char* const>(argv, static_cast<std::size_t>(argc));
    auto const v = sonarium::core::current_version();
    std::cout << sonarium::core::product_name() << " worker " << v.major << '.' << v.minor << '.'
              << v.patch << '\n';

    auto const root_arg = flag_value(args, "--root");
    auto const root = root_arg.value_or(env_or("SONARIUM_MEDIA_ROOT", ""));
    if (root.empty()) {
        std::cerr << "  no media root supplied (--root <path> or SONARIUM_MEDIA_ROOT)\n";
        return 2;
    }

    auto const conninfo = env_or("SONARIUM_PG_CONNINFO", "");
    if (conninfo.empty()) {
        std::cerr << "  SONARIUM_PG_CONNINFO must be set so the worker can write rows\n";
        return 2;
    }

    std::cout << "  root=" << root << '\n' << "  backend=postgres\n";

    auto repo_result = sonarium::catalog::PostgresRepository::open(conninfo);
    if (!repo_result.has_value()) {
        std::cerr << "  postgres open failed: " << repo_result.error() << '\n';
        return 3;
    }
    auto repo = *repo_result;
    if (auto schema = repo->ensure_schema(); !schema.has_value()) {
        std::cerr << "  ensure_schema failed: " << schema.error() << '\n';
        return 3;
    }

    auto const report = sonarium::scanner::scan(std::filesystem::path{root}, *repo);
    print_report(report);
    return report.errors.empty() ? 0 : 1;
}
