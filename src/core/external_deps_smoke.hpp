#pragma once

#include <cstdint>
#include <string_view>

namespace sonarium::core {

// Sentinel values pulled from each kidoz/* wrapped dependency. Their
// existence proves atria/ctorwire/logspine/asterorm are linkable and
// consumable from the core library.
struct ExternalDepsSmoke {
    std::string_view atria_ok_reason;     // atria::reason_phrase(atria::Status::Ok)
    std::uint8_t logspine_info_level;     // static_cast<u8>(logspine::level::info)
    std::string_view logspine_info_label; // logspine::to_string(level::info)
    bool ctorwire_scope_constructible;    // ctorwire::scope default-construct succeeded
    std::uint8_t asterorm_unknown_kind;   // static_cast<u8>(asterorm::classify_sqlstate(""))
};

[[nodiscard]] ExternalDepsSmoke external_deps_smoke() noexcept;

} // namespace sonarium::core
