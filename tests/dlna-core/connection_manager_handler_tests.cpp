#include <catch2/catch_test_macros.hpp>

#include "dlna-core/connection_manager_handler.hpp"
#include "dlna-core/device_profile.hpp"
#include "upnp/soap_envelope.hpp"
#include "upnp/upnp_error.hpp"

using sonarium::dlna::build_protocol_info_for;
using sonarium::dlna::default_protocol_info_catalog;
using sonarium::dlna::DeviceProfileRegistry;
using sonarium::dlna::handle_get_protocol_info;
using sonarium::dlna::RequestHeaders;
using sonarium::upnp::ParsedSoapRequest;
using sonarium::upnp::UpnpErrorCode;

TEST_CASE("Generic profile lists MP3, AAC, WAV", "[dlna][connection_manager]") {
    auto const reg = DeviceProfileRegistry::with_defaults();
    auto const& generic = reg.match(RequestHeaders{"Unknown/1", {}});
    auto const cat = default_protocol_info_catalog();
    auto const result = build_protocol_info_for(generic, cat);

    REQUIRE(result.source.find("audio/mpeg") != std::string::npos);
    REQUIRE(result.source.find("audio/mp4") != std::string::npos);
    REQUIRE(result.source.find("audio/wav") != std::string::npos);
    REQUIRE(result.source.find("audio/flac") == std::string::npos);
    REQUIRE(result.sink.empty());
}

TEST_CASE("VLC/Kodi profile exposes FLAC and omits DLNA.ORG_PN", "[dlna][connection_manager]") {
    auto const reg = DeviceProfileRegistry::with_defaults();
    auto const& vlc = reg.match(RequestHeaders{"VLC/3", {}});
    auto const cat = default_protocol_info_catalog();
    auto const result = build_protocol_info_for(vlc, cat);

    REQUIRE(result.source.find("audio/flac") != std::string::npos);
    REQUIRE(result.source.find("DLNA.ORG_PN=") == std::string::npos);
}

TEST_CASE("Samsung profile keeps DLNA.ORG_PN and omits FLAC", "[dlna][connection_manager]") {
    auto const reg = DeviceProfileRegistry::with_defaults();
    auto const& samsung = reg.match(RequestHeaders{"SEC_HHP_TV/1", {}});
    auto const cat = default_protocol_info_catalog();
    auto const result = build_protocol_info_for(samsung, cat);

    REQUIRE(result.source.find("DLNA.ORG_PN=MP3") != std::string::npos);
    REQUIRE(result.source.find("audio/flac") == std::string::npos);
}

TEST_CASE("CSV order matches profile preferred_codec_order", "[dlna][connection_manager]") {
    auto const reg = DeviceProfileRegistry::with_defaults();
    auto const& vlc = reg.match(RequestHeaders{"Kodi/20", {}});
    auto const cat = default_protocol_info_catalog();
    auto const result = build_protocol_info_for(vlc, cat);
    // VLC/Kodi order: flac, mp3, aac_lc
    auto const flac_pos = result.source.find("audio/flac");
    auto const mp3_pos = result.source.find("audio/mpeg");
    auto const aac_pos = result.source.find("audio/mp4");
    REQUIRE(flac_pos != std::string::npos);
    REQUIRE(mp3_pos != std::string::npos);
    REQUIRE(aac_pos != std::string::npos);
    REQUIRE(flac_pos < mp3_pos);
    REQUIRE(mp3_pos < aac_pos);
}

TEST_CASE("Handler returns Source/Sink pair for valid request", "[dlna][connection_manager]") {
    auto const reg = DeviceProfileRegistry::with_defaults();
    auto const& vlc = reg.match(RequestHeaders{"VLC/3", {}});
    auto const cat = default_protocol_info_catalog();

    ParsedSoapRequest req;
    req.service_urn = "urn:schemas-upnp-org:service:ConnectionManager:1";
    req.action = "GetProtocolInfo";

    auto const result = handle_get_protocol_info(req, vlc, cat);
    REQUIRE(result.has_value());
    REQUIRE(result->size() == 2);
    REQUIRE(result->at(0).first == "Source");
    REQUIRE(result->at(0).second.find("audio/flac") != std::string::npos);
    REQUIRE(result->at(1).first == "Sink");
    REQUIRE(result->at(1).second.empty());
}

TEST_CASE("Handler rejects wrong action", "[dlna][connection_manager]") {
    auto const reg = DeviceProfileRegistry::with_defaults();
    auto const& p = reg.match(RequestHeaders{"VLC/3", {}});
    auto const cat = default_protocol_info_catalog();

    ParsedSoapRequest req;
    req.action = "Browse";
    auto const r = handle_get_protocol_info(req, p, cat);
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error() == UpnpErrorCode::invalid_action);
}
