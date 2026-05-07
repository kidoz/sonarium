#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "catalog/catalog_writer.hpp"

namespace sonarium::scanner {

// Counters returned to the operator after a scan. `errors` carries
// human-readable messages for files we tried to upsert but failed (e.g. the
// Postgres connection rejected a row). The scan does not abort on the first
// error — it logs and continues so a single bad file can't poison a multi-
// thousand-track import.
struct ScanReport {
    std::uint32_t artists_upserted = 0;
    std::uint32_t albums_upserted = 0;
    std::uint32_t tracks_upserted = 0;
    std::uint32_t renditions_upserted = 0;
    std::uint32_t covers_upserted = 0;
    std::uint32_t skipped_files = 0;
    std::vector<std::string> errors;
};

// Walk `root` recursively and upsert every file matching the
//   <root>/<artist>/<album>/[NN - ]<title>.<ext>
// convention with a known audio extension (mp3, flac, m4a, wav). For each
// album directory that contains a `cover.<jpg|jpeg|png>` or `folder.<...>`
// the cover is upserted as a StorageAsset and linked to the Album.
//
// Calls `writer.bump_system_update_id()` exactly once at the end so DLNA
// renderers re-fetch.
[[nodiscard]] ScanReport scan(std::filesystem::path const& root,
                              sonarium::catalog::CatalogWriter& writer);

} // namespace sonarium::scanner
