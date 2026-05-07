#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

namespace sonarium::cli {

// What `sonariumctl dlna status` reports for a running server.
struct DlnaStatus {
    std::string friendly_name;
    std::string udn;
    std::string model_name;
    std::string model_number;
    std::uint32_t system_update_id = 0;
    std::uint32_t total_matches = 0; // root-level container count from Browse(0)
    std::uint32_t number_returned = 0;
};

// Pull a single top-level XML element's inner text out of `body`. Tolerates
// namespace prefixes (matches "<friendlyName>" and "<dev:friendlyName>") and
// surrounding whitespace; ignores nested same-named elements. Returns "" if
// the tag is not found.
[[nodiscard]] std::string extract_xml_text(std::string_view body, std::string_view tag);

// Parse the device's /description.xml. Populates friendly_name, udn,
// model_name, model_number; leaves Browse-derived fields zero. Returns an
// error string when the XML obviously doesn't look like a UPnP device
// description (no <UDN> at all).
[[nodiscard]] std::expected<DlnaStatus, std::string> parse_description_xml(std::string_view body);

// Build the SOAP envelope body for ContentDirectory:Browse.
[[nodiscard]] std::string build_browse_request(std::string_view object_id,
                                               std::uint32_t starting_index,
                                               std::uint32_t requested_count);

// Browse response excerpt — what we surface in `dlna status`.
struct BrowseSummary {
    std::uint32_t total_matches = 0;
    std::uint32_t number_returned = 0;
    std::uint32_t update_id = 0;
};

// Parse a SOAP BrowseResponse body and pull TotalMatches / NumberReturned /
// UpdateID. Returns an error if any of the numeric fields can't be parsed.
[[nodiscard]] std::expected<BrowseSummary, std::string>
parse_browse_response(std::string_view body);

} // namespace sonarium::cli
