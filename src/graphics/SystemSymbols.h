// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// The symbol family of Aether's transient control surfaces (quick controls and their detail
// views): the icon fonts Windows itself installs, rendered as DirectWrite glyph runs through the
// existing Direct2D stack. No font file is bundled, downloaded or redistributed.
//
//   1. Segoe Fluent Icons (Windows 11)          - the system's current glyph family
//   2. Segoe MDL2 Assets (Windows 10 and later) - per-symbol alternates where MDL2 differs
//   3. Original vectors (Glyphs.h and a few marks drawn here) - only when neither font is installed
//
// Every symbol is resolved once per renderer: the family must be installed and the font must
// actually contain the code point (IDWriteFont::HasCharacter), otherwise the next candidate is
// tried. Glyphs are drawn on their em box, centred in the target box and snapped to device pixels,
// so hover/selection never shift a symbol's optical centre.
//
// Threading: UI thread only (font faces are cached per renderer).
#pragma once

#include "graphics/WinRT.h"

#include <array>
#include <cstddef>
#include <optional>
#include <string_view>

namespace aether::graphics {

enum class Symbol {
    WiFi,           // full signal / generic Wi-Fi
    WiFiLevel1,     // signal overlays drawn over a dim WiFi
    WiFiLevel2,
    WiFiLevel3,
    WiFiWarning,    // Wi-Fi with a cross (no connection)
    Ethernet,
    Bluetooth,
    Airplane,
    Accessibility,
    EnergySaver,
    LiveCaptions,
    NightLight,
    Hotspot,
    NearbySharing,
    Cast,
    Project,
    Settings,
    Brightness,
    Volume0,        // speaker without waves
    Volume1,
    Volume2,
    Volume3,
    VolumeMuted,
    Mixer,          // sliders with a speaker: output and volume mixer
    ChevronRight,
    ChevronLeft,
    ChevronDown,    // the Top Bar's hidden-icons button
    ChevronUp,
    ChromeMinimize, // window caption buttons (a maximized Settings window draws its own)
    ChromeMaximize,
    ChromeRestore,
    ChromeClose,
    CaretDown,      // solid page arrow
    CaretUp,
    Back,
    OpenExternal,
    Check,
    Play,
    Pause,
    Previous,
    Next,
    Magnifier,
    Narrator,
    Keyboard,
    BatteryUnknown,
    Battery0,       // Battery0..Battery10: 0..100 % in steps of ten
    Battery10 = Battery0 + 10,
    BatteryCharging0,
    BatteryCharging10 = BatteryCharging0 + 10,
    BatterySaver0,
    BatterySaver10 = BatterySaver0 + 10,
    Count
};
inline constexpr std::size_t kSymbolCount = static_cast<std::size_t>(Symbol::Count);

enum class SymbolFamily { Fluent, Mdl2, Vector, None };

// Pure mapping (unit tested): the code point a family uses for `symbol`, or nullopt when that
// family has no suitable glyph for it.
[[nodiscard]] std::optional<char32_t> SymbolCodePoint(Symbol symbol, SymbolFamily family) noexcept;
[[nodiscard]] std::wstring_view SymbolFamilyName(SymbolFamily family) noexcept;
// Battery symbol for a known level (0..100) and state; unknown levels map to BatteryUnknown.
[[nodiscard]] Symbol BatterySymbol(std::optional<int> percent, bool charging, bool saver) noexcept;
// Speaker symbol for a known level; muted wins; unknown maps to Volume0 (no waves claimed).
[[nodiscard]] Symbol VolumeSymbol(std::optional<int> percent, std::optional<bool> muted) noexcept;

class SymbolRenderer {
public:
    SymbolRenderer() = default;
    // Resolves the installed families (cheap; safe to call again after a font change).
    void Initialize(IDWriteFactory* factory);
    [[nodiscard]] bool Initialized() const noexcept { return factory_ != nullptr; }

    // The family `symbol` will actually be drawn with on this system.
    [[nodiscard]] SymbolFamily FamilyOf(Symbol symbol) const noexcept;
    // Draws `symbol` with an em size of `size` DIPs centred in `box` (size <= 0: the box's shorter
    // side). Opacity comes from the brush.
    void Draw(ID2D1DeviceContext* context, Symbol symbol, const D2D1_RECT_F& box, ID2D1Brush* brush, float size = 0.0f);

private:
    struct Face {
        winrt::com_ptr<IDWriteFontFace> face;
        float ascent = 0.0f;   // em fractions
        float descent = 0.0f;
    };
    struct Resolved {
        SymbolFamily family = SymbolFamily::Vector;
        UINT16 glyph = 0;
        float advance = 1.0f;  // em fraction
    };
    std::optional<Face> LoadFace(std::wstring_view family, winrt::com_ptr<IDWriteFont>& font) const;

    winrt::com_ptr<IDWriteFactory> factory_;
    std::array<std::optional<Face>, 2> faces_;  // Fluent, Mdl2
    std::array<Resolved, kSymbolCount> resolved_{};
};

// The original vector fallback for `symbol` (also used by tests to qualify the fallback path).
// Symbols without a vector mark draw nothing rather than a misleading substitute.
void PaintSymbolVector(ID2D1DeviceContext* context, Symbol symbol, const D2D1_RECT_F& box, ID2D1Brush* brush);

}  // namespace aether::graphics
