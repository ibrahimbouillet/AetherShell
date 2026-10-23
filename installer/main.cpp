// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "install/Install.h"
#include "install/LifecyclePolicy.h"
#include "core/Dispatcher.h"
#include "core/Strings.h"
#include "core/FileSystem.h"
#include "aether/BuildInfo.h"
#include "core/WorkerPool.h"
#include "platform/windows/AppPaths.h"
#include "platform/windows/ComApartment.h"
#include "platform/windows/DpiUtilities.h"
#include "platform/windows/SingleInstance.h"
#include "platform/windows/TaskbarReplacement.h"
#include "platform/windows/Win32UiDispatcher.h"
#include "platform/windows/MessageLoop.h"
#include "platform/windows/WindowUtilities.h"
#include "settings/SettingsStore.h"
#include "settings/SettingsSerialization.h"
#include "settings/StartupRegistration.h"

#include <commctrl.h>
#include <shlobj.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <winrt/base.h>
#include <filesystem>
#include <future>
#include <memory>
#include <charconv>
#include <array>
#include <array>
#include <cstddef>
#include <algorithm>
#include <climits>

namespace {
using namespace aether;
struct Options {
    std::filesystem::path root, data, log;
    bool silent=false, noLaunch=false, noShortcuts=false, startup=false, repair=false, uninstall=false, removeData=false, desktopShortcut=false, allowDowngrade=false;
    bool startupExplicit=false;
    std::uintptr_t maintenanceParent=0;
    std::string maintenanceToken;
#ifdef AETHER_SETUP_TEST_MODE
    bool testNoRegistration=false;
    std::filesystem::path testShortcutDir;  // replaces the Start menu folder for the restore shortcut
#endif
};
Result<Options> Parse() {
    Options options; int count=0; wchar_t** args=::CommandLineToArgvW(::GetCommandLineW(),&count);
    if(!args)return MakeError(ErrorCode::InvalidArgument,"setup arguments unavailable");
    std::vector<std::wstring> values; for(int i=1;i<count;++i)values.emplace_back(args[i]); ::LocalFree(args);
    for(std::size_t i=0;i<values.size();++i) {
        const auto& value=values[i];
        if(value==L"--silent")options.silent=true;
        else if(value==L"--no-launch")options.noLaunch=true;
        else if(value==L"--no-shortcuts")options.noShortcuts=true;
        else if(value==L"--start-with-windows" || value==L"--no-start-with-windows") {
            const bool enabled=value==L"--start-with-windows";
            if(options.startupExplicit && options.startup!=enabled)return MakeError(ErrorCode::InvalidArgument,"conflicting startup choices");
            options.startup=enabled;options.startupExplicit=true;
        }
        else if(value==L"--repair")options.repair=true;
        else if(value==L"--allow-downgrade")options.allowDowngrade=true;
        else if(value==L"--uninstall")options.uninstall=true;
        else if(value==L"--remove-user-data")options.removeData=true;
        else if(value==L"--keep-user-data")options.removeData=false;
#ifdef AETHER_SETUP_TEST_MODE
        else if(value==L"--test-no-registration")options.testNoRegistration=true;
        else if(value==L"--test-shortcut-dir") {
            if(++i>=values.size())return MakeError(ErrorCode::InvalidArgument,"test shortcut folder is missing");
            options.testShortcutDir=values[i];
        }
#endif
        else if(value==L"--maintenance-parent" || value==L"--maintenance-token") {
            if(++i>=values.size())return MakeError(ErrorCode::InvalidArgument,"maintenance argument is missing");
            const auto argument=text::Narrow(values[i]);
            if(value==L"--maintenance-token") options.maintenanceToken=argument;
            else {const auto parsed=std::from_chars(argument.data(),argument.data()+argument.size(),options.maintenanceParent);if(parsed.ec!=std::errc{} || parsed.ptr!=argument.data()+argument.size() || !options.maintenanceParent)return MakeError(ErrorCode::InvalidArgument,"invalid maintenance parent");}
        }
        else if(value==L"--install-root" || value==L"--data-dir" || value==L"--log") {
            if(++i>=values.size())return MakeError(ErrorCode::InvalidArgument,"setup path argument is missing");
            if(value==L"--install-root")options.root=values[i]; else if(value==L"--data-dir")options.data=values[i]; else options.log=values[i];
        } else return MakeError(ErrorCode::InvalidArgument,"unknown setup argument");
    }
    if(options.repair && options.uninstall)return MakeError(ErrorCode::InvalidArgument,"conflicting setup actions");
    if((options.maintenanceParent!=0)!=!options.maintenanceToken.empty() || (!options.maintenanceToken.empty() && !install::IsHealthToken(options.maintenanceToken)))return MakeError(ErrorCode::InvalidArgument,"invalid maintenance handoff");
#ifdef AETHER_SETUP_TEST_MODE
    if(!options.testShortcutDir.empty() && (!options.testNoRegistration || !options.testShortcutDir.is_absolute()))
        return MakeError(ErrorCode::InvalidArgument,"test shortcut folder requires isolated absolute roots");
    if(options.testNoRegistration) {
        if(!options.root.is_absolute() || !options.data.is_absolute())return MakeError(ErrorCode::InvalidArgument,"test setup requires isolated absolute roots");
        // Only the restore shortcut can be created, and only in the isolated test folder.
        options.noLaunch=true; options.startup=false; if(options.testShortcutDir.empty())options.noShortcuts=true;
    }
#endif
    return options;
}

Result<bool> RelocateMaintenance(const Options& options) {
    const auto current=platform::CurrentExecutablePath();
    auto layout=install::InstallLayout::Create(options.root);if(!layout)return layout.error();
    if(options.maintenanceParent) {
        platform::UniqueHandle parent(reinterpret_cast<HANDLE>(options.maintenanceParent));
        const auto expected=L"AetherShell-maintenance-"+text::Widen(options.maintenanceToken);
        if(current.filename()!=L"AetherShellSetup.exe" || current.parent_path().filename()!=expected || !::GetProcessId(parent.Get()))return MakeError(ErrorCode::AccessDenied,"invalid maintenance worker");
        auto original=updates::Sha256::HashFile(layout->Maintenance()),copy=updates::Sha256::HashFile(current);
        if(!original || !copy || original.value()!=copy.value())return MakeError(ErrorCode::Integrity,"maintenance worker differs from installed setup");
        if(::WaitForSingleObject(parent.Get(),30000)!=WAIT_OBJECT_0)return MakeError(ErrorCode::Timeout,"maintenance parent has not exited");
        return false;
    }
    if(::_wcsicmp(current.c_str(),layout->Maintenance().c_str())!=0)return false;
    auto token=install::MakeHealthToken();if(!token)return token.error();
    std::array<wchar_t,32768> temporary{};const auto length=::GetTempPathW(static_cast<DWORD>(temporary.size()),temporary.data());
    if(!length || length>=temporary.size())return MakeError(ErrorCode::Io,"temporary maintenance directory unavailable");
    const auto directory=std::filesystem::path(temporary.data())/(L"AetherShell-maintenance-"+text::Widen(*token));
    auto safe=install::InstallLayout::Create(directory);if(!safe)return safe.error();
    std::error_code error;if(!std::filesystem::create_directory(directory,error) || error)return MakeError(ErrorCode::Io,"cannot create isolated maintenance directory",error.value());
    const auto executable=directory/L"AetherShellSetup.exe";
    const auto cleanup=[&]{std::error_code ignored;std::filesystem::remove(executable,ignored);std::filesystem::remove(directory,ignored);};
    if(!::CopyFileW(current.c_str(),executable.c_str(),TRUE)){const auto code=::GetLastError();cleanup();return MakeError(ErrorCode::Io,"cannot relocate maintenance setup",code);}
    platform::UniqueHandle parent;
    if(!::DuplicateHandle(::GetCurrentProcess(),::GetCurrentProcess(),::GetCurrentProcess(),parent.Put(),SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,TRUE,0)){const auto code=::GetLastError();cleanup();return MakeError(ErrorCode::Platform,"cannot create maintenance parent handle",code);}
    SIZE_T bytes=0;::InitializeProcThreadAttributeList(nullptr,1,0,&bytes);std::vector<std::byte> storage(bytes);
    auto* attributes=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
    if(!::InitializeProcThreadAttributeList(attributes,1,0,&bytes)){cleanup();return MakeError(ErrorCode::Platform,"cannot initialize maintenance handoff",::GetLastError());}
    HANDLE handle=parent.Get();
    if(!::UpdateProcThreadAttribute(attributes,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,&handle,sizeof(handle),nullptr,nullptr)){::DeleteProcThreadAttributeList(attributes);cleanup();return MakeError(ErrorCode::Platform,"cannot prepare maintenance handoff",::GetLastError());}
    std::vector<std::wstring> args{L"--install-root",options.root.native(),L"--data-dir",options.data.native(),L"--log",options.log.native(),L"--maintenance-parent",std::to_wstring(reinterpret_cast<std::uintptr_t>(parent.Get())),L"--maintenance-token",text::Widen(*token)};
    if(options.silent)args.emplace_back(L"--silent");if(options.noLaunch)args.emplace_back(L"--no-launch");if(options.noShortcuts)args.emplace_back(L"--no-shortcuts");if(options.startup)args.emplace_back(L"--start-with-windows");if(options.repair)args.emplace_back(L"--repair");if(options.uninstall)args.emplace_back(L"--uninstall");if(options.removeData)args.emplace_back(L"--remove-user-data");if(options.allowDowngrade)args.emplace_back(L"--allow-downgrade");
    if(options.startupExplicit && !options.startup)args.emplace_back(L"--no-start-with-windows");
#ifdef AETHER_SETUP_TEST_MODE
    if(options.testNoRegistration)args.emplace_back(L"--test-no-registration");
    if(!options.testShortcutDir.empty()){args.emplace_back(L"--test-shortcut-dir");args.emplace_back(options.testShortcutDir.native());}
#endif
    std::wstring command=install::QuoteArgument(executable.native());for(const auto& arg:args)command+=L" "+install::QuoteArgument(arg);
    STARTUPINFOEXW startup{};startup.StartupInfo.cb=sizeof(startup);startup.lpAttributeList=attributes;PROCESS_INFORMATION process{};
    const BOOL launched=::CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,TRUE,EXTENDED_STARTUPINFO_PRESENT,nullptr,directory.c_str(),&startup.StartupInfo,&process);
    const auto code=::GetLastError();::DeleteProcThreadAttributeList(attributes);
    if(!launched){cleanup();return MakeError(ErrorCode::Platform,"cannot launch relocated maintenance setup",code);}
    platform::UniqueHandle thread(process.hThread),child(process.hProcess);
    // The original image must exit before its directory can be removed. A successful handoff has
    // exit code zero; the isolated worker records the final operation outcome in the selected log.
    return true;
}

