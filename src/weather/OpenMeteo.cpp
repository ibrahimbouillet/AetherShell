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

// "YYYY-MM-DD" as a civil date.
std::optional<std::chrono::sys_days> ParseCivilDate(std::string_view text) {
    if (text.size() != 10) return std::nullopt;
    const auto full = ParseCivilTime(std::string(text) + "T00:00");
    if (!full) return std::nullopt;
    return std::chrono::floor<std::chrono::days>(*full);
}

// is_day as true/false or 0/1.
std::optional<bool> DayFlag(const json::Value* value) {
    if (!value) return std::nullopt;
    if (const auto flag = value->AsBool()) return *flag;
    if (const auto number = value->AsInt64(); number && (*number == 0 || *number == 1)) return *number == 1;
    return std::nullopt;
}

std::optional<double> FiniteNumber(const json::Value* value) {
    if (!value) return std::nullopt;
    const auto number = value->AsDouble();
    if (!number || !std::isfinite(*number)) return std::nullopt;
    return number;
}

std::optional<double> NumberIn(const json::Value* value, double low, double high) {
    const auto number = FiniteNumber(value);
    if (!number || *number < low || *number > high) return std::nullopt;
    return number;
}

std::optional<int> RoundedIn(const json::Value* value, double low, double high) {
    const auto number = NumberIn(value, low, high);
    if (!number) return std::nullopt;
    return static_cast<int>(std::lround(*number));
}

std::optional<int> WeatherCodeOf(const json::Value* value) {
    const auto code = value ? value->AsInt64() : std::nullopt;
    if (!code || !IsValidWeatherCode(*code)) return std::nullopt;
    return static_cast<int>(*code);
}

std::string_view UnitLabel(status::TemperatureUnit unit) noexcept {
    return unit == status::TemperatureUnit::Fahrenheit ? "\xC2\xB0" "F" : "\xC2\xB0" "C";
}

// The unit label is metadata; when present it must agree with the requested unit.
Result<void> CheckUnitLabel(const json::Value& root, std::string_view group, std::string_view member,
                            status::TemperatureUnit unit) {
    const auto* units = root.Find(group);
    if (!units) return Ok();
    if (!units->IsObject()) return MakeError(ErrorCode::Parse, "'" + std::string(group) + "' is not an object");
    if (const auto* label = units->Find(member)) {
        const auto* labelText = label->AsString();
        if (!labelText || *labelText != UnitLabel(unit)) {
            return MakeError(ErrorCode::Validation, "temperature unit does not match the request");
        }
    }
    return Ok();
}

// The members of a "current" object every request depends on.
struct CurrentCore {
    std::chrono::sys_seconds localTime{};
    std::int64_t offset = 0;
    double temperature = 0.0;
    int weatherCode = 0;
    bool isDay = true;
};

Result<CurrentCore> ParseCurrentCore(const json::Value& root, status::TemperatureUnit unit) {
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
    if (auto label = CheckUnitLabel(root, "current_units", "temperature_2m", unit); !label) return label.error();

    const auto* codeValue = current->Find("weather_code");
    const auto code = codeValue ? codeValue->AsInt64() : std::nullopt;
    if (!code) return MakeError(ErrorCode::Parse, "'current.weather_code' is missing or not an integer");
    if (!IsValidWeatherCode(*code)) return MakeError(ErrorCode::Validation, "'current.weather_code' is not 0..99");

    const auto isDay = DayFlag(current->Find("is_day"));
    if (!isDay) return MakeError(ErrorCode::Validation, "'current.is_day' is missing or not 0/1");

    CurrentCore core;
    core.localTime = *localTime;
    core.offset = *offset;
    core.temperature = *temperature;
    core.weatherCode = static_cast<int>(*code);
    core.isDay = *isDay;
    return core;
}

// Member `name` of a forecast block as an array; nullptr when absent or not an array.
const json::Array* Series(const json::Value& block, std::string_view name) {
    const auto* member = block.Find(name);
    return member ? member->AsArray() : nullptr;
}

const json::Value* At(const json::Array* series, std::size_t index) {
    return series && index < series->size() ? &(*series)[index] : nullptr;
}

std::string TemperatureUnitParameter(status::TemperatureUnit unit) {
    return std::string("&temperature_unit=") + std::string(ToString(unit));
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
    url += TemperatureUnitParameter(unit);
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
    // Open-Meteo's compressed stream makes WinHTTP's decoder abort mid-body (E_ABORT), so the weather never
    // loaded. The reply is under 1 KB: ask for it uncompressed.
    request.allowCompression = false;
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

    auto core = ParseCurrentCore(root, unit);
    if (!core) return core.error();

    CurrentConditions conditions;
    conditions.temperature = core->temperature;
    conditions.weatherCode = core->weatherCode;
    conditions.isDay = core->isDay;
    conditions.observedAt = system_clock::time_point{core->localTime - std::chrono::seconds{core->offset}};
    return conditions;
}

