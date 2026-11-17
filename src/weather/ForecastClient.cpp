// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "weather/ForecastClient.h"

#include "diagnostics/Logger.h"
#include "weather/OpenMeteo.h"

#include <cmath>
#include <exception>
#include <string>

namespace aether::weather {
namespace {

constexpr std::string_view kLogComponent = "weather";

bool ValidCoordinate(double latitude, double longitude) noexcept {
    return std::isfinite(latitude) && std::isfinite(longitude) && latitude >= -90.0 && latitude <= 90.0 &&
           longitude >= -180.0 && longitude <= 180.0;
}

// Sends `url` and returns the body of a 200 answer. `what` names the request in fixed public text.
Result<std::string> Get(net::IHttpClient& http, std::string url, std::string_view what, const CancellationToken& cancel) {
    if (cancel.IsCancelled()) return MakeError(ErrorCode::Cancelled, std::string(what) + " cancelled");
    auto response = http.Send(openmeteo::MakeRequest(std::move(url)), cancel);
    if (cancel.IsCancelled()) return MakeError(ErrorCode::Cancelled, std::string(what) + " cancelled");
    if (!response) {
        AETHER_LOG_WARN(kLogComponent, "{} failed: {}", what, ToString(response.error().code));
        return response.error();
    }
    auto& reply = response.value();
    if (reply.status != 200) {
        // The provider reason may echo the request (coordinates): caller only, never the log.
        const auto reason = openmeteo::ProviderErrorReason(reply.body);
        std::string message = std::string(what) + " failed with HTTP " + std::to_string(reply.status);
        if (reason) message += ": " + *reason;
        AETHER_LOG_WARN(kLogComponent, "{} failed with HTTP {}", what, reply.status);
        ErrorCode code = ErrorCode::Http;
        if (reply.status == 429) {
            code = ErrorCode::RateLimited;
        } else if (reply.status >= 500 && reply.status <= 599) {
            code = ErrorCode::Unavailable;
        }
        return MakeError(code, std::move(message), reply.status);
    }
    return std::move(reply.body);
}

template <class Body>
auto Guarded(std::string_view what, Body&& body) -> decltype(body()) {
    try {
        return body();
    } catch (const std::exception&) {
        AETHER_LOG_ERROR(kLogComponent, "{} threw an exception", what);
        return MakeError(ErrorCode::Internal, std::string(what) + " failed unexpectedly");
    } catch (...) {
        AETHER_LOG_ERROR(kLogComponent, "{} threw a non-standard exception", what);
        return MakeError(ErrorCode::Internal, std::string(what) + " failed unexpectedly");
    }
}

}  // namespace

Result<DetailedForecast> ForecastClient::Fetch(double latitude, double longitude, status::TemperatureUnit unit,
                                               const CancellationToken& cancel) const {
    constexpr std::string_view what = "forecast";
    return Guarded(what, [&]() -> Result<DetailedForecast> {
        if (!ValidCoordinate(latitude, longitude)) return MakeError(ErrorCode::InvalidArgument, "forecast coordinates are invalid");
        auto body = Get(http_, openmeteo::BuildDetailedForecastUrl(latitude, longitude, unit), what, cancel);
        if (!body) return body.error();
        auto forecast = openmeteo::ParseDetailedForecast(body.value(), unit);
        if (cancel.IsCancelled()) return MakeError(ErrorCode::Cancelled, "forecast cancelled");
        if (!forecast) {
            AETHER_LOG_WARN(kLogComponent, "invalid forecast response ({})", ToString(forecast.error().code));
            return forecast.error();
        }
        forecast.value().fetchedAt = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
        return forecast;
    });
}

Result<std::vector<PointReading>> ForecastClient::FetchPoints(const std::vector<GeoPoint>& points,
                                                              status::TemperatureUnit unit,
                                                              const CancellationToken& cancel) const {
    constexpr std::string_view what = "map temperatures";
    return Guarded(what, [&]() -> Result<std::vector<PointReading>> {
        if (points.empty() || points.size() > kMaxForecastPoints) {
            return MakeError(ErrorCode::InvalidArgument, "map temperatures need 1 to 24 places");
        }
        for (const auto& point : points) {
            if (!ValidCoordinate(point.latitude, point.longitude)) {
                return MakeError(ErrorCode::InvalidArgument, "map place coordinates are invalid");
            }
        }
        auto body = Get(http_, openmeteo::BuildPointsUrl(points, unit), what, cancel);
        if (!body) return body.error();
        auto readings = openmeteo::ParsePoints(body.value(), unit, points.size());
        if (cancel.IsCancelled()) return MakeError(ErrorCode::Cancelled, "map temperatures cancelled");
        if (!readings) AETHER_LOG_WARN(kLogComponent, "invalid map temperature response ({})", ToString(readings.error().code));
        return readings;
    });
}

}  // namespace aether::weather
