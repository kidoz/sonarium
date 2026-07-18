#include <atria/application.hpp>
#include <atria/headers.hpp>
#include <atria/method.hpp>
#include <atria/request.hpp>
#include <atria/response.hpp>
#include <atria/status.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
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
#include "core/null_logger.hpp"
#include "dlna-core/connection_manager_handler.hpp"
#include "media/media_rendition.hpp"

using sonarium::catalog::Album;
using sonarium::catalog::Artist;
using sonarium::catalog::InMemoryRepository;
using sonarium::catalog::Repository;
using sonarium::catalog::Track;
using sonarium::composition::DlnaServer;
using sonarium::composition::make_dlna_server;
using sonarium::composition::register_dlna_routes;
using sonarium::composition::ServiceConfig;
using sonarium::media::AudioCodec;
using sonarium::media::AudioContainer;
using sonarium::media::MediaRendition;

namespace {

[[nodiscard]] std::shared_ptr<Repository> sample_repo() {
    auto repo = std::make_shared<InMemoryRepository>();

    Artist a;
    a.id = "1";
    a.name = "Stones";
    repo->add_artist(a);

    Album al;
    al.id = "1";
    al.artist_id = "1";
    al.title = "Let It Bleed";
    repo->add_album(al);

    Track t;
    t.id = "1";
    t.album_id = "1";
    t.artist_id = "1";
    t.title = "Gimme Shelter";
    t.duration_ms = 270'000;
    t.track_number = std::uint16_t{1};
    repo->add_track(t);

    MediaRendition r;
    r.id = "r1";
    r.track_id = "1";
    r.codec = AudioCodec::mp3;
    r.container = AudioContainer::mp3;
    r.bitrate_bps = 320'000;
    r.duration_ms = 270'000;
    repo->add_rendition(r);

    return repo;
}

[[nodiscard]] ServiceConfig sample_config() {
    ServiceConfig cfg;
    cfg.device.friendly_name = "Sonarium";
    cfg.device.manufacturer = "Kidoz";
    cfg.device.model_name = "sonarium-dlna";
    cfg.device.udn = "uuid:abcdef01-2345-6789-abcd-ef0123456789";
    cfg.base_url = "http://192.168.1.10:8200";
    cfg.protocol_info_catalog = sonarium::dlna::default_protocol_info_catalog();
    return cfg;
}

[[nodiscard]] ::atria::Request make_request(::atria::Method method,
                                            std::string path,
                                            std::string body = {},
                                            std::string ua = "VLC/3.0") {
    ::atria::Headers headers;
    headers.set("Host", "127.0.0.1");
    if (!ua.empty()) {
        headers.set("User-Agent", std::move(ua));
    }
    headers.set("Content-Type", "text/xml; charset=utf-8");
    return ::atria::Request{method, std::move(path), {}, std::move(headers), std::move(body)};
}

[[nodiscard]] std::string sample_browse_request_body() {
    return R"(<?xml version="1.0" encoding="utf-8"?>
<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/">
  <s:Body>
    <u:Browse xmlns:u="urn:schemas-upnp-org:service:ContentDirectory:1">
      <ObjectID>0</ObjectID>
      <BrowseFlag>BrowseDirectChildren</BrowseFlag>
      <Filter>*</Filter>
      <StartingIndex>0</StartingIndex>
      <RequestedCount>200</RequestedCount>
      <SortCriteria></SortCriteria>
    </u:Browse>
  </s:Body>
</s:Envelope>)";
}

[[nodiscard]] std::unique_ptr<::atria::Application> build_app(std::shared_ptr<Repository> repo) {
    auto server = make_dlna_server(repo, sample_config());
    auto app = std::make_unique<::atria::Application>();
    register_dlna_routes(*app,
                         std::const_pointer_cast<DlnaServer const>(server),
                         std::const_pointer_cast<Repository const>(repo));
    return app;
}

[[nodiscard]] std::unique_ptr<::atria::Application> build_app() {
    return build_app(sample_repo());
}

} // namespace

TEST_CASE("GET /description.xml returns the device description", "[composition][http]") {
    auto app = build_app();
    auto req = make_request(::atria::Method::Get, "/description.xml");
    auto const res = app->dispatch(req);

    REQUIRE(res.status() == ::atria::Status::Ok);
    REQUIRE(res.body().find("<deviceType>urn:schemas-upnp-org:device:MediaServer:1</deviceType>")
            != std::string::npos);
    REQUIRE(res.body().find("<friendlyName>Sonarium</friendlyName>") != std::string::npos);
}

