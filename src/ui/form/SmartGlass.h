// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// D84: Liquid's smart transparency for a form window (Settings). The window looks at what lies behind it and blurs
// only where its own content needs it:
//   * where its own text or controls lie over other content (a window, the desktop's icons and their names): the full
//     blur and tint, so text never reads over text;
//   * where its own content lies over the plain desktop picture: a light blur;
//   * where it has nothing of its own: fully clear, the desktop and the windows behind seen as they are.
//
// DWM's system backdrop blurs a whole window, and a composition layer of the window itself is drawn over its GDI
// content. So the blur lives in a second window: a click-through, never-activated window without a redirection
// surface, kept exactly under the form's client area and directly below it in z-order. It shows the blurred desktop
// (HostBackdropBrush) and the form's tint, each through a D2D AlphaMask effect whose mask is a small drawing surface,
// one pixel per cell of the form, stretched with linear filtering. The form keeps its frame extended over its client
// with no system backdrop, and paints its content with per-pixel alpha above.
//
// Threading: UI thread only (the thread whose DispatcherQueue the GraphicsDevice's compositor uses).
#pragma once

#include "core/Result.h"
#include "graphics/Theme.h"

#include <windows.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <vector>

namespace aether::graphics {
class GraphicsDevice;
}

namespace aether::ui::form {

// How much blur (0..1) lies under the form's own content.
struct SmartGlassLevels {
    float busy = 1.0f;    // over other content: windows, desktop icons and their names
    float quiet = 0.45f;  // over the plain desktop picture
};

// The blur under each cell of the client area: one byte (0..255) per cell, rows top to bottom.
struct SmartMask {
    int columns = 0;
    int rows = 0;
    int cellPx = 0;
    std::vector<std::uint8_t> cells;
    friend bool operator==(const SmartMask&, const SmartMask&) = default;
};

// Pure (tests): the mask of a client area `width` x `height` pixels in cells of `cellPx`. `own` is the form's own
// content and `busy` the other content behind it, both in client pixels; own content is widened by `marginPx`. A cell
// with own content gets levels.busy where other content lies behind it and levels.quiet otherwise; a cell without own
// content gets 0. Edges fade out as a gradient over three cells outward, never lowering a cell.
[[nodiscard]] SmartMask ComputeSmartMask(int width, int height, int cellPx, std::span<const RECT> own, std::span<const RECT> busy,
                                         int marginPx, SmartGlassLevels levels = {});

// What lies behind `window` (screen pixels, clipped to its rectangle): the visible, uncloaked top-level windows below
// it in z-order that are not click-through overlays, by their DWM frame bounds. Their content is not read: any such
// window may hold text, so it counts as busy. The desktop itself is not one of them (its icons count apart).
[[nodiscard]] std::vector<RECT> WindowsBehind(HWND window);
// The desktop's icons with their names (screen pixels); empty when the desktop shows none. Calls into Explorer:
// call from a worker thread (a COM apartment).
[[nodiscard]] std::vector<RECT> DesktopIconRects();
// The form's own content (client pixels): every visible child window that holds no visible window itself, clipped by
// the windows that hold it. Empty labels count for nothing.
[[nodiscard]] std::vector<RECT> OwnContentRects(HWND form);

class SmartGlass {
public:
    // `extraContent` adds content the form paints itself (its caption buttons), in client pixels.
    SmartGlass(graphics::GraphicsDevice& device, HWND form, std::function<std::vector<RECT>()> extraContent = {});
    ~SmartGlass();
    SmartGlass(const SmartGlass&) = delete;
    SmartGlass& operator=(const SmartGlass&) = delete;

    // Creates the backdrop window and its layers. Unavailable when the compositor cannot show the desktop blurred.
    [[nodiscard]] Result<void> Start();
    [[nodiscard]] bool Started() const noexcept;

    // The form's tint under its content, in the same mask as the blur.
    void SetTint(graphics::Color tint, float alpha);
    // From the form's window procedure: keeps the backdrop under the form (WM_WINDOWPOSCHANGING / CHANGED,
    // WM_SHOWWINDOW, WM_SIZE) and refreshes the mask soon after a move.
    void OnFormMessage(UINT message, WPARAM wParam, LPARAM lParam);
    // Recomputes the mask now (also done on its own timer while the form is shown).
    void Refresh();

    // Diagnostics and tests: the mask last drawn, and the backdrop window.
    [[nodiscard]] const SmartMask& Mask() const noexcept;
    [[nodiscard]] HWND BackdropWindow() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace aether::ui::form
