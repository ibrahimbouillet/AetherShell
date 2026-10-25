// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "install/Install.h"
#include "core/Strings.h"
#include "diagnostics/Logger.h"
#include "platform/windows/SingleInstance.h"
#include "platform/windows/DpiUtilities.h"
#include "platform/windows/ComApartment.h"
#include "platform/windows/WindowUtilities.h"
#include "graphics/Glyphs.h"
#include "graphics/SystemPalette.h"
#include "ui/form/FormSurface.h"
#include "ui/form/FormScrollView.h"

#include <commctrl.h>
#include <shellapi.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <atomic>
#include <map>
#include <mutex>
#include <thread>

namespace {
namespace form = aether::ui::form;
namespace graphics = aether::graphics;
namespace platform = aether::platform;

// The verify-and-apply progress window on the shared Aether form material. The engine reports no
// fraction, so progress is shown as activity; the outcome replaces it when the worker finishes.
struct Progress {
    std::atomic<bool> busy{true};
    HWND title=nullptr,label=nullptr,bar=nullptr,close=nullptr;
    form::FormSurface surface;form::FormScrollView scroll;bool styled=false;
    graphics::DesignTokens tokens;graphics::MotionPolicy motion;
    D2D1_RECT_F mark{};
    std::mutex mutex;
    const wchar_t* outcome=L"";
    bool succeeded=false;
    bool quitOnDestroy=true;
    bool layingOut=false;
    std::function<graphics::DesignTokens()> tokenFactory=[] {return graphics::MakeAppDesignTokens(graphics::AppearanceOptions{});};
};
float Scale(const Progress& data,HWND window) {
    const UINT dpi=data.styled ? data.surface.Dpi() : ::GetDpiForWindow(window);
    return static_cast<float>(dpi?dpi:96)/96.0f;
}
void RefreshTheme(Progress& data) {
    data.tokens=data.tokenFactory();
    data.motion.reduced=!platform::SystemAnimationsEnabled() || data.tokens.mode==graphics::ThemeMode::HighContrast;
    if(data.styled)data.surface.SetTheme(data.tokens,data.motion);
}
float TextHeight(Progress& data,HWND control,form::TextRole role,float width) {
    std::wstring text(static_cast<std::size_t>(::GetWindowTextLengthW(control))+1,L'\0');
    text.resize(static_cast<std::size_t>(::GetWindowTextW(control,text.data(),static_cast<int>(text.size()))));
    if(data.styled)return std::max(20.0f,std::ceil(data.surface.MeasureText(text,role,std::max(1.0f,width),true).height));
    return 40.0f;
}
void Place(Progress& data,HWND window,HWND control,float left,float top,float right,float bottom) {
    if(!control)return;
    const float scale=Scale(data,window);
    const RECT r{static_cast<LONG>(std::lround(left*scale)),static_cast<LONG>(std::lround(top*scale)),
                 static_cast<LONG>(std::lround(right*scale)),static_cast<LONG>(std::lround(bottom*scale))};
    if(data.styled && data.surface.Spec(control))data.surface.SetFrame(control,r);
    else ::SetWindowPos(control,nullptr,r.left,r.top,r.right-r.left,r.bottom-r.top,SWP_NOZORDER|SWP_NOACTIVATE);
}
void Layout(HWND window,Progress& data) {
    if(data.layingOut)return;
    data.layingOut=true;
    struct ResetLayout{bool& value;~ResetLayout(){value=false;}} reset{data.layingOut};
    RECT client{};::GetClientRect(window,&client);
    const float width=static_cast<float>(client.right)/Scale(data,window);
    const float outer=data.tokens.form.outer;
    data.mark=D2D1::RectF(outer,22.0f,outer+32.0f,54.0f);
    const float titleHeight=std::max(30.0f,TextHeight(data,data.title,form::TextRole::Section,width-outer*2.0f-44.0f));
    Place(data,window,data.title,outer+44.0f,22.0f,width-outer,22.0f+titleHeight);
    const float top=std::max(54.0f,22.0f+titleHeight)+12.0f;
    // Enlarged system text must leave a useful scroll viewport above the fixed Close action.
    const float minimumHeight=top+64.0f+outer+(data.close?data.tokens.form.controlHeight+12.0f:0.0f);
    const int minimumPx=static_cast<int>(std::ceil(minimumHeight*Scale(data,window)));
    if(client.bottom<minimumPx) {
        RECT bounds{};::GetWindowRect(window,&bounds);
        MONITORINFO monitor{sizeof(monitor)};
        if(::GetMonitorInfoW(::MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&monitor)) {
            const int frame=(bounds.bottom-bounds.top)-client.bottom;
            const int h=std::min(minimumPx+frame,static_cast<int>(monitor.rcWork.bottom-monitor.rcWork.top));
            const int y=std::clamp(static_cast<int>(bounds.top),static_cast<int>(monitor.rcWork.top),static_cast<int>(monitor.rcWork.bottom)-h);
            ::SetWindowPos(window,nullptr,bounds.left,y,bounds.right-bounds.left,h,SWP_NOZORDER|SWP_NOACTIVATE);
            ::GetClientRect(window,&client);
        }
    }
    const float height=static_cast<float>(client.bottom)/Scale(data,window);
    const float bottom=data.close?height-outer-data.tokens.form.controlHeight-12.0f:height-outer;
    if(data.styled) {
        const float scale=Scale(data,window);
        data.scroll.SetFrame(RECT{static_cast<LONG>(std::lround(outer*scale)),static_cast<LONG>(std::lround(top*scale)),
                                  client.right-static_cast<LONG>(std::lround(outer*scale)),static_cast<LONG>(std::lround(std::max(top+20.0f,bottom)*scale))});
        const float contentWidth=width-outer*2.0f-static_cast<float>(data.scroll.GutterPx())/scale;
        const auto role=data.close?form::TextRole::Status:form::TextRole::Body;
        const float labelHeight=TextHeight(data,data.label,role,contentWidth-28.0f);
        Place(data,window,data.label,0.0f,0.0f,contentWidth,labelHeight);
        Place(data,window,data.bar,0.0f,labelHeight+14.0f,contentWidth,labelHeight+20.0f);
        data.scroll.SetContentHeight(static_cast<int>(std::lround((labelHeight+(data.close?8.0f:28.0f))*scale)));
    } else {
        Place(data,window,data.label,outer,top,width-outer,top+40.0f);
        Place(data,window,data.bar,outer,top+54.0f,width-outer,top+60.0f);
    }
    if(data.close){const float w=data.styled?data.surface.PreferredWidth(data.close):96.0f;Place(data,window,data.close,width-outer-w,height-outer-data.tokens.form.controlHeight,width-outer,height-outer);}
    ::InvalidateRect(window,nullptr,FALSE);
}
HWND Child(HWND window,const wchar_t* cls,const wchar_t* text,DWORD style,int id=0) {
    return ::CreateWindowExW(0,cls,text,WS_CHILD|WS_VISIBLE|style,0,0,1,1,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),nullptr,nullptr);
}
void Register(Progress& data,HWND control,form::ControlSpec spec) {
    if(data.styled)(void)data.surface.Register(control,spec);
    else ::SendMessageW(control,WM_SETFONT,reinterpret_cast<WPARAM>(::GetStockObject(DEFAULT_GUI_FONT)),TRUE);
}
LRESULT CALLBACK WindowProc(HWND window,UINT message,WPARAM wparam,LPARAM lparam) {
    try {
    auto* data=reinterpret_cast<Progress*>(::GetWindowLongPtrW(window,GWLP_USERDATA));
    if(message==WM_NCCREATE) { data=static_cast<Progress*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams); ::SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(data)); }
    if(data && data->styled){if(auto handled=data->surface.HandleMessage(message,wparam,lparam))return *handled;}
    if(message==WM_CREATE) {
        RefreshTheme(*data);
        data->styled=data->surface.Attach(window,data->tokens,data->motion).ok();
        if(!data->styled)return -1;
        if(!data->scroll.Create(data->surface,window,2))return -1;
        if(data->styled)data->surface.SetDecorator([data](ID2D1DeviceContext* target,const D2D1_RECT_F&,const graphics::DesignTokens& tokens){graphics::PaintBrandMark(target,data->mark,tokens);});
        else platform::ApplyImmersiveDarkMode(window,platform::SystemPrefersDarkApps());
        const DWORD labelStyle=(data->styled?SS_OWNERDRAW:0u)|SS_NOPREFIX;
        data->title=Child(window,L"STATIC",L"Updating Aether Shell",labelStyle);
        {form::ControlSpec spec;spec.role=form::ControlRole::Label;spec.text=form::TextRole::Section;Register(*data,data->title,spec);}
        data->label=Child(data->scroll.Canvas(),L"STATIC",L"Verifying the signed package and applying the update.",labelStyle);
        {form::ControlSpec spec;spec.role=form::ControlRole::Label;spec.text=form::TextRole::Body;Register(*data,data->label,spec);}
        data->bar=Child(data->scroll.Canvas(),PROGRESS_CLASSW,nullptr,PBS_MARQUEE);
        {form::ControlSpec spec;spec.role=form::ControlRole::Progress;Register(*data,data->bar,spec);}
        if(data->styled)data->surface.SetProgress(data->bar,std::nullopt);
        else ::SendMessageW(data->bar,PBM_SETMARQUEE,data->motion.reduced?FALSE:TRUE,30);
        Layout(window,*data);return 0;
    }
    if(message==WM_APP+1) {
        if(!data->styled)::SendMessageW(data->bar,PBM_SETMARQUEE,FALSE,0);
        ::ShowWindow(data->bar,SW_HIDE);
        {
            std::lock_guard lock(data->mutex);
            ::SetWindowTextW(data->title,data->succeeded?L"Update completed":L"The update did not complete");
            ::SetWindowTextW(data->label,data->outcome);
            form::ControlSpec spec;spec.role=form::ControlRole::Label;spec.text=form::TextRole::Status;
            spec.severity=data->succeeded?form::Severity::Success:form::Severity::Error;
            if(data->styled)data->surface.SetSpec(data->label,spec);
        }
        data->close=Child(window,L"BUTTON",L"Close",WS_TABSTOP|BS_PUSHBUTTON|BS_DEFPUSHBUTTON,1);
        {form::ControlSpec spec;spec.role=form::ControlRole::Button;spec.button=form::ButtonStyle::Primary;Register(*data,data->close,spec);}
        Layout(window,*data);::SetFocus(data->close);return 0;
    }
    if(message==WM_DPICHANGED && data) {const auto& rect=*reinterpret_cast<RECT*>(lparam);::SetWindowPos(window,nullptr,rect.left,rect.top,rect.right-rect.left,rect.bottom-rect.top,SWP_NOZORDER|SWP_NOACTIVATE);Layout(window,*data);return 0;}
    if((message==WM_SETTINGCHANGE || message==WM_THEMECHANGED || message==WM_SYSCOLORCHANGE) && data){RefreshTheme(*data);if(data->title)Layout(window,*data);return 0;}
    if(message==WM_SIZE && data && data->title){Layout(window,*data);return 0;}
    if(message==WM_COMMAND && LOWORD(wparam)==1) { ::SendMessageW(window,WM_CLOSE,0,0); return 0; }
    if(message==WM_CLOSE && data && data->busy.load()) return 0;
    if(message==WM_DESTROY) { if(data){data->scroll.Destroy();data->surface.Detach();data->styled=false;} if(!data || data->quitOnDestroy)::PostQuitMessage(0); return 0; }
    if(message==WM_NCDESTROY && data) {::SetWindowLongPtrW(window,GWLP_USERDATA,0);}
    return ::DefWindowProcW(window,message,wparam,lparam);
    }catch(...){if(message==WM_CREATE)return -1;::PostQuitMessage(4);return 0;}
}
}
#ifndef AETHER_UPDATER_PRESENTATION_TEST
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
        platform::ComApartment apartment;
        if(!apartment.IsUsable()){::MessageBoxW(nullptr,L"The update interface could not start. The update has not been applied. Try again or use Repair.",L"Aether Shell Update",MB_OK|MB_ICONERROR);return 3;}
        struct ShutdownLog { ~ShutdownLog(){diag::Logger::Shutdown();} } shutdown;
        INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_PROGRESS_CLASS}; ::InitCommonControlsEx(&controls);
        platform::EnsurePerMonitorDpiAwareness();
        const wchar_t* className=L"AetherShellUpdaterWindow";
        WNDCLASSW windowClass{}; windowClass.hInstance=instance; windowClass.lpfnWndProc=&WindowProc; windowClass.lpszClassName=className;
        windowClass.hCursor=::LoadCursorW(nullptr,IDC_ARROW);
        windowClass.hIcon=::LoadIconW(instance,L"IDI_AETHER");
        if(!::RegisterClassW(&windowClass)) return 3;
        Progress progress;const UINT dpi=::GetDpiForSystem();HWND window=::CreateWindowExW(0,className,L"Aether Shell Update",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,::MulDiv(520,static_cast<int>(dpi),96),::MulDiv(224,static_cast<int>(dpi),96),nullptr,nullptr,instance,&progress);
        if(!window){::MessageBoxW(nullptr,L"The update interface could not start. The update has not been applied. Try again or use Repair in Windows Settings > Apps > Installed apps.",L"Aether Shell Update",MB_OK|MB_ICONERROR);return 3;} ::ShowWindow(window,show);
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
            { std::lock_guard lock(progress.mutex); progress.succeeded=result.ok(); progress.outcome=result?L"Update completed.":L"Update did not complete. See the updater log or use Repair."; }
            outcome=result?0:1; progress.busy=false; ::PostMessageW(window,WM_APP+1,0,0);
        });
        MSG message{}; while(::GetMessageW(&message,nullptr,0,0)>0) { if(!::IsDialogMessageW(window,&message)) { ::TranslateMessage(&message); ::DispatchMessageW(&message); } }
        worker.join(); return outcome;
    } catch(...) { return 4; }
}
#endif
