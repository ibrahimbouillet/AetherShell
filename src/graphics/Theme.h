// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Design tokens: the single source of truth for colors, glass parameters,
// geometry, typography and motion used by every Aether Shell surface.
#pragma once

#include <d2d1.h>

#include <string>

namespace aether::graphics {

struct Color {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;

    [[nodiscard]] static constexpr Color FromHex(unsigned rgb, float alpha = 1.0f) noexcept {
        return Color{static_cast<float>((rgb >> 16) & 0xFF) / 255.0f, static_cast<float>((rgb >> 8) & 0xFF) / 255.0f,
                     static_cast<float>(rgb & 0xFF) / 255.0f, alpha};
    }
    [[nodiscard]] static Color FromColorRef(unsigned long colorref, float alpha = 1.0f) noexcept;
    [[nodiscard]] constexpr Color WithAlpha(float alpha) const noexcept { return Color{r, g, b, alpha}; }
    [[nodiscard]] constexpr D2D1_COLOR_F ToD2D() const noexcept { return D2D1_COLOR_F{r, g, b, a}; }
};

enum class ThemeMode { Dark, Light, HighContrast };

struct GlassTokens {
    Color tint;                    // panel tint painted over the (blurred) backdrop
    float tintAlphaWithBlur = 0.5f;
    float tintAlphaWithoutBlur = 0.88f;  // without blur the panel must be more opaque to stay readable
    Color borderTop;               // 1-DIP inner border, gradient from top...
    Color borderBottom;            // ...to bottom
    Color highlight;               // soft specular sheen at the top of the panel
    float highlightFraction = 0.55f;
    Color shadow;
    float shadowBlur = 14.0f;      // DIPs (standard deviation)
    float shadowOffsetY = 6.0f;    // DIPs
};

struct DesignTokens {
    ThemeMode mode = ThemeMode::Dark;
    GlassTokens glass;

    Color textPrimary;
    Color textSecondary;
    Color textDisabled;
    Color accent;           // brand primary
    Color accentSecondary;  // brand secondary (aurora gradient end)
    Color focusRing;
    Color runningIndicator;
    Color hoverOverlay;
    Color pressedOverlay;
    Color warning;
    Color fallbackIconBackground;

    // Geometry (DIPs)
    float dockCornerRadius = 22.0f;
    float barCornerRadius = 14.0f;
    float popupCornerRadius = 18.0f;
    float tooltipCornerRadius = 8.0f;
    float itemCornerRadius = 10.0f;
    float spacingXS = 4.0f;
    float spacingS = 8.0f;
    float spacingM = 12.0f;
    float spacingL = 16.0f;
    float spacingXL = 24.0f;
    float focusRingWidth = 2.0f;
    float minimumTarget = 32.0f;  // minimum interactive target size

    // Typography
    std::wstring fontFamily = L"Segoe UI Variable Text";
    std::wstring displayFontFamily = L"Segoe UI Variable Display";
    float fontSizeBar = 13.0f;
    float fontSizeSmall = 12.0f;
    float fontSizeBody = 14.0f;
    float fontSizeTitle = 16.0f;
    float fontSizeSearch = 20.0f;

    // Motion (seconds, before MotionPolicy scaling)
    double durationFast = 0.12;
    double durationNormal = 0.22;
    double durationSlow = 0.36;
    double tooltipDelay = 0.45;
};

// High contrast (system colors, no translucency) takes precedence over dark/light.
[[nodiscard]] ThemeMode DetectThemeMode() noexcept;
[[nodiscard]] DesignTokens MakeDesignTokens(ThemeMode mode);

}  // namespace aether::graphics
