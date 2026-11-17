// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// D78: the weather window's detailed forecast and map temperatures: URLs, strict parsing with
// optional details, and the client's error mapping.
#include "AetherTest.h"
#include "WeatherTestSupport.h"

#include "weather/ForecastClient.h"
#include "weather/OpenMeteo.h"

#include <string>

using namespace aether;
using namespace aether::weather;
using aether::status::TemperatureUnit;
using aether::weather::testing::FakeHttpClient;
using namespace std::chrono;

namespace {

// Shape of a live answer (Paris, 2026-11-16, trimmed to three hours and two days).
std::string DetailedBody(std::string_view hourlyTemperatures = "[11.3,10.9,null]",
                         std::string_view dailyLows = "[8.1,6.0]") {
    return std::string(R"({"latitude":48.853382,"longitude":2.355208,"utc_offset_seconds":3600,)"
                       R"("timezone":"Europe/Paris","elevation":36.0,)"
                       R"("current_units":{"temperature_2m":"°C"},)"
                       R"("current":{"time":"2026-11-16T22:45","interval":900,"temperature_2m":11.3,)"
                       R"("apparent_temperature":9.2,"relative_humidity_2m":88,"weather_code":61,"is_day":0,)"
                       R"("cloud_cover":100,"pressure_msl":1011.4,"wind_speed_10m":14.8,"wind_direction_10m":225,)"
                       R"("wind_gusts_10m":null},)"
                       R"("hourly_units":{"temperature_2m":"°C"},)"
                       R"("hourly":{"time":["2026-11-16T22:00","2026-11-16T23:00","2026-11-17T00:00"],)"
                       R"("temperature_2m":)") +
           std::string(hourlyTemperatures) +
           R"(,"weather_code":[61,63,3],"is_day":[0,0,0],"precipitation_probability":[80,null,20]},)"
           R"("daily_units":{"temperature_2m_max":"°C"},)"
           R"("daily":{"time":["2026-11-16","2026-11-17"],"weather_code":[61,2],"temperature_2m_max":[12.4,13.0],)"
           R"("temperature_2m_min":)" +
           std::string(dailyLows) +
           R"(,"sunrise":["2026-11-16T08:01","2026-11-17T08:03"],"sunset":["2026-11-16T17:08",null],)"
           R"("precipitation_probability_max":[90,15],"uv_index_max":[1.4,1.1]}})";
}

}  // namespace

AETHER_TEST("forecast: the detailed URL asks for current details, 25 hours and 7 days in local time") {
    const auto url = openmeteo::BuildDetailedForecastUrl(48.8566, 2.3522, TemperatureUnit::Celsius);
    CHECK_EQ(url, std::string("https://api.open-meteo.com/v1/forecast?latitude=48.8566&longitude=2.3522"
                              "&current=temperature_2m,apparent_temperature,relative_humidity_2m,weather_code,is_day,"
                              "cloud_cover,pressure_msl,wind_speed_10m,wind_direction_10m,wind_gusts_10m"
                              "&hourly=temperature_2m,weather_code,is_day,precipitation_probability"
                              "&daily=weather_code,temperature_2m_max,temperature_2m_min,sunrise,sunset,"
                              "precipitation_probability_max,uv_index_max"
                              "&forecast_hours=25&forecast_days=7&temperature_unit=celsius&wind_speed_unit=kmh"
                              "&timezone=auto"));
    const auto fahrenheit = openmeteo::BuildDetailedForecastUrl(40.7128, -74.006, TemperatureUnit::Fahrenheit);
    CHECK(fahrenheit.find("&temperature_unit=fahrenheit&wind_speed_unit=mph") != std::string::npos);
}

