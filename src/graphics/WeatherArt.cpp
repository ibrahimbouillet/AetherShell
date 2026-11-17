// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "graphics/WeatherArt.h"

#include "graphics/Glyphs.h"
#include "graphics/WinRT.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace aether::graphics {
namespace {

constexpr float kGrid = 16.0f;
constexpr float kTau = 6.28318531f;
constexpr float kPi = 3.14159265f;

D2D1_POINT_2F P(float x, float y) noexcept { return D2D1::Point2F(x, y); }
float Frac(double value) noexcept { return static_cast<float>(value - std::floor(value)); }
float Smooth(float from, float to, float x) noexcept {
    const float t = std::clamp((x - from) / (to - from), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}
Color Mix(const Color& a, const Color& b, float t) noexcept {
    return Color{a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
}
float Luminance(const Color& c) noexcept { return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b; }
float Wave(double clock, double period, float phase = 0.0f) noexcept {
    return std::sin(static_cast<float>(clock / period) * kTau + phase);
}
D2D1_MATRIX_3X2_F Place(float scale, float x, float y) noexcept {
    return D2D1::Matrix3x2F::Scale(scale, scale) * D2D1::Matrix3x2F::Translation(x, y);
}

// Geometry built once per Direct2D factory (a few factories exist at most).
struct ArtCache {
    winrt::com_ptr<ID2D1Factory> factory;
    winrt::com_ptr<ID2D1PathGeometry> cloud;      // x 0.5..15.5, y 2..11, flat base at y 11
    winrt::com_ptr<ID2D1PathGeometry> cloudMask;  // a wide square less the cloud and a gap around it
    winrt::com_ptr<ID2D1PathGeometry> moon;       // the Top Bar's crescent on the 16 grid
    winrt::com_ptr<ID2D1PathGeometry> bolt;       // filled bolt, 4 x 6.4 from its origin
    winrt::com_ptr<ID2D1StrokeStyle> round;
};
thread_local std::vector<ArtCache> artCaches;

template <class Build>
winrt::com_ptr<ID2D1PathGeometry> MakePath(ID2D1Factory* factory, Build&& build) {
    winrt::com_ptr<ID2D1PathGeometry> path;
    winrt::com_ptr<ID2D1GeometrySink> sink;
    if (FAILED(factory->CreatePathGeometry(path.put())) || FAILED(path->Open(sink.put()))) return nullptr;
    if (!build(sink.get())) return nullptr;
    if (FAILED(sink->Close())) return nullptr;
    return path;
}

bool BuildCache(ArtCache& cache) {
    ID2D1Factory* factory = cache.factory.get();
    const auto properties = D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
                                                        D2D1_LINE_JOIN_ROUND);
    if (FAILED(factory->CreateStrokeStyle(properties, nullptr, 0, cache.round.put()))) return false;
    cache.cloud = MakePath(factory, [](ID2D1GeometrySink* sink) {
        sink->SetFillMode(D2D1_FILL_MODE_WINDING);
        sink->BeginFigure(P(4, 11), D2D1_FIGURE_BEGIN_FILLED);
        sink->AddBezier(D2D1::BezierSegment(P(0.5f, 11), P(0.5f, 6), P(4.5f, 6.5f)));
        sink->AddBezier(D2D1::BezierSegment(P(5, 2), P(11, 2), P(11.5f, 7)));
        sink->AddBezier(D2D1::BezierSegment(P(15, 6), P(15.5f, 11), P(12, 11)));
        sink->AddLine(P(4, 11));
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        return true;
    });
    cache.moon = MakePath(factory, [](ID2D1GeometrySink* sink) {
        sink->BeginFigure(P(10.5f, 2), D2D1_FIGURE_BEGIN_FILLED);
        sink->AddBezier(D2D1::BezierSegment(P(2, 1), P(0.5f, 11), P(7.5f, 13.75f)));
        sink->AddBezier(D2D1::BezierSegment(P(10.5f, 15), P(13, 12.5f), P(14, 10)));
        sink->AddBezier(D2D1::BezierSegment(P(8, 12), P(5, 5), P(10.5f, 2)));
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        return true;
    });
    cache.bolt = MakePath(factory, [](ID2D1GeometrySink* sink) {
        const D2D1_POINT_2F points[] = {P(0, 3.7f), P(1.95f, 3.7f), P(0.7f, 6.4f), P(4.1f, 2.35f), P(2.15f, 2.35f), P(3.55f, 0)};
        sink->BeginFigure(P(2.55f, 0), D2D1_FIGURE_BEGIN_FILLED);
        sink->AddLines(points, static_cast<UINT32>(std::size(points)));
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        return true;
    });
    if (!cache.cloud || !cache.moon || !cache.bolt) return false;
    // The gap around the cloud keeps what it hides from touching its outline.
    auto widened = MakePath(factory, [&](ID2D1GeometrySink* sink) {
        return SUCCEEDED(cache.cloud->Widen(2.6f, cache.round.get(), nullptr, 0.02f, sink));
    });
    if (!widened) return false;
    auto expanded = MakePath(factory, [&](ID2D1GeometrySink* sink) {
        return SUCCEEDED(cache.cloud->CombineWithGeometry(widened.get(), D2D1_COMBINE_MODE_UNION, nullptr, 0.02f, sink));
    });
    winrt::com_ptr<ID2D1RectangleGeometry> square;
    if (!expanded || FAILED(factory->CreateRectangleGeometry(D2D1::RectF(-64, -64, 80, 80), square.put()))) return false;
    cache.cloudMask = MakePath(factory, [&](ID2D1GeometrySink* sink) {
        return SUCCEEDED(square->CombineWithGeometry(expanded.get(), D2D1_COMBINE_MODE_EXCLUDE, nullptr, 0.02f, sink));
    });
    return cache.cloudMask != nullptr;
}

ArtCache* CacheFor(ID2D1Factory* factory) {
    if (!factory) return nullptr;
    for (auto& cache : artCaches) {
        if (cache.factory.get() == factory) return cache.cloudMask ? &cache : nullptr;
    }
    if (artCaches.size() >= 4) artCaches.erase(artCaches.begin());
    ArtCache cache;
    cache.factory.copy_from(factory);
    const bool built = BuildCache(cache);
    artCaches.push_back(std::move(cache));
    return built ? &artCaches.back() : nullptr;
}

enum class CloudTone { Light, Grey, Back, Storm, StormBack };

struct Palette {
    Color top, bottom, edge;
};
Palette CloudPalette(CloudTone tone, bool day) noexcept {
    switch (tone) {
        case CloudTone::Light:
            return day ? Palette{Color::FromHex(0xFFFFFF), Color::FromHex(0xDCE5F1), Color::FromHex(0x7F95B3, 0.35f)}
                       : Palette{Color::FromHex(0xDCE3F0), Color::FromHex(0x9FAAC4), Color::FromHex(0x55617F, 0.4f)};
        case CloudTone::Grey:
            return day ? Palette{Color::FromHex(0xF1F4F8), Color::FromHex(0xB9C4D3), Color::FromHex(0x6F7F96, 0.4f)}
                       : Palette{Color::FromHex(0xC3CBDA), Color::FromHex(0x8792AB), Color::FromHex(0x4B5571, 0.45f)};
        case CloudTone::Back:
            return day ? Palette{Color::FromHex(0xCDD5E0), Color::FromHex(0x9AA6B8), Color::FromHex(0x66748A, 0.35f)}
                       : Palette{Color::FromHex(0x9BA5BA), Color::FromHex(0x6B7590), Color::FromHex(0x3E4760, 0.4f)};
        case CloudTone::Storm:
            return {Color::FromHex(0x9AA3B3), Color::FromHex(0x5C6678), Color::FromHex(0x323A48, 0.5f)};
        case CloudTone::StormBack:
            return {Color::FromHex(0x737D8E), Color::FromHex(0x485162), Color::FromHex(0x262C38, 0.5f)};
    }
    return {};
}

enum class Drop { Streak, Dot, Flake, Grain, Hail };

struct Painter {
    ID2D1DeviceContext* dc = nullptr;
    D2D1_MATRIX_3X2_F grid{};
    ID2D1SolidColorBrush* brush = nullptr;
    ArtCache* cache = nullptr;
    WeatherArtStyle style = WeatherArtStyle::Monochrome;
    Color ink;
    WeatherArtMotion motion;
    float stroke = 1.5f;
    bool day = true;

    [[nodiscard]] bool Mono() const noexcept { return style == WeatherArtStyle::Monochrome; }
    [[nodiscard]] float Energy() const noexcept { return std::clamp(motion.energy, 0.0f, 1.0f); }
    void Grid() const { dc->SetTransform(grid); }
    void At(const D2D1_MATRIX_3X2_F& local) const { dc->SetTransform(local * grid); }
    ID2D1SolidColorBrush* Paint(const Color& color, float alpha = 1.0f) const {
        brush->SetColor(color.WithAlpha(color.a * alpha).ToD2D());
        return brush;
    }
    ID2D1SolidColorBrush* Ink(float alpha = 1.0f) const { return Paint(ink, alpha); }
    void Line(D2D1_POINT_2F a, D2D1_POINT_2F b, ID2D1Brush* with, float width) const {
        dc->DrawLine(a, b, with, width, cache->round.get());
    }
    winrt::com_ptr<ID2D1LinearGradientBrush> Linear(D2D1_POINT_2F from, D2D1_POINT_2F to, const Color& a, const Color& b,
                                                    float alpha = 1.0f) const {
        const D2D1_GRADIENT_STOP stops[] = {{0.0f, a.WithAlpha(a.a * alpha).ToD2D()}, {1.0f, b.WithAlpha(b.a * alpha).ToD2D()}};
        winrt::com_ptr<ID2D1GradientStopCollection> collection;
        winrt::com_ptr<ID2D1LinearGradientBrush> gradient;
        if (FAILED(dc->CreateGradientStopCollection(stops, 2, collection.put())) ||
            FAILED(dc->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(from, to), collection.get(), gradient.put()))) {
            return nullptr;
        }
        return gradient;
    }
    winrt::com_ptr<ID2D1RadialGradientBrush> Radial(D2D1_POINT_2F centre, float radius, const Color& a, const Color& b,
                                                    D2D1_POINT_2F offset = {}) const {
        const D2D1_GRADIENT_STOP stops[] = {{0.0f, a.ToD2D()}, {1.0f, b.ToD2D()}};
        winrt::com_ptr<ID2D1GradientStopCollection> collection;
        winrt::com_ptr<ID2D1RadialGradientBrush> gradient;
        if (FAILED(dc->CreateGradientStopCollection(stops, 2, collection.put())) ||
            FAILED(dc->CreateRadialGradientBrush(D2D1::RadialGradientBrushProperties(centre, offset, radius, radius),
                                                 collection.get(), gradient.put()))) {
            return nullptr;
        }
        return gradient;
    }

    // Draws what sits behind the cloud placed at `cloud`: in Monochrome through the cloud's mask.
    template <class Draw>
    void Behind(const D2D1_MATRIX_3X2_F& cloud, Draw&& draw) const {
        if (!Mono()) {
            draw();
            return;
        }
        Grid();
        dc->PushLayer(D2D1::LayerParameters1(D2D1::InfiniteRect(), cache->cloudMask.get(), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
                                             cloud, 1.0f, nullptr, D2D1_LAYER_OPTIONS1_NONE),
                      nullptr);
        draw();
        Grid();
        dc->PopLayer();
    }

    void Cloud(const D2D1_MATRIX_3X2_F& place, CloudTone tone, float flash = 0.0f) const {
        const float scale = std::max(0.1f, place._11);
        At(place);
        if (Mono()) {
            dc->DrawGeometry(cache->cloud.get(), Ink(), stroke / scale, cache->round.get());
            return;
        }
        const auto palette = CloudPalette(tone, day);
        if (auto fill = Linear(P(0, 3), P(0, 11.5f), palette.top, palette.bottom)) dc->FillGeometry(cache->cloud.get(), fill.get());
        if (flash > 0.01f) dc->FillGeometry(cache->cloud.get(), Paint(Color::FromHex(0xFFF7D6), 0.55f * flash));
        // A light along the top and a defined edge keep a white cloud readable on a pale sky.
        if (auto sheen = Linear(P(0, 2.5f), P(0, 7.5f), Color::FromHex(0xFFFFFF, 0.55f), Color::FromHex(0xFFFFFF, 0.0f))) {
            dc->FillGeometry(cache->cloud.get(), sheen.get());
        }
        dc->DrawGeometry(cache->cloud.get(), Paint(palette.edge), 0.42f / scale, cache->round.get());
    }

    void Sun(D2D1_POINT_2F centre, float core, float inner, float outer) const {
        Grid();
        const float energy = Energy();
        const float turn = static_cast<float>(std::fmod(motion.clock, 16.0) / 16.0) * kTau;
        const float breath = 1.0f + 0.07f * energy * Wave(motion.clock, 2.4);
        if (Mono()) {
            dc->DrawEllipse(D2D1::Ellipse(centre, core, core), Ink(), stroke, cache->round.get());
            for (int i = 0; i < 8; ++i) {
                const float angle = turn + static_cast<float>(i) * kPi / 4.0f;
                const float c = std::cos(angle), s = std::sin(angle);
                Line(P(centre.x + c * inner, centre.y + s * inner),
                     P(centre.x + c * outer * breath, centre.y + s * outer * breath), brush, stroke);
            }
            return;
        }
        if (auto glow = Radial(centre, outer * 1.25f, Color::FromHex(0xFFD54A, 0.42f), Color::FromHex(0xFFD54A, 0.0f))) {
            dc->FillEllipse(D2D1::Ellipse(centre, outer * 1.25f, outer * 1.25f), glow.get());
        }
        for (int i = 0; i < 8; ++i) {
            const float angle = turn + static_cast<float>(i) * kPi / 4.0f;
            const float reach = (i % 2 == 0 ? outer : inner + (outer - inner) * 0.72f) * breath;
            const float c = std::cos(angle), s = std::sin(angle);
            Line(P(centre.x + c * inner, centre.y + s * inner), P(centre.x + c * reach, centre.y + s * reach),
                 Paint(Color::FromHex(0xFFB72B)), std::max(0.9f, core * 0.42f));
        }
        if (auto body = Radial(centre, core * 1.25f, Color::FromHex(0xFFF2A8), Color::FromHex(0xFFAE1F),
                               P(-core * 0.35f, -core * 0.35f))) {
            dc->FillEllipse(D2D1::Ellipse(centre, core, core), body.get());
        }
        dc->DrawEllipse(D2D1::Ellipse(centre, core, core), Paint(Color::FromHex(0xF59E0B, 0.55f)), 0.3f);
    }

    void Moon(D2D1_POINT_2F centre, float scale) const {
        const float rock = 0.09f * Energy() * Wave(motion.clock, 6.0);
        const auto place = D2D1::Matrix3x2F::Translation(-8, -8) * D2D1::Matrix3x2F::Rotation(rock * 180.0f / kPi) *
                           D2D1::Matrix3x2F::Scale(scale, scale) * D2D1::Matrix3x2F::Translation(centre.x, centre.y);
        if (Mono()) {
            At(place);
            dc->DrawGeometry(cache->moon.get(), Ink(), stroke / scale, cache->round.get());
            return;
        }
        Grid();
        if (auto glow = Radial(centre, 8.5f * scale, Color::FromHex(0xFFF1C2, 0.32f), Color::FromHex(0xFFF1C2, 0.0f))) {
            dc->FillEllipse(D2D1::Ellipse(centre, 8.5f * scale, 8.5f * scale), glow.get());
        }
        At(place);
        if (auto fill = Linear(P(3, 2), P(11, 14), Color::FromHex(0xFFF9E3), Color::FromHex(0xE6D7A6))) {
            dc->FillGeometry(cache->moon.get(), fill.get());
        }
        for (const auto& crater : {std::array<float, 3>{4.2f, 8.4f, 0.95f}, std::array<float, 3>{5.4f, 11.5f, 0.6f},
                                   std::array<float, 3>{4.9f, 4.9f, 0.5f}}) {
            dc->FillEllipse(D2D1::Ellipse(P(crater[0], crater[1]), crater[2], crater[2]), Paint(Color::FromHex(0xD2C08A, 0.55f)));
        }
        dc->DrawGeometry(cache->moon.get(), Paint(Color::FromHex(0xB89F5A, 0.45f)), 0.35f / scale, cache->round.get());
    }

    void Star(D2D1_POINT_2F at, float size, float phase) const {
        const float twinkle = 1.0f - Energy() * 0.6f * (0.5f - 0.5f * std::cos(static_cast<float>(motion.clock / 1.9) * kTau + phase));
        const float reach = size * (0.85f + 0.15f * twinkle);
        Grid();
        if (Mono()) {
            auto* pen = Ink(twinkle);
            Line(P(at.x - reach, at.y), P(at.x + reach, at.y), pen, stroke * 0.7f);
            Line(P(at.x, at.y - reach), P(at.x, at.y + reach), pen, stroke * 0.7f);
            return;
        }
        auto* light = Paint(Color::FromHex(0xFFF6CF), twinkle);
        dc->FillEllipse(D2D1::Ellipse(at, reach, reach * 0.26f), light);
        dc->FillEllipse(D2D1::Ellipse(at, reach * 0.26f, reach), light);
        dc->FillEllipse(D2D1::Ellipse(at, reach * 0.32f, reach * 0.32f), Paint(Color::FromHex(0xFFFFFF), twinkle));
    }

    void FogBands(bool rime) const {
        Grid();
        const float energy = Energy();
        const std::array<std::array<float, 3>, 2> bands{{{2.6f, 13.4f, 11.9f}, {4.2f, 11.8f, 14.3f}}};
        for (std::size_t k = 0; k < bands.size(); ++k) {
            const float drift = 0.85f * energy * Wave(motion.clock, 5.0, static_cast<float>(k) * kPi);
            const float left = bands[k][0] + drift, right = bands[k][1] + drift, y = bands[k][2];
            if (Mono()) {
                Line(P(left, y), P(right, y), Ink(), stroke);
            } else {
                const Color tint = day ? Color::FromHex(0xE4E9F0, 0.95f) : Color::FromHex(0xAEB8CB, 0.92f);
                dc->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(left, y - 0.68f, right, y + 0.68f), 0.68f, 0.68f), Paint(tint));
            }
        }
        if (rime) Crystal(P(14.4f, 13.1f), 0.75f, 0.0f, 1.0f);
    }

    void Crystal(D2D1_POINT_2F at, float radius, float angle, float alpha) const {
        Grid();
        for (int arm = 0; arm < 3; ++arm) {
            const float a = angle + static_cast<float>(arm) * kPi / 3.0f;
            const float c = std::cos(a) * radius, s = std::sin(a) * radius;
            if (Mono()) {
                Line(P(at.x - c, at.y - s), P(at.x + c, at.y + s), Ink(alpha), stroke * 0.75f);
            } else {
                Line(P(at.x - c, at.y - s), P(at.x + c, at.y + s), Paint(Color::FromHex(0x8DB9E3), alpha), 0.85f);
                Line(P(at.x - c, at.y - s), P(at.x + c, at.y + s), Paint(Color::FromHex(0xFFFFFF), alpha), 0.42f);
            }
        }
    }

    void DrawDrop(Drop kind, float x, float y, float alpha, float angle, bool freezing, int intensity) const {
        if (alpha <= 0.01f) return;
        switch (kind) {
            case Drop::Streak: {
                const D2D1_POINT_2F a = P(x, y), b = P(x - 0.85f, y + 2.0f);
                if (Mono()) Line(a, b, Ink(alpha), stroke);
                else Line(a, b, Paint(freezing ? Color::FromHex(0x7CC8F2) : intensity >= 3 ? Color::FromHex(0x2C7EE0)
                                                                                          : Color::FromHex(0x3E95F5), alpha), 1.05f);
                break;
            }
            case Drop::Dot:
                dc->FillEllipse(D2D1::Ellipse(P(x, y), Mono() ? 0.85f : 0.72f, Mono() ? 0.85f : 0.72f),
                                Mono() ? Ink(alpha) : Paint(freezing ? Color::FromHex(0x8ED0F4) : Color::FromHex(0x5EAAF6), alpha));
                break;
            case Drop::Flake: Crystal(P(x, y), Mono() ? 1.55f : 1.2f, angle, alpha); break;  // legible at 18 px
            case Drop::Grain:
                if (Mono()) dc->FillEllipse(D2D1::Ellipse(P(x, y), 0.65f, 0.65f), Ink(alpha));
                else {
                    dc->FillEllipse(D2D1::Ellipse(P(x, y), 0.55f, 0.55f), Paint(Color::FromHex(0xF4F8FF), alpha));
                    dc->DrawEllipse(D2D1::Ellipse(P(x, y), 0.55f, 0.55f), Paint(Color::FromHex(0x8DB9E3), alpha), 0.25f);
                }
                break;
            case Drop::Hail:
                if (Mono()) dc->DrawEllipse(D2D1::Ellipse(P(x, y), 0.8f, 0.8f), Ink(alpha), stroke * 0.8f);
                else {
                    dc->FillEllipse(D2D1::Ellipse(P(x, y), 0.85f, 0.85f), Paint(Color::FromHex(0xEAF6FF), alpha));
                    dc->DrawEllipse(D2D1::Ellipse(P(x, y), 0.85f, 0.85f), Paint(Color::FromHex(0x77A9D8), alpha), 0.32f);
                }
                break;
        }
        Grid();
    }

    // Particles falling from `top` across [left, right]; the still pose rests them at two heights.
    void Fall(Drop kind, float left, float right, float top, int count, float speed, bool freezing, int intensity,
              const std::vector<float>& explicitX = {}) const {
        Grid();
        dc->PushAxisAlignedClip(D2D1::RectF(-1.0f, top - 0.4f, 17.0f, 16.6f), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        const float energy = Energy();
        const float travel = 16.4f - top;
        const int n = explicitX.empty() ? count : static_cast<int>(explicitX.size());
        for (int i = 0; i < n; ++i) {
            const float x0 = explicitX.empty() ? left + (static_cast<float>(i) + 0.5f) * (right - left) / static_cast<float>(n)
                                               : explicitX[static_cast<std::size_t>(i)];
            const float still = (i % 2 == 0 ? 0.22f : 0.45f) + 0.03f * static_cast<float>(i % 3);
            // A freezing scene mixes ice in: its middle particle is a crystal.
            const Drop drop = freezing && i == n / 2 ? Drop::Flake : kind;
            const float slant = drop == Drop::Streak ? -0.425f : 0.0f;
            if (energy < 0.999f) {
                const float y = top + still * travel;
                DrawDrop(drop, x0 + slant * (y - top), y, 1.0f - energy, 0.0f, freezing, intensity);
            }
            if (energy > 0.001f) {
                const float p = Frac(still + motion.clock * speed + 0.0);
                const float fade = Smooth(0.0f, 0.14f, p) * (1.0f - Smooth(0.6f, 0.96f, p));
                const float y = top + p * travel;
                const float sway = drop == Drop::Flake || drop == Drop::Grain
                                       ? 0.55f * Wave(motion.clock, 2.2, static_cast<float>(i) * 1.7f)
                                       : 0.0f;
                const float spin = static_cast<float>(std::fmod(motion.clock * 0.9 + i, static_cast<double>(kTau)));
                DrawDrop(drop, x0 + slant * (y - top) + sway, y, energy * fade, spin, freezing, intensity);
            }
        }
        Grid();
        dc->PopAxisAlignedClip();
    }

    // How bright the bolt is: steady, then a double flash every 3.2 seconds (never more than three a second).
    [[nodiscard]] float Flicker() const noexcept {
        const float tau = Frac(motion.clock / 3.2) * 3.2f;
        float level = 1.0f;
        if (tau >= 2.2f && tau < 2.42f) level = 0.2f;
        else if (tau >= 2.42f && tau < 2.52f) level = 1.0f;
        else if (tau >= 2.52f && tau < 2.66f) level = 0.25f;
        else if (tau >= 2.66f) level = 0.25f + (tau - 2.66f) / 0.54f * 0.75f;
        return 1.0f + (level - 1.0f) * Energy();
    }
    [[nodiscard]] float Flash() const noexcept {
        const float tau = Frac(motion.clock / 3.2) * 3.2f;
        return tau >= 2.42f && tau < 2.52f ? Energy() : 0.0f;
    }

    void Bolt(float alpha) const {
        if (Mono()) {
            Grid();
            auto* pen = Ink(alpha);
            Line(P(9.75f, 10.75f), P(7.25f, 13), pen, stroke);
            Line(P(7.25f, 13), P(10.25f, 13), pen, stroke);
            Line(P(10.25f, 13), P(8.25f, 15), pen, stroke);
            return;
        }
        Grid();
        if (auto glow = Radial(P(9.1f, 12.9f), 4.2f, Color::FromHex(0xFFE27A, 0.5f * alpha), Color::FromHex(0xFFE27A, 0.0f))) {
            dc->FillEllipse(D2D1::Ellipse(P(9.1f, 12.9f), 4.2f, 4.2f), glow.get());
        }
        At(D2D1::Matrix3x2F::Translation(7.0f, 9.7f));
        dc->FillGeometry(cache->bolt.get(), Paint(Color::FromHex(0xFFD43B), alpha));
        dc->DrawGeometry(cache->bolt.get(), Paint(Color::FromHex(0xE89A00), alpha), 0.3f, cache->round.get());
        Grid();
    }
};

}  // namespace