Result<void> Resolve(Options& options) {
    if(options.root.empty()) {
        // The installed maintenance copy lives beside the journal, regardless of custom root.
        const auto directory=platform::CurrentExecutablePath().parent_path();
        if(std::filesystem::is_regular_file(directory/L"install-state.json"))options.root=directory;
        else { auto local=platform::KnownFolder(FOLDERID_LocalAppData); if(!local)return local.error(); options.root=*local/L"Programs"/L"AetherShell"; }
    }
    if(options.data.empty()) { auto local=platform::KnownFolder(FOLDERID_LocalAppData); if(!local)return local.error(); options.data=*local/L"AetherShell"; }
    if(options.log.empty()) options.log=options.data/L"logs"/L"setup.log";
    if(!options.root.is_absolute() || !options.data.is_absolute())return MakeError(ErrorCode::InvalidArgument,"setup directories must be absolute");
#ifdef AETHER_SETUP_TEST_MODE
    if(options.testNoRegistration)return Ok();  // never inspect the owner's startup keys in fixtures
#endif
    if(!options.startupExplicit && !options.silent && !options.uninstall) {
        auto layout=install::InstallLayout::Create(options.root);if(!layout)return layout.error();
        auto registry=settings::MakeWindowsStartupRegistry();if(!registry)return registry.error();
        auto state=settings::StartupRegistration(*registry).IsEnabled(layout->Launcher());if(!state)return state.error();
        std::optional<bool> savedChoice;std::error_code error;
        const auto settingsPath=options.data/L"config"/L"settings.json";
        const bool hasSettings=std::filesystem::exists(settingsPath,error);if(error)return MakeError(ErrorCode::Io,"cannot inspect existing settings",error.value());
        if(hasSettings) {
            // Opening/cancelling setup must never recover, rename or prune existing settings.
            auto bytes=fs::ReadFile(settingsPath,1024*1024);if(!bytes)return bytes.error();
            auto json=json::Parse(*bytes);if(!json)savedChoice=false;
            else {auto parsed=settings::FromJson(*json);savedChoice=parsed?parsed->value.general.startWithWindows:false;}
        }
        const bool installed=std::filesystem::exists(layout->Journal(),error);if(error)return MakeError(ErrorCode::Io,"cannot inspect existing installation",error.value());
        options.startup=install::SetupStartupDefault(true,installed || options.repair,savedChoice,*state);
    }
    return Ok();
}

