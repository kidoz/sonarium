#include "upnp/device_description.hpp"

#include <string>
#include <string_view>

#include "dlna-core/xml_escape.hpp"

namespace sonarium::upnp {

namespace {

void append_optional_element(std::string& out, std::string_view tag, std::string_view value) {
    if (value.empty()) {
        return;
    }
    out.push_back('<');
    out.append(tag);
    out.push_back('>');
    out.append(sonarium::dlna::xml_escape(value));
    out.append("</");
    out.append(tag);
    out.push_back('>');
}

void append_service(std::string& out,
                    std::string_view service_type,
                    std::string_view service_id,
                    std::string_view scpd_url,
                    std::string_view control_url,
                    std::string_view event_url) {
    using sonarium::dlna::xml_escape;
    out.append("<service>");
    out.append("<serviceType>").append(service_type).append("</serviceType>");
    out.append("<serviceId>").append(service_id).append("</serviceId>");
    out.append("<SCPDURL>").append(xml_escape(scpd_url)).append("</SCPDURL>");
    out.append("<controlURL>").append(xml_escape(control_url)).append("</controlURL>");
    out.append("<eventSubURL>").append(xml_escape(event_url)).append("</eventSubURL>");
    out.append("</service>");
}

} // namespace

std::string build_device_description(DeviceInfo const& info, DeviceServicePaths const& paths) {
    std::string out;
    out.reserve(1024);
    out.append(R"(<?xml version="1.0" encoding="utf-8"?>)");
    out.append(
        R"(<root xmlns="urn:schemas-upnp-org:device-1-0" xmlns:dlna="urn:schemas-dlna-org:device-1-0">)");
    out.append("<specVersion><major>1</major><minor>0</minor></specVersion>");
    out.append("<device>");
    out.append("<deviceType>urn:schemas-upnp-org:device:MediaServer:1</deviceType>");
    out.append("<dlna:X_DLNADOC>DMS-1.50</dlna:X_DLNADOC>");

    append_optional_element(out, "friendlyName", info.friendly_name);
    append_optional_element(out, "manufacturer", info.manufacturer);
    append_optional_element(out, "manufacturerURL", info.manufacturer_url);
    append_optional_element(out, "modelDescription", info.model_description);
    append_optional_element(out, "modelName", info.model_name);
    append_optional_element(out, "modelNumber", info.model_number);
    append_optional_element(out, "modelURL", info.model_url);
    append_optional_element(out, "serialNumber", info.serial_number);
    append_optional_element(out, "UDN", info.udn);

    out.append("<serviceList>");
    append_service(out,
                   "urn:schemas-upnp-org:service:ContentDirectory:1",
                   "urn:upnp-org:serviceId:ContentDirectory",
                   paths.content_directory_scpd,
                   paths.content_directory_ctrl,
                   paths.content_directory_event);
    append_service(out,
                   "urn:schemas-upnp-org:service:ConnectionManager:1",
                   "urn:upnp-org:serviceId:ConnectionManager",
                   paths.connection_manager_scpd,
                   paths.connection_manager_ctrl,
                   paths.connection_manager_event);
    out.append("</serviceList>");

    out.append("</device>");
    out.append("</root>");
    return out;
}

} // namespace sonarium::upnp
