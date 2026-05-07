#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace sonarium::media {

enum class AudioCodec : std::uint8_t {
    mp3,
    aac_lc,
    pcm_wav,
    flac,
    alac,
};

enum class AudioContainer : std::uint8_t {
    mp3,
    mp4,
    wav,
    flac,
};

struct RenditionMime {
    AudioCodec codec;
    AudioContainer container;
};

[[nodiscard]] std::string_view default_mime_for(RenditionMime rendition) noexcept;

[[nodiscard]] std::optional<std::string_view> mime_from_extension(std::string_view ext) noexcept;

[[nodiscard]] std::optional<std::string_view> dlna_org_pn_for(RenditionMime rendition) noexcept;

[[nodiscard]] std::string ascii_lowercase(std::string_view s);

} // namespace sonarium::media
