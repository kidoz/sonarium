#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "upnp/upnp_error.hpp"

namespace sonarium::upnp {

// Parsed UPnP SOAP request body.
//
// `service_urn` is the namespace declared on the action element (xmlns:<prefix> or
// the default xmlns on the action element itself). `action` is the local name of
// the action element. `args` preserves child-element order.
struct ParsedSoapRequest {
    std::string service_urn;
    std::string action;
    std::vector<std::pair<std::string, std::string>> args;

    [[nodiscard]] std::string const* arg(std::string_view name) const noexcept {
        for (auto const& kv : args) {
            if (kv.first == name) {
                return &kv.second;
            }
        }
        return nullptr;
    }

    [[nodiscard]] std::string arg_or(std::string_view name, std::string_view fallback) const {
        auto const* p = arg(name);
        return p ? *p : std::string(fallback);
    }
};

enum class SoapParseError : std::uint8_t {
    empty,
    body_not_found,
    action_not_found,
    malformed,
    namespace_missing,
};

// Parse a UPnP SOAP envelope body. Tolerates BOM, XML declarations, comments, mixed-case
// element names, namespace prefixes, attribute reordering, and reasonable whitespace.
// Does NOT support nested arg elements; UPnP control args are flat scalars.
[[nodiscard]] std::expected<ParsedSoapRequest, SoapParseError>
parse_soap_request(std::string_view body);

// Build a SOAP response envelope for `action` in `service_urn`.
// Each (name, value) is emitted as <name>value</name>; values are XML-escaped.
[[nodiscard]] std::string
build_soap_response(std::string_view service_urn,
                    std::string_view action,
                    std::vector<std::pair<std::string, std::string>> const& results);

// Build a SOAP fault response. `code` is the UPnP fault code; description defaults
// to the UPnP-canonical text when empty.
[[nodiscard]] std::string build_soap_fault(UpnpErrorCode code, std::string_view description = "");

} // namespace sonarium::upnp
