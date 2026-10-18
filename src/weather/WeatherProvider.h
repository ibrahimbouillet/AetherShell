// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Current weather for the Top Status Bar, from Open-Meteo (no API key, HTTPS).
//
// Lifecycle and threading:
//  * One WorkerPool thread ("Aether weather") schedules publication/cache work, while a second
//    serial worker performs HTTP. Expiry publication therefore continues during a slow request.
//    Network/cache work stays off the UI thread; Start(), Stop(),
//    UpdateConfig() and RefreshNow() only exchange state under a mutex and return immediately
//    (Stop() additionally joins the thread after cancelling the in-flight request).
//  * Refreshes are timed with a condition-variable deadline (no polling). Each deadline is kept on
//    both the steady and the system clock: the steady one is immune to wall-clock changes, the
//    system one catches up after sleep/hibernation (which steady waits may not count). Deadlines are
//    re-evaluated at least every kMaxIdleWait; NotifyClockChanged() re-evaluates immediately.
//  * Listeners run on the provider thread, never concurrently with themselves and never after
//    Stop() has returned. A listener may call Stop() (the thread is then joined by the next Stop(),
//    Start() or the destructor) but must never destroy the provider.
//
// States (status::WeatherState):
//  * Disabled - config.enabled is false. No request, no cache access.
//  * LocationNotConfigured - enabled without coordinates. No request, no cache access.
//  * Loading - first request in flight and no usable cache.
//  * Available - a request succeeded in this session less than refreshInterval ago by both clocks,
//    and no refresh has failed since.
//  * Cached - last good data (session or disk cache, < 6 h since it was fetched, same location and
//    unit) shown while a refresh is pending or after any failed refresh; `detail` says why. A disk
//    entry stamped in the future is ignored; a reading fetched in this session is also aged by the
//    steady clock, so moving the wall clock back does not hide it.
//  * Offline / ProviderUnavailable / RequestFailed - the last attempt failed (network or timeout /
//    HTTP 429 or 5xx / anything else) and there is no usable cached data.
#pragma once

#include "core/Http.h"
#include "core/Result.h"
#include "core/Time.h"
#include "status/StatusTypes.h"
#include "weather/WeatherTypes.h"

#include <array>
#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>

namespace aether::weather {

// Delays before the retries that follow a failed attempt. After the last retry fails the provider
// waits for the next scheduled refresh (refreshInterval after that attempt).
inline constexpr std::array<std::chrono::seconds, 3> kRetryDelays{std::chrono::seconds{30}, std::chrono::minutes{2},
                                                                  std::chrono::minutes{10}};
// Minimum spacing between RefreshNow() calls and between any attempt and a manual refresh.
inline constexpr std::chrono::seconds kManualRefreshMinInterval{60};
// Upper bound on a single scheduler wait (see the threading notes above).
inline constexpr std::chrono::minutes kMaxIdleWait{10};
// Server-provided Retry-After values are honoured up to this bound.
inline constexpr std::chrono::hours kMaxRetryAfter{1};

class WeatherProvider final : public status::IStatusProvider<status::WeatherSnapshot> {
public:
    using ReportListener = std::function<void(const WeatherReport&)>;

    // `http` and `time` must outlive the provider. `cacheFile` is typically
    // AppPaths::cacheDir / "weather.json". The configuration is validated by Start().
    WeatherProvider(net::IHttpClient& http, const ITimeSource& time, std::filesystem::path cacheFile,
                    WeatherConfig config);
    ~WeatherProvider() override;  // Calls Stop().

    WeatherProvider(const WeatherProvider&) = delete;
    WeatherProvider& operator=(const WeatherProvider&) = delete;

    // IStatusProvider. Start fails with InvalidState when already started, InvalidArgument for an
    // empty listener or an invalid configuration (see ValidateConfig).
    Result<void> Start(Listener listener) override;
    void Stop() noexcept override;
    [[nodiscard]] status::WeatherSnapshot Current() const override;

    // Like Start(), but the listener also receives the glyph category and day/night flag the Top
    // Bar renderer needs (WeatherSnapshot has no field for them).
    Result<void> StartWithReports(ReportListener listener);
    [[nodiscard]] WeatherReport CurrentReport() const;

    // Applies new settings. A change of enabled state, coordinates (compared at the request
    // precision) or unit cancels the in-flight request and fetches again immediately; a name or
    // interval change only republishes / reschedules. Invalid settings are rejected
    // (InvalidArgument) and the previous ones stay active. May be called before Start or after Stop.
    Result<void> UpdateConfig(WeatherConfig config);

    // Requests an immediate refresh. Fails with InvalidState when not started, disabled or without
    // location, and with RateLimited when the last attempt or manual refresh was less than
    // kManualRefreshMinInterval ago.
    Result<void> RefreshNow();

    // Re-evaluates refresh and expiry deadlines now. Call after resume from sleep or a system time
    // change (platform::SystemEvents::ResumedFromSleep / TimeChanged). Tests call it after
    // advancing a FakeTimeSource.
    void NotifyClockChanged() noexcept;

    // Blocks until the provider thread has processed every pending request and is waiting for its
    // next deadline (or the provider is not running). Returns false on timeout. Intended for tests
    // and diagnostics; never call it on the UI thread with a long timeout, nor from the listener.
    [[nodiscard]] bool WaitUntilIdle(std::chrono::milliseconds timeout) const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace aether::weather