std::string BuildDetailedForecastUrl(double latitude, double longitude, status::TemperatureUnit unit) {
    std::string url(kForecastEndpoint);
    url += "?latitude=";
    url += FormatCoordinate(latitude);
    url += "&longitude=";
    url += FormatCoordinate(longitude);
    url += "&current=temperature_2m,apparent_temperature,relative_humidity_2m,weather_code,is_day,cloud_cover,"
           "pressure_msl,wind_speed_10m,wind_direction_10m,wind_gusts_10m";
    url += "&hourly=temperature_2m,weather_code,is_day,precipitation_probability";
    url += "&daily=weather_code,temperature_2m_max,temperature_2m_min,sunrise,sunset,precipitation_probability_max,"
           "uv_index_max";
    url += "&forecast_hours=" + std::to_string(kForecastHours);
    url += "&forecast_days=" + std::to_string(kForecastDays);
    url += TemperatureUnitParameter(unit);
    url += unit == status::TemperatureUnit::Fahrenheit ? "&wind_speed_unit=mph" : "&wind_speed_unit=kmh";
    url += "&timezone=auto";
    return url;
}

Result<DetailedForecast> ParseDetailedForecast(std::string_view body, status::TemperatureUnit unit) {
    auto parsed = ParseBody(body);
    if (!parsed) return parsed.error();
    const json::Value& root = parsed.value();
    if (auto reason = ErrorReasonFromValue(root)) {
        return MakeError(ErrorCode::Http, "provider error: " + *reason);
    }
    auto core = ParseCurrentCore(root, unit);
    if (!core) return core.error();
    if (auto label = CheckUnitLabel(root, "hourly_units", "temperature_2m", unit); !label) return label.error();
    if (auto label = CheckUnitLabel(root, "daily_units", "temperature_2m_max", unit); !label) return label.error();

    DetailedForecast forecast;
    forecast.unit = unit;
    forecast.utcOffsetSeconds = static_cast<std::int32_t>(core->offset);
    forecast.latitude = NumberIn(root.Find("latitude"), -90.0, 90.0).value_or(0.0);
    forecast.longitude = NumberIn(root.Find("longitude"), -180.0, 180.0).value_or(0.0);
    forecast.elevation = NumberIn(root.Find("elevation"), -500.0, 9000.0);
    if (const auto* zone = root.Find("timezone"); zone && zone->AsString()) {
        forecast.timezone = CleanDisplayText(*zone->AsString());
        if (forecast.timezone.size() > 64) forecast.timezone.clear();
    }

    const auto& current = *root.Find("current");
    auto& now = forecast.current;
    now.localTime = core->localTime;
    now.temperature = core->temperature;
    now.weatherCode = core->weatherCode;
    now.isDay = core->isDay;
    now.apparentTemperature = FiniteNumber(current.Find("apparent_temperature"));
    if (now.apparentTemperature && !IsPlausibleTemperature(*now.apparentTemperature, unit)) now.apparentTemperature.reset();
    now.humidity = RoundedIn(current.Find("relative_humidity_2m"), 0.0, 100.0);
    now.cloudCover = RoundedIn(current.Find("cloud_cover"), 0.0, 100.0);
    now.pressure = NumberIn(current.Find("pressure_msl"), 850.0, 1100.0);
    now.windSpeed = NumberIn(current.Find("wind_speed_10m"), 0.0, 500.0);
    now.windDirection = RoundedIn(current.Find("wind_direction_10m"), 0.0, 360.0);
    now.windGusts = NumberIn(current.Find("wind_gusts_10m"), 0.0, 600.0);

    const auto* hourly = root.Find("hourly");
    if (!hourly || !hourly->IsObject()) return MakeError(ErrorCode::Parse, "missing 'hourly' object");
    const auto* hourTimes = Series(*hourly, "time");
    if (!hourTimes) return MakeError(ErrorCode::Parse, "'hourly.time' is not an array");
    const auto* hourTemperatures = Series(*hourly, "temperature_2m");
    const auto* hourCodes = Series(*hourly, "weather_code");
    const auto* hourDays = Series(*hourly, "is_day");
    const auto* hourRain = Series(*hourly, "precipitation_probability");
    for (std::size_t i = 0; i < hourTimes->size() && forecast.hourly.size() < kForecastHours; ++i) {
        const auto* time = (*hourTimes)[i].AsString();
        const auto localTime = time ? ParseCivilTime(*time) : std::nullopt;
        const auto temperature = FiniteNumber(At(hourTemperatures, i));
        const auto code = WeatherCodeOf(At(hourCodes, i));
        if (!localTime || !temperature || !IsPlausibleTemperature(*temperature, unit) || !code) continue;
        HourlyPoint point;
        point.localTime = *localTime;
        point.temperature = *temperature;
        point.weatherCode = *code;
        point.isDay = DayFlag(At(hourDays, i)).value_or(true);
        point.precipitationProbability = RoundedIn(At(hourRain, i), 0.0, 100.0);
        forecast.hourly.push_back(point);
    }

    const auto* daily = root.Find("daily");
    if (!daily || !daily->IsObject()) return MakeError(ErrorCode::Parse, "missing 'daily' object");
    const auto* dayDates = Series(*daily, "time");
    if (!dayDates) return MakeError(ErrorCode::Parse, "'daily.time' is not an array");
    const auto* dayCodes = Series(*daily, "weather_code");
    const auto* dayHighs = Series(*daily, "temperature_2m_max");
    const auto* dayLows = Series(*daily, "temperature_2m_min");
    const auto* sunrises = Series(*daily, "sunrise");
    const auto* sunsets = Series(*daily, "sunset");
    const auto* dayRain = Series(*daily, "precipitation_probability_max");
    const auto* dayUv = Series(*daily, "uv_index_max");
    const auto civilTimeAt = [](const json::Array* series, std::size_t i) -> std::optional<std::chrono::sys_seconds> {
        const auto* value = At(series, i);
        const auto* text = value ? value->AsString() : nullptr;
        return text ? ParseCivilTime(*text) : std::nullopt;
    };
    for (std::size_t i = 0; i < dayDates->size() && forecast.daily.size() < kForecastDays; ++i) {
        const auto* dateText = (*dayDates)[i].AsString();
        const auto date = dateText ? ParseCivilDate(*dateText) : std::nullopt;
        const auto code = WeatherCodeOf(At(dayCodes, i));
        const auto high = FiniteNumber(At(dayHighs, i));
        const auto low = FiniteNumber(At(dayLows, i));
        if (!date || !code || !high || !low || !IsPlausibleTemperature(*high, unit) ||
            !IsPlausibleTemperature(*low, unit) || *low > *high) {
            continue;
        }
        DailyPoint day;
        day.date = *date;
        day.weatherCode = *code;
        day.maxTemperature = *high;
        day.minTemperature = *low;
        day.sunrise = civilTimeAt(sunrises, i);
        day.sunset = civilTimeAt(sunsets, i);
        if (day.sunrise && day.sunset && *day.sunset <= *day.sunrise) day.sunrise.reset(), day.sunset.reset();
        day.precipitationProbability = RoundedIn(At(dayRain, i), 0.0, 100.0);
        day.uvIndex = NumberIn(At(dayUv, i), 0.0, 25.0);
        forecast.daily.push_back(day);
    }
    if (forecast.daily.empty()) return MakeError(ErrorCode::Validation, "the forecast has no valid day");
    return forecast;
}

