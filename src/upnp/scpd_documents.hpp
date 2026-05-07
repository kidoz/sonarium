#pragma once

#include <string_view>

namespace sonarium::upnp {

// Minimal valid SCPD documents for the two services Sonarium DLNA implements.
// They declare actions (Browse / GetProtocolInfo / ...) and the related state variables
// so that strict control points (e.g. Windows Media Player) accept the device.

[[nodiscard]] std::string_view content_directory_scpd_xml() noexcept;
[[nodiscard]] std::string_view connection_manager_scpd_xml() noexcept;

} // namespace sonarium::upnp
