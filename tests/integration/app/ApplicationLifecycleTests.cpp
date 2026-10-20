// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "AetherTest.h"
#include "app/ShellApplication.h"
#include "core/FileSystem.h"
#include "install/Install.h"
#include "platform/windows/ComApartment.h"
#include "platform/windows/DpiUtilities.h"
#include "platform/windows/MonitorUtilities.h"
#include "platform/windows/SingleInstance.h"
#include "platform/windows/TaskbarReplacement.h"
#include "settings/SettingsStore.h"

#include <atomic>
#include <chrono>
#include <thread>

using namespace aether;
using namespace std::chrono_literals;
namespace {
HWND OwnWindow(const wchar_t* name, bool visible = true) {
    HWND found=nullptr;
    while((found=::FindWindowExW(nullptr,found,name,nullptr))!=nullptr){DWORD pid=0;::GetWindowThreadProcessId(found,&pid);if(pid==::GetCurrentProcessId() && (!visible || ::IsWindowVisible(found)))return found;}
    return nullptr;
}
}
AETHER_TEST("app: own Dock and Top Bar, health before discovery, channel actions and clean shutdown") {
    const auto root=test::MakeTempDirectory("app-lifecycle");
    struct Cleanup {std::filesystem::path root;~Cleanup(){std::error_code ignored;std::filesystem::remove_all(root,ignored);}} cleanup{root};
    auto paths=platform::AppPaths::Resolve(root);REQUIRE(paths.ok());
    auto store=settings::MakeFileSettingsStore(paths->configDir/L"settings.json");REQUIRE(store.ok());REQUIRE(store.value()->Load().ok());
    settings::Settings value;value.topBar.reserveSpace=false;value.dock.reserveSpace=false;value.dock.showRunningApps=false;
    value.general.hotkeysEnabled=false;value.general.motion=settings::MotionPreference::Reduced;value.updates.automaticChecks=false;value.weather.enabled=false;
    // D23 makes replacement the default. This fixture never reaches the real taskbar: the setting is
    // saved off and the session runs under its own instance name with native taskbar access denied.
    value.general.replaceWindowsTaskbar=false;
    REQUIRE(store.value()->Save(value,false).ok());
    auto token=install::MakeHealthToken();REQUIRE(token.ok());auto health=install::HealthCheck::Create(*token);REQUIRE(health.ok());
    const auto instance=L"AetherShell.Test."+std::to_wstring(::GetCurrentProcessId());
    RECT before{};REQUIRE(::SystemParametersInfoW(SPI_GETWORKAREA,0,&before,0)!=FALSE);
    platform::EnsurePerMonitorDpiAwareness();platform::ComApartment apartment;
    std::atomic<bool> bars{false},fullscreenSuppressed{false},healthy{false},settingsOpened{false},searchOpened{false},shutdownSent{false};
    const DWORD uiThread=::GetCurrentThreadId();
    std::jthread observer([&](std::stop_token stop){
        const auto until=[&](auto predicate){const auto deadline=std::chrono::steady_clock::now()+20s;while(!stop.stop_requested() && std::chrono::steady_clock::now()<deadline){if(predicate())return true;std::this_thread::sleep_for(10ms);}return false;};
        bars=until([&]{
            const auto dock=OwnWindow(L"AetherShell.Dock",false),bar=OwnWindow(L"AetherShell.TopBar",false);
            if(!dock || !bar)return false;
            if(::IsWindowVisible(dock) && ::IsWindowVisible(bar))return true;
            // The real desktop can enter full-screen during this read-only test. Accept hidden
            // bars only with that documented external precondition; never activate a foreign app.
            const bool fullscreen=platform::IsFullscreenWindow(::GetForegroundWindow(),platform::MonitorForWindow(bar));
            fullscreenSuppressed=fullscreen;return fullscreen;
        });
        auto signal=health->Wait(5000ms);healthy=signal && *signal;
        if(bars){
            (void)platform::InstanceChannel::Send(instance,L"settings");settingsOpened=until([]{return OwnWindow(L"AetherShell.Settings");});
            (void)platform::InstanceChannel::Send(instance,L"search");searchOpened=until([]{return OwnWindow(L"AetherShell.Search");});
        }
        auto requested=platform::InstanceChannel::Send(instance,L"shutdown");shutdownSent=requested.ok();
        if(!requested)::PostThreadMessageW(uiThread,WM_QUIT,1,0);
    });
    app::CommandLine command;command.healthToken=std::wstring(token->begin(),token->end());
    {app::ShellApplication application;auto ran=application.Run(std::move(command),*paths,instance,false);observer.request_stop();observer.join();REQUIRE(ran.ok());CHECK_EQ(*ran,0);}
    CHECK_FALSE(std::filesystem::exists(platform::TaskbarBaselineFile(paths->configDir)));
    CHECK(bars.load());CHECK(healthy.load());CHECK(settingsOpened.load());CHECK(searchOpened.load());CHECK(shutdownSent.load());
    if(fullscreenSuppressed)test::Note("own bars created while the real foreground was full-screen; visibility was suppressed");
    CHECK_FALSE(OwnWindow(L"AetherShell.Dock",false));CHECK_FALSE(OwnWindow(L"AetherShell.TopBar",false));
    RECT after{};REQUIRE(::SystemParametersInfoW(SPI_GETWORKAREA,0,&after,0)!=FALSE);CHECK(::EqualRect(&before,&after)!=FALSE);
    CHECK_EQ(platform::InstanceChannel::FindListenerProcessId(instance),DWORD{0});
}

