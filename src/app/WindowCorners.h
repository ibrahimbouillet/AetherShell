// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Rounded corners and a thin perimeter for genuinely maximized windows (FR-036, D72, ADR 0014).
//
// The guard never writes to another application's window: no region, no DWM attribute, no property,
// no message. It only reads what Windows reports about top-level windows (state, bounds, styles,
// whether a region exists), and draws with two click-through windows of its own, kept directly
// above the front maximized window of a monitor:
//   - the corner pieces: the four areas outside the rounded outline, showing the live desktop there
//     through DWM thumbnails of the desktop window, pixel for pixel;
//   - the rim: the thin band just inside that outline.
// A window in front of the maximized one covers them like anything else. Every kind of window is
// served alike (browsers, Electron, Explorer, elevated and packaged applications), and nothing the
// guard does can hang, blank or reshape one. If the guard stops or dies, its windows go with it and
// every application is exactly as it always was.
#pragma once

#include "core/Result.h"

#include <windows.h>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

namespace aether::app {

// What the guard reads of one top-level window. Nothing here is ever written back.
struct CornerWindowFacts {
    bool visible = false;
    bool minimized = false;
    bool maximized = false;      // IsZoomed: genuine maximize, not a window merely sized to the work area
    bool topLevel = false;       // its own root
    bool cloaked = false;        // on another virtual desktop, or suspended
    bool toolWindow = false;     // WS_EX_TOOLWINDOW
    bool clickThrough = false;   // WS_EX_TRANSPARENT
    bool own = false;            // one of the guard's own windows
    RECT bounds{};               // GetWindowRect
    RECT frame{};                // DWM's visible frame
    RECT monitor{};              // the monitor it is on
    RECT work{};                 // that monitor's work area
    UINT dpi = 96;
    // Its window region, if any: 0 none, SIMPLEREGION a plain rectangle, COMPLEXREGION a shape of its own.
    int regionKind = 0;
    RECT regionBox{};            // window coordinates
};

struct CornerWindowObservation {
    RECT bounds{};
    RECT frame{};
    RECT work{};
    UINT dpi = 96;
    bool eligible = false;
};
// Pure rule (unit tested). Eligible: a visible, genuinely maximized application window sitting between
// the bars. D56: one that covers its whole monitor (both bars auto-hidden, full screen) simply takes the
// screen. A window with a shape of its own keeps it: Aether's own maximized Settings rounds itself.
[[nodiscard]] CornerWindowObservation ClassifyCornerWindow(const CornerWindowFacts& facts) noexcept;

// The rounded outline of a maximized window, screen pixels: where the corner pieces and the rim go.
struct CornerRim {
    RECT bounds{};      // outer rounded rectangle
    int radiusPx = 0;   // outer corner radius
    int widthPx = 0;    // rim width
    [[nodiscard]] bool operator==(const CornerRim& other) const noexcept {
        return ::EqualRect(&bounds, &other.bounds) != FALSE && radiusPx == other.radiusPx && widthPx == other.widthPx;
    }
};
// Physical width of the rim at `dpi` (one DIP, at least one pixel).
[[nodiscard]] int CornerRimWidthPx(UINT dpi) noexcept;
[[nodiscard]] std::optional<CornerRim> CornerFrameFor(const CornerWindowObservation& observation);

// Pure geometry (unit tested), rectangles relative to a `widthPx` x `heightPx` outline with corners of
// `radiusPx`. The pieces are every pixel outside the rounded outline (four corners); the rim is the band
// `rimPx` wide just inside it. A pixel is never in both, and none is left between them.
[[nodiscard]] std::vector<RECT> CornerPieceRects(int widthPx, int heightPx, int radiusPx);
[[nodiscard]] std::vector<RECT> CornerRimRects(int widthPx, int heightPx, int radiusPx, int rimPx);

// Sanitized counters (no window titles, classes or process names) for bounded qualification of
// event load. Optional; written only by the guard thread.
struct WindowCornerGuardStatistics {
    std::atomic<std::uint64_t> events{0};          // WinEvents/broadcasts that asked for a look
    std::atomic<std::uint64_t> sweeps{0};          // looks at the windows
    std::atomic<std::uint64_t> placements{0};      // times the guard's own windows were moved or restacked
    std::atomic<std::uint64_t> abandonedClips{0};  // clips of an earlier version's dead guard that were removed
    std::atomic<std::uint64_t> longestSweepMicros{0};
};

struct WindowCornerGuardOptions {
    // Tests MUST set both. No other window is queried in this mode.
    DWORD onlyProcess = 0;
    std::vector<HWND> onlyWindows;
    WindowCornerGuardStatistics* statistics = nullptr;
    // Tests: the window that stands for the desktop (a private backdrop). Production finds Windows' own.
    HWND desktopSource = nullptr;
};

// Internal child entrypoint. Normal production uses three explicitly inherited handles. The child leaves
// when asked to stop or when its parent is gone; its windows leave with it. No settings/logging IO.
[[nodiscard]] Result<int> RunWindowCornerGuard(HANDLE parent, HANDLE ready, HANDLE stop,
    const WindowCornerGuardOptions& options = {});
[[nodiscard]] Result<std::optional<int>> DispatchWindowCornerGuard(
    const std::vector<std::wstring>& arguments);

// AS4-04: the wanted state (a saved preference) is not the effective state. A failed start or a
// helper that died is observed, retried a bounded number of times and then reported as Failed.
enum class CornerGuardState { Off, Starting, Ready, Failed };
struct CornerGuardHealth {
    bool wanted = false;
    CornerGuardState state = CornerGuardState::Off;
    unsigned failures = 0;  // consecutive failed starts or unexpected exits since the last request
};
struct WindowCornersOptions {
    std::chrono::milliseconds firstRetryDelay{1000};  // doubled after every further failure
    unsigned maximumAttempts = 4;
};

// Request is asynchronous acceptance, not proof of readiness. Stop is shutdown-only and joins its
// worker after asking the child to leave.
class WindowCorners final {
public:
    WindowCorners(bool allowNativeAccess, std::filesystem::path hostExecutable, WindowCornersOptions options = {});
    ~WindowCorners();
    WindowCorners(const WindowCorners&) = delete;
    WindowCorners& operator=(const WindowCorners&) = delete;
    [[nodiscard]] Result<void> Request(bool enabled) noexcept;
    // Periodic supervision from the owner's existing timer: notices a helper that exited and
    // retries a failed start once its back-off elapsed. Never blocks; no-op while not wanted.
    void Poll() noexcept;
    [[nodiscard]] CornerGuardHealth Health() const noexcept;
    void Stop() noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace aether::app
