#include <catch2/catch_test_macros.hpp>

#include "upnp/device_description.hpp"

using sonarium::upnp::build_device_description;
using sonarium::upnp::DeviceInfo;
using sonarium::upnp::DeviceServicePaths;

namespace {

DeviceInfo sample_info() {
    DeviceInfo info;
    info.friendly_name = "Sonarium";
    info.manufacturer = "Kidoz";
    info.model_name = "sonarium-dlna";
    info.model_number = "0.1";
    info.udn = "uuid:abcdef01-2345-6789-abcd-ef0123456789";
    return info;
}

} // namespace

TEST_CASE("Device description includes MediaServer:1 type", "[upnp][device_description]") {
    auto const xml = build_device_description(sample_info());
    REQUIRE(xml.contains("<deviceType>urn:schemas-upnp-org:device:MediaServer:1</deviceType>"));
}

TEST_CASE("Device description carries friendlyName, model and UDN", "[upnp][device_description]") {
    auto const xml = build_device_description(sample_info());
    REQUIRE(xml.contains("<friendlyName>Sonarium</friendlyName>"));
    REQUIRE(xml.contains("<manufacturer>Kidoz</manufacturer>"));
    REQUIRE(xml.contains("<modelName>sonarium-dlna</modelName>"));
    REQUIRE(xml.contains("<modelNumber>0.1</modelNumber>"));
    REQUIRE(xml.contains("<UDN>uuid:abcdef01-2345-6789-abcd-ef0123456789</UDN>"));
}

TEST_CASE("Optional empty fields are omitted, not emitted empty", "[upnp][device_description]") {
    auto const xml = build_device_description(sample_info());
    REQUIRE(!xml.contains("<modelDescription>"));
    REQUIRE(!xml.contains("<manufacturerURL>"));
    REQUIRE(!xml.contains("<serialNumber>"));
}

TEST_CASE("Device description references both services", "[upnp][device_description]") {
    auto const xml = build_device_description(sample_info());
    REQUIRE(xml.contains("urn:schemas-upnp-org:service:ContentDirectory:1"));
    REQUIRE(xml.contains("urn:schemas-upnp-org:service:ConnectionManager:1"));
    REQUIRE(xml.contains("<SCPDURL>/ContentDirectory/scpd.xml</SCPDURL>"));
    REQUIRE(xml.contains("<controlURL>/upnp/control/content-directory</controlURL>"));
    REQUIRE(xml.contains("<eventSubURL>/upnp/event/content-directory</eventSubURL>"));
    REQUIRE(xml.contains("<SCPDURL>/ConnectionManager/scpd.xml</SCPDURL>"));
    REQUIRE(xml.contains("<controlURL>/upnp/control/connection-manager</controlURL>"));
}

TEST_CASE("Custom service paths are honored", "[upnp][device_description]") {
    DeviceServicePaths paths;
    paths.content_directory_ctrl = "/api/cd/ctrl";
    auto const xml = build_device_description(sample_info(), paths);
    REQUIRE(xml.contains("<controlURL>/api/cd/ctrl</controlURL>"));
}

TEST_CASE("Special characters in friendlyName are XML-escaped", "[upnp][device_description]") {
    DeviceInfo info = sample_info();
    info.friendly_name = "AT&T <Lab>";
    auto const xml = build_device_description(info);
    REQUIRE(xml.contains("<friendlyName>AT&amp;T &lt;Lab&gt;</friendlyName>"));
}
