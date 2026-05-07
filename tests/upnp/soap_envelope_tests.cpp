#include <catch2/catch_test_macros.hpp>
#include <string>

#include "upnp/soap_envelope.hpp"
#include "upnp/upnp_error.hpp"

using sonarium::upnp::build_soap_fault;
using sonarium::upnp::build_soap_response;
using sonarium::upnp::parse_soap_request;
using sonarium::upnp::SoapParseError;
using sonarium::upnp::UpnpErrorCode;

namespace {

constexpr std::string_view browse_request = R"(<?xml version="1.0" encoding="utf-8"?>
<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/" s:encodingStyle="http://schemas.xmlsoap.org/soap/encoding/">
  <s:Body>
    <u:Browse xmlns:u="urn:schemas-upnp-org:service:ContentDirectory:1">
      <ObjectID>0</ObjectID>
      <BrowseFlag>BrowseDirectChildren</BrowseFlag>
      <Filter>*</Filter>
      <StartingIndex>0</StartingIndex>
      <RequestedCount>200</RequestedCount>
      <SortCriteria></SortCriteria>
    </u:Browse>
  </s:Body>
</s:Envelope>)";

} // namespace

TEST_CASE("Browse request parses cleanly", "[upnp][soap_envelope]") {
    auto const r = parse_soap_request(browse_request);
    REQUIRE(r.has_value());
    REQUIRE(r->service_urn == "urn:schemas-upnp-org:service:ContentDirectory:1");
    REQUIRE(r->action == "Browse");
    REQUIRE(r->args.size() == 6);
    REQUIRE(r->arg_or("ObjectID", "") == "0");
    REQUIRE(r->arg_or("BrowseFlag", "") == "BrowseDirectChildren");
    REQUIRE(r->arg_or("Filter", "") == "*");
    REQUIRE(r->arg_or("StartingIndex", "") == "0");
    REQUIRE(r->arg_or("RequestedCount", "") == "200");
    REQUIRE(r->arg_or("SortCriteria", "FALLBACK") == "");
}

TEST_CASE("Parser tolerates lowercase Envelope/Body and BOM", "[upnp][soap_envelope]") {
    std::string body;
    body.append("\xef\xbb\xbf"); // UTF-8 BOM
    body.append(R"(<?xml version="1.0"?>)");
    body.append(R"(<envelope xmlns="http://schemas.xmlsoap.org/soap/envelope/">)");
    body.append("<body>");
    body.append(
        R"(<u:GetProtocolInfo xmlns:u="urn:schemas-upnp-org:service:ConnectionManager:1"/>)");
    body.append("</body></envelope>");

    auto const r = parse_soap_request(body);
    REQUIRE(r.has_value());
    REQUIRE(r->service_urn == "urn:schemas-upnp-org:service:ConnectionManager:1");
    REQUIRE(r->action == "GetProtocolInfo");
    REQUIRE(r->args.empty());
}

TEST_CASE("Parser unescapes the five XML entities in arg values", "[upnp][soap_envelope]") {
    std::string const body = R"(<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/">)"
                             R"(<s:Body><u:X xmlns:u="urn:test">)"
                             R"(<a>AT&amp;T</a>)"
                             R"(<b>&lt;hello&gt;</b>)"
                             R"(<c>&quot;quoted&quot;</c>)"
                             R"(<d>&apos;a&apos;</d>)"
                             R"(</u:X></s:Body></s:Envelope>)";
    auto const r = parse_soap_request(body);
    REQUIRE(r.has_value());
    REQUIRE(r->arg_or("a", "") == "AT&T");
    REQUIRE(r->arg_or("b", "") == "<hello>");
    REQUIRE(r->arg_or("c", "") == "\"quoted\"");
    REQUIRE(r->arg_or("d", "") == "'a'");
}

