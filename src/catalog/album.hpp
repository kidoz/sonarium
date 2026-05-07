#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace sonarium::catalog {

struct Album {
    std::string id;
    std::string artist_id;
    std::string title;
    std::string sort_title;
    std::optional<std::uint16_t> release_year;
    std::optional<std::string> cover_art_asset_id;
};

} // namespace sonarium::catalog
