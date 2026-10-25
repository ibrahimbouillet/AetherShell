// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// The redesigned Settings window (FR-037 / T47): page layout at several sizes and DPIs, people-facing
// numbers with exact stored values, slider/field synchronisation, native accessible dropdowns,
// theme changes that keep drafts/focus/scroll, the reset confirmation seam, in-place update
// states and own-window renders over a private two-phase backdrop. Settings use temporary files and
// injected callbacks only; nothing touches startup, the taskbar or the real settings.
#include "AetherTest.h"

#include "../ui_form/FormTestSupport.h"

#include "core/FileSystem.h"
#include "graphics/Theme.h"
#include "platform/windows/ComApartment.h"
#include "settings/ui/SettingsWindow.h"

#include <commctrl.h>

#include <chrono>
#include <cmath>
#include <span>
#include <string>
#include <thread>
#include <vector>

using namespace aether;
using namespace std::chrono_literals;
using namespace aether::test::form;

namespace {

template <class Predicate>
bool PumpUntil(ManualDispatcher& dispatcher, Predicate predicate, std::chrono::milliseconds timeout = 5s) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!predicate()) {
        dispatcher.RunPending();
        MSG message{};
        while (::PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { ::TranslateMessage(&message); ::DispatchMessageW(&message); }
        if (std::chrono::steady_clock::now() > deadline) return false;
        std::this_thread::sleep_for(1ms);
    }
    return true;
}

void PumpFor(ManualDispatcher& dispatcher, std::chrono::milliseconds duration) {
    const auto end = std::chrono::steady_clock::now() + duration;
    (void)PumpUntil(dispatcher, [&] { return std::chrono::steady_clock::now() >= end; }, duration + 1s);
}

std::wstring TextOf(HWND control) {
    const int length = ::GetWindowTextLengthW(control);
    std::wstring value(static_cast<std::size_t>(length) + 1, L'\0');
    ::GetWindowTextW(control, value.data(), length + 1);
    value.resize(static_cast<std::size_t>(length));
    return value;
}

struct Fixture {
    platform::ComApartment apartment;
    ManualDispatcher dispatcher;
    std::filesystem::path file;
    settings::SettingsService service;
    explicit Fixture(const char* name)
        : file(test::MakeTempDirectory(name) / L"settings.json"), service(file, dispatcher) {
        INITCOMMONCONTROLSEX common{sizeof(common), ICC_WIN95_CLASSES | ICC_HOTKEY_CLASS};
        ::InitCommonControlsEx(&common);
        REQUIRE(service.Load({}).ok());
        REQUIRE(PumpUntil(dispatcher, [&] { return service.Loaded(); }));
    }
};

// Visible descendants that are actual controls (no containers), in the window's client coordinates.
struct Placed { HWND hwnd; RECT rect; RECT bounds; std::wstring cls; };
std::vector<Placed> VisibleControls(HWND window) {
    struct Collect { HWND window; std::vector<Placed> items; } collect{window, {}};
    ::EnumChildWindows(window, [](HWND child, LPARAM parameter) -> BOOL {
        auto& c = *reinterpret_cast<Collect*>(parameter);
        if (!::IsWindowVisible(child)) return TRUE;
        wchar_t name[64]{};
        ::GetClassNameW(child, name, 64);
        const std::wstring cls(name);
        if (cls.rfind(L"AetherShell.", 0) == 0) return TRUE;  // scroll view containers and indicator
        RECT rect{};
        ::GetWindowRect(child, &rect);
        RECT bounds{};
        ::GetClientRect(c.window, &bounds);
        ::MapWindowPoints(c.window, nullptr, reinterpret_cast<POINT*>(&bounds), 2);
        // Clip vertically to skip off-screen rows. Preserve the original horizontal bounds so
        // an overflowing control cannot pass merely because the viewport hides its right side.
        for (HWND parent = ::GetParent(child); parent && parent != c.window; parent = ::GetParent(parent)) {
            wchar_t parentName[64]{};
            ::GetClassNameW(parent, parentName, 64);
            if (std::wstring_view(parentName) != L"AetherShell.FormScroll.Viewport") continue;
            RECT viewport{};
            ::GetWindowRect(parent, &viewport);
            if (rect.bottom <= viewport.top || rect.top >= viewport.bottom) return TRUE;
            rect.top = std::max(rect.top, viewport.top);
            rect.bottom = std::min(rect.bottom, viewport.bottom);
            bounds.left = viewport.left;
            bounds.right = viewport.right;
        }
        ::MapWindowPoints(nullptr, c.window, reinterpret_cast<POINT*>(&rect), 2);
        ::MapWindowPoints(nullptr, c.window, reinterpret_cast<POINT*>(&bounds), 2);
        c.items.push_back(Placed{child, rect, bounds, cls});
        return TRUE;
    }, reinterpret_cast<LPARAM>(&collect));
    return collect.items;
}

