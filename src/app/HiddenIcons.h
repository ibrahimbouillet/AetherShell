// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Windows' hidden notification-area icons (D45, FR-038): the overflow of background applications'
// tray icons belongs to Explorer. Aether opens it only through UI Automation on the taskbar's own
// overflow button, recognized by its chevron glyph (never by position, localized name, keystrokes or
// a guessed focus target), and reports success only when Windows actually shows its overflow window.
// The overflow is a window of its own: it opens while Aether hides the taskbar, which stays hidden.
// Nothing reads, lists, hosts or imitates other applications' icons.
#pragma once

#include "core/Cancellation.h"
#include "core/Result.h"

#include <chrono>
#include <functional>
#include <memory>
#include <string_view>

namespace aether::app {

// Explorer's taskbar as far as the overflow is concerned. Production: Shell_TrayWnd through UI
// Automation (MTA worker thread only: these calls go to Explorer and may wait on it).
class ITrayOverflow {
public:
    virtual ~ITrayOverflow() = default;
    // Invokes the overflow button. Unavailable without an Explorer taskbar; NotFound when it shows no
    // such button (no hidden icons, or its content is not populated yet); Unsupported when the button
    // offers no supported action.
    [[nodiscard]] virtual Result<void> InvokeOverflowButton() = 0;
    // Windows' overflow window is shown.
    [[nodiscard]] virtual bool OverflowOpen() = 0;
};

struct OverflowTiming {
    int attempts = 4;                           // a taskbar that is still populating its content
    std::chrono::milliseconds interval{75};     // between attempts and checks
    std::chrono::milliseconds openWait{900};    // after an invoke, for the overflow to appear
};

// Worker only. Invokes the overflow button once and confirms that the overflow window appeared. An
// overflow that is already open counts as opened (invoking again would close it).
[[nodiscard]] Result<void> OpenTrayOverflow(ITrayOverflow& tray, const CancellationToken& cancel, const OverflowTiming& timing = {},
                                            const std::function<void(std::chrono::milliseconds)>& sleep = {});

// The system tray's buttons share one automation id; the overflow button is the one that shows a
// chevron glyph (Segoe Fluent Icons / MDL2), whatever the display language. `text` is the content
// of one of the button's text elements.
[[nodiscard]] bool IsOverflowGlyph(std::wstring_view text) noexcept;

[[nodiscard]] std::unique_ptr<ITrayOverflow> MakeSystemTrayOverflow();

}  // namespace aether::app
