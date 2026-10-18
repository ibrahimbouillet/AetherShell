// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Paints the Aether glass material with Direct2D: soft drop shadow, tint,
// top sheen and a gradient hairline border. When the compositor provides a
// blurred backdrop underneath, the tint stays translucent; otherwise it is
// made more opaque so text remains readable over any wallpaper.
#pragma once

#include "graphics/Theme.h"

#include <d2d1_1.h>
#include <dwrite.h>

namespace aether::graphics {

struct GlassStyle {
    float cornerRadius = 16.0f;  // DIPs
    float opacity = 0.8f;        // user preference 0.3..1.0 (scales the tint)
    bool backdropBlur = false;   // a blurred backdrop is composited under this panel
    bool shadow = true;
};

// Effective tint alpha for a style (exposed for tests and for components that paint sub-panels).
[[nodiscard]] float GlassTintAlpha(const DesignTokens& tokens, const GlassStyle& style) noexcept;

void PaintGlassPanel(ID2D1DeviceContext* context, const D2D1_RECT_F& rect, const DesignTokens& tokens,
                     const GlassStyle& style);

// Keyboard focus indicator drawn outside `rect`.
void PaintFocusRing(ID2D1DeviceContext* context, const D2D1_RECT_F& rect, float cornerRadius,
                    const DesignTokens& tokens);

// Neutral placeholder used when an application icon cannot be extracted: a rounded tile with the
// first letter of the name. Never pretends to be the application's real icon.
void PaintFallbackIcon(ID2D1DeviceContext* context, IDWriteFactory* dwrite, const D2D1_RECT_F& rect,
                       wchar_t initial, const DesignTokens& tokens);

}  // namespace aether::graphics
