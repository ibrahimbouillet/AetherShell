# net — WinHTTP client (`src/platform/net`, target `aether_net`)

## Purpose

`aether_net` provides the production implementation of `aether::net::IHttpClient` (`src/core/Http.h`):
`WinHttpClient`, built on WinHTTP in **synchronous** mode. Updates and weather depend only on the
interface; this module is the one place where HTTP, TLS, proxies and redirects meet the OS.

Dependencies: `aether_core`, `aether_diagnostics`, `aether_platform`, `winhttp` (tests also link `ws2_32`
for the loopback server).

## Public API

| Header | Contents |
| --- | --- |
| `WinHttpClient.h` | `WinHttpClient::Create(WinHttpClientOptions) -> Result<std::unique_ptr<WinHttpClient>>`, `Send`, `SendStreaming`, `SessionInfo()`, `DefaultUserAgent()`, `WinHttpClient::kChunkSize` (64 KiB) |
| `UrlPolicy.h` | Pure URL and redirect policy: `ParseRequestUrl`, `IsLoopbackHost`, `IsRedirectStatus`, `IsRedirectHostAllowed`, `ResolveReference` (RFC 3986), `EvaluateRedirect` |
| `HttpMessage.h` | Pure message helpers: `IsValidMethod`, `ValidateRequestHeaders`, `FormatRequestHeaders`, `ParseRawHeaders`, `ParseContentLength`, `IsWinHttpDecodedCoding`, `ResponseHasBody`, `IsCredentialHeader` |
| `WinHttpError.h` | `MapWinHttpError`, `IsTlsFailure`, `DescribeSecureFailureFlags` |

```cpp
auto client = aether::net::WinHttpClient::Create();          // no network I/O
if (!client) return client.error();
aether::net::HttpRequest request;
request.url = "https://api.github.com/repos/o/r/releases/latest";
request.headers = {{"Accept", "application/vnd.github+json"}};
request.allowedRedirectHosts = {"objects.githubusercontent.com"};
auto response = client.value()->Send(request, cancelToken);  // worker thread only
```

Any HTTP status, including 4xx and 5xx, is a successful `Result`; callers interpret `status`.

## Session

- `WinHttpOpen` with `WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY` (system proxy settings and WPAD).
- User-Agent `AetherShell/<build::kVersion> (+<build::kProjectUrl>)`, which is
  `AetherShell/0.1.0-alpha.1 (+https://github.com/ibrahimbouillet/AetherShell)` for the official build.
- `WINHTTP_OPTION_SECURE_PROTOCOLS` = TLS 1.2 | TLS 1.3. If the OS rejects the TLS 1.3 flag (WinHTTP
  before Windows 11 / Server 2022), the session falls back to TLS 1.2 only; `SessionInfo().tls13Enabled`
  reports whether the flag was accepted (negotiating TLS 1.3 still depends on Schannel and the server).
  TLS 1.0/1.1 and SSL are never enabled.
- HTTP/2 through `WINHTTP_OPTION_ENABLE_HTTP_PROTOCOL` when available (`enableHttp2`, default on).
- Transparent gzip/deflate through `WINHTTP_OPTION_DECOMPRESSION` (`enableDecompression`, default on).
  Measured on Windows 11 build 26300: WinHTTP decodes only a lone `Content-Encoding: gzip` or `deflate`
  and then **removes** `Content-Encoding` and `Content-Length` from the returned headers. Any other value
  (`br`, `x-gzip`, `gzip, br`) is passed through undecoded with its exact `Content-Length`.
- Automatic redirects disabled (`WINHTTP_OPTION_REDIRECT_POLICY_NEVER` on session and request, plus
  `WINHTTP_DISABLE_REDIRECTS`); cookies disabled (`WINHTTP_DISABLE_COOKIES`). Nothing is cached.
- Certificate errors are never ignored: no `SECURITY_FLAG_IGNORE_*` flag is set anywhere.

## Request rules

- **URL** (`ParseRequestUrl`, via `WinHttpCrackUrl`): absolute `https://` only. Plain `http://` only
  when `allowInsecureLoopback` is set **and** the host is exactly `127.0.0.1`, `::1` or `localhost`
  (tests). Rejected: user info (`user:pw@`, even an empty `@`), empty hosts, port 0, non-ASCII,
  whitespace/control characters, backslashes, URLs over 8 KiB. Error messages never echo the URL
  (query strings may hold keys). Fragments are never sent.
