// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// WMO weather interpretation codes (WMO 4677 subset used by Open-Meteo) mapped to
// short English condition text and to a small set of glyph categories the Top Bar
// renderer draws. The renderer combines the glyph with `isDay` (WeatherReport) to pick
// a day or night variant for Clear and PartlyCloudy.
//
//   Code(s)   Text             Glyph
//   0         Clear sky        Clear
//   1         Mainly clear     Clear
//   2         Partly cloudy    PartlyCloudy
//   3         Overcast         Cloudy
//   45, 48    Fog              Fog
//   51-57     Drizzle          Drizzle   (includes freezing drizzle 56/57)
//   61-67     Rain             Rain      (includes freezing rain 66/67)
//   71-77     Snow             Snow      (includes snow grains 77)
//   80-82     Rain showers     Rain
//   85-86     Snow showers     Snow
//   95-99     Thunderstorm     Thunder   (includes hail variants 96/99)
//
// Any other value in 0..99 is a valid WMO code that Open-Meteo does not document; it maps to
// an empty text and WeatherGlyph::Unknown rather than a guessed condition.
#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace aether::weather {

enum class WeatherGlyph { Clear, PartlyCloudy, Cloudy, Fog, Drizzle, Rain, Snow, Thunder, Unknown };

inline constexpr int kMinWeatherCode = 0;
inline constexpr int kMaxWeatherCode = 99;

// True for codes in the WMO 4677 range 0..99.
[[nodiscard]] constexpr bool IsValidWeatherCode(std::int64_t code) noexcept {
    return code >= kMinWeatherCode && code <= kMaxWeatherCode;
}

// English condition text; empty for codes without a documented meaning (see table above).
[[nodiscard]] std::wstring_view ConditionText(int code) noexcept;

// Glyph category; Unknown for undocumented codes or when no code is known.
[[nodiscard]] WeatherGlyph GlyphForCode(std::optional<int> code) noexcept;

// Stable identifier for logs and tests ("clear", "partly-cloudy", ...).
[[nodiscard]] std::string_view ToString(WeatherGlyph glyph) noexcept;

}  // namespace aether::weather
