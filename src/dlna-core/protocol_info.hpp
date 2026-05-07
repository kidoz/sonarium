#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "media/mime_type.hpp"

namespace sonarium::dlna {

// DLNA.ORG_OP — operation parameter:
//   bit 0: time-seek-range supported
//   bit 1: byte-seek-range supported
// "01" means byte-seek (Range) only; that is what the direct-file media route supports.
constexpr std::string_view dlna_op_byte_seek = "01";

// DLNA.ORG_CI — content-injection (transcoded). 0 = original.
constexpr std::string_view dlna_ci_original = "0";

struct ProtocolInfoOptions {
    bool include_dlna_org_pn = true;
    std::string_view op = dlna_op_byte_seek;
    std::string_view ci = dlna_ci_original;
};

// Build a single `protocolInfo` entry for an audio rendition, e.g.:
//   "http-get:*:audio/mpeg:DLNA.ORG_PN=MP3;DLNA.ORG_OP=01;DLNA.ORG_CI=0"
[[nodiscard]] std::string build_protocol_info(sonarium::media::RenditionMime rendition,
                                              ProtocolInfoOptions options = {});

// Comma-separated source list used by ConnectionManager::GetProtocolInfo.
[[nodiscard]] std::string join_protocol_info_csv(std::vector<std::string> const& entries);

} // namespace sonarium::dlna
