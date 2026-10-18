// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Pure identity rule of the window tracker: which application a top-level window belongs to, given
// facts gathered from the window and its process. Kept free of Win32 calls (except the best-effort
// short-name expansion inside MakeIdForExecutable) so every branch can be unit tested.
//
// Order of preference:
//   1. the window's own AppUserModelID when it is packaged or explicit (PWAs, browser profiles,
//      apps that call SetCurrentProcessExplicitAppUserModelID or set it per window);
//   2. the packaged AUMID of the identity process (GetApplicationUserModelId);
//   3. the identity process's executable ("exe:...");
//   4. "window:<class>" when the process could not be opened (protected processes).
// For ApplicationFrameHost frames (class ApplicationFrameWindow) the identity process is the process
// of the hosted Windows.UI.Core.CoreWindow. Windows detaches that CoreWindow while the app is minimized
// or suspended; the identity remembered from the last attached observation is then reused, so
// minimizing a UWP app keeps its process id and executable instead of ApplicationFrameHost's.
#pragma once

#include <windows.h>

#include <optional>
#include <string>

namespace aether::apps {

inline constexpr wchar_t kApplicationFrameWindowClass[] = L"ApplicationFrameWindow";

struct WindowIdentity {
    std::wstring id;              // "aumid:...", "exe:..." or "window:..." (see ApplicationIdentity.h)
    std::wstring appUserModelId;  // empty when the identity is not AUMID-based
    std::wstring executablePath;  // empty when unknown
    DWORD processId = 0;          // the hosted app's process for attached frames
};

struct WindowIdentityFacts {
    std::wstring className;
    DWORD windowProcessId = 0;  // GetWindowThreadProcessId of the window itself
    std::wstring windowAumid;   // SHGetPropertyStoreForWindow(PKEY_AppUserModel_ID), may be synthesized
    // Process of the frame's hosted CoreWindow; 0 when the window is not a frame or the CoreWindow is
    // detached. Ignored unless className is ApplicationFrameWindow.
    DWORD hostedProcessId = 0;
    // Facts about the identity process: the hosted process for an attached frame, otherwise the
    // window's own process.
    bool processOpened = false;
    std::wstring processAumid;       // packaged processes only
    std::wstring processExecutable;  // QueryFullProcessImageNameW
    // Identity last computed for this frame while its CoreWindow was attached (frames only).
    std::optional<WindowIdentity> rememberedFrameIdentity;
};

[[nodiscard]] bool IsApplicationFrameClass(const std::wstring& className) noexcept;

// The window's identity. `id` is empty only when even the window class is unusable (empty class
// name); such windows cannot be grouped and are skipped by the tracker.
[[nodiscard]] WindowIdentity ResolveWindowIdentity(const WindowIdentityFacts& facts);

// Whether the tracker should remember `identity` for the frame described by `facts`: only identities
// observed while the CoreWindow is attached, or a first AUMID-based identity when nothing is known yet.
[[nodiscard]] bool ShouldRememberFrameIdentity(const WindowIdentityFacts& facts, const WindowIdentity& identity);

}  // namespace aether::apps
