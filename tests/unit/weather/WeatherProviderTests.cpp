// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// WeatherProvider state machine, scheduling, retries, cache use and lifecycle, driven by an
// in-memory HTTP client and a manually advanced clock. The provider runs its real thread; tests
// synchronize with WaitUntilIdle() after every step.
#include "AetherTest.h"
#include "WeatherTestSupport.h"

#include "core/FileSystem.h"
#include "core/Time.h"
#include "diagnostics/Logger.h"
#include "weather/GeocodingClient.h"
#include "weather/WeatherCache.h"
#include "weather/WeatherProvider.h"

#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>

using namespace aether;
using namespace aether::weather;
using namespace aether::weather::testing;
using aether::status::TemperatureUnit;
using aether::status::WeatherState;
using namespace std::chrono_literals;

namespace {

constexpr std::chrono::milliseconds kWait = 5s;

constexpr std::string_view kParisCelsiusUrl =
    "https://api.open-meteo.com/v1/forecast?latitude=48.8566&longitude=2.3522"
    "&current=temperature_2m,weather_code,is_day&temperature_unit=celsius&timezone=auto";

std::filesystem::path NewCacheFile() { return aether::test::MakeTempDirectory("weather-provider") / "weather.json"; }

void Settle(const WeatherProvider& provider) { REQUIRE(provider.WaitUntilIdle(kWait)); }

// Moves the fake clock and lets the provider re-evaluate its deadlines.
void Advance(WeatherProvider& provider, FakeTimeSource& time, std::chrono::nanoseconds delta) {
    time.Advance(delta);
    provider.NotifyClockChanged();
    Settle(provider);
}

ErrorCode ErrorOf(const Result<void>& result) { return result.ok() ? ErrorCode::Unknown : result.error().code; }

WeatherReading CachedParis(const FakeTimeSource& time, std::chrono::seconds age,
                           TemperatureUnit unit = TemperatureUnit::Celsius) {
    WeatherReading reading;
    reading.locationKey = "48.8566,2.3522";
    reading.unit = unit;
    reading.temperature = unit == TemperatureUnit::Celsius ? 12.5 : 54.5;
    reading.weatherCode = 61;
    reading.isDay = false;
    reading.fetchedAt = std::chrono::floor<std::chrono::seconds>(time.SystemNow()) - age;
    reading.observedAt = reading.fetchedAt - 15min;
    return reading;
}

bool AnyAvailable(const std::vector<WeatherReport>& reports) {
    for (const auto& report : reports) {
        if (report.snapshot.state == WeatherState::Available) return true;
    }
    return false;
}

bool AnyTemperature(const std::vector<WeatherReport>& reports) {
    for (const auto& report : reports) {
        if (report.snapshot.temperature) return true;
    }
    return false;
}

}  // namespace

// ---------------------------------------------------------------------------------------------
// Configuration states
// ---------------------------------------------------------------------------------------------

AETHER_TEST("weather provider: disabled sends no requests and touches no cache") {
    FakeHttpClient http;
    http.RespondWith(200, ForecastBody(15.3, 2, 1));
    FakeTimeSource time;
    ReportRecorder recorder;
    const auto cacheFile = NewCacheFile();
    auto disabled = ParisConfig();
    disabled.enabled = false;

    WeatherProvider provider(http, time, cacheFile, disabled);
    REQUIRE(provider.StartWithReports(recorder.Listener()).ok());
    Settle(provider);
    CHECK_EQ(provider.Current().state, WeatherState::Disabled);
    CHECK_EQ(ErrorOf(provider.RefreshNow()), ErrorCode::InvalidState);
    Advance(provider, time, 2h);
    CHECK_EQ(http.RequestCount(), std::size_t{0});
    CHECK_EQ(recorder.Count(), std::size_t{1});
    CHECK_FALSE(std::filesystem::exists(cacheFile));

    // Enabling fetches; disabling again drops the data and stops all traffic.
    REQUIRE(provider.UpdateConfig(ParisConfig()).ok());
    Settle(provider);
    CHECK_EQ(http.RequestCount(), std::size_t{1});
    CHECK_EQ(provider.Current().state, WeatherState::Available);

    REQUIRE(provider.UpdateConfig(disabled).ok());
    Settle(provider);
    const auto snapshot = provider.Current();
    CHECK_EQ(snapshot.state, WeatherState::Disabled);
    CHECK_FALSE(snapshot.temperature.has_value());
    CHECK(snapshot.attribution.empty());
    Advance(provider, time, 3h);
    CHECK_EQ(http.RequestCount(), std::size_t{1});
}

AETHER_TEST("weather provider: enabled without coordinates is LocationNotConfigured") {
    FakeHttpClient http;
    http.RespondWith(200, ForecastBody(15.3, 2, 1));
    FakeTimeSource time;
    ReportRecorder recorder;
    WeatherConfig config;
    config.enabled = true;

    WeatherProvider provider(http, time, NewCacheFile(), config);
    REQUIRE(provider.StartWithReports(recorder.Listener()).ok());
    Settle(provider);
    CHECK_EQ(provider.Current().state, WeatherState::LocationNotConfigured);
    CHECK_EQ(ErrorOf(provider.RefreshNow()), ErrorCode::InvalidState);
    Advance(provider, time, 1h);
    CHECK_EQ(http.RequestCount(), std::size_t{0});
    CHECK_EQ(recorder.Count(), std::size_t{1});
}

// ---------------------------------------------------------------------------------------------
// Fetch outcomes
// ---------------------------------------------------------------------------------------------

