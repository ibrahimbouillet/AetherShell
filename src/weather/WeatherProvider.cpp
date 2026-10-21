// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "weather/WeatherProvider.h"

#include "core/Cancellation.h"
#include "core/WorkerPool.h"
#include "diagnostics/Logger.h"
#include "weather/OpenMeteo.h"
#include "weather/WeatherCache.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>

namespace aether::weather {
namespace {

using std::chrono::steady_clock;
using std::chrono::system_clock;
using status::WeatherState;

constexpr std::string_view kLog = "weather";
constexpr std::wstring_view kThreadName = L"Aether weather";
constexpr std::chrono::seconds kMinWait{1};        // floor for a scheduler wait; never spin on a past deadline
constexpr std::chrono::seconds kErrorBackoff{30};  // pause after an unexpected scheduler exception

// What the provider fetches; nullopt when nothing may be fetched (disabled or no location). Two
// configurations with the same identity share requests, cache entries and session results.
std::optional<std::string> FetchIdentity(const WeatherConfig& config) {
    if (!config.enabled || !config.HasLocation()) return std::nullopt;
    return openmeteo::LocationKey(*config.latitude, *config.longitude) + "|" + std::string(ToString(config.unit));
}

// Snapshot shown before the provider thread has evaluated anything.
WeatherReport InitialReport(const WeatherConfig& config) {
    WeatherReport report;
    auto& snapshot = report.snapshot;
    snapshot.unit = config.unit;
    snapshot.locationName = config.locationName;
    if (!config.enabled) {
        snapshot.state = WeatherState::Disabled;
    } else if (!config.HasLocation()) {
        snapshot.state = WeatherState::LocationNotConfigured;
        snapshot.detail = config.locationUnavailableDetail;
    } else {
        snapshot.state = WeatherState::Loading;
    }
    return report;
}

struct Failure {
    WeatherState state = WeatherState::RequestFailed;  // shown when no usable cached data exists
    std::string detail;                                // shown to the user (may include the provider reason)
};

struct FetchOutcome {
    enum class Kind { Success, Failure, Cancelled };
    Kind kind = Kind::Failure;
    WeatherReading reading;  // Success
    Failure failure;         // Failure
    std::string logText;     // Failure; never contains coordinates or provider text
    bool retryable = true;   // Failure: eligible for the kRetryDelays sequence
    std::optional<std::chrono::seconds> retryAfter;
};

struct CompletedFetch {
    WeatherConfig request;
    CancellationSource source;
    FetchOutcome outcome;
};

FetchOutcome MakeFailure(WeatherState state, std::string detail, std::string logText, bool retryable = true) {
    FetchOutcome outcome;
    outcome.kind = FetchOutcome::Kind::Failure;
    outcome.failure = Failure{state, std::move(detail)};
    outcome.logText = std::move(logText);
    outcome.retryable = retryable;
    return outcome;
}

}  // namespace

struct WeatherProvider::Impl {
    Impl(net::IHttpClient& httpClient, const ITimeSource& timeSource, std::filesystem::path cachePath,
         WeatherConfig initialConfig)
        : http(httpClient), time(timeSource), cacheFile(std::move(cachePath)), config(std::move(initialConfig)) {
        applied = config;
        published = InitialReport(config);
    }

    net::IHttpClient& http;
    const ITimeSource& time;
    const std::filesystem::path cacheFile;

    // Serializes Start() and the joining half of Stop(). Never taken by the provider thread.
    std::mutex lifecycleMutex;
    // Id of the thread running Run(); lets Stop()/Start() detect calls made from inside the listener.
    std::atomic<std::thread::id> providerThread{};

    mutable std::mutex mutex;
    std::condition_variable wake;                 // wakes the provider thread
    mutable std::condition_variable idleChanged;  // signalled when the provider thread goes idle