// Every visible control lies within the client width and none overlaps another. Controls scrolled
// out of the viewport are clipped by it, so only horizontal bounds are absolute.
void CheckLayout(HWND window, const char* what) {
    const auto controls = VisibleControls(window);
    CHECK(controls.size() >= 4);
    for (const auto& item : controls) {
        const bool inside = item.rect.left >= item.bounds.left - 1 && item.rect.right <= item.bounds.right + 1 && item.rect.right > item.rect.left;
        if (!inside) TEST_NOTE(std::string(what) + ": control " + std::to_string(::GetDlgCtrlID(item.hwnd)) + " outside the client width");
        CHECK(inside);
    }
    for (std::size_t i = 0; i < controls.size(); ++i) {
        for (std::size_t j = i + 1; j < controls.size(); ++j) {
            RECT overlap{};
            const RECT a = controls[i].rect;
            const RECT b = controls[j].rect;
            // Focus margins of neighbouring buttons may touch; a real overlap is a few pixels deep.
            if (::IntersectRect(&overlap, &a, &b) && overlap.right - overlap.left > 4 && overlap.bottom - overlap.top > 4) {
                TEST_NOTE(std::string(what) + ": controls " + std::to_string(::GetDlgCtrlID(controls[i].hwnd)) + " and " +
                          std::to_string(::GetDlgCtrlID(controls[j].hwnd)) + " overlap");
                CHECK(false);
            }
        }
    }
}

void CheckScrollableLayout(settings::ui::SettingsWindow& window, const char* what) {
    ::SendMessageW(window.Handle(), WM_VSCROLL, SB_TOP, 0);
    for (;;) {
        CheckLayout(window.Handle(), what);
        const auto before = window.ScrollMetrics();
        if (before.position >= before.range) break;
        ::SendMessageW(window.Handle(), WM_VSCROLL, SB_PAGEDOWN, 0);
        if (window.ScrollMetrics().position <= before.position) { CHECK(false); break; }
    }
}

constexpr settings::ui::SettingsPage kPages[] = {
    settings::ui::SettingsPage::Dock, settings::ui::SettingsPage::TopBar, settings::ui::SettingsPage::Weather,
    settings::ui::SettingsPage::Updates, settings::ui::SettingsPage::General, settings::ui::SettingsPage::About,
    settings::ui::SettingsPage::Desktop, settings::ui::SettingsPage::Appearance};
constexpr const char* kPageNames[] = {"dock", "topbar", "weather", "updates", "general", "about", "desktop", "appearance"};

}  // namespace

