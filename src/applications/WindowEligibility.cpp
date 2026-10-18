// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "applications/WindowEligibility.h"

#include "core/Strings.h"
#include "platform/windows/WindowUtilities.h"

#include <array>

namespace aether::apps {
namespace {

// GetWindowTextW on a window of another process copies the caption kept by the window manager and
// never sends WM_GETTEXT, so a hung application cannot block the caller. GetWindowTextLengthW is not
// documented with that guarantee, hence the fixed buffer (longer captions are truncated, which is
// fine for a Dock label).
std::wstring ForeignWindowTitle(HWND hwnd) {
    std::array<wchar_t, 512> buffer{};
    const int copied = ::GetWindowTextW(hwnd, buffer.data(), static_cast<int>(buffer.size()));
    return copied > 0 ? std::wstring(buffer.data(), static_cast<std::size_t>(copied)) : std::wstring{};
}

}  // namespace

bool IsShellWindowClass(std::wstring_view className) noexcept {
    return className == L"Progman" || className == L"WorkerW" || className == L"Shell_TrayWnd" ||
           className == L"Shell_SecondaryTrayWnd";
}

WindowExclusion ClassifyWindow(const WindowFacts& facts) noexcept {
    const bool appWindow = (facts.exStyle & WS_EX_APPWINDOW) != 0;
    if (facts.ownProcess) return WindowExclusion::OwnProcess;
    if (!facts.visible) return WindowExclusion::Hidden;
    if (facts.cloaked) return WindowExclusion::Cloaked;
    if (!facts.topLevel) return WindowExclusion::NotTopLevel;
    if (IsShellWindowClass(facts.className)) return WindowExclusion::ShellWindow;
    if (facts.owned && !appWindow) return WindowExclusion::Owned;
    if ((facts.exStyle & WS_EX_TOOLWINDOW) != 0 && !appWindow) return WindowExclusion::ToolWindow;
    if ((facts.exStyle & WS_EX_NOACTIVATE) != 0 && !appWindow) return WindowExclusion::NoActivate;
    if (text::Trim(std::wstring_view(facts.title)).empty()) return WindowExclusion::Untitled;
    return WindowExclusion::None;
}

WindowFacts CollectWindowFacts(HWND hwnd) {
    WindowFacts facts;
    DWORD processId = 0;
    ::GetWindowThreadProcessId(hwnd, &processId);
    facts.ownProcess = processId == ::GetCurrentProcessId();
    facts.visible = ::IsWindowVisible(hwnd) != FALSE;
    facts.topLevel = ::GetAncestor(hwnd, GA_PARENT) == ::GetDesktopWindow();
    facts.owned = ::GetWindow(hwnd, GW_OWNER) != nullptr;
    facts.exStyle = static_cast<DWORD>(::GetWindowLongPtrW(hwnd, GWL_EXSTYLE));
    if (facts.ownProcess || !facts.visible) return facts;  // excluded anyway; skip the remaining calls
    facts.cloaked = platform::IsWindowCloaked(hwnd);
    facts.className = platform::WindowClassName(hwnd);
    facts.title = ForeignWindowTitle(hwnd);
    return facts;
}

}  // namespace aether::apps
