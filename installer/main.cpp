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
#include "graphics/Glyphs.h"
#include "graphics/SystemPalette.h"
#include "ui/form/FormScrollView.h"
#include "ui/form/FormSurface.h"

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
#include <optional>
#include <cstddef>
#include <algorithm>
#include <climits>

namespace {
using namespace aether;
struct Options {
    std::filesystem::path root, data, log;
    bool silent=false, noLaunch=false, noShortcuts=false, startup=false, repair=false, uninstall=false, removeData=false, desktopShortcut=false, allowDowngrade=false;
    bool startupExplicit=false;
    bool existingInstall=false;  // presentation only: the startup choice shown comes from an existing installation
    std::uintptr_t maintenanceParent=0;
    std::string maintenanceToken;
#ifdef AETHER_SETUP_TEST_MODE
    bool testNoRegistration=false;
    std::filesystem::path testShortcutDir;  // replaces the Start menu folder for the restore shortcut
    int testTheme=0;  // 0 follows Windows, 1 light, 2 dark: deterministic wizard renders in fixtures
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
        else if(value==L"--test-theme") {
            if(++i>=values.size() || (values[i]!=L"light" && values[i]!=L"dark"))return MakeError(ErrorCode::InvalidArgument,"test theme is missing");
            options.testTheme=values[i]==L"light"?1:2;
        }
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
        options.existingInstall=installed || options.repair;
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

// The setup wizard on the shared Aether form presentation (ui/form): one window with a fixed brand
// header and step indicator, a scrolling page area and a fixed footer for status and navigation.
// Every native control keeps its id, class and messages, so keyboard, screen readers and the
// isolated viewport fixtures drive the same buttons and check boxes as before. When the
// presentation cannot attach (no renderer at all), the same controls are shown with stock painting.
namespace form = aether::ui::form;

class Wizard {
public:
    explicit Wizard(Options options):options_(std::move(options)),worker_({1,L"Aether setup",ComApartment::SingleThreaded}){}
    ~Wizard(){*alive_=false;worker_.Shutdown();if(window_)::DestroyWindow(window_);dispatcher_.Shutdown();}
    int Run() {
        if(!dispatcher_.Initialize())return 1;
        WNDCLASSEXW wc{sizeof(wc)};wc.lpfnWndProc=&Proc;wc.hInstance=::GetModuleHandleW(nullptr);wc.hCursor=::LoadCursorW(nullptr,IDC_ARROW);wc.lpszClassName=L"AetherShell.Setup";
        wc.hIcon=::LoadIconW(wc.hInstance,L"IDI_AETHER");
        if(!platform::RegisterWindowClassOnce(wc))return 1;
        RefreshTheme();
        const UINT systemDpi=::GetDpiForSystem();
        window_=::CreateWindowExW(WS_EX_CONTROLPARENT,wc.lpszClassName,L"Aether Shell Setup",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,
            ::MulDiv(kWidth,static_cast<int>(systemDpi),96),::MulDiv(kHeight,static_cast<int>(systemDpi),96),nullptr,nullptr,wc.hInstance,this);
        if(!window_)return 1;
        if(auto attached=surface_.Attach(window_,tokens_,motion_);attached) {
            styled_=true;
            surface_.SetDecorator([this](ID2D1DeviceContext* target,const D2D1_RECT_F&,const graphics::DesignTokens& tokens){PaintHeader(target,tokens);});
            if(!scroll_.Create(surface_,window_,kScrollId)){surface_.Detach();styled_=false;}
        }
        // A renderer-free FormSurface already supplies the supported opaque native fallback.
        // A failed attachment/scroll host is an initialization failure, not a usable wizard.
        if(!styled_){::MessageBoxW(window_,L"Setup could not create its interface. Close it and try again, or use the documented silent setup options.",L"Aether Shell Setup",MB_OK|MB_ICONERROR);return 1;}
        CreateFrame();
        RECT initial{};::GetWindowRect(window_,&initial);Constrain(initial);Build();::ShowWindow(window_,SW_SHOWNORMAL);
        // IsDialogMessage claims every message of the window; only keyboard input may move the page to
        // the focus, so a wheel or scroll-bar position survives the next mouse move.
        loop_.AddMessageFilter([this](MSG& msg){const bool handled=::IsDialogMessageW(window_,&msg)!=FALSE;if(handled && (msg.message==WM_KEYDOWN || msg.message==WM_SYSKEYDOWN))EnsureFocusVisible();return handled;});
        return loop_.Run();
    }
private:
    static constexpr int kWidth=720,kHeight=580;
    static constexpr int kScrollId=40,kTitleId=41,kStepId=42,kStatusId=43;
    static constexpr int kPages=5;

