#pragma once

#include <span>
#include <string>
#include <vector>

#include "core/media_token.hpp"
#include "dlna-core/device_profile.hpp"
#include "dlna-core/didl_lite_builder.hpp"
#include "media/media_rendition.hpp"

namespace sonarium::dlna {

struct ResourceSelectionContext {
    // URL builder: rendition_id -> playable absolute URL.
    // Plain function-pointer-style; resource selection has no I/O of its own.
    std::string base_url;

    // Optional: when set, each minted URL gets `?expires=...&sig=...` from
    // the signer. nullptr (or a disabled signer) suppresses the suffix.
    sonarium::core::MediaTokenSigner const* token_signer = nullptr;
};

// Pick DidlResource entries for `track_renditions`, ordered per `profile`.
// Renditions whose codec the profile does not list are dropped.
// Renditions whose dlna_profile_name is "FLAC"/"ALAC" are dropped if profile.exposes_flac is false.
[[nodiscard]] std::vector<DidlResource>
select_resources(std::span<sonarium::media::MediaRendition const> track_renditions,
                 DeviceProfile const& profile,
                 ResourceSelectionContext const& ctx);

} // namespace sonarium::dlna