AETHER_TEST("forecast: a live-shaped answer parses with its optional details") {
    auto parsed = openmeteo::ParseDetailedForecast(DetailedBody(), TemperatureUnit::Celsius);
    REQUIRE(parsed.ok());
    const auto& forecast = parsed.value();
    CHECK_EQ(forecast.utcOffsetSeconds, 3600);
    CHECK_EQ(forecast.timezone, std::wstring(L"Europe/Paris"));
    REQUIRE(forecast.elevation.has_value());
    CHECK_NEAR(*forecast.elevation, 36.0, 1e-9);
    CHECK_NEAR(forecast.current.temperature, 11.3, 1e-9);
    CHECK_EQ(forecast.current.weatherCode, 61);
    CHECK(!forecast.current.isDay);
    CHECK(forecast.current.localTime == sys_days{year{2026} / November / day{16}} + hours{22} + minutes{45});
    REQUIRE(forecast.current.apparentTemperature.has_value());
    CHECK_NEAR(*forecast.current.apparentTemperature, 9.2, 1e-9);
    CHECK(forecast.current.humidity == 88);
    CHECK(forecast.current.cloudCover == 100);
    CHECK(forecast.current.windDirection == 225);
    CHECK(!forecast.current.windGusts.has_value());  // null stays unset

    // The third hour has no temperature: it is left out rather than shown as zero.
    REQUIRE_EQ(forecast.hourly.size(), std::size_t{2});
    CHECK(forecast.hourly[0].precipitationProbability == 80);
    CHECK(!forecast.hourly[1].precipitationProbability.has_value());
    CHECK_EQ(forecast.hourly[1].weatherCode, 63);

    REQUIRE_EQ(forecast.daily.size(), std::size_t{2});
    CHECK(forecast.daily[1].date == sys_days{year{2026} / November / day{17}});
    CHECK_NEAR(forecast.daily[1].maxTemperature, 13.0, 1e-9);
    CHECK(forecast.daily[0].sunrise.has_value());
    CHECK(forecast.daily[0].sunset == sys_days{year{2026} / November / day{16}} + hours{17} + minutes{8});
    CHECK(forecast.daily[1].sunrise.has_value());
    CHECK(!forecast.daily[1].sunset.has_value());
    REQUIRE(forecast.daily[0].uvIndex.has_value());
    CHECK_NEAR(*forecast.daily[0].uvIndex, 1.4, 1e-9);

    // LocalNow: 21:45 UTC is 22:45 in Paris in November.
    const auto now = system_clock::time_point{sys_days{year{2026} / November / day{16}} + hours{21} + minutes{45}};
    CHECK(forecast.LocalNow(now) == forecast.current.localTime);
}

AETHER_TEST("forecast: days with a low above the high, wrong units and missing blocks are refused") {
    auto swapped = openmeteo::ParseDetailedForecast(DetailedBody("[11.3,10.9,9.0]", "[8.1,14.0]"), TemperatureUnit::Celsius);
    REQUIRE(swapped.ok());
    CHECK_EQ(swapped.value().daily.size(), std::size_t{1});
    CHECK_EQ(swapped.value().hourly.size(), std::size_t{3});

    auto wrongUnit = openmeteo::ParseDetailedForecast(DetailedBody(), TemperatureUnit::Fahrenheit);
    REQUIRE(!wrongUnit.ok());
    CHECK(wrongUnit.error().code == ErrorCode::Validation);

    auto noDays = openmeteo::ParseDetailedForecast(DetailedBody("[1,2,3]", "[99,99]"), TemperatureUnit::Celsius);
    REQUIRE(!noDays.ok());
    CHECK(noDays.error().code == ErrorCode::Validation);

    std::string noHourly = DetailedBody();
    noHourly.replace(noHourly.find("\"hourly\":"), 9, "\"hourlyX\":");
    auto missing = openmeteo::ParseDetailedForecast(noHourly, TemperatureUnit::Celsius);
    REQUIRE(!missing.ok());
    CHECK(missing.error().code == ErrorCode::Parse);

    auto provider = openmeteo::ParseDetailedForecast(R"({"error":true,"reason":"Latitude must be in range"})",
                                                     TemperatureUnit::Celsius);
    REQUIRE(!provider.ok());
    CHECK(provider.error().code == ErrorCode::Http);
}

