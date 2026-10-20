// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Base class for every custom-drawn Aether Shell surface (Dock, Top Bar,
// Search, tooltips). It owns a borderless WS_EX_NOREDIRECTIONBITMAP popup, its
// composition tree, per-monitor DPI handling, pointer/keyboard dispatch and
// on-demand rendering through the FrameScheduler:
//
//   * Invalidate()           -> one render on the next display refresh
//   * OnAnimate() returning true keeps frames coming while animations run
//   * nothing pending        -> no frames, no CPU
//
// Subclasses draw in DIPs in OnRender(). Input regions limit hit-testing to the
// visible parts so transparent margins (shadows, magnification headroom) never
// block clicks to the windows underneath.
#pragma once

#include "core/Result.h"
#include "core/Signal.h"
#include "graphics/CompositionSurface.h"
#include "graphics/FrameScheduler.h"
#include "graphics/GraphicsDevice.h"
#include "graphics/Transition.h"

#include <windows.h>

#include <optional>
#include <span>
#include <string>
#include <vector>

namespace aether::ui {

struct SurfaceWindowOptions {
    std::wstring className;  // unique per window type, e.g. L"AetherShell.Dock"
    std::wstring title;      // window text (also the default accessible name)
    bool topmost = true;
    bool noActivate = true;  // clicking does not take focus (Dock, Top Bar)
    bool toolWindow = true;  // no taskbar button, not in Alt+Tab
    HWND owner = nullptr;
};

struct RenderContext {
    ID2D1DeviceContext* d2d = nullptr;
    UINT dpi = 96;
    float widthDip = 0.0f;
    float heightDip = 0.0f;
    double now = 0.0;  // FrameScheduler::Now()
};

enum class PointerButton { Left, Right, Middle };

class SurfaceWindow : private graphics::IFrameClient {
public:
    SurfaceWindow(graphics::GraphicsDevice& device, graphics::FrameScheduler& scheduler);
    ~SurfaceWindow() override;
    SurfaceWindow(const SurfaceWindow&) = delete;
    SurfaceWindow& operator=(const SurfaceWindow&) = delete;

    // Creates the (hidden) window at `boundsPx` in virtual-screen pixels.
    [[nodiscard]] Result<void> Create(const SurfaceWindowOptions& options, const RECT& boundsPx);
    void Destroy() noexcept;

    [[nodiscard]] HWND Handle() const noexcept { return hwnd_; }
    [[nodiscard]] UINT Dpi() const noexcept { return dpi_; }
    [[nodiscard]] bool IsShown() const noexcept { return shown_; }
    [[nodiscard]] RECT BoundsPx() const noexcept { return bounds_; }

    void SetBoundsPx(const RECT& boundsPx);
    // Rectangles in window-relative pixels that accept input; empty means the whole window.
    void SetInputRegionPx(std::span<const RECT> rects);
    // Optional drawing extent, independent of the input rectangles (shadows and transition space).
    // Empty restores the default visual clip to the input region. Visual-only margins return
    // HTTRANSPARENT; Windows guarantees forwarding that result only to windows on the same thread.
    void SetVisualRegionPx(std::span<const RECT> rects);
    // Places the blurred backdrop (when available) under the panel shape, in DIPs. `backdropOpacity` is
    // the blurred desktop's opacity under the tint (DesignTokens::glass.backdropOpacity).
    void SetBackdropPanel(const D2D1_RECT_F& panelDip, float cornerRadiusDip, bool blur, float backdropOpacity = 1.0f);
    [[nodiscard]] bool BackdropBlurActive() const noexcept;

    // Show() and Hide() act at once and cancel a running transition.
    void Show(bool activate = false);
    void Hide();

    // Shows the window and plays `transition` (usually graphics::MakeEnterTransition) on the compositor.
    // An instant transition (duration 0: reduced motion, high contrast) shows the resting state at once.
    void ShowAnimated(const graphics::Transition& transition, bool activate = false);
    // Plays `transition` (usually graphics::MakeExitTransition) and hides the window when it completes;
    // IsShown() stays true until then. An instant transition, or a compositor failure, hides at once.
    // ShowAnimated, Show or Hide during the exit cancels it.
    void HideAnimated(const graphics::Transition& transition);
    [[nodiscard]] bool IsTransitioning() const noexcept { return surface_.IsTransitioning(); }
    [[nodiscard]] bool IsHiding() const noexcept { return hiding_; }