AETHER_TEST("weather provider: a successful fetch is Available with attribution and is cached") {
    FakeHttpClient http;
    http.RespondWith(200, ForecastBody(15.3, 2, 1));
    FakeTimeSource time;
    ReportRecorder recorder;
    const auto cacheFile = NewCacheFile();

    WeatherProvider provider(http, time, cacheFile, ParisConfig());
    REQUIRE(provider.StartWithReports(recorder.Listener()).ok());
    Settle(provider);

    const auto requests = http.Requests();
    REQUIRE_EQ(requests.size(), std::size_t{1});
    CHECK_EQ(requests[0].url, std::string(kParisCelsiusUrl));
    CHECK_EQ(requests[0].method, std::string("GET"));
    CHECK(requests[0].connectTimeout == 10s);
    CHECK(requests[0].receiveTimeout == 15s);
    CHECK_EQ(requests[0].maxResponseBytes, static_cast<std::uint64_t>(256 * 1024));

    const auto reports = recorder.Reports();
    REQUIRE_EQ(reports.size(), std::size_t{2});
    CHECK_EQ(reports[0].snapshot.state, WeatherState::Loading);
    CHECK_FALSE(reports[0].snapshot.temperature.has_value());

    const auto& report = reports[1];
    const auto& snapshot = report.snapshot;
    CHECK_EQ(snapshot.state, WeatherState::Available);
    REQUIRE(snapshot.temperature.has_value());
    CHECK_NEAR(*snapshot.temperature, 15.3, 1e-9);
    CHECK_EQ(snapshot.unit, TemperatureUnit::Celsius);
    CHECK(snapshot.conditionCode == std::optional<int>(2));
    CHECK_EQ(snapshot.conditionText, std::wstring(L"Partly cloudy"));
    CHECK_EQ(snapshot.locationName, std::wstring(L"Paris, France"));
    CHECK_EQ(snapshot.attribution, std::wstring(L"Weather data by Open-Meteo.com"));
    CHECK(snapshot.observedAt == std::optional(ForecastObservedAt()));
    CHECK(snapshot.fetchedAt == std::optional(time.SystemNow()));
    CHECK(snapshot.detail.empty());
    CHECK_EQ(report.glyph, WeatherGlyph::PartlyCloudy);
    CHECK(report.isDay == std::optional<bool>(true));
    CHECK_EQ(provider.Current().state, WeatherState::Available);
    CHECK(provider.CurrentReport() == report);

    auto saved = cache::Load(cacheFile);
    REQUIRE(saved.ok());
    REQUIRE(saved.value().has_value());
    const auto& reading = *saved.value();
    CHECK_EQ(reading.locationKey, std::string("48.8566,2.3522"));
    CHECK_EQ(reading.unit, TemperatureUnit::Celsius);
    CHECK_NEAR(reading.temperature, 15.3, 1e-9);
    CHECK_EQ(reading.weatherCode, 2);
    CHECK(reading.isDay);
    CHECK(reading.observedAt == ForecastObservedAt());
    CHECK(reading.fetchedAt == time.SystemNow());
}

AETHER_TEST("weather provider: the IStatusProvider listener receives plain snapshots") {
    FakeHttpClient http;
    http.RespondWith(200, ForecastBody(-4.0, 73, 0));
    FakeTimeSource time;
    std::mutex mutex;
    std::vector<status::WeatherSnapshot> snapshots;

    WeatherProvider provider(http, time, NewCacheFile(), ParisConfig());
    status::IStatusProvider<status::WeatherSnapshot>& contract = provider;
    REQUIRE(contract
                .Start([&](const status::WeatherSnapshot& snapshot) {
                    std::lock_guard lock(mutex);
                    snapshots.push_back(snapshot);
                })
                .ok());
    Settle(provider);
    contract.Stop();
    std::lock_guard lock(mutex);
    REQUIRE_EQ(snapshots.size(), std::size_t{2});
    CHECK_EQ(snapshots.back().state, WeatherState::Available);
    CHECK_EQ(snapshots.back().conditionText, std::wstring(L"Snow"));
    CHECK_EQ(contract.Current().state, WeatherState::Available);
}

AETHER_TEST("weather provider: malformed JSON is RequestFailed without data") {
    FakeHttpClient http;
    http.RespondWith(200, R"({"current":{"temperature_2m":15.3,)");
    FakeTimeSource time;
    ReportRecorder recorder;

    WeatherProvider provider(http, time, NewCacheFile(), ParisConfig());
    REQUIRE(provider.StartWithReports(recorder.Listener()).ok());
    Settle(provider);
    const auto snapshot = provider.Current();
    CHECK_EQ(snapshot.state, WeatherState::RequestFailed);
    CHECK_FALSE(snapshot.temperature.has_value());
    CHECK_FALSE(snapshot.conditionCode.has_value());
    CHECK(snapshot.attribution.empty());
    CHECK_FALSE(snapshot.detail.empty());
    CHECK_EQ(provider.CurrentReport().glyph, WeatherGlyph::Unknown);
    CHECK_FALSE(provider.CurrentReport().isDay.has_value());

    // Invalid responses are retried like other transient failures.
    Advance(provider, time, 30s);
    CHECK_EQ(http.RequestCount(), std::size_t{2});
}

