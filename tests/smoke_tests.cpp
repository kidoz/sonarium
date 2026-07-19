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
#include <string>
#include <system_error>
#include <utility>

#include "catalog/in_memory_repository.hpp"
#include "composition/dlna_server.hpp"
#include "composition/http_routes.hpp"
#include "composition/injector.hpp"
#include "composition/service_config.hpp"
#include "dlna-core/connection_manager_handler.hpp"
#include "media/media_rendition.hpp"

// In-process end-to-end smoke: the renderer flow (device description →
// Browse root → Browse album → fetch media with a Range) against the real
// composition graph, with no sockets. Anything failing here means the stack
// is broken in a way unit suites might not catch as a whole.

namespace {

using sonarium::catalog::Album;
using sonarium::catalog::Artist;
using sonarium::catalog::InMemoryRepository;
using sonarium::catalog::Repository;
using sonarium::catalog::Track;
using sonarium::composition::DlnaServer;
using sonarium::composition::ServiceConfig;
using sonarium::media::AudioCodec;
using sonarium::media::AudioContainer;
using sonarium::media::MediaRendition;

[[nodiscard]] ::atria::Request make_request(::atria::Method method,
                                            std::string path,
                                            std::string body = {},
                                            std::string range = {}) {
    ::atria::Headers headers;
    headers.set("Host", "127.0.0.1");
    headers.set("User-Agent", "smoke-control-point/0.1");
    headers.set("Content-Type", "text/xml; charset=utf-8");
    if (!range.empty()) {
        headers.set("Range", std::move(range));
    }
    return ::atria::Request{method, std::move(path), {}, std::move(headers), std::move(body)};
}

[[nodiscard]] std::string browse_body(std::string const& object_id) {
    return std::string{R"(<?xml version="1.0" encoding="utf-8"?>
<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/">
  <s:Body>
    <u:Browse xmlns:u="urn:schemas-upnp-org:service:ContentDirectory:1">
      <ObjectID>)"}
           + object_id + R"(</ObjectID>
      <BrowseFlag>BrowseDirectChildren</BrowseFlag>
      <Filter>*</Filter>
      <StartingIndex>0</StartingIndex>
      <RequestedCount>200</RequestedCount>
      <SortCriteria></SortCriteria>
    </u:Browse>
  </s:Body>
</s:Envelope>)";
}

} // namespace

TEST_CASE("in-process renderer flow: describe, browse, fetch media", "[smoke]") {
    // A real on-disk media payload so the file route serves actual bytes.
    auto const media_path = std::filesystem::temp_directory_path() / "sonarium-smoke-rendition.mp3";
    {
        std::ofstream out{media_path, std::ios::binary | std::ios::trunc};
        out << std::string(2048, 'S');
    }

    auto repo = std::make_shared<InMemoryRepository>();
    Artist artist;
    artist.id = "1";
    artist.name = "Smoke Artist";
    repo->add_artist(artist);
    Album album;
    album.id = "1";
    album.artist_id = "1";
    album.title = "Smoke Album";
    repo->add_album(album);
    Track track;
    track.id = "1";
    track.album_id = "1";
    track.artist_id = "1";
    track.title = "Smoke Track";
    track.duration_ms = 180'000;
    repo->add_track(track);
    MediaRendition rendition;
    rendition.id = "smoke-mp3";
    rendition.track_id = "1";
    rendition.codec = AudioCodec::mp3;
    rendition.container = AudioContainer::mp3;
    rendition.bitrate_bps = 320'000;
    rendition.duration_ms = 180'000;
    rendition.storage_path = media_path.string();
    repo->add_rendition(rendition);

    ServiceConfig cfg;
    cfg.device.friendly_name = "Sonarium Smoke";
    cfg.device.manufacturer = "Kidoz";
    cfg.device.model_name = "sonarium-dlna";
    cfg.device.udn = "uuid:00000000-0000-0000-0000-00000000beef";
    cfg.base_url = "http://192.168.1.10:8200";
    cfg.protocol_info_catalog = sonarium::dlna::default_protocol_info_catalog();

    auto server = sonarium::composition::make_dlna_server(repo, cfg);
    ::atria::Application app;
    sonarium::composition::register_dlna_routes(
        app,
        std::const_pointer_cast<DlnaServer const>(server),
        std::const_pointer_cast<Repository const>(std::static_pointer_cast<Repository>(repo)));

    // 1. Device description.
    auto describe = make_request(::atria::Method::Get, "/description.xml");
    auto const description = app.dispatch(describe);
    REQUIRE(description.status() == ::atria::Status::Ok);
    REQUIRE(description.body().contains("MediaServer"));

    // 2. Browse the root, then the album; the album listing must mint a
    //    fetchable media URL.
    auto browse_root =
        make_request(::atria::Method::Post, "/upnp/control/content-directory", browse_body("0"));
    auto const root = app.dispatch(browse_root);
    REQUIRE(root.status() == ::atria::Status::Ok);
    REQUIRE(root.body().contains("BrowseResponse"));

    auto browse_album = make_request(
        ::atria::Method::Post, "/upnp/control/content-directory", browse_body("album:1"));
    auto const album_listing = app.dispatch(browse_album);
    REQUIRE(album_listing.status() == ::atria::Status::Ok);
    REQUIRE(album_listing.body().contains("/media/renditions/smoke-mp3"));

    // 3. Fetch the media with a Range, renderer-style. File responses stream,
    //    so drain the chunk provider to see the actual bytes.
    auto fetch =
        make_request(::atria::Method::Get, "/media/renditions/smoke-mp3", {}, "bytes=0-15");
    auto media = app.dispatch(fetch);
    REQUIRE(media.status() == ::atria::Status::PartialContent);
    REQUIRE(media.content_length().value_or(0) == 16);
    std::string payload;
    if (media.is_streaming()) {
        auto provider = media.take_chunk_provider();
        while (auto chunk = provider()) {
            if (chunk->empty()) {
                break;
            }
            payload.append(*chunk);
        }
    } else {
        payload = media.body();
    }
    REQUIRE(payload == std::string(16, 'S'));

    std::error_code ec;
    std::filesystem::remove(media_path, ec);
}
