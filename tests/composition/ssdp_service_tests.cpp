#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <string>

#include "composition/ssdp_service.hpp"
#include "upnp/ssdp_messages.hpp"

using sonarium::composition::alive_announcements;
using sonarium::composition::byebye_announcements;
using sonarium::composition::OutboundMessage;
using sonarium::composition::responses_for_msearch;
using sonarium::composition::SsdpConfig;
using sonarium::upnp::ssdp::MSearch;

namespace {

[[nodiscard]] SsdpConfig sample_ssdp_config() {
    SsdpConfig c;
    c.description_url = "http://192.168.1.10:8200/description.xml";
    c.udn = "uuid:abcdef01-2345-6789-abcd-ef0123456789";
    c.server_token = "Sonarium/0.1 UPnP/1.0 sonarium-dlna/0.1";
    c.cache_max_age_seconds = 1800;
    return c;
}

[[nodiscard]] bool any_payload_contains(std::vector<OutboundMessage> const& msgs,
                                        std::string_view needle) {
    for (auto const& m : msgs) {
        if (m.payload.find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool
all_target(std::vector<OutboundMessage> const& msgs, std::string_view address, std::uint16_t port) {
    for (auto const& m : msgs) {
        if (m.target_address != address || m.target_port != port) {
            return false;
        }
    }
    return true;
}

} // namespace

TEST_CASE("alive_announcements emits 5 packets to the multicast group", "[composition][ssdp]") {
    auto const cfg = sample_ssdp_config();
    auto const msgs = alive_announcements(cfg);

    REQUIRE(msgs.size() == 5);
    REQUIRE(all_target(msgs, "239.255.255.250", 1900));

    // Every alive packet starts with NOTIFY and carries NTS: ssdp:alive
    for (auto const& m : msgs) {
        REQUIRE(m.payload.starts_with("NOTIFY * HTTP/1.1\r\n"));
        REQUIRE(m.payload.find("NTS: ssdp:alive\r\n") != std::string::npos);
        REQUIRE(m.payload.find("CACHE-CONTROL: max-age=1800\r\n") != std::string::npos);
        REQUIRE(m.payload.find("LOCATION: http://192.168.1.10:8200/description.xml\r\n")
                != std::string::npos);
        REQUIRE(m.payload.find("SERVER: Sonarium/0.1 UPnP/1.0 sonarium-dlna/0.1\r\n")
                != std::string::npos);
    }

    // Coverage: every required NT must show up exactly once.
    REQUIRE(any_payload_contains(msgs, "NT: upnp:rootdevice\r\n"));
    REQUIRE(any_payload_contains(msgs, "NT: uuid:abcdef01-2345-6789-abcd-ef0123456789\r\n"));
    REQUIRE(any_payload_contains(msgs, "NT: urn:schemas-upnp-org:device:MediaServer:1\r\n"));
    REQUIRE(any_payload_contains(msgs, "NT: urn:schemas-upnp-org:service:ContentDirectory:1\r\n"));
    REQUIRE(any_payload_contains(msgs, "NT: urn:schemas-upnp-org:service:ConnectionManager:1\r\n"));
}

TEST_CASE("alive USN for uuid: target is the bare UDN", "[composition][ssdp]") {
    auto const cfg = sample_ssdp_config();
    auto const msgs = alive_announcements(cfg);
    bool found_bare = false;
    for (auto const& m : msgs) {
        if (m.payload.find("NT: uuid:abcdef01-2345-6789-abcd-ef0123456789\r\n")
            != std::string::npos) {
            REQUIRE(m.payload.find("USN: uuid:abcdef01-2345-6789-abcd-ef0123456789\r\n")
                    != std::string::npos);
            found_bare = true;
        }
    }
    REQUIRE(found_bare);
}

TEST_CASE("byebye_announcements emit ssdp:byebye with no LOCATION/cache", "[composition][ssdp]") {
    auto const cfg = sample_ssdp_config();
    auto const msgs = byebye_announcements(cfg);

    REQUIRE(msgs.size() == 5);
    REQUIRE(all_target(msgs, "239.255.255.250", 1900));

    for (auto const& m : msgs) {
        REQUIRE(m.payload.find("NTS: ssdp:byebye\r\n") != std::string::npos);
        REQUIRE(m.payload.find("LOCATION") == std::string::npos);
        REQUIRE(m.payload.find("CACHE-CONTROL") == std::string::npos);
    }
}

TEST_CASE("responses_for_msearch ssdp:all yields five unicast responses", "[composition][ssdp]") {
    auto const cfg = sample_ssdp_config();
    MSearch m;
    m.st = "ssdp:all";
    m.man = "ssdp:discover";
    m.mx = 1;

    auto const msgs = responses_for_msearch(cfg, m, "192.168.1.50", 50000);
    REQUIRE(msgs.size() == 5);
    REQUIRE(all_target(msgs, "192.168.1.50", 50000));

    for (auto const& r : msgs) {
        REQUIRE(r.payload.starts_with("HTTP/1.1 200 OK\r\n"));
        REQUIRE(r.payload.find("CACHE-CONTROL: max-age=1800\r\n") != std::string::npos);
        REQUIRE(r.payload.find("LOCATION: http://192.168.1.10:8200/description.xml\r\n")
                != std::string::npos);
        REQUIRE(r.payload.find("EXT: \r\n") != std::string::npos);
        REQUIRE(r.payload.find("ST:") != std::string::npos);
        REQUIRE(r.payload.find("USN: uuid:") != std::string::npos);
    }
}

TEST_CASE("responses_for_msearch specific ST yields one packet", "[composition][ssdp]") {
    auto const cfg = sample_ssdp_config();
    MSearch m;
    m.st = "urn:schemas-upnp-org:device:MediaServer:1";
    m.man = "ssdp:discover";

    auto const msgs = responses_for_msearch(cfg, m, "10.0.0.5", 49152);
    REQUIRE(msgs.size() == 1);
    REQUIRE(msgs[0].target_address == "10.0.0.5");
    REQUIRE(msgs[0].target_port == 49152);
    REQUIRE(msgs[0].payload.find("ST: urn:schemas-upnp-org:device:MediaServer:1\r\n")
            != std::string::npos);
    REQUIRE(msgs[0].payload.find("USN: uuid:abcdef01-2345-6789-abcd-ef0123456789::"
                                 "urn:schemas-upnp-org:device:MediaServer:1\r\n")
            != std::string::npos);
}

TEST_CASE("responses_for_msearch unrecognised ST produces no responses", "[composition][ssdp]") {
    auto const cfg = sample_ssdp_config();
    MSearch m;
    m.st = "urn:schemas-upnp-org:service:Mystery:1";
    m.man = "ssdp:discover";
    auto const msgs = responses_for_msearch(cfg, m, "10.0.0.5", 49152);
    REQUIRE(msgs.empty());
}

TEST_CASE("LOCATION never resolves to 0.0.0.0 from helpers", "[composition][ssdp]") {
    auto const cfg = sample_ssdp_config();
    for (auto const& m : alive_announcements(cfg)) {
        REQUIRE(m.payload.find("LOCATION: http://0.0.0.0") == std::string::npos);
    }
    MSearch ms;
    ms.st = "ssdp:all";
    ms.man = "ssdp:discover";
    for (auto const& m : responses_for_msearch(cfg, ms, "1.2.3.4", 1)) {
        REQUIRE(m.payload.find("LOCATION: http://0.0.0.0") == std::string::npos);
    }
}
