// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Design tokens: the single source of truth for colors, glass parameters,
// geometry, typography and motion used by every Aether Shell surface.
//
// Design language ("Aether Fluent"): Windows 11 Fluent roles and metrics on a 4-DIP grid (neutral
// materials, the Windows accent color, two-tone focus, Segoe UI Variable type ramp) with
// macOS-like Dock polish (springs, lift, launch bounce, pill indicators). Every field that existed
// before the design language keeps its name and meaning; the role groups below are additive.
#pragma once

#include "graphics/Animation.h"

#include <d2d1.h>

#include <optional>
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
    // GDI COLORREF of the color channels (alpha ignored), for owner-drawn native controls.
    [[nodiscard]] unsigned long ToColorRef() const noexcept;
};

enum class ThemeMode { Dark, Light, HighContrast };

// Appearance preferences. They mirror settings::ThemePreference and settings::AccentSource; graphics
// does not depend on the settings module, so the app maps one onto the other.
enum class ThemePreference { System, Light, Dark };  // System follows the Windows mode
enum class AccentSource { Windows, Aether };          // the Windows accent color or the Aether brand palette

struct AppearanceOptions {
    ThemePreference theme = ThemePreference::System;
    AccentSource accent = AccentSource::Windows;
};

// The Windows accent palette (UISettings) and text scale. `available` is false when the palette
// could not be read; the brand palette is then used. Tests build synthetic palettes.
struct SystemPalette {
    bool available = false;
    std::optional<Color> accent;
    std::optional<Color> accentLight1;
    std::optional<Color> accentLight2;
    std::optional<Color> accentLight3;
    std::optional<Color> accentDark1;
    std::optional<Color> accentDark2;
    std::optional<Color> accentDark3;
    float textScale = 1.0f;  // Accessibility > Text size (1..2.25)
};

struct GlassTokens {
    Color tint;                    // panel tint painted over the (blurred) backdrop
    float tintAlphaWithBlur = 0.5f;
    float tintAlphaWithoutBlur = 0.66f;  // stronger tint without blur, while retaining the backdrop
    Color borderTop;               // 1-DIP inner border, gradient from top...
    Color borderBottom;            // ...to bottom
    Color highlight;               // soft specular sheen at the top of the panel
    float highlightFraction = 0.30f;  // the sheen fades out over this fraction of the panel height
    Color rim;                     // 1-DIP inner rim just inside the border, strongest at the top edge
    // Legacy single shadow (the Dock elevation's key layer); painting uses DesignTokens::elevation.
    Color shadow;
    float shadowBlur = 14.0f;      // DIPs
    float shadowOffsetY = 6.0f;    // DIPs
    float backdropOpacity = 1.0f;  // opacity of the blurred desktop (HostBackdropBrush) under the tint
    float noiseAlpha = 0.02f;      // mean alpha of the grain texture; 0 disables it
};

// Opaque Fluent layer and control colors, usable by GDI-drawn native controls as well.
struct SurfaceColors {
    Color background;          // window background
    Color layer;               // content layer on the background
    Color card;                // cards and flyouts drawn opaque
    Color cardStroke;
    Color subtleFill;          // hovered list item on a card
    Color subtleFillPressed;
    Color controlFill;         // buttons, fields
    Color controlFillHover;
    Color controlFillPressed;
    Color controlFillDisabled;
    Color controlStrokeTop;
    Color controlStrokeBottom;
    Color strongStroke;        // control borders that must be visible (3:1)
    Color divider;
    Color selectedFill;        // selected list item (high contrast: COLOR_HIGHLIGHT)
    Color selectedText;        // text on selectedFill (high contrast: COLOR_HIGHLIGHTTEXT)
    Color popupBackground;     // owner-drawn popup menus
};

// Accent roles. With AccentSource::Windows they come from the Windows accent palette, picked per mode
// and stepped until they keep 3:1 against the card; otherwise from the brand palette.
struct AccentColors {
    Color fill;          // toggles, sliders, selection indicator, default button
    Color fillHover;
    Color fillPressed;
    Color fillDisabled;
    Color text;          // accent-colored text and links on the card
    Color textOnAccent;  // text and glyphs drawn on `fill` (black or white, whichever contrasts more)
};