AETHER_TEST("weather provider: out-of-range values are never shown") {
    const std::string bodies[] = {
        ForecastBody(80.0, 2, 1),       // implausible Celsius temperature
        ForecastBody(15.0, 100, 1),     // weather_code outside 0..99
        ForecastBody(15.0, -3, 1),      // negative weather_code
        ForecastBody(15.0, 2, 7),       // is_day is not 0/1
        ForecastBody(15.0, 2, 1, "F"),  // unit label does not match the request
    };
    for (const auto& body : bodies) {
        FakeHttpClient http;
        http.RespondWith(200, body);
        FakeTimeSource time;
        ReportRecorder recorder;
        WeatherProvider provider(http, time, NewCacheFile(), ParisConfig());
        REQUIRE(provider.StartWithReports(recorder.Listener()).ok());
        Settle(provider);
        CHECK_EQ(provider.Current().state, WeatherState::RequestFailed);
        CHECK_FALSE(AnyTemperature(recorder.Reports()));
    }

    // 200 °F is implausible as well.
    FakeHttpClient http;
    http.RespondWith(200, ForecastBody(200.0, 0, 1, "F"));
    FakeTimeSource time;
    WeatherProvider provider(http, time, NewCacheFile(), ParisConfig(TemperatureUnit::Fahrenheit));
    ReportRecorder recorder;
    REQUIRE(provider.StartWithReports(recorder.Listener()).ok());
    Settle(provider);
    CHECK_EQ(provider.Current().state, WeatherState::RequestFailed);
}

AETHER_TEST("weather provider: HTTP 500 and 429 are ProviderUnavailable") {
    for (const int status : {500, 503, 429}) {
        FakeHttpClient http;
        http.RespondWith(status, "<html>error</html>");
        FakeTimeSource time;
        ReportRecorder recorder;
        WeatherProvider provider(http, time, NewCacheFile(), ParisConfig());
        REQUIRE(provider.StartWithReports(recorder.Listener()).ok());
        Settle(provider);
        const auto snapshot = provider.Current();
        CHECK_EQ(snapshot.state, WeatherState::ProviderUnavailable);
        CHECK(snapshot.detail.find(std::to_string(status)) != std::string::npos);
        CHECK_FALSE(snapshot.temperature.has_value());
    }
}

AETHER_TEST("weather provider: Retry-After on 429 postpones the retry") {
    FakeHttpClient http;
    http.RespondWith(429, "", {{"Retry-After", "120"}});
    FakeTimeSource time;
    ReportRecorder recorder;
    WeatherProvider provider(http, time, NewCacheFile(), ParisConfig());
    REQUIRE(provider.StartWithReports(recorder.Listener()).ok());
    Settle(provider);
    Advance(provider, time, 30s);
    CHECK_EQ(http.RequestCount(), std::size_t{1});
    Advance(provider, time, 90s);
    CHECK_EQ(http.RequestCount(), std::size_t{2});
}

AETHER_TEST("weather provider: a rejected request reports the provider reason and is not retried early") {
    FakeHttpClient http;
    http.RespondWith(400, R"({"error":true,"reason":"Latitude must be in range of -90 to 90°. Given: 91.0."})");
    FakeTimeSource time;
    ReportRecorder recorder;
    WeatherProvider provider(http, time, NewCacheFile(), ParisConfig());
    REQUIRE(provider.StartWithReports(recorder.Listener()).ok());
    Settle(provider);
    const auto snapshot = provider.Current();
    CHECK_EQ(snapshot.state, WeatherState::RequestFailed);
    CHECK(snapshot.detail.find("Latitude must be in range") != std::string::npos);
    Advance(provider, time, 10min);
    CHECK_EQ(http.RequestCount(), std::size_t{1});
    Advance(provider, time, 20min);
    CHECK_EQ(http.RequestCount(), std::size_t{2});
}

AETHER_TEST("weather provider: a timeout without cache is Offline") {
    FakeHttpClient http;
    http.FailWith(ErrorCode::Timeout);
    FakeTimeSource time;
    ReportRecorder recorder;
    WeatherProvider provider(http, time, NewCacheFile(), ParisConfig());
    REQUIRE(provider.StartWithReports(recorder.Listener()).ok());
    Settle(provider);
    const auto snapshot = provider.Current();
    CHECK_EQ(snapshot.state, WeatherState::Offline);
    CHECK_FALSE(snapshot.temperature.has_value());
    CHECK(snapshot.attribution.empty());
    CHECK_FALSE(snapshot.detail.empty());

    http.FailWith(ErrorCode::Network);
    Advance(provider, time, 30s);
    CHECK_EQ(http.RequestCount(), std::size_t{2});
    CHECK_EQ(provider.Current().state, WeatherState::Offline);
}

// ---------------------------------------------------------------------------------------------
// Cache
// ---------------------------------------------------------------------------------------------

