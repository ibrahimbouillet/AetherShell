// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Value types shared by the weather provider, the Open-Meteo protocol helpers, the cache and
// the geocoding client. The snapshot contract itself (WeatherSnapshot, WeatherState) lives in
// status/StatusTypes.h; this module only includes that header and does not link aether_status.
#pragma once

#include "core/Result.h"
#include "status/StatusTypes.h"
#include "weather/WeatherConditions.h"

#include <chrono>
#include <optional>
#include <string>
#include <string_view>

namespace aether::weather {

// Attribution required by the Open-Meteo CC BY 4.0 licence (https://open-meteo.com/en/licence).
// Every Available or Cached snapshot carries it; the Top Bar shows it next to the data.
inline constexpr std::wstring_view kAttribution = L"Weather data by Open-Meteo.com";
inline constexpr std::wstring_view kAttributionUrl = L"https://open-meteo.com/";

inline constexpr std::chrono::minutes kDefaultRefreshInterval{30};
inline constexpr std::chrono::minutes kMinRefreshInterval{10};
inline constexpr std::chrono::hours kMaxRefreshInterval{24};

// User-facing weather settings, mapped by the application from settings::WeatherSettings.
struct WeatherConfig {
    bool enabled = false;               // false: no network traffic and no cache access at all
    std::optional<double> latitude;     // WGS84, -90..90
    std::optional<double> longitude;    // WGS84, -180..180
    std::wstring locationName;          // display name chosen by the user, e.g. "Paris, France"
    status::TemperatureUnit unit = status::TemperatureUnit::Celsius;
    std::chrono::minutes refreshInterval = kDefaultRefreshInterval;  // kMinRefreshInterval..kMaxRefreshInterval

    std::string locationUnavailableDetail; // fixed public-safe reason when automatic location cannot be read

    // True when both coordinates are present.
    [[nodiscard]] bool HasLocation() const noexcept { return latitude.has_value() && longitude.has_value(); }
};

// InvalidArgument when coordinates are non-finite or out of range, when only one coordinate is
// given, or when the refresh interval is outside [kMinRefreshInterval, kMaxRefreshInterval].
[[nodiscard]] Result<void> ValidateConfig(const WeatherConfig& config);

// One validated reading of current conditions, as fetched and as persisted in the cache.
struct WeatherReading {
    std::string locationKey;  // normalized coordinates, see openmeteo::LocationKey()
    status::TemperatureUnit unit = status::TemperatureUnit::Celsius;
    double temperature = 0.0;  // in `unit`, range checked
    int weatherCode = 0;       // WMO code 0..99
    bool isDay = true;
    std::chrono::system_clock::time_point observedAt{};  // provider observation time (UTC)
    std::chrono::system_clock::time_point fetchedAt{};   // when Aether Shell received it (whole seconds)

    bool operator==(const WeatherReading&) const = default;
};

// Snapshot plus the presentation hints the Top Bar renderer needs. WeatherSnapshot has no
// day/night field, so it travels here (see docs/modules/weather.md).
struct WeatherReport {
    status::WeatherSnapshot snapshot;
    WeatherGlyph glyph = WeatherGlyph::Unknown;  // Unknown whenever no condition code is shown
    std::optional<bool> isDay;                   // only set together with shown data
};

[[nodiscard]] bool operator==(const WeatherReport& a, const WeatherReport& b);

// A place returned by the geocoding search, for the Settings UI.
struct LocationCandidate {
    std::wstring name;     // e.g. "Zurich" (localized when the provider has a translation)
    std::wstring admin1;   // first-level administrative area; may be empty
    std::wstring country;  // may be empty
    double latitude = 0.0;
    double longitude = 0.0;
};

// "Zurich, Canton of Zurich, Switzerland": non-empty parts, repeated parts skipped.
[[nodiscard]] std::wstring FormatLocationLabel(const LocationCandidate& candidate);

// Stable identifiers for logs ("available", "cached", ...).
[[nodiscard]] std::string_view ToString(status::WeatherState state) noexcept;
[[nodiscard]] std::string_view ToString(status::TemperatureUnit unit) noexcept;

}  // namespace aether::weather
