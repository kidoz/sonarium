#include "composition/http_routes.hpp"

#include <atria/headers.hpp>
#include <atria/request.hpp>
#include <atria/response.hpp>
#include <atria/status.hpp>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "core/path_containment.hpp"
#include "media/mime_type.hpp"

namespace sonarium::composition {

namespace {

[[nodiscard]] std::string user_agent_of(::atria::Request const& req) {
    if (auto const ua = req.header("User-Agent"); ua.has_value()) {
        return std::string{*ua};
    }
    return {};
}

[[nodiscard]] ::atria::Response xml_ok(std::string body) {
    return ::atria::Response::xml(std::move(body), ::atria::Status::Ok);
}

[[nodiscard]] ::atria::Response head_ok(std::string_view content_type) {
    ::atria::Response r{::atria::Status::Ok};
    // No XML body for HEAD; Content-Type still present so renderers know what
    // they would have received.
    r.headers().set("Content-Type", std::string{content_type});
    return r;
}

[[nodiscard]] ::atria::Response not_found() {
    return ::atria::Response{::atria::Status::NotFound};
}

[[nodiscard]] std::string mime_for_rendition(sonarium::media::MediaRendition const& r) {
    if (!r.mime_type.empty()) {
        return r.mime_type;
    }
    return std::string{
        sonarium::media::default_mime_for(sonarium::media::RenditionMime{r.codec, r.container})};
}

// A catalog storage path is servable when it is a regular file that resolves
// inside the configured media root. The containment check is the security
// boundary against poisoned catalog rows and symlinks pointing outside the
// library tree; an empty root (dev mode) skips it.
[[nodiscard]] bool storage_path_is_servable(std::string const& path,
                                            std::filesystem::path const& media_root) {
    if (path.empty()) {
        return false;
    }
    std::error_code ec;
    auto const status = std::filesystem::status(std::filesystem::path{path}, ec);
    if (ec || !std::filesystem::is_regular_file(status)) {
        return false;
    }
    return sonarium::core::path_within_root(std::filesystem::path{path}, media_root);
}

} // namespace

void register_dlna_routes(::atria::Application& app,
                          std::shared_ptr<DlnaServer const> server,
                          std::shared_ptr<sonarium::catalog::Repository const> catalog) {
    // GET /description.xml
    app.get("/description.xml",
            [server](::atria::Request& /*req*/) { return xml_ok(server->description_xml()); })
        .name("getDeviceDescription")
        .summary("UPnP MediaServer device description");

    app.head("/description.xml",
             [](::atria::Request& /*req*/) { return head_ok("application/xml"); });

    // GET /ContentDirectory/scpd.xml
    app.get("/ContentDirectory/scpd.xml",
            [](::atria::Request& /*req*/) {
                return xml_ok(std::string{DlnaServer::content_directory_scpd()});
            })
        .name("getContentDirectoryScpd")
        .summary("ContentDirectory:1 SCPD");

    app.head("/ContentDirectory/scpd.xml",
             [](::atria::Request& /*req*/) { return head_ok("application/xml"); });

    // GET /ConnectionManager/scpd.xml
    app.get("/ConnectionManager/scpd.xml",
            [](::atria::Request& /*req*/) {
                return xml_ok(std::string{DlnaServer::connection_manager_scpd()});
            })
        .name("getConnectionManagerScpd")
        .summary("ConnectionManager:1 SCPD");

    app.head("/ConnectionManager/scpd.xml",
             [](::atria::Request& /*req*/) { return head_ok("application/xml"); });

    // POST /upnp/control/content-directory
    app.post("/upnp/control/content-directory",
             [server](::atria::Request& req) {
                 auto const ua = user_agent_of(req);
                 return xml_ok(server->dispatch_soap(req.body(), ua));
             })
        .name("contentDirectoryControl")
        .summary("ContentDirectory:1 SOAP control endpoint");

    // POST /upnp/control/connection-manager
    app.post("/upnp/control/connection-manager",
             [server](::atria::Request& req) {
                 auto const ua = user_agent_of(req);
                 return xml_ok(server->dispatch_soap(req.body(), ua));
             })
        .name("connectionManagerControl")
        .summary("ConnectionManager:1 SOAP control endpoint");

    // GET / HEAD /media/renditions/{id}
    //
    // atria::Response::file() handles HEAD natively (no body, same headers) and
    // honors Range requests with 206 Partial Content + Content-Range. Both verbs
    // delegate to the same lambda — the method-specific behavior lives in atria.
    //
    // When ServiceConfig.media_token_signer is enabled the route requires a
    // valid `?expires=...&sig=...` query pair; absent/expired/forged tokens
    // produce 403 Forbidden.
    auto media_handler = [catalog, server](::atria::Request& req) -> ::atria::Response {
        auto const id = req.path_param("id").value_or("");
        if (id.empty()) {
            return not_found();
        }

        auto const& signer = server->config().media_token_signer;
        if (signer && signer->enabled()) {
            auto const expires = req.query("expires").value_or("");
            auto const sig = req.query("sig").value_or("");
            if (!signer->verify(id, expires, sig)) {
                return ::atria::Response{::atria::Status::Forbidden};
            }
        }

        auto rendition = catalog->get_rendition(id);
        if (!rendition.has_value()) {
            return not_found();
        }
        if (!storage_path_is_servable(rendition->storage_path, server->config().media_root)) {
            return not_found();
        }
        ::atria::FileResponseOptions opts;
        opts.content_type = mime_for_rendition(*rendition);
        opts.allow_range = true;
        return ::atria::Response::file(
            req, std::filesystem::path{rendition->storage_path}, std::move(opts));
    };

    app.get("/media/renditions/{id}", media_handler)
        .name("getMediaRendition")
        .summary("Direct media file with Range support");

    app.head("/media/renditions/{id}", media_handler);

    // GET / HEAD /art/albums/{id}
    //
    // Resolves the album's cover_art_asset_id through the repository asset
    // lookup, then serves the asset file with its registered MIME.
    auto album_art_handler = [catalog, server](::atria::Request& req) -> ::atria::Response {
        auto const id = req.path_param("id").value_or("");
        if (id.empty()) {
            return not_found();
        }
        auto album = catalog->get_album(id);
        if (!album.has_value() || !album->cover_art_asset_id.has_value()
            || album->cover_art_asset_id->empty()) {
            return not_found();
        }
        auto asset = catalog->get_asset(*album->cover_art_asset_id);
        if (!asset.has_value()
            || !storage_path_is_servable(asset->storage_path, server->config().media_root)) {
            return not_found();
        }
        ::atria::FileResponseOptions opts;
        opts.content_type =
            asset->mime_type.empty() ? std::string{"application/octet-stream"} : asset->mime_type;
        opts.allow_range = true;
        return ::atria::Response::file(
            req, std::filesystem::path{asset->storage_path}, std::move(opts));
    };

    app.get("/art/albums/{id}", album_art_handler).name("getAlbumArt").summary("Album cover art");

    app.head("/art/albums/{id}", album_art_handler);
}

} // namespace sonarium::composition