AETHER_TEST("weather provider: a failure with a fresh cache shows Cached data, never Available") {
    FakeHttpClient http;
    http.FailWith(ErrorCode::Timeout);
    FakeTimeSource time;
    ReportRecorder recorder;
    const auto cacheFile = NewCacheFile();
    const auto cached = CachedParis(time, 1h);
    REQUIRE(cache::Save(cacheFile, cached).ok());

    WeatherProvider provider(http, time, cacheFile, ParisConfig());
    REQUIRE(provider.StartWithReports(recorder.Listener()).ok());
    Settle(provider);

    const auto reports = recorder.Reports();
    REQUIRE_EQ(reports.size(), std::size_t{2});
    CHECK_EQ(reports[0].snapshot.state, WeatherState::Cached);  // shown while the first request runs
    CHECK_FALSE(AnyAvailable(reports));

    const auto& report = reports[1];
    const auto& snapshot = report.snapshot;
    CHECK_EQ(snapshot.state, WeatherState::Cached);
    REQUIRE(snapshot.temperature.has_value());
    CHECK_NEAR(*snapshot.temperature, 12.5, 1e-9);
    CHECK(snapshot.conditionCode == std::optional<int>(61));
    CHECK_EQ(snapshot.conditionText, std::wstring(L"Rain"));
    CHECK(snapshot.observedAt == std::optional(cached.observedAt));
    CHECK(snapshot.fetchedAt == std::optional(cached.fetchedAt));
    CHECK_EQ(snapshot.attribution, std::wstring(L"Weather data by Open-Meteo.com"));
    CHECK(snapshot.detail.find("could not be reached") != std::string::npos);
    CHECK_EQ(report.glyph, WeatherGlyph::Rain);
    CHECK(report.isDay == std::optional<bool>(false));

    // Provider errors keep showing the cached data as well.
    http.RespondWith(503, "");
    Advance(provider, time, 30s);
    CHECK_EQ(provider.Current().state, WeatherState::Cached);
    CHECK(provider.Current().detail.find("503") != std::string::npos);

    // A later success replaces the cached data and is Available.
    http.RespondWith(200, ForecastBody(16.0, 1, 1));
    Advance(provider, time, 2min);
    CHECK_EQ(provider.Current().state, WeatherState::Available);
    CHECK(provider.Current().temperature == std::optional<double>(16.0));
}

AETHER_TEST("weather provider: cache older than six hours is ignored") {
    FakeHttpClient http;
    http.FailWith(ErrorCode::Network);
    FakeTimeSource time;
    ReportRecorder recorder;
    const auto cacheFile = NewCacheFile();
    REQUIRE(cache::Save(cacheFile, CachedParis(time, 7h)).ok());

    WeatherProvider provider(http, time, cacheFile, ParisConfig());
    REQUIRE(provider.StartWithReports(recorder.Listener()).ok());
    Settle(provider);
    CHECK_EQ(provider.Current().state, WeatherState::Offline);
    CHECK_FALSE(AnyTemperature(recorder.Reports()));
}

AETHER_TEST("weather provider: cache for another location or unit is ignored") {
    WeatherReading berlin;
    {
        FakeTimeSource time;
        berlin = CachedParis(time, 10min);
        berlin.locationKey = "52.5200,13.4050";
    }
    for (int variant = 0; variant < 2; ++variant) {
        FakeHttpClient http;
        http.FailWith(ErrorCode::Network);
        FakeTimeSource time;
        ReportRecorder recorder;
        const auto cacheFile = NewCacheFile();
        REQUIRE(cache::Save(cacheFile, variant == 0 ? berlin : CachedParis(time, 10min, TemperatureUnit::Fahrenheit))
                    .ok());
        WeatherProvider provider(http, time, cacheFile, ParisConfig());
        REQUIRE(provider.StartWithReports(recorder.Listener()).ok());
        Settle(provider);
        CHECK_EQ(provider.Current().state, WeatherState::Offline);
        CHECK_FALSE(AnyTemperature(recorder.Reports()));
    }
}

AETHER_TEST("weather provider: a corrupt cache file is ignored") {
    FakeHttpClient http;
    http.RespondWith(200, ForecastBody(15.3, 2, 1));
    FakeTimeSource time;
    ReportRecorder recorder;
    const auto cacheFile = NewCacheFile();
    REQUIRE(fs::WriteFileAtomic(cacheFile, "{\"version\":1,\"temperature\":").ok());
    WeatherProvider provider(http, time, cacheFile, ParisConfig());
    REQUIRE(provider.StartWithReports(recorder.Listener()).ok());
    Settle(provider);
    CHECK_EQ(provider.Current().state, WeatherState::Available);
    CHECK_EQ(recorder.Reports().front().snapshot.state, WeatherState::Loading);
    auto rewritten = cache::Load(cacheFile);
    CHECK(rewritten.ok());
}

// ---------------------------------------------------------------------------------------------
// Scheduling and retries
// ---------------------------------------------------------------------------------------------

AETHER_TEST("weather provider: refreshes every interval and degrades to Cached, then expires") {
    FakeHttpClient http;
    http.RespondWith(200, ForecastBody(15.3, 2, 1));
    FakeTimeSource time;
    ReportRecorder recorder;
    WeatherProvider provider(http, time, NewCacheFile(), ParisConfig());
    REQUIRE(provider.StartWithReports(recorder.Listener()).ok());
    Settle(provider);

    Advance(provider, time, 29min);
    CHECK_EQ(http.RequestCount(), std::size_t{1});
    CHECK_EQ(provider.Current().state, WeatherState::Available);
    Advance(provider, time, 1min);
    CHECK_EQ(http.RequestCount(), std::size_t{2});
    CHECK_EQ(provider.Current().state, WeatherState::Available);
    const auto lastSuccess = time.SystemNow();
    CHECK(provider.Current().fetchedAt == std::optional(lastSuccess));

    http.FailWith(ErrorCode::Network);
    Advance(provider, time, 30min);
    CHECK_EQ(http.RequestCount(), std::size_t{3});
    auto snapshot = provider.Current();
    CHECK_EQ(snapshot.state, WeatherState::Cached);
    CHECK(snapshot.temperature.has_value());
    CHECK(snapshot.fetchedAt == std::optional(lastSuccess));
    CHECK_EQ(snapshot.attribution, std::wstring(L"Weather data by Open-Meteo.com"));

    Advance(provider, time, 5h);  // data is 5 h 30 min old
    CHECK_EQ(provider.Current().state, WeatherState::Cached);
    Advance(provider, time, 31min);  // 6 h 1 min
    snapshot = provider.Current();
    CHECK_EQ(snapshot.state, WeatherState::Offline);
    CHECK_FALSE(snapshot.temperature.has_value());
    CHECK(snapshot.attribution.empty());
}

