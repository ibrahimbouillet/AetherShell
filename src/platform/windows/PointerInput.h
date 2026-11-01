// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// FR-040 Win32 pointer plumbing shared by every owned surface. Touch contacts are read from
// WM_POINTER* messages with their identity, timestamp and cancellation; mouse, pen and precision
// touchpad keep their ordinary messages. A window that handles a touch pointer message (does not pass
// it to DefWindowProc) receives no promoted mouse message for it; IsTouchPromotedMouse() identifies
// promoted mouse messages that still arrive (for example from a window that declined the contact).
#pragma once

#include <windows.h>

#include <cstdint>
#include <functional>
#include <optional>

namespace aether::platform {

struct PointerContact {
    std::uint32_t id = 0;
    POINT screenPx{};
    double time = 0.0;        // seconds on the QueryPerformanceCounter clock (FrameScheduler::Now)
    bool touch = false;
    bool primary = false;
    bool inContact = false;
    bool canceled = false;    // POINTER_FLAG_CANCELED: the system ended this contact; never a tap
};

// The contact a WM_POINTERDOWN/UPDATE/UP/CAPTURECHANGED message refers to; nullopt if unavailable.
[[nodiscard]] std::optional<PointerContact> ReadPointer(WPARAM wParam) noexcept;
// True for a touch contact (PT_TOUCH). Pen, mouse and touchpad pointers are left to mouse handling.
[[nodiscard]] bool IsTouchPointer(WPARAM wParam) noexcept;
// For the mouse message being processed: true when Windows synthesized it from touch input.
[[nodiscard]] bool IsTouchPromotedMouse() noexcept;
[[nodiscard]] bool IsPointerMessage(UINT message) noexcept;

// Test seam (this thread only): contacts come from `source` instead of GetPointerInfo, so own-window
// suites can post WM_POINTER messages through the real routing. Empty restores the system source.
// This is not physical-touch evidence: digitizers, Windows gestures and timing stay unexercised.
void SetPointerSourceForTesting(std::function<std::optional<PointerContact>(WPARAM)> source);

}  // namespace aether::platform
