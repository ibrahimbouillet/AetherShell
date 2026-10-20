// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Paints the Aether glass material with Direct2D: a two-layer exterior-only shadow (key and
// ambient), the neutral tint and a subtle deterministic grain. Dock and Bar use centered shadows
// and a uniform edge, without directional sheen or rim. Other elevations retain their flyout sheen
// and gradient hairline border. When the compositor provides a blurred backdrop underneath, the
// tint stays translucent; otherwise it is made more opaque so text remains readable over any
// wallpaper. High contrast paints an opaque system-color panel without shadow, sheen or grain.
//
// Shadows are baked once per (device, size class, radius, elevation, DPI, color) into a small
// bitmap and drawn as nine slices, so a panel that changes width every frame (Dock magnification)
// costs nine bitmap draws instead of an effect graph per frame. The cache lives on the UI thread and
// is dropped when the Direct2D device changes.
#pragma once

#include "graphics/Theme.h"

#include <d2d1_1.h>
#include <dwrite.h>

#include <array>
#include <cstdint>
#include <vector>

namespace aether::graphics {

enum class Elevation { None, Tooltip, Bar, Dock, Popup, Lift };

struct GlassStyle {
    float cornerRadius = 16.0f;  // DIPs
    float opacity = 0.8f;        // user preference 0.3..1.0 (scales the tint)
    bool backdropBlur = false;   // a blurred backdrop is composited under this panel
    bool shadow = true;          // false: no shadow whatever `elevation` says
    Elevation elevation = Elevation::Dock;
    bool noise = true;           // grain texture (never in high contrast or without transparency)
    bool highlight = true;       // flyout sheen/rim; Dock and Bar always use the neutral material
};

// Effective tint alpha for a style (exposed for tests and for components that paint sub-panels).
[[nodiscard]] float GlassTintAlpha(const DesignTokens& tokens, const GlassStyle& style) noexcept;

void PaintGlassPanel(ID2D1DeviceContext* context, const D2D1_RECT_F& rect, const DesignTokens& tokens,
                     const GlassStyle& style);

// Two-layer exterior-only shadow of the rounded rectangle `rect` (nothing is drawn inside it).
// `opacity` scales both layers. Nothing in high contrast or for Elevation::None.
void PaintElevation(ID2D1DeviceContext* context, const D2D1_RECT_F& rect, float cornerRadius, const DesignTokens& tokens,
                    Elevation elevation, float opacity = 1.0f);

// Two-tone keyboard focus indicator drawn outside `rect`: an inner ring in focus.inner and an outer
// ring in focus.outer (high contrast: the highlight color, 3 DIPs).
void PaintFocusRing(ID2D1DeviceContext* context, const D2D1_RECT_F& rect, float cornerRadius,
                    const DesignTokens& tokens);
void PaintFocusRing(ID2D1RenderTarget* target, const D2D1_RECT_F& rect, float cornerRadius, const DesignTokens& tokens);

// Dock running-application indicator. The Dock owns the geometry (DockLayout); `pill` is drawn
// fully rounded: Running in runningIndicator, Active (foreground) in the accent, Attention in the
// caution color. `opacity` 0..1 lets the Dock fade indicators in and out.
enum class IndicatorState { Running, Active, Attention };
void PaintRunningIndicator(ID2D1DeviceContext* context, const D2D1_RECT_F& pill, const DesignTokens& tokens,
                           IndicatorState state, float opacity = 1.0f);

// Soft shadow under a lifted (dragged) icon; `lift` 0..1 fades it in and out with the lift spring.
void PaintLiftShadow(ID2D1DeviceContext* context, const D2D1_RECT_F& iconRect, float cornerRadius,
                     const DesignTokens& tokens, float lift);

// Hover and press feedback tile behind an item; `hover` and `pressed` are animated amounts 0..1.
// High contrast adds a 1-DIP highlight outline while hovered.
void PaintItemHighlight(ID2D1DeviceContext* context, const D2D1_RECT_F& rect, float cornerRadius,
                        const DesignTokens& tokens, float hover, float pressed);

// A list row's selection or hover background (Search rows, menus, navigation). Over glass it is a
// translucent overlay; high contrast uses surface.selectedFill for the selection.
void PaintSelectionPill(ID2D1RenderTarget* target, const D2D1_RECT_F& rect, float cornerRadius, const DesignTokens& tokens,
                        bool selected, bool hover);
// The small fully rounded accent bar that marks the selected row (accentRoles.fill; high contrast:
// surface.selectedText, since it sits on the selected fill).
void PaintAccentIndicator(ID2D1RenderTarget* target, const D2D1_RECT_F& rect, const DesignTokens& tokens);

// Draws an icon bitmap into `rect`, with high-quality cubic filtering when it is downscaled by more
// than 1.2x and linear filtering otherwise.
void DrawIconBitmap(ID2D1DeviceContext* context, ID2D1Bitmap* bitmap, const D2D1_RECT_F& rect, float opacity = 1.0f);
[[nodiscard]] D2D1_INTERPOLATION_MODE IconInterpolation(float sourcePx, float targetPx) noexcept;

// Neutral placeholder used when an application icon cannot be extracted: a rounded tile with the
// first letter of the name. Never pretends to be the application's real icon.
void PaintFallbackIcon(ID2D1DeviceContext* context, IDWriteFactory* dwrite, const D2D1_RECT_F& rect,
                       wchar_t initial, const DesignTokens& tokens);

// Drops the calling thread's cached shadow and grain bitmaps. GraphicsDevice calls it on Shutdown and
// before recreating a lost device.
void ReleaseGlassResources() noexcept;

// --- Pure helpers (exposed for tests) ---

[[nodiscard]] const ElevationLayers& ElevationFor(const DesignTokens& tokens, Elevation elevation) noexcept;
// DIPs a two-layer shadow reaches beyond its shape (3 standard deviations plus the offset).
[[nodiscard]] float ShadowExtent(const ElevationLayers& layers) noexcept;
// Silhouette length baked for a shape side of `length` DIPs: the side itself when it is too short to
// stretch (so the bitmap matches it exactly), else the shortest stretchable length.
[[nodiscard]] float ShadowBakeLength(float length, float extent, float cornerRadius) noexcept;

struct NineSlice {
    std::array<D2D1_RECT_F, 9> source{};  // row-major: top-left, top, top-right, left, center, right, ...
    std::array<D2D1_RECT_F, 9> target{};
};
// Splits a `sourceSize` image with corners of `cornerX` x `cornerY` into nine slices mapped onto
// `target`: corners keep their size, edges and the center stretch. When `target` is smaller than
// two corners along an axis, the corners shrink to half of the target along that axis.
[[nodiscard]] NineSlice NineSliceRects(const D2D1_SIZE_F& sourceSize, float cornerX, float cornerY,
                                       const D2D1_RECT_F& target) noexcept;

// Deterministic grain tile: `size` x `size` premultiplied top-down BGRA pixels of black and white
// specks whose alpha averages `meanAlpha` (xorshift32 from `seed`; 0 seeds use a fixed default).
[[nodiscard]] std::vector<std::uint8_t> MakeNoiseTile(std::uint32_t size, std::uint32_t seed, float meanAlpha);

}  // namespace aether::graphics
