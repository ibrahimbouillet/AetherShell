// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#pragma once

#include "applications/ApplicationTypes.h"
#include "core/Dispatcher.h"
#include "core/Signal.h"
#include "settings/SettingsService.h"
#include "updates/UpdateCoordinator.h"
#include "weather/GeocodingClient.h"

#include <windows.h>
#include <functional>
#include <memory>

namespace aether::settings::ui {

enum class SettingsPage { Dock, TopBar, Weather, Updates, General, About };
struct SettingsCallbacks {
    std::function<void()> checkForUpdates, downloadAndInstall, postpone, skip, disableNotifications;
    // Explicit consent only. Implementation performs registry work on a worker and completes on UI.
    std::function<void(bool, std::function<void(Result<void>)>)> setStartup;
    std::function<std::wstring()> hotkeyStatus;
    std::function<void()> openLogFolder, exportDiagnostics;
    std::function<std::vector<apps::ApplicationInfo>()> catalog;
    weather::GeocodingClient* geocoding = nullptr;
};

class SettingsWindow {
public:
    SettingsWindow(SettingsService& service, IUiDispatcher& dispatcher, SettingsCallbacks callbacks = {});
    ~SettingsWindow();
    Result<void> Show(SettingsPage page = SettingsPage::Dock);
    // Drain pending editor changes; ready runs on UI after any consent transaction settles.
    // The application queues SettingsService::FlushAsync from ready before terminating the loop.
    Result<void> PrepareToClose(std::function<void()> ready);
    void Close();
    bool PreTranslate(MSG& message);
    [[nodiscard]] HWND Handle() const noexcept;
    void SetUpdateSnapshot(updates::UpdateSnapshot snapshot);
    // Caller has already obtained explicit reset consent; useful to other native confirmation UI.
    Result<void> ResetAfterConsent();
    [[nodiscard]] HWND Control(int id) const noexcept;
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
