#pragma once

#include <expected>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "dlna-core/device_profile.hpp"
#include "media/mime_type.hpp"
#include "upnp/soap_envelope.hpp"
#include "upnp/upnp_error.hpp"

namespace sonarium::dlna {

// What ConnectionManager::GetProtocolInfo returns. `Source` is the CSV of
// `protocolInfo` entries the server can supply; `Sink` is empty for a pure
// MediaServer. The full SOAP response carries both.
struct ProtocolInfoResult {
    std::string source;
    std::string sink;
};

// All supported source renditions, used to seed the GetProtocolInfo response.
struct ProtocolInfoCatalog {
    std::vector<sonarium::media::RenditionMime> source_codecs;
};

// Default catalog: MP3 / AAC-LC / WAV-LPCM / FLAC. Profiles filter further at runtime.
[[nodiscard]] ProtocolInfoCatalog default_protocol_info_catalog();

// Build a ProtocolInfo source CSV by intersecting `catalog` with `profile`.
[[nodiscard]] ProtocolInfoResult build_protocol_info_for(DeviceProfile const& profile,
                                                         ProtocolInfoCatalog const& catalog);

// Handle a parsed ConnectionManager:GetProtocolInfo request.
[[nodiscard]] std::expected<std::vector<std::pair<std::string, std::string>>,
                            sonarium::upnp::UpnpErrorCode>
handle_get_protocol_info(sonarium::upnp::ParsedSoapRequest const& req,
                         DeviceProfile const& profile,
                         ProtocolInfoCatalog const& catalog);

} // namespace sonarium::dlna
