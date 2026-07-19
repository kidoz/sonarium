#include <catch2/catch_test_macros.hpp>

#include "upnp/scpd_documents.hpp"

using sonarium::upnp::connection_manager_scpd_xml;
using sonarium::upnp::content_directory_scpd_xml;

TEST_CASE("ContentDirectory SCPD declares the four required actions", "[upnp][scpd]") {
    auto const xml = content_directory_scpd_xml();
    REQUIRE(xml.contains("<scpd xmlns=\"urn:schemas-upnp-org:service-1-0\">"));
    REQUIRE(xml.contains("<name>GetSearchCapabilities</name>"));
    REQUIRE(xml.contains("<name>GetSortCapabilities</name>"));
    REQUIRE(xml.contains("<name>GetSystemUpdateID</name>"));
    REQUIRE(xml.contains("<name>Browse</name>"));
}

TEST_CASE("ContentDirectory SCPD declares Browse arguments", "[upnp][scpd]") {
    auto const xml = content_directory_scpd_xml();
    for (const auto* name : {"ObjectID",
                             "BrowseFlag",
                             "Filter",
                             "StartingIndex",
                             "RequestedCount",
                             "SortCriteria",
                             "Result",
                             "NumberReturned",
                             "TotalMatches",
                             "UpdateID"}) {
        std::string const tag = std::string("<name>") + name + "</name>";
        REQUIRE(xml.contains(tag));
    }
}

TEST_CASE("ConnectionManager SCPD declares the three required actions", "[upnp][scpd]") {
    auto const xml = connection_manager_scpd_xml();
    REQUIRE(xml.contains("<name>GetProtocolInfo</name>"));
    REQUIRE(xml.contains("<name>GetCurrentConnectionIDs</name>"));
    REQUIRE(xml.contains("<name>GetCurrentConnectionInfo</name>"));
}

TEST_CASE("ConnectionManager SCPD declares Source/Sink protocol-info vars", "[upnp][scpd]") {
    auto const xml = connection_manager_scpd_xml();
    REQUIRE(xml.contains("<name>SourceProtocolInfo</name>"));
    REQUIRE(xml.contains("<name>SinkProtocolInfo</name>"));
}