    void RefreshTheme() {
        graphics::AppearanceOptions appearance;
#ifdef AETHER_SETUP_TEST_MODE
        if(options_.testTheme==1)appearance.theme=graphics::ThemePreference::Light;
        if(options_.testTheme==2)appearance.theme=graphics::ThemePreference::Dark;
#endif
        tokens_=graphics::MakeAppDesignTokens(appearance);
        motion_.reduced=tokens_.mode==graphics::ThemeMode::HighContrast || !platform::SystemAnimationsEnabled();
        if(styled_)surface_.SetTheme(tokens_,motion_);
    }

    // --- geometry ---
    [[nodiscard]] UINT Dpi()const{return styled_?surface_.Dpi():(window_?platform::DpiForWindow(window_):96);}
    [[nodiscard]] int Px(float dip)const{return static_cast<int>(std::lround(dip*static_cast<float>(Dpi())/96.0f));}
    [[nodiscard]] float Dip(int px)const{return static_cast<float>(px)*96.0f/static_cast<float>(Dpi());}
    void Place(HWND control,float left,float top,float right,float bottom) {
        if(!control)return;
        const RECT r{Px(left),Px(top),Px(std::max(left+1.0f,right)),Px(std::max(top+1.0f,bottom))};
        if(styled_ && surface_.Spec(control))surface_.SetFrame(control,r);
        else ::SetWindowPos(control,nullptr,r.left,r.top,r.right-r.left,r.bottom-r.top,SWP_NOZORDER|SWP_NOACTIVATE);
    }
    [[nodiscard]] float TextHeight(HWND control,form::TextRole role,float width)const {
        if(!control)return 0.0f;
        std::wstring text(static_cast<std::size_t>(::GetWindowTextLengthW(control))+1,L'\0');
        text.resize(static_cast<std::size_t>(::GetWindowTextW(control,text.data(),static_cast<int>(text.size()))));
        if(text.empty())return 0.0f;
        if(!styled_)return 20.0f*static_cast<float>(1+text.size()/72);
        return std::ceil(surface_.MeasureText(text,role,std::max(1.0f,width),true).height);
    }
    [[nodiscard]] float Preferred(HWND control)const{return styled_?surface_.PreferredWidth(control):120.0f;}
    [[nodiscard]] HWND Parent()const{return styled_?scroll_.Canvas():window_;}

    // --- controls ---
    HWND Make(HWND parent,const wchar_t* cls,const std::wstring& text,DWORD style,int id=0,DWORD ex=0) {
        if(cls==std::wstring_view(L"BUTTON"))style|=BS_NOTIFY;
        if(!styled_ && cls==std::wstring_view(L"EDIT"))ex|=WS_EX_CLIENTEDGE;
        HWND hwnd=::CreateWindowExW(ex,cls,text.c_str(),WS_CHILD|WS_VISIBLE|style,0,0,1,1,parent,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),::GetModuleHandleW(nullptr),nullptr);
        if(!hwnd)return nullptr;
        if(!styled_)::SendMessageW(hwnd,WM_SETFONT,reinterpret_cast<WPARAM>(::GetStockObject(DEFAULT_GUI_FONT)),TRUE);
        if(collecting_)pageControls_.push_back(hwnd);  // page content and footer buttons are rebuilt per page
        return hwnd;
    }
    void Style(HWND hwnd,form::ControlSpec spec){if(styled_ && hwnd)(void)surface_.Register(hwnd,spec);}
    HWND Label(HWND parent,const std::wstring& text,form::TextRole role,int id=0,form::Severity severity=form::Severity::None,std::optional<graphics::Glyph> glyph={}) {
        HWND label=Make(parent,L"STATIC",text,(styled_?SS_OWNERDRAW:0u)|SS_NOPREFIX,id);
        form::ControlSpec spec;spec.role=form::ControlRole::Label;spec.text=role;spec.severity=severity;spec.glyph=glyph;
        Style(label,spec);return label;
    }
    HWND Button(HWND parent,const std::wstring& text,int id,form::ButtonStyle style,std::optional<graphics::Glyph> glyph={},DWORD extra=0) {
        HWND button=Make(parent,L"BUTTON",text,WS_TABSTOP|BS_PUSHBUTTON|extra,id);
        form::ControlSpec spec;spec.role=form::ControlRole::Button;spec.button=style;spec.glyph=glyph;Style(button,spec);return button;
    }
    HWND Switch(HWND parent,const std::wstring& text,const std::wstring& description,int id,bool checked,bool check=false) {
        HWND box=Make(parent,L"BUTTON",text,WS_TABSTOP|BS_AUTOCHECKBOX,id);
        ::SendMessageW(box,BM_SETCHECK,checked?BST_CHECKED:BST_UNCHECKED,0);
        form::ControlSpec spec;spec.role=check?form::ControlRole::Check:form::ControlRole::Switch;spec.description=description;Style(box,spec);
        return box;
    }

