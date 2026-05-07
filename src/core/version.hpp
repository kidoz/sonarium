#pragma once

#include <string_view>

namespace sonarium::core {

struct Version {
    int major;
    int minor;
    int patch;

    [[nodiscard]] constexpr bool operator==(Version const&) const = default;
};

[[nodiscard]] Version current_version() noexcept;
[[nodiscard]] std::string_view product_name() noexcept;
[[nodiscard]] std::string_view server_signature() noexcept;

} // namespace sonarium::core