TEST_CASE("HEAD /description.xml returns no body but the right Content-Type",
          "[composition][http]") {
    auto app = build_app();
    auto req = make_request(::atria::Method::Head, "/description.xml");
    auto const res = app->dispatch(req);

    REQUIRE(res.status() == ::atria::Status::Ok);
    REQUIRE(res.body().empty());
    auto const ct = res.headers().find("Content-Type");
    REQUIRE(ct.has_value());
    REQUIRE(*ct == "application/xml");
}

TEST_CASE("GET /ContentDirectory/scpd.xml returns the SCPD", "[composition][http]") {
    auto app = build_app();
    auto req = make_request(::atria::Method::Get, "/ContentDirectory/scpd.xml");
    auto const res = app->dispatch(req);

    REQUIRE(res.status() == ::atria::Status::Ok);
    REQUIRE(res.body().find("<name>Browse</name>") != std::string::npos);
    REQUIRE(res.body().find("<name>GetSystemUpdateID</name>") != std::string::npos);
}

TEST_CASE("GET /ConnectionManager/scpd.xml returns the SCPD", "[composition][http]") {
    auto app = build_app();
    auto req = make_request(::atria::Method::Get, "/ConnectionManager/scpd.xml");
    auto const res = app->dispatch(req);

    REQUIRE(res.status() == ::atria::Status::Ok);
    REQUIRE(res.body().find("<name>GetProtocolInfo</name>") != std::string::npos);
    REQUIRE(res.body().find("<name>SourceProtocolInfo</name>") != std::string::npos);
}

TEST_CASE("POST /upnp/control/content-directory dispatches Browse", "[composition][http]") {
    auto app = build_app();
    auto req = make_request(
        ::atria::Method::Post, "/upnp/control/content-directory", sample_browse_request_body());
    auto const res = app->dispatch(req);

    REQUIRE(res.status() == ::atria::Status::Ok);
    REQUIRE(res.body().find("<u:BrowseResponse xmlns:u=\""
                            "urn:schemas-upnp-org:service:ContentDirectory:1\">")
            != std::string::npos);
    REQUIRE(res.body().find("<NumberReturned>2</NumberReturned>") != std::string::npos);
    REQUIRE(res.body().find("&lt;DIDL-Lite") != std::string::npos);
}

TEST_CASE("POST /upnp/control/connection-manager dispatches GetProtocolInfo",
          "[composition][http]") {
    auto app = build_app();
    auto const body = R"(<?xml version="1.0"?>
<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/">
  <s:Body>
    <u:GetProtocolInfo xmlns:u="urn:schemas-upnp-org:service:ConnectionManager:1"/>
  </s:Body>
</s:Envelope>)";
    auto req = make_request(::atria::Method::Post, "/upnp/control/connection-manager", body);
    auto const res = app->dispatch(req);

    REQUIRE(res.status() == ::atria::Status::Ok);
    REQUIRE(res.body().find("<u:GetProtocolInfoResponse xmlns:u=\""
                            "urn:schemas-upnp-org:service:ConnectionManager:1\">")
            != std::string::npos);
    REQUIRE(res.body().find("audio/mpeg") != std::string::npos);
}

TEST_CASE("Unknown path returns 404", "[composition][http]") {
    auto app = build_app();
    auto req = make_request(::atria::Method::Get, "/no-such-route");
    auto const res = app->dispatch(req);
    REQUIRE(res.status() == ::atria::Status::NotFound);
}

namespace {

// Lightweight RAII fixture for media-route tests: writes `contents` to a uniquely
// named file under the OS temp dir and removes it on destruction.
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
    [[nodiscard]] std::size_t size() const noexcept { return contents_.size(); }
    [[nodiscard]] std::string const& contents() const noexcept { return contents_; }

private:
    std::filesystem::path path_;
    std::string contents_;
};

// Pull a streaming Response's body by draining its chunk provider into a string.
// Works for both full-file and Range responses produced by atria::Response::file.
[[nodiscard]] std::string drain_streaming_body(::atria::Response& res) {
    if (!res.is_streaming()) {
        return res.body();
    }
    auto provider = res.take_chunk_provider();
    std::string out;
    while (true) {
        auto chunk = provider();
        if (!chunk.has_value() || chunk->empty()) {
            break;
        }
        out.append(*chunk);
    }
    return out;
}

[[nodiscard]] std::shared_ptr<Repository>
repo_with_rendition(std::string id,
                    std::string storage_path,
                    AudioCodec codec = AudioCodec::mp3,
                    AudioContainer cont = AudioContainer::mp3) {
    auto repo = std::make_shared<InMemoryRepository>();

    Track t;
    t.id = "1";
    t.title = "Demo";
    t.duration_ms = 1'000;
    repo->add_track(t);

    MediaRendition r;
    r.id = std::move(id);
    r.track_id = "1";
    r.codec = codec;
    r.container = cont;
    r.storage_path = std::move(storage_path);
    repo->add_rendition(r);

    return repo;
}

} // namespace

