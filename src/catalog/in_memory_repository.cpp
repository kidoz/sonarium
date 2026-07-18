#include "catalog/in_memory_repository.hpp"

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace sonarium::catalog {

namespace {

template <typename Map>
[[nodiscard]] auto find_value(Map const& m, std::string_view id)
    -> std::optional<typename Map::mapped_type> {
    auto const it = m.find(std::string(id));
    if (it == m.end()) {
        return std::nullopt;
    }
    return it->second;
}

template <typename T, typename Pred>
[[nodiscard]] Page<T>
paginate(std::unordered_map<std::string, T> const& source, PageRequest req, Pred predicate) {
    std::vector<T> filtered;
    filtered.reserve(source.size());
    for (auto const& [_, value] : source) {
        if (predicate(value)) {
            filtered.push_back(value);
        }
    }
    std::sort(filtered.begin(), filtered.end(), [](T const& a, T const& b) { return a.id < b.id; });

    Page<T> out;
    out.total_matches = static_cast<std::uint32_t>(filtered.size());
    if (req.starting_index >= filtered.size()) {
        return out;
    }
    auto const remaining = static_cast<std::uint32_t>(filtered.size()) - req.starting_index;
    auto const requested = (req.requested_count == 0)
                               ? max_browse_page_size
                               : std::min(req.requested_count, max_browse_page_size);
    auto const take = std::min(requested, remaining);
    out.rows.reserve(take);
    for (std::uint32_t i = 0; i < take; ++i) {
        out.rows.push_back(std::move(filtered[req.starting_index + i]));
    }
    return out;
}

} // namespace

void InMemoryRepository::add_artist(Artist artist) {
    auto const id = artist.id;
    artists_[id] = std::move(artist);
}

void InMemoryRepository::add_album(Album album) {
    auto const id = album.id;
    albums_[id] = std::move(album);
}

void InMemoryRepository::add_track(Track track) {
    auto const id = track.id;
    tracks_[id] = std::move(track);
}

void InMemoryRepository::add_rendition(sonarium::media::MediaRendition rendition) {
    renditions_[rendition.track_id].push_back(std::move(rendition));
}

void InMemoryRepository::add_playlist(Playlist playlist) {
    auto const id = playlist.id;
    playlists_[id] = std::move(playlist);
}

// CatalogWriter overrides — wrappers around the legacy add_* methods so the
// existing tests (and the dlna sample catalog) keep using `add_*` while
// ingestion code targets the abstract interface.

std::expected<void, std::string> InMemoryRepository::upsert_artist(Artist const& a) {
    add_artist(a);
    return {};
}

std::expected<void, std::string> InMemoryRepository::upsert_album(Album const& al) {
    add_album(al);
    return {};
}

std::expected<void, std::string> InMemoryRepository::upsert_track(Track const& t) {
    add_track(t);
    return {};
}

std::expected<void, std::string>
InMemoryRepository::upsert_rendition(sonarium::media::MediaRendition const& m) {
    add_rendition(m);
    return {};
}

std::expected<void, std::string> InMemoryRepository::upsert_asset(StorageAsset const& s) {
    add_asset(s);
    return {};
}

std::expected<void, std::string> InMemoryRepository::bump_system_update_id() {
    ++system_update_id_;
    return {};
}

std::uint32_t InMemoryRepository::system_update_id() const {
    return system_update_id_;
}

Page<Artist> InMemoryRepository::list_artists(PageRequest req) const {
    return paginate<Artist>(artists_, req, [](Artist const&) { return true; });
}

std::optional<Artist> InMemoryRepository::get_artist(std::string_view id) const {
    return find_value(artists_, id);
}

Page<Album> InMemoryRepository::list_albums_for_artist(std::string_view artist_id,
                                                       PageRequest req) const {
    return paginate<Album>(
        albums_, req, [artist_id](Album const& a) { return a.artist_id == artist_id; });
}

std::optional<Album> InMemoryRepository::get_album(std::string_view id) const {
    return find_value(albums_, id);
}

Page<Track> InMemoryRepository::list_tracks_for_album(std::string_view album_id,
                                                      PageRequest req) const {
    return paginate<Track>(
        tracks_, req, [album_id](Track const& t) { return t.album_id == album_id; });
}

Page<Track> InMemoryRepository::list_all_tracks(PageRequest req) const {
    return paginate<Track>(tracks_, req, [](Track const&) { return true; });
}

std::optional<Track> InMemoryRepository::get_track(std::string_view id) const {
    return find_value(tracks_, id);
}

std::vector<sonarium::media::MediaRendition>
InMemoryRepository::list_renditions_for_track(std::string_view track_id) const {
    auto const it = renditions_.find(std::string(track_id));
    if (it == renditions_.end()) {
        return {};
    }
    return it->second;
}

std::optional<sonarium::media::MediaRendition>
InMemoryRepository::get_rendition(std::string_view rendition_id) const {
    for (auto const& [_, list] : renditions_) {
        for (auto const& r : list) {
            if (r.id == rendition_id) {
                return r;
            }
        }
    }
    return std::nullopt;
}

Page<Playlist> InMemoryRepository::list_playlists(PageRequest req) const {
    return paginate<Playlist>(playlists_, req, [](Playlist const&) { return true; });
}

std::optional<Playlist> InMemoryRepository::get_playlist(std::string_view id) const {
    return find_value(playlists_, id);
}

void InMemoryRepository::add_asset(StorageAsset asset) {
    auto const id = asset.id;
    assets_[id] = std::move(asset);
}

std::optional<StorageAsset> InMemoryRepository::get_asset(std::string_view asset_id) const {
    return find_value(assets_, asset_id);
}

} // namespace sonarium::catalog