    [[nodiscard]] Result<void> AnimateEntrance(std::chrono::milliseconds duration, float offsetDip = 8.0f);
    [[nodiscard]] Result<void> AnimateExit(std::chrono::milliseconds duration, float offsetDip = -8.0f);

    void Invalidate();               // render once on the next frame
    void RequestAnimationFrame();    // call OnAnimate() on the next frame

    [[nodiscard]] float ToPx(float dip) const noexcept { return dip * static_cast<float>(dpi_) / 96.0f; }
    [[nodiscard]] float ToDip(float px) const noexcept { return px * 96.0f / static_cast<float>(dpi_); }

protected:
    [[nodiscard]] graphics::GraphicsDevice& Device() noexcept { return device_; }
    [[nodiscard]] graphics::FrameScheduler& Scheduler() noexcept { return scheduler_; }

    virtual void OnRender(const RenderContext& context) = 0;
    // Advance animations to `now`; return true while anything is still moving.
    virtual bool OnAnimate(double /*now*/) { return false; }
    // Default: adopt the suggested rectangle from WM_DPICHANGED.
    virtual void OnDpiChanged(UINT newDpi, const RECT& suggestedPx);
    virtual void OnPointerMove(D2D1_POINT_2F /*dip*/) {}
    virtual void OnPointerLeave() {}
    virtual void OnPointerDown(PointerButton /*button*/, D2D1_POINT_2F /*dip*/) {}
    virtual void OnPointerUp(PointerButton /*button*/, D2D1_POINT_2F /*dip*/) {}
    // The window lost mouse capture to another window or to the system (WM_CAPTURECHANGED), for
    // example while dragging. Not raised for the base class's own release when the left button goes
    // up; that release is followed by OnPointerUp instead.
    virtual void OnPointerCaptureLost() {}
    virtual void OnWheel(float /*notches*/, D2D1_POINT_2F /*dip*/) {}
    // Return true when the key was handled.
    virtual bool OnKeyDown(UINT /*virtualKey*/) { return false; }
    virtual void OnFocusChanged(bool /*focused*/) {}
    virtual void OnDeviceReplaced() {}
    // First chance at every message (WM_GETOBJECT for UI Automation, WM_CONTEXTMENU...).
    virtual std::optional<LRESULT> OnMessage(UINT /*message*/, WPARAM /*wParam*/, LPARAM /*lParam*/) {
        return std::nullopt;
    }
    // Called after the HWND exists and before the first render (accessibility setup...).
    virtual void OnCreated() {}
    // Called while handling WM_DESTROY, before the composition tree is released.
    virtual void OnDestroying() {}

private:
    void OnFrame(double now) override;
    void Render(double now);
    void ApplyInputRegion();
    [[nodiscard]] D2D1_POINT_2F PointFromLParam(LPARAM lParam) const noexcept;
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);

    graphics::GraphicsDevice& device_;
    graphics::FrameScheduler& scheduler_;
    graphics::CompositionSurface surface_;
    Connection deviceReplaced_;
    SurfaceWindowOptions options_;
    HWND hwnd_ = nullptr;
    UINT dpi_ = 96;
    RECT bounds_{};
    std::vector<RECT> inputRegion_;
    std::vector<RECT> visualRegion_;
    D2D1_RECT_F backdropPanelDip_{};
    float backdropRadiusDip_ = 0.0f;
    float backdropOpacity_ = 1.0f;
    bool backdropBlurRequested_ = false;
    bool hiding_ = false;
    bool shown_ = false;
    bool trackingLeave_ = false;
    bool releasingCapture_ = false;
    bool renderPending_ = false;
    bool animationPending_ = false;
    bool inRender_ = false;
};

}  // namespace aether::ui
