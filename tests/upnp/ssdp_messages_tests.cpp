#include <catch2/catch_test_macros.hpp>

#include "upnp/ssdp_messages.hpp"

using sonarium::upnp::ssdp::AdvertFields;
using sonarium::upnp::ssdp::adverts_for_search_target;
using sonarium::upnp::ssdp::build_msearch_response;
using sonarium::upnp::ssdp::build_notify;
using sonarium::upnp::ssdp::NotifyKind;
using sonarium::upnp::ssdp::parse_msearch;
using sonarium::upnp::ssdp::required_search_targets;
using sonarium::upnp::ssdp::SsdpParseError;

namespace {

constexpr std::string_view sample_msearch = "M-SEARCH * HTTP/1.1\r\n"
                                            "HOST: 239.255.255.250:1900\r\n"
                                            "MAN: \"ssdp:discover\"\r\n"
                                            "MX: 2\r\n"
                                            "ST: urn:schemas-upnp-org:device:MediaServer:1\r\n"
                                            "USER-AGENT: VLC/3.0 LibVLC/3.0\r\n"
                                            "\r\n";

} // namespace

TEST_CASE("Valid M-SEARCH parses cleanly", "[upnp][ssdp]") {
    auto const r = parse_msearch(sample_msearch);
    REQUIRE(r.has_value());
    REQUIRE(r->st == "urn:schemas-upnp-org:device:MediaServer:1");
    REQUIRE(r->man == "ssdp:discover");
    REQUIRE(r->mx == 2);
    REQUIRE(r->user_agent == "VLC/3.0 LibVLC/3.0");
}

TEST_CASE("M-SEARCH parser tolerates LF-only line endings", "[upnp][ssdp]") {
    std::string_view const lf = "M-SEARCH * HTTP/1.1\n"
                                "HOST: 239.255.255.250:1900\n"
                                "MAN: \"ssdp:discover\"\n"
                                "MX: 1\n"
                                "ST: ssdp:all\n"
                                "\n";
    auto const r = parse_msearch(lf);
    REQUIRE(r.has_value());
    REQUIRE(r->st == "ssdp:all");
}

TEST_CASE("M-SEARCH parser rejects empty datagram", "[upnp][ssdp]") {
    REQUIRE(parse_msearch("").error() == SsdpParseError::empty);
}

TEST_CASE("M-SEARCH parser rejects non-M-SEARCH start line", "[upnp][ssdp]") {
    auto const r = parse_msearch("NOTIFY * HTTP/1.1\r\n\r\n");
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error() == SsdpParseError::not_msearch);
}

TEST_CASE("M-SEARCH parser flags missing MAN", "[upnp][ssdp]") {
    auto const r = parse_msearch("M-SEARCH * HTTP/1.1\r\n"
                                 "ST: ssdp:all\r\n"
                                 "\r\n");
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error() == SsdpParseError::missing_man);
}

TEST_CASE("M-SEARCH parser flags missing ST", "[upnp][ssdp]") {
    auto const r = parse_msearch("M-SEARCH * HTTP/1.1\r\n"
                                 "MAN: \"ssdp:discover\"\r\n"
                                 "\r\n");
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error() == SsdpParseError::missing_target);
}

TEST_CASE("M-SEARCH parser flags wrong MAN", "[upnp][ssdp]") {
    auto const r = parse_msearch("M-SEARCH * HTTP/1.1\r\n"
                                 "MAN: \"something-else\"\r\n"
                                 "ST: ssdp:all\r\n"
                                 "\r\n");
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error() == SsdpParseError::bad_man);
}

TEST_CASE("M-SEARCH response carries CACHE-CONTROL/LOCATION/ST/USN/SERVER", "[upnp][ssdp]") {
    AdvertFields f;
    f.location = "http://192.168.1.10:8200/description.xml";
    f.nt_or_st = "urn:schemas-upnp-org:device:MediaServer:1";
    f.usn = "uuid:abc::urn:schemas-upnp-org:device:MediaServer:1";
    f.server = "Sonarium/0.1 UPnP/1.0 sonarium-dlna/0.1";
    f.cache_max_age = 1800;

    auto const xml = build_msearch_response(f);
    REQUIRE(xml.starts_with("HTTP/1.1 200 OK\r\n"));
    REQUIRE(xml.contains("CACHE-CONTROL: max-age=1800\r\n"));
    REQUIRE(xml.contains("LOCATION: http://192.168.1.10:8200/description.xml\r\n"));
    REQUIRE(xml.contains("ST: urn:schemas-upnp-org:device:MediaServer:1\r\n"));
    REQUIRE(xml.contains("USN: uuid:abc::urn:schemas-upnp-org:device:MediaServer:1\r\n"));
    REQUIRE(xml.contains("SERVER: Sonarium/0.1 UPnP/1.0 sonarium-dlna/0.1\r\n"));
    REQUIRE(xml.contains("EXT: \r\n"));
    REQUIRE(xml.ends_with("\r\n\r\n"));
    REQUIRE(!xml.contains("LOCATION: 0.0.0.0"));
}

