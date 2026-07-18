#include <atomic>
#include <atria/application.hpp>
#include <atria/middleware.hpp>
#include <atria/server_config.hpp>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "catalog/in_memory_repository.hpp"
#include "catalog/postgres_repository.hpp"
#include "catalog/repository.hpp"
#include "core/media_token.hpp"
#include "core/operator_mode.hpp"
#include "core/version.hpp"
#include "hls/playlist_builder.hpp"
#include "hls/segmenter.hpp"
#include "media/media_rendition.hpp"

namespace {

constexpr std::uint16_t default_http_port = 18201;
constexpr std::string_view default_media_base = "http://127.0.0.1:18200";

[[nodiscard]] std::string env_or(std::string_view name, std::string fallback) {
    if (auto const* v = std::getenv(std::string{name}.c_str()); v != nullptr) {
        return std::string{v};
    }
    return fallback;
}

[[nodiscard]] std::uint16_t env_port_or(std::string_view name, std::uint16_t fallback) noexcept {
    auto const* v = std::getenv(std::string{name}.c_str());
    if (v == nullptr) {
        return fallback;
    }
    auto const parsed = std::strtoul(v, nullptr, 10);
    if (parsed == 0 || parsed > 65535U) {
        return fallback;
    }
    return static_cast<std::uint16_t>(parsed);
}

// Generate a 15-second sine-wave mp3 if it isn't already on disk. Lets the
// demo catalog produce something the segmenter can actually slice when the
// server runs without Postgres + a real media library.
[[nodiscard]] std::filesystem::path ensure_demo_audio() {
    auto const path = std::filesystem::temp_directory_path() / "sonarium-demo-audio.mp3";
    std::error_code ec;
    if (std::filesystem::exists(path, ec) && std::filesystem::file_size(path, ec) > 0) {
        return path;
    }
    auto const cmd = std::string{"ffmpeg -y -loglevel error -f lavfi -i "}
                     + R"("sine=frequency=440:duration=15")" + " -ar 44100 -ac 2 " + path.string()
                     + " >/dev/null 2>&1";
    if (std::system(cmd.c_str()) != 0) {
        // Best-effort: leave the path empty so the segmenter falls back to
        // the single-segment playlist that points at /media/renditions/{id}.
        return {};
    }
    return path;
}

// Repository selection mirrors sonarium-dlna: prefer Postgres when
// SONARIUM_PG_CONNINFO is set, otherwise fall back to a tiny sample so the
// server stays demoable without a database.
[[nodiscard]] std::shared_ptr<sonarium::catalog::Repository> sample_catalog_with_track() {
    using namespace sonarium::catalog;
    using namespace sonarium::media;
    auto repo = std::make_shared<InMemoryRepository>();

    Artist a;
    a.id = "1";
    a.name = "Sample Artist";
    repo->add_artist(a);

    Album al;
    al.id = "1";
    al.artist_id = "1";
    al.title = "Demo Album";
    repo->add_album(al);

    auto const demo_audio = ensure_demo_audio();

    Track t;
    t.id = "1";
    t.album_id = "1";
    t.artist_id = "1";
    t.title = "Hello Track";
    t.duration_ms = 15'000;
    repo->add_track(t);

    MediaRendition r;
    r.id = "demo-mp3";
    r.track_id = "1";
    r.codec = AudioCodec::mp3;
    r.container = AudioContainer::mp3;
    r.mime_type = "audio/mpeg";
    r.bitrate_bps = 192'000;
    r.duration_ms = 15'000;
    r.storage_path = demo_audio.string();
    repo->add_rendition(r);

    return repo;
}

[[nodiscard]] std::shared_ptr<sonarium::catalog::Repository>
try_open_postgres_catalog(std::string_view conninfo) {
    auto repo = sonarium::catalog::PostgresRepository::open(std::string{conninfo});
    if (!repo.has_value()) {
        std::cerr << "  postgres: " << repo.error() << " — falling back to in-memory\n";
        return nullptr;
    }
    if (auto schema = (*repo)->ensure_schema(); !schema.has_value()) {
        std::cerr << "  postgres: " << schema.error() << " — falling back to in-memory\n";
        return nullptr;
    }
    return *repo;
}

[[nodiscard]] ::atria::Response not_found() {
    return ::atria::Response{::atria::Status::NotFound};
}

// SIGINT/SIGTERM → Application::shutdown(), which only stores an atomic flag
// (async-signal-safe). The accept loop notices within its poll interval, so
// `listen()` returns instead of the process dying mid-request.
std::atomic<::atria::Application*> g_shutdown_app{nullptr};

void handle_shutdown_signal(int /*signum*/) {
    if (auto* app = g_shutdown_app.load()) {
        app->shutdown();
    }
}

[[nodiscard]] ::atria::Response m3u8_ok(std::string body) {
    ::atria::Response r{::atria::Status::Ok};
    r.set_header("Content-Type", "application/vnd.apple.mpegurl");
    r.set_header("Cache-Control", "no-store");
    r.set_body(std::move(body));
    return r;
}

[[nodiscard]] std::optional<std::string> read_file(std::filesystem::path const& path) {
    std::ifstream in{path, std::ios::binary};
    if (!in) {
        return std::nullopt;
    }
    std::ostringstream buf;
    buf << in.rdbuf();
    return buf.str();
}

void register_hls_routes(::atria::Application& app,
                         std::shared_ptr<sonarium::catalog::Repository const> catalog,
                         std::shared_ptr<sonarium::core::MediaTokenSigner const> signer,
                         std::shared_ptr<sonarium::hls::Segmenter> segmenter,
                         std::string media_base_url,
                         std::string self_base_url) {
    // GET /version — basic liveness/identity probe.
    app.get("/version", [self = self_base_url](::atria::Request&) {
        auto const v = sonarium::core::current_version();
        return ::atria::Response::text(std::string{sonarium::core::product_name()} + " server "
                                       + std::to_string(v.major) + '.' + std::to_string(v.minor)
                                       + '.' + std::to_string(v.patch) + " (HLS)\n" + "self=" + self
                                       + "\n");
    });

    // GET /hls/tracks/{id}/master.m3u8 — multi-variant master playlist.
    app.get(
        "/hls/tracks/{id}/master.m3u8",
        [catalog, signer, self_base_url, media_base_url](
            ::atria::Request& req) -> ::atria::Response {
            auto const id = req.path_param("id").value_or("");
            if (id.empty()) {
                return not_found();
            }
            auto const track = catalog->get_track(id);
            if (!track.has_value()) {
                return not_found();
            }
            auto const renditions = catalog->list_renditions_for_track(id);
            if (renditions.empty()) {
                return not_found();
            }
            std::vector<sonarium::hls::MediaVariant> variants;
            variants.reserve(renditions.size());
            for (auto const& r : renditions) {
                // signer.sign() returns "" when disabled, so unconditional append is safe.
                auto media_url = media_base_url + "/media/renditions/" + r.id + signer->sign(r.id);
                variants.push_back(sonarium::hls::variant_from_rendition(r, std::move(media_url)));
            }
            return m3u8_ok(sonarium::hls::build_master_playlist(variants, self_base_url));
        });

    // GET /hls/renditions/{id}/index.m3u8 — segmented VOD playlist. First
    // request to a rendition triggers ffmpeg to write seg00000.ts ... beside
    // index.m3u8 in the cache dir; subsequent requests fast-return the cached
    // playlist. When `rendition.storage_path` is empty / missing (e.g. the
    // demo catalog without seeded files) we fall back to the single-segment
    // playlist that points at the existing /media/renditions/{id} route.
    app.get(
        "/hls/renditions/{id}/index.m3u8",
        [catalog, signer, segmenter, media_base_url](::atria::Request& req) -> ::atria::Response {
            auto const id = req.path_param("id").value_or("");
            if (id.empty()) {
                return not_found();
            }
            auto rendition = catalog->get_rendition(id);
            if (!rendition.has_value()) {
                return not_found();
            }

            // No on-disk source → fall back to single-segment playlist.
            std::error_code ec;
            if (rendition->storage_path.empty()
                || !std::filesystem::exists(rendition->storage_path, ec)) {
                auto media_url = media_base_url + "/media/renditions/" + rendition->id
                                 + signer->sign(rendition->id);
                auto const variant =
                    sonarium::hls::variant_from_rendition(*rendition, std::move(media_url));
                return m3u8_ok(sonarium::hls::build_media_playlist(variant));
            }

            // Use the rendition's natural bitrate so an ABR set with
            // different rendition.bitrate_bps values produces actually
            // distinct segment ladders. Zero falls back to the cfg default.
            auto const per_rendition_kbps =
                (rendition->bitrate_bps > 0) ? (rendition->bitrate_bps / 1000U) : 0U;
            auto playlist_path = segmenter->ensure_segments(
                std::string{id}, rendition->storage_path, per_rendition_kbps);
            if (!playlist_path.has_value()) {
                ::atria::Response r{::atria::Status::InternalServerError};
                r.set_body("segmenter: " + playlist_path.error() + "\n");
                return r;
            }
            auto body = read_file(*playlist_path);
            if (!body.has_value()) {
                return not_found();
            }
            return m3u8_ok(std::move(*body));
        });

    // GET / HEAD /hls/renditions/{id}/{seg} — serve a cached .ts segment.
    // Filename is validated against `seg\d{5}\.ts`; nothing else is allowed.
    // atria::Response::file() handles HEAD natively (no body, same headers)
    // and honors Range, so both verbs share the same lambda.
    auto segment_handler = [segmenter](::atria::Request& req) -> ::atria::Response {
        auto const id = req.path_param("id").value_or("");
        auto const seg = req.path_param("seg").value_or("");
        if (id.empty() || seg.empty()) {
            return not_found();
        }
        auto path = segmenter->cached_file(std::string{id}, seg);
        if (!path.has_value()) {
            return not_found();
        }
        ::atria::FileResponseOptions opts;
        opts.content_type = "video/mp2t";
        opts.allow_range = true;
        return ::atria::Response::file(req, *path, std::move(opts));
    };
    app.get("/hls/renditions/{id}/{seg}", segment_handler);
    app.head("/hls/renditions/{id}/{seg}", segment_handler);
}

} // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IOLBF, 0);

    auto const v = sonarium::core::current_version();
    auto const bind_host = env_or("SONARIUM_SERVER_BIND_HOST", "0.0.0.0");
    auto const http_port = env_port_or("SONARIUM_SERVER_HTTP_PORT", default_http_port);
    auto const media_base = env_or("SONARIUM_MEDIA_BASE_URL", std::string{default_media_base});
    auto const self_base =
        env_or("SONARIUM_SERVER_BASE_URL",
               std::string{"http://"} + (bind_host == "0.0.0.0" ? "127.0.0.1" : bind_host) + ":"
                   + std::to_string(http_port));
    auto const pg_conninfo = env_or("SONARIUM_PG_CONNINFO", "");
    auto const segment_cache_root =
        env_or("SONARIUM_HLS_CACHE_DIR",
               (std::filesystem::temp_directory_path() / "sonarium-hls").string());
    auto const segment_seconds_env = std::getenv("SONARIUM_HLS_SEGMENT_SECONDS");
    auto const segment_seconds = static_cast<std::uint32_t>(
        (segment_seconds_env != nullptr && std::atoi(segment_seconds_env) > 0)
            ? std::atoi(segment_seconds_env)
            : 6);
    auto segmenter = std::make_shared<sonarium::hls::Segmenter>(sonarium::hls::SegmenterConfig{
        .cache_root = std::filesystem::path{segment_cache_root},
        .segment_duration_seconds = segment_seconds,
        .bitrate_kbps = 192,
    });
    auto const token_secret = env_or("SONARIUM_MEDIA_TOKEN_SECRET", "");
    auto const token_ttl_seconds = [&]() -> std::int64_t {
        auto const* v = std::getenv("SONARIUM_MEDIA_TOKEN_TTL_SECONDS");
        if (v == nullptr) {
            return 3600;
        }
        auto const parsed = std::strtol(v, nullptr, 10);
        return (parsed > 0) ? parsed : 3600;
    }();
    auto signer = std::make_shared<sonarium::core::MediaTokenSigner>(
        token_secret, std::chrono::seconds{token_ttl_seconds});

    auto const mode = sonarium::core::current_operator_mode();
    auto const allow_public_bind = env_or("SONARIUM_ALLOW_PUBLIC_BIND", "0") != "0";
    auto const violations = sonarium::core::check_startup_invariants({
        .bind_host = bind_host,
        .media_token_secret = token_secret,
        .pg_conninfo = pg_conninfo,
        .allow_public_bind = allow_public_bind,
    });
    auto const fatal = mode == sonarium::core::OperatorMode::production && !violations.empty();
    for (auto const& vio : violations) {
        std::cerr << "  " << (fatal ? "FATAL: " : "WARN: ") << vio << '\n';
    }
    if (fatal) {
        std::cerr
            << "  refusing to start: production mode requires safe defaults — set SONARIUM_MODE="
               "development to override during local testing\n";
        return 2;
    }

    std::shared_ptr<sonarium::catalog::Repository> catalog;
    std::string catalog_kind;
    if (!pg_conninfo.empty()) {
        catalog = try_open_postgres_catalog(pg_conninfo);
        if (catalog) {
            catalog_kind = "postgres";
        } else if (mode == sonarium::core::OperatorMode::production) {
            std::cerr << "  FATAL: catalog unavailable and production mode forbids in-memory "
                         "fallback\n";
            return 3;
        }
    }
    if (!catalog) {
        catalog = sample_catalog_with_track();
        catalog_kind = "in-memory (sample)";
    }

    std::cout << sonarium::core::product_name() << " server " << v.major << '.' << v.minor << '.'
              << v.patch << " (HLS)\n"
              << "  mode=" << sonarium::core::to_string(mode) << '\n'
              << "  bind_host=" << bind_host << '\n'
              << "  http_port=" << http_port << '\n'
              << "  self_base=" << self_base << '\n'
              << "  media_base=" << media_base << '\n'
              << "  catalog=" << catalog_kind << '\n'
              << "  hls_cache=" << segment_cache_root << " (segments=" << segment_seconds << "s)\n"
              << "  media_tokens=" << (signer->enabled() ? "enabled" : "disabled");
    if (signer->enabled()) {
        std::cout << " ttl=" << token_ttl_seconds << "s";
    }
    std::cout << '\n';

    ::atria::Application app;
    g_shutdown_app.store(&app);
    std::signal(SIGINT, handle_shutdown_signal);
    std::signal(SIGTERM, handle_shutdown_signal);
    app.use(::atria::middleware::error_handler());
    app.use(::atria::middleware::request_logger());
    register_hls_routes(app,
                        std::const_pointer_cast<sonarium::catalog::Repository const>(catalog),
                        std::const_pointer_cast<sonarium::core::MediaTokenSigner const>(signer),
                        segmenter,
                        media_base,
                        self_base);

    ::atria::ServerConfig cfg;
    cfg.host = bind_host;
    cfg.port = http_port;
    cfg.worker_threads = 2;
    auto const rc = app.listen(cfg);
    g_shutdown_app.store(nullptr);
    std::cout << "  server stopped\n";
    return rc;
}
