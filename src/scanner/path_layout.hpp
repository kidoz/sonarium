#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace sonarium::scanner {

// What we extract from a path that follows the on-disk convention
//   <root>/<artist>/<album>/<NN - title>.<ext>
// (track number prefix is optional). All fields are raw text from the path
// — slug() turns them into id-safe identifiers downstream.
struct ParsedTrackPath {
    std::string artist_name;
    std::string album_name;
    std::string track_title;
    std::optional<std::uint16_t> track_number;
    std::string extension; // lowercase, no leading dot
};

// Convert arbitrary text into an id-safe slug: lowercased ASCII alphanumerics,
// non-alphanumeric runs collapsed to a single `-`, leading/trailing `-` stripped.
//   "Pink Floyd"          -> "pink-floyd"
//   "OK Computer (1997)"  -> "ok-computer-1997"
//   "Aerosmith - Toys"    -> "aerosmith-toys"
[[nodiscard]] std::string slug(std::string_view text);

// Parse a path *relative to the media root*. Returns nullopt unless the path
// has exactly three components and the leaf has an extension. The caller is
// responsible for filtering by extension (only audio files become tracks).
[[nodiscard]] std::optional<ParsedTrackPath>
parse_track_path(std::filesystem::path const& relative_path);

} // namespace sonarium::scanner
