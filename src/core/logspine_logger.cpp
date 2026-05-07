#include "core/logspine_logger.hpp"

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

} // namespace sonarium::core
