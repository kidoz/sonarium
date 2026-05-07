#include "dlna-core/xml_escape.hpp"

namespace sonarium::dlna {

std::string xml_escape(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (auto c : s) {
        switch (c) {
            case '&':
                out += "&amp;";
                break;
            case '<':
                out += "&lt;";
                break;
            case '>':
                out += "&gt;";
                break;
            case '"':
                out += "&quot;";
                break;
            case '\'':
                out += "&apos;";
                break;
            default:
                out.push_back(c);
                break;
        }
    }
    return out;
}

} // namespace sonarium::dlna
