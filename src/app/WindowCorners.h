// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#pragma once

#include "core/Result.h"

#include <windows.h>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

namespace aether::app {

// The guard observes genuine maximized windows only; it never changes placement or styles.
// Regions clip both painting and input, but are not antialiased DWM corner geometry.
struct CornerWindowObservation {
    DWORD process = 0;
    DWORD thread = 0;
    RECT bounds{};
    RECT frame{};
    RECT work{};
    UINT dpi = 96;
    bool eligible = false;
};
using CornerRegion = std::vector<std::byte>;

// D45: the thin continuous perimeter around a clipped window. The window is clipped to a rounded
// rectangle inset by `widthPx`; a click-through ring window directly beneath it fills exactly that
// band (the same rasterized geometry), so the rim never covers application content or input.
struct CornerRim {
    RECT bounds{};      // outer rounded rectangle, screen pixels
    int radiusPx = 0;   // outer corner radius
    int widthPx = 0;    // ring width (the clip is inset by this much)
    [[nodiscard]] bool operator==(const CornerRim& other) const noexcept {
        return ::EqualRect(&bounds, &other.bounds) != FALSE && radiusPx == other.radiusPx && widthPx == other.widthPx;
    }
};
// Physical width of the rim at `dpi` (one DIP, at least one pixel).
[[nodiscard]] int CornerRimWidthPx(UINT dpi) noexcept;

// Injection boundary for lease tests. All native methods run exclusively in the child guard,
// never on Aether's UI thread. A missing region means the window has no custom region.
// V4 (AS4-03): eligibility and responsiveness are separate. A lease never sends a mutation to a
// target that is not answering; it defers (ErrorCode::Timeout) and keeps its snapshot instead.
// WriteRegion may also return Timeout when the target stalled inside the write: the write is then
// still in flight, its outcome is read back once Responsive() is true again.
class IWindowCornerBackend {
public:
    virtual ~IWindowCornerBackend() = default;
    virtual Result<CornerWindowObservation> Observe(HWND window) = 0;
    // Cheap, bounded and side-effect free; false while a previous write to `window` is pending.
    virtual bool Responsive(HWND) noexcept { return true; }
    virtual Result<std::optional<CornerRegion>> ReadRegion(HWND window) = 0;
    virtual Result<void> WriteRegion(HWND window, const std::optional<CornerRegion>& region) = 0;
    // nullopt means this documented attribute is unavailable on this Windows version.
    virtual Result<std::optional<DWORD>> ReadBackdrop(HWND window) = 0;
    virtual Result<void> WriteBackdrop(HWND window, DWORD value) = 0;
    virtual bool Owns(HWND window, std::uintptr_t token) noexcept = 0;
    virtual Result<void> Claim(HWND window, std::uintptr_t token) = 0;
    virtual void Release(HWND window, std::uintptr_t token) noexcept = 0;
    // AS4-05: removes the claim of a guard process that no longer exists, and says whether there was one.
    virtual bool ClearAbandonedClaim(HWND) noexcept { return false; }
};

// What made a lease give its window back.
enum class CornerReplacement {
    None,
    Region,              // the application installed its own window region
    Material,            // the application chose a different system material
    MaterialReasserted,  // the application re-applied the very material it had before the clip
    RegionReasserted,    // no region, or a plain rectangle around the clip: not a shape of its own
};
// D46 (owner feedback, 2026-10-27), D56: an application that only re-asserts what it had (Chromium
// re-applying Mica or its work-area rectangle) is clipped again after a growing delay, at most this many
// times in a burst, and again once it has been quiet for kCornerRetryRestMs. Every other replacement wins
// until the window is maximized anew.
inline constexpr int kCornerMaterialRetries = 8;
inline constexpr UINT kCornerRetryRestMs = 60000;
[[nodiscard]] std::optional<std::chrono::milliseconds> CornerRetryDelay(CornerReplacement replacement, int attempt) noexcept;

// A lifetime/region lease, not an HWND-only instruction. If an app changes its region, the
// replacement is preserved and the guard stops managing that identity until it is destroyed.
class WindowCornerLease final {
public:
    [[nodiscard]] static Result<std::unique_ptr<WindowCornerLease>> Acquire(
        IWindowCornerBackend& backend, HWND window, std::uintptr_t token);
    ~WindowCornerLease();
    WindowCornerLease(const WindowCornerLease&) = delete;
    WindowCornerLease& operator=(const WindowCornerLease&) = delete;
    [[nodiscard]] Result<bool> Refresh(); // false means restored because it is no longer eligible
    [[nodiscard]] Result<void> Restore();
    [[nodiscard]] bool HasOwnership() const noexcept { return claimed_; }
    // The perimeter to draw beneath the window while it is clipped (nullopt when restored).
    [[nodiscard]] const std::optional<CornerRim>& Rim() const noexcept { return rim_; }
    // Why the last Refresh gave the window back (None while it is still clipped).
    [[nodiscard]] CornerReplacement Replacement() const noexcept { return replacement_; }

private:
    WindowCornerLease(IWindowCornerBackend& backend, HWND window, std::uintptr_t token,
        DWORD process, DWORD thread) noexcept;
    [[nodiscard]] Result<CornerWindowObservation> ObserveOwned();
    [[nodiscard]] Result<void> Reconcile();
    [[nodiscard]] Result<void> WriteOwnedRegion(const std::optional<CornerRegion>& value);
    IWindowCornerBackend& backend_;
    HWND window_ = nullptr;
    std::uintptr_t token_ = 0;
    DWORD process_ = 0;
    DWORD thread_ = 0;
    std::optional<CornerRegion> original_;
    std::optional<CornerRegion> installed_;
    // A region write whose bounded wait expired; it may still land (AS4-03).
    std::optional<std::optional<CornerRegion>> uncertain_;
    std::optional<DWORD> originalBackdrop_;
    std::optional<CornerRim> rim_;
    CornerReplacement replacement_ = CornerReplacement::None;
    bool claimed_ = false;
    bool applied_ = false;
    bool backdropApplied_ = false;
};

// Sanitized counters (no window titles, classes or process names) for bounded qualification of
// event load and mutation churn (AS4-07). Optional; written only by the guard thread.
struct WindowCornerGuardStatistics {
    std::atomic<std::uint64_t> events{0};          // WinEvents/broadcasts that reached the guard
    std::atomic<std::uint64_t> sweeps{0};          // refresh passes, including dirty-only passes
    std::atomic<std::uint64_t> fullSweeps{0};      // passes that enumerated top-level windows
    std::atomic<std::uint64_t> leaseRefreshes{0};
    std::atomic<std::uint64_t> regionWrites{0};    // writes that completed within their bound
    std::atomic<std::uint64_t> pendingWrites{0};   // writes that outlived their bound
    std::atomic<std::uint64_t> deferrals{0};       // mutations postponed for an unresponsive target
    std::atomic<std::uint64_t> longestSweepMicros{0};
};

struct WindowCornerGuardOptions {
    // Tests MUST set both. No other windows are enumerated, queried or modified in this mode.
    DWORD onlyProcess = 0;
    std::vector<HWND> onlyWindows;
    WindowCornerGuardStatistics* statistics = nullptr;
};

// Internal child entrypoint. Normal production uses three explicitly inherited handles.
// The child restores its leases after stop or parent death, then exits. No settings/logging IO.
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

// Request is asynchronous acceptance, not proof of readiness or visual compatibility. Stop is
// shutdown-only and joins its worker after asking the child to restore. A foreign app hanging
// inside synchronous SetWindowRgn can delay the child; never terminate that recovery child.
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
