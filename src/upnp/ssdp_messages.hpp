#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace sonarium::upnp::ssdp {

// SSDP wire constants.
constexpr std::string_view multicast_group = "239.255.255.250";
constexpr std::uint16_t multicast_port = 1900;

// Parsed M-SEARCH request fields. UPnP requires `MAN` to be `"ssdp:discover"`.
struct MSearch {
    std::string st; // Search target
    std::string man = "ssdp:discover";
    std::uint16_t mx = 1;   // Max wait
    std::string user_agent; // Optional
};

enum class SsdpParseError : std::uint8_t {
    empty,
    not_msearch,
    missing_target,
    missing_man,
    bad_man,
    malformed,
};

// Parse an inbound M-SEARCH datagram. Tolerates any line ending (\r\n or \n) and
// case-insensitive header names. The first line must be `M-SEARCH * HTTP/1.1`.
[[nodiscard]] std::expected<MSearch, SsdpParseError>
parse_msearch(std::string_view datagram) noexcept;

// Fields shared by M-SEARCH responses and NOTIFY messages.
struct AdvertFields {
    std::string location; // http://<lan-ip>:<port>/description.xml
    std::string nt_or_st; // NT for NOTIFY, ST for M-SEARCH response
    std::string usn;      // uuid:<udn>::<service-or-rootdevice>
    std::string server;   // "Sonarium/0.1 UPnP/1.0 sonarium-dlna/0.1"
    std::uint32_t cache_max_age = 1800;
};

[[nodiscard]] std::string build_msearch_response(AdvertFields const& fields);

enum class NotifyKind : std::uint8_t { alive, byebye };

[[nodiscard]] std::string build_notify(NotifyKind kind, AdvertFields const& fields);

// Helpers for the conventional advert set a UPnP MediaServer must publish.
[[nodiscard]] std::vector<std::string> required_search_targets(std::string_view udn);

[[nodiscard]] std::vector<std::pair<std::string, std::string>>
adverts_for_search_target(std::string_view udn, std::string_view st);

} // namespace sonarium::upnp::ssdp
