// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// FormScrollView: a vertically scrolling region of a FormSurface window, built from two plain child
// windows so content never paints over a fixed header or footer:
//
//   viewport  (fixed, clips)            - created as a child of the form (or of another container)
//   └─ canvas (content height, moves)   - controls are created as children of Canvas()
//
// Scrolling moves the canvas (one SetWindowPos without copying bits) and repaints the visible scene,
// so the window grain stays anchored to the form. A thin overlay scroll indicator is painted at the
// viewport's trailing edge; it widens while hovered and can be dragged. Mouse wheel, WM_VSCROLL
// codes, EnsureVisible for keyboard focus and an optional short animated scroll are supported.
// The viewport answers WM_GETOBJECT with a UI Automation provider exposing the Scroll pattern
// (vertical percent, view size, Scroll and SetScrollPercent) merged with its native HWND provider.
//
// Both windows have WS_EX_CONTROLPARENT so IsDialogMessage walks into the content for Tab order.
// Threading: UI thread only.
#pragma once

#include "core/Result.h"
#include "ui/form/FormSurface.h"

#include <windows.h>

#include <functional>
#include <memory>

namespace aether::ui::form {

class FormScrollView {
public:
    FormScrollView();
    ~FormScrollView();
    FormScrollView(const FormScrollView&) = delete;
    FormScrollView& operator=(const FormScrollView&) = delete;

    // Creates the viewport (child of `parent`, control id `id`) and its canvas, registers both with
    // `surface` as Container controls and connects the overlay indicator to the surface's tokens.
    [[nodiscard]] Result<void> Create(FormSurface& surface, HWND parent, int id);
    void Destroy() noexcept;

    [[nodiscard]] HWND Viewport() const noexcept;
    [[nodiscard]] HWND Canvas() const noexcept;

    // Viewport frame in pixels of the parent's client area. The canvas keeps the viewport width.
    void SetFrame(const RECT& framePx);
    [[nodiscard]] RECT Frame() const noexcept;
    // Total content height in pixels; the position is clamped to the new range.
    void SetContentHeight(int heightPx);
    [[nodiscard]] int ContentHeight() const noexcept;

    // Width in pixels the owner keeps free of controls (and cards) at the canvas's trailing edge,
    // where the overlay scroll indicator lives.
    [[nodiscard]] int GutterPx() const noexcept;

    [[nodiscard]] int Position() const noexcept;   // current offset in pixels (0 = top)
    [[nodiscard]] int Range() const noexcept;      // maximum offset: max(0, content - page)
    [[nodiscard]] int Page() const noexcept;       // viewport height in pixels

    // Moves to `positionPx` (clamped). Animated only when `animate`, motion is allowed and the
    // distance is small; otherwise immediate. Interrupting an animation retargets from where it is.
    void ScrollTo(int positionPx, bool animate = false);
    void ScrollBy(int deltaPx, bool animate = false);
    // Scrolls the least distance that shows `canvasRectPx` (canvas coordinates) with `marginPx`
    // above and below it. Returns true when the position changed.
    bool EnsureVisible(const RECT& canvasRectPx, int marginPx);
    // Scrolls so the focused window, when it is inside the canvas, becomes visible.
    bool RevealFocus(int marginPx);

    // Handles WM_VSCROLL codes (SB_LINE*, SB_PAGE*, SB_TOP, SB_BOTTOM, SB_THUMB*) for owners that
    // forward legacy scroll messages; returns true when handled.
    bool HandleScrollCode(int code);
    // The same with the thumb position (pixels) that SB_THUMBPOSITION / SB_THUMBTRACK carry; the
    // one-argument form cannot know it and leaves those two codes unhandled.
    bool HandleScrollCode(int code, int thumbPositionPx);

    // Called on the UI thread after the position changed (layout-dependent owners, tests).
    std::function<void()> scrolled;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace aether::ui::form
