#include <array>
#include <catch2/catch_test_macros.hpp>
#include <span>
#include <string>

#include "hls/playlist_builder.hpp"
#include "media/media_rendition.hpp"

namespace {

[[nodiscard]] sonarium::hls::MediaVariant make_variant(std::string id,
                                                       std::string mime,
                                                       std::string codec,
                                                       std::uint32_t bitrate,
                                                       std::uint64_t duration_ms,
                                                       std::string url) {
    sonarium::hls::MediaVariant v;
    v.rendition_id = std::move(id);
    v.mime_type = std::move(mime);
    v.codec_string = std::move(codec);
    v.bitrate_bps = bitrate;
    v.duration_ms = duration_ms;
    v.media_url = std::move(url);
    return v;
}

} // namespace

TEST_CASE("variant_from_rendition fills codec string for AAC", "[hls][playlist]") {
    sonarium::media::MediaRendition r{};
    r.id = "track1:m4a";
    r.codec = sonarium::media::AudioCodec::aac_lc;
    r.bitrate_bps = 256'000;
    r.duration_ms = 180'000;

    auto const v = sonarium::hls::variant_from_rendition(r, "http://x/y");
    REQUIRE(v.rendition_id == "track1:m4a");
    REQUIRE(v.codec_string == "mp4a.40.2");
    REQUIRE(v.bitrate_bps == 256'000);
    REQUIRE(v.duration_ms == 180'000);
    REQUIRE(v.media_url == "http://x/y");
}

TEST_CASE("variant_from_rendition leaves codec string empty for mp3", "[hls][playlist]") {
    sonarium::media::MediaRendition r{};
    r.codec = sonarium::media::AudioCodec::mp3;
    auto const v = sonarium::hls::variant_from_rendition(r, "http://x");
    REQUIRE(v.codec_string.empty());
}

TEST_CASE("build_media_playlist emits VOD playlist with single segment", "[hls][playlist]") {
    auto const v = make_variant("r1",
                                "audio/mpeg",
                                /*codec=*/"",
                                320'000,
                                240'000,
                                "http://media.example/r1");
    auto const m3u8 = sonarium::hls::build_media_playlist(v);

    REQUIRE(m3u8.starts_with("#EXTM3U\n"));
    REQUIRE(m3u8.contains("#EXT-X-VERSION:3"));
    REQUIRE(m3u8.contains("#EXT-X-PLAYLIST-TYPE:VOD"));
    REQUIRE(m3u8.contains("#EXT-X-TARGETDURATION:240"));
    REQUIRE(m3u8.contains("#EXTINF:240.000,"));
    REQUIRE(m3u8.contains("http://media.example/r1\n"));
    REQUIRE(m3u8.contains("#EXT-X-ENDLIST\n"));
}

TEST_CASE("build_media_playlist rounds target-duration up for sub-second remainders",
          "[hls][playlist]") {
    auto const v = make_variant("r1", "audio/mpeg", "", 128'000, 240'500, "http://x/r1");
    auto const m3u8 = sonarium::hls::build_media_playlist(v);
    REQUIRE(m3u8.contains("#EXT-X-TARGETDURATION:241"));
    REQUIRE(m3u8.contains("#EXTINF:240.500,"));
}

TEST_CASE("build_master_playlist lists every variant with bandwidth", "[hls][playlist]") {
    std::array const variants{
        make_variant("r-mp3", "audio/mpeg", "", 320'000, 240'000, "http://m/r-mp3"),
        make_variant("r-aac", "audio/mp4", "mp4a.40.2", 128'000, 240'000, "http://m/r-aac"),
    };
    auto const m3u8 = sonarium::hls::build_master_playlist(
        std::span<sonarium::hls::MediaVariant const>{variants}, "http://server.example");

    REQUIRE(m3u8.starts_with("#EXTM3U\n"));
    REQUIRE(m3u8.contains("#EXT-X-STREAM-INF:BANDWIDTH=320000\n"));
    REQUIRE(m3u8.contains("http://server.example/hls/renditions/r-mp3/index.m3u8\n"));
    REQUIRE(m3u8.contains("#EXT-X-STREAM-INF:BANDWIDTH=128000,CODECS=\"mp4a.40.2\"\n"));
    REQUIRE(m3u8.contains("http://server.example/hls/renditions/r-aac/index.m3u8\n"));
}
