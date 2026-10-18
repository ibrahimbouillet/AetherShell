// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "install/Install.h"
#include "core/Strings.h"
#include "diagnostics/Logger.h"
#include "platform/windows/SingleInstance.h"
#include "platform/windows/DpiUtilities.h"

#include <commctrl.h>
#include <shellapi.h>
#include <array>
#include <atomic>
#include <map>
#include <mutex>
#include <thread>

namespace {
struct Progress {
    std::atomic<bool> busy{true};
    HWND label=nullptr,bar=nullptr,close=nullptr;
    HFONT font=nullptr;UINT dpi=96;
    std::mutex mutex;
    const wchar_t* outcome=L"";
};
void Layout(HWND window,Progress& data) {
    data.dpi=::GetDpiForWindow(window);if(!data.dpi)data.dpi=96;
    const auto px=[&](int value){return ::MulDiv(value,static_cast<int>(data.dpi),96);};
    HFONT font=::CreateFontW(-px(14),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    for(const auto control:{data.label,data.bar,data.close})if(control)::SendMessageW(control,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
    if(data.font)::DeleteObject(data.font);data.font=font;
    if(data.label)::SetWindowPos(data.label,nullptr,px(20),px(20),px(420),px(44),SWP_NOZORDER|SWP_NOACTIVATE);
    if(data.bar)::SetWindowPos(data.bar,nullptr,px(20),px(70),px(420),px(22),SWP_NOZORDER|SWP_NOACTIVATE);
    if(data.close)::SetWindowPos(data.close,nullptr,px(350),px(112),px(90),px(30),SWP_NOZORDER|SWP_NOACTIVATE);
}
LRESULT CALLBACK WindowProc(HWND window,UINT message,WPARAM wparam,LPARAM lparam) {
    try {
    auto* data=reinterpret_cast<Progress*>(::GetWindowLongPtrW(window,GWLP_USERDATA));
    if(message==WM_NCCREATE) { data=static_cast<Progress*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams); ::SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(data)); }
    if(message==WM_CREATE) {
        data->label=::CreateWindowW(L"STATIC",L"Verifying and applying the Aether Shell update…",WS_CHILD|WS_VISIBLE,20,20,420,32,window,nullptr,nullptr,nullptr);
        data->bar=::CreateWindowW(PROGRESS_CLASSW,nullptr,WS_CHILD|WS_VISIBLE|PBS_MARQUEE,20,66,420,22,window,nullptr,nullptr,nullptr);
        BOOL animation=TRUE;::SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&animation,0);
        ::SendMessageW(data->bar,PBM_SETMARQUEE,animation,30);Layout(window,*data);return 0;
    }
    if(message==WM_APP+1) {
        ::SendMessageW(data->bar,PBM_SETMARQUEE,FALSE,0);
        std::lock_guard lock(data->mutex); ::SetWindowTextW(data->label,data->outcome);
        data->close=::CreateWindowW(L"BUTTON",L"Close",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_DEFPUSHBUTTON,350,110,90,30,window,reinterpret_cast<HMENU>(1),nullptr,nullptr);Layout(window,*data);::SetFocus(data->close);return 0;
    }
    if(message==WM_DPICHANGED && data) {const auto& rect=*reinterpret_cast<RECT*>(lparam);::SetWindowPos(window,nullptr,rect.left,rect.top,rect.right-rect.left,rect.bottom-rect.top,SWP_NOZORDER|SWP_NOACTIVATE);Layout(window,*data);return 0;}
    if(message==WM_COMMAND && LOWORD(wparam)==1) { ::SendMessageW(window,WM_CLOSE,0,0); return 0; }
    if(message==WM_CLOSE && data && data->busy.load()) return 0;
    if(message==WM_DESTROY) { ::PostQuitMessage(0); return 0; }
    if(message==WM_NCDESTROY && data) {if(data->font)::DeleteObject(data->font);data->font=nullptr;::SetWindowLongPtrW(window,GWLP_USERDATA,0);}
    return ::DefWindowProcW(window,message,wparam,lparam);
    }catch(...){if(message==WM_CREATE)return -1;::PostQuitMessage(4);return 0;}
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int show) {
    using namespace aether;
    try {
        int count=0; auto* argv=::CommandLineToArgvW(::GetCommandLineW(),&count); if(!argv) return 2;
        std::map<std::wstring,std::wstring> options; bool apply=false,valid=true;
        for(int i=1;i<count;++i) {
            const std::wstring key=argv[i];
            if(key==L"--apply") { if(apply) valid=false; apply=true; continue; }
            if(key!=L"--package" && key!=L"--manifest" && key!=L"--signature" && key!=L"--install-root" && key!=L"--data-dir" && key!=L"--wait-pid") { valid=false; break; }
            if(i+1>=count || options.contains(key)) { valid=false; break; } options.emplace(key,argv[++i]);
        }
        ::LocalFree(argv);
        for(const auto* key:{L"--package",L"--manifest",L"--signature",L"--install-root"}) if(!options.contains(key)) valid=false;
        if(!valid || !apply) return 2;
        if(!std::filesystem::path(options[L"--install-root"]).is_absolute())return 2;
        if(options.contains(L"--wait-pid")) {
            const auto text=options[L"--wait-pid"]; if(text.empty() || text.find_first_not_of(L"0123456789")!=std::wstring::npos) return 2;
            const auto pid=std::stoull(text); if(pid==0 || pid>MAXDWORD) return 2;
            const DWORD listening=platform::InstanceChannel::FindListenerProcessId(L"AetherShell"); if(listening && listening!=pid) return 2;
        }
        std::filesystem::path dataRoot;
        if(options.contains(L"--data-dir")) dataRoot=options[L"--data-dir"];
        else { std::array<wchar_t,32768> local{}; const DWORD size=::GetEnvironmentVariableW(L"LOCALAPPDATA",local.data(),static_cast<DWORD>(local.size())); if(!size || size>=local.size()) return 2; dataRoot=std::filesystem::path(local.data())/L"AetherShell"; }
        if(!dataRoot.is_absolute())return 2;
        struct ShutdownLog { ~ShutdownLog(){diag::Logger::Shutdown();} } shutdown;
        INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_PROGRESS_CLASS}; ::InitCommonControlsEx(&controls);
        platform::EnsurePerMonitorDpiAwareness();
        const wchar_t* className=L"AetherShellUpdaterWindow";
        WNDCLASSW windowClass{}; windowClass.hInstance=instance; windowClass.lpfnWndProc=&WindowProc; windowClass.lpszClassName=className;
        windowClass.hCursor=::LoadCursorW(nullptr,IDC_ARROW); windowClass.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);
        windowClass.hIcon=::LoadIconW(instance,L"IDI_AETHER");
        if(!::RegisterClassW(&windowClass)) return 3;
        Progress progress;const UINT dpi=::GetDpiForSystem();HWND window=::CreateWindowExW(0,className,L"Aether Shell Update",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU,CW_USEDEFAULT,CW_USEDEFAULT,::MulDiv(480,static_cast<int>(dpi),96),::MulDiv(195,static_cast<int>(dpi),96),nullptr,nullptr,instance,&progress);
        if(!window) return 3; ::ShowWindow(window,show);
        int outcome=1;
        std::jthread worker([&] {
            Result<void> result=MakeError(ErrorCode::Io,"update failed");
            try {
                auto layout=install::InstallLayout::Create(options[L"--install-root"]),data=install::InstallLayout::Create(dataRoot);
                if(!layout)result=layout.error();else if(!data)result=data.error();else {
                    diag::LoggerOptions logging;logging.directory=dataRoot/L"logs";logging.baseName=L"updater";diag::Logger::Initialize(logging);
                    result=install::ApplyUpdate(options[L"--package"],options[L"--manifest"],options[L"--signature"],layout.value(),dataRoot);
                    if(!result)AETHER_LOG_ERROR("updater","update failed: {}",ToString(result.error().code));
                }
            }catch(...){result=MakeError(ErrorCode::Io,"update worker failed");}
            { std::lock_guard lock(progress.mutex); progress.outcome=result?L"Update completed.":L"Update did not complete. See the updater log or use Repair."; }
            outcome=result?0:1; progress.busy=false; ::PostMessageW(window,WM_APP+1,0,0);
        });
        MSG message{}; while(::GetMessageW(&message,nullptr,0,0)>0) { if(!::IsDialogMessageW(window,&message)) { ::TranslateMessage(&message); ::DispatchMessageW(&message); } }
        worker.join(); return outcome;
    } catch(...) { return 4; }
}
