#pragma once

#include <expected>
#include <string>

#include "catalog/album.hpp"
#include "catalog/artist.hpp"
#include "catalog/storage_asset.hpp"
#include "catalog/track.hpp"
#include "media/media_rendition.hpp"

namespace sonarium::catalog {

// Mutating side of the catalog. Read access lives on `Repository`; ingestion
// (the worker, `sonariumctl import`) depends only on this interface so it can
// upsert into either the in-memory or the Postgres-backed implementation
// without knowing which.
//
// Each upsert is a single statement (`INSERT ... ON CONFLICT (id) DO UPDATE`
// in Postgres; insertion-or-replace in InMemory). Failures bubble up as
// `std::unexpected<std::string>` so callers can decide whether to skip the
// row or abort the import.
class CatalogWriter {
public:
    CatalogWriter() = default;
    virtual ~CatalogWriter() = default;

    [[nodiscard]] virtual std::expected<void, std::string> upsert_artist(Artist const& a) = 0;
    [[nodiscard]] virtual std::expected<void, std::string> upsert_album(Album const& al) = 0;
    [[nodiscard]] virtual std::expected<void, std::string> upsert_track(Track const& t) = 0;
    [[nodiscard]] virtual std::expected<void, std::string>
    upsert_rendition(sonarium::media::MediaRendition const& m) = 0;
    [[nodiscard]] virtual std::expected<void, std::string> upsert_asset(StorageAsset const& s) = 0;

    // No [[nodiscard]] — bumping the counter is fire-and-forget for callers
    // that just want clients to re-fetch; the return value is informational.
    virtual std::expected<void, std::string> bump_system_update_id() = 0;

protected:
    CatalogWriter(CatalogWriter const&) = default;
    CatalogWriter& operator=(CatalogWriter const&) = default;
    CatalogWriter(CatalogWriter&&) noexcept = default;
    CatalogWriter& operator=(CatalogWriter&&) noexcept = default;
};

} // namespace sonarium::catalog