AETHER_TEST("weather provider: a wall clock moved back does not hide data fetched in this session") {
    FakeHttpClient http;
    http.RespondWith(200, ForecastBody(15.3, 2, 1));
    SkewableTimeSource time;
    ReportRecorder recorder;
    WeatherProvider provider(http, time, NewCacheFile(), ParisConfig());
    REQUIRE(provider.StartWithReports(recorder.Listener()).ok());
    Settle(provider);
    const auto fetchedAt = provider.Current().fetchedAt;
    REQUIRE(fetchedAt.has_value());

    // The reading is two minutes old but its timestamp is now 58 minutes in the "future".
    time.Advance(2min);
    time.ShiftWallClock(-1h);
    provider.NotifyClockChanged();
    Settle(provider);
    const auto snapshot = provider.Current();
    CHECK_EQ(snapshot.state, WeatherState::Available);
    CHECK(snapshot.temperature.has_value());
    CHECK(snapshot.fetchedAt == fetchedAt);
    CHECK_EQ(http.RequestCount(), std::size_t{1});

    // The steady clock still drives the regular refresh.
    time.Advance(28min);
    provider.NotifyClockChanged();
    Settle(provider);
    CHECK_EQ(http.RequestCount(), std::size_t{2});
    CHECK_EQ(provider.Current().state, WeatherState::Available);
}

AETHER_TEST("weather provider: an overdue reading is Cached while the scheduled refresh is blocked") {
    FakeHttpClient http;
    http.RespondWith(200, ForecastBody(15.3, 2, 1));
    FakeTimeSource time;
    ReportRecorder recorder;
    WeatherProvider provider(http, time, NewCacheFile(), ParisConfig());
    REQUIRE(provider.StartWithReports(recorder.Listener()).ok());
    Settle(provider);
    const auto fetchedAt = provider.Current().fetchedAt;
    REQUIRE(fetchedAt.has_value());

    http.BlockRequests();
    time.Advance(30min);
    provider.NotifyClockChanged();
    REQUIRE(http.WaitForRequests(2, kWait));
    const auto snapshot = provider.Current();
    CHECK_EQ(snapshot.state, WeatherState::Cached);
    CHECK(snapshot.fetchedAt == fetchedAt);
    CHECK(snapshot.temperature == std::optional<double>(15.3));
    CHECK_EQ(snapshot.attribution, std::wstring(kAttribution));
    http.Release();
    Settle(provider);
    CHECK_EQ(provider.Current().state, WeatherState::Available);
    CHECK(provider.Current().fetchedAt == std::optional(time.SystemNow()));
}

AETHER_TEST("weather provider: cache expires while a refresh is still blocked in HTTP") {
    FakeHttpClient http;
    http.RespondWith(200, ForecastBody(15.3, 2, 1));
    FakeTimeSource time;
    ReportRecorder recorder;
    WeatherProvider provider(http, time, NewCacheFile(), ParisConfig());
    REQUIRE(provider.StartWithReports(recorder.Listener()).ok());
    Settle(provider);
    REQUIRE_EQ(provider.Current().state, WeatherState::Available);

    http.BlockRequests();
    time.Advance(30min);
    provider.NotifyClockChanged();
    REQUIRE(http.WaitForRequests(2, kWait));
    REQUIRE_EQ(provider.Current().state, WeatherState::Cached);
    const auto beforeExpiry = recorder.Count();
    time.Advance(5h + 30min);
    provider.NotifyClockChanged();
    // No request completes: the scheduler must remove the expired value independently.
    REQUIRE(recorder.WaitForCount(beforeExpiry + 1, kWait));
    CHECK_EQ(provider.Current().state, WeatherState::Loading);
    CHECK_FALSE(provider.Current().temperature.has_value());
    CHECK_EQ(http.RequestCount(), std::size_t{2});
    CHECK_FALSE(provider.WaitUntilIdle(20ms));

    http.Release();
    Settle(provider);
    CHECK_EQ(provider.Current().state, WeatherState::Available);
}

AETHER_TEST("weather provider: disabling publishes before cancelled HTTP returns") {
    FakeHttpClient http;
    http.BlockRequests();
    FakeTimeSource time;
    ReportRecorder recorder;
    WeatherProvider provider(http, time, NewCacheFile(), ParisConfig());
    REQUIRE(provider.StartWithReports(recorder.Listener()).ok());
    REQUIRE(http.WaitForRequests(1, kWait));
    auto disabled = ParisConfig();
    disabled.enabled = false;
    REQUIRE(provider.UpdateConfig(disabled).ok());
    Settle(provider);
    CHECK_EQ(provider.Current().state, WeatherState::Disabled);
    CHECK_FALSE(provider.Current().temperature.has_value());
    CHECK_EQ(http.RequestCount(), std::size_t{1});
}

