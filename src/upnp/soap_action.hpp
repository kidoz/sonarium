#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

namespace sonarium::upnp {

// Result of parsing the SOAPACTION HTTP header.
// Header form: "<service-urn>#<ActionName>" (the literal `#` separator is mandatory).
// The header value may or may not be wrapped in double-quotes and may carry surrounding whitespace.
struct SoapAction {
    std::string service_urn; // e.g. "urn:schemas-upnp-org:service:ContentDirectory:1"
    std::string action;      // e.g. "Browse"
};

enum class SoapActionParseError : std::uint8_t {
    empty,
    missing_separator,
    empty_service_urn,
    empty_action,
};

[[nodiscard]] std::expected<SoapAction, SoapActionParseError>
parse_soap_action_header(std::string_view header) noexcept;

} // namespace sonarium::upnp
