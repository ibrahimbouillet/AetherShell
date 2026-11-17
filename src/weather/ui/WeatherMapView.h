// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// D78: the pure arithmetic of the weather window's map: placing geographic positions in a rectangle,
// the colour of a temperature and the temperature between the places that were measured.
#pragma once

#include "graphics/Theme.h"
#include "status/StatusTypes.h"
#include "weather/WorldMap.h"

#include <d2d1.h>

#include <optional>
#include <span>

namespace aether::weather::ui {

// Equirectangular projection of `view` into `area` (DIPs). Longitudes are shortened by the cosine of the
// view's middle latitude so a country keeps its shape; the result is centred in `area` and never stretched.
struct MapProjection {
    GeoBounds view;
    D2D1_RECT_F area{};
    D2D1_RECT_F drawn{};   // where the view lands inside `area`
    double scale = 1.0;    // DIPs per projected degree
    double cosine = 1.0;

    [[nodiscard]] D2D1_POINT_2F Project(double latitude, double longitude) const noexcept;
    // The geographic position under a point of `area` (the inverse of Project).
    [[nodiscard]] std::pair<double, double> Unproject(D2D1_POINT_2F point) const noexcept;
};
[[nodiscard]] MapProjection MakeProjection(const GeoBounds& view, const D2D1_RECT_F& area) noexcept;

// The colour of a temperature on the map and in the forecast's range bars: deep blue at -20 °C, cyan
// at freezing, green, yellow, orange, red at 35 °C and above. Fahrenheit is converted first.
[[nodiscard]] graphics::Color TemperatureColor(double value, status::TemperatureUnit unit) noexcept;

struct TemperatureSample {
    double latitude = 0.0;
    double longitude = 0.0;
    double value = 0.0;
};
// Inverse-distance-weighted temperature (power 2) at a position: a sample at the position itself wins;
// nullopt without samples.
[[nodiscard]] std::optional<double> InterpolateTemperature(std::span<const TemperatureSample> samples, double latitude,
                                                           double longitude, double cosine = 1.0) noexcept;

}  // namespace aether::weather::ui
