#include "upnp/ssdp_messages.hpp"

#include <cctype>
#include <charconv>
#include <cstddef>
#include <string>
#include <string_view>

namespace sonarium::upnp::ssdp {

namespace {

constexpr std::string_view kCrlf = "\r\n";

constexpr std::string_view nt_root_device = "upnp:rootdevice";
constexpr std::string_view nt_media_server = "urn:schemas-upnp-org:device:MediaServer:1";
constexpr std::string_view nt_content_directory = "urn:schemas-upnp-org:service:ContentDirectory:1";
constexpr std::string_view nt_connection_manager =
    "urn:schemas-upnp-org:service:ConnectionManager:1";

[[nodiscard]] std::string_view strip(std::string_view s) noexcept {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())) != 0) {
        s.remove_prefix(1);
    }
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())) != 0) {
        s.remove_suffix(1);
    }
    return s;
}

[[nodiscard]] bool ieq(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        auto x = static_cast<unsigned char>(a[i]);
        auto y = static_cast<unsigned char>(b[i]);
        if (std::tolower(x) != std::tolower(y)) {
            return false;
        }
    }
    return true;
}

// Split header value, normalising the quoted form `"ssdp:discover"` for MAN.
[[nodiscard]] std::string_view unquote(std::string_view s) noexcept {
    s = strip(s);
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
        s.remove_prefix(1);
        s.remove_suffix(1);
    }
    return s;
}

// Split a datagram into trimmed lines, accepting LF or CRLF endings. Empty
// trailing lines are dropped.
[[nodiscard]] std::vector<std::string_view> split_lines(std::string_view s) {
    std::vector<std::string_view> out;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= s.size(); ++i) {
        if (i == s.size() || s[i] == '\n') {
            auto line = s.substr(start, i - start);
            if (!line.empty() && line.back() == '\r') {
                line.remove_suffix(1);
            }
            out.push_back(line);
            start = i + 1;
        }
    }
    while (!out.empty() && out.back().empty()) {
        out.pop_back();
    }
    return out;
}

void append_header(std::string& out, std::string_view name, std::string_view value) {
    out.append(name);
    out.append(": ");
    out.append(value);
    out.append(kCrlf);
}

[[nodiscard]] std::string build_advert(std::string_view start_line,
                                       AdvertFields const& fields,
                                       std::string_view nt_header_name,
                                       bool include_cache_control,
                                       bool include_location,
                                       bool include_nts,
                                       std::string_view nts_value) {
    std::string out;
    out.reserve(256);
    out.append(start_line);
    out.append(kCrlf);
    if (include_cache_control) {
        std::string ttl = "max-age=";
        ttl += std::to_string(fields.cache_max_age);
        append_header(out, "CACHE-CONTROL", ttl);
    }
    if (include_location) {
        append_header(out, "LOCATION", fields.location);
    }
    if (include_nts) {
        append_header(out, "NTS", nts_value);
    }
    append_header(out, nt_header_name, fields.nt_or_st);
    append_header(out, "USN", fields.usn);
    append_header(out, "SERVER", fields.server);
    if (start_line == "HTTP/1.1 200 OK") {
        append_header(out, "EXT", "");
    }
    if (start_line == "NOTIFY * HTTP/1.1") {
        append_header(out, "HOST", "239.255.255.250:1900");
    }
    out.append(kCrlf);
    return out;
}

} // namespace