    // --- Requests from API callers -------------------------------------------------------------
    WeatherConfig config;  // latest accepted configuration
    std::uint64_t configVersion = 0;
    bool refreshRequested = false;
    bool wakeRequested = false;
    bool running = false;   // a provider thread exists (possibly finishing after a Stop() from the listener)
    bool stopping = false;  // Stop() requested; no listener call may start
    bool idle = false;
    std::optional<steady_clock::time_point> lastAttempt;
    std::optional<steady_clock::time_point> lastManualRefresh;
    std::optional<CancellationSource> inFlight;  // cancels the current request
    std::optional<CompletedFetch> completed;
    ReportListener listener;
    std::unique_ptr<WorkerPool> pool;
    std::unique_ptr<WorkerPool> requests;
    WeatherReport published;  // what Current() returns

    // --- Scheduler state, owned by the provider thread (also guarded by `mutex`) ----------------
    bool hasApplied = false;
    std::uint64_t appliedVersion = 0;
    WeatherConfig applied;
    bool cacheLoaded = false;
    bool hasPublished = false;
    std::optional<WeatherReading> lastGood;                  // newest validated reading (session or disk)
    std::optional<steady_clock::time_point> sessionSuccess;  // when lastGood was fetched in this session
    std::optional<Failure> lastFailure;                      // cleared by a success or an identity change
    // The next fetch is due when either clock reaches its deadline. The steady deadline is immune to
    // wall-clock changes; the system deadline catches up after sleep or hibernation, which the steady
    // clock and condition-variable timeouts may not count.
    std::optional<steady_clock::time_point> nextFetch;
    std::optional<system_clock::time_point> nextFetchSystem;
    // Set while the pending fetch is the regular refresh (scheduled refreshInterval after this
    // instant); empty while an immediate fetch or a retry is pending. Only a regular refresh moves
    // when the user changes the interval.
    std::optional<steady_clock::time_point> regularAnchor;
    std::size_t retryIndex = 0;

    [[nodiscard]] bool OnProviderThread() const noexcept {
        return providerThread.load() == std::this_thread::get_id();
    }

    [[nodiscard]] bool HasPendingRequestsLocked() const noexcept {
        return wakeRequested || refreshRequested || completed.has_value() || !hasApplied ||
               appliedVersion != configVersion;
    }

    [[nodiscard]] std::chrono::seconds FreshnessLocked() const noexcept {
        return applied.refreshInterval;
    }

    // lastGood belongs to the applied location and unit.
    [[nodiscard]] bool MatchesAppliedLocked() const {
        if (!lastGood || !applied.HasLocation()) return false;
        return lastGood->unit == applied.unit &&
               lastGood->locationKey == openmeteo::LocationKey(*applied.latitude, *applied.longitude);
    }

    // lastGood matches the applied settings and is younger than the maximum cache age. A reading from
    // disk whose timestamp lies in the future cannot be trusted (cache::IsWithinMaxAge). A reading
    // fetched in this session is aged by both clocks instead: its timestamp can only be in the future
    // because the wall clock moved back, which must not hide data that is minutes old.
    [[nodiscard]] bool UsableLocked(steady_clock::time_point now, system_clock::time_point systemNow) const {
        if (!MatchesAppliedLocked()) return false;
        if (sessionSuccess) {
            return now - *sessionSuccess < cache::kMaxCacheAge && systemNow - lastGood->fetchedAt < cache::kMaxCacheAge;
        }
        return cache::IsWithinMaxAge(*lastGood, systemNow);
    }

    // Available: fetched successfully in this session, no failure since, and younger than the refresh
    // interval by both clocks, so data is never presented as fresh after a long sleep.
    [[nodiscard]] bool FreshLocked(steady_clock::time_point now, system_clock::time_point systemNow) const {
        if (!sessionSuccess || lastFailure || !UsableLocked(now, systemNow)) return false;
        return now - *sessionSuccess < FreshnessLocked() && systemNow - lastGood->fetchedAt < FreshnessLocked();
    }

    [[nodiscard]] bool FetchDueLocked(steady_clock::time_point now, system_clock::time_point systemNow) const {
        return (nextFetch && *nextFetch <= now) || (nextFetchSystem && *nextFetchSystem <= systemNow);
    }

    void SetDeadlineLocked(steady_clock::duration delay) {
        delay = std::max<steady_clock::duration>(delay, steady_clock::duration::zero());
        nextFetch = time.SteadyNow() + delay;
        nextFetchSystem = time.SystemNow() + std::chrono::duration_cast<system_clock::duration>(delay);
    }

