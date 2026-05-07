#pragma once

#include <string>
#include <string_view>

namespace sonarium::upnp {

struct DeviceInfo {
    std::string friendly_name;
    std::string manufacturer;
    std::string manufacturer_url;
    std::string model_name;
    std::string model_description;
    std::string model_number;
    std::string model_url;
    std::string serial_number;
    std::string udn; // "uuid:..." — must be stable across restarts
};

struct DeviceServicePaths {
    std::string content_directory_scpd = "/ContentDirectory/scpd.xml";
    std::string content_directory_ctrl = "/upnp/control/content-directory";
    std::string content_directory_event = "/upnp/event/content-directory";

    std::string connection_manager_scpd = "/ConnectionManager/scpd.xml";
    std::string connection_manager_ctrl = "/upnp/control/connection-manager";
    std::string connection_manager_event = "/upnp/event/connection-manager";
};

// Build the GET /description.xml payload. UDN is XML-escaped.
[[nodiscard]] std::string build_device_description(DeviceInfo const& info,
                                                   DeviceServicePaths const& paths = {});

} // namespace sonarium::upnp
