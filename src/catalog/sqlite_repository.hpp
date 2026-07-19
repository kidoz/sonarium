#pragma once

#include <expected>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

#include "catalog/catalog_writer.hpp"
#include "catalog/repository.hpp"

struct sqlite3; // avoid leaking <sqlite3.h> into every includer

namespace sonarium::catalog {

// SQLite-backed Repository + CatalogWriter: the zero-service catalog for
// single-host deployments — one file next to the music library instead of a
// Postgres instance. Selected via SONARIUM_SQLITE_PATH.
//
// The connection is opened in WAL mode with a busy timeout so the worker can
// write while the DLNA/HLS servers read the same file from other processes.
// All calls serialize on an internal mutex, mirroring PostgresRepository.
//
// Error handling matches the Postgres backend: read methods throw
// `RepositoryError` when a query fails (never a silently empty page); write
// methods return the error through their expected<>.
class SqliteRepository final : public Repository, public CatalogWriter {
public:
    // Open (creating if absent) the database file. Prefer this factory; it
    // configures WAL, foreign keys, and the busy timeout.
    [[nodiscard]] static std::expected<std::shared_ptr<SqliteRepository>, std::string>
    open(std::filesystem::path const& db_path);

    ~SqliteRepository() override;
    SqliteRepository(SqliteRepository const&) = delete;
    SqliteRepository& operator=(SqliteRepository const&) = delete;
    SqliteRepository(SqliteRepository&&) = delete;
    SqliteRepository& operator=(SqliteRepository&&) = delete;

    // Apply baseline DDL + pending migrations; records the schema version.
    // Same contract as PostgresRepository::ensure_schema.
    [[nodiscard]] std::expected<void, std::string> ensure_schema();

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

    // CatalogWriter — INSERT ... ON CONFLICT (id) DO UPDATE upserts.
    [[nodiscard]] std::expected<void, std::string> upsert_artist(Artist const& a) override;
    [[nodiscard]] std::expected<void, std::string> upsert_album(Album const& al) override;
    [[nodiscard]] std::expected<void, std::string> upsert_track(Track const& t) override;
    [[nodiscard]] std::expected<void, std::string>
    upsert_rendition(sonarium::media::MediaRendition const& m) override;
    [[nodiscard]] std::expected<void, std::string> upsert_asset(StorageAsset const& s) override;
    std::expected<void, std::string> bump_system_update_id() override;

private:
    explicit SqliteRepository(sqlite3* db) noexcept;

    [[nodiscard]] std::vector<PlaylistItem> load_playlist_items(std::string_view playlist_id) const;

    sqlite3* db_ = nullptr;
    mutable std::mutex mutex_;
};

} // namespace sonarium::catalog
