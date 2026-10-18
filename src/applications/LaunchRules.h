// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Pure decisions of the launcher, kept free of side effects so they can be unit tested: how launch
// failures are reported to the user and which window Activate() brings forward.
#pragma once

#include "applications/ApplicationTypes.h"
#include "core/Result.h"

#include <windows.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace aether::apps {

// User-facing error for a failed launch. `win32Code` is the GetLastError() value of ShellExecuteEx
// (or the Win32 code inside a FACILITY_WIN32 HRESULT); `displayName` names the application.
//   ERROR_FILE_NOT_FOUND / PATH_NOT_FOUND / ...  -> NotFound     "<name> could not be found. ..."
//   ERROR_CANCELLED (a declined UAC prompt)      -> Cancelled    "The launch of <name> was cancelled."
//   ERROR_ACCESS_DENIED                          -> AccessDenied "Windows denied access to <name>."
//   ERROR_ELEVATION_REQUIRED                     -> AccessDenied "<name> requires administrator rights."
//   ERROR_NO_ASSOCIATION, ERROR_BAD_EXE_FORMAT   -> Unsupported
//   anything else                                -> Platform     "Windows could not start <name>."
// The code is kept in Error::platformCode.
[[nodiscard]] Error MakeLaunchError(DWORD win32Code, std::wstring_view displayName);

// Same for an HRESULT (IApplicationActivationManager, shell item creation). FACILITY_WIN32 codes are
// unwrapped; E_ACCESSDENIED maps to AccessDenied; others become Platform errors with the HRESULT.
[[nodiscard]] Error MakeLaunchErrorFromHResult(HRESULT hr, std::wstring_view displayName);

// Whether a launch strategy that failed with `error` may be followed by the next strategy. Only
// "this route does not exist" failures fall through. A cancelled UAC prompt, an access denial or any
// other failure of an actual launch attempt stops: trying another route would prompt again or start
// something the user did not ask for.
[[nodiscard]] bool ShouldTryNextLaunchRoute(const Error& error) noexcept;

struct ActivationChoice {
    HWND window = nullptr;
    bool alreadyActive = false;  // the only window is already in front: nothing to do
    bool cycled = false;         // the application was in front; another of its windows was chosen
};

// Picks the window Activate() brings forward. `windows` are the application's live windows, most
// recently active first, with up-to-date `minimized` flags. `foreground` is GetForegroundWindow()
// and `foregroundRoot` its GA_ROOTOWNER ancestor (either may be null).
//  * The application owns the foreground window: with a single window there is nothing to do;
//    otherwise the least recently active window is chosen, so repeated activations cycle through
//    every window (each activation moves the chosen window to the front of the MRU order).
//  * Otherwise: the most recently active window that is not minimized; when all are minimized, the
//    most recently active one (the caller restores it).
// nullopt when `windows` is empty.
[[nodiscard]] std::optional<ActivationChoice> ChooseWindowToActivate(const std::vector<RunningWindow>& windows,
                                                                     HWND foreground, HWND foregroundRoot);

}  // namespace aether::apps
