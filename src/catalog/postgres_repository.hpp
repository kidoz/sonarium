#pragma once

#include <asterorm/postgres/connection.hpp>
#include <expected>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

#include "catalog/catalog_writer.hpp"
#include "catalog/repository.hpp"

namespace sonarium::catalog {

// PostgreSQL-backed Repository, built on `asterorm::pg::connection`. The
// constructor takes a single connection for now (no pool); production
// deployments should swap in `asterorm::session<connection_pool<pg::driver>>`
// once concurrent access matters.
//
// Implements both `Repository` (read) and `CatalogWriter` (write). Every
// listing query is paginated (`LIMIT $n OFFSET $m` with a fallback page size
// when `requested_count == 0`) and pairs with a `COUNT(*)` for `total_matches`
// so DLNA `Browse` can report it back to the renderer.
//
// The write side issues `INSERT ... ON CONFLICT (id) DO UPDATE` so the worker
// can run the same scan repeatedly without rewriting unchanged rows; columns
// like `created_at` are preserved while `updated_at = NOW()` advances.
//
// Thread-safety: all calls take an internal mutex around the libpq connection
// (libpq connections are not thread-safe). Replace with the asterorm session +
// pool when concurrency is needed.
//
// Error handling: read methods throw `RepositoryError` when a query fails —
// never an empty page — so a dropped connection surfaces as a SOAP fault /
// HTTP 500 instead of an apparently empty library. When constructed via
// `open()` the repository remembers its conninfo and transparently reconnects
// once per failed call before giving up.
class PostgresRepository final : public Repository, public CatalogWriter {
public:
    explicit PostgresRepository(std::shared_ptr<::asterorm::pg::connection> conn,
                                std::string conninfo = {}) noexcept;

    // Apply the schema DDL (idempotent — `CREATE ... IF NOT EXISTS`). Call
    // once at startup before serving traffic. Returns an error result on the
    // first DDL statement that fails.
    [[nodiscard]] std::expected<void, std::string> ensure_schema();

    // Open a connection from a libpq conninfo string and wrap it. Convenience
    // factory — production wiring would feed a connection_pool here.
    [[nodiscard]] static std::expected<std::shared_ptr<PostgresRepository>, std::string>
    open(std::string const& conninfo);

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
    // Execute with one reconnect-and-retry when the connection has dropped;
    // throws RepositoryError when the query still fails. Callers hold mutex_.
    [[nodiscard]] ::asterorm::pg::result exec(std::string_view sql) const;
    [[nodiscard]] ::asterorm::pg::result
    exec_params(std::string_view sql, std::vector<std::optional<std::string>> const& params) const;
    [[nodiscard]] bool try_reconnect() const;

    [[nodiscard]] std::uint32_t count(std::string_view sql) const;
    [[nodiscard]] std::uint32_t
    count_params(std::string_view sql, std::vector<std::optional<std::string>> const& params) const;
    [[nodiscard]] std::vector<PlaylistItem> load_playlist_items(std::string_view playlist_id) const;

    // Write-side adapter: keeps the CatalogWriter expected<> contract by
    // translating RepositoryError back into an error result.
    [[nodiscard]] std::expected<void, std::string>
    exec_write(std::string_view sql,
               std::vector<std::optional<std::string>> const& params,
               std::string_view label);

    mutable std::shared_ptr<::asterorm::pg::connection> conn_;
    std::string conninfo_;
    mutable std::mutex mutex_;
};

} // namespace sonarium::catalog
