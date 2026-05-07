#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include "media/media_rendition.hpp"

namespace sonarium::hls {

// One playable rendition surfaced through HLS as a single-segment VOD entry.
// `media_url` is the absolute URL the media playlist will reference (typically
// the existing DLNA rendition route exposed by sonarium-dlna).
struct MediaVariant {
    std::string rendition_id;
    std::string mime_type;    // "audio/mpeg", "audio/mp4", "audio/wav", "audio/flac"
    std::string codec_string; // RFC 6381 string ("mp4a.40.2") or empty for non-AAC
    std::uint32_t bitrate_bps = 0;
    std::uint64_t duration_ms = 0;
    std::string media_url;
};

// Map the catalog rendition into the HLS variant shape, computing codec_string
// (currently only AAC LC has a meaningful entry; mp3/flac/wav return empty).
[[nodiscard]] MediaVariant variant_from_rendition(sonarium::media::MediaRendition const& m,
                                                  std::string media_url);

// Build the per-rendition VOD media playlist. The single segment URL points at
// `variant.media_url` and the EXTINF duration is taken from `variant.duration_ms`.
[[nodiscard]] std::string build_media_playlist(MediaVariant const& variant);

// Build the multi-variant master playlist. Each variant gets one #EXT-X-STREAM-INF
// row pointing at `<base_url>/hls/renditions/<rendition_id>/index.m3u8`.
[[nodiscard]] std::string build_master_playlist(std::span<MediaVariant const> variants,
                                                std::string_view base_url);

} // namespace sonarium::hls
