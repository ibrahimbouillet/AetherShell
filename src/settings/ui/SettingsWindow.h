// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// The Aether Shell Settings window: a normal, resizable application window on the shared FormSurface
// glass material (ui/form) with native child controls. Control ids, consent/live-apply semantics and
// the asynchronous save contract are stable; tests and the application drive the native controls
// through Control(id) exactly as before the visual redesign.
#pragma once

#include "applications/ApplicationTypes.h"
#include "core/Dispatcher.h"
#include "core/Signal.h"
#include "graphics/Animation.h"
#include "graphics/Theme.h"
#include "settings/SettingsService.h"
#include "updates/UpdateCoordinator.h"
#include "weather/GeocodingClient.h"

#include <windows.h>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace aether::graphics {
class FrameScheduler;
}

namespace aether::settings::ui {

enum class SettingsPage { Dock, TopBar, Weather, Updates, General, About, Desktop, Appearance };

// Tone of a footer status message (drawn as a status badge; high contrast keeps the text alone).
enum class StatusTone { Neutral, Info, Success, Warning, Error, Busy };

// What a reset (or another destructive Settings action) asks before it runs.
struct SettingsConfirmation {
    std::wstring title;
    std::wstring message;
    std::wstring detail;
    std::wstring confirm;  // label of the confirming button
};

struct SettingsCallbacks {
    std::function<void()> checkForUpdates, downloadAndInstall, postpone, skip, disableNotifications;
    // Explicit consent only. Implementation performs registry work on a worker and completes on UI.
    std::function<void(bool, std::function<void(Result<void>)>)> setStartup;
    std::function<std::wstring()> hotkeyStatus;
    std::function<void()> openLogFolder, exportDiagnostics;
    std::function<std::vector<apps::ApplicationInfo>()> catalog;
    weather::GeocodingClient* geocoding = nullptr;
    std::function<void()> importTaskbarPins;
    std::function<std::wstring()> taskbarStatus;
    std::function<void()> requestWindowsLocation, openLocationSettings;
    std::function<std::wstring()> weatherLocationStatus;
    // Optional display-refresh pacing for hover/page/indicator motion; null uses a short timer.
    graphics::FrameScheduler* scheduler = nullptr;
    // Confirmation seam: returns true to proceed. Null shows the shared glass confirmation dialog.
    std::function<bool(const SettingsConfirmation&)> confirm;
};

class SettingsWindow {
public:
    SettingsWindow(SettingsService& service, IUiDispatcher& dispatcher, SettingsCallbacks callbacks = {});
    ~SettingsWindow();
    // Navigation retains each page's edits/selection/scroll; showing the current page keeps its
    // existing controls. Close discards un-applied drafts, including startup/taskbar consent.
    Result<void> Show(SettingsPage page = SettingsPage::Dock);
    // Drain pending editor changes; ready runs on UI after any consent transaction settles.
    // The application queues SettingsService::FlushAsync from ready before terminating the loop.
    Result<void> PrepareToClose(std::function<void()> ready);
    void Close();
    bool PreTranslate(MSG& message);
    [[nodiscard]] HWND Handle() const noexcept;
    void SetUpdateSnapshot(updates::UpdateSnapshot snapshot);
    // UI-thread status refreshes preserve uncommitted consent checkboxes and other editor drafts.
    void RefreshDesktopStatus();
    void ReportActionStatus(std::wstring_view message);
    void ReportActionStatus(std::wstring_view message, StatusTone tone);
    // Caller has already obtained explicit reset consent; useful to other native confirmation UI.
    Result<void> ResetAfterConsent();
    [[nodiscard]] HWND Control(int id) const noexcept;

    // Restyles an open (or later opened) window in place: material, colors, fonts, text scale and
    // motion. Drafts, consent checkboxes, focus, selection and scroll position are preserved; nothing
    // is committed. The application calls it from its settings/theme/system-change route.
    void SetTheme(const graphics::DesignTokens& tokens, const graphics::MotionPolicy& motion);

    // The scrolling content region (pixels), for tests and diagnostics.
    struct ContentScroll {
        int position = 0;
        int range = 0;
        int page = 0;
    };
    [[nodiscard]] ContentScroll ScrollMetrics() const noexcept;

    Signal<> Closed;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class UpdateDialog {
public:
    explicit UpdateDialog(SettingsWindow& window) : window_(window) {}
    Result<void> Show(const updates::UpdateSnapshot& snapshot) { window_.SetUpdateSnapshot(snapshot); return window_.Show(SettingsPage::Updates); }
    void Update(const updates::UpdateSnapshot& snapshot) { window_.SetUpdateSnapshot(snapshot); }
    void Close() { window_.Close(); }
private:
    SettingsWindow& window_;
};

}  // namespace aether::settings::ui
