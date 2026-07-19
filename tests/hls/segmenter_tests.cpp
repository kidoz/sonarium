#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
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

TEST_CASE("build_segmenter_argv respects per-rendition bitrate override", "[hls][segmenter]") {
    sonarium::hls::SegmenterConfig cfg;
    cfg.cache_root = "/tmp/cache";
    cfg.segment_duration_seconds = 6;
    cfg.bitrate_kbps = 192;

    auto const argv =
        sonarium::hls::build_segmenter_argv("/in.flac", "/tmp/cache/track1", cfg, 320);

    auto const find = [&](std::string_view flag) -> std::string {
        for (std::size_t i = 0; i + 1 < argv.size(); ++i) {
            if (argv[i] == flag) {
                return argv[i + 1];
            }
        }
        return {};
    };
    REQUIRE(find("-b:a") == "320k"); // override wins over cfg default
}

TEST_CASE("build_segmenter_argv falls back to cfg default when override is zero",
          "[hls][segmenter]") {
    sonarium::hls::SegmenterConfig cfg;
    cfg.cache_root = "/tmp/cache";
    cfg.bitrate_kbps = 192;
    auto const argv = sonarium::hls::build_segmenter_argv("/in.flac", "/tmp/cache/track1", cfg, 0);

    auto const find = [&](std::string_view flag) -> std::string {
        for (std::size_t i = 0; i + 1 < argv.size(); ++i) {
            if (argv[i] == flag) {
                return argv[i + 1];
            }
        }
        return {};
    };
    REQUIRE(find("-b:a") == "192k");
}

TEST_CASE("Segmenter::cached_file rejects unsafe rendition ids", "[hls][segmenter]") {
    sonarium::hls::SegmenterConfig cfg;
    cfg.cache_root = std::filesystem::temp_directory_path() / "sonarium-test-cache";
    sonarium::hls::Segmenter const s{cfg};
    REQUIRE_FALSE(s.cached_file("../etc", "index.m3u8").has_value());
    REQUIRE_FALSE(s.cached_file("a/b", "index.m3u8").has_value());
    REQUIRE_FALSE(s.cached_file("", "index.m3u8").has_value());
}

TEST_CASE("Segmenter::cached_file returns nullopt when file is absent", "[hls][segmenter]") {
    sonarium::hls::SegmenterConfig cfg;
    cfg.cache_root = std::filesystem::temp_directory_path() / "sonarium-test-cache-absent";
    sonarium::hls::Segmenter const s{cfg};
    REQUIRE_FALSE(s.cached_file("track1", "seg00001.ts").has_value());
}

namespace {

// Build a fake cached rendition dir: a complete index.m3u8 plus one segment
// file of `payload_bytes`, with the playlist mtime pushed `age` into the past
// so LRU ordering is deterministic.
void seed_cache_entry(std::filesystem::path const& cache_root,
                      std::string const& rendition_id,
                      std::size_t payload_bytes,
                      std::chrono::minutes age) {
    namespace fs = std::filesystem;
    auto const dir = cache_root / rendition_id;
    fs::create_directories(dir);
    {
        std::ofstream playlist{dir / "index.m3u8", std::ios::binary};
        playlist << "#EXTM3U\n#EXTINF:6.0,\nseg00000.ts\n#EXT-X-ENDLIST\n";
    }
    {
        std::ofstream segment{dir / "seg00000.ts", std::ios::binary};
        segment << std::string(payload_bytes, 'x');
    }
    fs::last_write_time(dir / "index.m3u8", fs::file_time_type::clock::now() - age);
}

[[nodiscard]] std::filesystem::path fresh_cache_root(std::string const& name) {
    auto const root = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    return root;
}

} // namespace

TEST_CASE("evict_over_cap removes least-recently-used renditions first", "[hls][segmenter]") {
    auto const root = fresh_cache_root("sonarium-evict-lru");
    seed_cache_entry(root, "oldest", 1000, std::chrono::minutes{30});
    seed_cache_entry(root, "middle", 1000, std::chrono::minutes{20});
    seed_cache_entry(root, "newest", 1000, std::chrono::minutes{10});

    sonarium::hls::SegmenterConfig cfg;
    cfg.cache_root = root;
    cfg.max_cache_bytes = 2500; // fits two entries, not three
    sonarium::hls::Segmenter s{cfg};

    s.evict_over_cap();

    REQUIRE_FALSE(std::filesystem::exists(root / "oldest"));
    REQUIRE(std::filesystem::exists(root / "middle"));
    REQUIRE(std::filesystem::exists(root / "newest"));
    std::filesystem::remove_all(root);
}

