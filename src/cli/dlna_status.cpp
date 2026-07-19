#include "cli/dlna_status.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <ranges>
#include <string>

namespace sonarium::cli {

namespace {

// Find an opening tag whose local name is `tag`, allowing an optional
// namespace prefix (e.g. "<dev:friendlyName>"). Returns the position of the
// '<' that starts the tag, or npos.
[[nodiscard]] std::size_t
find_open_tag(std::string_view body, std::string_view tag, std::size_t from = 0) {
    while (from < body.size()) {
        auto const lt = body.find('<', from);
        if (lt == std::string_view::npos) {
            return std::string_view::npos;
        }
        auto const after = lt + 1;
        if (after >= body.size()) {
            return std::string_view::npos;
        }
        if (body[after] == '/' || body[after] == '!' || body[after] == '?') {
            from = lt + 1;
            continue;
        }
        // Skip optional "prefix:".
        auto name_begin = after;
        auto const colon = body.find_first_of(":> \t\r\n/", name_begin);
        if (colon == std::string_view::npos) {
            return std::string_view::npos;
        }
        if (body[colon] == ':') {
            name_begin = colon + 1;
        }
        // Local-name match: must equal `tag` and be terminated by '>', whitespace, or '/'.
        if (body.size() - name_begin >= tag.size()
            && body.compare(name_begin, tag.size(), tag) == 0) {
            auto const after_name = name_begin + tag.size();
            if (after_name < body.size()) {
                char const next = body[after_name];
                if (next == '>' || next == ' ' || next == '\t' || next == '\r' || next == '\n'
                    || next == '/') {
                    return lt;
                }
            }
        }
        from = lt + 1;
    }
    return std::string_view::npos;
}

[[nodiscard]] std::string trim(std::string_view s) {
    auto const not_space = [](char c) { return c != ' ' && c != '\t' && c != '\r' && c != '\n'; };
    const auto* const begin = std::ranges::find_if(s, not_space);
    const auto* const end = std::ranges::find_if(std::views::reverse(s), not_space).base();
    if (begin >= end) {
        return {};
    }
    return std::string{begin, end};
}

[[nodiscard]] std::string xml_unescape(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size();) {
        if (s[i] != '&') {
            out.push_back(s[i++]);
            continue;
        }
        auto const semi = s.find(';', i);
        if (semi == std::string_view::npos) {
            out.append(s.substr(i));
            break;
        }
        auto const entity = s.substr(i + 1, semi - i - 1);
        if (entity == "lt") {
            out.push_back('<');
        } else if (entity == "gt") {
            out.push_back('>');
        } else if (entity == "amp") {
            out.push_back('&');
        } else if (entity == "quot") {
            out.push_back('"');
        } else if (entity == "apos") {
            out.push_back('\'');
        } else {
            out.append(s.substr(i, semi - i + 1));
        }
        i = semi + 1;
    }
    return out;
}

[[nodiscard]] std::expected<std::uint32_t, std::string> parse_u32(std::string_view s,
                                                                  std::string_view label) {
    auto const trimmed = trim(s);
    std::uint32_t v = 0;
    auto const* first = trimmed.data();
    auto const* last = trimmed.data() + trimmed.size();
    auto const r = std::from_chars(first, last, v);
    if (r.ec != std::errc{}) {
        return std::unexpected(std::string{label} + ": not a number ('" + trimmed + "')");
    }
    return v;
}

} // namespace