TEST_CASE("GET /media/renditions/{id} returns the file body", "[composition][http][media]") {
    TempFile const tmp{"sonarium-media-get.bin", "ABCDEFGHIJ0123456789"};
    auto app = build_app(repo_with_rendition("demo", tmp.path_string()));
    auto req = make_request(::atria::Method::Get, "/media/renditions/demo");
    auto res = app->dispatch(req);

    REQUIRE(res.status() == ::atria::Status::Ok);
    REQUIRE(res.headers().find("Content-Type").value_or("") == "audio/mpeg");
    REQUIRE(res.headers().find("Accept-Ranges").value_or("") == "bytes");
    // For streaming responses Content-Length lives in Response::content_length(),
    // not in the Headers map; the runtime renders it onto the wire.
    REQUIRE(res.content_length().value_or(0) == tmp.size());
    REQUIRE(drain_streaming_body(res) == tmp.contents());
}

TEST_CASE("HEAD /media/renditions/{id} returns headers, no body", "[composition][http][media]") {
    TempFile const tmp{"sonarium-media-head.bin", "0123456789"};
    auto app = build_app(repo_with_rendition("demo", tmp.path_string()));
    auto req = make_request(::atria::Method::Head, "/media/renditions/demo");
    auto const res = app->dispatch(req);

    REQUIRE(res.status() == ::atria::Status::Ok);
    REQUIRE(res.body().empty());
    REQUIRE(res.headers().find("Content-Type").value_or("") == "audio/mpeg");
    REQUIRE(res.headers().find("Content-Length").value_or("") == "10");
    REQUIRE(res.headers().find("Accept-Ranges").value_or("") == "bytes");
}

TEST_CASE("GET media with Range returns 206 Partial Content", "[composition][http][media]") {
    TempFile const tmp{"sonarium-media-range.bin", "ABCDEFGHIJ0123456789"};
    auto app = build_app(repo_with_rendition("demo", tmp.path_string()));

    ::atria::Headers headers;
    headers.set("Host", "127.0.0.1");
    headers.set("User-Agent", "VLC/3.0");
    headers.set("Range", "bytes=0-9");
    ::atria::Request req{
        ::atria::Method::Get, "/media/renditions/demo", {}, std::move(headers), {}};
    auto res = app->dispatch(req);

    REQUIRE(res.status() == ::atria::Status::PartialContent);
    REQUIRE(res.headers().find("Content-Range").value_or("") == "bytes 0-9/20");
    REQUIRE(res.content_length().value_or(0) == 10);
    REQUIRE(drain_streaming_body(res) == "ABCDEFGHIJ");
}

TEST_CASE("Unknown rendition id returns 404", "[composition][http][media]") {
    auto app = build_app(repo_with_rendition("demo", "/this/file/does/not/exist"));
    auto req = make_request(::atria::Method::Get, "/media/renditions/unknown");
    auto const res = app->dispatch(req);
    REQUIRE(res.status() == ::atria::Status::NotFound);
}

TEST_CASE("Missing storage file returns 404 even for known id", "[composition][http][media]") {
    auto app = build_app(repo_with_rendition("demo", "/this/file/does/not/exist"));
    auto req = make_request(::atria::Method::Get, "/media/renditions/demo");
    auto const res = app->dispatch(req);
    REQUIRE(res.status() == ::atria::Status::NotFound);
}

TEST_CASE("FLAC rendition resolves to audio/flac", "[composition][http][media]") {
    TempFile const tmp{"sonarium-media-flac.bin", "FLACBYTES"};
    auto app = build_app(repo_with_rendition(
        "flac-demo", tmp.path_string(), AudioCodec::flac, AudioContainer::flac));
    auto req = make_request(::atria::Method::Head, "/media/renditions/flac-demo");
    auto const res = app->dispatch(req);

    REQUIRE(res.status() == ::atria::Status::Ok);
    REQUIRE(res.headers().find("Content-Type").value_or("") == "audio/flac");
}

namespace {

[[nodiscard]] std::shared_ptr<Repository> repo_with_album_art(std::string album_id,
                                                              std::string asset_id,
                                                              std::string storage_path,
                                                              std::string mime_type) {
    auto repo = std::make_shared<InMemoryRepository>();

    Album al;
    al.id = album_id;
    al.title = "Cover Album";
    al.cover_art_asset_id = asset_id;
    repo->add_album(al);

    sonarium::catalog::StorageAsset asset;
    asset.id = std::move(asset_id);
    asset.storage_path = std::move(storage_path);
    asset.mime_type = std::move(mime_type);
    repo->add_asset(std::move(asset));

    return repo;
}

} // namespace

