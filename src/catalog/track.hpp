#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace sonarium::catalog {

struct Track {
    std::string id;
    std::string album_id;
    std::string artist_id;
    std::string title;
    std::string sort_title;
    std::optional<std::uint16_t> disc_number;
    std::optional<std::uint16_t> track_number;
    std::uint64_t duration_ms = 0;
};

} // namespace sonarium::catalog
