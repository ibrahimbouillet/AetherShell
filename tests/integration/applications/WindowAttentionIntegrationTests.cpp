// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "AetherTest.h"
#include "applications/WindowAttention.h"

#include <windows.h>

#include <chrono>

using namespace aether::apps;
namespace {
struct AttentionProbe {
    HWND target=nullptr,sink=nullptr;
    UINT message=0;
    bool registered=false;
    int notifications=0;
    WindowAttentionState state;
    ~AttentionProbe(){
        if(target){FLASHWINFO stop{sizeof(stop),target,FLASHW_STOP,0,0};(void)::FlashWindowEx(&stop);}
        if(registered)(void)::DeregisterShellHookWindow(sink);
        if(target)::DestroyWindow(target);
        if(sink)::DestroyWindow(sink);
    }
    static LRESULT CALLBACK Proc(HWND window,UINT event,WPARAM code,LPARAM parameter){
        if(event==WM_NCCREATE){const auto* creation=reinterpret_cast<CREATESTRUCTW*>(parameter);
            ::SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(creation->lpCreateParams));}
        auto* self=reinterpret_cast<AttentionProbe*>(::GetWindowLongPtrW(window,GWLP_USERDATA));
        // Ignore unrelated Shell HWNDs without querying their content.
        if(self&&self->message&&event==self->message&&reinterpret_cast<HWND>(parameter)==self->target){
            if(const auto kind=ClassifyShellAttentionEvent(code);kind&&*kind==WindowAttentionEvent::Flash){
                try{if(self->state.Observe(*kind,self->target))++self->notifications;}catch(...){}
            }
            return 0;
        }
        return ::DefWindowProcW(window,event,code,parameter);
    }
    void Initialize(){
        WNDCLASSW type{};type.hInstance=::GetModuleHandleW(nullptr);type.lpfnWndProc=Proc;
        type.lpszClassName=L"AetherShell.AttentionFixture";
        REQUIRE(::RegisterClassW(&type)||::GetLastError()==ERROR_CLASS_ALREADY_EXISTS);
        sink=::CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,type.lpszClassName,L"",WS_POPUP,
            0,0,0,0,nullptr,nullptr,type.hInstance,this);REQUIRE(sink!=nullptr);
        target=::CreateWindowExW(WS_EX_APPWINDOW|WS_EX_NOACTIVATE,type.lpszClassName,L"Aether attention test window",
            WS_OVERLAPPEDWINDOW,40,40,320,160,nullptr,nullptr,type.hInstance,nullptr);REQUIRE(target!=nullptr);
        message=::RegisterWindowMessageW(L"SHELLHOOK");REQUIRE(message!=0);
        registered=::RegisterShellHookWindow(sink)!=FALSE;REQUIRE(registered);
        ::ShowWindow(target,SW_SHOWNOACTIVATE);
    }
};
bool WaitForFlash(AttentionProbe& probe){
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
    while(!probe.state.Requested(probe.target)&&std::chrono::steady_clock::now()<deadline){
        MSG message{};while(::PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){
            ::TranslateMessage(&message);::DispatchMessageW(&message);}
        (void)::MsgWaitForMultipleObjectsEx(0,nullptr,20,QS_ALLINPUT,MWMO_INPUTAVAILABLE);
    }
    return probe.state.Requested(probe.target);
}
}

AETHER_TEST("attention integration: genuine own FlashWindowEx emits Shell attention without activation") {
    AttentionProbe probe;probe.Initialize();
    REQUIRE(::GetForegroundWindow()!=probe.target);
    // Only this fixture's attention changes. No external window, settings, native-taskbar visibility
    // or screen capture is controlled. The fixture is always cleaned up, including its flash state.
    FLASHWINFO flash{sizeof(flash),probe.target,FLASHW_TRAY,3,60};
    (void)::FlashWindowEx(&flash); // return value is prior caption state, not success/failure
    REQUIRE(WaitForFlash(probe));CHECK_EQ(probe.notifications,1);
    CHECK(::GetForegroundWindow()!=probe.target);
    CHECK(probe.state.Observe(WindowAttentionEvent::Activated,probe.target));
    CHECK_FALSE(probe.state.Requested(probe.target));
}
