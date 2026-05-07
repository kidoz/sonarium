#include "media/mime_type.hpp"

#include <cctype>
#include <cstddef>
#include <string>

namespace sonarium::media {

std::string ascii_lowercase(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (auto c : s) {
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

std::string_view default_mime_for(RenditionMime rendition) noexcept {
    switch (rendition.codec) {
        case AudioCodec::mp3:
            return "audio/mpeg";
        case AudioCodec::aac_lc:
            return "audio/mp4";
        case AudioCodec::pcm_wav:
            return "audio/wav";
        case AudioCodec::flac:
            return "audio/flac";
        case AudioCodec::alac:
            return "audio/mp4";
    }
    return "application/octet-stream";
}

std::optional<std::string_view> mime_from_extension(std::string_view ext) noexcept {
    if (ext.starts_with('.')) {
        ext.remove_prefix(1);
    }

    constexpr struct ExtRow {
        std::string_view ext;
        std::string_view mime;
    } table[] = {
        {"mp3", "audio/mpeg"},
        {"m4a", "audio/mp4"},
        {"mp4", "audio/mp4"},
        {"wav", "audio/wav"},
        {"flac", "audio/flac"},
        {"jpg", "image/jpeg"},
        {"jpeg", "image/jpeg"},
        {"png", "image/png"},
    };

    for (auto const& row : table) {
        if (row.ext.size() != ext.size()) {
            continue;
        }
        bool match = true;
        for (std::size_t i = 0; i < ext.size(); ++i) {
            auto const a = static_cast<unsigned char>(row.ext[i]);
            auto const b = static_cast<unsigned char>(ext[i]);
            if (std::tolower(a) != std::tolower(b)) {
                match = false;
                break;
            }
        }
        if (match) {
            return row.mime;
        }
    }
    return std::nullopt;
}

std::optional<std::string_view> dlna_org_pn_for(RenditionMime rendition) noexcept {
    if (rendition.codec == AudioCodec::mp3 && rendition.container == AudioContainer::mp3) {
        return "MP3";
    }
    if (rendition.codec == AudioCodec::pcm_wav && rendition.container == AudioContainer::wav) {
        return "LPCM";
    }
    return std::nullopt;
}

} // namespace sonarium::media
