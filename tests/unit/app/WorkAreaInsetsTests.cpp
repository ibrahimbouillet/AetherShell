// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "AetherTest.h"
#include "app/WorkAreaInsets.h"

#include <limits>

using namespace aether::app;

AETHER_TEST("work-area insets: twelve DIP reserves symmetric primary monitor side strips") {
    const RECT monitor{0, 0, 1920, 1080};
    auto layout = ComputeWorkAreaInsets(monitor, 96); REQUIRE(layout.ok());
    CHECK_EQ(layout->widthPx, 12);
    CHECK_EQ(layout->left.left, 0); CHECK_EQ(layout->left.right, 12);
    CHECK_EQ(layout->right.left, 1908); CHECK_EQ(layout->right.right, 1920);
    CHECK_EQ(layout->left.top, monitor.top); CHECK_EQ(layout->right.bottom, monitor.bottom);
    CHECK_EQ(layout->right.left - layout->left.right, 1896);
}

AETHER_TEST("work-area insets: monitor DPI and negative coordinates preserve equal margins") {
    const RECT monitor{-2560, -400, 0, 1040};
    for (const UINT dpi : {96u, 120u, 144u, 168u, 192u}) {
        auto layout = ComputeWorkAreaInsets(monitor, dpi); REQUIRE(layout.ok());
        CHECK_EQ(layout->widthPx, 12 * static_cast<int>(dpi) / 96);
        CHECK_EQ(layout->left.right - layout->left.left, layout->widthPx);
        CHECK_EQ(layout->right.right - layout->right.left, layout->widthPx);
        CHECK_EQ(layout->left.left, monitor.left); CHECK_EQ(layout->right.right, monitor.right);
    }
    auto fractional = ComputeWorkAreaInsets(monitor, 102); REQUIRE(fractional.ok());
    CHECK_EQ(fractional->widthPx, 13); // same nearest-pixel conversion as the Top Bar's visual inset
}

AETHER_TEST("work-area insets: AppBar approved vertical extent and existing outer allocation are honored") {
    const RECT approved{20, 93, 1900, 969};
    const auto left = FitWorkAreaInset(approved, true, 12);
    const auto right = FitWorkAreaInset(approved, false, 12);
    REQUIRE(left.ok()); REQUIRE(right.ok());
    CHECK_EQ(left->left, 20); CHECK_EQ(left->right, 32);
    CHECK_EQ(right->left, 1888); CHECK_EQ(right->right, 1900);
    CHECK_EQ(left->top, 93); CHECK_EQ(left->bottom, 969);
    CHECK_EQ(right->top, 93); CHECK_EQ(right->bottom, 969);
    // QUERYPOS does rectangle subtraction; the opposite edge need not retain the proposed width.
    REQUIRE(FitWorkAreaInset(RECT{20, 93, 12, 969}, true, 12).ok());
}

AETHER_TEST("work-area insets: malformed monitor geometry cannot create a shell reservation") {
    const RECT monitor{0, 0, 1920, 1080};
    CHECK_FALSE(ComputeWorkAreaInsets(monitor, 0).ok());
    CHECK_FALSE(ComputeWorkAreaInsets(monitor, 769).ok());
    CHECK_FALSE(ComputeWorkAreaInsets(monitor, 96, 0).ok());
    CHECK_FALSE(ComputeWorkAreaInsets(monitor, 96, -1).ok());
    CHECK_FALSE(ComputeWorkAreaInsets(monitor, 96, std::numeric_limits<float>::quiet_NaN()).ok());
    CHECK_FALSE(ComputeWorkAreaInsets(RECT{0, 0, 24, 100}, 96).ok());
    CHECK_FALSE(ComputeWorkAreaInsets(RECT{0, 0, 100, 0}, 96).ok());
    CHECK_FALSE(ComputeWorkAreaInsets(RECT{1, 0, 0, 100}, 96).ok());
    const RECT extreme{std::numeric_limits<LONG>::min(), 0, std::numeric_limits<LONG>::max(), 1080};
    CHECK_FALSE(ComputeWorkAreaInsets(extreme, 96).ok());
}

AETHER_TEST("work-area insets: malformed approved bounds and native overflow are rejected") {
    CHECK_FALSE(FitWorkAreaInset(RECT{0, 93, 1920, 93}, true, 12).ok());
    CHECK_FALSE(FitWorkAreaInset(RECT{0, 0, 1920, 1080}, true, 0).ok());
    CHECK_FALSE(FitWorkAreaInset(RECT{0, std::numeric_limits<LONG>::min(), 1920, std::numeric_limits<LONG>::max()}, true, 12).ok());
    CHECK_FALSE(FitWorkAreaInset(RECT{std::numeric_limits<LONG>::max(), 0, 0, 1080}, true, 12).ok());
    CHECK_FALSE(FitWorkAreaInset(RECT{0, 0, std::numeric_limits<LONG>::min(), 1080}, false, 12).ok());
}
