#pragma once

#include <cstdint>
#include <string>

#include "media/mime_type.hpp"

namespace sonarium::transcode {

// Target codec for a transcode job. Aligned with sonarium::media::AudioCodec
// but limited to the codecs `ffmpeg` will encode for us; lossless inputs we
// pass through unchanged via `sonariumctl import`, never re-encode.
enum class TargetCodec : std::uint8_t {
    mp3,    // libmp3lame, .mp3
    aac_lc, // aac, .m4a
};

[[nodiscard]] sonarium::media::AudioCodec to_audio_codec(TargetCodec c) noexcept;
[[nodiscard]] sonarium::media::AudioContainer to_audio_container(TargetCodec c) noexcept;

// Inputs to a single transcode invocation.
struct TranscodeRequest {
    std::string input_path;
    std::string output_path;
    TargetCodec codec = TargetCodec::mp3;
    std::uint32_t bitrate_kbps = 128;
    bool overwrite = true;
};

struct TranscodeResult {
    int exit_code = 0;
    std::string stderr_excerpt; // last ~4 KiB of ffmpeg stderr — useful on failure
};

} // namespace sonarium::transcode
