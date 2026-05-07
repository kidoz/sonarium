#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <string>

#include "catalog/in_memory_repository.hpp"
#include "composition/dlna_server.hpp"
#include "composition/injector.hpp"
#include "composition/service_config.hpp"
#include "core/null_logger.hpp"
#include "dlna-core/connection_manager_handler.hpp"
#include "dlna-core/device_profile.hpp"
#include "media/media_rendition.hpp"

using sonarium::catalog::Album;
using sonarium::catalog::Artist;
using sonarium::catalog::InMemoryRepository;
using sonarium::catalog::Repository;
using sonarium::catalog::Track;
using sonarium::composition::make_dlna_server;
using sonarium::composition::ServiceConfig;
using sonarium::dlna::DeviceProfileRegistry;
using sonarium::media::AudioCodec;
using sonarium::media::AudioContainer;
using sonarium::media::MediaRendition;

namespace {

std::shared_ptr<Repository> sample_repo() {
    auto repo = std::make_shared<InMemoryRepository>();

    Artist a;
    a.id = "1";
    a.name = "Stones";
    repo->add_artist(a);

    Album al;
    al.id = "1";
    al.artist_id = "1";
    al.title = "Let It Bleed";
    repo->add_album(al);

    Track t;
    t.id = "1";
    t.album_id = "1";
    t.artist_id = "1";
    t.title = "Gimme Shelter";
    t.duration_ms = 270'000;
    t.track_number = std::uint16_t{1};
    repo->add_track(t);

    MediaRendition r;
    r.id = "r1";
    r.track_id = "1";
    r.codec = AudioCodec::mp3;
    r.container = AudioContainer::mp3;
    r.bitrate_bps = 320'000;
    r.duration_ms = 270'000;
    repo->add_rendition(r);

    return repo;
}

ServiceConfig sample_config() {
    ServiceConfig cfg;
    cfg.device.friendly_name = "Sonarium";
    cfg.device.manufacturer = "Kidoz";
    cfg.device.model_name = "sonarium-dlna";
    cfg.device.udn = "uuid:abcdef01-2345-6789-abcd-ef0123456789";
    cfg.base_url = "http://192.168.1.10:8200";
    cfg.protocol_info_catalog = sonarium::dlna::default_protocol_info_catalog();
    return cfg;
}

constexpr std::string_view browse_root_request = R"(<?xml version="1.0" encoding="utf-8"?>
<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/">
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

constexpr std::string_view browse_album_request = R"(<?xml version="1.0" encoding="utf-8"?>
<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/">
  <s:Body>
    <u:Browse xmlns:u="urn:schemas-upnp-org:service:ContentDirectory:1">
      <ObjectID>album:1</ObjectID>
      <BrowseFlag>BrowseDirectChildren</BrowseFlag>
      <Filter>*</Filter>
      <StartingIndex>0</StartingIndex>
      <RequestedCount>200</RequestedCount>
      <SortCriteria></SortCriteria>
    </u:Browse>
  </s:Body>
</s:Envelope>)";

constexpr std::string_view get_protocol_info_request = R"(<?xml version="1.0" encoding="utf-8"?>
<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/">
  <s:Body>
    <u:GetProtocolInfo xmlns:u="urn:schemas-upnp-org:service:ConnectionManager:1"/>
  </s:Body>
</s:Envelope>)";

constexpr std::string_view get_system_update_id_request = R"(<?xml version="1.0" encoding="utf-8"?>
<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/">
  <s:Body>
    <u:GetSystemUpdateID xmlns:u="urn:schemas-upnp-org:service:ContentDirectory:1"/>
  </s:Body>
</s:Envelope>)";

constexpr std::string_view unknown_service_request = R"(<?xml version="1.0" encoding="utf-8"?>
<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/">
  <s:Body>
    <u:Reboot xmlns:u="urn:schemas-upnp-org:service:Mystery:1"/>
  </s:Body>
</s:Envelope>)";

} // namespace

TEST_CASE("Injector resolves a non-null DlnaServer", "[composition]") {
    auto server = make_dlna_server(sample_repo(), sample_config());
    REQUIRE(server != nullptr);
}

TEST_CASE("description.xml carries the configured friendly name", "[composition]") {
    auto server = make_dlna_server(sample_repo(), sample_config());
    auto const xml = server->description_xml();
    REQUIRE(xml.find("<friendlyName>Sonarium</friendlyName>") != std::string::npos);
    REQUIRE(xml.find("<UDN>uuid:abcdef01-2345-6789-abcd-ef0123456789</UDN>") != std::string::npos);
    REQUIRE(xml.find("<deviceType>urn:schemas-upnp-org:device:MediaServer:1</deviceType>")
            != std::string::npos);
}

