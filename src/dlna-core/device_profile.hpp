#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "media/mime_type.hpp"

namespace sonarium::dlna {

// Per-renderer DLNA quirks. Drives protocolInfo formatting and resource ordering.
struct DeviceProfile {
    std::string name;

    // Codecs the renderer will accept, in preferred-resource order. The selector
    // emits resources matching this list in this order.
    std::vector<sonarium::media::AudioCodec> preferred_codec_order;

    // Per-codec MIME spelling override. Empty string means "use default_mime_for".
    // Keyed in the same enum range as AudioCodec for cheap lookup.
    std::vector<std::pair<sonarium::media::AudioCodec, std::string>> mime_overrides;

    bool requires_dlna_org_pn = true; // Samsung is famously strict here.
    bool exposes_flac = true;
    bool prefers_lpcm_fallback = false;
    bool search_enabled = false;
    std::uint32_t max_sample_rate_hz = 192'000;
    std::uint8_t max_bit_depth = 24;

    // Match rules. The first profile whose match rules pass is selected.
    std::vector<std::string> user_agent_substrings;
    std::vector<std::pair<std::string, std::string>> header_substrings; // (name, substring)

    // Free-form opaque quirks string for renderer-specific notes.
    std::string quirks_json;
};

// Look up the MIME spelling this profile wants for `rendition` codec, falling
// back to default_mime_for when there's no explicit override.
[[nodiscard]] std::string_view mime_for(DeviceProfile const& profile,
                                        sonarium::media::RenditionMime rendition) noexcept;

// True iff the profile lists `codec` in `preferred_codec_order`.
[[nodiscard]] bool profile_supports_codec(DeviceProfile const& profile,
                                          sonarium::media::AudioCodec codec) noexcept;

// HTTP headers presented to the registry are case-insensitive on name; values
// are matched case-insensitively as substrings.
struct RequestHeaders {
    std::string_view user_agent;
    std::span<std::pair<std::string_view, std::string_view> const> extras; // optional
};

class DeviceProfileRegistry {
public:
    // Construct with the built-in profile set (Generic +
    // Samsung/LG/Sony/Yamaha/VLC-Kodi/BubbleUPnP).
    [[nodiscard]] static DeviceProfileRegistry with_defaults();

    void add(DeviceProfile profile);

    // Match a renderer to a profile. Falls back to "Generic DLNA" if no rule matches.
    [[nodiscard]] DeviceProfile const& match(RequestHeaders headers) const noexcept;

    [[nodiscard]] std::span<DeviceProfile const> profiles() const noexcept { return profiles_; }

private:
    std::vector<DeviceProfile> profiles_;
};

} // namespace sonarium::dlna
