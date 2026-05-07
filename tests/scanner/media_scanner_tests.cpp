#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>

#include "catalog/in_memory_repository.hpp"
#include "scanner/media_scanner.hpp"

namespace {

namespace fs = std::filesystem;

class TempDir {
public:
    TempDir() {
        std::random_device rd;
        std::mt19937_64 gen{rd()};
        path_ = fs::temp_directory_path() / ("sonarium-scan-" + std::to_string(gen()));
        fs::create_directories(path_);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path_, ec);
    }
    TempDir(TempDir const&) = delete;
    TempDir& operator=(TempDir const&) = delete;
    TempDir(TempDir&&) = delete;
    TempDir& operator=(TempDir&&) = delete;

    [[nodiscard]] fs::path const& path() const noexcept { return path_; }

    fs::path write_file(std::string_view relative, std::string_view content) const {
        auto const full = path_ / fs::path{std::string{relative}};
        fs::create_directories(full.parent_path());
        std::ofstream out{full, std::ios::binary | std::ios::trunc};
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
        return full;
    }

private:
    fs::path path_;
};

} // namespace

TEST_CASE("scanner walks <artist>/<album>/<NN - title>.<ext> into the writer",
          "[scanner][writer]") {
    TempDir tmp;
    tmp.write_file("Pink Floyd/The Wall/01 - Another Brick.mp3", "FAKE_MP3_BYTES");
    tmp.write_file("Pink Floyd/The Wall/02 - Hey You.flac", "FAKE_FLAC_BYTES");
    tmp.write_file("Pink Floyd/Wish You Were Here/01 - Shine On.mp3", "FAKE_MP3_BYTES");
    // A non-audio file (top-level) and a too-deep file should both be skipped.
    tmp.write_file("README.md", "not audio");
    tmp.write_file("Pink Floyd/The Wall/disc1/03 - Mother.mp3", "FAKE_MP3_BYTES");
    // Album cover for The Wall — should land as a StorageAsset linked to the album.
    tmp.write_file("Pink Floyd/The Wall/cover.jpg", "JPEG_BYTES");

    sonarium::catalog::InMemoryRepository repo;
    auto const report = sonarium::scanner::scan(tmp.path(), repo);

    REQUIRE(report.errors.empty());
    REQUIRE(report.artists_upserted == 1);
    REQUIRE(report.albums_upserted == 2);
    REQUIRE(report.tracks_upserted == 3);
    REQUIRE(report.renditions_upserted == 3);
    REQUIRE(report.covers_upserted == 1);
    REQUIRE(report.skipped_files >= 2); // README.md and the disc1/* file

    // Note: ids are stored bare (without the `artist:` / `album:` etc.
    // prefix) so the DLNA layer can re-add them. The browse handler strips
    // the prefix before calling into the Repository.
    auto const artist = repo.get_artist("pink-floyd");
    REQUIRE(artist.has_value());
    REQUIRE(artist->name == "Pink Floyd");

    auto const albums =
        repo.list_albums_for_artist("pink-floyd", sonarium::catalog::PageRequest{0, 10});
    REQUIRE(albums.total_matches == 2);

    auto const album_with_cover = repo.get_album("pink-floyd:the-wall");
    REQUIRE(album_with_cover.has_value());
    REQUIRE(album_with_cover->cover_art_asset_id.has_value());
    REQUIRE(*album_with_cover->cover_art_asset_id == "cover:pink-floyd:the-wall");

    auto const cover = repo.get_asset("cover:pink-floyd:the-wall");
    REQUIRE(cover.has_value());
    REQUIRE(cover->mime_type == "image/jpeg");

    auto const tracks =
        repo.list_tracks_for_album("pink-floyd:the-wall", sonarium::catalog::PageRequest{0, 10});
    REQUIRE(tracks.total_matches == 2);

    // Find the rendition of the mp3 track.
    auto const* mp3_track = static_cast<sonarium::catalog::Track const*>(nullptr);
    for (auto const& t : tracks.rows) {
        if (t.title == "Another Brick") {
            mp3_track = &t;
        }
    }
    REQUIRE(mp3_track != nullptr);

    auto const renditions = repo.list_renditions_for_track(mp3_track->id);
    REQUIRE(renditions.size() == 1);
    REQUIRE(renditions.front().mime_type == "audio/mpeg");
    REQUIRE(renditions.front().protocol_info == "http-get:*:audio/mpeg:*");
    REQUIRE(renditions.front().dlna_profile_name == "MP3");
    REQUIRE(renditions.front().size_bytes == std::string_view{"FAKE_MP3_BYTES"}.size());
}

TEST_CASE("scanner is idempotent — second run upserts the same rows", "[scanner][writer]") {
    TempDir tmp;
    tmp.write_file("Artist/Album/01 - One.mp3", "X");
    tmp.write_file("Artist/Album/02 - Two.mp3", "XY");

    sonarium::catalog::InMemoryRepository repo;
    auto const first = sonarium::scanner::scan(tmp.path(), repo);
    auto const second = sonarium::scanner::scan(tmp.path(), repo);

    REQUIRE(first.errors.empty());
    REQUIRE(second.errors.empty());
    REQUIRE(first.tracks_upserted == 2);
    REQUIRE(second.tracks_upserted == 2); // upsert, not insert-only

    auto const all = repo.list_all_tracks(sonarium::catalog::PageRequest{0, 100});
    REQUIRE(all.total_matches == 2); // no duplicates after a re-scan
}

TEST_CASE("scanner reports a useful error for a missing root", "[scanner][error]") {
    sonarium::catalog::InMemoryRepository repo;
    auto const report = sonarium::scanner::scan(fs::path{"/nonexistent/sonarium/scan/root"}, repo);
    REQUIRE_FALSE(report.errors.empty());
    REQUIRE(report.tracks_upserted == 0);
}
