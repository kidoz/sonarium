#pragma once

#include <array>
#include <string_view>

namespace sonarium::catalog::postgres_schema {

// Forward-only DDL applied by `PostgresRepository::ensure_schema()`. Each
// statement is idempotent (`CREATE TABLE IF NOT EXISTS`, `CREATE INDEX IF NOT
// EXISTS`) so re-running on an existing DB is a no-op. When the schema needs
// to evolve, append new statements; never edit an existing one — that's a
// future migration concern that requires a separate version table.
//
// Mirrors `.agents/contexts/services/sonarium-worker.md` and the `Repository`
// interface. Column names match the C++ field names where practical.

constexpr std::string_view artist_ddl = R"sql(
CREATE TABLE IF NOT EXISTS artist (
    id          TEXT PRIMARY KEY,
    name        TEXT NOT NULL,
    sort_name   TEXT NOT NULL DEFAULT '',
    created_at  TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at  TIMESTAMPTZ NOT NULL DEFAULT NOW()
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
    created_at           TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at           TIMESTAMPTZ NOT NULL DEFAULT NOW()
)
)sql";

constexpr std::string_view track_ddl = R"sql(
CREATE TABLE IF NOT EXISTS track (
    id           TEXT PRIMARY KEY,
    album_id     TEXT NOT NULL REFERENCES album(id) ON DELETE CASCADE,
    artist_id    TEXT NOT NULL REFERENCES artist(id) ON DELETE CASCADE,
    title        TEXT NOT NULL,
    sort_title   TEXT NOT NULL DEFAULT '',
    disc_number  SMALLINT,
    track_number SMALLINT,
    duration_ms  BIGINT NOT NULL DEFAULT 0,
    created_at   TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at   TIMESTAMPTZ NOT NULL DEFAULT NOW()
)
)sql";

constexpr std::string_view media_rendition_ddl = R"sql(
CREATE TABLE IF NOT EXISTS media_rendition (
    id                  TEXT PRIMARY KEY,
    track_id            TEXT NOT NULL REFERENCES track(id) ON DELETE CASCADE,
    codec               SMALLINT NOT NULL,
    container           SMALLINT NOT NULL,
    mime_type           TEXT NOT NULL DEFAULT '',
    bitrate_bps         BIGINT NOT NULL DEFAULT 0,
    sample_rate_hz      BIGINT NOT NULL DEFAULT 0,
    bit_depth           SMALLINT NOT NULL DEFAULT 0,
    channels            SMALLINT NOT NULL DEFAULT 0,
    duration_ms         BIGINT NOT NULL DEFAULT 0,
    size_bytes          BIGINT NOT NULL DEFAULT 0,
    checksum            TEXT NOT NULL DEFAULT '',
    storage_path        TEXT NOT NULL,
    dlna_profile_name   TEXT NOT NULL DEFAULT '',
    protocol_info       TEXT NOT NULL DEFAULT '',
    purpose             SMALLINT NOT NULL DEFAULT 0,
    created_at          TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at          TIMESTAMPTZ NOT NULL DEFAULT NOW()
)
)sql";

constexpr std::string_view storage_asset_ddl = R"sql(
CREATE TABLE IF NOT EXISTS storage_asset (
    id            TEXT PRIMARY KEY,
    storage_path  TEXT NOT NULL,
    mime_type     TEXT NOT NULL DEFAULT 'application/octet-stream',
    created_at    TIMESTAMPTZ NOT NULL DEFAULT NOW()
)
)sql";

constexpr std::string_view playlist_ddl = R"sql(
CREATE TABLE IF NOT EXISTS playlist (
    id          TEXT PRIMARY KEY,
    title       TEXT NOT NULL,
    created_at  TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at  TIMESTAMPTZ NOT NULL DEFAULT NOW()
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
    update_counter BIGINT NOT NULL DEFAULT 0
)
)sql";

constexpr std::string_view system_state_seed = R"sql(
INSERT INTO system_state (key, update_counter)
VALUES ('content_directory', 0)
ON CONFLICT (key) DO NOTHING
)sql";

// Indexes — applied after tables.
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

// ---------------------------------------------------------------------------
// Schema versioning. The baseline above (all_statements) *is* version 1.
//
// To evolve the schema:
//   1. append a Migration entry below with the DDL that upgrades
//      version N to N+1 (never edit all_statements or older migrations),
//   2. bump current_version to N+1.
//
// ensure_schema() records the applied version in `schema_version`, applies
// any pending migrations in order, and refuses to run against a database
// whose recorded version is newer than this binary.
// ---------------------------------------------------------------------------

constexpr std::string_view schema_version_ddl = R"sql(
CREATE TABLE IF NOT EXISTS schema_version (
    version     INTEGER PRIMARY KEY,
    applied_at  TIMESTAMPTZ NOT NULL DEFAULT NOW()
)
)sql";

constexpr int current_version = 1;

struct Migration {
    int to_version;
    std::string_view statement;
};

// Ordered list of upgrade steps beyond the baseline. Empty today; the first
// real migration will look like:
//   { 2, "ALTER TABLE track ADD COLUMN IF NOT EXISTS lyrics TEXT" },
constexpr std::array<Migration, 0> migrations = {};

} // namespace sonarium::catalog::postgres_schema
