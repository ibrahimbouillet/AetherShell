// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "app/ShellApplication.h"

#include "applications/ShellApplicationCatalog.h"
#include "applications/ShellApplicationLauncher.h"
#include "applications/ShellIconProvider.h"
#include "applications/WindowTracker.h"
#include "core/Strings.h"
#include "core/FileSystem.h"
#include "core/Time.h"
#include "core/WorkerPool.h"
#include "diagnostics/Logger.h"
#include "dock/DockWindow.h"
#include "graphics/FrameScheduler.h"
#include "graphics/GraphicsDevice.h"
#include "install/Install.h"
#include "platform/net/WinHttpClient.h"
#include "platform/windows/MessageLoop.h"
#include "platform/windows/OsVersion.h"
#include "platform/windows/SingleInstance.h"
#include "platform/windows/SystemEvents.h"
#include "platform/windows/Win32UiDispatcher.h"
#include "platform/windows/WindowUtilities.h"
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
#include <utility>
#include <mutex>

namespace aether::app {
namespace {
constexpr UINT_PTR kUpdaterPollTimer = 0xA371;
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
    std::vector<apps::ApplicationInfo> applications;
    platform::AppPaths paths;
    CommandLine command;
    std::wstring instanceName = L"AetherShell";
    std::wstring hotkeyStatus;
    bool closing = false;
    bool ready = false;
    bool stopped = false;
    bool updateHandoffPending = false;
    platform::UniqueHandle updaterProcess;
    std::shared_ptr<UpdateLaunchResult> updateLaunch;

