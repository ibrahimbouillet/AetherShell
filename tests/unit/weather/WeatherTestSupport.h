// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Test doubles shared by the weather unit tests: an in-memory IHttpClient that records requests and
// answers from a scripted handler (optionally blocking until cancelled or released), canned
// Open-Meteo bodies, and a thread-safe recorder for provider listener calls.
#pragma once

#include "core/Cancellation.h"
#include "core/Http.h"
#include "core/Result.h"
#include "core/Time.h"
#include "weather/WeatherTypes.h"

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <format>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace aether::weather::testing {

class FakeHttpClient final : public net::IHttpClient {
public:
    using Handler = std::function<Result<net::HttpResponse>(const net::HttpRequest&, const CancellationToken&)>;

    void SetHandler(Handler handler) {
        std::lock_guard lock(mutex_);
        handler_ = std::move(handler);
    }

    void RespondWith(int status, std::string body, std::vector<net::HttpHeader> headers = {}) {
        SetHandler([status, body = std::move(body), headers = std::move(headers)](const net::HttpRequest& request,
                                                                                  const CancellationToken&) {
            net::HttpResponse response;
            response.status = status;
            response.body = body;
            response.headers = headers;
            response.finalUrl = request.url;
            return Result<net::HttpResponse>(std::move(response));
        });
    }

    void FailWith(ErrorCode code) {
        SetHandler([code](const net::HttpRequest&, const CancellationToken&) -> Result<net::HttpResponse> {
            return MakeError(code, "fake transport failure");
        });
    }

    // While blocking, Send() waits until Release() is called or the request is cancelled.
    void BlockRequests() {
        std::lock_guard lock(mutex_);
        blocking_ = true;
    }

    void Release() {
        std::lock_guard lock(mutex_);
        blocking_ = false;
        changed_.notify_all();
    }

    [[nodiscard]] std::vector<net::HttpRequest> Requests() const {
        std::lock_guard lock(mutex_);
        return requests_;
    }

    [[nodiscard]] std::size_t RequestCount() const {
        std::lock_guard lock(mutex_);
        return requests_.size();
    }

    [[nodiscard]] std::size_t CancelledCount() const {
        std::lock_guard lock(mutex_);
        return cancelled_;
    }

    [[nodiscard]] bool WaitForRequests(std::size_t count, std::chrono::milliseconds timeout) const {
        std::unique_lock lock(mutex_);
        return changed_.wait_for(lock, timeout, [&] { return requests_.size() >= count; });
    }

    Result<net::HttpResponse> Send(const net::HttpRequest& request, const CancellationToken& cancel) override {
        Handler handler;
        bool block = false;
        {
            std::lock_guard lock(mutex_);
            requests_.push_back(request);
            handler = handler_;
            block = blocking_;
            changed_.notify_all();
        }
        if (block) {
            auto registration = cancel.OnCancel([this] {
                std::lock_guard lock(mutex_);
                changed_.notify_all();
            });
            std::unique_lock lock(mutex_);
            changed_.wait(lock, [&] { return !blocking_ || cancel.IsCancelled(); });
        }
        if (cancel.IsCancelled()) {
            std::lock_guard lock(mutex_);
            ++cancelled_;
            return MakeError(ErrorCode::Cancelled, "request cancelled");
        }
        if (!handler) return MakeError(ErrorCode::Network, "no fake response configured");
        return handler(request, cancel);
    }

    Result<net::HttpResponse> SendStreaming(const net::HttpRequest&, const net::HttpChunkSink&,
                                            const net::HttpProgress&, const CancellationToken&) override {
        return MakeError(ErrorCode::Unsupported, "the weather module never streams");
    }

private:
    mutable std::mutex mutex_;
    mutable std::condition_variable changed_;
    Handler handler_;
    std::vector<net::HttpRequest> requests_;
    std::size_t cancelled_ = 0;
    bool blocking_ = false;
};

// Manually advanced clock whose wall clock can also be moved on its own (for example backwards, as
// when Windows corrects the system time) while the steady clock keeps running forward.
class SkewableTimeSource final : public ITimeSource {
public:
    [[nodiscard]] std::chrono::system_clock::time_point SystemNow() const override {
        std::lock_guard lock(mutex_);
        return base_.SystemNow() + skew_;
    }
    [[nodiscard]] std::chrono::steady_clock::time_point SteadyNow() const override { return base_.SteadyNow(); }

    // Advances both clocks.
    void Advance(std::chrono::nanoseconds delta) { base_.Advance(delta); }

    // Moves only the wall clock.
    void ShiftWallClock(std::chrono::system_clock::duration delta) {
        std::lock_guard lock(mutex_);
        skew_ += delta;
    }

private:
    mutable std::mutex mutex_;
    FakeTimeSource base_;
    std::chrono::system_clock::duration skew_{};
};

// UTF-8 degree sign, as Open-Meteo writes it in "current_units".
inline constexpr std::string_view kDegree = "\xC2\xB0";

// A realistic forecast response for Paris requested with timezone=auto ("current.time" is local
// time, UTC+2 in October).
inline std::string ForecastBody(double temperature, int weatherCode, int isDay, std::string_view unitLetter = "C") {
    return std::format(
        R"({{"latitude":48.86,"longitude":2.3399997,"generationtime_ms":0.0450611114501953,)"
        R"("utc_offset_seconds":7200,"timezone":"Europe/Paris","timezone_abbreviation":"GMT+2","elevation":43.0,)"
        R"("current_units":{{"time":"iso8601","interval":"seconds","temperature_2m":"{}{}",)"
        R"("weather_code":"wmo code","is_day":""}},)"
        R"("current":{{"time":"2026-10-17T17:00","interval":900,"temperature_2m":{},"weather_code":{},"is_day":{}}}}})",
        kDegree, unitLetter, temperature, weatherCode, isDay);
}

// 2026-10-17T17:00 at UTC+2.
inline std::chrono::system_clock::time_point ForecastObservedAt() {
    using namespace std::chrono;
    return system_clock::time_point{sys_days{year{2026} / October / day{17}} + hours{15}};
}

// Collects listener calls from any thread.
class ReportRecorder {
public:
    [[nodiscard]] std::function<void(const WeatherReport&)> Listener() {
        return [this](const WeatherReport& report) {
            std::lock_guard lock(mutex_);
            reports_.push_back(report);
            changed_.notify_all();
        };
    }

    [[nodiscard]] std::vector<WeatherReport> Reports() const {
        std::lock_guard lock(mutex_);
        return reports_;
    }

    [[nodiscard]] std::size_t Count() const {
        std::lock_guard lock(mutex_);
        return reports_.size();
    }

    [[nodiscard]] WeatherReport Last() const {
        std::lock_guard lock(mutex_);
        return reports_.empty() ? WeatherReport{} : reports_.back();
    }

    [[nodiscard]] bool WaitForCount(std::size_t count, std::chrono::milliseconds timeout) const {
        std::unique_lock lock(mutex_);
        return changed_.wait_for(lock, timeout, [&] { return reports_.size() >= count; });
    }

private:
    mutable std::mutex mutex_;
    mutable std::condition_variable changed_;
    std::vector<WeatherReport> reports_;
};

inline WeatherConfig ParisConfig(status::TemperatureUnit unit = status::TemperatureUnit::Celsius) {
    WeatherConfig config;
    config.enabled = true;
    config.latitude = 48.8566;
    config.longitude = 2.3522;
    config.locationName = L"Paris, France";
    config.unit = unit;
    return config;
}

}  // namespace aether::weather::testing
