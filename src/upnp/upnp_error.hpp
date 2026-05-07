#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace sonarium::upnp {

// Standard UPnP fault codes used in SOAP responses.
enum class UpnpErrorCode : std::uint16_t {
    invalid_action = 401,
    invalid_args = 402,
    action_failed = 501,
    argument_invalid = 600,
    argument_out_of_range = 601,
    optional_action_not_implemented = 602,
    no_such_object = 701,
};

struct UpnpError {
    UpnpErrorCode code = UpnpErrorCode::action_failed;
    std::string description;
};

[[nodiscard]] constexpr std::string_view default_description(UpnpErrorCode code) noexcept {
    switch (code) {
        case UpnpErrorCode::invalid_action:
            return "Invalid Action";
        case UpnpErrorCode::invalid_args:
            return "Invalid Args";
        case UpnpErrorCode::action_failed:
            return "Action Failed";
        case UpnpErrorCode::argument_invalid:
            return "Argument Value Invalid";
        case UpnpErrorCode::argument_out_of_range:
            return "Argument Value Out Of Range";
        case UpnpErrorCode::optional_action_not_implemented:
            return "Optional Action Not Implemented";
        case UpnpErrorCode::no_such_object:
            return "No Such Object";
    }
    return "Action Failed";
}

} // namespace sonarium::upnp
