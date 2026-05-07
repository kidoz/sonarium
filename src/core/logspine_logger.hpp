#pragma once

#include <logspine/level.hpp>
#include <logspine/registry.hpp>
#include <memory>
#include <string_view>
#include <vector>

#include "core/logger.hpp"

namespace sonarium::core {

// LogSpine-backed Logger. Owns a `shared_ptr<logspine::logger>` retrieved from
// a registry; flush() forwards to the underlying logger so tests / shutdown
// code can drain async dispatchers.
class LogSpineLogger final : public Logger {
public:
    explicit LogSpineLogger(std::shared_ptr<::logspine::logger> log) noexcept;

    void log(::logspine::level severity,
             std::string_view message,
             std::vector<::logspine::field> fields) override;

    void flush() override;

private:
    std::shared_ptr<::logspine::logger> log_;
};

// Registry handle plus a Logger view bound to one of its named loggers.
// The registry is exposed so callers can `flush()` and shut down cleanly.
struct ConsoleLoggerHandle {
    std::shared_ptr<::logspine::logger_registry> registry;
    std::shared_ptr<Logger> logger;
};

// Build a LogSpine registry with a synchronous human-format console sink and
// return a Logger wrapping the named logger out of it. The default level keeps
// debug noise down; raise it via the registry handle if needed.
[[nodiscard]] ConsoleLoggerHandle
build_console_logger(std::string_view component_name,
                     ::logspine::level threshold = ::logspine::level::info);

} // namespace sonarium::core
