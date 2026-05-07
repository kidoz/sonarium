#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace sonarium::scanner {

// Audio characteristics + tags extracted from a single media file. All
// numeric fields default to zero; callers can fall back to defaults when a
// value is unavailable.
struct AudioMetadata {
    std::uint64_t duration_ms = 0;
    std::uint32_t bitrate_bps = 0;
    std::uint32_t sample_rate_hz = 0;
    std::uint8_t channels = 0;
    std::uint8_t bit_depth = 0;
    std::optional<std::string> artist = std::nullopt;
    std::optional<std::string> album = std::nullopt;
    std::optional<std::string> title = std::nullopt;
};

// Parse the line-based output produced by:
//   ffprobe -v error
//     -show_entries format=duration,bit_rate
//     -show_entries stream=codec_type,sample_rate,channels,bits_per_sample,bit_rate
//     -show_entries format_tags=artist,album,title
//     -of default=noprint_wrappers=0
//
// `[STREAM]`/`[FORMAT]` markers delimit sections; we use the first stream with
// codec_type=audio. Missing fields stay at their default values.
[[nodiscard]] AudioMetadata parse_ffprobe_default_output(std::string_view text);

// Spawn `ffprobe` against `path` and return the parsed metadata. Returns an
// error string when the process can't be spawned or exits non-zero. The
// scanner treats this as a soft error and continues with default values.
[[nodiscard]] std::expected<AudioMetadata, std::string>
read_audio_metadata(std::filesystem::path const& path);

} // namespace sonarium::scanner
