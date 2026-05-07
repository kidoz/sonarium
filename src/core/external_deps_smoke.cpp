#include "core/external_deps_smoke.hpp"

#include <asterorm/core/error.hpp>
#include <atria/status.hpp>
#include <ctorwire/scope.hpp>
#include <logspine/level.hpp>

namespace sonarium::core {

ExternalDepsSmoke external_deps_smoke() noexcept {
    ExternalDepsSmoke out;
    out.atria_ok_reason = ::atria::reason_phrase(::atria::Status::Ok);
    out.logspine_info_level = static_cast<std::uint8_t>(::logspine::level::info);
    out.logspine_info_label = ::logspine::to_string(::logspine::level::info);
    out.ctorwire_scope_constructible = []() noexcept {
        ::ctorwire::scope s;
        (void)s;
        return true;
    }();
    // Empty SQLSTATE -> asterorm::db_error_kind::unknown — proves the
    // asterorm_core symbol links and the enum is reachable from our headers.
    out.asterorm_unknown_kind = static_cast<std::uint8_t>(::asterorm::classify_sqlstate(""));
    return out;
}

} // namespace sonarium::core