    // --- frame: header, footer and the status line stay across pages ---
    void CreateFrame() {
        title_=Label(window_,L"Aether Shell",form::TextRole::Title,kTitleId);
        version_=Label(window_,L"Version "+std::wstring(build::kVersionW),form::TextRole::Caption);
        step_=Label(window_,L"",form::TextRole::Caption,kStepId);
        status_=Label(window_,L"",form::TextRole::Status,kStatusId);
    }
    // A multiline EDIT breaks lines only at CR LF; the embedded licence is stored with LF line ends.
    [[nodiscard]] static std::wstring MultilineText(std::wstring_view text) {
        std::wstring result;result.reserve(text.size()+text.size()/32);
        for(const wchar_t c:text){if(c==L'\n' && (result.empty() || result.back()!=L'\r'))result.push_back(L'\r');result.push_back(c);}
        return result;
    }
    [[nodiscard]] static const wchar_t* StepName(int page) {
        switch(page){case 0:return L"Welcome";case 1:return L"Licence";case 2:return L"Options";case 3:return L"Install";default:return L"Done";}
    }
    void PaintHeader(ID2D1DeviceContext* target,const graphics::DesignTokens& tokens) {
        graphics::PaintBrandMark(target,brandMark_,tokens);
        // Step indicator: a dot per step joined by a hairline; done and current steps take the accent.
        if(stepDots_.right<=stepDots_.left)return;
        winrt::com_ptr<ID2D1SolidColorBrush> brush;
        if(FAILED(target->CreateSolidColorBrush(tokens.form.divider.ToD2D(),brush.put())))return;
        const float spacing=(stepDots_.right-stepDots_.left)/static_cast<float>(kPages-1);
        const float y=(stepDots_.top+stepDots_.bottom)/2.0f;
        target->DrawLine(D2D1::Point2F(stepDots_.left,y),D2D1::Point2F(stepDots_.right,y),brush.get(),1.0f);
        for(int i=0;i<kPages;++i) {
            const bool reached=i<=page_;
            brush->SetColor((reached?tokens.accentRoles.fill:tokens.form.switchOffStroke).ToD2D());
            const float radius=i==page_?5.0f:3.5f;
            target->FillEllipse(D2D1::Ellipse(D2D1::Point2F(stepDots_.left+spacing*static_cast<float>(i),y),radius,radius),brush.get());
        }
    }
    void SetStatus(const std::wstring& text,form::Severity severity=form::Severity::None) {
        ::SetWindowTextW(status_,text.c_str());
        if(styled_){form::ControlSpec spec;spec.role=form::ControlRole::Label;spec.text=form::TextRole::Status;spec.severity=severity;surface_.SetSpec(status_,spec);}
        ::InvalidateRect(status_,nullptr,FALSE);
        Layout();
    }

    void Constrain(RECT requested) {
        MONITORINFO monitor{sizeof(monitor)};if(!::GetMonitorInfoW(::MonitorFromRect(&requested,MONITOR_DEFAULTTONEAREST),&monitor))return;
        const auto& work=monitor.rcWork;const int margin=Px(8);
        const int width=std::min(requested.right-requested.left,work.right-work.left-margin*2);
        const int height=std::min(requested.bottom-requested.top,work.bottom-work.top-margin*2);
        const int left=std::clamp(requested.left,work.left+margin,work.right-margin-width),top=std::clamp(requested.top,work.top+margin,work.bottom-margin-height);
        ::SetWindowPos(window_,nullptr,left,top,width,height,SWP_NOZORDER|SWP_NOACTIVATE);
    }
    void EnsureFocusVisible(){if(styled_)scroll_.RevealFocus(Px(12.0f));}
    void CaptureControls() {
        if(folder_ && ::IsWindow(folder_)){std::wstring path(static_cast<std::size_t>(::GetWindowTextLengthW(folder_))+1,L'\0');::GetWindowTextW(folder_,path.data(),static_cast<int>(path.size()));path.resize(path.size()-1);options_.root=path;}
        if(startup_ && ::IsWindow(startup_))options_.startup=::SendMessageW(startup_,BM_GETCHECK,0,0)==BST_CHECKED;
        if(shortcut_ && ::IsWindow(shortcut_))options_.noShortcuts=::SendMessageW(shortcut_,BM_GETCHECK,0,0)!=BST_CHECKED;
        if(desktop_ && ::IsWindow(desktop_))options_.desktopShortcut=::SendMessageW(desktop_,BM_GETCHECK,0,0)==BST_CHECKED;
        if(removeData_ && ::IsWindow(removeData_))options_.removeData=::SendMessageW(removeData_,BM_GETCHECK,0,0)==BST_CHECKED;
        if(launch_ && ::IsWindow(launch_))options_.noLaunch=::SendMessageW(launch_,BM_GETCHECK,0,0)!=BST_CHECKED;
    }

