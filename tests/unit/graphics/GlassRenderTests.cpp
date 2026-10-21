// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Glass painters rendered headless (WARP device, offscreen bitmap): the exterior-only two-layer
// shadow, the grain, high contrast, indicators, highlights, the two-tone focus ring and icon drawing.
// Assertions read back the offscreen pixels; nothing is shown or captured from the screen.
#include "AetherTest.h"

#include "D2DCanvas.h"

#include "graphics/GlassPainter.h"
#include "graphics/Theme.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <string>
#include <vector>

using namespace aether;
using namespace aether::graphics;
using test::CanvasPixel;
using test::D2DCanvas;

namespace {

constexpr D2D1_RECT_F kPanel{40.0f, 30.0f, 160.0f, 80.0f};

DesignTokens Glass(ThemeMode mode) {
    auto tokens = MakeDesignTokens(mode);
    tokens.transparencyEnabled = mode != ThemeMode::HighContrast;  // independent of the machine setting
    return tokens;
}

GlassStyle Style(bool shadow) {
    GlassStyle style;
    style.cornerRadius = 12.0f;
    style.backdropBlur = false;
    style.shadow = shadow;
    style.elevation = Elevation::Dock;
    return style;
}

int MaxChannelDifference(const CanvasPixel& a, const CanvasPixel& b) {
    return std::max({std::abs(a.r - b.r), std::abs(a.g - b.g), std::abs(a.b - b.b), std::abs(a.a - b.a)});
}

struct CanvasGuard {
    ~CanvasGuard() { ReleaseGlassResources(); }
};

}  // namespace

AETHER_TEST("glass render: the shadow is exterior only and never darkens the translucent panel") {
    CanvasGuard guard;
    const auto tokens = Glass(ThemeMode::Dark);
    D2DCanvas with;
    D2DCanvas without;
    REQUIRE(with.Create(200, 140));
    REQUIRE(without.Create(200, 140));
    with.Begin();
    PaintGlassPanel(with.Context(), kPanel, tokens, Style(true));
    REQUIRE(with.End());
    without.Begin();
    PaintGlassPanel(without.Context(), kPanel, tokens, Style(false));
    REQUIRE(without.End());

    int worst = 0;
    for (UINT y = 36; y < 74; ++y) {
        for (UINT x = 46; x < 154; ++x) worst = std::max(worst, MaxChannelDifference(with.At(x, y), without.At(x, y)));
    }
    TEST_NOTE("interior max channel difference with and without the shadow: " + std::to_string(worst));
    CHECK(worst <= 1);

    // Outside the panel the shadow is visible below it and fades out within its extent.
    CHECK(with.At(100, 84).a >= 10);
    CHECK_EQ(without.At(100, 84).a, 0);
    CHECK(with.At(100, 84).a > with.At(100, 100).a);
    const float extent = ShadowExtent(tokens.elevation.dock);
    CHECK_EQ(with.At(100, static_cast<UINT>(kPanel.bottom + extent + 4.0f)).a, 0);
    // Shadows are black: premultiplied color channels stay 0.
    CHECK_EQ(with.At(100, 84).r, 0);
}

AETHER_TEST("glass render: a wider panel reuses the same shadow profile (nine-slice stretching)") {
    CanvasGuard guard;
    const auto tokens = Glass(ThemeMode::Light);
    D2DCanvas narrow;
    D2DCanvas wide;
    REQUIRE(narrow.Create(240, 140));
    REQUIRE(wide.Create(240, 140));
    narrow.Begin();
    PaintElevation(narrow.Context(), D2D1::RectF(70.0f, 30.0f, 170.0f, 80.0f), 12.0f, tokens, Elevation::Dock);
    REQUIRE(narrow.End());
    wide.Begin();
    PaintElevation(wide.Context(), D2D1::RectF(40.0f, 30.0f, 200.0f, 80.0f), 12.0f, tokens, Elevation::Dock);
    REQUIRE(wide.End());
    int worst = 0;
    for (UINT y = 80; y < 120; ++y) worst = std::max(worst, MaxChannelDifference(narrow.At(120, y), wide.At(120, y)));
    CHECK(worst <= 2);
    CHECK(narrow.At(120, 85).a > 0);
    // Nothing is drawn inside the shape.
    CHECK_EQ(wide.At(120, 55).a, 0);
}

