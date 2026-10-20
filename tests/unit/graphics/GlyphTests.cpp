// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Shared vector marks: every glyph's geometry stays inside its 16-DIP grid (with the stroke), the
// brand and Start marks have their expected shape, and painting lands inside the requested box with
// the token colors. Geometry uses a headless factory; painting uses an offscreen WARP canvas.
#include "AetherTest.h"

#include "D2DCanvas.h"

#include "graphics/Glyphs.h"
#include "graphics/Theme.h"

#include <cmath>
#include <cstdlib>
#include <string>

using namespace aether;
using namespace aether::graphics;
using test::D2DCanvas;

namespace {

winrt::com_ptr<ID2D1Factory> Factory() {
    winrt::com_ptr<ID2D1Factory> factory;
    (void)::D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory), nullptr, factory.put_void());
    return factory;
}

bool InsideGrid(const D2D1_RECT_F& bounds, float tolerance = 0.01f) {
    return bounds.left >= -tolerance && bounds.top >= -tolerance && bounds.right <= 16.0f + tolerance &&
           bounds.bottom <= 16.0f + tolerance && bounds.right > bounds.left && bounds.bottom > bounds.top;
}

struct GlyphGuard {
    ~GlyphGuard() { ReleaseGlyphResources(); }
};

int Channel(float value) { return static_cast<int>(std::lround(value * 255.0f)); }

}  // namespace

AETHER_TEST("glyphs: every glyph stays inside its 16-DIP grid including the round stroke") {
    GlyphGuard guard;
    const auto factory = Factory();
    REQUIRE(factory);
    auto* style = RoundStrokeStyle(factory.get());
    REQUIRE(style != nullptr);
    CHECK(RoundStrokeStyle(factory.get()) == style);  // cached per factory
    for (const Glyph glyph : kAllGlyphs) {
        auto built = BuildGlyphGeometry(factory.get(), glyph);
        REQUIRE(built.ok());
        CHECK(built->stroke || built->fill);
        if (built->stroke) {
            D2D1_RECT_F bounds{};
            REQUIRE(SUCCEEDED(built->stroke->GetWidenedBounds(1.5f, style, nullptr, &bounds)));
            if (!InsideGrid(bounds)) TEST_NOTE("glyph " + std::to_string(static_cast<int>(glyph)) + " stroke leaves the grid");
            CHECK(InsideGrid(bounds));
        }
        if (built->fill) {
            D2D1_RECT_F bounds{};
            REQUIRE(SUCCEEDED(built->fill->GetBounds(nullptr, &bounds)));
            CHECK(InsideGrid(bounds));
        }
    }
    CHECK_FALSE(BuildGlyphGeometry(nullptr, Glyph::Search).ok());
}

AETHER_TEST("glyphs: the brand mark is two filled ribbons centred in the grid") {
    const auto factory = Factory();
    REQUIRE(factory);
    auto brand = BuildBrandMarkGeometry(factory.get());
    REQUIRE(brand.ok());
    REQUIRE(*brand);
    UINT32 figures = 0;
    REQUIRE(SUCCEEDED((*brand)->GetFigureCount(&figures)));
    CHECK_EQ(figures, 2u);
    D2D1_RECT_F bounds{};
    REQUIRE(SUCCEEDED((*brand)->GetBounds(nullptr, &bounds)));
    CHECK(InsideGrid(bounds));
    // Same scale as the Top Bar logo: an 18-DIP box maps the 1000-unit design space at 0.02124 DIP per unit.
    CHECK_NEAR((bounds.right - bounds.left) * 18.0f / 16.0f, (850.0f - 150.0f) * 0.02124f, 0.2);
    float area = 0.0f;
    REQUIRE(SUCCEEDED((*brand)->ComputeArea(nullptr, &area)));
    CHECK(area > 20.0f);
    CHECK_FALSE(BuildBrandMarkGeometry(nullptr).ok());
}