// Two-tone keyboard focus: an outer ring in the text color and a thin inner ring in the opposite
// tone, so focus stays visible on any wallpaper and any accent.
struct FocusTokens {
    Color outer;
    Color inner;
    float outerWidth = 2.0f;
    float innerWidth = 1.0f;
    float gap = 1.0f;  // space between the target and the inner ring
};

struct RadiusTokens {
    float control = 4.0f;  // buttons, fields
    float card = 6.0f;
    float overlay = 8.0f;  // list items in flyouts, Search rows
    float popup = 12.0f;   // Search, menus
    float bar = 10.0f;     // Top Bar
    float tooltip = 8.0f;
    float item = 10.0f;    // Dock item highlight tiles
    float dockMin = 14.0f; // the Dock radius is about 0.3 x the panel height, clamped to these
    float dockMax = 22.0f;
};

// One soft shadow layer: Gaussian standard deviation and vertical offset in DIPs, color alpha.
struct ShadowLayer {
    float blur = 0.0f;
    float offsetY = 0.0f;
    float alpha = 0.0f;
};
// A two-layer elevation: a tighter key shadow below the surface and a wide ambient shadow around it.
struct ElevationLayers {
    ShadowLayer key;
    ShadowLayer ambient;
};
struct ElevationTokens {
    Color color;  // shadow color (alpha taken from the layers)
    ElevationLayers tooltip;
    ElevationLayers bar;
    ElevationLayers dock;
    ElevationLayers popup;
    ElevationLayers lift;  // a Dock icon lifted for dragging
};

// Fluent type ramp (DIPs). Families resolve to "Segoe UI" when Segoe UI Variable is missing.
struct TypeRamp {
    float caption = 12.0f;
    float body = 14.0f;
    float bodyStrong = 14.0f;  // semibold
    float subtitle = 20.0f;    // semibold
    float title = 28.0f;       // semibold
    std::wstring smallFamily = L"Segoe UI Variable Small";  // optical size for 12 DIPs or less
};

// Timing curves shared by Direct2D tweens and compositor animations.
struct CurveTokens {
    CubicBezier standard{0.2f, 0.0f, 0.0f, 1.0f};    // moves within the screen
    CubicBezier decelerate{0.0f, 0.0f, 0.0f, 1.0f};  // entering
    CubicBezier accelerate{0.3f, 0.0f, 1.0f, 1.0f};  // leaving
};

// Original vector glyphs (graphics/Glyphs.h) are drawn on a 16-DIP grid with round caps and joins.
struct GlyphTokens {
    float grid = 16.0f;
    float stroke = 1.5f;     // stroke width at the grid size
    Color halo;              // opaque bar-colored knockout behind overlapping marks
    float haloWidth = 1.5f;  // extra width of the knockout around a mark
};

struct DesignTokens {
    ThemeMode mode = ThemeMode::Dark;
    bool transparencyEnabled = true;  // honors Windows accessibility/personalization preference
    GlassTokens glass;

    Color textPrimary;
    Color textSecondary;
    Color textDisabled;
    Color accent;           // accent fill (the Windows accent with AccentSource::Windows, else brand primary)
    Color accentSecondary;  // gradient partner of `accent`
    Color focusRing;        // the outer focus color (focus.outer)
    Color runningIndicator;
    Color hoverOverlay;
    Color pressedOverlay;
    Color warning;          // legacy name of `caution`; MakeDesignTokens keeps both equal
    // Status roles. High contrast maps all three to the system text color, so a state never depends
    // on color alone there.
    Color success;
    Color caution;
    Color critical;
    Color info;
    Color fallbackIconBackground;

    // Brand palette (logo gradient), kept whatever the accent source.
    Color brandPrimary;
    Color brandSecondary;
    AccentSource accentSource = AccentSource::Aether;
    bool systemAccent = false;  // the accent roles come from the Windows palette

