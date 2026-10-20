// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Pure helpers behind the glass material: nine-slice shadow mapping, shadow extents and bake sizes,
// the deterministic grain tile and the icon filter choice.
#include "AetherTest.h"

#include "graphics/GlassPainter.h"
#include "graphics/Theme.h"

#include <cmath>
#include <limits>

using namespace aether::graphics;

namespace {

float Width(const D2D1_RECT_F& rect) { return rect.right - rect.left; }
float Height(const D2D1_RECT_F& rect) { return rect.bottom - rect.top; }

}  // namespace

AETHER_TEST("glass geometry: nine slices keep the corners and tile the target exactly") {
    const D2D1_RECT_F target = D2D1::RectF(10.0f, 20.0f, 410.0f, 100.0f);
    const auto slices = NineSliceRects(D2D1::SizeF(60.0f, 50.0f), 24.0f, 20.0f, target);
    // Corners keep the source corner size.
    for (const std::size_t corner : {0u, 2u, 6u, 8u}) {
        CHECK_NEAR(Width(slices.target[corner]), 24.0, 1e-5);
        CHECK_NEAR(Height(slices.target[corner]), 20.0, 1e-5);
        CHECK_NEAR(Width(slices.source[corner]), 24.0, 1e-5);
        CHECK_NEAR(Height(slices.source[corner]), 20.0, 1e-5);
    }
    // Edges and centre stretch; the source middle is what is left between the corners.
    CHECK_NEAR(Width(slices.target[4]), 400.0 - 48.0, 1e-4);
    CHECK_NEAR(Height(slices.target[4]), 80.0 - 40.0, 1e-4);
    CHECK_NEAR(Width(slices.source[4]), 60.0 - 48.0, 1e-5);
    // Rows and columns are contiguous and cover the target without gaps or overlap.
    double area = 0.0;
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            const auto& slice = slices.target[static_cast<std::size_t>(row * 3 + column)];
            area += static_cast<double>(Width(slice)) * Height(slice);
            if (column > 0) CHECK_EQ(slice.left, slices.target[static_cast<std::size_t>(row * 3 + column - 1)].right);
            if (row > 0) CHECK_EQ(slice.top, slices.target[static_cast<std::size_t>((row - 1) * 3 + column)].bottom);
        }
    }
    CHECK_NEAR(area, static_cast<double>(Width(target)) * Height(target), 1e-2);
    CHECK_EQ(slices.target[0].left, target.left);
    CHECK_EQ(slices.target[8].right, target.right);
    CHECK_EQ(slices.target[8].bottom, target.bottom);
}

AETHER_TEST("glass geometry: a target narrower than two corners shrinks the corners to fit") {
    const D2D1_RECT_F narrow = D2D1::RectF(0.0f, 0.0f, 30.0f, 200.0f);
    const auto slices = NineSliceRects(D2D1::SizeF(60.0f, 60.0f), 24.0f, 24.0f, narrow);
    CHECK_NEAR(Width(slices.target[0]), 15.0, 1e-5);
    CHECK_NEAR(Width(slices.target[2]), 15.0, 1e-5);
    CHECK_NEAR(Width(slices.target[1]), 0.0, 1e-5);  // nothing left to stretch horizontally
    CHECK_NEAR(Height(slices.target[0]), 24.0, 1e-5);
    CHECK(slices.target[2].right <= narrow.right + 1e-5f);
    // Corners larger than the source are clamped to half of it.
    const auto clamped = NineSliceRects(D2D1::SizeF(10.0f, 10.0f), 50.0f, 50.0f, D2D1::RectF(0, 0, 100, 100));
    CHECK_NEAR(Width(clamped.source[0]), 5.0, 1e-5);
    // A degenerate target never produces negative slices.
    const auto empty = NineSliceRects(D2D1::SizeF(10.0f, 10.0f), 4.0f, 4.0f, D2D1::RectF(5, 5, 5, 5));
    for (const auto& slice : empty.target) {
        CHECK(Width(slice) >= 0.0f);
        CHECK(Height(slice) >= 0.0f);
    }
}

