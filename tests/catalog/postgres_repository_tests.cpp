#include <asterorm/postgres/driver.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <string>

#include "catalog/postgres_repository.hpp"

using sonarium::catalog::Artist;
using sonarium::catalog::PageRequest;
using sonarium::catalog::PostgresRepository;

namespace {

[[nodiscard]] std::string conninfo_or_skip() {
    if (auto const* v = std::getenv("SONARIUM_PG_CONNINFO"); v != nullptr && *v != '\0') {
        return std::string{v};
    }
    return {};
}

} // namespace

// These tests run only when SONARIUM_PG_CONNINFO is set; otherwise they
// short-circuit so the suite stays green on dev boxes without a Postgres
// instance. Example:
//   SONARIUM_PG_CONNINFO='host=127.0.0.1 user=sonarium dbname=sonarium' just test
//
// The test uses `DROP TABLE` first to keep runs idempotent.

TEST_CASE("PostgresRepository connect + ensure_schema", "[catalog][postgres]") {
    auto const conninfo = conninfo_or_skip();
    if (conninfo.empty()) {
        SUCCEED("SONARIUM_PG_CONNINFO unset — skipping live Postgres test");
        return;
    }

    auto repo = PostgresRepository::open(conninfo);
    REQUIRE(repo.has_value());

    REQUIRE((*repo)->ensure_schema().has_value());
    // Re-running the migration must remain idempotent.
    REQUIRE((*repo)->ensure_schema().has_value());
}

TEST_CASE("PostgresRepository list_artists round-trip", "[catalog][postgres]") {
    auto const conninfo = conninfo_or_skip();
    if (conninfo.empty()) {
        SUCCEED("SONARIUM_PG_CONNINFO unset — skipping live Postgres test");
        return;
    }

    auto repo_result = PostgresRepository::open(conninfo);
    REQUIRE(repo_result.has_value());
    auto repo = *repo_result;
    REQUIRE(repo->ensure_schema().has_value());

    // No assertions on counts — the test runs against whatever DB the user
    // points it at. Just confirm the read path returns *something* sensibly
    // shaped (no crash, total_matches >= rows.size()).
    auto const page = repo->list_artists(PageRequest{0, 16});
    REQUIRE(page.total_matches >= page.rows.size());
}

TEST_CASE("PostgresRepository system_update_id is non-negative", "[catalog][postgres]") {
    auto const conninfo = conninfo_or_skip();
    if (conninfo.empty()) {
        SUCCEED("SONARIUM_PG_CONNINFO unset — skipping live Postgres test");
        return;
    }

    auto repo = PostgresRepository::open(conninfo).value();
    REQUIRE(repo->ensure_schema().has_value());
    auto const sid = repo->system_update_id();
    REQUIRE(sid >= 0u); // counter is unsigned; just confirm the call returns
}

