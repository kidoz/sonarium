#include <charconv>
#include <cstdlib>
#include <expected>
#include <filesystem>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <system_error>

#include "catalog/catalog_factory.hpp"
#include "catalog/in_memory_repository.hpp"
#include "catalog/repository.hpp"
#include "cli/dlna_status.hpp"
#include "cli/http_client.hpp"
#include "core/version.hpp"
#include "scanner/media_scanner.hpp"
#include "transcode/ffmpeg_command.hpp"
#include "transcode/ffmpeg_runner.hpp"
#include "transcode/transcode_request.hpp"

namespace {

void print_usage(std::string_view argv0) {
    std::cout
        << "usage: " << argv0 << " <command> [args]\n"
        << "commands:\n"
        << "  version                       — print sonariumctl version\n"
        << "  import <path>                 — scan <path> into the configured catalog\n"
        << "                                  (SONARIUM_PG_CONNINFO or SONARIUM_SQLITE_PATH)\n"
        << "  scan <path>                   — dry-run preview: walk <path> with an\n"
        << "                                  in-memory catalog, print the tree, no DB\n"
        << "  transcode --track-id <id>     — re-encode a track via ffmpeg, write a new\n"
        << "    [--codec mp3|aac]             rendition row in the configured catalog.\n"
        << "    [--bitrate <kbps>]            Defaults: codec=mp3, bitrate=128.\n"
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
    if (!sonarium::catalog::catalog_backend_configured()) {
        std::cerr << "sonariumctl import: set SONARIUM_PG_CONNINFO or SONARIUM_SQLITE_PATH\n";
        return 2;
    }
    auto opened = sonarium::catalog::open_catalog_from_env();
    if (!opened.has_value()) {
        std::cerr << "sonariumctl import: catalog open failed: " << opened.error() << '\n';
        return 3;
    }
    auto repo = (*opened)->writer;

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

struct TranscodeArgs {
    std::string track_id;
    sonarium::transcode::TargetCodec codec = sonarium::transcode::TargetCodec::mp3;
    std::uint32_t bitrate_kbps = 128;
};

[[nodiscard]] std::expected<TranscodeArgs, std::string>
parse_transcode_args(std::span<char*> args) {
    TranscodeArgs out;
    bool track_seen = false;
    for (std::size_t i = 2; i < args.size(); ++i) {
        std::string_view const a{args[i]};
        if (a == "--track-id" && (i + 1) < args.size()) {
            out.track_id = std::string{args[i + 1]};
            track_seen = true;
            ++i;
        } else if (a == "--codec" && (i + 1) < args.size()) {
            std::string_view const v{args[i + 1]};
            if (v == "mp3") {
                out.codec = sonarium::transcode::TargetCodec::mp3;
            } else if (v == "aac") {
                out.codec = sonarium::transcode::TargetCodec::aac_lc;
            } else {
                return std::unexpected(std::string{"unknown --codec '"} + std::string{v}
                                       + "' (expected: mp3|aac)");
            }
            ++i;
        } else if (a == "--bitrate" && (i + 1) < args.size()) {
            std::string_view const v{args[i + 1]};
            std::uint32_t kbps = 0;
            auto const* first = v.data();
            auto const* last = v.data() + v.size();
            auto const r = std::from_chars(first, last, kbps);
            if (r.ec != std::errc{} || kbps == 0) {
                return std::unexpected(std::string{"invalid --bitrate '"} + std::string{v} + "'");
            }
            out.bitrate_kbps = kbps;
            ++i;
        } else {
            return std::unexpected(std::string{"unknown arg '"} + std::string{a} + "'");
        }
    }
    if (!track_seen) {
        return std::unexpected("missing --track-id");
    }
    return out;
}

int cmd_transcode(std::span<char*> args) {
    auto parsed = parse_transcode_args(args);
    if (!parsed.has_value()) {
        std::cerr << "sonariumctl transcode: " << parsed.error() << '\n';
        return 2;
    }

    if (!sonarium::catalog::catalog_backend_configured()) {
        std::cerr << "sonariumctl transcode: set SONARIUM_PG_CONNINFO or SONARIUM_SQLITE_PATH\n";
        return 2;
    }
    auto opened = sonarium::catalog::open_catalog_from_env();
    if (!opened.has_value()) {
        std::cerr << "sonariumctl transcode: catalog open failed: " << opened.error() << '\n';
        return 3;
    }
    auto repo = (*opened)->repository;
    auto writer = (*opened)->writer;

    auto const track = repo->get_track(parsed->track_id);
    if (!track.has_value()) {
        std::cerr << "sonariumctl transcode: track '" << parsed->track_id << "' not found\n";
        return 3;
    }
    auto const sources = repo->list_renditions_for_track(parsed->track_id);
    if (sources.empty()) {
        std::cerr << "sonariumctl transcode: track has no source rendition\n";
        return 3;
    }
    auto const& source = sources.front();

    sonarium::transcode::TranscodeRequest req;
    req.input_path = source.storage_path;
    req.output_path = sonarium::transcode::output_path_for(source.storage_path, parsed->codec);
    req.codec = parsed->codec;
    req.bitrate_kbps = parsed->bitrate_kbps;

    if (req.input_path == req.output_path) {
        std::cerr << "sonariumctl transcode: refusing to transcode in place ('" << req.input_path
                  << "')\n";
        return 3;
    }

    std::cout << "sonariumctl transcode: " << req.input_path << " -> " << req.output_path
              << " (codec=" << sonarium::transcode::extension_for(parsed->codec)
              << " bitrate=" << parsed->bitrate_kbps << "k)\n";

    auto run = sonarium::transcode::run_ffmpeg(req);
    if (!run.has_value()) {
        std::cerr << "sonariumctl transcode: " << run.error() << '\n';
        return 3;
    }
    if (run->exit_code != 0) {
        std::cerr << "sonariumctl transcode: ffmpeg exited " << run->exit_code << "\n"
                  << run->stderr_excerpt;
        return 3;
    }

    std::error_code ec;
    auto const size = std::filesystem::file_size(req.output_path, ec);
    if (ec) {
        std::cerr << "sonariumctl transcode: stat output: " << ec.message() << '\n';
        return 3;
    }

    std::string const ext{sonarium::transcode::extension_for(parsed->codec)};
    sonarium::media::RenditionMime const rm{
        sonarium::transcode::to_audio_codec(parsed->codec),
        sonarium::transcode::to_audio_container(parsed->codec),
    };
    std::string const mime{sonarium::media::default_mime_for(rm)};

    sonarium::media::MediaRendition out{};
    out.id = parsed->track_id + ":" + ext + ":" + std::to_string(parsed->bitrate_kbps);
    out.track_id = parsed->track_id;
    out.codec = rm.codec;
    out.container = rm.container;
    out.mime_type = mime;
    out.bitrate_bps = parsed->bitrate_kbps * 1000;
    out.sample_rate_hz = source.sample_rate_hz;
    out.bit_depth = source.bit_depth;
    out.channels = source.channels;
    out.duration_ms = source.duration_ms;
    out.size_bytes = size;
    out.storage_path = req.output_path;
    out.protocol_info = std::string{"http-get:*:"} + mime + ":*";
    if (auto pn = sonarium::media::dlna_org_pn_for(rm); pn.has_value()) {
        out.dlna_profile_name = std::string{*pn};
    }
    out.purpose = sonarium::media::RenditionPurpose::dlna_lossy;

    if (auto u = writer->upsert_rendition(out); !u.has_value()) {
        std::cerr << "sonariumctl transcode: upsert_rendition failed: " << u.error() << '\n';
        return 3;
    }
    if (auto b = writer->bump_system_update_id(); !b.has_value()) {
        std::cerr << "sonariumctl transcode: bump_system_update_id failed: " << b.error() << '\n';
        return 3;
    }

    std::cout << "  rendition_id=" << out.id << " size=" << size << " bytes\n";
    return 0;
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
    if (cmd == "transcode") {
        return cmd_transcode(args);
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
