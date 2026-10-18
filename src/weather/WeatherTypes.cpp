// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "weather/WeatherTypes.h"

#include <cmath>

namespace aether::weather {

Result<void> ValidateConfig(const WeatherConfig& config) {
    if (config.latitude.has_value() != config.longitude.has_value()) {
        return MakeError(ErrorCode::InvalidArgument, "latitude and longitude must be set together");
    }
    if (config.latitude) {
        const double lat = *config.latitude;
        const double lon = *config.longitude;
        if (!std::isfinite(lat) || lat < -90.0 || lat > 90.0) {
            return MakeError(ErrorCode::InvalidArgument, "latitude must be within -90..90");
        }
        if (!std::isfinite(lon) || lon < -180.0 || lon > 180.0) {
            return MakeError(ErrorCode::InvalidArgument, "longitude must be within -180..180");
        }
    }
    if (config.refreshInterval < kMinRefreshInterval || config.refreshInterval > kMaxRefreshInterval) {
        return MakeError(ErrorCode::InvalidArgument, "refresh interval must be between 10 minutes and 24 hours");
    }
    if (config.unit != status::TemperatureUnit::Celsius && config.unit != status::TemperatureUnit::Fahrenheit) {
        return MakeError(ErrorCode::InvalidArgument, "unknown temperature unit");
    }
    return Ok();
}

bool operator==(const WeatherReport& a, const WeatherReport& b) {
    const auto& x = a.snapshot;
    const auto& y = b.snapshot;
    return a.glyph == b.glyph && a.isDay == b.isDay && x.state == y.state && x.temperature == y.temperature &&
           x.unit == y.unit && x.conditionCode == y.conditionCode && x.conditionText == y.conditionText &&
           x.locationName == y.locationName && x.observedAt == y.observedAt && x.fetchedAt == y.fetchedAt &&
           x.attribution == y.attribution && x.detail == y.detail;
}

std::wstring FormatLocationLabel(const LocationCandidate& candidate) {
    std::wstring label = candidate.name;
    std::wstring_view previous = candidate.name;
    for (const std::wstring* part : {&candidate.admin1, &candidate.country}) {
        if (part->empty() || *part == previous) continue;
        if (!label.empty()) label += L", ";
        label += *part;
        previous = *part;
    }
    return label;
}

std::string_view ToString(status::WeatherState state) noexcept {
    using status::WeatherState;
    switch (state) {
        case WeatherState::Disabled: return "disabled";
        case WeatherState::LocationNotConfigured: return "location-not-configured";
        case WeatherState::Loading: return "loading";
        case WeatherState::Available: return "available";
        case WeatherState::Cached: return "cached";
        case WeatherState::Offline: return "offline";
        case WeatherState::ProviderUnavailable: return "provider-unavailable";
        case WeatherState::RequestFailed: return "request-failed";
        case WeatherState::Unsupported: return "unsupported";
    }
    return "unknown";
}

std::string_view ToString(status::TemperatureUnit unit) noexcept {
    return unit == status::TemperatureUnit::Fahrenheit ? "fahrenheit" : "celsius";
}

}  // namespace aether::weather
