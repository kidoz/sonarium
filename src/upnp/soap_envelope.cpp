#include "upnp/soap_envelope.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "dlna-core/xml_escape.hpp"

namespace sonarium::upnp {

namespace {

[[nodiscard]] bool is_space(char c) noexcept {
    return std::isspace(static_cast<unsigned char>(c)) != 0;
}

[[nodiscard]] bool is_name_start(char c) noexcept {
    return std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_';
}

[[nodiscard]] bool is_name_part(char c) noexcept {
    return is_name_start(c) || std::isdigit(static_cast<unsigned char>(c)) != 0 || c == '-'
           || c == '.';
}

// Strip BOM and leading whitespace.
[[nodiscard]] std::string_view strip_prologue(std::string_view s) noexcept {
    if (s.starts_with("\xef\xbb\xbf")) {
        s.remove_prefix(3);
    }
    while (!s.empty() && is_space(s.front())) {
        s.remove_prefix(1);
    }
    return s;
}

// Return the index of the closing `>` for a tag starting at `i`. Skips over
// quoted attribute values so that `>` inside them is ignored.
[[nodiscard]] std::size_t find_tag_close(std::string_view s, std::size_t i) noexcept {
    char quote = '\0';
    for (; i < s.size(); ++i) {
        char const c = s[i];
        if (quote != '\0') {
            if (c == quote) {
                quote = '\0';
            }
            continue;
        }
        if (c == '"' || c == '\'') {
            quote = c;
            continue;
        }
        if (c == '>') {
            return i;
        }
    }
    return std::string_view::npos;
}

// Skip XML decl, processing instructions, comments, and DOCTYPE.
[[nodiscard]] std::string_view skip_meta(std::string_view s) noexcept {
    while (true) {
        s = strip_prologue(s);
        if (s.size() < 2 || s[0] != '<') {
            return s;
        }
        if (s.starts_with("<?")) {
            auto const end = s.find("?>");
            if (end == std::string_view::npos) {
                return {};
            }
            s.remove_prefix(end + 2);
            continue;
        }
        if (s.starts_with("<!--")) {
            auto const end = s.find("-->");
            if (end == std::string_view::npos) {
                return {};
            }
            s.remove_prefix(end + 3);
            continue;
        }
        if (s.starts_with("<!DOCTYPE") || s.starts_with("<!doctype")) {
            auto const end = find_tag_close(s, 0);
            if (end == std::string_view::npos) {
                return {};
            }
            s.remove_prefix(end + 1);
            continue;
        }
        return s;
    }
}

// Meters total characters scanned across one parse. `locate_body` and the
// per-element `find_close` calls re-scan spans, which crafted input can push
// toward O(n²); the budget bounds the damage. A legitimate envelope costs a
// small multiple of its size, so the limit is far above real traffic.
struct ParseBudget {
    std::size_t remaining;

    [[nodiscard]] bool charge(std::size_t n) noexcept {
        if (remaining < n) {
            remaining = 0;
            return false;
        }
        remaining -= n;
        return true;
    }