    // Immediate fetch or retry, `delay` from now.
    void ScheduleFetchLocked(steady_clock::duration delay) {
        regularAnchor.reset();
        SetDeadlineLocked(delay);
    }

    // Regular refresh, refreshInterval from now.
    void ScheduleRegularRefreshLocked() {
        SetDeadlineLocked(applied.refreshInterval);
        regularAnchor = time.SteadyNow();
    }

    void CancelScheduleLocked() noexcept {
        nextFetch.reset();
        nextFetchSystem.reset();
        regularAnchor.reset();
    }

    void Run() noexcept;
    void ApplyConfigLocked(std::unique_lock<std::mutex>& lock);
    void LoadCacheLocked(std::unique_lock<std::mutex>& lock);
    void FetchLocked(std::unique_lock<std::mutex>& lock);
    void CompleteFetchLocked(std::unique_lock<std::mutex>& lock);
    [[nodiscard]] FetchOutcome FetchOnce(const WeatherConfig& request, const CancellationToken& cancel) const;
    [[nodiscard]] WeatherReport ComposeLocked(steady_clock::time_point now, system_clock::time_point systemNow) const;
    void PublishLocked(std::unique_lock<std::mutex>& lock);
    [[nodiscard]] std::optional<steady_clock::time_point> NextDeadlineLocked(
        steady_clock::time_point now, system_clock::time_point systemNow) const;
    void WaitLocked(std::unique_lock<std::mutex>& lock);

