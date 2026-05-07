#include <atria/application.hpp>
#include <atria/headers.hpp>
#include <atria/method.hpp>
#include <atria/request.hpp>
#include <atria/response.hpp>
#include <atria/status.hpp>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "catalog/in_memory_repository.hpp"
#include "composition/dlna_server.hpp"
#include "composition/http_routes.hpp"
#include "composition/injector.hpp"
#include "composition/service_config.hpp"
#include "core/media_token.hpp"
#include "core/null_logger.hpp"
#include "dlna-core/connection_manager_handler.hpp"
#include "media/media_rendition.hpp"

using sonarium::catalog::InMemoryRepository;
using sonarium::catalog::Repository;
using sonarium::catalog::Track;
using sonarium::composition::DlnaServer;
using sonarium::composition::make_dlna_server;
using sonarium::composition::register_dlna_routes;
using sonarium::composition::ServiceConfig;
using sonarium::core::MediaTokenSigner;
using sonarium::media::AudioCodec;
using sonarium::media::AudioContainer;
using sonarium::media::MediaRendition;

namespace {

class TempFile {
public:
    TempFile(std::string_view name, std::string contents) : contents_{std::move(contents)} {
        path_ = std::filesystem::temp_directory_path() / name;
        std::ofstream out{path_, std::ios::binary | std::ios::trunc};
        out.write(contents_.data(), static_cast<std::streamsize>(contents_.size()));
    }
    ~TempFile() {
        std::error_code ec;
        std::filesystem::remove(path_, ec);
    }
    TempFile(TempFile const&) = delete;
    TempFile& operator=(TempFile const&) = delete;
    TempFile(TempFile&&) noexcept = delete;
    TempFile& operator=(TempFile&&) noexcept = delete;

    [[nodiscard]] std::string path_string() const { return path_.string(); }

private:
    std::filesystem::path path_;
    std::string contents_;
};

[[nodiscard]] std::shared_ptr<Repository> repo_with_rendition(std::string id,
                                                              std::string storage_path) {
    auto repo = std::make_shared<InMemoryRepository>();

    Track t;
    t.id = "1";
    t.title = "Demo";
    repo->add_track(t);

    MediaRendition r;
    r.id = std::move(id);
    r.track_id = "1";
    r.codec = AudioCodec::mp3;
    r.container = AudioContainer::mp3;
    r.storage_path = std::move(storage_path);
    repo->add_rendition(r);
    return repo;
}

[[nodiscard]] ServiceConfig sample_config_with_signer(std::shared_ptr<MediaTokenSigner> signer) {
    ServiceConfig cfg;
    cfg.device.friendly_name = "Sonarium";
    cfg.device.manufacturer = "Kidoz";
    cfg.device.model_name = "sonarium-dlna";
    cfg.device.udn = "uuid:abcd";
    cfg.base_url = "http://lan.invalid:8200";
    cfg.protocol_info_catalog = sonarium::dlna::default_protocol_info_catalog();
    cfg.media_token_signer = std::move(signer);
    return cfg;
}

[[nodiscard]] std::unique_ptr<::atria::Application>
build_app_with_signer(std::shared_ptr<Repository> repo, std::shared_ptr<MediaTokenSigner> signer) {
    auto cfg = sample_config_with_signer(signer);
    auto profiles = std::make_shared<sonarium::dlna::DeviceProfileRegistry>(
        sonarium::dlna::DeviceProfileRegistry::with_defaults());
    auto server =
        make_dlna_server(repo, profiles, sonarium::core::make_null_logger(), std::move(cfg));
    auto app = std::make_unique<::atria::Application>();
    register_dlna_routes(*app,
                         std::const_pointer_cast<DlnaServer const>(server),
                         std::const_pointer_cast<Repository const>(repo));
    return app;
}

[[nodiscard]] ::atria::Request make_get(std::string path,
                                        ::atria::Request::QueryParams query = {}) {
    ::atria::Headers headers;
    headers.set("Host", "127.0.0.1");
    headers.set("User-Agent", "VLC/3.0");
    ::atria::Request req{::atria::Method::Get, std::move(path), {}, std::move(headers), {}};
    req.set_query_params(std::move(query));
    return req;
}

} // namespace

