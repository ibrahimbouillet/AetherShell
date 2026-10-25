// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Exercise the actual progress presenter in process, excluding the updater entry point entirely.
// No package verification/application, lifecycle shutdown, registration or real update runs here.
#define AETHER_UPDATER_PRESENTATION_TEST
#include "../../../updater/main.cpp"

#include "AetherTest.h"
#include "../ui_form/FormTestSupport.h"
#include "platform/windows/ComApartment.h"

namespace {

struct UpdaterFixture {
    Progress progress;
    HWND window=nullptr;
    ~UpdaterFixture(){if(window)::DestroyWindow(window);}
    void Create() {
        progress.quitOnDestroy=false;
        // Avoid retaining WinRT activation factories across the fixture's temporary COM apartments.
        progress.tokenFactory=[] {return aether::graphics::MakeDesignTokens(aether::graphics::DetectAppThemeMode(aether::graphics::ThemePreference::System));};
        WNDCLASSEXW wc{sizeof(wc)};
        wc.lpfnWndProc=&WindowProc;
        wc.hInstance=::GetModuleHandleW(nullptr);
        wc.lpszClassName=L"AetherShell.Tests.UpdaterPresentation";
        REQUIRE(aether::platform::RegisterWindowClassOnce(wc).ok());
        const UINT dpi=::GetDpiForSystem();
        window=::CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,wc.lpszClassName,L"Aether updater fixture",
            WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_CLIPCHILDREN,40,40,::MulDiv(520,static_cast<int>(dpi),96),::MulDiv(224,static_cast<int>(dpi),96),nullptr,nullptr,wc.hInstance,&progress);
        REQUIRE(window!=nullptr);
    }
};

RECT In(HWND child,HWND parent) {
    RECT rect{};
    ::GetWindowRect(child,&rect);
    ::MapWindowPoints(nullptr,parent,reinterpret_cast<POINT*>(&rect),2);
    return rect;
}

}

AETHER_TEST("updater design: real progress and failure presentation scroll enlarged text and retain Close") {
    using namespace aether;
    platform::ComApartment apartment(COINIT_APARTMENTTHREADED);
    REQUIRE(apartment.IsUsable());
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_PROGRESS_CLASS};
    REQUIRE(::InitCommonControlsEx(&controls)!=FALSE);
    UpdaterFixture fixture;
    fixture.Create();
    REQUIRE(fixture.progress.scroll.Viewport()!=nullptr);
    ::SendMessageW(fixture.window,WM_CLOSE,0,0);
    CHECK(::IsWindow(fixture.window)!=FALSE);  // an active update cannot be dismissed
    fixture.progress.succeeded=false;
    fixture.progress.outcome=L"Update did not complete. See the updater log or use Repair.";
    fixture.progress.busy=false;
    ::SendMessageW(fixture.window,WM_APP+1,0,0);
    REQUIRE(fixture.progress.close!=nullptr);
    CHECK((::GetWindowLongPtrW(fixture.progress.bar,GWL_STYLE)&WS_VISIBLE)==0);
    auto tokens=fixture.progress.tokens;
    tokens.textScale=2.25f;
    fixture.progress.tokens=tokens;
    fixture.progress.surface.SetTheme(tokens,fixture.progress.motion);
    Layout(fixture.window,fixture.progress);
    CHECK(fixture.progress.scroll.Range()>0);
    RECT client{};
    ::GetClientRect(fixture.window,&client);
    const auto close=In(fixture.progress.close,fixture.window);
    CHECK(close.left>=0);CHECK(close.right<=client.right);
    CHECK(close.top>=0);CHECK(close.bottom<=client.bottom);
    const auto viewport=In(fixture.progress.scroll.Viewport(),fixture.window);
    CHECK(viewport.bottom<close.top);
    fixture.progress.scroll.ScrollTo(fixture.progress.scroll.Range());
    CHECK_EQ(fixture.progress.scroll.Position(),fixture.progress.scroll.Range());
    HWND label=fixture.progress.label,button=fixture.progress.close;
    ::SendMessageW(fixture.window,WM_SETTINGCHANGE,0,0);
    CHECK(fixture.progress.label==label);CHECK(fixture.progress.close==button);
    std::array<wchar_t,128> outcome{};
    ::GetWindowTextW(label,outcome.data(),static_cast<int>(outcome.size()));
    CHECK(std::wstring_view(outcome.data())==L"Update did not complete. See the updater log or use Repair.");
    tokens.mode=graphics::ThemeMode::HighContrast;
    fixture.progress.tokens=tokens;
    fixture.progress.surface.SetTheme(tokens,fixture.progress.motion);
    Layout(fixture.window,fixture.progress);
    CHECK(fixture.progress.surface.Material()==ui::form::FormMaterial::Native);
}

AETHER_TEST("updater design: actual presenter states render over an owned private backdrop") {
    using namespace aether;
    using namespace aether::test::form;
    platform::ComApartment apartment(COINIT_APARTMENTTHREADED);
    REQUIRE(apartment.IsUsable());
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_PROGRESS_CLASS};
    REQUIRE(::InitCommonControlsEx(&controls)!=FALSE);
    for(const bool dark:{true,false}) {
        for(const int state:{0,1,2}) {
            const RECT safe=SafeArea();
            const UINT dpi=::GetDpiForSystem();
            const int width=std::min<int>(::MulDiv(520,static_cast<int>(dpi),96),safe.right-safe.left-40);
            const int height=std::min<int>(::MulDiv(320,static_cast<int>(dpi),96),safe.bottom-safe.top-40);
            const int left=(safe.left+safe.right-width)/2,top=(safe.top+safe.bottom-height)/2;
            BackdropGuard backdrop;
            backdrop.area=RECT{left-20,top-20,left+width+20,top+height+20};
            backdrop.hwnd=CreateTestBackdrop(backdrop.area);
            REQUIRE(backdrop.hwnd!=nullptr);
            UpdaterFixture fixture;
            fixture.Create();
            graphics::AppearanceOptions appearance;
            appearance.theme=dark?graphics::ThemePreference::Dark:graphics::ThemePreference::Light;
            fixture.progress.tokens=graphics::MakeDesignTokens(graphics::DetectAppThemeMode(appearance.theme),graphics::SystemPalette{},appearance);
            fixture.progress.surface.SetTheme(fixture.progress.tokens,fixture.progress.motion);
            ::SetWindowPos(fixture.window,HWND_TOPMOST,left,top,width,height,SWP_NOACTIVATE);
            ::ShowWindow(fixture.window,SW_SHOWNOACTIVATE);
            if(state) {
                fixture.progress.succeeded=state==1;
                fixture.progress.outcome=state==1?L"Update completed.":L"Update did not complete. See the updater log or use Repair.";
                fixture.progress.busy=false;
                ::SendMessageW(fixture.window,WM_APP+1,0,0);
            }
            Layout(fixture.window,fixture.progress);
            Pump(700);
            RECT bounds{};
            REQUIRE(::GetWindowRect(fixture.window,&bounds)!=FALSE);
            const auto capture=CaptureWithRetry(backdrop.hwnd,backdrop.area,bounds);
            const char* stateName=state==0?"progress":state==1?"finish":"failure";
            const std::string name=std::string("updater-")+(dark?"dark-":"light-")+stateName;
            if(capture.Valid())CHECK(SavePng(capture,InspectionPath(name)));
            else TEST_NOTE(name+": render inconclusive; private backdrop was not exclusively visible");
        }
    }
}
