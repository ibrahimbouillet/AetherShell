// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// D78: the detailed forecast the weather window shows (current details, the next hours and seven
// days) and the current conditions of several places at once for its map. Pure value types; the
// Open-Meteo parsers live in OpenMeteo.h and the blocking client in ForecastClient.h.
//
// Local times: Open-Meteo answers with `timezone=auto` in the place's civil time. `localTime` and
// `date` carry that civil value in a sys_seconds/sys_days (no zone attached); `utcOffsetSeconds`
// turns it back into UTC when needed.
#pragma once

#include "status/StatusTypes.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace aether::weather {

struct ForecastCurrent {
    std::chrono::sys_seconds localTime{};       // civil time of the 15-minute value
    double temperature = 0.0;                   // in the forecast's unit
    std::optional<double> apparentTemperature;  // "feels like"
    std::optional<int> humidity;                // relative, percent
    int weatherCode = 0;                        // WMO 0..99
    bool isDay = true;
    std::optional<int> cloudCover;              // percent
    std::optional<double> pressure;             // hPa at mean sea level
    std::optional<double> windSpeed;            // km/h, or mph with Fahrenheit
    std::optional<int> windDirection;           // degrees the wind comes from, 0..360
    std::optional<double> windGusts;            // same unit as windSpeed
};

struct HourlyPoint {
    std::chrono::sys_seconds localTime{};
    double temperature = 0.0;
    int weatherCode = 0;
    bool isDay = true;
    std::optional<int> precipitationProbability;  // percent
};

struct DailyPoint {
    std::chrono::sys_days date{};  // civil date
    int weatherCode = 0;
    double maxTemperature = 0.0;
    double minTemperature = 0.0;
    std::optional<std::chrono::sys_seconds> sunrise;  // absent during polar day or night
    std::optional<std::chrono::sys_seconds> sunset;
    std::optional<int> precipitationProbability;      // percent, the day's maximum
    std::optional<double> uvIndex;                    // the day's maximum
};

inline constexpr std::size_t kForecastHours = 25;  // the current hour and the next 24
inline constexpr std::size_t kForecastDays = 7;

struct DetailedForecast {
    double latitude = 0.0;   // the model cell the provider used
    double longitude = 0.0;
    std::optional<double> elevation;  // metres; the provider corrects the temperature for it
    std::wstring timezone;            // IANA name such as "Europe/Paris"; may be empty
    std::int32_t utcOffsetSeconds = 0;
    status::TemperatureUnit unit = status::TemperatureUnit::Celsius;
    ForecastCurrent current;
    std::vector<HourlyPoint> hourly;  // from the current hour, at most kForecastHours
    std::vector<DailyPoint> daily;    // from today, at most kForecastDays
    std::chrono::system_clock::time_point fetchedAt{};

    // The forecast place's civil time now, from the device clock and the place's UTC offset.
    [[nodiscard]] std::chrono::sys_seconds LocalNow(std::chrono::system_clock::time_point now) const noexcept {
        return std::chrono::floor<std::chrono::seconds>(now) + std::chrono::seconds{utcOffsetSeconds};
    }
};

// A place the map asks the current conditions of.
struct GeoPoint {
    double latitude = 0.0;
    double longitude = 0.0;
};

// Current conditions of one requested place; `index` is its position in the request.
struct PointReading {
    std::size_t index = 0;
    double temperature = 0.0;
    int weatherCode = 0;
    bool isDay = true;
};

inline constexpr std::size_t kMaxForecastPoints = 24;  // one request; Open-Meteo counts each place

}  // namespace aether::weather
