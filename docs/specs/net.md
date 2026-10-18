<!-- Generated from the session's agent workflow; edit freely. -->
# Module specification: net (phase A)

Read `AGENTS.md` first; it states the rules every implementer follows. This is the work order that
was given to the implementing engineer. The real headers in the repository are authoritative where
they differ from names used here.

Module: net (directory src/platform/net, target aether_net; deps: aether_core aether_diagnostics aether_platform winhttp, plus ws2_32 only for tests if needed).
Your files: src/platform/net/**, tests/unit/net/**, tests/integration/net/**, tests/network/net/**, docs/modules/net.md.
Implement WinHttpClient : aether::net::IHttpClient (src/core/Http.h) with WinHTTP in synchronous mode on the caller's worker thread:
- Session: WinHttpOpen with WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY; User-Agent "AetherShell/<aether::build::kVersion> (+https://github.com/ibrahimbouillet/AetherShell)" (include "aether/BuildInfo.h"); TLS 1.2 and 1.3 only via WINHTTP_OPTION_SECURE_PROTOCOLS (fall back to TLS 1.2 only if the 1.3 flag is rejected); HTTP/2 when available; optional decompression.
- URL validation with WinHttpCrackUrl: https only (http only when allowInsecureLoopback is set AND the host is 127.0.0.1, ::1 or localhost); reject user info in the URL and empty hosts.
- Per-request timeouts (WinHttpSetTimeouts: resolve/connect = connectTimeout, send/receive = receiveTimeout).
- Redirects: disable automatic redirects (WINHTTP_OPTION_REDIRECT_POLICY NEVER) and follow 301/302/303/307/308 manually up to maxRedirects, only to https URLs whose host matches allowedRedirectHosts (case-insensitive exact match); otherwise fail with a clear "redirect to untrusted host" error. 303 switches to GET. Report finalUrl.
- Size limits: Content-Length above maxResponseBytes fails before reading; a streamed body exceeding the limit aborts with ErrorCode::Integrity.
- Cancellation: CancellationToken::OnCancel closes the request handle (WinHttpCloseHandle) to abort blocking calls, then return ErrorCode::Cancelled. Map ERROR_WINHTTP_TIMEOUT to Timeout; name resolution, connect and connection errors to Network; TLS failures to Network with a clear message. Never ignore certificate errors (no SECURITY_FLAG_IGNORE_* anywhere).
- Parse response headers (WINHTTP_QUERY_RAW_HEADERS_CRLF) into HttpHeader entries; status code.
- SendStreaming delivers 64 KB chunks to the sink and reports progress (total from Content-Length when present).
- Thread-safe: one WinHttpClient can be used concurrently from several worker threads (shared session, per-call connection and request handles).
Tests: unit tests for URL validation and redirect host policy (factor them into pure functions). Integration tests against an in-process loopback HTTP/1.1 server you write with Winsock on 127.0.0.1 (random port, allowInsecureLoopback): 200 body and headers; 404 returned as success with status 404; Content-Length over the limit; a streamed (no Content-Length or chunked) body over the limit aborts; redirect to an allowed host versus a disallowed host (use "localhost" and "127.0.0.1" as different host names); redirect loop limit; receive timeout; cancellation mid-transfer; streaming chunks and progress. Network test (tests/network/net, label network): GET https://api.github.com/zen with proper headers returns 200, or 403/429 when rate-limited; run it once to prove TLS works on this machine.
