// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "weather/WeatherConditions.h"

namespace aether::weather {
namespace {

struct ConditionRange {
    int first;
    int last;
    std::wstring_view text;
    WeatherGlyph glyph;
};

// Ranges documented by Open-Meteo (https://open-meteo.com/en/docs, "WMO Weather interpretation codes").
constexpr ConditionRange kConditions[] = {
    {0, 0, L"Clear sky", WeatherGlyph::Clear},
    {1, 1, L"Mainly clear", WeatherGlyph::Clear},
    {2, 2, L"Partly cloudy", WeatherGlyph::PartlyCloudy},
    {3, 3, L"Overcast", WeatherGlyph::Cloudy},
    {45, 45, L"Fog", WeatherGlyph::Fog},
    {48, 48, L"Fog", WeatherGlyph::Fog},
    {51, 57, L"Drizzle", WeatherGlyph::Drizzle},
    {61, 67, L"Rain", WeatherGlyph::Rain},
    {71, 77, L"Snow", WeatherGlyph::Snow},
    {80, 82, L"Rain showers", WeatherGlyph::Rain},
    {85, 86, L"Snow showers", WeatherGlyph::Snow},
    {95, 99, L"Thunderstorm", WeatherGlyph::Thunder},
};

const ConditionRange* Find(int code) noexcept {
    for (const auto& range : kConditions) {
        if (code >= range.first && code <= range.last) return &range;
    }
    return nullptr;
}

}  // namespace

std::wstring_view ConditionText(int code) noexcept {
    const auto* range = Find(code);
    return range ? range->text : std::wstring_view{};
}

WeatherGlyph GlyphForCode(std::optional<int> code) noexcept {
    if (!code) return WeatherGlyph::Unknown;
    const auto* range = Find(*code);
    return range ? range->glyph : WeatherGlyph::Unknown;
}

std::string_view ToString(WeatherGlyph glyph) noexcept {
    switch (glyph) {
        case WeatherGlyph::Clear: return "clear";
        case WeatherGlyph::PartlyCloudy: return "partly-cloudy";
        case WeatherGlyph::Cloudy: return "cloudy";
        case WeatherGlyph::Fog: return "fog";
        case WeatherGlyph::Drizzle: return "drizzle";
        case WeatherGlyph::Rain: return "rain";
        case WeatherGlyph::Snow: return "snow";
        case WeatherGlyph::Thunder: return "thunder";
        case WeatherGlyph::Unknown: return "unknown";
    }
    return "unknown";
}

}  // namespace aether::weather
