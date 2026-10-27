// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Windows' hidden notification-area icons (D45, FR-038): the overflow of background applications'
// tray icons belongs to Explorer. Aether opens it only through UI Automation on the taskbar's own
// overflow button, recognized by its accessible identity (never by position, localized name,
// keystrokes or a guessed focus target), and reports success only when Windows actually shows its
// overflow window. Nothing reads, lists, hosts or imitates other applications' icons.
#pragma once

#include "app/TaskbarController.h"
#include "core/Cancellation.h"
#include "core/Result.h"

#include <chrono>
#include <functional>
#include <memory>

namespace aether::app {

// Explorer's taskbar as far as the overflow is concerned. Production: Shell_TrayWnd through UI
// Automation (MTA worker thread only: these calls go to Explorer and may wait on it).
class ITrayOverflow {
public:
    virtual ~ITrayOverflow() = default;
    // The taskbar exists and is visible; a hidden taskbar exposes no accessible content at all.
    [[nodiscard]] virtual bool TaskbarVisible() = 0;
    // Invokes the overflow button. NotFound when the taskbar shows none (no hidden icons, or its
    // content is not populated yet); Unsupported when the button offers no supported action.
    [[nodiscard]] virtual Result<void> InvokeOverflowButton() = 0;
    // Windows' overflow window is visible.
    [[nodiscard]] virtual bool OverflowOpen() = 0;
};

struct OverflowTiming {
    int attempts = 40;                          // about three seconds at the default interval
    std::chrono::milliseconds interval{75};     // between checks while the taskbar comes back
    std::chrono::milliseconds openWait{900};    // after an invoke, for the overflow to appear
};

// Worker only. Waits for a visible, populated taskbar, invokes its overflow button once and
// confirms that the overflow window appeared. An overflow that is already open counts as opened
// (invoking again would close it). Timeout when the taskbar never became usable.
[[nodiscard]] Result<void> OpenTrayOverflow(ITrayOverflow& tray, const CancellationToken& cancel, const OverflowTiming& timing = {},
                                            const std::function<void(std::chrono::milliseconds)>& sleep = {});

// While Aether hides the Windows taskbar, its overflow is unreachable: the taskbar has to be shown
// (replacement suspended) first and hidden again once the overflow closes.
[[nodiscard]] bool OverflowNeedsReveal(const TaskbarUpdate& state) noexcept;

[[nodiscard]] std::unique_ptr<ITrayOverflow> MakeSystemTrayOverflow();

}  // namespace aether::app
