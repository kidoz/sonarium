#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <logspine/field.hpp>
#include <logspine/level.hpp>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "catalog/in_memory_repository.hpp"
#include "composition/dlna_server.hpp"
#include "composition/injector.hpp"
#include "composition/service_config.hpp"
#include "core/logger.hpp"
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
using sonarium::core::Logger;
using sonarium::media::AudioCodec;
using sonarium::media::AudioContainer;
using sonarium::media::MediaRendition;

namespace {

class CapturingLogger final : public Logger {
public:
    struct Entry {
        ::logspine::level level;
        std::string message;
        std::vector<::logspine::field> fields;
    };

    void log(::logspine::level level,
             std::string_view message,
             std::vector<::logspine::field> fields) override {
        std::scoped_lock const lock{mutex_};
        entries_.push_back(Entry{level, std::string{message}, std::move(fields)});
    }

    [[nodiscard]] std::vector<Entry> snapshot() const {
        std::scoped_lock const lock{mutex_};
        return entries_;
    }

private:
    mutable std::mutex mutex_;
    std::vector<Entry> entries_;
};

[[nodiscard]] std::shared_ptr<Repository> sample_repo() {
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

[[nodiscard]] ServiceConfig sample_config() {
    ServiceConfig cfg;
    cfg.device.friendly_name = "Sonarium";
    cfg.device.manufacturer = "Kidoz";
    cfg.device.model_name = "sonarium-dlna";
    cfg.device.udn = "uuid:abcdef01-2345-6789-abcd-ef0123456789";
    cfg.base_url = "http://192.168.1.10:8200";
    cfg.protocol_info_catalog = sonarium::dlna::default_protocol_info_catalog();
    return cfg;
}

[[nodiscard]] std::string field_value(::logspine::field const& f) {
    auto const& v = f.value();
    if (auto const* s = std::get_if<std::string>(&v)) {
        return *s;
    }
    if (auto const* i = std::get_if<std::int64_t>(&v)) {
        return std::to_string(*i);
    }
    if (auto const* u = std::get_if<std::uint64_t>(&v)) {
        return std::to_string(*u);
    }
    return {};
}

[[nodiscard]] bool
entry_has(CapturingLogger::Entry const& e, std::string_view key, std::string_view value) {
    return std::ranges::any_of(e.fields, [key, value](auto const& f) {
        return f.key() == key && field_value(f) == value;
    });
}

[[nodiscard]] CapturingLogger::Entry const*
find_entry(std::vector<CapturingLogger::Entry> const& entries, std::string_view message) {
    for (auto const& e : entries) {
        if (e.message == message) {
            return &e;
        }
    }
    return nullptr;
}

constexpr std::string_view browse_root = R"(<?xml version="1.0" encoding="utf-8"?>
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

constexpr std::string_view get_protocol_info_request = R"(<?xml version="1.0" encoding="utf-8"?>
<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/">
  <s:Body>
    <u:GetProtocolInfo xmlns:u="urn:schemas-upnp-org:service:ConnectionManager:1"/>
  </s:Body>
</s:Envelope>)";

constexpr std::string_view unknown_service_request = R"(<?xml version="1.0" encoding="utf-8"?>
<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/">
  <s:Body>
    <u:Reboot xmlns:u="urn:schemas-upnp-org:service:Mystery:1"/>
  </s:Body>
</s:Envelope>)";

} // namespace

TEST_CASE("Browse emits a structured info event", "[composition][logging]") {
    auto profiles = std::make_shared<sonarium::dlna::DeviceProfileRegistry>(
        sonarium::dlna::DeviceProfileRegistry::with_defaults());
    auto logger = std::make_shared<CapturingLogger>();
    auto server = make_dlna_server(
        sample_repo(), profiles, std::static_pointer_cast<Logger>(logger), sample_config());

    (void)server->dispatch_soap(browse_root, "VLC/3.0");

    auto const entries = logger->snapshot();
    auto const* dispatch = find_entry(entries, "soap.dispatch");
    REQUIRE(dispatch != nullptr);
    REQUIRE(entry_has(*dispatch, "component", "SoapRouter"));
    REQUIRE(entry_has(*dispatch, "service_urn", "urn:schemas-upnp-org:service:ContentDirectory:1"));
    REQUIRE(entry_has(*dispatch, "soap_action", "Browse"));

    auto const* browse_ok = find_entry(entries, "dlna.browse.ok");
    REQUIRE(browse_ok != nullptr);
    REQUIRE(browse_ok->level == ::logspine::level::info);
    REQUIRE(entry_has(*browse_ok, "component", "ContentDirectory"));
    REQUIRE(entry_has(*browse_ok, "object_id", "0"));
    REQUIRE(entry_has(*browse_ok, "device_profile", "VLC/Kodi"));
    REQUIRE(entry_has(*browse_ok, "number_returned", "2"));
}

TEST_CASE("GetProtocolInfo emits an info event", "[composition][logging]") {
    auto profiles = std::make_shared<sonarium::dlna::DeviceProfileRegistry>(
        sonarium::dlna::DeviceProfileRegistry::with_defaults());
    auto logger = std::make_shared<CapturingLogger>();
    auto server = make_dlna_server(
        sample_repo(), profiles, std::static_pointer_cast<Logger>(logger), sample_config());

    (void)server->dispatch_soap(get_protocol_info_request, "Kodi/20");

    auto const entries = logger->snapshot();
    auto const* ok = find_entry(entries, "dlna.protocol_info.ok");
    REQUIRE(ok != nullptr);
    REQUIRE(ok->level == ::logspine::level::info);
    REQUIRE(entry_has(*ok, "component", "ConnectionManager"));
    REQUIRE(entry_has(*ok, "soap_action", "GetProtocolInfo"));
    REQUIRE(entry_has(*ok, "device_profile", "VLC/Kodi"));
}

TEST_CASE("Unknown service emits a warn event with service_urn", "[composition][logging]") {
    auto profiles = std::make_shared<sonarium::dlna::DeviceProfileRegistry>(
        sonarium::dlna::DeviceProfileRegistry::with_defaults());
    auto logger = std::make_shared<CapturingLogger>();
    auto server = make_dlna_server(
        sample_repo(), profiles, std::static_pointer_cast<Logger>(logger), sample_config());

    (void)server->dispatch_soap(unknown_service_request, "VLC/3");

    auto const entries = logger->snapshot();
    auto const* unknown = find_entry(entries, "soap.unknown_service");
    REQUIRE(unknown != nullptr);
    REQUIRE(unknown->level == ::logspine::level::warn);
    REQUIRE(entry_has(*unknown, "service_urn", "urn:schemas-upnp-org:service:Mystery:1"));
}

TEST_CASE("Malformed body emits a parse-failed warn", "[composition][logging]") {
    auto profiles = std::make_shared<sonarium::dlna::DeviceProfileRegistry>(
        sonarium::dlna::DeviceProfileRegistry::with_defaults());
    auto logger = std::make_shared<CapturingLogger>();
    auto server = make_dlna_server(
        sample_repo(), profiles, std::static_pointer_cast<Logger>(logger), sample_config());

    (void)server->dispatch_soap("not soap", "demo-ua");

    auto const entries = logger->snapshot();
    auto const* parse = find_entry(entries, "soap.parse_failed");
    REQUIRE(parse != nullptr);
    REQUIRE(parse->level == ::logspine::level::warn);
    REQUIRE(entry_has(*parse, "user_agent", "demo-ua"));
}
