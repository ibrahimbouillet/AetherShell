// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Pure helpers of the weather module: WMO code mapping, Open-Meteo URL construction and strict
// response parsing, cache serialization and configuration validation.
#include "AetherTest.h"
#include "WeatherTestSupport.h"

#include "core/FileSystem.h"
#include "weather/OpenMeteo.h"
#include "weather/WeatherCache.h"
#include "weather/WeatherConditions.h"
#include "weather/WeatherTypes.h"

#include <clocale>
#include <cmath>
#include <limits>
#include <locale>
#include <string>

using namespace aether;
using namespace aether::weather;
using aether::status::TemperatureUnit;
using namespace std::chrono_literals;

namespace {

std::string ForecastWith(std::string_view current, std::string_view extra = {}) {
    return std::string(R"({"utc_offset_seconds":0,)") + std::string(extra) + R"("current":)" + std::string(current) +
           "}";
}

WeatherReading SampleReading() {
    WeatherReading reading;
    reading.locationKey = "48.8566,2.3522";
    reading.unit = TemperatureUnit::Celsius;
    reading.temperature = 15.5;
    reading.weatherCode = 61;
    reading.isDay = false;
    reading.observedAt = std::chrono::system_clock::time_point{std::chrono::seconds{1'790'000'000}};
    reading.fetchedAt = std::chrono::system_clock::time_point{std::chrono::seconds{1'790'000'120}};
    return reading;
}

}  // namespace

// ---------------------------------------------------------------------------------------------
// WMO codes
// ---------------------------------------------------------------------------------------------

AETHER_TEST("weather conditions: documented WMO codes map to text and glyphs") {
    struct Expectation {
        int code;
        std::wstring_view text;
        WeatherGlyph glyph;
    };
    const Expectation expectations[] = {
        {0, L"Clear sky", WeatherGlyph::Clear},         {1, L"Mainly clear", WeatherGlyph::Clear},
        {2, L"Partly cloudy", WeatherGlyph::PartlyCloudy}, {3, L"Overcast", WeatherGlyph::Cloudy},
        {45, L"Fog", WeatherGlyph::Fog},                {48, L"Fog", WeatherGlyph::Fog},
        {51, L"Drizzle", WeatherGlyph::Drizzle},        {57, L"Drizzle", WeatherGlyph::Drizzle},
        {61, L"Rain", WeatherGlyph::Rain},              {67, L"Rain", WeatherGlyph::Rain},
        {71, L"Snow", WeatherGlyph::Snow},              {77, L"Snow", WeatherGlyph::Snow},
        {80, L"Rain showers", WeatherGlyph::Rain},      {82, L"Rain showers", WeatherGlyph::Rain},
        {85, L"Snow showers", WeatherGlyph::Snow},      {86, L"Snow showers", WeatherGlyph::Snow},
        {95, L"Thunderstorm", WeatherGlyph::Thunder},   {96, L"Thunderstorm", WeatherGlyph::Thunder},
        {99, L"Thunderstorm", WeatherGlyph::Thunder},
    };
    for (const auto& expected : expectations) {
        CHECK_EQ(ConditionText(expected.code), expected.text);
        CHECK_EQ(GlyphForCode(expected.code), expected.glyph);
    }
}

AETHER_TEST("weather conditions: undocumented and invalid codes stay unknown") {
    for (const int code : {4, 10, 44, 46, 50, 58, 60, 68, 70, 78, 79, 83, 84, 87, 94, -1, 100}) {
        CHECK(ConditionText(code).empty());
        CHECK_EQ(GlyphForCode(code), WeatherGlyph::Unknown);
    }
    CHECK_EQ(GlyphForCode(std::nullopt), WeatherGlyph::Unknown);
    CHECK(IsValidWeatherCode(0));
    CHECK(IsValidWeatherCode(99));
    CHECK_FALSE(IsValidWeatherCode(-1));
    CHECK_FALSE(IsValidWeatherCode(100));
    CHECK_EQ(ToString(WeatherGlyph::PartlyCloudy), std::string_view("partly-cloudy"));
}

// ---------------------------------------------------------------------------------------------
// URLs and encoding
// ---------------------------------------------------------------------------------------------

AETHER_TEST("open-meteo: coordinates use fixed decimals and an invariant separator") {
    CHECK_EQ(openmeteo::FormatCoordinate(48.85661), std::string("48.8566"));
    CHECK_EQ(openmeteo::FormatCoordinate(-122.4194), std::string("-122.4194"));
    CHECK_EQ(openmeteo::FormatCoordinate(0.0), std::string("0.0000"));
    CHECK_EQ(openmeteo::FormatCoordinate(-0.00001), std::string("0.0000"));
    CHECK_EQ(openmeteo::FormatCoordinate(90.0), std::string("90.0000"));
    CHECK_EQ(openmeteo::FormatCoordinate(-180.0), std::string("-180.0000"));

    // A user locale with a decimal comma must not leak into URLs.
    const std::string previousC = std::setlocale(LC_ALL, nullptr);
    const std::locale previousCpp = std::locale();
    const bool switched = std::setlocale(LC_ALL, "fr-FR") != nullptr;
    try {
        if (switched) std::locale::global(std::locale("fr-FR"));
    } catch (...) {
    }
    if (!switched) TEST_NOTE("fr-FR locale unavailable; separator checked with the default locale only");
    CHECK_EQ(openmeteo::FormatCoordinate(2.3522), std::string("2.3522"));
    CHECK_EQ(openmeteo::LocationKey(48.8566, 2.3522), std::string("48.8566,2.3522"));
    std::locale::global(previousCpp);
    std::setlocale(LC_ALL, previousC.c_str());
}

AETHER_TEST("open-meteo: forecast URL matches the documented request") {
    CHECK_EQ(openmeteo::BuildForecastUrl(48.8566, 2.3522, TemperatureUnit::Celsius),
             std::string("https://api.open-meteo.com/v1/forecast?latitude=48.8566&longitude=2.3522"
                         "&current=temperature_2m,weather_code,is_day&temperature_unit=celsius&timezone=auto"));
    CHECK_EQ(openmeteo::BuildForecastUrl(-33.86788, 151.20732, TemperatureUnit::Fahrenheit),
             std::string("https://api.open-meteo.com/v1/forecast?latitude=-33.8679&longitude=151.2073"
                         "&current=temperature_2m,weather_code,is_day&temperature_unit=fahrenheit&timezone=auto"));
}

AETHER_TEST("open-meteo: percent-encoding keeps only unreserved characters") {
    CHECK_EQ(openmeteo::PercentEncode("AZaz09-._~"), std::string("AZaz09-._~"));
    CHECK_EQ(openmeteo::PercentEncode("New York"), std::string("New%20York"));
    CHECK_EQ(openmeteo::PercentEncode("a+b&c=d?e#f/g%h"), std::string("a%2Bb%26c%3Dd%3Fe%23f%2Fg%25h"));
    CHECK_EQ(openmeteo::PercentEncode("Z\xC3\xBCrich"), std::string("Z%C3%BCrich"));
    CHECK_EQ(openmeteo::PercentEncode("\xE6\x9D\xB1\xE4\xBA\xAC"), std::string("%E6%9D%B1%E4%BA%AC"));
    CHECK_EQ(openmeteo::PercentEncode(std::string_view("a\0b", 3)), std::string("a%00b"));
}

AETHER_TEST("open-meteo: language tags are reduced to a lower-case ISO 639 code") {
    CHECK_EQ(openmeteo::NormalizeLanguage(L"fr-FR"), std::string("fr"));
    CHECK_EQ(openmeteo::NormalizeLanguage(L"pt_BR"), std::string("pt"));
    CHECK_EQ(openmeteo::NormalizeLanguage(L"DE"), std::string("de"));
    CHECK_EQ(openmeteo::NormalizeLanguage(L"zh-Hans-CN"), std::string("zh"));
    CHECK_EQ(openmeteo::NormalizeLanguage(L"gsw-CH"), std::string("gsw"));
    CHECK_EQ(openmeteo::NormalizeLanguage(L""), std::string("en"));
    CHECK_EQ(openmeteo::NormalizeLanguage(L"x"), std::string("en"));
    CHECK_EQ(openmeteo::NormalizeLanguage(L"f1-FR"), std::string("en"));
    CHECK_EQ(openmeteo::NormalizeLanguage(L"ét"), std::string("en"));
    CHECK_EQ(openmeteo::NormalizeLanguage(L"en&x=1"), std::string("en"));
}

AETHER_TEST("open-meteo: geocoding URL encodes the query as UTF-8") {
    CHECK_EQ(openmeteo::BuildGeocodingUrl("S\xC3\xA3o Paulo", "pt", 5),
             std::string("https://geocoding-api.open-meteo.com/v1/search?name=S%C3%A3o%20Paulo&count=5&language=pt"
                         "&format=json"));
}

AETHER_TEST("open-meteo: requests carry the module's timeouts and limits") {
    const auto request = openmeteo::MakeRequest("https://api.open-meteo.com/v1/forecast");
    CHECK_EQ(request.method, std::string("GET"));
    CHECK(request.connectTimeout == 10s);
    CHECK(request.receiveTimeout == 15s);
    CHECK_EQ(request.maxResponseBytes, static_cast<std::uint64_t>(256 * 1024));
    CHECK(request.allowedRedirectHosts.empty());
    CHECK_FALSE(request.allowInsecureLoopback);
}

// ---------------------------------------------------------------------------------------------
// Forecast parsing
// ---------------------------------------------------------------------------------------------

AETHER_TEST("open-meteo: a forecast response is parsed with its UTC observation time") {
    auto parsed = openmeteo::ParseForecast(testing::ForecastBody(15.3, 2, 1), TemperatureUnit::Celsius);
    REQUIRE(parsed.ok());
    CHECK_NEAR(parsed.value().temperature, 15.3, 1e-9);
    CHECK_EQ(parsed.value().weatherCode, 2);
    CHECK(parsed.value().isDay);
    CHECK(parsed.value().observedAt == testing::ForecastObservedAt());

    auto night = openmeteo::ParseForecast(testing::ForecastBody(59.5, 3, 0, "F"), TemperatureUnit::Fahrenheit);
    REQUIRE(night.ok());
    CHECK_NEAR(night.value().temperature, 59.5, 1e-9);
    CHECK_FALSE(night.value().isDay);

    // Seconds in the timestamp, boolean is_day and an integral double code are tolerated.
    auto tolerant = openmeteo::ParseForecast(
        ForecastWith(R"({"time":"2026-10-17T15:00:30","temperature_2m":-3,"weather_code":71.0,"is_day":true})"),
        TemperatureUnit::Celsius);
    REQUIRE(tolerant.ok());
    CHECK_EQ(tolerant.value().weatherCode, 71);
    CHECK(tolerant.value().observedAt == testing::ForecastObservedAt() + 30s);
}

AETHER_TEST("open-meteo: malformed forecast responses are rejected") {
    const auto parse = [](std::string_view body, TemperatureUnit unit = TemperatureUnit::Celsius) {
        auto result = openmeteo::ParseForecast(body, unit);
        return result.ok() ? ErrorCode::Unknown : result.error().code;
    };
    CHECK_EQ(parse(""), ErrorCode::Parse);
    CHECK_EQ(parse("<html>Bad gateway</html>"), ErrorCode::Parse);
    CHECK_EQ(parse(R"({"current":)"), ErrorCode::Parse);
    CHECK_EQ(parse("[1,2,3]"), ErrorCode::Parse);
    CHECK_EQ(parse(R"({"utc_offset_seconds":0})"), ErrorCode::Parse);
    CHECK_EQ(parse(R"({"utc_offset_seconds":0,"current":[]})"), ErrorCode::Parse);
    CHECK_EQ(parse(ForecastWith(R"({"time":"2026-10-17T15:00","temperature_2m":"15","weather_code":2,"is_day":1})")),
             ErrorCode::Parse);
    CHECK_EQ(parse(ForecastWith(R"({"time":"2026-10-17T15:00","temperature_2m":null,"weather_code":2,"is_day":1})")),
             ErrorCode::Parse);
    CHECK_EQ(parse(ForecastWith(R"({"time":"2026-10-17T15:00","weather_code":2,"is_day":1})")), ErrorCode::Parse);
    CHECK_EQ(parse(ForecastWith(R"({"time":"2026-10-17T15:00","temperature_2m":15,"weather_code":2.5,"is_day":1})")),
             ErrorCode::Parse);
    CHECK_EQ(parse(ForecastWith(R"({"time":17000,"temperature_2m":15,"weather_code":2,"is_day":1})")),
             ErrorCode::Parse);
    CHECK_EQ(parse(R"({"current":{"time":"2026-10-17T15:00","temperature_2m":15,"weather_code":2,"is_day":1}})"),
             ErrorCode::Parse);  // utc_offset_seconds missing
    // Duplicate keys are rejected by the strict JSON parser.
    CHECK_EQ(parse(ForecastWith(
                 R"({"time":"2026-10-17T15:00","temperature_2m":15,"temperature_2m":16,"weather_code":2,"is_day":1})")),
             ErrorCode::Parse);
}

AETHER_TEST("open-meteo: out-of-range forecast values are rejected") {
    const auto parse = [](std::string_view current, TemperatureUnit unit = TemperatureUnit::Celsius,
                          std::string_view extra = {}) {
        auto result = openmeteo::ParseForecast(ForecastWith(current, extra), unit);
        return result.ok() ? ErrorCode::Unknown : result.error().code;
    };
    CHECK_EQ(parse(R"({"time":"2026-10-17T15:00","temperature_2m":70,"weather_code":2,"is_day":1})"),
             ErrorCode::Validation);
    CHECK_EQ(parse(R"({"time":"2026-10-17T15:00","temperature_2m":-101,"weather_code":2,"is_day":1})"),
             ErrorCode::Validation);
    CHECK_EQ(parse(R"({"time":"2026-10-17T15:00","temperature_2m":150,"weather_code":2,"is_day":1})",
                   TemperatureUnit::Fahrenheit),
             ErrorCode::Validation);
    CHECK_EQ(parse(R"({"time":"2026-10-17T15:00","temperature_2m":1e300,"weather_code":2,"is_day":1})"),
             ErrorCode::Validation);
    CHECK_EQ(parse(R"({"time":"2026-10-17T15:00","temperature_2m":15,"weather_code":100,"is_day":1})"),
             ErrorCode::Validation);
    CHECK_EQ(parse(R"({"time":"2026-10-17T15:00","temperature_2m":15,"weather_code":-1,"is_day":1})"),
             ErrorCode::Validation);
    CHECK_EQ(parse(R"({"time":"2026-10-17T15:00","temperature_2m":15,"weather_code":2,"is_day":2})"),
             ErrorCode::Validation);
    CHECK_EQ(parse(R"({"time":"2026-10-17T15:00","temperature_2m":15,"weather_code":2})"), ErrorCode::Validation);
    CHECK_EQ(parse(R"({"time":"2026-13-17T15:00","temperature_2m":15,"weather_code":2,"is_day":1})"),
             ErrorCode::Validation);
    CHECK_EQ(parse(R"({"time":"2026-02-30T15:00","temperature_2m":15,"weather_code":2,"is_day":1})"),
             ErrorCode::Validation);
    CHECK_EQ(parse(R"({"time":"2026-10-17 15:00","temperature_2m":15,"weather_code":2,"is_day":1})"),
             ErrorCode::Validation);
    CHECK_EQ(parse(R"({"time":"2026-10-17T24:00","temperature_2m":15,"weather_code":2,"is_day":1})"),
             ErrorCode::Validation);

    // A UTC offset beyond +-18 h is not a real time zone.
    auto badOffset = openmeteo::ParseForecast(
        R"({"utc_offset_seconds":90000,"current":{"time":"2026-10-17T15:00","temperature_2m":15,"weather_code":2,"is_day":1}})",
        TemperatureUnit::Celsius);
    REQUIRE_FALSE(badOffset.ok());
    CHECK_EQ(badOffset.error().code, ErrorCode::Validation);

    // The unit label, when present, must agree with the requested unit.
    auto mismatch = openmeteo::ParseForecast(testing::ForecastBody(15.0, 2, 1, "F"), TemperatureUnit::Celsius);
    REQUIRE_FALSE(mismatch.ok());
    CHECK_EQ(mismatch.error().code, ErrorCode::Validation);
}

AETHER_TEST("open-meteo: provider error objects and oversized bodies are reported") {
    const std::string errorBody = R"({"error":true,"reason":"Latitude must be in range of -90 to 90°.\r\nGiven: 91.0."})";
    auto error = openmeteo::ParseForecast(errorBody, TemperatureUnit::Celsius);
    REQUIRE_FALSE(error.ok());
    CHECK_EQ(error.error().code, ErrorCode::Http);
    const auto reason = openmeteo::ProviderErrorReason(errorBody);
    REQUIRE(reason.has_value());
    CHECK(reason->find("Latitude must be in range") == 0);
    CHECK(reason->find('\r') == std::string::npos);
    CHECK(reason->find('\n') == std::string::npos);
    CHECK_FALSE(openmeteo::ProviderErrorReason(R"({"error":false})").has_value());
    CHECK_FALSE(openmeteo::ProviderErrorReason("not json").has_value());
    CHECK_EQ(*openmeteo::ProviderErrorReason(R"({"error":true})"), std::string("unspecified error"));

    std::string huge = testing::ForecastBody(15.0, 2, 1);
    huge.insert(1, R"("padding":")" + std::string(openmeteo::kMaxResponseBytes, 'x') + R"(",)");
    auto oversized = openmeteo::ParseForecast(huge, TemperatureUnit::Celsius);
    REQUIRE_FALSE(oversized.ok());
    CHECK_EQ(oversized.error().code, ErrorCode::Integrity);
}

AETHER_TEST("open-meteo: optional unit metadata is strictly typed and uses the documented label") {
    constexpr std::string_view current =
        R"({"time":"2026-10-17T15:00","temperature_2m":15,"weather_code":2,"is_day":1})";
    for (const std::string_view units : {"null", "[]", "15", R"("celsius")"}) {
        auto parsed = openmeteo::ParseForecast(
            ForecastWith(current, "\"current_units\":" + std::string(units) + ","), TemperatureUnit::Celsius);
        REQUIRE_FALSE(parsed.ok());
        CHECK_EQ(parsed.error().code, ErrorCode::Parse);
    }
    for (const std::string_view label : {R"("not-a-unit-C")", R"("C")", "null", "15"}) {
        auto parsed = openmeteo::ParseForecast(
            ForecastWith(current, "\"current_units\":{\"temperature_2m\":" + std::string(label) + "},"),
            TemperatureUnit::Celsius);
        REQUIRE_FALSE(parsed.ok());
        CHECK_EQ(parsed.error().code, ErrorCode::Validation);
    }
    CHECK(openmeteo::ParseForecast(testing::ForecastBody(15.0, 2, 1), TemperatureUnit::Celsius).ok());
    CHECK(openmeteo::ParseForecast(testing::ForecastBody(59.0, 2, 1, "F"), TemperatureUnit::Fahrenheit).ok());
}

// ---------------------------------------------------------------------------------------------
// Geocoding parsing and Retry-After
// ---------------------------------------------------------------------------------------------

AETHER_TEST("open-meteo: geocoding results are validated and bounded") {
    const std::string body = R"({"results":[
        {"id":2657896,"name":"Zürich","latitude":47.36667,"longitude":8.55,"country_code":"CH",
         "admin1":"Zurich","country":"Switzerland","population":341730},
        {"id":1,"name":"No coordinates","country":"Nowhere"},
        {"id":2,"name":"Bad latitude","latitude":95.0,"longitude":8.0},
        {"id":3,"name":"  Tab\tName  ","latitude":-33.5,"longitude":-70.6,"country":"Chile"},
        {"id":4,"name":"","latitude":1,"longitude":1},
        "not an object",
        {"id":5,"name":"Third","latitude":10,"longitude":20},
        {"id":6,"name":"Fourth","latitude":11,"longitude":21}
    ],"generationtime_ms":0.5})";
    auto parsed = openmeteo::ParseGeocoding(body, 3);
    REQUIRE(parsed.ok());
    const auto& candidates = parsed.value();
    REQUIRE_EQ(candidates.size(), std::size_t{3});
    CHECK_EQ(candidates[0].name, std::wstring(L"Zürich"));
    CHECK_EQ(candidates[0].admin1, std::wstring(L"Zurich"));
    CHECK_EQ(candidates[0].country, std::wstring(L"Switzerland"));
    CHECK_NEAR(candidates[0].latitude, 47.36667, 1e-9);
    CHECK_NEAR(candidates[0].longitude, 8.55, 1e-9);
    CHECK_EQ(candidates[1].name, std::wstring(L"Tab Name"));
    CHECK(candidates[1].admin1.empty());
    CHECK_EQ(candidates[2].name, std::wstring(L"Third"));

    auto none = openmeteo::ParseGeocoding(R"({"generationtime_ms":0.2})", 5);
    REQUIRE(none.ok());
    CHECK(none.value().empty());

    auto wrongType = openmeteo::ParseGeocoding(R"({"results":{}})", 5);
    REQUIRE_FALSE(wrongType.ok());
    CHECK_EQ(wrongType.error().code, ErrorCode::Parse);

    auto providerError = openmeteo::ParseGeocoding(R"({"error":true,"reason":"Parameter count must be between 1 and 100."})", 5);
    REQUIRE_FALSE(providerError.ok());
    CHECK_EQ(providerError.error().code, ErrorCode::Http);
}

AETHER_TEST("open-meteo: Retry-After accepts bounded delta-seconds only") {
    const auto retryAfter = [](std::string value) {
        net::HttpResponse response;
        response.status = 429;
        response.headers.push_back({"retry-after", std::move(value)});
        return openmeteo::ParseRetryAfter(response);
    };
    CHECK(retryAfter("120") == std::optional<std::chrono::seconds>(120s));
    CHECK(retryAfter(" 5 ") == std::optional<std::chrono::seconds>(5s));
    CHECK_FALSE(retryAfter("-1").has_value());
    CHECK_FALSE(retryAfter("86401").has_value());
    CHECK_FALSE(retryAfter("12abc").has_value());
    CHECK_FALSE(retryAfter("Wed, 21 Oct 2026 07:28:00 GMT").has_value());
    CHECK_FALSE(openmeteo::ParseRetryAfter(net::HttpResponse{}).has_value());
}

// ---------------------------------------------------------------------------------------------
// Cache
// ---------------------------------------------------------------------------------------------

AETHER_TEST("weather cache: readings round-trip through JSON and disk") {
    const auto reading = SampleReading();
    auto parsed = cache::Parse(cache::Serialize(reading));
    REQUIRE(parsed.ok());
    CHECK(parsed.value() == reading);

    const auto dir = aether::test::MakeTempDirectory("weather-cache");
    const auto file = dir / "weather.json";
    auto missing = cache::Load(file);
    REQUIRE(missing.ok());
    CHECK_FALSE(missing.value().has_value());

    REQUIRE(cache::Save(file, reading).ok());
    auto loaded = cache::Load(file);
    REQUIRE(loaded.ok());
    REQUIRE(loaded.value().has_value());
    CHECK(*loaded.value() == reading);

    REQUIRE(fs::WriteFileAtomic(file, "{\"version\":1,").ok());
    auto corrupt = cache::Load(file);
    REQUIRE_FALSE(corrupt.ok());
    CHECK_EQ(corrupt.error().code, ErrorCode::Parse);
}

AETHER_TEST("weather cache: invalid entries are rejected as a whole") {
    // Replaces the value of one member of a serialized reading (string values included).
    const auto mutate = [](std::string_view key, std::string_view value) {
        std::string text = cache::Serialize(SampleReading());
        const std::string marker = "\"" + std::string(key) + "\":";
        const auto start = text.find(marker);
        if (start == std::string::npos) return text;
        const auto valueStart = start + marker.size();
        const auto valueEnd = text[valueStart] == '"' ? text.find('"', valueStart + 1) + 1
                                                      : text.find_first_of(",}", valueStart);
        text.replace(valueStart, valueEnd - valueStart, value);
        return text;
    };
    const auto errorOf = [](const std::string& text) {
        auto parsed = cache::Parse(text);
        return parsed.ok() ? ErrorCode::Unknown : parsed.error().code;
    };
    CHECK_EQ(errorOf(mutate("version", "1")), ErrorCode::Unknown);  // the mutation helper itself works
    CHECK_EQ(errorOf(mutate("version", "2")), ErrorCode::Validation);
    CHECK_EQ(errorOf(mutate("unit", "\"kelvin\"")), ErrorCode::Validation);
    CHECK_EQ(errorOf(mutate("temperature", "99")), ErrorCode::Validation);
    CHECK_EQ(errorOf(mutate("temperature", "\"15\"")), ErrorCode::Validation);
    CHECK_EQ(errorOf(mutate("weatherCode", "120")), ErrorCode::Validation);
    CHECK_EQ(errorOf(mutate("isDay", "1")), ErrorCode::Validation);
    CHECK_EQ(errorOf(mutate("observedAt", "0")), ErrorCode::Validation);
    CHECK_EQ(errorOf(mutate("fetchedAt", "-5")), ErrorCode::Validation);
    CHECK_EQ(errorOf(mutate("locationKey", "\"\"")), ErrorCode::Validation);
    CHECK_EQ(errorOf(mutate("locationKey", "\"1.0000,2.0000\"")), ErrorCode::Unknown);
    CHECK_EQ(errorOf("[]"), ErrorCode::Parse);
}

AETHER_TEST("weather cache: entries expire after six hours and never come from the future") {
    const auto reading = SampleReading();
    CHECK(cache::IsWithinMaxAge(reading, reading.fetchedAt));
    CHECK(cache::IsWithinMaxAge(reading, reading.fetchedAt + 5h + 59min));
    CHECK_FALSE(cache::IsWithinMaxAge(reading, reading.fetchedAt + 6h));
    CHECK(cache::IsWithinMaxAge(reading, reading.fetchedAt - 1min));
    CHECK_FALSE(cache::IsWithinMaxAge(reading, reading.fetchedAt - 10min));
}

// ---------------------------------------------------------------------------------------------
// Configuration and labels
// ---------------------------------------------------------------------------------------------

AETHER_TEST("weather config: validation rejects incomplete or out-of-range settings") {
    CHECK(ValidateConfig(WeatherConfig{}).ok());
    CHECK(ValidateConfig(testing::ParisConfig()).ok());

    auto config = testing::ParisConfig();
    config.longitude.reset();
    const auto incomplete = ValidateConfig(config);
    REQUIRE_FALSE(incomplete.ok());
    CHECK_EQ(incomplete.error().code, ErrorCode::InvalidArgument);

    config = testing::ParisConfig();
    config.latitude = 90.5;
    CHECK_FALSE(ValidateConfig(config).ok());
    config.latitude = std::numeric_limits<double>::quiet_NaN();
    CHECK_FALSE(ValidateConfig(config).ok());

    config = testing::ParisConfig();
    config.longitude = -180.01;
    CHECK_FALSE(ValidateConfig(config).ok());

    config = testing::ParisConfig();
    config.refreshInterval = 5min;
    CHECK_FALSE(ValidateConfig(config).ok());
    config.refreshInterval = 24h;
    CHECK(ValidateConfig(config).ok());
    config.refreshInterval = 25h;
    CHECK_FALSE(ValidateConfig(config).ok());
}

AETHER_TEST("weather types: location labels skip empty and repeated parts") {
    CHECK_EQ(FormatLocationLabel({L"Paris", L"Île-de-France", L"France", 48.85, 2.35}),
             std::wstring(L"Paris, Île-de-France, France"));
    CHECK_EQ(FormatLocationLabel({L"Zurich", L"Zurich", L"Switzerland", 47.37, 8.55}),
             std::wstring(L"Zurich, Switzerland"));
    CHECK_EQ(FormatLocationLabel({L"Monaco", L"", L"Monaco", 43.73, 7.42}), std::wstring(L"Monaco"));
    CHECK_EQ(ToString(status::WeatherState::ProviderUnavailable), std::string_view("provider-unavailable"));
    CHECK_EQ(ToString(TemperatureUnit::Fahrenheit), std::string_view("fahrenheit"));
}