- **Method and headers**: the method must be an RFC 9110 token; header names must be tokens and values
  printable ASCII, so a caller can never inject header lines. `Host`, `Content-Length` and
  `Transfer-Encoding` are managed by WinHTTP and rejected. Header values are never echoed in errors.
- **Timeouts** (`WinHttpSetTimeouts`): resolve and connect = `connectTimeout`, send and receive =
  `receiveTimeout` (also `WINHTTP_OPTION_RECEIVE_RESPONSE_TIMEOUT`). Non-positive values are rejected.
- **Redirects**: 301/302/303/307/308 are followed manually, at most `maxRedirects` times, and only when
  `EvaluateRedirect` accepts the resolved `Location`: a valid request URL (https, or loopback http
  with `allowInsecureLoopback`), never an https → http downgrade, and a host that matches an
  `allowedRedirectHosts` entry exactly (ASCII case-insensitive, no subdomain or suffix matching). An
  empty allow-list disables redirects. 303 switches to GET and drops the body and `Content-Type`;
  307/308 keep method and body. `Authorization`, `Proxy-Authorization` and `Cookie` are dropped when the
  origin (scheme, host, port) changes. `HttpResponse::finalUrl` is the URL that produced the response.
- **Size limits**: a `Content-Length` above `maxResponseBytes` fails with `Integrity` before any body
  byte is read; a chunked or close-delimited body that grows past the limit aborts with `Integrity`
  as soon as the limit is crossed. With decompression the limit applies to decoded bytes (a gzip bomb
  is still bounded); because WinHTTP drops `Content-Length` from bodies it decodes, only this streamed
  limit applies to them.
- **Framing**: a body shorter than its `Content-Length` is a `Network` error, not a short success, and
  so is a body longer than it (`Content-Length` plus chunked framing, RFC 9112 section 6.3). Both checks
  apply to every response whose `Content-Length` counts the bytes read, including undecoded codings
  such as `br`. Only a body WinHTTP decodes is exempt (`IsWinHttpDecodedCoding`), in case a WinHTTP
  build decodes it but keeps a `Content-Length` that counts encoded bytes.
- **Streaming** (`SendStreaming`): the sink receives 64 KiB chunks (only the last one may be shorter);
  `progress(received, total)` runs after every read, with `total` from `Content-Length`, or `nullopt`
  when the response has none (chunked, close-delimited, or a gzip/deflate body WinHTTP decoded). A
  sink error aborts the transfer and is returned unchanged; a throwing sink or progress callback
  becomes `Internal`.

## Threading model

- Every call blocks the calling **worker** thread; never call it from the UI thread (use `WorkerPool`
  and marshal results with `IUiDispatcher::Post`). `Create` does no network I/O.
- One `WinHttpClient` may be used concurrently from any number of threads: the session handle is
  shared and read-only after `Create`; each call owns its connection and request handles. The client
  must outlive every call in flight.
- Cancellation: each exchange registers `CancellationToken::OnCancel`. The callback closes the request
  handle (`WinHttpCloseHandle`) when the worker is inside a WinHTTP call, which makes the blocking call
  return at once; between calls it only marks the exchange cancelled. The call then returns `Cancelled`.
  The registration is destroyed before the handle, and `Registration`'s destructor waits for a callback
  running on another thread, so the callback never touches a freed handle. `CancellationSource::Cancel`
  may be called from the UI thread: the callback only takes a mutex and calls `WinHttpCloseHandle`,
  which returned in under 1 ms in the integration tests (asserted below 500 ms).

## Failure behaviour

| Condition | `ErrorCode` |
| --- | --- |
| Bad URL, scheme, user info, method, header, timeout, missing sink | `InvalidArgument` (nothing sent) |
| `ERROR_WINHTTP_TIMEOUT` | `Timeout` |
| Cancellation (token, or handle closed by it) | `Cancelled` |
| Name resolution, connect, reset, invalid response, proxy failures, body shorter or longer than its `Content-Length` | `Network` |
| TLS / certificate failures | `Network`, message `"...: TLS failure: <reason>"`; the reason comes from the `WINHTTP_CALLBACK_STATUS_SECURE_FAILURE` flags when WinHTTP reports them |
| Redirect rejected by policy | `Validation`, `"redirect to untrusted host '<host>' rejected"` (or https→http / invalid target), `platformCode` = the 3xx status |
| Too many redirects, redirect without usable `Location` | `Http`, `platformCode` = the 3xx status |
| Body over `maxResponseBytes` | `Integrity` |
| Session creation failure | `Platform` |
| Out of memory, unexpected exception | `Internal` |

