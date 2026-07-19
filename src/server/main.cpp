#include <atomic>
#include <atria/application.hpp>
#include <atria/middleware.hpp>
#include <atria/server_config.hpp>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "catalog/catalog_factory.hpp"
#include "catalog/in_memory_repository.hpp"
#include "catalog/repository.hpp"
#include "core/env_config.hpp"
#include "core/media_token.hpp"
#include "core/operator_mode.hpp"
#include "core/version.hpp"
#include "hls/segmenter.hpp"
#include "media/media_rendition.hpp"
#include "server/hls_routes.hpp"

namespace {

constexpr std::uint16_t default_http_port = 18201;
constexpr std::string_view default_media_base = "http://127.0.0.1:18200";

[[nodiscard]] std::string env_or(std::string_view name, std::string fallback) {
    if (auto const* v = std::getenv(std::string{name}.c_str()); v != nullptr) {
        return std::string{v};
    }
    return fallback;
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

// SIGINT/SIGTERM → Application::shutdown(), which only stores an atomic flag
// (async-signal-safe). The accept loop notices within its poll interval, so
// `listen()` returns instead of the process dying mid-request.
std::atomic<::atria::Application*> g_shutdown_app{nullptr};

void handle_shutdown_signal(int /*signum*/) {
    if (auto* app = g_shutdown_app.load()) {
        app->shutdown();
    }
}

} // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IOLBF, 0);

    auto const v = sonarium::core::current_version();
    // Malformed values are collected rather than silently defaulted: WARN in
    // development, refuse-to-start in production (alongside the invariants).
    std::vector<std::string> config_issues;
    using sonarium::core::checked_env_int;
    auto const bind_host = env_or("SONARIUM_SERVER_BIND_HOST", "0.0.0.0");
    auto const http_port = static_cast<std::uint16_t>(
        checked_env_int("SONARIUM_SERVER_HTTP_PORT", default_http_port, 1, 65535, config_issues));
    auto const media_base = env_or("SONARIUM_MEDIA_BASE_URL", std::string{default_media_base});
    auto const self_base =
        env_or("SONARIUM_SERVER_BASE_URL",
               std::string{"http://"} + (bind_host == "0.0.0.0" ? "127.0.0.1" : bind_host) + ":"
                   + std::to_string(http_port));
    auto const pg_conninfo = env_or("SONARIUM_PG_CONNINFO", "");
    auto const sqlite_path = env_or("SONARIUM_SQLITE_PATH", "");
    auto const media_root = env_or("SONARIUM_MEDIA_ROOT", "");
    auto const segment_cache_root =
        env_or("SONARIUM_HLS_CACHE_DIR",
               (std::filesystem::temp_directory_path() / "sonarium-hls").string());
    auto const segment_seconds = static_cast<std::uint32_t>(
        checked_env_int("SONARIUM_HLS_SEGMENT_SECONDS", 6, 1, 60, config_issues));
    // Cache budget in MiB; 0 disables eviction. Default 8 GiB.
    auto const cache_max_bytes =
        static_cast<std::uint64_t>(
            checked_env_int("SONARIUM_HLS_CACHE_MAX_MB", 8192, 0, 100'000'000, config_issues))
        * 1024 * 1024;
    auto const max_transcodes = static_cast<std::uint32_t>(
        checked_env_int("SONARIUM_HLS_MAX_TRANSCODES", 2, 1, 64, config_issues));
    auto segmenter = std::make_shared<sonarium::hls::Segmenter>(sonarium::hls::SegmenterConfig{
        .cache_root = std::filesystem::path{segment_cache_root},
        .segment_duration_seconds = segment_seconds,
        .bitrate_kbps = 192,
        .max_cache_bytes = cache_max_bytes,
        .max_concurrent_transcodes = max_transcodes,
    });
    auto const token_secret = env_or("SONARIUM_MEDIA_TOKEN_SECRET", "");
    auto const token_ttl_seconds =
        checked_env_int("SONARIUM_MEDIA_TOKEN_TTL_SECONDS", 3600, 1, 86'400 * 30, config_issues);
    auto signer = std::make_shared<sonarium::core::MediaTokenSigner>(
        token_secret, std::chrono::seconds{token_ttl_seconds});

    auto const mode = sonarium::core::current_operator_mode();
    auto const allow_public_bind = env_or("SONARIUM_ALLOW_PUBLIC_BIND", "0") != "0";
    auto const violations = sonarium::core::check_startup_invariants({
        .bind_host = bind_host,
        .media_token_secret = token_secret,
        .pg_conninfo = pg_conninfo,
        .sqlite_path = sqlite_path,
        .media_root = media_root,
        .allow_public_bind = allow_public_bind,
    });
    auto const fatal = mode == sonarium::core::OperatorMode::production
                       && !(violations.empty() && config_issues.empty());
    for (auto const& issue : config_issues) {
        std::cerr << "  " << (fatal ? "FATAL: " : "WARN: ") << issue << '\n';
    }
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
    if (auto opened = sonarium::catalog::open_catalog_from_env(); !opened.has_value()) {
        std::cerr << "  catalog: " << opened.error() << " — falling back to in-memory\n";
        if (mode == sonarium::core::OperatorMode::production) {
            std::cerr << "  FATAL: catalog unavailable and production mode forbids in-memory "
                         "fallback\n";
            return 3;
        }
    } else if (opened->has_value()) {
        catalog = (*opened)->repository;
        catalog_kind = (*opened)->kind;
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
              << "  media_root=" << (media_root.empty() ? "<unset — containment off>" : media_root)
              << '\n'
              << "  hls_cache=" << segment_cache_root << " (segments=" << segment_seconds
              << "s, cap="
              << (cache_max_bytes == 0 ? "unbounded"
                                       : std::to_string(cache_max_bytes / (1024 * 1024)) + "MiB")
              << ", transcodes<=" << max_transcodes << ")\n"
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
    sonarium::server::register_hls_routes(
        app,
        std::const_pointer_cast<sonarium::catalog::Repository const>(catalog),
        std::const_pointer_cast<sonarium::core::MediaTokenSigner const>(signer),
        segmenter,
        {.media_base_url = media_base,
         .self_base_url = self_base,
         .media_root = std::filesystem::path{media_root}});

    ::atria::ServerConfig cfg;
    cfg.host = bind_host;
    cfg.port = http_port;
    // Enough headroom that the bounded transcode slots can block their own
    // requests without starving playlist/segment traffic.
    cfg.worker_threads = 8;
    auto const rc = app.listen(cfg);
    g_shutdown_app.store(nullptr);
    std::cout << "  server stopped\n";
    return rc;
}
