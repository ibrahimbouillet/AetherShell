// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Design-language tokens: color roles and their contrast, the Windows accent with variant stepping
// and the brand fallback, theme-mode resolution and the legacy fields other modules still read.
// Palettes are synthetic; nothing here reads the real accent color.
#include "AetherTest.h"

#include "graphics/Glyphs.h"
#include "graphics/Theme.h"

#include <windows.h>

#include <cmath>

using namespace aether::graphics;

namespace {

bool Same(const Color& a, const Color& b) { return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a; }

// The Windows default blue accent palette (UISettings values for the default accent).
SystemPalette BluePalette() {
    SystemPalette palette;
    palette.available = true;
    palette.accent = Color::FromHex(0x0078D4);
    palette.accentLight1 = Color::FromHex(0x429CE3);
    palette.accentLight2 = Color::FromHex(0x76B9ED);
    palette.accentLight3 = Color::FromHex(0x99EBFF);
    palette.accentDark1 = Color::FromHex(0x005A9E);
    palette.accentDark2 = Color::FromHex(0x004275);
    palette.accentDark3 = Color::FromHex(0x002642);
    return palette;
}

AppearanceOptions WindowsAccent() {
    AppearanceOptions options;
    options.accent = AccentSource::Windows;
    return options;
}

}  // namespace

AETHER_TEST("design tokens: text and accent roles keep their contrast in dark and light mode") {
    for (ThemeMode mode : {ThemeMode::Dark, ThemeMode::Light}) {
        for (const bool windowsAccent : {false, true}) {
            const auto tokens = windowsAccent ? MakeDesignTokens(mode, BluePalette(), WindowsAccent()) : MakeDesignTokens(mode);
            CHECK(ContrastRatio(tokens.textPrimary, tokens.surface.background) >= 7.0f);
            CHECK(ContrastRatio(tokens.textPrimary, tokens.surface.card) >= 7.0f);
            CHECK(ContrastRatio(tokens.textSecondary, tokens.surface.card) >= 4.5f);
            CHECK(ContrastRatio(tokens.accentRoles.textOnAccent, tokens.accentRoles.fill) >= 4.5f);
            CHECK(ContrastRatio(tokens.accentRoles.fill, tokens.surface.card) >= 3.0f);
            CHECK(ContrastRatio(tokens.surface.strongStroke, tokens.surface.card) >= 3.0f);
            // The accent roles are opaque, so GDI-drawn controls can use them as they are.
            CHECK_EQ(tokens.accentRoles.fill.a, 1.0f);
            CHECK(Same(tokens.accent, tokens.accentRoles.fill));
            CHECK(Same(tokens.focusRing, tokens.focus.outer));
        }
    }
}

AETHER_TEST("design tokens: the Windows accent uses light variants in dark mode and dark variants in light mode") {
    const auto palette = BluePalette();
    const auto dark = MakeDesignTokens(ThemeMode::Dark, palette, WindowsAccent());
    CHECK(dark.systemAccent);
    CHECK(dark.accentSource == AccentSource::Windows);
    CHECK(Same(dark.accentRoles.fill, *palette.accentLight2));
    CHECK(Same(dark.info, dark.accentRoles.fill));
    const auto light = MakeDesignTokens(ThemeMode::Light, palette, WindowsAccent());
    CHECK(light.systemAccent);
    CHECK(Same(light.accentRoles.fill, *palette.accentDark1));
    // The brand gradient stays available for the logo whatever the accent.
    CHECK(Same(dark.brandPrimary, Color::FromHex(0x5B6CFF)));
    CHECK(Same(light.brandSecondary, Color::FromHex(0x3DD9E8)));
}