    // --- pages ---
    struct Item { enum Kind{Text,Card,Control,Buttons,Document,Field,Progress} kind=Text; HWND first=nullptr; HWND second=nullptr; form::TextRole role=form::TextRole::Body; float height=0.0f; bool cardStart=false; bool cardEnd=false; };
    void Build() {
        for(HWND child:pageControls_)if(::IsWindow(child))::DestroyWindow(child);
        pageControls_.clear();items_.clear();
        collecting_=true;
        struct StopCollecting{bool& flag;~StopCollecting(){flag=false;}} stop{collecting_};
        folder_=startup_=launch_=shortcut_=desktop_=removeData_=nullptr;primary_=back_=cancel_=nullptr;folderError_=nullptr;
        const HWND parent=Parent();
        const auto text=[&](const std::wstring& value,form::TextRole role,form::Severity severity=form::Severity::None,std::optional<graphics::Glyph> glyph={}) {
            items_.push_back(Item{Item::Text,Label(parent,value,role,0,severity,glyph),nullptr,role});
        };
        const auto card=[&](graphics::Glyph glyph,const std::wstring& heading,const std::wstring& body) {
            Item item{Item::Card,Label(parent,heading,form::TextRole::Strong,0,form::Severity::None,glyph),Label(parent,body,form::TextRole::Helper)};
            items_.push_back(item);
        };
        const auto control=[&](HWND hwnd){items_.push_back(Item{Item::Control,hwnd});};
        const bool repair=options_.repair,uninstall=options_.uninstall;
        if(page_==0) {
            text(uninstall?L"Remove Aether Shell":repair?L"Repair Aether Shell":L"Welcome",form::TextRole::Section);
            text(L"A native Dock and Top Status Bar for Windows, running alongside Explorer.",form::TextRole::Body);
            card(graphics::Glyph::Taskbar,L"Replaces the Windows taskbar while it runs",
                 L"Quit Aether Shell, turn this off in Settings > Desktop, or open \"Restore Windows taskbar\" in the Start menu to get the Windows taskbar back.");
            card(graphics::Glyph::Power,L"Starts when you sign in - your choice",
                 L"New installations start at sign-in by default. You can clear that choice before installing, or turn it off later in Settings. Existing startup choices and Task Manager disables are preserved.");
            card(graphics::Glyph::Shield,L"Installs for your account",
                 L"No administrator rights are needed. Your settings and data stay in your user profile.");
            text(uninstall?L"Uninstall keeps your settings and data unless you choose to remove them.":
                 repair?L"Repair restores this package's files and keeps your settings. If Aether Shell left the Windows taskbar hidden, repair restores it.":
                 L"Installing over an existing version keeps your settings. An older package is rejected unless you explicitly permit a downgrade.",form::TextRole::Helper);
        } else if(page_==1) {
            text(L"MIT License",form::TextRole::Section);
            HWND license=Make(parent,L"EDIT",MultilineText(kLicense),WS_TABSTOP|WS_VSCROLL|ES_MULTILINE|ES_READONLY);
            {form::ControlSpec spec;spec.role=form::ControlRole::Document;Style(license,spec);}
            items_.push_back(Item{Item::Document,license,nullptr,form::TextRole::Body,260.0f});
            text(L"The licence is also installed as LICENSE next to Aether Shell.",form::TextRole::Helper);
        } else if(page_==2) {
            text(uninstall?L"Remove this installation":L"Install location",form::TextRole::Section);
            folder_=Make(parent,L"EDIT",options_.root.wstring(),WS_TABSTOP|ES_AUTOHSCROLL,20);
            {form::ControlSpec spec;spec.role=form::ControlRole::Field;Style(folder_,spec);}
            items_.push_back(Item{Item::Field,folder_,Button(parent,L"&Browse...",9,form::ButtonStyle::Secondary,graphics::Glyph::Folder)});
            folderError_=Label(parent,L"",form::TextRole::Status,0,form::Severity::Error);
            items_.push_back(Item{Item::Text,folderError_,nullptr,form::TextRole::Status});
            if(uninstall) {
                removeData_=Switch(parent,L"Also remove my settings, cache and logs",L"This cannot be undone.",24,options_.removeData,true);
                control(removeData_);
                text(L"Setup will close Aether Shell gracefully and remove its installed files, shortcuts, startup entry and Installed apps registration. Other applications and Windows settings are left alone.",form::TextRole::Helper);
            } else {
                text(L"Shortcuts and startup",form::TextRole::Section);
                shortcut_=Switch(parent,L"Create Start menu shortcuts",L"Aether Shell and Restore Windows taskbar.",23,!options_.noShortcuts);control(shortcut_);
                desktop_=Switch(parent,L"Create a desktop shortcut",L"",25,options_.desktopShortcut);control(desktop_);
                startup_=Switch(parent,L"Start Aether Shell when I sign in",
                    std::wstring(options_.existingInstall?L"Shows your current choice. ":L"Recommended. ")+L"Clear it to opt out; Task Manager disables are respected.",21,options_.startup);
                control(startup_);
                text(L"Installing accepts the startup choice above. The launcher can restart Aether Shell after a crash, with a bounded retry limit. Quit and Restore Windows taskbar remain available. Weather is off by default. Unsigned binaries may trigger a Windows SmartScreen warning.",form::TextRole::Helper);
            }
        } else if(page_==3) {
            text(uninstall?L"Removing this installation...":repair?L"Repairing Aether Shell...":L"Installing Aether Shell...",form::TextRole::Section);
            HWND progress=Make(parent,PROGRESS_CLASSW,L"",PBS_MARQUEE,30);
            {form::ControlSpec spec;spec.role=form::ControlRole::Progress;Style(progress,spec);}
            if(styled_)surface_.SetProgress(progress,std::nullopt);else ::SendMessageW(progress,PBM_SETMARQUEE,motion_.reduced?FALSE:TRUE,30);
            items_.push_back(Item{Item::Progress,progress});
            text(uninstall?L"Closing Aether Shell and removing its files.":L"Verifying the embedded package and installing its files. Progress is shown as activity: setup does not know the remaining time.",form::TextRole::Body);
            text(uninstall?L"Please wait while the removal completes.":L"Cancel is available until the new version is activated; then setup finishes safely.",form::TextRole::Helper);
        } else {
            const bool cancelled=!success_ && errorCode_==ErrorCode::Cancelled;
            const auto severity=success_?form::Severity::Success:cancelled?form::Severity::None:form::Severity::Error;
            items_.push_back(Item{Item::Text,Label(parent,success_?(uninstall?L"Aether Shell was uninstalled.":L"Aether Shell is ready."):cancelled?L"Setup was cancelled.":L"Setup could not complete.",form::TextRole::Status,0,severity),nullptr,form::TextRole::Status});
            text(success_?(uninstall?L"Your data was kept unless you explicitly chose to remove it.":L"You can repair or uninstall it from Windows Settings > Apps > Installed apps."):
                  cancelled?L"The operation was cancelled before activation. Go back to Options to try again.":
                  L"Check file access and free disk space, then try again. If Aether Shell is already present, Repair can restore its files. Error category: "+error_,form::TextRole::Body);
            if(!success_) {
                text(L"Setup log",form::TextRole::Strong);
                HWND log=Make(parent,L"EDIT",options_.log.wstring(),WS_TABSTOP|ES_AUTOHSCROLL|ES_READONLY);
                {form::ControlSpec spec;spec.role=form::ControlRole::Field;Style(log,spec);}
                items_.push_back(Item{Item::Field,log,nullptr});
            }
            if(success_ && !uninstall){launch_=Switch(parent,L"Launch Aether Shell",L"",22,!options_.noLaunch);control(launch_);}
        }
        // Footer navigation. Ids are stable: 1 Next/Continue, 2 Install, 3 Close, 4 Cancel, 8 Back.
        if(page_==1 || page_==2 || (page_==4 && !success_))back_=Button(window_,L"&Back",8,form::ButtonStyle::Secondary);
        if(page_==0)primary_=Button(window_,L"&Next",1,form::ButtonStyle::Primary,{},BS_DEFPUSHBUTTON);
        else if(page_==1)primary_=Button(window_,L"&Continue",1,form::ButtonStyle::Primary,{},BS_DEFPUSHBUTTON);
        else if(page_==2)primary_=Button(window_,uninstall?L"&Uninstall":repair?L"&Repair":L"&Install",2,uninstall?form::ButtonStyle::Danger:form::ButtonStyle::Primary,{},BS_DEFPUSHBUTTON);
        else if(page_==3){if(!uninstall)cancel_=Button(window_,L"Cancel",4,form::ButtonStyle::Secondary);}
        else primary_=Button(window_,L"&Close",3,form::ButtonStyle::Primary,{},BS_DEFPUSHBUTTON);
        ::SetWindowTextW(step_,(L"Step "+std::to_wstring(page_+1)+L" of "+std::to_wstring(kPages)+L": "+StepName(page_)).c_str());
        SetStatus(L"");
        if(styled_){scroll_.ScrollTo(0);Layout();surface_.FadeIn(scroll_.Viewport());}
        else Layout();
        if(primary_)::SetFocus(primary_);else if(cancel_)::SetFocus(cancel_);
        ::InvalidateRect(window_,nullptr,FALSE);
    }