`platformCode` carries the WinHTTP/Win32 code for transport errors. Exceptions never leave the module.
Logging (`"net"` component): one Info line per session, Debug lines per request (method, origin, status,
duration) and a Warning for rejected redirects. Paths, queries, header values and bodies are never logged.

## Limitations

- **Synchronous cancellation window.** WinHTTP's synchronous mode can be aborted only by closing the
  handle from another thread. Between the worker publishing "in call" and WinHTTP taking its own
  reference on the handle there are a few instructions; a cancel landing exactly there normally makes
  the call fail with `ERROR_INVALID_HANDLE` (reported as `Cancelled`). In theory WinHTTP could reuse
  the closed handle value for another thread's request in that window. Only WinHTTP's asynchronous mode
  removes this; it was not used to keep the client simple and per the module brief.
- **Coarse WinHTTP timeouts.** WinHTTP checks timeouts on a coarse internal timer: on this Windows 11
  machine (build 26300) a 600 ms receive timeout fired after about 3-4 s, and earlier measurements saw
  200-3000 ms requests fire after about 4 s. Timeouts are upper bounds on hangs, not precise deadlines.
- **Connection refused** on loopback takes about 2 s on Windows (the TCP stack retries the SYN), so
  `Network` errors for a closed port are not instantaneous.
- **No certificate revocation checking.** WinHTTP's default is used (`WINHTTP_ENABLE_SSL_REVOCATION`
  is not set) so updates keep working on networks that block CRL/OCSP endpoints; chain, expiry, name
  and usage are still validated by the OS.
- **No authentication flows**: 401/407 responses are returned as statuses; the client does not call
  `WinHttpSetCredentials` (proxy authentication must be handled by the system proxy configuration).
- URLs must be ASCII: callers percent-encode paths/queries and convert IDN host names to Punycode.
- Bodies WinHTTP decodes (gzip/deflate) lose `Content-Length` and `Content-Encoding`, so no total is
  reported and truncation is detected only by WinHTTP's decoder (a cut gzip stream failed with
  `ERROR_WINHTTP_INVALID_SERVER_RESPONSE` in tests). A corrupt deflate stream surfaces as `Network`
  with WinHTTP's `E_ABORT` text ("Operation aborted"), not as `Cancelled`.
- The response sink and progress callbacks run on the calling worker thread, inside the transfer; slow
  sinks slow the download.

## Tests

- `tests/unit/net` (label `unit`): URL validation, redirect host policy and `Location` resolution,
  header validation/parsing, Content-Length parsing, which codings WinHTTP decodes, error mapping,
  session capabilities, rejection of invalid requests before any I/O, pre-cancelled tokens.
- `tests/integration/net` (label `integration`): an in-process Winsock HTTP/1.1 server
  (`LoopbackHttpServer`, random port on 127.0.0.1 and ::1) exercises 200 body and headers, 404 as a
  successful result, 204, POST bodies, IPv6 literals, Content-Length over the limit, chunked and
  close-delimited bodies over the limit, truncated bodies (identity and undecoded `br`), a body longer
  than its `Content-Length` (conflicting framing), progress totals for undecoded codings, redirects to
  allowed (`localhost`) versus
  disallowed hosts, credential stripping across origins, 303/307/308 semantics, redirect loop limit,
  missing `Location`, receive timeouts (headers and body), cancellation while waiting for headers and
  mid-transfer (asserting that `Cancel()` itself returns at once), sink errors and exceptions, 64 KiB
  chunking and progress (Content-Length and chunked), gzip decoding and a gzip bomb, concurrent use
  from 8 threads, 60 concurrent requests cancelled at spread-out moments from other threads (each must
  end as a complete body or `Cancelled`), connection refused, https to a plain-text server.
- `tests/network/net` (label `network`, needs Internet): `GET https://api.github.com/zen` with GitHub
  API headers (200, or 403/429 when rate limited), certificate failures at badssl.com reported as TLS
  failures (skipped per host when unreachable), and an unresolvable `.invalid` host mapped to `Network`.

Build and run: `./scripts/build.ps1 -BuildDir build/wf-net -Modules net -Jobs 2 -Test -TestRegex net`.
The Internet tests are excluded by default; run them with `-TestExclude none -TestLabel network`.
