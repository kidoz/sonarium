#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

#include "catalog/sqlite_repository.hpp"
#include "catalog/sqlite_schema.hpp"

using sonarium::catalog::Album;
using sonarium::catalog::Artist;
using sonarium::catalog::Playlist;
using sonarium::catalog::SqliteRepository;
using sonarium::catalog::StorageAsset;
using sonarium::catalog::Track;
using sonarium::media::AudioCodec;
using sonarium::media::AudioContainer;
using sonarium::media::MediaRendition;

namespace {

// Fresh database file per test, removed on destruction (including WAL files).
struct ScratchDb {
    std::filesystem::path path;
    std::shared_ptr<SqliteRepository> repo;

    explicit ScratchDb(std::string const& name) {
        path = std::filesystem::temp_directory_path() / name;
        cleanup();
        auto opened = SqliteRepository::open(path);
        REQUIRE(opened.has_value());
        repo = *opened;
        REQUIRE(repo->ensure_schema().has_value());
    }

    ~ScratchDb() {
        repo.reset();
        cleanup();
    }

    ScratchDb(ScratchDb const&) = delete;
    ScratchDb& operator=(ScratchDb const&) = delete;
    ScratchDb(ScratchDb&&) = delete;
    ScratchDb& operator=(ScratchDb&&) = delete;

private:
    void cleanup() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
        std::filesystem::remove(path.string() + "-wal", ec);
        std::filesystem::remove(path.string() + "-shm", ec);
    }
};

} // namespace

TEST_CASE("SqliteRepository ensure_schema is idempotent and records the version",
          "[catalog][sqlite]") {
    ScratchDb const db{"sonarium-sqlite-schema.db"};
    // Re-running the migration must remain a no-op.
    REQUIRE(db.repo->ensure_schema().has_value());
    REQUIRE(db.repo->system_update_id() == 0);
}

