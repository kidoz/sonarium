#pragma once

#include <cstdint>
#include <string>

#include "media/mime_type.hpp"

namespace sonarium::media {

enum class RenditionPurpose : std::uint8_t {
    dlna_lossy,
    dlna_lossy_optional,
    dlna_lossless,
    dlna_fallback,
    hls_audio,
};

// Pure data model. Persistence is owned by sonarium-catalog; transport by sonarium-dlna-core.
struct MediaRendition {
    std::string id;
    std::string track_id;
    AudioCodec codec;
    AudioContainer container;
    std::string mime_type;
    std::uint32_t bitrate_bps = 0;
    std::uint32_t sample_rate_hz = 0;
    std::uint8_t bit_depth = 0;
    std::uint8_t channels = 0;
    std::uint64_t duration_ms = 0;
    std::uint64_t size_bytes = 0;
    std::string checksum;
    std::string storage_path;
    std::string dlna_profile_name;
    std::string protocol_info;
    RenditionPurpose purpose = RenditionPurpose::dlna_lossy;
};

} // namespace sonarium::media