AETHER_TEST("forecast: map places go in one request and come back by position") {
    const std::vector<GeoPoint> points{{48.8566, 2.3522}, {45.764, 4.8357}, {43.2965, 5.3698}};
    CHECK_EQ(openmeteo::BuildPointsUrl(points, TemperatureUnit::Celsius),
             std::string("https://api.open-meteo.com/v1/forecast?latitude=48.8566,45.7640,43.2965"
                         "&longitude=2.3522,4.8357,5.3698&current=temperature_2m,weather_code,is_day"
                         "&temperature_unit=celsius"));

    const std::string body = R"([{"current":{"temperature_2m":11.2,"weather_code":61,"is_day":0}},)"
                             R"({"location_id":1,"current":{"temperature_2m":null,"weather_code":3,"is_day":0}},)"
                             R"({"location_id":2,"current":{"temperature_2m":15.5,"weather_code":0,"is_day":0}}])";
    auto readings = openmeteo::ParsePoints(body, TemperatureUnit::Celsius, 3);
    REQUIRE(readings.ok());
    REQUIRE_EQ(readings.value().size(), std::size_t{2});
    CHECK_EQ(readings.value()[0].index, std::size_t{0});
    CHECK_EQ(readings.value()[1].index, std::size_t{2});
    CHECK_NEAR(readings.value()[1].temperature, 15.5, 1e-9);

    auto mismatch = openmeteo::ParsePoints(body, TemperatureUnit::Celsius, 4);
    REQUIRE(!mismatch.ok());
    CHECK(mismatch.error().code == ErrorCode::Validation);

    auto single = openmeteo::ParsePoints(R"({"current":{"temperature_2m":-3.5,"weather_code":71,"is_day":1}})",
                                         TemperatureUnit::Celsius, 1);
    REQUIRE(single.ok());
    REQUIRE_EQ(single.value().size(), std::size_t{1});
    CHECK_EQ(single.value()[0].weatherCode, 71);
}

AETHER_TEST("forecast client: validates input, maps HTTP errors and stamps the fetch") {
    FakeHttpClient http;
    ForecastClient client(http);

    auto invalid = client.Fetch(91.0, 0.0, TemperatureUnit::Celsius, CancellationToken{});
    REQUIRE(!invalid.ok());
    CHECK(invalid.error().code == ErrorCode::InvalidArgument);
    CHECK_EQ(http.RequestCount(), std::size_t{0});

    http.RespondWith(200, DetailedBody());
    auto fetched = client.Fetch(48.8566, 2.3522, TemperatureUnit::Celsius, CancellationToken{});
    REQUIRE(fetched.ok());
    CHECK(fetched.value().fetchedAt.time_since_epoch().count() > 0);
    REQUIRE_EQ(http.RequestCount(), std::size_t{1});
    CHECK_EQ(http.Requests()[0].maxRedirects, 0);

    http.RespondWith(429, R"({"error":true,"reason":"Too many requests"})");
    auto limited = client.Fetch(48.8566, 2.3522, TemperatureUnit::Celsius, CancellationToken{});
    REQUIRE(!limited.ok());
    CHECK(limited.error().code == ErrorCode::RateLimited);

    http.RespondWith(503, "");
    auto down = client.FetchPoints({{48.8566, 2.3522}}, TemperatureUnit::Celsius, CancellationToken{});
    REQUIRE(!down.ok());
    CHECK(down.error().code == ErrorCode::Unavailable);

    auto none = client.FetchPoints({}, TemperatureUnit::Celsius, CancellationToken{});
    REQUIRE(!none.ok());
    CHECK(none.error().code == ErrorCode::InvalidArgument);

    CancellationSource source;
    source.Cancel();
    auto cancelled = client.Fetch(48.8566, 2.3522, TemperatureUnit::Celsius, source.Token());
    REQUIRE(!cancelled.ok());
    CHECK(cancelled.error().code == ErrorCode::Cancelled);
}
