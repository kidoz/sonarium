#include "catalog/sqlite_repository.hpp"

#include <sqlite3.h>

#include <cstdint>
#include <string>
#include <utility>

#include "catalog/sqlite_schema.hpp"

namespace sonarium::catalog {

namespace {

[[nodiscard]] std::uint32_t requested_or_default(std::uint32_t requested) noexcept {
    return (requested == 0) ? max_browse_page_size : std::min(requested, max_browse_page_size);
}

// RAII prepared statement. Throws RepositoryError on prepare/bind/step
// failures so read paths surface backend errors instead of empty results.
class Stmt {
public:
    Stmt(sqlite3* db, std::string_view sql) : db_{db} {
        if (sqlite3_prepare_v2(db, sql.data(), static_cast<int>(sql.size()), &raw_, nullptr)
            != SQLITE_OK) {
            throw RepositoryError{std::string{"sqlite: prepare failed: "} + sqlite3_errmsg(db)};
        }
    }

    ~Stmt() { sqlite3_finalize(raw_); }
    Stmt(Stmt const&) = delete;
    Stmt& operator=(Stmt const&) = delete;
    Stmt(Stmt&&) = delete;
    Stmt& operator=(Stmt&&) = delete;

    void bind_text(int index, std::string_view value) {
        check(sqlite3_bind_text(
            raw_, index, value.data(), static_cast<int>(value.size()), SQLITE_TRANSIENT));
    }

    void bind_int64(int index, std::int64_t value) {
        check(sqlite3_bind_int64(raw_, index, value));
    }

    void bind_optional_int64(int index, std::optional<std::int64_t> const& value) {
        if (value.has_value()) {
            bind_int64(index, *value);
        } else {
            check(sqlite3_bind_null(raw_, index));
        }
    }

    void bind_optional_text(int index, std::optional<std::string> const& value) {
        if (value.has_value()) {
            bind_text(index, *value);
        } else {
            check(sqlite3_bind_null(raw_, index));
        }
    }

    // True while a row is available.
    [[nodiscard]] bool step() {
        auto const rc = sqlite3_step(raw_);
        if (rc == SQLITE_ROW) {
            return true;
        }
        if (rc == SQLITE_DONE) {
            return false;
        }
        throw RepositoryError{std::string{"sqlite: step failed: "} + sqlite3_errmsg(db_)};
    }

    [[nodiscard]] std::string column_text(int col) const {
        auto const* text = sqlite3_column_text(raw_, col);
        if (text == nullptr) {
            return {};
        }
        return std::string{reinterpret_cast<char const*>(text)};
    }

    [[nodiscard]] std::int64_t column_int64(int col) const {
        return sqlite3_column_int64(raw_, col);
    }

    [[nodiscard]] bool column_is_null(int col) const {
        return sqlite3_column_type(raw_, col) == SQLITE_NULL;
    }

    template <typename T>
    [[nodiscard]] std::optional<T> column_optional_u(int col) const {
        if (column_is_null(col)) {
            return std::nullopt;
        }
        return static_cast<T>(column_int64(col));
    }

private:
    void check(int rc) {
        if (rc != SQLITE_OK) {
            throw RepositoryError{std::string{"sqlite: bind failed: "} + sqlite3_errmsg(db_)};
        }
    }

