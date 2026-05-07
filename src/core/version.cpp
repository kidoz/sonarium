#include "core/version.hpp"

namespace sonarium::core {

Version current_version() noexcept {
    return Version{0, 1, 0};
}

std::string_view product_name() noexcept {
    return "Sonarium";
}

std::string_view server_signature() noexcept {
    return "Sonarium/0.1 UPnP/1.0 dlna/0.1";
}

} // namespace sonarium::core
