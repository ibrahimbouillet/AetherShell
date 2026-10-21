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

AETHER_TEST("glyphs: Wi-Fi states and their unknown mark fit inside the shared grid") {
    GlyphGuard guard;
    const auto factory = Factory();
    REQUIRE(factory);
    auto* style = RoundStrokeStyle(factory.get());
    REQUIRE(style != nullptr);
    for (const auto waves : {std::optional<int>{}, std::optional<int>{0}, std::optional<int>{1},
                            std::optional<int>{2}, std::optional<int>{3}, std::optional<int>{4}}) {
        const auto built = BuildWiFiGlyphGeometry(factory.get(), waves);
        REQUIRE(built.ok());
        for (auto* geometry : {built->active.stroke.get(), built->inactiveStroke.get()}) {
            if (!geometry) continue;
            D2D1_RECT_F bounds{};
            REQUIRE(SUCCEEDED(geometry->GetWidenedBounds(1.5f, style, nullptr, &bounds)));
            CHECK(InsideGrid(bounds));
        }
        if (built->active.fill) {
            D2D1_RECT_F bounds{};
            REQUIRE(SUCCEEDED(built->active.fill->GetBounds(nullptr, &bounds)));
            CHECK(InsideGrid(bounds));
        }
    }
    CHECK_FALSE(BuildWiFiGlyphGeometry(nullptr, 1).ok());
}

AETHER_TEST("glyphs: Wi-Fi visibly gains waves and unknown never becomes invented full strength") {
    GlyphGuard guard;
    D2DCanvas canvas;
    REQUIRE(canvas.Create(7 * 48, 48));
    winrt::com_ptr<ID2D1SolidColorBrush> brush;
    REQUIRE(SUCCEEDED(canvas.Context()->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f), brush.put())));
    brush->SetOpacity(0.6f);
    const std::optional<int> states[]{std::nullopt, 0, 1, 2, 3, -1, 4};
    canvas.Begin();
    for (int index = 0; index < 7; ++index) {
        const float x = static_cast<float>(index * 48);
        PaintWiFiGlyph(canvas.Context(), D2D1::RectF(x + 8, 8, x + 40, 40), brush.get(), states[index]);
        CHECK_NEAR(brush->GetOpacity(), 0.6, 1e-6);
        D2D1_MATRIX_3X2_F transform{};
        canvas.Context()->GetTransform(&transform);
        CHECK_EQ(transform._11, 1.0f); CHECK_EQ(transform._22, 1.0f);
        CHECK_EQ(transform._31, 0.0f); CHECK_EQ(transform._32, 0.0f);
    }
    REQUIRE(canvas.End());
    int coverage[7]{};
    int unknownDifference = 0;
    for (UINT y = 0; y < 48; ++y) {
        for (UINT x = 0; x < 48; ++x) {
            const auto unknown = canvas.At(x, y);
            unknownDifference += std::abs(unknown.a - canvas.At(48 + x, y).a);
            CHECK_EQ(unknown.a, canvas.At(5 * 48 + x, y).a);
            CHECK_EQ(unknown.a, canvas.At(6 * 48 + x, y).a);
            for (UINT index = 0; index < 7; ++index) {
                const auto pixel = canvas.At(index * 48 + x, y);
                coverage[index] += pixel.a;
                if (x < 8 || x >= 40 || y < 8 || y >= 40) CHECK_EQ(pixel.a, 0);
            }
        }
    }
    for (int index = 2; index <= 4; ++index) CHECK(coverage[index] > coverage[index - 1]);
    CHECK(unknownDifference > 0); // Known zero and unavailable strength remain different states.
    CHECK(coverage[0] < coverage[4]);
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
    // A fine flattening tolerance: the default (0.25 DIP) is coarse for 1.5-DIP corners.
    REQUIRE(SUCCEEDED((*start)->ComputeArea(nullptr, 0.001f, &area)));
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

