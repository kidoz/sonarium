#include "scanner/audio_metadata.hpp"

#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace sonarium::scanner {

namespace {

constexpr std::size_t pipe_chunk = 4096;

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

template <typename T>
[[nodiscard]] T parse_u_or_zero(std::string_view s) noexcept {
    T out{};
    auto const* first = s.data();
    auto const* last = s.data() + s.size();
    auto const r = std::from_chars(first, last, out);
    if (r.ec != std::errc{}) {
        return T{};
    }
    return out;
}

[[nodiscard]] std::uint64_t parse_duration_seconds_to_ms(std::string_view s) noexcept {
    char* end_ptr = nullptr;
    auto const seconds = std::strtod(std::string{s}.c_str(), &end_ptr);
    if (end_ptr == s.data() || seconds < 0.0 || !std::isfinite(seconds)) {
        return 0;
    }
    return static_cast<std::uint64_t>(std::llround(seconds * 1000.0));
}

[[nodiscard]] std::pair<std::string_view, std::string_view> split_kv(std::string_view line) {
    auto const eq = line.find('=');
    if (eq == std::string_view::npos) {
        return {line, {}};
    }
    return {line.substr(0, eq), line.substr(eq + 1)};
}

} // namespace

AudioMetadata parse_ffprobe_default_output(std::string_view text) {
    AudioMetadata out;

    enum class Section : std::uint8_t {
        none,
        stream,
        format,
    };
    Section section = Section::none;
    bool stream_is_audio = false;
    bool audio_seen = false;

    auto stream = std::string{};
    auto stream_sample_rate = std::string{};
    auto stream_channels = std::string{};
    auto stream_bits_per_sample = std::string{};
    auto stream_bit_rate = std::string{};

    std::size_t pos = 0;
    while (pos < text.size()) {
        auto const eol = text.find('\n', pos);
        std::string_view const line =
            text.substr(pos, (eol == std::string_view::npos) ? text.size() - pos : eol - pos);
        pos = (eol == std::string_view::npos) ? text.size() : eol + 1;
        if (line.empty()) {
            continue;
        }

        if (line == "[STREAM]") {
            section = Section::stream;
            stream_is_audio = false;
            stream_sample_rate.clear();
            stream_channels.clear();
            stream_bits_per_sample.clear();
            stream_bit_rate.clear();
            continue;
        }
        if (line == "[/STREAM]") {
            if (stream_is_audio && !audio_seen) {
                out.sample_rate_hz = parse_u_or_zero<std::uint32_t>(stream_sample_rate);
                out.channels = static_cast<std::uint8_t>(
                    parse_u_or_zero<std::uint32_t>(stream_channels) & 0xFFU);
                out.bit_depth = static_cast<std::uint8_t>(
                    parse_u_or_zero<std::uint32_t>(stream_bits_per_sample) & 0xFFU);
                if (out.bitrate_bps == 0) {
                    out.bitrate_bps = parse_u_or_zero<std::uint32_t>(stream_bit_rate);
                }
                audio_seen = true;
            }
            section = Section::none;
            continue;
        }
        if (line == "[FORMAT]") {
            section = Section::format;
            continue;
        }
        if (line == "[/FORMAT]") {
            section = Section::none;
            continue;
        }

        auto const [key, value] = split_kv(line);
        if (section == Section::stream) {
            if (key == "codec_type") {
                stream_is_audio = (value == "audio");
            } else if (key == "sample_rate") {
                stream_sample_rate.assign(value);
            } else if (key == "channels") {
                stream_channels.assign(value);
            } else if (key == "bits_per_sample") {
                stream_bits_per_sample.assign(value);
            } else if (key == "bit_rate") {
                stream_bit_rate.assign(value);
            }
        } else if (section == Section::format) {
            if (key == "duration") {
                out.duration_ms = parse_duration_seconds_to_ms(value);
            } else if (key == "bit_rate") {
                out.bitrate_bps = parse_u_or_zero<std::uint32_t>(value);
            } else if (key == "TAG:artist") {
                out.artist = std::string{value};
            } else if (key == "TAG:album") {
                out.album = std::string{value};
            } else if (key == "TAG:title") {
                out.title = std::string{value};
            }
        }
    }

    return out;
}

std::expected<AudioMetadata, std::string> read_audio_metadata(std::filesystem::path const& path) {
    std::array<int, 2> out_pipe{-1, -1};
    if (::pipe(out_pipe.data()) != 0) {
        return std::unexpected("pipe: " + strerror_safe(errno));
    }

    auto const path_str = path.string();
    std::vector<char const*> argv{
        "ffprobe",
        "-v",
        "error",
        "-show_entries",
        "format=duration,bit_rate",
        "-show_entries",
        "stream=codec_type,sample_rate,channels,bits_per_sample,bit_rate",
        "-show_entries",
        "format_tags=artist,album,title",
        "-of",
        "default=noprint_wrappers=0",
        path_str.c_str(),
        nullptr,
    };

    pid_t const pid = ::fork();
    if (pid < 0) {
        ::close(out_pipe[0]);
        ::close(out_pipe[1]);
        return std::unexpected("fork: " + strerror_safe(errno));
    }
    if (pid == 0) {
        ::close(out_pipe[0]);
        ::dup2(out_pipe[1], STDOUT_FILENO);
        if (int const devnull = ::open("/dev/null", O_WRONLY); devnull >= 0) {
            ::dup2(devnull, STDERR_FILENO);
            ::close(devnull);
        }
        ::close(out_pipe[1]);
        ::execvp(argv[0], const_cast<char* const*>(argv.data()));
        ::_exit(127);
    }

    ::close(out_pipe[1]);
    std::string captured;
    std::array<char, pipe_chunk> buf{};
    while (true) {
        auto const n = ::read(out_pipe[0], buf.data(), buf.size());
        if (n > 0) {
            captured.append(buf.data(), static_cast<std::size_t>(n));
            continue;
        }
        if (n == 0) {
            break;
        }
        if (errno == EINTR) {
            continue;
        }
        break;
    }
    ::close(out_pipe[0]);

    int status = 0;
    while (::waitpid(pid, &status, 0) < 0) {
        if (errno == EINTR) {
            continue;
        }
        return std::unexpected("waitpid: " + strerror_safe(errno));
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        int const code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        return std::unexpected("ffprobe exited " + std::to_string(code) + " for " + path_str);
    }

    return parse_ffprobe_default_output(captured);
}

} // namespace sonarium::scanner