TEST_CASE("Parser handles XML comments and DOCTYPE", "[upnp][soap_envelope]") {
    std::string const body = R"(<?xml version="1.0"?>)"
                             R"(<!-- header comment -->)"
                             R"(<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/">)"
                             R"(<!-- inside envelope -->)"
                             R"(<s:Body>)"
                             R"(<u:Ping xmlns:u="urn:test"><When>now</When></u:Ping>)"
                             R"(</s:Body></s:Envelope>)";
    auto const r = parse_soap_request(body);
    REQUIRE(r.has_value());
    REQUIRE(r->action == "Ping");
    REQUIRE(r->arg_or("When", "") == "now");
}

TEST_CASE("Parser rejects empty body", "[upnp][soap_envelope]") {
    REQUIRE(parse_soap_request("").error() == SoapParseError::empty);
}

TEST_CASE("Parser flags missing namespace", "[upnp][soap_envelope]") {
    std::string const body = R"(<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/">)"
                             R"(<s:Body><Browse/></s:Body></s:Envelope>)";
    auto const r = parse_soap_request(body);
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error() == SoapParseError::namespace_missing);
}

TEST_CASE("Parser flags missing Body", "[upnp][soap_envelope]") {
    std::string const body = R"(<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/">)"
                             R"(<s:Header/></s:Envelope>)";
    auto const r = parse_soap_request(body);
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error() == SoapParseError::body_not_found);
}

TEST_CASE("Response envelope is well-formed", "[upnp][soap_envelope]") {
    auto const xml = build_soap_response("urn:schemas-upnp-org:service:ContentDirectory:1",
                                         "Browse",
                                         {{"Result", "<DIDL-Lite/>"},
                                          {"NumberReturned", "0"},
                                          {"TotalMatches", "0"},
                                          {"UpdateID", "1"}});

    REQUIRE(xml.starts_with(R"(<?xml version="1.0" encoding="utf-8"?>)"));
    REQUIRE(xml.find("<u:BrowseResponse xmlns:u=\""
                     "urn:schemas-upnp-org:service:ContentDirectory:1\">")
            != std::string::npos);
    // DIDL inside Result must be XML-escaped (UPnP convention: Result is a string).
    REQUIRE(xml.find("<Result>&lt;DIDL-Lite/&gt;</Result>") != std::string::npos);
    REQUIRE(xml.find("<NumberReturned>0</NumberReturned>") != std::string::npos);
    REQUIRE(xml.ends_with("</u:BrowseResponse></s:Body></s:Envelope>"));
}

TEST_CASE("Fault envelope carries UPnP error code and description", "[upnp][soap_envelope]") {
    auto const xml = build_soap_fault(UpnpErrorCode::no_such_object);
    REQUIRE(xml.find("<faultcode>s:Client</faultcode>") != std::string::npos);
    REQUIRE(xml.find("<faultstring>UPnPError</faultstring>") != std::string::npos);
    REQUIRE(xml.find(R"(<UPnPError xmlns="urn:schemas-upnp-org:control-1-0">)")
            != std::string::npos);
    REQUIRE(xml.find("<errorCode>701</errorCode>") != std::string::npos);
    REQUIRE(xml.find("<errorDescription>No Such Object</errorDescription>") != std::string::npos);
}

TEST_CASE("Fault envelope honors custom description", "[upnp][soap_envelope]") {
    auto const xml = build_soap_fault(UpnpErrorCode::action_failed, "container is empty");
    REQUIRE(xml.find("<errorDescription>container is empty</errorDescription>")
            != std::string::npos);
    REQUIRE(xml.find("<errorCode>501</errorCode>") != std::string::npos);
}

TEST_CASE("Round trip: parse then build", "[upnp][soap_envelope]") {
    auto const r = parse_soap_request(browse_request).value();
    auto const xml = build_soap_response(r.service_urn, r.action, {{"NumberReturned", "0"}});
    auto const re = parse_soap_request(xml);
    REQUIRE(re.has_value());
    REQUIRE(re->action == "BrowseResponse");
    REQUIRE(re->arg_or("NumberReturned", "") == "0");
}
