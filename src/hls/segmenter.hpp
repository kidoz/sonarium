#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace sonarium::hls {

struct SegmenterConfig {
    std::filesystem::path cache_root;
    std::uint32_t segment_duration_seconds = 6;
    std::uint32_t bitrate_kbps = 192;
};

// Whether `name` is a safe per-rendition cache filename: either the literal
// "index.m3u8" playlist or a "seg00000.ts" segment with five decimal digits.
// Reject anything that could traverse the cache directory (`..`, absolute
// paths, non-printable chars).
[[nodiscard]] bool is_safe_cache_filename(std::string_view name) noexcept;

// Build the argv for ffmpeg's HLS muxer. Output goes to `<output_dir>` with
// segments named seg00000.ts and a sibling index.m3u8.
[[nodiscard]] std::vector<std::string> build_segmenter_argv(std::filesystem::path const& source,
                                                            std::filesystem::path const& output_dir,
                                                            SegmenterConfig const& cfg);

class Segmenter {
public:
    explicit Segmenter(SegmenterConfig config);

    // Ensure a per-rendition cache dir is populated (playlist + segments).
    // Idempotent — once `index.m3u8` is on disk for that rendition,
    // subsequent calls fast-return without spawning ffmpeg. Per-rendition
    // mutex serializes concurrent first-time requests so we don't fork
    // duplicate segmenters.
    [[nodiscard]] std::expected<std::filesystem::path, std::string>
    ensure_segments(std::string const& rendition_id, std::filesystem::path const& source_path);

    // Resolve a filename (e.g. "seg00001.ts" / "index.m3u8") within a
    // rendition's cache dir. Returns nullopt for unsafe names or names that
    // don't actually exist on disk.
    [[nodiscard]] std::optional<std::filesystem::path> cached_file(std::string const& rendition_id,
                                                                   std::string_view filename) const;

    [[nodiscard]] SegmenterConfig const& config() const noexcept { return config_; }

    [[nodiscard]] std::filesystem::path cache_dir(std::string const& rendition_id) const;

private:
    [[nodiscard]] std::shared_ptr<std::mutex> mutex_for(std::string const& rendition_id);

    SegmenterConfig config_;
    mutable std::mutex registry_mutex_;
    std::unordered_map<std::string, std::shared_ptr<std::mutex>> per_rendition_mutexes_;
};

} // namespace sonarium::hls
