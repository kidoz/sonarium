#include <catch2/catch_test_macros.hpp>
#include <string>

#include "cli/dlna_status.hpp"

namespace {

constexpr char const* description_xml = R"(<?xml version="1.0" encoding="utf-8"?>
<root xmlns="urn:schemas-upnp-org:device-1-0">
  <specVersion><major>1</major><minor>0</minor></specVersion>
  <device>
    <deviceType>urn:schemas-upnp-org:device:MediaServer:1</deviceType>
    <friendlyName>Sonarium DLNA</friendlyName>
    <manufacturer>Sonarium</manufacturer>
    <modelName>Sonarium MediaServer</modelName>
    <modelNumber>0.1.0</modelNumber>
    <UDN>uuid:11111111-2222-3333-4444-555555555555</UDN>
  </device>
</root>
)";

constexpr char const* browse_response = R"(<?xml version="1.0"?>
<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/">
  <s:Body>
    <u:BrowseResponse xmlns:u="urn:schemas-upnp-org:service:ContentDirectory:1">
      <Result>&lt;DIDL-Lite&gt;&lt;container id="album:1"/&gt;&lt;/DIDL-Lite&gt;</Result>
      <NumberReturned>3</NumberReturned>
      <TotalMatches>17</TotalMatches>
      <UpdateID>42</UpdateID>
    </u:BrowseResponse>
  </s:Body>
</s:Envelope>
)";

constexpr char const* soap_fault = R"(<?xml version="1.0"?>
<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/">
  <s:Body>
    <s:Fault>
      <faultcode>s:Client</faultcode>
      <faultstring>UPnPError</faultstring>
    </s:Fault>
  </s:Body>
</s:Envelope>
)";

} // namespace

TEST_CASE("extract_xml_text pulls a top-level value", "[cli][dlna]") {
    REQUIRE(sonarium::cli::extract_xml_text(description_xml, "friendlyName") == "Sonarium DLNA");
    REQUIRE(sonarium::cli::extract_xml_text(description_xml, "modelName")
            == "Sonarium MediaServer");
    REQUIRE(sonarium::cli::extract_xml_text(description_xml, "UDN")
            == "uuid:11111111-2222-3333-4444-555555555555");
}

TEST_CASE("extract_xml_text tolerates namespace prefixes", "[cli][dlna]") {
    constexpr char const* prefixed =
        R"(<u:BrowseResponse><u:UpdateID>9</u:UpdateID></u:BrowseResponse>)";
    REQUIRE(sonarium::cli::extract_xml_text(prefixed, "UpdateID") == "9");
}

TEST_CASE("extract_xml_text returns empty for unknown tag", "[cli][dlna]") {
    REQUIRE(sonarium::cli::extract_xml_text(description_xml, "NoSuchTag").empty());
}

TEST_CASE("parse_description_xml fills the device fields", "[cli][dlna]") {
    auto const r = sonarium::cli::parse_description_xml(description_xml);
    REQUIRE(r.has_value());
    REQUIRE(r->friendly_name == "Sonarium DLNA");
    REQUIRE(r->udn == "uuid:11111111-2222-3333-4444-555555555555");
    REQUIRE(r->model_name == "Sonarium MediaServer");
    REQUIRE(r->model_number == "0.1.0");
    // Browse-derived fields stay zero until parse_browse_response runs.
    REQUIRE(r->total_matches == 0);
    REQUIRE(r->system_update_id == 0);
}

TEST_CASE("parse_description_xml errors when UDN is missing", "[cli][dlna]") {
    constexpr char const* nope = "<root><friendlyName>x</friendlyName></root>";
    auto const r = sonarium::cli::parse_description_xml(nope);
    REQUIRE_FALSE(r.has_value());
}

TEST_CASE("build_browse_request embeds object id and counts", "[cli][dlna]") {
    auto const body = sonarium::cli::build_browse_request("0", 0, 50);
    REQUIRE(body.find("<ObjectID>0</ObjectID>") != std::string::npos);
    REQUIRE(body.find("<StartingIndex>0</StartingIndex>") != std::string::npos);
    REQUIRE(body.find("<RequestedCount>50</RequestedCount>") != std::string::npos);
    REQUIRE(body.find("<BrowseFlag>BrowseDirectChildren</BrowseFlag>") != std::string::npos);
    REQUIRE(body.find("urn:schemas-upnp-org:service:ContentDirectory:1") != std::string::npos);
}

TEST_CASE("parse_browse_response pulls counts and update id", "[cli][dlna]") {
    auto const r = sonarium::cli::parse_browse_response(browse_response);
    REQUIRE(r.has_value());
    REQUIRE(r->total_matches == 17);
    REQUIRE(r->number_returned == 3);
    REQUIRE(r->update_id == 42);
}

TEST_CASE("parse_browse_response surfaces SOAP faults", "[cli][dlna]") {
    auto const r = sonarium::cli::parse_browse_response(soap_fault);
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().find("UPnPError") != std::string::npos);
}
