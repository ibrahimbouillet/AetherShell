// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "LoopbackHttpServer.h"

#include "core/Strings.h"

#include <algorithm>
#include <charconv>
#include <format>
#include <utility>

namespace aether::net::testing {
namespace {

using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;

constexpr std::size_t kMaxRequestHead = 64 * 1024;
constexpr std::size_t kMaxRequestBody = 4 * 1024 * 1024;
constexpr auto kRequestReadTimeout = 5s;
constexpr auto kDrainTimeout = 2s;
constexpr int kPollSliceMs = 50;

void CloseSocket(SOCKET& socket) noexcept {
    if (socket != INVALID_SOCKET) ::closesocket(std::exchange(socket, INVALID_SOCKET));
}

// Listening socket bound to a loopback address. SO_EXCLUSIVEADDRUSE stops another process from
// binding the same port and intercepting the test traffic.
SOCKET OpenListener(int family, std::uint16_t port, int* error) {
    *error = 0;
    SOCKET socket = ::socket(family, SOCK_STREAM, IPPROTO_TCP);
    if (socket == INVALID_SOCKET) {
        *error = ::WSAGetLastError();
        return INVALID_SOCKET;
    }
    BOOL exclusive = TRUE;
    ::setsockopt(socket, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&exclusive),
                 sizeof(exclusive));
    int bound = SOCKET_ERROR;
    if (family == AF_INET) {
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = ::htons(port);
        address.sin_addr.s_addr = ::htonl(INADDR_LOOPBACK);
        bound = ::bind(socket, reinterpret_cast<const sockaddr*>(&address), sizeof(address));
    } else {
        DWORD v6Only = 1;
        ::setsockopt(socket, IPPROTO_IPV6, IPV6_V6ONLY, reinterpret_cast<const char*>(&v6Only), sizeof(v6Only));
        sockaddr_in6 address{};
        address.sin6_family = AF_INET6;
        address.sin6_port = ::htons(port);
        address.sin6_addr = in6addr_loopback;
        bound = ::bind(socket, reinterpret_cast<const sockaddr*>(&address), sizeof(address));
    }
    if (bound == SOCKET_ERROR || ::listen(socket, SOMAXCONN) == SOCKET_ERROR) {
        *error = ::WSAGetLastError();
        CloseSocket(socket);
    }
    return socket;
}

std::uint16_t LocalPort(SOCKET socket) {
    sockaddr_storage address{};
    int length = sizeof(address);
    if (::getsockname(socket, reinterpret_cast<sockaddr*>(&address), &length) == SOCKET_ERROR) return 0;
    if (address.ss_family == AF_INET) return ::ntohs(reinterpret_cast<const sockaddr_in&>(address).sin_port);
    return ::ntohs(reinterpret_cast<const sockaddr_in6&>(address).sin6_port);
}

std::optional<std::uint64_t> ParseLength(std::string_view text) {
    text = text::Trim(text);
    std::uint64_t value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (text.empty() || error != std::errc{} || end != text.data() + text.size()) return std::nullopt;
    return value;
}

}  // namespace

const std::string* RecordedRequest::FindHeader(std::string_view name) const noexcept {
    for (const auto& header : headers) {
        if (text::EqualsIgnoreCaseAscii(header.name, name)) return &header.value;
    }
    return nullptr;
}

// ---- ResponseWriter --------------------------------------------------------------------------

void ResponseWriter::MarkClientGone() noexcept {
    if (clientGone_) return;
    clientGone_ = true;
    server_.NoteDisconnect();
}

bool ResponseWriter::Write(std::string_view bytes) {
    while (!bytes.empty() && !clientGone_) {
        const int size = static_cast<int>(std::min<std::size_t>(bytes.size(), 64 * 1024));
        const int sent = ::send(socket_, bytes.data(), size, 0);
        if (sent == SOCKET_ERROR || sent <= 0) {
            MarkClientGone();
            break;
        }
        bytes.remove_prefix(static_cast<std::size_t>(sent));
    }
    return !clientGone_;
}

