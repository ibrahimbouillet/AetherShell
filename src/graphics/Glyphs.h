// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Original vector marks shared by every Aether surface: the brand mark, the Start mark, Task View,
// Search, the navigation glyphs and small UI marks. All of them are geometry drawn in code on a
// 16-DIP grid with round caps and joins; no icon font, image asset or third-party artwork is used.
// Status marks share the same proportional grid and rounded stroke on the Top Bar and flyouts.
//
// Geometries are built once per Direct2D factory and glyph on the calling (UI) thread and cached.
#pragma once

#include "core/Result.h"
#include "graphics/Theme.h"
#include "graphics/WinRT.h"

#include <optional>

namespace aether::graphics {

enum class Glyph {
    Search,
    TaskView,
    // Navigation (Settings pages)
    Dock,
    TopBar,
    Desktop,
    Weather,
    Updates,
    General,
    About,
    // Small UI marks
    ChevronDown,
    ChevronUp,
    ChevronLeft,
    ChevronRight,
    Check,
    Close,
    ArrowUp,
    ArrowDown,
    ArrowLeft,
    ArrowRight,
    // Menus, forms and setup
    Settings,      // gear
    Appearance,    // half-filled circle (color mode)
    Power,         // quit
    Taskbar,       // a screen with its taskbar (Windows taskbar / notification area)
    Plus,
    Minus,
    Folder,
    Download,
    Shield,        // verified update
    Warning,       // triangle with an exclamation mark
    Location,
    OpenExternal,  // a box with an outgoing arrow (opens the browser)
    Reset,         // counter-clockwise arrow (restore defaults)
};
inline constexpr Glyph kAllGlyphs[] = {
    Glyph::Search,      Glyph::TaskView,     Glyph::Dock,        Glyph::TopBar,       Glyph::Desktop,
    Glyph::Weather,     Glyph::Updates,      Glyph::General,     Glyph::About,        Glyph::ChevronDown,
    Glyph::ChevronUp,   Glyph::ChevronLeft,  Glyph::ChevronRight, Glyph::Check,       Glyph::Close,
    Glyph::ArrowUp,     Glyph::ArrowDown,    Glyph::ArrowLeft,   Glyph::ArrowRight,   Glyph::Settings,
    Glyph::Appearance,  Glyph::Power,        Glyph::Taskbar,     Glyph::Plus,         Glyph::Minus,
    Glyph::Folder,      Glyph::Download,     Glyph::Shield,      Glyph::Warning,      Glyph::Location,
    Glyph::OpenExternal, Glyph::Reset,
};

enum class StatusBadge { Info, Success, Warning, Error };
enum class NetworkGlyphKind { Unknown, None, WiFi, Ethernet, Cellular, Other };
enum class WeatherSymbol { Unknown, Clear, PartlyCloudy, Cloudy, Fog, Drizzle, Rain, Snow, Thunder };
enum class StatusGlyph {
    Speaker, SpeakerLow, SpeakerMedium, SpeakerHigh, SpeakerMuted, SpeakerUnknown,
    Microphone, MicrophoneMuted, MicrophoneUnknown, Battery, Plug,
    Ethernet, NetworkNone, NetworkUnknown, NetworkOther,
    Sun, Moon, PartlyCloudyDay, PartlyCloudyNight, Cloud, Fog, Drizzle, Rain, Snow, Thunder, Unknown,
};
inline constexpr StatusGlyph kAllStatusGlyphs[] = {
    StatusGlyph::Speaker, StatusGlyph::SpeakerLow, StatusGlyph::SpeakerMedium, StatusGlyph::SpeakerHigh,
    StatusGlyph::SpeakerMuted, StatusGlyph::SpeakerUnknown, StatusGlyph::Microphone,
    StatusGlyph::MicrophoneMuted, StatusGlyph::MicrophoneUnknown, StatusGlyph::Battery, StatusGlyph::Plug,
    StatusGlyph::Ethernet, StatusGlyph::NetworkNone, StatusGlyph::NetworkUnknown, StatusGlyph::NetworkOther,
    StatusGlyph::Sun, StatusGlyph::Moon, StatusGlyph::PartlyCloudyDay, StatusGlyph::PartlyCloudyNight,
    StatusGlyph::Cloud, StatusGlyph::Fog, StatusGlyph::Drizzle, StatusGlyph::Rain, StatusGlyph::Snow,
    StatusGlyph::Thunder, StatusGlyph::Unknown,
};

// A glyph on the 16-DIP grid (origin at the top-left of the grid): `stroke` figures are drawn with
// the stroke width and RoundStrokeStyle(), `fill` figures are filled. Either may be null.
struct GlyphGeometry {
    winrt::com_ptr<ID2D1PathGeometry> stroke;
    winrt::com_ptr<ID2D1PathGeometry> fill;
};

struct WiFiGlyphGeometry {
    GlyphGeometry active;
    winrt::com_ptr<ID2D1PathGeometry> inactiveStroke;
};

[[nodiscard]] Result<GlyphGeometry> BuildGlyphGeometry(ID2D1Factory* factory, Glyph glyph);
[[nodiscard]] Result<GlyphGeometry> BuildStatusGlyphGeometry(ID2D1Factory* factory, StatusGlyph glyph);
// 0..3 active wave arcs; no value or an invalid count means unknown, not full signal. The caller
// maps its real network snapshot to that display count. Geometry includes a distinct unknown mark.
[[nodiscard]] Result<WiFiGlyphGeometry> BuildWiFiGlyphGeometry(ID2D1Factory* factory, std::optional<int> activeWaves);
// The two ribbons of the Aether brand mark (filled), centred in the 16-DIP grid.
[[nodiscard]] Result<winrt::com_ptr<ID2D1PathGeometry>> BuildBrandMarkGeometry(ID2D1Factory* factory);
// Four rounded squares in a 2x2 arrangement (filled), the Dock's Start mark.
[[nodiscard]] Result<winrt::com_ptr<ID2D1PathGeometry>> BuildStartMarkGeometry(ID2D1Factory* factory);

// Drops the calling thread's cached glyph geometries and stroke styles (see ReleaseGlassResources).
void ReleaseGlyphResources() noexcept;

// Round caps and joins; cached per factory on the calling thread. Null when it cannot be created.
[[nodiscard]] ID2D1StrokeStyle* RoundStrokeStyle(ID2D1Factory* factory);

// Paints `glyph` centred in `box`, scaled from the 16-DIP grid to the box's shorter side.
// `strokeWidth` is in grid units (tokens.glyph.stroke), so it scales with the box.
void PaintGlyph(ID2D1RenderTarget* target, Glyph glyph, const D2D1_RECT_F& box, ID2D1Brush* brush,
                float strokeWidth = 1.5f);
// Cached Wi-Fi waves on the same 16-DIP grid. Inactive arcs use 28% of the brush opacity; active
// arcs/dot or the explicit unknown mark use its original opacity. Restores opacity and transform.
void PaintWiFiGlyph(ID2D1RenderTarget* target, const D2D1_RECT_F& box, ID2D1Brush* brush,
                    std::optional<int> activeWaves, float strokeWidth = 1.5f);
void PaintStatusGlyph(ID2D1RenderTarget* target, StatusGlyph glyph, const D2D1_RECT_F& box,
                      ID2D1Brush* brush, float strokeWidth = 1.5f);
// Signal bars are Windows' actual 0..5 reading. Missing/invalid values remain visibly unknown.
void PaintNetworkGlyph(ID2D1RenderTarget* target, const D2D1_RECT_F& box, ID2D1Brush* brush,
                       NetworkGlyphKind kind, std::optional<int> signalBars, bool limited = false,
                       float strokeWidth = 1.5f);
void PaintVolumeGlyph(ID2D1RenderTarget* target, const D2D1_RECT_F& box, ID2D1Brush* brush,
                      std::optional<int> percent, std::optional<bool> muted, float strokeWidth = 1.5f);
void PaintMicrophoneGlyph(ID2D1RenderTarget* target, const D2D1_RECT_F& box, ID2D1Brush* brush,
                          std::optional<bool> muted, float strokeWidth = 1.5f);
// Critical is a known <=5% reading or a real low/critical flag; success is a known 100% only.
// High contrast always uses system text. No battery reading is inferred from charging/external power.
[[nodiscard]] Color BatteryStatusColor(const DesignTokens& tokens, std::optional<int> percent,
                                      std::optional<bool> lowOrCritical = {}) noexcept;
void PaintBatteryGlyph(ID2D1RenderTarget* target, const D2D1_RECT_F& box, ID2D1SolidColorBrush* brush,
                       const DesignTokens& tokens, std::optional<int> percent,
                       std::optional<bool> lowOrCritical = {}, bool charging = false,
                       bool externalPower = false, float strokeWidth = 1.5f);
void PaintWeatherGlyph(ID2D1RenderTarget* target, const D2D1_RECT_F& box, ID2D1Brush* brush,
                       WeatherSymbol category, std::optional<bool> isDay, bool cached = false,
                       float strokeWidth = 1.5f);

// The brand mark with the brand gradient (brandPrimary to brandSecondary); in high contrast a
// solid textPrimary. Same geometry as the Top Bar logo.
void PaintBrandMark(ID2D1RenderTarget* target, const D2D1_RECT_F& box, const DesignTokens& tokens);

// The Start mark. Color: StartMarkColor(tokens) with a subtle top highlight.
void PaintStartMark(ID2D1RenderTarget* target, const D2D1_RECT_F& box, const DesignTokens& tokens);
// The Windows accent when the tokens carry it (AccentSource::Windows and the palette was read),
// otherwise Windows blue (#0078D4 in light mode, #4CC2FF in dark mode); high contrast: textPrimary.
[[nodiscard]] Color StartMarkColor(const DesignTokens& tokens) noexcept;

// A filled status circle with a contrasting mark (InfoBar severities): info uses the accent,
// success/warning/error the status roles; high contrast draws textPrimary with a halo-colored mark.
void PaintStatusBadge(ID2D1RenderTarget* target, StatusBadge badge, const D2D1_RECT_F& box, const DesignTokens& tokens);

}  // namespace aether::graphics