AETHER_TEST("design tokens: a low-contrast accent variant is stepped until it keeps 3:1 on the card") {
    SystemPalette palette = BluePalette();
    // In dark mode the preferred variant (Light2) is nearly the card color; Light3 is next in line.
    palette.accentLight2 = Color::FromHex(0x2E2E30);
    const auto dark = MakeDesignTokens(ThemeMode::Dark, palette, WindowsAccent());
    CHECK(Same(dark.accentRoles.fill, *palette.accentLight3));
    CHECK(ContrastRatio(dark.accentRoles.fill, dark.surface.card) >= 3.0f);

    // In light mode a pale Dark1 is skipped for Dark2.
    palette = BluePalette();
    palette.accentDark1 = Color::FromHex(0xF0F0F0);
    const auto light = MakeDesignTokens(ThemeMode::Light, palette, WindowsAccent());
    CHECK(Same(light.accentRoles.fill, *palette.accentDark2));

    // When no variant reaches 3:1, the best one is used rather than none.
    SystemPalette weak;
    weak.available = true;
    weak.accent = Color::FromHex(0x404040);
    weak.accentLight2 = Color::FromHex(0x2C2C2C);
    const auto fallback = MakeDesignTokens(ThemeMode::Dark, weak, WindowsAccent());
    CHECK(fallback.systemAccent);
    CHECK(Same(fallback.accentRoles.fill, *weak.accent));
}

AETHER_TEST("design tokens: an unavailable palette or the Aether source keeps the brand accent") {
    const auto brandDark = MakeDesignTokens(ThemeMode::Dark);
    SystemPalette unavailable = BluePalette();
    unavailable.available = false;
    const auto noPalette = MakeDesignTokens(ThemeMode::Dark, unavailable, WindowsAccent());
    CHECK_FALSE(noPalette.systemAccent);
    CHECK(Same(noPalette.accentRoles.fill, brandDark.accentRoles.fill));
    CHECK(Same(noPalette.accentRoles.fill, Color::FromHex(0x5B6CFF)));

    AppearanceOptions aether;
    aether.accent = AccentSource::Aether;
    const auto brand = MakeDesignTokens(ThemeMode::Light, BluePalette(), aether);
    CHECK_FALSE(brand.systemAccent);
    CHECK(brand.accentSource == AccentSource::Aether);
    CHECK(Same(brand.accentRoles.fill, MakeDesignTokens(ThemeMode::Light).accentRoles.fill));
}

AETHER_TEST("design tokens: the deterministic overload is repeatable and keeps every legacy field") {
    for (ThemeMode mode : {ThemeMode::Dark, ThemeMode::Light}) {
        const auto a = MakeDesignTokens(mode);
        const auto b = MakeDesignTokens(mode);
        CHECK(Same(a.glass.tint, b.glass.tint));
        CHECK(Same(a.accent, b.accent));
        CHECK(Same(a.textPrimary, b.textPrimary));
        CHECK_FALSE(a.systemAccent);
        CHECK(a.accentSource == AccentSource::Aether);
        // Legacy geometry and motion fields are derived from the role groups.
        CHECK_EQ(a.dockCornerRadius, a.radii.dockMax);
        CHECK_EQ(a.barCornerRadius, a.radii.bar);
        CHECK_EQ(a.popupCornerRadius, a.radii.popup);
        CHECK_EQ(a.tooltipCornerRadius, a.radii.tooltip);
        CHECK_EQ(a.itemCornerRadius, a.radii.item);
        CHECK_EQ(a.focusRingWidth, a.focus.outerWidth);
        CHECK_EQ(a.fontSizeSmall, a.type.caption);
        CHECK_EQ(a.fontSizeBody, a.type.body);
        CHECK(Same(a.warning, a.caution));
        CHECK_NEAR(a.durationFast, 0.12, 1e-12);
        CHECK_NEAR(a.durationNormal, 0.22, 1e-12);
        CHECK_NEAR(a.durationSlow, 0.36, 1e-12);
        CHECK_NEAR(a.tooltipWarmWindow, 0.6, 1e-12);
        // The legacy single shadow mirrors the Dock elevation's key layer.
        CHECK_NEAR(a.glass.shadow.a, a.elevation.dock.key.alpha, 1e-6);
        CHECK_NEAR(a.glass.shadowOffsetY, a.elevation.dock.key.offsetY, 1e-6);
        CHECK(a.glass.shadowBlur > 0.0f);
        // Neutral material: the tint is a gray, the frosted backdrop is fully opaque and the grain subtle.
        CHECK_EQ(a.glass.tint.r, a.glass.tint.g);
        CHECK_EQ(a.glass.tint.g, a.glass.tint.b);
        CHECK_NEAR(a.glass.backdropOpacity, 1.0, 1e-6);
        CHECK(a.glass.noiseAlpha > 0.0f && a.glass.noiseAlpha <= 0.03f);
        CHECK(a.glass.highlightFraction <= 0.35f);
        // Two shadow layers: a tighter key shadow and a wider ambient one.
        for (const auto* layers : {&a.elevation.tooltip, &a.elevation.bar, &a.elevation.dock, &a.elevation.popup}) {
            CHECK(layers->key.alpha > 0.0f);
            CHECK(layers->ambient.alpha > 0.0f);
            CHECK(layers->ambient.blur > layers->key.blur);
        }
        CHECK(a.type.caption < a.type.body && a.type.body < a.type.subtitle && a.type.subtitle < a.type.title);
        CHECK(a.fontFamily == L"Segoe UI Variable Text");
    }
}

