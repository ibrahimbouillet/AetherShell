// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// D85: the Top Bar's strip stays free of application windows (FR-002, FR-039).
//
// Windows keeps maximized and snapped windows out of the work area the Top Bar reserves, but only while Explorer holds
// that reservation, and only for the windows it lays out itself. A reservation Explorer lost (an Explorer that started
// after Aether at sign-in), a window maximized before it, an application restoring its own saved position or a window
// dragged up by hand still lay under the bar's text. The keeper:
//   - asks the bar to register its strip again when Windows' work area no longer leaves it out;
//   - puts back below the strip any application window that reaches into it. A maximized window is fitted to the work
//     area again. Another one is moved down by as much as it reached in, keeping its size; it is made shorter only
//     when it is resizable and would otherwise pass the bottom of the work area.
//   - tells the host whether a window still lies under the strip (one it may not move: always on top, elevated, or
//     one that keeps going back), so the written items sit on glass rather than straight on that window.
// It looks at a window only after it was shown, restored, brought to the front or let go after a move by hand, never
// during a drag; never at tool windows, always-on-top windows, full-screen windows, the desktop, the shell's own
// windows or Aether's. A window that goes back three times within a minute is left where it is. The moves are
// asynchronous (SWP_ASYNCWINDOWPOS): an application that does not answer never holds Aether. Nothing is read of a
// window but its state, styles and bounds.
#pragma once

#include <windows.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace aether::app {

// What the keeper reads of one window (physical pixels).
struct StripWindowFacts {
    RECT outer{};             // GetWindowRect, invisible resize borders included
    RECT frame{};             // DWM's visible frame
    RECT monitor{};           // the monitor it is on
    RECT work{};              // that monitor's work area
    bool maximized = false;   // IsZoomed
    bool resizable = false;   // WS_THICKFRAME
};

// Pure (unit tested): the window rectangle (GetWindowRect coordinates) that keeps the window out of `strip`, or
// nothing when it does not reach into the strip, is on another monitor, covers its whole monitor (full screen), or
// is maximized while the work area itself still includes the strip (the reservation must come back first).
[[nodiscard]] std::optional<RECT> FitBelowStrip(const StripWindowFacts& facts, const RECT& strip) noexcept;

// Bounded retries per window (pure): at most `limit` moves of the same window within `windowMs`.
class StripRetryBudget {
public:
    explicit StripRetryBudget(unsigned limit = 3, std::uint64_t windowMs = 60000) noexcept : limit_(limit), windowMs_(windowMs) {}
    // Records a move of `window` at `now` if the budget allows one; false when it is spent.
    [[nodiscard]] bool Take(HWND window, std::uint64_t now);
    // Moves recorded within the last `windowMs` (all windows).
    [[nodiscard]] std::size_t Recorded() const noexcept { return entries_.size(); }

private:
    struct Entry {
        HWND window = nullptr;
        std::uint64_t at = 0;
    };
    unsigned limit_;
    std::uint64_t windowMs_;
    std::vector<Entry> entries_;   // one per move, dropped once older than windowMs_
};

struct TopStripCallbacks {
    // The strip the bar reserves now (screen pixels), or nothing when it reserves none (Auto-Hide, turned off,
    // hidden for full screen, tests).
    std::function<std::optional<RECT>()> strip;
    // Windows' work area leaves the strip in: the bar should register it again (the bar bounds how often).
    std::function<void()> reservationMissing;
    // Whether an application window still lies under the strip after the keeper's pass.
    std::function<void(bool)> covered;
};

// UI thread only. Production passes allowNativeAccess=true; isolated sessions pass false and the keeper then hooks
// nothing and moves nothing.
class TopStripKeeper final {
public:
    TopStripKeeper(bool allowNativeAccess, TopStripCallbacks callbacks);
    ~TopStripKeeper();
    TopStripKeeper(const TopStripKeeper&) = delete;
    TopStripKeeper& operator=(const TopStripKeeper&) = delete;

    // Starts or stops watching windows. Off, nothing is hooked and the host is told nothing covers the strip.
    void SetEnabled(bool enabled);
    [[nodiscard]] bool Enabled() const noexcept;
    // Looks at every window a moment from now: after the strip was reserved again, the work area changed, or the
    // displays changed.
    void SweepSoon();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace aether::app
