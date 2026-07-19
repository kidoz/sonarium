#pragma once

#include <atomic>
#include <atria/periodic_timer.hpp>
#include <atria/udp.hpp>
#include <chrono>
#include <cstdint>
#include <expected>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "core/logger.hpp"
#include "upnp/ssdp_messages.hpp"

namespace sonarium::composition {

struct SsdpConfig {
    // http://<lan-ip>:<port>/description.xml — must be reachable from renderers.
    std::string description_url;
    // "uuid:..." — stable across the process lifetime.
    std::string udn;
    // SERVER header: "<product>/<version> UPnP/1.0 <model>/<model-version>".
    std::string server_token;
    // Local interface used to join the SSDP multicast group; "0.0.0.0" lets
    // the kernel pick.
    std::string interface_address = "0.0.0.0";
    std::uint32_t cache_max_age_seconds = 1800;
    // Half of cache_max_age is the conventional alive cadence.
    std::chrono::milliseconds alive_interval{std::chrono::seconds{900}};
    // Token-bucket cap on M-SEARCH responses. A spoofed-source flood would
    // otherwise turn the responder into a (small) reflection amplifier.
    // 0 disables the limit.
    double msearch_responses_per_second = 10.0;
    double msearch_response_burst = 25.0;
};

// True when an M-SEARCH source address is a plausible LAN peer: RFC 1918
// private ranges, loopback, or link-local IPv4. The SSDP responder answers
// the *claimed* datagram source, so replying to a public address would
// reflect traffic at a spoofed victim.
[[nodiscard]] bool is_lan_msearch_source(std::string_view address) noexcept;

// Token bucket limiting M-SEARCH response packets. Pure logic — the caller
// supplies timestamps, so tests don't sleep. Not thread-safe; the SSDP
// receive thread is the only user.
class SsdpResponseBudget {
public:
    SsdpResponseBudget(double rate_per_second, double burst) noexcept
        : rate_{rate_per_second}, burst_{burst}, tokens_{burst} {}

    // Whether `count` response packets may be sent at `now`; deducts on success.
    [[nodiscard]] bool allow(std::size_t count, std::chrono::steady_clock::time_point now) noexcept;

private:
    double rate_;
    double burst_;
    double tokens_;
    std::optional<std::chrono::steady_clock::time_point> last_;
};

// One outbound SSDP packet ready for the wire.
struct OutboundMessage {
    std::string payload;
    std::string target_address;
    std::uint16_t target_port = 0;
};

// Build the M-SEARCH response set for an inbound search probe. Fans out across
// the search-target match (ssdp:all -> 5 packets, specific ST -> 1 packet).
[[nodiscard]] std::vector<OutboundMessage>
responses_for_msearch(SsdpConfig const& config,
                      sonarium::upnp::ssdp::MSearch const& msearch,
                      const std::string& requester_address,
                      std::uint16_t requester_port);

// Periodic NOTIFY ssdp:alive announcements (one per advertised target).
[[nodiscard]] std::vector<OutboundMessage> alive_announcements(SsdpConfig const& config);

// Shutdown NOTIFY ssdp:byebye announcements (one per advertised target).
[[nodiscard]] std::vector<OutboundMessage> byebye_announcements(SsdpConfig const& config);

// Owns the UDP socket on 0.0.0.0:1900, the periodic alive timer, and a
// receive thread for inbound M-SEARCH probes. Construct, start(), use HTTP
// listener concurrently, then call stop() (or destruct) for byebye + clean
// shutdown.
class SsdpService {
public:
    SsdpService(SsdpConfig config, std::shared_ptr<sonarium::core::Logger> logger);
    ~SsdpService();

    SsdpService(SsdpService const&) = delete;
    SsdpService& operator=(SsdpService const&) = delete;
    SsdpService(SsdpService&&) noexcept = delete;
    SsdpService& operator=(SsdpService&&) noexcept = delete;

    // Bind UDP, join multicast, spawn receive thread, start alive timer.
    // On error returns a human-readable description; the caller may fall back
    // to HTTP-only operation.
    [[nodiscard]] std::expected<void, std::string> start();

    // Send byebye burst, stop alive timer, close socket, join threads.
    // Idempotent.
    void stop() noexcept;

    [[nodiscard]] bool running() const noexcept { return running_.load(std::memory_order_acquire); }

private:
    void receive_loop();
    void send_each(std::vector<OutboundMessage> const& messages, std::string_view event_name);

    SsdpConfig config_;
    std::shared_ptr<sonarium::core::Logger> logger_;
    std::optional<::atria::UdpSocket> socket_;
    ::atria::PeriodicTimer alive_timer_;
    std::thread receive_thread_;
    std::mutex send_mutex_;
    std::atomic<bool> running_{false};
    // Touched only by the receive thread.
    std::optional<SsdpResponseBudget> response_budget_;
};

} // namespace sonarium::composition
