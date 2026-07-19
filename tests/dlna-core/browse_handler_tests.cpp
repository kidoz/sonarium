#include <catch2/catch_test_macros.hpp>
#include <string>

#include "catalog/in_memory_repository.hpp"
#include "dlna-core/browse_handler.hpp"
#include "dlna-core/device_profile.hpp"
#include "media/mime_type.hpp"
#include "upnp/soap_envelope.hpp"
#include "upnp/upnp_error.hpp"

using sonarium::catalog::Album;
using sonarium::catalog::Artist;
using sonarium::catalog::InMemoryRepository;
using sonarium::catalog::Track;
using sonarium::dlna::BrowseContext;
using sonarium::dlna::DeviceProfileRegistry;
using sonarium::dlna::handle_browse;
using sonarium::dlna::RequestHeaders;
using sonarium::media::AudioCodec;
using sonarium::media::AudioContainer;
using sonarium::media::MediaRendition;
using sonarium::upnp::ParsedSoapRequest;
using sonarium::upnp::UpnpErrorCode;

namespace {

InMemoryRepository sample_repo() {
    InMemoryRepository repo;

    Artist a;
    a.id = "1";
    a.name = "Stones";
    repo.add_artist(a);

    Album al;
    al.id = "1";
    al.artist_id = "1";
    al.title = "Let It Bleed";
    repo.add_album(al);

    Track t;
    t.id = "1";
    t.album_id = "1";
    t.artist_id = "1";
    t.title = "Gimme Shelter";
    t.duration_ms = 270'000;
    t.track_number = std::uint16_t{1};
    repo.add_track(t);

    Track t2;
    t2.id = "2";
    t2.album_id = "1";
    t2.artist_id = "1";
    t2.title = "Love in Vain";
    t2.duration_ms = 250'000;
    repo.add_track(t2);

    MediaRendition r;
    r.id = "r1";
    r.track_id = "1";
    r.codec = AudioCodec::mp3;
    r.container = AudioContainer::mp3;
    r.bitrate_bps = 320'000;
    r.duration_ms = 270'000;
    repo.add_rendition(r);

    return repo;
}

ParsedSoapRequest browse_request(std::string object_id,
                                 std::string browse_flag = "BrowseDirectChildren",
                                 std::string starting_index = "0",
                                 std::string requested_count = "0") {
    ParsedSoapRequest req;
    req.service_urn = "urn:schemas-upnp-org:service:ContentDirectory:1";
    req.action = "Browse";
    req.args.emplace_back("ObjectID", std::move(object_id));
    req.args.emplace_back("BrowseFlag", std::move(browse_flag));
    req.args.emplace_back("Filter", "*");
    req.args.emplace_back("StartingIndex", std::move(starting_index));
    req.args.emplace_back("RequestedCount", std::move(requested_count));
    req.args.emplace_back("SortCriteria", "");
    return req;
}

} // namespace

TEST_CASE("Browse(0, BrowseDirectChildren) returns Music + Playlists", "[dlna][browse]") {
    auto repo = sample_repo();
    auto const reg = DeviceProfileRegistry::with_defaults();
    auto const& profile = reg.match(RequestHeaders{"VLC/3", {}});
    BrowseContext const ctx{&repo, &profile, "http://h:8200"};

    auto const r = handle_browse(browse_request("0"), ctx);
    REQUIRE(r.has_value());
    REQUIRE(r->total_matches == 2);
    REQUIRE(r->number_returned == 2);
    REQUIRE(r->didl_lite.contains(R"(<container id="1" parentID="0")"));
    REQUIRE(r->didl_lite.contains(R"(<container id="playlists")"));
}

TEST_CASE("Browse(0, BrowseMetadata) returns the root container", "[dlna][browse]") {
    auto repo = sample_repo();
    auto const reg = DeviceProfileRegistry::with_defaults();
    auto const& profile = reg.match(RequestHeaders{"VLC/3", {}});
    BrowseContext const ctx{&repo, &profile, "http://h:8200"};

    auto const r = handle_browse(browse_request("0", "BrowseMetadata"), ctx);
    REQUIRE(r.has_value());
    REQUIRE(r->number_returned == 1);
    REQUIRE(r->didl_lite.contains(R"(<container id="0" parentID="-1")"));
}

TEST_CASE("Browse(artists) lists artist containers", "[dlna][browse]") {
    auto repo = sample_repo();
    auto const reg = DeviceProfileRegistry::with_defaults();
    auto const& profile = reg.match(RequestHeaders{"VLC/3", {}});
    BrowseContext const ctx{&repo, &profile, "http://h:8200"};

    auto const r = handle_browse(browse_request("artists"), ctx);
    REQUIRE(r.has_value());
    REQUIRE(r->total_matches == 1);
    REQUIRE(r->number_returned == 1);
    REQUIRE(r->didl_lite.contains(R"(<container id="artist:1" parentID="artists")"));
    REQUIRE(r->didl_lite.contains("<dc:title>Stones</dc:title>"));
}

