// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "platform/net/WinHttpClient.h"

#include "aether/BuildInfo.h"
#include "core/Strings.h"
#include "diagnostics/Logger.h"
#include "platform/net/HttpMessage.h"
#include "platform/net/UrlPolicy.h"
#include "platform/net/WinHttpError.h"
#include "platform/windows/Handles.h"

#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <climits>
#include <exception>
#include <mutex>
#include <new>
#include <utility>
#include <vector>

namespace aether::net {
namespace {

constexpr std::string_view kLog = "net";

// Never reserve more than this up front for a buffered body, whatever Content-Length claims.
constexpr std::uint64_t kMaxInitialReservation = 16ull * 1024 * 1024;

struct InternetHandleTraits {
    using Type = HINTERNET;
    static Type Invalid() noexcept { return nullptr; }
    static void Close(Type handle) noexcept { ::WinHttpCloseHandle(handle); }
};
using UniqueInternet = platform::UniqueResource<InternetHandleTraits>;

Error CancelledError() { return MakeError(ErrorCode::Cancelled, "the HTTP request was cancelled"); }

int ToTimeout(std::chrono::milliseconds value) noexcept {
    return static_cast<int>(std::clamp<long long>(value.count(), 1, INT_MAX));
}

bool SetDwordOption(HINTERNET handle, DWORD option, DWORD value) noexcept {
    return ::WinHttpSetOption(handle, option, &value, sizeof(value)) != FALSE;
}

bool SameOrigin(const ParsedUrl& a, const ParsedUrl& b) noexcept {
    return a.secure == b.secure && a.port == b.port && text::EqualsIgnoreCaseAscii(a.host, b.host);
}

// Synchronous calls report every certificate problem as ERROR_WINHTTP_SECURE_FAILURE; the precise
// reason arrives through the WINHTTP_CALLBACK_STATUS_SECURE_FAILURE notification, which WinHTTP also
// delivers in synchronous mode (on the calling thread, inside WinHttpSendRequest).
struct SecureFailureContext {
    std::atomic<DWORD> flags{0};
};

void CALLBACK OnWinHttpStatus(HINTERNET, DWORD_PTR context, DWORD status, LPVOID information, DWORD length) {
    if (status != WINHTTP_CALLBACK_STATUS_SECURE_FAILURE || context == 0 || !information || length < sizeof(DWORD)) {
        return;
    }
    auto* target = reinterpret_cast<SecureFailureContext*>(context);
    target->flags.fetch_or(*static_cast<const DWORD*>(information), std::memory_order_relaxed);
}

// Owns a WinHTTP request handle and serialises cancellation against the worker's use of it.
//
// In synchronous mode the only way to abort a blocking WinHTTP call is to close its handle from
// another thread. Cancel() closes the handle only while the worker is inside a WinHTTP call (which
// then fails promptly); between calls it merely records the cancellation, and Run() refuses to start
// another call. The worker therefore never begins a call on a handle it knows to be closed and the
// handle is closed exactly once. The one window left is the few instructions between Run() publishing
// `inCall_` and WinHTTP taking its own reference on the handle: if Cancel() closes the handle exactly
// there, the call normally fails with ERROR_INVALID_HANDLE (reported as Cancelled). In theory WinHTTP
// could hand the same handle value to another thread's new request in between; WinHTTP offers no way
// to close that window in synchronous mode (asynchronous mode would), so it is documented in
// docs/modules/net.md as a known limitation.
class GuardedRequest {
public:
    GuardedRequest() = default;
    GuardedRequest(const GuardedRequest&) = delete;
    GuardedRequest& operator=(const GuardedRequest&) = delete;
    ~GuardedRequest() { Close(); }

    void Attach(HINTERNET handle) noexcept {
        std::lock_guard lock(mutex_);
        handle_ = handle;
    }

    // Runs one WinHTTP call on the handle; returns ERROR_SUCCESS or the call's error code.
    template <class Call>
    DWORD Run(Call&& call) {
        HINTERNET handle = nullptr;
        {
            std::lock_guard lock(mutex_);
            if (cancelled_ || !handle_) return ERROR_WINHTTP_OPERATION_CANCELLED;
            handle = handle_;
            inCall_ = true;
        }
        const BOOL ok = call(handle);
        const DWORD error = ok ? ERROR_SUCCESS : ::GetLastError();
        {
            std::lock_guard lock(mutex_);
            inCall_ = false;
        }
        return error;
    }

