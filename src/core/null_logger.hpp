#pragma once

#include <memory>
#include <string_view>
#include <vector>

#include "core/logger.hpp"

namespace sonarium::core {

// Discards every log call. Use as a default in tests that don't assert on
// log output.
class NullLogger final : public Logger {
public:
    void log(::logspine::level /*level*/,
             std::string_view /*message*/,
             std::vector<::logspine::field> /*fields*/) override {}
};

[[nodiscard]] inline std::shared_ptr<Logger> make_null_logger() {
    return std::make_shared<NullLogger>();
}

} // namespace sonarium::core
