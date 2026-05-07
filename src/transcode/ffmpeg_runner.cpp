#include "transcode/ffmpeg_runner.hpp"

#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "transcode/ffmpeg_command.hpp"

namespace sonarium::transcode {

namespace {

constexpr std::size_t stderr_tail_max = 4096;
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

// Append `chunk` to `tail`, keeping at most `max` trailing bytes — useful for
// surfacing the end of ffmpeg's stderr without unbounded buffering.
void append_tail(std::string& tail, std::string_view chunk, std::size_t max) {
    tail.append(chunk);
    if (tail.size() > max) {
        tail.erase(0, tail.size() - max);
    }
}

} // namespace

std::expected<TranscodeResult, std::string> run_ffmpeg(TranscodeRequest const& req) {
    auto const argv_strs = build_ffmpeg_argv(req);
    std::vector<char*> argv;
    argv.reserve(argv_strs.size() + 1);
    for (auto const& s : argv_strs) {
        argv.push_back(const_cast<char*>(s.c_str()));
    }
    argv.push_back(nullptr);

    std::array<int, 2> err_pipe{-1, -1};
    if (::pipe(err_pipe.data()) != 0) {
        return std::unexpected("pipe: " + strerror_safe(errno));
    }

    pid_t const pid = ::fork();
    if (pid < 0) {
        ::close(err_pipe[0]);
        ::close(err_pipe[1]);
        return std::unexpected("fork: " + strerror_safe(errno));
    }
    if (pid == 0) {
        // Child: silence stdout, redirect stderr into the pipe, exec ffmpeg.
        ::close(err_pipe[0]);
        ::dup2(err_pipe[1], STDERR_FILENO);
        if (int const devnull = ::open("/dev/null", O_WRONLY); devnull >= 0) {
            ::dup2(devnull, STDOUT_FILENO);
            ::close(devnull);
        }
        ::close(err_pipe[1]);
        ::execvp(argv[0], argv.data());
        // execvp returns only on failure.
        std::string const msg = "execvp ffmpeg: " + strerror_safe(errno) + "\n";
        ::write(STDERR_FILENO, msg.data(), msg.size());
        ::_exit(127);
    }

    // Parent: drain stderr until the pipe closes.
    ::close(err_pipe[1]);
    std::string tail;
    std::array<char, pipe_chunk> buf{};
    while (true) {
        auto const n = ::read(err_pipe[0], buf.data(), buf.size());
        if (n > 0) {
            append_tail(
                tail, std::string_view{buf.data(), static_cast<std::size_t>(n)}, stderr_tail_max);
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
    ::close(err_pipe[0]);

    int status = 0;
    while (::waitpid(pid, &status, 0) < 0) {
        if (errno == EINTR) {
            continue;
        }
        return std::unexpected("waitpid: " + strerror_safe(errno));
    }

    TranscodeResult result;
    result.stderr_excerpt = std::move(tail);
    if (WIFEXITED(status)) {
        result.exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        result.exit_code = 128 + WTERMSIG(status);
    } else {
        result.exit_code = -1;
    }
    return result;
}

} // namespace sonarium::transcode
