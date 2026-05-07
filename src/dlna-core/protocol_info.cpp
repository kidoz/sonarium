#include "dlna-core/protocol_info.hpp"

#include <string>

#include "media/mime_type.hpp"

namespace sonarium::dlna {

std::string build_protocol_info(sonarium::media::RenditionMime rendition,
                                ProtocolInfoOptions options) {
    auto const mime = sonarium::media::default_mime_for(rendition);

    std::string out;
    out.reserve(64);
    out.append("http-get:*:");
    out.append(mime);
    out.push_back(':');

    bool has_field = false;

    if (options.include_dlna_org_pn) {
        if (auto pn = sonarium::media::dlna_org_pn_for(rendition); pn.has_value()) {
            out.append("DLNA.ORG_PN=");
            out.append(*pn);
            has_field = true;
        }
    }

    if (!options.op.empty()) {
        if (has_field) {
            out.push_back(';');
        }
        out.append("DLNA.ORG_OP=");
        out.append(options.op);
        has_field = true;
    }

    if (!options.ci.empty()) {
        if (has_field) {
            out.push_back(';');
        }
        out.append("DLNA.ORG_CI=");
        out.append(options.ci);
        has_field = true;
    }

    if (!has_field) {
        out.push_back('*');
    }

    return out;
}

std::string join_protocol_info_csv(std::vector<std::string> const& entries) {
    std::string out;
    bool first = true;
    for (auto const& e : entries) {
        if (!first) {
            out.push_back(',');
        }
        out.append(e);
        first = false;
    }
    return out;
}

} // namespace sonarium::dlna
