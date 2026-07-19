#include <atria/application.hpp>
#include <atria/headers.hpp>
#include <atria/method.hpp>
#include <atria/request.hpp>
#include <atria/response.hpp>
#include <atria/status.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "catalog/in_memory_repository.hpp"
#include "core/media_token.hpp"
#include "hls/segmenter.hpp"
#include "media/media_rendition.hpp"
#include "server/hls_routes.hpp"

using sonarium::catalog::InMemoryRepository;
using sonarium::catalog::Repository;
using sonarium::catalog::Track;
using sonarium::core::MediaTokenSigner;
using sonarium::hls::Segmenter;
using sonarium::hls::SegmenterConfig;
using sonarium::media::AudioCodec;
using sonarium::media::AudioContainer;
using sonarium::media::MediaRendition;
using sonarium::server::HlsRoutesConfig;
using sonarium::server::register_hls_routes;

namespace {

constexpr std::string_view media_base = "http://127.0.0.1:18200";
constexpr std::string_view self_base = "http://127.0.0.1:18201";

[[nodiscard]] std::shared_ptr<InMemoryRepository>
repo_with_rendition(std::string const& rendition_id, std::string const& storage_path) {
    auto repo = std::make_shared<InMemoryRepository>();
    Track t;
    t.id = "t1";
    t.title = "Track One";
    t.duration_ms = 60'000;
    repo->add_track(t);
    MediaRendition r;
    r.id = rendition_id;
    r.track_id = "t1";
    r.codec = AudioCodec::mp3;
    r.container = AudioContainer::mp3;
    r.mime_type = "audio/mpeg";
    r.bitrate_bps = 192'000;
    r.duration_ms = 60'000;
    r.storage_path = storage_path;
    repo->add_rendition(r);
    return repo;
}

struct App {
    std::shared_ptr<Segmenter> segmenter;
    std::unique_ptr<::atria::Application> app;
};

[[nodiscard]] App make_app(const std::shared_ptr<InMemoryRepository>& repo,
                           const std::shared_ptr<MediaTokenSigner>& signer,
                           std::filesystem::path media_root = {},
                           std::filesystem::path cache_root = {}) {
    if (cache_root.empty()) {
        cache_root = std::filesystem::temp_directory_path() / "sonarium-hls-routes-test-cache";
    }
    SegmenterConfig seg_cfg;
    seg_cfg.cache_root = cache_root;
    auto segmenter = std::make_shared<Segmenter>(seg_cfg);
    auto app = std::make_unique<::atria::Application>();
    register_hls_routes(
        *app,
        std::const_pointer_cast<Repository const>(std::static_pointer_cast<Repository>(repo)),
        std::const_pointer_cast<MediaTokenSigner const>(signer),
        segmenter,
        HlsRoutesConfig{.media_base_url = std::string{media_base},
                        .self_base_url = std::string{self_base},
                        .media_root = std::move(media_root)});
    return App{std::move(segmenter), std::move(app)};
}

[[nodiscard]] std::shared_ptr<MediaTokenSigner> disabled_signer() {
    return std::make_shared<MediaTokenSigner>("");
}

[[nodiscard]] ::atria::Request
make_get(std::string path, ::atria::Request::QueryParams query = {}, std::string range = {}) {
    ::atria::Headers headers;
    headers.set("Host", "127.0.0.1");
    if (!range.empty()) {
        headers.set("Range", std::move(range));
    }
    ::atria::Request req{::atria::Method::Get, std::move(path), {}, std::move(headers), {}};
    req.set_query_params(std::move(query));
    return req;
}

// Split the "?expires=...&sig=..." suffix minted by sign() into query params.
[[nodiscard]] ::atria::Request::QueryParams token_query(std::string const& suffix) {
    auto const expires = suffix.substr(suffix.find("expires=") + 8, suffix.find("&sig=") - 9);
    auto const sig = suffix.substr(suffix.find("&sig=") + 5);
    ::atria::Request::QueryParams query;
    query.emplace_back("expires", expires);
    query.emplace_back("sig", sig);
    return query;
}

} // namespace

TEST_CASE("GET /version identifies the HLS server", "[server][hls_routes]") {
    auto ctx = make_app(repo_with_rendition("r1", ""), disabled_signer());
    auto req = make_get("/version");
    auto const res = ctx.app->dispatch(req);
    REQUIRE(res.status() == ::atria::Status::Ok);
    REQUIRE(res.body().contains("server"));
    REQUIRE(res.body().contains(self_base));
}