AETHER_TEST("design tokens: high contrast is opaque, flat and uses the selection system colors") {
    const auto tokens = MakeDesignTokens(ThemeMode::HighContrast);
    CHECK_FALSE(tokens.transparencyEnabled);
    CHECK_EQ(tokens.glass.tintAlphaWithBlur, 1.0f);
    CHECK_EQ(tokens.glass.tintAlphaWithoutBlur, 1.0f);
    CHECK_EQ(tokens.glass.noiseAlpha, 0.0f);
    CHECK_EQ(tokens.glass.highlight.a, 0.0f);
    for (const auto* layers : {&tokens.elevation.tooltip, &tokens.elevation.bar, &tokens.elevation.dock,
                               &tokens.elevation.popup, &tokens.elevation.lift}) {
        CHECK_EQ(layers->key.alpha, 0.0f);
        CHECK_EQ(layers->ambient.alpha, 0.0f);
    }
    CHECK(Same(tokens.surface.selectedFill, Color::FromColorRef(::GetSysColor(COLOR_HIGHLIGHT))));
    CHECK(Same(tokens.surface.selectedText, Color::FromColorRef(::GetSysColor(COLOR_HIGHLIGHTTEXT))));
    CHECK(Same(tokens.textPrimary, Color::FromColorRef(::GetSysColor(COLOR_WINDOWTEXT))));
    CHECK(tokens.focus.outerWidth >= 3.0f);
    // A palette never overrides the system colors in high contrast.
    const auto withPalette = MakeDesignTokens(ThemeMode::HighContrast, BluePalette(), WindowsAccent());
    CHECK_FALSE(withPalette.systemAccent);
    CHECK(Same(withPalette.accentRoles.fill, tokens.accentRoles.fill));
}

AETHER_TEST("design tokens: theme resolution lets high contrast win and preferences override the system") {
    CHECK(ResolveThemeMode(ThemePreference::System, false, true) == ThemeMode::Dark);
    CHECK(ResolveThemeMode(ThemePreference::System, false, false) == ThemeMode::Light);
    CHECK(ResolveThemeMode(ThemePreference::Light, false, true) == ThemeMode::Light);
    CHECK(ResolveThemeMode(ThemePreference::Dark, false, false) == ThemeMode::Dark);
    for (ThemePreference preference : {ThemePreference::System, ThemePreference::Light, ThemePreference::Dark}) {
        CHECK(ResolveThemeMode(preference, true, false) == ThemeMode::HighContrast);
        CHECK(ResolveThemeMode(preference, true, true) == ThemeMode::HighContrast);
    }
}

