#include <catch2/catch_test_macros.hpp>

#include "catalog/in_memory_repository.hpp"
#include "media/mime_type.hpp"

using sonarium::catalog::Album;
using sonarium::catalog::Artist;
using sonarium::catalog::InMemoryRepository;
using sonarium::catalog::PageRequest;
using sonarium::catalog::Playlist;
using sonarium::catalog::PlaylistItem;
using sonarium::catalog::Track;
using sonarium::media::AudioCodec;
using sonarium::media::AudioContainer;
using sonarium::media::MediaRendition;

namespace {

InMemoryRepository sample_repo() {
    InMemoryRepository repo;
    Artist a;
    a.id = "artist:1";
    a.name = "Stones";
    a.sort_name = "Stones";
    repo.add_artist(a);

    Artist b;
    b.id = "artist:2";
    b.name = "Beatles";
    b.sort_name = "Beatles";
    repo.add_artist(b);

    Album al;
    al.id = "album:1";
    al.artist_id = "artist:1";
    al.title = "Let It Bleed";
    repo.add_album(al);

    Album al2;
    al2.id = "album:2";
    al2.artist_id = "artist:2";
    al2.title = "Abbey Road";
    repo.add_album(al2);

    Track t;
    t.id = "track:1";
    t.album_id = "album:1";
    t.artist_id = "artist:1";
    t.title = "Gimme Shelter";
    t.duration_ms = 270'000;
    repo.add_track(t);

    Track t2;
    t2.id = "track:2";
    t2.album_id = "album:2";
    t2.artist_id = "artist:2";
    t2.title = "Come Together";
    t2.duration_ms = 260'000;
    repo.add_track(t2);

    MediaRendition r;
    r.id = "rendition:1";
    r.track_id = "track:1";
    r.codec = AudioCodec::mp3;
    r.container = AudioContainer::mp3;
    r.bitrate_bps = 320'000;
    r.duration_ms = 270'000;
    repo.add_rendition(r);

    Playlist pl;
    pl.id = "playlist:1";
    pl.title = "Faves";
    pl.items.push_back(PlaylistItem{"track:1", 0});
    pl.items.push_back(PlaylistItem{"track:2", 1});
    repo.add_playlist(pl);

    return repo;
}

} // namespace

TEST_CASE("Listings are sorted by id and report total_matches", "[catalog][repo]") {
    auto const repo = sample_repo();
    auto const page = repo.list_artists(PageRequest{});
    REQUIRE(page.total_matches == 2);
    REQUIRE(page.rows.size() == 2);
    REQUIRE(page.rows[0].id == "artist:1");
    REQUIRE(page.rows[1].id == "artist:2");
}

TEST_CASE("Pagination respects starting_index and requested_count", "[catalog][repo]") {
    auto const repo = sample_repo();
    auto const page = repo.list_artists(PageRequest{1, 1});
    REQUIRE(page.total_matches == 2);
    REQUIRE(page.rows.size() == 1);
    REQUIRE(page.rows[0].id == "artist:2");
}

TEST_CASE("RequestedCount=0 means all remaining", "[catalog][repo]") {
    auto const repo = sample_repo();
    auto const page = repo.list_artists(PageRequest{0, 0});
    REQUIRE(page.rows.size() == 2);
}

TEST_CASE("starting_index past end returns empty page", "[catalog][repo]") {
    auto const repo = sample_repo();
    auto const page = repo.list_artists(PageRequest{99, 5});
    REQUIRE(page.total_matches == 2);
    REQUIRE(page.rows.empty());
}

TEST_CASE("Albums filter by artist_id", "[catalog][repo]") {
    auto const repo = sample_repo();
    auto const page = repo.list_albums_for_artist("artist:1", {});
    REQUIRE(page.total_matches == 1);
    REQUIRE(page.rows.size() == 1);
    REQUIRE(page.rows[0].id == "album:1");
}

TEST_CASE("Tracks filter by album_id", "[catalog][repo]") {
    auto const repo = sample_repo();
    auto const page = repo.list_tracks_for_album("album:2", {});
    REQUIRE(page.total_matches == 1);
    REQUIRE(page.rows[0].id == "track:2");
}

TEST_CASE("Renditions are returned for known track and empty for unknown", "[catalog][repo]") {
    auto const repo = sample_repo();
    REQUIRE(repo.list_renditions_for_track("track:1").size() == 1);
    REQUIRE(repo.list_renditions_for_track("track:404").empty());
}

TEST_CASE("get_* return nullopt for unknown ids", "[catalog][repo]") {
    auto const repo = sample_repo();
    REQUIRE_FALSE(repo.get_artist("artist:404").has_value());
    REQUIRE_FALSE(repo.get_album("album:404").has_value());
    REQUIRE_FALSE(repo.get_track("track:404").has_value());
    REQUIRE_FALSE(repo.get_playlist("playlist:404").has_value());
}

TEST_CASE("system_update_id starts at zero and bumps", "[catalog][repo]") {
    InMemoryRepository repo;
    REQUIRE(repo.system_update_id() == 0);
    repo.bump_system_update_id();
    repo.bump_system_update_id();
    REQUIRE(repo.system_update_id() == 2);
}

TEST_CASE("get_asset returns nullopt for unknown id", "[catalog][repo][asset]") {
    auto const repo = sample_repo();
    REQUIRE_FALSE(repo.get_asset("missing").has_value());
}

TEST_CASE("add_asset / get_asset round trip", "[catalog][repo][asset]") {
    using sonarium::catalog::StorageAsset;
    InMemoryRepository repo;
    StorageAsset a;
    a.id = "art-1";
    a.storage_path = "/var/lib/sonarium/art-1.jpg";
    a.mime_type = "image/jpeg";
    repo.add_asset(a);

    auto const found = repo.get_asset("art-1");
    REQUIRE(found.has_value());
    REQUIRE(found->id == "art-1");
    REQUIRE(found->storage_path == "/var/lib/sonarium/art-1.jpg");
    REQUIRE(found->mime_type == "image/jpeg");
}
