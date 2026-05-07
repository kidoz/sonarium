#include "catalog/postgres_repository.hpp"

#include <algorithm>
#include <asterorm/postgres/driver.hpp>
#include <charconv>
#include <cstdint>
#include <string>
#include <utility>

#include "catalog/postgres_schema.hpp"

namespace sonarium::catalog {

namespace {

constexpr std::size_t default_page_count = 1024;

[[nodiscard]] std::vector<std::optional<std::string>>
to_params(std::initializer_list<std::optional<std::string>> values) {
    return std::vector<std::optional<std::string>>{values};
}

[[nodiscard]] std::optional<std::string> as_param(std::string_view sv) {
    return std::string{sv};
}

[[nodiscard]] std::optional<std::string> as_param(std::uint32_t v) {
    return std::to_string(v);
}

[[nodiscard]] std::optional<std::string> as_param(std::uint64_t v) {
    return std::to_string(v);
}

[[nodiscard]] std::optional<std::string> as_nullable(std::optional<std::string> const& v) {
    return v;
}

template <typename T>
[[nodiscard]] std::optional<std::string> as_nullable_int(std::optional<T> const& v) {
    if (!v.has_value()) {
        return std::nullopt;
    }
    return std::to_string(*v);
}

template <typename T>
[[nodiscard]] T parse_u(std::string const& s) noexcept {
    T out{};
    auto const* first = s.data();
    auto const* last = s.data() + s.size();
    auto const r = std::from_chars(first, last, out);
    if (r.ec != std::errc{}) {
        return T{0};
    }
    return out;
}

// libpq returns NULL columns as `nullopt` from `get_string`. NOT NULL TEXT
// columns therefore always carry a value — these helpers fall back to "" /
// 0 only as a defence against schema drift.
[[nodiscard]] std::string get_text(::asterorm::pg::result const& rows, int r, int c) {
    if (auto v = rows.get_string(r, c); v.has_value()) {
        return std::move(*v);
    }
    return {};
}

template <typename T>
[[nodiscard]] T get_u(::asterorm::pg::result const& rows, int r, int c) {
    if (auto v = rows.get_string(r, c); v.has_value()) {
        return parse_u<T>(*v);
    }
    return T{0};
}

template <typename T>
[[nodiscard]] std::optional<T> get_optional_u(::asterorm::pg::result const& rows, int r, int c) {
    auto v = rows.get_string(r, c);
    if (!v.has_value()) {
        return std::nullopt;
    }
    return parse_u<T>(*v);
}

[[nodiscard]] std::optional<std::string>
get_optional_text(::asterorm::pg::result const& rows, int r, int c) {
    return rows.get_string(r, c);
}

[[nodiscard]] std::uint32_t count_query(::asterorm::pg::connection& conn, std::string const& sql) {
    auto res = conn.execute(sql);
    if (!res.has_value() || res->rows() == 0) {
        return 0;
    }
    auto cell = res->get_string(0, 0);
    if (!cell.has_value()) {
        return 0;
    }
    return parse_u<std::uint32_t>(*cell);
}

[[nodiscard]] std::uint32_t
count_query_params(::asterorm::pg::connection& conn,
                   std::string const& sql,
                   std::vector<std::optional<std::string>> const& params) {
    auto res = conn.execute_params(sql, params);
    if (!res.has_value() || res->rows() == 0) {
        return 0;
    }
    auto cell = res->get_string(0, 0);
    if (!cell.has_value()) {
        return 0;
    }
    return parse_u<std::uint32_t>(*cell);
}

[[nodiscard]] std::uint32_t requested_or_default(std::uint32_t requested) noexcept {
    return (requested == 0) ? static_cast<std::uint32_t>(default_page_count) : requested;
}

} // namespace

PostgresRepository::PostgresRepository(std::shared_ptr<::asterorm::pg::connection> conn) noexcept
    : conn_{std::move(conn)} {}

std::expected<void, std::string> PostgresRepository::ensure_schema() {
    std::scoped_lock const lock{mutex_};
    for (auto const& sql : postgres_schema::all_statements) {
        auto res = conn_->execute(sql);
        if (!res.has_value()) {
            return std::unexpected(std::string{"ensure_schema failed: "} + res.error().message);
        }
    }
    return {};
}

std::expected<std::shared_ptr<PostgresRepository>, std::string>
PostgresRepository::open(std::string const& conninfo) {
    ::asterorm::pg::driver driver;
    auto conn = driver.connect(conninfo);
    if (!conn.has_value()) {
        return std::unexpected(std::string{"connect failed: "} + conn.error().message);
    }
    auto shared = std::make_shared<::asterorm::pg::connection>(std::move(*conn));
    return std::make_shared<PostgresRepository>(std::move(shared));
}

std::uint32_t PostgresRepository::system_update_id() const {
    std::scoped_lock const lock{mutex_};
    auto res =
        conn_->execute("SELECT update_counter FROM system_state WHERE key = 'content_directory'");
    if (!res.has_value() || res->rows() == 0) {
        return 0;
    }
    auto const cell = res->get_string(0, 0);
    if (!cell.has_value()) {
        return 0;
    }
    return parse_u<std::uint32_t>(*cell);
}

// ---------------------------------------------------------------------------
// Artists
// ---------------------------------------------------------------------------

Page<Artist> PostgresRepository::list_artists(PageRequest req) const {
    std::scoped_lock const lock{mutex_};

    Page<Artist> out;
    out.total_matches = count_query(*conn_, "SELECT COUNT(*) FROM artist");

    auto rows = conn_->execute_params(
        "SELECT id, name, sort_name FROM artist ORDER BY id LIMIT $1 OFFSET $2",
        to_params(
            {as_param(requested_or_default(req.requested_count)), as_param(req.starting_index)}));
    if (!rows.has_value()) {
        return out;
    }

    auto const row_count = rows->rows();
    out.rows.reserve(static_cast<std::size_t>(row_count));
    for (int r = 0; r < row_count; ++r) {
        Artist a;
        a.id = get_text(*rows, r, 0);
        a.name = get_text(*rows, r, 1);
        a.sort_name = get_text(*rows, r, 2);
        out.rows.push_back(std::move(a));
    }
    return out;
}

std::optional<Artist> PostgresRepository::get_artist(std::string_view id) const {
    std::scoped_lock const lock{mutex_};
    auto rows = conn_->execute_params("SELECT id, name, sort_name FROM artist WHERE id = $1",
                                      to_params({as_param(id)}));
    if (!rows.has_value() || rows->rows() == 0) {
        return std::nullopt;
    }
    Artist a;
    a.id = get_text(*rows, 0, 0);
    a.name = get_text(*rows, 0, 1);
    a.sort_name = get_text(*rows, 0, 2);
    return a;
}

// ---------------------------------------------------------------------------
// Albums
// ---------------------------------------------------------------------------

namespace {

constexpr std::string_view album_columns =
    "id, artist_id, title, sort_title, release_year, cover_art_asset_id";

[[nodiscard]] Album row_to_album(::asterorm::pg::result const& rows, int r) {
    Album a;
    a.id = get_text(rows, r, 0);
    a.artist_id = get_text(rows, r, 1);
    a.title = get_text(rows, r, 2);
    a.sort_title = get_text(rows, r, 3);
    a.release_year = get_optional_u<std::uint16_t>(rows, r, 4);
    a.cover_art_asset_id = get_optional_text(rows, r, 5);
    return a;
}

} // namespace

Page<Album> PostgresRepository::list_albums_for_artist(std::string_view artist_id,
                                                       PageRequest req) const {
    std::scoped_lock const lock{mutex_};

    Page<Album> out;
    out.total_matches = count_query_params(*conn_,
                                           "SELECT COUNT(*) FROM album WHERE artist_id = $1",
                                           to_params({as_param(artist_id)}));

    auto rows = conn_->execute_params(
        std::string{"SELECT "} + std::string{album_columns}
            + " FROM album WHERE artist_id = $1 ORDER BY id LIMIT $2 OFFSET $3",
        to_params({as_param(artist_id),
                   as_param(requested_or_default(req.requested_count)),
                   as_param(req.starting_index)}));
    if (!rows.has_value()) {
        return out;
    }

    auto const row_count = rows->rows();
    out.rows.reserve(static_cast<std::size_t>(row_count));
    for (int r = 0; r < row_count; ++r) {
        out.rows.push_back(row_to_album(*rows, r));
    }
    return out;
}

std::optional<Album> PostgresRepository::get_album(std::string_view id) const {
    std::scoped_lock const lock{mutex_};
    auto rows = conn_->execute_params(std::string{"SELECT "} + std::string{album_columns}
                                          + " FROM album WHERE id = $1",
                                      to_params({as_param(id)}));
    if (!rows.has_value() || rows->rows() == 0) {
        return std::nullopt;
    }
    return row_to_album(*rows, 0);
}

// ---------------------------------------------------------------------------
// Tracks
// ---------------------------------------------------------------------------

namespace {

constexpr std::string_view track_columns =
    "id, album_id, artist_id, title, sort_title, disc_number, track_number, duration_ms";

[[nodiscard]] Track row_to_track(::asterorm::pg::result const& rows, int r) {
    Track t;
    t.id = get_text(rows, r, 0);
    t.album_id = get_text(rows, r, 1);
    t.artist_id = get_text(rows, r, 2);
    t.title = get_text(rows, r, 3);
    t.sort_title = get_text(rows, r, 4);
    t.disc_number = get_optional_u<std::uint16_t>(rows, r, 5);
    t.track_number = get_optional_u<std::uint16_t>(rows, r, 6);
    t.duration_ms = get_u<std::uint64_t>(rows, r, 7);
    return t;
}

} // namespace

Page<Track> PostgresRepository::list_tracks_for_album(std::string_view album_id,
                                                      PageRequest req) const {
    std::scoped_lock const lock{mutex_};

    Page<Track> out;
    out.total_matches = count_query_params(
        *conn_, "SELECT COUNT(*) FROM track WHERE album_id = $1", to_params({as_param(album_id)}));

    auto rows = conn_->execute_params(
        std::string{"SELECT "} + std::string{track_columns}
            + " FROM track WHERE album_id = $1 ORDER BY id LIMIT $2 OFFSET $3",
        to_params({as_param(album_id),
                   as_param(requested_or_default(req.requested_count)),
                   as_param(req.starting_index)}));
    if (!rows.has_value()) {
        return out;
    }

    auto const row_count = rows->rows();
    out.rows.reserve(static_cast<std::size_t>(row_count));
    for (int r = 0; r < row_count; ++r) {
        out.rows.push_back(row_to_track(*rows, r));
    }
    return out;
}

Page<Track> PostgresRepository::list_all_tracks(PageRequest req) const {
    std::scoped_lock const lock{mutex_};

    Page<Track> out;
    out.total_matches = count_query(*conn_, "SELECT COUNT(*) FROM track");

    auto rows =
        conn_->execute_params(std::string{"SELECT "} + std::string{track_columns}
                                  + " FROM track ORDER BY id LIMIT $1 OFFSET $2",
                              to_params({as_param(requested_or_default(req.requested_count)),
                                         as_param(req.starting_index)}));
    if (!rows.has_value()) {
        return out;
    }

    auto const row_count = rows->rows();
    out.rows.reserve(static_cast<std::size_t>(row_count));
    for (int r = 0; r < row_count; ++r) {
        out.rows.push_back(row_to_track(*rows, r));
    }
    return out;
}

std::optional<Track> PostgresRepository::get_track(std::string_view id) const {
    std::scoped_lock const lock{mutex_};
    auto rows = conn_->execute_params(std::string{"SELECT "} + std::string{track_columns}
                                          + " FROM track WHERE id = $1",
                                      to_params({as_param(id)}));
    if (!rows.has_value() || rows->rows() == 0) {
        return std::nullopt;
    }
    return row_to_track(*rows, 0);
}

// ---------------------------------------------------------------------------
// Renditions
// ---------------------------------------------------------------------------

namespace {

constexpr std::string_view rendition_columns =
    "id, track_id, codec, container, mime_type, bitrate_bps, sample_rate_hz, bit_depth, channels, "
    "duration_ms, size_bytes, checksum, storage_path, dlna_profile_name, protocol_info, purpose";

[[nodiscard]] sonarium::media::MediaRendition row_to_rendition(::asterorm::pg::result const& rows,
                                                               int r) {
    using sonarium::media::AudioCodec;
    using sonarium::media::AudioContainer;
    using sonarium::media::RenditionPurpose;

    sonarium::media::MediaRendition m;
    m.id = get_text(rows, r, 0);
    m.track_id = get_text(rows, r, 1);
    m.codec = static_cast<AudioCodec>(get_u<std::uint8_t>(rows, r, 2));
    m.container = static_cast<AudioContainer>(get_u<std::uint8_t>(rows, r, 3));
    m.mime_type = get_text(rows, r, 4);
    m.bitrate_bps = get_u<std::uint32_t>(rows, r, 5);
    m.sample_rate_hz = get_u<std::uint32_t>(rows, r, 6);
    m.bit_depth = get_u<std::uint8_t>(rows, r, 7);
    m.channels = get_u<std::uint8_t>(rows, r, 8);
    m.duration_ms = get_u<std::uint64_t>(rows, r, 9);
    m.size_bytes = get_u<std::uint64_t>(rows, r, 10);
    m.checksum = get_text(rows, r, 11);
    m.storage_path = get_text(rows, r, 12);
    m.dlna_profile_name = get_text(rows, r, 13);
    m.protocol_info = get_text(rows, r, 14);
    m.purpose = static_cast<RenditionPurpose>(get_u<std::uint8_t>(rows, r, 15));
    return m;
}

} // namespace

std::vector<sonarium::media::MediaRendition>
PostgresRepository::list_renditions_for_track(std::string_view track_id) const {
    std::scoped_lock const lock{mutex_};
    auto rows = conn_->execute_params(std::string{"SELECT "} + std::string{rendition_columns}
                                          + " FROM media_rendition WHERE track_id = $1 ORDER BY id",
                                      to_params({as_param(track_id)}));
    if (!rows.has_value()) {
        return {};
    }

    std::vector<sonarium::media::MediaRendition> out;
    auto const row_count = rows->rows();
    out.reserve(static_cast<std::size_t>(row_count));
    for (int r = 0; r < row_count; ++r) {
        out.push_back(row_to_rendition(*rows, r));
    }
    return out;
}

std::optional<sonarium::media::MediaRendition>
PostgresRepository::get_rendition(std::string_view rendition_id) const {
    std::scoped_lock const lock{mutex_};
    auto rows = conn_->execute_params(std::string{"SELECT "} + std::string{rendition_columns}
                                          + " FROM media_rendition WHERE id = $1",
                                      to_params({as_param(rendition_id)}));
    if (!rows.has_value() || rows->rows() == 0) {
        return std::nullopt;
    }
    return row_to_rendition(*rows, 0);
}

// ---------------------------------------------------------------------------
// Playlists
// ---------------------------------------------------------------------------

namespace {

[[nodiscard]] std::vector<PlaylistItem> load_playlist_items(::asterorm::pg::connection& conn,
                                                            std::string_view playlist_id) {
    auto rows = conn.execute_params(
        "SELECT track_id, position FROM playlist_item WHERE playlist_id = $1 ORDER BY position",
        to_params({as_param(playlist_id)}));
    if (!rows.has_value()) {
        return {};
    }

    std::vector<PlaylistItem> items;
    auto const row_count = rows->rows();
    items.reserve(static_cast<std::size_t>(row_count));
    for (int r = 0; r < row_count; ++r) {
        PlaylistItem item;
        item.track_id = get_text(*rows, r, 0);
        item.position = get_u<std::uint32_t>(*rows, r, 1);
        items.push_back(std::move(item));
    }
    return items;
}

} // namespace

Page<Playlist> PostgresRepository::list_playlists(PageRequest req) const {
    std::scoped_lock const lock{mutex_};

    Page<Playlist> out;
    out.total_matches = count_query(*conn_, "SELECT COUNT(*) FROM playlist");

    auto rows =
        conn_->execute_params("SELECT id, title FROM playlist ORDER BY id LIMIT $1 OFFSET $2",
                              to_params({as_param(requested_or_default(req.requested_count)),
                                         as_param(req.starting_index)}));
    if (!rows.has_value()) {
        return out;
    }

    auto const row_count = rows->rows();
    out.rows.reserve(static_cast<std::size_t>(row_count));
    for (int r = 0; r < row_count; ++r) {
        Playlist p;
        p.id = get_text(*rows, r, 0);
        p.title = get_text(*rows, r, 1);
        p.items = load_playlist_items(*conn_, p.id);
        out.rows.push_back(std::move(p));
    }
    return out;
}

std::optional<Playlist> PostgresRepository::get_playlist(std::string_view id) const {
    std::scoped_lock const lock{mutex_};
    auto rows = conn_->execute_params("SELECT id, title FROM playlist WHERE id = $1",
                                      to_params({as_param(id)}));
    if (!rows.has_value() || rows->rows() == 0) {
        return std::nullopt;
    }
    Playlist p;
    p.id = get_text(*rows, 0, 0);
    p.title = get_text(*rows, 0, 1);
    p.items = load_playlist_items(*conn_, p.id);
    return p;
}

// ---------------------------------------------------------------------------
// Storage assets
// ---------------------------------------------------------------------------

std::optional<StorageAsset> PostgresRepository::get_asset(std::string_view asset_id) const {
    std::scoped_lock const lock{mutex_};
    auto rows =
        conn_->execute_params("SELECT id, storage_path, mime_type FROM storage_asset WHERE id = $1",
                              to_params({as_param(asset_id)}));
    if (!rows.has_value() || rows->rows() == 0) {
        return std::nullopt;
    }
    StorageAsset s;
    s.id = get_text(*rows, 0, 0);
    s.storage_path = get_text(*rows, 0, 1);
    s.mime_type = get_text(*rows, 0, 2);
    return s;
}

// ---------------------------------------------------------------------------
// CatalogWriter — upserts. INSERT ... ON CONFLICT (id) DO UPDATE so re-running
// the import keeps schemas/metadata in sync without churning created_at while
// updating updated_at via NOW().
// ---------------------------------------------------------------------------

namespace {

[[nodiscard]] std::expected<void, std::string>
exec_or_error(::asterorm::pg::connection& conn,
              std::string const& sql,
              std::vector<std::optional<std::string>> const& params,
              std::string_view label) {
    auto res = conn.execute_params(sql, params);
    if (!res.has_value()) {
        return std::unexpected(std::string{label} + " failed: " + res.error().message);
    }
    return {};
}

} // namespace

std::expected<void, std::string> PostgresRepository::upsert_artist(Artist const& a) {
    std::scoped_lock const lock{mutex_};
    return exec_or_error(*conn_,
                         "INSERT INTO artist (id, name, sort_name) VALUES ($1, $2, $3) "
                         "ON CONFLICT (id) DO UPDATE SET "
                         "  name = EXCLUDED.name, "
                         "  sort_name = EXCLUDED.sort_name, "
                         "  updated_at = NOW()",
                         to_params({as_param(a.id), as_param(a.name), as_param(a.sort_name)}),
                         "upsert_artist");
}

std::expected<void, std::string> PostgresRepository::upsert_album(Album const& al) {
    std::scoped_lock const lock{mutex_};
    return exec_or_error(
        *conn_,
        "INSERT INTO album (id, artist_id, title, sort_title, release_year, cover_art_asset_id) "
        "VALUES ($1, $2, $3, $4, $5, $6) "
        "ON CONFLICT (id) DO UPDATE SET "
        "  artist_id = EXCLUDED.artist_id, "
        "  title = EXCLUDED.title, "
        "  sort_title = EXCLUDED.sort_title, "
        "  release_year = EXCLUDED.release_year, "
        "  cover_art_asset_id = EXCLUDED.cover_art_asset_id, "
        "  updated_at = NOW()",
        to_params({as_param(al.id),
                   as_param(al.artist_id),
                   as_param(al.title),
                   as_param(al.sort_title),
                   as_nullable_int(al.release_year),
                   as_nullable(al.cover_art_asset_id)}),
        "upsert_album");
}

std::expected<void, std::string> PostgresRepository::upsert_track(Track const& t) {
    std::scoped_lock const lock{mutex_};
    return exec_or_error(*conn_,
                         "INSERT INTO track (id, album_id, artist_id, title, sort_title, "
                         "                   disc_number, track_number, duration_ms) "
                         "VALUES ($1, $2, $3, $4, $5, $6, $7, $8) "
                         "ON CONFLICT (id) DO UPDATE SET "
                         "  album_id = EXCLUDED.album_id, "
                         "  artist_id = EXCLUDED.artist_id, "
                         "  title = EXCLUDED.title, "
                         "  sort_title = EXCLUDED.sort_title, "
                         "  disc_number = EXCLUDED.disc_number, "
                         "  track_number = EXCLUDED.track_number, "
                         "  duration_ms = EXCLUDED.duration_ms, "
                         "  updated_at = NOW()",
                         to_params({as_param(t.id),
                                    as_param(t.album_id),
                                    as_param(t.artist_id),
                                    as_param(t.title),
                                    as_param(t.sort_title),
                                    as_nullable_int(t.disc_number),
                                    as_nullable_int(t.track_number),
                                    as_param(t.duration_ms)}),
                         "upsert_track");
}

std::expected<void, std::string>
PostgresRepository::upsert_rendition(sonarium::media::MediaRendition const& m) {
    std::scoped_lock const lock{mutex_};
    return exec_or_error(
        *conn_,
        "INSERT INTO media_rendition (id, track_id, codec, container, mime_type, "
        "    bitrate_bps, sample_rate_hz, bit_depth, channels, duration_ms, size_bytes, "
        "    checksum, storage_path, dlna_profile_name, protocol_info, purpose) "
        "VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13, $14, $15, $16) "
        "ON CONFLICT (id) DO UPDATE SET "
        "  track_id = EXCLUDED.track_id, "
        "  codec = EXCLUDED.codec, "
        "  container = EXCLUDED.container, "
        "  mime_type = EXCLUDED.mime_type, "
        "  bitrate_bps = EXCLUDED.bitrate_bps, "
        "  sample_rate_hz = EXCLUDED.sample_rate_hz, "
        "  bit_depth = EXCLUDED.bit_depth, "
        "  channels = EXCLUDED.channels, "
        "  duration_ms = EXCLUDED.duration_ms, "
        "  size_bytes = EXCLUDED.size_bytes, "
        "  checksum = EXCLUDED.checksum, "
        "  storage_path = EXCLUDED.storage_path, "
        "  dlna_profile_name = EXCLUDED.dlna_profile_name, "
        "  protocol_info = EXCLUDED.protocol_info, "
        "  purpose = EXCLUDED.purpose, "
        "  updated_at = NOW()",
        to_params({as_param(m.id),
                   as_param(m.track_id),
                   as_param(static_cast<std::uint32_t>(m.codec)),
                   as_param(static_cast<std::uint32_t>(m.container)),
                   as_param(m.mime_type),
                   as_param(static_cast<std::uint64_t>(m.bitrate_bps)),
                   as_param(static_cast<std::uint64_t>(m.sample_rate_hz)),
                   as_param(static_cast<std::uint32_t>(m.bit_depth)),
                   as_param(static_cast<std::uint32_t>(m.channels)),
                   as_param(m.duration_ms),
                   as_param(m.size_bytes),
                   as_param(m.checksum),
                   as_param(m.storage_path),
                   as_param(m.dlna_profile_name),
                   as_param(m.protocol_info),
                   as_param(static_cast<std::uint32_t>(m.purpose))}),
        "upsert_rendition");
}

std::expected<void, std::string> PostgresRepository::upsert_asset(StorageAsset const& s) {
    std::scoped_lock const lock{mutex_};
    return exec_or_error(
        *conn_,
        "INSERT INTO storage_asset (id, storage_path, mime_type) "
        "VALUES ($1, $2, $3) "
        "ON CONFLICT (id) DO UPDATE SET "
        "  storage_path = EXCLUDED.storage_path, "
        "  mime_type = EXCLUDED.mime_type",
        to_params({as_param(s.id), as_param(s.storage_path), as_param(s.mime_type)}),
        "upsert_asset");
}

std::expected<void, std::string> PostgresRepository::bump_system_update_id() {
    std::scoped_lock const lock{mutex_};
    auto res = conn_->execute("UPDATE system_state SET update_counter = update_counter + 1 "
                              "WHERE key = 'content_directory'");
    if (!res.has_value()) {
        return std::unexpected(std::string{"bump_system_update_id failed: "} + res.error().message);
    }
    return {};
}

} // namespace sonarium::catalog
