// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Auto-Hide for the Dock and the Top Bar (D47, FR-039). Each bar has its own setting. An enabled bar
// hides only while an application window covers its area: on the desktop it stays. Resting the
// pointer on the screen edge the bar belongs to reveals it; it stays while it is used (pointer,
// menus, panels, drag, keyboard) and hides again a moment after that ends. An auto-hidden bar
// reserves no work area, so maximized windows use the whole screen and the revealed bar floats over
// them, as Windows' auto-hide taskbar does.
// D52, D56: the Top Bar waits a moment on its edge, because controls of maximized applications live
// right there and a quick visit must not put the bar over them. Leaving the edge cancels the wait.
#pragma once

#include <windows.h>

#include <cstdint>
#include <optional>

namespace aether::app {

struct AutoHideTiming {
    std::uint64_t revealDwellMs = 40;  // on the edge this long: sweeping across the edge does not reveal
    std::uint64_t hideDelayMs = 700;   // after the last use, before hiding again
};
// D56: measured on the owner's machine, the old two seconds ran exactly, and still felt like five: the
// wait only began on the very last rows of pixels, wherever the hand had actually stopped before that.
inline constexpr AutoHideTiming kTopBarAutoHideTiming{.revealDwellMs = 1000};

// The Top Bar's edge as a hand meets it: the wait starts in the top six DIPs and survives a wobble of
// the hand within the top sixteen. Only a real move away cancels it (D54, D56).
inline constexpr float kTopEdgeArrivalDip = 6.0f;
inline constexpr float kTopEdgeHoldDip = 16.0f;

// Whether the pointer is within `heightDip` of the top of `monitor` (physical pixels; `dpi` is the
// monitor's). A pointer that went on to a monitor above has left this edge, even though its y
// coordinate is smaller.
[[nodiscard]] bool PointerOnTopEdge(const POINT& pointer, const RECT& monitor, UINT dpi, float heightDip = kTopEdgeArrivalDip) noexcept;

// One periodic observation of a bar (the host samples about twenty times a second).
struct AutoHideSample {
    bool enabled = false;   // the person's setting for this bar
    bool covered = false;   // an application window overlaps the bar's area (otherwise the desktop shows)
    bool engaged = false;   // in use: pointer over it, a menu, panel, drag or keyboard focus of it
    bool atEdge = false;    // the pointer rests on the screen edge this bar belongs to
    bool nearEdge = false;  // close enough that a wait already running continues (the Dock leaves it off)
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
[[nodiscard]] bool ApplicationWindowCovers(const RECT& zone) noexcept;

}  // namespace aether::app
