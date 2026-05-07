#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "catalog/catalog_writer.hpp"
#include "catalog/repository.hpp"
#include "media/media_rendition.hpp"

namespace sonarium::catalog {

// In-memory Repository for development and tests. Inserts preserve insertion
// order; listings are sorted by id (stable, deterministic).
//
// Implements both the read interface (`Repository`) and the write interface
// (`CatalogWriter`). The legacy `add_*` methods remain so existing tests stay
// untouched; the `upsert_*` overrides delegate to them and always succeed.
class InMemoryRepository : public Repository, public CatalogWriter {
public:
    void add_artist(Artist artist);
    void add_album(Album album);
    void add_track(Track track);
    void add_rendition(sonarium::media::MediaRendition rendition);
    void add_playlist(Playlist playlist);
    void add_asset(StorageAsset asset);
    using Repository::system_update_id;

    // CatalogWriter — always returns success for the in-memory backend.
    [[nodiscard]] std::expected<void, std::string> upsert_artist(Artist const& a) override;
    [[nodiscard]] std::expected<void, std::string> upsert_album(Album const& al) override;
    [[nodiscard]] std::expected<void, std::string> upsert_track(Track const& t) override;
    [[nodiscard]] std::expected<void, std::string>
    upsert_rendition(sonarium::media::MediaRendition const& m) override;
    [[nodiscard]] std::expected<void, std::string> upsert_asset(StorageAsset const& s) override;
    std::expected<void, std::string> bump_system_update_id() override;

    [[nodiscard]] std::uint32_t system_update_id() const override;

    [[nodiscard]] Page<Artist> list_artists(PageRequest req) const override;
    [[nodiscard]] std::optional<Artist> get_artist(std::string_view id) const override;

    [[nodiscard]] Page<Album> list_albums_for_artist(std::string_view artist_id,
                                                     PageRequest req) const override;
    [[nodiscard]] std::optional<Album> get_album(std::string_view id) const override;

    [[nodiscard]] Page<Track> list_tracks_for_album(std::string_view album_id,
                                                    PageRequest req) const override;
    [[nodiscard]] Page<Track> list_all_tracks(PageRequest req) const override;
    [[nodiscard]] std::optional<Track> get_track(std::string_view id) const override;

    [[nodiscard]] std::vector<sonarium::media::MediaRendition>
    list_renditions_for_track(std::string_view track_id) const override;

    [[nodiscard]] std::optional<sonarium::media::MediaRendition>
    get_rendition(std::string_view rendition_id) const override;

    [[nodiscard]] Page<Playlist> list_playlists(PageRequest req) const override;
    [[nodiscard]] std::optional<Playlist> get_playlist(std::string_view id) const override;

    [[nodiscard]] std::optional<StorageAsset> get_asset(std::string_view asset_id) const override;

private:
    std::unordered_map<std::string, Artist> artists_;
    std::unordered_map<std::string, Album> albums_;
    std::unordered_map<std::string, Track> tracks_;
    std::unordered_map<std::string, std::vector<sonarium::media::MediaRendition>> renditions_;
    std::unordered_map<std::string, Playlist> playlists_;
    std::unordered_map<std::string, StorageAsset> assets_;
    std::uint32_t system_update_id_ = 0;
};

} // namespace sonarium::catalog