    SurfaceColors surface;
    AccentColors accentRoles;
    FocusTokens focus;
    RadiusTokens radii;
    ElevationTokens elevation;
    TypeRamp type;
    CurveTokens curves;
    GlyphTokens glyph;
    float textScale = 1.0f;  // SystemPalette::textScale (Settings and Search honor it)

    // Geometry (DIPs)
    float dockCornerRadius = 22.0f;  // radii.dockMax; see DockCornerRadius()
    float barCornerRadius = 10.0f;   // radii.bar
    float popupCornerRadius = 12.0f; // radii.popup
    float tooltipCornerRadius = 8.0f;
    float itemCornerRadius = 10.0f;
    float spacingXS = 4.0f;
    float spacingS = 8.0f;
    float spacingM = 12.0f;
    float spacingL = 16.0f;
    float spacingXL = 24.0f;
    float focusRingWidth = 2.0f;
    float minimumTarget = 32.0f;  // minimum interactive target size
    float tooltipPaddingX = 8.0f;
    float tooltipPaddingY = 6.0f;

    // Typography
    std::wstring fontFamily = L"Segoe UI Variable Text";
    std::wstring displayFontFamily = L"Segoe UI Variable Display";
    float fontSizeBar = 13.0f;
    float fontSizeSmall = 12.0f;
    float fontSizeBody = 14.0f;
    float fontSizeTitle = 16.0f;
    float fontSizeSearch = 20.0f;

    // Motion (seconds, before MotionPolicy scaling). Springs and gesture constants: graphics::Motion().
    double durationMicro = 0.083;  // hover and press feedback
    double durationFast = 0.12;
    double durationNormal = 0.22;
    double durationSlow = 0.36;
    double tooltipDelay = 0.45;
    double tooltipWarmWindow = 0.6;  // a tooltip hidden less than this ago shows the next one without delay
};

// App color mode (AppsUseLightTheme): Settings and other app windows. High contrast takes
// precedence over dark/light.
[[nodiscard]] ThemeMode DetectThemeMode() noexcept;
// Windows color mode (SystemUsesLightTheme, as the taskbar uses it), falling back to the app mode
// when the value is absent. High contrast takes precedence.
[[nodiscard]] ThemeMode DetectShellThemeMode() noexcept;
// The same with a user preference applied: Light and Dark override the system, high contrast wins.
[[nodiscard]] ThemeMode DetectShellThemeMode(ThemePreference preference) noexcept;
[[nodiscard]] ThemeMode DetectAppThemeMode(ThemePreference preference) noexcept;
// Pure resolution rule behind the Detect functions.
[[nodiscard]] ThemeMode ResolveThemeMode(ThemePreference preference, bool highContrast, bool systemDark) noexcept;

// Deterministic tokens: the brand accent and no system palette reads (capture tests and fixtures).
[[nodiscard]] DesignTokens MakeDesignTokens(ThemeMode mode);
// Tokens with the Windows accent applied per `options.accent` (palette.available false falls back
// to the brand accent) and the palette's text scale.
[[nodiscard]] DesignTokens MakeDesignTokens(ThemeMode mode, const SystemPalette& palette, const AppearanceOptions& options);

// WCAG 2 relative luminance of the color channels (alpha ignored).
[[nodiscard]] float RelativeLuminance(const Color& color) noexcept;
// WCAG 2 contrast ratio 1..21 of two opaque colors.
[[nodiscard]] float ContrastRatio(const Color& a, const Color& b) noexcept;
// `top` composited over an opaque `bottom` (source-over); the result is opaque.
[[nodiscard]] Color CompositeOver(const Color& top, const Color& bottom) noexcept;
// Dock panel corner radius for a panel height: about 0.3 x the height, clamped to radii.dockMin..dockMax.
[[nodiscard]] float DockCornerRadius(const DesignTokens& tokens, float panelHeight) noexcept;

}  // namespace aether::graphics
