// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Additive design tokens: status roles and the micro duration.
#include "AetherTest.h"

#include "graphics/Theme.h"

using namespace aether::graphics;

namespace {

bool Same(const Color& a, const Color& b) { return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a; }

}  // namespace

AETHER_TEST("theme: warning stays an alias of caution in every mode") {
    for (ThemeMode mode : {ThemeMode::Dark, ThemeMode::Light, ThemeMode::HighContrast}) {
        const auto tokens = MakeDesignTokens(mode);
        CHECK(Same(tokens.warning, tokens.caution));
    }
}

AETHER_TEST("theme: status roles are distinct opaque colors outside high contrast") {
    for (ThemeMode mode : {ThemeMode::Dark, ThemeMode::Light}) {
        const auto tokens = MakeDesignTokens(mode);
        for (const auto& color : {tokens.success, tokens.caution, tokens.critical}) CHECK_EQ(color.a, 1.0f);
        CHECK_FALSE(Same(tokens.success, tokens.caution));
        CHECK_FALSE(Same(tokens.caution, tokens.critical));
        CHECK_FALSE(Same(tokens.success, tokens.critical));
    }
    const auto dark = MakeDesignTokens(ThemeMode::Dark);
    CHECK(Same(dark.critical, Color::FromHex(0xFF99A4)));
    const auto light = MakeDesignTokens(ThemeMode::Light);
    CHECK(Same(light.critical, Color::FromHex(0xC42B1C)));
    // Light-mode status colors are dark enough to read on the light glass.
    for (const auto& color : {light.success, light.caution, light.critical}) CHECK(color.r + color.g + color.b < 1.6f);
}

AETHER_TEST("theme: high contrast maps status roles to the system text color") {
    const auto tokens = MakeDesignTokens(ThemeMode::HighContrast);
    CHECK(Same(tokens.success, tokens.textPrimary));
    CHECK(Same(tokens.caution, tokens.textPrimary));
    CHECK(Same(tokens.critical, tokens.textPrimary));
}

AETHER_TEST("theme: legacy durations are unchanged and micro is the shortest") {
    const DesignTokens tokens;
    CHECK_NEAR(tokens.durationFast, 0.12, 1e-12);
    CHECK_NEAR(tokens.durationNormal, 0.22, 1e-12);
    CHECK_NEAR(tokens.durationSlow, 0.36, 1e-12);
    CHECK_NEAR(tokens.durationMicro, 0.083, 1e-12);
    CHECK(tokens.durationMicro < tokens.durationFast);
    const auto made = MakeDesignTokens(ThemeMode::Dark);
    CHECK_NEAR(made.durationMicro, 0.083, 1e-12);
    CHECK_NEAR(made.durationFast, 0.12, 1e-12);
}
