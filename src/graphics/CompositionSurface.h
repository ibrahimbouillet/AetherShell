// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Per-window composition tree:
//
//   DesktopWindowTarget (HWND with WS_EX_NOREDIRECTIONBITMAP)
//   └─ root ContainerVisual
//      ├─ backdrop SpriteVisual  HostBackdropBrush (blurred desktop), clipped to the panel's
//      │                         rounded rectangle — Windows 11 only
//      └─ content SpriteVisual   CompositionDrawingSurface rendered with Direct2D
//                                (tint, highlights, border, shadow, icons, text), premultiplied alpha
//
// Visual coordinates are physical pixels; Draw() configures Direct2D so callers draw in DIPs.
#pragma once

#include "core/Result.h"
#include "graphics/GraphicsDevice.h"

#include <functional>
#include <chrono>

namespace aether::graphics {

enum class BackdropKind {
    None,      // per-pixel transparency only (tint painted by Direct2D)
    HostBlur,  // blurred desktop behind the panel (Windows 11)
};

class CompositionSurface {
public:
    CompositionSurface() = default;
    ~CompositionSurface();
    CompositionSurface(const CompositionSurface&) = delete;
    CompositionSurface& operator=(const CompositionSurface&) = delete;

    // `hwnd` must have been created with WS_EX_NOREDIRECTIONBITMAP.
    [[nodiscard]] Result<void> Attach(GraphicsDevice& device, HWND hwnd);
    void Detach() noexcept;
    [[nodiscard]] bool IsAttached() const noexcept { return static_cast<bool>(target_); }

    // Resizes the drawing surface to the window's client size in pixels.
    [[nodiscard]] Result<void> Resize(int widthPx, int heightPx);

    // Finite compositor-owned entrance; no UI frame loop. Zero duration resets immediately.
    [[nodiscard]] Result<void> AnimateEntrance(std::chrono::milliseconds duration, float offsetPx);
    [[nodiscard]] Result<void> AnimateExit(std::chrono::milliseconds duration, float offsetPx);

    // Places the backdrop under the panel shape (pixels, relative to the window). Requesting HostBlur on
    // a system without support falls back to None. Returns the effective kind.
    BackdropKind SetBackdrop(BackdropKind kind, const D2D1_RECT_F& panelPx, float cornerRadiusPx);
    [[nodiscard]] BackdropKind EffectiveBackdrop() const noexcept { return backdropKind_; }

    // Redraws the whole surface. The context is cleared to transparent, its DPI set to `dpi` (so the
    // callback draws in DIPs) and the surface offset applied. Device loss is reported as
    // ErrorCode::Unavailable with the HRESULT in platformCode; the caller then asks the GraphicsDevice
    // to recover and invalidates.
    [[nodiscard]] Result<void> Draw(UINT dpi, const std::function<void(ID2D1DeviceContext*)>& draw);

private:
    GraphicsDevice* device_ = nullptr;
    HWND hwnd_ = nullptr;
    wuc::Desktop::DesktopWindowTarget target_{nullptr};
    wuc::ContainerVisual root_{nullptr};
    wuc::SpriteVisual backdrop_{nullptr};
    wuc::SpriteVisual content_{nullptr};
    wuc::CompositionDrawingSurface surface_{nullptr};
    wuc::CompositionSurfaceBrush surfaceBrush_{nullptr};
    wuc::CompositionBackdropBrush hostBackdropBrush_{nullptr};
    wuc::CompositionRoundedRectangleGeometry clipGeometry_{nullptr};
    BackdropKind backdropKind_ = BackdropKind::None;
    int widthPx_ = 1;
    int heightPx_ = 1;
};

}  // namespace aether::graphics
