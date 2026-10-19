// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "graphics/Theme.h"

#include "platform/windows/WindowUtilities.h"

#include <windows.h>

namespace aether::graphics {
namespace {

// Brand palette (see docs/BRANDING.md): an indigo-to-cyan "aurora" gradient.
constexpr unsigned kBrandIndigo = 0x5B6CFF;
constexpr unsigned kBrandCyan = 0x3DD9E8;

}  // namespace

Color Color::FromColorRef(unsigned long colorref, float alpha) noexcept {
    return Color{static_cast<float>(GetRValue(colorref)) / 255.0f, static_cast<float>(GetGValue(colorref)) / 255.0f,
                 static_cast<float>(GetBValue(colorref)) / 255.0f, alpha};
}

ThemeMode DetectThemeMode() noexcept {
    if (platform::HighContrastEnabled()) return ThemeMode::HighContrast;
    return platform::SystemPrefersDarkApps() ? ThemeMode::Dark : ThemeMode::Light;
}

DesignTokens MakeDesignTokens(ThemeMode mode) {
    DesignTokens t;
    t.mode = mode;
    t.transparencyEnabled = platform::SystemTransparencyEnabled() && mode != ThemeMode::HighContrast;
    t.accent = Color::FromHex(kBrandIndigo);
    t.accentSecondary = Color::FromHex(kBrandCyan);

    switch (mode) {
        case ThemeMode::Dark:
            t.glass.tint = Color::FromHex(0x161A26);
            t.glass.tintAlphaWithBlur = 0.46f;
            t.glass.tintAlphaWithoutBlur = 0.66f;
            t.glass.borderTop = Color::FromHex(0xFFFFFF, 0.24f);
            t.glass.borderBottom = Color::FromHex(0xFFFFFF, 0.06f);
            t.glass.highlight = Color::FromHex(0xFFFFFF, 0.10f);
            t.glass.shadow = Color::FromHex(0x000000, 0.42f);
            t.textPrimary = Color::FromHex(0xF5F7FF);
            t.textSecondary = Color::FromHex(0xC3C9DA);
            t.textDisabled = Color::FromHex(0x8A90A2);
            t.focusRing = Color::FromHex(0x9FB4FF);
            t.runningIndicator = Color::FromHex(0xE8ECFF, 0.92f);
            t.hoverOverlay = Color::FromHex(0xFFFFFF, 0.10f);
            t.pressedOverlay = Color::FromHex(0xFFFFFF, 0.18f);
            t.fallbackIconBackground = Color::FromHex(0x3A4258);
            t.success = Color::FromHex(0x6CCB5F);
            t.caution = Color::FromHex(0xFCE100);
            t.critical = Color::FromHex(0xFF99A4);
            break;
        case ThemeMode::Light:
            t.glass.tint = Color::FromHex(0xF4F6FC);
            t.glass.tintAlphaWithBlur = 0.58f;
            t.glass.tintAlphaWithoutBlur = 0.72f;
            t.glass.borderTop = Color::FromHex(0xFFFFFF, 0.85f);
            t.glass.borderBottom = Color::FromHex(0x1B2033, 0.10f);
            t.glass.highlight = Color::FromHex(0xFFFFFF, 0.35f);
            t.glass.shadow = Color::FromHex(0x0E1530, 0.22f);
            t.textPrimary = Color::FromHex(0x141826);
            t.textSecondary = Color::FromHex(0x4A5168);
            t.textDisabled = Color::FromHex(0x8C92A5);
            t.focusRing = Color::FromHex(0x3446E0);
            t.runningIndicator = Color::FromHex(0x1E2436, 0.85f);
            t.hoverOverlay = Color::FromHex(0x0E1530, 0.07f);
            t.pressedOverlay = Color::FromHex(0x0E1530, 0.13f);
            t.fallbackIconBackground = Color::FromHex(0xC9D0E3);
            t.success = Color::FromHex(0x0F7B0F);
            t.caution = Color::FromHex(0x9D5D00);
            t.critical = Color::FromHex(0xC42B1C);
            break;
        case ThemeMode::HighContrast: {
            // System colors; no translucency, no decorative effects.
            const Color window = Color::FromColorRef(::GetSysColor(COLOR_WINDOW));
            const Color windowText = Color::FromColorRef(::GetSysColor(COLOR_WINDOWTEXT));
            const Color highlight = Color::FromColorRef(::GetSysColor(COLOR_HIGHLIGHT));
            const Color grayText = Color::FromColorRef(::GetSysColor(COLOR_GRAYTEXT));
            t.glass.tint = window;
            t.glass.tintAlphaWithBlur = 1.0f;
            t.glass.tintAlphaWithoutBlur = 1.0f;
            t.glass.borderTop = windowText;
            t.glass.borderBottom = windowText;
            t.glass.highlight = window.WithAlpha(0.0f);
            t.glass.shadow = Color{0, 0, 0, 0};
            t.textPrimary = windowText;
            t.textSecondary = windowText;
            t.textDisabled = grayText;
            t.focusRing = highlight;
            t.runningIndicator = windowText;
            t.hoverOverlay = highlight.WithAlpha(0.35f);
            t.pressedOverlay = highlight.WithAlpha(0.6f);
            t.fallbackIconBackground = grayText;
            t.accent = highlight;
            t.accentSecondary = highlight;
            t.focusRingWidth = 3.0f;
            t.success = windowText;
            t.caution = windowText;
            t.critical = windowText;
            break;
        }
    }
    t.warning = t.caution;
    return t;
}

}  // namespace aether::graphics
