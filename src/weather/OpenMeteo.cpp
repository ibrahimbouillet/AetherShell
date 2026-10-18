// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "weather/OpenMeteo.h"

#include "core/Json.h"
#include "core/Strings.h"

#include <charconv>
#include <cmath>
#include <cstdint>

namespace aether::weather::openmeteo {
namespace {

using std::chrono::system_clock;

constexpr std::int64_t kMaxUtcOffsetSeconds = 18 * 3600;
constexpr std::size_t kMaxReasonLength = 200;
constexpr std::size_t kMaxDisplayTextLength = 200;

bool IsAsciiAlpha(wchar_t c) noexcept { return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z'); }

bool IsUnreserved(unsigned char c) noexcept {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.' ||
           c == '_' || c == '~';
}

json::ParseOptions JsonOptions() {
    json::ParseOptions options;
    options.maxInputBytes = kMaxResponseBytes;
    options.maxDepth = kMaxJsonDepth;
    return options;
}

Result<json::Value> ParseBody(std::string_view body) {
    if (body.size() > kMaxResponseBytes) {
        return MakeError(ErrorCode::Integrity, "response exceeds " + std::to_string(kMaxResponseBytes) + " bytes");
    }
    auto parsed = json::Parse(body, JsonOptions());
    if (!parsed) return MakeError(ErrorCode::Parse, "response is not valid JSON: " + parsed.error().message);
    if (!parsed.value().IsObject()) return MakeError(ErrorCode::Parse, "response is not a JSON object");
    return std::move(parsed).value();
}

std::optional<std::string> ErrorReasonFromValue(const json::Value& root) {
    const auto* flag = root.Find("error");
    if (!flag || flag->AsBool() != true) return std::nullopt;
    const auto* reason = root.Find("reason");
    if (!reason || !reason->AsString() || reason->AsString()->empty()) return std::string("unspecified error");
    return text::SanitizeForLog(*reason->AsString(), kMaxReasonLength);
}

// Parses "YYYY-MM-DDTHH:MM" or "YYYY-MM-DDTHH:MM:SS" as a civil time without zone.
std::optional<std::chrono::sys_seconds> ParseCivilTime(std::string_view text) {
    if (text.size() != 16 && text.size() != 19) return std::nullopt;
    auto number = [&](std::size_t position, std::size_t count) -> std::optional<int> {
        int value = 0;
        for (std::size_t i = position; i < position + count; ++i) {
            if (text[i] < '0' || text[i] > '9') return std::nullopt;
            value = value * 10 + (text[i] - '0');
        }
        return value;
    };
    if (text[4] != '-' || text[7] != '-' || text[10] != 'T' || text[13] != ':') return std::nullopt;
    if (text.size() == 19 && text[16] != ':') return std::nullopt;
    const auto year = number(0, 4);
    const auto month = number(5, 2);
    const auto day = number(8, 2);
    const auto hour = number(11, 2);
    const auto minute = number(14, 2);
    const auto second = text.size() == 19 ? number(17, 2) : std::optional<int>(0);
    if (!year || !month || !day || !hour || !minute || !second) return std::nullopt;
    if (*hour > 23 || *minute > 59 || *second > 59) return std::nullopt;
    const std::chrono::year_month_day date{std::chrono::year{*year},
                                           std::chrono::month{static_cast<unsigned>(*month)},
                                           std::chrono::day{static_cast<unsigned>(*day)}};
    if (!date.ok()) return std::nullopt;
    return std::chrono::sys_days{date} + std::chrono::hours{*hour} + std::chrono::minutes{*minute} +
           std::chrono::seconds{*second};
}

// Converts untrusted UTF-8 display text to UTF-16, replacing control characters and capping length.
std::wstring CleanDisplayText(std::string_view utf8) {
    std::wstring wide = text::Widen(utf8);
    for (auto& c : wide) {
        if (c < 0x20 || c == 0x7F) c = L' ';
    }
    if (wide.size() > kMaxDisplayTextLength) wide.resize(kMaxDisplayTextLength);
    return std::wstring(text::Trim(std::wstring_view(wide)));
}

}  // namespace

std::string FormatCoordinate(double value) {
    char buffer[64];
    const auto result =
        std::to_chars(buffer, buffer + sizeof(buffer), value, std::chars_format::fixed, kCoordinateDecimals);
    if (result.ec != std::errc{}) return "0.0000";  // unreachable for validated coordinates
    std::string text(buffer, result.ptr);
    if (!text.empty() && text.front() == '-' && text.find_first_not_of("-0.") == std::string::npos) {
        text.erase(0, 1);  // "-0.0000" -> "0.0000"
    }
    return text;
}

std::string LocationKey(double latitude, double longitude) {
    return FormatCoordinate(latitude) + "," + FormatCoordinate(longitude);
}

std::string PercentEncode(std::string_view utf8) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string encoded;
    encoded.reserve(utf8.size() * 3);
    for (const char ch : utf8) {
        const auto c = static_cast<unsigned char>(ch);
        if (IsUnreserved(c)) {
            encoded.push_back(ch);
        } else {
            encoded.push_back('%');
            encoded.push_back(kHex[c >> 4]);
            encoded.push_back(kHex[c & 0x0F]);
        }
    }
    return encoded;
}

std::string NormalizeLanguage(std::wstring_view languageTag) {
    const auto trimmed = text::Trim(languageTag);
    const auto end = trimmed.find_first_of(L"-_");
    const auto primary = trimmed.substr(0, end);
    if (primary.size() < 2 || primary.size() > 3) return "en";
    std::string language;
    for (const wchar_t c : primary) {
        if (!IsAsciiAlpha(c)) return "en";
        language.push_back(static_cast<char>(c | 0x20));  // ASCII lower case
    }
    return language;
}

std::string BuildForecastUrl(double latitude, double longitude, status::TemperatureUnit unit) {
    std::string url(kForecastEndpoint);
    url += "?latitude=";
    url += FormatCoordinate(latitude);
    url += "&longitude=";
    url += FormatCoordinate(longitude);
    url += "&current=temperature_2m,weather_code,is_day";
    url += "&temperature_unit=";
    url += ToString(unit);
    url += "&timezone=auto";
    return url;
}

std::string BuildGeocodingUrl(std::string_view queryUtf8, std::string_view language, std::size_t count) {
    std::string url(kGeocodingEndpoint);
    url += "?name=";
    url += PercentEncode(queryUtf8);
    url += "&count=";
    url += std::to_string(count);
    url += "&language=";
    url += PercentEncode(language);
    url += "&format=json";
    return url;
}

net::HttpRequest MakeRequest(std::string url) {
    net::HttpRequest request;
    request.method = "GET";
    request.url = std::move(url);
    request.headers.push_back({"Accept", "application/json"});
    request.connectTimeout = kConnectTimeout;
    request.receiveTimeout = kReceiveTimeout;
    request.maxResponseBytes = kMaxResponseBytes;
    request.allowedRedirectHosts.clear();  // Open-Meteo answers directly; never follow redirects.
    request.maxRedirects = 0;
    return request;
}

bool IsPlausibleTemperature(double value, status::TemperatureUnit unit) noexcept {
    if (!std::isfinite(value)) return false;
    if (unit == status::TemperatureUnit::Fahrenheit) return value >= kMinFahrenheit && value <= kMaxFahrenheit;
    return value >= kMinCelsius && value <= kMaxCelsius;
}

std::optional<std::string> ProviderErrorReason(std::string_view body) {
    if (body.size() > kMaxResponseBytes) return std::nullopt;
    auto parsed = json::Parse(body, JsonOptions());
    if (!parsed || !parsed.value().IsObject()) return std::nullopt;
    return ErrorReasonFromValue(parsed.value());
}

Result<CurrentConditions> ParseForecast(std::string_view body, status::TemperatureUnit unit) {
    auto parsed = ParseBody(body);
    if (!parsed) return parsed.error();
    const json::Value& root = parsed.value();
    if (auto reason = ErrorReasonFromValue(root)) {
        return MakeError(ErrorCode::Http, "provider error: " + *reason);
    }

    const auto* current = root.Find("current");
    if (!current || !current->IsObject()) return MakeError(ErrorCode::Parse, "missing 'current' object");

    const auto* timeValue = current->Find("time");
    if (!timeValue || !timeValue->AsString()) return MakeError(ErrorCode::Parse, "'current.time' is not a string");
    const auto localTime = ParseCivilTime(*timeValue->AsString());
    if (!localTime) return MakeError(ErrorCode::Validation, "'current.time' is not an ISO 8601 local time");

    const auto* offsetValue = root.Find("utc_offset_seconds");
    const auto offset = offsetValue ? offsetValue->AsInt64() : std::nullopt;
    if (!offset) return MakeError(ErrorCode::Parse, "'utc_offset_seconds' is missing or not an integer");
    if (*offset < -kMaxUtcOffsetSeconds || *offset > kMaxUtcOffsetSeconds) {
        return MakeError(ErrorCode::Validation, "'utc_offset_seconds' is out of range");
    }

    const auto* temperatureValue = current->Find("temperature_2m");
    const auto temperature = temperatureValue ? temperatureValue->AsDouble() : std::nullopt;
    if (!temperature) return MakeError(ErrorCode::Parse, "'current.temperature_2m' is missing or not a number");
    if (!IsPlausibleTemperature(*temperature, unit)) {
        return MakeError(ErrorCode::Validation, "temperature is outside the plausible range for the unit");
    }

    // The unit label is metadata; when present it must agree with the requested unit.
    if (const auto* units = root.Find("current_units")) {
        if (!units->IsObject()) return MakeError(ErrorCode::Parse, "'current_units' is not an object");
        if (const auto* label = units->Find("temperature_2m")) {
            const auto* labelText = label->AsString();
            const std::string_view expected =
                unit == status::TemperatureUnit::Fahrenheit ? "\xC2\xB0" "F" : "\xC2\xB0" "C";
            if (!labelText || *labelText != expected) {
                return MakeError(ErrorCode::Validation, "temperature unit does not match the request");
            }
        }
    }

    const auto* codeValue = current->Find("weather_code");
    const auto code = codeValue ? codeValue->AsInt64() : std::nullopt;
    if (!code) return MakeError(ErrorCode::Parse, "'current.weather_code' is missing or not an integer");
    if (!IsValidWeatherCode(*code)) return MakeError(ErrorCode::Validation, "'current.weather_code' is not 0..99");

    const auto* isDayValue = current->Find("is_day");
    std::optional<bool> isDay;
    if (isDayValue) {
        if (const auto flag = isDayValue->AsBool()) {
            isDay = *flag;
        } else if (const auto number = isDayValue->AsInt64(); number && (*number == 0 || *number == 1)) {
            isDay = *number == 1;
        }
    }
    if (!isDay) return MakeError(ErrorCode::Validation, "'current.is_day' is missing or not 0/1");

    CurrentConditions conditions;
    conditions.temperature = *temperature;
    conditions.weatherCode = static_cast<int>(*code);
    conditions.isDay = *isDay;
    conditions.observedAt = system_clock::time_point{*localTime - std::chrono::seconds{*offset}};
    return conditions;
}

Result<std::vector<LocationCandidate>> ParseGeocoding(std::string_view body, std::size_t maxResults) {
    auto parsed = ParseBody(body);
    if (!parsed) return parsed.error();
    const json::Value& root = parsed.value();
    if (auto reason = ErrorReasonFromValue(root)) {
        return MakeError(ErrorCode::Http, "provider error: " + *reason);
    }

    std::vector<LocationCandidate> candidates;
    const auto* results = root.Find("results");
    if (!results) return candidates;  // documented behaviour when nothing matches
    const auto* items = results->AsArray();
    if (!items) return MakeError(ErrorCode::Parse, "'results' is not an array");

    for (const auto& item : *items) {
        if (candidates.size() >= maxResults) break;
        if (!item.IsObject()) continue;
        const auto* name = item.Find("name");
        const auto* latitude = item.Find("latitude");
        const auto* longitude = item.Find("longitude");
        if (!name || !name->AsString() || !latitude || !longitude) continue;
        const auto lat = latitude->AsDouble();
        const auto lon = longitude->AsDouble();
        if (!lat || !lon || *lat < -90.0 || *lat > 90.0 || *lon < -180.0 || *lon > 180.0) continue;

        LocationCandidate candidate;
        candidate.name = CleanDisplayText(*name->AsString());
        if (candidate.name.empty()) continue;
        if (const auto* admin1 = item.Find("admin1"); admin1 && admin1->AsString()) {
            candidate.admin1 = CleanDisplayText(*admin1->AsString());
        }
        if (const auto* country = item.Find("country"); country && country->AsString()) {
            candidate.country = CleanDisplayText(*country->AsString());
        }
        candidate.latitude = *lat;
        candidate.longitude = *lon;
        candidates.push_back(std::move(candidate));
    }
    return candidates;
}

std::optional<std::chrono::seconds> ParseRetryAfter(const net::HttpResponse& response) {
    const auto* header = response.FindHeader("Retry-After");
    if (!header) return std::nullopt;
    const auto value = text::Trim(std::string_view(*header));
    if (value.empty() || value.size() > 6) return std::nullopt;
    std::int64_t seconds = 0;
    const auto result = std::from_chars(value.data(), value.data() + value.size(), seconds);
    if (result.ec != std::errc{} || result.ptr != value.data() + value.size()) return std::nullopt;
    if (seconds < 0 || seconds > 86'400) return std::nullopt;
    return std::chrono::seconds{seconds};
}

}  // namespace aether::weather::openmeteo