AETHER_TEST("app: default-on replacement in an own-instance session never reaches the real taskbar") {
    // D23 guard: the settings keep their default (replacement on) and Run keeps its default
    // nativeTaskbarAccess; the test instance name alone must deny native taskbar access. The real
    // taskbar is only read (visibility), never changed.
    const auto root=test::MakeTempDirectory("app-lifecycle-default-on");
    struct Cleanup {std::filesystem::path root;~Cleanup(){std::error_code ignored;std::filesystem::remove_all(root,ignored);}} cleanup{root};
    auto paths=platform::AppPaths::Resolve(root);REQUIRE(paths.ok());
    auto store=settings::MakeFileSettingsStore(paths->configDir/L"settings.json");REQUIRE(store.ok());REQUIRE(store.value()->Load().ok());
    settings::Settings value;REQUIRE(value.general.replaceWindowsTaskbar);
    value.general.taskbarPinsImported=true;  // no automatic read of the user's Windows pins in a test
    value.topBar.reserveSpace=false;value.dock.reserveSpace=false;value.dock.showRunningApps=false;
    value.general.hotkeysEnabled=false;value.general.motion=settings::MotionPreference::Reduced;value.updates.automaticChecks=false;value.weather.enabled=false;
    REQUIRE(store.value()->Save(value,false).ok());
    const auto instance=L"AetherShell.Test.DefaultOn."+std::to_wstring(::GetCurrentProcessId());
    RECT before{};REQUIRE(::SystemParametersInfoW(SPI_GETWORKAREA,0,&before,0)!=FALSE);
    const auto trayVisible=[]{const HWND tray=::FindWindowW(L"Shell_TrayWnd",nullptr);return tray && ::IsWindowVisible(tray);};
    const bool trayBefore=trayVisible();
    platform::EnsurePerMonitorDpiAwareness();platform::ComApartment apartment;
    std::atomic<bool> bars{false},untouched{true},shutdownSent{false};
    const DWORD uiThread=::GetCurrentThreadId();
    std::jthread observer([&](std::stop_token stop){
        const auto deadline=std::chrono::steady_clock::now()+20s;
        while(!stop.stop_requested() && std::chrono::steady_clock::now()<deadline && !bars){
            bars=OwnWindow(L"AetherShell.Dock",false) && OwnWindow(L"AetherShell.TopBar",false);
            if(!bars)std::this_thread::sleep_for(10ms);
        }
        // An enabled controller would have hidden the taskbar and reserved Dock space by now.
        const auto until=std::chrono::steady_clock::now()+2s;
        while(bars && !stop.stop_requested() && std::chrono::steady_clock::now()<until){
            RECT area{};const bool read=::SystemParametersInfoW(SPI_GETWORKAREA,0,&area,0)!=FALSE;
            if(!read || !::EqualRect(&before,&area) || trayVisible()!=trayBefore ||
               std::filesystem::exists(platform::TaskbarBaselineFile(paths->configDir)))untouched=false;
            std::this_thread::sleep_for(50ms);
        }
        auto requested=platform::InstanceChannel::Send(instance,L"shutdown");shutdownSent=requested.ok();
        if(!requested)::PostThreadMessageW(uiThread,WM_QUIT,1,0);
    });
    {app::ShellApplication application;auto ran=application.Run(app::CommandLine{},*paths,instance);observer.request_stop();observer.join();REQUIRE(ran.ok());CHECK_EQ(*ran,0);}
    CHECK(bars.load());CHECK(untouched.load());CHECK(shutdownSent.load());
    CHECK_FALSE(std::filesystem::exists(platform::TaskbarBaselineFile(paths->configDir)));
    RECT after{};REQUIRE(::SystemParametersInfoW(SPI_GETWORKAREA,0,&after,0)!=FALSE);CHECK(::EqualRect(&before,&after)!=FALSE);
    CHECK_EQ(trayVisible(),trayBefore);
    CHECK_EQ(platform::InstanceChannel::FindListenerProcessId(instance),DWORD{0});
}