TEST_CASE("SCPD getters return the canonical UPnP service descriptors", "[composition]") {
    using sonarium::composition::DlnaServer;
    REQUIRE(DlnaServer::content_directory_scpd().find("<name>Browse</name>")
            != std::string_view::npos);
    REQUIRE(DlnaServer::connection_manager_scpd().find("<name>GetProtocolInfo</name>")
            != std::string_view::npos);
}

TEST_CASE("Browse(0) round-trips through the SOAP pipeline", "[composition]") {
    auto server = make_dlna_server(sample_repo(), sample_config());
    auto const response = server->dispatch_soap(browse_root_request, "VLC/3.0");

    REQUIRE(response.find("<u:BrowseResponse xmlns:u=\""
                          "urn:schemas-upnp-org:service:ContentDirectory:1\">")
            != std::string::npos);
    REQUIRE(response.find("<NumberReturned>2</NumberReturned>") != std::string::npos);
    // DIDL-Lite is XML-escaped inside <Result> per UPnP convention.
    REQUIRE(response.find("&lt;DIDL-Lite") != std::string::npos);
    REQUIRE(response.find("Music") != std::string::npos);
    REQUIRE(response.find("Playlists") != std::string::npos);
}

TEST_CASE("Browse(album:1) picks profile-appropriate resource URL", "[composition]") {
    auto server = make_dlna_server(sample_repo(), sample_config());
    auto const response = server->dispatch_soap(browse_album_request, "VLC/3.0");

    REQUIRE(response.find("<NumberReturned>1</NumberReturned>") != std::string::npos);
    REQUIRE(response.find("Gimme Shelter") != std::string::npos);
    REQUIRE(response.find("http://192.168.1.10:8200/media/renditions/r1") != std::string::npos);
}

TEST_CASE("GetProtocolInfo returns a Source CSV with audio MIMEs", "[composition]") {
    auto server = make_dlna_server(sample_repo(), sample_config());
    auto const response = server->dispatch_soap(get_protocol_info_request, "VLC/3.0");

    REQUIRE(response.find("<u:GetProtocolInfoResponse xmlns:u=\""
                          "urn:schemas-upnp-org:service:ConnectionManager:1\">")
            != std::string::npos);
    REQUIRE(response.find("audio/mpeg") != std::string::npos);
    REQUIRE(response.find("audio/flac") != std::string::npos); // VLC profile exposes FLAC
    REQUIRE(response.find("<Sink></Sink>") != std::string::npos);
}

TEST_CASE("GetSystemUpdateID returns the catalog counter", "[composition]") {
    auto server = make_dlna_server(sample_repo(), sample_config());
    auto const response = server->dispatch_soap(get_system_update_id_request, "VLC/3.0");
    REQUIRE(response.find("<Id>0</Id>") != std::string::npos);
}

TEST_CASE("Unknown service URN yields a SOAP fault", "[composition]") {
    auto server = make_dlna_server(sample_repo(), sample_config());
    auto const response = server->dispatch_soap(unknown_service_request, "VLC/3.0");
    REQUIRE(response.find("<faultcode>s:Client</faultcode>") != std::string::npos);
    REQUIRE(response.find("<errorCode>401</errorCode>") != std::string::npos);
}

TEST_CASE("Malformed SOAP body yields a fault", "[composition]") {
    auto server = make_dlna_server(sample_repo(), sample_config());
    auto const response = server->dispatch_soap("not a soap envelope", "VLC/3.0");
    REQUIRE(response.find("<faultcode>s:Client</faultcode>") != std::string::npos);
    REQUIRE(response.find("<errorCode>402</errorCode>") != std::string::npos);
}

TEST_CASE("Custom profile registry is honored for resource selection", "[composition]") {
    // A profile registry that exposes only MP3 and forbids DLNA.ORG_PN — verifies
    // our injector wires the profile registry as a true singleton dependency.
    auto profiles = std::make_shared<DeviceProfileRegistry>();
    sonarium::dlna::DeviceProfile only_mp3;
    only_mp3.name = "OnlyMP3";
    only_mp3.preferred_codec_order = {AudioCodec::mp3};
    only_mp3.requires_dlna_org_pn = false;
    profiles->add(only_mp3);

    auto server = make_dlna_server(
        sample_repo(), profiles, sonarium::core::make_null_logger(), sample_config());
    auto const response = server->dispatch_soap(browse_album_request, "anything");

    REQUIRE(response.find("Gimme Shelter") != std::string::npos);
    REQUIRE(response.find("DLNA.ORG_PN=") == std::string::npos);
}
