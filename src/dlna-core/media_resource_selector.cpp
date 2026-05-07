#include "dlna-core/media_resource_selector.hpp"

#include <cstdint>
#include <string>

#include "dlna-core/protocol_info.hpp"
#include "media/duration_format.hpp"

namespace sonarium::dlna {

namespace {

[[nodiscard]] bool is_lossless(sonarium::media::AudioCodec codec) noexcept {
    return codec == sonarium::media::AudioCodec::flac || codec == sonarium::media::AudioCodec::alac;
}

[[nodiscard]] std::string build_url(std::string_view base_url, std::string_view rendition_id) {
    std::string out;
    out.reserve(base_url.size() + rendition_id.size() + 32);
    out.append(base_url);
    if (!base_url.empty() && base_url.back() != '/') {
        out.push_back('/');
    }
    out.append("media/renditions/");
    out.append(rendition_id);
    return out;
}

} // namespace

std::vector<DidlResource>
select_resources(std::span<sonarium::media::MediaRendition const> track_renditions,
                 DeviceProfile const& profile,
                 ResourceSelectionContext const& ctx) {
    std::vector<DidlResource> out;
    out.reserve(track_renditions.size());

    // Walk profile's preferred order; for each codec, find the first matching
    // rendition. This enforces both filtering and ordering with a single pass.
    for (auto preferred : profile.preferred_codec_order) {
        if (!profile.exposes_flac && is_lossless(preferred)) {
            continue;
        }
        for (auto const& r : track_renditions) {
            if (r.codec != preferred) {
                continue;
            }
            if (!profile_supports_codec(profile, r.codec)) {
                continue;
            }
            DidlResource res;
            ProtocolInfoOptions opts;
            opts.include_dlna_org_pn = profile.requires_dlna_org_pn;
            res.protocol_info =
                build_protocol_info(sonarium::media::RenditionMime{r.codec, r.container}, opts);
            res.url = build_url(ctx.base_url, r.id);
            if (ctx.token_signer != nullptr && ctx.token_signer->enabled()) {
                res.url.append(ctx.token_signer->sign(r.id));
            }
            if (r.duration_ms > 0) {
                res.duration = sonarium::media::format_didl_duration_ms(
                    static_cast<std::int64_t>(r.duration_ms));
            }
            if (r.bitrate_bps > 0) {
                res.bitrate_bps = r.bitrate_bps;
            }
            if (r.sample_rate_hz > 0) {
                res.sample_rate_hz = r.sample_rate_hz;
            }
            if (r.channels > 0) {
                res.channels = r.channels;
            }
            if (r.size_bytes > 0) {
                res.size_bytes = r.size_bytes;
            }
            out.push_back(std::move(res));
            break; // one rendition per codec slot
        }
    }

    return out;
}

} // namespace sonarium::dlna