    [[nodiscard]] bool exhausted() const noexcept { return remaining == 0; }
};

[[nodiscard]] std::size_t scan_budget_for(std::size_t body_size) noexcept {
    constexpr std::size_t minimum = std::size_t{1} << 20; // small bodies get a generous floor
    return std::max(minimum, body_size * 8);
}

struct OpenTag {
    std::string_view qname;   // possibly prefixed: "u:Browse"
    std::string_view inside;  // raw attribute span (between qname and `>` / `/>`)
    std::size_t end_offset{}; // index immediately after the closing `>`
    bool self_closing{};
};

[[nodiscard]] std::optional<OpenTag> read_open_tag(std::string_view s, std::size_t pos) noexcept {
    if (pos >= s.size() || s[pos] != '<') {
        return std::nullopt;
    }
    auto i = pos + 1;
    if (i >= s.size() || !is_name_start(s[i])) {
        return std::nullopt;
    }
    auto const name_start = i;
    while (i < s.size() && (is_name_part(s[i]) || s[i] == ':')) {
        ++i;
    }
    auto const name = s.substr(name_start, i - name_start);

    auto const close = find_tag_close(s, i);
    if (close == std::string_view::npos) {
        return std::nullopt;
    }

    bool const self_closing = (close > 0 && s[close - 1] == '/');
    auto const attr_end = self_closing ? close - 1 : close;
    auto attrs = s.substr(i, attr_end - i);

    OpenTag t{};
    t.qname = name;
    t.inside = attrs;
    t.end_offset = close + 1;
    t.self_closing = self_closing;
    return t;
}

[[nodiscard]] std::string_view local_name(std::string_view qname) noexcept {
    auto const colon = qname.rfind(':');
    return (colon == std::string_view::npos) ? qname : qname.substr(colon + 1);
}

[[nodiscard]] std::string_view prefix_of(std::string_view qname) noexcept {
    auto const colon = qname.find(':');
    return (colon == std::string_view::npos) ? std::string_view{} : qname.substr(0, colon);
}

// Look up a namespace declaration on an open-tag attribute span.
//
//   prefix == ""  -> default xmlns
//   prefix != ""  -> xmlns:<prefix>
[[nodiscard]] std::optional<std::string_view> find_xmlns(std::string_view attrs,
                                                         std::string_view prefix) noexcept {
    std::size_t i = 0;
    while (i < attrs.size()) {
        while (i < attrs.size() && is_space(attrs[i])) {
            ++i;
        }
        if (i >= attrs.size()) {
            break;
        }
        auto const name_start = i;
        while (i < attrs.size() && (is_name_part(attrs[i]) || attrs[i] == ':')) {
            ++i;
        }
        auto const aname = attrs.substr(name_start, i - name_start);
        while (i < attrs.size() && is_space(attrs[i])) {
            ++i;
        }
        if (i >= attrs.size() || attrs[i] != '=') {
            break;
        }
        ++i;
        while (i < attrs.size() && is_space(attrs[i])) {
            ++i;
        }
        if (i >= attrs.size() || (attrs[i] != '"' && attrs[i] != '\'')) {
            break;
        }
        char const quote = attrs[i++];
        auto const val_start = i;
        while (i < attrs.size() && attrs[i] != quote) {
            ++i;
        }
        auto const value = attrs.substr(val_start, i - val_start);
        if (i < attrs.size()) {
            ++i;
        }

        if (prefix.empty()) {
            if (aname == "xmlns") {
                return value;
            }
        } else {
            if (aname.size() == 6 + prefix.size() && aname.starts_with("xmlns:")
                && aname.substr(6) == prefix) {
                return value;
            }
        }
    }
    return std::nullopt;
}

// Find the next opening element after `pos`, skipping whitespace, comments, text.
[[nodiscard]] std::optional<OpenTag>
next_element(std::string_view s, std::size_t& pos, ParseBudget& budget) noexcept {
    while (pos < s.size()) {
        if (is_space(s[pos]) || s[pos] != '<') {
            if (!budget.charge(1)) {
                return std::nullopt;
            }
            ++pos;
            continue;
        }
        if (s.substr(pos).starts_with("<!--")) {
            auto const end = s.find("-->", pos);
            if (end == std::string_view::npos || !budget.charge(end + 3 - pos)) {
                return std::nullopt;
            }
            pos = end + 3;
            continue;
        }
        if (s.substr(pos).starts_with("<![CDATA[")) {
            // CDATA at this level can only appear as text — skip.
            auto const end = s.find("]]>", pos);
            if (end == std::string_view::npos || !budget.charge(end + 3 - pos)) {
                return std::nullopt;
            }
            pos = end + 3;
            continue;
        }
        if (s[pos + 1] == '/') {
            return std::nullopt; // end tag
        }
        auto tag = read_open_tag(s, pos);
        if (tag.has_value() && !budget.charge(tag->end_offset - pos)) {
            return std::nullopt;
        }
        return tag;
    }
    return std::nullopt;
}

// Find matching closing tag for an open tag with `qname` starting at `from`.
// Returns the position of the `<` of the closing tag (so the inner span is
// [from, returned_pos)). Tracks nested opens of the same qname.
[[nodiscard]] std::size_t find_close(std::string_view s,
                                     std::string_view qname,
                                     std::size_t from,
                                     ParseBudget& budget) noexcept {
    int depth = 1;
    std::size_t i = from;
    while (i < s.size()) {
        if (s[i] != '<') {
            if (!budget.charge(1)) {
                return std::string_view::npos;
            }
            ++i;
            continue;
        }
        if (s.substr(i).starts_with("<!--")) {
            auto const end = s.find("-->", i);
            if (end == std::string_view::npos || !budget.charge(end + 3 - i)) {
                return std::string_view::npos;
            }
            i = end + 3;
            continue;
        }
        if (s.substr(i).starts_with("<![CDATA[")) {
            auto const end = s.find("]]>", i);
            if (end == std::string_view::npos || !budget.charge(end + 3 - i)) {
                return std::string_view::npos;
            }
            i = end + 3;
            continue;
        }
        if (s[i + 1] == '/') {
            // </qname>
            auto const close = find_tag_close(s, i);
            if (close == std::string_view::npos || !budget.charge(close + 1 - i)) {
                return std::string_view::npos;
            }
            auto const inside = s.substr(i + 2, close - (i + 2));
            std::size_t a = 0;
            std::size_t b = inside.size();
            while (a < b && is_space(inside[a])) {
                ++a;
            }
            while (b > a && is_space(inside[b - 1])) {
                --b;
            }
            auto const name = inside.substr(a, b - a);
            if (name == qname) {
                if (--depth == 0) {
                    return i;
                }
            }
            i = close + 1;
            continue;
        }
        if (auto opened = read_open_tag(s, i); opened) {
            if (!budget.charge(opened->end_offset - i)) {
                return std::string_view::npos;
            }
            if (opened->qname == qname && !opened->self_closing) {
                ++depth;
            }
            i = opened->end_offset;
            continue;
        }
        if (!budget.charge(1)) {
            return std::string_view::npos;
        }
        ++i;
    }
    return std::string_view::npos;
}

[[nodiscard]] std::string_view inner_text(std::string_view s) noexcept {
    // Strip leading/trailing whitespace; UPnP arg values are scalar text.
    std::size_t a = 0;
    std::size_t b = s.size();
    while (a < b && is_space(s[a])) {
        ++a;
    }
    while (b > a && is_space(s[b - 1])) {
        --b;
    }
    return s.substr(a, b - a);
}

// Decode the five XML entities used in UPnP arg values. Numeric entities
// (&#NN;, &#xNN;) are left as-is — UPnP control args do not need them.
[[nodiscard]] std::string xml_unescape(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size();) {
        if (s[i] != '&') {
            out.push_back(s[i++]);
            continue;
        }
        auto const tail = s.substr(i);
        if (tail.starts_with("&amp;")) {
            out.push_back('&');
            i += 5;
            continue;
        }
        if (tail.starts_with("&lt;")) {
            out.push_back('<');
            i += 4;
            continue;
        }
        if (tail.starts_with("&gt;")) {
            out.push_back('>');
            i += 4;
            continue;
        }
        if (tail.starts_with("&quot;")) {
            out.push_back('"');
            i += 6;
            continue;
        }
        if (tail.starts_with("&apos;")) {
            out.push_back('\'');
            i += 6;
            continue;
        }
        out.push_back(s[i++]);
    }
    return out;
}

// Find the `Body` element (any namespace prefix) inside the envelope content.
[[nodiscard]] std::expected<std::pair<std::size_t, std::string_view>, SoapParseError>
locate_body(std::string_view envelope_inner, ParseBudget& budget) {
    std::size_t pos = 0;
    while (true) {
        auto el = next_element(envelope_inner, pos, budget);
        if (!el) {
            return std::unexpected(budget.exhausted() ? SoapParseError::too_complex
                                                      : SoapParseError::body_not_found);
        }
        if (local_name(el->qname) == "Body" || local_name(el->qname) == "body") {
            auto const body_close = find_close(envelope_inner, el->qname, el->end_offset, budget);
            if (body_close == std::string_view::npos) {
                return std::unexpected(budget.exhausted() ? SoapParseError::too_complex
                                                          : SoapParseError::malformed);
            }
            auto const body_inner =
                envelope_inner.substr(el->end_offset, body_close - el->end_offset);
            return std::make_pair(el->end_offset, body_inner);
        }
        // Skip past element and continue scanning.
        if (el->self_closing) {
            pos = el->end_offset;
        } else {
            auto const close = find_close(envelope_inner, el->qname, el->end_offset, budget);
            if (close == std::string_view::npos) {
                return std::unexpected(budget.exhausted() ? SoapParseError::too_complex
                                                          : SoapParseError::malformed);
            }
            pos = close;
            // Move past closing tag too.
            auto const close_end = find_tag_close(envelope_inner, pos);
            if (close_end == std::string_view::npos) {
                return std::unexpected(SoapParseError::malformed);
            }
            pos = close_end + 1;
        }
    }
}

} // namespace

