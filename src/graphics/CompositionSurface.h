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
#include "graphics/Transition.h"

#include <chrono>
#include <cstdint>
#include <functional>

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

    // Finite compositor-owned entrance (decelerate curve) and exit (accelerate curve); no UI frame loop.
    // Zero duration applies the end state immediately. Wrappers over Animate().
    [[nodiscard]] Result<void> AnimateEntrance(std::chrono::milliseconds duration, float offsetPx);
    [[nodiscard]] Result<void> AnimateExit(std::chrono::milliseconds duration, float offsetPx);

    // Plays 	ransition on the root visual (opacity, offset, scale around its center; DIPs times
    // pxPerDip). Any running transition stops first and its completion is dropped. completed runs on
    // the UI thread when every keyframe animation has finished (CompositionScopedBatch.Completed, delivered
    // through the thread's DispatcherQueue), or synchronously before returning when the transition is
    // instant. On error nothing was started and completed is not called; the caller applies its end state.
    [[nodiscard]] Result<void> Animate(const Transition& transition, float pxPerDip, std::function<void()> completed = {});
    // Stops any transition and returns the root visual to opacity 1, no offset, scale 1.
    void ResetTransition() noexcept;
    [[nodiscard]] bool IsTransitioning() const noexcept { return transitionPending_; }

    // Places the backdrop under the panel shape (pixels, relative to the window). Requesting HostBlur on
    // a system without support falls back to None. Returns the effective kind. opacity is the blurred
    // desktop's opacity under the tint (DesignTokens::glass.backdropOpacity), clamped to 0..1.
    BackdropKind SetBackdrop(BackdropKind kind, const D2D1_RECT_F& panelPx, float cornerRadiusPx, float opacity = 1.0f);
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
    wuc::CompositionScopedBatch::Completed_revoker batchCompleted_;
    std::uint64_t transitionGeneration_ = 0;
    bool transitionPending_ = false;
    BackdropKind backdropKind_ = BackdropKind::None;
    int widthPx_ = 1;
    int heightPx_ = 1;
};

}  // namespace aether::graphics