AETHER_TEST("weather provider: retries are bounded, then wait for the scheduled refresh") {
    FakeHttpClient http;
    http.FailWith(ErrorCode::Timeout);
    FakeTimeSource time;
    ReportRecorder recorder;
    WeatherProvider provider(http, time, NewCacheFile(), ParisConfig());
    REQUIRE(provider.StartWithReports(recorder.Listener()).ok());
    Settle(provider);
    CHECK_EQ(http.RequestCount(), std::size_t{1});

    Advance(provider, time, 29s);
    CHECK_EQ(http.RequestCount(), std::size_t{1});
    Advance(provider, time, 1s);  // retry 1 after 30 s
    CHECK_EQ(http.RequestCount(), std::size_t{2});
    Advance(provider, time, 119s);
    CHECK_EQ(http.RequestCount(), std::size_t{2});
    Advance(provider, time, 1s);  // retry 2 after 2 min
    CHECK_EQ(http.RequestCount(), std::size_t{3});
    Advance(provider, time, 10min);  // retry 3 after 10 min
    CHECK_EQ(http.RequestCount(), std::size_t{4});
    Advance(provider, time, 10min);  // no fourth retry
    CHECK_EQ(http.RequestCount(), std::size_t{4});
    Advance(provider, time, 19min);
    CHECK_EQ(http.RequestCount(), std::size_t{4});
    Advance(provider, time, 1min);  // scheduled refresh, 30 min after the last attempt
    CHECK_EQ(http.RequestCount(), std::size_t{5});
    Advance(provider, time, 30s);  // a new, equally bounded retry sequence
    CHECK_EQ(http.RequestCount(), std::size_t{6});
    CHECK_EQ(provider.Current().state, WeatherState::Offline);
}

AETHER_TEST("weather provider: changing only the refresh interval reschedules without fetching") {
    FakeHttpClient http;
    http.RespondWith(200, ForecastBody(15.3, 2, 1));
    FakeTimeSource time;
    ReportRecorder recorder;
    WeatherProvider provider(http, time, NewCacheFile(), ParisConfig());
    REQUIRE(provider.StartWithReports(recorder.Listener()).ok());
    Settle(provider);

    auto hourly = ParisConfig();
    hourly.refreshInterval = 60min;
    REQUIRE(provider.UpdateConfig(hourly).ok());
    Settle(provider);
    CHECK_EQ(http.RequestCount(), std::size_t{1});
    Advance(provider, time, 45min);
    CHECK_EQ(http.RequestCount(), std::size_t{1});
    CHECK_EQ(provider.Current().state, WeatherState::Available);
    Advance(provider, time, 15min);
    CHECK_EQ(http.RequestCount(), std::size_t{2});
}

AETHER_TEST("weather provider: an interval change right after a location change keeps the immediate fetch") {
    FakeHttpClient http;
    http.RespondWith(200, ForecastBody(15.3, 2, 1));
    FakeTimeSource time;
    std::mutex mutex;
    std::condition_variable changed;
    bool berlinLoadingSeen = false;
    bool gateOpen = false;
    WeatherProvider provider(http, time, NewCacheFile(), ParisConfig());
    // Holds the provider thread inside the listener when it announces the new location, so the second
    // update deterministically arrives after the location change was applied but before its fetch.
    REQUIRE(provider
                .StartWithReports([&](const WeatherReport& report) {
                    if (report.snapshot.state != WeatherState::Loading ||
                        report.snapshot.locationName != L"Berlin, Germany") {
                        return;
                    }
                    std::unique_lock lock(mutex);
                    berlinLoadingSeen = true;
                    changed.notify_all();
                    changed.wait_for(lock, kWait, [&] { return gateOpen; });
                })
                .ok());
    Settle(provider);
    REQUIRE_EQ(http.RequestCount(), std::size_t{1});

    auto berlin = ParisConfig();
    berlin.latitude = 52.52;
    berlin.longitude = 13.405;
    berlin.locationName = L"Berlin, Germany";
    REQUIRE(provider.UpdateConfig(berlin).ok());
    {
        std::unique_lock lock(mutex);
        REQUIRE(changed.wait_for(lock, kWait, [&] { return berlinLoadingSeen; }));
    }
    berlin.refreshInterval = 60min;
    REQUIRE(provider.UpdateConfig(berlin).ok());
    {
        std::lock_guard lock(mutex);
        gateOpen = true;
    }
    changed.notify_all();
    Settle(provider);

    // Berlin is fetched at once, not one (new) interval after the last Paris request.
    REQUIRE_EQ(http.RequestCount(), std::size_t{2});
    CHECK(http.Requests()[1].url.find("latitude=52.5200&longitude=13.4050") != std::string::npos);
    CHECK_EQ(provider.Current().state, WeatherState::Available);
    Advance(provider, time, 59min);
    CHECK_EQ(http.RequestCount(), std::size_t{2});
    Advance(provider, time, 1min);
    CHECK_EQ(http.RequestCount(), std::size_t{3});
}

AETHER_TEST("weather provider: RefreshNow is rate limited to once per minute") {
    FakeHttpClient http;
    http.RespondWith(200, ForecastBody(15.3, 2, 1));
    FakeTimeSource time;
    ReportRecorder recorder;
    WeatherProvider provider(http, time, NewCacheFile(), ParisConfig());
    CHECK_EQ(ErrorOf(provider.RefreshNow()), ErrorCode::InvalidState);  // not started
    REQUIRE(provider.StartWithReports(recorder.Listener()).ok());
    Settle(provider);

    CHECK_EQ(ErrorOf(provider.RefreshNow()), ErrorCode::RateLimited);  // the first fetch just ran
    Advance(provider, time, 60s);
    CHECK(provider.RefreshNow().ok());
    Settle(provider);
    CHECK_EQ(http.RequestCount(), std::size_t{2});
    CHECK_EQ(ErrorOf(provider.RefreshNow()), ErrorCode::RateLimited);
    Advance(provider, time, 30s);
    CHECK_EQ(ErrorOf(provider.RefreshNow()), ErrorCode::RateLimited);
    Advance(provider, time, 30s);
    CHECK(provider.RefreshNow().ok());
    Settle(provider);
    CHECK_EQ(http.RequestCount(), std::size_t{3});
    CHECK_EQ(provider.Current().state, WeatherState::Available);
}