TEST_CASE("Browse(album:1) lists track items with resources", "[dlna][browse]") {
    auto repo = sample_repo();
    auto const reg = DeviceProfileRegistry::with_defaults();
    auto const& profile = reg.match(RequestHeaders{"VLC/3", {}});
    BrowseContext const ctx{&repo, &profile, "http://h:8200"};

    auto const r = handle_browse(browse_request("album:1"), ctx);
    REQUIRE(r.has_value());
    REQUIRE(r->total_matches == 2);
    REQUIRE(r->number_returned == 2);
    REQUIRE(r->didl_lite.contains(R"(<item id="track:1" parentID="album:1")"));
    REQUIRE(r->didl_lite.contains(R"(<item id="track:2" parentID="album:1")"));
    REQUIRE(r->didl_lite.contains("<dc:title>Gimme Shelter</dc:title>"));
    REQUIRE(r->didl_lite.contains(R"(<res protocolInfo="http-get:*:audio/mpeg)"));
    REQUIRE(r->didl_lite.contains("http://h:8200/media/renditions/r1"));
}

TEST_CASE("Browse(album:1) honors pagination", "[dlna][browse]") {
    auto repo = sample_repo();
    auto const reg = DeviceProfileRegistry::with_defaults();
    auto const& profile = reg.match(RequestHeaders{"VLC/3", {}});
    BrowseContext const ctx{&repo, &profile, "http://h:8200"};

    auto const r = handle_browse(browse_request("album:1", "BrowseDirectChildren", "1", "1"), ctx);
    REQUIRE(r.has_value());
    REQUIRE(r->total_matches == 2);
    REQUIRE(r->number_returned == 1);
    REQUIRE(r->didl_lite.contains(R"(<item id="track:2")"));
    REQUIRE(!r->didl_lite.contains(R"(<item id="track:1")"));
}

TEST_CASE("Browse for unknown artist returns no_such_object", "[dlna][browse]") {
    auto repo = sample_repo();
    auto const reg = DeviceProfileRegistry::with_defaults();
    auto const& profile = reg.match(RequestHeaders{"VLC/3", {}});
    BrowseContext const ctx{&repo, &profile, "http://h:8200"};

    auto const r = handle_browse(browse_request("artist:404"), ctx);
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error() == UpnpErrorCode::no_such_object);
}

TEST_CASE("Browse with malformed ObjectID returns no_such_object", "[dlna][browse]") {
    auto repo = sample_repo();
    auto const reg = DeviceProfileRegistry::with_defaults();
    auto const& profile = reg.match(RequestHeaders{"VLC/3", {}});
    BrowseContext const ctx{&repo, &profile, "http://h:8200"};

    auto const r = handle_browse(browse_request("garbage:42"), ctx);
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error() == UpnpErrorCode::no_such_object);
}

TEST_CASE("Browse without ObjectID returns invalid_args", "[dlna][browse]") {
    auto repo = sample_repo();
    auto const reg = DeviceProfileRegistry::with_defaults();
    auto const& profile = reg.match(RequestHeaders{"VLC/3", {}});
    BrowseContext const ctx{&repo, &profile, "http://h:8200"};

    ParsedSoapRequest req;
    req.service_urn = "urn:schemas-upnp-org:service:ContentDirectory:1";
    req.action = "Browse";
    auto const r = handle_browse(req, ctx);
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error() == UpnpErrorCode::invalid_args);
}

TEST_CASE("Non-Browse action is rejected", "[dlna][browse]") {
    auto repo = sample_repo();
    auto const reg = DeviceProfileRegistry::with_defaults();
    auto const& profile = reg.match(RequestHeaders{"VLC/3", {}});
    BrowseContext const ctx{&repo, &profile, "http://h:8200"};

    auto req = browse_request("0");
    req.action = "Search";
    auto const r = handle_browse(req, ctx);
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error() == UpnpErrorCode::invalid_action);
}

TEST_CASE("Track items carry duration on each <res>", "[dlna][browse]") {
    auto repo = sample_repo();
    auto const reg = DeviceProfileRegistry::with_defaults();
    auto const& profile = reg.match(RequestHeaders{"VLC/3", {}});
    BrowseContext const ctx{&repo, &profile, "http://h:8200"};

    auto const r = handle_browse(browse_request("track:1", "BrowseMetadata"), ctx);
    REQUIRE(r.has_value());
    REQUIRE(r->didl_lite.contains(R"(duration="0:04:30.000")"));
}

TEST_CASE("Track items carry upnp:albumArtURI when album has cover art",
          "[dlna][browse][album_art]") {
    auto repo = sample_repo();
    auto al = repo.get_album("1").value();
    al.cover_art_asset_id = "art-1";
    repo.add_album(al);

    auto const reg = DeviceProfileRegistry::with_defaults();
    auto const& profile = reg.match(RequestHeaders{"VLC/3", {}});
    BrowseContext const ctx{&repo, &profile, "http://lan.invalid:8200"};

    auto const r = handle_browse(browse_request("album:1"), ctx);
    REQUIRE(r.has_value());
    REQUIRE(r->didl_lite.contains(
        "<upnp:albumArtURI>http://lan.invalid:8200/art/albums/1</upnp:albumArtURI>"));
}

TEST_CASE("Track items omit upnp:albumArtURI when album has no cover art",
          "[dlna][browse][album_art]") {
    auto repo = sample_repo();
    auto const reg = DeviceProfileRegistry::with_defaults();
    auto const& profile = reg.match(RequestHeaders{"VLC/3", {}});
    BrowseContext const ctx{&repo, &profile, "http://h:8200"};

    auto const r = handle_browse(browse_request("album:1"), ctx);
    REQUIRE(r.has_value());
    REQUIRE(!r->didl_lite.contains("<upnp:albumArtURI>"));
}