TEST_CASE("master playlist lists variant index URLs", "[server][hls_routes]") {
    auto ctx = make_app(repo_with_rendition("r1", ""), disabled_signer());
    auto req = make_get("/hls/tracks/t1/master.m3u8");
    auto const res = ctx.app->dispatch(req);
    REQUIRE(res.status() == ::atria::Status::Ok);
    REQUIRE(res.body().contains(std::string{self_base} + "/hls/renditions/r1/index.m3u8"));
}

TEST_CASE("master playlist for unknown track is 404", "[server][hls_routes]") {
    auto ctx = make_app(repo_with_rendition("r1", ""), disabled_signer());
    auto req = make_get("/hls/tracks/nope/master.m3u8");
    REQUIRE(ctx.app->dispatch(req).status() == ::atria::Status::NotFound);
}

TEST_CASE("index playlist falls back to single-segment when no source on disk",
          "[server][hls_routes]") {
    auto ctx = make_app(repo_with_rendition("r1", ""), disabled_signer());
    auto req = make_get("/hls/renditions/r1/index.m3u8");
    auto const res = ctx.app->dispatch(req);
    REQUIRE(res.status() == ::atria::Status::Ok);
    REQUIRE(res.body().contains(std::string{media_base} + "/media/renditions/r1"));
    REQUIRE(res.body().contains("#EXT-X-ENDLIST"));
}

TEST_CASE("every HLS route rejects untokened requests when signing is enabled",
          "[server][hls_routes]") {
    auto signer = std::make_shared<MediaTokenSigner>("test-secret");
    auto ctx = make_app(repo_with_rendition("r1", ""), signer);

    auto master = make_get("/hls/tracks/t1/master.m3u8");
    REQUIRE(ctx.app->dispatch(master).status() == ::atria::Status::Forbidden);

    auto index = make_get("/hls/renditions/r1/index.m3u8");
    REQUIRE(ctx.app->dispatch(index).status() == ::atria::Status::Forbidden);

    auto segment = make_get("/hls/renditions/r1/seg00000.ts");
    REQUIRE(ctx.app->dispatch(segment).status() == ::atria::Status::Forbidden);
}

TEST_CASE("master accepts a track-bound token and mints tokened variant URLs",
          "[server][hls_routes]") {
    auto signer = std::make_shared<MediaTokenSigner>("test-secret");
    auto ctx = make_app(repo_with_rendition("r1", ""), signer);

    auto req = make_get("/hls/tracks/t1/master.m3u8", token_query(signer->sign("t1")));
    auto const res = ctx.app->dispatch(req);
    REQUIRE(res.status() == ::atria::Status::Ok);
    REQUIRE(res.body().contains("/hls/renditions/r1/index.m3u8?expires="));
}

TEST_CASE("a token bound to a different resource is rejected", "[server][hls_routes]") {
    auto signer = std::make_shared<MediaTokenSigner>("test-secret");
    auto ctx = make_app(repo_with_rendition("r1", ""), signer);

    // Rendition-bound token presented to the master (track-bound) route.
    auto req = make_get("/hls/tracks/t1/master.m3u8", token_query(signer->sign("r1")));
    REQUIRE(ctx.app->dispatch(req).status() == ::atria::Status::Forbidden);
}

TEST_CASE("index playlist refuses storage paths outside the media root", "[server][hls_routes]") {
    namespace fs = std::filesystem;
    auto const scratch = fs::temp_directory_path() / "sonarium-hls-routes-containment";
    fs::remove_all(scratch);
    fs::create_directories(scratch / "library");
    auto const outside = scratch / "outside.mp3";
    std::ofstream{outside, std::ios::binary} << "not yours";

    auto ctx = make_app(
        repo_with_rendition("r1", outside.string()), disabled_signer(), scratch / "library");
    auto req = make_get("/hls/renditions/r1/index.m3u8");
    REQUIRE(ctx.app->dispatch(req).status() == ::atria::Status::NotFound);
    fs::remove_all(scratch);
}

TEST_CASE("cached segments are served with Range support", "[server][hls_routes]") {
    namespace fs = std::filesystem;
    auto const cache_root = fs::temp_directory_path() / "sonarium-hls-routes-seg-cache";
    fs::remove_all(cache_root);
    fs::create_directories(cache_root / "r1");
    std::ofstream{cache_root / "r1" / "seg00000.ts", std::ios::binary} << std::string(1024, 'T');

    auto ctx = make_app(repo_with_rendition("r1", ""), disabled_signer(), {}, cache_root);

    auto whole = make_get("/hls/renditions/r1/seg00000.ts");
    auto const full = ctx.app->dispatch(whole);
    REQUIRE(full.status() == ::atria::Status::Ok);
    REQUIRE(full.headers().find("Content-Type").value_or("") == "video/mp2t");

    auto ranged = make_get("/hls/renditions/r1/seg00000.ts", {}, "bytes=0-99");
    auto const partial = ctx.app->dispatch(ranged);
    REQUIRE(partial.status() == ::atria::Status::PartialContent);
    // File responses stream; the payload size is carried by content_length().
    REQUIRE(partial.content_length().value_or(0) == 100);

    auto malformed = make_get("/hls/renditions/r1/seg00000.ts", {}, "bytes=zz-");
    REQUIRE(ctx.app->dispatch(malformed).status() == ::atria::Status::RangeNotSatisfiable);

    auto unsafe = make_get("/hls/renditions/r1/..%2Findex.m3u8");
    REQUIRE(ctx.app->dispatch(unsafe).status() == ::atria::Status::NotFound);

    auto missing = make_get("/hls/renditions/r1/seg00042.ts");
    REQUIRE(ctx.app->dispatch(missing).status() == ::atria::Status::NotFound);
    fs::remove_all(cache_root);
}