// Full read-path round-trip: insert sample rows via a separate libpq connection,
// then exercise every Repository read method. Uses a "rt-" id prefix so the
// cleanup at start + end is targeted (won't disturb other data in a shared DB).
TEST_CASE("PostgresRepository round-trips every entity type", "[catalog][postgres]") {
    auto const conninfo = conninfo_or_skip();
    if (conninfo.empty()) {
        SUCCEED("SONARIUM_PG_CONNINFO unset — skipping live Postgres test");
        return;
    }

    auto repo = PostgresRepository::open(conninfo).value();
    REQUIRE(repo->ensure_schema().has_value());

    // Use a second connection for fixture writes so the test can read via
    // the Repository under test without sharing transaction state.
    ::asterorm::pg::driver driver;
    auto fixture_conn_result = driver.connect(conninfo);
    REQUIRE(fixture_conn_result.has_value());
    auto& conn = *fixture_conn_result;

    auto const wipe = [&] {
        // Delete in dependency order — children before parents.
        REQUIRE(
            conn.execute("DELETE FROM playlist_item WHERE playlist_id LIKE 'rt-%'").has_value());
        REQUIRE(conn.execute("DELETE FROM playlist WHERE id LIKE 'rt-%'").has_value());
        REQUIRE(conn.execute("DELETE FROM media_rendition WHERE id LIKE 'rt-%'").has_value());
        REQUIRE(conn.execute("DELETE FROM track WHERE id LIKE 'rt-%'").has_value());
        REQUIRE(conn.execute("DELETE FROM album WHERE id LIKE 'rt-%'").has_value());
        REQUIRE(conn.execute("DELETE FROM artist WHERE id LIKE 'rt-%'").has_value());
        REQUIRE(conn.execute("DELETE FROM storage_asset WHERE id LIKE 'rt-%'").has_value());
    };

    wipe();

    REQUIRE(conn.execute("INSERT INTO storage_asset (id, storage_path, mime_type) "
                         "VALUES ('rt-cover', '/tmp/cover.jpg', 'image/jpeg')")
                .has_value());
    REQUIRE(conn.execute("INSERT INTO artist (id, name, sort_name) "
                         "VALUES ('rt-artist-1', 'Round Trip', 'round trip')")
                .has_value());
    REQUIRE(conn.execute("INSERT INTO album (id, artist_id, title, sort_title, release_year, "
                         "cover_art_asset_id) VALUES ('rt-album-1', 'rt-artist-1', 'RT Album', "
                         "'rt album', 2024, 'rt-cover')")
                .has_value());
    REQUIRE(conn.execute("INSERT INTO track (id, album_id, artist_id, title, sort_title, "
                         "disc_number, track_number, duration_ms) VALUES "
                         "('rt-track-1', 'rt-album-1', 'rt-artist-1', 'Track One', 'track one', "
                         "1, 1, 240000)")
                .has_value());
    REQUIRE(conn.execute("INSERT INTO media_rendition (id, track_id, codec, container, mime_type, "
                         "bitrate_bps, sample_rate_hz, bit_depth, channels, duration_ms, "
                         "size_bytes, checksum, storage_path, dlna_profile_name, protocol_info, "
                         "purpose) VALUES "
                         "('rt-rendition-1', 'rt-track-1', 0, 0, 'audio/mpeg', "
                         "320000, 44100, 16, 2, 240000, 9600000, 'sha256:abc', "
                         "'/tmp/track.mp3', 'MP3', 'http-get:*:audio/mpeg:*', 0)")
                .has_value());
    REQUIRE(conn.execute("INSERT INTO playlist (id, title) VALUES ('rt-playlist-1', 'RT Playlist')")
                .has_value());
    REQUIRE(conn.execute("INSERT INTO playlist_item (playlist_id, track_id, position) "
                         "VALUES ('rt-playlist-1', 'rt-track-1', 0)")
                .has_value());

    SECTION("artist read paths") {
        auto const artist = repo->get_artist("rt-artist-1");
        REQUIRE(artist.has_value());
        REQUIRE(artist->name == "Round Trip");
        REQUIRE(artist->sort_name == "round trip");
    }

    SECTION("album read paths") {
        auto const albums = repo->list_albums_for_artist("rt-artist-1", PageRequest{0, 10});
        REQUIRE(albums.total_matches >= 1);
        REQUIRE_FALSE(albums.rows.empty());
        REQUIRE(albums.rows.front().title == "RT Album");
        REQUIRE(albums.rows.front().release_year.has_value());
        REQUIRE(*albums.rows.front().release_year == 2024);
        REQUIRE(albums.rows.front().cover_art_asset_id.has_value());
        REQUIRE(*albums.rows.front().cover_art_asset_id == "rt-cover");

        auto const album = repo->get_album("rt-album-1");
        REQUIRE(album.has_value());
        REQUIRE(album->artist_id == "rt-artist-1");
    }

    SECTION("track read paths") {
        auto const tracks = repo->list_tracks_for_album("rt-album-1", PageRequest{0, 10});
        REQUIRE(tracks.total_matches >= 1);
        REQUIRE_FALSE(tracks.rows.empty());
        auto const& t = tracks.rows.front();
        REQUIRE(t.title == "Track One");
        REQUIRE(t.duration_ms == 240000U);
        REQUIRE(t.track_number.has_value());
        REQUIRE(*t.track_number == 1);

        auto const track = repo->get_track("rt-track-1");
        REQUIRE(track.has_value());
        REQUIRE(track->album_id == "rt-album-1");

        auto const all_tracks = repo->list_all_tracks(PageRequest{0, 10});
        REQUIRE(all_tracks.total_matches >= 1);
    }

    SECTION("rendition read paths") {
        auto const renditions = repo->list_renditions_for_track("rt-track-1");
        REQUIRE(renditions.size() == 1);
        REQUIRE(renditions.front().mime_type == "audio/mpeg");
        REQUIRE(renditions.front().bitrate_bps == 320000U);
        REQUIRE(renditions.front().channels == 2);

        auto const rendition = repo->get_rendition("rt-rendition-1");
        REQUIRE(rendition.has_value());
        REQUIRE(rendition->track_id == "rt-track-1");
        REQUIRE(rendition->storage_path == "/tmp/track.mp3");
    }

    SECTION("playlist read paths") {
        auto const playlists = repo->list_playlists(PageRequest{0, 10});
        REQUIRE(playlists.total_matches >= 1);
        bool found = false;
        for (auto const& p : playlists.rows) {
            if (p.id == "rt-playlist-1") {
                found = true;
                REQUIRE(p.title == "RT Playlist");
                REQUIRE(p.items.size() == 1);
                REQUIRE(p.items.front().track_id == "rt-track-1");
                REQUIRE(p.items.front().position == 0U);
            }
        }
        REQUIRE(found);

        auto const playlist = repo->get_playlist("rt-playlist-1");
        REQUIRE(playlist.has_value());
        REQUIRE(playlist->items.size() == 1);
    }

    SECTION("storage asset read path") {
        auto const asset = repo->get_asset("rt-cover");
        REQUIRE(asset.has_value());
        REQUIRE(asset->mime_type == "image/jpeg");
        REQUIRE(asset->storage_path == "/tmp/cover.jpg");
    }

    SECTION("missing ids return nullopt") {
        REQUIRE_FALSE(repo->get_artist("rt-missing").has_value());
        REQUIRE_FALSE(repo->get_album("rt-missing").has_value());
        REQUIRE_FALSE(repo->get_track("rt-missing").has_value());
        REQUIRE_FALSE(repo->get_rendition("rt-missing").has_value());
        REQUIRE_FALSE(repo->get_playlist("rt-missing").has_value());
        REQUIRE_FALSE(repo->get_asset("rt-missing").has_value());
    }

    wipe();
}
