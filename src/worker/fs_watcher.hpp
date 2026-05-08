#pragma once

#include <atomic>
#include <chrono>
#include <expected>
#include <filesystem>
#include <memory>
#include <string>

namespace sonarium::worker {

// Interface to "block until the filesystem under <root> changes". Used by
// `sonarium-worker --watch` to avoid the polling-rescan cycle when the host
// kernel can give us push-based events.
class FsWatcher {
public:
    FsWatcher() = default;
    virtual ~FsWatcher() = default;
    FsWatcher(FsWatcher const&) = delete;
    FsWatcher& operator=(FsWatcher const&) = delete;
    FsWatcher(FsWatcher&&) = delete;
    FsWatcher& operator=(FsWatcher&&) = delete;

    // Block until any of: a change is observed under the watch root, `stop`
    // becomes true, or `timeout` elapses. Returns true on a change, false
    // otherwise. Implementations must be cancellable within a few hundred
    // milliseconds of `stop` flipping.
    [[nodiscard]] virtual bool wait_for_change(std::chrono::milliseconds timeout,
                                               std::atomic_bool const& stop) = 0;

    // Human-readable name of the backend (e.g. "inotify", "fsevents",
    // "polling"). Surfaced in startup logs so operators know what they got.
    [[nodiscard]] virtual std::string_view backend_name() const noexcept = 0;
};

// Construct a native FS watcher for `root`. Returns an error string when no
// native backend compiled in or when initialisation fails (e.g. inotify_init
// EMFILE). Callers should fall back to a polling watcher when this fails.
[[nodiscard]] std::expected<std::unique_ptr<FsWatcher>, std::string>
make_native_fs_watcher(std::filesystem::path const& root);

// A polling watcher that sleeps for `poll_interval` and unconditionally
// reports a change (the worker re-scans regardless). Always available; used
// as the fallback when the native backend can't be opened.
[[nodiscard]] std::unique_ptr<FsWatcher>
make_polling_fs_watcher(std::chrono::seconds poll_interval);

} // namespace sonarium::worker
