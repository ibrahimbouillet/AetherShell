// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "graphics/Theme.h"

#include "platform/windows/WindowUtilities.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <initializer_list>

namespace aether::graphics {
namespace {

// Brand palette (see docs/BRANDING.md): an indigo-to-cyan "aurora" gradient.
constexpr unsigned kBrandIndigo = 0x5B6CFF;
constexpr unsigned kBrandCyan = 0x3DD9E8;

// Minimum contrast of an accent fill against the card it sits on (WCAG non-text contrast).
constexpr float kAccentMinimumContrast = 3.0f;

float Channel(float value) noexcept {
    value = std::clamp(std::isfinite(value) ? value : 0.0f, 0.0f, 1.0f);
    return value <= 0.04045f ? value / 12.92f : std::pow((value + 0.055f) / 1.055f, 2.4f);
}

// Picks the color with the higher contrast against `fill`: black or white.
Color TextOn(const Color& fill) noexcept {
    const Color white = Color::FromHex(0xFFFFFF);
    const Color black = Color::FromHex(0x000000);
    return ContrastRatio(white, fill) >= ContrastRatio(black, fill) ? white : black;
}

void ApplyDarkRoles(DesignTokens& t) {
    t.glass.tint = Color::FromHex(0x1E1E1E);
    t.glass.tintAlphaWithBlur = 0.60f;
    t.glass.tintAlphaWithoutBlur = 0.66f;
    t.glass.borderTop = Color::FromHex(0xFFFFFF, 0.16f);
    t.glass.borderBottom = Color::FromHex(0xFFFFFF, 0.05f);
    t.glass.uniformBorder = Color::FromHex(0xFFFFFF, 0.12f);
    t.glass.highlight = Color::FromHex(0xFFFFFF, 0.05f);
    t.glass.rim = Color::FromHex(0xFFFFFF, 0.06f);
    t.textPrimary = Color::FromHex(0xFFFFFF);
    t.textSecondary = Color::FromHex(0xC5C5C5);
    t.textDisabled = Color::FromHex(0x717171);
    t.runningIndicator = Color::FromHex(0xFFFFFF, 0.80f);
    t.hoverOverlay = Color::FromHex(0xFFFFFF, 0.10f);
    t.pressedOverlay = Color::FromHex(0xFFFFFF, 0.18f);
    t.fallbackIconBackground = Color::FromHex(0x424242);
    t.success = Color::FromHex(0x6CCB5F);
    t.caution = Color::FromHex(0xFCE100);
    t.critical = Color::FromHex(0xFF99A4);

    auto& s = t.surface;
    s.background = Color::FromHex(0x202020);
    s.layer = Color::FromHex(0x272727);
    s.card = Color::FromHex(0x2B2B2B);
    s.cardStroke = Color::FromHex(0x1C1C1C);
    s.subtleFill = Color::FromHex(0x363636);
    s.subtleFillPressed = Color::FromHex(0x313131);
    s.controlFill = Color::FromHex(0x353535);
    s.controlFillHover = Color::FromHex(0x393939);
    s.controlFillPressed = Color::FromHex(0x313131);
    s.controlFillDisabled = Color::FromHex(0x303030);
    s.controlStrokeTop = Color::FromHex(0x3F3F3F);
    s.controlStrokeBottom = Color::FromHex(0x3A3A3A);
    s.strongStroke = Color::FromHex(0x9E9E9E);
    s.divider = Color::FromHex(0x3D3D3D);
    s.selectedFill = Color::FromHex(0x363636);
    s.selectedText = t.textPrimary;
    s.popupBackground = Color::FromHex(0x2C2C2C);

    auto& f = t.form;
    f.windowTint = Color::FromHex(0x1C1C1C);
    f.windowTintAlpha = 0.44f;
    f.windowSolid = Color::FromHex(0x202020);
    f.windowRim = Color::FromHex(0x4D4D4F);
    f.footerTint = Color::FromHex(0x000000, 0.10f);
    f.cardFill = Color::FromHex(0xFFFFFF, 0.045f);
    f.cardFillSolid = Color::FromHex(0x2B2B2B);
    f.cardStroke = Color::FromHex(0xFFFFFF, 0.08f);
    f.rowHover = Color::FromHex(0xFFFFFF, 0.04f);
    f.rowPressed = Color::FromHex(0xFFFFFF, 0.025f);
    f.divider = Color::FromHex(0xFFFFFF, 0.07f);
    f.fieldFill = Color::FromHex(0x2C2C2C);
    f.fieldFillDisabled = Color::FromHex(0x262626);
    f.fieldStroke = Color::FromHex(0xFFFFFF, 0.12f);
    f.fieldText = t.textPrimary;
    f.buttonFill = Color::FromHex(0xFFFFFF, 0.07f);
    f.buttonFillHover = Color::FromHex(0xFFFFFF, 0.10f);
    f.buttonFillPressed = Color::FromHex(0xFFFFFF, 0.05f);
    f.buttonStroke = Color::FromHex(0xFFFFFF, 0.09f);
    f.switchOffStroke = Color::FromHex(0xC5C5C5);
    f.sliderTrack = Color::FromHex(0xFFFFFF, 0.24f);
    f.sliderThumb = Color::FromHex(0x454545);
    f.scrollThumb = Color::FromHex(0xFFFFFF, 0.30f);
    f.scrollThumbHover = Color::FromHex(0xFFFFFF, 0.50f);
    f.navSelected = Color::FromHex(0xFFFFFF, 0.07f);
    f.navHover = Color::FromHex(0xFFFFFF, 0.045f);

    t.focus.outer = Color::FromHex(0xFFFFFF);
    t.focus.inner = Color::FromHex(0x000000, 0.70f);
    t.glyph.halo = Color::FromHex(0x1E1E1E);

    t.elevation.color = Color::FromHex(0x000000);
    t.elevation.tooltip = ElevationLayers{ShadowLayer{2.0f, 0.0f, 0.26f}, ShadowLayer{6.0f, 0.0f, 0.16f}};
    t.elevation.bar = ElevationLayers{ShadowLayer{2.0f, 0.0f, 0.24f}, ShadowLayer{8.0f, 0.0f, 0.14f}};
    t.elevation.dock = ElevationLayers{ShadowLayer{5.0f, 0.0f, 0.30f}, ShadowLayer{10.0f, 0.0f, 0.20f}};
    t.elevation.popup = ElevationLayers{ShadowLayer{6.0f, 4.0f, 0.30f}, ShadowLayer{10.0f, 0.0f, 0.22f}};
    t.elevation.lift = ElevationLayers{ShadowLayer{4.0f, 5.0f, 0.34f}, ShadowLayer{8.0f, 2.0f, 0.16f}};
}

void ApplyLightRoles(DesignTokens& t) {
    t.glass.tint = Color::FromHex(0xF3F3F3);
    t.glass.tintAlphaWithBlur = 0.62f;
    t.glass.tintAlphaWithoutBlur = 0.72f;
    t.glass.borderTop = Color::FromHex(0xFFFFFF, 0.80f);
    t.glass.borderBottom = Color::FromHex(0x000000, 0.10f);
    t.glass.uniformBorder = Color::FromHex(0x000000, 0.12f);
    t.glass.highlight = Color::FromHex(0xFFFFFF, 0.20f);
    t.glass.rim = Color::FromHex(0xFFFFFF, 0.50f);
    t.textPrimary = Color::FromHex(0x1B1B1B);
    t.textSecondary = Color::FromHex(0x616161);
    t.textDisabled = Color::FromHex(0xA0A0A0);
    t.runningIndicator = Color::FromHex(0x1B1B1B, 0.70f);
    t.hoverOverlay = Color::FromHex(0x000000, 0.07f);
    t.pressedOverlay = Color::FromHex(0x000000, 0.13f);
    t.fallbackIconBackground = Color::FromHex(0xCFCFCF);
    t.success = Color::FromHex(0x0F7B0F);
    t.caution = Color::FromHex(0x9D5D00);
    t.critical = Color::FromHex(0xC42B1C);

    auto& s = t.surface;
    s.background = Color::FromHex(0xF3F3F3);
    s.layer = Color::FromHex(0xF9F9F9);
    s.card = Color::FromHex(0xFBFBFB);
    s.cardStroke = Color::FromHex(0xE5E5E5);
    s.subtleFill = Color::FromHex(0xF2F2F2);
    s.subtleFillPressed = Color::FromHex(0xF5F5F5);
    s.controlFill = Color::FromHex(0xFBFBFB);
    s.controlFillHover = Color::FromHex(0xF6F6F6);
    s.controlFillPressed = Color::FromHex(0xF5F5F5);
    s.controlFillDisabled = Color::FromHex(0xF7F7F7);
    s.controlStrokeTop = Color::FromHex(0xECECEC);
    s.controlStrokeBottom = Color::FromHex(0xD2D2D2);
    s.strongStroke = Color::FromHex(0x8B8B8B);
    s.divider = Color::FromHex(0xE5E5E5);
    s.selectedFill = Color::FromHex(0xF0F0F0);
    s.selectedText = t.textPrimary;
    s.popupBackground = Color::FromHex(0xF9F9F9);

    auto& f = t.form;
    f.windowTint = Color::FromHex(0xF3F3F3);
    f.windowTintAlpha = 0.50f;
    f.windowSolid = Color::FromHex(0xF3F3F3);
    f.windowRim = Color::FromHex(0xC6C6C8);
    f.footerTint = Color::FromHex(0xFFFFFF, 0.30f);
    f.cardFill = Color::FromHex(0xFFFFFF, 0.55f);
    f.cardFillSolid = Color::FromHex(0xFBFBFB);
    f.cardStroke = Color::FromHex(0x000000, 0.06f);
    f.rowHover = Color::FromHex(0x000000, 0.03f);
    f.rowPressed = Color::FromHex(0x000000, 0.02f);
    f.divider = Color::FromHex(0x000000, 0.07f);
    f.fieldFill = Color::FromHex(0xFDFDFD);
    f.fieldFillDisabled = Color::FromHex(0xF5F5F5);
    f.fieldStroke = Color::FromHex(0x000000, 0.12f);
    f.fieldText = t.textPrimary;
    f.buttonFill = Color::FromHex(0xFFFFFF, 0.70f);
    f.buttonFillHover = Color::FromHex(0xFFFFFF, 0.50f);
    f.buttonFillPressed = Color::FromHex(0xFFFFFF, 0.30f);
    f.buttonStroke = Color::FromHex(0x000000, 0.08f);
    f.switchOffStroke = Color::FromHex(0x616161);
    f.sliderTrack = Color::FromHex(0x000000, 0.26f);
    f.sliderThumb = Color::FromHex(0xFFFFFF);
    f.scrollThumb = Color::FromHex(0x000000, 0.30f);
    f.scrollThumbHover = Color::FromHex(0x000000, 0.45f);
    f.navSelected = Color::FromHex(0x000000, 0.055f);
    f.navHover = Color::FromHex(0x000000, 0.035f);

    t.focus.outer = Color::FromHex(0x000000, 0.90f);
    t.focus.inner = Color::FromHex(0xFFFFFF, 0.70f);
    t.glyph.halo = Color::FromHex(0xF3F3F3);

    t.elevation.color = Color::FromHex(0x000000);
    t.elevation.tooltip = ElevationLayers{ShadowLayer{2.0f, 0.0f, 0.14f}, ShadowLayer{6.0f, 0.0f, 0.08f}};
    t.elevation.bar = ElevationLayers{ShadowLayer{2.0f, 0.0f, 0.12f}, ShadowLayer{8.0f, 0.0f, 0.08f}};
    t.elevation.dock = ElevationLayers{ShadowLayer{5.0f, 0.0f, 0.16f}, ShadowLayer{10.0f, 0.0f, 0.10f}};
    t.elevation.popup = ElevationLayers{ShadowLayer{6.0f, 4.0f, 0.16f}, ShadowLayer{10.0f, 0.0f, 0.12f}};
    t.elevation.lift = ElevationLayers{ShadowLayer{4.0f, 5.0f, 0.20f}, ShadowLayer{8.0f, 2.0f, 0.10f}};
}

void ApplyHighContrastRoles(DesignTokens& t) {
    // System colors; no translucency, no decorative effects.
    const Color window = Color::FromColorRef(::GetSysColor(COLOR_WINDOW));
    const Color windowText = Color::FromColorRef(::GetSysColor(COLOR_WINDOWTEXT));
    const Color highlight = Color::FromColorRef(::GetSysColor(COLOR_HIGHLIGHT));
    const Color highlightText = Color::FromColorRef(::GetSysColor(COLOR_HIGHLIGHTTEXT));
    const Color grayText = Color::FromColorRef(::GetSysColor(COLOR_GRAYTEXT));
    const Color buttonFace = Color::FromColorRef(::GetSysColor(COLOR_BTNFACE));
    const Color buttonText = Color::FromColorRef(::GetSysColor(COLOR_BTNTEXT));
    const Color hotLight = Color::FromColorRef(::GetSysColor(COLOR_HOTLIGHT));
    const Color menu = Color::FromColorRef(::GetSysColor(COLOR_MENU));
    t.glass.tint = window;
    t.glass.tintAlphaWithBlur = 1.0f;
    t.glass.tintAlphaWithoutBlur = 1.0f;
    t.glass.borderTop = windowText;
    t.glass.borderBottom = windowText;
    t.glass.uniformBorder = windowText;
    t.glass.highlight = window.WithAlpha(0.0f);
    t.glass.rim = window.WithAlpha(0.0f);
    t.glass.noiseAlpha = 0.0f;
    t.textPrimary = windowText;
    t.textSecondary = windowText;
    t.textDisabled = grayText;
    t.runningIndicator = windowText;
    t.hoverOverlay = highlight.WithAlpha(0.35f);
    t.pressedOverlay = highlight.WithAlpha(0.6f);
    t.fallbackIconBackground = grayText;
    t.focusRingWidth = 3.0f;
    t.success = windowText;
    t.caution = windowText;
    t.critical = windowText;

    auto& s = t.surface;
    s.background = window;
    s.layer = window;
    s.card = window;
    s.cardStroke = windowText;
    s.subtleFill = window;
    s.subtleFillPressed = window;
    s.controlFill = buttonFace;
    s.controlFillHover = buttonFace;
    s.controlFillPressed = buttonFace;
    s.controlFillDisabled = buttonFace;
    s.controlStrokeTop = buttonText;
    s.controlStrokeBottom = buttonText;
    s.strongStroke = windowText;
    s.divider = windowText;
    s.selectedFill = highlight;
    s.selectedText = highlightText;
    s.popupBackground = menu;

    // Forms use native system-colored painting in high contrast; these keep any custom paint opaque.
    auto& f = t.form;
    f.windowTint = window;
    f.windowTintAlpha = 1.0f;
    f.windowSolid = window;
    f.windowRim = windowText;
    f.footerTint = window;
    f.cardFill = window;
    f.cardFillSolid = window;
    f.cardStroke = windowText;
    f.rowHover = highlight.WithAlpha(0.35f);
    f.rowPressed = highlight.WithAlpha(0.6f);
    f.divider = windowText;
    f.fieldFill = window;
    f.fieldFillDisabled = window;
    f.fieldStroke = windowText;
    f.fieldText = windowText;
    f.buttonFill = buttonFace;
    f.buttonFillHover = buttonFace;
    f.buttonFillPressed = buttonFace;
    f.buttonStroke = buttonText;
    f.switchOffStroke = windowText;
    f.sliderTrack = windowText;
    f.sliderThumb = buttonFace;
    f.scrollThumb = windowText;
    f.scrollThumbHover = highlight;
    f.navSelected = highlight;
    f.navHover = highlight.WithAlpha(0.35f);

    t.accentRoles.fill = highlight;
    t.accentRoles.fillHover = highlight;
    t.accentRoles.fillPressed = highlight;
    t.accentRoles.fillDisabled = grayText;
    t.accentRoles.text = hotLight;
    t.accentRoles.textOnAccent = highlightText;
    t.accent = highlight;
    t.accentSecondary = highlight;

    t.focus.outer = highlight;
    t.focus.inner = window;
    t.focus.outerWidth = 3.0f;
    t.glyph.halo = window;
    t.elevation.color = Color{0, 0, 0, 0};
    t.elevation.tooltip = t.elevation.bar = t.elevation.dock = t.elevation.popup = t.elevation.lift = ElevationLayers{};
}

// Accent roles from one opaque fill on the mode's card color.
void SetAccentRoles(DesignTokens& t, const Color& fill, const Color& text, const Color& secondary) {
    const Color card = t.surface.card;
    auto& roles = t.accentRoles;
    roles.fill = fill.WithAlpha(1.0f);
    roles.fillHover = CompositeOver(fill.WithAlpha(0.9f), card);
    roles.fillPressed = CompositeOver(fill.WithAlpha(0.8f), card);
    roles.fillDisabled = t.mode == ThemeMode::Dark ? CompositeOver(Color::FromHex(0xFFFFFF, 0.1581f), card)
                                                   : CompositeOver(Color::FromHex(0x000000, 0.2169f), card);
    roles.text = text.WithAlpha(1.0f);
    roles.textOnAccent = TextOn(roles.fill);
    t.accent = roles.fill;
    t.accentSecondary = secondary.WithAlpha(1.0f);
}

void ApplyBrandAccent(DesignTokens& t) {
    const bool dark = t.mode == ThemeMode::Dark;
    SetAccentRoles(t, t.brandPrimary, dark ? Color::FromHex(0x9FB4FF) : Color::FromHex(0x3446E0), t.brandSecondary);
    t.accentSource = AccentSource::Aether;
    t.systemAccent = false;
}

// The first variant (in stepping order) that keeps the minimum contrast against `card`, else the
// variant with the highest contrast. Returns nullopt when no variant is available.
std::optional<Color> PickVariant(std::initializer_list<const std::optional<Color>*> variants, const Color& card) {
    std::optional<Color> best;
    float bestContrast = 0.0f;
    for (const auto* variant : variants) {
        if (!variant->has_value()) continue;
        const Color color = (*variant)->WithAlpha(1.0f);
        const float contrast = ContrastRatio(color, card);
        if (contrast >= kAccentMinimumContrast) return color;
        if (contrast > bestContrast) {
            bestContrast = contrast;
            best = color;
        }
    }
    return best;
}

bool ApplyWindowsAccent(DesignTokens& t, const SystemPalette& palette) {
    if (!palette.available) return false;
    const Color card = t.surface.card;
    std::optional<Color> fill;
    std::optional<Color> text;
    std::optional<Color> secondary;
    if (t.mode == ThemeMode::Dark) {
        // Dark mode fills use the light variants, stepping lighter until they keep 3:1 on the card.
        fill = PickVariant({&palette.accentLight2, &palette.accentLight3, &palette.accentLight1, &palette.accent}, card);
        text = PickVariant({&palette.accentLight3, &palette.accentLight2, &palette.accentLight1}, card);
        secondary = palette.accentLight3 ? palette.accentLight3 : palette.accentLight1;
    } else {
        fill = PickVariant({&palette.accentDark1, &palette.accentDark2, &palette.accentDark3, &palette.accent}, card);
        text = PickVariant({&palette.accentDark2, &palette.accentDark3, &palette.accentDark1}, card);
        secondary = palette.accentLight1 ? palette.accentLight1 : palette.accent;
    }
    if (!fill) return false;
    SetAccentRoles(t, *fill, text.value_or(*fill), secondary.value_or(*fill));
    t.systemAccent = true;
    return true;
}

}  // namespace

Color Color::FromColorRef(unsigned long colorref, float alpha) noexcept {
    return Color{static_cast<float>(GetRValue(colorref)) / 255.0f, static_cast<float>(GetGValue(colorref)) / 255.0f,
                 static_cast<float>(GetBValue(colorref)) / 255.0f, alpha};
}

unsigned long Color::ToColorRef() const noexcept {
    const auto byte = [](float value) {
        return static_cast<BYTE>(std::lround(std::clamp(std::isfinite(value) ? value : 0.0f, 0.0f, 1.0f) * 255.0f));
    };
    return RGB(byte(r), byte(g), byte(b));
}

float RelativeLuminance(const Color& color) noexcept {
    return 0.2126f * Channel(color.r) + 0.7152f * Channel(color.g) + 0.0722f * Channel(color.b);
}

float ContrastRatio(const Color& a, const Color& b) noexcept {
    const float la = RelativeLuminance(a);
    const float lb = RelativeLuminance(b);
    return (std::max(la, lb) + 0.05f) / (std::min(la, lb) + 0.05f);
}

Color CompositeOver(const Color& top, const Color& bottom) noexcept {
    const float alpha = std::clamp(std::isfinite(top.a) ? top.a : 0.0f, 0.0f, 1.0f);
    return Color{top.r * alpha + bottom.r * (1.0f - alpha), top.g * alpha + bottom.g * (1.0f - alpha),
                 top.b * alpha + bottom.b * (1.0f - alpha), 1.0f};
}

float DockCornerRadius(const DesignTokens& tokens, float panelHeight) noexcept {
    const float low = std::min(tokens.radii.dockMin, tokens.radii.dockMax);
    if (!std::isfinite(panelHeight) || panelHeight <= 0.0f) return low;
    return std::clamp(panelHeight * 0.3f, low, std::max(tokens.radii.dockMin, tokens.radii.dockMax));
}

ThemeMode ResolveThemeMode(ThemePreference preference, bool highContrast, bool systemDark) noexcept {
    if (highContrast) return ThemeMode::HighContrast;
    switch (preference) {
        case ThemePreference::Light: return ThemeMode::Light;
        case ThemePreference::Dark: return ThemeMode::Dark;
        case ThemePreference::System: break;
    }
    return systemDark ? ThemeMode::Dark : ThemeMode::Light;
}

ThemeMode DetectThemeMode() noexcept { return DetectAppThemeMode(ThemePreference::System); }

ThemeMode DetectAppThemeMode(ThemePreference preference) noexcept {
    return ResolveThemeMode(preference, platform::HighContrastEnabled(), platform::SystemPrefersDarkApps());
}

ThemeMode DetectShellThemeMode() noexcept { return DetectShellThemeMode(ThemePreference::System); }

ThemeMode DetectShellThemeMode(ThemePreference preference) noexcept {
    return ResolveThemeMode(preference, platform::HighContrastEnabled(), platform::SystemPrefersDarkShell());
}

DesignTokens MakeDesignTokens(ThemeMode mode) {
    DesignTokens t;
    t.mode = mode;
    t.transparencyEnabled = platform::SystemTransparencyEnabled() && mode != ThemeMode::HighContrast;
    t.brandPrimary = Color::FromHex(kBrandIndigo);
    t.brandSecondary = Color::FromHex(kBrandCyan);

    switch (mode) {
        case ThemeMode::Dark: ApplyDarkRoles(t); break;
        case ThemeMode::Light: ApplyLightRoles(t); break;
        case ThemeMode::HighContrast: ApplyHighContrastRoles(t); break;
    }
    if (mode != ThemeMode::HighContrast) ApplyBrandAccent(t);
    t.info = t.mode == ThemeMode::HighContrast ? t.textPrimary : t.accentRoles.fill;
    t.warning = t.caution;
    t.focusRing = t.focus.outer;

    // Legacy single-shadow fields mirror the Dock elevation's key layer.
    t.glass.shadow = t.elevation.color.WithAlpha(t.elevation.dock.key.alpha);
    t.glass.shadowBlur = t.elevation.dock.key.blur * 2.0f;
    t.glass.shadowOffsetY = t.elevation.dock.key.offsetY;

    t.dockCornerRadius = t.radii.dockMax;
    t.barCornerRadius = t.radii.bar;
    t.popupCornerRadius = t.radii.popup;
    t.tooltipCornerRadius = t.radii.tooltip;
    t.itemCornerRadius = t.radii.item;
    t.focusRingWidth = t.focus.outerWidth;
    t.fontSizeSmall = t.type.caption;
    t.fontSizeBody = t.type.body;
    return t;
}

DesignTokens MakeDesignTokens(ThemeMode mode, const SystemPalette& palette, const AppearanceOptions& options) {
    DesignTokens t = MakeDesignTokens(mode);
    t.textScale = std::isfinite(palette.textScale) ? std::clamp(palette.textScale, 1.0f, 2.25f) : 1.0f;
    if (mode == ThemeMode::HighContrast) {
        t.accentSource = options.accent;
        return t;  // system colors already; the accent is COLOR_HIGHLIGHT
    }
    t.accentSource = options.accent;
    if (options.accent == AccentSource::Windows && ApplyWindowsAccent(t, palette)) {
        t.info = t.accentRoles.fill;
        t.accentSource = AccentSource::Windows;
    }
    return t;
}

}  // namespace aether::graphics
