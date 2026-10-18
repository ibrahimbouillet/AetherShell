// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "weather/WeatherCache.h"

#include "core/FileSystem.h"
#include "core/Json.h"
#include "weather/OpenMeteo.h"

namespace aether::weather::cache {
namespace {

using std::chrono::system_clock;

constexpr std::size_t kMaxLocationKeyLength = 64;

std::int64_t ToUnixSeconds(system_clock::time_point time) {
    return std::chrono::duration_cast<std::chrono::seconds>(time.time_since_epoch()).count();
}

std::optional<system_clock::time_point> FromUnixSeconds(const json::Value* value) {
    if (!value) return std::nullopt;
    const auto seconds = value->AsInt64();
    // Reject zero/negative values and anything beyond year ~2286 (10^10 s).
    if (!seconds || *seconds <= 0 || *seconds >= 10'000'000'000) return std::nullopt;
    return system_clock::time_point{std::chrono::seconds{*seconds}};
}

}  // namespace

std::string Serialize(const WeatherReading& reading) {
    json::Value root = json::Value::MakeObject();
    root.Set("version", kSchemaVersion);
    root.Set("locationKey", reading.locationKey);
    root.Set("unit", std::string(ToString(reading.unit)));
    root.Set("temperature", reading.temperature);
    root.Set("weatherCode", reading.weatherCode);
    root.Set("isDay", reading.isDay);
    root.Set("observedAt", ToUnixSeconds(reading.observedAt));
    root.Set("fetchedAt", ToUnixSeconds(reading.fetchedAt));
    return json::Serialize(root);
}

Result<WeatherReading> Parse(std::string_view text) {
    json::ParseOptions options;
    options.maxInputBytes = static_cast<std::size_t>(kMaxCacheFileBytes);
    options.maxDepth = 4;
    auto parsed = json::Parse(text, options);
    if (!parsed) return MakeError(ErrorCode::Parse, "weather cache is not valid JSON: " + parsed.error().message);
    const json::Value& root = parsed.value();
    if (!root.IsObject()) return MakeError(ErrorCode::Parse, "weather cache is not a JSON object");

    if (root.GetInt64("version", 0) != kSchemaVersion) {
        return MakeError(ErrorCode::Validation, "unsupported weather cache version");
    }

    WeatherReading reading;
    const auto* key = root.Find("locationKey");
    if (!key || !key->AsString() || key->AsString()->empty() || key->AsString()->size() > kMaxLocationKeyLength) {
        return MakeError(ErrorCode::Validation, "invalid weather cache location key");
    }
    reading.locationKey = *key->AsString();

    const std::string unit = root.GetString("unit");
    if (unit == "celsius") {
        reading.unit = status::TemperatureUnit::Celsius;
    } else if (unit == "fahrenheit") {
        reading.unit = status::TemperatureUnit::Fahrenheit;
    } else {
        return MakeError(ErrorCode::Validation, "invalid weather cache unit");
    }

    const auto* temperature = root.Find("temperature");
    if (!temperature || !temperature->AsDouble() ||
        !openmeteo::IsPlausibleTemperature(*temperature->AsDouble(), reading.unit)) {
        return MakeError(ErrorCode::Validation, "invalid weather cache temperature");
    }
    reading.temperature = *temperature->AsDouble();

    const auto* code = root.Find("weatherCode");
    if (!code || !code->AsInt64() || !IsValidWeatherCode(*code->AsInt64())) {
        return MakeError(ErrorCode::Validation, "invalid weather cache weather code");
    }
    reading.weatherCode = static_cast<int>(*code->AsInt64());

    const auto* isDay = root.Find("isDay");
    if (!isDay || !isDay->AsBool()) return MakeError(ErrorCode::Validation, "invalid weather cache isDay");
    reading.isDay = *isDay->AsBool();

    const auto observedAt = FromUnixSeconds(root.Find("observedAt"));
    const auto fetchedAt = FromUnixSeconds(root.Find("fetchedAt"));
    if (!observedAt || !fetchedAt) return MakeError(ErrorCode::Validation, "invalid weather cache timestamps");
    reading.observedAt = *observedAt;
    reading.fetchedAt = *fetchedAt;
    return reading;
}

Result<std::optional<WeatherReading>> Load(const std::filesystem::path& file) {
    auto contents = fs::ReadFile(file, kMaxCacheFileBytes);
    if (!contents) {
        if (contents.error().code == ErrorCode::NotFound) return std::optional<WeatherReading>{};
        return contents.error();
    }
    auto reading = Parse(contents.value());
    if (!reading) return reading.error();
    return std::optional<WeatherReading>{std::move(reading).value()};
}

Result<void> Save(const std::filesystem::path& file, const WeatherReading& reading) {
    return fs::WriteFileAtomic(file, Serialize(reading));
}

bool IsWithinMaxAge(const WeatherReading& reading, system_clock::time_point now) noexcept {
    if (reading.fetchedAt > now + kMaxFutureSkew) return false;
    return now - reading.fetchedAt < kMaxCacheAge;
}

}  // namespace aether::weather::cache