WeatherArtSubject WeatherSubjectForCode(std::optional<int> wmoCode, std::optional<bool> isDay) noexcept {
    WeatherArtSubject subject;
    subject.isDay = isDay.value_or(true);
    if (!wmoCode) return subject;
    const auto set = [&](WeatherScene scene, int intensity) {
        subject.scene = scene;
        subject.intensity = intensity;
    };
    switch (*wmoCode) {
        case 0: set(WeatherScene::Clear, 2); break;
        case 1: set(WeatherScene::MainlyClear, 2); break;
        case 2: set(WeatherScene::PartlyCloudy, 2); break;
        case 3: set(WeatherScene::Overcast, 2); break;
        case 45: set(WeatherScene::Fog, 2); break;
        case 48: set(WeatherScene::RimeFog, 2); break;
        case 51: set(WeatherScene::Drizzle, 1); break;
        case 53: set(WeatherScene::Drizzle, 2); break;
        case 55: set(WeatherScene::Drizzle, 3); break;
        case 56: set(WeatherScene::FreezingDrizzle, 1); break;
        case 57: set(WeatherScene::FreezingDrizzle, 3); break;
        case 61: set(WeatherScene::Rain, 1); break;
        case 63: set(WeatherScene::Rain, 2); break;
        case 65: set(WeatherScene::Rain, 3); break;
        case 66: set(WeatherScene::FreezingRain, 1); break;
        case 67: set(WeatherScene::FreezingRain, 3); break;
        case 71: set(WeatherScene::Snow, 1); break;
        case 73: set(WeatherScene::Snow, 2); break;
        case 75: set(WeatherScene::Snow, 3); break;
        case 77: set(WeatherScene::SnowGrains, 2); break;
        case 80: set(WeatherScene::RainShowers, 1); break;
        case 81: set(WeatherScene::RainShowers, 2); break;
        case 82: set(WeatherScene::RainShowers, 3); break;
        case 85: set(WeatherScene::SnowShowers, 1); break;
        case 86: set(WeatherScene::SnowShowers, 3); break;
        case 95: set(WeatherScene::Thunderstorm, 2); break;
        case 96: set(WeatherScene::ThunderstormHail, 1); break;
        case 99: set(WeatherScene::ThunderstormHail, 3); break;
        default: break;
    }
    return subject;
}

