// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Which top-level windows count as "application windows" (the ones a taskbar would show a button
// for). The predicate is a pure function over WindowFacts so it can be unit tested; CollectWindowFacts
// gathers the facts for a real window.
#pragma once

#include <windows.h>

#include <string>
#include <string_view>

namespace aether::apps {

struct WindowFacts {
    bool visible = false;     // IsWindowVisible
    bool cloaked = false;     // DWMWA_CLOAKED (other virtual desktop, suspended UWP frame...)
    bool topLevel = false;    // no parent window
    bool owned = false;       // GetWindow(GW_OWNER) != nullptr
    DWORD exStyle = 0;        // GWL_EXSTYLE
    std::wstring title;
    std::wstring className;
    bool ownProcess = false;  // belongs to the calling process (the Dock, the Top Bar...)
};

enum class WindowExclusion {
    None,         // eligible
    OwnProcess,
    Hidden,
    Cloaked,
    NotTopLevel,
    ShellWindow,  // desktop and taskbar windows
    Owned,        // owned without WS_EX_APPWINDOW (dialogs, palettes)
    ToolWindow,   // WS_EX_TOOLWINDOW without WS_EX_APPWINDOW
    NoActivate,   // WS_EX_NOACTIVATE without WS_EX_APPWINDOW
    Untitled,
};

// Progman, WorkerW, Shell_TrayWnd, Shell_SecondaryTrayWnd (exact class names).
[[nodiscard]] bool IsShellWindowClass(std::wstring_view className) noexcept;

// First rule that excludes the window, or WindowExclusion::None. Rules: not our own process, visible,
// not cloaked, top-level, not a shell window, unowned unless WS_EX_APPWINDOW, not WS_EX_TOOLWINDOW or
// WS_EX_NOACTIVATE unless WS_EX_APPWINDOW, non-blank title.
[[nodiscard]] WindowExclusion ClassifyWindow(const WindowFacts& facts) noexcept;
[[nodiscard]] inline bool IsEligibleAppWindow(const WindowFacts& facts) noexcept {
    return ClassifyWindow(facts) == WindowExclusion::None;
}

// Reads the facts of a real window. The title and class are not read for windows of the calling
// process (reading our own titles could send messages to a busy thread of ours); `ownProcess` is
// enough to exclude them.
[[nodiscard]] WindowFacts CollectWindowFacts(HWND hwnd);

}  // namespace aether::apps