std::expected<ParsedSoapRequest, SoapParseError> parse_soap_request(std::string_view body,
                                                                    std::size_t max_scan_chars) {
    auto stripped = skip_meta(body);
    if (stripped.empty()) {
        return std::unexpected(SoapParseError::empty);
    }

    std::size_t const pos = 0;
    auto envelope = read_open_tag(stripped, pos);
    if (!envelope
        || (local_name(envelope->qname) != "Envelope"
            && local_name(envelope->qname) != "envelope")) {
        return std::unexpected(SoapParseError::malformed);
    }
    if (envelope->self_closing) {
        return std::unexpected(SoapParseError::body_not_found);
    }

    ParseBudget budget{max_scan_chars > 0 ? max_scan_chars : scan_budget_for(body.size())};

    auto const env_close = find_close(stripped, envelope->qname, envelope->end_offset, budget);
    if (env_close == std::string_view::npos) {
        return std::unexpected(budget.exhausted() ? SoapParseError::too_complex
                                                  : SoapParseError::malformed);
    }
    auto const env_inner = stripped.substr(envelope->end_offset, env_close - envelope->end_offset);

    auto body_lookup = locate_body(env_inner, budget);
    if (!body_lookup) {
        return std::unexpected(body_lookup.error());
    }
    auto const body_inner = body_lookup->second;

    std::size_t bp = 0;
    auto action = next_element(body_inner, bp, budget);
    if (!action) {
        return std::unexpected(budget.exhausted() ? SoapParseError::too_complex
                                                  : SoapParseError::action_not_found);
    }

    auto const ns_lookup = find_xmlns(action->inside, prefix_of(action->qname));
    if (!ns_lookup) {
        return std::unexpected(SoapParseError::namespace_missing);
    }

    ParsedSoapRequest out;
    out.service_urn.assign(ns_lookup->begin(), ns_lookup->end());
    auto const local = local_name(action->qname);
    out.action.assign(local.begin(), local.end());

    if (action->self_closing) {
        return out;
    }

    auto const action_close = find_close(body_inner, action->qname, action->end_offset, budget);
    if (action_close == std::string_view::npos) {
        return std::unexpected(budget.exhausted() ? SoapParseError::too_complex
                                                  : SoapParseError::malformed);
    }
    auto const action_inner =
        body_inner.substr(action->end_offset, action_close - action->end_offset);

    std::size_t ap = 0;
    while (true) {
        auto arg = next_element(action_inner, ap, budget);
        if (!arg) {
            if (budget.exhausted()) {
                return std::unexpected(SoapParseError::too_complex);
            }
            break;
        }
        auto const arg_local = local_name(arg->qname);
        std::string value;
        if (!arg->self_closing) {
            auto const arg_close = find_close(action_inner, arg->qname, arg->end_offset, budget);
            if (arg_close == std::string_view::npos) {
                return std::unexpected(budget.exhausted() ? SoapParseError::too_complex
                                                          : SoapParseError::malformed);
            }
            auto const text = action_inner.substr(arg->end_offset, arg_close - arg->end_offset);
            value = xml_unescape(inner_text(text));
            ap = arg_close;
            auto const close_end = find_tag_close(action_inner, ap);
            if (close_end == std::string_view::npos) {
                return std::unexpected(SoapParseError::malformed);
            }
            ap = close_end + 1;
        } else {
            ap = arg->end_offset;
        }
        out.args.emplace_back(std::string(arg_local), std::move(value));
    }

    return out;
}

