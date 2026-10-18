// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Minimal HTTP/1.1 server on the loopback interface, used by the WinHttpClient integration tests.
// It runs real Winsock sockets so the client is exercised end to end (proxy bypass, connection
// setup, Content-Length / chunked / close-delimited framing, redirects, stalls, aborted transfers).
//
//   LoopbackHttpServer server;
//   server.Route("/ok", [](const RecordedRequest&, ResponseWriter& out) { out.WriteResponse(200, "OK", "hi"); });
//   REQUIRE(server.Start());
//   request.url = server.Url("/ok");               // http://127.0.0.1:<random port>/ok
//   request.url = server.Url("/ok", "localhost");  // same server, different host name
//
// It listens on 127.0.0.1 and, when available, on [::1] with the same random port, so "localhost"
// reaches it whichever address the resolver returns first. Each connection is served on its own
// thread and carries exactly one request; every response says "Connection: close", so tests never
// depend on keep-alive behaviour. Test-only code: no TLS, no pipelining, request bodies only with
// Content-Length.
#pragma once

#include <winsock2.h>
#include <ws2tcpip.h>

#include "core/Http.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace aether::net::testing {

struct RecordedRequest {
    std::string method;
    std::string target;  // request target as received ("/path?query")
    std::string path;    // target without the query
    std::vector<HttpHeader> headers;
    std::string body;
    bool overIpv6 = false;

    [[nodiscard]] const std::string* FindHeader(std::string_view name) const noexcept;
};

class LoopbackHttpServer;

// Writes one response to the client. Every write returns false once the client has gone away.
class ResponseWriter {
public:
    ResponseWriter(const ResponseWriter&) = delete;
    ResponseWriter& operator=(const ResponseWriter&) = delete;

    bool Write(std::string_view bytes);
    // Status line, `headers`, Content-Length when given, and "Connection: close".
    bool WriteHead(int status, std::string_view reason, const std::vector<HttpHeader>& headers = {},
                   std::optional<std::uint64_t> contentLength = std::nullopt);
    // Complete response framed with Content-Length.
    bool WriteResponse(int status, std::string_view reason, std::string_view body,
                       const std::vector<HttpHeader>& headers = {});
    // Chunked transfer coding (send "Transfer-Encoding: chunked" in WriteHead first).
    bool WriteChunk(std::string_view data);
    bool FinishChunks();

    // Blocks until the client closes the connection (returns true), the server stops or `timeout`
    // elapses (returns false). Used to stall a response without ever sending the rest.
    bool WaitForDisconnect(std::chrono::milliseconds timeout);

    [[nodiscard]] bool ClientGone() const noexcept { return clientGone_; }

private:
    friend class LoopbackHttpServer;
    ResponseWriter(LoopbackHttpServer& server, SOCKET socket) noexcept : server_(server), socket_(socket) {}
    void MarkClientGone() noexcept;

    LoopbackHttpServer& server_;
    SOCKET socket_;
    bool clientGone_ = false;
};

class LoopbackHttpServer {
public:
    using Handler = std::function<void(const RecordedRequest&, ResponseWriter&)>;

    LoopbackHttpServer();
    ~LoopbackHttpServer();  // Stop()
    LoopbackHttpServer(const LoopbackHttpServer&) = delete;
    LoopbackHttpServer& operator=(const LoopbackHttpServer&) = delete;

    // Exact path match (query ignored). Unrouted paths get "404 no route". Thread-safe.
    void Route(std::string path, Handler handler);

    // Binds a random loopback port and starts accepting. False when sockets are unavailable.
    [[nodiscard]] bool Start();
    // Stops accepting, releases stalled handlers and joins every thread. Idempotent.
    void Stop() noexcept;

    [[nodiscard]] std::uint16_t Port() const noexcept { return port_; }
    [[nodiscard]] bool HasIpv6() const noexcept { return listen6_ != INVALID_SOCKET; }
    [[nodiscard]] bool IsStopping() const noexcept { return stopping_.load(std::memory_order_acquire); }

    // "http://<host>:<port><pathAndQuery>"; IPv6 literals are bracketed.
    [[nodiscard]] std::string Url(std::string_view pathAndQuery, std::string_view host = "127.0.0.1") const;

    [[nodiscard]] std::vector<RecordedRequest> Requests() const;
    [[nodiscard]] std::vector<RecordedRequest> RequestsTo(std::string_view path) const;
    [[nodiscard]] std::size_t CountRequests(std::string_view path) const;
    bool WaitForRequests(std::string_view path, std::size_t count, std::chrono::milliseconds timeout) const;

    // Number of responses during which the client went away before the handler finished.
    [[nodiscard]] int Disconnects() const;
    bool WaitForDisconnects(int count, std::chrono::milliseconds timeout) const;

private:
    friend class ResponseWriter;

    void AcceptLoop();
    void Serve(SOCKET client, bool overIpv6);
    [[nodiscard]] std::optional<RecordedRequest> ReadRequest(SOCKET client) const;
    // Waits until `socket` is readable; false on timeout, error or server stop.
    [[nodiscard]] bool WaitReadable(SOCKET socket, std::chrono::steady_clock::time_point deadline) const;
    void NoteDisconnect();

    bool winsockReady_ = false;
    SOCKET listen4_ = INVALID_SOCKET;
    SOCKET listen6_ = INVALID_SOCKET;
    std::uint16_t port_ = 0;
    std::atomic<bool> stopping_{false};
    std::thread acceptThread_;

    mutable std::mutex mutex_;
    mutable std::condition_variable changed_;
    std::map<std::string, Handler, std::less<>> routes_;
    std::vector<std::thread> workers_;
    std::vector<RecordedRequest> requests_;
    int disconnects_ = 0;
};

}  // namespace aether::net::testing