    void Layout() {
        if(!window_)return;
        RECT client{};::GetClientRect(window_,&client);
        const float width=Dip(client.right),height=Dip(client.bottom);
        const auto& f=tokens_.form;
        const float outer=f.outer;
        // Header
        brandMark_=D2D1::RectF(outer,20.0f,outer+40.0f,60.0f);
        Place(title_,outer+52.0f,16.0f,width-outer,46.0f);
        Place(version_,outer+52.0f,46.0f,std::min(width-outer,outer+52.0f+160.0f),64.0f);
        const float dotsWidth=std::min(160.0f,std::max(80.0f,width-outer*2.0f-52.0f-180.0f));
        stepDots_=D2D1::RectF(width-outer-dotsWidth,30.0f,width-outer,40.0f);
        Place(step_,width-outer-std::max(dotsWidth,150.0f),44.0f,width-outer,62.0f);
        const float headerBottom=76.0f;
        // Footer
        const float footerTop=std::max(headerBottom+60.0f,height-f.footerHeight);
        const float buttonTop=footerTop+(f.footerHeight-f.controlHeight)/2.0f;
        float right=width-outer;
        for(HWND button:{primary_,cancel_,back_}) {
            if(!button)continue;
            const float w=Preferred(button);
            Place(button,right-w,buttonTop,right,buttonTop+f.controlHeight);
            right-=w+f.relatedGap+2.0f*f.focusMargin;
        }
        const float statusHeight=std::clamp(TextHeight(status_,form::TextRole::Status,std::max(40.0f,right-outer-28.0f)),18.0f,f.footerHeight-12.0f);
        Place(status_,outer,footerTop+(f.footerHeight-statusHeight)/2.0f,std::max(outer+40.0f,right-f.controlGap),footerTop+(f.footerHeight+statusHeight)/2.0f);
        std::vector<form::FormDivider> dividers{form::FormDivider{RECT{0,Px(footerTop),client.right,Px(footerTop)+1},nullptr},
                                                 form::FormDivider{RECT{0,Px(headerBottom),client.right,Px(headerBottom)+1},nullptr}};
        // Page content
        std::vector<form::FormCard> cards;
        if(styled_) {
            scroll_.SetFrame(RECT{0,Px(headerBottom)+1,client.right,Px(footerTop)});
            const float contentWidth=std::min(Dip(client.right-scroll_.GutterPx())-outer*2.0f+8.0f,620.0f);
            const float left=outer;
            float y=f.sectionGap;
            const Item* previous=nullptr;
            for(auto& item:items_) {
                // Consecutive switch rows share one card, separated by hairlines.
                const bool joined=previous && previous->kind==Item::Control && item.kind==Item::Control && !IsCheck(item) && !IsCheck(*previous);
                if(previous && !joined)y+=item.role==form::TextRole::Section?f.sectionGap:f.relatedGap;
                if(joined)dividers.push_back(form::FormDivider{RECT{Px(left+f.cardPaddingX),Px(y),Px(left+contentWidth-f.cardPaddingX),Px(y)+1},scroll_.Canvas()});
                y+=LayoutItem(item,left,y,contentWidth,cards,joined);
                previous=&item;
            }
            scroll_.SetContentHeight(Px(y+f.sectionGap));
            surface_.SetCards(std::move(cards));
            surface_.SetDividers(std::move(dividers));
        } else {
            float y=headerBottom+12.0f;
            for(auto& item:items_) y+=LayoutItem(item,outer,y,width-outer*2.0f,cards,false)+8.0f;
        }
        ::InvalidateRect(window_,nullptr,FALSE);
    }
    [[nodiscard]] bool IsCheck(const Item& item)const{const auto* spec=styled_?surface_.Spec(item.first):nullptr;return spec && spec->role==form::ControlRole::Check;}
    float LayoutItem(Item& item,float left,float top,float width,std::vector<form::FormCard>& cards,bool joined) {
        const auto& f=tokens_.form;
        const HWND canvas=styled_?scroll_.Canvas():nullptr;
        switch(item.kind) {
            case Item::Text: {
                const float h=std::max(item.role==form::TextRole::Section?22.0f:18.0f,TextHeight(item.first,item.role,width));
                if(item.first==folderError_ && h<=18.0f && ::GetWindowTextLengthW(item.first)==0){::ShowWindow(item.first,SW_HIDE);return -f.relatedGap;}
                ::ShowWindow(item.first,SW_SHOWNA);
                Place(item.first,left,top,left+width,top+h);
                return h;
            }
            case Item::Card: {
                const float inner=width-2.0f*f.cardPaddingX-28.0f;
                const float headingHeight=std::max(20.0f,TextHeight(item.first,form::TextRole::Strong,inner+28.0f));
                const float bodyHeight=TextHeight(item.second,form::TextRole::Helper,inner);
                const float h=f.rowPaddingY*2.0f+headingHeight+f.labelGap+bodyHeight;
                Place(item.first,left+f.cardPaddingX,top+f.rowPaddingY,left+width-f.cardPaddingX,top+f.rowPaddingY+headingHeight);
                Place(item.second,left+f.cardPaddingX+28.0f,top+f.rowPaddingY+headingHeight+f.labelGap,left+width-f.cardPaddingX,top+h-f.rowPaddingY);
                if(canvas)cards.push_back(form::FormCard{RECT{Px(left),Px(top),Px(left+width),Px(top+h)},canvas,false});
                return h;
            }
            case Item::Control: {
                const auto* spec=styled_?surface_.Spec(item.first):nullptr;
                float h=std::max(f.rowMinHeight,TextHeight(item.first,form::TextRole::Body,width-f.switchWidth-f.cardPaddingX*2.0f)+2.0f*f.rowPaddingY);
                if(spec && !spec->description.empty())h=std::max(h,2.0f*f.rowPaddingY+20.0f+f.labelGap+std::ceil(surface_.MeasureText(spec->description,form::TextRole::Helper,width-f.switchWidth-f.cardPaddingX*2.0f-f.controlGap,true).height));
                const bool check=spec && spec->role==form::ControlRole::Check;
                Place(item.first,left+(check?f.cardPaddingX:0.0f),top,left+width,top+h);
                if(canvas && !check) {
                    if(joined && !cards.empty())cards.back().px.bottom=Px(top+h);
                    else cards.push_back(form::FormCard{RECT{Px(left),Px(top),Px(left+width),Px(top+h)},canvas,false});
                }
                return h;
            }
            case Item::Field: {
                const float buttonWidth=item.second?Preferred(item.second):0.0f;
                const float fieldRight=left+width-(item.second?buttonWidth+f.relatedGap+2.0f*f.focusMargin:0.0f);
                Place(item.first,left,top,fieldRight,top+f.controlHeight);
                if(item.second)Place(item.second,fieldRight+f.relatedGap+f.focusMargin,top,fieldRight+f.relatedGap+f.focusMargin+buttonWidth,top+f.controlHeight);
                return f.controlHeight;
            }
            case Item::Document:
                Place(item.first,left,top,left+width,top+item.height);
                return item.height;
            case Item::Progress:
                Place(item.first,left,top+8.0f,left+width,top+16.0f);
                return 20.0f;
            default:
                return 0.0f;
        }
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
        if(!options_.root.is_absolute()) {
            // Inline validation: the field is marked and the message stays next to it.
            ::SetWindowTextW(folderError_,L"Choose a complete folder path, for example one that starts with a drive letter.");
            if(styled_)surface_.SetError(folder_,true);
            SetStatus(L"The install folder must be an absolute path.",form::Severity::Error);
            ::SetFocus(folder_);
            return;
        }
        cancellation_=CancellationSource{};
        const auto cancellation=cancellation_.Token();
        page_=3;Build();const auto weak=std::weak_ptr<bool>(alive_);auto operation=options_;operation.noLaunch=true;
#ifdef AETHER_SETUP_TEST_MODE
        const HANDLE gate=options_.testNoRegistration?testOperationGate_.Get():nullptr;
        worker_.Post([this,weak,operation,cancellation,gate]{
            // The isolated retry regression holds its own worker before staging, then Cancel
            // releases it. This seam cannot appear in a production setup executable.
            if(gate){auto release=cancellation.OnCancel([gate]{::SetEvent(gate);});(void)::WaitForSingleObject(gate,10000);}
#else
        worker_.Post([this,weak,operation,cancellation]{
#endif
            auto result=Perform(operation,cancellation);dispatcher_.Post([this,weak,result=std::move(result)]{if(auto live=weak.lock();live&&*live){success_=result.ok();if(!result){errorCode_=result.error().code;error_=text::Widen(ToString(errorCode_));}page_=4;Build();}});});
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
            if(self->styled_){if(auto handled=self->surface_.HandleMessage(msg,wParam,lParam))return *handled;}
#ifdef AETHER_SETUP_TEST_MODE
            // These private messages manipulate only this test executable's own window. No
            // global display/input state is changed by the viewport integration fixture.
            if(msg==WM_APP+71 && self->options_.testNoRegistration){const UINT dpi=static_cast<UINT>(wParam);if(dpi<96 || dpi>384)return 0;RECT r{};::GetWindowRect(hwnd,&r);r.right=r.left+::MulDiv(kWidth,static_cast<int>(dpi),96);r.bottom=r.top+::MulDiv(kHeight,static_cast<int>(dpi),96);::SendMessageW(hwnd,WM_DPICHANGED,MAKEWPARAM(dpi,dpi),reinterpret_cast<LPARAM>(&r));return 0;}
            if(msg==WM_APP+72 && self->options_.testNoRegistration){self->FocusById(static_cast<int>(wParam));self->EnsureFocusVisible();return 0;}
            if(msg==WM_APP+73 && self->options_.testNoRegistration)return self->styled_?self->scroll_.Position():0;
            if(msg==WM_APP+74 && self->options_.testNoRegistration && self->page_==2){self->testOperationGate_.Reset(::CreateEventW(nullptr,TRUE,FALSE,nullptr));return self->testOperationGate_?1:0;}
            if(msg==WM_APP+75 && self->options_.testNoRegistration && self->page_==3 && self->testOperationGate_)return ::SetEvent(self->testOperationGate_.Get());
#endif
            if(msg==WM_COMMAND){if(HIWORD(wParam)==BN_SETFOCUS || HIWORD(wParam)==EN_SETFOCUS){self->EnsureFocusVisible();return 0;}if(HIWORD(wParam)!=BN_CLICKED)return 0;switch(LOWORD(wParam)){case 1:self->CaptureControls();++self->page_;self->Build();return 0;case 2:self->Install();return 0;case 3:self->Finish();return 0;case 4:self->cancellation_.Cancel();if(HWND cancel=::GetDlgItem(hwnd,4)){::SetWindowTextW(cancel,L"Cancelling...");::EnableWindow(cancel,FALSE);}self->SetStatus(L"Setup stops at the next safe point.",form::Severity::Busy);return 0;case 8:self->CaptureControls();self->page_=self->page_==4?2:self->page_-1;self->Build();return 0;case 9:self->Browse();return 0;}}
            if(msg==WM_GETMINMAXINFO){auto* info=reinterpret_cast<MINMAXINFO*>(lParam);const UINT dpi=self->Dpi();RECT frame{0,0,::MulDiv(420,static_cast<int>(dpi),96),::MulDiv(360,static_cast<int>(dpi),96)};::AdjustWindowRectExForDpi(&frame,WS_OVERLAPPEDWINDOW,FALSE,WS_EX_CONTROLPARENT,dpi);info->ptMinTrackSize=POINT{frame.right-frame.left,frame.bottom-frame.top};return 0;}
            if(msg==WM_DPICHANGED){self->CaptureControls();const auto& r=*reinterpret_cast<RECT*>(lParam);self->Constrain(r);self->Layout();return 0;}
            if(msg==WM_SETTINGCHANGE || msg==WM_THEMECHANGED || msg==WM_SYSCOLORCHANGE){self->RefreshTheme();self->Layout();return 0;}
            if(msg==WM_SIZE){if(wParam!=SIZE_MINIMIZED)self->Layout();return 0;}
            if(msg==WM_MOUSEWHEEL){if(self->styled_)::SendMessageW(self->scroll_.Viewport(),WM_MOUSEWHEEL,wParam,lParam);return 0;}
            if(msg==WM_VSCROLL){if(self->styled_)self->scroll_.HandleScrollCode(LOWORD(wParam),static_cast<short>(HIWORD(wParam)));return 0;}
            if(msg==WM_CLOSE){if(self->page_==3){self->SetStatus(L"Setup is finishing the current step safely; please wait.",form::Severity::Busy);return 0;}::PostQuitMessage(0);return 0;}
            if(msg==WM_DESTROY){self->scroll_.Destroy();self->surface_.Detach();self->styled_=false;self->window_=nullptr;return 0;}
        }catch(...){::PostQuitMessage(1);return 0;}}
        return ::DefWindowProcW(hwnd,msg,wParam,lParam);
    }
    void FocusById(int id) {
        // Controls of the page live inside the scrolling canvas: search the whole window.
        struct Search{int id;HWND found=nullptr;} search{id};
        ::EnumChildWindows(window_,[](HWND child,LPARAM parameter)->BOOL{auto& s=*reinterpret_cast<Search*>(parameter);if(::GetDlgCtrlID(child)==s.id){s.found=child;return FALSE;}return TRUE;},reinterpret_cast<LPARAM>(&search));
        if(search.found)::SetFocus(search.found);
    }
    Options options_;WorkerPool worker_;platform::Win32UiDispatcher dispatcher_;platform::MessageLoop loop_;
    form::FormSurface surface_;form::FormScrollView scroll_;bool styled_=false;bool collecting_=false;
    graphics::DesignTokens tokens_;graphics::MotionPolicy motion_;
    HWND window_=nullptr,folder_=nullptr,startup_=nullptr,launch_=nullptr,shortcut_=nullptr,desktop_=nullptr,removeData_=nullptr;
    HWND title_=nullptr,version_=nullptr,step_=nullptr,status_=nullptr,primary_=nullptr,back_=nullptr,cancel_=nullptr,folderError_=nullptr;
    D2D1_RECT_F brandMark_{},stepDots_{};
    int page_=0;bool success_=false;
    ErrorCode errorCode_=ErrorCode::Unknown;
    std::wstring error_;std::vector<HWND> pageControls_;std::vector<Item> items_;std::shared_ptr<bool> alive_=std::make_shared<bool>(true);
    CancellationSource cancellation_;
#ifdef AETHER_SETUP_TEST_MODE
    platform::UniqueHandle testOperationGate_;
#endif
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
