// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "AetherTest.h"
#include "core/FileSystem.h"
#include "platform/windows/ComApartment.h"
#include "settings/ui/SettingsWindow.h"

#include <commctrl.h>
#include <chrono>
#include <thread>

using namespace aether;
using namespace std::chrono_literals;
namespace {
template<class Predicate> bool Pump(ManualDispatcher& dispatcher, Predicate predicate) {
    const auto deadline=std::chrono::steady_clock::now()+5s;
    while(!predicate()) {
        dispatcher.RunPending(); MSG message{};
        while(::PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){::TranslateMessage(&message);::DispatchMessageW(&message);}
        if(std::chrono::steady_clock::now()>deadline)return false;
        std::this_thread::sleep_for(1ms);
    }
    return true;
}
std::wstring ControlText(HWND control) {
    const int length=::GetWindowTextLengthW(control);
    std::wstring value(static_cast<std::size_t>(length)+1,L'\0');
    ::GetWindowTextW(control,value.data(),length+1);value.resize(static_cast<std::size_t>(length));return value;
}
}

AETHER_TEST("settings UI: native pages persist changes through a private temporary service") {
    platform::ComApartment apartment;
    INITCOMMONCONTROLSEX common{sizeof(common),ICC_WIN95_CLASSES|ICC_HOTKEY_CLASS};::InitCommonControlsEx(&common);
    ManualDispatcher dispatcher;
    const auto root=test::MakeTempDirectory("settings-ui");
    settings::SettingsService service(root/L"settings.json",dispatcher);
    REQUIRE(service.Load({}).ok()); REQUIRE(Pump(dispatcher,[&]{return service.Loaded();}));
    settings::ui::SettingsWindow window(service,dispatcher);
    REQUIRE(window.Show().ok()); REQUIRE(window.Control(200));
    ::SetWindowTextW(window.Control(200),L"64");
    REQUIRE(Pump(dispatcher,[&]{return service.Current().dock.iconSize==64 && !service.Dirty();}));
    CHECK_EQ(service.Current().dock.iconSize,64);
    auto saved=fs::ReadFile(root/L"settings.json",1024*1024); REQUIRE(saved.ok());
    CHECK(saved->find("64")!=std::string::npos);
    for(const auto page:{settings::ui::SettingsPage::TopBar,settings::ui::SettingsPage::Weather,
        settings::ui::SettingsPage::Updates,settings::ui::SettingsPage::General,settings::ui::SettingsPage::About}) {
        REQUIRE(window.Show(page).ok()); CHECK(::IsWindowVisible(window.Handle()));
        if(page==settings::ui::SettingsPage::Updates) {
            ::SetWindowTextW(window.Control(201),L"48");
            updates::UpdateSnapshot update;update.state=updates::UpdateState::Error;
            update.manifest=std::make_shared<updates::UpdateManifest>();
            update.failure=updates::UpdateFailure{MakeError(ErrorCode::Io,"synthetic-private-marker"),true,{}};
            window.SetUpdateSnapshot(update);CHECK(::IsWindowEnabled(window.Control(302)));
            wchar_t summary[1024]{};::GetWindowTextW(window.Control(317),summary,1024);
            CHECK(std::wstring(summary).find(L"synthetic-private-marker")==std::wstring::npos);
            CHECK(std::wstring(summary).find(L"Retry Download")!=std::wstring::npos);
            update.state=updates::UpdateState::Downloading;update.failure.reset();update.progress={512,0};
            window.SetUpdateSnapshot(update);CHECK_FALSE(::IsWindowEnabled(window.Control(302)));
            ::GetWindowTextW(window.Control(317),summary,1024);CHECK(std::wstring(summary).find(L'%')==std::wstring::npos);
            update.progress.total=1024;window.SetUpdateSnapshot(update);
            ::GetWindowTextW(window.Control(317),summary,1024);CHECK(std::wstring(summary).find(L"50%")!=std::wstring::npos);
            wchar_t draft[16]{};::GetWindowTextW(window.Control(201),draft,16);CHECK_EQ(std::wstring(draft),L"48");
            ::SendMessageW(window.Handle(),WM_TIMER,19,0);
            REQUIRE(Pump(dispatcher,[&]{return service.Current().updates.checkIntervalHours==48 && !service.Dirty();}));
        }
    }
    window.Close(); CHECK_FALSE(::IsWindowVisible(window.Handle()));
}