AETHER_TEST("glass render: elevation none, high contrast and invalid rectangles draw no shadow") {
    CanvasGuard guard;
    D2DCanvas canvas;
    REQUIRE(canvas.Create(200, 140));
    const auto dark = Glass(ThemeMode::Dark);
    const auto contrast = Glass(ThemeMode::HighContrast);
    canvas.Begin();
    PaintElevation(canvas.Context(), kPanel, 12.0f, dark, Elevation::None);
    PaintElevation(canvas.Context(), kPanel, 12.0f, contrast, Elevation::Dock);
    PaintElevation(canvas.Context(), kPanel, 12.0f, dark, Elevation::Dock, 0.0f);
    PaintElevation(canvas.Context(), D2D1::RectF(100.0f, 50.0f, 60.0f, 20.0f), 12.0f, dark, Elevation::Dock);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    PaintElevation(canvas.Context(), D2D1::RectF(nan, 0.0f, 10.0f, 10.0f), 12.0f, dark, Elevation::Dock);
    PaintGlassPanel(canvas.Context(), D2D1::RectF(nan, nan, nan, nan), dark, Style(true));
    PaintGlassPanel(nullptr, kPanel, dark, Style(true));
    REQUIRE(canvas.End());
    for (UINT y = 0; y < 140; y += 7) {
        for (UINT x = 0; x < 200; x += 7) CHECK_EQ(canvas.At(x, y).a, 0);
    }
}

AETHER_TEST("glass render: high contrast paints an opaque system-color panel without shadow or grain") {
    CanvasGuard guard;
    D2DCanvas canvas;
    REQUIRE(canvas.Create(200, 140));
    const auto tokens = Glass(ThemeMode::HighContrast);
    canvas.Begin();
    PaintGlassPanel(canvas.Context(), kPanel, tokens, Style(true));
    REQUIRE(canvas.End());
    const auto window = Color::FromColorRef(::GetSysColor(COLOR_WINDOW));
    const auto inside = canvas.At(100, 55);
    CHECK(test::Near(inside, static_cast<int>(std::lround(window.r * 255)), static_cast<int>(std::lround(window.g * 255)),
                     static_cast<int>(std::lround(window.b * 255)), 255, 1));
    CHECK_EQ(canvas.At(100, 86).a, 0);
    // Uniform interior: no grain, no sheen.
    for (UINT x = 50; x < 150; ++x) CHECK_EQ(MaxChannelDifference(canvas.At(x, 55), inside), 0);
}

AETHER_TEST("glass render: desktop glass has no directional light while popup glass keeps its flyout treatment") {
    CanvasGuard guard;
    for (const auto mode : {ThemeMode::Dark, ThemeMode::Light}) {
        const auto tokens = Glass(mode);
        for (const auto elevation : {Elevation::Dock, Elevation::Bar}) {
            D2DCanvas canvas;
            REQUIRE(canvas.Create(200, 140));
            auto style = Style(true);
            style.elevation = elevation;
            style.noise = false; // Isolate lighting from the deliberately irregular grain.
            canvas.Begin();
            PaintGlassPanel(canvas.Context(), kPanel, tokens, style);
            REQUIRE(canvas.End());
            int mirroredDifference = 0;
            UINT worstX = 0, worstY = 0;
            for (UINT y = 0; y < 110; ++y) {
                // Sample the flat central band. Direct2D's rounded-corner antialias coverage can
                // differ by a few channel values after reflection even without directional light.
                for (UINT x = 64; x < 136; ++x) {
                    const int difference = MaxChannelDifference(canvas.At(x, y), canvas.At(x, 109 - y));
                    if (difference > mirroredDifference) { mirroredDifference = difference; worstX = x; worstY = y; }
                }
            }
            if (mirroredDifference > 2) TEST_NOTE("desktop glass mode " + std::to_string(static_cast<int>(mode)) +
                " elevation " + std::to_string(static_cast<int>(elevation)) + " mirror delta " + std::to_string(mirroredDifference) +
                " at " + std::to_string(worstX) + "," + std::to_string(worstY) + "; alpha " +
                std::to_string(canvas.At(worstX, worstY).a) + "/" + std::to_string(canvas.At(worstX, 109 - worstY).a));
            // Includes the straight top/bottom edge, the interior and the exterior shadow.
            CHECK(mirroredDifference <= 2);
            CHECK(canvas.At(100, 55).a < 255); // The sanded material still transmits its backdrop.
        }
        D2DCanvas popup;
        REQUIRE(popup.Create(200, 140));
        auto style = Style(false);
        style.elevation = Elevation::Popup;
        style.noise = false;
        popup.Begin();
        PaintGlassPanel(popup.Context(), kPanel, tokens, style);
        REQUIRE(popup.End());
        CHECK(MaxChannelDifference(popup.At(100, 32), popup.At(100, 77)) > 3);
    }
}

