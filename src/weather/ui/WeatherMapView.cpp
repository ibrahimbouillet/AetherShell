// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "weather/ui/WeatherMapView.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace aether::weather::ui {

D2D1_POINT_2F MapProjection::Project(double latitude, double longitude) const noexcept {
    const double x = drawn.left + (longitude - view.west) * cosine * scale;
    const double y = drawn.top + (view.north - latitude) * scale;
    return D2D1::Point2F(static_cast<float>(x), static_cast<float>(y));
}

std::pair<double, double> MapProjection::Unproject(D2D1_POINT_2F point) const noexcept {
    const double longitude = view.west + (point.x - drawn.left) / (cosine * scale);
    const double latitude = view.north - (point.y - drawn.top) / scale;
    return {latitude, longitude};
}

MapProjection MakeProjection(const GeoBounds& view, const D2D1_RECT_F& area) noexcept {
    MapProjection projection;
    projection.view = view;
    projection.area = area;
    const double middle = std::clamp((view.south + view.north) / 2.0, -80.0, 80.0);
    projection.cosine = std::cos(middle * std::numbers::pi / 180.0);
    const double width = std::max(1e-6, view.Width() * projection.cosine);
    const double height = std::max(1e-6, view.Height());
    const double areaWidth = std::max(1.0, static_cast<double>(area.right - area.left));
    const double areaHeight = std::max(1.0, static_cast<double>(area.bottom - area.top));
    projection.scale = std::min(areaWidth / width, areaHeight / height);
    const double drawnWidth = width * projection.scale, drawnHeight = height * projection.scale;
    const double left = area.left + (areaWidth - drawnWidth) / 2.0;
    const double top = area.top + (areaHeight - drawnHeight) / 2.0;
    projection.drawn = D2D1::RectF(static_cast<float>(left), static_cast<float>(top), static_cast<float>(left + drawnWidth),
                                   static_cast<float>(top + drawnHeight));
    return projection;
}

graphics::Color TemperatureColor(double value, status::TemperatureUnit unit) noexcept {
    double celsius = unit == status::TemperatureUnit::Fahrenheit ? (value - 32.0) * 5.0 / 9.0 : value;
    if (!std::isfinite(celsius)) celsius = 10.0;
    struct Stop {
        double celsius;
        unsigned rgb;
    };
    static constexpr std::array<Stop, 9> kStops{{{-20, 0x3B4CC0}, {-5, 0x5A8DEB}, {0, 0x7EC8F0}, {8, 0x8FD9A8},
                                                 {15, 0xD6E66E}, {22, 0xF9C74F}, {28, 0xF8963F}, {35, 0xE8553E},
                                                 {42, 0xB5203B}}};
    if (celsius <= kStops.front().celsius) return graphics::Color::FromHex(kStops.front().rgb);
    for (std::size_t i = 1; i < kStops.size(); ++i) {
        if (celsius <= kStops[i].celsius) {
            const auto a = graphics::Color::FromHex(kStops[i - 1].rgb), b = graphics::Color::FromHex(kStops[i].rgb);
            const float t = static_cast<float>((celsius - kStops[i - 1].celsius) / (kStops[i].celsius - kStops[i - 1].celsius));
            return graphics::Color{a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, 1.0f};
        }
    }
    return graphics::Color::FromHex(kStops.back().rgb);
}

std::optional<double> InterpolateTemperature(std::span<const TemperatureSample> samples, double latitude, double longitude,
                                             double cosine) noexcept {
    if (samples.empty()) return std::nullopt;
    double weights = 0.0, total = 0.0;
    for (const auto& sample : samples) {
        const double dx = (sample.longitude - longitude) * cosine;
        const double dy = sample.latitude - latitude;
        const double distance = dx * dx + dy * dy;
        if (distance < 1e-10) return sample.value;
        const double weight = 1.0 / distance;
        weights += weight;
        total += weight * sample.value;
    }
    return total / weights;
}

}  // namespace aether::weather::ui
