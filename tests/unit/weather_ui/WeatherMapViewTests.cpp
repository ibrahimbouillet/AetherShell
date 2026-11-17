// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// D78: the weather window map's arithmetic: projection, temperature colours, interpolation.
#include "AetherTest.h"
#include "weather/ui/WeatherMapView.h"

#include <array>
#include <cmath>

using namespace aether;
using namespace aether::weather;
using namespace aether::weather::ui;

namespace {
bool Near(double a, double b, double tolerance = 1e-6) { return std::abs(a - b) <= tolerance; }
}  // namespace

AETHER_TEST("weather_ui map: the projection keeps the country's shape, centres it and inverts exactly") {
    // Metropolitan France, about 13 by 9.6 degrees.
    const GeoBounds france{-5.2, 41.3, 9.6, 51.1};
    const auto area = D2D1::RectF(10.0f, 20.0f, 330.0f, 320.0f);
    const auto projection = MakeProjection(france, area);
    CHECK(Near(projection.cosine, std::cos(46.2 * 3.14159265358979 / 180.0), 1e-9));
    // Never stretched: one projected degree has the same length both ways.
    const auto west = projection.Project(46.2, -5.2), east = projection.Project(46.2, 9.6);
    const auto north = projection.Project(51.1, 2.0), south = projection.Project(41.3, 2.0);
    const double width = east.x - west.x, height = south.y - north.y;
    CHECK(Near(width / height, 14.8 * projection.cosine / 9.8, 1e-4));
    // Centred inside the area and touching it on one axis.
    CHECK(projection.drawn.left >= area.left - 0.01f);
    CHECK(projection.drawn.right <= area.right + 0.01f);
    CHECK(projection.drawn.top >= area.top - 0.01f);
    CHECK(projection.drawn.bottom <= area.bottom + 0.01f);
    const bool fillsWidth = std::abs(projection.drawn.left - area.left) < 0.01f && std::abs(projection.drawn.right - area.right) < 0.01f;
    const bool fillsHeight = std::abs(projection.drawn.top - area.top) < 0.01f && std::abs(projection.drawn.bottom - area.bottom) < 0.01f;
    CHECK(fillsWidth || fillsHeight);
    CHECK(Near((projection.drawn.left + projection.drawn.right) / 2.0, (area.left + area.right) / 2.0, 0.01));
    CHECK(Near((projection.drawn.top + projection.drawn.bottom) / 2.0, (area.top + area.bottom) / 2.0, 0.01));
    // Paris lands north of Marseille and west of Strasbourg; Unproject undoes Project.
    const auto paris = projection.Project(48.8566, 2.3522);
    const auto marseille = projection.Project(43.2965, 5.3698);
    const auto strasbourg = projection.Project(48.5734, 7.7521);
    CHECK(paris.y < marseille.y);
    CHECK(paris.x < strasbourg.x);
    const auto [latitude, longitude] = projection.Unproject(paris);
    CHECK(Near(latitude, 48.8566, 1e-4));
    CHECK(Near(longitude, 2.3522, 1e-4));
}

AETHER_TEST("weather_ui map: an empty view or area never divides by zero") {
    const auto point = MakeProjection(GeoBounds{2.0, 48.0, 2.0, 48.0}, D2D1::RectF(0, 0, 100, 100));
    CHECK(std::isfinite(point.scale));
    const auto at = point.Project(48.0, 2.0);
    CHECK(std::isfinite(at.x));
    CHECK(std::isfinite(at.y));
    const auto flat = MakeProjection(GeoBounds{-5.0, 41.0, 9.0, 51.0}, D2D1::RectF(0, 0, 0, 0));
    CHECK(std::isfinite(flat.scale));
    CHECK(flat.scale > 0.0);
}

AETHER_TEST("weather_ui map: temperature colours run from blue through green and yellow to red, in either unit") {
    const auto cold = TemperatureColor(-30.0, status::TemperatureUnit::Celsius);
    const auto freezing = TemperatureColor(0.0, status::TemperatureUnit::Celsius);
    const auto mild = TemperatureColor(15.0, status::TemperatureUnit::Celsius);
    const auto hot = TemperatureColor(38.0, status::TemperatureUnit::Celsius);
    CHECK(cold.b > cold.r);
    CHECK(freezing.b > freezing.r);
    CHECK(mild.g > mild.b);
    CHECK(hot.r > hot.b);
    CHECK(hot.r > hot.g);
    // Fahrenheit is the same colour once converted.
    const auto celsius = TemperatureColor(20.0, status::TemperatureUnit::Celsius);
    const auto fahrenheit = TemperatureColor(68.0, status::TemperatureUnit::Fahrenheit);
    CHECK(Near(celsius.r, fahrenheit.r, 1e-5));
    CHECK(Near(celsius.g, fahrenheit.g, 1e-5));
    CHECK(Near(celsius.b, fahrenheit.b, 1e-5));
    // Opaque and continuous: one degree changes no channel by more than a tenth.
    for (double value = -25.0; value < 45.0; value += 1.0) {
        const auto a = TemperatureColor(value, status::TemperatureUnit::Celsius);
        const auto b = TemperatureColor(value + 1.0, status::TemperatureUnit::Celsius);
        CHECK_EQ(a.a, 1.0f);
        CHECK(std::abs(a.r - b.r) < 0.1f);
        CHECK(std::abs(a.g - b.g) < 0.1f);
        CHECK(std::abs(a.b - b.b) < 0.1f);
    }
    const auto invalid = TemperatureColor(std::nan(""), status::TemperatureUnit::Celsius);
    CHECK(std::isfinite(invalid.r));
}

AETHER_TEST("weather_ui map: interpolation stays between the samples and keeps each sample's own value") {
    const std::array<TemperatureSample, 3> samples{{{48.86, 2.35, 9.0}, {43.30, 5.37, 17.0}, {48.57, 7.75, 6.0}}};
    CHECK_FALSE(InterpolateTemperature(std::span<const TemperatureSample>{}, 46.0, 2.0).has_value());
    CHECK(Near(*InterpolateTemperature(samples, 43.30, 5.37), 17.0));
    const auto lyon = InterpolateTemperature(samples, 45.76, 4.84, std::cos(46.0 * 3.14159265358979 / 180.0));
    REQUIRE(lyon.has_value());
    CHECK(*lyon > 6.0);
    CHECK(*lyon < 17.0);
    // Nearer a sample means nearer its value.
    const auto nearParis = InterpolateTemperature(samples, 48.7, 2.4);
    const auto nearMarseille = InterpolateTemperature(samples, 43.5, 5.3);
    CHECK(std::abs(*nearParis - 9.0) < std::abs(*nearParis - 17.0));
    CHECK(std::abs(*nearMarseille - 17.0) < std::abs(*nearMarseille - 9.0));
}
