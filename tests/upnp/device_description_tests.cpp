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
    REQUIRE(xml.find("<deviceType>urn:schemas-upnp-org:device:MediaServer:1</deviceType>")
            != std::string::npos);
}

TEST_CASE("Device description carries friendlyName, model and UDN", "[upnp][device_description]") {
    auto const xml = build_device_description(sample_info());
    REQUIRE(xml.find("<friendlyName>Sonarium</friendlyName>") != std::string::npos);
    REQUIRE(xml.find("<manufacturer>Kidoz</manufacturer>") != std::string::npos);
    REQUIRE(xml.find("<modelName>sonarium-dlna</modelName>") != std::string::npos);
    REQUIRE(xml.find("<modelNumber>0.1</modelNumber>") != std::string::npos);
    REQUIRE(xml.find("<UDN>uuid:abcdef01-2345-6789-abcd-ef0123456789</UDN>") != std::string::npos);
}

TEST_CASE("Optional empty fields are omitted, not emitted empty", "[upnp][device_description]") {
    auto const xml = build_device_description(sample_info());
    REQUIRE(xml.find("<modelDescription>") == std::string::npos);
    REQUIRE(xml.find("<manufacturerURL>") == std::string::npos);
    REQUIRE(xml.find("<serialNumber>") == std::string::npos);
}

TEST_CASE("Device description references both services", "[upnp][device_description]") {
    auto const xml = build_device_description(sample_info());
    REQUIRE(xml.find("urn:schemas-upnp-org:service:ContentDirectory:1") != std::string::npos);
    REQUIRE(xml.find("urn:schemas-upnp-org:service:ConnectionManager:1") != std::string::npos);
    REQUIRE(xml.find("<SCPDURL>/ContentDirectory/scpd.xml</SCPDURL>") != std::string::npos);
    REQUIRE(xml.find("<controlURL>/upnp/control/content-directory</controlURL>")
            != std::string::npos);
    REQUIRE(xml.find("<eventSubURL>/upnp/event/content-directory</eventSubURL>")
            != std::string::npos);
    REQUIRE(xml.find("<SCPDURL>/ConnectionManager/scpd.xml</SCPDURL>") != std::string::npos);
    REQUIRE(xml.find("<controlURL>/upnp/control/connection-manager</controlURL>")
            != std::string::npos);
}

TEST_CASE("Custom service paths are honored", "[upnp][device_description]") {
    DeviceServicePaths paths;
    paths.content_directory_ctrl = "/api/cd/ctrl";
    auto const xml = build_device_description(sample_info(), paths);
    REQUIRE(xml.find("<controlURL>/api/cd/ctrl</controlURL>") != std::string::npos);
}

TEST_CASE("Special characters in friendlyName are XML-escaped", "[upnp][device_description]") {
    DeviceInfo info = sample_info();
    info.friendly_name = "AT&T <Lab>";
    auto const xml = build_device_description(info);
    REQUIRE(xml.find("<friendlyName>AT&amp;T &lt;Lab&gt;</friendlyName>") != std::string::npos);
}
