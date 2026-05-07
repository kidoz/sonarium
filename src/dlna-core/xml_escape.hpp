#pragma once

#include <string>
#include <string_view>

namespace sonarium::dlna {

// Escape an XML attribute or element-text fragment.
// Escapes &, <, >, ", '. Does not assume the input is well-formed UTF-8.
[[nodiscard]] std::string xml_escape(std::string_view s);

} // namespace sonarium::dlna
