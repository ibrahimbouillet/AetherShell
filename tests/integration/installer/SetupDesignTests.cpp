// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// The redesigned setup wizard (T47 / FR-037) in the isolated test binary only: every page of the
// install, repair and uninstall journeys keeps its control ids, Back/Next keep the choices, an invalid
// folder is reported inline without leaving the page, and the pages render on the form material over
// a private backdrop. Package operations run only in the cancel/retry fixture's temporary roots;
// --test-no-registration suppresses real registration, shortcuts, startup and application launch.
#include "AetherTest.h"

#include "../ui_form/FormTestSupport.h"

#include "core/Strings.h"
#include "install/Install.h"
#include "platform/windows/ComApartment.h"

#include <windows.h>

#include <array>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

using namespace aether;
using namespace aether::test::form;

namespace {

struct Wizard {
    std::filesystem::path temporary = test::MakeTempDirectory("setup-design");
    platform::UniqueHandle process;
    DWORD pid = 0;
    HWND window = nullptr;
    ~Wizard() {
        if (window && ::IsWindow(window)) ::PostMessageW(window, WM_CLOSE, 0, 0);
        if (process && ::WaitForSingleObject(process.Get(), 5000) != WAIT_OBJECT_0) return;
        std::error_code ignored;
        std::filesystem::remove_all(temporary, ignored);
    }
    void Start(const std::wstring& extra) {
        const std::filesystem::path executable = AETHER_SETUP_TEST_EXE;
        std::wstring command = install::QuoteArgument(executable.native()) +
            L" --test-no-registration --no-shortcuts --no-launch --install-root " +
            install::QuoteArgument((temporary / L"installation").native()) + L" --data-dir " +
            install::QuoteArgument((temporary / L"data").native()) + L" " + extra;
        STARTUPINFOW startup{sizeof(startup)};
        PROCESS_INFORMATION child{};
        REQUIRE(::CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &child) != FALSE);
        platform::UniqueHandle thread(child.hThread);
        process.Reset(child.hProcess);
        pid = child.dwProcessId;
        struct Find { DWORD pid; HWND result = nullptr; } find{pid};
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (!find.result && std::chrono::steady_clock::now() < end) {
            ::EnumWindows([](HWND candidate, LPARAM parameter) -> BOOL {
                auto& search = *reinterpret_cast<Find*>(parameter);
                DWORD owner = 0;
                ::GetWindowThreadProcessId(candidate, &owner);
                if (owner != search.pid) return TRUE;
                std::array<wchar_t, 64> name{};
                ::GetClassNameW(candidate, name.data(), static_cast<int>(name.size()));
                if (std::wstring_view(name.data()) == L"AetherShell.Setup") { search.result = candidate; return FALSE; }
                return TRUE;
            }, reinterpret_cast<LPARAM>(&find));
            if (!find.result) std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        window = find.result;
        REQUIRE(window != nullptr);
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }
    [[nodiscard]] HWND Item(int id) const {
        struct Search { int id; HWND found = nullptr; } search{id};
        ::EnumChildWindows(window, [](HWND child, LPARAM parameter) -> BOOL {
            auto& s = *reinterpret_cast<Search*>(parameter);
            if (::GetDlgCtrlID(child) != s.id) return TRUE;
            s.found = child;
            return FALSE;
        }, reinterpret_cast<LPARAM>(&search));
        return search.found;
    }
    void Click(int id) const {
        HWND button = Item(id);
        REQUIRE(button != nullptr);
        // The click notification, as a real click on the button would send it (no activation).
        ::SendMessageW(window, WM_COMMAND, MAKEWPARAM(id, BN_CLICKED), reinterpret_cast<LPARAM>(button));
        std::this_thread::sleep_for(std::chrono::milliseconds(120));
    }
};

std::wstring Text(HWND control) {
    std::wstring result(static_cast<std::size_t>(::SendMessageW(control, WM_GETTEXTLENGTH, 0, 0)) + 1, L'\0');
    ::SendMessageW(control, WM_GETTEXT, result.size(), reinterpret_cast<LPARAM>(result.data()));
    result.resize(result.size() - 1);
    return result;
}

bool HasText(HWND window,std::wstring_view text) {
    struct Search{std::wstring_view text;bool found=false;} search{text};
    ::EnumChildWindows(window,[](HWND child,LPARAM parameter)->BOOL {
        auto& s=*reinterpret_cast<Search*>(parameter);
        if(Text(child).find(s.text)==std::wstring::npos)return TRUE;
        s.found=true;return FALSE;
    },reinterpret_cast<LPARAM>(&search));
    return search.found;
}

// The private backdrop is the only content below the wizard (this test's process and the wizard's
// own process are trusted); geometry and PIDs only, never foreign titles or content.
bool OnlyPrivateBelow(HWND backdrop, const RECT& guard, DWORD wizardProcess) {
    for (HWND window = ::GetTopWindow(nullptr); window; window = ::GetWindow(window, GW_HWNDNEXT)) {
        if (window == backdrop) return true;
        if (!::IsWindowVisible(window)) continue;
        DWORD process = 0;
        ::GetWindowThreadProcessId(window, &process);
        if (process == ::GetCurrentProcessId() || process == wizardProcess) continue;
        RECT bounds{}, overlap{};
        if (::GetWindowRect(window, &bounds) && ::IntersectRect(&overlap, &bounds, &guard)) return false;
    }
    return false;
}

Capture CaptureWizard(HWND backdrop, const RECT& guard, const RECT& rect, DWORD wizardProcess) {
    const auto contained = [&] {
        RECT bounds{};
        DWORD owner=0;
        ::GetWindowThreadProcessId(backdrop,&owner);
        return owner==::GetCurrentProcessId() && ::IsWindowVisible(backdrop) && ::GetWindowRect(backdrop,&bounds) &&
            guard.left>=bounds.left && guard.top>=bounds.top && guard.right<=bounds.right && guard.bottom<=bounds.bottom &&
            rect.left>=guard.left && rect.top>=guard.top && rect.right<=guard.right && rect.bottom<=guard.bottom;
    };
    for (int attempt = 0; attempt < 6; ++attempt) {
        if (contained() && OnlyPrivateBelow(backdrop, guard, wizardProcess)) {
            Capture capture;
            const int width = rect.right - rect.left, height = rect.bottom - rect.top;
            HDC screen = ::GetDC(nullptr);
            HDC memory = ::CreateCompatibleDC(screen);
            BITMAPINFO info{};
            info.bmiHeader = BITMAPINFOHEADER{sizeof(BITMAPINFOHEADER), width, -height, 1, 32, BI_RGB, 0, 0, 0, 0, 0};
            void* bits = nullptr;
            HBITMAP bitmap = ::CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
            if (memory && bitmap && bits) {
                HGDIOBJ old = ::SelectObject(memory, bitmap);
                if (::BitBlt(memory, 0, 0, width, height, screen, rect.left, rect.top, SRCCOPY | CAPTUREBLT)) {
                    capture.width = width;
                    capture.height = height;
                    capture.bgra.assign(static_cast<std::uint8_t*>(bits), static_cast<std::uint8_t*>(bits) + static_cast<std::size_t>(width) * height * 4);
                }
                ::SelectObject(memory, old);
            }
            if (bitmap) ::DeleteObject(bitmap);
            if (memory) ::DeleteDC(memory);
            ::ReleaseDC(nullptr, screen);
            if (capture.Valid() && contained() && OnlyPrivateBelow(backdrop, guard, wizardProcess)) return capture;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
    }
    return Capture{};
}

void PlaceWizardOverBackdrop(const Wizard& wizard,BackdropGuard& backdrop) {
    const RECT safe=SafeArea();
    RECT bounds{};
    REQUIRE(::GetWindowRect(wizard.window,&bounds)!=FALSE);
    const int width=std::min<int>(bounds.right-bounds.left,safe.right-safe.left-40);
    const int height=std::min<int>(bounds.bottom-bounds.top,safe.bottom-safe.top-20);
    const int left=(safe.left+safe.right-width)/2,top=(safe.top+safe.bottom-height)/2;
    backdrop.area=RECT{left-20,top-10,left+width+20,top+height+10};
    ::IntersectRect(&backdrop.area,&backdrop.area,&safe);
    backdrop.hwnd=CreateTestBackdrop(backdrop.area);
    REQUIRE(backdrop.hwnd!=nullptr);
    ::SetWindowPos(wizard.window,HWND_TOPMOST,left,top,width,height,SWP_NOACTIVATE);
}

void SaveWizardVisual(const Wizard& wizard,const BackdropGuard& backdrop,const std::string& name) {
    ::SetWindowPos(wizard.window,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
    std::this_thread::sleep_for(std::chrono::milliseconds(700));
    RECT window{};
    REQUIRE(::GetWindowRect(wizard.window,&window)!=FALSE);
    const auto capture=CaptureWizard(backdrop.hwnd,backdrop.area,window,wizard.pid);
    if(!capture.Valid()) {
        TEST_NOTE(name+": render inconclusive; private backdrop was not exclusively visible");
        return;
    }
    CHECK(SavePng(capture,InspectionPath(name)));
}

}  // namespace

AETHER_TEST("setup design: the install journey keeps its ids, reports an invalid folder inline and keeps choices") {
    Wizard wizard;
    wizard.Start(L"--test-theme dark");
    // Welcome
    REQUIRE(wizard.Item(1) != nullptr);
    CHECK(wizard.Item(8) == nullptr);
    CHECK(wizard.Item(20) == nullptr);
    CHECK(Text(wizard.Item(42)).find(L"Step 1 of 5") != std::wstring::npos);
    wizard.Click(1);
    // Licence: the full MIT text with real line breaks (a multiline EDIT needs CR LF).
    REQUIRE(wizard.Item(8) != nullptr);
    CHECK(Text(wizard.Item(42)).find(L"Step 2 of 5") != std::wstring::npos);
    {
        struct Find { HWND found = nullptr; } find;
        ::EnumChildWindows(wizard.window, [](HWND child, LPARAM parameter) -> BOOL {
            std::array<wchar_t, 16> name{};
            ::GetClassNameW(child, name.data(), static_cast<int>(name.size()));
            if (_wcsicmp(name.data(), L"Edit") != 0 || !(::GetWindowLongW(child, GWL_STYLE) & ES_MULTILINE)) return TRUE;
            reinterpret_cast<Find*>(parameter)->found = child;
            return FALSE;
        }, reinterpret_cast<LPARAM>(&find));
        REQUIRE(find.found != nullptr);
        const std::wstring licence = Text(find.found);
        CHECK(licence.find(L"MIT License\r\n") == 0);
        CHECK(licence.find(L"Permission is hereby granted") != std::wstring::npos);
        CHECK(licence.find(L"copyof") == std::wstring::npos);  // no words joined across lost line breaks
        CHECK(::SendMessageW(find.found, EM_GETLINECOUNT, 0, 0) > 12);
    }
    wizard.Click(1);
    // Options
    for (const int id : {2, 8, 9, 20, 21, 23, 25}) {
        if (!wizard.Item(id)) TEST_NOTE("missing options control " + std::to_string(id));
        CHECK(wizard.Item(id) != nullptr);
    }
    CHECK(wizard.Item(24) == nullptr);
    CHECK(Text(wizard.Item(2)).find(L"Install") != std::wstring::npos);
    ::SendMessageW(wizard.Item(25), BM_SETCHECK, BST_CHECKED, 0);
    ::SendMessageW(wizard.Item(21), BM_SETCHECK, BST_UNCHECKED, 0);
    // A relative folder is refused on the page itself.
    ::SendMessageW(wizard.Item(20), WM_SETTEXT, 0, reinterpret_cast<LPARAM>(L"relative folder"));
    wizard.Click(2);
    CHECK(wizard.Item(20) != nullptr);  // still on Options
    CHECK(wizard.Item(30) == nullptr);  // no progress page
    CHECK_FALSE(Text(wizard.Item(43)).empty());  // the footer explains the error
    // System presentation refreshes preserve this live page and its uncommitted choices.
    HWND folder=wizard.Item(20),desktop=wizard.Item(25),startup=wizard.Item(21);
    ::SendMessageW(wizard.window,WM_SETTINGCHANGE,0,0);
    ::SendMessageW(wizard.window,WM_THEMECHANGED,0,0);
    ::SendMessageW(wizard.window,WM_SYSCOLORCHANGE,0,0);
    CHECK(wizard.Item(20)==folder);CHECK(wizard.Item(25)==desktop);CHECK(wizard.Item(21)==startup);
    CHECK_EQ(Text(folder),std::wstring(L"relative folder"));
    CHECK_EQ(::SendMessageW(desktop,BM_GETCHECK,0,0),LRESULT{BST_CHECKED});
    CHECK_EQ(::SendMessageW(startup,BM_GETCHECK,0,0),LRESULT{BST_UNCHECKED});
    // Back and forward keep the choices.
    wizard.Click(8);
    REQUIRE(wizard.Item(20) == nullptr);
    wizard.Click(1);
    REQUIRE(wizard.Item(25) != nullptr);
    CHECK_EQ(::SendMessageW(wizard.Item(25), BM_GETCHECK, 0, 0), LRESULT{BST_CHECKED});
    CHECK_EQ(::SendMessageW(wizard.Item(21), BM_GETCHECK, 0, 0), LRESULT{BST_UNCHECKED});
    CHECK_FALSE(std::filesystem::exists(wizard.temporary / L"installation"));
}

AETHER_TEST("setup design: repair and uninstall journeys show their own options") {
    platform::ComApartment apartment(COINIT_APARTMENTTHREADED);
    REQUIRE(apartment.IsUsable());
    for(const wchar_t* theme:{L"dark",L"light"}) {
    {
        Wizard repair;
        repair.Start(std::wstring(L"--repair --test-theme ")+theme);
        BackdropGuard backdrop;
        PlaceWizardOverBackdrop(repair,backdrop);
        repair.Click(1);
        repair.Click(1);
        REQUIRE(repair.Item(2) != nullptr);
        CHECK(Text(repair.Item(2)).find(L"Repair") != std::wstring::npos);
        CHECK(repair.Item(21) != nullptr);
        SaveWizardVisual(repair,backdrop,"setup-"+text::Narrow(theme)+"-repair-options");
    }
    {
        Wizard uninstall;
        uninstall.Start(std::wstring(L"--uninstall --test-theme ")+theme);
        BackdropGuard backdrop;
        PlaceWizardOverBackdrop(uninstall,backdrop);
        uninstall.Click(1);
        uninstall.Click(1);
        REQUIRE(uninstall.Item(2) != nullptr);
        CHECK(Text(uninstall.Item(2)).find(L"Uninstall") != std::wstring::npos);
        CHECK(uninstall.Item(24) != nullptr);  // explicit keep/remove data choice
        CHECK(uninstall.Item(21) == nullptr);
        CHECK_EQ(::SendMessageW(uninstall.Item(24), BM_GETCHECK, 0, 0), LRESULT{BST_UNCHECKED});  // data kept by default
        SaveWizardVisual(uninstall,backdrop,"setup-"+text::Narrow(theme)+"-uninstall-options");
        ::SendMessageW(uninstall.Item(24),BM_SETCHECK,BST_CHECKED,0);
        SaveWizardVisual(uninstall,backdrop,"setup-"+text::Narrow(theme)+"-uninstall-remove-data-options");
    }
    }
}

AETHER_TEST("setup design: a cancelled isolated install can be retried successfully") {
    platform::ComApartment apartment(COINIT_APARTMENTTHREADED);
    REQUIRE(apartment.IsUsable());
    for(const wchar_t* theme:{L"dark",L"light"}) {
    Wizard wizard;
    wizard.Start(std::wstring(L"--test-theme ")+theme);
    BackdropGuard backdrop;
    PlaceWizardOverBackdrop(wizard,backdrop);
    wizard.Click(1);
    wizard.Click(1);
    // Hold this test-only worker before extraction, so cancellation always wins before activation.
    REQUIRE_EQ(::SendMessageW(wizard.window, WM_APP + 74, 0, 0), LRESULT{1});
    ::SendMessageW(wizard.window, WM_COMMAND, MAKEWPARAM(2, BN_CLICKED), reinterpret_cast<LPARAM>(wizard.Item(2)));
    REQUIRE(wizard.Item(4) != nullptr);
    SaveWizardVisual(wizard,backdrop,"setup-"+text::Narrow(theme)+"-progress-cancel");
    ::SendMessageW(wizard.window, WM_COMMAND, MAKEWPARAM(4, BN_CLICKED), reinterpret_cast<LPARAM>(wizard.Item(4)));
    const auto waitForDone = [&] {
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(90);
        while (!wizard.Item(3) && std::chrono::steady_clock::now() < end)
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        REQUIRE(wizard.Item(3) != nullptr);
    };
    const auto closeDone = [&] {
        wizard.Click(3);
        REQUIRE_EQ(::WaitForSingleObject(wizard.process.Get(),5000),DWORD{WAIT_OBJECT_0});
        wizard.window=nullptr;
    };
    waitForDone();
    REQUIRE(wizard.Item(8) != nullptr);  // the failed attempt offers Back to Options
    CHECK(HasText(wizard.window,L"Setup was cancelled."));
    CHECK(HasText(wizard.window,L"Go back to Options to try again."));
    CHECK_FALSE(HasText(wizard.window,L"Check file access and free disk space"));
    CHECK_FALSE(std::filesystem::exists(wizard.temporary / L"installation" / L"install-state.json"));
    SaveWizardVisual(wizard,backdrop,"setup-"+text::Narrow(theme)+"-cancelled-retry");
    wizard.Click(8);
    REQUIRE(wizard.Item(20) != nullptr);
    ::SendMessageW(wizard.window, WM_COMMAND, MAKEWPARAM(2, BN_CLICKED), reinterpret_cast<LPARAM>(wizard.Item(2)));
    waitForDone();
    CHECK(wizard.Item(8) == nullptr);  // success, rather than a second pre-cancelled attempt
    auto layout = install::InstallLayout::Create(wizard.temporary / L"installation");
    REQUIRE(layout.ok());
    auto installed = install::LoadState(*layout);
    REQUIRE(installed.ok());
    CHECK_FALSE(installed->activeVersion.empty());
    SaveWizardVisual(wizard,backdrop,"setup-"+text::Narrow(theme)+"-finish");
    closeDone();
    // Exercise the complete maintenance presentation against that same private installation.
    // The gate keeps the progress page visible for inspection without altering deployment policy.
    for(const wchar_t* operation:{L"repair",L"uninstall"}) {
        wizard.Start(std::wstring(L"--")+operation+L" --test-theme "+theme);
        ::SetWindowPos(wizard.window,HWND_TOPMOST,backdrop.area.left+20,backdrop.area.top+10,
            backdrop.area.right-backdrop.area.left-40,backdrop.area.bottom-backdrop.area.top-20,SWP_NOACTIVATE);
        wizard.Click(1);
        wizard.Click(1);
        REQUIRE_EQ(::SendMessageW(wizard.window,WM_APP+74,0,0),LRESULT{1});
        ::SendMessageW(wizard.window,WM_COMMAND,MAKEWPARAM(2,BN_CLICKED),reinterpret_cast<LPARAM>(wizard.Item(2)));
        const bool uninstall=std::wstring_view(operation)==L"uninstall";
        CHECK((wizard.Item(4)==nullptr)==uninstall);
        SaveWizardVisual(wizard,backdrop,"setup-"+text::Narrow(theme)+"-"+text::Narrow(operation)+"-progress");
        REQUIRE(::SendMessageW(wizard.window,WM_APP+75,0,0)!=FALSE);
        waitForDone();
        CHECK(wizard.Item(8)==nullptr);  // successful maintenance outcome
        SaveWizardVisual(wizard,backdrop,"setup-"+text::Narrow(theme)+"-"+text::Narrow(operation)+"-finish");
        closeDone();
    }
    CHECK_FALSE(std::filesystem::exists(wizard.temporary/L"installation"));
    CHECK(std::filesystem::exists(wizard.temporary/L"data"));
    }
}

// Mean luminance of the central page region (capture pixels), away from text-dense header/footer.
double PageLuminance(const Capture& capture) {
    double sum = 0.0;
    int count = 0;
    for (int y = capture.height * 3 / 10; y < capture.height * 7 / 10; y += 3) {
        for (int x = capture.width / 10; x < capture.width * 9 / 10; x += 3) {
            const auto* pixel = capture.bgra.data() + (static_cast<std::size_t>(y) * capture.width + x) * 4;
            sum += 0.0722 * pixel[0] + 0.7152 * pixel[1] + 0.2126 * pixel[2];
            ++count;
        }
    }
    return count ? sum / count : 0.0;
}

AETHER_TEST("setup design: wizard pages render on the form material over a private backdrop") {
    platform::ComApartment apartment(COINIT_APARTMENTTHREADED);
    for (const wchar_t* theme : {L"dark", L"light"}) {
        const bool dark = std::wstring_view(theme) == L"dark";
        Wizard wizard;
        wizard.Start(std::wstring(L"--test-theme ") + theme);
        const RECT safe = SafeArea();
        RECT bounds{};
        ::GetWindowRect(wizard.window, &bounds);
        const int width = std::min<int>(bounds.right - bounds.left, safe.right - safe.left - 40);
        const int height = std::min<int>(bounds.bottom - bounds.top, safe.bottom - safe.top - 20);
        const int left = (safe.left + safe.right - width) / 2, top = (safe.top + safe.bottom - height) / 2;
        RECT guard{left - 20, top - 10, left + width + 20, top + height + 10};
        ::IntersectRect(&guard, &guard, &safe);
        BackdropGuard backdrop;
        backdrop.hwnd = CreateTestBackdrop(guard);
        REQUIRE(backdrop.hwnd != nullptr);
        ::SetWindowPos(wizard.window, HWND_TOPMOST, left, top, width, height, SWP_NOACTIVATE);
        const char* names[] = {"welcome", "licence", "options"};
        for (int page = 0; page < 3; ++page) {
            if (page) wizard.Click(1);
            ::SetWindowPos(wizard.window, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            std::this_thread::sleep_for(std::chrono::milliseconds(700));
            RECT window{};
            ::GetWindowRect(wizard.window, &window);
            const auto capture = CaptureWizard(backdrop.hwnd, guard, window, wizard.pid);
            if (!capture.Valid()) {
                TEST_NOTE("render inconclusive: a foreign window overlapped the private backdrop");
                continue;
            }
            // The page is on the themed material, never the stock white/grey form: dark pages stay dark,
            // light pages light, whatever the private backdrop below.
            const double luminance = PageLuminance(capture);
            TEST_NOTE(std::string("setup ") + text::Narrow(theme) + " " + names[page] + " page luminance " + std::to_string(static_cast<int>(luminance)));
            if (dark) CHECK(luminance < 110.0);
            else CHECK(luminance > 150.0);
            const auto path = InspectionPath(std::string("setup-") + text::Narrow(theme) + "-" + names[page]);
            CHECK(SavePng(capture, path));
        }
    }
}
