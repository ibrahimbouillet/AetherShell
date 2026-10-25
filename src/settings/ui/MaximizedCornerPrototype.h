// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#pragma once

#include "core/Result.h"

#include <windows.h>
#include <memory>

namespace aether::settings::ui::experimental {

struct MaximizedCornerLayout {
    // Window-local physical pixels, with exclusive right/bottom bounds.
    RECT clipPx{};
    int radiusPx = 0;
};

// Observed frame bounds and approved work area are screen-coordinate physical pixels.
// This geometry does not query or change the desktop, placement, or maximize state.
[[nodiscard]] Result<MaximizedCornerLayout> ComputeMaximizedCornerLayout(
    const RECT& nativeBounds, const RECT& visibleFrame, const RECT& workArea,
    UINT dpi, float radiusDip = 12.0f);

// Unwired, opt-in OWNED-WINDOW experiment, not a foreign-window rounding service.
// Caller must observe fresh DWM frame/work-area bounds. A native region proves clipping/input
// geometry only: DWM shadow, caption rendering and visual acceptance remain unqualified.
// Attach/Apply/Restore/destruction must all happen on the window's owning thread.
class MaximizedCornerPrototype final {
public:
    [[nodiscard]] static Result<std::unique_ptr<MaximizedCornerPrototype>> Attach(HWND window);
    ~MaximizedCornerPrototype();
    MaximizedCornerPrototype(const MaximizedCornerPrototype&) = delete;
    MaximizedCornerPrototype& operator=(const MaximizedCornerPrototype&) = delete;

    [[nodiscard]] Result<MaximizedCornerLayout> Apply(const RECT& visibleFrame,
        const RECT& workArea, UINT dpi, float radiusDip = 12.0f);
    // Restore a prior region only while this instance still owns the current region/identity.
    // A replaced region is preserved and reported; destructor cleanup is best effort.
    [[nodiscard]] Result<void> Restore();

private:
    MaximizedCornerPrototype(HWND window, DWORD thread, HRGN original, bool hadRegion) noexcept;
    [[nodiscard]] Result<void> CheckIdentity() const;
    [[nodiscard]] Result<bool> MatchesCurrent(HRGN expected, bool present) const;

    HWND window_ = nullptr;
    DWORD thread_ = 0;
    HRGN original_ = nullptr;
    bool hadRegion_ = false;
    HRGN installed_ = nullptr;
    bool busy_ = false;
};

}  // namespace aether::settings::ui::experimental
