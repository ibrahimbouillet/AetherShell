<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Ibrahim Bouillet -->
# weather — current conditions from Open-Meteo (`src/weather`, target `aether_weather`)

## Purpose

Optional current weather for the Top Status Bar and the Settings "Weather" page. Data comes from
[Open-Meteo](https://open-meteo.com/) over HTTPS without an API key. Weather is **off by default**;
forecast requests start only after the user enables weather and chooses either a manual place or
Windows location. Automatic location is explicitly requested from Windows in the foreground, remains
subject to the current OS privacy policy, and uses rounded in-memory coordinates. A denied or missing
reading leaves weather unavailable. Existing manual locations and explicit weather opt-outs are
preserved. The separate geocoding client sends only the text the user enters when `Search()` is called.

Dependencies: `aether_core`, `aether_diagnostics`, `aether_platform`. The module includes
`status/StatusTypes.h` for the `WeatherSnapshot` contract but does not link `aether_status`. HTTP goes
through `aether::net::IHttpClient` (`src/core/Http.h`); the application injects the WinHTTP client from
`aether_net`, which this module does not link.

## Public API (namespace `aether::weather`)

| Header | Contents |
| --- | --- |
| `WeatherProvider.h` | `WeatherProvider : status::IStatusProvider<status::WeatherSnapshot>`: `Start`, `Stop`, `Current`, `StartWithReports`, `CurrentReport`, `UpdateConfig`, `RefreshNow`, `NotifyClockChanged`, `WaitUntilIdle`; retry and rate-limit constants |
| `WeatherTypes.h` | `WeatherConfig`, `ValidateConfig`, `WeatherReading` (cache record), `WeatherReport` (snapshot + `glyph` + `isDay`), `LocationCandidate`, `FormatLocationLabel`, `kAttribution` ("Weather data by Open-Meteo.com"), `kAttributionUrl`, `ToString` for states and units |
| `WeatherConditions.h` | `WeatherGlyph` {Clear, PartlyCloudy, Cloudy, Fog, Drizzle, Rain, Snow, Thunder, Unknown}, `ConditionText(code)`, `GlyphForCode(code)`, `IsValidWeatherCode` |
| `GeocodingClient.h` | `GeocodingClient::Search(query, languageTag, cancel) -> Result<std::vector<LocationCandidate>>` (blocking) |
| `OpenMeteo.h` | Pure protocol helpers (namespace `openmeteo`): URL building, percent-encoding, coordinate formatting, `MakeRequest`, `ParseForecast`, `ParseGeocoding`, `ProviderErrorReason`, `ParseRetryAfter` |
| `WeatherCache.h` | Cache file format (namespace `cache`): `Serialize`, `Parse`, `Load`, `Save`, `IsWithinMaxAge` |

Wiring in the application (sketch):

```cpp
weather::WeatherConfig config;                 // mapped from settings::WeatherSettings
config.enabled = settings.weather.enabled;
config.latitude = resolvedLocation.latitude;   // manual settings or the current coarse Windows sample
config.longitude = resolvedLocation.longitude;
config.locationName = settings.weather.locationName;
config.unit = settings.weather.unit;           // status::TemperatureUnit
config.refreshInterval = std::chrono::minutes{settings.weather.refreshMinutes};

weather::WeatherProvider provider(*httpClient, systemTime, paths.cacheDir / L"weather.json", config);
provider.StartWithReports([&](const weather::WeatherReport& report) {
    dispatcher.Post([report, &topBar] { topBar.SetWeather(report); });   // marshal to the UI thread
});
systemEvents.ResumedFromSleep.Connect([&] { provider.NotifyClockChanged(); });
systemEvents.TimeChanged.Connect([&] { provider.NotifyClockChanged(); });
// Settings changed:   provider.UpdateConfig(newConfig);
// "Refresh" clicked:  provider.RefreshNow();   // RateLimited within 60 s of the last attempt
```

`WeatherSnapshot` has no day/night field, so the Top Bar should use `StartWithReports`/`CurrentReport`:
`WeatherReport::glyph` and `WeatherReport::isDay` are set only together with displayed data
(Available or Cached) and are `Unknown`/`nullopt` otherwise. `Start(Listener)` remains for code that
only needs the plain snapshot.

### WMO codes

| Code(s) | Text | Glyph |
| --- | --- | --- |
| 0 / 1 | Clear sky / Mainly clear | Clear |
| 2 | Partly cloudy | PartlyCloudy |
| 3 | Overcast | Cloudy |
| 45, 48 | Fog | Fog |
| 51-57 | Drizzle (incl. freezing) | Drizzle |
| 61-67 | Rain (incl. freezing) | Rain |
| 71-77 | Snow (incl. snow grains) | Snow |
| 80-82 | Rain showers | Rain |
| 85-86 | Snow showers | Snow |
| 95-99 | Thunderstorm (incl. hail) | Thunder |

Other codes in 0..99 are valid WMO codes that Open-Meteo does not document; they get an empty text and
`WeatherGlyph::Unknown` instead of a guessed condition. Codes outside 0..99 are rejected as invalid.

## Open-Meteo protocol (verified 2026-10-18)

Checked against the official [forecast documentation](https://open-meteo.com/en/docs),
[geocoding documentation](https://open-meteo.com/en/docs/geocoding-api),
[licence](https://open-meteo.com/en/licence) and [terms](https://open-meteo.com/en/terms).
No live forecast or geocoding requests were made during the 2026-10-18 verification.

- Forecast: `https://api.open-meteo.com/v1/forecast?latitude=<lat>&longitude=<lon>&current=temperature_2m,weather_code,is_day&temperature_unit=celsius|fahrenheit&timezone=auto`.
  Coordinates use 4 fixed decimals with `std::to_chars` (locale independent, `-0.0000` normalized).
- Geocoding: `https://geocoding-api.open-meteo.com/v1/search?name=<UTF-8 percent-encoded>&count=5&language=<iso639>&format=json`.
  Only RFC 3986 unreserved characters pass unencoded, so `&`, `=`, `#`, spaces and non-ASCII text can
  never alter the query. The BCP-47 tag is reduced to its lower-case primary subtag (`fr-FR` -> `fr`),
  falling back to `en`.
- Every request: `GET`, `Accept: application/json`, connect timeout 10 s, receive timeout 15 s,
  `maxResponseBytes` 256 KiB, redirects disabled.
- Forecast parsing is strict (`aether::json` with a 256 KiB / depth 16 limit): `current` must be an
  object; `current.time` an ISO 8601 local time (`YYYY-MM-DDTHH:MM[:SS]`, a real calendar date);
  `utc_offset_seconds` an integer within ±18 h (the UTC observation time is `time - offset`);
  `temperature_2m` a finite number within -100..65 °C or -148..149 °F; `current_units.temperature_2m`,
  when present, must exactly match `°C` or `°F` for the requested unit; a present `current_units`
  must be an object; `weather_code` an exact integer 0..99;
  `is_day` 0/1 (or a boolean). Duplicate keys, trailing data and wrong types are rejected.
- Errors are HTTP 400 with `{"error":true,"reason":"..."}`; the reason is sanitized, truncated to 200
  bytes and shown in `detail`. A geocoding search without matches omits `results` (empty list).
- Licence: data is CC BY 4.0; every Available or Cached snapshot carries `attribution` =
  "Weather data by Open-Meteo.com" (link target `kAttributionUrl`). The UI must show it next to the data.
- Terms: the free API is for non-commercial use, under 10 000 calls per day (5 000/h, 600/min). This
  automatic scheduler makes one request per refresh interval (default 30 min, at most 3 retries per
  failure). `RefreshNow()` has its own one-minute limit; explicit location/unit changes refetch.

## States

| `WeatherState` | When | Data shown |
| --- | --- | --- |
| Disabled | `enabled == false` | none; no request, no cache access |
| LocationNotConfigured | enabled without both coordinates | none; no request, no cache access |
| Loading | first request pending/in flight, no usable cache | none |
| Available | a request succeeded **in this session** less than `refreshInterval` ago (by both clocks) and nothing failed since | fresh |
| Cached | usable last good data (< 6 h since `fetchedAt`, same location key and unit) while a refresh is pending or after a failed refresh | cached, with its own `observedAt`/`fetchedAt` and a `detail` saying why |
| Offline | transport `Network`/`Timeout` error, no usable cache | none |
| ProviderUnavailable | HTTP 429 or 5xx, no usable cache | none |
| RequestFailed | invalid response, other HTTP status, other transport error, no usable cache | none |

`Unsupported` is never produced. Data read from disk at start-up is only ever Cached; it becomes
Available only after a successful fetch in the running session. Changing the unit never relabels old
values: a cached Celsius reading is not shown when Fahrenheit is configured (and vice versa).

## Threading model

- The provider owns a single-thread `WorkerPool` ("Aether weather", no COM) that runs a scheduler loop.
  All HTTP requests, cache reads and cache writes run there. `Start`, `UpdateConfig`, `RefreshNow`,
  `NotifyClockChanged` and `Current` only take a mutex and return at once, so they are safe on the UI
  thread. `Stop` cancels the in-flight request through its `CancellationSource` and joins the thread.
- Waiting uses a condition variable with a computed deadline (next fetch, Available->Cached
  transition, 6 h expiry), never polling. Each deadline is held on both the steady and the system
  clock: the steady clock is immune to wall-clock changes, the system clock catches up after sleep or
  hibernation. Deadlines are re-evaluated at least every 10 minutes, and immediately on
  `NotifyClockChanged()`.
- Listeners run on the provider thread, never concurrently with themselves, never after `Stop()`
  returned, and only when the composed report changed. A listener may call `Stop()` (the thread is then
  joined by the next `Stop`, `Start` or the destructor) but must not destroy the provider. Exceptions
  thrown by a listener are caught and logged.
- `GeocodingClient::Search` is blocking and stateless; call it from a worker thread with a
  `CancellationToken` (for example cancel the previous search when the user types again). It checks
  cancellation after transport completion and parsing before returning candidates.

## Scheduling, retries and configuration changes

- First fetch immediately on `Start` (or when weather becomes enabled with a location), then every
  `refreshInterval` (10 min..24 h, default 30 min) after the last attempt.
- Retryable failures (transport errors, invalid responses, 429, 5xx, 408) are retried after 30 s,
  2 min and 10 min. A server `Retry-After` (delta-seconds, capped at 1 h) postpones a retry further.
  After the last retry, the provider waits for the next regular refresh, which starts a new bounded
  sequence. Other 4xx responses are not retried before the regular refresh.
- `RefreshNow()` fails with `RateLimited` within 60 s of the last attempt or manual refresh, with
  `InvalidState` when not started, disabled or without a location; otherwise it fetches at once and
  restarts the retry sequence.
- `UpdateConfig()` validates first (`InvalidArgument` keeps the old settings). A change of enabled
  state, coordinates (compared at 4 decimals) or unit cancels the in-flight request and fetches at once;
  a display-name change only republishes; an interval change moves the pending regular refresh (a
  pending retry or immediate fetch is left as is).

## Cache

`<cacheDir>\weather.json`, written with `fs::WriteFileAtomic` after every successful fetch:

```json
{"version":1,"locationKey":"48.8566,2.3522","unit":"celsius","temperature":15.5,
 "weatherCode":2,"isDay":true,"observedAt":1790000000,"fetchedAt":1790000100}
```

Times are Unix seconds (UTC). The file is read once per provider instance, on the provider thread, the
first time weather is enabled with a location (normally at `Start`); afterwards the last good reading
is kept in memory. Any entry that fails validation (version, key, unit, plausible
temperature, code 0..99, boolean `isDay`, positive timestamps, 16 KiB size limit) is ignored as a whole
and logged. Entries older than 6 h, or stamped more than 5 minutes in the future, are never shown. A
reading fetched in the current session is additionally aged by the steady clock, so moving the wall
clock back does not hide it.

## Failure behaviour

| Cause | Without usable cache | With usable cache | Retried |
| --- | --- | --- | --- |
| Transport `Network` / `Timeout` | Offline | Cached | yes |
| HTTP 429 / 5xx | ProviderUnavailable | Cached | yes (honours Retry-After) |
| HTTP 408 | RequestFailed | Cached | yes |
| Other non-200 status | RequestFailed (provider reason in `detail`) | Cached | no |
| Invalid JSON, wrong types, out-of-range values | RequestFailed | Cached | yes |
| Response over 256 KiB (`Integrity`) | RequestFailed | Cached | yes |
| Unexpected exception | RequestFailed (logged) | Cached | yes |
| Cache unreadable/corrupt | ignored, logged | - | - |
| Cache write failure | logged; the fresh data is still shown | - | - |

Public functions return `Result`; no exception leaves the module or a thread. `GeocodingClient` maps
429 to `RateLimited`, 5xx to `Unavailable`, other statuses to `Http` (message includes the provider
reason, `platformCode` the status), and passes transport errors through.

Logging (`weather` component): state transitions, configuration changes and failures, never per
refresh at Info level, never coordinates, search text or provider reasons (which can echo the request).
Exception messages from transports and listeners are omitted because they can contain those values.

## Limitations

- **Unresolved cache-expiry requirement:** the scheduler executes synchronous `IHttpClient::Send`
  on its own worker. It cannot publish expiry transitions while that call is in progress. A Cached
  snapshot published shortly before the six-hour cutoff can remain visible beyond that cutoff until
  the request finishes or is cancelled. Configured transport timeouts limit normal requests; the
  injected transport determines whether those limits are enforced. Independent review identified
  this gap; the unit suite does not establish strict expiry during an in-flight request.
- `WeatherSnapshot` (owned by `status`) has no day/night field; `isDay` and the glyph travel in
  `WeatherReport`. Adding `std::optional<bool> isDay` to the shared snapshot would let consumers use the
  plain `IStatusProvider` contract.
- Condition texts are English only.
- Open-Meteo "current" values are model data on a 15-minute grid, not station observations;
  `observedAt` is the model time step.
- `Retry-After` in HTTP-date form is ignored (the regular backoff applies).
- Sleep handling relies on the system-clock deadlines and `NotifyClockChanged()`; if the application
  does not forward `ResumedFromSleep`/`TimeChanged`, catching up can take up to 10 minutes.
- Disabling weather stops all traffic but does not delete `weather.json` (which holds the rounded
  coordinates of the last location).
- Forecast URLs format coordinates with four decimals; that formatting does not express the accuracy
  of the source. Automatic Windows fixes are rounded to 0.1 degree (roughly a 10 km scale), while
  manual search results are place centres, not the user's position.
- Commercial use of Open-Meteo requires an API key and a different host; not supported.
- No live forecast test: protocol and provider behavior use an in-memory `IHttpClient`; real Windows
  location permission/read, live Open-Meteo transport and end-to-end UI wiring remain unverified.

## Tests

`tests/unit/weather/` (target `aether_weather_tests`, label `unit;weather`), with an in-memory
`FakeHttpClient` (scripted responses, optional blocking until cancelled), `aether::FakeTimeSource` and a
`SkewableTimeSource` whose wall clock can move on its own:

- `WeatherParsingTests.cpp`: WMO mapping, coordinate formatting under a `fr-FR` locale, URL building,
  percent-encoding, language tags, request limits, forecast parsing (valid, malformed, out of range,
  provider errors, oversized), geocoding parsing, Retry-After, cache round-trip/validation/expiry,
  configuration validation, strict optional unit metadata.
- `GeocodingClientTests.cpp`: UTF-8 percent-encoding of non-ASCII and hostile queries, query length
  limits (no request), empty results, HTTP/transport error mapping, cancellation (including transport
  completion races), exceptions.
- `WeatherProviderTests.cpp`: disabled and not-configured send nothing; success (URL, timeouts, cache
  written, attribution); malformed JSON; out-of-range values; HTTP 500/503/429 and Retry-After; 400 with
  reason; timeout -> Offline; failure with a fresh cache -> Cached; stale, foreign-location,
  foreign-unit and corrupt caches ignored; periodic refresh, Available -> Cached -> expiry; bounded
  retries; interval changes (including right after a location change); RefreshNow rate limit; unit
  switch refetches without relabelling; location change cancels the request in flight; wall clock moved
  back; invalid settings and restart; no listener call after Stop; Stop from inside the listener;
  throwing listener, Cached state at expiry while a refresh is blocked, exception log privacy.

Build and run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -BuildDir build/wf-codex-weather-20261018-final -Modules weather -Jobs 2 -CMakeArgs '-DAETHER_WARNINGS_AS_ERRORS=ON' -Test -TestRegex weather
```

Observed 2026-10-18: the existing implementation passed 50 cases in a fresh build before the focused
corrections. A second, previously nonexistent build directory compiled the final source at `/W4 /WX`
with no warnings and passed **54 cases, 0 failures** (1/1 CTest executable). The four added cases cover
strict unit metadata, the geocoding cancellation completion race, interval expiry during a blocked
refresh, and exception log privacy. A later continuation fixes the six-hour expiry gap below. No live forecast/geocoding request, real settings change, application launch, or commit
was performed for this module verification.

## Independent expiry scheduling (2026-10-18 continuation)

GAP-006 is resolved without changing FR-010. A private serial request worker performs synchronous
HTTP while the existing scheduler owns publication, cache expiry and listener delivery. At most one
request is active. Completion is transferred under the provider mutex and consumed by the scheduler;
Stop cancels and joins both lanes before destroying transport state.

`build/wf-weather-scheduler2`, `/W4 /WX`, passed **56 cases**. The added blocked-request regression
advances the fake clock beyond six hours and observes expiry while HTTP is still blocked. A second
case covers disabling during a cancellable request. Independent source review found no actionable
issue; the disabling fake observes cancellation promptly, so it does not prove delayed HTTP-return
ordering by itself. Live forecast/geocoding traffic remains untested.
