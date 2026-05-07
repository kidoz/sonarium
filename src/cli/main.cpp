#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <span>
#include <string>
#include <string_view>

#include "catalog/in_memory_repository.hpp"
#include "catalog/postgres_repository.hpp"
#include "catalog/repository.hpp"
#include "cli/dlna_status.hpp"
#include "cli/http_client.hpp"
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
              << "  dlna status [--url URL]       — probe a running sonarium-dlna server\n"
              << "                                  (URL defaults to http://127.0.0.1:18200)\n";
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

int cmd_dlna_status(std::string_view base_url) {
    auto parsed = sonarium::cli::parse_http_url(base_url);
    if (!parsed.has_value()) {
        std::cerr << "sonariumctl dlna status: bad URL: " << parsed.error() << '\n';
        return 2;
    }

    sonarium::cli::HttpRequest desc_req;
    desc_req.method = "GET";
    desc_req.url = *parsed;
    desc_req.url.path = "/description.xml";
    auto desc_resp = sonarium::cli::http_request(desc_req);
    if (!desc_resp.has_value()) {
        std::cerr << "sonariumctl dlna status: GET /description.xml failed: " << desc_resp.error()
                  << '\n';
        return 3;
    }
    if (desc_resp->status != 200) {
        std::cerr << "sonariumctl dlna status: /description.xml returned status "
                  << desc_resp->status << '\n';
        return 3;
    }
    auto status = sonarium::cli::parse_description_xml(desc_resp->body);
    if (!status.has_value()) {
        std::cerr << "sonariumctl dlna status: " << status.error() << '\n';
        return 3;
    }

    sonarium::cli::HttpRequest browse_req;
    browse_req.method = "POST";
    browse_req.url = *parsed;
    browse_req.url.path = "/upnp/control/content-directory";
    browse_req.headers.emplace_back("Content-Type", "text/xml; charset=\"utf-8\"");
    browse_req.headers.emplace_back("SOAPACTION",
                                    "\"urn:schemas-upnp-org:service:ContentDirectory:1#Browse\"");
    browse_req.body = sonarium::cli::build_browse_request("0", 0, 200);
    auto browse_resp = sonarium::cli::http_request(browse_req);
    if (!browse_resp.has_value()) {
        std::cerr << "sonariumctl dlna status: Browse(0) failed: " << browse_resp.error() << '\n';
        return 3;
    }
    if (browse_resp->status != 200) {
        std::cerr << "sonariumctl dlna status: Browse(0) returned status " << browse_resp->status
                  << '\n';
        return 3;
    }
    auto summary = sonarium::cli::parse_browse_response(browse_resp->body);
    if (summary.has_value()) {
        status->total_matches = summary->total_matches;
        status->number_returned = summary->number_returned;
        status->system_update_id = summary->update_id;
    } else {
        std::cerr << "sonariumctl dlna status: " << summary.error() << '\n';
        return 3;
    }

    std::cout << "url:          http://" << parsed->host << ':' << parsed->port << '\n'
              << "friendly:     " << status->friendly_name << '\n'
              << "udn:          " << status->udn << '\n'
              << "model:        " << status->model_name;
    if (!status->model_number.empty()) {
        std::cout << " (" << status->model_number << ')';
    }
    std::cout << '\n'
              << "root items:   " << status->total_matches << " total, " << status->number_returned
              << " returned\n"
              << "update_id:    " << status->system_update_id << '\n';
    return 0;
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
    if (cmd == "dlna" && args.size() >= 3 && std::string_view{args[2]} == "status") {
        std::string url = "http://127.0.0.1:18200";
        for (std::size_t i = 3; i < args.size(); ++i) {
            std::string_view const a{args[i]};
            if (a == "--url" && (i + 1) < args.size()) {
                url = std::string{args[i + 1]};
                ++i;
            } else {
                std::cerr << "sonariumctl dlna status: unknown arg '" << a << "'\n";
                return 2;
            }
        }
        return cmd_dlna_status(url);
    }

    std::cerr << "sonariumctl: command '" << cmd << "' not yet implemented\n";
    return 2;
}