std::string_view ToString(WeatherScene scene) noexcept {
    switch (scene) {
        case WeatherScene::Clear: return "clear";
        case WeatherScene::MainlyClear: return "mainly-clear";
        case WeatherScene::PartlyCloudy: return "partly-cloudy";
        case WeatherScene::Overcast: return "overcast";
        case WeatherScene::Fog: return "fog";
        case WeatherScene::RimeFog: return "rime-fog";
        case WeatherScene::Drizzle: return "drizzle";
        case WeatherScene::FreezingDrizzle: return "freezing-drizzle";
        case WeatherScene::Rain: return "rain";
        case WeatherScene::FreezingRain: return "freezing-rain";
        case WeatherScene::Snow: return "snow";
        case WeatherScene::SnowGrains: return "snow-grains";
        case WeatherScene::RainShowers: return "rain-showers";
        case WeatherScene::SnowShowers: return "snow-showers";
        case WeatherScene::Thunderstorm: return "thunderstorm";
        case WeatherScene::ThunderstormHail: return "thunderstorm-hail";
        case WeatherScene::Unknown: return "unknown";
    }
    return "unknown";
}

bool WeatherArtMoves(const WeatherArtSubject& subject) noexcept { return subject.scene != WeatherScene::Unknown; }

void PaintWeatherArt(ID2D1DeviceContext* target, const D2D1_RECT_F& box, const WeatherArtSubject& subject,
                     WeatherArtStyle style, const Color& ink, WeatherArtMotion motion, float strokeWidth, bool cached) {
    if (!target) return;
    const float width = box.right - box.left, height = box.bottom - box.top;
    if (!std::isfinite(width) || !std::isfinite(height) || width < 1.0f || height < 1.0f) return;
    if (subject.scene == WeatherScene::Unknown) {
        winrt::com_ptr<ID2D1SolidColorBrush> brush;
        const Color color = style == WeatherArtStyle::Color ? Color::FromHex(0x8A94A6) : ink;
        if (SUCCEEDED(target->CreateSolidColorBrush(color.ToD2D(), brush.put()))) {
            PaintWeatherGlyph(target, box, brush.get(), WeatherSymbol::Unknown, std::nullopt, cached, strokeWidth);
        }
        return;
    }
    winrt::com_ptr<ID2D1Factory> factory;
    target->GetFactory(factory.put());
    ArtCache* cache = CacheFor(factory.get());
    winrt::com_ptr<ID2D1SolidColorBrush> brush;
    if (!cache || FAILED(target->CreateSolidColorBrush(ink.ToD2D(), brush.put()))) return;

    D2D1_MATRIX_3X2_F previous{};
    target->GetTransform(&previous);
    const float scale = std::min(width, height) / kGrid;
    const float x = box.left + (width - kGrid * scale) / 2.0f;
    const float y = box.top + (height - kGrid * scale) / 2.0f;

    Painter paint;
    paint.dc = target;
    paint.grid = D2D1::Matrix3x2F::Scale(scale, scale) * D2D1::Matrix3x2F::Translation(x, y) * previous;
    paint.brush = brush.get();
    paint.cache = cache;
    paint.style = style;
    paint.ink = ink;
    paint.motion = motion;
    paint.stroke = std::isfinite(strokeWidth) && strokeWidth > 0.0f ? strokeWidth : 1.5f;
    paint.day = subject.isDay;

    const float energy = paint.Energy();
    const float drift = 0.5f * energy * Wave(motion.clock, 6.5);
    const bool day = subject.isDay;
    const int intensity = std::clamp(subject.intensity, 1, 3);

    // A celestial body peeking from behind a cloud (partly cloudy and showers).
    const auto peek = [&](const D2D1_MATRIX_3X2_F& cloud, D2D1_POINT_2F centre, float core) {
        paint.Behind(cloud, [&] {
            if (day) paint.Sun(centre, core, core * 1.53f, core * 2.05f);
            else paint.Moon(centre, core * 0.235f);
        });
    };
    const auto precipitation = [&](WeatherScene scene, float left, float right, float top) {
        // Three particles at most in the Top Bar's 18 px: more would blur into one grey patch.
        const int count = paint.Mono() ? std::min(intensity + 1, 3) : intensity + 1;
        switch (scene) {
            case WeatherScene::Drizzle: case WeatherScene::FreezingDrizzle:
                paint.Fall(Drop::Dot, left, right, top + 0.4f, count, 0.8f, scene == WeatherScene::FreezingDrizzle, intensity);
                break;
            case WeatherScene::Rain: case WeatherScene::FreezingRain: case WeatherScene::RainShowers:
                paint.Fall(Drop::Streak, left + 0.6f, right + 0.6f, top, count, intensity >= 3 ? 1.55f : 1.25f,
                           scene == WeatherScene::FreezingRain, intensity);
                break;
            case WeatherScene::Snow: case WeatherScene::SnowShowers:
                paint.Fall(Drop::Flake, left, right, top + 0.4f, count, 0.32f, false, intensity);
                break;
            case WeatherScene::SnowGrains:
                paint.Fall(Drop::Grain, left, right, top + 0.4f, 3, 0.55f, false, intensity);
                break;
            default: break;
        }
    };

    switch (subject.scene) {
        case WeatherScene::Clear:
            if (day) {
                paint.Sun(P(8, 8), 3.0f, 4.9f, 6.6f);
            } else {
                paint.Moon(P(7.4f, 8.4f), 0.9f);
                paint.Star(P(12.4f, 3.6f), 1.3f, 0.0f);
                paint.Star(P(14.6f, 6.8f), 0.8f, 2.1f);
            }
            break;
        case WeatherScene::MainlyClear: {
            const auto cloud = Place(0.52f, 7.6f + drift, 9.2f);
            paint.Behind(cloud, [&] {
                if (day) paint.Sun(P(6.8f, 6.8f), 2.6f, 4.2f, 5.6f);
                else {
                    paint.Moon(P(6.2f, 6.6f), 0.62f);
                    paint.Star(P(13.4f, 3.4f), 1.1f, 1.0f);
                }
            });
            paint.Cloud(cloud, CloudTone::Light);
            break;
        }
        case WeatherScene::PartlyCloudy: {
            const auto cloud = Place(0.8f, 3.2f + drift, 5.1f);
            peek(cloud, P(5.6f, 5.4f), 2.45f);
            paint.Cloud(cloud, CloudTone::Light);
            break;
        }
        case WeatherScene::Overcast: {
            const auto back = Place(0.62f, 0.3f - drift * 0.7f, 2.2f);
            const auto front = Place(0.86f, 2.3f + drift, 4.7f);
            paint.Behind(front, [&] { paint.Cloud(back, CloudTone::Back); });
            paint.Cloud(front, CloudTone::Grey);
            break;
        }
        case WeatherScene::Fog: case WeatherScene::RimeFog:
            paint.Cloud(Place(0.8f, 1.6f + drift * 0.5f, 1.4f), CloudTone::Grey);
            paint.FogBands(subject.scene == WeatherScene::RimeFog);
            break;
        case WeatherScene::Drizzle: case WeatherScene::FreezingDrizzle: case WeatherScene::Rain:
        case WeatherScene::FreezingRain: case WeatherScene::Snow: case WeatherScene::SnowGrains: {
            const bool heavy = intensity >= 3 && subject.scene != WeatherScene::Snow;
            paint.Cloud(Place(1.0f, drift, -0.75f), heavy ? CloudTone::Grey : CloudTone::Light);
            precipitation(subject.scene, 3.0f, 13.0f, 10.6f);
            break;
        }
        case WeatherScene::RainShowers: case WeatherScene::SnowShowers: {
            const auto cloud = Place(0.84f, 2.58f + drift, 1.36f);
            peek(cloud, P(4.6f, 4.3f), 2.05f);
            paint.Cloud(cloud, intensity >= 3 ? CloudTone::Grey : CloudTone::Light);
            precipitation(subject.scene, 4.9f, 14.3f, 10.9f);
            break;
        }
        case WeatherScene::Thunderstorm: case WeatherScene::ThunderstormHail: {
            const auto front = Place(1.0f, drift, -0.75f);
            const auto back = Place(0.55f, 0.2f - drift * 0.6f, 0.1f);
            paint.Behind(front, [&] { paint.Cloud(back, CloudTone::StormBack); });
            paint.Cloud(front, CloudTone::Storm, paint.Mono() ? 0.0f : paint.Flash());
            if (subject.scene == WeatherScene::ThunderstormHail) {
                paint.Fall(Drop::Hail, 0, 0, 11.0f, 0, 1.35f, false, intensity, {4.0f, 13.1f});
            } else {
                paint.Fall(Drop::Streak, 0, 0, 10.6f, 0, 1.4f, false, intensity, {4.6f, 13.8f});
            }
            paint.Bolt(paint.Flicker());
            break;
        }
        case WeatherScene::Unknown: break;
    }

    if (cached && paint.Mono()) {
        paint.Grid();
        target->DrawEllipse(D2D1::Ellipse(P(13.75f, 13.75f), 1.25f, 1.25f), paint.Ink(), 0.75f, cache->round.get());
        paint.Line(P(13.75f, 13), P(13.75f, 13.75f), brush.get(), 0.6f);
    }
    target->SetTransform(previous);
}

