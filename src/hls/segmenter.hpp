#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <semaphore>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace sonarium::hls {

struct SegmenterConfig {
    std::filesystem::path cache_root;
    std::uint32_t segment_duration_seconds = 6;
    std::uint32_t bitrate_kbps = 192;
    // Total on-disk cache budget. When a fresh segmentation pushes the cache
    // over this, least-recently-used rendition dirs are evicted. 0 = unbounded.
    std::uint64_t max_cache_bytes = std::uint64_t{8} * 1024 * 1024 * 1024;
    // Upper bound on ffmpeg processes running at once (clamped to [1, 64]).
    // Requests beyond the bound fail fast with `busy` instead of queueing on
    // the HTTP worker pool.
    std::uint32_t max_concurrent_transcodes = 2;
};

// Why ensure_segments failed. `busy` means "retry shortly" (another request
// is segmenting this rendition, or all transcode slots are taken) and maps to
// HTTP 503 + Retry-After; `failed` is a real error and maps to 500.
struct SegmenterError {
    enum class Kind : std::uint8_t { busy, failed };
    Kind kind = Kind::failed;
    std::string message;
};

// Whether `name` is a safe per-rendition cache filename: either the literal
// "index.m3u8" playlist or a "seg00000.ts" segment with five decimal digits.
// Reject anything that could traverse the cache directory (`..`, absolute
// paths, non-printable chars).
[[nodiscard]] bool is_safe_cache_filename(std::string_view name) noexcept;

// Build the argv for ffmpeg's HLS muxer. Output goes to `<output_dir>` with
// segments named seg00000.ts and a sibling index.m3u8. `bitrate_kbps_override`
// of 0 means "use cfg.bitrate_kbps".
[[nodiscard]] std::vector<std::string>
build_segmenter_argv(std::filesystem::path const& source,
                     std::filesystem::path const& output_dir,
                     SegmenterConfig const& cfg,
                     std::uint32_t bitrate_kbps_override = 0);

class Segmenter {
public:
    explicit Segmenter(SegmenterConfig config);

    // Ensure a per-rendition cache dir is populated (playlist + segments).
    // Idempotent — once a complete `index.m3u8` is on disk for that rendition,
    // subsequent calls fast-return without spawning ffmpeg.
    //
    // Never blocks on another request's transcode: a concurrent request for
    // the same rendition, or one arriving while every transcode slot is in
    // use, returns SegmenterError::Kind::busy immediately. On success the
    // cache is swept back under `max_cache_bytes` (LRU by playlist mtime).
    [[nodiscard]] std::expected<std::filesystem::path, SegmenterError>
    ensure_segments(std::string const& rendition_id,
                    std::filesystem::path const& source_path,
                    std::uint32_t bitrate_kbps_override = 0);

    // Sweep the cache back under `max_cache_bytes`: stale `.part` leftovers
    // are removed, then least-recently-used rendition dirs are evicted until
    // the total fits (skipping `keep_rendition_id` and anything currently
    // being segmented). No-op when max_cache_bytes is 0. Called automatically
    // after each successful segmentation; public so operators/tests can force
    // a sweep.
    void evict_over_cap(std::string_view keep_rendition_id = {});

    // Resolve a filename (e.g. "seg00001.ts" / "index.m3u8") within a
    // rendition's cache dir. Returns nullopt for unsafe names or names that
    // don't actually exist on disk.
    [[nodiscard]] std::optional<std::filesystem::path> cached_file(std::string const& rendition_id,
                                                                   std::string_view filename) const;

    [[nodiscard]] SegmenterConfig const& config() const noexcept { return config_; }

    [[nodiscard]] std::filesystem::path cache_dir(std::string const& rendition_id) const;

private:
    [[nodiscard]] std::shared_ptr<std::mutex> mutex_for(std::string const& rendition_id);
    void forget_mutex(std::string const& rendition_id);

    SegmenterConfig config_;
    std::counting_semaphore<> transcode_slots_;
    mutable std::mutex registry_mutex_;
    std::unordered_map<std::string, std::shared_ptr<std::mutex>> per_rendition_mutexes_;
};

} // namespace sonarium::hls
