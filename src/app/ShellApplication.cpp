// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "app/ShellApplication.h"
#include "app/DesktopPolicy.h"
#include "app/TaskbarController.h"
#include "app/TaskbarRecovery.h"

#include "applications/ShellApplicationCatalog.h"
#include "applications/ShellApplicationLauncher.h"
#include "applications/ShellIconProvider.h"
#include "applications/WindowTracker.h"
#include "applications/TaskbarPins.h"
#include "applications/ApplicationIdentity.h"
#include "applications/KnownApplications.h"
#include "applications/SystemShellActions.h"
#include "applications/TaskbarPinSync.h"
#include "core/Strings.h"
#include "core/FileSystem.h"
#include "core/Time.h"
#include "core/WorkerPool.h"
#include "diagnostics/Logger.h"
#include "dock/DockWindow.h"
#include "graphics/FrameScheduler.h"
#include "graphics/GraphicsDevice.h"
#include "graphics/SystemPalette.h"
#include "install/Install.h"
#include "platform/net/WinHttpClient.h"
#include "platform/windows/MessageLoop.h"
#include "platform/windows/OsVersion.h"
#include "platform/windows/SingleInstance.h"
#include "platform/windows/SystemEvents.h"
#include "platform/windows/Win32UiDispatcher.h"
#include "platform/windows/WindowUtilities.h"
#include "platform/windows/InputInjection.h"
#include "search/SearchService.h"
#include "search/ui/SearchWindow.h"
#include "settings/SettingsService.h"
#include "settings/StartupRegistration.h"
#include "settings/ui/SettingsWindow.h"
#include "status/StatusProviders.h"
#include "status/WinRtSupport.h"
#include "status/audio/AudioProvider.h"
#include "status/audio/MicrophoneProvider.h"
#include "topbar/TopBarWindow.h"
#include "updates/UpdateCoordinator.h"
#include "weather/GeocodingClient.h"
#include "weather/WeatherProvider.h"

#include <shellapi.h>
#include <shobjidl.h>
#include <future>
#include <utility>
#include <mutex>
#include <algorithm>

namespace aether::app {
namespace {
constexpr UINT_PTR kUpdaterPollTimer = 0xA371;
constexpr UINT_PTR kTaskbarPollTimer = 0xA372;
constexpr UINT_PTR kTaskbarResumeTimer = 0xA373;
struct UpdateLaunchResult {
    std::mutex mutex;
    bool finished = false;
    DWORD error = 0;
    platform::UniqueHandle child;
};
}

struct ShellApplication::Impl {
    platform::MessageLoop loop;
    platform::Win32UiDispatcher dispatcher;
    platform::SystemEvents events;
    platform::InstanceChannel channel;
    graphics::GraphicsDevice graphics;
    graphics::FrameScheduler scheduler;
    SystemTimeSource time;
    WorkerPool worker{{1, L"Aether application", ComApartment::SingleThreaded}};
    CancellationSource cancellation;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
    std::vector<Connection> connections;
    std::unique_ptr<settings::SettingsService> settings;
    std::unique_ptr<net::WinHttpClient> http;
    std::unique_ptr<apps::ShellApplicationCatalog> catalog;
    std::unique_ptr<apps::ShellApplicationLauncher> launcher;
    std::unique_ptr<apps::ShellIconProvider> icons;
    std::unique_ptr<apps::WindowTracker> tracker;
    std::unique_ptr<search::SearchService> search;
    std::unique_ptr<search::ui::SearchWindow> searchWindow;
    std::unique_ptr<settings::ui::SettingsWindow> settingsWindow;
    std::unique_ptr<status::StatusProviders> providers;
    std::unique_ptr<status::audio::AudioProvider> audio;
    std::unique_ptr<status::audio::MicrophoneProvider> microphone;
    std::unique_ptr<weather::WeatherProvider> weather;
    std::unique_ptr<weather::GeocodingClient> geocoding;
    std::unique_ptr<updates::UpdateCoordinator> updates;
    std::unique_ptr<dock::DockWindow> dock;
    std::unique_ptr<topbar::TopBarWindow> topbar;
    std::unique_ptr<TaskbarController> taskbar;
    std::vector<apps::ApplicationInfo> applications;
    platform::AppPaths paths;
    CommandLine command;
    std::wstring instanceName = L"AetherShell";
    std::wstring hotkeyStatus;
    std::wstring taskbarStatus = L"Windows taskbar visible";
    std::optional<bool> taskbarRequested;
    bool nativeTaskbarAccess = false;  // see ShellApplication::Run and MayAccessNativeTaskbar
    bool catalogReady = false;
    bool importingPins = false;
    bool manualImportQueued = false;
    std::uint64_t settingsRevision = 0;
    std::optional<bool> automaticPinSync;
    TaskbarUpdate desktopState;
    bool automaticResume = false;
    bool resumeTimerArmed = false;
    std::uint64_t resumeDeadline = 0;
    bool fullscreenSuppressed = false;
    bool taskbarReservationFailed = false;
    bool closing = false;
    bool ready = false;
    bool stopped = false;
    bool updateHandoffPending = false;
    platform::UniqueHandle updaterProcess;
    std::shared_ptr<UpdateLaunchResult> updateLaunch;

