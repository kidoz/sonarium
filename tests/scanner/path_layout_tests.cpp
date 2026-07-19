#include <catch2/catch_test_macros.hpp>
#include <filesystem>

#include "scanner/path_layout.hpp"

using sonarium::scanner::parse_track_path;
using sonarium::scanner::slug;

TEST_CASE("slug lowercases and collapses non-alphanumerics", "[scanner][slug]") {
    REQUIRE(slug("Pink Floyd") == "pink-floyd");
    REQUIRE(slug("OK Computer (1997)") == "ok-computer-1997");
    REQUIRE(slug("Aerosmith - Toys") == "aerosmith-toys");
    REQUIRE(slug("  leading and trailing  ") == "leading-and-trailing");
    REQUIRE(slug("UPPER") == "upper");
    REQUIRE(slug("").empty());
    REQUIRE(slug("---").empty());
    REQUIRE(slug("a---b") == "a-b");
    REQUIRE(slug("123 abc") == "123-abc");
}

TEST_CASE("parse_track_path requires three components", "[scanner][path]") {
    REQUIRE_FALSE(parse_track_path("track.mp3").has_value());
    REQUIRE_FALSE(parse_track_path("artist/track.mp3").has_value());
    REQUIRE_FALSE(parse_track_path("artist/album/disc/track.mp3").has_value());
}

TEST_CASE("parse_track_path extracts artist/album/title with track number", "[scanner][path]") {
    auto const parsed = parse_track_path("Pink Floyd/The Wall/03 - Another Brick.mp3");
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->artist_name == "Pink Floyd");
    REQUIRE(parsed->album_name == "The Wall");
    REQUIRE(parsed->track_title == "Another Brick");
    REQUIRE(parsed->track_number.has_value());
    REQUIRE(*parsed->track_number == 3);
    REQUIRE(parsed->extension == "mp3");
}

TEST_CASE("parse_track_path handles dot-separated track numbers", "[scanner][path]") {
    auto const parsed = parse_track_path("Pink Floyd/The Wall/12. Hey You.flac");
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->track_number.has_value());
    REQUIRE(*parsed->track_number == 12);
    REQUIRE(parsed->track_title == "Hey You");
    REQUIRE(parsed->extension == "flac");
}

TEST_CASE("parse_track_path lowercases the extension", "[scanner][path]") {
    auto const parsed = parse_track_path("A/B/Track.MP3");
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->extension == "mp3");
}

TEST_CASE("parse_track_path leaves track number empty when filename has none", "[scanner][path]") {
    auto const parsed = parse_track_path("A/B/Just A Title.flac");
    REQUIRE(parsed.has_value());
    REQUIRE_FALSE(parsed->track_number.has_value());
    REQUIRE(parsed->track_title == "Just A Title");
}

TEST_CASE("parse_track_path rejects extension-only or hidden files", "[scanner][path]") {
    REQUIRE_FALSE(parse_track_path("A/B/.hidden").has_value());
    REQUIRE_FALSE(parse_track_path("A/B/file.").has_value());
}

TEST_CASE("parse_track_path treats '01.mp3' as no-title case but keeps a stem", "[scanner][path]") {
    auto const parsed = parse_track_path("A/B/01.mp3");
    REQUIRE(parsed.has_value());
    // Once the digit run is consumed, the only separator is `.` which would
    // start the extension — so there's no track-number prefix to strip.
    // The title falls back to the full stem.
    REQUIRE(parsed->track_title == "01");
}