SkyColors SkyFor(const WeatherArtSubject& subject, double hour, double sunrise, double sunset) noexcept {
    if (!std::isfinite(sunrise) || !std::isfinite(sunset) || sunrise <= 0.5 || sunset >= 23.5 || sunset - sunrise < 3.0) {
        sunrise = 6.0;
        sunset = 18.0;
    }
    hour = std::isfinite(hour) ? std::fmod(std::fmod(hour, 24.0) + 24.0, 24.0) : 12.0;
    struct Key {
        double hour;
        Color top, bottom;
    };
    const Color nightTop = Color::FromHex(0x0A1530), nightBottom = Color::FromHex(0x1D3666);
    const double noon = (sunrise + sunset) / 2.0;
    const std::array<Key, 7> keys{{
        {sunrise - 1.4, nightTop, nightBottom},
        {sunrise - 0.2, Color::FromHex(0x41609F), Color::FromHex(0xF2B088)},            // dawn
        {sunrise + 1.4, Color::FromHex(0x77C2F5), Color::FromHex(0xD3EEFF)},            // morning: light sky blue
        {noon, Color::FromHex(0x3895E6), Color::FromHex(0xA0D3FA)},                       // day
        {sunset - 1.4, Color::FromHex(0x4A8DD6), Color::FromHex(0xC0DEF5)},             // afternoon
        {sunset + 0.1, Color::FromHex(0x3C4A88), Color::FromHex(0xEF8B5D)},             // dusk
        {sunset + 1.3, nightTop, nightBottom},
    }};
    SkyColors sky{nightTop, nightBottom, false};
    for (std::size_t i = 0; i + 1 < keys.size(); ++i) {
        if (hour >= keys[i].hour && hour <= keys[i + 1].hour) {
            const float t = static_cast<float>((hour - keys[i].hour) / std::max(0.01, keys[i + 1].hour - keys[i].hour));
            sky.top = Mix(keys[i].top, keys[i + 1].top, t);
            sky.bottom = Mix(keys[i].bottom, keys[i + 1].bottom, t);
            break;
        }
    }
    // Weather greys the sky: a little under scattered cloud, much under rain and storms.
    const bool lit = Luminance(sky.top) > 0.2f;
    float grey = 0.0f;
    Color veil = lit ? Color::FromHex(0x8E9AAB) : Color::FromHex(0x252C3B);
    switch (subject.scene) {
        case WeatherScene::MainlyClear: grey = 0.05f; break;
        case WeatherScene::PartlyCloudy: grey = 0.14f; break;
        case WeatherScene::Overcast: grey = 0.48f; break;
        case WeatherScene::Fog: case WeatherScene::RimeFog:
            grey = 0.58f;
            if (lit) veil = Color::FromHex(0xA7B1BE);
            break;
        case WeatherScene::Drizzle: case WeatherScene::FreezingDrizzle: grey = 0.42f; break;
        case WeatherScene::Rain: case WeatherScene::FreezingRain: grey = subject.intensity >= 3 ? 0.58f : 0.48f; break;
        case WeatherScene::RainShowers: grey = 0.34f; break;
        case WeatherScene::Snow: case WeatherScene::SnowGrains: case WeatherScene::SnowShowers:
            grey = 0.42f;
            if (lit) veil = Color::FromHex(0xB6C1CE);
            break;
        case WeatherScene::Thunderstorm: case WeatherScene::ThunderstormHail:
            grey = 0.66f;
            veil = lit ? Color::FromHex(0x4C5466) : Color::FromHex(0x1A1F2A);
            break;
        default: break;
    }
    sky.top = Mix(sky.top, veil, grey);
    sky.bottom = Mix(sky.bottom, veil, grey);
    sky.darkText = (Luminance(sky.top) + Luminance(sky.bottom)) * 0.5f > 0.56f;
    return sky;
}

}  // namespace aether::graphics
