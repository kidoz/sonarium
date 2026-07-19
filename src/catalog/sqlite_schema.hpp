#pragma once

#include <array>
#include <string_view>

#include "catalog/postgres_schema.hpp"

namespace sonarium::catalog::sqlite_schema {

// SQLite translation of postgres_schema: same tables, columns, and ordering
// so row_to_* mapping code stays identical across backends. Differences:
// TIMESTAMPTZ → TEXT with datetime('now') defaults, and SQLite's integer
// affinity absorbs SMALLINT/BIGINT. Keep the two files in sync — the shared
// schema version below is the contract.

constexpr int current_version = postgres_schema::current_version;

constexpr std::string_view artist_ddl = R"sql(
CREATE TABLE IF NOT EXISTS artist (
    id          TEXT PRIMARY KEY,
    name        TEXT NOT NULL,
    sort_name   TEXT NOT NULL DEFAULT '',
    created_at  TEXT NOT NULL DEFAULT (datetime('now')),
    updated_at  TEXT NOT NULL DEFAULT (datetime('now'))
)
)sql";

constexpr std::string_view album_ddl = R"sql(
CREATE TABLE IF NOT EXISTS album (
    id                   TEXT PRIMARY KEY,
    artist_id            TEXT NOT NULL REFERENCES artist(id) ON DELETE CASCADE,
    title                TEXT NOT NULL,
    sort_title           TEXT NOT NULL DEFAULT '',
    release_year         INTEGER,
    cover_art_asset_id   TEXT,
    created_at           TEXT NOT NULL DEFAULT (datetime('now')),
    updated_at           TEXT NOT NULL DEFAULT (datetime('now'))
)
)sql";

constexpr std::string_view track_ddl = R"sql(
CREATE TABLE IF NOT EXISTS track (
    id           TEXT PRIMARY KEY,
    album_id     TEXT NOT NULL REFERENCES album(id) ON DELETE CASCADE,
    artist_id    TEXT NOT NULL REFERENCES artist(id) ON DELETE CASCADE,
    title        TEXT NOT NULL,
    sort_title   TEXT NOT NULL DEFAULT '',
    disc_number  INTEGER,
    track_number INTEGER,
    duration_ms  INTEGER NOT NULL DEFAULT 0,
    created_at   TEXT NOT NULL DEFAULT (datetime('now')),
    updated_at   TEXT NOT NULL DEFAULT (datetime('now'))
)
)sql";

constexpr std::string_view media_rendition_ddl = R"sql(
CREATE TABLE IF NOT EXISTS media_rendition (
    id                  TEXT PRIMARY KEY,
    track_id            TEXT NOT NULL REFERENCES track(id) ON DELETE CASCADE,
    codec               INTEGER NOT NULL,
    container           INTEGER NOT NULL,
    mime_type           TEXT NOT NULL DEFAULT '',
    bitrate_bps         INTEGER NOT NULL DEFAULT 0,
    sample_rate_hz      INTEGER NOT NULL DEFAULT 0,
    bit_depth           INTEGER NOT NULL DEFAULT 0,
    channels            INTEGER NOT NULL DEFAULT 0,
    duration_ms         INTEGER NOT NULL DEFAULT 0,
    size_bytes          INTEGER NOT NULL DEFAULT 0,
    checksum            TEXT NOT NULL DEFAULT '',
    storage_path        TEXT NOT NULL,
    dlna_profile_name   TEXT NOT NULL DEFAULT '',
    protocol_info       TEXT NOT NULL DEFAULT '',
    purpose             INTEGER NOT NULL DEFAULT 0,
    created_at          TEXT NOT NULL DEFAULT (datetime('now')),
    updated_at          TEXT NOT NULL DEFAULT (datetime('now'))
)
)sql";

constexpr std::string_view storage_asset_ddl = R"sql(
CREATE TABLE IF NOT EXISTS storage_asset (
    id            TEXT PRIMARY KEY,
    storage_path  TEXT NOT NULL,
    mime_type     TEXT NOT NULL DEFAULT 'application/octet-stream',
    created_at    TEXT NOT NULL DEFAULT (datetime('now'))
)
)sql";

constexpr std::string_view playlist_ddl = R"sql(
CREATE TABLE IF NOT EXISTS playlist (
    id          TEXT PRIMARY KEY,
    title       TEXT NOT NULL,
    created_at  TEXT NOT NULL DEFAULT (datetime('now')),
    updated_at  TEXT NOT NULL DEFAULT (datetime('now'))
)
)sql";

constexpr std::string_view playlist_item_ddl = R"sql(
CREATE TABLE IF NOT EXISTS playlist_item (
    playlist_id TEXT NOT NULL REFERENCES playlist(id) ON DELETE CASCADE,
    track_id    TEXT NOT NULL REFERENCES track(id) ON DELETE CASCADE,
    position    INTEGER NOT NULL,
    PRIMARY KEY (playlist_id, position)
)
)sql";

constexpr std::string_view system_state_ddl = R"sql(
CREATE TABLE IF NOT EXISTS system_state (
    key            TEXT PRIMARY KEY,
    update_counter INTEGER NOT NULL DEFAULT 0
)
)sql";

constexpr std::string_view system_state_seed = R"sql(
INSERT INTO system_state (key, update_counter)
VALUES ('content_directory', 0)
ON CONFLICT (key) DO NOTHING
)sql";

constexpr std::string_view schema_version_ddl = R"sql(
CREATE TABLE IF NOT EXISTS schema_version (
    version     INTEGER PRIMARY KEY,
    applied_at  TEXT NOT NULL DEFAULT (datetime('now'))
)
)sql";

constexpr std::string_view album_artist_idx_ddl =
    "CREATE INDEX IF NOT EXISTS idx_album_artist_id ON album(artist_id)";
constexpr std::string_view track_album_idx_ddl =
    "CREATE INDEX IF NOT EXISTS idx_track_album_id ON track(album_id)";
constexpr std::string_view rendition_track_idx_ddl =
    "CREATE INDEX IF NOT EXISTS idx_media_rendition_track_id ON media_rendition(track_id)";

constexpr std::array<std::string_view, 12> all_statements = {
    artist_ddl,
    album_ddl,
    track_ddl,
    media_rendition_ddl,
    storage_asset_ddl,
    playlist_ddl,
    playlist_item_ddl,
    system_state_ddl,
    system_state_seed,
    album_artist_idx_ddl,
    track_album_idx_ddl,
    rendition_track_idx_ddl,
};

// Ordered upgrade steps beyond the baseline, mirroring
// postgres_schema::migrations — append here whenever that list grows.
constexpr std::array<postgres_schema::Migration, 0> migrations = {};

} // namespace sonarium::catalog::sqlite_schema
