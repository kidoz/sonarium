#include <atria/application.hpp>
#include <atria/middleware.hpp>
#include <atria/network_interface.hpp>
#include <atria/server_config.hpp>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iostream>
#include <logspine/level.hpp>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "catalog/in_memory_repository.hpp"
#include "catalog/postgres_repository.hpp"
#include "composition/dlna_server.hpp"
#include "composition/http_routes.hpp"
#include "composition/injector.hpp"
#include "composition/service_config.hpp"
#include "composition/ssdp_service.hpp"
#include "core/logspine_logger.hpp"
#include "core/version.hpp"
#include "dlna-core/connection_manager_handler.hpp"
#include "dlna-core/device_profile.hpp"
#include "dlna/config.hpp"
#include "media/media_rendition.hpp"

namespace {

[[nodiscard]] std::filesystem::path seed_demo_asset() {
    namespace fs = std::filesystem;
    auto const path = fs::temp_directory_path() / "sonarium-demo-mp3.bin";
    std::ofstream out{path, std::ios::binary | std::ios::trunc};
    // Real renderers will reject this — it's just a non-empty payload so Range
    // and HEAD requests have something to return until the worker pipeline lands.
    constexpr std::string_view filler = "SONARIUM_DEMO_RENDITION_PAYLOAD_";
    for (int i = 0; i < 64; ++i) {
        out.write(filler.data(), static_cast<std::streamsize>(filler.size()));
    }
    return path;
}

[[nodiscard]] std::filesystem::path seed_demo_album_art() {
    namespace fs = std::filesystem;
    auto const path = fs::temp_directory_path() / "sonarium-demo-cover.jpg";
    std::ofstream out{path, std::ios::binary | std::ios::trunc};
    // Minimal JPEG SOI/APP0/EOI shell — enough that `file(1)` sniffs it as
    // JPEG. Not a renderable image, but proves the route delivers binary
    // blobs cleanly until the catalog import pipeline lands.
    constexpr unsigned char jpeg_skeleton[] = {
        0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10, 'J',  'F',  'I',  'F',  0x00,
        0x01, 0x01, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0xFF, 0xD9,
    };
    out.write(reinterpret_cast<char const*>(jpeg_skeleton),
              static_cast<std::streamsize>(sizeof(jpeg_skeleton)));
    return path;
}

// Build a Postgres-backed Repository when SONARIUM_PG_CONNINFO is set; on
// failure (libpq connect error, ensure_schema error) fall back to the
// in-memory sample catalog so the server stays serviceable. The fall-through
// is *intentionally noisy* — the operator wanted Postgres and we couldn't
// reach it; printing the error to stderr lets `journalctl` or the log tailer
// surface the misconfiguration.
[[nodiscard]] std::shared_ptr<sonarium::catalog::Repository>
try_open_postgres_catalog(std::string_view conninfo) {
    auto repo = sonarium::catalog::PostgresRepository::open(std::string{conninfo});
    if (!repo.has_value()) {
        std::cerr << "  postgres: " << repo.error() << " — falling back to in-memory\n";
        return nullptr;
    }
    if (auto schema = (*repo)->ensure_schema(); !schema.has_value()) {
        std::cerr << "  postgres: " << schema.error() << " — falling back to in-memory\n";
        return nullptr;
    }
    return *repo;
}

std::shared_ptr<sonarium::catalog::Repository> sample_catalog() {
    using namespace sonarium::catalog;
    using namespace sonarium::media;

    auto repo = std::make_shared<InMemoryRepository>();

    Artist a;
    a.id = "1";
    a.name = "Sample Artist";
    repo->add_artist(a);

    Album al;
    al.id = "1";
    al.artist_id = "1";
    al.title = "Demo Album";
    al.cover_art_asset_id = "demo-cover";
    repo->add_album(al);

    Track t;
    t.id = "1";
    t.album_id = "1";
    t.artist_id = "1";
    t.title = "Hello Track";
    t.duration_ms = 240'000;
    t.track_number = std::uint16_t{1};
    repo->add_track(t);

    MediaRendition r;
    r.id = "demo-mp3";
    r.track_id = "1";
    r.codec = AudioCodec::mp3;
    r.container = AudioContainer::mp3;
    r.bitrate_bps = 320'000;
    r.duration_ms = 240'000;
    r.storage_path = seed_demo_asset().string();
    repo->add_rendition(r);

    StorageAsset cover;
    cover.id = "demo-cover";
    cover.storage_path = seed_demo_album_art().string();
    cover.mime_type = "image/jpeg";
    repo->add_asset(cover);

    return repo;
}

[[nodiscard]] sonarium::composition::ServiceConfig
build_service_config(std::string_view advertised_host,
                     std::uint16_t port,
                     sonarium::dlna::DlnaConfig const& app_cfg,
                     std::shared_ptr<sonarium::core::MediaTokenSigner> signer) {
    sonarium::composition::ServiceConfig cfg;
    cfg.device.friendly_name = app_cfg.friendly_name;
    cfg.device.manufacturer = "Kidoz";
    cfg.device.model_name = "sonarium-dlna";
    cfg.device.udn =
        app_cfg.udn.empty() ? "uuid:00000000-0000-0000-0000-000000000001" : app_cfg.udn;
    cfg.base_url = "http://" + std::string{advertised_host} + ":" + std::to_string(port);
    cfg.protocol_info_catalog = sonarium::dlna::default_protocol_info_catalog();
    cfg.media_token_signer = std::move(signer);
    return cfg;
}

// Pick the host renderers should reach Sonarium at. The HTTP listener may bind
// 0.0.0.0 (all interfaces); SSDP LOCATION must point at a single LAN-reachable
// IP. Override via SONARIUM_DLNA_ADVERTISED_HOST when the auto-detection picks
// the wrong interface (e.g. on a multi-homed dev box).
[[nodiscard]] std::string detect_advertised_host(std::string_view bind_host) {
    if (auto const* override_value = std::getenv("SONARIUM_DLNA_ADVERTISED_HOST");
        override_value != nullptr && *override_value != '\0') {
        return std::string{override_value};
    }
    auto selected = ::atria::select_lan_ipv4_interface();
    if (selected.has_value() && selected->has_value()) {
        return (*selected)->ipv4_address;
    }
    if (bind_host == "0.0.0.0") {
        return "127.0.0.1"; // last-resort fallback for "listen everywhere" without a LAN IP
    }
    return std::string{bind_host};
}

[[nodiscard]] std::string env_or(std::string_view name, std::string fallback) {
    if (auto const* v = std::getenv(std::string{name}.c_str()); v != nullptr) {
        return std::string{v};
    }
    return fallback;
}

[[nodiscard]] std::uint16_t env_port_or(std::string_view name, std::uint16_t fallback) noexcept {
    auto const* v = std::getenv(std::string{name}.c_str());
    if (v == nullptr) {
        return fallback;
    }
    auto const parsed = std::strtoul(v, nullptr, 10);
    if (parsed == 0 || parsed > 65535U) {
        return fallback;
    }
    return static_cast<std::uint16_t>(parsed);
}

[[nodiscard]] bool has_flag(std::span<char* const> args, std::string_view flag) noexcept {
    for (auto const* a : args.subspan(1)) {
        if (std::strcmp(a, std::string{flag}.c_str()) == 0) {
            return true;
        }
    }
    return false;
}

void run_offline_preview(sonarium::composition::DlnaServer& server) {
    constexpr auto browse_root = R"(<?xml version="1.0" encoding="utf-8"?>
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

    std::cout << "\n--- description.xml ---\n" << server.description_xml() << "\n";
    std::cout << "\n--- POST Browse(0) response ---\n"
              << server.dispatch_soap(browse_root, "demo-control-point/0.1") << "\n";
}

} // namespace