    Result<void> StartSurfaces();
    void ApplySettings();
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
    const auto tokens = graphics::MakeDesignTokens(graphics::DetectThemeMode());
    graphics::MotionPolicy motion;
    motion.reduced = value.general.motion == settings::MotionPreference::Reduced ||
        (value.general.motion == settings::MotionPreference::System && !platform::SystemAnimationsEnabled());
    if (dock) (void)dock->SetTheme(tokens, motion, value.dock);
    if (topbar) { topbar->SetTheme(tokens, motion); topbar->SetSettings(value.topBar, value.weather.enabled); }
    if (searchWindow) searchWindow->SetTheme(tokens, motion);
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
    connections.push_back(topbar->SearchRequested.Connect([this]{ if (!closing) searchWindow->Toggle(); }));
    connections.push_back(topbar->SettingsRequested.Connect([this]{ OpenSettings(settings::ui::SettingsPage::TopBar); }));
    connections.push_back(topbar->AboutRequested.Connect([this]{ OpenSettings(settings::ui::SettingsPage::About); }));
    connections.push_back(topbar->CheckForUpdatesRequested.Connect([this]{ CheckUpdates(); }));
    connections.push_back(topbar->UpdatesRequested.Connect([this]{ OpenSettings(settings::ui::SettingsPage::Updates); }));
    connections.push_back(topbar->QuitRequested.Connect([this]{ RequestQuit(); }));
    connections.push_back(topbar->FullscreenChanged.Connect([this](bool hidden){ (void)dock->SetSuppressed(hidden); }));
    connections.push_back(settings->Changed().Connect([this](const settings::Settings&){ ApplySettings(); }));
    connections.push_back(events.ThemeChanged.Connect([this]{ ApplySettings(); }));
    connections.push_back(events.DisplayChanged.Connect([this]{ if(topbar)topbar->Relayout(); if(dock)(void)dock->Relayout(); }));
    connections.push_back(events.ExplorerRestarted.Connect([this]{ if(topbar)topbar->OnExplorerRestarted(); if(dock)(void)dock->OnExplorerRestarted(); }));
    const auto clockChanged = [this] { providers->clock->NotifyTimeChanged(); providers->battery->Refresh(); providers->network->Refresh(); weather->NotifyClockChanged(); if(updates)updates->NotifyClockChanged(); };
    connections.push_back(events.TimeChanged.Connect(clockChanged));
    connections.push_back(events.ResumedFromSleep.Connect(clockChanged));
    connections.push_back(events.PowerSettingChanged.Connect([this]{ providers->battery->Refresh(); }));
    connections.push_back(events.SettingChanged.Connect([this](UINT, const std::wstring& section){ if(section==L"intl"){providers->clock->NotifyTimeChanged();providers->keyboard->Refresh();} ApplySettings(); }));
    if (updates) connections.push_back(updates->Subscribe([this](const updates::UpdateSnapshot& snapshot){ if(settingsWindow)settingsWindow->SetUpdateSnapshot(snapshot); RefreshUpdateOffer(snapshot); }));
    (void)tracker->Start([this](const std::vector<apps::RunningApplication>& running){ if(dock)(void)dock->SetRunningApplications(running); });
    const auto weak = std::weak_ptr<bool>(alive); const auto cancel = cancellation.Token();
    ApplySettings();
    dock->Show(); topbar->Show(); ready = true;
    if (!command.healthToken.empty()) { const auto token = text::Narrow(command.healthToken); worker.Post([token]{ (void)install::SignalHealthy(token); }); }
    worker.Post([this, weak, cancel] {
        auto discovered = catalog->DiscoverApplications(cancel);
        dispatcher.Post([this, weak, discovered = std::move(discovered)]() mutable {
            if (auto live = weak.lock(); live && *live && discovered) { applications = std::move(*discovered); (void)search->Rebuild(applications); }
        });
    });
    if (command.action == StartupAction::OpenSearch) searchWindow->Open();
    else if (command.action == StartupAction::OpenSettings) OpenSettings();
    AETHER_LOG_INFO("app", "native Dock and Top Status Bar started");
    return Ok();
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
    ::KillTimer(events.Handle(), kUpdaterPollTimer); updaterProcess.Reset(); updateLaunch.reset();
    ::UnregisterHotKey(events.Handle(), 1); ::UnregisterHotKey(events.Handle(), 2);
    connections.clear(); channel.Close();
    if (tracker) tracker->Stop();
    if (updates) updates->Stop();
    if (topbar) topbar.reset();
    if (providers) providers->StopAll();
    if (audio) audio->Stop(); if (microphone) microphone->Stop(); if (weather) weather->Stop();
    dock.reset(); searchWindow.reset(); settingsWindow.reset();
    worker.Shutdown();
    search.reset(); tracker.reset(); icons.reset(); launcher.reset(); catalog.reset();
    updates.reset(); geocoding.reset(); weather.reset(); audio.reset(); microphone.reset(); providers.reset(); http.reset(); settings.reset();
    events.Destroy(); scheduler.Shutdown(); graphics.Shutdown();
    (void)status::FinalizeStatusRuntime();
    dispatcher.Shutdown();
}

ShellApplication::ShellApplication() : impl_(std::make_unique<Impl>()) {}
ShellApplication::~ShellApplication() { impl_->Stop(); }
Result<int> ShellApplication::Run(CommandLine command, platform::AppPaths paths, std::wstring instanceName) {
    impl_->command = std::move(command); impl_->paths = std::move(paths); impl_->instanceName = std::move(instanceName);
    auto instance = platform::SingleInstanceLock::Acquire(impl_->instanceName); if (!instance) return instance.error();
    if (!instance->IsPrimary()) {
        const auto action = impl_->command.action == StartupAction::OpenSettings ? L"settings" : L"search";
        if (auto sent = platform::InstanceChannel::Send(impl_->instanceName, action); !sent) return sent.error();
        return 0;
    }
    struct ShutdownBeforeUnlock { Impl& app; ~ShutdownBeforeUnlock() { app.Stop(); } } shutdown{*impl_};
    if (auto result = impl_->dispatcher.Initialize(); !result) return result.error();
    if (auto result = impl_->graphics.Initialize(); !result) return result.error();
    if (auto result = impl_->scheduler.Initialize(impl_->loop); !result) return result.error();
    if (auto result = impl_->events.Create(); !result) return result.error();
    if (auto result = impl_->channel.Listen(impl_->instanceName, [this](std::wstring_view action){
        if(action==L"shutdown-for-update" || action==L"shutdown")impl_->RequestQuit();
        else if(action==L"settings" && !impl_->closing) { if(impl_->ready)impl_->OpenSettings(); else impl_->command.action=StartupAction::OpenSettings; }
        else if(action==L"search" && !impl_->closing) { if(impl_->ready && impl_->searchWindow)impl_->searchWindow->Open(); else impl_->command.action=StartupAction::OpenSearch; }
    }); !result) return result.error();
    impl_->loop.AddMessageFilter([this](MSG& message){
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
        if(auto started=impl_->StartSurfaces();!started) { AETHER_LOG_ERROR("app","native surfaces could not start ({})",ToString(started.error().code)); ::MessageBoxW(nullptr,L"Aether Shell could not start its desktop surfaces.",L"Aether Shell",MB_OK|MB_ICONERROR); ::PostQuitMessage(1); }
    }); !loading) return loading.error();
    return impl_->loop.Run();
}

}  // namespace aether::app