AETHER_TEST("design tokens: contrast, compositing and color conversions are exact") {
    CHECK_NEAR(ContrastRatio(Color::FromHex(0x000000), Color::FromHex(0xFFFFFF)), 21.0, 1e-3);
    CHECK_NEAR(ContrastRatio(Color::FromHex(0x777777), Color::FromHex(0x777777)), 1.0, 1e-6);
    CHECK_NEAR(ContrastRatio(Color::FromHex(0xFFFFFF), Color::FromHex(0x000000)), 21.0, 1e-3);  // symmetric
    CHECK_NEAR(RelativeLuminance(Color::FromHex(0xFFFFFF)), 1.0, 1e-6);
    CHECK_NEAR(RelativeLuminance(Color{std::nanf(""), 0.0f, 0.0f, 1.0f}), 0.0, 1e-6);  // non-finite channels count as 0

    const Color half = CompositeOver(Color::FromHex(0xFFFFFF, 0.5f), Color::FromHex(0x000000));
    CHECK_NEAR(half.r, 0.5, 1e-6);
    CHECK_EQ(half.a, 1.0f);
    const Color opaque = CompositeOver(Color::FromHex(0x123456), Color::FromHex(0xFFFFFF));
    CHECK(Same(opaque, Color::FromHex(0x123456)));

    const Color source = Color::FromHex(0x5B6CFF);
    CHECK_EQ(source.ToColorRef(), static_cast<unsigned long>(RGB(0x5B, 0x6C, 0xFF)));
    CHECK(Same(Color::FromColorRef(source.ToColorRef()), source));
    CHECK_EQ((Color{2.0f, -1.0f, std::nanf(""), 1.0f}).ToColorRef(), static_cast<unsigned long>(RGB(255, 0, 0)));
}

AETHER_TEST("design tokens: the Dock radius follows the panel height within its clamp") {
    const auto tokens = MakeDesignTokens(ThemeMode::Dark);
    CHECK_NEAR(DockCornerRadius(tokens, 60.0f), 18.0, 1e-5);
    CHECK_NEAR(DockCornerRadius(tokens, 20.0f), tokens.radii.dockMin, 1e-6);
    CHECK_NEAR(DockCornerRadius(tokens, 400.0f), tokens.radii.dockMax, 1e-6);
    CHECK_NEAR(DockCornerRadius(tokens, -5.0f), tokens.radii.dockMin, 1e-6);
    CHECK_NEAR(DockCornerRadius(tokens, std::nanf("")), tokens.radii.dockMin, 1e-6);
}

AETHER_TEST("design tokens: the palette's text scale is clamped into the supported range") {
    SystemPalette palette = BluePalette();
    palette.textScale = 1.5f;
    CHECK_NEAR(MakeDesignTokens(ThemeMode::Dark, palette, WindowsAccent()).textScale, 1.5, 1e-6);
    palette.textScale = 9.0f;
    CHECK_NEAR(MakeDesignTokens(ThemeMode::Dark, palette, WindowsAccent()).textScale, 2.25, 1e-6);
    palette.textScale = 0.2f;
    CHECK_NEAR(MakeDesignTokens(ThemeMode::Dark, palette, WindowsAccent()).textScale, 1.0, 1e-6);
    palette.textScale = std::nanf("");
    CHECK_NEAR(MakeDesignTokens(ThemeMode::Dark, palette, WindowsAccent()).textScale, 1.0, 1e-6);
}

AETHER_TEST("design tokens: the Start mark is Windows blue by default and follows the Windows accent") {
    CHECK(Same(StartMarkColor(MakeDesignTokens(ThemeMode::Light)), Color::FromHex(0x0078D4)));
    CHECK(Same(StartMarkColor(MakeDesignTokens(ThemeMode::Dark)), Color::FromHex(0x4CC2FF)));
    const auto accented = MakeDesignTokens(ThemeMode::Dark, BluePalette(), WindowsAccent());
    CHECK(Same(StartMarkColor(accented), accented.accentRoles.fill));
    AppearanceOptions aether;
    aether.accent = AccentSource::Aether;
    CHECK(Same(StartMarkColor(MakeDesignTokens(ThemeMode::Light, BluePalette(), aether)), Color::FromHex(0x0078D4)));
    const auto contrast = MakeDesignTokens(ThemeMode::HighContrast);
    CHECK(Same(StartMarkColor(contrast), contrast.textPrimary));
}

