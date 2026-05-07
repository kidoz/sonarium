#include "cli/http_client.hpp"

#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <cstring>
#include <string>

namespace sonarium::cli {

namespace {

constexpr std::size_t recv_chunk = 4096;

// RAII wrapper for a connected socket. Destructor closes; release() drops
// ownership without closing.
class Fd {
public:
    Fd() = default;
    explicit Fd(int fd) noexcept : fd_{fd} {}
    Fd(Fd const&) = delete;
    Fd& operator=(Fd const&) = delete;
    Fd(Fd&& other) noexcept : fd_{other.fd_} { other.fd_ = -1; }
    Fd& operator=(Fd&& other) noexcept {
        if (this != &other) {
            reset();
            fd_ = other.fd_;
            other.fd_ = -1;
        }
        return *this;
    }
    ~Fd() { reset(); }

    [[nodiscard]] int get() const noexcept { return fd_; }
    [[nodiscard]] bool valid() const noexcept { return fd_ >= 0; }

    void reset() noexcept {
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
    }

private:
    int fd_ = -1;
};

[[nodiscard]] std::string strerror_safe(int err) {
    std::array<char, 256> buf{};
#ifdef __GLIBC__
    auto const* msg = ::strerror_r(err, buf.data(), buf.size());
    return std::string{msg};
#else
    if (::strerror_r(err, buf.data(), buf.size()) == 0) {
        return std::string{buf.data()};
    }
    return std::string{"errno="} + std::to_string(err);
#endif
}

[[nodiscard]] std::expected<Fd, std::string>
connect_tcp(std::string const& host, std::uint16_t port, std::chrono::milliseconds timeout) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    auto const port_str = std::to_string(port);
    addrinfo* res = nullptr;
    int const rc = ::getaddrinfo(host.c_str(), port_str.c_str(), &hints, &res);
    if (rc != 0) {
        return std::unexpected(std::string{"getaddrinfo: "} + ::gai_strerror(rc));
    }