#ifdef AETHER_SETUP_TEST_MODE
class IsolatedProcesses final: public install::IProcessOperations {
public:
    Result<void> RequestExit(std::chrono::milliseconds) override { return Ok(); }
    Result<DWORD> Launch(const std::filesystem::path&,const std::vector<std::wstring>&) override { return MakeError(ErrorCode::Unsupported,"test setup cannot launch applications"); }
    Result<bool> WaitForHealth(const install::HealthCheck&,std::chrono::milliseconds) override { return false; }
    Result<void> StopUnhealthyChild(DWORD) override { return MakeError(ErrorCode::Unsupported,"test setup cannot terminate applications"); }
};
#endif

// D23 safety: a per-user Start menu entry that turns taskbar replacement off and shows the Windows
// taskbar again (`AetherShell.exe --restore-taskbar`; the launcher forwards the argument).
constexpr wchar_t kRestoreShortcutName[]=L"Restore Windows taskbar.lnk";

Result<std::filesystem::path> RestoreShortcutFolder(const Options& options) {
#ifdef AETHER_SETUP_TEST_MODE
    // Isolated tests never reach the user's Start menu; without a test folder nothing is created.
    if(options.testNoRegistration) {
        if(options.testShortcutDir.empty())return MakeError(ErrorCode::Unavailable,"no isolated shortcut folder");
        return options.testShortcutDir;
    }
#else
    (void)options;
#endif
    return platform::KnownFolder(FOLDERID_Programs);
}

// Creates (or refreshes) the shortcut, or removes it. A file of the same name that does not point at
// this installation's launcher is never replaced or removed.
Result<void> RestoreTaskbarShortcut(const install::InstallLayout& layout,const std::filesystem::path& folder,bool remove) try {
    platform::ComApartment apartment;
    if(!apartment.IsUsable())return MakeError(ErrorCode::Platform,"cannot initialize shortcut COM",apartment.Status());
    const auto path=folder/kRestoreShortcutName;
    winrt::com_ptr<IShellLinkW> link;
    HRESULT hr=::CoCreateInstance(CLSID_ShellLink,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(link.put()));
    if(FAILED(hr))return MakeError(ErrorCode::Platform,"cannot create shortcut interface",hr);
    auto persist=link.try_as<IPersistFile>();
    if(!persist)return MakeError(ErrorCode::Platform,"cannot create shortcut storage");
    std::error_code error;
    if(std::filesystem::exists(path,error)) {
        if(FAILED(persist->Load(path.c_str(),STGM_READ)))return MakeError(ErrorCode::Io,"cannot inspect the restore shortcut");
        std::array<wchar_t,32768> target{};
        if(FAILED(link->GetPath(target.data(),static_cast<int>(target.size()),nullptr,SLGP_RAWPATH)))return MakeError(ErrorCode::Io,"cannot inspect the restore shortcut target");
        if(::_wcsicmp(target.data(),layout.Launcher().c_str())!=0)return Ok();
        if(remove) {
            std::filesystem::remove(path,error);
            return error?Result<void>(MakeError(ErrorCode::Io,"cannot remove the restore shortcut",error.value())):Ok();
        }
    } else if(error)return MakeError(ErrorCode::Io,"cannot inspect the Start menu folder",error.value());
    else if(remove)return Ok();
    hr=link->SetPath(layout.Launcher().c_str());
    if(SUCCEEDED(hr))hr=link->SetArguments(L"--restore-taskbar");
    if(SUCCEEDED(hr))hr=link->SetDescription(L"Aether Shell: stop replacing the Windows taskbar and show it again");
    if(SUCCEEDED(hr))hr=link->SetWorkingDirectory(layout.root.c_str());
    if(SUCCEEDED(hr))hr=link->SetIconLocation(layout.Launcher().c_str(),0);
    if(SUCCEEDED(hr))hr=persist->Save(path.c_str(),TRUE);
    return SUCCEEDED(hr)?Ok():Result<void>(MakeError(ErrorCode::Platform,"cannot save the restore shortcut",hr));
} catch(...) { return MakeError(ErrorCode::Internal,"restore shortcut failed"); }

