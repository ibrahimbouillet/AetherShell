// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "AetherTest.h"
#include "install/Install.h"

#include <windows.h>
#include <array>
#include <chrono>
#include <thread>

using namespace aether;
namespace {
struct WizardFixture {
    std::filesystem::path temporary=test::MakeTempDirectory("setup-viewport");
    platform::UniqueHandle process;
    HWND window=nullptr;
    ~WizardFixture() {
        if(window && ::IsWindow(window))::PostMessageW(window,WM_CLOSE,0,0);
        if(process && ::WaitForSingleObject(process.Get(),5000)!=WAIT_OBJECT_0)return;
        std::error_code ignored;std::filesystem::remove_all(temporary,ignored);
    }
    void Start() {
        const std::filesystem::path executable=AETHER_SETUP_TEST_EXE;
        std::wstring command=install::QuoteArgument(executable.native())+L" --test-no-registration --no-shortcuts --no-launch --install-root "+install::QuoteArgument((temporary/L"installation").native())+L" --data-dir "+install::QuoteArgument((temporary/L"data").native());
        STARTUPINFOW startup{sizeof(startup)};PROCESS_INFORMATION child{};
        REQUIRE(::CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,FALSE,0,nullptr,nullptr,&startup,&child)!=FALSE);
        platform::UniqueHandle thread(child.hThread);process.Reset(child.hProcess);
        struct Find {DWORD pid;HWND result=nullptr;} find{child.dwProcessId};
        const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(10);
        while(!find.result && std::chrono::steady_clock::now()<end) {
            ::EnumWindows([](HWND candidate,LPARAM parameter)->BOOL {
                auto& search=*reinterpret_cast<Find*>(parameter);DWORD pid=0;::GetWindowThreadProcessId(candidate,&pid);if(pid!=search.pid)return TRUE;
                std::array<wchar_t,64> name{};::GetClassNameW(candidate,name.data(),static_cast<int>(name.size()));
                if(std::wstring_view(name.data())==L"AetherShell.Setup"){search.result=candidate;return FALSE;}return TRUE;
            },reinterpret_cast<LPARAM>(&find));
            if(!find.result)std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        window=find.result;REQUIRE(window!=nullptr);
    }
};
std::wstring Text(HWND control) {
    std::wstring result(static_cast<std::size_t>(::SendMessageW(control,WM_GETTEXTLENGTH,0,0))+1,L'\0');::SendMessageW(control,WM_GETTEXT,result.size(),reinterpret_cast<LPARAM>(result.data()));result.resize(result.size()-1);return result;
}
}
AETHER_TEST("setup viewport: DPI keeps choices, clamps window and scrolls keyboard focus without installing") {
    WizardFixture fixture;fixture.Start();
    // Focusing Next must not activate it. A button notification is not a click.
    ::SendMessageW(fixture.window,WM_COMMAND,MAKEWPARAM(1,BN_SETFOCUS),reinterpret_cast<LPARAM>(::GetDlgItem(fixture.window,1)));
    CHECK(::GetDlgItem(fixture.window,20)==nullptr);
    ::SendMessageW(::GetDlgItem(fixture.window,1),BM_CLICK,0,0);
    ::SendMessageW(::GetDlgItem(fixture.window,1),BM_CLICK,0,0);
    REQUIRE(::GetDlgItem(fixture.window,20)!=nullptr);
    const auto selected=(fixture.temporary/L"selected installation").native();
    ::SendMessageW(::GetDlgItem(fixture.window,20),WM_SETTEXT,0,reinterpret_cast<LPARAM>(selected.c_str()));
    ::SendMessageW(::GetDlgItem(fixture.window,21),BM_SETCHECK,BST_CHECKED,0);
    ::SendMessageW(::GetDlgItem(fixture.window,23),BM_SETCHECK,BST_UNCHECKED,0);
    ::SendMessageW(::GetDlgItem(fixture.window,25),BM_SETCHECK,BST_CHECKED,0);
    ::SendMessageW(fixture.window,WM_COMMAND,MAKEWPARAM(2,BN_SETFOCUS),reinterpret_cast<LPARAM>(::GetDlgItem(fixture.window,2)));
    REQUIRE(::GetDlgItem(fixture.window,20)!=nullptr);
    ::SendMessageW(fixture.window,WM_APP+71,192,0);
    CHECK_EQ(Text(::GetDlgItem(fixture.window,20)),selected);
    CHECK_EQ(::SendMessageW(::GetDlgItem(fixture.window,21),BM_GETCHECK,0,0),LRESULT{BST_CHECKED});
    CHECK_EQ(::SendMessageW(::GetDlgItem(fixture.window,23),BM_GETCHECK,0,0),LRESULT{BST_UNCHECKED});
    CHECK_EQ(::SendMessageW(::GetDlgItem(fixture.window,25),BM_GETCHECK,0,0),LRESULT{BST_CHECKED});
    RECT bounds{};::GetWindowRect(fixture.window,&bounds);MONITORINFO monitor{sizeof(monitor)};REQUIRE(::GetMonitorInfoW(::MonitorFromWindow(fixture.window,MONITOR_DEFAULTTONEAREST),&monitor)!=FALSE);
    CHECK(bounds.left>=monitor.rcWork.left);CHECK(bounds.top>=monitor.rcWork.top);CHECK(bounds.right<=monitor.rcWork.right);CHECK(bounds.bottom<=monitor.rcWork.bottom);
    ::SetWindowPos(fixture.window,nullptr,bounds.left,bounds.top,700,500,SWP_NOZORDER|SWP_NOACTIVATE);
    ::SendMessageW(fixture.window,WM_VSCROLL,SB_BOTTOM,0);SCROLLINFO scroll{sizeof(scroll),SIF_POS};REQUIRE(::GetScrollInfo(fixture.window,SB_VERT,&scroll)!=FALSE);CHECK(scroll.nPos>0);
    ::SendMessageW(fixture.window,WM_VSCROLL,SB_TOP,0);
    ::SendMessageW(fixture.window,WM_MOUSEWHEEL,MAKEWPARAM(0,static_cast<WORD>(-WHEEL_DELTA)),0);REQUIRE(::GetScrollInfo(fixture.window,SB_VERT,&scroll)!=FALSE);CHECK(scroll.nPos>0);
    ::SendMessageW(fixture.window,WM_APP+72,2,0);
    RECT button{},client{};::GetWindowRect(::GetDlgItem(fixture.window,2),&button);::MapWindowPoints(nullptr,fixture.window,reinterpret_cast<POINT*>(&button),2);::GetClientRect(fixture.window,&client);
    CHECK(button.top>=0);CHECK(button.bottom<=client.bottom);CHECK(button.left>=0);CHECK(button.right<=client.right);
    CHECK_FALSE(std::filesystem::exists(fixture.temporary/L"installation"));CHECK_FALSE(std::filesystem::exists(fixture.temporary/L"selected installation"));CHECK_FALSE(std::filesystem::exists(fixture.temporary/L"data"));
}