    // Cancellation callback; may run on any thread.
    void Cancel() noexcept {
        HINTERNET toClose = nullptr;
        {
            std::lock_guard lock(mutex_);
            cancelled_ = true;
            if (inCall_) toClose = std::exchange(handle_, nullptr);
        }
        if (toClose) ::WinHttpCloseHandle(toClose);
    }

    [[nodiscard]] bool IsCancelled() const noexcept {
        std::lock_guard lock(mutex_);
        return cancelled_;
    }

    void Close() noexcept {
        HINTERNET toClose = nullptr;
        {
            std::lock_guard lock(mutex_);
            toClose = std::exchange(handle_, nullptr);
        }
        if (toClose) ::WinHttpCloseHandle(toClose);
    }

private:
    mutable std::mutex mutex_;
    HINTERNET handle_ = nullptr;
    bool inCall_ = false;
    bool cancelled_ = false;
};

// One request/response round trip; following a redirect starts a new one. Members are destroyed in
// reverse order: the cancellation registration first (it waits for a callback running on another
// thread), then the request handle, then the connection, and the status-callback context last.
struct Exchange {
    SecureFailureContext secure;
    UniqueInternet connection;
    GuardedRequest request;
    CancellationToken::Registration registration;
    int status = 0;
    std::vector<HttpHeader> headers;
};

Result<void> InvokeSink(const HttpChunkSink& sink, std::span<const std::byte> chunk) {
    try {
        return sink(chunk);
    } catch (...) {
        return MakeError(ErrorCode::Internal, "the response sink threw an exception");
    }
}

Result<void> InvokeProgress(const HttpProgress& progress, std::uint64_t received, std::optional<std::uint64_t> total) {
    try {
        progress(received, total);
        return Ok();
    } catch (...) {
        return MakeError(ErrorCode::Internal, "the progress callback threw an exception");
    }
}

// Connects, sends the request and receives the status line and headers.
Result<void> OpenExchange(HINTERNET session, Exchange& exchange, const ParsedUrl& url, std::string_view method,
                          std::span<const HttpHeader> headers, std::string_view body, const HttpRequest& request,
                          const CancellationToken& cancel) {
    const std::string origin = url.Origin();

    // WinHttpConnect and WinHttpOpenRequest only allocate handles; no network I/O happens before
    // WinHttpSendRequest.
    const std::wstring host = text::Widen(url.ConnectHost());
    exchange.connection.Reset(::WinHttpConnect(session, host.c_str(), url.port, 0));
    if (!exchange.connection) return MapWinHttpError(::GetLastError(), "opening a connection to " + origin);

    const std::wstring verb = text::Widen(method);
    const std::wstring target = text::Widen(url.pathAndQuery);
    HINTERNET raw = ::WinHttpOpenRequest(exchange.connection.Get(), verb.c_str(), target.c_str(), nullptr,
                                         WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                         url.secure ? WINHTTP_FLAG_SECURE : 0);
    if (!raw) return MapWinHttpError(::GetLastError(), "creating a request for " + origin);
    exchange.request.Attach(raw);

    // Configure before any I/O. The cancellation callback is not registered yet, so `raw` is safe to use.
    const int connectTimeout = ToTimeout(request.connectTimeout);
    const int receiveTimeout = ToTimeout(request.receiveTimeout);
    if (!::WinHttpSetTimeouts(raw, connectTimeout, connectTimeout, receiveTimeout, receiveTimeout) ||
        !SetDwordOption(raw, WINHTTP_OPTION_RECEIVE_RESPONSE_TIMEOUT, static_cast<DWORD>(receiveTimeout)) ||
        !SetDwordOption(raw, WINHTTP_OPTION_REDIRECT_POLICY, WINHTTP_OPTION_REDIRECT_POLICY_NEVER) ||
        !SetDwordOption(raw, WINHTTP_OPTION_DISABLE_FEATURE, WINHTTP_DISABLE_COOKIES | WINHTTP_DISABLE_REDIRECTS)) {
        return MapWinHttpError(::GetLastError(), "configuring the request for " + origin);
    }
    if (::WinHttpSetStatusCallback(raw, &OnWinHttpStatus, WINHTTP_CALLBACK_FLAG_SECURE_FAILURE, 0) ==
        WINHTTP_INVALID_STATUS_CALLBACK) {
        // Diagnostics only: certificate failures are still detected and reported, just less precisely.
        AETHER_LOG_DEBUG(kLog, "secure-failure notifications unavailable (error {})", ::GetLastError());
    }

    exchange.registration = cancel.OnCancel([&guard = exchange.request] { guard.Cancel(); });
    if (exchange.request.IsCancelled()) return CancelledError();

    const std::wstring headerBlock = text::Widen(FormatRequestHeaders(headers));
    const auto bodyLength = static_cast<DWORD>(body.size());
    // WinHttpSendRequest takes a non-const pointer but only reads the optional data.
    void* bodyData = body.empty() ? nullptr : const_cast<char*>(body.data());
    DWORD error = exchange.request.Run([&](HINTERNET handle) {
        return ::WinHttpSendRequest(handle, headerBlock.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headerBlock.c_str(),
                                    headerBlock.empty() ? 0 : static_cast<DWORD>(-1), bodyData, bodyLength,
                                    bodyLength, reinterpret_cast<DWORD_PTR>(&exchange.secure));
    });
    if (exchange.request.IsCancelled()) return CancelledError();
    if (error != ERROR_SUCCESS) {
        return MapWinHttpError(error, "sending the request to " + origin, exchange.secure.flags.load());
    }

    error = exchange.request.Run([](HINTERNET handle) { return ::WinHttpReceiveResponse(handle, nullptr); });
    if (exchange.request.IsCancelled()) return CancelledError();
    if (error != ERROR_SUCCESS) {
        return MapWinHttpError(error, "waiting for the response from " + origin, exchange.secure.flags.load());
    }

    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    error = exchange.request.Run([&](HINTERNET handle) {
        return ::WinHttpQueryHeaders(handle, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                     WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX);
    });
    if (exchange.request.IsCancelled()) return CancelledError();
    if (error != ERROR_SUCCESS) return MapWinHttpError(error, "reading the response status from " + origin);
    exchange.status = static_cast<int>(status);

    // Raw headers: ask for the size first, then fetch.
    DWORD bytes = 0;
    error = exchange.request.Run([&](HINTERNET handle) {
        return ::WinHttpQueryHeaders(handle, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX,
                                     WINHTTP_NO_OUTPUT_BUFFER, &bytes, WINHTTP_NO_HEADER_INDEX);
    });
    if (exchange.request.IsCancelled()) return CancelledError();
    if (error != ERROR_INSUFFICIENT_BUFFER) {
        return MapWinHttpError(error == ERROR_SUCCESS ? ERROR_WINHTTP_INVALID_SERVER_RESPONSE : error,
                               "reading the response headers from " + origin);
    }
    std::wstring rawHeaders(bytes / sizeof(wchar_t) + 1, L'\0');
    bytes = static_cast<DWORD>(rawHeaders.size() * sizeof(wchar_t));
    error = exchange.request.Run([&](HINTERNET handle) {
        return ::WinHttpQueryHeaders(handle, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX,
                                     rawHeaders.data(), &bytes, WINHTTP_NO_HEADER_INDEX);
    });
    if (exchange.request.IsCancelled()) return CancelledError();
    if (error != ERROR_SUCCESS) return MapWinHttpError(error, "reading the response headers from " + origin);
    rawHeaders.resize(bytes / sizeof(wchar_t));
    exchange.headers = ParseRawHeaders(text::Narrow(rawHeaders));
    return Ok();
}

// Reads the body in kChunkSize chunks, enforcing the size limit, into `sink` or `response.body`.
Result<void> ReadBody(Exchange& exchange, const ParsedUrl& url, const WinHttpSessionInfo& info,
                      HttpResponse& response, std::uint64_t maxBytes, const HttpChunkSink* sink,
                      const HttpProgress* progress) {
    const std::string origin = url.Origin();
    const auto declared = ParseContentLength(response.headers);
    if (declared && *declared > maxBytes) {
        return MakeError(ErrorCode::Integrity, "the response from " + origin + " declares " +
                                                   std::to_string(*declared) + " bytes, above the limit of " +
                                                   std::to_string(maxBytes) + " bytes");
    }
    // WinHTTP decodes only a lone "gzip" or "deflate" coding, and then removes Content-Length and
    // Content-Encoding from the headers (so `declared` is normally empty here). Every other coding
    // ("br", "x-gzip", "gzip, br"...) reaches us undecoded and its Content-Length is exact, so the
    // total and the length checks below still apply to it. `decoded` only guards against a WinHTTP
    // build that decodes but keeps the headers, where Content-Length would count encoded bytes.
    const bool decoded = info.decompressionEnabled && IsWinHttpDecodedCoding(response.headers);
    // Exact body length, also the progress total; empty when unknown.
    const std::optional<std::uint64_t> exactLength = decoded ? std::nullopt : declared;
    if (!sink && declared) {
        response.body.reserve(static_cast<std::size_t>(std::min({*declared, maxBytes, kMaxInitialReservation})));
    }

    std::vector<std::byte> buffer(WinHttpClient::kChunkSize);
    std::size_t filled = 0;
    std::uint64_t received = 0;
    const auto deliver = [&]() -> Result<void> {
        if (filled == 0) return Ok();
        const std::span<const std::byte> chunk(buffer.data(), filled);
        filled = 0;
        if (sink) return InvokeSink(*sink, chunk);
        response.body.append(reinterpret_cast<const char*>(chunk.data()), chunk.size());
        return Ok();
    };

    for (;;) {
        DWORD read = 0;
        const DWORD error = exchange.request.Run([&](HINTERNET handle) {
            return ::WinHttpReadData(handle, buffer.data() + filled, static_cast<DWORD>(buffer.size() - filled), &read);
        });
        if (exchange.request.IsCancelled()) return CancelledError();
        if (error != ERROR_SUCCESS) return MapWinHttpError(error, "reading the response body from " + origin);
        if (read == 0) break;  // end of body

        received += read;
        if (received > maxBytes) {
            return MakeError(ErrorCode::Integrity, "the response body from " + origin + " exceeds the limit of " +
                                                       std::to_string(maxBytes) + " bytes");
        }
        if (exactLength && received > *exactLength) {
            // WinHTTP stops at Content-Length unless another framing (chunked) overrides it. A response
            // with both is malformed and a request-smuggling signature (RFC 9112 section 6.3).
            return MakeError(ErrorCode::Network, "the response from " + origin +
                                                     " sent more body bytes than its Content-Length of " +
                                                     std::to_string(*exactLength) + " (conflicting framing)");
        }
        filled += read;
        if (progress) {
            if (auto reported = InvokeProgress(*progress, received, exactLength); !reported) return reported.error();
        }
        if (filled == buffer.size()) {
            if (auto delivered = deliver(); !delivered) return delivered.error();
        }
    }
    if (auto delivered = deliver(); !delivered) return delivered.error();

    if (exactLength && received < *exactLength) {
        return MakeError(ErrorCode::Network, "the connection to " + origin + " closed after " +
                                                 std::to_string(received) + " of " + std::to_string(*exactLength) +
                                                 " bytes");
    }
    return Ok();
}

Result<HttpResponse> Execute(HINTERNET session, const WinHttpSessionInfo& info, const HttpRequest& request,
                             const HttpChunkSink* sink, const HttpProgress* progress,
                             const CancellationToken& cancel) {
    if (cancel.IsCancelled()) return CancelledError();
    if (!IsValidMethod(request.method)) {
        return MakeError(ErrorCode::InvalidArgument, "the HTTP method is not a valid token");
    }
    if (auto valid = ValidateRequestHeaders(request.headers); !valid) return valid.error();
    if (request.connectTimeout.count() <= 0 || request.receiveTimeout.count() <= 0) {
        return MakeError(ErrorCode::InvalidArgument, "HTTP timeouts must be positive");
    }
    if (request.body.size() > MAXDWORD) {
        return MakeError(ErrorCode::InvalidArgument, "the request body is larger than 4 GiB");
    }
    auto parsed = ParseRequestUrl(request.url, request.allowInsecureLoopback);
    if (!parsed) return parsed.error();

    ParsedUrl url = std::move(parsed).value();
    std::string currentUrl = request.url;
    std::string method = request.method;
    std::vector<HttpHeader> headers = request.headers;
    std::string_view body = request.body;
    const int maxRedirects = std::max(request.maxRedirects, 0);
    const auto started = std::chrono::steady_clock::now();

    for (int redirects = 0;;) {
        Exchange exchange;
        if (auto opened = OpenExchange(session, exchange, url, method, headers, body, request, cancel); !opened) {
            AETHER_LOG_DEBUG(kLog, "{} {} failed: {}", method, url.Origin(), opened.error().Describe());
            return opened.error();
        }

        if (IsRedirectStatus(exchange.status)) {
            const int status = exchange.status;
            const std::string* location = FindHeader(exchange.headers, "Location");
            if (!location) {
                return MakeError(ErrorCode::Http,
                                 "HTTP " + std::to_string(status) + " redirect from " + url.Origin() +
                                     " has no Location header",
                                 status);
            }
            if (redirects >= maxRedirects) {
                return MakeError(ErrorCode::Http, "too many redirects (limit " + std::to_string(maxRedirects) + ")",
                                 status);
            }
            auto resolved = ResolveReference(currentUrl, *location);
            if (!resolved) {
                return MakeError(ErrorCode::Http,
                                 "redirect from " + url.Origin() + " has an unusable Location: " +
                                     resolved.error().message,
                                 status);
            }
            auto next = EvaluateRedirect(url, resolved.value(), request.allowedRedirectHosts,
                                         request.allowInsecureLoopback);
            if (!next) {
                Error rejected = next.error();
                rejected.platformCode = status;
                AETHER_LOG_WARN(kLog, "HTTP {} redirect from {} rejected: {}", status, url.Origin(), rejected.message);
                return rejected;
            }
            if (status == 303 && method != "HEAD") {
                // 303 See Other: fetch the new location with GET and without the original body.
                method = "GET";
                body = {};
                std::erase_if(headers, [](const HttpHeader& h) { return text::EqualsIgnoreCaseAscii(h.name, "Content-Type"); });
            }
            if (!SameOrigin(url, next.value())) {
                std::erase_if(headers, [](const HttpHeader& h) { return IsCredentialHeader(h.name); });
            }
            AETHER_LOG_DEBUG(kLog, "following HTTP {} redirect from {} to {}", status, url.Origin(), next->Origin());
            url = std::move(next).value();
            currentUrl = std::move(resolved).value();
            ++redirects;
            continue;
        }

        HttpResponse response;
        response.status = exchange.status;
        response.headers = std::move(exchange.headers);
        response.finalUrl = currentUrl;
        if (ResponseHasBody(method, response.status)) {
            if (auto read = ReadBody(exchange, url, info, response, request.maxResponseBytes, sink, progress); !read) {
                AETHER_LOG_DEBUG(kLog, "{} {} body failed: {}", method, url.Origin(), read.error().Describe());
                return read.error();
            }
        }
        const auto elapsed =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started);
        AETHER_LOG_DEBUG(kLog, "{} {} -> HTTP {} in {} ms", method, url.Origin(), response.status, elapsed.count());
        return response;
    }
}

// Keeps exceptions (allocation failures, throwing callbacks) inside the module.
template <class Function>
Result<HttpResponse> CatchAll(Function&& function) {
    try {
        return function();
    } catch (const std::bad_alloc&) {
        return MakeError(ErrorCode::Internal, "out of memory during an HTTP request");
    } catch (const std::exception& ex) {
        return MakeError(ErrorCode::Internal,
                         "unexpected exception during an HTTP request: " + text::SanitizeForLog(ex.what(), 200));
    } catch (...) {
        return MakeError(ErrorCode::Internal, "unexpected exception during an HTTP request");
    }
}

Error SessionError(DWORD code, std::string_view operation) {
    Error error = MapWinHttpError(code, operation);
    error.code = ErrorCode::Platform;
    return error;
}

}  // namespace