Result<void> Perform(const Options& options,const CancellationToken& cancellation={}) {
    auto layout=install::InstallLayout::Create(options.root); if(!layout)return layout.error();
    auto separate=install::ValidateSeparateRoots(*layout,options.data);if(!separate)return separate;
    install::InstallOptions policy;
    policy.startMenuShortcut=!options.noShortcuts; policy.desktopShortcut=options.desktopShortcut; policy.startWithWindows=options.startup; policy.allowDowngrade=options.allowDowngrade;
    policy.cancellation=cancellation;
#ifdef AETHER_SETUP_TEST_MODE
    // This switch does not exist in the production setup binary.
    IsolatedProcesses isolated;
    if(options.testNoRegistration) { policy.registerInstallation=false; policy.processes=&isolated;policy.startMenuShortcut=false;policy.desktopShortcut=false;policy.startWithWindows=false; }
#endif
    const auto package=platform::CurrentExecutablePath();
    std::shared_ptr<settings::IStartupRegistry> startupRegistry;
    std::optional<std::wstring> previousRun;
    bool applyStartup=options.startupExplicit && !options.uninstall;
    bool preserveStartupRun=false;
#ifdef AETHER_SETUP_TEST_MODE
    if(options.testNoRegistration)applyStartup=false;
#endif
    std::string taskbarNote;
    if(applyStartup) {
        auto registry=settings::MakeWindowsStartupRegistry({policy.registry.run,policy.registry.startupApproval});if(!registry)return registry.error();startupRegistry=registry.value();
        auto previous=startupRegistry->ReadRun();if(!previous)return previous.error();previousRun=std::move(previous.value());
        if(policy.startWithWindows) {
            auto state=settings::StartupRegistration(startupRegistry).IsEnabled(layout->Launcher());if(!state)return state.error();
            if(*state==settings::StartupState::DisabledByUser || *state==settings::StartupState::UnknownApproval) {
                policy.startWithWindows=false;
                preserveStartupRun=true;
                taskbarNote="Windows startup approval was kept off. Enable Aether Shell in Task Manager before enabling startup.\n";
            }
        }
    }
    // D21: repair and uninstall restore a taskbar that Aether Shell left hidden when both the app and its
    // recovery helper failed. Only a saved baseline triggers this, so settings and other installs are untouched.
    // A running instance and its helper own the taskbar; the operation's exit request makes them restore it.
    if((options.repair || options.uninstall) && !platform::InstanceChannel::FindListenerProcessId(L"AetherShell")) {
        auto recovered=platform::RecoverTaskbars(platform::TaskbarBaselineFile(options.data/L"config"),false);
        if(!recovered)taskbarNote="The Windows taskbar could not be restored automatically; restart Windows Explorer from Task Manager.\n";
        else if(*recovered!=platform::TaskbarRecovery::NothingToRestore)taskbarNote="The Windows taskbar was restored.\n";
    }
    auto result=options.uninstall ? install::Uninstall(*layout,options.data,options.removeData,policy) :
        options.repair ? install::RepairPackage(package,*layout,policy) : install::InstallPackage(package,*layout,policy);
    if(result && applyStartup) {
        if(!policy.startWithWindows && !preserveStartupRun) {
            auto disabled=settings::StartupRegistration(startupRegistry).DisableIfOwned(layout->Launcher());
            if(!disabled)return disabled;
        }
        auto store=settings::MakeFileSettingsStore(options.data/L"config"/L"settings.json");
        Result<void> saved=store?Ok():Result<void>(store.error());
        if(store) {auto loaded=store.value()->Load();if(!loaded)saved=loaded.error();else {loaded->value.general.startWithWindows=policy.startWithWindows;saved=store.value()->Save(loaded->value,false);}}
        if(!saved) {auto restored=previousRun?startupRegistry->WriteRun(*previousRun):startupRegistry->DeleteRun();result=restored?saved:restored;}
    }
    // The committed installation stays usable without the shortcut, so a failure is only logged.
    if(result && (options.uninstall || !options.noShortcuts)) {
        if(auto folder=RestoreShortcutFolder(options); folder) {
            if(!RestoreTaskbarShortcut(*layout,*folder,options.uninstall))
                taskbarNote+=options.uninstall?"The Start menu shortcut \"Restore Windows taskbar\" could not be removed.\n":
                    "The Start menu shortcut \"Restore Windows taskbar\" could not be created; use Settings > Desktop instead.\n";
        }
    }
    if(!options.uninstall || !options.removeData || !result) {
        (void)fs::EnsureDirectory(options.log.parent_path());
        (void)fs::WriteFileAtomic(options.log,(result ? std::string("Aether Shell setup completed successfully.\n") : "Aether Shell setup failed. Error category: "+std::string(ToString(result.error().code))+"\n")+taskbarNote);
    }
    if(!result || options.uninstall)return result;
    bool launch=!options.noLaunch;
#ifdef AETHER_SETUP_TEST_MODE
    if(options.testNoRegistration)launch=false;
#endif
    if(launch) { auto launched=install::LaunchInstalledShell(*layout,{L"--data-dir",options.data.native()}); if(!launched)return launched.error(); }
    return Ok();
}

constexpr wchar_t kLicense[] = LR"MIT(MIT License

Copyright (c) 2026 Ibrahim Bouillet

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.)MIT";

