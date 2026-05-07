#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <span>
#include <string>
#include <string_view>

#include "catalog/in_memory_repository.hpp"
#include "catalog/postgres_repository.hpp"
#include "catalog/repository.hpp"
#include "core/version.hpp"
#include "scanner/media_scanner.hpp"

namespace {

void print_usage(std::string_view argv0) {
    std::cout << "usage: " << argv0 << " <command> [args]\n"
              << "commands:\n"
              << "  version                       — print sonariumctl version\n"
              << "  import <path>                 — scan <path> into the postgres catalog\n"
              << "                                  (uses SONARIUM_PG_CONNINFO)\n"
              << "  scan <path>                   — dry-run preview: walk <path> with an\n"
              << "                                  in-memory catalog, print the tree, no DB\n"
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

void print_scan_tree(sonarium::catalog::Repository const& repo) {
    constexpr sonarium::catalog::PageRequest unbounded{0, 0};
    auto const artists = repo.list_artists(unbounded);
    for (auto const& artist : artists.rows) {
        std::cout << artist.name << "\n";
        auto const albums = repo.list_albums_for_artist(artist.id, unbounded);
        for (auto const& album : albums.rows) {
            std::cout << "  " << album.title;
            if (album.release_year.has_value()) {
                std::cout << " (" << *album.release_year << ')';
            }
            if (album.cover_art_asset_id.has_value()) {
                std::cout << " [cover]";
            }
            std::cout << '\n';
            auto const tracks = repo.list_tracks_for_album(album.id, unbounded);
            for (auto const& track : tracks.rows) {
                std::cout << "    ";
                if (track.track_number.has_value()) {
                    std::cout << *track.track_number << ". ";
                }
                std::cout << track.title;
                if (track.duration_ms != 0) {
                    auto const seconds = track.duration_ms / 1000;
                    std::cout << "  [" << (seconds / 60) << ':';
                    auto const rem = seconds % 60;
                    std::cout << (rem < 10 ? "0" : "") << rem << ']';
                }
                std::cout << '\n';
            }
        }
    }
}

int cmd_scan(std::string_view path) {
    sonarium::catalog::InMemoryRepository repo;

    std::cout << "sonariumctl scan: walking " << path << " (dry-run, no DB writes)\n";
    auto const report = sonarium::scanner::scan(std::filesystem::path{std::string{path}}, repo);

    if (!report.errors.empty()) {
        std::cerr << "errors (" << report.errors.size() << "):\n";
        for (auto const& e : report.errors) {
            std::cerr << "  - " << e << '\n';
        }
    }

    if (report.tracks_upserted > 0) {
        std::cout << "\n";
        print_scan_tree(repo);
    }

    std::cout << "\nsummary: artists=" << report.artists_upserted
              << " albums=" << report.albums_upserted << " tracks=" << report.tracks_upserted
              << " renditions=" << report.renditions_upserted
              << " covers=" << report.covers_upserted << " skipped=" << report.skipped_files
              << '\n';

    return report.errors.empty() ? 0 : 1;
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
    if (cmd == "scan") {
        if (args.size() < 3) {
            std::cerr << "sonariumctl scan: missing <path>\n";
            return 2;
        }
        return cmd_scan(std::string_view{args[2]});
    }

    std::cerr << "sonariumctl: command '" << cmd << "' not yet implemented\n";
    return 2;
}
