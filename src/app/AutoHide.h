// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Auto-Hide for the Dock and the Top Bar (D47, FR-039). Each bar has its own setting. An enabled bar
// hides only while an application window covers its area: on the desktop it stays. Resting the
// pointer on the screen edge the bar belongs to reveals it; it stays while it is used (pointer,
// menus, panels, drag, keyboard) and hides again a moment after that ends. An auto-hidden bar
// reserves no work area, so maximized windows use the whole screen and the revealed bar floats over
// them, as Windows' auto-hide taskbar does.
// D52, D60: the Top Bar waits a moment on the very top of the screen, because controls of maximized
// applications live right below it and they must stay usable. Leaving that row cancels the wait.
#pragma once

#include <windows.h>

#include <cstdint>
#include <optional>
#include <string_view>

namespace aether::app {

struct AutoHideTiming {
    std::uint64_t revealDwellMs = 40;  // on the edge this long: sweeping across the edge does not reveal
    std::uint64_t hideDelayMs = 700;   // after the last use, before hiding again
};
// D60: a quarter of a second, the owner's number.
inline constexpr AutoHideTiming kTopBarAutoHideTiming{.revealDwellMs = 250};

// D60: the wait starts only on the very top row of pixels of `monitor` (physical pixels), with no margin:
// a browser's tabs and a window's close button sit just below it and belong to the application. A pointer
// that went on to a monitor above has left this edge, even though its y coordinate is smaller.
[[nodiscard]] bool PointerOnTopEdge(const POINT& pointer, const RECT& monitor) noexcept;

// One periodic observation of a bar (the host samples about twenty times a second).
struct AutoHideSample {
    bool enabled = false;   // the person's setting for this bar
    bool covered = false;   // an application window overlaps the bar's area (otherwise the desktop shows)
    bool engaged = false;   // in use: pointer over it, a menu, panel, drag or keyboard focus of it
    bool atEdge = false;    // the pointer rests on the screen edge this bar belongs to
    std::uint64_t now = 0;  // monotonic milliseconds
};

// Pure decision for one bar (unit tested): whether it should be hidden after `sample`.
class AutoHideState {
public:
    explicit AutoHideState(AutoHideTiming timing = {}) noexcept : timing_(timing) {}
    bool Update(const AutoHideSample& sample) noexcept;
    // Forgets the observed state (the setting went off); this bar keeps its own timing.
    void Reset() noexcept { *this = AutoHideState(timing_); }
    [[nodiscard]] bool Hidden() const noexcept { return hidden_; }
    // How long the current wait on the edge has run; zero when there is none.
    [[nodiscard]] std::uint64_t EdgeRestMs(std::uint64_t now) const noexcept { return edgeSince_ ? now - *edgeSince_ : 0; }

private:
    AutoHideTiming timing_;
    bool hidden_ = false;
    bool revealed_ = false;  // shown by the edge or by use while covered
    std::uint64_t lastUse_ = 0;
    std::optional<std::uint64_t> edgeSince_;
};

// Whether a visible application window overlaps `zone` (physical screen pixels): not minimized,
// cloaked, click-through, a tool window, the desktop, the shell's own windows or this process.
// Only window classes, styles and bounds are read.
// D59: nothing covers a bar while the desktop itself is in front (Show desktop, a click on it), even with
// windows still behind it: back on the desktop, both bars return.
[[nodiscard]] bool ApplicationWindowCovers(const RECT& zone) noexcept;
[[nodiscard]] bool IsDesktopWindowClass(std::wstring_view name) noexcept;

}  // namespace aether::app
