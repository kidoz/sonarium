#include <poll.h>
#include <sys/inotify.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include "worker/fs_watcher.hpp"

namespace sonarium::worker {

namespace {

constexpr std::uint32_t watch_mask = IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO | IN_MODIFY
                                     | IN_CLOSE_WRITE | IN_DELETE_SELF | IN_MOVE_SELF;

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

class InotifyWatcher final : public FsWatcher {
public:
    // `watches` maps watch descriptor -> directory path so that IN_CREATE
    // events can be resolved to full paths (and new subdirectories watched).
    InotifyWatcher(int fd, std::unordered_map<int, std::filesystem::path> watches)
        : fd_{fd}, watches_{std::move(watches)} {}

    InotifyWatcher(InotifyWatcher const&) = delete;
    InotifyWatcher& operator=(InotifyWatcher const&) = delete;
    InotifyWatcher(InotifyWatcher&&) = delete;
    InotifyWatcher& operator=(InotifyWatcher&&) = delete;

    ~InotifyWatcher() override {
        if (fd_ >= 0) {
            for (auto const& [wd, path] : watches_) {
                ::inotify_rm_watch(fd_, wd);
            }
            ::close(fd_);
        }
    }

    bool wait_for_change(std::chrono::milliseconds timeout, std::atomic_bool const& stop) override {
        constexpr int slice_ms = 200;
        std::chrono::milliseconds elapsed{0};
        while (elapsed < timeout) {
            if (stop.load(std::memory_order_relaxed)) {
                return false;
            }
            pollfd pfd{.fd = fd_, .events = POLLIN, .revents = 0};
            int const remaining =
                std::min<int>(slice_ms, static_cast<int>((timeout - elapsed).count()));
            int const rc = ::poll(&pfd, 1, remaining);
            if (rc < 0) {
                if (errno == EINTR) {
                    elapsed += std::chrono::milliseconds{slice_ms};
                    continue;
                }
                return false;
            }
            if (rc > 0 && (pfd.revents & POLLIN) != 0) {
                drain_events();
                return true;
            }
            elapsed += std::chrono::milliseconds{remaining};
        }
        return false;
    }

    [[nodiscard]] std::string_view backend_name() const noexcept override { return "inotify"; }

private:
    void drain_events() {
        // Read until the queue is empty so we don't get woken up immediately
        // on the next poll for events we already accounted for. While
        // draining, watch any directory created (or moved in) after startup —
        // inotify watches are not recursive, so without this a fresh
        // `<artist>/<album>/` tree would be invisible until an already-watched
        // directory changed.
        alignas(inotify_event) std::array<char, 8192> buf{};
        while (true) {
            auto const n = ::read(fd_, buf.data(), buf.size());
            if (n <= 0) {
                return;
            }
            std::size_t offset = 0;
            while (offset + sizeof(inotify_event) <= static_cast<std::size_t>(n)) {
                auto const* event = reinterpret_cast<inotify_event const*>(buf.data() + offset);
                if ((event->mask & IN_ISDIR) != 0 && (event->mask & (IN_CREATE | IN_MOVED_TO)) != 0
                    && event->len > 0) {
                    if (auto const it = watches_.find(event->wd); it != watches_.end()) {
                        // A moved-in directory may already contain subdirs.
                        watch_tree(it->second / event->name);
                    }
                }
                offset += sizeof(inotify_event) + event->len;
            }
        }
    }

    void watch_tree(std::filesystem::path const& dir) {
        add_watch(dir);
        std::error_code ec;
        for (auto it =
                 std::filesystem::recursive_directory_iterator{
                     dir, std::filesystem::directory_options::skip_permission_denied, ec};
             !ec && it != std::filesystem::recursive_directory_iterator{};
             it.increment(ec)) {
            if (it->is_directory(ec)) {
                add_watch(it->path());
            }
        }
    }

    void add_watch(std::filesystem::path const& dir) {
        int const wd = ::inotify_add_watch(fd_, dir.c_str(), watch_mask);
        if (wd >= 0) {
            watches_[wd] = dir;
        }
    }

    int fd_;
    std::unordered_map<int, std::filesystem::path> watches_;
};

} // namespace

std::expected<std::unique_ptr<FsWatcher>, std::string>
make_native_fs_watcher(std::filesystem::path const& root) {
    int const fd = ::inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (fd < 0) {
        return std::unexpected("inotify_init1: " + strerror_safe(errno));
    }

    std::unordered_map<int, std::filesystem::path> watches;
    std::error_code ec;
    auto add = [&](std::filesystem::path const& dir) {
        int const wd = ::inotify_add_watch(fd, dir.c_str(), watch_mask);
        if (wd >= 0) {
            watches[wd] = dir;
        }
    };
    add(root);
    for (auto it =
             std::filesystem::recursive_directory_iterator{
                 root, std::filesystem::directory_options::skip_permission_denied, ec};
         !ec && it != std::filesystem::recursive_directory_iterator{};
         it.increment(ec)) {
        if (it->is_directory(ec)) {
            add(it->path());
        }
    }
    if (watches.empty()) {
        ::close(fd);
        return std::unexpected("inotify: no watches added (root readable?)");
    }
    return std::make_unique<InotifyWatcher>(fd, std::move(watches));
}

} // namespace sonarium::worker