AETHER_TEST("glyphs: every status outline fits the proportional grid with round caps and joins") {
    GlyphGuard guard; const auto factory = Factory(); REQUIRE(factory);
    auto* style = RoundStrokeStyle(factory.get()); REQUIRE(style != nullptr);
    CHECK(style->GetStartCap() == D2D1_CAP_STYLE_ROUND); CHECK(style->GetEndCap() == D2D1_CAP_STYLE_ROUND);
    CHECK(style->GetLineJoin() == D2D1_LINE_JOIN_ROUND);
    for (const auto glyph : kAllStatusGlyphs) {
        const auto geometry = BuildStatusGlyphGeometry(factory.get(),glyph); REQUIRE(geometry.ok());
        REQUIRE(geometry->stroke || geometry->fill);
        if (geometry->stroke) {
            for (const float width : {1.5f,2.0f}) {
                D2D1_RECT_F bounds{}; REQUIRE(SUCCEEDED(geometry->stroke->GetWidenedBounds(width,style,nullptr,&bounds)));
                if (!InsideGrid(bounds)) TEST_NOTE("status outline outside grid: " + std::to_string(static_cast<int>(glyph)));
                CHECK(InsideGrid(bounds));
            }
        }
        if (geometry->fill) {
            D2D1_RECT_F bounds{}; REQUIRE(SUCCEEDED(geometry->fill->GetBounds(nullptr,&bounds))); CHECK(InsideGrid(bounds));
        }
    }
    CHECK_FALSE(BuildStatusGlyphGeometry(nullptr,StatusGlyph::Speaker).ok());
}

AETHER_TEST("glyphs: wide and tall status boxes preserve the same circular proportions") {
    GlyphGuard guard; D2DCanvas canvas; REQUIRE(canvas.Create(112,112));
    winrt::com_ptr<ID2D1SolidColorBrush> brush;
    REQUIRE(SUCCEEDED(canvas.Context()->CreateSolidColorBrush(D2D1::ColorF(1,1,1,1),brush.put())));
    for (const auto glyph : kAllStatusGlyphs) {
        canvas.Begin();
        PaintStatusGlyph(canvas.Context(),glyph,{4,4,36,36},brush.get());
        PaintStatusGlyph(canvas.Context(),glyph,{44,4,108,36},brush.get());
        PaintStatusGlyph(canvas.Context(),glyph,{4,44,36,108},brush.get());
        REQUIRE(canvas.End());
        int coverage = 0, difference = 0, outside = 0;
        for (UINT y = 0; y < 32; ++y) for (UINT x = 0; x < 32; ++x) {
            const auto base = canvas.At(4+x,4+y); coverage += base.a;
            difference += std::abs(base.a-canvas.At(60+x,4+y).a)+std::abs(base.a-canvas.At(4+x,60+y).a);
        }
        for (UINT y = 0; y < 112; ++y) for (UINT x = 0; x < 112; ++x) {
            const bool painted = (x>=4&&x<36&&y>=4&&y<36)||(x>=60&&x<92&&y>=4&&y<36)||(x>=4&&x<36&&y>=60&&y<92);
            if (!painted) outside += canvas.At(x,y).a;
        }
        CHECK(coverage > 0); CHECK_EQ(difference,0); CHECK_EQ(outside,0);
    }
}

