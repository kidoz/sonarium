#include "hls/segmenter.hpp"

#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace sonarium::hls {

namespace {

[[nodiscard]] std::string strerror_safe(int err) {
    std::array<char, 256> buf{};
#ifdef __GLIBC__
    auto const* msg = ::strerror_r(err, buf.data(), buf.size());
    return std::string{msg};
#else
    if (::strerror_r(err, buf.data(), buf.size()) == 0) {
        return std::string{buf.data()};
    }
    return std::string{"errno="} + std::to_string(err);
#endif
}

[[nodiscard]] bool is_segment_filename(std::string_view name) noexcept {
    constexpr std::string_view prefix = "seg";
    constexpr std::string_view suffix = ".ts";
    constexpr std::size_t digits = 5;
    if (name.size() != prefix.size() + digits + suffix.size()) {
        return false;
    }
    if (name.substr(0, prefix.size()) != prefix) {
        return false;
    }
    if (name.substr(prefix.size() + digits) != suffix) {
        return false;
    }
    for (std::size_t i = prefix.size(); i < prefix.size() + digits; ++i) {
        if (name[i] < '0' || name[i] > '9') {
            return false;
        }
    }
    return true;
}

// Validate that `rendition_id` is safe to use as a directory name. The
// catalog already constrains ids to slug-like values but this is defence in
// depth for the security boundary.
[[nodiscard]] bool is_safe_rendition_id(std::string_view id) noexcept {
    if (id.empty() || id.size() > 256) {
        return false;
    }
    if (id.ends_with(".part")) {
        return false; // reserved for in-progress segmenter output dirs
    }
    for (char const c : id) {
        bool const ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
                        || c == '-' || c == '_' || c == ':' || c == '.';
        if (!ok) {
            return false;
        }
        if (c == '.' && (id.find("..") != std::string_view::npos)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::expected<int, std::string> spawn_and_wait(std::vector<std::string> const& argv) {
    std::vector<char const*> argv_c;
    argv_c.reserve(argv.size() + 1);
    for (auto const& s : argv) {
        argv_c.push_back(s.c_str());
    }
    argv_c.push_back(nullptr);

    pid_t const pid = ::fork();
    if (pid < 0) {
        return std::unexpected("fork: " + strerror_safe(errno));
    }
    if (pid == 0) {
        if (int const devnull = ::open("/dev/null", O_RDWR); devnull >= 0) {
            ::dup2(devnull, STDIN_FILENO);
            ::dup2(devnull, STDOUT_FILENO);
            ::dup2(devnull, STDERR_FILENO);
            ::close(devnull);
        }
        ::execvp(argv_c[0], const_cast<char* const*>(argv_c.data()));
        ::_exit(127);
    }

    int status = 0;
    while (::waitpid(pid, &status, 0) < 0) {
        if (errno == EINTR) {
            continue;
        }
        return std::unexpected("waitpid: " + strerror_safe(errno));
    }
    if (!WIFEXITED(status)) {
        return std::unexpected("ffmpeg killed by signal");
    }
    return WEXITSTATUS(status);
}

// A cached playlist is only complete once ffmpeg has appended the VOD
// terminator. A truncated index.m3u8 (ffmpeg killed mid-transcode, disk full)
// must read as absent, or the fast path would serve it forever.
[[nodiscard]] bool playlist_is_complete(std::filesystem::path const& playlist) {
    std::ifstream in{playlist, std::ios::binary};
    if (!in) {
        return false;
    }
    std::string const contents{std::istreambuf_iterator<char>{in},
                               std::istreambuf_iterator<char>{}};
    return contents.find("#EXT-X-ENDLIST") != std::string::npos;
}

} // namespace

bool is_safe_cache_filename(std::string_view name) noexcept {
    if (name == "index.m3u8") {
        return true;
    }
    return is_segment_filename(name);
}

std::vector<std::string> build_segmenter_argv(std::filesystem::path const& source,
                                              std::filesystem::path const& output_dir,
                                              SegmenterConfig const& cfg,
                                              std::uint32_t bitrate_kbps_override) {
    auto const effective_bitrate =
        (bitrate_kbps_override > 0) ? bitrate_kbps_override : cfg.bitrate_kbps;
    auto const playlist = output_dir / "index.m3u8";
    auto const seg_pattern = output_dir / "seg%05d.ts";
    return {
        "ffmpeg",
        "-hide_banner",
        "-nostdin",
        "-loglevel",
        "error",
        "-y",
        "-i",
        source.string(),
        "-vn",
        "-c:a",
        "aac",
        "-b:a",
        std::to_string(effective_bitrate) + "k",
        "-f",
        "hls",
        "-hls_time",
        std::to_string(cfg.segment_duration_seconds),
        "-hls_list_size",
        "0",
        "-hls_playlist_type",
        "vod",
        "-hls_flags",
        "independent_segments",
        "-hls_segment_filename",
        seg_pattern.string(),
        playlist.string(),
    };
}

Segmenter::Segmenter(SegmenterConfig config) : config_{std::move(config)} {}

std::filesystem::path Segmenter::cache_dir(std::string const& rendition_id) const {
    return config_.cache_root / rendition_id;
}

std::shared_ptr<std::mutex> Segmenter::mutex_for(std::string const& rendition_id) {
    std::scoped_lock const lock{registry_mutex_};
    auto it = per_rendition_mutexes_.find(rendition_id);
    if (it == per_rendition_mutexes_.end()) {
        it = per_rendition_mutexes_.emplace(rendition_id, std::make_shared<std::mutex>()).first;
    }
    return it->second;
}

std::expected<std::filesystem::path, std::string>
Segmenter::ensure_segments(std::string const& rendition_id,
                           std::filesystem::path const& source_path,
                           std::uint32_t bitrate_kbps_override) {
    if (!is_safe_rendition_id(rendition_id)) {
        return std::unexpected("invalid rendition id: " + rendition_id);
    }

    auto const dir = cache_dir(rendition_id);
    auto const playlist = dir / "index.m3u8";

    // Fast path: already segmented. No lock needed for the read — worst case
    // we double-check under the lock below. The ENDLIST check guards against
    // caches written by pre-atomic versions or a crash between rename steps.
    std::error_code ec;
    if (std::filesystem::exists(playlist, ec) && playlist_is_complete(playlist)) {
        return playlist;
    }

    auto const lock_handle = mutex_for(rendition_id);
    std::scoped_lock const lock{*lock_handle};

    // Re-check under lock (another thread may have raced ahead).
    if (std::filesystem::exists(playlist, ec)) {
        if (playlist_is_complete(playlist)) {
            return playlist;
        }
        // Truncated leftover — rebuild from scratch.
        std::filesystem::remove_all(dir, ec);
    }

    if (!std::filesystem::exists(source_path, ec)) {
        return std::unexpected("source not found: " + source_path.string());
    }

    // Segment into a sibling .part dir and rename into place on success, so a
    // failed or killed ffmpeg can never leave a half-written cache behind.
    // Segment filenames inside index.m3u8 are relative, so the rename keeps
    // the playlist valid.
    auto const part_dir = std::filesystem::path{dir.string() + ".part"};
    std::filesystem::remove_all(part_dir, ec);
    std::filesystem::create_directories(part_dir, ec);
    if (ec) {
        return std::unexpected("create_directories: " + ec.message());
    }

    auto const fail = [&part_dir](std::string message) {
        std::error_code cleanup_ec;
        std::filesystem::remove_all(part_dir, cleanup_ec);
        return std::unexpected(std::move(message));
    };

    auto const argv = build_segmenter_argv(source_path, part_dir, config_, bitrate_kbps_override);
    auto status = spawn_and_wait(argv);
    if (!status.has_value()) {
        return fail(status.error());
    }
    if (*status != 0) {
        return fail("ffmpeg exited " + std::to_string(*status));
    }
    if (!playlist_is_complete(part_dir / "index.m3u8")) {
        return fail("ffmpeg succeeded but playlist is missing or truncated: "
                    + (part_dir / "index.m3u8").string());
    }

    std::filesystem::remove_all(dir, ec);
    std::filesystem::rename(part_dir, dir, ec);
    if (ec) {
        return fail("rename cache dir: " + ec.message());
    }
    return playlist;
}

std::optional<std::filesystem::path> Segmenter::cached_file(std::string const& rendition_id,
                                                            std::string_view filename) const {
    if (!is_safe_rendition_id(rendition_id) || !is_safe_cache_filename(filename)) {
        return std::nullopt;
    }
    auto const path = cache_dir(rendition_id) / std::string{filename};
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec)) {
        return std::nullopt;
    }
    return path;
}

} // namespace sonarium::hls
