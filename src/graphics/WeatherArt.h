// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// D78: animated weather illustrations for every condition Open-Meteo reports (WMO 4677 codes): a sun
// whose rays turn, clouds that drift, rain and drizzle that fall, snow that sways, hail, lightning,
// fog banks, a moon that rocks among twinkling stars.
//
// Two styles share one drawing on the 16-unit glyph grid:
//   Monochrome - outlines in one ink, the Top Bar's grey (a sun behind a cloud is hidden by a mask,
//                never by painting over the glass);
//   Color      - the illustrated palette of the weather window: a yellow sun, white or storm-grey
//                clouds, blue rain.
//
// Motion is driven by the caller, never by a timer here: `clock` is the animation time in seconds
// and `energy` (0..1) how much of the motion shows. At energy 0 every element rests in its still
// pose, so an icon that stops is never caught half-faded; oscillations grow with the energy and
// falling particles cross-fade from their still pose. The caller advances the clock only while it
// wants frames (hover in the Top Bar, the window while it is shown) and honours reduced motion by
// leaving the energy at 0.
#pragma once

#include "graphics/Theme.h"

#include <d2d1_1.h>

#include <cstdint>
#include <optional>
#include <string_view>

namespace aether::graphics {

enum class WeatherScene : std::uint8_t {
    Clear,             // 0
    MainlyClear,       // 1
    PartlyCloudy,      // 2
    Overcast,          // 3
    Fog,               // 45
    RimeFog,           // 48, depositing rime
    Drizzle,           // 51 53 55
    FreezingDrizzle,   // 56 57
    Rain,              // 61 63 65
    FreezingRain,      // 66 67
    Snow,              // 71 73 75
    SnowGrains,        // 77
    RainShowers,       // 80 81 82
    SnowShowers,       // 85 86
    Thunderstorm,      // 95
    ThunderstormHail,  // 96 99
    Unknown,           // no code, or one Open-Meteo does not document
};

struct WeatherArtSubject {
    WeatherScene scene = WeatherScene::Unknown;
    int intensity = 2;  // 1 light, 2 moderate, 3 heavy / dense / violent
    bool isDay = true;

    bool operator==(const WeatherArtSubject&) const = default;
};

// The scene and intensity of a WMO code; isDay unknown counts as day.
[[nodiscard]] WeatherArtSubject WeatherSubjectForCode(std::optional<int> wmoCode, std::optional<bool> isDay) noexcept;
// Stable identifier for tests and logs ("rain-showers"...).
[[nodiscard]] std::string_view ToString(WeatherScene scene) noexcept;
// Everything but Unknown has something that moves.
[[nodiscard]] bool WeatherArtMoves(const WeatherArtSubject& subject) noexcept;

enum class WeatherArtStyle { Monochrome, Color };

struct WeatherArtMotion {
    double clock = 0.0;   // seconds of animation time
    float energy = 0.0f;  // 0: still pose, 1: full motion
};

// Draws `subject` centred in `box` (square grid fitted to the shorter side). Monochrome draws in
// `ink`; Color ignores it. `strokeWidth` is in grid units (16 per box side). `cached` adds the small
// clock badge of a cached reading (Monochrome). The target's transform is restored.
void PaintWeatherArt(ID2D1DeviceContext* target, const D2D1_RECT_F& box, const WeatherArtSubject& subject,
                     WeatherArtStyle style, const Color& ink, WeatherArtMotion motion, float strokeWidth = 1.5f,
                     bool cached = false);

// D78: the sky behind the weather window's main card for the place's hour: night blue, dawn,
// morning light blue, day blue, dusk; greyed under cloud, darkened by storms. `top` and `bottom`
// are the gradient's ends.
struct SkyColors {
    Color top;
    Color bottom;
    bool darkText = false;  // the card's text reads dark on this sky
};
// `hour` is the local time of day (0..24, fractional); sunrise/sunset in the same unit (6 and 18
// when unknown).
[[nodiscard]] SkyColors SkyFor(const WeatherArtSubject& subject, double hour, double sunrise = 6.0, double sunset = 18.0) noexcept;

}  // namespace aether::graphics