AETHER_TEST("glyphs: battery warning and full colors require known readings and obey high contrast") {
    const auto dark = MakeDesignTokens(ThemeMode::Dark);
    const auto same = [](Color a,Color b) { return a.r==b.r&&a.g==b.g&&a.b==b.b&&a.a==b.a; };
    CHECK(same(BatteryStatusColor(dark,100),dark.success));
    for (const auto percent : {std::optional<int>{},std::optional<int>{6},std::optional<int>{99},std::optional<int>{101},std::optional<int>{-1}})
        CHECK(same(BatteryStatusColor(dark,percent),dark.textPrimary));
    for (const int percent : {0,1,5}) CHECK(same(BatteryStatusColor(dark,percent),dark.critical));
    CHECK(same(BatteryStatusColor(dark,42,true),dark.critical));
    CHECK(same(BatteryStatusColor(dark,std::nullopt,true),dark.critical));
    CHECK(same(BatteryStatusColor(dark,100,true),dark.critical));
    auto contrast = MakeDesignTokens(ThemeMode::HighContrast);
    contrast.success = {0,1,0,1}; contrast.critical = {1,0,0,1};
    for (const auto percent : {std::optional<int>{},std::optional<int>{0},std::optional<int>{5},std::optional<int>{100}})
        CHECK(same(BatteryStatusColor(contrast,percent,true),contrast.textPrimary));

    GlyphGuard guard; D2DCanvas canvas; REQUIRE(canvas.Create(200,40));
    winrt::com_ptr<ID2D1SolidColorBrush> brush;
    REQUIRE(SUCCEEDED(canvas.Context()->CreateSolidColorBrush(dark.textPrimary.ToD2D(),brush.put())));
    canvas.Begin();
    PaintBatteryGlyph(canvas.Context(),{0,0,32,32},brush.get(),dark,100);
    PaintBatteryGlyph(canvas.Context(),{40,0,72,32},brush.get(),dark,99);
    PaintBatteryGlyph(canvas.Context(),{80,0,112,32},brush.get(),dark,42,true);
    PaintBatteryGlyph(canvas.Context(),{120,0,152,32},brush.get(),contrast,100);
    PaintBatteryGlyph(canvas.Context(),{160,0,192,32},brush.get(),dark,0);
    REQUIRE(canvas.End());
    const auto pixel = [&](UINT x,Color color) { CHECK(test::Near(canvas.At(x,16),Channel(color.r),Channel(color.g),Channel(color.b),255,2)); };
    pixel(12,dark.success); pixel(52,dark.textPrimary); pixel(88,dark.critical); pixel(132,contrast.textPrimary);
    CHECK(test::Near(canvas.At(176,9),Channel(dark.critical.r),Channel(dark.critical.g),Channel(dark.critical.b),255,2));
    CHECK_EQ(canvas.At(176,16).a,0); // zero remains empty, with a visible critical outline
}

AETHER_TEST("glyphs: audio and network unknown states differ from known zero and preserve paint state") {
    GlyphGuard guard; D2DCanvas canvas; REQUIRE(canvas.Create(12*40,40));
    winrt::com_ptr<ID2D1SolidColorBrush> brush;
    const auto color = D2D1::ColorF(0.7f,0.8f,0.9f,0.85f);
    REQUIRE(SUCCEEDED(canvas.Context()->CreateSolidColorBrush(color,brush.put()))); brush->SetOpacity(0.6f);
    canvas.Begin();
    for (int i = 0; i < 5; ++i) PaintVolumeGlyph(canvas.Context(),{static_cast<float>(i*40+4),4,static_cast<float>(i*40+36),36},
        brush.get(),i==0?std::nullopt:std::optional<int>((i-1)*33),false);
    PaintVolumeGlyph(canvas.Context(),{204,4,236,36},brush.get(),std::nullopt,true);
    PaintMicrophoneGlyph(canvas.Context(),{244,4,276,36},brush.get(),std::nullopt);
    PaintMicrophoneGlyph(canvas.Context(),{284,4,316,36},brush.get(),false);
    PaintNetworkGlyph(canvas.Context(),{324,4,356,36},brush.get(),NetworkGlyphKind::Cellular,0);
    PaintNetworkGlyph(canvas.Context(),{364,4,396,36},brush.get(),NetworkGlyphKind::Cellular,5);
    PaintNetworkGlyph(canvas.Context(),{404,4,436,36},brush.get(),NetworkGlyphKind::Cellular,std::nullopt);
    PaintBatteryGlyph(canvas.Context(),{444,4,476,36},brush.get(),MakeDesignTokens(ThemeMode::Dark),100);
    CHECK_NEAR(brush->GetOpacity(),0.6f,1e-6);
    const auto restored = brush->GetColor(); CHECK_EQ(restored.r,color.r); CHECK_EQ(restored.g,color.g); CHECK_EQ(restored.b,color.b); CHECK_EQ(restored.a,color.a);
    D2D1_MATRIX_3X2_F transform{};canvas.Context()->GetTransform(&transform);
    CHECK_EQ(transform._11,1.0f);CHECK_EQ(transform._22,1.0f);CHECK_EQ(transform._31,0.0f);CHECK_EQ(transform._32,0.0f);
    REQUIRE(canvas.End());
    int coverage[12]{};int volumeUnknown=0,micUnknown=0,cellUnknown=0;
    for (UINT y=0;y<40;++y) for (UINT x=0;x<40;++x) {
        for (UINT i=0;i<12;++i) {const auto pixel=canvas.At(i*40+x,y);coverage[i]+=pixel.a;if(x<4||x>=36||y<4||y>=36)CHECK_EQ(pixel.a,0);}
        volumeUnknown+=std::abs(canvas.At(x,y).a-canvas.At(40+x,y).a);
        micUnknown+=std::abs(canvas.At(240+x,y).a-canvas.At(280+x,y).a);
        cellUnknown+=std::abs(canvas.At(400+x,y).a-canvas.At(320+x,y).a);
    }
    CHECK(coverage[2]>coverage[1]);CHECK(coverage[3]>coverage[2]);CHECK(coverage[4]>coverage[3]);
    CHECK(coverage[9]>coverage[8]);CHECK(volumeUnknown>0);CHECK(micUnknown>0);CHECK(cellUnknown>0);
    canvas.Begin();
    const auto nested = D2D1::Matrix3x2F::Scale(0.75f,0.75f)*D2D1::Matrix3x2F::Translation(3,2);
    canvas.Context()->SetTransform(nested);
    PaintNetworkGlyph(canvas.Context(),{4,4,36,36},brush.get(),NetworkGlyphKind::Cellular,3,true);
    PaintBatteryGlyph(canvas.Context(),{44,4,76,36},brush.get(),MakeDesignTokens(ThemeMode::Dark),42,{},true);
    PaintWeatherGlyph(canvas.Context(),{84,4,116,36},brush.get(),WeatherSymbol::Rain,true,true);
    canvas.Context()->GetTransform(&transform);
    CHECK_EQ(transform._11,nested._11);CHECK_EQ(transform._12,nested._12);CHECK_EQ(transform._21,nested._21);
    CHECK_EQ(transform._22,nested._22);CHECK_EQ(transform._31,nested._31);CHECK_EQ(transform._32,nested._32);
    CHECK_NEAR(brush->GetOpacity(),0.6f,1e-6);CHECK_EQ(brush->GetColor().r,color.r);CHECK_EQ(brush->GetColor().a,color.a);
    REQUIRE(canvas.End());
}

