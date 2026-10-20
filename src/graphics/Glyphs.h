// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Original vector marks shared by every Aether surface: the brand mark, the Start mark, Task View,
// Search, the navigation glyphs and small UI marks. All of them are geometry drawn in code on a
// 16-DIP grid with round caps and joins; no icon font, image asset or third-party artwork is used.
// Top Bar status glyphs (network, volume, battery...) live with the Top Bar and use the glyph tokens
// (DesignTokens::glyph) and RoundStrokeStyle() from here.
//
// Geometries are built once per Direct2D factory and glyph on the calling (UI) thread and cached.
#pragma once

#include "core/Result.h"
#include "graphics/Theme.h"
#include "graphics/WinRT.h"

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
};
inline constexpr Glyph kAllGlyphs[] = {
    Glyph::Search,      Glyph::TaskView,     Glyph::Dock,        Glyph::TopBar,       Glyph::Desktop,
    Glyph::Weather,     Glyph::Updates,      Glyph::General,     Glyph::About,        Glyph::ChevronDown,
    Glyph::ChevronUp,   Glyph::ChevronLeft,  Glyph::ChevronRight, Glyph::Check,       Glyph::Close,
    Glyph::ArrowUp,     Glyph::ArrowDown,    Glyph::ArrowLeft,   Glyph::ArrowRight,
};

enum class StatusBadge { Info, Success, Warning, Error };

// A glyph on the 16-DIP grid (origin at the top-left of the grid): `stroke` figures are drawn with
// the stroke width and RoundStrokeStyle(), `fill` figures are filled. Either may be null.
struct GlyphGeometry {
    winrt::com_ptr<ID2D1PathGeometry> stroke;
    winrt::com_ptr<ID2D1PathGeometry> fill;
};

[[nodiscard]] Result<GlyphGeometry> BuildGlyphGeometry(ID2D1Factory* factory, Glyph glyph);
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