TEST_CASE("NOTIFY ssdp:alive carries CACHE-CONTROL/LOCATION/NT/NTS/USN", "[upnp][ssdp]") {
    AdvertFields f;
    f.location = "http://192.168.1.10:8200/description.xml";
    f.nt_or_st = "upnp:rootdevice";
    f.usn = "uuid:abc::upnp:rootdevice";
    f.server = "Sonarium/0.1 UPnP/1.0";

    auto const xml = build_notify(NotifyKind::alive, f);
    REQUIRE(xml.starts_with("NOTIFY * HTTP/1.1\r\n"));
    REQUIRE(xml.contains("HOST: 239.255.255.250:1900\r\n"));
    REQUIRE(xml.contains("CACHE-CONTROL: max-age=1800\r\n"));
    REQUIRE(xml.contains("LOCATION: http://192.168.1.10:8200/description.xml\r\n"));
    REQUIRE(xml.contains("NT: upnp:rootdevice\r\n"));
    REQUIRE(xml.contains("NTS: ssdp:alive\r\n"));
    REQUIRE(xml.contains("USN: uuid:abc::upnp:rootdevice\r\n"));
}

TEST_CASE("NOTIFY ssdp:byebye omits CACHE-CONTROL and LOCATION", "[upnp][ssdp]") {
    AdvertFields f;
    f.nt_or_st = "urn:schemas-upnp-org:service:ContentDirectory:1";
    f.usn = "uuid:abc::urn:schemas-upnp-org:service:ContentDirectory:1";
    f.server = "Sonarium/0.1";

    auto const xml = build_notify(NotifyKind::byebye, f);
    REQUIRE(xml.starts_with("NOTIFY * HTTP/1.1\r\n"));
    REQUIRE(xml.contains("NTS: ssdp:byebye\r\n"));
    REQUIRE(!xml.contains("CACHE-CONTROL"));
    REQUIRE(!xml.contains("LOCATION"));
}

TEST_CASE("required_search_targets covers root, uuid, MediaServer, both services", "[upnp][ssdp]") {
    auto const t = required_search_targets("uuid:abc-123");
    REQUIRE(t.size() == 5);
    REQUIRE(t[0] == "upnp:rootdevice");
    REQUIRE(t[1] == "uuid:abc-123");
    REQUIRE(t[2] == "urn:schemas-upnp-org:device:MediaServer:1");
    REQUIRE(t[3] == "urn:schemas-upnp-org:service:ContentDirectory:1");
    REQUIRE(t[4] == "urn:schemas-upnp-org:service:ConnectionManager:1");
}

TEST_CASE("required_search_targets prepends uuid: when missing", "[upnp][ssdp]") {
    auto const t = required_search_targets("abc-123");
    REQUIRE(t[1] == "uuid:abc-123");
}

TEST_CASE("adverts_for_search_target ssdp:all returns all five", "[upnp][ssdp]") {
    auto const a = adverts_for_search_target("uuid:abc-123", "ssdp:all");
    REQUIRE(a.size() == 5);
}

TEST_CASE("adverts_for_search_target picks one for specific ST", "[upnp][ssdp]") {
    auto const a = adverts_for_search_target("uuid:abc-123", "upnp:rootdevice");
    REQUIRE(a.size() == 1);
    REQUIRE(a[0].first == "upnp:rootdevice");
    REQUIRE(a[0].second == "uuid:abc-123::upnp:rootdevice");

    auto const b = adverts_for_search_target("uuid:abc-123",
                                             "urn:schemas-upnp-org:service:ContentDirectory:1");
    REQUIRE(b.size() == 1);
    REQUIRE(b[0].second == "uuid:abc-123::urn:schemas-upnp-org:service:ContentDirectory:1");
}

TEST_CASE("adverts_for_search_target empty for unknown ST", "[upnp][ssdp]") {
    auto const a = adverts_for_search_target("uuid:abc-123", "urn:something-else");
    REQUIRE(a.empty());
}
