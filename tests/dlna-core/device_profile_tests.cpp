#include <array>
#include <catch2/catch_test_macros.hpp>
#include <utility>

#include "dlna-core/device_profile.hpp"
#include "media/mime_type.hpp"

using sonarium::dlna::DeviceProfile;
using sonarium::dlna::DeviceProfileRegistry;
using sonarium::dlna::mime_for;
using sonarium::dlna::profile_supports_codec;
using sonarium::dlna::RequestHeaders;
using sonarium::media::AudioCodec;
using sonarium::media::AudioContainer;
using sonarium::media::RenditionMime;

TEST_CASE("Default registry exposes all expected profiles", "[dlna][profile]") {
    auto const reg = DeviceProfileRegistry::with_defaults();
    auto const ps = reg.profiles();
    REQUIRE(ps.size() >= 7);
    bool has_generic = false;
    bool has_samsung = false;
    bool has_lg = false;
    bool has_sony = false;
    bool has_receiver = false;
    bool has_vlc_kodi = false;
    bool has_bubble = false;
    for (auto const& p : ps) {
        if (p.name == "Generic DLNA") {
            has_generic = true;
        } else if (p.name == "Samsung TV") {
            has_samsung = true;
        } else if (p.name == "LG TV") {
            has_lg = true;
        } else if (p.name == "Sony TV/Speaker") {
            has_sony = true;
        } else if (p.name == "Yamaha/Denon/Marantz") {
            has_receiver = true;
        } else if (p.name == "VLC/Kodi") {
            has_vlc_kodi = true;
        } else if (p.name == "BubbleUPnP") {
            has_bubble = true;
        }
    }
    REQUIRE(has_generic);
    REQUIRE(has_samsung);
    REQUIRE(has_lg);
    REQUIRE(has_sony);
    REQUIRE(has_receiver);
    REQUIRE(has_vlc_kodi);
    REQUIRE(has_bubble);
}

TEST_CASE("VLC user-agent matches VLC/Kodi profile", "[dlna][profile]") {
    auto const reg = DeviceProfileRegistry::with_defaults();
    RequestHeaders h{"VLC/3.0.20 LibVLC/3.0.20", {}};
    REQUIRE(reg.match(h).name == "VLC/Kodi");
}

TEST_CASE("Kodi/XBMC user-agent matches VLC/Kodi profile", "[dlna][profile]") {
    auto const reg = DeviceProfileRegistry::with_defaults();
    REQUIRE(reg.match(RequestHeaders{"Kodi/20.2", {}}).name == "VLC/Kodi");
    REQUIRE(reg.match(RequestHeaders{"XBMC/19.0", {}}).name == "VLC/Kodi");
}

TEST_CASE("Samsung TV matches by user-agent or X-AV-Client-Info header", "[dlna][profile]") {
    auto const reg = DeviceProfileRegistry::with_defaults();
    REQUIRE(reg.match(RequestHeaders{"SEC_HHP_TV/1.0", {}}).name == "Samsung TV");

    std::array<std::pair<std::string_view, std::string_view>, 1> extras{{
        {"X-AV-Client-Info", "av: 5.0; cn: \"Samsung\"; mn: \"DMP\"; mv: \"2.0\";"},
    }};
    REQUIRE(reg.match(RequestHeaders{"unknown-ua", extras}).name == "Samsung TV");
}

TEST_CASE("BubbleUPnP and receiver and Sony match cleanly", "[dlna][profile]") {
    auto const reg = DeviceProfileRegistry::with_defaults();
    REQUIRE(reg.match(RequestHeaders{"BubbleUPnP/4.6", {}}).name == "BubbleUPnP");
    REQUIRE(reg.match(RequestHeaders{"Yamaha-MusicCast/2.0", {}}).name == "Yamaha/Denon/Marantz");
    REQUIRE(reg.match(RequestHeaders{"BRAVIA/2024", {}}).name == "Sony TV/Speaker");
}

TEST_CASE("Unknown user-agent falls back to Generic DLNA", "[dlna][profile]") {
    auto const reg = DeviceProfileRegistry::with_defaults();
    REQUIRE(reg.match(RequestHeaders{"random-renderer/1.0", {}}).name == "Generic DLNA");
    REQUIRE(reg.match(RequestHeaders{"", {}}).name == "Generic DLNA");
}

TEST_CASE("profile_supports_codec reflects preferred order", "[dlna][profile]") {
    auto const reg = DeviceProfileRegistry::with_defaults();
    auto const& vlc = reg.match(RequestHeaders{"VLC/3", {}});
    REQUIRE(profile_supports_codec(vlc, AudioCodec::flac));
    REQUIRE(profile_supports_codec(vlc, AudioCodec::mp3));
    REQUIRE_FALSE(profile_supports_codec(vlc, AudioCodec::pcm_wav));
}

TEST_CASE("mime_for returns the codec default when no override", "[dlna][profile]") {
    DeviceProfile p;
    p.name = "T";
    REQUIRE(mime_for(p, RenditionMime{AudioCodec::mp3, AudioContainer::mp3}) == "audio/mpeg");
    REQUIRE(mime_for(p, RenditionMime{AudioCodec::flac, AudioContainer::flac}) == "audio/flac");
}

TEST_CASE("mime_for honors per-profile overrides", "[dlna][profile]") {
    DeviceProfile p;
    p.name = "OverrideExample";
    p.mime_overrides.emplace_back(AudioCodec::aac_lc, "audio/x-m4a");
    REQUIRE(mime_for(p, RenditionMime{AudioCodec::aac_lc, AudioContainer::mp4}) == "audio/x-m4a");
    REQUIRE(mime_for(p, RenditionMime{AudioCodec::mp3, AudioContainer::mp3}) == "audio/mpeg");
}

TEST_CASE("Header substring matching is case-insensitive", "[dlna][profile]") {
    DeviceProfileRegistry r;
    DeviceProfile prof;
    prof.name = "TestProfile";
    prof.user_agent_substrings = {"foo"};
    r.add(prof);
    DeviceProfile generic;
    generic.name = "Generic DLNA";
    r.add(generic);
    REQUIRE(r.match(RequestHeaders{"FOO/1.0", {}}).name == "TestProfile");
    REQUIRE(r.match(RequestHeaders{"baz/1", {}}).name == "Generic DLNA");
}
