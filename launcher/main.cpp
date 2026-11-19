// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "install/Install.h"
#include "core/Localization.h"
#include "install/LifecyclePolicy.h"
#include "install/LauncherSupervisor.h"
#include "core/Strings.h"
#include "platform/windows/AppPaths.h"
#include "platform/windows/TaskbarReplacement.h"
#include "platform/windows/SingleInstance.h"

#include <shellapi.h>
#include <array>
#include <algorithm>
#include <charconv>

namespace {
struct SupervisedLaunch {
    aether::install::ActiveVersionProcess child;
    aether::install::HealthCheck health;
};
aether::Result<SupervisedLaunch> StartSupervised(const aether::install::InstallLayout& layout,
    const std::vector<std::wstring>& arguments,
    std::optional<aether::updates::Sha256Digest> expected = std::nullopt) {
    auto token=aether::install::MakeHealthToken();if(!token)return token.error();
    auto health=aether::install::HealthCheck::Create(*token);if(!health)return health.error();
    auto childArguments=arguments;
    childArguments.emplace_back(L"--post-update-check");childArguments.push_back(aether::text::Widen(*token));
    auto child=aether::install::LaunchActiveVersionProcess(layout,childArguments,expected);
    if(!child)return child.error();return SupervisedLaunch{std::move(*child),std::move(*health)};
}
bool Transient(const std::vector<std::wstring>& arguments) {
    for(const auto& value:arguments)if(value==L"--quit" || value==L"--restore-taskbar" ||
        value==L"--version" || value==L"--help" || value==L"--post-update-check")return true;
    return false;
}
// D82: a sign-in start (the sign-in task, or Windows' Run list) goes ahead only while sign-in startup is on for this
// installation. The Run value and Task Manager's switch decide; the task, which Task Manager does not show, follows
// them. Unreadable or in an unknown format: start, as Windows' own Run list would.
bool SignInStartAllowed(const std::filesystem::path& launcher) {
    auto registry=aether::settings::MakeWindowsStartupRegistry();if(!registry)return true;
    auto state=aether::settings::StartupRegistration(*registry).IsEnabled(launcher);if(!state)return true;
    using aether::settings::StartupState;
    return *state==StartupState::Enabled || *state==StartupState::UnknownApproval;
}
void AwaitControlReadiness(const aether::install::ActiveVersionProcess& child) {
    // Do not abandon a just-created child before Quit/maintenance can address it. Requesters have
    // their own bounded lease wait and fail safely if startup hangs; we never terminate the child.
    // AS4-17: bounded, so a child that never opens its channel cannot keep the supervisor (and its
    // installation lease) waiting forever.
    (void)aether::install::AwaitControlReadiness(child.process.Get(), [&child] {
        return aether::platform::InstanceChannel::FindListenerProcessId(L"AetherShell")==child.processId;
    }, std::chrono::seconds(30));
}
}

