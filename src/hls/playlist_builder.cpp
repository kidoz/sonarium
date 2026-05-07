#include "hls/playlist_builder.hpp"

#include <cstdint>
#include <string>

namespace sonarium::hls {

namespace {

// RFC 6381 codec string. Only AAC-LC has a single canonical value here; mp3
// and lossless containers don't need a CODECS attribute (Apple's HLS authoring
// spec lets you omit it for audio-only mp3 streams).
[[nodiscard]] std::string codec_string_for(sonarium::media::AudioCodec codec) {
    using sonarium::media::AudioCodec;
    switch (codec) {
        case AudioCodec::aac_lc:
            return "mp4a.40.2";
        case AudioCodec::mp3:
        case AudioCodec::pcm_wav:
        case AudioCodec::flac:
        case AudioCodec::alac:
            return {};
    }
    return {};
}

// Format a millisecond count as a decimal with 3 fractional digits (HLS
// EXTINF duration). Manual instead of std::format so we don't pull in <format>
// for one number.
[[nodiscard]] std::string format_seconds(std::uint64_t ms) {
    auto const whole = ms / 1000;
    auto const frac = ms % 1000;
    std::string out = std::to_string(whole);
    out.push_back('.');
    if (frac < 100) {
        out.push_back('0');
    }
    if (frac < 10) {
        out.push_back('0');
    }
    out += std::to_string(frac);
    return out;
}

[[nodiscard]] std::uint64_t target_duration_seconds(std::uint64_t ms) noexcept {
    if (ms == 0) {
        return 1;
    }
    return (ms + 999) / 1000; // ceil
}

} // namespace

MediaVariant variant_from_rendition(sonarium::media::MediaRendition const& m,
                                    std::string media_url) {
    MediaVariant out;
    out.rendition_id = m.id;
    out.mime_type = m.mime_type;
    out.codec_string = codec_string_for(m.codec);
    out.bitrate_bps = m.bitrate_bps;
    out.duration_ms = m.duration_ms;
    out.media_url = std::move(media_url);
    return out;
}

std::string build_media_playlist(MediaVariant const& variant) {
    std::string out;
    out.reserve(256);
    out.append("#EXTM3U\n");
    out.append("#EXT-X-VERSION:3\n");
    out.append("#EXT-X-PLAYLIST-TYPE:VOD\n");
    out.append("#EXT-X-TARGETDURATION:");
    out.append(std::to_string(target_duration_seconds(variant.duration_ms)));
    out.push_back('\n');
    out.append("#EXT-X-MEDIA-SEQUENCE:0\n");
    out.append("#EXTINF:");
    out.append(format_seconds(variant.duration_ms));
    out.append(",\n");
    out.append(variant.media_url);
    out.push_back('\n');
    out.append("#EXT-X-ENDLIST\n");
    return out;
}

std::string build_master_playlist(std::span<MediaVariant const> variants,
                                  std::string_view base_url) {
    std::string out;
    out.reserve(256 + variants.size() * 128);
    out.append("#EXTM3U\n");
    out.append("#EXT-X-VERSION:3\n");
    for (auto const& v : variants) {
        out.append("#EXT-X-STREAM-INF:BANDWIDTH=");
        out.append(std::to_string(v.bitrate_bps == 0 ? std::uint32_t{0} : v.bitrate_bps));
        if (!v.codec_string.empty()) {
            out.append(",CODECS=\"");
            out.append(v.codec_string);
            out.push_back('"');
        }
        out.push_back('\n');
        out.append(base_url);
        out.append("/hls/renditions/");
        out.append(v.rendition_id);
        out.append("/index.m3u8\n");
    }
    return out;
}

} // namespace sonarium::hls