int main(int argc, char** argv) {
    // Make stdout line-buffered so log lines appear immediately even when piped
    // (the default block-buffering hides events until the process exits cleanly).
    std::setvbuf(stdout, nullptr, _IOLBF, 0);

    auto args = std::span<char* const>{argv, static_cast<std::size_t>(argc)};
    auto const v = sonarium::core::current_version();
    sonarium::dlna::DlnaConfig const app_cfg;

    auto const bind_host = env_or("SONARIUM_DLNA_BIND_HOST", "0.0.0.0");
    auto const http_port = env_port_or("SONARIUM_DLNA_HTTP_PORT", app_cfg.http_port);
    auto const advertised_host = detect_advertised_host(bind_host);

    auto logging = sonarium::core::build_console_logger("sonarium.dlna", ::logspine::level::info);
    auto profiles = std::make_shared<sonarium::dlna::DeviceProfileRegistry>(
        sonarium::dlna::DeviceProfileRegistry::with_defaults());
    auto const token_secret = env_or("SONARIUM_MEDIA_TOKEN_SECRET", "");
    auto signer = std::make_shared<sonarium::core::MediaTokenSigner>(token_secret);

    // Repository selection: if SONARIUM_PG_CONNINFO is present, prefer the
    // Postgres-backed Repository; on connection / migration failure, fall back
    // to the in-memory sample so the server still demonstrates the routes.
    auto const pg_conninfo = env_or("SONARIUM_PG_CONNINFO", "");
    std::shared_ptr<sonarium::catalog::Repository> catalog;
    std::string catalog_kind;
    if (!pg_conninfo.empty()) {
        catalog = try_open_postgres_catalog(pg_conninfo);
        if (catalog) {
            catalog_kind = "postgres";
        }
    }
    if (!catalog) {
        catalog = sample_catalog();
        catalog_kind = "in-memory (sample)";
    }
    auto server = sonarium::composition::make_dlna_server(
        catalog,
        profiles,
        logging.logger,
        build_service_config(advertised_host, http_port, app_cfg, signer));

    std::cout << sonarium::core::product_name() << " DLNA " << v.major << '.' << v.minor << '.'
              << v.patch << '\n'
              << "  bind_host=" << bind_host << '\n'
              << "  advertised_host=" << advertised_host << '\n'
              << "  http_port=" << http_port << '\n'
              << "  media_tokens=" << (signer->enabled() ? "enabled" : "disabled") << '\n'
              << "  catalog=" << catalog_kind << '\n'
              << "  composition graph resolved via ctorwire (LogSpine console logger)\n";

    if (has_flag(args, "--offline")) {
        std::cout << "  offline preview (no listener):\n";
        run_offline_preview(*server);
        logging.registry->flush();
        return 0;
    }

    ::atria::Application app;
    app.use(::atria::middleware::error_handler());
    app.use(::atria::middleware::request_logger());
    sonarium::composition::register_dlna_routes(
        app,
        std::const_pointer_cast<sonarium::composition::DlnaServer const>(server),
        std::const_pointer_cast<sonarium::catalog::Repository const>(catalog));

    // SSDP discovery — opt out via SONARIUM_DLNA_DISABLE_SSDP=1 if port 1900 is
    // unavailable (e.g. another MediaServer is already running).
    std::optional<sonarium::composition::SsdpService> ssdp;
    auto const ssdp_disabled =
        env_or("SONARIUM_DLNA_DISABLE_SSDP", "0") != "0" || has_flag(args, "--no-ssdp");
    if (ssdp_disabled) {
        std::cout << "  SSDP disabled by configuration\n";
    } else {
        sonarium::composition::SsdpConfig ssdp_cfg;
        ssdp_cfg.description_url = server->config().base_url + "/description.xml";
        ssdp_cfg.udn = server->config().device.udn;
        ssdp_cfg.server_token = std::string{sonarium::core::server_signature()};
        ssdp_cfg.cache_max_age_seconds = 1800;
        ssdp_cfg.alive_interval = std::chrono::seconds{900};
        ssdp.emplace(std::move(ssdp_cfg), logging.logger);
        if (auto started = ssdp->start(); !started.has_value()) {
            std::cerr << "  SSDP failed to start: " << started.error()
                      << " — continuing HTTP-only\n";
            ssdp.reset();
        } else {
            std::cout << "  SSDP listening on 239.255.255.250:1900\n";
        }
    }

    ::atria::ServerConfig cfg;
    cfg.host = bind_host;
    cfg.port = http_port;
    cfg.worker_threads = 4;

    std::cout << "  HTTP routes registered; LOCATION=http://" << advertised_host << ":" << http_port
              << "/description.xml\n";

    auto const rc = app.listen(cfg);
    if (ssdp.has_value()) {
        ssdp->stop();
    }
    logging.registry->flush();
    return rc;
}
