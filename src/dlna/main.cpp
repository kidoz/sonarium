#include <atomic>
#include <atria/application.hpp>
#include <atria/middleware.hpp>
#include <atria/network_interface.hpp>
#include <atria/server_config.hpp>
#include <csignal>
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
#include <vector>

#include "catalog/catalog_factory.hpp"
#include "catalog/in_memory_repository.hpp"
#include "composition/dlna_server.hpp"
#include "composition/http_routes.hpp"
#include "composition/injector.hpp"
#include "composition/service_config.hpp"
#include "composition/ssdp_service.hpp"
#include "core/env_config.hpp"
#include "core/logspine_logger.hpp"
#include "core/operator_mode.hpp"
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
                     std::shared_ptr<sonarium::core::MediaTokenSigner> signer,
                     std::filesystem::path media_root) {
    sonarium::composition::ServiceConfig cfg;
    cfg.device.friendly_name = app_cfg.friendly_name;
    cfg.device.manufacturer = "Kidoz";
    cfg.device.model_name = "sonarium-dlna";
    cfg.device.udn =
        app_cfg.udn.empty() ? "uuid:00000000-0000-0000-0000-000000000001" : app_cfg.udn;
    cfg.base_url = "http://" + std::string{advertised_host} + ":" + std::to_string(port);
    cfg.protocol_info_catalog = sonarium::dlna::default_protocol_info_catalog();
    cfg.media_token_signer = std::move(signer);
    cfg.media_root = std::move(media_root);
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

// True for hosts that only resolve to the local machine. Pinning the SSDP
// multicast join to one of these would make discovery invisible on the LAN.
[[nodiscard]] bool is_loopback_host(std::string_view host) noexcept {
    return host.starts_with("127.") || host == "::1" || host == "localhost";
}

[[nodiscard]] std::string env_or(std::string_view name, std::string fallback) {
    if (auto const* v = std::getenv(std::string{name}.c_str()); v != nullptr) {
        return std::string{v};
    }
    return fallback;
}

[[nodiscard]] bool has_flag(std::span<char* const> args, std::string_view flag) noexcept {
    for (auto const* a : args.subspan(1)) {
        if (std::strcmp(a, std::string{flag}.c_str()) == 0) {
            return true;
        }
    }
    return false;
}

// SIGINT/SIGTERM → Application::shutdown(), which only stores an atomic flag
// (async-signal-safe). The accept loop notices within its poll interval, so
// `listen()` returns and the SSDP byebye + logger flush below actually run.
std::atomic<::atria::Application*> g_shutdown_app{nullptr};

void handle_shutdown_signal(int /*signum*/) {
    if (auto* app = g_shutdown_app.load()) {
        app->shutdown();
    }
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

    // Malformed values are collected rather than silently defaulted: WARN in
    // development, refuse-to-start in production (alongside the invariants).
    std::vector<std::string> config_issues;
    auto const bind_host = env_or("SONARIUM_DLNA_BIND_HOST", "0.0.0.0");
    auto const http_port = static_cast<std::uint16_t>(sonarium::core::checked_env_int(
        "SONARIUM_DLNA_HTTP_PORT", app_cfg.http_port, 1, 65535, config_issues));
    auto const advertised_host = detect_advertised_host(bind_host);

    auto logging = sonarium::core::build_console_logger(
        "sonarium.dlna", sonarium::core::parse_log_level(env_or("SONARIUM_LOG_LEVEL", "")));
    auto profiles = std::make_shared<sonarium::dlna::DeviceProfileRegistry>(
        sonarium::dlna::DeviceProfileRegistry::with_defaults());
    auto const token_secret = env_or("SONARIUM_MEDIA_TOKEN_SECRET", "");
    auto signer = std::make_shared<sonarium::core::MediaTokenSigner>(token_secret);

    auto const pg_conninfo = env_or("SONARIUM_PG_CONNINFO", "");
    auto const sqlite_path = env_or("SONARIUM_SQLITE_PATH", "");
    auto const media_root = env_or("SONARIUM_MEDIA_ROOT", "");
    auto const mode = sonarium::core::current_operator_mode();
    auto const allow_public_bind = env_or("SONARIUM_ALLOW_PUBLIC_BIND", "0") != "0";
    auto const violations = sonarium::core::check_startup_invariants({
        .bind_host = bind_host,
        .media_token_secret = token_secret,
        .pg_conninfo = pg_conninfo,
        .sqlite_path = sqlite_path,
        .media_root = media_root,
        .allow_public_bind = allow_public_bind,
    });
    auto const fatal = mode == sonarium::core::OperatorMode::production
                       && !(violations.empty() && config_issues.empty());
    for (auto const& issue : config_issues) {
        std::cerr << "  " << (fatal ? "FATAL: " : "WARN: ") << issue << '\n';
    }
    for (auto const& vio : violations) {
        std::cerr << "  " << (fatal ? "FATAL: " : "WARN: ") << vio << '\n';
    }
    if (fatal) {
        std::cerr
            << "  refusing to start: production mode requires safe defaults — set SONARIUM_MODE="
               "development to override during local testing\n";
        logging.registry->flush();
        return 2;
    }

    // Repository selection: if SONARIUM_PG_CONNINFO is present, prefer the
    // Postgres-backed Repository. In production we fail closed on connect /
    // migration error; in development we fall back to the in-memory sample so
    // the routes still demo without a database.
    std::shared_ptr<sonarium::catalog::Repository> catalog;
    std::string catalog_kind;
    if (auto opened = sonarium::catalog::open_catalog_from_env(); !opened.has_value()) {
        // Configured backend failed. Noisy fallback in development; fatal in
        // production, which forbids the in-memory demo catalog.
        std::cerr << "  catalog: " << opened.error() << " — falling back to in-memory\n";
        if (mode == sonarium::core::OperatorMode::production) {
            std::cerr << "  FATAL: catalog unavailable and production mode forbids in-memory "
                         "fallback\n";
            logging.registry->flush();
            return 3;
        }
    } else if (opened->has_value()) {
        catalog = (*opened)->repository;
        catalog_kind = (*opened)->kind;
    }
    if (!catalog) {
        catalog = sample_catalog();
        catalog_kind = "in-memory (sample)";
    }
    auto server = sonarium::composition::make_dlna_server(
        catalog,
        profiles,
        logging.logger,
        build_service_config(
            advertised_host, http_port, app_cfg, signer, std::filesystem::path{media_root}));

    std::cout << sonarium::core::product_name() << " DLNA " << v.major << '.' << v.minor << '.'
              << v.patch << '\n'
              << "  mode=" << sonarium::core::to_string(mode) << '\n'
              << "  bind_host=" << bind_host << '\n'
              << "  advertised_host=" << advertised_host << '\n'
              << "  http_port=" << http_port << '\n'
              << "  media_tokens=" << (signer->enabled() ? "enabled" : "disabled") << '\n'
              << "  media_root=" << (media_root.empty() ? "<unset — containment off>" : media_root)
              << '\n'
              << "  catalog=" << catalog_kind << '\n'
              << "  composition graph resolved via ctorwire (LogSpine console logger)\n";

    if (has_flag(args, "--offline")) {
        std::cout << "  offline preview (no listener):\n";
        run_offline_preview(*server);
        logging.registry->flush();
        return 0;
    }

    ::atria::Application app;
    g_shutdown_app.store(&app);
    std::signal(SIGINT, handle_shutdown_signal);
    std::signal(SIGTERM, handle_shutdown_signal);
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
        // Pin the multicast join to the advertised interface so multi-homed
        // hosts answer M-SEARCH from the LAN-visible address. A loopback
        // fallback stays unpinned — joining 127.0.0.1 would hide SSDP from
        // the LAN entirely.
        if (!is_loopback_host(advertised_host)) {
            ssdp_cfg.interface_address = advertised_host;
        }
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
    g_shutdown_app.store(nullptr);
    if (ssdp.has_value()) {
        ssdp->stop();
    }
    logging.registry->flush();
    return rc;
}
