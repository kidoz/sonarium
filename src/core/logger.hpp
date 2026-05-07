#pragma once

#include <logspine/field.hpp>
#include <logspine/level.hpp>
#include <string_view>
#include <utility>
#include <vector>

namespace sonarium::core {

// Sonarium's logging interface. Wraps logspine fields without dictating a
// specific dispatcher / sink. The composition root receives `shared_ptr<Logger>`
// via dependency injection; tests inject a CapturingLogger fake; the
// `sonarium-dlna` binary uses a LogSpine-backed adapter.
class Logger {
public:
    Logger() = default;
    virtual ~Logger() = default;

    virtual void log(::logspine::level level,
                     std::string_view message,
                     std::vector<::logspine::field> fields = {}) = 0;

    void debug(std::string_view m, std::vector<::logspine::field> f = {}) {
        log(::logspine::level::debug, m, std::move(f));
    }
    void info(std::string_view m, std::vector<::logspine::field> f = {}) {
        log(::logspine::level::info, m, std::move(f));
    }
    void warn(std::string_view m, std::vector<::logspine::field> f = {}) {
        log(::logspine::level::warn, m, std::move(f));
    }
    void error(std::string_view m, std::vector<::logspine::field> f = {}) {
        log(::logspine::level::error, m, std::move(f));
    }

    virtual void flush() {}

protected:
    Logger(Logger const&) = default;
    Logger& operator=(Logger const&) = default;
    Logger(Logger&&) noexcept = default;
    Logger& operator=(Logger&&) noexcept = default;
};

} // namespace sonarium::core
