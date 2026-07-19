#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "transcode/ffmpeg_command.hpp"

namespace {

[[nodiscard]] bool argv_contains(std::vector<std::string> const& argv, std::string_view needle) {
    return std::ranges::any_of(argv, [&](auto const& s) { return s == needle; });
}

[[nodiscard]] std::string argv_value_after(std::vector<std::string> const& argv,
                                           std::string_view flag) {
    for (std::size_t i = 0; i + 1 < argv.size(); ++i) {
        if (argv[i] == flag) {
            return argv[i + 1];
        }
    }
    return {};
}

} // namespace

TEST_CASE("output_path_for replaces the extension", "[transcode][cmd]") {
    using sonarium::transcode::TargetCodec;
    REQUIRE(sonarium::transcode::output_path_for("/music/song.flac", TargetCodec::mp3)
            == "/music/song.mp3");
    REQUIRE(sonarium::transcode::output_path_for("/music/song.wav", TargetCodec::aac_lc)
            == "/music/song.m4a");
}

TEST_CASE("output_path_for handles a path with no extension", "[transcode][cmd]") {
    using sonarium::transcode::TargetCodec;
    REQUIRE(sonarium::transcode::output_path_for("/music/song", TargetCodec::mp3)
            == "/music/song.mp3");
}

TEST_CASE("output_path_for keeps directory components untouched", "[transcode][cmd]") {
    using sonarium::transcode::TargetCodec;
    REQUIRE(
        sonarium::transcode::output_path_for("relative/dir.with.dots/song.flac", TargetCodec::mp3)
        == "relative/dir.with.dots/song.mp3");
}

TEST_CASE("build_ffmpeg_argv emits libmp3lame for mp3 with bitrate", "[transcode][cmd]") {
    sonarium::transcode::TranscodeRequest req;
    req.input_path = "/in.flac";
    req.output_path = "/out.mp3";
    req.codec = sonarium::transcode::TargetCodec::mp3;
    req.bitrate_kbps = 192;

    auto const argv = sonarium::transcode::build_ffmpeg_argv(req);
    REQUIRE(argv.front() == "ffmpeg");
    REQUIRE(argv.back() == "/out.mp3");
    REQUIRE(argv_value_after(argv, "-i") == "/in.flac");
    REQUIRE(argv_value_after(argv, "-codec:a") == "libmp3lame");
    REQUIRE(argv_value_after(argv, "-b:a") == "192k");
    REQUIRE(argv_contains(argv, "-vn"));
}

TEST_CASE("build_ffmpeg_argv emits aac for AAC target", "[transcode][cmd]") {
    sonarium::transcode::TranscodeRequest req;
    req.input_path = "/in.flac";
    req.output_path = "/out.m4a";
    req.codec = sonarium::transcode::TargetCodec::aac_lc;
    req.bitrate_kbps = 256;

    auto const argv = sonarium::transcode::build_ffmpeg_argv(req);
    REQUIRE(argv_value_after(argv, "-codec:a") == "aac");
    REQUIRE(argv_value_after(argv, "-b:a") == "256k");
}

TEST_CASE("build_ffmpeg_argv toggles -y / -n by overwrite", "[transcode][cmd]") {
    sonarium::transcode::TranscodeRequest req;
    req.input_path = "/in.flac";
    req.output_path = "/out.mp3";

    req.overwrite = true;
    REQUIRE(argv_contains(sonarium::transcode::build_ffmpeg_argv(req), "-y"));

    req.overwrite = false;
    REQUIRE(argv_contains(sonarium::transcode::build_ffmpeg_argv(req), "-n"));
}