    sqlite3* db_;
    sqlite3_stmt* raw_ = nullptr;
};

constexpr std::string_view artist_columns = "id, name, sort_name";

[[nodiscard]] Artist row_to_artist(Stmt const& row) {
    Artist a;
    a.id = row.column_text(0);
    a.name = row.column_text(1);
    a.sort_name = row.column_text(2);
    return a;
}

constexpr std::string_view album_columns =
    "id, artist_id, title, sort_title, release_year, cover_art_asset_id";

[[nodiscard]] Album row_to_album(Stmt const& row) {
    Album a;
    a.id = row.column_text(0);
    a.artist_id = row.column_text(1);
    a.title = row.column_text(2);
    a.sort_title = row.column_text(3);
    a.release_year = row.column_optional_u<std::uint16_t>(4);
    if (!row.column_is_null(5)) {
        a.cover_art_asset_id = row.column_text(5);
    }
    return a;
}

constexpr std::string_view track_columns =
    "id, album_id, artist_id, title, sort_title, disc_number, track_number, duration_ms";

[[nodiscard]] Track row_to_track(Stmt const& row) {
    Track t;
    t.id = row.column_text(0);
    t.album_id = row.column_text(1);
    t.artist_id = row.column_text(2);
    t.title = row.column_text(3);
    t.sort_title = row.column_text(4);
    t.disc_number = row.column_optional_u<std::uint16_t>(5);
    t.track_number = row.column_optional_u<std::uint16_t>(6);
    t.duration_ms = static_cast<std::uint64_t>(row.column_int64(7));
    return t;
}

constexpr std::string_view rendition_columns =
    "id, track_id, codec, container, mime_type, bitrate_bps, sample_rate_hz, bit_depth, channels, "
    "duration_ms, size_bytes, checksum, storage_path, dlna_profile_name, protocol_info, purpose";

[[nodiscard]] sonarium::media::MediaRendition row_to_rendition(Stmt const& row) {
    using sonarium::media::AudioCodec;
    using sonarium::media::AudioContainer;
    using sonarium::media::RenditionPurpose;

    sonarium::media::MediaRendition m;
    m.id = row.column_text(0);
    m.track_id = row.column_text(1);
    m.codec = static_cast<AudioCodec>(row.column_int64(2));
    m.container = static_cast<AudioContainer>(row.column_int64(3));
    m.mime_type = row.column_text(4);
    m.bitrate_bps = static_cast<std::uint32_t>(row.column_int64(5));
    m.sample_rate_hz = static_cast<std::uint32_t>(row.column_int64(6));
    m.bit_depth = static_cast<std::uint8_t>(row.column_int64(7));
    m.channels = static_cast<std::uint8_t>(row.column_int64(8));
    m.duration_ms = static_cast<std::uint64_t>(row.column_int64(9));
    m.size_bytes = static_cast<std::uint64_t>(row.column_int64(10));
    m.checksum = row.column_text(11);
    m.storage_path = row.column_text(12);
    m.dlna_profile_name = row.column_text(13);
    m.protocol_info = row.column_text(14);
    m.purpose = static_cast<RenditionPurpose>(row.column_int64(15));
    return m;
}

[[nodiscard]] std::uint32_t count_scalar(sqlite3* db,
                                         std::string_view sql,
                                         std::optional<std::string_view> param = std::nullopt) {
    Stmt stmt{db, sql};
    if (param.has_value()) {
        stmt.bind_text(1, *param);
    }
    if (!stmt.step()) {
        return 0;
    }
    return static_cast<std::uint32_t>(stmt.column_int64(0));
}

} // namespace

SqliteRepository::SqliteRepository(sqlite3* db) noexcept : db_{db} {}

SqliteRepository::~SqliteRepository() {
    sqlite3_close(db_);
}

std::expected<std::shared_ptr<SqliteRepository>, std::string>
SqliteRepository::open(std::filesystem::path const& db_path) {
    sqlite3* db = nullptr;
    constexpr int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX;
    if (sqlite3_open_v2(db_path.string().c_str(), &db, flags, nullptr) != SQLITE_OK) {
        std::string err = (db != nullptr) ? sqlite3_errmsg(db) : "out of memory";
        sqlite3_close(db);
        return std::unexpected("sqlite open failed: " + err);
    }

    // WAL lets the worker write while the DLNA/HLS servers read the same file
    // from other processes; the busy timeout rides out write bursts.
    for (auto const* pragma : {"PRAGMA journal_mode=WAL",
                               "PRAGMA foreign_keys=ON",
                               "PRAGMA busy_timeout=5000",
                               "PRAGMA synchronous=NORMAL"}) {
        char* errmsg = nullptr;
        if (sqlite3_exec(db, pragma, nullptr, nullptr, &errmsg) != SQLITE_OK) {
            std::string err = (errmsg != nullptr) ? errmsg : "unknown error";
            sqlite3_free(errmsg);
            sqlite3_close(db);
            return std::unexpected("sqlite pragma failed: " + err);
        }
    }

    return std::shared_ptr<SqliteRepository>{new SqliteRepository{db}};
}

std::expected<void, std::string> SqliteRepository::ensure_schema() {
    std::scoped_lock const lock{mutex_};

    auto const run = [this](std::string_view sql) -> std::expected<void, std::string> {
        char* errmsg = nullptr;
        if (sqlite3_exec(db_, std::string{sql}.c_str(), nullptr, nullptr, &errmsg) != SQLITE_OK) {
            std::string err = (errmsg != nullptr) ? errmsg : "unknown error";
            sqlite3_free(errmsg);
            return std::unexpected("ensure_schema failed: " + err);
        }
        return {};
    };

    if (auto r = run(sqlite_schema::schema_version_ddl); !r.has_value()) {
        return r;
    }

    int stored_version = 0;
    try {
        Stmt stmt{db_, "SELECT COALESCE(MAX(version), 0) FROM schema_version"};
        if (stmt.step()) {
            stored_version = static_cast<int>(stmt.column_int64(0));
        }
    } catch (RepositoryError const& e) {
        return std::unexpected(std::string{"ensure_schema failed: "} + e.what());
    }

    if (stored_version > sqlite_schema::current_version) {
        return std::unexpected(
            "ensure_schema refused: database schema is v" + std::to_string(stored_version)
            + " but this binary only understands v" + std::to_string(sqlite_schema::current_version)
            + " — upgrade the binary");
    }

    if (stored_version == 0) {
        for (auto const& sql : sqlite_schema::all_statements) {
            if (auto r = run(sql); !r.has_value()) {
                return r;
            }
        }
        if (auto r = run("INSERT INTO schema_version (version) VALUES (1) "
                         "ON CONFLICT (version) DO NOTHING");
            !r.has_value()) {
            return r;
        }
        stored_version = 1;
    }

    for (auto const& migration : sqlite_schema::migrations) {
        if (migration.to_version <= stored_version) {
            continue;
        }
        if (auto r = run(migration.statement); !r.has_value()) {
            return r;
        }
        if (auto r =
                run("INSERT INTO schema_version (version) VALUES ("
                    + std::to_string(migration.to_version) + ") ON CONFLICT (version) DO NOTHING");
            !r.has_value()) {
            return r;
        }
        stored_version = migration.to_version;
    }

    return {};
}

std::uint32_t SqliteRepository::system_update_id() const {
    std::scoped_lock const lock{mutex_};
    return count_scalar(db_,
                        "SELECT update_counter FROM system_state WHERE key = 'content_directory'");
}

Page<Artist> SqliteRepository::list_artists(PageRequest req) const {
    std::scoped_lock const lock{mutex_};

    Page<Artist> out;
    out.total_matches = count_scalar(db_, "SELECT COUNT(*) FROM artist");

    Stmt stmt{db_,
              std::string{"SELECT "} + std::string{artist_columns}
                  + " FROM artist ORDER BY id LIMIT ?1 OFFSET ?2"};
    stmt.bind_int64(1, requested_or_default(req.requested_count));
    stmt.bind_int64(2, req.starting_index);
    while (stmt.step()) {
        out.rows.push_back(row_to_artist(stmt));
    }
    return out;
}

std::optional<Artist> SqliteRepository::get_artist(std::string_view id) const {
    std::scoped_lock const lock{mutex_};
    Stmt stmt{db_,
              std::string{"SELECT "} + std::string{artist_columns} + " FROM artist WHERE id = ?1"};
    stmt.bind_text(1, id);
    if (!stmt.step()) {
        return std::nullopt;
    }
    return row_to_artist(stmt);
}

Page<Album> SqliteRepository::list_albums_for_artist(std::string_view artist_id,
                                                     PageRequest req) const {
    std::scoped_lock const lock{mutex_};

    Page<Album> out;
    out.total_matches =
        count_scalar(db_, "SELECT COUNT(*) FROM album WHERE artist_id = ?1", artist_id);

    Stmt stmt{db_,
              std::string{"SELECT "} + std::string{album_columns}
                  + " FROM album WHERE artist_id = ?1 ORDER BY id LIMIT ?2 OFFSET ?3"};
    stmt.bind_text(1, artist_id);
    stmt.bind_int64(2, requested_or_default(req.requested_count));
    stmt.bind_int64(3, req.starting_index);
    while (stmt.step()) {
        out.rows.push_back(row_to_album(stmt));
    }
    return out;
}

std::optional<Album> SqliteRepository::get_album(std::string_view id) const {
    std::scoped_lock const lock{mutex_};
    Stmt stmt{db_,
              std::string{"SELECT "} + std::string{album_columns} + " FROM album WHERE id = ?1"};
    stmt.bind_text(1, id);
    if (!stmt.step()) {
        return std::nullopt;
    }
    return row_to_album(stmt);
}

Page<Track> SqliteRepository::list_tracks_for_album(std::string_view album_id,
                                                    PageRequest req) const {
    std::scoped_lock const lock{mutex_};

    Page<Track> out;
    out.total_matches =
        count_scalar(db_, "SELECT COUNT(*) FROM track WHERE album_id = ?1", album_id);

    Stmt stmt{db_,
              std::string{"SELECT "} + std::string{track_columns}
                  + " FROM track WHERE album_id = ?1 ORDER BY id LIMIT ?2 OFFSET ?3"};
    stmt.bind_text(1, album_id);
    stmt.bind_int64(2, requested_or_default(req.requested_count));
    stmt.bind_int64(3, req.starting_index);
    while (stmt.step()) {
        out.rows.push_back(row_to_track(stmt));
    }
    return out;
}

Page<Track> SqliteRepository::list_all_tracks(PageRequest req) const {
    std::scoped_lock const lock{mutex_};

    Page<Track> out;
    out.total_matches = count_scalar(db_, "SELECT COUNT(*) FROM track");

    Stmt stmt{db_,
              std::string{"SELECT "} + std::string{track_columns}
                  + " FROM track ORDER BY id LIMIT ?1 OFFSET ?2"};
    stmt.bind_int64(1, requested_or_default(req.requested_count));
    stmt.bind_int64(2, req.starting_index);
    while (stmt.step()) {
        out.rows.push_back(row_to_track(stmt));
    }
    return out;
}

std::optional<Track> SqliteRepository::get_track(std::string_view id) const {
    std::scoped_lock const lock{mutex_};
    Stmt stmt{db_,
              std::string{"SELECT "} + std::string{track_columns} + " FROM track WHERE id = ?1"};
    stmt.bind_text(1, id);
    if (!stmt.step()) {
        return std::nullopt;
    }
    return row_to_track(stmt);
}

std::vector<sonarium::media::MediaRendition>
SqliteRepository::list_renditions_for_track(std::string_view track_id) const {
    std::scoped_lock const lock{mutex_};
    Stmt stmt{db_,
              std::string{"SELECT "} + std::string{rendition_columns}
                  + " FROM media_rendition WHERE track_id = ?1 ORDER BY id"};
    stmt.bind_text(1, track_id);
    std::vector<sonarium::media::MediaRendition> out;
    while (stmt.step()) {
        out.push_back(row_to_rendition(stmt));
    }
    return out;
}

std::optional<sonarium::media::MediaRendition>
SqliteRepository::get_rendition(std::string_view rendition_id) const {
    std::scoped_lock const lock{mutex_};
    Stmt stmt{db_,
              std::string{"SELECT "} + std::string{rendition_columns}
                  + " FROM media_rendition WHERE id = ?1"};
    stmt.bind_text(1, rendition_id);
    if (!stmt.step()) {
        return std::nullopt;
    }
    return row_to_rendition(stmt);
}

std::vector<PlaylistItem>
SqliteRepository::load_playlist_items(std::string_view playlist_id) const {
    Stmt stmt{db_,
              "SELECT track_id, position FROM playlist_item WHERE playlist_id = ?1 "
              "ORDER BY position"};
    stmt.bind_text(1, playlist_id);
    std::vector<PlaylistItem> items;
    while (stmt.step()) {
        PlaylistItem item;
        item.track_id = stmt.column_text(0);
        item.position = static_cast<std::uint32_t>(stmt.column_int64(1));
        items.push_back(std::move(item));
    }
    return items;
}

Page<Playlist> SqliteRepository::list_playlists(PageRequest req) const {
    std::scoped_lock const lock{mutex_};

    Page<Playlist> out;
    out.total_matches = count_scalar(db_, "SELECT COUNT(*) FROM playlist");

    Stmt stmt{db_, "SELECT id, title FROM playlist ORDER BY id LIMIT ?1 OFFSET ?2"};
    stmt.bind_int64(1, requested_or_default(req.requested_count));
    stmt.bind_int64(2, req.starting_index);
    while (stmt.step()) {
        Playlist p;
        p.id = stmt.column_text(0);
        p.title = stmt.column_text(1);
        out.rows.push_back(std::move(p));
    }
    for (auto& p : out.rows) {
        p.items = load_playlist_items(p.id);
    }
    return out;
}

std::optional<Playlist> SqliteRepository::get_playlist(std::string_view id) const {
    std::scoped_lock const lock{mutex_};
    Playlist p;
    {
        Stmt stmt{db_, "SELECT id, title FROM playlist WHERE id = ?1"};
        stmt.bind_text(1, id);
        if (!stmt.step()) {
            return std::nullopt;
        }
        p.id = stmt.column_text(0);
        p.title = stmt.column_text(1);
    }
    p.items = load_playlist_items(p.id);
    return p;
}

std::optional<StorageAsset> SqliteRepository::get_asset(std::string_view asset_id) const {
    std::scoped_lock const lock{mutex_};
    Stmt stmt{db_, "SELECT id, storage_path, mime_type FROM storage_asset WHERE id = ?1"};
    stmt.bind_text(1, asset_id);
    if (!stmt.step()) {
        return std::nullopt;
    }
    StorageAsset s;
    s.id = stmt.column_text(0);
    s.storage_path = stmt.column_text(1);
    s.mime_type = stmt.column_text(2);
    return s;
}

// ---------------------------------------------------------------------------
// CatalogWriter — upserts mirror the Postgres SQL with SQLite's `excluded`.
// ---------------------------------------------------------------------------

std::expected<void, std::string> SqliteRepository::upsert_artist(Artist const& a) {
    std::scoped_lock const lock{mutex_};
    try {
        Stmt stmt{db_,
                  "INSERT INTO artist (id, name, sort_name) VALUES (?1, ?2, ?3) "
                  "ON CONFLICT (id) DO UPDATE SET "
                  "  name = excluded.name, "
                  "  sort_name = excluded.sort_name, "
                  "  updated_at = datetime('now')"};
        stmt.bind_text(1, a.id);
        stmt.bind_text(2, a.name);
        stmt.bind_text(3, a.sort_name);
        (void)stmt.step();
        return {};
    } catch (RepositoryError const& e) {
        return std::unexpected(std::string{"upsert_artist failed: "} + e.what());
    }
}

std::expected<void, std::string> SqliteRepository::upsert_album(Album const& al) {
    std::scoped_lock const lock{mutex_};
    try {
        Stmt stmt{db_,
                  "INSERT INTO album (id, artist_id, title, sort_title, release_year, "
                  "                   cover_art_asset_id) "
                  "VALUES (?1, ?2, ?3, ?4, ?5, ?6) "
                  "ON CONFLICT (id) DO UPDATE SET "
                  "  artist_id = excluded.artist_id, "
                  "  title = excluded.title, "
                  "  sort_title = excluded.sort_title, "
                  "  release_year = excluded.release_year, "
                  "  cover_art_asset_id = excluded.cover_art_asset_id, "
                  "  updated_at = datetime('now')"};
        stmt.bind_text(1, al.id);
        stmt.bind_text(2, al.artist_id);
        stmt.bind_text(3, al.title);
        stmt.bind_text(4, al.sort_title);
        stmt.bind_optional_int64(
            5,
            al.release_year.has_value()
                ? std::optional<std::int64_t>{static_cast<std::int64_t>(*al.release_year)}
                : std::nullopt);
        stmt.bind_optional_text(6, al.cover_art_asset_id);
        (void)stmt.step();
        return {};
    } catch (RepositoryError const& e) {
        return std::unexpected(std::string{"upsert_album failed: "} + e.what());
    }
}

std::expected<void, std::string> SqliteRepository::upsert_track(Track const& t) {
    std::scoped_lock const lock{mutex_};
    try {
        Stmt stmt{db_,
                  "INSERT INTO track (id, album_id, artist_id, title, sort_title, "
                  "                   disc_number, track_number, duration_ms) "
                  "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8) "
                  "ON CONFLICT (id) DO UPDATE SET "
                  "  album_id = excluded.album_id, "
                  "  artist_id = excluded.artist_id, "
                  "  title = excluded.title, "
                  "  sort_title = excluded.sort_title, "
                  "  disc_number = excluded.disc_number, "
                  "  track_number = excluded.track_number, "
                  "  duration_ms = excluded.duration_ms, "
                  "  updated_at = datetime('now')"};
        stmt.bind_text(1, t.id);
        stmt.bind_text(2, t.album_id);
        stmt.bind_text(3, t.artist_id);
        stmt.bind_text(4, t.title);
        stmt.bind_text(5, t.sort_title);
        stmt.bind_optional_int64(
            6,
            t.disc_number.has_value()
                ? std::optional<std::int64_t>{static_cast<std::int64_t>(*t.disc_number)}
                : std::nullopt);
        stmt.bind_optional_int64(
            7,
            t.track_number.has_value()
                ? std::optional<std::int64_t>{static_cast<std::int64_t>(*t.track_number)}
                : std::nullopt);
        stmt.bind_int64(8, static_cast<std::int64_t>(t.duration_ms));
        (void)stmt.step();
        return {};
    } catch (RepositoryError const& e) {
        return std::unexpected(std::string{"upsert_track failed: "} + e.what());
    }
}

std::expected<void, std::string>
SqliteRepository::upsert_rendition(sonarium::media::MediaRendition const& m) {
    std::scoped_lock const lock{mutex_};
    try {
        Stmt stmt{db_,
                  "INSERT INTO media_rendition (id, track_id, codec, container, mime_type, "
                  "    bitrate_bps, sample_rate_hz, bit_depth, channels, duration_ms, "
                  "    size_bytes, checksum, storage_path, dlna_profile_name, protocol_info, "
                  "    purpose) "
                  "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15, "
                  "        ?16) "
                  "ON CONFLICT (id) DO UPDATE SET "
                  "  track_id = excluded.track_id, "
                  "  codec = excluded.codec, "
                  "  container = excluded.container, "
                  "  mime_type = excluded.mime_type, "
                  "  bitrate_bps = excluded.bitrate_bps, "
                  "  sample_rate_hz = excluded.sample_rate_hz, "
                  "  bit_depth = excluded.bit_depth, "
                  "  channels = excluded.channels, "
                  "  duration_ms = excluded.duration_ms, "
                  "  size_bytes = excluded.size_bytes, "
                  "  checksum = excluded.checksum, "
                  "  storage_path = excluded.storage_path, "
                  "  dlna_profile_name = excluded.dlna_profile_name, "
                  "  protocol_info = excluded.protocol_info, "
                  "  purpose = excluded.purpose, "
                  "  updated_at = datetime('now')"};
        stmt.bind_text(1, m.id);
        stmt.bind_text(2, m.track_id);
        stmt.bind_int64(3, static_cast<std::int64_t>(m.codec));
        stmt.bind_int64(4, static_cast<std::int64_t>(m.container));
        stmt.bind_text(5, m.mime_type);
        stmt.bind_int64(6, static_cast<std::int64_t>(m.bitrate_bps));
        stmt.bind_int64(7, static_cast<std::int64_t>(m.sample_rate_hz));
        stmt.bind_int64(8, static_cast<std::int64_t>(m.bit_depth));
        stmt.bind_int64(9, static_cast<std::int64_t>(m.channels));
        stmt.bind_int64(10, static_cast<std::int64_t>(m.duration_ms));
        stmt.bind_int64(11, static_cast<std::int64_t>(m.size_bytes));
        stmt.bind_text(12, m.checksum);
        stmt.bind_text(13, m.storage_path);
        stmt.bind_text(14, m.dlna_profile_name);
        stmt.bind_text(15, m.protocol_info);
        stmt.bind_int64(16, static_cast<std::int64_t>(m.purpose));
        (void)stmt.step();
        return {};
    } catch (RepositoryError const& e) {
        return std::unexpected(std::string{"upsert_rendition failed: "} + e.what());
    }
}

std::expected<void, std::string> SqliteRepository::upsert_asset(StorageAsset const& s) {
    std::scoped_lock const lock{mutex_};
    try {
        Stmt stmt{db_,
                  "INSERT INTO storage_asset (id, storage_path, mime_type) "
                  "VALUES (?1, ?2, ?3) "
                  "ON CONFLICT (id) DO UPDATE SET "
                  "  storage_path = excluded.storage_path, "
                  "  mime_type = excluded.mime_type"};
        stmt.bind_text(1, s.id);
        stmt.bind_text(2, s.storage_path);
        stmt.bind_text(3, s.mime_type);
        (void)stmt.step();
        return {};
    } catch (RepositoryError const& e) {
        return std::unexpected(std::string{"upsert_asset failed: "} + e.what());
    }
}

std::expected<void, std::string> SqliteRepository::bump_system_update_id() {
    std::scoped_lock const lock{mutex_};
    try {
        Stmt stmt{db_,
                  "UPDATE system_state SET update_counter = update_counter + 1 "
                  "WHERE key = 'content_directory'"};
        (void)stmt.step();
        return {};
    } catch (RepositoryError const& e) {
        return std::unexpected(std::string{"bump_system_update_id failed: "} + e.what());
    }
}

} // namespace sonarium::catalog