std::string build_soap_response(std::string_view service_urn,
                                std::string_view action,
                                std::vector<std::pair<std::string, std::string>> const& results) {
    using sonarium::dlna::xml_escape;

    std::string out;
    out.reserve(256);
    out.append(R"(<?xml version="1.0" encoding="utf-8"?>)");
    out.append(R"(<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/" )"
               R"(s:encodingStyle="http://schemas.xmlsoap.org/soap/encoding/">)");
    out.append("<s:Body>");
    out.append("<u:");
    out.append(action);
    out.append("Response xmlns:u=\"");
    out.append(xml_escape(service_urn));
    out.append("\">");
    for (auto const& [name, value] : results) {
        out.push_back('<');
        out.append(name);
        out.push_back('>');
        out.append(xml_escape(value));
        out.append("</");
        out.append(name);
        out.push_back('>');
    }
    out.append("</u:");
    out.append(action);
    out.append("Response>");
    out.append("</s:Body></s:Envelope>");
    return out;
}

std::string build_soap_fault(UpnpErrorCode code, std::string_view description) {
    using sonarium::dlna::xml_escape;

    auto const desc = description.empty() ? default_description(code) : description;

    std::string out;
    out.reserve(384);
    out.append(R"(<?xml version="1.0" encoding="utf-8"?>)");
    out.append(R"(<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/" )"
               R"(s:encodingStyle="http://schemas.xmlsoap.org/soap/encoding/">)");
    out.append("<s:Body><s:Fault>");
    out.append("<faultcode>s:Client</faultcode>");
    out.append("<faultstring>UPnPError</faultstring>");
    out.append("<detail>");
    out.append(R"(<UPnPError xmlns="urn:schemas-upnp-org:control-1-0">)");
    out.append("<errorCode>");
    out.append(std::to_string(static_cast<unsigned>(code)));
    out.append("</errorCode>");
    out.append("<errorDescription>");
    out.append(xml_escape(desc));
    out.append("</errorDescription>");
    out.append("</UPnPError>");
    out.append("</detail>");
    out.append("</s:Fault></s:Body></s:Envelope>");
    return out;
}

} // namespace sonarium::upnp