AETHER_TEST("settings UI: configurable shortcut uses native hotkey control without touching startup") {
    platform::ComApartment apartment;
    INITCOMMONCONTROLSEX common{sizeof(common),ICC_WIN95_CLASSES|ICC_HOTKEY_CLASS};::InitCommonControlsEx(&common);
    ManualDispatcher dispatcher;
    settings::SettingsService service(test::MakeTempDirectory("settings-ui-key")/L"settings.json",dispatcher);
    REQUIRE(service.Load({}).ok()); REQUIRE(Pump(dispatcher,[&]{return service.Loaded();}));
    settings::ui::SettingsWindow window(service,dispatcher);
    REQUIRE(window.Show(settings::ui::SettingsPage::General).ok());
    ::SendMessageW(window.Control(203),HKM_SETHOTKEY,VK_F2,0);
    ::SendMessageW(window.Control(205),CB_SETCURSEL,5,0); // Ctrl+Shift
    ::SendMessageW(window.Control(101),BM_CLICK,0,0);
    REQUIRE(Pump(dispatcher,[&]{return !service.Dirty();}));
    CHECK_EQ(service.Current().general.searchHotkey.virtualKey,static_cast<std::uint32_t>(VK_F2));
    CHECK_EQ(service.Current().general.searchHotkey.modifiers,6u);
    CHECK_FALSE(service.Current().general.startWithWindows);
}

AETHER_TEST("settings UI: delayed startup consent runs once and preserves concurrent Dock pins") {
    platform::ComApartment apartment;
    INITCOMMONCONTROLSEX common{sizeof(common),ICC_WIN95_CLASSES|ICC_HOTKEY_CLASS};::InitCommonControlsEx(&common);
    ManualDispatcher dispatcher;
    settings::SettingsService service(test::MakeTempDirectory("settings-ui-consent")/L"settings.json",dispatcher);
    REQUIRE(service.Load({}).ok()); REQUIRE(Pump(dispatcher,[&]{return service.Loaded();}));
    int calls=0; bool desired=false; std::function<void(Result<void>)> complete;
    settings::ui::SettingsCallbacks callbacks;
    callbacks.setStartup=[&](bool enabled, auto completion){++calls;desired=enabled;complete=std::move(completion);};
    settings::ui::SettingsWindow window(service,dispatcher,std::move(callbacks));
    REQUIRE(window.Show(settings::ui::SettingsPage::General).ok());
    ::SendMessageW(window.Control(200),BM_CLICK,0,0);
    ::SendMessageW(window.Control(101),BM_CLICK,0,0);
    ::SendMessageW(window.Control(101),BM_CLICK,0,0);
    CHECK_EQ(calls,1); CHECK(desired); CHECK_FALSE(::IsWindowEnabled(window.Control(101)));
    int closeReady=0;
    REQUIRE(window.PrepareToClose([&]{++closeReady;}).ok());CHECK_EQ(closeReady,0);
    REQUIRE(service.Update([](auto& value){settings::PinnedApp pin;pin.id=L"synthetic-pending";pin.displayName=L"Synthetic pending";value.dock.pinned.push_back(pin);}).ok());
    REQUIRE(complete); complete(Ok());
    REQUIRE(Pump(dispatcher,[&]{return !service.Dirty() && ::IsWindowEnabled(window.Control(101));}));
    CHECK_EQ(closeReady,1);
    CHECK(service.Current().general.startWithWindows);
    REQUIRE_EQ(service.Current().dock.pinned.size(),1u); CHECK_EQ(service.Current().dock.pinned[0].id,L"synthetic-pending");
    REQUIRE(window.ResetAfterConsent().ok());
    CHECK_EQ(calls,2); CHECK_FALSE(desired); CHECK(service.Current().general.startWithWindows);
    complete(MakeError(ErrorCode::AccessDenied,"synthetic startup removal failure"));
    CHECK(service.Current().general.startWithWindows);CHECK_EQ(service.Current().dock.pinned.size(),1u);
    CHECK(::IsWindowEnabled(window.Control(101)));
    REQUIRE(window.ResetAfterConsent().ok());CHECK_EQ(calls,3);
    complete(Ok());
    REQUIRE(Pump(dispatcher,[&]{return !service.Dirty() && ::IsWindowEnabled(window.Control(101));}));
    CHECK_FALSE(service.Current().general.startWithWindows); CHECK(service.Current().dock.pinned.empty());
}