std::expected<MSearch, SsdpParseError> parse_msearch(std::string_view datagram) noexcept {
    if (datagram.empty()) {
        return std::unexpected(SsdpParseError::empty);
    }
    auto const lines = split_lines(datagram);
    if (lines.empty()) {
        return std::unexpected(SsdpParseError::empty);
    }

    auto const start = strip(lines.front());
    if (!start.starts_with("M-SEARCH ")) {
        return std::unexpected(SsdpParseError::not_msearch);
    }

    MSearch out;
    bool seen_man = false;
    for (std::size_t i = 1; i < lines.size(); ++i) {
        auto const line = strip(lines[i]);
        if (line.empty()) {
            continue;
        }
        auto const colon = line.find(':');
        if (colon == std::string_view::npos) {
            return std::unexpected(SsdpParseError::malformed);
        }
        auto const name = strip(line.substr(0, colon));
        auto const value = strip(line.substr(colon + 1));
        if (ieq(name, "ST")) {
            out.st = unquote(value);
        } else if (ieq(name, "MAN")) {
            out.man = unquote(value);
            if (out.man != "ssdp:discover") {
                return std::unexpected(SsdpParseError::bad_man);
            }
            seen_man = true;
        } else if (ieq(name, "MX")) {
            std::uint16_t mx{};
            auto const* first = value.data();
            auto const* last = value.data() + value.size();
            auto const r = std::from_chars(first, last, mx);
            if (r.ec == std::errc{} && r.ptr == last) {
                out.mx = mx;
            }
        } else if (ieq(name, "USER-AGENT")) {
            out.user_agent = std::string(value);
        }
    }

    if (out.st.empty()) {
        return std::unexpected(SsdpParseError::missing_target);
    }
    if (!seen_man) {
        return std::unexpected(SsdpParseError::missing_man);
    }
    return out;
}

std::string build_msearch_response(AdvertFields const& fields) {
    return build_advert("HTTP/1.1 200 OK",
                        fields,
                        "ST",
                        /*cache_control=*/true,
                        /*location=*/true,
                        /*nts=*/false,
                        /*nts_value=*/"");
}

std::string build_notify(NotifyKind kind, AdvertFields const& fields) {
    auto const nts = (kind == NotifyKind::alive) ? "ssdp:alive" : "ssdp:byebye";
    bool const include_location = (kind == NotifyKind::alive);
    bool const include_cache = (kind == NotifyKind::alive);
    return build_advert("NOTIFY * HTTP/1.1",
                        fields,
                        "NT",
                        include_cache,
                        include_location,
                        /*nts=*/true,
                        nts);
}

std::vector<std::string> required_search_targets(std::string_view udn) {
    std::string uuid_target;
    if (udn.starts_with("uuid:")) {
        uuid_target = std::string(udn);
    } else {
        uuid_target = "uuid:" + std::string(udn);
    }
    return {
        std::string(nt_root_device),
        std::move(uuid_target),
        std::string(nt_media_server),
        std::string(nt_content_directory),
        std::string(nt_connection_manager),
    };
}

std::vector<std::pair<std::string, std::string>> adverts_for_search_target(std::string_view udn,
                                                                           std::string_view st) {
    std::string uuid;
    if (udn.starts_with("uuid:")) {
        uuid = std::string(udn);
    } else {
        uuid = "uuid:" + std::string(udn);
    }

    auto make_pair = [&](std::string_view nt) {
        std::string usn = uuid + "::" + std::string(nt);
        if (nt == "uuid:") {
            usn = uuid;
        }
        return std::make_pair(std::string(nt), std::move(usn));
    };

    std::vector<std::pair<std::string, std::string>> out;

    if (st == "ssdp:all") {
        out.emplace_back(std::string(nt_root_device), uuid + "::" + std::string(nt_root_device));
        out.emplace_back(uuid, uuid);
        out.push_back(make_pair(nt_media_server));
        out.push_back(make_pair(nt_content_directory));
        out.push_back(make_pair(nt_connection_manager));
        return out;
    }
    if (st == nt_root_device) {
        out.emplace_back(std::string(nt_root_device), uuid + "::" + std::string(nt_root_device));
        return out;
    }
    if (st == uuid) {
        out.emplace_back(uuid, uuid);
        return out;
    }
    if (st == nt_media_server || st == nt_content_directory || st == nt_connection_manager) {
        out.push_back(make_pair(st));
        return out;
    }
    return out;
}

} // namespace sonarium::upnp::ssdp
