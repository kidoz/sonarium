#pragma once

#include <string_view>

namespace sonarium::core {

struct Version {
    int major;
    int minor;
    int patch;

    [[nodiscard]] constexpr bool operator==(Version const&) const = default;
};

// Version numbers come from the Meson project version (single source of
// truth); the git revision is stamped at build time via vcs_tag.
[[nodiscard]] Version current_version() noexcept;
[[nodiscard]] std::string_view product_name() noexcept;
[[nodiscard]] std::string_view server_signature() noexcept;
[[nodiscard]] std::string_view git_revision() noexcept;

} // namespace sonarium::core