AETHER_TEST("settings UI: live motion changes preserve all shortcut masks and never consent to startup") {
    platform::ComApartment apartment;
    INITCOMMONCONTROLSEX common{sizeof(common),ICC_WIN95_CLASSES|ICC_HOTKEY_CLASS};::InitCommonControlsEx(&common);
    ManualDispatcher dispatcher;
    settings::SettingsService service(test::MakeTempDirectory("settings-ui-live")/L"settings.json",dispatcher);
    REQUIRE(service.Load({}).ok()); REQUIRE(Pump(dispatcher,[&]{return service.Loaded();}));
    int startupCalls=0;
    settings::ui::SettingsCallbacks callbacks;
    callbacks.setStartup=[&](bool, auto){++startupCalls;};
    settings::ui::SettingsWindow window(service,dispatcher,std::move(callbacks));
    for (std::uint32_t mask=1;mask<=15;++mask) {
        REQUIRE(service.Update([mask](auto& value){value.general.searchHotkey={mask,VK_F3};}).ok());
        REQUIRE(window.Show(settings::ui::SettingsPage::General).ok());
        CHECK_EQ(::SendMessageW(window.Control(205),CB_GETCURSEL,0,0),static_cast<LRESULT>(mask-1));
        ::SendMessageW(window.Control(200),BM_CLICK,0,0); // Staged startup never participates in live saves.
        ::SendMessageW(window.Control(201),CB_SETCURSEL,1,0);
        ::SendMessageW(window.Handle(),WM_COMMAND,MAKEWPARAM(201,CBN_SELCHANGE),reinterpret_cast<LPARAM>(window.Control(201)));
        ::SendMessageW(window.Handle(),WM_TIMER,19,0); // Exercise the debounced path without a real input delay.
        REQUIRE(Pump(dispatcher,[&]{return !service.Dirty();}));
        CHECK_EQ(service.Current().general.searchHotkey.modifiers,mask);
        CHECK_EQ(service.Current().general.motion,settings::MotionPreference::Reduced);
        CHECK_FALSE(service.Current().general.startWithWindows);
    }
    CHECK_EQ(startupCalls,0);
}