AETHER_TEST("glass geometry: shadow extents cover three standard deviations plus the offset") {
    ElevationLayers layers;
    layers.key = ShadowLayer{5.0f, 4.0f, 0.3f};
    layers.ambient = ShadowLayer{10.0f, 1.0f, 0.2f};
    CHECK_NEAR(ShadowExtent(layers), 31.0, 1e-6);
    layers.ambient.alpha = 0.0f;  // an invisible layer does not count
    CHECK_NEAR(ShadowExtent(layers), 19.0, 1e-6);
    CHECK_NEAR(ShadowExtent(ElevationLayers{}), 0.0, 0.0);
    layers.key.blur = std::numeric_limits<float>::quiet_NaN();
    CHECK(std::isfinite(ShadowExtent(layers)));

    const auto tokens = MakeDesignTokens(ThemeMode::Dark);
    CHECK(&ElevationFor(tokens, Elevation::Dock) == &tokens.elevation.dock);
    CHECK(&ElevationFor(tokens, Elevation::Popup) == &tokens.elevation.popup);
    CHECK(&ElevationFor(tokens, Elevation::Lift) == &tokens.elevation.lift);
    CHECK_EQ(ElevationFor(tokens, Elevation::None).key.alpha, 0.0f);
    // Larger surfaces float higher.
    CHECK(ShadowExtent(tokens.elevation.tooltip) < ShadowExtent(tokens.elevation.dock));
    CHECK(ShadowExtent(tokens.elevation.bar) < ShadowExtent(tokens.elevation.popup));
}

AETHER_TEST("glass geometry: shadows are baked at the shortest stretchable size") {
    // A long side bakes only corners, extents and a small uniform middle.
    CHECK_NEAR(ShadowBakeLength(1000.0f, 31.0f, 18.0f), 2.0 * 31.0 + 2.0 * 18.0 + 2.0, 1e-5);
    // A short side bakes exactly, so the bitmap matches it without stretching.
    CHECK_NEAR(ShadowBakeLength(40.0f, 31.0f, 18.0f), 40.0, 1e-6);
    CHECK_NEAR(ShadowBakeLength(-1.0f, 31.0f, 18.0f), 0.0, 0.0);
    CHECK_NEAR(ShadowBakeLength(std::numeric_limits<float>::infinity(), 31.0f, 18.0f), 0.0, 0.0);
    // Any panel width beyond the stretchable size shares one bake (Dock magnification).
    CHECK_EQ(ShadowBakeLength(600.0f, 31.0f, 18.0f), ShadowBakeLength(612.5f, 31.0f, 18.0f));
}

AETHER_TEST("glass geometry: the grain tile is deterministic, premultiplied and subtle") {
    const auto a = MakeNoiseTile(128, 0x2545F491u, 0.02f);
    const auto b = MakeNoiseTile(128, 0x2545F491u, 0.02f);
    REQUIRE_EQ(a.size(), static_cast<std::size_t>(128 * 128 * 4));
    CHECK(a == b);
    CHECK(a != MakeNoiseTile(128, 7u, 0.02f));
    double alphaSum = 0.0;
    std::size_t white = 0;
    for (std::size_t index = 0; index < a.size(); index += 4) {
        const auto alpha = a[index + 3];
        // Premultiplied: black or white specks never exceed their alpha.
        CHECK(a[index] <= alpha);
        CHECK(a[index] == a[index + 1] && a[index + 1] == a[index + 2]);
        if (a[index] != 0) ++white;
        alphaSum += alpha;
    }
    const double meanAlpha = alphaSum / (128.0 * 128.0) / 255.0;
    CHECK(std::fabs(meanAlpha - 0.02) <= 0.002);
    // Roughly half of the specks lighten and half darken, so the grain does not shift the tint.
    const double whiteShare = static_cast<double>(white) / (128.0 * 128.0);
    CHECK(whiteShare > 0.4 && whiteShare < 0.6);

    const auto none = MakeNoiseTile(16, 1u, 0.0f);
    for (std::size_t index = 3; index < none.size(); index += 4) CHECK_EQ(none[index], 0);
    CHECK(MakeNoiseTile(4096, 1u, 0.02f).size() <= static_cast<std::size_t>(1024 * 1024 * 4));  // bounded
    CHECK(MakeNoiseTile(8, 0u, 0.02f) == MakeNoiseTile(8, 0u, 0.02f));  // a zero seed uses the fixed default
}

AETHER_TEST("glass geometry: icons use high-quality cubic filtering only when clearly downscaled") {
    CHECK(IconInterpolation(256.0f, 48.0f) == D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC);
    CHECK(IconInterpolation(64.0f, 48.0f) == D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC);
    CHECK(IconInterpolation(56.0f, 48.0f) == D2D1_INTERPOLATION_MODE_LINEAR);  // 1.17x
    CHECK(IconInterpolation(48.0f, 96.0f) == D2D1_INTERPOLATION_MODE_LINEAR);  // upscaled
    CHECK(IconInterpolation(0.0f, 48.0f) == D2D1_INTERPOLATION_MODE_LINEAR);
    CHECK(IconInterpolation(std::numeric_limits<float>::quiet_NaN(), 48.0f) == D2D1_INTERPOLATION_MODE_LINEAR);
}