AETHER_TEST("glass render: desktop material stays opaque and untextured when transparency is off") {
    CanvasGuard guard;
    for (const auto mode : {ThemeMode::Dark, ThemeMode::Light}) {
        auto tokens = Glass(mode);
        tokens.transparencyEnabled = false;
        D2DCanvas canvas;
        REQUIRE(canvas.Create(200, 140));
        auto style = Style(false);
        style.elevation = Elevation::Bar;
        canvas.Begin();
        PaintGlassPanel(canvas.Context(), kPanel, tokens, style);
        REQUIRE(canvas.End());
        const auto center = canvas.At(100, 55);
        CHECK_EQ(center.a, 255);
        for (UINT y = 35; y < 75; ++y)
            for (UINT x = 55; x < 145; ++x) CHECK_EQ(MaxChannelDifference(canvas.At(x, y), center), 0);
    }
}

AETHER_TEST("glass render: the grain adds subtle deterministic variation only when enabled") {
    CanvasGuard guard;
    auto tokens = Glass(ThemeMode::Dark);
    auto style = Style(false);
    style.highlight = false;
    D2DCanvas plain;
    D2DCanvas grainy;
    D2DCanvas again;
    REQUIRE(plain.Create(200, 140));
    REQUIRE(grainy.Create(200, 140));
    REQUIRE(again.Create(200, 140));
    style.noise = false;
    plain.Begin();
    PaintGlassPanel(plain.Context(), kPanel, tokens, style);
    REQUIRE(plain.End());
    style.noise = true;
    grainy.Begin();
    PaintGlassPanel(grainy.Context(), kPanel, tokens, style);
    REQUIRE(grainy.End());
    again.Begin();
    PaintGlassPanel(again.Context(), kPanel, tokens, style);
    REQUIRE(again.End());

    int plainSpread = 0;
    int grainSpread = 0;
    int largest = 0;
    for (UINT x = 50; x < 150; ++x) {
        plainSpread = std::max(plainSpread, MaxChannelDifference(plain.At(x, 60), plain.At(50, 60)));
        grainSpread = std::max(grainSpread, MaxChannelDifference(grainy.At(x, 60), grainy.At(50, 60)));
        largest = std::max(largest, MaxChannelDifference(grainy.At(x, 60), plain.At(x, 60)));
        CHECK_EQ(MaxChannelDifference(grainy.At(x, 60), again.At(x, 60)), 0);
    }
    CHECK_EQ(plainSpread, 0);
    CHECK(grainSpread > 0);
    CHECK(largest <= 16);  // subtle

    // Without transparency the grain is off as well.
    tokens.transparencyEnabled = false;
    D2DCanvas opaque;
    REQUIRE(opaque.Create(200, 140));
    opaque.Begin();
    PaintGlassPanel(opaque.Context(), kPanel, tokens, style);
    REQUIRE(opaque.End());
    for (UINT x = 50; x < 150; ++x) CHECK_EQ(MaxChannelDifference(opaque.At(x, 60), opaque.At(50, 60)), 0);
}

