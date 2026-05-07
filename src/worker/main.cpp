#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>

#include "catalog/postgres_repository.hpp"
#include "core/version.hpp"
#include "scanner/media_scanner.hpp"

namespace {

std::atomic_bool g_stop{false};

extern "C" void handle_signal(int /*signum*/) {
    g_stop.store(true, std::memory_order_relaxed);
}

void install_signal_handlers() {
    std::signal(SIGINT, &handle_signal);
    std::signal(SIGTERM, &handle_signal);
}

[[nodiscard]] std::string env_or(std::string_view name, std::string fallback) {
    if (auto const* v = std::getenv(std::string{name}.c_str()); v != nullptr) {
        return std::string{v};
    }
    return fallback;
}

[[nodiscard]] std::optional<std::string> flag_value(std::span<char* const> args,
                                                    std::string_view flag) noexcept {
    for (std::size_t i = 1; i + 1 < args.size(); ++i) {
        if (std::strcmp(args[i], std::string{flag}.c_str()) == 0) {
            return std::string{args[i + 1]};
        }
    }
    return std::nullopt;
}

[[nodiscard]] bool has_flag(std::span<char* const> args, std::string_view flag) noexcept {
    for (auto const* a : args.subspan(1)) {
        if (std::strcmp(a, std::string{flag}.c_str()) == 0) {
            return true;
        }
    }
    return false;
}

void print_report(sonarium::scanner::ScanReport const& r) {
    std::cout << "  artists=" << r.artists_upserted << " albums=" << r.albums_upserted
              << " tracks=" << r.tracks_upserted << " renditions=" << r.renditions_upserted
              << " covers=" << r.covers_upserted << " skipped=" << r.skipped_files << '\n';
    if (!r.errors.empty()) {
        std::cerr << "  errors (" << r.errors.size() << "):\n";
        for (auto const& e : r.errors) {
            std::cerr << "    - " << e << '\n';
        }
    }
}

// Sleep for `total` seconds in 1-second slices so SIGINT/SIGTERM lands within
// a second of the user pressing Ctrl-C instead of waiting out the full cycle.
void interruptible_sleep_seconds(std::int64_t total) {
    for (std::int64_t i = 0; i < total; ++i) {
        if (g_stop.load(std::memory_order_relaxed)) {
            return;
        }
        std::this_thread::sleep_for(std::chrono::seconds{1});
    }
}

[[nodiscard]] std::int64_t parse_poll_interval(std::span<char* const> args) {
    auto const flag = flag_value(args, "--interval");
    auto const env = std::getenv("SONARIUM_WORKER_POLL_INTERVAL_SECONDS");
    auto const* raw = flag.has_value() ? flag->c_str() : env;
    if (raw == nullptr) {
        return 60;
    }
    auto const parsed = std::strtol(raw, nullptr, 10);
    return (parsed > 0) ? parsed : 60;
}

} // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    install_signal_handlers();

    auto args = std::span<char* const>(argv, static_cast<std::size_t>(argc));
    auto const v = sonarium::core::current_version();
    std::cout << sonarium::core::product_name() << " worker " << v.major << '.' << v.minor << '.'
              << v.patch << '\n';

    auto const root_arg = flag_value(args, "--root");
    auto const root = root_arg.value_or(env_or("SONARIUM_MEDIA_ROOT", ""));
    if (root.empty()) {
        std::cerr << "  no media root supplied (--root <path> or SONARIUM_MEDIA_ROOT)\n";
        return 2;
    }

    auto const conninfo = env_or("SONARIUM_PG_CONNINFO", "");
    if (conninfo.empty()) {
        std::cerr << "  SONARIUM_PG_CONNINFO must be set so the worker can write rows\n";
        return 2;
    }

    auto const watch = has_flag(args, "--watch");
    auto const poll_seconds = parse_poll_interval(args);

    std::cout << "  root=" << root << '\n' << "  backend=postgres\n";
    if (watch) {
        std::cout << "  mode=watch (interval=" << poll_seconds << "s)\n";
    } else {
        std::cout << "  mode=one-shot\n";
    }

    auto repo_result = sonarium::catalog::PostgresRepository::open(conninfo);
    if (!repo_result.has_value()) {
        std::cerr << "  postgres open failed: " << repo_result.error() << '\n';
        return 3;
    }
    auto repo = *repo_result;
    if (auto schema = repo->ensure_schema(); !schema.has_value()) {
        std::cerr << "  ensure_schema failed: " << schema.error() << '\n';
        return 3;
    }

    auto run_pass = [&]() -> sonarium::scanner::ScanReport {
        std::cout << "[scan] start\n";
        auto report = sonarium::scanner::scan(std::filesystem::path{root}, *repo);
        std::cout << "[scan] done\n";
        print_report(report);
        return report;
    };

    auto first = run_pass();
    if (!watch) {
        return first.errors.empty() ? 0 : 1;
    }

    while (!g_stop.load(std::memory_order_relaxed)) {
        interruptible_sleep_seconds(poll_seconds);
        if (g_stop.load(std::memory_order_relaxed)) {
            break;
        }
        run_pass();
    }
    std::cout << "  worker stopped\n";
    return 0;
}
