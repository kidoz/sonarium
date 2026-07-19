#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "dlna-core/didl_lite_builder.hpp"

using sonarium::dlna::build_didl_lite;
using sonarium::dlna::DidlContainer;
using sonarium::dlna::DidlItem;
using sonarium::dlna::DidlResource;

TEST_CASE("Empty DIDL-Lite has just the wrapper", "[dlna][didl_lite]") {
    auto const xml = build_didl_lite(std::vector<DidlContainer>{}, std::vector<DidlItem>{});
    REQUIRE(xml.starts_with("<DIDL-Lite "));
    REQUIRE(xml.ends_with("</DIDL-Lite>"));
    REQUIRE(!xml.contains("<container"));
    REQUIRE(!xml.contains("<item"));
}

TEST_CASE("Container emits required attributes", "[dlna][didl_lite]") {
    DidlContainer c;
    c.id = "album:42";
    c.parent_id = "artist:7";
    c.title = "Greatest <Hits>";
    c.upnp_class = "object.container.album.musicAlbum";
    c.child_count = 12;

    auto const xml = build_didl_lite(std::vector<DidlContainer>{c});
    REQUIRE(xml.contains(R"(id="album:42")"));
    REQUIRE(xml.contains(R"(parentID="artist:7")"));
    REQUIRE(xml.contains(R"(restricted="1")"));
    REQUIRE(xml.contains(R"(childCount="12")"));
    REQUIRE(xml.contains("<dc:title>Greatest &lt;Hits&gt;</dc:title>"));
    REQUIRE(xml.contains("<upnp:class>object.container.album.musicAlbum</upnp:class>"));
}

TEST_CASE("Item with one resource", "[dlna][didl_lite]") {
    DidlResource r;
    r.protocol_info = "http-get:*:audio/mpeg:DLNA.ORG_PN=MP3;DLNA.ORG_OP=01;DLNA.ORG_CI=0";
    r.url = "http://192.168.1.10:8200/media/renditions/abc?token=tk";
    r.duration = "0:04:10.000";
    r.bitrate_bps = 320'000;
    r.sample_rate_hz = 44'100;
    r.channels = 2;
    r.size_bytes = 12'345'678;

    DidlItem i;
    i.id = "track:99";
    i.parent_id = "album:42";
    i.title = "Side & Step";
    i.upnp_class = "object.item.audioItem.musicTrack";
    i.creator = "AT&T Allstars";
    i.album = "Greatest";
    i.original_track_number = 3;
    i.album_art_uri = "http://192.168.1.10:8200/art/albums/42.jpg";
    i.resources = {r};

    auto const xml = build_didl_lite(std::vector<DidlItem>{i});

    REQUIRE(xml.contains(R"(<item id="track:99")"));
    REQUIRE(xml.contains(R"(parentID="album:42")"));
    REQUIRE(xml.contains("<dc:title>Side &amp; Step</dc:title>"));
    REQUIRE(xml.contains("<dc:creator>AT&amp;T Allstars</dc:creator>"));
    REQUIRE(xml.contains("<upnp:album>Greatest</upnp:album>"));
    REQUIRE(xml.contains("<upnp:originalTrackNumber>3</upnp:originalTrackNumber>"));
    REQUIRE(xml.contains("<upnp:albumArtURI>http://192.168.1.10:8200/art/albums/42.jpg"
                         "</upnp:albumArtURI>"));
    REQUIRE(xml.contains(R"(protocolInfo="http-get:*:audio/mpeg:DLNA.ORG_PN=MP3)"));
    REQUIRE(xml.contains(R"(duration="0:04:10.000")"));
    REQUIRE(xml.contains(R"(bitrate="320000")"));
    REQUIRE(xml.contains(R"(sampleFrequency="44100")"));
    REQUIRE(xml.contains(R"(nrAudioChannels="2")"));
    REQUIRE(xml.contains(R"(size="12345678")"));
}

TEST_CASE("Default upnp:class fallback for items and containers", "[dlna][didl_lite]") {
    DidlContainer c;
    c.id = "x";
    c.parent_id = "0";
    c.title = "T";
    DidlItem i;
    i.id = "y";
    i.parent_id = "x";
    i.title = "U";

    auto const xml = build_didl_lite({c}, {i});
    REQUIRE(xml.contains("<upnp:class>object.container</upnp:class>"));
    REQUIRE(xml.contains("<upnp:class>object.item.audioItem.musicTrack</upnp:class>"));
}
