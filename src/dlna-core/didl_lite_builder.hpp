#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sonarium::dlna {

struct DidlContainer {
    std::string id;
    std::string parent_id;
    std::string title;
    std::string upnp_class; // e.g. "object.container.album.musicAlbum"
    std::optional<std::uint32_t> child_count;
};

struct DidlResource {
    std::string protocol_info;
    std::string url;
    std::optional<std::string> duration; // pre-formatted "H:MM:SS.mmm"
    std::optional<std::uint32_t> bitrate_bps;
    std::optional<std::uint32_t> sample_rate_hz;
    std::optional<std::uint8_t> channels;
    std::optional<std::uint64_t> size_bytes;
};

struct DidlItem {
    std::string id;
    std::string parent_id;
    std::string title;
    std::string upnp_class; // e.g. "object.item.audioItem.musicTrack"
    std::optional<std::string> creator;
    std::optional<std::string> album;
    std::optional<std::string> genre;
    std::optional<std::uint32_t> original_track_number;
    std::optional<std::string> album_art_uri;
    std::vector<DidlResource> resources;
};

// Build a DIDL-Lite payload (the inner string used as ContentDirectory:Browse Result).
// Items and containers are emitted in the order given.
[[nodiscard]] std::string build_didl_lite(std::vector<DidlContainer> const& containers,
                                          std::vector<DidlItem> const& items);

[[nodiscard]] inline std::string build_didl_lite(std::vector<DidlContainer> const& containers) {
    return build_didl_lite(containers, {});
}

[[nodiscard]] inline std::string build_didl_lite(std::vector<DidlItem> const& items) {
    return build_didl_lite({}, items);
}

} // namespace sonarium::dlna
