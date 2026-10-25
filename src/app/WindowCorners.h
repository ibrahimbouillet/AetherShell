// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#pragma once

#include "core/Result.h"

#include <windows.h>
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

// Injection boundary for lease tests. All native methods run exclusively in the child guard,
// never on Aether's UI thread. A missing region means the window has no custom region.
class IWindowCornerBackend {
public:
    virtual ~IWindowCornerBackend() = default;
    virtual Result<CornerWindowObservation> Observe(HWND window) = 0;
    virtual Result<std::optional<CornerRegion>> ReadRegion(HWND window) = 0;
    virtual Result<void> WriteRegion(HWND window, const std::optional<CornerRegion>& region) = 0;
    // nullopt means this documented attribute is unavailable on this Windows version.
    virtual Result<std::optional<DWORD>> ReadBackdrop(HWND window) = 0;
    virtual Result<void> WriteBackdrop(HWND window, DWORD value) = 0;
    virtual bool Owns(HWND window, std::uintptr_t token) noexcept = 0;
    virtual Result<void> Claim(HWND window, std::uintptr_t token) = 0;
    virtual void Release(HWND window, std::uintptr_t token) noexcept = 0;
};

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

private:
    WindowCornerLease(IWindowCornerBackend& backend, HWND window, std::uintptr_t token,
        DWORD process, DWORD thread) noexcept;
    [[nodiscard]] Result<CornerWindowObservation> ObserveOwned();
    IWindowCornerBackend& backend_;
    HWND window_ = nullptr;
    std::uintptr_t token_ = 0;
    DWORD process_ = 0;
    DWORD thread_ = 0;
    std::optional<CornerRegion> original_;
    std::optional<CornerRegion> installed_;
    std::optional<DWORD> originalBackdrop_;
    bool claimed_ = false;
    bool applied_ = false;
    bool backdropApplied_ = false;
};

struct WindowCornerGuardOptions {
    // Tests MUST set both. No other windows are enumerated, queried or modified in this mode.
    DWORD onlyProcess = 0;
    std::vector<HWND> onlyWindows;
};

// Internal child entrypoint. Normal production uses three explicitly inherited handles.
// The child restores its leases after stop or parent death, then exits. No settings/logging IO.
[[nodiscard]] Result<int> RunWindowCornerGuard(HANDLE parent, HANDLE ready, HANDLE stop,
    const WindowCornerGuardOptions& options = {});
[[nodiscard]] Result<std::optional<int>> DispatchWindowCornerGuard(
    const std::vector<std::wstring>& arguments);

// Request is asynchronous acceptance, not proof of readiness or visual compatibility. Stop is
// shutdown-only and joins its worker after asking the child to restore. A foreign app hanging
// inside synchronous SetWindowRgn can delay the child; never terminate that recovery child.
class WindowCorners final {
public:
    WindowCorners(bool allowNativeAccess, std::filesystem::path hostExecutable);
    ~WindowCorners();
    WindowCorners(const WindowCorners&) = delete;
    WindowCorners& operator=(const WindowCorners&) = delete;
    [[nodiscard]] Result<void> Request(bool enabled) noexcept;
    void Stop() noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace aether::app