TEST_CASE("evict_over_cap never evicts the rendition it was asked to keep", "[hls][segmenter]") {
    auto const root = fresh_cache_root("sonarium-evict-keep");
    seed_cache_entry(root, "keep-me", 1000, std::chrono::minutes{30}); // oldest
    seed_cache_entry(root, "other", 1000, std::chrono::minutes{10});

    sonarium::hls::SegmenterConfig cfg;
    cfg.cache_root = root;
    cfg.max_cache_bytes = 1500; // fits one entry
    sonarium::hls::Segmenter s{cfg};

    s.evict_over_cap("keep-me");

    REQUIRE(std::filesystem::exists(root / "keep-me"));
    REQUIRE_FALSE(std::filesystem::exists(root / "other"));
    std::filesystem::remove_all(root);
}

TEST_CASE("evict_over_cap with zero cap is a no-op", "[hls][segmenter]") {
    auto const root = fresh_cache_root("sonarium-evict-nocap");
    seed_cache_entry(root, "a", 5000, std::chrono::minutes{30});

    sonarium::hls::SegmenterConfig cfg;
    cfg.cache_root = root;
    cfg.max_cache_bytes = 0;
    sonarium::hls::Segmenter s{cfg};

    s.evict_over_cap();

    REQUIRE(std::filesystem::exists(root / "a"));
    std::filesystem::remove_all(root);
}

TEST_CASE("evict_over_cap removes stale .part leftovers but spares fresh ones",
          "[hls][segmenter]") {
    namespace fs = std::filesystem;
    auto const root = fresh_cache_root("sonarium-evict-part");
    fs::create_directories(root / "crashed.part");
    fs::last_write_time(root / "crashed.part",
                        fs::file_time_type::clock::now() - std::chrono::hours{2});
    fs::create_directories(root / "active.part");

    sonarium::hls::SegmenterConfig cfg;
    cfg.cache_root = root;
    cfg.max_cache_bytes = 1; // force the sweep to run
    sonarium::hls::Segmenter s{cfg};

    s.evict_over_cap();

    REQUIRE_FALSE(fs::exists(root / "crashed.part"));
    REQUIRE(fs::exists(root / "active.part"));
    std::filesystem::remove_all(root);
}

TEST_CASE("is_safe_rendition_id rejects the reserved .part suffix", "[hls][segmenter]") {
    sonarium::hls::SegmenterConfig cfg;
    cfg.cache_root = std::filesystem::temp_directory_path() / "sonarium-part-suffix";
    sonarium::hls::Segmenter const s{cfg};
    REQUIRE_FALSE(s.cached_file("track1.part", "index.m3u8").has_value());
}

TEST_CASE("ensure_segments surfaces ffmpeg failure and leaves no cache behind",
          "[hls][segmenter]") {
    if (std::system("ffmpeg -version >/dev/null 2>&1") != 0) {
        SKIP("ffmpeg not on PATH — subprocess failure path not testable");
    }
    namespace fs = std::filesystem;
    auto const root = fresh_cache_root("sonarium-ffmpeg-failure");
    auto const bogus_source = root / "not-audio.txt";
    std::ofstream{bogus_source, std::ios::binary} << "this is not an audio file";

    sonarium::hls::SegmenterConfig cfg;
    cfg.cache_root = root;
    sonarium::hls::Segmenter s{cfg};

    auto const result = s.ensure_segments("bogus", bogus_source);
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().kind == sonarium::hls::SegmenterError::Kind::failed);
    // Atomicity: neither a poisoned cache dir nor a .part leftover.
    REQUIRE_FALSE(fs::exists(root / "bogus"));
    REQUIRE_FALSE(fs::exists(root / "bogus.part"));
    fs::remove_all(root);
}
