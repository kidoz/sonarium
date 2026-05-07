#include "composition/ssdp_service.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <logspine/field.hpp>
#include <span>
#include <string>
#include <utility>

namespace sonarium::composition {

namespace {

constexpr std::string_view ssdp_multicast_address = "239.255.255.250";
constexpr std::uint16_t ssdp_port = 1900;

// Recv buffer size. UPnP datagrams are small; 4 KiB is plenty.
constexpr std::size_t recv_buffer_size = 4096;

// Receive timeout — short so the loop wakes up to observe `running_`.
constexpr std::uint32_t recv_timeout_ms = 500;

[[nodiscard]] sonarium::upnp::ssdp::AdvertFields
make_advert_fields(SsdpConfig const& config, std::string nt_or_st, std::string usn) {
    sonarium::upnp::ssdp::AdvertFields f;
    f.location = config.description_url;
    f.nt_or_st = std::move(nt_or_st);
    f.usn = std::move(usn);
    f.server = config.server_token;
    f.cache_max_age = config.cache_max_age_seconds;
    return f;
}

} // namespace

std::vector<OutboundMessage> responses_for_msearch(SsdpConfig const& config,
                                                   sonarium::upnp::ssdp::MSearch const& msearch,
                                                   std::string requester_address,
                                                   std::uint16_t requester_port) {
    auto const adverts = sonarium::upnp::ssdp::adverts_for_search_target(config.udn, msearch.st);
    std::vector<OutboundMessage> out;
    out.reserve(adverts.size());
    for (auto const& [nt, usn] : adverts) {
        auto const fields = make_advert_fields(config, nt, usn);
        auto payload = sonarium::upnp::ssdp::build_msearch_response(fields);
        out.push_back(OutboundMessage{std::move(payload), requester_address, requester_port});
    }
    return out;
}

std::vector<OutboundMessage> alive_announcements(SsdpConfig const& config) {
    auto const targets = sonarium::upnp::ssdp::required_search_targets(config.udn);
    std::vector<OutboundMessage> out;
    out.reserve(targets.size());
    for (auto const& nt : targets) {
        std::string usn;
        if (nt == config.udn || nt.starts_with("uuid:")) {
            // The uuid: target uses the bare UDN as USN (no `::nt` suffix).
            usn = config.udn;
        } else {
            usn = config.udn + "::" + nt;
        }
        auto const fields = make_advert_fields(config, nt, std::move(usn));
        auto payload =
            sonarium::upnp::ssdp::build_notify(sonarium::upnp::ssdp::NotifyKind::alive, fields);
        out.push_back(
            OutboundMessage{std::move(payload), std::string{ssdp_multicast_address}, ssdp_port});
    }
    return out;
}

std::vector<OutboundMessage> byebye_announcements(SsdpConfig const& config) {
    auto const targets = sonarium::upnp::ssdp::required_search_targets(config.udn);
    std::vector<OutboundMessage> out;
    out.reserve(targets.size());
    for (auto const& nt : targets) {
        std::string usn;
        if (nt == config.udn || nt.starts_with("uuid:")) {
            usn = config.udn;
        } else {
            usn = config.udn + "::" + nt;
        }
        auto const fields = make_advert_fields(config, nt, std::move(usn));
        auto payload =
            sonarium::upnp::ssdp::build_notify(sonarium::upnp::ssdp::NotifyKind::byebye, fields);
        out.push_back(
            OutboundMessage{std::move(payload), std::string{ssdp_multicast_address}, ssdp_port});
    }
    return out;
}

SsdpService::SsdpService(SsdpConfig config, std::shared_ptr<sonarium::core::Logger> logger)
    : config_{std::move(config)}, logger_{std::move(logger)} {}

SsdpService::~SsdpService() {
    stop();
}

std::expected<void, std::string> SsdpService::start() {
    if (running_.load(std::memory_order_acquire)) {
        return {};
    }

    auto socket = ::atria::UdpSocket::bind_ipv4("0.0.0.0", ssdp_port, /*reuse_address=*/true);
    if (!socket.has_value()) {
        return std::unexpected(std::string{"ssdp: bind 0.0.0.0:1900 failed: "}
                               + socket.error().message);
    }

    if (auto join = socket->join_ipv4_multicast(ssdp_multicast_address, config_.interface_address);
        !join.has_value()) {
        return std::unexpected(std::string{"ssdp: multicast join "}
                               + std::string{ssdp_multicast_address}
                               + " failed: " + join.error().message);
    }

    if (auto t = socket->set_receive_timeout(recv_timeout_ms); !t.has_value()) {
        // Non-fatal; the receive loop will block longer between wakeups.
        logger_->warn("ssdp.recv_timeout_failed",
                      {::logspine::kv("component", std::string{"SsdpService"}),
                       ::logspine::kv("error", t.error().message)});
    }

    socket_.emplace(std::move(*socket));
    running_.store(true, std::memory_order_release);

    receive_thread_ = std::thread{[this] { receive_loop(); }};

    // Initial alive burst — renderers don't have to wait a full interval to find us.
    send_each(alive_announcements(config_), "ssdp.alive.initial");

    alive_timer_.start(config_.alive_interval, [this] {
        if (!running_.load(std::memory_order_acquire)) {
            return;
        }
        send_each(alive_announcements(config_), "ssdp.alive.tick");
    });

    logger_->info("ssdp.started",
                  {::logspine::kv("component", std::string{"SsdpService"}),
                   ::logspine::kv("location", config_.description_url),
                   ::logspine::kv("udn", config_.udn),
                   ::logspine::kv("interface", config_.interface_address)});
    return {};
}

void SsdpService::stop() noexcept {
    bool expected = true;
    if (!running_.compare_exchange_strong(expected, false, std::memory_order_acq_rel)) {
        return;
    }

    alive_timer_.stop();

    try {
        send_each(byebye_announcements(config_), "ssdp.byebye");
    } catch (...) {
        // We're shutting down — swallow.
    }

    if (socket_.has_value()) {
        socket_->close();
    }

    if (receive_thread_.joinable()) {
        receive_thread_.join();
    }

    socket_.reset();

    if (logger_) {
        logger_->info("ssdp.stopped", {::logspine::kv("component", std::string{"SsdpService"})});
    }
}

void SsdpService::receive_loop() {
    std::array<char, recv_buffer_size> buffer{};
    while (running_.load(std::memory_order_acquire)) {
        if (!socket_.has_value()) {
            return;
        }
        auto received = socket_->receive_from(std::span<char>{buffer.data(), buffer.size()});
        if (!received.has_value()) {
            // Likely a timeout — loop and re-check `running_`. We do not log
            // every timeout; that would drown the structured log.
            continue;
        }

        std::string_view const datagram{buffer.data(), received->bytes_received};
        auto parsed = sonarium::upnp::ssdp::parse_msearch(datagram);
        if (!parsed.has_value()) {
            // Other multicast traffic (NOTIFY from peers, malformed packets, etc.) —
            // not an error, just not something we answer.
            continue;
        }

        auto responses = responses_for_msearch(
            config_, *parsed, received->remote.address, received->remote.port);
        if (responses.empty()) {
            // ST didn't match anything we advertise.
            continue;
        }

        logger_->info("ssdp.msearch",
                      {::logspine::kv("component", std::string{"SsdpService"}),
                       ::logspine::kv("st", parsed->st),
                       ::logspine::kv("user_agent", parsed->user_agent),
                       ::logspine::kv("remote_ip", received->remote.address),
                       ::logspine::kv("responses", static_cast<std::int64_t>(responses.size()))});

        send_each(responses, "ssdp.msearch.response");
    }
}

void SsdpService::send_each(std::vector<OutboundMessage> const& messages,
                            std::string_view event_name) {
    if (messages.empty() || !socket_.has_value()) {
        return;
    }
    std::scoped_lock const lock{send_mutex_};
    if (!socket_.has_value()) {
        return;
    }
    for (auto const& m : messages) {
        ::atria::UdpEndpoint const remote{m.target_address, m.target_port};
        auto sent = socket_->send_to(m.payload, remote);
        if (!sent.has_value()) {
            logger_->warn(std::string{event_name} + ".send_failed",
                          {::logspine::kv("component", std::string{"SsdpService"}),
                           ::logspine::kv("target", m.target_address),
                           ::logspine::kv("error", sent.error().message)});
        }
    }
}

} // namespace sonarium::composition
