#include "dlna-core/browse_handler.hpp"

#include <charconv>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "catalog/repository.hpp"
#include "dlna-core/didl_lite_builder.hpp"
#include "dlna-core/dlna_object_id.hpp"
#include "dlna-core/media_resource_selector.hpp"
#include "media/duration_format.hpp"

namespace sonarium::dlna {

namespace {

constexpr std::string_view upnp_class_root_container = "object.container";
constexpr std::string_view upnp_class_storage_folder = "object.container.storageFolder";
constexpr std::string_view upnp_class_music_artist = "object.container.person.musicArtist";
constexpr std::string_view upnp_class_music_album = "object.container.album.musicAlbum";
constexpr std::string_view upnp_class_playlist = "object.container.playlistContainer";
constexpr std::string_view upnp_class_music_track = "object.item.audioItem.musicTrack";

[[nodiscard]] std::uint32_t parse_u32(std::string_view s, std::uint32_t fallback) noexcept {
    std::uint32_t value{};
    auto const* first = s.data();
    auto const* last = s.data() + s.size();
    auto const result = std::from_chars(first, last, value);
    if (result.ec != std::errc{} || result.ptr != last) {
        return fallback;
    }
    return value;
}

[[nodiscard]] DidlContainer
make_container(std::string id,
               std::string parent_id,
               std::string title,
               std::string upnp_class,
               std::optional<std::uint32_t> child_count = std::nullopt) {
    DidlContainer c;
    c.id = std::move(id);
    c.parent_id = std::move(parent_id);
    c.title = std::move(title);
    c.upnp_class = std::move(upnp_class);
    c.child_count = child_count;
    return c;
}

[[nodiscard]] DidlItem build_track_item(sonarium::catalog::Track const& track,
                                        std::string_view parent_id,
                                        BrowseContext const& ctx) {
    DidlItem item;
    item.id = make_track_id(track.id);
    item.parent_id = std::string(parent_id);
    item.title = track.title;
    item.upnp_class.assign(upnp_class_music_track);
    if (track.track_number.has_value()) {
        item.original_track_number = *track.track_number;
    }
    if (!track.album_id.empty()) {
        if (auto album = ctx.catalog->get_album(track.album_id);
            album.has_value() && album->cover_art_asset_id.has_value()
            && !album->cover_art_asset_id->empty()) {
            // The matching /art/albums/{album_id} route is registered in
            // composition::register_dlna_routes; the URL shape mirrors it.
            std::string uri = ctx.base_url;
            if (!uri.empty() && uri.back() != '/') {
                uri.push_back('/');
            }
            uri.append("art/albums/");
            uri.append(track.album_id);
            item.album_art_uri = std::move(uri);
        }
    }
    auto const renditions = ctx.catalog->list_renditions_for_track(track.id);
    ResourceSelectionContext sel_ctx;
    sel_ctx.base_url = ctx.base_url;
    sel_ctx.token_signer = ctx.token_signer;
    auto resources = select_resources(std::span(renditions), *ctx.profile, sel_ctx);
    // If the rendition lacked its own duration_ms, fall back to the track-level duration.
    for (auto& r : resources) {
        if (!r.duration.has_value() && track.duration_ms > 0) {
            r.duration = sonarium::media::format_didl_duration_ms(
                static_cast<std::int64_t>(track.duration_ms));
        }
    }
    item.resources = std::move(resources);
    return item;
}

[[nodiscard]] std::uint32_t album_track_count(sonarium::catalog::Repository const& repo,
                                              std::string_view album_id) {
    return repo.list_tracks_for_album(album_id, sonarium::catalog::PageRequest{0, 0}).total_matches;
}

[[nodiscard]] std::uint32_t artist_album_count(sonarium::catalog::Repository const& repo,
                                               std::string_view artist_id) {
    return repo.list_albums_for_artist(artist_id, sonarium::catalog::PageRequest{0, 0})
        .total_matches;
}

void apply_pagination(BrowseResult& result, std::uint32_t total_matches) {
    result.total_matches = total_matches;
    if (result.number_returned == 0) {
        // No counters set yet — caller should populate.
    }
}

[[nodiscard]] BrowseResult browse_metadata(ObjectId const& oid, BrowseContext const& ctx) {
    BrowseResult result;
    result.update_id = ctx.catalog->system_update_id();

    std::vector<DidlContainer> containers;
    std::vector<DidlItem> items;

    switch (oid.kind) {
        case ObjectIdKind::root: {
            containers.push_back(make_container(
                make_root_id(), "-1", "root", std::string(upnp_class_root_container), 1));
            break;
        }
        case ObjectIdKind::music: {
            containers.push_back(make_container(
                make_music_id(), make_root_id(), "Music", std::string(upnp_class_storage_folder)));
            break;
        }
        case ObjectIdKind::artists: {
            containers.push_back(make_container(make_artists_id(),
                                                make_music_id(),
                                                "Artists",
                                                std::string(upnp_class_storage_folder),
                                                ctx.catalog->list_artists({0, 0}).total_matches));
            break;
        }
        case ObjectIdKind::artist: {
            auto a = ctx.catalog->get_artist(oid.entity_id);
            if (!a.has_value()) {
                break;
            }
            containers.push_back(make_container(make_artist_id(oid.entity_id),
                                                make_artists_id(),
                                                a->name,
                                                std::string(upnp_class_music_artist),
                                                artist_album_count(*ctx.catalog, oid.entity_id)));
            break;
        }
        case ObjectIdKind::artist_albums: {
            auto a = ctx.catalog->get_artist(oid.entity_id);
            if (!a.has_value()) {
                break;
            }
            containers.push_back(make_container(make_artist_albums_id(oid.entity_id),
                                                make_artist_id(oid.entity_id),
                                                a->name + " — Albums",
                                                std::string(upnp_class_storage_folder),
                                                artist_album_count(*ctx.catalog, oid.entity_id)));
            break;
        }
        case ObjectIdKind::album: {
            auto al = ctx.catalog->get_album(oid.entity_id);
            if (!al.has_value()) {
                break;
            }
            std::string parent = make_root_id();
            if (!al->artist_id.empty()) {
                parent = make_artist_albums_id(al->artist_id);
            }
            containers.push_back(make_container(make_album_id(oid.entity_id),
                                                parent,
                                                al->title,
                                                std::string(upnp_class_music_album),
                                                album_track_count(*ctx.catalog, oid.entity_id)));
            break;
        }
        case ObjectIdKind::tracks: {
            containers.push_back(
                make_container(make_tracks_id(),
                               make_music_id(),
                               "All Tracks",
                               std::string(upnp_class_storage_folder),
                               ctx.catalog->list_all_tracks({0, 0}).total_matches));
            break;
        }
        case ObjectIdKind::playlists: {
            containers.push_back(make_container(make_playlists_id(),
                                                make_music_id(),
                                                "Playlists",
                                                std::string(upnp_class_storage_folder),
                                                ctx.catalog->list_playlists({0, 0}).total_matches));
            break;
        }
        case ObjectIdKind::playlist: {
            auto pl = ctx.catalog->get_playlist(oid.entity_id);
            if (!pl.has_value()) {
                break;
            }
            containers.push_back(make_container(make_playlist_id(oid.entity_id),
                                                make_playlists_id(),
                                                pl->title,
                                                std::string(upnp_class_playlist),
                                                static_cast<std::uint32_t>(pl->items.size())));
            break;
        }
        case ObjectIdKind::track: {
            auto t = ctx.catalog->get_track(oid.entity_id);
            if (!t.has_value()) {
                break;
            }
            std::string parent =
                t->album_id.empty() ? make_tracks_id() : make_album_id(t->album_id);
            items.push_back(build_track_item(*t, parent, ctx));
            break;
        }
        case ObjectIdKind::unknown:
            break;
    }

    result.didl_lite = build_didl_lite(containers, items);
    result.number_returned = static_cast<std::uint32_t>(containers.size() + items.size());
    apply_pagination(result, result.number_returned);
    return result;
}

[[nodiscard]] BrowseResult browse_root_children(BrowseContext const& ctx,
                                                sonarium::catalog::PageRequest /*req*/) {
    BrowseResult result;
    result.update_id = ctx.catalog->system_update_id();
    std::vector<DidlContainer> containers;
    containers.push_back(make_container(
        make_music_id(), make_root_id(), "Music", std::string(upnp_class_storage_folder)));
    containers.push_back(make_container(
        make_playlists_id(), make_root_id(), "Playlists", std::string(upnp_class_storage_folder)));
    result.didl_lite = build_didl_lite(containers, {});
    result.number_returned = static_cast<std::uint32_t>(containers.size());
    result.total_matches = result.number_returned;
    return result;
}

[[nodiscard]] BrowseResult browse_music_children(BrowseContext const& ctx,
                                                 sonarium::catalog::PageRequest /*req*/) {
    BrowseResult result;
    result.update_id = ctx.catalog->system_update_id();
    std::vector<DidlContainer> containers;
    containers.push_back(make_container(make_artists_id(),
                                        make_music_id(),
                                        "Artists",
                                        std::string(upnp_class_storage_folder),
                                        ctx.catalog->list_artists({0, 0}).total_matches));
    containers.push_back(make_container(make_tracks_id(),
                                        make_music_id(),
                                        "All Tracks",
                                        std::string(upnp_class_storage_folder),
                                        ctx.catalog->list_all_tracks({0, 0}).total_matches));
    result.didl_lite = build_didl_lite(containers, {});
    result.number_returned = static_cast<std::uint32_t>(containers.size());
    result.total_matches = result.number_returned;
    return result;
}

[[nodiscard]] BrowseResult browse_artists_children(BrowseContext const& ctx,
                                                   sonarium::catalog::PageRequest req) {
    BrowseResult result;
    result.update_id = ctx.catalog->system_update_id();
    auto page = ctx.catalog->list_artists(req);
    std::vector<DidlContainer> containers;
    containers.reserve(page.rows.size());
    for (auto const& a : page.rows) {
        containers.push_back(make_container(make_artist_id(a.id),
                                            make_artists_id(),
                                            a.name,
                                            std::string(upnp_class_music_artist),
                                            artist_album_count(*ctx.catalog, a.id)));
    }
    result.didl_lite = build_didl_lite(containers, {});
    result.number_returned = static_cast<std::uint32_t>(page.rows.size());
    result.total_matches = page.total_matches;
    return result;
}

[[nodiscard]] BrowseResult browse_artist_children(ObjectId const& oid,
                                                  BrowseContext const& ctx,
                                                  sonarium::catalog::PageRequest /*req*/) {
    BrowseResult result;
    result.update_id = ctx.catalog->system_update_id();
    std::vector<DidlContainer> containers;
    containers.push_back(make_container(make_artist_albums_id(oid.entity_id),
                                        make_artist_id(oid.entity_id),
                                        "Albums",
                                        std::string(upnp_class_storage_folder),
                                        artist_album_count(*ctx.catalog, oid.entity_id)));
    result.didl_lite = build_didl_lite(containers, {});
    result.number_returned = static_cast<std::uint32_t>(containers.size());
    result.total_matches = result.number_returned;
    return result;
}

[[nodiscard]] BrowseResult browse_artist_albums_children(ObjectId const& oid,
                                                         BrowseContext const& ctx,
                                                         sonarium::catalog::PageRequest req) {
    BrowseResult result;
    result.update_id = ctx.catalog->system_update_id();
    auto page = ctx.catalog->list_albums_for_artist(oid.entity_id, req);
    std::vector<DidlContainer> containers;
    containers.reserve(page.rows.size());
    auto const parent = make_artist_albums_id(oid.entity_id);
    for (auto const& al : page.rows) {
        containers.push_back(make_container(make_album_id(al.id),
                                            parent,
                                            al.title,
                                            std::string(upnp_class_music_album),
                                            album_track_count(*ctx.catalog, al.id)));
    }
    result.didl_lite = build_didl_lite(containers, {});
    result.number_returned = static_cast<std::uint32_t>(page.rows.size());
    result.total_matches = page.total_matches;
    return result;
}

[[nodiscard]] BrowseResult browse_album_children(ObjectId const& oid,
                                                 BrowseContext const& ctx,
                                                 sonarium::catalog::PageRequest req) {
    BrowseResult result;
    result.update_id = ctx.catalog->system_update_id();
    auto page = ctx.catalog->list_tracks_for_album(oid.entity_id, req);
    std::vector<DidlItem> items;
    items.reserve(page.rows.size());
    auto const parent = make_album_id(oid.entity_id);
    for (auto const& t : page.rows) {
        items.push_back(build_track_item(t, parent, ctx));
    }
    result.didl_lite = build_didl_lite({}, items);
    result.number_returned = static_cast<std::uint32_t>(page.rows.size());
    result.total_matches = page.total_matches;
    return result;
}

[[nodiscard]] BrowseResult browse_all_tracks_children(BrowseContext const& ctx,
                                                      sonarium::catalog::PageRequest req) {
    BrowseResult result;
    result.update_id = ctx.catalog->system_update_id();
    auto page = ctx.catalog->list_all_tracks(req);
    std::vector<DidlItem> items;
    items.reserve(page.rows.size());
    for (auto const& t : page.rows) {
        std::string parent = t.album_id.empty() ? make_tracks_id() : make_album_id(t.album_id);
        items.push_back(build_track_item(t, parent, ctx));
    }
    result.didl_lite = build_didl_lite({}, items);
    result.number_returned = static_cast<std::uint32_t>(page.rows.size());
    result.total_matches = page.total_matches;
    return result;
}

[[nodiscard]] BrowseResult browse_playlists_children(BrowseContext const& ctx,
                                                     sonarium::catalog::PageRequest req) {
    BrowseResult result;
    result.update_id = ctx.catalog->system_update_id();
    auto page = ctx.catalog->list_playlists(req);
    std::vector<DidlContainer> containers;
    containers.reserve(page.rows.size());
    for (auto const& pl : page.rows) {
        containers.push_back(make_container(make_playlist_id(pl.id),
                                            make_playlists_id(),
                                            pl.title,
                                            std::string(upnp_class_playlist),
                                            static_cast<std::uint32_t>(pl.items.size())));
    }
    result.didl_lite = build_didl_lite(containers, {});
    result.number_returned = static_cast<std::uint32_t>(page.rows.size());
    result.total_matches = page.total_matches;
    return result;
}

[[nodiscard]] std::expected<BrowseResult, sonarium::upnp::UpnpErrorCode> browse_direct_children(
    ObjectId const& oid, BrowseContext const& ctx, sonarium::catalog::PageRequest req) {
    using sonarium::upnp::UpnpErrorCode;
    switch (oid.kind) {
        case ObjectIdKind::root:
            return browse_root_children(ctx, req);
        case ObjectIdKind::music:
            return browse_music_children(ctx, req);
        case ObjectIdKind::artists:
            return browse_artists_children(ctx, req);
        case ObjectIdKind::artist:
            if (!ctx.catalog->get_artist(oid.entity_id).has_value()) {
                return std::unexpected(UpnpErrorCode::no_such_object);
            }
            return browse_artist_children(oid, ctx, req);
        case ObjectIdKind::artist_albums:
            if (!ctx.catalog->get_artist(oid.entity_id).has_value()) {
                return std::unexpected(UpnpErrorCode::no_such_object);
            }
            return browse_artist_albums_children(oid, ctx, req);
        case ObjectIdKind::album:
            if (!ctx.catalog->get_album(oid.entity_id).has_value()) {
                return std::unexpected(UpnpErrorCode::no_such_object);
            }
            return browse_album_children(oid, ctx, req);
        case ObjectIdKind::tracks:
            return browse_all_tracks_children(ctx, req);
        case ObjectIdKind::playlists:
            return browse_playlists_children(ctx, req);
        case ObjectIdKind::playlist:
            if (!ctx.catalog->get_playlist(oid.entity_id).has_value()) {
                return std::unexpected(UpnpErrorCode::no_such_object);
            }
            // Treating playlist as a leaf without expanded child track list for the MVP.
            return browse_artist_children(oid, ctx, req);
        case ObjectIdKind::track:
            // Tracks have no direct children — return empty result.
            return BrowseResult{};
        case ObjectIdKind::unknown:
            return std::unexpected(UpnpErrorCode::no_such_object);
    }
    return std::unexpected(UpnpErrorCode::action_failed);
}

} // namespace

std::expected<BrowseResult, sonarium::upnp::UpnpErrorCode>
handle_browse(sonarium::upnp::ParsedSoapRequest const& req, BrowseContext const& ctx) {
    using sonarium::upnp::UpnpErrorCode;

    if (ctx.catalog == nullptr || ctx.profile == nullptr) {
        return std::unexpected(UpnpErrorCode::action_failed);
    }
    if (req.action != "Browse") {
        return std::unexpected(UpnpErrorCode::invalid_action);
    }

    auto const* object_id_str = req.arg("ObjectID");
    if (object_id_str == nullptr) {
        return std::unexpected(UpnpErrorCode::invalid_args);
    }

    auto parsed = parse_object_id(*object_id_str);
    if (!parsed) {
        return std::unexpected(UpnpErrorCode::no_such_object);
    }

    auto const flag = req.arg_or("BrowseFlag", "BrowseDirectChildren");
    auto const starting_index = parse_u32(req.arg_or("StartingIndex", "0"), 0);
    auto const requested = parse_u32(req.arg_or("RequestedCount", "0"), 0);
    sonarium::catalog::PageRequest page_req{starting_index, requested};

    if (flag == "BrowseMetadata") {
        return browse_metadata(*parsed, ctx);
    }
    if (flag == "BrowseDirectChildren") {
        return browse_direct_children(*parsed, ctx, page_req);
    }
    return std::unexpected(UpnpErrorCode::argument_invalid);
}

} // namespace sonarium::dlna
