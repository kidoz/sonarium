#include "scanner/path_layout.hpp"

#include <cctype>
#include <charconv>
#include <string>
#include <vector>

namespace sonarium::scanner {

namespace {

[[nodiscard]] bool is_separator(char c) noexcept {
    return c == '-' || c == '.' || c == ' ' || c == '_';
}

[[nodiscard]] std::string ascii_lower(std::string_view sv) {
    std::string out;
    out.reserve(sv.size());
    for (auto ch : sv) {
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
    return out;
}

} // namespace

std::string slug(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    bool need_dash = false;
    for (auto ch : text) {
        auto const c = static_cast<unsigned char>(ch);
        if (std::isalnum(c) != 0) {
            if (need_dash && !out.empty()) {
                out.push_back('-');
            }
            out.push_back(static_cast<char>(std::tolower(c)));
            need_dash = false;
        } else {
            need_dash = !out.empty();
        }
    }
    return out;
}

std::optional<ParsedTrackPath> parse_track_path(std::filesystem::path const& relative_path) {
    std::vector<std::string> parts;
    parts.reserve(4);
    for (auto const& seg : relative_path) {
        auto const s = seg.string();
        if (s.empty() || s == ".") {
            continue;
        }
        parts.push_back(s);
    }
    if (parts.size() != 3) {
        return std::nullopt;
    }

    auto const& filename = parts[2];
    auto const dot = filename.rfind('.');
    if (dot == std::string::npos || dot == 0 || dot == filename.size() - 1) {
        return std::nullopt;
    }

    ParsedTrackPath out;
    out.artist_name = parts[0];
    out.album_name = parts[1];
    out.extension = ascii_lower(std::string_view{filename}.substr(dot + 1));

    auto stem = filename.substr(0, dot);

    // Try to consume a leading run of digits ("01", "12", "100") followed by
    // a single separator (`-`, `.`, ` `, or `_`). That run becomes the
    // track number; everything after the separator becomes the title.
    std::size_t digit_end = 0;
    while (digit_end < stem.size()
           && std::isdigit(static_cast<unsigned char>(stem[digit_end])) != 0) {
        ++digit_end;
    }
    if (digit_end > 0 && digit_end < stem.size()) {
        std::uint16_t n = 0;
        auto const r = std::from_chars(stem.data(), stem.data() + digit_end, n);
        std::size_t skip = digit_end;
        while (skip < stem.size() && is_separator(stem[skip])) {
            ++skip;
        }
        if (r.ec == std::errc{} && skip > digit_end) {
            out.track_number = n;
            stem = stem.substr(skip);
        }
    }

    // If the stem ended up empty (e.g. the filename was just "01.mp3") fall
    // back to the original stem so the track has *some* title.
    if (stem.empty()) {
        stem = filename.substr(0, dot);
    }
    out.track_title = stem;
    return out;
}

} // namespace sonarium::scanner
