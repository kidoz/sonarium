#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "dlna-core/protocol_info.hpp"
#include "media/mime_type.hpp"

using sonarium::dlna::build_protocol_info;
using sonarium::dlna::join_protocol_info_csv;
using sonarium::dlna::ProtocolInfoOptions;
using sonarium::media::AudioCodec;
using sonarium::media::AudioContainer;
using sonarium::media::RenditionMime;

TEST_CASE("MP3 320k carries DLNA.ORG_PN=MP3", "[dlna][protocol_info]") {
    RenditionMime const mp3{AudioCodec::mp3, AudioContainer::mp3};
    REQUIRE(build_protocol_info(mp3)
            == "http-get:*:audio/mpeg:DLNA.ORG_PN=MP3;DLNA.ORG_OP=01;DLNA.ORG_CI=0");
}

TEST_CASE("AAC-LC has no DLNA.ORG_PN by default", "[dlna][protocol_info]") {
    RenditionMime const aac{AudioCodec::aac_lc, AudioContainer::mp4};
    REQUIRE(build_protocol_info(aac) == "http-get:*:audio/mp4:DLNA.ORG_OP=01;DLNA.ORG_CI=0");
}

TEST_CASE("FLAC profile-aware suppression of DLNA.ORG_PN", "[dlna][protocol_info]") {
    RenditionMime const flac{AudioCodec::flac, AudioContainer::flac};
    ProtocolInfoOptions opts;
    opts.include_dlna_org_pn = false;
    REQUIRE(build_protocol_info(flac, opts)
            == "http-get:*:audio/flac:DLNA.ORG_OP=01;DLNA.ORG_CI=0");
}

TEST_CASE("WAV/LPCM gets LPCM PN", "[dlna][protocol_info]") {
    RenditionMime const wav{AudioCodec::pcm_wav, AudioContainer::wav};
    REQUIRE(build_protocol_info(wav)
            == "http-get:*:audio/wav:DLNA.ORG_PN=LPCM;DLNA.ORG_OP=01;DLNA.ORG_CI=0");
}

TEST_CASE("join_protocol_info_csv joins with commas, no trailing", "[dlna][protocol_info]") {
    REQUIRE(join_protocol_info_csv({}).empty());
    REQUIRE(join_protocol_info_csv({"a"}) == "a");
    REQUIRE(join_protocol_info_csv({"a", "b", "c"}) == "a,b,c");
}