bool ResponseWriter::WriteHead(int status, std::string_view reason, const std::vector<HttpHeader>& headers,
                               std::optional<std::uint64_t> contentLength) {
    std::string head = std::format("HTTP/1.1 {} {}\r\n", status, reason);
    for (const auto& header : headers) head += std::format("{}: {}\r\n", header.name, header.value);
    if (contentLength) head += std::format("Content-Length: {}\r\n", *contentLength);
    head += "Connection: close\r\n\r\n";
    return Write(head);
}

bool ResponseWriter::WriteResponse(int status, std::string_view reason, std::string_view body,
                                   const std::vector<HttpHeader>& headers) {
    return WriteHead(status, reason, headers, body.size()) && Write(body);
}

bool ResponseWriter::WriteChunk(std::string_view data) {
    if (data.empty()) return !clientGone_;  // an empty chunk would terminate the body
    return Write(std::format("{:x}\r\n", data.size())) && Write(data) && Write("\r\n");
}

bool ResponseWriter::FinishChunks() { return Write("0\r\n\r\n"); }

bool ResponseWriter::WaitForDisconnect(std::chrono::milliseconds timeout) {
    const auto deadline = Clock::now() + timeout;
    while (!clientGone_ && !server_.IsStopping() && Clock::now() < deadline) {
        WSAPOLLFD entry{socket_, POLLRDNORM, 0};
        const int ready = ::WSAPoll(&entry, 1, kPollSliceMs);
        if (ready == SOCKET_ERROR) {
            MarkClientGone();
            break;
        }
        if (ready == 0) continue;
        if ((entry.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
            MarkClientGone();
            break;
        }
        char scratch[1024];
        if (::recv(socket_, scratch, sizeof(scratch), 0) <= 0) {
            MarkClientGone();
            break;
        }
        // Unexpected extra bytes from the client are discarded.
    }
    return clientGone_;
}

// ---- LoopbackHttpServer ----------------------------------------------------------------------

LoopbackHttpServer::LoopbackHttpServer() {
    WSADATA data{};
    winsockReady_ = ::WSAStartup(MAKEWORD(2, 2), &data) == 0;
}

LoopbackHttpServer::~LoopbackHttpServer() {
    Stop();
    if (winsockReady_) ::WSACleanup();
}

void LoopbackHttpServer::Route(std::string path, Handler handler) {
    std::lock_guard lock(mutex_);
    routes_[std::move(path)] = std::move(handler);
}

bool LoopbackHttpServer::Start() {
    if (!winsockReady_ || listen4_ != INVALID_SOCKET) return false;
    // Pick a random IPv4 port, then claim the same port on [::1]. If that port is taken on IPv6,
    // try again: "localhost" may resolve to ::1 first and must never reach another process.
    for (int attempt = 0; attempt < 20 && listen4_ == INVALID_SOCKET; ++attempt) {
        int error = 0;
        SOCKET v4 = OpenListener(AF_INET, 0, &error);
        if (v4 == INVALID_SOCKET) return false;
        const std::uint16_t port = LocalPort(v4);
        if (port == 0) {
            CloseSocket(v4);
            return false;
        }
        SOCKET v6 = OpenListener(AF_INET6, port, &error);
        if (v6 == INVALID_SOCKET && (error == WSAEADDRINUSE || error == WSAEACCES)) {
            CloseSocket(v4);
            continue;
        }
        // Either both listeners are up, or IPv6 loopback is unavailable on this machine (IPv4 only).
        listen4_ = v4;
        listen6_ = v6;
        port_ = port;
    }
    if (listen4_ == INVALID_SOCKET) return false;
    stopping_.store(false, std::memory_order_release);
    acceptThread_ = std::thread([this] { AcceptLoop(); });
    return true;
}

void LoopbackHttpServer::Stop() noexcept {
    stopping_.store(true, std::memory_order_release);
    if (acceptThread_.joinable()) acceptThread_.join();
    CloseSocket(listen4_);
    CloseSocket(listen6_);
    std::vector<std::thread> workers;
    {
        std::lock_guard lock(mutex_);
        workers.swap(workers_);
    }
    for (auto& worker : workers) {
        if (worker.joinable()) worker.join();
    }
    changed_.notify_all();
}

std::string LoopbackHttpServer::Url(std::string_view pathAndQuery, std::string_view host) const {
    const bool ipv6 = host.find(':') != std::string_view::npos;
    return std::format("http://{}{}{}:{}{}", ipv6 ? "[" : "", host, ipv6 ? "]" : "", port_, pathAndQuery);
}

std::vector<RecordedRequest> LoopbackHttpServer::Requests() const {
    std::lock_guard lock(mutex_);
    return requests_;
}

std::vector<RecordedRequest> LoopbackHttpServer::RequestsTo(std::string_view path) const {
    std::lock_guard lock(mutex_);
    std::vector<RecordedRequest> matching;
    for (const auto& request : requests_) {
        if (request.path == path) matching.push_back(request);
    }
    return matching;
}

std::size_t LoopbackHttpServer::CountRequests(std::string_view path) const {
    std::lock_guard lock(mutex_);
    return static_cast<std::size_t>(
        std::count_if(requests_.begin(), requests_.end(), [&](const RecordedRequest& r) { return r.path == path; }));
}

bool LoopbackHttpServer::WaitForRequests(std::string_view path, std::size_t count,
                                         std::chrono::milliseconds timeout) const {
    std::unique_lock lock(mutex_);
    return changed_.wait_for(lock, timeout, [&] {
        return static_cast<std::size_t>(std::count_if(requests_.begin(), requests_.end(),
                                                      [&](const RecordedRequest& r) { return r.path == path; })) >=
               count;
    });
}

int LoopbackHttpServer::Disconnects() const {
    std::lock_guard lock(mutex_);
    return disconnects_;
}

bool LoopbackHttpServer::WaitForDisconnects(int count, std::chrono::milliseconds timeout) const {
    std::unique_lock lock(mutex_);
    return changed_.wait_for(lock, timeout, [&] { return disconnects_ >= count; });
}

void LoopbackHttpServer::NoteDisconnect() {
    std::lock_guard lock(mutex_);
    ++disconnects_;
    changed_.notify_all();
}

void LoopbackHttpServer::AcceptLoop() {
    while (!IsStopping()) {
        WSAPOLLFD entries[2]{};
        ULONG count = 0;
        entries[count++] = WSAPOLLFD{listen4_, POLLRDNORM, 0};
        if (listen6_ != INVALID_SOCKET) entries[count++] = WSAPOLLFD{listen6_, POLLRDNORM, 0};
        const int ready = ::WSAPoll(entries, count, kPollSliceMs);
        if (ready == SOCKET_ERROR) {
            ::Sleep(10);
            continue;
        }
        for (ULONG i = 0; i < count && ready > 0; ++i) {
            if ((entries[i].revents & POLLRDNORM) == 0) continue;
            const SOCKET client = ::accept(entries[i].fd, nullptr, nullptr);
            if (client == INVALID_SOCKET) continue;
            const bool overIpv6 = entries[i].fd == listen6_;
            std::lock_guard lock(mutex_);
            workers_.emplace_back([this, client, overIpv6] { Serve(client, overIpv6); });
        }
    }
}

bool LoopbackHttpServer::WaitReadable(SOCKET socket, Clock::time_point deadline) const {
    while (!IsStopping() && Clock::now() < deadline) {
        WSAPOLLFD entry{socket, POLLRDNORM, 0};
        const int ready = ::WSAPoll(&entry, 1, kPollSliceMs);
        if (ready == SOCKET_ERROR) return false;
        if (ready > 0) return true;  // readable, or closed/failed (recv reports which)
    }
    return false;
}

std::optional<RecordedRequest> LoopbackHttpServer::ReadRequest(SOCKET client) const {
    const auto deadline = Clock::now() + kRequestReadTimeout;
    std::string data;
    char buffer[16 * 1024];
    std::size_t headEnd = std::string::npos;
    while ((headEnd = data.find("\r\n\r\n")) == std::string::npos) {
        if (data.size() > kMaxRequestHead) return std::nullopt;
        // Not HTTP (for example a TLS ClientHello from an https:// request): give up at once.
        if (!data.empty() && (data.front() < 'A' || data.front() > 'Z')) return std::nullopt;
        if (!WaitReadable(client, deadline)) return std::nullopt;
        const int received = ::recv(client, buffer, sizeof(buffer), 0);
        if (received <= 0) return std::nullopt;
        data.append(buffer, static_cast<std::size_t>(received));
    }

    RecordedRequest request;
    std::string_view head(data.data(), headEnd);
    const auto lineEnd = head.find("\r\n");
    const std::string_view requestLine = head.substr(0, lineEnd);
    head = lineEnd == std::string_view::npos ? std::string_view{} : head.substr(lineEnd + 2);

    const auto firstSpace = requestLine.find(' ');
    const auto secondSpace = requestLine.find(' ', firstSpace == std::string_view::npos ? 0 : firstSpace + 1);
    if (firstSpace == std::string_view::npos || secondSpace == std::string_view::npos) return std::nullopt;
    request.method = std::string(requestLine.substr(0, firstSpace));
    request.target = std::string(requestLine.substr(firstSpace + 1, secondSpace - firstSpace - 1));
    request.path = request.target.substr(0, request.target.find('?'));

    while (!head.empty()) {
        const auto end = head.find("\r\n");
        const std::string_view line = head.substr(0, end);
        head = end == std::string_view::npos ? std::string_view{} : head.substr(end + 2);
        const auto colon = line.find(':');
        if (colon == std::string_view::npos) continue;
        request.headers.push_back(
            HttpHeader{std::string(text::Trim(line.substr(0, colon))), std::string(text::Trim(line.substr(colon + 1)))});
    }

    std::uint64_t bodyLength = 0;
    if (const auto* length = request.FindHeader("Content-Length")) {
        const auto parsed = ParseLength(*length);
        if (!parsed || *parsed > kMaxRequestBody) return std::nullopt;
        bodyLength = *parsed;
    }
    const std::size_t bodyStart = headEnd + 4;
    while (data.size() - bodyStart < bodyLength) {
        if (!WaitReadable(client, deadline)) return std::nullopt;
        const int received = ::recv(client, buffer, sizeof(buffer), 0);
        if (received <= 0) return std::nullopt;
        data.append(buffer, static_cast<std::size_t>(received));
    }
    request.body = data.substr(bodyStart, static_cast<std::size_t>(bodyLength));
    return request;
}

void LoopbackHttpServer::Serve(SOCKET client, bool overIpv6) {
    DWORD sendTimeout = 10'000;
    ::setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&sendTimeout), sizeof(sendTimeout));

    if (auto request = ReadRequest(client)) {
        request->overIpv6 = overIpv6;
        Handler handler;
        {
            std::lock_guard lock(mutex_);
            requests_.push_back(*request);
            if (const auto route = routes_.find(request->path); route != routes_.end()) handler = route->second;
            changed_.notify_all();
        }
        ResponseWriter writer(*this, client);
        try {
            if (handler) {
                handler(*request, writer);
            } else {
                writer.WriteResponse(404, "Not Found", "no route");
            }
        } catch (...) {
            // A throwing handler simply ends the response; the test sees a truncated or empty reply.
        }

        // Graceful close: half-close, then wait briefly for the client to close its side, so the
        // response is not cut short by a reset.
        if (!writer.ClientGone()) {
            ::shutdown(client, SD_SEND);
            const auto deadline = Clock::now() + kDrainTimeout;
            char scratch[1024];
            while (WaitReadable(client, deadline) && ::recv(client, scratch, sizeof(scratch), 0) > 0) {
            }
        }
    }
    ::closesocket(client);
}

}  // namespace aether::net::testing