AETHER_TEST("glyphs: weather categories remain distinct and absent daylight does not invent sun") {
    GlyphGuard guard;D2DCanvas canvas;REQUIRE(canvas.Create(6*40,40));
    winrt::com_ptr<ID2D1SolidColorBrush> brush;
    REQUIRE(SUCCEEDED(canvas.Context()->CreateSolidColorBrush(D2D1::ColorF(1,1,1,1),brush.put())));
    canvas.Begin();
    PaintWeatherGlyph(canvas.Context(),{4,4,36,36},brush.get(),WeatherSymbol::Clear,true);
    PaintWeatherGlyph(canvas.Context(),{44,4,76,36},brush.get(),WeatherSymbol::Clear,false);
    PaintWeatherGlyph(canvas.Context(),{84,4,116,36},brush.get(),WeatherSymbol::Rain,true);
    PaintWeatherGlyph(canvas.Context(),{124,4,156,36},brush.get(),WeatherSymbol::Snow,true);
    PaintWeatherGlyph(canvas.Context(),{164,4,196,36},brush.get(),WeatherSymbol::Clear,std::nullopt);
    PaintWeatherGlyph(canvas.Context(),{204,4,236,36},brush.get(),WeatherSymbol::Unknown,true);
    REQUIRE(canvas.End());int dayNight=0,rainSnow=0,unknown=0,unknownSun=0;
    for(UINT y=0;y<40;++y)for(UINT x=0;x<40;++x){
        dayNight+=std::abs(canvas.At(x,y).a-canvas.At(40+x,y).a);
        rainSnow+=std::abs(canvas.At(80+x,y).a-canvas.At(120+x,y).a);
        unknown+=std::abs(canvas.At(160+x,y).a-canvas.At(200+x,y).a);
        unknownSun+=std::abs(canvas.At(160+x,y).a-canvas.At(x,y).a);
    }
    CHECK(dayNight>0);CHECK(rainSnow>0);CHECK_EQ(unknown,0);CHECK(unknownSun>0);
}