    std::string last_err = "no addresses returned";
    for (addrinfo const* p = res; p != nullptr; p = p->ai_next) {
        Fd sock{::socket(p->ai_family, p->ai_socktype, p->ai_protocol)};
        if (!sock.valid()) {
            last_err = "socket: " + strerror_safe(errno);
            continue;
        }

        timeval const tv{
            .tv_sec = static_cast<time_t>(timeout.count() / 1000),
            .tv_usec = static_cast<suseconds_t>((timeout.count() % 1000) * 1000),
        };
        ::setsockopt(sock.get(), SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        ::setsockopt(sock.get(), SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

        if (::connect(sock.get(), p->ai_addr, p->ai_addrlen) == 0) {
            ::freeaddrinfo(res);
            return sock;
        }
        last_err = "connect " + host + ':' + port_str + ": " + strerror_safe(errno);
    }
    ::freeaddrinfo(res);
    return std::unexpected(last_err);
}

[[nodiscard]] std::expected<void, std::string> send_all(int fd, std::string_view data) {
    while (!data.empty()) {
        auto const sent = ::send(fd, data.data(), data.size(), 0);
        if (sent < 0) {
            if (errno == EINTR) {
                continue;
            }
            return std::unexpected(std::string{"send: "} + strerror_safe(errno));
        }
        data.remove_prefix(static_cast<std::size_t>(sent));
    }
    return {};
}

[[nodiscard]] std::expected<std::string, std::string> recv_until_close(int fd) {
    std::string out;
    std::array<char, recv_chunk> buf{};
    while (true) {
        auto const n = ::recv(fd, buf.data(), buf.size(), 0);
        if (n > 0) {
            out.append(buf.data(), static_cast<std::size_t>(n));
            continue;
        }
        if (n == 0) {
            return out;
        }
        if (errno == EINTR) {
            continue;
        }
        return std::unexpected(std::string{"recv: "} + strerror_safe(errno));
    }
}

[[nodiscard]] std::string ascii_lower(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char const c : s) {
        out.push_back((c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c);
    }
    return out;
}

[[nodiscard]] std::string trim(std::string_view s) {
    auto const not_space = [](char c) { return c != ' ' && c != '\t'; };
    auto const begin = std::find_if(s.begin(), s.end(), not_space);
    auto const end = std::find_if(s.rbegin(), s.rend(), not_space).base();
    if (begin >= end) {
        return {};
    }
    return std::string{begin, end};
}

[[nodiscard]] std::expected<HttpResponse, std::string> parse_response(std::string raw) {
    constexpr std::string_view header_terminator = "\r\n\r\n";
    auto const header_end = raw.find(header_terminator);
    if (header_end == std::string::npos) {
        return std::unexpected("malformed response: header terminator not found");
    }

    HttpResponse resp;
    std::string_view const head{raw.data(), header_end};
    auto pos = std::size_t{0};

    auto const status_end = head.find("\r\n");
    if (status_end == std::string_view::npos) {
        return std::unexpected("malformed response: missing status line");
    }
    auto const status_line = head.substr(0, status_end);
    auto const sp1 = status_line.find(' ');
    if (sp1 == std::string_view::npos) {
        return std::unexpected("malformed status line");
    }
    auto const sp2 = status_line.find(' ', sp1 + 1);
    auto const code_str =
        status_line.substr(sp1 + 1, (sp2 == std::string_view::npos ? sp2 : sp2 - sp1 - 1));
    int code = 0;
    auto const* first = code_str.data();
    auto const* last = code_str.data() + code_str.size();
    auto const r = std::from_chars(first, last, code);
    if (r.ec != std::errc{}) {
        return std::unexpected("malformed status code");
    }
    resp.status = code;
    if (sp2 != std::string_view::npos) {
        resp.status_text = std::string{status_line.substr(sp2 + 1)};
    }

    pos = status_end + 2;
    while (pos < head.size()) {
        auto const line_end = head.find("\r\n", pos);
        if (line_end == std::string_view::npos) {
            break;
        }
        auto const line = head.substr(pos, line_end - pos);
        pos = line_end + 2;
        auto const colon = line.find(':');
        if (colon == std::string_view::npos) {
            continue;
        }
        resp.headers.emplace_back(ascii_lower(line.substr(0, colon)), trim(line.substr(colon + 1)));
    }

    resp.body = raw.substr(header_end + header_terminator.size());
    return resp;
}

} // namespace

std::expected<HttpUrl, std::string> parse_http_url(std::string_view url) {
    constexpr std::string_view scheme = "http://";
    if (url.size() < scheme.size() || ascii_lower(url.substr(0, scheme.size())) != scheme) {
        return std::unexpected("URL must start with http://");
    }
    auto rest = url.substr(scheme.size());

    auto const path_start = rest.find('/');
    auto const authority =
        (path_start == std::string_view::npos) ? rest : rest.substr(0, path_start);
    auto const path =
        (path_start == std::string_view::npos) ? std::string_view{"/"} : rest.substr(path_start);

    HttpUrl out;
    auto const colon = authority.find(':');
    if (colon == std::string_view::npos) {
        out.host = std::string{authority};
    } else {
        out.host = std::string{authority.substr(0, colon)};
        auto const port_str = authority.substr(colon + 1);
        std::uint32_t port = 0;
        auto const* first = port_str.data();
        auto const* last = port_str.data() + port_str.size();
        auto const r = std::from_chars(first, last, port);
        if (r.ec != std::errc{} || port == 0 || port > 65535) {
            return std::unexpected("invalid port");
        }
        out.port = static_cast<std::uint16_t>(port);
    }
    if (out.host.empty()) {
        return std::unexpected("empty host");
    }
    out.path = std::string{path};
    return out;
}

std::expected<HttpResponse, std::string> http_request(HttpRequest const& req) {
    auto sock = connect_tcp(req.url.host, req.url.port, req.timeout);
    if (!sock.has_value()) {
        return std::unexpected(sock.error());
    }

    std::string wire;
    wire.reserve(256 + req.body.size());
    wire.append(req.method);
    wire.push_back(' ');
    wire.append(req.url.path);
    wire.append(" HTTP/1.1\r\nHost: ");
    wire.append(req.url.host);
    if (req.url.port != 80) {
        wire.push_back(':');
        wire.append(std::to_string(req.url.port));
    }
    wire.append("\r\nUser-Agent: sonariumctl/0.1\r\nConnection: close\r\n");

    bool has_content_length = false;
    for (auto const& [k, v] : req.headers) {
        if (ascii_lower(k) == "content-length") {
            has_content_length = true;
        }
        wire.append(k);
        wire.append(": ");
        wire.append(v);
        wire.append("\r\n");
    }
    if (!has_content_length && !req.body.empty()) {
        wire.append("Content-Length: ");
        wire.append(std::to_string(req.body.size()));
        wire.append("\r\n");
    }
    wire.append("\r\n");
    wire.append(req.body);

    if (auto sent = send_all(sock->get(), wire); !sent.has_value()) {
        return std::unexpected(sent.error());
    }

    auto raw = recv_until_close(sock->get());
    if (!raw.has_value()) {
        return std::unexpected(raw.error());
    }
    return parse_response(std::move(*raw));
}

} // namespace sonarium::cli
