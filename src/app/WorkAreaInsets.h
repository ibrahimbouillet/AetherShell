// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#pragma once

#include "core/Result.h"

#include <windows.h>
#include <memory>

namespace aether::app {

struct WorkAreaInsetLayout {
    RECT left{};
    RECT right{};
    int widthPx = 0;
};

// Pure physical-pixel geometry. Insets affect the shell work area used by maximized windows;
// no normal application window is moved/resized. Invalid/overflowing monitor bounds fail closed.
[[nodiscard]] Result<WorkAreaInsetLayout> ComputeWorkAreaInsets(const RECT& monitorBounds, UINT dpi,
                                                               float insetDip = 12.0f);
// Pure AppBar QUERYPOS adjustment: honor the approved outer edge and vertical span, restore width.
[[nodiscard]] Result<RECT> FitWorkAreaInset(const RECT& approved, bool leftEdge, int widthPx);

// UI-thread owner of two zero-opacity, click-through infrastructure AppBars, not visible surfaces.
// Construction creates no HWND/reservation. Production must explicitly pass its native-access
// guard; isolated sessions pass false and SetEnabled(true) is rejected without touching the shell.
// The host disables leases for native-taskbar reveal and shutdown, re-enables on resume, and calls
// OnExplorerRestarted when its TaskbarCreated observer fires. No SPI_SETWORKAREA or registry writes.
class WorkAreaInsets {
public:
    explicit WorkAreaInsets(bool allowNativeAccess, float insetDip = 12.0f);
    ~WorkAreaInsets();
    WorkAreaInsets(const WorkAreaInsets&) = delete;
    WorkAreaInsets& operator=(const WorkAreaInsets&) = delete;

    [[nodiscard]] Result<void> SetEnabled(bool enabled);
    [[nodiscard]] Result<void> Refresh();
    [[nodiscard]] Result<void> OnExplorerRestarted();
    [[nodiscard]] bool Enabled() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace aether::app