namespace {

// Repository stub whose backend is permanently down — every call throws the
// error the Postgres implementation raises on a dead connection.
class DownRepository final : public sonarium::catalog::Repository {
public:
    [[nodiscard]] std::uint32_t system_update_id() const override { throw_down(); }
    [[nodiscard]] sonarium::catalog::Page<sonarium::catalog::Artist>
    list_artists(sonarium::catalog::PageRequest) const override {
        throw_down();
    }
    [[nodiscard]] std::optional<sonarium::catalog::Artist>
    get_artist(std::string_view) const override {
        throw_down();
    }
    [[nodiscard]] sonarium::catalog::Page<sonarium::catalog::Album>
    list_albums_for_artist(std::string_view, sonarium::catalog::PageRequest) const override {
        throw_down();
    }
    [[nodiscard]] std::optional<sonarium::catalog::Album>
    get_album(std::string_view) const override {
        throw_down();
    }
    [[nodiscard]] sonarium::catalog::Page<sonarium::catalog::Track>
    list_tracks_for_album(std::string_view, sonarium::catalog::PageRequest) const override {
        throw_down();
    }
    [[nodiscard]] sonarium::catalog::Page<sonarium::catalog::Track>
    list_all_tracks(sonarium::catalog::PageRequest) const override {
        throw_down();
    }
    [[nodiscard]] std::optional<sonarium::catalog::Track>
    get_track(std::string_view) const override {
        throw_down();
    }
    [[nodiscard]] std::vector<sonarium::media::MediaRendition>
    list_renditions_for_track(std::string_view) const override {
        throw_down();
    }
    [[nodiscard]] std::optional<sonarium::media::MediaRendition>
    get_rendition(std::string_view) const override {
        throw_down();
    }
    [[nodiscard]] sonarium::catalog::Page<sonarium::catalog::Playlist>
    list_playlists(sonarium::catalog::PageRequest) const override {
        throw_down();
    }
    [[nodiscard]] std::optional<sonarium::catalog::Playlist>
    get_playlist(std::string_view) const override {
        throw_down();
    }
    [[nodiscard]] std::optional<sonarium::catalog::StorageAsset>
    get_asset(std::string_view) const override {
        throw_down();
    }

private:
    [[noreturn]] static void throw_down() {
        throw sonarium::catalog::RepositoryError{"postgres: connection lost"};
    }
};

} // namespace

TEST_CASE("readiness reflects catalog availability", "[server][hls_routes]") {
    auto ctx = make_app(repo_with_rendition("r1", ""), disabled_signer());

    auto health = make_get("/healthz");
    REQUIRE(ctx.app->dispatch(health).status() == ::atria::Status::Ok);

    auto ready = make_get("/readyz");
    REQUIRE(ctx.app->dispatch(ready).status() == ::atria::Status::Ok);

    // Same routes with a dead backend: alive but not ready.
    auto down_repo = std::make_shared<DownRepository>();
    SegmenterConfig seg_cfg;
    seg_cfg.cache_root = std::filesystem::temp_directory_path() / "sonarium-readyz-cache";
    auto down_app = std::make_unique<::atria::Application>();
    register_hls_routes(
        *down_app,
        std::const_pointer_cast<Repository const>(std::static_pointer_cast<Repository>(down_repo)),
        disabled_signer(),
        std::make_shared<Segmenter>(seg_cfg),
        HlsRoutesConfig{.media_base_url = std::string{media_base},
                        .self_base_url = std::string{self_base},
                        .media_root = {}});

    auto down_health = make_get("/healthz");
    REQUIRE(down_app->dispatch(down_health).status() == ::atria::Status::Ok);

    auto down_ready = make_get("/readyz");
    auto const res = down_app->dispatch(down_ready);
    REQUIRE(static_cast<std::uint16_t>(res.status()) == 503);
    REQUIRE(res.body().contains("catalog unavailable"));
}
