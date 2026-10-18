// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Application identity, discovery, launching, running-state tracking and icon
// loading contracts shared by the Dock, Search and Settings modules.
#pragma once

#include "core/Cancellation.h"
#include "core/Result.h"

#include <windows.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace aether::apps {

enum class ApplicationKind {
    Desktop,   // classic Win32 application (Start-menu shortcut or executable)
    Packaged,  // MSIX/UWP application identified by an AppUserModelID
    Other,     // other launchable shell item exposed by the Apps folder
};

// Identity rules (see ApplicationIdentity.h for the helpers):
//   * Packaged or explicit AUMID:  "aumid:<AppUserModelID>" (case-insensitive)
//   * Desktop application:         "exe:<lower-case normalized full path of the executable>"
//   * Fallback (no exe, no AUMID): "shell:<parsing name>"
struct ApplicationInfo {
    std::wstring id;
    std::wstring displayName;
    ApplicationKind kind = ApplicationKind::Desktop;
    std::wstring parsingName;     // shell:AppsFolder parsing name (launchable through the shell)
    std::wstring appUserModelId;  // when known
    std::wstring executablePath;  // resolved target executable when known
    std::wstring shortcutPath;    // .lnk the entry was discovered from, when any
    std::wstring arguments;       // only from a trusted shortcut definition; never user-typed text
    std::wstring keywords;        // extra searchable text (publisher, description), may be empty
};

struct RunningWindow {
    HWND handle = nullptr;
    std::wstring title;
    bool minimized = false;
};

struct RunningApplication {
    std::wstring id;  // same identity space as ApplicationInfo::id
    std::wstring displayName;
    std::wstring executablePath;
    std::wstring appUserModelId;
    std::uint32_t processId = 0;
    std::vector<RunningWindow> windows;  // most recently active first
    bool foreground = false;             // owns the foreground window
};

// Enumerates installed applications. Blocking; call from a worker thread.
class IApplicationCatalog {
public:
    virtual ~IApplicationCatalog() = default;
    virtual Result<std::vector<ApplicationInfo>> DiscoverApplications(const CancellationToken& cancel) = 0;
};

// Launches applications and activates their windows.
class IApplicationLauncher {
public:
    virtual ~IApplicationLauncher() = default;
    // Asynchronous: the launch runs on a worker thread; `completion` runs on the UI thread.
    virtual void LaunchAsync(const ApplicationInfo& app, std::function<void(Result<void>)> completion) = 0;
    // UI thread. Brings the application's most recent window forward (restoring it when minimized);
    // when it is already in front, cycles to its next window. Respects foreground-lock rules: when
    // Windows refuses activation the taskbar button is flashed and an error is returned.
    virtual Result<void> Activate(const RunningApplication& app) = 0;
};

// Observes top-level application windows. UI thread only (uses WinEvent hooks).
class IRunningApplicationTracker {
public:
    using Listener = std::function<void(const std::vector<RunningApplication>&)>;
    virtual ~IRunningApplicationTracker() = default;
    virtual Result<void> Start(Listener listener) = 0;  // listener runs on the UI thread
    virtual void Stop() noexcept = 0;
    [[nodiscard]] virtual std::vector<RunningApplication> Snapshot() const = 0;
};

// Premultiplied 32-bit BGRA pixels, top-down rows, stride = width * 4.
struct IconImage {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> pixels;
};

struct IconRequest {
    std::wstring key;  // cache key chosen by the caller (normally ApplicationInfo::id)
    ApplicationInfo app;
    std::uint32_t pixelSize = 64;
};

// Extracts application icons off the UI thread.
class IIconProvider {
public:
    using Callback = std::function<void(const std::wstring& key, std::shared_ptr<const IconImage> image)>;
    virtual ~IIconProvider() = default;
    // `callback` runs on the UI thread. `image` is null when no icon could be extracted; callers then
    // draw the neutral fallback icon.
    virtual void RequestIcon(IconRequest request, Callback callback) = 0;
    virtual void CancelAll() noexcept = 0;
};

}  // namespace aether::apps