AETHER_TEST("settings UI: DPI messages rescale permanent controls and font while preserving drafts and MIT text") {
    platform::ComApartment apartment;
    INITCOMMONCONTROLSEX common{sizeof(common),ICC_WIN95_CLASSES|ICC_HOTKEY_CLASS};::InitCommonControlsEx(&common);
    ManualDispatcher dispatcher;
    settings::SettingsService service(test::MakeTempDirectory("settings-ui-dpi")/L"settings.json",dispatcher);
    REQUIRE(service.Load({}).ok()); REQUIRE(Pump(dispatcher,[&]{return service.Loaded();}));
    settings::ui::SettingsWindow window(service,dispatcher);
    REQUIRE(window.Show().ok());
    RECT applyBefore{};::GetWindowRect(window.Control(101),&applyBefore);
    LOGFONTW fontBefore{};::GetObjectW(reinterpret_cast<HFONT>(::SendMessageW(window.Control(101),WM_GETFONT,0,0)),sizeof(fontBefore),&fontBefore);
    const UINT initialDpi=::GetDpiForWindow(window.Handle());
    ::SetWindowTextW(window.Control(200),L"7.");
    RECT bounds{};::GetWindowRect(window.Handle(),&bounds);
    bounds.right=bounds.left+::MulDiv(bounds.right-bounds.left,2,1); bounds.bottom=bounds.top+::MulDiv(bounds.bottom-bounds.top,2,1);
    ::SendMessageW(window.Handle(),WM_DPICHANGED,MAKEWPARAM(initialDpi*2,initialDpi*2),reinterpret_cast<LPARAM>(&bounds));
    RECT applyAfter{};::GetWindowRect(window.Control(101),&applyAfter);
    LOGFONTW fontAfter{};::GetObjectW(reinterpret_cast<HFONT>(::SendMessageW(window.Control(101),WM_GETFONT,0,0)),sizeof(fontAfter),&fontAfter);
    CHECK(std::abs((applyAfter.right-applyAfter.left)-2*(applyBefore.right-applyBefore.left))<=1);
    CHECK(std::abs(fontAfter.lfHeight-2*fontBefore.lfHeight)<=1);
    wchar_t draft[16]{};::GetWindowTextW(window.Control(200),draft,16);CHECK_EQ(std::wstring(draft),L"7.");
    REQUIRE(window.Show(settings::ui::SettingsPage::About).ok());
    const int length=::GetWindowTextLengthW(window.Control(316));std::wstring license(static_cast<std::size_t>(length)+1,L'\0');
    ::GetWindowTextW(window.Control(316),license.data(),length+1);
    CHECK(license.find(L"Permission is hereby granted")!=std::wstring::npos);
    CHECK(license.find(L"SOFTWARE IS PROVIDED")!=std::wstring::npos);
    ::SetWindowPos(window.Handle(),nullptr,0,0,420,360,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
    SCROLLINFO scroll{sizeof(scroll),SIF_ALL};REQUIRE(::GetScrollInfo(window.Handle(),SB_VERT,&scroll));
    CHECK(scroll.nMax>=static_cast<int>(scroll.nPage));
    ::SendMessageW(window.Handle(),WM_VSCROLL,SB_BOTTOM,0);
    ::SendMessageW(window.Handle(),WM_HSCROLL,SB_RIGHT,0);
    ::SetFocus(window.Control(102));
    MSG next{};next.hwnd=window.Control(102);next.message=WM_KEYDOWN;next.wParam=VK_TAB;
    REQUIRE(window.PreTranslate(next));CHECK_EQ(::GetFocus(),window.Control(101));
    RECT reachable{},client{};::GetWindowRect(window.Control(101),&reachable);
    ::MapWindowPoints(nullptr,window.Handle(),reinterpret_cast<POINT*>(&reachable),2);::GetClientRect(window.Handle(),&client);
    TEST_NOTE("viewport Apply rect "+std::to_string(reachable.left)+","+std::to_string(reachable.top)+","+std::to_string(reachable.right)+","+std::to_string(reachable.bottom)+"; client "+std::to_string(client.right)+","+std::to_string(client.bottom));
    CHECK(reachable.top>=0);CHECK(reachable.bottom<=client.bottom);
    CHECK(reachable.left>=0);CHECK(reachable.right<=client.right);
}

AETHER_TEST("settings UI: Desktop replacement requires Apply and status refresh preserves consent drafts") {
    platform::ComApartment apartment;
    INITCOMMONCONTROLSEX common{sizeof(common),ICC_WIN95_CLASSES|ICC_HOTKEY_CLASS};::InitCommonControlsEx(&common);
    ManualDispatcher dispatcher;
    const auto file=test::MakeTempDirectory("settings-ui-desktop")/L"settings.json";
    settings::SettingsService service(file,dispatcher);
    REQUIRE(service.Load({}).ok());REQUIRE(Pump(dispatcher,[&]{return service.Loaded();}));
    RECT before{};REQUIRE(::SystemParametersInfoW(SPI_GETWORKAREA,0,&before,0));
    std::wstring desktopStatus=L"Synthetic native taskbar retained.";
    int imports=0;
    settings::ui::SettingsWindow* editor=nullptr;
    settings::ui::SettingsCallbacks callbacks;
    callbacks.taskbarStatus=[&]{return desktopStatus;};
    callbacks.importTaskbarPins=[&]{
        ++imports;
        (void)service.Update([](auto& value){settings::PinnedApp pin;pin.id=L"exe:synthetic-import";pin.displayName=L"Synthetic import";value.dock.pinned.push_back(pin);});
        editor->ReportActionStatus(L"Synthetic pins imported.");
    };
    settings::ui::SettingsWindow window(service,dispatcher,std::move(callbacks));editor=&window;
    REQUIRE(window.Show(settings::ui::SettingsPage::Desktop).ok());
    CHECK_EQ(::SendMessageW(window.Control(100),LB_GETCOUNT,0,0),7);
    CHECK_EQ(::SendMessageW(window.Control(100),LB_GETCURSEL,0,0),6);
    CHECK_EQ(ControlText(window.Control(318)),desktopStatus);
    REQUIRE(window.Control(200));CHECK_FALSE(service.Current().general.replaceWindowsTaskbar);
    ::SendMessageW(window.Control(200),BM_CLICK,0,0);
    ::SendMessageW(window.Handle(),WM_TIMER,19,0);
    REQUIRE(Pump(dispatcher,[&]{return !service.Dirty();}));
    CHECK_FALSE(service.Current().general.replaceWindowsTaskbar);
    desktopStatus=L"Synthetic recovery helper is ready.";window.RefreshDesktopStatus();
    CHECK_EQ(ControlText(window.Control(318)),desktopStatus);
    CHECK_EQ(::SendMessageW(window.Control(200),BM_GETCHECK,0,0),BST_CHECKED);
    ::SendMessageW(window.Control(319),BM_CLICK,0,0);CHECK_EQ(imports,1);
    REQUIRE(Pump(dispatcher,[&]{return !service.Dirty();}));
    CHECK_EQ(ControlText(window.Control(103)),L"Synthetic pins imported.");
    CHECK_FALSE(service.Current().general.replaceWindowsTaskbar);
    window.Close();REQUIRE(window.Show(settings::ui::SettingsPage::Desktop).ok());
    CHECK_EQ(ControlText(window.Control(103)),L"Synthetic pins imported.");
    CHECK_EQ(::SendMessageW(window.Control(200),BM_GETCHECK,0,0),BST_UNCHECKED);
    ::SendMessageW(window.Control(200),BM_CLICK,0,0);::SendMessageW(window.Control(101),BM_CLICK,0,0);
    REQUIRE(Pump(dispatcher,[&]{return service.Current().general.replaceWindowsTaskbar && !service.Dirty();}));
    REQUIRE_EQ(service.Current().dock.pinned.size(),1u);CHECK_EQ(service.Current().dock.pinned[0].id,L"exe:synthetic-import");
    settings::SettingsService reloaded(file,dispatcher);REQUIRE(reloaded.Load({}).ok());
    REQUIRE(Pump(dispatcher,[&]{return reloaded.Loaded();}));CHECK(reloaded.Current().general.replaceWindowsTaskbar);
    ::SendMessageW(window.Control(200),BM_CLICK,0,0);::SendMessageW(window.Handle(),WM_TIMER,19,0);
    REQUIRE(Pump(dispatcher,[&]{return !service.Dirty();}));CHECK(service.Current().general.replaceWindowsTaskbar);
    ::SendMessageW(window.Control(101),BM_CLICK,0,0);
    REQUIRE(Pump(dispatcher,[&]{return !service.Current().general.replaceWindowsTaskbar && !service.Dirty();}));
    RECT after{};REQUIRE(::SystemParametersInfoW(SPI_GETWORKAREA,0,&after,0));CHECK(::EqualRect(&before,&after));
}

AETHER_TEST("settings UI: adding an application preserves executable arguments and unavailable Desktop actions stay disabled") {
    platform::ComApartment apartment;
    INITCOMMONCONTROLSEX common{sizeof(common),ICC_WIN95_CLASSES|ICC_HOTKEY_CLASS};::InitCommonControlsEx(&common);
    ManualDispatcher dispatcher;
    const auto file=test::MakeTempDirectory("settings-ui-pin-arguments")/L"settings.json";
    settings::SettingsService service(file,dispatcher);
    REQUIRE(service.Load({}).ok());REQUIRE(Pump(dispatcher,[&]{return service.Loaded();}));
    apps::ApplicationInfo app;app.id=L"exe:synthetic-profile";app.displayName=L"Synthetic profile";
    app.executablePath=L"synthetic.exe";app.arguments=L"--profile \"Synthetic profile\"";
    settings::ui::SettingsCallbacks callbacks;callbacks.catalog=[app]{return std::vector{app};};
    settings::ui::SettingsWindow window(service,dispatcher,std::move(callbacks));
    REQUIRE(window.Show(settings::ui::SettingsPage::Dock).ok());
    ::SendMessageW(window.Control(272),CB_SETCURSEL,0,0);::SendMessageW(window.Control(311),BM_CLICK,0,0);
    REQUIRE(Pump(dispatcher,[&]{return !service.Dirty();}));REQUIRE_EQ(service.Current().dock.pinned.size(),1u);
    CHECK_EQ(service.Current().dock.pinned[0].arguments,app.arguments);
    settings::SettingsService reloaded(file,dispatcher);REQUIRE(reloaded.Load({}).ok());
    REQUIRE(Pump(dispatcher,[&]{return reloaded.Loaded();}));REQUIRE_EQ(reloaded.Current().dock.pinned.size(),1u);
    CHECK_EQ(reloaded.Current().dock.pinned[0].arguments,app.arguments);
    REQUIRE(window.Show(settings::ui::SettingsPage::Desktop).ok());
    CHECK_FALSE(::IsWindowEnabled(window.Control(319)));
    CHECK(ControlText(window.Control(318)).find(L"unavailable")!=std::wstring::npos);
    CHECK_FALSE(service.Current().general.replaceWindowsTaskbar);
}