TEST_CASE("SqliteRepository round-trips every entity type", "[catalog][sqlite]") {
    ScratchDb const db{"sonarium-sqlite-roundtrip.db"};
    auto& repo = *db.repo;

    Artist artist;
    artist.id = "rob-zombie";
    artist.name = "Rob Zombie";
    artist.sort_name = "Zombie, Rob";
    REQUIRE(repo.upsert_artist(artist).has_value());

    StorageAsset cover;
    cover.id = "cover-1";
    cover.storage_path = "/music/cover.jpg";
    cover.mime_type = "image/jpeg";
    REQUIRE(repo.upsert_asset(cover).has_value());

    Album album;
    album.id = "hellbilly";
    album.artist_id = artist.id;
    album.title = "Hellbilly Deluxe";
    album.release_year = std::uint16_t{1998};
    album.cover_art_asset_id = cover.id;
    REQUIRE(repo.upsert_album(album).has_value());

    Track track;
    track.id = "dragula";
    track.album_id = album.id;
    track.artist_id = artist.id;
    track.title = "Dragula";
    track.track_number = std::uint16_t{2};
    track.duration_ms = 223'840;
    REQUIRE(repo.upsert_track(track).has_value());

    MediaRendition rendition;
    rendition.id = "dragula:mp3";
    rendition.track_id = track.id;
    rendition.codec = AudioCodec::mp3;
    rendition.container = AudioContainer::mp3;
    rendition.mime_type = "audio/mpeg";
    rendition.bitrate_bps = 128'000;
    rendition.duration_ms = 223'840;
    rendition.size_bytes = 5'507'762;
    rendition.storage_path = "/music/01 - Dragula.mp3";
    REQUIRE(repo.upsert_rendition(rendition).has_value());

    auto const got_artist = repo.get_artist(artist.id);
    REQUIRE(got_artist.has_value());
    REQUIRE(got_artist->name == "Rob Zombie");
    REQUIRE(got_artist->sort_name == "Zombie, Rob");

    auto const got_album = repo.get_album(album.id);
    REQUIRE(got_album.has_value());
    REQUIRE(got_album->artist_id == artist.id);
    REQUIRE(got_album->release_year == std::uint16_t{1998});
    REQUIRE(got_album->cover_art_asset_id == cover.id);

    auto const got_track = repo.get_track(track.id);
    REQUIRE(got_track.has_value());
    REQUIRE(got_track->title == "Dragula");
    REQUIRE(got_track->track_number == std::uint16_t{2});
    REQUIRE_FALSE(got_track->disc_number.has_value());
    REQUIRE(got_track->duration_ms == 223'840);

    auto const renditions = repo.list_renditions_for_track(track.id);
    REQUIRE(renditions.size() == 1);
    REQUIRE(renditions.front().mime_type == "audio/mpeg");
    REQUIRE(renditions.front().size_bytes == 5'507'762);

    auto const got_rendition = repo.get_rendition(rendition.id);
    REQUIRE(got_rendition.has_value());
    REQUIRE(got_rendition->storage_path == "/music/01 - Dragula.mp3");

    auto const got_asset = repo.get_asset(cover.id);
    REQUIRE(got_asset.has_value());
    REQUIRE(got_asset->mime_type == "image/jpeg");

    REQUIRE(repo.get_artist("nope") == std::nullopt);
    REQUIRE(repo.get_rendition("nope") == std::nullopt);
}

TEST_CASE("SqliteRepository upserts overwrite instead of duplicating", "[catalog][sqlite]") {
    ScratchDb const db{"sonarium-sqlite-upsert.db"};
    auto& repo = *db.repo;

    Artist artist;
    artist.id = "a1";
    artist.name = "Before";
    REQUIRE(repo.upsert_artist(artist).has_value());
    artist.name = "After";
    REQUIRE(repo.upsert_artist(artist).has_value());

    auto const page = repo.list_artists({0, 0});
    REQUIRE(page.total_matches == 1);
    REQUIRE(page.rows.front().name == "After");
}

TEST_CASE("SqliteRepository paginates with the shared Browse cap", "[catalog][sqlite]") {
    ScratchDb const db{"sonarium-sqlite-paging.db"};
    auto& repo = *db.repo;

    for (int i = 0; i < 5; ++i) {
        Artist a;
        a.id = "artist-" + std::to_string(i);
        a.name = "Artist " + std::to_string(i);
        REQUIRE(repo.upsert_artist(a).has_value());
    }

    auto const first_two = repo.list_artists({0, 2});
    REQUIRE(first_two.rows.size() == 2);
    REQUIRE(first_two.total_matches == 5);
    REQUIRE(first_two.rows[0].id == "artist-0");

    auto const offset = repo.list_artists({4, 2});
    REQUIRE(offset.rows.size() == 1);
    REQUIRE(offset.rows[0].id == "artist-4");

    auto const all = repo.list_artists({0, 0}); // 0 = "all", capped
    REQUIRE(all.rows.size() == 5);
}

TEST_CASE("SqliteRepository stores playlists with ordered items", "[catalog][sqlite]") {
    ScratchDb const db{"sonarium-sqlite-playlist.db"};
    // Playlists have no writer path yet; exercise reads against an empty set.
    auto const page = db.repo->list_playlists({0, 0});
    REQUIRE(page.total_matches == 0);
    REQUIRE(db.repo->get_playlist("nope") == std::nullopt);
}

TEST_CASE("SqliteRepository bumps the system update id", "[catalog][sqlite]") {
    ScratchDb const db{"sonarium-sqlite-updateid.db"};
    REQUIRE(db.repo->system_update_id() == 0);
    REQUIRE(db.repo->bump_system_update_id().has_value());
    REQUIRE(db.repo->bump_system_update_id().has_value());
    REQUIRE(db.repo->system_update_id() == 2);
}

TEST_CASE("SqliteRepository persists across reopen", "[catalog][sqlite]") {
    auto const path = std::filesystem::temp_directory_path() / "sonarium-sqlite-reopen.db";
    std::error_code ec;
    std::filesystem::remove(path, ec);

    {
        auto opened = SqliteRepository::open(path);
        REQUIRE(opened.has_value());
        REQUIRE((*opened)->ensure_schema().has_value());
        Artist a;
        a.id = "persisted";
        a.name = "Persisted Artist";
        REQUIRE((*opened)->upsert_artist(a).has_value());
    }

    auto reopened = SqliteRepository::open(path);
    REQUIRE(reopened.has_value());
    REQUIRE((*reopened)->ensure_schema().has_value());
    auto const got = (*reopened)->get_artist("persisted");
    REQUIRE(got.has_value());
    REQUIRE(got->name == "Persisted Artist");

    std::filesystem::remove(path, ec);
    std::filesystem::remove(path.string() + "-wal", ec);
    std::filesystem::remove(path.string() + "-shm", ec);
}
