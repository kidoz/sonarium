#pragma once

#include <string>

namespace sonarium::catalog {

// A binary blob in the asset store — album art today, track album-art
// thumbnails / artist images later. Production maps these to an `asset` table
// row; the in-memory repository keeps a flat id -> StorageAsset map.
struct StorageAsset {
    std::string id;
    std::string storage_path; // absolute filesystem path
    std::string mime_type;    // e.g. "image/jpeg"
};

} // namespace sonarium::catalog
