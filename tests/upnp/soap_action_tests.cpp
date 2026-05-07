#include <catch2/catch_test_macros.hpp>

#include "upnp/soap_action.hpp"

using sonarium::upnp::parse_soap_action_header;
using sonarium::upnp::SoapActionParseError;

TEST_CASE("Quoted SOAPACTION", "[upnp][soap_action]") {
    auto const r =
        parse_soap_action_header(R"("urn:schemas-upnp-org:service:ContentDirectory:1#Browse")");
    REQUIRE(r.has_value());
    REQUIRE(r->service_urn == "urn:schemas-upnp-org:service:ContentDirectory:1");
    REQUIRE(r->action == "Browse");
}

TEST_CASE("Unquoted SOAPACTION", "[upnp][soap_action]") {
    auto const r = parse_soap_action_header(
        "urn:schemas-upnp-org:service:ConnectionManager:1#GetProtocolInfo");
    REQUIRE(r.has_value());
    REQUIRE(r->service_urn == "urn:schemas-upnp-org:service:ConnectionManager:1");
    REQUIRE(r->action == "GetProtocolInfo");
}

TEST_CASE("SOAPACTION tolerates surrounding whitespace", "[upnp][soap_action]") {
    auto const r =
        parse_soap_action_header("  \"urn:schemas-upnp-org:service:ContentDirectory:1#Search\"  ");
    REQUIRE(r.has_value());
    REQUIRE(r->action == "Search");
}

TEST_CASE("SOAPACTION rejects empty input", "[upnp][soap_action]") {
    REQUIRE(parse_soap_action_header("").error() == SoapActionParseError::empty);
    REQUIRE(parse_soap_action_header("   ").error() == SoapActionParseError::empty);
    REQUIRE(parse_soap_action_header("\"\"").error() == SoapActionParseError::empty);
}

TEST_CASE("SOAPACTION rejects malformed values", "[upnp][soap_action]") {
    REQUIRE(parse_soap_action_header("no-hash-here").error()
            == SoapActionParseError::missing_separator);
    REQUIRE(parse_soap_action_header("#Browse").error() == SoapActionParseError::empty_service_urn);
    REQUIRE(parse_soap_action_header("urn:foo#").error() == SoapActionParseError::empty_action);
}