    Result<void> StartSurfaces();
    void ApplySettings();
    void ImportTaskbarPins(bool manual = false, unsigned retry = 0);
    void OpenWindowsSettings(const wchar_t* uri) {
        if (closing) return;
        const auto weak = std::weak_ptr<bool>(alive);
        worker.Post([this, weak, uri] {
            const auto result = reinterpret_cast<INT_PTR>(::ShellExecuteW(nullptr, L"open", uri, nullptr, nullptr, SW_SHOWNORMAL));
            if (result <= 32) dispatcher.Post([this, weak] {
                if (const auto live = weak.lock(); live && *live && settingsWindow) settingsWindow->ReportActionStatus(L"Windows Settings could not be opened");
            });
        });
    }
    void FailTaskbarReservation() {
        taskbarReservationFailed = true;
        taskbarStatus = L"Dock space could not be reserved; restoring Windows taskbar";
        AETHER_LOG_WARN("app", "taskbar replacement: Dock space could not be reserved");
        if (dock) (void)dock->SetTaskbarReplacement(false);
        if (taskbar) (void)taskbar->Request(false);
        if (settingsWindow) settingsWindow->RefreshDesktopStatus();
    }
    // Activation order (D19): the Dock reserves its replacement-mode space and the observation timer
    // exists before the helper may hide the Windows taskbar. A fresh request resets the retry budget.
    void EnsureTaskbarReplacement() {
        if (!taskbar) return;
        auto reserved = dock ? dock->SetTaskbarReplacement(true)
                             : Result<void>(MakeError(ErrorCode::Unavailable, "Dock unavailable"));
        if (!reserved) {
            AETHER_LOG_WARN("app", "taskbar replacement: Dock reservation failed ({})", reserved.error().Describe());
            FailTaskbarReservation();
            return;
        }
        // Only active while replacement is requested. Native process checks run on its worker.
        if (!::SetTimer(events.Handle(), kTaskbarPollTimer, 1000, nullptr)) {
            if (dock) (void)dock->SetTaskbarReplacement(false);
            taskbarStatus = L"Replacement unavailable: recovery observer could not start";
            if (settingsWindow) settingsWindow->RefreshDesktopStatus();
            return;
        }
        (void)taskbar->Request(true);
    }
    void OnTaskbarUpdate(const TaskbarUpdate& update) {
        desktopState = update;
        if (dock) (void)dock->SetSuppressed(ShouldSuppressDock(fullscreenSuppressed, update));
        AETHER_LOG_INFO("app", "taskbar replacement {}{}: {}", update.active ? "active" : "inactive",
                        update.suspended ? " (suspended)" : "", text::Narrow(update.status));
        taskbarStatus = update.status;
        // Keep the preflight reservation throughout activation/retries and restoration; release it
        // when nothing more is scheduled (the Windows taskbar is back) or while suspended. A failed
        // reservation must never be re-enabled by a restoring callback.
        const bool wanted = settings && settings->Current().general.replaceWindowsTaskbar;
        const bool reserve = ShouldForceDockReservation(taskbarReservationFailed, wanted, update);
        if (dock && !dock->SetTaskbarReplacement(reserve) && reserve) { FailTaskbarReservation(); return; }
        if (!update.active && taskbarReservationFailed) taskbarStatus = L"Windows taskbar restored. Dock space could not be reserved; turn replacement off and on to retry.";
        if (!ShouldObserveTaskbar(update)) {
            // Nothing more happens until the next request (setting toggled, Explorer restarted,
            // resumed); each of them arms the timer again through EnsureTaskbarReplacement or the
            // next active update.
            ::KillTimer(events.Handle(), kTaskbarPollTimer);
        } else if (update.active && !::SetTimer(events.Handle(), kTaskbarPollTimer, 1000, nullptr)) {
            taskbarStatus = L"Recovery observation is unavailable; restoring Windows taskbar";
            (void)taskbar->Request(false);
        }
        if (settingsWindow) settingsWindow->RefreshDesktopStatus();
        if (topbar) (void)topbar->SetTaskbarReplacementState(nativeTaskbarAccess && wanted && !closing, update.active, update.suspended);
        if (automaticResume && update.suspended && !update.active && !resumeTimerArmed) {
            resumeDeadline = ::GetTickCount64() + 60000;
            resumeTimerArmed = ::SetTimer(events.Handle(), kTaskbarResumeTimer, 60000, nullptr) != 0;
            if (!resumeTimerArmed) automaticResume = false; // leave Windows visible; manual Resume remains available
        } else if (!update.suspended || !wanted) {
            ::KillTimer(events.Handle(), kTaskbarResumeTimer); resumeTimerArmed = false; automaticResume = false;
        }
    }
    void OnExplorerRestarted() {
        if (topbar) topbar->OnExplorerRestarted();
        if (nativeTaskbarAccess && catalogReady && settings && settings->Current().general.replaceWindowsTaskbar && settings->Current().general.syncTaskbarPins) ImportTaskbarPins();
        const bool wanted = taskbar && taskbarRequested.value_or(false) && !closing;
        if (dock && !dock->OnExplorerRestarted() && wanted && !taskbarReservationFailed) { FailTaskbarReservation(); return; }
        if (!wanted) return;
        if (taskbarReservationFailed) {
            // A new Explorer is a fresh chance for a reservation that failed earlier, for example
            // while Explorer was not ready at sign-in. Bounded by Explorer restarts.
            AETHER_LOG_INFO("app", "taskbar replacement: retrying after Explorer restarted");
            taskbarReservationFailed = false;
            EnsureTaskbarReplacement();
            return;
        }
        if (taskbar->Suspended()) return;  // the Windows taskbar stays shown until Resume
        // Reserve before the replaced session can hide the taskbar again (also after the controller
        // gave up), then replace the session at once through Restart, which times its own quick
        // retries while the new Explorer finishes starting.
        if (dock && !dock->SetTaskbarReplacement(true)) { FailTaskbarReservation(); return; }
        (void)taskbar->Restart();
    }
    // Runtime-only temporary reveal of the Windows taskbar (never written to settings). Suspending
    // goes through the controller's normal restoration path; resuming reserves the Dock space first,
    // as activation does. Entry point for the Top Bar notification-area button (app integration).
    Result<void> SetTaskbarReplacementSuspended(bool suspend) {
        if (!taskbar || !taskbarRequested.value_or(false) || closing)
            return MakeError(ErrorCode::InvalidState, "taskbar replacement is not requested");
        if (suspend) return taskbar->Suspend();
        if (!taskbar->Suspended()) return Ok();
        if (taskbarReservationFailed) return MakeError(ErrorCode::Unavailable, "Dock space could not be reserved");
        if (dock && !dock->SetTaskbarReplacement(true)) {
            FailTaskbarReservation();
            return MakeError(ErrorCode::Unavailable, "Dock space could not be reserved");
        }
        if (!::SetTimer(events.Handle(), kTaskbarPollTimer, 1000, nullptr)) {
            if (dock) (void)dock->SetTaskbarReplacement(false);
            return MakeError(ErrorCode::Unavailable, "taskbar recovery observer could not resume");
        }
        return taskbar->Resume();
    }
    void RequestQuit();
    void OpenSettings(settings::ui::SettingsPage page = settings::ui::SettingsPage::Dock) {
        if (!closing && !updateHandoffPending && settingsWindow) (void)settingsWindow->Show(page);
    }
    void CheckUpdates() { if (closing) return; OpenSettings(settings::ui::SettingsPage::Updates); if (updates) (void)updates->CheckNow(); }
    void InstallUpdate();
    void ObserveUpdateHandoff() {
        if (!updateHandoffPending) return;
        DWORD error = 0;
        if (!updaterProcess && updateLaunch) {
            std::lock_guard lock(updateLaunch->mutex);
            if (!updateLaunch->finished) return;
            error = updateLaunch->error;
            updaterProcess = std::move(updateLaunch->child);
        }
        if (error) {
            FinishUpdateHandoff();
            auto snapshot = updates->Snapshot(); snapshot.state = updates::UpdateState::Error;
            snapshot.failure = updates::UpdateFailure{MakeError(ErrorCode::Platform, "updater could not start", error), true, {}};
            if (settingsWindow) settingsWindow->SetUpdateSnapshot(std::move(snapshot));
            OpenSettings(settings::ui::SettingsPage::Updates);
        } else if (updaterProcess && ::WaitForSingleObject(updaterProcess.Get(), 0) != WAIT_TIMEOUT) FinishUpdateHandoff();
    }
    void FinishUpdateHandoff() {
        ::KillTimer(events.Handle(), kUpdaterPollTimer);
        updaterProcess.Reset(); updateLaunch.reset(); updateHandoffPending = false;
        if (settingsWindow && settingsWindow->Handle()) {
            ::SetWindowTextW(settingsWindow->Handle(), L"Aether Shell Settings");
            if (!closing) ::EnableWindow(settingsWindow->Handle(), TRUE);
        }
    }
    void RefreshUpdateOffer(const updates::UpdateSnapshot& snapshot) {
        const bool available = snapshot.state == updates::UpdateState::UpdateAvailable || snapshot.state == updates::UpdateState::ReadyToInstall;
        if (topbar) (void)topbar->SetUpdateAvailable(settings && settings->Current().updates.notifications && available);
    }
    void ExportDiagnostics();
    void Stop() noexcept;
};

void ShellApplication::Impl::ApplySettings() {
    if (!settings || !settings->Loaded()) return;
    const auto& value = settings->Current();
    graphics::AppearanceOptions appearance;
    appearance.theme = static_cast<graphics::ThemePreference>(value.general.themeMode);
    appearance.accent = static_cast<graphics::AccentSource>(value.general.accentSource);
    const auto tokens = graphics::MakeShellDesignTokens(appearance);
    graphics::MotionPolicy motion;
    motion.reduced = tokens.mode == graphics::ThemeMode::HighContrast || value.general.motion == settings::MotionPreference::Reduced ||
        (value.general.motion == settings::MotionPreference::System && !platform::SystemAnimationsEnabled());
    motion.durationScale = static_cast<float>(value.general.animationSpeed);
    if (dock) {
        if (auto themed = dock->SetTheme(tokens, motion, value.dock); !themed && taskbarRequested.value_or(false)) {
            AETHER_LOG_WARN("app", "taskbar replacement: Dock relayout failed ({})", themed.error().Describe());
            FailTaskbarReservation();
        }
    }
    if (topbar) { topbar->SetTheme(tokens, motion); topbar->SetSettings(value.topBar, value.weather.enabled); }
    if (searchWindow) searchWindow->SetTheme(tokens, motion);
    if (taskbar && taskbarRequested != value.general.replaceWindowsTaskbar) {
        taskbarRequested = value.general.replaceWindowsTaskbar;
        taskbarReservationFailed = false;
        AETHER_LOG_INFO("app", "taskbar replacement {}", *taskbarRequested ? "requested" : "turned off");
        if (*taskbarRequested) {
            EnsureTaskbarReplacement();
        } else {
            ::KillTimer(events.Handle(), kTaskbarResumeTimer); automaticResume = false; resumeTimerArmed = false;
            // Keep observing until restoration acknowledges success, including bounded retries.
            (void)taskbar->Request(false);
        }
    }
    if (topbar) (void)topbar->SetTaskbarReplacementState(nativeTaskbarAccess && value.general.replaceWindowsTaskbar && !closing, desktopState.active, desktopState.suspended);
    const bool sync = value.general.replaceWindowsTaskbar && value.general.syncTaskbarPins;
    const bool syncJustEnabled = sync && automaticPinSync != sync;
    automaticPinSync = sync;
    if (nativeTaskbarAccess && catalogReady && value.general.replaceWindowsTaskbar &&
        (syncJustEnabled || !value.general.taskbarPinsImported || !value.dock.explorerDefaultApplied)) ImportTaskbarPins();
    if (providers && providers->clock) providers->clock->SetResolution(value.topBar.showSeconds ? status::ClockResolution::Seconds : status::ClockResolution::Minutes);
    if (weather) {
        weather::WeatherConfig config; config.enabled = value.weather.enabled; config.latitude = value.weather.latitude;
        config.longitude = value.weather.longitude; config.locationName = value.weather.locationName;
        config.unit = value.weather.unit == settings::TemperatureUnit::Celsius ? status::TemperatureUnit::Celsius : status::TemperatureUnit::Fahrenheit;
        config.refreshInterval = std::chrono::minutes(value.weather.refreshMinutes);
        (void)weather->UpdateConfig(std::move(config));
    }
    ::UnregisterHotKey(events.Handle(), 1); ::UnregisterHotKey(events.Handle(), 2);
    hotkeyStatus = L"Keyboard shortcuts disabled";
    if (value.general.hotkeysEnabled && !closing) {
        const auto& searchKey = value.general.searchHotkey; const auto& dockKey = value.general.focusDockHotkey;
        const bool first = ::RegisterHotKey(events.Handle(), 1, searchKey.modifiers | MOD_NOREPEAT, searchKey.virtualKey) != FALSE;
        const bool second = ::RegisterHotKey(events.Handle(), 2, dockKey.modifiers | MOD_NOREPEAT, dockKey.virtualKey) != FALSE;
        hotkeyStatus = first && second ? L"Both keyboard shortcuts registered" : L"A keyboard shortcut is already in use by another application";
    }
    if (updates) {
        updates::UpdatePolicy policy; policy.enabled = value.updates.automaticChecks;
        policy.interval = std::chrono::hours(value.updates.checkIntervalHours);
        policy.channel = value.updates.channel == settings::UpdateChannel::Stable ? updates::UpdateChannel::Stable : updates::UpdateChannel::Prerelease;
        policy.replaceSkippedVersion = true;
        if (!value.updates.skippedVersion.empty()) { auto skipped = updates::SemanticVersion::Parse(value.updates.skippedVersion); if (skipped) policy.skippedVersion = *skipped; }
        (void)updates->StartAutomaticChecks(policy);
        RefreshUpdateOffer(updates->Snapshot());
    }
}

Result<void> ShellApplication::Impl::StartSurfaces() {
    auto client = net::WinHttpClient::Create();
    if (!client) return client.error(); http = std::move(*client);
    launcher = std::make_unique<apps::ShellApplicationLauncher>(dispatcher);
    icons = std::make_unique<apps::ShellIconProvider>(dispatcher);
    catalog = std::make_unique<apps::ShellApplicationCatalog>();
    tracker = std::make_unique<apps::WindowTracker>(dispatcher);
    search = std::make_unique<search::SearchService>(dispatcher);
    geocoding = std::make_unique<weather::GeocodingClient>(*http);
    weather = std::make_unique<weather::WeatherProvider>(*http, time, paths.cacheDir / L"weather.json", weather::WeatherConfig{});
    auto createdStatus = status::CreateStatusProviders(); if (!createdStatus) return createdStatus.error();
    providers = std::make_unique<status::StatusProviders>(std::move(*createdStatus));
    audio = std::make_unique<status::audio::AudioProvider>();
    microphone = std::make_unique<status::audio::MicrophoneProvider>();
    updates::UpdateCoordinatorOptions options; options.configDir = paths.configDir; options.updatesDir = paths.updatesDir;
    auto update = updates::UpdateCoordinator::Create(*http, time, dispatcher, updates::EcdsaP256Verifier::ForReleaseManifests(), options);
    if (update) updates = std::move(*update); else AETHER_LOG_WARN("app", "update coordinator unavailable");

    settings::ui::SettingsCallbacks actions;
    actions.checkForUpdates = [this] { if (!closing && updates) (void)updates->CheckNow(); };
    actions.downloadAndInstall = [this] { InstallUpdate(); };
    actions.postpone = [this] { if (closing) return; if (updates) (void)updates->Postpone(); if (settingsWindow) settingsWindow->Close(); };
    actions.skip = [this] { if (!closing && updates) { const auto current = updates->Snapshot(); if (current.manifest && updates->SkipVersion(current.manifest->version)) { const auto version = current.manifest->version.ToString(); (void)settings->Update([version](settings::Settings& s){ s.updates.skippedVersion = version; }); } } };
    actions.disableNotifications = [this] { if (!closing) (void)settings->Update([](settings::Settings& s){ s.updates.notifications = false; }); };
    actions.hotkeyStatus = [this] { return hotkeyStatus; };
    actions.catalog = [this] { return applications; };
    actions.importTaskbarPins = [this] { ImportTaskbarPins(true); };
    actions.taskbarStatus = [this] { return taskbarStatus; };
    actions.geocoding = geocoding.get();
    actions.setStartup = [this](bool enabled, std::function<void(Result<void>)> completion) {
        if (closing) { if (completion) completion(MakeError(ErrorCode::InvalidState, "application is closing")); return; }
        const auto executable = paths.executableDir.parent_path().parent_path() / L"AetherShell.exe";
        const auto weak = std::weak_ptr<bool>(alive);
        worker.Post([this, weak, executable, enabled, completion = std::move(completion)]() mutable {
            auto registry = settings::MakeWindowsStartupRegistry();
            Result<void> result = registry ? (enabled ? settings::StartupRegistration(*registry).Enable(executable) : settings::StartupRegistration(*registry).Disable()) : Result<void>(registry.error());
            dispatcher.Post([weak, completion = std::move(completion), result = std::move(result)]() mutable { if (auto live = weak.lock(); live && *live && completion) completion(std::move(result)); });
        });
    };
    actions.openLogFolder = [this] { const auto folder = paths.logDir; worker.Post([folder]{ ::ShellExecuteW(nullptr,L"open",folder.c_str(),nullptr,nullptr,SW_SHOWNORMAL); }); };
    actions.exportDiagnostics = [this] { ExportDiagnostics(); };
    settingsWindow = std::make_unique<settings::ui::SettingsWindow>(*settings, dispatcher, std::move(actions));
    searchWindow = std::make_unique<search::ui::SearchWindow>(graphics, scheduler, *search, *launcher, *icons);
    if (auto result = searchWindow->Initialize(); !result) return result;
    dock = std::make_unique<dock::DockWindow>(dock::DockWindowContext{graphics, scheduler, dispatcher, *launcher, *icons, *settings});
    if (auto result = dock->Create(); !result) return result;
    topbar::TopBarContext context{graphics, scheduler, dispatcher}; context.settings = settings.get();
    context.clock = providers->clock.get(); context.battery = providers->battery.get(); context.keyboard = providers->keyboard.get();
    context.network = providers->network.get(); context.audio = audio.get(); context.microphone = microphone.get(); context.weather = weather.get();
    topbar = std::make_unique<topbar::TopBarWindow>(context);
    if (auto result = topbar->Create(); !result) return result;
    connections.push_back(dock->SearchRequested.Connect([this]{ if (!closing) searchWindow->Toggle(); }));
    connections.push_back(dock->SettingsRequested.Connect([this]{ OpenSettings(); }));
    connections.push_back(dock->ImportWindowsPinsRequested.Connect([this]{ ImportTaskbarPins(true); }));
    connections.push_back(dock->StartRequested.Connect([this]{
        if (closing) return;
        if (!apps::OpenStartMenu(dock->Handle()) && settingsWindow) settingsWindow->ReportActionStatus(L"Windows Start could not be opened");
    }));
    connections.push_back(dock->TaskViewRequested.Connect([this]{
        if (!closing && !apps::OpenTaskView() && settingsWindow) settingsWindow->ReportActionStatus(L"Windows Task View could not be opened");
    }));
    connections.push_back(topbar->SearchRequested.Connect([this]{ if (!closing) searchWindow->Toggle(); }));
    connections.push_back(topbar->SettingsRequested.Connect([this]{ OpenSettings(settings::ui::SettingsPage::TopBar); }));
    connections.push_back(topbar->AboutRequested.Connect([this]{ OpenSettings(settings::ui::SettingsPage::About); }));
    connections.push_back(topbar->CheckForUpdatesRequested.Connect([this]{ CheckUpdates(); }));
    connections.push_back(topbar->UpdatesRequested.Connect([this]{ OpenSettings(settings::ui::SettingsPage::Updates); }));
    connections.push_back(topbar->QuitRequested.Connect([this]{ RequestQuit(); }));
    connections.push_back(topbar->KeyboardRequested.Connect([this]{
        if (closing) return;
        if (!platform::SendKeyChord({VK_LWIN,VK_SPACE}) && settingsWindow) settingsWindow->ReportActionStatus(L"Windows keyboard layout switching was unavailable");
    }));
    connections.push_back(topbar->NetworkRequested.Connect([this]{ OpenWindowsSettings(L"ms-settings:network-status"); }));
    connections.push_back(topbar->WeatherRequested.Connect([this]{ OpenSettings(settings::ui::SettingsPage::Weather); }));
    connections.push_back(topbar->MicrophoneSettingsRequested.Connect([this]{ OpenWindowsSettings(L"ms-settings:privacy-microphone"); }));
    connections.push_back(topbar->VoiceTypingRequested.Connect([this]{
        if (!closing && !platform::SendKeyChord({VK_LWIN,static_cast<WORD>('H')}) && settingsWindow)
            settingsWindow->ReportActionStatus(L"Windows voice typing could not be opened");
    }));
    connections.push_back(topbar->SoundSettingsRequested.Connect([this]{ OpenWindowsSettings(L"ms-settings:sound"); }));
    connections.push_back(topbar->PowerSettingsRequested.Connect([this]{ OpenWindowsSettings(L"ms-settings:powersleep"); }));
    connections.push_back(topbar->ClockRequested.Connect([this]{
        if (!closing && !platform::SendKeyChord({VK_LWIN,VK_MENU,static_cast<WORD>('D')}) && settingsWindow) settingsWindow->ReportActionStatus(L"Windows calendar could not be opened");
    }));
    connections.push_back(topbar->RevealWindowsTaskbarRequested.Connect([this]{
        if (closing) return;
        automaticResume = true;
        if (!SetTaskbarReplacementSuspended(true)) { automaticResume = false; if(settingsWindow)settingsWindow->ReportActionStatus(L"Windows taskbar could not be revealed"); }
    }));
    connections.push_back(topbar->ResumeTaskbarReplacementRequested.Connect([this]{
        ::KillTimer(events.Handle(), kTaskbarResumeTimer); automaticResume = false; resumeTimerArmed = false;
        if (!SetTaskbarReplacementSuspended(false) && settingsWindow) settingsWindow->ReportActionStatus(L"Taskbar replacement could not resume; Windows remains available");
    }));
    connections.push_back(topbar->FullscreenChanged.Connect([this](bool hidden){ fullscreenSuppressed=hidden; (void)dock->SetSuppressed(ShouldSuppressDock(hidden, desktopState)); }));
    connections.push_back(settings->Changed().Connect([this](const settings::Settings&){ ++settingsRevision; ApplySettings(); }));
    connections.push_back(events.ThemeChanged.Connect([this]{ ApplySettings(); }));
    connections.push_back(events.DisplayChanged.Connect([this]{ if(topbar)topbar->Relayout(); if(dock && !dock->Relayout() && taskbarRequested.value_or(false))FailTaskbarReservation(); }));
    connections.push_back(events.ExplorerRestarted.Connect([this]{ OnExplorerRestarted(); }));
    const auto clockChanged = [this] { providers->clock->NotifyTimeChanged(); providers->battery->Refresh(); providers->network->Refresh(); weather->NotifyClockChanged(); if(updates)updates->NotifyClockChanged(); };
    connections.push_back(events.TimeChanged.Connect(clockChanged));
    connections.push_back(events.ResumedFromSleep.Connect(clockChanged));
    connections.push_back(events.PowerSettingChanged.Connect([this]{ providers->battery->Refresh(); }));
    connections.push_back(events.SettingChanged.Connect([this](UINT, const std::wstring& section){ if(section==L"intl"){providers->clock->NotifyTimeChanged();providers->keyboard->Refresh();} ApplySettings(); }));
    if (updates) connections.push_back(updates->Subscribe([this](const updates::UpdateSnapshot& snapshot){ if(settingsWindow)settingsWindow->SetUpdateSnapshot(snapshot); RefreshUpdateOffer(snapshot); }));
    (void)tracker->Start([this](const std::vector<apps::RunningApplication>& running){ if(dock)(void)dock->SetRunningApplications(running); });
    const auto weak = std::weak_ptr<bool>(alive); const auto cancel = cancellation.Token();
    if (nativeTaskbarAccess) {
        TaskbarController::Options taskbarOptions;
        taskbarOptions.baselineFile = platform::TaskbarBaselineFile(paths.configDir);
        taskbar = std::make_unique<TaskbarController>(dispatcher, paths.executableDir / L"AetherShell.exe",
            [this, weak](const TaskbarUpdate& update) {
                if (const auto live = weak.lock(); live && *live) OnTaskbarUpdate(update);
            }, std::move(taskbarOptions));
    } else {
        // Own-window sessions (tests): replacement requests never reach the real Windows taskbar.
        taskbarStatus = L"Windows taskbar replacement is unavailable in this session";
    }
    ApplySettings();
    dock->Show(); topbar->Show(); ready = true;
    if (!command.healthToken.empty()) { const auto token = text::Narrow(command.healthToken); worker.Post([token]{ (void)install::SignalHealthy(token); }); }
    worker.Post([this, weak, cancel] {
        auto discovered = catalog->DiscoverApplications(cancel);
        dispatcher.Post([this, weak, discovered = std::move(discovered)]() mutable {
            if (auto live = weak.lock(); live && *live) {
                catalogReady = true;
                if (discovered) { applications = std::move(*discovered); (void)search->Rebuild(applications); if(dock)(void)dock->SetApplicationCatalog(applications); }
                if (nativeTaskbarAccess && settings->Current().general.replaceWindowsTaskbar &&
                    (settings->Current().general.syncTaskbarPins || !settings->Current().general.taskbarPinsImported || !settings->Current().dock.explorerDefaultApplied)) ImportTaskbarPins();
            }
        });
    });
    if (command.action == StartupAction::OpenSearch) searchWindow->Open();
    else if (command.action == StartupAction::OpenSettings) OpenSettings();
    AETHER_LOG_INFO("app", "native Dock and Top Status Bar started");
    return Ok();
}

void ShellApplication::Impl::ImportTaskbarPins(bool manual, unsigned retry) {
    if (closing || !settings || !settings->Loaded()) return;
    const auto automaticAllowed = [&] {
        const auto& value = settings->Current();
        return ShouldAutomaticallyImportPins(value.general.replaceWindowsTaskbar,
            value.general.syncTaskbarPins, value.general.taskbarPinsImported, value.dock.explorerDefaultApplied);
    };
    if (!manual && !automaticAllowed()) return;
    if (importingPins) { manualImportQueued = manualImportQueued || manual; return; }
    if (!nativeTaskbarAccess) {
        if (manual && settingsWindow) settingsWindow->ReportActionStatus(L"Windows pin access is unavailable in this isolated session");
        return;
    }
    importingPins = true;
    if (manual) manualImportQueued = false;
    const auto weak = std::weak_ptr<bool>(alive); const auto cancel = cancellation.Token();
    const auto revision = settingsRevision;
    apps::TaskbarPinImportOptions options; options.catalog = applications;
    apps::PinSyncInput input;
    const auto& current = settings->Current();
    for (const auto& pin : current.dock.pinned)
        input.existing.push_back({pin.id,pin.appUserModelId,pin.executablePath,pin.arguments});
    input.dismissedIds = current.dock.dismissedPins;
    input.explorerDefaultApplied = current.dock.explorerDefaultApplied;
    input.ignoreDismissed = manual;
    if (!worker.Post([this, weak, cancel, manual, retry, revision, input = std::move(input), options = std::move(options)]() mutable {
        Result<apps::TaskbarPinSet> discovered = MakeError(ErrorCode::Internal, "Windows pin discovery failed");
        Result<apps::PinSyncPlan> plan = MakeError(ErrorCode::Unavailable, "Windows pins could not be read");
        try {
            discovered = apps::DiscoverTaskbarPinSet(cancel, options);
            if (discovered && !cancel.IsCancelled()) {
                input.windowsPins = discovered->pins;
                if (auto explorer = apps::MakeFileExplorerApplication(options.catalog); explorer) input.fileExplorer = std::move(*explorer);
                // Identity comparisons can resolve 8.3 paths: planning must remain on this worker.
                plan = apps::PlanTaskbarPinSync(input);
            }
        } catch (...) { plan = MakeError(ErrorCode::Internal, "Windows pin planning failed"); }
        dispatcher.Post([this, weak, manual, retry, revision, discovered = std::move(discovered), plan = std::move(plan)]() mutable {
            if (const auto live = weak.lock(); !live || !*live) return;
            importingPins = false;
            if (closing) return;
            if (manualImportQueued) { manualImportQueued = false; ImportTaskbarPins(true); return; }
            const auto& value = settings->Current();
            if (!manual && !ShouldAutomaticallyImportPins(value.general.replaceWindowsTaskbar,
                value.general.syncTaskbarPins, value.general.taskbarPinsImported, value.dock.explorerDefaultApplied)) return;
            if (!plan || !discovered) {
                if (settingsWindow) settingsWindow->ReportActionStatus(L"Windows pinned applications could not be read");
                return;
            }
            // Never apply a plan over pin edits, dismissals or other settings accepted during IO.
            // Re-discover against a fresh snapshot, with a bound while the editor remains active.
            if (settingsRevision != revision) {
                if (retry < 3) ImportTaskbarPins(manual, retry + 1);
                else if (settingsWindow) settingsWindow->ReportActionStatus(L"Settings changed during pin import. Try import again.");
                return;
            }
            for (const auto& entry : discovered->pins) {
                const auto found = std::find_if(applications.begin(), applications.end(), [&](const auto& app) { return text::EqualsIgnoreCase(app.id, entry.id); });
                if (found == applications.end()) applications.push_back(entry);
                else if (found->shortcutPath.empty() && !entry.shortcutPath.empty()) *found = entry;
            }
            if (dock) (void)dock->SetApplicationCatalog(applications);
            if (search) (void)search->Rebuild(applications);
            const auto count = plan->append.size() + (plan->explorerAtFront ? 1u : 0u);
            const auto toPin = [](const apps::ApplicationInfo& app) {
                settings::PinnedApp pin; pin.id=app.id; pin.displayName=app.displayName;
                pin.parsingName=app.parsingName; pin.appUserModelId=app.appUserModelId;
                pin.executablePath=app.executablePath; pin.shortcutPath=app.shortcutPath;
                pin.arguments=app.arguments; return pin;
            };
            importingPins = true; // Changed observers must not start another import during this commit.
            const auto changed = settings->Update([&](settings::Settings& value) {
                if (plan->explorerAtFront) value.dock.pinned.insert(value.dock.pinned.begin(), toPin(*plan->explorerAtFront));
                for (const auto& app : plan->append) value.dock.pinned.push_back(toPin(app));
                for (const auto& id : plan->reinstatedIds)
                    std::erase_if(value.dock.dismissedPins, [&](const auto& removed) { return text::EqualsIgnoreCase(id, removed); });
                if (plan->markExplorerApplied) value.dock.explorerDefaultApplied = true;
                value.general.taskbarPinsImported = true;
            }, [this, weak, count](Result<void> persisted) {
                if (const auto live = weak.lock(); live && *live && settingsWindow)
                    settingsWindow->ReportActionStatus(persisted ?
                        L"Saved " + std::to_wstring(count) + L" additional pinned applications. Some Windows pins may remain unavailable." :
                        L"Pins changed in memory; saving failed. Apply in Settings to retry.");
            });
            importingPins = false;
            if (settingsWindow) settingsWindow->ReportActionStatus(changed ? L"Pins updated in memory; saving..." : L"Imported pins could not be applied");
        });
    })) {
        importingPins = false;
        if (settingsWindow) settingsWindow->ReportActionStatus(L"Pin import is unavailable while closing");
    }
}

void ShellApplication::Impl::InstallUpdate() {
    if (closing || updateHandoffPending || !updates) return;
    const auto current = updates->Snapshot();
    if (!current.prepared) { (void)updates->DownloadAndPrepare(); return; }
    const auto prepared = *current.prepared; const auto updater = paths.executableDir / L"AetherShellUpdater.exe";
    const auto root = paths.executableDir.parent_path().parent_path();
    const auto weak = std::weak_ptr<bool>(alive);
    // Establish the UI observer before any process can launch. Shared completion makes failed
    // dispatcher delivery recoverable through this bounded, handoff-only timer.
    updateLaunch = std::make_shared<UpdateLaunchResult>();
    if (!::SetTimer(events.Handle(), kUpdaterPollTimer, 200, nullptr)) { updateLaunch.reset(); return; }
    updateHandoffPending = true;
    if (settingsWindow && settingsWindow->Handle()) {
        ::SetWindowTextW(settingsWindow->Handle(), L"Aether Shell Settings — update in progress");
        ::EnableWindow(settingsWindow->Handle(), FALSE);
    }
    if (!worker.Post([this, weak, prepared, updater, root, launch = updateLaunch]() mutable {
        std::wstring commandLine = install::QuoteArgument(updater.wstring()) + L" --apply --package " + install::QuoteArgument(prepared.packagePath.wstring()) +
            L" --manifest " + install::QuoteArgument(prepared.manifestPath.wstring()) + L" --signature " + install::QuoteArgument(prepared.signaturePath.wstring()) +
            L" --install-root " + install::QuoteArgument(root.wstring()) + L" --data-dir " + install::QuoteArgument(paths.dataRoot.wstring());
        STARTUPINFOW startup{sizeof(startup)}; PROCESS_INFORMATION process{};
        const bool launched = ::CreateProcessW(updater.c_str(), commandLine.data(), nullptr, nullptr, FALSE, 0, nullptr, root.c_str(), &startup, &process) != FALSE;
        const DWORD error = launched ? 0 : ::GetLastError();
        if (launched) {
            ::CloseHandle(process.hThread);
        }
        {
            std::lock_guard lock(launch->mutex);
            launch->child.Reset(launched ? process.hProcess : nullptr);
            launch->error = error; launch->finished = true;
        }
        dispatcher.Post([this, weak] { if (const auto live = weak.lock(); live && *live) ObserveUpdateHandoff(); });
    })) FinishUpdateHandoff();
}

void ShellApplication::Impl::RequestQuit() {
    if (closing) return; closing = true;
    ::KillTimer(events.Handle(), kTaskbarResumeTimer); automaticResume = false; resumeTimerArmed = false;
    ::UnregisterHotKey(events.Handle(), 1); ::UnregisterHotKey(events.Handle(), 2);
    if (dock) ::EnableWindow(dock->Handle(), FALSE);
    if (topbar) ::EnableWindow(topbar->Handle(), FALSE);
    if (searchWindow) searchWindow->Close();
    if (settingsWindow && settingsWindow->Handle()) ::EnableWindow(settingsWindow->Handle(), FALSE);
    const auto resume = [this] {
        if (settings) (void)settings->SetChangesEnabled(true);
        closing = false;
        if (dock) ::EnableWindow(dock->Handle(), TRUE);
        if (topbar) ::EnableWindow(topbar->Handle(), TRUE);
        if (settingsWindow && settingsWindow->Handle() && !updateHandoffPending) ::EnableWindow(settingsWindow->Handle(), TRUE);
        ApplySettings();
    };
    const auto finish = [this, resume] {
        if (settings && settings->Loaded()) {
            // Preparation has drained editor/startup work. Freeze every model writer (including
            // deferred Dock/UIA actions) before the final FIFO persistence barrier is queued.
            if (!settings->SetChangesEnabled(false)) { resume(); return; }
            const auto flushed = settings->FlushAsync([this, resume](Result<void> result) {
                if (!result && ::MessageBoxW(nullptr, L"Settings could not be saved. Quit without saving the latest changes?", L"Aether Shell", MB_YESNO | MB_DEFBUTTON2 | MB_ICONWARNING) != IDYES) { resume(); return; }
                ::PostQuitMessage(0);
            });
            if (flushed) return;
            resume(); return;
        }
        ::PostQuitMessage(0);
    };
    if (settingsWindow) {
        if (auto prepared = settingsWindow->PrepareToClose(finish); !prepared) { resume(); AETHER_LOG_WARN("app", "settings editor could not settle for shutdown"); }
    } else finish();
}

void ShellApplication::Impl::ExportDiagnostics() {
    winrt::com_ptr<IFileSaveDialog> dialog;
    if (FAILED(::CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(dialog.put())))) return;
    dialog->SetTitle(L"Export basic diagnostics — app version and Windows build, no logs or personal data");
    dialog->SetFileName(L"aethershell-diagnostics.txt"); dialog->SetDefaultExtension(L"txt");
    if (FAILED(dialog->Show(settingsWindow ? settingsWindow->Handle() : nullptr))) return;
    winrt::com_ptr<IShellItem> item; if (FAILED(dialog->GetResult(item.put()))) return;
    PWSTR raw{}; if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &raw))) return;
    const std::filesystem::path path(raw); ::CoTaskMemFree(raw);
    const auto os = platform::GetOsVersion();
    const auto report = std::string("Aether Shell ") + std::string(build::kVersion) + "\nWindows build: " + std::to_string(os.build) +
                        "\nArchitecture: " + std::string(build::kTargetArchitecture) + "\nExport scope: basic diagnostics only; logs and personal data excluded.\n";
    worker.Post([path, report]{ (void)fs::WriteFileAtomic(path, report); });
}