class Wizard {
public:
    explicit Wizard(Options options):options_(std::move(options)),worker_({1,L"Aether setup",ComApartment::SingleThreaded}){}
    ~Wizard(){*alive_=false;worker_.Shutdown();if(window_)::DestroyWindow(window_);if(font_)::DeleteObject(font_);dispatcher_.Shutdown();}
    int Run() {
        if(!dispatcher_.Initialize())return 1;
        WNDCLASSEXW wc{sizeof(wc)};wc.lpfnWndProc=&Proc;wc.hInstance=::GetModuleHandleW(nullptr);wc.hCursor=::LoadCursorW(nullptr,IDC_ARROW);wc.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);wc.lpszClassName=L"AetherShell.Setup";
        wc.hIcon=::LoadIconW(wc.hInstance,L"IDI_AETHER");
        if(!platform::RegisterWindowClassOnce(wc))return 1;
        dpi_=::GetDpiForSystem();UpdateFont();
        window_=::CreateWindowExW(WS_EX_CONTROLPARENT,wc.lpszClassName,L"Aether Shell Setup",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX|WS_VSCROLL|WS_HSCROLL,CW_USEDEFAULT,CW_USEDEFAULT,Px(680),Px(570),nullptr,nullptr,wc.hInstance,this);
        if(!window_)return 1;platform::ApplyImmersiveDarkMode(window_,platform::SystemPrefersDarkApps());
        dpi_=::GetDpiForWindow(window_);if(!dpi_)dpi_=96;UpdateFont();
        RECT initial{};::GetWindowRect(window_,&initial);Constrain(initial);Build();::ShowWindow(window_,SW_SHOWNORMAL);
        loop_.AddMessageFilter([this](MSG& msg){const bool handled=::IsDialogMessageW(window_,&msg)!=FALSE;EnsureFocusVisible();return handled;});
        return loop_.Run();
    }