int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int) {
    // D82: tell whoever started us that we are ready for input. Windows starts its Run list one program at a time, and
    // the next one waited 3.6 to 23 s after this launcher (measured at sign-in). A process that only ever waits on
    // handles never becomes input-idle, so a WaitForInputIdle on it runs to its timeout; one zero-length look at the
    // input queue makes it idle at once (13 ms in a probe, against the whole timeout without it).
    (void)::MsgWaitForMultipleObjects(0,nullptr,FALSE,0,QS_ALLINPUT);
    try {
        std::array<wchar_t,32768> executable{};
        const DWORD length=::GetModuleFileNameW(nullptr,executable.data(),static_cast<DWORD>(executable.size()));
        if(!length || length>=executable.size()) return 2;
        int count=0; auto* argv=::CommandLineToArgvW(::GetCommandLineW(),&count); if(!argv) return 2;
        std::vector<std::wstring> arguments; for(int i=1;i<count;++i) arguments.emplace_back(argv[i]); ::LocalFree(argv);
        if (!arguments.empty() && arguments.front() == L"--taskbar-guard") {
            if (arguments.size() != 5) return 2;
            std::array<HANDLE,4> handles{};
            for (std::size_t i=0;i<handles.size();++i) {
                std::string text;
                for (const auto c : arguments[i+1]) {
                    if (c<L'0' || c>L'9') return 2;
                    text.push_back(static_cast<char>(c));
                }
                std::uintptr_t value=0;
                const auto parsed=std::from_chars(text.data(),text.data()+text.size(),value);
                if(parsed.ec!=std::errc{} || parsed.ptr!=text.data()+text.size() || !value)return 2;
                handles[i]=reinterpret_cast<HANDLE>(value);
            }
            auto guarded=aether::platform::RunTaskbarGuard(handles[0],handles[1],handles[2],handles[3]); return guarded ? *guarded : 1;
        }
        auto layout=aether::install::InstallLayout::Create(std::filesystem::path(executable.data()).parent_path());
        if(!layout) return 2;
        if(Transient(arguments)) {
            for(const auto& value:arguments)if(value==L"--quit" || value==L"--restore-taskbar") {
                return aether::install::LaunchControlCommand(*layout,arguments)?0:4;
            }
            return aether::install::LaunchActiveVersion(*layout,arguments)?0:4;
        }
        if(std::find(arguments.begin(),arguments.end(),L"--autostart")!=arguments.end() && !SignInStartAllowed(layout->Launcher()))return 0;
        auto supervisor=aether::install::LauncherSupervisor::Acquire(*layout);
        if(!supervisor) {
            if(supervisor.error().code==aether::ErrorCode::Unavailable) {
                // During bootstrap/backoff the existing owner must remain responsible for the
                // next real child. Starting a direct child here would bypass its supervision.
                std::wstring_view action=L"search";
                bool asked=false,autostart=false;
                for(std::size_t i=0;i<arguments.size();++i) {
                    if(arguments[i]==L"--settings"){action=L"settings";asked=true;}
                    else if(arguments[i]==L"--search")asked=true;
                    else if(arguments[i]==L"--autostart")autostart=true;
                    else if(arguments[i]==L"--data-dir" && i+1<arguments.size() && !arguments[i+1].empty())++i;
                    else return 2;
                }
                // D82: the second of the two sign-in starts (the sign-in task, then Windows' Run list) finds Aether
                // running and asks it for nothing. Treated as a plain start, it opened Search.
                if(autostart && !asked)return 0;
                // IPC only: if the existing app disappears now, the owner alone may restart it.
                return aether::platform::InstanceChannel::Send(L"AetherShell",action)?0:3;
            }
            return 4;
        }
        auto stopping=supervisor->StopRequested(std::chrono::milliseconds(0));if(!stopping || *stopping)return 0;
        auto launched=StartSupervised(*layout,arguments);
        if(!launched) {
            supervisor->Release();  // a failure dialog must never block maintenance cancellation
            const bool busy=launched.error().code==aether::ErrorCode::Unavailable;
            if(busy)return 3;  // release this image promptly while setup owns the lifecycle lock
            for(const auto& value:arguments)if(value==L"--autostart")return 4;
            // Documented native fallback: the launcher has no renderer and the app did not start.
            {   // D70/D74/D83: the language chosen in Settings, else the Windows display language, from its pack
                const auto root = aether::platform::DefaultDataRoot();
                if (root) (void)aether::i18n::StartIn(*root / L"config" / L"settings.json", *root / L"languages");
            }
            ::MessageBoxW(nullptr,aether::i18n::Tr(L"Aether Shell could not start. Choose Repair in Windows Settings > Apps > Installed apps.").data(),L"Aether Shell",MB_OK|MB_ICONERROR);return 4;
        }
        aether::install::CrashRestartPolicy restarts;
        for(;;) {
            // The app signals this only after settings and its native surfaces are ready. Waiting
            // is bounded; an unresponsive live app is never terminated or repeatedly relaunched.
            const auto deadline=::GetTickCount64()+90000;
            while(::WaitForSingleObject(launched->child.process.Get(),0)==WAIT_TIMEOUT &&
                  ::GetTickCount64()<deadline) {
                auto ready=launched->health.Wait(std::chrono::milliseconds(100));
                if(!ready || *ready)break;
                auto stopped=supervisor->StopRequested(std::chrono::milliseconds(0));
                if(!stopped || *stopped){AwaitControlReadiness(launched->child);return 0;}
            }
            const HANDLE watched[]{launched->child.process.Get(),supervisor->StopEvent()};
            const auto ended=::WaitForMultipleObjects(2,watched,FALSE,INFINITE);
            if(ended==WAIT_OBJECT_0+1){AwaitControlReadiness(launched->child);return 0;}
            if(ended!=WAIT_OBJECT_0)return 5;
            DWORD code=0;if(!::GetExitCodeProcess(launched->child.process.Get(),&code))return 5;
            auto delay=restarts.NextDelay(code);if(!delay)return 0;
            const auto identity=launched->child.stateDigest;
            auto stopped=supervisor->StopRequested(*delay);if(!stopped || *stopped)return 0;
            // A restart after a crash is not a sign-in: the same arguments, without greeting again (FR-041).
            std::erase(arguments,std::wstring(L"--autostart"));
            auto next=StartSupervised(*layout,arguments,identity);
            // Missing/changed journals, maintenance lock contention and launch failure stop the
            // supervisor. The independent taskbar guard already restores the native taskbar.
            if(!next)return 0;
            launched=std::move(next);
        }
    } catch(...) { return 5; }
}
