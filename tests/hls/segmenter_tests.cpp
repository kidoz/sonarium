#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "hls/segmenter.hpp"

TEST_CASE("is_safe_cache_filename accepts the playlist", "[hls][segmenter]") {
    REQUIRE(sonarium::hls::is_safe_cache_filename("index.m3u8"));
}

TEST_CASE("is_safe_cache_filename accepts canonical segment names", "[hls][segmenter]") {
    REQUIRE(sonarium::hls::is_safe_cache_filename("seg00000.ts"));
    REQUIRE(sonarium::hls::is_safe_cache_filename("seg99999.ts"));
}

TEST_CASE("is_safe_cache_filename rejects unsafe inputs", "[hls][segmenter]") {
    REQUIRE_FALSE(sonarium::hls::is_safe_cache_filename(""));
    REQUIRE_FALSE(sonarium::hls::is_safe_cache_filename(".."));
    REQUIRE_FALSE(sonarium::hls::is_safe_cache_filename("../etc/passwd"));
    REQUIRE_FALSE(sonarium::hls::is_safe_cache_filename("seg.ts")); // missing digits
    REQUIRE_FALSE(sonarium::hls::is_safe_cache_filename("seg00001.tsx"));
    REQUIRE_FALSE(sonarium::hls::is_safe_cache_filename("seg0001.ts"));   // 4 digits
    REQUIRE_FALSE(sonarium::hls::is_safe_cache_filename("seg000000.ts")); // 6 digits
    REQUIRE_FALSE(sonarium::hls::is_safe_cache_filename("seg0001a.ts"));
    REQUIRE_FALSE(sonarium::hls::is_safe_cache_filename("/etc/passwd"));
}

TEST_CASE("build_segmenter_argv embeds source / output / bitrate / segment seconds",
          "[hls][segmenter]") {
    sonarium::hls::SegmenterConfig cfg;
    cfg.cache_root = "/tmp/cache";
    cfg.segment_duration_seconds = 4;
    cfg.bitrate_kbps = 256;

    auto const argv =
        sonarium::hls::build_segmenter_argv("/music/song.flac", "/tmp/cache/track1", cfg);

    auto const find = [&](std::string_view flag) -> std::string {
        for (std::size_t i = 0; i + 1 < argv.size(); ++i) {
            if (argv[i] == flag) {
                return argv[i + 1];
            }
        }
        return {};
    };
    REQUIRE(argv.front() == "ffmpeg");
    REQUIRE(find("-i") == "/music/song.flac");
    REQUIRE(find("-c:a") == "aac");
    REQUIRE(find("-b:a") == "256k");
    REQUIRE(find("-f") == "hls");
    REQUIRE(find("-hls_time") == "4");
    REQUIRE(find("-hls_playlist_type") == "vod");
    REQUIRE(find("-hls_segment_filename") == "/tmp/cache/track1/seg%05d.ts");
    REQUIRE(argv.back() == "/tmp/cache/track1/index.m3u8");
}

TEST_CASE("Segmenter::cached_file rejects unsafe rendition ids", "[hls][segmenter]") {
    sonarium::hls::SegmenterConfig cfg;
    cfg.cache_root = std::filesystem::temp_directory_path() / "sonarium-test-cache";
    sonarium::hls::Segmenter s{cfg};
    REQUIRE_FALSE(s.cached_file("../etc", "index.m3u8").has_value());
    REQUIRE_FALSE(s.cached_file("a/b", "index.m3u8").has_value());
    REQUIRE_FALSE(s.cached_file("", "index.m3u8").has_value());
}

TEST_CASE("Segmenter::cached_file returns nullopt when file is absent", "[hls][segmenter]") {
    sonarium::hls::SegmenterConfig cfg;
    cfg.cache_root = std::filesystem::temp_directory_path() / "sonarium-test-cache-absent";
    sonarium::hls::Segmenter s{cfg};
    REQUIRE_FALSE(s.cached_file("track1", "seg00001.ts").has_value());
}