// ---------------------------------------------------------------------------------------------
// Configuration changes
// ---------------------------------------------------------------------------------------------

AETHER_TEST("weather provider: switching the unit refetches and never relabels old data") {
    FakeHttpClient http;
    http.SetHandler([](const net::HttpRequest& request, const CancellationToken&) -> Result<net::HttpResponse> {
        net::HttpResponse response;
        response.status = 200;
        response.body = request.url.find("temperature_unit=fahrenheit") != std::string::npos
                            ? ForecastBody(59.5, 3, 0, "F")
                            : ForecastBody(15.3, 2, 1);
        return response;
    });
    FakeTimeSource time;
    ReportRecorder recorder;
    WeatherProvider provider(http, time, NewCacheFile(), ParisConfig());
    REQUIRE(provider.StartWithReports(recorder.Listener()).ok());
    Settle(provider);
    CHECK_EQ(provider.Current().state, WeatherState::Available);
    const auto before = recorder.Count();

    REQUIRE(provider.UpdateConfig(ParisConfig(TemperatureUnit::Fahrenheit)).ok());
    Settle(provider);
    const auto requests = http.Requests();
    REQUIRE_EQ(requests.size(), std::size_t{2});
    CHECK(requests[1].url.find("temperature_unit=fahrenheit") != std::string::npos);

    const auto reports = recorder.Reports();
    REQUIRE(reports.size() >= before + 2);
    const auto& switching = reports[before].snapshot;
    CHECK_EQ(switching.state, WeatherState::Loading);
    CHECK_EQ(switching.unit, TemperatureUnit::Fahrenheit);
    CHECK_FALSE(switching.temperature.has_value());  // the Celsius value is never shown as Fahrenheit
    const auto& report = reports.back();
    CHECK_EQ(report.snapshot.state, WeatherState::Available);
    CHECK_EQ(report.snapshot.unit, TemperatureUnit::Fahrenheit);
    CHECK(report.snapshot.temperature == std::optional<double>(59.5));
    CHECK_EQ(report.glyph, WeatherGlyph::Cloudy);
    CHECK(report.isDay == std::optional<bool>(false));

    // A display-name change republishes without a request.
    auto renamed = ParisConfig(TemperatureUnit::Fahrenheit);
    renamed.locationName = L"Paris";
    REQUIRE(provider.UpdateConfig(renamed).ok());
    Settle(provider);
    CHECK_EQ(http.RequestCount(), std::size_t{2});
    CHECK_EQ(provider.Current().locationName, std::wstring(L"Paris"));
    CHECK_EQ(provider.Current().state, WeatherState::Available);
}

AETHER_TEST("weather provider: a location change cancels the request in flight") {
    FakeHttpClient http;
    http.RespondWith(200, ForecastBody(15.3, 2, 1));
    http.BlockRequests();
    FakeTimeSource time;
    ReportRecorder recorder;
    WeatherProvider provider(http, time, NewCacheFile(), ParisConfig());
    REQUIRE(provider.StartWithReports(recorder.Listener()).ok());
    REQUIRE(http.WaitForRequests(1, kWait));

    auto berlin = ParisConfig();
    berlin.latitude = 52.52;
    berlin.longitude = 13.405;
    berlin.locationName = L"Berlin, Germany";
    REQUIRE(provider.UpdateConfig(berlin).ok());
    REQUIRE(http.WaitForRequests(2, kWait));
    CHECK_EQ(http.CancelledCount(), std::size_t{1});
    CHECK(http.Requests()[1].url.find("latitude=52.5200&longitude=13.4050") != std::string::npos);

    http.Release();
    Settle(provider);
    CHECK_EQ(http.RequestCount(), std::size_t{2});
    CHECK_EQ(provider.Current().state, WeatherState::Available);
    CHECK_EQ(provider.Current().locationName, std::wstring(L"Berlin, Germany"));
}

AETHER_TEST("weather provider: invalid settings and misuse are rejected") {
    FakeHttpClient http;
    http.RespondWith(200, ForecastBody(15.3, 2, 1));
    FakeTimeSource time;
    ReportRecorder recorder;
    WeatherProvider provider(http, time, NewCacheFile(), ParisConfig());
    CHECK_EQ(ErrorOf(provider.Start(nullptr)), ErrorCode::InvalidArgument);
    CHECK_EQ(ErrorOf(provider.StartWithReports(nullptr)), ErrorCode::InvalidArgument);

    auto invalid = ParisConfig();
    invalid.latitude = 123.0;
    CHECK_EQ(ErrorOf(provider.UpdateConfig(invalid)), ErrorCode::InvalidArgument);
    WeatherProvider invalidProvider(http, time, NewCacheFile(), invalid);
    CHECK_EQ(ErrorOf(invalidProvider.StartWithReports(recorder.Listener())), ErrorCode::InvalidArgument);

    REQUIRE(provider.StartWithReports(recorder.Listener()).ok());
    CHECK_EQ(ErrorOf(provider.StartWithReports(recorder.Listener())), ErrorCode::InvalidState);
    Settle(provider);
    CHECK_EQ(provider.Current().state, WeatherState::Available);  // the rejected settings did not apply
    provider.Stop();
    provider.Stop();  // idempotent

    // Restart: a new session, so the remembered reading is only Cached until it is refetched.
    ReportRecorder second;
    time.Advance(2min);
    REQUIRE(provider.StartWithReports(second.Listener()).ok());
    Settle(provider);
    const auto reports = second.Reports();
    REQUIRE_EQ(reports.size(), std::size_t{2});
    CHECK_EQ(reports[0].snapshot.state, WeatherState::Cached);
    CHECK_EQ(reports[1].snapshot.state, WeatherState::Available);
}