private:
    int Px(int v)const{return ::MulDiv(v,static_cast<int>(dpi_),96);}
    void UpdateFont(){if(font_)::DeleteObject(font_);font_=::CreateFontW(-Px(14),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");}
    void Constrain(RECT requested) {
        MONITORINFO monitor{sizeof(monitor)};if(!::GetMonitorInfoW(::MonitorFromRect(&requested,MONITOR_DEFAULTTONEAREST),&monitor))return;
        const auto& work=monitor.rcWork;const int margin=Px(8);
        const int width=std::min(requested.right-requested.left,work.right-work.left-margin*2);
        const int height=std::min(requested.bottom-requested.top,work.bottom-work.top-margin*2);
        const int left=std::clamp(requested.left,work.left+margin,work.right-margin-width),top=std::clamp(requested.top,work.top+margin,work.bottom-margin-height);
        ::SetWindowPos(window_,nullptr,left,top,width,height,SWP_NOZORDER|SWP_NOACTIVATE);
    }
    void ScrollTo(int position,int horizontal=-1) {
        RECT client{};::GetClientRect(window_,&client);const int maximum=std::max(0,Px(530)-static_cast<int>(client.bottom));
        scroll_=std::clamp(position,0,maximum);SCROLLINFO info{sizeof(info),SIF_RANGE|SIF_PAGE|SIF_POS};info.nMin=0;info.nMax=Px(530)-1;info.nPage=static_cast<UINT>(std::max(0L,client.bottom));info.nPos=scroll_;::SetScrollInfo(window_,SB_VERT,&info,TRUE);
        scrollX_=std::clamp(horizontal<0?scrollX_:horizontal,0,std::max(0,Px(644)-static_cast<int>(client.right)));info.nMax=Px(644)-1;info.nPage=static_cast<UINT>(std::max(0L,client.right));info.nPos=scrollX_;::SetScrollInfo(window_,SB_HORZ,&info,TRUE);
        for(std::size_t i=0;i<children_.size();++i){const auto& r=locations_[i];::SetWindowPos(children_[i],nullptr,Px(r.left)-scrollX_,Px(r.top)-scroll_,Px(r.right-r.left),Px(r.bottom-r.top),SWP_NOZORDER|SWP_NOACTIVATE);}
    }
    void EnsureFocusVisible() {
        const HWND focus=::GetFocus();if(!focus || !::IsChild(window_,focus))return;
        RECT target{},client{};::GetWindowRect(focus,&target);::MapWindowPoints(nullptr,window_,reinterpret_cast<POINT*>(&target),2);::GetClientRect(window_,&client);
        int vertical=scroll_,horizontal=scrollX_;
        if(target.top<Px(8))vertical+=target.top-Px(8);else if(target.bottom>client.bottom-Px(8))vertical+=target.bottom-client.bottom+Px(8);
        if(target.left<Px(8))horizontal+=target.left-Px(8);else if(target.right>client.right-Px(8))horizontal+=target.right-client.right+Px(8);
        if(vertical!=scroll_ || horizontal!=scrollX_)ScrollTo(vertical,horizontal);
    }
    void CaptureControls() {
        if(folder_ && ::IsWindow(folder_)){std::wstring path(static_cast<std::size_t>(::GetWindowTextLengthW(folder_))+1,L'\0');::GetWindowTextW(folder_,path.data(),static_cast<int>(path.size()));path.resize(path.size()-1);options_.root=path;}
        if(startup_ && ::IsWindow(startup_))options_.startup=::SendMessageW(startup_,BM_GETCHECK,0,0)==BST_CHECKED;
        if(shortcut_ && ::IsWindow(shortcut_))options_.noShortcuts=::SendMessageW(shortcut_,BM_GETCHECK,0,0)!=BST_CHECKED;
        if(desktop_ && ::IsWindow(desktop_))options_.desktopShortcut=::SendMessageW(desktop_,BM_GETCHECK,0,0)==BST_CHECKED;
        if(removeData_ && ::IsWindow(removeData_))options_.removeData=::SendMessageW(removeData_,BM_GETCHECK,0,0)==BST_CHECKED;
        if(launch_ && ::IsWindow(launch_))options_.noLaunch=::SendMessageW(launch_,BM_GETCHECK,0,0)!=BST_CHECKED;
    }
    HWND Add(const wchar_t* cls,const std::wstring& value,DWORD style,int x,int y,int w,int h,int id=0) {
        if(cls==std::wstring_view(L"BUTTON"))style|=BS_NOTIFY;
        HWND hwnd=::CreateWindowExW(cls==std::wstring_view(L"EDIT")?WS_EX_CLIENTEDGE:0,cls,value.c_str(),WS_CHILD|WS_VISIBLE|style,Px(x),Px(y),Px(w),Px(h),window_,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),::GetModuleHandleW(nullptr),nullptr);
        ::SendMessageW(hwnd,WM_SETFONT,reinterpret_cast<WPARAM>(font_),TRUE);children_.push_back(hwnd);locations_.push_back({x,y,x+w,y+h});return hwnd;
    }
    void Build() {
        for(auto child:children_)::DestroyWindow(child);children_.clear();locations_.clear();folder_=startup_=launch_=shortcut_=desktop_=removeData_=nullptr;
        Add(L"STATIC",L"Aether Shell  "+std::wstring(build::kVersionW),0,28,24,600,32);
        if(page_==0) {
            Add(L"STATIC",L"A native Dock and Top Status Bar for Windows",0,28,82,600,34);
            Add(L"STATIC",L"While Aether Shell runs, its Dock replaces the Windows taskbar.\r\nQuit Aether Shell, turn this off in Settings > Desktop, or open\r\n\"Restore Windows taskbar\" in the Start menu to get it back.\r\nInstalls for your account without administrator rights.\r\n\r\nNew installations start at sign-in by default. You can clear that\r\nchoice before installing, or turn it off later in Settings.\r\nExisting startup choices and Task Manager disables are preserved.",0,28,132,600,190);
            Add(L"STATIC",options_.uninstall?L"Uninstall keeps your settings and data unless you choose to remove them.":options_.repair?L"Repair restores this package's files and keeps your settings. If Aether Shell left the Windows taskbar hidden, repair restores it.":L"Installing over an existing version keeps your settings.\r\nAn older package is rejected unless you explicitly permit a downgrade.",0,28,335,600,75);
            Add(L"BUTTON",L"Next",WS_TABSTOP|BS_DEFPUSHBUTTON,500,468,125,32,1);
        } else if(page_==1) {
            Add(L"STATIC",L"MIT License",0,28,82,600,26);
            Add(L"EDIT",kLicense,WS_TABSTOP|WS_VSCROLL|ES_MULTILINE|ES_READONLY,28,118,600,320);
            Add(L"BUTTON",L"Back",WS_TABSTOP,360,468,125,32,8);
            Add(L"BUTTON",L"Continue",WS_TABSTOP|BS_DEFPUSHBUTTON,500,468,125,32,1);
        } else if(page_==2) {
            Add(L"STATIC",L"Install folder",0,28,84,600,25);
            folder_=Add(L"EDIT",options_.root.wstring(),WS_TABSTOP|ES_AUTOHSCROLL,28,114,480,32,20);
            Add(L"BUTTON",L"Browse…",WS_TABSTOP,520,114,108,32,9);
            if(options_.uninstall) {
                removeData_=Add(L"BUTTON",L"Also remove my settings, cache and logs (cannot be undone)",WS_TABSTOP|BS_AUTOCHECKBOX,28,180,600,34,24);
                ::SendMessageW(removeData_,BM_SETCHECK,options_.removeData?BST_CHECKED:BST_UNCHECKED,0);
                Add(L"STATIC",L"Setup will close Aether Shell gracefully and remove its installed files,\r\nshortcuts, startup entry and Installed apps registration.\r\nOther applications and Windows settings are left alone.",0,28,245,600,90);
            } else {
                shortcut_=Add(L"BUTTON",L"Create Start menu shortcuts (Aether Shell and Restore Windows taskbar)",WS_TABSTOP|BS_AUTOCHECKBOX,28,170,600,28,23);
                ::SendMessageW(shortcut_,BM_SETCHECK,options_.noShortcuts?BST_UNCHECKED:BST_CHECKED,0);
                desktop_=Add(L"BUTTON",L"Create a desktop shortcut",WS_TABSTOP|BS_AUTOCHECKBOX,28,210,600,28,25);
                ::SendMessageW(desktop_,BM_SETCHECK,options_.desktopShortcut?BST_CHECKED:BST_UNCHECKED,0);
                startup_=Add(L"BUTTON",L"Start Aether Shell when I sign in (recommended)",WS_TABSTOP|BS_AUTOCHECKBOX,28,250,600,28,21);
                ::SendMessageW(startup_,BM_SETCHECK,options_.startup?BST_CHECKED:BST_UNCHECKED,0);
                Add(L"STATIC",L"Installing accepts the startup choice above. Clear it to opt out.\r\nThe launcher can restart Aether after a crash, with a bounded retry limit.\r\nQuit and Restore Windows taskbar remain available. Weather is off by default.\r\nUnsigned binaries may trigger a Windows SmartScreen warning.",0,28,300,600,100);
            }
            Add(L"BUTTON",L"Back",WS_TABSTOP,360,468,125,32,8);
            Add(L"BUTTON",options_.uninstall?L"Uninstall":options_.repair?L"Repair":L"Install",WS_TABSTOP|BS_DEFPUSHBUTTON,500,468,125,32,2);
        } else if(page_==3) {
            Add(L"STATIC",options_.uninstall?L"Removing this installation…":L"Verifying and installing payload files…",0,28,110,600,60);
            Add(PROGRESS_CLASSW,L"",PBS_MARQUEE,28,200,600,24,30);::SendMessageW(children_.back(),PBM_SETMARQUEE,TRUE,30);
            Add(L"STATIC",L"Please wait while the active operation completes.\r\nCancel is available before activation; then setup finishes safely.",0,28,265,600,80);
            if(!options_.uninstall)Add(L"BUTTON",L"Cancel",WS_TABSTOP,500,468,125,32,4);
        } else {
            Add(L"STATIC",success_?(options_.uninstall?L"Aether Shell was uninstalled.":L"Aether Shell is ready."):L"Setup could not complete.",0,28,100,600,55);
            Add(L"STATIC",success_?(options_.uninstall?L"Your data was kept unless you explicitly chose its removal.":L"You can repair or uninstall from Windows Installed apps."):L"Setup stopped safely. Retry after checking file access and disk space.\r\nError category: "+error_,0,28,175,600,90);
            if(!success_) Add(L"EDIT",options_.log.wstring(),WS_TABSTOP|ES_AUTOHSCROLL|ES_READONLY,28,280,600,32);
            if(success_ && !options_.uninstall) {launch_=Add(L"BUTTON",L"Launch Aether Shell",WS_TABSTOP|BS_AUTOCHECKBOX,28,285,600,32,22);::SendMessageW(launch_,BM_SETCHECK,options_.noLaunch?BST_UNCHECKED:BST_CHECKED,0);}
            Add(L"BUTTON",L"Close",WS_TABSTOP|BS_DEFPUSHBUTTON,500,468,125,32,3);
        }
        ScrollTo(scroll_);
    }
    void Browse() {
        winrt::com_ptr<IFileOpenDialog> dialog;
        if(FAILED(::CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(dialog.put()))))return;
        DWORD flags=0;dialog->GetOptions(&flags);dialog->SetOptions(flags|FOS_PICKFOLDERS|FOS_FORCEFILESYSTEM);dialog->SetTitle(L"Choose the Aether Shell installation folder");
        if(FAILED(dialog->Show(window_)))return;
        winrt::com_ptr<IShellItem> item;if(FAILED(dialog->GetResult(item.put())))return;
        PWSTR path=nullptr;if(SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH,&path))) {::SetWindowTextW(folder_,path);::CoTaskMemFree(path);}
    }
    void Install() {
        std::wstring path(static_cast<std::size_t>(::GetWindowTextLengthW(folder_))+1,L'\0');::GetWindowTextW(folder_,path.data(),static_cast<int>(path.size()));path.resize(path.size()-1);
        options_.root=path;
        if(options_.uninstall)options_.removeData=::SendMessageW(removeData_,BM_GETCHECK,0,0)==BST_CHECKED;
        else {options_.startup=::SendMessageW(startup_,BM_GETCHECK,0,0)==BST_CHECKED;options_.startupExplicit=true;options_.noShortcuts=::SendMessageW(shortcut_,BM_GETCHECK,0,0)!=BST_CHECKED;options_.desktopShortcut=::SendMessageW(desktop_,BM_GETCHECK,0,0)==BST_CHECKED;}
        if(!options_.root.is_absolute()){::MessageBoxW(window_,L"Choose an absolute install folder.",L"Aether Shell",MB_OK|MB_ICONWARNING);return;}
        page_=3;Build();const auto weak=std::weak_ptr<bool>(alive_);auto operation=options_;operation.noLaunch=true;
        worker_.Post([this,weak,operation]{auto result=Perform(operation,cancellation_.Token());dispatcher_.Post([this,weak,result=std::move(result)]{if(auto live=weak.lock();live&&*live){success_=result.ok();if(!result)error_=text::Widen(ToString(result.error().code));page_=4;Build();}});});
    }
    void Finish() {
#ifdef AETHER_SETUP_TEST_MODE
        if(options_.testNoRegistration){::PostQuitMessage(success_?0:1);return;}
#endif
        if(success_ && !options_.uninstall && launch_ && ::SendMessageW(launch_,BM_GETCHECK,0,0)==BST_CHECKED) {
            ::EnableWindow(window_,FALSE);const auto root=options_.root;
            const auto data=options_.data;worker_.Post([this,root,data]{auto layout=install::InstallLayout::Create(root);if(layout)(void)install::LaunchInstalledShell(*layout,{L"--data-dir",data.native()});dispatcher_.Post([]{::PostQuitMessage(0);});});
        } else ::PostQuitMessage(success_?0:1);
    }
    static LRESULT CALLBACK Proc(HWND hwnd,UINT msg,WPARAM wParam,LPARAM lParam) {
        auto* self=reinterpret_cast<Wizard*>(::GetWindowLongPtrW(hwnd,GWLP_USERDATA));
        if(msg==WM_NCCREATE){self=static_cast<Wizard*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);self->window_=hwnd;::SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
        if(self){try{
#ifdef AETHER_SETUP_TEST_MODE
            // These private messages manipulate only this test executable's own window. No
            // global display/input state is changed by the viewport integration fixture.
            if(msg==WM_APP+71 && self->options_.testNoRegistration){const UINT dpi=static_cast<UINT>(wParam);if(dpi<96 || dpi>384)return 0;RECT r{};::GetWindowRect(hwnd,&r);r.right=r.left+::MulDiv(680,static_cast<int>(dpi),96);r.bottom=r.top+::MulDiv(570,static_cast<int>(dpi),96);::SendMessageW(hwnd,WM_DPICHANGED,MAKEWPARAM(dpi,dpi),reinterpret_cast<LPARAM>(&r));return 0;}
            if(msg==WM_APP+72 && self->options_.testNoRegistration){::SetFocus(::GetDlgItem(hwnd,static_cast<int>(wParam)));self->EnsureFocusVisible();return 0;}
#endif
            if(msg==WM_COMMAND){if(HIWORD(wParam)==BN_SETFOCUS || HIWORD(wParam)==EN_SETFOCUS){self->EnsureFocusVisible();return 0;}if(HIWORD(wParam)!=BN_CLICKED)return 0;switch(LOWORD(wParam)){case 1:self->CaptureControls();self->scroll_=self->scrollX_=0;++self->page_;self->Build();return 0;case 2:self->Install();return 0;case 3:self->Finish();return 0;case 4:self->cancellation_.Cancel();::EnableWindow(::GetDlgItem(hwnd,4),FALSE);return 0;case 8:self->CaptureControls();self->scroll_=self->scrollX_=0;--self->page_;self->Build();return 0;case 9:self->Browse();return 0;}}
            if(msg==WM_DPICHANGED){self->CaptureControls();const int oldDpi=static_cast<int>(self->dpi_);self->dpi_=HIWORD(wParam);if(!self->dpi_)self->dpi_=96;self->scroll_=::MulDiv(self->scroll_,static_cast<int>(self->dpi_),oldDpi);self->scrollX_=::MulDiv(self->scrollX_,static_cast<int>(self->dpi_),oldDpi);const auto& r=*reinterpret_cast<RECT*>(lParam);self->Constrain(r);self->UpdateFont();self->Build();return 0;}
            if(msg==WM_SIZE){self->ScrollTo(self->scroll_);return 0;}
            if(msg==WM_MOUSEWHEEL){self->ScrollTo(self->scroll_-static_cast<short>(HIWORD(wParam))*self->Px(48)/WHEEL_DELTA);return 0;}
            if(msg==WM_VSCROLL){SCROLLINFO info{sizeof(info),SIF_TRACKPOS};::GetScrollInfo(hwnd,SB_VERT,&info);RECT client{};::GetClientRect(hwnd,&client);int next=self->scroll_;switch(LOWORD(wParam)){case SB_LINEUP:next-=self->Px(24);break;case SB_LINEDOWN:next+=self->Px(24);break;case SB_PAGEUP:next-=client.bottom;break;case SB_PAGEDOWN:next+=client.bottom;break;case SB_THUMBTRACK:case SB_THUMBPOSITION:next=info.nTrackPos;break;case SB_TOP:next=0;break;case SB_BOTTOM:next=INT_MAX;break;}self->ScrollTo(next);return 0;}
            if(msg==WM_HSCROLL){SCROLLINFO info{sizeof(info),SIF_TRACKPOS};::GetScrollInfo(hwnd,SB_HORZ,&info);RECT client{};::GetClientRect(hwnd,&client);int next=self->scrollX_;switch(LOWORD(wParam)){case SB_LINELEFT:next-=self->Px(24);break;case SB_LINERIGHT:next+=self->Px(24);break;case SB_PAGELEFT:next-=client.right;break;case SB_PAGERIGHT:next+=client.right;break;case SB_THUMBTRACK:case SB_THUMBPOSITION:next=info.nTrackPos;break;case SB_LEFT:next=0;break;case SB_RIGHT:next=INT_MAX;break;}self->ScrollTo(self->scroll_,next);return 0;}
            if(msg==WM_CLOSE){if(self->page_==3){::MessageBoxW(hwnd,L"Wait for the active installation operation to finish.",L"Aether Shell",MB_OK);return 0;}::PostQuitMessage(0);return 0;}
        }catch(...){::PostQuitMessage(1);return 0;}}
        return ::DefWindowProcW(hwnd,msg,wParam,lParam);
    }
    Options options_;WorkerPool worker_;platform::Win32UiDispatcher dispatcher_;platform::MessageLoop loop_;
    HWND window_=nullptr,folder_=nullptr,startup_=nullptr,launch_=nullptr,shortcut_=nullptr,desktop_=nullptr,removeData_=nullptr;HFONT font_=nullptr;UINT dpi_=96;int page_=0;bool success_=false;
    std::wstring error_;std::vector<HWND> children_;std::vector<RECT> locations_;int scroll_=0,scrollX_=0;std::shared_ptr<bool> alive_=std::make_shared<bool>(true);
    CancellationSource cancellation_;
};
}

int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int) {
    try {
        auto parsed=Parse();if(!parsed)return 2;
        aether::WorkerPool bootstrap({1,L"Aether setup bootstrap",aether::ComApartment::None});
        std::promise<aether::Result<void>> promise;auto future=promise.get_future();
        bool relocated=false;
        bootstrap.Post([&]{try{auto resolved=Resolve(*parsed);if(resolved){auto handoff=RelocateMaintenance(*parsed);if(!handoff)resolved=handoff.error();else relocated=handoff.value();}promise.set_value(resolved);}catch(...){promise.set_value(aether::MakeError(aether::ErrorCode::Io,"setup paths unavailable"));}});
        auto resolved=future.get();bootstrap.Shutdown();if(!resolved)return 2;
        if(relocated)return 0;
        aether::platform::EnsurePerMonitorDpiAwareness();aether::platform::ComApartment apartment;
        INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES|ICC_PROGRESS_CLASS};::InitCommonControlsEx(&controls);
        if(parsed->silent)return Perform(*parsed)?0:1;
        Wizard wizard(std::move(*parsed));return wizard.Run();
    } catch (...) {return 1;}
}