AETHER_TEST("glass render: running indicators use the running, accent and caution colors and fade") {
    CanvasGuard guard;
    const auto tokens = Glass(ThemeMode::Dark);
    D2DCanvas canvas;
    REQUIRE(canvas.Create(200, 40));
    canvas.Begin();
    PaintRunningIndicator(canvas.Context(), D2D1::RectF(10.0f, 10.0f, 26.0f, 13.0f), tokens, IndicatorState::Active);
    PaintRunningIndicator(canvas.Context(), D2D1::RectF(40.0f, 10.0f, 46.0f, 13.0f), tokens, IndicatorState::Running);
    PaintRunningIndicator(canvas.Context(), D2D1::RectF(60.0f, 10.0f, 76.0f, 13.0f), tokens, IndicatorState::Attention);
    PaintRunningIndicator(canvas.Context(), D2D1::RectF(90.0f, 10.0f, 106.0f, 13.0f), tokens, IndicatorState::Active, 0.0f);
    PaintRunningIndicator(canvas.Context(), D2D1::RectF(120.0f, 10.0f, 136.0f, 13.0f), tokens, IndicatorState::Active, 0.5f);
    REQUIRE(canvas.End());
    const auto accent = tokens.accentRoles.fill;
    CHECK(test::Near(canvas.At(18, 11), static_cast<int>(std::lround(accent.r * 255)), static_cast<int>(std::lround(accent.g * 255)),
                     static_cast<int>(std::lround(accent.b * 255)), 255, 2));
    const int running = static_cast<int>(std::lround(tokens.runningIndicator.a * 255));
    CHECK(test::Near(canvas.At(43, 11), running, running, running, running, 2));  // white at 80%, premultiplied
    const auto caution = tokens.caution;
    CHECK(test::Near(canvas.At(68, 11), static_cast<int>(std::lround(caution.r * 255)), static_cast<int>(std::lround(caution.g * 255)),
                     static_cast<int>(std::lround(caution.b * 255)), 255, 2));
    CHECK_EQ(canvas.At(98, 11).a, 0);
    CHECK(std::abs(canvas.At(128, 11).a - 128) <= 2);
}

AETHER_TEST("glass render: item highlights, selection pills and the accent indicator follow the tokens") {
    CanvasGuard guard;
    const auto tokens = Glass(ThemeMode::Dark);
    D2DCanvas canvas;
    REQUIRE(canvas.Create(240, 60));
    canvas.Begin();
    PaintItemHighlight(canvas.Context(), D2D1::RectF(0.0f, 0.0f, 40.0f, 40.0f), 8.0f, tokens, 1.0f, 0.0f);
    PaintItemHighlight(canvas.Context(), D2D1::RectF(50.0f, 0.0f, 90.0f, 40.0f), 8.0f, tokens, 0.0f, 0.0f);
    PaintItemHighlight(canvas.Context(), D2D1::RectF(100.0f, 0.0f, 140.0f, 40.0f), 8.0f, tokens, 1.0f, 1.0f);
    PaintSelectionPill(canvas.Context(), D2D1::RectF(150.0f, 0.0f, 190.0f, 40.0f), 4.0f, tokens, true, false);
    PaintSelectionPill(canvas.Context(), D2D1::RectF(200.0f, 0.0f, 240.0f, 40.0f), 4.0f, tokens, false, false);
    PaintAccentIndicator(canvas.Context(), D2D1::RectF(10.0f, 44.0f, 13.0f, 60.0f), tokens);
    REQUIRE(canvas.End());
    const int hover = static_cast<int>(std::lround(tokens.hoverOverlay.a * 255));
    CHECK(std::abs(canvas.At(20, 20).a - hover) <= 2);
    CHECK_EQ(canvas.At(70, 20).a, 0);
    CHECK(canvas.At(120, 20).a > canvas.At(20, 20).a);  // pressed adds to hover
    CHECK(std::abs(canvas.At(170, 20).a - hover) <= 2);
    CHECK_EQ(canvas.At(220, 20).a, 0);
    const auto accent = tokens.accentRoles.fill;
    CHECK(test::Near(canvas.At(11, 52), static_cast<int>(std::lround(accent.r * 255)), static_cast<int>(std::lround(accent.g * 255)),
                     static_cast<int>(std::lround(accent.b * 255)), 255, 2));

    // High contrast: an opaque selected fill instead of a translucent overlay.
    const auto contrast = Glass(ThemeMode::HighContrast);
    D2DCanvas hc;
    REQUIRE(hc.Create(60, 60));
    hc.Begin();
    PaintSelectionPill(hc.Context(), D2D1::RectF(0.0f, 0.0f, 40.0f, 40.0f), 4.0f, contrast, true, false);
    REQUIRE(hc.End());
    CHECK_EQ(hc.At(20, 20).a, 255);
}

