// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "applications/SystemShellActions.h"
#include "platform/windows/OsVersion.h"

namespace aether::apps {
namespace {

bool IsWindowOfThisThread(HWND window) noexcept {
    return window != nullptr && ::IsWindow(window) &&
           ::GetWindowThreadProcessId(window, nullptr) == ::GetCurrentThreadId();
}

Result<void> WithMessage(Result<void> result, const char* message) {
    if (result) return result;
    return MakeError(result.error().code, message, result.error().platformCode);
}

}  // namespace

Result<void> OpenStartMenu(HWND ownUiThreadWindow, platform::IKeyboardInjector& injector, StartMenuRoute route) {
    if (route == StartMenuRoute::SystemCommand && IsWindowOfThisThread(ownUiThreadWindow)) {
        // Documented WM_SYSCOMMAND command; DefWindowProcW forwards it to the shell. The return value
        // carries no outcome.
        (void)::DefWindowProcW(ownUiThreadWindow, WM_SYSCOMMAND, SC_TASKLIST, 0);
        return Ok();
    }
    return WithMessage(platform::SendKeyChord(injector, {static_cast<WORD>(VK_LWIN)}),
                       "Windows Start could not be opened");
}

Result<void> OpenStartMenu(HWND ownUiThreadWindow, StartMenuRoute route) {
    return OpenStartMenu(ownUiThreadWindow, platform::SystemKeyboardInjector(), route);
}

Result<void> OpenTaskView(platform::IKeyboardInjector& injector) {
    return WithMessage(platform::SendKeyChord(injector, {static_cast<WORD>(VK_LWIN), static_cast<WORD>(VK_TAB)}),
                       "Task View could not be opened");
}

Result<void> OpenTaskView() { return OpenTaskView(platform::SystemKeyboardInjector()); }

Result<void> OpenWindowsSearch(platform::IKeyboardInjector& injector) {
    return WithMessage(platform::SendKeyChord(injector, {static_cast<WORD>(VK_LWIN), static_cast<WORD>('S')}),
                       "Windows Search could not be opened");
}

Result<void> OpenWindowsSearch() { return OpenWindowsSearch(platform::SystemKeyboardInjector()); }

Result<void> OpenWindowsQuickSettings(platform::IKeyboardInjector& injector, unsigned windowsBuild) {
    if (windowsBuild < 22000)
        return MakeError(ErrorCode::Unsupported, "Windows Quick Settings requires Windows 11");
    return WithMessage(platform::SendKeyChord(injector, {static_cast<WORD>(VK_LWIN), static_cast<WORD>('A')}),
                       "Windows Quick Settings could not be requested");
}

Result<void> OpenWindowsQuickSettings() {
    return OpenWindowsQuickSettings(platform::SystemKeyboardInjector(),
        platform::IsWindows11OrGreater() ? 22000u : 0u);
}

}  // namespace aether::apps
