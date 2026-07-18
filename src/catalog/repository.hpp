#pragma once

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <vector>

#include "catalog/album.hpp"
#include "catalog/artist.hpp"
#include "catalog/playlist.hpp"
#include "catalog/storage_asset.hpp"
#include "catalog/track.hpp"
#include "media/media_rendition.hpp"

namespace sonarium::catalog {

// UPnP Browse pagination request. RequestedCount == 0 means "all remaining",
// which every backend caps at `max_browse_page_size` — clients paginate via
// the TotalMatches the Browse response reports.
struct PageRequest {
    std::uint32_t starting_index = 0;
    std::uint32_t requested_count = 0;
};

// Hard upper bound on rows a single listing call returns, shared by every
// Repository implementation so Browse behaves identically against the
// in-memory and Postgres backends.
inline constexpr std::uint32_t max_browse_page_size = 1024;

template <typename T>
struct Page {
    std::vector<T> rows;
    std::uint32_t total_matches = 0;
};

// Thrown by repository implementations when the backing store fails (query
// error, lost connection). Distinct from "no rows": callers must translate it
// into a UPnP `action_failed` fault or HTTP 500 rather than presenting an
// empty library. The in-memory repository never throws it.
class RepositoryError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Abstract catalog repository. The DLNA Browse handler depends only on this
// interface; the AsterORM-backed implementation lands later. An in-memory
// implementation lives in `in_memory_repository.{hpp,cpp}` for tests and
// development.
class Repository {
public:
    Repository() = default;
    virtual ~Repository() = default;

    [[nodiscard]] virtual std::uint32_t system_update_id() const = 0;

    [[nodiscard]] virtual Page<Artist> list_artists(PageRequest req) const = 0;
    [[nodiscard]] virtual std::optional<Artist> get_artist(std::string_view id) const = 0;

    [[nodiscard]] virtual Page<Album> list_albums_for_artist(std::string_view artist_id,
                                                             PageRequest req) const = 0;
    [[nodiscard]] virtual std::optional<Album> get_album(std::string_view id) const = 0;

    [[nodiscard]] virtual Page<Track> list_tracks_for_album(std::string_view album_id,
                                                            PageRequest req) const = 0;
    [[nodiscard]] virtual Page<Track> list_all_tracks(PageRequest req) const = 0;
    [[nodiscard]] virtual std::optional<Track> get_track(std::string_view id) const = 0;

    [[nodiscard]] virtual std::vector<sonarium::media::MediaRendition>
    list_renditions_for_track(std::string_view track_id) const = 0;

    // Look up a rendition by its globally-unique id. Used by the media file
    // route to resolve {rendition_id} -> storage_path.
    [[nodiscard]] virtual std::optional<sonarium::media::MediaRendition>
    get_rendition(std::string_view rendition_id) const = 0;

    [[nodiscard]] virtual Page<Playlist> list_playlists(PageRequest req) const = 0;
    [[nodiscard]] virtual std::optional<Playlist> get_playlist(std::string_view id) const = 0;

    // Resolve an asset (album art, future thumbnails) by id. Returns nullopt
    // if no asset has been registered.
    [[nodiscard]] virtual std::optional<StorageAsset>
    get_asset(std::string_view asset_id) const = 0;

protected:
    Repository(Repository const&) = default;
    Repository& operator=(Repository const&) = default;
    Repository(Repository&&) noexcept = default;
    Repository& operator=(Repository&&) noexcept = default;
};

} // namespace sonarium::catalog
