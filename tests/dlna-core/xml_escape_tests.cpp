#include <catch2/catch_test_macros.hpp>

#include "dlna-core/xml_escape.hpp"

using sonarium::dlna::xml_escape;

TEST_CASE("xml_escape escapes the five canonical entities", "[dlna][xml]") {
    REQUIRE(xml_escape("AT&T") == "AT&amp;T");
    REQUIRE(xml_escape("a<b") == "a&lt;b");
    REQUIRE(xml_escape("a>b") == "a&gt;b");
    REQUIRE(xml_escape("\"x\"") == "&quot;x&quot;");
    REQUIRE(xml_escape("'x'") == "&apos;x&apos;");
}

TEST_CASE("xml_escape leaves benign text alone", "[dlna][xml]") {
    REQUIRE(xml_escape("") == "");
    REQUIRE(xml_escape("Hello, world!") == "Hello, world!");
    REQUIRE(xml_escape("track-42") == "track-42");
}

TEST_CASE("xml_escape preserves byte order for non-ASCII", "[dlna][xml]") {
    auto const utf8 = std::string("Mot\xc3\xb6rhead"); // "Motörhead" in UTF-8
    REQUIRE(xml_escape(utf8) == utf8);
}

TEST_CASE("xml_escape handles all five entities together", "[dlna][xml]") {
    REQUIRE(xml_escape("&<>\"'") == "&amp;&lt;&gt;&quot;&apos;");
}
