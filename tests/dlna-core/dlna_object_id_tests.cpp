#include <catch2/catch_test_macros.hpp>

#include "dlna-core/dlna_object_id.hpp"

using sonarium::dlna::make_album_id;
using sonarium::dlna::make_artist_albums_id;
using sonarium::dlna::make_artist_id;
using sonarium::dlna::make_artists_id;
using sonarium::dlna::make_music_id;
using sonarium::dlna::make_playlist_id;
using sonarium::dlna::make_playlists_id;
using sonarium::dlna::make_root_id;
using sonarium::dlna::make_track_id;
using sonarium::dlna::make_tracks_id;
using sonarium::dlna::ObjectIdKind;
using sonarium::dlna::ObjectIdParseError;
using sonarium::dlna::parse_object_id;

TEST_CASE("make_* helpers produce documented forms", "[dlna][object_id]") {
    REQUIRE(make_root_id() == "0");
    REQUIRE(make_music_id() == "1");
    REQUIRE(make_artists_id() == "artists");
    REQUIRE(make_artist_id("7") == "artist:7");
    REQUIRE(make_artist_albums_id("7") == "artist:7:albums");
    REQUIRE(make_album_id("42") == "album:42");
    REQUIRE(make_tracks_id() == "tracks");
    REQUIRE(make_track_id("99") == "track:99");
    REQUIRE(make_playlists_id() == "playlists");
    REQUIRE(make_playlist_id("p1") == "playlist:p1");
}

TEST_CASE("parse_object_id round-trips top-level kinds", "[dlna][object_id]") {
    REQUIRE(parse_object_id("0").value().kind == ObjectIdKind::root);
    REQUIRE(parse_object_id("1").value().kind == ObjectIdKind::music);
    REQUIRE(parse_object_id("artists").value().kind == ObjectIdKind::artists);
    REQUIRE(parse_object_id("tracks").value().kind == ObjectIdKind::tracks);
    REQUIRE(parse_object_id("playlists").value().kind == ObjectIdKind::playlists);
}

TEST_CASE("parse_object_id round-trips entity kinds", "[dlna][object_id]") {
    {
        auto const r = parse_object_id("artist:42").value();
        REQUIRE(r.kind == ObjectIdKind::artist);
        REQUIRE(r.entity_id == "42");
    }
    {
        auto const r = parse_object_id("artist:42:albums").value();
        REQUIRE(r.kind == ObjectIdKind::artist_albums);
        REQUIRE(r.entity_id == "42");
    }
    {
        auto const r = parse_object_id("album:7").value();
        REQUIRE(r.kind == ObjectIdKind::album);
        REQUIRE(r.entity_id == "7");
    }
    {
        auto const r = parse_object_id("track:abc").value();
        REQUIRE(r.kind == ObjectIdKind::track);
        REQUIRE(r.entity_id == "abc");
    }
    {
        auto const r = parse_object_id("playlist:fav").value();
        REQUIRE(r.kind == ObjectIdKind::playlist);
        REQUIRE(r.entity_id == "fav");
    }
}

TEST_CASE("parse_object_id rejects malformed input", "[dlna][object_id]") {
    REQUIRE(parse_object_id("").error() == ObjectIdParseError::empty);
    REQUIRE(parse_object_id("artist:").error() == ObjectIdParseError::invalid_format);
    REQUIRE(parse_object_id("album:").error() == ObjectIdParseError::invalid_format);
    REQUIRE(parse_object_id("track:").error() == ObjectIdParseError::invalid_format);
    REQUIRE(parse_object_id("artist::albums").error() == ObjectIdParseError::invalid_format);
    REQUIRE(parse_object_id("garbage:42").error() == ObjectIdParseError::unknown_kind);
}
