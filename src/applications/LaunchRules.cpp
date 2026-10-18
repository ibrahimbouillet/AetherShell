// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "applications/LaunchRules.h"

#include "core/Strings.h"

#include <algorithm>

namespace aether::apps {
namespace {

std::string NameForMessage(std::wstring_view displayName) {
    const auto trimmed = text::Trim(displayName);
    if (trimmed.empty()) return "The application";
    // Display names come from the shell (or from a shortcut file name): untrusted but not secret.
    return text::SanitizeForLog(text::Narrow(trimmed), 120);
}

}  // namespace

Error MakeLaunchError(DWORD win32Code, std::wstring_view displayName) {
    const std::string name = NameForMessage(displayName);
    const auto code = static_cast<std::int64_t>(win32Code);
    switch (win32Code) {
        case ERROR_FILE_NOT_FOUND:
        case ERROR_PATH_NOT_FOUND:
        case ERROR_MOD_NOT_FOUND:
        case ERROR_NOT_FOUND:
        case ERROR_INVALID_DRIVE:
        case ERROR_BAD_NETPATH:
        case ERROR_DEV_NOT_EXIST:
            return MakeError(ErrorCode::NotFound, name + " could not be found. It may have been moved or uninstalled.",
                             code);
        case ERROR_CANCELLED:
            return MakeError(ErrorCode::Cancelled, "The launch of " + name + " was cancelled.", code);
        case ERROR_ACCESS_DENIED:
        case ERROR_SHARING_VIOLATION:
            return MakeError(ErrorCode::AccessDenied, "Windows denied access to " + name + ".", code);
        case ERROR_ELEVATION_REQUIRED:
            return MakeError(ErrorCode::AccessDenied, name + " requires administrator rights.", code);
        case ERROR_NO_ASSOCIATION:
            return MakeError(ErrorCode::Unsupported, "Windows does not know how to open " + name + ".", code);
        case ERROR_BAD_EXE_FORMAT:
        case ERROR_EXE_MACHINE_TYPE_MISMATCH:
            return MakeError(ErrorCode::Unsupported, name + " cannot run on this PC.", code);
        default:
            return MakeError(ErrorCode::Platform, "Windows could not start " + name + ".", code);
    }
}

Error MakeLaunchErrorFromHResult(HRESULT hr, std::wstring_view displayName) {
    if (HRESULT_FACILITY(hr) == FACILITY_WIN32) {
        return MakeLaunchError(static_cast<DWORD>(HRESULT_CODE(hr)), displayName);
    }
    const std::string name = NameForMessage(displayName);
    const auto code = static_cast<std::int64_t>(static_cast<std::uint32_t>(hr));
    if (hr == E_ACCESSDENIED) {
        return MakeError(ErrorCode::AccessDenied, "Windows denied access to " + name + ".", code);
    }
    return MakeError(ErrorCode::Platform, "Windows could not start " + name + ".", code);
}

bool ShouldTryNextLaunchRoute(const Error& error) noexcept {
    return error.code == ErrorCode::NotFound || error.code == ErrorCode::InvalidArgument;
}

std::optional<ActivationChoice> ChooseWindowToActivate(const std::vector<RunningWindow>& windows, HWND foreground,
                                                       HWND foregroundRoot) {
    if (windows.empty()) return std::nullopt;
    const auto isFront = [&](const RunningWindow& window) {
        return window.handle != nullptr &&
               ((foreground != nullptr && window.handle == foreground) ||
                (foregroundRoot != nullptr && window.handle == foregroundRoot));
    };
    const auto front = std::find_if(windows.begin(), windows.end(), isFront);
    if (front != windows.end()) {
        if (windows.size() == 1) return ActivationChoice{front->handle, true, false};
        // Least recently active window that is not the one in front.
        for (auto it = windows.rbegin(); it != windows.rend(); ++it) {
            if (it->handle != nullptr && it->handle != front->handle) return ActivationChoice{it->handle, false, true};
        }
        return ActivationChoice{front->handle, true, false};
    }
    const auto visible = std::find_if(windows.begin(), windows.end(),
                                      [](const RunningWindow& window) { return window.handle && !window.minimized; });
    if (visible != windows.end()) return ActivationChoice{visible->handle, false, false};
    const auto any = std::find_if(windows.begin(), windows.end(),
                                  [](const RunningWindow& window) { return window.handle != nullptr; });
    if (any == windows.end()) return std::nullopt;
    return ActivationChoice{any->handle, false, false};
}

}  // namespace aether::apps
