#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <thread>

#include "worker/fs_watcher.hpp"

namespace {

namespace fs = std::filesystem;

class TempDir {
public:
    TempDir() {
        std::random_device rd;
        std::mt19937_64 gen{rd()};
        path_ = fs::temp_directory_path() / ("sonarium-watcher-" + std::to_string(gen()));
        fs::create_directories(path_);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path_, ec);
    }
    TempDir(TempDir const&) = delete;
    TempDir& operator=(TempDir const&) = delete;
    TempDir(TempDir&&) = delete;
    TempDir& operator=(TempDir&&) = delete;

    [[nodiscard]] fs::path const& path() const noexcept { return path_; }

private:
    fs::path path_;
};

void touch_file(fs::path const& dir, std::string_view name) {
    std::ofstream out{dir / std::string{name}};
    out << "data\n";
}

} // namespace

TEST_CASE("polling watcher reports change after the slice elapses", "[worker][watcher]") {
    auto w = sonarium::worker::make_polling_fs_watcher(std::chrono::seconds{1});
    REQUIRE(w);
    REQUIRE(w->backend_name() == "polling");

    std::atomic_bool stop{false};
    auto const t0 = std::chrono::steady_clock::now();
    bool const changed = w->wait_for_change(std::chrono::seconds{2}, stop);
    auto const elapsed = std::chrono::steady_clock::now() - t0;

    REQUIRE(changed);
    REQUIRE(elapsed >= std::chrono::milliseconds{900}); // ~1 s wait honored
    REQUIRE(elapsed < std::chrono::seconds{3});         // didn't run away
}

TEST_CASE("polling watcher returns false when stop is signalled", "[worker][watcher]") {
    auto w = sonarium::worker::make_polling_fs_watcher(std::chrono::seconds{60});
    REQUIRE(w);

    std::atomic_bool stop{false};
    std::thread setter{[&stop]() {
        std::this_thread::sleep_for(std::chrono::milliseconds{300});
        stop.store(true);
    }};
    auto const t0 = std::chrono::steady_clock::now();
    bool const changed = w->wait_for_change(std::chrono::seconds{60}, stop);
    auto const elapsed = std::chrono::steady_clock::now() - t0;
    setter.join();

    REQUIRE_FALSE(changed);
    REQUIRE(elapsed < std::chrono::seconds{2}); // bailed within ~slice of stop
}

TEST_CASE("native watcher detects a new file", "[worker][watcher][live]") {
    TempDir root;
    auto w_or = sonarium::worker::make_native_fs_watcher(root.path());
    if (!w_or.has_value()) {
        // No native backend on this build — skip rather than fail.
        SUCCEED("native fs watcher unavailable: " << w_or.error());
        return;
    }
    auto w = std::move(*w_or);
    REQUIRE((w->backend_name() == "inotify" || w->backend_name() == "fsevents"));

    std::atomic_bool stop{false};
    std::thread writer{[&root]() {
        // Brief pause so the watcher has a chance to enter wait_for_change
        // before we write — otherwise on macOS FSEvents the historical event
        // would be filtered out by sinceNow.
        std::this_thread::sleep_for(std::chrono::milliseconds{300});
        touch_file(root.path(), "newfile.txt");
    }};
    bool const changed = w->wait_for_change(std::chrono::seconds{4}, stop);
    writer.join();

    REQUIRE(changed);
}