TEST_CASE("Token-protected route accepts a valid signed URL", "[composition][http][media][token]") {
    TempFile const tmp{"sonarium-token-ok.bin", "OKBYTES"};
    auto signer = std::make_shared<MediaTokenSigner>(
        "shared-secret", std::chrono::seconds{60}, [] { return std::int64_t{1'700'000'000}; });

    auto app = build_app_with_signer(repo_with_rendition("demo", tmp.path_string()), signer);
    auto const suffix = signer->sign("demo");
    REQUIRE(suffix.starts_with("?expires="));

    auto const expires =
        std::string(suffix.substr(suffix.find("expires=") + 8, suffix.find("&sig=") - 9));
    auto const sig = std::string(suffix.substr(suffix.find("&sig=") + 5));

    ::atria::Request::QueryParams query;
    query.emplace_back("expires", expires);
    query.emplace_back("sig", sig);
    auto req = make_get("/media/renditions/demo", std::move(query));
    auto const res = app->dispatch(req);
    REQUIRE(res.status() == ::atria::Status::Ok);
    REQUIRE(res.headers().find("Content-Type").value_or("") == "audio/mpeg");
}

TEST_CASE("Token-protected route rejects missing token with 403",
          "[composition][http][media][token]") {
    TempFile const tmp{"sonarium-token-missing.bin", "X"};
    auto signer = std::make_shared<MediaTokenSigner>("secret", std::chrono::seconds{60});
    auto app = build_app_with_signer(repo_with_rendition("demo", tmp.path_string()), signer);
    auto req = make_get("/media/renditions/demo");
    auto const res = app->dispatch(req);
    REQUIRE(res.status() == ::atria::Status::Forbidden);
}

TEST_CASE("Token-protected route rejects forged sig with 403",
          "[composition][http][media][token]") {
    TempFile const tmp{"sonarium-token-forged.bin", "X"};
    auto signer = std::make_shared<MediaTokenSigner>(
        "secret", std::chrono::seconds{60}, [] { return std::int64_t{1'700'000'000}; });
    auto app = build_app_with_signer(repo_with_rendition("demo", tmp.path_string()), signer);

    ::atria::Request::QueryParams query;
    query.emplace_back("expires", "1700000060");
    query.emplace_back("sig", std::string(64, '0'));
    auto req = make_get("/media/renditions/demo", std::move(query));
    auto const res = app->dispatch(req);
    REQUIRE(res.status() == ::atria::Status::Forbidden);
}

TEST_CASE("Token-protected route rejects expired token with 403",
          "[composition][http][media][token]") {
    TempFile const tmp{"sonarium-token-expired.bin", "X"};
    // Mint at t=0 with 1-second TTL …
    auto minter = std::make_shared<MediaTokenSigner>(
        "secret", std::chrono::seconds{1}, [] { return std::int64_t{0}; });
    auto const suffix = minter->sign("demo");
    auto const expires =
        std::string(suffix.substr(suffix.find("expires=") + 8, suffix.find("&sig=") - 9));
    auto const sig = std::string(suffix.substr(suffix.find("&sig=") + 5));

    // … verifier's clock is well past the expiry.
    auto verifier = std::make_shared<MediaTokenSigner>(
        "secret", std::chrono::seconds{60}, [] { return std::int64_t{1'000'000}; });
    auto app = build_app_with_signer(repo_with_rendition("demo", tmp.path_string()), verifier);

    ::atria::Request::QueryParams query;
    query.emplace_back("expires", expires);
    query.emplace_back("sig", sig);
    auto req = make_get("/media/renditions/demo", std::move(query));
    auto const res = app->dispatch(req);
    REQUIRE(res.status() == ::atria::Status::Forbidden);
}

TEST_CASE("Disabled signer treats route as unprotected", "[composition][http][media][token]") {
    TempFile const tmp{"sonarium-token-off.bin", "X"};
    auto signer = std::make_shared<MediaTokenSigner>(""); // empty secret => disabled
    auto app = build_app_with_signer(repo_with_rendition("demo", tmp.path_string()), signer);

    auto req = make_get("/media/renditions/demo");
    auto const res = app->dispatch(req);
    REQUIRE(res.status() == ::atria::Status::Ok);
}

TEST_CASE("DIDL-Lite minted URLs include the token suffix", "[composition][http][media][token]") {
    TempFile const tmp{"sonarium-token-didl.bin", "X"};
    auto signer = std::make_shared<MediaTokenSigner>(
        "secret", std::chrono::seconds{60}, [] { return std::int64_t{1'700'000'000}; });

    auto repo = std::make_shared<InMemoryRepository>();
    using sonarium::catalog::Album;
    using sonarium::catalog::Artist;
    Artist a;
    a.id = "1";
    a.name = "T";
    repo->add_artist(a);
    Album al;
    al.id = "1";
    al.artist_id = "1";
    al.title = "T";
    repo->add_album(al);
    Track t;
    t.id = "1";
    t.album_id = "1";
    t.title = "T";
    t.duration_ms = 1000;
    repo->add_track(t);
    MediaRendition r;
    r.id = "demo";
    r.track_id = "1";
    r.codec = AudioCodec::mp3;
    r.container = AudioContainer::mp3;
    r.duration_ms = 1000;
    r.storage_path = tmp.path_string();
    repo->add_rendition(r);

    auto app = build_app_with_signer(std::static_pointer_cast<Repository>(repo), signer);

    constexpr auto browse = R"(<?xml version="1.0"?>
<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/">
  <s:Body>
    <u:Browse xmlns:u="urn:schemas-upnp-org:service:ContentDirectory:1">
      <ObjectID>album:1</ObjectID>
      <BrowseFlag>BrowseDirectChildren</BrowseFlag>
      <Filter>*</Filter>
      <StartingIndex>0</StartingIndex>
      <RequestedCount>200</RequestedCount>
      <SortCriteria></SortCriteria>
    </u:Browse>
  </s:Body>
</s:Envelope>)";

    ::atria::Headers headers;
    headers.set("Host", "127.0.0.1");
    headers.set("User-Agent", "VLC/3.0");
    headers.set("Content-Type", "text/xml");
    ::atria::Request req{
        ::atria::Method::Post, "/upnp/control/content-directory", {}, std::move(headers), browse};
    auto const res = app->dispatch(req);
    REQUIRE(res.status() == ::atria::Status::Ok);
    INFO("response body: " << res.body());
    // The DIDL is double-escaped inside <Result>: `?` -> `?`, `&` -> `&amp;amp;`
    // (ampersand once for the DIDL-XML, then once for the SOAP envelope wrapping it).
    REQUIRE(res.body().find("/media/renditions/demo?expires=1700000060") != std::string::npos);
    REQUIRE(res.body().find("sig=") != std::string::npos);
}
