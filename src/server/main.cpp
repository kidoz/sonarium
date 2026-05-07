#include <atria/application.hpp>
#include <atria/middleware.hpp>
#include <atria/server_config.hpp>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "catalog/in_memory_repository.hpp"
#include "catalog/postgres_repository.hpp"
#include "catalog/repository.hpp"
#include "core/media_token.hpp"
#include "core/version.hpp"
#include "hls/playlist_builder.hpp"
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

    Track t;
    t.id = "1";
    t.album_id = "1";
    t.artist_id = "1";
    t.title = "Hello Track";
    t.duration_ms = 240'000;
    repo->add_track(t);

    MediaRendition r;
    r.id = "demo-mp3";
    r.track_id = "1";
    r.codec = AudioCodec::mp3;
    r.container = AudioContainer::mp3;
    r.mime_type = "audio/mpeg";
    r.bitrate_bps = 320'000;
    r.duration_ms = 240'000;
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

[[nodiscard]] ::atria::Response m3u8_ok(std::string body) {
    ::atria::Response r{::atria::Status::Ok};
    r.set_header("Content-Type", "application/vnd.apple.mpegurl");
    r.set_header("Cache-Control", "no-store");
    r.set_body(std::move(body));
    return r;
}

void register_hls_routes(::atria::Application& app,
                         std::shared_ptr<sonarium::catalog::Repository const> catalog,
                         std::shared_ptr<sonarium::core::MediaTokenSigner const> signer,
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

    // GET /hls/renditions/{id}/index.m3u8 — single-variant VOD media playlist.
    app.get("/hls/renditions/{id}/index.m3u8",
            [catalog, signer, media_base_url](::atria::Request& req) -> ::atria::Response {
                auto const id = req.path_param("id").value_or("");
                if (id.empty()) {
                    return not_found();
                }
                auto rendition = catalog->get_rendition(id);
                if (!rendition.has_value()) {
                    return not_found();
                }
                auto media_url = media_base_url + "/media/renditions/" + rendition->id
                                 + signer->sign(rendition->id);
                auto const variant =
                    sonarium::hls::variant_from_rendition(*rendition, std::move(media_url));
                return m3u8_ok(sonarium::hls::build_media_playlist(variant));
            });
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

    std::shared_ptr<sonarium::catalog::Repository> catalog;
    std::string catalog_kind;
    if (!pg_conninfo.empty()) {
        catalog = try_open_postgres_catalog(pg_conninfo);
        if (catalog) {
            catalog_kind = "postgres";
        }
    }
    if (!catalog) {
        catalog = sample_catalog_with_track();
        catalog_kind = "in-memory (sample)";
    }

    std::cout << sonarium::core::product_name() << " server " << v.major << '.' << v.minor << '.'
              << v.patch << " (HLS)\n"
              << "  bind_host=" << bind_host << '\n'
              << "  http_port=" << http_port << '\n'
              << "  self_base=" << self_base << '\n'
              << "  media_base=" << media_base << '\n'
              << "  catalog=" << catalog_kind << '\n'
              << "  media_tokens=" << (signer->enabled() ? "enabled" : "disabled");
    if (signer->enabled()) {
        std::cout << " ttl=" << token_ttl_seconds << "s";
    }
    std::cout << '\n';

    ::atria::Application app;
    app.use(::atria::middleware::error_handler());
    app.use(::atria::middleware::request_logger());
    register_hls_routes(app,
                        std::const_pointer_cast<sonarium::catalog::Repository const>(catalog),
                        std::const_pointer_cast<sonarium::core::MediaTokenSigner const>(signer),
                        media_base,
                        self_base);

    ::atria::ServerConfig cfg;
    cfg.host = bind_host;
    cfg.port = http_port;
    cfg.worker_threads = 2;
    return app.listen(cfg);
}