TEST_CASE("GET /art/albums/{id} returns the cover art bytes", "[composition][http][album_art]") {
    TempFile const tmp{"sonarium-art-get.bin", "JPEGBYTES_HERE"};
    auto app = build_app(repo_with_album_art("1", "art-1", tmp.path_string(), "image/jpeg"));
    auto req = make_request(::atria::Method::Get, "/art/albums/1");
    auto res = app->dispatch(req);

    REQUIRE(res.status() == ::atria::Status::Ok);
    REQUIRE(res.headers().find("Content-Type").value_or("") == "image/jpeg");
    REQUIRE(res.content_length().value_or(0) == tmp.size());
    REQUIRE(drain_streaming_body(res) == tmp.contents());
}

TEST_CASE("HEAD /art/albums/{id} returns headers, no body", "[composition][http][album_art]") {
    TempFile const tmp{"sonarium-art-head.bin", "PNGBYTES"};
    auto app = build_app(repo_with_album_art("1", "art-1", tmp.path_string(), "image/png"));
    auto req = make_request(::atria::Method::Head, "/art/albums/1");
    auto const res = app->dispatch(req);

    REQUIRE(res.status() == ::atria::Status::Ok);
    REQUIRE(res.body().empty());
    REQUIRE(res.headers().find("Content-Type").value_or("") == "image/png");
    REQUIRE(res.headers().find("Content-Length").value_or("") == std::to_string(tmp.size()));
}

TEST_CASE("Unknown album id returns 404", "[composition][http][album_art]") {
    TempFile const tmp{"sonarium-art-404.bin", "X"};
    auto app = build_app(repo_with_album_art("1", "art-1", tmp.path_string(), "image/jpeg"));
    auto req = make_request(::atria::Method::Get, "/art/albums/missing");
    auto const res = app->dispatch(req);
    REQUIRE(res.status() == ::atria::Status::NotFound);
}

TEST_CASE("Album without cover_art_asset_id returns 404", "[composition][http][album_art]") {
    auto repo = std::make_shared<InMemoryRepository>();
    Album al;
    al.id = "1";
    al.title = "No Cover";
    repo->add_album(al);

    auto app = build_app(std::static_pointer_cast<Repository>(repo));
    auto req = make_request(::atria::Method::Get, "/art/albums/1");
    auto const res = app->dispatch(req);
    REQUIRE(res.status() == ::atria::Status::NotFound);
}

TEST_CASE("Album with missing asset file returns 404", "[composition][http][album_art]") {
    auto app =
        build_app(repo_with_album_art("1", "art-1", "/this/file/does/not/exist", "image/jpeg"));
    auto req = make_request(::atria::Method::Get, "/art/albums/1");
    auto const res = app->dispatch(req);
    REQUIRE(res.status() == ::atria::Status::NotFound);
}

TEST_CASE("Malformed and unsatisfiable Range headers return 416", "[composition][http][media]") {
    TempFile const tmp{"sonarium-media-badrange.bin", "ABCDEFGHIJKLMNOPQRST"};
    auto app = build_app(repo_with_rendition("demo", tmp.path_string()));

    auto const range_request = [&](std::string range) {
        ::atria::Headers headers;
        headers.set("Host", "127.0.0.1");
        headers.set("Range", std::move(range));
        return ::atria::Request{
            ::atria::Method::Get, "/media/renditions/demo", {}, std::move(headers), {}};
    };

    auto garbage = range_request("bytes=zz-");
    REQUIRE(app->dispatch(garbage).status() == ::atria::Status::RangeNotSatisfiable);

    auto inverted = range_request("bytes=10-2");
    REQUIRE(app->dispatch(inverted).status() == ::atria::Status::RangeNotSatisfiable);

    auto beyond = range_request("bytes=999-");
    REQUIRE(app->dispatch(beyond).status() == ::atria::Status::RangeNotSatisfiable);
}

TEST_CASE("Operational probes respond on the DLNA server", "[composition][http]") {
    auto app = build_app();

    auto version_req = make_request(::atria::Method::Get, "/version");
    auto const version_res = app->dispatch(version_req);
    REQUIRE(version_res.status() == ::atria::Status::Ok);
    REQUIRE(version_res.body().find("Sonarium DLNA") != std::string::npos);

    auto health_req = make_request(::atria::Method::Get, "/healthz");
    REQUIRE(app->dispatch(health_req).status() == ::atria::Status::Ok);

    auto ready_req = make_request(::atria::Method::Get, "/readyz");
    auto const ready_res = app->dispatch(ready_req);
    REQUIRE(ready_res.status() == ::atria::Status::Ok);
    REQUIRE(ready_res.body() == "ready\n");
}