// ---------------------------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------------------------

AETHER_TEST("weather provider: no listener call after Stop, even with a request in flight") {
    FakeHttpClient http;
    http.RespondWith(200, ForecastBody(15.3, 2, 1));
    http.BlockRequests();
    FakeTimeSource time;
    ReportRecorder recorder;
    WeatherProvider provider(http, time, NewCacheFile(), ParisConfig());
    REQUIRE(provider.StartWithReports(recorder.Listener()).ok());
    REQUIRE(http.WaitForRequests(1, kWait));
    REQUIRE(recorder.WaitForCount(1, kWait));

    const auto stopStarted = std::chrono::steady_clock::now();
    provider.Stop();
    CHECK(std::chrono::steady_clock::now() - stopStarted < kWait);
    CHECK_EQ(http.CancelledCount(), std::size_t{1});
    const auto calls = recorder.Count();

    http.Release();
    CHECK(provider.UpdateConfig(ParisConfig(TemperatureUnit::Fahrenheit)).ok());
    provider.NotifyClockChanged();
    CHECK_EQ(ErrorOf(provider.RefreshNow()), ErrorCode::InvalidState);
    time.Advance(2h);
    provider.NotifyClockChanged();
    std::this_thread::sleep_for(200ms);
    CHECK_EQ(recorder.Count(), calls);
    CHECK_EQ(http.RequestCount(), std::size_t{1});
    provider.Stop();
}

AETHER_TEST("weather provider: the listener may stop the provider") {
    FakeHttpClient http;
    http.RespondWith(200, ForecastBody(15.3, 2, 1));
    FakeTimeSource time;
    std::atomic<int> calls{0};
    WeatherProvider provider(http, time, NewCacheFile(), ParisConfig());
    REQUIRE(provider
                .StartWithReports([&](const WeatherReport&) {
                    ++calls;
                    provider.Stop();
                })
                .ok());
    const auto deadline = std::chrono::steady_clock::now() + kWait;
    while (calls.load() == 0 && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(5ms);
    std::this_thread::sleep_for(100ms);
    CHECK_EQ(calls.load(), 1);
    CHECK_EQ(ErrorOf(provider.RefreshNow()), ErrorCode::InvalidState);

    // Start() completes the stop (joins the old thread) and runs a new session.
    ReportRecorder recorder;
    REQUIRE(provider.StartWithReports(recorder.Listener()).ok());
    Settle(provider);
    CHECK_EQ(provider.Current().state, WeatherState::Available);
    CHECK_EQ(calls.load(), 1);
    provider.Stop();
}

AETHER_TEST("weather provider: a throwing listener does not stop updates") {
    FakeHttpClient http;
    http.RespondWith(200, ForecastBody(15.3, 2, 1));
    FakeTimeSource time;
    std::atomic<int> calls{0};
    WeatherProvider provider(http, time, NewCacheFile(), ParisConfig());
    REQUIRE(provider
                .StartWithReports([&](const WeatherReport&) {
                    ++calls;
                    throw std::runtime_error("consumer bug");
                })
                .ok());
    Settle(provider);
    CHECK_EQ(calls.load(), 2);
    CHECK_EQ(provider.Current().state, WeatherState::Available);
}

AETHER_TEST("weather diagnostics: exception text from transports and listeners never reaches logs") {
    struct ShutdownLog {
        ~ShutdownLog() { diag::Logger::Shutdown(); }
    } shutdown;
    diag::LoggerOptions options;
    options.directory = aether::test::MakeTempDirectory("weather-private-log");
    options.baseName = L"weather";
    options.writeToDebugger = false;
    diag::Logger::Initialize(options);

    FakeHttpClient http;
    http.SetHandler([](const net::HttpRequest&, const CancellationToken&) -> Result<net::HttpResponse> {
        throw std::runtime_error("synthetic-private-transport-query");
    });
    FakeTimeSource time;
    WeatherProvider provider(http, time, NewCacheFile(), ParisConfig());
    REQUIRE(provider.StartWithReports([](const WeatherReport&) {
        throw std::runtime_error("synthetic-private-listener-data");
    }).ok());
    Settle(provider);
    CHECK_EQ(provider.Current().state, WeatherState::RequestFailed);
    CHECK(provider.Current().detail.find("synthetic-private") == std::string::npos);
    provider.Stop();

    GeocodingClient client(http);
    auto searched = client.Search(L"Example City", L"en", CancellationToken{});
    REQUIRE_FALSE(searched.ok());
    CHECK_EQ(searched.error().code, ErrorCode::Internal);
    diag::Logger::Shutdown();
    const auto contents = fs::ReadFile(options.directory / L"weather.log", 64 * 1024);
    REQUIRE(contents.ok());
    CHECK(contents.value().find("synthetic-private") == std::string::npos);
    CHECK(contents.value().find("weather listener threw an exception") != std::string::npos);
    CHECK(contents.value().find("weather request threw an exception") != std::string::npos);
    CHECK(contents.value().find("location search threw an exception") != std::string::npos);
}