AETHER_TEST("glyphs: the Start mark is four rounded squares in a 2x2 arrangement") {
    const auto factory = Factory();
    REQUIRE(factory);
    auto start = BuildStartMarkGeometry(factory.get());
    REQUIRE(start.ok());
    REQUIRE(*start);
    UINT32 figures = 0;
    REQUIRE(SUCCEEDED((*start)->GetFigureCount(&figures)));
    CHECK_EQ(figures, 4u);
    D2D1_RECT_F bounds{};
    REQUIRE(SUCCEEDED((*start)->GetBounds(nullptr, &bounds)));
    CHECK_NEAR(bounds.left, 1.0, 1e-3);
    CHECK_NEAR(bounds.top, 1.0, 1e-3);
    CHECK_NEAR(bounds.right, 15.0, 1e-3);
    CHECK_NEAR(bounds.bottom, 15.0, 1e-3);
    float area = 0.0f;
    REQUIRE(SUCCEEDED((*start)->ComputeArea(nullptr, &area)));
    // Four 6.5-DIP squares minus their rounded corners (radius 1.5).
    const float expected = 4.0f * (6.5f * 6.5f - (4.0f - 3.14159265f) * 1.5f * 1.5f);
    CHECK_NEAR(area, expected, 0.5);
    BOOL contains = TRUE;
    REQUIRE(SUCCEEDED((*start)->FillContainsPoint(D2D1::Point2F(8.0f, 8.0f), nullptr, &contains)));
    CHECK_FALSE(contains);  // the gap between the squares
    REQUIRE(SUCCEEDED((*start)->FillContainsPoint(D2D1::Point2F(4.0f, 12.0f), nullptr, &contains)));
    CHECK(contains);
}

AETHER_TEST("glyphs: painted glyphs stay inside their box and use the brush color") {
    GlyphGuard guard;
    D2DCanvas canvas;
    REQUIRE(canvas.Create(64, 64));
    winrt::com_ptr<ID2D1SolidColorBrush> brush;
    REQUIRE(SUCCEEDED(canvas.Context()->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f), brush.put())));
    canvas.Begin();
    // A 32-DIP box at (16,16): the grid is scaled by 2.
    PaintGlyph(canvas.Context(), Glyph::Close, D2D1::RectF(16.0f, 16.0f, 48.0f, 48.0f), brush.get(), 1.5f);
    PaintGlyph(canvas.Context(), Glyph::Search, D2D1::RectF(0.0f, 0.0f, 0.2f, 0.2f), brush.get());  // unusable box
    PaintGlyph(nullptr, Glyph::Search, D2D1::RectF(0.0f, 0.0f, 16.0f, 16.0f), brush.get());
    REQUIRE(canvas.End());
    int outside = 0;
    for (UINT y = 0; y < 64; ++y) {
        for (UINT x = 0; x < 64; ++x) {
            if (x >= 16 && x < 48 && y >= 16 && y < 48) continue;
            if (canvas.At(x, y).a != 0) ++outside;
        }
    }
    CHECK_EQ(outside, 0);
    // The cross passes through the centre; its corners are empty.
    CHECK(test::Near(canvas.At(32, 32), 255, 255, 255, 255, 2));
    CHECK_EQ(canvas.At(32, 18).a, 0);
}

