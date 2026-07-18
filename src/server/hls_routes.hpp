#pragma once

#include <atria/application.hpp>
#include <filesystem>
#include <memory>
#include <string>

#include "catalog/repository.hpp"
#include "core/media_token.hpp"
#include "hls/segmenter.hpp"

namespace sonarium::server {

// Everything the HLS route set needs beyond its collaborators.
struct HlsRoutesConfig {
    // Base URL of the sonarium-dlna media routes referenced from playlists.
    std::string media_base_url;
    // This server's own base URL, embedded in master playlists and /version.
    std::string self_base_url;
    // Library containment root; storage paths outside it are never read.
    // Empty disables containment (dev mode).
    std::filesystem::path media_root;
};

// Register /version and the /hls/... route set on `app`. Route policy:
//   - with the signer enabled, every route requires a resource-bound
//     `?expires=...&sig=...` token (403 otherwise); playlists rewrite the
//     URLs they emit so clients receive the tokens they need next
//   - segmentation work is bounded; "busy" maps to 503 + Retry-After
//   - storage paths must resolve inside `media_root` when it is set
void register_hls_routes(::atria::Application& app,
                         std::shared_ptr<sonarium::catalog::Repository const> catalog,
                         std::shared_ptr<sonarium::core::MediaTokenSigner const> signer,
                         std::shared_ptr<sonarium::hls::Segmenter> segmenter,
                         HlsRoutesConfig config);

} // namespace sonarium::server