std::string BuildPointsUrl(const std::vector<GeoPoint>& points, status::TemperatureUnit unit) {
    std::string latitudes;
    std::string longitudes;
    const std::size_t count = points.size() < kMaxForecastPoints ? points.size() : kMaxForecastPoints;
    for (std::size_t i = 0; i < count; ++i) {
        if (i > 0) {
            latitudes += ',';
            longitudes += ',';
        }
        latitudes += FormatCoordinate(points[i].latitude);
        longitudes += FormatCoordinate(points[i].longitude);
    }
    std::string url(kForecastEndpoint);
    url += "?latitude=" + latitudes;
    url += "&longitude=" + longitudes;
    url += "&current=temperature_2m,weather_code,is_day";
    url += TemperatureUnitParameter(unit);
    return url;
}

Result<std::vector<PointReading>> ParsePoints(std::string_view body, status::TemperatureUnit unit,
                                              std::size_t requested) {
    if (body.size() > kMaxResponseBytes) {
        return MakeError(ErrorCode::Integrity, "response exceeds " + std::to_string(kMaxResponseBytes) + " bytes");
    }
    auto parsed = json::Parse(body, JsonOptions());
    if (!parsed) return MakeError(ErrorCode::Parse, "response is not valid JSON: " + parsed.error().message);
    const json::Value& root = parsed.value();
    std::vector<const json::Value*> places;
    if (const auto* items = root.AsArray()) {
        for (const auto& item : *items) places.push_back(&item);
    } else if (root.IsObject()) {
        if (auto reason = ErrorReasonFromValue(root)) return MakeError(ErrorCode::Http, "provider error: " + *reason);
        places.push_back(&root);
    } else {
        return MakeError(ErrorCode::Parse, "response is neither an object nor an array");
    }
    // The answer is matched to the request by position: any other count cannot be trusted.
    if (places.size() != requested) return MakeError(ErrorCode::Validation, "response does not match the requested places");

    std::vector<PointReading> readings;
    for (std::size_t i = 0; i < places.size(); ++i) {
        if (!places[i]->IsObject()) continue;
        const auto* current = places[i]->Find("current");
        if (!current || !current->IsObject()) continue;
        if (!CheckUnitLabel(*places[i], "current_units", "temperature_2m", unit)) continue;
        const auto temperature = FiniteNumber(current->Find("temperature_2m"));
        const auto code = WeatherCodeOf(current->Find("weather_code"));
        if (!temperature || !IsPlausibleTemperature(*temperature, unit) || !code) continue;
        PointReading reading;
        reading.index = i;
        reading.temperature = *temperature;
        reading.weatherCode = *code;
        reading.isDay = DayFlag(current->Find("is_day")).value_or(true);
        readings.push_back(reading);
    }
    return readings;
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
