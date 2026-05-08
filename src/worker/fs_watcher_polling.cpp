#include <chrono>
#include <thread>

#include "worker/fs_watcher.hpp"

namespace sonarium::worker {

namespace {

class PollingWatcher final : public FsWatcher {
public:
    explicit PollingWatcher(std::chrono::seconds interval) : interval_{interval} {}

    bool wait_for_change(std::chrono::milliseconds timeout, std::atomic_bool const& stop) override {
        // Sleep in 200 ms slices so SIGINT lands within ~a quarter second of
        // the user pressing Ctrl-C. Treat the lesser of `interval_` and
        // `timeout` as the waiting budget; the worker's outer loop owns the
        // re-scan cadence so we just need to return promptly.
        auto const total_ms =
            std::min(std::chrono::duration_cast<std::chrono::milliseconds>(interval_).count(),
                     timeout.count());
        std::chrono::milliseconds elapsed{0};
        constexpr std::chrono::milliseconds slice{200};
        while (elapsed.count() < total_ms) {
            if (stop.load(std::memory_order_relaxed)) {
                return false;
            }
            std::this_thread::sleep_for(slice);
            elapsed += slice;
        }
        // Polling backends always report "maybe changed" so the worker
        // rescans on every tick — the catalog upserts are idempotent.
        return true;
    }

    [[nodiscard]] std::string_view backend_name() const noexcept override { return "polling"; }

private:
    std::chrono::seconds interval_;
};

} // namespace

std::unique_ptr<FsWatcher> make_polling_fs_watcher(std::chrono::seconds poll_interval) {
    return std::make_unique<PollingWatcher>(poll_interval);
}

} // namespace sonarium::worker
