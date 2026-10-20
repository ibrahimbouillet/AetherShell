// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Windows shell surfaces that the Dock opens on the user's behalf: Start, Task View and Windows
// Search. Call these only in direct response to a user's click or key press, on the UI thread.
//
//  * Start: WM_SYSCOMMAND / SC_TASKLIST, documented as "Activates the Start menu", handled by
//    DefWindowProcW for a window of the calling thread (no synthesized input). The Windows key
//    (VK_LWIN) through platform::SendKeyChord is the fallback route: used when the route asks for it
//    or when no usable window of the calling thread is given.
//  * Task View (Win+Tab) and Windows Search (Win+S): Windows documents these only as keyboard
//    shortcuts, so they are sent as one key chord through platform::SendKeyChord (key-up recovery,
//    see InputInjection.h). Keys the user is still holding combine with the chord.
//
// UIPI: synthesized input is silently dropped while an elevated window is in the foreground (see
// InputInjection.h); that case cannot be detected. Tests pass a fake injector and never inject input.
#pragma once

#include "core/Result.h"
#include "platform/windows/InputInjection.h"

#include <windows.h>

namespace aether::apps {

enum class StartMenuRoute {
    SystemCommand,  // WM_SYSCOMMAND / SC_TASKLIST through DefWindowProcW on the caller's window
    WindowsKey,     // VK_LWIN key press through the injector
};

// Opens (or, when it is open, lets Windows close) the Start menu. With StartMenuRoute::SystemCommand
// and a window of the calling thread, DefWindowProcW handles SC_TASKLIST; Windows reports no outcome
// for it. Otherwise (null, destroyed or foreign window, or StartMenuRoute::WindowsKey) the Windows key
// is sent through `injector`; Platform when Windows inserted none of it.
[[nodiscard]] Result<void> OpenStartMenu(HWND ownUiThreadWindow, platform::IKeyboardInjector& injector,
                                         StartMenuRoute route = StartMenuRoute::SystemCommand);
[[nodiscard]] Result<void> OpenStartMenu(HWND ownUiThreadWindow,
                                         StartMenuRoute route = StartMenuRoute::SystemCommand);

// Win+Tab.
[[nodiscard]] Result<void> OpenTaskView(platform::IKeyboardInjector& injector);
[[nodiscard]] Result<void> OpenTaskView();

// Win+S.
[[nodiscard]] Result<void> OpenWindowsSearch(platform::IKeyboardInjector& injector);
[[nodiscard]] Result<void> OpenWindowsSearch();

}  // namespace aether::apps
