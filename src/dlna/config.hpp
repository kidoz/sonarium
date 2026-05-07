#pragma once

#include <cstdint>
#include <string>

namespace sonarium::dlna {

// Configuration for the sonarium-dlna app. Loaded from sonarium.toml + SONARIUM_* env in
// later phases; for now the struct exists so the app stub can carry default values.
struct DlnaConfig {
    std::string friendly_name = "Sonarium";
    std::string udn;                    // uuid:... — generated on first start
    std::string bind_interface_address; // chosen LAN interface for SSDP LOCATION
    std::uint16_t http_port = 8200;
    bool enabled = true;
};

} // namespace sonarium::dlna
