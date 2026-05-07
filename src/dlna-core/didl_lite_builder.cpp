#include "dlna-core/didl_lite_builder.hpp"

#include <string>
#include <string_view>

#include "dlna-core/xml_escape.hpp"

namespace sonarium::dlna {

namespace {

constexpr std::string_view didl_open = R"(<DIDL-Lite )"
                                       R"(xmlns="urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/" )"
                                       R"(xmlns:dc="http://purl.org/dc/elements/1.1/" )"
                                       R"(xmlns:upnp="urn:schemas-upnp-org:metadata-1-0/upnp/">)";

constexpr std::string_view didl_close = "</DIDL-Lite>";

void append_attr(std::string& out, std::string_view name, std::string_view value) {
    out.push_back(' ');
    out.append(name);
    out.append(R"(=")");
    out.append(xml_escape(value));
    out.push_back('"');
}

void append_optional_attr(std::string& out, std::string_view name, auto const& value) {
    if (value.has_value()) {
        append_attr(out, name, std::to_string(*value));
    }
}

void append_text_element(std::string& out, std::string_view tag, std::string_view value) {
    out.push_back('<');
    out.append(tag);
    out.push_back('>');
    out.append(xml_escape(value));
    out.append("</");
    out.append(tag);
    out.push_back('>');
}

void append_optional_text_element(std::string& out,
                                  std::string_view tag,
                                  std::optional<std::string> const& value) {
    if (value.has_value()) {
        append_text_element(out, tag, *value);
    }
}

void append_container(std::string& out, DidlContainer const& c) {
    out.append("<container");
    append_attr(out, "id", c.id);
    append_attr(out, "parentID", c.parent_id);
    append_attr(out, "restricted", "1");
    append_optional_attr(out, "childCount", c.child_count);
    out.push_back('>');

    append_text_element(out, "dc:title", c.title);
    if (!c.upnp_class.empty()) {
        append_text_element(out, "upnp:class", c.upnp_class);
    } else {
        append_text_element(out, "upnp:class", "object.container");
    }

    out.append("</container>");
}

void append_resource(std::string& out, DidlResource const& r) {
    out.append("<res");
    append_attr(out, "protocolInfo", r.protocol_info);
    if (r.duration.has_value()) {
        append_attr(out, "duration", *r.duration);
    }
    append_optional_attr(out, "bitrate", r.bitrate_bps);
    append_optional_attr(out, "sampleFrequency", r.sample_rate_hz);
    append_optional_attr(out, "nrAudioChannels", r.channels);
    append_optional_attr(out, "size", r.size_bytes);
    out.push_back('>');
    out.append(xml_escape(r.url));
    out.append("</res>");
}

void append_item(std::string& out, DidlItem const& i) {
    out.append("<item");
    append_attr(out, "id", i.id);
    append_attr(out, "parentID", i.parent_id);
    append_attr(out, "restricted", "1");
    out.push_back('>');

    append_text_element(out, "dc:title", i.title);
    if (!i.upnp_class.empty()) {
        append_text_element(out, "upnp:class", i.upnp_class);
    } else {
        append_text_element(out, "upnp:class", "object.item.audioItem.musicTrack");
    }
    append_optional_text_element(out, "dc:creator", i.creator);
    append_optional_text_element(out, "upnp:album", i.album);
    append_optional_text_element(out, "upnp:genre", i.genre);
    if (i.original_track_number.has_value()) {
        append_text_element(
            out, "upnp:originalTrackNumber", std::to_string(*i.original_track_number));
    }
    append_optional_text_element(out, "upnp:albumArtURI", i.album_art_uri);

    for (auto const& r : i.resources) {
        append_resource(out, r);
    }

    out.append("</item>");
}

} // namespace

std::string build_didl_lite(std::vector<DidlContainer> const& containers,
                            std::vector<DidlItem> const& items) {
    std::string out;
    out.reserve(256);
    out.append(didl_open);
    for (auto const& c : containers) {
        append_container(out, c);
    }
    for (auto const& i : items) {
        append_item(out, i);
    }
    out.append(didl_close);
    return out;
}

} // namespace sonarium::dlna
