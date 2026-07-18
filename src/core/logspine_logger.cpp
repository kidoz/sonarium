#include "core/logspine_logger.hpp"

#include <cctype>
#include <cstddef>
#include <logspine/dispatcher.hpp>
#include <logspine/sink.hpp>
#include <logspine/sinks/console_sink.hpp>
#include <logspine/sync_dispatcher.hpp>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace sonarium::core {

LogSpineLogger::LogSpineLogger(std::shared_ptr<::logspine::logger> log) noexcept
    : log_{std::move(log)} {}

void LogSpineLogger::log(::logspine::level severity,
                         std::string_view message,
                         std::vector<::logspine::field> fields) {
    if (!log_) {
        return;
    }
    log_->log(severity, message, std::move(fields));
    // Flush per call so structured events are visible in real time even when
    // stdout is piped or the process is killed before clean shutdown.
    log_->flush();
}

void LogSpineLogger::flush() {
    if (log_) {
        log_->flush();
    }
}

ConsoleLoggerHandle build_console_logger(std::string_view component_name,
                                         ::logspine::level threshold) {
    using ::logspine::sink_format;
    using ::logspine::sinks::console_sink;
    using ::logspine::sinks::console_sink_options;

    auto console =
        std::make_shared<console_sink>(console_sink_options{.format = sink_format::human});
    auto dispatcher = std::make_shared<::logspine::sync_dispatcher>(
        std::vector<std::shared_ptr<::logspine::sink>>{console});
    auto registry = std::make_shared<::logspine::logger_registry>(dispatcher, threshold);
    auto inner = registry->get(std::string(component_name));
    auto logger =
        std::static_pointer_cast<Logger>(std::make_shared<LogSpineLogger>(std::move(inner)));
    return ConsoleLoggerHandle{std::move(registry), std::move(logger)};
}

::logspine::level parse_log_level(std::string_view raw, ::logspine::level fallback) noexcept {
    auto const iequals = [](std::string_view a, std::string_view b) noexcept {
        if (a.size() != b.size()) {
            return false;
        }
        for (std::size_t i = 0; i < a.size(); ++i) {
            if (std::tolower(static_cast<unsigned char>(a[i]))
                != std::tolower(static_cast<unsigned char>(b[i]))) {
                return false;
            }
        }
        return true;
    };

    if (iequals(raw, "debug")) {
        return ::logspine::level::debug;
    }
    if (iequals(raw, "info")) {
        return ::logspine::level::info;
    }
    if (iequals(raw, "warn") || iequals(raw, "warning")) {
        return ::logspine::level::warn;
    }
    if (iequals(raw, "error")) {
        return ::logspine::level::error;
    }
    return fallback;
}

} // namespace sonarium::core