struct WinHttpClient::Session {
    UniqueInternet handle;
    WinHttpSessionInfo info;
};

std::string DefaultUserAgent() {
    std::string agent(build::kProductId);
    agent += '/';
    agent += build::kVersion;
    agent += " (+";
    agent += build::kProjectUrl;
    agent += ')';
    return agent;
}

WinHttpClient::WinHttpClient(std::unique_ptr<Session> session) noexcept : session_(std::move(session)) {}

WinHttpClient::~WinHttpClient() = default;

Result<std::unique_ptr<WinHttpClient>> WinHttpClient::Create(const WinHttpClientOptions& options) {
    try {
        auto session = std::make_unique<Session>();
        session->info.userAgent = DefaultUserAgent();
        const std::wstring agent = text::Widen(session->info.userAgent);
        // Synchronous session (no WINHTTP_FLAG_ASYNC). Automatic proxy uses the system/WPAD configuration.
        session->handle.Reset(::WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                                            WINHTTP_NO_PROXY_BYPASS, 0));
        if (!session->handle) return SessionError(::GetLastError(), "opening the WinHTTP session");
        HINTERNET handle = session->handle.Get();

        // TLS 1.2 and 1.3 only. Windows builds whose WinHTTP predates TLS 1.3 reject that flag.
        if (SetDwordOption(handle, WINHTTP_OPTION_SECURE_PROTOCOLS,
                           WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3)) {
            session->info.tls13Enabled = true;
        } else {
            const DWORD rejected = ::GetLastError();
            if (!SetDwordOption(handle, WINHTTP_OPTION_SECURE_PROTOCOLS, WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2)) {
                return SessionError(::GetLastError(), "restricting the session to TLS 1.2");
            }
            AETHER_LOG_INFO(kLog, "TLS 1.3 is not available in WinHTTP here (error {}); using TLS 1.2 only", rejected);
        }
        // Redirects are evaluated and followed by Execute(), never by WinHTTP.
        if (!SetDwordOption(handle, WINHTTP_OPTION_REDIRECT_POLICY, WINHTTP_OPTION_REDIRECT_POLICY_NEVER)) {
            return SessionError(::GetLastError(), "disabling automatic redirects");
        }
        if (options.enableHttp2) {
            if (SetDwordOption(handle, WINHTTP_OPTION_ENABLE_HTTP_PROTOCOL, WINHTTP_PROTOCOL_FLAG_HTTP2)) {
                session->info.http2Enabled = true;
            } else {
                const DWORD error = ::GetLastError();
                AETHER_LOG_INFO(kLog, "HTTP/2 is not available in WinHTTP here (error {}); using HTTP/1.1", error);
            }
        }
        if (options.enableDecompression) {
            if (SetDwordOption(handle, WINHTTP_OPTION_DECOMPRESSION, WINHTTP_DECOMPRESSION_FLAG_ALL)) {
                session->info.decompressionEnabled = true;
            } else {
                const DWORD error = ::GetLastError();
                AETHER_LOG_INFO(kLog, "WinHTTP decompression is not available here (error {})", error);
            }
        }
        AETHER_LOG_INFO(kLog, "WinHTTP session ready ({}, HTTP/2 {}, decompression {})",
                        session->info.tls13Enabled ? "TLS 1.2-1.3" : "TLS 1.2",
                        session->info.http2Enabled ? "on" : "off", session->info.decompressionEnabled ? "on" : "off");
        return std::unique_ptr<WinHttpClient>(new WinHttpClient(std::move(session)));
    } catch (const std::bad_alloc&) {
        return MakeError(ErrorCode::Internal, "out of memory while creating the WinHTTP session");
    } catch (...) {
        return MakeError(ErrorCode::Internal, "unexpected exception while creating the WinHTTP session");
    }
}

Result<HttpResponse> WinHttpClient::Send(const HttpRequest& request, const CancellationToken& cancel) {
    return CatchAll([&] { return Execute(session_->handle.Get(), session_->info, request, nullptr, nullptr, cancel); });
}

Result<HttpResponse> WinHttpClient::SendStreaming(const HttpRequest& request, const HttpChunkSink& sink,
                                                  const HttpProgress& progress, const CancellationToken& cancel) {
    if (!sink) return MakeError(ErrorCode::InvalidArgument, "SendStreaming requires a chunk sink");
    return CatchAll([&] {
        return Execute(session_->handle.Get(), session_->info, request, &sink, progress ? &progress : nullptr, cancel);
    });
}

const WinHttpSessionInfo& WinHttpClient::SessionInfo() const noexcept { return session_->info; }

}  // namespace aether::net
