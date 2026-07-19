#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "dlna-core/device_profile.hpp"
#include "dlna-core/media_resource_selector.hpp"
#include "media/media_rendition.hpp"

using sonarium::dlna::DeviceProfile;
using sonarium::dlna::DeviceProfileRegistry;
using sonarium::dlna::RequestHeaders;
using sonarium::dlna::ResourceSelectionContext;
using sonarium::dlna::select_resources;
using sonarium::media::AudioCodec;
using sonarium::media::AudioContainer;
using sonarium::media::MediaRendition;

namespace {

MediaRendition make_rendition(std::string id,
                              AudioCodec codec,
                              AudioContainer container,
                              std::uint32_t bitrate_bps = 0,
                              std::uint32_t sample_rate_hz = 0,
                              std::uint8_t channels = 0,
                              std::uint64_t size_bytes = 0) {
    MediaRendition r;
    r.id = std::move(id);
    r.codec = codec;
    r.container = container;
    r.bitrate_bps = bitrate_bps;
    r.sample_rate_hz = sample_rate_hz;
    r.channels = channels;
    r.size_bytes = size_bytes;
    return r;
}

} // namespace

TEST_CASE("Generic profile picks MP3 first, then AAC, then WAV", "[dlna][resource_selector]") {
    auto const reg = DeviceProfileRegistry::with_defaults();
    auto const& generic = reg.match(RequestHeaders{"Unknown/1", {}});

    std::vector<MediaRendition> renditions = {
        make_rendition("wav-1", AudioCodec::pcm_wav, AudioContainer::wav, 1411200, 44100, 2),
        make_rendition("mp3-1", AudioCodec::mp3, AudioContainer::mp3, 320000, 44100, 2),
        make_rendition("aac-1", AudioCodec::aac_lc, AudioContainer::mp4, 256000, 44100, 2),
    };

    ResourceSelectionContext ctx;
    ctx.base_url = "http://192.168.1.10:8200";
    auto const out = select_resources(renditions, generic, ctx);
    REQUIRE(out.size() == 3);
    REQUIRE(out[0].url == "http://192.168.1.10:8200/media/renditions/mp3-1");
    REQUIRE(out[1].url == "http://192.168.1.10:8200/media/renditions/aac-1");
    REQUIRE(out[2].url == "http://192.168.1.10:8200/media/renditions/wav-1");
}

TEST_CASE("FLAC is dropped for profiles that don't expose lossless", "[dlna][resource_selector]") {
    auto const reg = DeviceProfileRegistry::with_defaults();
    auto const& samsung = reg.match(RequestHeaders{"SEC_HHP_TV/1", {}});

    std::vector<MediaRendition> renditions = {
        make_rendition("flac-1", AudioCodec::flac, AudioContainer::flac),
        make_rendition("mp3-1", AudioCodec::mp3, AudioContainer::mp3),
    };

    ResourceSelectionContext ctx;
    ctx.base_url = "http://lan.invalid:8200/";
    auto const out = select_resources(renditions, samsung, ctx);
    REQUIRE(out.size() == 1);
    REQUIRE(out[0].url == "http://lan.invalid:8200/media/renditions/mp3-1");
}

TEST_CASE("VLC/Kodi picks FLAC first when available", "[dlna][resource_selector]") {
    auto const reg = DeviceProfileRegistry::with_defaults();
    auto const& vlc = reg.match(RequestHeaders{"VLC/3.0", {}});

    std::vector<MediaRendition> renditions = {
        make_rendition("mp3-1", AudioCodec::mp3, AudioContainer::mp3),
        make_rendition("flac-1", AudioCodec::flac, AudioContainer::flac),
    };

    ResourceSelectionContext ctx;
    ctx.base_url = "http://h:8200";
    auto const out = select_resources(renditions, vlc, ctx);
    REQUIRE(out.size() == 2);
    REQUIRE(out[0].url == "http://h:8200/media/renditions/flac-1");
    REQUIRE(out[1].url == "http://h:8200/media/renditions/mp3-1");
}

TEST_CASE("DLNA.ORG_PN omitted when profile says so", "[dlna][resource_selector]") {
    auto const reg = DeviceProfileRegistry::with_defaults();
    auto const& vlc = reg.match(RequestHeaders{"Kodi/20", {}}); // requires_dlna_org_pn = false

    std::vector<MediaRendition> renditions = {
        make_rendition("mp3-1", AudioCodec::mp3, AudioContainer::mp3),
    };
    ResourceSelectionContext ctx;
    ctx.base_url = "http://h:8200";
    auto const out = select_resources(renditions, vlc, ctx);
    REQUIRE(out.size() == 1);
    REQUIRE(!out[0].protocol_info.contains("DLNA.ORG_PN="));
}

TEST_CASE("Selector preserves attribute fields when present", "[dlna][resource_selector]") {
    auto const reg = DeviceProfileRegistry::with_defaults();
    auto const& generic = reg.match(RequestHeaders{"u", {}});
    std::vector<MediaRendition> renditions = {
        make_rendition("mp3-1", AudioCodec::mp3, AudioContainer::mp3, 320000, 44100, 2, 12345678),
    };
    ResourceSelectionContext ctx;
    ctx.base_url = "http://h:8200";
    auto const out = select_resources(renditions, generic, ctx);
    REQUIRE(out.size() == 1);
    REQUIRE(out[0].bitrate_bps.value_or(0) == 320000);
    REQUIRE(out[0].sample_rate_hz.value_or(0) == 44100);
    REQUIRE(out[0].channels.value_or(0) == 2);
    REQUIRE(out[0].size_bytes.value_or(0) == 12345678);
}

TEST_CASE("Empty rendition list returns empty selection", "[dlna][resource_selector]") {
    auto const reg = DeviceProfileRegistry::with_defaults();
    auto const& generic = reg.match(RequestHeaders{"u", {}});
    ResourceSelectionContext const ctx;
    auto const out = select_resources(std::vector<MediaRendition>{}, generic, ctx);
    REQUIRE(out.empty());
}

TEST_CASE("Codec not in profile is dropped even if rendition exists", "[dlna][resource_selector]") {
    DeviceProfile only_mp3;
    only_mp3.name = "OnlyMP3";
    only_mp3.preferred_codec_order = {AudioCodec::mp3};
    only_mp3.exposes_flac = false;

    std::vector<MediaRendition> renditions = {
        make_rendition("aac-1", AudioCodec::aac_lc, AudioContainer::mp4),
        make_rendition("mp3-1", AudioCodec::mp3, AudioContainer::mp3),
    };
    ResourceSelectionContext ctx;
    ctx.base_url = "http://h";
    auto const out = select_resources(renditions, only_mp3, ctx);
    REQUIRE(out.size() == 1);
    REQUIRE(out[0].url == "http://h/media/renditions/mp3-1");
}