AETHER_TEST("form tokens: spacing and control metrics follow the shared 4-DIP rhythm") {
    const auto form = MakeDesignTokens(ThemeMode::Dark).form;
    const float metrics[] = {form.outer, form.paneGap, form.sectionGap, form.sectionTitleGap, form.relatedGap,
                             form.cardPaddingX, form.rowMinHeight, form.controlGap, form.controlHeight,
                             form.buttonPaddingX, form.buttonMinWidth, form.comboMinWidth, form.valueFieldWidth,
                             form.sliderWidth, form.switchWidth, form.switchHeight, form.switchKnob, form.glyphSize,
                             form.navWidth, form.navRailWidth, form.navItemHeight, form.navIndicatorHeight,
                             form.footerHeight, form.scrollbarWidth, form.scrollbarHoverWidth, form.contentMaxWidth};
    for (const float value : metrics) CHECK_EQ(std::fmod(value, 4.0f), 0.0f);
    // Nested radii: controls sit inside cards with smaller corners; the window body has no own corner.
    CHECK(form.controlRadius < form.cardRadius);
    CHECK(form.fieldRadius <= form.cardRadius);
    CHECK(form.titleSize > form.sectionSize);
    CHECK(form.helperSize < form.bodySize);
    // Motion vocabulary: small feedback < popups/indicators < page change, all finite and short.
    CHECK(form.feedback < form.indicator);
    CHECK(form.page <= 0.36);
    CHECK(form.feedback >= 0.12);
}

AETHER_TEST("form tokens: native editors sit on opaque fields with readable text in every mode") {
    for (const auto mode : {ThemeMode::Dark, ThemeMode::Light, ThemeMode::HighContrast}) {
        const auto tokens = MakeDesignTokens(mode);
        const auto& form = tokens.form;
        // The native EDIT/HOTKEY/LISTBOX background must equal the drawn field exactly, so it is opaque.
        CHECK_EQ(form.fieldFill.a, 1.0f);
        CHECK_EQ(form.fieldFillDisabled.a, 1.0f);
        CHECK_EQ(form.windowSolid.a, 1.0f);
        CHECK_EQ(form.cardFillSolid.a, 1.0f);
        CHECK(ContrastRatio(form.fieldText, form.fieldFill) >= 4.5f);
        // Opaque fallback window: primary and helper text on cards and on the window itself.
        const Color card = CompositeOver(form.cardFillSolid, form.windowSolid);
        CHECK(ContrastRatio(tokens.textPrimary, card) >= 4.5f);
        CHECK(ContrastRatio(tokens.textPrimary, form.windowSolid) >= 4.5f);
        if (mode != ThemeMode::HighContrast) {
            CHECK(ContrastRatio(tokens.textSecondary, card) >= 4.5f);
            // A switch that is off stays distinguishable from its card (WCAG non-text 3:1).
            CHECK(ContrastRatio(form.switchOffStroke, card) >= 3.0f);
            // Translucent layers stay translucent; the window tint keeps the backdrop visible.
            CHECK(form.cardFill.a < 1.0f);
            CHECK(form.buttonFill.a < 1.0f);
            CHECK(form.windowTintAlpha > 0.0f);
            CHECK(form.windowTintAlpha < 1.0f);
        }
    }
}

AETHER_TEST("menu tokens: compact rows, a modest entrance and a quicker exit") {
    const auto menu = MakeDesignTokens(ThemeMode::Light).menu;
    CHECK(menu.itemHeight >= 28.0f);
    CHECK(menu.itemHeight <= 40.0f);
    CHECK(menu.minWidth < menu.maxWidth);
    CHECK(menu.rowRadius < menu.radius);
    CHECK(menu.enterScale > 0.9f);
    CHECK(menu.enterScale < 1.0f);
    CHECK(menu.close < menu.open);
    CHECK(menu.open <= 0.30);
}
