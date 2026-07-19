#include "dlna-core/device_profile.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <utility>

#include "media/mime_type.hpp"

namespace sonarium::dlna {

namespace {

[[nodiscard]] bool icontains(std::string_view haystack, std::string_view needle) noexcept {
    if (needle.empty()) {
        return true;
    }
    if (haystack.size() < needle.size()) {
        return false;
    }
    auto const limit = haystack.size() - needle.size();
    for (std::size_t i = 0; i <= limit; ++i) {
        bool match = true;
        for (std::size_t j = 0; j < needle.size(); ++j) {
            auto const a = static_cast<unsigned char>(haystack[i + j]);
            auto const b = static_cast<unsigned char>(needle[j]);
            if (std::tolower(a) != std::tolower(b)) {
                match = false;
                break;
            }
        }
        if (match) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool ieq(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        auto const x = static_cast<unsigned char>(a[i]);
        auto const y = static_cast<unsigned char>(b[i]);
        if (std::tolower(x) != std::tolower(y)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] DeviceProfile generic_profile() {
    DeviceProfile p;
    p.name = "Generic DLNA";
    p.preferred_codec_order = {
        sonarium::media::AudioCodec::mp3,
        sonarium::media::AudioCodec::aac_lc,
        sonarium::media::AudioCodec::pcm_wav,
    };
    p.requires_dlna_org_pn = true;
    p.exposes_flac = false;
    return p;
}

[[nodiscard]] DeviceProfile vlc_kodi_profile() {
    DeviceProfile p;
    p.name = "VLC/Kodi";
    p.preferred_codec_order = {
        sonarium::media::AudioCodec::flac,
        sonarium::media::AudioCodec::mp3,
        sonarium::media::AudioCodec::aac_lc,
    };
    p.requires_dlna_org_pn = false;
    p.exposes_flac = true;
    p.search_enabled = true;
    p.user_agent_substrings = {"VLC/", "Kodi", "XBMC"};
    return p;
}

[[nodiscard]] DeviceProfile bubble_upnp_profile() {
    DeviceProfile p;
    p.name = "BubbleUPnP";
    p.preferred_codec_order = {
        sonarium::media::AudioCodec::flac,
        sonarium::media::AudioCodec::alac,
        sonarium::media::AudioCodec::mp3,
        sonarium::media::AudioCodec::aac_lc,
    };
    p.requires_dlna_org_pn = false;
    p.exposes_flac = true;
    p.search_enabled = true;
    p.user_agent_substrings = {"BubbleUPnP"};
    return p;
}

[[nodiscard]] DeviceProfile samsung_tv_profile() {
    DeviceProfile p;
    p.name = "Samsung TV";
    p.preferred_codec_order = {
        sonarium::media::AudioCodec::mp3,
        sonarium::media::AudioCodec::aac_lc,
        sonarium::media::AudioCodec::pcm_wav,
    };
    p.requires_dlna_org_pn = true;
    p.exposes_flac = false;
    p.prefers_lpcm_fallback = true;
    p.user_agent_substrings = {"SEC_HHP_", "Samsung", "DLNADOC"};
    p.header_substrings = {{"X-AV-Client-Info", "Samsung"}};
    return p;
}

[[nodiscard]] DeviceProfile lg_tv_profile() {
    DeviceProfile p;
    p.name = "LG TV";
    p.preferred_codec_order = {
        sonarium::media::AudioCodec::mp3,
        sonarium::media::AudioCodec::aac_lc,
    };
    p.requires_dlna_org_pn = true;
    p.exposes_flac = false;
    p.user_agent_substrings = {"LG", "webOS"};
    return p;
}

[[nodiscard]] DeviceProfile sony_profile() {
    DeviceProfile p;
    p.name = "Sony TV/Speaker";
    p.preferred_codec_order = {
        sonarium::media::AudioCodec::mp3,
        sonarium::media::AudioCodec::aac_lc,
        sonarium::media::AudioCodec::flac,
    };
    p.requires_dlna_org_pn = true;
    p.exposes_flac = true;
    p.user_agent_substrings = {"Sony", "BRAVIA"};
    return p;
}

[[nodiscard]] DeviceProfile receiver_profile() {
    DeviceProfile p;
    p.name = "Yamaha/Denon/Marantz";
    p.preferred_codec_order = {
        sonarium::media::AudioCodec::flac,
        sonarium::media::AudioCodec::alac,
        sonarium::media::AudioCodec::mp3,
        sonarium::media::AudioCodec::pcm_wav,
    };
    p.requires_dlna_org_pn = true;
    p.exposes_flac = true;
    p.user_agent_substrings = {"Yamaha", "Denon", "Marantz", "HEOS"};
    return p;
}

} // namespace

std::string_view mime_for(DeviceProfile const& profile,
                          sonarium::media::RenditionMime rendition) noexcept {
    for (auto const& [codec, mime] : profile.mime_overrides) {
        if (codec == rendition.codec && !mime.empty()) {
            return mime;
        }
    }
    return sonarium::media::default_mime_for(rendition);
}

bool profile_supports_codec(DeviceProfile const& profile,
                            sonarium::media::AudioCodec codec) noexcept {
    return std::ranges::any_of(profile.preferred_codec_order,
                               [codec](sonarium::media::AudioCodec c) { return c == codec; });
}

DeviceProfileRegistry DeviceProfileRegistry::with_defaults() {
    DeviceProfileRegistry r;
    r.add(samsung_tv_profile());
    r.add(lg_tv_profile());
    r.add(sony_profile());
    r.add(receiver_profile());
    r.add(vlc_kodi_profile());
    r.add(bubble_upnp_profile());
    r.add(generic_profile()); // generic must come last; matches everything
    return r;
}

void DeviceProfileRegistry::add(DeviceProfile profile) {
    profiles_.push_back(std::move(profile));
}

DeviceProfile const& DeviceProfileRegistry::match(RequestHeaders headers) const noexcept {
    for (auto const& p : profiles_) {
        bool matched = false;
        for (auto const& ua : p.user_agent_substrings) {
            if (icontains(headers.user_agent, ua)) {
                matched = true;
                break;
            }
        }
        if (!matched) {
            for (auto const& [name, sub] : p.header_substrings) {
                for (auto const& [hname, hvalue] : headers.extras) {
                    if (ieq(hname, name) && icontains(hvalue, sub)) {
                        matched = true;
                        break;
                    }
                }
                if (matched) {
                    break;
                }
            }
        }
        if (p.user_agent_substrings.empty() && p.header_substrings.empty()) {
            // Generic / fallback profile — matches if nothing earlier did.
            matched = true;
        }
        if (matched) {
            return p;
        }
    }
    // The defaults always include a generic fallback. If the registry was reduced,
    // returning the last profile is the safest behavior.
    return profiles_.back();
}

} // namespace sonarium::dlna