    // Stops and joins the provider thread. Caller holds lifecycleMutex and is not the provider thread.
    void JoinThread() noexcept;
};

// ---------------------------------------------------------------------------------------------
// Provider thread
// ---------------------------------------------------------------------------------------------

void WeatherProvider::Impl::Run() noexcept {
    providerThread.store(std::this_thread::get_id());
    std::unique_lock lock(mutex);
    while (!stopping) {
        try {
            wakeRequested = false;
            if (!hasApplied || appliedVersion != configVersion) ApplyConfigLocked(lock);
            if (stopping) break;
            if (completed) CompleteFetchLocked(lock);
            if (stopping) break;
            if (refreshRequested) {
                refreshRequested = false;
                if (FetchIdentity(applied)) {
                    ScheduleFetchLocked(steady_clock::duration::zero());
                    retryIndex = 0;  // a manual refresh starts a new retry sequence
                }
            }
            PublishLocked(lock);
            if (stopping) break;
            if (HasPendingRequestsLocked()) continue;  // arrived while the lock was released
            if (!inFlight && FetchDueLocked(time.SteadyNow(), time.SystemNow())) {
                FetchLocked(lock);
                continue;
            }
            WaitLocked(lock);
        } catch (const std::exception&) {
            if (!lock.owns_lock()) lock.lock();
            AETHER_LOG_ERROR(kLog, "weather scheduler error: exception");
            wake.wait_for(lock, kErrorBackoff, [this] { return stopping; });
        } catch (...) {
            if (!lock.owns_lock()) lock.lock();
            AETHER_LOG_ERROR(kLog, "weather scheduler error: non-standard exception");
            wake.wait_for(lock, kErrorBackoff, [this] { return stopping; });
        }
    }
    idle = false;
    providerThread.store(std::thread::id{});
    idleChanged.notify_all();
}

void WeatherProvider::Impl::ApplyConfigLocked(std::unique_lock<std::mutex>& lock) {
    const bool first = !hasApplied;
    const auto previousIdentity = first ? std::optional<std::string>{} : FetchIdentity(applied);
    const auto previousInterval = applied.refreshInterval;
    applied = config;
    appliedVersion = configVersion;
    hasApplied = true;

    const auto identity = FetchIdentity(applied);
    if (first || identity != previousIdentity) {
        // New location, unit or enabled state: results of the old identity no longer count as fresh.
        lastFailure.reset();
        sessionSuccess.reset();
        CancelScheduleLocked();
        retryIndex = 0;
        if (!applied.enabled) {
            AETHER_LOG_INFO(kLog, "weather disabled");
            return;
        }
        if (!identity) {
            AETHER_LOG_INFO(kLog, "weather enabled without a location");
            return;
        }
        // Coordinates are personal data and are not logged.
        AETHER_LOG_INFO(kLog, "weather enabled (unit {}, refresh every {} min)", ToString(applied.unit),
                        applied.refreshInterval.count());
        if (!cacheLoaded) LoadCacheLocked(lock);
        ScheduleFetchLocked(steady_clock::duration::zero());
        return;
    }

    if (identity && applied.refreshInterval != previousInterval && regularAnchor) {
        // Only the regular schedule moves; a pending retry keeps its backoff delay and a pending
        // immediate fetch (for example right after a location change) stays immediate.
        SetDeadlineLocked(*regularAnchor + applied.refreshInterval - time.SteadyNow());
    }
}

void WeatherProvider::Impl::LoadCacheLocked(std::unique_lock<std::mutex>& lock) {
    cacheLoaded = true;
    lock.unlock();
    auto loaded = cache::Load(cacheFile);
    lock.lock();
    if (!loaded) {
        const auto& error = loaded.error();
        if (error.code == ErrorCode::Parse || error.code == ErrorCode::Validation) {
            AETHER_LOG_WARN(kLog, "ignoring weather cache: {}", error.message);
        } else {
            AETHER_LOG_WARN(kLog, "weather cache could not be read ({})", ToString(error.code));
        }
        return;
    }
    if (const auto& reading = loaded.value(); reading && (!lastGood || reading->fetchedAt > lastGood->fetchedAt)) {
        lastGood = *reading;
    }
}

void WeatherProvider::Impl::FetchLocked(std::unique_lock<std::mutex>& lock) {
    (void)lock;  // caller holds the scheduler state mutex; HTTP uses a separate worker
    const WeatherConfig request = applied;
    const auto identity = FetchIdentity(request);
    if (!identity) {
        CancelScheduleLocked();
        return;
    }

    CancellationSource source;
    inFlight = source;  // copies share the cancellation state
    lastAttempt = time.SteadyNow();
    CancelScheduleLocked();
    if (!requests->Post([this, request, source] {
            auto outcome = FetchOnce(request, source.Token());
            {
                std::lock_guard state(mutex);
                completed = CompletedFetch{request, source, std::move(outcome)};
            }
            wake.notify_all();
        })) {
        completed = CompletedFetch{request, source,
                                  MakeFailure(WeatherState::RequestFailed, "Weather worker unavailable.",
                                              "request worker unavailable")};
    }
}

void WeatherProvider::Impl::CompleteFetchLocked(std::unique_lock<std::mutex>& lock) {
    CompletedFetch result = std::move(*completed);
    completed.reset();
    inFlight.reset();
    auto& outcome = result.outcome;

    if (stopping) return;
    const auto now = time.SteadyNow();
    if (result.source.IsCancelled() || FetchIdentity(config) != FetchIdentity(result.request)) {
        // Superseded by UpdateConfig(); the next iteration applies the new configuration.
        ScheduleFetchLocked(steady_clock::duration::zero());
        return;
    }
    if (outcome.kind == FetchOutcome::Kind::Cancelled) {
        // Nobody cancelled this request, so the transport reported a cancellation on its own.
        outcome = MakeFailure(WeatherState::RequestFailed, "The weather request was interrupted.",
                              "transport reported an unexpected cancellation");
    }

    if (outcome.kind == FetchOutcome::Kind::Success) {
        lastGood = outcome.reading;
        sessionSuccess = now;
        lastFailure.reset();
        retryIndex = 0;
        ScheduleRegularRefreshLocked();
        const WeatherReading reading = outcome.reading;
        lock.unlock();
        auto saved = cache::Save(cacheFile, reading);
        lock.lock();
        if (!saved) AETHER_LOG_WARN(kLog, "weather cache could not be written ({})", ToString(saved.error().code));
        AETHER_LOG_DEBUG(kLog, "weather updated (code {})", reading.weatherCode);
        return;
    }

    lastFailure = outcome.failure;
    if (outcome.retryable && retryIndex < kRetryDelays.size()) {
        steady_clock::duration delay = kRetryDelays[retryIndex++];
        if (outcome.retryAfter) {
            const steady_clock::duration retryAfter =
                std::min<steady_clock::duration>(*outcome.retryAfter, kMaxRetryAfter);
            delay = std::max(delay, retryAfter);
        }
        ScheduleFetchLocked(delay);
        AETHER_LOG_WARN(kLog, "weather refresh failed: {}; retry {} of {} in {} s", outcome.logText, retryIndex,
                        kRetryDelays.size(), std::chrono::duration_cast<std::chrono::seconds>(delay).count());
    } else {
        retryIndex = 0;
        ScheduleRegularRefreshLocked();
        AETHER_LOG_WARN(kLog, "weather refresh failed: {}; next attempt at the scheduled refresh", outcome.logText);
    }
}

FetchOutcome WeatherProvider::Impl::FetchOnce(const WeatherConfig& request, const CancellationToken& cancel) const {
    try {
        const auto httpRequest = openmeteo::MakeRequest(
            openmeteo::BuildForecastUrl(*request.latitude, *request.longitude, request.unit));
        auto response = http.Send(httpRequest, cancel);
        if (!response) {
            const auto code = response.error().code;
            const std::string codeName(ToString(code));
            switch (code) {
                case ErrorCode::Cancelled: {
                    FetchOutcome outcome;
                    outcome.kind = FetchOutcome::Kind::Cancelled;
                    return outcome;
                }
                case ErrorCode::Network:
                case ErrorCode::Timeout:
                    return MakeFailure(WeatherState::Offline, "Open-Meteo could not be reached (" + codeName + ").",
                                       "transport error " + codeName);
                case ErrorCode::Integrity:
                    return MakeFailure(WeatherState::RequestFailed, "The weather response was too large.",
                                       "response exceeded the size limit");
                default:
                    return MakeFailure(WeatherState::RequestFailed, "The weather request failed (" + codeName + ").",
                                       "transport error " + codeName);
            }
        }

        const auto& reply = response.value();
        const std::string statusText = std::to_string(reply.status);
        if (reply.status == 429 || (reply.status >= 500 && reply.status <= 599)) {
            auto outcome = MakeFailure(WeatherState::ProviderUnavailable,
                                       reply.status == 429 ? "Open-Meteo rate limit reached (HTTP 429)."
                                                           : "Open-Meteo is unavailable (HTTP " + statusText + ").",
                                       "HTTP " + statusText);
            outcome.retryAfter = openmeteo::ParseRetryAfter(reply);
            return outcome;
        }
        if (reply.status != 200) {
            std::string detail = "Open-Meteo rejected the request (HTTP " + statusText + ")";
            if (const auto reason = openmeteo::ProviderErrorReason(reply.body)) detail += ": " + *reason;
            detail += ".";
            // A rejected request does not improve by repeating it; 408 is the exception.
            return MakeFailure(WeatherState::RequestFailed, std::move(detail), "HTTP " + statusText,
                               reply.status == 408);
        }

        auto conditions = openmeteo::ParseForecast(reply.body, request.unit);
        if (!conditions) {
            const auto& error = conditions.error();
            return MakeFailure(WeatherState::RequestFailed, "Open-Meteo sent an invalid response: " + error.message,
                               "invalid response (" + std::string(ToString(error.code)) + ")");
        }

        FetchOutcome outcome;
        outcome.kind = FetchOutcome::Kind::Success;
        auto& reading = outcome.reading;
        reading.locationKey = openmeteo::LocationKey(*request.latitude, *request.longitude);
        reading.unit = request.unit;
        reading.temperature = conditions.value().temperature;
        reading.weatherCode = conditions.value().weatherCode;
        reading.isDay = conditions.value().isDay;
        reading.observedAt = conditions.value().observedAt;
        // Whole seconds, so the in-memory reading equals what the cache file stores.
        reading.fetchedAt = std::chrono::floor<std::chrono::seconds>(time.SystemNow());
        return outcome;
    } catch (const std::exception&) {
        AETHER_LOG_ERROR(kLog, "weather request threw an exception");
    } catch (...) {
        AETHER_LOG_ERROR(kLog, "weather request threw a non-standard exception");
    }
    return MakeFailure(WeatherState::RequestFailed, "The weather request failed unexpectedly.", "internal error");
}

WeatherReport WeatherProvider::Impl::ComposeLocked(steady_clock::time_point now,
                                                   system_clock::time_point systemNow) const {
    WeatherReport report = InitialReport(applied);
    if (report.snapshot.state != WeatherState::Loading) return report;  // Disabled / LocationNotConfigured

    auto& snapshot = report.snapshot;
    if (UsableLocked(now, systemNow)) {
        const auto& reading = *lastGood;
        snapshot.temperature = reading.temperature;
        snapshot.conditionCode = reading.weatherCode;
        snapshot.conditionText = std::wstring(ConditionText(reading.weatherCode));
        snapshot.observedAt = reading.observedAt;
        snapshot.fetchedAt = reading.fetchedAt;
        snapshot.attribution = std::wstring(kAttribution);
        report.glyph = GlyphForCode(reading.weatherCode);
        report.isDay = reading.isDay;
        if (FreshLocked(now, systemNow)) {
            snapshot.state = WeatherState::Available;
        } else {
            snapshot.state = WeatherState::Cached;
            snapshot.detail = lastFailure ? lastFailure->detail : "Showing saved weather while refreshing.";
        }
        return report;
    }
    if (lastFailure) {
        snapshot.state = lastFailure->state;
        snapshot.detail = lastFailure->detail;
    }
    return report;  // otherwise Loading: first request pending or in flight
}

void WeatherProvider::Impl::PublishLocked(std::unique_lock<std::mutex>& lock) {
    if (stopping) return;
    WeatherReport report = ComposeLocked(time.SteadyNow(), time.SystemNow());
    if (hasPublished && report == published) return;
    if (!hasPublished || published.snapshot.state != report.snapshot.state) {
        AETHER_LOG_INFO(kLog, "weather state: {}", ToString(report.snapshot.state));
    }
    published = report;
    hasPublished = true;
    const ReportListener callback = listener;
    lock.unlock();
    try {
        if (callback) callback(report);
    } catch (const std::exception&) {
        AETHER_LOG_ERROR(kLog, "weather listener threw an exception");
    } catch (...) {
        AETHER_LOG_ERROR(kLog, "weather listener threw a non-standard exception");
    }
    lock.lock();
}

std::optional<steady_clock::time_point> WeatherProvider::Impl::NextDeadlineLocked(
    steady_clock::time_point now, system_clock::time_point systemNow) const {
    std::optional<steady_clock::time_point> deadline;
    const auto consider = [&](steady_clock::time_point candidate) {
        if (!deadline || candidate < *deadline) deadline = candidate;
    };
    const auto considerSystem = [&](system_clock::time_point candidate) {
        consider(now + std::chrono::duration_cast<steady_clock::duration>(candidate - systemNow));
    };
    if (!inFlight) {
        if (nextFetch) consider(*nextFetch);
        if (nextFetchSystem) considerSystem(*nextFetchSystem);
    }
    if (applied.enabled && UsableLocked(now, systemNow)) {
        // Available turns into Cached when the refresh is overdue...
        if (FreshLocked(now, systemNow)) {
            consider(*sessionSuccess + FreshnessLocked());
            considerSystem(lastGood->fetchedAt + FreshnessLocked());
        }
        // ...and cached data disappears once it is older than the maximum age.
        considerSystem(lastGood->fetchedAt + cache::kMaxCacheAge);
        if (sessionSuccess) consider(*sessionSuccess + cache::kMaxCacheAge);
    }
    return deadline;
}

void WeatherProvider::Impl::WaitLocked(std::unique_lock<std::mutex>& lock) {
    const auto now = time.SteadyNow();
    steady_clock::duration wait = kMaxIdleWait;
    if (const auto deadline = NextDeadlineLocked(now, time.SystemNow())) {
        wait = std::clamp<steady_clock::duration>(*deadline - now, kMinWait, kMaxIdleWait);
    }
    idle = true;
    idleChanged.notify_all();
    wake.wait_for(lock, wait, [this] { return stopping || HasPendingRequestsLocked(); });
    idle = false;
}

void WeatherProvider::Impl::JoinThread() noexcept {
    try {
        std::unique_ptr<WorkerPool> joined;
        std::unique_ptr<WorkerPool> joinedRequests;
        std::optional<CancellationSource> request;
        {
            std::lock_guard lock(mutex);
            if (!running) return;
            stopping = true;
            request = inFlight;
            joined = std::move(pool);
            joinedRequests = std::move(requests);
        }
        wake.notify_all();
        if (request) request->Cancel();  // outside the lock: the transport's callback runs synchronously
        if (joined) joined->Shutdown();  // joins the provider thread; no listener call after this
        if (joinedRequests) joinedRequests->Shutdown();  // transport cannot outlive its state
        joined.reset();
        joinedRequests.reset();

        ReportListener released;
        {
            std::lock_guard lock(mutex);
            released = std::move(listener);
            listener = nullptr;
            running = false;
            stopping = false;
            idle = false;
            hasApplied = false;
            hasPublished = false;
            inFlight.reset();
            completed.reset();
        }
        idleChanged.notify_all();
        // `released` (and whatever it captured) is destroyed here, outside the lock.
    } catch (...) {
        // Only std::mutex failures (resource exhaustion) can get here; Stop() must not throw.
    }
}

// ---------------------------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------------------------

WeatherProvider::WeatherProvider(net::IHttpClient& http, const ITimeSource& time, std::filesystem::path cacheFile,
                                 WeatherConfig config)
    : impl_(std::make_unique<Impl>(http, time, std::move(cacheFile), std::move(config))) {}

WeatherProvider::~WeatherProvider() { Stop(); }

Result<void> WeatherProvider::Start(Listener listener) {
    if (!listener) return MakeError(ErrorCode::InvalidArgument, "weather listener is empty");
    try {
        return StartWithReports([callback = std::move(listener)](const WeatherReport& report) {
            callback(report.snapshot);
        });
    } catch (...) {
        return MakeError(ErrorCode::Internal, "weather provider could not start");
    }
}

Result<void> WeatherProvider::StartWithReports(ReportListener listener) {
    if (!listener) return MakeError(ErrorCode::InvalidArgument, "weather listener is empty");
    if (impl_->OnProviderThread()) {
        return MakeError(ErrorCode::InvalidState, "weather provider cannot be started from its own listener");
    }
    try {
        std::lock_guard lifecycle(impl_->lifecycleMutex);
        {
            std::lock_guard lock(impl_->mutex);
            if (impl_->running && !impl_->stopping) {
                return MakeError(ErrorCode::InvalidState, "weather provider is already started");
            }
            if (auto valid = ValidateConfig(impl_->config); !valid) return valid.error();
        }
        // Completes a Stop() that was requested from inside the listener (the thread is exiting).
        impl_->JoinThread();

        std::lock_guard lock(impl_->mutex);
        impl_->listener = std::move(listener);
        impl_->stopping = false;
        impl_->idle = false;
        impl_->hasApplied = false;
        impl_->hasPublished = false;
        impl_->requests = std::make_unique<WorkerPool>(
            WorkerPool::Options{1, L"Aether weather HTTP", ComApartment::None});
        impl_->pool = std::make_unique<WorkerPool>(WorkerPool::Options{1, std::wstring(kThreadName), ComApartment::None});
        if (!impl_->pool->Post([impl = impl_.get()] { impl->Run(); })) {
            impl_->pool.reset();
            impl_->requests.reset();
            impl_->listener = nullptr;
            return MakeError(ErrorCode::Internal, "weather provider thread could not be started");
        }
        impl_->running = true;
        return Ok();
    } catch (const std::exception&) {
        AETHER_LOG_ERROR(kLog, "weather provider failed to start: exception");
    } catch (...) {
        AETHER_LOG_ERROR(kLog, "weather provider failed to start: non-standard exception");
    }
    return MakeError(ErrorCode::Internal, "weather provider could not start");
}

void WeatherProvider::Stop() noexcept {
    try {
        if (impl_->OnProviderThread()) {
            // Called from inside the listener: the thread cannot join itself. Mark the provider as
            // stopping so no further listener call starts; the provider loop exits as soon as the
            // listener returns, and the next Stop(), Start() or the destructor joins the thread.
            std::optional<CancellationSource> request;
            {
                std::lock_guard lock(impl_->mutex);
                if (!impl_->running) return;
                impl_->stopping = true;
                request = impl_->inFlight;
            }
            impl_->wake.notify_all();
            if (request) request->Cancel();
            return;
        }
        std::lock_guard lifecycle(impl_->lifecycleMutex);
        impl_->JoinThread();
    } catch (...) {
        // Stop must not throw; only std::mutex failures (resource exhaustion) can get here.
    }
}

status::WeatherSnapshot WeatherProvider::Current() const {
    std::lock_guard lock(impl_->mutex);
    return impl_->published.snapshot;
}

WeatherReport WeatherProvider::CurrentReport() const {
    std::lock_guard lock(impl_->mutex);
    return impl_->published;
}

Result<void> WeatherProvider::UpdateConfig(WeatherConfig config) {
    try {
        if (auto valid = ValidateConfig(config); !valid) return valid.error();
        std::optional<CancellationSource> superseded;
        {
            std::lock_guard lock(impl_->mutex);
            if (impl_->inFlight && FetchIdentity(config) != FetchIdentity(impl_->config)) {
                superseded = impl_->inFlight;
            }
            impl_->config = std::move(config);
            ++impl_->configVersion;
            if (!impl_->running) {
                impl_->applied = impl_->config;
                impl_->published = InitialReport(impl_->config);
            }
        }
        impl_->wake.notify_all();
        if (superseded) superseded->Cancel();  // outside the lock: the transport's callback runs synchronously
        return Ok();
    } catch (const std::exception&) {
        AETHER_LOG_ERROR(kLog, "weather configuration update failed: exception");
    } catch (...) {
        AETHER_LOG_ERROR(kLog, "weather configuration update failed: non-standard exception");
    }
    return MakeError(ErrorCode::Internal, "weather configuration could not be applied");
}

Result<void> WeatherProvider::RefreshNow() {
    try {
        std::lock_guard lock(impl_->mutex);
        if (!impl_->running || impl_->stopping) {
            return MakeError(ErrorCode::InvalidState, "weather provider is not running");
        }
        if (!impl_->config.enabled) return MakeError(ErrorCode::InvalidState, "weather is disabled");
        if (!impl_->config.HasLocation()) {
            return MakeError(ErrorCode::InvalidState, "weather location is not configured");
        }
        const auto now = impl_->time.SteadyNow();
        const auto recent = [&](const std::optional<steady_clock::time_point>& when) {
            return when && now - *when < kManualRefreshMinInterval;
        };
        if (impl_->inFlight || recent(impl_->lastAttempt) || recent(impl_->lastManualRefresh)) {
            return MakeError(ErrorCode::RateLimited, "weather was refreshed less than a minute ago");
        }
        impl_->lastManualRefresh = now;
        impl_->refreshRequested = true;
        impl_->wake.notify_all();
        return Ok();
    } catch (...) {
        return MakeError(ErrorCode::Internal, "weather refresh could not be requested");
    }
}

void WeatherProvider::NotifyClockChanged() noexcept {
    try {
        std::lock_guard lock(impl_->mutex);
        if (!impl_->running) return;
        impl_->wakeRequested = true;
        impl_->wake.notify_all();
    } catch (...) {
        // Only std::mutex failures (resource exhaustion) can get here; the periodic re-evaluation
        // (kMaxIdleWait) still catches up.
    }
}

bool WeatherProvider::WaitUntilIdle(std::chrono::milliseconds timeout) const noexcept {
    try {
        std::unique_lock lock(impl_->mutex);
        return impl_->idleChanged.wait_for(lock, timeout, [this] {
            return !impl_->running || (impl_->idle && !impl_->stopping && !impl_->inFlight &&
                                      !impl_->HasPendingRequestsLocked());
        });
    } catch (...) {
        return false;
    }
}

}  // namespace aether::weather
