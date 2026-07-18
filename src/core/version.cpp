#include "core/version.hpp"

#include "version_info.hpp"

// SONARIUM_VERSION_* are injected by src/core/meson.build from the Meson
// project version, so `meson.build` stays the single source of truth.
#ifndef SONARIUM_VERSION_MAJOR
#    error "SONARIUM_VERSION_MAJOR must be defined by the build system"
#endif

#define SONARIUM_STR_IMPL(x) #x
#define SONARIUM_STR(x) SONARIUM_STR_IMPL(x)

namespace sonarium::core {

Version current_version() noexcept {
    return Version{SONARIUM_VERSION_MAJOR, SONARIUM_VERSION_MINOR, SONARIUM_VERSION_PATCH};
}

std::string_view product_name() noexcept {
    return "Sonarium";
}

std::string_view server_signature() noexcept {
    return "Sonarium/" SONARIUM_STR(SONARIUM_VERSION_MAJOR) "." SONARIUM_STR(SONARIUM_VERSION_MINOR) " UPnP/1.0 dlna/" SONARIUM_STR(
        SONARIUM_VERSION_MAJOR) "." SONARIUM_STR(SONARIUM_VERSION_MINOR);
}

std::string_view git_revision() noexcept {
    return SONARIUM_GIT_REVISION;
}

} // namespace sonarium::core
