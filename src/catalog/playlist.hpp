#pragma once

#include <string>
#include <vector>

namespace sonarium::catalog {

struct PlaylistItem {
    std::string track_id;
    std::uint32_t position = 0;
};

struct Playlist {
    std::string id;
    std::string title;
    std::vector<PlaylistItem> items;
};

} // namespace sonarium::catalog