void ShellApplication::Impl::Stop() noexcept {
    if (stopped) return;
    stopped = true;
    *alive = false; cancellation.Cancel();
    ::KillTimer(events.Handle(), kTaskbarPollTimer);
    ::KillTimer(events.Handle(), kTaskbarResumeTimer);
    ::KillTimer(events.Handle(), kUpdaterPollTimer); updaterProcess.Reset(); updateLaunch.reset();
    ::UnregisterHotKey(events.Handle(), 1); ::UnregisterHotKey(events.Handle(), 2);
    connections.clear(); channel.Close();
    if (tracker) tracker->Stop();
    if (updates) updates->Stop();
    if (topbar) topbar.reset();
    if (providers) providers->StopAll();
    if (audio) audio->Stop(); if (microphone) microphone->Stop(); if (weather) weather->Stop();
    dock.reset(); searchWindow.reset(); settingsWindow.reset();
    // Remove our AppBars on their UI thread before joining shell restoration. Explorer can notify
    // AppBars synchronously; no owned AppBar may wait on this same shutdown join.
    if (taskbar) taskbar->Stop();
    taskbar.reset();
    worker.Shutdown();
    search.reset(); tracker.reset(); icons.reset(); launcher.reset(); catalog.reset();
    updates.reset(); geocoding.reset(); weather.reset(); audio.reset(); microphone.reset(); providers.reset(); http.reset(); settings.reset();
    events.Destroy(); scheduler.Shutdown(); graphics.Shutdown();
    (void)status::FinalizeStatusRuntime();
    dispatcher.Shutdown();
}

