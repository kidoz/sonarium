#include "upnp/soap_action.hpp"

#include <cctype>

namespace sonarium::upnp {

namespace {

[[nodiscard]] std::string_view strip(std::string_view s) noexcept {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())) != 0) {
        s.remove_prefix(1);
    }
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())) != 0) {
        s.remove_suffix(1);
    }
    return s;
}

[[nodiscard]] std::string_view unquote(std::string_view s) noexcept {
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
        s.remove_prefix(1);
        s.remove_suffix(1);
    }
    return s;
}

} // namespace

std::expected<SoapAction, SoapActionParseError>
parse_soap_action_header(std::string_view header) noexcept {
    auto value = unquote(strip(header));
    value = strip(value);

    if (value.empty()) {
        return std::unexpected(SoapActionParseError::empty);
    }

    auto const hash = value.find('#');
    if (hash == std::string_view::npos) {
        return std::unexpected(SoapActionParseError::missing_separator);
    }

    auto const urn = strip(value.substr(0, hash));
    auto const action = strip(value.substr(hash + 1));

    if (urn.empty()) {
        return std::unexpected(SoapActionParseError::empty_service_urn);
    }
    if (action.empty()) {
        return std::unexpected(SoapActionParseError::empty_action);
    }

    return SoapAction{std::string(urn), std::string(action)};
}

} // namespace sonarium::upnp
