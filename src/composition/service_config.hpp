#pragma once

#include <memory>
#include <string>

#include "core/media_token.hpp"
#include "dlna-core/connection_manager_handler.hpp"
#include "upnp/device_description.hpp"

namespace sonarium::composition {

// Configuration consumed by every DLNA service. Construct once at startup,
// share read-only across the composition graph as `shared_ptr<const ServiceConfig>`.
struct ServiceConfig {
    sonarium::upnp::DeviceInfo device;
    sonarium::upnp::DeviceServicePaths service_paths;
    std::string base_url; // e.g. "http://192.168.1.10:8200" — used for media URLs
    sonarium::dlna::ProtocolInfoCatalog protocol_info_catalog;

    // Optional: when present and `enabled()`, every minted media URL carries
    // a short-lived `?expires=...&sig=...` and the route enforces it.
    // Construct with an empty secret to disable signing without forking the
    // route logic.
    std::shared_ptr<sonarium::core::MediaTokenSigner> media_token_signer;
};

} // namespace sonarium::composition
