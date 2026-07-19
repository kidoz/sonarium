#include <CoreFoundation/CoreFoundation.h>
#include <CoreServices/CoreServices.h>
#include <dispatch/dispatch.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <string>

#include "worker/fs_watcher.hpp"

namespace sonarium::worker {

namespace {

class FsEventsWatcher final : public FsWatcher {
public:
    FsEventsWatcher() = default;

    FsEventsWatcher(FsEventsWatcher const&) = delete;
    FsEventsWatcher& operator=(FsEventsWatcher const&) = delete;
    FsEventsWatcher(FsEventsWatcher&&) = delete;
    FsEventsWatcher& operator=(FsEventsWatcher&&) = delete;

    ~FsEventsWatcher() override {
        if (stream_ != nullptr) {
            FSEventStreamStop(stream_);
            FSEventStreamSetDispatchQueue(stream_, nullptr);
            FSEventStreamInvalidate(stream_);
            FSEventStreamRelease(stream_);
        }
        if (queue_ != nullptr) {
            dispatch_release(queue_);
        }
    }

    // Two-phase init: caller constructs the watcher, hands `&watcher` to
    // FSEventStreamCreate as the callback context, then transfers ownership
    // of the started stream + queue here. The pointer the callback closes
    // over remains stable for the watcher's lifetime.
    void adopt(FSEventStreamRef stream, dispatch_queue_t queue) noexcept {
        stream_ = stream;
        queue_ = queue;
    }

    bool wait_for_change(std::chrono::milliseconds timeout, std::atomic_bool const& stop) override {
        std::unique_lock lock{change_mutex_};
        constexpr std::chrono::milliseconds slice{200};
        std::chrono::milliseconds elapsed{0};
        while (elapsed < timeout) {
            if (stop.load(std::memory_order_relaxed)) {
                return false;
            }
            if (changed_) {
                changed_ = false;
                return true;
            }
            auto const wait = std::min(slice, timeout - elapsed);
            change_cv_.wait_for(lock, wait, [this] { return changed_; });
            if (changed_) {
                changed_ = false;
                return true;
            }
            elapsed += wait;
        }
        return false;
    }

    [[nodiscard]] std::string_view backend_name() const noexcept override { return "fsevents"; }

    void notify_changed() {
        {
            std::scoped_lock const lock{change_mutex_};
            changed_ = true;
        }
        change_cv_.notify_all();
    }

private:
    FSEventStreamRef stream_{nullptr};
    dispatch_queue_t queue_{nullptr};
    std::mutex change_mutex_;
    std::condition_variable change_cv_;
    bool changed_{false};
};

void event_callback(ConstFSEventStreamRef /*stream*/,
                    void* client_info,
                    std::size_t /*num_events*/,
                    void* /*event_paths*/,
                    FSEventStreamEventFlags const* /*event_flags*/,
                    FSEventStreamEventId const* /*event_ids*/) {
    auto* self = static_cast<FsEventsWatcher*>(client_info);
    self->notify_changed();
}

} // namespace

std::expected<std::unique_ptr<FsWatcher>, std::string>
make_native_fs_watcher(std::filesystem::path const& root) {
    std::error_code ec;
    if (!std::filesystem::is_directory(root, ec)) {
        return std::unexpected("fsevents: root is not a directory: " + root.string());
    }

    auto watcher = std::make_unique<FsEventsWatcher>();
    auto const path_str = root.string();
    CFStringRef path_ref =
        CFStringCreateWithCString(nullptr, path_str.c_str(), kCFStringEncodingUTF8);
    CFArrayRef paths_to_watch =
        CFArrayCreate(nullptr, reinterpret_cast<void const**>(&path_ref), 1, nullptr);

    FSEventStreamContext ctx{};
    ctx.info = watcher.get();

    FSEventStreamRef stream =
        FSEventStreamCreate(nullptr,
                            &event_callback,
                            &ctx,
                            paths_to_watch,
                            kFSEventStreamEventIdSinceNow,
                            /*latency=*/0.5,
                            kFSEventStreamCreateFlagFileEvents | kFSEventStreamCreateFlagNoDefer);

    CFRelease(paths_to_watch);
    CFRelease(path_ref);

    if (stream == nullptr) {
        return std::unexpected("FSEventStreamCreate returned null");
    }

    dispatch_queue_t queue = dispatch_queue_create("sonarium.fsevents", DISPATCH_QUEUE_SERIAL);
    FSEventStreamSetDispatchQueue(stream, queue);
    if (FSEventStreamStart(stream) == 0u) {
        FSEventStreamSetDispatchQueue(stream, nullptr);
        FSEventStreamInvalidate(stream);
        FSEventStreamRelease(stream);
        dispatch_release(queue);
        return std::unexpected("FSEventStreamStart failed");
    }

    watcher->adopt(stream, queue);
    return watcher;
}

} // namespace sonarium::worker
