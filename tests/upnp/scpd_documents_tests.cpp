#include <catch2/catch_test_macros.hpp>

#include "upnp/scpd_documents.hpp"

using sonarium::upnp::connection_manager_scpd_xml;
using sonarium::upnp::content_directory_scpd_xml;

TEST_CASE("ContentDirectory SCPD declares the four required actions", "[upnp][scpd]") {
    auto const xml = content_directory_scpd_xml();
    REQUIRE(xml.find("<scpd xmlns=\"urn:schemas-upnp-org:service-1-0\">")
            != std::string_view::npos);
    REQUIRE(xml.find("<name>GetSearchCapabilities</name>") != std::string_view::npos);
    REQUIRE(xml.find("<name>GetSortCapabilities</name>") != std::string_view::npos);
    REQUIRE(xml.find("<name>GetSystemUpdateID</name>") != std::string_view::npos);
    REQUIRE(xml.find("<name>Browse</name>") != std::string_view::npos);
}

TEST_CASE("ContentDirectory SCPD declares Browse arguments", "[upnp][scpd]") {
    auto const xml = content_directory_scpd_xml();
    for (auto* name : {"ObjectID",
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
        REQUIRE(xml.find(tag) != std::string_view::npos);
    }
}

TEST_CASE("ConnectionManager SCPD declares the three required actions", "[upnp][scpd]") {
    auto const xml = connection_manager_scpd_xml();
    REQUIRE(xml.find("<name>GetProtocolInfo</name>") != std::string_view::npos);
    REQUIRE(xml.find("<name>GetCurrentConnectionIDs</name>") != std::string_view::npos);
    REQUIRE(xml.find("<name>GetCurrentConnectionInfo</name>") != std::string_view::npos);
}

TEST_CASE("ConnectionManager SCPD declares Source/Sink protocol-info vars", "[upnp][scpd]") {
    auto const xml = connection_manager_scpd_xml();
    REQUIRE(xml.find("<name>SourceProtocolInfo</name>") != std::string_view::npos);
    REQUIRE(xml.find("<name>SinkProtocolInfo</name>") != std::string_view::npos);
}