ShellApplication::ShellApplication() : impl_(std::make_unique<Impl>()) {}
ShellApplication::~ShellApplication() { impl_->Stop(); }
Result<int> ShellApplication::Run(CommandLine command, platform::AppPaths paths, std::wstring instanceName,
                                  bool nativeTaskbarAccess) {
    impl_->command = std::move(command); impl_->paths = std::move(paths); impl_->instanceName = std::move(instanceName);
    impl_->nativeTaskbarAccess = MayAccessNativeTaskbar(impl_->instanceName, nativeTaskbarAccess);
    auto instance = platform::SingleInstanceLock::Acquire(impl_->instanceName); if (!instance) return instance.error();
    if (!instance->IsPrimary()) {
        const auto action = impl_->command.action == StartupAction::Quit ? L"shutdown" :
            impl_->command.action == StartupAction::RestoreTaskbar ? L"restore-taskbar" :
            impl_->command.action == StartupAction::OpenSettings ? L"settings" : L"search";
        if (auto sent = platform::InstanceChannel::Send(impl_->instanceName, action); !sent) return sent.error();
        return 0;
    }
    if (impl_->command.action == StartupAction::Quit) return 0;
    if (impl_->command.action == StartupAction::RestoreTaskbar) {
        // D21: no instance (and so no guard) is running. Restore from the persisted baseline while
        // this process holds the instance lock; the blocking shell calls run on a worker. Without
        // resolved data folders nothing is touched and no UI is shown.
        if (!impl_->paths.configDir.is_absolute())
            return MakeError(ErrorCode::InvalidArgument, "taskbar recovery needs the data folder");
        if (!impl_->nativeTaskbarAccess)
            return MakeError(ErrorCode::Unavailable, "taskbar recovery is unavailable in this session");
        std::promise<Result<TaskbarRecoveryReport>> promise;
        auto future = promise.get_future();
        WorkerPool recovery{{1, L"Aether taskbar recovery", ComApartment::None}};
        if (!recovery.Post([&promise, config = impl_->paths.configDir] {
                try { promise.set_value(RecoverTaskbarWithoutInstance(config)); }
                catch (...) { promise.set_value(MakeError(ErrorCode::Internal, "taskbar recovery failed")); }
            })) return MakeError(ErrorCode::Unavailable, "taskbar recovery worker unavailable");
        auto recovered = future.get();
        recovery.Shutdown();
        if (!recovered) AETHER_LOG_ERROR("app", "taskbar recovery failed ({})", ToString(recovered.error().code));
        ::MessageBoxW(nullptr, DescribeTaskbarRecovery(recovered).c_str(), L"Aether Shell",
                      MB_OK | (recovered ? MB_ICONINFORMATION : MB_ICONERROR));
        return recovered ? 0 : 1;
    }
    struct ShutdownBeforeUnlock { Impl& app; ~ShutdownBeforeUnlock() { app.Stop(); } } shutdown{*impl_};
    if (auto result = impl_->dispatcher.Initialize(); !result) return result.error();
    if (auto result = impl_->graphics.Initialize(); !result) return result.error();
    if (auto result = impl_->scheduler.Initialize(impl_->loop); !result) return result.error();
    if (auto result = impl_->events.Create(); !result) return result.error();
    if (auto result = impl_->channel.Listen(impl_->instanceName, [this](std::wstring_view action){
        if(action==L"shutdown-for-update" || action==L"shutdown") { if(impl_->ready)impl_->RequestQuit(); else impl_->command.action=StartupAction::Quit; }
        else if(action==L"restore-taskbar" && !impl_->closing) {
            if (impl_->ready) (void)impl_->settings->Update([](settings::Settings& s) { s.general.replaceWindowsTaskbar = false; });
            else impl_->command.action=StartupAction::RestoreTaskbar;
        }
        else if(action==L"settings" && !impl_->closing) { if(impl_->ready)impl_->OpenSettings(); else impl_->command.action=StartupAction::OpenSettings; }
        else if(action==L"search" && !impl_->closing) { if(impl_->ready && impl_->searchWindow)impl_->searchWindow->Open(); else impl_->command.action=StartupAction::OpenSearch; }
    }); !result) return result.error();
    impl_->loop.AddMessageFilter([this](MSG& message){
        if (message.message == WM_TIMER && message.hwnd == impl_->events.Handle() && message.wParam == kTaskbarResumeTimer) {
            const auto delay = TaskbarRevealDelay(impl_->automaticResume, impl_->resumeTimerArmed,
                impl_->settings->Current().general.replaceWindowsTaskbar, impl_->closing,
                impl_->desktopState, ::GetTickCount64(), impl_->resumeDeadline);
            if (!delay) return true;
            ::KillTimer(impl_->events.Handle(), kTaskbarResumeTimer);
            if (*delay > 0) {
                impl_->resumeTimerArmed = ::SetTimer(impl_->events.Handle(), kTaskbarResumeTimer,
                    static_cast<UINT>(std::min<std::uint64_t>(*delay, 60000)), nullptr) != 0;
                if (!impl_->resumeTimerArmed) impl_->automaticResume = false;
                return true;
            }
            impl_->resumeTimerArmed = false; impl_->automaticResume = false;
            (void)impl_->SetTaskbarReplacementSuspended(false);
            return true;
        }
        if (message.message == WM_TIMER && message.hwnd == impl_->events.Handle() && message.wParam == kTaskbarPollTimer) {
            if (impl_->taskbar && !impl_->closing) (void)impl_->taskbar->Refresh();
            return true;
        }
        if (message.message == WM_TIMER && message.hwnd == impl_->events.Handle() && message.wParam == kUpdaterPollTimer) {
            impl_->ObserveUpdateHandoff();
            return true;
        }
        if (message.message == WM_HOTKEY) { if(impl_->closing)return true; if(message.wParam==1 && impl_->searchWindow)impl_->searchWindow->Toggle(); else if(message.wParam==2 && impl_->dock)(void)impl_->dock->FocusDock(); return true; }
        return impl_->settingsWindow && impl_->settingsWindow->PreTranslate(message);
    });
    impl_->settings = std::make_unique<settings::SettingsService>(impl_->paths.configDir / L"settings.json", impl_->dispatcher);
    if (auto loading = impl_->settings->Load([this](Result<settings::LoadReport> result){
        if(!result) { ::MessageBoxW(nullptr,L"Settings could not be read safely. The existing file was preserved.",L"Aether Shell",MB_OK|MB_ICONERROR); ::PostQuitMessage(1); return; }
        if (impl_->command.action == StartupAction::Quit) { ::PostQuitMessage(0); return; }
        if (impl_->command.action == StartupAction::RestoreTaskbar) (void)impl_->settings->Update([](settings::Settings& s) { s.general.replaceWindowsTaskbar = false; });
        if(auto started=impl_->StartSurfaces();!started) { AETHER_LOG_ERROR("app","native surfaces could not start ({})",ToString(started.error().code)); ::MessageBoxW(nullptr,L"Aether Shell could not start its desktop surfaces.",L"Aether Shell",MB_OK|MB_ICONERROR); ::PostQuitMessage(1); }
    }); !loading) return loading.error();
    return impl_->loop.Run();
}

}  // namespace aether::app
