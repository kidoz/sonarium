#include <catch2/catch_test_macros.hpp>
#include <string_view>

#include "scanner/audio_metadata.hpp"

namespace {

constexpr std::string_view mp3_no_tags = R"([STREAM]
codec_type=audio
sample_rate=44100
channels=2
bits_per_sample=0
bit_rate=128000
[/STREAM]
[FORMAT]
duration=2.000000
bit_rate=131412
[/FORMAT]
)";

constexpr std::string_view flac_with_tags = R"([STREAM]
codec_type=audio
sample_rate=96000
channels=2
bits_per_sample=24
bit_rate=2304000
[/STREAM]
[FORMAT]
duration=180.500000
bit_rate=2400000
TAG:artist=Pink Floyd
TAG:album=The Wall
TAG:title=Another Brick in the Wall
[/FORMAT]
)";

constexpr std::string_view file_with_video_first_then_audio = R"([STREAM]
codec_type=video
sample_rate=
channels=
[/STREAM]
[STREAM]
codec_type=audio
sample_rate=48000
channels=2
bits_per_sample=16
bit_rate=320000
[/STREAM]
[FORMAT]
duration=10.000000
[/FORMAT]
)";

constexpr std::string_view empty_input;

} // namespace

TEST_CASE("parse_ffprobe_default_output extracts mp3 stream + format fields", "[scanner][meta]") {
    auto const m = sonarium::scanner::parse_ffprobe_default_output(mp3_no_tags);
    REQUIRE(m.duration_ms == 2000);
    REQUIRE(m.sample_rate_hz == 44100);
    REQUIRE(m.channels == 2);
    REQUIRE(m.bit_depth == 0);
    REQUIRE(m.bitrate_bps == 131412); // format.bit_rate wins over stream.bit_rate
    REQUIRE_FALSE(m.artist.has_value());
}

TEST_CASE("parse_ffprobe_default_output reads flac fields and tags", "[scanner][meta]") {
    auto const m = sonarium::scanner::parse_ffprobe_default_output(flac_with_tags);
    REQUIRE(m.duration_ms == 180500);
    REQUIRE(m.sample_rate_hz == 96000);
    REQUIRE(m.channels == 2);
    REQUIRE(m.bit_depth == 24);
    REQUIRE(m.bitrate_bps == 2400000);
    REQUIRE(m.artist.has_value());
    REQUIRE(*m.artist == "Pink Floyd");
    REQUIRE(*m.album == "The Wall");
    REQUIRE(*m.title == "Another Brick in the Wall");
}

TEST_CASE("parse_ffprobe_default_output skips video streams and prefers first audio",
          "[scanner][meta]") {
    auto const m =
        sonarium::scanner::parse_ffprobe_default_output(file_with_video_first_then_audio);
    REQUIRE(m.sample_rate_hz == 48000);
    REQUIRE(m.channels == 2);
    REQUIRE(m.bit_depth == 16);
    REQUIRE(m.bitrate_bps == 320000); // only stream.bit_rate (no format.bit_rate)
    REQUIRE(m.duration_ms == 10000);
}

TEST_CASE("parse_ffprobe_default_output handles empty input", "[scanner][meta]") {
    auto const m = sonarium::scanner::parse_ffprobe_default_output(empty_input);
    REQUIRE(m.duration_ms == 0);
    REQUIRE(m.sample_rate_hz == 0);
    REQUIRE(m.channels == 0);
    REQUIRE(m.bitrate_bps == 0);
}

TEST_CASE("parse_ffprobe_default_output rounds fractional ms correctly", "[scanner][meta]") {
    constexpr std::string_view text = R"([FORMAT]
duration=1.4995
[/FORMAT]
)";
    auto const m = sonarium::scanner::parse_ffprobe_default_output(text);
    REQUIRE(m.duration_ms == 1500); // llround(1499.5)=1500
}