AETHER_TEST("settings design: every page lays out without overlap at default, small and high-DPI sizes") {
    Fixture f("settings-design-layout");
    settings::ui::SettingsWindow window(f.service, f.dispatcher);
    for (const auto page : kPages) {
        REQUIRE(window.Show(page).ok());
        PumpFor(f.dispatcher, 60ms);
        CheckScrollableLayout(window, kPageNames[static_cast<int>(page)]);
    }
    // A small window: navigation becomes a rail, rows stack, content scrolls, the footer stays.
    const int compactDpi = static_cast<int>(::GetDpiForWindow(window.Handle()));
    ::SetWindowPos(window.Handle(), nullptr, 0, 0, ::MulDiv(420, compactDpi, 96), ::MulDiv(360, compactDpi, 96),
                   SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    for (const auto page : kPages) {
        REQUIRE(window.Show(page).ok());
        PumpFor(f.dispatcher, 40ms);
        CheckScrollableLayout(window, kPageNames[static_cast<int>(page)]);
        RECT apply{}, client{};
        ::GetWindowRect(window.Control(101), &apply);
        ::MapWindowPoints(nullptr, window.Handle(), reinterpret_cast<POINT*>(&apply), 2);
        ::GetClientRect(window.Handle(), &client);
        CHECK(apply.bottom <= client.bottom);
        CHECK(apply.right <= client.right);
    }
    // Synthetic per-monitor DPI changes (144 and 192): the same layout scaled, drafts kept.
    REQUIRE(window.Show(settings::ui::SettingsPage::Dock).ok());
    ::SetWindowTextW(window.Control(200), L"77");
    for (const UINT dpi : {144u, 192u}) {
        RECT bounds{};
        ::GetWindowRect(window.Handle(), &bounds);
        bounds.right = bounds.left + ::MulDiv(900, static_cast<int>(dpi), 96);
        bounds.bottom = bounds.top + ::MulDiv(640, static_cast<int>(dpi), 96);
        ::SendMessageW(window.Handle(), WM_DPICHANGED, MAKEWPARAM(dpi, dpi), reinterpret_cast<LPARAM>(&bounds));
        PumpFor(f.dispatcher, 40ms);
        CheckLayout(window.Handle(), dpi == 144 ? "dock at 144 DPI" : "dock at 192 DPI");
        CHECK_EQ(TextOf(window.Control(200)), std::wstring(L"77"));
    }
    window.Close();
}

AETHER_TEST("settings design: numbers show for people, store exactly and follow their sliders") {
    Fixture f("settings-design-numbers");
    REQUIRE(f.service.Update([](settings::Settings& s) { s.dock.opacity = 0.8; s.dock.magnification = 1.45; }).ok());
    REQUIRE(PumpUntil(f.dispatcher, [&] { return !f.service.Dirty(); }));
    settings::ui::SettingsWindow window(f.service, f.dispatcher);
    REQUIRE(window.Show(settings::ui::SettingsPage::Dock).ok());
    // Binding order: 200 icon size, 201 bottom, 202 side, 203 spacing, 204 opacity, 205 magnification.
    CHECK_EQ(TextOf(window.Control(204)), std::wstring(L"80"));
    CHECK_EQ(TextOf(window.Control(205)), std::wstring(L"1.45"));
    CHECK(TextOf(window.Control(204)).find(L'.') == std::wstring::npos);  // never "0.800000"
    ::SetWindowTextW(window.Control(204), L"65");
    ::SendMessageW(window.Handle(), WM_TIMER, 19, 0);
    REQUIRE(PumpUntil(f.dispatcher, [&] { return std::fabs(f.service.Current().dock.opacity - 0.65) < 1e-9 && !f.service.Dirty(); }));
    // The opacity slider (400 + binding index) follows the field...
    HWND slider = window.Control(404);
    REQUIRE(slider != nullptr);
    CHECK_EQ(::SendMessageW(slider, TBM_GETPOS, 0, 0), LRESULT{35});  // 65 % on 30..100
    // ...and moving it rewrites the field and schedules the same live apply.
    ::SendMessageW(slider, TBM_SETPOS, TRUE, 50);
    ::SendMessageW(::GetParent(slider), WM_HSCROLL, MAKEWPARAM(TB_THUMBPOSITION, 50), reinterpret_cast<LPARAM>(slider));
    CHECK_EQ(TextOf(window.Control(204)), std::wstring(L"80"));
    ::SendMessageW(window.Handle(), WM_TIMER, 19, 0);
    REQUIRE(PumpUntil(f.dispatcher, [&] { return std::fabs(f.service.Current().dock.opacity - 0.8) < 1e-9 && !f.service.Dirty(); }));
    // An incomplete number is rejected with an explanation instead of being guessed.
    ::SetWindowTextW(window.Control(205), L"abc");
    ::SendMessageW(window.Handle(), WM_TIMER, 19, 0);
    PumpFor(f.dispatcher, 50ms);
    CHECK(std::fabs(f.service.Current().dock.magnification - 1.45) < 1e-9);
    CHECK(TextOf(window.Control(103)).find(L"number") != std::wstring::npos);
    window.Close();
}

AETHER_TEST("settings design: compact pages keep complete rows and actions at enlarged text sizes") {
    Fixture f("settings-design-text-scale");
    settings::ui::SettingsWindow window(f.service, f.dispatcher);
    REQUIRE(window.Show(settings::ui::SettingsPage::Dock).ok());
    const int compactDpi = static_cast<int>(::GetDpiForWindow(window.Handle()));
    ::SetWindowPos(window.Handle(), nullptr, 0, 0, ::MulDiv(420, compactDpi, 96), ::MulDiv(520, compactDpi, 96),
                   SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    for (const float scale : {1.5f, 2.25f}) {
        auto tokens = graphics::MakeDesignTokens(graphics::ThemeMode::Dark);
        tokens.textScale = scale;
        window.SetTheme(tokens, graphics::MotionPolicy{.reduced = true});
        for (const auto page : kPages) {
            REQUIRE(window.Show(page).ok());
            PumpFor(f.dispatcher, 20ms);
            CheckScrollableLayout(window, kPageNames[static_cast<int>(page)]);
            RECT apply{}, reset{}, client{};
            ::GetWindowRect(window.Control(101), &apply);
            ::GetWindowRect(window.Control(102), &reset);
            ::MapWindowPoints(nullptr, window.Handle(), reinterpret_cast<POINT*>(&apply), 2);
            ::MapWindowPoints(nullptr, window.Handle(), reinterpret_cast<POINT*>(&reset), 2);
            ::GetClientRect(window.Handle(), &client);
            CHECK(apply.top >= 0 && apply.bottom <= client.bottom);
            CHECK(reset.top >= 0 && reset.bottom <= client.bottom);
        }
    }
    window.Close();
}

AETHER_TEST("settings design: dropdowns keep the native list and selection contract") {
    Fixture f("settings-design-dropdown");
    settings::ui::SettingsWindow window(f.service, f.dispatcher);
    REQUIRE(window.Show(settings::ui::SettingsPage::Appearance).ok());
    HWND mode = window.Control(200);  // Color mode: Follow Windows / Light / Dark
    REQUIRE(mode != nullptr);
    ::SetFocus(mode);
    ::SendMessageW(mode, CB_SHOWDROPDOWN, TRUE, 0);
    REQUIRE(PumpUntil(f.dispatcher, [&] { return ::SendMessageW(mode, CB_GETDROPPEDSTATE, 0, 0) != 0; }));
    COMBOBOXINFO combo{sizeof(combo)};
    REQUIRE(::GetComboBoxInfo(mode, &combo));
    REQUIRE(combo.hwndList != nullptr);
    CHECK(::IsWindowVisible(combo.hwndList));
    CHECK_EQ(::SendMessageW(combo.hwndList, LB_GETCOUNT, 0, 0), LRESULT{3});
    CHECK_EQ(::SendMessageW(combo.hwndList, LB_GETCURSEL, 0, 0), LRESULT{0});
    ::SendMessageW(mode, CB_SHOWDROPDOWN, FALSE, 0);
    CHECK_FALSE(::IsWindowVisible(combo.hwndList));
    // Selection is still represented by the real combo and its normal CBN_SELCHANGE notification.
    ::SendMessageW(mode, CB_SETCURSEL, 2, 0);
    ::SendMessageW(::GetParent(mode), WM_COMMAND, MAKEWPARAM(200, CBN_SELCHANGE), reinterpret_cast<LPARAM>(mode));
    CHECK_EQ(::SendMessageW(mode, CB_GETCURSEL, 0, 0), LRESULT{2});
    ::SendMessageW(window.Handle(), WM_TIMER, 19, 0);
    REQUIRE(PumpUntil(f.dispatcher, [&] { return f.service.Current().general.themeMode == settings::ThemePreference::Dark && !f.service.Dirty(); }));
    // Dismissing the native list changes neither selection nor the current setting.
    ::SendMessageW(mode, CB_SHOWDROPDOWN, TRUE, 0);
    ::SendMessageW(mode, CB_SHOWDROPDOWN, FALSE, 0);
    CHECK_EQ(::SendMessageW(mode, CB_GETCURSEL, 0, 0), LRESULT{2});
    window.Close();
}

AETHER_TEST("settings design: theme changes keep drafts, staged consent, focus and scroll position") {
    Fixture f("settings-design-theme");
    settings::ui::SettingsWindow window(f.service, f.dispatcher);
    REQUIRE(window.Show(settings::ui::SettingsPage::General).ok());
    ::SetWindowPos(window.Handle(), nullptr, 0, 0, 560, 420, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    PumpFor(f.dispatcher, 40ms);
    ::SendMessageW(window.Control(200), BM_CLICK, 0, 0);  // staged startup consent: needs Apply
    CHECK(TextOf(window.Control(103)).find(L"Apply") != std::wstring::npos);
    // Focusing a control lower on the page scrolls it into view; then the position must survive.
    ::SetFocus(window.Control(205));
    PumpFor(f.dispatcher, 60ms);
    const HWND focus = ::GetFocus();
    const int position = window.ScrollMetrics().position;
    CHECK(position > 0);
    for (const auto mode : {graphics::ThemeMode::Light, graphics::ThemeMode::HighContrast, graphics::ThemeMode::Dark}) {
        auto tokens = graphics::MakeDesignTokens(mode);
        window.SetTheme(tokens, graphics::MotionPolicy{});
        PumpFor(f.dispatcher, 40ms);
        CHECK_EQ(::SendMessageW(window.Control(200), BM_GETCHECK, 0, 0), LRESULT{BST_CHECKED});
        CHECK_EQ(window.ScrollMetrics().position, position);
        CHECK(::GetFocus() == focus);
    }
    CHECK_FALSE(f.service.Current().general.startWithWindows);  // nothing was committed
    window.Close();
}

AETHER_TEST("settings design: navigation retains unfinished edits and page-local consent until explicit Apply") {
    Fixture f("settings-design-navigation-drafts");
    int startupCalls = 0;
    settings::ui::SettingsCallbacks callbacks;
    callbacks.setStartup = [&](bool, std::function<void(Result<void>)> complete) { ++startupCalls; complete(Ok()); };
    settings::ui::SettingsWindow window(f.service, f.dispatcher, std::move(callbacks));
    const auto navigate = [&](settings::ui::SettingsPage page) {
        ::SendMessageW(window.Control(100), LB_SETCURSEL, static_cast<WPARAM>(page), 0);
        ::SendMessageW(window.Handle(), WM_COMMAND, MAKEWPARAM(100, LBN_SELCHANGE), reinterpret_cast<LPARAM>(window.Control(100)));
    };
    REQUIRE(window.Show(settings::ui::SettingsPage::Dock).ok());
    HWND number = window.Control(205);
    ::SetWindowTextW(number, L"unfinished");
    ::SetFocus(number);
    ::SendMessageW(number, EM_SETSEL, 2, 5);
    PumpFor(f.dispatcher, 30ms);
    // A repeated --settings/open request must keep the actual editor and its in-progress input.
    REQUIRE(f.service.Update([](settings::Settings& value) { value.dock.iconSize = 64; }).ok());
    REQUIRE(window.Show(settings::ui::SettingsPage::Dock).ok());
    CHECK(window.Control(205) == number);
    CHECK_EQ(TextOf(number), std::wstring(L"unfinished"));
    CHECK_EQ(TextOf(window.Control(200)), std::wstring(L"64"));  // refresh an externally changed, untouched field
    PumpFor(f.dispatcher, 30ms);
    const int dockScroll = window.ScrollMetrics().position;
    navigate(settings::ui::SettingsPage::General);
    ::SendMessageW(window.Control(200), BM_CLICK, 0, 0);  // staged startup: no explicit consent yet
    REQUIRE(f.service.Update([](settings::Settings& value) { value.general.searchHotkey.modifiers = 15; }).ok());
    REQUIRE(window.Show(settings::ui::SettingsPage::General).ok());
    CHECK_EQ(::SendMessageW(window.Control(205), CB_GETCURSEL, 0, 0), LRESULT{14});
    CHECK_EQ(::SendMessageW(window.Control(200), BM_GETCHECK, 0, 0), LRESULT{BST_CHECKED});  // keep the consent draft
    navigate(settings::ui::SettingsPage::Desktop);
    ::SendMessageW(window.Control(200), BM_CLICK, 0, 0);  // staged taskbar opt-out
    navigate(settings::ui::SettingsPage::Dock);
    PumpFor(f.dispatcher, 30ms);
    CHECK_EQ(TextOf(window.Control(205)), std::wstring(L"unfinished"));
    CHECK(::GetFocus() == window.Control(205));
    DWORD selectionStart = 0, selectionEnd = 0;
    ::SendMessageW(window.Control(205), EM_GETSEL, reinterpret_cast<WPARAM>(&selectionStart), reinterpret_cast<LPARAM>(&selectionEnd));
    CHECK_EQ(selectionStart, DWORD{2});
    CHECK_EQ(selectionEnd, DWORD{5});
    CHECK_EQ(window.ScrollMetrics().position, dockScroll);
    CHECK_FALSE(f.service.Current().general.startWithWindows);
    CHECK(f.service.Current().general.replaceWindowsTaskbar);
    CHECK_EQ(startupCalls, 0);
    navigate(settings::ui::SettingsPage::Desktop);
    CHECK_EQ(::SendMessageW(window.Control(200), BM_GETCHECK, 0, 0), LRESULT{BST_UNCHECKED});
    ::SendMessageW(window.Control(101), BM_CLICK, 0, 0);
    REQUIRE(PumpUntil(f.dispatcher, [&] { return !f.service.Current().general.replaceWindowsTaskbar && !f.service.Dirty(); }));
    CHECK_FALSE(f.service.Current().general.startWithWindows);  // Apply commits only the owning page
    CHECK_EQ(startupCalls, 0);
    navigate(settings::ui::SettingsPage::General);
    CHECK_EQ(::SendMessageW(window.Control(200), BM_GETCHECK, 0, 0), LRESULT{BST_CHECKED});
    window.Close();
    REQUIRE(window.Show(settings::ui::SettingsPage::General).ok());
    CHECK_EQ(::SendMessageW(window.Control(200), BM_GETCHECK, 0, 0), LRESULT{BST_UNCHECKED});
    CHECK_EQ(startupCalls, 0);  // closing discards the staged startup choice
    window.Close();
}

AETHER_TEST("settings design: reset asks through the confirmation seam and declining keeps everything") {
    Fixture f("settings-design-reset");
    REQUIRE(f.service.Update([](settings::Settings& s) { s.dock.iconSize = 64; s.general.replaceWindowsTaskbar = false; }).ok());
    REQUIRE(PumpUntil(f.dispatcher, [&] { return !f.service.Dirty(); }));
    int asked = 0;
    bool answer = false;
    settings::ui::SettingsCallbacks callbacks;
    callbacks.confirm = [&](const settings::ui::SettingsConfirmation& question) {
        ++asked;
        CHECK(question.message.find(L"default") != std::wstring::npos);
        CHECK(question.detail.find(L"taskbar") != std::wstring::npos);
        return answer;
    };
    settings::ui::SettingsWindow window(f.service, f.dispatcher, std::move(callbacks));
    REQUIRE(window.Show(settings::ui::SettingsPage::Dock).ok());
    ::SendMessageW(window.Control(102), BM_CLICK, 0, 0);
    CHECK_EQ(asked, 1);
    PumpFor(f.dispatcher, 50ms);
    CHECK_EQ(f.service.Current().dock.iconSize, 64);
    answer = true;
    ::SendMessageW(window.Control(102), BM_CLICK, 0, 0);
    REQUIRE(PumpUntil(f.dispatcher, [&] { return f.service.Current().dock.iconSize == 40 && !f.service.Dirty(); }));
    CHECK_EQ(asked, 2);
    CHECK_FALSE(f.service.Current().general.replaceWindowsTaskbar);  // D23: the taskbar choice is kept
    window.Close();
}

AETHER_TEST("settings design: update states refresh in place and keep focus and drafts") {
    Fixture f("settings-design-updates");
    int checks = 0;
    settings::ui::SettingsCallbacks callbacks;
    callbacks.checkForUpdates = [&] { ++checks; };
    settings::ui::SettingsWindow window(f.service, f.dispatcher, std::move(callbacks));
    REQUIRE(window.Show(settings::ui::SettingsPage::Updates).ok());
    ::SetWindowTextW(window.Control(201), L"36");
    ::SetFocus(window.Control(201));
    const HWND summary = window.Control(317);
    const HWND install = window.Control(302);
    updates::UpdateSnapshot update;
    update.state = updates::UpdateState::Checking;
    window.SetUpdateSnapshot(update);
    CHECK(::IsWindowVisible(window.Control(322)));  // indeterminate progress while checking
    CHECK_FALSE(::IsWindowEnabled(install));
    update.state = updates::UpdateState::UpdateAvailable;
    update.manifest = std::make_shared<updates::UpdateManifest>();
    update.downloadSize = 4u * 1024u * 1024u;
    window.SetUpdateSnapshot(update);
    CHECK(::IsWindowEnabled(install));
    CHECK(TextOf(window.Control(323)).find(L"available") != std::wstring::npos);
    CHECK(TextOf(summary).find(L"MB") != std::wstring::npos);
    update.state = updates::UpdateState::Downloading;
    update.progress = {256, 0};
    window.SetUpdateSnapshot(update);
    CHECK(TextOf(summary).find(L'%') == std::wstring::npos);  // unknown total: no invented percentage
    update.progress.total = 1024;
    window.SetUpdateSnapshot(update);
    CHECK(TextOf(summary).find(L"25%") != std::wstring::npos);
    CHECK_FALSE(::IsWindowEnabled(install));
    update.state = updates::UpdateState::ReadyToInstall;
    update.prepared = updates::PreparedUpdate{};
    window.SetUpdateSnapshot(update);
    CHECK(TextOf(summary).find(L"signed") != std::wstring::npos);
    CHECK(TextOf(install).find(L"Install") != std::wstring::npos);
    CHECK_FALSE(::IsWindowVisible(window.Control(322)));
    // Same controls throughout: the draft and focus were never rebuilt away.
    CHECK(window.Control(317) == summary);
    CHECK_EQ(TextOf(window.Control(201)), std::wstring(L"36"));
    CHECK(::GetFocus() == window.Control(201));
    ::SendMessageW(window.Control(301), BM_CLICK, 0, 0);
    CHECK_EQ(checks, 1);
    window.Close();
}

AETHER_TEST("settings design: pages render on the form material over a private backdrop") {
    Fixture f("settings-design-render");
    settings::ui::SettingsWindow window(f.service, f.dispatcher);
    REQUIRE(window.Show(settings::ui::SettingsPage::Dock).ok());
    const RECT safe = SafeArea();
    const int width = std::min<int>(safe.right - safe.left - 40, ::MulDiv(820, static_cast<int>(::GetDpiForWindow(window.Handle())), 96));
    const int height = std::min<int>(safe.bottom - safe.top - 20, ::MulDiv(600, static_cast<int>(::GetDpiForWindow(window.Handle())), 96));
    const int left = (safe.left + safe.right - width) / 2;
    const int top = (safe.top + safe.bottom - height) / 2;
    ::SetWindowPos(window.Handle(), HWND_TOPMOST, left, top, width, height, SWP_NOACTIVATE);
    RECT guard{};
    ::GetWindowRect(window.Handle(), &guard);
    ::InflateRect(&guard, 24, 24);
    ::IntersectRect(&guard, &guard, &safe);
    RECT bounds{};
    ::GetWindowRect(window.Handle(), &bounds);
    ::UnionRect(&guard, &guard, &bounds);
    BackdropGuard backdrop;
    backdrop.hwnd = CreateTestBackdrop(guard);
    REQUIRE(backdrop.hwnd != nullptr);
    ::SetWindowPos(window.Handle(), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    for (const auto mode : {graphics::ThemeMode::Dark, graphics::ThemeMode::Light}) {
        window.SetTheme(graphics::MakeDesignTokens(mode), graphics::MotionPolicy{});
        for (const auto page : kPages) {
            REQUIRE(window.Show(page).ok());
            ::SetWindowPos(window.Handle(), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            PumpFor(f.dispatcher, 600ms);
            RECT client{};
            ::GetClientRect(window.Handle(), &client);
            ::MapWindowPoints(window.Handle(), nullptr, reinterpret_cast<POINT*>(&client), 2);
            SetBackdropPhase(backdrop.hwnd, false);
            PumpFor(f.dispatcher, 250ms);
            const auto first = CaptureWithRetry(backdrop.hwnd, guard, client);
            if (!first.Valid()) {
                TEST_NOTE("render inconclusive: a foreign window overlapped the private backdrop");
                continue;
            }
            const auto path = InspectionPath(std::string("settings-") + (mode == graphics::ThemeMode::Dark ? "dark-" : "light-") +
                                             kPageNames[static_cast<int>(page)]);
            CHECK(SavePng(first, path));
            TEST_NOTE("saved " + path.string());
            if (page == settings::ui::SettingsPage::Dock) {
                // The value field of the icon size is an opaque native editor: identical over both phases.
                SetBackdropPhase(backdrop.hwnd, true);
                PumpFor(f.dispatcher, 250ms);
                const auto second = CaptureWithRetry(backdrop.hwnd, guard, client);
                if (second.Valid()) {
                    RECT field{};
                    ::GetWindowRect(window.Control(200), &field);
                    ::OffsetRect(&field, -client.left, -client.top);
                    ::InflateRect(&field, -2, -2);
                    CHECK_EQ(MaxDifference(first, second, field), 0);
                }
            }
        }
    }
    window.Close();
}