AETHER_TEST("glyphs: the Start mark paints Windows blue by default and the accent when it is the Windows accent") {
    GlyphGuard guard;
    const auto light = MakeDesignTokens(ThemeMode::Light);
    SystemPalette palette;
    palette.available = true;
    palette.accent = Color::FromHex(0xC30052);
    palette.accentDark1 = Color::FromHex(0x9B0041);
    AppearanceOptions windows;
    windows.accent = AccentSource::Windows;
    const auto accented = MakeDesignTokens(ThemeMode::Light, palette, windows);

    D2DCanvas canvas;
    REQUIRE(canvas.Create(80, 40));
    canvas.Begin();
    PaintStartMark(canvas.Context(), D2D1::RectF(0.0f, 0.0f, 32.0f, 32.0f), light);
    PaintStartMark(canvas.Context(), D2D1::RectF(40.0f, 0.0f, 72.0f, 32.0f), accented);
    REQUIRE(canvas.End());
    // The bottom of a square is the base color (the gradient lightens only towards the top).
    const auto blue = Color::FromHex(0x0078D4);
    CHECK(test::Near(canvas.At(8, 28), Channel(blue.r), Channel(blue.g), Channel(blue.b), 255, 6));
    CHECK_EQ(canvas.At(16, 16).a, 0);  // gap between the squares
    const auto accent = accented.accentRoles.fill;
    CHECK(test::Near(canvas.At(48, 28), Channel(accent.r), Channel(accent.g), Channel(accent.b), 255, 6));
}

AETHER_TEST("glyphs: the brand mark keeps its gradient and is solid text color in high contrast") {
    GlyphGuard guard;
    D2DCanvas canvas;
    REQUIRE(canvas.Create(80, 40));
    const auto dark = MakeDesignTokens(ThemeMode::Dark);
    const auto contrast = MakeDesignTokens(ThemeMode::HighContrast);
    canvas.Begin();
    PaintBrandMark(canvas.Context(), D2D1::RectF(0.0f, 0.0f, 36.0f, 36.0f), dark);
    PaintBrandMark(canvas.Context(), D2D1::RectF(40.0f, 0.0f, 76.0f, 36.0f), contrast);
    REQUIRE(canvas.End());
    int branded = 0;
    int system = 0;
    const auto text = contrast.textPrimary;
    for (UINT y = 0; y < 36; ++y) {
        for (UINT x = 0; x < 36; ++x) {
            const auto pixel = canvas.At(x, y);
            if (pixel.a == 255 && pixel.b > pixel.r) ++branded;  // indigo to cyan: blue dominates
            const auto other = canvas.At(x + 40, y);
            if (other.a == 255) {
                ++system;
                CHECK(test::Near(other, Channel(text.r), Channel(text.g), Channel(text.b), 255, 1));
            }
        }
    }
    CHECK(branded > 50);
    CHECK(system > 50);
}

AETHER_TEST("glyphs: status badges fill with the status roles and draw a contrasting mark") {
    GlyphGuard guard;
    const auto tokens = MakeDesignTokens(ThemeMode::Light);
    D2DCanvas canvas;
    REQUIRE(canvas.Create(140, 40));
    canvas.Begin();
    PaintStatusBadge(canvas.Context(), StatusBadge::Success, D2D1::RectF(0.0f, 0.0f, 32.0f, 32.0f), tokens);
    PaintStatusBadge(canvas.Context(), StatusBadge::Error, D2D1::RectF(36.0f, 0.0f, 68.0f, 32.0f), tokens);
    PaintStatusBadge(canvas.Context(), StatusBadge::Warning, D2D1::RectF(72.0f, 0.0f, 104.0f, 32.0f), tokens);
    PaintStatusBadge(canvas.Context(), StatusBadge::Info, D2D1::RectF(108.0f, 0.0f, 140.0f, 32.0f), tokens);
    REQUIRE(canvas.End());
    // Grid point (8, 13) is inside every circle and away from every mark.
    const auto expect = [&](UINT left, const Color& color) {
        CHECK(test::Near(canvas.At(left + 16, 26), Channel(color.r), Channel(color.g), Channel(color.b), 255, 3));
    };
    expect(0, tokens.success);
    expect(36, tokens.critical);
    expect(72, tokens.caution);
    expect(108, tokens.info);
    // The error mark crosses the centre in a contrasting color (white on the light critical red).
    CHECK(test::Near(canvas.At(52, 16), 255, 255, 255, 255, 30));
}
