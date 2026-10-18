// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Open-Meteo protocol helpers: URL construction, percent-encoding and strict parsing of the
// forecast and geocoding responses. Pure functions without I/O, so they are unit tested
// directly.
//
// API reference (official documentation verified 2026-10-18; no live API request):
//   https://open-meteo.com/en/docs                 forecast, "current" conditions, WMO codes
//   https://open-meteo.com/en/docs/geocoding-api   location search
//   https://open-meteo.com/en/terms                free for non-commercial use, < 10 000 calls/day
//   https://open-meteo.com/en/licence              CC BY 4.0, "Weather data by Open-Meteo.com"
// Errors are reported as HTTP 400 with {"error": true, "reason": "..."}. With timezone=auto,
// "current.time" is local time ("2026-10-17T17:00") and "utc_offset_seconds" gives the offset.
// A geocoding search without matches omits the "results" member.
#pragma once

#include "core/Http.h"
#include "core/Result.h"
#include "status/StatusTypes.h"
#include "weather/WeatherTypes.h"

#include <chrono>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace aether::weather::openmeteo {

inline constexpr std::string_view kForecastEndpoint = "https://api.open-meteo.com/v1/forecast";
inline constexpr std::string_view kGeocodingEndpoint = "https://geocoding-api.open-meteo.com/v1/search";

inline constexpr std::chrono::milliseconds kConnectTimeout{10'000};
inline constexpr std::chrono::milliseconds kReceiveTimeout{15'000};
inline constexpr std::size_t kMaxResponseBytes = 256u * 1024u;
inline constexpr std::size_t kMaxJsonDepth = 16;

// Coordinates are sent with four decimals (about 11 m), which is finer than the model grid.
inline constexpr int kCoordinateDecimals = 4;

inline constexpr std::size_t kMaxGeocodingResults = 5;
inline constexpr std::size_t kMaxQueryBytes = 200;  // UTF-8 bytes after trimming

// Plausible near-surface air temperatures (records: -89.2 °C, +56.7 °C) with a margin.
inline constexpr double kMinCelsius = -100.0;
inline constexpr double kMaxCelsius = 65.0;
inline constexpr double kMinFahrenheit = -148.0;
inline constexpr double kMaxFahrenheit = 149.0;

// Fixed-point text with kCoordinateDecimals decimals and the invariant '.' separator, independent
// of the user locale. Negative zero is normalized ("-0.0000" -> "0.0000").
[[nodiscard]] std::string FormatCoordinate(double value);

// Cache/location identity: "<lat>,<lon>" formatted with FormatCoordinate().
[[nodiscard]] std::string LocationKey(double latitude, double longitude);

// RFC 3986 percent-encoding of UTF-8 bytes; only unreserved characters (A-Z a-z 0-9 - . _ ~) are
// left as is, everything else becomes %XX (upper-case hex).
[[nodiscard]] std::string PercentEncode(std::string_view utf8);

// Open-Meteo expects a lower-case ISO 639 language code: "fr-FR" -> "fr", "pt_BR" -> "pt".
// Anything that is not 2-3 ASCII letters before the first '-' or '_' becomes "en".
[[nodiscard]] std::string NormalizeLanguage(std::wstring_view languageTag);

[[nodiscard]] std::string BuildForecastUrl(double latitude, double longitude, status::TemperatureUnit unit);
[[nodiscard]] std::string BuildGeocodingUrl(std::string_view queryUtf8, std::string_view language,
                                            std::size_t count);

// GET request with the module's timeouts, response size limit, no redirects and an Accept header.
[[nodiscard]] net::HttpRequest MakeRequest(std::string url);

[[nodiscard]] bool IsPlausibleTemperature(double value, status::TemperatureUnit unit) noexcept;

// Current conditions extracted from a forecast response.
struct CurrentConditions {
    double temperature = 0.0;
    int weatherCode = 0;
    bool isDay = true;
    std::chrono::system_clock::time_point observedAt{};  // UTC
};

// Reason of a provider error object ({"error": true, "reason": "..."}), sanitized and truncated;
// nullopt when `body` is not such an object.
[[nodiscard]] std::optional<std::string> ProviderErrorReason(std::string_view body);

// Strict parse of a forecast response requested with BuildForecastUrl(). Fails with
//   Integrity  - body larger than kMaxResponseBytes,
//   Parse      - not JSON, wrong member types or missing members,
//   Validation - temperature implausible for `unit`, unit label mismatch, weather_code outside 0..99,
//                is_day not 0/1, invalid time or UTC offset,
//   Http       - provider error object.
[[nodiscard]] Result<CurrentConditions> ParseForecast(std::string_view body, status::TemperatureUnit unit);

// Parse of a geocoding response. Entries with missing or invalid name/coordinates are skipped; at
// most `maxResults` candidates are returned. A response without "results" yields an empty list.
[[nodiscard]] Result<std::vector<LocationCandidate>> ParseGeocoding(std::string_view body, std::size_t maxResults);

// "Retry-After" as delta-seconds (0..86400); the HTTP-date form is not supported.
[[nodiscard]] std::optional<std::chrono::seconds> ParseRetryAfter(const net::HttpResponse& response);

}  // namespace aether::weather::openmeteo