std::string extract_xml_text(std::string_view body, std::string_view tag) {
    auto const lt = find_open_tag(body, tag);
    if (lt == std::string_view::npos) {
        return {};
    }
    auto const gt = body.find('>', lt);
    if (gt == std::string_view::npos) {
        return {};
    }
    if (gt > 0 && body[gt - 1] == '/') {
        return {}; // self-closing
    }
    auto const content_begin = gt + 1;

    // Find a closing tag with matching local name.
    auto pos = content_begin;
    while (pos < body.size()) {
        auto const close_lt = body.find("</", pos);
        if (close_lt == std::string_view::npos) {
            return {};
        }
        auto name_begin = close_lt + 2;
        auto const colon = body.find_first_of(":> \t\r\n", name_begin);
        if (colon == std::string_view::npos) {
            return {};
        }
        if (body[colon] == ':') {
            name_begin = colon + 1;
        }
        if (body.size() - name_begin >= tag.size()
            && body.compare(name_begin, tag.size(), tag) == 0) {
            auto const after_name = name_begin + tag.size();
            if (after_name < body.size()) {
                char const next = body[after_name];
                if (next == '>' || next == ' ' || next == '\t' || next == '\r' || next == '\n') {
                    return trim(body.substr(content_begin, close_lt - content_begin));
                }
            }
        }
        pos = close_lt + 2;
    }
    return {};
}

std::expected<DlnaStatus, std::string> parse_description_xml(std::string_view body) {
    DlnaStatus out;
    out.friendly_name = extract_xml_text(body, "friendlyName");
    out.udn = extract_xml_text(body, "UDN");
    out.model_name = extract_xml_text(body, "modelName");
    out.model_number = extract_xml_text(body, "modelNumber");
    if (out.udn.empty()) {
        return std::unexpected("description.xml: <UDN> not found");
    }
    return out;
}

std::string build_browse_request(std::string_view object_id,
                                 std::uint32_t starting_index,
                                 std::uint32_t requested_count) {
    std::string out;
    out.reserve(512);
    out.append(R"(<?xml version="1.0" encoding="utf-8"?>)");
    out.append("\n<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\">");
    out.append("\n  <s:Body>");
    out.append("\n    <u:Browse xmlns:u=\"urn:schemas-upnp-org:service:ContentDirectory:1\">");
    out.append("\n      <ObjectID>");
    out.append(object_id);
    out.append("</ObjectID>");
    out.append("\n      <BrowseFlag>BrowseDirectChildren</BrowseFlag>");
    out.append("\n      <Filter>*</Filter>");
    out.append("\n      <StartingIndex>");
    out.append(std::to_string(starting_index));
    out.append("</StartingIndex>");
    out.append("\n      <RequestedCount>");
    out.append(std::to_string(requested_count));
    out.append("</RequestedCount>");
    out.append("\n      <SortCriteria></SortCriteria>");
    out.append("\n    </u:Browse>");
    out.append("\n  </s:Body>");
    out.append("\n</s:Envelope>\n");
    return out;
}

std::expected<BrowseSummary, std::string> parse_browse_response(std::string_view body) {
    auto const total_str = extract_xml_text(body, "TotalMatches");
    auto const returned_str = extract_xml_text(body, "NumberReturned");
    auto const update_str = extract_xml_text(body, "UpdateID");
    if (total_str.empty() && returned_str.empty()) {
        // Look for a SOAP Fault to surface a useful error.
        auto const fault = extract_xml_text(body, "faultstring");
        if (!fault.empty()) {
            return std::unexpected("SOAP fault: " + fault);
        }
        return std::unexpected("Browse response: TotalMatches/NumberReturned not found");
    }

    BrowseSummary s;
    auto const total = parse_u32(total_str, "TotalMatches");
    if (!total.has_value()) {
        return std::unexpected(total.error());
    }
    s.total_matches = *total;
    auto const returned = parse_u32(returned_str, "NumberReturned");
    if (!returned.has_value()) {
        return std::unexpected(returned.error());
    }
    s.number_returned = *returned;
    if (!update_str.empty()) {
        if (auto v = parse_u32(update_str, "UpdateID"); v.has_value()) {
            s.update_id = *v;
        }
    }
    // The Result element holds escaped DIDL-Lite. We don't need to decode it
    // for the status report, but unescape exists for callers that want to.
    (void)xml_unescape;
    return s;
}

} // namespace sonarium::cli