AETHER_TEST("glass render: the focus ring is two-tone outside the target") {
    CanvasGuard guard;
    const auto tokens = Glass(ThemeMode::Dark);
    D2DCanvas canvas;
    REQUIRE(canvas.Create(200, 120));
    canvas.Begin();
    PaintFocusRing(canvas.Context(), D2D1::RectF(60.0f, 40.0f, 140.0f, 80.0f), 4.0f, tokens);
    REQUIRE(canvas.End());
    // Inner ring: 1 DIP of focus.inner (black at 70%) after a 1-DIP gap; outer ring: 2 DIPs of white.
    CHECK_EQ(canvas.At(140, 60).a, 0);  // the gap
    const int inner = static_cast<int>(std::lround(tokens.focus.inner.a * 255));
    CHECK(test::Near(canvas.At(141, 60), 0, 0, 0, inner, 3));
    CHECK(test::Near(canvas.At(142, 60), 255, 255, 255, 255, 3));
    CHECK(test::Near(canvas.At(143, 60), 255, 255, 255, 255, 3));
    CHECK_EQ(canvas.At(145, 60).a, 0);
    CHECK_EQ(canvas.At(100, 60).a, 0);  // nothing inside the target
}

AETHER_TEST("glass render: the lift shadow fades with the lift and stays outside the icon") {
    CanvasGuard guard;
    const auto tokens = Glass(ThemeMode::Dark);
    D2DCanvas lifted;
    D2DCanvas resting;
    REQUIRE(lifted.Create(200, 140));
    REQUIRE(resting.Create(200, 140));
    lifted.Begin();
    PaintLiftShadow(lifted.Context(), D2D1::RectF(80.0f, 40.0f, 120.0f, 80.0f), 9.0f, tokens, 1.0f);
    REQUIRE(lifted.End());
    resting.Begin();
    PaintLiftShadow(resting.Context(), D2D1::RectF(80.0f, 40.0f, 120.0f, 80.0f), 9.0f, tokens, 0.0f);
    REQUIRE(resting.End());
    CHECK(lifted.At(100, 84).a > 0);
    CHECK_EQ(lifted.At(100, 60).a, 0);
    CHECK_EQ(resting.At(100, 84).a, 0);
}

AETHER_TEST("glass render: icon bitmaps are drawn into their rectangle with the requested opacity") {
    CanvasGuard guard;
    D2DCanvas canvas;
    REQUIRE(canvas.Create(120, 60));
    std::vector<std::uint8_t> red(64u * 64u * 4u);
    for (std::size_t index = 0; index < red.size(); index += 4) {
        red[index] = 0;
        red[index + 1] = 0;
        red[index + 2] = 255;
        red[index + 3] = 255;
    }
    winrt::com_ptr<ID2D1Bitmap1> bitmap;
    REQUIRE(SUCCEEDED(canvas.Context()->CreateBitmap(
        D2D1::SizeU(64, 64), red.data(), 64 * 4,
        D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_NONE,
                                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)),
        bitmap.put())));
    canvas.Begin();
    DrawIconBitmap(canvas.Context(), bitmap.get(), D2D1::RectF(10.0f, 10.0f, 42.0f, 42.0f));
    DrawIconBitmap(canvas.Context(), bitmap.get(), D2D1::RectF(60.0f, 10.0f, 92.0f, 42.0f), 0.5f);
    DrawIconBitmap(canvas.Context(), nullptr, D2D1::RectF(0.0f, 0.0f, 10.0f, 10.0f));
    REQUIRE(canvas.End());
    CHECK(test::Near(canvas.At(26, 26), 255, 0, 0, 255, 1));
    CHECK(test::Near(canvas.At(76, 26), 128, 0, 0, 128, 2));
    CHECK_EQ(canvas.At(50, 26).a, 0);
    CHECK_EQ(canvas.At(5, 5).a, 0);
}

AETHER_TEST("glass render: neutral transient material removes the tooltip sheen without removing its border") {
    CanvasGuard guard;auto tokens=Glass(ThemeMode::Dark);tokens.glass.highlight=Color::FromHex(0xffffff,0.6f);tokens.glass.noiseAlpha=0;
    D2DCanvas neutral;D2DCanvas sheen;REQUIRE(neutral.Create(200,140));REQUIRE(sheen.Create(200,140));
    GlassStyle style;style.elevation=Elevation::Tooltip;style.shadow=false;style.neutral=true;
    neutral.Begin();PaintGlassPanel(neutral.Context(),kPanel,tokens,style);REQUIRE(neutral.End());
    style.neutral=false;sheen.Begin();PaintGlassPanel(sheen.Context(),kPanel,tokens,style);REQUIRE(sheen.End());
    CHECK(MaxChannelDifference(neutral.At(100,40),neutral.At(100,100))<=2);
    CHECK(MaxChannelDifference(sheen.At(100,40),sheen.At(100,100))>5);
    CHECK(neutral.At(100,30).a>0);
}
