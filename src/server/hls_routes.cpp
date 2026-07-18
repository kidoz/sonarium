#include "server/hls_routes.hpp"

#include <atria/headers.hpp>
#include <atria/request.hpp>
#include <atria/response.hpp>
#include <atria/status.hpp>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "core/path_containment.hpp"
#include "core/version.hpp"
#include "hls/playlist_builder.hpp"

namespace sonarium::server {

namespace {

[[nodiscard]] ::atria::Response not_found() {
    return ::atria::Response{::atria::Status::NotFound};
}

[[nodiscard]] ::atria::Response forbidden() {
    return ::atria::Response{::atria::Status::Forbidden};
}

[[nodiscard]] ::atria::Response m3u8_ok(std::string body) {
    ::atria::Response r{::atria::Status::Ok};
    r.set_header("Content-Type", "application/vnd.apple.mpegurl");
    r.set_header("Cache-Control", "no-store");
    r.set_body(std::move(body));
    return r;
}

[[nodiscard]] std::optional<std::string> read_file(std::filesystem::path const& path) {
    std::ifstream in{path, std::ios::binary};
    if (!in) {
        return std::nullopt;
    }
    std::ostringstream buf;
    buf << in.rdbuf();
    return buf.str();
}

// Every HLS route enforces the same token policy as the direct media route on
// sonarium-dlna: with signing enabled, a request must carry a valid
// `?expires=...&sig=...` pair bound to the resource id. Without it, any LAN
// client could stream transcoded audio — and worse, trigger unauthenticated
// ffmpeg segmentation jobs.
[[nodiscard]] bool token_ok(sonarium::core::MediaTokenSigner const& signer,
                            ::atria::Request& req,
                            std::string_view resource_id) {
    if (!signer.enabled()) {
        return true;
    }
    auto const expires = req.query("expires").value_or("");
    auto const sig = req.query("sig").value_or("");
    return signer.verify(resource_id, expires, sig);
}

// Append the rendition-scoped token to each segment line of a cached media
// playlist so segment requests pass the same gate. Tag lines start with '#'.
[[nodiscard]] std::string with_signed_segment_urls(std::string const& playlist,
                                                   std::string const& token_suffix) {
    if (token_suffix.empty()) {
        return playlist;
    }
    std::string out;
    out.reserve(playlist.size() + (token_suffix.size() * 16));
    std::size_t pos = 0;
    while (pos < playlist.size()) {
        auto const eol = playlist.find('\n', pos);
        auto const line_end = (eol == std::string::npos) ? playlist.size() : eol;
        auto const line = std::string_view{playlist}.substr(pos, line_end - pos);
        out.append(line);
        if (!line.empty() && line.front() != '#') {
            out.append(token_suffix);
        }
        if (eol != std::string::npos) {
            out.push_back('\n');
        }
        pos = line_end + 1;
    }
    return out;
}

} // namespace

void register_hls_routes(::atria::Application& app,
                         std::shared_ptr<sonarium::catalog::Repository const> catalog,
                         std::shared_ptr<sonarium::core::MediaTokenSigner const> signer,
                         std::shared_ptr<sonarium::hls::Segmenter> segmenter,
                         HlsRoutesConfig config) {
    auto const media_base_url = std::move(config.media_base_url);
    auto const self_base_url = std::move(config.self_base_url);
    auto const media_root = std::move(config.media_root);

    // GET /version — basic liveness/identity probe.
    app.get("/version", [self = self_base_url](::atria::Request&) {
        auto const v = sonarium::core::current_version();
        return ::atria::Response::text(
            std::string{sonarium::core::product_name()} + " server " + std::to_string(v.major) + '.'
            + std::to_string(v.minor) + '.' + std::to_string(v.patch) + " ("
            + std::string{sonarium::core::git_revision()} + ") (HLS)\n" + "self=" + self + "\n");
    });

    // GET /healthz — liveness: the process is up and serving HTTP.
    app.get("/healthz", [](::atria::Request&) { return ::atria::Response::text("ok\n"); });

    // GET /readyz — readiness: the catalog backend answers queries. A dropped
    // Postgres connection turns this into 503 so orchestrators stop routing.
    app.get("/readyz", [catalog](::atria::Request&) -> ::atria::Response {
        try {
            (void)catalog->system_update_id();
            return ::atria::Response::text("ready\n");
        } catch (std::exception const& e) {
            // atria's Status enum has no 503; clients act on the numeric code.
            ::atria::Response r{static_cast<::atria::Status>(503)};
            r.set_body(std::string{"catalog unavailable: "} + e.what() + "\n");
            return r;
        }
    });

    // GET /hls/tracks/{id}/master.m3u8 — multi-variant master playlist.
    app.get("/hls/tracks/{id}/master.m3u8",
            [catalog, signer, self_base_url, media_base_url](
                ::atria::Request& req) -> ::atria::Response {
                auto const id = req.path_param("id").value_or("");
                if (id.empty()) {
                    return not_found();
                }
                // Master playlist tokens are bound to the track id; the variant
                // URLs below carry their own rendition-bound tokens.
                if (!token_ok(*signer, req, id)) {
                    return forbidden();
                }
                auto const track = catalog->get_track(id);
                if (!track.has_value()) {
                    return not_found();
                }
                auto const renditions = catalog->list_renditions_for_track(id);
                if (renditions.empty()) {
                    return not_found();
                }
                std::vector<sonarium::hls::MediaVariant> variants;
                variants.reserve(renditions.size());
                for (auto const& r : renditions) {
                    // signer.sign() returns "" when disabled, so unconditional append is safe.
                    auto media_url =
                        media_base_url + "/media/renditions/" + r.id + signer->sign(r.id);
                    auto variant = sonarium::hls::variant_from_rendition(r, std::move(media_url));
                    variant.index_url_suffix = signer->sign(r.id);
                    variants.push_back(std::move(variant));
                }
                return m3u8_ok(sonarium::hls::build_master_playlist(variants, self_base_url));
            });

    // GET /hls/renditions/{id}/index.m3u8 — segmented VOD playlist. First
    // request to a rendition triggers ffmpeg to write seg00000.ts ... beside
    // index.m3u8 in the cache dir; subsequent requests fast-return the cached
    // playlist. When `rendition.storage_path` is empty / missing (e.g. the
    // demo catalog without seeded files) we fall back to the single-segment
    // playlist that points at the existing /media/renditions/{id} route.
    app.get("/hls/renditions/{id}/index.m3u8",
            [catalog, signer, segmenter, media_base_url, media_root](
                ::atria::Request& req) -> ::atria::Response {
                auto const id = req.path_param("id").value_or("");
                if (id.empty()) {
                    return not_found();
                }
                // Gate before any catalog lookup or ffmpeg spawn — an untokened
                // request must not be able to trigger segmentation work.
                if (!token_ok(*signer, req, id)) {
                    return forbidden();
                }
                auto rendition = catalog->get_rendition(id);
                if (!rendition.has_value()) {
                    return not_found();
                }

                // The segmenter reads the storage path from disk — it must resolve
                // inside the media root, same policy as the direct media route.
                if (!rendition->storage_path.empty()
                    && !sonarium::core::path_within_root(
                        std::filesystem::path{rendition->storage_path}, media_root)) {
                    return not_found();
                }

                // No on-disk source → fall back to single-segment playlist.
                std::error_code ec;
                if (rendition->storage_path.empty()
                    || !std::filesystem::exists(rendition->storage_path, ec)) {
                    auto media_url = media_base_url + "/media/renditions/" + rendition->id
                                     + signer->sign(rendition->id);
                    auto const variant =
                        sonarium::hls::variant_from_rendition(*rendition, std::move(media_url));
                    return m3u8_ok(sonarium::hls::build_media_playlist(variant));
                }

                // Use the rendition's natural bitrate so an ABR set with
                // different rendition.bitrate_bps values produces actually
                // distinct segment ladders. Zero falls back to the cfg default.
                auto const per_rendition_kbps =
                    (rendition->bitrate_bps > 0) ? (rendition->bitrate_bps / 1000U) : 0U;
                auto playlist_path = segmenter->ensure_segments(
                    std::string{id}, rendition->storage_path, per_rendition_kbps);
                if (!playlist_path.has_value()) {
                    if (playlist_path.error().kind == sonarium::hls::SegmenterError::Kind::busy) {
                        // atria's Status enum has no 503; the numeric code is
                        // what clients act on, so cast it in.
                        ::atria::Response r{static_cast<::atria::Status>(503)};
                        r.set_header("Retry-After", "2");
                        r.set_body("segmenter busy: " + playlist_path.error().message + "\n");
                        return r;
                    }
                    ::atria::Response r{::atria::Status::InternalServerError};
                    r.set_body("segmenter: " + playlist_path.error().message + "\n");
                    return r;
                }
                auto body = read_file(*playlist_path);
                if (!body.has_value()) {
                    return not_found();
                }
                return m3u8_ok(with_signed_segment_urls(*body, signer->sign(rendition->id)));
            });

    // GET / HEAD /hls/renditions/{id}/{seg} — serve a cached .ts segment.
    // Filename is validated against `seg\d{5}\.ts`; nothing else is allowed.
    // atria::Response::file() handles HEAD natively (no body, same headers)
    // and honors Range, so both verbs share the same lambda.
    auto segment_handler = [segmenter, signer](::atria::Request& req) -> ::atria::Response {
        auto const id = req.path_param("id").value_or("");
        auto const seg = req.path_param("seg").value_or("");
        if (id.empty() || seg.empty()) {
            return not_found();
        }
        // One rendition-bound token covers every segment of that rendition;
        // the playlist rewrite in the index route hands it to the client.
        if (!token_ok(*signer, req, id)) {
            return forbidden();
        }
        auto path = segmenter->cached_file(std::string{id}, seg);
        if (!path.has_value()) {
            return not_found();
        }
        ::atria::FileResponseOptions opts;
        opts.content_type = "video/mp2t";
        opts.allow_range = true;
        return ::atria::Response::file(req, *path, std::move(opts));
    };
    app.get("/hls/renditions/{id}/{seg}", segment_handler);
    app.head("/hls/renditions/{id}/{seg}", segment_handler);
}

} // namespace sonarium::server
