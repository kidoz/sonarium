#pragma once

#include <chrono>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sonarium::cli {

// Parsed http:// URL. HTTPS is intentionally unsupported — sonariumctl only
// talks to local DLNA servers on the LAN.
struct HttpUrl {
    std::string host;
    std::uint16_t port = 80;
    std::string path = "/";
};

[[nodiscard]] std::expected<HttpUrl, std::string> parse_http_url(std::string_view url);

struct HttpRequest {
    std::string method = "GET";
    HttpUrl url;
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;
    std::chrono::milliseconds timeout = std::chrono::seconds{5};
};

struct HttpResponse {
    int status = 0;
    std::string status_text;
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;
};

// Send `req` synchronously over a POSIX TCP socket. Returns an error string
// on transport failure (DNS, connect, timeout, malformed status line).
[[nodiscard]] std::expected<HttpResponse, std::string> http_request(HttpRequest const& req);

} // namespace sonarium::cli
