#include "dlna-core/connection_manager_handler.hpp"

#include <string>
#include <vector>

#include "dlna-core/protocol_info.hpp"

namespace sonarium::dlna {

ProtocolInfoCatalog default_protocol_info_catalog() {
    using sonarium::media::AudioCodec;
    using sonarium::media::AudioContainer;
    using sonarium::media::RenditionMime;

    ProtocolInfoCatalog cat;
    cat.source_codecs = {
        RenditionMime{AudioCodec::mp3, AudioContainer::mp3},
        RenditionMime{AudioCodec::aac_lc, AudioContainer::mp4},
        RenditionMime{AudioCodec::pcm_wav, AudioContainer::wav},
        RenditionMime{AudioCodec::flac, AudioContainer::flac},
    };
    return cat;
}

ProtocolInfoResult build_protocol_info_for(DeviceProfile const& profile,
                                           ProtocolInfoCatalog const& catalog) {
    using sonarium::media::AudioCodec;

    std::vector<std::string> entries;
    entries.reserve(catalog.source_codecs.size());

    for (auto codec : profile.preferred_codec_order) {
        if (!profile.exposes_flac && (codec == AudioCodec::flac || codec == AudioCodec::alac)) {
            continue;
        }
        for (auto const& rm : catalog.source_codecs) {
            if (rm.codec != codec) {
                continue;
            }
            ProtocolInfoOptions opts;
            opts.include_dlna_org_pn = profile.requires_dlna_org_pn;
            entries.push_back(build_protocol_info(rm, opts));
            break;
        }
    }

    ProtocolInfoResult out;
    out.source = join_protocol_info_csv(entries);
    return out;
}

std::expected<std::vector<std::pair<std::string, std::string>>, sonarium::upnp::UpnpErrorCode>
handle_get_protocol_info(sonarium::upnp::ParsedSoapRequest const& req,
                         DeviceProfile const& profile,
                         ProtocolInfoCatalog const& catalog) {
    if (req.action != "GetProtocolInfo") {
        return std::unexpected(sonarium::upnp::UpnpErrorCode::invalid_action);
    }
    auto const result = build_protocol_info_for(profile, catalog);
    return std::vector<std::pair<std::string, std::string>>{
        {"Source", result.source},
        {"Sink", result.sink},
    };
}

} // namespace sonarium::dlna
