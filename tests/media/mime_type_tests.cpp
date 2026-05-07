#include <catch2/catch_test_macros.hpp>

#include "media/mime_type.hpp"

using sonarium::media::ascii_lowercase;
using sonarium::media::AudioCodec;
using sonarium::media::AudioContainer;
using sonarium::media::default_mime_for;
using sonarium::media::dlna_org_pn_for;
using sonarium::media::mime_from_extension;
using sonarium::media::RenditionMime;

TEST_CASE("default_mime_for known codecs", "[media][mime]") {
    REQUIRE(default_mime_for({AudioCodec::mp3, AudioContainer::mp3}) == "audio/mpeg");
    REQUIRE(default_mime_for({AudioCodec::aac_lc, AudioContainer::mp4}) == "audio/mp4");
    REQUIRE(default_mime_for({AudioCodec::pcm_wav, AudioContainer::wav}) == "audio/wav");
    REQUIRE(default_mime_for({AudioCodec::flac, AudioContainer::flac}) == "audio/flac");
    REQUIRE(default_mime_for({AudioCodec::alac, AudioContainer::mp4}) == "audio/mp4");
}

TEST_CASE("mime_from_extension handles dot prefix and case", "[media][mime]") {
    REQUIRE(mime_from_extension("mp3").value() == "audio/mpeg");
    REQUIRE(mime_from_extension(".MP3").value() == "audio/mpeg");
    REQUIRE(mime_from_extension("M4A").value() == "audio/mp4");
    REQUIRE(mime_from_extension(".flac").value() == "audio/flac");
    REQUIRE(mime_from_extension("jpeg").value() == "image/jpeg");
    REQUIRE(mime_from_extension("png").value() == "image/png");
}

TEST_CASE("mime_from_extension returns nullopt for unknown", "[media][mime]") {
    REQUIRE_FALSE(mime_from_extension("xyz").has_value());
    REQUIRE_FALSE(mime_from_extension("").has_value());
}

TEST_CASE("dlna_org_pn_for mp3 and lpcm", "[media][mime]") {
    REQUIRE(dlna_org_pn_for({AudioCodec::mp3, AudioContainer::mp3}).value() == "MP3");
    REQUIRE(dlna_org_pn_for({AudioCodec::pcm_wav, AudioContainer::wav}).value() == "LPCM");
    REQUIRE_FALSE(dlna_org_pn_for({AudioCodec::aac_lc, AudioContainer::mp4}).has_value());
}

TEST_CASE("ascii_lowercase preserves non-ascii bytes verbatim", "[media][mime]") {
    REQUIRE(ascii_lowercase("Hello WORLD") == "hello world");
    REQUIRE(ascii_lowercase("ABCXYZ") == "abcxyz");
    REQUIRE(ascii_lowercase("") == "");
}
