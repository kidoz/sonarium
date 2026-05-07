#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

#include "catalog/repository.hpp"
#include "core/media_token.hpp"
#include "dlna-core/device_profile.hpp"
#include "upnp/soap_envelope.hpp"
#include "upnp/upnp_error.hpp"

namespace sonarium::dlna {

struct BrowseContext {
    sonarium::catalog::Repository const* catalog = nullptr;
    DeviceProfile const* profile = nullptr;
    std::string base_url;
    // Optional: when non-null and `enabled()`, every minted media URL carries
    // a `?expires=...&sig=...` suffix.
    sonarium::core::MediaTokenSigner const* token_signer = nullptr;
};

struct BrowseResult {
    std::string didl_lite;
    std::uint32_t number_returned = 0;
    std::uint32_t total_matches = 0;
    std::uint32_t update_id = 0;
};

// Handle a parsed ContentDirectory:Browse SOAP request and produce the data
// that the SOAP response envelope needs. Errors map directly to UPnP fault
// codes — see browse_error_to_upnp.
std::expected<BrowseResult, sonarium::upnp::UpnpErrorCode>
handle_browse(sonarium::upnp::ParsedSoapRequest const& req, BrowseContext const& ctx);

} // namespace sonarium::dlna
